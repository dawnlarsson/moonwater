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
        unsigned int path, damaged, held, flipped, vblanked, refused, backoffs, late, lists, lists_refused;
};

static struct compositor_record compositor_records[COMPOSITOR_RECORD_SIZE];
static unsigned int compositor_records_at;     // the slot the next present takes
static unsigned int compositor_records_kept;   // presents in the ring, up to its size
static unsigned long long compositor_records_all; // presents in all, since the compositor started
static unsigned int compositor_records_flip;   // the slot of a flip waiting for its vblank, plus one (0: none)
static unsigned int compositor_record_polls;
static unsigned int compositor_record_refusals; // flips the card refused, counted as they happen (the refusal's own counter is reset at three)
static unsigned int compositor_record_backoffs; // refusals that were the card busy (a back-off, scanout.c compositor_refusal_fatal), counted as they happen
static unsigned int compositor_record_remakes; // the overlay's buffers asked to be made again (scanout.c compositor_overlay_buffers), counted as they are asked
static unsigned long long compositor_record_until; // the end of a recording (0: none running)
static unsigned int compositor_record_seconds, compositor_record_frames, compositor_record_late, compositor_record_refused, compositor_record_held,
    compositor_record_flips;
static char compositor_record_text[COMPOSITOR_RECORD_TEXT];
static unsigned int compositor_record_text_at;

/*
        What a present is measured from: its damage's union (the rectangle the present would draw), whether it had any, whether a flip was waiting
        for its vblank, the flip refusals and lists counted before it. Compositor_record_end reads the same counts after the present.
*/
static struct
{
        int damage[4];
        unsigned int damaged, pending, refusals, backoffs, lists, lists_refused, screen;
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
        compositor_record_mark.backoffs = compositor_record_backoffs;
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
        r->backoffs = compositor_record_backoffs - compositor_record_mark.backoffs;
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
        The frames of a recording. While one runs, each present that had damage has its scanout copied in full, into one of three buffers (frame
        k in buffer k mod 3), so a frame is classified against the one before it and the one after it; and a quarter of it (every fourth pixel) goes
        into a ring that the report counts. A frame is one of four classes, judged against P (before) and N (after) with S the frame:
          tear    every row of S is a row of P or of N, both kinds present, and S is neither (a band written before the read, the rest after)
          flash   S is neither P nor N while P and N are the same (a frame that was never on the screen between two that were)
          size    the window pictures' sizes differ in S from P and N, which are the same (a window one frame the wrong size)
          blank   a window's area is uniformly the ground colour in S, and not in P or N (a window cleared for a frame)
        A frame in a class is written to /run/moonwater/record/frame-SEQ-CLASS.ppm (at most COMPOSITOR_RECORD_KEEP files a recording). The check
        injects a tear: when /run/moonwater/record/inject is there (taken, and removed, by the classification of the next frame), the frame is
        spliced (the rows of P in a band a quarter down the screen, the rows of N elsewhere), and only the classification sees it.
*/
#define COMPOSITOR_RECORD_KEEP 8
#define COMPOSITOR_RECORD_RING 256
#define COMPOSITOR_RECORD_WORK_MAX (32ul << 20)  // three full frames, at most
#define COMPOSITOR_RECORD_RING_MAX (16ul << 20)  // the quarter frames, at most
#define COMPOSITOR_RECORD_EVENTS 16
enum compositor_record_class
{
        COMPOSITOR_RECORD_TEAR,
        COMPOSITOR_RECORD_FLASH,
        COMPOSITOR_RECORD_SIZE_JUMP,
        COMPOSITOR_RECORD_BLANK,
        COMPOSITOR_RECORD_CLASSES
};
static const char *compositor_record_class_name[COMPOSITOR_RECORD_CLASSES] = {"tear", "flash", "size", "blank"};

struct compositor_record_shot
{
        unsigned int *pixels;           // width times height, the scanout's pixels
        unsigned long long when;
        unsigned int seq, sizes, rect_count;
        int (*rects)[4];                // the window pictures: x1 y1 x2 y2, a row each
};
static struct compositor_record_shot compositor_record_shots[3];
static unsigned int *compositor_record_block;     // the mapping of the three frames and the ring
static unsigned long compositor_record_block_bytes;
static unsigned int compositor_record_width, compositor_record_height, compositor_record_ring_w, compositor_record_ring_h;
static unsigned int compositor_record_ring_cap, compositor_record_ring_at, compositor_record_ring_count;
static unsigned int *compositor_record_ring_pixels;
static unsigned int compositor_record_taken;      // frames copied in this recording
static unsigned int compositor_record_classed[COMPOSITOR_RECORD_CLASSES], compositor_record_written;
static unsigned int compositor_record_class_written[COMPOSITOR_RECORD_CLASSES]; // the files written, by class: each class keeps its own eight
static unsigned int compositor_record_refused_size; // the screen is too large for the budget: nothing is classified
static unsigned int compositor_record_events[COMPOSITOR_RECORD_EVENTS][3]; // seq, bits (1 << class), kept
static unsigned int compositor_record_event_count;
static int (*compositor_record_pending)[4]; // the window pictures of the present in hand (compositor_record_scene)
static unsigned int compositor_record_pending_count, compositor_record_pending_sizes;

// The window pictures of the present in hand, from compositor_present_frames: their rectangles and their sizes (a hash).
static void compositor_record_scene(const struct compositor_frame *frames, unsigned int windows)
{
        unsigned int sizes = 2166136261u;

        compositor_record_pending_count = windows < compositor_rows_in_use ? windows : compositor_rows_in_use;
        for (unsigned int at = 0; at < compositor_record_pending_count; at++)
        {
                compositor_record_pending[at][0] = frames[at].x;
                compositor_record_pending[at][1] = frames[at].y;
                compositor_record_pending[at][2] = frames[at].x + frames[at].width;
                compositor_record_pending[at][3] = frames[at].y + frames[at].height;
                sizes = (sizes ^ (unsigned int)frames[at].width) * 16777619u;
                sizes = (sizes ^ (unsigned int)frames[at].height) * 16777619u;
        }
        compositor_record_pending_sizes = sizes;
}

static void compositor_record_free(void)
{
        void *p = compositor_record_block;

        mem_buf_free(&p, &compositor_record_block_bytes, 0);
        compositor_record_block = 0;
        compositor_record_ring_pixels = 0;
        for (unsigned int at = 0; at < 3; at++)
                compositor_record_shots[at].pixels = 0;
}

// The mapping for the three frames and the ring, made at the first frame of a recording; false when the screen is too large for the budget.
static int compositor_record_alloc(void)
{
        unsigned long bytes, ring_bytes, ring_cap, total;
        void *p = 0;

        if (compositor_record_block)
                return 1;
        if (!compositor.width || !compositor.height)
                return 0;
        bytes = (unsigned long)compositor.width * (unsigned long)compositor.height * 4;
        if (3 * bytes > COMPOSITOR_RECORD_WORK_MAX)
                return 0;
        compositor_record_ring_w = compositor.width / 4 ? compositor.width / 4 : 1;
        compositor_record_ring_h = compositor.height / 4 ? compositor.height / 4 : 1;
        ring_bytes = (unsigned long)compositor_record_ring_w * compositor_record_ring_h * 4;
        ring_cap = COMPOSITOR_RECORD_RING_MAX / ring_bytes;
        ring_cap = ring_cap < COMPOSITOR_RECORD_RING ? ring_cap : COMPOSITOR_RECORD_RING;
        total = 3 * bytes + ring_cap * ring_bytes;
        if (!mem_buf_grow(&p, &compositor_record_block_bytes, total, 0))
                return 0;
        compositor_record_block = (unsigned int *)p;
        compositor_record_width = compositor.width;
        compositor_record_height = compositor.height;
        for (unsigned int at = 0; at < 3; at++)
                compositor_record_shots[at].pixels = compositor_record_block + at * (bytes / 4);
        compositor_record_ring_pixels = compositor_record_block + 3 * (bytes / 4);
        compositor_record_ring_cap = (unsigned int)ring_cap;
        return 1;
}

// Whether the rows of two frames are the same, and whether the whole frames are.
static int compositor_record_row_same(const unsigned int *a, const unsigned int *b, unsigned int width)
{
        return !memory_compare(a, b, (unsigned long)width * 4);
}

static int compositor_record_same(const unsigned int *a, const unsigned int *b, unsigned int width, unsigned int height)
{
        return compositor_record_row_same(a, b, width * height);
}

// The tear: every row of S is one of P's or N's, both kinds present, and the frame is neither P nor N.
static int compositor_record_torn(const unsigned int *s, const unsigned int *p, const unsigned int *n, unsigned int width, unsigned int height)
{
        int has_p = 0, has_n = 0;

        if (compositor_record_same(s, p, width, height) || compositor_record_same(s, n, width, height))
                return 0;
        for (unsigned int y = 0; y < height; y++)
        {
                const unsigned int *row = s + (unsigned long)y * width;

                if (compositor_record_row_same(row, p + (unsigned long)y * width, width))
                        has_p = 1;
                else if (compositor_record_row_same(row, n + (unsigned long)y * width, width))
                        has_n = 1;
                else
                        return 0;
        }
        return has_p && has_n;
}

// Whether a rectangle (x1 y1 x2 y2, clipped to the screen) of a frame is the ground colour throughout.
static int compositor_record_ground(const unsigned int *pixels, unsigned int width, unsigned int height, const int *rect)
{
        int x1 = rect[0] < 0 ? 0 : rect[0], y1 = rect[1] < 0 ? 0 : rect[1];
        int x2 = rect[2] > (int)width ? (int)width : rect[2], y2 = rect[3] > (int)height ? (int)height : rect[3];

        if (x2 <= x1 || y2 <= y1)
                return 0;
        for (int y = y1; y < y2; y++)
                for (int x = x1; x < x2; x++)
                        if ((pixels[(unsigned long)y * width + (unsigned long)x] & 0xffffffu) != (compositor_ground & 0xffffffu))
                                return 0;
        return 1;
}

static int compositor_record_blank(const struct compositor_record_shot *s, const struct compositor_record_shot *p,
                                   const struct compositor_record_shot *n, unsigned int width, unsigned int height)
{
        for (unsigned int at = 0; at < s->rect_count; at++)
                if (compositor_record_ground(s->pixels, width, height, s->rects[at]) && !compositor_record_ground(p->pixels, width, height, s->rects[at]) &&
                    !compositor_record_ground(n->pixels, width, height, s->rects[at]))
                        return 1;
        return 0;
}

static unsigned int compositor_record_cat(char *to, unsigned int at, unsigned int room, const char *text)
{
        while (*text && at + 1 < room)
                to[at++] = *text++;
        to[at] = 0;
        return at;
}

static unsigned int compositor_record_number(char *to, unsigned int at, unsigned int room, unsigned long long value)
{
        char digits[24];
        unsigned int count = 0;

        do
        {
                digits[count++] = (char)('0' + value % 10);
                value /= 10;
        } while (value && count < sizeof(digits));
        while (count && at + 1 < room)
                to[at++] = digits[--count];
        to[at] = 0;
        return at;
}

// frame-SEQ-CLASS.ppm: the frame as a binary PPM (P6), its pixels' red, green and blue; written a row at a time, in chunks.
static void compositor_record_write_ppm(const unsigned int *pixels, unsigned int width, unsigned int height, unsigned long pitch, unsigned int seq,
                                        const char *cls)
{
        char path[160], head[64];
        unsigned char chunk[768];
        long handle;
        unsigned int at;

        at = compositor_record_cat(path, 0, sizeof(path), COMPOSITOR_RECORD_DIR "/frame-");
        at = compositor_record_number(path, at, sizeof(path), seq);
        at = compositor_record_cat(path, at, sizeof(path), "-");
        at = compositor_record_cat(path, at, sizeof(path), cls);
        at = compositor_record_cat(path, at, sizeof(path), ".ppm");
        // A frame is the screen's own pixels, what was on it when a password or a document was: the owner alone reads it (0600). The directory is 0755 so
        // that the names of the records can be listed; the report and the state are 0644 and say only counts.
        handle = (long)system_open_at_mode(AT_FDCWD, (string_address)path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (handle < 0)
                return;
        at = compositor_record_cat(head, 0, sizeof(head), "P6\n");
        at = compositor_record_number(head, at, sizeof(head), width);
        at = compositor_record_cat(head, at, sizeof(head), " ");
        at = compositor_record_number(head, at, sizeof(head), height);
        at = compositor_record_cat(head, at, sizeof(head), "\n255\n");
        system_write_once(handle, head, at);
        for (unsigned long y = 0; y < height; y++)
                for (unsigned long x = 0; x < width;)
                {
                        unsigned int count = 0;

                        while (count < 256 && x < width)
                        {
                                unsigned int pixel = pixels[y * pitch + x++];

                                chunk[3 * count] = (unsigned char)(pixel >> 16);
                                chunk[3 * count + 1] = (unsigned char)(pixel >> 8);
                                chunk[3 * count + 2] = (unsigned char)pixel;
                                count++;
                        }
                        system_write_once(handle, chunk, 3 * count);
                }
        system_close(handle);
        compositor_record_written++;
}

// The four classes of frame S, against P and N. Each class found is counted, and written when there are files left to write.
static void compositor_record_classify(struct compositor_record_shot *s, const struct compositor_record_shot *p, const struct compositor_record_shot *n)
{
        unsigned int width = compositor_record_width, height = compositor_record_height, bits = 0;
        const char *inject = COMPOSITOR_RECORD_DIR "/inject";
        long handle;

        // The check's tear: the rows of P in a band a quarter down, the rows of N elsewhere. The file is taken with it.
        // Taken on a triple whose neighbours differ: a splice of two equal frames is the one frame, and no tear.
        handle = (long)system_open_at(AT_FDCWD, (string_address)inject, FILE_READ | O_CLOEXEC | O_NOFOLLOW);
        if (handle >= 0 && compositor_record_same(p->pixels, n->pixels, width, height))
                system_close(handle), handle = -1;
        if (handle >= 0)
        {
                system_close(handle);
                compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)inject, 0, 0, 0, 0);
                for (unsigned int y = 0; y < height; y++)
                        memory_copy(s->pixels + (unsigned long)y * width, (y >= height / 4 && y < height / 2 ? p : n)->pixels + (unsigned long)y * width,
                                    (unsigned long)width * 4);
        }
        if (compositor_record_torn(s->pixels, p->pixels, n->pixels, width, height))
                bits |= 1u << COMPOSITOR_RECORD_TEAR;
        if (!compositor_record_same(s->pixels, p->pixels, width, height) && !compositor_record_same(s->pixels, n->pixels, width, height) &&
            compositor_record_same(p->pixels, n->pixels, width, height))
                bits |= 1u << COMPOSITOR_RECORD_FLASH;
        if (p->sizes == n->sizes && s->sizes != p->sizes)
                bits |= 1u << COMPOSITOR_RECORD_SIZE_JUMP;
        if (compositor_record_blank(s, p, n, width, height))
                bits |= 1u << COMPOSITOR_RECORD_BLANK;
        if (!bits)
                return;
        for (unsigned int c = 0; c < COMPOSITOR_RECORD_CLASSES; c++)
                if (bits & (1u << c))
                {
                        unsigned int kept = compositor_record_class_written[c] < COMPOSITOR_RECORD_KEEP;

                        compositor_record_classed[c]++;
                        if (kept)
                                compositor_record_write_ppm(s->pixels, width, height, width, s->seq, compositor_record_class_name[c]),
                                    compositor_record_class_written[c]++;
                        // A tear is kept with the frames either side of it, so the splice can be read against them.
                        if (kept && c == COMPOSITOR_RECORD_TEAR)
                        {
                                compositor_record_write_ppm(p->pixels, width, height, width, p->seq, "tear-before");
                                compositor_record_write_ppm(n->pixels, width, height, width, n->seq, "tear-after");
                        }
                        if (compositor_record_event_count < COMPOSITOR_RECORD_EVENTS)
                        {
                                unsigned int *e = compositor_record_events[compositor_record_event_count++];

                                e[0] = s->seq, e[1] = bits, e[2] = kept;
                        }
                }
}

// A frame of the recording: the scanout, copied in full, its window pictures, and a quarter of it into the ring. Classified once the frame after
// it is copied too.
static void compositor_record_capture(unsigned long long when)
{
        unsigned int width = compositor.width, height = compositor.height, stride = compositor.scanout_pitch / 4;
        struct compositor_record_shot *cur;

        // The screen changed size while the block was in use (a screen came or went, or the arrangement moved the space): the frames copied below are
        // the new size, and the block was made for the old one, so it is made again and the recording's frames start over. Copied into the old block, a
        // frame of a larger screen ran past its end.
        if (compositor_record_block && (width != compositor_record_width || height != compositor_record_height))
        {
                compositor_record_free();
                compositor_record_taken = compositor_record_ring_at = compositor_record_ring_count = 0;
        }
        if (!compositor.scanout || !compositor_record_alloc())
        {
                compositor_record_refused_size = 1;
                return;
        }
        cur = &compositor_record_shots[compositor_record_taken % 3];
        memory_copy_rect_u32(cur->pixels, width, compositor.scanout, stride, width, height);
        cur->when = when;
        // The frame's number is the present's (since the compositor started), so two recordings never write the same file.
        cur->seq = (unsigned int)compositor_records_all;
        cur->sizes = compositor_record_pending_sizes;
        cur->rect_count = compositor_record_pending_count;
        memory_copy(cur->rects, compositor_record_pending, compositor_record_pending_count * sizeof(*cur->rects));
        compositor_record_taken++;
        if (compositor_record_ring_cap && compositor_record_ring_pixels)
        {
                unsigned int rw = compositor_record_ring_w, rh = compositor_record_ring_h;
                unsigned int *slot = compositor_record_ring_pixels + (unsigned long)compositor_record_ring_at * rw * rh;

                for (unsigned int y = 0; y < rh; y++)
                        for (unsigned int x = 0; x < rw; x++)
                                slot[(unsigned long)y * rw + x] = compositor.scanout[(unsigned long)(4 * y < height ? 4 * y : height - 1) * stride +
                                                                                    (4 * x < width ? 4 * x : width - 1)];
                compositor_record_ring_at = (compositor_record_ring_at + 1) % compositor_record_ring_cap;
                if (compositor_record_ring_count < compositor_record_ring_cap)
                        compositor_record_ring_count++;
        }
        if (compositor_record_taken >= 3)
                compositor_record_classify(&compositor_record_shots[(compositor_record_taken - 2) % 3],
                                           &compositor_record_shots[(compositor_record_taken - 3) % 3],
                                           &compositor_record_shots[(compositor_record_taken - 1) % 3]);
}

// A recording starts: its counts are its own.
static void compositor_record_reset(void)
{
        compositor_record_taken = compositor_record_written = compositor_record_refused_size = 0;
        memory_zero(compositor_record_class_written, sizeof compositor_record_class_written);
        compositor_record_ring_at = compositor_record_ring_count = compositor_record_event_count = 0;
        memory_zero(compositor_record_classed, sizeof compositor_record_classed);
}

/*
        The report: the totals of the ring, its per-path counts, the flips and their vblanks, the presents late, and the recording's counts
        when one has run. Written to report.txt, and its one-line state to state.
*/
static void compositor_record_report(void)
{
        unsigned int first, count, paths[3] = {0, 0, 0};
        unsigned long long longest = 0, origin;
        unsigned int refused = 0, backoffs = 0, late = 0, held = 0, flips = 0, vblanks = 0, lists = 0, lists_refused = 0;

        compositor_record_text_at = 0;
        count = compositor_records_kept;
        first = count < COMPOSITOR_RECORD_SIZE ? 0 : compositor_records_at;
        origin = count ? compositor_records[first].when : 0;
        for (unsigned int at = 0; at < count; at++)
        {
                const struct compositor_record *r = &compositor_records[(first + at) % COMPOSITOR_RECORD_SIZE];

                paths[r->path < 3 ? r->path : 0]++;
                refused += r->refused, backoffs += r->backoffs, late += r->late, held += r->held, flips += r->flipped, lists += r->lists, lists_refused += r->lists_refused;
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
        compositor_record_put_number(backoffs), compositor_record_put_text(" of them backed off (the card busy), ");
        compositor_record_put_number(vblanks), compositor_record_put_text(" answered by a vblank, longest ");
        compositor_record_put_number(longest / 1000), compositor_record_put_text(" us\n");
        compositor_record_put_text("overlay      "), compositor_record_put_number(compositor_record_remakes), compositor_record_put_text(" remakes of its buffers since the start\n");
        compositor_record_put_text("held         "), compositor_record_put_number(held), compositor_record_put_text(" presents kept their damage for the next\n");
        compositor_record_put_text("late         "), compositor_record_put_number(late), compositor_record_put_text(" presents more than 25 ms after the one before\n");
        compositor_record_put_text("lists        "), compositor_record_put_number(lists), compositor_record_put_text(" drawn from lists, ");
        compositor_record_put_number(lists_refused), compositor_record_put_text(" refused\n");
        // What the compositor measures of its input while it runs (it says the same at its exit): from the device's stamp of a report to the plane
        // moved, to the frame that shows it, and a key to the window that has the keys.
        {
                static const char *const names[3] = {"pointer to the plane ", "pointer to the frame ", "key to the window    "};

                for (unsigned int w = 0; w < 3; w++)
                {
                        unsigned long long n = compositor.latency[w].count;

                        compositor_record_put_text("latency      "), compositor_record_put_text(names[w]), compositor_record_put_number(n);
                        compositor_record_put_text(", on average "), compositor_record_put_number(n ? compositor.latency[w].total / n / 1000 : 0);
                        compositor_record_put_text(" us, worst "), compositor_record_put_number(compositor.latency[w].worst / 1000), compositor_record_put_text(" us\n");
                }
        }
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
        compositor_record_put_text("frames       "), compositor_record_put_number(compositor_record_taken), compositor_record_put_text(" copied in the recording, ");
        compositor_record_put_number(compositor_record_ring_count), compositor_record_put_text(" in the quarter ring\n");
        if (compositor_record_refused_size)
                compositor_record_put_text("classes      not made: the screen is larger than the record's budget\n");
        else
        {
                compositor_record_put_text("classes      tear "), compositor_record_put_number(compositor_record_classed[COMPOSITOR_RECORD_TEAR]);
                compositor_record_put_text(", flash "), compositor_record_put_number(compositor_record_classed[COMPOSITOR_RECORD_FLASH]);
                compositor_record_put_text(", size jump "), compositor_record_put_number(compositor_record_classed[COMPOSITOR_RECORD_SIZE_JUMP]);
                compositor_record_put_text(", blank "), compositor_record_put_number(compositor_record_classed[COMPOSITOR_RECORD_BLANK]);
                compositor_record_put_text("; "), compositor_record_put_number(compositor_record_written), compositor_record_put_text(" frames written to record/frame-*.ppm\n");
        }
        // The events, oldest first: the frames of the classes, then the late presents, the refused flips, the held presents (at most forty lines).
        compositor_record_put_text("events\n");
        for (unsigned int at = 0; at < compositor_record_event_count; at++)
        {
                const unsigned int *e = compositor_record_events[at];

                compositor_record_put_text("  frame "), compositor_record_put_number(e[0]), compositor_record_put_text(": ");
                for (unsigned int c = 0; c < COMPOSITOR_RECORD_CLASSES; c++)
                        if (e[1] & (1u << c))
                                compositor_record_put_text(compositor_record_class_name[c]), compositor_record_put_text(" ");
                compositor_record_put_text(e[2] ? "(written)\n" : "(not written: the files are used up)\n");
        }
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
        compositor_record_put_number(compositor_records_all), compositor_record_put_text(" presents in all, tear ");
        compositor_record_put_number(compositor_record_classed[COMPOSITOR_RECORD_TEAR]), compositor_record_put_text(", flash ");
        compositor_record_put_number(compositor_record_classed[COMPOSITOR_RECORD_FLASH]), compositor_record_put_text(", size ");
        compositor_record_put_number(compositor_record_classed[COMPOSITOR_RECORD_SIZE_JUMP]), compositor_record_put_text(", blank ");
        compositor_record_put_number(compositor_record_classed[COMPOSITOR_RECORD_BLANK]), compositor_record_put_text("\n");
        compositor_record_write_file("state", compositor_record_text, compositor_record_text_at);
}

/*
        Whether the directory's files are to be read at this look: its mtime (nanoseconds of the real clock) is not the one the last read was made
        at (seen; 0 for none), or the last read was so soon after that mtime (read_at, within COMPOSITOR_RECORD_SETTLE_NS) that a file made after
        the read in the same tick of the filesystem's clock left the mtime as it was. moonwater canvas record makes its start and report files there
        and the look takes each one away, so every request moves the mtime. A directory that is not there (mtime 0) is read for, after it is made.
*/
#define COMPOSITOR_RECORD_SETTLE_NS 50000000ll
static long long compositor_record_seen, compositor_record_read_at;
// moonwater canvas shot asked for the screen as it is: the next present writes it (frame-SEQ-shot.ppm), whatever the card or the processor composed.
static int compositor_record_shot_wanted;

/*
        moonwater canvas film SECONDS [X Y W H]: every present for SECONDS (1 to 60), the screen's rectangle X Y W H of it (all of it by default),
        appended as it was composed to film.raw: a record a frame, 32 bytes of head (the bytes "MWFILM01", the present's number, the time it
        ended in nanoseconds of the monotonic clock, the width and the height) and then its pixels, XRGB rows of width. film.txt says it is done
        and how many frames. The owner's alone (the screen). One write a frame, from a buffer of the rectangle made once.
*/
#define COMPOSITOR_FILM_MAX_SECONDS 60
static struct
{
        unsigned long long until;
        int x, y, w, h;
        long fd;
        unsigned int frames;
        unsigned int *buffer;
        unsigned long bytes;
} compositor_film = {0, 0, 0, 0, 0, -1, 0, 0, 0};

static void compositor_film_end(void)
{
        char text[64];
        unsigned int at;
        void *p = compositor_film.buffer;

        if (compositor_film.fd >= 0)
                system_close((int)compositor_film.fd);
        compositor_film.fd = -1;
        compositor_film.until = 0;
        mem_buf_free(&p, &compositor_film.bytes, 0);
        compositor_film.buffer = 0;
        at = compositor_record_cat(text, 0, sizeof(text), "done ");
        at = compositor_record_number(text, at, sizeof(text), compositor_film.frames);
        at = compositor_record_cat(text, at, sizeof(text), " frames\n");
        compositor_record_write_file("film.txt", text, at);
}

static int compositor_under(int x, int y, int *tab); // manage.c

/*
        Read a rectangle (x, y, w, h) of what the screen currently shows into out (out_pitch pixels/row):
        resolves direct scanout (lent or table buffer, swapping R/B if lent_swap) or compositor.scanout,
        then overlays compositor.plane_row where the overlay plane overlaps the rectangle. Sets bit 29 of
        *flags_out when a direct or overlay plane buffer was read.
*/
static unsigned int *compositor_buffer(int row, unsigned int which); // scanout.c
static int compositor_readback_row(int row, unsigned int *out, unsigned long out_pitch, int ox, int oy, int x1, int y1, int x2, int y2)
{
        const unsigned int *from;
        unsigned long pitch;
        int rx0, ry0, rx1, ry1;

        if (row < 0 || row >= (int)compositor_rows_in_use)
                return 0;
        rx0 = compositor.x[row], ry0 = compositor.y[row];
        rx1 = rx0 + (int)compositor.w[row], ry1 = ry0 + (int)compositor.h[row];
        if (x1 < rx0)
                x1 = rx0;
        if (y1 < ry0)
                y1 = ry0;
        if (x2 > rx1)
                x2 = rx1;
        if (y2 > ry1)
                y2 = ry1;
        if (x1 >= x2 || y1 >= y2)
                return 0;
        compositor_wl_straight_settle(row, y1 - ry0, y2 - ry0);
        from = compositor_lent_pixels(row, &pitch);
        if (!from && compositor.table)
                from = compositor_buffer(row, compositor.front[row]), pitch = (unsigned long)compositor_stride();
        if (!from)
                return 0;
        for (int at = y1; at < y2; at++)
        {
                unsigned int *line = out + (unsigned long)(at - oy) * out_pitch + (unsigned long)(x1 - ox);

                memory_copy(line, from + (unsigned long)(at - ry0) * pitch + (unsigned long)(x1 - rx0),
                            (unsigned long)(x2 - x1) * 4);
                if (compositor_lent_swap(row))
                        memory_swizzle_rb_u32(line, line, (positive)(x2 - x1), 0);
        }
        return 1;
}

static void compositor_readback_rect(unsigned int *out, unsigned long out_pitch, int x, int y, int w, int h, unsigned int *flags_out)
{
        int direct = compositor.direct_row, plane = compositor.plane_row;

        if (direct >= 0 && direct < (int)compositor_rows_in_use && plane != direct && compositor.on_screen &&
            compositor.on_screen != compositor.fb && !compositor.scaled_direct &&
            compositor.x[direct] == 0 && compositor.y[direct] == 0 &&
            x >= 0 && y >= 0 && w > 0 && h > 0 &&
            x <= (int)compositor.w[direct] && w <= (int)compositor.w[direct] - x &&
            y <= (int)compositor.h[direct] && h <= (int)compositor.h[direct] - y &&
            compositor_readback_row(direct, out, out_pitch, x, y, x, y, x + w, y + h))
        {
                if (flags_out)
                        *flags_out |= 1u << 29;
        }
        else
                memory_copy_rect_u32(out, out_pitch, compositor.scanout + (unsigned long)y * (compositor.scanout_pitch / 4) + (unsigned long)x,
                                     compositor.scanout_pitch / 4, (unsigned long)w, (unsigned long)h);

        if (plane >= 0 && compositor.plane_fb)
        {
                int x1 = compositor.plane_rect[0] > x ? compositor.plane_rect[0] : x;
                int y1 = compositor.plane_rect[1] > y ? compositor.plane_rect[1] : y;
                int x2 = compositor.plane_rect[2] < x + w ? compositor.plane_rect[2] : x + w;
                int y2 = compositor.plane_rect[3] < y + h ? compositor.plane_rect[3] : y + h;

                if (x2 > x1 && y2 > y1 && compositor_readback_row(plane, out, out_pitch, x, y, x1, y1, x2, y2) && flags_out)
                        *flags_out |= 1u << 29;
        }
}

// A frame of the film: the present just made, its rectangle, one write.
static void compositor_film_frame(unsigned long long when)
{
        unsigned int width = compositor.width, height = compositor.height;
        int x = compositor_film.x, y = compositor_film.y, w = compositor_film.w, h = compositor_film.h;
        unsigned long need;
        void *p;

        if (compositor_film.fd < 0 || !compositor.scanout)
                return;
        if (w <= 0 || h <= 0 || x < 0 || y < 0 || (unsigned int)(x + w) > width || (unsigned int)(y + h) > height)
                x = y = 0, w = (int)width, h = (int)height;
        need = 32 + (unsigned long)w * (unsigned long)h * 4;
        p = compositor_film.buffer;
        if (!mem_buf_grow(&p, &compositor_film.bytes, need, 0))
        {
                compositor_film.buffer = 0;
                compositor_film_end();
                return;
        }
        compositor_film.buffer = (unsigned int *)p;
        memory_copy(compositor_film.buffer, "MWFILM01", 8);
        compositor_film.buffer[2] = (unsigned int)compositor_records_all;
        // The window under the rectangle's middle as this present composed it: bit 31 read in place (lent), bit 30 the table's buffer it
        // shows, bit 29 shown straight (below), the rest its picture's number (table frame), so a frame that shows an older picture says
        // where it was read from.
        {
                int tab, row = compositor_under(x + w / 2, y + h / 2, &tab);

                compositor_film.buffer[3] = row < 0 ? 0 : ((unsigned int)(compositor_lent(row) != 0) << 31) |
                                                            ((compositor.front[row] & 1u) << 30) |
                                                            (__atomic_load_n(&compositor.table->frame[row], __ATOMIC_ACQUIRE) & 0x1fffffffu);
        }
        *(unsigned long long *)(compositor_film.buffer + 4) = when;
        compositor_film.buffer[6] = (unsigned int)w, compositor_film.buffer[7] = (unsigned int)h;
        compositor_readback_rect(compositor_film.buffer + 8, (unsigned long)w, x, y, w, h, &compositor_film.buffer[3]);
        system_write_all((int)compositor_film.fd, (const char *)compositor_film.buffer, need);
        compositor_film.frames++;
}

/*
        moonwater canvas keys SECONDS: for SECONDS (1 to 60), every key pressed is followed to the screen, each stage stamped on the monotonic
        clock as it passes: the kernel's report (the event's own time), the compositor's read of it, the key given to the window's client
        (wayland_key), the client's socket written, the client's next picture of that window, the present that composed it, and the vblank its
        flip landed on (the photon, give or take the scanout to the row). keys.txt gets a line a key: each stage after the one before in
        microseconds, and the whole; nothing is written until the end, and each stage while it runs is a compare or two (the ring's open keys).
*/
#define COMPOSITOR_KEYTRACE_RING 1024
#define COMPOSITOR_KEYTRACE_MAX_SECONDS 60
static struct compositor_keytrace_entry
{
        unsigned long long reported, read, sent, flushed, commit, present, shown;
        int row;
        unsigned int code;
} compositor_keytrace_ring[COMPOSITOR_KEYTRACE_RING];
static struct
{
        unsigned long long until;
        unsigned int count, open; // keys taken; the first that is not yet on the screen
} compositor_keytrace;
static char compositor_keytrace_text[COMPOSITOR_KEYTRACE_RING * 112 + 256];

static void compositor_keytrace_read(unsigned int code, unsigned long long reported, unsigned long long now)
{
        struct compositor_keytrace_entry *k;

        if (!compositor_keytrace.until || compositor_keytrace.count >= COMPOSITOR_KEYTRACE_RING)
                return;
        k = &compositor_keytrace_ring[compositor_keytrace.count++];
        memory_zero(k, sizeof(*k));
        k->reported = reported ? reported : now, k->read = now, k->code = code, k->row = -1;
}

// The newest key read and not yet given to a client is given to this window's.
static void compositor_keytrace_sent(int row)
{
        if (!compositor_keytrace.until || !compositor_keytrace.count)
                return;
        for (unsigned int at = compositor_keytrace.count; at-- > compositor_keytrace.open;)
                if (!compositor_keytrace_ring[at].sent)
                {
                        compositor_keytrace_ring[at].sent = clock_monotonic_nanoseconds(), compositor_keytrace_ring[at].row = row;
                        return;
                }
}

// The newest key read and not yet given to a client was written to this window's client by the input thread itself at sent (its socket
// written then too: the thread's send is the flush); 0 for a send the loop made.
static void compositor_keytrace_direct(int row, unsigned long long sent)
{
        if (!sent)
        {
                compositor_keytrace_sent(row);
                return;
        }
        if (!compositor_keytrace.until || !compositor_keytrace.count)
                return;
        for (unsigned int at = compositor_keytrace.count; at-- > compositor_keytrace.open;)
                if (!compositor_keytrace_ring[at].sent)
                {
                        compositor_keytrace_ring[at].sent = compositor_keytrace_ring[at].flushed = sent, compositor_keytrace_ring[at].row = row;
                        return;
                }
}

// Each stage below is taken by every open key at that stage: the first picture, present and vblank after it.
static void compositor_keytrace_flushed(void)
{
        unsigned long long now = 0;

        if (!compositor_keytrace.until)
                return;
        for (unsigned int at = compositor_keytrace.open; at < compositor_keytrace.count; at++)
                if (compositor_keytrace_ring[at].sent && !compositor_keytrace_ring[at].flushed)
                        compositor_keytrace_ring[at].flushed = now ? now : (now = clock_monotonic_nanoseconds());
}

static void compositor_keytrace_commit(int row)
{
        unsigned long long now = 0;

        if (!compositor_keytrace.until)
                return;
        for (unsigned int at = compositor_keytrace.open; at < compositor_keytrace.count; at++)
                if (compositor_keytrace_ring[at].flushed && !compositor_keytrace_ring[at].commit && compositor_keytrace_ring[at].row == row)
                        compositor_keytrace_ring[at].commit = now ? now : (now = clock_monotonic_nanoseconds());
}

static void compositor_keytrace_present(unsigned long long began, unsigned long long ended)
{
        if (!compositor_keytrace.until)
                return;
        for (unsigned int at = compositor_keytrace.open; at < compositor_keytrace.count; at++)
                if (compositor_keytrace_ring[at].commit && !compositor_keytrace_ring[at].present && compositor_keytrace_ring[at].commit <= began)
                        compositor_keytrace_ring[at].present = ended;
}

static void compositor_keytrace_shown(unsigned long long when)
{
        if (!compositor_keytrace.until)
                return;
        for (unsigned int at = compositor_keytrace.open; at < compositor_keytrace.count; at++)
                if (compositor_keytrace_ring[at].present && !compositor_keytrace_ring[at].shown)
                        compositor_keytrace_ring[at].shown = when > compositor_keytrace_ring[at].present ? when : compositor_keytrace_ring[at].present;
        while (compositor_keytrace.open < compositor_keytrace.count && compositor_keytrace_ring[compositor_keytrace.open].shown)
                compositor_keytrace.open++;
}

static unsigned int compositor_keytrace_us(char *to, unsigned int at, unsigned int room, unsigned long long from, unsigned long long till)
{
        unsigned long long ns = from && till >= from ? till - from : 0;

        if (!from || !till)
                return compositor_record_cat(to, at, room, " -");
        at = compositor_record_cat(to, at, room, " ");
        at = compositor_record_number(to, at, room, ns / 1000);
        at = compositor_record_cat(to, at, room, ".");
        at = compositor_record_number(to, at, room, ns / 100 % 10);
        return at;
}

static void compositor_keytrace_end(void)
{
        unsigned int at = 0, room = sizeof(compositor_keytrace_text);
        char *to = compositor_keytrace_text;

        compositor_keytrace.until = 0;
        at = compositor_record_cat(to, at, room, "# us: read sent flushed commit present shown whole (each after the stage before; whole: report to vblank)\n");
        for (unsigned int k = 0; k < compositor_keytrace.count; k++)
        {
                const struct compositor_keytrace_entry *t = &compositor_keytrace_ring[k];

                at = compositor_record_number(to, at, room, t->code);
                at = compositor_keytrace_us(to, at, room, t->reported, t->read);
                at = compositor_keytrace_us(to, at, room, t->read, t->sent);
                at = compositor_keytrace_us(to, at, room, t->sent, t->flushed);
                at = compositor_keytrace_us(to, at, room, t->flushed, t->commit);
                at = compositor_keytrace_us(to, at, room, t->commit, t->present);
                at = compositor_keytrace_us(to, at, room, t->present, t->shown);
                at = compositor_keytrace_us(to, at, room, t->reported, t->shown);
                at = compositor_record_cat(to, at, room, "\n");
        }
        compositor_record_write_file("keys.txt", to, at);
        compositor_keytrace.count = compositor_keytrace.open = 0;
}

/*
        moonwater canvas frames SECONDS: for SECONDS (1 to 60), every window's new picture taken (compositor_take: a frame of its program's), every
        present's start and end, the card's job's end, every frame callback, every flip's vblank and every wake, stamped on the monotonic clock into
        a ring (a compare a stage while nothing runs), and at the end /run/moonwater/record/frames.txt, a line an event:
                "T ROW NS" a window's picture taken,
                "B - NS" a present begun, "P - NS" a present made (for a present the card draws, when its jobs were seen done),
                "F - NS" the card's last job of a present done (its fence's own stamp, SYNC_IOC_FILE_INFO: compositor_card_split),
                "C ROW NS" a Wayland window's frame callbacks sent (wayland_callbacks_due),
                "V - NS" the vblank a flip landed on,
                "W - NS" a wake from sleep, or the screens taken again (a mode set) begun.
        The ring starts at COMPOSITOR_FRAMETRACE_RING events and grows as it fills. test/perf/frames.py reads it: the frame times of a game (its
        window's pictures), its 1% and 0.1% lows, the pictures replaced before a present showed them, when each reached the screen; and from the
        marks a present's cost in the loop (B to P, or to F), its card's time (F after B), a client's callback to its next picture (C to T), and
        the frames around each wake.
*/
#define COMPOSITOR_FRAMETRACE_RING 131072
#define COMPOSITOR_FRAMETRACE_MAX_SECONDS 60
static struct
{
        unsigned long long until;
        unsigned int count;
        struct { unsigned long long when; int row; char kind; } *ring;
        unsigned long bytes;
} compositor_frametrace;

static void compositor_frametrace_note(char kind, int row, unsigned long long when)
{
        if (!compositor_frametrace.until || !compositor_frametrace.ring)
                return;
        if ((unsigned long)(compositor_frametrace.count + 1) * sizeof(compositor_frametrace.ring[0]) > compositor_frametrace.bytes)
        {
                void *p = compositor_frametrace.ring;

                if (!mem_buf_grow(&p, &compositor_frametrace.bytes, (unsigned long)(compositor_frametrace.count + 1) * sizeof(compositor_frametrace.ring[0]),
                                  MEM_BUF_KEEP | MEM_BUF_GROW_2X))
                        return;
                compositor_frametrace.ring = p;
        }
        compositor_frametrace.ring[compositor_frametrace.count].when = when;
        compositor_frametrace.ring[compositor_frametrace.count].row = row;
        compositor_frametrace.ring[compositor_frametrace.count++].kind = kind;
}

static void compositor_frametrace_end(void)
{
        static char line[64];
        long handle;

        compositor_frametrace.until = 0;
        handle = (long)system_open_at_mode(AT_FDCWD, (string_address)COMPOSITOR_RECORD_DIR "/frames.txt",
                                           O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0644);
        if (handle >= 0)
        {
                for (unsigned int i = 0; i < compositor_frametrace.count; i++)
                {
                        unsigned int at = 0;

                        line[at++] = compositor_frametrace.ring[i].kind, line[at++] = ' ';
                        if (compositor_frametrace.ring[i].row < 0)
                                line[at++] = '-';
                        else
                                at = compositor_record_number(line, at, sizeof(line), (unsigned long long)compositor_frametrace.ring[i].row);
                        line[at++] = ' ';
                        at = compositor_record_number(line, at, sizeof(line), compositor_frametrace.ring[i].when);
                        line[at++] = '\n';
                        system_write_all((int)handle, line, at);
                }
                system_close((int)handle);
        }
        compositor_frametrace.count = 0;
}

static int compositor_record_look_wanted(long long mtime, long long seen, long long read_at)
{
        return !mtime || mtime != seen || read_at - mtime < COMPOSITOR_RECORD_SETTLE_NS;
}

// The directory's mtime in nanoseconds of the real clock, 0 when it is not there: one statx (STATX_MTIME), at the 112th byte of the answer.
static long long compositor_record_mtime(void)
{
        unsigned char answer[256];

        if (compositor_call(syscall(statx), (positive)AT_FDCWD, (positive)COMPOSITOR_RECORD_DIR, 0, 0x40, (positive)answer, 0) < 0)
                return 0;
        return *(long long *)(answer + 112) * 1000000000ll + *(unsigned int *)(answer + 120);
}

// Consume a command file under COMPOSITOR_RECORD_DIR: returns -1 if absent, else count of non-negative decimal numbers parsed (0..max_values).
static int record_take_cmd(const char *path, long *values, int max_values)
{
        long handle = (long)system_open_at(AT_FDCWD, (string_address)path, FILE_READ | O_CLOEXEC | O_NOFOLLOW);
        char line[96];
        long got = 0, value = -1;
        int count = 0;

        if (handle < 0)
                return -1;
        if (max_values > 0)
                got = (long)system_read_once(handle, line, sizeof(line) - 1);
        system_close(handle);
        compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)path, 0, 0, 0, 0);
        for (long at = 0; at < got && count < max_values; at++)
                if (line[at] >= '0' && line[at] <= '9')
                        value = (value < 0 ? 0 : value) * 10 + (line[at] - '0');
                else if (value >= 0)
                        values[count++] = value, value = -1;
        if (value >= 0 && count < max_values)
                values[count++] = value;
        return count;
}

/*
        A look at the files of moonwater canvas record. Every COMPOSITOR_RECORD_POLL presents: the start file (a recording, its seconds taken from
        the file; the file goes), the report request (the report is written; the request goes). A look is one statx of the directory, and the
        files are opened only when the directory changed since they were (compositor_record_look_wanted); it was a mkdirat and two openats.
*/
static void compositor_record_look(unsigned long long now)
{
        long values[5];
        int count;
        long long mtime = compositor_record_mtime();
        timespec real = {0, 0};

        if (!compositor_record_look_wanted(mtime, compositor_record_seen, compositor_record_read_at))
                return;
        clock_gettime(CLOCK_REALTIME, address_of real);
        compositor_record_seen = mtime, compositor_record_read_at = (long long)real.tv_sec * 1000000000ll + real.tv_nsec;
        if (!mtime)
                compositor_call(syscall(mkdirat), (positive)AT_FDCWD, (positive)COMPOSITOR_RECORD_DIR, 0755, 0, 0, 0);
        if ((count = record_take_cmd(COMPOSITOR_RECORD_DIR "/film", values, 5)) >= 1 && values[0] >= 1 && values[0] <= COMPOSITOR_FILM_MAX_SECONDS)
        {
                if (compositor_film.fd >= 0)
                        compositor_film_end();
                compositor_film.fd = (long)system_open_at_mode(AT_FDCWD, (string_address)COMPOSITOR_RECORD_DIR "/film.raw",
                                                              O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
                compositor_film.x = count >= 2 ? (int)values[1] : 0, compositor_film.y = count >= 3 ? (int)values[2] : 0;
                compositor_film.w = count == 5 ? (int)values[3] : 0, compositor_film.h = count == 5 ? (int)values[4] : 0;
                compositor_film.frames = 0;
                compositor_film.until = compositor_film.fd >= 0 ? now + (unsigned long long)values[0] * 1000000000ull : 0;
                compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)COMPOSITOR_RECORD_DIR "/film.txt", 0, 0, 0, 0);
        }
        if (record_take_cmd(COMPOSITOR_RECORD_DIR "/frames", values, 1) == 1 && values[0] >= 1 && values[0] <= COMPOSITOR_FRAMETRACE_MAX_SECONDS)
        {
                void *p = compositor_frametrace.ring;

                compositor_frametrace.ring = mem_buf_grow(&p, &compositor_frametrace.bytes,
                                                          (unsigned long)COMPOSITOR_FRAMETRACE_RING * sizeof(compositor_frametrace.ring[0]), 0);
                compositor_frametrace.count = 0;
                compositor_frametrace.until = compositor_frametrace.ring ? now + (unsigned long long)values[0] * 1000000000ull : 0;
                compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)COMPOSITOR_RECORD_DIR "/frames.txt", 0, 0, 0, 0);
        }
        if (record_take_cmd(COMPOSITOR_RECORD_DIR "/keys", values, 1) == 1 && values[0] >= 1 && values[0] <= COMPOSITOR_KEYTRACE_MAX_SECONDS)
        {
                compositor_keytrace.count = compositor_keytrace.open = 0;
                compositor_keytrace.until = now + (unsigned long long)values[0] * 1000000000ull;
                compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)COMPOSITOR_RECORD_DIR "/keys.txt", 0, 0, 0, 0);
        }
        if (record_take_cmd(COMPOSITOR_RECORD_DIR "/shot", 0, 0) >= 0)
                compositor_record_shot_wanted = 1;
        if (record_take_cmd(COMPOSITOR_RECORD_DIR "/start", values, 1) == 1 && values[0] >= 1 && values[0] <= COMPOSITOR_RECORD_MAX_SECONDS)
        {
                compositor_record_seconds = (unsigned int)values[0];
                compositor_record_until = now + (unsigned long long)values[0] * 1000000000ull;
                compositor_record_frames = compositor_record_late = compositor_record_refused = compositor_record_held = 0;
                compositor_record_flips = 0;
                compositor_record_reset();
        }
        if (record_take_cmd(COMPOSITOR_RECORD_DIR "/report", 0, 0) >= 0)
                compositor_record_report();
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
        if (compositor_record_until && compositor_record_mark.damaged)
                compositor_record_capture(ended);
        if (compositor_frametrace.until)
        {
                compositor_frametrace_note('B', -1, began);
                compositor_frametrace_note('P', -1, ended);
                if (ended >= compositor_frametrace.until)
                        compositor_frametrace_end();
        }
        if (compositor_keytrace.until)
        {
                compositor_keytrace_present(began, ended);
                if (ended >= compositor_keytrace.until)
                        compositor_keytrace_end();
        }
        if (compositor_film.until)
        {
                compositor_film_frame(ended);
                if (ended >= compositor_film.until)
                        compositor_film_end();
        }
        if (compositor_record_shot_wanted && compositor.scanout)
        {
                compositor_record_shot_wanted = 0;
                compositor_record_write_ppm(compositor.scanout, compositor.width, compositor.height, compositor.scanout_pitch / 4,
                                            (unsigned int)compositor_records_all, "shot");
        }
        if (compositor_record_until && ended >= compositor_record_until)
        {
                compositor_record_until = 0;
                compositor_record_report();
                compositor_record_free();
        }
}
