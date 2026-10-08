/*
        The regular expression engine: a graph, built once and walked.

        Expanded into text.c, and through it reached by grep, sed and awk,
        which is why it is a file of its own rather than any one of their
        lower halves. A pattern is compiled to nodes, then matched by a lazy
        DFA over them with the literal and anchor hints below taken first,
        so the common shapes never build a state at all.

        The ceilings are fixed and stated at the top: a pattern that would
        exceed one is refused rather than grown into, so no input decides
        how much memory matching it costs. What a pattern is made of is
        built in scratch that grows with it, to a ceiling a pattern of some
        megabytes is far under, and one that is kept is copied out to a
        block of its own, so that a script of ten thousand expressions is
        ten thousand blocks and not a table that fills.
*/

#define RX_NODE_LIMIT ((positive)1 << 22)
#define RX_SET_LIMIT 65535
#define RX_NODE_FIRST 1024
#define RX_SET_FIRST 16
#define RX_FIXED_MAX 8192
#define RX_LITERAL_MAX 256
#define RX_EXTRA_MAX 32
#define RX_GROUP_MAX 9
#define RX_SLOT_MAX 20
#define RX_UNBOUNDED (-1)
#define RX_PARSE_MAX 8193

enum { RX_BYTE = 1, RX_ANY, RX_SET, RX_BEGIN, RX_END, RX_EDGE,
       RX_CAPTURE, RX_BACKREF, RX_ALT, RX_COUNT };
enum { RX_HAS_BACKREF = 2, RX_BRANCHING = 4,
       RX_FIRST_KNOWN = 8, RX_LAST_KNOWN = 16, RX_ANCHORED = 32,
       RX_LITERAL_PROVES = 64, RX_IGNORE_CASE = 128 };
enum { RX_NO_MATCH, RX_MATCH, RX_COMPLEX };
enum { REGEX_DOT_NEWLINE = 1, REGEX_LINE_ANCHORS = 2, REGEX_BASIC_REPEATS = 4,
       /* A dot and a bracket stand for one character rather than one byte. */
       REGEX_CHARACTERS = 8,
       /* + and ? repeat unescaped, as in the syntax GNU's regex falls back
          to when a program sets none -- Emacs's: \| and \( still need their
          backslash, and there are no intervals, so { is itself. tac -r is
          that program: `\._+` is a dot and one underscore or more. */
       REGEX_PLAIN_REPEATS = 16,
       /* A pattern is refused where regcomp refuses it in POSIX basic
          syntax, with its reason, which regex_failure keeps -- expr and
          csplit say it in regcomp's words: a malformed interval (\{ with
          nothing before it to repeat is a literal brace), a trailing
          backslash, a back reference to a group not yet closed, a bracket
          that is unclosed, names no class or collating element, or runs
          backwards. In extended syntax a repeat with nothing to repeat (at
          the start, or after an anchor) is refused as well, and a ) with no
          group open is itself. [.x.] and [=x=] name x. */
       REGEX_STRICT_INTERVALS = 32,
       /* A repeat with nothing before it, in extended syntax at the start of
          the pattern, of a group or of an alternative, repeats nothing and
          so matches the empty string, as GNU grep's dfa reads it -- *a is
          a, and a lone + matches every line -- and says so, warnings kept
          in regex_warnings, where it once was the character itself. */
       REGEX_LEADING_REPEATS = 64,
       /* A strict bracket reads its names and range ends as UTF-8 characters
          (REGEX_CHARACTERS says the same, and more, for matching). */
       REGEX_UTF8_NAMES = 128,
       REGEX_POLICY_DEFAULT = 5, REGEX_POLICY_TAC = 2 | 16,
       REGEX_POLICY_EXPR = 5 | 32 };
enum { REGEX_FAILED_OTHER = 1, REGEX_FAILED_BRACE, REGEX_FAILED_CONTENT,
       REGEX_FAILED_SIZE, REGEX_FAILED_OPEN, REGEX_FAILED_CLOSE,
       REGEX_FAILED_ESCAPE, REGEX_FAILED_REFERENCE, REGEX_FAILED_BRACKET,
       REGEX_FAILED_CLASS, REGEX_FAILED_PRECEDING, REGEX_FAILED_RANGE,
       REGEX_FAILED_COLLATE };
enum { REGEX_BOUNDARY_NONE, REGEX_BOUNDARY_WORD, REGEX_BOUNDARY_LINE };
enum { REGEX_EDGE_WORD, REGEX_EDGE_NOT_WORD, REGEX_EDGE_START, REGEX_EDGE_STOP };

/* Zero ends a sequence. ALT owns two branch roots; CAPTURE and COUNT own
   a child sequence's first/last nodes. No node is copied for repetition. */
// A node, by its place in the graph: the first is 1, and 0 ends a sequence.
typedef p32 rx_ref;

typedef struct
{
        p8 kind;
        // The byte, the set, the group, the kind of edge.
        p16 argument;
        rx_ref next, previous, left, right;
        b16 minimum, maximum;
} rx_node;

typedef struct
{
        p8 first_skip[256], last_bytes[256], literal[RX_LITERAL_MAX];
        positive literal_length, fixed_length, fixed_work;
        // The next longest string the pattern cannot match without, for the
        // line that has the first and not this one: a.*b on a line of a's.
        p8 extra[RX_EXTRA_MAX];
        positive extra_length;
        // A pattern that begins with a run of one set with no end to it: the
        // node of the set, or none; and whether the end of the text is all
        // that follows the run.
        rx_ref lead_run, lead_next, shape_node;
        b16 lead_minimum, shape_minimum;
        p8 lead_tail, shape;
        // Keeps what follows off the address the walk's stores of its slots
        // alias: without it grep -o and awk match ran a fifth slower on the
        // same instructions, for no reason but where the fields fell.
        p8 apart[64];
        // regex_test's ledger of this program: what its questions cost the
        // graph, and which compile this is.
        positive asks, work, bytes;
        p32 serial;
        p8 verdict;
        positive2 literal_anchors, extra_anchors, fixed_anchors;
        p8 fixed_literal[RX_FIXED_MAX];
} rx_hints;

typedef struct
{
        const rx_node *nodes;
        const p8 (*sets)[256];
        const rx_hints *hints;
        rx_ref first;
        p8 groups, policy, boundary, flags;
} regex_program;

typedef struct
{
        positive nodes, sets;
} rx_mark;

/*
        Where one pattern is made: the nodes and the sets it has so far, the
        one set of hints it fills in, and how far it has got. A pattern is
        made from the start each time, over what the one before it left, and
        a program that is to outlive the next is copied out of it
        (regex_keep).
*/
typedef struct
{
        rx_node *nodes;
        positive node_room;
        p8 (*sets)[256];
        positive set_room;
        rx_hints hints;
        rx_mark used;
} rx_pool;

typedef struct
{
        rx_ref first, last;
} rx_fragment;

typedef struct
{
        rx_pool *pool;
        rx_mark cursor;
        regex_program program;
        string_address pattern;
        positive length, at;
        b32 depth;
        /*
                The five byte sets a character is spelled with: the one-byte
                characters, the three lead ranges and the continuation
                range. Every dot and every negated bracket in one pattern
                shares them, so a pattern full of dots costs them once
                against the sixty-four a pool holds. -1 until first asked.
        */
        b32 wide_ascii, wide_two, wide_three, wide_four, wide_tail;
        bool extended, escapes, broken;
        /* The atom just read is an anchor, which nothing may repeat. */
        bool bare;
        p8 failure;
        p16 closed; // the groups whose close has been read, by number
} rx_compiler;

// The leading repeats the last compile skipped, as the character that began
// each ('{' for an interval), in the order met; only the first eight kept.
static p8 regex_warnings[8];
static positive regex_warning_count;

static void rx_warn_repeat(p8 byte)
{
        if (regex_warning_count < sizeof(regex_warnings))
                regex_warnings[regex_warning_count] = byte;
        regex_warning_count++;
}

// A refusal regcomp makes, kept when it is the first thing wrong.
static fn rx_refuse(rx_compiler *c, p8 failure)
{
        if (!c->broken)
                c->failure = failure;
        c->broken = true;
}

static p8 rx_peek(rx_compiler *c, positive ahead)
{
        return ahead < c->length - c->at ? c->pattern[c->at + ahead] : 0;
}

static bool rx_operator(rx_compiler *c, p8 character)
{
        return c->extended ? rx_peek(c, 0) == character
                           : rx_peek(c, 0) == '\\' && rx_peek(c, 1) == character;
}

// Room for the nodes a pattern has made and one more, the scratch grown for
// it when it has not.
static bool rx_room(rx_compiler *c, positive nodes)
{
        rx_pool *pool = c->pool;

        if (nodes <= pool->node_room)
                return true;

        positive room = pool->node_room ? 2 * pool->node_room : RX_NODE_FIRST;

        while (room < nodes)
                room *= 2;

        if (room > RX_NODE_LIMIT)
                room = RX_NODE_LIMIT;
        if (nodes > room)
                return false;

        rx_node *grown = memory_resize(pool->nodes, room * sizeof(rx_node));

        if (!grown)
                return false;

        pool->nodes = grown;
        pool->node_room = room;
        return true;
}

static rx_ref rx_emit(rx_compiler *c, rx_node node)
{
        if (c->broken || !rx_room(c, c->cursor.nodes + 1))
        {
                c->broken = true;
                return 0;
        }
        rx_ref at = (rx_ref)c->cursor.nodes++;
        c->pool->nodes[at] = node;
        return at;
}

static rx_fragment rx_join(rx_compiler *c, rx_fragment one, rx_fragment two)
{
        if (!one.first)
                return two;
        if (!two.first)
                return one;
        c->pool->nodes[one.last].next = two.first;
        c->pool->nodes[two.first].previous = one.last;
        one.last = two.last;
        return one;
}

static b32 rx_new_set(rx_compiler *c)
{
        rx_pool *pool = c->pool;

        if (c->cursor.sets == pool->set_room)
        {
                positive room = pool->set_room ? 2 * pool->set_room : RX_SET_FIRST;

                if (room > RX_SET_LIMIT)
                        room = RX_SET_LIMIT;

                p8 (*grown)[256] = c->cursor.sets == RX_SET_LIMIT
                                       ? null
                                       : memory_resize(pool->sets, room * 256);

                if (!grown)
                {
                        c->broken = true;
                        return -1;
                }
                pool->sets = grown;
                pool->set_room = room;
        }
        b32 set = (b32)c->cursor.sets++;
        memory_fill(c->pool->sets[set], 0, 256);
        return set;
}

static fn rx_set_add(rx_compiler *c, b32 set, p8 byte)
{
        c->pool->sets[set][byte] = 1;
        if ((c->program.flags & RX_IGNORE_CASE) && byte_is_alpha(byte))
        {
                byte ^= 32;
                c->pool->sets[set][byte] = 1;
        }
}

static rx_fragment rx_one(rx_compiler *c, p8 kind, b32 argument)
{
        rx_ref at = rx_emit(c, (rx_node){.kind = kind, .argument = (p16)argument});

        return (rx_fragment){at, at};
}

static rx_fragment rx_either(rx_compiler *c, rx_fragment one, rx_fragment two)
{
        rx_ref at = rx_emit(c, (rx_node){.kind = RX_ALT, .left = one.first,
                                         .right = two.first});

        return (rx_fragment){at, at};
}

/*
        The small letters that are not the lower case of their own upper
        case, which no walk from the upper case finds: the micro sign, the
        dotless i, the long s, the title case of the four digraphs, the
        final sigma and the Greek letters of another shape, the old-style
        Cyrillic ones and the rest of these. They are what grep's own
        folding names beside a character's upper and lower.
*/
static const p32 rx_case_lonesome[] = {
    0xb5, 0x131, 0x17f, 0x1c5, 0x1c8, 0x1cb, 0x1f2, 0x345, 0x3c2, 0x3d0, 0x3d1, 0x3d5,
    0x3d6, 0x3f0, 0x3f1, 0x3f5, 0x1e9b, 0x1fbe,
};

/*
        A character of more than one byte under ignore-case is the
        alternation of its bytes and those of every character with the same
        upper case, which is how glibc's regular expressions and grep's
        compare them: the upper case and the lower case of it, and the
        lonesome ones above that go up to it. The kelvin sign has the
        Kelvin sign for its upper case and finds only itself, the final
        sigma and the sigma find each other and the capital. Their bytes
        above 0x7f are never changed by the byte compare, and an ASCII one
        is kept lower so that the compare finds it.
*/
static rx_fragment rx_cased(rx_compiler *c, p32 code)
{
        p32 upper = unicode_case(code, UNICODE_CASE_UPPER);
        p32 lower = unicode_case(upper, UNICODE_CASE_LOWER);
        p32 cases[3 + sizeof(rx_case_lonesome) / sizeof(rx_case_lonesome[0])] = {code, upper};
        positive known = 2;
        p32 seen[sizeof(cases) / sizeof(cases[0])];
        rx_fragment all = {0};
        positive count = 0;

        if (unicode_case(lower, UNICODE_CASE_UPPER) == upper)
                cases[known++] = lower;
        for (positive i = 0; i < sizeof(rx_case_lonesome) / sizeof(rx_case_lonesome[0]); i++)
                if (unicode_case(rx_case_lonesome[i], UNICODE_CASE_UPPER) == upper)
                        cases[known++] = rx_case_lonesome[i];

        for (positive i = 0; i < known; i++)
        {
                p32 variant = cases[i] < 0x80 ? byte_to_lower(cases[i]) : cases[i];
                p8 bytes[4];
                bool again = false;
                rx_fragment run = {0};
                positive made;

                for (positive j = 0; j < count; j++)
                        again |= seen[j] == variant;
                if (again)
                        continue;
                seen[count++] = variant;
                made = memory_utf8_encode(bytes, 4, variant);
                for (positive b = 0; b < made; b++)
                        run = rx_join(c, run, rx_one(c, RX_BYTE, bytes[b]));
                all = all.first ? rx_either(c, all, run) : run;
        }
        return all;
}

/* A lead byte and the continuation bytes that follow it, as one sequence. */
static rx_fragment rx_wide_run(rx_compiler *c, b32 lead, b32 tails)
{
        rx_fragment run = rx_one(c, RX_SET, lead);

        for (b32 i = 0; i < tails; i++)
                run = rx_join(c, run, rx_one(c, RX_SET, c->wide_tail));

        return run;
}

/*
        The byte sets a character is spelled with, made once for a pattern.

        A character is one byte below 0x80, or a lead byte and one, two or
        three continuation bytes. Spelling a dot this way rather than
        teaching the walk to count bytes leaves the walk, the machine and a
        counted repeat exactly as they were: every one of them already
        knows what an alternation of byte sets means. Measured, it costs the
        machine nothing -- the classes a byte falls into collapse the four
        lead ranges and the continuation range to one column each.
*/
static bool rx_wide_sets(rx_compiler *c)
{
        if (c->wide_tail >= 0)
                return true;

        b32 tail = rx_new_set(c), two = rx_new_set(c);
        b32 three = rx_new_set(c), four = rx_new_set(c);

        if (tail < 0 || two < 0 || three < 0 || four < 0)
                return false;

        for (b32 i = 0x80; i <= 0xbf; i++)
                c->pool->sets[tail][i] = 1;

        for (b32 i = 0xc2; i <= 0xdf; i++)
                c->pool->sets[two][i] = 1;

        for (b32 i = 0xe0; i <= 0xef; i++)
                c->pool->sets[three][i] = 1;

        for (b32 i = 0xf0; i <= 0xf4; i++)
                c->pool->sets[four][i] = 1;

        c->wide_tail = tail;
        c->wide_two = two;
        c->wide_three = three;
        c->wide_four = four;
        return true;
}

/*
        One character: the bytes `ascii` names, or any sequence of more than
        one byte. A caller hands the set of one-byte characters it will
        take -- everything but a newline for a dot, everything a negated
        bracket did not name -- and gets back the whole of what stands for
        a character there.
*/
static rx_fragment rx_character(rx_compiler *c, b32 ascii)
{
        if (!rx_wide_sets(c))
        {
                c->broken = true;
                return (rx_fragment){0};
        }

        rx_fragment wide = rx_either(c, rx_wide_run(c, c->wide_three, 2),
                                     rx_wide_run(c, c->wide_four, 3));

        wide = rx_either(c, rx_wide_run(c, c->wide_two, 1), wide);

        /*
                The alternation is marked (a branch has no use for its
                minimum), so that the walk can tell this one from any other
                and decide a character that is one byte without a choice.
        */
        rx_fragment character = rx_either(c, rx_one(c, RX_SET, (p8)ascii), wide);

        if (character.first)
                c->pool->nodes[character.first].minimum = 1;
        return character;
}

/*
        What a bracket named besides the bytes it put in its set: whether it
        was negated, and the characters of more than one byte it holds. The
        set is a table of bytes and cannot hold one of those; putting its
        bytes in separately would make [e-acute] match either half of one,
        and match the halves of every other character as well.
*/
#define RX_SET_WIDE_MAX 24

typedef struct
{
        bool negated;
        /* Some member, or some end of a range, was a byte above 0x7f. */
        bool high;
        b32 count;
        positive at[RX_SET_WIDE_MAX];
        p8 size[RX_SET_WIDE_MAX];
} rx_set_facts;

/*
        A strict bracket is read once as regcomp reads it, before any set is
        built, so that what it refuses it refuses in its words and in its
        order. A token is the character that stands for it: the end, a
        character, - ] ^, or the opener of a class : a collating symbol . or
        an equivalence class =.
*/
static p8 rx_token(rx_compiler *c, positive *size)
{
        p8 byte = rx_peek(c, 0), next = rx_peek(c, 1);

        *size = 1;
        if (byte == '[' && (next == ':' || next == '.' || next == '='))
                return *size = 2, next;
        return !byte ? 0 : byte == '-' || byte == ']' || byte == '^' ? byte : 'c';
}

// The character at index at and the bytes it takes: a UTF-8 one where the
// policy says names are read so, and any other byte as itself.
static positive rx_code(rx_compiler *c, positive at, p32 *value)
{
        memory_utf8_state state = {0};
        positive used = 0;
        b32 fed;

        *value = c->pattern[at];
        if (c->pattern[at] < 0x80 ||
            !(c->program.policy & (REGEX_CHARACTERS | REGEX_UTF8_NAMES)))
                return 1;
        while (!(fed = memory_utf8_feed(&state, c->pattern[at + used])))
                used++;
        if (fed < 0)
                return 1;
        *value = state.value;
        return used + 1;
}

typedef struct
{
        p8 kind;
        p32 value;
        positive length;
        string_address name;
} rx_element;

// One member, or the name of one, at the cursor: 0, or the reason it is
// refused. A - that is neither first nor the end of a range must be last.
static p8 rx_element_read(rx_compiler *c, rx_element *element, p8 token,
                          positive size, bool hyphen)
{
        element->kind = 'c';
        element->length = 1;
        if (token == ':' || token == '.' || token == '=')
        {
                positive kept = 0;

                c->at += 2;
                element->name = c->pattern + c->at;
                for (; rx_peek(c, 0) && rx_peek(c, 1); kept++, c->at++)
                        if (rx_peek(c, 0) == token && rx_peek(c, 1) == ']')
                                break;
                if (kept > 31 || rx_peek(c, 0) != token || rx_peek(c, 1) != ']')
                        return REGEX_FAILED_BRACKET;
                c->at += 2;
                element->kind = token;
                element->length = kept;
                //      A collating name of one character is that character.
                if (token != ':' && kept)
                {
                        positive taken = rx_code(c, element->name - c->pattern,
                                                 &element->value);

                        element->length = taken == kept ? 1 : kept;
                }
                return 0;
        }
        if (token == '-' && !hyphen)
        {
                positive next;
                p8 after;

                c->at += size;
                after = rx_token(c, &next);
                c->at -= size;
                if (after != ']')
                        return REGEX_FAILED_RANGE;
        }
        c->at += rx_code(c, c->at, &element->value) - 1 + size;
        return 0;
}

// Whether the bracket the cursor stands in, after its [, is one regcomp
// takes: 0, or why not. It leaves the cursor past the ].
static p8 rx_set_verify(rx_compiler *c)
{
        positive size, second_size = 0;
        p8 token = rx_token(c, &size), second;
        bool first = true;

        if (token == '^')
        {
                c->at += size;
                token = rx_token(c, &size);
        }
        if (!token)
                return REGEX_FAILED_OTHER;
        if (token == ']')
                token = 'c';
        for (;;)
        {
                rx_element start = {0}, stop = {0};
                bool range = false;
                p8 refused = rx_element_read(c, &start, token, size, first);

                if (refused)
                        return refused;
                first = false;
                token = rx_token(c, &size);
                if ((start.kind == 'c' || start.kind == '.') && token == '-')
                {
                        c->at += size;
                        second = rx_token(c, &second_size);
                        c->at -= size;
                        if (!second)
                                return REGEX_FAILED_BRACKET;
                        if (second == ']')
                                token = 'c';
                        else
                                c->at += size, range = true;
                }
                else if ((start.kind == 'c' || start.kind == '.') && !token)
                        return REGEX_FAILED_BRACKET;
                if (range)
                {
                        if ((refused = rx_element_read(c, &stop, second,
                                                       second_size, true)))
                                return refused;
                        token = rx_token(c, &size);
                        if ((start.kind != 'c' && start.kind != '.') ||
                            (stop.kind != 'c' && stop.kind != '.'))
                                return REGEX_FAILED_RANGE;
                        //      Nor does glibc's UTF-8 locale order a range
                        //      whose end is past ASCII.
                        if (start.length != 1 || stop.length != 1 ||
                            ((start.value | stop.value) > 0x7f &&
                             (c->program.policy &
                              (REGEX_CHARACTERS | REGEX_UTF8_NAMES))))
                                return REGEX_FAILED_COLLATE;
                        //      Basic syntax as expr and csplit read it lets
                        //      a range run backwards and match nothing.
                        if (c->extended && start.value > stop.value)
                                return REGEX_FAILED_RANGE;
                }
                else if (start.kind == ':' &&
                         byte_class_index(start.name, start.length) < 0)
                        return REGEX_FAILED_CLASS;
                else if ((start.kind == '.' || start.kind == '=') &&
                         start.length != 1)
                        return REGEX_FAILED_COLLATE;
                if (!token)
                        return REGEX_FAILED_BRACKET;
                if (token == ']')
                        return c->at += size, 0;
        }
}

// The byte a strict bracket's [.x.] or [=x=] at ahead names, or -1.
static bipolar rx_named(rx_compiler *c, positive ahead)
{
        p8 kind = rx_peek(c, ahead + 1);

        return rx_peek(c, ahead) == '[' && (kind == '.' || kind == '=') &&
                       rx_peek(c, ahead + 3) == kind &&
                       rx_peek(c, ahead + 4) == ']'
                   ? rx_peek(c, ahead + 2)
                   : -1;
}

/* Brackets keep the BRE/ERE backslash rule; sed enables its own escapes. */
static b32 rx_parse_set(rx_compiler *c, rx_set_facts *facts)
{
        b32 set = rx_new_set(c);
        bool strict = (c->program.policy & REGEX_STRICT_INTERVALS) != 0;
        bool negate = rx_peek(c, 0) == '^', first = true;
        bool wide = (c->program.policy & REGEX_CHARACTERS) != 0;
        if (set < 0)
                return 0;
        if (strict)
        {
                positive began = c->at;
                p8 refused = rx_set_verify(c);

                c->at = began;
                if (refused)
                        return rx_refuse(c, refused), set;
        }
        facts->negated = negate;
        c->at += negate;
        while (c->at < c->length)
        {
                p8 byte = rx_peek(c, 0);
                if (byte == ']' && !first)
                {
                        c->at++;
                        if (negate)
                                for (b32 i = 0; i < 256; i++)
                                        c->pool->sets[set][i] ^= 1;
                        return set;
                }
                first = false;
                if (byte == '[' && rx_peek(c, 1) == ':')
                {
                        positive used;
                        b32 kind = byte_class_parse(c->pattern + c->at,
                                                   c->length - c->at, address_of used);
                        if (kind >= 0)
                        {
                                for (b32 i = 0; i < 256; i++)
                                        if (byte_class_holds(kind, (p8)i))
                                                rx_set_add(c, set, (p8)i);
                                c->at += used;
                                continue;
                        }
                }
                bipolar named = strict ? rx_named(c, 0) : -1;

                if (named >= 0)
                {
                        byte = (p8)named;
                        c->at += 4;
                }
                else if (c->escapes && byte == '\\' && rx_peek(c, 1))
                {
                        byte = rx_peek(c, 1);
                        byte = byte == 'n' ? '\n' : byte == 't' ? '\t' :
                               byte == 'r' ? '\r' : byte;
                        c->at++;
                }
                /*
                        A member of more than one byte is kept whole and
                        alternated beside the set rather than broken into
                        bytes the set could not tell apart. A range with an
                        endpoint like that has no byte order to walk and
                        keeps the byte reading it always had.

                        So does a negated bracket that names one: saying
                        every character but this one needs each lead byte's
                        range split around it, which an alternation of whole
                        sequences cannot say, and a wrong answer that looks
                        right is worse than the byte answer it has now.
                */
                // c->at still stands on the member here, so one ahead is a
                // range's dash and two ahead is the end it runs to.
                if (byte >= 0x80 ||
                    (rx_peek(c, 1) == '-' && rx_peek(c, 2) >= 0x80))
                        facts->high = true;

                if (wide && !negate && named < 0 && byte >= 0x80 &&
                    rx_peek(c, 1) != '-')
                {
                        positive size = memory_utf8_span(
                            (address_any)(c->pattern + c->at),
                            c->length - c->at, 1).x;

                        if (size > 1)
                        {
                                if (facts->count == RX_SET_WIDE_MAX)
                                {
                                        c->broken = true;
                                        return set;
                                }

                                facts->at[facts->count] = c->at;
                                facts->size[facts->count++] = (p8)size;
                                c->at += size;
                                continue;
                        }
                }
                c->at++;
                if (rx_peek(c, 0) == '-' && rx_peek(c, 1) && rx_peek(c, 1) != ']')
                {
                        bipolar stop = strict ? rx_named(c, 1) : -1;
                        p8 last = stop >= 0 ? (p8)stop : rx_peek(c, 1);

                        c->at += stop >= 0 ? 6 : 2;
                        for (b32 i = byte; i <= last; i++)
                                rx_set_add(c, set, (p8)i);
                }
                else
                        rx_set_add(c, set, byte);
        }
        c->broken = true;
        return set;
}

static rx_fragment rx_alternation(rx_compiler *c);

static rx_fragment rx_atom(rx_compiler *c)
{
        p8 byte = rx_peek(c, 0), kind = RX_BYTE;
        // Which set, when the atom is one: a pattern may hold more than a byte
        // can number.
        b32 index = 0;
        rx_fragment child = {0};
        bool anchor = false;

        c->bare = false;
        if (c->extended && (c->program.policy & REGEX_STRICT_INTERVALS) &&
            (byte == '*' || byte == '+' || byte == '?' || byte == '{'))
                return rx_refuse(c, REGEX_FAILED_PRECEDING), (rx_fragment){0};
        if (rx_operator(c, '('))
        {
                byte = c->program.groups < RX_GROUP_MAX ? ++c->program.groups : 0;
                c->at += c->extended ? 1 : 2;
                child = rx_alternation(c);
                if (!rx_operator(c, ')'))
                        rx_refuse(c, REGEX_FAILED_OPEN);
                else
                {
                        c->at += c->extended ? 1 : 2;
                        c->closed |= (p16)(1u << byte);
                }
                if (!byte)
                        return child;
                kind = RX_CAPTURE;
        }
        else
        {
                positive at = c->at++;
                bool wide = (c->program.policy & REGEX_CHARACTERS) != 0;

                /*
                        A character of more than one byte is one atom, so a
                        repeat after it repeats all of it: é+ was the byte
                        C3 and then one or more of A9, which matched the
                        first é of ééé and no more.
                */
                if (wide && byte >= 0xc2 && byte <= 0xf4)
                {
                        positive size = memory_utf8_span((address_any)(c->pattern + at),
                                                         c->length - at, 1).x;

                        if (size > 1)
                        {
                                rx_fragment run = {0};

                                if (c->program.flags & RX_IGNORE_CASE)
                                {
                                        p32 code;

                                        if (rx_code(c, at, address_of code) == size)
                                        {
                                                c->at = at + size;
                                                return rx_cased(c, code);
                                        }
                                }
                                for (positive b = 0; b < size; b++)
                                        run = rx_join(c, run,
                                                      rx_one(c, RX_BYTE, c->pattern[at + b]));

                                c->at = at + size;
                                return run;
                        }
                }

                if (byte == '.')
                {
                        kind = RX_ANY;

                        /*
                                A dot is one character, and every character
                                of one byte but a newline is the set it
                                takes. The bytes above it are never one on
                                their own, so they are left to the
                                sequences rx_character alternates in.
                        */
                        if (wide)
                        {
                                b32 plain = c->wide_ascii;

                                if (plain < 0 && (plain = rx_new_set(c)) >= 0)
                                {
                                        for (b32 i = 0; i < 0x80; i++)
                                                c->pool->sets[plain][i] = 1;

                                        if (!(c->program.policy & REGEX_DOT_NEWLINE))
                                                c->pool->sets[plain]['\n'] = 0;

                                        c->wide_ascii = plain;
                                }

                                if (plain < 0)
                                {
                                        c->broken = true;
                                        return (rx_fragment){0};
                                }

                                return rx_character(c, plain);
                        }
                }
                else if (byte == '[')
                {
                        rx_set_facts facts = {0};

                        kind = RX_SET;
                        index = rx_parse_set(c, address_of facts);

                        /*
                                A negated bracket names every character it
                                did not, which the table cannot say on its
                                own: the bytes above 0x7f it holds are the
                                halves of characters, so they come out and
                                the sequences stand beside it instead. A
                                bracket that named characters of more than
                                one byte alternates each of them in whole.
                        */
                        if (wide && ((facts.negated && !facts.high) || facts.count))
                        {
                                rx_fragment whole;

                                if (facts.negated)
                                {
                                        for (b32 i = 0x80; i < 256; i++)
                                                c->pool->sets[index][i] = 0;

                                        whole = rx_character(c, index);
                                }
                                else
                                        whole = rx_one(c, RX_SET, index);

                                for (b32 i = 0; i < facts.count; i++)
                                {
                                        rx_fragment run = {0};
                                        p32 code;

                                        if ((c->program.flags & RX_IGNORE_CASE) &&
                                            rx_code(c, facts.at[i], address_of code) ==
                                                facts.size[i])
                                                run = rx_cased(c, code);
                                        else
                                                for (b32 b = 0; b < facts.size[i]; b++)
                                                        run = rx_join(
                                                            c, run,
                                                            rx_one(c, RX_BYTE,
                                                                   c->pattern[facts.at[i] + b]));

                                        whole = rx_either(c, whole, run);
                                }

                                return whole;
                        }

                        /*
                                A bracket with one member is that byte, and
                                the hints, the fixed string and the machine's
                                classes see a byte where they would see a
                                set: [z]{3} is zzz. The set is given back.
                        */
                        if (!c->broken && (positive)index == c->cursor.sets - 1)
                        {
                                positive members = 0, only = 0;

                                for (positive i = 0; i < 256 && members < 2; i++)
                                        if (c->pool->sets[index][i])
                                        {
                                                members++;
                                                only = i;
                                        }

                                if (members == 1 && only < 0x80)
                                {
                                        kind = RX_BYTE;
                                        byte = (p8)only;
                                        c->cursor.sets--;
                                }
                        }
                }
                else if (byte == '^' && (c->extended || !at ||
                         (at >= 2 && c->pattern[at - 2] == '\\' &&
                          (c->pattern[at - 1] == '(' || c->pattern[at - 1] == '|'))))
                        kind = RX_BEGIN;
                else if (byte == '$' && (c->extended || c->at == c->length ||
                         rx_operator(c, ')') || rx_operator(c, '|')))
                        kind = RX_END;
                else if (byte == '\\' && rx_peek(c, 0))
                {
                        byte = rx_peek(c, 0);
                        c->at++;
                        if (byte >= '1' && byte <= '9')
                        {
                                kind = RX_BACKREF;
                                byte -= '0';
                                if ((c->program.policy & REGEX_STRICT_INTERVALS) &&
                                    !(c->closed & (1u << byte)))
                                        rx_refuse(c, REGEX_FAILED_REFERENCE);
                        }
                        else if (byte == 'w' || byte == 'W' || byte == 's' || byte == 'S')
                        {
                                bool space = byte == 's' || byte == 'S';
                                bool negate = byte == 'W' || byte == 'S';
                                b32 set = rx_new_set(c);
                                if (set >= 0)
                                        for (b32 i = 0; i < 256; i++)
                                                if ((space ? byte_is_space((p8)i) : text_word((p8)i)) != negate)
                                                        rx_set_add(c, set, (p8)i);
                                kind = RX_SET;
                                index = set < 0 ? 0 : set;
                        }
                        else if (byte == 'b' || byte == 'B' || byte == '<' || byte == '>')
                        {
                                kind = RX_EDGE;
                                byte = byte == 'b' ? REGEX_EDGE_WORD : byte == 'B' ?
                                       REGEX_EDGE_NOT_WORD : byte == '<' ? REGEX_EDGE_START : REGEX_EDGE_STOP;
                        }
                        else if (c->escapes)
                                byte = byte == 'n' ? '\n' : byte == 't' ? '\t' : byte;
                        //      The buffer edges are anchors to regcomp too.
                        anchor = kind == RX_BYTE && (byte == '`' || byte == '\'');
                }
                else if (byte == '\\' && (c->program.policy & REGEX_STRICT_INTERVALS))
                        rx_refuse(c, REGEX_FAILED_ESCAPE);
                if (kind == RX_BYTE && (c->program.flags & RX_IGNORE_CASE))
                        byte = (p8)byte_to_lower(byte);
        }
        rx_ref at = rx_emit(c, (rx_node){.kind = kind,
                                         .argument = kind == RX_SET ? (p16)index : byte,
                                         .left = child.first, .right = child.last});
        c->bare = anchor || kind == RX_BEGIN || kind == RX_END || kind == RX_EDGE;
        return (rx_fragment){at, at};
}

/*
        An interval under REGEX_STRICT_INTERVALS, as glibc's parse_dup_op
        reads one: each bound is digits saturating at one past RE_DUP_MAX, a
        byte that is not a digit spoils the bound but reading goes on to the
        comma or the close, and the end of the pattern first is an unmatched
        brace. No lower bound is the content refused unless a comma says it
        is zero; a lower bound above the upper one is refused too; and a bound
        past RE_DUP_MAX is a pattern too big.
*/
static bool rx_interval_strict(rx_compiler *c, b32 *low, b32 *high)
{
        positive at = c->at + (c->extended ? 1 : 2);
        bool closed = false;
        bipolar bounds[2] = {-1, -1};
        b32 which = 0;
        bool comma = false;

        while (at < c->length)
        {
                bool close = c->extended ? c->pattern[at] == '}'
                                         : c->pattern[at] == '\\' && at + 1 < c->length &&
                                               c->pattern[at + 1] == '}';

                if (close)
                {
                        at += c->extended ? 1 : 2;
                        closed = true;
                        break;
                }

                p8 byte = (p8)c->pattern[at];

                // The lower bound's comma ends its reading, and a spoiled
                // bound is refused there without reading the upper one. The
                // upper bound's reading stops at a second comma too, which
                // is no close: bad content, not an unmatched brace.
                if (byte == ',' && (which || bounds[0] == -2))
                {
                        c->broken = true;
                        c->failure = REGEX_FAILED_CONTENT;
                        return false;
                }

                if (byte == ',')
                {
                        comma = true;
                        which = 1;
                        at++;
                        continue;
                }

                if (byte == '\\' && at + 1 < c->length)
                        at++, byte = 0;

                bipolar made = bounds[which];

                bounds[which] = byte < '0' || byte > '9' || made == -2 ? -2
                                : made == -1 ? byte - '0'
                                : min(32768, made * 10 + (byte - '0'));
                at++;
        }

        c->broken = true;

        if (!closed)
        {
                c->failure = REGEX_FAILED_BRACE;
                return false;
        }

        bipolar first = bounds[0];
        bipolar second = comma ? bounds[1] : first;

        if (first == -1)
        {
                if (!comma)
                {
                        c->failure = REGEX_FAILED_CONTENT;
                        return false;
                }
                first = 0;
        }

        if (first == -2 || second == -2 || (second != -1 && first > second))
        {
                c->failure = REGEX_FAILED_CONTENT;
                return false;
        }

        if ((second == -1 ? first : second) > 32767)
        {
                c->failure = REGEX_FAILED_SIZE;
                return false;
        }

        c->broken = false;
        *low = (b32)first;
        *high = second == -1 ? RX_UNBOUNDED : (b32)second;
        c->at = at;
        return true;
}

static bool rx_interval(rx_compiler *c, b32 *low, b32 *high)
{
        if (c->program.policy & REGEX_STRICT_INTERVALS)
                return rx_interval_strict(c, low, high);

        positive at = c->at + (c->extended ? 1 : 2), used;
        if (at >= c->length)
                return false;
        positive first = string_digits_max(c->pattern + at, c->length - at, address_of used);
        if (!used && c->pattern[at] != ',')
                return false;
        at += used;
        positive second = first;
        if (at < c->length && c->pattern[at] == ',')
        {
                at++;
                second = string_digits_max(c->pattern + at, c->length - at, address_of used);
                at += used;
                if (!used)
                        second = positive_max;
        }
        if (first > 32767 || (second != positive_max && second > 32767))
                return false;
        if (c->extended ? at >= c->length || c->pattern[at] != '}' :
            at + 1 >= c->length || c->pattern[at] != '\\' || c->pattern[at + 1] != '}')
                return false;
        *low = (b32)first;
        *high = second == positive_max ? RX_UNBOUNDED : (b32)second;
        c->at = at + (c->extended ? 1 : 2);
        return true;
}

static rx_fragment rx_piece(rx_compiler *c)
{
        positive body_start = c->cursor.nodes;
        positive began = c->at;
        rx_fragment prefix = {0}, body = rx_atom(c);

        //      An anchor is not repeated, and what would repeat it has
        //      nothing before it.
        if (c->bare && c->extended && (c->program.policy & REGEX_STRICT_INTERVALS) &&
            (rx_peek(c, 0) == '*' || rx_peek(c, 0) == '+' ||
             rx_peek(c, 0) == '?' || rx_peek(c, 0) == '{'))
                rx_refuse(c, REGEX_FAILED_PRECEDING);

        // ^ that begins an alternative, then a repeat: the repeat is at the
        // start of an expression too, and grep says so; it still repeats
        // the anchor, which is the same as repeating nothing.
        if (c->extended && (c->program.policy & REGEX_LEADING_REPEATS) && !c->broken &&
            body.first && c->pool->nodes[body.first].kind == RX_BEGIN &&
            (!began || c->pattern[began - 1] == '(' || c->pattern[began - 1] == '|') &&
            (rx_peek(c, 0) == '*' || rx_peek(c, 0) == '+' || rx_peek(c, 0) == '?'))
                rx_warn_repeat(rx_peek(c, 0));
        while (!c->broken)
        {
                p8 byte = rx_peek(c, 0);
                b32 low = 0, high = RX_UNBOUNDED;
                //      A basic anchor takes no repetition: what would repeat
                //      it is the character itself, as regcomp reads ^* and
                //      ^\+ (and tac's Emacs syntax ^+).
                positive plain = 0;

                if (!c->extended && c->pool->nodes[body.first].kind == RX_BEGIN)
                        plain = byte == '*' ? 1
                                : (c->program.policy & REGEX_BASIC_REPEATS) &&
                                          (rx_operator(c, '+') || rx_operator(c, '?'))
                                    ? 2
                                : (c->program.policy & REGEX_PLAIN_REPEATS) &&
                                          (byte == '+' || byte == '?')
                                    ? 1
                                    : 0;
                if (plain)
                {
                        p8 literal = rx_peek(c, plain - 1);

                        c->at += plain;
                        prefix = rx_join(c, prefix, body);
                        body_start = c->cursor.nodes;
                        rx_ref at = rx_emit(c, (rx_node){.kind = RX_BYTE, .argument = literal});
                        body = (rx_fragment){at, at};
                        continue;
                }
                if (byte == '*')
                        c->at++;
                else if ((c->extended || (c->program.policy & REGEX_BASIC_REPEATS)) &&
                         (rx_operator(c, '+') || rx_operator(c, '?')))
                {
                        low = rx_operator(c, '+');
                        high = low ? RX_UNBOUNDED : 1;
                        c->at += c->extended ? 1 : 2;
                }
                else if ((c->program.policy & REGEX_PLAIN_REPEATS) &&
                         (byte == '+' || byte == '?'))
                {
                        low = byte == '+';
                        high = low ? RX_UNBOUNDED : 1;
                        c->at++;
                }
                else if ((c->extended || (c->program.policy & REGEX_BASIC_REPEATS)) && rx_operator(c, '{'))
                {
                        //      Nothing before it to repeat: in basic syntax
                        //      regcomp takes the brace as itself.
                        if ((c->program.policy & REGEX_STRICT_INTERVALS) && !c->extended &&
                            (!body.first || c->pool->nodes[body.first].kind == RX_BEGIN))
                                break;
                        if (!rx_interval(c, address_of low, address_of high))
                                break;
                }
                else
                        break;
                bool simple = body.first == body.last && body.first &&
                              c->pool->nodes[body.first].kind >= RX_BYTE &&
                              c->pool->nodes[body.first].kind <= RX_SET;
                if (!simple)
                {
                        if (!low && !high)
                        {
                                c->cursor.nodes = body_start;
                                body = (rx_fragment){0};
                        }
                        if (high >= 0 && high < low)
                                high = low;

                        if (high == low && (!body.first || low == 1))
                                continue;
                }
                rx_ref at = rx_emit(c, (rx_node){.kind = RX_COUNT,
                                                 .left = body.first, .right = body.last,
                                                 .minimum = (b16)low, .maximum = (b16)high});
                body = (rx_fragment){at, at};
        }
        return rx_join(c, prefix, body);
}

/* A repeat operator where there is nothing to repeat, skipped; the caller
   said extended syntax. */
static bool rx_leading_repeat(rx_compiler *c)
{
        p8 byte = rx_peek(c, 0);
        b32 low, high;

        if (byte == '*' || byte == '+' || byte == '?')
                c->at++;
        else if (!(byte == '{' && rx_interval(c, &low, &high)))
                return false;
        rx_warn_repeat(byte);
        return true;
}

static rx_fragment rx_alternation(rx_compiler *c)
{
        rx_fragment whole = {0};
        bool have_alternative = false;
        if (++c->depth > RX_PARSE_MAX)
                c->broken = true;
        //      A ) with no group open is itself, as regcomp reads extended
        //      syntax.
        bool loose = c->extended && (c->program.policy & REGEX_STRICT_INTERVALS) &&
                     c->depth == 1;
        //      A back reference sees the groups closed before this
        //      alternation and in its own alternative, not in a sibling.
        p16 initial = c->closed;

        for (;;)
        {
                p16 before = c->closed;
                rx_fragment branch = {0};
                if (have_alternative)
                        c->closed = initial;
                if (c->extended && (c->program.policy & REGEX_LEADING_REPEATS))
                {
                        bool skipped = false;

                        while (rx_leading_repeat(c))
                                skipped = true;
                        // glibc goes on to parse what follows as a new
                        // expression, and a ) there closes nothing.
                        if (skipped && rx_operator(c, ')'))
                                rx_refuse(c, REGEX_FAILED_OPEN);
                }
                while (!c->broken && c->at < c->length && (loose || !rx_operator(c, ')')) &&
                       !rx_operator(c, '|'))
                        branch = rx_join(c, branch, rx_piece(c));
                if (have_alternative)
                        c->closed |= before;
                if (!have_alternative)
                        whole = branch;
                else
                {
                        rx_ref at = rx_emit(c, (rx_node){.kind = RX_ALT,
                                                         .left = whole.first, .right = branch.first});
                        whole = (rx_fragment){at, at};
                }
                if (c->broken || !rx_operator(c, '|'))
                        break;
                have_alternative = true;
                c->at += c->extended ? 1 : 2;
        }
        c->depth--;
        return whole;
}

static rx_ref rx_tail(const rx_node *nodes, rx_ref at)
{
        while (at && nodes[at].next)
                at = nodes[at].next;
        return at;
}

/* Return 0 after a consuming edge, 1 for an empty path, 2 for an unknown
   first byte. The reverse walk passes assertions but never backreferences. */
static b32 rx_edges(const regex_program *program, rx_ref first, rx_ref last, p8 *table, bool reverse)
{
        b32 result = 1;
        for (rx_ref at = reverse ? last : first; at;)
        {
                const rx_node *node = program->nodes + at;
                result = 1;
                if (node->kind >= RX_BYTE && node->kind <= RX_SET)
                {
                        if (node->kind == RX_BYTE)
                        {
                                table[node->argument] = 1;
                                if ((program->flags & RX_IGNORE_CASE) && byte_is_alpha(node->argument))
                                        table[node->argument ^ 32] = 1;
                        }
                        else if (node->kind == RX_ANY)
                        {
                                p8 newline = table['\n'];
                                memory_fill(table, 1, 256);
                                if (!(program->policy & REGEX_DOT_NEWLINE))
                                        table['\n'] = newline;
                        }
                        else
                                for (b32 i = 0; i < 256; i++)
                                        table[i] |= program->sets[node->argument][i];
                        result = 0;
                }
                else if (node->kind == RX_BACKREF ||
                         (!reverse && node->kind >= RX_BEGIN && node->kind <= RX_EDGE))
                        result = 2;
                else if (node->kind == RX_ALT)
                {
                        b32 one = rx_edges(program, node->left, rx_tail(program->nodes, node->left), table, reverse);
                        b32 two = rx_edges(program, node->right, rx_tail(program->nodes, node->right), table, reverse);
                        result = one == 2 || two == 2 ? 2 : one || two;
                }
                else if (node->kind == RX_CAPTURE || (node->kind == RX_COUNT && node->maximum))
                {
                        result = rx_edges(program, node->left, node->right, table, reverse);
                        if (node->kind == RX_COUNT && !node->minimum && result != 2)
                                result = 1;
                }
                if (result != 1)
                        break;
                at = reverse ? node->previous : node->next;
        }
        return result;
}

/*
        A literal sequence outside alternation, or inside a mandatory child,
        is a safe block prefilter. A capture only groups, so the bytes in it
        and the bytes beside it are one run: (a)(a)(a)\3 needs aaa, and the
        longest run is the one searched for. Counted nodes are never copied,
        and what is not a byte ends the run it is in.
*/
typedef struct
{
        p8 *literal;
        positive *literal_length;
        p8 *extra;
        positive *extra_length;
        p8 run[RX_LITERAL_MAX];
        positive run_length;
} rx_required_state;

static fn rx_required_flush(rx_required_state *state)
{
        positive kept = min(state->run_length, (positive)RX_EXTRA_MAX);

        if (state->run_length > *state->literal_length)
        {
                // The longest so far steps down to be the next, if it is longer.
                positive stepped = min(*state->literal_length, (positive)RX_EXTRA_MAX);

                if (stepped > *state->extra_length)
                {
                        *state->extra_length = stepped;
                        memory_copy_apart(state->extra, state->literal, stepped);
                }
                *state->literal_length = state->run_length;
                memory_copy_apart(state->literal, state->run, state->run_length);
        }
        else if (kept > *state->extra_length)
        {
                *state->extra_length = kept;
                memory_copy_apart(state->extra, state->run, kept);
        }
        state->run_length = 0;
}

static fn rx_required_walk(const rx_node *nodes, rx_ref first, rx_required_state *state)
{
        for (rx_ref at = first; at; at = nodes[at].next)
        {
                const rx_node *node = nodes + at;

                if (node->kind == RX_BYTE)
                {
                        if (state->run_length == RX_LITERAL_MAX)
                                rx_required_flush(state);
                        state->run[state->run_length++] = (p8)node->argument;
                }
                else if (node->kind == RX_CAPTURE)
                        rx_required_walk(nodes, node->left, state);
                else
                {
                        rx_required_flush(state);
                        if (node->kind == RX_COUNT && node->minimum > 0)
                        {
                                rx_required_walk(nodes, node->left, state);
                                rx_required_flush(state);
                        }
                }
        }
}

static fn rx_required_two(const rx_node *nodes, rx_ref first, p8 *literal, positive *literal_length,
                          p8 *extra, positive *extra_length)
{
        rx_required_state state = {.literal = literal, .literal_length = literal_length,
                                   .extra = extra, .extra_length = extra_length};

        rx_required_walk(nodes, first, &state);
        rx_required_flush(&state);
}

static fn rx_required(const rx_node *nodes, rx_ref first, p8 *literal, positive *literal_length)
{
        p8 extra[RX_EXTRA_MAX];
        positive extra_length = 0;

        rx_required_two(nodes, first, literal, literal_length, extra, &extra_length);
}

/* A deterministic graph can reuse the prepared literal search when its
   captures are not observed. Bound both the expansion and interpreter work;
   the graph itself stays compact and remains the resource-limited fallback. */
static bool rx_fixed(const rx_node *nodes, rx_ref first, rx_hints *hints, positive *work)
{
        for (rx_ref at = first; at; at = nodes[at].next)
        {
                const rx_node *node = nodes + at;
                positive begin = hints->fixed_length, child_work = 0;
                if (node->kind == RX_BYTE)
                {
                        if (begin < RX_FIXED_MAX)
                                hints->fixed_literal[begin] = (p8)node->argument;
                        hints->fixed_length++;
                        child_work = 1;
                }
                else if (node->kind == RX_CAPTURE ||
                         (node->kind == RX_COUNT && node->minimum == node->maximum))
                {
                        if (!rx_fixed(nodes, node->left, hints, &child_work))
                                return false;
                        if (node->kind == RX_CAPTURE)
                                child_work += 2;
                        else
                        {
                                positive size = hints->fixed_length - begin;
                                positive count = node->minimum;
                                if (count > RX_FIXED_MAX / (child_work + 1))
                                        return false;
                                /* Every byte costs at least one work unit, so this
                                   multiplication is bounded by the work check. */
                                hints->fixed_length = begin + count * size;
                                if (hints->fixed_length <= RX_FIXED_MAX)
                                        for (positive i = 1; i < count && size; i++)
                                                memory_copy_apart(hints->fixed_literal + begin + i * size,
                                                                  hints->fixed_literal + begin, size);
                                child_work = 1 + count * (child_work + 1);
                        }
                }
                else
                        return false;
                if (child_work > RX_FIXED_MAX - *work)
                        return false;
                *work += child_work;
        }
        return true;
}

static bool rx_single(const regex_program *program, const rx_node *node, p8 byte);

static p32 rx_compiles;

// Why the last compile refused its pattern, when it did.
static p8 regex_failure;

/* Compile above the current mark. Neither a failed compile nor its scratch
   metadata changes a published descriptor or the pool's ownership cursor. */
static bool rx_compile_length(rx_pool *pool, regex_program *out, string_address pattern,
                              positive length, bool extended, bool icase, bool escapes, p8 policy)
{
        rx_compiler c = {.pool = pool, .cursor = pool->used, .pattern = pattern,
                         .length = length, .extended = extended,
                         .escapes = escapes, .wide_ascii = -1, .wide_two = -1,
                         .wide_three = -1, .wide_four = -1, .wide_tail = -1};
        if (!c.cursor.nodes)
                c.cursor.nodes = 1;
        // Node nought is none, and the pool has it even when nothing is
        // written: a pattern of nothing leaves the program a table to point at.
        if (!rx_room(address_of c, c.cursor.nodes + 1))
                return regex_failure = REGEX_FAILED_OTHER, false;
        rx_ref first = (rx_ref)c.cursor.nodes;
        rx_hints *hints = &pool->hints;
        /* Every accepted proof byte is constructed before publication;
           reusing the hints does not require clearing unused capacity. */
        memory_fill(hints, 0, __builtin_offsetof(rx_hints, fixed_literal));
        c.program = (regex_program){.nodes = pool->nodes, .sets = (const p8 (*)[256])pool->sets,
                                .hints = hints, .policy = policy, .flags = icase ? RX_IGNORE_CASE : 0};
        hints->serial = ++rx_compiles;
        regex_warning_count = 0;
        rx_fragment root = rx_alternation(address_of c);
        // The scratch may have moved while it grew.
        c.program.nodes = pool->nodes;
        c.program.sets = (const p8 (*)[256])pool->sets;
        c.program.first = root.first;
        if (!c.broken && c.at != c.length && rx_operator(address_of c, ')'))
                c.failure = REGEX_FAILED_CLOSE;
        regex_failure = c.failure;
        if (c.at != c.length || c.broken)
        {
                if (!regex_failure)
                        regex_failure = REGEX_FAILED_OTHER;
                return false;
        }
        bool literal = root.first && c.cursor.nodes - first <= RX_LITERAL_MAX;
        for (positive i = first; i < c.cursor.nodes; i++)
        {
                const rx_node *node = pool->nodes + i;
                if (node->kind != RX_BYTE)
                        literal = false;
                if (node->kind == RX_BACKREF)
                        c.program.flags |= RX_HAS_BACKREF;
                if (node->kind == RX_ALT || (node->kind == RX_COUNT && node->minimum != node->maximum))
                        c.program.flags |= RX_BRANCHING;
        }
        if (!rx_edges(address_of c.program, root.first, root.last, hints->first_skip, false))
        {
                c.program.flags |= RX_FIRST_KNOWN;
                for (b32 i = 0; i < 256; i++)
                        hints->first_skip[i] = !hints->first_skip[i];
        }
        if (!(c.program.flags & RX_HAS_BACKREF))
        {
                rx_edges(address_of c.program, root.first, root.last, hints->last_bytes, true);
                c.program.flags |= RX_LAST_KNOWN;
        }
        // Under M a ^ is also at each line's start, so the walk tries every start.
        if (root.first && pool->nodes[root.first].kind == RX_BEGIN && !(policy & REGEX_LINE_ANCHORS))
                c.program.flags |= RX_ANCHORED;
        rx_required_two(pool->nodes, root.first, hints->literal, address_of hints->literal_length,
                        hints->extra, address_of hints->extra_length);
        /*
                The run a pattern begins with, if it does, through the
                groups it sits at the front of (when nothing refers back to
                one): its set, how many it needs, and the node that must
                follow it when that is a byte or a set no byte of the run
                can also be, so the run's last place is the only one it can
                be followed from.
        */
        if (root.first && !(c.program.flags & RX_HAS_BACKREF))
        {
                rx_ref head = root.first, after = 0;

                while (head && pool->nodes[head].kind == RX_CAPTURE && pool->nodes[head].left)
                {
                        if (pool->nodes[head].next)
                                after = pool->nodes[head].next;
                        head = pool->nodes[head].left;
                }
                if (head && pool->nodes[head].kind == RX_COUNT && pool->nodes[head].maximum < 0)
                {
                        const rx_node *count = pool->nodes + head;
                        const rx_node *child = pool->nodes + count->left;

                        if (count->left && !child->next && child->kind >= RX_BYTE && child->kind <= RX_SET)
                        {
                                rx_ref follow = count->next ? count->next : after;
                                const rx_node *next = pool->nodes + follow;

                                hints->lead_run = count->left;
                                hints->lead_minimum = count->minimum;
                                hints->lead_tail = head == root.first && count->next &&
                                                   pool->nodes[count->next].kind == RX_END &&
                                                   !pool->nodes[count->next].next &&
                                                   !(policy & REGEX_LINE_ANCHORS);
                                if (follow && (next->kind == RX_BYTE || next->kind == RX_SET))
                                {
                                        bool apart = true;

                                        for (b32 b = 0; b < 256 && apart; b++)
                                                apart = !(rx_single(&c.program, child, (p8)b) &&
                                                          rx_single(&c.program, next, (p8)b));
                                        if (apart)
                                                hints->lead_next = follow;
                                }
                        }
                }
        }
        /*
                A pattern that is one class of byte ([aeiou], [0-9], a dot)
                or a run of one of them ([0-9]+, [0-9][0-9]*, " *") needs no
                walk: the first byte of the class, and the run from it.
        */
        if (root.first && !c.program.groups && !(c.program.flags & RX_HAS_BACKREF))
        {
                const rx_node *one = pool->nodes + root.first;

                if (!one->next && (one->kind == RX_SET || one->kind == RX_ANY))
                {
                        hints->shape = 1;
                        hints->shape_node = root.first;
                }
                else if (one->kind == RX_COUNT && one->maximum < 0 && !one->next && one->left &&
                         !pool->nodes[one->left].next && pool->nodes[one->left].kind >= RX_BYTE &&
                         pool->nodes[one->left].kind <= RX_SET)
                {
                        hints->shape = 2;
                        hints->shape_node = one->left;
                        hints->shape_minimum = one->minimum;
                }
                else if (one->next && one->kind >= RX_BYTE && one->kind <= RX_SET &&
                         pool->nodes[one->next].kind == RX_COUNT && pool->nodes[one->next].maximum < 0 &&
                         !pool->nodes[one->next].next && pool->nodes[one->next].left)
                {
                        const rx_node *more = pool->nodes + one->next;
                        const rx_node *again = pool->nodes + more->left;

                        if (!again->next && again->kind == one->kind &&
                            (one->kind == RX_ANY || (one->kind == RX_BYTE && again->argument == one->argument) ||
                             (one->kind == RX_SET &&
                              !memory_compare(pool->sets[again->argument], pool->sets[one->argument], 256))))
                        {
                                hints->shape = 2;
                                hints->shape_node = root.first;
                                hints->shape_minimum = 1 + more->minimum;
                        }
                }
        }
        /*
                A string and the end of the text, or all of the text (X$,
                \.$, ^X$): the text ends with it or it does not.
        */
        if (!hints->shape && root.first && !c.program.groups && !(policy & REGEX_LINE_ANCHORS) &&
            hints->literal_length)
        {
                rx_ref at = root.first;
                bool begins = pool->nodes[at].kind == RX_BEGIN;
                positive size = 0;

                if (begins)
                        at = pool->nodes[at].next;
                while (at && pool->nodes[at].kind == RX_BYTE)
                {
                        size++;
                        at = pool->nodes[at].next;
                }
                if (at && size == hints->literal_length && pool->nodes[at].kind == RX_END &&
                    !pool->nodes[at].next)
                {
                        hints->shape = 3;
                        hints->shape_minimum = begins;
                }
        }
        positive fixed_work = 0;
        if (!literal && rx_fixed(pool->nodes, root.first, hints, &fixed_work) &&
            hints->fixed_length)
        {
                hints->fixed_work = fixed_work + 1;
                hints->fixed_anchors = memory_search_prepare(
                    hints->fixed_literal, hints->fixed_length, icase);
        }
        if (literal)
                c.program.flags |= RX_LITERAL_PROVES;
        hints->literal_anchors = memory_search_prepare(hints->literal, hints->literal_length, icase);
        if (hints->extra_length)
                hints->extra_anchors = memory_search_prepare(hints->extra, hints->extra_length, icase);
        *out = c.program;
        pool->used = c.cursor;
        return true;
}

/*
        rx_compile for a caller that compiles above a mark and rewinds to it
        after every use -- [[ =~ ]] in a loop is the same pattern each time --
        and so builds the same program over again (5.8 thousand instructions
        a run). What it built is still standing above the mark until the next
        compile of any kind, and a compile that has not happened since is a
        pool nobody has written to: the same pattern under the same terms
        gets the same program back without a byte of the pool touched.
*/
static struct
{
        p8 key[256];
        positive length;
        p8 extended, icase, escapes, policy, valid;
        p32 compiles;
        rx_pool *pool;
        regex_program program;
} rx_again;

static bool rx_compile(rx_pool *pool, regex_program *out, string_address pattern,
                       bool extended, bool icase, bool escapes, p8 policy)
{
        return rx_compile_length(pool, out, pattern, string_length(pattern), extended,
                                 icase, escapes, policy);
}

static __attribute__((unused)) bool rx_compile_again(rx_pool *pool, regex_program *out, string_address pattern,
                             positive length, bool extended, bool icase, bool escapes, p8 policy)
{
        if (rx_again.valid && rx_again.compiles == rx_compiles && rx_again.pool == pool &&
            rx_again.length == length && rx_again.extended == extended &&
            rx_again.icase == icase && rx_again.escapes == escapes &&
            rx_again.policy == policy && !memory_compare(rx_again.key, pattern, length))
        {
                *out = rx_again.program;
                regex_failure = 0;
                return true;
        }
        bool made = rx_compile_length(pool, out, pattern, length, extended, icase, escapes, policy);
        rx_again.valid = made && length < sizeof(rx_again.key);
        if (rx_again.valid)
        {
                memory_copy_apart(rx_again.key, pattern, length);
                rx_again.length = length;
                rx_again.extended = extended, rx_again.icase = icase;
                rx_again.escapes = escapes, rx_again.policy = policy;
                rx_again.compiles = rx_compiles;
                rx_again.pool = pool;
                rx_again.program = *out;
        }
        return made;
}

/* Iterative graph execution. Continuations stay immutable while a choice can
   revisit them; a choice restores both its frame mark and capture undo mark. */
typedef struct {
        /* Zero resumes a sequence, a positive slot closes its capture,
           and -1 resumes a counted child. The slot is also the frame tag. */
        b16 slot;
        rx_ref node;
        p32 parent;
        positive repetitions, previous_position;
} rx_frame;

typedef struct {
        rx_ref node;
        p32 continuation, frame_mark, undo_mark;
        positive position, lower;
} rx_choice;

typedef struct {
        p8 slot;
        positive previous;
} rx_undo;

typedef struct {
        rx_frame *frames;
        rx_choice *choices;
        rx_undo *undo;
        p32 frame_capacity, choice_capacity, undo_capacity;
        const regex_program *program;
        string_address bytes;
        positive length, slots[20], best_slots[20];
        positive best_stop, best_limit, work_used, work_limit;
        // The caller has just found the required string in the subject.
        bool literal_known;
        /*
                A second, lower ceiling that only the walk below honours,
                set by a caller holding a machine that reads every byte
                once. work_limit stays what it was: the fixed-literal gate
                in rx_find divides by it to decide whether a prepared
                string can answer the whole line, and lowering that would
                send fixed strings to the graph. Zero is no second ceiling.
        */
        positive work_yield;
        p32 frame_used, choice_used, undo_used;
        p8 selection, active_captures;
        // The last walk's slots are its answer, none having been set aside.
        bool best_in_place;
        bool pending_exhaustion, first_exhausted;
} rx_match;

static p32 rx_frame_put(rx_match *match, rx_frame frame)
{
        if (match->frame_used == match->frame_capacity)
        {
                match->pending_exhaustion = true;
                return 0;
        }
        match->frames[match->frame_used++] = frame;
        return match->frame_used;
}

static bool rx_choice_put(rx_match *match, rx_ref node, p32 continuation,
                          positive position, positive lower)
{
        if (match->choice_used == match->choice_capacity)
        {
                match->pending_exhaustion = true;
                return false;
        }
        match->choices[match->choice_used++] = (rx_choice){
            node, continuation, match->frame_used, match->undo_used,
            position, lower};
        return true;
}

static bool rx_slot_put(rx_match *match, p8 slot, positive value)
{
        if (match->choice_used)
        {
                if (match->undo_used == match->undo_capacity)
                {
                        match->pending_exhaustion = true;
                        return false;
                }
                match->undo[match->undo_used++] =
                    (rx_undo){slot, match->slots[slot]};
        }
        match->slots[slot] = value;
        return true;
}

static bool rx_single(const regex_program *program, const rx_node *node, p8 byte)
{
        if (node->kind == RX_ANY)
                return (program->policy & REGEX_DOT_NEWLINE) || byte != '\n';
        if (node->kind == RX_SET)
                return program->sets[node->argument][byte];
        return ((program->flags & RX_IGNORE_CASE) ? byte_to_lower(byte) : byte) == node->argument;
}

static bool rx_accept(rx_match *match, positive position)
{
        const regex_program *program = match->program;
        if (match->selection == REGEX_FIRST || match->best_stop == positive_max)
                match->first_exhausted = match->pending_exhaustion;
        if (match->selection == REGEX_FIRST ||
            (match->selection == REGEX_EXACT_LONGEST && match->first_exhausted))
                return true;
        positive stop = position + (program->boundary == REGEX_BOUNDARY_WORD &&
                                    position < match->length);
        /*
                The limit is how far any match could reach, found by walking
                back from the end of the text to a byte a match can end on,
                and a match that reaches it ends the search early. The walk
                is made only when it can save something: not when no choice
                is left to go back to, since the first match is then the
                longest, and not for a match of nothing, since the choices
                left are tried anyway -- the first match that has a length
                pays for it. An empty match at every position of a long line
                -- sed's s///g, gsub, split and FS on a pattern that may
                match nothing -- paid the walk once per position, and 64 KiB
                took half a second.
        */
        if (match->best_stop == positive_max && !match->choice_used)
        {
                /* The walk ends here, and what it holds is the answer where it stands. */
                match->best_stop = stop;
                match->best_in_place = true;
                return true;
        }
        if (match->best_limit == positive_max &&
            (stop != match->slots[0] || program->boundary == REGEX_BOUNDARY_WORD))
        {
                match->best_limit = match->length;
                positive least = position + (!position && program->boundary == REGEX_BOUNDARY_WORD);
                while (match->best_limit > least && (program->flags & RX_LAST_KNOWN) &&
                       !program->hints->last_bytes[match->bytes[match->best_limit - 1]])
                        match->best_limit--;
                match->best_limit += program->boundary == REGEX_BOUNDARY_WORD &&
                                     match->best_limit < match->length;
        }
        if (match->best_stop == positive_max || stop > match->best_stop)
        {
                match->best_stop = stop;
                memory_copy_apart(match->best_slots, match->slots,
                                  match->active_captures * sizeof(positive));
        }
        return stop == match->best_limit;
}

/* How many bytes of at[0, available) a single-node child takes in a row. */
static inline INLINE positive rx_span(const regex_program *program, const rx_node *child,
                                      string_address at, positive available)
{
        if (child->kind == RX_BYTE && !(program->flags & RX_IGNORE_CASE))
                return memory_span_byte(at, child->argument, available);
        if (child->kind == RX_ANY)
        {
                string_address newline = (program->policy & REGEX_DOT_NEWLINE)
                    ? 0 : memory_first_of(at, '\n', available);
                return newline ? (positive)(newline - at) : available;
        }
        if (child->kind == RX_SET)
                return string_span_max(at, available, (const b8 *)program->sets[child->argument]);
        positive taken = 0;
        while (taken < available && rx_single(program, child, at[taken]))
                taken++;
        return taken;
}

/*
        A run of a set gives its bytes back one at a time, and what follows
        it is tried at each. When what follows is a byte or a set (past the
        ends of the groups the run sits in, whose closing cannot fail) only
        the places that hold one can go on, so the others are not tried:
        .*: or [0-9]*\. made a choice, a pop and a failed compare for every
        byte. The place to try is moved down to the last that can, or the
        choice is spent when none can. Each place passed over is charged
        what the walk spent on it, and a pass that the work ceiling would
        end inside is not made.
*/
enum { RX_BACK_STAY, RX_BACK_MOVED, RX_BACK_NONE };

static __attribute__((noinline)) positive rx_run_span(const regex_program *program, const rx_node *child,
                                                       string_address at, positive available)
{
        return rx_span(program, child, at, available);
}

static inline INLINE int rx_back(const rx_match *match, const rx_choice *choice,
                                 positive *position, positive *work, positive work_limit)
{
        const regex_program *program = match->program;
        rx_ref ahead = choice->node;
        p32 beyond = choice->continuation;
        positive charge = 1;

        while (!ahead && beyond && match->frames[beyond - 1].slot >= 0)
        {
                ahead = match->frames[beyond - 1].node;
                beyond = match->frames[beyond - 1].parent;
                charge = 2;
        }

        const rx_node *next = program->nodes + ahead;

        if (!ahead || (next->kind != RX_SET && next->kind != RX_BYTE))
                return RX_BACK_STAY;

        positive above = *position + 1;

        if (next->kind == RX_BYTE && !(program->flags & RX_IGNORE_CASE))
        {
                string_address hit = memory_last_of(match->bytes + choice->lower, next->argument,
                                                    above - choice->lower);

                above = hit ? (positive)(hit - match->bytes) + 1 : choice->lower;
        }
        else if (next->kind == RX_SET)
        {
                const p8 *member = program->sets[next->argument];

                while (above > choice->lower && !member[match->bytes[above - 1]])
                        above--;
        }
        else
                while (above > choice->lower && !rx_single(program, next, match->bytes[above - 1]))
                        above--;

        /* The places above the last one that can go on, or all of them. */
        positive cost = (*position + 1 - above) * charge;

        if (cost >= work_limit - *work)
                return RX_BACK_STAY;
        *work += cost;
        if (above == choice->lower)
                return RX_BACK_NONE;
        *position = above - 1;
        return RX_BACK_MOVED;
}

static bool rx_run(rx_match *match, positive start)
{
        const regex_program *program = match->program;
        string_address bytes = match->bytes;
        positive length = match->length;
        rx_ref node = program->first;
        p32 continuation = 0;
        positive position = start, repetitions = 0, previous = positive_max;
        positive work = match->work_used, work_limit = match->work_limit;

        if (match->work_yield && match->work_yield < work_limit)
                work_limit = match->work_yield;
        bool accepted = false;
        match->frame_used = match->choice_used = match->undo_used = 0;
        match->best_limit = positive_max;
        match->best_in_place = false;
        // Two slots are the common walk, and two stores are not a call.
        if (match->active_captures == 2)
                match->slots[1] = positive_max;
        else
                memory_fill(match->slots, -1, match->active_captures * sizeof(positive));
        match->slots[0] = start;
        for (;;)
        {
                if (work == work_limit)
                {
                        match->pending_exhaustion = true;
                        goto backtrack;
                }
                work++;
                if (!node)
                {
                        if (continuation)
                        {
                                rx_frame frame = match->frames[continuation - 1];
                                p32 protected = match->choice_used
                                    ? match->choices[match->choice_used - 1].frame_mark : 0;
                                if (continuation == match->frame_used && continuation > protected)
                                        match->frame_used--;
                                continuation = frame.parent;
                                node = frame.node;
                                if (frame.slot > 0 &&
                                    !rx_slot_put(match, (p8)frame.slot, position))
                                        goto backtrack;
                                if (frame.slot < 0)
                                {
                                        repetitions = frame.repetitions;
                                        previous = frame.previous_position;
                                        goto count;
                                }
                                continue;
                        }
                        if ((program->boundary == REGEX_BOUNDARY_LINE && position != length) ||
                            (program->boundary == REGEX_BOUNDARY_WORD && position < length &&
                             string_set_name[bytes[position]]))
                                goto backtrack;
                        match->slots[1] = position;
                        if (rx_accept(match, position))
                        {
                                accepted = true;
                                break;
                        }
                        goto backtrack;
                }

                const rx_node *instruction = program->nodes + node;
                switch (instruction->kind)
                {
                case RX_BYTE:
                case RX_ANY:
                case RX_SET:
                        if (position >= length || !rx_single(program, instruction, bytes[position]))
                                goto backtrack;
                        position++;
                        break;
                case RX_BEGIN:
                        if (position && (!(program->policy & REGEX_LINE_ANCHORS) ||
                                         bytes[position - 1] != '\n'))
                                goto backtrack;
                        break;
                case RX_END:
                        if (position != length && (!(program->policy & REGEX_LINE_ANCHORS) ||
                                                         bytes[position] != '\n'))
                                goto backtrack;
                        break;
                case RX_EDGE:
                {
                        /* Bit (before*2 + after), in WORD/NOT_WORD/START/STOP order. */
                        static const p8 edge_masks[] = {6, 9, 2, 4};
                        bool before = position && string_set_name[bytes[position - 1]];
                        bool after = position < length && string_set_name[bytes[position]];
                        if (!(edge_masks[instruction->argument] & (1u << (before * 2 + after))))
                                goto backtrack;
                        break;
                }
                case RX_BACKREF:
                {
                        if (instruction->argument > program->groups)
                                goto backtrack;
                        positive from = match->slots[instruction->argument * 2];
                        positive to = match->slots[instruction->argument * 2 + 1];
                        if (from == positive_max || to == positive_max || to < from ||
                            to - from > length - position)
                                goto backtrack;
                        bipolar different = (program->flags & RX_IGNORE_CASE)
                            ? memory_compare_ascii_case(bytes + from, bytes + position, to - from)
                            : memory_compare(bytes + from, bytes + position, to - from);
                        if (different)
                                goto backtrack;
                        position += to - from;
                        break;
                }
                case RX_CAPTURE:
                case RX_ALT:
                {
                        /*
                                A character (rx_character's alternation)
                                that stands on a byte below 0x80 is that
                                byte or nothing: the sequences beside it
                                begin above 0x7f, so no choice is left to go
                                back to, and none is made.
                        */
                        if (instruction->kind == RX_ALT && instruction->minimum == 1 &&
                            position < length && bytes[position] < 0x80 &&
                            program->boundary == REGEX_BOUNDARY_NONE && !match->work_yield)
                        {
                                if (!program->sets[program->nodes[instruction->left].argument][bytes[position]])
                                        goto backtrack;
                                position++;
                                break;
                        }
                        p32 resume = continuation;
                        p8 slot = (p8)(instruction->argument * 2);
                        bool capture = instruction->kind == RX_CAPTURE && slot < match->active_captures;
                        if (capture && !rx_slot_put(match, slot, position))
                                goto backtrack;
                        if (capture || instruction->next)
                        {
                                resume = rx_frame_put(match, (rx_frame){
                                    capture ? slot + 1 : 0,
                                    instruction->next, continuation, 0, 0});
                                if (!resume)
                                        goto backtrack;
                        }
                        if (instruction->kind == RX_ALT &&
                            !rx_choice_put(match, instruction->right, resume, position, positive_max))
                                goto backtrack;
                        continuation = resume;
                        node = instruction->left;
                        continue;
                }
                case RX_COUNT:
                {
                        const rx_node *child = program->nodes + instruction->left;
                        /*
                                A repeated character (a dot or a negated
                                bracket in a UTF-8 locale) is the run of the
                                bytes below 0x80 its set takes, when what
                                stops the run is the end, the repeat's
                                ceiling or a byte below 0x80: no sequence
                                lies in it, so a character is a byte and the
                                run is the one a set would make. A run that
                                stops on a byte above 0x7f goes the long way
                                round the graph, as it always did. So does a
                                word or line boundary, which reads the walk's
                                choices where it ends, and a walk with a
                                machine standing by (see rx_find), whose
                                spending is what hands the line over.
                        */
                        bool characters = instruction->left && !child->next &&
                                          child->kind == RX_ALT && child->minimum == 1 &&
                                          program->boundary == REGEX_BOUNDARY_NONE &&
                                          !match->work_yield;
                        if (characters)
                                child = program->nodes + child->left;
                        if (instruction->left && !child->next &&
                            (child->kind == RX_BYTE || child->kind == RX_ANY || child->kind == RX_SET))
                        {
                                positive taken = 0, limit = length - position;
                                if (instruction->maximum >= 0 && limit > (positive)instruction->maximum)
                                        limit = instruction->maximum;
                                positive available = work_limit - work;
                                if (available > limit)
                                        available = limit;
                                string_address at = bytes + position;
                                taken = rx_span(program, child, at, available);
                                if (characters && taken != limit &&
                                    (taken == available || at[taken] >= 0x80))
                                {
                                        repetitions = 0;
                                        previous = positive_max;
                                        goto count;
                                }
                                work += taken;
                                /* The old loop charged only matching bytes: a mismatch at
                                   the work limit must still finish without exhaustion. */
                                if (taken == available && taken < limit && rx_single(program, child, at[taken]))
                                {
                                        match->pending_exhaustion = true;
                                        goto backtrack;
                                }
                                if (taken < (positive)instruction->minimum)
                                        goto backtrack;
                                /*
                                        A run with nothing after it in the
                                        pattern has no use for the places it
                                        could give back: each ends a match
                                        that is shorter than the one this
                                        run makes, and none can come to be
                                        preferred (a word or line boundary
                                        can refuse the longest, and then a
                                        shorter one may stand).
                                */
                                if (taken > (positive)instruction->minimum &&
                                    (instruction->next || continuation ||
                                     program->boundary != REGEX_BOUNDARY_NONE) &&
                                    !rx_choice_put(match, instruction->next, continuation,
                                                   position + taken - 1, position + instruction->minimum))
                                        goto backtrack;
                                position += taken;
                                break;
                        }
                        repetitions = 0;
                        previous = positive_max;
                        goto count;
                }
                }
                node = instruction->next;
                continue;

        count:
                instruction = program->nodes + node;
                if ((repetitions >= (positive)instruction->minimum && instruction->maximum >= 0 &&
                     repetitions >= (positive)instruction->maximum) ||
                    (instruction->maximum < 0 && repetitions > (positive)instruction->minimum && previous == position))
                {
                        node = instruction->next;
                        continue;
                }
                if (repetitions >= (positive)instruction->minimum &&
                    !rx_choice_put(match, instruction->next, continuation, position, positive_max))
                        goto backtrack;
                continuation = rx_frame_put(match, (rx_frame){
                    -1, node, continuation, repetitions + 1, position});
                if (!continuation)
                        goto backtrack;
                node = instruction->left;
                continue;

        backtrack:
                if (!match->choice_used)
                        break;
                rx_choice choice = match->choices[--match->choice_used];
                while (match->undo_used > choice.undo_mark)
                {
                        rx_undo undo = match->undo[--match->undo_used];
                        match->slots[undo.slot] = undo.previous;
                }
                match->frame_used = choice.frame_mark;
                node = choice.node;
                continuation = choice.continuation;
                position = choice.position;
                if (choice.lower != positive_max)
                {
                        /* Only a byte or a set ahead, there or past a group's close, is worth the look. */
                        if ((node ? program->nodes[node].kind == RX_BYTE || program->nodes[node].kind == RX_SET
                                  : continuation != 0) &&
                            rx_back(match, &choice, &position, &work, work_limit) == RX_BACK_NONE)
                                goto backtrack;
                        if (position > choice.lower)
                        {
                                choice.position = position - 1;
                                match->choices[match->choice_used++] = choice;
                        }
                }
        }
        match->work_used = work;
        return accepted;
}

static p8 rx_find_walk(rx_match *match, const regex_program *program, p8 mode, bool captures,
                       string_address bytes, positive length, positive start)
{
        match->program = program;
        match->bytes = bytes;
        match->length = length;
        match->work_used = 0;
        match->selection = (program->flags & RX_BRANCHING) || program->boundary == REGEX_BOUNDARY_WORD
                               ? mode : REGEX_FIRST;
        match->active_captures = captures || (program->flags & RX_HAS_BACKREF)
                                    ? (p8)((program->groups + 1) * 2) : 2;
        if (start > length)
                return RX_NO_MATCH;
        const rx_hints *hints = program->hints;
        bool fixed = hints->fixed_work && match->active_captures == 2 &&
                     !program->boundary && mode != REGEX_EXACT_LONGEST &&
                     !match->pending_exhaustion && match->frame_capacity >= RX_FIXED_MAX &&
                     length - start < match->work_limit / hints->fixed_work;
        if (fixed && hints->fixed_length > length - start)
                return RX_NO_MATCH;
        if (((program->flags & RX_LITERAL_PROVES) || fixed) &&
            !program->boundary && mode != REGEX_EXACT_LONGEST)
        {
                positive size = fixed ? hints->fixed_length : hints->literal_length;
                string_address found = text_literal_find(bytes, length, start,
                    (string_address)(fixed ? hints->fixed_literal : hints->literal), size,
                    (program->flags & RX_IGNORE_CASE) != 0,
                    fixed ? hints->fixed_anchors : hints->literal_anchors);
                if (!found)
                        return RX_NO_MATCH;
                memory_fill(match->slots, -1, match->active_captures * sizeof(positive));
                match->slots[0] = (positive)(found - bytes);
                match->slots[1] = match->slots[0] + size;
                return RX_MATCH;
        }
        /*
                The one prefilter every caller shares: a string the program
                cannot match without, looked for once in the whole subject
                before any start is tried. grep, sed, awk, expr, csplit and
                [[ =~ ]] all come through here, so a record without it is a
                search and nothing more, whichever of them asks.
        */
        if (hints->literal_length && !match->literal_known &&
            !text_literal_find(bytes, length, start, (string_address)hints->literal,
                               hints->literal_length,
                               (program->flags & RX_IGNORE_CASE) != 0,
                               hints->literal_anchors))
                return RX_NO_MATCH;
        if (hints->extra_length && !start && !match->literal_known && !match->work_yield &&
            !text_literal_find(bytes, length, 0, (string_address)hints->extra, hints->extra_length,
                               (program->flags & RX_IGNORE_CASE) != 0, hints->extra_anchors))
                return RX_NO_MATCH;
        /*
                A pattern that begins with a run of one set, [a-z]+ or
                " *", that has tried a start and failed has tried every
                start inside the run: from a later one the run is shorter,
                and each place the shorter run could hand on from was one
                the longer run handed on from. The next start is the byte
                that ended the run, not the next of its bytes, and a word of
                n letters was n starts of n steps.
        */
        const rx_node *run = hints->lead_run && program->boundary == REGEX_BOUNDARY_NONE &&
                                     mode != REGEX_EXACT_LONGEST
                                 ? program->nodes + hints->lead_run : null;
        if (hints->shape == 3 && program->boundary == REGEX_BOUNDARY_NONE && mode != REGEX_EXACT_LONGEST)
        {
                positive size = hints->literal_length;

                // ^X$ is the whole text and X$ the end of it; neither is anywhere else.
                if (length < size || length - size < start || (hints->shape_minimum && (start || length != size)))
                        return RX_NO_MATCH;
                if (program->flags & RX_IGNORE_CASE)
                {
                        for (positive i = 0; i < size; i++)
                                if (byte_to_lower(bytes[length - size + i]) != hints->literal[i])
                                        return RX_NO_MATCH;
                }
                else if (memory_compare(bytes + length - size, hints->literal, size))
                        return RX_NO_MATCH;
                memory_fill(match->slots, -1, match->active_captures * sizeof(positive));
                match->slots[0] = length - size;
                match->slots[1] = length;
                return RX_MATCH;
        }
        if (hints->shape && hints->shape != 3 && program->boundary == REGEX_BOUNDARY_NONE &&
            mode != REGEX_EXACT_LONGEST)
        {
                const rx_node *one = program->nodes + hints->shape_node;
                positive at = start;

                if (hints->shape == 2 && !hints->shape_minimum)
                {
                        match->slots[0] = at;
                        match->slots[1] = at + rx_run_span(program, one, bytes + at, length - at);
                        return RX_MATCH;
                }
                for (;;)
                {
                        if (program->flags & RX_FIRST_KNOWN)
                                at += string_span_max(bytes + at, length - at, (const b8 *)hints->first_skip);
                        else
                                while (at < length && !rx_single(program, one, bytes[at]))
                                        at++;
                        if (at >= length)
                                return RX_NO_MATCH;
                        positive taken = hints->shape == 1 ? 1 : rx_run_span(program, one, bytes + at, length - at);

                        if (taken >= (hints->shape == 1 ? 1 : (positive)hints->shape_minimum))
                        {
                                match->slots[0] = at;
                                match->slots[1] = at + taken;
                                return RX_MATCH;
                        }
                        at += taken;
                }
        }
        /*
                And when nothing but the end of the text follows the run,
                " *$" or "[ \t]*$", it is the only place a match can be: the
                last byte that is not the run's, and the run after it.
                Every earlier start was a try that could only fail.
        */
        if (run && hints->lead_tail)
        {
                positive reach = length;

                while (reach > start && rx_single(program, run, bytes[reach - 1]))
                        reach--;
                if (length - reach < (positive)program->nodes[program->first].minimum)
                        return RX_NO_MATCH;
                memory_fill(match->slots, -1, match->active_captures * sizeof(positive));
                match->slots[0] = reach;
                match->slots[1] = length;
                return RX_MATCH;
        }
        /*
                Where a machine stands by (grep's, whose caller gave the walk
                a ceiling to spend before the machine reads the line), the
                walk does not take the short ways through a run: what it
                spends is how the caller knows to hand the line over, and
                the machine reads a line faster than a try at every start
                of a run does, shortcut or not.
        */
        if (match->work_yield)
                run = null;
        const rx_node *follow = run && hints->lead_next ? program->nodes + hints->lead_next : null;
        for (positive at = start; at <= length; at++)
        {
                if (program->boundary == REGEX_BOUNDARY_WORD && at && mode != REGEX_EXACT_LONGEST)
                        at += string_span_max(bytes + at, length - at, string_set_name);
                if ((program->flags & RX_FIRST_KNOWN) && !program->boundary && mode != REGEX_EXACT_LONGEST)
                {
                        at += string_span_max(bytes + at, length - at, (const b8 *)program->hints->first_skip);
                        if (at == length)
                                return RX_NO_MATCH;
                }
                /*
                        A run that nothing in it can be followed from, and
                        the byte after it is not what must follow, has no
                        start in it that goes anywhere: the word that is not
                        followed by "=" is passed in the one look, not tried
                        by a walk and its choices.
                */
                if (follow)
                {
                        positive taken = rx_run_span(program, run, bytes + at, length - at);

                        if (taken < (positive)hints->lead_minimum || at + taken >= length ||
                            !rx_single(program, follow, bytes[at + taken]))
                        {
                                if (taken > 1)
                                        at += taken - 1;
                                continue;
                        }
                }
                match->best_stop = positive_max;
                bool found = false;
                if (program->boundary == REGEX_BOUNDARY_NONE || !at)
                        found = rx_run(match, at);
                if (!found && program->boundary == REGEX_BOUNDARY_WORD && at < length && !string_set_name[bytes[at]])
                        found = rx_run(match, at + 1);
                if (match->best_stop != positive_max)
                {
                        if (!match->best_in_place)
                                memory_copy_apart(match->slots, match->best_slots,
                                                  match->active_captures * sizeof(positive));
                        found = true;
                }
                /*
                        A longest search that ran out of work after it found a
                        match has not seen every alternative: the match it has
                        may be shorter than the longest, so it is not an answer.
                */
                if (found && match->pending_exhaustion && match->selection == REGEX_LONGEST)
                {
                        match->pending_exhaustion = false;
                        return RX_COMPLEX;
                }
                if (found && (mode != REGEX_EXACT_LONGEST || !match->first_exhausted))
                        return RX_MATCH;
                if (!found && mode == REGEX_EXACT_LONGEST)
                        return RX_NO_MATCH;
                if (match->pending_exhaustion)
                {
                        match->pending_exhaustion = false;
                        return RX_COMPLEX;
                }
                if (program->boundary == REGEX_BOUNDARY_LINE ||
                    ((program->flags & RX_ANCHORED) && program->boundary != REGEX_BOUNDARY_WORD))
                        return RX_NO_MATCH;
                if (run && at < length && rx_single(program, run, bytes[at]))
                {
                        positive taken = rx_run_span(program, run, bytes + at, length - at);

                        if (taken > 1)
                                at += taken - 1;
                }
        }
        return RX_NO_MATCH;
}

/*
        A deterministic machine built as it is needed, for the one question
        grep asks of a record that no fixed string answers: does anything in
        it match.

        The graph above answers that by trying starts and backtracking, which
        is exact and bounded by work rather than by time; over a span of many
        records it is a run per record and sometimes per start. Here the
        graph is first unrolled into a Thompson automaton -- counted
        repetitions copied out, alternation and groups as splits -- and sets
        of its states become the states of a machine that reads each byte
        once. Only the transitions that bytes actually take are ever made,
        so a machine that would be large in principle stays the size of the
        input's habits.

        Bytes are read by class: two bytes every set treats alike are one
        column. The delimiter has a column of its own, which is where a
        record is judged at its end, and name bytes are kept apart from the
        rest whenever \b, \<, \> or -w can ask about them.

        What would need a look ahead is settled one byte late. A state holds
        the automaton's positions before any assertion is followed, with
        whether the byte before was a name byte and whether this is the
        record's first position; the transition on the next byte knows that
        byte's class and so knows every assertion, follows them, notes a
        match that ended before the byte, and only then consumes it. A match
        at the end of a record is the delimiter's transition.

        Starts are added back after every byte, as the graph's search tries
        every position; under -w only after a byte that is not a name byte,
        and under -x never, which is the same set of starts rx_find tries.

        Backreferences are not a regular language and keep the graph. So
        does an automaton that will not fit, and a machine whose states keep
        outgrowing the cache: it is emptied and begun again from where it
        stands a bounded number of times, and past that the caller asks the
        graph instead.
*/
#define RX_DFA_NFA_MAX 4096
#define RX_DFA_ORDER_MAX 8192
#define RX_DFA_SET_MAX 128
/* A state's transitions are a row of one cell a class, so the cells bound
   the states along with the arrays: 2,048 of them for a pattern that tells
   256 kinds of byte apart, and the arrays' 16,384 for one that tells 32 or
   fewer. (a|b)*a(a|b){12} is 8,192 states, which was the graph's
   backtracking at a second a megabyte and is one pass over the text. The
   states are found by a hash that begins at RX_DFA_HASH slots and doubles
   while there are more than half as many states as slots, so a pattern of a
   few states clears what it did before. */
#define RX_DFA_CELLS (2048 * 256)
#define RX_DFA_STATE_MAX 16384
#define RX_DFA_POOL_MAX 262144
#define RX_DFA_HASH 8192
#define RX_DFA_HASH_MAX (RX_DFA_STATE_MAX * 2)
#define RX_DFA_RESETS_MAX 64
#define RX_DFA_DEPTH_MAX 256
/* Calls the NFA build may make. Nodes are capped, but a count over a body
   that makes none -- (){32767} -- costs a call per repeat and nests: three
   levels was 32767^3 of them before a byte of data was read. */
#define RX_DFA_WORK_MAX (1u << 17)

enum { RX_NFA_SET = 1, RX_NFA_SPLIT, RX_NFA_BEGIN, RX_NFA_END, RX_NFA_EDGE, RX_NFA_MATCH };
enum { RX_DFA_UNKNOWN = -1, RX_DFA_HIT = -2, RX_DFA_DEAD = -3,
       RX_DFA_END_HIT = -4, RX_DFA_FULL = -6 };
enum { RX_DFA_PREVIOUS_NAME = 1, RX_DFA_BEGINNING = 2 };
enum { RX_ACCEPT_NAME = 1, RX_ACCEPT_OTHER = 2, RX_ACCEPT_END = 4 };
enum { RX_DFA_RESTART_ALWAYS, RX_DFA_RESTART_AFTER_OTHER, RX_DFA_RESTART_NEVER };

typedef struct
{
        p8 kind;
        // The set for RX_NFA_SET, the edge for RX_NFA_EDGE.
        p8 argument;
        p16 out, out1;
} rx_nfa;

typedef struct
{
        rx_nfa nfa[RX_DFA_NFA_MAX];
        p8 sets[RX_DFA_SET_MAX][256];
        // A byte's class, a class's first byte, and whether it is a name byte.
        p8 classes[256];
        p8 representative[256];
        p8 name[256];
        rx_ref order[RX_DFA_ORDER_MAX];
        positive nfa_count, set_count, class_count, order_top, work;
        p16 start;
        p8 boundary, delimiter, delimiter_class;
        // Starts after the first position: every one, only after a byte that
        // is not a name byte, or none -- -x, and a program that begins with ^,
        // which rx_find tries at nought and nowhere else.
        p8 restart;
        bool usable;
        // A longest machine reads on past a match, which its states' accept
        // bits (rx_dfa_cache.accept) say where it was, and is anchored.
        bool longest;
} rx_dfa;

typedef struct
{
        const rx_dfa *dfa;
        b32 trans[RX_DFA_CELLS];
        p32 set_at[RX_DFA_STATE_MAX];
        // Where the state hashed to, kept for the hash to be made over again.
        p32 hashed[RX_DFA_STATE_MAX];
        p16 set_size[RX_DFA_STATE_MAX];
        p8 flags[RX_DFA_STATE_MAX];
        // For a longest machine: a match ends before a byte of a name, before
        // one that is not, and at the end of the record.
        p8 accept[RX_DFA_STATE_MAX];
        p16 pool[RX_DFA_POOL_MAX];
        p32 hash[RX_DFA_HASH_MAX];
        p32 mark[RX_DFA_NFA_MAX];
        p64 bits[RX_DFA_NFA_MAX / 64];
        p16 stack[RX_DFA_NFA_MAX];
        p16 found[RX_DFA_NFA_MAX];
        positive pool_used, state_count, state_cap, hash_slots, generation, resets;
        b32 start;
        bool failed;
} rx_dfa_cache;

static p16 rx_nfa_new(rx_dfa *dfa, p8 kind, p8 argument, p16 out, p16 out1)
{
        if (dfa->nfa_count == RX_DFA_NFA_MAX)
        {
                dfa->usable = false;
                return 0;
        }
        p16 at = (p16)dfa->nfa_count++;
        dfa->nfa[at] = (rx_nfa){kind, argument, out, out1};
        return at;
}

static p8 rx_nfa_set(rx_dfa *dfa, const p8 *table)
{
        for (positive i = 0; i < dfa->set_count; i++)
                if (!memory_compare(dfa->sets[i], table, 256))
                        return (p8)i;
        if (dfa->set_count == RX_DFA_SET_MAX)
        {
                dfa->usable = false;
                return 0;
        }
        memory_copy_apart(dfa->sets[dfa->set_count], table, 256);
        return (p8)dfa->set_count++;
}

/* The sequence from `first` along `next`, ending in `follow`, built from its
   last node back so every piece already knows where it goes. */
static p16 rx_nfa_build(rx_dfa *dfa, const regex_program *program, rx_ref first,
                        p16 follow, b32 depth)
{
        const rx_node *nodes = program->nodes;
        if (++dfa->work > RX_DFA_WORK_MAX)
                dfa->usable = false;
        if (!first || !dfa->usable)
                return follow;
        if (depth > RX_DFA_DEPTH_MAX)
        {
                dfa->usable = false;
                return follow;
        }
        positive base = dfa->order_top;
        for (rx_ref at = first; at; at = nodes[at].next)
        {
                if (dfa->order_top == RX_DFA_ORDER_MAX)
                {
                        dfa->usable = false;
                        return follow;
                }
                dfa->order[dfa->order_top++] = at;
        }
        for (positive k = dfa->order_top; k > base && dfa->usable; k--)
        {
                const rx_node *node = nodes + dfa->order[k - 1];
                p8 table[256];
                switch (node->kind)
                {
                case RX_BYTE:
                        memory_fill(table, 0, 256);
                        table[node->argument] = 1;
                        if ((program->flags & RX_IGNORE_CASE) && byte_is_alpha(node->argument))
                                table[node->argument ^ 32] = 1;
                        follow = rx_nfa_new(dfa, RX_NFA_SET, rx_nfa_set(dfa, table), follow, 0);
                        break;
                case RX_ANY:
                        memory_fill(table, 1, 256);
                        if (!(program->policy & REGEX_DOT_NEWLINE))
                                table['\n'] = 0;
                        follow = rx_nfa_new(dfa, RX_NFA_SET, rx_nfa_set(dfa, table), follow, 0);
                        break;
                case RX_SET:
                        follow = rx_nfa_new(dfa, RX_NFA_SET,
                                            rx_nfa_set(dfa, program->sets[node->argument]), follow, 0);
                        break;
                case RX_BEGIN:
                        follow = rx_nfa_new(dfa, RX_NFA_BEGIN, 0, follow, 0);
                        break;
                case RX_END:
                        follow = rx_nfa_new(dfa, RX_NFA_END, 0, follow, 0);
                        break;
                case RX_EDGE:
                        follow = rx_nfa_new(dfa, RX_NFA_EDGE, node->argument, follow, 0);
                        break;
                case RX_CAPTURE:
                        follow = rx_nfa_build(dfa, program, node->left, follow, depth + 1);
                        break;
                case RX_ALT:
                {
                        p16 left = rx_nfa_build(dfa, program, node->left, follow, depth + 1);
                        p16 right = rx_nfa_build(dfa, program, node->right, follow, depth + 1);
                        follow = rx_nfa_new(dfa, RX_NFA_SPLIT, 0, left, right);
                        break;
                }
                case RX_COUNT:
                {
                        p16 after = follow;
                        b32 low = node->minimum, high = node->maximum;
                        if (high < 0)
                        {
                                p16 loop = rx_nfa_new(dfa, RX_NFA_SPLIT, 0, 0, after);
                                p16 body = rx_nfa_build(dfa, program, node->left, loop, depth + 1);
                                if (dfa->usable)
                                        dfa->nfa[loop].out = body;
                                follow = loop;
                        }
                        else
                                for (b32 i = low; i < high && dfa->usable; i++)
                                {
                                        p16 body = rx_nfa_build(dfa, program, node->left, follow, depth + 1);
                                        follow = rx_nfa_new(dfa, RX_NFA_SPLIT, 0, body, after);
                                }
                        for (b32 i = 0; i < low && dfa->usable; i++)
                                follow = rx_nfa_build(dfa, program, node->left, follow, depth + 1);
                        break;
                }
                default:
                        dfa->usable = false;
                        break;
                }
        }
        dfa->order_top = base;
        return follow;
}

/* Split the classes by one more property of a byte. */
static fn rx_dfa_refine(rx_dfa *dfa, const p8 *member)
{
        p16 map[512];
        p8 classes[256];
        positive count = 0;
        memory_fill(map, 0xff, sizeof(map));
        for (b32 byte = 0; byte < 256; byte++)
        {
                p16 key = (p16)(dfa->classes[byte] * 2 + (member[byte] != 0));
                if (map[key] == 0xffff)
                        map[key] = (p16)count++;
                classes[byte] = (p8)map[key];
        }
        memory_copy_apart(dfa->classes, classes, 256);
        dfa->class_count = count;
}

static bool rx_dfa_compile(rx_dfa *dfa, const regex_program *program, p8 boundary,
                           p8 delimiter)
{
        dfa->usable = !(program->flags & RX_HAS_BACKREF);
        dfa->longest = false;
        dfa->nfa_count = 1;
        dfa->set_count = 0;
        dfa->order_top = 0;
        dfa->work = 0;
        dfa->boundary = boundary;
        dfa->delimiter = delimiter;
        dfa->restart = boundary == REGEX_BOUNDARY_WORD ? RX_DFA_RESTART_AFTER_OTHER
                     : boundary == REGEX_BOUNDARY_LINE || (program->flags & RX_ANCHORED)
                           ? RX_DFA_RESTART_NEVER
                           : RX_DFA_RESTART_ALWAYS;
        if (!dfa->usable)
                return false;
        p16 match = rx_nfa_new(dfa, RX_NFA_MATCH, 0, 0, 0);
        dfa->start = rx_nfa_build(dfa, program, program->first, match, 0);
        if (!dfa->usable)
                return false;
        memory_fill(dfa->classes, 0, 256);
        dfa->class_count = 1;
        for (positive i = 0; i < dfa->set_count; i++)
                rx_dfa_refine(dfa, dfa->sets[i]);
        rx_dfa_refine(dfa, (const p8 *)string_set_name);
        p8 alone[256];
        memory_fill(alone, 0, 256);
        alone[delimiter] = 1;
        rx_dfa_refine(dfa, alone);
        for (b32 byte = 255; byte >= 0; byte--)
        {
                dfa->representative[dfa->classes[byte]] = (p8)byte;
                dfa->name[dfa->classes[byte]] = string_set_name[byte] != 0;
        }
        dfa->delimiter_class = dfa->classes[delimiter];
        return true;
}

static fn rx_dfa_begin(rx_dfa_cache *cache);
static bool rx_dfa_close(rx_dfa_cache *cache, b32 state, bool before, bool after,
                         bool ending, positive *consuming);

/* A state for the positions in cache->bits; interned, and the cache begun
   again when it has no room for one more. */
static b32 rx_dfa_intern(rx_dfa_cache *cache, p8 flags, bool *reset)
{
        const rx_dfa *dfa = cache->dfa;
        positive size = 0;
        p64 hash = 1469598103934665603ull ^ flags;
        for (positive word = 0; word < RX_DFA_NFA_MAX / 64; word++)
                for (p64 bits = cache->bits[word]; bits; bits &= bits - 1)
                {
                        p16 id = (p16)(word * 64 + bottom_bit_known(bits));
                        cache->found[size++] = id;
                        hash = (hash ^ id) * 1099511628211ull;
                }
        p32 where = (p32)(hash >> 20);
        positive slot = where & (cache->hash_slots - 1);
        for (;;)
        {
                p32 entry = cache->hash[slot];
                if (!entry)
                        break;
                b32 state = (b32)entry - 1;
                if (cache->flags[state] == flags && cache->set_size[state] == size &&
                    !memory_compare(cache->pool + cache->set_at[state], cache->found,
                                    size * sizeof(p16)))
                        return state;
                slot = (slot + 1) & (cache->hash_slots - 1);
        }
        if (cache->state_count == cache->state_cap ||
            cache->pool_used + size > RX_DFA_POOL_MAX)
        {
                if (*reset || ++cache->resets > RX_DFA_RESETS_MAX)
                {
                        cache->failed = true;
                        return RX_DFA_FULL;
                }
                *reset = true;
                rx_dfa_begin(cache);
                return rx_dfa_intern(cache, flags, reset);
        }
        if ((cache->state_count + 1) * 2 > cache->hash_slots &&
            cache->hash_slots < RX_DFA_HASH_MAX)
        {
                cache->hash_slots *= 2;
                memory_fill(cache->hash, 0, cache->hash_slots * sizeof(p32));
                for (positive again = 0; again < cache->state_count; again++)
                {
                        positive place = cache->hashed[again] & (cache->hash_slots - 1);
                        while (cache->hash[place])
                                place = (place + 1) & (cache->hash_slots - 1);
                        cache->hash[place] = (p32)again + 1;
                }
                slot = where & (cache->hash_slots - 1);
                while (cache->hash[slot])
                        slot = (slot + 1) & (cache->hash_slots - 1);
        }
        b32 state = (b32)cache->state_count++;
        cache->set_at[state] = (p32)cache->pool_used;
        cache->hashed[state] = where;
        cache->set_size[state] = (p16)size;
        cache->flags[state] = flags;
        memory_copy_apart(cache->pool + cache->pool_used, cache->found, size * sizeof(p16));
        cache->pool_used += size;
        cache->hash[slot] = (p32)state + 1;
        for (positive c = 0; c < dfa->class_count; c++)
                cache->trans[(positive)state * dfa->class_count + c] = RX_DFA_UNKNOWN;
        if (dfa->longest)
        {
                positive consuming;
                bool before = (flags & RX_DFA_PREVIOUS_NAME) != 0;
                cache->accept[state] = (p8)((rx_dfa_close(cache, state, before, true, false, &consuming) ? RX_ACCEPT_NAME : 0) |
                                            (rx_dfa_close(cache, state, before, false, false, &consuming) ? RX_ACCEPT_OTHER : 0) |
                                            (rx_dfa_close(cache, state, before, false, true, &consuming) ? RX_ACCEPT_END : 0));
        }
        return state;
}

static fn rx_dfa_begin(rx_dfa_cache *cache)
{
        cache->state_count = 0;
        cache->pool_used = 0;
        cache->hash_slots = RX_DFA_HASH;
        cache->state_cap = min((positive)RX_DFA_STATE_MAX,
                               (positive)RX_DFA_CELLS / cache->dfa->class_count);
        memory_fill(cache->hash, 0, RX_DFA_HASH * sizeof(p32));
        // The start is the one set a caller holds across a reset.
        p64 saved[RX_DFA_NFA_MAX / 64];
        memory_copy_apart(saved, cache->bits, sizeof(saved));
        memory_fill(cache->bits, 0, sizeof(cache->bits));
        cache->bits[cache->dfa->start / 64] |= 1ull << (cache->dfa->start % 64);
        bool reset = true;
        cache->start = rx_dfa_intern(cache, RX_DFA_BEGINNING, &reset);
        memory_copy_apart(cache->bits, saved, sizeof(saved));
}

static fn rx_dfa_attach(rx_dfa_cache *cache, const rx_dfa *dfa)
{
        cache->dfa = dfa;
        cache->resets = 0;
        cache->failed = !dfa->usable;
        cache->generation = 0;
        memory_fill(cache->mark, 0, sizeof(cache->mark));
        memory_fill(cache->bits, 0, sizeof(cache->bits));
        if (dfa->usable)
                rx_dfa_begin(cache);
}

/* Follow every assertion the context allows from a state's positions:
   consuming positions go to cache->found, and the answer is whether the
   automaton's end was reached. */
static bool rx_dfa_close(rx_dfa_cache *cache, b32 state, bool before, bool after,
                         bool ending, positive *consuming)
{
        const rx_dfa *dfa = cache->dfa;
        static const p8 edge_masks[] = {6, 9, 2, 4};
        bool beginning = (cache->flags[state] & RX_DFA_BEGINNING) != 0;
        bool matched = false;
        positive top = 0, count = 0;
        if (++cache->generation == 0)
        {
                memory_fill(cache->mark, 0, sizeof(cache->mark));
                cache->generation = 1;
        }
        for (positive i = 0; i < cache->set_size[state]; i++)
                cache->stack[top++] = cache->pool[cache->set_at[state] + i];
        while (top)
        {
                p16 id = cache->stack[--top];
                if (cache->mark[id] == cache->generation)
                        continue;
                cache->mark[id] = (p32)cache->generation;
                const rx_nfa *nfa = dfa->nfa + id;
                switch (nfa->kind)
                {
                case RX_NFA_SET:
                        cache->found[count++] = id;
                        break;
                case RX_NFA_SPLIT:
                        cache->stack[top++] = nfa->out1;
                        cache->stack[top++] = nfa->out;
                        break;
                case RX_NFA_BEGIN:
                        if (beginning)
                                cache->stack[top++] = nfa->out;
                        break;
                case RX_NFA_END:
                        if (ending)
                                cache->stack[top++] = nfa->out;
                        break;
                case RX_NFA_EDGE:
                        if (edge_masks[nfa->argument] & (1u << (before * 2 + after)))
                                cache->stack[top++] = nfa->out;
                        break;
                case RX_NFA_MATCH:
                        matched = true;
                        break;
                }
        }
        *consuming = count;
        return matched;
}

/* The transition on one class from the state whose row starts at `row`,
   made and remembered. */
static b32 rx_dfa_step(rx_dfa_cache *cache, b32 row, p8 class)
{
        const rx_dfa *dfa = cache->dfa;
        b32 state = row / (b32)dfa->class_count;
        bool before = (cache->flags[state] & RX_DFA_PREVIOUS_NAME) != 0;
        positive cell = (positive)row + class;
        positive consuming;
        b32 next;
        if (class == dfa->delimiter_class)
        {
                // A record that ends unmatched begins the next: the cell is the
                // start's own row, and the walk never stops for one.
                next = rx_dfa_close(cache, state, before, false, true, &consuming)
                           ? RX_DFA_END_HIT : cache->start * (b32)dfa->class_count;
                cache->trans[cell] = next;
                return next;
        }
        bool after = dfa->name[class];
        bool matched = rx_dfa_close(cache, state, before, after, false, &consuming);
        if (matched && !dfa->longest && dfa->boundary != REGEX_BOUNDARY_LINE &&
            (dfa->boundary != REGEX_BOUNDARY_WORD || !after))
        {
                cache->trans[cell] = RX_DFA_HIT;
                return RX_DFA_HIT;
        }
        p8 byte = dfa->representative[class];
        memory_fill(cache->bits, 0, sizeof(cache->bits));
        bool any = false;
        for (positive i = 0; i < consuming; i++)
        {
                const rx_nfa *nfa = dfa->nfa + cache->found[i];
                if (dfa->sets[nfa->argument][byte])
                {
                        cache->bits[nfa->out / 64] |= 1ull << (nfa->out % 64);
                        any = true;
                }
        }
        if (dfa->restart == RX_DFA_RESTART_ALWAYS ||
            (dfa->restart == RX_DFA_RESTART_AFTER_OTHER && !after))
        {
                cache->bits[dfa->start / 64] |= 1ull << (dfa->start % 64);
                any = true;
        }
        // Nothing left and nothing to come is the rest of the record skipped.
        // Under -w nothing left is still waiting for the next byte that is
        // not a name byte, and is a state like any other.
        if (!any && dfa->restart == RX_DFA_RESTART_NEVER)
        {
                cache->trans[cell] = RX_DFA_DEAD;
                return RX_DFA_DEAD;
        }
        bool reset = false;
        next = rx_dfa_intern(cache, after ? RX_DFA_PREVIOUS_NAME : 0, &reset);
        if (next < 0)
                return next;
        // A row, not a state: the scan adds a class to it and never multiplies.
        next *= (b32)dfa->class_count;
        // A reset emptied every row, this state's among them.
        if (!reset)
                cache->trans[cell] = next;
        return next;
}

/* The first record in [at, past) that matches, as the address of the
   delimiter that ends it; every record there ends with one. Null when none
   does, and null with cache->failed when the machine would not fit. */
static string_address rx_dfa_scan(rx_dfa_cache *cache, string_address at,
                                  string_address past)
{
        const rx_dfa *dfa = cache->dfa;
        const p8 *classes = dfa->classes;
        const b32 *trans = cache->trans;
        b32 start = cache->start * (b32)dfa->class_count;
        b32 row = start;
        for (;;)
        {
                // The whole of the work, while nothing needs deciding.
                b32 next = 0;
                while (at < past && (next = trans[row + classes[*at]]) >= 0)
                {
                        row = next;
                        at++;
                }
                if (at == past)
                        return null;
                if (next == RX_DFA_UNKNOWN)
                {
                        next = rx_dfa_step(cache, row, classes[*at]);
                        if (next == RX_DFA_FULL)
                                return null;
                        // A reset moves the start, which every record needs.
                        start = cache->start * (b32)dfa->class_count;
                        if (next >= 0)
                        {
                                row = next;
                                at++;
                                continue;
                        }
                }
                if (next == RX_DFA_END_HIT)
                        return at;
                string_address stop = (string_address)memory_first_of(
                    at, dfa->delimiter, (positive)(past - at));
                if (!stop)
                        return next == RX_DFA_HIT ? past - 1 : null;
                if (next == RX_DFA_HIT)
                        return stop;
                at = stop + 1;
                row = start;
        }
}

/*
        The same walk over four stretches of records at once. One byte of the
        walk waits for the load that named its row, so a walk alone is
        latency and not work (six and a half cycles a byte); four walks that
        share nothing keep four loads in flight and cost about a quarter each.

        The span is cut into four at record ends, each stretch walked from
        its first byte with its own row, and the walks are stepped together
        for as long as all four have bytes and none needs deciding. What
        needs deciding is what the serial walk decides -- a cell not yet
        made, a record that matched, a record the machine has finished with
        -- and a stretch that has to is dealt with alone before the four go
        on. A cell that made a reset empties every row, so every stretch
        begins again at its own record's first byte. The stretches are in
        order, so what they find is in order too.

        Counting takes any length. With `out` the delimiters of the records
        that matched are kept, per stretch, and the span is cut short to
        RX_DFA_LANE_BLOCK bytes a stretch, `*block` telling where. Positive
        maximum is the answer when the machine gave up (cache->failed) or a
        stretch matched more records than a table holds; the serial walk
        starts again where the caller was.
*/
#define RX_DFA_LANES 4
#define RX_DFA_LANE_BLOCK 16384
#define RX_DFA_LANE_HITS 512

typedef struct
{
        positive count[RX_DFA_LANES];
        // Offsets from the start of the block.
        p32 at[RX_DFA_LANES][RX_DFA_LANE_HITS];
} rx_dfa_hits;

typedef struct
{
        string_address at, past, begin;
        b32 row;
} rx_lane;

enum { RX_LANE_GO, RX_LANE_RESET, RX_LANE_GAVE_UP };

/* What the serial walk does with a cell that is not a row, for the stretch
   whose next byte is at lane->at. A record that matched is `*hit`. */
static int rx_lane_event(rx_dfa_cache *cache, rx_lane *lane, b32 next,
                         string_address *hit)
{
        const rx_dfa *dfa = cache->dfa;
        b32 start = cache->start * (b32)dfa->class_count;
        *hit = null;
        if (next == RX_DFA_UNKNOWN)
        {
                positive resets = cache->resets;
                next = rx_dfa_step(cache, lane->row, dfa->classes[*lane->at]);
                if (next == RX_DFA_FULL)
                        return RX_LANE_GAVE_UP;
                if (cache->resets != resets)
                        return RX_LANE_RESET;
                if (next >= 0)
                {
                        lane->row = next;
                        lane->at++;
                        return RX_LANE_GO;
                }
                start = cache->start * (b32)dfa->class_count;
        }
        if (next == RX_DFA_END_HIT)
        {
                *hit = lane->at++;
                lane->row = start;
                return RX_LANE_GO;
        }
        string_address stop = (string_address)memory_first_of(
            lane->at, dfa->delimiter, (positive)(lane->past - lane->at));
        if (next == RX_DFA_HIT)
                *hit = stop ? stop : lane->past - 1;
        lane->at = stop ? stop + 1 : lane->past;
        lane->row = start;
        return RX_LANE_GO;
}

static positive rx_dfa_lanes(rx_dfa_cache *cache, string_address at,
                             string_address past, rx_dfa_hits *out,
                             string_address *block)
{
        const rx_dfa *dfa = cache->dfa;
        const p8 *classes = dfa->classes;
        const b32 *trans = cache->trans;
        const string_address base = at;
        rx_lane lane[RX_DFA_LANES];
        positive total = 0;
        if (out)
        {
                memory_fill(out->count, 0, sizeof(out->count));
                if ((positive)(past - at) > RX_DFA_LANES * RX_DFA_LANE_BLOCK)
                {
                        string_address from = at + RX_DFA_LANES * RX_DFA_LANE_BLOCK - 1;
                        string_address stop = (string_address)memory_first_of(
                            from, dfa->delimiter, (positive)(past - from));
                        past = stop ? stop + 1 : past;
                }
        }
        *block = past;
        b32 start = cache->start * (b32)dfa->class_count;
        string_address cut = at;
        for (positive k = 0; k < RX_DFA_LANES; k++)
        {
                string_address next = past;
                if (k < RX_DFA_LANES - 1)
                {
                        string_address target = at + (positive)(past - at) / RX_DFA_LANES * (k + 1);
                        if (target < cut)
                                target = cut;
                        string_address stop = (string_address)memory_first_of(
                            target, dfa->delimiter, (positive)(past - target));
                        next = stop ? stop + 1 : past;
                }
                lane[k] = (rx_lane){.at = cut, .past = next, .begin = cut, .row = start};
                cut = next;
        }
        for (;;)
        {
                positive m = positive_max, live = 0;
                for (positive k = 0; k < RX_DFA_LANES; k++)
                {
                        positive left = (positive)(lane[k].past - lane[k].at);
                        m = left < m ? left : m;
                        live += left != 0;
                }
                if (!live)
                        break;
                // The lane that walks alone while another has run out, or the
                // four while every one has bytes.
                positive one = 0, mask = (1u << RX_DFA_LANES) - 1;
                b32 n[RX_DFA_LANES] = {0};
                if (m)
                {
                        string_address a0 = lane[0].at, a1 = lane[1].at, a2 = lane[2].at,
                                       a3 = lane[3].at;
                        b32 r0 = lane[0].row, r1 = lane[1].row, r2 = lane[2].row,
                            r3 = lane[3].row;
                        b32 n0 = 0, n1 = 0, n2 = 0, n3 = 0;
                        bool stopped = false;
                        while (m--)
                        {
                                n0 = trans[r0 + classes[*a0]];
                                n1 = trans[r1 + classes[*a1]];
                                n2 = trans[r2 + classes[*a2]];
                                n3 = trans[r3 + classes[*a3]];
                                if ((n0 | n1 | n2 | n3) < 0)
                                {
                                        stopped = true;
                                        break;
                                }
                                a0++, a1++, a2++, a3++;
                                r0 = n0, r1 = n1, r2 = n2, r3 = n3;
                        }
                        lane[0].at = a0, lane[1].at = a1, lane[2].at = a2, lane[3].at = a3;
                        lane[0].row = r0, lane[1].row = r1, lane[2].row = r2, lane[3].row = r3;
                        if (!stopped)
                                continue;
                        n[0] = n0, n[1] = n1, n[2] = n2, n[3] = n3;
                }
                else
                {
                        while (lane[one].at == lane[one].past)
                                one++;
                        rx_lane *l = lane + one;
                        b32 next = 0;
                        while (l->at < l->past && (next = trans[l->row + classes[*l->at]]) >= 0)
                        {
                                l->row = next;
                                l->at++;
                        }
                        if (l->at == l->past)
                                continue;
                        n[one] = next;
                        mask = 1u << one;
                }
                bool reset = false;
                for (positive k = 0; k < RX_DFA_LANES && !reset; k++)
                {
                        rx_lane *l = lane + k;
                        string_address hit;
                        if (!(mask >> k & 1))
                                continue;
                        if (n[k] >= 0)
                        {
                                l->row = n[k];
                                l->at++;
                                continue;
                        }
                        int event = rx_lane_event(cache, l, n[k], &hit);
                        if (event == RX_LANE_GAVE_UP)
                                return positive_max;
                        reset = event == RX_LANE_RESET;
                        if (!hit)
                                continue;
                        total++;
                        if (out)
                        {
                                if (out->count[k] == RX_DFA_LANE_HITS)
                                        return positive_max;
                                out->at[k][out->count[k]++] = (p32)(hit - base);
                        }
                }
                if (reset)
                {
                        start = cache->start * (b32)dfa->class_count;
                        for (positive k = 0; k < RX_DFA_LANES; k++)
                        {
                                rx_lane *l = lane + k;
                                if (l->at == l->past)
                                        continue;
                                string_address before = (string_address)memory_last_of(
                                    l->begin, dfa->delimiter, (positive)(l->at - l->begin));
                                l->at = before ? before + 1 : l->begin;
                                l->row = start;
                        }
                }
        }
        return total;
}

#define REGEX_SCRATCH_MAX 20000

static rx_pool address_to regex_pool_held;
static regex_program regex_current;
static rx_frame (address_to regex_frames_held)[REGEX_SCRATCH_MAX];
#define regex_frames UTILITY_HELD(regex_frames)
static rx_choice (address_to regex_choices_held)[REGEX_SCRATCH_MAX];
#define regex_choices UTILITY_HELD(regex_choices)
static rx_undo (address_to regex_undo_held)[REGEX_SCRATCH_MAX];
#define regex_undo UTILITY_HELD(regex_undo)
//      Its scratch is held, with the pool, at the first use of either.
static rx_match regex_match = {
    .frame_capacity = REGEX_SCRATCH_MAX, .choice_capacity = REGEX_SCRATCH_MAX,
    .undo_capacity = REGEX_SCRATCH_MAX,
    .work_limit = 100000000,
};

static COLD fn rx_match_scratch(rx_match *match)
{
        match->frames = regex_frames;
        match->choices = regex_choices;
        match->undo = regex_undo;
}

/* Every match runs a program compiled into the pool, so the pool's first
   use is where the scratch the matcher backtracks in is put in place:
   once, and nowhere a line or a starting position pays for it. */
static inline rx_pool address_to regex_pool_ready(void)
{
        if (unlikely(!regex_match.frames))
                rx_match_scratch(&regex_match);
        return &UTILITY_HELD(regex_pool);
}
#define regex_pool (*regex_pool_ready())

/*
        The deterministic machine, and the states it has learned.

        It lives here rather than beside grep because grep is not the only
        caller that could use one: every utility that asks this file a
        question shares one automaton and one cache, and a second pair would
        be another two and a half megabytes of nothing for whichever of them
        is not running. No two of them run in one process at a time, so the
        pair is only ever built for one program; which program that is, is
        remembered beside it so a caller that finds somebody else's
        automaton loaded knows to build its own.
*/
static rx_dfa regex_dfa;
static rx_dfa_cache address_to regex_dfa_cache_held;
#define regex_dfa_cache UTILITY_HELD(regex_dfa_cache)

#define regex_slots regex_match.slots
#define regex_group_count regex_current.groups
#define regex_boundary regex_current.boundary

/* A compilation is made in the scratch from the start, over the one before. */
// The pattern is the bytes it is given, NUL bytes included when a caller
// has length for them (sed's \x00 is one): a C string stops at the first.
static bool regex_compile_bytes(string_address pattern, positive length, bool extended,
                                bool icase, bool escapes, p8 policy)
{
        regex_pool.used = (rx_mark){0};
        return rx_compile_length(&regex_pool, &regex_current, pattern, length, extended,
                                 icase, escapes, policy);
}

static bool regex_compile(string_address pattern, bool extended, bool icase,
                          bool escapes, p8 policy)
{
        return regex_compile_bytes(pattern, string_length(pattern), extended, icase, escapes, policy);
}

/*
        The program just compiled in `pool`, copied into a block of its own so
        that it stands while others are made: its hints (and the fixed string
        as far as it goes, which is most of what they weigh), then its nodes
        and its sets, as one allocation, which regex_release gives back whole.
        A table of them was five hundred and twelve, and a script of a
        thousand s/// commands was "unsupported or invalid script".
*/
static bool rx_keep(rx_pool *pool, const regex_program *program, regex_program *into)
{
        positive fixed = program->hints->fixed_length < RX_FIXED_MAX
                             ? program->hints->fixed_length : RX_FIXED_MAX;
        positive hints = (__builtin_offsetof(rx_hints, fixed_literal) + fixed + 15) & ~(positive)15;
        // A pattern of nothing has no nodes and no sets, and the pool may
        // never have been given any: the program points at nothing then.
        positive nodes = program->nodes ? pool->used.nodes * sizeof(rx_node) : 0;
        positive sets = program->sets ? pool->used.sets * 256 : 0;
        p8 *block = memory_take(hints + nodes + sets);

        if (!block)
                return false;

        memory_copy_apart(block, program->hints, hints);
        memory_copy_apart(block + hints, program->nodes, nodes);
        memory_copy_apart(block + hints + nodes, program->sets, sets);

        *into = *program;
        into->hints = (const rx_hints *)block;
        into->nodes = (const rx_node *)(block + hints);
        into->sets = (const p8 (*)[256])(block + hints + nodes);
        return true;
}

static fn regex_keep(regex_program *into)
{
        if (!rx_keep(&regex_pool, &regex_current, into))
        {
                string_diagnostic(&text_diagnostic, 0, null, "memory exhausted");
                exit(1);
        }
}

// A kept program given back, its block being its hints and what follows them.
static fn regex_release(regex_program *program)
{
        if (program->hints)
                memory_give((address_any)program->hints);
        *program = (regex_program){0};
}

/*
        Whether a program matches anywhere in text[0, length), for a caller
        that asks the same program of many subjects and holds text[length] as
        a NUL. The graph answers, and is watched: a program whose questions
        cost it more than the machine's reading of every byte would (an awk
        /re/ over a log was 1.7 s in the graph and 0.25 s in the machine) is
        given the machine, and one the required-string prefilter answers for
        nothing stays with the graph. The one machine belongs to one program
        at a time and is handed over a few times at most. A subject holding
        a NUL, a program with line anchors and a machine that would not fit
        are the graph's.
*/
enum { RX_ASK_WATCH, RX_ASK_MACHINE, RX_ASK_GRAPH };
#define RX_ASKS_FIRST 64
#define RX_ASKS_LAST 1024
#define RX_MACHINE_HANDOVERS 8

static const rx_hints *regex_machine_owner;
static p32 regex_machine_serial;
static positive regex_machine_handovers;

static bool regex_find(p8 mode, string_address text, positive length, positive from);

/*
        The machine holds this program: it is compiled and attached unless it
        already is. A program the machine will not take is answered false.
        The handover budget bounds the switches a verdict makes; a graph walk
        that ran out of its work asks with force, which is not counted, so a
        line the graph cannot answer is never refused for want of a switch.
*/
static bool regex_machine_holds(const regex_program *program, rx_hints *hints, bool force)
{
        if (regex_machine_owner == hints && regex_machine_serial == hints->serial)
                return true;
        if (!force && regex_machine_handovers == RX_MACHINE_HANDOVERS)
                return false;
        if (!rx_dfa_compile(&regex_dfa, program, REGEX_BOUNDARY_NONE, '\0'))
                return false;
        if (!force)
                regex_machine_handovers++;
        rx_dfa_attach(&regex_dfa_cache, &regex_dfa);
        regex_machine_owner = hints;
        regex_machine_serial = hints->serial;
        return true;
}

/*
        Leftmost-longest positions from the machines, for a pattern with no
        back-reference, when the walk ran out of its work. The forward machine
        (regex_dfa) gives the earliest end of any match from `from`, E0; the
        leftmost match starts at or before E0, so the starts from `from` to E0
        are tried in order by an anchored longest machine (regex_anchor),
        and the first that matches is the leftmost, with its longest end.
        Each try is a walk over the line that stops when no thread is left,
        so a line costs its starts times the reach of each, not a backtrack.
        It needs no NUL in the line, and a pattern with no word boundary to ask
        (-w): the end of the line is the end, whatever byte follows it.
*/
static rx_dfa regex_anchor_dfa;
static rx_dfa_cache regex_anchor_cache;
static const rx_hints *regex_anchor_owner;
static p32 regex_anchor_serial;

static bool regex_anchor_holds(const regex_program *program, rx_hints *hints)
{
        if (regex_anchor_owner == hints && regex_anchor_serial == hints->serial)
                return true;
        if (!rx_dfa_compile(&regex_anchor_dfa, program, REGEX_BOUNDARY_NONE, '\0'))
                return false;
        regex_anchor_dfa.restart = RX_DFA_RESTART_NEVER;
        regex_anchor_dfa.longest = true;
        rx_dfa_attach(&regex_anchor_cache, &regex_anchor_dfa);
        regex_anchor_owner = hints;
        regex_anchor_serial = hints->serial;
        return !regex_anchor_cache.failed;
}

/* The row a machine's walk starts from at `at` in the record: the start
   state, with the flags the byte before it gives (none at the record's first
   byte). Null when the machine has no room for it. */
static b32 rx_dfa_row_at(rx_dfa_cache *cache, string_address text, positive at, bool *failed)
{
        p8 flags = at == 0 ? RX_DFA_BEGINNING
                 : string_set_name[text[at - 1]] ? RX_DFA_PREVIOUS_NAME : 0;
        memory_fill(cache->bits, 0, sizeof(cache->bits));
        cache->bits[cache->dfa->start / 64] |= 1ull << (cache->dfa->start % 64);
        bool reset = false;
        b32 state = rx_dfa_intern(cache, flags, &reset);
        if (state < 0)
        {
                *failed = true;
                return 0;
        }
        return state * (b32)cache->dfa->class_count;
}

/* The earliest end of a match that starts at or after `at`, or -1 for none;
   -2 when the machine would not hold the line. */
static positive rx_dfa_first_end(rx_dfa_cache *cache, string_address text, positive at, positive length)
{
        const rx_dfa *dfa = cache->dfa;
        bool failed = false;
        b32 row = rx_dfa_row_at(cache, text, at, &failed);
        if (failed)
                return (positive)-2;
        for (positive p = at;; p++)
        {
                p8 class = p == length ? dfa->delimiter_class : dfa->classes[text[p]];
                b32 next = cache->trans[row + class];
                if (next == RX_DFA_UNKNOWN)
                        next = rx_dfa_step(cache, row, class);
                if (next == RX_DFA_FULL)
                        return (positive)-2;
                if (next == RX_DFA_HIT || (p == length && next == RX_DFA_END_HIT))
                        return p;
                if (next < 0)
                        return (positive)-1;
                row = next;
                if (p == length)
                        return (positive)-1;
        }
}

/* The longest end of a match that starts at `at` (the machine's start is
   the row given), or -1 when none does; -2 when the machine would not hold
   the line. */
static positive rx_dfa_longest_end(rx_dfa_cache *cache, b32 row, string_address text,
                                   positive at, positive length)
{
        const rx_dfa *dfa = cache->dfa;
        positive last = (positive)-1;
        for (positive p = at;; p++)
        {
                b32 state = row / (b32)dfa->class_count;
                if (p == length)
                {
                        if (cache->accept[state] & RX_ACCEPT_END)
                                last = length;
                        return last;
                }
                p8 class = dfa->classes[text[p]];
                if (cache->accept[state] & (dfa->name[class] ? RX_ACCEPT_NAME : RX_ACCEPT_OTHER))
                        last = p;
                b32 next = cache->trans[row + class];
                if (next == RX_DFA_UNKNOWN)
                        next = rx_dfa_step(cache, row, class);
                if (next == RX_DFA_FULL)
                        return (positive)-2;
                if (next == RX_DFA_DEAD)
                        return last;
                if (next < 0)
                        return (positive)-2;
                row = next;
        }
}

/* 1 with the leftmost-longest match of a no-back-reference program from
   `from` in `text` as begin and finish, 0 for none, -1 when the machines
   would not hold it. */
static int rx_span_machine(const regex_program *program, string_address text, positive length,
                           positive from, positive *begin, positive *finish)
{
        rx_hints *hints = (rx_hints *)program->hints;
        if (!regex_machine_holds(program, hints, true) || regex_dfa_cache.failed)
                return -1;
        positive first = rx_dfa_first_end(&regex_dfa_cache, text, from, length);
        if (first == (positive)-1)
                return 0;
        if (first == (positive)-2 || regex_dfa_cache.failed)
                return -1;
        if (!regex_anchor_holds(program, hints))
                return -1;
        for (positive at = from; at <= first; at++)
        {
                bool failed = false;
                b32 row = rx_dfa_row_at(&regex_anchor_cache, text, at, &failed);
                if (failed)
                        return -1;
                positive stop = rx_dfa_longest_end(&regex_anchor_cache, row, text, at, length);
                if (stop == (positive)-2 || regex_anchor_cache.failed)
                        return -1;
                if (stop != (positive)-1)
                {
                        *begin = at;
                        *finish = stop;
                        return 1;
                }
        }
        return -1;
}

static p8 rx_find(rx_match *match, const regex_program *program, p8 mode, bool captures,
                  string_address bytes, positive length, positive start)
{
        p8 result = rx_find_walk(match, program, mode, captures, bytes, length, start);
        if (result != RX_COMPLEX || mode != REGEX_LONGEST || captures ||
            (program->flags & RX_HAS_BACKREF) || program->boundary != REGEX_BOUNDARY_NONE ||
            (program->policy & REGEX_LINE_ANCHORS) ||
            start > length || memory_first_of(bytes, 0, length))
                return result;
        positive begin, finish;
        int found = rx_span_machine(program, bytes, length, start, &begin, &finish);
        if (found < 0)
                return RX_COMPLEX;
        memory_fill(match->slots, -1, match->active_captures * sizeof(positive));
        match->slots[0] = found ? begin : 0;
        match->slots[1] = found ? finish : 0;
        return found ? RX_MATCH : RX_NO_MATCH;
}

static bool regex_test(const regex_program *program, string_address text, positive length)
{
        rx_hints *hints = (rx_hints *)program->hints;
        if (hints->verdict == RX_ASK_MACHINE && !regex_machine_holds(program, hints, false))
                hints->verdict = RX_ASK_GRAPH;
        if (hints->verdict == RX_ASK_MACHINE && !regex_dfa_cache.failed &&
            !memory_first_of(text, 0, length))
        {
                string_address hit = rx_dfa_scan(&regex_dfa_cache, text, text + length + 1);
                if (hit || !regex_dfa_cache.failed)
                        return hit != null;
        }
        regex_current = *program;
        /*
                The graph ran out of its work on this line. A pattern with no
                back-reference is a regular language, and the machine answers
                it in one pass whatever the line is: the line is the machine's
                to answer, and "too complex" is said only where neither can.
        */
        p8 result = rx_find(&regex_match, &regex_current, REGEX_FIRST, false, text, length, 0);
        if (result == RX_COMPLEX && !(program->flags & RX_HAS_BACKREF) &&
            !(program->policy & REGEX_LINE_ANCHORS) && !memory_first_of(text, 0, length) &&
            regex_machine_holds(program, hints, true) &&
            !regex_dfa_cache.failed)
        {
                string_address hit = rx_dfa_scan(&regex_dfa_cache, text, text + length + 1);
                if (hit || !regex_dfa_cache.failed)
                        return hit != null;
        }
        if (result == RX_COMPLEX)
        {
                string_diagnostic(&text_diagnostic, 0, null, "regular expression too complex");
                text_status = 2;
        }
        bool answer = result == RX_MATCH;
        if (hints->verdict == RX_ASK_WATCH)
        {
                hints->asks++;
                hints->work += regex_match.work_used;
                hints->bytes += length;
                if (hints->asks == RX_ASKS_FIRST || hints->asks == RX_ASKS_LAST)
                        hints->verdict = hints->work * 2 > hints->bytes &&
                                         !(program->policy & REGEX_LINE_ANCHORS)
                                             ? RX_ASK_MACHINE
                                         : hints->asks == RX_ASKS_LAST ? RX_ASK_GRAPH
                                                                       : RX_ASK_WATCH;
        }
        return answer;
}

static bool regex_find(p8 mode, string_address text, positive length, positive from)
{
        p8 result = rx_find(&regex_match, &regex_current,
                            mode & ~REGEX_CAPTURES, mode & REGEX_CAPTURES, text, length, from);
        if (result == RX_COMPLEX)
        {
                string_diagnostic(&text_diagnostic, 0, null, "regular expression too complex");
                text_status = 2;
        }
        return result == RX_MATCH;
}
