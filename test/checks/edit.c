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

        /* A substitution held open over a heredoc is fed in pieces, and the
           lexer walks the document at every feed, speculative scans included.
           The walk keeps its entries to itself: however many times the text
           is walked, the registry the parser reads is left as it was. */
        parse_reset();
        p8 held_head[] = ": \"$(cat <<'E'\n";
        p8 held_rest[] = "inside\nE\n)\" tail";
        parse_feed(held_head);
        check("held substitution with a heredoc is accepted", parse_feed(held_rest));
        check("the walk leaves no here-document registered", here_wanted == 0 && here_filled == 0);
        parse_reset();
        p8 whole[] = ": \"$(cat <<'E'\ninside\nE\n)\" tail";
        parse_feed(whole);
        check("a complete substitution with a heredoc registers none", here_wanted == 0);
        parse_reset();
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
                                fast.owned_scratch = false;
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
                A repeated group pushes a frame and a choice for each
                iteration, so twenty thousand and one pairs once ran the
                scratch out, and the search reported "too complex". The
                scratch grows instead: the whole line matches, in both
                selections, and the iterations are counted in full.
        */
        static p8 pairs[2 * 20001];
        for (positive i = 0; i < sizeof(pairs); i++)
                pairs[i] = i & 1 ? 'b' : 'a';
        proof_check(regex_compile("(ab)*", true, false, true, REGEX_POLICY_DEFAULT));
        proof_check(regex_find(REGEX_LONGEST, pairs, sizeof(pairs), 0));
        proof_check(regex_slots[0] == 0 && regex_slots[1] == sizeof(pairs));
        proof_check(regex_find(REGEX_FIRST, pairs, sizeof(pairs), 0));
        proof_check(regex_slots[0] == 0 && regex_slots[1] == sizeof(pairs));
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

#ifdef CHECK_files_teamf
/*
        Regression checks for Files (FILES-001, FILES-002, FILES-003, FILES-004):
        - FILES-001: orphaned .trashinfo does not wedge trashing; empty bin does not prematurely unlink .trashinfo;
          deleting a top-level Trash/files entry unlinks its .trashinfo only after deletion succeeds.
        - FILES-002: files_bin_act uses pinned m->handle + files_model_check and does not follow an ancestor symlink swap.
        - FILES-003: files_moved resets files_pick_armed; save pick rejects directories and detects dangling symlinks.
        - FILES-004: files_move_deed records only completed items (copied[k] == 2), not partial/cancelled items (copied[k] == 1).
*/
#define FILES_MOST 4096u
#include "../../src/lib.util.c"
#include "../../src/moonwater/spark.c"
#include "../../src/sh/shell.c"
#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

static p8 tf_root[128];

static fn tf_path(p8 address_to into, const char address_to rel)
{
        string_copy_bounded((string_address)into, (string_address)tf_root, FILES_PATH);
        if (rel && rel[0])
        {
                string_append_bounded((string_address)into, (string_address) "/", FILES_PATH);
                string_append_bounded((string_address)into, (string_address)rel, FILES_PATH);
        }
}

static fn tf_mkdir(const char address_to rel)
{
        p8 p[FILES_PATH];
        tf_path(p, rel);
        system_call_3(syscall(mkdirat), (positive)AT_FDCWD, (positive)p, 0755);
}

static fn tf_write(const char address_to rel, const char address_to data)
{
        p8 p[FILES_PATH];
        bipolar fd;
        tf_path(p, rel);
        fd = (bipolar)system_call_4(syscall(openat), (positive)AT_FDCWD, (positive)p, FILE_CREATE | FILE_WRITE | O_TRUNC, 0644);
        if (fd >= 0)
        {
                system_write_all_checked((b32)fd, data, string_length((string_address)data));
                system_close(fd);
        }
}

static b32 tf_exists(const char address_to rel)
{
        p8 p[FILES_PATH];
        file_facts f;
        tf_path(p, rel);
        return files_look_at(AT_FDCWD, p, &f) >= 0;
}

static fn tf_model_open(struct files_model address_to m, const char address_to rel)
{
        p8 p[FILES_PATH];
        tf_path(p, rel);
        files_model_go(m, p);
        while (files_model_step(m, 1000))
                ;
}

static b32 tf_select_name(struct files_model address_to m, const char address_to want)
{
        for (p32 k = 0; k < files_model_count(m); k++)
        {
                const struct files_entry address_to e = files_model_entry(m, k);
                if (e && string_equals((string_address)files_model_name(m, e), (string_address)want))
                {
                        files_model_choose(m, k, FILES_CHOOSE_ONLY);
                        return true;
                }
        }
        return false;
}

b32 main(void)
{
        p8 bin[FILES_PATH], path[FILES_PATH], named[FILES_NAME], named2[FILES_NAME], old_sub[FILES_PATH], new_sub[FILES_PATH], victim[FILES_PATH];
        bipolar pid = (bipolar)system_call_1(syscall(getpid), 0);

        string_copy_bounded((string_address)tf_root, (string_address) "/tmp/mw_teamf_files_", sizeof(tf_root));
        positive len = string_length((string_address)tf_root);
        tf_root[len + positive_into(tf_root + len, (positive)pid)] = 0;
        system_call_3(syscall(mkdirat), (positive)AT_FDCWD, (positive)tf_root, 0755);

        setenv((string_address) "HOME", (string_address)tf_root, 1);

        files.big = (struct files_big address_to)files_anon_map(sizeof(*files.big), null);
        check("setup: files.big mapped", files.big != null);
        if (!files.big)
                return test_report(null);
        files_model_open(&files.big->model);
        files_bin_root(bin, null);

        /* ---- FILES-001 Case A: orphaned .trashinfo must not wedge trashing ---- */
        tf_mkdir("case1");
        tf_write("case1/foo.txt", "first");
        tf_path(path, "case1/foo.txt");
        check("FILES-001A: first trash succeeds", files_bin_put(bin, path, "2026-11-24T12:00:00", named) == FILES_DID);
        tf_path(path, ".local/share/Trash/files/foo.txt");
        system_remove_at(AT_FDCWD, (string_address)path, 0);
        tf_write("case1/foo.txt", "second");
        tf_path(path, "case1/foo.txt");
        check("FILES-001A: trashing same-named file with orphaned .trashinfo succeeds",
              files_bin_put(bin, path, "2026-11-24T12:00:01", named2) == FILES_DID);

        /* ---- FILES-001 Case B: empty bin must not prematurely unlink .trashinfo ---- */
        tf_write("case1/a.txt", "aaa");
        tf_write("case1/b.txt", "bbb");
        tf_path(path, "case1/a.txt");
        files_bin_put(bin, path, "2026-11-24T12:00:02", named);
        tf_path(path, "case1/b.txt");
        files_bin_put(bin, path, "2026-11-24T12:00:03", named);
        tf_model_open(&files.big->model, ".local/share/Trash/files");
        files_select_all(&files.big->model);
        files.armed = true;
        files.armed_at = files.big->model.revision;
        files_bin_empty();
        check("FILES-001B: a.txt.trashinfo still exists before delete job runs",
              tf_exists(".local/share/Trash/info/a.txt.trashinfo"));
        check("FILES-001B: b.txt.trashinfo still exists before delete job runs",
              tf_exists(".local/share/Trash/info/b.txt.trashinfo"));
        files_job_cancel(&files.big->job);
        files_job_end(&files.big->job);
        tf_model_open(&files.big->model, ".local/share/Trash/files");
        if (tf_select_name(&files.big->model, "a.txt"))
        {
                files.armed = true;
                files.armed_at = files.big->model.revision;
                files_delete();
                while (files_job_step(&files.big->model, &files.big->job, 16))
                        ;
                files_job_end(&files.big->job);
        }
        check("FILES-001B: deleting a.txt from Trash removes a.txt.trashinfo",
              !tf_exists(".local/share/Trash/info/a.txt.trashinfo"));
        check("FILES-001B: b.txt.trashinfo remains intact when only a.txt is deleted",
              tf_exists(".local/share/Trash/info/b.txt.trashinfo"));

        /* ---- FILES-002: symlink swap must not redirect files_bin_act to victim ---- */
        tf_mkdir("work");
        tf_mkdir("work/sub");
        tf_mkdir("victim");
        tf_write("work/sub/target", "original");
        tf_write("victim/target", "victim_secret");
        tf_model_open(&files.big->model, "work/sub");
        check("FILES-002: selected target in work/sub", tf_select_name(&files.big->model, "target"));
        tf_path(old_sub, "work/sub");
        tf_path(new_sub, "work/sub_old");
        tf_path(victim, "victim");
        files_rename_nr(AT_FDCWD, old_sub, AT_FDCWD, new_sub);
        system_call_3(syscall(symlinkat), (positive)victim, (positive)AT_FDCWD, (positive)old_sub);
        files_bin_act(false);
        check("FILES-002: victim/target is NOT moved to Trash after ancestor symlink swap",
              tf_exists("victim/target"));

        /* ---- FILES-003: save-pick overwrite confirmation resets on dir change & rejects dir/dangling symlink ---- */
        tf_mkdir("dirA");
        tf_mkdir("dirB");
        tf_mkdir("dirB/subdir");
        tf_write("dirA/out.txt", "A");
        tf_write("dirB/out.txt", "B");
        tf_path(path, "dirB/dangling");
        system_call_3(syscall(symlinkat), (positive)"/nonexistent_mw_teamf_target", (positive)AT_FDCWD, (positive)path);

        files_field_init(&files.pick_name, FILES_NAME - 1);
        ui_text_set(&files.pick_name, (const p8 address_to) "out.txt", 7);
        files_picking = true, files_pick_kind = FILES_PICK_SAVE, files_picked = false, files_pick_armed = false;
        tf_model_open(&files.big->model, "dirA");
        files_pick_act(1);
        check("FILES-003: first Save on existing dirA/out.txt arms confirmation", !files_picked && files_pick_armed);
        tf_model_open(&files.big->model, "dirB");
        files_moved();
        check("FILES-003: files_moved resets files_pick_armed across directory change", !files_pick_armed);
        files_picked = false;
        files_pick_act(1);
        check("FILES-003: Save in dirB requires fresh confirmation instead of overwriting immediately",
              !files_picked && files_pick_armed);

        ui_text_set(&files.pick_name, (const p8 address_to) "subdir", 6);
        files_picked = false, files_pick_armed = true;
        files_pick_act(1);
        check("FILES-003: Save refuses existing subdirectory even when armed", !files_picked);

        ui_text_set(&files.pick_name, (const p8 address_to) "dangling", 8);
        files_picked = false, files_pick_armed = false;
        files_pick_act(1);
        check("FILES-003: Save detects dangling symlink and arms confirmation", !files_picked && files_pick_armed);
        ui_text_close(&files.pick_name);

        /* ---- FILES-004: files_move_deed only records completed items (copied[k] == 2) ---- */
        {
                struct files_move job;
                p8 src1[FILES_PATH], src2[FILES_PATH], joined[FILES_PATH * 2];
                tf_mkdir("dst");
                tf_path(src1, "dirA/out.txt");
                tf_path(src2, "dirB/out.txt");
                positive l1 = string_length((string_address)src1), l2 = string_length((string_address)src2);
                memory_copy(joined, src1, l1 + 1);
                memory_copy(joined + l1 + 1, src2, l2 + 1);
                tf_path(path, "dst");
                bipolar dst_fd = files_open_dir(AT_FDCWD, path, 0);
                check("FILES-004: move job setup", files_move_begin(&job, FILES_MOVE_COPY, dst_fd, joined, 2));
                job.copied[0] = 2;
                string_copy_bounded((string_address)files_move_landed(&job, 0), (string_address) "ok.txt", FILES_NAME);
                job.copied[1] = 1;
                string_copy_bounded((string_address)files_move_landed(&job, 1), (string_address) "partial.txt", FILES_NAME);
                string_copy_bounded((string_address)files.move_into, (string_address)path, FILES_PATH);
                files.deed_count = files.deed_done = 0;
                files_move_deed(&job);
                check("FILES-004: deed records only completed item (copied == 2), not cancelled item (copied == 1)",
                      files.deed_count == 1 && files.deed[0].count == 1);
                files_move_end(&job);
        }

        files_model_close(&files.big->model);
        files_unmap(files.big, sizeof(*files.big));
        return test_report(null);
}
#endif


