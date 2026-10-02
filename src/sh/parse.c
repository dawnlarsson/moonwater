/*
        The shell's parser.

        The lexer hands over words and operators and nothing else. It does not
        know that "if" is a reserved word, and it is right not to: POSIX decides
        that by position, never by spelling, so "if" is a command name wherever
        a command name was expected and a keyword only where a keyword was.
        That decision belongs to a parser that knows where it is, and this is
        it -- recursive descent, one function per production of the grammar in
        XCU section 2.10.

        Two things shape the storage. There is no allocator, so the tree lives
        in one fixed array of nodes; and a function body has to outlive the
        line that defined it, so that array is used from both ends. Parsing a
        line claims nodes upward from the bottom and gives them all back when
        the line has run; a function definition is copied downward from the top
        and stays. The two never meet because the parse stops when they would.
*/

#define PT_END 0
#define PT_WORD 1
#define PT_OP 2
#define PT_NEWLINE 3
#define PT_ARITHMETIC 4
#define PT_CONDITIONAL 5

typedef struct parse_alias_trace parse_alias_trace;

struct parse_alias_trace
{
        parse_alias_trace address_to next;
        string_address name;
};

typedef struct
{
        b32 kind;
        b32 op;
        // Whether this token touches the one before it, which is the whole
        // difference between 2>file and 2 >file.
        b32 joined;
        string_address text;
        positive length;
        // Alias replacement is recursive, except through a name already in
        // the replacement chain. A trailing blank also asks that the next
        // ordinary word be considered even after the command name.
        parse_alias_trace address_to alias_trace;
        b32 alias_forced;
        // Which line of the input this word was read from, which is where a
        // construct built out of it was written.
        b32 line;
} parse_token;

/*
        The words of a line, and the bytes they are made of.

        The table may move -- nothing keeps an address inside it -- so it
        grows by taking a bigger mapping. The bytes may not: every token holds
        a pointer at them and so does everything downstream, so those come out
        of a block store that never moves what it has handed out. The parser
        already saved and restored a position in that arena to unwind a nested
        construct, and a mark in the store is the same idea.

        Both used to be fixed, which a line of any length met: sixteen
        kilobytes of token text is one long argument, and 512 tokens is a
        generated command list.
*/
static parse_token address_to parse_tokens HOT_STATE;
static positive parse_token_room HOT_STATE;
static positive parse_token_count HOT_STATE;
static shell_store parse_store HOT_STATE;
static parse_token parse_no_token;

string_address alias_lookup(string_address name);

#define NODE_SIMPLE 1
#define NODE_PIPELINE 2
#define NODE_ANDOR 3
#define NODE_LIST 4
#define NODE_IF 5
#define NODE_WHILE 6
#define NODE_UNTIL 7
#define NODE_FOR 8
#define NODE_CASE 9
#define NODE_CASE_ITEM 10
#define NODE_SUBSHELL 11
#define NODE_GROUP 12
#define NODE_FUNCTION 13
#define NODE_ARITHMETIC 14
#define NODE_CFOR 15
#define NODE_CONDITIONAL 16
#define NODE_SELECT 17
#define NODE_TIME 18
#define NODE_COPROC 19

/*
        One node shape for every production.

        A union would save a little memory and cost the reader the ability to
        see what a field means without first knowing which arm is live. The
        fields that go unused in a given kind are simply zero.

        left/right/extra are children, next is the sibling: a pipeline chains
        its commands through next, an and-or list chains its pipelines, and a
        list chains its and-ors, so the three levels of the grammar are three
        chains and not three shapes.
*/
typedef struct
{
        b32 kind;
        b32 op;
        b32 flags;
        b32 left;
        b32 right;
        b32 extra;
        b32 next;
        b32 word;
        b32 word_count;
        b32 redirect;
        b32 redirect_count;
        // The line this command was written on, taken from the token it
        // begins with. A function body outlives the line that defined it, so
        // this is how a call knows where it was made from.
        b32 line;
} parse_node;

typedef struct
{
        b32 op;
        b32 fd;
        string_address text;
        positive text_length;
        // `{name}` or `{name[index]}` as the descriptor, which bash looks up
        // when the redirect is applied. Empty when the descriptor is a number.
        string_address var;
        positive var_length;
        // A here-document carries its body in place of a file name. Which of
        // the two arenas the body sits in depends on whether the command it
        // belongs to outlived the line that wrote it.
        b32 kept;
        // A quoted delimiter makes the body literal; an unquoted one lets the
        // parameters in it expand.
        b32 raw;
        // <<- : the body has had its leading tabs taken off, and a listing
        // of the command must say so.
        b32 strip;
        positive body;
        positive body_length;
} parse_redirect;

/*
        How much the tree may hold, which is address space and not memory.

        These were fixed arrays of 768 nodes and words, 192 redirections and
        8 KiB of kept text, and a script met them long before it met anything
        dash or bash would refuse: a function of a hundred lines had no room
        to be kept, a hundred and ninety three redirections were a syntax
        error, and so was an if block of 765 commands. Nothing may move --
        a running command holds the address of its node while an eval parses
        underneath it -- so the arrays are carved out of one reservation made
        at the first parse, big enough for any script, and a page of it is
        memory only once a parse has written there. Both ends are used as
        before: the line's tree grows up from the bottom, kept bodies down
        from the top.
*/
#define PARSE_NODES (1 << 18)
#define PARSE_WORDS (1 << 18)
#define PARSE_REDIRECTS (1 << 16)
#define PARSE_KEPT_TEXT (1 << 23)

static parse_node address_to parse_nodes;
static string_address address_to parse_words;
/* What a word is besides its text, in one row: a command reads its words'
   rows together, a parse writes them together, and one array is one page of
   a start where four arrays were four (each in a window of its own, so each
   with page tables of its own). The text stays an array of its own because a
   command's words are handed on as the argument vector. */
typedef struct
{
        positive length;
        positive name_length;
        positive name_hash;
        positive flags;
} parse_word_row;
static parse_word_row address_to parse_word_rows;
static parse_redirect address_to parse_redirects;

#define PARSE_WORD_LITERAL 1
#define PARSE_WORD_ASSIGNMENT 2
#define PARSE_WORD_APPEND 4
// NAME=( ... ): the value is a list of elements and not one string, so it is
// neither expanded nor assigned the way every other assignment word is.
#define PARSE_WORD_COMPOUND 8
/* An assignment word with a newline inside it, which moves the line a
   command made only of assignments reports: asked once here, not at every
   run of the command. */
#define PARSE_WORD_NEWLINE 16

// What a case item's terminator was, kept in the item node's flags.
#define CASE_STOP 0
#define CASE_FALL_THROUGH 1
#define CASE_TEST_ON 2

static b32 parse_node_used;
static b32 parse_node_top HOT_STATE;
static b32 parse_word_used;
static b32 parse_word_top;
static b32 parse_redirect_used;
static b32 parse_redirect_top;

static p8 address_to parse_kept_text;
static bool parse_arenas();

#define PARSE_OK 0
#define PARSE_INCOMPLETE 1
#define PARSE_SYNTAX 2
/* a=( *(...) ) with extglob off: bash reports a syntax error, answers 1,
   and keeps reading. POSIX mode of the same bash exits 127 instead. */
#define PARSE_COMPOUND_SYNTAX 3

/*
        Where this parse starts, which is not always the beginning.

        eval runs a line from inside a line that is already running, and the
        tree being walked is in these same arrays. So a nested parse claims
        from where the outer one stopped and gives that back when it is done,
        rather than from zero over the top of what is still in use.
*/
typedef struct
{
        b32 node, word, redirect, position, state;
        positive token;
        shell_mark token_text;
        b32 wanted, filled, taken;
        positive used, names_used;
        // The last parse ran out of tokens where a list reads its next
        // command, and what the lines since then may take at most.
        b32 open_list;
        positive open_nodes, open_words, open_redirects;
        // The last parse of this frame ran out of tokens, under this memo
        // epoch, having used the arenas up to here.
        b32 memo_open;
        positive memo_epoch;
        b32 memo_nodes, memo_words, memo_redirects;
} parse_frame;

/*
        What the last parse already knew.

        A construct that spans lines is parsed again from its first token as
        each line arrives, since the parser cannot stop and resume, so a
        function or loop of n lines was parsed n times over: 867 million
        instructions to read a function of 800 lines of if, where dash takes
        seven. Each list remembers, by the token it began at, how far it had
        got -- its first and last command and where the next one begins --
        and the next parse of the same tokens, which only ever grow at the
        end, carries on from there instead of reading those commands again.
        A state is kept only with two tokens still behind it, which is as far
        as the parser ever looks ahead, and taken only with the same
        here-documents consumed before the list. A parse that carries on
        begins its arenas where the last one ended, so the nodes it keeps
        stay where they are; an epoch that moves whenever the tokens could
        have changed -- a reset, a nested parse, an alias -- retires every
        state.
*/
typedef struct
{
        positive epoch;
        b32 index, head, tail, at, here_start, here_at;
} parse_memo;

static parse_memo address_to parse_memos;
static positive parse_memo_room;
static positive parse_memo_epoch HOT_DATA = 1;
static bool parse_memo_on;

/* Live marks and saved marks intentionally have one shape. One assignment is
   the complete nest transition, so future state cannot drift between entry
   and return. */
static parse_frame parse_context = {.node = 1};

#define parse_node_base parse_context.node
#define parse_word_base parse_context.word
#define parse_redirect_base parse_context.redirect
#define parse_position parse_context.position
#define parse_state parse_context.state
#define parse_token_base parse_context.token
#define parse_text_base parse_context.token_text
#define here_wanted parse_context.wanted
#define here_filled parse_context.filled
#define here_taken parse_context.taken
#define here_used parse_context.used
#define here_names_used parse_context.names_used
#define parse_open_list parse_context.open_list

/*
        Here-documents, which arrive after the line that asked for them.

        The delimiters are collected while the lines are still being read,
        because the reader has to know that the next line is a body and not a
        command before the parser has seen any of it. The bodies are kept in
        the order the operators appeared, and the parser hands them out in the
        same order.
*/
typedef struct
{
        positive delimiter;
        positive body;
        positive length;
        b32 quoted;
        // <<- rather than <<, which takes the leading tabs off every line of
        // the body and off the line that ends it.
        b32 strip;
        // An unquoted trailing backslash removes its physical newline.  The
        // next physical line is body text even when it spells the delimiter,
        // because the logical line has not begun there.
        b32 continued;
        b32 overflow;
        positive line;
} here_document;

static here_document address_to here_documents;
static positive here_document_room;
static p8 address_to here_text;
static positive here_text_room;
static p8 address_to here_names;
static positive here_names_room;

/*
        A line the language is not finished with.

        A quote, a substitution and a trailing backslash all run past the end of
        the line they start on, and the reader hands over one line at a time.
        The unfinished text waits here until the rest of it arrives, so the
        lexer is only ever shown whole words -- before this, an open quote came
        out as a word with a quote still in it and the next line ran as a
        command of its own.
*/
static p8 address_to parse_pending;
static positive parse_pending_room;
static positive parse_pending_used HOT_STATE;
static positive parse_pending_line;

#define PARSE_WANT_ROOM 16
static string_address parse_want[PARSE_WANT_ROOM];
static string_address parse_want_opener[PARSE_WANT_ROOM];
static positive parse_want_line[PARSE_WANT_ROOM];
static positive parse_want_used HOT_STATE;

//      A closer waited for, with the command waiting and its line, which
//      is what bash names when the input ends before it is closed.
static HOT fn parse_want_push_for(string_address word, b32 index)
{
        string_address opener = null;

        switch (parse_nodes[index].kind)
        {
        case NODE_IF: opener = "if"; break;
        case NODE_WHILE: opener = "while"; break;
        case NODE_UNTIL: opener = "until"; break;
        case NODE_FOR: case NODE_CFOR: opener = "for"; break;
        case NODE_SELECT: opener = "select"; break;
        case NODE_CASE: opener = "case"; break;
        case NODE_GROUP: opener = "{"; break;
        default: break;
        }

        if (parse_want_used < PARSE_WANT_ROOM)
        {
                parse_want_opener[parse_want_used] = opener;
                parse_want_line[parse_want_used] = parse_nodes[index].line;
                parse_want[parse_want_used++] = word;
        }
}

static fn parse_want_pop()
{
        if (parse_want_used)
                parse_want_used--;
}

//      The command the innermost unfinished construct began with, or null.
PURE string_address parse_want_opener_now()
{
        return parse_want_used ? parse_want_opener[parse_want_used - 1] : null;
}

PURE positive parse_want_line_now()
{
        return parse_want_used ? parse_want_line[parse_want_used - 1] : 0;
}

static PURE string_address parse_want_now()
{
        return parse_want_used ? parse_want[parse_want_used - 1] : null;
}

static PURE positive parse_pending_start_line()
{
        return parse_pending_line ? parse_pending_line : 1;
}

/* Bash 5.2 keeps at most sixteen here-documents waiting for a body. lima
   reports `maximum here-document count exceeded` and leaves 2. Dash has no
   such cap. A document whose body has already been read is not pending, so
   forty cats on forty lines still run. */
#define PARSE_HERE_PENDING_MAX 16
static bool parse_here_capped;

fn lex_take_physical_newline();

// A terminal backslash-newline removed itself and only asked for the next
// physical line. At EOF an empty next line completes that command. A
// backslash that met EOF with no newline never asked: it stayed in the word.
// An open quote or substitution remains unfinished and is a syntax error.
bool parse_eof_can_complete()
{
        return parse_pending_used && !lex_unfinished(parse_pending);
}

//      dash's own reason for a syntax error the grammar cannot name by its
//      token: a for loop's variable that is no name.
static string_address parse_syntax_reason HOT_STATE;

//      How many commands deep the parse is, for parse_command's limit. Every
//      way out of parse_command passes the one decrement, so nothing needs to
//      put it back.
static positive parse_depth;

HOT fn parse_reset()
{
        parse_syntax_reason = null;
        parse_pending_used = 0;
        parse_pending_line = 0;
        parse_want_used = 0;
        parse_token_count = parse_token_base;
        shell_store_rewind(address_of parse_store, parse_text_base);
        here_wanted = 0;
        here_filled = 0;
        here_taken = 0;
        here_used = 0;
        here_names_used = 0;
        parse_here_capped = false;
        parse_open_list = false;
        parse_memo_epoch++;
}

bool parse_here_limit_exceeded()
{
        return parse_here_capped;
}

/*
        A line inside a line.

        What eval runs is parsed into the space above whatever is running, and
        the marks come back afterwards. A frame is the marks themselves, so
        nesting costs a few words and not a copy of the arrays. The stack is
        here rather than at the caller because its shape is nobody else's.
*/
static parse_frame address_to parse_frames;
static positive parse_frame_room;
static b32 parse_nest_depth;

fn parse_nest_enter()
{
        parse_frame address_to frame;

        if (parse_nest_depth == 0x7fffffff ||
            !shell_array_room(parse_frames, parse_frame_room, (positive)parse_nest_depth + 1))
        {
                log_error(str("No room for nested shell input\n"));
                log_flush();
                system_call_1(syscall(exit_group), 2);
        }

        frame = parse_frames + parse_nest_depth++;

        address_to frame = parse_context;
        parse_memo_epoch++;

        /* Node zero is the parser's absent-child sentinel.  A nested source
           can be the first source this process parses (BASH_ENV is one), so
           the live low-water mark has not necessarily been initialized by
           parse_program yet. */
        parse_node_base = parse_node_used ? parse_node_used : 1;
        parse_word_base = parse_word_used;
        parse_redirect_base = parse_redirect_used;
        parse_token_base = parse_token_count;
        parse_text_base = shell_store_mark(address_of parse_store);
}

fn parse_nest_leave()
{
        parse_frame address_to frame;

        if (!parse_nest_depth)
                return;

        frame = parse_frames + --parse_nest_depth;

        /*
                The tokens this nest claimed, given back to where it claimed
                from -- which is what the bases hold now, and not where the
                line outside it began, which is what they held before.

                Giving them back to the outer base threw away the tokens of
                the line that was still running: an eval inside a loop wrote
                its own line over the loop's words, and the second time round
                the name of the loop variable was the empty string. Not giving
                the nodes back at all was the other half of it, and a loop that
                ran eval eighty times ran the tree out.
        */
        parse_node_used = parse_node_base;
        parse_word_used = parse_word_base;
        parse_redirect_used = parse_redirect_base;
        parse_token_count = parse_token_base;
        shell_store_rewind(address_of parse_store, parse_text_base);

        parse_context = address_to frame;
        parse_memo_epoch++;
}

// What a child of this shell has to know about being one. Declared here and
// answered in exec.c, which is where being a child makes a difference.
fn exec_child_began();

// A forked substitution has the outer line's marks and no use for them: what
// it runs is the only thing it will ever run.
fn parse_reset_all()
{
        exec_child_began();

        parse_node_base = 1;
        parse_word_base = 0;
        parse_redirect_base = 0;
        parse_token_base = 0;
        //      Back to the beginning of the store, which is what a
        //      mark taken before anything was put in it means.
        shell_store_reset(address_of parse_store);
        parse_text_base = shell_store_mark(address_of parse_store);
        parse_reset();
}

static PURE inline INLINE parse_token address_to parse_look(b32 ahead)
{
        b32 index = parse_position + ahead;

        if (index < 0 || index >= (b32)parse_token_count)
                return address_of parse_no_token;

        return parse_tokens + index;
}

static HOT PURE bool parse_word_is_length(b32 ahead, string_address text,
                                      positive length)
{
        parse_token address_to token = parse_look(ahead);

        return token->kind == PT_WORD && token->length == length &&
               !memory_compare(token->text, text, length);
}

/* Every grammar spelling is a literal. Carry its size through the call so a
   parser comparison becomes the compiler_memory fixed-size floor rather than
   a separate string-length pass followed by a generic comparison. */
#define parse_word_is(ahead, text) \
        parse_word_is_length((ahead), (string_address)(text), sizeof(text) - 1)

enum
{
        PARSE_KEYWORD_NONE,
        PARSE_KEYWORD_IF,
        PARSE_KEYWORD_WHILE,
        PARSE_KEYWORD_UNTIL,
        PARSE_KEYWORD_FOR,
        PARSE_KEYWORD_CASE,
        //      Everything from IF to OPEN begins a command and everything
        //      from THEN to IN ends a list, and two range tests below say
        //      which is which. A new word that begins a command belongs
        //      here, in front of OPEN, and nowhere else. `in` is with the
        //      closers because it cannot start a command; bang is after
        //      them because a pipeline may begin with it.
        PARSE_KEYWORD_SELECT,
        PARSE_KEYWORD_TIME,
        PARSE_KEYWORD_COPROC,
        PARSE_KEYWORD_OPEN,
        PARSE_KEYWORD_THEN,
        PARSE_KEYWORD_ELSE,
        PARSE_KEYWORD_ELIF,
        PARSE_KEYWORD_FI,
        PARSE_KEYWORD_DO,
        PARSE_KEYWORD_DONE,
        PARSE_KEYWORD_ESAC,
        PARSE_KEYWORD_CLOSE,
        PARSE_KEYWORD_IN,
        PARSE_KEYWORD_BANG,
        PARSE_KEYWORD_DEND,
};

/*
        Which reserved word a token is, if any: one question per token the
        parser meets, a million and a half of them in reading five configure
        scripts, test/run and two ltmain.sh.

        The C asked by length first, a jump table over lengths one to six
        and then a compare or two, and the lengths of real words arrive in
        no order a predictor can follow. Here nothing waits on the length:
        the word's first eight bytes are read at once -- whenever they lie
        in its page, and byte by byte in the rare case they would not --
        and cut to its length, and that key is multiplied into one of 32
        slots that hold the twenty reserved words each in a slot of its own.
        The slot's word is compared with the key, and a word longer than six
        or a slot holding another word both answer none with a conditional
        move. No word holds a NUL, so the key alone names the word.

        ]] answers as a keyword here; parse_keyword keeps it one only for
        bash, as the reserved words of POSIX mode do not include it.
*/
PURE b32 parse_keyword_of(const parse_token address_to token);

_Static_assert(__builtin_offsetof(parse_token, kind) == 0 &&
               __builtin_offsetof(parse_token, text) == 16 &&
               __builtin_offsetof(parse_token, length) == 24 && PT_WORD == 1,
               "parse_keyword_of reads the token at these offsets");
_Static_assert(PARSE_KEYWORD_IF == 1 && PARSE_KEYWORD_WHILE == 2 &&
               PARSE_KEYWORD_UNTIL == 3 && PARSE_KEYWORD_FOR == 4 &&
               PARSE_KEYWORD_CASE == 5 && PARSE_KEYWORD_SELECT == 6 &&
               PARSE_KEYWORD_TIME == 7 && PARSE_KEYWORD_COPROC == 8 &&
               PARSE_KEYWORD_OPEN == 9 && PARSE_KEYWORD_THEN == 10 &&
               PARSE_KEYWORD_ELSE == 11 && PARSE_KEYWORD_ELIF == 12 &&
               PARSE_KEYWORD_FI == 13 && PARSE_KEYWORD_DO == 14 &&
               PARSE_KEYWORD_DONE == 15 && PARSE_KEYWORD_ESAC == 16 &&
               PARSE_KEYWORD_CLOSE == 17 && PARSE_KEYWORD_IN == 18 &&
               PARSE_KEYWORD_BANG == 19 && PARSE_KEYWORD_DEND == 20,
               "the slots of parse_keyword_of spell these numbers");

/* Slot = (key * 0xd1ea041814d4954f) >> 59; each row is two slots, the
   little-endian word and its keyword. */
#define PARSE_KEYWORD_SLOTS \
    ".quad 0x6c69746e75, 3, 0x21, 19\n" \
    ".quad 0x0, 0, 0x0, 0\n" \
    ".quad 0x6e656874, 10, 0x0, 0\n" \
    ".quad 0x65736163, 5, 0x7463656c6573, 6\n" \
    ".quad 0x5d5d, 20, 0x0, 0\n" \
    ".quad 0x6669, 1, 0x656d6974, 7\n" \
    ".quad 0x636f72706f63, 8, 0x0, 0\n" \
    ".quad 0x6f64, 14, 0x7d, 17\n" \
    ".quad 0x0, 0, 0x0, 0\n" \
    ".quad 0x0, 0, 0x6966, 13\n" \
    ".quad 0x6e69, 18, 0x0, 0\n" \
    ".quad 0x656c696877, 2, 0x656e6f64, 15\n" \
    ".quad 0x0, 0, 0x63617365, 16\n" \
    ".quad 0x66696c65, 12, 0x7b, 9\n" \
    ".quad 0x65736c65, 11, 0x726f66, 4\n" \
    ".quad 0x0, 0, 0x0, 0\n"

#if X64
__asm__(
    ASM_FUNC(parse_keyword_of)
    "cmpl $1, (%rdi)\n   jne 3f\n"
    "mov 16(%rdi), %rsi\n   mov 24(%rdi), %r8\n   lea .Lparse_keyword_x64_slots(%rip), %r10\n"
    // An empty word may point anywhere, so it reads the slots instead.
    "test %r8, %r8\n   cmovz %r10, %rsi\n"
    "mov %esi, %eax\n   and $4095, %eax\n   cmp $4088, %eax\n   ja 4f\n"
    "mov (%rsi), %rdx\n"
    // The key: the word's bytes, the rest cut away.
    "1:  lea (,%r8,8), %ecx\n   mov $-1, %rax\n   shl %cl, %rax\n   not %rax\n   and %rax, %rdx\n"
    "movabs $0xd1ea041814d4954f, %rax\n   imul %rdx, %rax\n   shr $59, %rax\n   shl $4, %eax\n"
    "mov %r10, %rcx\n   xor %r9d, %r9d\n"
    "cmp (%rcx,%rax), %rdx\n   mov 8(%rcx,%rax), %eax\n   cmovne %r9d, %eax\n"
    "cmp $6, %r8\n   cmova %r9d, %eax\n"
    ASM_RET
    "3:  xor %eax, %eax\n"
    ASM_RET
    // Eight bytes would leave the page: a word of one to six, byte by byte.
    "4:  lea -1(%r8), %rcx\n   cmp $5, %rcx\n   ja 3b\n   xor %edx, %edx\n   mov %r8, %rcx\n"
    "5:  shl $8, %rdx\n   movzbl -1(%rsi,%rcx), %eax\n   or %rax, %rdx\n   dec %rcx\n   jnz 5b\n"
    "jmp 1b\n"
    ".pushsection .rodata\n   .balign 16\n.Lparse_keyword_x64_slots:\n"
    PARSE_KEYWORD_SLOTS
    ".popsection\n"
    ASM_END(parse_keyword_of)
);
#elif ARM64
__asm__(
    ASM_FUNC(parse_keyword_of)
    "ldr w2, [x0]\n   cmp w2, #1\n   b.ne 3f\n"
    "ldr x1, [x0, #16]\n   ldr x3, [x0, #24]\n   adr x10, 6f\n"
    // An empty word may point anywhere, so it reads the slots instead.
    "cmp x3, #0\n   csel x1, x10, x1, eq\n"
    "and x4, x1, #4095\n   cmp x4, #4088\n   b.hi 4f\n   ldr x5, [x1]\n"
    // The key: the word's bytes, the rest cut away.
    "1:  lsl x6, x3, #3\n   mov x7, #-1\n   lsl x7, x7, x6\n   bic x5, x5, x7\n"
    "mov x8, #0x954f\n   movk x8, #0x14d4, lsl #16\n   movk x8, #0x0418, lsl #32\n   movk x8, #0xd1ea, lsl #48\n"
    "mul x9, x5, x8\n   lsr x9, x9, #59\n   add x10, x10, x9, lsl #4\n"
    "ldp x11, x12, [x10]\n   cmp x11, x5\n   csel w0, w12, wzr, eq\n"
    "cmp x3, #6\n   csel w0, w0, wzr, ls\n"
    ASM_RET
    "3:  mov w0, #0\n"
    ASM_RET
    // Eight bytes would leave the page: a word of one to six, byte by byte.
    "4:  sub x6, x3, #1\n   cmp x6, #5\n   b.hi 3b\n   mov x5, #0\n   mov x6, x3\n"
    "5:  sub x6, x6, #1\n   ldrb w7, [x1, x6]\n   orr x5, x7, x5, lsl #8\n   cbnz x6, 5b\n   b 1b\n"
    ".balign 16\n6:\n"
    PARSE_KEYWORD_SLOTS
    ASM_END(parse_keyword_of)
);
#elif RISCV64
__asm__(
    ASM_FUNC(parse_keyword_of)
    "lw t0, 0(a0)\n   li t1, 1\n   bne t0, t1, 3f\n"
    "ld a1, 16(a0)\n   ld a2, 24(a0)\n   addi t2, a2, -1\n   li t1, 5\n   bgtu t2, t1, 3f\n"
    // Byte by byte: a misaligned load is not something every core does.
    "li a3, 0\n   mv t2, a2\n"
    "1:  addi t2, t2, -1\n   add t3, a1, t2\n   lbu t4, 0(t3)\n   slli a3, a3, 8\n   or a3, a3, t4\n   bnez t2, 1b\n"
    "li t0, 0xd1ea041814d4954f\n   mul t0, a3, t0\n   srli t0, t0, 59\n   slli t0, t0, 4\n"
    "lla t1, .Lparse_keyword_riscv_slots\n   add t1, t1, t0\n"
    "ld t2, 0(t1)\n   lw a0, 8(t1)\n   bne t2, a3, 3f\n"
    ASM_RET
    "3:  li a0, 0\n"
    ASM_RET
    ".pushsection .rodata\n   .balign 16\n.Lparse_keyword_riscv_slots:\n"
    PARSE_KEYWORD_SLOTS
    ".popsection\n"
    ASM_END(parse_keyword_of)
);
#endif

static PURE inline INLINE b32 parse_keyword(b32 ahead)
{
        b32 keyword = parse_keyword_of(parse_look(ahead));

        /* dash reserves the fifteen of POSIX and no more: select, time,
           coproc and ]] are ordinary words there. */
        if (shell_dash_compat &&
            (keyword == PARSE_KEYWORD_SELECT || keyword == PARSE_KEYWORD_TIME ||
             keyword == PARSE_KEYWORD_COPROC))
                return PARSE_KEYWORD_NONE;

        return keyword == PARSE_KEYWORD_DEND && !shell_bash_compat
                   ? PARSE_KEYWORD_NONE : keyword;
}

/*
        A here-document delimiter with its quoting taken off.

        The quotes decide whether the body is expanded, so which quotes were
        used matters as much as what is left when they are gone.
*/
static bool parse_here_register(string_address word, bool strip)
{
        string_address step = word;
        positive start = here_names_used;
        positive reserve = string_length(word) + 1;
        here_document address_to document;

        if (shell_bash_compat &&
            here_wanted - here_filled >= PARSE_HERE_PENDING_MAX)
        {
                if (!parse_here_capped)
                {
                        shell_syntax_where();
                        log_error(str("maximum here-document count exceeded\n"));
                        parse_here_capped = true;
                }

                return false;
        }

        if (!shell_array_room(here_documents, here_document_room, (positive)here_wanted + 1) ||
            !shell_array_room(here_names, here_names_room, here_names_used + reserve))
                return false;

        document = here_documents + here_wanted;
        memory_fill(document, 0, sizeof(*document));
        document->delimiter = start;
        document->strip = strip;
        document->line = shell_line_number ? shell_line_number : 1;

        while (string_get(step))
        {
                p8 c = string_get(step);

                if (c == '\'' || c == '"')
                {
                        document->quoted = true;
                        step++;

                        while (string_get(step) && string_get(step) != c)
                                here_names[here_names_used++] = string_get(step++);

                        if (string_get(step))
                                step++;

                        continue;
                }

                if (c == '\\' && string_get(step + 1))
                {
                        document->quoted = true;
                        step++;
                        c = string_get(step);
                }

                here_names[here_names_used++] = c;
                step++;
        }

        here_names[here_names_used++] = end;
        here_wanted++;

        return true;
}

fn parse_here_close();

// Which delimiter the reader is waiting for, or nothing when it is waiting for
// a command.
PURE string_address parse_here_open()
{
        if (here_filled >= here_wanted)
                return null;

        return here_names + here_documents[here_filled].delimiter;
}

static PURE positive parse_here_start_line()
{
        if (here_filled >= here_wanted)
                return 1;

        return here_documents[here_filled].line
                   ? here_documents[here_filled].line
                   : 1;
}

/* A body line, even an empty one, is what moves bash's EOF warning off the
   opener. A trailing newline that only finished the << line is not a body. */
static PURE bool parse_here_got_body()
{
        if (here_filled >= here_wanted)
                return false;

        return here_documents[here_filled].length != 0;
}

static bool parse_here_take_span(string_address line, positive length,
                                 bool keep)
{
        here_document address_to document;
        string_address delimiter;
        positive delimiter_length;
        bool continues = false;

        if (here_filled >= here_wanted)
                return false;

        /*
                The tabs a <<- body does not keep, and the terminator hiding
                behind them.

                The reader compares the line it read against the delimiter
                before handing it over, and by then the tabs are still on it.
                Which of the two the line is can only be decided where what
                would be stripped is known, which is here.
        */
        document = here_documents + here_filled;

        if (document->strip)
        {
                positive tabs = memory_span_byte(line, '\t', length);
                line += tabs;
                length -= tabs;
        }

        delimiter = here_names + document->delimiter;
        delimiter_length = string_length(delimiter);

        /* Delimiter recognition belongs beside continuation state.  A line
           joined to its predecessor cannot terminate the document, even if
           that physical line consists only of the delimiter. */
        if (!document->continued && length == delimiter_length &&
            !memory_compare(line, delimiter, length))
        {
                parse_here_close();
                return true;
        }

        if (!document->quoted && length)
        {
                positive slash = length;

                while (slash && string_is(line + slash - 1, '\\'))
                        slash--;

                continues = ((length - slash) & 1) != 0;
                if (continues)
                        length--;
        }

        /* Boundary discovery needs exactly the same delimiter and physical
           continuation decisions, but not a second copy of body bytes that
           the real nested parser will collect later. */
        if (!keep)
        {
                document->continued = continues;
                return true;
        }

        if (!document->length)
                document->body = here_used;

        // A body that does not fit is not a body with its end cut off: the
        // reader does not look at what this answers, so the complaint is made
        // here and the command it belongs to is refused when it is parsed.
        if (!shell_array_room(here_text, here_text_room, here_used + length + 2))
        {
                if (!document->overflow)
                        string_format(log_error, "Here-document too long: %s\n",
                                      here_names + document->delimiter);

                document->overflow = true;

                return false;
        }

        memory_copy(here_text + here_used, line, length);
        here_used += length;

        if (!continues)
                here_text[here_used++] = '\n';

        here_text[here_used] = end;
        document->length = here_used - document->body;
        document->continued = continues;

        return true;
}

bool parse_here_line(string_address line)
{
        return parse_here_take_span(line, string_length(line), true);
}

fn parse_here_close()
{
        if (here_filled < here_wanted)
        {
                here_document address_to document = here_documents + here_filled;

                if (!document->length)
                        document->body = here_used;

                // Past the terminator, so the body after this one does not
                // overwrite it and every body stands as a string.
                if (shell_array_room(here_text, here_text_room, here_used + 1))
                {
                        here_text[here_used] = end;
                        here_used++;
                }
                else
                        document->overflow = true;

                here_filled++;
        }
}

static bool parse_hold(string_address line, b32 unfinished)
{
        positive length = string_length(line);

        if (!parse_pending_used)
                parse_pending_line = shell_line_number ? shell_line_number : 1;

        if (line != parse_pending)
        {
                if (!shell_array_room(parse_pending, parse_pending_room, length + 2))
                        return false;

                memory_copy(parse_pending, line, length + 1);
        }

        // A backslash before a newline was only ever the mark saying "not
        // yet"; a newline inside a quote or a substitution is a byte of it.
        if (unfinished == LEX_CONTINUES)
                length--;
        else
                parse_pending[length++] = '\n';

        parse_pending[length] = end;
        parse_pending_used = length;

        return true;
}

/*
        A backslash before a newline that a joined word still holds.

        The reader drops a line-ending backslash when it joins the next line
        on, but only where the line was complete but for it: at the top of a
        word and inside double quotes. Inside ${ } and $(( )) the line is open
        instead, so the next one is joined with its newline and the backslash
        stays -- and `echo "${v-a \<newline>b}"` printed the backslash where
        bash and dash print `a b`, while $((1 + \<newline>2)) was an error.
        Continuation is the tokenizer's business everywhere but inside real
        single quotes; a $( ) or backquote body keeps its bytes for its own
        reader. Contexts: 0 unquoted, 1 double quotes, 2 ${ } unquoted,
        3 ${ } inside double quotes, 4 $(( )).
*/
#define PARSE_STRIP_DEPTH 64

static bool parse_joined_line HOT_STATE;

static COLD positive parse_strip_continuations(p8 address_to text,
                                               positive length, p8 kind)
{
        p8 context[PARSE_STRIP_DEPTH];
        positive parens[PARSE_STRIP_DEPTH];
        positive depth = 0;
        positive at = 0;
        positive out = 0;

        context[0] = kind;
        parens[0] = 0;
        while (at < length)
        {
                p8 value = text[at];
                p8 now = context[depth];
                bool literal_quotes = now == 1 || now == 3 || now == 4;

                if (value == '\\' && at + 1 < length)
                {
                        if (text[at + 1] == '\n')
                        {
                                at += 2;
                                continue;
                        }
                        text[out++] = text[at++];
                        text[out++] = text[at++];
                        continue;
                }
                if (value == '\'' && !literal_quotes)
                {
                        string_address shut = memory_first_of(
                            text + at + 1, '\'', length - at - 1);
                        positive stop = shut ? (positive)(shut - text) + 1
                                             : length;

                        memory_copy(text + out, text + at, stop - at);
                        out += stop - at;
                        at = stop;
                        continue;
                }
                if (value == '`' ||
                    (value == '$' && at + 1 < length && text[at + 1] == '(' &&
                     !(at + 2 < length && text[at + 2] == '(')) ||
                    (value == '$' && at + 1 < length && text[at + 1] == '\'' &&
                     !literal_quotes))
                {
                        string_address from = text + at + (value == '$');
                        string_address stop =
                            value == '$' && text[at + 1] == '\''
                                ? lex_dollar_quote_end(from + 1)
                                : lex_nesting(from);
                        positive until;

                        if (stop == from || !stop)
                                until = length;
                        else
                        {
                                until = (positive)(stop - text);
                                if (value == '$' && text[at + 1] == '\'')
                                        until++;
                        }
                        if (until > length)
                                until = length;
                        memory_copy(text + out, text + at, until - at);
                        out += until - at;
                        at = until;
                        continue;
                }
                if (depth + 1 < PARSE_STRIP_DEPTH && value == '$' &&
                    at + 1 < length &&
                    (text[at + 1] == '{' || text[at + 1] == '('))
                {
                        bool arith = text[at + 1] == '(';

                        context[++depth] = arith ? 4 : now == 0 || now == 2 ? 2 : 3;
                        parens[depth] = 0;
                        text[out++] = text[at++];
                        text[out++] = text[at++];
                        if (arith)
                                text[out++] = text[at++];
                        continue;
                }
                if (value == '"' && (now == 0 || now == 2 || now == 3) &&
                    depth + 1 < PARSE_STRIP_DEPTH)
                        context[++depth] = 1, parens[depth] = 0;
                else if (value == '"' && now == 1 && depth)
                        depth--;
                else if (value == '}' && (now == 2 || now == 3) && depth)
                        depth--;
                else if (now == 4 && value == '(')
                        parens[depth]++;
                else if (now == 4 && value == ')')
                {
                        if (parens[depth])
                                parens[depth]--;
                        else if (at + 1 < length && text[at + 1] == ')' &&
                                 depth)
                        {
                                text[out++] = text[at++];
                                depth--;
                        }
                }
                text[out++] = text[at++];
        }
        text[out] = 0;
        return out;
}

static const string_address parse_operator_spelling[] = {
    0,
    "&&",
    "||",
    ";;",
    "<<",
    ">>",
    "<&",
    ">&",
    "<>",
    ">|",
    ";",
    "|",
    "&",
    "<",
    ">",
    "(",
    ")",
    "&>",
    "&>>",
    "<<<",
    "|&",
    ";&",
    ";;&",
};

/*
        bash's longer operators, read the way dash reads them.

        dash has &>, <<<, |&, ;& and ;;& as two operators each, and so the
        second of the two is where it stops: `cat <<< x` is a here-document
        with no word, `a |& b` a pipe to nothing and `&> f` a background
        job with a redirection after it. The lexer is one for both names, so
        the split is made here where the tokens are copied, which leaves the
        parser to say what dash says of what follows.
*/
static COLD __attribute__((noinline)) bool parse_operator_split(parse_token address_to first,
                                 parse_token address_to second)
{
        static const b32 pairs[][3] = {
            {OP_ANDGREAT, OP_AMP, OP_GREAT},
            {OP_ANDDGREAT, OP_AMP, OP_DGREAT},
            {OP_HERESTRING, OP_DLESS, OP_LESS},
            {OP_PIPEAND, OP_PIPE, OP_AMP},
            {OP_SEMIAND, OP_SEMI, OP_AMP},
            {OP_DSEMIAND, OP_DSEMI, OP_AMP},
        };

        if (first->kind != PT_OP)
                return false;

        for (positive at = 0; at < array_count(pairs); at++)
                if (first->op == pairs[at][0])
                {
                        *second = *first;
                        first->op = pairs[at][1];
                        first->text = (string_address)parse_operator_spelling[first->op];
                        first->length = string_length(first->text);
                        second->op = pairs[at][2];
                        second->text = (string_address)parse_operator_spelling[second->op];
                        second->length = string_length(second->text);
                        second->joined = 1;
                        return true;
                }

        return false;
}

// Lexer's storage is reused on its next call. Copy one token into parser
// storage, keeping every piece of text in the stable arena.
static bool parse_copy_lex(parse_token address_to into,
                           lex_token address_to source,
                           parse_alias_trace address_to trace)
{
        into->kind = source->kind == LEX_WORD
                         ? PT_WORD
                         : source->kind == LEX_ARITHMETIC
                               ? PT_ARITHMETIC
                               : source->kind == LEX_CONDITIONAL
                                     ? PT_CONDITIONAL
                                     : PT_OP;
        into->op = source->op;
        into->text = null;
        into->length = source->length;
        into->alias_trace = trace;
        into->alias_forced = false;
        into->line = (b32)shell_line_number;

        if (into->kind == PT_OP)
        {
                if (into->op > 0 &&
                    into->op < (b32)array_count(parse_operator_spelling))
                        into->text = (string_address)parse_operator_spelling[into->op];

                return true;
        }

        into->text = shell_store_copy(address_of parse_store, source->text,
                                      source->length);
        if (into->text && parse_joined_line &&
            memory_first_of(into->text, '\n', into->length))
                into->length = parse_strip_continuations(
                    (p8 address_to)into->text, into->length,
                    into->kind == PT_ARITHMETIC ? 4 : 0);
        return into->text != null;
}

// One token of the line the lexer just cut, and whether it touched the one
// before it -- which the lexer's positions say and only the parser keeps.
static HOT bool parse_copy_lexed(parse_token address_to into, b32 index,
                             parse_alias_trace address_to trace)
{
        lex_token address_to source = lex_tokens + index;

        into->joined = index && source->at == lex_tokens[index - 1].at +
                                                  lex_tokens[index - 1].length;

        return parse_copy_lex(into, source, trace);
}

// The newline that ends a line of tokens, which the lexer never makes: it
// separates commands exactly as a semicolon does, and inside a construct it
// is the only thing that does.
static fn parse_token_newline(parse_token address_to into,
                              parse_alias_trace address_to trace)
{
        into->kind = PT_NEWLINE;
        into->op = 0;
        into->joined = 0;
        into->text = null;
        into->length = 0;
        into->alias_trace = trace;
        into->alias_forced = false;
        into->line = (b32)shell_line_number;
}

/* One lexer/parser-independent delimiter view.  Both ordinary parser tokens
   and the substitution boundary scanner reduce to these three values before
   quote removal and <<- policy are decided here. */
static bool parse_here_words(string_address delimiter, bool joined,
                             string_address next)
{
        bool strip = false;

        if (delimiter && joined && string_is(delimiter, '-'))
        {
                strip = true;
                delimiter++;

                if (!string_get(delimiter))
                        delimiter = next;
        }

        return !delimiter || parse_here_register(delimiter, strip);
}

// Register a here-document as soon as its complete parser-token line is
// available, whether that line came from source or from an alias replacement.
static bool parse_here_at(b32 at)
{
        b32 word = at + 1;
        string_address delimiter =
            word < (b32)parse_token_count &&
                    parse_tokens[word].kind == PT_WORD
                ? parse_tokens[word].text
                : null;
        string_address next =
            word + 1 < (b32)parse_token_count &&
                    parse_tokens[word + 1].kind == PT_WORD
                ? parse_tokens[word + 1].text
                : null;
        bool joined = delimiter && parse_tokens[word].joined;

        return parse_here_words(delimiter, joined, next);
}

/*
        A line of source, appended to whatever is already waiting.

        The lexer works a line at a time and reuses its own storage, so every
        token is copied out before the next line goes through it. The newline
        is kept as a token of its own: it separates commands exactly as a
        semicolon does, and inside a construct it is the only thing that does.
*/
HOT bool parse_feed(string_address line)
{
        b32 count;
        positive token_start;
        b32 index;
        b32 unfinished;
        //      The line the text being lexed starts on: this one, or the one
        //      an open quote or a trailing backslash started on.
        positive first_line = shell_line_number ? shell_line_number : 1;

        parse_joined_line = parse_pending_used != 0;

        // Joining first is what lets the unfinished thing be recognised at
        // all: the quote that closes is on this line and the one that opened
        // it is on the last.
        if (parse_pending_used)
        {
                first_line = parse_pending_start_line();
                positive length = string_length(line);

                if (!shell_array_room(parse_pending, parse_pending_room,
                                      parse_pending_used + length + 2))
                        return false;

                memory_copy(parse_pending + parse_pending_used, line, length + 1);
                parse_pending_used += length;
                line = parse_pending;
        }

        // The unfinished walk and the token walk read the same bytes. Skip
        // the first when a span of lex_closed already proves nothing on the
        // line can still be open.
        lex_take_physical_newline();
        unfinished = lex_line_closed(line) ? LEX_COMPLETE : lex_unfinished(line);

        if (unfinished)
                return parse_hold(line, unfinished);

        parse_pending_used = 0;
        count = lex_line(line);

        if (count < 0)
                return false;

        token_start = parse_token_count;

        /*
                Each token takes the physical line it starts on. A word that
                ran over a newline used to give every token of the joined text
                the line the text ended on, so a command whose quoted word
                spans two lines, or that goes on after a backslash, read
                $LINENO as its last line where bash reads its first.
        */
        positive counted = 0;
        positive token_line = first_line;

        for (index = 0; index < count; index++)
        {
                //      Room taken before any address into the table is,
                //      because taking room is what may move it.
                if (!shell_array_room(parse_tokens, parse_token_room, parse_token_count + 2))
                        return false;

                if (!parse_copy_lexed(parse_tokens + parse_token_count, index,
                                      null))
                        return false;

                if (lex_tokens[index].at > counted)
                {
                        token_line += memory_count(line + counted,
                                                   lex_tokens[index].at - counted, '\n');
                        counted = lex_tokens[index].at;
                }
                parse_tokens[parse_token_count].line = (b32)token_line;

                if (shell_dash_compat &&
                    parse_operator_split(parse_tokens + parse_token_count,
                                         parse_tokens + parse_token_count + 1))
                        parse_token_count++;

                parse_token_count++;
        }

        for (positive at = token_start; at < parse_token_count; at++)
                if (parse_tokens[at].kind == PT_OP &&
                    parse_tokens[at].op == OP_DLESS && !parse_here_at((b32)at))
                        return false;

        /*
                A comment-only line has no lexer tokens, but it is still a
                line and the parser still needs its newline sentinel.  The
                per-token room check above never runs for that shape.  A
                script beginning with #! therefore wrote through the null
                initial table before it reached its first command.
        */
        if (!shell_array_room(parse_tokens, parse_token_room, parse_token_count + 1))
                return false;

        parse_token_newline(parse_tokens + parse_token_count, null);
        parse_token_count++;

        return true;
}

/* A bounded physical header copied for the ordinary lexer.  Short headers
   stay on the stack; a generated long one grows through the same shell room
   primitive as every other unbounded parser input. */
static p8 address_to parse_here_scan_line(
    string_address start, positive length, p8 address_to local_line,
    positive local_room, p8 address_to address_to mapped,
    positive address_to mapped_room)
{
        p8 address_to into = local_line;

        if (length == positive_max)
                return null;

        if (length + 1 > local_room)
        {
                if (!shell_room((address_any address_to)mapped, mapped_room,
                                length + 1, 1))
                        return null;

                into = address_to mapped;
        }

        memory_copy_end(into, start, length);
        return into;
}

/*
        Past the here-document bodies introduced by one physical command line.

        lex_nesting calls this only after seeing an unquoted << candidate in a
        command substitution.  A nested lexer/parser frame then runs the exact
        normal token-to-delimiter path above; body lines use the same quoted,
        <<- and backslash-newline decisions as parse_here_line without retaining
        a speculative body copy.  Bash also ends a document at the delimiter
        followed immediately by `)`, and leaves that `)` for the substitution
        closer.  lima dash 0.5.x does not: `EOF)` is a body line, so an
        unquoted multi-word `<<-EOF EOF` whose closer is `EOF EOF` never
        matches delimiter `EOF` and the `)` stays inside the document.
        The returned address is the source byte after the final delimiter
        line, or the terminating null when input ended in a body.  Null
        itself means allocation/token failure.
*/
static string_address parse_here_skip_bodies(string_address line,
                                              string_address newline)
{
        p8 local_line[1024];
        p8 address_to mapped = null;
        positive mapped_room = 0;
        p8 address_to copy;
        p8 address_to held_pending = parse_pending;
        positive held_pending_room = parse_pending_room;
        positive held_pending_used = parse_pending_used;
        string_address answer = null;
        string_address at = newline + 1;
        string_address header = line;
        lex_frame frame;
        bool substitution = expand_in_substitution;

        /* parse_feed's continuation buffer can already hold the containing
           multiline word.  Give this speculative logical header independent
           storage so joining it cannot overwrite the source being scanned. */
        parse_pending = null;
        parse_pending_room = 0;
        parse_pending_used = 0;
        lex_nest_enter(address_of frame);

        /* This line is command-substitution source even when its containing
           source is interactive and interactive_comments is disabled. */
        expand_in_substitution = true;
        here_filled = here_wanted;

        /* Feed every physical piece through the ordinary continuation path.
           It removes backslash-newline before tokenization, so both halves of
           << and of its delimiter meet exactly as they do in normal input. */
        while (true)
        {
                string_address header_end =
                    string_first_of_or_end(header, '\n');

                copy = parse_here_scan_line(
                    header, (positive)(header_end - header), local_line,
                    sizeof(local_line), address_of mapped,
                    address_of mapped_room);
                if (!copy || !parse_feed(copy))
                        goto leave;

                if (header_end == newline)
                        break;

                header = header_end + 1;
        }

        while (parse_here_open())
        {
                string_address line_end;
                string_address line;
                here_document address_to document;
                string_address delimiter;
                positive length;
                positive delimiter_length;

                if (!string_get(at))
                        break;

                line_end = string_first_of_or_end(at, '\n');
                length = (positive)(line_end - at);
                document = here_documents + here_filled;
                line = at;

                if (document->strip)
                {
                        positive tabs = memory_span_byte(line, '\t', length);

                        line += tabs;
                        length -= tabs;
                }

                delimiter = here_names + document->delimiter;
                delimiter_length = string_length(delimiter);

                /*
                        A here-document inside $( ) ends at a line that is
                        exactly the delimiter. Bash also ends it at the
                        delimiter followed immediately by the substitution's
                        closer, so `EOF)` is not a body line: the document
                        ends and the ) is left for lex_nesting, which is how
                        lima bash 5.2 reads

                            $(cat <<EOF
                            body
                            EOF)

                        lima dash 0.5.x keeps `EOF)` as body text. An
                        unquoted `<<-EOF EOF` is therefore delimiter EOF
                        and a cat operand, and the closer `EOF EOF` does
                        not end the document.
                */
                if (shell_bash_compat && !document->continued &&
                    delimiter_length && length > delimiter_length &&
                    string_is(line + delimiter_length, ')') &&
                    !memory_compare(line, delimiter, delimiter_length))
                {
                        parse_here_close();
                        at = line + delimiter_length;
                        break;
                }

                // The exact delimiter closes the document in the span taker.
                if (!parse_here_take_span(at, (positive)(line_end - at),
                                          false))
                        goto leave;

                if (!string_get(line_end))
                {
                        at = line_end;
                        break;
                }

                at = line_end + 1;
        }

        answer = at;

leave:
        expand_in_substitution = substitution;
        lex_nest_leave(address_of frame);
        if (parse_pending)
                memory_free(parse_pending, parse_pending_room);

        parse_pending = held_pending;
        parse_pending_room = held_pending_room;
        parse_pending_used = held_pending_used;
        if (mapped)
                memory_free(mapped, mapped_room);

        return answer;
}

static HOT b32 parse_node_new(b32 kind)
{
        b32 index;

        if (parse_node_used + 1 >= parse_node_top)
        {
                parse_state = PARSE_SYNTAX;
                return 0;
        }

        index = parse_node_used++;
        memory_fill(parse_nodes + index, 0, sizeof(parse_node));
        parse_nodes[index].kind = kind;
        parse_nodes[index].line = parse_look(0)->line;

        return index;
}

/*
        What a new word is, in one pass: whether it is NAME= or NAME+= (one
        or two in the low bits, with the name's length), and whether
        expansion leaves it as it is (four).

        The parser makes 1.36 million words reading five configure scripts,
        test/run and two ltmain.sh, and asked each two questions in two
        calls: shell_assignment_kind walked the name and shell_expand_literal
        walked the whole word again through string_span_max. Every byte a
        name is made of is one expansion leaves alone, so the walk for the
        second question starts where the name stopped, and both are in this
        body, two bytes a turn through each table. A subscript after the
        name, a[i]=v, is the lexer's to close, and goes to C.
*/
p8 parse_word_kind(string_address text, positive length, positive address_to name_length);

KEEP __attribute__((externally_visible, noinline)) p8
parse_word_kind_subscript(string_address text, positive length,
                          positive address_to name_length)
{
        return shell_assignment_kind(text, name_length) |
               (shell_expand_literal(text, length) ? 4 : 0);
}

#if X64
__asm__(
    ASM_FUNC(parse_word_kind)
    "lea string_set_name(%rip), %r8\n   xor %eax, %eax\n"
    "1:  movzbl (%rdi,%rax), %ecx\n   cmpb $0, (%r8,%rcx)\n   je 2f\n"
    "movzbl 1(%rdi,%rax), %ecx\n   cmpb $0, (%r8,%rcx)\n   je 3f\n   add $2, %rax\n   jmp 1b\n"
    "3:  inc %rax\n"
    // The name is %rax bytes and %ecx the byte after it.
    "2:  xor %r9d, %r9d\n   test %rax, %rax\n   jz 5f\n   movzbl (%rdi), %r10d\n   sub $48, %r10d\n"
    "cmp $9, %r10d\n   jbe 5f\n   cmp $91, %ecx\n   je 9f\n   cmp $61, %ecx\n   jne 4f\n   mov $1, %r9d\n   jmp 5f\n"
    "4:  cmp $43, %ecx\n   jne 5f\n   cmpb $61, 1(%rdi,%rax)\n   jne 5f\n   mov $2, %r9d\n"
    // Whether expansion leaves the rest alone too.
    "5:  mov %rax, (%rdx)\n   lea expand_literal_set(%rip), %r8\n"
    "6:  lea 1(%rax), %r10\n   cmp %rsi, %r10\n   jae 11f\n"
    "movzbl (%rdi,%rax), %r11d\n   cmpb $0, (%r8,%r11)\n   je 8f\n"
    "movzbl 1(%rdi,%rax), %r11d\n   cmpb $0, (%r8,%r11)\n   je 8f\n   add $2, %rax\n   jmp 6b\n"
    "11: cmp %rsi, %rax\n   jae 7f\n   movzbl (%rdi,%rax), %r11d\n   cmpb $0, (%r8,%r11)\n   je 8f\n"
    "7:  lea 4(%r9), %eax\n"
    ASM_RET
    // A lone [ is the test command, which expansion leaves alone.
    "8:  cmp $1, %rsi\n   jne 10f\n   cmpb $91, (%rdi)\n   jne 10f\n   or $4, %r9d\n"
    "10: mov %r9d, %eax\n"
    ASM_RET
    "9:  jmp parse_word_kind_subscript\n"
    ASM_END(parse_word_kind)
);
#elif ARM64
__asm__(
    ASM_FUNC(parse_word_kind)
    "adrp x8, string_set_name\n   add x8, x8, :lo12:string_set_name\n   mov x3, #0\n"
    "1:  ldrb w4, [x0, x3]\n   ldrb w5, [x8, x4]\n   cbz w5, 2f\n   add x3, x3, #1\n"
    "ldrb w4, [x0, x3]\n   ldrb w5, [x8, x4]\n   cbz w5, 2f\n   add x3, x3, #1\n   b 1b\n"
    // The name is x3 bytes and w4 the byte after it.
    "2:  mov w9, #0\n   cbz x3, 5f\n   ldrb w5, [x0]\n   sub w5, w5, #48\n   cmp w5, #9\n   b.ls 5f\n"
    "cmp w4, #91\n   b.eq 9f\n   cmp w4, #61\n   b.ne 4f\n   mov w9, #1\n   b 5f\n"
    "4:  cmp w4, #43\n   b.ne 5f\n   add x5, x0, x3\n   ldrb w5, [x5, #1]\n   cmp w5, #61\n   b.ne 5f\n   mov w9, #2\n"
    // Whether expansion leaves the rest alone too.
    "5:  str x3, [x2]\n   adrp x8, expand_literal_set\n   add x8, x8, :lo12:expand_literal_set\n"
    "6:  add x10, x3, #1\n   cmp x10, x1\n   b.hs 11f\n"
    "ldrb w4, [x0, x3]\n   ldrb w5, [x8, x4]\n   cbz w5, 8f\n   ldrb w4, [x0, x10]\n   ldrb w5, [x8, x4]\n   cbz w5, 8f\n"
    "add x3, x3, #2\n   b 6b\n"
    "11: cmp x3, x1\n   b.hs 7f\n   ldrb w4, [x0, x3]\n   ldrb w5, [x8, x4]\n   cbz w5, 8f\n"
    "7:  orr w0, w9, #4\n"
    ASM_RET
    // A lone [ is the test command, which expansion leaves alone.
    "8:  cmp x1, #1\n   b.ne 10f\n   ldrb w5, [x0]\n   cmp w5, #91\n   b.ne 10f\n   orr w9, w9, #4\n"
    "10: mov w0, w9\n"
    ASM_RET
    "9:  b parse_word_kind_subscript\n"
    ASM_END(parse_word_kind)
);
#elif RISCV64
__asm__(
    ASM_FUNC(parse_word_kind)
    "lla t0, string_set_name\n   li t1, 0\n"
    "1:  add t2, a0, t1\n   lbu t3, 0(t2)\n   add t4, t0, t3\n   lbu t4, 0(t4)\n   beqz t4, 2f\n"
    "addi t1, t1, 1\n   j 1b\n"
    // The name is t1 bytes and t3 the byte after it, at t2.
    "2:  li a3, 0\n   beqz t1, 5f\n   lbu t5, 0(a0)\n   addi t5, t5, -48\n   li t6, 9\n   bleu t5, t6, 5f\n"
    "li t6, 91\n   beq t3, t6, 9f\n   li t6, 61\n   bne t3, t6, 4f\n   li a3, 1\n   j 5f\n"
    "4:  li t6, 43\n   bne t3, t6, 5f\n   lbu t5, 1(t2)\n   li t6, 61\n   bne t5, t6, 5f\n   li a3, 2\n"
    // Whether expansion leaves the rest alone too.
    "5:  sd t1, 0(a2)\n   lla t0, expand_literal_set\n"
    "6:  bgeu t1, a1, 7f\n   add t2, a0, t1\n   lbu t3, 0(t2)\n   add t4, t0, t3\n   lbu t4, 0(t4)\n"
    "beqz t4, 8f\n   addi t1, t1, 1\n   j 6b\n"
    "7:  ori a0, a3, 4\n"
    ASM_RET
    // A lone [ is the test command, which expansion leaves alone.
    "8:  li t6, 1\n   bne a1, t6, 10f\n   lbu t5, 0(a0)\n   li t6, 91\n   bne t5, t6, 10f\n   ori a3, a3, 4\n"
    "10: mv a0, a3\n"
    ASM_RET
    "9:  tail parse_word_kind_subscript\n"
    ASM_END(parse_word_kind)
);
#endif

static HOT b32 parse_word_new(string_address text, positive length)
{
        positive name_length = 0;
        p8 assignment;
        p8 flags;

        if (parse_word_used + 1 >= parse_word_top)
        {
                parse_state = PARSE_SYNTAX;
                return 0;
        }

        parse_words[parse_word_used] = text;
        parse_word_rows[parse_word_used].length = length;
        expand_sets_prepare();
        assignment = parse_word_kind(text, length, address_of name_length);
        flags = assignment & 4 ? PARSE_WORD_LITERAL : 0;
        assignment &= 3;

        //      dash has no +=: NAME+=x is a command's name.
        if (assignment == 2 && shell_dash_compat)
                assignment = 0;

        if (assignment)
        {
                flags |= PARSE_WORD_ASSIGNMENT;

                if (assignment == 2)
                        flags |= PARSE_WORD_APPEND;

                if (string_is(text + name_length + assignment, '('))
                {
                        flags |= PARSE_WORD_COMPOUND;

                        if (!lex_compound_body_legal(text + name_length +
                                                     assignment))
                                parse_state = PARSE_COMPOUND_SYNTAX;
                }

                parse_word_rows[parse_word_used].name_hash =
                    memory_hash_33(text, name_length);

                if (memory_first_of(text, '\n', length))
                        flags |= PARSE_WORD_NEWLINE;
        }
        else
                parse_word_rows[parse_word_used].name_hash = 0;

        parse_word_rows[parse_word_used].name_length = name_length;
        parse_word_rows[parse_word_used].flags = flags;

        return parse_word_used++;
}

static fn parse_attach_word(b32 index, string_address text, positive length)
{
        b32 slot = parse_word_new(text, length);

        if (parse_state)
                return;

        if (!parse_nodes[index].word_count)
                parse_nodes[index].word = slot;

        parse_nodes[index].word_count++;
}

// The word the reader is standing on, put on a node and stepped past. Every
// construct that names something -- for's variable, case's subject and each
// of an item's patterns, a function, a coproc -- takes its word this way.
static HOT fn parse_take_word(b32 index)
{
        parse_attach_word(index, parse_look(0)->text, parse_look(0)->length);
        parse_position++;
}

static fn parse_skip_newlines()
{
        while (parse_look(0)->kind == PT_NEWLINE)
                parse_position++;
}

static fn parse_skip_separators()
{
        while (parse_look(0)->kind == PT_NEWLINE ||
               (parse_look(0)->kind == PT_OP && parse_look(0)->op == OP_SEMI))
                parse_position++;
}

// Running out of tokens inside a construct is not an error: it means the rest
// of it is on a line nobody has typed yet.
static COLD fn parse_fail()
{
        parse_state = parse_look(0)->kind == PT_END ? PARSE_INCOMPLETE : PARSE_SYNTAX;
}

// A word the grammar requires: taken as above when one is there, and a
// syntax error when it is not, which leaves the caller nothing to parse.
static bool parse_want_word(b32 index)
{
        if (parse_look(0)->kind != PT_WORD)
        {
                parse_fail();
                return false;
        }

        parse_take_word(index);

        return true;
}

static bool parse_expect_word_length(string_address text, positive length)
{
        if (parse_word_is_length(0, text, length))
        {
                parse_position++;
                return true;
        }

        parse_fail();
        return false;
}

#define parse_expect_word(text) \
        parse_expect_word_length((string_address)(text), sizeof(text) - 1)

static bool parse_expect_operator(b32 op)
{
        if (parse_look(0)->kind == PT_OP && parse_look(0)->op == op)
        {
                parse_position++;
                return true;
        }

        parse_fail();
        return false;
}

/*
        Where a list of commands stops.

        Every one of these is a word that only means what it says at the start
        of a command, which is exactly where a list would look for the next
        one -- so the test belongs here and nowhere else. An argument that
        happens to be spelled "done" is read as an argument, because argument
        position never asks this question.
*/
static HOT PURE bool parse_at_list_end()
{
        parse_token address_to token = parse_look(0);
        b32 keyword;

        if (token->kind == PT_END)
                return true;

        //      The three case terminators and the closing parenthesis.
        //      OP_SEMIAND and OP_DSEMIAND are the two highest operator
        //      numbers there are, so one comparison covers both.
        if (token->kind == PT_OP &&
            (token->op == OP_RPAREN || token->op == OP_DSEMI ||
             token->op >= OP_SEMIAND))
                return true;

        if (token->kind != PT_WORD)
                return false;

        keyword = parse_keyword(0);

        return keyword > PARSE_KEYWORD_OPEN &&
               keyword <= PARSE_KEYWORD_IN;
}

/*
        The reserved words, which are not names.

        Position decides whether one of these is a keyword, but nothing makes
        it a function name: "if() { ...; }" defines something that can never
        be reached, because the word in front of a command is read as the
        keyword every time. dash calls it a syntax error and so does this.
*/
static PURE bool parse_reserved(b32 ahead)
{
        return parse_keyword(ahead) != PARSE_KEYWORD_NONE;
}

static CONST bool parse_redirect_operator(b32 op)
{
        /* lex.c keeps redirect operators in three contiguous families. */
        return (op >= OP_DLESS && op <= OP_CLOBBER) ||
               (op >= OP_LESS && op <= OP_GREAT) ||
               (op >= OP_ANDGREAT && op <= OP_HERESTRING);
}

/* `{name}` or `{name[index]}` as a redirect descriptor. Dash has no such
   form, so a word that looks like one stays an operand under a dash name. */
static PURE bool parse_redirect_brace(string_address text, positive length)
{
        positive base;
        const_string subscript;
        positive subscript_length;

        if (!shell_bash_compat || length < 3 || text[0] != '{' ||
            text[length - 1] != '}')
                return false;

        text++;
        length -= 2;

        if (shell_valid_name(text, length))
                return true;

        return env_reference_element_span(text, length, address_of base,
                                          address_of subscript,
                                          address_of subscript_length) &&
               subscript_length;
}

/* A numbered redirect prefix. Dash 0.5.x only takes a single digit 0-9:
   `10>file` and `255>file` are that number as a word plus a stdout
   redirect, so `exec 255>x` is `exec 255` with stdout on x. Bash,
   including --posix, takes any fd. */
static PURE bool parse_redirect_fd_number(string_address text, positive length,
                                          positive address_to descriptor)
{
        positive parsed;

        if (!length || !string_digits_checked_exact(text, 10, address_of parsed) ||
            parsed > 0x7fffffff)
                return false;

        if (!shell_bash_compat && length != 1)
                return false;

        address_to descriptor = parsed;
        return true;
}

/* Return the number of descriptor tokens before a redirect operator, or -1.
   Alias scans and the grammar must agree on this exact two-token prefix. */
static HOT PURE b32 parse_redirect_prefix(b32 at)
{
        if (at >= (b32)parse_token_count)
                return -1;

        parse_token address_to token = parse_tokens + at;

        if (token->kind == PT_OP && parse_redirect_operator(token->op))
                return 0;

        if (at + 1 >= (b32)parse_token_count)
                return -1;

        parse_token address_to next = token + 1;

        // &> always means descriptors one and two. In "echo 2&>file", the 2
        // is therefore an argument, unlike the descriptor prefix in 2>file.
        if (token->kind != PT_WORD || next->kind != PT_OP || !next->joined ||
            next->op == OP_ANDGREAT || next->op == OP_ANDDGREAT ||
            !parse_redirect_operator(next->op))
                return -1;

        // Digits that fit a descriptor are one; bash reads a number past
        // INT_MAX in front of > as an ordinary word, so `echo a
        // 2147483648>f` writes "a 2147483648" to f. Dash recognizes only a
        // single descriptor digit.
        {
                positive parsed;

                if (string_digits_checked_exact(token->text, 10,
                                                address_of parsed) &&
                    parsed <= 0x7fffffff &&
                    (shell_bash_compat || token->length == 1))
                        return 1;
        }

        return parse_redirect_brace(token->text, token->length) ? 1 : -1;
}

/* The token after a redirect: past its descriptor prefix, the operator, and
   the operand word when there is one. The two alias scans step over
   redirects this way and have to agree with each other about how far. */
static PURE b32 parse_after_redirect(b32 at, b32 prefix)
{
        at += prefix + 1;

        if (at < (b32)parse_token_count && parse_tokens[at].kind == PT_WORD)
                at++;

        return at;
}

/*
        Replace one eligible word with the tokens of its alias value.

        This happens in the parser, not at execution. An alias may therefore
        produce a reserved word, a separator, a redirect, or several commands;
        rewriting argv after expansion cannot express any of those. The token
        text is copied into the parser's stable store before the lexer is used
        again, exactly as source-line tokens are.

        Each replacement token carries the chain which produced it. That is
        the cycle rule for both the direct `a=a` case and a chain with words or
        assignments between its names: a name already being expanded is left
        as a word instead of beginning the chain again.
*/
static bool parse_alias_replace(b32 position)
{
        parse_token address_to token = parse_tokens + position;
        parse_alias_trace address_to chain;
        parse_alias_trace address_to trace;
        // Lexing a replacement can scan nested here-documents but cannot
        // expand another alias until this replacement has been published.
        static parse_token address_to replacement;
        static positive replacement_room;
        positive replacement_count = 0;
        string_address value;
        string_address line;
        positive value_length;
        positive removed = 1;
        b32 forced;
        b32 joined;
        bool final_comment = false;
        bool trailing_blank;

        if (token->kind != PT_WORD || token->alias_forced == 2)
                return false;

        /* POSIX shells always consider aliases. Bash deliberately disables
           them in a non-interactive reader unless expand_aliases (or POSIX
           mode, which drives that same bit) is on. */
        if (shell_bash_compat && !shell_shopt_on(EXPAND_ALIASES))
                return false;

        for (chain = token->alias_trace; chain; chain = chain->next)
                if (!string_compare(token->text, chain->name))
                {
                        // Do not reconsider a cycle when an incomplete parse
                        // is walked again after another physical line arrives.
                        token->alias_forced = 2;
                        return false;
                }

        value = alias_lookup(token->text);

        if (!value)
                return false;

        trace = (parse_alias_trace address_to)shell_store_take_aligned(
            address_of parse_store, sizeof(*trace));

        if (!trace)
        {
                parse_state = PARSE_SYNTAX;
                return false;
        }

        trace->next = token->alias_trace;
        trace->name = token->text;
        forced = token->alias_forced;
        joined = token->joined;
        value_length = string_length(value);
        trailing_blank = value_length &&
                         (value[value_length - 1] == ' ' ||
                          value[value_length - 1] == '\t');
        line = value;

        while (true)
        {
                b32 count = lex_line(line);
                b32 index;
                string_address next_line;

                if (count < 0)
                {
                        parse_state = PARSE_SYNTAX;
                        goto alias_done;
                }

                if (!shell_array_room(
                        replacement, replacement_room,
                        replacement_count + (positive)count + 1))
                {
                        parse_state = PARSE_SYNTAX;
                        goto alias_done;
                }

                for (index = 0; index < count; index++)
                        if (!parse_copy_lexed(replacement + replacement_count++,
                                              index, trace))
                        {
                                parse_state = PARSE_SYNTAX;
                                goto alias_done;
                        }

                {
                        /*
                                Where the lexer stopped, which its end token
                                holds: the newline that ends this line, a
                                comment in front of it, or the end of the
                                value. A newline inside a quote was lexed as a
                                byte of a word, and cutting the value at the
                                first newline regardless put the rest of that
                                word on a line of its own.
                        */
                        positive stopped = lex_tokens[count].at;

                        next_line = string_first_of(line + stopped, '\n');

                        if (!next_line)
                                final_comment = string_is(line + stopped, '#');
                }

                line = next_line;

                if (!line)
                        break;

                if (!shell_array_room(replacement, replacement_room, replacement_count + 1))
                {
                        parse_state = PARSE_SYNTAX;
                        goto alias_done;
                }

                parse_token_newline(replacement + replacement_count, trace);
                replacement_count++;
                line++;
        }

        if (!shell_array_room(parse_tokens, parse_token_room,
                              parse_token_count + replacement_count + 1))
        {
                parse_state = PARSE_SYNTAX;
                goto alias_done;
        }

        if (final_comment)
                while (position + (b32)removed < (b32)parse_token_count &&
                       parse_tokens[position + removed].kind != PT_NEWLINE)
                        removed++;

        parse_memo_epoch++;
        memory_copy(parse_tokens + position + replacement_count,
                    parse_tokens + position + removed,
                    (parse_token_count - (positive)position - removed) *
                        sizeof(parse_tokens[0]));
        parse_token_count = parse_token_count - removed + replacement_count;

        if (replacement_count)
        {
                memory_copy(parse_tokens + position, replacement,
                            replacement_count * sizeof(replacement[0]));
                parse_tokens[position].joined = joined;
                parse_tokens[position].alias_forced = forced;
        }

        if (position + (b32)replacement_count < (b32)parse_token_count)
                parse_tokens[position + replacement_count].joined =
                    replacement_count && !trailing_blank &&
                    parse_tokens[position + replacement_count].joined;

        // A blank at the end of the value makes the next ordinary word an
        // alias candidate too. Redirect operands are not command words and
        // separators begin a fresh command under the ordinary rule.
        if (trailing_blank)
        {
                b32 at = position + (b32)replacement_count;

                while (at < (b32)parse_token_count)
                {
                        b32 prefix = parse_redirect_prefix(at);

                        if (prefix < 0 && parse_tokens[at].kind == PT_WORD)
                        {
                                parse_tokens[at].alias_forced = true;
                                break;
                        }

                        if (parse_tokens[at].kind == PT_NEWLINE ||
                            parse_tokens[at].kind == PT_END ||
                            (parse_tokens[at].kind == PT_OP &&
                             !parse_redirect_operator(parse_tokens[at].op)))
                                break;

                        if (prefix >= 0)
                        {
                                at = parse_after_redirect(at, prefix);
                                continue;
                        }

                        at++;
                }
        }

        // Here-documents introduced by an alias are first visible now. The
        // next physical line is still collected before parsing resumes.
        for (b32 at = position;
             at < position + (b32)replacement_count; at++)
                if (parse_tokens[at].kind == PT_OP &&
                    parse_tokens[at].op == OP_DLESS && !parse_here_at(at))
                {
                        parse_state = PARSE_SYNTAX;
                        break;
                }

alias_done:
        shell_room_relax((address_any address_to)address_of replacement,
                         address_of replacement_room, replacement_count,
                         sizeof(replacement[0]));

        return !parse_state;
}

// Expand the command-name candidate, skipping assignment prefixes and
// redirects. A replacement can change which token is the candidate, so the
// short scan restarts until it reaches a reserved word or a stable name.
static fn parse_alias_command()
{
        /*
                The commonest shell has no aliases at all, and the scan below
                is per command: it walks the assignment prefixes and the
                redirects of every command in the script to find the name,
                and then asks a table with nothing in it. Both helpers it
                walks with are pure, so with no alias defined the whole pass
                can only end in the same place it starts. It measured 7% of
                a script that is all commands and no aliases.
        */
        if (!alias_count)
                return;

        while (!parse_state)
        {
                b32 at = parse_position;

                while (at < (b32)parse_token_count)
                {
                        parse_token address_to token = parse_tokens + at;
                        positive name_length;

                        if (token->kind == PT_WORD &&
                            shell_assignment_kind(token->text,
                                                  address_of name_length))
                        {
                                at++;
                                continue;
                        }

                        b32 prefix = parse_redirect_prefix(at);

                        if (prefix >= 0)
                        {
                                at = parse_after_redirect(at, prefix);
                                continue;
                        }

                        if (token->kind != PT_WORD ||
                            ((!shell_bash_compat || shell_posix_on()) &&
                             parse_keyword(at - parse_position)) ||
                            (!shell_dash_compat &&
                             parse_word_is_length(at - parse_position,
                                                  "function", 8)) ||
                            !parse_alias_replace(at))
                                return;

                        break;
                }

                if (at >= (b32)parse_token_count)
                        return;
        }
}

static HOT bool parse_take_redirect(b32 index)
{
        string_address delimiter;
        string_address brace_name = null;
        positive brace_length = 0;
        b32 descriptor = -1;
        b32 op;
        b32 slot;

        if (parse_look(0)->kind == PT_WORD)
        {
                parse_token address_to prefix = parse_look(0);
                positive parsed;

                if (parse_redirect_brace(prefix->text, prefix->length))
                {
                        brace_name = prefix->text + 1;
                        brace_length = prefix->length - 2;
                        parse_position++;
                }
                else if (!parse_redirect_fd_number(prefix->text, prefix->length,
                                                   address_of parsed))

                {
                        parse_state = PARSE_SYNTAX;
                        return false;
                }
                else
                {
                        descriptor = (b32)parsed;
                        parse_position++;
                }
        }

        op = parse_look(0)->op;
        parse_position++;

        if (descriptor < 0 && !brace_length)
                descriptor = (op == OP_LESS || op == OP_DLESS ||
                              op == OP_HERESTRING ||
                              op == OP_LESSAND || op == OP_LESSGREAT)
                                 ? 0
                                 : 1;

        /*
                <<< requires a word. The digits in 2>/dev/null look like one
                because the lexer does not know this operator is still waiting,
                but they begin the next redirect, and bash then reports them
                as unexpected. An empty expansion ($empty, "") is a word; a
                missing token is not.
        */
        if (parse_look(0)->kind != PT_WORD ||
            (op == OP_HERESTRING &&
             parse_redirect_prefix(parse_position) >= 0))
        {
                //      A redirection with nothing after it on the line is not
                //      waiting for the next line, as a pipe or && is: the
                //      newline is the unexpected token.
                if (parse_look(0)->kind == PT_END)
                        parse_state = PARSE_SYNTAX;
                else
                        parse_fail();
                return false;
        }

        delimiter = parse_look(0)->text;
        bool stripped = false;

        // The dash of <<-, read here exactly as parse_feed read it when it
        // registered the delimiter. The two have to agree on how many tokens
        // the operator covers or the body and the command come apart.
        if (op == OP_DLESS && parse_look(0)->joined && string_is(delimiter, '-'))
        {
                delimiter++;
                stripped = true;

                if (!string_get(delimiter))
                {
                        parse_position++;

                        if (parse_look(0)->kind != PT_WORD)
                        {
                                parse_fail();
                                return false;
                        }

                        delimiter = parse_look(0)->text;
                }
        }

        if (parse_redirect_used + 1 >= parse_redirect_top)
        {
                parse_state = PARSE_SYNTAX;
                return false;
        }

        slot = parse_redirect_used++;
        parse_redirects[slot] = (parse_redirect){
            .op = op, .fd = descriptor, .var = brace_name,
            .var_length = brace_length, .text = delimiter,
            .text_length = string_length(delimiter), .strip = stripped};

        if (op == OP_DLESS)
        {
                if (here_taken >= here_filled)
                {
                        parse_state = PARSE_INCOMPLETE;
                        return false;
                }

                parse_redirects[slot].body = here_documents[here_taken].body;
                parse_redirects[slot].body_length =
                    here_documents[here_taken].length;
                parse_redirects[slot].raw = here_documents[here_taken].quoted;

                if (here_documents[here_taken].overflow)
                {
                        parse_state = PARSE_SYNTAX;
                        return false;
                }

                here_taken++;
        }

        parse_position++;

        if (!parse_nodes[index].redirect_count)
                parse_nodes[index].redirect = slot;

        parse_nodes[index].redirect_count++;

        return !parse_state;
}

static fn parse_take_redirects(b32 index)
{
        while (!parse_state && parse_redirect_prefix(parse_position) >= 0)
                parse_take_redirect(index);
}

static b32 parse_list();
static b32 parse_command();

// The condition of an if and the body of a loop have to contain something.
// An empty one is a script that lost a line, not a command that does nothing.
static b32 parse_list_required()
{
        b32 index = parse_list();

        if (parse_state)
                return 0;

        if (!index)
        {
                parse_fail();
                return 0;
        }

        return index;
}

static COLD __attribute__((noinline)) bool parse_compound_placed(b32 index)
{
        static string_address const declarers[] = {
            "declare", "typeset", "local", "export", "readonly", "let"};
        b32 at = parse_nodes[index].word;
        b32 stop = at + parse_nodes[index].word_count;
        bool prefix = true;
        bool declaring = false;

        for (; at < stop; at++)
        {
                p8 flags = parse_word_rows[at].flags;

                if (flags & PARSE_WORD_COMPOUND)
                {
                        if (!declaring && !(prefix && (flags & PARSE_WORD_ASSIGNMENT)))
                                return false;
                        continue;
                }

                if (prefix && (flags & PARSE_WORD_ASSIGNMENT))
                        continue;

                prefix = false;

                if (declaring || !(flags & PARSE_WORD_LITERAL))
                        continue;

                if (word_is(parse_words[at], "builtin") ||
                    word_is(parse_words[at], "command"))
                        continue;

                declaring = string_table_find(parse_words[at], declarers,
                                              sizeof(declarers[0]),
                                              array_count(declarers)) <
                            array_count(declarers);
        }

        return true;
}

static b32 parse_simple()
{
        b32 index = parse_node_new(NODE_SIMPLE);
        bool commanded = false;
        bool compound_seen = false;

        while (!parse_state)
        {
                //      After assignments alone [[ is no keyword: bash reads
                //      FOO=bar [[ x == x ]] as a command named [[, which is
                //      not found. The token holds the whole of the
                //      condition, so the whole of it is the word.
                if (parse_look(0)->kind == PT_CONDITIONAL &&
                    parse_nodes[index].word_count && !commanded &&
                    shell_bash_compat)
                        parse_tokens[parse_position].kind = PT_WORD;

                if (parse_look(0)->kind == PT_WORD &&
                    parse_look(0)->alias_forced &&
                    parse_look(0)->alias_forced != 2 &&
                    parse_alias_replace(parse_position))
                        continue;

                if (parse_redirect_prefix(parse_position) >= 0)
                {
                        parse_take_redirect(index);
                        continue;
                }

                if (parse_look(0)->kind != PT_WORD)
                        break;

                parse_take_word(index);

                if (!parse_state && parse_word_used)
                {
                        p8 flags = parse_word_rows[parse_word_used - 1].flags;

                        commanded |= !(flags & PARSE_WORD_ASSIGNMENT);
                        compound_seen |= (flags & PARSE_WORD_COMPOUND) != 0;
                }
        }

        if (parse_state)
                return 0;

        if (!parse_nodes[index].word_count && !parse_nodes[index].redirect_count)
        {
                parse_fail();
                return 0;
        }

        //      An array's list belongs to an assignment in front of the
        //      command or to the operand of a declaration: echo a=(1 2) is
        //      an echo and a parenthesis.
        if (compound_seen && !parse_compound_placed(index))
        {
                parse_fail();
                return 0;
        }

        return index;
}

/*
        if, and every elif hanging off it.

        Called with the position just past the "if" or the "elif", which is why
        the two share one function: an elif is an if whose fi belongs to
        somebody further out. The innermost one consumes it and every level
        above returns without looking.
*/
static b32 parse_if_tail()
{
        b32 index = parse_node_new(NODE_IF);

        if (parse_state)
                return 0;

        parse_want_push_for("then", index);
        parse_nodes[index].left = parse_list_required();

        if (parse_state || !parse_expect_word("then"))
                return 0;
        parse_want_pop();

        parse_want_push_for("fi", index);
        parse_nodes[index].right = parse_list_required();

        if (parse_state)
                return 0;

        if (parse_word_is(0, "elif"))
        {
                parse_position++;
                parse_nodes[index].extra = parse_if_tail();
                if (!parse_state)
                        parse_want_pop();

                return parse_state ? 0 : index;
        }

        if (parse_word_is(0, "else"))
        {
                parse_position++;
                parse_nodes[index].extra = parse_list_required();

                if (parse_state)
                        return 0;
        }

        if (!parse_expect_word("fi"))
                return 0;
        parse_want_pop();

        return index;
}

static b32 parse_do_body(b32 index)
{
        parse_want_push_for("do", index);
        if (parse_state || !parse_expect_word("do"))
                return 0;
        parse_want_pop();

        parse_want_push_for("done", index);
        parse_nodes[index].right = parse_list_required();

        if (parse_state || !parse_expect_word("done"))
                return 0;
        parse_want_pop();

        return index;
}

/* bash takes a brace group for a for or select body, `for i in a b; {
   echo $i; }` and `for ((i = 0; i < 2; i++)) { ...; }`, where do ... done
   stands in POSIX; while and until have no such form. */
static b32 parse_enclosed(b32 kind);

static b32 parse_for_body(b32 index)
{
        if (shell_bash_compat && parse_word_is(0, "{"))
        {
                parse_nodes[index].right = parse_enclosed(NODE_GROUP);

                return parse_state ? 0 : index;
        }

        return parse_do_body(index);
}

static b32 parse_loop(b32 kind)
{
        b32 index = parse_node_new(kind);

        if (parse_state)
                return 0;

        parse_position++;

        parse_nodes[index].left = parse_list_required();

        return parse_do_body(index);
}

/*
        for and select, which have one production between them.

        A select is a for that reads the item back from the operator instead
        of walking the list, so everything up to the do is the same words in
        the same places. Only the C-style head belongs to for alone.
*/
static b32 parse_for(b32 kind)
{
        b32 index = parse_node_new(kind);

        if (parse_state)
                return 0;

        parse_position++;

        if (kind == NODE_FOR && parse_look(0)->kind == PT_ARITHMETIC)
        {
                parse_nodes[index].kind = NODE_CFOR;
                parse_take_word(index);

                if (parse_look(0)->kind == PT_OP &&
                    parse_look(0)->op == OP_SEMI)
                        parse_position++;

                parse_skip_newlines();

                return parse_for_body(index);
        }

        if (!parse_want_word(index))
                return 0;

        //      dash refuses a loop variable that is no name as it reads it;
        //      bash takes it and refuses it when the loop runs.
        if (!shell_bash_compat)
        {
                parse_token address_to named = parse_look(-1);

                if (!shell_valid_name(named->text, named->length))
                {
                        parse_syntax_reason = (string_address) "Bad for loop variable";
                        parse_position--;
                        parse_state = PARSE_SYNTAX;
                        return 0;
                }
        }

        /* The linebreak production is allowed between the loop variable and
           `in` (or `do`). Without consuming it here, a perfectly ordinary
           multiline `for name\nin ...` reached the separator cleanup below
           without ever recognizing its word list. */
        parse_skip_newlines();

        // Without "in" the loop walks the positional parameters, which is a
        // different thing from walking an empty list.
        if (parse_word_is(0, "in"))
        {
                parse_nodes[index].flags = 1;
                parse_position++;

                /*
                        The list ends at the separator, not at the first word
                        spelled "do".

                        POSIX puts a semicolon or a newline between the list
                        and the do, so "do" among the words is a word like any
                        other -- and stopping at it made "for i in then do"
                        walk one item and then fail to find its own do.
                */
                //      A list is words: a=() is `a=` and then a parenthesis.
                while (parse_look(0)->kind == PT_WORD)
                {
                        parse_take_word(index);

                        if (!parse_state && parse_word_used &&
                            (parse_word_rows[parse_word_used - 1].flags &
                             PARSE_WORD_COMPOUND))
                        {
                                parse_fail();
                                return 0;
                        }
                }
        }

        parse_skip_separators();

        return parse_for_body(index);
}

static b32 parse_case()
{
        b32 index = parse_node_new(NODE_CASE);
        b32 head = 0;
        b32 tail = 0;

        if (parse_state)
                return 0;

        parse_position++;

        if (!parse_want_word(index))
                return 0;

        parse_skip_newlines();

        if (!parse_expect_word("in"))
                return 0;

        parse_skip_newlines();

        parse_want_push_for("esac", index);
        while (!parse_word_is(0, "esac"))
        {
                b32 item;

                parse_want_push_for(")", index);
                if (parse_look(0)->kind == PT_END)
                {
                        parse_state = PARSE_INCOMPLETE;
                        return 0;
                }

                item = parse_node_new(NODE_CASE_ITEM);

                if (parse_state)
                        return 0;

                if (parse_look(0)->kind == PT_OP && parse_look(0)->op == OP_LPAREN)
                        parse_position++;

                while (1)
                {
                        if (!parse_want_word(item))
                                return 0;

                        if (parse_look(0)->kind == PT_OP && parse_look(0)->op == OP_PIPE)
                        {
                                parse_position++;
                                continue;
                        }

                        break;
                }

                if (!parse_expect_operator(OP_RPAREN))
                        return 0;
                parse_want_pop();

                parse_skip_newlines();

                parse_nodes[item].right = parse_list();

                if (parse_state)
                        return 0;

                /*
                        Which of the three terminators closed the item, kept
                        on the item because the executor is the only thing
                        that can tell them apart: ;; stops, ;& runs the next
                        body without asking, and ;;& carries on asking.
                */
                if (parse_look(0)->kind == PT_OP)
                {
                        if (parse_look(0)->op == OP_SEMIAND)
                                parse_nodes[item].flags = CASE_FALL_THROUGH;
                        else if (parse_look(0)->op == OP_DSEMIAND)
                                parse_nodes[item].flags = CASE_TEST_ON;

                        if (parse_look(0)->op == OP_DSEMI ||
                            parse_look(0)->op == OP_SEMIAND ||
                            parse_look(0)->op == OP_DSEMIAND)
                                parse_position++;
                }

                parse_skip_newlines();

                if (tail)
                        parse_nodes[tail].next = item;
                else
                        head = item;

                tail = item;
        }

        parse_position++;
        parse_want_pop();
        parse_nodes[index].left = head;

        return index;
}

static b32 parse_enclosed(b32 kind)
{
        b32 index = parse_node_new(kind);

        if (parse_state)
                return 0;

        parse_position++;

        parse_want_push_for(kind == NODE_SUBSHELL ? ")" : "}", index);
        parse_nodes[index].left = parse_list_required();

        if (parse_state)
                return 0;

        if (kind == NODE_SUBSHELL)
        {
                if (!parse_expect_operator(OP_RPAREN))
                        return 0;
        }
        else if (!parse_expect_word("}"))
                return 0;
        parse_want_pop();

        return index;
}

// Whether a compound command begins at this token, which is the one thing
// that tells "coproc NAME { ... }" from "coproc command arguments".
static PURE bool parse_at_compound(b32 ahead)
{
        b32 keyword = parse_keyword(ahead);

        if ((keyword >= PARSE_KEYWORD_IF && keyword <= PARSE_KEYWORD_SELECT) ||
            keyword == PARSE_KEYWORD_OPEN)
                return true;

        return parse_look(ahead)->kind == PT_ARITHMETIC ||
               parse_look(ahead)->kind == PT_CONDITIONAL ||
               (parse_look(ahead)->kind == PT_OP &&
                parse_look(ahead)->op == OP_LPAREN);
}

/*
        Whether this word is an assignment, not a name that happens to
        contain an equals.

        `f=g()` is `f=g` followed by `()`, a syntax error near `(`. Treating
        the whole spelling as a function name installed `f=g`. Bash also
        reads `+=` that way; dash has no `+=`, so `f+=g()` is still a bad
        function name there.
*/
static PURE bool parse_word_is_assignment(b32 ahead)
{
        parse_token address_to token = parse_look(ahead);
        string_address equal;

        if (token->kind != PT_WORD || !token->length)
                return false;

        equal = memory_first_of(token->text, '=', token->length);
        if (!equal)
                return false;

        // lex_assignment_head reads NAME+ as NAME, which dash has no word for.
        if (!shell_bash_compat && equal > token->text && equal[-1] == '+')
                return false;

        return lex_assignment_head(token->text,
                                   (positive)(equal - token->text));
}

static b32 parse_function(bool keyword)
{
        b32 index = parse_node_new(NODE_FUNCTION);

        if (parse_state)
                return 0;
        if (keyword)
                parse_position++;
        if (parse_look(0)->kind != PT_WORD || parse_reserved(0))
        {
                parse_fail();
                return 0;
        }
        parse_attach_word(index, parse_look(0)->text,
                          parse_look(0)->length);
        //      Bash without posix lets a function be named a/b. Dash
        //      refuses that name in the grammar, so eval of it is a
        //      special-builtin syntax failure. Bash --posix parses the
        //      definition and the executor refuses the identifier, which
        //      ends the process even under command eval.
        if (!shell_bash_compat &&
            !shell_valid_name(parse_look(0)->text, parse_look(0)->length))
        {
                parse_fail();
                return 0;
        }
        //      dash will not have a function named for a special builtin: a
        //      Syntax error, Bad function name, that ends the script.
        if (shell_dash_compat &&
            string_table_find(
                parse_look(0)->text, shell_special_names,
                sizeof(shell_special_names[0]),
                array_count(shell_special_names)) <
                array_count(shell_special_names) &&
            string_compare(parse_look(0)->text, "source"))
        {
                parse_fail();
                return 0;
        }
        parse_position++;
        if (parse_look(0)->kind == PT_OP && parse_look(0)->op == OP_LPAREN &&
            parse_look(1)->kind == PT_OP && parse_look(1)->op == OP_RPAREN)
                parse_position += 2;
        parse_skip_newlines();
        //      dash takes any command for a body: `f() ls` and `f() g() {..}`.
        if (!shell_dash_compat && !parse_at_compound(0))
        {
                parse_fail();
                return 0;
        }
        parse_nodes[index].right = parse_command();
        return parse_state ? 0 : index;
}

/*
        coproc, and what it is going to run.

        The word behind it is a name only when a compound command follows it,
        which is the rule Bash's grammar states and the only thing that tells
        "coproc C { ... }" from "coproc cat". Without one the pair is called
        COPROC, which is the name a script that never said otherwise reads.
*/
static b32 parse_coproc()
{
        b32 index = parse_node_new(NODE_COPROC);

        if (parse_state)
                return 0;

        parse_position++;

        if (parse_look(0)->kind == PT_WORD && !parse_reserved(0) &&
            parse_at_compound(1))
                parse_take_word(index);
        else
                parse_attach_word(index, (string_address) "COPROC", 6);

        if (parse_state)
                return 0;

        parse_nodes[index].left = parse_command();

        return parse_state ? 0 : index;
}

/*
        How deep one command may sit inside others.

        A group, a subshell, an if, a loop, a case arm and a function body
        each parse the commands inside them by calling parse_command again,
        so the depth of a script is the depth of the recursion, and a script
        of 32,000 nested braces or ifs -- a 200 KB file -- ran the process
        off the end of its stack. Bash refuses somewhere past two thousand
        (its parser stack is 10,000 deep) and answers a syntax error; so
        does this, at four thousand, which no script written by a person
        approaches.
*/
#define PARSE_NESTING 4096

static b32 parse_command_body();

static b32 parse_command()
{
        b32 index;

        if (parse_depth >= PARSE_NESTING)
        {
                parse_fail();
                return 0;
        }

        parse_depth++;
        index = parse_command_body();
        parse_depth--;
        return index;
}

static HOT b32 parse_command_body()
{
        b32 index;
        b32 compound = true;

        parse_alias_command();

        if (parse_state)
                return 0;

        if (!shell_dash_compat && parse_word_is(0, "function"))
                return parse_function(true);

        if (parse_look(0)->kind == PT_ARITHMETIC ||
            parse_look(0)->kind == PT_CONDITIONAL)
        {
                index = parse_node_new(parse_look(0)->kind == PT_ARITHMETIC
                                           ? NODE_ARITHMETIC : NODE_CONDITIONAL);
                if (!parse_state)
                        parse_take_word(index);
                goto command_done;
        }

        b32 keyword = parse_keyword(0);

        if (parse_look(0)->kind == PT_WORD && keyword == PARSE_KEYWORD_NONE &&
            parse_look(1)->kind == PT_OP && parse_look(1)->op == OP_LPAREN &&
            parse_look(2)->kind == PT_OP && parse_look(2)->op == OP_RPAREN &&
            !parse_word_is_assignment(0))
                return parse_function(false);

        if (keyword == PARSE_KEYWORD_IF)
        {
                parse_position++;
                index = parse_if_tail();
        }
        else if (keyword == PARSE_KEYWORD_WHILE)
                index = parse_loop(NODE_WHILE);
        else if (keyword == PARSE_KEYWORD_UNTIL)
                index = parse_loop(NODE_UNTIL);
        else if (keyword == PARSE_KEYWORD_FOR)
                index = parse_for(NODE_FOR);
        else if (keyword == PARSE_KEYWORD_SELECT)
                index = parse_for(NODE_SELECT);
        else if (keyword == PARSE_KEYWORD_COPROC)
                index = parse_coproc();
        else if (keyword == PARSE_KEYWORD_CASE)
                index = parse_case();
        else if (keyword == PARSE_KEYWORD_OPEN)
                index = parse_enclosed(NODE_GROUP);
        else if (parse_look(0)->kind == PT_OP && parse_look(0)->op == OP_LPAREN)
                index = parse_enclosed(NODE_SUBSHELL);
        else if (keyword == PARSE_KEYWORD_NONE ||
                 keyword == PARSE_KEYWORD_TIME)
        {
                //      `time` after `|` is an ordinary name: lima runs the
                //      utility, because a bang is the only reserved word the
                //      pipeline production itself consumes. At the start of
                //      a pipeline parse_pipeline has already taken `time`.
                compound = false;
                index = parse_simple();
        }
        else
        {
                //      then/do/in/fi, a bang after `|`, and bash `]]` are
                //      reserved here. lima reports a syntax error, not
                //      command-not-found. Bang at the start of a pipeline is
                //      consumed above this.
                parse_fail();
                return 0;
        }

command_done:
        if (parse_state)
                return 0;

        if (compound)
                parse_take_redirects(index);

        return parse_state ? 0 : index;
}

/*
        The 2>&1 that |& adds to the command in front of the pipe.

        Bash puts it behind whatever redirections the command wrote for
        itself, so a command that sent its errors elsewhere has them brought
        back to the pipe rather than the other way about. Appending is what
        that means here: the redirections of one command are one run, and
        parse_redirect_used is still standing at the end of it.
*/
static bool parse_merge_streams(b32 index)
{
        b32 slot;

        if (parse_redirect_used + 1 >= parse_redirect_top)
        {
                parse_state = PARSE_SYNTAX;
                return false;
        }

        slot = parse_redirect_used++;
        parse_redirects[slot] = (parse_redirect){
            .op = OP_GREATAND, .fd = 2, .text = (string_address) "1",
            .text_length = 1};

        if (!parse_nodes[index].redirect_count)
                parse_nodes[index].redirect = slot;

        parse_nodes[index].redirect_count++;

        return true;
}

static PURE inline INLINE bool parse_at_pipe()
{
        return parse_look(0)->kind == PT_OP &&
               (parse_look(0)->op == OP_PIPE ||
                parse_look(0)->op == OP_PIPEAND);
}

static b32 parse_pipeline(bool inverted);

static PURE bool parse_time_reserved()
{
        /* POSIX and dash treat time as an ordinary command name. Bash,
           including --posix, reserves it in pipeline position. */
        if (!shell_bash_compat)
                return false;

        if (parse_look(0)->kind != PT_WORD ||
            parse_look(0)->length != 4 ||
            parse_keyword(0) != PARSE_KEYWORD_TIME)
                return false;

        /* In Bash POSIX mode, a following option-shaped word makes `time`
           an ordinary command name. This is what lets a function or the
           external POSIX time utility receive -p itself. */
        return !(shell_posix_on() &&
                 parse_look(1)->kind == PT_WORD &&
                 string_is(parse_look(1)->text, '-'));
}

/*
        time, and what it is put in front of.

        The whole of a pipeline is timed, so time takes one and everything
        that may stand in front of a pipeline may stand behind a time: a bang,
        and another time. -p asks for the three POSIX lines instead of
        TIMEFORMAT, and time with nothing behind it times the null command,
        which is how a script asks what the shell has used so far.
*/
static b32 parse_time(bool inverted)
{
        b32 index = parse_node_new(NODE_TIME);

        if (parse_state)
                return 0;

        //      Bit 1 is -p and bit 2 the bang: the connector that joins this
        //      node to its neighbour in an and-or list lives in op, and
        //      `a && time b` took it for a bang.
        parse_nodes[index].flags = inverted ? 2 : 0;

        //      Bash marks the command it times rather than wrapping it, so a
        //      time in front of a time times once and not twice.
        while (parse_time_reserved())
        {
                parse_position++;

                if (parse_word_is(0, "-p"))
                {
                        parse_nodes[index].flags |= 1;
                        parse_position++;
                }

                //      Bash ends the options here and reports the POSIX
                //      three lines whether or not -p was among them.
                if (parse_word_is(0, "--"))
                {
                        parse_nodes[index].flags |= 1;
                        parse_position++;
                        break;
                }
        }

        //      Nothing to time is not a missing command: a separator, the end
        //      of a list, or the end of the input all end the construct here
        //      and the null command is what gets measured.
        if (parse_look(0)->kind == PT_NEWLINE || parse_at_list_end() ||
            (parse_look(0)->kind == PT_OP &&
             (parse_look(0)->op == OP_SEMI || parse_look(0)->op == OP_AMP)))
                return index;

        parse_nodes[index].left = parse_pipeline(false);

        return parse_state ? 0 : index;
}

static HOT b32 parse_pipeline(bool inverted)
{
        if (parse_state)
                return 0;

        parse_alias_command();

        if (parse_state)
                return 0;

        /*
                time is four bytes long, and every command in the script comes
                through here. Asking the classifier about each of them to find
                that out costs more than the length does, and this is the one
                place a keyword is looked for before the command is read.
        */
        if (parse_time_reserved())
                return parse_time(inverted);

        if (!inverted && parse_word_is(0, "!"))
        {
                inverted = true;
                parse_position++;
                parse_skip_newlines();
                parse_alias_command();

                // The grammar has one optional Bang, not a repeatable list.
                // dash rejects a second one rather than cancelling the first.
                if (parse_word_is(0, "!"))
                {
                        parse_state = PARSE_SYNTAX;
                        return 0;
                }

                // A time behind the bang is a whole pipeline of its own, and
                // what the bang inverts is the answer it gives.
                if (parse_time_reserved())
                        return parse_time(true);
        }

        b32 head = parse_command();

        if (parse_state)
                return 0;

        bool piped = parse_at_pipe();

        // The grammar level carries no information in the overwhelmingly
        // common singleton case. Do not put a node in the executor merely to
        // rediscover that fact on every iteration of a loop.
        if (!inverted && !piped)
                return head;

        b32 index = parse_node_new(NODE_PIPELINE);
        b32 tail = head;

        if (parse_state)
                return 0;

        parse_nodes[index].flags = inverted;

        while (piped)
        {
                if (parse_look(0)->op == OP_PIPEAND &&
                    !parse_merge_streams(tail))
                        return 0;

                parse_position++;
                parse_skip_newlines();

                b32 child = parse_command();

                if (parse_state)
                        return 0;

                parse_nodes[tail].next = child;
                tail = child;
                piped = parse_at_pipe();
        }

        parse_nodes[index].left = head;

        return index;
}

static b32 parse_and_or()
{
        if (parse_state)
                return 0;

        b32 head = parse_pipeline(false);

        if (parse_state)
                return 0;

        bool joined = parse_look(0)->kind == PT_OP &&
                      (parse_look(0)->op == OP_AND_IF ||
                       parse_look(0)->op == OP_OR_IF);

        if (!joined)
                return head;

        b32 index = parse_node_new(NODE_ANDOR);
        b32 tail = head;

        if (parse_state)
                return 0;

        while (joined)
        {
                b32 op = parse_look(0)->op;

                parse_position++;
                parse_skip_newlines();

                b32 child = parse_pipeline(false);

                if (parse_state)
                        return 0;

                parse_nodes[child].op = op;
                parse_nodes[tail].next = child;
                tail = child;
                joined = parse_look(0)->kind == PT_OP &&
                         (parse_look(0)->op == OP_AND_IF ||
                          parse_look(0)->op == OP_OR_IF);
        }

        parse_nodes[index].left = head;

        return index;
}

/* Whether the list that stopped last stopped because the tokens ran out. */
static bool parse_list_ran_out;

static HOT b32 parse_list()
{
        b32 index = 0;
        b32 head = 0;
        b32 tail = 0;
        b32 start = parse_position;
        b32 here_start = (b32)here_taken;

        if (parse_state)
                return 0;

        if (parse_memo_on && (positive)start < parse_memo_room &&
            parse_memos[start].epoch == parse_memo_epoch &&
            parse_memos[start].here_start == here_start)
        {
                parse_memo address_to memo = parse_memos + start;

                index = memo->index;
                head = memo->head;
                tail = memo->tail;
                parse_position = memo->at;
                here_taken = (positive)memo->here_at;
        }
        else
        {
                parse_skip_newlines();
                parse_alias_command();

                // A semicolon separates two commands; it cannot stand where no
                // command precedes it. Treating it like a blank line made `;` a
                // successful empty program and accepted repeated separators.
                if (parse_look(0)->kind == PT_OP && parse_look(0)->op == OP_SEMI)
                {
                        parse_state = PARSE_SYNTAX;
                        return 0;
                }
        }

        while (!parse_at_list_end())
        {
                b32 child = parse_and_or();
                bool separated = false;

                if (parse_state)
                        return 0;

                if (parse_look(0)->kind == PT_OP && parse_look(0)->op == OP_AMP)
                {
                        /* A direct singleton may use flags for its own node
                           kind. Background state belongs to an and-or list,
                           so restore that grammar node only for this case. */
                        if (parse_nodes[child].kind != NODE_ANDOR)
                        {
                                b32 wrapper = parse_node_new(NODE_ANDOR);

                                if (parse_state)
                                        return 0;

                                parse_nodes[wrapper].left = child;
                                child = wrapper;
                        }

                        parse_nodes[child].flags = 1;
                        parse_position++;
                        separated = true;
                }
                else if (parse_look(0)->kind == PT_OP && parse_look(0)->op == OP_SEMI)
                {
                        parse_position++;
                        separated = true;
                }
                else if (parse_look(0)->kind == PT_NEWLINE)
                {
                        parse_position++;
                        separated = true;
                }

                if (tail)
                {
                        if (!index)
                        {
                                index = parse_node_new(NODE_LIST);

                                if (parse_state)
                                        return 0;

                                parse_nodes[index].left = head;
                        }

                        parse_nodes[tail].next = child;
                }
                else
                        head = child;

                tail = child;

                if (!separated)
                        break;

                parse_skip_newlines();
                parse_alias_command();

                if (parse_look(0)->kind == PT_OP && parse_look(0)->op == OP_SEMI)
                {
                        parse_state = PARSE_SYNTAX;
                        return 0;
                }

                // Where the next command begins, for the next parse of the
                // same tokens, while two more are there to have looked at.
                if (parse_memo_on && (positive)parse_position + 2 < parse_token_count &&
                    shell_array_room(parse_memos, parse_memo_room, (positive)start + 1))
                        parse_memos[start] = (parse_memo){
                            parse_memo_epoch, index, head, tail, parse_position,
                            here_start, (b32)here_taken};
        }

        parse_list_ran_out = parse_look(0)->kind == PT_END;

        if (!head)
                return 0;

        return index ? index : head;
}

// Everything read so far, as one tree. Zero with parse_state set to
// PARSE_INCOMPLETE means the source stops in the middle of a construct and the
// caller should ask for another line rather than complain.
HOT b32 parse_program()
{
        b32 root;

        // Half a word is not a program, and the reader is the one who has to
        // be told: there is another line to ask for.
        if (parse_pending_used)
        {
                parse_state = PARSE_INCOMPLETE;
                return 0;
        }

        parse_want_used = 0;

        if (!parse_node_top && !parse_arenas())
        {
                parse_state = PARSE_SYNTAX;
                return 0;
        }

        // The last parse ran out of these same tokens and left the arenas
        // at least half free: begin past it, and reuse what it finished.
        bool reuse = parse_context.memo_open && !alias_count &&
                     parse_context.memo_epoch == parse_memo_epoch &&
                     parse_context.memo_nodes - parse_node_base < (parse_node_top - parse_node_base) / 2 &&
                     parse_context.memo_words - parse_word_base < (parse_word_top - parse_word_base) / 2 &&
                     parse_context.memo_redirects - parse_redirect_base <
                         (parse_redirect_top - parse_redirect_base) / 2;

again:
        if (!reuse)
                parse_memo_epoch++;
        parse_memo_on = !alias_count;
        parse_position = (b32)parse_token_base;
        parse_state = PARSE_OK;
        parse_node_used = reuse ? parse_context.memo_nodes : parse_node_base;
        parse_word_used = reuse ? parse_context.memo_words : parse_word_base;
        parse_redirect_used = reuse ? parse_context.memo_redirects : parse_redirect_base;
        here_taken = 0;
        shell_parse_generation++;
        parse_list_ran_out = false;

        root = parse_list();

        // A mistake is told by a parse of its own, word for word as before.
        if (reuse && parse_state != PARSE_INCOMPLETE &&
            (parse_state || parse_look(0)->kind != PT_END))
        {
                reuse = false;
                goto again;
        }

        parse_memo_on = false;
        parse_context.memo_open = parse_state == PARSE_INCOMPLETE;
        parse_context.memo_epoch = parse_memo_epoch;
        parse_context.memo_nodes = parse_node_used;
        parse_context.memo_words = parse_word_used;
        parse_context.memo_redirects = parse_redirect_used;

        parse_open_list = parse_state == PARSE_INCOMPLETE && parse_list_ran_out;
        parse_context.open_nodes = 0;
        parse_context.open_words = 0;
        parse_context.open_redirects = 0;

        if (parse_state)
                return 0;

        if (parse_look(0)->kind != PT_END)
        {
                parse_state = PARSE_SYNTAX;
                return 0;
        }

        return root;
}

/*
        Whether the line just fed can only carry on the list the last parse
        ran out of tokens in, so that parsing everything again would run out
        in the same place and say nothing new.

        The parser starts from the first token of a construct every time, so
        a function or an if block of n lines was parsed n times over, which
        made reading one quadratic in its length: a function of 150 lines
        took 6.6 million cycles to read, where a flat script of the same
        lines took under one. The last parse stopped inside a list, and a
        line that is only simple commands -- words that are not reserved and
        not a=(...), redirections with their words, and ; & | && || |& each
        after a word -- can only put more commands in that list, which then
        runs out of tokens where it did before. Anything else is parsed as it
        was, as is every line while an alias is defined, since an alias can
        become any of those. So is the line that could take the last free
        node, word or redirection, because running out of those is a syntax
        error that has to be told on the line that caused it: each skipped
        line counts the most it could take.
*/
static bool parse_line_continues_list(positive from)
{
        positive words = 0, redirects = 0, controls = 0;
        b32 last = 0;
        b32 held = parse_position;

        if (!parse_open_list || parse_state != PARSE_INCOMPLETE ||
            parse_pending_used || alias_count)
                return false;

        for (positive at = from; at < parse_token_count; at++)
        {
                parse_token address_to token = parse_tokens + at;

                if (token->kind == PT_WORD)
                {
                        positive name_length = 0;
                        p8 assignment = shell_assignment_kind(token->text,
                                                              address_of name_length);
                        b32 keyword;

                        parse_position = (b32)at;
                        keyword = parse_keyword(0);
                        parse_position = held;

                        if (keyword ||
                            (!shell_dash_compat && token->length == 8 &&
                             !memory_compare(token->text, "function", 8)) ||
                            (assignment &&
                             string_is(token->text + name_length + assignment, '(')))
                                return false;
                        words++;
                        last = 1;
                }
                else if (token->kind == PT_OP &&
                         (token->op == OP_AND_IF || token->op == OP_OR_IF ||
                          token->op == OP_SEMI || token->op == OP_PIPE ||
                          token->op == OP_AMP || token->op == OP_PIPEAND))
                {
                        if (last != 1)
                                return false;
                        controls++;
                        last = 3;
                }
                else if (token->kind == PT_OP && token->op != OP_DLESS &&
                         (token->op == OP_DGREAT || token->op == OP_LESSAND ||
                          token->op == OP_GREATAND || token->op == OP_LESSGREAT ||
                          token->op == OP_CLOBBER || token->op == OP_LESS ||
                          token->op == OP_GREAT || token->op == OP_ANDGREAT ||
                          token->op == OP_ANDDGREAT || token->op == OP_HERESTRING))
                {
                        if (last == 2)
                                return false;
                        redirects++;
                        last = 2;
                }
                else if (token->kind == PT_NEWLINE)
                {
                        if (last == 2)
                                return false;
                        last = 0;
                }
                else
                        return false;
        }

        if (last == 2)
                return false;

        parse_context.open_nodes += 3 * (words + redirects + controls) + 3;
        parse_context.open_words += words;
        parse_context.open_redirects += redirects;

        if (parse_node_used + parse_context.open_nodes + 2 >= parse_node_top ||
            parse_word_used + parse_context.open_words + 2 >= parse_word_top ||
            parse_redirect_used + parse_context.open_redirects + 2 >= parse_redirect_top)
                return false;

        return true;
}

/* Retained bodies own independent ranges in the existing arenas. Occupancy
   maps make adjacent free ranges one gap without moving a live node or word.
   A definition and every active call each hold the immutable body, so unset
   or replacement cannot discard the tail that an older call still walks. */
typedef struct
{
        b32 start[4], count[4];
        positive references;
} parse_kept_body;

static parse_kept_body address_to parse_kept_bodies;
static struct
{
        b8 address_to occupied;
        b32 room;
} parse_kept_arenas[] = {
    {null, PARSE_NODES}, {null, PARSE_WORDS},
    {null, PARSE_REDIRECTS}, {null, PARSE_KEPT_TEXT},
};

/* Every array above, in one mapping the kernel fills a page at a time: 51
   MiB of address space for 262,144 nodes and words, 65,536 redirections and
   8 MiB of kept text, a list of some eighty thousand commands, where bash
   and dash both run out of stack at a hundred thousand. MAP_NORESERVE keeps
   the address space from being charged as memory. */
#define PARSE_MAP_NORESERVE 0x4000

static bool parse_arenas()
{
        positive sizes[] = {
            PARSE_NODES * sizeof(parse_node), PARSE_NODES * sizeof(parse_kept_body),
            PARSE_WORDS * sizeof(string_address),
            PARSE_WORDS * sizeof(parse_word_row),
            PARSE_REDIRECTS * sizeof(parse_redirect), PARSE_KEPT_TEXT,
            PARSE_NODES, PARSE_WORDS, PARSE_REDIRECTS, PARSE_KEPT_TEXT,
        };
        address_any address_to places[] = {
            (address_any address_to)address_of parse_nodes,
            (address_any address_to)address_of parse_kept_bodies,
            (address_any address_to)address_of parse_words,
            (address_any address_to)address_of parse_word_rows,
            (address_any address_to)address_of parse_redirects,
            (address_any address_to)address_of parse_kept_text,
            (address_any address_to)address_of parse_kept_arenas[0].occupied,
            (address_any address_to)address_of parse_kept_arenas[1].occupied,
            (address_any address_to)address_of parse_kept_arenas[2].occupied,
            (address_any address_to)address_of parse_kept_arenas[3].occupied,
        };
        positive total = 0;
        bipolar mapped;

        for (positive i = 0; i < array_count(sizes); i++)
                total += (sizes[i] + 4095) & ~(positive)4095;
        mapped = system_call_6(syscall(mmap), 0, total,
                               FILE_PROTECT_READ | FILE_PROTECT_WRITE,
                               FILE_MAP_PRIVATE | FILE_MAP_ANONYMOUS | PARSE_MAP_NORESERVE,
                               (positive)-1, 0);
        if (mapped < 0 && mapped > -4096)
                return false;
        /* Ordinary pages, as lib.util.c asks for the bss and for what it
           says: a command touches the first page or two of each of these
           arrays, and a kernel that has transparent huge pages at "always"
           answers every one of those first touches with two megabytes of
           zeroed memory. Five arrays are five of them: `echo hi` spent 60 of
           its 150 microseconds on it, and the one call (a model of the same
           five touches in a plain C program: 120 microseconds with the
           default, 54 with this) is below what a syscall measures. */
        system_call_3(syscall(madvise), (positive)mapped, total, 15);
        for (positive i = 0; i < array_count(sizes); i++)
        {
                *places[i] = (address_any)mapped;
                mapped += (bipolar)((sizes[i] + 4095) & ~(positive)4095);
        }
        parse_node_top = PARSE_NODES;
        parse_word_top = PARSE_WORDS;
        parse_redirect_top = PARSE_REDIRECTS;
        return true;
}

static fn parse_kept_mark(parse_kept_body address_to body, p8 occupied)
{
        for (positive i = 0; i < array_count(parse_kept_arenas); i++)
                memory_fill(parse_kept_arenas[i].occupied + body->start[i],
                            occupied, body->count[i]);
}

/* The lowest kept slot of each arena, found from a slot nothing below is
   kept at: the old frontier, or a new body's start where that is lower. The
   walk then covers the kept end and not the whole reservation, which would
   touch a page of the map for every 4096 slots nobody ever used. */
static fn parse_kept_frontiers(const b32 address_to from)
{
        b32 address_to tops[] = {address_of parse_node_top, address_of parse_word_top,
                                 address_of parse_redirect_top};

        for (positive i = 0; i < array_count(tops); i++)
                *tops[i] = from[i] + (b32)memory_span_byte(
                    parse_kept_arenas[i].occupied + from[i], 0,
                    parse_kept_arenas[i].room - from[i]);
}

static fn parse_release(b32 index)
{
        if (!index || --parse_kept_bodies[index].references)
                return;
        parse_kept_body address_to body = parse_kept_bodies + index;
        parse_kept_mark(body, 0);
        // Compound-assignment lookup scans the retained word region. A free
        // word must not masquerade as text later reused by another body.
        memory_fill(parse_words + body->start[1], 0,
                    body->count[1] * sizeof(parse_words[0]));
        b32 from[] = {parse_node_top, parse_word_top, parse_redirect_top};
        parse_kept_frontiers(from);
}

static bool parse_keep_amount(parse_kept_body address_to body, positive arena,
                              positive amount)
{
        if (amount > (positive)(parse_kept_arenas[arena].room - body->count[arena]))
                return false;
        body->count[arena] += (b32)amount;
        return true;
}

/* Both walks follow a list's commands through next in a loop and recurse
   only into the three children, so a body of a hundred thousand commands in
   one list is as deep on the stack as one command. */
static bool parse_keep_measure(b32 index, parse_kept_body address_to body)
{
        for (; index; index = parse_nodes[index].next)
        {
                parse_node address_to node = parse_nodes + index;
                if (!parse_keep_amount(body, 0, 1) ||
                    !parse_keep_amount(body, 1, node->word_count) ||
                    !parse_keep_amount(body, 2, node->redirect_count))
                        return false;
                for (b32 i = 0; i < node->word_count; i++)
                {
                        positive length = parse_word_rows[node->word + i].length;
                        if (length >= PARSE_KEPT_TEXT || !parse_keep_amount(body, 3, length + 1))
                                return false;
                }
                for (b32 i = 0; i < node->redirect_count; i++)
                {
                        parse_redirect address_to redirect = parse_redirects + node->redirect + i;
                        if (redirect->text_length >= PARSE_KEPT_TEXT ||
                            !parse_keep_amount(body, 3, redirect->text_length + 1) ||
                            (redirect->var_length &&
                             (redirect->var_length >= PARSE_KEPT_TEXT ||
                              !parse_keep_amount(body, 3, redirect->var_length + 1))) ||
                            (redirect->body_length && (redirect->body_length >= PARSE_KEPT_TEXT ||
                             !parse_keep_amount(body, 3, redirect->body_length + 1))))
                                return false;
                }
                if (!parse_keep_measure(node->left, body) ||
                    !parse_keep_measure(node->right, body) ||
                    !parse_keep_measure(node->extra, body))
                        return false;
        }
        return true;
}

// Choose the highest fitting gap, leaving the low end for transient parsing.
static b32 parse_keep_reserve(positive arena, b32 count, b32 floor)
{
        if (!count)
                return 0;
        b8 address_to occupied = parse_kept_arenas[arena].occupied;
        b32 room = parse_kept_arenas[arena].room;
        for (b32 at = room; at - floor >= count;)
        {
                b8 address_to last = memory_last_of(occupied + floor, 0, at - floor);
                if (!last)
                        break;
                at = (b32)(last - occupied) + 1;
                if (at - floor < count)
                        break;
                b32 chosen = at - count;
                b8 address_to used = memory_first_of(occupied + chosen, 1, count);
                if (!used)
                {
                        memory_fill(occupied + chosen, 1, count);
                        return chosen;
                }
                // Every higher candidate includes this occupied byte.
                at = (b32)(used - occupied);
        }
        return -1;
}

static string_address parse_keep_text(b32 address_to cursor, string_address text,
                                      positive length)
{
        string_address kept = parse_kept_text + cursor[3];
        memory_copy_end(kept, text, length);
        cursor[3] += (b32)length + 1;
        return kept;
}

/* Measurement and all four reservations precede this copy. It cannot fail,
   and its source is either below the transient frontier or held by a call. */
static b32 parse_keep_tree(b32 index, b32 address_to cursor)
{
        b32 first = 0, previous = 0;

        for (; index; index = parse_nodes[index].next)
        {
                b32 copy = cursor[0]++;
                parse_node address_to from = parse_nodes + index;
                parse_node address_to into = parse_nodes + copy;
                *into = *from;
                if (from->word_count)
                        into->word = cursor[1];
                for (b32 i = 0; i < from->word_count; i++)
                {
                        b32 source = from->word + i;
                        b32 target = cursor[1]++;
                        parse_words[target] = parse_keep_text(cursor, parse_words[source],
                                                               parse_word_rows[source].length);
                        parse_word_rows[target].length = parse_word_rows[source].length;
                        parse_word_rows[target].name_length = parse_word_rows[source].name_length;
                        parse_word_rows[target].name_hash = parse_word_rows[source].name_hash;
                        parse_word_rows[target].flags = parse_word_rows[source].flags;
                }
                if (from->redirect_count)
                        into->redirect = cursor[2];
                for (b32 i = 0; i < from->redirect_count; i++)
                {
                        parse_redirect address_to source = parse_redirects + from->redirect + i;
                        parse_redirect address_to target = parse_redirects + cursor[2]++;
                        *target = *source;
                        target->text = parse_keep_text(cursor, source->text, source->text_length);
                        if (source->var_length)
                                target->var = parse_keep_text(cursor, source->var,
                                                              source->var_length);
                        if (source->body_length)
                        {
                                target->body = cursor[3];
                                parse_keep_text(cursor, (source->kept ? parse_kept_text : here_text) +
                                                source->body, source->body_length);
                                target->kept = true;
                        }
                }
                into->left = parse_keep_tree(from->left, cursor);
                into->right = parse_keep_tree(from->right, cursor);
                into->extra = parse_keep_tree(from->extra, cursor);
                into->next = 0;
                if (previous)
                        parse_nodes[previous].next = copy;
                else
                        first = copy;
                previous = copy;
        }
        return first;
}

/* A uniquely held old body can contribute its space without risking a
   partial overwrite: reserve every destination before clearing or copying.
   A failed reservation restores its occupancy and leaves the definition live. */
static b32 parse_keep(b32 index, b32 replaced)
{
        parse_kept_body made = {0};
        if (!index || !parse_keep_measure(index, &made))
                return 0;
        parse_kept_body previous = parse_kept_bodies[replaced];
        bool reuse = replaced && previous.references == 1;
        if (reuse)
                parse_kept_mark(&previous, 0);
        b32 floors[] = {parse_node_used + 1, parse_word_used + 1,
                        parse_redirect_used + 1, 0};
        positive arena = 0;
        for (; arena < array_count(parse_kept_arenas); arena++)
        {
                made.start[arena] = parse_keep_reserve(arena, made.count[arena], floors[arena]);
                if (made.start[arena] < 0)
                        break;
        }
        if (arena < array_count(parse_kept_arenas))
        {
                while (arena--)
                        memory_fill(parse_kept_arenas[arena].occupied + made.start[arena],
                                    0, made.count[arena]);
                if (reuse)
                        parse_kept_mark(&previous, 1);
                return 0;
        }
        if (reuse)
        {
                memory_fill(parse_words + previous.start[1], 0,
                            previous.count[1] * sizeof(parse_words[0]));
                parse_kept_bodies[replaced].references = 0;
        }
        else
                parse_release(replaced);
        b32 cursor[4];
        memory_copy(cursor, made.start, sizeof(cursor));
        b32 copy = parse_keep_tree(index, cursor);
        made.references = 1;
        parse_kept_bodies[copy] = made;
        b32 from[] = {parse_node_top, parse_word_top, parse_redirect_top};
        for (positive i = 0; i < array_count(from); i++)
                if (made.count[i] && made.start[i] < from[i])
                        from[i] = made.start[i];
        parse_kept_frontiers(from);
        return copy;
}
