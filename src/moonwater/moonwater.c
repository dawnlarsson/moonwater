/*
        Moonwater's machine: the event source, the script, and the CLI.

        Spark stays the image and the spawn device. This file owns the rest.
        The numbers 14 and 15 stay in Spark's ioctl catalog so they are
        never reused; their structs live here.
*/

#ifndef MOONWATER_INCLUDED
#define MOONWATER_INCLUDED

#define MOONWATER_ATTACH 0u
#define MOONWATER_DETACH 1u
#define MOONWATER_WAIT 2u
#define MOONWATER_STATUS 3u
#define MOONWATER_END 4u
#define MOONWATER_ATTACHED 0x1u

struct machine_control {
        unsigned int op;     // MOONWATER_ATTACH .. END
        unsigned int event;  // 1..SPARK_BIND_EVENTS, or 0 for end
        unsigned int queued; // answered: events waiting
        unsigned int flags;  // answered: MOONWATER_ATTACHED
        unsigned int extra;  // pair on: 1, off: 0
        unsigned int reserved[3]; // WAIT: [0] timeout ms, 0 waits forever
        char name[SPARK_BIND_NAME_MAX];
        char unused[8];
};

_Static_assert(sizeof(struct machine_control) == 64, "moonwater machine ABI");

// _IOWR('s', 14, struct machine_control)
#define MOONWATER_IOCTL_MACHINE 0xc040730eu

#define MOONWATER_HOOK_INIT 1u
#define MOONWATER_HOOK_EVENT 2u
#define MOONWATER_HOOK_END 4u
#define MOONWATER_HOOKS 3u

#define MOONWATER_SCRIPT_BYTES 65536u
#define MOONWATER_SCRIPT_GET 0u
#define MOONWATER_SCRIPT_SET 1u
#define MOONWATER_ORIGIN_BUILTIN 0u
#define MOONWATER_ORIGIN_DISK 1u

struct moonwater_overlay {
        unsigned char hooks;
        unsigned char pad;
        unsigned short init_line;
        unsigned short event_line;
        unsigned short end_line;
        unsigned short star_line;
        unsigned short bind_line[SPARK_BIND_EVENTS];
        unsigned short spare;
};

_Static_assert(sizeof(struct moonwater_overlay) == 56, "moonwater overlay");

static const struct {
        char name[16];
        unsigned char bit;
        unsigned char line_off;
} moonwater_hook[MOONWATER_HOOKS] = {
        { "moonwater_init", MOONWATER_HOOK_INIT,
          (unsigned char)__builtin_offsetof(struct moonwater_overlay, init_line) },
        { "moonwater_event", MOONWATER_HOOK_EVENT,
          (unsigned char)__builtin_offsetof(struct moonwater_overlay, event_line) },
        { "moonwater_end", MOONWATER_HOOK_END,
          (unsigned char)__builtin_offsetof(struct moonwater_overlay, end_line) },
};

static inline unsigned short moonwater_overlay_line(const struct moonwater_overlay *overlay,
                                                    unsigned char off)
{
        return *(const unsigned short *)((const unsigned char *)overlay + off);
}

static inline unsigned short moonwater_bind_line(const struct moonwater_overlay *overlay,
                                                 unsigned int event)
{
        if (!event || event > SPARK_BIND_EVENTS)
                return 0;
        return overlay->bind_line[event - 1];
}

#define MOONWATER_PAIRS 4

static const struct moonwater_pair {
        unsigned char on;
        unsigned char off;
        char name[12];
} moonwater_pairs[MOONWATER_PAIRS] = {
        { SPARK_BIND_CANVAS_ON, SPARK_BIND_CANVAS_OFF, "canvas" },
        { SPARK_BIND_TABLET_ON, SPARK_BIND_TABLET_OFF, "tablet" },
        { SPARK_BIND_HEADPHONE_ON, SPARK_BIND_HEADPHONE_OFF, "headphone" },
        { SPARK_BIND_DOCK_ON, SPARK_BIND_DOCK_OFF, "dock" },
};

static inline const struct moonwater_pair *moonwater_paired(unsigned int event)
{
        unsigned int i;

        for (i = 0; i < MOONWATER_PAIRS; i++)
                if (event == moonwater_pairs[i].on || event == moonwater_pairs[i].off)
                        return &moonwater_pairs[i];
        return 0;
}

static inline int moonwater_same_pair(unsigned int a, unsigned int b)
{
        const struct moonwater_pair *pair = moonwater_paired(a);

        return pair && (b == pair->on || b == pair->off);
}

/*
        The script text lives in the module: main.moonwater.sh at the
        repository root is baked in as the fallback, and /root/main.moonwater.sh
        overlays it when present. GET answers the overlay the CLI and the
        machine both use; SET replaces the live copy. The module scans once;
        userspace does not.
*/
/*
        length carries the room, both ways.

        On SET it is how many bytes are at address, which is what it always
        meant. On GET it is how many bytes fit there, and the answer is the
        script's own length: the kernel refuses with ENOSPC rather than
        writing past a buffer, and the refused request still carries the
        length so the caller knows what to allocate. Naming the room is not
        optional -- a GET with an address and no room is refused -- because
        the alternative is the kernel writing up to MOONWATER_SCRIPT_BYTES
        into a buffer whose size it was never told, which is a bug the day
        somebody writes a second caller rather than a bug today.
        spark_settings_request writes its contract into the field's own
        comment; this is the same contract, said the same way.

        A caller that wants only the overlay passes address 0, and then the
        room is not consulted.
*/
struct machine_script {
        unsigned int op;
        unsigned int origin;
        unsigned int length; // SET: bytes at address. GET: room there
        unsigned int flags; // none; nonzero is refused
        unsigned long address;
        struct moonwater_overlay overlay;
};

_Static_assert(sizeof(struct machine_script) == 80, "moonwater script ABI");
_Static_assert(__builtin_offsetof(struct machine_script, overlay) == 24,
               "moonwater script overlay");
_Static_assert(__builtin_offsetof(struct moonwater_overlay, bind_line) == 10,
               "moonwater overlay arms");

// _IOWR('s', 15, struct machine_script)
#define MOONWATER_IOCTL_SCRIPT 0xc050730fu

#if defined(STANDARD_MODERN_C_KERNEL) || defined(MOONWATER_SCAN)
/*
        Classify-once lexer: a 256-byte class table, the same shape as
        Plan 9 lex and the kernel n_tty map. Skip, punct, quote and word
        all switch on script_kind[c]; the walk does not re-test character
        classes.
*/

#define SCRIPT_WORD 64
#define SCRIPT_TOK_END 0
#define SCRIPT_TOK_WORD 1
#define SCRIPT_TOK_PUNCT 2
#define SCRIPT_TOK_DSEMI 3

#define SK_NONE 0
#define SK_SPACE 1
#define SK_NL 2
#define SK_HASH 3
#define SK_ESC 4
#define SK_PUNCT 5
#define SK_SEMI 6
#define SK_QUOTE 7
#define SK_WORD 8
#define SK_WORD2 9

static const unsigned char script_kind[256] = {
        ['\t'] = SK_SPACE, ['\n'] = SK_NL, ['\r'] = SK_SPACE, [' '] = SK_SPACE,
        ['#'] = SK_HASH, ['\\'] = SK_ESC,
        ['('] = SK_PUNCT, [')'] = SK_PUNCT, ['{'] = SK_PUNCT, ['}'] = SK_PUNCT,
        ['|'] = SK_PUNCT, [';'] = SK_SEMI,
        ['"'] = SK_QUOTE, ['\''] = SK_QUOTE, ['`'] = SK_QUOTE,
        ['A' ... 'Z'] = SK_WORD, ['a' ... 'z'] = SK_WORD,
        ['_'] = SK_WORD, ['$'] = SK_WORD, ['*'] = SK_WORD, ['?'] = SK_WORD,
        ['['] = SK_WORD, ['@'] = SK_WORD,
        ['0' ... '9'] = SK_WORD2, ['-'] = SK_WORD2, ['/'] = SK_WORD2,
        [']'] = SK_WORD2, ['.'] = SK_WORD2,
};

struct script_read {
        const unsigned char *text;
        unsigned long length;
        unsigned long at;
        unsigned short line;
        unsigned char heredoc_tabs;     // <<- strips leading tabs
        unsigned char heredoc_length;   // a body waits for the next newline
        unsigned long heredoc_total;    // its ending line's length, all of it
        unsigned long rescan;           // what may still be read again for one
        char heredoc[SCRIPT_WORD];      // the start of the line that ends it
};

struct script_token {
        unsigned char kind;
        unsigned char punct;
        unsigned char fresh; // a newline came before it
        unsigned short line;
        char word[SCRIPT_WORD];
};

/*
        The scanner stands alone on purpose. moonwater_scan is the one reader of
        the machine script, expanded in the kernel module and in the moonwater
        command, and compiled by itself, with no library under it, by the
        core_state harness in test/differential.py: so its string compare, its
        clearing and its prefix test are these, and not lib.c's.
*/
static int script_eq(const char *a, const char *b)
{
        while (*a && *a == *b) {
                a++;
                b++;
        }
        return *a == *b;
}

static int script_word_is(const struct script_token *token, const char *text)
{
        return token->kind == SCRIPT_TOK_WORD && script_eq(token->word, text);
}

static int script_punct_is(const struct script_token *token, unsigned char value)
{
        return token->kind == SCRIPT_TOK_PUNCT && token->punct == value;
}

/*
        A here-document's body is text, not script: a function spelled in one
        is no function, and bash defines none. <<WORD, <<'WORD', <<"WORD" and
        <<-WORD note the ending line; the body is skipped at the newline that
        ends the command, through the line that is the word alone.
*/
static void script_heredoc_start(struct script_read *scan)
{
        unsigned char quote = 0;
        unsigned int used = 0;
        unsigned long total = 0;

        scan->at += 2;
        scan->heredoc_tabs = scan->at < scan->length && scan->text[scan->at] == '-';
        if (scan->heredoc_tabs)
                scan->at++;
        while (scan->at < scan->length &&
               (scan->text[scan->at] == ' ' || scan->text[scan->at] == '\t'))
                scan->at++;
        while (scan->at < scan->length) {
                unsigned char value = scan->text[scan->at];

                if (!quote && (value == '\'' || value == '"')) {
                        quote = value;
                        scan->at++;
                        continue;
                }
                if (quote && value == quote) {
                        quote = 0;
                        scan->at++;
                        continue;
                }
                if (value == '\n' ||
                    (!quote && (value == ' ' || value == '\t' || value == ';' ||
                                value == '|' || value == '&' || value == '>' ||
                                value == '<' || value == ')')))
                        break;
                if (value != '\\') {
                        total++;
                        if (used + 1 < SCRIPT_WORD)
                                scan->heredoc[used++] = (char)value;
                }
                scan->at++;
        }
        scan->heredoc[used] = 0;
        scan->heredoc_length = (unsigned char)used;
        scan->heredoc_total = total;
}

static void script_skip_heredoc(struct script_read *scan)
{
        //      A << that no line ever closes is not taken as a here-document
        //      -- a shift in $(( )) reads the same -- and nothing is skipped.
        //      Finding that out reads to the end of the text, and a script of
        //      nothing but shifts would do it from every one of them, under
        //      the script lock: so what may be read again for them is
        //      bounded, and past it a << is a shift.
        unsigned long at = scan->at;
        unsigned long open = scan->at;
        unsigned short lines = 0;
        unsigned long length = scan->heredoc_length;

        while (scan->rescan && at < scan->length) {
                unsigned long start = ++at, stop = start, from;

                lines++;
                while (stop < scan->length && scan->text[stop] != '\n')
                        stop++;
                from = start;
                if (scan->heredoc_tabs)
                        while (from < stop && scan->text[from] == '\t')
                                from++;
                at = stop;
                if (stop - from == scan->heredoc_total) {
                        unsigned long k = 0;

                        while (k < length &&
                               scan->text[from + k] == (unsigned char)scan->heredoc[k])
                                k++;
                        if (k == length) {
                                scan->at = at;
                                scan->line += lines;
                                break;
                        }
                }
        }
        if (scan->at == open) {
                //      Unclosed: the newline is an ordinary one.
                unsigned long spent = at > open ? at - open : 0;

                scan->rescan -= spent < scan->rescan ? spent : scan->rescan;
                scan->at++;
                scan->line++;
        }
        scan->heredoc_length = 0;
}

static void script_next(struct script_read *scan, struct script_token *token)
{
        unsigned char value, quote, kind, fresh = 0;
        unsigned int used = 0;
        unsigned char *b = (unsigned char *)token;
        unsigned long n = sizeof(*token);

        while (n--)
                *b++ = 0;
        while (scan->at < scan->length) {
                value = scan->text[scan->at];
                kind = script_kind[value];
                if (kind == SK_NL && scan->heredoc_length) {
                        script_skip_heredoc(scan);
                        fresh = 1;
                        continue;
                }
                if (kind == SK_NL || kind == SK_SPACE) {
                        fresh |= kind == SK_NL;
                        scan->line += kind == SK_NL;
                        scan->at++;
                        continue;
                }
                if (kind == SK_ESC && scan->at + 1 < scan->length &&
                    scan->text[scan->at + 1] == '\n') {
                        scan->at += 2;
                        scan->line++;
                        continue;
                }
                if (kind == SK_HASH) {
                        while (scan->at < scan->length &&
                               scan->text[scan->at] != '\n')
                                scan->at++;
                        continue;
                }
                break;
        }
        token->line = scan->line;
        token->fresh = fresh;
        if (scan->at >= scan->length) {
                token->kind = SCRIPT_TOK_END;
                return;
        }

        value = scan->text[scan->at];
        kind = script_kind[value];
        if (kind == SK_SEMI && scan->at + 1 < scan->length &&
            scan->text[scan->at + 1] == ';') {
                scan->at += 2;
                token->kind = SCRIPT_TOK_DSEMI;
                return;
        }
        if (value == '<' && scan->at + 2 < scan->length &&
            scan->text[scan->at + 1] == '<' && scan->text[scan->at + 2] != '<') {
                script_heredoc_start(scan);
                token->kind = SCRIPT_TOK_PUNCT;
                token->punct = '<';
                token->word[0] = '<';
                return;
        }
        if (kind == SK_PUNCT || kind == SK_SEMI) {
                scan->at++;
                token->kind = SCRIPT_TOK_PUNCT;
                token->punct = value;
                token->word[0] = (char)value;
                return;
        }

        token->kind = SCRIPT_TOK_WORD;
        if (kind == SK_QUOTE) {
                quote = value;
                scan->at++;
                while (scan->at < scan->length && scan->text[scan->at] != quote) {
                        value = scan->text[scan->at];
                        if (quote != '\'' && value == '\\' &&
                            scan->at + 1 < scan->length) {
                                scan->at++;
                                value = scan->text[scan->at];
                        }
                        if (value == '\n')
                                scan->line++;
                        if (used + 1 < SCRIPT_WORD)
                                token->word[used++] = (char)value;
                        scan->at++;
                }
                if (scan->at < scan->length)
                        scan->at++;
                return;
        }

        if (kind == SK_WORD) {
                while (scan->at < scan->length) {
                        kind = script_kind[scan->text[scan->at]];
                        //      Inside a word # is a letter; only at the
                        //      start of one does it open a comment.
                        if (kind != SK_WORD && kind != SK_WORD2 && kind != SK_HASH)
                                break;
                        if (used + 1 < SCRIPT_WORD)
                                token->word[used++] = (char)scan->text[scan->at];
                        scan->at++;
                }
                return;
        }

        scan->at++;
        token->kind = SCRIPT_TOK_PUNCT;
        token->punct = value;
        token->word[0] = (char)value;
}

static struct script_token script_peek(struct script_read *scan)
{
        struct script_read held = *scan;
        struct script_token token;

        script_next(scan, &token);
        *scan = held;
        return token;
}

static int script_event_is(const char *pattern, const char *event)
{
        while (*pattern && *event) {
                char want = *event == ' ' ? '_' : *event;

                if (*pattern != want && *pattern != *event)
                        return 0;
                pattern++;
                event++;
        }
        return !*pattern && !*event;
}

static void script_arm_event(struct moonwater_overlay *into, unsigned int event,
                             unsigned short line)
{
        if (event && event <= SPARK_BIND_EVENTS && !into->bind_line[event - 1])
                into->bind_line[event - 1] = line;
}

static void script_arm(struct moonwater_overlay *into, const char *pattern,
                       unsigned short line)
{
        unsigned int event, i;

        if (!into)
                return;
        if (script_eq(pattern, "*")) {
                if (!into->star_line)
                        into->star_line = line;
                return;
        }
        for (i = 0; i < MOONWATER_PAIRS; i++)
                if (script_eq(pattern, moonwater_pairs[i].name)) {
                        script_arm_event(into, moonwater_pairs[i].on, line);
                        script_arm_event(into, moonwater_pairs[i].off, line);
                        return;
                }
        for (event = 0; event < SPARK_BIND_EVENTS; event++)
                if (script_event_is(pattern, spark_bind_event_name[event])) {
                        script_arm_event(into, event + 1, line);
                        return;
                }
}

static void script_block(struct script_read *scan, struct moonwater_overlay *into,
                         unsigned short depth);

static void script_parse_case(struct script_read *scan,
                              struct moonwater_overlay *into)
{
        struct script_token token;
        unsigned short cases = 1;

        do {
                script_next(scan, &token);
                if (token.kind == SCRIPT_TOK_END)
                        return;
        } while (!script_word_is(&token, "esac") && !script_word_is(&token, "in"));
        if (script_word_is(&token, "esac"))
                return;

        while (cases) {
                script_next(scan, &token);
                if (token.kind == SCRIPT_TOK_END || script_punct_is(&token, '}'))
                        return;
                if (script_word_is(&token, "esac")) {
                        cases--;
                        continue;
                }
                if (script_punct_is(&token, '{')) {
                        script_block(scan, 0, 1);
                        continue;
                }
                if (script_word_is(&token, "case")) {
                        cases++;
                        continue;
                }
                if (script_punct_is(&token, '('))
                        script_next(scan, &token);

                while (token.kind == SCRIPT_TOK_WORD) {
                        script_arm(into, token.word, token.line);
                        script_next(scan, &token);
                        if (!script_punct_is(&token, '|'))
                                break;
                        script_next(scan, &token);
                }

                while (!script_punct_is(&token, ')') &&
                       token.kind != SCRIPT_TOK_END &&
                       token.kind != SCRIPT_TOK_DSEMI &&
                       !script_word_is(&token, "esac"))
                        script_next(scan, &token);
                if (token.kind == SCRIPT_TOK_END)
                        return;
                if (script_word_is(&token, "esac")) {
                        cases--;
                        continue;
                }

                for (;;) {
                        script_next(scan, &token);
                        if (token.kind == SCRIPT_TOK_END ||
                            script_punct_is(&token, '}'))
                                return;
                        if (token.kind == SCRIPT_TOK_DSEMI)
                                break;
                        if (script_word_is(&token, "case"))
                                cases++;
                        else if (script_word_is(&token, "esac")) {
                                cases--;
                                break;
                        } else if (script_punct_is(&token, '{'))
                                script_block(scan, 0, 1);
                }
        }
}

static void script_block(struct script_read *scan, struct moonwater_overlay *into,
                         unsigned short depth)
{
        struct script_token token;

        while (depth) {
                script_next(scan, &token);
                if (token.kind == SCRIPT_TOK_END)
                        return;
                if (script_punct_is(&token, '{'))
                        depth++;
                else if (script_punct_is(&token, '}'))
                        depth--;
                else if (into && script_word_is(&token, "case"))
                        script_parse_case(scan, into);
        }
}

/*
        A function's body is any compound command, and bash defines the
        function whichever one it is: a group, a subshell (or an arithmetic
        one, which is two), a [[ ]] test, an if, a loop or a case. Only the
        end of it has to be found, so that what follows is read as what
        follows. A group and a subshell are counted by their punctuation, the
        keyword ones by the keywords that open and close them, and those only
        where a command can begin -- `echo fi` closes nothing. A case inside
        either is read as a case, so that its patterns' ) are not taken for
        the end, and the arms of the event hook's are the rows it owns.
*/
static int script_command_start(const struct script_token *token)
{
        static const char *const before[] = {
                "if", "while", "until", "then", "do", "else", "elif", "!", "time", 0 };
        unsigned int i;

        if (token->kind == SCRIPT_TOK_DSEMI)
                return 1;
        if (token->kind == SCRIPT_TOK_PUNCT)
                return token->punct == ';' || token->punct == '|' ||
                       token->punct == '(' || token->punct == ')' ||
                       token->punct == '{' || token->punct == '}' ||
                       token->punct == '&';
        for (i = 0; before[i]; i++)
                if (script_word_is(token, before[i]))
                        return 1;
        return 0;
}

static void script_compound(struct script_read *scan, struct moonwater_overlay *into,
                            int parens)
{
        struct script_token token;
        unsigned short depth = 1;
        int command = 1;

        while (depth) {
                script_next(scan, &token);
                if (token.kind == SCRIPT_TOK_END)
                        return;
                if (token.fresh)
                        command = 1;
                if (parens && script_punct_is(&token, '('))
                        depth++;
                else if (parens && script_punct_is(&token, ')'))
                        depth--;
                else if (command && script_word_is(&token, "case"))
                        script_parse_case(scan, into);
                else if (command && !parens &&
                         (script_word_is(&token, "if") || script_word_is(&token, "while") ||
                          script_word_is(&token, "until") || script_word_is(&token, "for") ||
                          script_word_is(&token, "select")))
                        depth++;
                else if (command && !parens &&
                         (script_word_is(&token, "fi") || script_word_is(&token, "done")))
                        depth--;
                command = script_command_start(&token);
        }
}

static int script_take_function(struct script_read *scan, const char *name,
                                unsigned short line, struct moonwater_overlay *into)
{
        struct script_token token;
        unsigned int i;
        unsigned char hook = 0;
        unsigned char off = 0;

        token = script_peek(scan);
        if (script_punct_is(&token, '(')) {
                script_next(scan, &token);
                script_next(scan, &token);
                if (!script_punct_is(&token, ')'))
                        return 0;
                token = script_peek(scan);
        }
        if (!script_punct_is(&token, '{') && !script_punct_is(&token, '(') &&
            !script_word_is(&token, "[[") && !script_word_is(&token, "if") &&
            !script_word_is(&token, "while") && !script_word_is(&token, "until") &&
            !script_word_is(&token, "for") && !script_word_is(&token, "select") &&
            !script_word_is(&token, "case"))
                return 0;

        script_next(scan, &token);
        for (i = 0; i < MOONWATER_HOOKS; i++)
                if (script_eq(name, moonwater_hook[i].name)) {
                        hook = moonwater_hook[i].bit;
                        off = moonwater_hook[i].line_off;
                        break;
                }
        if (hook) {
                unsigned short *slot =
                        (unsigned short *)((unsigned char *)into + off);

                into->hooks |= hook;
                if (!*slot)
                        *slot = line;
        }
        if (name[0] == 'm' && name[1] == 'o' && name[2] == 'o' &&
            name[3] == 'n' && name[4] == 'w' && name[5] == 'a' &&
            name[6] == 't' && name[7] == 'e' && name[8] == 'r' &&
            name[9] == '_' && name[10])
                script_arm(into, name + 10, line);

        if (script_punct_is(&token, '{'))
                script_block(scan, hook == MOONWATER_HOOK_EVENT ? into : 0, 1);
        else if (script_word_is(&token, "[[")) {
                while (token.kind != SCRIPT_TOK_END && !script_punct_is(&token, ']'))
                        script_next(scan, &token);
                token = script_peek(scan);
                if (script_punct_is(&token, ']'))
                        script_next(scan, &token);
        } else if (script_word_is(&token, "case"))
                script_parse_case(scan, hook == MOONWATER_HOOK_EVENT ? into : 0);
        else
                script_compound(scan, hook == MOONWATER_HOOK_EVENT ? into : 0,
                                script_punct_is(&token, '('));
        return 1;
}

static void moonwater_scan(const char *text, unsigned long length,
                           struct moonwater_overlay *into)
{
        struct script_read scan;
        struct script_token token;
        unsigned char *b = (unsigned char *)into;
        unsigned long n = sizeof(*into);

        while (n--)
                *b++ = 0;
        scan.text = (const unsigned char *)text;
        scan.length = length;
        scan.at = 0;
        scan.line = 1;
        scan.heredoc_tabs = 0;
        scan.heredoc_length = 0;
        scan.heredoc_total = 0;
        scan.rescan = 8 * length;

        while (scan.at < scan.length) {
                script_next(&scan, &token);
                if (token.kind == SCRIPT_TOK_END)
                        break;
                if (script_word_is(&token, "function")) {
                        script_next(&scan, &token);
                        if (token.kind == SCRIPT_TOK_WORD)
                                script_take_function(&scan, token.word, token.line,
                                                     into);
                        continue;
                }
                if (token.kind == SCRIPT_TOK_WORD) {
                        struct script_token next = script_peek(&scan);

                        if (script_punct_is(&next, '('))
                                script_take_function(&scan, token.word, token.line,
                                                     into);
                }
        }
}

#endif /* scan */

#ifdef STANDARD_MODERN_C_KERNEL
#ifdef CONFIG_MOONWATER_CANVAS
static _Bool canvas_is_on(void);
#endif

/*
        One input handler, not grabbing. The callback is IRQ context and only
        debounces and queues; the line runs from system_dfl_long_wq as
        `/shell -c`. A machine-script arm is queued into the attached process
        instead. `moonwater_event`, when present, is queued every event. A
        function or literal case arm owns the row so the image line is not
        also spawned. Events the overlay does not name still use the image line.

        poweroff and reboot must not fail quietly: a line that could not start,
        or returned without stopping, falls back to orderly_poweroff or
        orderly_reboot.
*/
#define BIND_MOD_CTRL 1u
#define BIND_MOD_ALT 2u
#define BIND_MOD_RCTRL 4u
#define BIND_MOD_RALT 8u
#define BIND_BOOT 1u
#define BIND_DROP 2u
#define BIND_CANVAS 4u
#define BIND_CAD 8u
#define BIND_CODES 768u
#define BIND_MACHINE_QUEUE 32
#define BIND_CODES_PER 2

struct bind_row {
        unsigned int event;
        unsigned int debounce;
        unsigned int flags;
        atomic_t bound;
        atomic_t runs;
        atomic_t busy;
        unsigned long last;
        const char *def;
        char command[SPARK_BIND_COMMAND_MAX];
        struct work_struct work;
};

struct bind_spawn {
        char command[SPARK_BIND_COMMAND_MAX];
        char event[SPARK_BIND_NAME_MAX];
};

struct bind_handle {
        struct input_handle handle;
        struct work_struct open;
        int opened;
        unsigned int mods;
};

static struct bind_row bind_table[SPARK_BIND_EVENTS];
static DEFINE_SPINLOCK(bind_lock);
static atomic_t bind_ctrl;
static atomic_t bind_alt;
static unsigned bind_held_n;
static _Bool bind_handler_registered;
static struct work_struct bind_canvas_work;

/*
        Whether this Canvas start has been handed to anyone.

        Canvas starts during the initcalls, before system_state is running,
        so the canvas on it fires at boot is dropped, and the machine process
        that opens a desktop's windows attaches seconds later. Each canvas
        on or off clears it; handing canvas on to the machine process, or
        queueing its image line, sets it; and an attach that finds Canvas on
        and this clear fires canvas on again. A machine process that crashes
        and attaches again after it read the event does not get a second one,
        so it does not open a second set of windows.
*/
static atomic_t bind_canvas_told;
static atomic_t bind_alive;
static atomic_t bind_machine_live;

static struct {
        unsigned int event[BIND_MACHINE_QUEUE];
        unsigned int count;
        struct file *owner;
        struct pid *process; // the thread group that attached it
        wait_queue_head_t wait;
        _Bool ending;
} bind_machine;

static DEFINE_SPINLOCK(bind_machine_lock);

extern const char moonwater_machine_builtin[];
extern const char moonwater_machine_builtin_end[];

/*
        The live script. The builtin one is read where the image keeps it,
        and one set from disk is the copy SET took of it, held at its own
        length: a 64 KiB array for a script of a few kilobytes was that much
        of the kernel's memory from boot whichever one was live.
*/
static const char *machine_script_text;
static char *machine_script_held;
static unsigned int machine_script_length;
static unsigned int machine_script_origin;
static unsigned int machine_script_owned;
static unsigned int machine_script_hooks;
static unsigned int machine_script_builtin;
static struct moonwater_overlay machine_script_overlay;
static DEFINE_MUTEX(machine_script_lock);

static const struct {
        const char *def;
        unsigned short debounce_ms;
        unsigned char flags;
        unsigned short code[BIND_CODES_PER];
} bind_spec[SPARK_BIND_EVENTS] = {
        { "poweroff", 1000, BIND_BOOT | BIND_DROP, { KEY_POWER, 0 } },
        { "", 1000, BIND_BOOT | BIND_DROP, { KEY_SLEEP, KEY_SUSPEND } },
        { "reboot", 1000, BIND_BOOT | BIND_DROP, { KEY_RESTART, 0 } },
        { "reboot", 1000, BIND_BOOT | BIND_DROP | BIND_CAD, { KEY_DELETE, KEY_KPDOT } },
        { "", 500, BIND_DROP, { 0, 0 } },
        { "", 500, BIND_DROP, { 0, 0 } },
        { "", 0, 0, { KEY_VOLUMEUP, 0 } },
        { "", 0, 0, { KEY_VOLUMEDOWN, 0 } },
        { "", 0, 0, { KEY_MUTE, 0 } },
        { "", 0, 0, { KEY_BRIGHTNESSUP, 0 } },
        { "", 0, 0, { KEY_BRIGHTNESSDOWN, 0 } },
        { "", 0, BIND_DROP | BIND_CANVAS, { 0, 0 } },
        { "", 0, BIND_DROP | BIND_CANVAS, { 0, 0 } },
        { "", 0, 0, { KEY_MICMUTE, 0 } },
        { "", 0, 0, { KEY_RFKILL, KEY_WLAN } },
        { "", 500, BIND_DROP, { 0, 0 } },
        { "", 500, BIND_DROP, { 0, 0 } },
        { "", 500, BIND_DROP, { 0, 0 } },
        { "", 500, BIND_DROP, { 0, 0 } },
        { "", 500, BIND_DROP, { 0, 0 } },
        { "", 500, BIND_DROP, { 0, 0 } },
        { "", 0, BIND_DROP, { 0, 0 } },
};

_Static_assert(sizeof(bind_spec) / sizeof(bind_spec[0]) == SPARK_BIND_EVENTS,
               "bind spec");

static const unsigned short bind_mod_code[] = {
        KEY_LEFTCTRL, KEY_RIGHTCTRL, KEY_LEFTALT, KEY_RIGHTALT,
};

static const unsigned char bind_mod_of[256] = {
        [KEY_LEFTCTRL] = BIND_MOD_CTRL,
        [KEY_RIGHTCTRL] = BIND_MOD_RCTRL,
        [KEY_LEFTALT] = BIND_MOD_ALT,
        [KEY_RIGHTALT] = BIND_MOD_RALT,
};

static const struct {
        unsigned short code;
        unsigned char event[2];
} bind_sw[] = {
        { SW_LID, { SPARK_BIND_LID_OPEN, SPARK_BIND_LID_CLOSE } },
        { SW_TABLET_MODE, { SPARK_BIND_TABLET_OFF, SPARK_BIND_TABLET_ON } },
        { SW_HEADPHONE_INSERT, { SPARK_BIND_HEADPHONE_OFF, SPARK_BIND_HEADPHONE_ON } },
        { SW_DOCK, { SPARK_BIND_DOCK_OFF, SPARK_BIND_DOCK_ON } },
};

static const unsigned char bind_machine_admin[MOONWATER_END + 1] = {
        [MOONWATER_ATTACH] = 1,
        [MOONWATER_WAIT] = 1,
        [MOONWATER_END] = 1,
};

static struct bind_row *bind_row(unsigned int event)
{
        if (!event || event > SPARK_BIND_EVENTS)
                return NULL;
        return bind_table + event - 1;
}

static void bind_map_set(unsigned long *map, unsigned int code, _Bool on)
{
        unsigned int word;
        unsigned long bit, now;

        if (code >= BIND_CODES)
                return;
        word = code / 64;
        bit = 1UL << (code % 64);
        now = READ_ONCE(map[word]);
        WRITE_ONCE(map[word], on ? now | bit : now & ~bit);
}

static unsigned long bind_down[BIND_CODES / 64];
static u8 bind_code_event[BIND_CODES];

static _Bool bind_map_on(const unsigned long *map, unsigned int code)
{
        return code < BIND_CODES &&
               (READ_ONCE(map[code / 64]) & (1UL << (code % 64))) != 0;
}

static _Bool bind_watched(unsigned int code)
{
        return code < BIND_CODES && READ_ONCE(bind_code_event[code]) != 0;
}

static void bind_watch_code(unsigned int code, unsigned int event, _Bool on)
{
        if (!code || code >= BIND_CODES)
                return;
        WRITE_ONCE(bind_code_event[code], on ? (u8)event : 0);
}

/*
        A row somebody hears: one that has a line, or every row while a machine
        process is attached, because the process's script may name any of them.
        Read where the watch is written, under bind_lock, so a detach that
        lands after another attach reads that attach's state and not its own.
*/
static _Bool bind_row_bound(struct bind_row *row)
{
        return row && (atomic_read(&bind_machine_live) || atomic_read(&row->bound));
}

static void bind_watch_row(struct bind_row *row)
{
        unsigned int i, event = row->event;
        _Bool on = bind_row_bound(row);

        for (i = 0; i < BIND_CODES_PER; i++)
                bind_watch_code(bind_spec[event - 1].code[i], event, on);
}

static int bind_spawn_enter(void *data)
{
        struct bind_spawn *spawn = data;
        char command[SPARK_BIND_COMMAND_MAX];
        char event_env[sizeof("MOONWATER_EVENT=") + SPARK_BIND_NAME_MAX];
        char *argv[] = { SPARK_TOOL_PROGRAM, "-c", command, NULL };
        char *envp[] = { SPARK_COMMAND_ENVIRONMENT(SPARK_ENVIRONMENT_ENTRY)
                         event_env, NULL };
        int ret;

        strscpy(command, spawn->command, sizeof(command));
        snprintf(event_env, sizeof(event_env), "MOONWATER_EVENT=%s", spawn->event);
        ret = kernel_execve(argv[0], (const char *const *)argv,
                            (const char *const *)envp);
        if (ret)
                do_exit(ret);
        return 0;
}

/*
        How a command that did not answer 0 ended, said as the shell says it.
        ret is a wait status, or an error number when the command never got
        to run or be waited for; the number is the exit code, the signal or
        the error, whichever the words are about.
*/
static const char *bind_ending(int ret, int *number)
{
        if (ret < 0) {
                *number = -ret;
                return "could not be run, error";
        }
        if (ret & 0x7f) {
                *number = ret & 0x7f;
                return "was killed by signal";
        }
        *number = (ret >> 8) & 0xff;
        return "exited";
}

static void bind_run(struct bind_row *row)
{
        struct bind_spawn spawn;
        const char *name = spark_bind_event_name[row->event - 1];
        const char *how;
        _Bool poweroff, reboot;
        pid_t pid;
        int ret = 0, stat = 0, number, shown;
        unsigned long flags;

        spin_lock_irqsave(&bind_lock, flags);
        strscpy(spawn.command, row->command, sizeof(spawn.command));
        spin_unlock_irqrestore(&bind_lock, flags);
        strscpy(spawn.event, name, sizeof(spawn.event));
        atomic_set(&row->busy, 1);

        if (!spawn.command[0]) {
                pr_info("[moonwater] %s: ignored\n", name);
                goto done;
        }

        poweroff = !strcmp(spawn.command, "poweroff");
        reboot = !strcmp(spawn.command, "reboot");
        //      %*pE: the line is somebody's text, and a log is not a terminal.
        shown = (int)strlen(spawn.command);
        pr_info("[moonwater] %s: %*pE\n", name, shown, spawn.command);

        kernel_sigaction(SIGCHLD, SIG_DFL);
        pid = user_mode_thread(bind_spawn_enter, &spawn, SIGCHLD);
        if (pid > 0) {
                ret = kernel_wait(pid, &stat);
                if (ret > 0)
                        ret = stat;
        } else
                ret = pid ? pid : -EAGAIN;
        kernel_sigaction(SIGCHLD, SIG_IGN);

        if (!ret)
                goto done;
        how = bind_ending(ret, &number);
        if (!poweroff && !reboot) {
                pr_warn("[moonwater] %s: %*pE %s %d\n", name, shown, spawn.command,
                        how, number);
                goto done;
        }

        pr_warn("[moonwater] %s: %*pE %s %d, stopping the machine anyway\n",
                name, shown, spawn.command, how, number);
        if (reboot)
                orderly_reboot();
        else
                orderly_poweroff(true);
done:
        atomic_set(&row->busy, 0);
}

static void bind_work(struct work_struct *work)
{
        bind_run(container_of(work, struct bind_row, work));
}

static void bind_canvas_run(struct work_struct *work)
{
        struct bind_row *on = bind_row(SPARK_BIND_CANVAS_ON);
        struct bind_row *off = bind_row(SPARK_BIND_CANVAS_OFF);
        _Bool running = false;

        (void)work;
#ifdef CONFIG_MOONWATER_CANVAS
        running = canvas_is_on();
#endif
        bind_run(running ? on : off);
        atomic_set(&on->busy, 0);
        atomic_set(&off->busy, 0);
}

static void bind_machine_shift(unsigned int at)
{
        unsigned int n = --bind_machine.count - at;

        if (n)
                memmove(bind_machine.event + at, bind_machine.event + at + 1,
                        n * sizeof(bind_machine.event[0]));
}

static void bind_machine_watch_all(void)
{
        unsigned at;
        unsigned long flags;

        spin_lock_irqsave(&bind_lock, flags);
        for (at = 0; at < SPARK_BIND_EVENTS; at++)
                bind_watch_row(bind_table + at);
        spin_unlock_irqrestore(&bind_lock, flags);
}

/*
        Hand one press to the machine process, and say whether it took it.

        The answer is about this event, not about the process: bind_queue has
        only two copies of what a row means -- the function in the machine
        script and the image line -- and it gives the press to the second
        whenever the first did not get it. Anything else is a press that goes
        nowhere, and the rows that come through here own the power button.

        Three ways it does not get it. A process that has been told the
        machine is stopping will not come back for the queue, so the wait
        answers the end and everything behind it is never read. A queue with
        no room left and nothing in it that may be dropped cannot hold one
        more. And a stop event still sitting in the queue was not taken: the
        debounce on those rows is a second, so a second press means the
        process has not read the first in all that time, and the image line is
        the only other way to stop this machine.
*/
static _Bool bind_machine_push(struct bind_row *row)
{
        unsigned long flags;
        unsigned int event = row->event, i;

        spin_lock_irqsave(&bind_machine_lock, flags);
        if (!bind_machine.owner || bind_machine.ending) {
                spin_unlock_irqrestore(&bind_machine_lock, flags);
                return false;
        }

        if (spark_bind_is_stop(event)) {
                for (i = 0; i < bind_machine.count; i++)
                        if (bind_machine.event[i] == event) {
                                spin_unlock_irqrestore(&bind_machine_lock, flags);
                                return false;
                        }
        }

        if (moonwater_paired(event)) {
                for (i = 0; i < bind_machine.count; i++)
                        if (moonwater_same_pair(event, bind_machine.event[i])) {
                                bind_machine.event[i] = event;
                                atomic_fetch_add(1, &row->runs);
                                spin_unlock_irqrestore(&bind_machine_lock, flags);
                                wake_up(&bind_machine.wait);
                                return true;
                        }
        }

        if (bind_machine.count == BIND_MACHINE_QUEUE) {
                for (i = 0; i < bind_machine.count; i++)
                        if (!spark_bind_is_stop(bind_machine.event[i])) {
                                bind_machine_shift(i);
                                break;
                        }
        }
        if (bind_machine.count == BIND_MACHINE_QUEUE) {
                spin_unlock_irqrestore(&bind_machine_lock, flags);
                return false;
        }

        bind_machine.event[bind_machine.count++] = event;
        atomic_fetch_add(1, &row->runs);
        spin_unlock_irqrestore(&bind_machine_lock, flags);
        wake_up(&bind_machine.wait);
        return true;
}

static void bind_queue(struct bind_row *row)
{
        struct work_struct *work;
        unsigned int owned;

        if (system_state != SYSTEM_RUNNING || !atomic_read(&bind_alive))
                return;

        //      Owned and taken is the machine process's press and nobody
        //      else's. Owned and not taken falls through to the image line
        //      below, because a row the script owns has no third copy and
        //      dropping it here is the button that does nothing.
        owned = READ_ONCE(machine_script_owned) & (1u << (row->event - 1));
        if (atomic_read(&bind_machine_live) &&
            ((READ_ONCE(machine_script_hooks) & MOONWATER_HOOK_EVENT) || owned) &&
            bind_machine_push(row) && owned)
                return;

        if (!(row->flags & BIND_CANVAS) && !row->command[0])
                return;

        if (row->flags & BIND_CANVAS) {
                struct bind_row *on = bind_row(SPARK_BIND_CANVAS_ON);
                struct bind_row *off = bind_row(SPARK_BIND_CANVAS_OFF);

                if (!on->command[0] && !off->command[0])
                        return;
                if (atomic_read(&on->busy) || atomic_read(&off->busy) ||
                    work_pending(&bind_canvas_work))
                        return;
                atomic_set(&on->busy, 1);
                atomic_set(&off->busy, 1);
                if (row->event == SPARK_BIND_CANVAS_ON)
                        atomic_set(&bind_canvas_told, 1);
                work = &bind_canvas_work;
        } else {
                if ((row->flags & BIND_DROP) &&
                    (atomic_read(&row->busy) || work_pending(&row->work)))
                        return;
                work = &row->work;
        }

        if (queue_work(system_dfl_long_wq, work))
                atomic_fetch_add(1, &row->runs);
}

static void bind_fire(unsigned int event)
{
        struct bind_row *row = bind_row(event);

        if (event == SPARK_BIND_CANVAS_ON || event == SPARK_BIND_CANVAS_OFF)
                atomic_set(&bind_canvas_told, 0);

        if (bind_row_bound(row))
                bind_queue(row);
}

/*
        Nobody holds the machine: no owner, nothing queued, no ending in
        progress. With bind_machine_lock held; the process it returns is the
        caller's to put once the lock is let go.
*/
static struct pid *bind_machine_reset(void)
{
        struct pid *process = bind_machine.process;

        bind_machine.owner = NULL;
        bind_machine.process = NULL;
        bind_machine.ending = false;
        bind_machine.count = 0;
        atomic_set(&bind_machine_live, 0);
        return process;
}

/*
        Let go of the machine, if file holds it -- and, when process is named,
        only if that thread group is the one that attached, which is how a
        flush from a process that is not the machine process's own is told
        apart from the real thing.
*/
static void bind_machine_detach(struct file *file, struct pid *process)
{
        unsigned long flags;
        unsigned int drain[BIND_MACHINE_QUEUE], n = 0, i;
        struct pid *gone = NULL;
        _Bool was_owner = false;

        spin_lock_irqsave(&bind_machine_lock, flags);
        if (bind_machine.owner == file &&
            (!process || bind_machine.process == process)) {
                was_owner = true;
                if (!bind_machine.ending)
                        for (i = 0; i < bind_machine.count; i++)
                                drain[n++] = bind_machine.event[i];
                gone = bind_machine_reset();
        }
        spin_unlock_irqrestore(&bind_machine_lock, flags);

        if (!was_owner)
                return;

        put_pid(gone);
        bind_machine_watch_all();
        wake_up(&bind_machine.wait);
        for (i = 0; i < n; i++) {
                struct bind_row *row = bind_row(drain[i]);

                if (row)
                        bind_queue(row);
        }
}

/*
        Called when a descriptor of the device is closed, by whoever closes it:
        every spawn device a shell opens comes through here, so the common
        answer costs one load.

        The machine process's attach is its open file, and an open file is let
        go of only when the last reference to it is. A process that forks and
        does not exec -- a background function in the script, a subshell, the
        wifi and time children -- hands each child a copy of the descriptor,
        so killing the machine process left the file open for as long as any
        of them lived. The owner stayed set, the next machine process was
        answered EBUSY, and the rows it had claimed went to a queue nobody
        read. The machine process dying is what ends its attach, and it ends
        it here, as its descriptors are closed on the way out: not a close by
        anyone else, and not a close by the machine process itself of a copy
        it made, which is its own business and does not end anything.
*/
static void bind_machine_flush(struct file *file)
{
        if (likely(READ_ONCE(bind_machine.owner) != file) ||
            !(current->flags & PF_EXITING))
                return;
        bind_machine_detach(file, task_tgid(current));
}

static void bind_machine_fill(struct machine_control *request, unsigned int event)
{
        const struct moonwater_pair *pair = moonwater_paired(event);

        request->event = event;
        request->extra = pair && event == pair->on;
        request->flags = MOONWATER_ATTACHED;
        request->queued = bind_machine.count;
        if (event && event <= SPARK_BIND_EVENTS)
                strscpy(request->name, spark_bind_event_name[event - 1],
                        sizeof(request->name));
        else
                strscpy(request->name, "end", sizeof(request->name));
}

static long bind_machine_wait(struct file *file, struct machine_control *request)
{
        unsigned long flags;
        unsigned int event;
        unsigned int wait_ms = request->reserved[0];
        long left;

        if (wait_ms > 60000)
                wait_ms = 60000;

        for (;;) {
                spin_lock_irqsave(&bind_machine_lock, flags);
                if (bind_machine.owner != file) {
                        spin_unlock_irqrestore(&bind_machine_lock, flags);
                        return -EPIPE;
                }
                if (bind_machine.ending) {
                        bind_machine_fill(request, 0);
                        spin_unlock_irqrestore(&bind_machine_lock, flags);
                        return 0;
                }
                if (bind_machine.count) {
                        event = bind_machine.event[0];
                        bind_machine_shift(0);
                        if (event == SPARK_BIND_CANVAS_ON)
                                atomic_set(&bind_canvas_told, 1);
                        bind_machine_fill(request, event);
                        spin_unlock_irqrestore(&bind_machine_lock, flags);
                        return 0;
                }
                spin_unlock_irqrestore(&bind_machine_lock, flags);
                if (wait_ms) {
                        left = wait_event_interruptible_timeout(
                                bind_machine.wait,
                                READ_ONCE(bind_machine.count) ||
                                    READ_ONCE(bind_machine.ending) ||
                                    READ_ONCE(bind_machine.owner) != file,
                                msecs_to_jiffies(wait_ms));
                        if (left < 0)
                                return -EINTR;
                        if (!left)
                                return -ETIMEDOUT;
                        continue;
                }
                if (wait_event_interruptible(
                            bind_machine.wait,
                            READ_ONCE(bind_machine.count) ||
                                READ_ONCE(bind_machine.ending) ||
                                READ_ONCE(bind_machine.owner) != file))
                        return -EINTR;
        }
}

/*
        text becomes the live script: the builtin one, or a copy SET took,
        which the script now owns and frees when it is replaced.
*/
static void machine_script_commit(const char *text, unsigned int length,
                                  unsigned int origin, char *held)
{
        unsigned int bits = 0, event;

        if (length > MOONWATER_SCRIPT_BYTES)
                length = MOONWATER_SCRIPT_BYTES;
        kfree(machine_script_held);
        machine_script_held = held;
        machine_script_text = text;
        machine_script_length = length;
        machine_script_origin = origin;
        moonwater_scan(machine_script_text, length, &machine_script_overlay);
        for (event = 0; event < SPARK_BIND_EVENTS; event++)
                if (machine_script_overlay.bind_line[event])
                        bits |= 1u << event;
        WRITE_ONCE(machine_script_owned, bits);
        WRITE_ONCE(machine_script_hooks, machine_script_overlay.hooks);
}

static void machine_script_reset(void)
{
        machine_script_builtin = (unsigned int)(moonwater_machine_builtin_end -
                                                moonwater_machine_builtin);
        if (machine_script_builtin > MOONWATER_SCRIPT_BYTES) {
                pr_warn("[moonwater] builtin machine script is %u bytes; using the first %u\n",
                        machine_script_builtin, MOONWATER_SCRIPT_BYTES);
                machine_script_builtin = MOONWATER_SCRIPT_BYTES;
        }
        mutex_lock(&machine_script_lock);
        machine_script_commit(moonwater_machine_builtin, machine_script_builtin,
                              MOONWATER_ORIGIN_BUILTIN, NULL);
        mutex_unlock(&machine_script_lock);
}

static void machine_script_answer(struct machine_script *request)
{
        request->origin = machine_script_origin;
        request->length = machine_script_length;
        request->flags = 0;
        request->overlay = machine_script_overlay;
}

static long report_machine(struct file *file, struct machine_control __user *out)
{
        struct machine_control request;
        unsigned long flags;
        long ret = 0;

        if (copy_from_user(&request, out, sizeof(request)))
                return -EFAULT;
        if (request.reserved[1] || request.reserved[2])
                return -EINVAL;
        if (request.op != MOONWATER_WAIT && request.reserved[0])
                return -EINVAL;
        if (request.op > MOONWATER_END)
                return -EINVAL;
        if (bind_machine_admin[request.op] && !capable(CAP_SYS_ADMIN))
                return -EPERM;

        switch (request.op) {
        case MOONWATER_ATTACH: {
                struct pid *process = get_pid(task_tgid(current));
                struct pid *before;

                spin_lock_irqsave(&bind_machine_lock, flags);
                if (bind_machine.owner && bind_machine.owner != file) {
                        spin_unlock_irqrestore(&bind_machine_lock, flags);
                        put_pid(process);
                        return -EBUSY;
                }
                before = bind_machine.process;
                bind_machine.process = process;
                bind_machine.owner = file;
                bind_machine.ending = false;
                request.queued = bind_machine.count;
                atomic_set(&bind_machine_live, 1);
                spin_unlock_irqrestore(&bind_machine_lock, flags);
                put_pid(before);
                bind_machine_watch_all();
#ifdef CONFIG_MOONWATER_CANVAS
                if (!atomic_read(&bind_canvas_told) && canvas_is_on())
                        bind_fire(SPARK_BIND_CANVAS_ON);
#endif
                request.flags = MOONWATER_ATTACHED;
                break;
        }
        case MOONWATER_DETACH:
                bind_machine_detach(file, NULL);
                request.flags = 0;
                request.queued = 0;
                break;
        case MOONWATER_WAIT:
                ret = bind_machine_wait(file, &request);
                if (ret)
                        return ret;
                break;
        case MOONWATER_STATUS:
                spin_lock_irqsave(&bind_machine_lock, flags);
                request.queued = bind_machine.count;
                request.flags = bind_machine.owner ? MOONWATER_ATTACHED : 0;
                request.event = 0;
                spin_unlock_irqrestore(&bind_machine_lock, flags);
                break;
        case MOONWATER_END:
                spin_lock_irqsave(&bind_machine_lock, flags);
                if (!bind_machine.owner) {
                        spin_unlock_irqrestore(&bind_machine_lock, flags);
                        return -EPIPE;
                }
                bind_machine.ending = true;
                spin_unlock_irqrestore(&bind_machine_lock, flags);
                wake_up(&bind_machine.wait);
                request.flags = MOONWATER_ATTACHED;
                break;
        }

        return copy_to_user(out, &request, sizeof(request)) ? -EFAULT : 0;
}

static long report_machine_script(struct machine_script __user *out)
{
        struct machine_script request;

        if (copy_from_user(&request, out, sizeof(request)))
                return -EFAULT;
        if (request.flags)
                return -EINVAL;

        switch (request.op) {
        case MOONWATER_SCRIPT_GET: {
                /* Taken before the answer overwrites the field: on the way
                   in it is the caller's room, on the way out it is the
                   script's length, and the two share one word. */
                unsigned int room = request.length;
                unsigned int length;
                char *copy = NULL;

                /*      The text is root's. It is read from
                        /root/main.moonwater.sh, which is where a person who
                        cannot open that file was kept from it, and it can
                        carry what an operator would not print: a network's
                        password, a token in a command. /dev/spark is open to
                        everyone, so the copy needs the capability the
                        settings' GET needs. What anyone may still ask for is
                        the overlay, the length and the origin -- the answer
                        moonwater status is made from -- which is the request
                        that names no address. */
                if (request.address && !capable(CAP_SYS_ADMIN))
                        return -EPERM;

                mutex_lock(&machine_script_lock);
                length = machine_script_length;
                if (request.address && length > room) {
                        machine_script_answer(&request);
                        mutex_unlock(&machine_script_lock);
                        return copy_to_user(out, &request, sizeof(request))
                                       ? -EFAULT
                                       : -ENOSPC;
                }
                machine_script_answer(&request);

                /*      Taken out of the lock before it goes to the caller. A
                        page the caller mapped from a file on a filesystem
                        that answers slowly, or never, would otherwise hold
                        this lock for as long -- and SET, the machine
                        process re-reading its own script, and module exit
                        all wait for it. */
                if (request.address && length) {
                        copy = kvmalloc(length, GFP_KERNEL);
                        if (!copy) {
                                mutex_unlock(&machine_script_lock);
                                return -ENOMEM;
                        }
                        memcpy(copy, machine_script_text, length);
                }
                mutex_unlock(&machine_script_lock);

                if (copy) {
                        unsigned long failed = copy_to_user(
                                (void __user *)request.address, copy, length);

                        kvfree(copy);
                        if (failed)
                                return -EFAULT;
                }
                break;
        }
        case MOONWATER_SCRIPT_SET: {
                /*      The whole new script is taken before the live one is
                        touched. copy_from_user may copy a prefix and then
                        fault, and copying straight into the live text leaves
                        the front of the new script spliced onto the tail of
                        the old one -- a script nobody wrote, with a length
                        and an overlay that still describe the old one, and
                        the refusal says nothing about it. Taking it whole
                        first means a refused SET changes nothing at all. */
                char *fresh = NULL;

                if (!capable(CAP_SYS_ADMIN))
                        return -EPERM;
                if (request.length > MOONWATER_SCRIPT_BYTES)
                        return -EINVAL;
                if (request.length && !request.address)
                        return -EINVAL;
                if (request.length) {
                        fresh = memdup_user((void __user *)request.address,
                                            request.length);
                        if (IS_ERR(fresh))
                                return PTR_ERR(fresh);
                }
                mutex_lock(&machine_script_lock);
                if (fresh)
                        machine_script_commit(fresh, request.length,
                                              MOONWATER_ORIGIN_DISK, fresh);
                else
                        machine_script_commit(moonwater_machine_builtin,
                                              machine_script_builtin,
                                              MOONWATER_ORIGIN_BUILTIN, NULL);
                machine_script_answer(&request);
                mutex_unlock(&machine_script_lock);
                break;
        }
        default:
                return -EINVAL;
        }

        return copy_to_user(out, &request, sizeof(request)) ? -EFAULT : 0;
}

static _Bool bind_debounce(struct bind_row *row)
{
        unsigned long now = jiffies | 1;
        unsigned long last = READ_ONCE(row->last);

        if (!row->debounce)
                return true;
        if (last && time_before(now, last + row->debounce))
                return false;
        return cmpxchg(&row->last, last, now) == last;
}

static struct bind_row *bind_match(unsigned int type, unsigned int code, int value)
{
        unsigned int event;

        if (type == EV_SW) {
                unsigned int i;

                if ((unsigned int)value > 1)
                        return NULL;
                for (i = 0; i < ARRAY_SIZE(bind_sw); i++)
                        if (bind_sw[i].code == code)
                                return bind_row(bind_sw[i].event[value]);
                return NULL;
        }
        if (type != EV_KEY || value != 1 || code >= BIND_CODES)
                return NULL;

        event = READ_ONCE(bind_code_event[code]);
        if (likely(!event))
                return NULL;
        if ((bind_spec[event - 1].flags & BIND_CAD) &&
            !(atomic_read(&bind_ctrl) > 0 && atomic_read(&bind_alt) > 0))
                return NULL;
        return bind_row(event);
}

static void bind_hold(unsigned int code, int value)
{
        _Bool down = value != 0;

        if (code >= BIND_CODES || bind_map_on(bind_down, code) == down)
                return;
        bind_map_set(bind_down, code, down);
        bind_held_n += down ? 1 : -1;
}

static _Bool bind_key_swallowed(unsigned int code, int value)
{
        struct bind_row *row;
        unsigned long flags;
        _Bool swallow = false;

        if (likely(!bind_watched(code) && (value == 1 || !READ_ONCE(bind_held_n))))
                return false;

        spin_lock_irqsave(&bind_lock, flags);
        if (value == 1) {
                row = bind_match(EV_KEY, code, 1);
                if (row) {
                        bind_hold(code, 1);
                        swallow = true;
                }
        } else if (bind_map_on(bind_down, code)) {
                if (!value)
                        bind_hold(code, 0);
                swallow = true;
        }
        spin_unlock_irqrestore(&bind_lock, flags);
        return swallow;
}

static void bind_mods(struct bind_handle *bind, unsigned int code, int value)
{
        unsigned int bit = code < 256 ? bind_mod_of[code] : 0;
        atomic_t *side;

        if (!bit)
                return;
        side = (bit & (BIND_MOD_CTRL | BIND_MOD_RCTRL)) ? &bind_ctrl : &bind_alt;
        if (value == 1) {
                if (!(bind->mods & bit)) {
                        bind->mods |= bit;
                        atomic_fetch_add(1, side);
                }
        } else if (value == 0 && (bind->mods & bit)) {
                bind->mods &= ~bit;
                atomic_fetch_sub(1, side);
        }
}

static void bind_event(struct input_handle *handle, unsigned int type,
                       unsigned int code, int value)
{
        struct bind_handle *bind = container_of(handle, struct bind_handle, handle);
        struct bind_row *row;

        // A mouse is a device with keys, so this hears every count it moves by
        // and every report that ends one; most of what arrives is neither.
        if (likely(type != EV_KEY && type != EV_SW))
                return;

        if (type == EV_KEY) {
                bind_mods(bind, code, value);
                if (value != 1)
                        return;
        }

        row = bind_match(type, code, value);
        if (!row || (type == EV_SW && !bind_row_bound(row)) || !bind_debounce(row))
                return;
        bind_queue(row);
}

/*
        Opened from a worker, not from connect.

        A connect runs inside the probe of the device being registered, with
        the input core's mutex held, and a USB HID device's first open sleeps
        fifty milliseconds in usbhid_open. This handler takes anything with a
        key, which includes a mouse's buttons, so opening here put that sleep
        in front of the next device on the hub: under QEMU the keyboard
        enumerated fifty milliseconds after the tablet for no reason but this.
        A device that will not open is left registered and deaf, as a refused
        connect left it unbound; either way no binding hears it.
*/
static void bind_open(struct work_struct *work)
{
        struct bind_handle *bind = container_of(work, struct bind_handle, open);

        bind->opened = input_open_device(&bind->handle);
}

static int bind_connect(struct input_handler *handler, struct input_dev *dev,
                        const struct input_device_id *id)
{
        struct bind_handle *bind = kzalloc(sizeof(*bind), GFP_KERNEL);
        int ret;

        (void)id;
        if (!bind)
                return -ENOMEM;
        bind->handle.dev = dev;
        bind->handle.handler = handler;
        bind->handle.name = "moonwater-bind";
        bind->opened = -EINPROGRESS;
        INIT_WORK(&bind->open, bind_open);
        ret = input_register_handle(&bind->handle);
        if (ret)
        {
                kfree(bind);
                return ret;
        }
        schedule_work(&bind->open);
        return 0;
}

static void bind_disconnect(struct input_handle *handle)
{
        struct bind_handle *bind = container_of(handle, struct bind_handle, handle);
        unsigned int i;

        cancel_work_sync(&bind->open);
        for (i = 0; i < ARRAY_SIZE(bind_mod_code); i++)
                bind_mods(bind, bind_mod_code[i], 0);
        if (!bind->opened)
                input_close_device(handle);
        input_unregister_handle(handle);
        kfree(bind);
}

static const struct input_device_id bind_ids[] = {
        { .flags = INPUT_DEVICE_ID_MATCH_EVBIT, .evbit = { BIT_MASK(EV_KEY) } },
        { .flags = INPUT_DEVICE_ID_MATCH_EVBIT, .evbit = { BIT_MASK(EV_SW) } },
        {},
};

static struct input_handler bind_handler = {
        .event = bind_event,
        .connect = bind_connect,
        .disconnect = bind_disconnect,
        .name = "moonwater-bind",
        .id_table = bind_ids,
};

#ifdef CONFIG_VT
static int bind_keyboard_notify(struct notifier_block *nb, unsigned long code,
                                void *p)
{
        struct keyboard_notifier_param *param = p;
        struct bind_row *row;

        (void)nb;
        if (code != KBD_KEYSYM || !param->down)
                return NOTIFY_DONE;
        if ((KTYP(param->value) & 0x0f) != KT_SPEC ||
            KVAL(param->value) != KVAL(K_BOOT))
                return NOTIFY_DONE;

        row = bind_row(SPARK_BIND_CTRL_ALT_DELETE);
        if (bind_row_bound(row) && bind_debounce(row))
                bind_queue(row);
        return NOTIFY_STOP;
}

static struct notifier_block bind_kbd_nb = { .notifier_call = bind_keyboard_notify };
#endif

#ifdef CONFIG_PM
static int bind_pm_notify(struct notifier_block *nb, unsigned long event, void *p)
{
        unsigned at;

        (void)nb;
        (void)p;
        if (event != PM_POST_SUSPEND)
                return NOTIFY_DONE;
        bind_fire(SPARK_BIND_RESUME);
        for (at = 0; at < SPARK_BIND_EVENTS; at++)
                if (bind_table[at].flags & BIND_DROP)
                        WRITE_ONCE(bind_table[at].last, jiffies | 1);
        return NOTIFY_OK;
}

static struct notifier_block bind_pm_nb = { .notifier_call = bind_pm_notify };
#endif

static void bind_start(void)
{
        unsigned at;

        INIT_WORK(&bind_canvas_work, bind_canvas_run);
        init_waitqueue_head(&bind_machine.wait);
        machine_script_reset();
        spin_lock_irq(&bind_machine_lock);
        bind_machine_reset();
        spin_unlock_irq(&bind_machine_lock);
        atomic_set(&bind_ctrl, 0);
        atomic_set(&bind_alt, 0);
        bind_held_n = 0;
        memset(bind_down, 0, sizeof(bind_down));
        memset(bind_code_event, 0, sizeof(bind_code_event));

        for (at = 0; at < SPARK_BIND_EVENTS; at++) {
                struct bind_row *row = bind_table + at;

                row->def = bind_spec[at].def;
                row->event = at + 1;
                row->flags = bind_spec[at].flags;
                row->debounce = (unsigned int)msecs_to_jiffies(
                        bind_spec[at].debounce_ms);
                strscpy(row->command, row->def, sizeof(row->command));
                atomic_set(&row->bound, row->def[0] != 0);
                atomic_set(&row->runs, 0);
                atomic_set(&row->busy, 0);
                row->last = 0;
                INIT_WORK(&row->work, bind_work);
                bind_watch_row(row);
        }
        atomic_set(&bind_alive, 1);
        if (input_register_handler(&bind_handler))
                pr_alert("[moonwater] could not watch the machine's keys\n");
        else
                bind_handler_registered = true;
#ifdef CONFIG_VT
        register_keyboard_notifier(&bind_kbd_nb);
#endif
#ifdef CONFIG_PM
        register_pm_notifier(&bind_pm_nb);
#endif
}

static void bind_stop(void)
{
        struct pid *gone;
        unsigned at;

        atomic_set(&bind_alive, 0);
        spin_lock_irq(&bind_machine_lock);
        gone = bind_machine_reset();
        spin_unlock_irq(&bind_machine_lock);
        put_pid(gone);
        wake_up_all(&bind_machine.wait);

#ifdef CONFIG_VT
        unregister_keyboard_notifier(&bind_kbd_nb);
#endif
#ifdef CONFIG_PM
        unregister_pm_notifier(&bind_pm_nb);
#endif
        if (bind_handler_registered)
                input_unregister_handler(&bind_handler);

        cancel_work_sync(&bind_canvas_work);
        for (at = 0; at < SPARK_BIND_EVENTS; at++)
                cancel_work_sync(&bind_table[at].work);

        mutex_lock(&machine_script_lock);
        kfree(machine_script_held);
        machine_script_held = NULL;
        machine_script_text = NULL;
        machine_script_length = 0;
        mutex_unlock(&machine_script_lock);
}

/*
        What a row says, to whoever asked.

        The line a row runs is run as root with every capability, and the
        settings' own GET is CAP_SYS_ADMIN "because a command can carry a
        secret": a bound line is the same text. So a caller without the
        capability sees the event, its name, how often it has run and what
        state it is in, and the line only while it is the event's default --
        which is public, it is in this file. A line somebody set reads as
        empty to them, with the DEFAULT flag clear, so that they can still
        tell that it was changed.
*/
static void bind_answer(struct bind_control *request, struct bind_row *row,
                        _Bool reveal)
{
        unsigned long flags;

        spin_lock_irqsave(&bind_lock, flags);
        strscpy(request->name, spark_bind_event_name[row->event - 1],
                sizeof(request->name));
        strscpy(request->command, row->command, sizeof(request->command));
        spin_unlock_irqrestore(&bind_lock, flags);

        request->runs = (unsigned int)atomic_read(&row->runs);
        request->count = SPARK_BIND_EVENTS;
        request->flags = 0;
        if (!strcmp(request->command, row->def))
                request->flags |= SPARK_BIND_DEFAULT;
        else if (!reveal)
                request->command[0] = 0;
        if (atomic_read(&row->busy))
                request->flags |= SPARK_BIND_RUNNING;
        if (work_pending(&row->work) ||
            ((row->flags & BIND_CANVAS) && work_pending(&bind_canvas_work)))
                request->flags |= SPARK_BIND_PENDING;
        if (row->flags & BIND_BOOT)
                request->flags |= SPARK_BIND_BOOT;
}

static long report_bind(struct bind_control __user *out)
{
        struct bind_control request;
        struct bind_row *row;
        unsigned long flags;

        if (copy_from_user(&request, out, sizeof(request)))
                return -EFAULT;
        if (request.op > SPARK_BIND_SET || request.reserved[0] ||
            request.reserved[1] || request.reserved[2])
                return -EINVAL;

        row = bind_row(request.event);
        if (!row)
                return -EINVAL;

        if (request.op == SPARK_BIND_SET) {
                if (!capable(CAP_SYS_ADMIN) ||
                    ((row->flags & BIND_BOOT) && !capable(CAP_SYS_BOOT))) {
                        /* CAP_SYS_BOOT alone is not the capability that
                           reads a line, and the answer to a refusal is a
                           read like any other. */
                        bind_answer(&request, row, capable(CAP_SYS_ADMIN));
                        return copy_to_user(out, &request, sizeof(request))
                                       ? -EFAULT
                                       : -EPERM;
                }
                if (!memchr(request.command, 0, sizeof(request.command)))
                        return -ENAMETOOLONG;

                spin_lock_irqsave(&bind_lock, flags);
                strscpy(row->command,
                        request.command[0] ? request.command : row->def,
                        sizeof(row->command));
                atomic_set(&row->bound, row->command[0] != 0);
                bind_watch_row(row);
                spin_unlock_irqrestore(&bind_lock, flags);
        }

        bind_answer(&request, row, capable(CAP_SYS_ADMIN));
        return copy_to_user(out, &request, sizeof(request)) ? -EFAULT : 0;
}

#endif /* STANDARD_MODERN_C_KERNEL */

#endif /* MOONWATER_INCLUDED */

#ifdef MOONWATER_CLI
#define HOST_MACHINE_OK 0
#define HOST_MACHINE_ABSENT 1
#define HOST_MACHINE_REFUSED 2
#define HOST_REBOOT_RESTART 0x01234567u
#define HOST_REBOOT_POWER_OFF 0x4321fedcu

extern string_address address_to shell_argv;
extern positive shell_argc;

COLD fn shell_dot(writer write, string_address input);
bool exec_function_readonly_set(string_address name);
bool exec_function_readonly_clear(string_address name);
positive shell_function_slot(string_address name);
b32 shell_call_slot(positive slot, string_address name,
                    string_address address_to arguments, positive count);
fn shell_stop(writer write, positive command);
bool env_assign(const_string name, const_string value);

static struct machine_script host_machine;
static p8 host_machine_text[MOONWATER_SCRIPT_BYTES];
static p8 host_machine_fresh;
/*
        What this process sourced, as against what the kernel now holds.

        host_machine is the kernel's answer, re-asked on every event. The
        functions this process can actually run are the ones it sourced, and
        the two stop agreeing the moment anything else publishes an edited
        /root/main.moonwater.sh -- moonwater status does, on its way to
        printing the script's own lines. The kernel then routes an event by
        the new text while this process still holds the old functions, and
        bind_queue, having handed the press to the machine, does not also run
        the image line. That is an event nobody runs.

        Keeping what was sourced is what lets the loop notice and re-source.
*/
static unsigned int host_machine_sourced_length;
static unsigned int host_machine_sourced_origin;
static struct moonwater_overlay host_machine_sourced_overlay;
//      This process owns the machine attach, from the moment it does.
static bool host_machine_self;
//      moonwater_end has run; it runs once however the machine stops.
static bool host_machine_ended;

#define HOST_MACHINE_ASSIGN(name, value) env_assign(name, value);

static bool host_machine_file_allowed(p16 mode, p32 owner)
{
        return (mode & MODE_FORMAT) == MODE_FILE && owner == 0 &&
               (mode & 0022) == 0;
}

static p8 host_machine_read_file(string_address path, p8 address_to text,
                                 positive room, positive address_to used)
{
        file_facts facts;
        file_facts opened;
        bipolar handle;
        bipolar got;

        address_to used = 0;
        if (!file_look(AT_FDCWD, path, AT_SYMLINK_NOFOLLOW, address_of facts))
                return HOST_MACHINE_ABSENT;
        if ((facts.mode & MODE_FORMAT) == MODE_LINK ||
            !host_machine_file_allowed(facts.mode, facts.owner))
                return HOST_MACHINE_REFUSED;

        handle = system_open_at(AT_FDCWD, path, FILE_READ | O_CLOEXEC | O_NOFOLLOW);
        if (handle < 0)
                return HOST_MACHINE_REFUSED;
        if (!file_look(handle, (string_address)"", AT_EMPTY_PATH, address_of opened) ||
            !host_machine_file_allowed(opened.mode, opened.owner)) {
                system_close(handle);
                return HOST_MACHINE_REFUSED;
        }

        for (;;) {
                if (address_to used == room) {
                        p8 extra;

                        got = system_read_retry((positive)handle,
                                                address_of extra, 1);
                        system_close(handle);
                        return (got < 0 || got) ? HOST_MACHINE_REFUSED : HOST_MACHINE_OK;
                }
                got = system_read_retry((positive)handle, text + address_to used,
                                        room - address_to used);
                if (got < 0) {
                        system_close(handle);
                        return HOST_MACHINE_REFUSED;
                }
                if (!got)
                        break;
                address_to used += (positive)got;
        }
        system_close(handle);
        return HOST_MACHINE_OK;
}

static bipolar host_machine_ioctl(bipolar device, unsigned int op,
                                  struct machine_control address_to control)
{
        memory_zero(control, sizeof(address_to control));
        control->op = op;
        return system_control(device, MOONWATER_IOCTL_MACHINE, control);
}

#define HOST_RADIO_WAIT_MS 3000u

static bipolar host_machine_wait(bipolar device,
                                 struct machine_control address_to control)
{
        memory_zero(control, sizeof(address_to control));
        control->op = MOONWATER_WAIT;
        control->reserved[0] = locale_wake_ms(HOST_RADIO_WAIT_MS);
        return system_control(device, MOONWATER_IOCTL_MACHINE, control);
}

static bipolar host_machine_script(unsigned int op)
{
        host_machine.op = op;
        return host_spark_once(MOONWATER_IOCTL_SCRIPT, address_of host_machine,
                               FILE_READ);
}

static fn host_machine_publish(void)
{
        positive used = 0;
        p8 loaded;

        if (!bowl_is_root())
                return;

        loaded = host_machine_read_file(HOST_MACHINE_SCRIPT, host_machine_text,
                                        MOONWATER_SCRIPT_BYTES, address_of used);
        if (loaded == HOST_MACHINE_REFUSED) {
                string_address line[] = { "machine script refused: ",
                                          HOST_MACHINE_SCRIPT, null };

                host_kmsg(line);
                return;
        }

        memory_zero(address_of host_machine, sizeof(host_machine));
        if (loaded == HOST_MACHINE_OK) {
                host_machine.length = (unsigned int)used;
                host_machine.address = (unsigned long)host_machine_text;
        }
        if (host_machine_script(MOONWATER_SCRIPT_SET) >= 0)
                host_machine_fresh = 1;
        byte_store_release(address_of text);
}

static fn host_machine_ask(void)
{
        if (host_machine_fresh)
                return;
        host_machine_publish();
        if (host_machine_fresh)
                return;
        memory_zero(address_of host_machine, sizeof(host_machine));
        if (host_machine_script(MOONWATER_SCRIPT_GET) < 0)
                host_machine.origin = MOONWATER_ORIGIN_BUILTIN;
        host_machine_fresh = 1;
}

static string_address host_machine_where(void)
{
        host_machine_ask();
        return host_machine.origin == MOONWATER_ORIGIN_DISK
                   ? HOST_MACHINE_SCRIPT
                   : HOST_MACHINE_BUILTIN;
}

static p16 host_machine_hook_line(p8 hook)
{
        unsigned int i;

        host_machine_ask();
        for (i = 0; i < MOONWATER_HOOKS; i++)
                if (moonwater_hook[i].bit == hook)
                        return moonwater_overlay_line(&host_machine.overlay,
                                                      moonwater_hook[i].line_off);
        return 0;
}

static p16 host_machine_event_line(unsigned int event)
{
        host_machine_ask();
        return moonwater_bind_line(&host_machine.overlay, event);
}

static fn host_machine_refused(string_address name, p16 line)
{
        string_format(log_error, host_label "%s is %s:%p; change it there\n", name,
                      host_machine_where(), (positive)line);
}

static fn host_machine_wait_verdict(p8 address_to into, positive room)
{
        p64 started = system_clock_ns(HOST_CLOCK_BOOTTIME);

        into[0] = end;
        for (;;) {
                if (host_read_text(HOST_VERDICT, into, room) >= 0)
                        return;
                if (system_clock_ns(HOST_CLOCK_BOOTTIME) - started >=
                    HOST_VERDICT_WAIT_NS)
                        return;
                host_pause(HOST_POLL_NS * 2);
        }
}

static b32 host_machine_call(positive slot, string_address name,
                             string_address first, string_address second)
{
        string_address arguments[2];
        positive count = 0;

        if (first)
                arguments[count++] = first;
        if (second)
                arguments[count++] = second;
        return shell_call_slot(slot, name, arguments, count);
}

#define HOST_MACHINE_FN 32

static fn host_machine_bind_fn(p8 address_to into, positive room, string_address rest)
{
        positive i;

        string_copy_bounded(into, "moonwater_", room);
        string_append_bounded(into, rest, room);
        for (i = 0; into[i]; i++)
                if (into[i] == ' ')
                        into[i] = '_';
}

static bool host_machine_try(string_address rest, string_address first,
                             string_address second)
{
        p8 fn[HOST_MACHINE_FN];
        positive slot;

        host_machine_bind_fn(fn, sizeof(fn), rest);
        slot = shell_function_slot(fn);
        if (slot == positive_max)
                return false;
        host_machine_call(slot, fn, first, second);
        return true;
}

static fn host_machine_lock_one(string_address name, bool lock)
{
        if (lock)
                exec_function_readonly_set(name);
        else
                exec_function_readonly_clear(name);
}

static fn host_machine_lock_fns(bool lock)
{
        p8 fn[HOST_MACHINE_FN];
        unsigned int event, i;

        for (event = 0; event < SPARK_BIND_EVENTS; event++)
        {
                host_machine_bind_fn(fn, sizeof(fn),
                                     (string_address)spark_bind_event_name[event]);
                host_machine_lock_one(fn, lock);
        }
        for (i = 0; i < MOONWATER_PAIRS; i++)
        {
                host_machine_bind_fn(fn, sizeof(fn),
                                     (string_address)moonwater_pairs[i].name);
                host_machine_lock_one(fn, lock);
        }
        host_machine_bind_fn(fn, sizeof(fn), "recover");
        host_machine_lock_one(fn, lock);
}

/*
        Asking the kernel what it holds now, and saying whether that is still
        the script this process is running.

        The answer is the overlay, the length and where the text came from --
        everything the kernel already puts in the reply, so noticing costs
        the GET that was happening anyway and nothing more. An edit that adds
        or removes a function moves the overlay, and one that changes a body
        almost always moves the length; what escapes is an edit that keeps
        the byte count, every line number and the same set of functions, and
        that leaves a stale body rather than an event nobody runs.
*/
static bool host_machine_changed(void)
{
        struct machine_script held = host_machine;

        memory_zero(address_of host_machine, sizeof(host_machine));
        if (host_machine_script(MOONWATER_SCRIPT_GET) < 0) {
                host_machine = held;
                return false;
        }
        host_machine_fresh = 1;
        return host_machine.length != host_machine_sourced_length ||
               host_machine.origin != host_machine_sourced_origin ||
               memory_compare(address_of host_machine.overlay,
                              address_of host_machine_sourced_overlay,
                              sizeof(host_machine_sourced_overlay));
}

static fn host_machine_hook(positive slot, unsigned int which,
                            string_address first, string_address second);

/*
        An event the script owns and this process could not run.

        The kernel hands a press to the machine instead of the bound image
        line precisely because the script owns that row, so there is no
        second copy waiting behind this call: a miss here is a key that does
        nothing. It should be unreachable now that the loop re-sources an
        edited script before naming the event, and that is the reason to say
        it out loud -- the next way this breaks will be silent otherwise.
*/
static fn host_machine_missed(string_address name)
{
        string_address line[] = { "machine script owns ", name,
                                  " but this process has no function for it; "
                                  "the event ran nowhere", null };

        host_kmsg(line);
}

static fn host_machine_emit(positive event_slot, unsigned int event,
                            string_address name, string_address extra)
{
        const struct moonwater_pair *pair = moonwater_paired(event);

        if (event && moonwater_bind_line(&host_machine.overlay, event))
        {
                if (pair && extra)
                {
                        p8 rest[24];

                        rest[0] = end;
                        string_append_bounded(rest, (string_address)pair->name,
                                              sizeof(rest));
                        string_append_bounded(rest, "_", sizeof(rest));
                        string_append_bounded(rest, extra, sizeof(rest));
                        if (!host_machine_try(rest, extra, null) &&
                            !host_machine_try((string_address)pair->name, extra,
                                              null))
                                host_machine_missed(name);
                }
                else if (!host_machine_try(name, name, extra))
                        host_machine_missed(name);
        }
        else if (string_equals(name, "recover"))
                host_machine_try("recover", name, extra);

        host_machine_hook(event_slot, 1, name, extra);
}

static fn host_machine_dirty(bool on)
{
        if (on)
                host_write_text(HOST_MACHINE_DIRTY, "1\n");
        else
                system_remove_at(AT_FDCWD, HOST_MACHINE_DIRTY, 0);
}

static fn host_machine_close(bipolar device)
{
        struct machine_control control;

        if (device < 0)
                return;
        (void)host_machine_ioctl(device, MOONWATER_DETACH, address_of control);
        system_close(device);
}

/*
        moonwater_end, at most once, wherever the machine turns out to stop.

        A machine script stops the machine from inside its own event function
        -- moonwater_poweroff runs poweroff -- and that never comes back to
        the wait loop, so the loop's own call is not the only place this has
        to happen. Running it from here as well, guarded, means the one
        function the file promises runs whether the stop came from the event
        or from the machine being told to end.
*/
static fn host_machine_end_run(void)
{
        string_address name = (string_address)moonwater_hook[2].name;

        if (host_machine_ended)
                return;
        host_machine_ended = true;
        if (!(host_machine.overlay.hooks & MOONWATER_HOOK_END))
                return;
        (void)host_machine_call(shell_function_slot(name), name, null, null);
}

/*
        Telling the machine process the machine is stopping, and waiting for
        it to let go -- unless we are it. True once moonwater_end has had its
        turn: the process was told, and was seen to leave. False when there
        was no process to tell, when it would not be told (the caller is not
        root: END is CAP_SYS_ADMIN's), and when it was still attached after
        ten seconds, which is a machine process that never ran moonwater_end,
        so the stop must not behave as though it had.

        host_machine_self is set the moment this process owns the attach, not
        when it is told to stop: its own moonwater_poweroff calls poweroff,
        which comes through here, and a process that asked the kernel to end
        the attach it is itself holding would poll for ten seconds for a
        detach only it could perform -- ten seconds in which the button
        already pressed looks like a button that did nothing.
*/
static bool host_machine_stop(void)
{
        struct machine_control control;
        bipolar device;
        p64 started;
        bool left = false;

        if (host_machine_self) {
                host_machine_end_run();
                return true;
        }
        device = system_open_at(AT_FDCWD, SPARK_DEVICE, FILE_READ | O_CLOEXEC);
        if (device < 0)
                return false;
        if (host_machine_ioctl(device, MOONWATER_STATUS, address_of control) >= 0 &&
            (control.flags & MOONWATER_ATTACHED) &&
            host_machine_ioctl(device, MOONWATER_END, address_of control) >= 0) {
                started = system_clock_ns(HOST_CLOCK_BOOTTIME);
                while (!left &&
                       system_clock_ns(HOST_CLOCK_BOOTTIME) - started < HOST_EXIT_EACH_NS) {
                        left = host_machine_ioctl(device, MOONWATER_STATUS,
                                                  address_of control) < 0 ||
                               !(control.flags & MOONWATER_ATTACHED);
                        if (!left)
                                host_pause(HOST_EVENT_POLL_NS);
                }
        }
        system_close(device);
        return left;
}

static b32 host_machine_source(void)
{
        string_address argv[3];
        string_address address_to saved_argv;
        positive saved_argc;
        bipolar handle;
        bipolar failed;

        memory_zero(address_of host_machine, sizeof(host_machine));
        host_machine.address = (unsigned long)host_machine_text;
        host_machine.length = MOONWATER_SCRIPT_BYTES;
        failed = host_machine_script(MOONWATER_SCRIPT_GET);
        if (failed < 0)
                return failed;
        host_machine_fresh = 1;

        host_state_ready();
        handle = host_open_state(AT_FDCWD, HOST_MACHINE_RUNTIME, 0600);
        if (handle < 0)
                return handle;
        failed = storage_format_write(handle, host_machine_text, host_machine.length,
                                      0);
        system_close(handle);
        if (failed < 0)
                return failed;

        saved_argv = shell_argv;
        saved_argc = shell_argc;
        argv[0] = ".";
        argv[1] = HOST_MACHINE_RUNTIME;
        argv[2] = null;
        shell_argv = argv;
        shell_argc = 2;
        shell_dot(log, HOST_MACHINE_RUNTIME);
        shell_argv = saved_argv;
        shell_argc = saved_argc;

        //      From here the functions in this process are this text's.
        host_machine_sourced_length = host_machine.length;
        host_machine_sourced_origin = host_machine.origin;
        host_machine_sourced_overlay = host_machine.overlay;
        return 0;
}

/*
        Holding the script's functions, and letting go.

        Locking them is what stops anything the script starts from redefining
        the machine's own bindings. Letting go is for one caller: this
        process reloading its own script, which has to be allowed to replace
        the bodies it locked, and takes the lock straight back.
*/
static fn host_machine_hold(positive address_to slot, bool lock)
{
        unsigned int i;

        for (i = 0; i < MOONWATER_HOOKS; i++) {
                host_machine_lock_one((string_address)moonwater_hook[i].name,
                                      lock);
                if (lock)
                        slot[i] = shell_function_slot(
                            (string_address)moonwater_hook[i].name);
        }
        host_machine_lock_fns(lock);
}

/*
        Taking up an edited script without restarting.

        Re-sourcing is the whole of it: the kernel already holds the new
        text, host_machine_source writes it out and dots it, and the
        definitions in it replace the ones this process is holding -- which
        is why the lock comes off first and goes straight back on. The hook
        slots are re-taken because a redefinition can land in a different
        one, and calling the old index would run the old body or nothing.

        A reload that fails leaves the old functions in place and the sourced
        identity unchanged, so the next event tries again rather than running
        on with a script it only half took. What it does not put back is the
        overlay: host_machine keeps the kernel's answer, because that is the
        answer the kernel routed this event by, and judging the event the
        same way it was routed is what turns a function this process has not
        got into one loud line instead of another silent drop.

        This re-runs the file's top-level lines, which is why a machine
        script keeps its work inside functions.
*/
static fn host_machine_reload(positive address_to slot)
{
        host_machine_hold(slot, false);
        if (host_machine_source() < 0) {
                string_address line[] = { "machine script reload refused; "
                                          "keeping the running one", null };

                host_kmsg(line);
        }
        host_machine_hold(slot, true);
}

static fn host_machine_hook(positive slot, unsigned int which,
                            string_address first, string_address second)
{
        if (host_machine.overlay.hooks & moonwater_hook[which].bit)
                host_machine_call(slot, (string_address)moonwater_hook[which].name,
                                  first, second);
}

/*
        What this process answers, and what PID 1 reads into it.

        init retires the machine for the rest of the boot on 0 and on 1,
        because those are the two ways it is finished on purpose: 0 is the
        queue told to end, 1 is a stop event already handed to shell_stop.
        Every other answer is a machine that did not get to start, or one
        that could not keep going, and init restarts it a bounded number of
        times.

        So a failure has to say something else. host_fail answers 1, so a
        /dev/spark that is not there yet, an attach the last machine process
        has not let go of, or one refused ioctl is otherwise a boot in which
        moonwater_init never runs -- and host_events_boot skips the settings
        init list whenever the overlay names that hook, whether or not
        anything is left to run it, so on a machine whose script defines
        moonwater_init nothing runs at boot at all.

        The bound image lines are not part of that: bind_queue hands a press
        to the machine only while an attach is live, and the process that
        died took its attach with it, so the power button is still poweroff.

        Not being root stays at host_refuse's 1. Every other refusal in this
        command answers 1, and PID 1 runs this as root, so that answer never
        reaches the retirement.
*/
#define HOST_MACHINE_ENDED 0
#define HOST_MACHINE_STOPPED 1
#define HOST_MACHINE_FAILED 2

static b32 host_machine_run(void)
{
        struct machine_control control;
        p8 verdict[HOST_NAME_ROOM + 16];
        bipolar device = -1;
        bipolar failed;
        p8 dirty[8];
        positive slot[MOONWATER_HOOKS];
        bool marked = false;
        b32 answer = HOST_MACHINE_ENDED;

        if (!bowl_is_root())
                return host_refuse("%s needs root\n", "moonwater machine");

        host_state_ready();
        system_call_1(syscall(chdir), (positive)(string_address) "/root");
        bowl_session_prepare("/root", null);
        SPARK_COMMAND_ENVIRONMENT(HOST_MACHINE_ASSIGN)

        device = system_open_at(AT_FDCWD, SPARK_DEVICE, FILE_READ | O_CLOEXEC);
        if (device < 0) {
                host_fail(SPARK_DEVICE, device);
                return HOST_MACHINE_FAILED;
        }
        failed = host_machine_ioctl(device, MOONWATER_ATTACH, address_of control);
        if (failed < 0) {
                system_close(device);
                host_fail("machine attach", failed);
                return HOST_MACHINE_FAILED;
        }
        //      The attach is ours from here, and every stop this process
        //      performs goes through host_machine_stop, including the one
        //      its own moonwater_poweroff asks for.
        host_machine_self = true;

        host_machine_wait_verdict(verdict, sizeof(verdict));
        host_machine_publish();
        failed = host_machine_source();
        if (failed < 0) {
                host_machine_close(device);
                host_fail("machine script", failed);
                return HOST_MACHINE_FAILED;
        }

        if (!host_starts((string_address)verdict, "ask "))
                radio_restore();
        locale_restore();
        tune_restore();

        host_machine_hold(slot, true);

        host_machine_hook(slot[0], 0, (string_address)verdict, null);
        if (host_read_text(HOST_MACHINE_DIRTY, dirty, sizeof(dirty)) >= 0) {
                host_machine_dirty(false);
                host_machine_emit(slot[1], 0, "recover", null);
        }

        for (;;) {
                string_address extra = null;
                string_address name;
                const struct moonwater_pair *pair;

                radio_recover();
                locale_recover();
                link_keep();
                failed = host_machine_wait(device, address_of control);
                if (failed == -4 || failed == -ETIMEDOUT)
                        continue;
                if (failed < 0) {
                        //      Not an ending: the wait itself was refused.
                        //      Answering the ending's own 0 here is the one
                        //      refused ioctl that retires the machine for the
                        //      rest of the boot.
                        host_fail("machine wait", failed);
                        answer = HOST_MACHINE_FAILED;
                        break;
                }

                if (!control.event) {
                        host_machine_dirty(false);
                        marked = false;
                        host_machine_end_run();
                        break;
                }

                //      Before the event is named, because naming it is how
                //      the new overlay decides whether the script owns it,
                //      and the functions have to be the new ones by then.
                if (host_machine_changed())
                        host_machine_reload(slot);
                pair = moonwater_paired(control.event);
                if (pair) {
                        name = (string_address)pair->name;
                        extra = control.event == pair->on ? (string_address) "on"
                                                          : (string_address) "off";
                } else if (control.event > SPARK_BIND_EVENTS)
                        continue;
                else
                        name = (string_address)spark_bind_event_name[control.event - 1];

                if (!marked) {
                        host_machine_dirty(true);
                        marked = true;
                }
                host_machine_emit(slot[1], control.event, name, extra);
                if (!control.queued && marked) {
                        host_machine_dirty(false);
                        marked = false;
                }

                if (spark_bind_is_stop(control.event) &&
                    moonwater_bind_line(&host_machine.overlay, control.event)) {
                        unsigned int stopping = control.event;

                        host_machine_end_run();
                        host_machine_dirty(false);
                        host_machine_close(device);
                        shell_stop(log, stopping == SPARK_BIND_POWEROFF
                                            ? HOST_REBOOT_POWER_OFF
                                            : HOST_REBOOT_RESTART);
                        return HOST_MACHINE_STOPPED;
                }
        }

        host_machine_dirty(false);
        host_machine_close(device);
        return answer;
}

#endif /* MOONWATER_CLI */
