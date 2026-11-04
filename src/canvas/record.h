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
        int rects[WINDOWS][4];          // the window pictures: x1 y1 x2 y2
};
static struct compositor_record_shot compositor_record_shots[3];
static unsigned int *compositor_record_block;     // the mapping of the three frames and the ring
static unsigned long compositor_record_block_bytes;
static unsigned int compositor_record_width, compositor_record_height, compositor_record_ring_w, compositor_record_ring_h;
static unsigned int compositor_record_ring_cap, compositor_record_ring_at, compositor_record_ring_count;
static unsigned int *compositor_record_ring_pixels;
static unsigned int compositor_record_taken;      // frames copied in this recording
static unsigned int compositor_record_classed[COMPOSITOR_RECORD_CLASSES], compositor_record_written;
static unsigned int compositor_record_refused_size; // the screen is too large for the budget: nothing is classified
static unsigned int compositor_record_events[COMPOSITOR_RECORD_EVENTS][3]; // seq, bits (1 << class), kept
static unsigned int compositor_record_event_count;
static int compositor_record_pending[WINDOWS][4];   // the window pictures of the present in hand (compositor_record_scene)
static unsigned int compositor_record_pending_count, compositor_record_pending_sizes;

// The window pictures of the present in hand, from compositor_present_frames: their rectangles and their sizes (a hash).
static void compositor_record_scene(const struct compositor_frame *frames, unsigned int windows)
{
        unsigned int sizes = 2166136261u;

        compositor_record_pending_count = windows < WINDOWS ? windows : WINDOWS;
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
        if (compositor_record_block)
                compositor_call(syscall(munmap), (positive)compositor_record_block, compositor_record_block_bytes, 0, 0, 0, 0);
        compositor_record_block = 0;
        compositor_record_block_bytes = 0;
        compositor_record_ring_pixels = 0;
        for (unsigned int at = 0; at < 3; at++)
                compositor_record_shots[at].pixels = 0;
}

// The mapping for the three frames and the ring, made at the first frame of a recording; false when the screen is too large for the budget.
static int compositor_record_alloc(void)
{
        unsigned long bytes, ring_bytes, ring_cap, total;
        long got;

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
        got = (long)compositor_call(syscall(mmap), 0, total, 3, 0x22, (positive)-1, 0);
        if (got < 0 && got > -4096)
                return 0;
        compositor_record_block = (unsigned int *)(unsigned long)got;
        compositor_record_block_bytes = total;
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
        for (unsigned int x = 0; x < width; x++)
                if (a[x] != b[x])
                        return 0;
        return 1;
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
static void compositor_record_write_ppm(const unsigned int *pixels, unsigned int width, unsigned int height, unsigned int seq, const char *cls)
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
        handle = (long)system_open_at_mode(AT_FDCWD, (string_address)path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0644);
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
                                unsigned int pixel = pixels[y * width + x++];

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
        handle = (long)system_open_at(AT_FDCWD, (string_address)inject, FILE_READ | O_CLOEXEC | O_NOFOLLOW);
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
                        unsigned int kept = compositor_record_written < COMPOSITOR_RECORD_KEEP;

                        compositor_record_classed[c]++;
                        if (kept)
                                compositor_record_write_ppm(s->pixels, width, height, s->seq, compositor_record_class_name[c]);
                        // A tear is kept with the frames either side of it, so the splice can be read against them.
                        if (kept && c == COMPOSITOR_RECORD_TEAR)
                        {
                                compositor_record_write_ppm(p->pixels, width, height, p->seq, "tear-before");
                                compositor_record_write_ppm(n->pixels, width, height, n->seq, "tear-after");
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

        if (!compositor.scanout || !compositor_record_alloc())
        {
                compositor_record_refused_size = 1;
                return;
        }
        cur = &compositor_record_shots[compositor_record_taken % 3];
        for (unsigned int y = 0; y < height; y++)
                memory_copy(cur->pixels + (unsigned long)y * width, compositor.scanout + (unsigned long)y * stride, (unsigned long)width * 4);
        cur->when = when;
        // The frame's number is the present's (since the compositor started), so two recordings never write the same file.
        cur->seq = (unsigned int)compositor_records_all;
        cur->sizes = compositor_record_pending_sizes;
        cur->rect_count = compositor_record_pending_count;
        memory_copy(cur->rects, compositor_record_pending, sizeof(cur->rects));
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
        compositor_record_ring_at = compositor_record_ring_count = compositor_record_event_count = 0;
        for (unsigned int c = 0; c < COMPOSITOR_RECORD_CLASSES; c++)
                compositor_record_classed[c] = 0;
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
                        compositor_record_reset();
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
        if (compositor_record_until && compositor_record_mark.damaged)
                compositor_record_capture(ended);
        if (compositor_record_until && ended >= compositor_record_until)
        {
                compositor_record_until = 0;
                compositor_record_report();
                compositor_record_free();
        }
}
