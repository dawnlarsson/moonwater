#define STANDARD_APPLETS
#include "../src/lib.util.c"
#include "../src/moonwater/spark.c"
#include "../src/sh/shell.c"

/*
        A complete -c program which needs no state once the caller has ruled
        out inherited startup work and function overrides.

        Comments are only recognised after blanks/newlines because reaching
        this routine already means no word has begun.  `:#text` is therefore
        a command name and is deliberately rejected, while `: # text` is the
        no-op followed by a comment.  Only one literal colon, true or false
        command is accepted; separators, operands and further commands belong
        to the real parser even when their eventual combined status would be
        the same.
*/
static bool shell_command_literal_status(string_address command,
                                         b32 address_to status)
{
        bool command_seen = false;
        b32 answer = 0;
        string_address step = command;

        if (!step)
                return false;

        while (true)
        {
                step += string_span_of_set(step, " \t\n");

                if (*step == '#')
                {
                        step = string_first_of_or_end(step, '\n');
                        continue;
                }

                if (!*step)
                {
                        address_to status = answer;
                        return true;
                }

                if (command_seen)
                        return false;
                positive length = *step == ':' ? 1
                    : !string_compare_max(step, "true", 4) ? 4
                    : !string_compare_max(step, "false", 5) ? 5 : 0;
                if (!length || (step[length] && step[length] != ' ' &&
                                step[length] != '\t' && step[length] != '\n'))
                        return false;
                command_seen = true;
                answer = length == 5;
                step += length;
        }
}

static bool shell_start_parameters(string_address address_to arguments,
                                   positive first, positive count)
{
        if (!shell_array_room(shell_argv, shell_argv_room, count + 1))
                return false;

        if (count)
                memory_copy_apart(shell_argv, arguments + first,
                                  count * sizeof(shell_argv[0]));
        shell_argv[count] = null;

        return shell_parameters_set(shell_argv, count);
}

/* A complete source which is already resident in memory can identify its
   final physical line before parsing it.  Grant the executor's one-command
   tail privilege only around that line, and restore an outer reader's state
   if the command is a builtin, compound command, or failed exec. */
static fn shell_run_known_line(string_address line, bool final)
{
        bool held = shell_tail_line_requested;

        if (final && exec_tail_line_safe())
                shell_tail_line_requested = true;
        run_line(line);
        shell_tail_line_requested = held;
}

static positive shell_run_complete_lines(p8 address_to text, positive length,
                                         bool command_string)
{
        positive at = 0;

        shell_verbose_from_string = command_string;

        while (at < length)
        {
                positive left = length - at;
                p8 address_to newline = memory_first_of(text + at, '\n', left);

                if (!newline)
                        break;

                address_to newline = end;

                {
                        string_address ready = text + at;
                        b32 history_action = HISTORY_EXPAND_RUN;

                        // What a person typed, and only that: an eval or a
                        // sourced file is a line this shell wrote for itself.
                        if (shell_history_recording())
                        {
                                history_action = history_expand_line(
                                    ready, address_of ready);
                                if (history_action >= HISTORY_EXPAND_RUN)
                                        history_remember(ready);
                        }

                        if (history_action == HISTORY_EXPAND_RUN)
                        {
                                positive next = (positive)(newline - text) + 1;
                                positive left = length - next;

                                shell_line_has_more =
                                    memory_span_byte(text + next, '\n', left) < left;

                                shell_verbose_line(ready);
                                shell_run_known_line(
                                    ready,
                                    command_string && !shell_line_has_more);
                                shell_line_has_more = false;
                        }
                }
                at = (positive)(newline - text) + 1;

                if (!command_string && shell_onecmd_on() &&
                    !shell_reading_more())
                        return at;
        }

        return at;
}

typedef struct
{
        positive next;
        bool command;
        bool from_stdin;
        bool finished;
        b32 interactive;
        b32 monitor;
        b32 status;
} shell_invocation;

/*
        The GNU long options, in the order Bash lists them.

        This is one table and it is read twice: the parser accepts these
        names, and the usage banner below prints them. A name this shell
        does not take must therefore not be listed, and a name it lists it
        must take -- the two lists cannot drift apart because there is only
        one.

        A name marked true takes the following word. Bash reads the whole
        run of these before it reads any letters, and only the leading run:
        the first word that is not a long option ends the phase, so
        `sh -x --posix` never reaches the name and complains about the
        letter '-' instead. That ordering is observable and is why the
        phases are separate here rather than folded into one loop.
*/
typedef struct
{
        string_address name;
        bool takes_word;
} shell_long_option;

/*
        -O names, kept rather than acted on.

        A shopt asked for on the command line is not applied where it is
        read: interactive startup and posix mode both write over parts of
        the same state afterwards, so Bash holds the names until that is
        done and only then turns them on. It is observable through the one
        that is not a name at all -- `sh -O bogus --nonsense` complains
        about --nonsense, because the shopt list has not been looked at
        yet, and the shell that complains about bogus first leaves a
        different status behind.

        Sixteen is more than the option walk ever asks for; past that the
        name is applied where it was read, which is the old behaviour and
        wrong only in the ordering nothing that deep is looking at.
*/
#define SHELL_SHOPT_ASKED 16

static struct
{
        string_address name;
        bool on;
} shell_shopt_asked[SHELL_SHOPT_ASKED];
static positive shell_shopt_asked_count;

/* One name, applied. Answers false having said which name it was. */
static bool shell_shopt_apply(string_address self, string_address name, bool on)
{
        positive item = shell_shopt_find(name);

        if (item >= SHELL_SHOPT_NAMES)
                return string_report(log_error, false,
                    "%s: line 0: %s: invalid shell option name\n", self, name);
        if (item == SHELL_SHOPT_EXPAND_ALIASES)
                shell_alias_startup_told = true;
        if (on)
                shell_shopt_state |= (positive)1 << item;
        else
                shell_shopt_state &= ~((positive)1 << item);
        return true;
}

/* The held names, in the order they were written. */
static bool shell_shopt_asked_apply(string_address self)
{
        for (positive at = 0; at < shell_shopt_asked_count; at++)
                if (!shell_shopt_apply(self, shell_shopt_asked[at].name,
                                       shell_shopt_asked[at].on))
                        return false;
        shell_shopt_asked_count = 0;
        return true;
}

//      Which file an interactive bash reads before its first prompt: the one
//      --rcfile or --init-file named, none under --norc, and ~/.bashrc
//      otherwise.
static string_address shell_rcfile;
static bool shell_norc;

static const shell_long_option shell_long_options[] = {
    {"debug", false}, {"debugger", false}, {"dump-po-strings", false},
    {"dump-strings", false}, {"help", false}, {"init-file", true},
    {"login", false}, {"noediting", false}, {"noprofile", false},
    {"norc", false}, {"posix", false}, {"pretty-print", false},
    {"rcfile", true}, {"restricted", false}, {"verbose", false},
    {"version", false},
};

/*
        What Bash writes when it is handed an option it has not got.

        A whole page, not a line: this is the one place a shell prints its
        own usage, and the caller reads it on standard error under the
        complaint. Whoever ran the shell wrote its name, so that name is
        what the banner says -- `./bash`, `-bash` or a path, whatever
        argv[0] held.

        The letters are this shell's own set letters and the names are the
        table above, so the banner cannot advertise a surface the parser
        refuses.
*/
static fn shell_usage_said(writer write, string_address self)
{
        string_format(write,
                      "Usage:\t%s [GNU long option] [option] ...\n"
                      "\t%s [GNU long option] [option] script-file ...\n"
                      "GNU long options:\n", self, self);

        for (positive at = 0; at < array_count(shell_long_options); at++)
                string_format(write, "\t--%s\n", shell_long_options[at].name);

        write(str("Shell options:\n"
                  "\t-ilrsD or -c command or -O shopt_option"
                  "\t\t(invocation only)\n"
                  "\t-abefhkmnptuvxBCEHPT or -o option\n"));
}

/*
        The complaint, and what follows it.

        Bash reports an unknown option through the same routine every other
        error goes through, and that routine leaves at once when errexit is
        already on -- before the usage is written, and with 1 rather than
        the 2 a usage error otherwise carries. So `sh -eZ` and `sh -Ze` are
        the same mistake and end differently, because in one of them the -e
        was read first. dash says it in its own words, has no banner, and
        does not have that errexit rule; it also spells a plus as a minus.
*/
static b32 shell_invocation_refused(string_address self, p8 sign, p8 letter)
{
        //      The sign is part of what is quoted back, and dash quotes a
        //      plus back as a minus. The two bytes are made into a word
        //      because the formatter takes strings and not characters.
        p8 said[3] = {sign, letter, end};

        if (!shell_bash_compat)
        {
                said[0] = '-';
                return string_report(log_error, 2, "%s: 0: Illegal option %s\n",
                                     self, said);
        }

        string_format(log_error, "%s: %s: invalid option\n", self, said);
        if (shell_options & SHELL_FLAG('e'))
                return 1;
        shell_usage_said(log_error, self);
        return 2;
}

/*
        The leading run of GNU long options.

        Returns the index of the first word that is not one. A word that
        begins with two dashes and is not in the table is refused here and
        the shell is over; a word that begins with one dash and is not in
        the table is a letter cluster and simply ends the phase. That is
        Bash's own asymmetry: `-login` is `--login`, and `-x` is `-x`.
*/
static positive shell_start_long_options(string_address address_to arguments,
                                         positive count,
                                         shell_invocation *invocation)
{
        positive at = 1;

        while (at < count)
        {
                string_address word = arguments[at];
                string_address name = word;
                bool doubled = false;
                positive found;

                if (!word || word[0] != '-')
                        break;
                if (word[1] == '-' && word[2])
                {
                        doubled = true;
                        name++;
                }
                name++;

                for (found = 0; found < array_count(shell_long_options); found++)
                        if (word_is(name, shell_long_options[found].name))
                                break;

                if (found == array_count(shell_long_options))
                {
                        if (!doubled)
                                break;
                        string_format(log_error, "%s: %s: invalid option\n",
                                      arguments[0], word);
                        shell_usage_said(log_error, arguments[0]);
                        invocation->status = 2;
                        invocation->finished = true;
                        return at;
                }

                if (shell_long_options[found].takes_word && at + 1 >= count)
                {
                        invocation->status = string_report(log_error, 2,
                            "%s: %s: option requires an argument\n",
                            arguments[0], shell_long_options[found].name);
                        invocation->finished = true;
                        return at;
                }

                //      What each name does. The ones with nothing to do
                //      here are taken all the same: --debug and --debugger
                //      want a debugger this shell does not carry,
                //      --pretty-print wants a script file rather than a
                //      command, --noprofile and --norc opt out of files
                //      only a login or interactive shell reads, and
                //      --noediting turns off a line editor this shell does
                //      not put between a terminal and its reader. Refusing
                //      a name the banner advertises would be worse than
                //      taking it and doing nothing, which is what Bash does
                //      with most of them for a -c command as well.
                if (word_is(name, "posix"))
                {
                        if (!shell_extra_told("posix", true))
                        {
                                invocation->status = 2;
                                invocation->finished = true;
                                return at;
                        }
                }
                else if (word_is(name, "verbose"))
                        shell_option_letter_told('v', true);
                else if (word_is(name, "restricted"))
                {
                        shell_restricted_enter();
                        shell_restricted_sticky = true;
                }
                else if (word_is(name, "login"))
                        shell_shopt_state |= SHELL_SHOPT(LOGIN_SHELL);
                else if (word_is(name, "rcfile") || word_is(name, "init-file"))
                        shell_rcfile = arguments[at + 1];
                else if (word_is(name, "norc"))
                        shell_norc = true;
                else if (word_is(name, "version"))
                {
                        /*
                                What this is, and what it answers for. The
                                reference writes "GNU bash, version ..." and
                                four lines of FSF copyright after it; saying
                                the first without the rest would be neither
                                true nor a copy, and claiming the rest would
                                be a copyright statement about somebody
                                else's work. The version is bash's because
                                that is the surface being matched, and a
                                script reading it for a feature test gets the
                                number it is looking for.
                        */
                        string_format(log,
                                      "Moonwater shell, bash %s compatible (%s)\n",
                                      "5.3.15(1)-release", MOONWATER_MACHTYPE);
                        invocation->status = 0;
                        invocation->finished = true;
                        return at;
                }
                else if (word_is(name, "help"))
                {
                        shell_usage_said(log, arguments[0]);
                        invocation->status = 0;
                        invocation->finished = true;
                        return at;
                }
                else if (word_is(name, "dump-strings") ||
                         word_is(name, "dump-po-strings"))
                {
                        //      Every $"..." in the program, and there are
                        //      none: this shell has no message catalogue, so
                        //      the dump is empty and the program is not run.
                        invocation->status = 0;
                        invocation->finished = true;
                        return at;
                }

                at += shell_long_options[found].takes_word ? 2 : 1;
        }

        return at;
}

/* The command-line grammar locates the source and operands, while set's
   existing option adapter owns option state. No command string is synthesized
   and argv bytes never pass through expansion to become startup options.

   The long names are read first and separately, over the leading run only,
   because that is where the two phases part company and it is visible:
   `--posix -x` turns posix mode on and `-x --posix` is a usage error. */
static bool shell_start_options(string_address address_to arguments,
                                positive count, shell_invocation *invocation)
{
        string_address self = count && arguments[0] ? arguments[0]
                                                    : (string_address) "sh";
        positive at = 1;

        invocation->interactive = -1;
        invocation->monitor = -1;

        if (shell_bash_compat)
        {
                at = shell_start_long_options(arguments, count, invocation);
                if (invocation->finished)
                {
                        invocation->next = at;
                        return false;
                }
        }

        while (at < count)
        {
                string_address word = arguments[at];
                bool on;

                if (word_is(word, "--") || word_is(word, "-"))
                {
                        at++;
                        break;
                }
                if ((word[0] != '-' && word[0] != '+') || !word[1])
                        break;

                on = word[0] == '-';
                for (string_address letter = word + 1; *letter; letter++)
                {
                        p8 value = *letter;

                        if (value == 'c')
                                invocation->command = true;
                        else if (value == 'm')
                                invocation->monitor = on;
                        else if (value == 'l')
                        {
                                if (on)
                                        shell_shopt_state |=
                                            SHELL_SHOPT(LOGIN_SHELL);
                                else
                                        shell_shopt_state &=
                                            ~SHELL_SHOPT(LOGIN_SHELL);
                        }
                        else if (value == 'r' && shell_bash_compat)
                        {
                                /* Restriction goes on and stays: +r at
                                   invocation is a no-op, including on rbash. */
                                if (on)
                                {
                                        shell_restricted_enter();
                                        shell_restricted_sticky = true;
                                }
                        }
                        else if (value == 'D' && shell_bash_compat)
                        {
                                //      Every $"..." in the program, of which
                                //      this shell has none: the dump is empty
                                //      and the program is not run.
                                invocation->status = 0;
                                invocation->finished = true;
                                invocation->next = count;
                                return false;
                        }
                        else if (value == 'o')
                        {
                                if (at + 1 == count)
                                        shell_options_listed(log, !on);
                                else if (word_is(arguments[at + 1], "monitor"))
                                {
                                        invocation->monitor = on;
                                        at++;
                                }
                                else if (!shell_option_named(arguments[++at],
                                                             on))
                                {
                                        //      A name nobody has is a usage
                                        //      error whatever else was asked
                                        //      for: unlike a letter nobody
                                        //      has, this one does not go
                                        //      through the errexit door and
                                        //      always leaves 2 behind.
                                        invocation->status = 2;
                                        invocation->next = at;
                                        return shell_bash_compat
                                            ? string_report(log_error, false,
                                                  "%s: line 0: %s: %s: invalid option name\n",
                                                  self, self, arguments[at])
                                            : string_report(log_error, false,
                                                  "%s: 0: Illegal option -o %s\n",
                                                  self, arguments[at]);
                                }
                        }
                        else if (value == 'O' && shell_bash_compat)
                        {
                                if (at + 1 == count)
                                        for (positive item = 0;
                                             item < SHELL_SHOPT_NAMES; item++)
                                                shell_shopt_said(log, item,
                                                                 !on);
                                else if (shell_shopt_asked_count <
                                         SHELL_SHOPT_ASKED)
                                {
                                        shell_shopt_asked[
                                            shell_shopt_asked_count].name =
                                                arguments[++at];
                                        shell_shopt_asked[
                                            shell_shopt_asked_count++].on = on;
                                }
                                else if (!shell_shopt_apply(self,
                                                            arguments[++at], on))
                                {
                                        invocation->status = 2;
                                        invocation->next = at;
                                        return false;
                                }
                        }
                        else if (!shell_option_letter_told(value, on))
                        {
                                invocation->status = shell_invocation_refused(
                                    self, on ? '-' : '+', value);
                                invocation->next = at;
                                return false;
                        }

                        if (value == 's')
                                invocation->from_stdin = shell_bash_compat || on;
                        else if (value == 'i')
                                invocation->interactive = on;
                }
                at++;
        }

        invocation->next = at;
        return true;
}

/* Only an explicitly supplied startup variable has work here. No PATH search
   and no synthesized dot command: filename expansion and sourced-file parsing
   share the existing document expander and source reader. */
static bool shell_startup_file()
{
        string_address value;
        string_address path;
        p8 address_to text = null;
        positive room = 0;
        bool no_room = false;
        bipolar handle, got;

        bool posix_startup = !shell_bash_compat || shell_posix_on();
        bool rc = false;

        if (shell_startup_privileged)
                return true;

        //      An interactive bash that is not posix reads its rc file, the
        //      one named or ~/.bashrc, and neither BASH_ENV nor ENV.
        if (shell_bash_compat && !posix_startup && shell_is_interactive)
        {
                if (shell_norc)
                        return true;
                rc = true;
                value = shell_rcfile;
                if (!value)
                {
                        string_address home = env_get("HOME");
                        static p8 made[4096];

                        if (!home || !*home ||
                            string_length(home) + 10 >= sizeof(made))
                                return true;
                        string_copy(made, home);
                        string_copy(made + string_length(home),
                                    (string_address) "/.bashrc");
                        value = made;
                }
        }
        else if (posix_startup ? !shell_is_interactive : shell_is_interactive)
                return true;
        else
                value = env_get(posix_startup ? "ENV" : "BASH_ENV");
        if (!value || !*value)
                return true;

        /* A dash/ sh ENV file must not run under mismatched credentials.
           Keep these reads off the ordinary noninteractive dash entry path. */
        if (!shell_bash_compat)
        {
                shell_privilege_prepare();
                if (shell_privilege_mismatched)
                        return true;
        }

        token_used = 0;
        token_overflow = false;
        if (rc)
        {
                //      A name given on the command line is used as it stands.
                token_push_bytes(value, string_length(value));
                token_push(end);
                if (token_overflow)
                        return false;
        }
        else
        {
                shell_substitution_status = shell_status;
                if (!shell_expand_document(token_push_bytes, value,
                                           string_length(value), true))
                        return false;
                shell_status = shell_substitution_status;
                token_push(end);
                if (token_overflow)
                        return false;
        }
        path = token_storage;

        if (!*path)
                return true;
        handle = shell_source_direct(path);
        if (handle < 0)
        {
                if (handle != -2 && handle != -20)
                        string_format(log_error, "%s: cannot read startup file\n", path);
                return true;
        }
        got = shell_source_read(handle, address_of text, address_of room,
                                address_of no_room);
        if (got < 0)
        {
                string_format(log_error, "%s: cannot read startup file\n", path);
                memory_free(text, room);
                return !no_room;
        }

        shell_source_execute(text, (positive)got, true);
        memory_free(text, room);
        return true;
}

b32 main()
{
        b32 interactive;
        bipolar input = 0;
        bool script_file = false;
        bool command_option = false;
        bool parser_source_started = false;
        string_address command = null;
        string_address address_to arguments;
        positive process_arguments;
        string_address called = shell_tool_name(program_argument(0));

        /*
                One binary, forty names.

                The utilities are in here, so a link called grep pointing at
                this is grep -- there is nothing else to install and nothing
                that can be a version behind. Asked by its own name, or by any
                name that is not a tool's, it is a shell.
        */
        {
                /* This binary was invoked under an applet's name, so the
                   process is that applet and nothing follows it. */
                b32 answered = shell_tool_named_in(called, true);

                if (answered >= 0)
                {
                        log_flush();
                        return answered;
                }
        }

        /* This is a shell process, not a directly invoked applet.  It may
           become the parent of a confined command before that command reaches
           its final launch decision (pipelines, background jobs and
           substitutions all fork earlier), so establish and protect the
           launcher before parsing or startup code can create a child. */
        (void)floodlight_parent_begin();

        process_arguments = (positive)program_argument_count();
        arguments = program_argument_list();

        // The multicall dispatch already found the basename. Reuse it for
        // personality instead of scanning the path again after the fast exit.
        if (called && *called == '-')
                called++;
        shell_invocation_name = called;
        shell_invoked_as = arguments[0];
        /* rbash is bash's restricted name: the same policy, already
           restricted before any option is read. A leading dash was
           stripped above, so -rbash is a login rbash. */
        shell_bash_compat = called && (word_is(called, "bash") ||
                                       word_is(called, "rbash"));
        /* dash, and sh: Debian's /bin/sh is dash, so the name sh is the
           same shell, the dash grammar with none of bash's additions.
           Any other name, and an embedded caller, keeps the language whole. */
        shell_dash_compat = called && (word_is(called, "dash") ||
                                       word_is(called, "sh"));
        if (shell_dash_compat)
                shell_dash_begin();
        if (called && word_is(called, "rbash"))
                shell_restricted_enter();

        /* Bash privilege policy is decided from the entry credentials, not
           from an ID a startup file or command might later change. */
        if (shell_bash_compat)
                shell_privilege_prepare();

        /*
                A login shell is one whose zeroth argument begins with a dash.

                That is the whole of the mark, it has been since the seventh
                edition. The mark also gates logout; login profile loading
                is separate from the ENV/BASH_ENV startup policy below.
        */
        if (process_arguments && arguments[0] && arguments[0][0] == '-')
                shell_shopt_state |= SHELL_SHOPT(LOGIN_SHELL);

        /* Find the -c command without publishing any shell state yet. */
        if (process_arguments > 1)
        {
                string_address option = arguments[1];

                if (option && option[0] == '-' && option[1] == 'c' &&
                    !option[2])
                {
                        command_option = true;
                        if (process_arguments >= 3)
                                command = arguments[2];
                }
        }

        /* Signal policy is observable even while an otherwise empty shell is
           alive, so it remains on the semantic floor rather than being
           treated as parser setup. */
        /* A `-c` string is no shell somebody is typing at, so there is
           nothing for interrupt and quit to be ignored for until the options
           have been read, and the four calls that ignore them and then put
           them back are all that is left to leave out: what was inherited is
           still what is in place, and trap asks for it the first time it
           needs it. If the options turn out to ask for an interactive shell
           after all (`sh -ic`, which is not this spelling), it is done then. */
        shell_signals_deferred = command_option;
        if (!command_option)
                shell_signals_start();

        {
                b32 literal_status = 0;

                if (!shell_privilege_mismatched &&
                    shell_command_literal_status(command,
                                                 address_of literal_status))
                {
                        bool observable = false;

                        // Imported functions can override even :, true or
                        // false, and Bash imports SHELLOPTS/BASHOPTS before it
                        // executes the literal: xtrace alone makes the command
                        // observable. Fold all three transport checks into one
                        // startup environment walk.
                        if (shell_bash_compat)
                                for (positive at = 0; environ && environ[at]; at++)
                                {
                                        string_address entry = environ[at];

                                        if (*entry != 'B' && *entry != 'S')
                                                continue;
                                        if (env_function_assignment(entry) ||
                                            (!string_compare_max(entry, "BASH_ENV=", 9) &&
                                             entry[9]) ||
                                            (!string_compare_max(entry, "BASHOPTS=", 9) &&
                                             entry[9]) ||
                                            (!string_compare_max(entry, "SHELLOPTS=", 10) &&
                                             entry[10]))
                                        {
                                                observable = true;
                                                break;
                                        }
                                }

                        if (!observable)
                                return literal_status;
                }
        }

        /* Environment/parameter allocation and the option parser remain off
           the no-startup literal path. Startup functions and traps, however,
           can change even `false` or `:`, so they must run first. */
        shell_invocation invocation = {0};
        if (process_arguments && arguments[0])
        {
                shell_script_name = arguments[0];
        }

        if (!shell_start_options(arguments, process_arguments,
                                 address_of invocation))
        {
                log_flush();
                return invocation.status;
        }
        command_option = invocation.command;

        /* A bash-named process can also enter restricted mode through the
           optional $0 operand of -c.  Decide that before importing option
           transport from the environment, just as -r and an rbash process
           name are decided before it.  A non-rbash $0 still lifts the
           non-sticky restriction inherited only from the process name. */
        if (shell_bash_compat && command_option &&
            process_arguments > invocation.next + 1 &&
            !shell_restricted_sticky)
        {
                string_address named = shell_tool_name(
                    arguments[invocation.next + 1]);

                if (named && *named == '-')
                        named++;
                shell_restricted = named && word_is(named, "rbash");
        }

        /* Startup -p preserves entry IDs. Its absence resets both credentials
           before the environment can become shell state; either form keeps
           attacker-controlled startup files suppressed for this invocation. */
        if (shell_bash_compat)
                shell_privilege_started();

        /* environ is the live process environment. Ordinarily it is the
           kernel vector published by the startup shim; clone-and-reentry can
           deliberately replace it without forging another initial stack. */
        shell_env_init(environ);

        /* Frozen identity numbers bash lists as readonly integers. */
        if (shell_bash_compat)
                shell_bash_ids_publish();

        /*      getopts already reads OPTERR and already treats an unset one
                as asking to complain, which is what a 1 means. Bash
                publishes the 1 as well, under --posix too, so a script that
                reads it back sees a number rather than nothing. It has to
                come after the table exists or it is written into nothing,
                and it is an ordinary assignment so an inherited OPTERR and
                anything the script does to it both still win. */
        if (shell_bash_compat && !env_get("OPTERR"))
                env_assign("OPTERR", "1");
        if (shell_bash_compat && env_get("POSIXLY_CORRECT"))
                shell_posix_changed(true);
        if (shell_bash_compat && !shell_posix_variable())
                return 1;

        /* The -O names, now that posix mode and the environment have had
           their turn at the same state. A name nobody has is a usage error
           here and not where it was read, which is why it is the last thing
           the invocation complains about rather than the first. */
        if (!shell_shopt_asked_apply(process_arguments && arguments[0]
                                         ? arguments[0] : (string_address) "sh"))
        {
                log_flush();
                return 2;
        }

        /*
                sh file [word ...]

                The process arguments are not commands: the first one names
                the input file, and everything after it is the script's
                positional-parameter list. Keep the bytes in the shell's own
                parameter store before command parsing starts, because the
                command argv table is reused for every line it runs.

                The interpreter keeps the file descriptor apart from standard
                input. A read command inside the script still reads what the
                script's caller sent on descriptor zero; only this outer
                reader consumes the source file.
        */
        /*
                sh -c COMMAND [name [word ...]]

                The one spelling nearly everything else uses. system(), the
                shell a Makefile runs a recipe with, find -exec sh -c and
                xargs sh -c all arrive this way, and without it this is not a
                shell anything can call -- it read "-c" as a file name and
                said it could not open it.

                POSIX puts the command string in the first operand and, if
                there is another, makes it $0 and the rest the positional
                parameters. So `sh -c 'echo $0 $1' name one` prints "name one"
                rather than treating either as a word of the command.
        */
        if (command_option)
        {
                positive first = invocation.next;
                positive count = process_arguments > first + 1
                                     ? process_arguments - first - 2 : 0;

                if (first >= process_arguments)
                        return string_report(log_error, shell_bash_compat && (shell_options & SHELL_FLAG('e')) ? 1 : 2,
                            "sh: -c wants a command\n");

                command = arguments[first];
                shell_execution_string = command;
                if (!shell_start_parameters(arguments, first + 2, count))
                        return string_report(log_error, 1, "sh: no room for arguments\n");

                //      With no name operand $0 is unspecified, and what
                //      dash does is use the path it was invoked as. That is
                //      more use than the word "sh" when a message has to say
                //      where it came from.
                shell_script_name = process_arguments > first + 1
                                                          ? arguments[first + 1]
                                                          : arguments[0];
                shell_option_flags = (string_address) "c";
                script_file = true;
        }
        else if (!invocation.from_stdin && invocation.next < process_arguments)
        {
                positive first = invocation.next;
                string_address script = arguments[first];
                positive count = process_arguments - first - 1;

                if (!shell_start_parameters(arguments, first + 1, count))
                        return string_report(log_error, 1, "sh: no room for arguments\n");

                shell_script_name = script;
                shell_option_flags = (string_address) "";
                script_file = true;
        }
        else if (invocation.next < process_arguments &&
                 !shell_start_parameters(arguments, invocation.next,
                                          process_arguments - invocation.next))
                return string_report(log_error, 1, "sh: no room for arguments\n");

        interactive = shell_is_interactive = invocation.interactive >= 0
                          ? invocation.interactive
                          : (!script_file && shell_interactive());
        shell_signals_settle(interactive);
        shell_options_started(interactive, invocation.monitor);
        if (interactive && shell_option_on(SHELL_OPTION_MONITOR) &&
            !job_terminal_owned)
                shell_option_told(SHELL_OPTION_MONITOR, false);
        history_start();
        exec_function_import_environment(environ);

        /* Startup code shares stdin with a stdin-driven shell. Authenticate
           that stream before ENV/BASH_ENV can launch a confined applet which
           would otherwise see an unclassified reader as in-memory input. */
        if (!script_file)
        {
                if (!shell_parser_source_begin(input))
                        return string_report(log_error, 126,
                                             "sh: cannot isolate input\n");
                parser_source_started = true;
        }

        if (!shell_startup_file())
        {
                if (parser_source_started)
                        shell_parser_source_end();
                log_flush();
                return shell_status ? shell_status : 1;
        }

        /* Startup files may change directory. Bash opens the named script
           afterwards, while $0 and its parameters were available to startup.
           Source-open failures do not run an installed EXIT trap. */
        if (script_file && !command)
        {
                input = shell_source_direct(shell_script_name);
                if (input < 0)
                {
                        string_format(log_error, "sh: %s: cannot open\n",
                                      shell_script_name);
                        log_flush();
                        return shell_bash_compat ? (input == -2 ? 127 : 126) : 2;
                }
                /* bash will not run a file whose first line holds a NUL: its
                   check_binary_file reads the first 80 bytes and answers 126.
                   A stream that cannot be read twice is not looked at. */
                if (shell_bash_compat)
                {
                        p8 sample[80];
                        bipolar got = system_call_4(syscall(pread64),
                                                    (positive)input,
                                                    (positive)sample,
                                                    sizeof(sample), 0);

                        for (bipolar at = 0; at < got; at++)
                        {
                                if (sample[at] == '\n')
                                        break;

                                if (sample[at])
                                        continue;

                                string_format(log_error,
                                              "%s: %s: cannot execute binary "
                                              "file\n",
                                              shell_invocation_name
                                                  ? shell_invocation_name
                                                  : (string_address) "sh",
                                              shell_script_name);
                                log_flush();
                                system_close(input);
                                return 126;
                        }
                }
                exec_script_fd = (b32)input;
                if (!exec_script_preserve(null))
                        // At a very low descriptor limit there may be no
                        // spare slot. Keep streaming the original, CLOEXEC,
                        // and refuse a later collision if it cannot move.
                        system_call_3(syscall(fcntl), input, 2, 1);

                input = exec_script_fd;
                if (!shell_parser_source_begin(input))
                {
                        system_close(input);
                        exec_script_fd = -1;
                        return string_report(log_error, 126,
                                             "sh: cannot isolate script input\n");
                }
                parser_source_started = true;
        }

        /*
                Whatever arrived, split into lines, with the last one held back
                if it has no newline yet.

                A read is not a command. A terminal hands over one line because
                the line discipline waits for Enter; a pipe or a file arrives
                in four kilobyte lumps that end wherever they end. Treating a
                lump as a line ran two commands as one at the start and split a
                command in half at every boundary -- a forty thousand line
                script came out with a hundred and forty three lines too many.
        */
        /*
                A command string is not read from anywhere, so it is run and
                that is the whole of it.

                It is copied first because the lines are ended in place, and
                what came in is the process's own argument block. The copy is
                as long as the string, so a -c of any size works -- there is no
                buffer here to be longer than.
        */
        if (command)
        {
                string_address first_newline =
                    string_first_of_or_end(command, '\n');

                /* The lexer and parser copy tokens and never write the source.
                   A single command can therefore stay in the process argument
                   block; only multi-line input needs a writable copy whose
                   newlines are ended in place. */
                if (!*first_newline)
                {
                        shell_verbose_from_string = true;
                        shell_verbose_line(command);
                        lex_physical_newline(false);
                        shell_run_known_line(command, true);
                }
                else
                {
                        positive length = (positive)(first_newline - command) +
                                          1 + string_length(first_newline + 1);
                        p8 address_to held_command =
                            (p8 address_to)memory(length + 1);

                        if (!held_command ||
                            (positive)held_command >= (positive)-4095)
                                return string_report(log_error, 1, "sh: no room for the command\n");

                        memory_copy(held_command, command, length + 1);

                        {
                                positive at = shell_run_complete_lines(
                                    held_command, length, true);

                                if (at < length)
                                {
                                        shell_verbose_line(held_command + at);
                                        lex_physical_newline(false);
                                        shell_run_known_line(
                                            held_command + at, true);
                                }
                        }

                        memory_free(held_command, length + 1);
                }

                /* Join the ordinary input teardown without closing stdin:
                   -c has no source descriptor of its own. */
                script_file = false;
                goto input_finished;
        }

        positive held = 0;

        /*
                A script on standard input shares that descriptor with every
                command it runs.

                Reading ahead in four kilobyte lumps handed `read`, and the
                commands after it, an input the shell had already swallowed:
                `read x` in a piped script read nothing, and the line meant
                for it was run as a command. dash and bash read a pipe a byte
                at a time and leave a regular file's offset at the end of the
                line being run, so a child sees exactly the bytes the script
                has not reached yet. A terminal already hands over one line
                per read, so it keeps the wider read.
        */
        bool terminal_input = !script_file && shell_interactive();
        bool shared_input = !script_file && !terminal_input;
        bool seekable_input = shared_input && system_seek(input, 0, 1) >= 0;

        /* Keep one authenticated identity for the whole live reader. This is
           established before the first byte is consumed, and a committed
           bare exec refreshes it when it replaces that reader. */
        while (1)
        {
                bipolar got;
                positive total, at;

                //      Before a fresh prompt, not a continuation's: bash's
                //      PROMPT_COMMAND, with the status of the last command
                //      still in $? and put back after it.
                if (interactive && shell_bash_compat && !shell_reading_more() &&
                    !held)
                        shell_prompt_command();

                //      What a program that died on the alternate screen left,
                //      put right before every prompt. The way back is 47 and
                //      not 1049: 1049 also restores the cursor, and Konsole,
                //      xterm and VTE do that whether or not 1049h ever saved
                //      one, so each prompt went to the top left corner and
                //      the next line was written over it.
                if (interactive && terminal_input)
                {
                        log_direct(str(TERM_MAIN_BUFFER_ONLY TERM_RESET
                                           TERM_SHOW_CURSOR));
                        shell_prompt_write(log_direct, shell_reading_more());
                }
                //      -i with no terminal on standard input: the prompt
                //      goes to standard error, as both references write it,
                //      and nothing here is for a screen to draw.
                else if (interactive && !held)
                {
                        bool more = shell_reading_more();
                        string_address text = env_get(more ? "PS2" : "PS1");

                        if (!text)
                                text = more ? (string_address) "> "
                                       : shell_bash_compat
                                           ? (string_address) "\\s-\\v\\$ "
                                           : (string_address) "$ ";
                        if (shell_bash_compat)
                                log_error(shell_prompt_expand(text, false), 0);
                        else
                                shell_prompt_written(log_error, text);
                        log_flush();
                }

                //      Room for another read on top of whatever is being
                //      held back, so a line has no length it cannot reach.
                if (!shell_array_room(shell_buffer, shell_buffer_room, held + MAX_INPUT_STEP + 1))
                        return string_report(log_error, 1, "sh: no room to read\n");

                if (script_file)
                        input = exec_script_fd;
                /*
                        A resize while the shell waits at its prompt runs a
                        WINCH trap then, as bash's does. The kernel restarts a
                        read that a caught signal interrupted, so while a WINCH
                        command trap is set, a terminal read takes WINCH without
                        restarting: the read comes back -4, the trap runs, and the
                        read goes on. No other signal changes.
                */
                bool window_wait = interactive && terminal_input && !script_file &&
                                   trap_action((positive)SIGWINCH) &&
                                   string_get(trap_action((positive)SIGWINCH));

                if (window_wait)
                        shell_catch_mode(SIGWINCH, false);
                got = system_read_once(input, shell_buffer + held,
                                       shared_input && !seekable_input
                                          ? 1 : shell_buffer_room - 1 - held);
                while (got == -4 && window_wait)
                {
                        exec_traps();
                        got = system_read_once(input, shell_buffer + held,
                                               shared_input && !seekable_input
                                                  ? 1 : shell_buffer_room - 1 - held);
                }
                if (window_wait)
                        shell_catch_mode(SIGWINCH, true);

                if (got < 0 && script_file && shell_bash_compat)
                {
                        string_format(log_error, "sh: %s: cannot read\n",
                                      shell_script_name);
                        system_close(input);
                        log_flush();
                        return 126;
                }
                if (got <= 0)
                        break;

                total = held + (positive)got;

                if (shared_input)
                {
                        p8 address_to newline = memory_first_of(
                            shell_buffer + held, '\n', (positive)got);

                        // Nothing to run yet; keep collecting the line.
                        if (!newline)
                        {
                                held = total;
                                continue;
                        }

                        // Only the line that ended is run. What was read
                        // past it is given back to the file, where the next
                        // command to read standard input expects to find it.
                        if (seekable_input)
                        {
                                positive line_end =
                                    (positive)(newline - shell_buffer) + 1;

                                if (line_end < total)
                                        system_seek(input,
                                                    -(bipolar)(total - line_end),
                                                    1);
                                total = line_end;
                        }
                }

                at = shell_run_complete_lines(shell_buffer, total, false);

                if (at && shell_onecmd_on() && !shell_reading_more())
                {
                        held = 0;
                        break;
                }

                held = total - at;

                //      What is held back is a line that has not ended yet, so
                //      it moves to the front and the next read lands after
                //      it. The buffer grows above rather than the line being
                //      cut, so there is no length at which this stops working.
                if (held)
                        memory_copy(shell_buffer, shell_buffer + at, held);
        }

        // Whatever was still in hand when the input ended.
        if (held)
        {
                string_address ready = shell_buffer;
                b32 history_action = HISTORY_EXPAND_RUN;

                shell_buffer[held] = end;

                if (shell_history_recording())
                {
                        history_action = history_expand_line(
                            shell_buffer, address_of ready);
                        if (history_action >= HISTORY_EXPAND_RUN)
                                history_remember(ready);
                }

                if (history_action == HISTORY_EXPAND_RUN)
                {
                        shell_verbose_from_string = false;
                        shell_verbose_line(ready);
                        // A backslash that meets the end of the input with
                        // no newline after it is a byte of the word, in
                        // bash 5.3 and dash alike: `echo a\` prints a\.
                        lex_physical_newline(false);
                        shell_run_known_line(ready, true);
                }
        }

        //      Input ran out, which is a way of leaving like any other,
        //      and bash says the same "exit" for the end of a terminal's
        //      input that the builtin says. Not for the end of a script
        //      named on the command line, though: "bash -i script" is
        //      interactive and still leaves without a word, because what
        //      ended is the file and not the session.
        if (!script_file)
                shell_interactive_exit_said();

        shell_parser_source_end();

input_finished:
        shell_input_end();

        if (script_file)
        {
                system_close(exec_script_fd);
                exec_script_fd = -1;
        }

        shell_trap_exit();

        log_flush();
        return shell_status;
}
