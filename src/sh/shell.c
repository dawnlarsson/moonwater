/*
        The shell.

        Parsing, expansion, redirection and dispatch. The commands themselves
        are in builtin.c beside this, and programs/shell.c is the four lines
        that start it -- so the same core can be driven by something that is
        not a program, which is what an in-kernel console would need.
*/

/*
        Control-C cancels the command, not the shell.

        The line discipline sends SIGINT to everything in the terminal's
        foreground group, which is this and whatever it is running. Ignoring it
        here leaves the shell standing; every command it runs is given its own
        default disposition back first, whether it is spawned or is a builtin
        in a fork of this process.

        SIG_IGN needs no restorer, which is the whole reason this is three
        words and not a per-architecture trampoline.
*/
#define SIGNAL_INTERRUPT 2
#define SIGNAL_QUIT 3
#define SIGNAL_PIPE 13
#define SIGNAL_STOP_KEY 20
#define SIGNAL_TTY_INPUT 21
#define SIGNAL_TTY_OUTPUT 22
#define SIGNAL_IGNORE 1
#define SIGNAL_DEFAULT 0

#define SIGNAL_RESTART 0x10000000
#define SIGNAL_RESTORER 0x04000000

fn shell_signal(b32 number, positive disposition)
{
        system_signal_install(number, disposition, 0, 0, null);
}

#define shell_ignore(n) shell_signal(n, SIGNAL_IGNORE)
#define shell_default(n) shell_signal(n, SIGNAL_DEFAULT)

/*
        What was already ignored when this shell started.

        A non-interactive shell that inherits SIG_IGN for a signal keeps
        ignoring it: a trap on it does nothing, and neither does giving it
        back. The shell that started this one decided, and a script must not
        be able to undo that decision from the inside -- which is what makes a
        command run under nohup, or in the background, stay uninterruptible
        however it sets its own traps.

        Asked before this shell installs a disposition of its own, because
        that is the only moment the answer is about what was inherited rather
        than about what we just did.
*/
positive shell_signals_ignored;
static positive shell_signals_known HOT_STATE;
// Start left to the options: see programs/shell.c.
bool shell_signals_deferred HOT_STATE;

static HOT bool shell_signal_was_ignored(b32 number)
{
        positive mask;

        if (number <= 0 || number >= (b32)positive_bits)
                return false;

        mask = (positive)1 << number;

        if (!(shell_signals_known & mask))
        {
                positive action[4] = {0, 0, 0, 0};

                if (system_signal_action(number, 0, address_of action, 8) >= 0 &&
                    action[0] == SIGNAL_IGNORE)
                        shell_signals_ignored |= mask;

                shell_signals_known |= mask;
        }

        return (shell_signals_ignored & mask) != 0;
}

#define shell_was_ignored(n) shell_signal_was_ignored((b32)(n))

HOT fn shell_signals_start()
{
        positive ignored[4] = {SIGNAL_IGNORE, 0, 0, 0};
        b32 numbers[2] = {SIGNAL_INTERRUPT, SIGNAL_QUIT};

        /* Installing SIG_IGN can return the inherited action in the same
           call. These are the only two dispositions changed at entry; every
           other signal remains queryable when trap first touches it. */
        for (positive at = 0; at < 2; at++)
        {
                positive old[4] = {0, 0, 0, 0};
                b32 number = numbers[at];
                positive mask = (positive)1 << number;

                if (system_signal_action(number,
                                  address_of ignored,
                                  address_of old, 8) >= 0)
                {
                        shell_signals_known |= mask;

                        if (old[0] == SIGNAL_IGNORE)
                                shell_signals_ignored |= mask;
                }
        }
}

/*
        Only a shell somebody is typing at stays deaf to control-C.

        Every shell starts ignoring interrupt and quit, because until the
        options are read nobody knows whether this one is interactive. A
        script or a -c string that went on ignoring them could not be stopped
        from the terminal at all: control-C took the command it was running
        and the shell went on to the next line, a loop of builtins ran for
        ever, and a utility run as the last command in the shell's own process
        kept that deafness too. dash and bash leave a non-interactive shell
        interruptible; what was already ignored when it started stays ignored.
*/
HOT fn shell_signals_settle(bool interactive)
{
        b32 numbers[2] = {SIGNAL_INTERRUPT, SIGNAL_QUIT};

        if (shell_signals_deferred)
        {
                shell_signals_deferred = false;
                if (interactive)
                        shell_signals_start();
                return;
        }

        if (interactive)
                return;

        for (positive at = 0; at < 2; at++)
                if (!(shell_signals_ignored & ((positive)1 << numbers[at])))
                        shell_default(numbers[at]);
}

// Set where the signal landed, read where a command ends. A handler that ran
// the action itself would be running the parser on top of whatever the parser
// was already in the middle of.
fn trap_signal_caught(b32 number);

#if defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__)

/*
        Where the handler goes when it is done.

        x86_64 has no return trampoline of its own: the kernel jumps to
        sa_restorer, and what is there has to call rt_sigreturn. arm64 has
        one only in the vDSO, and a Spark image is mapped without a vDSO, so
        the kernel's default return address there is the trampoline's offset
        from zero: every handler the shell ran on arm64 returned to 0x83c and
        died of SIGSEGV, which killed the shell after every command that had a
        child. arm64 honours SA_RESTORER the way x86_64 requires it, so both
        are handed the platform's trampoline, the one sigaction hands the
        kernel too. riscv64 has no restorer field at all.
*/
#define SIGNAL_CATCH_RESTORER ((positive)signal_return_trampoline)
#define SIGNAL_CATCH_FLAGS (SIGNAL_RESTART | SIGNAL_RESTORER)

#else

#define SIGNAL_CATCH_RESTORER 0
#define SIGNAL_CATCH_FLAGS SIGNAL_RESTART

#endif

/*
        A signal the script asked to hear about.

        Restarting, so that a wait for a child is not cut short by a signal
        the shell is only noting down: the action runs when the command it
        interrupted has finished, which is where POSIX says it runs.
*/
fn shell_catch_mode(b32 number, bool restart)
{
        positive flags = SIGNAL_CATCH_FLAGS;

        if (!restart)
                flags &= ~SIGNAL_RESTART;

        system_signal_install(number, (positive)trap_signal_caught, flags,
                              SIGNAL_CATCH_RESTORER, null);
}

fn shell_catch(b32 number)
{
        shell_catch_mode(number, true);
}

// Whether anybody is watching. A script and a terminal want different
// things of a shell that has just been told to do something impossible.
b32 shell_is_interactive HOT_STATE;

/* One implementation, two conflicting shell policies. The standalone entry
   selects Bash policy only when invoked as bash; sh/dash and embedded callers
   retain Moonwater's existing dash-compatible defaults. */
KEEP __attribute__((externally_visible)) bool shell_bash_compat HOT_STATE;
bool shell_dash_compat HOT_STATE;
// Takes bash's additions out of the language for the names dash and sh.
fn shell_dash_begin();
/* Set by the reader when more source remains after this physical line,
   ignoring trailing newlines. Dash's runtime "Bad fd number" names that
   next line rather than the command's own. */
bool shell_line_has_more;

/*
        rbash: a shell started as rbash, or with -r / --restricted, or
        that later ran set -r.

        Not one of the set options, because in Bash it is not one either: it
        is in $- and `set -r` turns it on, but nothing turns it off again,
        `set -o` never lists it and there is no -o name for it. A word of
        its own is what that shape wants; a table entry would have brought
        the three spellings it does not have with it.
*/
KEEP __attribute__((externally_visible)) bool shell_restricted;
/* -r and --restricted, as opposed to the name rbash: the restriction
   stays even when -c's $0 operand is not rbash. */
bool shell_restricted_sticky;

/*
        Whether job listings are written in dash's columns.

        The two shells lay a job line out differently and there is no third
        answer, so the question is asked once, by name, rather than every
        caller testing which personality this is and getting it right.
*/
#define shell_dash_columns() (!shell_bash_compat)

/*
        How many readers deep this is, and a syntax failure's scope.

        The process reader leaves on one, while eval and dot return it to the
        executor so POSIX special-builtin policy can distinguish a direct
        invocation from one behind command. A generation, rather than a
        sticky bit, lets nested readers notice only failures that happened
        inside their own input.

        It is also the depth Bash marks an xtrace line with, which is why it
        is declared here rather than beside the reader below: the executor is
        included first and reads it.
*/
static positive shell_run_depth HOT_STATE;

// Whether output that can carry colour does. An interface that draws its own
// screen turns it off while it holds the terminal.
bool shell_styles = true;


/*
        Nothing the shell builds has a ceiling.

        This is init. It runs on a machine that may have a hundred thousand
        files in a directory, and a glob that quietly stops at the sixty
        fourth of them is worse than one that fails outright, because the
        script carries on believing it saw everything. Every list a script can
        make longer therefore grows, and the only thing that ends it is the
        kernel refusing more memory.

        Two shapes are needed, and the difference between them is whether
        anything is holding a pointer into the thing while it grows.

        A table of pointers may move. Nothing keeps an address inside one --
        it is always reached by index -- so it grows by taking a larger
        mapping, copying, and giving the old one back.

        The bytes those pointers point AT may never move, because argv entries
        are addresses into them and are handed to execve. So a byte store is a
        chain of blocks instead: when a block is full the next one is spliced
        on and everything already given out stays exactly where it was. A line
        ends by rewinding to the first block rather than freeing, so the steady
        state of a shell in a loop is no allocation at all.
*/

typedef struct shell_block
{
        struct shell_block address_to next;
        positive size;
        positive used;
} shell_block;

typedef struct
{
        shell_block address_to head;
        shell_block address_to here;
} shell_store;

#define SHELL_BLOCK 65536
/* A command which needed more than this keeps its expansion buffers for the
   next command, so repeated large work remains allocation-free. If the next
   command uses no more than this much, the old high-water mapping has proved
   to be a one-off and is returned to the kernel at that command boundary. */
#define SHELL_SCRATCH_RETAIN (1u << 20)

static bool shell_memory_failed;
/* Set by a completed exceptional expansion and cleared only when the complete
   top-level command ends. A compound command may finish on a tiny `:` after
   doing large work; its final expansion is not its working set. */
static bool shell_large_request;

/*
        The first of everything the shell holds comes out of one block.

        A command's tables (the variables, their index, the expansion text, the
        tokens) and its first store block each began as a mapping of their own:
        six mmaps, six pages, six sets of page tables to build and tear down,
        for a few kilobytes that nearly every command uses. memory_pool_block
        is the program's bss for exactly that, so they are carved from it, in
        the order they are asked, onto the pages the last one left half empty.
        A piece is never given back (memory_free of one is nothing, which
        lib.c arranges) and never at a page boundary, so a store that grows
        past its piece is moved by a copy into a mapping of its own, as it was
        always going to be. A request the block cannot hold, a big one or one
        after it has run out, is a mapping as before.
*/
#if defined(LINUX) && !defined(KERNEL_MODE)
static positive shell_pool_used HOT_STATE;

static address_any shell_pool_take(positive size)
{
        positive rounded = (size + 15) & ~(positive)15;
        positive start;

        if (size >= MEMORY_POOL_BYTES || rounded + 16 > MEMORY_POOL_BYTES)
                return null;

        /* A piece and the sixteen bytes that keep it off a page boundary. */
        start = __atomic_fetch_add(&shell_pool_used, rounded + 16,
                                   __ATOMIC_RELAXED);

        if (start + rounded + 16 > MEMORY_POOL_BYTES)
                return null;

        if (!((positive)(memory_pool_block + start) & 4095))
                start += 16;

        return memory_pool_block + start;
}
#else
static address_any shell_pool_take(positive size)
{
        (void)size;
        return null;
}
#endif

static HOT address_any shell_map(positive size)
{
        {
                address_any carved = shell_pool_take(size);

                if (carved)
                        return carved;
        }

        //      memory_checked answers null for the kernel's own negative
        //      errno as well, which as an address is the top page of the
        //      space and never a mapping.
        address_any got = memory_checked(size);

        if (!got)
                shell_memory_failed = true;

        return got;
}

/*
        The table is almost always already big enough.

        Every push into a growing store asks this, twice for a byte pair, so
        it is the most-executed predicate in the shell. Splitting the growth
        off keeps the answer in the caller as one compare and a fall-through:
        the arguments no longer have to be set up for a call that is not
        going to happen, and memory_reserve's register pressure stops
        reaching into the loop that asked.
*/
static __attribute__((noinline)) COLD bool
shell_room_grow(address_any address_to held, positive address_to have,
                positive want, positive unit)
{
        /* A table's first room is carved from the pool (see shell_pool_take),
           with the capacity memory_reserve would have given it. */
        if (!address_to held && !address_to have && unit)
        {
                positive room = memory_growth(0, want, 64);

                if (room && room <= (positive)-1 / unit)
                {
                        address_any carved = shell_pool_take(room * unit);

                        if (carved)
                        {
                                address_to held = carved;
                                address_to have = room;
                                return true;
                        }
                }
        }

        if (memory_reserve(held, have, *have, want, unit, 64))
                return true;

        shell_memory_failed = true;
        return false;
}

//      Room for want entries of unit bytes each, moving the table if it must.
static inline INLINE bool shell_room(address_any address_to held,
                                     positive address_to have,
                                     positive want, positive unit)
{
        if (likely(want <= *have))
                return true;

        return shell_room_grow(held, have, want, unit);
}

static inline INLINE fn shell_scratch_bytes(positive want)
{
        if (!shell_large_request && want > SHELL_SCRATCH_RETAIN)
                shell_large_request = true;
}

// Most growing stores carry their width in their pointed-to type. Keep the
// cast and sizeof at this floor instead of repeating both at every caller.
#define shell_array_room(array, room, want)                                  \
        shell_room((address_any address_to)address_of (array),               \
                   address_of (room), (want), sizeof((array)[0]))

// Parallel byte stores share one capacity decision at their call sites.
#define shell_byte_pair_room(first, first_room, second, second_room, want)    \
        (shell_room((address_any address_to)address_of (first),               \
                    address_of (first_room), (want), 1) &&                    \
         shell_room((address_any address_to)address_of (second),              \
                    address_of (second_room), (want), 1))

//      Bytes that will not move for as long as the line lasts.
/*
        The block in hand is almost always the one the bytes come from, and
        the bump is four instructions. Everything below -- the free-list walk
        and the mapping behind it -- happens once per block, so it is a call
        the fast path never makes rather than a tail it always carries.
*/
static __attribute__((noinline)) COLD p8 address_to
shell_store_take_block(shell_store address_to store, positive room)
{
        shell_block address_to block = store->here;

        /* Claim the first large-enough released block, wherever it is in the
           tail. The same link insertion serves reused and newly mapped blocks,
           including an arena whose current position is before its head. */
        shell_block address_to address_to next =
            block ? address_of block->next : address_of store->head;
        shell_block address_to address_to link = next;

        while (*link && room > (*link)->size)
                link = address_of (*link)->next;

        block = *link;
        if (block)
                *link = block->next;
        else
        {
                positive size = memory_growth(0, room, SHELL_BLOCK);

                if (!size || size > positive_max - sizeof(shell_block))
                {
                        shell_memory_failed = true;
                        return null;
                }

                block = (shell_block address_to)shell_map(
                    size + sizeof(shell_block));
                if (!block)
                        return null;
                block->size = size;
        }

        block->next = *next;
        *next = block;
        block->used = room;
        store->here = block;
        return (p8 address_to)(block + 1);
}

static inline INLINE p8 address_to
shell_store_take(shell_store address_to store, positive room)
{
        shell_block address_to block = store->here;

        if (!room)
                room = 1;

        if (likely(block && block->used <= block->size &&
                   room <= block->size - block->used))
        {
                p8 address_to bytes = (p8 address_to)(block + 1) + block->used;
                block->used += room;
                return bytes;
        }

        return shell_store_take_block(store, room);
}

/*
        A record or a table of pointers, which the byte the last string ended
        on leaves at any offset: an eight-byte word stored through it is a
        misaligned store, which x86_64 and arm64 allow and a strict machine
        does not, and which the language does not allow anywhere. Taken from
        the next multiple of eight instead, at most seven bytes further on.
        A mark taken before it rewinds over the padding with everything else.
*/
static inline INLINE p8 address_to
shell_store_take_aligned(shell_store address_to store, positive room)
{
        shell_block address_to block = store->here;

        if (block && block->used <= block->size)
        {
                positive pad = (positive)(-block->used) & 7;

                if (pad <= block->size - block->used)
                        block->used += pad;
        }

        return shell_store_take(store, room);
}

/* Stable, terminated spans share the arena's allocation and overflow policy. */
static HOT p8 address_to shell_store_copy(shell_store address_to store,
                                      address_any text, positive length)
{
        if (length == positive_max)
        {
                shell_memory_failed = true;
                return null;
        }
        p8 address_to held = shell_store_take(store, length + 1);
        if (held)
                memory_copy_end(held, text, length);
        return held;
}

//      The line is over. Rewind rather than free: the next line will want the
//      same blocks, and a shell in a loop should stop allocating entirely.
static fn shell_store_reset(shell_store address_to store)
{
        store->here = store->head;

        if (store->here)
                store->here->used = 0;
}

/* Moving expansion arrays use a one-command grace before returning a stale
   high-water mapping. Chained stores keep every block: their many-small-word
   aggregate cannot be inferred from any one completed expansion without
   adding bookkeeping to the allocator hot path. */
static fn shell_room_relax(address_any address_to held,
                           positive address_to have, positive active,
                           positive unit)
{
        positive limit;

        if (!unit)
                return;

        limit = SHELL_SCRATCH_RETAIN / unit;

        if (address_to have <= limit || active > limit)
                return;

        memory_free(address_to held, address_to have * unit);
        address_to held = null;
        address_to have = 0;
}

/*
        A point in the store, and the way back to it.

        Some of this is built speculatively -- a command's assignments are kept
        while its words are expanded and thrown away if the line turns out not
        to run -- so the store has to be able to give back everything taken
        since a moment, without giving back what was there before it.
*/
typedef struct
{
        shell_block address_to block;
        positive used;
} shell_mark;

static inline INLINE PURE shell_mark shell_store_mark(shell_store address_to store)
{
        shell_mark mark;

        mark.block = store->here;
        mark.used = store->here ? store->here->used : 0;

        return mark;
}

static inline INLINE fn shell_store_rewind(shell_store address_to store, shell_mark mark)
{
        if (mark.block)
        {
                if (store->here == mark.block && store->here->used == mark.used)
                        return;

                store->here = mark.block;
                store->here->used = mark.used;
                return;
        }

        if (!store->here)
                return;

        shell_store_reset(store);
}

/*
        A list of words that grows as words are put in it.

        This is what argv is made of and what a field split fills, so it is the
        one place a glob's answer stops being bounded by anything.
*/
typedef struct
{
        string_address address_to address_to word;
        positive address_to room;
        positive count;
} shell_words;

//      Bound to wherever the table actually lives, so the same appending code
//      serves argv, a for loop's list and set's arguments alike.
static inline INLINE bool shell_words_add(shell_words address_to list, string_address word)
{
        if (list->count + 2 <= *list->room)
        {
                (address_to list->word)[list->count++] = word;
                (address_to list->word)[list->count] = null;
                return true;
        }

        if (!shell_room((address_any address_to)list->word, list->room,
                        list->count + 2, sizeof(string_address)))
                return false;

        (address_to list->word)[list->count++] = word;
        (address_to list->word)[list->count] = null;

        return true;
}

static fn shell_words_bind(shell_words address_to list,
                           string_address address_to address_to table,
                           positive address_to room)
{
        list->word = table;
        list->room = room;
        list->count = 0;
}

/* Set only in a process created solely to run one simple command. Utilities
   can then use that process directly instead of cloning a disposable wrapper
   around another disposable child. */
static bool shell_tail_command HOT_STATE;
// The substitution child grants that privilege after parsing proves the line
// is one simple command; functions decline it later in exec_dispatch.
static bool shell_tail_line_requested HOT_STATE;
fn shell_thread_instance_mode(bool preserve_ignored);

// More than one line of source, run one at a time: what a trap action, eval
// and dot hand over. Defined beside run_line below, after the executor.
fn run_lines(string_address text);

/* The mount table is shared by file and storage utilities. Its parser lives
   in storage.c, while this declaration keeps consumers independent
   of source inclusion order. */
typedef struct
{
        positive id;
        positive parent_id;
        string_address device;
        string_address root;
        string_address target;
        string_address options;
        string_address type;
        string_address source;
        string_address filesystem_options;
} storage_mount;

typedef struct
{
        byte_store text;
        storage_mount address_to entry;
        positive entry_room;
        positive count;
} storage_mount_table;

bool storage_mount_table_load(storage_mount_table address_to table,
                              writer diagnostic);
fn storage_mount_table_release(storage_mount_table address_to table);

/*
        shopt: the option namespace set does not carry.

        A script that opens with `shopt -s nullglob` must not die on the name,
        so every option Bash 5.3 has is stored whether or not anything reads
        it -- a stored no-op keeps the script running, which is the whole
        point of the builtin for the fifty-odd names nothing here acts on.

        One bit each in a single word, because the readers are the ones that
        cannot afford a lookup: pathname expansion asks about four of these
        for every pattern it walks, and the matcher asks about a fifth for
        every case arm. A bit test against a global is what that costs.

        The table is here rather than beside the builtin because the readers
        are spread across the expander, the matcher and the executor, and all
        three are included before builtin.c is.
*/
static string_address shell_shopt_names[] = {
    "array_expand_once", "assoc_expand_once", "autocd",
    "bash_source_fullpath", "cdable_vars", "cdspell", "checkhash",
    "checkjobs", "checkwinsize", "cmdhist", "compat31", "compat32",
    "compat40", "compat41", "compat42", "compat43", "compat44",
    "complete_fullquote", "direxpand", "dirspell", "dotglob", "execfail",
    "expand_aliases", "extdebug", "extglob", "extquote", "failglob",
    "force_fignore", "globasciiranges", "globskipdots", "globstar",
    "gnu_errfmt", "histappend", "histreedit", "histverify", "hostcomplete",
    "huponexit", "inherit_errexit", "interactive_comments", "lastpipe",
    "lithist", "localvar_inherit", "localvar_unset", "login_shell",
    "mailwarn", "no_empty_cmd_completion", "nocaseglob", "nocasematch",
    "noexpand_translation", "nullglob", "patsub_replacement", "progcomp",
    "progcomp_alias", "promptvars", "restricted_shell", "shift_verbose",
    "sourcepath", "varredir_close", "xpg_echo", null,
};

#define SHELL_SHOPT_NAMES (array_count(shell_shopt_names) - 1)

//      The position of a name in the table above is its bit. Only the names
//      something reads are spelled out; the rest are reached by lookup.
#define SHELL_SHOPT_CHECKWINSIZE 8
#define SHELL_SHOPT_CMDHIST 9
#define SHELL_SHOPT_COMPLETE_FULLQUOTE 17
#define SHELL_SHOPT_DOTGLOB 20
#define SHELL_SHOPT_EXECFAIL 21
#define SHELL_SHOPT_EXPAND_ALIASES 22
#define SHELL_SHOPT_EXTGLOB 24
#define SHELL_SHOPT_EXTQUOTE 25
#define SHELL_SHOPT_FAILGLOB 26
#define SHELL_SHOPT_FORCE_FIGNORE 27
#define SHELL_SHOPT_GLOBASCIIRANGES 28
#define SHELL_SHOPT_GLOBSKIPDOTS 29
#define SHELL_SHOPT_GLOBSTAR 30
#define SHELL_SHOPT_HOSTCOMPLETE 35
#define SHELL_SHOPT_INHERIT_ERREXIT 37
#define SHELL_SHOPT_INTERACTIVE_COMMENTS 38
#define SHELL_SHOPT_LASTPIPE 39
#define SHELL_SHOPT_LOCALVAR_INHERIT 41
#define SHELL_SHOPT_LOGIN_SHELL 43
#define SHELL_SHOPT_NOCASEGLOB 46
#define SHELL_SHOPT_NOCASEMATCH 47
#define SHELL_SHOPT_NULLGLOB 49
#define SHELL_SHOPT_PATSUB_REPLACEMENT 50
#define SHELL_SHOPT_PROGCOMP 51
#define SHELL_SHOPT_PROMPTVARS 53
#define SHELL_SHOPT_RESTRICTED_SHELL 54
#define SHELL_SHOPT_SOURCEPATH 56
#define SHELL_SHOPT_SHIFT_VERBOSE 55
#define SHELL_SHOPT_XPG_ECHO 58

#define SHELL_SHOPT(which) ((positive)1 << SHELL_SHOPT_##which)

//      What Bash reports for a shell that was started to read a script. Every
//      other name begins off, which is why the word and not the table is the
//      one place the state lives.
#define SHELL_SHOPT_STARTED                                                  \
        (SHELL_SHOPT(CHECKWINSIZE) | SHELL_SHOPT(CMDHIST) |                  \
         SHELL_SHOPT(COMPLETE_FULLQUOTE) | SHELL_SHOPT(EXTQUOTE) |           \
         SHELL_SHOPT(FORCE_FIGNORE) | SHELL_SHOPT(GLOBASCIIRANGES) |         \
         SHELL_SHOPT(GLOBSKIPDOTS) | SHELL_SHOPT(HOSTCOMPLETE) |             \
         SHELL_SHOPT(INTERACTIVE_COMMENTS) |                                 \
         SHELL_SHOPT(PATSUB_REPLACEMENT) | SHELL_SHOPT(PROGCOMP) |           \
         SHELL_SHOPT(PROMPTVARS) | SHELL_SHOPT(SOURCEPATH))

positive shell_shopt_state = SHELL_SHOPT_STARTED;

#define shell_shopt_on(which)                                                \
        ((shell_shopt_state & SHELL_SHOPT(which)) != 0)

/*
        rbash, -r, --restricted and set -r all enter the same state: the
        restriction itself, and the shopt that reports how the shell was
        started. shopt -u restricted_shell is a no-op, and set +r is an
        error once this has run. The one thing that can still drop the
        restriction itself (not the shopt) is -c's $0 operand: bash
        restricts a shell whose $0 is rbash and lifts an argv0-only
        restriction when $0 is any other name. -r is sticky through that.
*/
fn shell_restricted_enter()
{
        shell_restricted = true;
        shell_shopt_state |= SHELL_SHOPT(RESTRICTED_SHELL);
}

/*
        The names that answer without being stored.

        Declared here because the expander is included first and is where the
        miss that reaches them happens; what they answer with is beside the
        environment table in builtin.c, which is the only place that knows how
        a name is looked up in the first place.
*/
#if defined(__aarch64__) || defined(_M_ARM64)
#define MOONWATER_HOSTTYPE "aarch64"
#define MOONWATER_MACHTYPE "aarch64-unknown-linux-gnu"
#elif defined(__riscv)
#define MOONWATER_HOSTTYPE "riscv64"
#define MOONWATER_MACHTYPE "riscv64-unknown-linux-gnu"
#else
#define MOONWATER_HOSTTYPE "x86_64"
#define MOONWATER_MACHTYPE "x86_64-pc-linux-gnu"
#endif

COLD string_address shell_dynamic_value(const_string name, positive length,
                                        positive address_to value_length);
COLD bool shell_dynamic_assign(const_string name, positive length,
                               const_string value);
COLD bool shell_dynamic_wanted(const_string name, positive length);
COLD fn shell_bash_ids_publish();

//      How many subshells deep this process is, which is what $BASH_SUBSHELL
//      is and the only thing a fork has to remember to say it.
extern positive shell_subshell_depth;

fn shell_last_argument_set(string_address word);
PURE bool shell_braceexpand_on();
PURE bool shell_posix_on();

//      The next live function out of the executor's table. The table is in
//      exec.c, which is included last; callers keep only the stable name.
string_address exec_function_next(positive address_to slot,
                                  bool address_to readonly);
bool exec_function_write(writer write, string_address name, b32 filter);
b32 exec_function_attributes_hashed(string_address name, positive2 named);
bool exec_function_export_set(string_address name, bool enabled);
positive exec_function_environment_generation();
positive exec_function_environment_count();
bool exec_function_environment_fill(string_address address_to environment,
                                    positive count);
fn exec_function_import_environment(string_address address_to environment);
static fn shell_spawn_device_disable();

/* Nested-command utilities are included before the policy engine that owns
   their final exec decision.  Declare the shared handoff here so every
   wrapper reaches the same resolver, restriction check and Floodlight pin. */
bipolar shell_exec_file(string_address path,
                        string_address address_to arguments,
                        positive count,
                        string_address address_to environment);
static bipolar file_exec_path_try_in(
    string_address name, string_address address_to words,
    string_address address_to environment, string_address path);
static bipolar file_exec_path_try(string_address address_to words);

/* Keep child wiring names at their call sites while ownership and the fd==fd
   edge live with the shared descriptor operations. */
#define shell_child_fd_move system_descriptor_move
#define SHELL_PIPE_CLOSE_ON_EXEC 02000000

#include "lex.c"
#include "file.c"
#include "gzip.c"
#include "xz.c"
#include "zstd.c"
#include "tar.c"
#include "snapshot.c"
#include "storage.c"
#include "text.c"
#include "checksum.c"
#include "awk.c"
#include "tools.c"
#include "pty.c"

/*
        Before process_tools, because stdbuf has to find its preload library
        inside a distribution root and that means knowing where those live.
        It used to know by spelling "/bowls/" out for itself, which is bowl's
        layout written down twice -- so moving the roots would have left
        stdbuf quietly failing to find the library under a bowl. Bowl needs
        nothing of the shell but file.c's environment, so it can come this
        early and be the one place that says where a root is.
*/
#include "../canvas/terminfo.c"
#include "../bowl.c"
#include "process_tools.c"
#include "monitor.c"
#include "net.c"
fn shell_child_death(bipolar child, positive raw, bool foreground);
static fn shell_parser_source_fork_prepare();
static bool floodlight_descendants_present();
static bool floodlight_descendants_blocking();
static fn job_child_watch();
static bool floodlight_parent_supervised;
#include "expand.c"
#include "../canvas/window.c"
#include "../canvas/term.c"
#include "host.c"
#include "../canvas/screen.c"
#include "edit.c"
#include "system.c"
#define PROMPT TERM_RESET TERM_BOLD " $ " TERM_RESET

static positive shell_syntax_generation HOT_STATE;

/* Source bytes held in memory have no live producer for a child to influence.
   A streamed descriptor does: classify it once at the authenticated reader
   boundary, then let the final policy decision admit only sources whose
   influence that policy can actually remove. */
#define SHELL_PARSER_SOURCE_MEMORY 0
#define SHELL_PARSER_SOURCE_SEALED_FILE 1
#define SHELL_PARSER_SOURCE_REGULAR_FILE 2
#define SHELL_PARSER_SOURCE_SOCKET 3
#define SHELL_PARSER_SOURCE_MUTABLE 4
#define SHELL_PARSER_SOURCE_AMBIGUOUS 5

#define SHELL_PARSER_MFD_CLOEXEC 1
#define SHELL_PARSER_MFD_ALLOW_SEALING 2
// Linux 6.3's MFD_NOEXEC_SEAL: the file can never be mapped executable or run.
#define SHELL_PARSER_MFD_NOEXEC_SEAL 8
#define SHELL_PARSER_F_GETFD 1
#define SHELL_PARSER_F_ADD_SEALS 1033
#define SHELL_PARSER_F_GET_SEALS 1034
#define SHELL_PARSER_F_SEAL_SEAL 0x01
#define SHELL_PARSER_F_SEAL_SHRINK 0x02
#define SHELL_PARSER_F_SEAL_GROW 0x04
#define SHELL_PARSER_F_SEAL_WRITE 0x08
#define SHELL_PARSER_FD_CLOEXEC 1
#define SHELL_PARSER_SNAPSHOT_SEALS                                     \
        (SHELL_PARSER_F_SEAL_SEAL | SHELL_PARSER_F_SEAL_SHRINK |       \
         SHELL_PARSER_F_SEAL_GROW | SHELL_PARSER_F_SEAL_WRITE)
#define SHELL_PARSER_SNAPSHOT_STEP 4096

/* Sealed files need every inherited alias removed from a final child. This
   identity is deliberately generic: changing the
   source's bytes is not the only influence, because reading or seeking a
   shared open description also changes where the parent continues. */
static file_facts shell_parser_isolated_facts;
static bool shell_parser_isolated_live;
static bool shell_parser_source_active HOT_STATE;
static bool shell_parser_source_ambiguous HOT_STATE;
static positive shell_parser_source_kind = SHELL_PARSER_SOURCE_MEMORY;
static bipolar shell_parser_source_handle = -1;
static bipolar shell_parser_source_process = -1;
static p8 shell_parser_snapshot_buffer[SHELL_PARSER_SNAPSHOT_STEP];

static bool shell_parser_snapshot_sealed(bipolar handle)
{
        bipolar seals = system_call_3(syscall(fcntl), (positive)handle,
                                      SHELL_PARSER_F_GET_SEALS, 0);

        return seals >= 0 &&
               ((positive)seals & SHELL_PARSER_SNAPSHOT_SEALS) ==
                   SHELL_PARSER_SNAPSHOT_SEALS;
}

static bool shell_parser_snapshot_same(
    const file_facts address_to before,
    const file_facts address_to after)
{
        return (after->mask & STATX_BASIC) == STATX_BASIC &&
               file_same_identity((file_facts address_to)before,
                                  (file_facts address_to)after) &&
               !((before->mask ^ after->mask) & STATX_MOUNT_ID) &&
               (!(before->mask & STATX_MOUNT_ID) ||
                before->mount_id == after->mount_id) &&
               before->size == after->size &&
               before->changed.seconds == after->changed.seconds &&
               before->changed.nanoseconds == after->changed.nanoseconds &&
               before->modified.seconds == after->modified.seconds &&
               before->modified.nanoseconds == after->modified.nanoseconds;
}

/* Freeze all bytes the reader has not yet consumed without moving the source
   offset.  Nothing is installed until the complete bounded tail is copied,
   the source is reauthenticated unchanged, and the replacement is sealed
   against writes and size changes.  Every failure therefore leaves the
   original descriptor in place for unrestricted compatibility, while its
   source class makes a later restricted launch fail closed. */
static bool shell_parser_snapshot_make(
    bipolar handle, const file_facts address_to original,
    file_facts address_to frozen)
{
        file_facts after;
        bipolar offset = system_seek(handle, 0, FILE_SEEK_CUR);
        bipolar descriptor_flags = system_call_3(
            syscall(fcntl), (positive)handle, SHELL_PARSER_F_GETFD, 0);
        bipolar snapshot = -1;
        bipolar beyond;
        p64 copied = 0;
        p64 remaining;
        p8 probe;
        bool ready = false;

        if (offset < 0 || descriptor_flags < 0 ||
            ((positive)descriptor_flags & ~SHELL_PARSER_FD_CLOEXEC) ||
            original->size > (p64)bipolar_max ||
            (p64)offset > original->size)
                return false;

        remaining = original->size - (p64)offset;
        snapshot = system_call_2(
            syscall(memfd_create), (positive)(string_address)"shell-parser",
            SHELL_PARSER_MFD_CLOEXEC | SHELL_PARSER_MFD_ALLOW_SEALING |
                SHELL_PARSER_MFD_NOEXEC_SEAL);
        if (snapshot < 0 || snapshot == handle)
                goto finished;

        while (copied < remaining)
        {
                positive ask = remaining - copied > SHELL_PARSER_SNAPSHOT_STEP
                                   ? SHELL_PARSER_SNAPSHOT_STEP
                                   : (positive)(remaining - copied);
                bipolar got = system_call_4(
                    syscall(pread64), (positive)handle,
                    (positive)address_of shell_parser_snapshot_buffer, ask,
                    (positive)((p64)offset + copied));

                if (got == -4)
                        continue;
                if (got <= 0 || (positive)got > ask ||
                    system_write_all((positive)snapshot,
                                     shell_parser_snapshot_buffer,
                                     (positive)got) != (positive)got)
                        goto finished;
                copied += (positive)got;
        }

        /* statx size is not a readable-length promise for proc-style files.
           Probe exactly at the advertised end with positional I/O: a byte
           there means the tail was not complete, while any error makes the
           representation ambiguous. Neither outcome may publish an empty or
           truncated snapshot as safe. */
        do
                beyond = system_call_4(
                    syscall(pread64), (positive)handle,
                    (positive)address_of probe, 1, (positive)original->size);
        while (beyond == -4);
        if (beyond != 0)
                goto finished;

        if (!file_look(handle, (string_address)"", AT_EMPTY_PATH,
                       address_of after) ||
            !shell_parser_snapshot_same(original, address_of after) ||
            system_seek(handle, 0, FILE_SEEK_CUR) != offset ||
            system_call_3(syscall(fcntl), (positive)snapshot,
                          SHELL_PARSER_F_ADD_SEALS,
                          SHELL_PARSER_SNAPSHOT_SEALS) < 0 ||
            !shell_parser_snapshot_sealed(snapshot) ||
            system_seek(snapshot, 0, FILE_SEEK_SET) != 0 ||
            !file_look(snapshot, (string_address)"", AT_EMPTY_PATH, frozen) ||
            (frozen->mask & STATX_BASIC) != STATX_BASIC ||
            (frozen->mode & MODE_FORMAT) != MODE_FILE)
                goto finished;

        if (system_duplicate(snapshot, handle,
                             ((positive)descriptor_flags &
                              SHELL_PARSER_FD_CLOEXEC)
                                 ? O_CLOEXEC : 0) != handle)
                goto finished;

        ready = true;

finished:
        /* dup3 leaves the reader itself holding the sealed open description.
           Do not retain a second, script-visible descriptor which could read
           or seek the parent's future parser bytes. */
        if (snapshot >= 0 && snapshot != handle)
                system_close(snapshot);
        return ready;
}

static fn shell_parser_source_refresh()
{
        file_facts facts;

        shell_parser_isolated_live = false;
        shell_parser_source_ambiguous = false;
        shell_parser_source_kind = SHELL_PARSER_SOURCE_MEMORY;
        if (!shell_parser_source_active)
                return;

        /* Failure to authenticate either the descriptor or its complete type
           is its own source class.  It must never inherit the privileges of a
           source that merely happens to look absent. */
        shell_parser_source_kind = SHELL_PARSER_SOURCE_AMBIGUOUS;

        if (!file_look(shell_parser_source_handle, (string_address)"",
                       AT_EMPTY_PATH, address_of facts) ||
            (facts.mask & STATX_BASIC) != STATX_BASIC)
        {
                shell_parser_source_ambiguous = true;
                return;
        }

        if ((facts.mode & MODE_FORMAT) == MODE_PIPE)
                /* Both a named FIFO and an anonymous pipe retain a writer
                   outside this process. A same-UID child can reacquire that
                   writer through the feeder's proc descriptor table. */
                shell_parser_source_kind = SHELL_PARSER_SOURCE_MUTABLE;
        else if ((facts.mode & MODE_FORMAT) == MODE_FILE)
        {
                /* A snapshot already installed at the reader survives between
                   batches and through named-script relocation.  An ordinary
                   file remains cheap to stream until a parent-side policy
                   decision proves that a restricted child will need its tail
                   frozen. */
                if (shell_parser_snapshot_sealed(
                        shell_parser_source_handle))
                {
                        shell_parser_source_kind =
                            SHELL_PARSER_SOURCE_SEALED_FILE;
                        shell_parser_isolated_facts = facts;
                        shell_parser_isolated_live = true;
                }
                else
                        shell_parser_source_kind =
                            SHELL_PARSER_SOURCE_REGULAR_FILE;
        }
        else if ((facts.mode & MODE_FORMAT) == MODE_SOCKET)
                shell_parser_source_kind = SHELL_PARSER_SOURCE_SOCKET;
        else
                /* Terminals, block devices and every other concrete source
                   remain externally mutable while the shell streams later
                   parser bytes from them. A PTY's influencing master can be
                   held by an outside same-UID process, beyond this child's
                   descriptor inventory, so terminal input must also refuse a
                   restricted launch. */
                shell_parser_source_kind = SHELL_PARSER_SOURCE_MUTABLE;
}

/* A regular script is copied only when a nonfinal decision in its owning
   shell has proved that this launch needs confinement.  A forked/final child
   must never publish a private snapshot which leaves the real parent reader
   mutable. */
static bool shell_parser_source_prepare()
{
        file_facts facts;
        bipolar own;

        if (shell_parser_source_kind == SHELL_PARSER_SOURCE_MEMORY ||
            shell_parser_source_kind == SHELL_PARSER_SOURCE_SEALED_FILE)
                return true;

        if (shell_parser_source_kind != SHELL_PARSER_SOURCE_REGULAR_FILE ||
            !shell_parser_source_active)
                return false;

        own = system_call_1(syscall(getpid), 0);
        if (own <= 0 || own != shell_parser_source_process ||
            !file_look(shell_parser_source_handle, (string_address)"",
                       AT_EMPTY_PATH, address_of facts) ||
            (facts.mask & STATX_BASIC) != STATX_BASIC ||
            (facts.mode & MODE_FORMAT) != MODE_FILE ||
            shell_parser_snapshot_sealed(shell_parser_source_handle) ||
            !shell_parser_snapshot_make(
                shell_parser_source_handle, address_of facts,
                address_of shell_parser_isolated_facts))
        {
                shell_parser_source_kind = SHELL_PARSER_SOURCE_AMBIGUOUS;
                shell_parser_source_ambiguous = true;
                return false;
        }

        shell_parser_source_kind = SHELL_PARSER_SOURCE_SEALED_FILE;
        shell_parser_isolated_live = true;
        return true;
}

static bool shell_parser_source_begin(bipolar handle)
{
        shell_parser_source_handle = handle;
        shell_parser_source_process =
            system_call_1(syscall(getpid), 0);
        shell_parser_source_active = true;

        /* Startup code can leave a confined child alive before the main
           reader is opened.  That child may already have selected or changed
           the named source, so freezing bytes after open would authenticate
           an attacker-chosen program.  Memory sources never call begin. */
        if (floodlight_parent_supervised &&
            floodlight_descendants_present())
        {
                shell_parser_isolated_live = false;
                shell_parser_source_kind = SHELL_PARSER_SOURCE_AMBIGUOUS;
                shell_parser_source_ambiguous = true;
                return false;
        }

        shell_parser_source_refresh();
        return true;
}

/* The named-script reader is a shell-owned descriptor.  A user redirect can
   claim its number, in which case exec.c moves the same open description out
   of the way.  Keep the published handle with that move without re-statting
   it: every command in this reader batch still came from the identity that
   begin authenticated, including commands nested below temporary redirects. */
static fn shell_parser_source_relocated(bipolar from, bipolar to)
{
        if (shell_parser_source_active &&
            shell_parser_source_handle == from)
                shell_parser_source_handle = to;
}

static fn shell_parser_source_end()
{
        shell_parser_isolated_live = false;
        shell_parser_source_active = false;
        shell_parser_source_ambiguous = false;
        shell_parser_source_kind = SHELL_PARSER_SOURCE_MEMORY;
        shell_parser_source_handle = -1;
        shell_parser_source_process = -1;
}

static bool exec_inplace_ready(bool restricted);
#include "builtin.c"

/* Structural forks can execute parsed commands before ordinary dispatch has
   resolved a policy subject. When the policy confines anything at all, seal a
   regular reader in its owning parent before making that copy. A shell whose
   policy confines nothing -- every unconfigured machine, register or not --
   keeps ordinary process semantics: no subreaper, dumpable, streaming its
   script without the copy. */
static fn shell_parser_source_fork_prepare()
{
        /* The snapshot the last launch read, not a fresh one: a restriction
           added since is caught by the final child, which reloads, finds its
           parent unprotected and refuses. Missing it here costs a refusal,
           never an allowance, and a structural fork is not charged a read of
           the register. */
        if (floodlight_confines_any())
        {
                if (!floodlight_parent_prepare(true))
                        return;

                if (shell_parser_source_kind ==
                        SHELL_PARSER_SOURCE_REGULAR_FILE &&
                    shell_parser_source_active &&
                    shell_parser_source_process ==
                        system_call_1(syscall(getpid), 0))
                        (void)shell_parser_source_prepare();
        }
}

/*
        The line being read, which grows to hold whatever arrives.

        This was four kilobytes, and a line longer than that was run truncated
        -- not refused, run -- which is the same silent wrongness the word
        lists had. A generated script with one very long line is the ordinary
        way to meet it.
*/
#define MAX_INPUT_STEP 4096

p8 address_to shell_buffer;
positive shell_buffer_room;

// Expansion and here-documents share this growable byte arena. A word's
// address is taken only after it is complete, so the block may move while the
// word is being built.
p8 address_to token_storage;
positive token_storage_room;
positive token_used;
bool token_overflow;

static bool token_room(positive want)
{
        return shell_array_room(token_storage, token_storage_room, want);
}

// Nothing holds an address inside argv, so it may move as the line grows.
string_address address_to shell_argv;
positive shell_argv_room;
positive shell_argc;

/* The executor supplies these only when argv[0] came unchanged from a parsed
   literal. They let repeated loop commands reuse dispatch work without ever
   trusting the address of lexer storage, which is reused between lines. */
static bool shell_command_name_stable HOT_STATE;
static string_address shell_command_name_address HOT_STATE;
/* parse_program reuses word slots. A remembered builtin is only the same
   command while this generation is unchanged -- a kept loop tree. */
static positive shell_parse_generation HOT_STATE;

p8 address_to argument_line;
positive argument_line_room;

fn token_push(p8 value)
{
        if (!token_room(token_used + 2))
        {
                token_overflow = true;
                return;
        }

        token_storage[token_used++] = value;
}

static fn token_push_bytes(address_any data, positive length)
{
        if (!token_room(token_used + length + 2))
        {
                token_overflow = true;
                return;
        }

        if (length)
                memory_copy_apart(token_storage + token_used, data, length);

        token_used += length;
}

/*
        Assignment syntax is a property of the parsed word, not of an
        execution. Return one for NAME= and two for NAME+=, together with the
        stable name length so the executor can reuse its hash metadata.

        The parser asks this of every word it makes, 1.36 million words in
        reading five configure scripts, test/run and two ltmain.sh, and more
        than half of them begin with a byte no name can: the C called
        string_span for the name and looked at what followed. Here the name
        is walked two bytes a turn in the body, through the same table, and
        the answer taken from the byte it stops at. A subscript, a[i]=v, is
        the lexer's walk to make, and one word in 250 goes on to
        shell_assignment_subscript for it.
*/
p8 shell_assignment_kind(string_address word, positive address_to name_length);

KEEP __attribute__((externally_visible, noinline)) p8
shell_assignment_subscript(string_address word, positive length,
                           positive address_to name_length)
{
        /* A subscript is part of the name being assigned to. a[i+1]=v and
           m[a key]=v each name one element. The lexer's walk says where it
           closes -- a ] held in quotes or a substitution closes nothing --
           and whether = or += follows. An empty subscript names nothing. */
        string_address stop = lex_assignment_subscript_end(word + length + 1);

        if (stop && stop - (word + length) > 2)
                length = (positive)(stop - word);

        if (name_length)
                address_to name_length = length;

        if (string_get(word + length) == '=')
                return 1;

        return string_get(word + length) == '+' &&
                       string_get(word + length + 1) == '='
                   ? 2
                   : 0;
}

#if X64
__asm__(
    ASM_FUNC(shell_assignment_kind)
    "lea string_set_name(%rip), %r8\n   xor %eax, %eax\n"
    "1:  movzbl (%rdi,%rax), %ecx\n   cmpb $0, (%r8,%rcx)\n   je 2f\n"
    "movzbl 1(%rdi,%rax), %ecx\n   cmpb $0, (%r8,%rcx)\n   je 3f\n   add $2, %rax\n   jmp 1b\n"
    "3:  inc %rax\n"
    // The name is %rax bytes and %ecx the byte after it.
    "2:  test %rax, %rax\n   jz 6f\n   movzbl (%rdi), %edx\n   sub $48, %edx\n   cmp $9, %edx\n   jbe 6f\n"
    "cmp $91, %ecx\n   je 8f\n   test %rsi, %rsi\n   jz 4f\n   mov %rax, (%rsi)\n"
    "4:  cmp $61, %ecx\n   je 5f\n   cmp $43, %ecx\n   jne 7f\n   cmpb $61, 1(%rdi,%rax)\n   jne 7f\n"
    "mov $2, %eax\n"
    ASM_RET
    "5:  mov $1, %eax\n"
    ASM_RET
    "6:  test %rsi, %rsi\n   jz 7f\n   mov %rax, (%rsi)\n"
    "7:  xor %eax, %eax\n"
    ASM_RET
    "8:  mov %rsi, %rdx\n   mov %rax, %rsi\n   jmp shell_assignment_subscript\n"
    ASM_END(shell_assignment_kind)
);
#elif ARM64
__asm__(
    ASM_FUNC(shell_assignment_kind)
    "adrp x8, string_set_name\n   add x8, x8, :lo12:string_set_name\n   mov x2, #0\n"
    "1:  ldrb w3, [x0, x2]\n   ldrb w4, [x8, x3]\n   cbz w4, 2f\n   add x2, x2, #1\n"
    "ldrb w3, [x0, x2]\n   ldrb w4, [x8, x3]\n   cbz w4, 2f\n   add x2, x2, #1\n   b 1b\n"
    // The name is x2 bytes and w3 the byte after it.
    "2:  cbz x2, 6f\n   ldrb w5, [x0]\n   sub w5, w5, #48\n   cmp w5, #9\n   b.ls 6f\n"
    "cmp w3, #91\n   b.eq 8f\n   cbz x1, 4f\n   str x2, [x1]\n"
    "4:  cmp w3, #61\n   b.eq 5f\n   cmp w3, #43\n   b.ne 7f\n   add x5, x0, x2\n   ldrb w5, [x5, #1]\n"
    "cmp w5, #61\n   b.ne 7f\n   mov w0, #2\n"
    ASM_RET
    "5:  mov w0, #1\n"
    ASM_RET
    "6:  cbz x1, 7f\n   str x2, [x1]\n"
    "7:  mov w0, #0\n"
    ASM_RET
    "8:  mov x5, x1\n   mov x1, x2\n   mov x2, x5\n   b shell_assignment_subscript\n"
    ASM_END(shell_assignment_kind)
);
#elif RISCV64
__asm__(
    ASM_FUNC(shell_assignment_kind)
    "lla t0, string_set_name\n   li t1, 0\n"
    "1:  add t2, a0, t1\n   lbu t3, 0(t2)\n   add t4, t0, t3\n   lbu t4, 0(t4)\n   beqz t4, 2f\n"
    "addi t1, t1, 1\n   j 1b\n"
    // The name is t1 bytes and t3 the byte after it, at t2.
    "2:  beqz t1, 6f\n   lbu t5, 0(a0)\n   addi t5, t5, -48\n   li t6, 9\n   bleu t5, t6, 6f\n"
    "li t6, 91\n   beq t3, t6, 8f\n   beqz a1, 4f\n   sd t1, 0(a1)\n"
    "4:  li t6, 61\n   beq t3, t6, 5f\n   li t6, 43\n   bne t3, t6, 7f\n   lbu t5, 1(t2)\n"
    "li t6, 61\n   bne t5, t6, 7f\n   li a0, 2\n"
    ASM_RET
    "5:  li a0, 1\n"
    ASM_RET
    "6:  beqz a1, 7f\n   sd t1, 0(a1)\n"
    "7:  li a0, 0\n"
    ASM_RET
    "8:  mv a2, a1\n   mv a1, t1\n   tail shell_assignment_subscript\n"
    ASM_END(shell_assignment_kind)
);
#endif

// The builtins still take the rest of the line as a single string, so the
// words are handed back joined. Quoting survives as far as argv, no further.
string_address shell_arguments()
{
        positive used = 0;
        positive index = 1;

        if (shell_argc < 2)
                return null;

        while (index < shell_argc)
        {
                positive length = string_length(shell_argv[index]);

                if (!shell_array_room(argument_line, argument_line_room, used + length + 2))
                        break;

                if (used)
                        argument_line[used++] = ' ';

                memory_copy(argument_line + used, shell_argv[index], length);
                used += length;
                index++;
        }

        if (!shell_array_room(argument_line, argument_line_room, used + 1))
                return null;

        argument_line[used] = end;

        return argument_line;
}

/*
        A command gets the default disposition back, unless somebody chose
        otherwise.

        Ignored signals cross execve, and this shell ignores interrupt so that
        control-C does not take it down with the command. Handing that
        deafness on would leave the command uninterruptible -- but the
        deafness is the shell's own, and two other decisions outrank it: the
        script's, made with trap '' on the signal, and that of whoever started
        this shell with the signal already ignored. Both mean "the commands
        too", and giving the default back over either made a subshell or a
        spawned program the one thing in the script control-C could reach.
*/
static HOT fn shell_child_default(b32 number)
{
        if (!trap_ignored((positive)number) && !shell_was_ignored(number))
                shell_default(number);
}

DEAD_END fn shell_thread_instance_mode(bool preserve_ignored)
{
        string_address address_to environment;

        if (!preserve_ignored)
        {
                shell_child_default(SIGNAL_INTERRUPT);
                shell_child_default(SIGNAL_QUIT);
        }

        environment = shell_environment();
        if (!environment)
        {
                string_format(log, "failed: no room for environment\n");
                log_flush();
                exit(126);
        }

        string_address path = shell_exec_path ? shell_exec_path : shell_argv[0];
        bipolar exec_result = shell_exec_file(path, shell_argv, shell_argc,
                                              environment);

        if (floodlight_inplace_terminal)
                floodlight_silent_stop();

        /* What execve said, on standard error and in the references' words:
           bash names the file it tried, dash the command and "not found"
           for a file that is not there. A file that vanished after it was
           found -- a stale hashed path -- is not found, 127; anything else
           the kernel refused is 126. This wrote "failed with error: ..." to
           standard output and left 126 either way. */
        {
                bipolar code = exec_result < 0 ? -exec_result : exec_result;
                string_address why = system_error_message(code);

                file_facts facts;

                if (!why)
                        why = (string_address) "No such file or directory";
                if (shell_bash_compat && code == ERROR_ACCESS &&
                    test_facts(path, address_of facts, true) &&
                    (facts.mode & MODE_FORMAT) == MODE_DIRECTORY)
                        why = (string_address) "Is a directory";

                shell_told("%s: %s\n",
                           shell_bash_compat ? path : shell_argv[0],
                           !shell_bash_compat && code == ERROR_NO_ENTRY
                                  ? (string_address) "not found"
                                  : why);
                log_flush();
                exit(code == ERROR_NO_ENTRY ? 127 : 126);
        }
}

DEAD_END fn shell_thread_instance()
{
        shell_thread_instance_mode(false);
}

// Opened once at startup. Spawning through it skips the fork whose address
// space copy execve would only throw away: about 3us per command here.
// Negative means the kernel has no spark device and we fall back to forking.
b32 spawn_device = -1;
static bool spawn_device_opened;

/* A shell redirection can claim any numeric descriptor.  Never trust the
   cached number after user-controlled descriptor work: it is usable only
   while it still names the authenticated Spark character device. */
static bool shell_spawn_device_valid()
{
        file_facts facts;

        return spawn_device >= 0 &&
               file_look(spawn_device, (string_address)"", AT_EMPTY_PATH,
                         address_of facts) &&
               (facts.mode & MODE_FORMAT) == MODE_CHARACTER &&
               facts.rdev_major == SPARK_DEVICE_MAJOR &&
               facts.rdev_minor == SPARK_DEVICE_MINOR;
}

/*
        argv and envp go across as flat blocks of NUL terminated strings.

        Both grow to hold what is being run. They used to be four kilobytes
        each and a command whose words came to more than that was refused --
        which the caller answers by forking instead, so nothing broke, but a
        long command line quietly stopped taking the fast path. What made it
        visible was the line reader growing first: a twenty thousand byte
        echo went from being cut in half to being refused here.
*/
p8 address_to spawn_argv_block;
positive spawn_argv_room;
p8 address_to spawn_envp_block;
positive spawn_envp_room;
positive spawn_envp_used;
positive spawn_envp_count;
positive spawn_envp_generation = positive_max;

/* A spawn vector has one wire shape whether it is argv or envp: a count and
   one packed run of terminated strings. Keep the overflow proof, sizing and
   copy together so the two launch paths cannot disagree about that shape. */
static positive shell_flatten_strings(string_address address_to strings,
                                      p8 address_to address_to block,
                                      positive address_to room,
                                      positive address_to count_out)
{
        positive count = 0;
        positive used = 0;

        while (strings[count])
        {
                positive length;

                /* Reject outside the wire contract before sizing, allocating
                   or copying the rest of an in-memory expansion.  The caller
                   repeats these checks before its 32-bit casts as a final ABI
                   guard, but waiting until then could transiently allocate and
                   copy an arbitrarily large argv or environment merely to take
                   the ordinary fork/exec fallback. */
                if (count >= SPARK_SPAWN_MAX_STRINGS)
                {
                        address_to count_out = positive_max;
                        return 0;
                }

                length = string_length(strings[count++]);
                if (length == positive_max ||
                    length >= SPARK_SPAWN_MAX_BYTES - used)
                {
                        address_to count_out = positive_max;
                        return 0;
                }

                used += length + 1;
        }

        if (used == positive_max ||
            !shell_room((address_any address_to)block, room, used + 1, 1))
        {
                address_to count_out = positive_max;
                return 0;
        }

        used = 0;

        for (positive at = 0; at < count; at++)
                used = (positive)(string_copy_end(address_to block + used,
                                                  strings[at]) -
                                    address_to block) + 1;

        address_to count_out = count;
        return used;
}

//      The environment, flattened into a block that is made to fit it.
positive shell_flatten_env(positive address_to count_out)
{
        string_address address_to environment = shell_environment();
        positive used;

        /* shell_environment already has a precise generation: it changes
           only when an exported value changes.  The flat ioctl block is an
           equally immutable view of that generation, so copying every byte
           again for every command was pure launch overhead. */
        if (!shell_envp_dirty &&
            spawn_envp_generation == shell_envp_generation)
        {
                address_to count_out = spawn_envp_count;
                return spawn_envp_used;
        }

        if (shell_envp_dirty)
        {
                address_to count_out = positive_max;
                return 0;
        }

        used = shell_flatten_strings(environment, address_of spawn_envp_block,
                                     address_of spawn_envp_room, count_out);
        if (address_to count_out == positive_max)
                return 0;

        spawn_envp_used = used;
        spawn_envp_count = address_to count_out;
        spawn_envp_generation = shell_envp_generation;

        return used;
}

static bool shell_spawn_device_open();

static bool shell_spawn_request(struct spawn address_to request,
                                 string_address path,
                                 string_address address_to arguments)
{
        positive argc = 0;
        positive envc = 0;
        positive argv_bytes;
        positive envp_bytes;

        request->path = (unsigned long)path;
        argv_bytes = shell_flatten_strings(
            arguments, address_of spawn_argv_block,
            address_of spawn_argv_room, address_of argc);

        /*
                The in-memory shell stores are deliberately unbounded, while
                Spark's wire ABI uses 32-bit counts and byte lengths and has a
                much smaller copy ceiling.  Decide that the complete request
                fits before narrowing anything into the ABI.  Otherwise a
                multi-gigabyte expansion can wrap argv_bytes/argc into an
                apparently valid small request and execute only a prefix rather
                than taking the ordinary fork/exec fallback.
        */
        if (argc == positive_max || argc > SPARK_SPAWN_MAX_STRINGS ||
            argv_bytes > SPARK_SPAWN_MAX_BYTES)
                return false;

        request->argv = (unsigned long)spawn_argv_block;
        request->argv_bytes = (unsigned int)argv_bytes;
        request->argv_count = (unsigned int)argc;

        envp_bytes = shell_flatten_env(address_of envc);

        /* An allocation failure must take the fork/exec fallback.  Sending a
           syntactically valid request with envc zero silently stripped every
           exported variable from the child instead.  The same pre-narrowing
           rule as argv keeps an oversized environment on that fallback too. */
        if (envc == positive_max || envc > SPARK_SPAWN_MAX_STRINGS ||
            envp_bytes > SPARK_SPAWN_MAX_BYTES)
                return false;

        request->envp = (unsigned long)spawn_envp_block;
        request->envp_bytes = (unsigned int)envp_bytes;
        request->envp_count = (unsigned int)envc;
        request->envp_generation = spawn_envp_generation;
        return true;
}

/* Which of its ignored signals a spawned program keeps: every one the
   shell did not ignore for its own sake. That is interrupt and quit unless
   a trap '', the shell's own start or an asynchronous list ignored them,
   and the stop signals unless a trap '' or the start did. */
static b32 shell_spawn_keeps()
{
        static const struct
        {
                b32 number;
                b32 flag;
        } own[] = {
            {SIGNAL_INTERRUPT, SPARK_SPAWN_KEEP_INTERRUPT},
            {SIGNAL_QUIT, SPARK_SPAWN_KEEP_QUIT},
            {SIGNAL_STOP_KEY, SPARK_SPAWN_KEEP_STOP_KEY},
            {SIGNAL_TTY_INPUT, SPARK_SPAWN_KEEP_TTY_INPUT},
            {SIGNAL_TTY_OUTPUT, SPARK_SPAWN_KEEP_TTY_OUTPUT},
        };
        b32 keeps = SPARK_SPAWN_KEEP_IGNORED;

        for (positive at = 0; at < array_count(own); at++)
                if (trap_ignored((positive)own[at].number) ||
                    shell_was_ignored(own[at].number) ||
                    (at < 2 && exec_asynchronous))
                        keeps |= own[at].flag;

        return keeps;
}

/* Submit a launch whose Floodlight decision the caller already made. Keeping
   policy out of this sender lets paths which need the decision for their fork
   fallback make one stable choice before probing or opening /dev/spark. */
static HOT bipolar shell_spawn_preflighted(b32 flags, string_address path,
                                       string_address address_to arguments,
                                       b32 input, b32 output, b32 error)
{
        struct spawn request;
        bool tool = (flags & SPARK_SPAWN_TOOL) != 0;

        /* An ambiguous parser source cannot be carried into a fresh image;
           the fork path reaches the final fail-closed decision. */
        if (tool && shell_parser_source_ambiguous)
                return -1;

        if (!shell_spawn_device_open())
                return -1;

        if (!shell_spawn_request(address_of request, path, arguments))
                return -1;

        request.flags = flags | shell_spawn_keeps();
        request.stdio[0] = input;
        request.stdio[1] = output;
        request.stdio[2] = error;

        return system_control(spawn_device, SPARK_IOCTL_SPAWN,
                              address_of request);
}

/*
        One pipeline stage, with its three descriptors named in the request.

        The difference from a plain launch is the whole point of it: a stage
        used to be a forked copy of the shell that arranged its own
        descriptors and then replaced itself, and the copy was a page table
        duplicated for an address space the child discards microseconds
        later. Naming them here means the stage is spawned instead, and the
        fork never happens.
*/
bipolar shell_spawn_stage(string_address path,
                          string_address address_to arguments,
                          b32 input, b32 output, b32 error)
{
        return shell_spawn_preflighted(SPARK_SPAWN_SHELL, path,
                                       arguments, input, output, error);
}

static bool shell_spawn_device_open()
{
        if (spawn_device >= 0 && !shell_spawn_device_valid())
        {
                /* The number belongs to somebody else now.  Its owner, not
                   the stale cache, decides when it closes. */
                spawn_device = -1;
                spawn_device_opened = false;
        }

        if (!spawn_device_opened)
        {
                spawn_device = system_open_at(AT_FDCWD,
                                             SPARK_DEVICE,
                                             FILE_READ_WRITE | O_CLOEXEC);
                spawn_device_opened = true;

                if (spawn_device >= 0 && !shell_spawn_device_valid())
                {
                        system_close(spawn_device);
                        spawn_device = -1;
                }
        }

        return spawn_device >= 0;
}

/* A final Floodlight-confined child must not retain or reopen the privileged
   launch device.  CLOEXEC protects the ordinary exec boundary; disabling the
   cache also protects applets which run directly in their forked shell image. */
static fn shell_spawn_device_disable()
{
        if (shell_spawn_device_valid())
                system_close(spawn_device);

        spawn_device = -1;
        spawn_device_opened = true;
}

static fn exec_wait_background(bipolar child);

/* argv[0] selects a utility in the kernel-owned /shell image. The caller has
   already established that this launch is unrestricted. */
static bipolar shell_spawn_tool_preflighted(
    string_address address_to arguments, b32 output, bool quiet)
{
        b32 null_output = -1;
        bipolar child;

        if (quiet)
                null_output = system_open_at(AT_FDCWD,
                                             "/dev/null",
                                             FILE_READ_WRITE | O_CLOEXEC);

        if (quiet && null_output < 0)
                return -1;

        child = shell_spawn_preflighted(SPARK_SPAWN_TOOL, null, arguments, -1,
                                        output, quiet ? null_output : -1);
        if (null_output >= 0)
                system_close(null_output);
        return child;
}

/* Decide before opening either the launch device or the quiet-output sink.
   A restricted command-substitution tool then takes its existing fork path,
   whose child inherits the parser parent's authenticated contract. */
bipolar shell_spawn_tool(string_address address_to arguments,
                         b32 output, bool quiet)
{
        positive count = pointer_vector_count(arguments);

        if (floodlight_launch_decide(null, arguments, count, true,
                                     false, false, null) !=
            FLOODLIGHT_LAUNCH_ALLOW)
                return -1;

        return shell_spawn_tool_preflighted(arguments, output, quiet);
}

fn shell_execute_command()
{
        bipolar child = -1;
        positive count;
        b32 policy;
        string_address path = shell_exec_path ? shell_exec_path
                                              : shell_argv[0];

        log_flush();

        count = pointer_vector_count(shell_argv);
        policy = floodlight_launch_decide(path, shell_argv, count,
                                          false, false, false, null);

        if (policy == FLOODLIGHT_LAUNCH_ALLOW)
                child = shell_spawn_preflighted(SPARK_SPAWN_SHELL,
                                                path, shell_argv,
                                                -1, -1, -1);

        if (child < 0)
        {
                // No spark device, or it refused the request: fall back to the
                // portable path so the shell still works on a stock kernel.
                //
                // clone takes (flags, child_stack, ...). Passing only flags
                // left child_stack as whatever happened to be in the second
                // argument register, so the child started on a garbage stack
                // and execve was handed an empty path.
                child = policy == FLOODLIGHT_LAUNCH_ALLOW
                            ? shell_clone_raw() : shell_clone();

                if (child == 0)
                        shell_thread_instance();
        }

        if (child > 0)
        {
                positive status = 0;
                bipolar waited;

                exec_wait_background(child);
                waited = system_wait4_retry(child, address_of status, 0, null);

                if (waited < 0)
                {
                        string_format(log_error, "failed with error: %s\n",
                                      file_reason(waited));
                        shell_status = 125;
                }
                else
                {
                        shell_child_death(child, status, true);
                        shell_status = wait_status_code(status);
                }

                /*
                        An exit byte is not an execve error channel.

                        A program is allowed to answer 127 itself, and its wait
                        status is bit-for-bit identical to a loader choosing
                        that number. The portable child diagnoses its own
                        failed execve before exiting; guessing here accused a
                        successfully run `/bin/sh -c 'exit 127'` of not having
                        run at all.
                */
        }
        else
                string_format(log_error, "failed with error: %s\n",
                              file_reason(child));

        log_flush();
}

HOT bool shell_builtin(string_address arguments, positive2 named)
{
        static shell_command address_to remembered HOT_STATE;
        static positive remembered_length HOT_STATE;
        static positive remembered_hash HOT_STATE;
        static positive remembered_generation HOT_STATE;
        shell_command address_to command = null;

        if (!arguments && shell_command_name_stable &&
            shell_argv[0] == shell_command_name_address && remembered &&
            !shell_disabled[remembered - shell_commands] &&
            remembered_length == named.y && remembered_hash &&
            remembered_hash == named.x &&
            remembered_generation == shell_parse_generation)
                command = remembered;
        else
        {
                /* A cache hit already proves this name has no slash. Other
                   paths still bypass both builtin and utility indexes. */
                if (memory_first_of(shell_argv[0], '/', named.y))
                        return false;
                command = shell_command_named_hashed(shell_argv[0], named);

                if (!arguments && shell_command_name_stable &&
                    shell_argv[0] == shell_command_name_address && command)
                {
                        remembered = command;
                        remembered_length = named.y;
                        remembered_hash = named.x;
                        remembered_generation = shell_parse_generation;
                }
        }

        if (command)
        {
                /* A normal builtin can evaluate nested commands and consumes
                   the one-command tail privilege. `command` keeps it until
                   its second lookup identifies the command it wraps; a tool
                   miss keeps it too. Keeping this beside the lookup avoids a
                   complete duplicate command-table probe in exec_dispatch. */
                if (command->function != shell_command_builtin)
                        shell_tail_command = false;

                /* This old builtin still consumes a rejoined line.  Everyone
                   else reads argv directly, so do not scan and copy every
                   argument before every ordinary builtin. */
                if (!arguments && command->function == shell_which)
                        arguments = shell_arguments();

                /*
                        A builtin that finishes without an opinion succeeded.

                        The status used to be left exactly as the previous
                        command set it, so "false; echo hi" reported failure
                        and a script whose last act was a successful echo
                        exited non-zero. Answering here rather than in each
                        builtin means the ones that cannot fail do not have to
                        remember to say they did not.
                */
                shell_status_entering = shell_status;
                shell_status = 0;

                shell_exit_was_previous = shell_exit_is_current;
                shell_exit_is_current = command->function == shell_exit;

                if (shell_dash_compat && shell_dash_option_refused(command))
                        return true;

                command->function(log, arguments);
                return true;
        }

        // Commands and utilities are disjoint tables.  A script executes the
        // former far more often, so do not make every echo, test, read and
        // printf pay for a guaranteed miss through all of the utility index.
        if (shell_tool_run_hashed(shell_argv[0], named))
                return true;

        return false;
}

#include "parse.c"
#include "exec.c"

/* The allocations below hold the moving expansion scratch for one top-level
   command. Small capacities remain the allocation-free steady state. A
   completed large expansion sets a command-wide grace: a second large
   command reuses the mappings, while the next small command proves their
   high-water capacity cold and returns it. Chained word stores and long-lived
   builtin streams retain their allocations; this boundary must not turn a
   repeated large-word or large read/printf/mapfile workload into churn. */
static fn shell_command_scratch_relax()
{
        positive array_active = shell_large_request
                                    ? SHELL_SCRATCH_RETAIN + 1
                                    : 0;

        /* Completed argv strings are dead even when their chained backing is
           retained for allocation-free reuse. */
        shell_expand_reset();

        shell_room_relax((address_any address_to)address_of expand_text,
                         address_of expand_text_room, array_active, 1);
        shell_room_relax((address_any address_to)address_of expand_mark,
                         address_of expand_mark_room, array_active, 1);
        shell_large_request = false;
}

/*
        Whether the shell is in the middle of something.

        The reader hands over one line at a time and a while loop is not one
        line. When the parser runs out of tokens inside a construct it says so
        rather than failing, the tokens are kept, and the next line is added to
        them -- which is also how a here-document body is collected, except
        that a body is not source and is taken verbatim until its delimiter.
*/
static bool shell_more;

//      Whether the parser is in the middle of a construct, which is the one
//      thing the reader needs to know to choose between PS1 and PS2.
bool shell_reading_more()
{
        return shell_more;
}

/* Defined above the included readers, which trace under it. */

/*
        A syntax error drops what was parsed and moves the generation, which
        is how a nested reader learns to stop. A terminal recovers at its next
        prompt. A direct script, file, stdin stream or -c string has no
        enclosing builtin to receive the error, so when it is fatal the rest
        of that input must not run; the ordinary fatal boundary still honors
        an installed EXIT trap.
*/
static fn shell_syntax_fatal(b32 status, bool fatal)
{
        shell_status = status;
        parse_reset();
        shell_more = false;
        shell_syntax_generation += 2;
        if (fatal && shell_run_depth == 1 && !shell_source_depth)
        {
                if (string_is(shell_option_flags, 'c'))
                        exec_child_leave(shell_status);
                if (!shell_is_interactive)
                        expand_fatal_status(shell_status);
        }
}

/*
        A command substitution is a command written inside a word, and both
        references parse it where it is written: `echo $(if true); echo
        reached` is a syntax error before anything runs, where this parsed the
        body only when the substitution ran and went on to print reached.
        dash parses a backquoted body the same way; bash leaves that to the
        substitution. Each body is parsed here in a nested frame, as eval
        parses its text, and nothing in it runs; a body's own substitutions
        are checked by the same walk as its words are read.
*/
static bool shell_check_only;

static COLD bool shell_substitution_parses(string_address body,
                                           positive length, bool backquoted)
{
        // Most bodies are a few words: a copy on the stack costs nothing,
        // where a mapping and its release cost more than the parse.
        p8 small[256];
        p8 address_to copy = small;
        positive room = 0;
        positive used = 0;
        positive syntax = shell_syntax_generation;
        bool held = shell_check_only;
        lex_frame frame;

        if (length >= sizeof small)
        {
                copy = null;
                if (!shell_array_room(copy, room, length + 1))
                        return true;
        }
        for (positive at = 0; at < length; at++)
        {
                //      Inside backquotes a backslash keeps only $, ` and \.
                if (backquoted && body[at] == '\\' && at + 1 < length &&
                    (body[at + 1] == '$' || body[at + 1] == '`' ||
                     body[at + 1] == '\\'))
                        at++;
                copy[used++] = body[at];
        }
        copy[used] = end;

        string_address current = exec_current_line;
        bool more = shell_more;

        shell_check_only = true;
        lex_nest_enter(address_of frame);
        run_lines(copy);
        shell_input_end();
        lex_nest_leave(address_of frame);
        shell_check_only = held;
        shell_more = more;
        exec_current_line = current;
        if (room)
                memory_free(copy, room);

        return shell_syntax_generation == syntax;
}

static COLD bool shell_substitutions_parse_word(string_address text,
                                                positive length)
{
        positive at = 0;
        bool quoted = false;

        while (at < length)
        {
                p8 value = text[at];

                if (value == '\\')
                {
                        at += 2;
                        continue;
                }
                //      $'...' ends where the lexer says, past any \' in it:
                //      read as a plain quote it ended early, and a $( after
                //      it was parsed as a command that was only text.
                if (value == '$' && !quoted && at + 1 < length &&
                    text[at + 1] == '\'')
                {
                        string_address shut = lex_dollar_quote_end(text + at + 2);

                        at = (positive)(shut - text) + 1;
                        continue;
                }
                if (value == '\'' && !quoted)
                {
                        string_address shut = memory_first_of(
                            text + at + 1, '\'', length - at - 1);

                        at = shut ? (positive)(shut - text) + 1 : length;
                        continue;
                }
                if (value == '"')
                        quoted = !quoted;
                if ((value == '$' && at + 1 < length && text[at + 1] == '(') ||
                    value == '`')
                {
                        string_address from = text + at + (value == '$');
                        string_address stop = lex_nesting(from);
                        positive until;
                        bool arithmetic = value == '$' && at + 2 < length &&
                                          text[at + 2] == '(';

                        if (!stop || stop == from)
                                return true;
                        until = (positive)(stop - text);
                        if (until > length)
                                return true;
                        if (!arithmetic &&
                            (value == '$' || !shell_bash_compat) &&
                            !shell_substitution_parses(
                                text + at + (value == '$' ? 2 : 1),
                                until - at - (value == '$' ? 3 : 2),
                                value == '`'))
                                return false;
                        at = until;
                        continue;
                }
                at++;
        }
        return true;
}

static COLD bool shell_substitutions_parse(positive from, positive to)
{
        for (positive at = from; at < to; at++)
        {
                parse_token address_to token = parse_tokens + at;

                if ((token->kind != PT_WORD && token->kind != PT_CONDITIONAL) ||
                    !token->text ||
                    (!memory_first_of(token->text, '(', token->length) &&
                     !memory_first_of(token->text, '`', token->length)))
                        continue;
                if (!shell_substitutions_parse_word(token->text,
                                                    token->length))
                        return false;
        }
        return true;
}

static HOT fn run_line_inner(string_address line)
{
        string_address waiting = parse_here_open();
        positive fed_from;
        b32 root;

        // What a job made of a loop or a group is listed under: the words are
        // in the parse tree, but only the reader still has the line. And the
        // count of physical lines, which is the only place there is to keep
        // it: one is what a job is named after, the other what $LINENO reads.
        exec_current_line = line;

        // The lexer's own count of physical lines, which is where the line a
        // command was written on comes from; $LINENO reads the executor's
        // copy of that rather than this running total.
        shell_line_number++;

        // A nested eval or sourced file can hand over more physical lines
        // after one of them failed expansion. They belong to the same outer
        // input line and none may restart execution underneath the failure.
        if (exec_line_aborted())
                return;

        fed_from = parse_token_count;

        if (waiting)
                parse_here_line(line);
        else if (!parse_feed(line))
        {
                if (parse_here_limit_exceeded())
                {
                        shell_syntax_fatal(2, true);
                        return;
                }

                log_error(str("Command line too long\n"));
                parse_reset();
                shell_more = false;
                return;
        }

        if (parse_here_open())
        {
                shell_more = true;
                return;
        }

        if (!waiting && parse_line_continues_list(fed_from))
        {
                shell_more = true;
                return;
        }

        root = parse_program();

        if (parse_state == PARSE_INCOMPLETE)
        {
                shell_more = true;
                return;
        }

        shell_more = false;

        if (parse_state)
        {
                if (parse_here_limit_exceeded())
                {
                        shell_syntax_fatal(2, true);
                        return;
                }

                parse_token address_to tok = parse_look(0);
                bool compound = parse_state == PARSE_COMPOUND_SYNTAX &&
                                shell_bash_compat;

                shell_syntax_where();
                if (shell_bash_compat)
                {
                        //      The end of a line that a construct still
                        //      wanted something after is the token `newline'
                        //      to bash, and end of file is only what it says
                        //      when the input itself ran out.
                        if (compound && lex_compound_bad)
                        {
                                p8 said[4] = {string_get(lex_compound_bad),
                                              0, 0, 0};

                                if (string_get(lex_compound_bad + 1) == said[0] &&
                                    (said[0] == '&' || said[0] == '|' ||
                                     said[0] == ';' || said[0] == '<' ||
                                     said[0] == '>'))
                                        said[1] = said[0];
                                string_format(
                                    log_error,
                                    "syntax error near unexpected token `%s'\n",
                                    said);
                                if (line && string_get(line) &&
                                    (!shell_is_interactive ||
                                     shell_interactive_sourcing()))
                                {
                                        shell_syntax_where();
                                        log_error("`", 1);
                                        log_error(line, 0);
                                        log_error("'\n", 2);
                                }
                        }
                        else if (!tok || tok->kind == PT_END ||
                            tok->kind == PT_NEWLINE || !tok->text)
                        {
                                string_format(
                                    log_error,
                                    "syntax error near unexpected token `newline'\n");
                                if (line && string_get(line) &&
                                    (!shell_is_interactive ||
                                     shell_interactive_sourcing()))
                                {
                                        shell_syntax_where();
                                        log_error("`", 1);
                                        log_error(line, 0);
                                        log_error("'\n", 2);
                                }
                        }
                        else
                        {
                                string_format(
                                    log_error,
                                    "syntax error near unexpected token `%s'\n",
                                    tok->text);
                                /* Bash repeats the prefix and quotes the
                                   physical line the token came from --
                                   except to a person at an interactive
                                   prompt, who can still see it, which
                                   takes in eval there too but not a file
                                   being sourced. */
                                if (line && string_get(line) &&
                                    (!shell_is_interactive ||
                                     shell_interactive_sourcing()))
                                {
                                        shell_syntax_where();
                                        log_error("`", 1);
                                        log_error(line, 0);
                                        log_error("'\n", 2);
                                }
                        }
                }
                /*
                        A token with no spelling is every token the lexer
                        never cut: the newline the parser synthesizes at the
                        end of a line, and an operator slot with no entry in
                        the spelling table. The bash arm already stepped
                        around it; this one reached %s with a null and
                        `sh -c 'cat <<'`, `sh -c 'cat > '`, `sh -c case`
                        and `sh -c for` all died in strlen.
                */
                else if (parse_syntax_reason)
                        string_format(log_error, "Syntax error: %s\n",
                                      parse_syntax_reason);
                else if (!tok || tok->kind == PT_END ||
                         tok->kind == PT_NEWLINE || !tok->text)
                        log_error(str("Syntax error: unexpected end of file\n"));
                else
                        string_format(log_error,
                                      "Syntax error: \"%s\" unexpected\n",
                                      tok->text);
                /* Compound-assignment interior errors are not the fatal
                   status-2 class: bash answers 1 and keeps the rest of the
                   script, and its POSIX mode exits 127 and stops. */
                shell_syntax_fatal(compound ? (shell_posix_on() ? 127 : 1) : 2,
                                   !(compound && !shell_posix_on()));
                return;
        }

        if (shell_check_only)
        {
                (void)shell_substitutions_parse(parse_token_base,
                                                parse_token_count);
                parse_reset();
                return;
        }

        //      bash -c leaves 127 for this one syntax error, where a
        //      script read from a file or standard input leaves 2.
        if (!shell_substitutions_parse(parse_token_base, parse_token_count))
        {
                shell_syntax_fatal(shell_bash_compat &&
                                           string_is(shell_option_flags, 'c')
                                       ? 127
                                       : 2,
                                   true);
                return;
        }

        if (!(shell_options & SHELL_FLAG('n')) || shell_is_interactive)
        {
                bool held_tail = shell_tail_command;

                if (shell_tail_line_requested && root &&
                    parse_nodes[root].kind == NODE_SIMPLE)
                        shell_tail_command = true;

                exec_program(root);
                shell_tail_command = held_tail;
        }
        parse_reset();

        // A signal that arrived while the shell was reading rather than
        // running has no command boundary of its own to wait for.
        if (!exec_line_aborted())
                exec_traps();

        /*
                A terminal wants each line the moment it happens. A script does
                not, and flushing per line is one write system call per line of
                it -- which is where the time in a forty thousand line script
                went. The buffer drains when it fills, before anything is
                spawned, and when the input ends.
        */
        if (shell_is_interactive)
                log_flush();
}

/*
        Top-level lines recover from an interactive expansion error; nested
        lines normally keep carrying it. Recoverable Bash slice arithmetic
        errors instead belong to the innermost reader, including eval/dot.

        Keeping the depth around the entire executor call is what makes an
        eval or a multi-line dot script stop, while the next line read from the
        terminal gets a clean execution signal and still sees $? == 2.
*/
HOT fn run_line(string_address line)
{
        bool top = !shell_run_depth;

        // Hold the depth while recovery dispatches a pending trap. Its action
        // is a nested line and must not recursively begin another top-level
        // recovery before this one has reached the user's next command.
        shell_run_depth++;

        if (top || (exec_input_error() && !expand_discard_whole_line))
                exec_line_begin();

        run_line_inner(line);
        shell_run_depth--;

        /*
                The words a line made die with it -- the outer line's.

                A nested line runs while the command that started it is still
                standing on its own words in the same store: argv, and the
                assignments in front of it that have to be taken back when it
                is over. Resetting from inside a sourced file or a trap action
                handed those words to the next nested line to write over, and
                an export meant for one command was released by name from
                whatever had landed there instead.
        */
        if (top)
        {
                /* A physical line is not necessarily a command boundary. An
                   open quote, compound command or here-document keeps parser
                   state for the next line, and its capacity may be much
                   larger than the live suffix. Only a complete command owns
                   none of the scratch reclaimed above. Expansion retains the
                   old per-physical-line reset because nested execution may
                   have used it even while the outer parse remains open. */
                if (shell_more)
                        shell_expand_reset();
                else
                        shell_command_scratch_relax();
        }
}

/*
        More than one line, run one at a time.

        run_line is one physical line: the lexer stops at a newline, so a trap
        action or an eval argument with a second line lost everything after
        the first. The parser already joins the lines that belong together --
        an open quote, a substitution, a here-document body -- because that
        is how the reader feeds it, so this hands over the same physical lines
        the reader would and leaves the joining to the parser.

        The text is copied before it is cut up, because it may not be there
        by the time the second line runs: a trap action is the trap table's
        own copy and the first line is allowed to be "trap - USR1".

        Bash eval asks for a verbose reprint of each physical line here.
        jobs -x and a trap action use the same walker and are not that
        input, so the echo is only the flag eval sets.
*/
fn run_lines(string_address text)
{
        p8 address_to copy = null;
        positive room = 0;
        positive length;
        string_address at;

        positive syntax = shell_syntax_generation;

        if (!string_first_of(text, '\n'))
        {
                if (shell_verbose_eval_lines && string_get(text))
                        shell_verbose_line(text);
                lex_physical_newline(false);
                run_line(text);
                return;
        }

        length = string_length(text);

        if (length == positive_max ||
            !shell_array_room(copy, room, length + 1))
        {
                log_error(str("No room to run lines\n"));
                shell_status = 2;
                return;
        }

        memory_copy(copy, text, length + 1);
        at = copy;

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

                {
                        positive left = (positive)(copy + length - stop);

                        shell_line_has_more =
                            memory_span_byte(stop, '\n', left) < left;
                }

                // An empty line is a line: it is a body line of a
                // here-document, and it ends a command a backslash held open.
                if (shell_verbose_eval_lines)
                        shell_verbose_line(at);
                run_line(at);
                shell_line_has_more = false;
                if (shell_syntax_generation != syntax)
                        break;
                at = stop;
        }

        memory_free(copy, room);
}

/*
        No next line exists.

        An incomplete parse means "ask the reader for more" only while the
        reader can still answer. At EOF it is a syntax error. Nested readers
        (eval and dot) are special builtins, so their syntax error aborts the
        containing non-interactive shell rather than quietly returning to the
        outer line.
*/
fn shell_input_end()
{
        if (!shell_more)
                return;

        /*
                A here-document the input ended inside of.

                Both dash and bash take the end of the input as the delimiter
                and run the command. Bash says so on stderr; lima dash 0.5.x
                prints nothing. Refusing the line here threw away a script
                whose last line was the body -- which is what a generated one
                looks like when the generator forgot the delimiter, and what
                "cat <<EOF" typed into eval looks like every time.
        */
        if (parse_here_open())
        {
                //      A substitution's body read ahead for its syntax is not
                //      the input bash warns about; its reader will be.
                if (shell_bash_compat && !shell_check_only)
                {
                        positive start = parse_here_start_line();
                        positive now = shell_line_number ? shell_line_number
                                                         : 1;

                        shell_syntax_line_override =
                            parse_here_got_body() ? now : start;
                        shell_diagnostic_where();
                        shell_syntax_line_override = 0;
                        string_format(log_error,
                                      "warning: here-document at line %p "
                                      "delimited by end-of-file (wanted `%s')\n",
                                      start, parse_here_open());
                }

                while (parse_here_open())
                        parse_here_close();

                run_line_inner((string_address) "");

                if (!shell_more)
                        return;
        }

        if (parse_eof_can_complete())
        {
                run_line_inner((string_address) "");

                if (!shell_more)
                        return;
        }

        {
                bool pending = parse_pending_used != 0;
                b32 unfinished = pending ? lex_unfinished(parse_pending)
                                         : LEX_COMPLETE;
                p8 unmatched = pending ? lex_unmatched_now() : 0;
                p8 match[2];
                string_address want = parse_want_now();
                positive now = shell_line_number ? shell_line_number : 1;
                bool word_eof = pending && unfinished == LEX_OPEN_WORD;

                if (shell_bash_compat)
                {
                        if (pending &&
                            (unfinished == LEX_OPEN_WORD || unmatched == '\'' ||
                             unmatched == '"' || unmatched == '`' ||
                             unmatched == '}'))
                                shell_syntax_line_override =
                                    parse_pending_start_line();
                        else
                                shell_syntax_line_override = now + 1;
                }

                shell_syntax_where();
                match[0] = unmatched;
                match[1] = end;

                if (shell_bash_compat)
                {
                        if (unmatched)
                                string_format(log_error,
                                              "unexpected EOF while looking "
                                              "for matching `%s'\n",
                                              match);
                        else if (parse_want_opener_now() && parse_want_line_now())
                                string_format(
                                    log_error,
                                    "syntax error: unexpected end of file from "
                                    "`%s' command on line %p\n",
                                    parse_want_opener_now(),
                                    parse_want_line_now());
                        else
                                log_error(str(
                                    "syntax error: unexpected end of file\n"));
                }
                else if (unmatched == '\'' || unmatched == '"')
                        log_error(str(
                            "Syntax error: Unterminated quoted string\n"));
                else if (unmatched == '`')
                        log_error(str(
                            "Syntax error: EOF in backquote substitution\n"));
                else if (unmatched == '}')
                        log_error(str("Syntax error: Missing '}'\n"));
                else if (unmatched || want)
                        string_format(log_error,
                                      "Syntax error: end of file unexpected "
                                      "(expecting \"%s\")\n",
                                      unmatched ? match : want);
                else
                        log_error(str(
                            "Syntax error: unexpected end of file\n"));

                shell_syntax_line_override = 0;
                parse_reset();
                shell_more = false;
                shell_status = 2;
                shell_syntax_generation += word_eof ? 1 : 2;
        }
}

// A prompt is for somebody watching. Asking the terminal about itself is the
// only way to know whether anybody is: a script piped in gets none, which is
// also what keeps its output free of them.
static b32 shell_interactive()
{
        p8 settings[64];

        return system_control(0, PTY_TCGETS, settings) == 0;
}
