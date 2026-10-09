/*
        Expansion.

        POSIX fixes the order and the order is the substance of it: tilde, then
        parameter, then command substitution and arithmetic, then field
        splitting, then pathname expansion, then quote removal. A star that came
        out of an unquoted variable is a glob and a star that came out of "$x" is
        a star, and nothing but that order tells the two apart -- which is why
        every byte here carries a mark from the moment it is produced until the
        fields are handed back, and why quote removal is not a pass at all but a
        thing that has already happened by the time anybody looks.

        Nothing allocates. One arena holds the words a line expands to and is
        emptied when the next line begins.
*/

#include "../lib.util.c"

/*
        What the files beside this one own.

        Named rather than reached for, so this file can be included on either
        side of builtin.c without the two of them needing each other first.
*/
PURE string_address env_get(const_string name);
string_address env_get_hashed_span(const_string name, positive length,
                                   positive hash,
                                   positive address_to value_length);
positive env_names_prefix(string_address prefix, positive length,
                          string_address address_to names, positive room);
PURE bool env_readonly(const_string name);
PURE bool env_assignment_readonly_hashed_span(const_string name,
                                              positive length,
                                              positive hash);
bool env_assign(const_string name, const_string value);
//      Where a diagnostic came from, written through the caller's writer:
//      these lines go out unbuffered, so the prefix has to travel with them.
static COLD fn shell_diagnostic_where_to(writer write);
COLD fn shell_prompt_written(writer write, string_address text);
COLD string_address shell_prompt_expand(string_address text, bool nested);

fn exec_tested_forget();

static COLD fn expand_where()
{
        shell_diagnostic_where_to(writer_stderr_once);
}

/*
        The original `${...}` as bash writes it, or dash's capitalised
        sentence with no word. close points at the closing brace.
*/
static COLD fn expand_bad_substitution(string_address whole, string_address close)
{
        expand_where();
        if (!shell_bash_compat)
        {
                writer_stderr_once(str("Bad substitution\n"));
                return;
        }

        writer_stderr_once(whole, (positive)(close - whole + 1));
        writer_stderr_once(str(": bad substitution\n"));
}

// Resolve LC_CTYPE only for character operations. Prefix assignments, locals,
// unset and restoration still have to agree, so this is not a process-lifetime
// answer: env_locale_touch bumps the generation when LC_ALL, LC_CTYPE or LANG
// is written, hidden or dropped, and the next character operation rereads.
// Other encodings retain the byte path; UTF-8 names may include an @modifier.
static positive env_locale_generation = 1;
static positive env_locale_cached;
static bool env_locale_utf8;

static inline INLINE fn env_locale_touch(const_string name, positive length)
{
        if (length == 4)
        {
                if (memory_is_4(name, 'L', 'A', 'N', 'G'))
                        env_locale_generation++;
                return;
        }
        if (length == 6)
        {
                if (memory_is_4(name, 'L', 'C', '_', 'A') && name[4] == 'L' &&
                    name[5] == 'L')
                        env_locale_generation++;
                return;
        }
        if (length == 8 && memory_is_4(name, 'L', 'C', '_', 'C') &&
            memory_is_4(name + 4, 'T', 'Y', 'P', 'E'))
                env_locale_generation++;
}

static HOT bool shell_utf8_on()
{
        string_address locale;
        string_address code;

        if (env_locale_cached == env_locale_generation)
                return env_locale_utf8;

        // These names never vary. Supply their compile-time DJB2 hashes to
        // the same span lookup used by prepared parameter expansions, instead
        // of scanning and hashing a literal on every character operation.
        static const positive all_hash =
            ((((((positive)5381 * 33 + 'L') * 33 + 'C') * 33 + '_') * 33 + 'A') * 33 + 'L') * 33 + 'L';
        static const positive type_hash =
            ((((((((positive)5381 * 33 + 'L') * 33 + 'C') * 33 + '_') * 33 + 'C') * 33 + 'T') * 33 + 'Y') * 33 + 'P') * 33 + 'E';
        static const positive lang_hash =
            ((((positive)5381 * 33 + 'L') * 33 + 'A') * 33 + 'N') * 33 + 'G';
        locale = env_get_hashed_span("LC_ALL", 6, all_hash, null);
        if (!locale || !locale[0])
                locale = env_get_hashed_span("LC_CTYPE", 8, type_hash, null);
        if (!locale || !locale[0])
                locale = env_get_hashed_span("LANG", 4, lang_hash, null);
        env_locale_cached = env_locale_generation;
        if (!locale || !locale[0] || (locale[0] == 'C' && !locale[1]))
        {
                env_locale_utf8 = false;
                return false;
        }

        code = string_first_of(locale, '.');
        code = code ? code + 1 : locale;
        if (byte_to_lower(code[0]) != 'u' ||
            byte_to_lower(code[1]) != 't' ||
            byte_to_lower(code[2]) != 'f')
        {
                env_locale_utf8 = false;
                return false;
        }
        code += 3;
        if (*code == '-')
                code++;
        env_locale_utf8 = code[0] == '8' && (!code[1] || code[1] == '@');
        return env_locale_utf8;
}

static inline INLINE PURE bool expand_bytes_ascii(string_address text,
                                                  positive size)
{
        return memory_escape_index(text, size, HEX_HIGH) == size;
}

static inline INLINE positive expand_character_count(string_address text,
                                                     positive size)
{
        if (!size || expand_bytes_ascii(text, size))
                return size;
        return shell_utf8_on() ? memory_utf8_span(text, size, positive_max).y
                               : size;
}

static PURE positive expand_character_width(string_address text, positive size)
{
        return memory_utf8_span(text, size, 1).x;
}

static PURE positive expand_character_previous(string_address text, positive size)
{
        if (!size)
                return 0;
        positive at = size > 4 ? size - 4 : 0;
        for (; at + 1 < size; at++)
                if (text[at] >= 0xc2 &&
                    expand_character_width(text + at, size - at) == size - at)
                        return at;
        return size - 1;
}

// The matcher has terminated strings rather than spans. Inspect at most four
// bytes, stopping at NUL before asking the same bounded library primitive.
static PURE positive expand_character_step(string_address text)
{
        positive size = 0;
        while (size < 4 && text[size])
                size++;
        return expand_character_width(text, size);
}

/*
        The Bash variable attributes, and the array surface built on them.

        Arrays are stored beside the environment table in builtin.c, which is
        compiled after this file, so what the expander needs of them is named
        here the way every other cross-file name in this block is. An item is
        the one shape both kinds hand back: an indexed element is named by
        its subscript and a keyed one by its bytes, and ${a[@]}, ${!a[@]} and
        declare -p walk either with one loop.
*/
#define SHELL_ARRAY_INDEXED 1
#define SHELL_ARRAY_ASSOCIATIVE 2
#define SHELL_ARRAY_EITHER 3
#define SHELL_ARRAY_INTEGER 4
#define SHELL_ARRAY_LOWER 8
#define SHELL_ARRAY_UPPER 16
#define SHELL_ARRAY_NAMEREF 32
// Whether the array has been given a value at all. `declare -A m` prints no
// element list and `m=()` prints an empty one, and nothing else tells them
// apart once both hold nothing.
#define SHELL_ARRAY_ASSIGNED 64
// Readonly is a property of the dynamically visible variable, not of the
// process-lifetime spelling of its name. Keeping it in the same spare byte as
// the other declaration attributes lets a function local save and restore it
// without a second scope table or another lookup on every assignment.
#define SHELL_ARRAY_READONLY 128

typedef struct
{
        positive index;
        // The key bytes of an associative element, and null for a subscript.
        string_address key;
        positive key_length;
        string_address value;
        positive value_length;
} shell_array_item;

PURE p8 shell_variable_attributes(const_string name, positive length);
string_address shell_nameref_target(const_string name, positive length);
PURE p8 shell_array_attributes(const_string name, positive length);
PURE bool shell_variable_exported(const_string name, positive length);
bool shell_variable_attribute_set(const_string name, positive length,
                                  p8 set, p8 clear);
positive shell_array_length(const_string name, positive length);
PURE positive shell_array_highest(const_string name, positive length);
positive shell_array_items(const_string name, positive length,
                           shell_array_item address_to items, positive room);
string_address shell_array_get(const_string name, positive length,
                               const_string key, positive key_length,
                               positive address_to value_length);
bool shell_array_set(const_string name, positive length, const_string key,
                     positive key_length, const_string value, bool append);
bool shell_array_forget(const_string name, positive length,
                        const_string key, positive key_length);
bool shell_array_clear(const_string name, positive length);
bool shell_reference_resolve(const_string name, positive length,
                             const_string address_to resolved_name,
                             positive address_to resolved_length);
bool shell_reference_element(const_string name, positive length,
                             const_string address_to base,
                             positive address_to base_length,
                             const_string address_to subscript,
                             positive address_to subscript_length);
string_address shell_reference_element_value(
    const_string name, positive length, positive address_to value_length);
bool shell_frames_wanted(const_string name, positive length);
bool shell_array_words(const_string name, positive length,
                       string_address address_to words, positive count);
bool shell_array_numbers(const_string name, positive length,
                         bipolar address_to values, positive count);
bool shell_compound_assign(string_address name, positive length,
                           string_address body, positive body_length,
                           bool append);
string_address shell_expand_subscript(string_address name, positive length,
                                      string_address subscript,
                                      positive subscript_length,
                                      positive address_to key_length);
positive shell_expand_fields(string_address word, shell_words address_to out);
fn run_line(string_address line);
fn shell_input_end();
fn parse_reset_all();
fn shell_trap_exit();
fn exec_child_began();
static COLD fn exec_abort_line(b32 status);
COLD fn exec_expand_input_error();
string_address shell_flags_current();
bool shell_tool_only_here(string_address name, positive2 named);
bool exec_function_here_hashed(string_address name, positive2 named);
string_address alias_lookup(string_address name);
bipolar shell_spawn_tool(string_address address_to arguments,
                         b32 output, bool quiet);

extern b32 shell_status;
extern b32 shell_is_interactive;

// The set flags, one bit per letter. Only -f is anybody's business here:
// it says a pattern is a word and not a question about the filesystem.
extern positive shell_options;

#define SHELL_NO_GLOB ((positive)1 << ('f' - 'a'))

/*
        What the last command substitution answered.

        Bash exposes that number as $? for the rest of the same word, so
        `echo $(exit 2)$?` prints 2. Bash --posix and dash keep the status from
        before the command until the command itself finishes: there it prints 0
        after `echo $(exit 2)$?` at the start of a shell (the status of nothing
        yet), and 1 after `false`.
*/
b32 shell_substitution_status;
positive shell_substitution_generation HOT_STATE;

#define EXPAND_LOCAL_NAME 128
#define EXPAND_LOCAL_TEXT 1024

// Linux accepts a pathname of 4095 bytes in one syscall. Walking beyond this
// floor would require directory-relative opens; never quietly shorten it.
#define GLOB_PATH 4096
#define GLOB_DEPTH 64

/*
        What a byte is allowed to become later.

        PLAIN   literal and unquoted: a glob character, never a separator
        FIELD   came out of an unquoted expansion: globs and splits
        QUOTED  was quoted or escaped: neither, whatever it looks like
        BREAK   a field boundary $@ put there on purpose
*/
#define MARK_PLAIN 0
#define MARK_FIELD 1
#define MARK_QUOTED 2
#define MARK_BREAK 3
// A quoted piece that produced no bytes. It is not a byte of the word: it
// stands where "" stood so that splitting knows a field was there, and every
// other reader of the text steps over it.
#define MARK_EMPTY 4
// An unquoted sequence boundary preserves separate words, but not implicit
// null arguments. MARK_BREAK is the quoted boundary that preserves both.
#define MARK_SEPARATE 5

#define EXPAND_PARAMETER_INDIRECT 1
#define EXPAND_PARAMETER_MISSING 2

//      Set while an assignment's value is expanded: the word of a ${x-word}
//      inside it takes the value's tilde rules, after each colon as well,
//      as bash and dash both do: x=${u-~:~} is two home directories; and
//      bash joins an unquoted $@ there with blanks, as it joins "$@".
static bool expand_assigning;

//      Set while the word of a ${name-word} is expanded in bash: inside
//      double quotes a backslash in front of the } hides it and goes, and
//      a double quote met there opens a run in which a backslash goes in
//      front of anything, since those quotes toggle the outer ones.
static bool expand_brace_word;
static bool expand_double_bare;

/*
        Whether an unquoted list comes out as fields, which is how it is
        joined with blanks, or joined by IFS. An assigned $@ and ${a[@]} is
        joined with blanks in bash -- as a slice, a substitution and a case
        change of one are -- while a trim or a transform of one is joined by
        IFS, and an assigned $* with IFS empty is joined by nothing.
*/
#define expand_list_by_ifs(form, between)                                   \
        (!(between) && !(expand_assigning && shell_bash_compat &&           \
                         (form) == '*'))
#define expand_list_blanks(form, between)                                   \
        (expand_list_by_ifs(form, between) ||                               \
         (expand_assigning && shell_bash_compat && (form) == '@'))

//      One word being built, and what each of its bytes is allowed to become.
//      "$@" against a directory's worth of parameters is a single word, so
//      this grows with it. Everything here is reached by index, never by an
//      address kept across a push, so the two blocks may move.
static p8 address_to expand_text;
static positive expand_text_room HOT_STATE;
static p8 address_to expand_mark;
static positive expand_mark_room HOT_STATE;
static positive expand_length HOT_STATE;
static bool expand_overflow HOT_STATE;
static bool expand_quoted_seen HOT_STATE;
// How many of the bytes in the buffer are empty marks and not bytes of the
// word, so that "the word expanded to nothing" can still be asked.
static positive expand_empty_count HOT_STATE;
static bool expand_failed HOT_STATE;
// A here-document expanded in this process (dash) turns ${x?} into the
// command's status rather than ending the script, so ${x:=} can still stick.
static bool expand_redirect_error;
// PS4 is being expanded for a trace line: an error there is reported and
// the prompt written as it stands, and the command still runs, in both
// references.
static bool expand_errors_soft;
static bool expand_name_at_empty;
static bool expand_explicit_empty;
static positive expand_depth;

static inline INLINE fn expand_fail_state()
{
        expand_overflow = true;
        expand_failed = true;
}

/*
        Where a finished word lives.

        argv points in here and argv is handed to execve, so these bytes may
        not move once given out: the store chains another block on instead of
        reallocating. It used to be a fixed arena that started over from the
        beginning when it filled, which did not truncate a long line so much as
        quietly write its later words on top of its earlier ones.
*/
static shell_store expand_store HOT_STATE;

//      What a substitution running in this shell must not free: the words
//      the command that holds it has already made. Its lines end with the
//      store rewound to here and not to the start.
static shell_mark expand_store_floor;
static bool expand_store_floored HOT_STATE;

// The line is over and every word it made is dead with it.
HOT fn shell_expand_reset()
{
        if (expand_store_floored)
                shell_store_rewind(address_of expand_store, expand_store_floor);
        else
                shell_store_reset(address_of expand_store);
}

//      Room for want bytes in both halves at once, since a byte and its mark
//      are always written together.
#define expand_room(want)                                                    \
        shell_byte_pair_room(expand_text, expand_text_room, expand_mark,     \
                             expand_mark_room, (want))

// One short of the end, because trimming writes a terminator at the length.
static fn expand_push(p8 value, p8 mark)
{
        if_rare(!expand_room(expand_length + 2))
        {
                expand_overflow = true;
                return;
        }

        expand_mark[expand_length] = mark;
        expand_text[expand_length++] = value;
}

// A run that all comes out the same way, which is a copy and a fill.
static HOT fn expand_push_run(const_string text, positive length, p8 mark)
{
        if_rare(!expand_room(expand_length + length + 2))
        {
                expand_overflow = true;
                return;
        }

        memory_copy_apart(expand_text + expand_length, text, length);
        memory_fill(expand_mark + expand_length, mark, length);
        expand_length += length;
}

/*
        A quoted piece that produced nothing still stands in the word.

        "" is an empty field, and ""$x with a blank in x is that empty field
        and then a separator -- one field, where $x alone is none. Nothing in
        the bytes says where the quotes stood once they have produced none,
        so a byte that is not a byte stands there instead, marked empty. It
        decides splitting and nothing else: every reader that copies the text
        out steps over it, and expand_drop_empty removes it where the marks
        have stopped mattering.
*/
static fn expand_push_empty()
{
        positive before = expand_length;

        expand_push(0, MARK_EMPTY);

        if (expand_length != before)
                expand_empty_count++;
}

// The empty marks out of the text, once nothing more will ask the marks.
static string_address expand_ifs();

static fn expand_drop_empty()
{
        positive used = 0;

        //      Where "$@" is not a list of words dash joins it as it joins
        //      "$*": on the first byte of IFS, and on nothing when IFS is
        //      empty. What is left here is one word, so every boundary the
        //      list made is such a join.
        if (shell_dash_compat)
        {
                p8 join = string_get(expand_ifs());

                for (positive at = 0; at < expand_length; at++)
                        if (expand_mark[at] == MARK_BREAK ||
                            expand_mark[at] == MARK_SEPARATE)
                        {
                                if (join)
                                        expand_text[at] = join;
                                else
                                {
                                        expand_mark[at] = MARK_EMPTY;
                                        expand_empty_count++;
                                }
                        }
        }

        if (!expand_empty_count)
                return;

        for (positive at = 0; at < expand_length; at++)
        {
                if (expand_mark[at] == MARK_EMPTY)
                        continue;

                expand_text[used] = expand_text[at];
                expand_mark[used] = expand_mark[at];
                used++;
        }

        expand_length = used;
        expand_empty_count = 0;
}

static fn expand_push_string(string_address text, p8 mark)
{
        if (text)
                expand_push_run(text, string_length(text), mark);
}

/*
        What a byte can be without anything having to look at it.

        plain    outside quotes: not an escape, a quote, a dollar or a backtick
        inside   within a double quote, where the single quote is a byte again
*/
static b8 expand_plain_set[STRING_SET_BYTES] HOT_STATE;
static b8 expand_inside_set[STRING_SET_BYTES] HOT_STATE;
KEEP __attribute__((externally_visible)) b8 expand_literal_set[STRING_SET_BYTES];
static b32 expand_sets_ready HOT_STATE;

static HOT fn expand_sets_prepare()
{
        if (expand_sets_ready)
                return;

        memory_fill(expand_plain_set + 1, 1, STRING_SET_BYTES - 1);
        memory_fill(expand_inside_set + 1, 1, STRING_SET_BYTES - 1);
        memory_fill(expand_literal_set + 1, 1, STRING_SET_BYTES - 1);

        {
                static const string_address inside = "\\\"$`";

                for (positive i = 0; inside[i]; i++)
                        expand_plain_set[inside[i]] = expand_inside_set[inside[i]] = 0;
        }

        expand_plain_set['\''] = 0;

        //      <( and >( are decided one byte at a time below, so neither
        //      byte may be swallowed by a run. Both are rare inside a word,
        //      which is where the run is.
        expand_plain_set['<'] = expand_plain_set['>'] = 0;

        {
                //      < and > are in here for the head of a process
                //      substitution. Neither can begin one on its own, but a
                //      word holding either is rare enough that asking again
                //      inside the expander costs less than a second scan
                //      would in front of every word that holds neither.
                static const string_address changes = "'\"\\$`*?[{~<>(";

                for (positive i = 0; changes[i]; i++)
                        expand_literal_set[changes[i]] = 0;
        }

        expand_sets_ready = true;
}

/*
        Start an expansion with nothing carried over from the last one.

        Three entry points begin here -- a word, an arithmetic body, and the
        dollar in a here-document -- and each of them cleared the same six
        pieces of state in the same order. A seventh piece added to two of the
        three is the bug that shape invites: the third then sees whatever the
        second left behind, which is a wrong answer that depends on what ran
        before it.
*/
static fn expand_begin()
{
        expand_sets_prepare();

        expand_length = 0;
        expand_empty_count = 0;
        expand_overflow = false;
        expand_quoted_seen = false;
        expand_failed = false;
        expand_name_at_empty = false;
        expand_explicit_empty = false;
        expand_depth = 0;
}

/*
        Whether expansion is provably the identity operation.

        The lexer has already made blanks and operators token boundaries. If
        none of the bytes that can quote, substitute, glob, brace-expand or
        begin tilde expansion is present, the parser's stable word is already
        the final field. string_span_max is the hardware-floor set scan; this
        function adds only the shell-specific policy.
*/
bool shell_expand_literal(string_address word, positive length)
{
        expand_sets_prepare();

        /* A bracket starts a glob only when another bracket can close it.
           The overwhelmingly common lone `[` is the test builtin name and
           expansion is provably the identity operation for that one byte. */
        return string_span_max(word, length, expand_literal_set) == length ||
               (length == 1 && string_is(word, '['));
}

/*
        How long the name at at runs and its DJB2 hash, in one pass: x the
        hash string_hash_33_length and memory_hash_33 give the same bytes,
        y the length. A name read out of a word used to be spanned, copied
        out to be terminated and hashed again from the copy.
*/
static PURE positive2 expand_name_hash(string_address at)
{
        positive hash = 5381;
        positive length = 0;
        p8 seen;

        while (string_set_name[seen = string_get(at + length)])
        {
                hash = hash * 33 + seen;
                length++;
        }

        return (positive2){{hash, length}};
}

#define expand_name_character(value) (byte_is_alnum(value) || (value) == '_')
#define expand_assignable_name(name)                                        \
        (byte_is_alpha(string_get(name)) || string_is((name), '_'))

// A scalar parameter name after Bash's indirect ${!name}. Arrays have their
// own grammar and representation and are deliberately not smuggled in here.
static bool expand_parameter_name(string_address name, positive length)
{
        p8 first = string_get(name);
        bool numeric;

        if (!length)
                return false;

        if (length == 1 && (first == '@' || first == '*' || first == '#' ||
                            first == '?' || first == '$' || first == '!' ||
                            first == '-'))
                return true;

        numeric = byte_is_digit(first);

        if (!numeric && !expand_assignable_name(name))
                return false;

        return string_span_max(name, length, numeric ? string_set_digits
                                                     : string_set_name) == length;
}

/*
        Keep the common case on the stack and spill only a long value into the
        stable line store. This is one policy for names, operator words and
        arithmetic bodies instead of three bounded copies with three subtly
        different truncation points.
*/
static string_address expand_hold(string_address text, positive length,
                                  p8 address_to scratch, positive scratch_room)
{
        p8 address_to held;

        if (length < scratch_room)
                held = scratch;
        else
                held = shell_store_take(address_of expand_store, length + 1);

        if (!held)
        {
                expand_fail_state();
                return null;
        }

        memory_copy_end(held, text, length);
        return held;
}

/*
        A number in the three bases C gives it and the shell inherits: 0x is
        sixteen, a leading zero is eight, anything else is ten. Ten was the
        only one read, so $((0x10)) came out as nothing and a mode written
        0644 as six hundred and forty four.

        The cursor is handed back one past the last digit, because arithmetic
        reads a literal out of the middle of an expression and a variable is
        read whole -- and both have to agree about what 010 is worth.

        empty_hex_ok is one personality the two shells split on here.
        Bash reads a bare 0x as zero. dash refuses it, both as a literal
        and as the whole value of a name. The other is overflow: a limit of
        nought wraps the digits modulo 2^64, as bash reads them, where dash
        stops at the limit.
*/
static positive expand_base_positive(string_address address_to at,
                                     bool address_to valid, positive limit,
                                     bool empty_hex_ok)
{
        string_address step = address_to at;
        positive value = 0;
        positive base = 10;
        positive used = 0;

        address_to valid = true;

        if (string_is(step, '0') &&
            (string_is(step + 1, 'x') || string_is(step + 1, 'X')))
        {
                step += 2;
                base = 16;
        }
        else if (string_is(step, '0') && byte_is_digit(string_get(step + 1)))
        {
                step++;
                base = 8;
        }

        /* The limit and base do not change with the digit. Computing their
           quotient once keeps hardware division out of the character loop;
           the remainder is the largest final digit accepted at the cutoff. */
        positive cutoff = limit / base;
        positive last = limit % base;

        while (1)
        {
                positive digit = digit_known(string_get(step), 16);

                if (digit >= base)
                        break;

                /*
                        Arithmetic is a signed machine word. dash saturates a
                        literal that does not fit instead of letting the input
                        helper wrap its unsigned accumulator and hand a
                        plausible negative value to the expression. Consume
                        every digit after saturation so the parser still lands
                        at the real operator or end of the expression.
                */
                if (limit && (value > cutoff || (value == cutoff && digit > last)))
                        value = limit;
                else
                        value = value * base + digit;

                used++;
                step++;
        }

        /* A leftover letter is still this constant, not a new token:
           0xg and 0x10g are invalid hex. A bare 0x is zero only when
           empty_hex_ok says so -- bash, not dash. */
        if (expand_name_character(string_get(step)))
                address_to valid = false;
        else if (!used && (base != 16 || !empty_hex_ok))
                address_to valid = false;

        address_to at = step;

        return value;
}

static bipolar expand_base_number(string_address address_to at, bool address_to valid,
                                  bool empty_hex_ok)
{
        return (bipolar)expand_base_positive(at, valid,
                                             empty_hex_ok ? 0 : (positive)bipolar_max,
                                             empty_hex_ok);
}

/*
        A bracket set: where it ends, and whether a character is in it.

        A [ with no ] anywhere after it is a plain [ and not a set at all, which
        is why the end is found first and the membership asked second.

        POSIX inverts a class with !. Bash also takes ^, the regex letter, so
        [^0-9] and [!0-9] are the same class there and two different classes
        on dash: the caret is an ordinary member. The closer and the membership
        walk have to agree, because [^]] is "not a bracket" in bash and the
        class of a caret, then a leftover bracket, in dash.
*/
/* A bracket set opened by ! or ^ is the complement. dash 0.5.13 negates on
   ^ too, in case and in globs, so the caret is not bash's alone:
   case b in [^a]) matched nothing under the dash and sh names. */
static inline INLINE bool expand_set_inverts(string_address at)
{
        return string_is(at, '!') || string_is(at, '^');
}

static PURE string_address expand_set_end(string_address at)
{
        string_address step = at + 1;

        if (expand_set_inverts(step))
                step++;

        // The first ] is a member, not the close.
        if (string_is(step, ']'))
                step++;

        while (string_get(step) && string_not(step, ']'))
        {
                string_address past = byte_class_end(step, null);

                if (past)
                        step = past;
                else if (string_is(step, '\\') && string_not(step + 1, end))
                        step += 2;
                else
                        step++;
        }

        return string_get(step) ? step : null;
}

/* Decode only after the shared bounded scanner has validated the width.
   Invalid bytes occupy a disjoint range, not the code point of a valid UTF-8
   character. A null bound means a terminated subject, never a pattern span. */
static inline INLINE p32 expand_set_character(string_address at, string_address stop,
                               bool utf8, string_address address_to past)
{
        p32 value = *at;
        positive width = 1;
        if (utf8 && value >= 0x80)
        {
                width = stop ? expand_character_width(at, stop - at)
                             : expand_character_step(at);
                if (width == 1)
                        value += 0x110000;
                else
                {
                        value &= (1u << (7 - width)) - 1;
                        for (positive i = 1; i < width; i++)
                                value = (value << 6) | (at[i] & 0x3f);
                }
        }
        address_to past = at + width;
        return value;
}

static CONST inline INLINE p32 expand_set_fold(p32 value, bool fold)
{
        return fold && value < 0x80 ? byte_to_lower(value) : value;
}

static PURE inline INLINE bool expand_in_set(string_address at, string_address stop,
                               p32 value, bool fold, bool utf8)
{
        string_address step = at + 1;
        bool invert = false;
        bool found = false;

        if (expand_set_inverts(step))
        {
                invert = true;
                step++;
        }

        // Literal singleton and plain ASCII range sets are common behind a
        // star. Resolve them before the generic class/escape decoder; the
        // same fold operation is applied before complementing either shape.
        if (stop - step == 1 && *step < 0x80)
                return invert != (expand_set_fold(value, fold) ==
                                  expand_set_fold(*step, fold));
        if (stop - step == 3 && step[1] == '-' &&
            step[0] != '\\' && step[2] != '\\' &&
            step[0] < 0x80 && step[2] < 0x80)
        {
                p32 member = expand_set_fold(value, fold);
                return invert != (member >= expand_set_fold(step[0], fold) &&
                                  member <= expand_set_fold(step[2], fold));
        }

        while (step < stop)
        {
                p32 low;

                {
                        string_address past = byte_class_end(step, null);

                        if (past && past <= stop)
                        {
                                // nocasematch folds literal/range members,
                                // not predicates such as [[:lower:]].
                                if (value < 0x80 && byte_class_holds(
                                            byte_class_index(step + 2,
                                                             (positive)(past - step - 4)),
                                            value))
                                        found = true;

                                step = past;
                                continue;
                        }
                }

                if (string_is(step, '\\') && step + 1 < stop)
                        step++;
                low = expand_set_character(step, stop, utf8, &step);
                low = expand_set_fold(low, fold);

                if (step + 1 < stop && string_is(step, '-'))
                {
                        step++;
                        if (string_is(step, '\\') && step + 1 < stop)
                                step++;
                        p32 high = expand_set_character(step, stop, utf8, &step);
                        high = expand_set_fold(high, fold);
                        p32 member = expand_set_fold(value, fold);

                        if (member >= low && member <= high)
                                found = true;

                        continue;
                }

                if (expand_set_fold(value, fold) == low)
                        found = true;
        }

        return invert ? !found : found;
}

static inline INLINE positive expand_set_match(string_address pattern, string_address stop,
                                 string_address text, string_address text_end,
                                 bool fold)
{
        // Every byte of a non-ASCII UTF-8 member is above the ASCII range.
        // An ASCII subject therefore has identical literal/range membership
        // on the byte path; avoid locale lookup and decoding in that hot case.
        bool utf8 = *text >= 0x80 && shell_utf8_on();
        string_address past;
        p32 value = expand_set_character(text, text_end, utf8, &past);
        return expand_in_set(pattern, stop, value, fold, utf8) ? past - text : 0;
}

/*
        shopt -s extglob.

        ?( ) *( ) +( ) @( ) and !( ) are an extension to the pattern language,
        and Bash reads them as one only where this is on -- except inside
        [[ ]], where they are always read, because what is in there is matched
        when the command runs and not when the line is parsed.

        The option belongs to shopt, and shopt keeps every one of its names
        in one word, so this reads that bit rather than keeping a second copy
        of the same answer beside it. A byte of its own would have to be kept
        in step with the one the builtin writes, and the two would disagree
        the first time anybody forgot.
*/
#define shell_extglob_on shell_shopt_on(EXTGLOB)

PURE bool shell_extglob_asked()
{
        return shell_extglob_on;
}

/*
        Whether the pattern holds an extended group at all.

        Asked before every match, so it is one hardware scan for a
        parenthesis and not a walk of the pattern: the overwhelming majority
        of patterns have no parenthesis in them at all and stop on the first
        answer.
*/
static PURE bool glob_extended_anywhere(string_address pattern)
{
        string_address at = pattern;

        while (1)
        {
                at = string_first_of(at, '(');

                if (!at)
                        return false;

                if (at > pattern && lex_extended_head(string_get(at - 1)))
                        return true;

                at++;
        }
}

// An escaped byte or a bracket set closed before bound (or the terminator,
// with no bound), which a group scan steps over whole: one byte past it, or
// nothing when at begins neither.
static PURE string_address glob_unit_end(string_address at,
                                         string_address bound)
{
        string_address close;

        if (string_is(at, '\\') && string_get(at + 1) &&
            (!bound || at + 1 < bound))
                return at + 2;
        if (string_is(at, '[') && (close = expand_set_end(at)) &&
            (!bound || close < bound))
                return close + 1;
        return null;
}

// Where the group whose head is at ends, one byte past its parenthesis, or
// nothing when it never closes.
static PURE string_address glob_group_end(string_address at)
{
        string_address step = at;
        string_address past;
        positive depth = 0;

        while (string_get(step))
        {
                if ((past = glob_unit_end(step, null)))
                {
                        step = past;
                        continue;
                }

                if (string_is(step, '('))
                        depth++;
                else if (string_is(step, ')') && !--depth)
                        return step + 1;

                step++;
        }

        return null;
}

/*
        How much recursion an extended pattern gets before it is matched a
        different way.

        glob_bounded tries every split of the text for every group and every
        star, and the same question about the same suffix is asked again by
        every arrangement of the ones in front: +(a|aa)+(a|aa)b against fifty
        a's took two minutes here and in Bash, and each character more doubles
        it. The pattern and the string are a script's own data often enough
        (a case arm over an argument, a [[ ]] over a name) that a match which
        does not end is a way to stop a shell, so a match is given this many
        questions, which is more than any ordinary one asks by three orders of
        magnitude, and past them is answered by glob_reach instead, which
        cannot take more than a polynomial number of steps in the pattern and
        the text. Every match that ends inside the budget is what it always
        was.
*/
#ifndef GLOB_BUDGET
#define GLOB_BUDGET ((positive)1 << 16)
#endif

typedef struct
{
        positive steps;
        bool blown;
} glob_budget;

static bool glob_bounded(string_address pattern, string_address pattern_end,
                         string_address text, string_address text_end, bool fold,
                         glob_budget address_to budget);

// Whether any one of the alternatives in a group matches the whole run.
static bool glob_alternatives(string_address body, string_address body_end,
                              string_address text, string_address text_end, bool fold,
                              glob_budget address_to budget)
{
        string_address start = body;
        string_address at = body;
        string_address past;
        positive depth = 0;

        while (at < body_end)
        {
                if ((past = glob_unit_end(at, body_end)))
                {
                        at = past;
                        continue;
                }

                if (string_is(at, '('))
                        depth++;
                else if (string_is(at, ')') && depth)
                        depth--;
                else if (!depth && string_is(at, '|'))
                {
                        if (glob_bounded(start, at, text, text_end, fold, budget))
                                return true;

                        start = at + 1;
                }

                at++;
        }

        return glob_bounded(start, body_end, text, text_end, fold, budget);
}

/*
        One extended group and everything behind it.

        Where the group ends in the text is not something anything can know in
        advance, so every split is tried and the rest of the pattern asked
        about each one. The four counted forms differ only in how many
        occurrences they will take; !( ) is the odd one, and reads as "some
        split where the rest matches and the front matches none of these".

        A repeat is expressed by asking the same question again with the head
        turned into a star, which is what "one and then any number" means.
*/
static bool glob_extended(p8 head, string_address body, string_address body_end,
                          string_address rest, string_address pattern_end,
                          string_address text, string_address text_end, bool fold,
                          glob_budget address_to budget)
{
        string_address at;
        bool utf8 = shell_utf8_on();

        if (head == '!')
        {
                for (at = text; ;)
                {
                        if (glob_bounded(rest, pattern_end, at, text_end, fold, budget) &&
                            !glob_alternatives(body, body_end, text, at, fold, budget))
                                return true;
                        if (at == text_end)
                                break;
                        at += utf8 ? expand_character_width(at, text_end - at) : 1;
                }

                return false;
        }

        // None at all, which only these two will take.
        if ((head == '?' || head == '*') &&
            glob_bounded(rest, pattern_end, text, text_end, fold, budget))
                return true;

        //      Longest first, which is what a glob answers with, and never an
        //      empty occurrence: a group that matched nothing and asked again
        //      would ask forever.
        for (at = text_end; at > text;
             at = utf8 ? text + expand_character_previous(text, at - text) : at - 1)
        {
                if (!glob_alternatives(body, body_end, text, at, fold, budget))
                        continue;

                if (head == '@' || head == '?')
                {
                        if (glob_bounded(rest, pattern_end, at, text_end, fold, budget))
                                return true;

                        continue;
                }

                if (glob_extended('*', body, body_end, rest, pattern_end, at,
                                  text_end, fold, budget))
                        return true;
        }

        // An explicit empty alternative counts once for @ and +. Check it
        // after ordinary matches so nonempty successful groups pay nothing
        // for this edge; never recurse on a zero-width occurrence.
        return (head == '@' || head == '+') &&
               glob_alternatives(body, body_end, text, text, fold, budget) &&
               glob_bounded(rest, pattern_end, text, text_end, fold, budget);
}

/*
        One pattern against one run of bytes, both bounded.

        The matcher below walks the string once and keeps a single mark for
        the last star, which is everything an ordinary glob needs. An extended
        group is a different shape -- it splits the text somewhere nobody
        knows and every split has to be tried -- so it is recursion, and that
        is why this is a second matcher rather than another branch in the
        first. Nothing without a group in it comes through here.
*/
static bool glob_bounded_run(string_address pattern, string_address pattern_end,
                             string_address text, string_address text_end, bool fold,
                             glob_budget address_to budget)
{
        while (pattern < pattern_end)
        {
                p8 want = string_get(pattern);
                string_address stop;

                if (lex_extended_head(want) && string_is(pattern + 1, '('))
                {
                        string_address close = glob_group_end(pattern + 1);

                        if (close && close <= pattern_end)
                                return glob_extended(want, pattern + 2,
                                                     close - 1, close,
                                                     pattern_end, text,
                                                     text_end, fold, budget);
                }

                if (want == '*')
                {
                        string_address at = text_end;

                        pattern++;

                        //      Longest first, and counted down to the front
                        //      rather than past it: a cursor below the start
                        //      of the run is not an address.
                        while (1)
                        {
                                if (glob_bounded(pattern, pattern_end, at,
                                                 text_end, fold, budget))
                                        return true;

                                if (at == text)
                                        return false;

                                at = shell_utf8_on()
                                    ? text + expand_character_previous(text, at - text)
                                    : at - 1;
                        }
                }

                stop = want == '[' ? expand_set_end(pattern) : null;

                if (stop && stop < pattern_end)
                {
                        if (text >= text_end)
                                return false;
                        positive width = expand_set_match(pattern, stop, text,
                                                          text_end, fold);
                        if (!width)
                                return false;

                        pattern = stop + 1;
                        text += width;
                        continue;
                }

                if (want == '\\' && pattern + 1 < pattern_end)
                {
                        want = string_get(++pattern);

                        if (text >= text_end ||
                            (want != string_get(text) &&
                             (!fold || byte_to_lower(want) !=
                                       byte_to_lower(string_get(text)))))
                                return false;

                        pattern++;
                        text++;
                        continue;
                }

                if (text >= text_end ||
                    (want != '?' && want != string_get(text) &&
                     (!fold || byte_to_lower(want) !=
                               byte_to_lower(string_get(text)))))
                        return false;

                pattern++;
                text += want == '?' && *text >= 0x80 && shell_utf8_on()
                    ? expand_character_width(text, text_end - text) : 1;
        }

        return text == text_end;
}

static bool glob_bounded(string_address pattern, string_address pattern_end,
                         string_address text, string_address text_end, bool fold,
                         glob_budget address_to budget)
{
        if (budget->blown)
                return false;

        if (budget->steps++ >= GLOB_BUDGET)
        {
                budget->blown = true;
                return false;
        }

        return glob_bounded_run(pattern, pattern_end, text, text_end, fold,
                                budget);
}

/*
        The same match as sets of positions.

        glob_bounded asks, for each way a group or a star could end, whether
        the rest matches from there, and the ways multiply. This carries the
        one thing that matters instead: the set of offsets in the text the
        pattern read so far can have got to, as a bit for each of the length
        plus one. A byte moves every offset it fits on by one, a star fills
        from the first offset to the end, a group is the union over its
        alternatives run from the same set, and a repeat runs them again from
        what they added until they add nothing -- each offset joins once, so
        that ends after at most length rounds. !( ) is every offset from a
        start on that the alternatives do not end at, start by start. The
        text matches when its own end is in the set the whole pattern
        reaches, which is exactly what glob_bounded means by matching, in
        work that grows as the pattern times the text squared and not as the
        ways there are of splitting the text.

        It is reached only by a match that has spent its budget, so the
        allocation for each set costs nothing anyone ordinary sees, and a
        match that cannot get one, or a pattern nested past GLOB_REACH_DEPTH
        groups, is answered as not matching.
*/
#define GLOB_REACH_DEPTH 64

typedef p64 address_to glob_bits;

typedef struct
{
        string_address text;
        positive length;
        positive words;
        bool fold;
        bool utf8;
        bool failed;
} glob_reach;

static glob_bits glob_bits_new(glob_reach address_to walk)
{
        glob_bits bits = memory_take_zeroed(walk->words, sizeof(p64));

        if (!bits)
                walk->failed = true;

        return bits;
}

static inline INLINE bool glob_bits_get(const glob_bits bits, positive at)
{
        return (bits[at >> 6] >> (at & 63)) & 1;
}

static inline INLINE fn glob_bits_put(glob_bits bits, positive at)
{
        bits[at >> 6] |= (p64)1 << (at & 63);
}

static bool glob_bits_any(glob_reach address_to walk, const glob_bits bits)
{
        return array_any(at, walk->words, bits[at]);
}

static fn glob_bits_union(glob_reach address_to walk, glob_bits into,
                          const glob_bits from)
{
        for (positive at = 0; at < walk->words; at++)
                into[at] |= from[at];
}

// One character on from an offset short of the end: a byte, or in a UTF-8
// locale the whole sequence a non-ASCII byte begins.
static inline INLINE positive glob_reach_width(glob_reach address_to walk,
                                              positive at)
{
        return walk->utf8 && walk->text[at] >= 0x80
                   ? expand_character_width(walk->text + at, walk->length - at)
                   : 1;
}

static bool glob_reach_sequence(glob_reach address_to walk, string_address pattern,
                               string_address pattern_end, glob_bits cur,
                               positive depth);

// The offsets any one of the alternatives in a group reaches from a set.
static bool glob_reach_alternatives(glob_reach address_to walk,
                                   string_address body, string_address body_end,
                                   const glob_bits from, glob_bits reached,
                                   positive depth)
{
        glob_bits work = glob_bits_new(walk);
        string_address start = body;
        string_address at = body;
        string_address past;
        positive nesting = 0;
        bool alive = work != null;

        while (alive)
        {
                bool ends = at >= body_end;

                if (!ends)
                {
                        if ((past = glob_unit_end(at, body_end)))
                        {
                                at = past;
                                continue;
                        }

                        if (string_is(at, '('))
                                nesting++;
                        else if (string_is(at, ')') && nesting)
                                nesting--;
                        else if (!nesting && string_is(at, '|'))
                                ends = true;
                }

                if (ends)
                {
                        memory_copy(work, from, walk->words * sizeof(p64));
                        alive = glob_reach_sequence(walk, start, at, work, depth);

                        if (alive)
                                glob_bits_union(walk, reached, work);

                        if (at >= body_end)
                                break;

                        start = at + 1;
                }

                at++;
        }

        if (work)
                memory_give(work);

        return alive;
}

static bool glob_reach_group(glob_reach address_to walk, p8 head,
                            string_address body, string_address body_end,
                            glob_bits cur, positive depth)
{
        glob_bits added = glob_bits_new(walk);
        glob_bits frontier = glob_bits_new(walk);
        glob_bits next = glob_bits_new(walk);
        bool alive = added && frontier && next && depth < GLOB_REACH_DEPTH;

        if (alive && head == '!')
        {
                glob_bits one = glob_bits_new(walk);
                glob_bits ends = glob_bits_new(walk);

                alive = one && ends;

                for (positive from = 0; alive && from <= walk->length; from++)
                {
                        if (!glob_bits_get(cur, from))
                                continue;

                        memory_zero(one, walk->words * sizeof(p64));
                        memory_zero(ends, walk->words * sizeof(p64));
                        glob_bits_put(one, from);
                        alive = glob_reach_alternatives(walk, body, body_end,
                                                       one, ends, depth + 1);

                        for (positive at = from; alive; )
                        {
                                if (!glob_bits_get(ends, at))
                                        glob_bits_put(added, at);
                                if (at >= walk->length)
                                        break;
                                at += glob_reach_width(walk, at);
                        }
                }

                if (one)
                        memory_give(one);
                if (ends)
                        memory_give(ends);
        }
        else if (alive)
        {
                alive = glob_reach_alternatives(walk, body, body_end, cur, added,
                                               depth + 1);

                if (alive && (head == '+' || head == '*'))
                {
                        memory_copy(frontier, added, walk->words * sizeof(p64));

                        while (alive && glob_bits_any(walk, frontier))
                        {
                                memory_zero(next, walk->words * sizeof(p64));
                                alive = glob_reach_alternatives(walk, body,
                                                               body_end, frontier,
                                                               next, depth + 1);

                                for (positive at = 0; alive && at < walk->words;
                                     at++)
                                {
                                        frontier[at] = next[at] & ~added[at];
                                        added[at] |= next[at];
                                }
                        }
                }

                if (alive && (head == '?' || head == '*'))
                        glob_bits_union(walk, added, cur);
        }

        if (alive)
                memory_copy(cur, added, walk->words * sizeof(p64));
        else
                walk->failed = true;

        if (added)
                memory_give(added);
        if (frontier)
                memory_give(frontier);
        if (next)
                memory_give(next);

        return alive;
}

static bool glob_reach_sequence(glob_reach address_to walk, string_address pattern,
                               string_address pattern_end, glob_bits cur,
                               positive depth)
{
        glob_bits moved = null;

        while (pattern < pattern_end && glob_bits_any(walk, cur))
        {
                p8 want = string_get(pattern);
                string_address stop;

                if (lex_extended_head(want) && string_is(pattern + 1, '('))
                {
                        string_address close = glob_group_end(pattern + 1);

                        if (close && close <= pattern_end)
                        {
                                if (!glob_reach_group(walk, want, pattern + 2,
                                                     close - 1, cur, depth))
                                        break;

                                pattern = close;
                                continue;
                        }
                }

                if (want == '*')
                {
                        positive first = 0;

                        while (!glob_bits_get(cur, first))
                                first++;

                        for (positive at = first; ; )
                        {
                                glob_bits_put(cur, at);
                                if (at >= walk->length)
                                        break;
                                at += glob_reach_width(walk, at);
                        }

                        pattern++;
                        continue;
                }

                if (!moved && !(moved = glob_bits_new(walk)))
                        break;

                memory_zero(moved, walk->words * sizeof(p64));
                stop = want == '[' ? expand_set_end(pattern) : null;

                if (stop && stop < pattern_end)
                {
                        for (positive at = 0; at < walk->length; at++)
                        {
                                positive width;

                                if (!glob_bits_get(cur, at))
                                        continue;

                                width = expand_set_match(pattern, stop,
                                                         walk->text + at,
                                                         walk->text + walk->length,
                                                         walk->fold);
                                if (width)
                                        glob_bits_put(moved, at + width);
                        }

                        pattern = stop + 1;
                }
                else
                {
                        bool any = want == '?';

                        if (want == '\\' && pattern + 1 < pattern_end)
                        {
                                want = string_get(++pattern);
                                any = false;
                        }

                        for (positive at = 0; at < walk->length; at++)
                        {
                                p8 have;

                                if (!glob_bits_get(cur, at))
                                        continue;

                                have = walk->text[at];

                                if (any)
                                        glob_bits_put(moved, at + glob_reach_width(walk, at));
                                else if (want == have ||
                                         (walk->fold && byte_to_lower(want) ==
                                                            byte_to_lower(have)))
                                        glob_bits_put(moved, at + 1);
                        }

                        pattern++;
                }

                memory_copy(cur, moved, walk->words * sizeof(p64));
        }

        if (moved)
                memory_give(moved);

        return !walk->failed;
}

static bool glob_reach_match(string_address pattern, string_address pattern_end,
                            string_address text, string_address text_end,
                            bool fold)
{
        glob_reach walk = {text, (positive)(text_end - text), 0, fold,
                          shell_utf8_on(), false};
        glob_bits cur;
        bool matched = false;

        walk.words = (walk.length >> 6) + 1;

        if (!(cur = glob_bits_new(&walk)))
                return false;

        glob_bits_put(cur, 0);

        if (glob_reach_sequence(&walk, pattern, pattern_end, cur, 0))
                matched = glob_bits_get(cur, walk.length);

        memory_give(cur);

        return matched;
}

// An extended pattern against a whole string: the recursive matcher, which
// answers everything ordinary at once, and the position sets for a match that
// has asked it too much.
static bool glob_extended_match(string_address pattern, string_address text,
                                bool fold)
{
        glob_budget budget = {0, false};
        string_address pattern_end = pattern + string_length(pattern);
        string_address text_end = text + string_length(text);
        bool matched = glob_bounded(pattern, pattern_end, text, text_end, fold,
                                    &budget);

        if (budget.blown)
                matched = glob_reach_match(pattern, pattern_end, text, text_end,
                                          fold);

        return matched;
}

/*
        A glob against a string, whole.

        This is the matcher for case, for the four trimming forms, and for every
        component of a path -- one pattern language, matched one way, so that a
        case arm and a glob cannot disagree about what a star is.

        Folding is a parameter and not a second matcher: nocasematch is what
        case and [[ ]] ask for and nocaseglob is what pathname expansion asks
        for, and they are separately settable options over the same rules.
*/
static inline INLINE bool shell_match_core(string_address pattern,
                                          string_address text, bool fold)
{
        string_address star = null;
        string_address back = null;
        p8 behind = 0;

        //      One bool, and a hardware scan for a parenthesis only when
        //      it says the groups are being read at all. Every match in the
        //      shell comes through here.
        if (shell_extglob_on && glob_extended_anywhere(pattern))
                return glob_extended_match(pattern, text, fold);

        while (string_get(text))
        {
                p8 want = string_get(pattern);
                string_address stop = null;

                if (want == '*')
                {
                        star = ++pattern;
                        back = text;

                        /*
                                A plain byte behind the star is the only place
                                what follows can begin, so a backtrack goes
                                there rather than trying every position on the
                                way. Folded, that byte stands for two, and the
                                skip is given up rather than made wrong.
                        */
                        behind = fold ? 0 : string_get(pattern);

                        if (behind == '*' || behind == '?' || behind == '[' ||
                            behind == '\\')
                                behind = 0;

                        continue;
                }

                if (want == '[')
                        stop = expand_set_end(pattern);

                if (stop)
                {
                        positive width = expand_set_match(pattern, stop, text,
                                                          null, fold);
                        if (width)
                        {
                                pattern = stop + 1;
                                text += width;
                                continue;
                        }
                }
                else
                {
                        bool escaped = false;

                        // A [ with no ] after it is a plain [.
                        if (want == '\\' && string_get(pattern + 1))
                        {
                                want = string_get(++pattern);
                                escaped = true;
                        }

                        if (want && ((!escaped && want == '?') ||
                                     want == string_get(text) ||
                                     (fold && byte_to_lower(want) ==
                                                  byte_to_lower(
                                                      string_get(text)))))
                        {
                                pattern++;
                                text += !escaped && want == '?' && *text >= 0x80 && shell_utf8_on()
                                    ? expand_character_step(text) : 1;
                                continue;
                        }
                }

                /*
                        Nothing matched here, so the last star takes one byte
                        more. Walking rather than calling: a pattern of many
                        stars against a long name tries every arrangement of
                        them when each star recurses, and only the arrangement
                        it is standing in when the star is a mark.
                */
                if (!star)
                        return false;

                pattern = star;
                back += *back >= 0x80 && shell_utf8_on()
                    ? expand_character_step(back) : 1;
                text = back;

                if (behind)
                {
                        text = string_first_of_or_end(text, behind);

                        if (!string_get(text))
                                return false;

                        back = text;
                }
        }

        pattern += string_span_of_set(pattern, "*");

        return string_get(pattern) == end;
}

/*
        Two entries into one body, and the fold decided at each of them.

        The parameter is what keeps the rules in one place, and inlining is
        what keeps that from costing anything: every caller reaches the body
        with fold already a constant, so the ordinary match compiles to
        exactly what it was before folding existed.
*/
PURE bool shell_match(string_address pattern, string_address text)
{
        return shell_match_core(pattern, text, false);
}

PURE bool shell_match_folded(string_address pattern, string_address text,
                             bool fold)
{
        return fold ? shell_match_core(pattern, text, true)
                    : shell_match_core(pattern, text, false);
}

// The same match with the extended groups read whether or not the option is
// on, which is what [[ ]] does with them.
PURE bool shell_match_extended(string_address pattern, string_address text,
                                bool fold)
{
        if (!shell_extglob_on && glob_extended_anywhere(pattern))
                return glob_extended_match(pattern, text, fold);

        return shell_match_folded(pattern, text, fold);
}

//      set -- takes as many words as it is given, which after a glob may be
//      every name in a directory, so neither the table nor the bytes behind it
//      is allowed a fixed size.
string_address address_to shell_parameter;
static positive shell_parameter_room HOT_STATE;
positive shell_parameter_count;
string_address shell_script_name HOT_DATA = (string_address) "sh";
// Entry-only flags seed the options `set` can subsequently change. A no-arg
// shell reads standard input and begins with s; a script file resets it and
// -c has its own entry marker.
string_address shell_option_flags HOT_DATA = (string_address) "s";

static p8 address_to shell_parameter_bytes;
static positive shell_parameter_bytes_room;
static p8 address_to shell_parameter_staging;
static positive shell_parameter_staging_room;

/*
        Where set -- and shift keep their words.

        The copy goes through a staging block because "set -- $@" hands back the
        very bytes it is about to be written over.
*/
HOT bool shell_parameters_set(string_address address_to words, positive count)
{
        static string_address empty[1];
        positive used = 0;
        positive index = 0;
        positive at;
        positive need = 0;

        /* Entry with no operands is already the complete empty state. Keep
           the three growable stores untouched until set/function arguments
           actually need them. Once allocated, an empty `set --` retains its
           reusable table rather than rebinding it to static storage. */
        if (!count && !shell_parameter_room)
        {
                shell_parameter = empty;
                shell_parameter_count = 0;
                return true;
        }

        if (count > positive_max - 2 ||
            !shell_array_room(shell_parameter, shell_parameter_room, count + 2))
                return false;

        /* Measure before changing the live table, so allocation failure leaves
           the old positional parameters intact.  The bytes still pass through
           staging because set -- $@ may overlap the live parameter store. */
        for (at = 0; at < count; at++)
        {
                positive length = string_length(words[at]);

                if (length == positive_max ||
                    need > positive_max - length - 1)
                        return false;

                need += length + 1;
        }

        if (need == positive_max ||
            !shell_byte_pair_room(shell_parameter_staging,
                                  shell_parameter_staging_room,
                                  shell_parameter_bytes,
                                  shell_parameter_bytes_room, need + 1))
                return false;

        while (index < count)
        {
                positive length = string_length(words[index]) + 1;

                memory_copy(shell_parameter_staging + used, words[index],
                            length);
                shell_parameter[index] = shell_parameter_bytes + used;
                used += length;
                index++;
        }

        memory_copy(shell_parameter_bytes, shell_parameter_staging, used);

        shell_parameter_count = index;

        shell_parameter[index] = null;

        return index == count;
}

/*
        The parameters as they stand, put aside.

        A function is given its own and hands these back when it returns, and a
        function may call a function, so what is put aside is a stack of bytes
        rather than one spare copy. The pointers are not worth saving: they all
        point into the one block that the next set is about to write over.
*/
#define EXPAND_NO_ROOM ((positive)-1)

//      A function is given its own parameters and hands these back when it
//      returns, and functions nest, so what is put aside is a stack of bytes
//      that grows with the depth rather than a fixed one.
static p8 address_to shell_parameter_stack;
static positive shell_parameter_stack_room;
static positive shell_parameter_stack_used;

positive shell_parameters_save()
{
        positive mark = shell_parameter_stack_used;
        positive used = 0;
        positive at;

        for (at = 0; at < shell_parameter_count; at++)
        {
                positive length = string_length(shell_parameter[at]) + 1;

                if (used > positive_max - length)
                        return EXPAND_NO_ROOM;

                used += length;
        }

        if (used == positive_max || mark > positive_max - used - 1 ||
            !shell_array_room(shell_parameter_stack, shell_parameter_stack_room, mark + used + 1))
                return EXPAND_NO_ROOM;

        for (at = 0; at < shell_parameter_count; at++)
        {
                positive length = string_length(shell_parameter[at]) + 1;

                memory_copy(shell_parameter_stack + shell_parameter_stack_used,
                            shell_parameter[at], length);
                shell_parameter_stack_used += length;
        }

        return mark;
}

static string_address address_to shell_restore_words;
static positive shell_restore_room;

bool shell_parameters_restore_prepare(positive count)
{
        return count != positive_max &&
               shell_array_room(shell_restore_words, shell_restore_room, count + 1);
}

bool shell_parameters_restore(positive mark, positive count)
{
        positive at = mark;
        positive index;

        if (mark == EXPAND_NO_ROOM)
                return false;

        if (!shell_parameters_restore_prepare(count))
                return false;

        for (index = 0; index < count; index++)
        {
                shell_restore_words[index] = shell_parameter_stack + at;
                at += string_length(shell_parameter_stack + at) + 1;
        }

        if (!shell_parameters_set(shell_restore_words, count))
                return false;

        shell_parameter_stack_used = mark;
        return true;
}

fn shell_parameters_shift(positive count)
{
        if (count > shell_parameter_count)
                count = shell_parameter_count;

        memory_copy(shell_parameter, shell_parameter + count,
                    (shell_parameter_count - count) *
                        sizeof(shell_parameter[0]));

        shell_parameter_count -= count;
        shell_parameter[shell_parameter_count] = null;
}

/*
        IFS unset is the default three; IFS set to nothing splits nothing at
        all, and collapsing the two is how a script that clears IFS on purpose
        gets its fields taken apart anyway.
*/
static PURE string_address expand_ifs()
{
        /* djb2("IFS"). The name and its extent are invariant, so sending it
           through the NUL-scanning hash path on every split only rediscovers
           the same three bytes. */
        string_address value = env_get_hashed_span(
            (const_string) "IFS", 3, 193458887, null);

        return value ? value : (string_address) " \t\n";
}

/*
        IFS as one byte per byte value, built once a word.

        Splitting asks about every byte it walks over and the answer is the same
        every time; asking env_get for it each time turned a field into a linear
        walk of the environment per character.
*/
// Read at startup, from where nothing has forked yet.
positive expand_shell_pid;
bipolar shell_background_last;

fn shell_pid_ensure()
{
        if (!expand_shell_pid)
                expand_shell_pid =
                    (positive)system_call_1(syscall(getpid), 0);
}

/* A child may perform its first $$ expansion after the clone. Capture the
   shell's identity on the parent side while leaving no-fork startup lazy. */
HOT bipolar shell_clone_raw()
{
        shell_pid_ensure();
        return system_fork();
}

bipolar shell_clone()
{
        shell_parser_source_fork_prepare();
        return shell_clone_raw();
}

enum
{
        EXPAND_IFS_MEMBER = 1,
        EXPAND_IFS_BLANK = 2
};

/* Membership and whitespace are two properties of the same byte.  Keeping
   them in one table halves the hot splitter's footprint and preparation work. */
static b8 expand_ifs_kind[256];
static p8 expand_ifs_mb[8][4];
static b8 expand_ifs_mb_len[8];
static positive expand_ifs_mb_count;

static fn expand_ifs_prepare()
{
        string_address ifs = expand_ifs();

        memory_fill(expand_ifs_kind, 0, sizeof(expand_ifs_kind));
        expand_ifs_mb_count = 0;

        while (string_get(ifs))
        {
                p8 value = string_get(ifs);

                if (shell_utf8_on() && value >= 0x80)
                {
                        positive width = expand_character_step(ifs);

                        if (width > 1 && width <= 4 &&
                            expand_ifs_mb_count < array_count(expand_ifs_mb))
                        {
                                memory_copy(expand_ifs_mb[expand_ifs_mb_count],
                                            ifs, width);
                                expand_ifs_mb_len[expand_ifs_mb_count] = (b8)width;
                                expand_ifs_mb_count++;
                                ifs += width;
                                continue;
                        }
                }

                ifs++;
                expand_ifs_kind[value] = EXPAND_IFS_MEMBER;

                if (value == ' ' || value == '\t' || value == '\n')
                        expand_ifs_kind[value] |= EXPAND_IFS_BLANK;
        }
}

static PURE bool expand_in_ifs(p8 value)
{
        return expand_ifs_kind[value] & EXPAND_IFS_MEMBER;
}

static PURE bool expand_ifs_blank(p8 value)
{
        return expand_ifs_kind[value] & EXPAND_IFS_BLANK;
}

/*
        How many bytes of an IFS separator start here. ASCII stays the
        table; a multibyte IFS character is one separator, not each of
        its bytes.
*/
static HOT positive expand_ifs_span(string_address text, positive left)
{
        p8 value;
        positive width;
        positive at;

        if (!left)
                return 0;

        value = string_get(text);
        if (expand_in_ifs(value))
                return 1;

        if (!expand_ifs_mb_count || !shell_utf8_on() || value < 0x80)
                return 0;

        width = expand_character_width(text, left);
        if (width <= 1 || width > left)
                return 0;

        for (at = 0; at < expand_ifs_mb_count; at++)
                if (expand_ifs_mb_len[at] == width &&
                    !memory_compare(expand_ifs_mb[at], text, width))
                        return width;

        return 0;
}

/*
        What a parameter stands for, and whether it stands for anything at
        all. Ordinary variables and positional parameters already live at
        stable addresses and are returned directly. Only the numeric special
        parameters need scratch space.

        $* and unquoted $@ need one joined string. It comes from the line's
        stable store, sized from the parameters rather than from a fixed
        expansion buffer.
*/
// A resolved parameter keeps the key separate from the variable's name.
// Scalars use only name; element names and keys live through their operator.
typedef struct
{
        string_address name, key;
        positive name_length, key_length;
} expand_reference;

// Only diagnostics need shell syntax after a subscript has been evaluated.
static COLD string_address expand_reference_text(expand_reference reference)
{
        if (!reference.key)
                return reference.name;
        positive base = reference.name_length, key = reference.key_length;
        if (key > positive_max - 3 || base > positive_max - key - 3)
                return reference.name;
        string_address text = shell_store_take(address_of expand_store, base + key + 3);
        if (!text)
                return reference.name;
        memory_copy(text, reference.name, base);
        text[base] = '[';
        memory_copy(text + base + 1, reference.key, key);
        text[base + key + 1] = ']';
        text[base + key + 2] = end;
        return text;
}

/*
        What a name that is not simply a variable stands for.

        Three things reach here and every one of them is cold: an element,
        which a name stops being a variable name the moment it names, so no
        scalar lookup pays for the possibility of one; an associative array,
        which keeps no value of its own and whose bare $m Bash reads as the
        element named "0"; and the three names the call stack publishes,
        which are only built once something has asked for them.
*/
static COLD string_address expand_absent_value(string_address name,
                                               positive2 answer,
                                               positive address_to value_length)
{
        {
                string_address reference = shell_reference_element_value(
                    name, answer.y, value_length);

                if (reference)
                        return reference;
        }

        if (shell_frames_wanted(name, answer.y))
                return env_get_hashed_span(name, answer.y, answer.x,
                                           value_length);

        if (shell_array_attributes(name, answer.y) &
            SHELL_ARRAY_ASSOCIATIVE)
                return shell_array_get(name, answer.y, "0", 1, value_length);

        if (shell_dynamic_wanted(name, answer.y))
                return env_get_hashed_span(name, answer.y, answer.x,
                                           value_length);

        return shell_dynamic_value(name, answer.y, value_length);
}

static HOT string_address expand_value_of(expand_reference reference, p8 address_to scratch,
                                      bool address_to present,
                                      positive address_to value_length)
{
        string_address name = reference.name;
        p8 first = string_get(name);

        address_to present = true;
        if (value_length)
                address_to value_length = 0;
        scratch[0] = end;

        if (reference.key)
        {
                shell_frames_wanted(name, reference.name_length);
                shell_dynamic_wanted(name, reference.name_length);
                string_address value = shell_array_get(name, reference.name_length,
                    reference.key, reference.key_length, value_length);
                *present = value != null;
                return value;
        }

        if (byte_is_digit(first))
        {
                positive which = string_digits(name, null);

                if (!which)
                {
                        if (value_length)
                                address_to value_length =
                                    string_length(shell_script_name);
                        return shell_script_name;
                }

                if (which > shell_parameter_count)
                {
                        address_to present = false;
                        return null;
                }

                if (value_length)
                        address_to value_length =
                            string_length(shell_parameter[which - 1]);

                return shell_parameter[which - 1];
        }

        if (string_get(name + 1) == end)
        {
                if (first == '#')
                {
                        positive length = bipolar_into_string(
                            scratch, (bipolar)shell_parameter_count);
                        if (value_length)
                                address_to value_length = length;
                        return scratch;
                }

                if (first == '?')
                {
                        positive length = bipolar_into_string(
                            scratch, (bipolar)shell_status);
                        if (value_length)
                                address_to value_length = length;
                        return scratch;
                }

                if (first == '$')
                {
                        /*
                                The shell's pid, and not this process's.

                                A subshell is a fork, and asking the kernel
                                here answered with the fork -- so "( kill $$ )"
                                signalled the subshell instead of the shell,
                                which is the opposite of what POSIX says $$
                                is. Read once, before anything can fork.
                        */
                        shell_pid_ensure();

                        positive length = bipolar_into_string(
                            scratch, (bipolar)expand_shell_pid);
                        if (value_length)
                                address_to value_length = length;
                        return scratch;
                }

                if (first == '!')
                {
                        if (shell_background_last <= 0)
                        {
                                address_to present = false;
                                return null;
                        }

                        positive length = bipolar_into_string(
                            scratch, shell_background_last);
                        if (value_length)
                                address_to value_length = length;
                        return scratch;
                }

                if (first == '-')
                {
                        string_address flags = shell_flags_current();

                        if (value_length)
                                address_to value_length = string_length(flags);
                        return flags;
                }

                if (first == '@' || first == '*')
                {
                        string_address ifs = expand_ifs();
                        p8 between = string_get(ifs);
                        positive room = 1;
                        positive used = 0;
                        positive at;
                        p8 address_to into;

                        for (at = 0; at < shell_parameter_count; at++)
                        {
                                positive run = string_length(shell_parameter[at]);

                                if (room > (positive)-1 - run - (at && between ? 1 : 0))
                                {
                                        address_to present = false;
                                        return null;
                                }

                                room += run + (at && between ? 1 : 0);
                        }

                        into = shell_store_take(address_of expand_store, room);

                        if (!into)
                        {
                                address_to present = false;
                                return null;
                        }

                        for (at = 0; at < shell_parameter_count; at++)
                        {
                                positive run = string_length(shell_parameter[at]);

                                if (at && between)
                                        into[used++] = between;

                                memory_copy_apart(into + used, shell_parameter[at], run);
                                used += run;
                        }

                        into[used] = end;
                        if (value_length)
                                address_to value_length = used;
                        return into;
                }
        }

        {
                positive2 answer = string_hash_33_length(name);
                string_address value;

                // A simple command's one-element PIPESTATUS is deferred by
                // the executor. Materialize it only for the exact scalar
                // read; array forms already pass through dynamic_wanted.
                if (shell_bash_compat &&
                    memory_is_word(name, answer.y, "PIPESTATUS"))
                        shell_dynamic_wanted(name, answer.y);

                value = env_get_hashed_span(name, answer.y, answer.x,
                                            value_length);

                if (value)
                        return value;

                value = expand_absent_value(name, answer, value_length);

                if (!value)
                {
                        address_to present = false;
                        return null;
                }

                return value;
        }
}

static COLD fn expand_fatal_status(b32 status);
static PURE b32 expand_nounset_status(b32 indirect);

//      set -u met an unset name. A command string that dies of it leaves 127,
//      the same status the other unset-parameter path gives; only the two
//      spellings differ.
static COLD fn expand_unbound(expand_reference reference, b32 indirect)
{
        expand_where();
        string_format(writer_stderr_once,
                      shell_bash_compat ? "%s: unbound variable\n"
                                        : "%s: parameter not set\n",
                      expand_reference_text(reference));
        expand_fatal_status(expand_nounset_status(indirect));
}

static COLD fn expand_slice_error();

//      ${name:} names no offset at all.
static COLD fn expand_slice_refused(expand_reference reference)
{
        expand_where();
        if (!shell_bash_compat)
                writer_stderr_once(str("Bad substitution\n"));
        else
                string_format(writer_stderr_once, "${%s:}: bad substitution\n",
                              expand_reference_text(reference));
        //      bash abandons the command and goes on with the next; dash ends.
        if (shell_bash_compat && !shell_posix_on())
                expand_slice_error();
        else
                expand_fatal_status(shell_bash_compat ? 1 : 2);
}

//      An assignment made inside an expansion that the variable table refused.
static COLD fn expand_assign_refused(expand_reference reference)
{
        string_address name = expand_reference_text(reference);

        expand_where();
        string_format(writer_stderr_once,
                      !env_readonly(name) ? "%s: cannot assign\n"
                      : shell_bash_compat ? "%s: readonly variable\n"
                                          : "%s: is read only\n",
                      name);
}
static COLD fn expand_slice_error();
static string_address expand_tilde(string_address step, bool assignment);

/*
        A parameter pushed with the marks that decide its fate later.

        "$@" is the one form that makes its own field boundaries: they go in
        as a byte of their own so that splitting cannot miss them, and a
        parameter with a space in it stays whole.

        Unquoted, $@ and $* do not. Both join on the first byte of IFS and the
        join is taken apart again, which is why set -- "" a is one field and
        not two -- an empty parameter joins to nothing and splits to nothing.
        Only when IFS is empty is there no byte to join on, and there the
        boundaries have to be put in or every parameter runs together.
*/
static HOT bool expand_push_parameter_as(expand_reference reference, bool quoted,
                                     b32 mode)
{
        string_address name = reference.name;
        p8 mark = quoted ? MARK_QUOTED : MARK_FIELD;
        p8 scratch[32];
        string_address value;
        positive value_length;
        bool present;
        bool all = string_get(name + 1) == end &&
                   (string_is(name, '@') || string_is(name, '*'));

        if (!(mode & EXPAND_PARAMETER_MISSING) && all &&
            (quoted ? string_is(name, '@')
                    : expand_list_blanks(string_get(name),
                                         string_get(expand_ifs()))))
        {
                positive at;

                /* bash drops a word whose only pieces are an empty "$@" and
                   quoted expansions that came out empty: "$xxx$@" and
                   "$@""$@" are no word with no parameters, as "$@" is. A
                   literal "" or any byte keeps the word, and dash keeps it
                   either way. */
                if (quoted && !shell_parameter_count && shell_bash_compat)
                        expand_name_at_empty = true;

                for (at = 0; at < shell_parameter_count; at++)
                {
                        if (at)
                                expand_push(' ', quoted ? MARK_BREAK : MARK_SEPARATE);

                        expand_push_string(shell_parameter[at], mark);
                }

                return shell_parameter_count != 0;
        }

        if (mode & EXPAND_PARAMETER_MISSING)
        {
                present = false;
                value = null;
                value_length = 0;
        }
        else
                value = expand_value_of(reference, scratch, address_of present,
                                        address_of value_length);

        if (!present)
        {
                if (shell_options & ((positive)1 << ('u' - 'a')))
                {
                        expand_unbound(reference,
                                       mode & EXPAND_PARAMETER_INDIRECT);
                }

                return false;
        }

        expand_push_run(value, value_length, mark);

        return true;
}

static bool expand_push_parameter(expand_reference reference, bool quoted)
{
        return expand_push_parameter_as(reference, quoted, 0);
}

// Sequence readers calculate IFS once, then share this inlined boundary
// writer. A slice, full array and per-element transform must carry identical
// quoted/unquoted empty-field policy without rescanning IFS for each item.
static inline INLINE fn expand_sequence_between(bool fields, p8 between, p8 mark)
{
        if (fields)
                expand_push(' ', mark == MARK_QUOTED ? MARK_BREAK : MARK_SEPARATE);
        else if (between)
                expand_push(between, mark);
}


static fn expand_into(string_address text, bool quoted, p8 plain,
                      bool assignment);
static string_address expand_double(string_address step);
static string_address expand_dollar(string_address step, bool quoted);
static string_address expand_backtick(string_address step, bool quoted,
                                       bool in_double);
static bool expand_sort_names(string_address address_to names, positive count);

/*
        A nested word -- the tail of ${x-...}, a trimming pattern, the body of
        an arithmetic expression -- run through the whole expander and lifted
        back out, leaving the working buffer exactly as it was found.

        A pattern gets a backslash in front of every byte that was quoted, so
        that ${x%"*"} strips one star and not everything.
*/
#define EXPAND_CAPTURE_TEXT 0
#define EXPAND_CAPTURE_PATTERN 1
#define EXPAND_CAPTURE_REPLACEMENT 2
// The word of ${x:=word} and ${x:?word}: text, with a leading tilde expanded
// first, as POSIX asks of every word a parameter form can substitute.
#define EXPAND_CAPTURE_WORD 3
// The right-hand side of [[ string =~ regex ]].
#define EXPAND_CAPTURE_REGEX 4

/* Glob and ERE operands preserve quoted metacharacters with the same encoder.
   Two machine-word sets keep membership constant-time without a callback or
   a 256-byte table; ERE adds to the four metacharacters glob already owns. */
static CONST bool expand_quoted_metacharacter(p8 value, bool regex)
{
        const p64 common_low = ((p64)1 << '*') | ((p64)1 << '?');
        const p64 common_high = ((p64)1 << ('[' - 64)) |
                                ((p64)1 << ('\\' - 64));
        //      A hyphen and a close bracket are special inside a bracket
        //      expression and nowhere else, which is enough: [a\\-c] is the
        //      three members a, hyphen and c to both references where [a-c]
        //      is the range. They are the pattern language's alone -- a
        //      backslash before either in an ERE is not defined -- so the
        //      regex right-hand side does not take them from here.
        //      Parentheses and a bar are the extended groups' own, which
        //      case and [[ ]] read with extglob off: *'()' is a star and two
        //      parentheses, not the group *( ) that matches only nothing.
        //      A quoted ! or ^ right after [ is a member, not the
        //      complement: [\^a] matches a caret.
        const p64 glob_low = ((p64)1 << '-') | ((p64)1 << '(') |
                             ((p64)1 << ')') | ((p64)1 << '!');
        const p64 glob_high = ((p64)1 << (']' - 64)) |
                              ((p64)1 << ('|' - 64)) |
                              ((p64)1 << ('^' - 64));
        const p64 regex_low = ((p64)1 << '$') | ((p64)1 << '(') |
                              ((p64)1 << ')') | ((p64)1 << '+') |
                              ((p64)1 << '.');
        const p64 regex_high = ((p64)1 << (']' - 64)) |
                               ((p64)1 << ('^' - 64)) |
                               ((p64)1 << ('{' - 64)) |
                               ((p64)1 << ('|' - 64)) |
                               ((p64)1 << ('}' - 64));
        p64 bit;

        if (value >= 128)
                return false;

        bit = (p64)1 << (value & 63);

        if (bit & (value < 64 ? common_low : common_high))
                return true;

        if (!regex && (bit & (value < 64 ? glob_low : glob_high)))
                return true;

        return regex && (bit & (value < 64 ? regex_low : regex_high));
}

static inline INLINE PURE bool expand_lift_escaped(positive step, b32 mode)
{
        p8 value = expand_text[step];

        if (expand_mark[step] != MARK_QUOTED)
                return false;
        if (mode == EXPAND_CAPTURE_REPLACEMENT)
                return value == '&';
        return (mode == EXPAND_CAPTURE_PATTERN || mode == EXPAND_CAPTURE_REGEX) &&
               expand_quoted_metacharacter(value, mode == EXPAND_CAPTURE_REGEX);
}

/*
        The working buffer from start on, lifted out as one string in the
        store: empty marks dropped, and a backslash kept in front of each
        quoted byte the consumer would otherwise read as syntax. Null when
        it does not fit.
*/
static p8 address_to expand_lift(positive start, b32 mode)
{
        positive room = 1;
        positive used = 0;
        positive step;
        p8 address_to into;

        for (step = start; step < expand_length; step++)
        {
                if (expand_mark[step] == MARK_EMPTY)
                        continue;
                if (room > positive_max - 2)
                        return null;
                room += 1 + expand_lift_escaped(step, mode);
        }

        if (!(into = shell_store_take(address_of expand_store, room)))
                return null;

        for (step = start; step < expand_length; step++)
        {
                if (expand_mark[step] == MARK_EMPTY)
                        continue;
                if (expand_lift_escaped(step, mode))
                        into[used++] = '\\';
                into[used++] = expand_text[step];
        }

        into[used] = end;
        return into;
}

//      Set while a compound assignment expands its bare words, which bash
//      does not read as assignments however they are spelled.
static bool expand_list_element;


// The word of ${x-word} and ${x:+word}, expanded in place: a leading tilde
// first, unless the whole form sits inside double quotes.
static fn expand_word_into(string_address word, bool quoted)
{
        if (!quoted && expand_assigning &&
            !(shell_bash_compat && shell_posix_on()))
        {
                expand_into(word, quoted, MARK_FIELD, true);
                return;
        }
        //      bash ends that tilde prefix at a colon as well as a slash:
        //      ${u-~:~} is the home directory and :~.
        if (!quoted && string_is(word, '~'))
                word = expand_tilde(word, shell_bash_compat);

        {
                bool held = expand_brace_word;

                expand_brace_word = shell_bash_compat && !shell_posix_on();
                expand_into(word, quoted, MARK_FIELD, false);
                expand_brace_word = held;
        }
}

static string_address expand_capture(string_address text, bool quoted, b32 mode)
{
        positive at = expand_length;
        positive held_empty = expand_empty_count;
        bool held = expand_quoted_seen;
        bool held_name_at = expand_name_at_empty;
        bool held_explicit = expand_explicit_empty;
        p8 address_to into;

        if (mode == EXPAND_CAPTURE_WORD && !quoted && string_is(text, '~'))
                text = expand_tilde(text, false);
        //      bash tilde-expands a pattern and a replacement word too,
        //      quoted or not: ${p//~/z} replaces the home directory.
        else if ((mode == EXPAND_CAPTURE_PATTERN ||
                  mode == EXPAND_CAPTURE_REPLACEMENT) &&
                 shell_bash_compat && string_is(text, '~'))
                text = expand_tilde(text, false);

        {
                bool held_word = expand_brace_word;

                if (mode == EXPAND_CAPTURE_WORD)
                        expand_brace_word = shell_bash_compat &&
                                            !shell_posix_on();
                expand_into(text, quoted, MARK_PLAIN, false);
                expand_brace_word = held_word;
        }

        if (!(into = expand_lift(at, mode)))
                expand_fail_state();

        expand_length = at;
        expand_empty_count = held_empty;
        expand_quoted_seen = held;
        expand_name_at_empty = held_name_at;
        expand_explicit_empty = held_explicit;
        return into;
}

/*
        Arithmetic.

        A cursor in file scope rather than one threaded through every grammar
        level. Recursive variable values and indexed subscripts retain and
        restore it explicitly before entering the same evaluator again.
*/
static string_address arith_at HOT_STATE;

/*
        What the expression could not answer.

        Nothing was ever wrong here: a divide by zero was zero, a missing
        operand was whatever had been read so far, and a name holding a word
        was a name holding nothing. All three handed a number to the script
        that the script never computed. dash stops on each of them and leaves
        2 behind, and so does this.
*/
static bool arith_bad HOT_STATE;
static string_address arith_why HOT_STATE;
static p8 arith_token_buf[32] HOT_STATE;
// Where the right operand of the binary operator just read began: bash
// names a zero divisor from there on.
static string_address arith_operand HOT_STATE;
static string_address arith_origin HOT_STATE;
static bool arith_said HOT_STATE;

// Parsing always reaches the far end of a logical or conditional expression,
// but an untaken arm is grammar only: it must not read or write variables or
// raise evaluation errors such as division by zero.
static bool arith_active HOT_STATE;
static bool arith_bash_mode HOT_STATE;
static bool arith_nounset HOT_STATE;
static bool arith_unset HOT_STATE;

// The last primary that is still a variable, so assignment can refuse
// `1 + x = 2` and `y >= z |= 1` while still taking `x = 1` and a
// ternary's middle `x = 1`. The name is copied out of the primary's
// frame: the right-hand side may parse another lvalue on top of it.
static bool arith_is_lvalue HOT_STATE;
static expand_reference arith_held HOT_STATE;
static bipolar arith_held_value HOT_STATE;
static p8 arith_held_name[EXPAND_LOCAL_NAME] HOT_STATE;
static p8 arith_held_key[32] HOT_STATE;

/*
        A persistent `name=$((...))` whose whole right-hand side is one
        arithmetic expansion. The increment fast path may store that name
        once; the executor then skips the second env write. Prefix
        assignments leave this empty so a restored `i=$((i+1)) true`
        still puts `i` back.
*/
static string_address arith_assign_target HOT_STATE;
static positive arith_assign_target_length HOT_STATE;
static bool arith_assign_stored HOT_STATE;
static bool expand_assignment_commit HOT_STATE;

/* Most operators and operands stand against each other, and the ladder asks
   at every level, so the byte in front decides before the set scan runs. */
static PURE inline INLINE string_address arith_skip_space(string_address at)
{
        p8 seen = string_get(at);

        if (seen != ' ' && seen != '\t' && seen != '\n')
                return at;

        return at + 1 + string_span_of_set(at + 1, " \t\n");
}

/*
        Remember why the expression failed, and a short token from where
        the cursor was standing. Bash names both in the diagnostic; dash
        names the whole expression. The first failure sticks -- a later
        leftover byte must not overwrite a division that already happened.
*/
static COLD fn arith_fail_from(string_address why, string_address from)
{
        if (arith_bad)
                return;

        arith_bad = true;
        arith_why = why;

        {
                string_address at = arith_skip_space(from);
                positive n = 0;

                if (!string_get(at) && arith_origin && arith_at > arith_origin)
                {
                        at = arith_at;
                        while (at > arith_origin &&
                               (at[-1] == ' ' || at[-1] == '\t'))
                                at--;
                        if (at > arith_origin)
                                at--;
                }

                //      Bash names the rest of the expression from the
                //      token it stopped at, blanks and all: "+ " for
                //      `2 + `, "* 3 " for ` 1 +* 3 `.
                while (n + 1 < sizeof(arith_token_buf) && string_get(at))
                        arith_token_buf[n++] = string_get(at++);

                arith_token_buf[n] = end;
        }
}

static COLD fn arith_fail(string_address why)
{
        arith_fail_from(why, arith_at);
}

/*
        The arithmetic diagnostic both $(( )) and let/(( share.

        Bash writes the expression, a reason and an error token, and let
        and (( put their own name in front of that. dash writes a shorter
        sentence and no token. Unset-name failures already wrote their
        own line.
*/
COLD fn shell_arith_report(writer write, string_address command,
                           string_address expr)
{
        string_address why;
        string_address token;

        if (arith_unset || arith_said)
                return;

        why = arith_why ? arith_why
                        : (string_address) "arithmetic syntax error in expression";
        token = string_get(arith_token_buf) ? arith_token_buf : expr;
        if (!token || !string_get(token))
                token = expr ? expr : (string_address) "";
        if (!expr)
                expr = (string_address) "";

        shell_diagnostic_where_to(write);

        if (shell_bash_compat)
        {
                expr = arith_skip_space(expr);
                if (command)
                        string_format(write,
                                      "%s: %s: %s (error token is \"%s\")\n",
                                      command, expr, why, token);
                else
                        string_format(write,
                                      "%s: %s (error token is \"%s\")\n",
                                      expr, why, token);
                return;
        }

        if (arith_why && !string_compare(arith_why, "division by 0"))
                string_format(write,
                              "arithmetic expression: division error: "
                              "\"%s\"\n",
                              expr);
        else if (arith_why &&
                 (!string_compare(arith_why, "value too great for base") ||
                  !string_compare(arith_why, "expecting EOF")))
                string_format(write,
                              "arithmetic expression: expecting EOF: "
                              "\"%s\"\n",
                              expr);
        else
                string_format(write,
                              "arithmetic expression: expecting primary: "
                              "\"%s\"\n",
                              expr);
}

static inline INLINE fn arith_space()
{
        arith_at = arith_skip_space(arith_at);
}

static bipolar arith_choose();
static bipolar arith_assign();
static bipolar arith_expression();

/* The name goes by address here, into arith_store and into arith_value_of:
   passed whole it was four words stored to the stack and read back as two
   pairs, a load across two stores that cannot be forwarded, at every
   variable an expression reads. */
//      The subscript of an element bash refused, kept for the store's
//      complaint, and the length that marks the refusal.
static p8 arith_refused_subscript[64];
#define ARITH_KEY_REFUSED (positive_max - 1)
//      Set while arithmetic resolves an element, which reports a refused
//      subscript only if it reads or writes the element.
static bool arith_subscript_quiet;

static fn arith_keep_lvalue(const expand_reference address_to name,
                            bipolar value)
{
        if (name->key_length == ARITH_KEY_REFUSED &&
            name->name_length < EXPAND_LOCAL_NAME)
        {
                memory_copy_end(arith_held_name, name->name, name->name_length);
                arith_held = address_to name;
                arith_held.name = arith_held_name;
                arith_held_value = value;
                arith_is_lvalue = true;
                return;
        }
        if (name->name_length >= EXPAND_LOCAL_NAME ||
            (name->key && name->key_length >= sizeof(arith_held_key)))
        {
                arith_is_lvalue = false;
                return;
        }

        memory_copy_end(arith_held_name, name->name, name->name_length);
        arith_held = address_to name;
        arith_held.name = arith_held_name;
        if (name->key)
        {
                memory_copy_end(arith_held_key, name->key, name->key_length);
                arith_held.key = arith_held_key;
        }
        arith_held_value = value;
        arith_is_lvalue = true;
}

static PURE string_address expand_bracket_end(string_address at, p8 open,
                                              p8 close);
static COLD bool expand_assign_named(expand_reference reference,
                                     string_address value);

// Writing a name back, which every assigning form ends with.
static bipolar arith_store(const expand_reference address_to reference,
                           bipolar value)
{
        p8 written[32];

        if (!arith_active)
                return 0;

        if (reference->key_length == ARITH_KEY_REFUSED)
        {
                expand_where();
                string_format(writer_stderr_once,
                              "%s[%s]: bad array subscript\n",
                              reference->name, reference->key);
                return value;
        }

        bipolar_into_string(written, value);
        if (!expand_assign_named(address_to reference, written))
        {
                arith_bad = true;
                arith_said = true;
                expand_assign_refused(address_to reference);

                if (!arith_bash_mode)
                        expand_fatal_status(2);

                return arith_bash_mode ? 0 : value;
        }

        return value;
}

/*
        Resolve the bracket immediately after an arithmetic variable name.

        The parameter expander already owns subscript expansion, indexed
        arithmetic, negative-index adjustment and associative keys. Reusing
        that path here keeps one array lookup grammar. Its arithmetic call is
        nested inside this evaluator, so retain the outer cursor and error
        state explicitly. An inactive short-circuit arm only advances over
        the balanced bracket: Bash neither reads nor mutates its subscript.
*/

static COLD bool arith_element_name(expand_reference address_to reference)
{
        string_address open = arith_at;
        string_address close = expand_bracket_end(open + 1, '[', ']');
        string_address after;
        string_address held_at;
        bool held_bad;
        bool held_active;
        bool nested_bad;

        if (!close)
        {
                arith_bad = true;
                return false;
        }

        after = close + 1;
        arith_at = after;

        if (!arith_active)
                return true;

        held_at = arith_at;
        held_bad = arith_bad;
        held_active = arith_active;
        arith_subscript_quiet = shell_bash_compat;
        reference->key = shell_expand_subscript(reference->name, reference->name_length,
            open + 1, (positive)(close - open - 1), &reference->key_length);
        arith_subscript_quiet = false;
        nested_bad = arith_bad;
        //      A subscript counting back past the start of its array: bash
        //      has said so, reads the element as zero and writes nothing.
        if (!reference->key && !nested_bad && !expand_failed &&
            shell_bash_compat)
        {
                positive kept = min((positive)(close - open - 1),
                                    (positive)sizeof(arith_refused_subscript) - 1);

                memory_copy_end(arith_refused_subscript, open + 1, kept);
                reference->key = arith_refused_subscript;
                reference->key_length = ARITH_KEY_REFUSED;
        }
        arith_at = held_at;
        arith_bad = held_bad || nested_bad;
        arith_active = held_active;

        return reference->key != null;
}

static CONST bipolar arith_negate(bipolar value)
{
        return (bipolar)(0 - (positive)value);
}

/*
        A plain decimal operand, and only that.

        Leading zeros are octal in this grammar, 08 is a diagnostic, and
        0x / base# are a different literal. Those all belong to the walker.
        A lone 0 is a decimal zero. Anything that does not fit in a signed
        machine word is left to the saturating reader as well.

        x is the value and y one past its last digit, or both are zero when
        this is not one: the answer comes back in the two return registers,
        where a cursor and a value handed in by address went to the stack
        and back on each side of every literal and every counter's value.
        The C asked at each digit whether the next would overflow; eighteen
        digits never can and nineteen are still below 2^64, so the digits
        are only counted and one test at the end refuses what a signed word
        cannot hold. A digit is a load, a range test and two multiply-adds,
        the loop's only taken branch is its own, and one digit, which is
        most of them, counts nothing. The byte after the digits is looked
        up in string_set_name, the name bytes expand_name_character asks
        for.
*/
positive2 arith_plain_natural(string_address at);

#if X64
__asm__(
    ASM_FUNC(arith_plain_natural)
    "movzbl (%rdi), %eax\n   sub $48, %eax\n   cmp $9, %eax\n   ja 8f\n"
    "lea string_set_name(%rip), %r10\n   test %eax, %eax\n   jz 5f\n"
    "lea 1(%rdi), %rdx\n   movzbl (%rdx), %ecx\n   lea -48(%rcx), %r9d\n   cmp $9, %r9d\n   ja 4f\n"
    "1:  lea (%rax,%rax,4), %rax\n   lea (%r9,%rax,2), %rax\n   inc %rdx\n"
    "movzbl (%rdx), %ecx\n   lea -48(%rcx), %r9d\n   cmp $9, %r9d\n   jbe 1b\n"
    // %ecx is the byte after the digits, %rdx where it stands.
    "cmpb $0, (%r10,%rcx)\n   jne 8f\n   mov %rdx, %rcx\n   sub %rdi, %rcx\n   cmp $18, %rcx\n   ja 6f\n"
    ASM_RET
    "4:  cmpb $0, (%r10,%rcx)\n   jne 8f\n"
    ASM_RET
    // A zero on its own: not in front of a digit, a letter, _ or #.
    "5:  movzbl 1(%rdi), %ecx\n   cmp $35, %ecx\n   je 8f\n   cmpb $0, (%r10,%rcx)\n   jne 8f\n"
    "lea 1(%rdi), %rdx\n"
    ASM_RET
    // Nineteen digits are exact in 64 bits and fit when the sign is clear.
    "6:  cmp $19, %rcx\n   ja 8f\n   test %rax, %rax\n   js 8f\n"
    ASM_RET
    "8:  xor %eax, %eax\n   xor %edx, %edx\n"
    ASM_RET
    ASM_END(arith_plain_natural)
);
#elif ARM64
__asm__(
    ASM_FUNC(arith_plain_natural)
    "ldrb w2, [x0]\n   sub w2, w2, #48\n   cmp w2, #9\n   b.hi 8f\n"
    "adrp x10, string_set_name\n   add x10, x10, :lo12:string_set_name\n   cbz w2, 5f\n"
    "add x1, x0, #1\n   ldrb w4, [x1]\n   sub w5, w4, #48\n   cmp w5, #9\n   b.hi 4f\n"
    "1:  add x2, x2, x2, lsl #2\n   add x2, x5, x2, lsl #1\n"
    "ldrb w4, [x1, #1]!\n   sub w5, w4, #48\n   cmp w5, #9\n   b.ls 1b\n"
    // w4 is the byte after the digits, x1 where it stands.
    "ldrb w6, [x10, x4]\n   cbnz w6, 8f\n   sub x6, x1, x0\n   cmp x6, #18\n   b.hi 6f\n"
    "3:  mov x0, x2\n"
    ASM_RET
    "4:  ldrb w6, [x10, x4]\n   cbnz w6, 8f\n   mov x0, x2\n"
    ASM_RET
    // A zero on its own: not in front of a digit, a letter, _ or #.
    "5:  ldrb w4, [x0, #1]\n   cmp w4, #35\n   b.eq 8f\n   ldrb w6, [x10, x4]\n   cbnz w6, 8f\n"
    "add x1, x0, #1\n   mov x0, #0\n"
    ASM_RET
    // Nineteen digits are exact in 64 bits and fit when the sign is clear.
    "6:  cmp x6, #19\n   b.hi 8f\n   tbz x2, #63, 3b\n"
    "8:  mov x0, #0\n   mov x1, #0\n"
    ASM_RET
    ASM_END(arith_plain_natural)
);
#elif RISCV64
__asm__(
    ASM_FUNC(arith_plain_natural)
    "lbu t0, 0(a0)\n   addi t0, t0, -48\n   li t1, 10\n   bgeu t0, t1, 8f\n"
    "lla t6, string_set_name\n   beqz t0, 5f\n"
    "addi a1, a0, 1\n   lbu t3, 0(a1)\n   addi t4, t3, -48\n   bgeu t4, t1, 4f\n"
    "1:  slli t5, t0, 2\n   add t0, t0, t5\n   slli t0, t0, 1\n   add t0, t0, t4\n"
    "addi a1, a1, 1\n   lbu t3, 0(a1)\n   addi t4, t3, -48\n   bltu t4, t1, 1b\n"
    // t3 is the byte after the digits, a1 where it stands.
    "add t5, t6, t3\n   lbu t5, 0(t5)\n   bnez t5, 8f\n   sub t5, a1, a0\n   li t4, 18\n   bgtu t5, t4, 6f\n"
    "3:  mv a0, t0\n"
    ASM_RET
    "4:  add t5, t6, t3\n   lbu t5, 0(t5)\n   bnez t5, 8f\n   mv a0, t0\n"
    ASM_RET
    // A zero on its own: not in front of a digit, a letter, _ or #.
    "5:  lbu t3, 1(a0)\n   li t4, 35\n   beq t3, t4, 8f\n   add t5, t6, t3\n   lbu t5, 0(t5)\n   bnez t5, 8f\n"
    "addi a1, a0, 1\n   li a0, 0\n"
    ASM_RET
    // Nineteen digits are exact in 64 bits and fit when the sign is clear.
    "6:  li t4, 19\n   bgtu t5, t4, 8f\n   bgez t0, 3b\n"
    "8:  li a0, 0\n   li a1, 0\n"
    ASM_RET
    ASM_END(arith_plain_natural)
);
#endif

/*
        A variable that already holds a decimal, the way a loop counter does
        after the first assignment: blanks, a sign, a plain decimal as
        arith_plain_natural reads one, blanks and the end. x is the value
        and y is one when the text is that, both zero when it is not. Octal,
        bases and nested expressions go through arith_value_of instead of
        being guessed at here, and a null or empty text is zero, as an
        unset or empty name is.

        Every read of a counter comes through here, and the C paid a call
        to arith_plain_natural and a cursor and a value through the stack
        on each side of it and of this. The digits are read in the same
        body now, a digit in front goes straight to them with blanks and a
        sign asked only when it is not one, and since only blanks and the
        end may follow the digits, the name byte and the # the literal
        reader refuses after a number are refused here by that rule and
        never looked up.
*/
positive2 arith_plain_scalar(string_address text);

#if X64
__asm__(
    ASM_FUNC(arith_plain_scalar)
    "test %rdi, %rdi\n   jz 9f\n   movzbl (%rdi), %eax\n   xor %r11d, %r11d\n"
    "sub $48, %eax\n   cmp $9, %eax\n   ja 30f\n"
    // The digits, the first one's value in %eax at %rdi.
    "2:  lea 1(%rdi), %rdx\n   movzbl (%rdx), %ecx\n   test %eax, %eax\n   jz 4f\n"
    "lea -48(%rcx), %r9d\n   cmp $9, %r9d\n   ja 4f\n"
    "3:  lea (%rax,%rax,4), %rax\n   lea (%r9,%rax,2), %rax\n   inc %rdx\n"
    "movzbl (%rdx), %ecx\n   lea -48(%rcx), %r9d\n   cmp $9, %r9d\n   jbe 3b\n"
    // Nineteen digits are exact in 64 bits and fit when the sign is clear.
    "mov %rdx, %r8\n   sub %rdi, %r8\n   cmp $18, %r8\n   jbe 4f\n"
    "cmp $19, %r8\n   ja 9f\n   test %rax, %rax\n   js 9f\n"
    // %ecx is the byte after the digits, or after a lone 0, at %rdx.
    "4:  test %ecx, %ecx\n   jnz 12f\n"
    "8:  test %r11d, %r11d\n   jnz 14f\n   mov $1, %edx\n"
    ASM_RET
    "14: neg %rax\n   mov $1, %edx\n"
    ASM_RET
    // Blanks after the number, then the end.
    "12: cmp $32, %ecx\n   je 13f\n   cmp $9, %ecx\n   jne 9f\n"
    "13: inc %rdx\n   movzbl (%rdx), %ecx\n   test %ecx, %ecx\n   jz 8b\n   jmp 12b\n"
    // Not a digit in front: blanks, then the end, a sign or a digit.
    "30: add $48, %eax\n"
    "31: cmp $32, %eax\n   je 32f\n   cmp $9, %eax\n   jne 33f\n"
    "32: inc %rdi\n   movzbl (%rdi), %eax\n   jmp 31b\n"
    "33: test %eax, %eax\n   jz 7f\n   cmp $45, %eax\n   je 34f\n   cmp $43, %eax\n   je 35f\n"
    "sub $48, %eax\n   cmp $9, %eax\n   jbe 2b\n   jmp 9f\n"
    "34: mov $1, %r11d\n"
    "35: inc %rdi\n   movzbl (%rdi), %eax\n   sub $48, %eax\n   cmp $9, %eax\n   jbe 2b\n"
    "9:  xor %eax, %eax\n   xor %edx, %edx\n"
    ASM_RET
    "7:  xor %eax, %eax\n   mov $1, %edx\n"
    ASM_RET
    ASM_END(arith_plain_scalar)
);
#elif ARM64
__asm__(
    ASM_FUNC(arith_plain_scalar)
    "cbz x0, 9f\n   ldrb w2, [x0]\n   mov w11, #0\n"
    "sub w2, w2, #48\n   cmp w2, #9\n   b.hi 30f\n"
    // The digits, the first one's value in w2 at x0.
    "2:  add x1, x0, #1\n   ldrb w4, [x1]\n   cbz w2, 4f\n"
    "sub w5, w4, #48\n   cmp w5, #9\n   b.hi 4f\n"
    "3:  add x2, x2, x2, lsl #2\n   add x2, x5, x2, lsl #1\n"
    "ldrb w4, [x1, #1]!\n   sub w5, w4, #48\n   cmp w5, #9\n   b.ls 3b\n"
    // Nineteen digits are exact in 64 bits and fit when the sign is clear.
    "sub x6, x1, x0\n   cmp x6, #18\n   b.ls 4f\n   cmp x6, #19\n   b.hi 9f\n   tbnz x2, #63, 9f\n"
    // w4 is the byte after the digits, or after a lone 0, at x1.
    "4:  cbnz w4, 12f\n"
    "8:  cbnz w11, 14f\n   mov x0, x2\n   mov x1, #1\n"
    ASM_RET
    "14: neg x0, x2\n   mov x1, #1\n"
    ASM_RET
    // Blanks after the number, then the end.
    "12: cmp w4, #32\n   ccmp w4, #9, #4, ne\n   b.ne 9f\n"
    "ldrb w4, [x1, #1]!\n   cbz w4, 8b\n   b 12b\n"
    // Not a digit in front: blanks, then the end, a sign or a digit.
    "30: add w2, w2, #48\n"
    "31: cmp w2, #32\n   ccmp w2, #9, #4, ne\n   b.ne 33f\n   ldrb w2, [x0, #1]!\n   b 31b\n"
    "33: cbz w2, 7f\n   cmp w2, #45\n   b.eq 34f\n   cmp w2, #43\n   b.eq 35f\n"
    "sub w2, w2, #48\n   cmp w2, #9\n   b.ls 2b\n   b 9f\n"
    "34: mov w11, #1\n"
    "35: ldrb w2, [x0, #1]!\n   sub w2, w2, #48\n   cmp w2, #9\n   b.ls 2b\n"
    "9:  mov x0, #0\n   mov x1, #0\n"
    ASM_RET
    "7:  mov x0, #0\n   mov x1, #1\n"
    ASM_RET
    ASM_END(arith_plain_scalar)
);
#elif RISCV64
__asm__(
    ASM_FUNC(arith_plain_scalar)
    "beqz a0, 9f\n   lbu t0, 0(a0)\n   li a5, 0\n   li t6, 10\n"
    "addi t0, t0, -48\n   bgeu t0, t6, 30f\n"
    // The digits, the first one's value in t0 at a0.
    "2:  addi a1, a0, 1\n   lbu t3, 0(a1)\n   beqz t0, 4f\n"
    "addi t4, t3, -48\n   bgeu t4, t6, 4f\n"
    "3:  slli t5, t0, 2\n   add t0, t0, t5\n   slli t0, t0, 1\n   add t0, t0, t4\n"
    "addi a1, a1, 1\n   lbu t3, 0(a1)\n   addi t4, t3, -48\n   bltu t4, t6, 3b\n"
    // Nineteen digits are exact in 64 bits and fit when the sign is clear.
    "sub t5, a1, a0\n   li t4, 18\n   bleu t5, t4, 4f\n   li t4, 19\n   bgtu t5, t4, 9f\n   bltz t0, 9f\n"
    // t3 is the byte after the digits, or after a lone 0, at a1.
    "4:  bnez t3, 12f\n"
    "8:  bnez a5, 14f\n   mv a0, t0\n   li a1, 1\n"
    ASM_RET
    "14: neg a0, t0\n   li a1, 1\n"
    ASM_RET
    // Blanks after the number, then the end.
    "12: li t1, 32\n   beq t3, t1, 13f\n   li t1, 9\n   bne t3, t1, 9f\n"
    "13: addi a1, a1, 1\n   lbu t3, 0(a1)\n   beqz t3, 8b\n   j 12b\n"
    // Not a digit in front: blanks, then the end, a sign or a digit.
    "30: addi t0, t0, 48\n"
    "31: li t1, 32\n   beq t0, t1, 32f\n   li t1, 9\n   bne t0, t1, 33f\n"
    "32: addi a0, a0, 1\n   lbu t0, 0(a0)\n   j 31b\n"
    "33: beqz t0, 7f\n   li t1, 45\n   beq t0, t1, 34f\n   li t1, 43\n   beq t0, t1, 35f\n"
    "addi t0, t0, -48\n   bltu t0, t6, 2b\n   j 9f\n"
    "34: li a5, 1\n"
    "35: addi a0, a0, 1\n   lbu t0, 0(a0)\n   addi t0, t0, -48\n   bltu t0, t6, 2b\n"
    "9:  li a0, 0\n   li a1, 0\n"
    ASM_RET
    "7:  li a0, 0\n   li a1, 1\n"
    ASM_RET
    ASM_END(arith_plain_scalar)
);
#endif

/*
        The number a name holds, and the value itself when it is not one.

        Unset and empty are both zero. Anything else Bash reads as an
        expression rather than a number, so with a=b and b=7 $((a)) is seven
        and x="1 + 2" is three. dash does not: a value that is not one
        number is illegal. That is the caller's to do, and this function
        hands back the bytes instead of evaluating them.

        Splitting it there is not tidiness. A loop counter is a name holding
        digits read tens of thousands of times, and reaching the grammar from
        in here would put this function inside the arithmetic recursion, where
        nothing about it can be inlined into the caller that asks the
        question. The scratch belongs to the caller for the same reason: what
        comes back may point into it.
*/
static bipolar arith_number_of(expand_reference reference, p8 address_to scratch,
                               string_address address_to expression)
{
        bool present;
        bool valid;
        string_address value;
        string_address step;
        string_address digits;
        positive magnitude;
        bool negative = false;

        address_to expression = null;

        if (!arith_active)
                return 0;

        value = expand_value_of(reference, scratch, address_of present, null);

        if (!present)
        {
                if (arith_nounset)
                {
                        arith_bad = true;
                        arith_unset = true;
                        expand_unbound(reference, false);
                }

                return 0;
        }

        // A counter holds plain decimal digits, which is one pass here.
        {
                positive2 plain = arith_plain_scalar(value);

                if (plain.y)
                        return (bipolar)plain.x;
        }

        step = value + string_span(value, string_set_blanks);

        if (!string_get(step))
                return 0;

        if (string_is(step, '-') || string_is(step, '+'))
        {
                negative = string_is(step, '-');
                step++;
        }

        digits = step;
        magnitude = expand_base_positive(address_of step, address_of valid,
                                         arith_bash_mode ? 0 : (positive)bipolar_max + 1,
                                         arith_bash_mode);

        if (step == digits || !valid)
        {
                address_to expression = value;
                return 0;
        }

        step += string_span(step, string_set_blanks);

        if (string_get(step))
        {
                address_to expression = value;
                return 0;
        }

        // Bash's digits have already wrapped; dash's stopped at the limit.
        return bipolar_from_magnitude(
            negative || arith_bash_mode ? magnitude
                                        : min(magnitude, (positive)bipolar_max),
            negative);
}

/*
        A value that is not a number, read as an expression of its own.

        The value is copied first. What it is about to be evaluated as may
        assign a name, and an assignment is free to move the storage the value
        was still being read out of.

        A chain that comes back to a name already on it never ends, so the
        depth is counted rather than trusted -- a=b; b=a has no answer and must
        say so instead of running until the machine stack is gone.
*/
#define ARITH_NAMES 32

static positive arith_names HOT_STATE;

static COLD bipolar arith_named_expression(string_address value)
{
        p8 held_local[EXPAND_LOCAL_NAME];
        string_address held;
        string_address outer = arith_at;
        bipolar answer;

        if (arith_names >= ARITH_NAMES)
        {
                arith_bad = true;
                return 0;
        }

        held = expand_hold(value, string_length(value), held_local,
                           sizeof(held_local));

        if (!held)
        {
                arith_bad = true;
                return 0;
        }

        arith_names++;
        arith_at = held;
        answer = arith_expression();

        // Every byte of the value belongs to the expression, exactly as every
        // byte of the outer one does: x=12ab is not twelve.
        if (string_get(arith_at))
                arith_fail("arithmetic syntax error in expression");

        arith_at = outer;
        arith_names--;

        return answer;
}

/*
        dash names the value, not the expression, and stops. arith_said
        keeps the later arithmetic report from writing a second sentence
        over that.
*/
static COLD fn arith_illegal_number(string_address value)
{
        if (arith_bad)
                return;

        arith_bad = true;
        arith_said = true;
        shell_diagnostic_where_to(writer_stderr_once);
        string_format(writer_stderr_once, "Illegal number: %s\n",
                      value ? value : (string_address) "");
}

// What a name is worth to the grammar, which is the number it holds or,
// in bash, the answer to the expression it holds. dash stops: a value
// that is not one number is not an expression.
static bipolar arith_value_of(const expand_reference address_to reference)
{
        p8 scratch[32];
        string_address expression;

        if (reference->key_length == ARITH_KEY_REFUSED)
        {
                if (arith_active)
                {
                        expand_where();
                        string_format(writer_stderr_once,
                                      "%s: bad array subscript\n",
                                      reference->name);
                }
                return 0;
        }

        bipolar value = arith_number_of(address_to reference, scratch,
                                        address_of expression);

        if (!expression)
                return value;

        if (!arith_bash_mode)
        {
                arith_illegal_number(expression);
                return 0;
        }

        return arith_named_expression(expression);
}

/*
        The one division the machine will not do.

        A zero divisor faults. The smallest number over minus one used
        to share that diagnostic, because its opposite is not a number
        this width holds and the instruction raises SIGFPE. Bash and
        dash answer the quotient as the number itself -- wrapping --
        and the remainder as zero.
*/
static bipolar arith_divide(bipolar left, bipolar right, bool remainder)
{
        if (!arith_active)
                return 0;

        if (!right)
        {
                arith_fail_from("division by 0",
                                arith_operand ? arith_operand : arith_at);
                return 0;
        }

        if (right == -1 && left == bipolar_min)
                return remainder ? 0 : left;

        return remainder ? left % right : left / right;
}

// Shell arithmetic is the target machine word. Express wrapping through the
// unsigned type so compiler overflow assumptions cannot change that contract.
static CONST bipolar arith_addition(bipolar left, bipolar right)
{
        return (bipolar)((positive)left + (positive)right);
}

static CONST bipolar arith_subtraction(bipolar left, bipolar right)
{
        return (bipolar)((positive)left - (positive)right);
}

static CONST bipolar arith_product(bipolar left, bipolar right)
{
        return (bipolar)((positive)left * (positive)right);
}

/*
        Raising to a power, which the machine has no instruction for.

        Squaring the base and halving the exponent is six-and-a-bit steps for
        the largest exponent that answers anything at all, against sixty-three
        for repeated multiplication -- and the wrapping is the same either way,
        because every step goes through arith_product.

        A negative exponent is not a small number here: Bash refuses it rather
        than answering with the zero that integer division would give.
*/
static bipolar arith_power_of(bipolar base, bipolar exponent)
{
        bipolar value = 1;

        if (!arith_active)
                return 0;

        if (exponent < 0)
        {
                arith_fail("exponent less than 0");
                return 0;
        }

        while (exponent)
        {
                if (exponent & 1)
                        value = arith_product(value, base);

                exponent = (bipolar)((positive)exponent >> 1);

                if (exponent)
                        base = arith_product(base, base);
        }

        return value;
}

static CONST bipolar arith_shift_left(bipolar left, bipolar right)
{
        positive count = (positive)right & (positive_bits - 1);

        return (bipolar)((positive)left << count);
}

static CONST bipolar arith_shift_right(bipolar left, bipolar right)
{
        positive count = (positive)right & (positive_bits - 1);

        return left >> count;
}

// What the operator in front of the = does.
static bipolar arith_combine(p8 op, bipolar left, bipolar right)
{
        if (!arith_active)
                return 0;

        switch (op)
        {
        case '+': return arith_addition(left, right);
        case '-': return arith_subtraction(left, right);
        case '*': return arith_product(left, right);
        case '/': return arith_divide(left, right, false);
        case '%': return arith_divide(left, right, true);
        case '&': return left & right;
        case '|': return left | right;
        case '^': return left ^ right;
        case 'l': return arith_shift_left(left, right);
        case 'r': return arith_shift_right(left, right);
        }

        return right;
}


/*
        base#digits, with the base written out in front in decimal.

        The alphabet is 0-9, then the lower-case letters, then the upper-case
        ones, then @ and _, which is sixty-four places. Only bases past
        thirty-six have room for both cases, so below that the two are the
        same letter -- which is why what a letter is worth depends on the base
        and not on the letter alone.

        arith_at is on the first digit of the base and hash is the # behind
        it, because the caller found it while deciding this was not an
        ordinary number.
*/
static COLD fn arith_based_refused(string_address start, string_address why)
{
        string_address held = arith_at;

        if (!arith_bash_mode)
        {
                arith_bad = true;
                return;
        }

        // Bash names the literal, from its first digit, and says why.
        arith_at = start;
        arith_fail(why);
        arith_at = held;
}

static bipolar arith_based(string_address hash)
{
        string_address start = arith_at;
        positive base = 0;
        positive value = 0;
        bool any = false;

        // A base of a hundred digits is still not a base. Stopping the
        // accumulation once it is out of range keeps it out of the wrap that
        // would bring it back into range.
        while (arith_at < hash)
        {
                if (base < 1024)
                        base = base * 10 +
                               (positive)(string_get(arith_at) - '0');

                arith_at++;
        }

        arith_at++;

        if (base < 2 || base > 64)
        {
                arith_based_refused(start, "invalid arithmetic base");
                return 0;
        }

        while (1)
        {
                p8 seen = string_get(arith_at);
                positive digit;

                if (byte_is_digit(seen))
                        digit = (positive)(seen - '0');
                else if (byte_is_lower(seen))
                        digit = (positive)(seen - 'a') + 10;
                else if (byte_is_upper(seen))
                        digit = (positive)(seen - 'A') + (base <= 36 ? 10 : 36);
                else if (seen == '@')
                        digit = 62;
                else if (seen == '_')
                        digit = 63;
                else
                        break;

                if (digit >= base)
                {
                        arith_based_refused(start, "value too great for base");
                        return 0;
                }

                value = value * base + digit;
                any = true;
                arith_at++;
        }

        if (!any)
                arith_based_refused(start, "invalid integer constant");

        return (bipolar)value;
}

// Each lvalue owns its name and subscript storage through the whole operation.
// Recursive right operands may allocate and rewind their own inner marks.
// Assignment is not consumed here: it is looser than every binary operator
// and is recognised after the ternary, so `y >= z |= 1` is not `z |= 1`.
static bipolar arith_lvalue(p8 prefix)
{
        string_address start = arith_at;
        p8 name_local[EXPAND_LOCAL_NAME];
        expand_reference name = {0};
        positive2 named = expand_name_hash(start);
        positive length = named.y;
        shell_mark held;
        bipolar value = 0;

        arith_is_lvalue = false;
        arith_at += length;

        /*
                A plain read of a scalar that holds a decimal, which is what a
                loop counter is: the span in the expression is hashed as it
                is measured and probed as it stands, where the name used to be
                copied out to be terminated and hashed again from the copy.
                A prefix or postfix operator, a subscript, an untaken arm, a
                value that is not one plain number and anything the table does
                not hold as a scalar take the walk below, unchanged.
        */
        if (!prefix && length && length < EXPAND_LOCAL_NAME && arith_active &&
            string_not(arith_at, '[') &&
            !(shell_bash_compat && memory_is_word(start, length, "PIPESTATUS")))
        {
                string_address after = arith_skip_space(arith_at);
                p8 op = string_get(after);

                if (!((op == '+' || op == '-') && string_get(after + 1) == op))
                {
                        string_address raw = env_get_hashed_span(
                            start, length, named.x, null);

                        positive2 plain = arith_plain_scalar(raw);

                        if (plain.y)
                        {
                                value = (bipolar)plain.x;
                                arith_at = after;
                                name.name = start;
                                name.name_length = length;
                                arith_keep_lvalue(address_of name, value);
                                return value;
                        }
                }
        }

        bool element = length && !shell_dash_compat && string_is(arith_at, '[');
        if (element)
                held = shell_store_mark(address_of expand_store);
        name.name = expand_hold(start, length, name_local, sizeof(name_local));
        name.name_length = length;
        if (element && name.name && !arith_element_name(&name))
                name.name = null;

        if (!length || !name.name)
        {
                arith_bad = true;
                goto done;
        }

        if (prefix)
        {
                value = arith_value_of(address_of name);
                value = arith_store(address_of name,
                                    prefix == '+' ? arith_addition(value, 1)
                                                  : arith_subtraction(value, 1));
                goto done;
        }

        arith_space();
        //      dash reads x++ as x + +, which wants an operand.
        if (!shell_dash_compat &&
            ((string_is(arith_at, '+') && string_is(arith_at + 1, '+')) ||
             (string_is(arith_at, '-') && string_is(arith_at + 1, '-'))))
        {
                bool increment = string_is(arith_at, '+');

                value = arith_value_of(address_of name);
                arith_at += 2;
                arith_store(address_of name,
                            increment ? arith_addition(value, 1)
                                      : arith_subtraction(value, 1));
        }
        else
        {
                // A plain = never reads what it replaces: x=hello, or x
                // unset under set -u, takes $((x=3)) as dash and bash do.
                if (!(string_is(arith_at, '=') && string_not(arith_at + 1, '=')))
                        value = arith_value_of(address_of name);
                arith_keep_lvalue(address_of name, value);
        }

done:
        if (element)
                shell_store_rewind(address_of expand_store, held);
        return value;
}

/*
        How deep a parenthesis or a prefix operator may nest before the stack
        is asked about it. Every level is a descent through the ladder of
        precedences, some two hundred bytes of stack, and nothing held it:
        15,000 open parentheses in a value -- [[ $x -eq 1 ]] on input a
        script was handed -- took the shell down with SIGSEGV. Past this many
        a level asks how much of the stack is used, once in sixty-four, and
        past three quarters of it the expression is refused with the stack
        whole; bash's own limit is on the depth of values that name values,
        which nesting here is not.
*/
#define ARITH_NESTING 1024
#define ARITH_NESTING_MAX 1048576
static positive arith_nesting HOT_STATE;
static bipolar arith_primary_step();

static bipolar arith_primary()
{
        bipolar value;

        //      Neither dash nor the bash this is read against stops at a
        //      number: this goes as far as the stack does, three quarters
        //      of it, which is ten thousand parentheses and a good deal more.
        if (arith_nesting >= ARITH_NESTING &&
            (arith_nesting >= ARITH_NESTING_MAX ||
             (!(arith_nesting & 63) && !shell_stack_within(75))))
        {
                arith_fail("expression recursion level exceeded");
                return 0;
        }
        arith_nesting++;
        value = arith_primary_step();
        arith_nesting--;
        return value;
}

static bipolar arith_primary_step()
{
        bipolar value = 0;

        arith_space();
        arith_is_lvalue = false;

        if (byte_is_digit(string_get(arith_at)))
        {
                bool valid;
                string_address scan = arith_at;
                string_address start = arith_at;

                // A plain decimal is most literals a script writes. A # behind
                // it makes it a base, which the walker below reads.
                {
                        positive2 plain = arith_plain_natural(arith_at);

                        //      dash has no bases: 2#101 is a 2 and a stray
                        //      byte for the leftover check to name.
                        if (plain.y && (shell_dash_compat ||
                                        string_not((string_address)plain.y, '#')))
                        {
                                arith_at = (string_address)plain.y;
                                return (bipolar)plain.x;
                        }
                }

                // What is in front of a # is a base and not a value, and only
                // a run of plain decimal digits can be one: 0x10#1 is neither.
                scan += string_span(scan, string_set_digits);

                if (string_is(scan, '#'))
                        return arith_based(scan);

                value = expand_base_number(address_of arith_at, address_of valid,
                                          arith_bash_mode);

                if (!valid)
                {
                        /* 08 is refused as an octal digit. The cursor has
                           already walked past the leading 0, so the token
                           bash names is restored from the start of the
                           literal, then the cursor goes back to after it. */
                        string_address held = arith_at;

                        arith_at = start;
                        arith_fail("value too great for base");
                        arith_at = held;
                }

                return value;
        }

        /*
                A name with no dollar in front of it, which is the one place in
                the language where that reads a variable. Numbers and names
                are most operands and neither starts with an operator, so
                they are asked first.
        */
        if (expand_name_character(string_get(arith_at)))
                return arith_lvalue(0);


        if (string_is(arith_at, '('))
        {
                arith_at++;
                value = arith_expression();

                if (string_is(arith_at, ')'))
                        arith_at++;
                else
                        arith_bad = true;

                arith_is_lvalue = false;
                return value;
        }

        if (string_is(arith_at, '!'))
        {
                arith_at++;
                value = !arith_primary();
                arith_is_lvalue = false;
                return value;
        }

        if (string_is(arith_at, '~'))
        {
                arith_at++;
                value = ~arith_primary();
                arith_is_lvalue = false;
                return value;
        }

        if (arith_bash_mode &&
            ((string_is(arith_at, '+') && string_is(arith_at + 1, '+')) ||
             (string_is(arith_at, '-') && string_is(arith_at + 1, '-'))) &&
            expand_assignable_name(arith_skip_space(arith_at + 2)))
        {
                p8 prefix = string_get(arith_at);

                arith_at += 2;
                arith_space();
                return arith_lvalue(prefix);
        }

        if (string_is(arith_at, '-'))
        {
                arith_at++;
                value = arith_negate(arith_primary());
                arith_is_lvalue = false;
                return value;
        }

        if (string_is(arith_at, '+'))
        {
                arith_at++;
                value = arith_primary();
                arith_is_lvalue = false;
                return value;
        }

        // A byte that starts no value at all, which is where a missing
        // operand lands: $((1 + )) answered 1 and $((2 ** 3)) answered 0.
        arith_fail(arith_bash_mode ? "arithmetic syntax error: operand expected"
                                  : "expecting primary");

        return 0;
}

/*
        Raising to a power, which sits between the unary operators and the
        products and leans right: 2 ** 3 ** 2 is two to the ninth.

        Leaning right is also why the whole level is one function and not a
        loop -- and why -2 ** 2 is four rather than minus four: the minus is
        part of the primary underneath, so it is the base that is negative and
        not the answer.
*/
static bipolar arith_power()
{
        bipolar value = arith_primary();

        arith_space();

        //      dash has no **: the first star is a product's and the
        //      second is a primary that is not there.
        if (!string_is(arith_at, '*') || !string_is(arith_at + 1, '*') ||
            (shell_dash_compat && !arith_bash_mode))
                return value;

        arith_at += 2;
        value = arith_power_of(value, arith_power());
        arith_is_lvalue = false;
        return value;
}

/*
        The binary operators, one level to a kind, the loosest first: || &&
        then | ^ & then == != then the four comparisons, the two shifts, + -,
        and * / % tightest. Where the cursor stands on one, the level it is
        and the bytes it takes; a level of nought where it does not. An
        operator followed by = is the assignment's, <<= and >>= too.
*/
static positive arith_operator(p8 op, p8 next, positive address_to width)
{
        *width = 1;

        switch (op)
        {
        case '*': case '/': case '%':
                return next != '=' ? 10 : 0;
        case '+': case '-':
                return next != '=' ? 9 : 0;
        case '<': case '>':
                if (next == op)
                {
                        *width = 2;
                        return string_is(arith_at + 2, '=') ? 0 : 8;
                }
                *width = next == '=' ? 2 : 1;
                return 7;
        case '=': case '!':
                *width = 2;
                return next == '=' ? 6 : 0;
        case '&':
                if (next == '&')
                {
                        *width = 2;
                        return 2;
                }
                return next != '=' ? 5 : 0;
        case '^':
                return next != '=' ? 4 : 0;
        case '|':
                if (next == '|')
                {
                        *width = 2;
                        return 1;
                }
                return next != '=' ? 3 : 0;
        }

        return 0;
}

/*
        Every level from floor up in one function: an operand, then the
        operators of that level and above, each taking the operand after it
        from the level above its own, so a run of equal ones leans left. A
        function to a level made a parenthesis cost one call for each of
        them, and a parenthesis is all that a deep expression is.

        arith_power returns past trailing whitespace, and so does this: every
        production keeps that cursor, so each operator is read where it stands
        without scanning the same span again.
*/
static bipolar arith_binary(positive floor)
{
        bipolar value = arith_power();

        while (true)
        {
                p8 op = string_get(arith_at);
                p8 next = op ? string_get(arith_at + 1) : 0;
                positive width;
                positive level = arith_operator(op, next, address_of width);

                if (level < floor || !level)
                        return value;

                arith_at += width;

                // The right of && and || is read whether it is wanted or not, for
                // the cursor's sake, and has no effect where it is not.
                if (level < 3)
                {
                        bool active = arith_active;

                        arith_active = active && (level == 2 ? value != 0 : value == 0);
                        bipolar far = arith_binary(level + 1);
                        arith_active = active;
                        arith_is_lvalue = false;
                        value = !active ? 0 : level == 2 ? (value && far) : (value || far);
                        continue;
                }

                string_address operand = arith_at;
                bipolar right = arith_binary(level + 1);

                arith_operand = operand;
                arith_is_lvalue = false;

                switch (level)
                {
                case 10:
                        value = op == '*' ? arith_product(value, right)
                                          : arith_divide(value, right, op == '%');
                        break;
                case 9:
                        value = op == '+' ? arith_addition(value, right)
                                          : arith_subtraction(value, right);
                        break;
                case 8:
                        value = op == '<' ? arith_shift_left(value, right)
                                          : arith_shift_right(value, right);
                        break;
                case 7:
                        value = op == '<' ? (next == '=' ? value <= right : value < right)
                                          : (next == '=' ? value >= right : value > right);
                        break;
                case 6:
                        value = op == '=' ? value == right : value != right;
                        break;
                case 5:
                        value &= right;
                        break;
                case 4:
                        value ^= right;
                        break;
                default:
                        value |= right;
                }
        }
}


/*
        The ternary, looser than the two logical levels and tighter than an
        assignment. There was no level here at all, so $((1 ? 2 : 3)) stopped
        after the condition and answered with it.

        Both arms are read whichever one is wanted, for the same reason && and
        || read both sides: the cursor has to come out of the far end of the
        expression, and skipping an arm leaves it in the middle of one.
*/
static bipolar arith_choose()
{
        bipolar value = arith_binary(1);
        bipolar taken;
        bipolar left;
        bool active;


        if (!string_is(arith_at, '?'))
                return value;

        arith_at++;
        active = arith_active;
        arith_active = active && value;
        taken = arith_expression();

        if (string_is(arith_at, ':'))
                arith_at++;
        else
                arith_bad = true;

        arith_active = active && !value;
        left = arith_choose();
        arith_active = active;
        arith_is_lvalue = false;

        return active ? (value ? taken : left) : 0;
}


/*
        Assignment is looser than the ternary and tighter than comma.

        The left side has to still be a variable: `x = 1` writes, and so
        does the middle of `1 ? x = 1 : 0`, but `y >= z |= 1` is an
        assignment to the comparison. Bash names that; dash leaves the
        operator for the leftover-byte check, which is "expecting EOF".
*/
static bipolar arith_assign();

/*
        What the assignment does, once its operator has been read: out of line,
        for its copy of the name and of the subscript, 300 bytes of a frame
        that every parenthesis of an expression has one of, and a thousand of
        them are what a shell that does not stop at a thousand has to hold.
*/
static __attribute__((noinline)) bipolar arith_assign_to(bipolar value, p8 kind, b32 skip)
{
        expand_reference target;
        p8 name_local[EXPAND_LOCAL_NAME];
        p8 key_local[32];

        if (!arith_is_lvalue)
        {
                if (arith_bash_mode)
                {
                        string_address at = arith_at;
                        positive n = 0;

                        arith_fail("attempted assignment to non-variable");
                        while (n + 1 < sizeof(arith_token_buf) &&
                               string_get(at))
                                arith_token_buf[n++] = string_get(at++);
                        arith_token_buf[n] = end;
                }

                return value;
        }

        target = arith_held;
        memory_copy_end(name_local, arith_held_name, target.name_length);
        target.name = name_local;
        //      A refused element keeps pointing at its subscript's text.
        if (target.key && target.key_length != ARITH_KEY_REFUSED)
        {
                memory_copy_end(key_local, arith_held_key, target.key_length);
                target.key = key_local;
        }

        {
                bipolar left = arith_held_value;
                bipolar right;

                string_address operand;

                arith_at += skip;
                operand = arith_at;
                arith_is_lvalue = false;
                right = arith_assign();

                if (kind == '=')
                        return arith_store(address_of target, right);

                arith_operand = operand;

                return arith_store(address_of target,
                                   arith_combine(kind, left, right));
        }
}

static bipolar arith_assign()
{
        bipolar value = arith_choose();
        p8 op;
        p8 next;
        p8 third;
        p8 kind = 0;
        b32 skip = 0;

        arith_space();
        op = string_get(arith_at);
        next = op ? string_get(arith_at + 1) : 0;
        third = next ? string_get(arith_at + 2) : 0;

        if (op == '=' && next != '=')
        {
                kind = '=';
                skip = 1;
        }
        else if (op == '<' && next == '<' && third == '=')
        {
                kind = 'l';
                skip = 3;
        }
        else if (op == '>' && next == '>' && third == '=')
        {
                kind = 'r';
                skip = 3;
        }
        else if (next == '=' &&
                 (op == '+' || op == '-' || op == '*' || op == '/' ||
                  op == '%' || op == '&' || op == '|' || op == '^'))
        {
                kind = op;
                skip = 2;
        }

        if (!kind)
                return value;

        return arith_assign_to(value, kind, skip);
}

// The comma sequence is the outer arithmetic grammar, including parenthesis
// groups, recursive variable values and a ternary's middle operand. The
// middle is a full assignment expression; the right arm is only another
// ternary, so `0 ? 1 : x |= 2` assigns to the conditional.
static bipolar arith_expression()
{
        bipolar value = arith_assign();

        while (arith_bash_mode && string_is(arith_at, ','))
        {
                arith_at++;
                arith_is_lvalue = false;
                value = arith_assign();
        }

        return value;
}

/*
        A loop counter is a name plus one.

        $((i + 1)) and the assigning cousins a script actually writes used
        to walk every precedence level for two operands and one operator.
        The forms below are recognised as a whole expression. Anything
        else -- parentheses, a comma, a ternary, a base, a quote, a dollar,
        an octal -- is left on the cursor for the grammar.

        A miss does not move arith_at. A hit leaves it at the far end, so
        the leftover-byte check still means every byte belonged.
*/
static HOT bool arith_increment_fast(bipolar address_to value)
{
        string_address at = arith_at;
        p8 first = string_get(at);
        p8 name_local[EXPAND_LOCAL_NAME];
        expand_reference name = {0};
        string_address name_start;
        positive name_length;
        bool prefix = false;
        bool postfix = false;
        bool assign = false;
        bool add = true;
        bipolar delta = 1;
        bipolar held;
        bipolar next;
        positive2 hashed;
        positive2 plain;
        bool same;

        if (arith_bash_mode && (first == '+' || first == '-') &&
            string_get(at + 1) == first)
        {
                prefix = true;
                add = first == '+';
                at = arith_skip_space(at + 2);
        }

        if (!expand_assignable_name(at))
                return false;

        name_start = at;
        hashed = expand_name_hash(at);
        name_length = hashed.y;
        if (!name_length || name_length >= EXPAND_LOCAL_NAME)
                return false;

        at += name_length;
        if (string_is(at, '['))
                return false;

        /* Bash's PIPESTATUS may still be deferred in the executor, and the
           table holds the vector before it: arith_lvalue's walk asks for it
           by name, and so must a counter read here. */
        if (shell_bash_compat &&
            memory_is_word(name_start, name_length, "PIPESTATUS"))
                return false;

        at = arith_skip_space(at);

        if (prefix)
        {
                if (string_get(at))
                        return false;
        }
        else
        {
                p8 op = string_get(at);
                p8 more = string_get(at + 1);

                if (arith_bash_mode && (op == '+' || op == '-') &&
                    more == op)
                {
                        postfix = true;
                        add = op == '+';
                        at = arith_skip_space(at + 2);
                        if (string_get(at))
                                return false;
                }
                else if ((op == '+' || op == '-') && more == '=')
                {
                        assign = true;
                        add = op == '+';
                        at = arith_skip_space(at + 2);
                        plain = arith_plain_natural(at);
                        if (!plain.y)
                                return false;
                        at = (string_address)plain.y;
                        delta = (bipolar)plain.x;
                        at = arith_skip_space(at);
                        if (string_get(at))
                                return false;
                }
                else if (op == '+' || op == '-')
                {
                        add = op == '+';
                        at = arith_skip_space(at + 1);
                        plain = arith_plain_natural(at);
                        if (!plain.y)
                                return false;
                        at = (string_address)plain.y;
                        delta = (bipolar)plain.x;
                        at = arith_skip_space(at);
                        if (string_get(at))
                                return false;
                }
                else
                        return false;
        }

        memory_copy_end(name_local, name_start, name_length);
        name.name = name_local;
        name.name_length = name_length;

        {
                string_address raw = env_get_hashed_span(name_local, hashed.y,
                                                         hashed.x, null);

                plain = arith_plain_scalar(raw);
                held = plain.y ? (bipolar)plain.x
                               : arith_value_of(address_of name);
        }

        arith_at = at;
        if (arith_bad)
        {
                address_to value = held;
                return true;
        }

        next = add ? arith_addition(held, delta)
                   : arith_subtraction(held, delta);

        same = arith_assign_target &&
               arith_assign_target_length == name_length &&
               !memory_compare(arith_assign_target, name_local, name_length);

        if (prefix || assign)
        {
                address_to value = arith_store(address_of name, next);
                if (same && !arith_bad)
                        arith_assign_stored = true;
        }
        else if (postfix)
        {
                arith_store(address_of name, next);
                address_to value = held;
        }
        else if (same &&
                 !env_assignment_readonly_hashed_span(name_local, hashed.y,
                                                      hashed.x))
        {
                address_to value = arith_store(address_of name, next);
                if (!arith_bad)
                        arith_assign_stored = true;
        }
        else
                address_to value = next;

        return true;
}

static bipolar arith_evaluate(string_address text)
{
        bipolar value;
        bool held_nounset = arith_nounset;

        arith_bad = false;
        arith_unset = false;
        arith_why = null;
        arith_token_buf[0] = end;
        arith_operand = null;
        arith_said = false;
        arith_active = true;
        arith_is_lvalue = false;
        arith_nounset =
            shell_bash_compat &&
            (shell_options & ((positive)1 << ('u' - 'a'))) != 0;
        arith_at = text;
        arith_origin = text;
        arith_space();

        bool held_bash_mode = arith_bash_mode;

        /* The same arithmetic engine serves POSIX expansion and Bash-only
           commands. A Bash personality gives every complex arithmetic
           context the extensions those commands already requested
           explicitly; native sh/dash keeps the stricter grammar unless its
           caller opted in. */
        arith_bash_mode = arith_bash_mode || shell_bash_compat;

        // An arithmetic expansion contains an expression, not an optional
        // expression. Empty input used to turn into a plausible zero. Bash
        // still answers 0 for `$(())`; dash refuses it.
        if (!string_get(arith_at))
        {
                if (!arith_bash_mode)
                        arith_fail("expecting primary");
                arith_nounset = held_nounset;
                arith_bash_mode = held_bash_mode;
                return 0;
        }

        if (!arith_increment_fast(address_of value))
                value = arith_expression();

        // Every byte has to belong to the grammar. This catches comma and
        // postfix increment/decrement instead of returning the left prefix.
        if (string_get(arith_at))
                arith_fail(arith_bash_mode ? "arithmetic syntax error in expression"
                                          : "expecting EOF");

        arith_nounset = held_nounset;
        arith_bash_mode = held_bash_mode;
        return value;
}

static string_address arith_subscripts_held(string_address text);

/*
        Parameter, command and quote expansion of an arithmetic body.

        $((i + 1)) has nothing to expand. Walking it through the parameter
        expander allocates a second copy of the same bytes.
*/
/*
        Whether a quote stands in an arithmetic body outside every $( ), ${ }
        and backquote pair. dash leaves quotes in the expression, where no
        operand begins with one, so "1" + 2 is an error there and the two
        bytes of quote are never removed as bash removes them.
*/
static COLD __attribute__((noinline)) bool arith_body_quoted(string_address text)
{
        positive depth = 0;

        for (; string_get(text); text++)
        {
                p8 value = string_get(text);

                if (value == '$' && (string_is(text + 1, '(') ||
                                     string_is(text + 1, '{')))
                {
                        depth++;
                        text++;
                }
                else if (depth && (value == ')' || value == '}'))
                        depth--;
                else if (!depth && (value == '"' || value == '\''))
                        return true;
        }

        return false;
}

static string_address arith_expand_body(string_address text)
{
        if (!string_get(text + string_span_without_set(text, "$`\"'\\")))
                return text;

        if (shell_dash_compat && arith_body_quoted(text))
                return text;

        return expand_capture(arith_subscripts_held(text), true,
                              EXPAND_CAPTURE_TEXT);
}

/*
        The interior of ((...)) has the same parameter, command and quote
        expansion as arithmetic expansion, but it is a command token rather
        than a word. Capture it whole: no field splitting or pathname lookup.
*/
static string_address shell_expand_arithmetic_text(string_address text)
{
        expand_begin();

        return arith_expand_body(text);
}

/*
        Where the quoted run that opens here ends, one byte past its close.

        A single quoted run ends at the next quote and a backslash inside it is
        an ordinary byte; a double quoted run lets a backslash carry the byte
        behind it, so \" does not close one. A run that is never closed ends at
        the terminating null, and every caller's loop ends there too.

        Three scanners below walk a word looking for one byte that is theirs --
        the bracket that closes an expansion, the / that separates a
        replacement, the } that closes a brace list -- and none of them may
        find it inside quotes. Written out three times, the three had to be
        kept level by hand.
*/
static PURE string_address expand_quoted_run(string_address at, p8 quote)
{
        at = lex_quote_end(at + 1, quote);
        return string_get(at) ? at + 1 : at;
}

// A dollar-single-quoted run has already been kept whole by the lexer.  Its
// escaped quote is not the closing quote, so syntax scanners use the same end
// rule as the lexer rather than the ordinary single-quote rule above.
static string_address expand_dollar_quoted_run(string_address at)
{
        string_address stop = lex_dollar_quote_end(at + 2);

        return string_get(stop) ? stop + 1 : stop;
}

/*
        Quote-aware closer. A dollar-single, a backslash, or a quoted run
        can hide the bracket that would otherwise close, and a POSIX
        double-quoted ${#/%} keeps a single quote as a byte.

        `${...}` ends at the first unquoted `}`. A nested brace list is
        bytes of the word, not another expansion, so `{a,b}` does not
        raise the depth. Nested `${`, `$( )`, process substitution and
        backticks still hide a `}` of their own. `$( )` / `$(( ))` keep
        counting parentheses.
*/
static COLD PURE string_address expand_bracket_end_quoted(string_address at,
                                                         p8 open, p8 close,
                                                         bool posix_double,
                                                         bool parameter)
{
        positive depth = 1;
        bool dollar_brace = parameter && open == '{';
        bool raw_single = posix_double && open == '{' && shell_posix_on() &&
                          !lex_parameter_pattern(at);

        while (string_get(at))
        {
                p8 value = string_get(at);

                if (value == '$' && string_is(at + 1, '\''))
                {
                        at = expand_dollar_quoted_run(at);
                        continue;
                }

                if (dollar_brace && value == '$' &&
                    (string_is(at + 1, '(') || string_is(at + 1, '{')))
                {
                        if (string_is(at + 1, '{'))
                        {
                                depth++;
                                at += 2;
                                continue;
                        }

                        {
                                string_address stop = expand_bracket_end_quoted(
                                    at + 2, '(', ')', false, false);

                                if (!stop)
                                        return null;
                                at = stop + 1;
                                continue;
                        }
                }

                if (dollar_brace && (value == '<' || value == '>') &&
                    string_is(at + 1, '('))
                {
                        string_address stop = expand_bracket_end_quoted(
                            at + 2, '(', ')', false, false);

                        if (!stop)
                                return null;
                        at = stop + 1;
                        continue;
                }

                if (dollar_brace && value == '`')
                {
                        at++;
                        while (string_get(at) && string_get(at) != '`')
                        {
                                if (string_is(at, '\\') && string_get(at + 1))
                                        at += 2;
                                else
                                        at++;
                        }
                        if (!string_get(at))
                                return null;
                        at++;
                        continue;
                }

                if (value == '\\' && string_get(at + 1))
                {
                        at += 2;
                        continue;
                }

                if (value == '\'' && raw_single)
                {
                        at++;
                        continue;
                }

                if (value == '\'' || value == '"')
                {
                        at = expand_quoted_run(at, value);
                        continue;
                }

                if (value == open && !dollar_brace)
                        depth++;

                if (value == close && !--depth)
                        return at;

                at++;
        }

        return null;
}

/*
        $((i + 1)) and ${x#?} have none of those. Count open and close until
        the matching bracket; a quote, a backslash or a dollar starts the
        walker above again from the front, so nested depth is not lost.
        A parameter closer does not count a bare `{`.
*/
static inline INLINE HOT PURE string_address expand_bracket_end_mode(
        string_address at, p8 open, p8 close, bool posix_double,
        bool parameter)
{
        string_address start = at;
        positive depth = 1;
        bool dollar_brace = parameter && open == '{';

        while (1)
        {
                p8 value = string_get(at);

                if (value == close)
                {
                        if (!--depth)
                                return at;
                }
                else if (!value)
                        return null;
                else if (value == '\'' || value == '"' || value == '\\' ||
                         value == '$' ||
                         (dollar_brace &&
                          (value == '`' ||
                           ((value == '<' || value == '>') &&
                            string_is(at + 1, '(')))))
                        return expand_bracket_end_quoted(start, open, close,
                                                        posix_double,
                                                        parameter);
                else if (value == open && !dollar_brace)
                        depth++;

                at++;
        }
}

static PURE string_address expand_bracket_end(string_address at, p8 open,
                                              p8 close)
{
        return expand_bracket_end_mode(at, open, close, false, false);
}

#define expand_paren_end(at) \
        expand_bracket_end_mode((at), '(', ')', false, false)
#define expand_parameter_end(at, posix_double) \
        expand_bracket_end_mode((at), '{', '}', (posix_double), true)

/*
        An array subscript is expanded once, by the evaluator as it reads the
        element (arith_element_name), which is where bash expands it. The
        body used to be expanded whole first, so the subscript was expanded
        twice and data became code: with i='$(cmd)', $(( a[$i] )) ran cmd
        where bash stops at an operand it cannot read, and (( m[$k]++ )) read
        quotes and brackets out of an associative key. Each subscript is
        carried through the body's expansion as text, its $, `, " and \
        escaped, and the evaluator expands it the one time.

        Bash holds a subscript by where it is written, not by how deep: one
        inside double quotes or in the word of ${x:-...} and ${x:+...} is
        the source's too, and runs nothing a value held; the word is text
        the expression reads. A subscript a value or a substitution makes
        is expanded by the evaluator, in bash as here.
*/
static p8 address_to arith_hold_span(p8 address_to out, string_address text,
                                     string_address at, string_address stop,
                                     bool quoted)
{
        while (at < stop)
        {
                p8 value = string_get(at);
                string_address run = null;

                if (value == '\\' && at + 1 < stop)
                        run = at + 2;
                else if (value == '"' && !quoted)
                {
                        string_address close = expand_quoted_run(at, '"');

                        if (close <= stop && close > at + 1 &&
                            string_is(close - 1, '"'))
                        {
                                *out++ = '"';
                                out = arith_hold_span(out, text, at + 1,
                                                      close - 1, true);
                                *out++ = '"';
                                at = close;
                                continue;
                        }
                }
                else if (value == '\'' && !quoted)
                        run = expand_quoted_run(at, value);
                else if (value == '`')
                        run = lex_nesting(at);
                else if (value == '$' && string_is(at + 1, '('))
                        run = lex_nesting(at + 1);
                else if (value == '$' && string_is(at + 1, '{'))
                {
                        string_address close =
                            expand_parameter_end(at + 2, quoted);
                        string_address word = at + 2;

                        if (close && close < stop)
                        {
                                if (string_is(word, '#') || string_is(word, '!'))
                                        word++;
                                if (expand_assignable_name(word))
                                        word += string_span(word, string_set_name);
                                else if (word < close)
                                        word++;
                                if (string_is(word, '['))
                                {
                                        string_address shut = expand_bracket_end(
                                            word + 1, '[', ']');

                                        word = shut && shut < close ? shut + 1
                                                                    : close;
                                }
                                if (string_is(word, ':'))
                                        word++;
                                if (word < close && (string_is(word, '-') ||
                                                     string_is(word, '+')))
                                {
                                        word++;
                                        memory_copy_apart(out, at,
                                                          (positive)(word - at));
                                        out += word - at;
                                        out = arith_hold_span(out, text, word,
                                                              close, quoted);
                                        *out++ = '}';
                                        at = close + 1;
                                        continue;
                                }
                                run = close + 1;
                        }
                }
                else if (value == '[' && at > text &&
                         expand_name_character(string_get(at - 1)))
                {
                        string_address close =
                            expand_bracket_end(at + 1, '[', ']');

                        if (close && close < stop)
                        {
                                *out++ = '[';
                                for (at++; at < close; at++)
                                {
                                        p8 inner = string_get(at);

                                        if (inner == '$' || inner == '`' ||
                                            inner == '"' || inner == '\\')
                                                *out++ = '\\';
                                        *out++ = inner;
                                }
                                continue;
                        }
                }

                if (!run || run <= at || run > stop)
                        run = at + 1;
                while (at < run)
                        *out++ = string_get(at++);
        }

        return out;
}

//      Answers the body unchanged when it has no subscript to carry.
static string_address arith_subscripts_held(string_address text)
{
        positive length;
        p8 address_to made;

        if (!string_first_of(text, '['))
                return text;

        length = string_length(text);
        made = shell_store_take(address_of expand_store, length * 2 + 1);
        if (!made)
                return text;

        *arith_hold_span(made, text, text, text + length, false) = 0;
        return made;
}

/*
        Whether this process is a substitution's child.

        Its exit ends a word, not the shell, so what the shell does on the way
        out is not its to do: running the exit trap in here wrote the trap's
        output down the substitution pipe and folded it into the word.
*/
static bool expand_in_substitution;

/*
        The no-shell command-substitution path.

        A literal multicall utility needs neither the outer shell's address
        space nor another parser. Strip only quoting whose contents are
        provably literal, then let Spark attach the existing pipe to a fresh
        /shell utility. Anything with expansion, globbing, operators, an
        alias, or a function declines this path and keeps the full shell.
*/
static string_address address_to expand_tool_argv;
static positive expand_tool_argv_room;

/* Remove quoting from one word which has no expansion. Long literal runs use
   the same hardware-floor byte-set scans as ordinary expansion. */
static p8 address_to expand_tool_word(string_address word, positive length,
                                      p8 address_to out)
{
        string_address at = word;
        string_address stop = word + length;

        expand_sets_prepare();

        while (at < stop)
        {
                positive run = string_span_max(
                    at, (positive)(stop - at), expand_literal_set);

                if (run)
                {
                        memory_copy_apart(out, at, run);
                        out += run; at += run;
                }

                if (at == stop)
                        break;

                p8 value = *at++;

                if (value == '\'')
                {
                        string_address close = (string_address)memory_first_of(
                            at, '\'', (positive)(stop - at));

                        if (!close)
                                return null;

                        run = (positive)(close - at);
                        memory_copy_apart(out, at, run);
                        out += run; at = close + 1;
                }
                else if (value == '"')
                {
                        while (true)
                        {
                                run = string_span_max(
                                    at, (positive)(stop - at), expand_inside_set);
                                if (run)
                                {
                                        memory_copy_apart(out, at, run);
                                        out += run; at += run;
                                }

                                if (at == stop ||
                                    (value = *at++) == '$' || value == '`')
                                        return null;
                                if (value == '"')
                                        break;
                                if (at == stop)
                                        return null;

                                value = *at++;
                                if (value == '$' || value == '`' ||
                                    value == '"' || value == '\\')
                                        *out++ = value;
                                else if (value != '\n')
                                {
                                        *out++ = '\\';
                                        *out++ = value;
                                }
                        }
                }
                else if (value == '\\' && at < stop)
                {
                        value = *at++;
                        if (value != '\n')
                                *out++ = value;
                }
                else
                        return null; /* expansion, brace expansion or glob */
        }

        return out;
}

static bipolar expand_tool_direct(string_address command, b32 output)
{
        static p8 address_to text;
        static positive text_room;
        b32 count;
        bipolar child = -1;
        positive2 named;
        bool quiet = false;
        p8 address_to out;

        /* Execution only retains the parser's stable copy of its tokens. The
           lexer scratch is therefore free to reuse without a nested mapping. */
        count = lex_line(command);

        // The end token stops short at a top-level newline or comment.
        if (count <= 0 || string_get(command + lex_tokens[count].at))
                goto done;

        if (count >= 3)
        {
                lex_token address_to descriptor = lex_tokens + count - 3;
                lex_token address_to redirect = descriptor + 1;
                lex_token address_to target = redirect + 1;

                if (descriptor->kind == LEX_WORD && descriptor->length == 1 &&
                    string_is(descriptor->text, '2') &&
                    redirect->kind == LEX_OPERATOR && redirect->op == OP_GREAT &&
                    target->kind == LEX_WORD &&
                    memory_is_word(target->text, target->length, "/dev/null") &&
                    descriptor->at + descriptor->length == redirect->at &&
                    redirect->at + redirect->length == target->at)
                {
                        quiet = true;
                        count -= 3;
                }
        }

        if (!count ||
            !shell_array_room(text, text_room, string_length(command) + 1) ||
            !shell_array_room(expand_tool_argv, expand_tool_argv_room, (positive)count + 1))
                goto done;

        out = text;

        for (b32 at = 0; at < count; at++)
        {
                if (lex_tokens[at].kind != LEX_WORD)
                        goto done;

                expand_tool_argv[at] = out;
                out = expand_tool_word(lex_tokens[at].text,
                                       lex_tokens[at].length, out);

                if (!out)
                        goto done;

                *out++ = 0;
        }

        expand_tool_argv[count] = null;
        named = string_hash_33_length(expand_tool_argv[0]);

        if (!shell_tool_only_here(expand_tool_argv[0], named) ||
            exec_function_here_hashed(expand_tool_argv[0], named) ||
            alias_lookup(expand_tool_argv[0]))
                goto done;

        child = shell_spawn_tool(expand_tool_argv, output, quiet);

done:
        return child;
}

/*
        A command substitution.

        The text runs in a child whose standard output is a pipe, and what it
        wrote becomes the bytes. Reading to the end before waiting is what keeps
        a command that writes more than a pipe holds from wedging both sides.
*/
static fn expand_substitution_body(string_address command, bool capture);

//      A substitution's bytes, read to the end of fd, which is then closed.
//      The newlines at the end go, and only the ones at the end: that is the
//      single piece of editing a substitution is allowed.
//      A NUL byte cannot live in a word, so it is dropped and the bytes
//      after it kept, as both references do; it ended the word, and
//      $(printf 'a\0b') was a where they give ab. bash says so once.
static bool expand_substitution_keep;

static fn expand_read_substitution(b32 fd, p8 mark, positive start)
{
        p8 block[512];
        bipolar got;
        bool dropped = false;

        while ((got = system_read_retry((positive)fd, block, sizeof(block))) > 0)
        {
                positive at = 0;

                while (at < (positive)got)
                {
                        positive run = memory_span_byte(block + at, 0,
                                                        (positive)got - at);

                        if (run)
                        {
                                dropped = true;
                                at += run;
                                continue;
                        }

                        p8 address_to nul = memory_first_of(block + at, 0,
                                                            (positive)got - at);
                        positive take = nul ? (positive)(nul - (block + at))
                                            : (positive)got - at;

                        expand_push_run(block + at, take, mark);
                        at += take;
                }
        }

        if (dropped && shell_bash_compat)
        {
                expand_where();
                writer_stderr_once(str("warning: command substitution: ignored null byte in input\n"));
        }

        system_close(fd);

        if (!expand_substitution_keep)
                expand_length -= memory_span_byte_reverse(
                    expand_text + start, '\n', expand_length - start);
}

static fn expand_substitution_done(b32 status)
{
        shell_substitution_status = status;
        shell_substitution_generation++;
        if (shell_bash_compat && !shell_posix_on())
                shell_status = status;
}

static fn expand_run(string_address command, bool quoted)
{
        p8 mark = quoted ? MARK_QUOTED : MARK_FIELD;
        positive start = expand_length;
        b32 channel[2];
        bipolar child = -1;
        positive status = 0;

        // Whatever this shell has buffered belongs to this shell, and a fork
        // with it still in hand prints all of it a second time.
        log_flush();

        if (system_pipe(address_of channel, SHELL_PIPE_CLOSE_ON_EXEC) < 0)
                return;

        child = expand_tool_direct(command, channel[1]);

        if (child < 0)
                child = shell_clone();

        if (child == 0)
        {
                system_close(channel[0]);
                if (shell_child_fd_move(channel[1],
                                        standard_output_descriptor) < 0)
                        system_call_1(syscall(exit_group), 126);

                expand_substitution_body(command, true);
        }

        system_close(channel[1]);

        if (child <= 0)
        {
                system_close(channel[0]);
                return;
        }

        expand_read_substitution(channel[0], mark, start);
        bipolar waited =
            system_wait4_retry(child, address_of status, 0, null);

        /* A substitution that went through a child shell already wrote this
           line there. A direct spawn has no such child, and dash still names
           the signal the way lima 0.5.x does. Bash keeps substitutions
           silent. */
        if (waited >= 0 && !shell_bash_compat)
                shell_child_death(child, status, true);

        expand_substitution_done(waited < 0 ? 125
                                            : wait_status_code(status));
}

/*
        Bash $(<file) is not a command: it is the file's contents, with the
        same trailing-newline trim as any other substitution.  Spaces and
        tabs around the operator are allowed, as is one trailing semicolon.
        Dash has no such form; a redirect with no command stays silent.
*/
static bool expand_command_file(string_address text, bool quoted)
{
        string_address at = text;
        string_address name;
        string_address name_end;
        string_address path;
        p8 held;
        p8 mark = quoted ? MARK_QUOTED : MARK_FIELD;
        positive start = expand_length;
        bipolar opened;

        if (!shell_bash_compat)
                return false;

        at += string_span_of_set(at, " \t\n");

        if (string_not(at, '<') || string_is(at + 1, '<') ||
            string_is(at + 1, '>') || string_is(at + 1, '('))
                return false;

        at++;

        at += string_span_of_set(at, " \t\n");

        if (!string_get(at) || string_is(at, ';'))
                return false;

        name = at;

        // The name ends at a blank or an operator outside anything quoted or
        // nested: $(<fo$(echo o)) reads foo, as bash does.
        while (string_get(at))
        {
                p8 c = string_get(at);
                b32 skipped = lex_skip_held(address_of at);

                if (skipped == LEX_SKIP_UNCLOSED)
                        return false;
                if (skipped)
                        continue;
                if (lex_is_space(c) || c == ';' || c == '|' || c == '&' ||
                    c == '<' || c == '>')
                        break;
                at++;
        }

        if (at == name)
                return false;

        name_end = at;

        at += string_span_of_set(at, " \t\n");

        if (string_is(at, ';'))
        {
                at++;
                at += string_span_of_set(at, " \t\n");
        }

        if (string_get(at))
                return false;

        held = *name_end;
        *name_end = end;
        path = expand_capture(name, false, EXPAND_CAPTURE_TEXT);
        *name_end = held;

        if (!path)
                return true;

        opened = system_open_at(AT_FDCWD, path, FILE_READ);

        if (opened < 0)
        {
                expand_substitution_done(1);
                return true;
        }

        expand_read_substitution((b32)opened, mark, start);
        expand_substitution_done(0);
        return true;
}

static string_address expand_command(string_address step, bool quoted)
{
        string_address inner = step + 2;
        string_address after = lex_nesting(step + 1);
        string_address stop = after == step + 1 ? null : after - 1;
        p8 address_to text;
        positive length;

        // No closing paren inside this word: the lexer stopped short of it, and
        // a dollar with nothing it can begin is a dollar.
        if (!stop)
        {
                expand_push('$', MARK_PLAIN);
                return step + 1;
        }

        length = (positive)(stop - inner);

        //      Out of the line's own store, so a substitution inside a
        //      substitution gets its own room rather than sharing one buffer.
        text = shell_store_copy(address_of expand_store, inner, length);

        if (!text)
        {
                expand_overflow = true;
                return stop + 1;
        }

        if (!expand_command_file(text, quoted))
                expand_run(text, quoted);

        return stop + 1;
}

/*
        lima dash 0.5.x re-parses a backtick body with list(2).  After the
        first complete command, a leftover `(` is not a syntax error: the
        list ends and the rest of the extracted text is dropped.  `\$()` is
        that leftover -- the backslash made a literal `$` argument, and the
        parenthesis is what stops the list -- so `echo \$(echo x)` runs as
        `echo $`.  Bash parses the whole string and refuses the `(`.
*/
static fn expand_backtick_dash_cut(p8 address_to text)
{
        string_address step = text;

        while (string_get(step))
        {
                if (string_is(step, '\\') && string_is(step + 1, '$') &&
                    string_is(step + 2, '('))
                {
                        step[2] = end;
                        return;
                }

                if (!lex_skip_held(address_of step))
                        step++;
        }
}

static string_address expand_backtick(string_address step, bool quoted,
                                       bool in_double)
{
        p8 address_to text;
        positive length = 0;
        positive room;
        string_address after = lex_nesting(step);
        string_address look = after == step ? null : after - 1;

        if (!look)
        {
                expand_push('`', MARK_PLAIN);
                return step + 1;
        }

        //      The scan above already found the closing backtick, so the
        //      most this can need is the distance to it.
        room = (positive)(look - step) + 1;
        text = shell_store_take(address_of expand_store, room);

        if (!text)
        {
                expand_overflow = true;
                return look + 1;
        }

        step++;

        while (step < look)
        {
                // Inside backticks a backslash only hides the next byte when
                // that byte is one that backticks care about, and inside
                // double quotes the double quote is one of them: the body
                // of "x `echo \"hi\"`" is echo "hi". A here-document body
                // reads it the same way in dash and bash --posix; bash
                // itself keeps the backslash there.
                if (string_is(step, '\\') &&
                    (string_get(step + 1) == '`' || string_get(step + 1) == '\\' ||
                     string_get(step + 1) == '$' ||
                     (in_double && string_get(step + 1) == '"')))
                        step++;

                if (length + 1 < room)
                        text[length++] = string_get(step);

                step++;
        }

        text[length] = end;

        if (!shell_bash_compat)
                expand_backtick_dash_cut(text);

        expand_run(text, quoted);

        return after;
}

/*
        The two things that stop a script where it stands.

        ${x:?} exists to stop, and arithmetic that cannot be answered has
        nothing to hand back -- saying so and carrying on is the one thing
        either must not do. dash leaves 2 behind and runs the exit trap on the
        way out. A terminal is the exception, because there is somebody there
        to type the line again.
*/
/*
        <(command) and >(command).

        The word becomes a path -- /dev/fd/N over a pipe -- and the command
        runs at the other end of it. That is what lets a program which only
        knows how to open files read from a command instead: diff takes two
        file names, and two of these turn two commands into two names.

        Which way the pipe faces is the whole difference between the two.
        <( ) hands over a path this shell can read and the command writes;
        >( ) hands over one this shell writes and the command reads.
*/
typedef struct
{
        b32 descriptor;
        bipolar child;
} expand_substitution;

static expand_substitution address_to expand_substitutions;
static positive expand_substitutions_room;
static positive expand_substitutions_count HOT_STATE;
/* Whether this shell has ever made one. Almost no script does, and the
   executor asks after every command it runs, so one byte in the common case
   is worth having instead of the two the mark and its comparison cost. */
static bool expand_substitutions_ever HOT_STATE;

#define EXPAND_WAIT_NO_HANG 1
#define EXPAND_DUPLICATE_FROM 0
#define EXPAND_SUBSTITUTION_FLOOR 60

/*
        The descriptors this command opened, given back.

        A mark rather than the lot, because the command that made them is not
        always the one running: a function handed /dev/fd/N as an argument may
        open it in its third command, and closing at every command boundary
        would have taken it away after the first.

        The children are asked for without waiting. A shell that stopped here
        would hang on >(sleep 100), which Bash returns from at once; whatever
        is still running is asked again the next time a command finishes.
*/
static fn shell_substitutions_close(positive mark)
{
        positive kept = 0;

        if (mark > expand_substitutions_count)
                mark = expand_substitutions_count;

        for (positive at = 0; at < expand_substitutions_count; at++)
        {
                expand_substitution entry = expand_substitutions[at];
                positive status = 0;
                bipolar reaped;

                /* Entries below the mark belong to an outer command. Its
                   published descriptor remains open, while a child retained
                   after an earlier close is safe to poll again. */
                if (at < mark && entry.descriptor >= 0)
                {
                        expand_substitutions[kept++] = entry;
                        continue;
                }

                if (entry.descriptor >= 0)
                        system_close(entry.descriptor);

                reaped = system_wait4_retry(entry.child,
                                            address_of status,
                                            EXPAND_WAIT_NO_HANG, null);

                if (!reaped)
                {
                        expand_substitutions[kept].descriptor = -1;
                        expand_substitutions[kept].child = entry.child;
                        kept++;
                }
        }

        expand_substitutions_count = kept;
}

/* SIGCHLD may arrive while a retained process-substitution child is the only
   child this shell knows about. Poll closed entries without disturbing the
   descriptors still owned by an active command. */
static fn shell_substitutions_reap()
{
        positive into = 0;

        for (positive at = 0; at < expand_substitutions_count; at++)
        {
                expand_substitution entry = expand_substitutions[at];
                positive status = 0;

                if (entry.descriptor < 0 &&
                    system_wait4_retry(entry.child, address_of status,
                                       EXPAND_WAIT_NO_HANG, null) != 0)
                        continue;

                expand_substitutions[into++] = entry;
        }

        expand_substitutions_count = into;
}

// A fork inherits the list and none of the children on it. Dropping the
// entries leaves the descriptors alone: what the child was handed a path to
// is still a path it may open.
static fn shell_substitutions_forget()
{
        expand_substitutions_count = 0;
}

//      Whether a process is one a <( ) or >( ) started, which wait $! may
//      be asked about.
bool shell_substitution_child(bipolar pid)
{
        return array_any(at, expand_substitutions_count, expand_substitutions[at].child == pid);
}

static bool expand_substitution_remember(b32 descriptor, bipolar child)
{
        if (!shell_array_room(expand_substitutions, expand_substitutions_room,
                              expand_substitutions_count + 1))
                return false;

        expand_substitutions[expand_substitutions_count].descriptor = descriptor;
        expand_substitutions[expand_substitutions_count].child = child;
        expand_substitutions_count++;
        expand_substitutions_ever = true;
        job_child_watch();

        return true;
}

// The body of a substitution, run in the child that is now standing at one
// end of the pipe. Identical to what a command substitution runs, and for
// the same reason: a body is a script and not a line.
static fn expand_substitution_body(string_address command, bool capture)
{
        // parse_reset_all below announces the child. Announcing it here as
        // well counted this one fork twice, so $BASH_SUBSHELL inside a
        // command substitution read one deeper than the shell had gone.
        /* A nested substitution keeps the outer freeze; the first one
           takes the line of the command that wrote `$(...)`. */
        /*
                Inside a compound command the reader has gone on to the end
                of the whole construct before any of it runs, so its count
                is where the construct stops -- the fi, the done -- and not
                the line this command was written on, which is what bash
                reads: if true; then echo $(echo $LINENO); fi said the line
                of the fi, and a here-document's $(echo $LINENO) the line
                after its end. Only a substitution that itself spans lines,
                in a simple command on its own, is read as far as the reader
                got, which is where it ends and so what bash reads for one.
        */
        if (!expand_substitution_lineno)
        {
                if (shell_eval_lineno_base || exec_in_function() ||
                    exec_compound_now() || !string_first_of(command, '\n'))
                        expand_substitution_lineno = shell_line_now();
                else
                        expand_substitution_lineno =
                            shell_line_number ? shell_line_number
                                              : shell_line_now();
        }
        expand_in_substitution = true;
        /* Only Bash command capture clears errexit by default; process
           substitutions and dash inherit it. POSIX mode enables the same
           inherit_errexit bit that shopt exposes, so there is one policy. */
        if (capture && shell_bash_compat && !shell_shopt_on(INHERIT_ERREXIT))
                shell_options &= ~((positive)1 << ('e' - 'a'));
        if (shell_dash_compat)
                exec_tested_forget();
        parse_reset_all();

        if (!string_first_of(command, '\n'))
        {
                shell_tail_line_requested = true;
                lex_physical_newline(false);
                run_line(command);
        }
        else
        {
                string_address at = command;

                while (string_get(at))
                {
                        string_address stop = string_first_of_or_end(at, '\n');

                        if (string_get(stop))
                        {
                                address_to stop = end;
                                stop++;
                        }
                        else
                                lex_physical_newline(false);

                        if (string_get(at))
                                run_line(at);

                        at = stop;
                }
        }

        shell_input_end();
        shell_trap_exit();
        log_flush();
        system_call_1(syscall(exit_group), (positive)shell_status);
}

static string_address expand_process(string_address step, p8 mark)
{
        bool reading = string_is(step, '<');
        string_address inner = step + 2;
        string_address after = lex_nesting(step + 1);
        string_address stop = after == step + 1 ? null : after - 1;
        p8 address_to text;
        positive length;
        b32 channel[2];
        b32 ours;
        b32 theirs;
        bipolar child;
        p8 path[32];
        positive written;

        // No closing parenthesis in the word: the lexer stopped short of it,
        // and what is left is the byte itself.
        if (!stop)
        {
                expand_push(string_get(step), mark);
                return step + 1;
        }

        length = (positive)(stop - inner);
        text = shell_store_copy(address_of expand_store, inner, length);

        if (!text)
        {
                expand_overflow = true;
                return stop + 1;
        }

        // Whatever this shell has buffered belongs to this shell, and a fork
        // with it still in hand writes all of it a second time.
        log_flush();

        /* Reserve the child row before it exists. Losing its pid after fork
           would leave an unreapable zombie and make descendant liveness
           permanently ambiguous. */
        if (!shell_array_room(expand_substitutions,
                              expand_substitutions_room,
                              expand_substitutions_count + 1))
        {
                expand_fatal_status(2);
                return stop + 1;
        }

        if (system_pipe(address_of channel, SHELL_PIPE_CLOSE_ON_EXEC) < 0)
        {
                expand_fatal_status(2);
                return stop + 1;
        }

        ours = reading ? channel[0] : channel[1];
        theirs = reading ? channel[1] : channel[0];

        job_child_watch();
        child = shell_clone();

        if (child == 0)
        {
                system_close(ours);
                if (shell_child_fd_move(
                        theirs, reading ? standard_output_descriptor
                                        : standard_input_descriptor) < 0)
                        system_call_1(syscall(exit_group), 126);
                expand_substitution_body(text, false);
        }

        system_close(theirs);

        /*
                Out of the way of the script's own descriptors.

                A path handed over as /dev/fd/3 is one an "exec 3<file" three
                lines later would quietly take away, so the end this shell
                keeps is moved somewhere nobody writes by hand -- which is
                what Bash's sixty-somethings are. Not close-on-exec: the
                command that opens the path is the one on the far side of an
                exec, and a descriptor it cannot see is a path to nothing.
        */
        {
                bipolar moved = system_call_3(syscall(fcntl), (positive)ours,
                                              EXPAND_DUPLICATE_FROM,
                                              EXPAND_SUBSTITUTION_FLOOR);

                if (moved >= 0)
                {
                        system_close(ours);
                        ours = (b32)moved;
                }
                else if (system_descriptor_install(ours, ours) < 0)
                {
                        /* F_DUPFD can fail when the descriptor limit leaves
                           no room above the substitution floor.  The pipe
                           itself is still usable, but pipe2 made it
                           close-on-exec; clear that bit before publishing a
                           /dev/fd path or fail without handing out a path
                           whose descriptor vanishes at exec. */
                        system_close(ours);
                        expand_fatal_status(2);
                        return stop + 1;
                }
        }

        if (child < 0 || !expand_substitution_remember(ours, child))
        {
                system_close(ours);
                expand_fatal_status(2);
                return stop + 1;
        }

        //      bash counts it as the last thing started asynchronously.
        if (shell_bash_compat)
                shell_background_last = child;

        expand_push_run((string_address) "/dev/fd/", 8, mark);
        written = bipolar_into_string(path, (bipolar)ours);
        expand_push_run(path, written, mark);

        return stop + 1;
}

/*
        Nounset and ${name?} end the process. A -c command that dies of it
        at the top level leaves 127; a subshell or substitution of that
        command leaves 1, because the -c bit is still in $- either way.
        Errexit is already a failing-command exit: with -e the same
        expansion leaves 1 even at the top of -c. Dash answers 2, or 1
        when the name arrived through indirection.
*/
static PURE b32 expand_nounset_status(b32 indirect)
{
        if (shell_bash_compat)
                return (string_is(shell_option_flags, 'c') &&
                        !shell_subshell_depth &&
                        !(shell_options & ((positive)1 << ('e' - 'a'))))
                           ? 127
                           : 1;
        return indirect ? 1 : 2;
}

/*
        What bash calls discarding the command: the error ends the command
        being read, not the shell. A script read from a file or from standard
        input goes on at its next command; a -c string is a single command to
        that reader and ends, and so does a subshell. dash keeps its fatal
        answer, and so does an interactive shell's own line recovery.
*/
static COLD fn expand_discard(b32 status)
{
        if (expand_errors_soft)
        {
                expand_failed = true;
                return;
        }
        if (!shell_bash_compat || shell_is_interactive ||
            string_is(shell_option_flags, 'c'))
        {
                expand_fatal_status(status);
                return;
        }

        shell_status = status;
        expand_failed = true;
        exec_expand_input_error();
}

/*
        The same discard, reached through bash's top_level_cleanup: a
        subscript that cannot be evaluated, or a builtin handed words it has
        no place for (exit 1 2, shift 1 2, return 1 2, break 1 2). eval, a
        sourced file and a function do not catch it; the whole command the
        reader was running is dropped and the reader goes on at its next one
        with status 2. A -c string and a substitution end with 1.
*/
static bool expand_discard_whole_line;

static COLD fn expand_discard_whole(b32 status)
{
        if (string_is(shell_option_flags, 'c') || expand_in_substitution)
                status = 1;
        //      A soft failure -- PS4's -- discards nothing, and the mark would
        //      outlive it: the next eval or function would refuse to catch a
        //      recoverable error because of it.
        if (!expand_errors_soft)
                expand_discard_whole_line = shell_bash_compat;
        expand_discard(status);
}

static COLD fn expand_fatal_status(b32 status)
{
        if (expand_errors_soft)
        {
                expand_failed = true;
                return;
        }

        shell_status = status;

        if (expand_redirect_error)
        {
                expand_failed = true;
                return;
        }

        if (shell_is_interactive)
        {
                expand_failed = true;
                exec_abort_line(shell_status);
                return;
        }

        /* The child marker suppresses an inherited EXIT action while still
           allowing one installed inside a substitution to run. The parent
           reaches the same existing exit boundary here. */
        shell_trap_exit();

        log_flush();
        system_call_1(syscall(exit_group), status);
}

/*
        Arithmetic expansion $(( )) that cannot produce a word, and a
        leftover or nested ${} that is a bad substitution.

        Dash and bash --posix end the process: 2, or the nounset -c
        status (127 at the top of bash --posix -c). let and (( )) are
        commands and keep their recoverable status. Bash without posix
        reuses a bad slice's unwind -- the family scripts `exec 2>/dev/null`
        before the word, and that personality continues with status 1.
        Interactive shells recover at the next line either way. Invalid
        ${!name} is not this class: bash continues under --posix too.
*/
// Set while an arithmetic expression's own failure is being raised, which an
// interactive bash survives under -e where every other expansion error ends
// it; exec_abort_line reads it.
static bool expand_arithmetic_failing;

static COLD fn expand_arithmetic_error()
{
        if (shell_bash_compat && !shell_posix_on())
        {
                expand_slice_error();
                return;
        }

        expand_fatal_status(expand_nounset_status(false));
}

static HOT string_address expand_arithmetic_finish(string_address ready,
                                                   string_address stop,
                                                   bool quoted)
{
        p8 written[32];

        // A nested expansion already diagnosed the whole word.  In an
        // interactive shell that diagnosis returns here instead of exiting
        // the process, so nothing after it may parse the partial capture or
        // perform one of its side effects.
        if (expand_failed)
                return stop + 2;

        {
                bipolar value = arith_evaluate(ready);
                positive written_length;

                if (arith_bad)
                {
                        /* A subscript that could not be read has already
                           said so and ended what it had to end. */
                        if (!arith_unset && !expand_failed)
                        {
                                shell_arith_report(writer_stderr_once, null,
                                                   ready);
                                expand_arithmetic_failing = true;
                                expand_arithmetic_error();
                                expand_arithmetic_failing = false;
                        }

                        return stop + 2;
                }

                written_length = bipolar_into_string(written, value);
                expand_push_run(written, written_length,
                                quoted ? MARK_QUOTED : MARK_FIELD);
        }

        return stop + 2;
}

/*
        POSIX gives $(( precedence, but only when that expansion can be
        parsed as arithmetic. The closer is `))`. A first `)` that is not
        that closer means the bytes were `$(` and a `(...)` subshell, so
        `$((exit 7) | true)` is command substitution of a pipeline rather
        than a dollar followed by the source text. Bash extracts `$(`
        first and only then asks whether the body is `(expr)`.
*/
//      dash reads $(( as arithmetic and nothing else: a body that does not
//      close with )) is a syntax error and not a subshell in a substitution.
static COLD string_address expand_arithmetic_missing(string_address step)
{
        expand_where();
        writer_stderr_once(str("Syntax error: Missing '))'\n"));
        expand_fatal_status(2);

        return step + string_length(step);
}

static COLD string_address expand_arithmetic_complex(string_address step,
                                                     bool quoted)
{
        string_address inner = step + 3;
        string_address stop = expand_bracket_end_quoted(inner, '(', ')', false,
                                                       false);
        p8 text_local[EXPAND_LOCAL_TEXT];
        string_address text;
        positive length;

        if (!stop || string_get(stop + 1) != ')')
                return shell_dash_compat ? expand_arithmetic_missing(step)
                                         : expand_command(step, quoted);

        length = (positive)(stop - inner);
        text = expand_hold(inner, length, text_local, sizeof(text_local));
        if (!text)
                return stop + 2;

        return expand_arithmetic_finish(arith_expand_body(text), stop, quoted);
}

/*
        $[ expr ], the spelling bash kept from before $(( )): the same
        arithmetic, closed by the ] that matches its [. With no match it is
        the two bytes it looks like.
*/
static string_address expand_simple(string_address step, bool quoted);

static COLD string_address expand_arithmetic_legacy(string_address step,
                                                    bool quoted)
{
        string_address inner = step + 2;
        string_address stop = expand_bracket_end_quoted(inner, '[', ']', false,
                                                       false);
        p8 text_local[EXPAND_LOCAL_TEXT];
        string_address text;

        if (!stop)
                return expand_simple(step, quoted);

        text = expand_hold(inner, (positive)(stop - inner), text_local,
                           sizeof(text_local));
        if (!text)
                return stop + 1;

        return expand_arithmetic_finish(arith_expand_body(text), stop - 1,
                                        quoted);
}

static HOT __attribute__((noinline)) string_address expand_arithmetic(
        string_address step, bool quoted)
{
        string_address inner = step + 3;
        string_address at = inner;
        p8 tiny[64];
        positive n = 0;

        // $((i + 1)) has no quote, nested paren or dollar. Copy the body
        // while looking for the first ), then demand the second; anything
        // the quote walker owns starts that walker from the front.
        while (n < 63)
        {
                p8 value = string_get(at);

                if (value == ')')
                {
                        if (string_get(at + 1) != ')')
                                break;

                        tiny[n] = 0;
                        return expand_arithmetic_finish(tiny, at, quoted);
                }

                if (!value)
                        break;

                if (value == '(' || value == '\'' || value == '"' ||
                    value == '\\' || value == '$' || value == '`')
                        return expand_arithmetic_complex(step, quoted);

                tiny[n++] = value;
                at++;
        }

        if (!string_get(at) ||
            (string_get(at) == ')' && string_get(at + 1) != ')'))
                return shell_dash_compat ? expand_arithmetic_missing(step)
                                         : expand_command(step, quoted);

        return expand_arithmetic_complex(step, quoted);
}

/*
        ${x#pat} and its three relatives, done in place on the value that was
        just pushed. The cut lengths are tried in the order the form asks for
        and the first that matches wins, which is all that shortest and longest
        mean.
*/
static fn expand_trim(positive start, string_address pattern, bool prefix, bool longest)
{
        positive length = expand_length - start;
        positive cut = 0;
        bool found = false;
        positive at;
        bool utf8 = false;

        // An empty value cannot be shortened, and the buffer this reads is
        // only made by the first push: ${nosuch#} as the first expansion of
        // the process has nothing behind it yet.
        if (!length)
                return;

        // ${x#?} and ${x%?} cut one character. An ASCII edge is one byte even
        // when the locale is UTF-8, so the matcher and the UTF-8 walker stay
        // off the path this loop is measured on.
        if (pattern[0] == '?' && !pattern[1])
        {
                p8 edge = prefix ? expand_text[start]
                                 : expand_text[start + length - 1];
                if (edge < 0x80 || !shell_utf8_on())
                        cut = 1;
                else
                        cut = prefix ? expand_character_width(expand_text + start,
                                                              length)
                                     : length - expand_character_previous(
                                                    expand_text + start, length);
                found = true;
        }
        else
                utf8 = shell_utf8_on() &&
                       !expand_bytes_ascii(expand_text + start, length);

        // The four forms a script writes most -- the basename and dirname
        // idioms with a slash, ${x%%.*} and ${x#*.} -- are one star beside
        // one plain byte, and each is answered by one hunt for that byte:
        // the longest prefix ending in it runs to its last occurrence and
        // the shortest to its first, and the suffixes are the mirror. The
        // loop below asks the matcher about every cut in turn, which costs
        // a pass over the value per byte of it and answers the same thing.
        if (pattern[0] && pattern[1] && !pattern[2])
        {
                p8 star = prefix ? pattern[0] : pattern[1];
                p8 plain = prefix ? pattern[1] : pattern[0];

                if (star == '*' && plain != '*' && plain != '?' &&
                    plain != '[' && plain != '\\')
                {
                        p8 address_to value = expand_text + start;
                        bool last = prefix == longest;
                        p8 address_to hit = (p8 address_to)(
                            last ? memory_last_of(value, (b8)plain, length)
                                 : memory_first_of(value, (b8)plain, length));

                        if (!hit)
                                return;

                        cut = prefix ? (positive)(hit - value) + 1
                                     : length - (positive)(hit - value);
                        found = true;
                }
        }

        for (at = 0; !found && at <= length;)
        {
                positive size = longest ? (length - at) : at;
                p8 held;
                bool hit;

                if (prefix)
                {
                        held = expand_text[start + size];
                        expand_text[start + size] = end;
                        hit = shell_match(pattern, expand_text + start);
                        expand_text[start + size] = held;
                }
                else
                {
                        held = expand_text[expand_length];
                        expand_text[expand_length] = end;
                        hit = shell_match(pattern, expand_text + expand_length - size);
                        expand_text[expand_length] = held;
                }

                if (hit)
                {
                        cut = size;
                        found = true;
                        break;
                }
                if (at == length)
                        break;
                at += !utf8 ? 1 : prefix != longest
                    ? expand_character_width(expand_text + start + at, length - at)
                    : length - at - expand_character_previous(expand_text + start,
                                                              length - at);
        }

        if (!found || !cut)
                return;

        if (prefix)
        {
                memory_copy(expand_text + start, expand_text + start + cut, length - cut);
                memory_copy(expand_mark + start, expand_mark + start + cut, length - cut);
        }

        expand_length -= cut;
}

//      The slash separating a Bash replacement pattern from its replacement.
//      A slash hidden by a quote, a backslash or a nested expansion belongs to
//      that word and is not the separator of the outer ${.../.../...}.
static PURE string_address expand_replace_separator(string_address at)
{
        while (string_get(at))
        {
                if (string_is(at, '/'))
                        return at;

                if (lex_skip_held(address_of at))
                        continue;

                at++;
        }

        return null;
}

// How many bytes an ASCII value gives to a pattern that has no star and no
// group in it: each byte, ?, \x and [set] matches exactly one, so the pattern
// can match nowhere but at that size. Zero when the pattern is anything else.
static PURE positive expand_pattern_width(string_address pattern)
{
        positive width = 0;

        for (string_address at = pattern; string_get(at); width++)
        {
                string_address close;

                if (string_is(at, '*') || string_is(at, '('))
                        return 0;
                if (string_is(at, '[') && (close = expand_set_end(at)))
                        at = close + 1;
                else if (string_is(at, '\\') && string_get(at + 1))
                        at += 2;
                else
                        at++;
        }

        return width;
}

//      Whether pattern matches exactly size bytes at source + at. The source
//      is our private copy, so terminating one candidate in place avoids a
//      fresh allocation for every possible match.
static bool expand_replace_match(p8 address_to source, positive at,
                                 positive size, string_address pattern, bool fold)
{
        p8 held = source[at + size];
        bool matched;

        source[at + size] = end;
        matched = shell_match_folded(pattern, source + at, fold);
        source[at + size] = held;

        return matched;
}

// Bash's patsub_replacement option is on by default: an unquoted ampersand in
// the replacement is the bytes that matched, while a quoted one is literal.
// expand_capture records the quoted one as \& so that distinction survives
// the nested expansion buffer.
static fn expand_replace_push(string_address replacement,
                              string_address matched, positive match_length,
                              p8 mark)
{
        string_address at = replacement;
        string_address run = at;

        while (string_get(at))
        {
                if (string_is(at, '\\') && string_is(at + 1, '&'))
                {
                        expand_push_run(run, (positive)(at - run), mark);
                        expand_push('&', mark);
                        at += 2;
                        run = at;
                        continue;
                }

                if (string_is(at, '&') &&
                    shell_shopt_on(PATSUB_REPLACEMENT))
                {
                        expand_push_run(run, (positive)(at - run), mark);
                        expand_push_run(matched, match_length, mark);
                        at++;
                        run = at;
                        continue;
                }

                at++;
        }

        expand_push_run(run, (positive)(at - run), mark);
}

// The replacement of a literal pattern: a compare at the anchored end, or
// the leftmost occurrence and, doubled, every one after it. A literal is
// never empty here, so every match moves forward.
static fn expand_replace_literal(p8 address_to source, positive length,
                                 string_address pattern, positive need,
                                 string_address replacement, p8 anchor,
                                 bool global, p8 mark, bool fold)
{
        positive copied = 0;

        if (anchor)
        {
                positive where = anchor == '#' ? 0 : length - need;

                if (need <= length && !(fold
                        ? memory_compare_ascii_case(source + where, pattern, need)
                        : memory_compare(source + where, pattern, need)))
                {
                        expand_push_run(source, where, mark);
                        expand_replace_push(replacement, source + where, need,
                                            mark);
                        copied = where + need;
                }

                expand_push_run(source + copied, length - copied, mark);
                return;
        }

        positive2 anchors = memory_search_prepare(pattern, need, fold);

        while (copied < length)
        {
                p8 address_to hit = (p8 address_to)(fold
                    ? memory_search_ascii_case_prepared(source + copied,
                        length - copied, pattern, need, anchors.x, anchors.y)
                    : memory_search_prepared(source + copied,
                        length - copied, pattern, need, anchors.x, anchors.y));

                if (!hit)
                        break;

                expand_push_run(source + copied, (positive)(hit - source) - copied,
                                mark);
                expand_replace_push(replacement, hit, need, mark);
                copied = (positive)(hit - source) + need;

                if (!global)
                        break;
        }

        expand_push_run(source + copied, length - copied, mark);
}

/*
        Bash ${name/pattern/replacement}.

        The first match is the leftmost one and the match at that position is
        the longest one accepted by the glob. A doubled slash repeats that
        search over the remainder. # and % immediately after the operator
        anchor the match to the beginning or end respectively.
*/
static fn expand_replace_value(p8 address_to source, positive length,
                               string_address pattern,
                               string_address replacement, bool global, p8 mark);

static fn expand_replace(expand_reference reference, string_address pattern_text,
                         string_address replacement_text, bool quoted,
                         bool global, b32 parameter_mode)
{
        positive expansion_start = expand_length;
        positive length;
        p8 address_to source;
        string_address pattern;
        string_address replacement;
        p8 mark = quoted ? MARK_QUOTED : MARK_FIELD;

        // This also applies nounset and the special-parameter rules before
        // the value is lifted out of the shared expansion buffer.
        expand_push_parameter_as(reference, quoted, parameter_mode);

        if (expand_failed)
                return;

        length = expand_length - expansion_start;
        source = shell_store_copy(address_of expand_store,
                                  expand_text + expansion_start, length);

        if (!source)
        {
                expand_fail_state();
                expand_length = expansion_start;
                return;
        }

        expand_length = expansion_start;

        pattern = expand_capture(pattern_text, false, EXPAND_CAPTURE_PATTERN);
        replacement = expand_capture(replacement_text, false,
                                     EXPAND_CAPTURE_REPLACEMENT);

        if (expand_failed || !pattern || !replacement)
                return;

        expand_replace_value(source, length, pattern, replacement, global,
                             mark);
}

/* One value's replacement, pushed: the parameter's, or one positional
   parameter's when $@ and $* are replaced one parameter at a time. */
static fn expand_replace_value(p8 address_to source, positive length,
                               string_address pattern,
                               string_address replacement, bool global, p8 mark)
{
        positive at = 0;
        positive copied = 0;
        p8 anchor = 0;
        bool fold = shell_shopt_on(NOCASEMATCH);

        // // selects global replacement, so its leading #/% is a literal
        // pattern member. Only the single-slash operator admits anchors.
        if (!global && (string_is(pattern, '#') || string_is(pattern, '%')))
                anchor = string_get(pattern++);

        // An empty pattern does not designate a match in Bash parameter
        // replacement; the value passes through unchanged. Anchored, it is
        // the empty string at that end and the replacement lands there.
        if (!string_get(pattern))
        {
                if (anchor == '#')
                        expand_replace_push(replacement, source, 0, mark);

                expand_push_run(source, length, mark);

                if (anchor == '%')
                        expand_replace_push(replacement, source + length, 0,
                                            mark);

                return;
        }

        // Bash replaces an empty value for a run of bare stars, but does not
        // search the terminal empty position for nullable extended groups.
        // This common all-value form also needs no candidate-cut loop.
        if (pattern[0] == '*' &&
            !pattern[string_span_of_set(pattern, "*")])
        {
                expand_replace_push(replacement, source, length, mark);
                return;
        }

        /*
                A pattern with nothing magic in it is its own bytes, and the
                bulk search finds them where the loop below would: it asks
                the matcher about every cut of every position, which a
                literal never needs, and ${x//,/ } over a long value paid for
                that at every comma.
        */
        {
                //      A parenthesis is in here for the head of an
                //      extended group. With the option off it is an ordinary
                //      byte and the slower path finds the same bytes; with it
                //      on, a literal search would find nothing at all.
                positive plain = string_span_without_set(pattern, "*?[\\(");

                if (!pattern[plain])
                {
                        expand_replace_literal(source, length, pattern, plain,
                                               replacement, anchor, global,
                                               mark, fold);
                        return;
                }
        }

        /*
                Whether the pattern can match anywhere, asked once of the
                whole value as bash's match_upattern asks it -- "*pat*", or
                "pat*" and "*pat" under an anchor -- and "pat*" at each
                start before its every size is tried. Without them the loop
                below asked the matcher about every cut of every position to
                conclude there was nothing: ${x/*b/} over 50,000 bytes of a
                value a script was handed ran past 30 s where bash takes
                2 ms. A pattern ending in a lone backslash would have the
                star read as a literal one, so it goes the long way.
        */
        bool utf8 = shell_utf8_on();
        positive fixed = !utf8 || expand_bytes_ascii(source, length)
            ? expand_pattern_width(pattern) : 0;
        positive pattern_length = string_length(pattern);
        positive slashes = 0;

        while (slashes < pattern_length &&
               pattern[pattern_length - 1 - slashes] == '\\')
                slashes++;

        string_address starred = null;

        if (!fixed && !(slashes & 1))
        {
                p8 address_to built = shell_store_take(address_of expand_store,
                                                       pattern_length + 3);

                if (!built)
                {
                        expand_fail_state();
                        return;
                }
                built[0] = '*';
                memory_copy(built + 1, pattern, pattern_length);
                built[pattern_length + 1] = anchor == '%' ? end : '*';
                built[pattern_length + 2] = end;
                starred = built;

                if (!expand_replace_match(source, 0, length,
                                          anchor == '#' ? starred + 1 : starred,
                                          fold))
                {
                        expand_push_run(source, length, mark);
                        return;
                }
        }

        // Bash's multibyte suffix search includes the terminal empty span;
        // its byte/ASCII search does not. Keep that observable distinction
        // without imposing a scan on unanchored or literal replacements.
        positive starts = length + (anchor == '%' && utf8 &&
            memory_utf8_span(source, length, positive_max).y < length);
        while (at < starts)
        {
                positive begin = at;
                positive size = 0;
                bool found = false;

                if (anchor == '#')
                        begin = 0;

                if (fixed)
                {
                        // One size to ask about at each start, and no start
                        // that leaves less than that.
                        size = fixed;
                        if (anchor == '%')
                                begin = length >= fixed ? length - fixed : length + 1;
                        for (; begin + fixed <= length; begin++)
                                if (expand_replace_match(source, begin, fixed,
                                                         pattern, fold))
                                {
                                        found = true;
                                        break;
                                }
                                else if (anchor)
                                        break;
                }

                for (; !fixed && begin < starts;)
                {
                        positive largest = length - begin;
                        bool may = anchor == '%' || !starred ||
                                   expand_replace_match(source, begin, largest,
                                                        starred + 1, fold);

                        for (size = largest; may;)
                        {
                                if (expand_replace_match(source, begin, size,
                                                         pattern, fold))
                                {
                                        found = true;
                                        break;
                                }
                                // A suffix has only one possible end. Other
                                // candidates shrink by whole characters, not
                                // truncated UTF-8 prefixes that could match a
                                // negated set as malformed bytes.
                                if (!size || anchor == '%')
                                        break;
                                size = utf8
                                    ? expand_character_previous(source + begin, size)
                                    : size - 1;
                        }

                        if (found || anchor == '#' || begin == length)
                                break;
                        begin += utf8
                            ? expand_character_width(source + begin, length - begin)
                            : 1;
                }

                if (!found)
                        break;

                expand_push_run(source + copied, begin - copied, mark);
                expand_replace_push(replacement, source + begin, size, mark);
                copied = begin + size;

                // The end of the value ends the search as well: a pattern
                // that can also match nothing would otherwise match it there
                // again on every turn, and the value would never be finished.
                if (!global || anchor || copied >= length)
                        break;

                // A zero-width match must still advance through the source;
                // otherwise a global replacement never reaches its end.
                if (!size && copied < length)
                {
                        positive width = utf8
                            ? expand_character_width(source + copied, length - copied)
                            : 1;
                        expand_push_run(source + copied, width, mark);
                        copied += width;
                }

                at = copied;
        }

        expand_push_run(source + copied, length - copied, mark);
}

// The length separator in ${name:offset:length}. A colon paired with a
// top-level arithmetic ?: belongs to the offset; parentheses and nested
// expansions keep all of their colons inside too.
static PURE string_address expand_substring_separator(string_address at)
{
        positive parentheses = 0;
        positive choices = 0;

        while (string_get(at))
        {
                p8 value = string_get(at);

                if (lex_skip_held(address_of at))
                        continue;

                if (value == '(')
                        parentheses++;
                else if (value == ')' && parentheses)
                        parentheses--;
                else if (!parentheses && value == '?')
                        choices++;
                else if (!parentheses && value == ':' && choices)
                        choices--;
                else if (!parentheses && value == ':')
                        return at;

                at++;
        }

        return null;
}

// Bash's substring arithmetic errors abandon the current input unit, not the
// remaining script. Invalid ${!name} is the same unwind, including under
// --posix. Native and dash errors retain their fatal policy.
static COLD fn expand_slice_error()
{
        if (expand_errors_soft)
        {
                expand_failed = true;
                return;
        }
        if (!shell_bash_compat)
        {
                expand_fatal_status(2);
                return;
        }

        shell_status = 1;
        expand_failed = true;
        exec_expand_input_error();
}

/*
        ${!name} whose value is not a parameter. Bash fails that command
        and reads on, including under --posix; eval catches it. ${name?}
        in a command word still ends the process.
*/
static COLD fn expand_indirect_error()
{
        //      An interactive bash under -e leaves on it as on any other
        //      expansion error but an arithmetic one.
        if (shell_bash_compat &&
            !(shell_is_interactive && (shell_options & ((positive)1 << ('e' - 'a')))))
                expand_slice_error();
        else
                expand_fatal_status(1);
}

static bool expand_slice_number(string_address text, bipolar address_to value)
{
        string_address ready = expand_capture(arith_subscripts_held(text), true,
                                              EXPAND_CAPTURE_TEXT);

        if (expand_failed)
                return false;

        if (!string_get(arith_skip_space(ready)))
        {
                address_to value = 0;
                return true;
        }

        address_to value = arith_evaluate(ready);

        if (arith_bad)
        {
                if (!arith_unset)
                        shell_arith_report(writer_stderr_once, null, ready);
                expand_slice_error();
                return false;
        }

        return true;
}

// Offsets are indices; lengths are counts. Scalar, positional and indexed
// array slices share the arithmetic and short-circuit rules, but supply their
// own index limit and negative-offset origin. Do not evaluate a length when
// the offset is outside the sequence, including its side effects or errors.
#define SLICE_STRING 0
#define SLICE_POSITIONAL 1
#define SLICE_ARRAY 2
static bool expand_slice_bounds(expand_reference reference, string_address expression,
                                positive origin, positive limit, p8 kind,
                                positive address_to begin,
                                positive address_to count)
{
        string_address separator = expand_substring_separator(expression);
        string_address length_text = null;
        bipolar offset = 0;
        bipolar wanted = 0;

        if (separator)
        {
                separator[0] = end;
                length_text = separator + 1;
        }

        if (string_get(expression))
        {
                if (!expand_slice_number(expression, address_of offset))
                        return false;
        }
        else if (!separator)
        {
                expand_slice_refused(reference);
                return false;
        }

        // An offset expression may insert an element. For arrays, origin
        // arrives as the name length so the largest live index can be read
        // after that arithmetic, without an extra pre-evaluation lookup.
        if (kind == SLICE_ARRAY)
        {
                limit = shell_array_highest(reference.name, origin);
                origin = limit + 1;
        }

        if (offset < 0)
        {
                positive back = (positive)(-(offset + 1)) + 1;
                if (back > origin)
                        return false;
                address_to begin = origin - back;
        }
        else
                address_to begin = (positive)offset;

        if (address_to begin > limit)
                return false;

        address_to count = kind ? positive_max : origin - address_to begin;

        if (!separator)
                return true;

        if (!expand_slice_number(length_text, address_of wanted))
                return false;

        if (wanted < 0)
        {
                positive back = (positive)(-(wanted + 1)) + 1;

                if (kind || back > origin - address_to begin)
                {
                        p8 shown[32];

                        expand_where();
                        bipolar_into_string(shown, wanted);
                        string_format(writer_stderr_once,
                                      "%s: substring expression < 0\n", shown);
                        expand_slice_error();
                        return false;
                }

                address_to count = origin - address_to begin - back;
        }
        else if ((positive)wanted < address_to count)
                address_to count = (positive)wanted;

        return true;
}

static COLD fn expand_positional_slice(string_address name,
                                      string_address expression, bool quoted)
{
        positive begin;
        positive count;
        p8 form = string_get(name);
        p8 mark = quoted ? MARK_QUOTED : MARK_FIELD;
        p8 between = string_get(expand_ifs());
        bool fields = quoted ? form == '@'
                             : expand_list_blanks(form, between);
        positive origin = shell_parameter_count + 1;

        if (!expand_slice_bounds((expand_reference){.name = name}, expression, origin, origin, SLICE_POSITIONAL,
                                 address_of begin, address_of count))
        {
                if (form == '@')
                        expand_name_at_empty = true;
                return;
        }

        if (count > origin - begin)
                count = origin - begin;

        if (!count && form == '@')
                expand_name_at_empty = true;

        for (positive at = 0; at < count; at++)
        {
                positive index = begin + at;
                if (at)
                        expand_sequence_between(fields, between, mark);

                expand_push_string(index ? shell_parameter[index - 1]
                                         : shell_script_name, mark);
        }
}

static fn expand_substring(expand_reference reference, string_address expression,
                           bool quoted, b32 parameter_mode)
{
        string_address name = reference.name;
        positive expansion_start = expand_length;
        positive length;
        positive begin;
        positive count;

        if ((string_is(name, '@') || string_is(name, '*')) && !string_get(name + 1))
        {
                if (!shell_bash_compat)
                {
                        expand_where();
                        string_format(writer_stderr_once, "%s: bad substitution\n", name);
                        expand_fatal_status((shell_bash_compat || (parameter_mode & EXPAND_PARAMETER_INDIRECT)) ? 1 : 2);
                        return;
                }
                expand_positional_slice(name, expression, quoted);
                return;
        }

        /* ${name:} is a bad substitution even when the name is unset: lima
           bash 5.2.32 still names `${x:}`, and an unset skip used to swallow
           that and expand to nothing. */
        if (!string_get(expression) && !expand_substring_separator(expression))
        {
                expand_slice_refused(reference);
                return;
        }

        // Capture the value before evaluating arithmetic that may reassign
        // it. The expansion buffer already owns the bytes and marks; no new
        // snapshot or copy is needed. An unset value skips the arithmetic.
        if (!expand_push_parameter_as(reference, quoted, parameter_mode) || expand_failed)
                return;

        length = expand_length - expansion_start;
        bool utf8 = shell_utf8_on();
        positive characters = utf8
            ? memory_utf8_span(expand_text + expansion_start, length, positive_max).y
            : length;
        if (!expand_slice_bounds(reference, expression, characters, characters, SLICE_STRING,
                                 address_of begin, address_of count))
        {
                expand_length = expansion_start;
                return;
        }

        if (utf8)
        {
                begin = memory_utf8_span(expand_text + expansion_start,
                                         length, begin).x;
                count = memory_utf8_span(expand_text + expansion_start + begin,
                                         length - begin, count).x;
        }

        memory_copy(expand_text + expansion_start,
                    expand_text + expansion_start + begin, count);
        memory_copy(expand_mark + expansion_start,
                    expand_mark + expansion_start + begin, count);
        expand_length = expansion_start + count;
}

/* ${x~} and ${x~~}: each matched character goes to the other case. */
static bool expand_case_toggle;

static fn expand_case_span(positive start, bool upper, bool every,
                           string_address pattern, bool default_pattern)
{
        positive length = expand_length - start;
        p8 one[5];

        if (!length)
                return;

        // C locale is bytes. A UTF-8 locale whose value is still ASCII uses
        // the same path: the architecture loop does not touch the marks, and
        // ASCII case is the Unicode case.
        if (!shell_utf8_on() || expand_bytes_ascii(expand_text + start, length))
        {
                positive count = every ? length : 1;

                if (default_pattern && every && count >= 32 &&
                    !expand_case_toggle)
                {
                        if (upper)
                                memory_to_upper_ascii(expand_text + start, count);
                        else
                                memory_to_lower_ascii(expand_text + start, count);
                        return;
                }

                one[1] = end;
                for (positive at = 0; at < count; at++)
                {
                        p8 value = expand_text[start + at];

                        one[0] = value;

                        if (!shell_match(pattern, one))
                                continue;

                        if (expand_case_toggle)
                                upper = byte_to_upper(value) != value;
                        expand_text[start + at] =
                            upper ? byte_to_upper(value) : byte_to_lower(value);
                }
                return;
        }

        // One Unicode scalar at a time. ${x^} converts the first character,
        // not the first byte: é is É. A mapped spelling can change width, so
        // the parallel mark array moves with the text.
        for (positive at = start; at < expand_length; )
        {
                string_address past;
                p32 scalar = expand_set_character(expand_text + at,
                                                  expand_text + expand_length,
                                                  true, address_of past);
                positive width = (positive)(past - (expand_text + at));
                bool matched = default_pattern;

                // A scalar is at most four bytes: one holds it and its end.
                if (!width || width >= sizeof(one))
                        break;

                if (!matched)
                {
                        memory_copy(one, expand_text + at, width);
                        one[width] = end;
                        matched = shell_match(pattern, one);
                }

                if (matched && scalar < 0x110000)
                {
                        p32 mapped;

                        if (expand_case_toggle)
                                upper = unicode_case(scalar, UNICODE_CASE_UPPER) !=
                                        scalar;
                        mapped = unicode_case(scalar, upper ? UNICODE_CASE_UPPER : UNICODE_CASE_LOWER);

                        if (mapped != scalar)
                        {
                                p8 encoded[4];
                                positive made = memory_utf8_encode(encoded, 4,
                                                                   mapped);

                                if (made)
                                {
                                        p8 mark = expand_mark[at];

                                        if (made != width)
                                        {
                                                if (made > width)
                                                {
                                                        positive extra = made - width;

                                                        if (!expand_room(expand_length + extra + 2))
                                                        {
                                                                expand_fail_state();
                                                                return;
                                                        }
                                                }

                                                positive tail = expand_length - at - width;

                                                if (tail)
                                                {
                                                        memory_copy(expand_text + at + made,
                                                                    expand_text + at + width,
                                                                    tail);
                                                        memory_copy(expand_mark + at + made,
                                                                    expand_mark + at + width,
                                                                    tail);
                                                }

                                                expand_length = expand_length + made - width;
                                        }

                                        memory_copy(expand_text + at, encoded, made);
                                        if (made != width)
                                                memory_fill(expand_mark + at, mark, made);
                                        width = made;
                                }
                        }
                }

                at += width;
                if (!every)
                        break;
        }
}

// declare -u / -l store the folded bytes. The expansion buffer is live during
// assignment, so this walks a private copy and only overwrites a character
// whose UTF-8 width did not change.
static fn expand_case_buffer(p8 address_to text, positive length, bool upper)
{
        if (!length)
                return;

        if (!shell_utf8_on() || expand_bytes_ascii(text, length))
        {
                if (upper)
                        memory_to_upper_ascii(text, length);
                else
                        memory_to_lower_ascii(text, length);
                return;
        }

        for (positive at = 0; at < length; )
        {
                string_address past;
                p32 scalar = expand_set_character(text + at, text + length, true,
                                                  address_of past);
                positive width = (positive)(past - (text + at));

                if (!width)
                        break;

                if (scalar < 0x110000)
                {
                        p32 mapped = unicode_case(scalar, upper ? UNICODE_CASE_UPPER : UNICODE_CASE_LOWER);

                        if (mapped != scalar)
                        {
                                p8 encoded[4];
                                positive made = memory_utf8_encode(encoded, 4,
                                                                   mapped);

                                if (made == width)
                                        memory_copy(text + at, encoded, made);
                        }
                }

                at += width;
        }
}

static fn expand_case_change(expand_reference reference, string_address pattern_text,
                             bool quoted, bool upper, bool every,
                             b32 parameter_mode)
{
        positive start = expand_length;
        bool default_pattern = !string_get(pattern_text);
        string_address pattern;

        expand_push_parameter_as(reference, quoted, parameter_mode);

        if (expand_failed)
                return;

        pattern = string_get(pattern_text)
                      ? expand_capture(pattern_text, false,
                                       EXPAND_CAPTURE_PATTERN)
                      : (string_address) "?";

        if (expand_failed || !pattern)
                return;

        expand_case_span(start, upper, every, pattern, default_pattern);
}

/*
        ${name@X}: the value put through one named transformation.

        Q gives back bytes the shell would read as this same value, E reads
        the backslash escapes in it as $'...' would, and U u and L are the
        three case changes. What they have in common is that the value is
        lifted out of the expansion buffer first: two of them change its
        length, and every one of them is easier to write against a string
        than against the buffer it will be written back into.
*/

// An immutable scanner set for the double-quote writer, for the form
// escape_categories has no category for: $, backquote and ".
static const b8 shell_quote_double[STRING_SET_BYTES] = {
        [32 ... 33] = 1, [35] = 1, [37 ... 91] = 1,
        [93 ... 95] = 1, [97 ... 126] = 1
};

/*
        How many bytes of a character bash shows as itself start here: a
        whole UTF-8 character under a UTF-8 locale, or none -- a byte that
        begins nothing valid, and every high byte in any other locale, is
        spelled as an octal escape.
*/
static positive shell_shown_character(const p8 address_to value, positive left)
{
        memory_utf8_state state = {0};
        positive used = 0;
        b32 fed = 0;

        if (!shell_utf8_on())
                return 0;
        while (used < left &&
               !(fed = memory_utf8_feed(address_of state, value[used])))
                used++;
        return used < left && fed == 1 ? used + 1 : 0;
}

// Whether a value holds a high byte bash will not show as itself.
static bool shell_bytes_unshown(const p8 address_to value, positive length)
{
        for (positive at = 0; at < length; at++)
                if (value[at] >= 0x80)
                {
                        positive shown = shell_shown_character(value + at,
                                                               length - at);

                        if (!shown)
                                return true;
                        at += shown - 1;
                }
        return false;
}

//      Whether bash writes a value in $'...': a control byte, or a high
//      byte that is no character of the locale.
static inline INLINE bool shell_bytes_awkward(string_address value,
                                              positive length)
{
        return memory_escape_index(value, length, HEX_CONTROL | HEX_TAB) <
                   length ||
               shell_bytes_unshown((const p8 address_to)value, length);
}

/* Both spellings through a writer: the transforms push through them, and so do
   xtrace and quoted history words. Which one a word needs is the caller's. */
static COLD fn shell_single_quote_write(writer write, string_address value,
                                        positive length)
{
        positive at = 0;

        write("'", 1);
        while (at < length)
        {
                positive run = memory_span_without_byte(value + at, '\'',
                                                        length - at);

                if (run)
                        write(value + at, run);
                at += run;
                if (at < length)
                {
                        write("'\\''", 4);
                        at++;
                }
        }
        write("'", 1);
}

static COLD fn shell_ansi_run(writer write, string_address value,
                              positive length, bool high)
{
        write("$'", 2);
        writer_spelled(write, value, length, spelling_ansi(high));
        write("'", 1);
}

static p8 transform_write_mark;

static fn transform_write(address_any data, positive length)
{
        if (!length && data)
                length = string_length(data);
        if (length)
                expand_push_run(data, length, transform_write_mark);
}

// The writers of builtin.c that the transforms below push through.
static fn shell_ansi_quoted_shown(writer write, string_address text,
                                  positive length);
static fn shell_declare_quoted_span(writer write, string_address value,
                                    positive length);

/*
        The value as bytes the shell would read back as itself.

        A single-quoted run holds anything but a quote, so that is the answer
        wherever it can be: 'a b' reads back as a b. A value holding a
        control byte cannot be written that way at all, so it goes out as
        $'...' with the escapes -- which is the only form that survives being
        pasted back into a script.
*/
static fn transform_quoted(string_address value, positive length, p8 mark)
{
        // In the C locale a high byte is not a character, so @Q has to spell
        // it in $'...' the way bash 5.2 does. UTF-8 may keep it inside quotes.
        bool high = !shell_utf8_on();

        transform_write_mark = mark;
        if (memory_escape_index(value, length,
                                HEX_CONTROL | HEX_TAB | (high ? HEX_HIGH : 0)) == length &&
            !(shell_bash_compat &&
              shell_bytes_unshown((const p8 address_to)value, length)))
                shell_single_quote_write(transform_write, value, length);
        else if (shell_bash_compat)
                shell_ansi_quoted_shown(transform_write, value, length);
        else
                shell_ansi_run(transform_write, value, length, high);
}

static const b8 expand_ansi_plain[STRING_SET_BYTES] = {
        [1 ... 38] = 1, [40 ... 91] = 1, [93 ... 255] = 1
};

/* $'...' and ${value@E} interpret the same escapes. Only source has a closing
   quote and doubles a backslash used as a control operand. A decoded NUL
   discards the rest of this value, never the following word's suffix. */
/*
        A code point, as the bytes this locale spells it in.

        Bash writes the escape back out when it cannot spell one -- uppercase,
        and padded to the width the letter asked for -- rather than dropping
        it or writing a replacement, so \u00e9 in the C locale stays \u00E9
        and can still be read by whatever does know the encoding.
*/
#define CODE_POINT_MAX_BYTES 10
static positive shell_code_point_bytes(p8 letter, positive wide, positive code,
                                       p8 address_to out)
{
        //      Bash spells a surrogate as it would any other code point, and
        //      the library encoder refuses one: its neighbour a block down has
        //      the same tail and a lead byte one less.
        bool surrogate = code >= 0xd800 && code <= 0xdfff;
        positive used = code < 0x80 || shell_utf8_on()
                            ? memory_utf8_encode(out, CODE_POINT_MAX_BYTES,
                                                 code - (surrogate ? 0x1000 : 0))
                            : 0;
        positive at;

        if (used)
        {
                out[0] = (p8)(out[0] + surrogate);
                return used;
        }

        out[used++] = '\\';
        out[used++] = letter;

        for (at = wide; at--;)
        {
                p8 digit = (p8)((code >> (at * 4)) & 15);

                out[used++] = (p8)(digit < 10 ? '0' + digit : 'A' + digit - 10);
        }

        return used;
}

static fn expand_push_code_point(p8 letter, positive wide, positive code,
                                 p8 mark)
{
        p8 bytes[CODE_POINT_MAX_BYTES];
        positive used = shell_code_point_bytes(letter, wide, code, bytes);
        positive at;

        for (at = 0; at < used; at++)
                expand_push(bytes[at], mark);
}

static string_address expand_ansi(string_address at, p8 mark, bool source)
{
        bool discard = false;

        while (string_get(at) && !(source && string_is(at, '\'')))
        {
                if (expand_ansi_plain[string_get(at)])
                {
                        positive run = string_span(at, expand_ansi_plain);
                        if (!discard)
                                expand_push_run(at, run, mark);
                        at += run;
                        continue;
                }
                p8 value = string_get(at++);
                if (value != '\\' || !string_get(at))
                {
                        if (!discard)
                                expand_push(value, mark);
                        continue;
                }
                value = string_get(at);

                if (value >= '0' && value <= '7')
                {
                        positive used;
                        value = (p8)string_digits_octal_escape_max(
                            at, 3, address_of used);
                        at += used;
                }
                /*
                        bash's \x{H...}: every hex digit up to the brace,
                        which is taken too when it is there, and the value's
                        low byte. No digits is a zero, which ends the string.
                */
                else if (value == 'x' && string_is(at + 1, '{') &&
                         shell_bash_compat)
                {
                        positive used;
                        positive number = string_digits_hexadecimal_escape_max(
                            at + 2, positive_max, address_of used);

                        at += used + 2;
                        if (string_is(at, '}'))
                                at++;
                        value = (p8)(number & 0xff);
                }
                //      dash reads \x as a code point of at most two digits: none
                //      is a NUL, which ends the string, and one past 0x7f is
                //      spelled in UTF-8 rather than left a single byte.
                else if (value == 'x' && shell_dash_compat)
                {
                        positive used;
                        positive number = string_digits_hexadecimal_escape_max(
                            at + 1, 2, address_of used);
                        at += used + 1;
                        if (!used)
                        {
                                discard = true;
                                continue;
                        }
                        if (number > 0x7f)
                        {
                                p8 bytes[4];
                                positive made = memory_utf8_encode(
                                    bytes, sizeof(bytes), number);

                                for (positive step = 0; step < made && !discard;
                                     step++)
                                        expand_push(bytes[step], mark);
                                continue;
                        }
                        value = (p8)number;
                }
                else if (value == 'x')
                {
                        positive used;
                        positive number = string_digits_hexadecimal_escape_max(
                            at + 1, 2, address_of used);
                        at += used + 1;
                        if (!used && !discard)
                                expand_push('\\', mark);
                        if (used)
                                value = (p8)number;
                }
                else if (value == 'u' || value == 'U')
                {
                        positive wide = value == 'u' ? 4 : 8;
                        positive used;
                        positive code = string_digits_hexadecimal_escape_max(
                            at + 1, wide, address_of used);

                        at += used + 1;

                        //      No digits at all: the two characters stand for
                        //      themselves, the way \x with no digits does; in
                        //      dash it is a NUL, and the string ends.
                        if (!used && shell_dash_compat)
                        {
                                discard = true;
                                continue;
                        }
                        if (!used)
                        {
                                if (!discard)
                                {
                                        expand_push('\\', mark);
                                        expand_push(value, mark);
                                }
                                continue;
                        }

                        if (!code)
                        {
                                discard = true;
                                continue;
                        }

                        //      dash spells a code point in UTF-8 whatever the
                        //      locale, and drops one that has no spelling.
                        if (shell_dash_compat)
                        {
                                p8 bytes[CODE_POINT_MAX_BYTES];
                                positive made = code > 0x10ffff
                                                    ? 0
                                                    : memory_utf8_encode(
                                                          bytes, sizeof(bytes),
                                                          code);

                                for (positive step = 0; step < made && !discard;
                                     step++)
                                        expand_push(bytes[step], mark);
                                continue;
                        }

                        if (!discard)
                                expand_push_code_point(value, wide, code, mark);

                        continue;
                }
                else if (value == 'c' && string_get(at + 1))
                {
                        value = string_get(at + 1);
                        if (source && value == '\\' && string_is(at + 2, '\\'))
                                at++;
                        //      dash folds the letter to upper case and flips
                        //      the bit that makes it a control: \c0 is p.
                        if (shell_dash_compat)
                        {
                                if (value >= 'a' && value <= 'z')
                                        value -= 32;
                                value ^= 0x40;
                        }
                        else
                                value = value == '?' ? 127 : value & 31;
                        at += 2;
                }
                else
                {
                        at++;
                        p8 escaped = byte_simple_escape(value);
                        if (value == 'e' || (value == 'E' && !shell_dash_compat))
                                value = 27;
                        else if (escaped)
                                value = escaped;
                        else if ((value != '?' || shell_dash_compat) &&
                                 value != '\\' &&
                                 value != '\'' && value != '"' && !discard)
                                expand_push('\\', mark);
                }

                if (!value)
                        discard = true;
                else if (!discard)
                        expand_push(value, mark);
        }
        return at;
}

/*
        The attribute letters a name carries, in the order Bash writes them.

        Not the order they may be given in: this is a listing and a listing
        has to read the same from both shells. An ordinary name carries none
        of them and answers with nothing at all.
*/
// Export is stored beside the attribute byte, so its listing bit is wider.
#define SHELL_ATTRIBUTE_EXPORTED 256
static const p16 shell_attribute_bits[256] = {
    ['a'] = SHELL_ARRAY_INDEXED, ['A'] = SHELL_ARRAY_ASSOCIATIVE,
    ['i'] = SHELL_ARRAY_INTEGER, ['n'] = SHELL_ARRAY_NAMEREF,
    ['r'] = SHELL_ARRAY_READONLY, ['x'] = SHELL_ATTRIBUTE_EXPORTED,
    ['l'] = SHELL_ARRAY_LOWER, ['u'] = SHELL_ARRAY_UPPER,
    // Lower and upper at once cannot be asked for, so both bits stand for
    // -c, which folds to lower and then raises the first letter.
    ['c'] = SHELL_ARRAY_LOWER | SHELL_ARRAY_UPPER
};

static COLD positive shell_attribute_letters(p8 address_to into, p8 attributes,
                                             bool readonly, bool exported)
{
        static const p8 letters[] = "aAcinrxlu";
        positive flags = (attributes & ~(SHELL_ARRAY_ASSIGNED | SHELL_ARRAY_READONLY)) |
                         (readonly ? SHELL_ARRAY_READONLY : 0) |
                         (exported ? SHELL_ATTRIBUTE_EXPORTED : 0);
        positive count = 0;
        bool capital = (attributes & (SHELL_ARRAY_LOWER | SHELL_ARRAY_UPPER)) ==
                       (SHELL_ARRAY_LOWER | SHELL_ARRAY_UPPER);

        for (positive at = 0; at < sizeof(letters) - 1; at++)
        {
                //      Both bits mean capitalize and are written as c alone.
                if (capital ? (letters[at] == 'l' || letters[at] == 'u')
                            : letters[at] == 'c')
                        continue;
                if (flags & shell_attribute_bits[letters[at]])
                        into[count++] = letters[at];
        }
        return count;
}

static COLD fn transform_attributes(expand_reference reference, p8 mark)
{
        string_address name = reference.name;
        positive length = reference.key ? reference.name_length : string_length(name);
        const_string base = name;
        bool element = !shell_reference_resolve(name, length, address_of base, address_of length);
        if (element && !shell_reference_element(name, string_length(name),
                address_of base, address_of length, null, null))
                return;
        p8 attributes = shell_variable_attributes(base, length);
        bool exported = shell_variable_exported(base, length);
        p8 letters[8];
        if (element || reference.key)
        {
                p8 scratch[32];
                bool present;
                expand_value_of(reference, scratch, address_of present, null);
                if (element && !present)
                        return;
        }
        positive count = shell_attribute_letters(letters, attributes,
            (attributes & SHELL_ARRAY_READONLY) != 0, exported);
        if (count)
                expand_push_run(letters, count, mark);
}

// declare -p / @A compound values: double quotes, or $'...' with high bytes
// escaped. Distinct from @Q, which prefers a single-quoted reusable form.
static COLD fn transform_declare_quoted(string_address value, positive length,
                                        p8 mark)
{
        transform_write_mark = mark;
        shell_declare_quoted_span(transform_write, value, length);
}

static COLD fn transform_declare_key(string_address key, positive length,
                                     p8 mark)
{
        if (string_span_max(key, length, string_set_name) != length)
                transform_declare_quoted(key, length, mark);
        else
                expand_push_run(key, length, mark);
}

static COLD fn transform_prompt(string_address value, positive length, p8 mark)
{
        p8 address_to held = shell_store_take(address_of expand_store,
                                              length + 1);

        if (!held)
        {
                expand_fail_state();
                return;
        }

        memory_copy(held, value, length);
        held[length] = end;
        transform_write_mark = mark;
        if (shell_bash_compat)
        {
                string_address made = shell_prompt_expand(held, true);

                if (!expand_failed)
                        transform_write(made, string_length(made));
                return;
        }
        shell_prompt_written(transform_write, held);
}

static COLD bool transform_name_readonly(const_string name, positive length,
                                         p8 attributes)
{
        p8 named[EXPAND_LOCAL_NAME];

        if (length >= EXPAND_LOCAL_NAME)
                return (attributes & SHELL_ARRAY_READONLY) != 0;

        memory_copy_end(named, name, length);
        return env_readonly(named) || (attributes & SHELL_ARRAY_READONLY);
}

/*
        ${v@A} as bash 5.2 writes it: a reusable assignment. A name with no
        flags is `v='...'`; export, integer, readonly and array flags become
        `declare -x v='...'` and the rest. Associative names without a
        subscript have no scalar value, so they stop at `declare -A v`.
*/
static COLD fn transform_assign(expand_reference reference, p8 mark)
{
        string_address name = reference.name;
        positive length = reference.name_length
                              ? reference.name_length
                              : string_length(name);
        const_string base = name;
        positive base_length = length;
        p8 attributes;
        bool exported;
        bool readonly;
        p8 letters[8];
        positive flags;
        p8 scratch[32];
        bool present = true;
        positive value_length = 0;
        string_address value = null;

        if (!expand_assignable_name(name))
                return;

        shell_reference_resolve(name, length, address_of base,
                                address_of base_length);
        attributes = reference.key
                         ? shell_array_attributes(base, base_length)
                         : shell_variable_attributes(base, base_length);
        if (reference.key && !(attributes & SHELL_ARRAY_EITHER))
                attributes = shell_variable_attributes(base, base_length);
        exported = shell_variable_exported(base, base_length);
        readonly = transform_name_readonly(base, base_length, attributes);
        flags = shell_attribute_letters(letters, attributes, readonly, exported);

        if (!(attributes & SHELL_ARRAY_ASSOCIATIVE) || reference.key)
        {
                value = expand_value_of(reference, scratch, address_of present,
                                        address_of value_length);
                if (!present)
                        value = null;
        }

        if (!flags && !value)
        {
                if (shell_options & ((positive)1 << ('u' - 'a')))
                        expand_unbound(reference, false);
                return;
        }

        if (flags)
        {
                expand_push_run((string_address) "declare -", 9, mark);
                expand_push_run(letters, flags, mark);
                expand_push(' ', mark);
        }

        expand_push_run(base, base_length, mark);

        if (value)
        {
                expand_push('=', mark);
                transform_quoted(value, value_length, mark);
        }
}

static COLD fn expand_value_transform(p8 which, string_address value,
                                      positive length, bool present, p8 mark)
{
        positive start;

        if (which == 'Q' || which == 'K' || which == 'k')
        {
                if (present)
                        transform_quoted(value, length, mark);
                return;
        }

        if (which == 'P')
        {
                if (present)
                        transform_prompt(value, length, mark);
                return;
        }

        if (which == 'a' || which == 'A')
                return;

        if (which == 'E')
        {
                p8 address_to held;

                if (!present)
                        return;

                held = shell_store_take(address_of expand_store, length + 1);
                if (!held)
                {
                        expand_fail_state();
                        return;
                }

                memory_copy(held, value, length);
                held[length] = end;
                expand_ansi(held, mark, false);
                return;
        }

        start = expand_length;
        expand_push_run(value, length, mark);
        if (which == 'U' || which == 'L' || which == 'u')
                expand_case_span(start, which != 'L', which != 'u',
                                 (string_address) "?", true);
}

/*
        $* and $@ run the letter on each parameter, then join the way those
        names already join: quoted @ keeps field breaks, * uses the first
        IFS byte. @A is the one exception -- it is `set --` plus the quoted
        parameters as one string, and quoted @ still splits that string on
        IFS so `declare`/`set` flags become their own words.
*/
static COLD fn expand_positional_transform(p8 which, p8 form, bool quoted)
{
        p8 mark = quoted ? MARK_QUOTED : MARK_FIELD;
        p8 between = string_get(expand_ifs());
        bool fields = quoted ? form == '@'
                             : expand_list_by_ifs(form, between);
        positive at;

        if (!shell_parameter_count)
        {
                if (form == '@')
                        expand_name_at_empty = true;
                return;
        }

        if (which == 'A')
        {
                // Quoted * keeps the assignment one field; quoted @ and
                // unquoted forms mark it as a field so IFS can take it apart.
                p8 lead = (quoted && form == '*') ? MARK_QUOTED : MARK_FIELD;

                expand_push_run((string_address) "set -- ", 7, lead);
                for (at = 0; at < shell_parameter_count; at++)
                {
                        if (at && between)
                                expand_push(between, lead);
                        transform_quoted(shell_parameter[at],
                                         string_length(shell_parameter[at]),
                                         lead);
                }
                return;
        }

        for (at = 0; at < shell_parameter_count && !expand_failed; at++)
        {
                if (at)
                        expand_sequence_between(fields, between, mark);
                if (which == 'a')
                        continue;
                expand_value_transform(which, shell_parameter[at],
                                       string_length(shell_parameter[at]), true,
                                       mark);
        }
}

static COLD fn expand_transform(expand_reference reference, string_address word,
                                bool quoted, b32 parameter_mode)
{
        positive start = expand_length;
        p8 which = string_get(word);
        p8 mark = quoted ? MARK_QUOTED : MARK_FIELD;
        p8 scratch[32];
        bool present = true;
        positive length;
        p8 address_to held;
        string_address name = reference.name;
        bool all = string_get(name + 1) == end &&
                   (string_is(name, '@') || string_is(name, '*'));

        // Attribute queries follow namerefs and evaluate explicit subscripts.
        if (which == 'a')
        {
                if (all)
                        expand_positional_transform(which, string_get(name),
                                                    quoted);
                else
                {
                        //      Under set -u a name with no value is unbound
                        //      to bash whatever attributes it carries.
                        if (shell_options & ((positive)1 << ('u' - 'a')))
                        {
                                expand_value_of(reference, scratch,
                                                address_of present, null);
                                if (!present)
                                {
                                        expand_unbound(reference, false);
                                        return;
                                }
                        }
                        transform_attributes(reference, mark);
                }
                return;
        }

        if (all)
        {
                expand_positional_transform(which, string_get(name), quoted);
                return;
        }

        if (which == 'A')
        {
                transform_assign(reference, mark);
                return;
        }

        //      Asked before the value is pushed, because a name that is unset
        //      and a name holding nothing push the same nothing and Q tells
        //      them apart: one is no bytes at all and the other is a pair of
        //      quotes with nothing between them.
        if (which == 'Q' || which == 'K' || which == 'k')
                expand_value_of(reference, scratch, address_of present, null);

        expand_push_parameter_as(reference, quoted, parameter_mode);

        if (expand_failed)
                return;

        length = expand_length - start;

        //      The three case changes need no copy: the bytes are already
        //      where they belong and only their case is wrong. UTF-8 walks
        //      characters so é becomes É and the first-character form stops
        //      after that scalar.
        if (which == 'U' || which == 'L' || which == 'u')
        {
                expand_case_span(start, which != 'L', which != 'u',
                                 (string_address) "?", true);
                return;
        }

        //      Q, K, k, E and P all answer with a different number of bytes
        //      than they were given, so the value comes out of the buffer
        //      before anything is written back into it.
        held = shell_store_copy(address_of expand_store,
                                expand_text + start, length);

        if (!held)
        {
                expand_fail_state();
                expand_length = start;
                return;
        }

        expand_length = start;

        //      Nothing is nothing: an unset name has no bytes to quote, and
        //      Bash writes none rather than a pair of empty quotes.
        if ((which == 'Q' || which == 'K' || which == 'k') && present)
                transform_quoted(held, length, mark);
        else if (which == 'E')
                expand_ansi(held, mark, false);
        else if (which == 'P')
                transform_prompt(held, length, mark);
}

/* Bash ${!prefix@} is a field list; ${!prefix*} is the same sorted names
   joined by the first IFS byte. The environment table supplies set names,
   and the expander owns the terminated copies needed by the shared sorter. */
static fn expand_push_names(string_address prefix, positive prefix_length,
                            p8 form, bool quoted)
{
        positive count = env_names_prefix(prefix, prefix_length, null, 0);
        string_address address_to names;
        p8 mark = quoted ? MARK_QUOTED : MARK_FIELD;
        p8 between;

        if (!count)
        {
                if (form == '@')
                        expand_name_at_empty = true;
                return;
        }

        between = form == '@' ? ' ' : string_get(expand_ifs());

        if (count > positive_max / sizeof(names[0]) ||
            !(names = (string_address address_to)shell_store_take_aligned(
                  address_of expand_store, count * sizeof(names[0]))) ||
            env_names_prefix(prefix, prefix_length, names, count) != count)
        {
                expand_fail_state();
                return;
        }

        for (positive at = 0; at < count; at++)
        {
                string_address equals = string_first_of(names[at], '=');
                positive length = equals ? (positive)(equals - names[at])
                                         : string_length(names[at]);
                p8 address_to kept = shell_store_copy(address_of expand_store,
                                                       names[at], length);

                if (!kept)
                {
                        expand_fail_state();
                        return;
                }

                names[at] = kept;
        }

        if (!expand_sort_names(names, count))
        {
                expand_fail_state();
                return;
        }

        for (positive at = 0; at < count; at++)
        {
                if (at && between)
                        expand_push(between,
                                    form == '@' ? MARK_BREAK : mark);

                expand_push_string(names[at], mark);
        }
}

/*
        ${ ... } in every form POSIX gives it.

        The name comes first, then the colon that decides whether empty counts
        as unset, then the operator, then the word -- and the word is a word, so
        it is expanded the same way as any other and can hold another of these.
*/
/*
        The name a subscript resolves to.

        An indexed subscript is arithmetic: a[i+1] and a[-1] are both
        ordinary expressions, the second counting back from the largest
        subscript in use rather than from how many elements there are, which
        is what Bash does and is why a hole does not move it. An associative
        subscript is bytes, so it is expanded as a word and then kept exactly
        as it stands -- spaces, brackets and all.

        The resolved key is retained separately from the base name, so every
        operator can read or write it without evaluating its subscript again.
*/
//      Set while a compound assignment reads its words: an indexed
//      subscript comes back as its expanded text, for arithmetic later.
static bool expand_subscript_deferred;
//      What a refused subscript is called, when not its array's name.
static string_address expand_subscript_named;
COLD string_address shell_subscript_index(string_address base,
                                          positive base_length,
                                          string_address key,
                                          p8 address_to written,
                                          positive address_to key_length,
                                          string_address named);

static COLD string_address expand_subscript_key(string_address base,
                                           positive base_length,
                                           string_address subscript,
                                           positive subscript_length,
                                           p8 address_to written,
                                           positive address_to key_length)
{
        p8 text_local[EXPAND_LOCAL_TEXT];
        string_address text = expand_hold(subscript, subscript_length,
                                          text_local, sizeof(text_local));
        string_address key;

        if (!text)
                return null;

        //      Bash's alias and command tables are associative arrays that
        //      exist only when named, so a subscript must know it is keyed
        //      before it is read as arithmetic.
        if (shell_bash_compat && base_length >= 9 && base_length <= 12 &&
            string_is(base, 'B') && string_is(base + 1, 'A'))
                shell_dynamic_wanted(base, base_length);

        /* The subscript remains one field because capture never splits it,
           but its own quote syntax is still syntax. Passing an outer quoted
           context here retained those bytes, so m['x'] created the literal
           key 'x' and `unset 'm[x]'` could not find it. */
        key = expand_capture(text, false, EXPAND_CAPTURE_TEXT);

        if (expand_failed || !key)
                return null;

        if (expand_subscript_deferred ||
            (shell_array_attributes(base, base_length) &
             SHELL_ARRAY_ASSOCIATIVE))
        {
                address_to key_length = string_length(key);
                return key;
        }

        return shell_subscript_index(
            base, base_length, key, written, key_length,
            expand_subscript_named ? expand_subscript_named : base);
}

/*
        An indexed subscript's text, expanded, made a number: arithmetic,
        and a negative one counted back from the end of the array as it is
        now. Apart from its expansion so that a compound assignment can
        expand every word first and do the arithmetic as it assigns, the way
        bash does: a=([0]=1+2+3 [a[0]]=10) puts 10 at 6.
*/
COLD string_address shell_subscript_index(string_address base,
                                          positive base_length,
                                          string_address key,
                                          p8 address_to written,
                                          positive address_to key_length,
                                          string_address named)
{
        {
                bipolar index = arith_evaluate(key);

                //      Counted back from the end of an array that has
                //      one: an empty array has no end, and bash refuses it.
                if (!arith_bad && index < 0 &&
                    shell_array_length(base, base_length))
                        index += (bipolar)shell_array_highest(base,
                                                              base_length) + 1;

                if (arith_bad)
                {
                        if (!arith_unset)
                                shell_arith_report(writer_stderr_once, null,
                                                   key);
                        if (shell_bash_compat)
                                expand_discard_whole(1);
                        else
                                expand_discard(2);
                        return null;
                }

                if (index < 0)
                {
                        //      A computed index that is still negative is an
                        //      empty expansion, not a fatal one: bash names
                        //      the array and the next command still runs.
                        //      Arithmetic says so itself, when it reads.
                        if (arith_subscript_quiet)
                                return null;
                        expand_where();
                        string_format(writer_stderr_once,
                                      "%s: bad array subscript\n", named);
                        if (!shell_bash_compat)
                                expand_fatal_status(2);
                        return null;
                }

                address_to key_length = bipolar_into_string(written, index);
        }

        return written;
}

/*
        The same resolution, for the left hand side of an assignment.

        a[i+1]=v and m[$k]=v name an element exactly the way ${a[i+1]} reads
        one, so they resolve their subscript through one routine. The answer
        is copied into the expansion arena because the caller is about to
        assign with it and the numeric spelling lived on this frame.
*/
COLD string_address shell_expand_subscript(string_address name, positive length,
                                      string_address subscript,
                                      positive subscript_length,
                                      positive address_to key_length)
{
        p8 written[32];
        string_address key = expand_subscript_key(name, length, subscript,
                                                  subscript_length, written,
                                                  key_length);
        return key ? shell_store_copy(address_of expand_store, key,
                                       address_to key_length) : null;
}

static COLD bool expand_assign_named(expand_reference reference, string_address value)
{
        return reference.key
            ? shell_array_set(reference.name, reference.name_length, reference.key,
                               reference.key_length, value, false)
            : env_assign(reference.name, value);
}

/* Scalar and per-element forms share the same modifier dispatch. The
   replacement separator is restored because array elements reuse the word. */
/*
        What ${x?} says when the script wrote no words of its own.

        The colon form asks about a value as well as a name, and each shell
        says so in its own order: bash calls it null or not set, dash not
        set or null.
*/
static COLD string_address expand_unset_reason(bool colon)
{
        if (!colon)
                return (string_address) "parameter not set";

        return shell_bash_compat
                   ? (string_address) "parameter null or not set"
                   : (string_address) "parameter not set or null";
}

/*
        Bytes dash quotes with CTLESC inside ${@%pat}. _rmescapes then copies
        only $1 (it stops at the NUL join), so the suffix/prefix scan walks
        that copy -- and into the pattern sitting in front of it -- while the
        cut is applied to the full NUL-joined "$@". That is why "${@%*.}" with
        a*c b c is one field a*, and with /usr/local/bin b c is /u.
*/
static PURE bool expand_dash_quoted_ctl(string_address text)
{
        p8 value;

        while ((value = *text++))
        {
                if (value == '!' || value == '*' || value == '-' ||
                    value == '/' || value == ':' || value == '=' ||
                    value == '?' || value == '[' || value == '\\' ||
                    value == ']' || value == '~')
                        return true;
        }

        return false;
}

static COLD p8 address_to expand_dash_scanleft(p8 address_to startp,
                                               p8 address_to rmesc,
                                               string_address pattern,
                                               bool zero)
{
        p8 address_to loc = startp;
        p8 address_to loc2 = rmesc;
        p8 c;

        do
        {
                string_address s = loc2;

                c = *loc2;
                if (zero)
                {
                        *loc2 = end;
                        s = rmesc;
                }
                bool hit = shell_match(pattern, s);
                *loc2 = c;
                if (hit)
                        return loc;
                loc++;
                loc2++;
        } while (c);

        return null;
}

static COLD p8 address_to expand_dash_scanright(p8 address_to startp,
                                                p8 address_to endp,
                                                p8 address_to rmesc,
                                                p8 address_to rmescend,
                                                string_address pattern,
                                                bool zero)
{
        p8 address_to loc;
        p8 address_to loc2;

        for (loc = endp, loc2 = rmescend; loc >= startp; loc2--)
        {
                string_address s = loc2;
                p8 c = *loc2;

                if (zero && loc2 >= rmesc && loc2 <= rmescend)
                {
                        *loc2 = end;
                        s = rmesc;
                }
                bool hit = shell_match(pattern, s);
                *loc2 = c;
                if (hit)
                        return loc;
                loc--;
        }

        return null;
}

static COLD fn expand_push_nul_fields(p8 address_to start, p8 address_to stop,
                                      bool quoted)
{
        p8 mark = quoted ? MARK_QUOTED : MARK_FIELD;
        p8 address_to at = start;
        bool first = true;

        if (start == stop)
        {
                if (quoted)
                        expand_push_empty();
                return;
        }

        while (1)
        {
                p8 address_to next = at;

                next += string_length_max(next, (positive)(stop - next));
                if (!first)
                        expand_push(' ', quoted ? MARK_BREAK : MARK_SEPARATE);
                first = false;
                if (next != at)
                        expand_push_run(at, (positive)(next - at), mark);
                else if (quoted)
                        expand_push_empty();
                if (next >= stop)
                        break;
                at = next + 1;
                if (at == stop)
                {
                        expand_push(' ', quoted ? MARK_BREAK : MARK_SEPARATE);
                        if (quoted)
                                expand_push_empty();
                        break;
                }
        }
}

static COLD fn expand_bash_positional_trim(p8 form, string_address pattern,
                                           bool prefix, bool longest,
                                           bool quoted)
{
        p8 mark = quoted ? MARK_QUOTED : MARK_FIELD;
        p8 between = string_get(expand_ifs());
        bool fields = quoted ? form == '@'
                             : expand_list_by_ifs(form, between);
        positive at;

        if (!shell_parameter_count)
        {
                if (quoted && form == '@')
                        expand_name_at_empty = true;
                return;
        }

        for (at = 0; at < shell_parameter_count && !expand_failed; at++)
        {
                positive start;

                if (at)
                        expand_sequence_between(fields, between, mark);
                start = expand_length;
                expand_push_string(shell_parameter[at], mark);
                expand_trim(start, pattern, prefix, longest);
        }
}

static COLD fn expand_dash_at_trim(string_address pattern, bool prefix,
                                   bool longest, bool quoted)
{
        positive joined = 1;
        positive at;
        positive first_length = 0;
        positive pattern_length;
        bool copy;
        p8 address_to block;
        p8 address_to startp;
        p8 address_to endp;
        p8 address_to rmesc;
        p8 address_to rmescend;
        p8 address_to loc;
        p8 address_to used;
        bool zero = prefix;
        bool left = prefix != longest;

        if (!shell_parameter_count)
        {
                if (quoted)
                        expand_push_empty();
                return;
        }

        for (at = 0; at < shell_parameter_count; at++)
        {
                positive run = string_length(shell_parameter[at]);

                if (joined > (positive)-1 - run - (at ? 1 : 0))
                {
                        expand_fail_state();
                        return;
                }
                joined += run + (at ? 1 : 0);
        }

        first_length = string_length(shell_parameter[0]);
        pattern_length = string_length(pattern);
        copy = quoted && expand_dash_quoted_ctl(shell_parameter[0]);
        if (joined > (positive)-1 - pattern_length - 1 -
                         (copy ? first_length + 1 : 0))
        {
                expand_fail_state();
                return;
        }

        block = shell_store_take(address_of expand_store,
                                 joined + pattern_length + 1 +
                                     (copy ? first_length + 1 : 0));
        if (!block)
        {
                expand_fail_state();
                return;
        }

        used = block;
        for (at = 0; at < shell_parameter_count; at++)
        {
                positive run = string_length(shell_parameter[at]);

                if (at)
                        *used++ = end;
                memory_copy_apart(used, shell_parameter[at], run);
                used += run;
        }
        *used++ = end;
        memory_copy_apart(used, pattern, pattern_length + 1);
        startp = block;
        endp = block + joined - 1;
        if (copy)
        {
                p8 address_to into = used + pattern_length + 1;

                memory_copy_apart(into, shell_parameter[0], first_length);
                into[first_length] = end;
                rmesc = into;
                rmescend = into + first_length;
        }
        else
        {
                rmesc = startp;
                rmescend = endp;
        }

        loc = left ? expand_dash_scanleft(startp, rmesc, pattern, zero)
                   : expand_dash_scanright(startp, endp, rmesc, rmescend,
                                           pattern, zero);
        if (loc)
        {
                if (zero)
                {
                        memory_copy(startp, loc, (positive)(endp - loc));
                        loc = startp + (endp - loc);
                }
                *loc = end;
        }
        else
                loc = endp;

        expand_push_nul_fields(startp, loc, quoted);
}

static PURE bool expand_positional_list(expand_reference reference)
{
        return string_get(reference.name + 1) == end &&
               (string_is(reference.name, '@') || string_is(reference.name, '*'));
}

/*
        Replacement and case change on $@ and $*: bash applies the operator
        to each positional parameter and then joins them the way the name
        joins, as it does for # and %. This applied it to the joined string, so
        "${@/#/-I}" was one word -I a b, and ${@^} changed only $1.
*/
static COLD fn expand_positional_each(p8 form, p8 operation,
                                      string_address word,
                                      string_address replacement_text,
                                      bool doubled, bool quoted)
{
        p8 mark = quoted ? MARK_QUOTED : MARK_FIELD;
        p8 between = string_get(expand_ifs());
        bool fields = quoted ? form == '@'
                             : expand_list_blanks(form, between);
        bool default_pattern = !string_get(word);
        string_address pattern;
        string_address replacement = null;

        if (operation == '/')
        {
                pattern = expand_capture(word, false, EXPAND_CAPTURE_PATTERN);
                replacement = expand_capture(replacement_text, false,
                                             EXPAND_CAPTURE_REPLACEMENT);
        }
        else
                pattern = default_pattern
                              ? (string_address) "?"
                              : expand_capture(word, false,
                                               EXPAND_CAPTURE_PATTERN);

        if (expand_failed || !pattern || (operation == '/' && !replacement))
                return;

        if (!shell_parameter_count)
        {
                if (quoted && form == '@')
                        expand_name_at_empty = true;
                return;
        }

        for (positive at = 0; at < shell_parameter_count && !expand_failed;
             at++)
        {
                positive start;

                if (at)
                        expand_sequence_between(fields, between, mark);
                start = expand_length;
                if (operation == '/')
                        expand_replace_value(
                            (p8 address_to)shell_parameter[at],
                            string_length(shell_parameter[at]), pattern,
                            replacement, doubled, mark);
                else
                {
                        expand_push_string(shell_parameter[at], mark);
                        expand_case_toggle = operation == '~';
                        expand_case_span(start, operation == '^', doubled,
                                         pattern, default_pattern);
                        expand_case_toggle = false;
                }
        }
}

static fn expand_modifier(expand_reference reference, p8 operation, bool doubled,
                           string_address word, bool quoted, b32 parameter_mode)
{
        /*
                The word after the operator is expanded only when it can
                matter. Neither reference expands it for a parameter that is
                not set, and bash does not for an empty one either when the
                word is a pattern to remove; this expanded it every time, so
                ${x#$(cmd)} ran cmd and ${x%$((1/0))} stopped the shell with x
                unset, where both references write nothing and go on. What is
                pushed instead is the parameter itself, which is empty and
                still answers to set -u. $@ and $* are lists and keep their
                own path.
        */
        {
                string_address name = reference.name;
                bool list = string_get(name + 1) == end &&
                            (string_is(name, '@') || string_is(name, '*'));
                bool present = !(parameter_mode & EXPAND_PARAMETER_MISSING);
                positive bytes = 0;
                p8 scratch[32];

                //      ${x@a} names the attributes of a declared name that
                //      has no value, so a transform is never skipped.
                list = list || operation == '@';
                if (!list && present)
                        expand_value_of(reference, scratch, address_of present,
                                        address_of bytes);
                if (!list &&
                    (!present || (shell_bash_compat && !bytes &&
                                  (operation == '#' || operation == '%'))))
                {
                        expand_push_parameter_as(reference, quoted, parameter_mode);
                        return;
                }
        }

        if (operation == '#' || operation == '%')
        {
                string_address name = reference.name;
                bool all = string_get(name + 1) == end &&
                           (string_is(name, '@') || string_is(name, '*'));
                string_address pattern = expand_capture(
                    word, false, EXPAND_CAPTURE_PATTERN);

                if (expand_failed)
                        return;
                if (all && shell_bash_compat)
                        expand_bash_positional_trim(string_get(name), pattern,
                                                    operation == '#', doubled,
                                                    quoted);
                else if (all && quoted && string_is(name, '@'))
                        expand_dash_at_trim(pattern, operation == '#', doubled,
                                            quoted);
                else
                {
                        positive start = expand_length;

                        expand_push_parameter_as(reference, quoted,
                                                 parameter_mode);
                        expand_trim(start, pattern, operation == '#', doubled);
                }
        }
        else if (operation == '/')
        {
                //      A pattern may begin with a slash: bash looks for the
                //      one that ends it after that, so ${x////c} replaces
                //      each / with c.
                string_address separator = expand_replace_separator(
                    string_is(word, '/') ? word + 1 : word);
                string_address replacement = (string_address)"";
                if (separator)
                {
                        *separator = end;
                        replacement = separator + 1;
                }
                if (expand_positional_list(reference) && shell_bash_compat)
                        expand_positional_each(string_get(reference.name),
                                               operation, word, replacement,
                                               doubled, quoted);
                else
                        expand_replace(reference, word, replacement, quoted,
                                       doubled, parameter_mode);
                if (separator)
                        *separator = '/';
        }
        else if (operation == ':')
                expand_substring(reference, word, quoted, parameter_mode);
        else if ((operation == '^' || operation == ',' || operation == '~') &&
                 expand_positional_list(reference) && shell_bash_compat)
                expand_positional_each(string_get(reference.name), operation,
                                       word, null, doubled, quoted);
        else if (operation == '^' || operation == ',' || operation == '~')
        {
                expand_case_toggle = operation == '~';
                expand_case_change(reference, word, quoted, operation == '^',
                                   doubled, parameter_mode);
                expand_case_toggle = false;
        }
        else
                expand_transform(reference, word, quoted, parameter_mode);
}

/* Values, keys, slices and per-element modifiers all retain one inventory
   and share field/empty-array policy. Quoted [@] supplies field boundaries;
   the other forms join on IFS for the normal splitting stage. */
static COLD fn expand_array_sequence(string_address name, positive length,
                                     p8 form, p8 operation, bool doubled,
                                     string_address word, bool quoted, bool keys)
{
        p8 mark = quoted ? MARK_QUOTED : MARK_FIELD;
        p8 between = string_get(expand_ifs());
        bool fields = quoted ? form == '@'
                             : operation == '#' || operation == '%' || operation == '@'
                                   ? expand_list_by_ifs(form, between)
                                   : expand_list_blanks(form, between);
        bool slice = operation == ':';
        bool transform = operation && !slice;
        shell_mark held = shell_store_mark(address_of expand_store);
        positive count = shell_array_length(name, length);
        positive begin = 0;
        positive finish;
        positive offset = 0;
        positive wanted = positive_max;
        shell_array_item address_to items;
        p8 written[32];

        if (!count)
                goto empty;

        if (slice)
        {
                if (!expand_slice_bounds((expand_reference){.name = name}, word, length, 0, SLICE_ARRAY,
                                         address_of offset, address_of wanted) ||
                    !wanted)
                        goto empty;
                // Offset arithmetic may replace the array. Borrow its live
                // elements only after evaluating both bounds.
                count = shell_array_length(name, length);
                if (!count)
                        goto empty;
        }

        if (count > positive_max / sizeof(items[0]) ||
            !(items = (shell_array_item address_to)shell_store_take_aligned(
                  address_of expand_store, count * sizeof(items[0]))))
        {
                expand_fail_state();
                goto done;
        }
        shell_array_items(name, length, items, count);

        finish = count;
        if (slice)
        {
                // Sparse indexed slices begin at the first live index, not
                // at the offset-th element.
                while (begin < finish)
                {
                        positive middle = begin + (finish - begin) / 2;
                        if (items[middle].index < offset)
                                begin = middle + 1;
                        else
                                finish = middle;
                }
                finish = wanted < count - begin ? begin + wanted : count;
        }
        if (begin == finish)
                goto empty;

        for (positive at = begin; at < finish && !expand_failed; at++)
        {
                if (at > begin)
                        expand_sequence_between(fields, between, mark);

                if (!transform && (!keys || slice))
                {
                        expand_push_run(items[at].value, items[at].value_length,
                                        mark);
                        continue;
                }

                string_address key = items[at].key;
                positive key_length = items[at].key_length;
                if (!key)
                {
                        key_length = bipolar_into_string(
                            written, (bipolar)items[at].index);
                        key = written;
                }
                if (!transform)
                {
                        expand_push_run(key, key_length, mark);
                        continue;
                }

                // A modifier's word may replace the cell that owns this key.
                key = shell_store_copy(address_of expand_store, key, key_length);
                if (!key)
                {
                        expand_fail_state();
                        break;
                }
                expand_modifier((expand_reference){.name = name, .name_length = length,
                    .key = key, .key_length = key_length}, operation, doubled, word, quoted, 0);
        }
        goto done;

empty:
        // Quoted @ over no elements is no field, not one empty field.
        if (form == '@')
                expand_name_at_empty = true;
done:
        shell_store_rewind(address_of expand_store, held);
}

/*
        @A and @K name the array, not each element. Per-element application
        would turn ${a[@]@A} into one assignment per value; bash writes a
        single declare (or a key/value listing) and lets IFS split the flags.
*/
static shell_array_item address_to expand_array_items_take(
    string_address name, positive length, positive count,
    shell_mark address_to held)
{
        shell_array_item address_to items;

        address_to held = shell_store_mark(address_of expand_store);
        if (count > positive_max / sizeof(items[0]) ||
            !(items = (shell_array_item address_to)shell_store_take_aligned(
                  address_of expand_store, count * sizeof(items[0]))))
        {
                expand_fail_state();
                return null;
        }

        shell_array_items(name, length, items, count);
        return items;
}

// The subscript a row answers to, spelled the way its transform spells one:
// as it stands for @k, quoted as declare would for @K and for the declare
// text itself. A row with no key of its own is an index.
static COLD fn expand_array_subscript(shell_array_item address_to item,
                                      p8 address_to written, p8 mark,
                                      bool declared)
{
        if (!item->key)
                expand_push_run(written,
                                bipolar_into_string(written,
                                                    (bipolar)item->index),
                                mark);
        else if (declared)
                transform_declare_key(item->key, item->key_length, mark);
        else
                expand_push_run(item->key, item->key_length, mark);
}

// What goes between a subscript and its value, and between one pair and the
// next: a field boundary for @k, a plain space for @K.
static COLD fn expand_array_apart(bool declared, bool fields, p8 between,
                                  p8 mark)
{
        if (declared)
                expand_push(' ', mark);
        else
                expand_sequence_between(fields, between, mark);
}

static COLD fn expand_array_whole_transform(string_address name, positive length,
                                            p8 form, p8 which, bool quoted)
{
        p8 mark_join = (quoted && form == '*') ? MARK_QUOTED : MARK_FIELD;
        p8 mark_body = quoted ? MARK_QUOTED : MARK_FIELD;
        p8 between = string_get(expand_ifs());
        bool fields = quoted ? form == '@'
                             : expand_list_by_ifs(form, between);
        p8 attributes;
        bool exported;
        bool readonly;
        bool keyed;
        p8 letters[8];
        positive flags;
        positive count;
        positive at;
        shell_mark held;
        shell_array_item address_to items;
        p8 written[32];

        shell_dynamic_wanted(name, length);
        attributes = shell_array_attributes(name, length);
        keyed = (attributes & SHELL_ARRAY_ASSOCIATIVE) != 0;
        exported = shell_variable_exported(name, length);
        readonly = transform_name_readonly(name, length, attributes);
        flags = shell_attribute_letters(letters, attributes, readonly, exported);
        count = shell_array_length(name, length);

        //      @k and @K are one walk written two ways: @k puts subscript
        //      and value in as they stand, so IFS parts them, while @K
        //      quotes both the way declare would and puts a space between.
        if (which == 'k' || which == 'K')
        {
                bool declared = which == 'K';

                if (!count)
                {
                        if (form == '@')
                                expand_name_at_empty = true;
                        return;
                }

                items = expand_array_items_take(name, length, count,
                                                address_of held);
                if (!items)
                        return;
                for (at = 0; at < count && !expand_failed; at++)
                {
                        if (at)
                                expand_array_apart(declared, fields, between,
                                                   mark_body);
                        expand_array_subscript(items + at, written, mark_body,
                                               declared);
                        expand_array_apart(declared, fields, between,
                                           mark_body);
                        if (declared)
                                transform_declare_quoted(
                                    items[at].value, items[at].value_length,
                                    mark_body);
                        else
                                expand_push_run(items[at].value,
                                                items[at].value_length,
                                                mark_body);
                }
                if (declared && keyed)
                        expand_push(' ', mark_body);
                shell_store_rewind(address_of expand_store, held);
                return;
        }

        expand_push_run((string_address) "declare -", 9, mark_join);
        if (flags)
                expand_push_run(letters, flags, mark_join);
        expand_push(' ', mark_join);
        expand_push_run(name, length, mark_join);

        if (!(attributes & SHELL_ARRAY_ASSIGNED) && !count)
                return;

        expand_push('=', mark_join);
        expand_push('(', mark_body);

        if (count)
        {
                items = expand_array_items_take(name, length, count,
                                                address_of held);
                if (!items)
                        return;
                for (at = 0; at < count && !expand_failed; at++)
                {
                        if (at)
                                expand_push(' ', mark_body);
                        expand_push('[', mark_body);
                        expand_array_subscript(items + at, written, mark_body,
                                               true);
                        expand_push_run((string_address) "]=", 2, mark_body);
                        transform_declare_quoted(items[at].value,
                                                 items[at].value_length,
                                                 mark_body);
                }
                if (keyed && count)
                        expand_push(' ', mark_body);
                shell_store_rewind(address_of expand_store, held);
        }

        expand_push(')', mark_body);
}

/*
        What the whole array answers to.

        ${#a[@]} is a count and not a length, ${!a[@]} is the subscripts,
        ${a[@]:1:2} selects elements, and every byte operator is that
        operator applied to each element in turn. What is left -- no operator,
        or one of the four that ask whether a parameter is set -- reads an
        array as set when it has any element at all, which is how a=() comes
        out unset.

        Kept out of the reader that dispatches to it: none of this is on the
        path a scalar takes, and the buffer it needs was in that reader's
        frame for every ${x} in every script that has no array in it.
*/
/*
        Whether a list is null to :- :+ := and :?, which bash asks of the
        list joined: with no element, or with only empty ones and nothing
        between them -- one element, or quoted * with IFS empty. ("" "") is
        not null as @, whose join is a blank, where this called any list of
        empty elements null and a single "" set.
*/
static bool expand_list_null_direct = true;

//      Whether a list of count elements can be null at all, before anyone
//      looks at them: one element, or quoted * joined by nothing.
static bool expand_list_join_empty(positive count, p8 form, bool quoted)
{
        return count == 1 || (quoted && form == '*' &&
                              !string_get(expand_ifs()) &&
                              env_get("IFS") != null);
}

static bool expand_list_null(positive count, bool every_empty, p8 form,
                             bool quoted)
{
        return !count ||
               (every_empty && expand_list_join_empty(count, form, quoted));
}

static COLD bool expand_array_every_empty(string_address name,
                                          positive length, positive count)
{
        shell_mark held = shell_store_mark(address_of expand_store);
        shell_array_item address_to items;
        bool empty = true;

        if (count > positive_max / sizeof(items[0]) ||
            !(items = (shell_array_item address_to)shell_store_take_aligned(
                  address_of expand_store, count * sizeof(items[0]))))
                return false;
        shell_array_items(name, length, items, count);
        for (positive at = 0; at < count && empty; at++)
                empty = !items[at].value_length;
        shell_store_rewind(address_of expand_store, held);
        return empty;
}

static COLD fn expand_array_form(string_address name, positive length,
                                 p8 form, bool want_length, p8 operation,
                                 bool doubled, string_address word,
                                 bool quoted, b32 parameter_mode, p8 mark)
{
        bool keys = (parameter_mode & EXPAND_PARAMETER_INDIRECT) != 0;
        positive held;
        p8 written[32];

        // GROUPS, DIRSTACK and BASH_VERSINFO are made where they are first
        // named, the same as the three the call stack publishes.
        shell_dynamic_wanted(name, length);

        held = shell_array_length(name, length);

        //      With a colon an array of nothing but empty elements can be
        //      null too; bash joins it and asks the join.
        if (doubled && held && shell_bash_compat &&
            (expand_list_null_direct || !quoted) &&
            (operation == '-' || operation == '+' || operation == '=' ||
             operation == '?') &&
            expand_list_join_empty(held, form, quoted) &&
            expand_array_every_empty(name, length, held))
                held = 0;

        if (want_length)
        {
                expand_push_run(written,
                                bipolar_into_string(written, (bipolar)held),
                                mark);

                return;
        }

        if (operation == '@' && !string_get(word + 1) &&
            (string_get(word) == 'A' || string_get(word) == 'K' ||
             string_get(word) == 'k'))
        {
                expand_array_whole_transform(name, length, form,
                                             string_get(word), quoted);
                return;
        }

        if (operation == ':' || operation == '#' || operation == '%' ||
            operation == '/' || operation == '^' || operation == ',' ||
            operation == '~' ||
            operation == '@')
                expand_array_sequence(name, length, form, operation, doubled,
                                      word, quoted, keys);
        else if (operation == '-' && !held)
                expand_word_into(word, quoted);
        else if (operation == '+' && held)
                expand_word_into(word, quoted);
        else if (operation == '?' && !held)
        {
                string_address said =
                    expand_capture(word, quoted, EXPAND_CAPTURE_WORD);

                if (expand_failed)
                        return;

                shell_diagnostic_where_to(writer_stderr_once);
                string_format(writer_stderr_once, "%s: %s\n", name,
                              said[0] ? said
                                      : expand_unset_reason(doubled));
                expand_fatal_status(expand_nounset_status(
                    parameter_mode & EXPAND_PARAMETER_INDIRECT));
        }
        else if (operation != '+')
                expand_array_sequence(name, length, form, 0, false, word,
                                      quoted, keys);
}

static string_address expand_braced_body(string_address step,
                                        string_address close, bool quoted);

static HOT string_address expand_braced_length(string_address name,
                                               positive length,
                                               string_address close,
                                               bool quoted)
{
        expand_reference reference = {.name = name, .name_length = length};
        p8 written[32];
        p8 scratch[32];
        bool present = true;
        positive count = 0;
        p8 mark = quoted ? MARK_QUOTED : MARK_FIELD;
        string_address value;

        value = expand_value_of(reference, scratch, address_of present,
                                address_of count);
        if (present && count)
                count = expand_character_count(value, count);

        if (!present)
        {
                if (shell_options & ((positive)1 << ('u' - 'a')))
                {
                        expand_unbound(reference, false);
                        return close + 1;
                }

                count = 0;
        }

        expand_push_run(written,
                        bipolar_into_string(written, (bipolar)count), mark);
        return close + 1;
}

bipolar shell_funsub_run(string_address text, bool value_form,
                         b32 address_to status);
static fn expand_read_substitution(b32 fd, p8 mark, positive start);

/*
        ${ command; } and ${| command; }: run in this shell, the first
        replaced by what it wrote with the newlines at the end taken off, the
        second by REPLY as it stands.
*/
static COLD string_address expand_funsub(string_address step, bool quoted)
{
        string_address inner = step + 2;
        bool value_form = string_is(inner, '|');
        string_address after = lex_nesting(step + 1);
        string_address close = after == step + 1 ? null : after - 1;
        p8 mark = quoted ? MARK_QUOTED : MARK_FIELD;
        positive start = expand_length;
        p8 address_to text;
        positive length;
        bipolar handle;
        b32 status = 0;

        if (!close)
        {
                expand_push('$', MARK_PLAIN);
                return step + 1;
        }

        if (value_form)
                inner++;
        length = (positive)(close - inner);
        text = shell_store_take(address_of expand_store, length + 3);
        if (!text)
        {
                expand_overflow = true;
                return close + 1;
        }
        memory_copy(text, inner, length);
        text[length] = '\n';
        text[length + 1] = end;

        handle = shell_funsub_run(text, value_form, address_of status);
        if (handle < 0)
        {
                expand_where();
                writer_stderr_once(str("cannot make pipe for command substitution\n"));
                return close + 1;
        }

        expand_substitution_keep = value_form;
        expand_read_substitution((b32)handle, mark, start);
        expand_substitution_keep = false;
        expand_substitution_done(status);

        return close + 1;
}

static HOT __attribute__((noinline)) string_address expand_braced(
        string_address step, bool quoted)
{
        string_address inner = step + 2;
        string_address at = inner;
        string_address close;

        if (shell_bash_compat &&
            (string_is(inner, ' ') || string_is(inner, '\t') ||
             string_is(inner, '\n') || string_is(inner, '|')))
                return expand_funsub(step, quoted);

        p8 name_buf[EXPAND_LOCAL_NAME];
        positive interior;
        positive name_length;
        p8 op;
        p8 last;
        bool doubled = false;

        while (1)
        {
                p8 value = string_get(at);

                if (value == '}')
                {
                        close = at;
                        break;
                }

                if (!value)
                {
                        expand_push('$', MARK_PLAIN);
                        return step + 1;
                }

                if (value == '{' || value == '\'' || value == '"' ||
                    value == '\\' || value == '$' || value == '`')
                {
                        close = expand_bracket_end_quoted(
                            inner, '{', '}', quoted && shell_posix_on(),
                            true);
                        if (!close)
                        {
                                expand_push('$', MARK_PLAIN);
                                return step + 1;
                        }

                        return expand_braced_body(step, close, quoted);
                }

                at++;
        }

        interior = (positive)(close - inner);

        // ${#name} with an ordinary name: no operator word to hold.
        if (interior >= 2 && inner[0] == '#')
        {
                name_length = interior - 1;
                if (name_length < EXPAND_LOCAL_NAME &&
                    string_span(inner + 1, string_set_name) == name_length)
                {
                        memory_copy_end(name_buf, inner + 1, name_length);
                        return expand_braced_length(name_buf, name_length, close,
                                                    quoted);
                }
        }

        // ${name#?} and ${name%?} (and the longest ## / %% cousins).
        if (interior >= 3)
        {
                last = inner[interior - 1];
                op = inner[interior - 2];
                name_length = interior - 2;
                if (last == '?' && (op == '#' || op == '%'))
                {
                        if (name_length && inner[name_length - 1] == op)
                        {
                                doubled = true;
                                name_length--;
                        }

                        if (name_length && name_length < EXPAND_LOCAL_NAME &&
                            string_span(inner, string_set_name) == name_length)
                        {
                                expand_reference reference;
                                positive start;

                                memory_copy_end(name_buf, inner, name_length);
                                reference = (expand_reference){
                                    .name = name_buf,
                                    .name_length = name_length};
                                start = expand_length;
                                expand_push_parameter_as(reference, quoted, 0);
                                if (!expand_failed)
                                        expand_trim(start, "?", op == '#',
                                                    doubled);
                                return close + 1;
                        }
                }
        }

        return expand_braced_body(step, close, quoted);
}

// A name followed by one whole subscript, as an indirect value may be.
static PURE bool expand_indirect_element(string_address name, positive length)
{
        positive base = string_span_max(name, length, string_set_name);
        string_address shut;

        if (!base || byte_is_digit(string_get(name)) || base + 2 >= length ||
            name[base] != '[' || name[length - 1] != ']')
                return false;

        shut = expand_bracket_end(name + base + 1, '[', ']');
        return shut == name + length - 1;
}

static COLD string_address expand_indirect_through(string_address name,
                                                   positive length, p8 form,
                                                   string_address rest,
                                                   string_address close,
                                                   bool quoted)
{
        positive tail = (positive)(close - rest);
        p8 address_to joined_text;
        string_address joined;
        positive joined_length;
        p8 address_to built;

        if (!shell_array_length(name, length))
        {
                p8 which[2] = {form, end};

                expand_where();
                string_format(writer_stderr_once,
                              "%s[%s]: invalid indirect expansion\n", name,
                              which);
                expand_indirect_error();
                return close + 1;
        }
        {
                positive count = shell_array_length(name, length);
                positive room = 1;
                shell_array_item address_to items;

                if (count > positive_max / sizeof(items[0]) ||
                    !(items = (shell_array_item address_to)shell_store_take_aligned(
                          address_of expand_store, count * sizeof(items[0]))))
                {
                        expand_fail_state();
                        return close + 1;
                }
                shell_array_items(name, length, items, count);
                for (positive at = 0; at < count; at++)
                        room += items[at].value_length + 1;
                if (!(joined_text = shell_store_take(address_of expand_store,
                                                     room)))
                {
                        expand_fail_state();
                        return close + 1;
                }
                joined_length = 0;
                for (positive at = 0; at < count; at++)
                {
                        if (at)
                                joined_text[joined_length++] = ' ';
                        memory_copy(joined_text + joined_length,
                                    items[at].value, items[at].value_length);
                        joined_length += items[at].value_length;
                }
                joined_text[joined_length] = end;
                joined = joined_text;
        }
        if (!expand_parameter_name(joined, joined_length) &&
            !expand_indirect_element(joined, joined_length))
        {
                expand_where();
                string_format(writer_stderr_once,
                              "%s: invalid variable name\n", joined);
                expand_indirect_error();
                return close + 1;
        }
        if (!(built = shell_store_take(address_of expand_store,
                                       joined_length + tail + 4)))
        {
                expand_fail_state();
                return close + 1;
        }
        built[0] = '$';
        built[1] = '{';
        memory_copy(built + 2, joined, joined_length);
        memory_copy(built + 2 + joined_length, rest, tail);
        built[2 + joined_length + tail] = '}';
        built[3 + joined_length + tail] = end;
        expand_braced_body(built, built + 2 + joined_length + tail, quoted);
        return close + 1;
}

/*
        Whether ${#...} asks for a length. Only a whole parameter may follow
        the #: a name, a positional number or one special character, with a
        subscript, and then the brace. Anything else makes the # the
        parameter $# and what follows its operator, so ${###} is $# with an
        empty prefix taken off (25) and ${##2} takes the 2 off 25, as bash
        and dash read them; ${##} is still the length of $#.
*/
static PURE bool expand_length_form(string_address at, string_address close)
{
        p8 first = string_get(at);

        if (at >= close)
                return false;
        if (first == '@' || first == '*' || first == '#' || first == '?' ||
            first == '$' || first == '!' || first == '-')
                at++;
        else if (byte_is_digit(first))
                at += string_span(at, string_set_digits);
        else
                at += string_span(at, string_set_name);
        if (at < close && string_is(at, '[') && !shell_dash_compat)
        {
                string_address shut = expand_bracket_end(at + 1, '[', ']');

                if (!shut)
                        return false;
                at = shut + 1;
        }
        return at == close;
}

/*
        How many braced expansions are being read, and which one this is.

        A nameref to an element holds the subscript as text, and reading the
        name evaluates it -- commands and all. One ${r} reads the name more
        than once (to tell what is set, then to write it), and Bash evaluates
        the subscript once for the whole expansion, so a subscript with a
        $( ) in it ran its command twice here. The evaluation is kept for
        the expansion that made it: shell_reference_element_value asks
        whether one is in progress, and which, and reuses the answer for
        the same text within it.
*/
static positive expand_braced_active;
static positive expand_braced_generation;

static string_address expand_braced_body_run(string_address step,
                                             string_address close, bool quoted);

static string_address expand_braced_body(string_address step,
                                        string_address close, bool quoted)
{
        string_address after;

        expand_braced_generation++;
        expand_braced_active++;
        after = expand_braced_body_run(step, close, quoted);
        expand_braced_active--;
        return after;
}

static string_address expand_braced_body_run(string_address step,
                                             string_address close, bool quoted)
{
        string_address whole = step;
        string_address name_start;
        p8 name_local[EXPAND_LOCAL_NAME];
        p8 word_local[EXPAND_LOCAL_TEXT];
        p8 indirect_scratch[32];
        string_address name;
        expand_reference reference = {0};
        string_address word;
        // The name without its subscript, kept for the forms that mean the
        // whole array rather than one element of it.
        string_address plain_name = null;
        positive plain_length = 0;
        p8 array_form = 0;
        p8 mark = quoted ? MARK_QUOTED : MARK_FIELD;
        positive length = 0;
        bool want_length = false;
        b32 parameter_mode = 0;
        bool colon = false;
        bool doubled = false;
        p8 name_list = 0;
        p8 operation = 0;
        p8 seen;
        bool element_refused = false;

        step += 2;

        // ${#} is how many parameters there are; ${#x} is how long one is.
        if (string_is(step, '#') && expand_length_form(step + 1, close))
        {
                want_length = true;
                step++;
        }

        /*
                Bash ${!name} expands name once and uses that value as the
                parameter to read. ${!} itself remains the ordinary special
                parameter: an operator after the bang is $!, not prefix
                indirection, so ${!:+set} follows whether a background job
                has a pid. POSIX ${!#} / ${!?} are that same $! with a
                trim or ? operator rather than ${#} / ${?} indirection.
                Dash has no indirection, so a bang is always $!.
        */
        if (shell_bash_compat && string_is(step, '!') && step + 1 < close)
        {
                p8 next = string_get(step + 1);

                if (next != ':' && next != '-' && next != '+' && next != '=' &&
                    next != '%' && next != '/' && next != '^' && next != ',' &&
                    next != '~' &&
                    !(shell_posix_on() && (next == '#' || next == '?')))
                {
                        parameter_mode = EXPAND_PARAMETER_INDIRECT;
                        step++;
                }
        }

        seen = string_get(step);
        name_start = step;

        if (seen == '@' || seen == '*' || seen == '#' || seen == '?' ||
            seen == '$' || seen == '!' || seen == '-')
        {
                length = 1;
                step++;
        }
        else
        {
                length = string_span(step, byte_is_digit(seen)
                                           ? string_set_digits : string_set_name);
                step += length;
        }

        name = expand_hold(name_start, length, name_local,
                           sizeof(name_local));

        if (!name)
        {
                expand_fail_state();
                return close + 1;
        }

        reference.name = name;
        reference.name_length = length;
        seen = string_get(step);

        /*
                A subscript belongs to the name and not to the operators
                after it, so it is read and resolved right here. Every
                operator below then sees one already-resolved name, and [@]
                and [*] -- which mean the whole array and not an element of
                it -- are recognised before anything tries to read a value.
        */
        if (seen == '[' && !shell_dash_compat && expand_assignable_name(name))
        {
                string_address shut = expand_bracket_end(step + 1, '[', ']');
                positive inner;

                if (!shut || shut >= close)
                {
                        expand_bad_substitution(whole, close);
                        expand_fatal_status((shell_bash_compat || (parameter_mode & EXPAND_PARAMETER_INDIRECT)) ? 1 : 2);
                        return close + 1;
                }

                inner = (positive)(shut - step - 1);
                plain_name = name;
                plain_length = length;

                if (inner == 1 &&
                    (string_is(step + 1, '@') || string_is(step + 1, '*')))
                        array_form = string_get(step + 1);
                else
                {
                        reference.key = shell_expand_subscript(name, length, step + 1,
                                                               inner, &reference.key_length);
                        //      A subscript counting back past the start is
                        //      said and then read as an unset element, so
                        //      ${a[-9]-none} is none in bash.
                        if (!reference.key && shell_bash_compat &&
                            !expand_failed)
                        {
                                parameter_mode |= EXPAND_PARAMETER_MISSING;
                                element_refused = true;
                        }
                        else if (!reference.key)
                                return close + 1;
                }

                step = shut + 1;
                seen = string_get(step);
        }

        /*
                ${!a[@]} alone is the subscripts. With an operator after it
                bash reads ${!r...} whose r is "${a[*]}": the elements,
                joined, name the parameter the operator is applied to, so
                a=(v) makes ${!a[@]:2} the same as ${v:2}.
        */
        if (array_form && (parameter_mode & EXPAND_PARAMETER_INDIRECT) &&
            step < close && shell_bash_compat)
                return expand_indirect_through(plain_name, plain_length,
                                               array_form, step, close,
                                               quoted);

        if (!array_form && !reference.key && (parameter_mode & EXPAND_PARAMETER_INDIRECT) &&
            expand_assignable_name(name) && step + 1 == close &&
            (seen == '@' || seen == '*'))
        {
                name_list = seen;
                seen = string_get(++step);
        }

        if (seen == ':')
        {
                colon = true;
                step++;
                seen = string_get(step);
        }

        if (seen == '-' || seen == '=' || seen == '?' || seen == '+')
        {
                operation = seen;
                step++;
        }
        else if (colon && shell_bash_compat)
                operation = ':';
        /*
                Bash reads a colon that is not one of those four as substring:
                ${name:offset} and ${name:offset:length}, including a nested
                expansion in the offset. Dash has none of that. A colon after
                a name is only :- := :? :+, and a colon followed by a dollar,
                a digit, or anything else is a bad substitution that ends the
                process.
        */
        else if (!colon && (seen == '%' || seen == '#'))
        {
                operation = seen;
                step++;

                if (string_is(step, seen))
                {
                        doubled = true;
                        step++;
                }
        }
        /*
                Pattern substitution, case conversion and ${v@X} are bash's.
                dash has none of them, so under its name they are not
                operators at all and fall to the refusal below -- the Bad
                substitution that ends the process -- rather than doing the
                bash thing: ${x/a/b}, ${x^^}, ${x,} and ${x@U} all ran here
                where dash stops.
        */
        else if (!colon && seen == '/' && shell_bash_compat)
        {
                operation = seen;
                step++;

                if (string_is(step, '/'))
                {
                        doubled = true;
                        step++;
                }
        }
        else if (!colon && (seen == '^' || seen == ',' || seen == '~') &&
                 shell_bash_compat)
        {
                operation = seen;
                step++;

                if (string_is(step, seen))
                {
                        doubled = true;
                        step++;
                }
        }
        //      ${v@X}: one letter naming a transformation of the value. It
        //      takes no doubled form and no pattern -- the letter is the
        //      whole of the word behind it.
        //      ${v@} with no letter is still a transform: bash refuses it as
        //      a bad substitution. The letter is read as the word below, and
        //      an empty word takes the unknown-letter path.
        else if (!colon && seen == '@' && shell_bash_compat)
        {
                operation = seen;
                step++;
        }

        /*
                Nothing after a name is harmless unless it is one of the
                operators parsed above. In particular, / and ^ introduce
                Bash substitutions this shell does not implement. Returning
                the unmodified value made ${x//X/-} and ${x^^} look as though
                they had worked, with plausible but wrong data. Refuse every
                unknown suffix just as dash does. Length form has no operator
                tail of its own. Dash has already refused a colon that did
                not introduce :- := :? or :+; bash has already taken
                substring.
        */
        if (!length || ((parameter_mode & EXPAND_PARAMETER_INDIRECT) &&
                        length == 1 &&
                        (string_is(name, '!') || string_is(name, '$'))) ||
            (colon && !operation) ||
            (!operation && step != close) ||
            (want_length && (operation || name_list)))
        {
                expand_bad_substitution(whole, close);
                expand_arithmetic_error();

                return close + 1;
        }

        {
                positive room = (positive)(close - step);

                word = expand_hold(step, room, word_local,
                                   sizeof(word_local));

                if (!word)
                        return close + 1;
        }

        //      A refused element has no length and nothing to transform.
        if (element_refused && (want_length || operation == '@'))
                return close + 1;

        /*
                ${name@X} names one transformation. An unknown letter is a
                bad substitution, but only when the name is set: bash 5.2
                leaves an unset ${x@Z} empty and continues. Nounset still
                sees the missing name.
        */
        if (operation == '@')
        {
                p8 which = string_get(word);
                bool known = which && !string_get(word + 1) &&
                             (which == 'Q' || which == 'E' || which == 'P' ||
                              which == 'A' || which == 'a' || which == 'K' ||
                              which == 'k' || which == 'U' || which == 'u' ||
                              which == 'L');

                if (!known)
                {
                        p8 scratch[32];
                        bool present = true;

                        expand_value_of(reference, scratch, address_of present,
                                        null);

                        if (!present)
                        {
                                if (shell_options & ((positive)1 << ('u' - 'a')))
                                        expand_unbound(reference, false);

                                return close + 1;
                        }

                        expand_bad_substitution(whole, close);
                        expand_fatal_status(shell_bash_compat
                                                ? expand_nounset_status(false)
                                                : 2);
                        return close + 1;
                }
        }

        /*
                What the whole array answers to.

                ${#a[@]} is a count and not a length, ${!a[@]} is the
                subscripts, ${a[@]:1:2} selects elements, and every byte
                operator is that operator applied to each element in turn.
                What is left -- no operator, or one of the four that ask
                whether a parameter is set -- reads an array as set when it
                has any element at all, which is how `a=()` comes out unset.
        */
        if (array_form)
        {
                //      For the four set tests the flag says colon.
                expand_array_form(plain_name, plain_length, array_form,
                                  want_length, operation,
                                  operation == '-' || operation == '+' ||
                                          operation == '=' || operation == '?'
                                      ? colon
                                      : doubled,
                                  word, quoted, parameter_mode, mark);

                return close + 1;
        }

        if (name_list)
        {
                expand_push_names(name, length, name_list, quoted);
                return close + 1;
        }

        if (parameter_mode & EXPAND_PARAMETER_INDIRECT)
        {
                string_address source = name;
                positive target_length;
                bool present;

                //      ${!ref} of a nameref is the name it refers to, in
                //      bash, and not the value of the variable it holds.
                //      One that refers to nothing is no indirection at all.
                if (shell_bash_compat && !reference.key &&
                    (shell_variable_attributes(source, length) &
                     SHELL_ARRAY_NAMEREF))
                {
                        string_address target =
                            shell_nameref_target(source, length);

                        if (!target)
                        {
                                expand_where();
                                string_format(writer_stderr_once,
                                              "%s: invalid indirect expansion\n",
                                              expand_reference_text(reference));
                                expand_indirect_error();
                                return close + 1;
                        }
                        if (!operation && !want_length)
                        {
                                expand_push_string(target, mark);
                                return close + 1;
                        }
                }

                name = expand_value_of(reference, indirect_scratch,
                                       address_of present,
                                       address_of target_length);

                // An absent positional parameter is itself an unset indirect
                // value; an absent ordinary variable is an invalid expansion.
                if (!present)
                {
                        p8 first = string_get(source);

                        if (first < '0' || first > '9')
                        {
                                expand_where();
                                string_format(writer_stderr_once,
                                              "%s: invalid indirect expansion\n",
                                              expand_reference_text(reference));
                                expand_indirect_error();
                                return close + 1;
                        }

                        parameter_mode |= EXPAND_PARAMETER_MISSING;
                        name = source;
                        target_length = length;
                }
                else if (!target_length &&
                         ((string_is(source, '@') || string_is(source, '*')) &&
                          !string_get(source + 1) && !shell_parameter_count))
                {
                        parameter_mode |= EXPAND_PARAMETER_MISSING;
                        name = source;
                        target_length = length;
                }
                else if (!reference.key && !array_form &&
                         expand_indirect_element(name, target_length))
                {
                        /* ${!r} with r='a[1]' or 'a[@]' is ${a[1]} or
                           ${a[@]}, whatever operator follows: the value is
                           a parameter with its subscript, read as one. */
                        positive rest = (positive)(close - (name_start + length));
                        p8 address_to built = shell_store_take(
                            address_of expand_store, target_length + rest + 4);

                        if (!built)
                        {
                                expand_fail_state();
                                return close + 1;
                        }
                        built[0] = '$';
                        built[1] = '{';
                        memory_copy(built + 2, name, target_length);
                        memory_copy(built + 2 + target_length,
                                    name_start + length, rest);
                        built[2 + target_length + rest] = '}';
                        built[3 + target_length + rest] = end;
                        //      Reached this way and quoted, bash asks the
                        //      list's own set test, not the join:
                        //      "${!r:-x}" with r='a[@]' and a=("") stays
                        //      empty, where unquoted it is x.
                        expand_list_null_direct = false;
                        expand_braced_body(built, built + 2 + target_length + rest,
                                           quoted);
                        expand_list_null_direct = true;
                        return close + 1;
                }
                else if (!expand_parameter_name(name, target_length))
                {
                        expand_where();
                        string_format(writer_stderr_once,
                                      "%s: invalid variable name\n", name);
                        expand_indirect_error();
                        return close + 1;
                }

                if (!(parameter_mode & EXPAND_PARAMETER_MISSING))
                {
                        // The operator word may assign the source variable
                        // before the target is read or written. Keep the
                        // resolved name outside mutable environment storage.
                        name = expand_hold(name, target_length, name_local,
                                           sizeof(name_local));

                        if (!name)
                                return close + 1;
                }

                length = target_length;
                reference = (expand_reference){.name = name};
        }

        if (want_length)
        {
                p8 written[32];
                p8 scratch[32];
                bool present = true;
                positive count = 0;

                // Bash ${#*} and ${#@} are how many parameters there are.
                // Dash uses the character length of the joined "$*" string,
                // including concatenation when IFS is empty; bash --posix
                // keeps the count. Nounset with no positionals is 0 in both.
                if (shell_bash_compat && length == 1 &&
                    (string_is(name, '@') || string_is(name, '*')))
                        count = shell_parameter_count;
                else
                {
                        string_address value = expand_value_of(
                            reference, scratch, address_of present, address_of count);
                        if (present && count)
                                count = expand_character_count(value, count);
                }

                if (!present)
                {
                        if (shell_options & ((positive)1 << ('u' - 'a')))
                        {
                                expand_unbound(reference,
                                               parameter_mode &
                                                   EXPAND_PARAMETER_INDIRECT);
                                return close + 1;
                        }

                        count = 0;
                }

                expand_push_run(written,
                                bipolar_into_string(written, (bipolar)count),
                                mark);

                return close + 1;
        }

        if (operation == '#' || operation == '%' || operation == '/' ||
            operation == ':' || operation == '^' || operation == ',' ||
            operation == '~' ||
            operation == '@')
        {
                expand_modifier(reference, operation, doubled, word, quoted,
                                parameter_mode);
                return close + 1;
        }

        {
                p8 scratch[32];
                bool present;
                string_address value = (parameter_mode & EXPAND_PARAMETER_MISSING)
                                           ? null
                                           : expand_value_of(reference, scratch,
                                                             address_of present,
                                                             null);

                if (parameter_mode & EXPAND_PARAMETER_MISSING)
                        present = false;
                /* Bash treats empty $@ / $* as unset for - + = ?, so
                   ${@-none} is none. Dash keeps them set-but-empty. */
                if (present && shell_bash_compat && !shell_parameter_count &&
                    length == 1 &&
                    (string_is(name, '@') || string_is(name, '*')))
                        present = false;
                bool blank = present && value[0] == end;

                //      "$@" and "$*" are null by their join, as arrays are.
                if (colon && present && shell_bash_compat && length == 1 &&
                    (string_is(name, '@') || string_is(name, '*')))
                {
                        bool every_empty = true;

                        for (positive at = 0; at < shell_parameter_count &&
                                              every_empty; at++)
                                every_empty = !string_get(shell_parameter[at]);
                        blank = expand_list_null(shell_parameter_count,
                                                 every_empty,
                                                 string_get(name), quoted);
                }
                bool missing = !present || (colon && blank);

                if (operation == '-')
                {
                        if (missing)
                                expand_word_into(word, quoted);
                        else
                                expand_push_parameter(reference, quoted);

                        return close + 1;
                }

                if (operation == '=')
                {
                        if (missing)
                        {
                                string_address made;

                                if ((parameter_mode & EXPAND_PARAMETER_INDIRECT) &&
                                    !expand_assignable_name(name))
                                {
                                        expand_where();
                                        string_format(
                                            writer_stderr_once,
                                            "%s: invalid indirect expansion\n",
                                            name);
                                        expand_indirect_error();
                                        return close + 1;
                                }

                                made = expand_capture(word, quoted,
                                                      EXPAND_CAPTURE_WORD);

                                if (expand_failed)
                                        return close + 1;

                                if (!expand_assign_named(reference, made))
                                {
                                        expand_assign_refused(reference);
                                        expand_fatal_status(2);
                                        return close + 1;
                                }
                                expand_push_string(made, mark);
                        }
                        else
                                expand_push_parameter_as(reference, quoted,
                                                         parameter_mode);

                        return close + 1;
                }

                if (operation == '+')
                {
                        if (!missing)
                                expand_word_into(word, quoted);
                        /* "${@:+y}" with no parameters is no word in bash,
                           the way "$@" is. */
                        else if (quoted && shell_bash_compat &&
                                 string_is(name, '@') && !string_get(name + 1))
                                expand_name_at_empty = true;

                        return close + 1;
                }

                if (operation == '?')
                {
                        if (missing)
                        {
                                string_address said;

                                said = expand_capture(word, quoted,
                                                      EXPAND_CAPTURE_WORD);

                                if (expand_failed)
                                        return close + 1;

                                shell_diagnostic_where_to(
                                    writer_stderr_once);
                                string_format(writer_stderr_once, "%s: %s\n",
                                              expand_reference_text(reference),
                                              said[0] ? said
                                                      : expand_unset_reason(
                                                            colon));
                                expand_fatal_status(expand_nounset_status(
                                    parameter_mode & EXPAND_PARAMETER_INDIRECT));

                                return close + 1;
                        }

                        expand_push_parameter_as(reference, quoted, parameter_mode);

                        return close + 1;
                }

                expand_push_parameter_as(reference, quoted, parameter_mode);
        }

        return close + 1;
}

static HOT string_address expand_simple(string_address step, bool quoted)
{
        string_address start;
        p8 name_local[EXPAND_LOCAL_NAME];
        string_address name;
        positive length = 0;
        positive2 named;
        p8 seen;

        step++;
        start = step;
        seen = string_get(step);

        if (seen == '?' || seen == '#' || seen == '$' || seen == '!' ||
            seen == '-' || seen == '@' || seen == '*' ||
            byte_is_digit(seen))
        {
                p8 special[2];

                special[0] = seen;
                special[1] = end;
                expand_push_parameter((expand_reference){.name = special}, quoted);

                return step + 1;
        }

        named = expand_name_hash(step);
        length = named.y;
        step += length;

        // A dollar in front of nothing that could be a name is a dollar.
        if (!length)
        {
                expand_push('$', MARK_PLAIN);
                return step;
        }

        /* A scalar the table holds is the value's bytes, found by the span
           in the word. Anything else -- unset, an array, a dynamic name,
           Bash's deferred PIPESTATUS -- is asked by name below. */
        if (!shell_bash_compat || !memory_is_word(start, length, "PIPESTATUS"))
        {
                positive value_length;
                string_address value = env_get_hashed_span(
                    start, length, named.x, address_of value_length);

                if (value)
                {
                        expand_push_run(value, value_length,
                                        quoted ? MARK_QUOTED : MARK_FIELD);
                        return step;
                }
        }

        name = expand_hold(start, length, name_local,
                           sizeof(name_local));

        if (!name)
                return step;
        expand_push_parameter((expand_reference){.name = name}, quoted);

        return step;
}

/*
        POSIX.1-2024 dollar-single-quotes.

        Every byte produced here is quoted: blanks do not split and pattern
        characters do not glob.  C strings cannot carry a null byte.  POSIX
        explicitly permits discarding a null escape and the remaining bytes
        through the closing quote, which is the policy used here; an adjacent
        piece after the quote is still expanded normally.
*/
static string_address expand_dollar_single(string_address step)
{
        positive begun = expand_length;
        expand_quoted_seen = true;
        string_address at = expand_ansi(step + 2, MARK_QUOTED, true);

        if (expand_length == begun)
                expand_push_empty();

        return string_get(at) ? at + 1 : at;
}

static HOT string_address expand_dollar(string_address step, bool quoted)
{
        p8 next = string_get(step + 1);
        string_address result;

        // Deep enough. Something is expanding itself and the stack is the only
        // thing that would notice: as deep as the stack holds (lex.c).
        if (expand_depth >= EXPAND_DEPTH ||
            (expand_depth >= EXPAND_DEPTH_ASKED && !shell_stack_within(50)))
        {
                expand_where();
                string_format(writer_stderr_once, "Expansion nested too deeply\n");
                expand_fatal_status(2);
                return step + 1;
        }

        expand_depth++;

        if (next == '\'')
                result = expand_dollar_single(step);
        else if (next == '(' && string_get(step + 2) == '(')
                result = expand_arithmetic(step, quoted);
        else if (next == '(')
                result = expand_command(step, quoted);
        else if (next == '{')
                result = expand_braced(step, quoted);
        else if (next == '[' && shell_bash_compat)
                result = expand_arithmetic_legacy(step, quoted);
        else
                result = expand_simple(step, quoted);

        expand_depth--;

        return result;
}

/*
        One dollar expansion in a here-document.

        A here body does not remove quotes or split fields, so it cannot use
        shell_expand_word. The dollar grammar itself is exactly the quoted
        grammar above, including command/arithmetic expansion and rejection of
        unsupported braced operators. Its bytes are copied into the command's
        existing token arena, where the literal runs around it already live.
*/
RETURNS_NONNULL string_address shell_expand_document_part(string_address step,
                                        string_address address_to text,
                                        positive address_to length,
                                        bool address_to overflow)
{
        string_address result;

        expand_begin();

        // Dollar-single-quotes have no quoting role in a here-document body;
        // like a single quote there, the bytes are literal.  Return only the
        // dollar and let the here-body walker copy the following quote/run.
        if (string_is(step, '`'))
                result = expand_backtick(step, true,
                                         !shell_bash_compat || shell_posix_on());
        else if (string_is(step + 1, '\''))
        {
                expand_push('$', MARK_QUOTED);
                result = step + 1;
        }
        else
                result = expand_dollar(step, true);

        if (!expand_failed)
                shell_scratch_bytes(expand_length);

        expand_drop_empty();
        address_to text = expand_text;
        address_to length = expand_failed ? 0 : expand_length;
        address_to overflow = expand_overflow;

        return result;
}

static string_address expand_double(string_address step)
{
        positive begun = expand_length;
        bool at_before = expand_name_at_empty;

        expand_quoted_seen = true;
        expand_name_at_empty = false;

        if (string_is(step + 1, '"'))
                expand_explicit_empty = true;

        step++;

        while (!expand_failed && string_get(step) && string_not(step, '"'))
        {
                positive run = string_span(step, expand_inside_set);
                p8 seen;

                if (run)
                {
                        expand_push_run(step, run, MARK_QUOTED);
                        step += run;
                        continue;
                }

                seen = string_get(step);

                if (seen == '\\')
                {
                        p8 next = string_get(step + 1);

                        if (expand_double_bare && next)
                        {
                                expand_push(next, MARK_QUOTED);
                                step += 2;
                                continue;
                        }

                        // Only these four are hidden by a backslash in double
                        // quotes. In front of anything else it is a backslash.
                        if (next == '"' || next == '\\' || next == '$' || next == '`')
                        {
                                expand_push(next, MARK_QUOTED);
                                step += 2;
                                continue;
                        }

                        expand_push(seen, MARK_QUOTED);
                        step++;
                        continue;
                }

                if (seen == '$')
                {
                        // An enclosing double quote makes $'...' literal.
                        // This guard lives here, rather than in expand_dollar,
                        // because a ${...word...} can carry quoted output while
                        // dollar-single-quotes in word still retain syntax.
                        if (string_is(step + 1, '\''))
                        {
                                expand_push('$', MARK_QUOTED);
                                step++;
                                continue;
                        }

                        step = expand_dollar(step, true);
                        continue;
                }

                if (seen == '`')
                {
                        step = expand_backtick(step, true, true);
                        continue;
                }

                expand_push(seen, MARK_QUOTED);
                step++;
        }

        /* A quoted run that came out empty with no empty "$@" in it is an
           empty argument the way "" is: bash keeps "$*""$@" as one word and
           drops "$*$@". */
        if (expand_length == begun && !expand_name_at_empty &&
            shell_bash_compat)
                expand_explicit_empty = true;
        expand_name_at_empty = expand_name_at_empty || at_before;

        if (expand_length == begun)
                expand_push_empty();

        if (string_get(step))
                step++;

        return step;
}

/*
        A word, expanded in place.

        plain is what a byte nothing quoted comes out as. In the word the
        lexer handed over it is PLAIN, because the lexer has already put every
        separator between words; in the tail of ${x-a b} it is FIELD, because
        those bytes are the result of an expansion and an unquoted expansion
        splits -- ${nosuch-D E} used to be one field holding a space.
*/
static string_address expand_tilde(string_address step, bool assignment);

static HOT fn expand_into(string_address text, bool quoted, p8 plain,
                      bool assignment)
{
        string_address step = text;
        bool tilde = assignment;

        while (likely(!expand_failed) && string_get(step))
        {
                positive run = string_span(step, quoted ? expand_inside_set
                                                        : expand_plain_set);
                p8 seen;

                if (assignment && !quoted)
                {
                        positive at = 0;

                        while (at < run)
                        {
                                if (string_is(step + at, '~') &&
                                    (!at ? tilde : string_is(step + at - 1, ':')))
                                        break;

                                at++;
                        }

                        run = at;
                }

                if (run)
                {
                        expand_push_run(step, run, quoted ? MARK_QUOTED : plain);
                        tilde = assignment && string_is(step + run - 1, ':');
                        step += run;
                        continue;
                }

                seen = string_get(step);

                if (assignment && !quoted && tilde && seen == '~')
                {
                        string_address after = expand_tilde(step, true);

                        if (after != step)
                        {
                                step = after;
                                tilde = false;
                                continue;
                        }
                }

                tilde = false;

                if (seen == '\\' && string_get(step + 1))
                {
                        p8 next = string_get(step + 1);

                        /* Double-quote rules when this text is already
                           quoted: only ", \, $ and ` hide the backslash.
                           PS4 and a quoted ${...} word still need `\n`
                           intact so prompt escapes can see it. */
                        if (quoted)
                        {
                                if (next == '"' || next == '\\' ||
                                    next == '$' || next == '`' ||
                                    (next == '}' && expand_brace_word))
                                {
                                        expand_push(next, MARK_QUOTED);
                                        step += 2;
                                        continue;
                                }

                                expand_push(seen, MARK_QUOTED);
                                step++;
                                continue;
                        }

                        step++;
                        expand_push(string_get(step++), MARK_QUOTED);
                        continue;
                }

                if (seen == '\'' && !quoted)
                {
                        string_address stop;

                        expand_quoted_seen = true;
                        step++;
                        stop = string_first_of_or_end(step, '\'');

                        if (stop == step)
                        {
                                expand_explicit_empty = true;
                                expand_push_empty();
                        }
                        else
                                expand_push_run(step, (positive)(stop - step),
                                                MARK_QUOTED);

                        step = stop;

                        if (string_get(step))
                                step++;

                        continue;
                }

                if (seen == '"')
                {
                        bool held_bare = expand_double_bare;

                        expand_double_bare = quoted && expand_brace_word;
                        step = expand_double(step);
                        expand_double_bare = held_bare;
                        continue;
                }

                if (seen == '$')
                {
                        /*
                                $"..." asks for the string in the caller's
                                language, and there is one language here, so
                                the answer is the string itself -- which is
                                what Bash answers too when its catalogue has
                                no entry for the string.

                                Asked where a quote could begin, exactly as
                                $'...' is. Inside a double quote the dollar is
                                an ordinary byte and the quote behind it is
                                the one that closes the run, so "$" must not
                                open another.
                        */
                        if (!quoted && !shell_dash_compat &&
                            string_is(step + 1, '"'))
                                step = expand_double(step + 1);
                        else
                                step = expand_dollar(step, quoted);

                        continue;
                }

                if (seen == '`')
                {
                        step = expand_backtick(step, quoted, false);
                        continue;
                }

                //      <(command) and >(command), which are a word in a
                //      double quote and a path outside one.
                if (!quoted && (seen == '<' || seen == '>') &&
                    string_is(step + 1, '('))
                {
                        step = expand_process(step, plain);
                        continue;
                }

                expand_push(seen, quoted ? MARK_QUOTED : plain);
                step++;
        }
}

/*
        The home-directory column of a passwd record. A missing file, a
        missing user, or a record that never reached that column is not a
        home directory: the tilde word stays literal.
*/
static bool expand_tilde_account(string_address name, positive length,
                                 string_address address_to home,
                                 positive address_to home_length)
{
        p8 address_to text = file_account_text(FILE_ACCOUNT_USER);
        positive at = 0;
        file_account_record record;

        while (file_account_next(text, address_of at, 5, address_of record))
        {
                if (!record.has_value || record.name_length != length ||
                    memory_compare(record.name, name, length))
                        continue;

                address_to home = record.value;
                address_to home_length = record.value_length;
                return true;
        }

        return false;
}

static bool expand_tilde_self(string_address address_to home,
                              positive address_to home_length)
{
        p8 name[FILE_NAME_MAX];
        positive uid = (positive)getuid();

        if (!file_account_name(file_account_text(FILE_ACCOUNT_USER), uid, 2,
                               name, sizeof(name)))
                return false;

        return expand_tilde_account(name, string_length(name), home,
                                    home_length);
}

/* HOME and a passwd directory are quoted so they do not split. An empty
   one is still a field in bash, the way "" is, and no field in dash. */
static fn expand_tilde_push(string_address home, positive home_length)
{
        if (home_length)
                expand_push_run(home, home_length, MARK_QUOTED);
        else if (shell_bash_compat)
                expand_quoted_seen = true;
}

/*
        A tilde, and only at the very front of a word.

        An empty login is HOME. Bash, HOME unset, takes this uid's directory
        from the password file; dash leaves the tilde alone. A name is that
        file's home column for the login. Bash also spells the working
        directory ~+ and the previous one ~-. No splitting afterwards: a
        home directory with a space in it is still one directory.
*/
static string_address expand_tilde(string_address step, bool assignment)
{
        string_address name = step + 1;
        string_address at = name;
        string_address home;
        positive home_length;
        positive length;

        while (string_get(at) && string_not(at, '/') &&
               !(assignment && string_is(at, ':')))
                at++;

        length = (positive)(at - name);

        if (!length)
        {
                home = env_get((const_string) "HOME");

                if (home)
                {
                        expand_tilde_push(home, string_length(home));
                        return name;
                }

                if (!shell_bash_compat ||
                    !expand_tilde_self(address_of home, address_of home_length))
                        return step;

                expand_tilde_push(home, home_length);
                return name;
        }

        if (shell_bash_compat && length == 1 &&
            (string_is(name, '+') || string_is(name, '-')))
        {
                home = env_get(string_is(name, '+') ? (const_string) "PWD"
                                                    : (const_string) "OLDPWD");
                if (!home)
                        return step;

                expand_tilde_push(home, string_length(home));
                return at;
        }

        if (!expand_tilde_account(name, length, address_of home,
                                  address_of home_length))
                return step;

        expand_tilde_push(home, home_length);
        return at;
}

/* Here bodies and startup filenames share expansion without quote removal,
   field splitting or globbing. Emit literal runs straight to the caller's
   existing store; a large here body must not acquire a second whole-body
   expansion/mark buffer just to share its dollar/backtick grammar. */
static bool shell_expand_document(writer write, string_address body,
                                   positive length, bool startup)
{
        static b8 plain[STRING_SET_BYTES];
        static bool ready;
        positive at = 0;

        if (!ready)
        {
                memory_fill(plain + 1, 1, STRING_SET_BYTES - 1);
                plain['\\'] = plain['$'] = plain['`'] = 0;
                ready = true;
        }

        if (startup && length && body[0] == '~')
        {
                expand_begin();
                at = (positive)(expand_tilde(body, false) - body);
                if (expand_length)
                        write(expand_text, expand_length);
                if (expand_failed || expand_overflow)
                        return false;
        }

        // A literal body never needs the expansion/mark stores initialized.
        // Each real dollar/backtick piece initializes its own scratch below.
        while (at < length)
        {
                positive run = string_span_max(body + at, length - at, plain);
                p8 value = body[at];

                if (run)
                {
                        write(body + at, run);
                        at += run;
                        continue;
                }

                if (value == '\\' && at + 1 < length)
                {
                        p8 next = body[at + 1];
                        if (next == '\n')
                        {
                                at += 2;
                                continue;
                        }
                        if (next == '$' || next == '`' || next == '\\')
                        {
                                write(body + at + 1, 1);
                                at += 2;
                                continue;
                        }
                }

                if (value == '$' || value == '`')
                {
                        string_address text;
                        positive filled;
                        bool overflow;
                        at = (positive)(shell_expand_document_part(
                            body + at, address_of text, address_of filled,
                            address_of overflow) - body);
                        if (expand_failed || overflow)
                                return false;
                        write(text, filled);
                        continue;
                }

                write(body + at++, 1);
        }

        return true;
}

static HOT fn expand_word(string_address word)
{
        string_address step = word;

        expand_begin();

        if (string_is(word, '~'))
                step = expand_tilde(word, false);
        //      Outside posix mode bash takes an argument spelled like an
        //      assignment, name=value, as one for its tildes: echo x=~ and
        //      PATH=a:~/bin as a word to a command both expand.
        else if (shell_bash_compat && expand_assignable_name(word))
        {
                positive name = string_span(word, string_set_name);

                if (string_is(word + name, '=') &&
                    string_first_of(word + name, '~') && !shell_posix_on() &&
                    !expand_list_element)
                {
                        expand_push_run(word, name + 1, MARK_PLAIN);
                        expand_into(word + name + 1, false, MARK_PLAIN, true);
                        step = null;
                }
        }

        if (step)
                expand_into(step, false, MARK_PLAIN, false);

        /*
                A word that is only "$@" and has no parameters behind it is no
                word: f "$@" hands a function nothing, where it used to hand
                it one empty argument and $# came back 1. The quotes are what
                make "" an empty argument everywhere else, and they still do
                -- "a$@b" and "$nosuch$@" are both an empty field here as they
                are in dash. This is the one shape that disappears.
        */
        if (expand_length == expand_empty_count &&
            ((!shell_parameter_count &&
              (string_equals(word, "\"$@\"") ||
               string_equals(word, "\"${@}\""))) ||
             (expand_name_at_empty && !expand_explicit_empty)))
        {
                expand_quoted_seen = false;
                expand_length = 0;
                expand_empty_count = 0;
        }
}

static HOT bool expand_word_ready(string_address word)
{
        expand_word(word);

        // An interactive fatal marks the word failed and returns so the
        // reader can recover on its next line. Do not mistake that empty,
        // aborted result for an allocation failure while emitting fields.
        if (expand_failed)
                return false;

        /* Observe the completed word once, rather than adding bookkeeping to
           every byte/run appended by the expansion hot loop. */
        shell_scratch_bytes(expand_length);

        if (!expand_overflow)
                return true;

        expand_fatal_status(string_report(writer_stderr_once, 2, "Expansion too long: %s\n", word));
        return false;
}

//      A directory may hold any number of names, so the answer to a pattern
//      is not allowed a ceiling either. The table of matches may move; the
//      names themselves may not, since the table points at them.
static string_address address_to glob_result;
static positive glob_room;
static string_address address_to expand_sort_room;
static positive expand_sort_room_count;
static shell_store glob_store;
static positive glob_count;
static bool glob_failed;

static PURE bool glob_magic(string_address pattern)
{
        while (string_get(pattern))
        {
                if (string_is(pattern, '\\') && string_get(pattern + 1))
                {
                        pattern += 2;
                        continue;
                }

                if (string_is(pattern, '*') || string_is(pattern, '?') ||
                    string_is(pattern, '['))
                        return true;

                //      + @ and ! are ordinary bytes on their own and the head
                //      of a group in front of a parenthesis, which is a
                //      pattern to look on disk with like any other.
                if (shell_extglob_on && string_is(pattern + 1, '(') &&
                    lex_extended_head(string_get(pattern)))
                        return true;

                pattern++;
        }

        return false;
}

static bool glob_add(string_address path)
{
        positive length = string_length(path) + 1;
        p8 address_to bytes;

        if (!shell_array_room(glob_result, glob_room, glob_count + 1))
        {
                glob_failed = true;
                return false;
        }

        bytes = shell_store_take(address_of glob_store, length);

        if (!bytes)
        {
                glob_failed = true;
                return false;
        }

        memory_copy(bytes, path, length);
        glob_result[glob_count++] = bytes;

        return true;
}

static bool glob_exists(string_address path)
{
        p8 facts[256];

        return system_stat_at(AT_FDCWD, path, 0x100, 0, facts) == 0;
}

/*
        One pattern against the filesystem, a component at a time.

        A component with nothing magic in it is joined on without a look; one
        that has is read out of the directory above it. Only paths that are
        really there come back, which is what makes a pattern that matches
        nothing stay a pattern.
*/
/*
        GLOBIGNORE, bash's list of patterns a pathname expansion leaves out.

        While it is set and not empty, a leading dot needs no dot in the
        pattern, as under dotglob; . and .. are never answers; and each name
        the walk found is dropped when a pattern of the list matches it whole,
        a component at a time so that a * does not reach across a slash.
        What is left empty is no match, so the word stays as it was written.
*/
static string_address glob_ignoring;

static bool glob_ignore_component(string_address pattern, positive pattern_length,
                                  string_address name, positive name_length)
{
        p8 one[GLOB_PATH];
        p8 two[GLOB_PATH];

        if (pattern_length >= GLOB_PATH || name_length >= GLOB_PATH)
                return false;
        memory_copy_end(one, pattern, pattern_length);
        memory_copy_end(two, name, name_length);
        return shell_match(one, two);
}

static bool glob_ignored_whole(string_address list, string_address path);

/*
        bash tests each directory on the way to a name as well as the name:
        GLOBIGNORE='d*' leaves d/* standing as written, where '*.md' does not
        reach d/two.md across its slash.
*/
static bool glob_ignored(string_address list, string_address path)
{
        string_address last = string_last_of(path, '/');
        string_address base = last ? last + 1 : path;
        p8 prefix[GLOB_PATH];

        if (string_is(base, '.') &&
            (!string_get(base + 1) ||
             (string_is(base + 1, '.') && !string_get(base + 2))))
                return true;

        for (string_address at = path; last && at <= last; at++)
        {
                positive length = (positive)(at - path);

                if (!string_is(at, '/') || !length || length >= GLOB_PATH)
                        continue;
                memory_copy_end(prefix, path, length);
                if (glob_ignored_whole(list, prefix))
                        return true;
        }
        return glob_ignored_whole(list, path);
}

static bool glob_ignored_whole(string_address list, string_address path)
{
        while (string_get(list))
        {
                //      A colon inside a bracket expression, [[:alpha:]], is
                //      a byte of the pattern and not a separator.
                string_address stop = list;
                positive bracket = 0;

                while (string_get(stop) && (bracket || !string_is(stop, ':')))
                {
                        if (string_is(stop, '\\') && string_get(stop + 1))
                                stop++;
                        else if (string_is(stop, '['))
                                bracket++;
                        else if (string_is(stop, ']') && bracket)
                                bracket--;
                        stop++;
                }
                string_address pattern = list;
                string_address name = path;
                bool matched = stop > list;

                while (matched)
                {
                        string_address pattern_stop =
                            memory_first_of(pattern, '/', (positive)(stop - pattern));
                        string_address name_stop = string_first_of_or_end(name, '/');

                        if (!pattern_stop)
                                pattern_stop = stop;
                        if (!glob_ignore_component(pattern,
                                                   (positive)(pattern_stop - pattern),
                                                   name,
                                                   (positive)(name_stop - name)))
                                matched = false;
                        else if (pattern_stop == stop || !string_get(name_stop))
                        {
                                matched = pattern_stop == stop &&
                                          !string_get(name_stop);
                                break;
                        }
                        else
                        {
                                pattern = pattern_stop + 1;
                                name = name_stop + 1;
                        }
                }
                if (matched)
                        return true;
                list = string_get(stop) ? stop + 1 : stop;
        }
        return false;
}

static positive glob_ignore_filter(string_address list)
{
        positive kept = 0;

        for (positive at = 0; at < glob_count; at++)
                if (!glob_ignored(list, glob_result[at]))
                        glob_result[kept++] = glob_result[at];
        return kept;
}

static fn glob_walk(p8 address_to prefix, positive used, string_address pattern,
                    positive depth, bool from_star)
{
        p8 component[GLOB_PATH];
        positive length = 0;
        string_address rest;
        string_address whole;
        bool dotted = shell_shopt_on(DOTGLOB) || glob_ignoring;
        bool folded = shell_shopt_on(NOCASEGLOB);

        if (depth >= GLOB_DEPTH)
        {
                glob_failed = true;
                return;
        }

        // A run of slashes belongs to the prefix, not to any component.
        while (string_is(pattern, '/'))
        {
                if (used + 1 >= GLOB_PATH)
                {
                        glob_failed = true;
                        return;
                }

                prefix[used++] = '/';

                pattern++;
        }

        if (!string_get(pattern))
        {
                prefix[used] = end;

                if (used && glob_exists(prefix))
                        glob_add(prefix);

                return;
        }

        whole = pattern;

        while (string_get(pattern) && string_not(pattern, '/'))
        {
                if (length + 2 >= GLOB_PATH)
                {
                        glob_failed = true;
                        return;
                }

                if (string_is(pattern, '\\') && string_get(pattern + 1))
                        component[length++] = string_get(pattern++);

                component[length++] = string_get(pattern++);
        }

        component[length] = end;
        rest = pattern;

        bool star = length == 2 && component[0] == '*' && component[1] == '*' &&
                    shell_shopt_on(GLOBSTAR);

        // ** first matches zero directories, then reuses this component at
        // each child directory. Ordinary patterns consume it exactly once.
        if (star && string_get(rest))
                glob_walk(prefix, used, rest + 1, depth + 1, false);

        /*
                A last-component ** includes the directory it is standing
                in, with a trailing slash: `d/**` lists `d/` as well as
                what is under it. Recursion into a child already listed
                that child without a slash (`d/s`); another add made `d/s/`.
        */
        if (star && !string_get(rest) && used && !from_star)
        {
                /*
                        `d/**` has already consumed the slash into the
                        prefix, so another one made `d//`. A directory
                        that does not already end in a slash still wants
                        one: bash lists `d/` as well as what is under it.
                */
                if (prefix[used - 1] != '/')
                {
                        if (used + 1 >= GLOB_PATH)
                        {
                                glob_failed = true;
                                return;
                        }

                        prefix[used] = '/';
                        prefix[used + 1] = end;
                }
                else
                        prefix[used] = end;

                //      The directory was joined on without a look when its
                //      component had nothing magic in it: c/a/** must not
                //      answer c/a/ where there is no c/a.
                if (glob_exists(prefix))
                        glob_add(prefix);
        }

        if (!star && !glob_magic(component))
        {
                positive out = used;
                for (positive at = 0; at < length; at++)
                {
                        if (component[at] == '\\' && at + 1 < length)
                                at++;
                        if (out + 1 >= GLOB_PATH)
                        {
                                glob_failed = true;
                                return;
                        }
                        prefix[out++] = component[at];
                }
                glob_walk(prefix, out, rest, depth + 1, false);
                return;
        }

        p8 block[2048];
        prefix[used] = end;
        bipolar directory = system_open_at(
            AT_FDCWD, used ? prefix : (string_address)".",
            FILE_READ | O_DIRECTORY);
        if (directory < 0)
                return;

        positive have = 0, at = 0;
        bipolar error = 0;
        struct linux_dirent64 address_to entry;
        while (!glob_failed &&
               (entry = file_directory_next(directory, block, sizeof(block),
                                             address_of have, address_of at,
                                             address_of error)))
        {
                string_address named = (string_address)entry->d_name;
                if (shell_bash_compat && shell_shopt_on(GLOBSKIPDOTS) &&
                    file_is_dot(named))
                        continue;
                // Explicit (including escaped) dots are ordinary
                // pattern matches. ** never visits . or .., even
                // with dotglob; that would recurse back into itself.
                if (named[0] == '.' &&
                    (star || component[component[0] == '\\'] != '.') &&
                    (!dotted || file_is_dot(named)))
                        continue;
                if (!star && !shell_match_folded(component, named, folded))
                        continue;

                positive run = string_length_max(named, GLOB_PATH - used);
                positive out = used + run;
                // ** also needs a slash for recursive descent.
                if (out + (star ? 1 : 0) >= GLOB_PATH)
                {
                        glob_failed = true;
                        break;
                }
                memory_copy_apart(prefix + used, named, run);

                // A final matching directory entry needs no second lookup.
                // Keep the old recursion-limit failure for ordinary patterns.
                if (!string_get(rest) && (star || depth + 1 < GLOB_DEPTH))
                {
                        prefix[out] = end;
                        glob_add(prefix);
                        if (!star)
                                continue;
                }

                if (!star)
                {
                        //      A directory GLOBIGNORE names is left out on
                        //      the way down as well: GLOBIGNORE=* leaves
                        //      nothing for */* to find.
                        if (glob_ignoring)
                        {
                                prefix[out] = end;
                                if (glob_ignored(glob_ignoring, prefix))
                                        continue;
                        }
                        glob_walk(prefix, out, rest, depth + 1, false);
                }
                else
                {
                        // Unknown directory types are resolved by
                        // the recursive open; symlinks are not followed.
                        bool link_directory = entry->d_type == 10 &&
                                              string_equals(rest, "/");
                        if (entry->d_type == 4 || entry->d_type == 0 ||
                            link_directory)
                        {
                                prefix[out] = '/';
                                glob_walk(prefix, out + 1,
                                          link_directory ? rest + 1 : whole,
                                          depth + 1, true);
                        }
                }
        }
        if (error < 0)
                glob_failed = true;
        system_close(directory);
}

// POSIX asks for the matches in order, and a script that reads a directory
// twice should be told the same story both times.
static bool expand_sort_names(string_address address_to names, positive count)
{
        if (count < 2)
                return true;

        if (!shell_array_room(expand_sort_room, expand_sort_room_count, count))
                return false;

        string_address address_to source = array_merge_sort(
            names, expand_sort_room, count, string_compare);

        if (source != names)
                memory_copy(names, source, count * sizeof(string_address));

        return true;
}

// Pathname matches are ordered by the locale's collation, bash's and dash's
// both: a b C d E under en_US where C sorts A C E b d. A locale whose
// collation this machine has is asked; any other orders by byte as before.
static b32 expand_collate_order(string_address left, string_address right)
{
        bipolar order = collate_compare((p8 address_to)left, string_length(left),
                                        (p8 address_to)right, string_length(right));

        return order ? (b32)order : (b32)string_compare(left, right);
}

static bool expand_sort_matches(string_address address_to names, positive count)
{
        if (count < 2)
                return true;
        if (!collate_ready())
                return expand_sort_names(names, count);

        if (!shell_array_room(expand_sort_room, expand_sort_room_count, count))
                return false;

        string_address address_to source = array_merge_sort(
            names, expand_sort_room, count, expand_collate_order);

        if (source != names)
                memory_copy(names, source, count * sizeof(string_address));

        return true;
}

static inline INLINE string_address expand_keep_bytes(string_address text,
                                                       positive length)
{
        string_address result = shell_store_copy(address_of expand_store,
                                                 text, length);

        if (!result)
        {
                expand_fail_state();
                return (string_address) "";
        }

        return result;
}

// One field's bytes, kept: the common word has no empty marks in it and is
// one copy, and the rare one is copied a byte at a time around them.
static HOT string_address expand_keep_field(positive at, positive stop)
{
        p8 address_to result;
        positive used = 0;

        if (!expand_empty_count)
                return expand_keep_bytes(expand_text + at, stop - at);

        result = shell_store_take(address_of expand_store, stop - at + 1);

        if (!result)
        {
                expand_fail_state();
                return (string_address) "";
        }

        for (; at < stop; at++)
                if (expand_mark[at] != MARK_EMPTY)
                        result[used++] = expand_text[at];

        result[used] = end;
        return result;
}

/*
        One field, out of the working buffer and into the arena -- and through
        the filesystem on the way if anything unquoted in it was magic.

        This is the last place the marks exist: what leaves here is bytes.
*/
static inline INLINE bool glob_quoted_special(p8 byte)
{
        //      A hyphen and a close bracket are special only inside a bracket
        //      expression, and that is enough: [a\\-c] is the three members a,
        //      hyphen and c to both references, and [a-c] is the range. Losing
        //      the backslash with the rest of the quoting turned the one into
        //      the other, so the set matched b and not the hyphen.
        return byte == '*' || byte == '?' || byte == '[' || byte == '\\' ||
               byte == '-' || byte == ']' || byte == '^' || byte == '!' ||
               (shell_extglob_on && (lex_extended_head(byte) || byte == '(' ||
                                     byte == ')' || byte == '|'));
}

static HOT bool expand_emit(positive at, positive stop, shell_words address_to out)
{
        p8 address_to pattern;
        positive room = 1;
        positive used = 0;
        positive index;
        bool magic = false;

        /*
                A bracket only makes a word a pattern if it closes.

                POSIX is plain that an unterminated '[' is a literal
                character, and nothing here enforced it. So the commonest
                command in any script -- the test builtin, whose other name
                is '[' -- was a pattern, and every single one of them opened
                a directory and read it to the end looking for a file called
                that. A loop written with '[' ran six times slower than the
                identical loop written with 'test': 0.590s against 0.099s
                over three hundred thousand turns, and one and a quarter
                million system calls against thirty three.

                What is allowed to close it comes from the same page. After
                the '[' an optional '!' or '^' does not end it -- and a ']' standing immediately
                after that invert is itself literal -- "[]]" is the bracket
                expression that matches a bracket. So the first ']' that can
                close is the one after that, and the three states below are
                which of those places the scan is standing in.
        */
        positive bracket = 0;

        for (index = at; index < stop; index++)
        {
                if (expand_mark[index] == MARK_EMPTY)
                        continue;

                if (room == positive_max)
                        break;

                room++;

                if (expand_mark[index] == MARK_QUOTED &&
                    glob_quoted_special(expand_text[index]))
                        room++;
        }

        pattern = index == stop
                      ? shell_store_take(address_of expand_store, room)
                      : null;

        if (!pattern)
        {
                expand_fail_state();
                return false;
        }

        for (index = at; index < stop; index++)
        {
                p8 value = expand_text[index];

                if (expand_mark[index] == MARK_EMPTY)
                        continue;

                if (expand_mark[index] == MARK_QUOTED)
                {
                        if (glob_quoted_special(value))
                                pattern[used++] = '\\';
                }
                else if (value == '*' || value == '?' ||
                         (shell_extglob_on && lex_extended_head(value) &&
                          index + 1 < stop && expand_text[index + 1] == '(' &&
                          expand_mark[index + 1] != MARK_QUOTED))
                        magic = true;
                else if (bracket == 0)
                {
                        if (value == '[')
                                bracket = 1;
                }
                else if (bracket == 1 &&
                         (value == '!' || value == '^'))
                        bracket = 2;
                else if (bracket == 3 && value == ']')
                        magic = true;
                else
                        bracket = 3;

                pattern[used++] = value;
        }

        pattern[used] = end;

        // set -f was kept and never asked about, so a script that turned
        // globbing off to hold a pattern still had it read the directory.
        if (magic && !(shell_options & SHELL_NO_GLOB))
        {
                p8 built[GLOB_PATH];

                string_address ignore = shell_bash_compat
                                            ? env_get("GLOBIGNORE") : null;

                glob_count = 0;
                glob_failed = false;
                glob_ignoring = ignore && string_get(ignore) ? ignore : null;
                shell_store_reset(address_of glob_store);
                glob_walk(built, 0, pattern, 0, false);
                glob_ignoring = null;

                if (glob_failed)
                {
                        expand_fail_state();
                        return false;
                }

                if (glob_count && ignore && string_get(ignore))
                        glob_count = glob_ignore_filter(ignore);

                if (glob_count)
                {
                        if (!expand_sort_matches(glob_result, glob_count))
                        {
                                expand_fail_state();
                                return false;
                        }

                        for (index = 0; index < glob_count; index++)
                        {
                                string_address name = glob_result[index];
                                if (shell_shopt_on(GLOBSTAR) && index &&
                                    !string_compare(name, glob_result[index - 1]))
                                        continue;
                                string_address kept = expand_keep_bytes(
                                    name, string_length(name));

                                if (expand_failed || !shell_words_add(out, kept))
                                {
                                        expand_fail_state();
                                        return false;
                                }
                        }

                        return true;
                }

                /*
                        A pattern that matched nothing is the pattern itself,
                        unless the script has said otherwise: nullglob makes
                        it no word at all and failglob makes it an error that
                        the command never runs after.
                */
                if (shell_shopt_on(FAILGLOB))
                {
                        /*
                                Bash fails that command and reads on. Killing
                                the process made `echo after=$?` never run.
                                The prefix is the same one a session compares
                                byte for byte.
                        */
                        expand_where();
                        string_format(writer_stderr_once, "no match: %s\n",
                                      pattern);
                        shell_status = 1;
                        expand_failed = true;
                        //      bash drops the rest of the line with it,
                        //      whatever holds the word -- a for list and a
                        //      compound list too -- and under -e leaves.
                        if (shell_bash_compat && !expand_errors_soft)
                        {
                                if (shell_options & ((positive)1 << ('e' - 'a')))
                                        expand_fatal_status(1);
                                else
                                        exec_expand_input_error();
                        }
                        return false;
                }

                if (shell_shopt_on(NULLGLOB))
                        return true;
        }

        {
                string_address kept = expand_keep_field(at, stop);

                if (expand_failed || !shell_words_add(out, kept))
                {
                        expand_fail_state();
                        return false;
                }
        }

        return true;
}

/*
        Field splitting.

        Only bytes that came out of an unquoted expansion can be a separator, so
        the marks do the whole of the deciding. A run of IFS whitespace is one
        separator; anything else in IFS is a separator on its own, with the
        whitespace around it swallowed.
*/
static HOT positive expand_split(shell_words address_to out)
{
        positive at = 0;
        positive start;

        // A word that expanded to nothing at all is no field, unless some part
        // of it was quoted: "" is an empty argument and $nosuch is no argument.
        if (!expand_length)
        {
                if (expand_quoted_seen)
                        expand_emit(0, 0, out);

                return out->count;
        }

        // A wholly quoted nonempty field can neither split nor glob. Keep
        // it directly instead of constructing an unused escaped pattern.
        if (memory_span_byte(expand_mark, MARK_QUOTED, expand_length) == expand_length)
        {
                string_address kept = expand_keep_bytes(expand_text, expand_length);
                if (expand_failed || !shell_words_add(out, kept))
                        expand_fail_state();
                return out->count;
        }

        expand_ifs_prepare();

        while (at < expand_length && expand_mark[at] == MARK_FIELD &&
               expand_ifs_blank(expand_text[at]))
                at++;

        // Nothing but separators: a word of blanks out of a variable is no
        // word at all.
        if (at >= expand_length)
                return out->count;

        start = at;

        while (at < expand_length)
        {
                if (expand_failed)
                        return out->count;

                if (expand_mark[at] == MARK_BREAK ||
                    expand_mark[at] == MARK_SEPARATE)
                {
                        if (start != at || expand_mark[at] == MARK_BREAK ||
                            !shell_bash_compat)
                                expand_emit(start, at, out);
                        at++;
                        start = at;
                        continue;
                }

                if (expand_mark[at] == MARK_FIELD)
                {
                        positive width = expand_ifs_span(expand_text + at,
                                                         expand_length - at);

                        if (width)
                        {
                                expand_emit(start, at, out);

                                while (at < expand_length &&
                                       expand_mark[at] == MARK_FIELD &&
                                       expand_ifs_blank(expand_text[at]))
                                        at++;

                                if (at < expand_length &&
                                    expand_mark[at] == MARK_FIELD)
                                {
                                        width = expand_ifs_span(
                                            expand_text + at,
                                            expand_length - at);
                                        if (width)
                                        {
                                                at += width;

                                                while (at < expand_length &&
                                                       expand_mark[at] == MARK_FIELD &&
                                                       expand_ifs_blank(
                                                           expand_text[at]))
                                                        at++;
                                        }
                                }

                                start = at;

                                // Separators at the end make no empty field after them.
                                if (at >= expand_length)
                                        return out->count;

                                continue;
                        }
                }

                at++;
        }

        if (!expand_failed &&
            (start != expand_length ||
             expand_mark[expand_length - 1] != MARK_SEPARATE))
                expand_emit(start, expand_length, out);

        return out->count;
}

/*
        A substitution in a brace is held whole by the scanners that find its
        close and its commas, as bash holds it: lex_skip_held steps over
        quotes, $( ), ${ } and backquotes, and a process substitution is the
        same kind of program. Counted, the { , } inside $(echo {a,b}) closed
        or split the brace around it.
*/
static bool expand_brace_skip(string_address address_to at)
{
        string_address step = address_to at;

        if (lex_skip_held(at))
                return true;
        if ((string_is(step, '<') || string_is(step, '>')) &&
            string_is(step + 1, '('))
        {
                string_address stop = lex_nesting(step + 1);

                if (stop && stop > step + 1)
                {
                        address_to at = stop;
                        return true;
                }
        }
        return false;
}

// The } that closes the brace whose body starts at at, or nothing.
static PURE string_address expand_brace_close(string_address at)
{
        positive depth = 1;

        while (string_get(at))
        {
                if (expand_brace_skip(address_of at))
                        continue;
                if (string_is(at, '{'))
                        depth++;
                else if (string_is(at, '}') && !--depth)
                        return at;
                at++;
        }

        return null;
}

static PURE string_address expand_brace_comma(string_address at,
                                         string_address close)
{
        positive depth = 0;

        while (at < close)
        {
                p8 value = string_get(at);

                if (expand_brace_skip(address_of at))
                        continue;

                if (value == '{')
                        depth++;
                else if (value == '}' && depth)
                        depth--;
                else if (!depth && value == ',')
                        return at;

                at++;
        }

        return null;
}

static bool expand_brace_number(string_address text, positive length,
                                bipolar address_to value,
                                positive address_to width,
                                bool address_to padded)
{
        positive at = 0;
        positive magnitude = 0;
        bool minus = false;
        positive limit;

        if (at < length && text[at] == '-')
        {
                minus = true;
                at++;
        }

        if (at >= length)
                return false;

        address_to width = length;
        address_to padded = at + 1 < length && text[at] == '0';
        limit = minus ? (positive)bipolar_max + 1 : (positive)bipolar_max;

        if (!memory_digits_whole(text + at, length - at, limit, address_of magnitude))
                return false;

        address_to value = bipolar_from_magnitude(magnitude, minus);

        return true;
}

static positive expand_brace_number_text(p8 address_to out, bipolar value,
                                         positive width, bool padded)
{
        positive sign = value < 0;
        if (sign)
                out[0] = '-';
        positive magnitude = sign ? (positive)0 - (positive)value : (positive)value;
        return sign + positive_into_padded(out + sign, magnitude,
            width > sign ? width - sign : 0, padded ? '0' : 0);
}

static positive shell_expand_braces(string_address word,
                                    shell_words address_to out, bool split);

static positive shell_expand_without_braces(string_address word,
                                             shell_words address_to out,
                                             bool split)
{
        if (!expand_word_ready(word))
                return out->count;

        if (split)
                return expand_split(out);

        /* A redirect kept whole: no field split and no glob. expand_emit
           would still pathname-expand, and lima bash 5.2.32 --posix does
           not: > .* creates that name even with failglob on, and a glob
           with a slash stays the pattern, so the open fails, even with
           globstar. An unquoted expansion to nothing is still no target,
           while a quoted empty expansion is one empty target. */
        if (expand_length || expand_quoted_seen)
        {
                string_address kept = expand_keep_field(0, expand_length);

                if (expand_failed || !shell_words_add(out, kept))
                        expand_fail_state();
        }

        return out->count;
}

static positive expand_brace_made(string_address word,
                                  string_address open,
                                  string_address close,
                                  string_address middle,
                                  positive middle_length,
                                  shell_words address_to out, bool split)
{
        positive prefix = (positive)(open - word);
        positive suffix = string_length(close + 1);
        p8 address_to made;

        if (prefix > positive_max - middle_length ||
            prefix + middle_length > positive_max - suffix - 1 ||
            !(made = shell_store_take(address_of expand_store,
                                      prefix + middle_length + suffix + 1)))
        {
                expand_fail_state();
                return out->count;
        }

        memory_copy(made, word, prefix);
        memory_copy(made + prefix, middle, middle_length);
        memory_copy_end(made + prefix + middle_length, close + 1, suffix);

        return shell_expand_braces(made, out, split);
}

static bool expand_brace_range(string_address word, string_address open,
                               string_address close, shell_words address_to out,
                               bool split)
{
        string_address first_dots = null;
        string_address second_dots = null;
        string_address at = open + 1;
        positive depth = 0;
        bipolar first_number, last_number, step_number = 0;
        positive first_width = 0, last_width = 0;
        bool first_padded = false, last_padded = false;
        bool numeric;

        while (at + 1 < close)
        {
                if (string_is(at, '{'))
                        depth++;
                else if (string_is(at, '}') && depth)
                        depth--;
                else if (!depth && string_is(at, '.') && string_is(at + 1, '.'))
                {
                        if (!first_dots)
                                first_dots = at;
                        else if (!second_dots)
                                second_dots = at;
                        else
                                return false;

                        at += 2;
                        continue;
                }

                at++;
        }

        if (!first_dots || first_dots == open + 1 || first_dots + 2 == close ||
            (second_dots && second_dots + 2 == close))
                return false;

        numeric = expand_brace_number(open + 1,
                                      (positive)(first_dots - open - 1),
                                      address_of first_number,
                                      address_of first_width,
                                      address_of first_padded) &&
                  expand_brace_number(first_dots + 2,
                                      (positive)((second_dots ? second_dots : close) -
                                                 first_dots - 2),
                                      address_of last_number,
                                      address_of last_width,
                                      address_of last_padded);

        if (second_dots)
        {
                positive ignored_width;
                bool ignored_padded;

                if (!expand_brace_number(second_dots + 2,
                                         (positive)(close - second_dots - 2),
                                         address_of step_number,
                                         address_of ignored_width,
                                         address_of ignored_padded))
                        return false;
        }

        if (numeric)
        {
                bipolar step;
                bipolar current = first_number;
                positive width = first_width > last_width ? first_width : last_width;
                bool padded = first_padded || last_padded;
                p8 address_to made;

                if (step_number == bipolar_min)
                        return false;

                /* The padded field is as wide as the operand was written,
                   and positive_into_padded wants room for the widest
                   machine word besides: {0000000000000000000000000001..2}
                   is a 28-byte item. */
                if (!(made = shell_store_take(address_of expand_store,
                                              1 + (width > 20 ? width : 20))))
                {
                        expand_fail_state();
                        return true;
                }

                step = step_number < 0 ? -step_number : step_number;
                if (!step)
                        step = 1;
                if (first_number > last_number)
                        step = -step;

                while ((step > 0 && current <= last_number) ||
                       (step < 0 && current >= last_number))
                {
                        positive length = expand_brace_number_text(
                            made, current, width, padded);

                        expand_brace_made(word, open, close, made, length, out,
                                          split);

                        if (expand_failed || current == last_number ||
                            (step > 0 && current > bipolar_max - step) ||
                            (step < 0 && current < bipolar_min - step))
                                break;

                        current += step;
                }

                return true;
        }

        if (first_dots == open + 2 &&
            first_dots + 3 == (second_dots ? second_dots : close))
        {
                bipolar first = string_get(open + 1);
                bipolar last = string_get(first_dots + 2);
                bipolar magnitude;
                bipolar step;

                /* Seq braces need two numbers or two letters. {1..a} is
                   neither, so it stays one field; mixed-case letters still
                   walk the ASCII span the way bash does. */
                if (!byte_is_alpha(first) || !byte_is_alpha(last))
                        return false;

                if (step_number == bipolar_min)
                        return false;

                magnitude = step_number < 0 ? -step_number : step_number;
                if (!magnitude)
                        magnitude = 1;
                step = first <= last ? magnitude : -magnitude;

                for (bipolar current = first;
                     step > 0 ? current <= last : current >= last;
                     current += step)
                {
                        p8 made = (p8)current;

                        expand_brace_made(word, open, close, address_of made, 1,
                                          out, split);

                        if (expand_failed || current == last)
                                break;
                }

                return true;
        }

        return false;
}

static HOT positive shell_expand_braces(string_address word,
                                    shell_words address_to out, bool split)
{
        string_address open = word;

        while (string_get(open))
        {
                string_address close;
                string_address comma;
                string_address piece;

                if (string_is(open, '\\') && string_get(open + 1))
                {
                        open += 2;
                        continue;
                }

                if (string_is(open, '$') && string_is(open + 1, '\''))
                {
                        open = expand_dollar_quoted_run(open);
                        continue;
                }

                if (string_is(open, '\'') || string_is(open, '"'))
                {
                        open = expand_quoted_run(open, string_get(open));
                        continue;
                }

                if (string_is(open, '$') && string_is(open + 1, '{'))
                {
                        close = expand_parameter_end(open + 2, false);
                        open = close ? close + 1 : open + 1;
                        continue;
                }

                /*
                        A command, arithmetic or process substitution is a
                        program of its own, and bash expands a brace inside
                        it there, once, when it runs. Taken here, $(cmd
                        {a,b}) became two words, two substitutions and cmd
                        run twice with every side effect it has, `...` and
                        <(...) the same, a brace range inside one a fork per
                        member, and $((1+{1,2})) answered where bash refuses.
                */
                if (string_is(open, '`') ||
                    ((string_is(open, '$') || string_is(open, '<') ||
                      string_is(open, '>')) &&
                     string_is(open + 1, '(')))
                {
                        string_address inner = string_is(open, '`') ? open
                                                                     : open + 1;
                        string_address stop = lex_nesting(inner);

                        if (stop && stop > inner)
                        {
                                open = stop;
                                continue;
                        }
                }

                if (string_not(open, '{') ||
                    !(close = expand_brace_close(open + 1)))
                {
                        open++;
                        continue;
                }

                comma = expand_brace_comma(open + 1, close);

                if (!comma)
                {
                        if (expand_brace_range(word, open, close, out, split))
                                return out->count;

                        open++;
                        continue;
                }

                piece = open + 1;

                while (piece <= close)
                {
                        comma = expand_brace_comma(piece, close);
                        expand_brace_made(word, open, close, piece,
                                          (positive)((comma ? comma : close) - piece),
                                          out, split);
                        if (expand_failed || !comma)
                                return out->count;
                        piece = comma + 1;
                }

                return out->count;
        }

        return shell_expand_without_braces(word, out, split);
}

/*
        A whole word that is exactly $name or "$name".

        Ordinary POSIX names only: no braces, no subscript, no $1/$#/$@.
        Quoted, or unquoted with nothing to split or glob, the environment
        value is already the field -- a copy of its bytes, never the pointer:
        the table rewrites a value in place when the new one fits its cell,
        so `echo $i $((i=7))` and `export i=m $i` saw a later assignment in
        the word they had already expanded. It still skips expand_into. Unquoted empty vanishes; quoted empty is one
        empty field. Unset, arrays that are not their own scalar, IFS split
        and glob stay on the general expander, which is also where nounset
        still diagnoses.
*/
static inline INLINE positive expand_simple_dollar_shape(string_address word,
                                           bool address_to quoted,
                                           positive address_to hash)
{
        positive2 named;
        string_address name;
        positive length;
        p8 first;

        if (string_is(word, '"'))
        {
                if (string_not(word + 1, '$'))
                        return 0;

                name = word + 2;
                address_to quoted = true;
        }
        else if (string_is(word, '$'))
        {
                name = word + 1;
                address_to quoted = false;
        }
        else
                return 0;

        first = string_get(name);
        if (!byte_is_alpha(first) && first != '_')
                return 0;

        /* The field path hashes after the shape is known, out of line: its
           caller is inlined into the executor, where a hash loop of its own
           costs every other command registers. */
        if (hash)
        {
                named = expand_name_hash(name);
                length = named.y;
                address_to hash = named.x;
        }
        else
                length = string_span(name, string_set_name);

        if (address_to quoted)
        {
                if (string_not(name + length, '"') || string_get(name + length + 1))
                        return 0;
        }
        else if (string_get(name + length))
                return 0;

        return length;
}

static inline INLINE bool expand_simple_dollar_word(string_address word,
                                      shell_words address_to out)
{
        bool quoted;
        positive length = expand_simple_dollar_shape(word, address_of quoted,
                                                     null);
        string_address name;
        string_address value;
        positive value_length;
        positive hash;

        if (!length)
                return false;

        name = word + (quoted ? 2 : 1);
        hash = memory_hash_33((address_any)name, length);
        value = env_get_hashed_span(name, length, hash, address_of value_length);

        /* Absent includes nameref-to-element, associative $name and the
           names that are only published when something asks. Those are
           not "unset" yet. */
        if (!value)
                return false;

        if (!quoted)
        {
                string_address ifs;

                if (!value_length)
                        return true;

                ifs = expand_ifs();
                if (string_get(ifs) && string_first_of_set(value, ifs))
                        return false;

                if (!(shell_options & SHELL_NO_GLOB) && glob_magic(value))
                        return false;
        }
        else if (!value_length)
                value = (string_address) "";

        value = expand_keep_bytes(value, value_length);
        if (expand_failed)
                return true;

        if (!shell_words_add(out, value))
        {
                expand_fail_state();
                return true;
        }

        return true;
}

/*
        One lexed word, expanded whole, and the fields it became written out.

        The answer is not one word. $@ makes as many as there are parameters, an
        unquoted variable with a space in it makes two, a glob makes as many as
        the directory holds, and an unset variable on its own makes none at all
        -- which is the difference between "rm $file" deleting one thing and
        deleting the working directory.
*/
HOT positive shell_expand_fields(string_address word, shell_words address_to out)
{
        positive count;

        if (expand_simple_dollar_word(word, out))
                return out->count;

        count = shell_braceexpand_on()
                             ? shell_expand_braces(word, out, true)
                             : shell_expand_without_braces(word, out, true);

        if (expand_overflow)
        {
                expand_fatal_status(string_report(writer_stderr_once, 2, "Expansion too long: %s\n", word));
        }

        return count;
}

/*
        The same word, kept whole.

        A redirection target and the right hand side of an assignment are the
        two places POSIX does not split and does not glob, and this is what they
        are supposed to call.
*/
HOT RETURNS_NONNULL string_address shell_expand_word(string_address word)
{
        string_address result;
        bool quoted;
        positive hash;
        positive length = expand_simple_dollar_shape(word, address_of quoted,
                                                     address_of hash);
        string_address value = null;
        positive value_length = 0;

        /* A word that is only $name or "$name" -- a case subject, a
           redirection target -- is the value's bytes, since nothing here
           splits or globs, and a word with nothing to expand -- /dev/null
           -- is its own; the walk took both through expand_into and the
           expansion buffer. Anything the table does not hold as a scalar
           is the walk's, as in expand_assignment_parameter. */
        if (length &&
            !(shell_bash_compat &&
              memory_is_word(word + (quoted ? 2 : 1), length, "PIPESTATUS")))
                value = env_get_hashed_span(word + (quoted ? 2 : 1), length,
                                            hash, address_of value_length);
        else if (!length)
        {
                expand_sets_prepare();
                value_length = string_span(word, expand_literal_set);
                if (!string_get(word + value_length))
                        value = word;
        }

        if (value)
        {
                expand_begin();
                result = expand_keep_bytes(value, value_length);
        }
        else
        {
                if (!expand_word_ready(word))
                        return (string_address) "";

                expand_drop_empty();
                result = expand_keep_bytes(expand_text, expand_length);
        }

        if (expand_overflow)
        {
                expand_fatal_status(string_report(writer_stderr_once, 2, "Expansion too long: %s\n", word));
                return (string_address) "";
        }

        return result;
}

/* A redirect is a single whole word in POSIX/non-Bash policy. Dash has no
   brace expansion, so that path is enough. Bash --posix still honours the
   braceexpand extra option (on by default, including under --posix) and
   treats several brace fields as an ambiguous redirect, but lima 5.2.32
   --posix does not split or glob the word: failglob, globstar and
   globskipdots apply to pathname expansion of command words, not to the
   name opened here. Bash's default policy additionally splits and
   pathname-expands, and still requires exactly one target. The caller
   supplies its reusable word table so a one-off large glob does not leave
   another retained array here.

   1 is one target, 0 is Bash's ambiguous redirect, -1 is an expansion abort
   already carrying its diagnostic/status. */
static b32 shell_expand_redirect(string_address word,
                                 shell_words address_to fields,
                                 string_address address_to target)
{
        if (!shell_bash_compat)
        {
                address_to target = shell_expand_word(word);
                return expand_failed ? -1 : 1;
        }

        {
                positive count;

                if (shell_posix_on())
                        count = shell_braceexpand_on()
                                    ? shell_expand_braces(word, fields, false)
                                    : shell_expand_without_braces(word, fields,
                                                                  false);
                else
                        count = shell_expand_fields(word, fields);

                if (expand_failed)
                        return -1;
                if (count != 1)
                        return 0;

                address_to target = (address_to fields->word)[0];
                return 1;
        }
}

/*
        A persistent assignment whose whole right-hand side is $((...))
        with nothing left to expand in the body.

        The increment fast path can store `i=$((i + 1))` (and the += / ++
        cousins) into the env cell once. xtrace still needs the digits in
        the word; otherwise the executor skips building `name=<digits>`
        and writing it a second time. A miss still evaluates here so a
        comma or octal is not parsed twice.
*/
static string_address expand_assignment_arithmetic(string_address word,
                                                   positive value_at)
{
        string_address rhs;
        string_address inner;
        string_address stop;
        string_address text;
        string_address ready;
        p8 text_local[EXPAND_LOCAL_TEXT];
        p8 written[32];
        positive length;
        positive digits;
        bipolar value;
        string_address made;
        bool plain;

        if (value_at < 2 || word[value_at - 1] != '=')
                return null;

        rhs = word + value_at;
        if (string_get(rhs) != '$' || string_get(rhs + 1) != '(' ||
            string_get(rhs + 2) != '(')
                return null;

        inner = rhs + 3;

        /* A body with no parenthesis, quote, backslash, dollar or backtick
           ends at its first ), and that one scan is all it needs: the
           bracket walker, the check for something to expand and
           arith_expand_body's own check each walked it again. A group
           takes the walker. */
        stop = inner + string_span_without_set(inner, "()$`\"'\\");
        plain = string_is(stop, ')');
        if (!plain)
        {
                stop = expand_paren_end(inner);
                if (!stop)
                        return null;
        }

        if (string_get(stop + 1) != ')' || string_get(stop + 2))
                return null;

        length = (positive)(stop - inner);
        text = expand_hold(inner, length, text_local, sizeof(text_local));
        if (!text)
                return (string_address) "";

        if (!plain && string_get(text + string_span_without_set(text, "$`\"'\\")))
                return null;

        arith_assign_target = word;
        arith_assign_target_length = value_at - 1;
        ready = text;
        if (expand_failed)
        {
                arith_assign_target = null;
                return (string_address) "";
        }

        value = arith_evaluate(ready);
        arith_assign_target = null;

        if (arith_bad)
        {
                if (!arith_unset)
                {
                        shell_arith_report(writer_stderr_once, null, ready);
                        expand_arithmetic_error();
                }
                return (string_address) "";
        }

        if (arith_assign_stored &&
            !(shell_options & ((positive)1 << ('x' - 'a'))))
                return word;

        digits = bipolar_into_string(written, value);
        made = shell_store_take(address_of expand_store, value_at + digits + 1);
        if (!made)
        {
                expand_fail_state();
                return (string_address) "";
        }

        memory_copy(made, word, value_at);
        memory_copy_end(made + value_at, written, digits);
        return made;
}

/*
        An assignment whose whole right-hand side is $name or "$name".

        An assignment neither splits nor globs, so the answer is the name=
        prefix and the value's bytes, and the quotes change nothing. The
        general walk copied the name out to terminate it, hashed it a second
        time, pushed prefix and value into the expansion buffer with their
        marks and then copied the buffer out again. A name the table does not
        hold as a scalar -- unset, an array, a nameref to an element, a
        dynamic name such as RANDOM that is published only when asked -- and
        Bash's deferred PIPESTATUS are left to that walk, which also owns
        nounset.
*/
static string_address expand_assignment_parameter(string_address word,
                                                  positive value_at)
{
        bool quoted;
        string_address rhs = word + value_at;
        positive hash;
        positive length = expand_simple_dollar_shape(rhs, address_of quoted,
                                                     address_of hash);
        string_address name;
        string_address value;
        positive value_length;
        p8 address_to made;

        if (!length)
                return null;

        name = rhs + (quoted ? 2 : 1);
        if (shell_bash_compat && memory_is_word(name, length, "PIPESTATUS"))
                return null;

        value = env_get_hashed_span(name, length,
                                    hash,
                                    address_of value_length);
        if (!value)
                return null;

        made = shell_store_take(address_of expand_store,
                                value_at + value_length + 1);
        if (!made)
        {
                expand_fail_state();
                return (string_address) "";
        }

        memory_copy_apart(made, word, value_at);
        memory_copy_end(made + value_at, value, value_length);
        return made;
}

/*
        A declaration operand is an assignment even though it follows the
        command name. Keep it whole like a leading assignment, and recognize
        the additional tilde-prefix positions after '=' and unquoted ':'.
*/
RETURNS_NONNULL string_address shell_expand_assignment(string_address word, positive value_at)
{
        string_address result;

        arith_assign_stored = false;
        if (expand_assignment_commit)
        {
                result = expand_assignment_arithmetic(word, value_at);
                if (result)
                        return result;
        }

        expand_begin();

        if ((result = expand_assignment_parameter(word, value_at)))
                return result;

        expand_push_run(word, value_at, MARK_PLAIN);
        {
                bool held = expand_assigning;

                expand_assigning = true;
                expand_into(word + value_at, false, MARK_PLAIN, true);
                expand_assigning = held;
        }

        if (!expand_failed)
                shell_scratch_bytes(expand_length);

        expand_drop_empty();
        result = expand_keep_bytes(expand_text, expand_length);

        if (expand_overflow)
        {
                expand_fatal_status(string_report(writer_stderr_once, 2, "Expansion too long: %s\n", word));
                return (string_address) "";
        }

        return result;
}

/*
        A case pattern, kept whole and with its quote marks translated into
        matcher escapes before those marks disappear.

        Ordinary word expansion deliberately returns only bytes. That made a
        star from "$p" indistinguishable from an unquoted star, and made an
        escaped star active again. The matcher already uses backslash for a
        literal metacharacter, so preserving that distinction needs no second
        pattern language.
*/
static RETURNS_NONNULL string_address shell_expand_quoted(
    string_address word, bool regex)
{
        string_address result;

        if (!expand_word_ready(word))
                return (string_address) "";

        if (!(result = expand_lift(0, regex ? EXPAND_CAPTURE_REGEX
                                            : EXPAND_CAPTURE_PATTERN)))
        {
                expand_overflow = true;
                expand_fatal_status(string_report(writer_stderr_once, 2, "Expansion too long: %s\n", word));
                return (string_address) "";
        }

        return result;
}

RETURNS_NONNULL string_address shell_expand_pattern(string_address word)
{
        positive length;

        /* A pattern with nothing to expand, no quote and no pattern byte --
           the a in case $x in a) -- is its own bytes: the walk and the lift
           each copied them once more to say so. */
        expand_sets_prepare();
        length = string_span(word, expand_literal_set);
        if (!string_get(word + length))
        {
                string_address result;

                expand_begin();
                result = expand_keep_bytes(word, length);
                if (expand_overflow)
                {
                        expand_fatal_status(string_report(writer_stderr_once, 2, "Expansion too long: %s\n", word));
                        return (string_address) "";
                }

                return result;
        }

        return shell_expand_quoted(word, false);
}

// Quoted pieces of a [[ string =~ regex ]] right hand side are literal.
RETURNS_NONNULL string_address shell_expand_regex(string_address word)
{
        return shell_expand_quoted(word, true);
}

/*
        PS4 is expanded the way a double-quoted string is -- parameters,
        command substitutions and arithmetic -- so a script can put a
        counter or a command in the trace prefix. Backslash prompt
        escapes are applied afterwards, where the prefix is written.
*/
/* A prompt's decoded text, expanded as a double-quoted word. Inside a
   running expansion (${x@P}) the capture nests; a prompt on its own
   begins one. */
COLD string_address expand_capture_prompt(string_address text, bool nested)
{
        string_address ready;

        if (!nested)
                expand_begin();
        ready = expand_capture(text, true, EXPAND_CAPTURE_TEXT);
        return ready ? ready : (string_address) "";
}

COLD string_address shell_expand_ps4(string_address text)
{
        string_address ready;

        if (!text)
                return (string_address) "";

        expand_begin();
        ready = expand_capture(text, true, EXPAND_CAPTURE_TEXT);
        return ready ? ready : (string_address) "";
}
