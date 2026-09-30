/*
        Moonwater Canvas

        An in-kernel compositor, on the DRM client API the framebuffer console
        and drm_log already use. Attaches without patching the kernel: a DRM
        device can be opened like any other file, and struct drm_file leads
        back to the drm_device behind it.

        Two files are not here. window.c is the page a program shares with
        this and the whole interface it needs -- see there first. One run of
        pixels and one glyph are lib.c assembly under KERNEL_MODE, per
        architecture, and everything Canvas draws goes through them.

        The rest is the sections below, in dependency order: paint, text,
        pane, console, compose, plane, drag, output, keys, client, pointer,
        and last the ioctls the device hands on. Included by core.c after
        lib.c, whose "end" macro is why the DRM headers the module needs
        before lib.c are at the top of core.c rather than here.

        There is one desktop across every card, and one cursor on it. Windows
        and the cursor are in desktop coordinates; an output is a rectangle of
        the desktop that some crtc scans out. Nothing has a fixed maximum.
*/

#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/rtmutex.h>
#include <uapi/linux/sched/types.h>
#include <linux/pm_qos.h>
#include <linux/suspend.h>
#include <linux/input.h>
#include <linux/math64.h>
#include <linux/minmax.h>
#include <linux/list_sort.h>
#include <linux/hrtimer.h>
#include <linux/font.h>
#include <linux/cacheflush.h>
#include <drm/drm_file.h>
#include <drm/drm_rect.h>

#include "window.c"


/*
        For the ones that mean there is no picture.

        A machine with no serial port reads these off its own monitor or not at
        all, and that only works while something else still owns the screen --
        which is why every one of them is next to a decision to let go of it.
*/

/*
        Colours are an index, not a value.

        Every one has to be converted for the format of the screen it lands
        on, and that used to happen at each of the hundreds of thousands of
        draw calls a compose makes. An output converts the whole palette once
        when it learns its format, and the drawing code indexes what it was
        handed without knowing what a format is.
*/
enum
{
        INK_DESKTOP,
        INK_FRAME,
        INK_TITLE,
        INK_TITLE_LIT,
        INK_BODY,
        INK_TEXT,
        INK_CURSOR,
        INK_CURSOR_EDGE,
        INK_COUNT,
};

/*
        A pane is the compositor's side of a window: where it is, and the
        pixels behind it. struct window, in window.c, is the page the
        program that owns it has mapped. Panes without one are the compositor's
        own.

        The geometry here is a checked copy of the geometry there. Nothing
        composes from the shared page directly: a program can store into it
        between a bounds check and a read.
*/
struct pane
{
        struct list_head link;
        int x, y, z;
        int width, height;
        unsigned int region;
        unsigned int display;
        unsigned int style;
        unsigned int state;
        int edge;
        unsigned int sequence;

        // When a commit of this window last composed, and whether a key has
        // gone to it since. See window_ioctl_commit.
        u64 composed_ns;
        _Bool keyed;

        char title[WINDOW_TITLE_MAX];
        unsigned int title_length;

        struct window *shared;
        u32 *pixels;

        // Where the program sleeps until there is something for it: a key
        // in its ring, or a grid that changed under it. A poll on the window
        // file waits here, and the thread that wrote the key wakes it, so a
        // keystroke reaches the program in the time a wakeup takes rather
        // than at the end of whatever nap the program was taking.
        wait_queue_head_t wait;

        // A window of text instead: cells the compositor draws the glyphs for.
        struct window_cell *cells;
        unsigned int columns, rows;
        unsigned int grid_columns, grid_rows;
        unsigned int max_columns, max_rows;
        unsigned int damage_row, damage_rows;

        /*
                The cells are a ring of lines, the same shape a program has
                mapped: stride apart, history of them, head one past the
                newest, and how long each one was written.

                view is the line at the top of what is shown, or PANE_LIVE to
                follow the end. An absolute line rather than a distance back,
                so a line arriving does not slide what is being read out from
                under it.
        */
        unsigned int *lengths;
        unsigned int stride, history, head;
        unsigned int view;
        unsigned int view_skip;
        _Bool view_moved;

        unsigned int pitch;
        unsigned int max_width, max_height;
        unsigned long bytes;
        void *mapping;

        /*
                A program's ring of cells is reserved whole and paid for a
                page at a time as it is written. area is the reservation;
                pages says which of its pages have memory behind them, and a
                null entry is a page nothing is mapped at, here or in the
                program. Null pages for every other window, whose mapping is
                an ordinary vmalloc.
        */
        struct vm_struct *area;
        struct page **pages;
        unsigned long page_count;
        struct mutex pages_lock;

        /*
                Where it was before something other than its program decided
                its shape, and which of those decided.

                One rectangle, saved on the way out of floating and not on
                every arrangement: snapped left, then right, then dragged off
                lands where the window started rather than on the left half.
        */
        int saved_x, saved_y, saved_w, saved_h;
        unsigned int saved_display;
        unsigned int arranged;

        /*
                How far down the stack of centred windows this one is, and
                whether that has been decided.

                Decided once and then kept, because placement runs on every
                commit a program makes: a step worked out afresh each time
                would walk the window down the screen while its program drew.
        */
        int cascade;
        _Bool cascaded;

        /*
                Opened by the terminal the compositor started, and so owed the
                keyboard by the next desktop_refresh_panes -- once, whether or
                not it can be given then.
        */
        _Bool spawned;

        /*
                One wake owed to the program, because it has been asked to
                close.

                An event and not a state: the state is WINDOW_CLOSING in the
                shared page, which the program reads whenever it likes and
                nothing here ever clears. If the poll answered from the state
                instead, a program that does not act on the request would be
                handed a ready file every time it asked -- ppoll returning
                immediately, for ever, which is a spin and not a wait.
        */
        atomic_t closing_wake;
};

struct output
{
        struct list_head link;
        struct canvas *canvas;
        struct drm_client_buffer *buffer;
        struct drm_mode_set *mode_set;

        int x, y;
        unsigned int width, height;

        // Every colour, converted once for this output's format.
        u32 palette[INK_COUNT];
        u32 opaque;

        // Said once. Everything drawn goes through the mapping, so a failure
        // to make one is a screen with nothing on it, not a dropped frame.
        _Bool unmappable;

        // Null means the cursor is drawn into this output's framebuffer.
        struct drm_plane *cursor_plane;
        struct drm_client_buffer *cursor_buffer;
        // The plane's other cursor image. A driver that copies on framebuffer
        // change, not on pixel writes, keeps showing the first shape unless
        // the object itself changes; this is that second object.
        struct drm_client_buffer *cursor_back;
        unsigned int cursor_w, cursor_h;
        unsigned int cursor_shape;
        unsigned int cursor_scale;
        _Bool cursor_shown;
        unsigned int cursor_recovery; // 1 pending, 2 covered by this commit

        /*
                The flusher's, under desktop.flush_lock: the rectangle waiting
                for the driver or the whole buffer, whether this output is
                with the driver right now, and whether it was dropped while it
                was -- in which case the flusher is the one to free it.
        */
        struct list_head flush_link;
        struct drm_rect flush_pending;
        _Bool flush_queued;
        _Bool flush_whole;
        _Bool flushing;
        _Bool retired;

        // The scanout the CRTC still holds after a grow, freed once the
        // commit that replaces it has landed. Destroying it sooner blanks
        // the pipe.
        struct drm_client_buffer *replaced;
};

struct canvas
{
        struct list_head link;
        struct drm_client_dev client;
        _Bool started;

        // What the last commit answered, so the same answer is not said
        // twice. One, because no commit ever answers that: nothing has been
        // put on this card's screens yet.
        int set_result;

        // Outputs dropped while their buffer was with the flusher. The card
        // is released only once the flusher has freed every one.
        atomic_t retiring;

        // Hotplug runs here, never on the DRM helper workqueue. A commit
        // from that queue disables cursor planes and can wait on the same
        // queue, which is a lockup: the first picture stays, the pointer
        // thread never runs, and the kernel log is what is left.
        struct work_struct plug;

        // A hotplug that arrived while the machine slept: the card's power was
        // going or half back, so it waits for the wake to run.
        _Bool replug;
};

static struct desktop
{
        /*
                An rt_mutex, so whoever holds it runs at the priority of whoever
                waits for it.

                The canvas thread is SCHED_FIFO and takes this for every pointer
                move, key and frame, while a program's WINDOW_IOCTL_COMMIT takes
                it in that program's own context at that program's priority. As
                a plain mutex the cursor waited behind a SCHED_OTHER terminal
                that stress-ng had preempted inside its commit: 27 waits of up
                to 11 ms in 7 s of one trace. The driver's flush is no longer
                done under it (flush_lock below), and what is left of a hold is
                run at the waiter's priority until it lets go.
        */
        struct rt_mutex lock;

        /*
                Serialises the input handler against itself.

                It is called straight from the input core, once per device,
                and every device has its own lock there -- so a tablet and a
                mouse reporting at the same moment were two writers on one set
                of counters. A leaf lock held for the length of one event.
        */
        spinlock_t input_lock;
        struct list_head outputs;
        struct list_head windows;

        /*
                Windows `moonwater canvas off` asked to close whose programs
                have not closed them yet. Off from the desktop, never drawn
                again, and freed by window_release like any other window: a
                mapping holds the file, and the file holds the pane.
        */
        struct list_head detached;

        int cursor_x, cursor_y;
        int drawn_x, drawn_y;

        // The pane being dragged, and where inside it the cursor took hold.
        struct pane *dragging;
        int grab_x, grab_y;
        struct pane *focused;

        // Resizing: which edges are held, and the rectangle and pointer
        // position they were held at, so every step measures from the grab
        // rather than accumulating.
        struct pane *resizing;
        unsigned int resize_edges;
        int resize_x, resize_y, resize_w, resize_h;
        int press_x, press_y;

        // The scrollbar being held, and where in its thumb it was taken, so
        // the thumb stays under the finger instead of jumping to it.
        struct pane *barring;
        int bar_grab;

        // The last press, for telling a second click of a pair from a first.
        struct pane *press_pane;
        u64 press_ns;

        // Fractions of a line left from a high-resolution wheel event.
        int wheel_remainder;

        unsigned int cursor_shape;
        unsigned int drawn_shape;

        /*
                Shake to find it, the way a desktop does: reverse direction
                enough times in a short enough window and the cursor grows,
                then goes back on its own.
        */
        unsigned int cursor_scale;
        unsigned int drawn_scale;
        u64 magnified_until;
        atomic_t magnify;
        atomic_t shake_dir;
        atomic_t shake_count;
        u64 shake_window;

        /*
                Acceleration. The remainder is what stops a gain that is not a
                whole number from quietly dropping the fraction of every
                movement: a slow drag would come up short of where it was
                aimed, which is the thing people notice.
        */
        u64 accel_stamp;
        int accel_x, accel_y;

        // What a device has reported so far this frame. A mouse sends each
        // axis as its own event and then says it is done, and a movement is
        // the whole of what arrived between those.
        int raw_x, raw_y;

        // The same for a tablet, which reports where it is rather than how far
        // it moved. abs_have says which axes this report has carried.
        int abs_x, abs_y;
        unsigned int abs_have;

        /*
                What changed since the last commit, so a program saying so
                repaints its own window rather than every screen. Four is a
                working set, not a limit: past it the whole desktop is cheaper
                than tracking the pieces.
        */
        struct drm_rect damage[4];
        unsigned int damage_count;
        _Bool damage_all;

        struct hrtimer frame;
        atomic_t frame_pending;
        u64 frame_ns;

        // Unconsumed wheel distance in v120 units: 120 is one detent.
        // Positive is back through what has already gone past.
        atomic_t wheel;
        unsigned int idle_frames;
        _Bool awake;
        _Bool started;

        // Asked for before the desktop had a size; canvas_start opens them.
        _Bool log_wanted;
        _Bool terminal_wanted;

        // Turned off from userspace: no window may be created until it is on.
        _Bool off;

        /*
                Another program is master of a card Canvas draws on. Input is
                not Canvas's while it is, and nothing drawn would land. Written
                under the lock; see canvas_suspend_check.
        */
        _Bool suspended;

        /*
                The machine is going to sleep, or has not finished waking.

                Not the same as suspended: no program holds the card, the card
                is about to have its power taken. Anything Canvas sent it from
                here on -- a frame, a modeset commit, a dirtyfb -- reached a
                device that was off or half back, and a driver that waits on
                its own hardware for such a call never returned. So it counts
                as the card being taken (desktop_taken), the frame timer is off,
                and the flusher hands nothing to a driver. Written under the
                lock, read without it by the flusher.
        */
        _Bool asleep;

        /*
                How many device pixels one drawn pixel is.

                A cell is eight by sixteen and a titlebar is twenty, and those
                are the right numbers on a ninety dot inch screen and a
                quarter of the right numbers on a Retina one. Everything the
                compositor draws is multiplied by this, so the same window is
                the same size in millimetres on both.
        */
        unsigned int scale;

        // The bounding box of every output. Read by the input handler in
        // atomic context, where it cannot walk the list.
        int width, height;

        atomic_t pending_x;
        atomic_t pending_y;
        atomic_t motion_pending;
        u64 motion_stamp;

        /*
                Keys as they arrive, before anything knows which window they
                belong to. The handler cannot take the lock that says which
                window has focus, and cannot follow a pointer to one that may
                be freed underneath it, so it records here and the thread
                delivers.
        */
        struct window_key key_ring[WINDOW_KEYS];
        atomic_t key_head;
        atomic_t key_tail;
        atomic_t modifiers;
        atomic_t focus_steps;
        atomic_t focus_commit;
        atomic_t focus_cycling;
        atomic_t minimize;

        /*
                A terminal asked for from the keyboard.

                One bit and not a count: held down, a key autorepeats, and a
                counter would answer that with a screen full of shells. The
                press is what asks, the repeat is not, and a second press that
                arrives before the thread has run is the same request.
        */
        atomic_t spawn;
        int focus_cycle_z;

        // The button, and where it went down. Picking a window needs the list,
        // which the input handler cannot walk, so it records and the thread
        // picks.
        atomic_t button_down;
        atomic_t button_changed;
        atomic_t button_x;
        atomic_t button_y;
        atomic_t client_button;
        atomic_t client_down;
        atomic_t client_changed;

        /*
                The driver's half of a repaint, handed to the flusher.

                drm_client_buffer_flush is dirtyfb, and on virtio-gpu and bochs
                that is a blocking atomic commit waiting out a display period.
                Done where the pixels were drawn, it was done under the lock
                above by whoever drew them -- a terminal's commit, the canvas
                thread's software cursor -- and every waiter for the lock
                waited out the period too. Each output keeps one pending
                rectangle, grown by whatever arrives before the flusher takes
                it, so a repaint costs a merge and a wake, and the driver sees
                at most one flush in flight and one waiting per output.
        */
        spinlock_t flush_lock;
        struct list_head flush_queue;
        wait_queue_head_t flush_idle;
        // Outputs with a flush in the driver's hands, for the wait before sleep.
        atomic_t flushes_in_flight;
} desktop = {
    .lock = __RT_MUTEX_INITIALIZER(desktop.lock),
    .input_lock = __SPIN_LOCK_UNLOCKED(desktop.input_lock),
    .flush_lock = __SPIN_LOCK_UNLOCKED(desktop.flush_lock),
    .flush_queue = LIST_HEAD_INIT(desktop.flush_queue),
    .flush_idle = __WAIT_QUEUE_HEAD_INITIALIZER(desktop.flush_idle),
    .outputs = LIST_HEAD_INIT(desktop.outputs),
    .windows = LIST_HEAD_INIT(desktop.windows),
    .detached = LIST_HEAD_INIT(desktop.detached),
    .scale = 1,
};

/*
        The terminal the compositor last started, as that task recorded itself
        in spawn_enter, until the first window it opens takes it back in
        window_ioctl_create. A reference: the pid it names cannot be freed and
        handed to another task while it is held here.
*/
static struct pid *canvas_spawned;

static DEFINE_MUTEX(canvas_list_lock);
static LIST_HEAD(canvas_list);

static void canvas_thread_start(void);
static void canvas_thread_stop(void);
static void canvas_thread_wake(void);
// Counted for `moonwater canvas`: how often each thing wakes while idle.
static unsigned int canvas_passes, canvas_ticks;
// And the CPU latency hold, below with the request it is.
static _Bool hold_on;
static unsigned int hold_takes;
static void canvas_flush_wake(void);
static _Bool canvas_flush_running(void);
static void output_free(struct output *output);
static void desktop_redraw(void);
static void desktop_recompose(void);
static void desktop_repaint(void);
static void cursor_plane_recover(void);
static void desktop_watch(void);
static u64 canvas_frame_ns(void);
static void cursor_move(int x, int y);
static _Bool desktop_taken(void);
static _Bool desktop_lit(void);

// Nanoseconds from an event arriving to the cursor being on screen.
static u64 pointer_latency_total;
static u64 pointer_latency_worst;
static unsigned long pointer_events;
static u64 pointer_queue_total;
static u64 pointer_draw_total;
static u64 pointer_flush_total;
static unsigned long pointer_counts;
static unsigned long pointer_moved;

// Hardware cursor diagnostics. Counters are atomic because plane arming also
// happens during output commits; requested/armed coordinate pairs are changed
// under desktop.lock. Updates include ordinary motion and urgent resize syncs.
static atomic_long_t cursor_plane_updates = ATOMIC_LONG_INIT(0);
static atomic_long_t cursor_plane_failures = ATOMIC_LONG_INIT(0);
static unsigned long cursor_plane_requested_generation;
static unsigned long cursor_plane_armed_generation;
static int cursor_plane_requested_x, cursor_plane_requested_y;
static int cursor_plane_armed_x, cursor_plane_armed_y;
static _Bool cursor_plane_recovery;

// What drawing costs: passes over every output, the time in them, and the
// pixels written, which against the size of the desktop is the overdraw.
static unsigned long canvas_composes;
static u64 canvas_compose_ns;
static unsigned long canvas_painted;
static unsigned long canvas_runs;
static u64 canvas_flush_ns;
static u64 canvas_text_ns;

/*
        The loops every pixel goes through, all assembly in lib.c: the
        reusable 32-bit span, and under KERNEL_MODE the strided rectangles,
        alpha blits and bitmap expansion. They are assembly because a full
        compose is four megabytes of stores and the kernel is compiled with
        no vector instructions, so what the C turned into was two four byte
        stores an iteration. The vector forms are below them.
*/
void memory_fill_u32(void *at, unsigned long long count, unsigned int value);
void memory_fill_u64_aligned(void *at, unsigned long long count,
                             unsigned long long value);
void canvas_rect_fill(u32 *at, unsigned long pitch, unsigned long width,
                      unsigned long height, u32 colour);
void canvas_glyph(u32 *at, unsigned long pitch, const u8 *bits,
                  unsigned long stride, unsigned long rows, u32 colour);
void canvas_glyph2(u32 *at, unsigned long pitch, const u8 *bits,
                   unsigned long stride, unsigned long rows, u32 colour);
void canvas_cell(u32 *at, unsigned long pitch, const u8 *bits,
                 unsigned long rows, u32 ink, u32 paper);
void canvas_cell2(u32 *at, unsigned long pitch, const u8 *bits,
                  unsigned long rows, u32 ink, u32 paper);
void canvas_row_blit(u32 *at, const u32 *from, unsigned long count, u32 opaque);
void canvas_cells(u32 *at, unsigned long pitch, const u8 *font,
                  const struct window_cell *cells, unsigned long count,
                  u32 ink, u32 paper);

/*
        The same loops a vector register at a time: AVX2, NEON, RVV. They
        are lib.c's too, in the one section a kernel object may hold vector
        instructions in, and they are called from nowhere but TARGET_PICK
        below, on a target whose simd says both conditions hold.
*/
void canvas_cell_wide(u32 *at, unsigned long pitch, const u8 *bits,
                      unsigned long rows, u32 ink, u32 paper);
void canvas_cell2_wide(u32 *at, unsigned long pitch, const u8 *bits,
                       unsigned long rows, u32 ink, u32 paper);
void canvas_row_blit_wide(u32 *at, const u32 *from, unsigned long count,
                          u32 opaque);
void canvas_cells_wide(u32 *at, unsigned long pitch, const u8 *font,
                       const struct window_cell *cells, unsigned long count,
                       u32 ink, u32 paper);
void canvas_rect_fill_wide(u32 *at, unsigned long pitch, unsigned long width,
                           unsigned long height, u32 colour);
void canvas_glyph_wide(u32 *at, unsigned long pitch, const u8 *bits,
                       unsigned long stride, unsigned long rows, u32 colour);

/*
        The vector registers, for a whole pass of drawing.

        They belong to whatever task the kernel interrupted, so using them
        means the kernel's bracket around the use -- kernel_fpu_begin,
        kernel_neon_begin, kernel_vector_begin -- which saves that task's
        state the first time and hands the registers back at the end, and
        only where may_use_simd says this context may have them at all. The
        first begin after a switch pays the save, so it is taken once for a
        compose and never per cell.

        The bracket is half of it. The other half is the buffer: a vector
        store onto device memory is emulated if it is trapped at all, and
        KVM's emulator has none, so a framebuffer that is iomem is drawn by
        the general register bodies whatever this answers. The caller asks
        with the mapping in hand.

        moonwater.simd=0 turns it off, for a machine where it misbehaves and
        for timing one against the other.
*/
static bool canvas_simd = true;
module_param_named(simd, canvas_simd, bool, 0644);

/*
        moonwater.pm_dark, for the guest lane, makes a card wake the way one
        that lost its pipe does. Value 1: the card is treated as a real one (it
        gets the hotplug rebind and the off and on cycle a guest's display is
        spared), its screens are turned off once everything is quiet so the
        driver's own suspend saves an off pipe and its resume restores that,
        and a hotplug is sent while the machine sleeps, as amdgpu's resume
        does. Value 2: the wake's own redraws are left undone, so only what the
        hotplug finds can bring the picture back. Value 4: no off and on cycle.
*/
static int canvas_pm_dark;
module_param_named(pm_dark, canvas_pm_dark, int, 0644);

struct canvas_simd_hold
{
        _Bool on;
#ifdef CONFIG_ARM64
        struct user_fpsimd_state state;
#endif
};

// AVX2 on x86, which the kernel clears when the OS does not save ymm;
// Advanced SIMD on arm64; V on riscv64. Each is a static branch.
static _Bool canvas_simd_present(void)
{
#if defined(CONFIG_X86_64)
        return boot_cpu_has(X86_FEATURE_AVX2);
#elif defined(CONFIG_ARM64) && defined(CONFIG_KERNEL_MODE_NEON)
        return cpu_has_neon();
#elif defined(CONFIG_RISCV) && defined(CONFIG_RISCV_ISA_V)
        return has_vector();
#else
        return false;
#endif
}

static _Bool canvas_simd_begin(struct canvas_simd_hold *hold, _Bool iomem)
{
        hold->on = false;
        if (iomem || !READ_ONCE(canvas_simd) || !canvas_simd_present() ||
            !may_use_simd())
                return false;
#if defined(CONFIG_X86_64)
        kernel_fpu_begin();
        hold->on = true;
#elif defined(CONFIG_ARM64) && defined(CONFIG_KERNEL_MODE_NEON)
        kernel_neon_begin(&hold->state);
        hold->on = true;
#elif defined(CONFIG_RISCV) && defined(CONFIG_RISCV_ISA_V)
        kernel_vector_begin();
        hold->on = true;
#endif
        return hold->on;
}

static void canvas_simd_end(struct canvas_simd_hold *hold)
{
        if (!hold->on)
                return;
#if defined(CONFIG_X86_64)
        kernel_fpu_end();
#elif defined(CONFIG_ARM64) && defined(CONFIG_KERNEL_MODE_NEON)
        kernel_neon_end(&hold->state);
#elif defined(CONFIG_RISCV) && defined(CONFIG_RISCV_ISA_V)
        kernel_vector_end();
#endif
        hold->on = false;
}

/*
        Somewhere to draw, and the only thing the drawing code is given.

        It used to take a pixel pointer, a pitch, an output and a clip, four
        arguments threaded through every function down to the innermost loop,
        and the innermost loop was the only place all four were wanted. Now
        the caller assembles one of these and the drawing knows nothing about
        outputs, formats or windows.

        The origin is where the target sits on the desktop, so drawing can be
        in desktop coordinates and land in target ones. The clip is the damage,
        in target coordinates. Stores still cut to the buffer: a clip that
        lies about the size is a write into whatever follows the scanout.
*/
struct target
{
        u32 *pixels;
        unsigned int pitch;
        int width, height;
        int x, y;
        u32 opaque;
        const u32 *ink;
        struct drm_rect clip;
        _Bool simd;             // inside canvas_simd_begin, and pixels is RAM
};

// A pixel loop's body for this target: the vector one only when simd says so.
#define TARGET_PICK(t, name) ((t)->simd ? name##_wide : name)

static void target_row(const struct target *t, int y, int x1, int x2, u32 colour);
static PURE _Bool output_touched(struct output *output, const struct drm_rect *damage,
                                 unsigned int count);
static void target_rectangle(const struct target *t, int x, int y, int w, int h,
                              u32 colour);

// The console window is made and drawn like any other, so pane has to be
// able to say a shape changed before the console section below.
static void console_regrid(struct pane *pane);

static CONST struct canvas *canvas_from_client(struct drm_client_dev *client)
{
        return container_of(client, struct canvas, client);
}

/* pure, not const: it reads the rectangles through its pointers. A const
   function may examine nothing but its argument values, so the compiler is
   free to treat the memory behind them as unread -- and it does. */
static inline PURE _Bool drm_rects_overlap(const struct drm_rect *a,
                                           const struct drm_rect *b)
{
        return a->x1 < b->x2 && b->x1 < a->x2 && a->y1 < b->y2 && b->y1 < a->y2;
}

static inline void canvas_rect_join(struct drm_rect *a, const struct drm_rect *b)
{
        a->x1 = min(a->x1, b->x1);
        a->y1 = min(a->y1, b->y1);
        a->x2 = max(a->x2, b->x2);
        a->y2 = max(a->y2, b->y2);
}

static inline CONST _Bool point_in_rect(int x, int y, int width, int height,
                                        int point_x, int point_y)
{
        return point_x >= x && point_x < x + width &&
               point_y >= y && point_y < y + height;
}

// Everything the compositor draws for itself, in device pixels.
#define canvas_title (WINDOW_TITLE * (int)desktop.scale)
#define canvas_border (2 * (int)desktop.scale)
#define canvas_cell_w (WINDOW_CELL_W * (int)desktop.scale)
#define canvas_cell_h (WINDOW_CELL_H * (int)desktop.scale)
#define canvas_bar (10 * (int)desktop.scale)

#define pane_title(pane) \
        ((pane)->style & WINDOW_FRAME ? canvas_title : 0)
#define pane_border(pane) \
        ((pane)->style & WINDOW_FRAME ? canvas_border : 0)
#define target_mark(pixels) \
        do { canvas_painted += (pixels); canvas_runs++; } while (0)

// The border and titlebar a framed window wears, and nothing when it does not.
static void pane_frame(const struct pane *pane, struct drm_rect *frame)
{
        int title = pane_title(pane);
        int border = pane_border(pane);

        drm_rect_init(frame, pane->x - border, pane->y - border,
                      pane->width + border * 2,
                      pane->height + title + border * 3);
}

/* pure for the same reason: it reads the pane behind the pointer. */
static inline PURE _Bool pane_in_title(const struct pane *pane, int x, int y)
{
        int title = pane_title(pane);

        return title &&
               point_in_rect(pane->x, pane->y, pane->width, title, x, y);
}

static void pane_gutter_rect(const struct pane *pane, struct drm_rect *gutter)
{
        drm_rect_init(gutter, pane->x + pane->width - canvas_bar,
                      pane->y + pane_title(pane), canvas_bar,
                      (int)pane->rows * canvas_cell_h);
}

/*
        How a window came to be the shape it is.

        Floating is the program's own rectangle. The rest are the
        compositor's, share one saved rectangle to go back to, and are all
        the same operation with different arithmetic.
*/
#define PANE_FLOATING 0u
#define PANE_MAXIMIZED 1u
#define PANE_LEFT 2u
#define PANE_RIGHT 3u

// How close to the edge of a screen a drag has to end to snap to it.
#define SNAP_MARGIN (8 * (int)desktop.scale)

// Which edges of a window's frame a point is close enough to take hold of.
#define EDGE_LEFT 1u
#define EDGE_RIGHT 2u
#define EDGE_TOP 4u
#define EDGE_BOTTOM 8u
#define EDGE_GRIP 6

/*
        The close button, in desktop coordinates.

        One function and not two, because two drift: the square that is drawn
        and the square that answers a press are the same square here, and a
        button that can be seen but not hit is the oldest bug in window
        decoration.

        A square at the right end of the titlebar, inset by the border so it
        does not sit on the frame. Nothing for a window with no titlebar to
        put it in, nothing for the compositor's own panes -- there is no
        program to ask -- and nothing for a window too narrow to hold both a
        button and a title, below which a window is all button and the strip
        the hand actually drags by has gone.
*/
static _Bool pane_close_box(struct pane *pane, int *x, int *y, int *side)
{
        int box = canvas_title - canvas_border * 2;

        if (!pane_title(pane) || !pane->shared || box <= 0)
                return false;

        if (pane->width < box + canvas_cell_w * 2)
                return false;

        *side = box;
        *x = pane->x + pane->width - box - canvas_border;
        *y = pane->y + canvas_border;
        return true;
}

/*
        The X, as a bitmap, because there is no line to draw one with and a
        letter x is a letter. Two pixels thick so it reads at scale one.
*/
static const unsigned char close_bits[8] = {
    0xc3, 0xe7, 0x7e, 0x3c, 0x3c, 0x7e, 0xe7, 0xc3,
};

// The first format in the plane's own order that is one of the two wanted.
static PURE u32 canvas_plane_pick_format(struct drm_plane *plane, u32 first, u32 second)
{
        unsigned int i;

        for (i = 0; i < plane->format_count; i++)
                if (plane->format_types[i] == first ||
                    plane->format_types[i] == second)
                        return plane->format_types[i];

        return DRM_FORMAT_INVALID;
}

/* ---- paint: pixels, the ink table, the bitmaps and the blits ---- */

/*
        Canvas -- paint

        Pixels, and nothing above them. Everything here takes a pointer, a
        pitch and a rectangle; none of it knows what a window or a screen is.
        That is what makes it worth its own file: it is the only part that can
        be read without the DRM stack in your head.
*/

static const u32 canvas_ink[INK_COUNT] = {
    [INK_DESKTOP] = 0x1b2733,
    [INK_FRAME] = 0x2f3f52,
    [INK_TITLE] = 0x2b3a4c,
    [INK_TITLE_LIT] = 0x4c6785,
    [INK_BODY] = 0x101820,
    [INK_TEXT] = 0xdfe7ef,
    [INK_CURSOR] = 0xffffff,
    [INK_CURSOR_EDGE] = 0x000000,
};

/*
        The two hundred and fifty six an xterm has: the original sixteen, the
        6x6x6 cube, then twenty four greys. A cell carries an index into this
        rather than a colour, so the compositor looks the colour up once.
*/
static u32 canvas_terminal[256];
static _Bool canvas_terminal_ready;

static void canvas_terminal_prepare(void)
{
        if (canvas_terminal_ready)
                return;

        for (unsigned int i = 0; i < 256; i++)
                canvas_terminal[i] = window_cell_colour(i);

        canvas_terminal_ready = true;
}

static void canvas_palette(u32 *palette, u32 format)
{
        canvas_row_blit(palette, canvas_ink, INK_COUNT,
                        format == DRM_FORMAT_ARGB8888 ? 0xff000000 : 0);
}


/*
        A bitmap, into whatever it is given. The set bits of a row are drawn as
        runs rather than one at a time: a call for every lit pixel is thousands
        of calls for a line of text, and a run of a few is what the fill is
        cheapest at.

        A glyph and a cursor are the same picture to this, so there is one walk
        and not two. It lives here rather than beside the glyphs because paint
        is included first and the cursor below is drawn from it.
*/
static void bits_draw(const struct target *t, int x, int y, int scale,
                      const unsigned char *bits, unsigned int pitch,
                      unsigned int w, unsigned int h, u32 colour)
{
        unsigned int row, column;

        if (!w || !h)
                return;

        /*
                Whole eight-pixel tiles share the glyph floor when the bitmap
                is entirely inside the damage. Scale two is the Retina metric
                of the same floor, so a cursor, a title and a close button do
                not go back to scanning bits into rectangles. The destination
                rectangle is what has to fit, not the source.
        */
        if ((scale == 1 || scale == 2) && !(w & 7) &&
            x >= max(t->clip.x1, 0) &&
            (long)x + (long)w * scale <= min(t->clip.x2, t->width) &&
            y >= max(t->clip.y1, 0) &&
            (long)y + (long)h * scale <= min(t->clip.y2, t->height))
        {
                target_mark((unsigned long)h * w * (unsigned long)scale *
                            (unsigned long)scale);
                canvas_runs += w / 8 - 1;
                for (column = 0; column < w; column += 8)
                {
                        u32 *at = t->pixels + (size_t)y * t->pitch + x +
                                  (int)column * scale;
                        const u8 *src = bits + column / 8;

                        if (scale == 1)
                                TARGET_PICK(t, canvas_glyph)(at, t->pitch, src,
                                                             pitch, h, colour);
                        else
                                canvas_glyph2(at, t->pitch, src, pitch, h,
                                              colour);
                }
                return;
        }

        for (row = 0; row < h; row++)
        {
                const unsigned char *line_bits = bits + row * pitch;

                for (column = 0; column < w;)
                {
                        unsigned int run = column;
                        int px, x1, x2, y1, y2;

                        if (!(line_bits[column / 8] & (0x80 >> (column % 8))))
                        {
                                column++;
                                continue;
                        }

                        while (run < w &&
                               (line_bits[run / 8] & (0x80 >> (run % 8))))
                                run++;

                        px = x + (int)column * scale;
                        x1 = max(max(px, t->clip.x1), 0);
                        x2 = min(min(px + (int)(run - column) * scale, t->clip.x2),
                                 t->width);
                        y1 = y + (int)row * scale;
                        y2 = min(min(y1 + scale, t->clip.y2), t->height);
                        y1 = max(max(y1, t->clip.y1), 0);
                        target_rectangle(t, x1, y1, x2 - x1, y2 - y1, colour);

                        column = run;
                }
        }
}


/*
        Cursors.

        One cell for every shape so a shape change is a different bitmap and
        nothing else -- same buffer, same damage, same hardware plane. Each
        word is one sixteen-pixel row, in the byte order bits_draw consumes.

        The hotspot is the pixel the pointer actually is. It is the corner for
        an arrow and the centre for the resize shapes, which is why it is per
        shape rather than one constant.
*/
#define CURSOR_W 16
#define CURSOR_H 20

#define CURSOR_ARROW 0
#define CURSOR_RESIZE_H 1
#define CURSOR_RESIZE_V 2
#define CURSOR_RESIZE_NWSE 3
#define CURSOR_RESIZE_NESW 4
#define CURSOR_SHAPES 5

static const u16 cursor_edge[CURSOR_SHAPES][CURSOR_H] = {
    {0x0080, 0x00c0, 0x00a0, 0x0090, 0x0088, 0x0084, 0x0082, 0x0081, 0x8080, 0x4080,
     0xe083, 0x0092, 0x00a9, 0x00c9, 0x8084, 0x8004, 0x8002, 0x8003, 0x0000, 0x0000},
    {0x0000, 0x0000, 0x0000, 0x2008, 0x3018, 0x2828, 0xe44f, 0x0280, 0xe44f, 0x2828,
     0x3018, 0x2008, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    {0x0001, 0x8002, 0x4004, 0x2008, 0xf01e, 0x8002, 0x8002, 0x8002, 0x8002, 0x8002,
     0xf01e, 0x2008, 0x4004, 0x8002, 0x0001, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    {0x0000, 0x007f, 0x0042, 0x0044, 0x004c, 0x005a, 0x0065, 0x8442, 0x4c01, 0xb400,
     0x6400, 0x4400, 0x8400, 0xfc01, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    {0x0000, 0xfc01, 0x8400, 0x4400, 0x6400, 0xb400, 0x4c01, 0x8442, 0x0065, 0x005a,
     0x004c, 0x0044, 0x0042, 0x007f, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
};

static const u16 cursor_fill[CURSOR_SHAPES][CURSOR_H] = {
    {0x0000, 0x0000, 0x0040, 0x0060, 0x0070, 0x0078, 0x007c, 0x007e, 0x007f, 0x807f,
     0x007c, 0x006c, 0x0046, 0x0006, 0x0003, 0x0003, 0x0001, 0x0000, 0x0000, 0x0000},
    {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x1010, 0x1830, 0xfc7f, 0x1830, 0x1010,
     0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    {0x0000, 0x0001, 0x8003, 0xc007, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001,
     0x0001, 0xc007, 0x8003, 0x0001, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    {0x0000, 0x0000, 0x003c, 0x0038, 0x0030, 0x0024, 0x0002, 0x0001, 0x8000, 0x4800,
     0x1800, 0x3800, 0x7800, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    {0x0000, 0x0000, 0x7800, 0x3800, 0x1800, 0x4800, 0x8000, 0x0001, 0x0002, 0x0024,
     0x0030, 0x0038, 0x003c, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
};

static const int canvas_cursor_hot[CURSOR_SHAPES][2] = {
    {0, 0}, {7, 7}, {7, 7}, {7, 7}, {7, 7},
};

static void cursor_cell(struct drm_rect *rect, int x, int y,
                        unsigned int shape, unsigned int scale)
{
        int hx, hy;

        /*
                A shape nobody drew.

                desktop_resume writes ~0u into every output's cursor_shape to
                mean "whatever is on the plane is stale, paint it again", and
                plane_update works out the cell before it knows whether the
                cursor is being shown -- so a hide, and the plane_drop that a
                failed repaint goes through, read the hotspot of a shape that
                is not one. Unsigned, that is a read tens of gigabytes past a
                five entry table in ring 0. Bounded here because this is the
                one place the table is read.
        */
        if (shape >= CURSOR_SHAPES)
                shape = CURSOR_ARROW;

        hx = canvas_cursor_hot[shape][0] * (int)scale;
        hy = canvas_cursor_hot[shape][1] * (int)scale;

        drm_rect_init(rect, x - hx, y - hy, CURSOR_W * (int)scale,
                      CURSOR_H * (int)scale);
}

/*
        The cursor, into whatever it is given. Two colours, so two passes of
        the same walk a glyph takes: the outline and the fill never share a
        pixel, so which goes down first does not matter. (x, y) is the hotspot.
*/
static HOT void canvas_draw_cursor(const struct target *t, int x, int y,
                                   unsigned int shape, unsigned int scale)
{
        struct drm_rect cell;

        // The same bound, for the same reason: the bitmaps are indexed by it.
        if (shape >= CURSOR_SHAPES)
                shape = CURSOR_ARROW;

        cursor_cell(&cell, x, y, shape, scale);
        bits_draw(t, cell.x1, cell.y1, (int)scale, (const u8 *)cursor_fill[shape],
                  2, CURSOR_W, CURSOR_H, t->ink[INK_CURSOR]);
        bits_draw(t, cell.x1, cell.y1, (int)scale, (const u8 *)cursor_edge[shape],
                  2, CURSOR_W, CURSOR_H, t->ink[INK_CURSOR_EDGE]);
}

// xrgb8888 is the source of truth; argb differs only in the alpha byte.

/*
        How far a row of a rounded rectangle is inset from its edge. Zero
        everywhere except within radius of the top and bottom, where it follows
        the circle those corners are quarters of.
*/
static CONST int round_inset(int row, int height, int radius)
{
        int dy;

        if (radius <= 0)
                return 0;

        if (row < radius)
                dy = radius - 1 - row;
        else if (row >= height - radius)
                dy = radius - (height - row);
        else
                return 0;

        return radius - (int)int_sqrt((unsigned long)(radius * radius - dy * dy));
}

/* ---- text: the font, and where one line of it ends ---- */

/*
        Canvas -- text

        Text is the compositor's to draw, not each program's. A program says
        what the words are and what box they go in; where the lines break and
        where they sit inside it is decided here, once, for everything on the
        machine.

        The face is one the kernel already carries for its own console, so
        there is no font file to ship and no parser to write. It is a bitmap,
        so a size is an integer scale of it rather than a point value.
*/

static const struct font_desc *canvas_font;

static PURE const unsigned char *glyph_bits(unsigned int character)
{
        return font_data_buf(canvas_font->data) +
               (size_t)character *
                   font_glyph_size(canvas_font->width, canvas_font->height);
}

// Whether the face is the one canvas_cell assumes: eight wide, a byte a row,
// and a cell tall.
static PURE _Bool glyph_is_cell(void)
{
        return canvas_font && canvas_font->width == WINDOW_CELL_W &&
               canvas_font->height == WINDOW_CELL_H &&
               font_glyph_pitch(canvas_font->width) == 1;
}

// One glyph, which is a bitmap like any other: bits_draw above is the
// walk, and the face says how wide and how tall.
static void glyph_draw(const struct target *t, int x, int y, int scale,
                       unsigned char character, u32 colour)
{
        bits_draw(t, x, y, scale, glyph_bits(character),
                  font_glyph_pitch(canvas_font->width), canvas_font->width,
                  canvas_font->height, colour);
}

/*
        Where one line ends.

        Greedy: take as many characters as fit, and if that lands mid word back
        up to the last space. A single word longer than the box breaks where it
        runs out of room, because the alternative is drawing off the stop.
*/
static PURE unsigned int text_line_end(const char *text, unsigned int length,
                                       unsigned int start, unsigned int columns, _Bool wrap)
{
        unsigned int remaining;
        unsigned int scanned;
        const char *found;

        if (start >= length)
                return length;

        remaining = length - start;
        scanned = remaining;

        // Include the breaking character: a newline exactly at the column
        // boundary takes precedence over wrapping at the last space.
        if (wrap && remaining > columns)
                scanned = columns + 1;

        found = memory_first_of((address_any)(text + start), '\n', scanned);

        if (found)
                return (unsigned int)(found - text);

        if (!wrap || remaining <= columns)
                return length;

        found = memory_last_of((address_any)(text + start), ' ', scanned);

        return found && found > text + start ? (unsigned int)(found - text)
                                            : start + columns;
}

// Past the break, and past the character the break was made on.
static PURE unsigned int text_line_next(const char *text, unsigned int length, unsigned int stop)
{
        if (stop < length && (text[stop] == '\n' || text[stop] == ' '))
                return stop + 1;

        return stop;
}

static PURE unsigned int text_line_count(const char *text, unsigned int length,
                                         unsigned int columns, _Bool wrap)
{
        unsigned int start = 0, lines = 0;

        while (start < length)
        {
                unsigned int stop = text_line_end(text, length, start, columns, wrap);

                lines++;
                start = text_line_next(text, length, stop);
        }

        return lines;
}

/*
        Lays a string out in a box and draws it. Coordinates are the output's,
        the clip is the damage, and nothing is drawn outside either.
*/
static void text_draw(const struct target *t, int x, int y, int w, int h,
                      const char *text, unsigned int length,
                      unsigned int align, int scale, u32 colour)
{
        u64 started;
        struct target box;
        int cell_w, cell_h;
        _Bool wrap = align & TEXT_WRAP;
        unsigned int columns, start = 0;
        int line_y;

        if (!canvas_font || !length)
                return;

        /*
                The box is a clip like the damage is. A caller asks for a title
                in a titlebar and hands over whatever string the window has;
                one newline in it and the second line lands on the contents
                below, so the box is met at the same place the damage is rather
                than trusted to the layout above.
        */
        box = *t;
        if (!drm_rect_intersect(&box.clip, &(struct drm_rect){ x, y, x + w, y + h }))
                return;

        cell_w = (int)canvas_font->width * scale;
        cell_h = (int)canvas_font->height * scale;
        columns = cell_w > 0 && w > 0 ? (unsigned int)(w / cell_w) : 0;

        if (!columns)
                return;

        started = ktime_get_ns();

        if (!wrap)
                columns = length;

        unsigned int vertical = align & (TEXT_MIDDLE | TEXT_BOTTOM);
        line_y = y;
        if (vertical == TEXT_MIDDLE || vertical == TEXT_BOTTOM)
        {
                int space = h - (int)text_line_count(text, length, columns, wrap) * cell_h;
                line_y += vertical == TEXT_MIDDLE ? space / 2 : space;
        }

        while (start < length && line_y < y + h)
        {
                unsigned int stop = text_line_end(text, length, start, columns, wrap);
                int run = (int)(stop - start);
                int line_x = x;
                int i;

                // A line the damage cannot reach still has to be measured, so
                // the ones after it start in the right place, but nothing in
                // it needs looking at glyph by glyph. Repainting under a
                // cursor is twenty rows of a paragraph that is a hundred.
                if (line_y + cell_h > box.clip.y1 && line_y < box.clip.y2)
                {
                        switch (align & (TEXT_CENTRE | TEXT_RIGHT))
                        {
                        case TEXT_CENTRE:
                                line_x = x + (w - run * cell_w) / 2;
                                break;
                        case TEXT_RIGHT:
                                line_x = x + w - run * cell_w;
                                break;
                        }

                        for (i = 0; i < run; i++)
                        {
                                int px = line_x + i * cell_w;

                                if (px + cell_w > box.clip.x1 && px < box.clip.x2)
                                        glyph_draw(&box, px, line_y, scale,
                                                   (unsigned char)text[start + i], colour);
                        }
                }

                line_y += cell_h;
                start = text_line_next(text, length, stop);
        }

        canvas_text_ns += ktime_get_ns() - started;
}

/* ---- pane: a pane, the size a window of cells is given, and the grid ---- */

/*
        Canvas -- windows

        One file per window. The ioctl sizes it, the mapping is at offset zero,
        and closing the file destroys it, so there is no identifier for either
        side to get wrong and no lifetime to track separately from the fd.

        The shared page is writable by the program at any instant, so nothing
        below composes from it. pane_refresh takes a copy and checks it; the
        buffer's extent is the compositor's and is never read back from the
        page at all.

        A window of cells is a ring of lines. The compositor allocates it and
        both sides address it the same way, so the scrollback, the wheel and
        the bar down the side are here once rather than once per window that
        wanted them.
*/

/*
        How many lines a window of cells keeps.

        Enough to hold a boot, and never fewer than a couple of screens of
        whatever the display can show, because the arithmetic below counts
        backwards from the newest line and must not run past the oldest.
*/
#define PANE_HISTORY 512

// The view follows the end rather than sitting on a line.
#define PANE_LIVE ((unsigned int)-1)

static PURE struct output *output_by_index(unsigned int index)
{
        struct output *output;
        unsigned int i = 0;

        list_for_each_entry(output, &desktop.outputs, link)
                if (i++ == index)
                        return output;

        return list_first_entry_or_null(&desktop.outputs, struct output, link);
}

/*
        The first step down the cascade that no other window is sitting on.

        Bounded, and not only because an unbounded search would be a loop over
        the window list per step: eight titlebars down is already most of the
        way across a small screen, and past that the honest answer is to start
        again at the centre and let them overlap. A step that would put the
        window off the bottom or the right of its own output stops the search
        for the same reason -- a window placed where it cannot be seen is
        worse than one placed on top of another.
*/
static PURE int pane_cascade_free(struct pane *pane, struct output *output,
                                  int title)
{
        int step = canvas_title;
        int at;

        for (at = 0; at < 8; at++)
        {
                struct pane *other;
                int x = pane->x + at * step;
                int y = pane->y + at * step;
                _Bool taken = false;

                if (x + pane->width > output->x + (int)output->width ||
                    y + pane->height + title > output->y + (int)output->height)
                        break;

                list_for_each_entry(other, &desktop.windows, link)
                        if (other != pane && other->x == x && other->y == y)
                        {
                                taken = true;
                                break;
                        }

                if (!taken)
                        return at;
        }

        return 0;
}

// Where a region or a style puts a window. Free floating and not fullscreen
// is the program's own x and y.
static void pane_place(struct pane *pane)
{
        struct output *output = output_by_index(pane->display);
        int title;

        if (!output)
                return;

        if (pane->shared)
        {
                WRITE_ONCE(pane->shared->display_width, output->width);
                WRITE_ONCE(pane->shared->display_height, output->height);
        }

        title = pane_title(pane);

        if (pane->style & WINDOW_FULLSCREEN)
        {
                pane->x = output->x;
                pane->y = output->y;
                return;
        }

        if (pane->region != WINDOW_CENTRED)
                return;

        pane->x = output->x + ((int)output->width - pane->width) / 2;
        pane->y = output->y + ((int)output->height - (pane->height + title)) / 2;

        /*
                Down and across, when the centre is already taken.

                Every terminal asks to be centred, so a second one landed on
                the first exactly -- same size, same place, pixel for pixel --
                and opening one looked like nothing had happened at all. The
                window was there, with another behind it.

                Worked out once, the first time this window is placed, and
                kept: it is the window's position in a stack, not a function
                of where the other windows are this instant, or closing the
                one underneath would slide this one up the screen.
        */
        if (!pane->cascaded)
        {
                pane->cascaded = true;
                pane->cascade = pane_cascade_free(pane, output, title);
        }

        pane->x += pane->cascade * canvas_title;
        pane->y += pane->cascade * canvas_title;
}

/*
        What a style says the size should be, before the grid rounds it.

        Fullscreen used to move a window to the corner of its output and leave
        it whatever size it was, which is not what the flag says. A window of
        cells grows to the whole screen; one with a buffer covers as much of it
        as the buffer it asked for will reach.
*/
static void pane_size(struct pane *pane)
{
        struct output *output = output_by_index(pane->display);
        int title;

        if (!output || !(pane->style & WINDOW_FULLSCREEN))
                return;

        title = pane_title(pane);

        pane->width = (int)min(output->width, pane->max_width);
        pane->height = (int)min(output->height - min(output->height, (unsigned int)title),
                                pane->max_height);
}

static void pane_raise(struct pane *raised)
{
        int top = 0;

        if (!list_empty(&desktop.windows))
                top = max(top, list_last_entry(&desktop.windows,
                                                struct pane, link)->z);

        raised->z = top + 1;

        if (raised->shared)
                WRITE_ONCE(raised->shared->z, raised->z);
}

static PURE int pane_by_z(void *unused, const struct list_head *a, const struct list_head *b)
{
        int za = list_entry(a, struct pane, link)->z;
        int zb = list_entry(b, struct pane, link)->z;

        return za < zb ? -1 : za > zb;
}

/*
        What a program's window says, when it is made and when it goes.

        Anyone who can open /dev/spark can make a window and close it as fast
        as the calls go, so a line said for each is a way for a program with
        no privilege to write the kernel log: the ring holds a few hundred
        of them at a hundred bytes, and everything logged before the loop
        started -- a floodlight change, a warning, an oops's first lines --
        is gone from it by the time anybody reads. Rate limited, then, the
        way the loader's own refusals are. The compositor's own console
        window is made once, by the kernel, and says its line plainly.
*/
#define pane_say(...) pr_info_ratelimited("[moonwater canvas] " __VA_ARGS__)

/*
        How much of the machine every window may hold between them.

        There is no cap on how many windows there are and there must not be,
        but /dev/spark is open to anything, so without a ceiling on the total
        one program can ask until there are no pages left. A quarter of memory,
        which no honest desktop comes near.
*/
static unsigned long canvas_pane_bytes;

static unsigned long canvas_pane_budget(void)
{
        return (totalram_pages() / 4) * PAGE_SIZE;
}

/*
        A program's ring, held a page at a time.

        A ring is cut for the largest the window could ever become (see
        pane_ring), so a terminal eighty columns wide sits in lines four
        hundred and seventy eight wide, five hundred and twelve of them: about
        two megabytes, of which an idle prompt had written eight pages. Every
        page of that was allocated, zeroed and kept for the window's life.

        So a program's window of cells is only reserved. The page the program
        reads its geometry from and the page the line lengths are in are
        there from the start, because the compositor writes the one and reads
        the other on every compose; a page of cells gets memory the first time
        the program touches it, from window_fault, and in the same step is
        mapped at its place in the compositor's reservation. Until then that
        place maps nothing, and pane_cells_held is what keeps compose off it.

        A page once held stays held until the window closes: a ring that has
        wrapped holds all of it, which is what every window paid before.

        Not in a module: the compositor's reservation is a kernel mapping this
        edits, and the calls that reserve one and flush it are the kernel's
        own. A modular build keeps the whole ring, as it always did.
*/
#define PANE_LAZY IS_BUILTIN(CONFIG_MOONWATER_CORE)

static int pane_pte_set(pte_t *pte, unsigned long address, void *data)
{
        set_pte_at(&init_mm, address, pte, mk_pte((struct page *)data, PAGE_KERNEL));
        return 0;
}

/*
        One page of a held ring, given memory if it has none yet. Under
        pages_lock, or before anything else can see the pane. The page is
        zeroed and mapped before the entry says so: whoever reads the entry
        without the lock may go straight to the mapping.
*/
static struct page *pane_page_hold(struct pane *pane, unsigned long index)
{
        struct page *page = pane->pages[index];
        unsigned long address;

        if (page)
                return page;

        /*
                Charged to whoever touched the page. /dev/spark is open to
                everyone and a window's ring is memory a program pays for a
                page at a time by writing to it: from a control group with
                a limit, a loop of first touches was kernel memory that no
                limit saw, up to the quarter of the machine the desktop
                allows all windows between them.
        */
        page = alloc_page(GFP_KERNEL_ACCOUNT | __GFP_ZERO);
        if (!page)
                return NULL;

        address = (unsigned long)pane->mapping + (index << PAGE_SHIFT);
        if (apply_to_page_range(&init_mm, address, PAGE_SIZE, pane_pte_set, page))
        {
                __free_page(page);
                return NULL;
        }

        // Nothing to flush where an empty entry is never cached; the
        // architectures that may cache one flush here.
        flush_cache_vmap(address, address + PAGE_SIZE);
        smp_store_release(&pane->pages[index], page);
        return page;
}

// Every page from first up to, not including, last.
static _Bool pane_pages_hold(struct pane *pane, unsigned long first,
                             unsigned long last)
{
        for (; first < last; first++)
                if (!pane_page_hold(pane, first))
                        return false;

        return true;
}

/*
        Whether a line of a ring has memory behind every cell compose would
        read of it. A program that wrote a length without the cells has
        written nothing to draw, and its line is drawn empty.
*/
static _Bool pane_cells_held(struct pane *pane, unsigned int slot,
                             unsigned int length)
{
        unsigned long first, last;

        if (!pane->pages || !length)
                return true;

        first = WINDOW_PIXELS + (unsigned long)slot * pane->stride *
                                    sizeof(struct window_cell);
        last = (first + (unsigned long)length * sizeof(struct window_cell) - 1) >>
               PAGE_SHIFT;

        for (first >>= PAGE_SHIFT; first <= last; first++)
                if (!smp_load_acquire(&pane->pages[first]))
                        return false;

        return true;
}

static void pane_mapping_free(struct pane *pane)
{
        unsigned long i, held = 0;

        if (!pane->pages)
        {
                vfree(pane->mapping);
                return;
        }

        for (i = 0; i < pane->page_count; i++)
                held += pane->pages[i] != NULL;

        // What a window of cells cost by the end, against what it reserved:
        // the canvas lane reads this back for the terminal it closes.
        if (pane->area)
                pane_say("window closed, ring held %lu of %lu KiB\n",
                         (held << PAGE_SHIFT) >> 10, (pane->page_count << PAGE_SHIFT) >> 10);

        // Unmapped before the pages go back, as vfree does.
        if (pane->area)
                free_vm_area(pane->area);

        for (i = 0; i < pane->page_count; i++)
                if (pane->pages[i])
                        __free_page(pane->pages[i]);

        kvfree(pane->pages);
}

/*
        A reservation for a program's ring, with the pages below head_end and
        from tail_start on held. Null when there is no room, with nothing left
        behind.
*/
static void *pane_mapping_reserve(struct pane *pane, unsigned long bytes,
                                  unsigned long head_end, unsigned long tail_start)
{
        pane->page_count = bytes >> PAGE_SHIFT;
        pane->pages = kvcalloc(pane->page_count, sizeof(*pane->pages),
                               GFP_KERNEL_ACCOUNT);
        if (!pane->pages)
                return NULL;

        mutex_init(&pane->pages_lock);
        pane->area = get_vm_area(bytes, VM_MAP);
        if (pane->area)
        {
                pane->mapping = pane->area->addr;

                if (pane_pages_hold(pane, 0, PAGE_ALIGN(head_end) >> PAGE_SHIFT) &&
                    pane_pages_hold(pane, tail_start >> PAGE_SHIFT, pane->page_count))
                        return pane->mapping;
        }

        pane_mapping_free(pane);
        pane->pages = NULL;
        pane->area = NULL;
        pane->mapping = NULL;
        return NULL;
}

/*
        A pane going away, and everything that was still pointing at it.

        The desktop remembers a particular window between events -- what a
        hand has hold of, what it last pressed, where the keys go -- and every
        one of those outlives the window unless it is told otherwise. This was
        written at window_release's call site, which left the other caller
        freeing a pane the desktop could still be holding: console_stop runs
        on the way out of the module, and a console that had been clicked was
        freed with desktop.focused still naming it.

        Here rather than there, so that freeing a pane is the whole of
        forgetting one and a third caller cannot be written that forgets to.
        Who gets the keys instead is the caller's: window_release hands them
        to the window underneath, and nothing is left to hand them to when
        the module is going away.
*/
static void pane_free(struct pane *pane)
{
        struct pane **held[] = {
                &desktop.dragging, &desktop.resizing, &desktop.barring,
                &desktop.press_pane, &desktop.focused,
        };
        unsigned int i;

        for (i = 0; i < ARRAY_SIZE(held); i++)
                if (*held[i] == pane)
                        *held[i] = NULL;

        list_del(&pane->link);
        canvas_pane_bytes -= pane->bytes;
        pane_mapping_free(pane);
        kfree(pane);
}

/*
        How many cells the desktop has room for, the frame counted.

        A window wears a titlebar and a border, so a grid measured against the
        bare screen is a window larger than the screen it has to fit on. At a
        scale of one the terminal never asked for enough cells to notice; at
        two it did, and came out taller than the display, centred to a negative
        y with its titlebar off the top of it.
*/
static void desktop_grid(int width, int height,
                          unsigned int *columns, unsigned int *rows)
{
        width -= canvas_border * 2 + canvas_bar;
        height -= canvas_title + canvas_border * 3;

        *columns = (unsigned int)(max(width, 0) / canvas_cell_w);
        *rows = (unsigned int)(max(height, 0) / canvas_cell_h);
}

/*
        The grid a window is BUILT for, which is not the grid the desktop
        happens to have when it is built.

        A window of cells keeps its lines in a ring whose stride is fixed for
        the window's life, because a program cannot be handed a larger
        mapping than the one it already mapped. Sizing that ring from the
        desktop of the moment therefore freezes the window's largest possible
        size at whatever the screen was when it opened -- and the screen is
        not a constant. desktop.width is the sum of the outputs, recomputed
        every time a connector is probed, a mode is chosen, or a monitor
        arrives, and on real hardware those happen after the first terminal
        already exists. The terminal that opened first then refuses to grow
        past a screen nobody has any more, which is a bug that comes and goes
        with the boot's timing and never appears under an emulator whose
        display is ready before anything asks.

        So the ring is built for a ceiling instead: a size no ordinary display
        exceeds, taken as the larger of that and the desktop as it stands, so
        a screen bigger than the ceiling is still served. The window's visible
        size is clamped to the output it sits on, exactly as before -- this
        changes only what it is ALLOWED to become, never what it is.

        The cost is the difference between a ring cut for the screen and one
        cut for the ceiling. At the eight by sixteen cell that is about two
        megabytes for a terminal against one, inside a budget that is a
        quarter of memory.
*/
#define PANE_CEILING_W 3840
#define PANE_CEILING_H 2160

/*
        How long a ring is, and how far apart its lines are.

        A line is as wide as the desktop could ever make this window, so the
        stride never changes and a resize moves nothing. The history is at
        least two screens of the tallest window there could be, because
        everything below counts backwards from the newest line.
*/
/*
        How wide the compositor's own console is cut: its window's columns, a
        step at a time so a drag that widens it recuts it a few times rather
        than once a column, and never wider than any window may be.
*/
#define CONSOLE_STRIDE_STEP 32u

static unsigned int console_stride(unsigned int columns, unsigned int max_columns)
{
        return min(round_up(max(columns, 1u), CONSOLE_STRIDE_STEP), max_columns);
}

static void pane_ring(unsigned int max_columns, unsigned int max_rows,
                      unsigned int *stride, unsigned int *history,
                      unsigned long *bytes)
{
        *stride = max_columns;
        *history = max_t(unsigned int, PANE_HISTORY, max_rows * 2 + 1);
        *bytes = (unsigned long)*history *
                 ((unsigned long)*stride * sizeof(struct window_cell) +
                  sizeof(unsigned int));
}

// One line of the ring, and how much of it was written. Any index: it is
// taken modulo the history, which is what makes head a count and not a cursor.
//
// Read once, and that is the whole point of the local. pane->lengths points
// into pane->mapping, which window_mmap hands to the program through
// remap_vmalloc_range, so every one of these words is writable by the program
// while this runs and reading one twice can answer twice. Clamping a value the
// compiler is free to fetch again clamps the first fetch and draws with the
// second: the bound and the use have to be the same load, or the bound is not
// one. Nothing in the source said so before, which left it a property of what
// GCC happened to emit rather than of what is written here.
static unsigned int pane_length(struct pane *pane, unsigned int index)
{
        unsigned int written = READ_ONCE(pane->lengths[index % pane->history]);

        return min(written, pane->stride);
}

/*
        How many rows are actually drawn.

        The room the window has now and the shape whoever owns the cells last
        laid them out in are the same at rest and not during a resize.
*/
static PURE unsigned int pane_rows(struct pane *pane)
{
        return max(min(pane->grid_rows, pane->rows), 1u);
}

/*
        The oldest line still held.

        Counting back from head rather than from a line number that was kept,
        so a ring that has wrapped stops at the oldest line it still has
        instead of scrolling into ones it gave away. Never fewer than the rows
        on the screen, so a window taller than what has been written to it
        still has the newest line at the bottom of it.
*/
static PURE unsigned int pane_oldest(struct pane *pane)
{
        unsigned int filled = clamp(pane->head - pane->history, pane_rows(pane),
                                    pane->history);

        return pane->head - filled;
}

/*
        How many rows a line takes at the width it is being drawn at.

        This is the whole of the wrapping. A line is stored as long as it was
        written and folded when it is drawn, so a window made wider re-wraps
        everything in it -- including what has already scrolled past -- without
        anything being moved or rewritten.
*/
static PURE unsigned int pane_line_rows(struct pane *pane, unsigned int index)
{
        unsigned int width = max(pane->grid_columns, 1u);
        unsigned int length = pane_length(pane, index);

        return length ? (length + width - 1) / width : 1;
}

/*
        The line the top row of the window is on, and how many rows of that
        line are above the window.

        Following the end means counting rows backwards from the newest line
        until the window is full, which can stop in the middle of a line that
        is too long to fit -- so the top row of the screen is a fold of one,
        the way the bottom of a terminal has always been.
*/
static unsigned int pane_view_at(struct pane *pane, unsigned int view,
                                 unsigned int *skip)
{
        unsigned int oldest = pane_oldest(pane);
        unsigned int rows = pane_rows(pane);
        unsigned int at = pane->head - 1;
        unsigned int count = 0;

        *skip = 0;

        if (view != PANE_LIVE)
        {
                at = clamp(view, oldest, pane->head - 1);
                *skip = min(pane->view_skip, pane_line_rows(pane, at) - 1);
                return at;
        }

        while (count < rows && at > oldest)
        {
                count += pane_line_rows(pane, at);

                if (count >= rows)
                        break;

                at--;
        }

        if (count < rows)
                count += pane_line_rows(pane, at);

        *skip = count > rows ? count - rows : 0;

        return at;
}

/*
        Where the view sits in what there is, for something to draw a bar with.

        In rows rather than pixels, and rows rather than lines: what the bar is
        made of is the drawing code's business, and a bar measured in lines is
        one that jumps whenever a line folds. False when there is nothing to
        scroll, which is also when a bar would say nothing worth the pixels.
*/
static _Bool pane_extent(struct pane *pane, unsigned int *first,
                         unsigned int *shown, unsigned int *total)
{
        unsigned int skip, top, at;

        if (!pane->cells)
                return false;

        top = pane_view_at(pane, pane->view, &skip);

        *shown = pane_rows(pane);
        *total = 0;
        *first = 0;

        for (at = pane_oldest(pane); at != pane->head; at++)
        {
                if (at == top)
                        *first = *total + skip;

                *total += pane_line_rows(pane, at);
        }

        return *total > *shown;
}

/*
        Put the view where a count of rows says, rather than a number of lines.

        The bar is measured in drawn rows, because that is what it is a picture
        of: a line too long for the window is several of them. So the walk is
        the same one pane_extent does, stopping at the line the row asked for.
*/
static _Bool pane_view_set(struct pane *pane, unsigned int above)
{
        unsigned int was = pane->view;
        unsigned int was_skip = pane->view_skip;
        unsigned int at, run = 0, live, live_skip, skip = 0;

        if (!pane->cells)
                return false;

        live = pane_view_at(pane, PANE_LIVE, &live_skip);

        for (at = pane_oldest(pane); at != pane->head; at++)
        {
                unsigned int past = run + pane_line_rows(pane, at);

                if (above < past)
                {
                        skip = above - run;
                        break;
                }

                run = past;
        }

        if (at > live || (at == live && skip >= live_skip))
        {
                pane->view = PANE_LIVE;
                pane->view_skip = 0;
        }
        else
        {
                pane->view = at;
                pane->view_skip = skip;
        }

        if (pane->view == was && pane->view_skip == was_skip)
                return false;

        pane->view_moved = true;

        return true;
}

/*
        The wheel, in lines. Positive is away from the hand, which is back
        through what has already been said.

        Back at the end is following again rather than sitting on the last
        line: what arrives next should appear.
*/
static _Bool pane_scroll(struct pane *pane, int lines)
{
        unsigned int above, shown, total, live;
        long target;

        if (!pane->cells)
                return false;

        pane_extent(pane, &above, &shown, &total);
        live = total > shown ? total - shown : 0;
        target = (long)above - lines;

        if (target < 0)
                target = 0;
        else if ((unsigned long)target > live)
                target = live;

        return pane_view_set(pane, (unsigned int)target);
}

/*
        Back to following the end, wherever the view had been left.

        Separate from pane_scroll because it is not a distance: a keystroke
        does not move the view by so many lines, it says the end is where the
        interesting part is again. Answers whether anything moved, so a window
        already at the end costs no frame.
*/
static _Bool pane_view_live(struct pane *pane)
{
        if (!pane->cells || pane->view == PANE_LIVE)
                return false;

        pane->view = PANE_LIVE;
        pane->view_skip = 0;
        pane->view_moved = true;

        return true;
}

/*
        The view is never later in the ring than following the end would be.

        pane_view_set refuses to place the view past the last screenful, so
        that there is always a full window of lines below it. What it cannot
        do is keep that true afterwards: the rule is about where the end is,
        and the end moves. A window made taller shows more rows, and a window
        made wider folds its lines into fewer -- both pull the top of the last
        screenful EARLIER in the ring, and a view left where it was is then
        past it. compose_cells draws what there is and fills the rest with
        blank rows, so a scrolled-back window dragged taller grew a band of
        empty rows under its last line and kept them until the wheel moved.

        The same test pane_view_set makes, made again against the size the
        window is now.
*/
static _Bool pane_view_clamp(struct pane *pane)
{
        unsigned int live, live_skip;

        if (!pane->cells || pane->view == PANE_LIVE)
                return false;

        live = pane_view_at(pane, PANE_LIVE, &live_skip);

        if (pane->view < live ||
            (pane->view == live && pane->view_skip < live_skip))
                return false;

        return pane_view_live(pane);
}

/*
        A window, of pixels or of cells.

        An owned one is the compositor's own: nothing maps it, so there is no
        shared page and none of what a program would have read from one is
        written. Everything that reads a page checks for one first, so an owned
        window is placed, moved and composed like any other.
*/
static COLD struct pane *pane_create(unsigned int width, unsigned int height,
                                     unsigned int columns, unsigned int rows,
                                     _Bool owned)
{
        unsigned int max_columns, max_rows;
        unsigned int fit_columns, fit_rows;
        unsigned int stride = 0, history = 0, cut = 0;
        unsigned long ring_bytes = 0;
        struct window *page;
        struct pane *pane;
        unsigned long bytes;

        // What it may be built for, and what there is room to show right now.
        desktop_grid(max(desktop.width, PANE_CEILING_W),
                     max(desktop.height, PANE_CEILING_H), &max_columns, &max_rows);
        desktop_grid(desktop.width, desktop.height, &fit_columns, &fit_rows);

        // A screen with no room for one cell has no room for a window of
        // cells, whatever the ring could hold.
        if (columns && (!fit_columns || !fit_rows))
                return NULL;

        /*
                A window of cells is allocated for as many as the desktop
                could hold, not for as many as it asked for, because its size
                is its grid: resizing it changes how many cells there are, and
                a program cannot be handed a larger mapping than the one it
                already has.
        */
        if (columns)
        {
                // The compositor's own window is cut for what it opens as,
                // and console_recut cuts it again when it grows: nothing
                // else has it mapped, so nothing else has to be told.
                pane_ring(owned ? console_stride(min(columns, fit_columns), max_columns)
                                : max_columns,
                          max_rows, &stride, &history, &ring_bytes);

                // A ring held as it is written is laid out for the ceiling
                // but its lines start as far apart as the window is wide,
                // and pane_restride spreads them when the program asks.
                cut = PANE_LAZY && !owned
                          ? console_stride(min(columns, fit_columns), max_columns)
                          : stride;

                // The ring is cut for the ceiling; what opens is what fits.
                columns = min(columns, fit_columns);
                rows = min(rows, fit_rows);
                width = columns * (unsigned int)canvas_cell_w + canvas_bar;
                height = rows * (unsigned int)canvas_cell_h;
        }

        // A window is allowed to be as large as the desktop and no larger.
        // Not the kind of ceiling the rest of this refuses: it is what stops
        // one program asking for every page in the machine.
        if (!width || !height ||
            width > (unsigned int)desktop.width || height > (unsigned int)desktop.height)
                return NULL;

        if (!columns && height &&
            (unsigned long)width > ((unsigned long)-1 - WINDOW_PIXELS) / 4 / height)
                return NULL;

        bytes = PAGE_ALIGN(WINDOW_PIXELS +
                           (columns ? ring_bytes : (unsigned long)width * height * 4));

        if (canvas_pane_bytes + bytes > canvas_pane_budget())
                return NULL;

        pane = kzalloc(sizeof(*pane), GFP_KERNEL_ACCOUNT);
        if (!pane)
                return NULL;

        init_waitqueue_head(&pane->wait);

        /*
                A program's cells are held as they are written; the page it
                reads its geometry from and the line lengths at the end are
                there from the start. Not vmalloc_user when it is the
                compositor's own: nothing maps it. The compositor's own
                console is written from here, by the emulator, and from a
                dying kernel that cannot wait for a page, so it keeps all of
                its ring; so does a window of pixels, which is drawn whole.
        */
        if (PANE_LAZY && columns && !owned)
                pane_mapping_reserve(pane, bytes, WINDOW_PIXELS,
                                     WINDOW_PIXELS + (unsigned long)history *
                                                         stride *
                                                         sizeof(struct window_cell));
        else
                pane->mapping = owned ? vzalloc(bytes) : vmalloc_user(bytes);

        if (!pane->mapping)
        {
                kfree(pane);
                return NULL;
        }

        canvas_pane_bytes += bytes;
        pane->bytes = bytes;
        pane->pitch = width;
        page = pane->mapping;

        // The page is written either way -- the compositor's own window reads
        // its geometry out of one too. What an owned window has not got is a
        // program behind it, and pane->shared is what says so.
        if (!owned)
                pane->shared = pane->mapping;

        if (columns)
        {
                unsigned long lines = WINDOW_PIXELS + (unsigned long)history *
                                                          stride *
                                                          sizeof(struct window_cell);

                pane->cells = pane->mapping + WINDOW_PIXELS;
                pane->lengths = pane->mapping + lines;
                pane->stride = cut;
                pane->history = history;
                pane->head = history + rows;
                pane->view = PANE_LIVE;
                pane->columns = columns;
                pane->rows = rows;
                pane->max_columns = max_columns;
                pane->max_rows = max_rows;
                pane->max_width = max_columns * (unsigned int)canvas_cell_w + canvas_bar;
                pane->max_height = max_rows * (unsigned int)canvas_cell_h;
                pane->grid_columns = columns;
                pane->grid_rows = rows;
                /*
                        Say what was allocated, not only the tallest visible
                        grid.  history is deliberately at least PANE_HISTORY,
                        so logging max_rows here understated each boot pane by
                        almost four times and hid about two MiB of live RAM.
                */
                if (owned)
                        pr_info("[moonwater canvas] " "kernel log grid %ux%u, ring cut to %ux%u (%lu KiB), recut as it grows\n", columns, rows, stride, history, bytes >> 10);
                else
                        pane_say("window grid %ux%u, ring holds %ux%u (%lu KiB), lines %u apart\n", columns, rows, stride, history, bytes >> 10, cut);

                page->max_columns = max_columns;
                page->max_rows = max_rows;
                page->columns = columns;
                page->rows = rows;
                page->grid_columns = columns;
                page->grid_rows = rows;
                page->stride = cut;
                page->history = history;
                page->head = pane->head;
                page->lines = (unsigned int)lines;
        }
        else
        {
                pane->pixels = pane->mapping + WINDOW_PIXELS;

                // A window of pixels can never outgrow the buffer it asked
                // for. A window of cells has room for the whole desktop, and
                // setting this from the requested size here is what clamped
                // every attempt to resize one back to the size it started at.
                pane->max_width = width;
                pane->max_height = height;
        }

        pane->width = (int)width;
        pane->height = (int)height;
        pane->x = 80;
        pane->y = 80;
        pane->style = WINDOW_FRAME;

        pane_raise(pane);
        list_add_tail(&pane->link, &desktop.windows);

        page->style = WINDOW_FRAME;
        page->x = pane->x;
        page->y = pane->y;
        page->z = pane->z;
        page->width = width;
        page->height = height;
        page->pitch = pane->pitch;
        page->max_width = (unsigned int)pane->max_width;
        page->max_height = (unsigned int)pane->max_height;
        page->mapping = (unsigned int)bytes;

        return pane;
}

/*
        A grid follows the window it is in.

        The size of a window of cells is a whole number of them, so a resize
        is rounded down to one and the program is told how many it now has --
        it is the program that has to lay its text out again.
*/
/*
        The size a window of cells will actually be given, which is a whole
        number of them and never more than the ring was cut for.

        Separate from pane_regrid because a resize has to know the answer
        before it can decide where to put the window. The edge a hand is not
        holding stays where it was only if the rounding is taken off the edge
        it IS holding, and that cannot be arranged after the fact -- so the
        one rule lives here and both of them ask it.
*/
static void pane_grid_fit(struct pane *pane, int *width, int *height)
{
        unsigned int columns, rows;

        // A window of pixels is whatever size it was asked for; it has no
        // cells to be a whole number of, and no ceiling but its own buffer.
        if (!pane->cells)
                return;

        columns =
            min((unsigned int)(max(*width - canvas_bar, 0) / canvas_cell_w),
                pane->max_columns);
        rows = min((unsigned int)(*height / canvas_cell_h), pane->max_rows);

        *width = (int)max(columns, 1u) * canvas_cell_w + canvas_bar;
        *height = (int)max(rows, 1u) * canvas_cell_h;
}

static void pane_regrid(struct pane *pane)
{
        if (!pane->cells)
                return;

        pane_grid_fit(pane, &pane->width, &pane->height);

        // Exact, because the fit is what made these a whole number of cells.
        pane->columns = (unsigned int)(pane->width - canvas_bar) / canvas_cell_w;
        pane->rows = (unsigned int)pane->height / canvas_cell_h;

        // Against the shape just worked out, not the one it replaced: whether
        // the view still has a full window of lines below it is a question
        // about the rows there are now.
        pane_view_clamp(pane);

        /*
                A resize reaches here from drag below without going through
                pane_refresh, so a pane the compositor owns has no page to be
                told and nobody to tell. Its cells are the compositor's, and
                the shape they are drawn in is this one -- which is what makes
                a window it owns grow into the room it was given instead of
                leaving the desktop showing through the part nothing wrote.
        */
        if (!pane->shared)
        {
                pane->grid_columns = pane->columns;
                pane->grid_rows = pane->rows;
                pane->damage_row = 0;
                pane->damage_rows = pane->rows;
                console_regrid(pane);
                return;
        }

        WRITE_ONCE(pane->shared->columns, pane->columns);
        WRITE_ONCE(pane->shared->rows, pane->rows);
        WRITE_ONCE(pane->shared->width, (unsigned int)pane->width);
        WRITE_ONCE(pane->shared->height, (unsigned int)pane->height);

        // A program asleep on its file lays out to the new grid now, not
        // when its next key arrives.
        wake_up_interruptible(&pane->wait);
}

static void pane_refresh(struct pane *pane)
{
        struct window *shared = pane->shared;
        unsigned int width, height;

        if (!shared)
                return;

        /*
                Not while it is being dragged or resized. The compositor owns
                the rectangle for as long as a hand is on it, and reading the
                program's copy back mid-drag is two writers fighting over one
                number -- which looks like a window that will not stay where
                it is put.
        */
        if (desktop.dragging != pane && desktop.resizing != pane)
        {
                width = READ_ONCE(shared->width);
                height = READ_ONCE(shared->height);

                pane->width = (int)min(width, pane->max_width);
                pane->height = (int)min(height, pane->max_height);
                // Clamped the way z is, and for the same reason: these
                // are a program's numbers and everything drawn is measured
                // from them.
                pane->x = clamp(READ_ONCE(shared->x),
                                -WINDOW_COORD_MAX, WINDOW_COORD_MAX);
                pane->y = clamp(READ_ONCE(shared->y),
                                -WINDOW_COORD_MAX, WINDOW_COORD_MAX);
        }
        pane->z = clamp(READ_ONCE(shared->z), -WINDOW_Z_MAX, WINDOW_Z_MAX);
        pane->region = READ_ONCE(shared->region);
        pane->display = READ_ONCE(shared->display);
        pane->style = READ_ONCE(shared->style);
        pane->edge = (int)min(READ_ONCE(shared->edge), 256u);
        pane->sequence = READ_ONCE(shared->sequence);

        if (pane->cells)
        {
                // What the program says it changed, clamped to what it has.
                unsigned int row = READ_ONCE(shared->damage_row);
                unsigned int count = READ_ONCE(shared->damage_rows);
                unsigned int head = READ_ONCE(shared->head);

                /*
                        The shape the program says its cells are in, which is
                        not the shape they were asked to be in until it has
                        caught up. Composing from the requested one mid-resize
                        wraps every line at a width nothing was written at.
                */
                pane->grid_columns = min(READ_ONCE(shared->grid_columns),
                                         pane->max_columns);
                pane->grid_rows = min(READ_ONCE(shared->grid_rows), pane->max_rows);

                // A program's number, so it can be anything at all. Before the
                // ring began there is nothing to count backwards through.
                pane->head = max(head, pane->history);

                pane->damage_row = min(row, pane->grid_rows);
                pane->damage_rows = min(count, pane->grid_rows - pane->damage_row);

                // Scrolled back, what the program drew is not what is shown.
                if (pane->view != PANE_LIVE)
                        pane->damage_rows = 0;

                WRITE_ONCE(shared->damage_row, 0);
                WRITE_ONCE(shared->damage_rows, 0);
        }

        /*
                Copied, not pointed at. Composing walks this string every time
                it draws a titlebar, and the program can be rewriting the page
                underneath at any instant.
        */
        memory_copy_apart(pane->title, shared->title, WINDOW_TITLE_MAX);
        pane->title[WINDOW_TITLE_MAX - 1] = 0;
        pane->title_length = (unsigned int)string_length_max(
            pane->title, WINDOW_TITLE_MAX);

        pane_size(pane);
        pane_regrid(pane);
        pane_place(pane);

        // Where a region put it, so the program can read where it ended up.
        if (pane->region != WINDOW_FREE)
        {
                WRITE_ONCE(shared->x, pane->x);
                WRITE_ONCE(shared->y, pane->y);
        }
}

/*
        Focus is a pointer here and a bit in every shared page, so a program
        can see it without being told and the compositor can colour a titlebar
        without a lookup.
*/
static void pane_focus(struct pane *pane)
{
        struct pane *old = desktop.focused;

        if (old == pane)
                return;

        /*
                One bit each way, not the whole word.

                state was only ever WINDOW_FOCUSED, so assigning it was the
                same as setting the bit -- and stopped being the same the
                moment WINDOW_CLOSING joined it: clicking another window
                after pressing the X would have withdrawn the request the
                program had not read yet.
        */
        if (old)
        {
                old->state &= ~WINDOW_FOCUSED;
                if (old->shared)
                        WRITE_ONCE(old->shared->state, old->state);
        }

        desktop.focused = pane;

        if (pane)
        {
                pane->state |= WINDOW_FOCUSED;
                if (pane->shared)
                        WRITE_ONCE(pane->shared->state, pane->state);
        }
}

/*
        Ask the program to close its window.

        Nothing here frees anything. The pane goes when the program closes the
        file it was made from, which is window_release, and a program that
        never does keeps its window on the screen -- visibly, which is the
        honest outcome and not a leak: the alternative is the compositor
        taking pages out from under a task that is still writing to them.
*/
static void pane_close_request(struct pane *pane)
{
        if (!pane->shared || (pane->state & WINDOW_CLOSING))
                return;

        pane->state |= WINDOW_CLOSING;
        WRITE_ONCE(pane->shared->state, pane->state);
        atomic_set(&pane->closing_wake, 1);
        wake_up_interruptible(&pane->wait);
}

/*
        A client can place its pane beyond every output, or collapse either
        content dimension to zero. Neither shape can be reached by the pointer,
        so neither may own the keyboard. The frame counts for intersection: a
        visible titlebar remains a visible, recoverable window.
*/
static PURE _Bool pane_visible(struct pane *pane)
{
        struct drm_rect frame;
        struct output *output;

        if (pane->width <= 0 || pane->height <= 0)
                return false;

        pane_frame(pane, &frame);

        list_for_each_entry(output, &desktop.outputs, link)
                if (output_touched(output, &frame, 1))
                        return true;

        return false;
}

static PURE _Bool pane_focusable(struct pane *pane, _Bool include_minimized)
{
        return pane->shared && !(pane->style & WINDOW_PASSTHROUGH) &&
               (include_minimized || !(pane->style & WINDOW_MINIMIZED)) &&
               pane_visible(pane);
}

/*
        The highest z that can take focus, skipping one on its way out.

        Closing and minimizing both hand focus to the window underneath the
        one leaving, and Alt-Tab starts from the top of the whole stack; this
        walk was written once per caller before it was written here.
*/
static PURE struct pane *pane_topmost(struct pane *except,
                                      _Bool include_minimized, int below)
{
        struct pane *pane;
        struct pane *top = NULL;

        list_for_each_entry(pane, &desktop.windows, link)
                if (pane != except && pane_focusable(pane, include_minimized) &&
                    pane->z < below && (!top || pane->z > top->z))
                        top = pane;

        return top;
}

/*
        One Alt-Tab step, without changing z yet.

        Focus is allowed to preview a minimized pane; releasing Alt restores
        and raises it in pane_focus_commit. When focus is already the highest
        eligible z, choose the one immediately behind it. When focus came from
        minimizing that top window, choose the highest one instead, which is
        the window the user just put away and makes Alt-Tab a restore path.
*/
static _Bool pane_focus_step(void)
{
        struct pane *top = pane_topmost(NULL, true, INT_MAX);
        struct pane *focused = desktop.focused;
        struct pane *next;
        int below;

        /*
                Start behind the active top window. If focus is not top -- it
                moved when that top window was minimized -- start from top so
                one Alt-Tab restores the thing just put away. Further steps
                descend the unchanged z order and wrap after the bottom.
        */
        if (desktop.focus_cycle_z)
                below = desktop.focus_cycle_z;
        else
                below = focused == top && focused ? focused->z : INT_MAX;

        next = pane_topmost(NULL, true, below);
        if (!next)
                next = top;

        if (!next || next == focused)
                return false;

        desktop.focus_cycle_z = next->z;
        pane_focus(next);
        return true;
}

static _Bool pane_focus_commit(void)
{
        struct pane *pane = desktop.focused;

        desktop.focus_cycle_z = 0;

        if (!pane || !pane_focusable(pane, true))
                return false;

        if (pane->style & WINDOW_MINIMIZED)
        {
                pane->style &= ~WINDOW_MINIMIZED;
                WRITE_ONCE(pane->shared->style, pane->style);
        }

        pane_raise(pane);
        list_move_tail(&pane->link, &desktop.windows);
        return true;
}

static _Bool pane_minimize_focused(void)
{
        struct pane *pane = desktop.focused;

        if (!pane || !pane_focusable(pane, false))
                return false;

        pane->style |= WINDOW_MINIMIZED;
        WRITE_ONCE(pane->shared->style, pane->style);

        pane_focus(pane_topmost(pane, false, INT_MAX));
        return true;
}

/*
        Windows the desktop no longer reaches.

        Unplugging a screen shrinks the desktop, and anything that was on it
        is then outside every output -- drawn nowhere and impossible to take
        hold of, because the pointer is confined to the outputs. Each one is
        pulled back far enough that its titlebar is on a screen.
*/
static void desktop_gather_panes(void)
{
        struct pane *pane;

        list_for_each_entry(pane, &desktop.windows, link)
        {
                int x = clamp(pane->x, 0, max(desktop.width - WINDOW_MIN_WIDTH, 0));
                int y = clamp(pane->y, 0, max(desktop.height - canvas_title, 0));

                if (x == pane->x && y == pane->y)
                        continue;

                pane->x = x;
                pane->y = y;

                if (pane->shared)
                {
                        WRITE_ONCE(pane->shared->x, x);
                        WRITE_ONCE(pane->shared->y, y);
                }
        }
}

static void desktop_damage_rect(const struct drm_rect *r)
{
        if (desktop.damage_all)
                return;

        if (desktop.damage_count == ARRAY_SIZE(desktop.damage))
        {
                desktop.damage_all = true;
                return;
        }

        desktop.damage[desktop.damage_count++] = *r;
}

static void pane_damage_frame(struct pane *pane)
{
        struct drm_rect frame;

        pane_frame(pane, &frame);
        desktop_damage_rect(&frame);
}

static void pane_damage_was_and_now(struct pane *pane, const struct drm_rect *was)
{
        desktop_damage_rect(was);
        pane_damage_frame(pane);
}

// The rectangle a run of changed cell rows covers, in desktop coordinates.
// Owned panes and shared ones both report damage this way, and the titlebar
// offset was the same arithmetic written in each.
static void pane_damage_rows(struct pane *pane, unsigned int row, unsigned int count)
{
        struct drm_rect r;

        drm_rect_init(&r, pane->x,
                      pane->y + pane_title(pane) + (int)row * canvas_cell_h,
                      pane->width, (int)count * canvas_cell_h);
        desktop_damage_rect(&r);
}

/*
        The strip the scrollbar is drawn in, in desktop coordinates.

        The bounding box, worked out arithmetically, and never pane_bar: that
        walks all five hundred lines of the ring twice over to place the thumb
        exactly, which is the right price to pay once inside compose_bar --
        which rejects by this same box first -- and the wrong one to pay for
        every window on every pass of the refresh loop.
*/
static void pane_damage_bar(struct pane *pane)
{
        struct drm_rect gutter;

        pane_gutter_rect(pane, &gutter);
        desktop_damage_rect(&gutter);
}

/*
        Reads every shared page and records what moved.

        A program that changed a few rows of text should not cost a repaint of
        every screen, which is what committing used to mean -- and resizing a
        window of cells relaid every row, so a drag was a full recompose per
        step of it.
*/
static void desktop_refresh_panes(void)
{
        struct pane *pane;

        desktop.damage_count = 0;
        desktop.damage_all = false;

        list_for_each_entry(pane, &desktop.windows, link)
        {
                int was_x = pane->x, was_y = pane->y;
                int was_w = pane->width, was_h = pane->height;
                unsigned int was_z = (unsigned int)pane->z;
                unsigned int was_style = pane->style;
                unsigned int was_sequence = pane->sequence;
                struct drm_rect was;

                /*
                        The compositor's own. There is no page to read back, so
                        what changed is already recorded on the pane; taking it
                        clears it, the way pane_refresh clears a program's.
                */
                if (!pane->shared)
                {
                        if (pane->view_moved)
                        {
                                pane->view_moved = false;
                                pane->damage_rows = 0;
                                pane_damage_frame(pane);
                                continue;
                        }

                        /*
                                Scrolled back, the same as a program's window
                                below: the rows are not what is being shown,
                                so only the bar is redrawn -- and the report
                                is taken either way, or the desktop would be
                                told for ever that something had changed.
                        */
                        if (pane->cells && pane->view != PANE_LIVE)
                        {
                                pane_damage_bar(pane);
                                pane->damage_row = 0;
                                pane->damage_rows = 0;
                                continue;
                        }

                        if (pane->cells && pane->damage_rows)
                        {
                                unsigned int row = min(pane->damage_row, pane->grid_rows);
                                unsigned int count = min(pane->damage_rows,
                                                         pane->grid_rows - row);

                                if (count)
                                        pane_damage_rows(pane, row, count);

                                pane->damage_row = 0;
                                pane->damage_rows = 0;
                        }
                        continue;
                }

                pane_frame(pane, &was);
                pane_refresh(pane);

                /*
                        The wheel or a keystroke moved the view, which changes
                        every row of it.

                        Both rectangles, because pane_refresh has run since the
                        old frame was taken and the program may have moved the
                        window in the same pass. Damaging only where it was
                        left the rows it moved to undrawn, which is the trail
                        the reshaped path below damages twice to avoid.
                */
                if (pane->view_moved)
                {
                        pane->view_moved = false;
                        pane_damage_was_and_now(pane, &was);
                        continue;
                }

                _Bool reshaped = pane->x != was_x || pane->y != was_y ||
                                 pane->width != was_w || pane->height != was_h ||
                                 (unsigned int)pane->z != was_z ||
                                 pane->style != was_style;

                // Nothing that reaches the screen changed.
                if (!reshaped && pane->sequence == was_sequence)
                        continue;

                /*
                        Scrolled back: what the program drew is not what is
                        being shown, so none of its rows are repainted.

                        The bar is the exception, and used to be skipped with
                        them. It is a picture of how much there is and how far
                        down it you are looking, and both of those change with
                        every line that arrives -- so a window left scrolled
                        back while its program kept writing showed a thumb
                        that grew steadily more wrong about a ring that had
                        moved on underneath it, and only told the truth again
                        once the wheel was touched.
                */
                if (!reshaped && pane->view != PANE_LIVE)
                {
                        if (pane->cells)
                                pane_damage_bar(pane);

                        continue;
                }

                // Anything but text changing in place is easier to repaint
                // whole than to reason about.
                if (reshaped || !pane->cells || !pane->damage_rows)
                {
                        pane_damage_was_and_now(pane, &was);
                        continue;
                }

                pane_damage_rows(pane, pane->damage_row, pane->damage_rows);
        }

        /*
                The keyboard follows what the user can actually see.

                A program owns its style word, and pane_refresh above has just
                read it back: it can set MINIMIZED, which compose draws
                nowhere, and PASSTHROUGH, which drag lets the pointer fall
                straight through. Neither bit moved focus, so a window could
                take the keyboard on creation and then make itself invisible
                and untouchable while every keystroke kept arriving in the
                ring it shares. That is a keylogger built entirely out of the
                documented interface, with no bug to find.

                pane_minimize_focused already holds this invariant for the
                compositor's own minimize. This is the same rule applied to
                the path the program drives.

                Only a pane with a shared page. An owned one -- the console
                -- has no program behind it and no page to set a style
                through, so it cannot be the thing this guards against, and
                pane_focusable rejects it for the unrelated reason that it
                has no page at all. pane_under does not skip it, so a click
                can focus it, and without this test that focus would be taken
                straight back on the next pass.

                Mid Alt-Tab a minimized window is legitimately focused, so
                cycling passes include_minimized and only PASSTHROUGH revokes.
                Both flags are read, not just focus_cycling: releasing Alt
                clears cycling and sets commit in one step, and until the
                canvas thread runs pane_focus_commit the window it is about to
                restore is still minimized. A refresh in that gap -- any
                program can drive one through WINDOW_IOCTL_COMMIT -- would
                otherwise revoke the focus commit was on its way to keep.
        */
        if (desktop.focused && desktop.focused->shared &&
            !pane_focusable(desktop.focused,
                            atomic_read(&desktop.focus_cycling) ||
                            atomic_read(&desktop.focus_commit)))
        {
                pane_focus(pane_topmost(desktop.focused, false, INT_MAX));

                // pane_focus restyles every titlebar, and the damage loop
                // above has already run. Every other caller pairs it with a
                // redraw; this is that, without recursing into one.
                desktop.damage_all = true;
        }

        /*
                A terminal the compositor started takes the keyboard.

                Opened at boot or asked for with Control-Shift-T, it is the
                window about to be typed into, and nothing else would give it
                focus: create does not, so that no program can take the
                keyboard by opening a window. The mark is spent on the first
                pass that sees it, and given only to a pane that can be seen
                and clicked, so it is never a claim left standing.
        */
        list_for_each_entry(pane, &desktop.windows, link)
        {
                if (!pane->spawned)
                        continue;

                pane->spawned = false;

                if (pane_focusable(pane, false))
                {
                        pane_focus(pane);
                        desktop.damage_all = true;
                }
        }

        list_sort(NULL, &desktop.windows, pane_by_z);
}

static long window_ioctl_create(struct file *file, unsigned long argument)
{
        struct device_context *context = file->private_data;
        struct window_request request;
        struct pane *pane;
        unsigned long bytes;

        if (copy_from_user(&request, (void __user *)argument, sizeof(request)))
                return -EFAULT;

        rt_mutex_lock(&desktop.lock);

        // Tested and stored under the one lock: two threads on one file used
        // to be able to both create, and the window one of them made was then
        // reachable from nothing and freed by nothing.
        if (context->pane)
        {
                rt_mutex_unlock(&desktop.lock);
                return -EBUSY;
        }

        if (list_empty(&desktop.outputs) || desktop.off)
        {
                rt_mutex_unlock(&desktop.lock);
                return -ENODEV;
        }

        pane = pane_create(request.width, request.height,
                           request.columns, request.rows, false);
        if (!pane)
        {
                rt_mutex_unlock(&desktop.lock);
                return -EINVAL;
        }

        bytes = pane->bytes;

        /*
                Released, because window_mmap reads this pointer without the
                desktop lock and must not see it before the pane it points at.
                Two threads on one descriptor -- create here, mmap there -- is
                allowed, and on a weakly ordered machine the store of the
                pointer can otherwise land ahead of the stores that filled in
                bytes and mapping. Free on x86, a compiler barrier only.
        */
        smp_store_release(&context->pane, pane);

        /*
                Marked, never focused here. Only the task spawn_terminal
                started matches, and taking its pid back means only the first
                window it opens is marked. current holds its own pid alive, so
                a match cannot be a freed pid reused.
        */
        {
                struct pid *spawned = READ_ONCE(canvas_spawned);

                if (spawned && spawned == task_tgid(current) &&
                    cmpxchg(&canvas_spawned, spawned, NULL) == spawned)
                {
                        pane->spawned = true;
                        put_pid(spawned);
                }
        }

        desktop_recompose();

        rt_mutex_unlock(&desktop.lock);

        // How much to map, which the program cannot work out for itself.
        return (long)bytes;
}

/*
        Whether the program has anything to wake up for: a key it has not
        read, or a grid the compositor changed that it has not laid out to.
        The wait queue is the pane's, and the pane outlives every poll on its
        file, since only the file's own release frees it.
*/
static __poll_t window_poll(struct file *file, poll_table *wait)
{
        struct device_context *context = file->private_data;
        struct pane *pane = smp_load_acquire(&context->pane);
        struct window *shared;

        if (!pane || !pane->shared)
                return 0;

        shared = pane->shared;
        poll_wait(file, &pane->wait, wait);

        /*
                The close request wakes the program and has to be something it
                can wake up to. pane_close_request already wakes this queue;
                without a reason here the poll went straight back to sleep and
                the X in the titlebar did nothing until the next keystroke.

                Taken rather than read, so it is delivered once. The request
                itself stays set in the shared page for the program to find at
                its own pace; answering from that instead would make every
                later poll return immediately and turn a program that ignores
                the X into one that spins.
        */
        if (atomic_xchg(&pane->closing_wake, 0))
                return EPOLLIN | EPOLLRDNORM;

        // The grid the program laid out to is its own record in the page;
        // the compositor's columns and rows moving away from it is a resize
        // it has not seen yet, the same test window_regrid makes.
        if (READ_ONCE(shared->key_head) != READ_ONCE(shared->key_tail) ||
            READ_ONCE(shared->columns) != READ_ONCE(shared->grid_columns) ||
            READ_ONCE(shared->rows) != READ_ONCE(shared->grid_rows))
                return EPOLLIN | EPOLLRDNORM;

        return 0;
}

/*
        A program's lines spread further apart, because its window has grown
        wider than they are.

        The ring is laid out for the ceiling, so it has room for lines as
        wide as the window can ever be, but they start as far apart as the
        window opens: a terminal's lines a page each were a scrolled
        terminal holding 1,920 KiB for text a quarter as wide. A program
        that is given more columns than its stride asks for this before it
        lays its text out at them, and every line it has written moves out
        to its place at the wider stride here, under desktop.lock so no
        compose reads the ring half moved, and under pages_lock because the
        lines land on pages that may not be held yet. The program is in this
        call, so it is not writing; whatever else writes its page gets the
        same ring it would have got from a program that raced itself.

        Only wider: a narrower stride would cut the lines a wider window
        wrote, which it still draws folded. Every page a moved line lands on
        is held before any line moves, so running out of memory leaves the
        ring as it was, and the answer is the stride the lines are now.
*/
static long pane_restride(struct pane *pane, unsigned int columns)
{
        unsigned int stride = console_stride(min(columns, pane->max_columns),
                                             pane->max_columns);
        unsigned int was = pane->stride;
        unsigned int slot, length;
        unsigned long first, last;

        if (!pane->pages || stride <= was)
                return was;

        for (slot = 0; slot < pane->history; slot++)
        {
                length = min(READ_ONCE(pane->lengths[slot]), was);
                if (!length || !pane_cells_held(pane, slot, length))
                        continue;

                first = WINDOW_PIXELS + (unsigned long)slot * stride *
                                            sizeof(struct window_cell);
                last = first + (unsigned long)length * sizeof(struct window_cell);
                if (!pane_pages_hold(pane, first >> PAGE_SHIFT,
                                     PAGE_ALIGN(last) >> PAGE_SHIFT))
                        return -ENOMEM;
        }

        // From the last line down: a line only ever moves further out, past
        // the end of every line below it, so nothing lands on a line that
        // has yet to move.
        for (slot = pane->history; slot--;)
        {
                length = min(READ_ONCE(pane->lengths[slot]), was);
                if (length && !pane_cells_held(pane, slot, length))
                        length = 0;

                /*
                        The program's word again, and "the program is in
                        this call" is one thread of it: another can have
                        grown the line since the pass above held its new
                        place, and the move then wrote into a page of the
                        reservation with nothing mapped, an oops under both
                        locks. Hold the place for the length read now --
                        nothing to do where the first pass did it -- and
                        leave the line empty where memory is short.
                */
                if (length)
                {
                        first = WINDOW_PIXELS + (unsigned long)slot * stride *
                                                    sizeof(struct window_cell);
                        last = first + (unsigned long)length *
                                           sizeof(struct window_cell);
                        if (!pane_pages_hold(pane, first >> PAGE_SHIFT,
                                             PAGE_ALIGN(last) >> PAGE_SHIFT))
                                length = 0;
                }

                if (length)
                        memmove(pane->cells + (unsigned long)slot * stride,
                                pane->cells + (unsigned long)slot * was,
                                (unsigned long)length * sizeof(struct window_cell));

                WRITE_ONCE(pane->lengths[slot], length);
        }

        pane->stride = stride;
        WRITE_ONCE(pane->shared->stride, stride);
        pane_say("window ring recut to %ux%u\n", stride, pane->history);
        return stride;
}

static long window_ioctl_stride(struct file *file, unsigned long columns)
{
        struct device_context *context = file->private_data;
        struct pane *pane = smp_load_acquire(&context->pane);
        long answer;

        if (!pane || !pane->cells || !pane->shared)
                return -EINVAL;

        rt_mutex_lock(&desktop.lock);
        if (pane->pages)
                mutex_lock(&pane->pages_lock);

        answer = pane_restride(pane, (unsigned int)min(columns, (unsigned long)UINT_MAX));

        if (pane->pages)
                mutex_unlock(&pane->pages_lock);

        // Nothing to draw: every line reads the same at its new place.
        rt_mutex_unlock(&desktop.lock);
        return answer;
}

static long window_ioctl_commit(struct file *file)
{
        struct device_context *context = file->private_data;

        if (!context->pane)
                return -EINVAL;

        rt_mutex_lock(&desktop.lock);

        // Another program has the display: nothing drawn now would land, and
        // the resume draws everything once it lets go.
        if (desktop_taken())
        {
                if (!desktop.suspended)
                {
                        desktop.suspended = true;
                        canvas_thread_wake();
                }

                rt_mutex_unlock(&desktop.lock);
                return 0;
        }

        desktop_watch();

        /*
                At most once a frame per window, and the frame does the rest.

                A terminal commits every time it drains its pty, and under a
                stream that is thousands of times a second: a cat of UTF-8
                text through a guest's terminal composed its whole window
                5,800 times a second for a display that shows sixty. The
                sequence the program bumped before calling is what the next
                frame looks for, and the damage rows stay in its page until a
                compose takes them, so a commit arriving within a frame of the
                last one that composed is left to that frame.

                Not the first commit after a key has gone to the window: that
                is the echo window_flush exists for, and it composes here and
                now whatever the window was doing, as does any commit after a
                quiet frame. The frame timer runs while the desktop is awake,
                which desktop_watch has just made sure of; without a thread to
                take the frame, awake stays false and every commit composes.
        */
        {
                struct pane *pane = context->pane;
                u64 now = ktime_get_ns();

                if (desktop.awake && !READ_ONCE(pane->keyed) &&
                    now - pane->composed_ns < canvas_frame_ns())
                {
                        rt_mutex_unlock(&desktop.lock);
                        return 0;
                }

                pane->composed_ns = now;
                WRITE_ONCE(pane->keyed, false);
        }

        desktop_refresh_panes();
        desktop_repaint();
        rt_mutex_unlock(&desktop.lock);

        return 0;
}

/*
        A program touching a page of its ring for the first time: the page is
        given memory, mapped where the compositor reads it, and handed to the
        program. One lock per window, taken only on a first touch, which a
        window pays at most once for each page it has.
*/
static vm_fault_t window_fault(struct vm_fault *vmf)
{
        struct pane *pane = vmf->vma->vm_private_data;
        struct page *page;

        if (vmf->pgoff >= pane->page_count)
                return VM_FAULT_SIGBUS;

        page = smp_load_acquire(&pane->pages[vmf->pgoff]);
        if (!page)
        {
                mutex_lock(&pane->pages_lock);
                page = pane_page_hold(pane, vmf->pgoff);
                mutex_unlock(&pane->pages_lock);

                if (!page)
                        return VM_FAULT_OOM;
        }

        get_page(page);
        vmf->page = page;
        return 0;
}

static const struct vm_operations_struct window_vm_ops = {
        .fault = window_fault,
};

static int window_mmap(struct file *file, struct vm_area_struct *vma)
{
        struct device_context *context = file->private_data;
        struct pane *pane = smp_load_acquire(&context->pane);

        if (!pane)
                return -EINVAL;

        if (vma->vm_pgoff)
                return -EINVAL;

        if (vma->vm_end - vma->vm_start > pane->bytes)
                return -EINVAL;

        if (!pane->pages)
                return remap_vmalloc_range(vma, pane->mapping, 0);

        // A held ring is mapped as it is touched, by window_fault. The
        // file outlives every mapping of it, so the pane does as well.
        vma->vm_ops = &window_vm_ops;
        vma->vm_private_data = pane;
        vm_flags_set(vma, VM_DONTEXPAND | VM_DONTDUMP);
        return 0;
}

static void window_release(struct file *file)
{
        struct device_context *context = file->private_data;
        struct pane *pane = context->pane;
        _Bool refocus;

        if (!pane)
                return;

        rt_mutex_lock(&desktop.lock);

        // Asked before the free, which is what clears it.
        refocus = desktop.focused == pane;

        pane_free(pane);

        // Closing the active window hands focus to what is now on top,
        // instead of leaving a live desktop with nowhere for keys to go.
        if (refocus)
                pane_focus(pane_topmost(NULL, false, INT_MAX));

        if (!list_empty(&desktop.outputs))
                desktop_recompose();

        rt_mutex_unlock(&desktop.lock);

        context->pane = NULL;
}

/*
        Watching the shared pages.

        A program changes a window by storing into its page and bumping a
        sequence, which costs it nothing. Something has to notice, so this
        ticks while there is something to notice and stops once nothing has
        changed for a couple of seconds. An idle desktop takes no wakeups, and
        a program drawing every frame makes no calls; the one call is the first
        change after the compositor went to sleep.

        The period is the fastest output's refresh. Sixteen milliseconds is
        60 Hz, which is the wrong answer on a 120 Hz panel and used to be
        the only answer this had.
*/
#define CANVAS_FRAME_FALLBACK_NS (NSEC_PER_SEC / 60)
#define CANVAS_IDLE_NS (2ULL * NSEC_PER_SEC)

static void desktop_sync_frame_ns(void)
{
        struct output *output;
        unsigned int hz = 0;
        u64 ns;

        list_for_each_entry(output, &desktop.outputs, link)
        {
                unsigned int refresh;

                if (!output->mode_set || !output->mode_set->mode)
                        continue;

                refresh = drm_mode_vrefresh(output->mode_set->mode);
                if (refresh > hz)
                        hz = refresh;
        }

        ns = hz ? NSEC_PER_SEC / hz : CANVAS_FRAME_FALLBACK_NS;
        if (!ns)
                ns = CANVAS_FRAME_FALLBACK_NS;

        WRITE_ONCE(desktop.frame_ns, ns);
}

static u64 canvas_frame_ns(void)
{
        u64 ns = READ_ONCE(desktop.frame_ns);

        return ns ? ns : CANVAS_FRAME_FALLBACK_NS;
}

static void desktop_set_awake(_Bool awake)
{
        struct pane *pane;

        desktop.awake = awake;

        list_for_each_entry(pane, &desktop.windows, link)
                if (pane->shared)
                        WRITE_ONCE(pane->shared->awake, awake);
}

static enum hrtimer_restart desktop_frame(struct hrtimer *timer)
{
        if (!READ_ONCE(desktop.awake))
                return HRTIMER_NORESTART;

        atomic_set(&desktop.frame_pending, 1);
        WRITE_ONCE(canvas_ticks, canvas_ticks + 1);
        canvas_thread_wake();

        hrtimer_forward_now(timer, ns_to_ktime(canvas_frame_ns()));
        return HRTIMER_RESTART;
}

static _Bool canvas_thread_running(void);

static void desktop_watch(void)
{
        if (desktop.awake)
        {
                desktop.idle_frames = 0;
                return;
        }

        // A frame with nothing to service it wakes nobody and re-arms itself
        // forever, and every program would sit waiting on an awake that no
        // longer means anything.
        if (!canvas_thread_running())
                return;

        desktop_set_awake(true);
        desktop.idle_frames = 0;

        //      The last frame of the idle spell may still be running on
        //      another CPU: it can read awake after the store above, forward
        //      itself and ask to be re-armed, while the start below has
        //      already queued it -- and forwarding a queued timer is the
        //      WARN_ON in hrtimer_forward and a corrupted timer tree. Cancel
        //      waits for that callback and takes the timer off the queue, so
        //      only the start below arms it. This runs under desktop.lock in
        //      process context, which the callback never takes, and only on
        //      the step out of idle.
        hrtimer_cancel(&desktop.frame);
        hrtimer_start(&desktop.frame, ns_to_ktime(canvas_frame_ns()),
                      HRTIMER_MODE_REL);
}

static _Bool desktop_sequence_changed(void)
{
        struct pane *pane;

        list_for_each_entry(pane, &desktop.windows, link)
        {
                if (pane->view_moved)
                        return true;

                if (!pane->shared)
                {
                        if (pane->damage_rows)
                                return true;

                        continue;
                }

                if (READ_ONCE(pane->shared->sequence) != pane->sequence)
                        return true;
        }

        return false;
}

/*
        A magnified cursor goes back on its own. Nothing else is watching the
        clock, so the frame the shake armed is what notices.
*/
static void pointer_report(struct pane *pane, int x, int y,
                           unsigned int button, unsigned int flags)
{
        struct window *shared;
        struct window_key key;
        unsigned int head, tail;
        int title, col, row;

        if (!pane || !pane->shared || !pane->cells)
                return;

        shared = pane->shared;
        if (!(READ_ONCE(shared->want) & WINDOW_WANT_POINTER))
                return;

        title = pane_title(pane);
        if (canvas_cell_w <= 0 || canvas_cell_h <= 0)
                return;

        col = (x - pane->x) / canvas_cell_w;
        row = (y - pane->y - title) / canvas_cell_h;
        if (col < 0 || row < 0)
                return;
        if ((unsigned int)col >= pane->columns ||
            (unsigned int)row >= pane->rows)
                return;

        {
                unsigned int mods = (unsigned int)atomic_read(&desktop.modifiers);

                if (mods & WINDOW_KEY_SHIFT)
                        button += 4;
                if (mods & WINDOW_KEY_ALT)
                        button += 8;
                if (mods & WINDOW_KEY_CONTROL)
                        button += 16;
        }

        key.code = 0;
        key.character = button;
        key.flags = WINDOW_KEY_POINTER | flags;
        key.reserved = ((unsigned int)(col + 1) & 0xffff) |
                       (((unsigned int)(row + 1) & 0xffff) << 16);

        head = READ_ONCE(shared->key_head);
        tail = READ_ONCE(shared->key_tail);

        if ((flags & WINDOW_KEY_POINTER_MOVE) && head != tail)
        {
                struct window_key *last =
                    &shared->keys[(head - 1) % WINDOW_KEYS];

                if (last->flags & WINDOW_KEY_POINTER_MOVE)
                {
                        *last = key;
                        smp_wmb();
                        return;
                }
        }

        if (head - tail >= WINDOW_KEYS)
                return;

        shared->keys[head % WINDOW_KEYS] = key;
        smp_wmb();
        WRITE_ONCE(shared->key_head, head + 1);
        wake_up_interruptible(&pane->wait);
}

static void cursor_settle(void)
{
        if (desktop.cursor_scale <= desktop.scale)
                return;

        if (ktime_get_ns() < desktop.magnified_until)
                return;

        desktop.cursor_scale = desktop.scale;
        cursor_move(desktop.cursor_x, desktop.cursor_y);
}

static _Bool console_pending(void);

static void desktop_frame_pass(void)
{
        rt_mutex_lock(&desktop.lock);

        cursor_settle();

        if (desktop_sequence_changed())
        {
                desktop_refresh_panes();
                desktop_repaint();
                desktop.idle_frames = 0;
        }
        else if (desktop.cursor_scale > desktop.scale)
        {
                desktop.idle_frames = 0;
        }
        else if ((u64)++desktop.idle_frames * canvas_frame_ns() >= CANVAS_IDLE_NS)
        {
                desktop_set_awake(false);

                /*
                        One more look after clearing it. A program that read
                        awake just before this and so skipped its call would
                        otherwise be left on screen a frame behind until the
                        next thing it did.
                */
                smp_mb();

                if (desktop_sequence_changed())
                {
                        desktop_refresh_panes();
                        desktop_repaint();
                }

                // And the console, whose writer read awake too and left its
                // record for this frame rather than waking anyone.
                if (console_pending())
                {
                        atomic_set(&desktop.frame_pending, 1);
                        canvas_thread_wake();
                }
        }

        rt_mutex_unlock(&desktop.lock);
}

#include "term.c"
/* ---- console: the kernel log window ---- */

/*
        The screen the machine always has.

        A window the compositor owns and writes itself, carrying the kernel
        log. Everything else on the desktop belongs to a program: if userspace
        never starts, or starts and dies, or the display comes up and nothing
        claims it, the desktop is empty and there is nothing to read. This is
        what is there instead.

        It is also the answer to a black screen on a machine with no serial
        port. Canvas says why it could not draw through printk, and printk now
        has somewhere to go that is not a serial line -- so the reason lands on
        the monitor of the machine that has the problem.

        A console write arrives from any context, including one that must not
        sleep, so this puts characters into cells and asks for a frame. The
        drawing happens later on the compositor's own thread, which is the only
        thing here that touches the display.

        The lines, the ring they sit in and the wheel that moves over it are
        the ones every window of cells has, in pane above, and what turns a stream
        of bytes into lines is the emulator in term.c -- the same one the
        shell's terminal is. So a carriage return, a tab and an escape sequence
        mean here what they mean there, and this file is only the wiring.
*/

#define CONSOLE_COLUMNS 100
#define CONSOLE_ROWS 30

static struct pane *console_pane;
static DEFINE_SPINLOCK(console_cells);

/*
        One printk record into the emulator, as the console has always fed
        it: its own message, not the end of the last one, and a newline that
        means the start of the next line.

        A newline here is a line feed and nothing else. The emulator is what
        sits behind a pty, and the line discipline in front of one turns \n
        into \r\n before it ever arrives. printk has no line discipline: it
        says \n and means the start of the next line. Fed raw, the cursor
        drops a row and stays in the column it was in, so every message
        begins where the one before it ended and the log walks off to the
        right until it wraps. The return is put in here, which is where the
        pty that is missing would have put it.

        Under console_cells.
*/
static void console_feed(const char *text, unsigned int count)
{
        unsigned int i;

        term_record_begin();

        for (i = 0; i < count;)
        {
                const p8 *newline = memory_first_of(
                    (address_any)(text + i), '\n', count - i);
                unsigned int stop = newline
                                        ? (unsigned int)(newline -
                                                         (const p8 *)text)
                                        : count;

                term_bytes((const p8 *)(text + i), stop - i);

                if (stop == count)
                        break;

                consume('\r');
                consume('\n');
                i = stop + 1;
        }
}

/*
        What the console write leaves for the compositor's thread.

        printk calls the console with interrupts off, and a vector register
        cannot be taken there, so the write does not parse: it copies the
        record into this ring and asks for a frame, and the canvas thread
        takes the ring, runs the emulator over it inside its vector bracket,
        and draws. The interrupts-off stretch a message costs is one copy.

        Records keep their boundaries -- two bytes of length and the bytes --
        because each is its own message to the emulator. A record that finds
        the ring full pushes the oldest out whole rather than being lost
        itself: the last thing a machine said is the one worth keeping, and
        the count of what went is said in the log when the ring is next read.

        A dying machine does not wait for a thread. With an oops or a panic
        in progress, or no thread to hand to, the write empties the ring and
        parses its own record where it is, as it always did.

        As long as the window can show and no longer: its ring keeps
        PANE_HISTORY lines, so a flood the drain meets all at once shows the
        last of them and scrolls the rest away. 128 KiB holds that many
        records of 254 bytes, where a kernel line is under a hundred; the
        256 KiB this was cut at held records the window scrolled away as
        soon as it was given them, in kernel memory from boot. A power of
        two, because the ring's positions are free-running unsigned words
        taken modulo its size.
*/
#define CONSOLE_QUEUE (PANE_HISTORY * 256u)
#define CONSOLE_RECORD 4096u
_Static_assert((CONSOLE_QUEUE & (CONSOLE_QUEUE - 1)) == 0,
               "the console ring's size divides the wrap of its positions");

static char console_queue[CONSOLE_QUEUE];
static unsigned int console_queue_head, console_queue_tail;
static unsigned long console_queue_dropped;
static DEFINE_RAW_SPINLOCK(console_queue_lock);
static atomic_t console_queued = ATOMIC_INIT(0);

// The copy the reader parses from, a record at a time; the reader is the
// canvas thread or a writer that holds console_cells, never both.
static char console_record[CONSOLE_RECORD];

static void console_queue_put(unsigned int at, const void *from, unsigned int count)
{
        unsigned int first = min(count, CONSOLE_QUEUE - at % CONSOLE_QUEUE);

        memory_copy_apart(console_queue + at % CONSOLE_QUEUE, (address_any)from, first);
        memory_copy_apart(console_queue, (address_any)((const char *)from + first),
                          count - first);
}

static void console_queue_get(unsigned int at, void *to, unsigned int count)
{
        unsigned int first = min(count, CONSOLE_QUEUE - at % CONSOLE_QUEUE);

        memory_copy_apart(to, console_queue + at % CONSOLE_QUEUE, first);
        memory_copy_apart((char *)to + first, console_queue, count - first);
}

// Under console_queue_lock.
static unsigned int console_queue_length_at(unsigned int at)
{
        u16 length;

        console_queue_get(at, &length, sizeof(length));
        return length;
}

static _Bool console_pending(void)
{
        return atomic_read(&console_queued) != 0;
}

static void console_enqueue(const char *text, unsigned int count)
{
        unsigned long flags;
        u16 length;

        count = min(count, CONSOLE_RECORD);
        length = (u16)count;

        raw_spin_lock_irqsave(&console_queue_lock, flags);

        while (console_queue_head - console_queue_tail + sizeof(length) + count >
               CONSOLE_QUEUE)
        {
                console_queue_tail += sizeof(length) +
                                      console_queue_length_at(console_queue_tail);
                console_queue_dropped++;
        }

        console_queue_put(console_queue_head, &length, sizeof(length));
        console_queue_put(console_queue_head + sizeof(length), text, count);
        console_queue_head += sizeof(length) + count;
        atomic_set(&console_queued, 1);

        raw_spin_unlock_irqrestore(&console_queue_lock, flags);
}

/*
        The oldest record into console_record, and its length; false when none.

        The length is the ring's own u16, which the enqueue clamps to
        CONSOLE_RECORD -- but it is read back as it lies, and a copy sized by
        it goes into a 4 KiB static in ring 0. So it is held to the record and
        to what the ring holds past it, and a ring that disagrees with itself
        is emptied rather than read on from a position that is not a record's.
*/
static _Bool console_dequeue(unsigned int *count, unsigned long *dropped)
{
        unsigned long flags;
        _Bool some;

        raw_spin_lock_irqsave(&console_queue_lock, flags);

        some = console_queue_head - console_queue_tail >= 2;
        if (some)
        {
                unsigned int held = console_queue_head - console_queue_tail - 2;

                *count = console_queue_length_at(console_queue_tail);
                if (*count > CONSOLE_RECORD || *count > held)
                {
                        console_queue_dropped++;
                        console_queue_tail = console_queue_head;
                        *count = 0;
                }
                else
                {
                        console_queue_get(console_queue_tail + 2, console_record, *count);
                        console_queue_tail += 2 + *count;
                }
        }
        else if (console_queue_head != console_queue_tail)
        {
                console_queue_dropped++;
                console_queue_tail = console_queue_head;
                atomic_set(&console_queued, 0);
        }
        else
                atomic_set(&console_queued, 0);

        *dropped = console_queue_dropped;
        console_queue_dropped = 0;

        raw_spin_unlock_irqrestore(&console_queue_lock, flags);
        return some;
}

// Every record the ring holds into the emulator. Under console_cells.
static void console_feed_queue(void)
{
        unsigned int count;
        unsigned long dropped;

        while (console_dequeue(&count, &dropped))
        {
                if (dropped)
                {
                        char note[64];
                        int n = scnprintf(note, sizeof(note),
                                          "[moonwater canvas] %lu log records "
                                          "dropped, the ring was full\n",
                                          dropped);

                        console_feed(note, (unsigned int)n);
                }

                console_feed(console_record, count);
        }
}

// The emulator has written: tell the compositor. Under console_cells.
static void console_moved(struct pane *pane)
{
        // The emulator counts the ring on in the page; the compositor reads
        // its own copy, and this is where the two meet.
        pane->head = window->head;

        /*
                Said whether or not the view is at the end, because what it
                means is "the ring moved", not "repaint these rows".

                Scrolled back, what arrives changes the ring and not what is
                being looked at -- but the bar beside it is a picture of the
                ring, so it does change, and the refresh loop is where that
                is decided. Saying nothing here instead was a console whose
                only report was one nobody wanted while scrolled back, so
                desktop_sequence_changed answered that nothing had happened
                and the loop that would have redrawn the bar never ran.
        */
        pane->damage_row = 0;
        pane->damage_rows = pane->grid_rows;
}

/*
        The canvas thread's half: the ring into the emulator, inside the
        vector bracket so the emulator's runs of text take the wide bodies.
        Process context with interrupts on, which the bracket needs;
        console_cells is taken plainly, and the only writer that can meet it
        from an interrupt on this processor is a dying one, which does not
        wait for it.
*/
static void console_drain(void)
{
        struct pane *pane = READ_ONCE(console_pane);
        struct canvas_simd_hold simd;

        if (!atomic_read(&console_queued) || !pane || !pane->cells)
                return;

        /*
                console_cells is a spinlock that sleeps on PREEMPT_RT, and
                the bracket holds preemption off, so an RT build drains with
                the general registers instead. No profile turns RT on today.
        */
        canvas_simd_begin(&simd, IS_ENABLED(CONFIG_PREEMPT_RT));
        spin_lock(&console_cells);
        term_simd = simd.on;

        console_feed_queue();
        console_moved(pane);

        term_simd = false;
        spin_unlock(&console_cells);
        canvas_simd_end(&simd);

        /*
                From here the frame timer comes for the console: a flood is
                drained and drawn once a frame, whatever number of records
                arrived in it, rather than once a record.
        */
        rt_mutex_lock(&desktop.lock);
        desktop_watch();
        rt_mutex_unlock(&desktop.lock);
}

/*
        console_cells from a dying writer: taken if it can be, waited on for a
        moment if not, and then gone around -- the holder may be a processor
        panic has stopped, and a log that never reaches the screen is worse
        than one drawn over a half-finished line. Only a dying writer takes
        it from an interrupt, and an oops has turned lockdep off by then.
*/
static _Bool console_cells_take(void)
{
        unsigned int tries;

        for (tries = 0; tries < 1000; tries++)
        {
                if (spin_trylock(&console_cells))
                        return true;
                udelay(10);
        }

        return false;
}

static void console_put_line(struct console *console, const char *text,
                             unsigned int count)
{
        struct pane *pane = READ_ONCE(console_pane);
        unsigned long flags;
        _Bool locked;

        if (!pane || !pane->cells)
                return;

        /*
                Queued whether or not the thread is there to take it: a
                console with no compositor running is a window nobody can
                see, and the ring keeps the last of what was said until
                there is one.
        */
        if (!oops_in_progress && !panic_in_progress())
        {
                console_enqueue(text, count);

                /*
                        A desktop that is awake has a frame coming, and the
                        frame drains the ring: no wake for every record. One
                        that is not is woken, and the drain wakes the frames.
                        The ring's flag is set before awake is read, and the
                        frame that lets awake go reads the flag after it, so
                        one of the two always sees the other.
                */
                smp_mb();
                if (READ_ONCE(desktop.awake))
                        return;
                goto wake;
        }

        local_irq_save(flags);
        locked = console_cells_take();

        // The general register bodies here, whatever a reader left behind.
        term_simd = false;

        /*
                The ring is emptied only with console_cells held: its records
                are copied through console_record, which the canvas thread
                is parsing from when it holds the lock this could not take,
                and a dying writer that went around it shares the emulator
                with that thread already -- sharing its copy of the record as
                well turned the line being drawn into another. What the ring
                still holds is drawn after this one, by whoever next holds
                the lock.
        */
        if (locked)
                console_feed_queue();
        console_feed(text, count);
        console_moved(pane);

        if (locked)
                spin_unlock(&console_cells);
        local_irq_restore(flags);

wake:
        /*
                Asking for a frame rather than drawing one.

                This can be called with interrupts off, from a spinlock, or
                from the middle of a panic. wake_up_process is safe in all
                three; a modeset is not.
        */
        atomic_set(&desktop.frame_pending, 1);
        canvas_thread_wake();
}

static struct console canvas_console = {
        .name = "canvas",
        .write = console_put_line,
        /*
                CON_ENABLED because this one is not a choice.

                Naming console= on the command line turns off every console
                the user did not name, and a machine booted with console=ttyS0
                would register this and never enable it -- which is exactly
                the machine that has no screen output to spare. The point of
                this window is to be there when nothing else is.
        */
        .flags = CON_PRINTBUFFER | CON_ANYTIME | CON_ENABLED,
        .index = -1,
};

static _Bool console_registered;

/*
        Opened once the desktop has a size, and never closed while the module
        is loaded -- including across `moonwater canvas off`. CON_PRINTBUFFER
        replays everything printk has kept, so the window comes up carrying
        the boot it missed rather than starting from whatever was printed next.
*/
static void console_start(void)
{
        struct pane *pane;

        if (console_registered || console_pane)
                return;

        pane = pane_create(0, 0, CONSOLE_COLUMNS, CONSOLE_ROWS, true);

        if (!pane)
        {
                pr_info("[moonwater canvas] " "no room for a console window\n");
                return;
        }

        /*
                The emulator writes through the page every window of cells has,
                so it is pointed at this one's and started the way the terminal
                starts.

                Wrapped at the width the window is drawn at, and at nothing
                else. The byte handler this replaced kept a line as long as it
                was written and let the compositor fold it, which is right for
                something that only ever appends -- it has no cursor and does
                not care which row anything lands on. An emulator does: it owns
                a grid, and every erase, every scroll and every cursor move is
                said in rows of it. Wrap at one width and fold at another and
                the two disagree about where row four is, by one row for every
                line long enough to fold.
        */
        window = pane->mapping;
        COLUMNS = min(pane->columns, pane->stride);
        ROWS = pane->rows;
        full_reset();

        // Set here rather than read from a shared page, because an owned
        // pane has no program behind it to have named itself.
        memory_copy_apart(pane->title, "kernel log", sizeof("kernel log"));
        pane->title_length = sizeof("kernel log") - 1;

        WRITE_ONCE(console_pane, pane);

        // CON_ENABLED lives on the object, not only in the initializer:
        // unregister_console clears it, and a later register then consults
        // console= -- which names ttyS0 here -- and leaves this disabled, so
        // the window comes back empty. Restore the flags a first start had.
        canvas_console.flags = CON_PRINTBUFFER | CON_ANYTIME | CON_ENABLED;
        register_console(&canvas_console);
        console_registered = true;
}

/*
        The window changed shape.

        An owned pane has no program to be told, so pane_regrid sets the grid
        and returns; the emulator behind this one still believes the width it
        was started at. It is told here, through the same page a program would
        have been told through.
*/
/*
        The console grown wider than its ring, cut again for the width it has.

        A program's ring is cut for the ceiling because the program has it
        mapped and cannot be handed a larger one. This one is the
        compositor's own and nothing maps it, so it is cut for the window it
        opens as -- a quarter of the two megabytes a ceiling ring is -- and
        cut again here, under desktop.lock, when the window grows past it.
        The history is the ceiling's already, so every line keeps its slot
        and moves over whole; the swap is under console_cells as well, which
        every writer of the ring holds, a dying one included.

        With no room for a wider ring the old one stays, and the emulator
        wraps at its width (grid_take): a log narrower than its window, not a
        write past the end.
*/
static void console_recut(struct pane *pane)
{
        unsigned int stride = console_stride(pane->columns, pane->max_columns);
        unsigned long lines, bytes, flags;
        struct window_cell *cells;
        unsigned int *lengths;
        unsigned int slot;
        void *fresh, *old;

        if (stride <= pane->stride)
                return;

        lines = WINDOW_PIXELS + (unsigned long)pane->history * stride *
                                    sizeof(struct window_cell);
        bytes = PAGE_ALIGN(lines + (unsigned long)pane->history * sizeof(unsigned int));

        if (canvas_pane_bytes - pane->bytes + bytes > canvas_pane_budget())
                return;

        fresh = vzalloc(bytes);
        if (!fresh)
                return;

        cells = fresh + WINDOW_PIXELS;
        lengths = fresh + lines;

        spin_lock_irqsave(&console_cells, flags);

        old = pane->mapping;
        memory_copy_apart(fresh, old, WINDOW_PIXELS);

        for (slot = 0; slot < pane->history; slot++)
        {
                unsigned int length = min(pane->lengths[slot], pane->stride);

                memory_copy_apart(cells + (unsigned long)slot * stride,
                                  pane->cells + (unsigned long)slot * pane->stride,
                                  (positive)length * sizeof(struct window_cell));
                lengths[slot] = length;
        }

        ((struct window *)fresh)->stride = stride;
        ((struct window *)fresh)->lines = (unsigned int)lines;
        ((struct window *)fresh)->mapping = (unsigned int)bytes;

        pane->mapping = fresh;
        pane->cells = cells;
        pane->lengths = lengths;
        pane->stride = stride;
        canvas_pane_bytes += bytes - pane->bytes;
        pane->bytes = bytes;

        if (window == old)
                window = fresh;

        spin_unlock_irqrestore(&console_cells, flags);

        vfree(old);
        pr_info("[moonwater canvas] " "kernel log ring recut to %ux%u (%lu KiB)\n",
                stride, pane->history, bytes >> 10);
}

static void console_regrid(struct pane *pane)
{
        unsigned long flags;

        if (pane != READ_ONCE(console_pane) || !pane->cells)
                return;

        console_recut(pane);

        spin_lock_irqsave(&console_cells, flags);

        window->columns = pane->columns;
        window->rows = pane->rows;

        regrid(-1);

        pane->head = window->head;

        spin_unlock_irqrestore(&console_cells, flags);
}

static void console_stop(void)
{
        struct pane *pane;

        if (!console_registered)
                return;

        unregister_console(&canvas_console);
        console_registered = false;

        // unregister_console drains callbacks. This owned pane has no file
        // context whose release could return its ring to the desktop budget.
        rt_mutex_lock(&desktop.lock);
        pane = console_pane;
        WRITE_ONCE(console_pane, NULL);
        if (pane)
                pane_free(pane);
        rt_mutex_unlock(&desktop.lock);
}

/* ---- compose: composing a window of cells into the shapes that are drawn ---- */

/*
        Canvas -- compose

        Panes are in desktop coordinates and outputs are rectangles of the
        desktop, so composing one output is drawing the desktop offset by where
        that output sits. Back to front, which is the list order, which
        pane_by_z keeps in z order.

        Everything takes a clip in output coordinates. Repainting damage rather
        than a whole screen is the reason: a pane that merely overlaps the
        damage would otherwise repaint all of itself, over whatever was drawn
        above it outside that rectangle.

        A pane is drawn as bands of one rounded rectangle -- the frame, the
        titlebar, the contents -- so every band's corners follow the same
        curve, and a band that is not at a corner is a plain run of pixels.
*/

// Whether any of these desktop rectangles reaches this output. Early, because
// it is the one overlap question everything from cursor cells to damage asks.
static PURE _Bool output_touched(struct output *output, const struct drm_rect *damage,
                                 unsigned int count)
{
        struct drm_rect screen;
        unsigned int i;

        drm_rect_init(&screen, output->x, output->y, (int)output->width,
                      (int)output->height);
        for (i = 0; i < count; i++)
                if (drm_rects_overlap(&damage[i], &screen))
                        return true;

        return false;
}

struct shape
{
        int x, y, w, h;
        int radius;
};

// One run of one row, already clipped. Everything that draws ends here.
static void target_row(const struct target *t, int y, int x1, int x2, u32 colour)
{
        if ((unsigned int)y >= (unsigned int)t->height)
                return;

        x1 = max(x1, 0);
        x2 = min(x2, t->width);
        if (x2 <= x1)
                return;

        target_mark((unsigned long)(x2 - x1));
        if (t->simd)
                TARGET_PICK(t, canvas_rect_fill)(t->pixels + (size_t)y * t->pitch + x1,
                                                 0, (unsigned long)(x2 - x1), 1,
                                                 colour);
        else
                memory_fill_u32(t->pixels + (size_t)y * t->pitch + x1,
                                (unsigned long)(x2 - x1), colour);
}

// Clipped solid rectangles share one accounting and strided-store floor.
static void target_rectangle(const struct target *t, int x, int y, int w, int h,
                              u32 colour)
{
        if (x < 0)
        {
                w += x;
                x = 0;
        }
        if (y < 0)
        {
                h += y;
                y = 0;
        }
        w = min(w, t->width - x);
        h = min(h, t->height - y);
        if (w <= 0 || h <= 0)
                return;
        target_mark((unsigned long)w * h);
        TARGET_PICK(t, canvas_rect_fill)(t->pixels + (size_t)y * t->pitch + x,
                                         t->pitch, (unsigned long)w,
                                         (unsigned long)h, colour);
}

/*
        The run of one row of a shape that survives its band and the clip, or
        false when nothing does.

        This is the whole of what the rounded corners cost: an inset per row,
        and every band measured from there rather than from the edge.
*/
static _Bool shape_span(const struct target *t, const struct shape *shape,
                        int band_x, int band_w, int y, int *x1, int *x2)
{
        int inset;

        if (y < max(t->clip.y1, 0) || y >= min(t->clip.y2, t->height))
                return false;

        inset = round_inset(y - shape->y, shape->h, shape->radius);

        *x1 = max(max(shape->x + inset, band_x), t->clip.x1);
        *x2 = min(min(shape->x + shape->w - inset, band_x + band_w),
                  min(t->clip.x2, t->width));

        return *x2 > *x1;
}

/*
        A band of the shape.

        Split three ways, because only the rows inside a corner have an inset
        and only they need looking at one at a time. The straight middle is one
        rectangle and goes out as one call: a window's two sides are two pixels
        wide and a hundred and ninety rows tall, which was 4560 calls a compose
        before this, each of them to write two pixels.
*/
static _Bool shape_band_rows(const struct target *t, int band_y, int band_h,
                             int *top, int *bottom)
{
        *top = max(max(band_y, t->clip.y1), 0);
        *bottom = min(min(band_y + band_h, t->clip.y2), t->height);
        return *bottom > *top;
}

static void shape_fill_rows(const struct target *t, const struct shape *shape,
                            int band_x, int band_w, int y, int stop, u32 colour)
{
        int x1, x2;

        for (; y < stop; y++)
                if (shape_span(t, shape, band_x, band_w, y, &x1, &x2))
                        target_row(t, y, x1, x2, colour);
}

static void shape_fill(const struct target *t, const struct shape *shape,
                       int band_x, int band_y, int band_w, int band_h, u32 colour)
{
        int top, bottom, x1, x2;
        int curve_top, curve_bottom;

        if (!shape_band_rows(t, band_y, band_h, &top, &bottom))
                return;

        // Clamped to the band, not just to the shape: a titlebar starts below
        // the shape's top, and a rectangle measured from the shape would paint
        // the whole window.
        curve_top = clamp(shape->y + shape->radius, top, bottom);
        curve_bottom = clamp(shape->y + shape->h - shape->radius, top, bottom);

        shape_fill_rows(t, shape, band_x, band_w, top, curve_top, colour);

        if (curve_bottom > curve_top)
        {
                x1 = max(max(shape->x, band_x), t->clip.x1);
                x2 = min(min(shape->x + shape->w, band_x + band_w),
                         min(t->clip.x2, t->width));

                target_rectangle(t, x1, curve_top, x2 - x1,
                                  curve_bottom - curve_top, colour);
        }

        shape_fill_rows(t, shape, band_x, band_w, curve_bottom, bottom, colour);
}

static void shape_blit(const struct target *t, const struct shape *shape,
                       int band_x, int band_y, int band_w, int band_h,
                       const u32 *source, unsigned int source_pitch)
{
        int top, bottom, y, x1, x2;

        if (!shape_band_rows(t, band_y, band_h, &top, &bottom))
                return;

        for (y = top; y < bottom; y++)
        {
                if (!shape_span(t, shape, band_x, band_w, y, &x1, &x2))
                        continue;

                target_mark((unsigned long)(x2 - x1));
                TARGET_PICK(t, canvas_row_blit)(
                        t->pixels + (size_t)y * t->pitch + x1,
                        source + (size_t)(y - band_y) * source_pitch + (x1 - band_x),
                        (unsigned long)(x2 - x1), t->opaque);
        }
}

#define BX_N 1u
#define BX_S 2u
#define BX_W 4u
#define BX_E 8u

static void glyph_hline(unsigned char *bits, unsigned int y, unsigned char mask)
{
        if (y < WINDOW_CELL_H)
                bits[y] |= mask;
}

static void glyph_vline(unsigned char *bits, unsigned int x0, unsigned int x1,
                        unsigned int y0, unsigned int y1)
{
        unsigned int y;
        unsigned char mask = 0;

        while (x0 <= x1 && x0 < 8)
        {
                mask |= (unsigned char)(0x80u >> x0);
                x0++;
        }

        if (y1 >= WINDOW_CELL_H)
                y1 = WINDOW_CELL_H - 1;
        for (y = y0; y <= y1; y++)
                bits[y] |= mask;
}

static void glyph_box_nsew(unsigned char *bits, unsigned int nsew, unsigned int thick)
{
        unsigned int mid_y = 7;
        unsigned int mid_x = 3;
        unsigned char hmask = 0;
        unsigned int y;

        if (nsew & BX_W)
                hmask |= (unsigned char)(0xffu << (8 - (mid_x + 1 + thick)));
        if (nsew & BX_E)
                hmask |= (unsigned char)(0xffu >> mid_x);
        if ((nsew & (BX_W | BX_E)) == (BX_W | BX_E))
                hmask = 0xff;

        for (y = 0; y < thick; y++)
                glyph_hline(bits, mid_y + y, hmask);

        if (nsew & BX_N)
                glyph_vline(bits, mid_x, mid_x + thick - 1, 0, mid_y + thick - 1);
        if (nsew & BX_S)
                glyph_vline(bits, mid_x, mid_x + thick - 1, mid_y, WINDOW_CELL_H - 1);
}

/*
        Which edges each of U+2500 to U+257F draws.

        The block is a chart and this is the chart: the code point's low seven
        bits pick the row, and a row is the edges that meet in the middle of
        the cell. It was a hundred and twenty five cases returning fifteen
        answers between them, which is a table spelled out at four lines an
        entry. Weight is not in here -- the light, heavy, dashed and double
        spellings of a join all draw the same edges, and glyph_synthesize asks
        the code point itself how thick to draw them. U+2571 to U+2573 are the
        diagonals, which no combination of edges describes, so they answer
        nothing and fall through to the face.
*/
static const unsigned char box_nsew[0x80] = {
    [0x00 ... 0x01]    = BX_W | BX_E,
    [0x02 ... 0x03]    = BX_N | BX_S,
    [0x04 ... 0x05]    = BX_W | BX_E,
    [0x06 ... 0x07]    = BX_N | BX_S,
    [0x08 ... 0x09]    = BX_W | BX_E,
    [0x0a ... 0x0b]    = BX_N | BX_S,
    [0x0c ... 0x0f]    = BX_S | BX_E,
    [0x10 ... 0x13]    = BX_S | BX_W,
    [0x14 ... 0x17]    = BX_N | BX_E,
    [0x18 ... 0x1b]    = BX_N | BX_W,
    [0x1c ... 0x23]    = BX_N | BX_S | BX_E,
    [0x24 ... 0x2b]    = BX_N | BX_S | BX_W,
    [0x2c ... 0x33]    = BX_S | BX_W | BX_E,
    [0x34 ... 0x3b]    = BX_N | BX_W | BX_E,
    [0x3c ... 0x4b]    = BX_N | BX_S | BX_W | BX_E,
    [0x4c ... 0x4d]    = BX_W | BX_E,
    [0x4e ... 0x4f]    = BX_N | BX_S,
    [0x50]             = BX_W | BX_E,
    [0x51]             = BX_N | BX_S,
    [0x52 ... 0x54]    = BX_S | BX_E,
    [0x55 ... 0x57]    = BX_S | BX_W,
    [0x58 ... 0x5a]    = BX_N | BX_E,
    [0x5b ... 0x5d]    = BX_N | BX_W,
    [0x5e ... 0x60]    = BX_N | BX_S | BX_E,
    [0x61 ... 0x63]    = BX_N | BX_S | BX_W,
    [0x64 ... 0x66]    = BX_S | BX_W | BX_E,
    [0x67 ... 0x69]    = BX_N | BX_W | BX_E,
    [0x6a ... 0x6c]    = BX_N | BX_S | BX_W | BX_E,
    [0x6d]             = BX_S | BX_E,
    [0x6e]             = BX_S | BX_W,
    [0x6f]             = BX_N | BX_W,
    [0x70]             = BX_N | BX_E,
    [0x74]             = BX_W,
    [0x75]             = BX_N,
    [0x76]             = BX_E,
    [0x77]             = BX_S,
    [0x78]             = BX_W,
    [0x79]             = BX_N,
    [0x7a]             = BX_E,
    [0x7b]             = BX_S,
    [0x7c]             = BX_W | BX_E,
    [0x7d]             = BX_N | BX_S,
    [0x7e]             = BX_W | BX_E,
    [0x7f]             = BX_N | BX_S,
};

static unsigned int glyph_box_nsew_from(unsigned int c)
{
        return c - 0x2500 < sizeof(box_nsew) ? box_nsew[c - 0x2500] : 0;
}

/*
        The block elements, each of which is one rectangle of the cell filled.

        A run of rows and a column mask is the whole of what any of them is,
        so they are a table of the two rather than a test apiece: 2581 to 2587
        are the eighths growing up from the floor, 2589 to 258f the eighths
        growing in from the right edge -- which is where 258c, the left half,
        already came from -- and the quadrants and the one eighth bars are
        each a rectangle of their own. The shades are the exception, being a
        pattern per row rather than a fill, and anything with no rectangle
        here is the full block: that is what the quadrant pairs 2599 to 259c,
        259e and 259f have always been drawn as.
*/
static void glyph_block(unsigned int c, unsigned char *bits)
{
        static const struct { unsigned char from, rows, mask; } filled[0x20] = {
            [0x00] = {0, 8, 0xff},   [0x01] = {14, 2, 0xff},
            [0x02] = {12, 4, 0xff},  [0x03] = {10, 6, 0xff},
            [0x04] = {8, 8, 0xff},   [0x05] = {6, 10, 0xff},
            [0x06] = {4, 12, 0xff},  [0x07] = {2, 14, 0xff},
            [0x08] = {0, 16, 0xff},  [0x09] = {0, 16, 0xfe},
            [0x0a] = {0, 16, 0xfc},  [0x0b] = {0, 16, 0xf8},
            [0x0c] = {0, 16, 0xf0},  [0x0d] = {0, 16, 0xe0},
            [0x0e] = {0, 16, 0xc0},  [0x0f] = {0, 16, 0x80},
            [0x10] = {0, 16, 0x0f},  [0x14] = {0, 2, 0xff},
            [0x15] = {0, 16, 0x01},  [0x16] = {8, 8, 0xf0},
            [0x17] = {8, 8, 0x0f},   [0x18] = {0, 8, 0xf0},
            [0x1d] = {0, 8, 0x0f},
        };
        unsigned int at = c - 0x2580;
        unsigned int y;

        if (c >= 0x2591 && c <= 0x2593)
        {
                unsigned char shade = c == 0x2591 ? 0x44 : c == 0x2592 ? 0xaa : 0xee;

                for (y = 0; y < WINDOW_CELL_H; y++)
                        bits[y] = (unsigned char)(y & 1 ? shade >> 1 : shade);
                return;
        }

        if (at < sizeof(filled) / sizeof(filled[0]) && filled[at].rows)
                memory_fill(bits + filled[at].from, filled[at].mask,
                            filled[at].rows);
        else
                memory_fill(bits, 0xff, WINDOW_CELL_H);
}

static void glyph_braille(unsigned int dots, unsigned char *bits)
{
        static const unsigned char ox[8] = {1, 1, 1, 4, 4, 4, 1, 4};
        static const unsigned char oy[8] = {1, 5, 9, 1, 5, 9, 13, 13};
        unsigned int i, x, y;

        for (i = 0; i < 8; i++)
        {
                if (!(dots & (1u << i)))
                        continue;
                for (y = 0; y < 3; y++)
                        for (x = 0; x < 2; x++)
                                bits[oy[i] + y] |=
                                    (unsigned char)(0x80u >> (ox[i] + x));
        }
}

static void glyph_tofu(unsigned char *bits)
{
        unsigned int y;

        bits[1] = 0x7e;
        bits[WINDOW_CELL_H - 2] = 0x7e;
        for (y = 2; y < WINDOW_CELL_H - 2; y++)
                bits[y] = 0x42;
}

// One half of a box two cells wide, for a double-width character the face
// has no glyph for.
static void glyph_tofu_half(unsigned char *bits, _Bool right)
{
        unsigned int y;

        memory_fill(bits, 0, WINDOW_CELL_H);
        bits[1] = right ? 0xfe : 0x7f;
        bits[WINDOW_CELL_H - 2] = bits[1];
        for (y = 2; y < WINDOW_CELL_H - 2; y++)
                bits[y] = right ? 0x02 : 0x40;
}

/*
        What the VGA face already draws.

        The cell font is the IBM VGA ROM face, 256 glyphs in code page 437
        order. Past ASCII its slots hold accented Latin, Greek, arrows, card
        suits and maths, so a character with one of those shapes is drawn from
        the font rather than as a question mark. Box drawing and the blocks are
        not here: glyph_synthesize draws those to the edges of the cell so that
        neighbours join. Sorted by character for the search.
*/
static const struct
{
        unsigned short character;
        unsigned char glyph;
} glyph_face[] = {
    {0x00a0, 0xff}, {0x00a1, 0xad}, {0x00a2, 0x9b}, {0x00a3, 0x9c},
    {0x00a5, 0x9d}, {0x00a7, 0x15}, {0x00aa, 0xa6}, {0x00ab, 0xae},
    {0x00ac, 0xaa}, {0x00b0, 0xf8}, {0x00b1, 0xf1}, {0x00b5, 0xe6},
    {0x00b6, 0x14}, {0x00b7, 0xfa}, {0x00ba, 0xa7}, {0x00bb, 0xaf},
    {0x00bc, 0xac}, {0x00bd, 0xab}, {0x00bf, 0xa8}, {0x00c4, 0x8e},
    {0x00c5, 0x8f}, {0x00c6, 0x92}, {0x00c7, 0x80}, {0x00c9, 0x90},
    {0x00d1, 0xa5}, {0x00d6, 0x99}, {0x00dc, 0x9a}, {0x00df, 0xe1},
    {0x00e0, 0x85}, {0x00e1, 0xa0}, {0x00e2, 0x83}, {0x00e4, 0x84},
    {0x00e5, 0x86}, {0x00e6, 0x91}, {0x00e7, 0x87}, {0x00e8, 0x8a},
    {0x00e9, 0x82}, {0x00ea, 0x88}, {0x00eb, 0x89}, {0x00ec, 0x8d},
    {0x00ed, 0xa1}, {0x00ee, 0x8c}, {0x00ef, 0x8b}, {0x00f1, 0xa4},
    {0x00f2, 0x95}, {0x00f3, 0xa2}, {0x00f4, 0x93}, {0x00f6, 0x94},
    {0x00f7, 0xf6}, {0x00f9, 0x97}, {0x00fa, 0xa3}, {0x00fb, 0x96},
    {0x00fc, 0x81}, {0x00ff, 0x98}, {0x0192, 0x9f}, {0x0393, 0xe2},
    {0x0398, 0xe9}, {0x03a3, 0xe4}, {0x03a6, 0xe8}, {0x03a9, 0xea},
    {0x03b1, 0xe0}, {0x03b4, 0xeb}, {0x03b5, 0xee}, {0x03c0, 0xe3},
    {0x03c3, 0xe5}, {0x03c4, 0xe7}, {0x03c6, 0xed}, {0x2022, 0x07},
    {0x203c, 0x13}, {0x207f, 0xfc}, {0x20a7, 0x9e}, {0x2190, 0x1b},
    {0x2191, 0x18}, {0x2192, 0x1a}, {0x2193, 0x19}, {0x2194, 0x1d},
    {0x2195, 0x12}, {0x21a8, 0x17}, {0x2219, 0xf9}, {0x221a, 0xfb},
    {0x221e, 0xec}, {0x221f, 0x1c}, {0x2229, 0xef}, {0x2248, 0xf7},
    {0x2261, 0xf0}, {0x2264, 0xf3}, {0x2265, 0xf2}, {0x2302, 0x7f},
    {0x2310, 0xa9}, {0x2320, 0xf4}, {0x2321, 0xf5}, {0x25a0, 0xfe},
    {0x25ac, 0x16}, {0x25b2, 0x1e}, {0x25ba, 0x10}, {0x25bc, 0x1f},
    {0x25c4, 0x11}, {0x25cb, 0x09}, {0x25d8, 0x08}, {0x25d9, 0x0a},
    {0x263a, 0x01}, {0x263b, 0x02}, {0x263c, 0x0f}, {0x2640, 0x0c},
    {0x2642, 0x0b}, {0x2660, 0x06}, {0x2663, 0x05}, {0x2665, 0x03},
    {0x2666, 0x04}, {0x266a, 0x0d}, {0x266b, 0x0e},
};

// The font's slot for a character past ASCII, or 0 when it has none.
static unsigned int glyph_in_face(unsigned int c)
{
        unsigned int low = 0, high = sizeof(glyph_face) / sizeof(glyph_face[0]);

        while (low < high)
        {
                unsigned int middle = (low + high) / 2;

                if (glyph_face[middle].character < c)
                        low = middle + 1;
                else
                        high = middle;
        }

        return low < sizeof(glyph_face) / sizeof(glyph_face[0]) &&
                       glyph_face[low].character == c
                   ? glyph_face[low].glyph
                   : 0;
}

/*
        A digit raised or lowered: the face's own digit at half size, each
        pair of rows and of columns folded into one, in the top or the bottom
        half of the cell. btop names every box with a superscript digit.
*/
static __attribute__((__cold__)) _Bool
glyph_script_digit(unsigned int c, const unsigned char *face,
                   size_t glyph_size, unsigned char *bits)
{
        static const unsigned short raised[10] = {
            0x2070, 0x00b9, 0x00b2, 0x00b3, 0x2074,
            0x2075, 0x2076, 0x2077, 0x2078, 0x2079};
        const unsigned char *from;
        unsigned int digit = 0, top = 0, y, x;

        if (c >= 0x2080 && c <= 0x2089)
        {
                digit = c - 0x2080;
                top = WINDOW_CELL_H / 2;
        }
        else
        {
                while (digit < 10 && raised[digit] != c)
                        digit++;
                if (digit == 10)
                        return false;
        }

        from = face + (size_t)('0' + digit) * glyph_size;
        memory_fill(bits, 0, WINDOW_CELL_H);

        for (y = 0; y < WINDOW_CELL_H / 2; y++)
        {
                unsigned char pair = (unsigned char)(from[2 * y] | from[2 * y + 1]);
                unsigned char half = 0;

                for (x = 0; x < WINDOW_CELL_W / 2; x++)
                        if (pair & (0xc0 >> (2 * x)))
                                half |= (unsigned char)(0x20 >> x);

                bits[top + y] = half;
        }

        return true;
}

static __attribute__((__cold__)) _Bool
glyph_synthesize(unsigned int c, unsigned char *bits)
{
        unsigned int nsew;

        memory_fill(bits, 0, WINDOW_CELL_H);

        if (c >= 0x2800 && c <= 0x28ff)
        {
                glyph_braille(c - 0x2800, bits);
                return true;
        }

        if (c >= 0x2580 && c <= 0x259f)
        {
                glyph_block(c, bits);
                return true;
        }

        if (c >= 0x2500 && c <= 0x257f)
        {
                nsew = glyph_box_nsew_from(c);
                if (!nsew)
                {
                        // The two diagonals, which are no arrangement of
                        // edges: one pixel a row, stepping across the cell in
                        // whichever direction the code point leans.
                        if (c == 0x2571 || c == 0x2572)
                        {
                                for (unsigned int y = 0; y < 8; y++)
                                        bits[2 * y] = (unsigned char)(
                                            c == 0x2571 ? 1u << y : 0x80u >> y);
                                return true;
                        }
                        glyph_tofu(bits);
                        return true;
                }
                glyph_box_nsew(bits, nsew, (c == 0x2501 || c == 0x2503 ||
                                            (c >= 0x2550 && c <= 0x256c))
                                               ? 2
                                               : 1);
                return true;
        }

        if (c == 0x25e6)
        {
                bits[7] = 0x18;
                bits[8] = 0x18;
                return true;
        }

        if (c == 0x2260)
        {
                bits[3] = 0x02;
                bits[4] = 0x7e;
                bits[5] = 0x04;
                bits[6] = 0x08;
                bits[7] = 0x7e;
                bits[8] = 0x10;
                bits[9] = 0x20;
                return true;
        }

        if (c == 0x25ae || c == 0x25fc || c == 0x25fe)
        {
                memory_fill(bits + 3, 0x7e, 10);
                return true;
        }

        if (c == 0x25c6)
        {
                bits[3] = 0x18;
                bits[4] = 0x3c;
                bits[5] = 0x7e;
                bits[6] = 0xff;
                bits[7] = 0xff;
                bits[8] = 0x7e;
                bits[9] = 0x3c;
                bits[10] = 0x18;
                return true;
        }

        // The four horizontal scan lines a VT100 drew, one row each and the
        // rows they sat on.
        if (c >= 0x23ba && c <= 0x23bd)
        {
                static const unsigned char scan[4] = {0, 5, 10, 15};

                bits[scan[c - 0x23ba]] = 0xff;
                return true;
        }

        if (c == 0xfffd)
        {
                glyph_tofu(bits);
                return true;
        }

        return false;
}

static void glyph_apply_style(unsigned char *bits, unsigned short flags)
{
        unsigned int y;

        if (flags & WINDOW_CELL_ITALIC)
                for (y = 0; y < 8; y++)
                        bits[y] = (unsigned char)(bits[y] >> 1);

        if (flags & WINDOW_CELL_BOLD)
                for (y = 0; y < WINDOW_CELL_H; y++)
                        bits[y] |= (unsigned char)(bits[y] >> 1);

        if (flags & WINDOW_CELL_STRIKE)
                bits[7] |= 0xff;

        if (flags & WINDOW_CELL_UNDERLINE)
        {
                bits[WINDOW_CELL_H - 2] |= 0xff;
                bits[WINDOW_CELL_H - 1] |= 0xff;
        }

        if (flags & WINDOW_CELL_BAR)
                for (y = 0; y < WINDOW_CELL_H; y++)
                        bits[y] |= 0x80;
}

static u32 cell_palette(unsigned char index, u32 opaque)
{
        return canvas_terminal[index] | opaque;
}

static u32 cell_ink_colour(const struct window_cell *cell, u32 opaque)
{
        u32 ink = cell_palette(cell->ink, opaque);
        u32 paper;

        if (!(cell->flags & WINDOW_CELL_DIM))
                return ink;

        paper = cell_palette(cell->paper, opaque);
        return ((ink & 0xfefefe) >> 1) + ((paper & 0xfefefe) >> 1);
}

/*
        One cell, background and glyph together.

        The whole point is the single pass: filling the paper and then drawing
        the letter over it writes most of the cell twice, and the display is
        reading the buffer while that happens. A cell at the edge of the damage
        or inside a rounded corner still goes the long way round, where a
        double write is worth more than the case is worth handling.
*/
static void cell_draw(const struct target *t, const struct shape *shape,
                      int x, int y, const unsigned char *bits, _Bool direct,
                      u32 ink, u32 paper)
{
        if (direct && bits && x >= max(t->clip.x1, 0) &&
            x + canvas_cell_w <= min(t->clip.x2, t->width))
        {
                target_mark((unsigned long)canvas_cell_w *
                            (unsigned long)canvas_cell_h);
                if (desktop.scale == 1)
                        TARGET_PICK(t, canvas_cell)(
                                t->pixels + (size_t)y * t->pitch + x,
                                t->pitch, bits, WINDOW_CELL_H, ink, paper);
                else
                        TARGET_PICK(t, canvas_cell2)(
                                t->pixels + (size_t)y * t->pitch + x,
                                t->pitch, bits, WINDOW_CELL_H, ink, paper);
                return;
        }

        shape_fill(t, shape, x, y, canvas_cell_w, canvas_cell_h, paper);
        if (bits)
                bits_draw(t, x, y, (int)desktop.scale, bits, 1, WINDOW_CELL_W,
                          WINDOW_CELL_H, ink);
}

/*
        Blank cells in one colour, count of them from x.

        One alone is a cell of paper, and drawn as one: the rectangle's clip,
        corner and row walk cost five hundred instructions for sixteen rows
        of eight pixels, and a row whose background changes every cell was
        all of them. Out of line for the reason compose_run is.
*/
static noinline void blank_fill(const struct target *t,
                                const struct shape *shape, int x, int y,
                                int count, u32 paper, _Bool direct)
{
        static const unsigned char blank[WINDOW_CELL_H];

        if (count == 1 && direct && x >= max(t->clip.x1, 0) &&
            x + canvas_cell_w <= min(t->clip.x2, t->width))
        {
                u32 *at = t->pixels + (size_t)y * t->pitch + x;

                target_mark((unsigned long)canvas_cell_w *
                            (unsigned long)canvas_cell_h);
                if (desktop.scale == 1)
                        TARGET_PICK(t, canvas_cell)(at, t->pitch, blank,
                                                    WINDOW_CELL_H, paper, paper);
                else
                        TARGET_PICK(t, canvas_cell2)(at, t->pitch, blank,
                                                     WINDOW_CELL_H, paper, paper);
                return;
        }

        shape_fill(t, shape, x, y, count * canvas_cell_w, canvas_cell_h, paper);
}

/*
        A run of printable ASCII in one pair of colours from column, scale one
        and wholly inside the damage: one call, one table of colour pairs,
        drawn a scanline at a time. Answers the cells drawn, or 0 for a run of
        one, which is canvas_cell's -- it builds the same table for one cell
        without the walk, and text whose colour changes every character is
        all runs of one. A space inside the run is the face's blank glyph;
        two in a row end it, so a stretch of blanks is still a rectangle.

        Out of line, because compose_row's loop is every other kind of cell
        as well, and a run's walk laid out inside it cost those a tenth.
*/
static noinline int compose_run(const struct target *t, int x, int y,
                                const struct window_cell *cells, int column,
                                int used, const unsigned char *font_data,
                                _Bool space_blank)
{
        unsigned char ink_index = cells[column].ink;
        unsigned char paper_index = cells[column].paper;
        int left = x + column * canvas_cell_w;
        int stop = min(used, (min(t->clip.x2, t->width) - x) / canvas_cell_w);
        int run;

        if (left < max(t->clip.x1, 0) || stop <= column + 1 ||
            cells[column + 1].flags || cells[column + 1].ink != ink_index ||
            cells[column + 1].paper != paper_index ||
            cells[column + 1].character - 33 >= 94)
                return 0;

        for (run = column + 2; run < stop; run++)
        {
                unsigned int next = cells[run].character;

                if (cells[run].flags || cells[run].ink != ink_index ||
                    cells[run].paper != paper_index || next - 32 >= 95 ||
                    (next == ' ' && (!space_blank || run + 1 >= stop ||
                                     cells[run + 1].character <= ' ')))
                        break;
        }

        target_mark((unsigned long)(run - column) *
                    (unsigned long)canvas_cell_w * (unsigned long)canvas_cell_h);
        TARGET_PICK(t, canvas_cells)(
                t->pixels + (size_t)y * t->pitch + left, t->pitch,
                font_data, cells + column, (unsigned long)(run - column),
                cell_palette(ink_index, t->opaque),
                cell_palette(paper_index, t->opaque));
        return run - column;
}

/*
        One row of a window made of text.

        A cell with a letter in it is drawn whole, one pixel one store. Runs of
        blank cells sharing a background go out as one rectangle, since a
        terminal is mostly empty and a rectangle is what the fill is fastest
        at.
*/
static HOT void compose_row(const struct target *t, const struct shape *shape,
                            int x, int y, const struct window_cell *cells,
                            int used, int first, int last)
{
        int column = first;
        int cell_w = canvas_cell_w;
        const unsigned char *font_data = NULL;
        size_t glyph_size = 0;
        _Bool space_blank = false, runs;
        _Bool direct = glyph_is_cell() &&
                       (desktop.scale == 1 || desktop.scale == 2) &&
                       y >= max(t->clip.y1, 0) &&
                       y + canvas_cell_h <= min(t->clip.y2, t->height) &&
                       !round_inset(y - shape->y, shape->h, shape->radius) &&
                       !round_inset(y + canvas_cell_h - 1 - shape->y,
                                    shape->h, shape->radius);

        if (glyph_is_cell() && canvas_font)
        {
                font_data = font_data_buf(canvas_font->data);
                glyph_size = font_glyph_size(canvas_font->width,
                                             canvas_font->height);
        }

        runs = direct && font_data && desktop.scale == 1;

        /*
                A space is paper whatever the face has at ' ', so a run takes
                one in only where the face's own space is blank, which the
                kernel's fonts all are.
        */
        if (runs)
        {
                u64 space[2];

                memory_copy(space, font_data + ' ' * WINDOW_CELL_H, sizeof(space));
                space_blank = !(space[0] | space[1]);
        }

        while (column < used)
        {
                unsigned int character = cells[column].character;
                unsigned short flags = cells[column].flags;
                u32 paper = cell_palette(cells[column].paper, t->opaque);
                u32 ink;
                unsigned char made[WINDOW_CELL_H];
                const unsigned char *bits;
                _Bool styled;
                int run;

                styled = (flags & (WINDOW_CELL_BOLD | WINDOW_CELL_ITALIC |
                                   WINDOW_CELL_UNDERLINE | WINDOW_CELL_STRIKE |
                                   WINDOW_CELL_HIDDEN | WINDOW_CELL_DIM |
                                   WINDOW_CELL_BAR)) != 0;

                // The neighbour first, here: text whose colour changes every
                // character never pays for the call.
                if (runs && !flags && character - 33 < 94 &&
                    column + 1 < used &&
                    !((cells[column + 1].ink ^ cells[column].ink) |
                      (cells[column + 1].paper ^ cells[column].paper) |
                      cells[column + 1].flags) &&
                    cells[column + 1].character - 33 < 94)
                {
                        run = compose_run(t, x, y, cells, column, used,
                                          font_data, space_blank);

                        if (run)
                        {
                                column += run;
                                continue;
                        }
                }

                if (character <= ' ' && !styled)
                {
                        for (run = column + 1; run < used; run++)
                        {
                                if (cells[run].character > ' ' ||
                                    cells[run].flags ||
                                    cell_palette(cells[run].paper, t->opaque) !=
                                        paper)
                                        break;
                        }

                        blank_fill(t, shape, x + column * cell_w, y,
                                   run - column, paper, direct);
                        column = run;
                        continue;
                }

                bits = NULL;
                if (!(flags & WINDOW_CELL_HIDDEN) && character > ' ')
                {
                        unsigned int glyph = character <= 126
                                                 ? character
                                                 : glyph_in_face(character);

                        if (glyph && font_data && !styled)
                                bits = font_data + (size_t)glyph * glyph_size;
                        else
                        {
                                if (glyph && font_data)
                                        memory_copy(made,
                                                    font_data +
                                                        (size_t)glyph *
                                                            glyph_size,
                                                    WINDOW_CELL_H);
                                else if (flags & (WINDOW_CELL_WIDE |
                                                  WINDOW_CELL_WIDE_RIGHT))
                                        glyph_tofu_half(
                                            made,
                                            (flags & WINDOW_CELL_WIDE_RIGHT) != 0);
                                else if (!glyph_synthesize(character, made) &&
                                         !(font_data &&
                                           glyph_script_digit(character,
                                                              font_data,
                                                              glyph_size,
                                                              made)))
                                {
                                        if (font_data)
                                                memory_copy(made,
                                                            font_data +
                                                                (size_t)'?' *
                                                                    glyph_size,
                                                            WINDOW_CELL_H);
                                        else
                                                glyph_tofu(made);
                                }
                                glyph_apply_style(made, flags);
                                bits = made;
                        }
                }
                else if (flags & (WINDOW_CELL_UNDERLINE | WINDOW_CELL_STRIKE |
                                  WINDOW_CELL_BAR))
                {
                        memory_fill(made, 0, WINDOW_CELL_H);
                        glyph_apply_style(made, flags);
                        bits = made;
                }

                ink = cell_ink_colour(&cells[column], t->opaque);
                cell_draw(t, shape, x + column * cell_w, y, bits, direct, ink,
                          paper);
                column++;
        }

        if (column < last)
                shape_fill(t, shape, x + column * cell_w, y,
                           (last - column) * cell_w, canvas_cell_h,
                           cell_palette(0, t->opaque));
}

/*
        A window made of text.

        The rows are a window onto a ring of lines rather than the whole of a
        grid, so what is drawn is wherever the view is sitting -- the end of
        the ring while nothing has touched the wheel, and a line written long
        ago once something has.

        A line is folded into as many rows as it needs at the width the window
        is now, which is what makes a window widened re-wrap everything already
        in it. Only the rows the damage reaches are drawn; the walk down to
        them is a few additions a line and costs nothing beside a fill.
*/
static void compose_cells(struct pane *pane, const struct target *t,
                          const struct shape *shape, int x, int y)
{
        /*
                Both the width whoever owns the cells laid its lines out at and
                the room the window has now. They are the same at rest and not
                during a resize: a window that has shrunk still has the wider
                lines until whoever writes them catches up, and drawing all of
                one puts the inside of the window on the desktop beside it.
        */
        unsigned int width = max(pane->grid_columns, 1u);
        int columns = (int)min(width, pane->columns);
        int rows = (int)pane_rows(pane);

        int first_row = max((t->clip.y1 - y) / canvas_cell_h, 0);
        int last_row = min((t->clip.y2 - y + canvas_cell_h - 1) / canvas_cell_h, rows);

        // Columns as well as rows. Clipping only the rows meant a cursor
        // moving over a terminal repainted two whole lines of it, eighty
        // cells wide, to put sixteen pixels somewhere.
        int first = max((t->clip.x1 - x) / canvas_cell_w, 0);
        int last = min((t->clip.x2 - x + canvas_cell_w - 1) / canvas_cell_w, columns);
        unsigned int skip;
        unsigned int line;
        int row = 0;

        /*
                Frame and title damage reaches compose_pane too.  With no
                content cell inside the clip, walking the live view and every
                folded row can only arrive at compose_row calls that reject
                themselves.  Stop before any ring arithmetic instead.
        */
        if (first_row >= last_row || first >= last)
                return;

        canvas_terminal_prepare();

        line = pane_view_at(pane, pane->view, &skip);

        while (row < last_row && line != pane->head)
        {
                unsigned int slot = line % pane->history;
                // The program's word, taken once: it is writable behind us,
                // and the fold count and the run drawn in each fold are both
                // measured from this one. pane_length says the rest.
                unsigned int written = READ_ONCE(pane->lengths[slot]);
                unsigned int length = min(written, pane->stride);
                unsigned int folds = length ? (length + width - 1) / width : 1;
                const struct window_cell *cells =
                    pane->cells + (size_t)slot * pane->stride;
                unsigned int fold = skip;
                // A line whose cells have no memory behind them keeps its
                // rows, and they are drawn empty.
                unsigned int drawn = pane_cells_held(pane, slot, length) ? length : 0;

                if (row < first_row && folds > skip)
                {
                        unsigned int omitted = min((unsigned int)(first_row - row),
                                                   folds - skip);
                        fold += omitted;
                        row += omitted;
                }
                for (; fold < folds && row < last_row; fold++, row++)
                {
                        unsigned int from = fold * width;
                        int used = (int)min(drawn > from ? drawn - from : 0, width);

                        compose_row(t, shape, x, y + row * canvas_cell_h,
                                    cells + from,
                                    min(used, last), first, last);
                }

                skip = 0;
                line++;
        }

        // Below the newest line, for a window with more room in it than there
        // is anything to put there.
        for (row = max(row, first_row); row < last_row; row++)
                compose_row(t, shape, x, y + row * canvas_cell_h, NULL,
                            0, first, last);
}

/*
        The bar down the right of a window that has more than it is showing.

        The grid ends before its reserved gutter, so every cell stays visible.
        The gutter remains when everything fits; acquiring scrollback must not
        change the terminal's columns.
*/
struct pane_bar_geometry
{
        int x, y, width, height;
        int thumb_at, thumb_span;
        unsigned int total;
};

/*
        The bar, and the thumb in it, in the desktop's own coordinates.

        Drawn here and taken hold of in drag below, worked out in one place so the
        thumb a hand grabs is the thumb that was drawn. Answers false for a
        window with nothing to scroll, which is also the answer to whether
        there is anything there to press.
*/
static _Bool pane_bar(struct pane *pane, struct pane_bar_geometry *bar)
{
        unsigned int first, shown, total;
        struct drm_rect gutter;

        if (!pane_extent(pane, &first, &shown, &total) || !total)
                return false;

        pane_gutter_rect(pane, &gutter);
        bar->x = gutter.x1;
        bar->y = gutter.y1;
        bar->width = drm_rect_width(&gutter);
        bar->height = drm_rect_height(&gutter);
        bar->total = total;

        bar->thumb_span = max((int)((unsigned long)bar->height * shown / total),
                              canvas_cell_h);
        bar->thumb_at = (int)((unsigned long)bar->height * first / total);

        if (bar->thumb_at + bar->thumb_span > bar->height)
                bar->thumb_at = bar->height - bar->thumb_span;

        return true;
}

static void compose_bar(struct pane *pane, const struct target *t,
                        const struct shape *shape)
{
        struct pane_bar_geometry bar;
        struct drm_rect gutter;

        pane_gutter_rect(pane, &gutter);
        drm_rect_translate(&gutter, -t->x, -t->y);
        if (!drm_rects_overlap(&gutter, &t->clip))
                return;

        if (!pane_bar(pane, &bar))
                return;

        shape_fill(t, shape, bar.x - t->x, bar.y - t->y,
                   bar.width, bar.height, t->ink[INK_FRAME]);
        shape_fill(t, shape, bar.x - t->x, bar.y - t->y + bar.thumb_at,
                   bar.width, bar.thumb_span,
                   t->ink[INK_TITLE_LIT]);
}

static void compose_pane(struct pane *pane, const struct target *t)
{
        int title = pane_title(pane);
        int x = pane->x - t->x;
        int y = pane->y - t->y;
        int bottom = y + title + pane->height;
        struct shape shape;
        struct drm_rect frame, local_frame;
        int cx, cy, side, reserved;
        _Bool has_close;

        if (pane->style & WINDOW_MINIMIZED)
                return;

        pane_frame(pane, &frame);
        local_frame = frame;
        drm_rect_translate(&local_frame, -t->x, -t->y);

        /*
                Nothing at all for a window the damage does not touch, and for
                a cursor move that is every window but one. Without this every
                pane laid out its own text on every mouse move, whether or not
                a pixel of it could land.
        */
        if (!drm_rects_overlap(&local_frame, &t->clip))
                return;

        shape.x = local_frame.x1;
        shape.y = local_frame.y1;
        shape.w = drm_rect_width(&local_frame);
        shape.h = drm_rect_height(&local_frame);
        shape.radius = min(pane->edge, min(shape.w, shape.h) / 2);

        if (title)
        {
                u32 frame_ink = t->ink[INK_FRAME];
                int span = title + pane->height;
                struct {
                        int x, y, w, h;
                } chrome[] = {
                        {shape.x, shape.y, shape.w, y - shape.y},
                        {shape.x, bottom, shape.w, shape.y + shape.h - bottom},
                        {shape.x, y, x - shape.x, span},
                        {x + pane->width, y, shape.x + shape.w - (x + pane->width),
                         span},
                };
                unsigned int i;

                for (i = 0; i < ARRAY_SIZE(chrome); i++)
                        shape_fill(t, &shape, chrome[i].x, chrome[i].y,
                                   chrome[i].w, chrome[i].h, frame_ink);

                shape_fill(t, &shape, x, y, pane->width, title,
                           t->ink[pane->state & WINDOW_FOCUSED ? INK_TITLE_LIT
                                                              : INK_TITLE]);

                reserved = canvas_cell_w;
                has_close = pane_close_box(pane, &cx, &cy, &side);
                if (has_close)
                        reserved = side + canvas_border * 2;

                if (pane->title_length)
                        text_draw(t, x + canvas_cell_w, y,
                                  pane->width - canvas_cell_w - reserved, title,
                                  pane->title, pane->title_length,
                                  TEXT_CENTRE | TEXT_MIDDLE, (int)desktop.scale,
                                  t->ink[INK_TEXT]);

                if (has_close)
                        bits_draw(t, cx - t->x + (side - canvas_cell_w) / 2,
                                  cy - t->y + (side - canvas_cell_w) / 2,
                                  (int)desktop.scale, close_bits, 1, 8, 8,
                                  t->ink[INK_TEXT]);
        }

        if (pane->cells)
        {
                int gw = (int)min(pane->grid_columns, pane->columns) * canvas_cell_w;
                int gh = (int)min(pane->grid_rows, pane->rows) * canvas_cell_h;

                compose_cells(pane, t, &shape, x, y + title);

                // What the window has grown into but the program has not laid
                // out yet, which would otherwise show the desktop through it.
                if (gw < pane->width)
                        shape_fill(t, &shape, x + gw, y + title,
                                   pane->width - gw, pane->height, t->ink[INK_BODY]);

                if (gh < pane->height)
                        shape_fill(t, &shape, x, y + title + gh, min(gw, pane->width),
                                   pane->height - gh, t->ink[INK_BODY]);

                compose_bar(pane, t, &shape);
        }
        else if (pane->pixels)
                shape_blit(t, &shape, x, y + title, pane->width, pane->height,
                           pane->pixels, pane->pitch);
        else
                shape_fill(t, &shape, x, y + title, pane->width, pane->height,
                           t->ink[INK_BODY]);
}

/*
        The desktop, everywhere a window is not.

        There is one buffer and the display is scanning it, so a pixel written
        twice is a pixel seen twice: painting the background and then a window
        over it is a flash of the desktop through that window, and during a
        resize that is its whole body, sixty times a second. So the windows are
        cut out of the rectangle first and only what is left is painted.

        Windows are cut inset by their corner radius, which is the one place a
        window does not cover its own rectangle.
*/
#define DESKTOP_PIECES 8

static unsigned int rect_subtract(struct drm_rect *out, const struct drm_rect *a,
                                  const struct drm_rect *b)
{
        unsigned int n = 0;

        // An empty cut takes nothing away, and going the long way round for it
        // returns the whole of a as four pieces that then cost four slots.
        if (b->x2 <= b->x1 || b->y2 <= b->y1 ||
            b->x1 >= a->x2 || b->x2 <= a->x1 || b->y1 >= a->y2 || b->y2 <= a->y1)
        {
                out[0] = *a;
                return 1;
        }

        if (b->y1 > a->y1)
                drm_rect_init(&out[n++], a->x1, a->y1, a->x2 - a->x1, b->y1 - a->y1);

        if (b->y2 < a->y2)
                drm_rect_init(&out[n++], a->x1, b->y2, a->x2 - a->x1, a->y2 - b->y2);

        {
                int top = max(a->y1, b->y1);
                int bottom = min(a->y2, b->y2);

                if (b->x1 > a->x1)
                        drm_rect_init(&out[n++], a->x1, top, b->x1 - a->x1, bottom - top);

                if (b->x2 < a->x2)
                        drm_rect_init(&out[n++], b->x2, top, a->x2 - b->x2, bottom - top);
        }

        return n;
}

static void desktop_fill(const struct target *t, int x1, int y1, int x2, int y2)
{
        struct drm_rect storage[2][DESKTOP_PIECES];
        struct drm_rect *piece = storage[0], *spare = storage[1], *swap;
        unsigned int count = 1, i;
        struct pane *pane;

        drm_rect_init(&piece[0], x1, y1, x2 - x1, y2 - y1);

        list_for_each_entry_reverse(pane, &desktop.windows, link)
        {
                unsigned int kept = 0;
                struct drm_rect cut;
                struct drm_rect frame;
                int radius;

                if (!count)
                        return;

                if (pane->style & WINDOW_MINIMIZED)
                        continue;

                pane_frame(pane, &frame);
                radius = min(pane->edge, min(drm_rect_width(&frame),
                                             drm_rect_height(&frame)) / 2);
                drm_rect_init(&cut, frame.x1 + radius - t->x,
                         frame.y1 + radius - t->y,
                         drm_rect_width(&frame) - radius * 2,
                         drm_rect_height(&frame) - radius * 2);

                for (i = 0; i < count; i++)
                {
                        struct drm_rect part[4];
                        unsigned int n = rect_subtract(part, &piece[i], &cut);

                        /*
                                Past the array the pieces cost more than the
                                paint, so a split that would not leave room for
                                the pieces still to come is dropped and that
                                piece kept whole. What it counts is what the
                                cut actually made, not the four a cut can make
                                at worst: on the worst case a window several
                                windows down was never cut out at all, and the
                                desktop under it is a flash of the background
                                through it every compose.
                        */
                        if (kept + n + (count - i - 1) > DESKTOP_PIECES)
                        {
                                spare[kept++] = piece[i];
                                continue;
                        }

                        memory_copy_apart(&spare[kept], part, n * sizeof(*part));
                        kept += n;
                }

                swap = piece;
                piece = spare;
                spare = swap;
                count = kept;
        }

        for (i = 0; i < count; i++)
                target_rectangle(t, piece[i].x1, piece[i].y1,
                                  piece[i].x2 - piece[i].x1,
                                  piece[i].y2 - piece[i].y1, t->ink[INK_DESKTOP]);
}

static HOT void compose_clip(const struct target *t)
{
        int x1 = max(t->clip.x1, 0);
        int y1 = max(t->clip.y1, 0);
        int x2 = min(t->clip.x2, t->width);
        int y2 = min(t->clip.y2, t->height);
        struct pane *pane;

        if (x2 > x1 && y2 > y1)
                desktop_fill(t, x1, y1, x2, y2);

        list_for_each_entry(pane, &desktop.windows, link)
                compose_pane(pane, t);
}

// Somewhere to draw: an output, a pointer into its scanout buffer, and the
// damage. The clip is in target coordinates; the rectangle asked for is in
// desktop ones.
static PURE struct target target_of(struct output *output, u32 *pixels,
                                    const struct drm_rect *r, _Bool simd)
{
        struct target t = {
                .simd = simd,
                .pixels = pixels,
                .pitch = output->buffer->fb->pitches[0] / sizeof(u32),
                .width = (int)output->width,
                .height = (int)output->height,
                .x = output->x,
                .y = output->y,
                .opaque = output->opaque,
                .ink = output->palette,
                .clip = {
                        .x1 = max(r->x1 - output->x, 0),
                        .y1 = max(r->y1 - output->y, 0),
                        .x2 = min(r->x2 - output->x, (int)output->width),
                        .y2 = min(r->y2 - output->y, (int)output->height),
                },
        };

        return t;
}

/*
        One rectangle of the desktop rather than the whole of it. A mouse move
        dirties two small areas; repainting 1280x800 for it would be a megabyte
        of writes. The rectangle is in desktop coordinates.
*/
static void compose_rect(struct output *output, u32 *pixels,
                         const struct drm_rect *r, _Bool simd)
{
        struct target t = target_of(output, pixels, r, simd);

        if (t.clip.x2 > t.clip.x1 && t.clip.y2 > t.clip.y1)
                compose_clip(&t);
}

/*
        The cursor, where this output shows it. On a hardware plane it is never
        drawn in, and on the outputs it is not over there is nothing to draw.
*/
static void output_draw_cursor(struct output *output, u32 *pixels, _Bool simd)
{
        struct drm_rect cell, screen;
        struct target t;

        cursor_cell(&cell, desktop.cursor_x, desktop.cursor_y,
                    desktop.cursor_shape, desktop.cursor_scale);

        /*
                Whether this draws the cursor, kept here. cursor_shown is
                the plane section's -- set when a plane is armed over the pointer and
                counted by the pointer applet as a plane showing it -- and
                storing this answer into it cleared a live plane's flag on
                every compose: a terminal redrawing under a cursor that sat on
                its plane made the applet say no plane was showing.
        */
        _Bool drawn = !output->cursor_plane && output_touched(output, &cell, 1);

        if (!drawn)
                return;

        drm_rect_init(&screen, output->x, output->y, (int)output->width,
                      (int)output->height);
        t = target_of(output, pixels, &screen, simd);
        canvas_draw_cursor(&t, desktop.cursor_x - output->x,
                           desktop.cursor_y - output->y,
                           desktop.cursor_shape, desktop.cursor_scale);
}

/*
        The scanout buffer, mapped.

        Everything Canvas draws goes through this pointer, so failing to get
        one is not a dropped frame, it is a screen that stays as it was. It
        used to be a bare return.
*/
static _Bool output_map(struct output *output, struct iosys_map *map)
{
        int ret = drm_client_buffer_vmap_local(output->buffer, map);

        if (!ret)
        {
                output->unmappable = false;
                return true;
        }

        if (!output->unmappable)
        {
                output->unmappable = true;
                pr_err("[moonwater canvas] " "the scanout buffer will not map (%d), "
                                                 "so nothing can be drawn on it\n", ret);
        }

        return false;
}

/*
        What the driver actually gave us to scan out, and whether it can be
        written to at all.

        The mapping is made once here rather than first discovered halfway
        through a compose. Everything Canvas draws goes through it, so a driver
        that will not give us one is a screen that stays exactly as it was
        while mode setting and the cursor plane both go on working -- black
        from the first moment, with a cursor moving over it. Answering no here
        is what lets that screen be handed back instead of held.
*/
static _Bool output_describe(struct output *output)
{
        struct drm_framebuffer *fb = output->buffer->fb;
        struct iosys_map map;

        if (!output_map(output, &map))
                return false;

        pr_info("[moonwater canvas] " "scanout %p4cc, %u bytes a row (%lu KiB), modifier %llx, "
                           "%s memory, drawn with %s registers\n", &fb->format->format, fb->pitches[0], ((unsigned long)fb->pitches[0] * output->height) >> 10, (unsigned long long)fb->modifier, map.is_iomem ? "device" : "system",
                           !map.is_iomem && canvas_simd && canvas_simd_present() ? "vector" : "general");

        drm_client_buffer_vunmap_local(output->buffer);
        return true;
}

/*
        Hands a flush to the flusher: a rectangle in the output's own
        coordinates, or NULL for the whole buffer, which is what compose_output
        tells the driver. With no flusher -- before the canvas thread starts,
        or if its flusher could not be made -- the flush happens here, as every
        flush did before there was one.

        Process context only, which is why flush_lock is a plain spin_lock.
        Every caller composes, and composing sleeps: the console's write path
        only wakes the canvas thread and never queues. Anything that would
        queue from the console, an interrupt or a panic has to make this lock
        irqsave first, or hand the flush to process context instead.
*/
static void output_flush_queue(struct output *output, const struct drm_rect *rect)
{
        if (!canvas_flush_running())
        {
                struct drm_rect clip = rect ? *rect : (struct drm_rect){0};
                u64 started = ktime_get_ns();

                drm_client_buffer_flush(output->buffer, rect ? &clip : NULL);
                canvas_flush_ns += ktime_get_ns() - started;
                return;
        }

        spin_lock(&desktop.flush_lock);

        if (!output->flush_queued)
        {
                output->flush_queued = true;
                output->flush_whole = !rect;
                if (rect)
                        output->flush_pending = *rect;
                list_add_tail(&output->flush_link, &desktop.flush_queue);
        }
        else if (!rect)
                output->flush_whole = true;
        else if (!output->flush_whole)
                canvas_rect_join(&output->flush_pending, rect);

        spin_unlock(&desktop.flush_lock);

        canvas_flush_wake();
}

// The next output with a flush waiting, now marked as with the driver.
static struct output *output_flush_take(struct drm_rect *rect, _Bool *whole)
{
        struct output *output = NULL;

        spin_lock(&desktop.flush_lock);

        // What is queued waits for the card to come back; the redraw that
        // follows the wake queues its own.
        if (!READ_ONCE(desktop.asleep) && !list_empty(&desktop.flush_queue))
        {
                output = list_first_entry(&desktop.flush_queue, struct output,
                                          flush_link);
                list_del_init(&output->flush_link);
                *rect = output->flush_pending;
                *whole = output->flush_whole;
                output->flush_queued = false;
                output->flush_whole = false;
                output->flushing = true;
                atomic_fetch_add(1, &desktop.flushes_in_flight);
        }

        spin_unlock(&desktop.flush_lock);
        return output;
}

/*
        The flush is back. An output dropped while its buffer was with the
        driver was left for this, because the buffer could not be deleted out
        from under a flush in flight, and its card's release is waiting on it.
*/
static void output_flush_done(struct output *output)
{
        struct canvas *canvas = output->canvas;
        _Bool retired;

        spin_lock(&desktop.flush_lock);
        output->flushing = false;
        retired = output->retired;
        atomic_fetch_sub(1, &desktop.flushes_in_flight);
        spin_unlock(&desktop.flush_lock);

        if (retired)
        {
                output_free(output);
                atomic_fetch_sub(1, &canvas->retiring);
        }

        wake_up_all(&desktop.flush_idle);
}

/*
        The flusher: the canvas thread's policy, started and stopped beside it,
        and it never takes desktop.lock. Waiting out the driver is all it does.
*/
static int canvas_flush_loop(void *unused)
{
        while (!kthread_should_stop())
        {
                struct drm_rect rect;
                struct output *output;
                _Bool whole = false;
                u64 started;

                set_current_state(TASK_IDLE);
                output = output_flush_take(&rect, &whole);

                if (!output)
                {
                        schedule();
                        continue;
                }

                __set_current_state(TASK_RUNNING);

                started = ktime_get_ns();
                drm_client_buffer_flush(output->buffer, whole ? NULL : &rect);
                canvas_flush_ns += ktime_get_ns() - started;

                output_flush_done(output);
        }

        return 0;
}

/*
        Repaints a set of damaged rectangles on one output and hands the driver
        their union. A set rather than a pair because moving a window damages
        four things: where its frame was and is, and where the cursor was and
        is. The cursor's cell reaches outside the frame it is dragging, so
        leaving it out of the damage leaves a trail of it behind.

        Every rectangle is in desktop coordinates.
*/
static void output_repaint(struct output *output, const struct drm_rect *damage,
                           unsigned int count)
{
        struct iosys_map map;
        struct drm_rect flush;
        u32 *pixels;
        unsigned int i, j;
        u64 started;

        struct drm_rect merged[4];
        unsigned int kept = 0;
        _Bool joined;
        struct canvas_simd_hold simd;

        if (!count || count > ARRAY_SIZE(merged) || !output_map(output, &map))
                return;

        /*
                Overlapping damage composed twice is composed twice: a cursor
                that moved four pixels leaves two cells that are nearly the
                same cell, and every window and every glyph under them was
                laid out once for each.

                Until nothing more joins, rather than once through. Joining two
                rectangles grows one of them, and what it grew into can reach a
                third that neither of them touched -- which a single pass has
                already walked past. A window dragged in one step arrives as
                four: where its frame was and is, where the cursor was and is,
                and those chain.
        */
        flush = damage[0];
        memory_copy_apart(merged, (address_any)damage, count * sizeof(*damage));
        kept = count;

        for (i = 1; i < kept; i++)
                canvas_rect_join(&flush, &merged[i]);

        for (joined = true; joined;)
        {
                joined = false;

                for (i = 0; i < kept && !joined; i++)
                        for (j = i + 1; j < kept; j++)
                        {
                                if (!drm_rects_overlap(&merged[i], &merged[j]))
                                        continue;

                                canvas_rect_join(&merged[i], &merged[j]);

                                merged[j] = merged[--kept];
                                joined = true;
                                break;
                        }
        }

        pixels = map.vaddr;
        started = ktime_get_ns();

        canvas_simd_begin(&simd, map.is_iomem);
        for (i = 0; i < kept; i++)
                compose_rect(output, pixels, &merged[i], simd.on);

        output_draw_cursor(output, pixels, simd.on);
        canvas_simd_end(&simd);

        drm_client_buffer_vunmap_local(output->buffer);
        pointer_draw_total += ktime_get_ns() - started;

        drm_rect_translate(&flush, -output->x, -output->y);
        if (!drm_rect_intersect(&flush, &(struct drm_rect){
                        .x2 = (int)output->width, .y2 = (int)output->height }))
                return;

        output_flush_queue(output, &flush);
}

static void compose_output(struct output *output)
{
        struct iosys_map map;
        struct drm_rect screen;
        struct canvas_simd_hold simd;
        u32 *pixels;

        if (!output_map(output, &map))
                return;

        pixels = map.vaddr;
        drm_rect_init(&screen, output->x, output->y, (int)output->width,
                      (int)output->height);
        canvas_simd_begin(&simd, map.is_iomem);
        compose_rect(output, pixels, &screen, simd.on);

        output_draw_cursor(output, pixels, simd.on);
        canvas_simd_end(&simd);

        drm_client_buffer_vunmap_local(output->buffer);

        /*
                Telling the driver the whole buffer changed, which the damage
                path did and this one did not.

                It is not only for drivers that shadow the framebuffer. i915
                maps a dumb buffer write-back cached and implements dirty as a
                frontbuffer flush, which is what invalidates framebuffer
                compression and panel self refresh. Without it the display
                keeps serving the compressed copy it already had for regions we
                have just painted, and what reaches the screen is the new
                picture with holes of the old one through it.
        */
        output_flush_queue(output, NULL);
}

/* ---- plane: the cursor plane ---- */

/*
        Canvas -- the cursor

        One cursor, on whichever outputs it currently overlaps.

        A hardware cursor plane is composited by the display engine during
        scanout, so moving it touches no pixels: the commit carries a position
        and nothing else. drm_atomic_helper_update_plane sets
        legacy_cursor_update whenever the plane is the crtc's cursor, and the
        commit helper then completes without waiting for vblank. Drawing into
        the framebuffer instead means a damage rectangle, and on an atomic
        driver that is a commit that does wait -- a cursor move cannot land in
        less than a frame however little is drawn.

        Every commit has to re-arm the plane: on the atomic drivers used here,
        drm_client_modeset_commit disables every non-primary plane first.
*/

/*
        The lock dance is the one drm_mode_cursor_common does: take the crtc
        and the plane, and back off and retry the whole thing on -EDEADLK. The
        retry has to cover update_plane too, which takes more locks of its own.

        Uninterruptible, unlike the ioctl: this is a thread with no signals to
        take, where -ERESTARTSYS would only mean a dropped mouse move.
*/
static int plane_update(struct output *output, _Bool show, int x, int y)
{
        struct drm_plane *plane = output->cursor_plane;
        struct drm_crtc *crtc = output->mode_set->crtc;
        struct drm_rect cell;
        struct drm_modeset_acquire_ctx ctx;
        int ret;

        cursor_cell(&cell, x, y, output->cursor_shape, output->cursor_scale);
        drm_modeset_acquire_init(&ctx, 0);
retry:
        /* Match drm_mode_cursor_common's global modeset lock order. */
        ret = drm_modeset_lock(&crtc->mutex, &ctx);

        if (!ret)
                ret = drm_modeset_lock(&plane->mutex, &ctx);

        if (!ret)
                ret = show ? plane->funcs->update_plane(
                                 plane, crtc, output->cursor_buffer->fb,
                                 cell.x1, cell.y1,
                                 output->cursor_w, output->cursor_h, 0, 0,
                                 output->cursor_w << 16, output->cursor_h << 16, &ctx)
                           : plane->funcs->disable_plane(plane, &ctx);

        if (ret == -EDEADLK)
        {
                drm_modeset_backoff(&ctx);
                goto retry;
        }

        drm_modeset_drop_locks(&ctx);
        drm_modeset_acquire_fini(&ctx);

        return ret;
}

/* Both cursor buffers gone and forgotten, which is how every path below
   that could not put an arrow on a plane leaves the output. */
static void cursor_buffers_drop(struct output *output)
{
        drm_client_buffer_delete(output->cursor_buffer);
        drm_client_buffer_delete(output->cursor_back);
        output->cursor_buffer = NULL;
        output->cursor_back = NULL;
}

static void plane_drop(struct output *output)
{
        int ret = 0;

        // Keep a failed cursor for this output's next full client commit,
        // which disables non-primary planes. Output destruction instead
        // releases the client buffer through DRM's framebuffer removal.
        if (output->cursor_plane)
                ret = plane_update(output, false, 0, 0);

        if (ret)
        {
                atomic_long_inc(&cursor_plane_failures);
                output->cursor_recovery = 1;
                cursor_plane_recovery = true;
        }

        output->cursor_plane = NULL;
        output->cursor_shown = false;

        if (!ret && !output->cursor_recovery)
        {
                cursor_buffers_drop(output);
        }
}

// The largest whole scale of a shape that fits the plane's buffer.
static PURE unsigned int plane_scale(struct output *output, unsigned int scale)
{
        return min(scale, max(1u, min(output->cursor_w / CURSOR_W,
                                      output->cursor_h / CURSOR_H)));
}

/*
        Paints one shape into the plane's buffer.

        The image is written before the plane is ever armed, because a driver
        that keeps its framebuffer elsewhere only uploads it when the plane's
        framebuffer changes -- virtio-gpu transfers the image on that edge and
        sends nothing but a position afterwards, which is what makes this
        cheap. Changing shape is therefore a repaint here, not per move, and
        the repaint has to land in a different framebuffer object than the
        one already on the plane: flushing new pixels into the same object
        is not that edge, so the arrow from the first paint stayed up for
        every later shape.
*/
static int plane_paint(struct output *output, unsigned int shape,
                       unsigned int fitted_scale)
{
        struct drm_client_buffer *into = output->cursor_buffer;
        u32 opaque_ink[INK_COUNT];
        struct iosys_map map;
        struct target t;

        canvas_palette(opaque_ink, DRM_FORMAT_ARGB8888);

        if (output->cursor_plane)
        {
                if (!output->cursor_back)
                {
                        output->cursor_back = drm_client_buffer_create_dumb(
                            &output->canvas->client, output->cursor_w,
                            output->cursor_h, DRM_FORMAT_ARGB8888);
                        if (IS_ERR(output->cursor_back))
                                output->cursor_back = NULL;
                }

                if (output->cursor_back)
                        into = output->cursor_back;
        }

        if (drm_client_buffer_vmap_local(into, &map))
                return -EIO;

        t.pixels = map.vaddr;
        t.pitch = into->fb->pitches[0] / sizeof(u32);
        t.width = (int)output->cursor_w;
        t.height = (int)output->cursor_h;
        t.x = 0;
        t.y = 0;
        t.opaque = 0xff000000;
        t.ink = opaque_ink;
        t.simd = false;         // a cursor's worth of pixels is not worth the bracket
        drm_rect_init(&t.clip, 0, 0, t.width, t.height);

        // Transparent everywhere the shape does not cover, or it wears a box
        // of whatever the buffer was allocated holding.
        target_rectangle(&t, 0, 0, t.width, t.height, 0x00000000);
        canvas_draw_cursor(&t,
                           canvas_cursor_hot[shape][0] * (int)fitted_scale,
                           canvas_cursor_hot[shape][1] * (int)fitted_scale,
                           shape, fitted_scale);

        drm_client_buffer_vunmap_local(into);
        drm_client_buffer_flush(into, NULL);

        if (into != output->cursor_buffer)
        {
                output->cursor_back = output->cursor_buffer;
                output->cursor_buffer = into;
        }

        output->cursor_shape = shape;
        output->cursor_scale = fitted_scale;
        return 0;
}

/*
        moonwater.cursor_plane=0 turns the plane off: every output draws the
        cursor into its framebuffer, as one with no cursor plane does.

        The escape hatch for a machine whose cursor plane is broken --
        misplaced, stale, or never shown -- on a card where the software cursor
        still works, slower on every move and otherwise the same. The canvas
        lane boots with it too, because QEMU's screendump holds the primary
        plane only and its pixel checks are the software cursor's. Read back
        under /sys/module/moonwater/parameters, and by the pointer applet.
*/
static bool canvas_cursor_plane = true;
module_param_named(cursor_plane, canvas_cursor_plane, bool, 0444);

/*
        Said, once for the output it happened to, because what it leaves is
        the software cursor: identical on screen and slower on every move, so
        nothing else would ever show that a plane was there to be had. It went
        unseen that way on virtio-gpu for as long as the fallback below was the
        arrow's own size.
*/
static COLD void plane_lost(struct drm_client_dev *client, struct output *output,
                            const char *why, long error)
{
        pr_info("[moonwater canvas] " "%s cursor plane %ux%u %s (%ld), drawing the cursor instead\n",
                client->dev->driver->name, output->cursor_w, output->cursor_h, why, error);
}

static void plane_claim(struct drm_client_dev *client, struct output *output)
{
        struct drm_plane *plane = output->mode_set->crtc->cursor;
        const struct drm_mode_config *config = &client->dev->mode_config;
        int ret;

        if (!canvas_cursor_plane)
        {
                pr_info_once("[moonwater canvas] " "cursor plane turned off (moonwater.cursor_plane=0), drawing the cursor instead\n");
                return;
        }

        /*
                i915's cursor plane does not scan a dumb buffer.

                Haswell will take the object and then stop the pipe the
                moment the plane is armed: the first picture (kernel log)
                stays, the pointer thread waits out a cursor update that
                never completes, and there is no terminal. virtio-gpu is
                why the plane exists; i915's dirtyfb is a frontbuffer
                flush, so drawing the arrow into the scanout is cheap.
                moonwater.cursor_plane=0 is the same drawing everywhere.
        */
        if (client->dev->driver && client->dev->driver->name &&
            (!strcmp(client->dev->driver->name, "i915") ||
             !strcmp(client->dev->driver->name, "xe")))
        {
                pr_info_once("[moonwater canvas] " "%s cursor plane skipped, drawing the cursor instead\n",
                             client->dev->driver->name);
                return;
        }

        // Direct callbacks rely on atomic state owning framebuffer references;
        // legacy callbacks need core bookkeeping and a different recovery path.
        if (!drm_drv_uses_atomic_modeset(client->dev) || !plane ||
            !plane->funcs->update_plane || !plane->funcs->disable_plane ||
            canvas_plane_pick_format(plane, DRM_FORMAT_ARGB8888,
                                     DRM_FORMAT_ARGB8888) == DRM_FORMAT_INVALID)
                return;

        /*
                The size the driver asks for, and 64 when it asks for none --
                what DRM_CAP_CURSOR_WIDTH and _HEIGHT answer a compositor in
                userspace for the same silence -- then raised to the smallest
                framebuffer the driver makes at all.

                The arrow's own 16x20 was the fallback. virtio-gpu sets no
                cursor size and refuses a framebuffer under 32x32, so this
                buffer failed with EINVAL and every move was drawn into the
                screen instead: a blocking commit in the canvas thread, a
                display period long, beside a plane nothing used. The arrow is
                drawn at its hotspot inside whatever buffer this is, and
                plane_scale fits the shape to it, so a larger buffer moves
                nothing but where the transparent part ends.
        */
        output->cursor_w = max_t(unsigned int, config->cursor_width ?: 64,
                                 config->min_width);
        output->cursor_h = max_t(unsigned int, config->cursor_height ?: 64,
                                 config->min_height);

        if (config->max_width)
                output->cursor_w = min_t(unsigned int, output->cursor_w,
                                         config->max_width);
        if (config->max_height)
                output->cursor_h = min_t(unsigned int, output->cursor_h,
                                         config->max_height);

        if (output->cursor_w < CURSOR_W || output->cursor_h < CURSOR_H)
        {
                plane_lost(client, output, "is smaller than the arrow", 0);
                return;
        }

        output->cursor_buffer = drm_client_buffer_create_dumb(
            client, output->cursor_w, output->cursor_h, DRM_FORMAT_ARGB8888);
        if (IS_ERR(output->cursor_buffer))
        {
                plane_lost(client, output, "has no buffer",
                           PTR_ERR(output->cursor_buffer));
                output->cursor_buffer = NULL;
                return;
        }

        ret = plane_paint(output, CURSOR_ARROW, 1);
        if (ret)
        {
                plane_lost(client, output, "would not take the arrow", ret);
                cursor_buffers_drop(output);
                return;
        }

        output->cursor_plane = plane;
}

static void cursor_arm_output(struct output *output, _Bool wanted)
{
        unsigned int scale;
        int ret;

        if (!output->cursor_plane)
                return;

        scale = wanted ? plane_scale(output, desktop.cursor_scale) : 0;

        if (wanted &&
            (output->cursor_shape != desktop.cursor_shape ||
             output->cursor_scale != scale) &&
            plane_paint(output, desktop.cursor_shape, scale))
        {
                atomic_long_inc(&cursor_plane_failures);
                plane_drop(output);
                return;
        }

        ret = plane_update(output, wanted, desktop.cursor_x - output->x,
                           desktop.cursor_y - output->y);

        if (!ret)
        {
                if (wanted)
                        atomic_long_inc(&cursor_plane_updates);
                output->cursor_shown = wanted;
                return;
        }

        // Give it up rather than leave a cursor that cannot move. The next
        // event repaints through the software path.
        atomic_long_inc(&cursor_plane_failures);
        pr_info("[moonwater canvas] " "cursor plane refused an update (%d), drawing the cursor instead\n", ret);
        plane_drop(output);
}

/*
        Moves the one cursor, and changes its shape where that is what changed.
        Only the outputs it left and the outputs it arrived on are touched.

        During a window drag the window repaint carries a software cursor with
        it, but a hardware cursor is a different plane and that repaint cannot
        move it. planes_only arms those planes immediately, before the more
        expensive window compose, and leaves the software damage and drawn
        coordinates for pane_reshape to finish in the same pass.
*/
static _Bool cursor_move_core(int new_x, int new_y, _Bool planes_only)
{
        int old_x = desktop.drawn_x;
        int old_y = desktop.drawn_y;
        unsigned int old_shape = desktop.drawn_shape;
        unsigned int old_scale = desktop.drawn_scale;
        struct drm_rect damage[2];
        struct output *output;
        _Bool plane_presented = false;
        _Bool plane_complete = true;

        if (old_x == new_x && old_y == new_y &&
            old_shape == desktop.cursor_shape && old_scale == desktop.cursor_scale)
                return false;

        desktop.cursor_x = new_x;
        desktop.cursor_y = new_y;

        /*
                Cursor geometry is desktop geometry.  Computing both cells
                once keeps the output walk to two overlap checks; formerly it
                rebuilt the new cell twice and the old cell once per output,
                including on the urgent plane-only resize path.
        */
        cursor_cell(&damage[0], old_x, old_y, old_shape, old_scale);
        cursor_cell(&damage[1], new_x, new_y,
                    desktop.cursor_shape, desktop.cursor_scale);

        list_for_each_entry(output, &desktop.outputs, link)
        {
                _Bool wanted = output_touched(output, &damage[1], 1);

                if (!output_touched(output, &damage[0], 1) && !wanted)
                        continue;

                if (output->cursor_plane)
                {
                        u64 started = ktime_get_ns();

                        cursor_arm_output(output, wanted);
                        pointer_flush_total += ktime_get_ns() - started;

                        if (output->cursor_plane)
                        {
                                if (wanted)
                                        plane_presented = true;
                                continue;
                        }
                }

                // A failed hide matters too: the old cursor may still be on
                // screen, so this request was not an all-plane completion.
                plane_complete = false;

                if (!planes_only)
                        output_repaint(output, damage, 2);
        }

        if (!planes_only)
        {
                desktop.drawn_x = new_x;
                desktop.drawn_y = new_y;
                desktop.drawn_shape = desktop.cursor_shape;
                desktop.drawn_scale = desktop.cursor_scale;
        }

        return plane_presented && plane_complete;
}

static _Bool cursor_move_planes(int new_x, int new_y)
{
        _Bool complete;

        cursor_plane_requested_generation++;
        cursor_plane_requested_x = new_x;
        cursor_plane_requested_y = new_y;
        complete = cursor_move_core(new_x, new_y, true);

        if (complete)
        {
                cursor_plane_armed_generation = cursor_plane_requested_generation;
                cursor_plane_armed_x = new_x;
                cursor_plane_armed_y = new_y;
        }

        return complete;
}

static void cursor_move(int new_x, int new_y)
{
        cursor_move_core(new_x, new_y, false);
}

/* ---- drag: moving and sizing a window, and filling the screen with it ---- */

/*
        Canvas -- moving and resizing

        Both are the same operation underneath: give a window a new rectangle
        and repaint the desktop where it was and where it is, on every output
        either rectangle touches. Because panes are in desktop coordinates,
        crossing to another screen is not a case.

        A window with no pixels behind it can be any size the desktop allows,
        and its text rewraps to whatever that turns out to be. One with a
        buffer cannot outgrow the buffer its program asked for.
*/

/*
        The screen a point is on, and which number it is.

        The pointer is confined to an output, so a press or a drag always ends
        on one. Which one matters: arranging against the bounding box of every
        monitor puts a maximized window across all of them.
*/
static struct output *output_at(int x, int y, unsigned int *index)
{
        struct output *candidate;
        unsigned int at = 0;

        list_for_each_entry(candidate, &desktop.outputs, link)
        {
                if (point_in_rect(candidate->x, candidate->y,
                                  (int)candidate->width, (int)candidate->height,
                                  x, y))
                {
                        *index = at;
                        return candidate;
                }

                at++;
        }

        return NULL;
}

/*
        The window the pointer is over, and which of its edges it has hold of.

        A window blocks over all of itself, not only over the parts that
        answer: its middle is not a hole through to whatever is behind. The
        list is walked front to back and the first match wins, which is the
        topmost; a window can opt out with WINDOW_PASSTHROUGH.
*/
static struct pane *pane_under(int x, int y, unsigned int *edges)
{
        struct pane *pane;

        *edges = 0;

        list_for_each_entry_reverse(pane, &desktop.windows, link)
        {
                struct drm_rect frame;

                if (pane->style & (WINDOW_MINIMIZED | WINDOW_PASSTHROUGH))
                        continue;

                pane_frame(pane, &frame);

                if (x < frame.x1 || x >= frame.x2 || y < frame.y1 || y >= frame.y2)
                        continue;

                if (!(pane->style & WINDOW_FRAME))
                        return pane;

                if (x < frame.x1 + EDGE_GRIP)
                        *edges |= EDGE_LEFT;
                else if (x >= frame.x2 - EDGE_GRIP)
                        *edges |= EDGE_RIGHT;

                if (y < frame.y1 + EDGE_GRIP)
                        *edges |= EDGE_TOP;
                else if (y >= frame.y2 - EDGE_GRIP)
                        *edges |= EDGE_BOTTOM;

                return pane;
        }

        return NULL;
}

/*
        Which shape an edge mask wears. Sixteen entries because the mask is
        four bits; the impossible ones -- left and right at once -- read as an
        arrow, which is what a mask of no edges means too.
*/
static const unsigned char cursor_by_edges[16] = {
    [EDGE_LEFT] = CURSOR_RESIZE_H,
    [EDGE_RIGHT] = CURSOR_RESIZE_H,
    [EDGE_TOP] = CURSOR_RESIZE_V,
    [EDGE_BOTTOM] = CURSOR_RESIZE_V,
    [EDGE_TOP | EDGE_LEFT] = CURSOR_RESIZE_NWSE,
    [EDGE_BOTTOM | EDGE_RIGHT] = CURSOR_RESIZE_NWSE,
    [EDGE_TOP | EDGE_RIGHT] = CURSOR_RESIZE_NESW,
    [EDGE_BOTTOM | EDGE_LEFT] = CURSOR_RESIZE_NESW,
};

static PURE unsigned int cursor_shape_at(int x, int y)
{
        unsigned int edges;

        if (desktop.resizing)
                return cursor_by_edges[desktop.resize_edges & 15];

        if (desktop.dragging)
                return CURSOR_ARROW;

        pane_under(x, y, &edges);
        return cursor_by_edges[edges & 15];
}

/*
        The one way a window's rectangle changes. Everything else -- a drag, a
        resize, a region -- decides what the new rectangle is and comes here.
*/
static void pane_reshape(struct pane *pane, int x, int y, int w, int h)
{
        struct drm_rect damage[4];
        struct output *output;

        pane_frame(pane, &damage[0]);

        pane->x = x;
        pane->y = y;
        pane->width = w;
        pane->height = h;

        pane_regrid(pane);
        pane_frame(pane, &damage[1]);

        // The cursor is dragging this, so where it was and where it is are
        // damaged too, and its cell reaches outside the frame.
        cursor_cell(&damage[2], desktop.drawn_x, desktop.drawn_y,
                    desktop.drawn_shape, desktop.drawn_scale);
        cursor_cell(&damage[3], desktop.cursor_x, desktop.cursor_y,
                    desktop.cursor_shape, desktop.cursor_scale);

        if (pane->shared)
        {
                WRITE_ONCE(pane->shared->x, pane->x);
                WRITE_ONCE(pane->shared->y, pane->y);
                WRITE_ONCE(pane->shared->width, (unsigned int)pane->width);
                WRITE_ONCE(pane->shared->height, (unsigned int)pane->height);
        }

        list_for_each_entry(output, &desktop.outputs, link)
        {
                if (!output_touched(output, damage, 4))
                        continue;

                output_repaint(output, damage, 4);
        }

        desktop.drawn_x = desktop.cursor_x;
        desktop.drawn_y = desktop.cursor_y;
        desktop.drawn_shape = desktop.cursor_shape;
        desktop.drawn_scale = desktop.cursor_scale;
}

// Back to the rectangle the program last had, wherever it was put since.
static void pane_float(struct pane *pane)
{
        if (pane->arranged == PANE_FLOATING)
                return;

        pane->arranged = PANE_FLOATING;
        pane->display = pane->saved_display;

        if (pane->shared)
                WRITE_ONCE(pane->shared->display, pane->display);

        pane_reshape(pane, pane->saved_x, pane->saved_y,
                     pane->saved_w, pane->saved_h);
}

static void drag_move(int x, int y)
{
        struct pane *pane = desktop.dragging;

        pane_reshape(pane, x - desktop.grab_x, y - desktop.grab_y,
                     pane->width, pane->height);
}

// A window with a buffer cannot outgrow it; one without can fill the desktop.
static void pane_limits(struct pane *pane, int *max_w, int *max_h)
{
        if (pane->pixels)
        {
                *max_w = (int)pane->max_width;
                *max_h = (int)pane->max_height;
                return;
        }

        *max_w = desktop.width;
        *max_h = desktop.height;
}

static void resize_move(int x, int y)
{
        struct pane *pane = desktop.resizing;
        unsigned int edges = desktop.resize_edges;
        int dx = x - desktop.press_x;
        int dy = y - desktop.press_y;
        int nx = desktop.resize_x, ny = desktop.resize_y;
        int nw = desktop.resize_w, nh = desktop.resize_h;
        int max_w, max_h, min_w, min_h;

        pane_limits(pane, &max_w, &max_h);
        min_w = min(WINDOW_MIN_WIDTH, max_w);
        min_h = min(WINDOW_MIN_HEIGHT, max_h);

        if (edges & EDGE_LEFT)
                nw = desktop.resize_w - dx;
        else if (edges & EDGE_RIGHT)
                nw = desktop.resize_w + dx;

        if (edges & EDGE_TOP)
                nh = desktop.resize_h - dy;
        else if (edges & EDGE_BOTTOM)
                nh = desktop.resize_h + dy;

        nw = clamp(nw, min_w, max_w);
        nh = clamp(nh, min_h, max_h);

        /*
                Rounded to the grid before the corner is worked out, not after.

                pane_regrid rounds a window of cells down to a whole number of
                them, and it used to do that after this had already decided
                where the left edge goes -- so dragging the left edge of a
                terminal moved its right edge as well, by up to a cell, in and
                out as the rounding changed under the hand. The edge that was
                not grabbed only stays where it was if the rounding comes off
                the one that was.
        */
        pane_grid_fit(pane, &nw, &nh);

        // The edge that was not grabbed stays where it was.
        if (edges & EDGE_LEFT)
                nx = desktop.resize_x + desktop.resize_w - nw;

        if (edges & EDGE_TOP)
                ny = desktop.resize_y + desktop.resize_h - nh;

        pane_reshape(pane, nx, ny, nw, nh);
}

/*
        The bar under the hand, in rows.

        The thumb is a picture of how much of the whole is showing, so where it
        sits is where the view is: the top of the thumb over the height of the
        bar is the same fraction as the rows above the view over all of them.
        Grabbed anywhere inside the thumb it stays under the finger, and
        pressed outside it the thumb comes to the finger's middle.
*/
static void bar_move(struct pane *pane, int y,
                     const struct pane_bar_geometry *bar)
{
        int top;
        unsigned int above;

        top = clamp(y - desktop.bar_grab - bar->y, 0,
                    max(bar->height - bar->thumb_span, 0));
        above = (unsigned int)((unsigned long)top * bar->total /
                               (unsigned long)max(bar->height, 1));

        if (!pane_view_set(pane, above))
                return;

        atomic_set(&desktop.frame_pending, 1);
        canvas_thread_wake();
}

/*
        Filling the screen, or half of it.

        Maximized and the two halves are one operation: work out a rectangle
        from the output the hand is over and go there. The rectangle the window
        had is saved on the way out of floating and not on every arrangement,
        so snapping left, then right, then dragging off puts it back where its
        program had it rather than on the left half.
*/
static void pane_arrange(struct pane *pane, unsigned int how,
                         int at_x, int at_y)
{
        int title = pane_title(pane);
        int border = pane_border(pane);
        unsigned int display = pane->display;
        struct output *output = output_at(at_x, at_y, &display);
        int max_w, max_h, width, height, x, half;

        if (how == PANE_FLOATING)
        {
                pane_float(pane);
                return;
        }

        if (!output)
        {
                output = output_by_index(pane->display);
                display = pane->display;
        }

        if (!output)
                return;

        if (pane->arranged == PANE_FLOATING)
        {
                pane->saved_x = pane->x;
                pane->saved_y = pane->y;
                pane->saved_w = pane->width;
                pane->saved_h = pane->height;
                pane->saved_display = pane->display;
        }

        pane->arranged = how;
        pane->display = display;

        if (pane->shared)
                WRITE_ONCE(pane->shared->display, display);

        pane_limits(pane, &max_w, &max_h);

        // Halves are cut from the whole rather than each taking half of it,
        // so an odd number of pixels goes to the right one instead of leaving
        // a column of desktop showing down the middle.
        half = (int)output->width / 2;

        switch (how)
        {
        case PANE_LEFT:
                x = output->x + border;
                width = half - border * 2;
                break;

        case PANE_RIGHT:
                x = output->x + half + border;
                width = (int)output->width - half - border * 2;
                break;

        default:
                x = output->x + border;
                width = (int)output->width - border * 2;
                break;
        }

        width = clamp(width, 0, max_w);
        height = clamp((int)output->height - title - border * 3, 0, max_h);

        /*
                Rounded to whole cells before the left edge is chosen, the way
                a resize is, and for the same reason: a window of text cannot
                be exactly half a screen wide, so a few pixels have to go
                somewhere. They come off the edge the window was not thrown
                at. Without this a window snapped right sat two pixels short
                of the screen edge with a stripe of desktop down the outside,
                while the same window snapped left was flush -- the rounding
                always ate the right-hand side, whichever side had been asked
                for.
        */
        pane_grid_fit(pane, &width, &height);

        if (how == PANE_RIGHT)
                x = output->x + (int)output->width - border - width;

        pane_reshape(pane, x, output->y + border, width, height);
}

// A double click on the titlebar, which fills the screen or undoes it.
static void pane_maximize(struct pane *pane, int at_x, int at_y)
{
        if (pane->arranged == PANE_MAXIMIZED)
        {
                pane_float(pane);
                return;
        }

        pane_arrange(pane, PANE_MAXIMIZED, at_x, at_y);
}

/*
        Taking an arranged titlebar restores the saved window under the hand.

        Leaving it arranged while ordinary drag_move changed x and y produced
        a full-screen-sized loose window, and the next double-click restored
        geometry from before the maximize. Keep the horizontal fraction that
        was grabbed, so taking the right side does not make the restored
        window jump until its left side is under the pointer.
*/
static void pane_restore_for_drag(struct pane *pane, int x, int y)
{
        int max_w, max_h;
        int old_w = max(pane->width, 1);
        int title_at = clamp(y - pane->y, 0, max(canvas_title - 1, 0));
        int width, height, grab;

        pane_limits(pane, &max_w, &max_h);
        width = clamp(pane->saved_w, min(WINDOW_MIN_WIDTH, max_w), max_w);
        height = clamp(pane->saved_h, min(WINDOW_MIN_HEIGHT, max_h), max_h);
        grab = (int)((long)(x - pane->x) * width / old_w);

        pane->arranged = PANE_FLOATING;
        pane_reshape(pane, x - grab, y - title_at, width, height);
}

// Two clicks are a pair when the second lands on the same window soon enough
// after the first. A quarter of a second is what a hand does without meaning
// to say two separate things.
#define PRESS_AGAIN_NS 400000000ull

static _Bool pointer_client_down;

static void drag_press(int x, int y)
{
        unsigned int edges;
        struct pane_bar_geometry bar;
        struct pane *pane = pane_under(x, y, &edges);
        struct pane *was = desktop.focused;

        if (!pane)
                return;

        pane_focus(pane);
        pane_raise(pane);
        list_move_tail(&pane->link, &desktop.windows);

        // Taking hold of a window is what free floating means, so a window the
        // compositor placed is handed back to its program.
        pane->region = WINDOW_FREE;
        if (pane->shared)
                WRITE_ONCE(pane->shared->region, WINDOW_FREE);

        desktop.press_x = x;
        desktop.press_y = y;

        /*
                Alt and anywhere on a window moves it.

                A window with no titlebar has nothing to take hold of at all:
                its program put it where it is and only its program could move
                it again. This is the thirty-year-old answer to that, and it is
                the whole of what makes a frameless window manageable.

                Before the edges, so Alt over a corner moves rather than
                resizes: with Alt held the hand has said which of the two it
                meant, and a frameless window has no corner to resize from
                anyway.

                A window the compositor owns is not the program's to move
                around by the body -- the kernel log has no titlebar because it
                is not a window in that sense -- so this needs a shared page
                like everything else that answers to a hand.
        */
        if (pane->shared &&
            ((unsigned int)atomic_read(&desktop.modifiers) & WINDOW_KEY_ALT))
        {
                if (pane->arranged != PANE_FLOATING)
                        pane_restore_for_drag(pane, x, y);

                desktop.dragging = pane;
                desktop.grab_x = x - pane->x;
                desktop.grab_y = y - pane->y;
                goto redraw;
        }

        /*
                The X, before anything that takes hold of the window, and after
                the edges.

                After, for the reason the scrollbar is: the button's square and
                the corner's grip overlap by a few pixels, and a window that
                cannot be resized from its own corner is worse than a button
                that has to be hit a little further in. There is plenty of
                button left inside.

                Above the double-click test, though: the button is inside the
                titlebar, so a second click on it would otherwise maximize the
                window on its way out. Nothing is dragged and no press is
                remembered -- a click on the button is not one of a pair.
        */
        {
                int cx, cy, side;

                if (!edges && pane_close_box(pane, &cx, &cy, &side) &&
                    point_in_rect(cx, cy, side, side, x, y))
                {
                        desktop.press_pane = NULL;
                        pane_close_request(pane);
                        goto redraw;
                }
        }

        {
                u64 now = ktime_get_ns();
                _Bool again = pane == desktop.press_pane &&
                              now - desktop.press_ns <= PRESS_AGAIN_NS;

                desktop.press_pane = pane;
                desktop.press_ns = now;

                if (again && pane_in_title(pane, x, y))
                {
                        // Cleared, so a third click is a first one again
                        // rather than the window flickering under a hand that
                        // is still clicking.
                        desktop.press_pane = NULL;
                        pane_maximize(pane, x, y);
                        return;
                }
        }

        /*
                An edge first, and the bar after it.

                The grip reaches six pixels in from the frame and the bar is
                ten wide, so the two overlap by four and one of them has to
                give. The edge wins: a window that cannot be resized from its
                own corner is worse than a bar that has to be grabbed a few
                pixels further in, and there is bar left over on the inside to
                grab. That is why the bar is ten and not six.
        */
        if (!edges && pane_bar(pane, &bar) &&
            point_in_rect(bar.x, bar.y, bar.width, bar.height, x, y))
        {
                int thumb_y = bar.y + bar.thumb_at;

                desktop.bar_grab = y >= thumb_y && y < thumb_y + bar.thumb_span
                                       ? y - thumb_y
                                       : bar.thumb_span / 2;
                desktop.barring = pane;
                bar_move(pane, y, &bar);
        }
        else if (edges)
        {
                // An edge resize makes this ordinary geometry; a later
                // double-click must maximize it, not restore stale pre-max
                // coordinates.
                pane->arranged = PANE_FLOATING;
                desktop.resizing = pane;
                desktop.resize_edges = edges;
                desktop.resize_x = pane->x;
                desktop.resize_y = pane->y;
                desktop.resize_w = pane->width;
                desktop.resize_h = pane->height;
        }
        else if (pane_in_title(pane, x, y))
        {
                if (pane->arranged != PANE_FLOATING)
                        pane_restore_for_drag(pane, x, y);

                desktop.dragging = pane;
                desktop.grab_x = x - pane->x;
                desktop.grab_y = y - pane->y;
        }
        else
        {
                pointer_client_down = true;
                pointer_report(pane, x, y, 0, WINDOW_KEY_DOWN);
        }

redraw:
        // The titlebar that lost focus and the window that came to the front.
        desktop.damage_count = 0;
        desktop.damage_all = false;

        if (was && was != pane)
                pane_damage_frame(was);

        pane_damage_frame(pane);
        desktop_repaint();
}

/*
        Where a drag ended, read as an arrangement.

        The pointer and not the window, because the pointer is what the hand
        aimed and the window is wherever it happened to be held by. Against
        the output the pointer is on rather than the desktop, so the shared
        border between two screens is not two snap targets that fight.

        Top before the sides, so a corner maximizes: it is the one of the
        three that cannot be reached any other way by dragging.
*/
static unsigned int snap_at(int x, int y)
{
        unsigned int index;
        struct output *output = output_at(x, y, &index);

        if (!output)
                return PANE_FLOATING;

        if (y - output->y < SNAP_MARGIN)
                return PANE_MAXIMIZED;

        if (x - output->x < SNAP_MARGIN)
                return PANE_LEFT;

        if (output->x + (int)output->width - 1 - x < SNAP_MARGIN)
                return PANE_RIGHT;

        return PANE_FLOATING;
}

static void drag_release(int x, int y)
{
        struct pane *dragged = desktop.dragging;

        desktop.dragging = NULL;
        desktop.resizing = NULL;
        desktop.barring = NULL;

        if (pointer_client_down)
        {
                struct pane *pane = desktop.focused;

                pointer_client_down = false;
                if (pane)
                        pointer_report(pane, x, y, 0, 0);
        }

        /*
                Only a window that was being moved. A resize is the hand saying
                what size it wants, and answering that by snapping would throw
                the size away at the moment it was chosen.
        */
        if (!dragged)
                return;

        {
                unsigned int how = snap_at(x, y);

                if (how != PANE_FLOATING)
                        pane_arrange(dragged, how, x, y);
        }
}

/*
        The wheel goes to whatever is under the pointer.

        Under, not focused: a wheel is aimed with the hand rather than chosen,
        and every desktop since the wheel existed has read it that way. Nothing
        takes focus for it either, so reading one window while typing into
        another works the way it looks like it should.

        Every window of cells answers, its own or a program's, because the
        lines a program wrote are in a ring the compositor allocated and the
        view onto that ring is the compositor's. A program that has asked for
        the pointer -- mouse tracking -- is owed the wheel as button 64 and 65
        instead, which is how btop and ncurses read it.

        Linux calls one legacy REL_WHEEL unit a physical detent and calls 120
        REL_WHEEL_HI_RES units the same distance. Three text lines per detent
        is the conventional desktop step.

        Do not accelerate here. The old curve made four ordinary notches move
        3 + 6 + 9 + 12 = 30 lines, so a wheel became ten times faster merely
        by being used continuously. Multiple input events are already
        coalesced in desktop.wheel; this conversion preserves their distance.
        A high-resolution wheel keeps the fraction until it amounts to a line.
*/
#define WHEEL_LINES 3
#define WHEEL_V120 120

static int wheel_lines(int v120, int *remainder)
{
        long scaled = (long)v120 * WHEEL_LINES + *remainder;
        int lines = (int)(scaled / WHEEL_V120);

        *remainder = (int)(scaled % WHEEL_V120);
        return lines;
}

static void wheel_deliver(void)
{
        int v120 = atomic_xchg(&desktop.wheel, 0);
        int lines;
        unsigned int edges;
        struct pane *pane;

        if (!v120)
                return;

        pane = pane_under(desktop.cursor_x, desktop.cursor_y, &edges);

        if (!pane)
                return;

        lines = wheel_lines(v120, &desktop.wheel_remainder);
        if (!lines)
                return;

        if (pane->shared &&
            (READ_ONCE(pane->shared->want) & WINDOW_WANT_POINTER))
        {
                pointer_report(pane, desktop.cursor_x, desktop.cursor_y,
                               lines < 0 ? 65u : 64u, WINDOW_KEY_DOWN);
                return;
        }

        // The same way a console write asks for a frame. Damaging and
        // repainting from here draws before the cells are looked at again,
        // and the view lands a frame later or not at all.
        if (pane_scroll(pane, lines))
        {
                atomic_set(&desktop.frame_pending, 1);
                canvas_thread_wake();
        }
}

/* ---- output: outputs, what the hardware offers and what is taken from it ---- */

/*
        Canvas -- outputs

        An output is a rectangle of the desktop that one crtc scans out. They
        are laid left to right in the order their cards attached, which is the
        placement; the desktop is their bounding box.
*/

/*
        Whether this display is a window on somebody else's screen.

        The driver's own name, which is the only thing here that actually
        knows. Working it out from what the display reports does not succeed:
        a virtual one answers a physical size anyway, bochs says 320 by 200
        millimetres whatever mode it is in, and QEMU synthesises an EDID too,
        so neither the size nor the presence of one separates them.
*/
/* Is this device's driver one of these? Asked of two lists below, with
   the same answer for a device that has no driver or no name. */
static PURE _Bool canvas_driver_among(struct drm_device *dev,
                                      const char *const *names, positive count)
{
        if (!dev->driver || !dev->driver->name)
                return false;

        return string_table_find((string_address)dev->driver->name, names,
                                 sizeof(names[0]), count) < count;
}

static PURE _Bool canvas_is_virtual(struct drm_device *dev)
{
        static const char *const guests[] = {
            "bochs-drm", "virtio_gpu", "qxl", "vmwgfx", "cirrus-qemu",
            "hyperv_drm", "vkms"};

        return canvas_driver_among(dev, guests, array_count(guests));
}

/*
        The firmware's leftover framebuffer, not a GPU.

        simpledrm (and efidrm) bind to whatever GOP left in sysfb. On a
        Dell with i915 that is card0 at the firmware's size -- often
        1024x768 -- and Canvas starting there paints the kernel log into
        that buffer. i915 then takes the same pipe; the firmware client is
        kicked with a picture on it, and the machine freezes. It is a last
        resort when no real card appears.
*/
static PURE _Bool canvas_is_firmware(struct drm_device *dev)
{
        static const char *const firmware[] = {"simpledrm", "efidrm"};

        return canvas_driver_among(dev, firmware, array_count(firmware));
}

static PURE unsigned int output_mode_count(struct drm_connector *connector)
{
        struct drm_display_mode *mode;
        unsigned int count = 0;

        list_for_each_entry(mode, &connector->modes, head)
                count++;

        return count;
}

/*
        The best mode a connector lists among those that pass: every bit of
        type set, a type of 0 being any, and when width is not 0 a size equal
        to width by height or, unless exact, one that fits under it. Of those:
        a refresh equal to prefer_refresh first when it is positive, then the
        most pixels, then the highest refresh.

        Same size at 60 Hz and 120 Hz is the 120 Hz entry: that is what a
        Mac's virtio EDID actually offers, and what the cursor needs. A guest
        cap of seventy percent of a 120 Hz panel often still lists a larger
        60 Hz established timing under that cap; pixels first would take it
        and throw the refresh away, so the screen's rate comes first.
*/
static struct drm_display_mode *output_pick_mode(struct drm_connector *connector,
                                                 unsigned int type, int width,
                                                 int height, _Bool exact,
                                                 int prefer_refresh)
{
        struct drm_display_mode *mode, *best = NULL;
        int best_score = 0, best_refresh = 0;
        _Bool best_preferred = false;

        list_for_each_entry(mode, &connector->modes, head)
        {
                int score, refresh;
                _Bool preferred;

                if ((mode->type & type) != type)
                        continue;
                if (width && (exact ? mode->hdisplay != width ||
                                          mode->vdisplay != height
                                    : mode->hdisplay > width ||
                                          mode->vdisplay > height))
                        continue;
                if (mode->flags & (DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_DBLSCAN))
                        continue;

                refresh = drm_mode_vrefresh(mode);
                score = mode->hdisplay * mode->vdisplay;
                preferred = prefer_refresh > 0 && refresh == prefer_refresh;
                if (best && (preferred < best_preferred ||
                             (preferred == best_preferred &&
                              (score < best_score ||
                               (score == best_score && refresh <= best_refresh)))))
                        continue;

                best = mode;
                best_score = score;
                best_refresh = refresh;
                best_preferred = preferred;
        }

        return best;
}

/*
        The mode the first successful start put on a connector.

        Off and on probes again. A guest's preferred size is then the window
        already committed -- seventy percent of that, not of the host -- and
        a real screen's probe is the preferred mode, not the largest one
        startup took. Remembering the committed size keeps restart on the
        same picture.
*/
#define CANVAS_SAVED_MODES 8

struct canvas_saved_mode
{
        char name[32];
        int hdisplay;
        int vdisplay;
        int vrefresh;
};

static struct canvas_saved_mode canvas_saved_mode[CANVAS_SAVED_MODES];
static unsigned int canvas_saved_modes;

static const struct canvas_saved_mode *canvas_mode_saved(const char *name)
{
        unsigned int i;

        if (!name)
                return NULL;

        for (i = 0; i < canvas_saved_modes; i++)
                if (!strcmp(canvas_saved_mode[i].name, name))
                        return &canvas_saved_mode[i];

        return NULL;
}

static void canvas_mode_keep(const char *name, const struct drm_display_mode *mode)
{
        unsigned int i;

        if (!name || !mode)
                return;

        for (i = 0; i < canvas_saved_modes; i++)
                if (!strcmp(canvas_saved_mode[i].name, name))
                {
                        canvas_saved_mode[i].hdisplay = mode->hdisplay;
                        canvas_saved_mode[i].vdisplay = mode->vdisplay;
                        canvas_saved_mode[i].vrefresh = drm_mode_vrefresh(mode);
                        return;
                }

        if (canvas_saved_modes >= CANVAS_SAVED_MODES)
                return;

        strscpy(canvas_saved_mode[canvas_saved_modes].name, name,
                sizeof(canvas_saved_mode[0].name));
        canvas_saved_mode[canvas_saved_modes].hdisplay = mode->hdisplay;
        canvas_saved_mode[canvas_saved_modes].vdisplay = mode->vdisplay;
        canvas_saved_mode[canvas_saved_modes].vrefresh = drm_mode_vrefresh(mode);
        canvas_saved_modes++;
}

static void canvas_modes_keep(void)
{
        struct output *output;

        list_for_each_entry(output, &desktop.outputs, link)
        {
                struct drm_mode_set *set = output->mode_set;

                if (!set || !set->mode || !set->num_connectors ||
                    !set->connectors || !set->connectors[0] ||
                    !set->connectors[0]->name)
                        continue;

                canvas_mode_keep(set->connectors[0]->name, set->mode);
        }
}

/*
        The host's idea of the screen: the preferred mode, or the largest
        if the connector did not mark one.
*/
static struct drm_display_mode *output_screen_mode(struct drm_connector *connector)
{
        struct drm_display_mode *preferred = output_pick_mode(
            connector, DRM_MODE_TYPE_PREFERRED, 0, 0, false, 0);

        return preferred ? preferred
                         : output_pick_mode(connector, 0, 0, 0, false, 0);
}

/*
        A copy of src with its CRTC timings filled, or nothing.

        Connector modes leave crtc_clock at 0 until a modeset fills it.
        virtio-gpu's vblank timer reads that field; 0 means no vblank, and
        the next flip waits ten seconds then warns.
*/
static struct drm_display_mode *output_mode_take(struct drm_device *dev,
                                                 const struct drm_display_mode *src)
{
        struct drm_display_mode *taken;

        if (!src || src->clock <= 0)
                return NULL;

        taken = drm_mode_duplicate(dev, src);
        if (!taken)
                return NULL;

        drm_mode_set_crtcinfo(taken, CRTC_INTERLACE_HALVE_V);
        if (taken->crtc_clock <= 0)
        {
                drm_mode_destroy(dev, taken);
                return NULL;
        }

        return taken;
}

/*
        A guest is a window on somebody else's screen, so it cannot take the
        whole of that screen. Seventy percent of the host, in the pixels the
        host already scaled: cocoa then divides by the backing factor and
        centres the window, which is seventy percent of the DIP screen on a
        Retina panel.

        The mode has to be one the connector already listed. A CVT line for
        this size is a size virtio-gpu will scan out, but it arrives with
        crtc_clock still 0, the CRTC never generates vblank, and the first
        flip after the picture is on screen waits forever. The 120 Hz entry
        of a listed size is still preferred over a 60 Hz established timing
        of the same width and height. When seventy percent itself is not
        listed, the under-cap pick prefers the screen's refresh before it
        prefers more pixels.
*/
static struct drm_display_mode *output_guest_mode(struct drm_device *dev,
                                                  struct drm_connector *connector)
{
        struct drm_display_mode *screen, *listed;
        int w, h;

        screen = output_screen_mode(connector);
        if (!screen)
                return NULL;

        w = screen->hdisplay * 7 / 10;
        h = screen->vdisplay * 7 / 10;
        w &= ~1;
        h &= ~1;
        if (w < 2)
                w = 2;
        if (h < 2)
                h = 2;

        listed = output_pick_mode(connector, 0, w, h, true, 0);
        if (listed)
                return output_mode_take(dev, listed);

        listed = output_pick_mode(connector, 0, w, h, false, drm_mode_vrefresh(screen));
        if (listed)
                return output_mode_take(dev, listed);

        return output_mode_take(dev, screen);
}

/*
        Probes every connector and puts the best mode on each modeset.

        The lock order is the one drm_client_modeset_probe uses: the client's
        modesets, then the device's mode configuration, which is what guards a
        connector's list of modes.
*/
static COLD int canvas_probe_modes(struct canvas *canvas, _Bool biggest,
                                   _Bool keep_saved)
{
        struct drm_client_dev *client = &canvas->client;
        struct drm_device *dev = client->dev;
        struct drm_mode_set *mode_set;

        if (drm_client_modeset_probe(client, 0, 0))
                return -ENODEV;

        mutex_lock(&client->modeset_mutex);
        mutex_lock(&dev->mode_config.mutex);

        drm_client_for_each_modeset(mode_set, client)
        {
                struct drm_connector *connector;
                struct drm_display_mode *want, *taken;
                const struct canvas_saved_mode *saved;

                if (!mode_set->mode || !mode_set->num_connectors ||
                    !mode_set->connectors || !mode_set->connectors[0])
                        continue;

                connector = mode_set->connectors[0];
                saved = keep_saved && connector->name
                                ? canvas_mode_saved(connector->name)
                                : NULL;
                /*
                        A guest stays a window even when the first modeset is
                        refused. The retry used to take the probe's preferred
                        size, which is the host's whole screen, and that is
                        the blow-out the seventy percent cap exists to stop.

                        A later start has already committed once: take that
                        size rather than seventy percent of the window, or
                        the probe's preferred instead of the largest.

                        A hotplug on a running card must not: the first
                        picture is often a fallback written before EDID
                        finished, and restoring that size leaves a real
                        screen at 1024x768 for the rest of the boot.
                */
                if (saved)
                {
                        want = output_pick_mode(connector, 0, saved->hdisplay,
                                                saved->vdisplay, true,
                                                saved->vrefresh);
                        if (!want || drm_mode_equal(want, mode_set->mode))
                                continue;
                        taken = output_mode_take(dev, want);
                        if (!taken)
                                continue;
                }
                else if (canvas_is_virtual(dev))
                {
                        taken = output_guest_mode(dev, connector);
                        if (!taken)
                                continue;
                        if (drm_mode_equal(taken, mode_set->mode))
                        {
                                drm_mode_destroy(dev, taken);
                                continue;
                        }
                }
                else
                {
                        want = biggest ? output_pick_mode(connector, 0, 0, 0, false, 0)
                                       : output_pick_mode(connector, 0,
                                                          mode_set->mode->hdisplay,
                                                          mode_set->mode->vdisplay,
                                                          true, 0);
                        if (!want || drm_mode_equal(want, mode_set->mode))
                                continue;
                        taken = output_mode_take(dev, want);
                        if (!taken)
                                continue;
                }

                drm_mode_destroy(dev, mode_set->mode);
                mode_set->mode = taken;
        }

        mutex_unlock(&dev->mode_config.mutex);
        mutex_unlock(&client->modeset_mutex);

        desktop_sync_frame_ns();
        return 0;
}

static struct output *output_for_modeset(struct canvas *canvas,
                                         struct drm_mode_set *mode_set)
{
        struct output *output;

        list_for_each_entry(output, &desktop.outputs, link)
                if (output->canvas == canvas && output->mode_set == mode_set)
                        return output;

        return NULL;
}

/*
        Puts this output's buffer back on the modeset that is scanning it.

        A probe may have replaced the mode with a different size. The buffer
        we already have is the one on the CRTC; a modeset of a new size and
        the old framebuffer is refused, and destroying that framebuffer while
        the CRTC still holds it blanks the scanout. So the running size is
        restored, and a guest window that grew can wait.
*/
static void output_attach(struct output *output)
{
        struct drm_mode_set *mode_set = output->mode_set;
        struct drm_device *dev;
        struct drm_connector *connector;
        struct drm_display_mode *want, *taken;

        if (!mode_set || !output->buffer)
                return;

        if (!mode_set->mode)
                return;

        if (mode_set->mode->hdisplay == (int)output->width &&
            mode_set->mode->vdisplay == (int)output->height)
        {
                mode_set->fb = output->buffer->fb;
                return;
        }

        dev = output->canvas->client.dev;
        connector = (mode_set->connectors && mode_set->num_connectors)
                        ? mode_set->connectors[0]
                        : NULL;
        if (!connector)
                return;

        mutex_lock(&dev->mode_config.mutex);
        want = output_pick_mode(connector, 0, (int)output->width,
                                (int)output->height, true, 0);
        taken = output_mode_take(dev, want);
        mutex_unlock(&dev->mode_config.mutex);

        if (!taken)
                return;

        drm_mode_destroy(dev, mode_set->mode);
        mode_set->mode = taken;
        mode_set->fb = output->buffer->fb;
        desktop_sync_frame_ns();
}

/*
        A later probe listed a larger mode than the one already scanning.

        The first picture is often a fallback: i915 writes 1024x768 before
        EDID finishes, then hotplugs with the panel's real list. The buffer
        on the CRTC is the old size, so a modeset of the new size with that
        framebuffer is refused. A new buffer is made first; the old one is
        kept until the commit that points the pipe at the new one has
        landed, because freeing it sooner blanks the scanout.
*/
static _Bool output_grow(struct output *output, struct drm_mode_set *mode_set)
{
        unsigned int width, height;
        u32 format;
        struct drm_client_buffer *fresh;

        if (!mode_set || !mode_set->mode || !mode_set->crtc ||
            !mode_set->crtc->primary)
                return false;

        width = mode_set->mode->hdisplay;
        height = mode_set->mode->vdisplay;
        if ((unsigned long)width * height <=
            (unsigned long)output->width * output->height)
                return false;

        /* The flusher dirtyfb's output->buffer without desktop.lock.
           Replacing that buffer, or deleting it as replaced after a
           later commit, is a UAF. output_drop already waits; grow must
           too. */
        spin_lock(&desktop.flush_lock);
        if (output->flushing || output->flush_queued)
        {
                spin_unlock(&desktop.flush_lock);
                return false;
        }
        spin_unlock(&desktop.flush_lock);

        format = canvas_plane_pick_format(mode_set->crtc->primary,
                                          DRM_FORMAT_XRGB8888,
                                          DRM_FORMAT_ARGB8888);
        if (format == DRM_FORMAT_INVALID)
                return false;

        fresh = drm_client_buffer_create_dumb(&output->canvas->client, width,
                                              height, format);
        if (IS_ERR(fresh))
                return false;

        if (output->replaced)
                drm_client_buffer_delete(output->buffer);
        else
                output->replaced = output->buffer;

        output->buffer = fresh;
        output->width = width;
        output->height = height;
        output->opaque = format == DRM_FORMAT_ARGB8888 ? 0xff000000 : 0;
        canvas_palette(output->palette, format);
        mode_set->fb = fresh->fb;
        desktop_sync_frame_ns();
        return true;
}

static void desktop_place_outputs(void);
static struct output *output_add(struct canvas *canvas, struct drm_mode_set *mode_set);
static void output_disable_modeset(struct drm_device *dev,
                                   struct drm_mode_set *mode_set);

/*
        A screen with a mode: say which, and how much of a choice there was,
        then put an output on it. A mode that turns out to be wrong on a
        machine that is not here is answered by what its connector offered,
        not by what was picked out of it. A screen that cannot take an output
        has its mode set turned off.
*/
static _Bool output_bring_up(struct canvas *canvas, struct drm_mode_set *mode_set)
{
        struct drm_connector *connector =
            mode_set->num_connectors ? mode_set->connectors[0] : NULL;
        struct output *output;

        pr_info("[moonwater canvas] " "screen %s %ux%u at %u Hz, drawn %ux, %u mode(s) offered\n", connector && connector->name ? connector->name : "?", mode_set->mode->hdisplay, mode_set->mode->vdisplay, drm_mode_vrefresh(mode_set->mode), desktop.scale, connector ? output_mode_count(connector) : 0);

        output = output_add(canvas, mode_set);
        if (!output)
        {
                output_disable_modeset(canvas->client.dev, mode_set);
                return false;
        }

        list_add_tail(&output->link, &desktop.outputs);
        return true;
}

/*
        A hotplug after the first picture.

        drm_client_modeset_probe drops every modeset's framebuffer pointer.
        The old path treated a different mode -- or a different count of them
        -- as a reason to destroy the buffers still on the CRTCs. That is
        SET_SCANOUT 0. virtio-gpu then replaces the host window with
        "Display output is not active", and a later commit does not always
        get that window back.

        Guest displays also fire a hotplug about a second after the first
        scanout, when the host window is up. That callback runs on the DRM
        helper workqueue. A commit from here disables every cursor plane
        (drm_client_modeset_commit does) and can wait on that same queue,
        so the pointer thread never runs again and the plane is left off.
        The mode already scanning is the one the window has; leave it.

        A real card is the same queue. i915 hotplugs after EDID, and with a
        cursor plane the commit from here is the virtio lockup: low-res
        kernel log, no pointer, no terminal. The callback only queues; this
        runs on moonwater/plug. A guest still returns above. A real screen
        may grow if the connector now lists more pixels than the fallback
        that was committed first; a hotplug that does not grow does not
        commit, so the cursor plane stays up.
*/
static int canvas_rebind(struct canvas *canvas)
{
        struct drm_client_dev *client = &canvas->client;
        struct drm_mode_set *mode_set;
        struct output *output;
        _Bool placed = false;

        if (canvas_is_virtual(client->dev) && !(canvas_pm_dark & 1))
                return 0;

        if (canvas_probe_modes(canvas,
                               IS_ENABLED(CONFIG_MOONWATER_CANVAS_LARGEST_MODE),
                               false))
        {
                desktop_redraw();
                return 0;
        }

        mutex_lock(&client->modeset_mutex);
        drm_client_for_each_modeset(mode_set, client)
        {
                if (!mode_set->mode)
                        continue;

                output = output_for_modeset(canvas, mode_set);
                if (output)
                {
                        if (output_grow(output, mode_set))
                                placed = true;
                        else
                                output_attach(output);
                        continue;
                }

                if (output_bring_up(canvas, mode_set))
                        placed = true;
        }
        mutex_unlock(&client->modeset_mutex);

        if (placed)
        {
                desktop_place_outputs();
                canvas_modes_keep();
                desktop_redraw();
        }
        else if (!desktop_lit())
        {
                /*
                        Nothing grew, and the driver no longer shows Canvas's
                        picture: a connector that went and came back (a panel
                        after a wake) leaves a mode of the same size, which
                        used to mean no commit and a screen that stayed off.
                */
                if (!desktop_taken())
                {
                        pr_info("[moonwater canvas] " "hotplug: the screen is not showing the desktop, drawing it again\n");
                        desktop_redraw();
                }
        }

        return 0;
}

static void desktop_place_outputs(void)
{
        struct output *output;
        int x = 0, height = 0;

        list_for_each_entry(output, &desktop.outputs, link)
        {
                output->x = x;
                output->y = 0;

                x += (int)output->width;
                height = max(height, (int)output->height);
        }

        desktop.width = x;
        desktop.height = height;

        desktop_gather_panes();
        desktop_sync_frame_ns();
}

static struct output *output_add(struct canvas *canvas, struct drm_mode_set *mode_set)
{
        unsigned int width = mode_set->mode->hdisplay;
        unsigned int height = mode_set->mode->vdisplay;
        u32 format = canvas_plane_pick_format(mode_set->crtc->primary,
                                              DRM_FORMAT_XRGB8888,
                                              DRM_FORMAT_ARGB8888);
        struct output *output;

        if (format == DRM_FORMAT_INVALID)
        {
                pr_err("[moonwater canvas] " "no 32 bit format on this plane, skipping output\n");
                return NULL;
        }

        output = kzalloc(sizeof(*output), GFP_KERNEL);
        if (!output)
                return NULL;

        output->buffer = drm_client_buffer_create_dumb(&canvas->client, width, height, format);
        if (IS_ERR(output->buffer))
        {
                pr_err("[moonwater canvas] " "could not create a %ux%u scanout buffer\n", width, height);
                kfree(output);
                return NULL;
        }

        output->canvas = canvas;
        output->mode_set = mode_set;
        output->width = width;
        output->height = height;
        output->opaque = format == DRM_FORMAT_ARGB8888 ? 0xff000000 : 0;
        canvas_palette(output->palette, format);
        mode_set->fb = output->buffer->fb;

        if (!output_describe(output))
        {
                drm_client_buffer_delete(output->buffer);
                mode_set->fb = NULL;
                kfree(output);
                return NULL;
        }

        plane_claim(&canvas->client, output);

        return output;
}

/*
        A modeset nothing will be drawn on, left the way a probe leaves one.

        A screen that could not be given a buffer -- no 32 bit format on its
        plane, no memory for one -- still carries the mode the probe chose and
        no framebuffer to go with it, and that combination is not one screen
        missing: the commit carrying it is refused whole, so one connector
        nobody can draw on takes every other screen on the card down with it.

        Clearing it is what says "this crtc is off", and then the screens that
        did build commit without it.
*/
static void output_disable_modeset(struct drm_device *dev,
                                   struct drm_mode_set *mode_set)
{
        unsigned int i;

        if (mode_set->mode)
        {
                drm_mode_destroy(dev, mode_set->mode);
                mode_set->mode = NULL;
        }

        mode_set->fb = NULL;

        for (i = 0; mode_set->connectors && i < (unsigned int)mode_set->num_connectors; i++)
        {
                drm_connector_put(mode_set->connectors[i]);
                mode_set->connectors[i] = NULL;
        }

        mode_set->num_connectors = 0;
}

static void output_free(struct output *output)
{
        drm_client_buffer_delete(output->buffer);
        drm_client_buffer_delete(output->replaced);
        kfree(output);
}

static void output_drop(struct output *output)
{
        _Bool flushing;

        plane_drop(output);

        // A failed disable leaves a client buffer that recovery can no longer
        // reach after this output is gone. RMFB drops the client ownership;
        // atomic plane state keeps scanout alive even if removal also fails.
        cursor_buffers_drop(output);

        list_del(&output->link);

        /*
                Off the flusher's queue, and left to the flusher if its buffer
                is with the driver right now: deleting it under a dirtyfb in
                flight frees what the driver is using. output_flush_done frees
                it instead, and client_unregister waits for that.
        */
        spin_lock(&desktop.flush_lock);
        if (output->flush_queued)
        {
                list_del_init(&output->flush_link);
                output->flush_queued = false;
        }
        flushing = output->flushing;
        output->retired = flushing;
        if (flushing)
                atomic_fetch_add(1, &output->canvas->retiring);
        spin_unlock(&desktop.flush_lock);

        if (!flushing)
                output_free(output);
}

/*
        Puts every output's buffer back on the modeset that scans it out.

        A probe releases every modeset, and releasing one takes its framebuffer
        away. A modeset carrying a mode and no framebuffer is refused, the
        whole commit with it, and what stays on the screen is whatever was
        there before this ever ran: on a machine that inherits the firmware's
        picture that is a cursor moving over it and nothing else.

        So it is set before every commit rather than once when the output was
        made. Whoever cleared it, and for whatever reason, it is right again by
        the time it matters.
*/
static void desktop_attach_buffers(void)
{
        struct output *output;

        list_for_each_entry(output, &desktop.outputs, link)
        {
                struct drm_client_dev *client = &output->canvas->client;

                if (!output->buffer)
                        continue;

                mutex_lock(&client->modeset_mutex);
                output->mode_set->fb = output->buffer->fb;
                mutex_unlock(&client->modeset_mutex);
        }
}

/*
        A card's outputs are added together, so they are consecutive here and
        remembering the last one is enough to commit each card once.

        The answer used to be thrown away. A commit is the only thing that puts
        a mode on a screen, and one that refuses says so in the one place that
        could have noticed.

        known is a card committed a moment ago whose answer was known_set; it
        is not committed a second time. canvas_start commits a new card on its
        own to learn whether the mode sets, and committing it again straight
        after meant a second atomic commit queued behind the first one's
        vblank before the first picture was flushed.
*/
static _Bool desktop_commit_known(struct canvas *known, int known_set)
{
        struct canvas *committed = NULL;
        struct drm_rect cursor;
        struct output *output;
        _Bool complete = true;

        desktop_attach_buffers();
        cursor_cell(&cursor, desktop.cursor_x, desktop.cursor_y,
                    desktop.cursor_shape, desktop.cursor_scale);

        list_for_each_entry(output, &desktop.outputs, link)
        {
                int set;

                if (output->canvas == committed)
                        continue;

                committed = output->canvas;
                set = committed == known
                          ? known_set
                          : drm_client_modeset_commit(&committed->client);
                if (!set)
                {
                        struct output *grown;

                        list_for_each_entry(grown, &desktop.outputs, link)
                                if (grown->canvas == committed && grown->replaced)
                                {
                                        drm_client_buffer_delete(grown->replaced);
                                        grown->replaced = NULL;
                                }
                }

                // Somebody else is master, and the card is not ours to draw
                // on until they let go. The loop is woken so it watches for that.
                if (set == -EBUSY || set == -EACCES)
                {
                        desktop.suspended = true;
                        canvas_thread_wake();
                }

                if (set)
                        complete = false;

                // EBUSY is not now: something else is the device's master and
                // the next commit is the one that lands.
                if (set == -EBUSY || set == committed->set_result)
                        continue;

                committed->set_result = set;

                if (set)
                        pr_info("[moonwater canvas] " "%ux%u would not go on the screen (%d)\n", output->width, output->height, set);
                else
                        pr_info("[moonwater canvas] " "%ux%u is on the screen\n", output->width, output->height);
        }

        list_for_each_entry(output, &desktop.outputs, link)
                cursor_arm_output(output,
                                  output_touched(output, &cursor, 1));

        return complete;
}

static _Bool desktop_commit(void)
{
        return desktop_commit_known(NULL, 0);
}

// A failed cursor-plane disable may leave the old image live over the
// software fallback. A full client commit disables all non-primary planes.
static void cursor_plane_recover(void)
{
        struct output *output;
        _Bool complete;

        if (!cursor_plane_recovery)
                return;

        list_for_each_entry(output, &desktop.outputs, link)
                if (output->cursor_recovery == 1)
                        output->cursor_recovery = 2;

        cursor_plane_recovery = false;
        complete = desktop_commit();

        // The successful full commit covered state 2. A failure while its
        // post-commit cursor arms ran is new state 1 and needs the next pass.
        list_for_each_entry(output, &desktop.outputs, link)
        {
                if (output->cursor_recovery != 2)
                        continue;

                if (!complete)
                {
                        output->cursor_recovery = 1;
                        continue;
                }

                cursor_buffers_drop(output);

                output->cursor_recovery = 0;
        }

        if (!complete)
                cursor_plane_recovery = true;
}

/*
        Every output drawn again, and nothing asked of the modes.

        What a window opening, closing or changing focus needs: the pixels,
        handed to the flusher like any other frame. desktop_redraw below also
        commits every modeset, which is what a probe, a resume or a first
        start needs and nothing else does -- and the commit is an atomic one
        that waits out the flush already in flight and then a vblank of its
        own, under desktop.lock. On virtio-gpu that was 20 to 47 ms per call,
        with every committing program and the canvas thread queued behind it:
        opening the first terminal held that terminal inside its create ioctl
        for 46 ms, and its first frame waited 20 ms more for the focus.
*/
static void desktop_recompose(void)
{
        u64 started = ktime_get_ns();
        struct output *output;

        list_for_each_entry(output, &desktop.outputs, link)
                compose_output(output);

        canvas_composes++;
        canvas_compose_ns += ktime_get_ns() - started;
}

static void desktop_redraw(void)
{
        desktop_recompose();
        desktop_commit();
}

/*
        Whether a program other than this one is master of a card Canvas draws
        on: Weston, a game, anything that opened the card for itself.

        In-kernel clients never become master, so a device with one has a
        program in front of it. Tested under the device's master_mutex, taken
        inside desktop.lock the way drm_client_modeset_commit already takes
        it, and never dereferenced: the master can go the moment the lock is
        dropped, and all this answers is whether there was one.
*/
static _Bool desktop_taken(void)
{
        struct drm_device *checked = NULL;
        struct output *output;
        _Bool taken = false;

        // The card's power is going, or has not come back: as good as taken.
        if (desktop.asleep)
                return true;

        list_for_each_entry(output, &desktop.outputs, link)
        {
                struct drm_device *dev = output->canvas->client.dev;

                if (dev == checked)
                        continue;

                checked = dev;
                mutex_lock(&dev->master_mutex);
                taken = dev->master != NULL;
                mutex_unlock(&dev->master_mutex);

                if (taken)
                        break;
        }

        return taken;
}

/*
        The card is Canvas's again.

        Nothing Canvas remembers about the screen can be trusted after another
        program had it: the mode on each crtc is theirs, and so may be the
        image, the position and whether there is anything at all on the cursor
        plane. So the windows are read again, everything is drawn and
        committed, which puts Canvas's modes back and disables every plane
        but the primary, and each cursor plane is painted and armed afresh
        under a new request generation that only counts as armed once every
        output showing the cursor has it back: on its plane where it has one,
        and drawn by the redraw where it has none.
*/
static void desktop_resume(void)
{
        struct drm_rect cursor;
        struct output *output;
        _Bool presented = false;
        _Bool complete = true;

        desktop.suspended = false;

        cursor_plane_requested_generation++;
        cursor_plane_requested_x = desktop.cursor_x;
        cursor_plane_requested_y = desktop.cursor_y;

        list_for_each_entry(output, &desktop.outputs, link)
                output->cursor_shape = ~0u;

        desktop_refresh_panes();
        desktop_redraw();
        cursor_plane_recover();

        cursor_cell(&cursor, desktop.cursor_x, desktop.cursor_y,
                    desktop.cursor_shape, desktop.cursor_scale);

        list_for_each_entry(output, &desktop.outputs, link)
        {
                if (!output_touched(output, &cursor, 1))
                        continue;

                // An output with no cursor plane had its cursor drawn by the
                // redraw above; one with a plane has it only if it is shown.
                if (!output->cursor_plane || output->cursor_shown)
                        presented = true;
                else
                        complete = false;
        }

        if (presented && complete && !desktop.suspended)
        {
                cursor_plane_armed_generation = cursor_plane_requested_generation;
                cursor_plane_armed_x = desktop.cursor_x;
                cursor_plane_armed_y = desktop.cursor_y;
        }
}

/*
        Whether the driver's own record has every screen showing what Canvas
        drew, and what it says, said.

        A wake, or a hotplug that changes no size, leaves nothing else to go on:
        the commit that answered zero may have been for a pipe the driver had
        already written off, and a probe that did not grow the mode used to
        commit nothing at all. Read under the device's modeset locks, the order
        drm_client_modeset_commit takes them in after desktop.lock.
*/
static _Bool output_lit_locked(struct output *output)
{
        struct drm_crtc *crtc = output->mode_set ? output->mode_set->crtc : NULL;
        struct drm_plane *primary = crtc ? crtc->primary : NULL;

        return crtc && primary && crtc->state && primary->state &&
               crtc->state->active && crtc->state->enable &&
               primary->state->crtc == crtc && output->buffer &&
               primary->state->fb == output->buffer->fb;
}

static _Bool desktop_lit(void)
{
        struct output *output;
        _Bool lit = true;

        list_for_each_entry(output, &desktop.outputs, link)
        {
                struct drm_device *dev = output->canvas->client.dev;

                drm_modeset_lock_all(dev);
                lit &= output_lit_locked(output);
                drm_modeset_unlock_all(dev);
        }

        return lit;
}

static void desktop_report(const char *tag)
{
        struct output *output;

        list_for_each_entry(output, &desktop.outputs, link)
        {
                struct drm_device *dev = output->canvas->client.dev;
                struct drm_crtc *crtc = output->mode_set->crtc;
                struct drm_connector *connector = output->mode_set->num_connectors
                                                      ? output->mode_set->connectors[0]
                                                      : NULL;
                struct iosys_map map;
                _Bool mapped = output_map(output, &map);
                int active, enable, lit;

                drm_modeset_lock_all(dev);
                active = crtc && crtc->state ? crtc->state->active : -1;
                enable = crtc && crtc->state ? crtc->state->enable : -1;
                lit = output_lit_locked(output);
                drm_modeset_unlock_all(dev);

                if (mapped)
                        drm_client_buffer_vunmap_local(output->buffer);

                pr_info("[moonwater canvas] " "%s: %s: crtc active %d enabled %d, %s, connector status %d dpms %d, buffer %s, last commit %d, cursor %s\n",
                        tag, connector && connector->name ? connector->name : "?",
                        active, enable, lit ? "primary shows ours" : "primary does NOT show ours",
                        connector ? (int)connector->status : -1,
                        connector ? connector->dpms : -1,
                        mapped ? (map.is_iomem ? "maps (device memory)" : "maps") : "does NOT map",
                        output->canvas->set_result,
                        output->cursor_plane ? (output->cursor_shown ? "plane shown" : "plane off")
                                             : "drawn");
        }
}

/*
        Every screen off or on through the client's own modesets, one card at a
        time: an active-false commit, and the on that follows it is an
        active-changed one, which is the only kind a driver whose idea of the
        pipe has drifted from the hardware's cannot skip.
*/
static void desktop_dpms(int mode)
{
        struct canvas *done = NULL;
        struct output *output;

        desktop_attach_buffers();

        list_for_each_entry(output, &desktop.outputs, link)
        {
                int ret;

                if (output->canvas == done)
                        continue;

                done = output->canvas;
                ret = drm_client_modeset_dpms(&done->client, mode);
                pr_info("[moonwater canvas] " "screens %s: %d\n", mode == DRM_MODE_DPMS_ON ? "on" : "off", ret);
        }
}

// Whatever desktop_refresh_panes recorded, or the whole thing when it gave up
// counting.
static void desktop_repaint(void)
{
        struct output *output;

        // Nothing recorded means nothing changed. It used to mean repaint
        // every screen, which is the opposite.
        if (!desktop.damage_count && !desktop.damage_all)
                return;

        if (desktop.damage_all)
        {
                desktop.damage_count = 0;
                desktop.damage_all = false;
                desktop_recompose();
                return;
        }

        list_for_each_entry(output, &desktop.outputs, link)
        {
                if (!output_touched(output, desktop.damage, desktop.damage_count))
                        continue;

                output_repaint(output, desktop.damage, desktop.damage_count);
        }

        desktop.damage_count = 0;
}

static COLD void canvas_release(struct canvas *canvas);

static int canvas_build(struct canvas *canvas, _Bool biggest)
{
        struct drm_client_dev *client = &canvas->client;
        struct drm_mode_set *mode_set;
        unsigned int count = 0;

        if (canvas_probe_modes(canvas, biggest, true))
                return -ENODEV;

#ifdef CONFIG_MOONWATER_CANVAS_SCALE
        desktop.scale = CONFIG_MOONWATER_CANVAS_SCALE;
#endif
        if (desktop.scale < 1)
                desktop.scale = 1;

        mutex_lock(&client->modeset_mutex);
        drm_client_for_each_modeset(mode_set, client)
        {
                if (!mode_set->mode)
                        continue;

                if (output_bring_up(canvas, mode_set))
                        count++;
        }
        mutex_unlock(&client->modeset_mutex);

        if (!count)
                return -ENODEV;

        desktop_place_outputs();

        return 0;
}

/*
        A terminal, started on the canvas thread.

        Canvas opens no window by itself: the kernel log and the terminals a
        desktop starts with are the machine script's, asked for on the canvas
        on event through moonwater canvas log and moonwater canvas terminal.
        That also settles what the first terminal used to wait for: a
        terminal started before the initcalls finished exited without a
        window, and a request cannot arrive from userspace before then.

        Outside desktop.lock: canvas_start holds that lock for the first
        picture, and the terminal's first WINDOW ioctl takes it too, so
        spawning under it would wait out the child's open. The thread starts
        Control-Shift-T the same way.
*/
static void canvas_terminal_spawn(void)
{
        atomic_set(&desktop.spawn, 1);
        canvas_thread_wake();
}

/*
        The biggest mode every screen offers, and what to do when it will not
        set.

        A monitor listing a mode is not a promise the link can carry it,
        especially with more than one screen sharing the bandwidth, so a
        refused commit falls back to the mode the probe would have chosen --
        which is the one that used to be taken unconditionally. A guest is
        already capped: retrying still asks for seventy percent of the host,
        not the probe's native size.
*/
static int canvas_start(struct canvas *canvas)
{
        /*
                A refused commit means the mode, not the moment.

                The claim file is closed before drm_client_register, and this
                runs from moonwater/plug after that, so a commit is not EBUSY
                because we still hold master. EBUSY here is another program.
                Falling back on that threw away every mode this ever chose and
                quietly took the probe's, which is the opposite of the point.
        */
        int ret;

        for (unsigned int attempt = 0; ; attempt++)
        {
                ret = canvas_build(canvas, !attempt &&
                    IS_ENABLED(CONFIG_MOONWATER_CANVAS_LARGEST_MODE));

                if (ret)
                {
                        pr_err("[moonwater canvas] " "no screen to draw on (%d), leaving the display alone\n", ret);
                        return ret;
                }

                desktop_attach_buffers();
                ret = drm_client_modeset_commit(&canvas->client);
                if (!ret || ret == -EBUSY)
                        break;

                // A rejected mode owns no useful picture. Release its outputs
                // before retrying, or standing aside for the console client.
                if (attempt)
                        pr_err("[moonwater canvas] " "no mode would set (%d), leaving the display alone\n", ret);
                else
                        pr_info("[moonwater canvas] " "that mode would not set (%d), taking the offered one\n", ret);
                canvas_release(canvas);
                if (attempt)
                        return ret;
        }

        // The cursor is drawn from a bitmap like everything else, so it is the
        // same sixteen pixels and the same too small without this.
        desktop.cursor_scale = desktop.scale;
        desktop.drawn_scale = desktop.scale;
        canvas_modes_keep();

        if (!desktop.started)
        {
                desktop.started = true;
                desktop.cursor_x = desktop.width / 2;
                desktop.cursor_y = desktop.height / 2;
                desktop.drawn_x = desktop.cursor_x;
                desktop.drawn_y = desktop.cursor_y;
                atomic_set(&desktop.pending_x, desktop.cursor_x);
                atomic_set(&desktop.pending_y, desktop.cursor_y);
        }

        // Asked for before the desktop had a size. Before the redraw, so the
        // first frame already carries it.
        if (desktop.log_wanted)
                console_start();

        // The redraw, less the commit this card has just had: the picture
        // goes to the flusher and the answer above is what is reported.
        desktop_recompose();
        desktop_commit_known(canvas, ret);

        {
                struct output *output;
                unsigned int count = 0;

                list_for_each_entry(output, &desktop.outputs, link)
                        count++;

                pr_info("[moonwater canvas] " "desktop %dx%d, %u output(s)\n", desktop.width, desktop.height, count);
        }

        if (desktop.terminal_wanted)
                canvas_terminal_spawn();
        desktop.terminal_wanted = false;

        return 0;
}

static COLD void canvas_release(struct canvas *canvas)
{
        struct output *output, *next;

        list_for_each_entry_safe(output, next, &desktop.outputs, link)
                if (output->canvas == canvas)
                        output_drop(output);

        desktop_place_outputs();

        if (!list_empty(&desktop.outputs))
                desktop_redraw();
}

/* ---- keys: the keyboard ---- */

/*
        Canvas -- keys

        What a keyboard means is the compositor's to decide, the same way what
        a string looks like is. A program is handed characters, not scancodes
        and a table of its own.

        The default table is US ASCII. `moonwater keyboard` switches the live
        map; AltGr is level 3, not compositor Alt. Dead keys and compose are
        still a larger table and the same shape of code.
*/

/*
        Wide enough for the keypad, which is the last thing on a keyboard that
        means a character. Everything past it -- the arrows, Home and its
        neighbours, the function keys -- means no character at all, and is
        carried to a program as key.code with a character of zero, which is
        what code exists for.
*/
#define KEY_TABLE 128
#define KEY_LEVELS 4
#ifndef KEY_102ND
#define KEY_102ND 86
#endif

/*
        Backspace is DEL, not BS.

        A terminal line discipline erases on VERASE, which is 127 everywhere,
        and treats 8 as an ordinary character -- so sending 8 echoed as ^H and
        made the line two characters longer for every press.
*/
/*
        Which modifier a key is, in the held word's own spelling.

        A right-hand key is its left-hand flag moved up four bits, so
        keyboard_modifiers can put the side back by shifting down and one word
        carries both hands. That works for shift, control and alt because
        their flags are 2, 4 and 8 and nothing else lives at 32, 64 and 128.

        AltGr is the one that does not fit. WINDOW_KEY_CONTROL << 4 is 64 and
        WINDOW_KEY_ALTGR is 64, so right Control and right Alt were the same
        bit in this table: holding right Control selected the AltGr level of
        the map, holding right Alt read as Control -- so Control chords fired
        on it and Alt-Tab did not -- and either key held made the other's
        release go out on disconnect. AltGr is also not the other side of
        Alt, which is why it cannot simply be WINDOW_KEY_ALT << 4: since
        af20afca right Alt means level three of the map, not compositor Alt.

        So the held word gets a bit of its own, above everything the shift can
        reach and deliberately not a window flag, and keyboard_modifiers
        spells it as WINDOW_KEY_ALTGR on the way out. The flag itself does not
        move: it is in window_key.flags, which is what every client reads.

        WINDOW_KEY_SHIFT << 4 is 32, which is also WINDOW_KEY_POINTER_MOVE,
        and that one is harmless rather than lucky: a held word is never a
        client's flags. What reaches window_key.flags is keyboard_modifiers'
        answer, which carries no bit above 8 but the one it puts there, and a
        pointer's flags never enter a keyboard's held word.
*/
#define KEY_HELD_ALTGR 256u

static unsigned short key_mod[KEY_TABLE] = {
    [KEY_LEFTSHIFT] = WINDOW_KEY_SHIFT,
    [KEY_RIGHTSHIFT] = WINDOW_KEY_SHIFT << 4,
    [KEY_LEFTCTRL] = WINDOW_KEY_CONTROL,
    [KEY_RIGHTCTRL] = WINDOW_KEY_CONTROL << 4,
    [KEY_LEFTALT] = WINDOW_KEY_ALT,
    [KEY_RIGHTALT] = KEY_HELD_ALTGR,
};
static unsigned short key_live[KEY_TABLE][KEY_LEVELS];
static char canvas_layout_held[8] = "us";
static bool key_ready;

struct key_patch {
        unsigned char code;
        unsigned short level[KEY_LEVELS];
};

static const unsigned short key_us[KEY_TABLE][KEY_LEVELS] = {
    [1] = {27, 27},
    [2] = {'1', '!'},  [3] = {'2', '@'},  [4] = {'3', '#'},
    [5] = {'4', '$'},  [6] = {'5', '%'},  [7] = {'6', '^'},
    [8] = {'7', '&'},  [9] = {'8', '*'},  [10] = {'9', '('},
    [11] = {'0', ')'}, [12] = {'-', '_'}, [13] = {'=', '+'},
    [14] = {127, 127}, [15] = {'\t', '\t'},
    [16] = {'q', 'Q'}, [17] = {'w', 'W'}, [18] = {'e', 'E'},
    [19] = {'r', 'R'}, [20] = {'t', 'T'}, [21] = {'y', 'Y'},
    [22] = {'u', 'U'}, [23] = {'i', 'I'}, [24] = {'o', 'O'},
    [25] = {'p', 'P'}, [26] = {'[', '{'}, [27] = {']', '}'},
    [28] = {'\n', '\n'},
    [30] = {'a', 'A'}, [31] = {'s', 'S'}, [32] = {'d', 'D'},
    [33] = {'f', 'F'}, [34] = {'g', 'G'}, [35] = {'h', 'H'},
    [36] = {'j', 'J'}, [37] = {'k', 'K'}, [38] = {'l', 'L'},
    [39] = {';', ':'}, [40] = {'\'', '"'}, [41] = {'`', '~'},
    [43] = {'\\', '|'},
    [44] = {'z', 'Z'}, [45] = {'x', 'X'}, [46] = {'c', 'C'},
    [47] = {'v', 'V'}, [48] = {'b', 'B'}, [49] = {'n', 'N'},
    [50] = {'m', 'M'}, [51] = {',', '<'}, [52] = {'.', '>'},
    [53] = {'/', '?'}, [55] = {'*', '*'}, [57] = {' ', ' '},
    [71] = {'7', '7'}, [72] = {'8', '8'}, [73] = {'9', '9'},
    [74] = {'-', '-'}, [75] = {'4', '4'}, [76] = {'5', '5'},
    [77] = {'6', '6'}, [78] = {'+', '+'}, [79] = {'1', '1'},
    [80] = {'2', '2'}, [81] = {'3', '3'}, [82] = {'0', '0'},
    [83] = {'.', '.'}, [96] = {'\n', '\n'}, [98] = {'/', '/'},
};

static const struct key_patch key_uk[] = {
        {3, {'2', '"', '@'}},
        {4, {'3', 0xA3, 0xA3}},
        {40, {'\'', '@'}},
        {41, {'`', 0xAC, 0xA6}},
        {43, {'#', '~', '\\'}},
        {KEY_102ND, {'\\', '|'}},
};

static const struct key_patch key_de[] = {
        {12, {0xDF, '?', '\\'}},
        {13, {0xB4, '`'} },
        {21, {'z', 'Z'}},
        {26, {0xFC, 0xDC}},
        {27, {'+', '*', '~'}},
        {39, {0xF6, 0xD6}},
        {40, {0xE4, 0xC4}},
        {41, {'^', 0xB0}},
        {43, {'#', '\''}},
        {44, {'y', 'Y'}},
        {KEY_102ND, {'<', '>', '|'}},
        {8, {'7', '/', '{'}},
        {9, {'8', '(', '['}},
        {10, {'9', ')', ']'}},
        {11, {'0', '=', '}'}},
        {16, {'q', 'Q', '@'}},
        {18, {'e', 'E', 0x20AC}},
};

static const struct key_patch key_se[] = {
        {3, {'2', '"', '@'}},
        {4, {'3', '#', 0xA3}},
        {5, {'4', 0xA4, '$'}},
        {6, {'5', '%', 0x20AC}},
        {7, {'6', '&'}},
        {8, {'7', '/', '{'}},
        {9, {'8', '(', '['}},
        {10, {'9', ')', ']'}},
        {11, {'0', '=', '}'}},
        {12, {'+', '?', '\\'}},
        {13, {0xB4, '`'}},
        {26, {0xE5, 0xC5}},
        {27, {0xA8, '^', '~'}},
        {39, {0xF6, 0xD6}},
        {40, {0xE4, 0xC4}},
        {41, {0xA7, 0xBD}},
        {43, {'\'', '*'}},
        {53, {'-', '_'}},
        {KEY_102ND, {'<', '>', '|'}},
};

static const struct key_patch key_no[] = {
        {3, {'2', '"', '@'}},
        {4, {'3', '#', 0xA3}},
        {5, {'4', 0xA4, '$'}},
        {6, {'5', '%', 0x20AC}},
        {8, {'7', '/', '{'}},
        {9, {'8', '(', '['}},
        {10, {'9', ')', ']'}},
        {11, {'0', '=', '}'}},
        {12, {'+', '?'}},
        {13, {'\\', '`', 0xB4}},
        {26, {0xE5, 0xC5}},
        {39, {0xF8, 0xD8}},
        {40, {0xE6, 0xC6}},
        {41, {'|', 0xA7}},
        {43, {'\'', '*'}},
        {53, {'-', '_'}},
        {KEY_102ND, {'<', '>'}},
};

static const struct key_patch key_fr[] = {
        {2, {'&', '1'}},
        {3, {0xE9, '2', '~'}},
        {4, {'"', '3', '#'}},
        {5, {'\'', '4', '{'}},
        {6, {'(', '5', '['}},
        {7, {'-', '6', '|'}},
        {8, {0xE8, '7', '`'}},
        {9, {'_', '8', '\\'}},
        {10, {0xE7, '9', '^'}},
        {11, {0xE0, '0', '@'}},
        {12, {')', 0xB0, ']'}},
        {13, {'=', '+', '}'}},
        {16, {'a', 'A'}},
        {17, {'z', 'Z'}},
        {18, {'e', 'E', 0x20AC}},
        {26, {'^', 0xA8}},
        {27, {'$', 0xA3, 0xA4}},
        {30, {'q', 'Q'}},
        {38, {'m', 'M'}},
        {39, {0xF9, '%'}},
        {40, {'*', 0xB5}},
        {44, {'w', 'W'}},
        {50, {',', '?'}},
        {51, {';', '.'}},
        {52, {':', '/'}},
        {53, {'!', 0xA7}},
        {KEY_102ND, {'<', '>'}},
};

static const struct key_patch key_es[] = {
        {3, {'2', '"', '@'}},
        {4, {'3', 0xB7, '#'}},
        {8, {'7', '/', '{'}},
        {11, {'0', '=', '}'}},
        {12, {'\'', '?'}},
        {13, {0xA1, 0xBF}},
        {26, {'`', '^', '['}},
        {27, {'+', '*', ']'}},
        {39, {0xF1, 0xD1}},
        {40, {0xB4, 0xA8, '{'}},
        {41, {0xBA, 0xAA, '\\'}},
        {43, {0xE7, 0xC7, '}'}},
        {KEY_102ND, {'<', '>'}},
        {18, {'e', 'E', 0x20AC}},
};

static const struct key_patch key_it[] = {
        {3, {'2', '"'}},
        {4, {'3', 0xA3}},
        {8, {'7', '{'}},
        {9, {'8', '['}},
        {10, {'9', ']'}},
        {11, {'0', '}'}},
        {12, {'\'', '?'}},
        {13, {0xEC, '^'}},
        {26, {0xE8, 0xE9, '['}},
        {27, {'+', '*', ']'}},
        {39, {0xF2, 0xE7, '@'}},
        {40, {0xE0, 0xB0, '#'}},
        {41, {'\\', '|'}},
        {43, {0xF9, 0xA7}},
        {KEY_102ND, {'<', '>'}},
        {18, {'e', 'E', 0x20AC}},
};

static void key_load_us(void)
{
        memset(key_live, 0, sizeof(key_live));
        memcpy(key_live, key_us, sizeof(key_us));
}

static void key_patch_apply(const struct key_patch *patch, unsigned int count)
{
        unsigned int at;

        for (at = 0; at < count; at++)
                memcpy(key_live[patch[at].code], patch[at].level,
                       sizeof(patch[at].level));
}

static const char *canvas_layout_name(void)
{
        if (!key_ready)
        {
                key_load_us();
                key_ready = true;
                strscpy(canvas_layout_held, "us", sizeof(canvas_layout_held));
        }
        return canvas_layout_held;
}

static long canvas_layout_set(const char *name)
{
        key_load_us();
        key_ready = true;
        if (!name || !strcmp(name, "us"))
        {
                strscpy(canvas_layout_held, "us", sizeof(canvas_layout_held));
                return 0;
        }
        if (!strcmp(name, "uk") || !strcmp(name, "gb"))
        {
                key_patch_apply(key_uk, ARRAY_SIZE(key_uk));
        }
        else if (!strcmp(name, "de"))
                key_patch_apply(key_de, ARRAY_SIZE(key_de));
        else if (!strcmp(name, "se") || !strcmp(name, "sv") || !strcmp(name, "fi"))
                key_patch_apply(key_se, ARRAY_SIZE(key_se));
        else if (!strcmp(name, "no") || !strcmp(name, "nb") || !strcmp(name, "dk"))
                key_patch_apply(key_no, ARRAY_SIZE(key_no));
        else if (!strcmp(name, "fr"))
                key_patch_apply(key_fr, ARRAY_SIZE(key_fr));
        else if (!strcmp(name, "es"))
                key_patch_apply(key_es, ARRAY_SIZE(key_es));
        else if (!strcmp(name, "it"))
                key_patch_apply(key_it, ARRAY_SIZE(key_it));
        else
        {
                key_load_us();
                strscpy(canvas_layout_held, "us", sizeof(canvas_layout_held));
                return -EINVAL;
        }
        strscpy(canvas_layout_held, name, sizeof(canvas_layout_held));
        return 0;
}

// The input handler owns each device's held bits and the list to combine.
static unsigned int *keyboard_held(struct input_handle *handle);
static unsigned int keyboard_modifiers(void);

static unsigned int key_character(unsigned int code, unsigned int modifiers)
{
        unsigned int level = 0;
        unsigned int c;
        const unsigned short (*map)[KEY_LEVELS] = key_ready ? key_live : key_us;

        if (code >= KEY_TABLE)
                return 0;
        if (modifiers & WINDOW_KEY_ALTGR)
                level += 2;
        if (modifiers & WINDOW_KEY_SHIFT)
                level += 1;
        c = map[code][level];
        if (!c && level)
                c = map[code][level & 2];
        if (!c && (modifiers & WINDOW_KEY_SHIFT))
                c = map[code][1];
        if (!c)
                c = map[code][0];
        if (!c)
                return 0;

        // Control turns a letter into the control code that letter names,
        // which is the whole of why a terminal wants a modifier at all.
        if ((modifiers & WINDOW_KEY_CONTROL) &&
            (unsigned int)((c | 0x20) - 'a') < 26)
                return c & 0x1f;

        return c;
}

/*
        Whether a key is somebody typing.

        A modifier on its own is not. It has no character and no sequence, so
        term_key_modified emits nothing for it and nothing reaches the program
        -- and it is what one hand holds while the other works the wheel or
        the bar, which must not be read as a reason to leave where they were
        reading.
*/
static PURE _Bool key_typed(unsigned int code, unsigned int flags)
{
        if (!(flags & WINDOW_KEY_DOWN))
                return false;

        return code >= KEY_TABLE || !key_mod[code];
}

/*
        Called by the input core, so no lock and no sleeping. Which window has
        focus is decided under desktop.lock and the window can be freed, so
        this records what happened and the thread hands it over.
*/
static void keyboard_event(struct input_handle *handle, unsigned int code, int value)
{
        unsigned int modifiers;
        unsigned int bit;
        unsigned int head, tail;
        struct window_key key;

        if (bind_key_swallowed(code, value))
                return;

        modifiers = (unsigned int)atomic_read(&desktop.modifiers);
        bit = code < KEY_TABLE ? key_mod[code] : 0;

        if (bit)
        {
                unsigned int *held = keyboard_held(handle);
                *held = value ? *held | bit : *held & ~bit;
                modifiers = keyboard_modifiers();
                atomic_set(&desktop.modifiers, (int)modifiers);

                /*
                        Alt belongs to compositor chords.

                        Sending Alt-down to one client, changing focus on Tab,
                        then sending Alt-up to another leaves both clients with
                        a modifier state that never happened. Keep Alt in the
                        flags of ordinary keys, but consume its own events.
                */
                if (bit & (WINDOW_KEY_ALT | (WINDOW_KEY_ALT << 4) |
                           KEY_HELD_ALTGR))
                {
                        if (!(modifiers & WINDOW_KEY_ALT) &&
                            atomic_xchg(&desktop.focus_cycling, 0))
                        {
                                atomic_set(&desktop.focus_commit, 1);
                                canvas_thread_wake();
                        }

                        return;
                }
        }

        /*
                The conservative chord is unmodified Alt-Tab. Shift-Alt-Tab
                is left with the client until modifier buffering exists; that
                is safer than moving focus after Shift-down was already sent
                to the old client.
        */
        if ((modifiers & (WINDOW_KEY_ALT | WINDOW_KEY_SHIFT |
                          WINDOW_KEY_CONTROL)) == WINDOW_KEY_ALT &&
            code == KEY_TAB)
        {
                if (value)
                {
                        // lib.c has an address-first atomic_inc of its
                        // own, so use the kernel spelling that cannot collide.
                        atomic_fetch_add(1, &desktop.focus_steps);
                        atomic_set(&desktop.focus_cycling, 1);
                        canvas_thread_wake();
                }

                return;
        }

        // Alt-F9 is the compositor-owned minimize affordance. Its release is
        // consumed as part of the same chord.
        if ((modifiers & (WINDOW_KEY_ALT | WINDOW_KEY_SHIFT |
                          WINDOW_KEY_CONTROL)) == WINDOW_KEY_ALT &&
            code == KEY_F9 && !atomic_read(&desktop.focus_cycling))
        {
                if (value)
                {
                        atomic_set(&desktop.minimize, 1);
                        canvas_thread_wake();
                }

                return;
        }

        /*
                Control-Shift-T is a new terminal.

                Shift, and not Control-T on its own, because Control-T is
                readline's transpose-characters and the shell in every window
                would lose it. Nothing is lost by taking this one: key_character
                folds Control over a letter before it looks at Shift, so
                Control-Shift-T and Control-T are the same byte 0x14 to a
                program, and a chord no client could tell apart from another is
                a chord no client can miss.

                A press and not a repeat, so a key held down does not fill the
                desktop with shells; the release is consumed with it, or the
                client sees half a chord.
        */
        if ((modifiers & (WINDOW_KEY_ALT | WINDOW_KEY_SHIFT |
                          WINDOW_KEY_CONTROL)) ==
                (WINDOW_KEY_SHIFT | WINDOW_KEY_CONTROL) &&
            code == KEY_T)
        {
                if (value == 1)
                {
                        atomic_set(&desktop.spawn, 1);
                        canvas_thread_wake();
                }

                return;
        }

        // Once an Alt-Tab traversal has started, do not leak another
        // Alt-modified key into the selected-but-not-yet-raised client.
        if ((modifiers & WINDOW_KEY_ALT) &&
            atomic_read(&desktop.focus_cycling))
                return;

        key.code = code;
        key.character = value ? key_character(code, modifiers) : 0;
        // Autorepeat arrives as 2, and a terminal wants it like a press.
        key.flags = (value ? WINDOW_KEY_DOWN : 0) | modifiers;
        key.reserved = 0;

        head = (unsigned int)atomic_read(&desktop.key_head);
        tail = (unsigned int)atomic_read(&desktop.key_tail);

        /*
                One writer each: this moves head, the thread moves tail. A full
                ring therefore loses the newest, because losing the oldest
                would mean moving tail from here as well, and two writers on
                one index is a ring that delivers a key twice or reads a slot
                while it is being overwritten.
        */
        if (head - tail >= WINDOW_KEYS)
                return;

        desktop.key_ring[head % WINDOW_KEYS] = key;
        smp_wmb();
        atomic_set(&desktop.key_head, (int)(head + 1));

        canvas_thread_wake();
}

// Under desktop.lock, so the focused window is safe to reach.
static void keys_deliver(void)
{
        struct pane *pane = desktop.focused;
        unsigned int head, tail, at;

        head = (unsigned int)atomic_read(&desktop.key_head);
        tail = (unsigned int)atomic_read(&desktop.key_tail);

        if (head == tail)
                return;

        // Owned panes have no program. Off can leave the kernel log
        // focused, and a click can put it there; either way the keys
        // belong in a window that can take them, not in the ring of
        // nothing. Create still does not focus: this is only when a
        // key has already arrived and nothing shared holds it.
        if (!pane || !pane->shared)
        {
                pane = pane_topmost(NULL, false, INT_MAX);
                if (pane)
                {
                        pane_focus(pane);
                        desktop.damage_all = true;
                }
        }

        if (!pane || !pane->shared)
        {
                atomic_set(&desktop.key_tail, (int)head);
                return;
        }

        smp_rmb();

        /*
                Typing puts the window back at the end.

                What was typed lands at the bottom, and a view left where the
                hand put it shows none of it -- nor anything the program says
                about it, because nothing scrolled back is drawn at all. So a
                window read back through and then typed into looked like one
                that had stopped listening, when every key had in fact arrived.

                The window the keys land in, which is why this is below the
                return above rather than beside it: the kernel log has no
                program to type at and drops them, and reading back through a
                boot must not end at the first key pressed at it. Before the
                handover rather than after, so the frame it asks for is the
                one the program's answer is drawn in.
        */
        for (at = tail; at != head; at++)
        {
                const struct window_key *key = &desktop.key_ring[at % WINDOW_KEYS];

                if (!key_typed(key->code, key->flags))
                        continue;

                WRITE_ONCE(pane->keyed, true);

                // Focus rather than what the pointer is over, which is where
                // the wheel goes: reading one window while typing into
                // another leaves the one being read where it was.
                if (pane_view_live(pane))
                        atomic_set(&desktop.frame_pending, 1);

                break;
        }

        /*
                Every one is consumed here whether or not it lands.

                Nothing obliges a program to read its keys, and holding the
                desktop's ring until it does makes one window able to stop the
                keyboard for every other -- and to spin the thread, which sleeps
                only while the ring is empty. A window that will not listen
                loses what was said to it, which is what a keyboard buffer has
                always done.
        */
        for (; tail != head; tail++)
        {
                struct window *shared = pane->shared;
                unsigned int at = READ_ONCE(shared->key_head);

                // Tail belongs to the program. Moving it from here to make
                // room would put two writers on it, and window_key is the
                // other one.
                if (at - READ_ONCE(shared->key_tail) < WINDOW_KEYS)
                {
                        shared->keys[at % WINDOW_KEYS] = desktop.key_ring[tail % WINDOW_KEYS];
                        smp_wmb();
                        WRITE_ONCE(shared->key_head, at + 1);
                }
        }

        atomic_set(&desktop.key_tail, (int)head);

        // The program may be asleep on its file; this is what it was waiting
        // for, and the wake is the whole of the latency from here on.
        wake_up_interruptible(&pane->wait);
}

/* ---- client: a client, and what it is allowed to ask for ---- */

/*
        Canvas -- attaching to DRM

        Opening the node runs the driver's open path and hands back a
        drm_file, which knows its minor, which knows its device. The file is
        closed immediately; drm_client_init takes its own reference.

        Every card is taken, not just the first. The poll is here because the
        nodes appear when devtmpfs is mounted, after the initcalls that could
        otherwise have started this.
*/

/*
        The outputs come off the desktop before the card they belong to is
        released, or output->canvas dangles for anything still composing.
        canvas_thread_stop joins a thread that takes desktop.lock, so it runs under
        canvas_list_lock and never under desktop.lock. The plug work is cancelled
        first, so it cannot start or rebind a card that is leaving.
*/
static struct workqueue_struct *canvas_plug_wq;
static void canvas_claimed_forget(struct drm_device *dev);

static void canvas_plug_ensure(void)
{
        if (!canvas_plug_wq)
                canvas_plug_wq = alloc_ordered_workqueue("moonwater/plug", 0);
}

static void canvas_plug_work(struct work_struct *work)
{
        struct canvas *canvas = container_of(work, struct canvas, plug);
        int ret = 0;

        rt_mutex_lock(&desktop.lock);

        /*
                Not while the card sleeps.

                A driver reports a connector as it comes back from suspend
                (amdgpu's resume ends with a hotplug event, and the DRM core
                hands it to a client only when that client's resume returns),
                and a rebind commits. The commit belongs after the wake has
                redrawn, not in the middle of the driver's own resume.
        */
        if (canvas->started && READ_ONCE(desktop.asleep))
        {
                canvas->replug = true;
                rt_mutex_unlock(&desktop.lock);
                return;
        }

        if (!canvas->started)
        {
                ret = canvas_start(canvas);
                canvas->started = (ret == 0);
        }
        else
                ret = canvas_rebind(canvas);

        rt_mutex_unlock(&desktop.lock);
        (void)ret;
}

static COLD void client_unregister(struct drm_client_dev *client)
{
        struct canvas *canvas = canvas_from_client(client);

        canvas_claimed_forget(client->dev);

        mutex_lock(&canvas_list_lock);
        list_del(&canvas->link);
        cancel_work_sync(&canvas->plug);
        if (list_empty(&canvas_list))
                canvas_thread_stop();
        mutex_unlock(&canvas_list_lock);

        rt_mutex_lock(&desktop.lock);
        canvas->started = 0;
        canvas_release(canvas);
        rt_mutex_unlock(&desktop.lock);

        // An output dropped while its buffer was with the flusher is freed by
        // the flusher, and that buffer is this client's.
        wait_event(desktop.flush_idle, !atomic_read(&canvas->retiring));

        drm_client_release(client);
}

static COLD void client_free(struct drm_client_dev *client)
{
        kfree(canvas_from_client(client));
}

static int client_hotplug(struct drm_client_dev *client)
{
        struct canvas *canvas = canvas_from_client(client);

        /*
                Never start or rebind on the caller's workqueue.

                drm_client_register calls this itself, which is fine, but a
                later connector hotplug runs on the DRM helper workqueue. A
                modeset commit from there disables every cursor plane and can
                wait on that same queue: on i915 that is a frozen low-res
                kernel log and no pointer. Queue on moonwater/plug, which
                nothing in DRM flushes.
        */
        canvas_plug_ensure();
        queue_work(canvas_plug_wq ? canvas_plug_wq : system_unbound_wq,
                   &canvas->plug);
        return 0;
}

// The bool argument is whether the restore happens from an atomic context.
static int client_restore(struct drm_client_dev *client, _Bool in_atomic)
{
        struct canvas *canvas = canvas_from_client(client);

        if (in_atomic)
                return -EBUSY;

        rt_mutex_lock(&desktop.lock);
        // The wake's own redraw is the restore; a commit now is the one into
        // a card whose power is going or has not come back.
        if (canvas->started && !READ_ONCE(desktop.asleep))
        {
                // The last program that held the card has closed it.
                if (desktop.suspended)
                        desktop_resume();
                else
                        desktop_redraw();
        }
        rt_mutex_unlock(&desktop.lock);

        return 0;
}

static const struct drm_client_funcs client_funcs = {
    .owner = THIS_MODULE,
    .unregister = client_unregister,
    .free = client_free,
    .restore = client_restore,
    .hotplug = client_hotplug,
};

static _Bool canvas_holds(struct drm_device *dev)
{
        struct canvas *canvas;
        _Bool held = false;

        mutex_lock(&canvas_list_lock);
        list_for_each_entry(canvas, &canvas_list, link)
        {
                if (canvas->client.dev == dev)
                {
                        held = true;
                        break;
                }
        }
        mutex_unlock(&canvas_list_lock);

        return held;
}

/*
        Everything but the registering, which is deliberately not done here:
        registering fires the hotplug that chooses a mode and commits it, and
        that has to happen with no file open on the card. See canvas_claim.
*/
static struct canvas *canvas_take_over(struct drm_device *dev)
{
        struct canvas *canvas;
        _Bool first;

        if (!drm_core_check_feature(dev, DRIVER_MODESET) || canvas_holds(dev))
                return NULL;

        // The face the whole machine draws with. The kernel already carries
        // it for its own console.
        if (!canvas_font)
        {
                canvas_font = find_font("VGA8x16");

                if (!canvas_font)
                {
                        pr_info("[moonwater canvas] " "no console font to draw with\n");
                        return NULL;
                }

                canvas_terminal_prepare();

        }

        canvas = kzalloc(sizeof(*canvas), GFP_KERNEL);
        if (!canvas)
                return NULL;

        canvas->set_result = 1;

        if (drm_client_init(dev, &canvas->client, "moonwater", &client_funcs))
        {
                kfree(canvas);
                return NULL;
        }

        canvas_plug_ensure();
        INIT_WORK(&canvas->plug, canvas_plug_work);

        mutex_lock(&canvas_list_lock);
        first = list_empty(&canvas_list);
        list_add_tail(&canvas->link, &canvas_list);
        if (first)
                canvas_thread_start();
        mutex_unlock(&canvas_list_lock);

        return canvas;
}

// Retried fast: the node appears the moment devtmpfs is mounted, and every
// millisecond spent waiting after that is a millisecond of black screen.
#define CANVAS_RETRY_MS 5
#define CANVAS_ATTEMPTS 1000

// Rounds to keep looking after the first card, so a sibling that probes late
// is found too.
//
// A hundred rounds at CANVAS_RETRY_MS is half a second of polling after the
// screen is already up, which reads like an obvious thing to cut. It is not:
// measured at 10 rounds against 100, moonwater starts, the canvas is drawn
// and the shell runs at the same times to within the noise of five boots
// each. The poll is a delayed work item that sleeps between rounds, so what
// it costs the boot is not the half second it spans. Cutting it would buy
// nothing and lose the late sibling it is here for.
#define CANVAS_SETTLE 100

/*
        What turning Canvas on needs from a card before it is claimed.
*/
#include <drm/drm_auth.h>
#include <drm/drm_file.h>

/* Every in-kernel client on a card but Canvas's, as drm_client_dev_unregister takes them. */
static void canvas_clients_clear(struct drm_device *dev)
{
        struct drm_client_dev *client, *next;

        mutex_lock(&dev->clientlist_mutex);
        list_for_each_entry_safe(client, next, &dev->clientlist, list)
        {
                if (client->funcs == &client_funcs)
                        continue;

                // Unregistering consumes and frees the client.
                list_del(&client->list);
                if (client->funcs && client->funcs->unregister)
                        client->funcs->unregister(client);
                else
                        drm_client_release(client);
        }
        mutex_unlock(&dev->clientlist_mutex);
}

/* Which program is master of a card, for a refusal to say. */
static int canvas_master_holder(struct drm_device *dev, char *command, size_t room)
{
        struct drm_file *file;
        int holder = 0;

        mutex_lock(&dev->filelist_mutex);
        list_for_each_entry(file, &dev->filelist, lhead)
        {
                struct task_struct *task;

                if (!drm_is_current_master(file))
                        continue;

                rcu_read_lock();
                task = pid_task(rcu_dereference(file->pid), PIDTYPE_TGID);
                if (task)
                {
                        // In the asker's pid namespace, where a number the
                        // asker can kill means something; 0 is a holder it
                        // cannot see. A program names itself with prctl, so
                        // only printable bytes go to the asker's terminal.
                        holder = task_tgid_nr_ns(task, task_active_pid_ns(current));
                        strscpy(command, task->comm, room);
                        for (char *at = command; *at; at++)
                                if (*at < ' ' || *at > '~')
                                        *at = '?';
                }
                rcu_read_unlock();
                break;
        }
        mutex_unlock(&dev->filelist_mutex);

        return holder;
}

static struct delayed_work canvas_probe_work;
static unsigned int canvas_attempts;
static unsigned int canvas_settled_at;

/*
        Which primary nodes are already ours.

        Not an optimisation. Opening a node we already hold and closing it
        again is a client releasing the device as far as DRM is concerned, and
        it answers by telling every client to restore -- a full repaint of
        every screen. The poll below runs for a hundred rounds, so booting
        cost a hundred and one full composes, about two hundred milliseconds
        of drawing nobody asked for.

        One bit per minor, which is the whole of DRM's minor space.
*/
static u64 canvas_claimed;
static _Bool canvas_is_on(void);
static void canvas_firmware_yield(void);

static void canvas_claimed_forget(struct drm_device *dev)
{
        int minor;

        if (!dev || !dev->primary)
                return;

        minor = dev->primary->index;
        if (minor >= 0 && minor < 64)
                canvas_claimed &= ~BIT_ULL(minor);
}

static _Bool canvas_has_native(void)
{
        struct canvas *canvas;
        _Bool native = false;

        mutex_lock(&canvas_list_lock);
        list_for_each_entry(canvas, &canvas_list, link)
                if (!canvas_is_firmware(canvas->client.dev))
                {
                        native = true;
                        break;
                }
        mutex_unlock(&canvas_list_lock);

        return native;
}

/*
        Close a node this code opened, and have it gone before returning.

        A plain close from a program's ioctl puts the last fput on the task's
        work list, which runs as the call returns to the program, and on a
        kernel thread on a worker a jiffy later. Until it runs, the file is
        still open on the card, and the first program to open a card is its
        master. So a refused pass held the card against the next pass of the
        same call: with a firmware card as the only one, the first pass opens
        and skips it, the second opens it again, finds the first still master
        and the refusal names the caller itself -- a shell that is gone by the
        time anyone looks, and a different one on every retry. And the first
        commit of a card taken from a kernel thread lost the same race, so
        the desktop began suspended. This is the only reference, so the last
        put is done here.
*/
static void canvas_node_close(struct file *filp)
{
        __fput_sync(filp);
}

static int canvas_claim(const char *path, unsigned int minor,
                        struct canvas_control *on, _Bool firmware_ok)
{
        struct file *filp;
        struct drm_file *file_priv;
        struct canvas *canvas;
        struct drm_device *dev;

        if (canvas_claimed & BIT_ULL(minor))
                return -EBUSY;

        filp = filp_open(path, O_RDWR, 0);
        if (IS_ERR(filp))
                return PTR_ERR(filp);

        file_priv = filp->private_data;

        if (!file_priv || !file_priv->minor || !file_priv->minor->dev)
        {
                canvas_node_close(filp);
                return -ENODEV;
        }

        /*
                simpledrm is card0 on a Dell with i915: GOP's leftover
                size, often 1024x768. Starting there paints the kernel
                log, then i915 takes the same pipe and the machine
                freezes. Leave it until a GPU appears, or until the
                settle rounds have passed with none. Userspace on still
                takes it when it is the only card -- the first pass of
                claim_all skips firmware so card0 cannot start before
                card1 is tried.
        */
        dev = file_priv->minor->dev;
        if (canvas_is_firmware(dev))
        {
                int skip = 0;

                if (!firmware_ok)
                        skip = -EAGAIN;
                else if (canvas_has_native())
                        skip = -ENODEV;
                else if (!on && canvas_attempts < CANVAS_SETTLE)
                        skip = -EAGAIN;

                if (skip)
                {
                        canvas_node_close(filp);
                        return skip;
                }
        }

        /*
                Turned on from userspace, a card is taken only from nobody.

                This open is the card's master unless another program already
                is, and a Canvas started behind that program would only sit
                suspended, so the refusal names it instead. The kernel
                console's client, which off left on the card, goes while this
                file is still the master, so the close below restores nothing.
        */
        if (on && drm_core_check_feature(dev, DRIVER_MODESET))
        {
                if (!drm_is_current_master(file_priv))
                {
                        if (!on->master_pid && !on->master_command[0])
                                on->master_pid = canvas_master_holder(
                                    dev, on->master_command,
                                    sizeof(on->master_command));
                        canvas_node_close(filp);
                        return -EACCES;
                }

                canvas_clients_clear(dev);
        }

        canvas = canvas_take_over(dev);

        if (canvas)
                canvas_claimed |= BIT_ULL(minor);

        /*
                Closed before the client is registered, and drm_client_init has
                taken its own reference to the device by now.

                Registering fires the hotplug that picks a mode and commits it,
                and a commit is refused out of hand while anything else is the
                device's master -- which this file is until it is closed. With
                it still open the first commit always answered EBUSY, so
                "that mode would not set, take the offered one" could never
                run: the one answer it was written to read was the one answer
                it could never get.
        */
        canvas_node_close(filp);

        if (!canvas)
                return -EBUSY;

        drm_client_register(&canvas->client);
        pr_info("[moonwater canvas] " "attached to %s, %s display\n", canvas->client.dev->driver->name, canvas_is_virtual(canvas->client.dev) ? "a guest's" : "a real");

        if (!canvas_is_firmware(canvas->client.dev))
                canvas_firmware_yield();

        return 0;
}

/*
        Every primary node, every round. DRM allocates card minors out of an
        idr with no promise they are contiguous, and a card that probes late
        would be missed by a scan that stopped at the first gap. The whole
        minor space is cheap to try: an absent node fails in filp_open.
*/
static unsigned int canvas_claim_range(struct canvas_control *on,
                                       unsigned int *refused,
                                       _Bool firmware_ok)
{
        char path[24];
        unsigned int minor, taken = 0;

        for (minor = 0; minor < 64; minor++)
        {
                if (canvas_claimed & BIT_ULL(minor))
                        continue;

                positive_into_string(
                    memory_copy_apart_end(path, "/dev/dri/card",
                                         sizeof("/dev/dri/card") - 1),
                    minor);

                switch (canvas_claim(path, minor, on, firmware_ok))
                {
                case 0:
                        taken++;
                        break;
                case -EACCES:
                        if (refused)
                                (*refused)++;
                        break;
                }
        }

        return taken;
}

static unsigned int canvas_claim_all(struct canvas_control *on,
                                     unsigned int *refused)
{
        unsigned int taken = canvas_claim_range(on, refused, false);

        /* Firmware last: card0 is often simpledrm, and starting it
           before i915 is the freeze. If nothing else attached, take it. */
        if (!taken)
                taken = canvas_claim_range(on, refused, true);

        return taken;
}

static COLD void canvas_probe(struct work_struct *work)
{
        _Bool was_on = canvas_is_on();

        canvas_claim_all(NULL, NULL);
        if (!was_on && canvas_is_on())
                bind_fire(SPARK_BIND_CANVAS_ON);
        canvas_attempts++;

        if (!canvas_settled_at && !list_empty(&canvas_list))
                canvas_settled_at = canvas_attempts + CANVAS_SETTLE;

        if (canvas_settled_at && canvas_attempts >= canvas_settled_at)
                return;

        if (canvas_attempts >= CANVAS_ATTEMPTS)
        {
                pr_info("[moonwater canvas] " "gave up waiting for a card\n");
                return;
        }

        schedule_delayed_work(&canvas_probe_work, msecs_to_jiffies(CANVAS_RETRY_MS));
}

static void __maybe_unused canvas_start_probing(void)
{
        INIT_DELAYED_WORK(&canvas_probe_work, canvas_probe);
        schedule_delayed_work(&canvas_probe_work, 0);
}

/*
        Canvas, off and on, from userspace.

        Off undoes what attaching did, in this order: the input handler and
        the thread go, which gives every console its keyboard back; every
        program's window is asked to close and taken off the desktop; each
        card's client is released; and the kernel's own framebuffer console
        is set up on each card, which drm_client_lib.active= kept from ever
        having one. The kernel log window stays: its cells are the cache of
        the boot, and printk keeps writing them while the cards are away.
        On claims the cards again the way the boot does and fires canvas on,
        and the machine script opens whatever windows it wants again.

        Lock order: canvas_control_lock, then a card's clientlist_mutex, then
        canvas_list_lock, then desktop.lock, then a card's master_mutex.
        canvas_thread_stop joins a thread that takes desktop.lock, so it is
        called under canvas_list_lock and never under desktop.lock.
*/
#ifndef MODULE
#include <../drivers/gpu/drm/clients/drm_client_internal.h>
#endif

static DEFINE_MUTEX(canvas_control_lock);

static _Bool canvas_is_on(void)
{
        _Bool on;

        mutex_lock(&canvas_list_lock);
        on = !list_empty(&canvas_list);
        mutex_unlock(&canvas_list_lock);

        return on;
}

/* The desktop's own pointers into a pane, as pane_free clears them. */
static void pane_forget(struct pane *pane)
{
        struct pane **held[] = {
                &desktop.dragging, &desktop.resizing, &desktop.barring,
                &desktop.press_pane, &desktop.focused,
        };

        for (unsigned int i = 0; i < ARRAY_SIZE(held); i++)
                if (*held[i] == pane)
                        *held[i] = NULL;
}

/*
        Every program's window asked to close and taken off the desktop.

        Nothing is freed: a pane goes when its program closes the file, and
        a program still writing to its pages keeps them. One that never
        closes stays on the detached list, drawn by nothing. Under
        desktop.lock.
*/
static void desktop_detach_windows(void)
{
        struct pane *pane, *next;

        list_for_each_entry_safe(pane, next, &desktop.windows, link)
        {
                if (!pane->shared)
                        continue;

                pane_close_request(pane);
                pane_forget(pane);
                list_move_tail(&pane->link, &desktop.detached);
        }
}

/*
        One card's client off DRM's list and released, unless the card is
        going away at this moment and its own unregister already has it.
*/
static void canvas_client_drop(struct drm_device *dev)
{
        struct drm_client_dev *client, *next;

        mutex_lock(&dev->clientlist_mutex);
        list_for_each_entry_safe(client, next, &dev->clientlist, list)
        {
                if (client->funcs != &client_funcs)
                        continue;
                list_del(&client->list);
                client_unregister(client);
        }
        mutex_unlock(&dev->clientlist_mutex);
}

/*
        A GPU is attached: the firmware framebuffer is not a second
        screen, it is the same pipe. Leaving a client on it is what
        froze the OptiPlex -- i915 taking that pipe with a picture
        still scanning from simpledrm.
*/
static void canvas_firmware_yield(void)
{
        for (;;)
        {
                struct canvas *canvas;
                struct drm_device *dev = NULL;

                mutex_lock(&canvas_list_lock);
                list_for_each_entry(canvas, &canvas_list, link)
                        if (canvas_is_firmware(canvas->client.dev))
                        {
                                /* The device only: DRM can unregister this
                                   client between the unlock and the drop. */
                                dev = canvas->client.dev;
                                drm_dev_get(dev);
                                break;
                        }
                mutex_unlock(&canvas_list_lock);

                if (!dev)
                        return;

                pr_info("[moonwater canvas] " "dropping %s, a GPU is attached\n",
                        dev->driver->name);
                canvas_client_drop(dev);
                drm_dev_put(dev);
        }
}

static long canvas_turn_off(void)
{
        struct drm_device *released[8];
        unsigned int count = 0;

        mutex_lock(&canvas_control_lock);

        if (!canvas_is_on())
        {
                mutex_unlock(&canvas_control_lock);
                return -EALREADY;
        }

#ifdef CONFIG_MOONWATER_CANVAS_AUTOSTART
        cancel_delayed_work_sync(&canvas_probe_work);
#endif

        // Input and the thread before any window or output goes, so no key
        // lands in a pane being detached and nothing composes against an
        // output being released. The kernel log window stays registered:
        // printk keeps filling the same cells, and on will draw them again.
        mutex_lock(&canvas_list_lock);
        atomic_set(&desktop.spawn, 0);
        canvas_thread_stop();
        mutex_unlock(&canvas_list_lock);

        rt_mutex_lock(&desktop.lock);
        desktop.off = true;
        desktop_detach_windows();
        // The kernel log stays; a click can have focused it. Left
        // that way, on comes back painted with every key dropped.
        if (desktop.focused && !desktop.focused->shared)
                pane_focus(NULL);
        desktop.suspended = false;
        rt_mutex_unlock(&desktop.lock);

        put_pid(xchg(&canvas_spawned, NULL));

        for (;;)
        {
                struct canvas *canvas;
                struct drm_device *dev = NULL;

                mutex_lock(&canvas_list_lock);
                canvas = list_first_entry_or_null(&canvas_list, struct canvas, link);
                if (canvas)
                {
                        dev = canvas->client.dev;
                        drm_dev_get(dev);
                }
                mutex_unlock(&canvas_list_lock);

                if (!canvas)
                        break;

                canvas_client_drop(dev);

                if (count < ARRAY_SIZE(released))
                        released[count++] = dev;
                else
                        drm_dev_put(dev);
        }

        canvas_claimed = 0;

        // The kernel's console takes each screen back.
        for (unsigned int i = 0; i < count; i++)
        {
#ifndef MODULE
                if (released[i]->registered && !released[i]->fb_helper)
                        drm_fbdev_client_setup(released[i], NULL);
#endif
                drm_dev_put(released[i]);
        }

        pr_info("[moonwater canvas] " "off: %u card(s) given back to the console\n", count);

        mutex_unlock(&canvas_control_lock);
        bind_fire(SPARK_BIND_CANVAS_OFF);
        return 0;
}

static long canvas_turn_on(struct canvas_control *answer)
{
        unsigned int taken, refused = 0;

        mutex_lock(&canvas_control_lock);

        if (canvas_is_on())
        {
                mutex_unlock(&canvas_control_lock);
                return -EALREADY;
        }

#ifdef CONFIG_MOONWATER_CANVAS_AUTOSTART
        cancel_delayed_work_sync(&canvas_probe_work);
#endif

        rt_mutex_lock(&desktop.lock);
        desktop.off = false;
        rt_mutex_unlock(&desktop.lock);

        taken = canvas_claim_all(answer, &refused);

        mutex_unlock(&canvas_control_lock);

        if (taken)
        {
                (void)canvas_layout_name();
                bind_fire(SPARK_BIND_CANVAS_ON);
                return 0;
        }

        return refused ? -EBUSY : -ENODEV;
}

/* What Canvas holds, for `moonwater canvas`. */
static void canvas_state(struct canvas_control *answer)
{
        struct canvas *canvas;
        struct output *output;
        struct pane *pane;

        mutex_lock(&canvas_list_lock);
        list_for_each_entry(canvas, &canvas_list, link)
                if (!answer->cards++)
                        strscpy(answer->driver, canvas->client.dev->driver->name,
                                sizeof(answer->driver));
        mutex_unlock(&canvas_list_lock);

        answer->running = answer->cards != 0;

        rt_mutex_lock(&desktop.lock);

        answer->suspended = desktop.suspended;
        answer->latency_hold = READ_ONCE(hold_on);
        answer->latency_holds = READ_ONCE(hold_takes);
        answer->thread_passes = READ_ONCE(canvas_passes);
        answer->frame_ticks = READ_ONCE(canvas_ticks);

        list_for_each_entry(pane, &desktop.windows, link)
                if (pane->shared)
                        answer->windows++;

        list_for_each_entry(pane, &desktop.detached, link)
                answer->detached++;

        list_for_each_entry(output, &desktop.outputs, link)
        {
                struct canvas_output_state *state;
                struct drm_mode_set *set = output->mode_set;

                if (answer->output_count == SPARK_CANVAS_OUTPUTS)
                        break;

                state = &answer->output[answer->output_count++];
                state->width = output->width;
                state->height = output->height;

                if (set && set->mode)
                        state->refresh = drm_mode_vrefresh(set->mode);
                if (set && set->num_connectors && set->connectors[0])
                        strscpy(state->connector, set->connectors[0]->name,
                                sizeof(state->connector));
        }

        rt_mutex_unlock(&desktop.lock);
}

/*
        The kernel log window and a terminal, when userspace asks.

        Canvas on can be answered before the card's first picture: the claim
        queues the start on moonwater/plug. A request that arrives in that
        gap is kept, and canvas_start opens it once the desktop has a size.
        The log is one window, so asking with it open leaves it; each
        terminal request is a new terminal, started on the canvas thread as
        Control-Shift-T starts one.
*/
static long canvas_log_open(void)
{
        mutex_lock(&canvas_control_lock);

        if (!canvas_is_on())
        {
                mutex_unlock(&canvas_control_lock);
                return -ENODEV;
        }

        rt_mutex_lock(&desktop.lock);
        desktop.log_wanted = true;
        if (desktop.started)
        {
                console_start();
                desktop_recompose();
        }
        rt_mutex_unlock(&desktop.lock);

        mutex_unlock(&canvas_control_lock);
        return 0;
}

static long canvas_terminal_open(void)
{
        _Bool started;

        mutex_lock(&canvas_control_lock);

        if (!canvas_is_on())
        {
                mutex_unlock(&canvas_control_lock);
                return -ENODEV;
        }

        rt_mutex_lock(&desktop.lock);
        started = desktop.started;
        if (!started)
                desktop.terminal_wanted = true;
        rt_mutex_unlock(&desktop.lock);

        mutex_unlock(&canvas_control_lock);

        if (started)
                canvas_terminal_spawn();
        return 0;
}

/* ---- pointer: the pointer ---- */

/*
        Canvas -- input

        This is the reason for putting the compositor in the kernel at all. A
        userspace display server sees a mouse move as: interrupt, input core,
        wake the server, the server reads the event, composites, and asks the
        kernel to move the cursor. Every one of those arrows is a context
        switch.

        The handler below is called by the input core directly, in the same
        path that received the event. There is one handoff left and it is
        unavoidable: input events arrive in atomic context and a DRM commit
        can sleep, so the position is taken immediately and a SCHED_FIFO
        thread applies it.
*/

static struct task_struct __rcu *canvas_thread;
static struct task_struct __rcu *canvas_flusher;
static _Bool pointer_handler_registered;

/*
        How long the processor is allowed to take waking up.

        The largest cost from a pointer moving to the thread that draws it is
        the processor coming back from an idle state, and the deeper the state
        the longer that takes. Held while there is input rather than taken per
        movement, and let go of CANVAS_HOLD_MS after the last of it. A request
        of zero microseconds keeps every processor out of every idle state,
        which is a processor polling: to cpufreq it is busy, so with the
        hardware P-states in control (amd-pstate active) the clock sat at its
        top for as long as Canvas existed, on a desktop nobody was touching.
        The move that pays the wakeup is the first after a pause, and it pays
        it either way: the processor was in its idle state when the interrupt
        came.

        Owned by the canvas thread alone, which is why it may add and remove
        the request: neither belongs in an input callback, and the thread is
        already woken for every event that matters. What counts is what a
        person did (a movement, a button, a wheel step, a key, a focus
        change), not a program's commit, so a clock ticking in a window does
        not keep it. The thread's own sleep is cut short to let go on time.
*/
#define CANVAS_HOLD_MS 500
static struct pm_qos_request pointer_qos;
static u64 hold_until;

static void canvas_hold_touch(void)
{
        WRITE_ONCE(hold_until, ktime_get_ns() + (u64)CANVAS_HOLD_MS * NSEC_PER_MSEC);
        if (hold_on)
                return;
        cpu_latency_qos_add_request(&pointer_qos, 0);
        WRITE_ONCE(hold_on, true);
        WRITE_ONCE(hold_takes, hold_takes + 1);
}

static void canvas_hold_drop(void)
{
        if (!hold_on)
                return;
        cpu_latency_qos_remove_request(&pointer_qos);
        WRITE_ONCE(hold_on, false);
}

// Jiffies until the hold is due to go, or the given sleep when nothing is held.
static long canvas_hold_sleep(long sleep)
{
        s64 left;

        if (!hold_on)
                return sleep;
        left = (s64)(hold_until - ktime_get_ns());
        if (left <= 0)
                return 1;
        return min_t(long, sleep, nsecs_to_jiffies(left) + 1);
}

/*
        Acceleration, the way a desktop does it.

        A mouse reports counts, not pixels, and the same count means different
        things depending on how fast the hand is moving: slow is aiming and
        fast is crossing the screen. So the gain is a curve on speed rather
        than a constant. Below the floor it is exactly one to one, which is
        what makes careful movement land where it is aimed; above the ceiling
        it is flat, because past that the hand is already travelling and more
        gain only makes it hard to stop.

        Fixed point, since there is no floating point in here. Speed is counts
        per millisecond.
*/
#define ACCEL_ONE 1024
#define ACCEL_FLOOR 1
#define ACCEL_CEILING 8
#define ACCEL_MAX (ACCEL_ONE * 5 / 2)

static CONST int pointer_gain(int speed)
{
        if (speed <= ACCEL_FLOOR)
                return ACCEL_ONE;

        if (speed >= ACCEL_CEILING)
                return ACCEL_MAX;

        return ACCEL_ONE + (ACCEL_MAX - ACCEL_ONE) * (speed - ACCEL_FLOOR) /
                               (ACCEL_CEILING - ACCEL_FLOOR);
}

static int accel_apply(int delta, int *remainder, int gain)
{
        s64 scaled;
        int whole;

        if (!delta)
                return 0;

        // The remainder is what stops a gain that is not a whole number from
        // dropping the fraction of every movement: a slow drag would come up
        // short of where it was aimed.
        scaled = (s64)delta * gain + *remainder;
        whole = (int)clamp_t(s64, scaled / ACCEL_ONE, INT_MIN, INT_MAX);
        *remainder = (int)(scaled % ACCEL_ONE);

        pointer_counts += delta < 0 ? -(s64)delta : delta;
        pointer_moved += whole < 0 ? -(s64)whole : whole;

        return whole;
}

/*
        Shake to find it.

        Reversing direction takes a real movement each time, so a slow wobble
        or a hand resting on the mouse is not a shake, and the reversals have
        to arrive inside one window or the count starts again.
*/
#define SHAKE_WINDOW_NS (700ULL * NSEC_PER_MSEC)
#define SHAKE_STEP 6
#define SHAKE_REVERSALS 5
#define CURSOR_MAGNIFIED 3
#define MAGNIFIED_NS (1200ULL * NSEC_PER_MSEC)

static void pointer_shake(int delta)
{
        int direction;
        u64 now;

        if (delta > -SHAKE_STEP && delta < SHAKE_STEP)
                return;

        direction = delta > 0 ? 1 : -1;
        now = ktime_get_ns();

        if (now - desktop.shake_window > SHAKE_WINDOW_NS)
        {
                desktop.shake_window = now;
                atomic_set(&desktop.shake_count, 0);
        }

        if (direction == atomic_read(&desktop.shake_dir))
                return;

        atomic_set(&desktop.shake_dir, direction);

        if (atomic_inc_return(&desktop.shake_count) >= SHAKE_REVERSALS)
        {
                atomic_set(&desktop.shake_count, 0);
                atomic_set(&desktop.magnify, 1);
        }
}

/*
        Keeps the cursor on a screen. The desktop is the bounding box of the
        outputs, so with screens of different heights it has corners no crtc
        scans out; a cursor left there would vanish.
*/
static void desktop_confine_cursor(int *x, int *y)
{
        struct output *output, *nearest = NULL;
        int best = INT_MAX;

        list_for_each_entry(output, &desktop.outputs, link)
        {
                int dx = clamp(*x, output->x, output->x + (int)output->width - 1) - *x;
                int dy = clamp(*y, output->y, output->y + (int)output->height - 1) - *y;
                int distance = abs(dx) + abs(dy);

                if (!dx && !dy)
                        return;

                if (distance < best)
                {
                        best = distance;
                        nearest = output;
                }
        }

        if (!nearest)
                return;

        *x = clamp(*x, nearest->x, nearest->x + (int)nearest->width - 1);
        *y = clamp(*y, nearest->y, nearest->y + (int)nearest->height - 1);
}

static void pointer_latency_record(u64 started)
{
        u64 elapsed = ktime_get_ns() - started;

        pointer_latency_total += elapsed;
        pointer_events++;

        if (elapsed > pointer_latency_worst)
                pointer_latency_worst = elapsed;
}

static void pointer_apply(void)
{
        _Bool button = atomic_xchg(&desktop.button_changed, 0);
        _Bool client = atomic_xchg(&desktop.client_changed, 0);
        _Bool motion = atomic_xchg(&desktop.motion_pending, 0);
        int x, y;
        u64 started;

        if (!button && !client && !motion)
                return;

        started = motion ? desktop.motion_stamp : 0;
        x = atomic_read(&desktop.pending_x);
        y = atomic_read(&desktop.pending_y);

        if (started)
                pointer_queue_total += ktime_get_ns() - started;

        rt_mutex_lock(&desktop.lock);

        if (!list_empty(&desktop.outputs))
        {
                if (button)
                {
                        if (atomic_read(&desktop.button_down))
                                drag_press(atomic_read(&desktop.button_x),
                                           atomic_read(&desktop.button_y));
                        else
                                drag_release(atomic_read(&desktop.button_x),
                                             atomic_read(&desktop.button_y));
                }

                if (client)
                {
                        struct pane *pane = desktop.focused;
                        unsigned int flags = atomic_read(&desktop.client_down)
                                                 ? WINDOW_KEY_DOWN
                                                 : 0;

                        if (pane)
                                pointer_report(pane,
                                               atomic_read(&desktop.button_x),
                                               atomic_read(&desktop.button_y),
                                               (unsigned int)atomic_read(
                                                   &desktop.client_button),
                                               flags);
                }

                if (motion)
                {
                        desktop_confine_cursor(&x, &y);
                        atomic_set(&desktop.pending_x, x);
                        atomic_set(&desktop.pending_y, y);

                        desktop.cursor_shape = cursor_shape_at(x, y);

                        if (atomic_xchg(&desktop.magnify, 0))
                        {
                                desktop.cursor_scale = CURSOR_MAGNIFIED * desktop.scale;
                                desktop.magnified_until = ktime_get_ns() + MAGNIFIED_NS;
                                desktop_watch();
                        }

                        if (desktop.dragging || desktop.resizing ||
                            desktop.barring)
                        {
                                if (desktop.dragging || desktop.resizing)
                                {
                                        /*
                                                A resize repaints the window,
                                                not the independent hardware
                                                cursor plane. Move that plane
                                                first so the pointer never
                                                waits behind composition; the
                                                integrated repaint below
                                                carries software cursors.
                                        */
                                        if (cursor_move_planes(x, y) && started)
                                        {
                                                /*
                                                        The independent plane
                                                        is on screen now. Do
                                                        not charge the window
                                                        compose behind it to
                                                        pointer latency.
                                                */
                                                pointer_latency_record(started);
                                                started = 0;
                                        }
                                }
                                else
                                {
                                        struct pane *pane = desktop.barring;
                                        struct pane_bar_geometry bar;

                                        // Bar movement schedules a later
                                        // frame, so both cursor paths land
                                        // synchronously here.
                                        cursor_move(x, y);

                                        if (pane && pane_bar(pane, &bar))
                                                bar_move(pane, y, &bar);
                                }

                                if (desktop.dragging)
                                        drag_move(x, y);
                                else if (desktop.resizing)
                                        resize_move(x, y);
                        }
                        else
                        {
                                cursor_move(x, y);
                                if (desktop.focused)
                                {
                                        unsigned int flags = WINDOW_KEY_POINTER_MOVE;

                                        if (atomic_read(&desktop.button_down))
                                                flags |= WINDOW_KEY_DOWN;
                                        pointer_report(desktop.focused, x, y, 0,
                                                       flags);
                                }
                        }

                        // cursor_move/reshape has painted the software
                        // fallback. Clear any old plane that resisted its
                        // explicit disable before this event is reported done.
                        cursor_plane_recover();
                }
        }

        rt_mutex_unlock(&desktop.lock);

        if (started)
                pointer_latency_record(started);
}

static void pointer_commit(s64 x, s64 y)
{
        atomic_set(&desktop.pending_x, (int)clamp_t(s64, x, 0, desktop.width - 1));
        atomic_set(&desktop.pending_y, (int)clamp_t(s64, y, 0, desktop.height - 1));

        // Stamp only the first event of a burst, so the measurement is the age
        // of the oldest movement not yet on screen.
        if (!atomic_xchg(&desktop.motion_pending, 1))
                desktop.motion_stamp = ktime_get_ns();

        /*
                A wake, not a queue. The work is one atomic commit that returns
                without waiting, so a workqueue's pool and dispatch and kworker
                would all be overhead around it.
        */
        canvas_thread_wake();
}

/*
        One movement of the hand, once the device has said it is over.

        Measuring each axis on its own was a bug you could feel. A mouse sends
        REL_X and REL_Y back to back for one movement, so X was timed against
        the gap since the last report and Y against nothing at all -- floored
        to a millisecond, which read as several times faster and earned
        several times the gain. Left and right moved at one speed, up and down
        at another.

        So the speed is the speed of the movement, not of an axis, and both
        axes are given the same gain.
*/
static void pointer_frame(void)
{
        int dx = desktop.raw_x;
        int dy = desktop.raw_y;
        u64 now, interval;
        int gain;
        u64 distance, speed;

        desktop.raw_x = 0;
        desktop.raw_y = 0;

        if (!dx && !dy)
                return;

        now = ktime_get_ns();
        interval = now - desktop.accel_stamp;
        desktop.accel_stamp = now;

        if (interval < NSEC_PER_MSEC)
                interval = NSEC_PER_MSEC;

        // The two INT_MIN squares sum to 2^63: widen each product and make
        // their addition unsigned before taking the integer square root.
        distance = int_sqrt((u64)((s64)dx * dx) + (u64)((s64)dy * dy));
        speed = div64_u64(distance * NSEC_PER_MSEC, interval);
        gain = pointer_gain((int)min_t(u64, speed, ACCEL_CEILING));

        pointer_commit((s64)atomic_read(&desktop.pending_x) +
                           accel_apply(dx, &desktop.accel_x, gain),
                       (s64)atomic_read(&desktop.pending_y) +
                           accel_apply(dy, &desktop.accel_y, gain));
}

static void pointer_event_locked(struct input_handle *handle, unsigned int type,
                                 unsigned int code, int value)
{
        if (type == EV_REL)
        {
                // Only remembered here. What it means depends on what else
                // arrives before the device says the movement is over.
                if (code == REL_X)
                {
                        desktop.raw_x = (int)clamp_t(s64,
                            (s64)desktop.raw_x + value, INT_MIN, INT_MAX);
                        pointer_shake(value);
                }
                else if (code == REL_Y)
                {
                        desktop.raw_y = (int)clamp_t(s64,
                            (s64)desktop.raw_y + value, INT_MIN, INT_MAX);
                }
                else if (code == REL_WHEEL_HI_RES ||
                         (code == REL_WHEEL &&
                          !test_bit(REL_WHEEL_HI_RES, handle->dev->relbit)))
                {
                        // Committed here rather than at the report, because a
                        // wheel is not movement: nothing else in the report
                        // changes what it means, and holding it back only
                        // delays the line by a frame.
                        // A consumer can exchange the pending distance with
                        // zero while this input lock is held. Retry against
                        // that new value instead of resurrecting drained input.
                        s64 delta = code == REL_WHEEL_HI_RES ? value
                                                              : (s64)value * WHEEL_V120;
                        int held = atomic_read(&desktop.wheel), wanted;
                        do {
                                wanted = (int)clamp_t(s64, (s64)held + delta,
                                                      INT_MIN, INT_MAX);
                        } while (!atomic_try_cmpxchg(&desktop.wheel, &held, wanted));
                        canvas_thread_wake();
                }

                return;
        }

        if (type == EV_SYN)
        {
                if (code != SYN_REPORT)
                        return;

                // A tablet reports both axes and then says it is done, the
                // same as a mouse. Committing each axis as it arrived moved
                // the cursor twice for one movement, the second time with the
                // other axis a report out of date.
                if (desktop.abs_have)
                {
                        int x = desktop.abs_have & 1
                                    ? desktop.abs_x
                                    : atomic_read(&desktop.pending_x);
                        int y = desktop.abs_have & 2
                                    ? desktop.abs_y
                                    : atomic_read(&desktop.pending_y);

                        pointer_commit(x, y);
                        desktop.abs_have = 0;
                        return;
                }

                pointer_frame();
                return;
        }

        if (type == EV_ABS)
        {
                // Absolute devices report in their own range, so scale into
                // the desktop. QEMU's tablet is one of these.
                struct input_absinfo *abs;

                if (code != ABS_X && code != ABS_Y)
                        return;

                abs = &handle->dev->absinfo[code];

                if (abs->maximum <= abs->minimum)
                        return;

                // Input ranges may span the signed domain. Widen before
                // subtracting and clamp an out-of-range report before scaling.
                _Bool horizontal = code == ABS_X;
                u64 offset = (s64)clamp(value, abs->minimum, abs->maximum) - abs->minimum;
                u32 span = (s64)abs->maximum - abs->minimum;
                int *position = horizontal ? &desktop.abs_x : &desktop.abs_y;

                *position = (int)div_u64(offset * (u32)(horizontal ? desktop.width : desktop.height), span);
                desktop.abs_have |= horizontal ? 1 : 2;

                return;
        }

        if (type == EV_KEY)
        {
                unsigned int which;

                if (code == BTN_LEFT || code == BTN_TOUCH)
                        which = 0;
                else if (code == BTN_MIDDLE)
                        which = 1;
                else if (code == BTN_RIGHT)
                        which = 2;
                else
                {
                        keyboard_event(handle, code, value);
                        return;
                }

                atomic_set(&desktop.button_x, atomic_read(&desktop.pending_x));
                atomic_set(&desktop.button_y, atomic_read(&desktop.pending_y));

                if (which == 0)
                {
                        atomic_set(&desktop.button_down, !!value);
                        atomic_set(&desktop.button_changed, 1);
                }
                else
                {
                        atomic_set(&desktop.client_button, (int)which);
                        atomic_set(&desktop.client_down, !!value);
                        atomic_set(&desktop.client_changed, 1);
                }

                canvas_thread_wake();
                return;
        }
}

/*
        One attached device, with what it has done.

        The count is per device because the one fault this has to explain is
        a mouse that does nothing until it is plugged in again: whether the
        kernel delivered nothing from it, or delivered reports the cursor did
        not follow, is the whole question, and a total over every device
        cannot answer it. The list is kept here under the input lock rather
        than read out of the input core, whose handle list has a lock of its
        own that a stats ioctl has no business taking.

        An open that fails is tried again. The input core drops a device a
        connect refuses, and a mouse the BIOS was still driving when the
        kernel took the controller can refuse its first open and accept the
        next; a device that stays refused is logged and left, with the error
        where the pointer applet can show it.
*/
#define POINTER_OPEN_TRIES 30

struct pointer_handle
{
        struct input_handle handle;
        struct list_head link;
        struct delayed_work reopen;
        unsigned long events;
        int opened;
        unsigned int tries;
        unsigned int modifiers;
};

static LIST_HEAD(pointer_handles);

static inline struct pointer_handle *pointer_handle_of(struct input_handle *handle)
{
        return container_of(handle, struct pointer_handle, handle);
}

static unsigned int *keyboard_held(struct input_handle *handle)
{
        return &pointer_handle_of(handle)->modifiers;
}

static unsigned int keyboard_modifiers(void)
{
        struct pointer_handle *pointer;
        unsigned int held = 0;

        list_for_each_entry(pointer, &pointer_handles, link)
                held |= pointer->modifiers;
        return ((held | (held >> 4)) &
                (WINDOW_KEY_SHIFT | WINDOW_KEY_CONTROL | WINDOW_KEY_ALT)) |
               ((held & KEY_HELD_ALTGR) ? WINDOW_KEY_ALTGR : 0);
}

static HOT void pointer_event(struct input_handle *handle, unsigned int type,
                              unsigned int code, int value)
{
        unsigned long flags;

        // Counted before the desktop check: a report that arrived while there
        // was no screen is still a report the device sent.
        pointer_handle_of(handle)->events++;

        if (!READ_ONCE(desktop.width))
                return;

        spin_lock_irqsave(&desktop.input_lock, flags);
        pointer_event_locked(handle, type, code, value);
        spin_unlock_irqrestore(&desktop.input_lock, flags);
}

static const char *pointer_device_name(struct input_handle *handle)
{
        return handle->dev->name ? handle->dev->name : "unnamed";
}

static void pointer_reopen(struct work_struct *work)
{
        struct pointer_handle *pointer =
            container_of(to_delayed_work(work), struct pointer_handle, reopen);
        int ret = input_open_device(&pointer->handle);

        pointer->opened = ret;

        if (!ret)
        {
                if (pointer->tries)
                        pr_info("[moonwater canvas] " "input: %s opened on try %u\n", pointer_device_name(&pointer->handle), pointer->tries + 1);
                else
                        pr_info("[moonwater canvas] " "input: %s\n", pointer_device_name(&pointer->handle));
                return;
        }

        if (!pointer->tries)
                pr_info("[moonwater canvas] " "input: %s would not open (%d), trying again\n", pointer_device_name(&pointer->handle), ret);

        if (++pointer->tries < POINTER_OPEN_TRIES)
                schedule_delayed_work(&pointer->reopen, HZ);
        else
                pr_info("[moonwater canvas] " "input: %s would not open (%d), given up\n", pointer_device_name(&pointer->handle), ret);
}

static COLD int pointer_connect(struct input_handler *handler,
                                struct input_dev *dev,
                                const struct input_device_id *id)
{
        struct pointer_handle *pointer;
        unsigned long flags;
        int ret;

        pointer = kzalloc(sizeof(*pointer), GFP_KERNEL);
        if (!pointer)
                return -ENOMEM;

        pointer->handle.dev = dev;
        pointer->handle.handler = handler;
        pointer->handle.name = "moonwater";
        INIT_DELAYED_WORK(&pointer->reopen, pointer_reopen);

        ret = input_register_handle(&pointer->handle);
        if (ret)
        {
                kfree(pointer);
                return ret;
        }

        spin_lock_irqsave(&desktop.input_lock, flags);
        list_add_tail(&pointer->link, &pointer_handles);
        spin_unlock_irqrestore(&desktop.input_lock, flags);

        /*
                Opened from a worker, not here.

                A connect runs inside the bus probe that registered the
                device, with the input core's mutex held, and opening a USB
                HID device sleeps fifty milliseconds in usbhid_open before it
                returns. Done here, that sleep sat in front of every device
                after this one on the same hub -- a mouse enumerated first
                held the keyboard back by it, and a keyboard with a media-key
                interface paid it twice -- and in front of every other input
                device registering anywhere. Nothing is lost by the worker: a
                device delivers nothing until it is open either way.
        */
        pointer->opened = -EINPROGRESS;
        schedule_delayed_work(&pointer->reopen, 0);

        return 0;
}

static COLD void pointer_disconnect(struct input_handle *handle)
{
        struct pointer_handle *pointer = pointer_handle_of(handle);
        unsigned long flags;

        cancel_delayed_work_sync(&pointer->reopen);

        // Stop callbacks before releasing held keys; no late press may
        // resurrect a modifier after this device leaves the list.
        if (!pointer->opened)
                input_close_device(handle);

        spin_lock_irqsave(&desktop.input_lock, flags);
        for (unsigned int code = 0; code < KEY_TABLE; code++)
                if (pointer->modifiers & key_mod[code])
                        keyboard_event(handle, code, 0);
        list_del(&pointer->link);
        spin_unlock_irqrestore(&desktop.input_lock, flags);

        input_unregister_handle(handle);
        kfree(pointer);
}

/*
        The console's keyboard, off while Canvas has the keys.

        The input core hands every key to every handler, and the VT's keyboard
        is one of them: it turns keys into input on the foreground console's
        tty. A machine booted without console= -- every real install -- points
        /dev/console at that tty, and init's shell reads it. So every line
        typed into a window here was typed a second time, unseen, into a root
        shell on tty1, and ran there too.

        K_OFF stops the VT turning keys into tty input and nothing else. evdev
        readers and sysrq are handlers of their own and still hear every key,
        which an exclusive grab would have taken from them, and the shell on
        tty1 stays where it is for whoever is at the console once Canvas lets
        go. Switching consoles from the keyboard is off with it, as it is under
        any display server that sets K_OFF.

        Every console, not only the one in front. Muting just that one left a
        console switch as the way round it: a program calling VT_ACTIVATE puts
        another console in front with its keyboard still on, and the keys go
        to whatever reads that tty. And a console is not muted once for good:
        allocating one runs vc_init, whose reset_vc puts its keyboard back to
        unicode, and a switch away from a VT_PROCESS owner that has died does
        the same. So the VT's own notifier is watched while Canvas has the
        keys, and a console it says was allocated or redrawn -- a switch is a
        redraw of the console coming to the front -- is muted again.

        Each console muted is remembered with the mode it had when it was
        first muted, and that is what it gets back. A mode somebody else set
        in the meantime is theirs and is left alone, and so is a console that
        was already off when Canvas arrived.

        The notifier runs with the console lock held and nothing may sleep, so
        what it and the two ends share is under a spinlock, taken outside the
        keyboard's own lock and never inside it.
*/
#ifdef CONFIG_VT
static DEFINE_SPINLOCK(canvas_vt_lock);
static _Bool canvas_vt_owned;
static _Bool canvas_vt_watching;
static signed char canvas_vt_mode[MAX_NR_CONSOLES] = {
        [0 ... MAX_NR_CONSOLES - 1] = -1,
};

// Under canvas_vt_lock.
static void canvas_keyboard_mute(unsigned int console)
{
        int mode;

        if (console >= MAX_NR_CONSOLES)
                return;

        mode = vt_do_kdgkbmode(console);
        if (mode == K_OFF || vt_do_kdskbmode(console, K_OFF))
                return;

        if (canvas_vt_mode[console] < 0)
                canvas_vt_mode[console] = (signed char)mode;
}

static int canvas_keyboard_follow(struct notifier_block *block,
                                  unsigned long event, void *data)
{
        struct vt_notifier_param *param = data;
        unsigned long flags;

        (void)block;

        if ((event != VT_ALLOCATE && event != VT_UPDATE) || !param || !param->vc)
                return NOTIFY_DONE;

        spin_lock_irqsave(&canvas_vt_lock, flags);
        if (canvas_vt_owned)
                canvas_keyboard_mute(param->vc->vc_num);
        spin_unlock_irqrestore(&canvas_vt_lock, flags);

        return NOTIFY_DONE;
}

static struct notifier_block canvas_keyboard_watch = {
        .notifier_call = canvas_keyboard_follow,
};

static void canvas_keyboard_take(void)
{
        unsigned long flags;
        unsigned int console;

        // Watching first: a console allocated between the loop below and
        // the watch starting would come up unmuted and never be told of.
        if (!canvas_vt_watching && !register_vt_notifier(&canvas_keyboard_watch))
                canvas_vt_watching = true;

        spin_lock_irqsave(&canvas_vt_lock, flags);
        canvas_vt_owned = true;
        for (console = 0; console < MAX_NR_CONSOLES; console++)
                canvas_keyboard_mute(console);
        spin_unlock_irqrestore(&canvas_vt_lock, flags);
}

static void canvas_keyboard_give(void)
{
        unsigned long flags;
        unsigned int console;

        // Unregistering waits out a notifier already running, so nothing
        // mutes a console again behind the loop below.
        if (canvas_vt_watching)
        {
                unregister_vt_notifier(&canvas_keyboard_watch);
                canvas_vt_watching = false;
        }

        spin_lock_irqsave(&canvas_vt_lock, flags);
        canvas_vt_owned = false;
        for (console = 0; console < MAX_NR_CONSOLES; console++)
        {
                if (canvas_vt_mode[console] < 0)
                        continue;

                if (vt_do_kdgkbmode(console) == K_OFF)
                        vt_do_kdskbmode(console, (unsigned int)canvas_vt_mode[console]);

                canvas_vt_mode[console] = -1;
        }
        spin_unlock_irqrestore(&canvas_vt_lock, flags);
}
#else
#define canvas_keyboard_take() ((void)0)
#define canvas_keyboard_give() ((void)0)
#endif

static void canvas_input_devices(struct input_devices *out)
{
        struct pointer_handle *pointer;
        unsigned long flags;

        memory_fill(out, 0, sizeof(*out));

        spin_lock_irqsave(&desktop.input_lock, flags);
        list_for_each_entry(pointer, &pointer_handles, link)
        {
                struct input_device_stats *device;

                if (out->count < INPUT_DEVICES_MAX)
                {
                        device = &out->device[out->count];
                        strscpy(device->name, pointer_device_name(&pointer->handle),
                                sizeof(device->name));
                        device->events = READ_ONCE(pointer->events);
                        device->opened = pointer->opened;
                }

                out->count++;
        }
        spin_unlock_irqrestore(&desktop.input_lock, flags);
}

// Anything that reports motion or keys: mice, tablets, touchpads, keyboards.
static const struct input_device_id pointer_ids[] = {
    {
        .flags = INPUT_DEVICE_ID_MATCH_EVBIT,
        .evbit = {BIT_MASK(EV_REL)},
    },
    {
        .flags = INPUT_DEVICE_ID_MATCH_EVBIT,
        .evbit = {BIT_MASK(EV_ABS)},
    },
    {
        .flags = INPUT_DEVICE_ID_MATCH_EVBIT,
        .evbit = {BIT_MASK(EV_KEY)},
    },
    {},
};

static struct input_handler pointer_handler = {
    .event = pointer_event,
    .connect = pointer_connect,
    .disconnect = pointer_disconnect,
    .name = "moonwater",
    .id_table = pointer_ids,
};

/*
        The machine's own sleep, which nothing above knew about.

        Suspend takes the card's power in the middle of whatever Canvas is
        doing, and Canvas's threads are not freezable: the frame timer kept
        waking the loop, the loop kept committing modes, and the flusher kept
        sending dirtyfb to a device that was off or coming back. On a driver
        that waits for its hardware that is a hang, in either direction, with
        the fans still going. So before the tasks are frozen and the devices
        suspended the frame timer stops, the desktop counts as taken, and any
        flush in the driver's hands is waited for; after the devices are back
        and the tasks thawed, the desktop is drawn again from scratch, as it is
        when a program lets go of the card.
*/
#define CANVAS_PM_FLUSH_WAIT_MS 2000

static void canvas_input_drop(_Bool keys);

/*
        The first redraw after a wake can land before the panel has its link
        back: the driver's resume returns, the panel is still training, and a
        commit made then is accepted and shows nothing. The desktop is drawn
        again from scratch half a second and two and a half seconds later,
        which costs nothing when the first one took.
*/
static int canvas_pm_again_left;

/*
        Whether the wake should turn the screens off and on again before the
        redraw. A real card's driver restores the state it saved, and a panel
        that state says is on but the hardware is not showing is answered by
        a plane update as if nothing had happened; only an active change makes
        it program the pipe again. A guest's display has no such drift, and
        virtio-gpu does not always get its host window back from an off.
*/
static _Bool desktop_cycle_wanted(void)
{
        struct output *output;

        if (canvas_pm_dark & 4)
                return false;

        if (canvas_pm_dark & 1)
                return true;

        list_for_each_entry(output, &desktop.outputs, link)
                if (!canvas_is_virtual(output->canvas->client.dev))
                        return true;

        return false;
}

static void canvas_pm_again_work(struct work_struct *work);
static DECLARE_DELAYED_WORK(canvas_pm_again, canvas_pm_again_work);

static void canvas_pm_again_work(struct work_struct *work)
{
        unsigned int pass = 3 - canvas_pm_again_left;

        rt_mutex_lock(&desktop.lock);
        if (!READ_ONCE(desktop.asleep) && !desktop_taken() && !(canvas_pm_dark & 2))
        {
                if (pass == 1 && desktop_cycle_wanted())
                        desktop_dpms(DRM_MODE_DPMS_OFF);

                desktop_resume();
                desktop_report(pass == 1 ? "wake +0.5 s" : "wake +2.5 s");
        }
        rt_mutex_unlock(&desktop.lock);

        if (--canvas_pm_again_left > 0)
                schedule_delayed_work(&canvas_pm_again, msecs_to_jiffies(2000));
}

static void canvas_pm_sleep(_Bool sleeping)
{
        struct output *output;

        rt_mutex_lock(&desktop.lock);

        if (sleeping)
        {
                WRITE_ONCE(desktop.asleep, true);
                desktop.suspended = true;
                if (desktop.awake)
                        desktop_set_awake(false);
        }
        else
        {
                WRITE_ONCE(desktop.asleep, false);

                // The other program that took the card while this slept is
                // still there: the loop draws when it lets go.
                if (canvas_pm_dark & 2)
                        pr_info("[moonwater canvas] " "wake: redraw left undone (moonwater.pm_dark)\n");
                else if (desktop.suspended && !desktop_taken())
                {
                        desktop_resume();
                        desktop_watch();
                        desktop_report("wake");
                }
                else
                        pr_info("[moonwater canvas] " "wake: another program has the card, the redraw waits for it\n");

                // What the driver reported while it came back, run now that
                // the desktop is drawn.
                list_for_each_entry(output, &desktop.outputs, link)
                        if (output->canvas->replug)
                        {
                                output->canvas->replug = false;
                                queue_work(canvas_plug_wq ? canvas_plug_wq : system_unbound_wq,
                                           &output->canvas->plug);
                        }
        }

        rt_mutex_unlock(&desktop.lock);

        if (sleeping)
        {
                cancel_delayed_work_sync(&canvas_pm_again);
                hrtimer_cancel(&desktop.frame);
                canvas_input_drop(true);
                wait_event_timeout(desktop.flush_idle, !atomic_read(&desktop.flushes_in_flight),
                                   msecs_to_jiffies(CANVAS_PM_FLUSH_WAIT_MS));

                if (canvas_pm_dark & 1)
                {
                        struct canvas *done = NULL;

                        rt_mutex_lock(&desktop.lock);
                        desktop_dpms(DRM_MODE_DPMS_OFF);
                        list_for_each_entry(output, &desktop.outputs, link)
                                if (output->canvas != done)
                                {
                                        done = output->canvas;
                                        drm_kms_helper_hotplug_event(done->client.dev);
                                }
                        rt_mutex_unlock(&desktop.lock);
                }
                pr_info("[moonwater canvas] " "sleep: quiet, flushes waited for\n");
        }
        else
        {
                canvas_flush_wake();
                canvas_thread_wake();
                canvas_pm_again_left = 2;
                schedule_delayed_work(&canvas_pm_again, msecs_to_jiffies(500));
        }
}

static int canvas_pm_event(struct notifier_block *block, unsigned long event,
                           void *unused)
{
        switch (event)
        {
        case PM_HIBERNATION_PREPARE:
        case PM_SUSPEND_PREPARE:
        case PM_RESTORE_PREPARE:
                canvas_pm_sleep(true);
                break;
        case PM_POST_HIBERNATION:
        case PM_POST_SUSPEND:
        case PM_POST_RESTORE:
                canvas_pm_sleep(false);
                break;
        }

        return NOTIFY_DONE;
}

static struct notifier_block canvas_pm_notifier = {
    .notifier_call = canvas_pm_event,
};

static _Bool canvas_pm_registered;

static void canvas_thread_stop(void)
{
        struct task_struct *thread = rcu_dereference_protected(
            canvas_thread, lockdep_is_held(&canvas_list_lock));

        if (!thread)
                return;
        RCU_INIT_POINTER(canvas_thread, NULL);

        if (canvas_pm_registered)
        {
                unregister_pm_notifier(&canvas_pm_notifier);
                canvas_pm_registered = false;
        }
        WRITE_ONCE(desktop.asleep, false);
        cancel_delayed_work_sync(&canvas_pm_again);

        /* Registration can be interrupted before the input core initializes
           the handler's lists.  Only hand a handler back after the matching
           registration completed. */
        if (pointer_handler_registered)
        {
                input_unregister_handler(&pointer_handler);
                pointer_handler_registered = false;
        }

        // Only once no key can reach the handler: from here the console's
        // keyboard is the only one again.
        canvas_keyboard_give();

        rt_mutex_lock(&desktop.lock);
        desktop_set_awake(false);
        rt_mutex_unlock(&desktop.lock);
        hrtimer_cancel(&desktop.frame);

        synchronize_rcu();
        kthread_stop(thread);
        canvas_hold_drop();

        // After the canvas thread, which queues flushes until it stops. A
        // flush in flight finishes before the flusher does.
        thread = rcu_dereference_protected(canvas_flusher,
                                           lockdep_is_held(&canvas_list_lock));
        if (thread)
        {
                RCU_INIT_POINTER(canvas_flusher, NULL);
                synchronize_rcu();
                kthread_stop(thread);
        }

        if (canvas_plug_wq)
        {
                destroy_workqueue(canvas_plug_wq);
                canvas_plug_wq = NULL;
        }
}

/*
        Sleeps until something moves, then draws it.

        set_current_state before the flag is read, which is what makes the
        sleep safe: a wake arriving between the two finds the task already
        marked and schedule() returns at once rather than losing the event.
*/
static void canvas_thread_wake(void)
{
        struct task_struct *thread;

        rcu_read_lock();
        thread = rcu_dereference(canvas_thread);
        if (thread)
                wake_up_process(thread);
        rcu_read_unlock();
}

// Whether there is anything to answer a frame. Nothing arms one otherwise.
static _Bool canvas_thread_running(void)
{
        return rcu_access_pointer(canvas_thread) != NULL;
}

/*
        While another program is master of a card Canvas draws on.

        libinput does not grab a device, and neither does this, so a program
        that took the display -- Weston, a game -- and Canvas both heard every
        key. Everything typed into it went into the focused Canvas window as
        well, usually a root shell and often the one that started it, which
        read it all once the program exited. A Control-Shift-T typed there
        opened a terminal behind it.

        So Canvas drops what arrives while somebody else is master: every key,
        button, movement, wheel step, chord and asked-for terminal is taken and
        thrown away before any of it reaches a window, and no frame is drawn,
        since nothing drawn would land. The consoles stay off throughout. The
        other program reads the devices itself and loses nothing.

        The test is made at the top of every pass the thread wakes for, before
        anything is delivered, and every ~250 ms while suspended with nothing
        else to wake for, so a program that drops master and stays running
        does not leave a frozen screen until the mouse moves. A key that
        arrived before the other program became master can still be delivered
        by the pass that was already running; none after. Flushes queued
        before the program took the card answer EBUSY and change nothing.
*/
#define CANVAS_SUSPENDED_POLL_MS 250

static void canvas_input_drop(_Bool keys)
{
        atomic_set(&desktop.button_changed, 0);
        atomic_set(&desktop.client_changed, 0);
        atomic_set(&desktop.motion_pending, 0);
        atomic_set(&desktop.wheel, 0);
        atomic_set(&desktop.focus_steps, 0);
        atomic_set(&desktop.focus_commit, 0);
        atomic_set(&desktop.minimize, 0);
        atomic_set(&desktop.frame_pending, 0);

        // The tail is this thread's to move; the handler only moves head.
        if (keys)
                atomic_set(&desktop.key_tail, atomic_read(&desktop.key_head));

        /*
                A wanted terminal is not dropped.

                Off then on used to lose the first /term whenever this ran
                in the same pass as canvas_start's spawn: the claim file, or
                the console client off had just set up, still looked like
                master, spawn was cleared, and nothing asked again. Keys
                typed at a program that holds the card still go nowhere;
                the terminal is started so the desktop has one when that
                program leaves.
        */
}

static _Bool canvas_suspend_check(void)
{
        _Bool taken;

        rt_mutex_lock(&desktop.lock);

        taken = desktop_taken();
        if (taken)
        {
                desktop.suspended = true;

                // No frame drawn now would land, so none is worth a timer: left
                // awake, desktop_frame wakes this thread every frame for as long
                // as the other program keeps the card. Programs make their call
                // again, which answers at once while the card is taken; the
                // resume redraws every pane, and the first commit after it
                // re-arms the timer through desktop_watch.
                if (desktop.awake)
                        desktop_set_awake(false);
        }
        else if (desktop.suspended)
                desktop_resume();

        rt_mutex_unlock(&desktop.lock);

        // Keys are not dropped while the card only sleeps: a keyboard that
        // reports the key that woke the machine after its resume (a USB one
        // does; a PS/2 controller's buffer is flushed by its own resume) has
        // that key delivered once the wake has redrawn. Another program
        // holding the card is what drops them.
        if (taken)
                canvas_input_drop(!READ_ONCE(desktop.asleep));

        return taken;
}

static void canvas_flush_wake(void)
{
        struct task_struct *thread;

        rcu_read_lock();
        thread = rcu_dereference(canvas_flusher);
        if (thread)
                wake_up_process(thread);
        rcu_read_unlock();
}

// Without a flusher, a flush happens where it is asked for.
static _Bool canvas_flush_running(void)
{
        return rcu_access_pointer(canvas_flusher) != NULL;
}

static int canvas_loop(void *unused)
{
        while (!kthread_should_stop())
        {
                _Bool input;

                set_current_state(TASK_IDLE);

                if (!atomic_read(&desktop.motion_pending) &&
                    !atomic_read(&desktop.button_changed) &&
                    !atomic_read(&desktop.client_changed) &&
                    !atomic_read(&desktop.frame_pending) &&
                    !atomic_read(&desktop.wheel) &&
                    !atomic_read(&desktop.focus_steps) &&
                    !atomic_read(&desktop.focus_commit) &&
                    !atomic_read(&desktop.minimize) &&
                    !atomic_read(&desktop.spawn) &&
                    atomic_read(&desktop.key_head) == atomic_read(&desktop.key_tail))
                        schedule_timeout(canvas_hold_sleep(
                            READ_ONCE(desktop.suspended) && !READ_ONCE(desktop.asleep)
                                ? msecs_to_jiffies(CANVAS_SUSPENDED_POLL_MS)
                                : MAX_SCHEDULE_TIMEOUT));

                __set_current_state(TASK_RUNNING);
                WRITE_ONCE(canvas_passes, canvas_passes + 1);

                // What a person did, read before the pass consumes it.
                input = atomic_read(&desktop.motion_pending) ||
                        atomic_read(&desktop.button_changed) ||
                        atomic_read(&desktop.wheel) ||
                        atomic_read(&desktop.focus_steps) ||
                        atomic_read(&desktop.focus_commit) ||
                        atomic_read(&desktop.minimize) ||
                        atomic_read(&desktop.key_head) != atomic_read(&desktop.key_tail);

                if (hold_on && !input && ktime_get_ns() >= hold_until)
                        canvas_hold_drop();

                /*
                        Outside desktop.lock, and before the suspend check.

                        Starting a program allocates, makes a task and runs
                        execve on it, none of which the lock has anything to do
                        with -- and the window it ends up asking for is created
                        under that same lock by the ioctl the new program will
                        make. Holding it across the spawn is a lock held over an
                        unbounded amount of somebody else's work.

                        The spawn is consumed here even when another program
                        still looks like master: waiting until the card is
                        ours again was how off then on came back painted and
                        with every key dropped, because the first terminal
                        had been asked for and then never started.
                */
                if (atomic_xchg(&desktop.spawn, 0))
                {
                        int ret = spawn_terminal();

                        pr_info("[moonwater canvas] " "terminal: %d\n", ret);
                }

                if (canvas_suspend_check())
                        continue;

                pointer_apply();

                // After the movement is applied, so the first event after a
                // pause is not made to wait for the request; every one after
                // it inside the window finds it taken.
                if (input)
                        canvas_hold_touch();

                if (atomic_read(&desktop.focus_steps) ||
                    atomic_read(&desktop.focus_commit) ||
                    atomic_read(&desktop.minimize))
                {
                        unsigned int steps =
                            (unsigned int)atomic_xchg(&desktop.focus_steps, 0);
                        _Bool commit = atomic_xchg(&desktop.focus_commit, 0);
                        _Bool minimize = atomic_xchg(&desktop.minimize, 0);
                        _Bool changed = false;

                        rt_mutex_lock(&desktop.lock);
                        while (steps--)
                                changed |= pane_focus_step();
                        if (minimize)
                                changed |= pane_minimize_focused();
                        if (commit)
                                changed |= pane_focus_commit();
                        if (changed)
                                desktop_recompose();
                        rt_mutex_unlock(&desktop.lock);
                }

                if (atomic_read(&desktop.key_head) != atomic_read(&desktop.key_tail))
                {
                        rt_mutex_lock(&desktop.lock);
                        keys_deliver();
                        rt_mutex_unlock(&desktop.lock);
                }

                // On this thread because it walks the window list and writes
                // cells, neither of which an input callback may do.
                if (atomic_read(&desktop.wheel))
                {
                        rt_mutex_lock(&desktop.lock);
                        wheel_deliver();
                        rt_mutex_unlock(&desktop.lock);
                }

                // The console's queued records into its window first, so the
                // frame they asked for draws them.
                console_drain();

                if (atomic_xchg(&desktop.frame_pending, 0))
                        desktop_frame_pass();
        }

        return 0;
}

static void canvas_thread_start(void)
{
        /*
                A thread rather than a workqueue.

                WQ_HIGHPRI raises a kworker's nice level and leaves it an
                ordinary task, so it is still scheduled against everything
                else running. SCHED_FIFO is a different queue entirely: the
                scheduler picks it before any normal task, which is the whole
                of what this thread is for.

                Priority 1, as sched_set_fifo_low gives: ahead of every
                SCHED_OTHER task and behind anything the machine considers
                more urgent than a cursor, which is the honest place for it.

                And reset on fork, which sched_set_fifo_low does not ask for.
                This thread starts programs -- Control-Shift-T's terminal is
                user_mode_thread called from here -- and a task forked from a
                FIFO task is FIFO unless the parent says otherwise. So every
                terminal opened from the keyboard, its shell and whatever was
                run in it were FIFO 1 beside this thread: a stress-ng --cpu 0
                in one held every processor against the cursor, which FIFO
                never timeslices at equal priority, and left the other
                terminals and every kworker RT throttling's 5%. With the flag
                the scheduler starts each child SCHED_OTHER at nice 0, by its
                own rule and on every path a program can be started from.
        */
        static const struct sched_attr canvas_policy = {
                .size = sizeof(struct sched_attr),
                .sched_policy = SCHED_FIFO,
                .sched_priority = 1,
                .sched_flags = SCHED_FLAG_RESET_ON_FORK,
        };
        static _Bool frame_ready;
        struct task_struct *thread, *flush;

        // Once: hrtimer_setup on a timer that has already been cancelled is
        // a second init of the same object, which some kernels warn on, and
        // off then on is exactly that path.
        if (!frame_ready)
        {
                hrtimer_setup(&desktop.frame, desktop_frame, CLOCK_MONOTONIC,
                              HRTIMER_MODE_REL);
                frame_ready = true;
        }
        thread = kthread_run(canvas_loop, NULL, "moonwater/canvas");

        if (IS_ERR(thread))
        {
                pr_info("[moonwater canvas] " "no thread for input\n");
                return;
        }

        rcu_assign_pointer(canvas_thread, thread);
        WARN_ON_ONCE(sched_setattr_nocheck(thread, &canvas_policy));

        /*
                The flusher, at the same policy. Without it every flush happens
                where it is asked for, which still works: it is what the canvas
                thread and every committing program did before there was one.
        */
        flush = kthread_run(canvas_flush_loop, NULL, "moonwater/flush");
        if (IS_ERR(flush))
                pr_info("[moonwater canvas] " "no flush thread, flushing in place\n");
        else
        {
                WARN_ON_ONCE(sched_setattr_nocheck(flush, &canvas_policy));
                rcu_assign_pointer(canvas_flusher, flush);
        }

        if (!register_pm_notifier(&canvas_pm_notifier))
                canvas_pm_registered = true;

        // Before the handler, so no key is ever delivered to both.
        canvas_keyboard_take();

        if (input_register_handler(&pointer_handler))
        {
                pr_info("[moonwater canvas] " "could not register the input handler\n");
                canvas_keyboard_give();
        }
        else
                pointer_handler_registered = true;
}

// Nanoseconds, for the stats ioctl.
static void canvas_input_stats(struct input_stats *out)
{
        unsigned long n = pointer_events ? pointer_events : 1;

        out->events = pointer_events;
        out->mean_ns = pointer_latency_total / n;
        out->worst_ns = pointer_latency_worst;
        out->queue_ns = pointer_queue_total / n;
        out->draw_ns = pointer_draw_total / n;
        out->flush_ns = pointer_flush_total / n;
        out->counts = pointer_counts;
        out->moved = pointer_moved;
        out->composes = canvas_composes;
        out->compose_ns = canvas_compose_ns;
        out->painted = canvas_painted;
        out->runs = canvas_runs;
        out->driver_ns = canvas_flush_ns;
        out->text_ns = canvas_text_ns;
}

static void canvas_cursor_stats(struct cursor_stats *out)
{
        struct drm_rect cursor;
        struct output *output;

        memory_fill(out, 0, sizeof(*out));
        rt_mutex_lock(&desktop.lock);

        out->requested_generation = cursor_plane_requested_generation;
        out->armed_generation = cursor_plane_armed_generation;
        out->updates = (unsigned long)atomic_long_read(&cursor_plane_updates);
        out->failures = (unsigned long)atomic_long_read(&cursor_plane_failures);
        out->requested_x = cursor_plane_requested_x;
        out->requested_y = cursor_plane_requested_y;
        out->armed_x = cursor_plane_armed_x;
        out->armed_y = cursor_plane_armed_y;
        cursor_cell(&cursor, desktop.cursor_x, desktop.cursor_y,
                    desktop.cursor_shape, desktop.cursor_scale);

        list_for_each_entry(output, &desktop.outputs, link)
        {
                if (output_touched(output, &cursor, 1))
                        out->wanted++;

                if (output->cursor_plane)
                {
                        out->active++;

                        if (output->cursor_shown)
                                out->shown++;
                }
        }

        out->recovering = cursor_plane_recovery;

        rt_mutex_unlock(&desktop.lock);
}

/* ---- ioctl: what the device asks of Canvas ---- */

/*
        The three diagnostics: what the input handler counted, where the
        cursor is and what the planes did, and which devices it is attached
        to and how many reports each has delivered.

        CAP_SYS_ADMIN, because /dev/spark is open to everyone and these are
        a live record of somebody else's hands: a report count per device
        moves with every key pressed at a keyboard, a process that reads it
        in a loop reads when another user types and how fast, and the
        cursor's coordinates are where their pointer is. `pointer` and
        `spawn` are the machine's operator's tools, which need nothing they
        did not need before -- root runs them. The struct starts zeroed, not
        as whatever the stack held: a collector that fills every field today
        is one field short the day one is added, and the copy is of all of
        it.
*/
#define REPORT_CANVAS(name, type, collect)                                   \
        static long name(struct type __user *out)                            \
        {                                                                    \
                struct type stats = {};                                      \
                                                                             \
                if (!capable(CAP_SYS_ADMIN))                                 \
                        return -EPERM;                                       \
                collect(&stats);                                             \
                return copy_to_user(out, &stats, sizeof(stats)) ? -EFAULT : 0; \
        }

REPORT_CANVAS(report_input, input_stats, canvas_input_stats)
REPORT_CANVAS(report_cursor, cursor_stats, canvas_cursor_stats)
REPORT_CANVAS(report_devices, input_devices, canvas_input_devices)
#undef REPORT_CANVAS

/*
        Canvas off and on, and what it holds.

        The state is copied back whatever the request answers, so a refused on
        can name the program that holds the display. Reading needs nothing;
        on and off stop and start the desktop everyone at the machine is
        using, so they need CAP_SYS_ADMIN.
*/
static long report_canvas(struct canvas_control __user *out)
{
        struct canvas_control control;
        unsigned int request;
        long answer = 0;
        char layout[16];

        if (copy_from_user(&control, out, sizeof(control)))
                return -EFAULT;

        request = control.request;
        memcpy(layout, control.master_command, sizeof(layout));
        layout[sizeof(layout) - 1] = 0;
        if (request > SPARK_CANVAS_TERMINAL)
                return -EINVAL;
        if (request == SPARK_CANVAS_LAYOUT)
        {
                if (layout[0] && !capable(CAP_SYS_ADMIN))
                        return -EPERM;
        }
        else if (request != SPARK_CANVAS_STATUS && !capable(CAP_SYS_ADMIN))
                return -EPERM;

        memset(&control, 0, sizeof(control));
        control.request = request;

        if (request == SPARK_CANVAS_LAYOUT)
        {
                if (layout[0])
                        answer = canvas_layout_set(layout);
                strscpy(control.master_command, canvas_layout_name(),
                        sizeof(control.master_command));
                if (copy_to_user(out, &control, sizeof(control)))
                        return -EFAULT;
                return answer;
        }

        if (request == SPARK_CANVAS_ON)
                answer = canvas_turn_on(&control);
        else if (request == SPARK_CANVAS_OFF)
                answer = canvas_turn_off();
        else if (request == SPARK_CANVAS_KERNEL_LOG)
                answer = canvas_log_open();
        else if (request == SPARK_CANVAS_TERMINAL)
                answer = canvas_terminal_open();

        canvas_state(&control);

        if (copy_to_user(out, &control, sizeof(control)))
                return -EFAULT;

        return answer;
}
