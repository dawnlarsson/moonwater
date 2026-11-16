/*
        test/checks/edit.c: the editor's regressions, the lexer's spans and regular expressions.

        A run of test/checks.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. A section is built as
        test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib -nostartfiles
        -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its
        freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name>
        test/checks.c).
*/

#ifdef CHECK_edit_regressions
/*
        The editor's fixes that the edit lane has no row for: each caret takes
        its own tab stop, Up and Down on the first and last line keep the column
        the cursor remembers, a go-to-line number past the overflow goes to the
        last line, and a cut whose clipboard add fails leaves the text where it
        was. Keys go in through edit_key as a terminal sends them. The clipboard
        refusal is injected where memory_resize would refuse it, as the edit
        lane does for its own cases.

            gcc ... -DCHECK_edit_regressions -fwhole-program test/checks.c
*/
#include "../../src/lib.util.c"
#include "../../src/moonwater/spark.c"
#include "../../src/canvas/window.c"
#include "../../src/sh/pty.c"
#include "../../src/canvas/term.c"
#define EDIT_NO_DRIVER
static positive edit_check_requests;
static positive edit_check_failure;
#define memory_resize_growth(...) \
        (++edit_check_requests == edit_check_failure ? null : memory_resize_growth(__VA_ARGS__))
#include "../../src/sh/edit.c"
#undef memory_resize_growth
#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

static p8 edit_check_line(positive at, string_address want)
{
        positive length = string_length(want);

        if (at >= edit_line_count || edit_lines[at].length != length)
                return false;

        for (positive i = 0; i < length; i++)
                if (edit_lines[at].text[i] != (p8)want[i])
                        return false;

        return true;
}

static void edit_check_carets(positive count, positive *lines, positive *columns)
{
        edit_cursors_room_for(count);
        edit_cursor_count = count;

        for (positive at = 0; at < count; at++)
        {
                edit_cursors[at].line = lines[at];
                edit_cursors[at].column = columns[at];
                edit_cursors[at].anchor_line = lines[at];
                edit_cursors[at].anchor_column = columns[at];
                edit_cursors[at].wanted = columns[at];
                edit_cursors[at].selecting = false;
        }
}

int main(void)
{
        positive lines[2], columns[2];

        // Tab with two carets on different lines: caret one at column 0 takes
        // eight spaces, caret two at column 4 takes four.
        edit_load((string_address)"ab\ncdef", 7);
        lines[0] = 0; columns[0] = 0;
        lines[1] = 1; columns[1] = 4;
        edit_check_carets(2, lines, columns);
        edit_key(EDIT_KEY_TAB);
        check("tab: first caret takes eight spaces", edit_check_line(0, "        ab"));
        check("tab: second caret takes the spaces to its own stop",
              edit_check_line(1, "cdef    "));

        // Up on the first line, then Down: the remembered column of 5 is kept.
        edit_load((string_address)"abcdefgh\nabcdefgh\nab", 21);
        lines[0] = 0; columns[0] = 5;
        edit_check_carets(1, lines, columns);
        edit_key(EDIT_KEY_UP);
        check("up on the first line goes to column zero",
              edit_cursors[0].line == 0 && edit_cursors[0].column == 0);
        edit_key(EDIT_KEY_DOWN);
        check("down after that keeps the remembered column",
              edit_cursors[0].line == 1 && edit_cursors[0].column == 5);

        // A line number past the overflow goes to the last line.
        edit_load((string_address)"a\nb\nc\nd", 7);
        lines[0] = 0; columns[0] = 0;
        edit_check_carets(1, lines, columns);
        edit_key('g' | EDIT_KEY_CONTROL);
        //      2^64 + 2 wraps to line 2 in a 64-bit count, which is what the
        //      unchecked digit loop used to reach.
        for (string_address digit = (string_address)"18446744073709551618"; *digit; digit++)
                edit_key((positive)(*digit));
        edit_key(EDIT_KEY_ENTER);
        check("a line number past the overflow goes to the last line",
              edit_cursors[0].line == 3);

        // A cut whose clipboard add fails leaves the selection in the file.
        edit_load((string_address)"abc", 3);
        lines[0] = 0; columns[0] = 0;
        edit_check_carets(1, lines, columns);
        edit_key('a' | EDIT_KEY_CONTROL);
        edit_check_failure = edit_check_requests + 1;
        edit_key('x' | EDIT_KEY_CONTROL);
        edit_check_failure = 0;
        check("a cut the clipboard refuses keeps the text", edit_check_line(0, "abc"));
        check("a cut the clipboard refuses stores nothing", edit_clip_count == 0);

        return test_report(null);
}
#endif

#ifdef CHECK_lexer_spans
/* Counted lexer views borrow input; parser text must survive its reuse.
   Build with `build freestanding` or the shell lane's freestanding-checks runner. */
#include "../../src/lib.util.c"
#include "../../src/moonwater/spark.c"
#include "../../src/sh/shell.c"
#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

static fn lexer_span_owned(p8 *line, p8 *continuation,
                           string_address const *words, positive count)
{
        parse_reset();
        p8 ready = parse_feed(line);
        if (ready && continuation) ready = parse_feed(continuation);
        check("parser accepts source", ready);
        if (!ready) return;
        memory_fill(line, 'x', string_length(line));
        if (continuation) memory_fill(continuation, 'y', string_length(continuation));
        check("parser token count", parse_token_count == count + 1);
        if (parse_token_count != count + 1) return;
        for (positive i = 0; i < count; i++)
                check("parser owns terminated spelling",
                      parse_tokens[i].kind == PT_WORD &&
                      parse_tokens[i].length == string_length(words[i]) &&
                      !string_compare(parse_tokens[i].text, words[i]));
}

b32 main(void)
{
        shell_bash_compat = true;
        static const struct { string_address text; positive length; b32 kind; } cases[] = {
#define SPAN(word, kind) {word ";tail", sizeof(word) - 1, kind}
            SPAN("plain", LEX_WORD), SPAN("'one two'", LEX_WORD),
            SPAN("\"${x:-two words}\"", LEX_WORD), SPAN("escaped\\ word", LEX_WORD),
            SPAN("a[1 + 2]=x", LEX_WORD), SPAN("a=(one 'two three')", LEX_WORD),
            SPAN("[[ x == x ]]", LEX_CONDITIONAL), SPAN("((a=1+2))", LEX_ARITHMETIC),
            SPAN("\"$(cat <<'E'\ninside ) text\nE\n)\"", LEX_WORD),
#undef SPAN
        };
        for (positive i = 0; i < array_count(cases); i++)
        {
                b32 count = lex_line(cases[i].text);
                check("word, separator, tail", count == 3);
                if (count != 3) continue;
                check("whole token borrows exact source", lex_tokens[0].kind == cases[i].kind &&
                      lex_tokens[0].text == cases[i].text && lex_tokens[0].length == cases[i].length);
                check("counted end is a separator", lex_tokens[0].text[lex_tokens[0].length] == ';' &&
                      lex_tokens[1].kind == LEX_OPERATOR && lex_tokens[1].op == OP_SEMI);
                check("following token retains its own span", lex_tokens[2].kind == LEX_WORD &&
                      lex_tokens[2].text == cases[i].text + cases[i].length + 1 && lex_tokens[2].length == 4);
        }

        p8 *pages = memory(3 * 4096);
        check("guard allocation", (bipolar)(positive)pages > 0);
        if ((bipolar)(positive)pages <= 0) return test_report(null);
        p8 guarded = !system_call_3(syscall(mprotect), (positive)pages, 4096, 0) &&
            !system_call_3(syscall(mprotect), (positive)(pages + 8192), 4096, 0);
        check("guard protection", guarded);
        if (guarded)
        {
                static const positive sizes[] = {1,2,3,4,7,8,15,16,31,32,63,64,127,128,255,256};
                for (positive i = 0; i < array_count(sizes); i++)
                {
                        positive length = sizes[i];
                        p8 *line = pages + 8191 - length;
                        memory_fill(line, 'a', length);
                        line[length] = 0;
                        check("guarded word borrows complete span", lex_line(line) == 1 &&
                              lex_tokens[0].text == line && lex_tokens[0].length == length);
                }

                static const struct {
                        string_address text;
                        b32 op;
                } operators[] = {
                    {";", OP_SEMI}, {"|", OP_PIPE}, {"&", OP_AMP},
                    {"<", OP_LESS}, {">", OP_GREAT}, {"(", OP_LPAREN},
                    {")", OP_RPAREN}, {"&&", OP_AND_IF}, {"||", OP_OR_IF},
                    {"<<<", OP_HERESTRING}, {"&>>", OP_ANDDGREAT},
                    {";;&", OP_DSEMIAND},
                };
                for (positive i = 0; i < array_count(operators); i++)
                {
                        positive length = string_length(operators[i].text);
                        p8 address_to line = pages + 8191 - length;

                        memory_copy(line, operators[i].text, length + 1);
                        b32 count = lex_line(line);
                        check("guarded final operator stays inside input",
                              count == 1 && lex_tokens[0].kind == LEX_OPERATOR &&
                                  lex_tokens[0].op == operators[i].op &&
                                  lex_tokens[0].length == length);
                }
        }
        memory_free(pages, 3 * 4096);

        static p8 many[2049];
        for (positive i = 0; i < 1024; i++) { many[2*i] = 'a'; many[2*i+1] = ' '; }
        b32 grown = lex_line(many);
        check("grown token table", grown == 1024);
        if (grown != 1024) return test_report(null);
        for (positive i = 0; i < 1024; i++)
                check("table growth preserves each source address",
                      lex_tokens[i].text == many + 2*i && lex_tokens[i].length == 1);
        lex_token held = lex_tokens[500];
        lex_frame frame;
        lex_nest_enter(&frame);
        check("nested lexer input", lex_line("'nested source'") == 1);
        lex_nest_leave(&frame);
        check("outer table restored", lex_count == 1025 && lex_tokens[500].text == held.text &&
              lex_tokens[500].length == held.length && lex_tokens[500].text[0] == 'a');

        p8 plain[] = ": plain 'quoted word' \\x";
        string_address plain_words[] = {":", "plain", "'quoted word'", "\\x"};
        lexer_span_owned(plain, null, plain_words, array_count(plain_words));
        p8 first[] = ": 'held", second[] = "more' tail";
        string_address held_words[] = {":", "'held\nmore'", "tail"};
        lexer_span_owned(first, second, held_words, array_count(held_words));
        p8 nested[] = ": \"$(cat <<'E'\ninside\nE\n)\" tail";
        string_address nested_words[] = {":", "\"$(cat <<'E'\ninside\nE\n)\"", "tail"};
        lexer_span_owned(nested, null, nested_words, array_count(nested_words));
        return test_report(null);
}
#endif
#ifdef CHECK_regex
/* Built by the regex lane. Compare complete-literal shortcuts with their original
   VM fallback, using the real shared library and injected resource budgets. */
#include "../../src/lib.util.c"
#include "../../src/moonwater/spark.c"
#include "../../src/sh/shell.c"

static positive proof_checks, proof_failures;

static fn proof_check(p8 condition)
{
        proof_checks++;
        proof_failures += !condition;
}

b32 main(void)
{
        static string_address patterns[] = {
            "(ab){32}", "((ab){4}){8}", "(ab){128}", "(ab){129}",
            "(a){1}", "a{0}b", "(a())b", "(a)(b)", "(ab){32}z",
            "(ab){1,2}", "(ab)*", "(a|b){2}", "(ab){2}\\1", "(^ab){2}",
        };
        static string_address subjects[] = {
            "", "a", "ab", "abab", "aabb", "xabab", "ABAB", "ab\nab",
        };
        static positive budgets[] = {0, 1, 5, 50, 100000000};
        static p32 capacities[] = {0, 2, RX_FIXED_MAX - 1, RX_FIXED_MAX};
        p8 repeated[260];
        for (positive i = 0; i < sizeof(repeated); i++)
                repeated[i] = i & 1 ? 'b' : 'a';

        for (positive p = 0; p < array_count(patterns); p++)
        for (b32 fold = 0; fold < 2; fold++)
        {
                regex_pool.used = (rx_mark){0};
                regex_program program;
                p8 compiled = rx_compile(&regex_pool, &program, patterns[p], true, fold, true,
                                           REGEX_POLICY_DEFAULT);
                proof_check(compiled);
                if (!compiled)
                        continue;
                if (p < 4)
                {
                        proof_check(!(program.flags & RX_LITERAL_PROVES));
                        proof_check(program.hints->literal_length == 2 &&
                                    !memory_compare(program.hints->literal, "ab", 2));
                        proof_check(program.hints->fixed_length == (p < 2 ? 64 : p == 2 ? 256 : 258));
                        proof_check(program.hints->fixed_work != 0);
                }
                rx_hints interpreter_hints = *program.hints;
                interpreter_hints.fixed_work = 0;
                regex_program interpreter = program;
                interpreter.hints = &interpreter_hints;
                for (positive s = 0; s < array_count(subjects) + 5; s++)
                {
                        string_address bytes = s < array_count(subjects) ? subjects[s] : repeated;
                        positive length = s < array_count(subjects) ? string_length(bytes) :
                            s == array_count(subjects) ? 63 :
                            s == array_count(subjects) + 1 ? 64 :
                            s == array_count(subjects) + 2 ? 65 :
                            s == array_count(subjects) + 3 ? 256 : 258;
                        for (p8 mode = REGEX_FIRST; mode <= REGEX_EXACT_LONGEST; mode++)
                        for (p8 boundary = REGEX_BOUNDARY_NONE; boundary <= REGEX_BOUNDARY_LINE; boundary++)
                        for (p8 capture = 0; capture < 2; capture++)
                        for (p8 pending = 0; pending < 2; pending++)
                        for (positive work = 0; work < array_count(budgets); work++)
                        for (positive room = 0; room < array_count(capacities); room++)
                        {
                                program.boundary = interpreter.boundary = boundary;
                                rx_match fast = regex_match;
                                fast.work_limit = budgets[work];
                                fast.frame_capacity = capacities[room];
                                fast.choice_capacity = fast.undo_capacity = room ? REGEX_SCRATCH_MAX : 0;
                                fast.pending_exhaustion = pending;
                                rx_match slow = fast;
                                p8 one = rx_find(&fast, &program, mode, capture, bytes, length, 0);
                                p8 two = rx_find(&slow, &interpreter, mode, capture, bytes, length, 0);
                                proof_check(one == two && fast.pending_exhaustion == slow.pending_exhaustion);
                                if (one == RX_MATCH && two == RX_MATCH)
                                        proof_check(!memory_compare(fast.slots, slow.slots,
                                            (capture ? (program.groups + 1) * 2 : 2) * sizeof(positive)));
                        }
                }
        }
        /* Reuse the same hint after its complete buffer held a maximal
           literal. Short, empty-count and unsupported proofs must not see it. */
        p8 longest[RX_FIXED_MAX];
        memory_fill(longest, 'a', sizeof(longest) - 1);
        longest[sizeof(longest) - 1] = 0;
        string_address reuse_patterns[] = {longest, "(ab){2}", "a{0}b", "[a-z]+", "((a){0}b){2}"};
        string_address reuse_subjects[] = {longest, "abab", "b", "xyz", "bb"};
        regex_pool.used = (rx_mark){0};
        for (positive round = 0; round < 4; round++)
        for (positive i = 0; i < array_count(reuse_patterns); i++)
        {
                proof_check(regex_compile(reuse_patterns[i], true, false, true, REGEX_POLICY_DEFAULT));
                positive length = string_length(reuse_subjects[i]);
                proof_check(regex_find(REGEX_LONGEST, reuse_subjects[i], length, 0));
                proof_check(regex_slots[0] == 0 && regex_slots[1] == length);
        }
        /*
                The shortcuts of rx_find that look at a pattern's shape (a
                class, a run of one, a run at the front, a run at the end)
                and the character that is a byte below 0x80, against the
                walk with none of them: the hints that name them taken out
                of a copy of the program and the mark of the character
                taken off a copy of its nodes. Same answer, same slots, in
                every mode and for every subject here, UTF-8 or not.
        */
        static string_address shape_patterns[] = {
            ".*", ".*o", "a.*b", ".+x", "[^,]*,", "[^,]+", "([^:]*):(.*)", ".{3}", ".{2,4}x",
            "(.*)(.)", "x*", "x+", "[0-9]+", "[0-9][0-9]*", "[a-z]+=", "([a-z]+)-([0-9]+),",
            " *$", "[ \t]*$", "a*$", ".*$", "[aeiou]", ".", "[0-9]*\\.html", "(a*)b", "(a*b)c",
            "((a*))b", "a+b|c", "x{2,}y", "\xc3\xa9+", ".\xc3\xa9", "\xc3\xa9.*\xc3\xa9", "[^a]*",
            "[^a]*b", "[a-c]+c", "[a-c]*c", "(b*)(b*)b", "a*a*a", "b+$", "^.*$", "[a-z]{2,}",
            "a.*b.*c", "x.*y", "w.*d", "(.*)x(.*)y", "key.*val", "foo.*bar", "o.*o", "ab.*ca",
            "o$", "\\.html$", "^hello world$", "ld$", "^aaa$", "^abc$", "3,$", "z$",
        };
        static string_address shape_subjects[] = {
            "", "a", "x", "o", "hello world", "key=value", "foo-12, bar-3,", "  trailing   ", "a,b,,c",
            "aaaaaaaaaabaaaaaaaaaab", "ab\nab", "h\xc3\xa9llo w\xc3\xb6rld", "\xe6\x97\xa5\xe6\x9c\xac text",
            "\xc3\xa9\xc3\xa9\xc3\xa9", "\xff\xfe a b", "ab12.html cd345.html", "wxyzzx xx xxy", "bbb", "abcabcabcc",
            "a:b:c", "  \t \t", "x\xc3\xa9y,z",
        };
        static p8 shape_policies[] = {REGEX_POLICY_DEFAULT, REGEX_POLICY_DEFAULT | REGEX_CHARACTERS,
                                      REGEX_BASIC_REPEATS | REGEX_LINE_ANCHORS | REGEX_CHARACTERS,
                                      REGEX_BASIC_REPEATS};
        static rx_node plain_nodes[4096];

        for (positive p = 0; p < array_count(shape_patterns); p++)
        for (positive q = 0; q < array_count(shape_policies); q++)
        for (b32 fold = 0; fold < 2; fold++)
        {
                regex_pool.used = (rx_mark){0};
                regex_program program;
                p8 compiled = rx_compile(&regex_pool, &program, shape_patterns[p], true, fold, true,
                                           shape_policies[q]);
                proof_check(compiled);
                if (!compiled || regex_pool.used.nodes > array_count(plain_nodes))
                        continue;
                rx_hints plain_hints = *program.hints;
                plain_hints.lead_run = plain_hints.lead_next = plain_hints.shape_node = 0;
                plain_hints.lead_minimum = plain_hints.shape_minimum = 0;
                plain_hints.lead_tail = plain_hints.shape = 0;
                plain_hints.extra_length = 0;
                memory_copy_apart(plain_nodes, program.nodes, regex_pool.used.nodes * sizeof(rx_node));
                for (positive n = 0; n < regex_pool.used.nodes; n++)
                        if (plain_nodes[n].kind == RX_ALT)
                                plain_nodes[n].minimum = 0;
                regex_program plain = program;
                plain.hints = &plain_hints;
                plain.nodes = plain_nodes;
                for (positive s = 0; s < array_count(shape_subjects); s++)
                {
                        string_address bytes = shape_subjects[s];
                        positive length = string_length(bytes);
                        for (positive start = 0; start <= length && start < 4; start++)
                        for (p8 mode = REGEX_FIRST; mode <= REGEX_EXACT_LONGEST; mode++)
                        for (p8 boundary = REGEX_BOUNDARY_NONE; boundary <= REGEX_BOUNDARY_LINE; boundary++)
                        for (p8 capture = 0; capture < 2; capture++)
                        {
                                program.boundary = plain.boundary = boundary;
                                rx_match fast = regex_match;
                                fast.work_limit = 100000000;
                                fast.pending_exhaustion = false;
                                rx_match slow = fast;
                                p8 one = rx_find(&fast, &program, mode, capture, bytes, length, start);
                                p8 two = rx_find(&slow, &plain, mode, capture, bytes, length, start);
                                p8 same = one == two;
                                if (one == RX_MATCH && two == RX_MATCH)
                                        same = same && !memory_compare(fast.slots, slow.slots,
                                            (capture ? (program.groups + 1) * 2 : 2) * sizeof(positive));
                                proof_check(same);
                                if (!same && proof_failures < 40)
                                        string_format(log, "shape %s policy %p fold %p subject %s start %p mode %p boundary %p capture %p: %p [%p %p] against %p [%p %p]\n",
                                                      shape_patterns[p], (positive)shape_policies[q], (positive)fold, bytes,
                                                      start, (positive)mode, (positive)boundary, (positive)capture,
                                                      (positive)one, fast.slots[0], fast.slots[1],
                                                      (positive)two, slow.slots[0], slow.slots[1]);
                        }
                }
        }
        /*
                A line without the second string a back-reference pattern
                cannot match without is no match, also when the caller (grep
                finding the longest string in a block) has already found the
                first: (a+)\1+b over a line of a thousand a's walked past
                the graph's ceiling and grep said "regular expression too
                complex" where GNU answers at once. The first string is the
                a's, the second is the b.
        */
        {
                static p8 line[5000];
                memory_fill(line, 'a', sizeof(line));
                regex_pool.used = (rx_mark){0};
                regex_program program;
                p8 compiled = rx_compile(&regex_pool, &program, "(a+)\\1+b", true, false, true,
                                           REGEX_POLICY_DEFAULT);
                proof_check(compiled);
                for (b32 known = 0; compiled && known < 2; known++)
                {
                        rx_match found = regex_match;
                        found.work_limit = 100000;
                        found.literal_known = known;
                        proof_check(program.hints->extra_length == 1 && program.hints->extra[0] == 'b');
                        proof_check(rx_find(&found, &program, REGEX_FIRST, false, line, sizeof(line), 0) ==
                                    RX_NO_MATCH);
                }
        }
        string_format(log, "%p checks, %p failures\n", proof_checks, proof_failures);
        log_flush();
        return proof_failures != 0;
}
#endif

