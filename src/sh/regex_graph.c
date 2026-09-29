/*
        The regular expression engine: a graph, built once and walked.

        Expanded into text.c, and through it reached by grep, sed and awk,
        which is why it is a file of its own rather than any one of their
        lower halves. A pattern is compiled to nodes, then matched by a lazy
        DFA over them with the literal and anchor hints below taken first,
        so the common shapes never build a state at all.

        The ceilings are fixed and stated at the top: a pattern that would
        exceed one is refused rather than grown into, so no input decides
        how much memory matching it costs.
*/

#define RX_NODE_MAX 8192
#define RX_SET_MAX 512
#define RX_HINT_MAX 512
#define RX_LITERAL_MAX 256
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
          backslash, a back reference to a group not yet closed, an
          unclosed bracket and a class name there is no class of. */
       REGEX_STRICT_INTERVALS = 32,
       /* A repeat with nothing before it, in extended syntax at the start of
          the pattern, of a group or of an alternative, repeats nothing and
          so matches the empty string, as GNU grep's dfa reads it -- *a is
          a, and a lone + matches every line -- and says so, warnings kept
          in regex_warnings, where it once was the character itself. */
       REGEX_LEADING_REPEATS = 64,
       REGEX_POLICY_DEFAULT = 5, REGEX_POLICY_TAC = 2 | 16,
       REGEX_POLICY_EXPR = 5 | 32 };
enum { REGEX_FAILED_OTHER = 1, REGEX_FAILED_BRACE, REGEX_FAILED_CONTENT,
       REGEX_FAILED_SIZE, REGEX_FAILED_OPEN, REGEX_FAILED_CLOSE,
       REGEX_FAILED_ESCAPE, REGEX_FAILED_REFERENCE, REGEX_FAILED_BRACKET,
       REGEX_FAILED_CLASS };
enum { REGEX_BOUNDARY_NONE, REGEX_BOUNDARY_WORD, REGEX_BOUNDARY_LINE };
enum { REGEX_EDGE_WORD, REGEX_EDGE_NOT_WORD, REGEX_EDGE_START, REGEX_EDGE_STOP };

/* Zero ends a sequence. ALT owns two branch roots; CAPTURE and COUNT own
   a child sequence's first/last nodes. No node is copied for repetition. */
typedef struct
{
        p8 kind, argument;
        p16 next, previous, left, right;
        b16 minimum, maximum;
} rx_node;

typedef struct
{
        p8 first_skip[256], last_bytes[256], literal[RX_LITERAL_MAX];
        positive literal_length, fixed_length, fixed_work;
        positive2 literal_anchors, fixed_anchors;
        p8 fixed_literal[RX_NODE_MAX];
} rx_hints;

typedef struct
{
        const rx_node *nodes;
        const p8 (*sets)[256];
        const rx_hints *hints;
        p16 first;
        p8 groups, policy, boundary, flags;
} regex_program;

typedef struct
{
        p16 nodes, sets, hints;
} rx_mark;

typedef struct
{
        rx_node nodes[RX_NODE_MAX];
        p8 sets[RX_SET_MAX][256];
        rx_hints hints[RX_HINT_MAX];
        rx_mark used;
} rx_pool;

typedef struct
{
        p16 first, last;
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

static p16 rx_emit(rx_compiler *c, rx_node node)
{
        if (c->broken || c->cursor.nodes == RX_NODE_MAX)
        {
                c->broken = true;
                return 0;
        }
        p16 at = c->cursor.nodes++;
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
        if (c->cursor.sets == RX_SET_MAX)
        {
                c->broken = true;
                return -1;
        }
        b32 set = c->cursor.sets++;
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

static rx_fragment rx_one(rx_compiler *c, p8 kind, p8 argument)
{
        p16 at = rx_emit(c, (rx_node){.kind = kind, .argument = argument});

        return (rx_fragment){at, at};
}

static rx_fragment rx_either(rx_compiler *c, rx_fragment one, rx_fragment two)
{
        p16 at = rx_emit(c, (rx_node){.kind = RX_ALT, .left = one.first,
                                      .right = two.first});

        return (rx_fragment){at, at};
}

/* A lead byte and the continuation bytes that follow it, as one sequence. */
static rx_fragment rx_wide_run(rx_compiler *c, b32 lead, b32 tails)
{
        rx_fragment run = rx_one(c, RX_SET, (p8)lead);

        for (b32 i = 0; i < tails; i++)
                run = rx_join(c, run, rx_one(c, RX_SET, (p8)c->wide_tail));

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
        return rx_either(c, rx_one(c, RX_SET, (p8)ascii), wide);
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

/* Brackets keep the BRE/ERE backslash rule; sed enables its own escapes. */
static b32 rx_parse_set(rx_compiler *c, rx_set_facts *facts)
{
        b32 set = rx_new_set(c);
        bool negate = rx_peek(c, 0) == '^', first = true;
        bool wide = (c->program.policy & REGEX_CHARACTERS) != 0;
        if (set < 0)
                return 0;
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

                        //      regcomp reads a name of up to 31 bytes to
                        //      its :] and has no class by it, or no :].
                        if (c->program.policy & REGEX_STRICT_INTERVALS)
                        {
                                positive close = c->at + 2;

                                while (close + 1 < c->length && close - c->at < 34 &&
                                       !(c->pattern[close] == ':' && c->pattern[close + 1] == ']'))
                                        close++;
                                rx_refuse(c, close + 1 < c->length && close - c->at < 34
                                                 ? REGEX_FAILED_CLASS
                                                 : REGEX_FAILED_BRACKET);
                                return set;
                        }
                }
                if (c->escapes && byte == '\\' && rx_peek(c, 1))
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

                if (wide && !negate && byte >= 0x80 && rx_peek(c, 1) != '-')
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
                        p8 last = rx_peek(c, 1);
                        c->at += 2;
                        for (b32 i = byte; i <= last; i++)
                                rx_set_add(c, set, (p8)i);
                }
                else
                        rx_set_add(c, set, byte);
        }
        // Nothing after [ or [^ at all is regcomp's plain refusal.
        if ((c->program.policy & REGEX_STRICT_INTERVALS) && !first)
                rx_refuse(c, REGEX_FAILED_BRACKET);
        c->broken = true;
        return set;
}

static rx_fragment rx_alternation(rx_compiler *c);

static rx_fragment rx_atom(rx_compiler *c)
{
        p8 byte = rx_peek(c, 0), kind = RX_BYTE;
        rx_fragment child = {0};
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
                        byte = (p8)rx_parse_set(c, address_of facts);

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
                                                c->pool->sets[byte][i] = 0;

                                        whole = rx_character(c, byte);
                                }
                                else
                                        whole = rx_one(c, RX_SET, byte);

                                for (b32 i = 0; i < facts.count; i++)
                                {
                                        rx_fragment run = {0};

                                        for (b32 b = 0; b < facts.size[i]; b++)
                                                run = rx_join(
                                                    c, run,
                                                    rx_one(c, RX_BYTE,
                                                           c->pattern[facts.at[i] + b]));

                                        whole = rx_either(c, whole, run);
                                }

                                return whole;
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
                                byte = (p8)(set < 0 ? 0 : set);
                        }
                        else if (byte == 'b' || byte == 'B' || byte == '<' || byte == '>')
                        {
                                kind = RX_EDGE;
                                byte = byte == 'b' ? REGEX_EDGE_WORD : byte == 'B' ?
                                       REGEX_EDGE_NOT_WORD : byte == '<' ? REGEX_EDGE_START : REGEX_EDGE_STOP;
                        }
                        else if (c->escapes)
                                byte = byte == 'n' ? '\n' : byte == 't' ? '\t' : byte;
                }
                else if (byte == '\\' && (c->program.policy & REGEX_STRICT_INTERVALS))
                        rx_refuse(c, REGEX_FAILED_ESCAPE);
                if (kind == RX_BYTE && (c->program.flags & RX_IGNORE_CASE))
                        byte = (p8)byte_to_lower(byte);
        }
        p16 at = rx_emit(c, (rx_node){.kind = kind, .argument = byte,
                                     .left = child.first, .right = child.last});
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

                if (byte == ',' && !which)
                {
                        comma = true;
                        which = 1;
                        at++;
                        continue;
                }

                // The upper bound's reading stops at a second comma too,
                // which is no close: bad content, not an unmatched brace.
                if (byte == ',')
                {
                        c->broken = true;
                        c->failure = REGEX_FAILED_CONTENT;
                        return false;
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
        p16 body_start = c->cursor.nodes;
        positive began = c->at;
        rx_fragment prefix = {0}, body = rx_atom(c);

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
                        p16 at = rx_emit(c, (rx_node){.kind = RX_BYTE, .argument = literal});
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
                p16 at = rx_emit(c, (rx_node){.kind = RX_COUNT,
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
        for (;;)
        {
                rx_fragment branch = {0};
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
                while (!c->broken && c->at < c->length && !rx_operator(c, ')') && !rx_operator(c, '|'))
                        branch = rx_join(c, branch, rx_piece(c));
                if (!have_alternative)
                        whole = branch;
                else
                {
                        p16 at = rx_emit(c, (rx_node){.kind = RX_ALT,
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

static p16 rx_tail(const rx_node *nodes, p16 at)
{
        while (at && nodes[at].next)
                at = nodes[at].next;
        return at;
}

/* Return 0 after a consuming edge, 1 for an empty path, 2 for an unknown
   first byte. The reverse walk passes assertions but never backreferences. */
static b32 rx_edges(const regex_program *program, p16 first, p16 last, p8 *table, bool reverse)
{
        b32 result = 1;
        for (p16 at = reverse ? last : first; at;)
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

/* A literal sequence outside alternation, or inside a mandatory child, is a
   safe block prefilter. Captures delimit runs; counted nodes are never copied. */
static fn rx_required(const rx_node *nodes, p16 first, rx_hints *hints)
{
        for (p16 at = first; at;)
        {
                const rx_node *node = nodes + at;
                p16 from = at;
                positive length = 0;
                while (at && nodes[at].kind == RX_BYTE)
                {
                        length++;
                        at = nodes[at].next;
                }
                if (length)
                {
                        if (length > RX_LITERAL_MAX)
                                length = RX_LITERAL_MAX;
                        if (length > hints->literal_length)
                        {
                                hints->literal_length = length;
                                for (positive i = 0; i < length; i++)
                                {
                                        hints->literal[i] = nodes[from].argument;
                                        from = nodes[from].next;
                                }
                        }
                        continue;
                }
                if (node->kind == RX_CAPTURE || (node->kind == RX_COUNT && node->minimum > 0))
                        rx_required(nodes, node->left, hints);
                at = node->next;
        }
}

/* A deterministic graph can reuse the prepared literal search when its
   captures are not observed. Bound both the expansion and interpreter work;
   the graph itself stays compact and remains the resource-limited fallback. */
static bool rx_fixed(const rx_node *nodes, p16 first, rx_hints *hints, positive *work)
{
        for (p16 at = first; at; at = nodes[at].next)
        {
                const rx_node *node = nodes + at;
                positive begin = hints->fixed_length, child_work = 0;
                if (node->kind == RX_BYTE)
                {
                        if (begin < RX_NODE_MAX)
                                hints->fixed_literal[begin] = node->argument;
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
                                if (count > RX_NODE_MAX / (child_work + 1))
                                        return false;
                                /* Every byte costs at least one work unit, so this
                                   multiplication is bounded by the work check. */
                                hints->fixed_length = begin + count * size;
                                if (hints->fixed_length <= RX_NODE_MAX)
                                        for (positive i = 1; i < count && size; i++)
                                                memory_copy_apart(hints->fixed_literal + begin + i * size,
                                                                  hints->fixed_literal + begin, size);
                                child_work = 1 + count * (child_work + 1);
                        }
                }
                else
                        return false;
                if (child_work > RX_NODE_MAX - *work)
                        return false;
                *work += child_work;
        }
        return true;
}

// Why the last compile refused its pattern, when it did.
static p8 regex_failure;

/* Compile above the current mark. Neither a failed compile nor its scratch
   metadata changes a published descriptor or the pool's ownership cursor. */
static bool rx_compile(rx_pool *pool, regex_program *out, string_address pattern,
                       bool extended, bool icase, bool escapes, p8 policy)
{
        rx_compiler c = {.pool = pool, .cursor = pool->used, .pattern = pattern,
                         .length = string_length(pattern), .extended = extended,
                         .escapes = escapes, .wide_ascii = -1, .wide_two = -1,
                         .wide_three = -1, .wide_four = -1, .wide_tail = -1};
        if (c.cursor.hints == RX_HINT_MAX)
                return false;
        if (!c.cursor.nodes)
                c.cursor.nodes = 1;
        p16 first = c.cursor.nodes;
        rx_hints *hints = pool->hints + c.cursor.hints++;
        /* Every accepted proof byte is constructed before publication;
           rewinding a hint slot does not require clearing unused capacity. */
        memory_fill(hints, 0, __builtin_offsetof(rx_hints, fixed_literal));
        c.program = (regex_program){.nodes = pool->nodes, .sets = (const p8 (*)[256])pool->sets,
                                .hints = hints, .policy = policy, .flags = icase ? RX_IGNORE_CASE : 0};
        regex_warning_count = 0;
        rx_fragment root = rx_alternation(address_of c);
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
        for (p16 i = first; i < c.cursor.nodes; i++)
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
        if (root.first && pool->nodes[root.first].kind == RX_BEGIN)
                c.program.flags |= RX_ANCHORED;
        rx_required(pool->nodes, root.first, hints);
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
        *out = c.program;
        pool->used = c.cursor;
        return true;
}

/* Iterative graph execution. Continuations stay immutable while a choice can
   revisit them; a choice restores both its frame mark and capture undo mark. */
typedef struct {
        /* Zero resumes a sequence, a positive slot closes its capture,
           and -1 resumes a counted child. The slot is also the frame tag. */
        b16 slot;
        p16 node;
        p32 parent;
        positive repetitions, previous_position;
} rx_frame;

typedef struct {
        p16 node;
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

static bool rx_choice_put(rx_match *match, p16 node, p32 continuation,
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
                match->best_stop = stop;
                memory_copy_apart(match->best_slots, match->slots,
                                  match->active_captures * sizeof(positive));
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

static bool rx_run(rx_match *match, positive start)
{
        const regex_program *program = match->program;
        string_address bytes = match->bytes;
        positive length = match->length;
        p16 node = program->first;
        p32 continuation = 0;
        positive position = start, repetitions = 0, previous = positive_max;
        positive work = match->work_used, work_limit = match->work_limit;

        if (match->work_yield && match->work_yield < work_limit)
                work_limit = match->work_yield;
        bool accepted = false;
        match->frame_used = match->choice_used = match->undo_used = 0;
        match->best_limit = positive_max;
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
                                if (child->kind == RX_BYTE && !(program->flags & RX_IGNORE_CASE))
                                        taken = memory_span_byte(at, child->argument, available);
                                else if (child->kind == RX_ANY)
                                {
                                        string_address newline = (program->policy & REGEX_DOT_NEWLINE)
                                            ? 0 : memory_first_of(at, '\n', available);
                                        taken = newline ? (positive)(newline - at) : available;
                                }
                                else if (child->kind == RX_SET)
                                        taken = string_span_max(at, available,
                                            (const b8 *)program->sets[child->argument]);
                                else
                                        while (taken < available && rx_single(program, child, at[taken]))
                                                taken++;
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
                                if (taken > (positive)instruction->minimum &&
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
                if (choice.lower != positive_max && choice.position > choice.lower)
                {
                        choice.position--;
                        match->choices[match->choice_used++] = choice;
                }
        }
        match->work_used = work;
        return accepted;
}

static p8 rx_find(rx_match *match, const regex_program *program, p8 mode, bool captures,
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
                     !match->pending_exhaustion && match->frame_capacity >= RX_NODE_MAX &&
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
                match->best_stop = positive_max;
                bool found = false;
                if (program->boundary == REGEX_BOUNDARY_NONE || !at)
                        found = rx_run(match, at);
                if (!found && program->boundary == REGEX_BOUNDARY_WORD && at < length && !string_set_name[bytes[at]])
                        found = rx_run(match, at + 1);
                if (match->best_stop != positive_max)
                {
                        memory_copy_apart(match->slots, match->best_slots, match->active_captures * sizeof(positive));
                        found = true;
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
#define RX_DFA_SET_MAX 128
#define RX_DFA_STATE_MAX 2048
#define RX_DFA_POOL_MAX 262144
#define RX_DFA_HASH 8192
#define RX_DFA_RESETS_MAX 64
#define RX_DFA_DEPTH_MAX 256
/* Calls the NFA build may make. Nodes are capped, but a count over a body
   that makes none -- (){32767} -- costs a call per repeat and nests: three
   levels was 32767^3 of them before a byte of data was read. */
#define RX_DFA_WORK_MAX (1u << 17)

enum { RX_NFA_SET = 1, RX_NFA_SPLIT, RX_NFA_BEGIN, RX_NFA_END, RX_NFA_EDGE, RX_NFA_MATCH };
enum { RX_DFA_UNKNOWN = -1, RX_DFA_HIT = -2, RX_DFA_DEAD = -3,
       RX_DFA_END_HIT = -4, RX_DFA_END_MISS = -5, RX_DFA_FULL = -6 };
enum { RX_DFA_PREVIOUS_NAME = 1, RX_DFA_BEGINNING = 2 };
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
        p16 order[RX_NODE_MAX];
        positive nfa_count, set_count, class_count, order_top, work;
        p16 start;
        p8 boundary, delimiter, delimiter_class;
        // Starts after the first position: every one, only after a byte that
        // is not a name byte, or none -- -x, and a program that begins with ^,
        // which rx_find tries at nought and nowhere else.
        p8 restart;
        bool usable;
} rx_dfa;

typedef struct
{
        const rx_dfa *dfa;
        b32 trans[RX_DFA_STATE_MAX * 256];
        p32 set_at[RX_DFA_STATE_MAX];
        p16 set_size[RX_DFA_STATE_MAX];
        p8 flags[RX_DFA_STATE_MAX];
        p16 pool[RX_DFA_POOL_MAX];
        p32 hash[RX_DFA_HASH];
        p32 mark[RX_DFA_NFA_MAX];
        p64 bits[RX_DFA_NFA_MAX / 64];
        p16 stack[RX_DFA_NFA_MAX];
        p16 found[RX_DFA_NFA_MAX];
        positive pool_used, state_count, generation, resets;
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
static p16 rx_nfa_build(rx_dfa *dfa, const regex_program *program, p16 first,
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
        for (p16 at = first; at; at = nodes[at].next)
        {
                if (dfa->order_top == RX_NODE_MAX)
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
#if X64 || ARM64
                        p16 id = (p16)(word * 64 + (positive)__builtin_ctzll(bits));
#else
                        // No Zbb on the riscv floor: the builtin is a libgcc call.
                        p16 id = (p16)(word * 64 + (positive)bits_trailing_zeros(bits));
#endif
                        cache->found[size++] = id;
                        hash = (hash ^ id) * 1099511628211ull;
                }
        positive slot = (positive)(hash >> 20) & (RX_DFA_HASH - 1);
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
                slot = (slot + 1) & (RX_DFA_HASH - 1);
        }
        if (cache->state_count == RX_DFA_STATE_MAX ||
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
        b32 state = (b32)cache->state_count++;
        cache->set_at[state] = (p32)cache->pool_used;
        cache->set_size[state] = (p16)size;
        cache->flags[state] = flags;
        memory_copy_apart(cache->pool + cache->pool_used, cache->found, size * sizeof(p16));
        cache->pool_used += size;
        cache->hash[slot] = (p32)state + 1;
        for (positive c = 0; c < dfa->class_count; c++)
                cache->trans[(positive)state * dfa->class_count + c] = RX_DFA_UNKNOWN;
        return state;
}

static fn rx_dfa_begin(rx_dfa_cache *cache)
{
        cache->state_count = 0;
        cache->pool_used = 0;
        memory_fill(cache->hash, 0, sizeof(cache->hash));
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
                next = rx_dfa_close(cache, state, before, false, true, &consuming)
                           ? RX_DFA_END_HIT : RX_DFA_END_MISS;
                cache->trans[cell] = next;
                return next;
        }
        bool after = dfa->name[class];
        if (rx_dfa_close(cache, state, before, after, false, &consuming) &&
            dfa->boundary != REGEX_BOUNDARY_LINE &&
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
                if (next == RX_DFA_END_MISS)
                {
                        at++;
                        row = start;
                        continue;
                }
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

#define REGEX_SCRATCH_MAX 20000

static rx_pool address_to regex_pool_held;
static rx_mark regex_retained;
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

/* Ordinary compilations borrow the space above the retained program mark. */
static bool regex_compile(string_address pattern, bool extended, bool icase,
                          bool escapes, p8 policy)
{
        regex_pool.used = regex_retained;
        return rx_compile(&regex_pool, &regex_current, pattern, extended,
                          icase, escapes, policy);
}

static fn regex_keep(regex_program *into)
{
        *into = regex_current;
        regex_retained = regex_pool.used;
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
