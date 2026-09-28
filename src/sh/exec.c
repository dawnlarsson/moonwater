/*
        The shell's executor.

        One function per node kind, walking the tree the parser built. Nothing
        here re-reads the line and nothing here decides what a word means: the
        shape is settled and the expander is called on the words as they are
        needed, which is why a for loop expands its list once and its body
        every time round.

        break, continue and return are not returns from C. A break in the body
        of a loop is three or four calls below the loop it means, and every one
        of those frames has to unwind without running what came after it, so
        the state travels in exec_signal and every construct that can contain a
        command checks it before going on.
*/

#define EXEC_SIGNAL_NONE 0
#define EXEC_SIGNAL_BREAK 1
#define EXEC_SIGNAL_CONTINUE 2
#define EXEC_SIGNAL_RETURN 3
#define EXEC_SIGNAL_FATAL 4
#define EXEC_SIGNAL_COMMAND_ERROR 5
#define EXEC_SIGNAL_INPUT_ERROR 6
#define EXEC_ASSIGNMENT_LINE_ABORT 256

static b32 exec_signal;
static b32 exec_signal_level;
static b32 exec_loop_depth HOT_STATE;
static b32 exec_function_depth;
static PURE p8 exec_special_kind(string_address name);

/*
        set -e, and the places it does not reach.

        A command that fails ends the shell, unless somebody was going to look
        at the failure anyway: the condition of an if or a loop, everything but
        the last of an && or || list, and a pipeline whose status is inverted.
        POSIX names those three and nothing else, so the flag travels down the
        tree rather than being asked about at each node.

        exec_forked is what a child of a subshell or a pipeline sets. Leaving
        that way is not the shell leaving, and the exit trap belongs to the
        shell.
*/
#define SHELL_ERREXIT ((positive)1 << ('e' - 'a'))
#define SHELL_NOEXEC ((positive)1 << ('n' - 'a'))

static bool exec_tested HOT_STATE;
static bool exec_forked HOT_STATE;
static bool exec_asynchronous HOT_STATE;
static bool exec_pipe_status_pending HOT_STATE;
static b32 exec_pipe_status_value HOT_STATE;
static b32 exec_return_previous HOT_STATE;
/* Set only by a builtin path that diagnosed an invocation error. A nonzero
   status alone is not enough: eval, return, dot and trap can all answer
   nonzero without the POSIX special-builtin fatality applying. */
static bool exec_special_error HOT_STATE;

static fn exec_special_error_note()
{
        exec_special_error = true;
}

/* Whether this process is a child some subshell or pipeline forked. Asked by
   a builtin compiled before this file, which cannot see the flag itself. */
static bool exec_child_process()
{
        return exec_forked;
}

/*
        Which line the command now running was written on.

        The reader's own count is where the input has got to, which is not the
        same thing at all inside a function: a body written twenty lines ago
        is running now, and both a call frame and $LINENO want the line it was
        written on rather than the line that called it.
*/
static b32 exec_line HOT_STATE;
static b32 exec_wait_node HOT_STATE;
static bool exec_lastpipe_live HOT_STATE;
static positive exec_compound_depth HOT_STATE;

fn shell_trap_exit();
fn exec_traps();
fn job_forget();
fn job_reap();
fn trap_child_began();
fn trap_omit_exit_set();
static b32 exec_child_status(bipolar child);
static b32 job_wait_foreground(positive number);
static fn exec_pipe_status_publish(bipolar address_to values, positive count);
static fn exec_coproc_child();
static fn exec_coproc_reaped(bipolar pid);
static fn exec_coproc_drop_finished();
static fn exec_unknown_children_reap();
static fn exec_parent_supervision_relax();
static positive exec_coproc_count HOT_STATE;

/* A wait builtin in a parent-run lastpipe stage may hear that an upstream
   foreground stage exited.  That pid is not a background job, but its status
   must survive until exec_pipe reaches the stage's ordinary waiter. Frames
   nest because the lastpipe stage may itself run another pipeline. */
typedef struct exec_foreground_frame
{
        struct exec_foreground_frame address_to previous;
        bipolar address_to children;
        positive address_to statuses;
        positive count;
} exec_foreground_frame;

static exec_foreground_frame address_to exec_foreground_frames;

static bool exec_foreground_child_changed(bipolar pid, positive status)
{
        exec_foreground_frame address_to frame = exec_foreground_frames;

        while (frame)
        {
                for (positive at = 0; at < frame->count; at++)
                        if (frame->children[at] == pid)
                        {
                                /* A stopped/continued notification is not the
                                   final answer the ordinary pipeline wait
                                   asks for. Consume that notification but
                                   keep waiting for the eventual exit. */
                                if ((status & 0xff) != 0x7f &&
                                    (status & 0xffff) != 0xffff)
                                        frame->statuses[at] = status;
                                return true;
                        }

                frame = frame->previous;
        }

        return false;
}

/* Reset every launcher state which cannot cross a structural fork. A child
   keeps the shell role only when its real parser parent established the full
   protected-supervisor contract before creating it. */
static fn exec_floodlight_child_began()
{
        if (!floodlight_parent_protected ||
            !floodlight_parent_supervised)
                floodlight_parent_role = false;
        floodlight_parent_subreaper = false;
        floodlight_parent_subreaper_owned = false;
        floodlight_parent_dumpable_owned = false;
        floodlight_inplace_requested = false;
        floodlight_inplace_final = false;
        floodlight_inplace_descendants_checked = false;
        floodlight_inplace_terminal = false;
}

static fn exec_saves_child_drop();

fn exec_child_began()
{
        exec_forked = true;
        exec_saves_child_drop();
        exec_floodlight_child_began();
        trap_child_began();
        // One more shell between this process and the one the script began
        // in, which is the whole of what $BASH_SUBSHELL counts.
        shell_subshell_depth++;

        //      What a process substitution left with the shell belongs to the
        //      shell. A fork inherits the list and none of the children on
        //      it, so it would ask about processes that are not its own and
        //      close descriptors somebody else is still handing out.
        shell_substitutions_forget();
        shell_background_child();
        exec_coproc_child();
}

static DEAD_END fn exec_child_leave(b32 status)
{
        shell_status = status;
        shell_trap_exit();
        log_flush();
        exit(status);
}

/*
        An expansion error at a terminal ends this input line, not the shell.

        A forked pipeline, subshell or command substitution can leave outright:
        its parent is the interactive shell that has to survive. In the shell
        itself the signal is carried through the executor like return and
        break, except that no construct is allowed to consume it. The reader
        clears it when the next top-level input line begins.
*/
static bool exec_abort_recoverable;

static COLD fn exec_abort_line(b32 status)
{
        if (exec_forked)
                exec_child_leave(status);

        /*
                The line is over, and under -e so is the shell: bash leaves
                an interactive session on ${y:?}, set -u's unbound name, a bad
                substitution or a readonly assignment the way errexit leaves
                it for any failed command -- only an arithmetic error, which
                is recoverable at a terminal, does not end it. This went on
                reading, so a session run with -e survived the very errors -e
                is there to stop on.
        */
        if (shell_bash_compat && shell_is_interactive && !exec_abort_recoverable &&
            !expand_arithmetic_failing && (shell_options & SHELL_ERREXIT))
                exec_child_leave(status);

        shell_status = status;
        exec_signal = EXEC_SIGNAL_FATAL;
        exec_signal_level = 0;
}

// A recoverable arithmetic expansion error belongs to the reader that
// evaluated it. eval/dot may continue on their next input line and consume
// this signal on return, while functions and loops must keep unwinding.
COLD fn exec_expand_input_error()
{
        exec_abort_recoverable = true;
        exec_abort_line(shell_status);
        exec_abort_recoverable = false;
        exec_signal = EXEC_SIGNAL_INPUT_ERROR;
}

static PURE bool exec_input_error()
{
        return exec_signal == EXEC_SIGNAL_INPUT_ERROR;
}

static fn exec_input_finish()
{
        if (exec_input_error() && !expand_discard_whole_line)
                exec_signal = EXEC_SIGNAL_NONE;

        // eval and a sourced file catch a recoverable expansion error.
        // Leaving expand_failed set re-raised the line abort after eval
        // returned, so `eval '...'; echo eval:$?` never ran the echo.
        expand_failed = false;
}

static fn exec_line_begin()
{
        if (exec_signal == EXEC_SIGNAL_FATAL || exec_input_error())
        {
                exec_signal = EXEC_SIGNAL_NONE;
                expand_discard_whole_line = false;

                // A trap that arrived during the failed expansion belongs
                // between input lines: not to the tail of the aborted one,
                // and not after the first command of the next one.
                exec_traps();
        }
}

static COLD fn exec_special_fail(b32 status)
{
        // Dash's command-wrapped eval/dot catches special-builtin failures
        // at that reader boundary. Carry the error through the existing
        // executor signal, without unwinding unrelated EXIT/trap state.
        if (!shell_bash_compat && shell_command_reader_depth)
        {
                shell_status = status;
                exec_signal = EXEC_SIGNAL_COMMAND_ERROR;
                exec_signal_level = 0;
        }
        else
                expand_fatal_status(status);
}

static fn exec_command_reader_finish()
{
        if (exec_signal == EXEC_SIGNAL_COMMAND_ERROR)
                exec_signal = EXEC_SIGNAL_NONE;
}

static PURE bool exec_line_aborted()
{
        return exec_signal >= EXEC_SIGNAL_FATAL;
}

/*
        A sourced file is a control-flow boundary of its own. Return stops at
        that boundary; break and continue must remain set so the caller's loop
        can consume them. Without this check shell_dot read another physical
        line, exec_program reset the signal, and every command after the
        control builtin ran anyway.
*/
static bool exec_source_stop(b32 address_to startup_status)
{
        if (!exec_signal || (exec_input_error() && !expand_discard_whole_line))
                return false;

        if (exec_signal == EXEC_SIGNAL_RETURN)
        {
                if (startup_status)
                        *startup_status = exec_return_previous;
                exec_signal = EXEC_SIGNAL_NONE;
        }

        return true;
}

/*
        Dash applies errexit to every command in a sourced file, even when
        the `.` itself is tested: bang, if, &&. `! . ./fails` with `false`
        in the file still ends the script. Bash carries the tested flag in,
        so the same line continues and inverts.
*/
static bool exec_source_tested_hold()
{
        bool kept = exec_tested;

        if (!shell_bash_compat)
                exec_tested = false;

        return kept;
}

static fn exec_source_tested_restore(bool kept)
{
        exec_tested = kept;
}

/*
        One of the three conditions the executor raises itself.

        Run the way a caught signal's action is run and with the same care:
        the status the condition was raised on is what the action reads and
        what the shell goes back to afterwards, and a condition raised inside
        an action is not raised again -- a DEBUG trap whose action is a
        command would otherwise never stop.
*/
static bool exec_condition_inside;

/* A line run inside the one being run, with lexer storage of its own or
   only parser marks of its own. */
static fn exec_run_nested(string_address text, bool lexer, positive start)
{
        lex_frame frame;

        /* A trap action's lines are not the script's: counting them moved
           $LINENO on by one for good each time a trap ran. The action counts
           its own from start, the line before its first. */
        positive line = shell_line_number;

        if (lexer)
                lex_nest_enter(address_of frame);
        else
        {
                parse_nest_enter();
                shell_line_number = start;
        }
        run_lines(text);
        shell_input_end();
        if (lexer)
                lex_nest_leave(address_of frame);
        else
        {
                parse_nest_leave();
                shell_line_number = line;
        }
}

static COLD fn exec_trap_condition(positive number)
{
        string_address action = trap_action(number);
        b32 kept_status = shell_status;
        b32 kept_signal = exec_signal;
        b32 kept_level = exec_signal_level;
        bool kept_tested = exec_tested;

        if (!action || !string_get(action) || exec_condition_inside ||
            exec_line_aborted())
                return;

        exec_condition_inside = true;
        exec_signal = EXEC_SIGNAL_NONE;
        exec_tested = false;
        /* An ERR, DEBUG or RETURN action reads the line of the command that
           raised it, as bash's does. */
        {
                positive line = exec_line ? (positive)exec_line
                                          : shell_line_number;

                exec_run_nested(action, false, line ? line - 1 : 0);
        }
        exec_condition_inside = false;

        shell_status = kept_status;
        exec_signal = kept_signal;
        exec_signal_level = kept_level;
        exec_tested = kept_tested;
}

/*
        Whether a condition reaches inside a function.

        Bash keeps DEBUG and RETURN out of functions unless functrace is set
        and ERR out of them unless errtrace is, so that a trap written for the
        script does not fire once per line of every library it sources. A
        subshell is the same question asked of a fork: without errtrace the
        ERR trap belongs to the shell that set it, so "( false )" raises it
        once in the parent and not again in the child.
*/
static PURE bool exec_condition_reaches(positive option)
{
        return (!exec_function_depth && !exec_forked) ||
               shell_extra_on(option);
}

/*
        DEBUG without functrace also stays out of a sourced file: the trap
        belongs to the script that set it, not to every library `.` pulls
        in. RETURN after `.` still uses the looser test above, because a
        sourced file is itself a return boundary even when functrace is off.
*/
static PURE bool exec_debug_reaches()
{
        return (!exec_function_depth && !exec_forked && !shell_dot_depth) ||
               shell_extra_on(SHELL_EXTRA_FUNCTRACE);
}

/*
        What $BASH_COMMAND answers: the simple command about to run, in the
        words it was written in. DEBUG reads it before those words expand,
        and the trap action itself must not replace it.
*/
static p8 exec_bash_command[256];

static COLD string_address exec_bash_command_value(positive address_to value_length)
{
        if (value_length)
                *value_length = string_length(exec_bash_command);

        return exec_bash_command;
}

static positive exec_bash_command_add(positive used, string_address text,
                                      positive length)
{
        if (length > sizeof(exec_bash_command) - 1 - used)
                length = sizeof(exec_bash_command) - 1 - used;
        if (length)
                memory_copy(exec_bash_command + used, text, length);
        return used + length;
}

/*
        bash writes BASH_COMMAND back from its parse tree: the words with one
        blank between them and each redirection as `2> file`; a for, select
        or case as its header, `case a in `; and an arithmetic for as the
        clause about to be evaluated, ((i<1)).
*/
static COLD fn exec_bash_command_from(parse_node address_to node)
{
        positive used = 0;
        b32 at;
        bool header = node->kind == NODE_FOR || node->kind == NODE_SELECT ||
                      node->kind == NODE_CASE;

        if (header)
                used = exec_bash_command_add(
                    used,
                    node->kind == NODE_FOR ? (string_address) "for "
                    : node->kind == NODE_SELECT ? (string_address) "select "
                                                : (string_address) "case ",
                    node->kind == NODE_SELECT ? 7 : node->kind == NODE_FOR ? 4 : 5);

        for (at = 0; at < node->word_count; at++)
        {
                b32 word = node->word + at;

                if (used && !(header && !at) &&
                    used + 1 < sizeof(exec_bash_command))
                        exec_bash_command[used++] = ' ';
                if (header && at == 1 && node->kind != NODE_CASE)
                        used = exec_bash_command_add(
                            used, (string_address) "in ", 3);
                used = exec_bash_command_add(used, parse_words[word],
                                             parse_word_lengths[word]);
                if (node->kind == NODE_CASE)
                        break;
        }
        if (node->kind == NODE_CASE)
                used = exec_bash_command_add(used, (string_address) " in ", 4);
        else if ((node->kind == NODE_FOR || node->kind == NODE_SELECT) &&
                 !node->flags)
                used = exec_bash_command_add(used,
                                             (string_address) " in \"$@\"", 8);

        if (node->kind == NODE_SIMPLE)
                for (at = 0; at < node->redirect_count; at++)
                {
                        parse_redirect address_to want =
                            parse_redirects + node->redirect + at;
                        static const string_address spelling[] = {
                            [OP_DLESS] = "<<", [OP_DGREAT] = ">>",
                            [OP_LESSAND] = "<&", [OP_GREATAND] = ">&",
                            [OP_LESSGREAT] = "<>", [OP_CLOBBER] = ">|",
                            [OP_LESS] = "<", [OP_GREAT] = ">",
                            [OP_ANDGREAT] = "&>", [OP_ANDDGREAT] = "&>>",
                            [OP_HERESTRING] = "<<<"};
                        bool input = want->op == OP_LESS ||
                                     want->op == OP_LESSAND ||
                                     want->op == OP_DLESS ||
                                     want->op == OP_LESSGREAT ||
                                     want->op == OP_HERESTRING;
                        p8 number[24];

                        if (want->op >= (b32)array_count(spelling) ||
                            !spelling[want->op])
                                continue;
                        if (used && used + 1 < sizeof(exec_bash_command))
                                exec_bash_command[used++] = ' ';
                        if (!want->var_length && want->op != OP_ANDGREAT &&
                            want->op != OP_ANDDGREAT &&
                            want->fd != (input ? 0 : 1))
                                used = exec_bash_command_add(
                                    used, number,
                                    positive_into_string(number, want->fd));
                        used = exec_bash_command_add(
                            used, spelling[want->op],
                            string_length(spelling[want->op]));
                        if (want->op != OP_GREATAND && want->op != OP_LESSAND &&
                            want->op != OP_DLESS &&
                            used + 1 < sizeof(exec_bash_command))
                                exec_bash_command[used++] = ' ';
                        used = exec_bash_command_add(used, want->text,
                                                     string_length(want->text));
                }

        exec_bash_command[used] = end;
}

static COLD fn exec_bash_command_clause(string_address clause)
{
        positive used = exec_bash_command_add(0, (string_address) "((", 2);

        clause = arith_skip_space(clause);
        used = exec_bash_command_add(used, clause, string_length(clause));
        used = exec_bash_command_add(used, (string_address) "))", 2);
        exec_bash_command[used] = end;
}

/*
        The DEBUG trap before one command, with $LINENO on that command's
        line: it read the line of the command before, one behind all the
        way down a script. bash raises it before a simple command, before
        (( )) and [[ ]], before case, before each pass of a for or select,
        and before each clause of an arithmetic for.
*/
static fn exec_debug_clause(parse_node address_to node, string_address clause)
{
        if (!trap_debug_here || exec_condition_inside || !exec_debug_reaches())
                return;
        if (node->line)
                exec_line = node->line;
        if (clause)
                exec_bash_command_clause(clause);
        else
                exec_bash_command_from(node);
        exec_trap_condition(TRAP_DEBUG);
}

#define exec_debug_before(node) exec_debug_clause((node), null)

static COLD fn exec_source_return_trap()
{
        if (trap_return_here && exec_condition_reaches(SHELL_EXTRA_FUNCTRACE))
                exec_trap_condition(TRAP_RETURN);
}

static fn exec_errexit(b32 status)
{
        if (exec_line_aborted() || !status || exec_tested)
                return;

        // Where errexit would leave is where the ERR trap runs, whether or
        // not errexit is on: the two ask the same question of the same
        // command.
        if (trap_err_here && exec_condition_reaches(SHELL_EXTRA_ERRTRACE))
                exec_trap_condition(TRAP_ERR);

        if (!(shell_options & SHELL_ERREXIT))
                return;

        exec_child_leave(status);
}


#define REDIRECT_SAVE_MAX 64
//      What a command keeps while it is being built. argv and the saved
//      assignments point in here, so like the expansion store these bytes may
//      never move once handed out.
static shell_store exec_store HOT_STATE;
static p8 exec_nothing[1];

/*
        Job control.

        A job is what one line started: a single command, or a whole pipeline,
        remembered under the number a person sees in brackets. What each of its
        children answered with is already kept in the wait table beside `wait`,
        so a row here holds only what that table has no opinion about -- the
        number, the process group, whether it is stopped, and the text to print
        back.

        A background job gets a row whether or not `set -m` is on, because
        `jobs` and `$!` are answers every shell gives. What the monitor option
        adds is a process group per job and, at a terminal, handing that group
        the terminal and taking it back afterwards.
*/
#define JOB_SIGNAL_CONTINUE 18
#define JOB_SIGNAL_STOP 19
#define JOB_SIGNAL_STOP_KEY 20
#define JOB_SIGNAL_TTY_INPUT 21
#define JOB_SIGNAL_TTY_OUTPUT 22

#define JOB_NO_HANG 1
#define JOB_UNTRACED 2
#define JOB_CONTINUED 8

// The two terminal ioctls are the whole of a shell's claim on a terminal.
// x86 and asm-generic agree on both numbers, which is why they are spelled
// here rather than asked of a header this tree does not have -- the same
// reason TCGETS is spelled beside the prompt.
#define JOB_TERMINAL_GET_GROUP 0x540Fu
#define JOB_TERMINAL_SET_GROUP 0x5410u

#define JOB_RUNNING 0
#define JOB_STOPPED 1
#define JOB_FINISHED 2

// Where the status word sits in a listing, which is bash's column and not a
// number of this shell's choosing: a person reads the two side by side.
// 27 is what bash 5.3.15 on the box writes, in the plain listing and
// under -l alike; this used to be 24.
#define JOB_STATUS_WIDTH 27
#define JOB_SIGNAL_DESC_WIDTH 24

typedef struct
{
        positive number;
        // The last child, which is what $! publishes and what the wait table
        // files every stage of the job under.
        bipolar last;
        bipolar group;
        positive state;
        // The raw wait status of the last stage, kept for the notice that
        // says how the job ended.
        positive status;
        positive stopped_by;
        bool reported;
        bool background;
        bool nohup;
        bipolar owner;
        p8 address_to text;
        positive text_room;
} job_entry;

static job_entry address_to job_table;
static positive job_room;
static positive job_count;
typedef struct
{
        bipolar pid;
        bipolar owner;
} exec_disowned_child;
static exec_disowned_child address_to exec_disowned_children;
static positive exec_disowned_children_room;
static positive exec_disowned_children_count HOT_STATE;

/* Keep the exact direct children removed from the public job/wait tables.
   Reserve the whole transfer first: a failed `disown` can leave the public
   job intact, while a partial transfer would lose the remaining PIDs. */
static bool exec_disowned_remember(job_entry address_to entry)
{
        bipolar own = system_call_1(syscall(getpid), 0);
        positive count = 0;

        if (own <= 0)
                return false;

        /* A fork may discard copied metadata for children owned by its
           parent. It has no wait rows to transfer and must not claim them. */
        if (entry->owner != own)
                return true;

        for (positive at = 0; at < shell_wait_count; at++)
        {
                shell_wait_entry address_to waited = shell_wait_table + at;

                if (waited->job != entry->last ||
                    (waited->flags & SHELL_WAIT_DONE))
                        continue;

                count++;
        }

        if (!count)
                return entry->state == JOB_FINISHED;
        if (count > positive_max - exec_disowned_children_count ||
            !shell_array_room(exec_disowned_children,
                              exec_disowned_children_room,
                              exec_disowned_children_count + count))
                return false;

        for (positive at = 0; at < shell_wait_count; at++)
        {
                shell_wait_entry address_to waited = shell_wait_table + at;
                exec_disowned_child address_to child;

                if (waited->job != entry->last ||
                    (waited->flags & SHELL_WAIT_DONE))
                        continue;

                child = exec_disowned_children +
                        exec_disowned_children_count++;
                child->pid = waited->pid;
                child->owner = own;
        }

        return true;
}

static bool exec_disowned_kept(bipolar own)
{
        positive into = 0;
        bool live = false;

        for (positive at = 0; at < exec_disowned_children_count; at++)
        {
                exec_disowned_child child = exec_disowned_children[at];
                positive status = 0;
                bipolar got;

                /* A fork inherits bookkeeping for children it does not own.
                   It may discard those copied rows without touching them. */
                if (child.owner != own)
                        continue;

                got = system_wait4_retry(child.pid, address_of status,
                                         JOB_NO_HANG, null);
                if (got == child.pid || got == ERROR_NO_CHILDREN)
                        continue;

                exec_disowned_children[into++] = child;
                live = true;
        }

        exec_disowned_children_count = into;
        return live;
}

/* Prepare the one transition which has no protected parent after success.
   A restricted nonfinal decision must already have established the verified
   contract. The descendant check then releases only shell-owned process
   attributes. Final policy may use this one-shot marker; if it starts
   irreversible confinement, any failed exec ends the process. */
static bool exec_inplace_ready(bool restricted)
{
        bipolar own = system_call_1(syscall(getpid), 0);

        floodlight_inplace_final = false;
        floodlight_inplace_terminal = false;
        floodlight_inplace_descendants_checked = false;

        /* An ordinary stock exec retains normal shell semantics. A child
           inventory is security-relevant only once this target needs final
           confinement or an earlier confined descendant is still supervised. */
        if (!restricted && !floodlight_parent_supervised)
        {
                floodlight_inplace_requested = true;
                floodlight_inplace_final = true;
                return true;
        }

        if (own <= 0)
                return false;

        /* In-place replacement transfers every unreaped child to the target,
           even when this command itself is unrestricted. Poll exact owners
           first, then use authenticated procfs as the final liveness answer. */
        if (job_count)
                job_reap();
        (void)exec_disowned_kept(own);
        exec_coproc_drop_finished();
        shell_substitutions_reap();
        if (floodlight_parent_supervised)
                exec_unknown_children_reap();

        if (floodlight_descendants_blocking())
                return false;

        if (floodlight_parent_supervised &&
            !floodlight_parent_release_subreaper())
                return false;
        floodlight_inplace_requested = true;
        floodlight_inplace_final = true;
        floodlight_inplace_descendants_checked = true;
        return true;
}
static positive job_current HOT_STATE;
static positive job_previous HOT_STATE;

// The line the executor is running, for the jobs a rendering of the words
// cannot describe. Set by the reader, because that is the only place the
// bytes a person actually typed are still together.
string_address exec_current_line;

// The descriptor the terminal is reached through, and whether this shell may
// hand it over. A script has process groups without a terminal, so the two
// are separate questions.
static b32 job_terminal = -1;
static bipolar job_shell_group;
static bool job_terminal_owned;
static bool job_monitor_ready;

// Set by the handler and read by the executor. A signal may arrive between
// any two instructions, so the compiler is told the value can change under it.
#define SIGNAL_CHILD 17
static volatile bool job_child_news;
static bool job_child_watching;

static fn job_child_watch();

/*
        Whether this process is the shell that owns the jobs.

        A subshell is inside somebody else's job already. Giving its commands
        groups of their own would split that job in two, and handing one of
        them the terminal would take it from the job the person is looking at
        -- so a fork answers no, and its pipelines go back to being spawned
        rather than forked.
*/
static PURE bool job_monitor()
{
        return !exec_forked && shell_option_on(SHELL_OPTION_MONITOR);
}

/* An implicit final-command exec may remove the shell only when no state
   needs it after the command returns.  A monitored command needs the shell
   to own its process group, and any recorded trap may need a command-boundary
   or EXIT action.  Explicit `exec` keeps its separate, deliberate semantics. */
static PURE bool exec_tail_line_safe()
{
        return !job_monitor() && !trap_count;
}

static bipolar job_group_of(bipolar process)
{
        return system_call_1(syscall(getpgid), (positive)process);
}

static bipolar job_group_set(bipolar process, bipolar group)
{
        return system_call_2(syscall(setpgid), (positive)process,
                             (positive)group);
}

static bipolar job_signal(bipolar target, positive number)
{
        return system_call_2(syscall(kill), (positive)target, number);
}

static bipolar job_terminal_group()
{
        b32 group = 0;

        if (job_terminal < 0 ||
            system_control(job_terminal, JOB_TERMINAL_GET_GROUP,
                           address_of group) < 0)
                return -1;

        return group;
}

/*
        Handing the terminal over, and taking it back.

        tcsetpgrp from a process outside the terminal's foreground group is
        itself a SIGTTOU, which would stop the shell in the middle of starting
        the job it is handing over to. Ignoring that signal for as long as the
        monitor is on is why the call needs no guard.
*/
static fn job_terminal_give(bipolar group)
{
        b32 wanted = (b32)group;

        if (!job_terminal_owned || job_terminal < 0 || group <= 0)
                return;

        system_control(job_terminal, JOB_TERMINAL_SET_GROUP,
                       address_of wanted);
}

/*
        The monitor, turned on.

        Job control asks three things of the shell itself: a process group of
        its own, so that a job's group is never also the shell's; the
        terminal; and deafness to the three signals that would otherwise stop
        it while it arranges either. A script gets the first and neither of
        the other two, which is the whole of what `set -m` means with nobody
        watching.
*/
static fn job_monitor_start()
{
        bipolar own;

        if (job_monitor_ready)
                return;

        job_monitor_ready = true;
        job_shell_group = job_group_of(0);

        if (!shell_is_interactive)
                return;

        /* Asked first, so that what the shell was started with is still
           what is answered once these are its own ignores. */
        (void)shell_was_ignored(JOB_SIGNAL_TTY_OUTPUT);
        (void)shell_was_ignored(JOB_SIGNAL_TTY_INPUT);
        (void)shell_was_ignored(JOB_SIGNAL_STOP_KEY);
        shell_ignore(JOB_SIGNAL_TTY_OUTPUT);
        shell_ignore(JOB_SIGNAL_TTY_INPUT);
        shell_ignore(JOB_SIGNAL_STOP_KEY);

        job_terminal = 0;
        own = system_call_1(syscall(getpid), 0);

        /* A shell sharing a group takes one of its own. Failing is not fatal:
           it keeps working and simply never arbitrates the terminal, which is
           what a shell started without one already does. */
        if (job_shell_group != own && job_group_set(0, own) == 0)
                job_shell_group = own;

        job_terminal_owned = job_terminal_group() >= 0;
        job_terminal_give(job_shell_group);
}

// `set +m`: the next job is started the POSIX way, and the shell stops
// answering for a terminal it no longer arbitrates.
static fn job_monitor_stop()
{
        job_monitor_ready = false;
        job_terminal_owned = false;
        job_terminal = -1;
}

/* Dash will not turn the monitor on without a controlling terminal.
   /dev/tty is that question: stdin being a pipe is not enough, and
   neither is stderr. lima 0.5.x says so and leaves `m` off, status 0.
   The answer is a property of this process, so it is remembered: a
   script that `set -m`s twice must not open /dev/tty twice. */
static b32 job_tty_known;

static bool job_tty_reachable()
{
        bipolar handle;

        if (job_tty_known)
                return job_tty_known > 0;

        handle = system_open_at(AT_FDCWD, "/dev/tty", FILE_READ_WRITE);

        if (handle < 0)
        {
                job_tty_known = -1;
                return false;
        }

        system_close(handle);
        job_tty_known = 1;
        return true;
}

fn job_monitor_told(bool on)
{
        if (on)
        {
                if (!shell_bash_compat && !job_tty_reachable())
                {
                        shell_diagnostic_where();
                        if (shell_argv && shell_argv[0] &&
                            string_equals(shell_argv[0], "set"))
                                string_format(log_error,
                                    "set: can't access tty; job control turned off\n");
                        else
                                string_format(log_error,
                                    "can't access tty; job control turned off\n");
                        shell_options &= ~SHELL_FLAG('m');
                        return;
                }

                job_monitor_start();
        }
        else
                job_monitor_stop();
}

// How many jobs the shell holds, which a prompt's \j says.
positive shell_job_count()
{
        return job_count;
}

static positive job_find(positive value, bool process)
{
        for (positive at = 0; at < job_count; at++)
                if ((process ? (positive)job_table[at].last : job_table[at].number) == value)
                        return at;

        return job_count;
}

/*
        Which job `%+` and `%-` mean.

        The most recently started or stopped job is current and the one before
        it is previous. When a job leaves, the previous one takes its place and
        the highest-numbered job that is neither becomes previous -- which is
        the order the marks are printed in and the order `fg` with no operand
        follows.
*/
static fn job_mark(positive number)
{
        if (job_current == number)
                return;

        job_previous = job_current;
        job_current = number;
}

static fn job_marks_settle()
{
        if (job_current && job_find(job_current, false) == job_count)
        {
                job_current = job_previous;
                job_previous = 0;
        }

        if (!job_current || job_find(job_current, false) == job_count)
        {
                job_current = 0;

                for (positive at = 0; at < job_count; at++)
                        if (job_table[at].number > job_current)
                                job_current = job_table[at].number;
        }

        if (job_previous == job_current ||
            (job_previous && job_find(job_previous, false) == job_count))
                job_previous = 0;

        if (!job_previous)
                for (positive at = 0; at < job_count; at++)
                {
                        positive number = job_table[at].number;

                        if (number != job_current && number > job_previous)
                                job_previous = number;
                }
}

static fn job_drop_at(positive at)
{
        if (at >= job_count)
                return;

        if (job_table[at].text)
                memory_free(job_table[at].text, job_table[at].text_room);

        job_count--;

        if (at < job_count)
                memory_copy(job_table + at, job_table + at + 1,
                            (job_count - at) * sizeof(job_table[0]));

        job_marks_settle();

        // A shell with no jobs left starts numbering again, which is what
        // makes the first job of the next command [1] rather than [57].
        if (!job_count)
        {
                job_current = 0;
                job_previous = 0;
        }
}

// A subshell inherits $! but not the right to wait for anybody, so it must
// not inherit a table whose numbers name processes that are not its children.
fn job_forget()
{
        while (job_count)
                job_drop_at(job_count - 1);

        exec_disowned_children_count = 0;
        job_current = 0;
        job_previous = 0;
}

static bool shell_bytes_add(byte_store address_to store,
                            string_address text, positive length)
{
        if (length > positive_max - store->used - 1 ||
            !byte_store_reserve(store, store->used + length + 1, 256))
                return false;

        memory_copy_apart(store->bytes + store->used, text, length);
        store->used += length;
        store->bytes[store->used] = end;
        return true;
}

static bool shell_bytes_byte(byte_store address_to store, p8 value)
{
        return shell_bytes_add(store, address_of value, 1);
}

/*
        The text a job is listed under.

        The parser keeps words, not the line they came from, so the line is
        written again out of them: a simple command is its own words, and a
        pipeline is its stages with a bar between. Anything else -- a loop, a
        subshell, a group -- has no word list of its own, and the source line
        it came from is closer to what a person typed than any rendering this
        could invent for it.
*/
static positive job_text_add(p8 address_to address_to into,
                             positive address_to room, positive used,
                             string_address text, positive length)
{
        byte_store store = {address_to into, address_to room, used};

        if (text && shell_bytes_add(address_of store, text, length))
        {
                address_to into = store.bytes;
                address_to room = store.room;
        }

        return store.used;
}

static positive job_text_line(p8 address_to address_to into,
                              positive address_to room, positive used)
{
        positive length;

        if (!exec_current_line)
                return used;

        length = string_length(exec_current_line);

        // The ampersand that made this a job is not part of the command, and
        // a listing puts its own back when the job is still running.
        while (length && (exec_current_line[length - 1] == ' ' ||
                          exec_current_line[length - 1] == '\t' ||
                          exec_current_line[length - 1] == '&'))
                length--;

        return job_text_add(into, room, used, exec_current_line, length);
}

static positive job_text_node(p8 address_to address_to into,
                              positive address_to room, positive used,
                              b32 node, b32 depth)
{
        b32 kind = parse_nodes[node].kind;
        b32 child;

        if (kind == NODE_SIMPLE)
        {
                for (b32 at = 0; at < parse_nodes[node].word_count; at++)
                {
                        b32 word = parse_nodes[node].word + at;

                        if (at)
                                used = job_text_add(into, room, used,
                                                    (string_address) " ", 1);

                        used = job_text_add(into, room, used,
                                            parse_words[word],
                                            parse_word_lengths[word]);
                }

                return used;
        }

        if (kind == NODE_PIPELINE)
        {
                if (parse_nodes[node].flags)
                        used = job_text_add(into, room, used,
                                            (string_address) "! ", 2);

                for (child = parse_nodes[node].left; child;
                     child = parse_nodes[child].next)
                {
                        used = job_text_node(into, room, used, child,
                                             depth + 1);

                        if (parse_nodes[child].next)
                                used = job_text_add(into, room, used,
                                                    (string_address) " | ", 3);
                }

                return used;
        }

        if (kind == NODE_SUBSHELL || kind == NODE_GROUP)
        {
                bool braces = kind == NODE_GROUP;

                used = job_text_add(into, room, used,
                                    braces ? (string_address) "{ "
                                           : (string_address) "( ",
                                    2);

                for (child = parse_nodes[node].left; child;
                     child = parse_nodes[child].next)
                {
                        used = job_text_node(into, room, used, child,
                                             depth + 1);

                        if (parse_nodes[child].next)
                                used = job_text_add(into, room, used,
                                                    (string_address) "; ", 2);
                }

                return job_text_add(into, room, used,
                                    braces ? (string_address) "; }"
                                           : (string_address) " )",
                                    3 - (braces ? 0 : 1));
        }

        if (kind == NODE_LIST || kind == NODE_ANDOR)
        {
                for (child = parse_nodes[node].left; child;
                     child = parse_nodes[child].next)
                {
                        b32 op = parse_nodes[child].op;

                        if (child != parse_nodes[node].left)
                                used = job_text_add(
                                    into, room, used,
                                    op == OP_AND_IF
                                        ? (string_address) " && "
                                        : op == OP_OR_IF
                                              ? (string_address) " || "
                                              : (string_address) "; ",
                                    op == OP_AND_IF || op == OP_OR_IF ? 4 : 2);

                        used = job_text_node(into, room, used, child,
                                             depth + 1);
                }

                return used;
        }

        /* A coprocess is named as bash names it: coproc, its name, and its
           body -- never the line it was on, which named it by the whole -c
           string, every command after it included. A plain command has no
           name written before it, and bash writes none. */
        if (kind == NODE_COPROC)
        {
                b32 body = parse_nodes[node].left;

                used = job_text_add(into, room, used, (string_address) "coproc ", 7);
                if (parse_nodes[node].word_count &&
                    !(body && parse_nodes[body].kind == NODE_SIMPLE &&
                      word_is(parse_words[parse_nodes[node].word], "COPROC")))
                {
                        used = job_text_add(into, room, used,
                                            parse_words[parse_nodes[node].word],
                                            parse_word_lengths[parse_nodes[node].word]);
                        used = job_text_add(into, room, used, (string_address) " ", 1);
                }
                return job_text_node(into, room, used, parse_nodes[node].left,
                                     depth + 1);
        }

        /* A loop, a case or an if has no rendering here worth inventing. The
           line it came from is what a person typed, and it is only the whole
           truth when the job is the whole line. */
        return depth ? used : job_text_line(into, room, used);
}

/*
        A job, remembered.

        Its children are already in the wait table under the last of them, so
        this adds only the row that names them collectively. Whether the job is
        in the background is the caller's to say, because a stopped foreground
        job is a job too and must not be listed with an ampersand it never had.
*/
static positive job_started(bipolar address_to children, positive count,
                            bipolar group, b32 node, bool chain,
                            bool background)
{
        job_entry address_to entry;
        positive at;

        if (!count || children[count - 1] <= 0)
                return 0;

        /* A process identifier is reusable once the kernel has reaped its
           last owner. The new job owns the name; a stale row still carrying
           it does not. */
        at = job_find(children[count - 1], true);

        if (at < job_count)
                job_drop_at(at);

        if (!shell_array_room(job_table, job_room, job_count + 1))
                return 0;

        entry = job_table + job_count++;

        /* The lowest number no listed job holds: a finished job that has
           been reported gives its number back, so the next job after [2]
           is gone is [2] again, as bash and dash both say. */
        entry->number = 0;
        for (positive candidate = 1; !entry->number; candidate++)
        {
                positive taken = 0;

                while (taken < job_count - 1 &&
                       job_table[taken].number != candidate)
                        taken++;
                if (taken == job_count - 1)
                        entry->number = candidate;
        }
        entry->last = children[count - 1];
        entry->group = group;
        entry->state = JOB_RUNNING;
        entry->status = 0;
        entry->stopped_by = 0;
        /* New jobs are news for `jobs -n`. Interactive job_report still
           stays quiet about a running one: that was already announced by
           the `&` line. */
        entry->reported = false;
        entry->background = background;
        entry->nohup = false;
        entry->owner = system_call_1(syscall(getpid), 0);
        entry->text = null;
        entry->text_room = 0;

        job_child_watch();

        if (node < 0)
        {
                positive used = 0;

                /* A command already expanded into argv has no node to walk,
                   and the words it is about to run describe it better than
                   the ones it was written with. */
                for (positive word = 0; word < shell_argc; word++)
                {
                        if (word)
                                used = job_text_add(address_of entry->text,
                                                    address_of entry->text_room,
                                                    used,
                                                    (string_address) " ", 1);

                        used = job_text_add(address_of entry->text,
                                            address_of entry->text_room, used,
                                            shell_argv[word],
                                            string_length(shell_argv[word]));
                }
        }
        else if (node && !chain)
                job_text_node(address_of entry->text,
                              address_of entry->text_room, 0, node, 0);
        else if (node)
        {
                positive used = 0;
                b32 stage = node;

                /* A pipeline reaches this as its list of stages rather than
                   as the node above them, because that is what the executor
                   was handed and what its children were made from. */
                while (stage)
                {
                        used = job_text_node(address_of entry->text,
                                             address_of entry->text_room,
                                             used, stage,
                                             parse_nodes[node].next ? 1 : 0);
                        stage = parse_nodes[stage].next;

                        if (stage)
                                used = job_text_add(address_of entry->text,
                                                    address_of entry->text_room,
                                                    used,
                                                    (string_address) " | ", 3);
                }
        }

        job_mark(entry->number);

        /*
                Bash names a job the moment it puts one in the background,
                with the number it will be asked about by and the pid of
                its last stage: "[1] 4242" and nothing else. It only says
                so where somebody is watching, so a script sees nothing.
                dash names it at the prompt afterwards, along with every
                other change, which is the report below and not this.
        */
        if (background && shell_bash_compat && shell_is_interactive)
        {
                string_format(log, "[%p] %b\n", entry->number, entry->last);
                log_flush();
        }

        return entry->number;
}

/*
        Retaining a job's children without publishing $!.

        A background job is what `$!` names; a foreground one that stopped is
        not, and bash agrees -- control-Z does not change the value a script
        reads back. The wait table is the same table either way, so the value
        is put back rather than the retention being written twice.
*/
static bool job_retain(bipolar address_to children, positive count,
                       bool pipefail, bool invert, bool publish)
{
        bipolar kept = shell_background_last;
        bool held = shell_background_started(children, count, pipefail,
                                             invert);

        if (!publish)
                shell_background_last = kept;

        return held;
}

// Whether the wait table still owes an answer for this job at all, and how
// many of its children the kernel has yet to speak about.
static positive job_children(bipolar last, bool running)
{
        positive rows = 0;

        for (positive at = 0; at < shell_wait_count; at++)
                rows += shell_wait_table[at].job == last &&
                        !(running &&
                          (shell_wait_table[at].flags & SHELL_WAIT_DONE));

        return rows;
}

/*
        The identifiers of a foreground pipeline, kept while its answers are
        being written over them.

        PIPESTATUS is published out of the vector the children were started
        in, so by the time a stop is noticed the identifiers are gone -- and a
        pipeline that stopped is a job, which is named by its children and not
        by what they answered.
*/
static bipolar address_to job_held;
static positive job_held_room;

/* Every allocation needed to retain a child is completed before fork.  A
   monitored foreground pipeline additionally needs a stable PID copy if it
   stops; background pipelines retain directly from their launch vector. */
static bool job_reserve(positive count, bool stopped_pipeline)
{
        if (!count || job_count == positive_max ||
            !shell_background_reserve(count) ||
            !shell_array_room(job_table, job_room, job_count + 1))
                return false;

        return !stopped_pipeline ||
               shell_array_room(job_held, job_held_room, count);
}

static bool job_hold(bipolar address_to children, positive count)
{
        if (!count || !shell_array_room(job_held, job_held_room, count))
                return false;

        memory_copy_apart(job_held, children, count * sizeof(children[0]));

        return true;
}

static bipolar job_first_child(job_entry address_to entry)
{
        for (positive at = 0; at < shell_wait_count; at++)
                if (shell_wait_table[at].job == entry->last)
                        return shell_wait_table[at].pid;

        return entry->group > 0 ? entry->group : entry->last;
}

/*
        One child changed, and what that means for the job holding it.

        A stop is not a death. Filing it in the wait table would let `wait`
        answer for a process that is still there and would take the job off
        the screen while it is stopped on it, so only an exit or a signal is
        handed on.
*/
static fn job_child_changed(bipolar pid, positive status)
{
        bool stopped = (status & 0xff) == 0x7f;
        bool continued = (status & 0xffff) == 0xffff;
        positive at;

        if (exec_foreground_child_changed(pid, status))
                return;

        for (at = 0; at < job_count; at++)
                if (job_table[at].last == pid ||
                    (job_table[at].group > 0 &&
                     job_group_of(pid) == job_table[at].group))
                        break;

        if (stopped)
        {
                if (at < job_count && job_table[at].state != JOB_STOPPED)
                {
                        job_table[at].state = JOB_STOPPED;
                        job_table[at].stopped_by = (status >> 8) & 0xff;
                        job_table[at].reported = false;
                        job_mark(job_table[at].number);
                }

                return;
        }

        if (continued)
        {
                if (at < job_count && job_table[at].state == JOB_STOPPED)
                        job_table[at].state = JOB_RUNNING;

                return;
        }

        shell_background_reaped(pid, status);
        exec_coproc_reaped(pid);

        at = job_find(pid, true);

        if (at < job_count)
                job_table[at].status = status;

        for (at = 0; at < job_count;)
        {
                job_entry address_to entry = job_table + at;

                if (entry->state == JOB_FINISHED || !job_children(entry->last, false) ||
                    job_children(entry->last, true))
                {
                        at++;
                        continue;
                }

                entry->state = JOB_FINISHED;
                entry->reported = false;

                /* Bash with a monitor writes a SIGKILL death on stderr
                   and forgets the row, so `jobs` after `kill -KILL %1`
                   is empty. Terminated jobs stay in the table for the
                   next `jobs`. The wait table stays either way. */
                if (shell_bash_compat && !shell_posix_on() && job_monitor() &&
                    (entry->status & 0x7f) == 9)
                {
                        shell_child_death(entry->last, entry->status, false);
                        job_drop_at(at);
                        continue;
                }

                at++;
        }
}

/*
        Everything the retained children have to say, taken without waiting.

        Waiting for "any child" here can consume a foreground pipeline stage
        whose pid still lives only in exec_pipe's local vector.  Its explicit
        waiter then sees ECHILD and reports status 1 instead of the stage's
        real answer.  The wait table is the complete set this asynchronous
        sweep owns, so ask for those pids one by one.

        WUNTRACED and WCONTINUED are what make a stopped job visible to a
        shell that is not waiting for it: without them a control-Z in a
        pipeline stage looks like nothing at all until somebody asks.
*/
fn job_reap()
{
        job_child_news = false;

        for (positive at = 0; at < shell_wait_count; at++)
        {
                shell_wait_entry address_to entry = shell_wait_table + at;
                bipolar pid;

                if (entry->flags & SHELL_WAIT_DONE)
                        continue;

                do
                {
                        positive status = 0;

                        pid = system_wait4_retry(
                            entry->pid, address_of status,
                            JOB_NO_HANG | JOB_UNTRACED | JOB_CONTINUED, null);
                        if (pid > 0)
                                job_child_changed(pid, status);
                }
                while (pid > 0 &&
                       !(entry->flags & SHELL_WAIT_DONE));
        }
}

/*
        Sweeping only when there is something to sweep.

        A shell that asks the kernel between commands learns late: a
        substitution reads the table in a fork of this process and can only
        know what the fork already knew, so `$(jobs)` reported a job that had
        finished two commands ago as running. Asking before every command
        instead is a system call per command for as long as anything is in the
        background, which a loop beside a background job pays for every turn.

        SIGCHLD is the answer to both. The handler does nothing but say that
        there is news, so the sweep itself stays where it can safely walk the
        tables, and a shell with nothing to hear makes no call at all.
*/
static fn job_child_arrived(b32 number)
{
        (void)number;

        job_child_news = true;
}

static fn job_child_watch()
{
        if (job_child_watching)
                return;

        job_child_watching = true;
        system_signal_install(SIGNAL_CHILD, (positive)job_child_arrived,
                              SIGNAL_CATCH_FLAGS, SIGNAL_CATCH_RESTORER,
                              null);
}

fn job_notice()
{
        if (!job_child_news)
                return;

        /* The wait table owns child statuses even when allocating the
           descriptive jobs row failed after a child was retained. */
        if (shell_wait_count)
                job_reap();
        else
                job_child_news = false;

        if (exec_disowned_children_count)
        {
                bipolar own = system_call_1(syscall(getpid), 0);

                if (own > 0)
                        (void)exec_disowned_kept(own);
        }

        shell_substitutions_reap();
        exec_coproc_drop_finished();
        exec_unknown_children_reap();
}

/* Whether any job is stopped, which is what an interactive shell asks before
   it agrees to leave. Asked by the exit builtin, compiled before this. */
static bool job_any_stopped()
{
        for (positive at = 0; at < job_count; at++)
                if (job_table[at].state == JOB_STOPPED)
                        return true;

        return false;
}

/*
        What a signal is called when a job ends by one.

        These are the descriptions a person reads next to the job number, and
        they are the C library's rather than the shell's own short names, which
        is why "TERM" appears in `kill -l` and "Terminated" appears here.
*/
static string_address job_signal_words[] = {
    null,          "Hangup",
    "Interrupt",   "Quit",
    "Illegal instruction",
    "Trace/breakpoint trap",
    "Aborted",     "Bus error",
    "Floating point exception",
    "Killed",      "User defined signal 1",
    "Segmentation fault",
    "User defined signal 2",
    "Broken pipe", "Alarm clock",
    "Terminated",  null,
    "Child exited", "Continued",
    "Stopped (signal)",
    "Stopped",     "Stopped (tty input)",
    "Stopped (tty output)",
    "Urgent I/O condition",
    "CPU time limit exceeded",
    "File size limit exceeded",
    "Virtual timer expired",
    "Profiling timer expired",
    "Window changed",
    "I/O possible",
    "Power failure",
    "Bad system call",
};

#define JOB_SIGNAL_WORDS (array_count(job_signal_words))

static positive job_signal_named(positive number, p8 address_to into)
{
        if (number < JOB_SIGNAL_WORDS && job_signal_words[number])
        {
                string_copy(into, job_signal_words[number]);
                return string_length(job_signal_words[number]);
        }

        string_copy(into, "Signal ");
        positive_into_string(into + 7, number);

        return string_length(into);
}

/*
        When a child dies of a signal, the line a person reads on stderr.

        Dash writes the C library's word and a newline, except interrupt and
        a broken pipe, which stay silent. A wait, a pipeline, a substitution
        and a foreground command all share that one sentence.

        Bash writes the same word for a foreground SIGTERM. A script's other
        reported deaths are `name: line N: PID word` padded to the jobs
        column, then the command. Interrupt and a broken pipe stay silent,
        a substitution stays silent, and a background SIGTERM stays silent.
        A posix `-c` command string is silent too: lima 5.2.32 prints the
        same deaths from a posix script file, and stays quiet once posix
        is on for a `-c` invocation.

        lastpipe freezes bash's jobs list while the last stage runs, so a
        death inside `if` / `{ }` / a loop is not written until that
        compound has restored its redirections. The line then lands after
        `else` rather than in `2>/dev/null`.
*/
static string_address job_death_command(bipolar child)
{
        positive at = job_find((positive)child, true);

        if (at < job_count && job_table[at].text)
                return (string_address)job_table[at].text;

        if (exec_wait_node)
        {
                static p8 address_to text;
                static positive room;

                job_text_node(address_of text, address_of room, 0, exec_wait_node,
                              0);
                return text ? (string_address)text : (string_address) "";
        }

        return (string_address) "";
}

static fn job_death_short(positive number, bool dumped)
{
        p8 name[64];
        positive length = job_signal_named(number, name);

        log_error(name, length);

        if (dumped)
                log_error(str(" (core dumped)"));

        log_error(str("\n"));
}

static fn job_death_long(bipolar child, positive number, bool dumped,
                         string_address command)
{
        p8 name[64];
        p8 digits[32];
        positive length = job_signal_named(number, name);
        positive width = positive_into_string(digits, (positive)child);

        name[length] = end;
        digits[width] = end;
        shell_diagnostic_where();
        string_to_field_bulk(log_error, digits, width < 5 ? 5 : width, ' ', false);
        log_error(str(" "));
        string_to_field_bulk(log_error, name, JOB_SIGNAL_DESC_WIDTH, ' ', true);

        if (dumped)
                log_error(str("(core dumped) "));

        if (command)
                log_error(command, string_length(command));

        log_error(str("\n"));
}

#define CHILD_DEATH_QUEUE 4

static struct
{
        bipolar child;
        positive raw;
        bool foreground;
} child_death_queue[CHILD_DEATH_QUEUE];
static positive child_death_queued;

static fn job_death_write(bipolar child, positive raw, bool foreground)
{
        positive number = raw & 0x7f;
        bool dumped = (raw & 0x80) != 0;

        if (shell_bash_compat)
        {
                bool listed = !shell_is_interactive &&
                              trap_action(number) == null && number != SIGTERM;

                if (!listed && !foreground)
                        return;

                if (listed)
                        job_death_long(child, number, dumped,
                                       job_death_command(child));
                else
                        job_death_short(number, dumped);
        }
        else
                job_death_short(number, dumped);

        log_flush();
}

static fn shell_child_death_flush()
{
        positive at;

        for (at = 0; at < child_death_queued; at++)
                job_death_write(child_death_queue[at].child,
                                child_death_queue[at].raw,
                                child_death_queue[at].foreground);

        child_death_queued = 0;
}

fn shell_child_death(bipolar child, positive raw, bool foreground)
{
        positive number;

        if (child <= 0 || !(raw & 0x7f) || (raw & 0xff) == 0x7f)
                return;

        number = raw & 0x7f;

        if (number == SIGNAL_INTERRUPT || number == SIGNAL_PIPE)
                return;

        if (shell_bash_compat)
        {
                if (shell_posix_on() && string_is(shell_option_flags, 'c'))
                        return;

                if (expand_in_substitution)
                        return;

                if (!foreground && shell_is_interactive)
                        return;

                if (exec_lastpipe_live && exec_compound_depth)
                {
                        if (child_death_queued < CHILD_DEATH_QUEUE)
                        {
                                child_death_queue[child_death_queued].child =
                                    child;
                                child_death_queue[child_death_queued].raw = raw;
                                child_death_queue[child_death_queued]
                                    .foreground = foreground;
                                child_death_queued++;
                        }

                        return;
                }
        }

        job_death_write(child, raw, foreground);
}

/*
        The status column of a listing.

        A stopped job is spelled twice over: the job's own summary says
        "Stopped", and the per-process detail `jobs -l` prints says which
        signal stopped it. Both are kept because both are what a person sees
        when the two commands are put side by side.
*/
static fn job_status_text(job_entry address_to entry, bool detailed,
                          p8 address_to into)
{
        positive code;

        if (entry->state == JOB_RUNNING)
        {
                string_copy(into, "Running");
                return;
        }

        if (entry->state == JOB_STOPPED)
        {
                positive by = entry->stopped_by;

                if (detailed && by && by != JOB_SIGNAL_STOP_KEY)
                {
                        job_signal_named(by, into);
                        return;
                }

                string_copy(into, "Stopped");

                /*
                        POSIX asks for the signal that stopped the job, and
                        bash writes it only in that mode: Stopped(SIGTSTP),
                        no space, in the summary a bare `jobs` prints. The
                        detail `jobs -l` prints says "Stopped (signal)" in
                        both modes, which is the line above this one.
                */
                if (shell_bash_compat && shell_posix_on() && by &&
                    by < TRAP_NAMES - 1 && trap_names[by])
                {
                        positive at = string_length(into);

                        string_copy(into + at, "(SIG");
                        at += 4;
                        string_copy(into + at, trap_names[by]);
                        at += string_length(trap_names[by]);
                        string_copy(into + at, ")");
                }
                return;
        }

        if ((entry->status & 0x7f) && (entry->status & 0xff) != 0x7f)
        {
                positive length = job_signal_named(entry->status & 0x7f, into);

                if (entry->status & 0x80)
                        string_copy(into + length, " (core dumped)");

                return;
        }

        code = wait_status_code(entry->status);

        if (!code)
        {
                string_copy(into, "Done");
                return;
        }

        /* Dash and bash --posix write Done(7) against bash's Exit 7.
           The parentheses are the whole of the difference. */
        if (shell_dash_columns() || shell_posix_on())
        {
                positive at;

                string_copy(into, "Done(");
                positive_into_string(into + 5, code);
                at = string_length(into);
                into[at] = ')';
                into[at + 1] = 0;
                return;
        }

        string_copy(into, "Exit ");
        positive_into_string(into + 5, code);
}

static bool job_died_signaled(job_entry address_to entry)
{
        return entry->state == JOB_FINISHED && (entry->status & 0x7f) &&
               (entry->status & 0xff) != 0x7f;
}

static string_address job_mark_of(job_entry address_to entry)
{
        /* Dash without a monitor uses `+` on every row once anything in
           the table has finished, and `-` only while every job is still
           running. Newest-first listing is the other half of that shape. */
        if (shell_dash_columns() && !shell_option_on(SHELL_OPTION_MONITOR))
                for (positive at = 0; at < job_count; at++)
                        if (job_table[at].state != JOB_RUNNING)
                                return (string_address) "+";

        if (entry->number == job_current)
                return (string_address) "+";

        if (entry->number == job_previous)
                return (string_address) "-";

        return (string_address) " ";
}

/*
        One job, on one line, in the columns of whichever shell this is.

        Bash writes the mark against the bracket and gives the status a
        width of its own, so a listing asked for pids puts its commands
        further right than one that was not. dash writes the mark with a
        space on each side and pads the whole line to one column instead,
        so both of its listings put the command in the same place. Under a
        dash name this shell had been writing bash's columns, which is
        every line of every job notice a dash session produces.

        An ampersand is not part of what was typed: it is how bash's listing
        says the job is still in the background, so it belongs to jobs that
        are running there and to no others. dash does not say it at all.
*/
#define JOB_DASH_COLUMN 33

static fn job_line(writer write, job_entry address_to entry, bool detailed)
{
        p8 status[64];
        string_address text = entry->text ? (string_address)entry->text
                                          : (string_address) "";
        bool ampersand = !shell_dash_columns() &&
                         entry->state == JOB_RUNNING && entry->background;

        /* Without job control dash still numbers background children, but
           the listing is the status column only: lima 0.5.x writes no
           command text until `set -m` has actually taken a terminal.
           A pipeline still shows the bar between empty stages. */
        if (shell_dash_columns() && !shell_option_on(SHELL_OPTION_MONITOR))
                text = string_search(text, " | ") ? (string_address) " | "
                                                  : (string_address) "";

        if (shell_dash_columns())
        {
                positive column = positive_digits(entry->number) + 5;

                string_format(write, "[%p] %s ", entry->number,
                              job_mark_of(entry));

                if (detailed)
                {
                        positive child = job_first_child(entry);

                        string_format(write, "%b ", child);
                        column += positive_digits((positive)child) + 1;
                }

                job_status_text(entry, detailed, status);
                write(status, string_length(status));
                column += string_length(status);

                string_to_field_bulk(write, (string_address) "",
                                column < JOB_DASH_COLUMN
                                    ? JOB_DASH_COLUMN - column : 1,
                                ' ', true);
                string_format(write, "%s\n", text);
                return;
        }

        string_format(write, "[%p]%s", entry->number, job_mark_of(entry));

        if (detailed)
                string_format(write, " %b ", job_first_child(entry));
        else
                string_format(write, "  ");

        job_status_text(entry, detailed, status);
        string_to_field_bulk(write, status, JOB_STATUS_WIDTH, ' ', true);

        /*
                A pipeline's detail is one line per process, as bash prints
                it: the first stage beside the status, every later one under
                it behind its own pid and a bar. The stages are the text cut
                at its bars, which is how the job's rendering joined them; a
                text that does not cut into as many pieces as the job has
                processes stays on one line.
        */
        if (detailed)
        {
                positive processes = job_children(entry->last, false);
                positive stages = 1;
                string_address bar = text;

                while ((bar = string_search(bar, " | ")))
                {
                        stages++;
                        bar += 3;
                }

                if (processes > 1 && processes == stages)
                {
                        positive written = 0;
                        string_address stage = text;

                        for (positive at = 0; at < shell_wait_count; at++)
                        {
                                string_address next;

                                if (shell_wait_table[at].job != entry->last)
                                        continue;

                                next = string_search(stage, " | ");
                                if (written)
                                {
                                        string_format(write, "\n     %b ",
                                                      shell_wait_table[at].pid);
                                        string_to_field_bulk(write, (string_address) "",
                                                        JOB_STATUS_WIDTH - 2, ' ', true);
                                        string_format(write, "| ");
                                }
                                if (next)
                                        write(stage, (positive)(next - stage));
                                else
                                        string_format(write, "%s", stage);
                                written++;
                                if (next)
                                        stage = next + 3;
                        }

                        if (ampersand)
                                string_format(write, " &");

                        string_format(write, "\n");
                        return;
                }
        }

        string_format(write, "%s", text);

        if (ampersand)
                string_format(write, " &");

        string_format(write, "\n");
}

/*
        What has changed since anybody last looked.

        A terminal is told as soon as the shell is between commands; a script
        is told nothing, because its output is somebody else's input and a
        line about job 1 in the middle of it is a bug. What a script gets
        instead is the same notice when it asks, through `jobs`.
*/
fn job_report()
{
        positive at = 0;

        if (!shell_is_interactive)
                return;

        while (at < job_count)
        {
                job_entry address_to entry = job_table + at;

                if (entry->reported || entry->state == JOB_RUNNING)
                {
                        at++;
                        continue;
                }

                job_line(log, entry, false);
                entry->reported = true;

                if (entry->state != JOB_FINISHED)
                {
                        at++;
                        continue;
                }

                shell_wait_drop(entry->last);
                job_drop_at(at);
        }

        log_flush();
}

/*
        The job an operand names.

        Bash accepts six spellings and so does this: a number, the current job
        as `%%` or `%+`, the previous one as `%-`, a command prefix, and a
        substring after `%?`. A prefix or substring matching two jobs is
        ambiguous rather than one of them, because guessing which of two
        running commands to kill is not a service.
*/
#define JOB_SPEC_FOUND 1
#define JOB_SPEC_UNKNOWN 2
#define JOB_SPEC_AMBIGUOUS 3

static positive job_specified(string_address word, positive address_to found)
{
        string_address text = word;
        positive matches = 0;
        positive number = 0;
        bool substring = false;

        address_to found = job_count;

        if (!word || !string_get(word))
        {
                address_to found = job_find(job_current, false);
                return job_current && address_to found < job_count
                           ? JOB_SPEC_FOUND
                           : JOB_SPEC_UNKNOWN;
        }

        if (string_get(text) == '%')
                text++;

        if (!string_get(text) || string_get(text) == '%' ||
            string_get(text) == '+')
        {
                address_to found = job_find(job_current, false);
                return job_current && address_to found < job_count
                           ? JOB_SPEC_FOUND
                           : JOB_SPEC_UNKNOWN;
        }

        if (string_get(text) == '-' && !string_get(text + 1))
        {
                /* One job is both current and previous: lima bash `fg %-`
                   with only `%1` still finds it. */
                positive number = job_previous ? job_previous : job_current;

                address_to found = job_find(number, false);
                return number && address_to found < job_count
                           ? JOB_SPEC_FOUND
                           : JOB_SPEC_UNKNOWN;
        }

        if (string_digits_exact(text, address_of number))
        {
                address_to found = job_find(number, false);
                return address_to found < job_count ? JOB_SPEC_FOUND
                                                    : JOB_SPEC_UNKNOWN;
        }

        if (string_get(text) == '?')
        {
                substring = true;
                text++;
        }

        for (positive at = 0; at < job_count; at++)
        {
                string_address have = job_table[at].text
                                          ? (string_address)job_table[at].text
                                          : (string_address) "";
                bool hit =
                    substring
                        ? string_find(have, text) != null
                        : !string_compare_max(have, text,
                                              string_length(text));

                if (!hit)
                        continue;

                matches++;
                address_to found = at;
        }

        if (matches == 1)
                return JOB_SPEC_FOUND;

        address_to found = job_count;

        return matches ? JOB_SPEC_AMBIGUOUS : JOB_SPEC_UNKNOWN;
}

//      The words of a "jobs -x" with the job specs already resolved, run as
//      one line. eval joins its words the same way, and for the same reason:
//      what reads a line lives above this file.
// The words from argv[from] on, joined by single spaces and terminated.
static bool shell_argv_joined(positive from, byte_store address_to store)
{
        store->used = 0;
        if (!shell_bytes_add(store, (string_address) "", 0))
                return false;

        for (positive at = from; at < shell_argc; at++)
                if ((at > from && !shell_bytes_byte(store, ' ')) ||
                    !shell_bytes_add(store, shell_argv[at],
                                     string_length(shell_argv[at])))
                        return false;

        return true;
}

static COLD fn shell_jobs_replaced(positive at)
{
        static byte_store joined;

        if (!shell_argv_joined(at, address_of joined))
                return shell_answered(2, "%s: no room\n", "jobs");

        //      Nested the way eval and fc nest: the parser is standing in
        //      the middle of the jobs that asked for this, and a line fed to
        //      it without its own lexer storage is a second sentence written
        //      over the first.
        exec_run_nested((string_address)joined.bytes, true, 0);
}

/* A spec that names no job, or more than one. jobs and disown handed a bare
   number are warned first that a job spec leads with a percent sign. */
static b32 job_spec_refused(string_address builtin, string_address word,
                            positive told, bool bare_warning)
{
        if (bare_warning && shell_bash_compat && word && !string_is(word, '%'))
        {
                shell_told("%s: warning: %s: job specification "
                    "requires leading `%s'\n", builtin, word,
                    (string_address) "%");
        }

        return shell_reported(
            1, told == JOB_SPEC_AMBIGUOUS ? "%s: %s: ambiguous job spec\n"
                                          : "%s: %s: no such job\n",
            builtin, told == JOB_SPEC_AMBIGUOUS || word
                         ? word : (string_address) "current");
}

fn shell_jobs(writer write, string_address input)
{
        shell_option_walk walk = {1};
        bool detailed = false;
        bool identifiers = false;
        bool running_only = false;
        bool stopped_only = false;
        bool changed_only = false;
        p8 letter;

        (void)input;

        while (shell_option_letter(address_of walk, address_of letter))
                switch (letter)
                {
                case 'l':
                        detailed = true;
                        break;
                case 'p':
                        identifiers = true;
                        break;
                case 'r':
                        running_only = true;
                        break;
                case 's':
                        stopped_only = true;
                        break;
                case 'n':
                        changed_only = true;
                        break;
                case 'x':
                        //      Every word that names a job becomes that
                        //      job's process, and what is left is a command
                        //      to run. Without job control there is nothing
                        //      a spec can name, so the words run as they are
                        //      and a spec among them is refused.
                        {
                                p8 more;
                                positive at;

                                //      The letters after -x, and any option
                                //      word behind them, are still options;
                                //      the walk is drained so that index
                                //      names the first word of the command.
                                while (shell_option_letter(address_of walk,
                                                           address_of more))
                                        ;

                                //      -x is the whole of what jobs is
                                //      doing, so Bash refuses any other
                                //      letter beside it.
                                if (detailed || identifiers || running_only ||
                                    stopped_only || changed_only)
                                        return shell_answered(1,
                                            "jobs: no other options allowed with `-x'\n");

                                at = walk.index;

                                if (at >= shell_argc)
                                        return shell_answer(0);

                                //      The command word names a job or it
                                //      names a command; a spec among the
                                //      arguments that no job answers to is
                                //      left as it was written.
                                if (string_is(shell_argv[at], '%'))
                                {
                                        positive found;

                                        if (job_specified(shell_argv[at],
                                                          address_of found)
                                            != JOB_SPEC_FOUND)
                                                return shell_answered(1,
                                                    "jobs: %s: no such job\n",
                                                    shell_argv[at]);
                                }

                                return shell_jobs_replaced(at);
                        }
                default:
                        return shell_letter_refuse("jobs", letter,
                            "jobs [-lnprs] [jobspec ...] or "
                            "jobs -x command [args]");
                }

        job_reap();

        if (walk.index < shell_argc)
        {
                b32 answer = 0;

                for (positive at = walk.index; at < shell_argc; at++)
                {
                        positive found;
                        positive told = job_specified(shell_argv[at],
                                                      address_of found);

                        if (told != JOB_SPEC_FOUND)
                        {
                                //      bash warns when a word with no
                                //      per-cent in front of it is read as a
                                //      job spec at all, and then says it
                                //      found no such job. The sign goes
                                //      through as an argument, because the
                                //      shared formatter has no escape for
                                //      one of its own.
                                answer = job_spec_refused("jobs", shell_argv[at],
                                                          told, true);
                                continue;
                        }

                        if (identifiers)
                                string_format(write, "%b\n",
                                              job_first_child(job_table +
                                                              found));
                        else
                                job_line(write, job_table + found, detailed);

                        job_table[found].reported = true;
                }

                return shell_answer(answer);
        }

        /* Dash lists newest first. Walking that way also lets a drop keep
           the unvisited older rows in place, so there is no second table. */
        bool newest = shell_dash_columns();
        positive at = newest ? job_count : 0;

        while (newest ? at : at < job_count)
        {
                job_entry address_to entry;
                bool show = true;

                if (newest)
                        at--;

                entry = job_table + at;

                if (running_only && entry->state != JOB_RUNNING)
                        show = false;
                if (stopped_only && entry->state != JOB_STOPPED)
                        show = false;
                if (changed_only && entry->reported)
                        show = false;

                if (show && identifiers)
                        string_format(write, "%b\n", job_first_child(entry));
                else if (show)
                        job_line(write, entry, detailed);

                if (show)
                        entry->reported = true;

                /* A finished job is news exactly once. Reporting it is also
                   forgetting it, which is why `jobs` twice over shows it and
                   then does not. */
                if (show && entry->state == JOB_FINISHED)
                {
                        shell_wait_drop(entry->last);
                        job_drop_at(at);
                        if (!newest)
                                continue;
                }
                else if (!newest)
                        at++;
        }

        shell_answer(0);
}

fn shell_fg(writer write, string_address input)
{
        positive found;
        positive told;
        job_entry address_to entry;

        (void)input;

        if (!job_monitor())
        {
                string_address word = shell_argc > 1 ? shell_argv[1] : null;

                if (!shell_bash_compat)
                {
                        shell_diagnostic_where();
                        if (word && string_get(word) == '%' &&
                            string_get(word + 1) == '-' &&
                            !string_get(word + 2) && !job_previous &&
                            job_count != 1)
                                return shell_answered(2, "fg: No previous job\n");

                        return shell_answered(2,
                            "fg: job %s not created under job control\n",
                            word ? word : (string_address) "(null)");
                }

                return shell_answered(1, "%s: no job control\n", "fg");
        }

        job_reap();

        string_address word = shell_argc > 1 ? shell_argv[1] : null;
        told = job_specified(word, address_of found);

        if (told != JOB_SPEC_FOUND)
                return shell_answer(job_spec_refused("fg", word, told, false));

        entry = job_table + found;
        entry->background = false;
        job_mark(entry->number);

        string_format(write, "%s\n",
                      entry->text ? (string_address)entry->text
                                  : (string_address) "");
        log_flush();

        if (entry->state == JOB_STOPPED)
        {
                entry->state = JOB_RUNNING;
                job_signal(entry->group > 0 ? -entry->group : entry->last,
                           JOB_SIGNAL_CONTINUE);
        }

        shell_answer(job_wait_foreground(entry->number));
}

fn shell_bg(writer write, string_address input)
{
        b32 answer = 0;
        positive at = 1;

        (void)input;

        if (!job_monitor())
                return shell_answered(1, "%s: no job control\n", "bg");

        job_reap();

        do
        {
                string_address word = at < shell_argc ? shell_argv[at] : null;
                positive found;
                positive told = job_specified(word, address_of found);
                job_entry address_to entry;

                if (told != JOB_SPEC_FOUND)
                {
                        answer = job_spec_refused("bg", word, told, false);
                        continue;
                }

                entry = job_table + found;

                if (entry->state == JOB_RUNNING && entry->background)
                {
                        string_format(log_error,
                                      "bg: job %p already in background\n",
                                      entry->number);
                        answer = 1;
                        continue;
                }

                entry->state = JOB_RUNNING;
                entry->background = true;
                job_mark(entry->number);
                job_signal(entry->group > 0 ? -entry->group : entry->last,
                           JOB_SIGNAL_CONTINUE);

                //      bg names the job it moved. Bash names the mark and
                //      the ampersand with it; dash writes the number and
                //      the command and nothing else.
                if (shell_dash_columns())
                        string_format(write, "[%p] %s\n", entry->number,
                                      entry->text ? (string_address)entry->text
                                                  : (string_address) "");
                else if (shell_posix_on())
                        string_format(write, "[%p] %s &\n", entry->number,
                                      entry->text ? (string_address)entry->text
                                                  : (string_address) "");
                else
                        string_format(write, "[%p]%s %s &\n", entry->number,
                                      job_mark_of(entry),
                                      entry->text ? (string_address)entry->text
                                                  : (string_address) "");
        } while (++at < shell_argc);

        shell_answer(answer);
}

/*
        A job the shell stops keeping.

        Forgetting is the whole of it: the row goes, the wait table's rows go
        with it, and the process carries on with nobody left to report for it.
        `-h` is the exception and keeps the row, because what it asks for is
        not forgetting but an exemption from the hangup a leaving shell sends.
*/
fn shell_disown(writer write, string_address input)
{
        shell_option_walk walk = {1};
        bool all = false;
        bool running_only = false;
        bool keep = false;
        b32 answer = 0;
        p8 letter;

        (void)write;
        (void)input;

        while (shell_option_letter(address_of walk, address_of letter))
                switch (letter)
                {
                case 'a':
                        all = true;
                        break;
                case 'r':
                        running_only = true;
                        break;
                case 'h':
                        keep = true;
                        break;
                default:
                        return shell_letter_refuse("disown", letter,
                            "disown [-h] [-ar] [jobspec ... | pid ...]");
                }

        job_reap();

        if (all || running_only || walk.index >= shell_argc)
        {
                bool touched = false;

                for (positive at = 0; at < job_count;)
                {
                        if (running_only &&
                            job_table[at].state != JOB_RUNNING)
                        {
                                at++;
                                continue;
                        }

                        if (!all && !running_only &&
                            job_table[at].number != job_current)
                        {
                                at++;
                                continue;
                        }

                        if (keep)
                        {
                                touched = true;
                                job_table[at].nohup = true;
                                at++;
                                continue;
                        }

                        touched = true;
                        if (!exec_disowned_remember(job_table + at))
                        {
                                answer = string_report(
                                    log_error, 2,
                                    "disown: no room to retain child\n");
                                at++;
                                continue;
                        }

                        shell_wait_drop(job_table[at].last);
                        job_drop_at(at);
                }

                //      Nothing named and nothing current: Bash says which
                //      job it looked for and answers one. -a and -r ask for
                //      whatever there is and are content with none.
                if (!all && !running_only && !touched)
                        return shell_answered(1, "disown: current: no such job\n");

                return shell_answer(answer);
        }

        for (positive at = walk.index; at < shell_argc; at++)
        {
                positive found;
                positive told = job_specified(shell_argv[at],
                                              address_of found);

                if (told != JOB_SPEC_FOUND)
                {
                        //      As jobs does: the warning first, and the
                        //      sign as an argument.
                        answer = job_spec_refused("disown", shell_argv[at],
                                                  told, true);
                        continue;
                }

                if (keep)
                {
                        job_table[found].nohup = true;
                        continue;
                }

                if (!exec_disowned_remember(job_table + found))
                {
                        answer = string_report(
                            log_error, 2,
                            "disown: no room to retain child\n");
                        continue;
                }
                shell_wait_drop(job_table[found].last);
                job_drop_at(found);
        }

        shell_answer(answer);
}

/*
        The shell, stopped by its own hand.

        Only a shell that has job control has anywhere to be stopped back to:
        without it there is no other foreground group to hand the terminal to
        and nobody who would ever continue this one.
*/
fn shell_suspend(writer write, string_address input)
{
        (void)write;
        (void)input;

        {
                //      The option walk comes first, because bash refuses a
                //      letter it does not have before it weighs whether it
                //      could suspend at all.
                shell_option_walk walk = {1};
                p8 which;

                while (shell_option_letter(address_of walk, address_of which))
                        if (which != 'f')
                                return shell_letter_refuse("suspend", which, "suspend [-f]");

                //      And no operands either: a word after the options is
                //      one word too many, whatever it says.
                if (walk.index < shell_argc)
                {
                        shell_refuse(1, "suspend: too many arguments\n");

                        //      And takes the script with it: bash never
                        //      reaches the next command after this one.
                        shell_stop_when_scripted(1);

                        return;
                }
        }

        if (!job_monitor() || !shell_is_interactive)
                return shell_answered(1, "suspend: cannot suspend: no job control\n");

        log_flush();
        job_signal(-job_shell_group, JOB_SIGNAL_STOP);

        shell_answer(0);
}

/*
        A foreground job, waited for.

        The wait is the one every foreground command gets, except that a stop
        is an answer too: the job stays in the table, the shell takes the
        terminal back, and the number the caller reads is the one POSIX gives
        a command that stopped.
*/
static b32 job_wait_foreground(positive number)
{
        positive at = job_find(number, false);
        b32 status = shell_status;
        bipolar last;

        if (at >= job_count)
                return status;

        last = job_table[at].last;
        job_terminal_give(job_table[at].group);

        while (true)
        {
                positive raw = 0;
                bipolar got;

                at = job_find(number, false);

                if (at >= job_count || job_table[at].state != JOB_RUNNING ||
                    !job_children(last, true))
                        break;

                got = job_wait_call(-1, address_of raw, JOB_UNTRACED);
                if (got <= 0)
                        break;

                job_child_changed(got, raw);
        }

        job_terminal_give(job_shell_group);

        at = job_find(number, false);

        if (at >= job_count)
                return status;

        if (job_table[at].state == JOB_STOPPED)
        {
                job_table[at].reported = true;
                job_line(log, job_table + at, false);
                log_flush();

                return 128 + (b32)job_table[at].stopped_by;
        }

        {
                bool interrupted;

                status = shell_wait_one(last, address_of interrupted, true,
                                        true);
                at = job_find(number, false);

                if (at < job_count)
                        job_drop_at(at);
        }

        return status;
}

/*
        A foreground child under the monitor, waited for.

        No job is made unless one is needed. A command that runs to the end
        was never a job: it took no number, `jobs` never mentioned it, and the
        next background command is still [1]. The number is taken at the
        moment it stops, which is also the moment it becomes something `fg`
        can name.
*/
static b32 job_foreground_wait(bipolar child, bipolar group, b32 node)
{
        positive raw = 0;
        positive stopped_by;
        positive number;
        b32 answer;
        bipolar got;

        job_terminal_give(group);
        got = job_wait_call(child, address_of raw, JOB_UNTRACED);
        job_terminal_give(job_shell_group);

        if (got == -4)
                return job_wait_interrupted();
        if (got < 0)
                return 1;

        if ((raw & 0xff) != 0x7f)
        {
                if (node > 0)
                        exec_wait_node = node;

                shell_child_death(child, raw, true);
                return wait_status_code(raw);
        }

        stopped_by = (raw >> 8) & 0xff;
        answer = 128 + (b32)stopped_by;

        if (!job_retain(address_of child, 1, false, false, false))
                return answer;

        number = job_started(address_of child, 1, group, node, false, false);

        if (!number)
                return answer;

        {
                positive at = job_find(number, false);

                job_table[at].state = JOB_STOPPED;
                job_table[at].stopped_by = stopped_by;
                job_table[at].reported = true;
                job_line(log, job_table + at, false);
                log_flush();
        }

        return answer;
}

/* A monitored child's own first steps: the process group, raced from both
   sides, and the stop signals a job takes by default. */
static fn job_child_group_enter(bipolar group)
{
        job_group_set(0, group);
        shell_child_default(JOB_SIGNAL_STOP_KEY);
        shell_child_default(JOB_SIGNAL_TTY_INPUT);
        shell_child_default(JOB_SIGNAL_TTY_OUTPUT);
}

/*
        One foreground child under the monitor: the command being run, when
        which is SHELL_TOOLS, or else a utility of this image.

        The spawn device would be quicker and cannot be used here. A spawned
        stage never runs a line of this shell's code, so the only side that
        could put it in a process group of its own is this one -- and by the
        time the request has returned the child may already have exec'd, at
        which point setpgid is refused. Both sides racing to the same answer is
        what makes the group certain, and only a fork has two sides. A
        utility is already resident, so the child calls it rather than
        loading one, and what the fork costs over the spawn buys a `sleep`
        that control-Z can stop.
*/
fn job_execute_tool(positive which, bool confined)
{
        bipolar child;

        if (!job_reserve(1, false))
                return shell_answered(2, "No room to retain foreground job\n");

        log_flush();
        child = confined ? shell_clone() : shell_clone_raw();

        if (child == 0)
        {
                job_child_group_enter(0);
                if (which == SHELL_TOOLS)
                        shell_thread_instance();
                trap_default_all();
                shell_child_default(SIGNAL_INTERRUPT);
                shell_child_default(SIGNAL_QUIT);
                exec_child_began();
                program_arguments_use(shell_argv, (b32)shell_argc);
                exit(shell_tool_call_in(which, true));
        }

        // A command no fork could take still runs, here; a utility fails.
        if (child < 0)
                return which == SHELL_TOOLS ? shell_execute_command()
                                            : shell_answer(1);

        job_group_set(child, child);

        shell_answer(job_foreground_wait(child, child, -1));
}

/*
        kill, once an operand names a job.

        Everything else stays with the utility: the recorded answers about
        signal names, numbers and refusals are its, and a second parser beside
        it would be a second set of them. What the shell adds is the one thing
        a utility in another process cannot know -- what `%1` means, and that
        under job control it means a process group rather than one process.
*/
static bool job_kill_specified()
{
        for (positive at = 1; at < shell_argc; at++)
                if (string_get(shell_argv[at]) == '%')
                        return true;

        return false;
}

fn shell_kill(writer write, string_address input)
{
        bipolar number = 15;
        positive at = 1;
        b32 answer = 0;
        bool signalled = false;

        (void)write;
        (void)input;

        if (!job_kill_specified())
        {
                string_address address_to saved = program_argument_list();
                b32 saved_count = program_argument_count();

                // This builtin shares the utility parser, but needs no helper
                // process. Handlers only mark pending traps until argv is back.
                log_flush();
                program_arguments_use(shell_argv, (b32)shell_argc);
                kill_shell_spelling = true;
                answer = file_kill();
                kill_shell_spelling = false;
                program_arguments_use(saved, saved_count);
                shell_answer(answer);
                return;
        }

        while (at < shell_argc)
        {
                string_address word = shell_argv[at];

                if (string_get(word) != '-' || !string_get(word + 1))
                        break;

                if (string_get(word + 1) == '-' && !string_get(word + 2))
                {
                        at++;
                        break;
                }

                if ((string_get(word + 1) == 's' ||
                     string_get(word + 1) == 'n') && !string_get(word + 2))
                {
                        if (++at >= shell_argc)
                                return shell_answered(2, "kill: -s needs a signal\n");

                        number = kill_number(shell_argv[at]);
                }
                else
                        number = kill_number(word + 1);

                if (number < 0)
                        return shell_answered(2, "kill: invalid signal\n");

                at++;
        }

        if (at >= shell_argc)
                return shell_answered(2, "kill: no process named\n");

        job_reap();

        for (; at < shell_argc; at++)
        {
                string_address word = shell_argv[at];
                bipolar target;
                positive found;
                positive told;

                if (string_get(word) != '%')
                {
                        bipolar pid;

                        //      A word that is no number names nobody. Read
                        //      as zero it would signal this shell's own
                        //      group. Bash says so and walks on; dash calls
                        //      the digits after a sign an illegal number and
                        //      reads no further.
                        if (!exec_control_integer(word, address_of pid) ||
                            pid != (b32)pid)
                        {
                                shell_diagnostic_where();
                                if (!shell_bash_compat)
                                        return shell_answered(2,
                                            "kill: Illegal number: %s\n",
                                            word + (string_get(word) == '-'));
                                answer = string_report(log_error,
                                    shell_posix_on() ? answer : 1,
                                    "kill: `%s': not a pid or valid job spec\n",
                                    word);
                                continue;
                        }

                        if (job_signal(pid, (positive)number) < 0)
                        {
                                string_format(log_error,
                                              "kill: %s: no such process\n",
                                              word);
                                answer = 1;
                        }
                        else
                                signalled = true;

                        continue;
                }

                told = job_specified(word, address_of found);

                if (!shell_bash_compat && !job_monitor())
                {
                        positive numeric = 0;
                        string_address spec = word + 1;
                        bool numbered =
                            !string_get(spec) || string_get(spec) == '%' ||
                            string_get(spec) == '+' ||
                            (string_get(spec) == '-' && !string_get(spec + 1)) ||
                            string_digits_exact(spec, address_of numeric);

                        /* `%sleep` is not a job without a monitor.
                           `%%` / `%1` still name the table, and then
                           lima says "No such process" rather than killing. */
                        if (!numbered)
                        {
                                shell_diagnostic_where();
                                answer = string_report(log_error, 2,
                                    "kill: No such job: %s\n", word);
                                continue;
                        }
                }

                if (told != JOB_SPEC_FOUND)
                {
                        if (shell_bash_compat)
                        {
                                answer = job_spec_refused("kill", word, told,
                                                          false);
                                continue;
                        }

                        shell_diagnostic_where();
                        answer = string_report(log_error, 2,
                                               "kill: No such job: %s\n", word);
                        continue;
                }

                /* Dash kill only follows a job spec under job control.
                   With the monitor off, a live `%1` is still "No such
                   process" and the child is left running, matching lima
                   0.5.x. A spec nobody has stays "No such job" above. */
                if (!shell_bash_compat && !job_monitor())
                {
                        shell_diagnostic_where();
                        answer = string_report(log_error, 1,
                                               "kill: No such process\n");
                        continue;
                }

                target = job_table[found].group > 0
                             ? -job_table[found].group
                             : job_table[found].last;

                if (job_signal(target, (positive)number) < 0)
                {
                        string_format(log_error,
                                      "kill: %s: no such job\n", word);
                        answer = 1;
                }
                else
                        signalled = true;
        }

        //      Bash answers for the whole list: nought when anybody was
        //      signalled, whatever the other operands said. Its posix mode
        //      fails only on a signal that could not be sent, so a word that
        //      is no pid does not count against it there.
        shell_answer(shell_bash_compat && !shell_posix_on() && signalled
                         ? 0 : answer);
}

// A job whose children the wait table no longer owes an answer for is not a
// job any more: somebody waited for it and POSIX says a successful wait
// forgets what it waited for.
static fn job_prune()
{
        for (positive at = 0; at < job_count;)
                if (!job_children(job_table[at].last, false))
                {
                        /* Dash wait forgets the wait-table row and still
                           owes `jobs` a Done line, so an unreported finish
                           without children is news, not a hole. */
                        if (!shell_bash_compat &&
                            job_table[at].state == JOB_FINISHED &&
                            !job_table[at].reported)
                                at++;
                        else
                                job_drop_at(at);
                }
                else
                        at++;
}

/*
        wait, once there are jobs to name.

        The plain forms are the ones already written beside the wait table and
        stay there. What a job table adds is a `%1` operand, the answer POSIX
        gives for a job that stopped rather than finished, and `-n` -- which is
        not a wait for anybody in particular but for whoever ends first.
*/
static b32 job_wait_job(positive found, string_address into,
                        bool address_to interrupted, bool forget, bool drop)
{
        bipolar last = job_table[found].last;
        positive number = job_table[found].number;
        b32 answer;
        positive raw;

        /* Under job control a stop is an answer too, in bash: wait %1 on a
           job that stops says so and gives 128 and the signal. This waited
           for an exit that a stopped job never makes. */
        if (shell_bash_compat && job_monitor())
        {
                address_to interrupted = false;
                while (true)
                {
                        positive changed = 0;
                        bipolar got;
                        positive at = job_find(number, false);

                        if (at >= job_count ||
                            job_table[at].state != JOB_RUNNING ||
                            !job_children(last, true))
                                break;

                        got = job_wait_call(-1, address_of changed,
                                            JOB_UNTRACED);
                        if (got == -4)
                        {
                                address_to interrupted = true;
                                return job_wait_interrupted();
                        }
                        if (got <= 0)
                                break;
                        job_child_changed(got, changed);
                }

                positive at = job_find(number, false);

                if (at < job_count && job_table[at].state == JOB_STOPPED)
                {
                        shell_told("warning: wait_for_job: job %p is stopped\n",
                                   number);
                        return 128 + (b32)job_table[at].stopped_by;
                }
        }

        answer = shell_wait_one(last, interrupted, false, false);
        if (into && !address_to interrupted)
                env_set_number(into, (positive)last);

        //      The last child's own wait word, so an exit code past 128 is
        //      filed as an exit and not as the signal it would spell.
        raw = answer > 128 ? (positive)(answer - 128) : (positive)answer << 8;
        for (positive at = 0; at < shell_wait_count; at++)
                if (shell_wait_table[at].job == last &&
                    (shell_wait_table[at].flags & SHELL_WAIT_LAST) &&
                    (shell_wait_table[at].flags & SHELL_WAIT_DONE))
                        raw = shell_wait_table[at].status;
        if (forget && !address_to interrupted)
                shell_wait_drop(last);
        found = job_find(last, true);

        if (found < job_count)
        {
                if (drop)
                        job_drop_at(found);
                else if (job_table[found].state != JOB_FINISHED)
                {
                        job_table[found].state = JOB_FINISHED;
                        job_table[found].reported = false;
                        job_table[found].status = raw;
                }
        }

        return answer;
}

/*
        The next job to end, whichever it turns out to be.

        Nothing is named, so the wait is for any child at all and the table is
        asked afterwards which job that was. A job that merely stopped has not
        ended, so the wait goes round again -- unless the caller said -f, which
        is the option for wanting the end rather than the next change.

        Named operands restrict that pool: wait -n $slow $fast waits only
        among those jobs, not an unrelated sibling.
*/
static bool job_wait_named(positive at, positive first)
{
        if (first >= shell_argc)
                return true;

        for (positive i = first; i < shell_argc; i++)
        {
                string_address word = shell_argv[i];
                positive found;
                positive pid;

                if (string_get(word) == '%')
                {
                        if (job_specified(word, address_of found) ==
                                JOB_SPEC_FOUND &&
                            found == at)
                                return true;
                        continue;
                }
                if (string_digits_checked_exact(word, 10, address_of pid) &&
                    (positive)job_table[at].last == pid)
                        return true;
        }

        return false;
}

static b32 job_wait_next(bool force, string_address into, positive first)
{
        while (true)
        {
                positive raw = 0;
                bipolar got;
                bool interrupted;

                for (positive at = 0; at < job_count; at++)
                        if (job_table[at].state == JOB_FINISHED &&
                            job_wait_named(at, first))
                                return job_wait_job(at, into,
                                                    address_of interrupted,
                                                    true, true);

                if (!force)
                        for (positive at = 0; at < job_count; at++)
                                if (job_table[at].state == JOB_STOPPED &&
                                    !job_table[at].reported &&
                                    job_wait_named(at, first))
                                {
                                        job_table[at].reported = true;

                                        return 128 +
                                               (b32)job_table[at].stopped_by;
                                }

                if (first < shell_argc)
                {
                        bool any = false;

                        for (positive at = 0; at < job_count; at++)
                                if (job_wait_named(at, first))
                                        any = true;
                        if (!any)
                                return 127;
                }

                if (!shell_wait_count)
                        return 127;

                got = job_wait_call(-1, address_of raw,
                                    force ? 0 : JOB_UNTRACED);

                if (got == -4)
                        return job_wait_interrupted();

                if (got <= 0)
                        return 127;

                job_child_changed(got, raw);
        }
}

fn job_wait(writer write, string_address input)
{
        bool next = false;
        bool force = false;
        string_address into = null;
        positive first = 1;
        b32 answer = 0;
        bool interrupted = false;

        while (first < shell_argc)
        {
                string_address word = shell_argv[first];
                positive at;

                if (string_get(word) != '-' || !string_get(word + 1))
                        break;

                if (string_get(word + 1) == '-' && !string_get(word + 2))
                {
                        first++;
                        break;
                }

                for (at = 1; string_get(word + at); at++)
                {
                        p8 letter = string_get(word + at);

                        if (letter == 'n')
                                continue;

                        if (letter == 'f')
                                continue;

                        if (letter == 'p')
                                break;

                        return shell_letter_refuse("wait", letter,
                            "wait [-fn] [-p var] [id ...]");
                }

                for (at = 1; string_get(word + at); at++)
                {
                        p8 letter = string_get(word + at);

                        if (letter == 'n')
                        {
                                next = true;
                                continue;
                        }

                        if (letter == 'f')
                        {
                                force = true;
                                continue;
                        }

                        if (string_get(word + at + 1))
                        {
                                into = word + at + 1;
                                break;
                        }

                        if (first + 1 >= shell_argc)
                                return shell_answered(2, "wait: -p wants a name\n");

                        into = shell_argv[++first];
                        break;
                }

                first++;
        }

        job_reap();

        if (into)
        {
                positive length = string_length(into);

                if (!length || !shell_valid_name(into, length))
                        return shell_answered(2,
                            "wait: %s: invalid identifier\n", into);
        }

        if (next)
                return shell_answer(job_wait_next(force, into, first));

        if (first >= shell_argc)
        {
                bool waited_running = false;

                /* Running jobs first. A sibling that dies while we sit is
                   filed by wait4(-1) into the same table; collecting it here
                   would drop the Terminated line lima keeps for `jobs`. */
                while (true)
                {
                        positive at;

                        for (at = 0; at < job_count; at++)
                                if (job_table[at].state == JOB_RUNNING ||
                                    (force &&
                                     job_table[at].state == JOB_STOPPED))
                                        break;

                        if (at >= job_count)
                                break;

                        waited_running = true;
                        answer = job_wait_job(at, into,
                                              address_of interrupted, true,
                                              shell_bash_compat);

                        if (interrupted)
                                return shell_answer(answer);
                }

                if (!waited_running)
                {
                        /* Nothing was running: kill-pipeline's only job may
                           already be a zombie, and wait still forgets it. */
                        while (true)
                        {
                                positive at;

                                for (at = 0; at < job_count; at++)
                                        if (job_table[at].state !=
                                                JOB_STOPPED ||
                                            force)
                                                break;

                                if (at >= job_count)
                                        break;

                                answer = job_wait_job(at, into,
                                                      address_of interrupted,
                                                      true, true);

                                if (interrupted)
                                        return shell_answer(answer);
                        }
                }
                else if (shell_bash_compat)
                {
                        positive keep = 0;

                        for (positive at = 0; at < job_count;)
                        {
                                job_entry address_to entry = job_table + at;

                                if (entry->state == JOB_FINISHED &&
                                    !job_died_signaled(entry))
                                {
                                        shell_wait_drop(entry->last);
                                        job_drop_at(at);
                                        continue;
                                }

                                at++;
                        }

                        for (positive at = 0; at < job_count; at++)
                                if (job_died_signaled(job_table + at) &&
                                    job_table[at].number > keep)
                                        keep = job_table[at].number;

                        for (positive at = 0; at < job_count;)
                        {
                                job_entry address_to entry = job_table + at;

                                if (job_died_signaled(entry) &&
                                    entry->number != keep)
                                {
                                        shell_wait_drop(entry->last);
                                        job_drop_at(at);
                                        continue;
                                }

                                at++;
                        }

                        /* lima writes `[2]   Terminated` with a blank mark
                           after wait collected the running jobs around it. */
                        job_current = 0;
                        job_previous = 0;
                }

                /* A retained child that never became a job -- one started
                   before the table could hold it -- is still owed an answer,
                   and the plain wait beside the table is the one that gives
                   it. Only when nothing is stopped: waiting for a stopped
                   process is waiting forever. */
                if (shell_wait_count && !job_count)
                {
                        shell_wait(write, input);
                        return;
                }

                job_prune();

                return shell_answer(0);
        }

        for (positive at = first; at < shell_argc; at++)
        {
                string_address word = shell_argv[at];
                positive found;
                positive pid;

                if (string_get(word) == '%')
                {
                        if (job_specified(word, address_of found) !=
                            JOB_SPEC_FOUND)
                        {
                                shell_diagnostic_where();

                                if (!shell_bash_compat)
                                        return shell_answered(2,
                                            "wait: No such job: %s\n", word);

                                return shell_answered(127, "wait: %s: no such job\n", word);
                        }
                }
                else
                {
                        if (!string_digits_checked_exact(word, 10, address_of pid) ||
                            pid > (positive)b32_max)
                        {
                                //      Bash names it as a job spec it could
                                //      not read and answers one; dash calls
                                //      it an illegal number, answers two and
                                //      reads no further.
                                shell_diagnostic_where();

                                if (!shell_bash_compat)
                                        return shell_answered(2,
                                            "wait: Illegal number: %s\n",
                                            word);

                                //      Every word gets its own line: bash
                                //      walks the rest of them and answers
                                //      one at the end.
                                answer = string_report(log_error, 1,
                                    "wait: `%s': not a pid or valid job spec\n",
                                    word);
                                continue;
                        }

                        found = job_find(pid, true);

                        if (found >= job_count)
                        {
                                if (into)
                                        env_set_number(into, pid);

                                answer = shell_wait_one((bipolar)pid,
                                                        address_of interrupted,
                                                        shell_posix_on(),
                                                        false);
                                job_prune();

                                if (interrupted)
                                        break;

                                continue;
                        }
                }

                if (!force && job_table[found].state == JOB_STOPPED)
                {
                        string_format(log_error,
                                      "wait: job %p is stopped\n",
                                      job_table[found].number);
                        answer = 128 + (b32)job_table[found].stopped_by;
                        continue;
                }

                answer = job_wait_job(found, into, address_of interrupted,
                                      shell_posix_on(), true);

                if (interrupted)
                        break;
        }

        shell_answer(answer);
}

/*
        A forked foreground child under the monitor.

        A subshell is a job like any other while it is in front: it has its own
        process group, so it has to be given the terminal, and it can stop,
        so the shell has to be able to say so and get its prompt back.
*/
static b32 job_foreground_child(bipolar child, b32 node)
{
        if (child < 0)
                return 1;

        return job_foreground_wait(child, child, node);
}

/*
        The history, and the one store there is of it.

        Two stores would have been the obvious shape, because two things want
        one: the line editor for its arrow keys, and `fc` for something to
        edit. They are not two stores here, and the reason is that they were
        never even two halves of one process. The editor's ring is in the
        terminal emulator -- src/canvas/term.c, which draws the screen and
        assembles the line -- and it reaches this shell down a pseudo-terminal
        as finished lines. Nothing in this address space can see it.

        So the store is here, where the shell reads its lines; `history` and
        `fc` are both written against it; and HISTFILE is the only place the
        two ends can ever meet, which is exactly what it is for.
*/
#define HISTORY_DEFAULT 500

static p8 address_to address_to history_text;
static positive history_text_room;

// How much was taken for each line, because the store gives them back one at
// a time and a mapping has to be returned with the size it was asked for.
static positive address_to history_bytes;
static positive history_bytes_room;

static positive history_used;

// What the oldest line held is numbered. Trimming takes from the front, so
// the numbers go on rising while the store stays the size it was told to be.
static positive history_first = 1;

// How much of the store has already been given to the file, so that -a
// appends what is new rather than everything again.
static positive history_saved;

static PURE positive history_number(string_address name, positive fallback)
{
        string_address value = env_get(name);
        positive number;

        if (!value || !string_get(value) ||
            !string_digits_exact(value, address_of number))
                return fallback;

        return number;
}

static PURE string_address history_file()
{
        string_address path = env_get((const_string) "HISTFILE");

        return path && string_get(path) ? path : null;
}

// The number the next line remembered will take, which \! and \# say.
positive shell_history_next()
{
        return history_first + history_used;
}

static fn history_drop(positive at, positive count)
{
        if (at >= history_used || !count)
                return;

        count = min(count, history_used - at);
        for (positive step = at; step < at + count; step++)
                if (history_text[step])
                        memory_free(history_text[step], history_bytes[step]);

        history_used -= count;

        if (at < history_used)
        {
                memory_copy(history_text + at, history_text + at + count,
                            (history_used - at) * sizeof(history_text[0]));
                memory_copy(history_bytes + at, history_bytes + at + count,
                            (history_used - at) * sizeof(history_bytes[0]));
        }

        if (!at)
                history_first += count;

        if (history_saved > at)
                history_saved -= min(count, history_saved - at);
}

static fn history_trim(string_address setting)
{
        positive limit = history_number(setting, HISTORY_DEFAULT);

        if (history_used > limit)
                history_drop(0, history_used - limit);
}

static bool history_hold(string_address text, positive length)
{
        p8 address_to copy;

        if (!shell_array_room(history_text, history_text_room,
                              history_used + 1) ||
            !shell_array_room(history_bytes, history_bytes_room,
                              history_used + 1))
                return false;

        copy = (p8 address_to)shell_map(length + 1);

        if (!copy)
                return false;

        memory_copy_apart(copy, text, length);
        copy[length] = end;

        history_text[history_used] = copy;
        history_bytes[history_used] = length + 1;
        history_used++;

        return true;
}

/* History expansion is a reader transform over the same entries history and
   fc use.  These are reusable output workspaces, not another history table. */
static byte_store history_expanded;
static byte_store history_piece;
static byte_store history_changed;
static byte_store history_percent;
static byte_store history_sub_old;
static byte_store history_sub_new;
static positive history_percent_number;
static bool history_percent_known;
static bool history_substitution_known;

#define HISTORY_EXPAND_ERROR (-1)
#define HISTORY_EXPAND_RUN 0
#define HISTORY_EXPAND_PRINT 1

/* `%` names the word containing the last `?text?` match.  Find that word
   with the shell lexer so quote/operator boundaries stay the same as every
   other history word designator. */
static bool history_percent_set(string_address event, string_address wanted,
                                positive wanted_length)
{
        lex_frame frame;
        b32 count;
        bool answer = false;

        history_percent.used = 0;
        lex_nest_enter(address_of frame);
        count = lex_line(event);

        if (count >= 0)
                for (b32 at = 0; at < count; at++)
                        if (lex_tokens[at].kind != LEX_OPERATOR &&
                            memory_search(lex_tokens[at].text,
                                          lex_tokens[at].length, wanted,
                                          wanted_length))
                        {
                                answer = shell_bytes_add(
                                    address_of history_percent,
                                    lex_tokens[at].text,
                                    lex_tokens[at].length);
                                break;
                        }

        lex_nest_leave(address_of frame);
        history_percent_known = answer;
        return answer;
}

static bool history_event_at(string_address bang,
                             string_address address_to after,
                             positive address_to found)
{
        string_address at = bang + 1;
        positive index = history_used;
        positive number = 0;

        if (!history_used)
                goto missing;

        if (string_is(at, '!'))
        {
                index = history_used - 1;
                at++;
        }
        //      !:1-2 names no event, which is the previous one.
        else if (string_is(at, ':'))
                index = history_used - 1;
        else if (string_is(at, '-') && byte_is_digit(string_get(at + 1)))
        {
                at++;
                if (!string_digits_checked(address_of at, 10,
                                           address_of number))
                        goto missing;

                if (!number || number > history_used)
                        goto missing;
                index = history_used - number;
        }
        else if (byte_is_digit(string_get(at)))
        {
                if (!string_digits_checked(address_of at, 10,
                                           address_of number))
                        goto missing;

                if (number < history_first ||
                    number - history_first >= history_used)
                        goto missing;
                index = number - history_first;
        }
        else if (string_is(at, '?'))
        {
                string_address wanted = ++at;
                string_address close = string_first_of(at, '?');
                positive wanted_length;

                if (close)
                        at = close + 1;
                else
                        at += string_span_without_set(
                            at, " \t\n:=();&|-'\"\\");

                wanted_length = (positive)((close ? close : at) - wanted);
                if (!wanted_length)
                        goto missing;

                for (positive look = history_used; look;)
                {
                        look--;
                        if (memory_search(history_text[look],
                                          string_length(history_text[look]),
                                          wanted, wanted_length))
                        {
                                index = look;
                                break;
                        }
                }

                if (index >= history_used ||
                    !history_percent_set(history_text[index], wanted,
                                         wanted_length))
                        goto missing;
                if (index > positive_max - history_first)
                        goto missing;
                history_percent_number = history_first + index;
        }
        else if (string_is(at, '%'))
        {
                if (!history_percent_known)
                        goto missing;
                if (history_percent_number < history_first ||
                    history_percent_number - history_first >= history_used)
                        goto missing;
                index = history_percent_number - history_first;
                at++;
        }
        else if (string_is(at, '^') || string_is(at, '$') ||
                 string_is(at, '*'))
                index = history_used - 1;
        else
        {
                string_address prefix = at;
                positive length;

                at += string_span_without_set(at, " \t\n:=();&|-'\"\\");
                length = (positive)(at - prefix);
                if (!length)
                        goto missing;

                for (positive look = history_used; look;)
                {
                        look--;
                        if (!string_compare_max(history_text[look], prefix,
                                                length))
                        {
                                index = look;
                                break;
                        }
                }
        }

        if (index >= history_used)
                goto missing;

        address_to after = at;
        address_to found = index;
        return true;

missing:
        log_error("bash: ", 6);
        log_error(bang, (positive)(at - bang));
        return string_report(log_error, false, ": event not found\n");
}

enum
{
        HISTORY_WORD_ALL,
        HISTORY_WORD_ONE,
        HISTORY_WORD_RANGE,
        HISTORY_WORD_ARGS,
        HISTORY_WORD_LAST,
        HISTORY_WORD_PERCENT,
        HISTORY_WORD_RANGE_PENULTIMATE
};

static bool history_words(string_address event, b32 kind, positive first,
                          positive last)
{
        lex_frame frame;
        b32 count;
        positive words = 0;
        positive written = 0;
        bool answer = true;

        history_piece.used = 0;
        if (kind == HISTORY_WORD_PERCENT)
                return history_percent_known &&
                       shell_bytes_add(address_of history_piece,
                                         history_percent.bytes,
                                         history_percent.used);
        if (kind == HISTORY_WORD_ALL)
                return shell_bytes_add(address_of history_piece, event,
                                         string_length(event));

        lex_nest_enter(address_of frame);
        count = lex_line(event);

        if (count < 0)
        {
                answer = false;
                goto leave;
        }

        for (b32 at = 0; at < count; at++)
                if (lex_tokens[at].kind != LEX_OPERATOR)
                        words++;

        if (kind == HISTORY_WORD_LAST)
                first = last = words ? words - 1 : 0;
        else if (kind == HISTORY_WORD_ARGS)
        {
                first = 1;
                last = words ? words - 1 : 0;
        }
        else if (kind == HISTORY_WORD_RANGE && last == positive_max)
                last = words ? words - 1 : 0;
        else if (kind == HISTORY_WORD_RANGE_PENULTIMATE)
                last = words > 1 ? words - 2 : 0;

        if ((kind == HISTORY_WORD_ONE || kind == HISTORY_WORD_LAST) &&
            first >= words)
                answer = false;
        else if ((kind == HISTORY_WORD_RANGE ||
                  kind == HISTORY_WORD_RANGE_PENULTIMATE) &&
                 (first >= words || last >= words || last < first))
                answer = false;
        else if (kind == HISTORY_WORD_ARGS && words <= 1)
                goto leave;

        if (!answer)
                goto leave;

        words = 0;
        for (b32 at = 0; at < count; at++)
        {
                if (lex_tokens[at].kind == LEX_OPERATOR)
                        continue;

                if (words >= first && words <= last)
                {
                        if (written++ &&
                            !shell_bytes_byte(address_of history_piece, ' '))
                        {
                                answer = false;
                                break;
                        }

                        if (!shell_bytes_add(address_of history_piece,
                                               lex_tokens[at].text,
                                               lex_tokens[at].length))
                        {
                                answer = false;
                                break;
                        }
                }
                words++;
        }

leave:
        lex_nest_leave(address_of frame);
        return answer;
}

static bool history_substitute(string_address old, positive old_length,
                               string_address replacement,
                               positive replacement_length, bool global)
{
        positive copied = 0;
        bool changed = false;

        if (!old_length)
                return false;

        history_changed.used = 0;
        while (copied <= history_piece.used)
        {
                string_address found = (string_address)memory_search(
                    history_piece.bytes + copied,
                    history_piece.used - copied, old, old_length);

                if (!found)
                        break;

                positive at = (positive)(found - history_piece.bytes);
                if (!shell_bytes_add(address_of history_changed,
                                       history_piece.bytes + copied,
                                       at - copied) ||
                    !shell_bytes_add(address_of history_changed,
                                       replacement, replacement_length))
                        return false;

                copied = at + old_length;
                changed = true;
                if (!global)
                        break;
        }

        if (!changed)
                return false;

        if (!shell_bytes_add(address_of history_changed,
                               history_piece.bytes + copied,
                               history_piece.used - copied))
                return false;

        {
                byte_store held = history_piece;

                history_piece = history_changed;
                history_changed = held;
        }
        return true;
}

static bool history_sub_field(byte_store address_to into,
                              string_address address_to at, p8 delimiter,
                              bool replacement, bool closing_required)
{
        string_address step = address_to at;

        into->used = 0;
        while (string_get(step) && !string_is(step, delimiter))
        {
                p8 value = string_get(step++);

                if (value == '\\' && string_get(step) &&
                    (string_is(step, delimiter) || string_is(step, '\\') ||
                     (replacement && string_is(step, '&'))))
                        value = string_get(step++);
                else if (replacement && value == '&')
                {
                        if (!shell_bytes_add(into, history_sub_old.bytes,
                                               history_sub_old.used))
                                return false;
                        continue;
                }

                if (!shell_bytes_byte(into, value))
                        return false;
        }

        if (string_is(step, delimiter))
                step++;
        else if (closing_required)
                return false;

        address_to at = step;
        return true;
}

/* Read and retain one :s expression.  Keeping the normalized old and new
   fields is both smaller and safer than retaining pointers into a reader
   buffer, and gives :& the same substitution without another history copy. */
static bool history_substitution_read(string_address modifier,
                                      string_address address_to after)
{
        p8 delimiter = string_get(modifier + 1);
        string_address at = modifier + 2;

        if (!delimiter ||
            !history_sub_field(address_of history_sub_old, address_of at,
                               delimiter, false, true) ||
            !history_sub_old.used ||
            !history_sub_field(address_of history_sub_new, address_of at,
                               delimiter, true, false))
                return false;

        history_substitution_known = true;
        address_to after = at;
        return true;
}

static bool history_substitution_apply(bool global)
{
        return history_substitution_known &&
               history_substitute(history_sub_old.bytes,
                                  history_sub_old.used,
                                  history_sub_new.bytes,
                                  history_sub_new.used, global);
}

static bool history_piece_path(p8 modifier)
{
        /*      As bash's history library cuts them, which is not csh's: :h
                drops from the last slash and :r from the last dot, anywhere
                in the word, :t and :e keep from there, and a word without
                the slash or the dot is left as it is -- gamma:h is gamma,
                not ".", and /usr:h is empty. */
        positive start = 0;
        positive length = history_piece.used;
        p8 address_to mark = (p8 address_to)memory_last_of(
            history_piece.bytes, modifier == 'h' || modifier == 't' ? '/' : '.', length);

        if (mark)
        {
                positive at = (positive)(mark - history_piece.bytes);

                if (modifier == 'h' || modifier == 'r')
                        length = at;
                else
                {
                        start = modifier == 't' ? at + 1 : at;
                        length -= start;
                }
        }

        history_changed.used = 0;
        if (!shell_bytes_add(address_of history_changed,
                               history_piece.bytes + start, length))
                return false;

        {
                byte_store held = history_piece;
                history_piece = history_changed;
                history_changed = held;
        }
        return true;
}

/*      A substitution with nothing to substitute says so, as bash does,
        spelling the modifier the way it was typed: ":s^old^new^" for the
        ^ shorthand, ":s/old/new/" or ":gs/old/new/" after a designator. */
static fn history_substitution_failed(string_address from, string_address to,
                                      bool shorthand)
{
        log_error("bash: :", 7);
        if (shorthand)
                log_error("s", 1);
        log_error(from, (positive)(to - from));
        log_error(": substitution failed\n", 22);
        log_flush();
}

static bool history_quote_failed;

// A quoted word lands in history_changed; running out of room is remembered.
static fn history_quote_write(address_any bytes, positive length)
{
        if (!shell_bytes_add(address_of history_changed, bytes, length))
                history_quote_failed = true;
}

static bool history_piece_quote(bool split)
{
        positive at = 0;
        bool written = false;

        history_changed.used = 0;
        history_quote_failed = false;
        while (at < history_piece.used)
        {
                positive start = at;

                if (split)
                {
                        while (start < history_piece.used &&
                               (history_piece.bytes[start] == ' ' ||
                                history_piece.bytes[start] == '\t' ||
                                history_piece.bytes[start] == '\n'))
                                start++;
                        at = start;
                        while (at < history_piece.used &&
                               history_piece.bytes[at] != ' ' &&
                               history_piece.bytes[at] != '\t' &&
                               history_piece.bytes[at] != '\n')
                                at++;
                }
                else
                        at = history_piece.used;

                if (start == at && split)
                        break;
                if (written++ &&
                    !shell_bytes_byte(address_of history_changed, ' '))
                        return false;
                shell_single_quote_write(history_quote_write,
                                         history_piece.bytes + start,
                                         at - start);
        }

        if (!written)
                shell_single_quote_write(history_quote_write,
                                         (string_address) "", 0);
        if (history_quote_failed)
                return false;

        {
                byte_store held = history_piece;
                history_piece = history_changed;
                history_changed = held;
        }
        return true;
}

static bool history_word_designator(string_address address_to at,
                                    b32 address_to kind,
                                    positive address_to first,
                                    positive address_to last)
{
        string_address step = address_to at;
        bool colon = string_is(step, ':');
        p8 value = string_get(step + colon);
        positive number = 0;

        if (value != '^' && value != '$' && value != '*' && value != '%' &&
            value != '-' && !byte_is_digit(value))
                return true;

        step += colon;
        if (value == '^')
        {
                address_to kind = HISTORY_WORD_ONE;
                address_to first = address_to last = 1;
                step++;
        }
        else if (value == '$')
        {
                address_to kind = HISTORY_WORD_LAST;
                step++;
        }
        else if (value == '*')
        {
                address_to kind = HISTORY_WORD_ARGS;
                step++;
        }
        else if (value == '%')
        {
                address_to kind = HISTORY_WORD_PERCENT;
                step++;
        }
        else if (value == '-')
        {
                address_to kind = HISTORY_WORD_RANGE_PENULTIMATE;
                address_to first = 0;
                step++;

                if (byte_is_digit(string_get(step)))
                {
                        positive end_word = 0;

                        if (!string_digits_checked(address_of step, 10,
                                                   address_of end_word))
                                return false;
                        address_to kind = HISTORY_WORD_RANGE;
                        address_to last = end_word;
                }
        }
        else
        {
                if (!string_digits_checked(address_of step, 10,
                                           address_of number))
                        return false;

                address_to kind = HISTORY_WORD_ONE;
                address_to first = address_to last = number;
                if (string_is(step, '-'))
                {
                        positive end_word = 0;

                        step++;
                        if (string_is(step, '$'))
                        {
                                end_word = positive_max;
                                step++;
                        }
                        else
                        {
                                if (string_get(step) < '0' ||
                                    string_get(step) > '9')
                                {
                                        address_to kind =
                                            HISTORY_WORD_RANGE_PENULTIMATE;
                                        address_to last = 0;
                                        goto word_done;
                                }
                                if (!string_digits_checked(address_of step, 10,
                                                           address_of end_word))
                                        return false;
                        }
                        address_to kind = HISTORY_WORD_RANGE;
                        address_to last = end_word;
                }
                else if (string_is(step, '*'))
                {
                        address_to kind = HISTORY_WORD_RANGE;
                        address_to last = positive_max;
                        step++;
                }
        }

word_done:
        address_to at = step;
        return true;
}

/*
        The ! that bash's history leaves alone because the shell itself
        means something by it there, bash_history_inhibit_expansion's list:
        ${!name} and ${!prefix*}, which are indirection and not an event, a
        bracket expression's [!...] negation, and $! the last background
        pid. Without these ${!x} at an interactive prompt answered
        "!x}: event not found" and ran nothing, and [!a]* was an event too.
*/
static PURE bool history_bang_inhibited(string_address line, string_address at)
{
        positive before = (positive)(at - line);

        if (before >= 1 && string_get(at - 1) == '[' &&
            string_first_of(at + 1, ']'))
                return true;
        if (before >= 2 && string_get(at - 1) == '{' &&
            string_get(at - 2) == '$' && string_first_of(at + 1, '}'))
                return true;
        return before >= 1 && string_get(at - 1) == '$';
}

/* Expand one interactive physical line.  The caller remembers the returned
   text, so `history` and `fc` see exactly what was executed. */
b32 history_expand_line(string_address line,
                        string_address address_to expanded)
{
        string_address at = line;
        string_address run = line;
        bool single = false;
        bool double_quote = false;
        bool changed = false;
        bool print_only = false;

        address_to expanded = line;
        if (!shell_histexpand_on() || parse_here_open())
                return HISTORY_EXPAND_RUN;

        history_expanded.used = 0;

        /* The interactive shorthand for the previous event's first
           substitution.  It is the same transform as !!:s, not a separate
           lookup or execution path. */
        if (string_is(line, '^'))
        {
                string_address after = line + 1;

                if (!history_used ||
                    !history_sub_field(address_of history_sub_old,
                                       address_of after, '^', false, true) ||
                    !history_sub_old.used ||
                    !history_sub_field(address_of history_sub_new,
                                       address_of after, '^', true, false))
                        goto bad_event;
                history_substitution_known = true;

                if (!history_words(history_text[history_used - 1],
                                   HISTORY_WORD_ALL, 0, positive_max))
                        goto bad_event;
                if (!history_substitution_apply(false))
                {
                        history_substitution_failed(line, after, true);
                        goto bad_event;
                }
                if (
                    !shell_bytes_add(address_of history_expanded,
                                       history_piece.bytes,
                                       history_piece.used) ||
                    !shell_bytes_add(address_of history_expanded, after,
                                       string_length(after)))
                        goto bad_event;

                address_to expanded = history_expanded.bytes;
                string_format(log_error, "%s\n",
                              history_expanded.bytes);
                log_flush();
                return HISTORY_EXPAND_RUN;
        }

        while (string_get(at))
        {
                /* Most input is neither quoting nor history syntax.  Let the
                   shared byte-set scanner skip that run in one architecture-
                   tuned pass instead of paying four scalar branches per
                   ordinary byte. */
                string_address special =
                    string_first_of_set(at, (string_address) "\\\\'\"!");
                p8 value;

                if (!special)
                {
                        at += string_length(at);
                        break;
                }

                at = special;
                value = string_get(at);

                if (!single && value == '\\' && string_get(at + 1))
                {
                        p8 carried = string_get(at + 1);

                        /* Outside quotes a backslash carries every byte. In
                           double quotes it carries only the bytes that the
                           shell itself treats specially, plus ! for history.
                           Either way an escaped quote cannot change this
                           scanner's quote state. */
                        if (!double_quote || carried == '!' ||
                            carried == '"' || carried == '\\' ||
                            carried == '$' || carried == '\n')
                                at += 2;
                        else
                                at++;
                        continue;
                }

                if (value == '\'' && !double_quote)
                {
                        single = !single;
                        at++;
                        continue;
                }

                if (value == '"' && !single)
                {
                        double_quote = !double_quote;
                        at++;
                        continue;
                }

                if (value != '!' || single ||
                    (double_quote && shell_posix_on()) ||
                    !string_get(at + 1) ||
                    string_get(at + 1) == ' ' ||
                    string_get(at + 1) == '\t' ||
                    string_get(at + 1) == '=' ||
                    string_get(at + 1) == '(' ||
                    history_bang_inhibited(line, at))
                {
                        at++;
                        continue;
                }

                if (!shell_bytes_add(address_of history_expanded, run,
                                       (positive)(at - run)))
                        goto no_room;

                {
                        string_address event;
                        positive event_at;
                        b32 word_kind = string_is(at + 1, '%')
                                            ? HISTORY_WORD_PERCENT
                                            : HISTORY_WORD_ALL;
                        positive first = 0;
                        positive last = positive_max;

                        if (string_is(at + 1, '#'))
                        {
                                event = at + 2;
                                if (!history_word_designator(
                                        address_of event,
                                        address_of word_kind,
                                        address_of first,
                                        address_of last))
                                        goto bad_event;

                                history_changed.used = 0;
                                if (!shell_bytes_add(
                                        address_of history_changed, line,
                                        (positive)(at - line)) ||
                                    !history_words(history_changed.bytes,
                                                   word_kind, first, last))
                                        goto no_room;
                        }
                        else
                        {
                                string_address designator;

                                if (!history_event_at(at, address_of event,
                                                      address_of event_at))
                                        goto bad_event;
                                designator = event;
                                if (!history_word_designator(
                                        address_of event, address_of word_kind,
                                        address_of first, address_of last))
                                        goto bad_event;
                                //      Words the event has not got, said
                                //      as bash says it.
                                if (!history_words(history_text[event_at],
                                                   word_kind, first, last))
                                {
                                        log_error("bash: ", 6);
                                        log_error(designator,
                                                  (positive)(event - designator));
                                        log_error(": bad word specifier\n", 21);
                                        log_flush();
                                        goto bad_event;
                                }
                        }

                        while (string_is(event, ':'))
                        {
                                string_address modifier = event + 1;
                                bool global = false;

                                if (string_is(modifier, 'g') &&
                                    (string_is(modifier + 1, 's') ||
                                     string_is(modifier + 1, '&')))
                                {
                                        global = true;
                                        modifier++;
                                }

                                if (string_is(modifier, 'p'))
                                {
                                        print_only = true;
                                        event = modifier + 1;
                                        continue;
                                }

                                if (string_is(modifier, '&'))
                                {
                                        if (!history_substitution_apply(
                                                global))
                                                goto bad_event;
                                        event = modifier + 1;
                                        continue;
                                }

                                if (string_is(modifier, 'h') ||
                                    string_is(modifier, 't') ||
                                    string_is(modifier, 'r') ||
                                    string_is(modifier, 'e'))
                                {
                                        if (!history_piece_path(
                                                string_get(modifier)))
                                                goto no_room;
                                        event = modifier + 1;
                                        continue;
                                }

                                if (string_is(modifier, 'q') ||
                                    string_is(modifier, 'x'))
                                {
                                        if (!history_piece_quote(
                                                string_is(modifier, 'x')))
                                                goto no_room;
                                        event = modifier + 1;
                                        continue;
                                }

                                if (!string_is(modifier, 's') ||
                                    !string_get(modifier + 1))
                                        goto unsupported_modifier;

                                if (!history_substitution_read(
                                        modifier, address_of event))
                                        goto bad_event;
                                if (!history_substitution_apply(global))
                                {
                                        history_substitution_failed(
                                            modifier - (global ? 1 : 0),
                                            event, false);
                                        goto bad_event;
                                }
                        }

                        if (!shell_bytes_add(address_of history_expanded,
                                               history_piece.bytes,
                                               history_piece.used))
                                goto no_room;

                        changed = true;
                        at = event;
                        run = at;
                }
        }

        if (!changed)
                return HISTORY_EXPAND_RUN;

        if (!shell_bytes_add(address_of history_expanded, run,
                               (positive)(at - run)))
                goto no_room;

        address_to expanded = history_expanded.bytes;
        string_format(log_error, "%s\n", history_expanded.bytes);
        log_flush();
        return print_only ? HISTORY_EXPAND_PRINT : HISTORY_EXPAND_RUN;

bad_event:
        return HISTORY_EXPAND_ERROR;
unsupported_modifier:
        log_error(str("bash: history expansion: unsupported modifier\n"));
        log_flush();
        return HISTORY_EXPAND_ERROR;
no_room:
        log_error(str("bash: history expansion: no room\n"));
        shell_status = 1;
        return HISTORY_EXPAND_ERROR;
}

/*
        Whether a line is worth remembering, which is not the shell's opinion.

        HISTCONTROL and HISTIGNORE are how a person says what their own
        history is for: a password typed after a space, a loop of the same
        command, a `ls` they will never want back. All three are checked
        before the copy is taken, because the point of them is that the line
        never enters the store at all.
*/
static bool history_wanted(string_address text, positive length)
{
        string_address control = env_get((const_string) "HISTCONTROL");
        string_address ignore = env_get((const_string) "HISTIGNORE");
        bool space = false;
        bool dedupe = false;
        bool erase = false;

        if (!length)
                return false;

        for (string_address at = control; at && string_get(at);)
        {
                string_address stop = string_first_of_or_end(at, ':');
                positive span = (positive)(stop - at);

                if (memory_is_word(at, span, "ignorespace"))
                        space = true;
                else if (memory_is_word(at, span, "ignoredups"))
                        dedupe = true;
                else if (memory_is_word(at, span, "erasedups"))
                        erase = true;
                else if (memory_is_word(at, span, "ignoreboth"))
                {
                        space = true;
                        dedupe = true;
                }

                at = string_get(stop) ? stop + 1 : stop;
        }

        if (space && (string_get(text) == ' ' || string_get(text) == '\t'))
                return false;

        if (dedupe && history_used &&
            !string_compare(history_text[history_used - 1], text))
                return false;

        for (string_address at = ignore; at && string_get(at);)
        {
                static p8 address_to pattern;
                static positive pattern_room;
                string_address stop = string_first_of_or_end(at, ':');
                positive span = (positive)(stop - at);

                if (!shell_array_room(pattern, pattern_room, span + 1))
                        break;

                memory_copy_apart(pattern, at, span);
                pattern[span] = end;

                if (span && shell_match(pattern, text))
                        return false;

                at = string_get(stop) ? stop + 1 : stop;
        }

        if (erase)
                for (positive at = 0; at < history_used;)
                {
                        if (!string_compare(history_text[at], text))
                                history_drop(at, 1);
                        else
                                at++;
                }

        return true;
}

/*
        One line, as it was typed.

        The reader calls this and nothing else does: an eval, a sourced file
        and a trap action are lines this shell wrote for itself, and a history
        of them is a history of the shell rather than of the person.
*/
fn history_remember(string_address line)
{
        positive length;

        if (!line)
                return;

        length = string_length(line);

        while (length && (line[length - 1] == '\n' || line[length - 1] == '\r'))
                length--;

        if (string_span_max(line, length, string_set_blanks) == length)
                return;

        {
                static p8 address_to held;
                static positive held_room;

                if (!shell_array_room(held, held_room, length + 1))
                        return;

                memory_copy_apart(held, line, length);
                held[length] = end;

                if (!history_wanted(held, length))
                        return;

                history_hold(held, length);
        }

        history_trim((string_address) "HISTSIZE");
}

// The whole of a file, however long it is. A history file is lines, and a
// reader that stopped at a fixed size would silently lose the oldest of them.
static p8 address_to history_slurp(string_address path,
                                   positive address_to length)
{
        static byte_store held;

        if (!file_store_slurp(path, address_of held))
                return null;

        address_to length = held.used;
        return held.bytes;
}

static positive history_read(string_address path, positive skip)
{
        positive length = 0;
        p8 address_to text = history_slurp(path, address_of length);
        positive at = 0;
        positive seen = 0;

        if (!text)
                return 0;

        while (at < length)
        {
                positive stop = at + memory_span_without_byte(
                    text + at, '\n', length - at);

                if (stop > at && seen++ >= skip)
                        history_hold(text + at, stop - at);

                at = stop + 1;
        }

        history_trim((string_address) "HISTSIZE");

        return seen;
}

static bipolar history_write_lines(bipolar handle, positive from)
{
        for (positive at = from; at < history_used; at++)
        {
                positive length = string_length(history_text[at]);
                system_write_result written = system_write_all_checked(
                    (positive)handle, history_text[at], length);

                if (written.bytes != length)
                        return written.error ? written.error
                                             : -ERROR_INPUT_OUTPUT;

                written = system_write_all_checked((positive)handle, "\n", 1);
                if (written.bytes != 1)
                        return written.error ? written.error
                                             : -ERROR_INPUT_OUTPUT;
        }

        return 0;
}

/* The history file's parent, opened, and what its leaf is now. The parent is
   closed again on every refusal: a look that failed other than for a missing
   leaf, facts without the basic mask, an existing endpoint this process may
   not write through, and a final-component symlink. */
static bipolar history_destination_open(string_address path,
                                        p8 address_to leaf,
                                        file_facts address_to existing,
                                        bool address_to exists)
{
        bipolar directory = file_parent_open(path, leaf);
        if (directory < 0)
                return directory;

        bipolar looked = file_look_code(directory, leaf,
                                        AT_SYMLINK_NOFOLLOW, existing);
        bipolar refused = 0;

        address_to exists = looked >= 0;
        if (looked < 0 && looked != -ERROR_NO_ENTRY)
                refused = looked;
        else if (address_to exists &&
                 (existing->mask & STATX_BASIC) != STATX_BASIC)
                refused = -ERROR_INPUT_OUTPUT;
        else if (address_to exists &&
                 !file_direct_endpoint_authorized(directory, existing))
                refused = -ERROR_ACCESS;
        else if (address_to exists &&
                 (existing->mode & MODE_FORMAT) == MODE_LINK)
                refused = -ERROR_TOO_MANY_LEVELS;

        if (!refused)
                return directory;

        system_close(directory);
        return refused;
}

static bipolar history_append(string_address path, positive from)
{
        p8 leaf[FILE_PATH_MAX];
        file_facts existing;
        bool exists;
        bipolar directory = history_destination_open(
            path, leaf, address_of existing, address_of exists);
        if (directory < 0)
                return directory;

        positive flags = (FILE_APPEND & ~FILE_CREATE) |
                         O_NOFOLLOW | O_CLOEXEC;
        if (exists && (existing.mode & MODE_FORMAT) == MODE_FILE)
                flags |= O_NONBLOCK;
        bipolar handle = exists
                             ? file_direct_endpoint_open(
                                   directory, leaf, address_of existing,
                                   flags)
                             : system_open_at_mode(
                                   directory, leaf,
                                   FILE_APPEND | FILE_EXCLUSIVE |
                                       O_NOFOLLOW | O_CLOEXEC,
                                   0600);

        if (handle < 0)
        {
                system_close(directory);
                return handle;
        }

        bipolar result = history_write_lines(handle, from);
        bipolar closed = system_close(handle);
        /* Closing the read-only parent cannot undo bytes already appended.
           Do not report a false failure that would append the same history
           range again on the next save. */
        (void)system_close(directory);

        return result < 0 ? result : closed;
}

static bipolar history_write_direct_at(bipolar directory,
                                       string_address leaf,
                                       file_facts address_to expected,
                                       positive from)
{
        positive flags = (FILE_WRITE & ~(FILE_CREATE | O_TRUNC)) |
                         O_NOFOLLOW | O_CLOEXEC;
        bipolar handle = file_direct_endpoint_open(
            directory, leaf, expected, flags);

        if (handle < 0)
                return handle;

        bipolar result = history_write_lines(handle, from);
        bipolar closed = system_close(handle);
        return result < 0 ? result : closed;
}

/* Regular histories are prepared beside a pinned destination and published
   only after their writes, metadata, sync and writing-descriptor close have
   all succeeded. Missing destinations use RENAME_NOREPLACE; existing ones
   use their original identity in the exchange. Special files retain stream
   semantics, while a final-component symlink fails closed. */
static bipolar history_replace(string_address path, positive from)
{
        p8 leaf[FILE_PATH_MAX];
        file_facts existing;
        bool exists;
        bipolar directory = history_destination_open(
            path, leaf, address_of existing, address_of exists);
        if (directory < 0)
                return directory;

        positive kind = exists ? existing.mode & MODE_FORMAT : MODE_FILE;
        if (exists && kind != MODE_FILE)
        {
                bipolar result = history_write_direct_at(
                    directory, leaf, address_of existing, from);
                bipolar closed = system_close(directory);
                return result < 0 ? result : closed;
        }

        positive mode = exists ? file_replacement_mode(existing.mode) : 0600;
        system_path_stage protected;
        bipolar handle = file_stage_file_open_at(
            address_of protected, directory, leaf, 0600);
        if (handle < 0)
        {
                system_close(directory);
                return handle;
        }

        bipolar result = history_write_lines(handle, from);
        if (result >= 0 && exists)
                result = system_call_3(syscall(fchown),
                                       (positive)handle,
                                       existing.owner,
                                       existing.group);
        if (result >= 0)
                result = system_call_2(syscall(fchmod),
                                       (positive)handle, mode);
        if (result >= 0)
                result = system_call_1(syscall(fsync),
                                       (positive)handle);

        result = file_stage_publish_protected_at(
            address_of protected, directory, leaf, handle, result,
            !exists, exists ? address_of existing : null, 0);
        /* A successful protected rename committed the history.  The parent
           descriptor is read-only, so a later close error cannot make that
           publication fail or justify writing the range a second time. */
        (void)system_close(directory);
        return result;
}

static bool history_write(string_address path, positive from, bool append)
{
        bipolar result = append ? history_append(path, from)
                                : history_replace(path, from);

        if (result < 0)
                return string_report(log_error, false,
                                     "history: %w: cannot write: %s\n",
                                     writer_terminal_name, path,
                                     file_reason(result));

        history_saved = history_used;

        return true;
}

// What was there before this session, at a terminal and nowhere else: a
// script's history is a history nobody will ever read back.
fn history_start()
{
        string_address path;

        if (!shell_is_interactive)
                return;

        //      An interactive bash keeps its history in ~/.bash_history
        //      unless told otherwise, and remembers five hundred lines.
        if (shell_bash_compat && !env_get("HISTFILE"))
        {
                string_address home = env_get("HOME");

                if (home && string_get(home))
                {
                        p8 address_to made = null;
                        positive room = 0;
                        positive length = string_length(home);

                        if (shell_array_room(made, room, length + 16))
                        {
                                memory_copy(made, home, length);
                                memory_copy_end(made + length, "/.bash_history",
                                                14);
                                env_assign("HISTFILE", made);
                                memory_free(made, room);
                        }
                }
                //      Not for -c, which reads no lines to remember.
                if (!string_is(shell_option_flags, 'c'))
                {
                        if (!env_get("HISTSIZE"))
                                env_assign("HISTSIZE", "500");
                        if (!env_get("HISTFILESIZE"))
                                env_assign("HISTFILESIZE", "500");
                }
        }

        path = history_file();

        if (path)
                history_read(path, 0);

        history_saved = history_used;
}

fn history_leaving()
{
        string_address path;

        /* A subshell of an interactive shell is still interactive, and it did
           not read any of these lines: letting it write the file would have a
           `( exit )` decide what the session remembers. */
        if (!shell_is_interactive || exec_forked)
                return;

        path = history_file();

        if (!path)
                return;

        //      bash writes the file only when this session remembered a
        //      line: bash -i -c, which remembers none, leaves no file.
        if (shell_bash_compat && history_used == history_saved)
                return;

        history_trim((string_address) "HISTFILESIZE");
        history_write(path, 0, false);
}

/*
        A line of a listing, in whichever of the two shapes was asked for.

        `history` right-justifies the number in five columns and follows it
        with two spaces; `fc -l` writes the number, a tab and a space, and
        `fc -ln` writes the tab and space with no number in front. They are
        not the same layout and never were, so both are here rather than one
        of them being made to stand for the other.
*/
static fn history_listed(writer write, positive at)
{
        p8 shown[24];

        positive_into_string(shown, history_first + at);
        writer_field_bulk(write, shown, string_length(shown), 5, ' ', false);
        string_format(write, "  %s\n", history_text[at]);
}

static fn history_listed_fc(writer write, positive at, bool numbered)
{
        if (numbered)
                string_format(write, "%p", history_first + at);

        //      Bash writes a tab and a space; its POSIX mode writes the tab
        //      alone, which is what POSIX spells for fc -l.
        string_format(write, shell_posix_on() ? "\t%s\n" : "\t %s\n",
                      history_text[at]);
}

fn shell_history(writer write, string_address input)
{
        positive show = history_used;
        positive at = 1;
        string_address path = history_file();

        (void)input;

        while (at < shell_argc && string_get(shell_argv[at]) == '-' &&
               string_get(shell_argv[at] + 1))
        {
                p8 letter = string_get(shell_argv[at] + 1);
                string_address named = at + 1 < shell_argc ? shell_argv[at + 1]
                                                           : null;

                // "--" ends the options; bash then lists as if bare.
                if (word_is(shell_argv[at], "--"))
                {
                        at++;
                        break;
                }

                switch (letter)
                {
                case 'c':
                        history_drop(0, history_used);

                        history_first = 1;
                        history_saved = 0;
                        at++;
                        continue;

                case 'd':
                {
                        bipolar offset;

                        if (!named)
                                return shell_answered(2, "history: -d wants an offset\n");

                        if (!exec_control_integer(named, address_of offset))
                        {
                                return shell_refuse(1,
                                    "history: %s: invalid number\n", named);
                        }

                        if (offset < 0)
                                offset += (bipolar)(history_first +
                                                    history_used);
                        offset -= (bipolar)history_first;

                        if (offset < 0 || (positive)offset >= history_used)
                                return shell_answered(1, "history: %s: not in the"
                                           " history\n",
                                           named);

                        history_drop((positive)offset, 1);

                        return shell_answer(0);
                }

                case 'a':
                case 'n':
                case 'r':
                case 'w':
                {
                        string_address where = named ? named : path;

                        if (!where)
                                return shell_answered(1, "history: no history file\n");

                        /* Restricted Bash permits its inherited HISTFILE but
                           does not let a command select another directory by
                           spelling an explicit filename with '/'. */
                        if (shell_restricted && named &&
                            string_first_of(named, '/'))
                                return shell_answered(1,
                                    "history: %s: restricted\n", named);

                        if (letter == 'a')
                                return shell_answer(
                                    history_write(where, history_saved, true)
                                        ? 0
                                        : 1);

                        if (letter == 'w')
                                return shell_answer(
                                    history_write(where, 0, false) ? 0 : 1);

                        if (letter == 'r')
                        {
                                history_read(where, 0);
                                return shell_answer(0);
                        }

                        history_read(where, history_saved);

                        return shell_answer(0);
                }

                case 's':
                {
                        static byte_store joined;

                        if (!shell_argv_joined(at + 1, address_of joined))
                                return shell_answer(1);
                        if (joined.used)
                                history_hold((string_address)joined.bytes,
                                             joined.used);

                        return shell_answer(0);
                }

                default:
                        return shell_letter_refuse("history", letter,
                            "history [-c] [-d offset] [n] or "
                            "history -anrw [filename] or "
                            "history -ps arg [arg...]");
                }
        }

        if (at < shell_argc)
        {
                positive wanted;

                if (!string_digits_exact(shell_argv[at], address_of wanted))
                {
                        shell_diagnostic_where();

                        //      Two, the status a usage error carries, and
                        //      not the one a failed listing would.
                        return shell_answered(2,
                            "history: %s: numeric argument required\n",
                            shell_argv[at]);
                }

                if (wanted < show)
                        show = wanted;
        }

        for (positive line = history_used - show; line < history_used; line++)
                history_listed(write, line);

        shell_answer(0);
}

/*
        fc, which is the history with an editor attached.

        The command being run is not part of what it operates on: `fc -l` is
        entered before it runs, like every other line, and a person asking for
        the last sixteen commands does not mean this one. Bash draws the same
        line, which is why the count below stops one short.
*/
static PURE positive history_range_count()
{
        //      The line asking is itself in the history when somebody typed
        //      it, and fc never operates on itself. A script's fc was never
        //      entered, so nothing is set aside there and "fc -s" reaches
        //      the last line remembered rather than the one before it.
        if (!shell_is_interactive)
                return history_used;

        return history_used ? history_used - 1 : 0;
}

static bool history_locate(string_address word, positive fallback,
                           positive address_to found)
{
        positive count = history_range_count();
        positive digits;
        bipolar offset;

        address_to found = fallback;

        if (!word)
                return true;

        //      A minus and digits is an offset from the end; a minus
        //      and anything else is not a number at all, and looking for a
        //      line called "-l" is what fc does with it.
        {
                positive signed_digits;

                if (string_is(word, '-') &&
                    !string_digits_exact(word + 1, address_of signed_digits))
                        goto by_prefix;
        }

        if (string_get(word) == '-' ||
            string_digits_exact(word, address_of digits))
        {
                offset = string_to_bipolar(word);

                if (offset < 0)
                        offset += (bipolar)count;
                else
                        offset -= (bipolar)history_first;

                if (offset < 0)
                        offset = 0;

                if ((positive)offset >= count)
                        offset = count ? (bipolar)count - 1 : 0;

                address_to found = (positive)offset;

                return count != 0;
        }

by_prefix:
        for (positive at = count; at;)
        {
                at--;

                if (!string_compare_max(history_text[at], word,
                                        string_length(word)))
                {
                        address_to found = at;
                        return true;
                }
        }

        return false;
}

/*
        A remembered line, run again.

        Nested the way eval nests: the parser is standing in the middle of the
        `fc` that asked for this, and a line fed to it without its own lexer
        storage and parser marks is a second sentence written over the first.
*/
static fn history_run_text(writer write, string_address text)
{
        //      The line being run again is announced on the diagnostic
        //      channel, not among the answers: what the line writes is the
        //      answer, and a script reading it wants only that.
        (void)write;
        string_format(log_error, "%s\n", text);
        log_flush();
        exec_run_nested(text, true, 0);
}

/* A name another user cannot prepare before fc gets there.  Entropy failure
   is an error: weakening this to a pid or clock restores the /tmp race this
   directory exists to remove. */
#define HISTORY_EDIT_RANDOM 16
#define HISTORY_EDIT_PREFIX "/tmp/mw-fc."
#define HISTORY_EDIT_FILE "/commands"

static bool history_edit_directory(p8 address_to path)
{
        p8 random[HISTORY_EDIT_RANDOM];

        string_copy(path, HISTORY_EDIT_PREFIX);

        for (positive attempt = 0; attempt < 64; attempt++)
        {
                if (system_random_fill(random, sizeof(random), 0) < 0)
                        return false;

                memory_into_hex(path + sizeof(HISTORY_EDIT_PREFIX) - 1,
                                random, sizeof(random));
                path[sizeof(HISTORY_EDIT_PREFIX) - 1 +
                     sizeof(random) * 2] = end;

                bipolar made = system_make_directory_exact_at(
                    AT_FDCWD, path, 0700);

                if (made >= 0)
                        return true;
                if (made != -ERROR_EXISTS)
                        return false;
        }

        return false;
}

/* Remove the edited file and ordinary editor side files while the private
   directory descriptor still names the object we created.  A final pathname
   rmdir only removes that same empty directory; /tmp's sticky bit and the
   random name keep another user from substituting one. */
static fn history_edit_cleanup(bipolar directory, string_address path)
{
        if (directory >= 0)
        {
                file_walk walk;

                (void)system_seek(directory, 0, 0);
                walk.handle = directory;
                walk.error = 0;
                walk.have = 0;
                walk.at = 0;

                while (true)
                {
                        struct linux_dirent64 address_to entry =
                            file_walk_next(address_of walk);

                        if (!entry)
                        {
                                if (walk.error == -4) /* EINTR */
                                {
                                        walk.error = 0;
                                        walk.have = 0;
                                        walk.at = 0;
                                        continue;
                                }
                                break;
                        }

                        if (file_is_dot(entry->d_name))
                                continue;

                        if (system_remove_at(directory, entry->d_name, 0) < 0)
                                (void)system_remove_at(directory, entry->d_name,
                                                       AT_REMOVEDIR);
                }

                file_walk_close(address_of walk);
        }

        (void)system_remove_at(AT_FDCWD, path, AT_REMOVEDIR);
}

/* The editor receives normal shell syntax, as FCEDIT promises, but in a
   child.  `exit`, traps and assignments in that syntax therefore cannot skip
   the parent's descriptor-based read and cleanup. */
static b32 history_edit_run_editor(string_address command)
{
        bipolar child;
        positive raw = 0;

        log_flush();
        child = shell_clone();

        if (child == 0)
        {
                trap_default_all();
                shell_child_default(SIGNAL_INTERRUPT);
                shell_child_default(SIGNAL_QUIT);
                exec_child_began();
                exec_run_nested(command, true, 0);
                exec_child_leave(shell_status);
        }

        if (child < 0 ||
            system_wait4_retry(child, address_of raw, 0, null) < 0)
                return 1;

        return wait_status_code(raw);
}

static b32 history_edit(writer write, string_address editor, positive first,
                        positive last)
{
        static p8 path[sizeof(HISTORY_EDIT_PREFIX) + HISTORY_EDIT_RANDOM * 2];
        static p8 address_to command;
        static positive command_room;
        byte_store edited = {null, 0, 0};
        file_facts facts;
        bipolar directory = -1;
        positive length;
        bipolar handle = -1;
        b32 editor_status;
        bool wrote = true;

        if (!history_edit_directory(path))
                return string_report(log_error, 1,
                                     "fc: cannot make private edit directory\n",
                                     0);

        directory = system_open_at(
            AT_FDCWD, path,
            FILE_READ | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);

        if (directory < 0 ||
            system_call_2(syscall(fchmod), (positive)directory, 0700) < 0)
                goto failed;

        handle = system_open_at_mode(
            directory, "commands",
            FILE_READ_WRITE | FILE_CREATE | FILE_EXCLUSIVE | O_NOFOLLOW |
                O_CLOEXEC,
            0600);

        if (handle < 0)
                goto failed;

        if (system_call_2(syscall(fchmod), (positive)handle, 0600) < 0)
                goto failed;

        for (positive at = first; at <= last && at < history_used; at++)
        {
                positive line_length = string_length(history_text[at]);

                if (system_write_all((positive)handle, history_text[at],
                                     line_length) != (bipolar)line_length ||
                    system_write_all((positive)handle, "\n", 1) != 1)
                {
                        wrote = false;
                        break;
                }
        }

        if (system_close(handle) < 0)
                wrote = false;
        handle = -1;

        if (!wrote)
                goto failed;

        positive editor_length = string_length(editor);
        positive path_length = string_length(path);
        positive file_length = sizeof(HISTORY_EDIT_FILE) - 1;

        /* blank, opening quote, closing quote and terminator */
        if (path_length > positive_max - file_length ||
            editor_length > positive_max - path_length - file_length - 4)
                goto failed;
        length = editor_length + path_length + file_length + 4;

        if (!shell_array_room(command, command_room, length))
                goto failed;

        memory_copy_apart(command, editor, editor_length);
        command[editor_length] = ' ';
        command[editor_length + 1] = '\'';
        memory_copy_apart(command + editor_length + 2, path, path_length);
        memory_copy_apart(command + editor_length + 2 + path_length,
                          HISTORY_EDIT_FILE, file_length);
        command[editor_length + 2 + path_length + file_length] = '\'';
        command[editor_length + 3 + path_length + file_length] = end;

        editor_status = history_edit_run_editor((string_address)command);

        if (editor_status)
        {
                history_edit_cleanup(directory, (string_address)path);
                return editor_status;
        }

        /* A confined editor can leave a descendant which still knows this
           private pathname.  Do not turn bytes that process can continue to
           change into shell commands after the editor itself has returned. */
        if (floodlight_parent_supervised &&
            floodlight_descendants_present())
                goto failed;

        handle = system_open_at(directory, "commands",
                                FILE_READ | O_NOFOLLOW | O_CLOEXEC);

        if (handle < 0 ||
            !file_look(handle, "", AT_EMPTY_PATH, address_of facts) ||
            (facts.mode & MODE_FORMAT) != MODE_FILE ||
            file_store_read((positive)handle, address_of edited) < 0)
                goto failed;

        if (system_close(handle) < 0)
        {
                handle = -1;
                goto failed;
        }
        handle = -1;

        /* From here on the text is memory-owned.  Remove every temporary
           object before an edited command can exit, recurse into fc, or
           otherwise transfer control away from this call. */
        history_edit_cleanup(directory, (string_address)path);
        directory = -1;
        shell_status = 0;

        {
                positive at = 0;

                while (at < edited.used)
                {
                        positive stop = at + memory_span_without_byte(
                            edited.bytes + at, '\n', edited.used - at);

                        edited.bytes[stop] = end;

                        if (stop > at)
                                history_run_text(
                                    write, (string_address)edited.bytes + at);

                        at = stop + 1;
                }
        }

        byte_store_release(address_of edited);
        return shell_status;

failed:
        if (handle >= 0)
                system_close(handle);
        history_edit_cleanup(directory, (string_address)path);
        byte_store_release(address_of edited);
        return string_report(log_error, 1, "fc: cannot edit history safely\n",
                             0);
}

fn shell_fc(writer write, string_address input)
{
        positive count = history_range_count();
        positive at = 1;
        bool listing = false;
        bool numbered = true;
        bool reversed = false;
        bool again = false;
        string_address editor = null;
        string_address replace = null;
        positive first;
        positive last;

        (void)input;

        while (at < shell_argc && string_get(shell_argv[at]) == '-' &&
               string_get(shell_argv[at] + 1))
        {
                p8 letter = string_get(shell_argv[at] + 1);

                if (letter == '-' && !string_get(shell_argv[at] + 2))
                {
                        at++;
                        break;
                }

                if (letter == 'e')
                {
                        if (at + 1 >= shell_argc)
                                return shell_answered(2, "fc: -e wants an editor\n");

                        editor = shell_argv[++at];

                        // `fc -e -` is how the option spelling asks for the
                        // re-execution `fc -s` asks for by name.
                        if (!string_compare(editor, (string_address) "-"))
                        {
                                again = true;
                                editor = null;
                        }

                        at++;
                        continue;
                }

                for (positive step = 1; string_get(shell_argv[at] + step);
                     step++)
                        switch (string_get(shell_argv[at] + step))
                        {
                        case 'l':
                                listing = true;
                                break;
                        case 'n':
                                numbered = false;
                                break;
                        case 'r':
                                reversed = true;
                                break;
                        case 's':
                                again = true;
                                break;
                        default:
                                return shell_letter_refuse("fc",
                                    string_get(shell_argv[at] + step),
                                    "fc [-e ename] [-lnr] [first] [last] or "
                                    "fc -s [pat=rep] [command]");
                        }

                at++;
        }

        if (again && at < shell_argc && string_first_of(shell_argv[at], '='))
                replace = shell_argv[at++];

        //      -s runs the line again, and running it is not listing it:
        //      Bash lets the re-execution win over an -l given beside it.
        if (again)
                listing = false;

        if (!count)
        {
                // Nothing to work on is not a failure when nothing was asked
                // for either: a shell with no history lists none and says so
                // by saying nothing.
                if (listing && at >= shell_argc)
                        return shell_answer(0);

                //      Bash keeps quiet about an empty history whatever was
                //      asked of it: there is no line to name, so there is
                //      nothing it could say was not found.
                if (shell_bash_compat)
                        return shell_answer(0);

                return shell_answered(1, "fc: no command found\n");
        }

        if (!history_locate(at < shell_argc ? shell_argv[at] : null,
                            again ? count - 1
                                  : listing ? (count > 16 ? count - 16 : 0)
                                            : count - 1,
                            address_of first))
                return shell_answered(1, "fc: %s: no such command\n",
                           shell_argv[at]);

        if (at < shell_argc)
                at++;

        if (!history_locate(at < shell_argc ? shell_argv[at] : null,
                            again ? first : listing ? count - 1 : first,
                            address_of last))
                return shell_answered(1, "fc: %s: no such command\n",
                           shell_argv[at]);

        if (last < first)
        {
                positive held = first;

                first = last;
                last = held;
                reversed = !reversed;
        }

        if (listing)
        {
                if (reversed)
                        for (positive line = last + 1; line > first;)
                                history_listed_fc(write, --line, numbered);
                else
                        for (positive line = first; line <= last; line++)
                                history_listed_fc(write, line, numbered);

                return shell_answer(0);
        }

        if (again)
        {
                static p8 address_to built;
                static positive built_room;
                string_address text = history_text[first];

                if (!replace)
                {
                        history_run_text(write, text);
                        return;
                }

                {
                        string_address split = string_first_of(replace, '=');
                        positive old_length = (positive)(split - replace);
                        string_address new_text = split + 1;
                        string_address where;
                        positive prefix;

                        /* What is being replaced is the front of the operand,
                           which is not a string of its own -- its equals sign
                           is still attached. Comparing that many bytes at
                           each position finds it without the operand having
                           to be cut up first. */
                        for (where = text; string_get(where); where++)
                                if (!string_compare_max(where, replace,
                                                        old_length))
                                        break;

                        if (!old_length || !string_get(where))
                        {
                                history_run_text(write, text);
                                return;
                        }

                        prefix = (positive)(where - text);

                        if (!shell_array_room(built, built_room,
                                              string_length(text) + string_length(new_text) + 1))
                                return shell_answer(1);

                        memory_copy_apart(built, text, prefix);
                        string_copy(built + prefix, new_text);
                        string_copy(built + prefix + string_length(new_text),
                                    where + old_length);
                        history_run_text(write, built);
                }

                return;
        }

        if (!editor)
                editor = env_get((const_string) "FCEDIT");

        if (!editor || !string_get(editor))
                editor = env_get((const_string) "EDITOR");

        if (!editor || !string_get(editor))
                editor = (string_address) "ed";

        shell_answer(history_edit(write, editor, first, last));
}

static string_address exec_arena_copy(string_address text)
{
        string_address into = shell_store_copy(address_of exec_store, text,
                                                string_length(text));
        return into ? into : exec_nothing;
}

/*
        Redirection, as file descriptors and not as a writer.

        The shell used to swap the function its builtins printed through, which
        left a builtin redirected and a spawned program not, and neither of
        them redirected inside a pipeline. Moving fd 1 is what the child of a
        pipe needs anyway, so there is one mechanism instead of two, and the
        buffered output is flushed before the descriptor moves under it.
*/
typedef struct
{
        b32 fd;
        b32 saved;
} exec_saved_fd;

static exec_saved_fd exec_saves[REDIRECT_SAVE_MAX];
static b32 exec_save_count;

/*
        A child never puts back what its parent put aside: it leaves before
        the parent's commands end. The copies were close-on-exec, which kept
        them from programs the child ran but not from the child itself, so a
        subshell waiting on a program held the parent's saved stdout open --
        and x=$( (sleep 1) >/dev/null ) waited a second for a pipe it had
        redirected away from. The parser's own descriptor stays.
*/
static b32 exec_child_root;

static fn exec_saves_child_drop()
{
        for (b32 at = 0; at < exec_save_count; at++)
                if (exec_saves[at].saved >= 0 &&
                    !(shell_parser_source_active &&
                      shell_parser_source_handle == exec_saves[at].saved))
                        system_close(exec_saves[at].saved);
        exec_save_count = 0;
        exec_child_root = 0;
}
static b32 exec_redirect_status;
// Only the top-level script reader streams an open file across commands.
// Its descriptor can move when user redirections claim the same number.
static b32 exec_script_fd = -1;
/* Shared with for/select list expansion below. Redirect fields are consumed
   before execution reaches another node, so one retained pointer table serves
   both without another growth path. */
static string_address address_to exec_fields;
static positive exec_fields_room;

#define F_DUPFD 0
#define F_DUPFD_CLOEXEC 1030
#define REDIR_VAR_FLOOR 10

// The longest name a coprocess pair may be called, which is what the NAME_PID
// buffer beside it is sized from.
#define EXEC_COPROC_NAME 128
// A save may not occupy a descriptor this command is going to redirect. A
// shell-owned descriptor additionally excludes numeric duplication sources:
// it may have been relocated onto one that was closed to the script.
static PURE bool exec_redirect_target_is(parse_node address_to node, b32 fd)
{
        for (b32 at = 0; at < node->redirect_count; at++)
        {
                parse_redirect address_to want = parse_redirects + node->redirect + at;

                if (want->fd == fd ||
                    ((want->op == OP_ANDGREAT || want->op == OP_ANDDGREAT) && fd == 2))
                        return true;
        }

        return false;
}

/* A shell-owned descriptor must not make a later user duplication source
   spring into existence. Literal numeric sources are visible before the
   redirect walk; expanded sources are handled when their value is known. */
static PURE bool exec_redirect_source_is(parse_node address_to node, b32 fd)
{
        for (b32 at = 0; at < node->redirect_count; at++)
        {
                parse_redirect address_to want =
                    parse_redirects + node->redirect + at;
                positive source;

                if ((want->op == OP_GREATAND || want->op == OP_LESSAND) &&
                    string_digits_checked_exact(want->text, 10,
                                                address_of source) &&
                    source == (positive)fd)
                        return true;
        }

        return false;
}

static PURE bool exec_saved_fd_is(b32 fd)
{
        for (b32 at = 0; at < exec_save_count; at++)
                if (exec_saves[at].saved == fd)
                        return true;

        return false;
}

static bipolar exec_save_duplicate(b32 fd, parse_node address_to node, b32 floor)
{
        for (;;)
        {
                bipolar saved = system_call_3(syscall(fcntl), fd,
                                               F_DUPFD_CLOEXEC, floor);

                if (saved < 0)
                        return saved;

                if (!node || !exec_redirect_target_is(node, (b32)saved))
                        return saved;

                system_close(saved);

                if (saved >= 0x7ffffffe)
                        return -1;

                floor = (b32)saved + 1;
        }
}

/* Relocate a shell-owned descriptor beyond every target in this redirect and
   every target an enclosing redirect will restore later. */
static COLD bipolar exec_internal_duplicate(b32 internal,
                                            parse_node address_to node,
                                            b32 floor, b32 avoid)
{
        for (;;)
        {
                bipolar saved = exec_save_duplicate(internal, node, floor);
                if (saved < 0)
                        return saved;

                // A closed descriptor in an enclosing redirect is free now,
                // but restoration will close or replace it later. The script
                // must outlive that scope, unlike an ordinary redirect save.
                b32 at = 0;
                while (at < exec_save_count && exec_saves[at].fd != saved)
                        at++;
                if (at == exec_save_count && saved != avoid &&
                    (!node || !exec_redirect_source_is(node, (b32)saved)))
                        return saved;

                system_close(saved);
                if (saved >= 0x7ffffffe)
                        return -1;
                floor = (b32)saved + 1;
        }
}

// The script reader now lives at moved, if it could be moved at all.
static COLD bool exec_script_moved(bipolar moved)
{
        if (moved < 0)
                return false;

        shell_parser_source_relocated(exec_script_fd, moved);
        system_close(exec_script_fd);
        exec_script_fd = (b32)moved;
        return true;
}

static COLD bool exec_script_preserve(parse_node address_to node)
{
        ul_limit_pair limits;
        b32 floor = 255;
        bipolar moved;

        if (ul_prlimit(0, 7, null, address_of limits) >= 0 && limits.soft <= 255)
                floor = limits.soft > 3 ? (b32)limits.soft - 1 : 3;

        moved = exec_internal_duplicate(exec_script_fd, node, floor, -1);
        if (moved < 0 && floor > 10)
                moved = exec_internal_duplicate(exec_script_fd, node, 10, -1);
        if (moved < 0 && floor > 3)
                moved = exec_internal_duplicate(exec_script_fd, node, 3, -1);
        return exec_script_moved(moved);
}

/* Keep the Spark cache outside a user redirection.  If descriptor pressure
   prevents relocation, relinquish the internal handle and let the cache open
   it again later; a valid redirect must not fail because an optimization fd
   could not move. */
static COLD fn exec_spawn_device_preserve(parse_node address_to node)
{
        bipolar moved;

        if (!shell_spawn_device_valid())
        {
                spawn_device = -1;
                spawn_device_opened = false;
                return;
        }

        moved = exec_internal_duplicate(spawn_device, node, 10, -1);
        if (moved < 0)
                moved = exec_internal_duplicate(spawn_device, node, 3, -1);

        system_close(spawn_device);
        spawn_device = moved < 0 ? -1 : (b32)moved;
        spawn_device_opened = moved >= 0;
}

/* Relocation uses a descriptor that was closed from the script's point of
   view. If a later expanded `>&N` names it, remove the cache or move the
   indispensable script reader once more, leaving N closed as it began. */
static COLD bool exec_internal_source_release(
    b32 source, parse_node address_to node)
{
        if (source == spawn_device)
        {
                if (shell_spawn_device_valid())
                        system_close(spawn_device);
                spawn_device = -1;
                spawn_device_opened = false;
        }

        return source != exec_script_fd ||
               exec_script_moved(exec_internal_duplicate(exec_script_fd, node,
                                                         3, source));
}

static bool exec_save_fd(b32 fd, parse_node address_to node)
{
        bipolar saved;
        bool closed = false;

        if (fd == spawn_device)
                exec_spawn_device_preserve(node);

        // Reuse the save allocator's complete target exclusion, so a command
        // claiming several descriptors cannot overwrite the relocated one
        // later in the same redirect list. Keep the original live on failure.
        if (fd == exec_script_fd && !exec_script_preserve(node))
                return string_report(log_error, false, "Cannot preserve script input\n");

        /* A temporary redirect can hide the parser descriptor before an
           expansion helper or nested command forks. Freeze while the owning
           handle is still installed and publish the save for this scope. */
        if (shell_parser_source_active &&
            shell_parser_source_handle == fd)
                shell_parser_source_fork_prepare();

        /* The command a forked child was made for is the last thing it
           runs, so what its own redirections replace is never put back:
           { sleep 1; } >/dev/null & inside $( ) held the substitution's pipe
           for the second the group ran, and bash has it closed at once. */
        if (exec_child_root && node == parse_nodes + exec_child_root)
                return true;

        if (exec_save_count >= REDIRECT_SAVE_MAX)
                return string_report(log_error, false, "Too many redirections\n");

        /* A descriptor holding an outer save is about to be replaced --
           exec 10>&1 inside { ...; } > f -- so the save moves first, or the
           group's end would put f back on stdout. */
        for (b32 at = 0; at < exec_save_count; at++)
        {
                if (exec_saves[at].saved != fd)
                        continue;

                bipolar moved = exec_save_duplicate(fd, node, 10);

                if (moved < 0)
                        return string_report(log_error, false,
                                             "Cannot preserve descriptor %p\n",
                                             (positive)fd);
                exec_saves[at].saved = (b32)moved;
        }

        saved = exec_save_duplicate(fd, node, 10);

        if (saved == -ERROR_BAD_DESCRIPTOR)
                closed = true;
        else if (saved < 0)
        {
                saved = exec_save_duplicate(fd, node, 3);

                if (saved == -ERROR_BAD_DESCRIPTOR)
                        closed = true;
        }

        // EBADF says there was nothing to restore. EINVAL/EMFILE say the
        // original is live but cannot be saved, and must never be treated as
        // a closed descriptor -- doing that closes it during restoration.
        if (!closed && saved < 0)
                return string_report(log_error, false, "Cannot preserve descriptor %p\n",
                              (positive)fd);

        if (!closed && shell_parser_source_active &&
            shell_parser_source_handle == fd)
                shell_parser_source_relocated(fd, saved);

        exec_saves[exec_save_count].fd = fd;
        exec_saves[exec_save_count].saved = closed ? -1 : (b32)saved;
        exec_save_count++;

        return true;
}

static fn exec_redirect_restore(b32 mark)
{
        log_flush();

        while (exec_save_count > mark)
        {
                exec_saved_fd address_to saved = exec_saves + --exec_save_count;

                if (saved->saved >= 0)
                {
                        if (shell_parser_source_active &&
                            shell_parser_source_handle == saved->saved)
                                shell_parser_source_relocated(saved->saved,
                                                              saved->fd);
                        system_duplicate(saved->saved, saved->fd, 0);
                        system_close(saved->saved);
                        continue;
                }

                system_close(saved->fd);
        }
}

// exec with nothing to run keeps its redirections, so what was put aside to
// undo them is dropped instead: holding the dup would keep the old file open
// for the rest of the line and never put it back.
static fn exec_redirect_forget(b32 mark)
{
        while (exec_save_count > mark)
        {
                exec_saved_fd address_to saved = exec_saves + --exec_save_count;

                if (saved->saved >= 0)
                {
                        if (shell_parser_source_active &&
                            shell_parser_source_handle == saved->saved)
                                shell_parser_source_relocated(saved->saved,
                                                              saved->fd);
                        system_close(saved->saved);
                }
        }
}

/* A bare exec makes its own saves permanent, but an enclosing function,
   eval or compound-command redirect may still restore the parser descriptor
   when that scope returns.  Refreshing while such a save exists would
   authenticate the temporary descriptor and forget the pipe the reader will
   resume, giving a nested confined applet a writable parser alias. */
static bool exec_parser_source_can_refresh()
{
        /* A substitution, pipeline stage or explicit subshell has a private
           descriptor table. Its bare exec cannot change the parent process
           which will resume the published reader, so its inherited snapshot
           must stay fixed until that final child exits. */
        if (!shell_parser_source_active ||
            shell_parser_source_process !=
                system_call_1(syscall(getpid), 0))
                return false;

        for (b32 at = 0; at < exec_save_count; at++)
                if (exec_saves[at].fd == shell_parser_source_handle)
                        return false;

        return true;
}

static bool exec_parser_source_saved(b32 mark)
{
        for (b32 at = mark; at < exec_save_count; at++)
                if (exec_saves[at].saved >= 0 &&
                    shell_parser_source_active &&
                    shell_parser_source_handle == exec_saves[at].saved)
                        return true;

        return false;
}

// A failed redirect may have closed fd 2 already. Put this redirect's saved
// value back long enough for the diagnostic; normal reverse restoration still
// owns and closes the save afterward. A save belonging to an earlier redirect
// or an enclosing group is deliberately below mark: restoring either would
// bypass `2>/file` or let a diagnostic leak through an outer `2>/dev/null`.
static fn exec_redirect_diagnostic_restore(b32 mark)
{
        for (b32 at = exec_save_count; at > mark; at--)
        {
                exec_saved_fd address_to saved = exec_saves + at - 1;

                if (saved->fd != 2)
                        continue;

                if (saved->saved >= 0)
                        system_duplicate(saved->saved, 2, 0);

                return;
        }
}

/*
        A here-document body with its parameters filled in.

        Not shell_expand_word: that also takes quotes off, and a quote in a
        here-document body is a quote. Only the expansions happen, and only
        when the delimiter was unquoted.
*/
static positive exec_here_expand(string_address body, positive length,
                                 string_address address_to out)
{
        positive start = token_used;

        if (!shell_expand_document(token_push_bytes, body, length, false))
                token_overflow |= expand_overflow;

        address_to out = token_storage + start;

        return token_used - start;
}

/*
        Bytes a here-document or here-string helper wrote, and the status it
        left. A non-zero status is the command's, not the script's: ${x?} in
        the body ends the helper with the nounset status and the script
        continues, the same way host bash and dash treat that expansion.
*/
static bool exec_helper_collect(bipolar child, b32 reading, positive start,
                                string_address address_to out,
                                positive address_to out_length)
{
        positive filled = 0;
        positive raw_status = 0;

        //      A here-document is as long as it is. Take another page of
        //      room whenever the last one filled, rather than deciding in
        //      advance how much of it is allowed to arrive.
        for (;;)
        {
                bipolar got;

                if (!token_room(start + filled + 4096))
                {
                        token_overflow = true;
                        break;
                }

                got = system_read_retry(reading, token_storage + start + filled,
                                        token_storage_room - start - filled - 1);

                if (got <= 0)
                        break;

                filled += (positive)got;
        }

        system_close(reading);

        system_wait4_retry(child, address_of raw_status, 0, null);

        exec_redirect_status = wait_status_code(raw_status);

        if (exec_redirect_status)
                return false;

        token_used = start + filled;
        address_to out = token_storage + start;
        address_to out_length = filled;

        return true;
}

/*
        Expand a here-document or a here-string outside the shell process.

        Parameter assignment in the body belongs to the helper, not to the
        parent: bash leaves ${x:=made} unset after the redirect, and that is
        the personality this isolation matches. Host bash treats ${x?} as a
        command status, not a process exit, so an expansion error ends the
        helper rather than the script, and because the helper is not a
        subshell the status is still 127 at the top of bash -c. A here-string
        is one word, written with the newline that ends it.
*/
static bool exec_helper_expand(string_address body, positive length,
                               bool here_string, string_address address_to out,
                               positive address_to out_length)
{
        positive start = token_used;
        b32 ends[2];
        bipolar child;

        if (system_pipe(ends, SHELL_PIPE_CLOSE_ON_EXEC) < 0)
                return false;

        log_flush();
        child = shell_clone();

        if (child == 0)
        {
                string_address expanded;
                positive made;

                system_close(ends[0]);
                //      Forked, so an interactive expansion error leaves this
                //      process and not the parent's input line, but no
                //      subshell: host bash keeps $BASH_SUBSHELL at 0 here.
                exec_child_began();
                shell_subshell_depth--;
                trap_default_all();
                token_overflow = false;

                if (here_string)
                {
                        expanded = shell_expand_word(body);
                        made = string_length(expanded);
                }
                else
                        made = exec_here_expand(body, length, address_of expanded);

                if (token_overflow && !here_string)
                {
                        log_error(str("Here-document too long\n"));
                        log_flush();
                        system_call_1(syscall(exit_group), 2);
                }

                if (system_write_all(ends[1], expanded, made) != made ||
                    (here_string && system_write_all(ends[1], "\n", 1) != 1))
                        system_call_1(syscall(exit_group), 1);

                system_call_1(syscall(exit_group), 0);
        }

        system_close(ends[1]);

        if (child < 0)
        {
                system_close(ends[0]);
                return false;
        }

        return exec_helper_collect(child, ends[0], start, out, out_length);
}

/* Large here-document bodies use a sealed file so staging cannot block before
   the command starts reading. Small bodies retain ordinary pipe semantics. */
static bipolar exec_here_file(string_address body, positive length)
{
        bipolar handle = system_call_2(
            syscall(memfd_create),
            (positive)(string_address)"shell-here",
            SHELL_PARSER_MFD_CLOEXEC | SHELL_PARSER_MFD_ALLOW_SEALING);

        if (handle < 0 ||
            system_write_all((positive)handle, body, length) != length ||
            system_seek(handle, 0, FILE_SEEK_SET) != 0 ||
            system_call_3(syscall(fcntl), (positive)handle,
                          SHELL_PARSER_F_ADD_SEALS,
                          SHELL_PARSER_SNAPSHOT_SEALS) < 0 ||
            !shell_parser_snapshot_sealed(handle))
        {
                if (handle >= 0)
                        system_close(handle);
                return -1;
        }

        return handle;
}

#define EXEC_F_GETPIPE_SZ 1032
/* Materialize every here-document without a writer process. A function or
   compound command may execute `exec` beneath a redirect, so recognizing
   only a literal top-level exec cannot prove that a helper will be reaped.
   A body which fits the kernel's actual empty-pipe capacity keeps ordinary
   pipe semantics; a larger body uses a sealed memfd and cannot block before
   the reader exists. */
static bipolar exec_here_open(string_address body, positive length)
{
        b32 ends[2];

        if (system_pipe(ends, SHELL_PIPE_CLOSE_ON_EXEC) >= 0)
        {
                bipolar capacity = system_call_3(
                    syscall(fcntl), (positive)ends[1], EXEC_F_GETPIPE_SZ, 0);

                if (capacity >= 0 && (positive)capacity >= length &&
                    system_write_all((positive)ends[1], body, length) ==
                        length)
                {
                        system_close(ends[1]);
                        return ends[0];
                }
                system_close(ends[0]);
                system_close(ends[1]);
        }

        return exec_here_file(body, length);
}

// Open an output redirect without replacing an existing regular file when
// noclobber is active. O_EXCL alone is too broad: POSIX still permits devices,
// pipes and sockets, and `set -C; echo x >/dev/null` is ordinary practice.
// The fallback opens without O_TRUNC and inspects that descriptor, so a path
// changing between a separate stat and open cannot make us truncate the file
// we had just decided to protect.
static bipolar exec_output_open(string_address target, bool force)
{
        bipolar opened;

        if (force || !shell_noclobber())
                return system_open_at_mode(AT_FDCWD,
                                     target, FILE_WRITE, 0666);

        opened = system_open_at_mode(AT_FDCWD, target,
                               FILE_WRITE | FILE_EXCLUSIVE, 0666);

        if (opened != -ERROR_EXISTS)
                return opened;

        opened = system_open_at(AT_FDCWD, target, 01);

        if (opened >= 0)
        {
                file_facts facts;

                if (!file_look(opened, "", AT_EMPTY_PATH, address_of facts) ||
                    (facts.mode & MODE_FORMAT) == MODE_FILE)
                {
                        system_close(opened);
                        opened = -ERROR_EXISTS;
                }
        }

        return opened;
}

/*
        A file a redirect could not have.

        bash names the file and what the kernel said and nothing else; dash
        says whether it was opening or creating, and has a word of its own
        for the two commonest reasons -- a file that is not there is "No
        such file" to a reader and a missing directory on the way to it is
        "Directory nonexistent" to a writer.
*/
static COLD b32 exec_redirect_refused(p8 op, string_address target,
                                      bipolar answer)
{
        bipolar code = answer < 0 ? -answer : answer;
        string_address why = system_error_message(code);
        bool reading = op == OP_LESS;

        if (!why)
                why = (string_address) "No such file or directory";

        shell_diagnostic_where();

        if (shell_bash_compat)
                return string_report(log_error, false, "%w: %s\n",
                                     writer_terminal_name, target, why);

        if (code == ERROR_NO_ENTRY)
                why = reading ? (string_address) "No such file"
                              : (string_address) "Directory nonexistent";

        return string_report(log_error, false,
                             reading ? "cannot open %w: %s\n"
                                     : "cannot create %w: %s\n",
                             writer_terminal_name, target, why);
}

/* `{name[index]}`: true, with the expanded index in key (null when it would
   not expand). A plain `{name}` is false. */
static bool exec_redirect_var_key(string_address name, positive length,
                                  positive address_to base,
                                  string_address address_to key,
                                  positive address_to key_length)
{
        const_string subscript;
        positive subscript_length;

        if (!env_reference_element_span(name, length, base, address_of subscript,
                                        address_of subscript_length) ||
            !subscript_length)
                return false;

        address_to key = shell_expand_subscript(name, address_to base,
            (string_address)subscript, subscript_length, key_length);
        return true;
}

/* `{name}` / `{name[index]}` names the descriptor stored in that variable. */
static bipolar exec_redirect_var_fd(string_address name, positive length)
{
        string_address value;
        string_address key;
        positive fd;
        positive base;
        positive key_length;

        if (exec_redirect_var_key(name, length, address_of base, address_of key,
                                  address_of key_length))
                value = key ? shell_array_get(name, base, key, key_length, null)
                            : null;
        else
                value = env_get_hashed_span(name, length,
                                            env_name_hash(name, length),
                                            null);

        if (!value || !string_digits_exact(value, address_of fd) ||
            fd > 0x7fffffff)
                return -1;

        return (bipolar)fd;
}

static bipolar exec_redirect_dup_min(bipolar from, b32 floor)
{
        if (from < 0)
                return from;

        return system_call_3(syscall(fcntl), (positive)from, F_DUPFD, floor);
}

/* `{name}>file` allocates a descriptor at or above 10 into that variable.
   `{name[index]}` stores the same way the coproc close looks one up. */
static bool exec_redirect_var_store(string_address name, positive length,
                                    b32 fd)
{
        p8 digits[24];
        positive base;
        string_address key;
        positive key_length;

        digits[positive_into_string(digits, (positive)fd)] = end;

        if (exec_redirect_var_key(name, length, address_of base, address_of key,
                                  address_of key_length))
                return key && shell_array_set(name, base, key, key_length,
                                              digits, false);

        return env_assign_hashed_span(name, length,
                                      env_name_hash(name, length), digits);
}

static bool exec_redirect_apply(b32 index)
{
        parse_node address_to node = parse_nodes + index;
        b32 at;

        /* Bash makes a failed redirection an ordinary failure and continues
           a non-interactive list; dash treats the language error as status
           two. Keep the policy at the one shared redirect boundary: every
           open, dup, save and here-document failure below reaches it. */
        exec_redirect_status = shell_bash_compat ? 1 : 2;

        for (at = 0; at < node->redirect_count; at++)
        {
                parse_redirect address_to want = parse_redirects + node->redirect + at;

                /* A successful prior here-document records zero. Restore the
                   policy before beginning the next independent redirect. */
                exec_redirect_status = shell_bash_compat ? 1 : 2;
                // A here-document's word is its delimiter, which only ever
                // has its quotes taken off: nothing in it runs, in POSIX or
                // in dash, and the body was matched against it when the
                // line was read. Expanding it here ran the substitution in
                // "cat <<$(x)" once per command, for nothing.
                string_address target = want->text;
                bipolar opened = -1;
                b32 redirect_mark = exec_save_count;
                bool both = want->op == OP_ANDGREAT || want->op == OP_ANDDGREAT;

                if (want->op != OP_DLESS && want->op != OP_HERESTRING)
                {
                        shell_words fields;
                        b32 expanded;

                        shell_words_bind(address_of fields,
                                         address_of exec_fields,
                                         address_of exec_fields_room);
                        expanded = shell_expand_redirect(want->text,
                                                         address_of fields,
                                                         address_of target);
                        if (expanded < 0)
                                return false;
                        if (!expanded)
                        {
                                string_format(log_error,
                                              "%s: ambiguous redirect\n",
                                              want->text);
                                exec_redirect_status = 1;
                                return false;
                        }
                }

                if (exec_line_aborted())
                        return false;

                b32 fd = want->fd;
                bool var_alloc = false;

                /*
                        bash's >&word with a word that is no descriptor is
                        &>word, stdout and stderr to one file, and with any
                        descriptor but 1 in front it is ambiguous. N>&M- moves
                        M to N: a duplicate, then M closed. dash has neither.
                */
                bool file_dup = false;
                bool move_dup = false;

                if (shell_bash_compat && !want->var_length &&
                    (want->op == OP_GREATAND || want->op == OP_LESSAND) &&
                    string_get(target))
                {
                        positive digits = string_span(target, string_set_digits);

                        if (digits && string_is(target + digits, '-') &&
                            !string_get(target + digits + 1))
                                move_dup = true;
                        else if (want->op == OP_GREATAND &&
                                 string_get(target + digits) &&
                                 !word_is(target, "-"))
                        {
                                if (fd != 1)
                                {
                                        string_format(log_error,
                                                      "%s: ambiguous redirect\n",
                                                      want->text);
                                        exec_redirect_status = 1;
                                        return false;
                                }
                                file_dup = true;
                                both = true;
                        }
                }

                if (want->var_length)
                {
                        bool closing =
                            (want->op == OP_GREATAND ||
                             want->op == OP_LESSAND) &&
                            string_is(target, '-') &&
                            string_is(target + 1, end);

                        if (closing)
                        {
                                bipolar named = exec_redirect_var_fd(
                                    want->var, want->var_length);
                                p8 shown[64];
                                positive shown_length;

                                if (named < 0)
                                {
                                        shown_length = want->var_length < 63
                                                           ? want->var_length
                                                           : 63;
                                        memory_copy(shown, want->var,
                                                    shown_length);
                                        shown[shown_length] = end;
                                        string_format(log_error,
                                                      "%s: ambiguous redirect\n",
                                                      shown);
                                        exec_redirect_status = 1;
                                        return false;
                                }

                                fd = (b32)named;
                        }
                        else
                                var_alloc = true;
                }

                /* Dash dups only a single digit 0-9. `>&99` and `>&$n` with
                   n=10 are a syntax error and end the process, the way lima
                   0.5.x does. A closed 0-9 is an ordinary runtime failure. */
                if (!shell_bash_compat &&
                    (want->op == OP_GREATAND || want->op == OP_LESSAND) &&
                    !(string_is(target, '-') && string_is(target + 1, end)))
                {
                        p8 first = string_get(target);

                        if (!byte_is_digit(first) || string_get(target + 1))
                        {
                                /* lima dash 0.5.x has already consumed this
                                   line's newline, so the diagnostic names
                                   the following line when one exists. */
                                b32 saved_line = exec_line;

                                if (shell_line_has_more && exec_line)
                                        exec_line++;
                                shell_syntax_where();
                                log_error(str("Syntax error: Bad fd number\n"));
                                exec_line = saved_line;
                                exec_redirect_status = 2;
                                expand_fatal_status(2);
                                return false;
                        }
                }

                /*
                        rbash: the redirections that can make a file or cut
                        one down. A duplication says nothing about the file
                        system -- 2>&1 and >&2 move a descriptor that is
                        already open -- and reading makes nothing, so both
                        stand here as they stand in bash.
                */
                if (shell_restricted &&
                    (want->op == OP_GREAT || want->op == OP_DGREAT ||
                     want->op == OP_CLOBBER || want->op == OP_LESSGREAT ||
                     want->op == OP_ANDGREAT || want->op == OP_ANDDGREAT ||
                     file_dup))
                {
                        exec_redirect_status = 1;
                        return shell_reported(false,
                            "%s: restricted: cannot redirect "
                            "output\n", target);
                }

                /*
                        The descriptor is put aside before anything is opened.

                        open hands back the lowest free descriptor, which is
                        the one being redirected whenever that one was closed.
                        Saving after the open therefore recorded the file that
                        had just arrived as "what was there before", and put it
                        back instead of closing it.

                        Put aside, not closed. `cat </dev/stdin` names
                        /proc/self/fd/0, so closing descriptor zero first
                        deleted the file the redirect was about to open --
                        every /dev/stdin, /dev/stdout and /dev/fd/N redirect
                        failed. The open lands wherever it lands and the dup3
                        below moves it, which is the order bash uses and the
                        only one under which those names exist.

                        `{name}>file` allocates a new descriptor rather than
                        replacing one, and `{name}>&-` closes the one stored
                        in the variable: both persist, so they are not saved.
                */
                if (!want->var_length)
                {
                        if (both)
                        {
                                if (!exec_save_fd(1, node) ||
                                    !exec_save_fd(2, node))
                                        return false;
                        }
                        else if (!exec_save_fd(fd, node))
                                return false;
                }

                if (want->op == OP_DLESS)
                {
                        string_address body = want->kept
                                                  ? parse_kept_text + want->body
                                                  : here_text + want->body;
                        positive length = want->body_length;

                        if (!want->raw)
                        {
                                if (shell_bash_compat)
                                {
                                        if (!exec_helper_expand(
                                                body, length, false,
                                                address_of body,
                                                address_of length))
                                                return false;
                                }
                                else
                                {
                                        // Dash expands the body here so
                                        // ${x:=word} sticks. ${x?} must not
                                        // exit_group: it is that command's
                                        // status, and the script continues.
                                        bool kept = expand_redirect_error;

                                        expand_redirect_error = true;
                                        length = exec_here_expand(
                                            body, length, address_of body);
                                        expand_redirect_error = kept;

                                        if (expand_failed)
                                        {
                                                exec_redirect_status =
                                                    shell_status ? shell_status
                                                                 : 2;
                                                expand_failed = false;
                                                return false;
                                        }

                                        if (token_overflow)
                                        {
                                                log_error(str(
                                                    "Here-document too long\n"));
                                                exec_redirect_status = 2;
                                                return false;
                                        }
                                }
                        }

                        opened = exec_here_open(body, length);
                }
                else if (want->op == OP_HERESTRING)
                {
                        string_address body;
                        positive length;

                        if (!exec_helper_expand(want->text, 0, true,
                                                address_of body,
                                                address_of length))
                                return false;

                        opened = exec_here_open(body, length);
                }
                else if ((want->op == OP_GREATAND || want->op == OP_LESSAND) &&
                         !file_dup)
                {
                        positive source;
                        p8 number[32];

                        if (string_is(target, '-') && string_is(target + 1, end))
                        {
                                system_close(fd);
                                continue;
                        }

                        if (move_dup)
                        {
                                positive digits = string_span(target,
                                                              string_set_digits);

                                if (digits >= sizeof(number))
                                        digits = sizeof(number) - 1;
                                memory_copy_end(number, target, digits);
                                target = number;
                        }

                        if (!string_digits_checked_exact(target, 10, address_of source) ||
                            source >= 0x7fffffff ||
                            !exec_internal_source_release((b32)source, node) ||
                            exec_saved_fd_is((b32)source))
                        {
                                exec_redirect_diagnostic_restore(redirect_mark);
                                return string_report(log_error, false, "Cannot redirect descriptor: %s\n",
                                              target);
                        }

                        if (var_alloc)
                        {
                                opened = exec_redirect_dup_min((bipolar)source,
                                                               REDIR_VAR_FLOOR);
                        }
                        else if ((b32)source == fd)
                                continue;
                        else
                                opened = system_duplicate(source, fd, 0);

                        //      The move's second half. bash closes the
                        //      source for good, even for one command's own
                        //      move: `cmd 4>&3-` leaves 3 closed after it.
                        if (move_dup && opened >= 0 && (b32)source != fd &&
                            (b32)source != (b32)opened)
                                system_close(source);
                }
                else if (want->op == OP_LESS)
                        opened = system_open_at(AT_FDCWD,
                                               target, FILE_READ);
                else if (want->op == OP_DGREAT || want->op == OP_ANDDGREAT)
                        opened = system_open_at_mode(AT_FDCWD,
                                               target, FILE_APPEND, 0666);
                else if (want->op == OP_LESSGREAT)
                        opened = system_open_at_mode(AT_FDCWD,
                                               target,
                                               FILE_READ_WRITE | FILE_CREATE, 0666);
                else
                        opened = exec_output_open(target,
                                                  want->op == OP_CLOBBER);

                if (opened < 0)
                {
                        exec_redirect_diagnostic_restore(redirect_mark);

                        return exec_redirect_refused(want->op, target,
                                                     opened);
                }

                if (var_alloc)
                {
                        bipolar moved = opened;

                        if (want->op != OP_GREATAND && want->op != OP_LESSAND)
                        {
                                moved = exec_redirect_dup_min(opened,
                                                              REDIR_VAR_FLOOR);
                                if (moved < 0)
                                {
                                        system_close(opened);
                                        exec_redirect_diagnostic_restore(
                                            redirect_mark);
                                        return exec_redirect_refused(want->op,
                                                                     target,
                                                                     moved);
                                }

                                if (moved != opened)
                                        system_close(opened);
                        }

                        if (!exec_redirect_var_store(want->var, want->var_length,
                                                     (b32)moved))
                        {
                                system_close(moved);
                                return false;
                        }

                        log_flush();
                        continue;
                }

                log_flush();

                /*
                        &>file is >file followed by 2>&1, as one indivisible
                        redirect. Duplicating one open file description also
                        keeps stdout and stderr on one shared file position.
                        &>> differs only in the open flags above.
                */
                if (both)
                {
                        if (opened != 1)
                        {
                                if (system_duplicate(opened, 1, 0) < 0)
                                {
                                        system_close(opened);
                                        return false;
                                }

                                system_close(opened);
                        }

                        if (system_duplicate(1, 2, 0) < 0)
                                return false;

                        continue;
                }

                // dup3 onto the descriptor it was handed is an error rather
                // than the no-op dup2 makes of it, and open answers with
                // exactly that descriptor when it was the lowest one free.
                if (opened != fd ? system_duplicate(opened, fd, 0) < 0
                                 : system_descriptor_install(opened, fd) < 0)
                {
                        system_close(opened);
                        exec_redirect_diagnostic_restore(redirect_mark);
                        return string_report(log_error, false,
                                             "Cannot redirect descriptor: %p\n",
                                             (positive)fd);
                }
                if (opened != fd)
                        system_close(opened);
        }

        return true;
}

typedef struct
{
        p8 address_to name;
        positive name_room;
        positive name_hash;
        positive name_length;
        b32 body;
        // Active calls keep this slot and its name stable. Each call holds
        // its own body independently when a definition replaces this one.
        positive active;
        bool readonly;
        bool exported;
        // The execve spelling is built lazily and retained while the
        // function is exported. Redefinition reuses this allocation.
        p8 address_to environment;
        positive environment_room;
        bool environment_valid;
        /* Zero is an ordinary function name, one a POSIX special name, and
           two Bash's `source` spelling. The name never changes while this
           slot is live, so command dispatch must not scan the same static
           name table on every call. */
        p8 special_kind;
} exec_function;

KEEP __attribute__((externally_visible)) exec_function address_to exec_functions;
static string_address exec_frames_code_source();

/* The file each slot's definition was read from, which BASH_SOURCE names for
   the function's frame. Beside the table rather than in it: the slot walk
   reads the table at a fixed stride. */
static string_address address_to exec_function_sources;
static positive exec_function_sources_room;

static fn exec_function_source_set(positive slot, string_address source)
{
        positive had = exec_function_sources_room;

        if (!shell_array_room(exec_function_sources, exec_function_sources_room,
                              slot + 1))
                return;
        for (positive at = had; at < exec_function_sources_room; at++)
                exec_function_sources[at] = null;
        exec_function_sources[slot] = source;
}

static string_address exec_function_source(positive slot)
{
        return slot < exec_function_sources_room && exec_function_sources[slot]
                   ? exec_function_sources[slot]
                   : shell_script_name;
}

/*
        The next live function at or beyond one slot.

        Unset slots remain reusable and may sit between live ones. Advancing
        here keeps every table walker from stopping at the first hole, and
        returning the separately allocated name removes compgen's old
        255-byte function-name ceiling.
*/
static positive exec_function_room;
KEEP __attribute__((externally_visible)) positive exec_function_count;
static positive exec_function_env_generation = 1;

static fn exec_function_environment_changed()
{
        exec_function_env_generation++;
        if (!exec_function_env_generation)
                exec_function_env_generation = 1;
}

string_address exec_function_next(positive address_to slot,
                                  bool address_to readonly)
{
        positive at = address_to slot;

        while (at < exec_function_count && !exec_functions[at].body)
                at++;

        if (at >= exec_function_count)
        {
                address_to slot = exec_function_count;
                return null;
        }

        address_to slot = at + 1;
        if (readonly)
                address_to readonly = exec_functions[at].readonly;
        return exec_functions[at].name;
}
KEEP __attribute__((externally_visible)) positive exec_function_recent = positive_max;

// Whether a name is a function, which direct command substitution asks.
bool exec_function_here_hashed(string_address name, positive2 named);

static PURE inline INLINE bool exec_function_matches(
    positive index, string_address name, positive hash, positive length)
{
        return exec_functions[index].name_hash == hash &&
               exec_functions[index].name_length == length &&
               !memory_compare(exec_functions[index].name, name, length);
}

/*
        The slot of a defined function, or positive_max when the name is not
        one.

        Every command asks this before it looks for a builtin or a utility,
        and nearly every command is neither a function nor the one found
        last, so the answer is usually a walk of every slot: a hundred and
        more in a script built on libtool's ltmain.sh. The walk asks the hash
        first, which alone turns away nearly every slot, and only then the
        length, whether the slot is live and whether it is the one already
        asked; the name is compared in the body, eight bytes a turn and the
        last eight again, and byte by byte under eight, where a word read
        would straddle a name just stored. The slot found is the one asked
        first next time.
*/
positive exec_function_slot(string_address name, positive2 named);

_Static_assert(sizeof(exec_function) == 80 && __builtin_offsetof(exec_function, name) == 0 &&
               __builtin_offsetof(exec_function, name_hash) == 16 &&
               __builtin_offsetof(exec_function, name_length) == 24 &&
               __builtin_offsetof(exec_function, body) == 32,
               "exec_function_slot reads a slot at these offsets");

#if X64
// The name at %rdi, length %rdx, against the name at held: on to fail
// unless they agree. Scratch: off and tmp, whose low byte is tmpb.
#define EXEC_NAME_SAME_X64(held, off, tmp, tmpb, fail)                       \
    "cmp $8, %rdx\n   jb 61f\n   xor %" off ", %" off "\n"                   \
    "60: mov (%" held ",%" off "), %" tmp "\n   cmp (%rdi,%" off "), %" tmp "\n"  \
    "jne " fail "\n   add $8, %" off "\n   lea 8(%" off "), %" tmp "\n"       \
    "cmp %rdx, %" tmp "\n   jbe 60b\n"                                      \
    "mov -8(%" held ",%rdx), %" tmp "\n   cmp -8(%rdi,%rdx), %" tmp "\n"      \
    "jne " fail "\n   jmp 63f\n"                                            \
    "61: test %rdx, %rdx\n   jz 63f\n   xor %" off ", %" off "\n"           \
    "64: movzbl (%" held ",%" off "), %" tmp "d\n   cmpb (%rdi,%" off "), %" tmpb "\n" \
    "jne " fail "\n   inc %" off "\n   cmp %rdx, %" off "\n   jb 64b\n"        \
    "63:\n"

__asm__(
    ASM_FUNC(exec_function_slot)
    "mov exec_function_count(%rip), %r8\n   mov exec_functions(%rip), %r9\n"
    // The one found last.
    "mov exec_function_recent(%rip), %rax\n   cmp %r8, %rax\n   jae 10f\n"
    "lea (%rax,%rax,4), %rcx\n   shl $4, %rcx\n   lea (%r9,%rcx), %r10\n"
    "cmpl $0, 32(%r10)\n   je 10f\n   cmp 16(%r10), %rsi\n   jne 10f\n"
    "cmp 24(%r10), %rdx\n   jne 10f\n   mov (%r10), %r10\n"
    EXEC_NAME_SAME_X64("r10", "rcx", "r11", "r11b", "10f")
    ASM_RET
    // Every slot in turn, the hash asked first: one branch back a slot,
    // and everything else out of the way below.
    "10: lea (%r8,%r8,4), %r11\n   shl $4, %r11\n   add %r9, %r11\n   mov %r9, %r10\n"
    "cmp %r11, %r10\n   jae 19f\n"
    ".p2align 5\n"
    "11: cmp 16(%r10), %rsi\n   je 14f\n"
    "13: add $80, %r10\n   cmp %r11, %r10\n   jb 11b\n"
    "19: mov $-1, %rax\n"
    ASM_RET
    // The hash agrees: the length, a live body, not the one already asked.
    "14: cmp 24(%r10), %rdx\n   jne 13b\n   cmpl $0, 32(%r10)\n   je 13b\n"
    "mov %r10, %rcx\n   sub %r9, %rcx\n   shr $4, %rcx\n   movabs $0xcccccccccccccccd, %r8\n"
    "imul %r8, %rcx\n   cmp %rax, %rcx\n   je 13b\n"
    "push %rcx\n   push %r10\n   mov (%r10), %r10\n"
    EXEC_NAME_SAME_X64("r10", "rcx", "r8", "r8b", "15f")
    "pop %r10\n   pop %rax\n   mov %rax, exec_function_recent(%rip)\n"
    ASM_RET
    "15: pop %r10\n   pop %rcx\n   jmp 13b\n"
    ASM_END(exec_function_slot)
);
#elif ARM64
// The name at x0, length x2, against the name at held: on to fail unless
// they agree. Scratch: x3 to x6.
#define EXEC_NAME_SAME_ARM64(held, fail)                                    \
    "cmp x2, #8\n   b.lo 61f\n   mov x5, #0\n"                               \
    "60: ldr x3, [" held ", x5]\n   ldr x4, [x0, x5]\n   cmp x3, x4\n   b.ne " fail "\n" \
    "add x5, x5, #8\n   add x6, x5, #8\n   cmp x6, x2\n   b.ls 60b\n"          \
    "sub x5, x2, #8\n   ldr x3, [" held ", x5]\n   ldr x4, [x0, x5]\n"        \
    "cmp x3, x4\n   b.ne " fail "\n   b 63f\n"                               \
    "61: cbz x2, 63f\n   mov x5, #0\n"                                      \
    "64: ldrb w3, [" held ", x5]\n   ldrb w4, [x0, x5]\n   cmp w3, w4\n   b.ne " fail "\n" \
    "add x5, x5, #1\n   cmp x5, x2\n   b.lo 64b\n"                          \
    "63:\n"

__asm__(
    ASM_FUNC(exec_function_slot)
    "adrp x8, exec_function_count\n   ldr x8, [x8, :lo12:exec_function_count]\n"
    "adrp x9, exec_functions\n   ldr x9, [x9, :lo12:exec_functions]\n"
    "adrp x10, exec_function_recent\n   add x10, x10, :lo12:exec_function_recent\n"
    // The one found last.
    "ldr x7, [x10]\n   cmp x7, x8\n   b.hs 10f\n   mov x11, #80\n   madd x12, x7, x11, x9\n"
    "ldr w3, [x12, #32]\n   cbz w3, 10f\n   ldp x3, x4, [x12, #16]\n"
    "cmp x3, x1\n   ccmp x4, x2, #0, eq\n   b.ne 10f\n   ldr x12, [x12]\n"
    EXEC_NAME_SAME_ARM64("x12", "10f")
    "mov x0, x7\n"
    ASM_RET
    // Every slot in turn, the hash asked first: one branch back a slot,
    // and everything else out of the way below.
    "10: mov x14, x9\n   mov x15, #80\n   madd x15, x8, x15, x9\n   cmp x14, x15\n   b.hs 19f\n"
    ".p2align 4\n"
    "11: ldr x3, [x14, #16]\n   cmp x3, x1\n   b.eq 14f\n"
    "13: add x14, x14, #80\n   cmp x14, x15\n   b.lo 11b\n"
    "19: mov x0, #-1\n"
    ASM_RET
    // The hash agrees: the length, a live body, not the one already asked.
    "14: ldr x4, [x14, #24]\n   cmp x4, x2\n   b.ne 13b\n   ldr w3, [x14, #32]\n   cbz w3, 13b\n"
    "sub x13, x14, x9\n   mov x16, #80\n   udiv x13, x13, x16\n   cmp x13, x7\n   b.eq 13b\n"
    "ldr x12, [x14]\n"
    EXEC_NAME_SAME_ARM64("x12", "13b")
    "str x13, [x10]\n   mov x0, x13\n"
    ASM_RET
    ASM_END(exec_function_slot)
);
#elif RISCV64
// The name at a0, length a2, against the name at held, byte by byte: on to
// fail unless they agree. Scratch: t3 to t5.
#define EXEC_NAME_SAME_RISCV(held, fail)                                    \
    "li t5, 0\n   beqz a2, 63f\n"                                           \
    "64: add t3, " held ", t5\n   lbu t3, 0(t3)\n   add t4, a0, t5\n   lbu t4, 0(t4)\n" \
    "bne t3, t4, " fail "\n   addi t5, t5, 1\n   bltu t5, a2, 64b\n"          \
    "63:\n"

__asm__(
    ASM_FUNC(exec_function_slot)
    "lla t0, exec_function_count\n   ld a3, 0(t0)\n   lla t0, exec_functions\n   ld a4, 0(t0)\n"
    "lla a5, exec_function_recent\n"
    // The one found last.
    "ld a6, 0(a5)\n   bgeu a6, a3, 10f\n   li t0, 80\n   mul t0, a6, t0\n   add t0, a4, t0\n"
    "lw t1, 32(t0)\n   beqz t1, 10f\n   ld t1, 16(t0)\n   bne t1, a1, 10f\n   ld t1, 24(t0)\n   bne t1, a2, 10f\n"
    "ld t2, 0(t0)\n"
    EXEC_NAME_SAME_RISCV("t2", "10f")
    "mv a0, a6\n"
    ASM_RET
    // Every slot in turn, the hash asked first.
    "10: li a7, 0\n   mv t0, a4\n"
    "11: bgeu a7, a3, 19f\n   ld t1, 16(t0)\n   beq t1, a1, 14f\n"
    "13: addi a7, a7, 1\n   addi t0, t0, 80\n   j 11b\n"
    "19: li a0, -1\n"
    ASM_RET
    // The hash agrees: the length, a live body, not the one already asked.
    "14: ld t1, 24(t0)\n   bne t1, a2, 13b\n   lw t1, 32(t0)\n   beqz t1, 13b\n   beq a7, a6, 13b\n"
    "ld t2, 0(t0)\n"
    EXEC_NAME_SAME_RISCV("t2", "13b")
    "sd a7, 0(a5)\n   mv a0, a7\n"
    ASM_RET
    ASM_END(exec_function_slot)
);
#endif

static b32 exec_function_find(string_address name, positive2 named)
{
        positive slot = exec_function_slot(name, named);

        return slot == positive_max ? 0 : exec_functions[slot].body;
}

bool exec_function_here_hashed(string_address name, positive2 named)
{
        return exec_function_find(name, named) != 0;
}

bool exec_function_readonly_set(string_address name)
{
        positive2 named = string_hash_33_length(name);
        positive slot = exec_function_slot(name, named);

        if (slot == positive_max)
                return false;

        exec_functions[slot].readonly = true;
        return true;
}

/*
        Giving a locked function back, so a reload can redefine it.

        The lock exists so that nothing a machine script starts can redefine
        the functions that script named -- a definition that reaches a locked
        slot is refused with "readonly function" and the source carries on
        past it. That refusal is the right answer to a later command and the
        wrong one to the machine process reloading its own edited script,
        which would otherwise source the new file and keep every old body.
        So the owner of the lock can hand it back; nothing else can, because
        nothing else is given this name.
*/
bool exec_function_readonly_clear(string_address name)
{
        positive2 named = string_hash_33_length(name);
        positive slot = exec_function_slot(name, named);

        if (slot == positive_max)
                return false;

        exec_functions[slot].readonly = false;
        return true;
}

bool exec_function_readonly_hashed(string_address name, positive2 named)
{
        positive slot = exec_function_slot(name, named);

        return slot != positive_max && exec_functions[slot].readonly;
}

b32 exec_function_attributes_hashed(string_address name, positive2 named)
{
        positive slot = exec_function_slot(name, named);

        if (slot == positive_max)
                return 0;
        return (exec_functions[slot].readonly ? DECLARE_READONLY : 0) |
               (exec_functions[slot].exported ? DECLARE_EXPORT : 0);
}

/* The here-documents a line of function text still owes, by where their
   redirection sits in the whole arena: a b32, since that is any slot. */
#define EXEC_FUNCTION_HERE_MAX 192

typedef struct
{
        b32 redirect;
        b32 ordinal;
} exec_function_here;

typedef struct
{
        p8 address_to address_to text;
        positive address_to room;
        positive used;
        bool failed;
        exec_function_here pending[EXEC_FUNCTION_HERE_MAX];
        p8 pending_used;
} exec_function_text;

static fn exec_function_text_add(exec_function_text address_to made,
                                 const_string text, positive length)
{
        byte_store store = {address_to made->text, address_to made->room,
                            made->used};

        if (made->failed || !length)
                return;

        made->failed = !shell_bytes_add(address_of store,
                                        (string_address)text, length);
        address_to made->text = store.bytes;
        address_to made->room = store.room;
        made->used = store.used;
}

#define exec_function_text_literal(made, literal)                           \
        exec_function_text_add((made), (const_string)(literal),             \
                               sizeof(literal) - 1)

static bool exec_function_text_node(exec_function_text address_to made,
                                    b32 index, positive depth);
static bool exec_function_text_line(exec_function_text address_to made);

static bool exec_function_text_body(exec_function_text address_to made,
                                    b32 body, positive depth)
{
        if (body && parse_nodes[body].kind == NODE_GROUP)
                return exec_function_text_node(made, body, depth + 1);

        exec_function_text_literal(made, "{ ");
        if (!exec_function_text_node(made, body, depth + 1))
                return false;
        if (!exec_function_text_line(made))
                return false;
        exec_function_text_literal(made, "}");
        return !made->failed;
}

static positive exec_function_here_delimiter(parse_redirect address_to redirect,
                                              positive ordinal,
                                              p8 address_to delimiter)
{
        static const p8 prefix[] = "MOONWATER_FUNCTION_EOF_";
        string_address body = (redirect->kept ? parse_kept_text : here_text) +
                              redirect->body;
        positive prefix_length = sizeof(prefix) - 1;

        for (;; ordinal++)
        {
                positive digits = positive_into(delimiter + prefix_length,
                                                ordinal);
                positive length = prefix_length + digits;
                positive at = 0;
                bool collision = false;

                memory_copy(delimiter, (address_any)prefix, prefix_length);
                while (at < redirect->body_length)
                {
                        positive start = at;

                        at += memory_span_without_byte(
                            body + at, '\n', redirect->body_length - at);
                        if (at - start == length &&
                            !memory_compare(body + start, delimiter, length))
                        {
                                collision = true;
                                break;
                        }
                        if (at < redirect->body_length)
                                at++;
                }

                if (!collision)
                        return length;
        }
}

/* A here-document belongs to the complete command line, not to the simple
   command whose redirect names it. Retain redirect references while the
   existing AST writer emits a pipeline or AND-OR header, then write every
   body in lexical order at the next real grammar line boundary. */
static bool exec_function_text_here_flush(exec_function_text address_to made)
{
        if (!made->pending_used)
                return !made->failed;

        exec_function_text_literal(made, "\n");
        for (b32 at = 0; at < made->pending_used; at++)
        {
                exec_function_here address_to pending = made->pending + at;
                parse_redirect address_to redirect =
                    parse_redirects + pending->redirect;
                string_address body =
                    (redirect->kept ? parse_kept_text : here_text) +
                    redirect->body;
                p8 delimiter[64];
                positive length = exec_function_here_delimiter(
                    redirect, (positive)pending->ordinal, delimiter);

                exec_function_text_add(made, body, redirect->body_length);
                if (redirect->body_length &&
                    body[redirect->body_length - 1] != '\n')
                        exec_function_text_literal(made, "\n");
                exec_function_text_add(made, delimiter, length);
                exec_function_text_literal(made, "\n");
        }

        made->pending_used = 0;
        return !made->failed;
}

static bool exec_function_text_line(exec_function_text address_to made)
{
        if (!exec_function_text_here_flush(made))
                return false;
        if (!made->used || (address_to made->text)[made->used - 1] != '\n')
                exec_function_text_literal(made, "\n");
        return !made->failed;
}

static bool exec_function_text_redirects(exec_function_text address_to made,
                                         parse_node address_to node)
{
        static const string_address spelling[] = {
            null, null, null, null, "<<", ">>", "<&", ">&", "<>",
            ">|", null, null, null, "<", ">", null, null, "&>",
            "&>>", "<<<"};

        for (b32 at = 0; at < node->redirect_count; at++)
        {
                parse_redirect address_to redirect =
                    parse_redirects + node->redirect + at;
                p8 number[32];
                positive used;

                if (redirect->op >= sizeof(spelling) / sizeof(spelling[0]) ||
                    !spelling[redirect->op])
                        return false;

                exec_function_text_literal(made, " ");

                if (redirect->op != OP_ANDGREAT &&
                    redirect->op != OP_ANDDGREAT)
                {
                        used = positive_into(number,
                                             (positive)redirect->fd);
                        exec_function_text_add(made, number, used);
                }

                exec_function_text_add(
                    made, spelling[redirect->op],
                    string_length(spelling[redirect->op]));

                if (redirect->op == OP_DLESS)
                {
                        exec_function_here address_to pending;
                        p8 delimiter[64];
                        positive length = exec_function_here_delimiter(
                            redirect, (positive)at, delimiter);

                        if (made->pending_used >= EXEC_FUNCTION_HERE_MAX)
                        {
                                made->failed = true;
                                return false;
                        }

                        if (redirect->raw)
                                exec_function_text_literal(made, "'");
                        exec_function_text_add(made, delimiter, length);
                        if (redirect->raw)
                                exec_function_text_literal(made, "'");
                        pending = made->pending + made->pending_used++;
                        pending->redirect = node->redirect + at;
                        pending->ordinal = at;
                }
                else
                        exec_function_text_add(made, redirect->text,
                                               redirect->text_length);
        }

        return !made->failed;
}

static bool exec_function_text_words(exec_function_text address_to made,
                                     parse_node address_to node,
                                     positive first)
{
        for (positive at = first; at < node->word_count; at++)
        {
                b32 word = node->word + (b32)at;

                if (at != first)
                        exec_function_text_literal(made, " ");
                exec_function_text_add(made, parse_words[word],
                                       parse_word_lengths[word]);
        }

        return !made->failed;
}

static bool exec_function_text_list(exec_function_text address_to made,
                                    b32 child, positive depth)
{
        while (child)
        {
                b32 next = parse_nodes[child].next;

                if (!exec_function_text_node(made, child, depth + 1))
                        return false;
                if (next && !exec_function_text_line(made))
                        return false;
                child = next;
        }

        return !made->failed;
}

static bool exec_function_text_loop_body(exec_function_text address_to made,
                                         parse_node address_to node,
                                         positive depth)
{
        if (!exec_function_text_line(made))
                return false;
        exec_function_text_literal(made, "do ");
        if (!exec_function_text_node(made, node->right, depth + 1))
                return false;
        if (!exec_function_text_line(made))
                return false;
        exec_function_text_literal(made, "done");
        return exec_function_text_redirects(made, node);
}

static bool exec_function_text_node(exec_function_text address_to made,
                                    b32 index, positive depth)
{
        parse_node address_to node;
        b32 child;

        if (!index)
                return true;
        if (depth > PARSE_NODES)
                return false;

        node = parse_nodes + index;

        if (node->kind == NODE_SIMPLE || node->kind == NODE_ARITHMETIC ||
            node->kind == NODE_CONDITIONAL)
        {
                exec_function_text_words(made, node, 0);
                return exec_function_text_redirects(made, node);
        }

        if (node->kind == NODE_PIPELINE || node->kind == NODE_ANDOR)
        {
                bool pipeline = node->kind == NODE_PIPELINE;

                if (pipeline && node->flags)
                        exec_function_text_literal(made, "! ");
                for (child = node->left; child; child = parse_nodes[child].next)
                {
                        if (child != node->left)
                                exec_function_text_add(
                                    made, pipeline ? (const_string)" | "
                                          : parse_nodes[child].op == OP_AND_IF
                                                ? (const_string)" && "
                                                : (const_string)" || ", pipeline ? 3 : 4);
                        if (!exec_function_text_node(made, child, depth + 1))
                                return false;
                }
                if (!pipeline && node->flags)
                        exec_function_text_literal(made, " &");
                return exec_function_text_redirects(made, node);
        }

        if (node->kind == NODE_LIST)
                return exec_function_text_list(made, node->left, depth);

        if (node->kind == NODE_SUBSHELL || node->kind == NODE_GROUP)
        {
                exec_function_text_add(
                    made, node->kind == NODE_GROUP ? (const_string)"{ "
                                                   : (const_string)"( ", 2);
                if (!exec_function_text_node(made, node->left, depth + 1))
                        return false;
                if (!exec_function_text_line(made))
                        return false;
                exec_function_text_add(
                    made, node->kind == NODE_GROUP ? (const_string)"}"
                                                   : (const_string)")", 1);
                return exec_function_text_redirects(made, node);
        }

        if (node->kind == NODE_IF)
        {
                exec_function_text_literal(made, "if ");
                if (!exec_function_text_node(made, node->left, depth + 1))
                        return false;
                if (!exec_function_text_line(made))
                        return false;
                exec_function_text_literal(made, "then ");
                if (!exec_function_text_node(made, node->right, depth + 1))
                        return false;
                if (node->extra)
                {
                        if (!exec_function_text_line(made))
                                return false;
                        exec_function_text_literal(made, "else ");
                        if (!exec_function_text_node(made, node->extra,
                                                     depth + 1))
                                return false;
                }
                if (!exec_function_text_line(made))
                        return false;
                exec_function_text_literal(made, "fi");
                return exec_function_text_redirects(made, node);
        }

        if (node->kind == NODE_WHILE || node->kind == NODE_UNTIL)
        {
                exec_function_text_add(
                    made, node->kind == NODE_WHILE ? (const_string)"while "
                                                   : (const_string)"until ",
                    6);
                if (!exec_function_text_node(made, node->left, depth + 1))
                        return false;
                return exec_function_text_loop_body(made, node, depth);
        }

        if (node->kind == NODE_FOR || node->kind == NODE_SELECT ||
            node->kind == NODE_CFOR)
        {
                exec_function_text_add(
                    made, node->kind == NODE_SELECT ? (const_string)"select "
                                                    : (const_string)"for ",
                    node->kind == NODE_SELECT ? 7 : 4);
                if (node->kind == NODE_CFOR)
                        exec_function_text_words(made, node, 0);
                else
                {
                        b32 word = node->word;

                        if (node->word_count)
                                exec_function_text_add(
                                    made, parse_words[word],
                                    parse_word_lengths[word]);
                        if (node->flags)
                        {
                                exec_function_text_literal(made, " in");
                                if (node->word_count > 1)
                                {
                                        exec_function_text_literal(made, " ");
                                        exec_function_text_words(made, node, 1);
                                }
                        }
                }
                return exec_function_text_loop_body(made, node, depth);
        }

        if (node->kind == NODE_CASE)
        {
                exec_function_text_literal(made, "case ");
                exec_function_text_words(made, node, 0);
                exec_function_text_literal(made, " in ");

                for (child = node->left; child; child = parse_nodes[child].next)
                {
                        parse_node address_to item = parse_nodes + child;

                        for (positive at = 0; at < item->word_count; at++)
                        {
                                b32 word = item->word + (b32)at;
                                if (at)
                                        exec_function_text_literal(made, "|");
                                exec_function_text_add(made, parse_words[word],
                                                       parse_word_lengths[word]);
                        }
                        exec_function_text_literal(made, ") ");
                        if (!exec_function_text_node(made, item->right,
                                                     depth + 1))
                                return false;
                        if (!exec_function_text_line(made))
                                return false;
                        exec_function_text_add(
                            made,
                            item->flags == CASE_FALL_THROUGH
                                ? (const_string)";& "
                                : item->flags == CASE_TEST_ON
                                      ? (const_string)";;& "
                                      : (const_string)";; ",
                            item->flags == CASE_TEST_ON ? 4 : 3);
                }

                exec_function_text_literal(made, "esac");
                return exec_function_text_redirects(made, node);
        }

        if (node->kind == NODE_FUNCTION)
        {
                exec_function_text_words(made, node, 0);
                exec_function_text_literal(made, " () ");
                if (!exec_function_text_body(made, node->right, depth + 1))
                        return false;
                return exec_function_text_redirects(made, node);
        }

        if (node->kind == NODE_TIME)
        {
                if (node->op)
                        exec_function_text_literal(made, "! ");
                exec_function_text_literal(made, "time ");
                if (node->flags)
                        exec_function_text_literal(made, "-p ");
                if (!exec_function_text_node(made, node->left, depth + 1))
                        return false;
                return exec_function_text_redirects(made, node);
        }

        if (node->kind == NODE_COPROC)
        {
                exec_function_text_literal(made, "coproc ");
                if (node->word_count &&
                    !word_is(parse_words[node->word], "COPROC"))
                {
                        exec_function_text_words(made, node, 0);
                        exec_function_text_literal(made, " ");
                }
                if (!exec_function_text_node(made, node->left, depth + 1))
                        return false;
                return exec_function_text_redirects(made, node);
        }

        return false;
}

static bool exec_function_text_definition(exec_function_text address_to made,
                                          positive slot, bool environment)
{
        exec_function address_to function = exec_functions + slot;

        made->used = 0;
        made->failed = false;
        made->pending_used = 0;

        if (environment)
        {
                exec_function_text_literal(made, "BASH_FUNC_");
                exec_function_text_add(made, function->name,
                                       function->name_length);
                exec_function_text_literal(made, "%%=() ");
        }
        else
        {
                exec_function_text_add(made, function->name,
                                       function->name_length);
                exec_function_text_literal(made, " () \n");
        }

        if (!exec_function_text_body(made, function->body, 0))
                return false;

        if (environment)
        {
                if (!exec_function_text_here_flush(made))
                        return false;
        }
        else if (!exec_function_text_line(made))
                return false;

        return !made->failed;
}

static bool exec_function_environment_prepare(positive slot)
{
        exec_function address_to function = exec_functions + slot;
        exec_function_text made;

        made.text = address_of function->environment;
        made.room = address_of function->environment_room;

        if (function->environment_valid)
                return true;
        function->environment_valid =
            exec_function_text_definition(address_of made, slot, true);
        return function->environment_valid;
}

bool exec_function_write(writer write, string_address name, b32 filter)
{
        positive2 named = string_hash_33_length(name);
        positive slot = exec_function_slot(name, named);
        p8 address_to text = null;
        positive room = 0;
        exec_function_text made;
        exec_function address_to function;
        bool answer;

        made.text = address_of text;
        made.room = address_of room;
        if (slot == positive_max)
                return false;
        function = exec_functions + slot;
        if ((filter & DECLARE_EXPORT) && !function->exported)
                return true;
        if ((filter & DECLARE_READONLY) && !function->readonly)
                return true;

        answer = exec_function_text_definition(address_of made, slot, false);
        if (answer)
        {
                write(text, made.used);
                if (function->readonly || function->exported)
                {
                        write("declare -f", 10);
                        if (function->readonly)
                                write("r", 1);
                        if (function->exported)
                                write("x", 1);
                        write(" ", 1);
                        write(function->name, function->name_length);
                        write("\n", 1);
                }
        }

        if (text)
                memory_free(text, room);
        return answer;
}

bool exec_function_export_set(string_address name, bool enabled)
{
        positive2 named = string_hash_33_length(name);
        positive slot = exec_function_slot(name, named);

        if (slot == positive_max)
                return false;
        if (enabled && !exec_function_environment_prepare(slot))
                return false;
        if (exec_functions[slot].exported != enabled)
        {
                exec_functions[slot].exported = enabled;
                exec_function_environment_changed();
        }
        return true;
}

positive exec_function_environment_generation()
{
        return exec_function_env_generation;
}

positive exec_function_environment_count()
{
        positive count = 0;

        for (positive at = 0; at < exec_function_count; at++)
                if (exec_functions[at].body && exec_functions[at].exported)
                        count++;
        return count;
}

bool exec_function_environment_fill(string_address address_to environment,
                                    positive count)
{
        positive used = 0;

        for (positive at = 0; at < exec_function_count; at++)
        {
                exec_function address_to function = exec_functions + at;
                if (!function->body || !function->exported)
                        continue;
                if (used >= count)
                        return false;

                if (!exec_function_environment_prepare(at))
                        return false;
                environment[used++] = function->environment;
        }

        return used == count;
}

b32 exec_function_unset(string_address name)
{
        positive2 named = string_hash_33_length(name);
        positive slot;

        for (slot = 0; slot < exec_function_count; slot++)
        {
                if (!exec_function_matches(slot, name, named.x, named.y))
                        continue;

                if (!exec_functions[slot].body)
                        return false;

                if (exec_functions[slot].readonly)
                        return -1;

                parse_release(exec_functions[slot].body);

                if (exec_functions[slot].exported)
                        exec_function_environment_changed();
                exec_functions[slot].body = 0;
                exec_functions[slot].exported = false;
                exec_functions[slot].environment_valid = false;
                if (exec_function_recent == slot)
                        exec_function_recent = positive_max;
                return true;
        }

        return false;
}

static b32 exec_node(b32 index);
/* Keep the large central dispatcher off A53 erratum 843419's page-edge
   ADRP/load shape.  This alignment removes a 4 KiB linker veneer island. */
static ARM64_ERRATUM_ALIGN b32 exec_node_kind(b32 index);

static b32 exec_define(b32 index)
{
        string_address name = parse_words[parse_nodes[index].word];
        b32 body;
        positive slot;
        positive2 named = string_hash_33_length(name);
        positive name_length = named.y;

        //      Dash already refused a non-identifier in the grammar. Bash
        //      --posix reaches here: lima 5.2.32 reports an invalid
        //      identifier and ends the process, including under command
        //      eval, so this is not an eval syntax error.
        if ((shell_posix_on() || !shell_bash_compat) &&
            !shell_valid_name(name, name_length))
        {
                shell_diagnostic_where();
                if (shell_bash_compat)
                {
                        string_format(log_error,
                                      "`%s': not a valid identifier\n", name);
                        expand_fatal_status(2);
                        return (shell_status = 2);
                }
                log_error(str("Syntax error: Bad function name\n"));
                exec_special_error_note();
                return (shell_status = 2);
        }

        for (slot = 0; slot < exec_function_count; slot++)
        {
                if (exec_function_matches(slot, name, named.x, named.y))
                        break;
        }

        if (slot < exec_function_count && exec_functions[slot].body &&
            exec_functions[slot].readonly)
        {
                string_format(log_error, "%s: readonly function\n", name);
                shell_status = 1;
                return 1;
        }

        if (slot == exec_function_count)
        {
                // An unset slot has no live tree and can carry a new name --
                // unless a call of what it held is still on the stack and
                // will count itself out of the slot on the way back.
                for (slot = 0; slot < exec_function_count; slot++)
                        if (!exec_functions[slot].body &&
                            !exec_functions[slot].active)
                                break;

                if (slot == exec_function_count)
                {
                        if (exec_function_count == positive_max ||
                            !shell_array_room(exec_functions, exec_function_room,
                                              exec_function_count + 1))
                                return (shell_status = string_report(log_error, 1, "No room for function: %s\n", name));

                        exec_functions[slot].name = null;
                        exec_functions[slot].name_room = 0;
                        exec_functions[slot].name_hash = 0;
                        exec_functions[slot].name_length = 0;
                        exec_functions[slot].body = 0;
                        exec_functions[slot].active = 0;
                        exec_functions[slot].readonly = false;
                        exec_functions[slot].exported = false;
                        exec_functions[slot].environment = null;
                        exec_functions[slot].environment_room = 0;
                        exec_functions[slot].environment_valid = false;
                        exec_functions[slot].special_kind = 0;
                        exec_function_count++;
                }

                if (name_length == positive_max ||
                    !shell_array_room(exec_functions[slot].name, exec_functions[slot].name_room,
                                      name_length + 1))
                        return (shell_status = string_report(log_error, 1, "No room for function: %s\n", name));

                string_copy(exec_functions[slot].name, name);
                exec_functions[slot].name_hash = named.x;
                exec_functions[slot].name_length = named.y;
                exec_functions[slot].readonly = false;
                exec_functions[slot].exported = false;
                exec_functions[slot].environment_valid = false;
                exec_functions[slot].special_kind =
                    exec_special_kind(name);
        }

        body = parse_keep(parse_nodes[index].right, exec_functions[slot].body);
        if (!body)
                return (shell_status = string_report(log_error, 1, "No room for function: %s\n", name));

        exec_functions[slot].body = body;
        exec_functions[slot].environment_valid = false;
        exec_function_source_set(slot, exec_frames_code_source());
        exec_function_recent = slot;
        if (exec_functions[slot].exported)
        {
                if (!exec_function_environment_prepare(slot))
                {
                        exec_functions[slot].exported = false;
                        exec_function_environment_changed();
                        string_format(log_error,
                                      "%s: function body cannot be exported\n",
                                      name);
                        shell_status = 1;
                        return 1;
                }
                exec_function_environment_changed();
        }
        shell_status = 0;

        return 0;
}

static bool exec_function_import_one(string_address entry)
{
        static const p8 prefix[] = "BASH_FUNC_";
        positive entry_length = string_length(entry);
        string_address equal;
        string_address value;
        positive name_length;
        positive value_length;
        p8 address_to source = null;
        positive room = 0;
        positive source_length;
        string_address line;
        b32 root = 0;
        bool answer = false;
        lex_frame lexed;

        if (entry_length < sizeof(prefix) + 5 ||
            memory_compare(entry, (address_any)prefix, sizeof(prefix) - 1))
                return false;

        equal = string_first_of(entry + sizeof(prefix) - 1, '=');
        if (!equal || equal < entry + sizeof(prefix) + 1 ||
            equal[-1] != '%' || equal[-2] != '%')
                return false;

        name_length = (positive)(equal - entry) - (sizeof(prefix) - 1) - 2;
        /* A name with a slash in it is a path, and a function by that name
           runs where a script wrote /usr/bin/id to be sure of the program:
           bash refuses to import one, and so does this. */
        if (memory_first_of((address_any)(entry + sizeof(prefix) - 1), '/',
                            name_length))
        {
                // Before any line is read, so the name and no line.
                string_format(log_error, "%s: ", shell_where_invoked());
                log_error(str("error importing function definition for `"));
                log_error(entry + sizeof(prefix) - 1, name_length);
                log_error(str("'\n"));
                return false;
        }
        value = equal + 1;
        value_length = entry_length - (positive)(value - entry);
        if (!name_length || value_length < 4 || value[0] != '(' ||
            value[1] != ')' || (value[2] != ' ' && value[2] != '\t') ||
            value[3] != '{' ||
            name_length > positive_max - value_length - 2)
                return false;

        source_length = name_length + value_length + 1;
        if (!shell_array_room(source, room, source_length + 1))
                return false;

        memory_copy(source, entry + sizeof(prefix) - 1, name_length);
        source[name_length] = ' ';
        memory_copy_end(source + name_length + 1, value, value_length);

        parse_nest_enter();
        lex_nest_enter(address_of lexed);
        line = source;

        while (string_get(line))
        {
                string_address stop = string_first_of_or_end(line, '\n');
                string_address next = stop;
                string_address waiting;

                if (string_get(stop))
                {
                        address_to stop = end;
                        next = stop + 1;
                }

                waiting = parse_here_open();
                if (waiting)
                        parse_here_line(line);
                else if (!parse_feed(line))
                        goto leave;

                if (!parse_here_open())
                {
                        root = parse_program();
                        if (parse_state == PARSE_INCOMPLETE)
                        {
                                line = next;
                                continue;
                        }
                        if (parse_state || !root ||
                            parse_nodes[root].kind != NODE_FUNCTION)
                                goto leave;

                        {
                                parse_node address_to function =
                                    parse_nodes + root;
                                b32 word = function->word;

                                if (function->word_count != 1 ||
                                    parse_word_lengths[word] != name_length ||
                                    memory_compare(
                                        parse_words[word],
                                        entry + sizeof(prefix) - 1,
                                        name_length))
                                        goto leave;
                        }

                        /* A valid definition may end before the environment
                           string does. Only blank physical lines may follow;
                           another AST is never installed or executed. */
                        for (string_address rest = next; string_get(rest); rest++)
                                if (string_not(rest, ' ') &&
                                    string_not(rest, '\t') &&
                                    string_not(rest, '\n') &&
                                    string_not(rest, '\r'))
                                        goto leave;

                        answer = exec_define(root) == 0;
                        if (answer &&
                            !exec_function_export_set(
                                exec_functions[exec_function_recent].name,
                                true))
                        {
                                exec_function_unset(
                                    exec_functions[exec_function_recent].name);
                                answer = false;
                        }
                        goto leave;
                }

                line = next;
        }

leave:
        parse_reset();
        lex_nest_leave(address_of lexed);
        parse_nest_leave();
        memory_free(source, room);
        return answer;
}

fn exec_function_import_environment(string_address address_to environment)
{
        b32 status = shell_status;

        if (!shell_bash_compat || shell_startup_privileged || shell_restricted)
                return;

        for (positive at = 0; environment && environment[at]; at++)
                if (env_function_assignment(environment[at]))
                        exec_function_import_one(environment[at]);

        shell_status = status;
}

/*
        FUNCNAME, BASH_SOURCE and BASH_LINENO.

        Three arrays that describe the call stack, innermost first. What a
        call costs is a name pushed on a stack; the arrays themselves are
        built the first time something asks for one, because writing three
        arrays on the way into every function cost more than the call did
        and a script that never mentions them should not pay for them.

        BASH_LINENO answers where each call was written, which is the line
        the reader was on when the frame was pushed. BASH_SOURCE names the
        script, which is the only source this shell has.
*/
// One entry and not two arrays: a call would otherwise ask twice whether
// there was room, and a call is the thing being counted.
typedef struct
{
        string_address name;
        positive line;
        // The file this frame's code was read from: a function's definition
        // file, or the file a `.` is reading.
        string_address source;
} exec_frame;

static exec_frame address_to exec_frames;
static positive exec_frame_room;
static positive exec_frame_count;
static bool exec_frames_published;
static bool exec_frames_standing;

/* A script named on the command line has a bottom frame of its own, main,
   read from that file; -c, standard input and a terminal have none. */
static PURE bool exec_frames_main()
{
        return !string_get(shell_option_flags) && !shell_is_interactive;
}

// The file the code now running was read from.
static string_address exec_frames_code_source()
{
        return exec_frame_count ? exec_frames[exec_frame_count - 1].source
                                : shell_script_name;
}

/*
        Names a frame points at outlive the reading that gave them: a
        function defined in a sourced file names that file for as long as it
        is defined. Each distinct name is kept once.
*/
static string_address address_to exec_source_names;
static positive exec_source_names_room;
static positive exec_source_name_count;

static string_address exec_source_intern(string_address name)
{
        positive length;
        p8 address_to made = null;
        positive made_room = 0;

        for (positive at = 0; at < exec_source_name_count; at++)
                if (!string_compare(exec_source_names[at], name))
                        return exec_source_names[at];

        length = string_length(name);
        if (length == positive_max ||
            !shell_array_room(made, made_room, length + 1) ||
            !shell_array_room(exec_source_names, exec_source_names_room,
                              exec_source_name_count + 1))
                return shell_script_name;

        memory_copy_end(made, name, length);
        exec_source_names[exec_source_name_count++] = made;
        return made;
}

static COLD fn exec_frames_forget();

// `.` and source: a frame named source for the file being read.
fn exec_frames_source_enter(string_address path)
{
        if (!shell_array_room(exec_frames, exec_frame_room,
                              exec_frame_count + 1))
                return;
        exec_frames[exec_frame_count].line = (positive)exec_line;
        exec_frames[exec_frame_count].name = (string_address) "source";
        exec_frames[exec_frame_count++].source = exec_source_intern(path);
        exec_frames_published = false;
        if (exec_frames_standing)
                exec_frames_forget();
}

fn exec_frames_source_leave()
{
        if (!exec_frame_count)
                return;
        exec_frame_count--;
        exec_frames_published = false;
        if (exec_frames_standing)
                exec_frames_forget();
}

static COLD fn exec_frames_forget()
{
        env_unset("FUNCNAME");
        env_unset("BASH_SOURCE");
        env_unset("BASH_LINENO");
        exec_frames_standing = false;
}

/*
        Innermost first, which is the opposite of the order the frames were
        pushed in and the order every script that reads them expects, with
        main at the bottom for a script file. FUNCNAME stands only while a
        function runs; BASH_SOURCE and BASH_LINENO stand wherever there is a
        frame, so ${BASH_SOURCE[0]} at the top of a script is its path and
        in a sourced file that file's. BASH_SOURCE was $0 for every frame
        and nothing at all outside a function, which is what
        cd "$(dirname "${BASH_SOURCE[0]}")" reads.
*/
static COLD fn exec_frames_publish()
{
        shell_mark held = shell_store_mark(address_of exec_store);
        string_address address_to walked;
        bipolar address_to lines;
        bool main = exec_frames_main();
        positive count = exec_frame_count + main;

        exec_frames_published = true;

        if (!count)
        {
                exec_frames_forget();
                return;
        }

        exec_frames_standing = true;

        walked = (string_address address_to)shell_store_take(
            address_of exec_store, count * sizeof(walked[0]));
        lines = (bipolar address_to)shell_store_take(
            address_of exec_store, count * sizeof(lines[0]));

        if (!walked || !lines)
        {
                shell_store_rewind(address_of exec_store, held);
                return;
        }

        for (positive at = 0; at < exec_frame_count; at++)
        {
                walked[at] = exec_frames[exec_frame_count - at - 1].name;
                lines[at] = (bipolar)exec_frames[exec_frame_count - at - 1].line;
        }
        if (main)
        {
                walked[exec_frame_count] = (string_address) "main";
                lines[exec_frame_count] = 0;
        }

        if (exec_function_depth)
                shell_array_words("FUNCNAME", 8, walked, count);
        else
                env_unset("FUNCNAME");
        shell_array_numbers("BASH_LINENO", 11, lines, count);

        for (positive at = 0; at < exec_frame_count; at++)
                walked[at] = exec_frames[exec_frame_count - at - 1].source;
        if (main)
                walked[exec_frame_count] = shell_script_name;

        shell_array_words("BASH_SOURCE", 11, walked, count);
        shell_store_rewind(address_of exec_store, held);
}

/*
        caller: where the function this is running in was called from.

        With no operand it is the line and the source; with a number it is
        that many frames further out and the name of the function there as
        well, which is what a script printing a backtrace walks. Outside a
        function there is no frame to describe and the answer is a failure.
*/
PURE positive shell_line_now()
{
        /* A diagnostic before any command has run still needs the line the
           reader is on: syntax errors used to say line 0 because exec_line
           is only filled when a node starts. */
        if (expand_substitution_lineno)
                return expand_substitution_lineno;

        /* Inside an eval body a command that has started is on a line of
           that body -- its own line, not how far the eval's reader got, which
           for an if or a loop is its last line: eval 'if :; then\n echo
           $LINENO\nfi' said the line of the fi. Before any has started, the
           reader's line is all there is. */
        if (shell_eval_lineno_base)
                return shell_eval_lineno_base +
                       (exec_line ? (positive)exec_line
                                  : shell_line_number ? shell_line_number : 1) - 1;

        return exec_line ? (positive)exec_line : shell_line_number;
}

// The line of the command running now, handed over and taken back by eval so
// that its body's commands count from their own first line.
positive exec_line_exchange(positive line)
{
        positive was = (positive)exec_line;

        exec_line = (b32)line;
        return was;
}

PURE bool exec_in_function()
{
        return exec_function_depth != 0;
}

/*
        The line Bash reports as $LINENO for the first line of an eval body:
        the line the eval command starts on, or that command's already-offset
        line when eval is nested inside eval. It used to be the reader's line,
        which is the eval's last line on its own and the end of the whole
        construct inside an if or a loop: eval "echo a\necho \$LINENO" at the
        top of a script said 3 where bash says 2, and 7 inside an if on line 1.
*/
positive shell_eval_lineno_base_now()
{
        if (shell_eval_lineno_base)
                return shell_line_now();
        if (exec_line)
                return (positive)exec_line;
        return shell_line_number ? shell_line_number : 1;
}

// Whether a compound command is running, which the reader has already read
// to its end.
PURE bool exec_compound_now()
{
        return exec_compound_depth != 0;
}

/*
        Whether a name is in the environment children inherit.

        Every other attribute is a bit of the variable's attribute byte and
        shell_variable_attributes hands that over; this one is a field of the
        entry beside it, and the entries live in the file above this one.
*/
PURE bool shell_variable_exported(const_string name, positive length)
{
        positive found = env_find_span(name, length);

        return found < shell_var_count && shell_vars[found].permanent;
}

fn shell_caller(writer write, string_address input)
{
        positive want = 0;
        bool numbered = shell_argc > 1;
        bool main = exec_frames_main();
        positive count = exec_frame_count + main;
        p8 shown[32];
        positive written;

        //      Nothing called this, so there is nothing to name and the
        //      words are never read: Bash answers one for "caller x" at the
        //      top level and refuses the same word inside a function.
        if (!exec_frame_count)
        {
                shell_answer(1);
                return;
        }

        if (numbered && !string_digits_exact(shell_argv[1], address_of want))
        {
                shell_told("caller: %s: invalid number\n", shell_argv[1]);
                string_format(log_error, "caller: usage: caller [expr]\n");
                shell_answer(2);
                return;
        }

        //      Numbered caller is BASH_LINENO[n], FUNCNAME[n+1] and
        //      BASH_SOURCE[n+1]. A function called from the top of -c has
        //      no n+1 slot, so `caller 0` inside it prints nothing; one run
        //      from a script file has main there.
        if (want >= exec_frame_count ||
            (numbered && want + 1 >= count))
        {
                shell_answer(1);
                return;
        }

        written = bipolar_into_string(
            shown, (bipolar)exec_frames[exec_frame_count - want - 1].line);
        write(shown, written);
        write(" ", 1);

        if (numbered)
        {
                write(want + 1 < exec_frame_count
                          ? exec_frames[exec_frame_count - want - 2].name
                          : (string_address) "main",
                      0);
                write(" ", 1);
        }

        /* Bash calls an unnamed input source NULL. A named script gives the
           file the calling frame was read from, while stdin and -c must not
           expose argv[0] as though it were the file containing the function. */
        write(string_get(shell_option_flags)
                  ? (string_address) "NULL"
                  : want + 1 < exec_frame_count
                        ? exec_frames[exec_frame_count - want - 2].source
                        : shell_script_name,
              0);
        write("\n", 1);

        shell_answer(0);
}

/*
        Something has asked for one of the three, so now they are made.

        Only a lookup that has already missed reaches this, which is where
        the three of them always miss until a function is running. A name
        that is not one of them costs two length tests, and the caller is
        told whether looking again is worth anything.
*/
COLD bool shell_frames_wanted(const_string name, positive length)
{
        if (exec_frames_published || (!exec_frame_count && !exec_frames_main()))
                return false;

        if (!memory_is_word((address_any)name, length, "FUNCNAME") &&
            !memory_is_word((address_any)name, length, "BASH_SOURCE") &&
            !memory_is_word((address_any)name, length, "BASH_LINENO"))
                return false;

        exec_frames_publish();

        return true;
}

static b32 exec_call(positive slot);

/*
        command_not_found_handle: a function of that name, when bash finds
        no command, is called in a subshell with the command and its words
        as its arguments, and its status is the command's. Its own misses
        are not handed back to it.
*/
static bool exec_not_found_inside;
positive shell_function_slot(string_address name);

static COLD bool exec_not_found_handled()
{
        positive slot;
        bipolar child;

        if (!shell_bash_compat || exec_not_found_inside || !exec_function_count ||
            string_first_of(shell_argv[0], '/'))
                return false;
        slot = shell_function_slot((string_address) "command_not_found_handle");
        if (slot == positive_max)
                return false;

        log_flush();
        child = shell_clone();
        if (child < 0)
                return false;
        if (child == 0)
        {
                string_address address_to words = null;
                positive room = 0;

                exec_forked = true;
                exec_not_found_inside = true;
                job_forget();
                exec_loop_depth = 0;
                if (!shell_array_room(words, room,
                                      (shell_argc + 2) * sizeof(*words)))
                        exec_child_leave(127);
                words[0] = (string_address) "command_not_found_handle";
                memory_copy(words + 1, shell_argv,
                            shell_argc * sizeof(*words));
                words[shell_argc + 1] = null;
                shell_argv = words;
                shell_argc++;
                exec_child_leave(exec_call(slot));
        }
        shell_status = exec_child_status(child);
        return true;
}

static b32 exec_call(positive slot)
{
        b32 body = exec_functions[slot].body;
        positive saved_count = shell_parameter_count;
        positive saved = 0;
        b32 status;
        shell_getopts_state saved_getopts;
        bool saved_replaced = shell_parameters_replaced;
        bool held_parameters;

        if (shell_dash_compat)
                saved_getopts = shell_getopts_save();

        if (exec_function_depth == 0x7fffffff)
        {
                log_error(str("Too deep\n"));
                shell_status = 1;
                return 1;
        }

        /*
                FUNCNEST, a positive number, is as deep as bash lets
                functions call: one call past it is refused, and the whole
                command the reader was running goes with it.
        */
        if (shell_bash_compat && shell_funcnest_seen)
        {
                string_address nest = env_get("FUNCNEST");
                positive limit;

                if (nest && string_digits_exact(nest, address_of limit) &&
                    limit && exec_function_depth >= limit)
                {
                        shell_diagnostic_where();
                        string_format(log_error,
                                      "%s: maximum function nesting level "
                                      "exceeded (%p)\n",
                                      shell_argv[0], limit);
                        expand_discard_whole(1);
                        return 1;
                }
        }

        if (!shell_local_enter())
        {
                shell_status = 1;
                return 1;
        }

        held_parameters = saved_count || shell_argc > 1;

        if (held_parameters)
        {
                saved = shell_parameters_save();
                if (saved == EXPAND_NO_ROOM ||
                    !shell_parameters_restore_prepare(saved_count) ||
                    !shell_parameters_set(shell_argv + 1,
                                          shell_argc > 0 ? shell_argc - 1 : 0))
                {
                        if (saved != EXPAND_NO_ROOM)
                                shell_parameter_stack_used = saved;
                        shell_local_leave();
                        log_error(str("No room for function arguments\n"));
                        shell_status = 1;
                        return 1;
                }

                shell_getopts_parameters_changed();
        }

        // By index and not by address: a definition made inside the body can
        // grow the table, and the table may move when it does.
        exec_function_depth++;
        exec_functions[slot].active++;
        parse_kept_bodies[body].references++;

        if (shell_array_room(exec_frames, exec_frame_room,
                             exec_frame_count + 1))
        {
                // Where the call was written, which is the line of the
                // command making it and not the line the reader is on.
                exec_frames[exec_frame_count].line = (positive)exec_line;
                exec_frames[exec_frame_count].source =
                    exec_function_source(slot);
                exec_frames[exec_frame_count++].name =
                    exec_functions[slot].name;
                exec_frames_published = false;
                // Arrays made for the caller answer a plain lookup, which
                // does not ask again; take them down so this frame's are
                // made when read.
                if (exec_frames_standing)
                        exec_frames_forget();
        }

        status = exec_node(body);

        // A return anywhere below -- a trap action's among them -- left its
        // answer in shell_status whatever the node it unwound through said.
        if (exec_signal == EXEC_SIGNAL_RETURN)
                status = shell_status;

        // The function is still standing while its RETURN trap runs, which is
        // what lets the action read FUNCNAME and the status it is returning.
        if (trap_return_here && shell_extra_on(SHELL_EXTRA_FUNCTRACE))
        {
                shell_status = status;
                exec_trap_condition(TRAP_RETURN);
        }

        if (exec_frame_count)
        {
                exec_frame_count--;
                exec_frames_published = false;

                // The three only exist while a function does, and they were
                // only ever made if something read them. Made inside the
                // callee they name it, and $FUNCNAME in the caller read
                // inner after inner had returned; they go, to be made again
                // for this frame when read.
                if (exec_frames_standing)
                        exec_frames_forget();
        }

        exec_functions[slot].active--;
        parse_release(body);
        shell_local_leave();
        exec_function_depth--;

        // return leaves the function and nothing further out.
        // A failglob inside the body is the same: the rest of the
        // function is skipped, the caller is not.
        if (exec_signal == EXEC_SIGNAL_RETURN ||
            (exec_input_error() && !expand_discard_whole_line))
                exec_signal = EXEC_SIGNAL_NONE;

        if (held_parameters)
        {
                if (!shell_parameters_restore(saved, saved_count))
                {
                        log_error(str("No room to restore function arguments\n"));
                        shell_status = 2;
                        /* Restore storage was reserved before the function ran, and
                           the caller's byte/table capacities cannot have shrunk.
                           Continuing would expose the callee's $@ as caller state;
                           make an invariant violation terminal instead. */
                        log_flush();
                        exit(2);
                }
        }
        else if (shell_parameter_count &&
                 !shell_parameters_set(shell_parameter, 0))
        {
                log_error(str("No room to restore function arguments\n"));
                shell_status = 2;
                log_flush();
                exit(2);
        }

        if (shell_dash_compat)
                shell_getopts_restore(saved_getopts);

        shell_parameters_replaced = saved_replaced;
        return status;
}

positive shell_function_slot(string_address name)
{
        return exec_function_slot(name, string_hash_33_length(name));
}

b32 shell_call_slot(positive slot, string_address name,
                    string_address address_to arguments, positive count)
{
        string_address words[8];
        string_address address_to saved_argv = shell_argv;
        positive saved_argc = shell_argc;
        positive at;
        b32 status;

        if (slot == positive_max)
                return 127;

        if (count > 6)
                count = 6;

        words[0] = name;
        memory_copy(words + 1, arguments, count * sizeof(*arguments));
        words[count + 1] = null;

        shell_argv = words;
        shell_argc = count + 1;
        status = exec_call(slot);
        shell_argv = saved_argv;
        shell_argc = saved_argc;
        return status;
}

/*
        set -x: the command about to run, written out.

        After the words are expanded, so what is traced is what runs, and
        before anything is redirected, so a command that sends its own errors
        somewhere does not send the trace there with them. PS4 goes in front,
        which is what a script marking its own depth changes.
*/
#define SHELL_XTRACE ((positive)1 << ('x' - 'a'))

/*
        Whether Bash would have to quote a word to write it back.

        Its list, not a guess at one: the blanks, the quoting characters,
        everything the parser reads as an operator, the globbing characters,
        the two expansion characters and a comma. A tilde only counts where
        it would expand -- at the front, or after the colon or equals sign
        an assignment puts it behind -- and a hash only where a comment
        would begin.
*/
static COLD PURE bool exec_trace_quoting(string_address word)
{
        for (string_address at = word; string_get(at); at++)
                switch (string_get(at))
                {
                case ' ': case '\t': case '\n':
                case '\'': case '"': case '\\':
                case '|': case '&': case ';':
                case '(': case ')': case '<': case '>':
                case '!': case '{': case '}':
                case '*': case '[': case '?': case ']':
                case '^': case '$': case '`':
                        return true;
                case '~':
                        if (at == word || at[-1] == ':' || at[-1] == '=')
                                return true;
                        break;
                case '#':
                        if (at == word)
                                return true;
                        break;
                default:
                        break;
                }

        return false;
}

/* One word as Bash 5.2 writes it: bare where it can be, in single quotes
   where a meta requires quoting -- a newline and a tab among them -- and a
   bare pair of quotes where the word is empty. $'...' is only for a word
   whose unprintable bytes -- in the C locale every control character and
   every byte with the high bit set -- have no meta that would have taken
   the other spelling. */
static COLD fn exec_trace_word(string_address word)
{
        positive length = string_length(word);

        // An empty word is one write, as it was, so pipeline stages tracing
        // side by side have no more places to interleave.
        if (!length)
                log_error(str("''"));
        else if (exec_trace_quoting(word))
                shell_single_quote_write(log_error, word, length);
        else if (memory_escape_index(word, length,
                                     HEX_CONTROL | HEX_TAB | HEX_HIGH) < length)
                shell_ansi_run(log_error, word, length, true);
        else
                log_error(word, length);
}

/* NAME= then the value, quoted the way a word is. Bash never wraps the
   name into that quoting, so a value that needs quotes is x='q"uote'
   and not 'x=q"uote'. An empty value is the name and the equals alone. */
static COLD fn exec_trace_assignment(string_address word)
{
        positive name_length = 0;
        p8 kind = shell_assignment_kind(word, address_of name_length);
        string_address value;

        if (!kind)
        {
                exec_trace_word(word);
                return;
        }

        log_error(word, name_length + kind);
        value = word + name_length + kind;
        if (string_get(value))
                exec_trace_word(value);
}

static bool exec_ps4_expanding;
static bool exec_ps4_dash_blocked;

static fn exec_ps4_dash_block(bool on)
{
        exec_ps4_dash_blocked = on;
}

static inline INLINE PURE bool exec_trace_on()
{
        return (shell_options & SHELL_XTRACE) && !exec_ps4_expanding;
}

static bool exec_ps4_command_sub(string_address text)
{
        while (string_get(text))
        {
                p8 value = string_get(text);

                if (value == '\\' && string_get(text + 1))
                {
                        text += 2;
                        continue;
                }

                if (value == '`')
                        return true;

                if (value == '$' && string_get(text + 1) == '(' &&
                    string_get(text + 2) != '(')
                        return true;

                text++;
        }

        return false;
}

static fn exec_trace_ps4()
{
        string_address prefix;
        string_address expanded;

        prefix = env_get("PS4");
        if (!prefix)
                prefix = (string_address) "+ ";

        /*
                Dash and bash both expand parameters, command substitutions
                and arithmetic in PS4. Expanding with xtrace still on would
                trace the expansion itself. Bash then applies prompt
                backslash escapes and writes the first character once per
                reader depth. Dash writes the expanded bytes as they stand.

                Dash re-parses PS4 as a double-quoted string, so a leftover
                end-of-file token from a one-line eval argument makes a
                command substitution in that parse fail. The stored prefix
                is then written, and the next trace expands again.
        */
        if (exec_ps4_dash_blocked && exec_ps4_command_sub(prefix))
        {
                exec_ps4_dash_blocked = false;
                shell_syntax_where();
                log_error(str("Syntax error: end of file unexpected"
                              " (expecting \")\")\n"));
                log_error(prefix, 0);
                return;
        }

        {
                bool soft = expand_errors_soft;

                expand_errors_soft = true;
                exec_ps4_expanding = true;
                //      bash decodes the prompt escapes first and expands
                //      what that made; dash only expands.
                expanded = shell_bash_compat ? shell_prompt_expand(prefix, false)
                                             : shell_expand_ps4(prefix);
                exec_ps4_expanding = false;
                expand_errors_soft = soft;
        }
        //      An expansion PS4 could not finish leaves the prompt as it
        //      was written, and the command still runs.
        bool failed = expand_failed;

        if (failed)
                expand_failed = false;
        else if (expanded)
                prefix = expanded;

        if (shell_bash_compat)
        {
                p8 first = string_get(prefix);

                if (first)
                {
                        positive depth = shell_run_depth ? shell_run_depth : 1;
                        p8 held = first;
                        p8 room[2];

                        room[0] = held;
                        room[1] = end;
                        for (positive again = 0; again < depth && again < 99;
                             again++)
                                log_error(room, 1);

                        if (string_get(prefix + 1))
                                log_error(prefix + 1, 0);
                        return;
                }
        }

        log_error(prefix, 0);
}

/*
        The words a simple command expanded to.

        Bash writes each prefix assignment on a line of its own -- the name,
        the equals, and the value quoted as a word -- and then the command.
        Dash writes every word as it stands, assignments and command together.
*/
static COLD fn exec_trace(b32 count, b32 assignments)
{
        b32 at;
        b32 from;

        if (!exec_trace_on())
                return;

        if (shell_bash_compat)
        {
                for (at = 0; at < assignments; at++)
                {
                        exec_trace_ps4();
                        exec_trace_assignment(shell_argv[at]);
                        log_error((string_address) "\n", 1);
                }
                from = assignments;
                if (from == count)
                        return;
        }
        else
                from = 0;

        exec_trace_ps4();
        for (at = from; at < count; at++)
        {
                if (at != from)
                        log_error((string_address) " ", 1);

                if (shell_bash_compat)
                        exec_trace_word(shell_argv[at]);
                else
                        log_error(shell_argv[at], 0);
        }

        log_error((string_address) "\n", 1);
}

/*
        Bash writes the source of a for header once per trip, a select
        header once when the menu is first drawn, and a case header once.
        The words are the ones the parser kept, quotes and all, so
        `for i in $x` traces `$x` and not what it became. Dash traces
        only the commands inside.
*/
static fn exec_trace_for_header(parse_node address_to node, bool selecting)
{
        if (!exec_trace_on() || !shell_bash_compat)
                return;

        exec_trace_ps4();
        log_error(selecting ? (string_address) "select " : (string_address) "for ",
                  selecting ? 7 : 4);
        if (node->word_count)
                log_error(parse_words[node->word],
                          parse_word_lengths[node->word]);
        if (node->flags)
        {
                log_error((string_address) " in", 3);
                for (positive at = 1; at < node->word_count; at++)
                {
                        log_error((string_address) " ", 1);
                        log_error(parse_words[node->word + at],
                                  parse_word_lengths[node->word + at]);
                }
        }
        else
                log_error((string_address) " in \"$@\"", 8);
        log_error((string_address) "\n", 1);
}

static fn exec_trace_case_header(parse_node address_to node)
{
        if (!exec_trace_on() || !shell_bash_compat)
                return;

        exec_trace_ps4();
        log_error((string_address) "case ", 5);
        if (node->word_count)
                log_error(parse_words[node->word],
                          parse_word_lengths[node->word]);
        log_error((string_address) " in", 3);
        log_error((string_address) "\n", 1);
}

/*
        (( )) as Bash writes it: the inside between `(( ' and ` ))',
        spaces that were in the source kept. The closing pair was taken
        off for the evaluator, so what is handed here is already the
        inside.
*/
static fn exec_trace_arith(string_address inner)
{
        if (!exec_trace_on() || !shell_bash_compat)
                return;

        exec_trace_ps4();
        log_error(str("(( "));
        log_error(inner, 0);
        log_error(str(" ))\n"));
}

/*
        One [[ ]] term as Bash writes it.

        && and || are not printed: each unary or binary term that actually
        runs is a line of its own, operands already expanded, empty ones a
        pair of quotes, and everything else as it stands. A `!' that belongs
        to this term is written; one that inverted a parenthesised group is
        not, because that invert lives on the group and not on the term.
*/
static fn exec_trace_conditional_operand(string_address word)
{
        if (!word || !string_get(word))
                log_error(str("''"));
        else
                log_error(word, 0);
}

static fn exec_trace_conditional_term(bool invert, string_address left,
                                     string_address op, string_address right)
{
        if (!exec_trace_on() || !shell_bash_compat)
                return;

        exec_trace_ps4();
        log_error(str("[[ "));
        if (invert)
                log_error(str("! "));
        if (!right)
        {
                log_error(op, 0);
                log_error((string_address) " ", 1);
                exec_trace_conditional_operand(left);
        }
        else
        {
                exec_trace_conditional_operand(left);
                log_error((string_address) " ", 1);
                log_error(op, 0);
                log_error((string_address) " ", 1);
                exec_trace_conditional_operand(right);
        }
        log_error(str(" ]]\n"));
}

/*
        Where one word of a compound assignment ends.

        a=(x "y z" $v) reaches the executor as a single word, because the
        lexer had to keep the parentheses with the name to know that they
        were not a subshell. Cutting it up again needs only the boundaries --
        a blank that is not inside quoting or a substitution -- since
        everything else about each piece is the ordinary word expansion that
        every other word gets.
*/
static COLD PURE string_address exec_compound_end(string_address at)
{
        while (string_get(at))
        {
                if (lex_is_space(string_get(at)))
                        break;

                if (lex_skip_held(address_of at))
                        continue;

                at++;
        }

        return at;
}

static string_address address_to exec_compound_word;
static positive exec_compound_room;

/*
        One element of a compound assignment, held until every piece has
        been expanded. Bash expands the whole list before it empties or
        extends the array, so ar=("${ar[@]}" c) is the old elements and c;
        emptying first left c alone, and ar+=(x "${ar[@]}") saw its own x.
*/
typedef struct exec_compound_held
{
        string_address key;
        positive key_length;
        string_address value;
        struct exec_compound_held address_to next;
} exec_compound_held;

/*
        local and declare in a function make their name before they assign
        it, so local old=(x "${old[@]}") read the new empty local where bash
        reads the caller's old: bash expands the list first. The list is read
        here ahead of the local, and the assignment that follows takes it.
*/
static exec_compound_held address_to exec_compound_prepared;
static string_address exec_compound_prepared_body;
static bool exec_compound_preparing;
static bool exec_compound_prepared_keyed;

fn shell_compound_prepare_drop()
{
        exec_compound_prepared = null;
        exec_compound_prepared_body = null;
}

bool shell_compound_prepare(string_address name, positive name_length,
                            string_address body, positive body_length,
                            bool keyed)
{
        bool answer;

        exec_compound_preparing = true;
        exec_compound_prepared_keyed = keyed;
        answer = shell_compound_assign(name, name_length, body, body_length,
                                       false);
        exec_compound_preparing = false;
        return answer;
}

static bool exec_compound_put(string_address name, positive name_length,
                              string_address key, positive key_length,
                              string_address value,
                              exec_compound_held address_to address_to tail)
{
        exec_compound_held address_to held;

        if (!tail)
                return shell_array_set(name, name_length, key, key_length,
                                       value, false);

        held = (exec_compound_held address_to)shell_store_take(
            address_of exec_store, sizeof(*held));
        if (!held)
                return false;
        held->key = shell_store_copy(address_of exec_store, key, key_length);
        held->key_length = key_length;
        held->value = shell_store_copy(address_of exec_store, value,
                                       string_length(value));
        held->next = null;
        if (!held->key || !held->value)
                return false;
        *tail = held;
        return true;
}

/*
        NAME=(...) and NAME+=(...).

        Bash replaces an array rather than merging into one, so a plain
        assignment empties it first; an append carries on past the largest
        subscript in use. A piece spelled [key]=value places itself and moves
        the running subscript to just after where it landed, which is what
        makes a=(x [5]=w y) put y at six.
*/
COLD bool shell_compound_assign(string_address name, positive name_length,
                           string_address body, positive body_length,
                           bool append)
{
        shell_mark held = shell_store_mark(address_of exec_store);
        const_string resolved_name;
        positive resolved_length;
        bool keyed;
        string_address at = body;
        string_address stop = body + body_length;
        positive next = 0;
        bool answer = true;

        if (!shell_reference_resolve(name, name_length, address_of resolved_name,
                                     address_of resolved_length))
        {
                shell_store_rewind(address_of exec_store, held);
                return false;
        }

        name = (string_address)resolved_name;
        name_length = resolved_length;
        keyed = exec_compound_preparing
                    ? exec_compound_prepared_keyed
                    : (shell_array_attributes(name, name_length) &
                       SHELL_ARRAY_ASSOCIATIVE) != 0;

        if (!exec_compound_preparing &&
            !shell_variable_attribute_set(
                name, name_length,
                (p8)((keyed ? SHELL_ARRAY_ASSOCIATIVE : SHELL_ARRAY_INDEXED) |
                     SHELL_ARRAY_ASSIGNED),
                0))
        {
                shell_store_rewind(address_of exec_store, held);
                return false;
        }

        exec_compound_held address_to first = null;
        exec_compound_held address_to address_to tail = address_of first;
        bool pairs_decided = false;
        bool pairs = false;
        string_address pending = null;

        //      The list was already read, before local made the name: take
        //      what it held rather than read it again.
        if (exec_compound_prepared_body == body)
        {
                first = exec_compound_prepared;
                exec_compound_prepared = null;
                exec_compound_prepared_body = null;
                at = stop;
        }

        if (append && shell_array_length(name, name_length))
                next = shell_array_highest(name, name_length) + 1;

        while (at < stop && answer)
        {
                string_address finish;
                string_address piece;
                string_address value;
                string_address shut = null;
                positive length;
                positive key_length = 0;
                p8 written[32];

                while (at < stop && (string_is(at, ' ') || string_is(at, '\t') ||
                                     string_is(at, '\n')))
                        at++;

                /* A list written over several lines may carry comments,
                   which run to the end of their line. */
                if (at < stop && string_is(at, '#') && lex_comments_on())
                {
                        while (at < stop && !string_is(at, '\n'))
                                at++;
                        continue;
                }

                if (at >= stop)
                        break;

                finish = exec_compound_end(at);

                if (finish > stop)
                        finish = stop;

                length = (positive)(finish - at);
                piece = shell_store_copy(address_of exec_store, at, length);

                if (!piece)
                {
                        answer = false;
                        break;
                }

                at = finish;

                if (string_is(piece, '['))
                        shut = expand_bracket_end(piece + 1, '[', ']');

                /*
                        bash 5.1's other spelling of an associative list:
                        when its first word is no [key]=value, the words are
                        keys and values in turn, each expanded as an
                        assignment's value is, and a key left without a value
                        gets an empty one.
                */
                if (keyed && !pairs_decided)
                {
                        pairs_decided = true;
                        pairs = !(shut && string_is(shut + 1, '='));
                }
                if (pairs)
                {
                        value = shell_expand_assignment(piece, 0);
                        if (!value)
                        {
                                answer = false;
                                break;
                        }
                        if (!pending)
                        {
                                pending = shell_store_copy(
                                    address_of exec_store, value,
                                    string_length(value));
                                if (!pending)
                                        answer = false;
                                continue;
                        }
                        if (!string_get(pending))
                                log_error(str("'': bad array subscript\n"));
                        else
                        {
                                answer = exec_compound_put(
                                    name, name_length, pending,
                                    string_length(pending), value, tail);
                                if (tail && *tail)
                                        tail = address_of (*tail)->next;
                        }
                        pending = null;
                        continue;
                }

                if (shut && string_is(shut + 1, '='))
                {
                        string_address key;
                        positive value_at =
                            (positive)(shut - piece) + 2;

                        // The subscript is resolved against the array it is
                        // being written into, so a keyed one stays bytes and
                        // an indexed one is arithmetic, exactly as it would
                        // be written on its own line.
                        key = shell_expand_subscript(name, name_length,
                                                     piece + 1,
                                                     (positive)(shut - piece) - 1,
                                                     address_of key_length);

                        if (!key)
                        {
                                answer = false;
                                break;
                        }

                        value = shell_expand_assignment(piece, value_at);
                        answer = exec_compound_put(name, name_length, key,
                                                   key_length, value + value_at,
                                                   tail);
                        if (tail && *tail)
                                tail = address_of (*tail)->next;

                        if (!keyed)
                                next = array_index_of(key, key_length) + 1;

                        continue;
                }

                if (keyed)
                {
                        string_format(log_error,
                                      "%s: must use subscript when assigning "
                                      "associative array\n",
                                      name);
                        answer = false;
                        break;
                }

                /* A bare piece is an ordinary word: $v with a space in it
                   becomes two elements, which is what field splitting is. */
                {
                        shell_words fields;
                        positive count;

                        shell_words_bind(address_of fields,
                                         address_of exec_compound_word,
                                         address_of exec_compound_room);
                        count = shell_expand_fields(piece, address_of fields);

                        for (positive one = 0; one < count && answer; one++)
                        {
                                key_length = positive_into_string(written,
                                                                  next++);
                                answer = exec_compound_put(
                                    name, name_length, written, key_length,
                                    exec_compound_word[one], tail);
                                if (tail && *tail)
                                        tail = address_of (*tail)->next;
                        }
                }
        }

        if (exec_compound_preparing)
        {
                exec_compound_prepared = answer ? first : null;
                exec_compound_prepared_body = answer ? body : null;
                return answer;
        }

        if (answer && pending)
        {
                if (!string_get(pending))
                        log_error(str("'': bad array subscript\n"));
                else
                {
                        answer = exec_compound_put(name, name_length, pending,
                                                   string_length(pending), "",
                                                   tail);
                        if (tail && *tail)
                                tail = address_of (*tail)->next;
                }
        }

        if (answer)
        {
                if (!append)
                        answer = shell_array_clear(name, name_length);
                for (exec_compound_held address_to one = first;
                     one && answer; one = one->next)
                        answer = shell_array_set(name, name_length, one->key,
                                                 one->key_length, one->value,
                                                 false);
        }

        shell_store_rewind(address_of exec_store, held);

        return answer;
}

static COLD bool exec_assignment_error(b32 fatal_status)
{
        if (fatal_status)
        {
                /* A regular prefix error in POSIX mode aborts the current
                   physical input program with one. A following physical line
                   still runs; assignment-only and direct-special errors use
                   Bash's terminal 127 path below. */
                if (fatal_status == EXEC_ASSIGNMENT_LINE_ABORT)
                {
                        exec_abort_line(1);
                        return false;
                }

                expand_fatal_status(fatal_status);
                return false;
        }

        /* Bash's ordinary mode diagnoses a rejected prefix assignment but
           still invokes the command with the old value. */
        shell_status = 1;
        return true;
}

/* Saved cells own their bytes and retain array tables across writes. An
   unset value remains distinct from an empty value or an unassigned declaration. */
typedef struct
{
        shell_binding binding;
        string_address key;
        positive key_length;
        bool promoted, detached;
} exec_kept_value;

/* Every assignment has already retained its target and evaluated an explicit
   or nameref subscript once. Keep the concrete key separate from shell syntax. */
static bool exec_assign_value(string_address word, positive name_length,
                              positive name_hash, bool append, bool compound,
                              exec_kept_value address_to target,
                              b32 assignment_error)
{
        string_address name_end = word + name_length;
        string_address mark = name_end + append;
        bool answer;

        if (arith_assign_stored)
        {
                arith_assign_stored = false;
                return true;
        }

        if (string_get(mark) != '=')
                return false;
        *name_end = end;

        if (compound)
        {
                positive length = string_length(mark + 1);
                if (env_assignment_readonly_hashed_span(word, name_length, name_hash))
                {
                        shell_readonly_refused(null, null, word,
                                               string_length(word));
                        *name_end = append ? '+' : '=';
                        return exec_assignment_error(assignment_error);
                }
                answer = shell_compound_assign(word, name_length, mark + 2,
                                                length > 2 ? length - 2 : 0, append);
        }
        else
        {
                string_address bracket = name_length && word[name_length - 1] == ']'
                    ? memory_first_of(word, '[', name_length) : null;
                positive base = bracket ? (positive)(bracket - word) : name_length;
                if (bracket)
                        *bracket = end;
                bool readonly = env_assignment_readonly_hashed_span(word, base,
                    bracket ? env_name_hash(word, base) : name_hash);
                if (readonly)
                        shell_readonly_refused(null, null, word,
                                               string_length(word));
                if (bracket)
                        *bracket = '[';
                if (readonly)
                {
                        *name_end = append ? '+' : '=';
                        return exec_assignment_error(assignment_error);
                }
                answer = target->key
                    ? shell_array_set(target->binding.name, target->binding.variable.name_length,
                        target->key, target->key_length, mark + 1, append)
                    : shell_scalar_assign(word, name_length, name_hash, mark + 1,
                                           append, false);
        }
        if (!answer && shell_reference_element(word,
                (positive)(string_first_of_or_end(word, '[') - word),
                null, null, null, null))
        {
                string_format(log_error, "%s: cannot assign\n", word);
                *name_end = append ? '+' : '=';
                return exec_assignment_error(assignment_error);
        }
        *name_end = append ? '+' : '=';
        return answer;
}

/* The name classification is stable and cached with a function definition;
   invocation policy is not. `enable`, Bash POSIX mode and the invocation
   personality can all change after a function is defined. */
static PURE p8 exec_special_kind(string_address name)
{
        static string_address names[] = {
            ":", ".", "break", "continue", "eval", "exec", "exit", "export",
            "readonly", "return", "set", "shift", "times", "trap", "unset",
        };
        positive which = string_table_find(name, names, sizeof(names[0]),
                                           array_count(names));

        if (which < array_count(names))
                return 1;

        if (word_is(name, "source"))
                return 2;

        /* Dash treats local as special; POSIX does not name it. */
        if (!shell_bash_compat && word_is(name, "local"))
                return 1;

        return 0;
}

static PURE bool exec_special_active(string_address name, p8 kind)
{
        if (!kind || shell_builtin_disabled(name) ||
            (shell_bash_compat && !shell_posix_on()))
                return false;

        return kind == 1 || shell_bash_compat;
}

/* The fifteen POSIX names, plus Bash's source spelling while Bash POSIX mode
   is active. Outside that mode Bash deliberately lets functions precede the
   names and restores their prefix assignments; the sh personality retains
   the POSIX policy it has always had. */
static PURE bool exec_special_builtin(string_address name)
{
        /* `time < file` is a timed null command: there is no argv name, and
           POSIX mode still asks this after a redirect fails. The empty
           command is not one of the fifteen. */
        if (!name || (shell_bash_compat && !shell_posix_on()))
                return false;
        return exec_special_active(name, exec_special_kind(name));
}

// Keep rare special-builtin error classification out of the large inlined
// command dispatcher. The existing nested error save/reset remains at the
// call site, and ordinary successful commands never probe the function table.
static COLD bool exec_special_error_fatal(string_address command,
                                          bool special, b32 status,
                                          bool error)
{
        if (!error)
                return false;
        if (!special)
                special = exec_special_builtin(command);
        return special && status;
}

static exec_kept_value address_to exec_promotable;
static b32 exec_promotable_count;

/* An explicit export/readonly on this simple command adopts its prefix
   value. A declaration in a nested function or eval does not adopt the
   caller's environment, so dispatch scopes this pointer, not the variables. */
static bool exec_assignment_promote(const_string name, positive length)
{
        bool found = false;

        if (!shell_bash_compat)
                return false;
        for (b32 at = 0; at < exec_promotable_count; at++)
                if (exec_promotable[at].binding.variable.name_length == length &&
                    !memory_compare(exec_promotable[at].binding.name, name, length))
                {
                        exec_promotable[at].promoted = true;
                        found = true;
                }
        return found;
}

static COLD bool exec_keep_element(exec_kept_value address_to kept,
                                   string_address base, positive base_length,
                                   string_address subscript,
                                   positive subscript_length)
{
        // Subscript arithmetic may replace the nameref that supplied base.
        base = shell_store_copy(address_of exec_store, base, base_length);
        if (!base)
                return false;
        positive key_length;
        string_address key = shell_expand_subscript(base, base_length,
            subscript, subscript_length, &key_length);
        if (!key || key_length == positive_max)
                return false;
        string_address value = shell_array_get(base, base_length, key, key_length, null);
        env_variable variable = {.name_length = base_length,
            .attributes = shell_array_attributes(base, base_length)};
        positive size = value ? string_length(value) : 0;
        variable.text = shell_store_take(&exec_store, base_length + size + 2);
        if (!variable.text)
                return false;
        memory_copy_end(variable.text, base, base_length);
        if (value)
        {
                variable.text[base_length] = '=';
                memory_copy_end(variable.text + base_length + 1, value, size);
                variable.value_length = size;
        }
        kept->binding = (shell_binding){base, variable};
        if (!shell_binding_hold(&kept->binding, key_length + 1))
                return false;
        kept->key = kept->binding.name + base_length + 1;
        kept->key_length = key_length;
        memory_copy_end(kept->key, key, key_length);
        return true;
}

enum { EXEC_KEEP_TARGET, EXEC_KEEP_PREFIX, EXEC_KEEP_CELL };
static bool exec_keep_value(exec_kept_value address_to kept, string_address word,
                             positive length, p8 mode)
{
        string_address bracket;

        kept->promoted = false;
        kept->detached = false;
        // Valid names carry a closing bracket only for an explicit subscript.
        bracket = length && word[length - 1] == ']' ?
            memory_first_of(word, '[', length) : null;

        // Resolve an explicit subscript once and retain that concrete key.
        if (bracket)
                return exec_keep_element(
                    kept, word, (positive)(bracket - word), bracket + 1,
                    length - (positive)(bracket - word) - 2);

        env_saved_state(address_of kept->binding, word, length);

        /* The common case stays the original one-probe save. A nameref alone
           takes the cold second probe needed to save the target that the
           provisional assignment will actually change. */
        if ((kept->binding.variable.attributes & SHELL_ARRAY_NAMEREF) && mode != EXEC_KEEP_CELL)
        {
                const_string resolved_name;
                const_string resolved_subscript;
                positive resolved_length;
                positive resolved_subscript_length;

                if (shell_reference_element(
                        word, length, address_of resolved_name,
                        address_of resolved_length,
                        address_of resolved_subscript,
                        address_of resolved_subscript_length))
                {
                        if (mode == EXEC_KEEP_PREFIX)
                                goto held;
                        return exec_keep_element(
                            kept, (string_address)resolved_name,
                            resolved_length,
                            (string_address)resolved_subscript,
                            resolved_subscript_length);
                }

                if (!shell_reference_resolve(
                        word, length, address_of resolved_name,
                        address_of resolved_length))
                        return false;

                word = (string_address)resolved_name;
                length = resolved_length;
                env_saved_state(address_of kept->binding, word, length);
        }

held:
        kept->key = null;
        kept->key_length = 0;
        return shell_binding_hold(&kept->binding, 0);
}

// Prefix expansion uses scalar cells. Append applies the original value
// attributes once; a later prefix then sees the accepted scalar bytes.
static bool exec_prefix_assign(exec_kept_value address_to kept,
                                string_address address_to word_at,
                                positive name_length, string_address value,
                                bool append, bool promote, b32 assignment_error)
{
        string_address word = *word_at;
        positive base = kept->binding.variable.name_length;
        positive hash = env_name_hash(kept->binding.name, base);
        p8 attributes = kept->binding.variable.attributes;

        if (!promote && (attributes & SHELL_ARRAY_READONLY))
        {
                /* Dash applies redirections before prefix assignments, so
                   `r=new true 2>/dev/null` leaves stderr empty. exec_simple
                   does that first; still say the sentence so a function
                   body with no redirect writes `x: is read only` the way
                   lima dash does. Bash diagnoses and keeps going. */
                shell_readonly_refused(null, null, kept->binding.name,
                                       string_length(kept->binding.name));
                return exec_assignment_error(assignment_error);
        }
        if (append)
        {
                string_address old = env_variable_value(&kept->binding.variable);
                if (attributes & SHELL_ARRAY_EITHER)
                        old = attributes & SHELL_ARRAY_INTEGER ? null
                            : attributes & SHELL_ARRAY_ASSOCIATIVE
                                ? shell_array_get(kept->binding.name, base, "0", 1, null) : old;
                value = env_append_value(old, value, attributes);
        }
        if (!value)
                return false;
        if ((append || promote) && (attributes & ENV_ATTRIBUTE_VALUE))
                value = env_attribute_value(attributes, value, !promote);
        if (!value)
                return false;
        positive length = string_length(value);
        if (name_length > positive_max - 2 || length > positive_max - name_length - 2)
                return false;
        string_address made = shell_store_take(address_of exec_store, name_length + length + 2);
        if (!made)
                return false;
        memory_copy(made, word, name_length);
        made[name_length] = '=';
        memory_copy_end(made + name_length + 1, value, length);
        *word_at = made;

        if (!promote)
        {
                bool written = env_value_restore(kept->binding.name, kept->binding.variable.name_length, value, 0, 0);
                if (written && (shell_options & SHELL_FLAG('a')))
                        env_export_restore(kept->binding.name, true);
                return written;
        }

        // Promotion has already evaluated the value. Keep array/nameref
        // routing, but suppress a second conversion at the storage boundary.
        p8 deferred = ENV_ATTRIBUTE_VALUE | SHELL_ARRAY_READONLY;
        positive found = env_find_hashed_span(kept->binding.name, base, hash);
        if (found < shell_var_count)
                shell_vars[found].attributes &= (p8)~deferred;
        bool answer = exec_assign_value(made, name_length,
            env_name_hash(made, name_length), false, false, kept, assignment_error);
        found = env_find_hashed_span(kept->binding.name, base, hash);
        if (found < shell_var_count)
                shell_vars[found].attributes |= attributes & deferred;
        return answer;
}

static COLD bool exec_put_back_attributes(exec_kept_value address_to kept)
{
        const_string name = kept->binding.name;
        positive length = kept->binding.variable.name_length;
        const_string resolved_name;
        positive resolved_length;
        p8 current;

        if (kept->key &&
            shell_reference_resolve(name, length, address_of resolved_name,
                                    address_of resolved_length))
        {
                name = resolved_name;
                length = resolved_length;
        }

        current = shell_variable_attributes(name, length);

        return current == kept->binding.variable.attributes ||
               shell_variable_attribute_set(
                   name, length, kept->binding.variable.attributes,
                   (p8)~kept->binding.variable.attributes);
}

static fn exec_put_back(exec_kept_value address_to kept, b32 count, bool restore)
{
        while (count--)
        {
                shell_binding address_to binding = &kept[count].binding;
                if (restore && !kept[count].promoted && !kept[count].detached)
                {
                        if (kept[count].key)
                        {
                                if (exec_put_back_attributes(kept + count))
                                {
                                        if (env_variable_value(&binding->variable))
                                                shell_array_set(binding->name, binding->variable.name_length,
                                                    kept[count].key, kept[count].key_length, env_variable_value(&binding->variable), false);
                                        else
                                                shell_array_forget(binding->name, binding->variable.name_length,
                                                    kept[count].key, kept[count].key_length);
                                }
                        }
                        else
                                shell_binding_restore(binding);
                }
                shell_binding_drop(binding);
        }
}

// Adopt only the final value for each promoted name. The command sees
// scalar cells; original attributes are applied once after its body returns.
static bool exec_finish_prefixes(exec_kept_value address_to kept, b32 count)
{
        exec_kept_value address_to adopted = null;
        b32 used = 0;
        bool answer = true;
        for (b32 at = 0; at < count; at++)
        {
                bool promote = kept[at].promoted && !kept[at].detached;
                kept[at].promoted = false;
                for (b32 next = at + 1; promote && next < count; next++)
                        if (kept[next].binding.variable.name_length == kept[at].binding.variable.name_length &&
                            !memory_compare(kept[next].binding.name, kept[at].binding.name, kept[at].binding.variable.name_length))
                        {
                                kept[next].promoted |= !kept[next].detached;
                                promote = false;
                        }
                if (!promote)
                        continue;
                if (!adopted)
                        adopted = (exec_kept_value address_to)shell_store_take(address_of exec_store,
                            (positive)count * sizeof(*adopted));
                if (!adopted || !exec_keep_value(adopted + used, kept[at].binding.name, kept[at].binding.variable.name_length, EXEC_KEEP_CELL))
                {
                        answer = false;
                        continue;
                }
                used++;
        }
        exec_put_back(kept, count, true);
        for (b32 at = 0; at < used; at++)
        {
                exec_kept_value target;
                exec_kept_value address_to source = adopted + at;
                if (!env_variable_value(&source->binding.variable) && !source->binding.variable.attributes && !source->binding.variable.permanent && !source->binding.variable.declared)
                {
                        env_unset_span(source->binding.name, source->binding.variable.name_length);
                        continue;
                }
                if (!exec_keep_value(&target, source->binding.name, source->binding.variable.name_length, EXEC_KEEP_TARGET))
                {
                        answer = false;
                        continue;
                }
                positive base = target.binding.variable.name_length;
                bool written;
                if (!env_variable_value(&source->binding.variable) || (source->binding.variable.attributes & SHELL_ARRAY_EITHER))
                {
                        p8 inherited = target.binding.variable.attributes;
                        if (source->binding.variable.attributes & SHELL_ARRAY_EITHER)
                        {
                                inherited &= (p8)~SHELL_ARRAY_EITHER;
                                if ((target.binding.variable.attributes & SHELL_ARRAY_READONLY) &&
                                    !(source->binding.variable.attributes & SHELL_ARRAY_READONLY))
                                        shell_readonly_refused(null, null,
                                            source->binding.name,
                                            string_length(source->binding.name));
                        }
                        written = env_value_restore(target.binding.name, base, env_variable_value(&source->binding.variable),
                            inherited | source->binding.variable.attributes, source->binding.variable.array);
                        source->binding.variable.array = 0;
                }
                else
                {
                        string_address word = source->binding.name;
                        written = exec_prefix_assign(&target, &word, source->binding.variable.name_length,
                                env_variable_value(&source->binding.variable) ? env_variable_value(&source->binding.variable) : (string_address)"",
                                false, true, 1);
                        if (written && source->binding.variable.attributes)
                                written = shell_variable_attribute_set(target.binding.name, base,
                                    source->binding.variable.attributes, 0);
                }
                if (written)
                {
                        if (source->binding.variable.declared)
                                written = env_declare(target.binding.name, base);
                        env_export_restore(target.binding.name, source->binding.variable.permanent);
                }
                answer &= written;
                exec_put_back(&target, 1, false);
        }
        exec_put_back(adopted, used, false);
        return answer;
}

static bool exec_declaration_name(b32 word)
{
        static string_address names[] = {
            "export", "readonly", "local", "declare", "typeset",
        };

        if (!(parse_word_flags[word] & PARSE_WORD_LITERAL))
                return false;

        return string_table_find(parse_words[word], names, sizeof(names[0]),
                                 array_count(names)) <
               array_count(names);
}

/*
        The assignment operands of a declaration utility use assignment
        expansion even though they follow the command name. POSIX Issue 8
        and dash treat command as a declaration utility when the name it
        invokes is one. Bash 5.2 without posix mode does not: words after
        command export or command local field-split like ordinary
        arguments. Walk literal command chains only where that wrapper
        still applies.
*/
static bool exec_declaration_compound(string_address word)
{
        // Raw compound operands retain their parse-word pointer; expanded
        // quoted parentheses have new storage and are ordinary scalar data.
        for (b32 at = 0; at < PARSE_WORDS; at++)
        {
                if (at == parse_word_used)
                        at = parse_word_top;
                if (at < PARSE_WORDS && parse_words[at] == word)
                        return (parse_word_flags[at] & PARSE_WORD_COMPOUND) != 0;
        }
        return false;
}

static PURE b32 exec_declaration_from(parse_node address_to node)
{
        b32 at = node->word;
        b32 stop = at + node->word_count;

        while (at < stop &&
               (parse_word_flags[at] & PARSE_WORD_ASSIGNMENT))
                at++;

        while (at < stop)
        {
                if (exec_declaration_name(at))
                        return at + 1;

                if (!(parse_word_flags[at] & PARSE_WORD_LITERAL) ||
                    !word_is(parse_words[at], "command"))
                        return stop;

                if (shell_bash_compat && !shell_posix_on())
                        return stop;

                at++;

                while (at < stop)
                {
                        string_address option;

                        if (!(parse_word_flags[at] & PARSE_WORD_LITERAL))
                                return stop;

                        option = parse_words[at];

                        if (word_is(option, "--"))
                        {
                                at++;
                                break;
                        }

                        if (string_not(option, '-') || !string_get(option + 1))
                                break;

                        option++;

                        while (string_get(option))
                        {
                                if (string_get(option) != 'p')
                                        return stop;

                                option++;
                        }

                        at++;
                }
        }

        return stop;
}

/*
        Control operands share the checked library scanner: signs, whitespace
        and overflow are parsed once. Dash then bounds the value to INT_MAX;
        Bash return accepts a signed machine word before truncating to status.
*/
static bool exec_control_integer(string_address word, bipolar address_to answer)
{
        // The common status/loop count is one digit: settle it in registers
        // before entering the shared sign/whitespace/overflow scanner.
        positive digit = (positive)(word[0] - '0');
        if (digit <= 9 && !word[1])
        {
                *answer = (bipolar)digit;
                return true;
        }
        string_address stopped;
        b32 overflow;
        bipolar parsed = string_to_number_checked(word, address_of stopped, 10,
                                                   address_of overflow);

        if (overflow || stopped == word)
                return false;
        stopped += string_span(stopped, string_set_space);
        if (*stopped)
                return false;

        *answer = parsed;
        return true;
}

static bool exec_control_number(string_address word, bool allow_zero,
                                b32 address_to answer)
{
        bipolar parsed;

        if (!exec_control_integer(word, address_of parsed) || parsed < 0 ||
            parsed > 0x7fffffff || (!allow_zero && !parsed))
                return false;
        *answer = (b32)parsed;
        return true;
}

static b32 exec_trap_status;

static COLD fn exec_return_bash()
{
        positive first = 1;
        bipolar value = trap_inside ? exec_trap_status : shell_status;
        bool valid = true;

        if (first < shell_argc && word_is(shell_argv[first], "--"))
                first++;
        if (first < shell_argc)
        {
                valid = exec_control_integer(shell_argv[first], address_of value);
                if (!valid)
                {
                        shell_told("return: %s: numeric argument required\n",
                            shell_argv[first]);
                        value = 2;
                }
                else if (shell_argc > first + 1)
                {
                        shell_diagnostic_where();
                        log_error("return: too many arguments\n", 0);
                        if (shell_bash_compat)
                        {
                                expand_discard_whole(2);
                                return;
                        }
                        shell_status = 1;
                        if (!shell_is_interactive || exec_forked ||
                            string_is(shell_option_flags, 'c'))
                                exec_child_leave(1);
                        exec_abort_line(shell_status);
                        return;
                }
        }

        if (!exec_function_depth && !shell_source_depth)
        {
                //      Bash says it cannot return from here, and quotes the
                //      name while it does; under posix the refusal is
                //      fatal, because return is a special builtin -- unless
                //      the command is tested. Bang inverts `! return 3` to
                //      zero and the script continues; if and && / || are
                //      the same question.
                shell_diagnostic_where();
                log_error("return: can only `return' from a function or "
                          "sourced script\n", 0);
                shell_status = 2;

                if (shell_posix_on() && !exec_tested)
                        exec_child_leave(2);

                return;
        }

        shell_status = (b32)((positive)value & 0xff);
        exec_signal = EXEC_SIGNAL_RETURN;
}

/* break, continue and return are executor operations, not ordinary C
   builtins: their result has to unwind the surrounding parse tree. Query mode
   exposes that same namespace to command/type without copying the name list. */
bool exec_control_builtin(string_address name, bool run)
{
        p8 initial = string_get(name);

        if ((initial == 'b' && !string_compare(name, "break")) ||
            (initial == 'c' && !string_compare(name, "continue")))
        {
                b32 levels = 1;

                if (!run)
                        return true;

                //      Bash reads the loop depth before it reads the count,
                //      so a word that is no number at all goes unmentioned
                //      when there was no loop to leave. It answers zero
                //      either way, and only says why outside posix mode.
                if (shell_bash_compat && !exec_loop_depth)
                {
                        if (!shell_posix_on())
                        {
                                shell_told("%s: only meaningful in a "
                                    "`for', `while', or `until' "
                                    "loop\n", name);
                        }
                        shell_status = 0;
                        return true;
                }

                positive first = 1;

                //      bash takes an end of options, and one count at most:
                //      a second word ends the shell with 1.
                if (shell_bash_compat && first < shell_argc &&
                    word_is(shell_argv[first], "--"))
                        first++;
                if (shell_bash_compat && shell_argc > first + 1)
                {
                        shell_told("%s: too many arguments\n", name);
                        expand_discard_whole(2);
                        return true;
                }

                if (shell_argc > first &&
                    !exec_control_number(shell_argv[first], false,
                                         address_of levels))
                {
                        bipolar counted = 0;
                        bool numeric = shell_bash_compat &&
                                       exec_control_integer(shell_argv[first],
                                                            address_of counted);

                        //      A count past the field is still a count, and
                        //      bash leaves every loop it holds for it. Only
                        //      zero and below are out of range, and those
                        //      leave every loop too, saying so and failing.
                        if (numeric && counted > 0)
                                levels = (b32)exec_loop_depth;
                        else if (numeric)
                        {
                                shell_told("%s: %s: loop count out of "
                                    "range\n", name, shell_argv[first]);
                                exec_signal = EXEC_SIGNAL_BREAK;
                                exec_signal_level = (b32)exec_loop_depth;
                                shell_status = 1;
                                return true;
                        }
                        else
                        {
                                shell_told(shell_bash_compat
                                    ? "%s: %s: numeric "
                                      "argument required\n"
                                    : "%s: Illegal number: "
                                      "%s\n",
                                    name, shell_argv[first]);
                                /* bash leaves on a count that is no number,
                                   command and eval or not; carrying on went
                                   round `while :; do break oops; done` for
                                   ever. For dash it is a special-builtin
                                   error. Aborting the line here made
                                   `command continue bad` fatal, and let eval
                                   of `break bad` return so the next -c line
                                   still printed end=. */
                                if (shell_bash_compat)
                                {
                                        expand_fatal_status(2);
                                        return true;
                                }
                                shell_status = 2;
                                exec_special_error_note();
                                return true;
                        }
                }

                if (exec_loop_depth)
                {
                        if (levels > exec_loop_depth)
                                levels = exec_loop_depth;

                        exec_signal = initial == 'b'
                                          ? EXEC_SIGNAL_BREAK
                                          : EXEC_SIGNAL_CONTINUE;
                        exec_signal_level = levels;
                }

                shell_status = 0;
                return true;
        }

        if (initial != 'r' || string_compare(name, "return"))
                return false;

        if (!run)
                return true;

        exec_return_previous = shell_status;
        if (shell_bash_compat)
        {
                exec_return_bash();
                return true;
        }
        if (shell_argc <= 1 && trap_inside)
                shell_status = exec_trap_status;
        if (shell_argc > 1 &&
            !exec_control_number(shell_argv[1], true, address_of shell_status))
        {
                //      Dash return is a special builtin. A non-integer is
                //      fatal, including a value past INT_MAX. Aborting only
                //      the line let the next -c line print end=2.
                shell_told("return: Illegal number: %s\n", shell_argv[1]);
                shell_status = 2;
                exec_special_error_note();
                return true;
        }

        //      dash returns from the top level without a word, and the
        //      script ends there with the status it was handed, the way
        //      exit would have ended it.
        if (!exec_function_depth && !shell_source_depth)
                exec_child_leave(shell_status);

        exec_signal = EXEC_SIGNAL_RETURN;
        return true;
}

static b32 exec_dispatch(b32 command_word)
{
        /* PATH answers are transient only until exec/spawn has copied argv.
           Keep one movable room across commands instead of mapping and
           unmapping it for every external command in a loop.  No parser or
           expansion pointer refers into this room, and dispatch does not
           re-enter while shell_execute_command waits for the child. */
        static p8 address_to found;
        static positive found_room;
        string_address name = shell_argv[0];
        positive2 named;
        p8 initial = string_get(name);
        positive slot;
        bool disabled = shell_builtin_disabled(name);
        bool colon = initial == ':' && !string_get(name + 1);
        // What `:`, true and false answer, or -1 for every other name.
        b32 fixed = disabled ? -1
                    : colon || (initial == 't' && word_is(name, "true")) ? 0
                    : initial == 'f' && word_is(name, "false") ? 1 : -1;

        /* `:` is a special builtin, so POSIX finds it before a function.
           true and false are ordinary: a function of that name still wins.
           With no function at all, none of the three needs a name hash. */
        if (fixed >= 0 && (!exec_function_count ||
                           (colon && (!shell_bash_compat || shell_posix_on()))))
                return shell_status = fixed;

        shell_command_name_stable =
            parse_words[command_word] == name &&
            (parse_word_flags[command_word] & PARSE_WORD_LITERAL);

        if (shell_command_name_stable)
        {
                /* Leading assignments have already been consumed, so this
                   literal word's otherwise-unused assignment-name hash slot
                   can cache the complete command hash for every later pass
                   through a kept loop tree. */
                named.x = parse_word_name_hashes[command_word];
                named.y = parse_word_lengths[command_word];

                if (!named.x)
                {
                        named.x = memory_hash_33(name, named.y);
                        parse_word_name_hashes[command_word] = named.x;
                }

                shell_command_name_address = name;
        }
        else
                named = string_hash_33_length(name);

        slot = exec_function_slot(name, named);

        if (slot != positive_max)
        {
                /* POSIX special builtins precede a same-named function.
                   Ordinary Bash reverses that order. The name classification
                   was made once at definition, while the active policy is
                   checked now because set -o posix and enable can change. */
                if (exec_special_active(
                        name, exec_functions[slot].special_kind))
                {
                        if (exec_control_builtin(name, true))
                                return shell_status;

                        {
                                bool tail = shell_tail_command;

                                if (shell_builtin(null, named))
                                        return shell_status;
                                shell_tail_command = tail;
                        }
                }

                shell_tail_command = false;
                return exec_call(slot);
        }

        if (fixed >= 0)
                return shell_status = fixed;

        if (!disabled && exec_control_builtin(name, true))
                return shell_status;

        {
                bool tail = shell_tail_command;

                if (shell_builtin(null, named))
                        return shell_status;
                shell_tail_command = tail;
        }

        //      A builtin or function never carries a slash, so this is the
        //      last moment before an external command name becomes a path.
        if (!shell_command_path_allowed(name, true))
        {
                shell_status = 1;
                return shell_status;
        }

        {
                bipolar located = shell_find_in_path_alloc(name,
                                                            address_of found,
                                                            address_of found_room);

                if (located < 0)
                {
                        shell_status = 2;
                        string_format(log_error, "%s: no room\n", name);
                        return shell_status;
                }

                /* A file of that name that is not executable. bash tries
                   it and reports what execve said about the path, 126;
                   dash reports the name, and its search ends at 127. */
                if (located == 2)
                {
                        file_facts facts;
                        bool slash = string_first_of(name, '/') != null;
                        bool directory =
                            test_facts(found, address_of facts, true) &&
                            (facts.mode & MODE_FORMAT) == MODE_DIRECTORY;

                        shell_status = shell_bash_compat || slash ? 126 : 127;
                        shell_diagnostic_where();
                        string_format(log_error, "%s: %s\n",
                                      shell_bash_compat ? (string_address)found
                                                        : name,
                                      shell_bash_compat && directory
                                          ? (string_address) "Is a directory"
                                          : (string_address) "Permission denied");
                        return shell_status;
                }

                if (located == 1)
                {
                        //      A monitored external skips the Spark
                        //      preflight and forks directly.
                        shell_execute_found(found, name, exec_asynchronous,
                                            job_monitor());
                        return shell_status;
                }
        }

        if (exec_not_found_handled())
                return shell_status;

        shell_status = 127;
        shell_diagnostic_where();

        //      The commonest line either shell writes, and the two houses
        //      do not write it the same: bash says the command was not
        //      found, dash says only that it was not.
        string_format(log_error,
                      !shell_bash_compat ? "%s: not found\n"
                      : string_first_of(name, '/')
                          ? "%s: No such file or directory\n"
                          : "%s: command not found\n", name);

        return shell_status;
}

/*
        The traps that arrived, run.

        Between commands and nowhere else. The action is a line, so it goes
        through the parser, and the parser is only free once the command it
        interrupted has finished -- which is also the moment POSIX names.

        What the action leaves behind is put back: a trap does not change the
        status the interrupted command answered with, and a return or a break
        inside one belongs to the action and not to the loop it landed in.
*/
/* exec_trap_status: the status a return with no operand gives inside a trap
   action, the one the interrupted command left, not the action's own last. */
fn exec_traps()
{
        b32 kept_status = shell_status;
        b32 kept_signal = exec_signal;
        b32 kept_level = exec_signal_level;
        bool kept_tested = exec_tested;
        bool action_fatal = false;
        bipolar number;

        if (exec_line_aborted() || !trap_waiting())
                return;

        trap_entered(true);

        while ((number = trap_taken()) >= 0)
        {
                string_address action = trap_action((positive)number);

                if (!action || !string_get(action))
                        continue;

                exec_signal = EXEC_SIGNAL_NONE;
                exec_tested = false;
                exec_trap_status = kept_status;
                // An action is source, however many lines of it there are,
                // and what it leaves unfinished is its own syntax error --
                // the same two calls eval makes. One line at a time used to
                // be one line only, and the second command of an action was
                // never run.
                // A signal's action counts its lines from one, as both
                // references do.
                exec_run_nested(action, false, 0);

                if (exec_line_aborted())
                {
                        action_fatal = true;
                        break;
                }

                /* A return in the action returns from the function the
                   trap interrupted, in bash and dash both: trap 'return 5'
                   USR1 inside f leaves f with 5. It was taken back, so f
                   ran on, and a function spinning in a loop never left. */
                if (exec_signal == EXEC_SIGNAL_RETURN &&
                    (exec_function_depth || shell_source_depth))
                {
                        trap_entered(false);
                        exec_tested = kept_tested;
                        return;
                }
        }

        trap_entered(false);

        if (action_fatal)
        {
                shell_status = 2;
                exec_signal = EXEC_SIGNAL_FATAL;
                exec_signal_level = 0;
                exec_tested = kept_tested;
                return;
        }

        shell_status = kept_status;
        exec_signal = kept_signal;
        exec_signal_level = kept_level;
        exec_tested = kept_tested;
}

typedef struct exec_declaration_frame
{
        struct exec_declaration_frame address_to previous;
        exec_kept_value address_to kept;
        b32 count;
        positive depth;
        positive locals;
} exec_declaration_frame;

static exec_declaration_frame address_to exec_declaration_frames;

/* -g chooses an owned payload below the temporary environment. Its writes
   never replace the visible lookup cell used by arithmetic evaluation. */
static env_variable address_to exec_declare_global_destination(string_address name,
    positive length, shell_declare_state address_to state, string_address value,
    bool address_to address_to promoted)
{
        exec_kept_value address_to global = null;
        if (!(state->attributes_set & SHELL_ARRAY_NAMEREF))
        {
                const_string target;
                positive target_length;
                if (shell_reference_resolve(name, length, &target, &target_length))
                {
                        name = (string_address)target;
                        length = target_length;
                }
        }
        for (exec_declaration_frame address_to frame = exec_declaration_frames;
             frame; frame = frame->previous)
                if (!frame->depth)
                        for (b32 at = frame->count; at-- > 0;)
                                if (!frame->kept[at].detached && !frame->kept[at].key &&
                                    frame->kept[at].binding.variable.name_length == length &&
                                    !memory_compare(frame->kept[at].binding.name, name, length))
                                        global = frame->kept + at;
        if (!global)
                return null;
        p8 attributes = (global->binding.variable.attributes & (p8)~state->attributes_clear) |
                        state->attributes_set;
        bool kind_error = (global->binding.variable.attributes & SHELL_ARRAY_EITHER) &&
            ((state->attributes_set & SHELL_ARRAY_NAMEREF) ||
             ((state->attributes_set & SHELL_ARRAY_EITHER) &&
              (state->attributes_set & SHELL_ARRAY_EITHER) !=
                  (global->binding.variable.attributes & SHELL_ARRAY_EITHER)));
        if (!kind_error && value && (attributes & SHELL_ARRAY_EITHER) && string_is(value, '('))
                return null;
        *promoted = &global->promoted;
        return &global->binding.variable;
}

// Removing a temporary binding exposes its saved context immediately.
static b32 exec_unset_prefix(const_string name, positive length)
{
        if (!shell_bash_compat)
                return 0;
        for (exec_declaration_frame address_to frame = exec_declaration_frames;
             frame; frame = frame->previous)
        {
                exec_kept_value address_to saved = null;
                for (b32 at = 0; at < frame->count; at++)
                        if (!frame->kept[at].detached && !frame->kept[at].key &&
                            frame->kept[at].binding.variable.name_length == length &&
                            !memory_compare(frame->kept[at].binding.name, name, length))
                        {
                                saved = frame->kept + at;
                                break;
                        }
                if (!saved)
                        continue;
                if (saved->promoted)
                        return 0;
                positive stop = local_count;
                for (positive depth = local_depth; depth; depth--)
                {
                        positive begin = local_from[depth - 1];
                        for (positive at = begin; at < stop; at++)
                                if (at >= frame->locals && !local_table[at].detached &&
                                    local_table[at].binding.variable.name_length == length &&
                                    !memory_compare(local_table[at].binding.name, name, length) &&
                                    (depth == local_depth || depth > frame->depth + 1))
                                        return 0;
                        stop = begin;
                }
                b32 array = array_table_hold(saved->binding.variable.array);
                if (array < 0 || !env_value_restore(saved->binding.name, length,
                    env_variable_value(&saved->binding.variable), saved->binding.variable.attributes, array))
                        return -1;
                name = saved->binding.name;
                env_declare_restore(saved->binding.name, saved->binding.variable.declared);
                env_export_restore(saved->binding.name, saved->binding.variable.permanent);
                for (positive at = frame->locals; at < local_count; at++)
                        if (local_table[at].binding.variable.name_length == length &&
                            !memory_compare(local_table[at].binding.name, name, length))
                                local_table[at].detached = true;
                for (b32 at = 0; at < frame->count; at++)
                        if (frame->kept[at].binding.variable.name_length == length &&
                            !memory_compare(frame->kept[at].binding.name, name, length))
                                frame->kept[at].detached = true;
                return 1;
        }
        return 0;
}

static COLD b32 exec_dispatch_scoped(b32 command_word,
                                     exec_kept_value address_to kept,
                                     b32 count)
{
        exec_kept_value address_to previous = exec_promotable;
        b32 previous_count = exec_promotable_count;
        b32 status;
        exec_declaration_frame frame = {
            exec_declaration_frames, kept, count, local_depth, local_count};

        exec_declaration_frames = &frame;
        exec_promotable = kept;
        exec_promotable_count = count;
        status = exec_dispatch(command_word);
        exec_promotable = previous;
        exec_promotable_count = previous_count;
        exec_declaration_frames = frame.previous;
        return status;
}

/* Keyword assignments use the same leading-assignment executor. Partition
   indices, not parser-owned words: a cached function can run with -k both on
   and off, and recursive execution must never rewrite its parse tree. */
static COLD b32 address_to exec_keyword_order(parse_node address_to node,
                                              b32 address_to leading)
{
        b32 assignments = 0;
        b32 address_to order;

        for (b32 at = 0; at < node->word_count; at++)
                assignments += (parse_word_flags[node->word + at] &
                                  PARSE_WORD_ASSIGNMENT) != 0;
        if (assignments == *leading)
                return null;

        order = (b32 address_to)shell_store_take(
            address_of exec_store, (positive)node->word_count * sizeof(*order));
        if (!order)
        {
                *leading = -1;
                return null;
        }

        b32 next_assignment = 0;
        b32 next_argument = assignments;
        for (b32 at = 0; at < node->word_count; at++)
        {
                b32 word = node->word + at;
                order[(parse_word_flags[word] & PARSE_WORD_ASSIGNMENT)
                          ? next_assignment++ : next_argument++] = word;
        }
        *leading = assignments;
        return order;
}

static PURE b32 exec_assignment_error_status(bool assignments_only,
                                               string_address command)
{
        if (!shell_bash_compat)
                return 2;

        //      A line of its own: bash gives up the rest of the command
        //      list it was written on and reads the next line, where this
        //      used to end the whole script.
        if (!shell_posix_on())
                return assignments_only ? EXEC_ASSIGNMENT_LINE_ABORT : 0;

        if (assignments_only || exec_special_builtin(command))
                return string_is(shell_option_flags, 'c') ? 127 : 1;

        return EXEC_ASSIGNMENT_LINE_ABORT;
}

static b32 exec_simple(b32 index)
{
        parse_node address_to node = parse_nodes + index;
        exec_kept_value address_to kept = null;
        exec_kept_value address_to expanded_kept = null;
        shell_mark arena_mark = shell_store_mark(address_of exec_store);
        b32 kept_count = 0;
        b32 expanded_count = 0;
        b32 mark = exec_save_count;
        b32 count = 0;
        b32 first = 0;
        b32 declaration_from = -1;
        bool compound_operand = false;
        b32 leading = 0;
        b32 address_to word_order = null;
        b32 status;
        b32 at;
        string_address command = null;
        bool bare_exec;
        bool assignments_only;
        bool special = false;
        bool fatal = false;
        bool redirects_applied = false;
        shell_words arguments;

        //      argv grows with the line. A command's words are whatever the
        //      expansions made of them, and a directory may hold any number of
        //      names, so there is nothing sensible to clamp this to.
        shell_words_bind(address_of arguments, address_of shell_argv,
                         address_of shell_argv_room);

        /* A previous word's failglob must not make this command's first
           literal word look like a failed expansion. expand_begin clears
           the flag, but a literal name never goes through it. */
        expand_failed = false;

        // The line this command was written on, which caller and $LINENO
        // answer with for as long as it runs.
        exec_line = node->line;

        /* A command made only of assignments is reported by bash at the line
           it ends on, not the one it starts on: x=${y?<newline>unset} says
           line 3 where echo ${y?<newline>unset} says line 2. The newlines
           inside its words are what separate the two. */
        {
                b32 words = node->word_count;
                b32 at = 0;

                while (at < words &&
                       (parse_word_flags[node->word + at] & PARSE_WORD_ASSIGNMENT))
                        at++;
                if (words && at == words)
                        for (at = 0; at < words; at++)
                                if (parse_word_flags[node->word + at] &
                                    PARSE_WORD_NEWLINE)
                                        exec_line += (b32)memory_count(
                                            parse_words[node->word + at],
                                            parse_word_lengths[node->word + at],
                                            '\n');
        }
        exec_wait_node = index;
        token_used = 0;
        token_overflow = false;
        // With no command name, POSIX makes the command's status that of the
        // last command substitution it performed. Each substitution updates
        // this while the words and redirect targets below are expanded.
        shell_substitution_status = 0;

        /* Expand command arguments before assignment right-hand sides. The
           reserved argv prefix is filled afterwards; provisional assignments
           still run left to right so `a=one b=$a` sees the preceding value. */
        while (leading < node->word_count &&
               (parse_word_flags[node->word + leading] &
                PARSE_WORD_ASSIGNMENT))
        {
                /*
                        dash has no subscripted assignment, so `a[1]=x` is
                        not a failed assignment to it -- it is not an
                        assignment at all. The whole word is a command name
                        and dash reports it not found, with or without a
                        command after it. Under a dash name the leading run
                        ends here rather than the word being taken for an
                        assignment that then turns out not to name anything.
                */
                if (!shell_bash_compat &&
                    parse_words[node->word + leading][
                        parse_word_name_lengths[node->word + leading] - 1] == ']')
                        break;

                leading++;
        }

        if (shell_keyword_on())
                word_order = exec_keyword_order(node, address_of leading);

        if (leading < 0)
        {
                status = 2;
                goto fail;
        }

#define EXEC_WORD(at) (word_order ? word_order[(at)] : node->word + (at))

        /* Literal words are the parse-time identity: no expand_begin, no
           field split, no assignment capture. Prefix assignments, keyword
           reordering and redirections keep the general walk. */
        if (!leading && !word_order && node->word_count)
        {
                b32 words = node->word_count;
                b32 word = node->word;
                b32 step = 0;

                while (step < words &&
                       (parse_word_flags[word + step] & PARSE_WORD_LITERAL))
                        step++;

                if (step == words)
                {
                        if (!shell_array_room(shell_argv, shell_argv_room,
                                              (positive)words + 2))
                        {
                                status = 2;
                                goto fail;
                        }

                        memory_copy(shell_argv, parse_words + word,
                                    words * sizeof(*parse_words));

                        count = words;
                        first = 0;
                        goto argv_ready;
                }
        }

        if (leading)
        {
                expanded_kept = (exec_kept_value address_to)shell_store_take(
                    address_of exec_store,
                    (positive)leading * sizeof(expanded_kept[0]));

                if (!expanded_kept ||
                    !shell_array_room(shell_argv, shell_argv_room,
                                      (positive)leading + 2))
                {
                        status = 2;
                        goto fail;
                }
                arguments.count = (positive)leading;
                count = first = leading;
        }

        for (at = leading; at < node->word_count; at++)
        {
                b32 word_index = EXEC_WORD(at);
                string_address word = parse_words[word_index];
                p8 word_flags = parse_word_flags[word_index];
                bool literal = word_flags & PARSE_WORD_LITERAL;
                bool assignment = word_flags & PARSE_WORD_ASSIGNMENT;

                /*
                        Declaration operands are expanded whole; the ordinary
                        argument expansion must not split their right sides.
                */
                if (assignment)
                {
                        compound_operand |=
                            (word_flags & PARSE_WORD_COMPOUND) != 0;
                        if (declaration_from < 0)
                                declaration_from = exec_declaration_from(node);

                        if (word_index >= declaration_from)
                        {
                                positive value_at =
                                    parse_word_name_lengths[word_index] + 1 +
                                    ((word_flags & PARSE_WORD_APPEND) != 0);

                                if (!shell_words_add(
                                        address_of arguments,
                                        (literal ||
                                         (word_flags & PARSE_WORD_COMPOUND))
                                            ? word
                                            : shell_expand_assignment(word,
                                                                      value_at)))
                                        break;

                                count = (b32)arguments.count;

                                if (exec_line_aborted())
                                        break;

                                continue;
                        }
                }

                if (literal)
                {
                        if (!shell_words_add(address_of arguments, word))
                                break;

                        count = (b32)arguments.count;
                }
                else if (expand_simple_dollar_word(word, address_of arguments))
                        count = (b32)arguments.count;
                else
                        count = (b32)shell_expand_fields(word,
                                                         address_of arguments);

                if (expand_failed && !exec_line_aborted())
                {
                        status = shell_status ? shell_status : 1;
                        goto fail;
                }

                if (exec_line_aborted())
                        break;
        }

        if (at != node->word_count && !exec_line_aborted())
        {
                status = 2;
                goto fail;
        }

        assignments_only = first == count;
        /* Dash opens redirections before prefix assignments, so a readonly
           prefix diagnoses into `2>/dev/null` and a function body with no
           redirect still writes the sentence. */
        if (!shell_bash_compat && leading && node->redirect_count &&
            !exec_line_aborted())
        {
                if (!exec_redirect_apply(index))
                {
                        exec_redirect_restore(mark);
                        status = (exec_line_aborted() ? shell_status :
                                  exec_redirect_status ? exec_redirect_status
                                                       : 1);
                        if (!exec_line_aborted())
                        {
                                if (first != count)
                                        special = exec_special_builtin(
                                            shell_argv[first]);
                                fatal = special;
                        }
                        goto fail;
                }
                redirects_applied = true;
        }
        for (at = 0; at < leading && !exec_line_aborted(); at++)
        {
                b32 word_index = EXEC_WORD(at);
                p8 flags = parse_word_flags[word_index];
                string_address word = parse_words[word_index];
                if (!assignments_only &&
                    word[parse_word_name_lengths[word_index] - 1] == ']')
                {
                        shell_argv[at] = word;
                        /*
                                string_format knows %s %p %b %f and %%, and
                                drops anything else with its percent -- so
                                "%.*s" printed the tail of the format itself,
                                "*s: not a valid identifier", and the name
                                never appeared. The name is cut into a buffer
                                and written whole, quoted the way Bash quotes
                                it.

                                dash does not come here at all: it takes
                                a[1]=x for a command and reports it not
                                found, which is a control-flow difference
                                rather than a wording one and belongs to
                                whoever is holding the shell domain.
                        */
                        {
                                positive named = parse_word_name_lengths[word_index];
                                p8 shown[FILE_NAME_MAX];
                                positive kept = min(named, (positive)FILE_NAME_MAX - 1);

                                memory_copy_apart(shown, (address_any)word, kept);
                                shown[kept] = 0;
                                string_format(log_error,
                                    "`%s': not a valid identifier\n", shown);
                        }
                        if (!exec_assignment_error(exec_assignment_error_status(false, shell_argv[first])))
                                break;
                        continue;
                }
                positive substitution_generation =
                    shell_substitution_generation;
                positive value_at = parse_word_name_lengths[word_index] + 1 +
                                      ((flags & PARSE_WORD_APPEND) != 0);
                bool held_commit = expand_assignment_commit;
                string_address trial;

                arith_assign_stored = false;
                expand_assignment_commit =
                    assignments_only && !(flags & PARSE_WORD_APPEND) &&
                    !(flags & PARSE_WORD_COMPOUND);
                trial = (flags & PARSE_WORD_LITERAL) ||
                    (assignments_only && (flags & PARSE_WORD_COMPOUND))
                        ? word : shell_expand_assignment(word, value_at);
                expand_assignment_commit = held_commit;

                /* Bash's ordinary mode exposes each substitution answer to
                   the next assignment RHS. POSIX freezes the status from
                   before the simple command until all RHS expansions finish;
                   both modes still return the last substitution below. */
                if (shell_bash_compat && !shell_posix_on() &&
                    substitution_generation != shell_substitution_generation)
                        shell_status = shell_substitution_status;

                if (exec_line_aborted())
                        break;
                if (arith_assign_stored)
                {
                        arith_assign_stored = false;
                        shell_argv[at] = trial;
                        continue;
                }
                /* A command that is one plain assignment and nothing else has
                   nothing after it that could fail and undo it, so there is no
                   old value to copy aside and release again. Its variable is
                   still looked up, since a nameref needs the kept path to find
                   the target it would change; a subscript or a compound value
                   takes it too. */
                if (trial && assignments_only && leading == 1 &&
                    !(flags & PARSE_WORD_COMPOUND) &&
                    trial[parse_word_name_lengths[word_index] - 1] != ']')
                {
                        exec_kept_value plain = {0};

                        env_saved_state(address_of plain.binding, trial,
                                        parse_word_name_lengths[word_index]);
                        if (!(plain.binding.variable.attributes &
                              SHELL_ARRAY_NAMEREF))
                        {
                                shell_argv[at] = trial;
                                if (!exec_assign_value(
                                        trial, parse_word_name_lengths[word_index],
                                        parse_word_name_hashes[word_index],
                                        (flags & PARSE_WORD_APPEND) != 0, false,
                                        address_of plain,
                                        exec_assignment_error_status(true, null)))
                                {
                                        status = shell_bash_compat ? 1 : 2;
                                        goto fail;
                                }
                                continue;
                        }
                }
                if (!trial ||
                    !exec_keep_value(expanded_kept + expanded_count, trial,
                        parse_word_name_lengths[word_index], assignments_only ? EXEC_KEEP_TARGET : EXEC_KEEP_PREFIX))
                {
                        status = 2;
                        goto fail;
                }
                expanded_count++;
                shell_argv[at] = trial;
                exec_kept_value address_to saved = expanded_kept + expanded_count - 1;
                b32 error = exec_assignment_error_status(assignments_only,
                    assignments_only ? null : shell_argv[first]);
                bool accepted = !assignments_only && !saved->key
                    ? exec_prefix_assign(saved, &trial, parse_word_name_lengths[word_index],
                        trial + value_at, (flags & PARSE_WORD_APPEND) != 0, false, error)
                    : exec_assign_value(trial, parse_word_name_lengths[word_index],
                        parse_word_name_hashes[word_index], (flags & PARSE_WORD_APPEND) != 0,
                        (flags & PARSE_WORD_COMPOUND) != 0, saved, error);
                if (!accepted)
                {
                        status = shell_bash_compat ? 1 : 2;
                        goto fail;
                }
                if (!assignments_only)
                        shell_argv[at] = trial;
        }

        if (exec_line_aborted())
        {
                status = shell_status;
                goto fail;
        }

        if (assignments_only)
                exec_put_back(expanded_kept, expanded_count, false);
        else
        {
                kept = expanded_kept;
                kept_count = expanded_count;
        }
        expanded_count = 0;

        //      An empty command line never entered the loop, so the table
        //      may not exist yet to hold even the null that ends it.
        if (!count &&
            !shell_array_room(shell_argv, shell_argv_room, 2))
        {
                status = 2;
                goto fail;
        }

        argv_ready:
        shell_argv[count] = null;
        shell_argc = count;

        /*
                An assignment in front of a command covers that command only.

                It has to be visible to what runs -- a spawned program reads
                it out of the environment and a builtin reads it out of the
                same table -- so it is made and then unmade, rather than being
                handed over as an environment of its own. The exception is a
                special builtin, in front of which POSIX says the assignment
                stays; and assignments with no command after them are the
                command, so they stay too.
        */
        if (first != count)
                command = shell_argv[first];

        if (first && first != count)
                special = exec_special_builtin(command);

        for (at = 0; at < kept_count; at++)
        {
                kept[at].promoted = special;
                // A rejected readonly prefix does not export its old value.
                if (!(kept[at].binding.variable.attributes & SHELL_ARRAY_READONLY) &&
                    !kept[at].key && (!special || shell_bash_compat ||
                        (count - first > 1 && word_is(command, "exec"))))
                        env_export_restore(kept[at].binding.name, true);
        }

        if (exec_trace_on())
                exec_trace(count, first);

        if (node->redirect_count && !redirects_applied)
        {
                if (!exec_redirect_apply(index))
                {
                        exec_redirect_restore(mark);
                        status = (exec_line_aborted() ? shell_status : exec_redirect_status ? exec_redirect_status : 1);
                        // An expansion error already selected its reader boundary.
                        // Do not turn a recoverable Bash substring failure into a
                        // POSIX special-builtin redirection failure that exits.
                        if (!exec_line_aborted())
                        {
                                if (!special)
                                        special = exec_special_builtin(command);
                                fatal = special;
                        }
                        goto fail;
                }

                redirects_applied = true;
        }

        if (first == count)
        {
                if (node->redirect_count)
                        exec_redirect_restore(mark);
                shell_status = shell_substitution_status;
                shell_store_rewind(address_of exec_store, arena_mark);
                //      A command of assignments alone leaves $_ empty in
                //      bash.
                if (shell_bash_compat)
                        shell_last_argument[0] = end;
                return shell_status;
        }

        // Include argv[count], the terminating null pointer.
        if (first)
                memory_copy(shell_argv, shell_argv + first,
                            (positive)(count - first + 1) *
                                sizeof(shell_argv[0]));

        shell_argc = count - first;

        // $_ is the last argument of the command before this one, which is
        // what taking it here and not after the run means: this command's
        // words are already expanded and have already read the old value.
        //      A compound operand of declare and its family is its name
        //      there: declare a=(1 2) leaves a.
        if (compound_operand && shell_bash_compat &&
            exec_declaration_compound(shell_argv[shell_argc - 1]))
        {
                string_address word = shell_argv[shell_argc - 1];
                positive length = string_span(word, string_set_name);
                p8 name[256];

                if (length < sizeof(name))
                {
                        memory_copy_end(name, word, length);
                        shell_last_argument_set(name);
                }
        }
        else
                shell_last_argument_set(shell_argv[shell_argc - 1]);

        // exec with nothing to run is there for its redirections, and those
        // belong to the shell from here on. Decided before anything runs: a
        // function body or a sourced file run by this command leaves its own
        // last command in argv, and "exec 3>/dev/null" in a function made
        // the redirections on the call permanent as well.
        bare_exec = node->redirect_count && shell_argc == 1 &&
                    word_is(shell_argv[0], "exec");

        log_failure_reset();

        {
                bool previous_error = exec_special_error;
                bool builtin_error;

                exec_special_error = false;
                status = kept || exec_promotable
                             ? exec_dispatch_scoped(EXEC_WORD(first), kept,
                                                    kept_count)
                             : exec_dispatch(EXEC_WORD(first));
                builtin_error = exec_special_error;
                exec_special_error = previous_error;

                fatal = exec_special_error_fatal(
                    command, special, status, builtin_error);
        }
#undef EXEC_WORD
        log_flush();

        /* A write to a closed descriptor reaches the buffered writer at
           flush and leaves its sticky failure bit set. Asking the kernel
           whether stdout was closed before every command duplicated that
           answer and put an fcntl syscall in otherwise kernel-free loops. */
        if (log_failed() && !status)
                status = shell_status = 1;

        if (kept && !exec_finish_prefixes(kept, kept_count) && !status)
                status = shell_status = 2;

        if (node->redirect_count)
        {
                if (bare_exec)
                {
                        if (exec_parser_source_can_refresh() &&
                            exec_parser_source_saved(mark) &&
                            floodlight_parent_supervised &&
                            floodlight_descendants_present())
                        {
                                /* A confined child may have selected or
                                   modified the proposed new input before it
                                   was opened. Keep the old authenticated
                                   reader instead of publishing that source. */
                                exec_redirect_restore(mark);
                                status = shell_status = 126;
                                log_error("exec: active child prevents replacing shell input\n",
                                          0);
                                bare_exec = false;
                                shell_exec_failed(126);
                        }
                        else
                                exec_redirect_forget(mark);
                        /* Subsequent commands on this physical line can feed
                           a new pipe installed by the committed exec.
                           Authenticate it only when no outer temporary
                           redirect will put the old reader back. Ambiguity
                           remains fail-closed. */
                        if (bare_exec && exec_parser_source_can_refresh())
                                shell_parser_source_refresh();
                }
                else
                        exec_redirect_restore(mark);
        }
        /* lima bash waitchld's leftover children when a simple command
           finishes, which is when a coproc that died during `sleep` is
           forgotten so a later unquoted wait $C_PID sees nothing. */
        exec_coproc_drop_finished();

        shell_store_rewind(address_of exec_store, arena_mark);

        if (fatal)
                exec_special_fail(status);

        return status;

        /*
                Nothing of a command that could not run outlives it: the
                exports it made, the values it wrote over, the arena it took.
                Nine ways out each said so in their own words, and one of
                them left the arena behind.
        */
fail:
        if (redirects_applied)
                exec_redirect_restore(mark);
        if (kept && !exec_finish_prefixes(kept, kept_count) && !status)
                status = shell_status = 2;
        exec_put_back(expanded_kept, expanded_count, true);
        shell_store_rewind(address_of exec_store, arena_mark);
        shell_status = status;

        if (fatal)
                exec_special_fail(status);

        return status;
}

// What a break or a continue means to the loop it lands in: go round again,
// stop, or hand it further out still.
static bool exec_loop_again()
{
        if (!exec_signal)
                return true;

        if (exec_signal >= EXEC_SIGNAL_RETURN)
                return false;

        if (exec_signal_level > 1)
        {
                exec_signal_level--;
                return false;
        }

        if (exec_signal == EXEC_SIGNAL_CONTINUE)
        {
                exec_signal = EXEC_SIGNAL_NONE;
                return true;
        }

        exec_signal = EXEC_SIGNAL_NONE;

        return false;
}

static b32 exec_loop(b32 index, bool until)
{
        parse_node address_to node = parse_nodes + index;
        b32 status = 0;

        while (1)
        {
                bool tested = exec_tested;
                b32 test;

                /*
                        The condition is inside the loop as much as the body
                        is: a break in it ends this loop and a continue in it
                        asks the condition again, which is what dash does.
                        Counted outside, a break in the condition meant the
                        enclosing loop, and with no enclosing loop it meant
                        nothing and "while break" ran forever.
                */
                exec_tested = true;
                exec_loop_depth++;
                test = exec_node(node->left);
                exec_loop_depth--;
                exec_tested = tested;

                if (exec_line_aborted())
                {
                        status = shell_status;
                        break;
                }

                if (exec_signal)
                {
                        if (exec_loop_again())
                                continue;

                        break;
                }

                if (until ? test == 0 : test != 0)
                        break;

                exec_loop_depth++;
                status = exec_node(node->right);
                exec_loop_depth--;

                if (!exec_loop_again())
                        break;
        }

        return status;
}

//      What a for loop walks over, and the fields one of its words became.
//      Both live here rather than on the stack so they can grow and be reused
//      by the next loop instead of being sized for a guess. Live nested loops
//      own indexed slices: growing the pointer table may move it, but no caller
//      retains a pointer into it and an inner loop cannot overwrite its outer.
static string_address address_to exec_items;
static positive exec_items_room;
static positive exec_items_used;

// An expansion that aborted the line leaves nothing to run: what was taken
// goes back, and the answer is the status the abort carries.
static b32 exec_aborted(shell_mark mark)
{
        shell_store_rewind(address_of exec_store, mark);
        return shell_status;
}

/*
        What a for or a select walks over, in exec_items.

        Both retain expanded or positional words through the same collector;
        the loop chooses either the next item or the user's selection.
*/
static b32 exec_loop_items(parse_node address_to node, positive base)
{
        b32 count = 0;
        positive words = node->flags ? (positive)node->word_count - 1
                                     : shell_parameter_count;
        for (positive at = 0; at < words; at++)
        {
                positive made = 1;
                string_address address_to fields;
                if (node->flags)
                {
                        shell_words list;
                        shell_words_bind(address_of list, address_of exec_fields,
                                         address_of exec_fields_room);
                        made = shell_expand_fields(parse_words[node->word + at + 1],
                                                   address_of list);
                        if (exec_line_aborted() || expand_failed)
                                break;
                        fields = exec_fields;
                }
                else
                        fields = shell_parameter + at;

                // Expanded words and retained positional arguments both own
                // copies: nested loops may grow either source pointer table.
                for (positive field = 0; field < made; field++)
                {
                        if (count == 0x7fffffff ||
                            base > positive_max - (positive)count - 1 ||
                            !shell_array_room(exec_items, exec_items_room,
                                              base + (positive)count + 1))
                        {
                                shell_memory_failed = true;
                                return count;
                        }
                        string_address kept = exec_arena_copy(fields[field]);
                        if (kept == exec_nothing)
                                return count;
                        exec_items[base + (positive)count++] = kept;
                }
        }
        return count;
}

static COLD b32 exec_loop_assignment_error(string_address name)
{
        if (env_readonly(name))
                shell_readonly_refused(null, null, name,
                                       string_length(name));
        else
        {
                shell_told("%s: cannot assign\n", name);
        }


        if (shell_bash_compat && !shell_posix_on())
                return 1;

        if (shell_bash_compat)
        {
                expand_fatal_status(127);
                return 127;
        }

        expand_fatal_status(2);
        return 2;
}


/*
        select: a numbered menu, a prompt, and a loop that runs the body once
        for every line somebody answers with.

        Everything it writes goes to standard error, so a script may take the
        body's output on a pipe while a person still sees the menu -- which is
        the whole reason the construct exists.
*/

// A line built whole before it is written. What a select writes is one
// menu somebody reads and what a time writes is one timing, not thirty
// writes with somebody else's output free to land between them.
static byte_store exec_built;

#define exec_built_add(text, length)                                         \
        shell_bytes_add(address_of exec_built, (string_address)(text), (length))

static bool exec_built_fill(p8 value, positive times)
{
        while (times--)
                if (!shell_bytes_byte(address_of exec_built, value))
                        return false;

        return true;
}

/*
        The gap between one column and the next, written as tabs wherever a
        tab reaches further than the spaces would.

        This is what Bash writes, and a menu is bytes somebody reads, so the
        padding is part of the answer and not a matter of taste.
*/
static bool select_menu_indent(positive from, positive to)
{
        if (from >= to)
                return true;

        positive tabs = to / 8 - from / 8;
        positive spaces = tabs ? to % 8 : to - from;
        return (!tabs || exec_built_fill('\t', tabs)) &&
               (!spaces || exec_built_fill(' ', spaces));
}

/*
        How wide the menu is allowed to be.

        COLUMNS when it says something, and eighty otherwise, which is what
        Bash falls back to when it is not looking at a terminal.
*/
static positive select_width()
{
        string_address given = env_get((const_string) "COLUMNS");
        positive parsed;

        if (given && string_digits_exact(given, address_of parsed) && parsed)
                return parsed;

        return 80;
}

/*
        The menu itself.

        Every cell is as wide as the longest item plus the room its number
        takes, and the items go down the columns rather than across them. A
        list that would fit on one row is turned on its side and written one
        to a line, which is the shape nearly every menu has.
*/
static fn select_menu_write(positive base, b32 count)
{
        positive width = select_width();
        positive longest = 0;
        positive cell;
        positive columns;
        positive rows;
        positive numbered;
        positive first;
        positive row;
        b32 at;

        for (at = 0; at < count; at++)
        {
                positive length =
                    string_length(exec_items[base + (positive)at]);

                if (length > longest)
                        longest = length;
        }

        numbered = positive_digits((positive)count);
        cell = longest + numbered + 4;
        columns = width / cell;

        if (!columns)
                columns = 1;

        rows = (positive)count / columns + ((positive)count % columns ? 1 : 0);
        columns = (positive)count / rows + ((positive)count % rows ? 1 : 0);

        if (rows == 1)
        {
                rows = columns;
                columns = 1;
        }

        first = positive_digits(rows);
        exec_built.used = 0;

        for (row = 0; row < rows; row++)
        {
                positive item = row;
                positive at_column = 0;

                while (1)
                {
                        p8 shown[32];
                        positive number = at_column ? numbered : first;
                        positive length = string_length(exec_items[base + item]);
                        positive written =
                            bipolar_into_string(shown, (bipolar)(item + 1));

                        if (written < number &&
                            !exec_built_fill(' ', number - written))
                                return;

                        if (!exec_built_add(shown, written) ||
                            !exec_built_add((string_address) ") ", 2) ||
                            !exec_built_add(exec_items[base + item], length))
                                return;

                        item += rows;

                        if (item >= (positive)count)
                                break;

                        if (!select_menu_indent(at_column + number + length + 2,
                                                at_column + cell))
                                return;

                        at_column += cell;
                }

                if (!exec_built_add((string_address) "\n", 1))
                        return;
        }

        log_error(exec_built.bytes, exec_built.used);
}

// The line somebody answered with, without its newline. A byte at a time,
// because whatever is behind it on the stream belongs to the next reader.
static p8 address_to select_reply;
static positive select_reply_room;
static positive select_reply_used;

static bool select_read()
{
        select_reply_used = 0;

        while (1)
        {
                p8 value;

                if (!shell_array_room(select_reply, select_reply_room,
                                      select_reply_used + 2))
                        return false;

                if (system_read_once(0, address_of value, 1) != 1)
                        break;

                if (value == '\n')
                {
                        select_reply[select_reply_used] = end;
                        return true;
                }

                select_reply[select_reply_used++] = value;
        }

        select_reply[select_reply_used] = end;

        // A last line with no newline behind it is still a line. Nothing at
        // all is the end of the input.
        return select_reply_used != 0;
}

/*
        Which item the answer names, or nothing.

        Blanks either side and a leading plus are read exactly as Bash reads
        any number; anything left over makes the whole line not a number, and
        the name is then set to the empty string rather than to an item.
*/
static PURE positive select_choice(b32 count)
{
        string_address at = select_reply;
        positive value = 0;

        at += string_span(at, string_set_blanks);

        if (string_is(at, '+'))
                at++;

        if (!string_digits_checked(address_of at, 10, address_of value))
                return 0;

        at += string_span(at, string_set_blanks);

        if (string_get(at) || !value || value > (positive)count)
                return 0;

        return value;
}

static b32 exec_for(b32 index, bool selecting)
{
        parse_node address_to node = parse_nodes + index;
        string_address name = parse_words[node->word];
        shell_mark mark = shell_store_mark(address_of exec_store);
        positive base = exec_items_used;
        positive substitutions =
            expand_substitutions_ever ? expand_substitutions_count : 0;
        b32 count;
        b32 status = 0;

        token_used = 0;
        count = exec_loop_items(node, base);

        if (exec_line_aborted())
        {
                exec_items_used = base;
                return exec_aborted(mark);
        }

        if (expand_failed)
        {
                exec_items_used = base;
                shell_store_rewind(address_of exec_store, mark);
                return shell_status ? shell_status : 1;
        }

        exec_items_used = base + (positive)count;

        // Nothing to choose from is not a menu nobody answered: no menu is
        // written, the body never runs, and the construct succeeds.
        if (!count)
                goto done;
        if (selecting)
        {
                exec_trace_for_header(node, true);
                select_menu_write(base, count);
        }

        for (positive at = 0; selecting || at < (positive)count;)
        {
                string_address value;
                if (selecting)
                {
                        string_address prompt = env_get((const_string) "PS3");
                        positive chosen;

                        log_error(prompt ? prompt : (string_address) "#? ", 0);

                        if (!select_read())
                        {
                                /*
                                        The end of the input ends the loop, and Bash
                                        calls that a failure rather than an empty
                                        answer.

                                        The newline that closes the unanswered prompt
                                        line goes to standard output, which is where
                                        Bash puts it and the one thing about a select
                                        that a script reading its body's output sees.
                                */
                                log((address_any) "\n", 1);
                                log_flush();
                                status = 1;
                                break;
                        }

                        // An empty line asks for the menu again and nothing else.
                        if (!select_reply_used)
                        {
                                select_menu_write(base, count);
                                continue;
                        }

                        if (!env_assign((const_string) "REPLY", select_reply))
                        {
                                log_error(str("REPLY: cannot assign\n"));
                                status = 2;
                                break;
                        }

                        chosen = select_choice(count);
                        value = chosen ? exec_items[base + chosen - 1]
                                       : (string_address)"";
                }
                else
                {
                        value = exec_items[base + at++];
                        if (trap_debug_here)
                                exec_debug_before(node);
                        exec_trace_for_header(node, false);
                }
                if (!env_assign(name, value))
                {
                        status = exec_loop_assignment_error(name);
                        break;
                }

                exec_loop_depth++;
                status = exec_node(node->right);
                exec_loop_depth--;

                /* lima bash 5.2 unlinks the process-substitution fifo list
                   after each execute_command of a for action, so a second
                   `<( )` path in the word list is gone once the first body
                   has run. */
                if (expand_substitutions_ever)
                        shell_substitutions_close(substitutions);

                if (!exec_loop_again())
                        break;
        }

done:
        exec_items_used = base;
        shell_store_rewind(address_of exec_store, mark);

        return status;
}

static bool exec_arithmetic_value(string_address text,
                                  bipolar address_to value,
                                  string_address command)
{
        shell_mark mark = shell_store_mark(address_of expand_store);
        string_address ready = shell_expand_arithmetic_text(text);
        bool held = arith_bash_mode;

        if (!ready || expand_failed)
        {
                shell_store_rewind(address_of expand_store, mark);
                return false;
        }

        arith_bash_mode = true;
        address_to value = arith_evaluate(ready);
        arith_bash_mode = held;

        if (!arith_bad)
        {
                shell_store_rewind(address_of expand_store, mark);
                return true;
        }

        /* A subscript that could not be read has said so already. */
        if (!arith_unset && !expand_failed)
                shell_arith_report(log_error, command, ready);
        shell_store_rewind(address_of expand_store, mark);
        return false;
}

/*
        (( )) and [[ ]] arrive as one word, brackets and all. The closing pair
        is taken off for as long as the inside is being read and put back
        afterwards, because the word is the parser's: a kept loop body is
        read again next time round.
*/
static bool exec_bracket_strip(string_address whole,
                               positive address_to length, p8 address_to held)
{
        address_to length = string_length(whole);

        if (address_to length < 4)
                return false;

        address_to held = whole[address_to length - 2];
        whole[address_to length - 2] = end;

        return true;
}

static fn exec_bracket_restore(string_address whole, positive length, p8 held)
{
        whole[length - 2] = held;
}

static b32 exec_arithmetic_command(b32 index)
{
        string_address whole = parse_words[parse_nodes[index].word];
        positive length;
        bipolar value;
        b32 status;
        p8 held;

        if (!exec_bracket_strip(whole, address_of length, address_of held))
                return 1;

        exec_trace_arith(whole + 2);

        if (!string_get(whole + 2 + string_span_of_set(whole + 2, " \t\n")))
                status = 1;
        else if (!exec_arithmetic_value(whole + 2, address_of value, "(("))
                status = exec_line_aborted() ? shell_status : 1;
        else
                status = value ? 0 : 1;

        exec_bracket_restore(whole, length, held);

        return status;
}

// A C-for separator is a semicolon in the outer arithmetic grammar, not one
// inside grouping, a quote, ${...}, $(...), or a backtick substitution.
static PURE string_address exec_cfor_separator(string_address at)
{
        positive depth = 0;

        while (string_get(at))
        {
                p8 value = string_get(at);
                b32 skipped = lex_skip_held(address_of at);

                //      An unclosed quote leaves at on the terminating null,
                //      which is where running out of separators ends too.
                if (skipped == LEX_SKIP_UNCLOSED)
                        return at;

                if (skipped)
                        continue;

                if (value == '(')
                        depth++;
                else if (value == ')' && depth)
                        depth--;
                else if (value == ';' && !depth)
                        return at;

                at++;
        }

        return at;
}

static b32 exec_cfor(b32 index)
{
        parse_node address_to node = parse_nodes + index;
        shell_mark arena = shell_store_mark(address_of exec_store);
        string_address whole = parse_words[node->word];
        positive length = string_length(whole);
        positive inner_length;
        p8 address_to expressions;
        string_address initialize;
        string_address condition;
        string_address update;
        string_address first;
        string_address second;
        bipolar value;
        b32 status = 0;

        if (length < 4)
        {
                status = 2;
                goto done;
        }

        inner_length = length - 4;
        expressions = shell_store_copy(address_of exec_store, whole + 2,
                                        inner_length);

        if (!expressions)
        {
                status = 2;
                goto done;
        }

        first = exec_cfor_separator(expressions);

        if (!string_get(first))
        {
                shell_diagnostic_where();
                log_error(str("arithmetic: expected two semicolons\n"));
                status = 2;
                goto done;
        }

        address_to first = end;
        second = exec_cfor_separator(first + 1);

        if (!string_get(second))
        {
                shell_diagnostic_where();
                log_error(str("arithmetic: expected two semicolons\n"));
                status = 2;
                goto done;
        }

        address_to second = end;
        /* A clause of nothing but blanks is an empty clause: for (( ; ; ))
           loops for ever, where the blank condition was read as 0 and the
           body never ran. */
        initialize = arith_skip_space(expressions);
        condition = arith_skip_space(first + 1);
        update = arith_skip_space(second + 1);

        if (trap_debug_here)
                exec_debug_clause(node, initialize);
        if (string_get(initialize) &&
            !exec_arithmetic_value(initialize, address_of value, "(("))
        {
                status = exec_line_aborted() ? shell_status : 1;
                goto done;
        }

        while (1)
        {
                //      The clauses are on the for's line, and $LINENO in
                //      them says so rather than the body's last line.
                if (node->line)
                        exec_line = node->line;
                if (trap_debug_here)
                        exec_debug_clause(node, condition);
                if (string_get(condition))
                {
                        if (!exec_arithmetic_value(condition, address_of value,
                                                   "(("))
                        {
                                status = exec_line_aborted() ? shell_status : 1;
                                goto done;
                        }

                        if (!value)
                                break;
                }

                exec_loop_depth++;
                status = exec_node(node->right);
                exec_loop_depth--;

                if (!exec_loop_again())
                        break;

                if (node->line)
                        exec_line = node->line;
                if (trap_debug_here)
                        exec_debug_clause(node, update);
                if (string_get(update) &&
                    !exec_arithmetic_value(update, address_of value, "(("))
                {
                        status = exec_line_aborted() ? shell_status : 1;
                        goto done;
                }
        }

done:
        shell_store_rewind(address_of exec_store, arena);
        return status;
}

/*
        Bash [[...]] has a word grammar of its own. In particular its && and
        || are not pipelines, and its operands expand without splitting or
        pathname lookup. Keep raw words here so operators produced by an
        expansion remain operands rather than turning into syntax afterward.
*/
static string_address address_to conditional_word;
static positive conditional_word_room;
static positive conditional_word_count;
static positive conditional_at;
static bool conditional_bad;
static bool conditional_runtime;
static bool conditional_active;

static bool conditional_add(string_address text, positive length)
{
        string_address kept;

        if (length == positive_max || conditional_word_count == positive_max ||
            !shell_array_room(conditional_word, conditional_word_room, conditional_word_count + 1))
                return false;

        kept = shell_store_copy(address_of exec_store, text, length);

        if (!kept)
                return false;

        conditional_word[conditional_word_count++] = kept;
        return true;
}

static bool conditional_tokenize(string_address text)
{
        string_address at = text;

        conditional_word_count = 0;

        while (string_get(at))
        {
                string_address start;
                bool regex_operand;

                at += string_span_of_set(at, " \t\n");

                if (!string_get(at))
                        break;

                regex_operand = conditional_word_count &&
                                word_is(conditional_word[conditional_word_count - 1],
                                        "=~");

                if ((string_is(at, '&') && string_is(at + 1, '&')) ||
                    (string_is(at, '|') && string_is(at + 1, '|')))
                {
                        if (!conditional_add(at, 2))
                                return false;

                        at += 2;
                        continue;
                }

                if (!regex_operand &&
                    (string_is(at, '(') || string_is(at, ')')))
                {
                        if (!conditional_add(at, 1))
                                return false;

                        at++;
                        continue;
                }

                start = at;

                /* A regex operand is one word through parentheses, blanks
                   inside them included: [[ $v =~ (one two) ]] has the
                   pattern "(one two)", as bash reads it. */
                positive depth = 0;

                while (string_get(at) &&
                       (depth || !lex_is_space(string_get(at))))
                {
                        p8 value = string_get(at);

                        if (regex_operand && value == '(')
                        {
                                depth++;
                                at++;
                                continue;
                        }
                        if (regex_operand && value == ')' && depth)
                        {
                                depth--;
                                at++;
                                continue;
                        }

                        /*
                                An extended pattern group is one piece of the
                                word. [[ ]] reads them whether or not the
                                option is on, because what is in here is
                                matched when the command runs.
                        */
                        if (string_is(at + 1, '(') && lex_extended_head(value))
                        {
                                string_address group = lex_nesting(at + 1);

                                if (group > at + 1)
                                {
                                        at = group;
                                        continue;
                                }
                        }

                        if ((!depth &&
                             ((value == '&' && string_is(at + 1, '&')) ||
                              (value == '|' && string_is(at + 1, '|')))) ||
                            (!regex_operand &&
                             (value == '(' || value == ')')))
                                break;

                        b32 skipped = lex_skip_held(address_of at);
                        if (skipped == LEX_SKIP_UNCLOSED)
                                return false;
                        if (skipped)
                                continue;

                        at++;
                }

                if (at == start ||
                    !conditional_add(start, (positive)(at - start)))
                        return false;
        }

        return true;
}

static PURE bool conditional_is(string_address word)
{
        return conditional_at < conditional_word_count &&
               word_is(conditional_word[conditional_at], word);
}

static string_address conditional_expand(string_address word, bool pattern)
{
        return pattern ? shell_expand_pattern(word) : shell_expand_word(word);
}

static bool conditional_integer(positive kind, string_address left,
                                string_address right)
{
        bool held = arith_bash_mode;
        bool held_nounset = arith_nounset;
        bipolar first;
        bipolar second;

        arith_bash_mode = true;
        arith_nounset =
            (shell_options & ((positive)1 << ('u' - 'a'))) != 0;
        arith_unset = false;
        first = arith_evaluate(left);

        if (arith_bad)
        {
                arith_bash_mode = held;
                arith_nounset = held_nounset;
                return false;
        }

        second = arith_evaluate(right);
        arith_bash_mode = held;
        arith_nounset = held_nounset;

        if (arith_bad)
                return false;

        return test_ordered(kind, first, second);
}

static COLD fn conditional_nounset_fatal()
{
        shell_diagnostic_where();
        log_error(str("arithmetic: parameter not set\n"));
        shell_status = 1;

        if (shell_is_interactive)
        {
                exec_abort_line(shell_status);
                return;
        }

        if (!expand_in_substitution)
                shell_trap_exit();

        log_flush();
        system_call_1(syscall(exit_group), 1);
}

/*
        The regex engine is shared by grep, sed, AWK, and the shell. A [[ =~ ]]
        compile is transient: restore both the selected program and every pool
        watermark afterward so repeated conditions cannot consume or retarget
        another builtin's cached programs.
*/
/*
        BASH_REMATCH: what matched, then one element per capture group.

        The engine's slot pairs are byte offsets into the text it was handed,
        so the substrings are cut while that text and those slots are still
        the ones this match left behind -- the caller puts the whole engine
        state back immediately afterwards. A group that took no part has no
        offsets, and Bash gives it an empty element rather than a hole.
*/
static COLD fn conditional_regex_captures(string_address text)
{
        shell_mark held = shell_store_mark(address_of expand_store);
        positive count = (positive)regex_group_count + 1;
        string_address address_to words =
            (string_address address_to)shell_store_take(
                address_of expand_store, count * sizeof(words[0]));

        if (!words)
                return;

        for (positive at = 0; at < count; at++)
        {
                positive from = regex_slots[at * 2];
                positive to = regex_slots[at * 2 + 1];
                p8 address_to made;

                if (from == positive_max || to == positive_max || from > to)
                {
                        words[at] = (string_address) "";
                        continue;
                }

                made = shell_store_copy(address_of expand_store,
                                        text + from, to - from);

                if (!made)
                {
                        shell_store_rewind(address_of expand_store, held);
                        return;
                }

                words[at] = made;
        }

        shell_array_words("BASH_REMATCH", 12, words, count);
        shell_store_rewind(address_of expand_store, held);
}

static bool conditional_regex_match(string_address text, string_address pattern,
                                    bool address_to valid)
{
        regex_program saved = regex_current;
        rx_mark mark = regex_pool.used;
        positive slots[RX_SLOT_MAX];
        bool matched = false;

        memory_copy_apart(slots, regex_slots, sizeof(slots));
        /* Compile above any live transient program; rewind only our own work. */
        address_to valid = rx_compile(&regex_pool, &regex_current, pattern,
                                      true, false, false, REGEX_POLICY_DEFAULT);
        if (address_to valid)
        {
                matched = regex_find(REGEX_FIRST | REGEX_CAPTURES, text, string_length(text), 0);
                if (matched)
                        conditional_regex_captures(text);
                else
                {
                        /* A failed match empties BASH_REMATCH, as bash's
                           does; the last success's groups stayed. */
                        string_address none[1] = {null};

                        shell_array_words("BASH_REMATCH", 12, none, 0);
                }
        }
        regex_pool.used = mark;
        regex_current = saved;
        memory_copy_apart(regex_slots, slots, sizeof(slots));
        return matched;
}

static bool conditional_expression();

static bool conditional_binary_ready()
{
        conditional_at++;

        if (conditional_at >= conditional_word_count)
        {
                conditional_bad = true;
                return false;
        }

        if (!conditional_active)
        {
                conditional_at++;
                return false;
        }

        return true;
}

static PURE bool conditional_unary_op(string_address word)
{
        return test_is_unary(word) || word_is(word, "-a") ||
               word_is(word, "-v") || word_is(word, "-o") ||
               word_is(word, "-R");
}

static PURE bool conditional_unary_operand(string_address word)
{
        return !word_is(word, "&&") && !word_is(word, "||") &&
               !word_is(word, "(") && !word_is(word, ")");
}

// [[ == ]] and case read extended groups whether or not extglob is on, and
// fold case under nocasematch.
static bool shell_match_case(string_address pattern, string_address subject)
{
        bool fold = shell_shopt_on(NOCASEMATCH);

        return glob_extended_anywhere(pattern)
                   ? shell_match_extended(pattern, subject, fold)
                   : shell_match_folded(pattern, subject, fold);
}

static bool conditional_primary(bool invert)
{
        string_address raw;

        if (conditional_at >= conditional_word_count)
        {
                conditional_bad = true;
                return false;
        }

        if (conditional_is("("))
        {
                /* As deep as arithmetic may nest (ARITH_NESTING): each
                   level is a descent through the whole ladder, and 30,000
                   of them took the shell down with SIGSEGV. */
                static positive nesting;
                bool value;

                if (nesting >= 1024)
                {
                        conditional_bad = true;
                        return false;
                }
                conditional_at++;
                nesting++;
                value = conditional_expression();
                nesting--;

                if (!conditional_is(")"))
                        conditional_bad = true;
                else
                        conditional_at++;

                return value;
        }

        raw = conditional_word[conditional_at];

        if (conditional_unary_op(raw))
        {
                string_address operand_raw;
                string_address operand;
                bool value = false;

                if (conditional_at + 1 >= conditional_word_count ||
                    !conditional_unary_operand(
                        conditional_word[conditional_at + 1]))
                {
                        conditional_bad = true;
                        conditional_at++;
                        return false;
                }

                operand_raw = conditional_word[conditional_at + 1];
                conditional_at += 2;

                if (!conditional_active)
                        return false;

                operand = conditional_expand(operand_raw, false);

                if (expand_failed)
                        return false;

                exec_trace_conditional_term(invert, operand, raw, null);

                if (word_is(raw, "-a"))
                        return test_unary('e', operand);

                if (word_is(raw, "-v"))
                        return test_variable_set(operand);

                /*
                        -R names a variable, not a path. After expansion an
                        unset $name is empty under set +u, and that empty
                        name is false rather than an error. -v is "set";
                        this is "this name is a nameref".
                */
                if (word_is(raw, "-R"))
                        return operand && string_get(operand) &&
                               (shell_variable_attributes(
                                    operand, string_length(operand)) &
                                SHELL_ARRAY_NAMEREF) != 0;

                if (word_is(raw, "-o"))
                {
                        positive option = string_table_find(
                            operand, shell_option_names,
                            sizeof(shell_option_names[0]), SHELL_OPTION_NAMES);

                        return option < SHELL_OPTION_NAMES &&
                               shell_option_on(option);
                }

                test_bad = false;
                {
                        /* A diagnostic names the command through
                           shell_argv[0], and before any simple command has
                           run there is no argument array to write into. */
                        string_address address_to held = shell_argv;
                        string_address named[2] = {(string_address) "[[", null};

                        shell_argv = named;
                        value = test_unary(string_get(raw + 1), operand);
                        shell_argv = held;
                }
                if (test_bad)
                        conditional_runtime = true;
                return value;
        }

        conditional_at++;

        if (conditional_at < conditional_word_count)
        {
                string_address op = conditional_word[conditional_at];
                positive kind = test_is_binary(op);
                bool pattern = word_is(op, "=") || word_is(op, "==") ||
                               word_is(op, "!=");

                if (word_is(op, "=="))
                        kind = TEST_SAME;

                if (word_is(op, "=~"))
                {
                        string_address left;
                        string_address right;
                        bool valid;
                        bool value;

                        if (!conditional_binary_ready())
                                return false;

                        left = conditional_expand(raw, false);
                        right = shell_expand_regex(
                            conditional_word[conditional_at++]);

                        if (expand_failed)
                                return false;

                        exec_trace_conditional_term(invert, left, op, right);

                        value = conditional_regex_match(left, right,
                                                        address_of valid);

                        if (!valid)
                                conditional_runtime = true;

                        return value;
                }

                if (kind)
                {
                        string_address left;
                        string_address right;
                        bool integer;
                        bool value;

                        if (!conditional_binary_ready())
                                return false;

                        /* An integer operand is arithmetic, whose subscripts
                           the evaluator expands: expanding them here too
                           ran a $(...) that a value held. */
                        integer = kind >= TEST_EQUAL &&
                                  kind <= TEST_GREATER_EQUAL;
                        left = conditional_expand(
                            integer ? arith_subscripts_held(raw) : raw, false);
                        right = conditional_word[conditional_at++];
                        right = conditional_expand(
                            integer ? arith_subscripts_held(right) : right,
                            pattern);

                        if (expand_failed)
                                return false;

                        exec_trace_conditional_term(invert, left, op, right);

                        if (pattern)
                        {
                                /*
                                        Both of the things [[ ]] does that
                                        ordinary matching does not: it reads
                                        the extended groups whether or not the
                                        option is on, and it folds case under
                                        nocasematch. Both modes travel through
                                        the shared matcher, including groups.
                                */
                                value = shell_match_case(right, left);
                                return word_is(op, "!=") ? !value : value;
                        }

                        if (integer)
                                return conditional_integer(kind, left, right);

                        test_bad = false;
                        value = test_compare(kind, left, right);

                        if (test_bad)
                                conditional_runtime = true;

                        return value;
                }
        }

        if (!conditional_active)
                return false;

        raw = conditional_expand(raw, false);
        if (expand_failed)
                return false;

        exec_trace_conditional_term(invert, raw, (string_address) "-n", null);
        return string_get(raw) != end;
}

static bool conditional_negation()
{
        bool invert = false;

        while (conditional_is("!"))
        {
                conditional_at++;
                invert = !invert;
        }

        /* An operand bash cannot read makes its term answer 2 rather than
           true or false, and the answer is a status like any other: `!`
           turns it into 0, && stops at it and || goes past it to the right
           hand side, whose answer is the one that stands. */
        bool before = conditional_runtime;

        conditional_runtime = false;
        bool value = conditional_primary(invert);

        if (invert && conditional_runtime)
        {
                conditional_runtime = before;
                return true;
        }
        conditional_runtime = conditional_runtime || before;
        return invert ? !value : value;
}

#define CONDITIONAL_LOGICAL_LEVEL(name, lower, spelling, wanted, operation)  \
        static bool name()                                                  \
        {                                                                    \
                bool value = lower();                                       \
                                                                             \
                while (conditional_is(spelling))                            \
                {                                                            \
                        bool held = conditional_active;                      \
                                                                             \
                        if (held && (wanted) && !value)                     \
                                conditional_runtime = false;                \
                        conditional_at++;                                   \
                        conditional_active = held && (wanted);              \
                        bool other = lower();                               \
                        conditional_active = held;                          \
                        value = value operation other;                       \
                }                                                            \
                                                                             \
                return value;                                                \
        }

CONDITIONAL_LOGICAL_LEVEL(conditional_conjunction, conditional_negation,
                          "&&", value, &&)
CONDITIONAL_LOGICAL_LEVEL(conditional_expression, conditional_conjunction,
                          "||", !value, ||)
#undef CONDITIONAL_LOGICAL_LEVEL

/*
        A malformed [[ ]] is a parse error in bash: the rest of that
        physical line does not run, and a non-interactive top-level
        reader leaves with status 2. Arithmetic and regex failures stay
        ordinary command statuses so a later echo still runs.

        The diagnostic waits until redirections of this command have been
        put back. `[[ ... ]] 2>/dev/null` must not swallow a parse error,
        because bash reports it before that redirect exists.
*/
static bool conditional_syntax;

static COLD fn conditional_syntax_abort()
{
        shell_status = 2;
        shell_syntax_generation += 2;
        exec_abort_line(2);
        conditional_syntax = true;
}

static COLD fn conditional_syntax_finish()
{
        if (!conditional_syntax)
                return;

        conditional_syntax = false;
        shell_syntax_where();
        log_error(str("syntax error in conditional expression\n"));
        log_flush();

        if (shell_run_depth == 1 && !shell_source_depth)
        {
                if (string_is(shell_option_flags, 'c'))
                        exec_child_leave(shell_status);
                if (!shell_is_interactive)
                        expand_fatal_status(shell_status);
        }
}

static b32 exec_conditional(b32 index)
{
        shell_mark arena = shell_store_mark(address_of exec_store);
        string_address whole = parse_words[parse_nodes[index].word];
        positive length;
        p8 held;
        bool value = false;
        b32 status;

        if (!exec_bracket_strip(whole, address_of length, address_of held))
                return 2;

        conditional_bad = false;
        conditional_runtime = false;
        conditional_syntax = false;
        conditional_active = true;
        conditional_at = 0;
        expand_failed = false;
        arith_unset = false;

        if (!conditional_tokenize(whole + 2))
                conditional_bad = true;
        else if (!conditional_word_count)
                conditional_bad = true;
        else
                value = conditional_expression();

        if (conditional_at != conditional_word_count)
                conditional_bad = true;
        if (expand_failed)
                conditional_runtime = true;

        if (arith_unset)
                conditional_nounset_fatal();

        exec_bracket_restore(whole, length, held);

        if (conditional_bad)
        {
                shell_store_rewind(address_of exec_store, arena);
                conditional_syntax_abort();
                return 2;
        }

        status = exec_line_aborted() ? shell_status
                 : arith_unset ? 1 : conditional_runtime ? 2 : value ? 0 : 1;
        shell_store_rewind(address_of exec_store, arena);
        return status;
}

static b32 exec_case(b32 index)
{
        parse_node address_to node = parse_nodes + index;
        shell_mark mark = shell_store_mark(address_of exec_store);
        string_address subject;
        b32 item;
        b32 status = 0;

        token_used = 0;
        subject = shell_expand_word(parse_words[node->word]);

        if (exec_line_aborted())
                return exec_aborted(mark);

        subject = exec_arena_copy(subject);
        exec_trace_case_header(node);

        /*
                An item whose predecessor ended in ;& runs without being
                asked, which is the only way a body runs with none of its own
                patterns matching.
        */
        bool falling = false;

        for (item = node->left; item; item = parse_nodes[item].next)
        {
                b32 at;
                bool taken = falling;

                falling = false;

                for (at = 0; !taken && at < parse_nodes[item].word_count; at++)
                {
                        string_address pattern;

                        token_used = 0;
                        pattern = shell_expand_pattern(
                            parse_words[parse_nodes[item].word + at]);

                        if (exec_line_aborted())
                                return exec_aborted(mark);

                        // The structure is fall-through's: an item that
                        // matched is taken, and ;;& goes on testing. The
                        // match is [[ ]]'s, for the same reason case reads
                        // extended groups and folds case.
                        taken = shell_match_case(pattern, subject);
                }

                if (!taken)
                        continue;

                // A matched item with nothing in it ran nothing and answered
                // with whatever came before the case; an empty list of
                // commands succeeds.
                status = parse_nodes[item].right
                             ? exec_node(parse_nodes[item].right)
                             : 0;

                if (parse_nodes[item].flags == CASE_FALL_THROUGH)
                {
                        falling = true;
                        continue;
                }

                // ;;& leaves the remaining patterns to be asked; ;; and the
                // last item of all are the end of the case.
                if (parse_nodes[item].flags != CASE_TEST_ON)
                        break;
        }

        shell_store_rewind(address_of exec_store, mark);

        return status;
}

static b32 exec_if(b32 index)
{
        parse_node address_to node = parse_nodes + index;
        bool tested = exec_tested;
        b32 test;

        exec_tested = true;
        test = exec_node(node->left);
        exec_tested = tested;

        if (exec_signal)
                return test;

        if (test == 0)
                return exec_node(node->right);

        if (node->extra)
                return exec_node(node->extra);

        return 0;
}

/*
        A foreground wait that still hears the background.

        Blocked in wait4 on the foreground child, the shell left a background
        job that ended a zombie until the command was over, so `sleep 1 &
        tail -f --pid=$! x` never ended: the pid tail watches still existed.
        bash reaps as each child ends. waitid with WNOWAIT says which child
        ended without taking it; one of this shell's background jobs is swept
        into its table, and anything else -- the foreground child, or a
        pipeline stage another waiter owns -- is left to the ordinary wait.
*/
#define WAIT_EXITED 4
#define WAIT_NO_WAIT 0x01000000

static COLD fn exec_wait_hearing(bipolar child)
{
        for (positive round = 0; round < 4096; round++)
        {
                b32 info[32] = {0};
                bipolar got = system_call_5(syscall(waitid), 0, 0,
                                            (positive)info,
                                            WAIT_EXITED | WAIT_NO_WAIT, 0);
                bipolar pid;
                bool background = false;

                if (got == -4)
                        continue;
                pid = info[4];
                if (got < 0 || pid <= 0 || pid == child)
                        return;
                for (positive at = 0; at < shell_wait_count; at++)
                        if (shell_wait_table[at].pid == pid &&
                            !(shell_wait_table[at].flags & SHELL_WAIT_DONE))
                                background = true;
                if (!background)
                        return;
                job_reap();
        }
}

static PURE bool exec_background_live()
{
        for (positive at = 0; at < shell_wait_count; at++)
                if (!(shell_wait_table[at].flags & SHELL_WAIT_DONE))
                        return true;
        return false;
}

static fn exec_wait_background(bipolar child)
{
        if (shell_wait_count && exec_background_live())
                exec_wait_hearing(child);
}

static b32 exec_wait_status(bipolar child, positive flags,
                            positive address_to raw)
{
        address_to raw = 0;

        if (child < 0)
                return 1;

        if (!flags)
                exec_wait_background(child);

        if (system_wait4_retry(child, raw, flags, null) < 0)
                return 1;

        if ((address_to raw & 0xff) == 0x7f)
                return 128 + (b32)((address_to raw >> 8) & 0xff);

        return wait_status_code(address_to raw);
}

static b32 exec_child_status(bipolar child)
{
        positive state = 0;
        b32 code = exec_wait_status(child, 0, address_of state);

        shell_child_death(child, state, true);
        return code;
}

/* An asynchronous command outside job control inherits the interactive
   shell's ignored INT/QUIT state and reads /dev/null unless the command later
   supplies its own redirection. A monitored job gets the foreground defaults
   here; its separate process group, rather than ignored signals, protects it
   from terminal input intended for the shell. fd 0 is already the desired
   result from openat and must not be closed. A foreground child gets the
   default back only where the shell's own deafness was the reason for the
   ignore: not over a trap '' in the script, and not over an ignore this shell
   was started with. */
static bool exec_child_signals(bool detached, bool null_input)
{
        if (!detached)
        {
                shell_child_default(SIGNAL_INTERRUPT);
                shell_child_default(SIGNAL_QUIT);
                return true;
        }

        shell_ignore(SIGNAL_INTERRUPT);
        shell_ignore(SIGNAL_QUIT);

        if (null_input)
        {
                bipolar null_handle;

                /* Make descriptor zero itself the room for /dev/null.  Opening
                   first fails under a full descriptor table and used to leave
                   the asynchronous command sharing the shell's parser input.
                   A failed replacement must end this child rather than let it
                   consume commands meant for its parent. */
                (void)system_close(standard_input_descriptor);
                do
                        null_handle = system_open_at(AT_FDCWD,
                                                     "/dev/null", 0);
                while (null_handle == -4);

                if (null_handle != standard_input_descriptor)
                {
                        if (null_handle >= 0)
                                system_close(null_handle);
                        return false;
                }
        }

        return true;
}

/* Recognize only the command name whose bytes are already fixed in the
   parent's parse tree. Functions and shell builtins keep their precedence;
   dynamic and compound commands reach the child with no claimed contract and
   therefore fail closed if they later try to confine an in-process applet. */
static bool exec_literal_tool(b32 index, string_address address_to name,
                              positive2 address_to named)
{
        parse_node address_to node = parse_nodes + index;
        b32 at = 0;
        b32 word;

        if (node->kind != NODE_SIMPLE || !node->word_count)
                return false;

        while (at < node->word_count &&
               (parse_word_flags[node->word + at] & PARSE_WORD_ASSIGNMENT))
                at++;
        if (at >= node->word_count)
                return false;

        word = node->word + at;
        if (!(parse_word_flags[word] & PARSE_WORD_LITERAL) ||
            string_first_of(parse_words[word], '/'))
                return false;

        *name = parse_words[word];
        *named = string_hash_33_length(*name);
        if (exec_function_slot(*name, *named) != positive_max ||
            exec_control_builtin(*name, false) ||
            shell_command_named_hashed(*name, *named))
                return false;

        return shell_tool_find_hashed(*name, *named) != SHELL_TOOLS;
}

/* A fallback-confined resident applet can run inside the structural child
   without another fork. Establish the real parser parent's contract before
   making that child. A later flag refusal is harmless; network/spawn policy
   depends only on this already authenticated literal subject. */
static fn exec_node_parent_policy_prepare(b32 index)
{
        string_address name;
        positive2 named;
        string_address arguments[2];

        if (!exec_literal_tool(index, address_of name, address_of named))
                return;

        arguments[0] = name;
        arguments[1] = null;
        (void)floodlight_launch_decide(null, arguments, 1, true,
                                       false, false, null);
}

static bipolar exec_spawn_node(b32 index, bool background)
{
        bipolar child;
        bool monitor = job_monitor();

        if ((background || monitor) && !job_reserve(1, false))
        {
                log_error(str("No room to retain child\n"));
                return -ERROR_NO_MEMORY;
        }

        exec_node_parent_policy_prepare(index);
        log_flush();
        child = shell_clone();

        if (child == 0)
        {
                b32 status;

                exec_asynchronous = background;
                trap_default_all();
                if (!exec_child_signals(background && !monitor,
                                        background && !monitor))
                        system_call_1(syscall(exit_group), 126);

                /* Raced from both sides, because either side alone loses:
                   the parent may reach setpgid after the child has exec'd,
                   and the child may be signalled before it has run at all. */
                if (monitor)
                        job_child_group_enter(0);
                exec_child_began();

                /* A subshell is not the shell whose jobs those are, and its
                   own numbering starts at one again. A command substitution
                   is the exception and keeps them, because `$(jobs -p)` is
                   how a script asks this shell what it is running. */
                job_forget();

                /* A subshell is outside every loop of the shell that made
                   it: bash's (break) there says it is only meaningful in a
                   loop, whatever its operand. */
                exec_loop_depth = 0;
                exec_child_root = index;

                /* The async environment is already a subshell. Turning an
                   explicit (...) node into its equivalent group avoids a
                   second process whose PID would not be $!. */
                if (background && parse_nodes[index].kind == NODE_SUBSHELL)
                        parse_nodes[index].kind = NODE_GROUP;

                /* This child is the background command, so its and-or list
                   runs here rather than being put in the background again. */
                if (background && parse_nodes[index].kind == NODE_ANDOR)
                        parse_nodes[index].flags = 0;

                /* A child made for one simple command -- `cmd &`, or a
                   subshell `( cmd )` whose whole body it is -- becomes that
                   command, rather than making a second process to run it and
                   waiting there to pass on its status. */
                shell_tail_command = parse_nodes[index].kind == NODE_SIMPLE;

                status = exec_node(index);
                exec_child_leave(status);
        }

        if (monitor && child > 0)
                job_group_set(child, child);

        return child;
}

/*
        A pipeline.

        Ordinarily every stage gets a process of its own, because a builtin on
        either end of a pipe has to have its own fd 1 and fd 0. Bash lastpipe
        is the deliberate exception: without job control, an explicitly
        enabled final builtin/function/compound command runs here so that its
        state survives. The same stage loop and status vector serve both
        policies; an eligible external final stage keeps the direct spawn path.

        The parent closes both ends of every pipe it made before it waits: a
        write end still open here is an end of file the reader never sees, and
        the whole shell stops.

        Bash finishes while/for/if/until/case/select in a pipeline with _exit,
        so an EXIT trap set in that stage does not run. A group, a function
        and an explicit ( ) still run one, which is why wrapping the while
        in { } prints what the bare while does not. dash runs the trap in
        every pipeline child. lastpipe is the parent, so a trap set there
        belongs to the shell and runs when the shell itself leaves.
*/

static PURE bool exec_pipe_omits_exit(b32 kind)
{
        if (!shell_bash_compat)
                return false;

        return kind == NODE_WHILE || kind == NODE_UNTIL || kind == NODE_FOR ||
               kind == NODE_SELECT || kind == NODE_IF || kind == NODE_CASE ||
               kind == NODE_CFOR || kind == NODE_ARITHMETIC ||
               kind == NODE_CONDITIONAL || kind == NODE_FUNCTION;
}

/*
        A stage that can be spawned rather than forked, and its pid.

        Every stage of a pipeline is a separate process either way. The fork
        exists only so the child can arrange its own descriptors before
        replacing itself, and it copies a page table to do it. When the stage
        is an ordinary external command the descriptors can be named in the
        request instead, and nothing is copied.

        The conditions are conservative on purpose, because a stage that
        takes this path is expanded here rather than in a child, and anything
        whose expansion could be felt afterwards must not be. Literal words
        with no pattern bytes expand to themselves, so there is nothing to
        feel. A redirection, an assignment prefix, a function, a builtin or
        any word that is not exactly its own text sends the stage back to the
        fork, which is still correct and merely slower.

        Answers -1 for "not this stage", which is not an error: the caller
        forks as it always did.
*/
#define EXEC_STAGE_WORDS_MAX 64

static bipolar exec_stage_spawn(b32 index, b32 input, b32 output)
{
        static p8 address_to found;
        static positive found_room;
        parse_node address_to node = parse_nodes + index;
        string_address words[EXEC_STAGE_WORDS_MAX + 1];
        string_address name;
        string_address executable;
        positive2 named;
        b32 at;

        if (node->kind != NODE_SIMPLE || node->redirect_count ||
            !node->word_count || node->word_count > EXEC_STAGE_WORDS_MAX)
                return -1;

        for (at = 0; at < node->word_count; at++)
        {
                b32 word = node->word + at;
                string_address text = parse_words[word];

                if (!(parse_word_flags[word] & PARSE_WORD_LITERAL) ||
                    (parse_word_flags[word] & PARSE_WORD_ASSIGNMENT))
                        return -1;

                words[at] = text;
        }

        words[node->word_count] = null;
        name = words[0];
        executable = name;

        if (exec_literal_tool(index, address_of name, address_of named))
        {
                /* The structural caller already prepared the owning parser
                   before choosing between this probe and its fork path. */
                return -1;
        }

        /* Let ordinary dispatch own the one user-facing refusal. This path
           only declines the direct spawn that would otherwise bypass it. */
        if (!shell_command_path_allowed(name, false))
                return -1;

        // A slash names the program outright; anything else has to prove it
        // is not something this shell would have run itself.
        if (!string_first_of(name, '/'))
        {
                named = string_hash_33_length(name);

                if (exec_function_slot(name, named) != positive_max ||
                    exec_control_builtin(name, false))
                        return -1;

                if (shell_command_named_hashed(name, named))
                        return -1;

                if (shell_find_in_path_alloc(name, address_of found,
                                             address_of found_room) != 1)
                        return -1;

                executable = found;
        }

        /* PATH selected an external image even when its basename is also a
           resident applet.  Give that image external policy identity.  An
           active register sends it through the child path, whose final check
           pins the executable; a stock kernel may still use direct spawn. */
        if (floodlight_launch_decide(executable, words,
                                     (positive)node->word_count, false,
                                     false, false, null) !=
            FLOODLIGHT_LAUNCH_ALLOW)
                return -1;

        /* A bowl guest is recognised by the file PATH found, and a wrapped
           stage starts the bowl program instead. Anything else is loaded from
           that file under the word as it was typed. */
        words[0] = executable;

        if (bowl_wrap_words(shell_directory, words, (positive)node->word_count,
                            EXEC_STAGE_WORDS_MAX + 1))
                executable = words[0];
        else
                words[0] = name;

        return shell_spawn_stage(executable, words, input, output, -1);
}

/*
        coproc: a command running alongside this shell with a pipe each way.

        A pipeline hands one command's output to the next and then waits for
        both. A coprocess is the other arrangement -- it keeps running, and
        the shell writes to it and reads from it whenever it likes, which is
        what makes a long-lived helper process possible at all.

        The two descriptors are published as an array because that is what
        they are: NAME[0] is read from and NAME[1] is written to, and NAME_PID
        is who is at the other end.
*/
#define COPROC_FLOOR 60

// Out of the way of the script's own descriptors and out of the way of the
// commands it runs. A coprocess that could see its own pipe ends through
// somebody else's child would wait for an end of file that never came.
static b32 coproc_kept(b32 descriptor)
{
        bipolar moved = system_call_3(syscall(fcntl), (positive)descriptor,
                                      F_DUPFD_CLOEXEC, COPROC_FLOOR);

        /* Both original ends were made close-on-exec by pipe2. A low
           descriptor is therefore still safe when the process limit leaves
           no number at COPROC_FLOOR or above. */
        if (moved < 0)
                return descriptor;

        system_close(descriptor);

        return (b32)moved;
}

#define EXEC_COPROC_LIVE 8

typedef struct
{
        p8 name[EXEC_COPROC_NAME + 1];
        positive name_length;
        bipolar pid;
} exec_coproc_slot;

static exec_coproc_slot exec_coprocs[EXEC_COPROC_LIVE];

static fn exec_coproc_child()
{
        exec_coproc_count = 0;
}

static fn exec_coproc_unset(exec_coproc_slot address_to slot)
{
        p8 pid_name[EXEC_COPROC_NAME + 5];

        env_unset_span(slot->name, slot->name_length);
        memory_copy(pid_name, slot->name, slot->name_length);
        memory_copy_end(pid_name + slot->name_length, (string_address) "_PID", 4);
        pid_name[slot->name_length + 4] = end;
        env_unset(pid_name);
}

static fn exec_coproc_reaped(bipolar pid)
{
        for (positive at = 0; at < exec_coproc_count; at++)
                if (exec_coprocs[at].pid == pid)
                        exec_coproc_unset(exec_coprocs + at);
}

static fn exec_coproc_drop_finished()
{
        positive into = 0;

        if (!exec_coproc_count)
                return;

        /* Wait only these children. wait4(-1) here would collect a lastpipe
           stage the pipeline still has to wait for. */
        for (positive at = 0; at < exec_coproc_count; at++)
        {
                positive status = 0;
                bipolar got = system_call_4(syscall(wait4),
                                            (positive)exec_coprocs[at].pid,
                                            (positive)address_of status,
                                            JOB_NO_HANG | JOB_UNTRACED |
                                                JOB_CONTINUED,
                                            0);

                if (got > 0)
                        job_child_changed(got, status);
        }

        for (positive at = 0; at < exec_coproc_count; at++)
        {
                bipolar pid = exec_coprocs[at].pid;
                positive found = shell_wait_find_job(pid);

                if (found < shell_wait_count &&
                    !(shell_wait_table[found].flags & SHELL_WAIT_DONE))
                {
                        exec_coprocs[into++] = exec_coprocs[at];
                        continue;
                }

                /*      The row stays. A coprocess that has finished is still
                        a job wait can be told about: the reference answers
                        `wait "$C_PID"` with the status it ended on and says
                        nothing, and plain `wait` walks over it in silence,
                        where dropping the row here made both of them say
                        "pid N is not a child of this shell" and answer 127.
                        Whoever waits it drops it, as for any other job.
                */
        }

        exec_coproc_count = into;
}

static fn exec_coproc_remember(string_address name, positive name_length,
                               bipolar pid)
{
        exec_coproc_slot address_to slot;

        if (exec_coproc_count >= EXEC_COPROC_LIVE)
                return;

        slot = exec_coprocs + exec_coproc_count++;
        memory_copy(slot->name, name, name_length);
        slot->name[name_length] = end;
        slot->name_length = name_length;
        slot->pid = pid;
}

/* A subreaper can acquire an orphan which has no shell bookkeeping row. Reap
   only exact PIDs absent from every owner table; broad wait4(-1) would steal
   the status of a foreground pipeline stage. */
static bool exec_child_tracked(bipolar pid)
{
        exec_foreground_frame address_to frame;
        bipolar own = system_call_1(syscall(getpid), 0);

        for (positive at = 0; at < shell_wait_count; at++)
                if (shell_wait_table[at].pid == pid &&
                    !(shell_wait_table[at].flags & SHELL_WAIT_DONE))
                        return true;
        for (positive at = 0; at < expand_substitutions_count; at++)
                if (expand_substitutions[at].child == pid)
                        return true;
        for (positive at = 0; at < exec_coproc_count; at++)
                if (exec_coprocs[at].pid == pid)
                        return true;
        for (positive at = 0; at < exec_disowned_children_count; at++)
                if (exec_disowned_children[at].owner == own &&
                    exec_disowned_children[at].pid == pid)
                        return true;
        for (frame = exec_foreground_frames; frame; frame = frame->previous)
                for (positive at = 0; at < frame->count; at++)
                        if (frame->children[at] == pid)
                                return true;

        return false;
}

static fn exec_unknown_children_reap()
{
        p8 text[FLOODLIGHT_CHILDREN_ROOM];
        bipolar got;
        p8 address_to at;
        p8 address_to stop;

        if (!floodlight_parent_supervised)
                return;

        got = floodlight_descendants_read(text, sizeof(text));
        if (got <= 0)
                return;
        at = text;
        stop = text + got;

        while (at < stop)
        {
                bipolar child = floodlight_child_next(address_of at, stop);

                if (child <= 0)
                        return;
                if (!exec_child_tracked(child))
                {
                        positive status = 0;

                        (void)system_wait4_retry(child, address_of status,
                                                JOB_NO_HANG, null);
                }
        }
}

/* Protection is needed only while a confined descendant can outlive its
   immediate launcher. Once the authenticated inventory is empty, restore
   shell-owned subreaper and dumpability state so later ordinary commands keep
   the process and ptrace semantics their caller supplied. */
static fn exec_parent_supervision_relax()
{
        if (!floodlight_parent_supervised)
                return;

        exec_unknown_children_reap();
        if (!floodlight_descendants_present())
                (void)floodlight_parent_release_subreaper();
}

static b32 exec_coproc(b32 index)
{
        parse_node address_to node = parse_nodes + index;
        string_address name = parse_words[node->word];
        positive name_length = string_length(name);
        p8 pid_name[EXEC_COPROC_NAME + 5];
        p8 written[32];
        b32 into[2];
        b32 from[2];
        bipolar pair[2];
        bipolar child;
        bool monitor = job_monitor();

        if (name_length > EXEC_COPROC_NAME)
                return string_report(log_error, 1, "coproc: %s: name too long\n", name);

        if (!job_reserve(1, false))
                return string_report(log_error, 2,
                                     "No room to retain coprocess\n");

        /*      A second coprocess is not a warning. Bash warned while it
                held one at a time; 5.3 holds as many as it is given and says
                nothing, and this has held eight all along, so the warning
                was a line the reference no longer writes and the only thing
                that separated `coproc A { ... }; coproc B { ... }` here from
                the same two lines there.
        */

        log_flush();

        if (system_pipe(address_of into, SHELL_PIPE_CLOSE_ON_EXEC) < 0)
                return string_report(log_error, 1, "coproc: no pipe\n");

        if (system_pipe(address_of from, SHELL_PIPE_CLOSE_ON_EXEC) < 0)
        {
                system_close(into[0]);
                system_close(into[1]);
                return string_report(log_error, 1, "coproc: no pipe\n");
        }

        job_child_watch();
        exec_node_parent_policy_prepare(node->left);
        /* A monitored coprocess needs the same two-sided setpgid race as a
           monitored pipeline stage. The Spark request has no child-side race
           and may already have exec'd before the parent can move it. */
        if (monitor)
                child = -1;
        else
                child = exec_stage_spawn(node->left, into[0], from[1]);

        if (child < 0)
                child = shell_clone();

        if (child == 0)
        {
                trap_default_all();
                exec_asynchronous = true;

                if (monitor)
                        job_child_group_enter(0);

                if (!exec_child_signals(!monitor, false))
                        system_call_1(syscall(exit_group), 126);
                exec_child_began();

                if (parse_nodes[node->left].kind == NODE_SUBSHELL)
                        parse_nodes[node->left].kind = NODE_GROUP;

                system_close(into[1]);
                system_close(from[0]);
                if (shell_child_fd_move(into[0], standard_input_descriptor) < 0 ||
                    shell_child_fd_move(from[1], standard_output_descriptor) < 0)
                        exec_child_leave(126);

                b32 status = exec_node(node->left);

                exec_child_leave(status);
        }

        if (monitor && child > 0)
                job_group_set(child, child);

        system_close(into[0]);
        system_close(from[1]);

        if (child < 0)
        {
                system_close(into[1]);
                system_close(from[0]);
                return string_report(log_error, 1, "coproc: cannot fork\n");
        }

        pair[0] = coproc_kept(from[0]);
        pair[1] = coproc_kept(into[1]);

        if (!shell_array_numbers(name, name_length, pair, 2))
                string_format(log_error, "coproc: %s: cannot assign\n", name);

        memory_copy(pid_name, name, name_length);
        memory_copy_end(pid_name + name_length, (string_address) "_PID", 4);
        bipolar_into_string(written, child);

        if (!env_assign(pid_name, written))
                string_format(log_error, "coproc: %s: cannot assign\n",
                              pid_name);

        // Registered the way a background command is, so that $! names it and
        // wait can be told what it answered.
        if (!shell_background_started(address_of child, 1, false, false))
                log_error(str("No room to retain coprocess\n"));

        job_started(address_of child, 1, monitor ? child : 0, index,
                    false, true);

        exec_coproc_remember(name, name_length, child);

        return 0;
}

/*
        lastpipe runs a final builtin, function or compound in this shell so
        `echo | read x` leaves x set. A path, or a name that is only a tool
        or a PATH lookup, is still a process of its own: lima forks /bin/true
        even with lastpipe on.
*/
static PURE bool exec_pipe_lastpipes(b32 index)
{
        parse_node address_to node = parse_nodes + index;
        string_address name;
        positive2 named;

        if (node->kind != NODE_SIMPLE)
                return true;

        if (!node->word_count)
                return true;

        if (!(parse_word_flags[node->word] & PARSE_WORD_LITERAL) ||
            (parse_word_flags[node->word] & PARSE_WORD_ASSIGNMENT))
                return true;

        name = parse_words[node->word];
        if (string_first_of(name, '/'))
                return false;

        named = string_hash_33_length(name);
        if (exec_function_slot(name, named) != positive_max)
                return true;

        return shell_command_named_hashed(name, named) ||
               exec_control_builtin(name, false);
}

static b32 exec_pipe(b32 first, positive count, bool background,
                     bool pipefail, bool invert)
{
        bipolar address_to children = null;
        positive children_room = 0;
        bipolar upstream = -1;
        b32 child = first;
        positive started = 0;
        b32 status = 0;
        positive at;
        bool spawn_failed = false;
        bool monitor = job_monitor();
        bool monitor_retained = false;
        bool lastpipe = !background && !monitor &&
                        shell_shopt_on(LASTPIPE);
        bool lastpipe_ran = false;
        b32 lastpipe_status = 0;
        b32 lastpipe_mark = exec_save_count;
        bipolar group = 0;
        positive address_to early_status = null;
        positive wanted = count;

        if (lastpipe)
        {
                if (count > positive_max / 2)
                        return string_report(log_error, 2,
                                             "No room for pipeline\n");
                wanted += count;
        }

        if (wanted > positive_max / sizeof(children[0]) ||
            !shell_array_room(children, children_room, wanted))
                return string_report(log_error, 2, "No room for pipeline\n");

        if ((background || monitor) &&
            !job_reserve(count, monitor && !background))
        {
                memory_free(children, children_room * sizeof(children[0]));
                return string_report(log_error, 2,
                                     "No room to retain pipeline\n");
        }

        if (background)
                job_child_watch();

        if (lastpipe)
        {
                early_status = (positive address_to)(children + count);
                for (at = 0; at < count; at++)
                        early_status[at] = positive_max;
        }

        /* Save before making a pipe: when the shell arrived with fd 0 closed,
           pipe may legitimately allocate that number. Saving at the final
           stage would then preserve the pipe instead of the original closed
           state. The save is CLOEXEC and so costs spawned stages no lifetime. */
        if (lastpipe)
        {
                b32 final = first;

                while (parse_nodes[final].next)
                        final = parse_nodes[final].next;

                if (!exec_save_fd(0, parse_nodes + final))
                {
                        memory_free(children,
                                    children_room * sizeof(children[0]));
                        return 2;
                }
        }

        while (child && started < count)
        {
                b32 ends[2];
                bipolar made;
                bool last = started + 1 >= count || !parse_nodes[child].next;

                ends[0] = -1;
                ends[1] = -1;

                /* Close-on-exec, because a spawned stage inherits a copy
                   of this shell's descriptors and a reader holding its own
                   write end waits for an end of file that never comes. The
                   forked path is unaffected: it duplicates the ends it wants
                   onto 0 and 1, and a duplicate does not carry the flag. */
                if (!last &&
                    system_pipe(ends, SHELL_PIPE_CLOSE_ON_EXEC) < 0)
                {
                        spawn_failed = true;
                        break;
                }

                log_flush();

                /* A spawned stage never runs a line of this shell, so only
                   the parent could put it in the job's group -- and by the
                   time the request has returned it may already have exec'd,
                   at which point setpgid is refused. Under the monitor the
                   stage is forked, which is slower and has two sides. */
                exec_node_parent_policy_prepare(child);
                made = monitor
                           ? -1
                           : exec_stage_spawn(child, upstream,
                                              last ? -1 : ends[1]);

                /* A final builtin, function or compound is the lastpipe
                   case: it runs here so its state survives. An external is
                   still a process of its own even when spark could not spawn
                   it. Running /bin/true in the parent keeps this shell as a
                   reader of the pipe, so a function that cats and then echoes
                   writes into a live pipe and answers 4 instead of SIGPIPE. */
                if (made < 0 && last && lastpipe &&
                    exec_pipe_lastpipes(child))
                {
                        bool tail = shell_tail_command;

                        /* Keep the caller's tested state: ! and conditional
                           lists suppress -e inside their pipeline, while an
                           untested final compound command must still stop at
                           its first failing simple command. Tail exec is
                           suppressed because the shell has pipeline
                           bookkeeping left to do when the stage returns. */
                        if (upstream >= 0)
                        {
                                if (shell_child_fd_move(upstream, 0) < 0)
                                {
                                        system_close(upstream);
                                        upstream = -1;
                                        lastpipe_status = 2;
                                        lastpipe_ran = true;
                                        log_error(str("Cannot connect pipeline\n"));
                                        break;
                                }

                        }

                        upstream = -1;
                        shell_tail_command = false;
                        exec_lastpipe_live = true;
                        {
                                exec_foreground_frame frame = {
                                    exec_foreground_frames, children,
                                    early_status, started};

                                exec_foreground_frames = &frame;
                                lastpipe_status = exec_node(child);
                                exec_foreground_frames = frame.previous;
                        }
                        exec_lastpipe_live = false;
                        shell_tail_command = tail;
                        lastpipe_ran = true;
                        child = parse_nodes[child].next;
                        break;
                }

                if (made < 0)
                        made = shell_clone();

                if (made == 0)
                {
                        trap_default_all();

                        if (monitor)
                                job_child_group_enter(group);

                        shell_child_default(SIGNAL_PIPE);
                        exec_asynchronous = background;
                        if (!exec_child_signals(
                                background && !monitor,
                                background && !monitor && upstream < 0))
                                system_call_1(syscall(exit_group), 126);
                        exec_child_began();
                        /* A pipeline stage is a child, and dash's jobs
                           listing is this shell's table, not the parent's.
                           Fork already copied the rows; drop them here
                           rather than walking them on every command. Bash
                           keeps the copy, which is why `jobs | wc` still
                           counts. Command substitution is not a pipeline
                           and still keeps the table: `$(jobs -p)`. */
                        if (!shell_bash_compat)
                                job_forget();
                        if (exec_pipe_omits_exit(parse_nodes[child].kind))
                                trap_omit_exit_set();
                        if (parse_nodes[child].kind == NODE_SUBSHELL)
                                parse_nodes[child].kind = NODE_GROUP;
                        shell_tail_command =
                            parse_nodes[child].kind == NODE_SIMPLE;

                        /* The unused read end can itself be fd 0 when the
                           caller closed stdin.  Close it before putting the
                           previous stage there; closing it afterwards would
                           close the newly installed input.  It cannot alias
                           upstream, which was open before this pipe existed. */
                        if (!last)
                                system_close(ends[0]);

                        if (upstream >= 0)
                        {
                                if (shell_child_fd_move(upstream, 0) < 0)
                                        exec_child_leave(126);
                        }

                        if (!last)
                        {
                                if (shell_child_fd_move(ends[1], 1) < 0)
                                        exec_child_leave(126);
                        }

                        status = exec_node(child);
                        exec_child_leave(status);
                }

                if (made < 0)
                {
                        if (upstream >= 0)
                                system_close(upstream);

                        if (!last)
                        {
                                system_close(ends[0]);
                                system_close(ends[1]);
                        }

                        upstream = -1;
                        spawn_failed = true;
                        break;
                }

                if (upstream >= 0)
                        system_close(upstream);

                upstream = -1;

                if (!last)
                {
                        system_close(ends[1]);
                        upstream = ends[0];
                }

                if (monitor)
                {
                        if (!group)
                                group = made;

                        job_group_set(made, group);
                }

                children[started++] = made;
                child = parse_nodes[child].next;
        }

        if (lastpipe)
        {
                exec_lastpipe_live = true;
                exec_redirect_restore(lastpipe_mark);
        }

        if (upstream >= 0)
                system_close(upstream);

        if (background && !spawn_failed)
        {
                if (!shell_background_started(children, started, pipefail,
                                              invert))
                {
                        log_error(str("No room to retain background pipeline\n"));
                        status = 2;
                }
                else
                        job_started(children, started, group, first, true,
                                    true);

                memory_free(children,
                            children_room * sizeof(children[0]));
                exec_lastpipe_live = false;
                return status;
        }

        //
        //      Every stage is waited for either way, because a pipeline that
        //      left a child unreaped would leave a zombie per turn of a loop.
        //      What pipefail changes is which of the answers is kept.
        //
        //      pipefail: the status is that of the rightmost command to exit
        //      non-zero, or zero if none did. Walking left to right, the last non-zero
        //      seen is the rightmost one, so one variable holds it.
        //
        b32 rightmost_failure = 0;
        bool stopped = false;
        positive stopped_by = 0;

        if (monitor)
        {
                /* Install the already-reserved wait rows before consuming
                   any status.  A short early stage may exit before a later
                   stage stops; retaining only afterwards would resurrect
                   that reaped PID as a live child. */
                if (job_hold(children, started))
                        monitor_retained =
                            job_retain(job_held, started, pipefail, invert,
                                       false);
                job_terminal_give(group);
        }

        for (at = 0; at < started; at++)
        {
                b32 got;
                positive raw = 0;
                bool changed = false;

                if (monitor)
                {
                        if (system_wait4_retry(children[at], address_of raw,
                                               JOB_UNTRACED, null) < 0)
                                got = 1;
                        else
                        {
                                changed = true;

                                if ((raw & 0xff) == 0x7f)
                                {
                                        stopped = true;
                                        stopped_by = (raw >> 8) & 0xff;
                                        got = 128 + (b32)stopped_by;
                                }
                                else
                                        got = wait_status_code(raw);
                        }
                }
                else if (early_status && early_status[at] != positive_max)
                {
                        raw = early_status[at];
                        got = wait_status_code(raw);
                }
                else
                        got = exec_wait_status(children[at], 0, address_of raw);

                if (monitor_retained && changed &&
                    (raw & 0xff) != 0x7f &&
                    (raw & 0xffff) != 0xffff)
                        shell_background_reaped(job_held[at], raw);

                if (got)
                        rightmost_failure = got;

                if (at + 1 == started)
                {
                        b32 stage = first;
                        positive step = 0;

                        status = got;

                        while (stage && step < at)
                        {
                                stage = parse_nodes[stage].next;
                                step++;
                        }

                        if (stage)
                                exec_wait_node = stage;

                        shell_child_death(children[at], raw, true);
                }

                // Every stage's answer, in the order they were written
                // in. The child ids are not wanted for anything else once
                // the last of them has been waited for, so the answers go
                // back into the vector that held them.
                children[at] = got;
        }

        /* The parent-run stage has no pid to wait for, but it is still the
           rightmost logical stage and gets the final slot of PIPESTATUS. */
        if (lastpipe_ran)
        {
                children[started] = lastpipe_status;
                started++;
                status = lastpipe_status;

                if (lastpipe_status)
                        rightmost_failure = lastpipe_status;
        }

        if (monitor)
                job_terminal_give(job_shell_group);

        /* A pipeline stopped in front is a job from that moment on: it is
           listed, it is what `fg` means, and the number it answers with is the
           one POSIX gives a command that stopped rather than ended. */
        if (stopped && monitor_retained)
        {
                positive number = job_started(job_held, started, group, first,
                                              true, false);
                positive slot = number ? job_find(number, false) : job_count;

                memory_free(children, children_room * sizeof(children[0]));

                exec_lastpipe_live = false;

                if (slot >= job_count)
                        return 128 + (b32)stopped_by;

                job_table[slot].state = JOB_STOPPED;
                job_table[slot].stopped_by = stopped_by;
                job_table[slot].reported = true;
                job_line(log, job_table + slot, false);
                log_flush();

                return 128 + (b32)stopped_by;
        }

        if (monitor_retained)
                shell_wait_drop(job_held[started - 1]);

        exec_pipe_status_pending = false;
        exec_pipe_status_publish(children, started);

        if (rightmost_failure && pipefail)
                status = rightmost_failure;

        if (spawn_failed)
        {
                log_error(str("Cannot start pipeline\n"));
                status = 2;
        }

        memory_free(children, children_room * sizeof(children[0]));

        exec_lastpipe_live = false;

        return status;
}

// How many commands a pipeline has, or positive_max when there is no
// counting them.
static positive exec_pipeline_count(b32 first)
{
        positive count = 0;

        for (b32 stage = first; stage; stage = parse_nodes[stage].next)
        {
                if (count == positive_max)
                {
                        log_error(str("Pipeline too long\n"));
                        return positive_max;
                }

                count++;
        }

        return count;
}

/* A simple command always replaces Bash's PIPESTATUS, but almost no command
   reads it. Keep the one integer in registers/data until an environment or
   array reader actually asks; that reader calls the public materializer
   below. A real pipeline publishes its already-built vector directly. */
static fn exec_pipe_status_publish(bipolar address_to values, positive count)
{
        bool locked;

        /* `readonly PIPESTATUS` with no value stays an unset readonly array
           in Bash. An existing readonly vector remains shell-owned and is
           refreshed normally, so only the absent case refuses creation. */
        locked = env_readonly("PIPESTATUS");

        if (locked && !shell_array_length("PIPESTATUS", 10))
                return;

        /* Ordinary writes must respect readonly. This is the shell updating
           its own status register, which Bash permits once that register has
           a value; lift and restore only that attribute around the common
           array writer rather than adding a second internal write engine. */
        if (locked)
                shell_variable_attribute_set("PIPESTATUS", 10, 0,
                                             SHELL_ARRAY_READONLY);

        shell_status_array_numbers("PIPESTATUS", 10, values, count);

        if (locked)
                shell_variable_attribute_set("PIPESTATUS", 10,
                                             SHELL_ARRAY_READONLY, 0);
}

fn exec_pipe_status_wanted()
{
        bipolar value;

        if (!exec_pipe_status_pending)
                return;

        exec_pipe_status_pending = false;
        value = exec_pipe_status_value;
        exec_pipe_status_publish(address_of value, 1);
}

static fn exec_pipe_status_one(b32 status)
{
        exec_pipe_status_value = status;
        exec_pipe_status_pending = true;
}

/*
        The time reserved word.

        What it measures is a whole pipeline and not a command, which is why
        it is grammar and not a utility: an external /usr/bin/time can only
        ever time one program, and "time a | b" is two.

        Three numbers come out of it. The real one is the monotonic clock,
        which nothing else can move; the two processor ones are this shell's
        own use plus every child it has reaped, because the work a pipeline
        did in its stages is work the pipeline did.
*/

// The front of struct rusage, which is where the two times are. The rest is
// counters nobody here asks after, and the kernel writes all of it.
typedef struct
{
        p64 user_seconds;
        p64 user_microseconds;
        p64 system_seconds;
        p64 system_microseconds;
        p64 counters[32];
} time_usage;

#define TIME_USAGE_SELF 0
#define TIME_USAGE_CHILDREN (-1)
#define TIME_MICROSECONDS 1000000

typedef struct
{
        timespec real;
        positive user;
        positive system;
} time_reading;

static fn time_now(time_reading address_to into)
{
        time_usage self;
        time_usage children;

        memory_fill(address_of self, 0, sizeof(self));
        memory_fill(address_of children, 0, sizeof(children));
        memory_fill(into, 0, sizeof(address_to into));

        system_call_2(syscall(clock_gettime), READ_CLOCK_MONOTONIC,
                      (positive)address_of into->real);
        system_call_2(syscall(getrusage), (positive)TIME_USAGE_SELF,
                      (positive)address_of self);
        system_call_2(syscall(getrusage),
                      (positive)(bipolar)TIME_USAGE_CHILDREN,
                      (positive)address_of children);

        into->user = (positive)self.user_seconds * TIME_MICROSECONDS +
                     (positive)self.user_microseconds +
                     (positive)children.user_seconds * TIME_MICROSECONDS +
                     (positive)children.user_microseconds;
        into->system = (positive)self.system_seconds * TIME_MICROSECONDS +
                       (positive)self.system_microseconds +
                       (positive)children.system_seconds * TIME_MICROSECONDS +
                       (positive)children.system_microseconds;
}

// What went by between two readings, in microseconds. A clock that went
// backwards is nothing, rather than an enormous number.
static CONST positive time_apart(positive after, positive before)
{
        return after > before ? after - before : 0;
}

static PURE positive time_real_apart(time_reading address_to after,
                                     time_reading address_to before)
{
        bipolar seconds = (bipolar)after->real.tv_sec -
                          (bipolar)before->real.tv_sec;
        bipolar nanoseconds = (bipolar)after->real.tv_nsec -
                              (bipolar)before->real.tv_nsec;
        bipolar total = seconds * TIME_MICROSECONDS + nanoseconds / 1000;

        return total > 0 ? (positive)total : 0;
}

// Ten to the places, for every number of places a format may ask for.
static CONST positive time_scale(positive places)
{
        static const positive powers[] = {1,     10,     100, 1000,
                                          10000, 100000, 1000000};

        return powers[places];
}

/*
        One time, rounded to the places asked for and written out.

        Rounding rather than cutting is what Bash's printf does, and the
        difference shows at once: five microseconds at five places is one
        hundred-thousandth of a second and not nothing.
*/
static fn time_number(positive microseconds, positive places, bool minutes)
{
        positive scale = time_scale(places);
        positive divisor = TIME_MICROSECONDS / scale;
        positive scaled = (microseconds + divisor / 2) / divisor;
        positive whole = scaled / scale;
        positive fraction = scaled % scale;
        p8 shown[32];
        positive written;

        if (minutes)
        {
                written = bipolar_into_string(shown, (bipolar)(whole / 60));
                exec_built_add(shown, written);
                exec_built_add((string_address) "m", 1);
                whole %= 60;
        }

        written = bipolar_into_string(shown, (bipolar)whole);
        exec_built_add(shown, written);

        if (places)
        {
                exec_built_add((string_address) ".", 1);
                written = positive_into_padded(shown, fraction, places, '0');
                exec_built_add(shown, written);
        }

        if (minutes)
                exec_built_add((string_address) "s", 1);
}

/*
        TIMEFORMAT, written out.

        %R %U %S are the three times and %P the share of the elapsed time that
        went on a processor. A digit in front of the letter says how many
        places follow the point, an l asks for the minutes to be taken out in
        front, and %% is a percent. Everything else is a byte of the line.

        A letter that is none of those is refused and nothing at all is
        written, which is what Bash does: half a timing is worse than none,
        and a format nobody can read is a mistake somebody wants told.
*/
static bool time_formatted(string_address format, positive real,
                           positive user, positive system)
{
        string_address at = format;

        exec_built.used = 0;

        while (string_get(at))
        {
                positive places = 3;
                bool given = false;
                bool minutes = false;
                p8 letter;

                if (string_not(at, '%'))
                {
                        exec_built_add(at, 1);
                        at++;
                        continue;
                }

                at++;

                // A percent with nothing behind it is a percent.
                if (!string_get(at))
                {
                        exec_built_add((string_address) "%", 1);
                        break;
                }

                if (byte_is_digit(string_get(at)))
                {
                        places = (positive)(string_get(at) - '0');
                        given = true;
                        at++;

                        // Microseconds are as fine as the two clocks are, so
                        // asking for more places asks for zeroes.
                        if (places > 6)
                                places = 6;
                }

                if (string_is(at, 'l'))
                {
                        minutes = true;
                        at++;
                }

                letter = string_get(at);

                if (letter == 'R')
                        time_number(real, places, minutes);
                else if (letter == 'U')
                        time_number(user, places, minutes);
                else if (letter == 'S')
                        time_number(system, places, minutes);
                else if (letter == '%' && !given && !minutes)
                        exec_built_add((string_address) "%", 1);
                else if (letter == 'P' && !given && !minutes)
                {
                        /*
                                Two times over a third, in hundredths of a
                                percent so that the places come out of one
                                division. A third of nothing is nothing:
                                elapsed time can be under the clock's own
                                resolution and then there is no share to give.
                        */
                        positive spent = user + system;
                        positive parts = real ? spent * 10000 / real : 0;
                        p8 shown[32];
                        positive written =
                            bipolar_into_string(shown, (bipolar)(parts / 100));

                        exec_built_add(shown, written);
                        exec_built_add((string_address) ".", 1);
                        written = positive_into_padded(shown, parts % 100, 2,
                                                       '0');
                        exec_built_add(shown, written);
                }
                else
                {
                        p8 shown[2];

                        shown[0] = letter;
                        shown[1] = end;

                        return string_report(log_error, false, "TIMEFORMAT: `%s': invalid format character\n", shown);
                }

                at++;
        }

        exec_built_add((string_address) "\n", 1);

        return true;
}

static fn time_written(bool posix, positive real, positive user,
                       positive system)
{
        string_address format = env_get((const_string) "TIMEFORMAT");

        if (posix)
                format = (string_address) "real %2R\nuser %2U\nsys %2S";
        else if (!format)
                format = (string_address) "\nreal\t%3lR\nuser\t%3lU\nsys\t%3lS";

        // An empty TIMEFORMAT asks for no timing at all, which is not the
        // same as asking for the usual one.
        if (!string_get(format))
                return;

        if (!time_formatted(format, real, user, system))
                return;

        // Whatever the timed command wrote comes first. It has already
        // happened; only the buffer is holding it back.
        log_flush();
        log_error(exec_built.bytes, exec_built.used);
}

static b32 exec_time(b32 index)
{
        parse_node address_to node = parse_nodes + index;
        bool tested = exec_tested;
        time_reading before;
        time_reading after;
        b32 status;

        time_now(address_of before);

        // A bang in front of a time inverts what the whole of it answers, so
        // nothing inside it is what errexit is looking at.
        if (node->op)
                exec_tested = true;

        status = node->left ? exec_node(node->left) : 0;

        exec_tested = tested;
        time_now(address_of after);

        time_written(node->flags,
                     time_real_apart(address_of after, address_of before),
                     time_apart(after.user, before.user),
                     time_apart(after.system, before.system));

        if (exec_line_aborted())
                return shell_status;

        if (node->op)
                status = status ? 0 : 1;

        shell_status = status;

        return status;
}

/* The compound whose own redirection failed last. bash hands that failure
   through a `!` untouched: `! { :; } < missing` is 1, not 0. */
static b32 exec_redirect_failed_node;

static b32 exec_pipeline(b32 index)
{
        parse_node address_to node = parse_nodes + index;
        bool tested = exec_tested;
        positive count = exec_pipeline_count(node->left);
        b32 status;

        if (count == positive_max)
        {
                shell_status = 2;
                return 2;
        }

        if (node->flags)
        {
                exec_tested = true;
                exec_redirect_failed_node = 0;
        }

        /*
                bash raises DEBUG for each simple command of a pipeline in
                the shell itself, before that stage is forked; a stage that
                is a group or a subshell runs in its child, where the trap
                does not reach. A lastpipe stage runs here and raises it
                for itself.
        */
        if (count > 1 && trap_debug_here)
        {
                bool lastpipe = !job_monitor() && shell_shopt_on(LASTPIPE);

                for (b32 stage = node->left; stage;
                     stage = parse_nodes[stage].next)
                        if (parse_nodes[stage].kind == NODE_SIMPLE &&
                            !(lastpipe && !parse_nodes[stage].next))
                                exec_debug_before(parse_nodes + stage);
        }

        status = count > 1
                     ? exec_pipe(node->left, count, false, shell_pipefail(),
                                 false)
                     : exec_node(node->left);

        exec_tested = tested;

        if (exec_line_aborted())
                return shell_status;

        if (node->flags)
        {
                shell_status = shell_bash_compat && count == 1 &&
                                       exec_redirect_failed_node == node->left
                                   ? status
                                   : status ? 0 : 1;

                return shell_status;
        }

        shell_status = status;
        exec_errexit(status);

        return status;
}

static b32 exec_and_or(b32 index)
{
        b32 child = parse_nodes[index].left;
        bool tested = exec_tested;
        b32 status = 0;

        while (child)
        {
                b32 op = parse_nodes[child].op;

                if (!(op == OP_AND_IF && status != 0) &&
                    !(op == OP_OR_IF && status == 0))
                {
                        exec_tested = parse_nodes[child].next ? true : tested;
                        status = exec_node(child);
                }

                if (exec_signal)
                        break;

                child = parse_nodes[child].next;
        }

        exec_tested = tested;

        return status;
}

static b32 exec_background(b32 index)
{
        b32 body = parse_nodes[index].left;
        bipolar child;

        /* A singleton gets no semantic value from its background-marker
           AND-OR wrapper. Launching the body itself lets an external command
           tail-exec into the PID published as $!, and lets a pipeline publish
           the PID of its last stage as POSIX requires. */
        if (body && !parse_nodes[body].next)
        {
                if (parse_nodes[body].kind == NODE_PIPELINE)
                {
                        positive count =
                            exec_pipeline_count(parse_nodes[body].left);

                        if (count == positive_max)
                                return 2;

                        return exec_pipe(parse_nodes[body].left, count, true,
                                         shell_pipefail(),
                                         parse_nodes[body].flags);
                }

                index = body;
        }

        job_child_watch();
        child = exec_spawn_node(index, true);

        if (child < 0)
                return 1;

        if (!shell_background_started(address_of child, 1, false, false))
                return string_report(log_error, 2, "No room to retain background command\n");

        job_started(address_of child, 1, job_monitor() ? child : 0, index,
                    false, true);

        return 0;
}

static b32 exec_list(b32 index)
{
        b32 child = parse_nodes[index].left;
        b32 status = 0;

        while (child)
        {
                bool background = parse_nodes[child].kind == NODE_ANDOR &&
                                  parse_nodes[child].flags;

                // Starting a command in the background is a command with a
                // status of its own, which is what $? reads next. Every
                // other kind of node writes shell_status on its way out.
                if (background)
                        status = shell_status = exec_background(child);
                else
                        status = exec_node(child);

                if (exec_signal)
                        break;

                child = parse_nodes[child].next;
        }

        return status;
}

static inline INLINE fn exec_expansion_done(shell_mark expanded,
                                            positive substitutions)
{
        shell_store_rewind(address_of expand_store, expanded);

        if (expand_substitutions_ever &&
            expand_substitutions_count != substitutions)
                shell_substitutions_close(substitutions);
}

/*
        Every node ends at a command boundary, which is where a trap that
        arrived is allowed to run. A simple command is the usual one; a
        subshell or a loop is one too, and checking only the simple ones left
        a trap waiting behind a "( ... )" until whatever came after it.
*/
static b32 exec_node(b32 index)
{
        /* -n still parses the entire physical program before it gets here.
           Once a command in that program enables it, later sibling nodes are
           skipped as Bash and dash skip the rest of the already parsed list. */
        if (!shell_is_interactive && (shell_options & SHELL_NOEXEC))
                return shell_status;

        b32 status = exec_node_kind(index);

        /* Signals are rare and node boundaries are not. Keep the volatile
           byte read on the hot path, but do not enter exec_traps merely to
           discover that no handler has written it. Checking abort state is
           likewise unnecessary until there is a trap to run. */
        if (trap_caught && !trap_inside && !exec_line_aborted())
        {
                exec_traps();

                // A return the action made is the function's answer.
                if (exec_signal == EXEC_SIGNAL_RETURN)
                        status = shell_status;
        }

        exec_parent_supervision_relax();

        return status;
}

static b32 exec_node_kind(b32 index)
{
        /* The count is read first and it is an ordinary word: a shell with
           nothing in the background never touches the volatile flag beside
           it, so the notice costs one load and a branch not taken. */
        if (job_child_news)
                job_notice();

        parse_node address_to node;
        shell_mark expanded;
        b32 mark;
        b32 status;

        if (!index)
                return shell_status;

        node = parse_nodes + index;

        /*
                What a process substitution opened belongs to the command that
                was handed the path, and to nothing inside it: a function given
                /dev/fd/N as an argument may not open it until its third
                command, so the mark is taken here and given back only when
                this whole command is done with.

                Until something makes one there is nothing to mark, which is
                every command of almost every script.
        */
        positive substitutions =
            expand_substitutions_ever ? expand_substitutions_count : 0;

        if (node->kind == NODE_SIMPLE)
        {
                // Before the words are expanded, which is where Bash runs it
                // and the only place the action can use argv of its own.
                if (trap_debug_here)
                        exec_debug_before(node);

                bool expand_scratch = node->redirect_count != 0;
                b32 word_at = node->word;
                b32 word_stop = word_at + node->word_count;

                while (!expand_scratch && word_at < word_stop)
                {
                        if (!(parse_word_flags[word_at] & PARSE_WORD_LITERAL))
                                expand_scratch = true;
                        word_at++;
                }

                if (expand_scratch)
                        expanded = shell_store_mark(address_of expand_store);

                status = exec_simple(index);

                if (expand_scratch)
                        exec_expansion_done(expanded, substitutions);
                else if (expand_substitutions_ever &&
                         expand_substitutions_count != substitutions)
                        shell_substitutions_close(substitutions);

                shell_status = status;

                /* Bash exposes a pipeline vector only until the next simple
                   command. Expansion of that command has already read the old
                   vector; publishing its one answer here gives assignments
                   and commands the same lifetime rule. Do this before ERR and
                   EXIT traps so they see the failing command's answer. The
                   pipeline overwrites the transient value after a returning
                   parent-run lastpipe stage. */
                if (shell_bash_compat)
                        exec_pipe_status_one(status);

                /* A function the command called ran lines of its own; an ERR
                   action for the call reads the line of the call. */
                if (parse_nodes[index].line)
                        exec_line = parse_nodes[index].line;
                exec_errexit(status);

                /* failglob (and other command-level expansion failures)
                   abort the rest of this physical line and of a function
                   body, after errexit has had its look. The next line of
                   a script still runs. */
                if (expand_failed && !exec_line_aborted())
                        exec_expand_input_error();

                return status;
        }

        exec_line = node->line;

        if (node->kind == NODE_LIST)
                return exec_list(index);

        /* The only command of a body -- `if c; then cmd & fi`, a loop's or
           a group's -- is an and-or node with no list around it, so the
           ampersand that the list would have read is read here. */
        if (node->kind == NODE_ANDOR)
                return node->flags ? (shell_status = exec_background(index))
                                   : exec_and_or(index);

        if (node->kind == NODE_PIPELINE)
                return exec_pipeline(index);

        // A time carries no redirections of its own: what it measures does.
        if (node->kind == NODE_TIME)
                return exec_time(index);

        if (node->kind == NODE_FUNCTION)
                return exec_define(index);

        // Everything left is a compound command, and every one of them can
        // carry redirections of its own. Its expansions live until that
        // command returns: a for list and case subject span child commands,
        // while a redirect target dies as soon as its descriptor is open.
        expanded = shell_store_mark(address_of expand_store);
        mark = exec_save_count;
        token_used = 0;

        if (node->redirect_count && !exec_redirect_apply(index))
        {
                exec_redirect_restore(mark);
                exec_expansion_done(expanded, substitutions);

                shell_status = (exec_line_aborted() ? shell_status : exec_redirect_status ? exec_redirect_status : 1);
                exec_redirect_failed_node = index;
                /* A compound whose redirection failed is a command that
                   failed, and set -e and the ERR trap see it as one. */
                exec_errexit(shell_status);
                return shell_status;
        }

        exec_compound_depth++;

        if (trap_debug_here &&
            (node->kind == NODE_ARITHMETIC || node->kind == NODE_CONDITIONAL ||
             node->kind == NODE_CASE))
                exec_debug_before(node);

        if (node->kind == NODE_ARITHMETIC)
                status = exec_arithmetic_command(index);
        else if (node->kind == NODE_CONDITIONAL)
                status = exec_conditional(index);
        else if (node->kind == NODE_IF)
                status = exec_if(index);
        else if (node->kind == NODE_WHILE)
                status = exec_loop(index, false);
        else if (node->kind == NODE_UNTIL)
                status = exec_loop(index, true);
        else if (node->kind == NODE_FOR)
                status = exec_for(index, false);
        else if (node->kind == NODE_SELECT)
                status = exec_for(index, true);
        else if (node->kind == NODE_COPROC)
                status = exec_coproc(index);
        else if (node->kind == NODE_CFOR)
                status = exec_cfor(index);
        else if (node->kind == NODE_CASE)
                status = exec_case(index);
        else if (node->kind == NODE_SUBSHELL)
        {
                bipolar made = exec_spawn_node(parse_nodes[index].left, false);

                status = job_monitor() ? job_foreground_child(made, index)
                                       : exec_child_status(made);
        }
        else
                status = exec_node(node->left);

        exec_compound_depth--;

        exec_redirect_restore(mark);

        if (shell_bash_compat && exec_compound_depth == 0)
                shell_child_death_flush();
        /* lima bash 5.2 leaves process-substitution write ends open after a
           brace group. `>(sed > file)` has not seen EOF, so a later cat of
           that file is still empty; the writer finishes when this shell
           itself leaves. A simple command still closes them, which is why
           `echo z > >(cat > written)` settles before sleep. */
        if (shell_bash_compat && node->kind == NODE_GROUP)
                shell_store_rewind(address_of expand_store, expanded);
        else
                exec_expansion_done(expanded, substitutions);

        // A return unwinding through this compound carries its own status,
        // which the loop's last body status must not replace.
        if (exec_signal == EXEC_SIGNAL_RETURN)
                status = shell_status;
        shell_status = status;

        if (conditional_syntax)
                conditional_syntax_finish();

        /* The three compounds bash runs as one command of their own each
           replace its PIPESTATUS, just as a simple command does: after
           `false | true; ((0))` it is (1), not the pipeline's (1 0). */
        if (node->kind == NODE_SUBSHELL || node->kind == NODE_ARITHMETIC ||
            node->kind == NODE_CONDITIONAL)
        {
                if (shell_bash_compat)
                        exec_pipe_status_one(status);
                exec_errexit(status);
        }

        return status;
}

static b32 exec_depth;

/*
        A tree, walked.

        eval, . and a trap action are programs run from inside one that is
        already running, and what they leave behind is not theirs to throw
        away: the arena holds the items of every for loop further out and the
        saved descriptors hold the redirections of every command further out.
        Both used to start over here, so an eval inside a loop walked over the
        value the loop was standing on and a redirection around it was never
        put back.
*/
fn exec_program(b32 root)
{
        shell_mark kept_arena = shell_store_mark(address_of exec_store);
        b32 kept_saves = exec_save_count;

        exec_signal = EXEC_SIGNAL_NONE;
        exec_signal_level = 0;

        // Only when something was started in the background. This runs at the
        // top of every complete command, so a script that never forked one
        // was paying a wait4 per line to be told it has no children.
        if (!exec_depth && (shell_wait_count || job_count))
        {
                job_reap();
                job_report();
        }

        exec_depth++;

        // A one-command list has no NODE_LIST wrapper. Its trailing ampersand
        // still belongs to the list, and must not become synchronous merely
        // because no second command followed it on the same physical line.
        if (root && parse_nodes[root].kind == NODE_ANDOR &&
            parse_nodes[root].flags)
                shell_status = exec_background(root);
        else
                exec_node(root);

        exec_depth--;

        shell_store_rewind(address_of exec_store, kept_arena);
        exec_save_count = kept_saves;
}
