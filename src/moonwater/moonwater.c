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
// A wait's answer that is no spark event: rover's ring has records for userspace (src/moonwater/rules.c).
#define MOONWATER_ROVER 0xffu

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
        // The twenty-third event is gone; the request is the size its number says.
        unsigned short retired;
};

_Static_assert(sizeof(struct moonwater_overlay) == 56, "moonwater overlay");

static const struct {
        char name[16];
        unsigned char bit;
        unsigned char line_off;
} moonwater_hook[MOONWATER_HOOKS] = {
        { "moonwater_init", MOONWATER_HOOK_INIT,
          (unsigned char)offset_of(struct moonwater_overlay, init_line) },
        { "moonwater_event", MOONWATER_HOOK_EVENT,
          (unsigned char)offset_of(struct moonwater_overlay, event_line) },
        { "moonwater_end", MOONWATER_HOOK_END,
          (unsigned char)offset_of(struct moonwater_overlay, end_line) },
};

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
        The script text lives in the module: CONFIG_MOONWATER_MACHINE_SCRIPT's
        file is baked in as the fallback, none by default since the rover
        runner took the machine, and a script SET replaces the live copy.
        GET answers the overlay. The module scans once; userspace does not.
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
_Static_assert(offset_of(struct machine_script, overlay) == 24,
               "moonwater script overlay");
_Static_assert(offset_of(struct moonwater_overlay, bind_line) == 10,
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

#define MOONWATER_SCRIPT_SCAN_ASM_IMPLEMENTATION
#include "script_scan.asm.c"

struct script_read {
        const unsigned char *text;
        unsigned long length;
        unsigned long at;
        unsigned int line;
        unsigned char heredoc_tabs;     // <<- strips leading tabs
        unsigned char heredoc_active;   // a body waits for the next newline (even if delimiter is empty)
        unsigned char heredoc_length;   // length of the delimiter prefix kept in heredoc[]
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

_Static_assert(SCRIPT_WORD == MW_SCRIPT_WORD &&
               SCRIPT_TOK_END == MW_SCRIPT_TOK_END &&
               SCRIPT_TOK_WORD == MW_SCRIPT_TOK_WORD &&
               SCRIPT_TOK_PUNCT == MW_SCRIPT_TOK_PUNCT &&
               SCRIPT_TOK_DSEMI == MW_SCRIPT_TOK_DSEMI,
               "script_scan token constants mismatch");
_Static_assert(sizeof(struct script_read) == sizeof(struct mw_script_read) &&
               offset_of(struct script_read, text) == MW_SCAN_OFFSET(struct mw_script_read, text) &&
               offset_of(struct script_read, length) == MW_SCAN_OFFSET(struct mw_script_read, length) &&
               offset_of(struct script_read, at) == MW_SCAN_OFFSET(struct mw_script_read, at) &&
               offset_of(struct script_read, line) == MW_SCAN_OFFSET(struct mw_script_read, line) &&
               offset_of(struct script_read, heredoc_tabs) == MW_SCAN_OFFSET(struct mw_script_read, heredoc_tabs) &&
               offset_of(struct script_read, heredoc_active) == MW_SCAN_OFFSET(struct mw_script_read, heredoc_active) &&
               offset_of(struct script_read, heredoc_length) == MW_SCAN_OFFSET(struct mw_script_read, heredoc_length) &&
               offset_of(struct script_read, heredoc_total) == MW_SCAN_OFFSET(struct mw_script_read, heredoc_total) &&
               offset_of(struct script_read, rescan) == MW_SCAN_OFFSET(struct mw_script_read, rescan) &&
               offset_of(struct script_read, heredoc) == MW_SCAN_OFFSET(struct mw_script_read, heredoc),
               "script_read != mw_script_read ABI");
_Static_assert(sizeof(struct script_token) == sizeof(struct mw_script_token) &&
               offset_of(struct script_token, kind) == MW_SCAN_OFFSET(struct mw_script_token, kind) &&
               offset_of(struct script_token, punct) == MW_SCAN_OFFSET(struct mw_script_token, punct) &&
               offset_of(struct script_token, fresh) == MW_SCAN_OFFSET(struct mw_script_token, fresh) &&
               offset_of(struct script_token, line) == MW_SCAN_OFFSET(struct mw_script_token, line) &&
               offset_of(struct script_token, word) == MW_SCAN_OFFSET(struct mw_script_token, word),
               "script_token != mw_script_token ABI");

/*
        The scanner stands alone on purpose. moonwater_scan is the one reader of
        the machine script, expanded in the kernel module and in the moonwater
        command, and compiled by itself, with no library under it, by the
        core_state harness in test/differential.py: its hot integer primitives
        live in script_scan.asm.c (zero lookup tables, zero allocations).
*/
static inline int script_eq(const char *a, const char *b)
{
        return mw_script_eq(a, b);
}

static inline int script_punct_is(const struct script_token *token, unsigned char value)
{
        return token->kind == SCRIPT_TOK_PUNCT && token->punct == value;
}

static inline void script_next(struct script_read *scan, struct script_token *token)
{
        mw_script_next((struct mw_script_read *)scan, (struct mw_script_token *)token);
}

static inline struct script_token script_peek(struct script_read *scan)
{
        struct script_token token;

        mw_script_peek((struct mw_script_read *)scan, (struct mw_script_token *)&token);
        return token;
}

static inline int script_event_is(const char *pattern, const char *event)
{
        return mw_script_event_is(pattern, event);
}

static inline unsigned int script_kw(const struct script_token *token)
{
        return mw_script_keyword((const struct mw_script_token *)token);
}

static inline int script_command_start(const struct script_token *token)
{
        return mw_script_command_start((const struct mw_script_token *)token);
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
                         unsigned int depth);

static void script_parse_case(struct script_read *scan,
                              struct moonwater_overlay *into)
{
        struct script_token token;
        unsigned int cases = 1;

        do {
                script_next(scan, &token);
                if (token.kind == SCRIPT_TOK_END)
                        return;
        } while (!(script_kw(&token) & (MW_KW_ESAC | MW_KW_IN)));
        if (script_kw(&token) == MW_KW_ESAC)
                return;

        while (cases) {
                script_next(scan, &token);
                if (token.kind == SCRIPT_TOK_END || script_punct_is(&token, '}'))
                        return;
                if (script_kw(&token) == MW_KW_ESAC) {
                        cases--;
                        continue;
                }
                if (script_punct_is(&token, '{')) {
                        script_block(scan, 0, 1);
                        continue;
                }
                if (script_kw(&token) == MW_KW_CASE) {
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
                       script_kw(&token) != MW_KW_ESAC)
                        script_next(scan, &token);
                if (token.kind == SCRIPT_TOK_END)
                        return;
                if (script_kw(&token) == MW_KW_ESAC) {
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
                        if (script_kw(&token) == MW_KW_CASE)
                                cases++;
                        else if (script_kw(&token) == MW_KW_ESAC) {
                                cases--;
                                break;
                        } else if (script_punct_is(&token, '{'))
                                script_block(scan, 0, 1);
                }
        }
}

static void script_block(struct script_read *scan, struct moonwater_overlay *into,
                         unsigned int depth)
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
                else if (into && script_kw(&token) == MW_KW_CASE)
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
static void script_compound(struct script_read *scan, struct moonwater_overlay *into,
                            int parens)
{
        struct script_token token;
        unsigned int depth = 1;
        int command = 1;

        while (depth) {
                unsigned int kw;

                script_next(scan, &token);
                if (token.kind == SCRIPT_TOK_END)
                        return;
                if (token.fresh)
                        command = 1;
                kw = script_kw(&token);
                if (parens && script_punct_is(&token, '('))
                        depth++;
                else if (parens && script_punct_is(&token, ')'))
                        depth--;
                else if (command && kw == MW_KW_CASE)
                        script_parse_case(scan, into);
                else if (command && !parens && kw == MW_KW_OPENER)
                        depth++;
                else if (command && !parens && kw == MW_KW_CLOSER)
                        depth--;
                command = script_command_start(&token);
        }
}

static int script_take_function(struct script_read *scan, const char *name,
                                unsigned short line, struct moonwater_overlay *into)
{
        struct script_token token;
        unsigned int i, kw;
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
        kw = script_kw(&token);
        if (!script_punct_is(&token, '{') && !script_punct_is(&token, '(') &&
            !(kw & (MW_KW_OPENER | MW_KW_CASE | MW_KW_TEST)))
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
        else if (kw == MW_KW_TEST) {
                while (token.kind != SCRIPT_TOK_END && !script_punct_is(&token, ']'))
                        script_next(scan, &token);
                token = script_peek(scan);
                if (script_punct_is(&token, ']'))
                        script_next(scan, &token);
        } else if (kw == MW_KW_CASE)
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
        scan.heredoc_active = 0;
        scan.heredoc_length = 0;
        scan.heredoc_total = 0;
        scan.rescan = 8 * length;

        while (scan.at < scan.length) {
                script_next(&scan, &token);
                if (token.kind == SCRIPT_TOK_END)
                        break;
                if (script_kw(&token) == MW_KW_FUNCTION) {
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
#define BIND_STATE 4u
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
        // rover's (rules.c): the chord modifiers this device holds, and the keys a rule took on their way down
        unsigned int rover_mods;
        // not the machine's own: a program's (uinput, uhid), heard by no rule and by no stop key's binding
        bool rover_virtual;
        int rel_x, rel_y; // pending REL_X / REL_Y accumulated for the current HID report (spark_input_ring)
        int focus_x, focus_y; // the same for the focused page (spark_focus_page), which hears virtual devices too
        unsigned long rover_taken[BITS_TO_LONGS(KEY_CNT)];
};

// Spark's capability test (spark.c, its SPARK_KERNEL pass, which core.c includes after this file): declared here
// for the machine's ioctls, which use it first.
static inline bool spark_file_capable(struct file *file, int cap);

static struct spark_input_ring *spark_input_page;
static DECLARE_WAIT_QUEUE_HEAD(spark_input_wait);
static DEFINE_SPINLOCK(spark_input_lock);
static struct file *spark_input_owner;

static void spark_input_init(void)
{
        struct spark_input_ring *page = vmalloc_user(SPARK_INPUT_PAGE_BYTES);

        if (!page)
                return;
        page->magic = SPARK_INPUT_MAGIC;
        page->version = SPARK_INPUT_VERSION;
        page->mask = SPARK_INPUT_SLOTS - 1u;
        page->active = 0;
        page->head = 0;
        page->tail = 0;
        page->drops = 0;
        smp_wmb();
        WRITE_ONCE(spark_input_page, page);
}

static void spark_input_exit(void)
{
        struct spark_input_ring *page = xchg(&spark_input_page, NULL);

        if (page)
        {
                WRITE_ONCE(page->active, 0);
                vfree(page);
        }
}

static bool spark_input_is_owner(struct file *file)
{
        return file && READ_ONCE(spark_input_owner) == file;
}

static int spark_input_mmap(struct file *file, struct vm_area_struct *vma)
{
        struct spark_input_ring *page = READ_ONCE(spark_input_page);
        unsigned long length = vma->vm_end - vma->vm_start;
        unsigned long flags;
        int err;

        if (!page)
                return -ENODEV;
        if (!file_ns_capable(file, &init_user_ns, CAP_SYS_ADMIN) || !capable(CAP_SYS_ADMIN))
                return -EPERM;
        if (length != SPARK_INPUT_PAGE_BYTES || (vma->vm_flags & VM_EXEC) || !(vma->vm_flags & VM_SHARED))
                return -EINVAL;
        vm_flags_clear(vma, VM_MAYEXEC);
        spin_lock_irqsave(&spark_input_lock, flags);
        if (spark_input_owner && spark_input_owner != file)
        {
                spin_unlock_irqrestore(&spark_input_lock, flags);
                return -EBUSY;
        }
        spark_input_owner = file;
        spin_unlock_irqrestore(&spark_input_lock, flags);
        err = remap_vmalloc_range(vma, page, 0);
        if (err)
        {
                spin_lock_irqsave(&spark_input_lock, flags);
                if (spark_input_owner == file)
                        spark_input_owner = NULL;
                spin_unlock_irqrestore(&spark_input_lock, flags);
        }
        return err;
}

static __poll_t spark_input_poll(struct file *file, poll_table *wait)
{
        struct spark_input_ring *page = READ_ONCE(spark_input_page);

        poll_wait(file, &spark_input_wait, wait);
        if (!page || !spark_input_is_owner(file))
                return 0;
        return smp_load_acquire(&page->head) != READ_ONCE(page->tail) ? EPOLLIN | EPOLLRDNORM : 0;
}

/*
        The focused pages (spark.c, spark_focus_page): one per thread group that mapped one, on a
        list kept under spark_focus_mutex for adding and taking away and spark_input_lock for the
        filter's walk. spark_focus_pid is the thread group the ring's owner named; the page whose
        thread group it is and whose claim is not off is the live one, the only page the filter
        writes. spark_focus_mode is the live page's claim, read by the filter before it takes the
        lock; spark_focus_down holds the buttons pressed into a page, whose releases go there
        (or, the page gone, nowhere), never to a reader that saw no press.
*/
struct spark_focus_reader
{
        struct list_head link;
        struct spark_focus_page *page;
        struct file *file;
        struct pid *pid;   // the thread group that mapped it
        unsigned int claim;
        unsigned int head; // the slots written, the kernel's own count: the page's is never read back
};

static LIST_HEAD(spark_focus_readers);
static DEFINE_MUTEX(spark_focus_mutex);
static struct spark_focus_reader *spark_focus_live;
static struct pid *spark_focus_pid;
static unsigned int spark_focus_mode;
static unsigned int spark_focus_down;

// spark_input_lock held: the page the filter writes made again from the name and the claims.
// A page that stops being written is wiped after its generation moves, so a reader that read
// across the wipe sees the move and throws away what it read.
static void spark_focus_relive(void)
{
        struct spark_focus_reader *reader, *live = NULL;

        if (spark_focus_pid)
                list_for_each_entry(reader, &spark_focus_readers, link)
                        if (reader->pid == spark_focus_pid && reader->claim != SPARK_FOCUS_CLAIM_OFF)
                        {
                                live = reader;
                                break;
                        }
        if (live != spark_focus_live)
        {
                struct spark_focus_reader *was = spark_focus_live;

                if (was)
                {
                        WRITE_ONCE(was->page->state, SPARK_FOCUS_STATE_OFF);
                        smp_store_release(&was->page->generation, was->page->generation + 1);
                        smp_wmb();
                        memset(was->page->slots, 0, sizeof(was->page->slots));
                        WRITE_ONCE(was->page->buttons, 0);
                }
                if (live)
                {
                        WRITE_ONCE(live->page->armed_ms, (unsigned int)(ktime_get_ns() / 1000000ull));
                        WRITE_ONCE(live->page->armed_head, live->head);
                        WRITE_ONCE(live->page->state, SPARK_FOCUS_STATE_ARMED);
                        smp_store_release(&live->page->generation, live->page->generation + 1);
                }
                spark_focus_live = live;
        }
        WRITE_ONCE(spark_focus_mode, live ? live->claim : SPARK_FOCUS_CLAIM_OFF);
}

// The ring's owner names the thread group a page is written for: a pidfd, or -1 for none.
static long spark_focus_arm(struct file *file, unsigned long arg)
{
        struct pid *pid = NULL, *was;
        unsigned long flags;
        int fd = (int)arg;

        if (!spark_input_is_owner(file))
                return -EPERM;
        if (fd >= 0)
        {
                unsigned int fd_flags;

                pid = pidfd_get_pid((unsigned int)fd, &fd_flags);
                if (IS_ERR(pid))
                        return PTR_ERR(pid);
        }
        else if (fd != -1)
                return -EINVAL;
        spin_lock_irqsave(&spark_input_lock, flags);
        if (!spark_input_is_owner(file))
        {
                spin_unlock_irqrestore(&spark_input_lock, flags);
                put_pid(pid);
                return -EPERM;
        }
        was = spark_focus_pid;
        spark_focus_pid = pid;
        spark_focus_relive();
        spin_unlock_irqrestore(&spark_input_lock, flags);
        put_pid(was);
        return 0;
}

static struct spark_focus_reader *spark_focus_find(struct file *file)
{
        struct spark_focus_reader *reader;

        list_for_each_entry(reader, &spark_focus_readers, link)
                if (reader->file == file)
                        return reader;
        return NULL;
}

// A page's own thread group claims it, or lets it go.
static long spark_focus_claim(struct file *file, unsigned long arg)
{
        struct spark_focus_reader *reader;
        unsigned long flags;
        long err = 0;

        if (arg > SPARK_FOCUS_CLAIM_TAKE)
                return -EINVAL;
        mutex_lock(&spark_focus_mutex);
        reader = spark_focus_find(file);
        if (!reader)
                err = -ENODEV;
        else if (reader->pid != task_tgid(current))
                err = -EPERM;
        else
        {
                spin_lock_irqsave(&spark_input_lock, flags);
                reader->claim = (unsigned int)arg;
                WRITE_ONCE(reader->page->claim, (unsigned int)arg);
                spark_focus_relive();
                spin_unlock_irqrestore(&spark_input_lock, flags);
        }
        mutex_unlock(&spark_focus_mutex);
        return err;
}

// A page for the caller's thread group: read only, never inherited by a fork, one per group.
static int spark_focus_mmap(struct file *file, struct vm_area_struct *vma)
{
        struct spark_focus_reader *reader, *other;
        struct pid *me = task_tgid(current);
        unsigned long flags;
        int err;

        if (vma->vm_end - vma->vm_start != SPARK_INPUT_PAGE_BYTES || (vma->vm_flags & (VM_WRITE | VM_EXEC)))
                return -EINVAL;
        vm_flags_clear(vma, VM_MAYWRITE | VM_MAYEXEC);
        vm_flags_set(vma, VM_DONTCOPY);
        mutex_lock(&spark_focus_mutex);
        reader = spark_focus_find(file);
        if (reader && reader->pid != me)
        {
                mutex_unlock(&spark_focus_mutex);
                return -EPERM;
        }
        if (!reader)
        {
                list_for_each_entry(other, &spark_focus_readers, link)
                        if (other->pid == me)
                        {
                                mutex_unlock(&spark_focus_mutex);
                                return -EBUSY;
                        }
                reader = kzalloc(sizeof(*reader), GFP_KERNEL);
                if (reader)
                        reader->page = vmalloc_user(SPARK_INPUT_PAGE_BYTES);
                if (!reader || !reader->page)
                {
                        kfree(reader);
                        mutex_unlock(&spark_focus_mutex);
                        return -ENOMEM;
                }
                reader->page->magic = SPARK_FOCUS_MAGIC;
                reader->page->version = SPARK_FOCUS_VERSION;
                reader->page->mask = SPARK_FOCUS_SLOTS - 1u;
                reader->file = file;
                reader->pid = get_pid(me);
                spin_lock_irqsave(&spark_input_lock, flags);
                list_add(&reader->link, &spark_focus_readers);
                spin_unlock_irqrestore(&spark_input_lock, flags);
        }
        err = remap_vmalloc_range(vma, reader->page, 0);
        mutex_unlock(&spark_focus_mutex);
        return err;
}

// The last close of a descriptor (every mapping holds it open, so none is left): its page goes.
static void spark_focus_release(struct file *file)
{
        struct spark_focus_reader *reader;
        unsigned long flags;

        //      Every close of /dev/spark comes here, and almost none mapped a
        //      page: a page this file has was put on the list while the file
        //      was still open, so an empty list is no page of its.
        if (list_empty(&spark_focus_readers))
                return;
        mutex_lock(&spark_focus_mutex);
        reader = spark_focus_find(file);
        if (reader)
        {
                spin_lock_irqsave(&spark_input_lock, flags);
                list_del(&reader->link);
                if (spark_focus_live == reader)
                {
                        spark_focus_live = NULL;
                        WRITE_ONCE(spark_focus_mode, SPARK_FOCUS_CLAIM_OFF);
                }
                spin_unlock_irqrestore(&spark_input_lock, flags);
        }
        mutex_unlock(&spark_focus_mutex);
        if (reader)
        {
                vfree(reader->page);
                put_pid(reader->pid);
                kfree(reader);
        }
}

static void spark_input_release(struct file *file)
{
        struct spark_input_ring *page = READ_ONCE(spark_input_page);
        struct pid *named = NULL;
        unsigned long flags;

        spin_lock_irqsave(&spark_input_lock, flags);
        if (spark_input_owner == file)
        {
                spark_input_owner = NULL;
                if (page)
                        WRITE_ONCE(page->active, 0);
                // No owner is left to take the name back at the next change of focus: none is named.
                named = spark_focus_pid;
                spark_focus_pid = NULL;
                spark_focus_relive();
        }
        spin_unlock_irqrestore(&spark_input_lock, flags);
        put_pid(named);
        spark_focus_release(file);
}

static struct bind_row bind_table[SPARK_BIND_EVENTS];
static DEFINE_SPINLOCK(bind_lock);
static atomic_t bind_ctrl;
static atomic_t bind_alt;
static bool bind_handler_registered;
static struct work_struct bind_state_work;

/*
        Whether this start of the desktop has been handed to anyone.

        A desktop starts during the initcalls, before system_state is
        running, so the on event it fires at boot is dropped, and the machine
        process that opens its windows attaches seconds later. Each on or off
        clears it; handing on to the machine process, or queueing its image
        line, sets it; and an attach that finds the desktop on and this clear
        fires on again. A machine process that crashes and attaches again
        after it read the event does not get a second one, so it does not
        open a second set of windows.

        The events whose run is the state of the thing when the run starts,
        and not the edge that queued it, are the ones flagged BIND_STATE: one
        run answers however many on and off came before it.
*/
static atomic_t bind_state_told;
static atomic_t bind_alive;
static atomic_t bind_machine_live;

static struct {
        unsigned int event[BIND_MACHINE_QUEUE];
        unsigned int count;
        struct file *owner;
        struct pid *process; // the thread group that attached it
        wait_queue_head_t wait;
        bool ending;
        u64 ring_seen; // rover's ring: the records for userspace this process has been told of
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
        { "", 0, BIND_DROP | BIND_STATE, { 0, 0 } },
        { "", 0, BIND_DROP | BIND_STATE, { 0, 0 } },
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
        [MOONWATER_DETACH] = 1,
        [MOONWATER_WAIT] = 1,
        [MOONWATER_END] = 1,
};

static struct bind_row *bind_row(unsigned int event)
{
        if (!event || event > SPARK_BIND_EVENTS)
                return null;
        return bind_table + event - 1;
}

static u8 bind_code_event[BIND_CODES];

static void bind_watch_code(unsigned int code, unsigned int event, bool on)
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
static bool bind_row_bound(struct bind_row *row)
{
        return row && (atomic_read(&bind_machine_live) || atomic_read(&row->bound));
}

static void bind_watch_row(struct bind_row *row)
{
        unsigned int i, event = row->event;
        bool on = bind_row_bound(row);

        for (i = 0; i < BIND_CODES_PER; i++)
                bind_watch_code(bind_spec[event - 1].code[i], event, on);
}

static int bind_spawn_enter(void *data)
{
        struct bind_spawn *spawn = data;
        char command[SPARK_BIND_COMMAND_MAX];
        char event_env[sizeof("MOONWATER_EVENT=") + SPARK_BIND_NAME_MAX];
        char *argv[] = { SPARK_TOOL_PROGRAM, "-c", command, null };
        char *envp[] = { SPARK_COMMAND_ENVIRONMENT(SPARK_ENVIRONMENT_ENTRY)
                         event_env, null };
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
        bool poweroff, reboot;
        pid_t pid;
        int ret = 0, stat = 0, number, shown;
        unsigned long flags;

        spin_lock_irqsave(&bind_lock, flags);
        strscpy(spawn.command, row->command, sizeof(spawn.command));
        spin_unlock_irqrestore(&bind_lock, flags);
        strscpy(spawn.event, name, sizeof(spawn.event));
        atomic_set(&row->busy, 1);

        if (!spawn.command[0])
                goto done;

        poweroff = !string_compare(spawn.command, "poweroff");
        reboot = !string_compare(spawn.command, "reboot");
        //      %*pE: the line is somebody's text, and a log is not a terminal. It is said only when it fails:
        //      a press that did what it is bound to is not news.
        shown = (int)string_length(spawn.command);

        //      A stop is stamped at each step, so one that is slow says where its time went.
        if (poweroff || reboot)
                pr_info("[moonwater] %s: %s, the line starts\n", name, spawn.command);
        kernel_sigaction(SIGCHLD, SIG_DFL);
        pid = user_mode_thread(bind_spawn_enter, &spawn, SIGCHLD);
        if (pid > 0) {
                ret = kernel_wait(pid, &stat);
                if (ret > 0)
                        ret = stat;
        } else
                ret = pid ? pid : -EAGAIN;
        kernel_sigaction(SIGCHLD, SIG_IGN);
        if (poweroff || reboot)
                pr_info("[moonwater] %s: the line answered %d\n", name, ret);

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

static void bind_state_run(struct work_struct *work)
{
        struct bind_row *on = bind_row(SPARK_BIND_CANVAS_ON);
        struct bind_row *off = bind_row(SPARK_BIND_CANVAS_OFF);
        (void)work;
        bind_run(canvas_is_on() ? on : off);
        atomic_set(&on->busy, 0);
        atomic_set(&off->busy, 0);
}

static void bind_machine_shift(unsigned int at)
{
        unsigned int n = --bind_machine.count - at;

        if (n)
                memory_copy(bind_machine.event + at, bind_machine.event + at + 1,
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
static bool bind_machine_push(struct bind_row *row)
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

        if (!(row->flags & BIND_STATE) && !row->command[0])
                return;

        if (row->flags & BIND_STATE) {
                struct bind_row *on = bind_row(SPARK_BIND_CANVAS_ON);
                struct bind_row *off = bind_row(SPARK_BIND_CANVAS_OFF);

                if (!on->command[0] && !off->command[0])
                        return;
                if (atomic_read(&on->busy) || atomic_read(&off->busy) ||
                    work_pending(&bind_state_work))
                        return;
                atomic_set(&on->busy, 1);
                atomic_set(&off->busy, 1);
                if (row->event == SPARK_BIND_CANVAS_ON)
                        atomic_set(&bind_state_told, 1);
                work = &bind_state_work;
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
                atomic_set(&bind_state_told, 0);

        if (bind_row_bound(row))
                bind_queue(row);
}

void moonwater_bind_fire(unsigned int event)
{
        bind_fire(event);
        moonwater_rules_spark(event);
}

/*
        Nobody holds the machine: no owner, nothing queued, no ending in
        progress. With bind_machine_lock held; the process it returns is the
        caller's to put once the lock is let go.
*/
static struct pid *bind_machine_reset(void)
{
        struct pid *process = bind_machine.process;

        bind_machine.owner = null;
        bind_machine.process = null;
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
        struct pid *gone = null;
        bool was_owner = false;

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
                strscpy(request->name, event == MOONWATER_ROVER ? "rover" : "end", sizeof(request->name));
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
                                atomic_set(&bind_state_told, 1);
                        bind_machine_fill(request, event);
                        spin_unlock_irqrestore(&bind_machine_lock, flags);
                        return 0;
                }
                //      Rover's ring has records a userspace owner acts on: the machine is that owner until the runner is.
                if (moonwater_rules_pending() != bind_machine.ring_seen) {
                        bind_machine.ring_seen = moonwater_rules_pending();
                        bind_machine_fill(request, MOONWATER_ROVER);
                        spin_unlock_irqrestore(&bind_machine_lock, flags);
                        return 0;
                }
                spin_unlock_irqrestore(&bind_machine_lock, flags);
                if (wait_ms) {
                        left = wait_event_interruptible_timeout(
                                bind_machine.wait,
                                READ_ONCE(bind_machine.count) ||
                                    READ_ONCE(bind_machine.ending) ||
                                    READ_ONCE(bind_machine.owner) != file ||
                                    moonwater_rules_pending() != READ_ONCE(bind_machine.ring_seen),
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
                                READ_ONCE(bind_machine.owner) != file ||
                                moonwater_rules_pending() != READ_ONCE(bind_machine.ring_seen)))
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
                              MOONWATER_ORIGIN_BUILTIN, null);
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
        if (bind_machine_admin[request.op] && !spark_file_capable(file, CAP_SYS_ADMIN))
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
                bind_machine.ring_seen = moonwater_rules_pending();
                request.queued = bind_machine.count;
                atomic_set(&bind_machine_live, 1);
                spin_unlock_irqrestore(&bind_machine_lock, flags);
                put_pid(before);
                bind_machine_watch_all();
                if (!atomic_read(&bind_state_told) && canvas_is_on())
                        bind_fire(SPARK_BIND_CANVAS_ON);
                request.flags = MOONWATER_ATTACHED;
                break;
        }
        case MOONWATER_DETACH:
                bind_machine_detach(file, null);
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

static long report_machine_script(struct file *file,
                                  struct machine_script __user *out)
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
                char *copy = null;

                /*      The text is root's: it is what root set, which a
                        person who cannot read root's files is kept from, and it can
                        carry what an operator would not print: a network's
                        password, a token in a command. /dev/spark is open to
                        everyone, so the copy needs the capability the
                        settings' GET needs. What anyone may still ask for is
                        the overlay, the length and the origin -- the answer
                        moonwater status is made from -- which is the request
                        that names no address. */
                if (request.address && !spark_file_capable(file, CAP_SYS_ADMIN))
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
                        memory_copy_apart(copy, machine_script_text, length);
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
                char *fresh = null;

                if (!spark_file_capable(file, CAP_SYS_ADMIN))
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
                                              MOONWATER_ORIGIN_BUILTIN, null);
                machine_script_answer(&request);
                mutex_unlock(&machine_script_lock);
                break;
        }
        default:
                return -EINVAL;
        }

        return copy_to_user(out, &request, sizeof(request)) ? -EFAULT : 0;
}

static bool bind_debounce(struct bind_row *row)
{
        unsigned long now = jiffies | 1;
        unsigned long last = READ_ONCE(row->last);

        if (!row->debounce)
                return true;
        if (last && time_before(now, last + row->debounce))
                return false;
        return cmpxchg(&row->last, last, now) == last;
}

// What the key watch costs a report, per processor: see seam.h.
static DEFINE_PER_CPU(struct handler_cost, bind_cost);

static struct bind_row *bind_match(unsigned int type, unsigned int code, int value)
{
        unsigned int event;

        if (type == EV_SW) {
                unsigned int i;

                if ((unsigned int)value > 1)
                        return null;
                for (i = 0; i < ARRAY_SIZE(bind_sw); i++)
                        if (bind_sw[i].code == code)
                                return bind_row(bind_sw[i].event[value]);
                return null;
        }
        if (type != EV_KEY || value != 1 || code >= BIND_CODES)
                return null;

        event = READ_ONCE(bind_code_event[code]);
        if (likely(!event))
                return null;
        if ((bind_spec[event - 1].flags & BIND_CAD) &&
            !(atomic_read(&bind_ctrl) > 0 && atomic_read(&bind_alt) > 0))
                return null;
        return bind_row(event);
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

static void bind_event_run(struct input_handle *handle, unsigned int type,
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

// Counted and, one in sixteen, timed: what `moonwater latency` shows. The
// kernel's watch is bind_filter now, which counts the same way; this is the
// bindings alone, as a test drives them.
static __attribute__((unused)) void bind_event(struct input_handle *handle, unsigned int type,
                       unsigned int code, int value)
{
        u64 started = moonwater_cost_begin(&bind_cost);

        bind_event_run(handle, type, code, value);
        moonwater_cost_end(&bind_cost, started);
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
        //      Deaf is said: a device that will not open is outside the floor,
        //      its power key, Reset and Ctrl-Alt-Delete reach nothing here.
        if (bind->opened)
                pr_warn("[moonwater] a key device would not open (%d): its keys reach no rule and no binding\n",
                        bind->opened);
}

// Rover's half of the key watch, in rules.c, which is expanded after this file.
static bool rules_input(struct bind_handle *bind, unsigned int type, unsigned int code, int value);
static void rules_device(struct bind_handle *bind, bool added);
static bool rules_device_physical(struct input_dev *dev);

/*
        The watch is a filter, so the input core puts it at the head of each
        device's handles and calls it before evdev and the console's keyboard:
        rover's rules hear an event before any reader does, and a rule that
        takes it drops it here. The bindings below hear what it lets through.
*/
static bool spark_input_push(struct spark_input_ring *ring, unsigned short kind, unsigned short code, int dx, int dy)
{
        unsigned long flags;
        u64 now_us = ktime_get_ns() / 1000ull;
        bool pushed = false, was_empty = false;

        spin_lock_irqsave(&spark_input_lock, flags);
        if (READ_ONCE(ring->active))
        {
                u64 head = ring->head;
                u64 tail = smp_load_acquire(&ring->tail);

                if (head - tail < SPARK_INPUT_SLOTS)
                {
                        struct spark_input_slot *s = &ring->slots[head & (SPARK_INPUT_SLOTS - 1u)];

                        s->dx = dx;
                        s->dy = dy;
                        s->time_us = now_us;
                        s->kind = kind;
                        s->code = code;
                        s->flags = 0;
                        smp_store_release(&ring->head, head + 1);
                        was_empty = (head == tail);
                        pushed = true;
                }
                else
                        ring->drops++;
        }
        spin_unlock_irqrestore(&spark_input_lock, flags);
        if (pushed && was_empty && wq_has_sleeper(&spark_input_wait))
                wake_up_interruptible(&spark_input_wait);
        return pushed;
}

// spark_input_lock held: one report into the live page, if there is one. The compositor's ring
// counts it (focus_seen), as a report it would have read, for the idle clocks.
static bool spark_focus_put(unsigned short kind, unsigned short code, int dx, int dy)
{
        struct spark_focus_reader *reader = spark_focus_live;
        struct spark_input_ring *ring = READ_ONCE(spark_input_page);
        struct spark_input_slot *s;

        if (!reader)
                return false;
        s = &reader->page->slots[reader->head & (SPARK_FOCUS_SLOTS - 1u)];
        s->dx = dx;
        s->dy = dy;
        s->time_us = ktime_get_ns() / 1000ull;
        s->kind = kind;
        s->code = code;
        s->flags = 0;
        WRITE_ONCE(reader->page->buttons, spark_focus_down);
        smp_store_release(&reader->page->head, ++reader->head);
        if (ring)
                WRITE_ONCE(ring->focus_seen, ring->focus_seen + 1);
        return true;
}

static bool spark_focus_push(unsigned short kind, unsigned short code, int dx, int dy)
{
        unsigned long flags;
        bool pushed;

        spin_lock_irqsave(&spark_input_lock, flags);
        pushed = spark_focus_put(kind, code, dx, dy);
        spin_unlock_irqrestore(&spark_input_lock, flags);
        return pushed;
}

/*
        A button and the focused page: a press under TAKE goes to the page alone and is held
        there; a release goes where its press went, so a reader never holds a button the person
        let go and the compositor never holds one it did not see pressed (a press held into a
        page that is gone is released nowhere). Under OBSERVE the page is given a copy. True when
        the filter takes the event from everything after it.
*/
static bool spark_focus_button(unsigned int code, int value)
{
        unsigned int bit = 1u << (code - BTN_LEFT);
        unsigned long flags;
        bool taken = false;

        if (value == 2)
                return false;
        spin_lock_irqsave(&spark_input_lock, flags);
        if (!value && (spark_focus_down & bit))
        {
                spark_focus_down &= ~bit;
                spark_focus_put(SPARK_INPUT_KIND_KEY, (unsigned short)code, 0, 0);
                taken = true;
        }
        else if (value && spark_focus_mode == SPARK_FOCUS_CLAIM_TAKE)
        {
                spark_focus_down |= bit;
                taken = spark_focus_put(SPARK_INPUT_KIND_KEY, (unsigned short)code, 1, 0);
                if (!taken)
                        spark_focus_down &= ~bit;
        }
        else if (spark_focus_mode == SPARK_FOCUS_CLAIM_OBSERVE)
                spark_focus_put(SPARK_INPUT_KIND_KEY, (unsigned short)code, value ? 1 : 0, 0);
        spin_unlock_irqrestore(&spark_input_lock, flags);
        return taken;
}

static _Bool bind_filter(struct input_handle *handle, unsigned int type,
                         unsigned int code, int value)
{
        struct bind_handle *bind = container_of(handle, struct bind_handle, handle);
        u64 started = moonwater_cost_begin(&bind_cost);
        bool taken = false;
        struct spark_input_ring *ring = READ_ONCE(spark_input_page);
        bool pointer = !test_bit(BTN_TOOL_FINGER, handle->dev->keybit) && !test_bit(ABS_MT_POSITION_X, handle->dev->absbit);
        bool spark_ok = ring && READ_ONCE(ring->active) && !bind->rover_virtual && pointer;
        unsigned int focus = pointer ? READ_ONCE(spark_focus_mode) : SPARK_FOCUS_CLAIM_OFF;

        //      The focused page first (spark_focus_page): under TAKE what it is
        //      given is taken from the compositor's ring and from evdev; under
        //      OBSERVE it is a copy and the event goes on as it would have.
        if (focus != SPARK_FOCUS_CLAIM_OFF)
        {
                bool take = focus == SPARK_FOCUS_CLAIM_TAKE;

                if (type == EV_REL && (code == REL_X || code == REL_Y))
                {
                        if (code == REL_X)
                                bind->focus_x = (int)((unsigned int)bind->focus_x + (unsigned int)value);
                        else
                                bind->focus_y = (int)((unsigned int)bind->focus_y + (unsigned int)value);
                        if (take)
                        {
                                taken = true;
                                goto counted;
                        }
                }
                else if (type == EV_REL && (code == REL_WHEEL || code == REL_HWHEEL || code == REL_WHEEL_HI_RES || code == REL_HWHEEL_HI_RES))
                {
                        bool coarse = (code == REL_WHEEL && test_bit(REL_WHEEL_HI_RES, handle->dev->relbit)) ||
                                      (code == REL_HWHEEL && test_bit(REL_HWHEEL_HI_RES, handle->dev->relbit));

                        if (!coarse)
                        {
                                if (bind->focus_x || bind->focus_y)
                                {
                                        spark_focus_push(SPARK_INPUT_KIND_REL, 0, bind->focus_x, bind->focus_y);
                                        bind->focus_x = bind->focus_y = 0;
                                }
                                if (!spark_focus_push(SPARK_INPUT_KIND_WHEEL, (unsigned short)code, value, 0))
                                        take = false;
                        }
                        if (take)
                        {
                                taken = true;
                                goto counted;
                        }
                }
                else if (type == EV_SYN && code == SYN_REPORT && (bind->focus_x || bind->focus_y))
                {
                        spark_focus_push(SPARK_INPUT_KIND_REL, 0, bind->focus_x, bind->focus_y);
                        bind->focus_x = bind->focus_y = 0;
                }
        }
        else if (unlikely(bind->focus_x || bind->focus_y))
                bind->focus_x = bind->focus_y = 0;

        if (spark_ok)
        {
                if (type == EV_REL && (code == REL_X || code == REL_Y))
                {
                        if (code == REL_X)
                                bind->rel_x = (int)((unsigned int)bind->rel_x + (unsigned int)value);
                        else
                                bind->rel_y = (int)((unsigned int)bind->rel_y + (unsigned int)value);
                        taken = true;
                        goto counted;
                }
                if (type == EV_REL && (code == REL_WHEEL || code == REL_HWHEEL || code == REL_WHEEL_HI_RES || code == REL_HWHEEL_HI_RES))
                {
                        if ((code == REL_WHEEL && test_bit(REL_WHEEL_HI_RES, handle->dev->relbit)) ||
                            (code == REL_HWHEEL && test_bit(REL_HWHEEL_HI_RES, handle->dev->relbit)))
                        {
                                taken = true;
                                goto counted;
                        }
                        if (bind->rel_x || bind->rel_y)
                        {
                                spark_input_push(ring, SPARK_INPUT_KIND_REL, 0, bind->rel_x, bind->rel_y);
                                bind->rel_x = bind->rel_y = 0;
                        }
                        taken = spark_input_push(ring, SPARK_INPUT_KIND_WHEEL, (unsigned short)code, value, 0);
                        goto counted;
                }
                if (type == EV_SYN && code == SYN_REPORT && (bind->rel_x || bind->rel_y))
                {
                        int dx = bind->rel_x, dy = bind->rel_y;

                        bind->rel_x = bind->rel_y = 0;
                        spark_input_push(ring, SPARK_INPUT_KIND_REL, 0, dx, dy);
                        goto counted;
                }
        }
        else if (unlikely(bind->rel_x || bind->rel_y))
                bind->rel_x = bind->rel_y = 0;

        if (unlikely(type == EV_KEY || type == EV_SW)) {
                //      A program's device (uinput, uhid) is no rule's, and
                //      it stops no machine and closes no lid: its switches
                //      and stop keys reach no binding either.
                if (unlikely(bind->rover_virtual) &&
                    (type == EV_SW || (code < BIND_CODES && spark_bind_is_stop(READ_ONCE(bind_code_event[code])))))
                        goto counted;
                taken = rules_input(bind, type, code, value);
                if (!taken)
                        bind_event_run(handle, type, code, value);
                //      The bindings' Ctrl and Alt follow the physical keys
                //      whatever a rule took, so a taken Ctrl never leaves
                //      Ctrl-Alt-Delete with no Ctrl held (bind_event_run
                //      counts them for every event it is given).
                else if (type == EV_KEY)
                        bind_mods(bind, code, value);
                //      A button, after the rules and the bindings heard it: the
                //      focused page's (spark_focus_button), whose releases follow
                //      their presses even after the page is gone.
                if (!taken && pointer && type == EV_KEY && code >= BTN_LEFT && code <= BTN_TASK &&
                    (focus != SPARK_FOCUS_CLAIM_OFF || READ_ONCE(spark_focus_down)))
                {
                        if (bind->focus_x || bind->focus_y)
                        {
                                spark_focus_push(SPARK_INPUT_KIND_REL, 0, bind->focus_x, bind->focus_y);
                                bind->focus_x = bind->focus_y = 0;
                        }
                        taken = spark_focus_button(code, value);
                }
                if (!taken && spark_ok && type == EV_KEY &&
                    ((code >= BTN_LEFT && code <= BTN_TASK) || (code > 0 && code < 256)))
                {
                        if (bind->rel_x || bind->rel_y)
                        {
                                spark_input_push(ring, SPARK_INPUT_KIND_REL, 0, bind->rel_x, bind->rel_y);
                                bind->rel_x = bind->rel_y = 0;
                        }
                        taken = spark_input_push(ring, SPARK_INPUT_KIND_KEY, (unsigned short)code, value, 0);
                        // Returning taken=true from input_handler.filter strips EV_KEY from vals[] before
                        // input_pass_values() runs its post-handler autorepeat loop: mirror input_start/stop_autorepeat
                        // here (dev->event_lock is held) so kernel key repeat keeps firing for all native UI apps.
                        if (taken && value != 2 && test_bit(EV_REP, handle->dev->evbit) &&
                            handle->dev->rep[REP_PERIOD] && handle->dev->rep[REP_DELAY] &&
                            handle->dev->timer.function)
                        {
                                if (value)
                                {
                                        handle->dev->repeat_key = code;
                                        mod_timer(&handle->dev->timer, jiffies + msecs_to_jiffies(handle->dev->rep[REP_DELAY]));
                                }
                                else if (handle->dev->repeat_key == code)
                                        timer_delete(&handle->dev->timer);
                        }
                }
        }
counted:
        moonwater_cost_end(&bind_cost, started);
        return taken;
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
        bind->rover_virtual = !rules_device_physical(dev);
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
        rules_device(bind, true);
        return 0;
}

static void bind_disconnect(struct input_handle *handle)
{
        struct bind_handle *bind = container_of(handle, struct bind_handle, handle);
        unsigned int i;

        cancel_work_sync(&bind->open);
        if (!bind->opened)
                input_close_device(handle);
        input_unregister_handle(handle);
        rules_device(bind, false);
        for (i = 0; i < ARRAY_SIZE(bind_mod_code); i++)
                bind_mods(bind, bind_mod_code[i], 0);
        kfree(bind);
}

static const struct input_device_id bind_ids[] = {
        { .flags = INPUT_DEVICE_ID_MATCH_EVBIT, .evbit = { BIT_MASK(EV_KEY) } },
        { .flags = INPUT_DEVICE_ID_MATCH_EVBIT, .evbit = { BIT_MASK(EV_SW) } },
        { .flags = INPUT_DEVICE_ID_MATCH_EVBIT, .evbit = { BIT_MASK(EV_REL) } },
        {},
};

static struct input_handler bind_handler = {
        .filter = bind_filter,
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

        INIT_WORK(&bind_state_work, bind_state_run);
        init_waitqueue_head(&bind_machine.wait);
        machine_script_reset();
        spin_lock_irq(&bind_machine_lock);
        bind_machine_reset();
        spin_unlock_irq(&bind_machine_lock);
        atomic_set(&bind_ctrl, 0);
        atomic_set(&bind_alt, 0);
        memory_fill(bind_code_event, 0, sizeof(bind_code_event));

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
        spark_input_init();
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
        spark_input_exit();

        cancel_work_sync(&bind_state_work);
        for (at = 0; at < SPARK_BIND_EVENTS; at++)
                cancel_work_sync(&bind_table[at].work);

        mutex_lock(&machine_script_lock);
        kfree(machine_script_held);
        machine_script_held = null;
        machine_script_text = null;
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
        tell that it was changed. Empty is every byte of it: the line was
        copied in whole to be compared, and the answer is the whole field.
*/
static void bind_answer(struct bind_control *request, struct bind_row *row,
                        bool reveal)
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
        if (!string_compare(request->command, row->def))
                request->flags |= SPARK_BIND_DEFAULT;
        else if (!reveal)
                memory_fill(request->command, 0, sizeof(request->command));
        if (atomic_read(&row->busy))
                request->flags |= SPARK_BIND_RUNNING;
        if (work_pending(&row->work) ||
            ((row->flags & BIND_STATE) && work_pending(&bind_state_work)))
                request->flags |= SPARK_BIND_PENDING;
        if (row->flags & BIND_BOOT)
                request->flags |= SPARK_BIND_BOOT;
}

static long report_bind(struct file *file, struct bind_control __user *out)
{
        struct bind_control request;
        struct bind_row *row;
        unsigned long flags;
        bool admin;

        if (copy_from_user(&request, out, sizeof(request)))
                return -EFAULT;
        if (request.op > SPARK_BIND_SET || request.reserved[0] ||
            request.reserved[1] || request.reserved[2])
                return -EINVAL;

        row = bind_row(request.event);
        if (!row)
                return -EINVAL;

        admin = spark_file_capable(file, CAP_SYS_ADMIN);
        if (request.op == SPARK_BIND_SET) {
                if (!admin ||
                    ((row->flags & BIND_BOOT) && !spark_file_capable(file, CAP_SYS_BOOT))) {
                        /* CAP_SYS_BOOT alone is not the capability that
                           reads a line, and the answer to a refusal is a
                           read like any other. */
                        bind_answer(&request, row, admin);
                        return copy_to_user(out, &request, sizeof(request))
                                       ? -EFAULT
                                       : -EPERM;
                }
                if (!memory_first_of(request.command, 0, sizeof(request.command)))
                        return -ENAMETOOLONG;

                spin_lock_irqsave(&bind_lock, flags);
                strscpy(row->command,
                        request.command[0] ? request.command : row->def,
                        sizeof(row->command));
                atomic_set(&row->bound, row->command[0] != 0);
                bind_watch_row(row);
                spin_unlock_irqrestore(&bind_lock, flags);
        }

        bind_answer(&request, row, admin);
        return copy_to_user(out, &request, sizeof(request)) ? -EFAULT : 0;
}

#endif /* STANDARD_MODERN_C_KERNEL */

#endif /* MOONWATER_INCLUDED */

#ifdef MOONWATER_CLI
/*
        The machine process: PID 1's moonwater machine, attached to the
        module, and the runner from there (src/rover/runner.c), which runs
        the rules in place of the machine script there was.
*/
fn shell_stop(writer write, positive command);
bool env_assign(const_string name, const_string value);

//      This process owns the machine attach, from the moment it does.
static bool host_machine_self;
//      The machine's end has been had; it is had once however the machine stops.
static bool host_machine_ended;

#define HOST_MACHINE_ASSIGN(name, value) env_assign(name, value);

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

static fn host_machine_close(bipolar device)
{
        struct machine_control control;

        if (device < 0)
                return;
        (void)host_machine_ioctl(device, MOONWATER_DETACH, address_of control);
        system_close(device);
}

//      The machine's end, at most once, wherever the machine turns out to stop: the runner's services are its to stop.
static fn host_machine_end_run(void)
{
        host_machine_ended = true;
}

/*
        Telling the machine process the machine is stopping, and waiting for
        it to let go -- unless we are it. True once the process was told, and
        was seen to leave. False when there was no process to tell, when it
        would not be told (the caller is not root: END is CAP_SYS_ADMIN's),
        and when it was still attached after ten seconds, a machine process
        that never let go, so the stop must not behave as though it had.

        host_machine_self is set the moment this process owns the attach, not
        when it is told to stop: a poweroff the runner asks for comes through
        here, and a process that asked the kernel to end the attach it is
        itself holding would poll for ten seconds for a detach only it could
        perform -- ten seconds in which the button already pressed looks like
        a button that did nothing.
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

/*
        The attach, asked again for a moment while another process has it.

        The one that held it is most often the machine process this one
        replaces, and it lets go as it exits: asked the instant before, this
        process is answered EBUSY for the little that the other still has to
        do, and each such answer counted toward init's giving the machine up
        for the boot. A process that is told EBUSY for the whole second is
        not racing one that is leaving, it is a second machine process, and
        that is refused as it always was.
*/
#define HOST_MACHINE_ATTACH_NS ((p64)1000000000)

static bipolar host_machine_attach(bipolar device, struct machine_control address_to control)
{
        p64 started = system_clock_ns(HOST_CLOCK_BOOTTIME);
        bipolar failed;

        while ((failed = host_machine_ioctl(device, MOONWATER_ATTACH, control)) == -EBUSY &&
               system_clock_ns(HOST_CLOCK_BOOTTIME) - started < HOST_MACHINE_ATTACH_NS)
                host_pause(HOST_EVENT_POLL_NS);
        return failed;
}

#define HOST_MACHINE_ENDED 0
#define HOST_MACHINE_STOPPED 1
#define HOST_MACHINE_FAILED 2

#include "../rover/runner.c"

static b32 host_machine_run(void)
{
        struct machine_control control;
        bipolar device;
        bipolar failed;

        if (!bowl_is_root())
                return host_refuse("moonwater machine needs root\n");

        host_say(log, host_label "commit %s (%s)\n", host_commit, host_commit_date);

        host_state_ready();
        host_input_none();
        system_call_1(syscall(chdir), (positive)(string_address) "/root");
        bowl_session_prepare("/root", null);
        SPARK_COMMAND_ENVIRONMENT(HOST_MACHINE_ASSIGN)

        //      The returns below come before the restores, and init starts
        //      this process again after each (it answers HOST_MACHINE_FAILED),
        //      every start doing them. What they skip is done elsewhere for a
        //      session with a disk, whose take did it (host_take), and for a
        //      start after another process's, which did it before it died or
        //      kept the attach.
        device = system_open_at(AT_FDCWD, SPARK_DEVICE, FILE_READ | O_CLOEXEC);
        if (device < 0) {
                host_fail(SPARK_DEVICE, device);
                return HOST_MACHINE_FAILED;
        }
        failed = host_machine_attach(device, address_of control);
        if (failed < 0) {
                system_close(device);
                host_fail("machine attach", failed);
                return HOST_MACHINE_FAILED;
        }
        //      The attach is ours from here, and every stop this process
        //      performs goes through host_machine_stop, including the one
        //      a rule's poweroff asks for.
        host_machine_self = true;
        failed = rover_runner_run(device);
        host_machine_close(device);
        return failed;
}

#endif /* MOONWATER_CLI */
