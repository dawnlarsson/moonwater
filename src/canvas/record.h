/*
        The present record (moonwater canvas record SECONDS, moonwater canvas record report): what each present of the compositor did, kept
        always in a ring of COMPOSITOR_RECORD_SIZE presents (its path, its damage, its compose time, its flip and the vblank that answered it, the
        flips refused, the damage held back, the lists drawn and refused), and the report of those presents, which moonwater prints. A recording
        for SECONDS (1 to 10) counts what its presents did as well. Files under /run/moonwater/record: start (the seconds a recording runs, written
        by moonwater canvas record, taken by the next look), report (a request for the report, written by moonwater canvas record report, taken
        by the next look: the report.txt is written, and the request removed), report.txt and state (the compositor's answers). A look is made
        every COMPOSITOR_RECORD_POLL presents: the card's present is the only clock the record has.

        Included by compositor.c (after compositor_flip_refused and compositor_list_counts, which it reads).
*/
#define COMPOSITOR_RECORD_SIZE 240
#define COMPOSITOR_RECORD_POLL 8
#define COMPOSITOR_RECORD_MAX_SECONDS 10
#define COMPOSITOR_RECORD_LATE_NS 25000000ull // more than 1.5 frames at 60 Hz between two presents that drew
#define COMPOSITOR_RECORD_DIR "/run/moonwater/record"
#define COMPOSITOR_RECORD_TEXT 8192

enum compositor_record_path
{
        COMPOSITOR_RECORD_PROCESSOR,
        COMPOSITOR_RECORD_CARD,
        COMPOSITOR_RECORD_PLANE
};

struct compositor_record
{
        unsigned long long when, compose_ns, flip_at, vblank_at;
        int damage[4];
        unsigned int path, damaged, held, flipped, vblanked, refused, late, lists, lists_refused;
};

static struct compositor_record compositor_records[COMPOSITOR_RECORD_SIZE];
static unsigned int compositor_records_at;     // the slot the next present takes
static unsigned int compositor_records_kept;   // presents in the ring, up to its size
static unsigned long long compositor_records_all; // presents in all, since the compositor started
static unsigned int compositor_records_flip;   // the slot of a flip waiting for its vblank, plus one (0: none)
static unsigned int compositor_record_polls;
static unsigned int compositor_record_refusals; // flips the card refused, counted as they happen (the refusal's own counter is reset at three)
static unsigned long long compositor_record_until; // the end of a recording (0: none running)
static unsigned int compositor_record_seconds, compositor_record_frames, compositor_record_late, compositor_record_refused, compositor_record_held,
    compositor_record_flips;
static char compositor_record_text[COMPOSITOR_RECORD_TEXT];
static unsigned int compositor_record_text_at;
static char compositor_record_state[256];

/*
        What a present is measured from: its damage's union (the rectangle the present would draw), whether it had any, whether a flip was waiting
        for its vblank, the flip refusals and lists counted before it. Compositor_record_end reads the same counts after the present.
*/
static struct
{
        int damage[4];
        unsigned int damaged, pending, refusals, lists, lists_refused, screen;
} compositor_record_mark;

static void compositor_record_begin(void)
{
        int rect[4] = {compositor.width, compositor.height, 0, 0};

        compositor_record_mark.damaged = compositor.damages != 0;
        for (unsigned int at = 0; at < compositor.damages && at < COMPOSITOR_DAMAGE; at++)
        {
                const int *d = compositor.damage[at];

                rect[0] = d[0] < rect[0] ? d[0] : rect[0];
                rect[1] = d[1] < rect[1] ? d[1] : rect[1];
                rect[2] = d[2] > rect[2] ? d[2] : rect[2];
                rect[3] = d[3] > rect[3] ? d[3] : rect[3];
        }
        memory_copy(compositor_record_mark.damage, rect, sizeof(rect));
        compositor_record_mark.pending = compositor.flip_pending != 0;
        compositor_record_mark.screen = compositor.on_screen;
        compositor_record_mark.refusals = compositor_record_refusals;
        compositor_record_mark.lists = compositor_list_counts[COMPOSITOR_LIST_DRAWN];
        compositor_record_mark.lists_refused = compositor.table ? compositor.table->list_refused : 0;
}

/*
        Written at each present that had damage: the ring slot, and the recording's counts while one runs. The present's late mark is against the
        one before it in the ring (a present with damage: an idle desktop makes none, so none is late).
*/
static void compositor_record_end(unsigned long long began, unsigned long long ended, enum compositor_record_path path)
{
        struct compositor_record *r;
        unsigned int prev;

        if (!compositor_record_mark.damaged)
                return;
        r = &compositor_records[compositor_records_at];
        memory_zero(r, sizeof(*r));
        prev = (compositor_records_at + COMPOSITOR_RECORD_SIZE - 1) % COMPOSITOR_RECORD_SIZE;
        r->when = began;
        r->compose_ns = ended - began;
        memory_copy(r->damage, compositor_record_mark.damage, sizeof(r->damage));
        r->path = path;
        r->damaged = 1;
        // The damage the present did not draw stays for the next one (the early return of compositor_present_frames).
        r->held = compositor.damages != 0;
        // A card's flip is pending from its issue to its vblank; a headless flip exchanges the names at once (the screen changes, nothing is pending).
        r->flipped = (!compositor_record_mark.pending && compositor.flip_pending != 0) || compositor_record_mark.screen != compositor.on_screen;
        if (r->flipped)
        {
                r->flip_at = ended;
                compositor_records_flip = compositor_records_at + 1;
        }
        r->refused = compositor_record_refusals - compositor_record_mark.refusals;
        r->lists = (unsigned int)(compositor_list_counts[COMPOSITOR_LIST_DRAWN] - compositor_record_mark.lists);
        r->lists_refused = compositor.table ? compositor.table->list_refused - compositor_record_mark.lists_refused : 0;
        if (compositor_records_kept && began > compositor_records[prev].when && began - compositor_records[prev].when > COMPOSITOR_RECORD_LATE_NS)
                r->late = 1;
        compositor_records_at = (compositor_records_at + 1) % COMPOSITOR_RECORD_SIZE;
        if (compositor_records_kept < COMPOSITOR_RECORD_SIZE)
                compositor_records_kept++;
        compositor_records_all++;
        if (compositor_record_until)
        {
                compositor_record_frames++;
                compositor_record_late += r->late;
                compositor_record_refused += r->refused;
                compositor_record_held += r->held;
                compositor_record_flips += r->flipped;
        }
}

// A flip the card refused (EBUSY, or any other answer): counted for the present in hand, whichever way the present goes after it.
static void compositor_record_refuse(void)
{
        compositor_record_refusals++;
}

// The vblank that answers the flip still waiting, now.
static void compositor_record_vblank(unsigned long long now)
{
        struct compositor_record *r;

        if (!compositor_records_flip)
                return;
        r = &compositor_records[compositor_records_flip - 1];
        r->vblank_at = now;
        r->vblanked = 1;
        compositor_records_flip = 0;
}

/* The text of the report: a number in decimal, and a string, appended. */
static void compositor_record_put_text(const char *text)
{
        while (*text && compositor_record_text_at < COMPOSITOR_RECORD_TEXT - 1)
                compositor_record_text[compositor_record_text_at++] = *text++;
}

static void compositor_record_put_number(unsigned long long value)
{
        char digits[24];
        unsigned int at = 0;

        do
        {
                digits[at++] = (char)('0' + value % 10);
                value /= 10;
        } while (value && at < sizeof(digits));
        while (at)
                if (compositor_record_text_at < COMPOSITOR_RECORD_TEXT - 1)
                        compositor_record_text[compositor_record_text_at++] = digits[--at];
                else
                        at = 0;
}

static void compositor_record_write_file(const char *name, const char *bytes, unsigned long length)
{
        char path[96];
        long handle;

        memory_zero(path, sizeof(path));
        memory_copy(path, COMPOSITOR_RECORD_DIR, string_length((string_address)COMPOSITOR_RECORD_DIR));
        path[string_length((string_address)COMPOSITOR_RECORD_DIR)] = '/';
        memory_copy(path + string_length((string_address)COMPOSITOR_RECORD_DIR) + 1, name, string_length((string_address)name));
        handle = (long)system_open_at_mode(AT_FDCWD, (string_address)path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0644);
        if (handle < 0)
                return;
        system_write_once(handle, bytes, length);
        system_close(handle);
}

/*
        The report: the totals of the ring, its per-path counts, the flips and their vblanks, the presents late, and the recording's counts
        when one has run. Written to report.txt, and its one-line state to state.
*/
static void compositor_record_report(void)
{
        unsigned int first, count, paths[3] = {0, 0, 0};
        unsigned long long longest = 0, origin;
        unsigned int refused = 0, late = 0, held = 0, flips = 0, vblanks = 0, lists = 0, lists_refused = 0;

        compositor_record_text_at = 0;
        count = compositor_records_kept;
        first = count < COMPOSITOR_RECORD_SIZE ? 0 : compositor_records_at;
        origin = count ? compositor_records[first].when : 0;
        for (unsigned int at = 0; at < count; at++)
        {
                const struct compositor_record *r = &compositor_records[(first + at) % COMPOSITOR_RECORD_SIZE];

                paths[r->path < 3 ? r->path : 0]++;
                refused += r->refused, late += r->late, held += r->held, flips += r->flipped, lists += r->lists, lists_refused += r->lists_refused;
                if (r->vblanked && r->vblank_at - r->flip_at > longest)
                        longest = r->vblank_at - r->flip_at;
                vblanks += r->vblanked;
        }
        compositor_record_put_text("moonwater canvas record: the presents of the compositor\n");
        compositor_record_put_text("presents      "), compositor_record_put_number(count), compositor_record_put_text(" in the ring of ");
        compositor_record_put_number(COMPOSITOR_RECORD_SIZE), compositor_record_put_text(", ");
        compositor_record_put_number(compositor_records_all), compositor_record_put_text(" in all\n");
        compositor_record_put_text("paths        card "), compositor_record_put_number(paths[COMPOSITOR_RECORD_CARD]);
        compositor_record_put_text(", plane "), compositor_record_put_number(paths[COMPOSITOR_RECORD_PLANE]);
        compositor_record_put_text(", processor "), compositor_record_put_number(paths[COMPOSITOR_RECORD_PROCESSOR]), compositor_record_put_text("\n");
        compositor_record_put_text("flips        "), compositor_record_put_number(flips), compositor_record_put_text(" issued, ");
        compositor_record_put_number(refused), compositor_record_put_text(" refused, ");
        compositor_record_put_number(vblanks), compositor_record_put_text(" answered by a vblank, longest ");
        compositor_record_put_number(longest / 1000), compositor_record_put_text(" us\n");
        compositor_record_put_text("held         "), compositor_record_put_number(held), compositor_record_put_text(" presents kept their damage for the next\n");
        compositor_record_put_text("late         "), compositor_record_put_number(late), compositor_record_put_text(" presents more than 25 ms after the one before\n");
        compositor_record_put_text("lists        "), compositor_record_put_number(lists), compositor_record_put_text(" drawn from lists, ");
        compositor_record_put_number(lists_refused), compositor_record_put_text(" refused\n");
        if (compositor_record_until)
        {
                compositor_record_put_text("recording    running, "), compositor_record_put_number(compositor_record_frames), compositor_record_put_text(" presents so far of ");
                compositor_record_put_number(compositor_record_seconds), compositor_record_put_text(" s\n");
        }
        else if (compositor_record_frames)
        {
                compositor_record_put_text("recording    done: "), compositor_record_put_number(compositor_record_frames), compositor_record_put_text(" presents, ");
                compositor_record_put_number(compositor_record_late), compositor_record_put_text(" late, ");
                compositor_record_put_number(compositor_record_refused), compositor_record_put_text(" refused flips, ");
                compositor_record_put_number(compositor_record_held), compositor_record_put_text(" held\n");
        }
        else
                compositor_record_put_text("recording    none yet (moonwater canvas record SECONDS)\n");
        // The events, oldest first: the late presents, the refused flips, the held presents (at most forty lines).
        compositor_record_put_text("events\n");
        for (unsigned int at = 0, shown = 0; at < count && shown < 40; at++)
        {
                const struct compositor_record *r = &compositor_records[(first + at) % COMPOSITOR_RECORD_SIZE];

                if (!r->late && !r->refused && !r->held)
                        continue;
                shown++;
                compositor_record_put_text("  ");
                compositor_record_put_number((r->when - origin) / 1000000), compositor_record_put_text(" ms  ");
                compositor_record_put_text(r->late ? "late " : "");
                compositor_record_put_text(r->refused ? "flip refused " : "");
                compositor_record_put_text(r->held ? "held " : "");
                compositor_record_put_text("\n");
        }
        compositor_record_write_file("report.txt", compositor_record_text, compositor_record_text_at);
        // The state: one line, what moonwater debug shows as the last record.
        compositor_record_text_at = 0;
        compositor_record_put_text("last: ");
        compositor_record_put_number(compositor_record_frames), compositor_record_put_text(" presents recorded, ");
        compositor_record_put_number(compositor_record_late), compositor_record_put_text(" late, ");
        compositor_record_put_number(compositor_record_refused), compositor_record_put_text(" refused flips, ");
        compositor_record_put_number(compositor_record_held), compositor_record_put_text(" held; ");
        compositor_record_put_number(compositor_records_all), compositor_record_put_text(" presents in all\n");
        compositor_record_write_file("state", compositor_record_text, compositor_record_text_at);
}

/*
        A look at the files of moonwater canvas record. Every COMPOSITOR_RECORD_POLL presents: the start file (a recording, its seconds taken from
        the file; the file goes), the report request (the report is written; the request goes).
*/
static void compositor_record_look(unsigned long long now)
{
        char word[16];
        long handle;
        long got;
        unsigned long long secs = 0;

        compositor_call(syscall(mkdirat), (positive)AT_FDCWD, (positive)COMPOSITOR_RECORD_DIR, 0755, 0, 0, 0);
        handle = (long)system_open_at(AT_FDCWD, (string_address)COMPOSITOR_RECORD_DIR "/start", FILE_READ | O_CLOEXEC | O_NOFOLLOW);
        if (handle >= 0)
        {
                got = (long)system_read_once(handle, word, sizeof(word) - 1);
                system_close(handle);
                compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)COMPOSITOR_RECORD_DIR "/start", 0, 0, 0, 0);
                for (long at = 0; at < got && word[at] >= '0' && word[at] <= '9'; at++)
                        secs = secs * 10 + (unsigned long long)(word[at] - '0');
                if (got > 0 && secs >= 1 && secs <= COMPOSITOR_RECORD_MAX_SECONDS)
                {
                        compositor_record_seconds = (unsigned int)secs;
                        compositor_record_until = now + secs * 1000000000ull;
                        compositor_record_frames = compositor_record_late = compositor_record_refused = compositor_record_held = 0;
                        compositor_record_flips = 0;
                }
        }
        handle = (long)system_open_at(AT_FDCWD, (string_address)COMPOSITOR_RECORD_DIR "/report", FILE_READ | O_CLOEXEC | O_NOFOLLOW);
        if (handle >= 0)
        {
                system_close(handle);
                compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)COMPOSITOR_RECORD_DIR "/report", 0, 0, 0, 0);
                compositor_record_report();
        }
}

/*
        A present's end: the record, a look every COMPOSITOR_RECORD_POLL presents, and the end of a recording that has run its seconds (its report
        is written then, and the state says it is done).
*/
static void compositor_record_present(unsigned long long began, unsigned long long ended, enum compositor_record_path path)
{
        compositor_record_end(began, ended, path);
        if (compositor_record_polls++ % COMPOSITOR_RECORD_POLL == 0)
                compositor_record_look(ended);
        if (compositor_record_until && ended >= compositor_record_until)
        {
                compositor_record_until = 0;
                compositor_record_report();
        }
}
