/*
        libmoonwater-input.so: the pointer's reports read in the game's own process (i386 and x86_64)

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater

        A game's mouse look, as Canvas carried it, went from the kernel's input filter to the compositor's
        input thread, from there to Xwayland's socket, through Xwayland into an X event and over the game's
        X socket, and the game read it at the top of its next frame. Here the game reads the report where the
        kernel wrote it: the focused page (src/moonwater/spark.c, spark_focus_page), which the kernel writes
        only for the thread group the compositor named (the owner of the window that holds the pointer
        locked) and only while that thread group claims it. The page is read when the game asks SDL for its
        events or its mouse, so a report is the game's the moment the kernel has it, with no hop and no
        wake, stamped with the kernel's microseconds.

        SDL 2 lets a library stand in for its own functions (SDL_DYNAMIC_API, read at SDL's first call, even
        by an SDL linked into the game): SDL loads the library named and calls its SDL_DYNAPI_entry with its
        table of functions to fill. This library fills it with the calling SDL's own functions, through that
        SDL's own SDL_DYNAPI_entry, and puts itself in front of a few: the event pump and the event reads, the
        mouse's state and relative state, relative mode, the GL swap and SDL_Quit. Everything else is SDL's,
        untouched. The session preloads this library into Steam and its games (by $LIB, as steam-overlay-gl.so)
        and SDL_DYNAMIC_API=libmoonwater-input.so names it by its soname, so the dynamic loader answers SDL's
        dlopen with the copy already loaded, inside pressure-vessel's container or out of it.

        The game turning relative mode on opens /dev/spark, maps its page (read only) and claims it:
        MOONWATER_INPUT=take (the default) takes the reports from Xwayland (the page alone has them, and
        they come out of SDL's own calls as SDL's events and state would), observe leaves them going as they
        went and only measures, off leaves the page alone. Relative mode off lets the claim go. A page the
        compositor stops naming (focus lost, the lock gone, Steam's overlay open) moves its generation and is
        wiped; every button this library had pressed is released then, so the game never holds one.

        MOONWATER_INPUT_LOG=PATH writes one line per event read, for the meter: S (a report read: the read's
        CLOCK_MONOTONIC ns, the kernel's us, dx, dy, kind, code), X (observe: a motion SDL read from X, ns,
        xrel, yrel), W (a GL swap, ns), A (the page armed or not: ns, generation, state), L (reports lost
        because the game read too late: ns, count).

        Keys never come from the page. Nothing runs at load: no constructor, nothing until SDL calls in.

        Built into the byte arrays in src/bowl.c (i386 and x86_64) with:
          clang -target {i386,x86_64}-linux-gnu -std=gnu11 -Os -fPIC -shared -nostdlib -ffreestanding -fno-stack-protector \
                -fvisibility=hidden -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-ident -fuse-ld=lld \
                -Wl,-soname,libmoonwater-input.so,-z,lazy,-z,undefs,--allow-shlib-undefined,-Bsymbolic,--hash-style=both,--build-id=none,-s \
                -o libmoonwater-input.so src/shim/input.c
*/

#define MWI_EXPORT __attribute__((visibility("default")))

/* ---- the page: spark.c's spark_focus_page and spark_input_slot, word for word (CHECK_input_shim holds them equal) ---- */

#define MWI_PAGE_BYTES 4096ul
#define MWI_PAGE_PGOFF 17ul
#define MWI_PAGE_MAGIC 0x53434f46u
#define MWI_PAGE_VERSION 1u
#define MWI_SLOTS 128u
#define MWI_CLAIM_OFF 0u
#define MWI_CLAIM_OBSERVE 1u
#define MWI_CLAIM_TAKE 2u
#define MWI_STATE_ARMED 1u
#define MWI_KIND_REL 0u
#define MWI_KIND_KEY 1u
#define MWI_KIND_WHEEL 2u
#define MWI_IOCTL_CLAIM 0x00007315ul

struct mwi_slot
{
        int dx;
        int dy;
        unsigned long long time_us;
        unsigned short kind;
        unsigned short code;
        unsigned int flags;
} __attribute__((aligned(8)));

struct mwi_page
{
        unsigned int magic, version, mask, state, generation, claim, head, buttons, drops, armed_ms, armed_head, reserved[5];
        struct mwi_slot slots[MWI_SLOTS];
};

_Static_assert(sizeof(struct mwi_slot) == 24, "a slot is 24 bytes on both machines");
_Static_assert(sizeof(struct mwi_page) == 64 + 24 * MWI_SLOTS, "the page's header is 64 bytes on both machines");

/* ---- SDL 2's words: the jump table's places (SDL_dynapi_procs.h, on Linux, append only) and its events ---- */

#define MWI_SDL_API 1u
#define MWI_AT_QUIT 17
#define MWI_AT_PUMP 75
#define MWI_AT_PEEP 76
#define MWI_AT_POLL 81
#define MWI_AT_WAIT 82
#define MWI_AT_WAIT_TIMEOUT 83
#define MWI_AT_PUSH 84
#define MWI_AT_MOUSE_FOCUS 204
#define MWI_AT_MOUSE_STATE 205
#define MWI_AT_RELATIVE_STATE 206
#define MWI_AT_SET_RELATIVE 208
#define MWI_AT_PLATFORM 250
#define MWI_AT_VERSION 450
#define MWI_AT_WINDOW_ID 472
#define MWI_AT_SWAP 522
#define MWI_AT_LAST MWI_AT_SWAP

#define MWI_SDL_MOUSEMOTION 0x400u
#define MWI_SDL_MOUSEBUTTONDOWN 0x401u
#define MWI_SDL_MOUSEBUTTONUP 0x402u
#define MWI_SDL_MOUSEWHEEL 0x403u
#define MWI_SDL_GETEVENT 2

struct mwi_sdl_motion
{
        unsigned int type, timestamp, window, which, state;
        int x, y, xrel, yrel;
};
struct mwi_sdl_button
{
        unsigned int type, timestamp, window, which;
        unsigned char button, state, clicks, padding;
        int x, y;
};
struct mwi_sdl_wheel
{
        unsigned int type, timestamp, window, which;
        int x, y;
        unsigned int direction;
        float precise_x, precise_y;
        int mouse_x, mouse_y;
};
union mwi_sdl_event
{
        unsigned int type;
        struct mwi_sdl_motion motion;
        struct mwi_sdl_button button;
        struct mwi_sdl_wheel wheel;
        unsigned char padding[56];
};
struct mwi_sdl_version
{
        unsigned char major, minor, patch;
};

_Static_assert(sizeof(union mwi_sdl_event) == 56, "SDL 2's event is 56 bytes on both machines");

typedef void (*mwi_fn)(void);
typedef int (*mwi_entry_fn)(unsigned int, void *, unsigned int);

/* ---- the C library's, resolved when the game's process loads this one ---- */

struct mwi_timespec
{
        long seconds, nanoseconds;
};
struct mwi_dl_info
{
        const char *file;
        void *base;
        const char *symbol;
        void *address;
};

#ifndef MWI_CHECK
extern int open(const char *, int, ...);
extern int close(int);
extern int ioctl(int, unsigned long, ...);
extern void *mmap(void *, unsigned long, int, int, int, long);
extern int munmap(void *, unsigned long);
extern long write(int, const void *, unsigned long);
extern char *getenv(const char *);
extern int clock_gettime(int, struct mwi_timespec *);
extern int dladdr(const void *, struct mwi_dl_info *);
extern void *dlopen(const char *, int);
extern void *dlsym(void *, const char *);
extern int __cxa_atexit(void (*)(void *), void *, void *);
#endif

/* ---- the state: one game, one SDL, one page ---- */

#define MWI_LOG_RECORDS 2048u

struct mwi_record
{
        unsigned long long ns, us;
        int a, b;
        unsigned short kind, code;
        unsigned int what;
};

static struct
{
        mwi_fn real[MWI_AT_LAST + 1]; // the calling SDL's own functions, for the places taken over
        const volatile struct mwi_page *page;
        int fd, looked, claim_wanted, claimed, armed, busy;
        int compat;                   // the SDL is sdl2-compat (SDL 2's API on SDL 3: its patch numbers start at 50)
        unsigned int generation, cursor, held, lost;
        long sum_x, sum_y;            // the relative motion read and not yet asked for (SDL_GetRelativeMouseState)
        int fine_x, fine_y;           // high-resolution wheel turns not yet a whole notch (120ths)
        int log_fd, log_looked, log_busy;
        unsigned int logged;
} mwi = {.fd = -1, .log_fd = -1};

static struct mwi_record mwi_log[MWI_LOG_RECORDS]; // zero, so it takes no room in the file and no page until a log is asked for

/* ---- the system's part, which a check supplies itself ---- */

#ifdef MWI_CHECK
static unsigned long long mwi_check_now;
static int mwi_check_claims[8], mwi_check_claim_count;
static unsigned long long mwi_now_ns(void) { return mwi_check_now; }
static int mwi_claim_send(int fd, unsigned int claim)
{
        (void)fd;
        if (mwi_check_claim_count < 8)
                mwi_check_claims[mwi_check_claim_count] = (int)claim;
        mwi_check_claim_count++;
        return 0;
}
static const char *mwi_env(const char *name) { (void)name; return 0; }
static void mwi_attach(void) { mwi.looked = 1; }
static void mwi_log_out(const char *text, unsigned long length) { (void)text, (void)length; }
static void mwi_log_open(void) { mwi.log_looked = 1; }
#else
static unsigned long long mwi_now_ns(void)
{
        struct mwi_timespec now = {0, 0};

        clock_gettime(1, &now);
        return (unsigned long long)now.seconds * 1000000000ull + (unsigned long long)now.nanoseconds;
}

static int mwi_claim_send(int fd, unsigned int claim) { return ioctl(fd, MWI_IOCTL_CLAIM, (unsigned long)claim); }

static const char *mwi_env(const char *name) { return getenv(name); }

// /dev/spark, and this thread group's page on it, read only; nothing when either is not there (another kernel, a bowl that hides it).
static void mwi_attach(void)
{
        void *mapped;
        int fd;

        mwi.looked = 1;
        if ((fd = open("/dev/spark", 0 | 0x80000)) < 0)
                return;
        mapped = mmap(0, MWI_PAGE_BYTES, 1, 1, fd, (long)(MWI_PAGE_PGOFF * MWI_PAGE_BYTES));
        if (mapped == (void *)-1l)
        {
                close(fd);
                return;
        }
        if (((const struct mwi_page *)mapped)->magic != MWI_PAGE_MAGIC || ((const struct mwi_page *)mapped)->version != MWI_PAGE_VERSION ||
            ((const struct mwi_page *)mapped)->mask != MWI_SLOTS - 1u)
        {
                munmap(mapped, MWI_PAGE_BYTES);
                close(fd);
                return;
        }
        mwi.fd = fd;
        mwi.page = (const volatile struct mwi_page *)mapped;
}

static void mwi_log_out(const char *text, unsigned long length)
{
        while (length && mwi.log_fd >= 0)
        {
                long wrote = write(mwi.log_fd, text, length);

                if (wrote <= 0)
                        return;
                text += wrote, length -= (unsigned long)wrote;
        }
}

static void mwi_log_flush(void);
static void mwi_at_exit(void *unused) { (void)unused, mwi_log_flush(); }

static void mwi_log_open(void)
{
        const char *path = getenv("MOONWATER_INPUT_LOG");

        mwi.log_looked = 1;
        if (!path || !*path)
                return;
        // O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC
        mwi.log_fd = open(path, 1 | 0x40 | 0x400 | 0x80000, 0644);
        if (mwi.log_fd >= 0)
                __cxa_atexit(mwi_at_exit, 0, 0);
}
#endif

/* ---- the meter's log ---- */

// Decimal by subtraction of powers of ten: a 64-bit division on i386 is libgcc's __udivdi3, which no C library exports.
static unsigned int mwi_decimal(char *into, unsigned long long value, int negative)
{
        static const unsigned long long powers[20] = {
                10000000000000000000ull, 1000000000000000000ull, 100000000000000000ull, 10000000000000000ull, 1000000000000000ull,
                100000000000000ull, 10000000000000ull, 1000000000000ull, 100000000000ull, 10000000000ull, 1000000000ull, 100000000ull,
                10000000ull, 1000000ull, 100000ull, 10000ull, 1000ull, 100ull, 10ull, 1ull};
        unsigned int at = 0, started = 0;

        if (negative)
                into[at++] = '-';
        for (unsigned int place = 0; place < 20; place++)
        {
                char digit = '0';

                while (value >= powers[place])
                        value -= powers[place], digit++;
                if (digit != '0' || started || place == 19)
                        into[at++] = digit, started = 1;
        }
        return at;
}

static void mwi_log_lock(void)
{
        while (__atomic_exchange_n(&mwi.log_busy, 1, __ATOMIC_ACQUIRE))
                ;
}

static void mwi_log_unlock(void)
{
        __atomic_store_n(&mwi.log_busy, 0, __ATOMIC_RELEASE);
}

static void mwi_log_flush_locked(void)
{
        char text[4096];
        unsigned int length = 0;

        if (mwi.log_fd < 0)
                return;
        for (unsigned int at = 0; at < mwi.logged && at < MWI_LOG_RECORDS; at++)
        {
                const struct mwi_record *r = &mwi_log[at];

                if (length > sizeof text - 128)
                        mwi_log_out(text, length), length = 0;
                text[length++] = (char)r->what;
                text[length++] = ' ', length += mwi_decimal(text + length, r->ns, 0);
                text[length++] = ' ', length += mwi_decimal(text + length, r->us, 0);
                text[length++] = ' ', length += mwi_decimal(text + length, (unsigned long long)(r->a < 0 ? -(long long)r->a : r->a), r->a < 0);
                text[length++] = ' ', length += mwi_decimal(text + length, (unsigned long long)(r->b < 0 ? -(long long)r->b : r->b), r->b < 0);
                text[length++] = ' ', length += mwi_decimal(text + length, r->kind, 0);
                text[length++] = ' ', length += mwi_decimal(text + length, r->code, 0);
                text[length++] = '\n';
        }
        mwi_log_out(text, length);
        mwi.logged = 0;
}

static void mwi_log_flush(void)
{
        mwi_log_lock();
        mwi_log_flush_locked();
        mwi_log_unlock();
}

static void mwi_note(unsigned int what, unsigned long long ns, unsigned long long us, int a, int b, unsigned short kind, unsigned short code)
{
        mwi_log_lock();
        if (!mwi.log_looked)
                mwi_log_open();
        if (mwi.log_fd < 0)
        {
                mwi_log_unlock();
                return;
        }
        if (mwi.logged >= MWI_LOG_RECORDS)
                mwi_log_flush_locked();
        mwi_log[mwi.logged++] = (struct mwi_record){ns, us, a, b, kind, code, what};
        mwi_log_unlock();
}

/* ---- SDL's own functions, through the places this library took ---- */

typedef void (*mwi_pump_fn)(void);
typedef int (*mwi_peep_fn)(union mwi_sdl_event *, int, int, unsigned int, unsigned int);
typedef int (*mwi_poll_fn)(union mwi_sdl_event *);
typedef int (*mwi_wait_timeout_fn)(union mwi_sdl_event *, int);
typedef int (*mwi_push_fn)(union mwi_sdl_event *);
typedef void *(*mwi_focus_fn)(void);
typedef unsigned int (*mwi_state_fn)(int *, int *);
typedef int (*mwi_set_relative_fn)(int);
typedef unsigned int (*mwi_window_id_fn)(void *);
typedef void (*mwi_swap_fn)(void *);
typedef void (*mwi_quit_fn)(void);

#define MWI_REAL(type, at) ((type)mwi.real[at])

// SDL's button for an evdev one: left, middle, right, then back and forward as X1 and X2. 0 for one SDL has no name for.
static unsigned char mwi_button_of(unsigned short code)
{
        switch (code)
        {
        case 0x110: return 1;              // BTN_LEFT
        case 0x111: return 3;              // BTN_RIGHT
        case 0x112: return 2;              // BTN_MIDDLE
        case 0x113: case 0x116: return 4;  // BTN_SIDE, BTN_BACK: X1
        case 0x114: case 0x115: return 5;  // BTN_EXTRA, BTN_FORWARD: X2
        }
        return 0;
}

static unsigned int mwi_window(void)
{
        void *window = mwi.real[MWI_AT_MOUSE_FOCUS] ? MWI_REAL(mwi_focus_fn, MWI_AT_MOUSE_FOCUS)() : 0;

        return window && mwi.real[MWI_AT_WINDOW_ID] ? MWI_REAL(mwi_window_id_fn, MWI_AT_WINDOW_ID)(window) : 0;
}

static void mwi_push(union mwi_sdl_event *event)
{
        if (mwi.real[MWI_AT_PUSH])
                MWI_REAL(mwi_push_fn, MWI_AT_PUSH)(event);
}

static void mwi_button(unsigned char button, int pressed, unsigned int window, int x, int y)
{
        union mwi_sdl_event event;
        unsigned int bit;

        if (!button || button > 32)
                return;
        bit = 1u << (button - 1);
        if (pressed ? (mwi.held & bit) : !(mwi.held & bit))
                return;
        mwi.held = pressed ? mwi.held | bit : mwi.held & ~bit;
        event = (union mwi_sdl_event){.button = {pressed ? MWI_SDL_MOUSEBUTTONDOWN : MWI_SDL_MOUSEBUTTONUP, 0, window, 0, button,
                                                 (unsigned char)(pressed ? 1 : 0), 1, 0, x, y}};
        mwi_push(&event);
}

// Every button this library pressed, let go: the page is no longer the game's.
static void mwi_release_all(void)
{
        int x = 0, y = 0;
        unsigned int window = mwi.held ? mwi_window() : 0;

        if (mwi.held && mwi.real[MWI_AT_MOUSE_STATE])
                MWI_REAL(mwi_state_fn, MWI_AT_MOUSE_STATE)(&x, &y);
        for (unsigned char button = 1; button <= 8; button++)
                mwi_button(button, 0, window, x, y);
}

// One report, as SDL would have made it of the same motion, press or turn (under take); counted for the relative state either way.
static void mwi_report(const struct mwi_slot *slot, unsigned int window, int x, int y, int take)
{
        union mwi_sdl_event event;

        if (slot->kind == MWI_KIND_REL)
        {
                mwi.sum_x += slot->dx, mwi.sum_y += slot->dy;
                if (!take)
                        return;
                event = (union mwi_sdl_event){.motion = {MWI_SDL_MOUSEMOTION, 0, window, 0, mwi.held, x, y, slot->dx, slot->dy}};
                mwi_push(&event);
        }
        else if (slot->kind == MWI_KIND_KEY && take)
                mwi_button(mwi_button_of(slot->code), slot->dx != 0, window, x, y);
        else if (slot->kind == MWI_KIND_WHEEL && take)
        {
                int fine = slot->code == 11 || slot->code == 12, sideways = slot->code == 6 || slot->code == 12;
                int *rest = sideways ? &mwi.fine_x : &mwi.fine_y, notches;
                float precise = fine ? (float)slot->dx / 120.0f : (float)slot->dx;

                if (fine)
                        *rest += slot->dx, notches = *rest / 120, *rest -= notches * 120;
                else
                        notches = slot->dx;
                // sdl2-compat makes a pushed SDL 2 wheel into SDL 3's by copying its words over SDL 3's (whose wheel.integer_x and
                // integer_y then hold SDL 2's mouseX and mouseY, which it never writes again) and back into SDL 2's (x and y from
                // integer_x and integer_y): the game read the pointer's place as its notches (seen on the box, 2.32.74). There the
                // place words carry the notches, so x and y come out right; in relative mode the place is nothing a game reads.
                event = (union mwi_sdl_event){.wheel = {MWI_SDL_MOUSEWHEEL, 0, window, 0, sideways ? notches : 0, sideways ? 0 : notches, 0,
                                                        sideways ? precise : 0.0f, sideways ? 0.0f : precise,
                                                        mwi.compat ? (sideways ? notches : 0) : x, mwi.compat ? (sideways ? 0 : notches) : y}};
                mwi_push(&event);
        }
}

/*
        The page read: what the kernel wrote since the last read, taken only when the generation is the same before and after
        (a disarm moves it before it wipes), and only the slots that cannot have been written again under the read. A generation
        that moved is the page armed or not: the held buttons are let go and the read starts at the head. A reader too late for
        some slots loses those (counted: every slot SLOTS or more behind the head may be under the kernel's next write), and
        then makes its buttons what the page says is held.
*/
#define MWI_BATCH 32u

static void mwi_drain(void)
{
        struct mwi_slot batch[MWI_BATCH];
        const volatile struct mwi_page *page = mwi.page;
        unsigned int generation, head, from, count, after, lost = 0;
        unsigned long long now;
        int take = mwi.claimed == MWI_CLAIM_TAKE, x = 0, y = 0;
        unsigned int window;

        if (!page || mwi.claimed == MWI_CLAIM_OFF || __atomic_exchange_n(&mwi.busy, 1, __ATOMIC_ACQUIRE))
                return;
        generation = __atomic_load_n(&page->generation, __ATOMIC_ACQUIRE);
        if (generation != mwi.generation)
        {
                int armed = __atomic_load_n(&page->state, __ATOMIC_RELAXED) == MWI_STATE_ARMED;

                mwi_release_all();
                mwi.generation = generation, mwi.armed = armed, mwi.fine_x = mwi.fine_y = 0;
                // An armed page is read from the head it was armed at: what was written before this read saw the arm is the game's.
                mwi.cursor = armed ? __atomic_load_n(&page->armed_head, __ATOMIC_RELAXED) : __atomic_load_n(&page->head, __ATOMIC_ACQUIRE);
                mwi_note('A', mwi_now_ns(), 0, (int)generation, armed, 0, 0);
        }
        if (!mwi.armed || (head = __atomic_load_n(&page->head, __ATOMIC_ACQUIRE)) == mwi.cursor)
        {
                __atomic_store_n(&mwi.busy, 0, __ATOMIC_RELEASE);
                return;
        }
        now = mwi_now_ns();
        window = take ? mwi_window() : 0;
        if (take && mwi.real[MWI_AT_MOUSE_STATE])
                MWI_REAL(mwi_state_fn, MWI_AT_MOUSE_STATE)(&x, &y);
        while (mwi.cursor != head)
        {
                // The kernel writes slot head before it publishes head + 1, and slot head is slot head - SLOTS: the newest
                // SLOTS - 1 are the ones that cannot be under a write.
                if (head - mwi.cursor >= MWI_SLOTS)
                        lost += head - mwi.cursor - (MWI_SLOTS - 1u), mwi.cursor = head - (MWI_SLOTS - 1u);
                from = mwi.cursor;
                count = head - from < MWI_BATCH ? head - from : MWI_BATCH;
                for (unsigned int at = 0; at < count; at++)
                {
                        const volatile struct mwi_slot *s = &page->slots[(from + at) & (MWI_SLOTS - 1u)];

                        batch[at].dx = s->dx, batch[at].dy = s->dy, batch[at].time_us = s->time_us;
                        batch[at].kind = s->kind, batch[at].code = s->code, batch[at].flags = s->flags;
                }
                __atomic_thread_fence(__ATOMIC_ACQUIRE);
                if (__atomic_load_n(&page->generation, __ATOMIC_RELAXED) != generation)
                        break; // the next read sees the move and starts again
                after = __atomic_load_n(&page->head, __ATOMIC_ACQUIRE);
                // The slots the kernel may have written again while they were copied: the oldest ones of a reader this late.
                for (unsigned int at = 0; at < count; at++)
                {
                        if (after - (from + at) >= MWI_SLOTS)
                        {
                                lost++;
                                continue;
                        }
                        mwi_note('S', now, batch[at].time_us, batch[at].dx, batch[at].dy, batch[at].kind, batch[at].code);
                        mwi_report(&batch[at], window, x, y, take);
                }
                mwi.cursor = from + count;
        }
        if (lost)
        {
                unsigned int held = __atomic_load_n(&page->buttons, __ATOMIC_RELAXED);

                mwi.lost += lost;
                mwi_note('L', now, 0, (int)lost, 0, 0, 0);
                for (unsigned int bit = 0; take && bit < 8; bit++)
                        mwi_button(mwi_button_of((unsigned short)(0x110 + bit)), (int)(held >> bit & 1), window, x, y);
        }
        __atomic_store_n(&mwi.busy, 0, __ATOMIC_RELEASE);
}

// The claim the game's relative mode asks for, sent when it changes. A claim let go lets every pressed button go with it.
static void mwi_claim(int on)
{
        unsigned int want = on ? (unsigned int)mwi.claim_wanted : MWI_CLAIM_OFF;

        if (on && !mwi.looked)
                mwi_attach();
        if (!mwi.page || want == (unsigned int)mwi.claimed)
                return;
        if (mwi_claim_send(mwi.fd, want) != 0)
                return;
        if (want == MWI_CLAIM_OFF)
                mwi_release_all(), mwi.armed = 0;
        mwi.claimed = (int)want;
        mwi.generation = ~__atomic_load_n(&mwi.page->generation, __ATOMIC_ACQUIRE); // read the page's state again at the next read
}

/* ---- the places taken over ---- */

static void mwi_observed(const union mwi_sdl_event *event, int got)
{
        if (got > 0 && mwi.claimed == MWI_CLAIM_OBSERVE && event && event->type == MWI_SDL_MOUSEMOTION)
                mwi_note('X', mwi_now_ns(), 0, event->motion.xrel, event->motion.yrel, 0, 0);
}

static void mwi_pump_events(void)
{
        mwi_drain();
        MWI_REAL(mwi_pump_fn, MWI_AT_PUMP)();
}

static int mwi_peep_events(union mwi_sdl_event *events, int count, int action, unsigned int first, unsigned int last)
{
        int got;

        if (action == MWI_SDL_GETEVENT)
                mwi_drain();
        got = MWI_REAL(mwi_peep_fn, MWI_AT_PEEP)(events, count, action, first, last);
        for (int at = 0; action == MWI_SDL_GETEVENT && events && at < got; at++)
                mwi_observed(&events[at], 1);
        return got;
}

static int mwi_poll_event(union mwi_sdl_event *event)
{
        int got;

        mwi_drain();
        got = MWI_REAL(mwi_poll_fn, MWI_AT_POLL)(event);
        mwi_observed(event, got);
        return got;
}

// A wait the page can end: SDL's own wait, a millisecond at a time while a page is read, so a report never waits on X. The time left
// is counted in those slices (no 64-bit division, which i386 would take from libgcc).
static int mwi_wait_event_timeout(union mwi_sdl_event *event, int timeout)
{
        int got;

        for (;;)
        {
                mwi_drain();
                if (!mwi.armed || timeout == 0)
                        break;
                if ((got = MWI_REAL(mwi_wait_timeout_fn, MWI_AT_WAIT_TIMEOUT)(event, 1)) != 0)
                {
                        mwi_observed(event, got);
                        return got;
                }
                if (timeout > 0 && --timeout == 0)
                        return 0;
        }
        got = MWI_REAL(mwi_wait_timeout_fn, MWI_AT_WAIT_TIMEOUT)(event, timeout);
        mwi_observed(event, got);
        return got;
}

static int mwi_wait_event(union mwi_sdl_event *event) { return mwi_wait_event_timeout(event, -1); }

static unsigned int mwi_mouse_state(int *x, int *y)
{
        unsigned int buttons;

        mwi_drain();
        buttons = MWI_REAL(mwi_state_fn, MWI_AT_MOUSE_STATE)(x, y);
        return buttons | (mwi.claimed == MWI_CLAIM_TAKE ? mwi.held : 0);
}

static unsigned int mwi_relative_state(int *x, int *y)
{
        int own_x = 0, own_y = 0;
        unsigned int buttons;

        mwi_drain();
        buttons = MWI_REAL(mwi_state_fn, MWI_AT_RELATIVE_STATE)(&own_x, &own_y);
        if (mwi.claimed == MWI_CLAIM_TAKE)
                own_x += (int)mwi.sum_x, own_y += (int)mwi.sum_y, buttons |= mwi.held;
        mwi.sum_x = mwi.sum_y = 0;
        if (x)
                *x = own_x;
        if (y)
                *y = own_y;
        return buttons;
}

static int mwi_set_relative(int on)
{
        int done = MWI_REAL(mwi_set_relative_fn, MWI_AT_SET_RELATIVE)(on);

        if (done == 0)
                mwi_claim(on);
        return done;
}

static void mwi_swap(void *window)
{
        if (mwi.claimed != MWI_CLAIM_OFF)
                mwi_note('W', mwi_now_ns(), 0, 0, 0, 0, 0);
        MWI_REAL(mwi_swap_fn, MWI_AT_SWAP)(window);
}

static void mwi_quit(void)
{
        mwi_claim(0);
        mwi_log_flush();
        MWI_REAL(mwi_quit_fn, MWI_AT_QUIT)();
}

/* ---- the entry ---- */

static int mwi_same(const char *a, const char *b)
{
        while (*a && *a == *b)
                a++, b++;
        return *a == *b;
}

/*
        The table filled by the calling SDL's own entry, then the places above put in front of SDL's: refused (SDL then fills
        its table itself, as if this library were not named) for another API, a table too short for the places, or an SDL whose
        table does not hold SDL 2 on Linux where these places say (its version and its platform, asked through those places).
*/
static int mwi_install(unsigned int api, void *table, unsigned int size, mwi_entry_fn sdl_entry)
{
        static const struct { unsigned short at; mwi_fn ours; } places[] = {
                {MWI_AT_QUIT, (mwi_fn)mwi_quit},
                {MWI_AT_PUMP, (mwi_fn)mwi_pump_events},
                {MWI_AT_PEEP, (mwi_fn)mwi_peep_events},
                {MWI_AT_POLL, (mwi_fn)mwi_poll_event},
                {MWI_AT_WAIT, (mwi_fn)mwi_wait_event},
                {MWI_AT_WAIT_TIMEOUT, (mwi_fn)mwi_wait_event_timeout},
                {MWI_AT_MOUSE_STATE, (mwi_fn)mwi_mouse_state},
                {MWI_AT_RELATIVE_STATE, (mwi_fn)mwi_relative_state},
                {MWI_AT_SET_RELATIVE, (mwi_fn)mwi_set_relative},
                {MWI_AT_SWAP, (mwi_fn)mwi_swap},
        };
        static const unsigned short used[] = {MWI_AT_PUSH, MWI_AT_MOUSE_FOCUS, MWI_AT_WINDOW_ID};
        mwi_fn *slots = (mwi_fn *)table;
        struct mwi_sdl_version version = {0, 0, 0};
        const char *platform;

        if (api != MWI_SDL_API || !table || !sdl_entry || size % sizeof(mwi_fn) || size / sizeof(mwi_fn) <= MWI_AT_LAST ||
            sdl_entry(api, table, size) < 0)
                return -1;
        if (!slots[MWI_AT_VERSION] || !slots[MWI_AT_PLATFORM])
                return -1;
        ((void (*)(struct mwi_sdl_version *))slots[MWI_AT_VERSION])(&version);
        platform = ((const char *(*)(void))slots[MWI_AT_PLATFORM])();
        if (version.major != 2 || !platform || !mwi_same(platform, "Linux"))
                return -1;
        mwi.compat = version.patch >= 50;
        for (unsigned int at = 0; at < sizeof places / sizeof places[0]; at++)
                if (!slots[places[at].at])
                        return -1;
        for (unsigned int at = 0; at < sizeof places / sizeof places[0]; at++)
                mwi.real[places[at].at] = slots[places[at].at], slots[places[at].at] = places[at].ours;
        for (unsigned int at = 0; at < sizeof used / sizeof used[0]; at++)
                mwi.real[used[at]] = slots[used[at]];
        {
                const char *mode = mwi_env("MOONWATER_INPUT");

                mwi.claim_wanted = !mode || !*mode || mwi_same(mode, "take") ? (int)MWI_CLAIM_TAKE
                                   : mwi_same(mode, "observe")              ? (int)MWI_CLAIM_OBSERVE
                                                                            : (int)MWI_CLAIM_OFF;
        }
        mwi_note('I', mwi_now_ns(), 0, version.minor, version.patch, (unsigned short)mwi.claim_wanted, 0);
        return 0;
}

#ifndef MWI_CHECK
// The SDL that called: the library its table is in, and that library's own entry (never this one, which the preload put first).
MWI_EXPORT int SDL_DYNAPI_entry(unsigned int api, void *table, unsigned int size)
{
        struct mwi_dl_info info = {0, 0, 0, 0};
        mwi_entry_fn sdl_entry = 0;
        void *library;

        if (!dladdr(table, &info))
                return -1;
        // RTLD_NOW | RTLD_NOLOAD; the program itself (an SDL linked in) by its null name
        library = dlopen(info.file && *info.file ? info.file : 0, 2 | 4);
        if (!library && info.file && *info.file)
                library = dlopen(0, 2);
        if (library)
                sdl_entry = (mwi_entry_fn)dlsym(library, "SDL_DYNAPI_entry");
        if (sdl_entry == (mwi_entry_fn)SDL_DYNAPI_entry)
                return -1;
        return mwi_install(api, table, size, sdl_entry);
}
#endif
