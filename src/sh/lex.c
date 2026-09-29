/*
        The shell's lexer.

        Turns a line into tokens: words, operators and the end. What a word
        means is not decided here -- the quotes stay in it, because POSIX does
        quote removal last, after every expansion has had its turn, and a lexer
        that strips them early cannot tell "$x" from $x afterwards.

        The scanning is string_span from lib.c, which is assembly on every
        architecture and reads about two bytes a cycle. A lexer is almost
        entirely that one operation: run to the next thing that matters. Every
        set below is prepared once, so the inner loop never asks what kind of
        byte it is holding.

        lex_line_floor fuses that span into the token loop on x86_64, arm64
        and riscv64, because a short blank skip was a call that looked at one
        byte and came back. Quotes, substitutions, [[, (( and a=(...) still
        take the C word walk.
*/

#define LEX_END 0
#define LEX_WORD 1
#define LEX_OPERATOR 2
#define LEX_ARITHMETIC 3
#define LEX_CONDITIONAL 4

// The operators, longest first, so that && is never read as two &.
#define OP_AND_IF 1     // &&
#define OP_OR_IF 2      // ||
#define OP_DSEMI 3      // ;;
#define OP_DLESS 4      // <<
#define OP_DGREAT 5     // >>
#define OP_LESSAND 6    // <&
#define OP_GREATAND 7   // >&
#define OP_LESSGREAT 8  // <>
#define OP_CLOBBER 9    // >|
#define OP_SEMI 10      // ;
#define OP_PIPE 11      // |
#define OP_AMP 12       // &
#define OP_LESS 13      // <
#define OP_GREAT 14     // >
#define OP_LPAREN 15    // (
#define OP_RPAREN 16    // )
#define OP_ANDGREAT 17  // &>
#define OP_ANDDGREAT 18 // &>>
#define OP_HERESTRING 19 // <<<
/* Appended, and never inserted among the ones above: parse.c tells a
   redirect from anything else by the number's range, so a new value in the
   middle would silently reclassify the operators behind it. */
#define OP_PIPEAND 20   // |&
#define OP_SEMIAND 21   // ;&
#define OP_DSEMIAND 22  // ;;&


typedef struct
{
        b32 kind;
        b32 op;
        string_address text;
        positive length;
        // Where this token began in the line it came from, so a caller can cut
        // the line at an operator without rebuilding what it already read.
        positive at;
} lex_token;

/*
        Tokens borrow counted spans from the caller's input. Quotes and
        expansions retain their exact source bytes, so no text arena is needed.
        The caller keeps that input alive until it consumes the tokens; a word
        need not end at a NUL. The parser copies it into stable, terminated
        storage before the next input line can reuse its source buffer.
*/
typedef struct
{
        lex_token address_to tokens;
        positive token_room;
        b32 count;
        /*
                Which line of this input the shell is running.

                The lexer is handed one line at a time and used to keep no
                count of them, so a function had no line to say it was called
                from and $LINENO had nothing to read. It belongs in the frame
                for the same reason everything else here does: a nested source
                -- an eval, or a dot file -- counts its own lines from one and
                the outer count comes back when it ends.
        */
        positive line;
} lex_frame;

_Static_assert(sizeof(lex_token) == 32, "lex_line_floor stores 32-byte tokens");
_Static_assert(__builtin_offsetof(lex_token, kind) == 0, "kind");
_Static_assert(__builtin_offsetof(lex_token, op) == 4, "op");
_Static_assert(__builtin_offsetof(lex_token, text) == 8, "text");
_Static_assert(__builtin_offsetof(lex_token, length) == 16, "length");
_Static_assert(__builtin_offsetof(lex_token, at) == 24, "at");
_Static_assert(__builtin_offsetof(lex_frame, tokens) == 0, "tokens");
_Static_assert(__builtin_offsetof(lex_frame, token_room) == 8, "room");
_Static_assert(__builtin_offsetof(lex_frame, count) == 16, "count");
_Static_assert(__builtin_offsetof(lex_frame, line) == 24, "line");

/*
        Nested input gets its own token table; the outer table and the input
        it views remain alive until the nested source returns. lex_line_floor
        names this object from file-scope assembly, whose textual references
        LTO cannot see; external visibility keeps the symbol through whole-
        program optimization.
*/
KEEP __attribute__((externally_visible)) lex_frame lex_context;

#define lex_tokens lex_context.tokens
#define lex_token_room lex_context.token_room
#define lex_count lex_context.count
#define shell_line_number lex_context.line

// The builtins own $LINENO itself and read this for it. What it answers is
// the line the running command was written on, which the executor keeps.
PURE positive shell_line_now();
PURE positive shell_lineno_reported();

fn parse_nest_enter();
fn parse_nest_leave();

/* Whether the physical line being fed ended with a newline. POSIX
   continuation is backslash-newline; a backslash that meets EOF with nothing
   after it is a byte of the word. Every reader says so -- a script, stdin,
   a -c string, a sourced file and eval -- as bash 5.3 and dash do; bash 5.2
   synthesized a newline for a script or stdin and dropped the backslash. */
static bool lex_line_newline = true;
static bool lex_scan_newline = true;

fn lex_physical_newline(bool newline)
{
        lex_line_newline = newline;
}

fn lex_take_physical_newline()
{
        lex_scan_newline = lex_line_newline;
        lex_line_newline = true;
}

static fn lex_nest_enter(lex_frame address_to frame)
{
        address_to frame = lex_context;
        lex_context = (lex_frame){0};

        parse_nest_enter();
}

static fn lex_nest_leave(lex_frame address_to frame)
{
        parse_nest_leave();

        if (lex_tokens)
                memory_free(lex_tokens, lex_token_room * sizeof(lex_token));

        lex_context = address_to frame;
}

/*
        The byte sets, built once.

        metachar     what ends a word without being part of it
        ordinary     everything a word may contain without further thought,
                     which is the complement of the two above plus the things
                     that begin a quote or an expansion

        lex_line_floor names both tables from file-scope assembly, so they
        need the same external visibility as lex_context under LTO.
*/
KEEP __attribute__((externally_visible)) b8 lex_ordinary[STRING_SET_BYTES];
KEEP __attribute__((externally_visible)) b8 lex_operator[STRING_SET_BYTES];
// What decides nothing inside a double quote: wider than lex_ordinary,
// because a blank means nothing in there.
static b8 lex_in_double[STRING_SET_BYTES];
/*
        Bytes that cannot open a line continuation. parse_feed used to walk
        every line twice -- lex_unfinished, then lex_line -- and the second
        walk is the only one that produces tokens. The unfinished walk is
        needed only when a quote, a backslash, a substitution, `[[`, a
        process substitution or an a=( list might still be open; this set lets one
        string_span prove the common line has none of those.
*/
static b8 lex_closed[STRING_SET_BYTES];
static b32 lex_ready;

/* expand.c is included later in this translation unit.  Its substitution
   child parses a fresh, non-interactive shell source even when the containing
   shell was interactive, which is material to comment policy. */
static bool expand_in_substitution;
/* $LINENO inside $( ) is the line of the substitution in the caller, not
   a fresh count of the child's body. The child freezes that number here. */
static positive expand_substitution_lineno;

/*
        Nested eval and `.` name themselves on a syntax error. Bash inserts
        `eval:` between $0 and the line, and replaces $0 with the sourced
        path; dash puts the extra word after the line. Expansion errors pick
        the same names up except that bash never writes `eval:` on those.
*/
static string_address shell_syntax_command;
static string_address shell_syntax_file;
static positive shell_syntax_line_override;

/*
        Bash $LINENO inside eval is the eval command's line plus the offset
        inside the evaluated text. The nested reader still counts from one;
        this is the command's line, added back by shell_line_now. dash has
        no LINENO, so only the bash personality sets it.
*/
static positive shell_eval_lineno_base;

positive shell_eval_lineno_base_now();
PURE bool exec_in_function();
PURE bool exec_compound_now();
positive exec_line_exchange(positive line);

fn lex_prepare()
{
        if (lex_ready)
                return;

        memory_fill(lex_ordinary, 0, sizeof(lex_ordinary));
        memory_fill(lex_operator, 0, sizeof(lex_operator));

        string_set_add(lex_operator, "|&;<>()");

        /*
                Everything a word may hold without a decision being needed.

                The complement of what ends a word, begins a quote, escapes, or
                starts an expansion -- and never the terminator, or a run would
                walk off the end of the string.
        */
        memory_fill(lex_ordinary + 1, 1, STRING_SET_BYTES - 1);
        memory_fill(lex_in_double + 1, 1, STRING_SET_BYTES - 1);

        {
                static const string_address decides = " \t\n|&;<>()['\"\\$`";
                static const string_address in_double = "\"\\$`";

                for (positive i = 0; decides[i]; i++)
                        lex_ordinary[decides[i]] = 0;

                for (positive i = 0; in_double[i]; i++)
                        lex_in_double[in_double[i]] = 0;
        }

        memory_fill(lex_closed + 1, 1, STRING_SET_BYTES - 1);
        {
                static const string_address opens = "\"'\\$`[<>(";

                for (positive i = 0; opens[i]; i++)
                        lex_closed[opens[i]] = 0;
        }

        lex_ready = true;
}

static KEEP positive lex_at;

static KEEP b32 lex_add(b32 kind, b32 op, string_address text, positive length)
{
        if (!shell_array_room(lex_tokens, lex_token_room, (positive)lex_count + 2))
                return false;

        lex_tokens[lex_count].at = lex_at;
        lex_tokens[lex_count].kind = kind;
        lex_tokens[lex_count].op = op;
        lex_tokens[lex_count].text = text;
        lex_tokens[lex_count].length = length;
        lex_count++;

        return true;
}

/* The token floor grows through here; LTO cannot see that call. */
static KEEP bool lex_grow(void)
{
        return shell_array_room(lex_tokens, lex_token_room, (positive)lex_count + 2);
}

PURE bool shell_extglob_asked();

// The five heads of an extended pattern group, which are ordinary bytes
// everywhere a parenthesis does not follow them.
static CONST inline INLINE bool lex_extended_head(p8 value)
{
        return value == '?' || value == '*' || value == '+' || value == '@' ||
               value == '!';
}

static string_address lex_nested_at(string_address at);
static __attribute__((noinline)) string_address
lex_nesting(string_address at);
static string_address lex_nesting_at(string_address at, positive nesting,
                                     bool posix_double);
static string_address lex_quote_end_kind(string_address at, p8 quote,
                                         bool address_to command_open,
                                         positive nesting);
#define lex_quote_end(at, quote) lex_quote_end_kind(at, quote, null, 0)
static string_address parse_here_skip_bodies(string_address line,
                                              string_address newline);

/* The expander enforces this same ceiling when it later evaluates the nested
   words.  The earlier syntax walk must not be the unbounded recursive path.
   Nested command substitutions a hundred and fifty deep work on lima 5.2.32;
   sixty-four made the walk return unclosed and the line read as unexpected
   EOF. Two hundred and fifty-six is enough for that family and still a bound
   on C-stack recursion. */
#define EXPAND_DEPTH 256

// The three bytes that separate words and lines. Asked in five places, which
// used to be five spellings of the same three comparisons.
static CONST inline INLINE bool lex_is_space(p8 value)
{
        return value == ' ' || value == '\t' || value == '\n';
}

/* Whether this unquoted < can be one half of << after line continuation has
   removed its bytes.  The full lexer remains the authority: a false positive
   only asks the parser-owned scanner and a here-string still registers no
   document. */
static PURE bool lex_dless_candidate(string_address line, string_address at)
{
        string_address before = at;
        positive distance = (positive)(at - line);

        if (string_is(at + 1, '<') && string_not(at + 2, '<'))
                return true;

        while (distance >= 2 && string_is(before - 1, '\n') &&
               string_is(before - 2, '\\'))
        {
                before -= 2;
                distance -= 2;
        }

        return distance && string_is(before - 1, '<');
}

/*
        Whether a # standing where a word could begin starts a comment.

        interactive_comments is deliberately narrower than the lexer: Bash
        consults it only while reading interactively.  A script still has
        comments after `shopt -u interactive_comments`, and the sh/dash
        personalities keep their existing always-on comment grammar.  Keep
        the test at the two rare fresh-# sites rather than in lex_ordinary,
        so an ordinary word pays no extra classification or branch.
*/
static inline INLINE bool lex_comments_on()
{
        return !shell_bash_compat || !shell_is_interactive ||
               expand_in_substitution ||
               shell_shopt_on(INTERACTIVE_COMMENTS);
}

/*
        Where a run closes in which a backslash carries the byte behind it.

        A dollar-single-quoted run is one: unlike an ordinary single quote,
        \' does not close it. The inside of a double quote seen from within a
        nesting is the other, where the closing quote is the only thing being
        looked for and a substitution inside it is not opened again. Same walk
        either way, so it is one walk with the quote as its argument.
*/
static PURE string_address lex_escaped_end(string_address at, p8 quote)
{
        while (string_get(at) && string_get(at) != quote)
                at += string_is(at, '\\') && string_get(at + 1) ? 2 : 1;

        return at;
}

/* Where a POSIX dollar-single-quoted run closes. The escape is interpreted
   just before expansion; the lexer only keeps the quoted bytes together. */
static PURE string_address lex_dollar_quote_end(string_address at)
{
        while (string_get(at) && string_not(at, '\''))
        {
                //      dash's \c takes whatever follows as its operand, a
                //      quote included, so $'\c'' is one control character
                //      and a quote that closes it.
                if (string_is(at, '\\') && string_get(at + 1))
                        at += shell_dash_compat && string_is(at + 1, 'c') &&
                                      string_get(at + 2)
                                  ? 3 : 2;
                else
                        at++;
        }

        return at;
}

/*
        Step over whatever begins here that a scanner is not allowed to look
        inside, and say whether anything was stepped over.

        Three scanners walk a word for one thing of their own -- the )) that
        closes an arithmetic command, the ]] that closes a Bash condition, the
        ; that separates the three parts of a C-style for -- and all three
        have to agree about which bytes cannot be it. A backslash carries the
        byte behind it; a quoted run holds everything up to its partner; and
        $( ), ${ } and a backtick pair hold a whole command or name. Written
        out three times, those three lists had to be kept level by hand.

        at is advanced past the run when there was one. An unclosed quote is
        its own answer rather than a plain miss, because it means the word
        never ended: two of the three callers report that as no answer at all
        and the third as the end, and at is left on the terminating null so
        the third can simply hand it back.
*/
#define LEX_SKIP_NOTHING 0
#define LEX_SKIP_STEPPED 1
#define LEX_SKIP_UNCLOSED 2

static b32 lex_skip_held(string_address address_to at)
{
        string_address step = address_to at;
        p8 value = string_get(step);

        if (value == '$' && string_is(step + 1, '\''))
        {
                string_address stop = lex_dollar_quote_end(step + 2);

                if (!string_get(stop))
                {
                        address_to at = stop;
                        return LEX_SKIP_UNCLOSED;
                }

                address_to at = stop + 1;
                return LEX_SKIP_STEPPED;
        }

        if (value == '\\' && string_get(step + 1))
        {
                address_to at = step + 2;
                return LEX_SKIP_STEPPED;
        }

        if (value == '\'' || value == '"')
        {
                string_address stop = lex_quote_end(step + 1, value);

                if (!string_get(stop))
                {
                        address_to at = stop;
                        return LEX_SKIP_UNCLOSED;
                }

                address_to at = stop + 1;
                return LEX_SKIP_STEPPED;
        }

        {
                string_address inner = lex_nested_at(step);
                string_address stop = inner ? lex_nesting(inner) : null;

                if (stop && stop > inner)
                {
                        address_to at = stop;
                        return LEX_SKIP_STEPPED;
                }
        }

        return LEX_SKIP_NOTHING;
}

/*
        How many groups on one line are allowed to run to the end of it and
        find no close.

        A ((, or a [[ where a word begins, is looked at for its close by a
        scan of what follows, and a scan that finds none is what makes the
        bytes ordinary operators. The next (( on the line is scanned in turn,
        so a line of n of them and no close was n scans of n bytes -- and
        each byte that opens a $( is a nested scan of its own -- which is
        4,000 bytes and thirty seconds. The first few unclosed ones are read
        as they always were; past LEX_SCAN_FAILURES they are not asked about,
        and are the operators the failed scans would have made them. A line
        that reaches this is a syntax error already.
*/
#define LEX_SCAN_FAILURES 8
static positive lex_scan_failed;

// One Bash arithmetic command token. Keeping its interior whole prevents the
// shell operators inside ((...)) -- notably ;, &&, < and > -- from becoming
// command-language tokens before the arithmetic parser sees them.
static KEEP string_address lex_arithmetic_end(string_address start)
{
        string_address at = start + 2;
        positive depth = 0;

        //      dash has no (( )): two parentheses are two subshells.
        if (shell_dash_compat || lex_scan_failed >= LEX_SCAN_FAILURES)
                return null;

        while (string_get(at))
        {
                b32 skipped = lex_skip_held(address_of at);

                if (skipped == LEX_SKIP_UNCLOSED)
                {
                        lex_scan_failed++;
                        return null;
                }

                if (skipped)
                        continue;

                if (string_is(at, '('))
                        depth++;
                else if (string_is(at, ')'))
                {
                        if (depth)
                                depth--;
                        else if (string_is(at + 1, ')'))
                                return at + 2;
                }

                at++;
        }

        lex_scan_failed++;
        return null;
}

// A Bash [[...]] condition is one command-language token. Its own &&, ||,
// parentheses, < and > belong to the conditional grammar, while the same
// bytes after the closing ]] belong to the shell grammar again.
static KEEP string_address lex_conditional_end(string_address start)
{
        string_address at = start + 2;

        //      dash has no [[: it is a word, and a command that is not found.
        if (shell_dash_compat)
                return null;
        //      Whether a word may begin here: after a blank, or in bash
        //      after one of the condition's own operators, so that
        //      [[ (a == a)]] closes where bash closes it.
        bool word_start = false;

        if (lex_scan_failed >= LEX_SCAN_FAILURES)
                return null;

        while (string_get(at))
        {
                //      value is read before the step, because the ]] test
                //      below is about the byte the scanner is standing on and
                //      not about wherever a skipped run has left it.
                p8 value = string_get(at);
                b32 skipped = lex_skip_held(address_of at);

                if (skipped == LEX_SKIP_UNCLOSED)
                {
                        lex_scan_failed++;
                        return null;
                }

                if (skipped)
                {
                        word_start = false;
                        continue;
                }

                if (value == ']' && string_is(at + 1, ']') && word_start)
                {
                        p8 after = string_get(at + 2);

                        if (!after || lex_is_space(after) || lex_operator[after])
                                return at + 2;
                }

                word_start = lex_is_space(value) ||
                             (shell_bash_compat &&
                              (value == ')' || value == '(' || value == '&' ||
                               value == '|' || value == '<' || value == '>'));
                at++;
        }

        lex_scan_failed++;
        return null;
}

/*
        Where the nesting that starts here begins, or nothing when none does.

        $( ), ${ } and a backtick pair are the three, and what matters at every
        call site is the same: the byte lex_nesting has to be pointed at, which
        is the bracket and not the dollar in front of it.
*/
static string_address lex_nested_at(string_address at)
{
        if (string_get(at) == '`')
                return at;

        if (string_get(at) == '$' &&
            (string_get(at + 1) == '(' || string_get(at + 1) == '{'))
                return at + 1;

        //      bash's old spelling of $(( )): $[ expr ].
        if (shell_bash_compat && string_get(at) == '$' &&
            string_get(at + 1) == '[')
                return at + 1;

        return null;
}

/*
        Where a quoted run closes, given the byte after the opening quote.

        Single quotes hold everything up to the next one, which is one call.
        Double quotes let a backslash keep the byte behind it and carry $( ),
        ${ } and a backtick pair whole, because the quote that closes one of
        those is not the one that closes this.

        The answer is the closing quote itself, or the terminator when there is
        no partner, or a trailing backslash -- which is not the same as an
        unclosed quote and the caller reading a line wants to tell them apart.
*/
static string_address lex_quote_end_kind(string_address at, p8 quote,
                                         bool address_to command_open,
                                         positive nesting)
{
        string_address step = at;

        if (quote != '"')
                return string_first_of_or_end(at, quote);

        while (string_get(step) && string_get(step) != '"')
        {
                positive run = string_span(step, lex_in_double);

                if (run)
                {
                        step += run;
                        continue;
                }

                if (string_get(step) == '\\')
                {
                        if (!string_get(step + 1))
                                return step;

                        step += 2;
                        continue;
                }

                {
                        string_address inner = lex_nested_at(step);
                        string_address stop =
                            inner ? lex_nesting_at(inner, nesting,
                                                   string_is(inner, '{'))
                                  : null;

                        if (stop && stop > inner)
                                step = stop;
                        else if (inner)
                        {
                                /* A nested construct with no mate also leaves
                                   this outer quote open. Stepping over its `$`
                                   let the outer quote appear to close and made
                                   EOF execute the broken construct literally. */
                                if (command_open && string_is(inner, '(') &&
                                    string_not(inner + 1, '('))
                                        *command_open = true;
                                step = inner + string_length(inner);
                        }
                        else
                                step++;
                }
        }

        return step;
}

/*
        Past a nesting that a word carries whole.

        $( ), ${ } and a backtick pair hold whatever is between them, blanks and
        operators included, because what is in there is a command or a name and
        not this line's business.

        Returns where the nesting ends, one past its closing byte, or where it
        started when there is no closing byte at all -- an unfinished one is the
        parser's to complain about, the same as an unfinished quote.
*/
/* Whether the operator after a parameter name is # or %, the two POSIX
   pattern-removal forms whose words retain single-quote handling inside an
   outer double quote. */
static PURE bool lex_parameter_pattern(string_address at)
{
        string_address step = at;

        if (string_is(step, '!'))
                step++;
        if (string_is(step, '#'))
                step++;

        if (byte_is_alpha(string_get(step)) || string_is(step, '_'))
                step += string_span(step, string_set_name);
        else if (byte_is_digit(string_get(step)))
                step += string_span(step, string_set_digits);
        else if (string_get(step))
                step++;

        if (string_is(step, '['))
        {
                positive depth = 1;

                step++;
                while (string_get(step) && depth)
                {
                        if (string_is(step, '\\') && string_get(step + 1))
                                step += 2;
                        else if (string_is(step, '['))
                        {
                                depth++;
                                step++;
                        }
                        else if (string_is(step, ']'))
                        {
                                depth--;
                                step++;
                        }
                        else
                                step++;
                }
        }

        return string_is(step, '#') || string_is(step, '%');
}

/*
        Where a case command inside $( ) stands, so that the ) closing an
        unparenthesized pattern is not taken for the ) closing the
        substitution. POSIX spells `$(case $x in a) echo a;; esac)` this way
        and every shell reads it; counting parentheses alone ended the
        substitution at `a)` and left `echo a;; esac)` a syntax error.

        One entry per case still open, each remembering the parenthesis depth
        it was opened at: a ( ) group inside an arm is ordinary nesting.
*/
#define LEX_CASES 16

enum
{
        LEX_CASE_SUBJECT = 1, // after `case`, before its word
        LEX_CASE_IN,          // after the word, before `in`
        LEX_CASE_PATTERN,     // where a pattern (or esac) may stand
        LEX_CASE_BODY         // the commands of an arm
};

typedef struct
{
        p8 state[LEX_CASES];
        positive depth[LEX_CASES];
        positive count;
        positive pattern_parens;
        bool pattern_started;
        bool command; // the next word is in command position
} lex_cases;

static PURE bool lex_word_is(string_address at, const_string word,
                             positive length)
{
        for (positive i = 0; i < length; i++)
                if (string_get(at + i) != (p8)word[i])
                        return false;

        p8 after = string_get(at + length);

        return !after || lex_is_space(after) || lex_operator[after];
}

// A word begins here, in a command substitution: what it does to the cases.
static fn lex_cases_word(lex_cases address_to cases, string_address at,
                         positive depth)
{
        p8 state = cases->count ? cases->state[cases->count - 1] : 0;
        bool here = cases->count && cases->depth[cases->count - 1] == depth;
        bool command = cases->command;

        cases->command = false;

        if (here && state == LEX_CASE_SUBJECT)
        {
                cases->state[cases->count - 1] = LEX_CASE_IN;
                return;
        }
        if (here && state == LEX_CASE_IN)
        {
                if (lex_word_is(at, "in", 2))
                {
                        cases->state[cases->count - 1] = LEX_CASE_PATTERN;
                        cases->pattern_started = false;
                        cases->pattern_parens = 0;
                }
                return;
        }
        if (here && state == LEX_CASE_PATTERN)
        {
                if (!cases->pattern_started && lex_word_is(at, "esac", 4))
                        cases->count--;
                cases->pattern_started = true;
                return;
        }
        if (!command)
                return;

        if (lex_word_is(at, "case", 4) && cases->count < LEX_CASES)
        {
                cases->state[cases->count] = LEX_CASE_SUBJECT;
                cases->depth[cases->count] = depth;
                cases->count++;
                return;
        }
        if (here && state == LEX_CASE_BODY && lex_word_is(at, "esac", 4))
        {
                cases->count--;
                return;
        }

        /* After a reserved word that begins a list the next word is a
           command again: `if case ...`, `then case ...`. */
        cases->command =
            lex_word_is(at, "then", 4) || lex_word_is(at, "do", 2) ||
            lex_word_is(at, "else", 4) || lex_word_is(at, "elif", 4) ||
            lex_word_is(at, "if", 2) || lex_word_is(at, "while", 5) ||
            lex_word_is(at, "until", 5) || lex_word_is(at, "!", 1) ||
            lex_word_is(at, "{", 1) || lex_word_is(at, "time", 4);
}

/* An operator byte in a command substitution: whether it belongs to a case
   pattern rather than to the nesting. Answers true when the byte is taken. */
static bool lex_cases_operator(lex_cases address_to cases, string_address at,
                               positive depth)
{
        p8 c = string_get(at);
        bool here = cases->count && cases->depth[cases->count - 1] == depth;
        p8 state = here ? cases->state[cases->count - 1] : 0;

        cases->command = c != '<' && c != '>';

        if (state == LEX_CASE_PATTERN)
        {
                if (c == '(')
                {
                        if (cases->pattern_started)
                                cases->pattern_parens++;
                        cases->pattern_started = true;
                        return true;
                }
                if (c == ')')
                {
                        if (cases->pattern_parens)
                        {
                                cases->pattern_parens--;
                                return true;
                        }
                        cases->state[cases->count - 1] = LEX_CASE_BODY;
                        cases->command = true;
                        return true;
                }
                return false;
        }

        if (state == LEX_CASE_BODY && c == ';' &&
            (string_is(at + 1, ';') || string_is(at + 1, '&')))
        {
                cases->state[cases->count - 1] = LEX_CASE_PATTERN;
                cases->pattern_started = false;
                cases->pattern_parens = 0;
        }

        return false;
}

static string_address lex_nesting_at(string_address at, positive nesting,
                                     bool posix_double)
{
        p8 open = string_get(at);
        p8 close = open == '(' ? ')' : open == '{' ? '}'
                 : open == '[' ? ']' : open;
        positive depth = 0;
        string_address step = at;
        string_address line = at + 1;
        /* ${...} and $((...)) have their own # operators. Command and process
           substitutions contain shell commands, so a fresh # hides every
           delimiter through the newline just as it does in the outer lexer.
           Legacy backticks are different: Bash finds their raw closing tick
           before parsing the extracted command, so a comment does not hide
           that delimiter. */
        //      Bash 5.3's ${ command; } and ${| command; } hold commands
        //      too, and a { in them is a group whose } is not the end.
        bool funsub = shell_bash_compat && open == '{' &&
                      (string_is(at + 1, ' ') || string_is(at + 1, '\t') ||
                       string_is(at + 1, '\n') || string_is(at + 1, '|'));
        bool commands = (open == '(' && !string_is(at + 1, '(')) || funsub;
        bool fresh = commands;
        bool comment = false;
        bool maybe_here = false;
        bool raw_single = posix_double && open == '{' && shell_posix_on() &&
                          !lex_parameter_pattern(at + 1);
        lex_cases cases;

        cases.count = 0;
        cases.command = true;

        if (nesting >= EXPAND_DEPTH)
                return at;

        while (string_get(step))
        {
                p8 c = string_get(step);

                if (comment)
                {
                        if (c == '\n')
                        {
                                string_address after = step + 1;

                                if (maybe_here)
                                {
                                        after = parse_here_skip_bodies(line,
                                                                        step);
                                        if (!after)
                                                return at;
                                }

                                step = after;
                                comment = false;
                                fresh = true;
                                maybe_here = false;
                                line = step;
                        }
                        else
                                step++;

                        continue;
                }

                /* A command substitution is parsed as non-interactive input,
                   including while its containing line is interactive. */
                if (commands && fresh && c == '#')
                {
                        comment = true;
                        step++;
                        continue;
                }

                if (commands && fresh && !lex_is_space(c) && !lex_operator[c])
                        lex_cases_word(&cases, step, depth);

                if (c == '$' && string_is(step + 1, '\''))
                {
                        string_address stop = lex_dollar_quote_end(step + 2);

                        if (!string_get(stop))
                                return at;

                        step = stop + 1;
                        fresh = false;
                        continue;
                }

                if (c == '\\' && string_get(step + 1))
                {
                        p8 carried = string_get(step + 1);

                        step += 2;

                        /* Backslash-newline removes both bytes.  It does not
                           begin a word and therefore preserves whether # was
                           fresh on the physical line before it. */
                        if (carried != '\n')
                                fresh = false;
                        continue;
                }

                if (c == '\'' && raw_single)
                {
                        step++;
                        fresh = false;
                        continue;
                }

                if (c == '\'' || c == '"')
                {
                        /* Quotes inside nested commands use the same scanner
                           as outer words. Carry depth through both scanners
                           rather than restarting its recursion guard. Legacy
                           backticks retain their raw escaped-quote grammar. */
                        step = open == '`' && c == '"'
                            ? lex_escaped_end(step + 1, c)
                            : lex_quote_end_kind(step + 1, c, null, nesting + 1);

                        if (string_get(step))
                                step++;

                        fresh = false;
                        continue;
                }

                /* A nested substitution is one word piece in this command.
                   Walk it independently so its comment state and closing
                   delimiter cannot leak into the containing word. */
                if ((c == '$' &&
                     (string_is(step + 1, '(') || string_is(step + 1, '{') ||
                      (shell_bash_compat && string_is(step + 1, '[')))) ||
                    ((c == '<' || c == '>') && string_is(step + 1, '(')) ||
                    (c == '`' && open != '`'))
                {
                        string_address inner = step + (c != '`');
                        string_address stop =
                            lex_nesting_at(inner, nesting + 1,
                                           c == '$' && posix_double && open == '{' &&
                                               string_is(inner, '{'));

                        if (stop == inner)
                                return at;

                        step = stop;
                        fresh = false;
                        continue;
                }

                if (commands && c == '\n')
                {
                        string_address after = step + 1;

                        if (maybe_here)
                        {
                                after = parse_here_skip_bodies(line, step);
                                if (!after)
                                        return at;
                        }

                        step = after;
                        fresh = true;
                        maybe_here = false;
                        line = step;
                        //      A newline ends a command, so the next word
                        //      may be a case: `esac<newline>case 2 in 2)`
                        //      closed the substitution at the second arm.
                        cases.command = true;
                        continue;
                }

                /* A ) that sits in a here-document body does not close
                   $( ) on bash or on lima dash 0.5.x. Remember << until
                   the newline that begins the body, then skip those
                   lines. A ) still on the operator line closes, which is
                   how dash reads $(cat <<EOF) before any body arrives. */
                if (commands && c == '<' &&
                    lex_dless_candidate(line, step))
                        maybe_here = true;

                // A backtick pair has the same byte at both ends, so it
                // opens on the first one and closes on the next.
                // `${...}` ends at the first unquoted `}`: nested
                // substitutions are walked above, and a bare `{` is not
                // another expansion. The opening brace still counts
                // (depth is 0 only on that first byte). Parentheses in
                // `$( )` / `$(( ))` keep nesting.
                if (commands && depth && lex_operator[c] &&
                    lex_cases_operator(&cases, step, depth))
                {
                        step++;
                        fresh = true;
                        continue;
                }
                if (commands && c == '\n')
                        cases.command = true;

                if (open == close)
                {
                        if (c == open)
                                depth = depth ? 0 : 1;
                }
                else if (c == open && !(open == '{' && depth && !funsub))
                        depth++;
                else if (c == close)
                        depth--;

                step++;

                if (!depth)
                        return step;

                if (commands)
                        fresh = lex_is_space(c) || lex_operator[c];
        }

        return at;
}

static __attribute__((noinline)) string_address
lex_nesting(string_address at)
{
        return lex_nesting_at(at, 0, false);
}

/*
        Whether the line is all of the line.

        A shell reads a line at a time and the language does not: a quote, a
        substitution and a trailing backslash all say "the rest of this is
        further down". Nothing here looked, so the three of them arrived as a
        word with a stray quote in it, a $ with no command behind it, and a
        backslash somebody meant to be invisible.

        LEX_CONTINUES asks for the next line joined on with nothing between,
        which is what a backslash before a newline means. LEX_OPEN asks for it
        joined on with the newline kept, because a newline inside a quote or a
        substitution is a byte of the thing, not the end of it.
*/
#define LEX_COMPLETE 0
#define LEX_CONTINUES 1
#define LEX_OPEN 2
// Bash treats an EOF inside a word differently from incomplete command
// grammar when eval/dot returns to its caller. The parser still joins both
// kinds with a newline; only its EOF boundary needs this distinction.
#define LEX_OPEN_WORD 3

static p8 lex_unmatched;

static COLD b32 lex_open_match(b32 kind, p8 match)
{
        lex_unmatched = match;
        return kind;
}

static PURE p8 lex_unmatched_now()
{
        return lex_unmatched;
}

/*
        Whether this physical line is already complete: nothing left open
        that would ask for another physical line.

        Redirect `<`/`>` and a lone `[` are not themselves continuations:
        they stop the span so this can look at the next byte, then they
        carry on. A quote, a backslash, `$`, a backtick, `[[`, or `<( ` /
        `>(` still need the real unfinished walk.
*/
static bool lex_line_closed(string_address line)
{
        string_address step = line;

        lex_scan_failed = 0;
        lex_prepare();

        while (1)
        {
                p8 c;

                step += string_span(step, lex_closed);
                c = string_get(step);

                if (!c || c == '\n')
                        return true;

                if ((c == '<' || c == '>') && string_not(step + 1, '('))
                {
                        step++;
                        continue;
                }

                if (c == '[' &&
                    !(string_is(step + 1, '[') &&
                      lex_is_space(string_get(step + 2))))
                {
                        step++;
                        continue;
                }

                return false;
        }
}

static PURE bool lex_assignment_head(string_address text, positive length);

b32 lex_unfinished(string_address line)
{
        string_address step = line;

        lex_scan_failed = 0;
        // A # is a comment only where a word could have started, which is the
        // same rule lex_line uses -- echo a#b is one word and not half of one.
        bool fresh = true;
        bool comments = lex_comments_on();
        bool newline = lex_scan_newline;

        string_address word = null;

        lex_prepare();
        lex_unmatched = 0;

        while (string_get(step))
        {
                p8 c = string_get(step);
                positive run;

                if (fresh && c == '[' && string_is(step + 1, '[') &&
                    !shell_dash_compat && lex_is_space(string_get(step + 2)))
                {
                        string_address stop = lex_conditional_end(step);

                        if (!stop)
                                return lex_open_match(LEX_OPEN, ']');

                        step = stop;
                        fresh = false;
                        continue;
                }

                if (c == '#' && fresh)
                {
                        if (comments)
                                return LEX_COMPLETE;
                }

                //      A blank only ends a word and marks where another
                //      may begin; nothing about the byte behind it matters.
                //      It is much the commonest byte to reach here, so it
                //      answers before the look-ahead the operators need.
                if (string_set_blanks[c])
                {
                        fresh = true;
                        step++;
                        continue;
                }

                if (lex_operator[c])
                {
                        /*
                                An arithmetic command, on exactly the terms
                                lex_line_floor takes it: two parentheses, and
                                lex_arithmetic_end willing to close them. No
                                further condition, because the floor has
                                none -- a test of its own here is a way for
                                the two walks to disagree again.

                                They did disagree about one byte. `#` inside
                                `(( ))` is an arithmetic operator to the
                                floor, which keeps the whole `((...))`
                                together; here it was a line comment, so
                                `((#))<(` looked finished and the floor went
                                on to an unclosed `<(` nobody had asked the
                                reader about.
                        */
                        //      Three bytes can be followed by a `(` that
                        //      means something; every other operator skips
                        //      the look-ahead entirely.
                        if ((c == '(' || c == '<' || c == '>') &&
                            !shell_dash_compat && string_is(step + 1, '('))
                        {
                                if (c == '(')
                                {
                                        string_address stop =
                                            lex_arithmetic_end(step);

                                        if (stop)
                                        {
                                                step = stop;
                                                fresh = false;
                                                continue;
                                        }
                                }
                                else
                                {
                                        //      A process substitution holds
                                        //      a command, and the line is
                                        //      not finished until it closes.
                                        string_address stop =
                                            lex_nesting(step + 1);

                                        if (stop == step + 1)
                                                return lex_open_match(LEX_OPEN,
                                                                      ')');

                                        step = stop;
                                        fresh = false;
                                        continue;
                                }
                        }

                        /* a=( with its elements on the lines below: the
                           word is not finished until the parenthesis
                           closes, as lex_word will read it. */
                        if (c == '(' && shell_bash_compat && word &&
                            !fresh && step > word && step[-1] == '=' &&
                            lex_assignment_head(word,
                                                (positive)(step - word - 1)))
                        {
                                string_address stop = lex_nesting(step);

                                if (stop == step)
                                        return lex_open_match(LEX_OPEN, ')');

                                step = stop;
                                continue;
                        }

                        fresh = true;
                        step++;
                        continue;
                }

                if (fresh)
                        word = step;
                fresh = false;

                // A # past the first byte of a run is a byte of the word, so
                // the run may swallow it and the test above still sees the one
                // that begins a comment.
                run = string_span(step, lex_ordinary);

                if (run)
                {
                        step += run;
                        continue;
                }

                if (c == '\\')
                {
                        if (!string_get(step + 1))
                                return newline ? LEX_CONTINUES : LEX_COMPLETE;

                        step += 2;
                        continue;
                }

                if (c == '$' && string_is(step + 1, '\''))
                {
                        step = lex_dollar_quote_end(step + 2);

                        if (!string_get(step))
                                return lex_open_match(LEX_OPEN_WORD, '\'');

                        step++;
                        continue;
                }

                if (c == '\'' || c == '"')
                {
                        bool command_open = false;
                        step = lex_quote_end_kind(step + 1, c,
                                                  address_of command_open, 0);

                        // A backslash at the end inside double quotes is still
                        // a continuation: the quote is open and the line is
                        // short, and joining first is what makes the quote
                        // close on the next pass.
                        if (string_get(step) == '\\')
                                return LEX_CONTINUES;

                        if (!string_get(step))
                                return lex_open_match(command_open ? LEX_OPEN
                                                                   : LEX_OPEN_WORD,
                                                      c);

                        step++;
                        continue;
                }

                if (lex_nested_at(step))
                {
                        string_address inner = lex_nested_at(step);
                        string_address stop = lex_nesting(inner);

                        if (stop == inner)
                                return lex_open_match(
                                    string_is(inner, '(') &&
                                            string_not(inner + 1, '(')
                                        ? LEX_OPEN
                                        : LEX_OPEN_WORD,
                                    string_is(inner, '{')
                                        ? '}'
                                        : string_is(inner, '`') ? '`' : ')');

                        step = stop;
                        continue;
                }

                step++;
        }

        return LEX_COMPLETE;
}

/*
        One word, quotes and all.

        A run of ordinary bytes is taken whole by string_span; anything else is
        one byte of decision and then another run. A quote swallows to its
        partner, a backslash swallows the byte after it, and both stay in the
        text for the expander to deal with in its own order.
*/
/*
        Whether what has been read so far is the left of an assignment.

        This is the only question that makes a=(x y z) one word rather than a
        name followed by a subshell, and it is asked at one byte -- directly
        after the equals -- so that a command's own parentheses, a function
        definition and a case pattern are all untouched by it. Dash has no
        arrays, so the same bytes stay a name and a parenthesis.
*/
static PURE bool lex_assignment_head(string_address text, positive length)
{
        positive at = 0;

        if (length && text[length - 1] == '+')
                length--;

        if (!length || (text[0] >= '0' && text[0] <= '9'))
                return false;

        at = string_span_max(text, length, string_set_name);

        if (!at)
                return false;

        if (at == length)
                return true;

        // A subscript holds anything, but it has to be closed at the end.
        return text[at] == '[' && text[length - 1] == ']' && length - at > 2;
}

/*
        Whether the body of a=(...) is still words.

        The assignment parentheses are one token so they are not a subshell,
        but the interior is the same language as any other word list: an
        unquoted ( opens a nested command, which is a syntax error here,
        unless it is a substitution, a process substitution, or an extended
        pattern while that option is on. lex_nesting that swallowed the
        assignment counted every parenthesis, so *(a|b)* would otherwise
        look like one element.
*/
static bool lex_compound_body_legal(string_address open)
{
        string_address at = open + 1;

        if (string_not(open, '('))
                return true;

        while (string_get(at))
        {
                p8 c = string_get(at);
                string_address stop;
                b32 skipped;

                if (lex_is_space(c))
                {
                        at++;
                        continue;
                }

                if ((c == '<' || c == '>') && string_is(at + 1, '('))
                {
                        stop = lex_nesting(at + 1);

                        if (stop <= at + 1)
                                return false;

                        at = stop;
                        continue;
                }

                if (shell_extglob_asked() && lex_extended_head(c) &&
                    string_is(at + 1, '('))
                {
                        stop = lex_nesting(at + 1);

                        if (stop <= at + 1)
                                return false;

                        at = stop;
                        continue;
                }

                skipped = lex_skip_held(address_of at);

                if (skipped == LEX_SKIP_UNCLOSED)
                        return false;

                if (skipped)
                        continue;

                if (c == '(')
                        return false;

                //      A list of words has no place for a command's
                //      operators: a & or a ; or a redirection in it is the
                //      syntax error bash names at that token.
                if (c == ';' || c == '&' || c == '|' || c == '<' || c == '>')
                        return false;

                at++;
        }

        return true;
}

/* Find the closing bracket whose following bytes prove this is an assignment.
   Reuse the lexer's quote/substitution walker so a `]` held inside either one
   cannot close the subscript. */
static string_address lex_assignment_subscript_end(string_address at)
{
        string_address step = at;
        positive depth = 1;

        while (string_get(step) && string_not(step, '\n'))
        {
                b32 skipped = lex_skip_held(address_of step);

                if (skipped)
                        continue;

                if (string_is(step, '['))
                        depth++;
                else if (string_is(step, ']'))
                {
                        string_address after;

                        if (--depth)
                        {
                                step++;
                                continue;
                        }

                        after = step + 1;

                        if (string_is(after, '+'))
                                after++;

                        return string_is(after, '=') ? step + 1 : null;
                }

                step++;
        }

        return null;
}

static KEEP b32 lex_word(string_address address_to at)
{
        string_address step = address_to at;
        string_address start = step;

        while (1)
        {
                positive run = string_span(step, lex_ordinary);

                if (run)
                {
                        step += run;
                        continue;
                }

                p8 c = string_get(step);

                /* `[` is a decision byte so the ordinary word loop reaches
                   this only for bracketed words. A NAME prefix plus a close
                   followed by = or += makes the complete subscript one piece,
                   including otherwise separating blanks. */
                if (c == '[' &&
                    lex_assignment_head(start, (positive)(step - start)))
                {
                        string_address stop =
                            lex_assignment_subscript_end(step + 1);

                        if (stop)
                        {
                                step = stop;
                                continue;
                        }
                }

                /* a=(...) stays one word so the parenthesis is not a
                   subshell. Unquoted ( inside the body is diagnosed later.
                   Dash has no arrays: name=() is name= and then (, which
                   lima dash 0.5.x reports as "(" unexpected. Bash --posix
                   still reads the compound form. */
                if (shell_bash_compat && c == '(' && step > start &&
                    step[-1] == '=' &&
                    lex_assignment_head(start, (positive)(step - start - 1)))
                {
                        string_address stop = lex_nesting(step);

                        if (stop > step)
                        {
                                step = stop;
                                continue;
                        }
                }

                /*
                        An extended pattern group is one piece of the word,
                        parentheses and alternations and all -- which is what
                        makes case aab in +(a)b) a pattern rather than a
                        syntax error. Only under the option: with it off the
                        parenthesis is what it has always been.
                */
                if (c == '(' && step > start && shell_extglob_asked() &&
                    lex_extended_head(step[-1]))
                {
                        string_address stop = lex_nesting(step);

                        if (stop > step)
                        {
                                step = stop;
                                continue;
                        }
                }

                //      <( and >( carry a whole command inside the word,
                //      the same way $( ) does, so the blanks and operators in
                //      it are not this line's business.
                //      dash has none: the redirection is an operator and
                //      the parenthesis after it is a syntax error, which
                //      is the operator the floor sent here to be made.
                if ((c == '<' || c == '>') && string_is(step + 1, '(') &&
                    shell_dash_compat)
                {
                        if (step > start)
                                break;

                        step++;
                        address_to at = step;

                        return lex_add(LEX_OPERATOR, c == '<' ? OP_LESS : OP_GREAT,
                                       start, 1);
                }

                if ((c == '<' || c == '>') && string_is(step + 1, '('))
                {
                        string_address stop = lex_nesting(step + 1);

                        if (stop > step + 1)
                        {
                                step = stop;
                                continue;
                        }
                }

                if (!c || string_set_blanks[c] || lex_operator[c] || c == '\n')
                        break;

                step++;

                bool dollar_quote = c == '$' && string_is(step, '\'');
                if (dollar_quote || c == '\'' || c == '"')
                {
                        // Quotes retain their bytes, including the closing
                        // quote when present; the parser diagnoses open ones.
                        string_address stop = dollar_quote
                            ? lex_dollar_quote_end(step + 1) : lex_quote_end(step, c);
                        step = stop + (string_get(stop) ? 1 : 0);
                        continue;
                }

                if (c == '\\' && string_get(step))
                {
                        step++;
                        continue;
                }

                // $( ), ${ } and ` ` are one piece of the word however much
                // blank or operator is inside them.
                {
                        string_address stop = null;

                        if (c == '`')
                                stop = lex_nesting(step - 1);
                        else if (c == '$' && (string_get(step) == '(' ||
                                              string_get(step) == '{' ||
                                              (shell_bash_compat &&
                                               string_get(step) == '[')))
                                stop = lex_nesting(step);

                        if (stop && stop > step)
                                step = stop;
                }
        }

        /*
                A word always takes a byte, or the line is over.

                lex_line_floor reads the position back out of here and asks
                again from wherever this left it, and it has no progress test
                of its own: a word of no bytes is therefore not an empty
                token but an endless loop at one byte. The one shape that
                reached here without taking anything was `<(` or `>(` with no
                closing parenthesis -- the nesting walk refuses it, and the
                terminator test below it sends `<` straight back as an
                operator byte. `sh -c '((#))<('` spun a core on that forever.

                Whatever arrives here unconsumed is a byte of the word: the
                parser is the one that decides an unfinished construct is a
                syntax error, and it needs the token to say so.
        */
        if (step == start && string_get(step))
                step++;

        address_to at = step;

        return lex_add(LEX_WORD, 0, start, (positive)(step - start));
}

/*
        A whole line into tokens.

        Returns how many, or -1 when there were more than there is room for.
        A comment runs to the end of the line and is not a token.

        Every target has the assembly floor, and this is its contract: skip
        blanks; stop at the end, a newline, or a # where a word could begin
        while comments are on; then at each position try, in order, a [[
        conditional followed by a blank (lex_conditional_end), a (( arithmetic
        command (lex_arithmetic_end), an operator, and otherwise a word
        (lex_word), recording lex_at before each; close with LEX_END and
        answer the count before it.

        The operator rules, which each machine's block spells for itself and
        no C here spells any more, longest form first so &>> is never &>
        followed by > and >> is never two > tokens:

            lex_operator decides whether a byte can begin one at all;
            <( and >( are not operators -- they begin a process substitution
            that belongs to the word in front of it, so cat 2>(x) is the one
            word Bash reads it as;
            three bytes: <<< &>> ;;&
            two bytes:   && || |& ;& ;; << >> <& >& <> >|
            one byte:    ; | & < > ( )

        A change to any of them is a change in three places, and the shell
        and builtins differentials are what says the three still agree.
*/
b32 lex_line_floor(string_address line, b32 comments);

#include "lex_line_floor.inc"

HOT b32 lex_line(string_address line)
{
        lex_scan_failed = 0;
        lex_prepare();
        return lex_line_floor(line, lex_comments_on());
}
