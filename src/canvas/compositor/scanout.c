/*
        What the card scans out, and how a frame gets there: a window shown straight from its buffer (its framebuffer,
        the tabs patched over it and what they covered kept, compositor_direct_under_ready), the card's present that
        nothing waits for in place (struct compositor_card, its steps and the pictures its jobs read, compositor_card_reads),
        the stale of the two plates and their swap, the flip and its event, the present's time and the frames it was late,
        the one way to the screen for each kind of card (compositor_copies_damage, compositor_plate_wanted), the
        processor's mirror and slices, the overlay and its buffers (compositor_overlay_width) and the planes, a screen
        turned by the card or by Canvas, and the damage cut to what each screen shows. It includes record.h, the present
        record, which reads the flips refused and the lists drawn.

        It shows and flips what was composed; it does not compose (compose.c, present.c) and does not take the screens
        or make the scanout (kms.c). It reads the compositor's state and the kernel's interfaces in compositor.c and
        comes after compose.c, pointer.c, outputs.c and damage.c, whose frames, cursor, clips and damage it uses; what it
        calls of gpu_host.c, present.c, kms.c and input.c is declared before the call. Called by present.c above all,
        and by kms.c (the overlay off, the plates, the turns), gpu_host.c (the pins, the turn), session.c and manage.c
        (a row's framebuffer, the card's reads), compositor.c's loop (the flip's event, the late present) and
        wayland_host.c (the planes' properties).
*/

/* ---- a window shown straight from its buffer ---- */

/*
        A window that fills the output is shown from the buffer its program
        drew, with no copy: the card scans that buffer out, and the compositor
        writes only what lies over it -- the tabs -- into the buffer itself,
        keeping what they covered, and tells the program the rectangle it wrote
        (spoiled) so the program draws it again before it shows that buffer
        next. A window is told a picture was taken when the card has let go of
        the buffer it showed before (the flip's event), never while the card
        still reads it. Anything the card or udmabuf will not do, and any
        window that does not fill the output alone, is composed and copied as
        before.
*/
struct compositor_fb2
{
        unsigned int fb_id, width, height, format, flags;
        unsigned int handles[4], pitches[4], offsets[4];
        unsigned long modifier[4];
};

// The card's pins of the table's picture pages (gpu.c's dma-buf route): a count a row, and the row's pinned flag stays set while one is held, so the program that
// owns the row gives back no page the card may read. offset is the pages' place in the table's memfd; the pins of other rows are not counted.
static unsigned int *compositor_gpu_pins;
static int compositor_shows_row(int row);
static void compositor_row_reclaim(int row);
static void compositor_gpu_pin(positive offset, b32 on)
{
        positive buffers;
        unsigned int row;

        if (!compositor.table || !compositor.buffer || offset < compositor.pixels)
                return;
        buffers = (offset - compositor.pixels) / compositor.buffer, row = (unsigned int)(buffers / 2);
        if (row >= COMPOSITOR_ROWS)
                return;
        if (on)
                compositor_gpu_pins[row]++;
        else if (compositor_gpu_pins[row])
                compositor_gpu_pins[row]--;
        __atomic_store_n(&compositor.table->pinned[row],
                         compositor_gpu_pins[row] || compositor_shows_row(row) || rows_has(&compositor.dropping, row) ? 1u : 0u,
                         __ATOMIC_RELEASE);
        if (!compositor_gpu_pins[row])
                compositor_row_reclaim((int)row);
}

/*
        A refusal of a table picture's framebuffer, as the table says it: the step that failed (refused) and the kernel's error, which is what raises the
        board's warning (windows_gpu_wanted: a refusal with an error). On amdgpu there is none: the table's pages reach the card as an import of a udmabuf,
        amdgpu imports a dma-buf it did not make with no AMDGPU_GEM_CREATE_CPU_GTT_USWC, so amdgpu_display_supported_domains gives it VRAM alone, and
        amdgpu_display_user_framebuffer_create refuses an import it cannot place in GTT (EINVAL), whatever its pitch or offset. That is every amdgpu's
        answer, which the owner can do nothing about: it is said in moonwater debug (the refusal's step), not as a warning that hides the ones that are not.
        What amdgpu can show straight is a client's own dma-buf (compositor_lent_fb). The refusal is made once a session (direct_refused).
*/
static int compositor_driver_in(const char *const *names, unsigned int count);
static void compositor_window_fb_refused(unsigned int step, long error)
{
        static const char *const no_imports[] = {"amdgpu"};

        compositor.direct_refused = 1;
        compositor.table->refused = step;
        compositor.table->refused_error = compositor_driver_in(no_imports, 1) ? 0 : (unsigned int)-error;
}

// The framebuffer of a row's buffer, made the first time it is asked for; 0 when the table is not a memfd or udmabuf
// or the card refuses (a card that cannot show memory it did not make), and then none is asked for again.
static unsigned int compositor_window_fb(int row, unsigned int which)
{
        struct
        {
                unsigned int memfd, flags;
                unsigned long offset, size;
        } create;
        struct
        {
                unsigned int handle, flags;
                int fd;
        } prime = {0, 0, -1};
        struct compositor_fb2 fb = {0};
        long dmabuf, made;

        if (compositor.scan_fb[row][which] || !compositor.table_memfd || compositor.direct_refused)
                return compositor.scan_fb[row][which];
        if (!compositor.udmabuf)
        {
                long handle = (long)system_open_at(AT_FDCWD, (string_address) "/dev/udmabuf", FILE_READ_WRITE | O_CLOEXEC);

                if (handle < 0)
                {
                        compositor_window_fb_refused(1, handle);
                        return 0;
                }
                compositor.udmabuf = (int)handle + 1;
        }

        // Pinned first: a program gives back no pages of a buffer the card may read.
        __atomic_store_n(&compositor.table->pinned[row], 1u, __ATOMIC_RELEASE);
        create.memfd = (unsigned int)(compositor.table_memfd - 1);
        create.flags = 1; // UDMABUF_FLAGS_CLOEXEC
        create.offset = compositor.pixels + ((unsigned long)row * 2 + which) * compositor.buffer;
        create.size = compositor.buffer;
        dmabuf = system_control(compositor.udmabuf - 1, COMPOSITOR_UDMABUF_CREATE, address_of create);
        if (dmabuf < 0)
        {
                compositor_window_fb_refused(2, dmabuf);
                return 0;
        }
        prime.fd = (int)dmabuf;
        if ((made = system_control(compositor.card, COMPOSITOR_PRIME_FD_TO_HANDLE, address_of prime)) != 0)
        {
                system_close(dmabuf);
                compositor_window_fb_refused(3, made);
                return 0;
        }
        system_close(dmabuf);

        fb.width = (unsigned int)compositor.width;
        fb.height = (unsigned int)compositor.height;
        fb.format = COMPOSITOR_XRGB8888;
        fb.handles[0] = prime.handle;
        fb.pitches[0] = (unsigned int)compositor_stride() * 4;
        if ((made = system_control(compositor.card, COMPOSITOR_ADDFB2, address_of fb)) != 0)
        {
                unsigned int close[2] = {prime.handle, 0};

                system_control(compositor.card, COMPOSITOR_GEM_CLOSE, address_of close);
                compositor_window_fb_refused(4, made);
                return 0;
        }
        compositor.scan_fb[row][which] = fb.fb_id;
        compositor.scan_handle[row][which] = prime.handle;
        return fb.fb_id;
}

static void compositor_row_reclaim(int row)
{
        if (!rows_has(&compositor.reclaim, row))
                return;
        if (compositor.showing[row] || rows_has(&compositor.dropping, row) || compositor_gpu_pins[row])
                return;
        rows_del(&compositor.reclaim, row);
        if (!compositor.table || __atomic_load_n(&compositor.table->pid[row], __ATOMIC_ACQUIRE))
                return;
        gpu_forget((const void *)((char *)compositor.table + compositor.pixels + (positive)row * 2 * compositor.buffer), 2 * compositor.buffer); // as at the release
        compositor_call(syscall(madvise), (positive)compositor.table + compositor.pixels + (positive)row * 2 * compositor.buffer,
                        2 * compositor.buffer, 9, 0, 0, 0);
}

// A row's framebuffers gone, when the card is not showing one of them.
static void compositor_window_fb_drop(int row)
{
        for (unsigned int which = 0; which < 2; which++)
                if (compositor.scan_fb[row][which])
                {
                        unsigned int close[2] = {compositor.scan_handle[row][which], 0};

                        system_control(compositor.card, COMPOSITOR_RMFB, address_of compositor.scan_fb[row][which]);
                        system_control(compositor.card, COMPOSITOR_GEM_CLOSE, address_of close);
                        compositor.scan_fb[row][which] = compositor.scan_handle[row][which] = 0;
                }
        compositor.patched[row][0] = compositor.patched[row][1] = 0;
        __atomic_store_n(&compositor.table->pinned[row],
                         compositor_gpu_pins[row] || rows_has(&compositor.dropping, row) ? 1u : 0u,
                         __ATOMIC_RELEASE); // the card's pins hold it too
}

static int compositor_shows_row(int row)
{
        return (compositor.on_screen &&
                (compositor.on_screen == compositor.scan_fb[row][0] || compositor.on_screen == compositor.scan_fb[row][1])) ||
               (compositor.plane_fb &&
                (compositor.plane_fb == compositor.scan_fb[row][0] || compositor.plane_fb == compositor.scan_fb[row][1]));
}

/*
        A present the card draws, without a wait (gpu.c's gpu_submit and gpu_scene_submit). Its jobs, in order: the pictures of the frames drawn at less than
        their size, a blit each (compositor_gpu_scale_frames), then a scene for each rectangle of the damage, each binned on its own in the side of the bins no
        running job reads, all into the buffer the present composes into. The card holds two jobs at a time (a slot each), so the jobs of a present of more
        are submitted as the ones before them end, the loop woken by the sync_file of the one it waits for (compositor_card_tick). The last job's sync_file is
        the frame's: the flip of the plate composed into is committed with it as the primary plane's IN_FENCE_FD (compositor_flip_fenced), so the kernel
        flips once the card is done, and the loop polls it too, for what comes once the pixels are there (compositor_card_done): the screens told what
        changed, the flip where the card takes no fence (the legacy flip, a headless scan image's names), the present's time (the metric the lead is measured
        on), and what the clients are told of their pictures: nothing while a job may read them (the loop reads no client while compositor_card.on). Nothing
        waits for the card in place. A job not done GPU_HANG_NS after its submit is the hang path there was (compositor_gpu_job_failed: the cool-down, the
        processor draws), and the frame it was for is drawn again (compositor_card_abandon); a flip whose event never comes is compositor_flip_lost.

        It is the present there is; the owner's file /run/moonwater/present-wait (looked for at the start: compositor_card.nowait 0) has every job of a
        present waited for in place as it was (gpu_scene and gpu_blit, a tick each), for a comparison. What the programs are told of their pictures waits
        for the jobs that read them wherever it is told (compositor_card_reads: the doors' taken, compositor_take; the shim's releases, row_busy).

        A window's list is drawn the same way unless the owner asks for it waited for in place, as it was (the file /run/moonwater/lists-wait, looked
        for at the start: compositor_card.lists_nowait 0): its job is submitted (gpu_scene_submit, its records in their own half
        of the ops' megabyte, COMPOSITOR_GPU_LIST_OPS_AT) and the row's picture is the job's until its fence says done (compositor_card_reads, so the door's
        taken is held and the program writes nothing the job reads); a present is held while it runs (it reads that picture), the clients' doors and
        sockets are not read while it is younger than GPU_LATE_NS (no other list asks for the card meanwhile, so none is refused for want of it), the doors
        that knocked with it are read at a later turn from the first of them (compositor_card_door_waits), and the loop's turn gives the row back when it is
        done (compositor_card_lists_settle), or with the hang path at its deadline, the list then refused (list_state 2: the program paints that frame).
*/
enum
{
        COMPOSITOR_CARD_SCALE, // a frame's picture at the size it is drawn (gpu.asm's op 7)
        COMPOSITOR_CARD_SCENE  // a rectangle of the scene (op 6)
};
#define COMPOSITOR_CARD_STEPS (8 + COMPOSITOR_DAMAGE) // the scaled frames' blits (COMPOSITOR_GPU_SCALED of them) and a scene a rectangle
struct compositor_card_step
{
        unsigned int kind, w, h, sp, dp;
        positive src, dst, tab;
        int rect[4];
};
static struct
{
        b32 nowait;                 // the loop's presents' jobs go without a wait (0: /run/moonwater/present-wait was there at the start)
        b32 looped;                 // the present in hand is the loop's (compositor_main): one made anywhere else, the take of the screens' own before
                                    // its mode set, waits for its jobs in place
        b32 on;                     // a present's jobs are on the card, or waiting for their turn
        b32 failed;                 // one was refused after one before it was submitted: the frame is drawn again once those are done
        b32 fenced;                 // the flip was committed with the last job's sync_file
        unsigned int steps, next, submitted; // the jobs, the next to submit, and how many were
        struct compositor_card_step step[COMPOSITOR_CARD_STEPS];
        unsigned int records, pitch, width, height; // the scene's records and the picture they are drawn into (pitch in pixels)
        positive out, began, last;  // the picture's address for the card, when the present began, and the last job submitted
        positive handed;            // when the loop's part of the present was over, its jobs handed to the card (compositor_present)
        int fence;                  // the sync_file the loop polls, plus one (0: none), and the job it is of
        positive fence_of;
        unsigned short clip[COMPOSITOR_DAMAGE][4]; // the present's rectangles, as the screens are told them
        unsigned int clips;
        int turning, composed, flip_own, back, plane_off; // how the present ends (compositor_present_tell)
        struct compositor_rows rows; // the rows whose pictures the present's jobs read (bit row)
        b32 lists_nowait;           // a window's list is drawn by a job not waited for (/run/moonwater/lists-wait was not there at the start)
        struct compositor_rows list_rows; // the rows whose picture a list's job not waited for draws (bit row), each one's job, and when the first began
        positive *list_seq, list_began;
        unsigned int list_recovered; // the card's restarts (gpu_recovered) when they were submitted: one since ends them, as a hang does
        unsigned int door_from;     // the door read first at the next turn: the first of those a list's job left for later
} compositor_card;

/*
        Whether a job of the card that reads a row's picture where it is (a window's table buffer, a client's lent buffer) may still run: while it does,
        nothing that program is told may let it draw into what the job reads (the door's "taken", the shim's release of a buffer a commit replaced), and
        both wait for it to be over: its fence signalled, or the hang path's end of it. A present whose jobs are waited for in place has no job left once it
        is over; a window's list drawn without a wait is its row's job until the loop's turn has seen it done (compositor_card.list_rows).
*/
static int compositor_card_reads(int row)
{
        return row >= 0 && row < COMPOSITOR_ROWS && ((compositor_card.on && rows_has(&compositor_card.rows, row)) || rows_has(&compositor_card.list_rows, row));
}

// Whether the doors still to be read this turn wait for a later one: a list's job not waited for was submitted in this turn (listing: one was on the card
// before the doors were read, a job late past GPU_LATE_NS, which holds no door), and the next list would find the card busy and be refused.
static b32 compositor_card_door_waits(b32 listing)
{
        return compositor_card.lists_nowait && !listing && rows_any(&compositor_card.list_rows);
}


// A flip to fb, done at the next vertical blank, with its event; false when the card refuses it.
/*
        The second scanout (compositor_plate_make, compositor_plate_swap): the buffer the screen is not showing is the one composed into,
        and the screen is flipped to it. The display never scans a buffer that is being drawn: an animation drawn in place tore at the line
        the write had reached, every frame. compositor_spare is that buffer; compositor.fb, scanout, scanout_handle, scanout_bytes and
        scanout_pitch are the one composed into (the target) once swapped in, and the one shown once flipped to. stale is what a buffer
        misses: the damage of every present since it was last composed, the whole screen when that is not known. Headless (-G, a file),
        the flip exchanges the two files' names (renameat2), so the scan image is always a whole frame.
*/
struct compositor_plate
{
        unsigned int fb, handle;
        unsigned int *pixels;
        unsigned long bytes, pitch;
        int stale[4], stale_any; // x1 y1 x2 y2 of what is not current, and whether there is any
};
static struct compositor_plate compositor_spare;      // the buffer the screen is not showing (fb 0: none; headless, the scan image's other name, 2)
static int compositor_front_stale[4], compositor_front_stale_any; // the stale of the buffer in compositor.* (the target, or the one shown)
/*
        A stale's own rectangles, beside its box: the damage of a present is up to COMPOSITOR_DAMAGE rectangles, and the buffer
        that missed them misses those and not the box round them (two windows that draw in opposite corners were composed again,
        in the other buffer at the next present, as the rectangle that holds both). The parts are the stale exactly while of is
        the box they were joined into (compositor_parts_hold): a box set or joined by anything else is no longer theirs, and then
        the box is what is composed, as it was. More rectangles than there is room for give the parts up the same way.
*/
struct compositor_parts
{
        int rect[COMPOSITOR_DAMAGE][4], of[4];
        unsigned int count;
};
static struct compositor_parts compositor_front_parts, compositor_spare_parts;
static unsigned int compositor_flip_refused; // presents the card refused a flip in a row (three: a modeset)
static int compositor_plate_refused;                  // the card would not make the second buffer: none until the scanout changes
static char compositor_scan_name[512], compositor_spare_name[512]; // headless: the scan image's file and its spare
static int compositor_headless_flip(void);
static void compositor_flip_done(void);
static void compositor_plate_make(void);
#include "../record.h" // the present record: a ring of the last presents, and moonwater canvas record (its files under /run/moonwater/record)

// What is missed of a buffer, joined with more: the bounding rectangle, and whether there is any.
static void compositor_stale_join(int *rect, int *any, const int *add)
{
        if (!*any)
        {
                memory_copy(rect, add, 4 * sizeof(int));
                *any = 1;
                return;
        }
        compositor_joined(rect, add, rect);
}

// A stale that is the whole screen: its box, and no parts (the box of parts that reached every edge is this box too, and is not this).
static void compositor_stale_all(int *box, int *any, struct compositor_parts *parts)
{
        box[0] = box[1] = 0, box[2] = compositor.width, box[3] = compositor.height;
        *any = 1;
        parts->count = 0;
}

// Whether a stale's parts are the box's: there are some, and the box is the one they were joined into.
static int compositor_parts_hold(const struct compositor_parts *parts, const int *box, int any)
{
        return any && parts->count && !memory_compare(parts->of, box, sizeof(parts->of));
}

// A rectangle joined into a stale and into its parts (compositor_stale_join for the box): the parts go on being the stale when they
// were, or when the stale was empty, and there is room; else they are given up and the box stands for them.
static void compositor_stale_part(int *box, int *any, struct compositor_parts *parts, const int *add)
{
        int held = !*any || compositor_parts_hold(parts, box, *any);

        if (!*any)
                parts->count = 0;
        compositor_stale_join(box, any, add);
        if (!held || parts->count == COMPOSITOR_DAMAGE)
        {
                parts->count = 0;
                return;
        }
        memory_copy(parts->rect[parts->count++], add, sizeof(parts->of));
        memory_copy(parts->of, box, sizeof(parts->of));
}

// The screen's buffer and the spare trade places: the one composed into is the scanout (compositor.*), with its stale; the spare is the one
// shown before. The screen's output 0 follows its framebuffer, which a crtc set or a plane would name.
static void compositor_plate_swap(void)
{
        unsigned int fb = compositor.fb, handle = compositor.scanout_handle, *pixels = compositor.scanout;
        unsigned long bytes = compositor.scanout_bytes, pitch = compositor.scanout_pitch;
        int stale[4], stale_any = compositor_front_stale_any;

        memory_copy(stale, compositor_front_stale, sizeof(stale));
        compositor.fb = compositor_spare.fb, compositor.scanout_handle = compositor_spare.handle, compositor.scanout = compositor_spare.pixels;
        compositor.scanout_bytes = compositor_spare.bytes, compositor.scanout_pitch = compositor_spare.pitch;
        memory_copy(compositor_front_stale, compositor_spare.stale, sizeof(stale));
        compositor_front_stale_any = compositor_spare.stale_any;
        compositor_spare.fb = fb, compositor_spare.handle = handle, compositor_spare.pixels = pixels;
        compositor_spare.bytes = bytes, compositor_spare.pitch = pitch;
        memory_copy(compositor_spare.stale, stale, sizeof(stale));
        compositor_spare.stale_any = stale_any;
        {
                struct compositor_parts parts = compositor_front_parts;

                compositor_front_parts = compositor_spare_parts;
                compositor_spare_parts = parts;
        }
        if (compositor.outputs && compositor.output[0].fb == fb)
                compositor.output[0].fb = compositor.fb;
}

// Headless: the scan image and its spare exchange names. The compositor's mappings follow the names (compositor_plate_swap made them so).
static int compositor_headless_flip(void)
{
        if (!compositor_scan_name[0] || !compositor_spare_name[0])
                return 0;
        return compositor_call(syscall(renameat2), (positive)AT_FDCWD, (positive)compositor_scan_name, (positive)AT_FDCWD,
                               (positive)compositor_spare_name, 2, 0) == 0;
}

/*
        The card told what changed of a framebuffer (DIRTYFB), where it is wanted: a virtual card copies to its host only what it is told, and a card
        whose framebuffer is scanned out as it is answers ENOSYS (amdgpu answers a program's every DIRTYFB so, and a driver with no dirty does), which
        the same card, by its descriptor and its driver's name, is not asked again. A headless compositor has no card to tell. Asked anyway, the
        headless present made one failed call a frame (EBADF on descriptor -1), and every present on amdgpu one that answered ENOSYS.
*/
static struct
{
        int card;        // the descriptor of the card that said it wants none, -1 for none
        char driver[16]; // and its driver
} compositor_dirty_none = {-1, {0}};

static void compositor_dirty(unsigned int fb, const unsigned short (*clip)[4], unsigned int clips)
{
        struct compositor_dirty dirty = {fb, 0, 0, clips, (unsigned long)clip};

        if (!clips || compositor.card < 0 ||
            (compositor.card == compositor_dirty_none.card && !memory_compare(compositor.driver, compositor_dirty_none.driver, sizeof(compositor_dirty_none.driver))))
                return;
        if (system_control(compositor.card, COMPOSITOR_DIRTYFB, address_of dirty) == -38)
        {
                compositor_dirty_none.card = compositor.card;
                memory_copy(compositor_dirty_none.driver, compositor.driver, sizeof(compositor_dirty_none.driver));
        }
}

static int compositor_flip(unsigned int fb, int row, unsigned int which)
{
        struct
        {
                unsigned int crtc_id, fb_id, flags, reserved;
                unsigned long user_data;
        } flip = {compositor.crtc, fb, COMPOSITOR_FLIP_EVENT, 0, 0};

        if (row >= 0 && compositor_wl_row_async(row))
                flip.flags |= COMPOSITOR_FLIP_ASYNC;
        // Headless: the file's names are exchanged now, so the flip is done at once (only the compositor's own buffers flip).
        if (compositor.card < 0)
        {
                if (row >= 0 || fb != compositor.fb || !compositor_spare.fb || !compositor_headless_flip())
                        return 0;
                compositor.on_screen = fb;
                compositor_flip_done();
                return 1;
        }
        flip.user_data = ++compositor.flip_serial;
        if (system_control(compositor.card, COMPOSITOR_PAGE_FLIP, address_of flip) != 0)
        {
                if (!(flip.flags & COMPOSITOR_FLIP_ASYNC))
                        return 0;
                flip.flags &= ~COMPOSITOR_FLIP_ASYNC;
                if (system_control(compositor.card, COMPOSITOR_PAGE_FLIP, address_of flip) != 0)
                        return 0;
        }
        compositor_flip_refused = 0;
        compositor.flip_pending = 1;
        compositor.vrr_kick = 0; // the pointer's place goes up with this flip
        compositor.flip_async = (flip.flags & COMPOSITOR_FLIP_ASYNC) != 0;
        if (compositor.flip_async && compositor.table)
                compositor.table->flips_async++;
        compositor.flip_issued = clock_monotonic_nanoseconds();
        compositor.flip_row = row;
        compositor.flip_buffer = which;
        compositor.on_screen = fb;
        return 1;
}

// The pictures held back are said taken: for the row the flip showed, once it shows that row's newest buffer.
#ifdef CONFIG_MOONWATER_WAYLAND
static void compositor_wl_flip_presented(void); // wayland_host.c: the feedbacks a drawn frame took, answered at its flip
static void compositor_wl_graves_sweep(void);   // wayland_host.c: the client framebuffers the card was still on, removed once it is off them
static void compositor_wl_frame(void);           // wayland_host.c: a frame drawn: callbacks answered, buffers given back
static int compositor_wl_frame_owed;             // a frame shown straight whose callbacks wait for its flip (compositor.c's loop)
static void compositor_wl_dmabuf_refresh(void); // wayland_host.c: re-probe render node after late GPU init or drm uevents
static int compositor_wl_dmabuf_missing(void);
#endif

static void compositor_flip_done(void)
{
        compositor_record_vblank(clock_monotonic_nanoseconds());
        compositor_keytrace_shown(compositor.flip_stamped ? compositor.flip_when : clock_monotonic_nanoseconds());
        compositor_frametrace_note('V', -1, compositor.flip_stamped ? compositor.flip_when : clock_monotonic_nanoseconds());
        compositor.flip_pending = 0;
        compositor.leaving_row = -1;
        for (int row = 0; row < (int)compositor_rows_in_use; row++)
        {
                if (!rows_has(&compositor.held_rows, row))
                        continue;
                if (row == compositor.flip_row && compositor.front[row] != compositor.flip_buffer)
                        continue;
                if (rows_has(&compositor_card.list_rows, row)) // a list's job draws its picture: the loop's turn gives it back at the job's end
                        continue;
                __atomic_store_n(&compositor.table->shown[row], compositor.taken_held[row], __ATOMIC_RELEASE);
                rows_del(&compositor.held_rows, row);
                rows_add(&compositor.woken, row);
        }
        // A window that left while the card showed it: its framebuffers now, the card being off them.
        for (int row = 0; row < (int)compositor_rows_in_use; row++)
                if (rows_has(&compositor.dropping, row) && !compositor_shows_row(row))
                {
                        rows_del(&compositor.dropping, row);
                        compositor_window_fb_drop(row);
                        compositor_row_reclaim(row);
                }
#ifdef CONFIG_MOONWATER_WAYLAND
        compositor_wl_graves_sweep();
        // The feedbacks of the frame this flip showed are answered with the card's time for it.
        compositor_wl_flip_presented();
        if (compositor_wl_frame_owed)
        {
                compositor_wl_frame_owed = 0;
                compositor_wl_frame();
        }
#endif
}

/*
        A flip the card has not completed is waited for, not tried against. A present that finds one pending composes nothing (it cannot flip a second) and has
        used up its period (compositor.presented is its start), so the frame it was to draw came a period later. The loop's timer and the card's vblank are two clocks,
        and each time the timer fell in the stretch before a vblank that the compose and the flip take, the flip landed on the vblank after it and the next present
        found it pending: a steady animation had a frame a vblank late every few dozen frames. The card's event wakes the loop and the present is made then; the wait
        is for COMPOSITOR_FLIP_PERIODS periods at the most, for an event that never comes (a screen that is off). The deadline, or 0 when nothing is waited for.
*/
#define COMPOSITOR_FLIP_PERIODS 4
static positive compositor_flip_wait(positive now);

/*
        The time a frame made at `stamp` is dated, which the next one is due a period after: the time this one was due (a period
        after the last) when it is made within a period of that, and `stamp` itself for the first frame, one made early, and one
        a period or more late (nothing was owed in between: the count starts again from now). Dated by the clock as it was read,
        every frame was late by the time the loop took to wake and each lateness was kept, so a client drawing on its frame
        callbacks with no card's flip to pace it had them 16.8 ms apart on a 16.667 ms screen, 59.5 frames a second and a frame
        of drift every two seconds; dated when it was due, the latenesses do not add up.
*/
static positive compositor_frame_dated(positive last, positive period, positive stamp)
{
        positive due = last + period;

        return last && period && stamp >= due && stamp - due < period ? due : stamp;
}

/*
        When the next present is due, and a present late in the period.

        A frame's picture is the hand's as it was when the present began, and it is on the screen at the vblank after its flip
        is issued. A present made the moment the last flip landed has the whole period to compose in and shows input a period
        old, or older; one made as late as still makes the vblank shows input as old as its composing takes. So, where the
        card says when its vblank was (the flip event's stamp) and that was within the last period, the next present is due
        `lead` before the vblank after it, and at once when that time has gone by; `lead` is what a present has lately taken
        and some to spare (compositor_present_lead). Never sooner than half a period after the last present, so presents
        that flip nothing are not made over and over. With no lead, no stamp, a stamp ahead of the clock or one a period
        old (a headless compositor, an idle desktop, a card that stamps nothing), the rule is the one there was: a period
        after the last present, or at once when that has gone by.

        It is the rule wherever a card stamps its vblanks, and the machine's owner turns it off with the file
        /run/moonwater/present-early (looked for at the start): every present is then made by the old rule. The lead is
        measured, not guessed: the worst of the last COMPOSITOR_LEAD_FRAMES presents, each on the path it took (the card's
        or the processor's, compositor_metric_card and compositor_metric_processor: a present's whole time, the card's job
        in it), half as much again and a millisecond; a lead that does not fit in the period is none, the old
        rule (a 4K frame whose job takes longer than a period is presented a period after the last, not at the start of
        every period with a lead that grows). What a card and its driver make of a flip issued a few milliseconds before
        the vblank is the card's, so each flip is checked: one that missed its vblank (its stamp half a period or more past
        the vblank it was for) is a frame a period late, and widens the lead at once, for the very next present, by a
        quarter of a period; a second miss while it is widened is a machine late presents do not suit, and the lead is the
        whole period, which is none: the old rule. A calm (flips in a row that landed, COMPOSITOR_LEAD_CALM at first)
        steps back, from the old rule to the quarter and from the quarter to nothing; every fall to the old rule doubles
        the calm the next step back waits for, up to COMPOSITOR_LEAD_CALM_MOST, so a machine on which every late present
        misses tries one about every two minutes, and a step back to nothing starts the calm again from the first. The
        presents made late and the misses are counted and said as the compositor ends, and the misses are what says the
        rule holds.
*/
#define COMPOSITOR_LEAD_FRAMES 32
#define COMPOSITOR_LEAD_SPARE 1000000ull
#define COMPOSITOR_LEAD_CALM 120
#define COMPOSITOR_LEAD_CALM_MOST (64 * COMPOSITOR_LEAD_CALM)
static struct
{
        int early;              // /run/moonwater/present-early was there at the start: every present by the old rule
        unsigned int calm;      // flips in a row that landed (late or not) since the lead was last widened or stepped back
        unsigned int calm_need; // the calm the next step back waits for (0: COMPOSITOR_LEAD_CALM)
        positive widen;         // what the misses added to the lead: 0, a quarter of a period, or the period (the old rule)
        positive aim;           // the vblank the last late present was for (0: it was not late)
        positive made, missed;  // presents made late in the period, and those whose flip landed a vblank after its aim
} compositor_late;

static positive compositor_present_due(positive presented, positive period, positive vblank, positive lead, positive now)
{
        positive due = now - presented < period ? presented + period : now, late, least;

        if (!lead || !period || lead >= period || !vblank || now < vblank || now - vblank >= period)
                return due;
        late = vblank + period - lead;
        late = now > late ? now : late;
        least = presented + period / 2;
        return late > least ? late : least;
}

// What a present is given before its vblank: the longest of the last presents timed (worst), half as much again, a millisecond, and what
// misses widened it by. 0 (the old rule) when that does not fit in the period, with fewer than four presents timed, or with no period.
static positive compositor_lead_of(positive worst, unsigned int timed, positive widen, positive period)
{
        positive lead = worst + worst / 2 + COMPOSITOR_LEAD_SPARE + widen;

        if (timed < 4 || !period || widen >= period || lead >= period)
                return 0;
        return lead;
}

static positive compositor_present_lead(void); // after the metric rings it reads (compositor_metric_note)

// A present is about to be made at `stamp`: whether it is one late in its period, and the vblank it is for.
static void compositor_late_begin(positive lead, positive stamp)
{
        positive vblank = compositor.flip_when;

        compositor_late.aim = 0;
        if (lead && compositor.period && lead < compositor.period && vblank && stamp >= vblank && stamp - vblank < compositor.period)
        {
                compositor_late.aim = vblank + compositor.period;
                compositor_late.made++;
        }
}

// A flip landed at the vblank `when`: one that was for the vblank before is a frame late, and widens the lead at once, to a quarter of a period and
// then to the period (the old rule, the next calm twice as long); a calm of flips that landed steps it back (compositor_late, above).
static void compositor_late_landed(positive when)
{
        unsigned int need = compositor_late.calm_need ? compositor_late.calm_need : COMPOSITOR_LEAD_CALM;

        if (compositor_late.aim && when > compositor_late.aim && when - compositor_late.aim >= compositor.period / 2)
        {
                compositor_late.missed++;
                compositor_late.calm = 0;
                if (!compositor_late.widen)
                        compositor_late.widen = compositor.period / 4;
                else
                        compositor_late.widen = compositor.period, compositor_late.calm_need = need * 2 < COMPOSITOR_LEAD_CALM_MOST ? need * 2 : COMPOSITOR_LEAD_CALM_MOST;
        }
        else if (compositor_late.widen && ++compositor_late.calm >= need)
        {
                compositor_late.calm = 0;
                if (compositor_late.widen >= compositor.period)
                        compositor_late.widen = compositor.period / 4;
                else
                        compositor_late.widen = 0, compositor_late.calm_need = 0;
        }
        compositor_late.aim = 0;
}

static positive compositor_flip_wait(positive now)
{
        positive period = compositor.period ? compositor.period : 16666667ull, last = compositor.flip_issued + COMPOSITOR_FLIP_PERIODS * period;

        return compositor.flip_pending && now < last ? last : 0;
}

/*
        A flip whose event never comes is taken as landed. The wait above ends after four periods, but the flip stayed pending for good: a present that
        finds one pending draws nothing, and a retake of the screens waits for it too, so one event the card lost (the driver's "flip_done timed out")
        was a screen that took input and drew nothing until the session ended. Past a second, sixty periods of a 60 Hz screen, no flip is still coming:
        it is let go of as its event would have let go of it, and the whole space is drawn again. A card that had not finished refuses the next flip
        (EBUSY): the frame is then composed again a period on, never sooner (the loop presents once a period), and the third refusal in a row sets the
        crtc with a mode set (compositor_present), so a card that stays busy costs a compose a period and no more. The event of the flip given up on,
        if it comes after all, is not the next flip's (compositor_card_events).
*/
#define COMPOSITOR_FLIP_LOST 1000000000ull
static int compositor_flip_lost(positive now)
{
        return compositor.flip_pending && now >= compositor.flip_issued && now - compositor.flip_issued >= COMPOSITOR_FLIP_LOST;
}

/*
        The card's events, as read: a flip's completion is the one asked for, and it is the flip in flight's only when it carries that
        flip's number (flip_serial, the user data it was asked with). A flip taken as landed after a second (compositor_flip_lost) may
        still send its event, late; taken for the next flip's, every flip after it was let go of when the one before it landed, a
        window's buffer the card still showed was said free to its program, and the card refused each present that followed. Such an
        event is passed over, and so is one that comes with no flip in flight. The count of flips it let go of.
*/
/*
        The period the card's vblanks measure: the time between the stamps of two flips over the vblanks between them, so a flip
        that missed one still measures one, eased an eighth at a time into the table's period_seen for moonwater debug. A pair
        a mode set came between (the vblank count gone back, or more than a second of vblanks) measures nothing; a take of the
        screens begins it again (compositor_vblank_last.when 0).
*/
static struct
{
        positive when;
        unsigned int sequence;
} compositor_vblank_last;

static void compositor_vblank_measure(positive when, unsigned int sequence)
{
        unsigned int vblanks = sequence - compositor_vblank_last.sequence;

        if (compositor_vblank_last.when && when > compositor_vblank_last.when && sequence > compositor_vblank_last.sequence && vblanks <= 1000 &&
            compositor.table)
        {
                positive seen = (when - compositor_vblank_last.when) / vblanks, was = compositor.table->period_seen;

                if (seen < 1000000000ull)
                        compositor.table->period_seen = (unsigned int)(was ? (was * 7 + seen) / 8 : seen);
        }
        compositor_vblank_last.when = when, compositor_vblank_last.sequence = sequence;
}

static unsigned int compositor_card_events(const p8 *events, long got)
{
        unsigned int landed = 0;

        for (long at = 0; at + 8 <= got;)
        {
                unsigned int type, length;

                memory_copy(address_of type, events + at, 4);
                memory_copy(address_of length, events + at + 4, 4);
                if (length < 8 || at + (long)length > got)
                        break;
                if (type == COMPOSITOR_EVENT_FLIP_COMPLETE)
                {
                        // The card's stamp of the vblank: struct drm_event_vblank is user data, then tv_sec, tv_usec and sequence.
                        unsigned int sec, usec, sequence;
                        positive serial = 0;

                        if (length >= 16)
                                memory_copy(address_of serial, events + at + 8, 8);
                        if (!compositor.flip_pending || serial != compositor.flip_serial)
                        {
                                at += length;
                                continue;
                        }
                        landed++;
                        /*
                                An asynchronous flip (a window that allows tearing, shown straight) lands mid-frame, and its event's stamp is
                                the vblank before it, or the flip's own time on another card: neither the vblank the presents are paced by
                                (compositor_present_due, compositor_late) nor when the picture came on the screen, which is now. Its stamp
                                is passed over, so flip_when stays the last vblank a flip landed on, and the flip is said shown by the clock.
                        */
                        compositor.flip_tore = compositor.flip_async;
                        compositor.flip_async = 0;
                        if (length >= 28 && !compositor.flip_tore)
                        {
                                memory_copy(address_of sec, events + at + 16, 4);
                                memory_copy(address_of usec, events + at + 20, 4);
                                memory_copy(address_of sequence, events + at + 24, 4);
                                compositor.flip_when = (positive)sec * 1000000000ull + (positive)usec * 1000ull;
                                compositor.flip_seq = sequence;
                                compositor.flip_stamped = 1;
                                compositor_vblank_measure(compositor.flip_when, sequence);
                                compositor_late_landed(compositor.flip_when);
                        }
                        compositor_flip_done();
                        compositor.flip_stamped = 0;
                }
                at += length;
        }
        return landed;
}

// The card's events, read when its descriptor is readable.
static void compositor_card_read(void)
{
        p8 events[512];
        long got = compositor_call(syscall(read), (positive)compositor.card, (positive)events, sizeof(events), 0, 0, 0);

        compositor_card_events(events, got);
}

// A row's buffer which, from the compositor's own copy of the layout: the table is shared, so the header's pixels,
// buffer and stride are a client's to write, and the compositor draws through what it set itself.
static unsigned int *compositor_buffer(int row, unsigned int which)
{
        return (unsigned int *)((char *)compositor.table + compositor.pixels +
                                ((unsigned long)row * 2 + (which & 1u)) * compositor.buffer);
}

// What the tabs covered of the buffer the card shows, put back, so what is made from it (the glass) is the window's.
static void compositor_direct_unpatch_at(int row, unsigned int which)
{
        const int *rect = compositor.under_rect[which];
        unsigned int *pixels;

        if (row < 0 || !compositor.patched[row][which] || !compositor.under[which])
                return;
        compositor_runs_forget(row);
        pixels = compositor_buffer(row, which);
        memory_copy_rect_u32(pixels + (unsigned long)rect[1] * (unsigned long)compositor_stride() + (unsigned long)rect[0],
                             (unsigned long)compositor_stride(),
                             compositor.under[which],
                             (unsigned long)(rect[2] - rect[0]),
                             (unsigned long)(rect[2] - rect[0]),
                             (unsigned long)(rect[3] - rect[1]));
        compositor.patched[row][which] = 0;
}

static void compositor_direct_unpatch(void)
{
        if (compositor.direct_row >= 0)
                compositor_direct_unpatch_at(compositor.direct_row, compositor.front[compositor.direct_row] & 1u);
}

static unsigned int *compositor_direct_stage;
static positive compositor_direct_stage_bytes;

static unsigned int *compositor_direct_stage_ready(void)
{
        positive need = (positive)compositor_stride() * (positive)compositor.height * 4;

        if (!need)
                return 0;
        if (compositor_direct_stage && compositor_direct_stage_bytes >= need)
                return compositor_direct_stage;
        if (compositor_direct_stage)
        {
                compositor_call(syscall(munmap), (positive)compositor_direct_stage, compositor_direct_stage_bytes, 0, 0, 0, 0);
                compositor_direct_stage = 0, compositor_direct_stage_bytes = 0;
        }
        {
                long mapped = compositor_call(syscall(mmap), 0, need, 3, 0x22, (positive)-1, 0);

                if (mapped < 0 && mapped > -4096)
                        return 0;
                compositor_direct_stage = (unsigned int *)mapped, compositor_direct_stage_bytes = need;
        }
        return compositor_direct_stage;
}

// The tabs written over a row's buffer, what they covered kept, and the program told the rectangle.
static int compositor_tabs_rect(const struct compositor_frame *tabs, unsigned int count, int rect[4]);

/*
        What the tabs covered of a buffer shown straight is kept in under[which], a rectangle of the tabs' size in rows of its own width. It was mapped
        once, at the first patch, the size of the screen then, and never again: a space taken again at another size (a game's mode, a screen of
        another size plugged in) can ask it to hold a band of more pixels than that screen had (an 800 by 600 start, then a 7680-wide band of 64
        rows), and the copy into it ran past its end. It is the size the rectangle asks now (the screen's at the least), made again when it is too
        small, with what it held.
*/
static positive compositor_under_bytes[2];

static b32 compositor_direct_under_ready(unsigned int which, positive need)
{
        positive bytes = (positive)compositor.width * (positive)compositor.height * 4;
        long mapped;

        if (compositor.under[which] && compositor_under_bytes[which] >= need)
                return true;
        bytes = ((need > bytes ? need : bytes) + 4095) & ~4095ull;
        mapped = compositor_call(syscall(mmap), 0, bytes, 3, 0x22, (positive)-1, 0);
        if (mapped < 0 && mapped > -4096)
                return false;
        if (compositor.under[which])
        {
                memory_copy((void *)mapped, compositor.under[which], compositor_under_bytes[which]);
                compositor_call(syscall(munmap), (positive)compositor.under[which], compositor_under_bytes[which], 0, 0, 0, 0);
        }
        compositor.under[which] = (unsigned int *)mapped, compositor_under_bytes[which] = bytes;
        return true;
}

static void compositor_direct_patch(int row, unsigned int which, const struct compositor_frame *tabs, unsigned int count)
{
        int rect[4];
        unsigned int *pixels = compositor_buffer(row, which);
        unsigned long stride = (unsigned long)compositor_stride();
        struct windows *table = compositor.table;

        compositor_runs_forget(row);
        if (!compositor_tabs_rect(tabs, count, rect))
        {
                if (compositor.patched[row][which])
                        compositor_direct_unpatch_at(row, which);
                return;
        }

        if (!compositor_direct_under_ready(which, 4ull * (positive)(rect[2] - rect[0]) * (positive)(rect[3] - rect[1])))
                return;
        // When updating tabs on an already-patched live on-screen buffer, stage the clean client pixels + new tabs off-screen
        // and write only finished pixels so the scanout never shows an unpatched frame.
        if (compositor.patched[row][which])
        {
                unsigned int *stage = compositor_direct_stage_ready();

                if (stage)
                {
                        const int *old = compositor.under_rect[which];
                        int u[4];

                        compositor_joined(old, rect, u);

                        memory_copy_rect_u32(stage + (unsigned long)u[1] * stride + (unsigned long)u[0], stride,
                                             pixels + (unsigned long)u[1] * stride + (unsigned long)u[0], stride,
                                             (unsigned long)(u[2] - u[0]), (unsigned long)(u[3] - u[1]));
                        memory_copy_rect_u32(stage + (unsigned long)old[1] * stride + (unsigned long)old[0], stride,
                                             compositor.under[which], (unsigned long)(old[2] - old[0]),
                                             (unsigned long)(old[2] - old[0]), (unsigned long)(old[3] - old[1]));
                        memory_copy_rect_u32(compositor.under[which], (unsigned long)(rect[2] - rect[0]),
                                             stage + (unsigned long)rect[1] * stride + (unsigned long)rect[0], stride,
                                             (unsigned long)(rect[2] - rect[0]), (unsigned long)(rect[3] - rect[1]));
                        memory_copy(compositor.under_rect[which], rect, sizeof(rect));
                        {
                                struct compositor_target target = {stage, stride, compositor.width, compositor.height,
                                                                   rect[0], rect[1], rect[2], rect[3]};

                                for (unsigned int at = 0; at < count; at++)
                                        compositor_draw(&target, &tabs[at]);
                        }
                        memory_copy_rect_u32(pixels + (unsigned long)u[1] * stride + (unsigned long)u[0], stride,
                                             stage + (unsigned long)u[1] * stride + (unsigned long)u[0], stride,
                                             (unsigned long)(u[2] - u[0]), (unsigned long)(u[3] - u[1]));
                        table->spoiled_x[row][which] = rect[0];
                        table->spoiled_y[row][which] = rect[1];
                        table->spoiled_width[row][which] = rect[2] - rect[0];
                        __atomic_store_n(&table->spoiled_height[row][which], rect[3] - rect[1], __ATOMIC_RELEASE);
                        return;
                }
                compositor_direct_unpatch_at(row, which);
        }
        memory_copy_rect_u32(compositor.under[which],
                             (unsigned long)(rect[2] - rect[0]),
                             pixels + (unsigned long)rect[1] * stride + (unsigned long)rect[0],
                             stride,
                             (unsigned long)(rect[2] - rect[0]),
                             (unsigned long)(rect[3] - rect[1]));
        memory_copy(compositor.under_rect[which], rect, sizeof(rect));

        struct compositor_target target = {pixels, stride, compositor.width, compositor.height,
                                           rect[0], rect[1], rect[2], rect[3]};

        for (unsigned int at = 0; at < count; at++)
                compositor_draw(&target, &tabs[at]);
        compositor.patched[row][which] = 1;
        table->spoiled_x[row][which] = rect[0];
        table->spoiled_y[row][which] = rect[1];
        table->spoiled_width[row][which] = rect[2] - rect[0];
        __atomic_store_n(&table->spoiled_height[row][which], rect[3] - rect[1], __ATOMIC_RELEASE);
}

// The window the card can show straight: the top one, opaque, square and filling the output at the size it drew, with
// nothing over it but the tabs, a pointer on its own plane or none (hidden), and a framebuffer of its buffer; -1 when there is none.
static int compositor_direct_candidate(const struct compositor_frame *frames, const unsigned int *rows, unsigned int windows)
{
        int row;

        // On more than one screen every frame is composed: a flip, the overlay and the window's framebuffer are one
        // crtc's, and the scanout is the whole space.
        // A pointer drawn into the scene is over the window, unless it is hidden (a game's, which asked for none): then nothing is.
        if (compositor.card < 0 || (compositor_cursor_in_scene() && compositor.cursor.drawn) || !windows ||
            compositor.drag >= 0 || compositor.resize >= 0 || compositor.preview_draw || compositor_output_count() > 1 ||
            compositor_spots_any(0))
                return -1;
        row = rows[windows - 1];
        if (!compositor_client_scans(row) && (compositor.direct_refused || !compositor.table_memfd))
                return -1;
        if (frames[windows - 1].blend)
                return -1;
        if (!compositor_client_scans(row) && __atomic_load_n(&compositor.table->list_mode, __ATOMIC_ACQUIRE))
                return -1;
        // Glass is the window's WINDOW_GLASS (compositor.glassy): the row's other flags (a client's NO_CLOSE and NO_GRAB) are no glass.
        if (compositor.glassy[row] || compositor.landing[row] || compositor.glide[row] || compositor.x[row] != 0 || compositor.y[row] != 0 ||
            compositor.w[row] != (unsigned int)compositor.width || compositor.h[row] != (unsigned int)compositor.height ||
            compositor.picture_w[row] != compositor.w[row] || compositor.picture_h[row] != compositor.h[row] ||
            compositor.asking[row] || compositor_radius(&frames[windows - 1]) || frames[windows - 1].line)
                return -1;
        return (compositor_client_scans(row) ? compositor_lent_fb(row) : compositor_window_fb(row, compositor.front[row])) ? row : -1;
}

// The frame of a window shown straight: the tabs over its buffer, and the card flipped to it. False when the card will not.
static b32 compositor_present_direct(int row, const struct compositor_frame *tabs, unsigned int count)
{
        int lent = compositor_client_scans(row), rect[4];
        unsigned int which = compositor.front[row];
        unsigned int fb = lent ? compositor_lent_fb(row) : compositor_window_fb(row, which);
        unsigned short clip[COMPOSITOR_DAMAGE][4];

        if (lent && compositor_tabs_rect(tabs, count, rect))
                return 0;
        if (!fb)
                return 0;
        if (compositor.on_screen != fb && compositor.flip_pending)
                return 1;
        if (!lent)
                compositor_direct_patch(row, which, tabs, count);
        if (compositor.on_screen != fb)
        {
                if (!compositor_flip(fb, row, which))
                {
                        if (lent)
                        {
                                compositor_lent_fb_fail(row);
                                return 0;
                        }
                        compositor_direct_unpatch_at(row, which);
                        compositor.direct_refused = 1;
                        compositor.table->refused = 5;
                        return 0;
                }
        }
        compositor.direct_row = row;
        compositor.table->scanned++;

        // A virtual card copies to its host only what it is told changed.
        for (unsigned int at = 0; at < compositor.damages; at++)
                for (unsigned int side = 0; side < 4; side++)
                        clip[at][side] = (unsigned short)compositor.damage[at][side];
        compositor_dirty(fb, (const unsigned short (*)[4])clip, compositor.damages);
        compositor.damages = 0;
        compositor.screen_stale = 1;
        return 1;
}

/*
        A slice of the scene composed apart, stored to the card: the software
        pointer over it, then each row by memory_copy_changed_u32 against
        the mirror, which stores only the 64-byte chunks that differ from
        what the card holds, so the card is written (and a virtual card's
        host told) only where the frame changed, however large the damage.
        A stale mirror is stored whole.
*/
static void compositor_slice_flush(const struct compositor_target *slice, int *first, int *last)
{
        unsigned long width = (unsigned long)(slice->x2 - slice->x1);
        unsigned long pitch = compositor.scanout_pitch / 4;

        *first = *last = -1;
        // The fingers' marks over the windows and under the pointer.
        compositor_spots_draw(slice);
        if (compositor.cursor.drawn)
                compositor_cursor_draw(slice, compositor.cursor.x, compositor.cursor.y, &compositor.cursor.drawn_as, compositor_cursor_ink(0, 0),
                                       compositor_cursor_ink(1, 0));
        for (int y = slice->y1; y < slice->y2; y++)
        {
                const unsigned int *from = slice->pixels + (unsigned long)(y - slice->oy) * slice->pitch +
                                           (unsigned long)(slice->x1 - slice->ox);
                unsigned int *into = compositor.scanout + (unsigned long)y * pitch + (unsigned long)slice->x1;
                unsigned int *mirror = compositor.mirror + (unsigned long)y * compositor.mirror_pitch + (unsigned long)slice->x1;

                if (compositor.mirror_stale)
                {
                        memory_copy(into, from, width * 4);
                        if (mirror != into)
                                memory_copy(mirror, from, width * 4);
                }
                else if (!memory_copy_changed_u32(into, mirror, from, width))
                        continue;
                *first = *first < 0 ? y : *first;
                *last = y;
        }
}

// Whether the card's driver is one of count names.
static int compositor_driver_in(const char *const *names, unsigned int count)
{
        for (unsigned int at = 0; at < count; at++)
                if (string_equals((string_address)names[at], (string_address)compositor.driver))
                        return 1;
        return 0;
}

/*
        Whether the scene is composed apart and stored to the card only where
        it changed (compositor_slice_flush), decided once by the card's driver.
        On where a store to the card is dear: a card with memory of its own
        mapped across the bus (amdgpu, radeon, nouveau, and the emulated
        cards' VRAM), and a card whose framebuffer is the machine's memory
        but which copies what it is told changed to the device or its host
        (virtio_gpu, simpledrm and the like), where the mirror is the
        scanout itself, read at the machine's speed. Off on i915 and xe,
        whose dumb buffer is the machine's memory and shown as it is, where
        composing apart is only one more pass, and with no card. The cost
        is a slice of rows a band (COMPOSITOR_SLICE), and the mirror's memory
        where it is apart.
*/
static int compositor_turning_software(void);

// What compositor_changed_ready mapped, given back; it is decided again at the next present.
static void compositor_apart_drop(void)
{
        if (compositor.composing)
                gpu_forget((const void *)(compositor.composing), (positive)(compositor.composing_bytes)), compositor_call(syscall(munmap), (positive)compositor.composing, compositor.composing_bytes, 0, 0, 0, 0);
        if (compositor.mirror && compositor.mirror_bytes)
                gpu_forget((const void *)(compositor.mirror), (positive)(compositor.mirror_bytes)), compositor_call(syscall(munmap), (positive)compositor.mirror, compositor.mirror_bytes, 0, 0, 0, 0);
        compositor.composing = compositor.mirror = 0;
        compositor.composing_bytes = compositor.mirror_bytes = 0;
        compositor.changed = 0;
}

/*
        One way to the screen for each kind of card. The two ways are not one done twice: what each is for is a kind of card's.

        A card that scans out what its buffer holds (a card with memory of its own, amdgpu, radeon, nouveau, the emulated cards' VRAM, and i915 and xe,
        whose buffer is the machine's memory scanned as it is) shows a buffer while it is being written: composed in place, a frame tears at the line the
        write has reached. There the frame goes into the second plate and the screen is flipped to it (compositor_plate_swap, compositor_flip), with one
        screen, a window on the overlay or not (what is under the plane is not composed, and a new buffer of that window goes with the plate's flip,
        compositor_plane_carry); with two screens or a screen turned it is composed in place, apart (compositor_slice_flush, with the mirror) where a
        store to the card is dear, as it was before the plates.

        A card that copies to its device or its host what it is told changed, from a buffer in the machine's memory (compositor_copies_damage: virtio_gpu
        and the other guests' cards that keep one, simpledrm, the server boards' ast and mgag200, the USB displays udl and gud) shows nothing of the buffer
        until it is told: the frame composed in place and told by DIRTYFB is copied whole and only where it changed, so it has no torn frame of its own.
        A flip there tells the driver no damage, and the driver copies the whole screen for it (drm_atomic_helper_damage_merged's full update): a second
        plate would cost every frame the whole screen across to the host or the device, 33 MB a frame at 4K, for nothing. So it has none, and is composed
        apart (the mirror is the scanout itself, read at the machine's speed), told only the rows that changed.
*/
static const char *const compositor_damage_copied[] = {"virtio_gpu", "simpledrm", "cirrus-qemu", "hyperv_drm", "ast", "mgag200", "udl", "gud"};

static int compositor_copies_damage(void)
{
        return compositor.card >= 0 && compositor_driver_in(compositor_damage_copied, sizeof(compositor_damage_copied) / sizeof(compositor_damage_copied[0]));
}

// Whether this present composes into the second plate and flips to it: one screen, no screen turned, and a card that shows
// what its buffer holds (a headless scan image's names are exchanged the same way); never one that copies its damage.
static int compositor_plate_wanted(int turning)
{
        return !turning && compositor_output_count() < 2 && !compositor_copies_damage();
}

static int compositor_changed_ready(void)
{
        static const char *const apart[] = {"amdgpu", "radeon", "nouveau", "bochs-drm", "qxl", "vmwgfx"};
        const char *const *shared = compositor_damage_copied;
        unsigned long bytes = (unsigned long)compositor.width * (unsigned long)compositor.height * 4;
        unsigned long slices = (unsigned long)compositor.width * COMPOSITOR_SLICE * COMPOSITOR_BANDS * 4;
        int own, listed, turned = compositor_turning_software();

        if (compositor.changed && compositor.changed_turned == turned)
                return compositor.changed > 0;
        compositor_apart_drop();
        compositor.changed = -1;
        compositor.changed_turned = turned;
        if (compositor.card < 0)
                return 0;
        own = compositor_driver_in(apart, sizeof(apart) / sizeof(apart[0]));
        listed = own || compositor_driver_in(shared, sizeof(compositor_damage_copied) / sizeof(compositor_damage_copied[0]));
        // A screen Canvas turns reads the scene whole, a frame at a time, and a read of the card's memory is more than a
        // frame can pay (a dumb buffer is mapped write-combined on most cards, and word loads from one came to about ten
        // megabytes a second on a real card): with a screen turned, the scene is kept in the machine's memory whatever the
        // card, as the apart cards' is.
        if (!listed && !turned)
                return 0;
        own = own || !listed;

        long composing = compositor_call(syscall(mmap), 0, slices, 3, 0x22, (positive)-1, 0);

        if (composing < 0 && composing > -4096)
                return 0;
        compositor.composing = (unsigned int *)composing;
        compositor.composing_bytes = slices;
        compositor.mirror = compositor.scanout;
        compositor.mirror_bytes = 0;
        compositor.mirror_pitch = compositor.scanout_pitch / 4;
        if (own)
        {
                long mirror = compositor_call(syscall(mmap), 0, bytes, 3, 0x22, (positive)-1, 0);

                if (mirror < 0 && mirror > -4096)
                {
                        gpu_forget((const void *)(composing), (positive)(slices)), compositor_call(syscall(munmap), (positive)composing, slices, 0, 0, 0, 0);
                        compositor.composing = 0;
                        return 0;
                }
                compositor.mirror = (unsigned int *)mirror;
                compositor.mirror_bytes = bytes;
                compositor.mirror_pitch = (unsigned long)compositor.width;
                compositor.mirror_stale = 1;
                compositor_damage(0, 0, compositor.width, compositor.height);
        }
        compositor.changed = 1;
        return 1;
}

/*
        The tabs over a window shown straight, on a plane of their own where
        the card has an overlay: the band's rectangle composed (the window
        under it, the glass, the tabs) into a buffer of the card's, and the
        window's buffer and that one given to the card in one atomic commit,
        which does not wait for the vertical blank and whose completion is the
        flip's event. The window's buffer is then never written: nothing it
        drew is spoiled and nothing is kept to put back. A card with no
        overlay that takes XRGB8888 on this crtc, or one that refuses the
        commit, has the tabs patched in as above.
*/
#define COMPOSITOR_SET_CLIENT_CAP 0x4010640du
#define COMPOSITOR_GETPLANERESOURCES COMPOSITOR_DRM(0xb5u, 16u)
#define COMPOSITOR_GETPLANE COMPOSITOR_DRM(0xb6u, 32u)
#define COMPOSITOR_SETPLANE COMPOSITOR_DRM(0xb7u, 48u)
#define COMPOSITOR_GETPROPERTY COMPOSITOR_DRM(0xaau, 64u)
#define COMPOSITOR_OBJ_GETPROPERTIES COMPOSITOR_DRM(0xb9u, 32u)
#define COMPOSITOR_ATOMIC COMPOSITOR_DRM(0xbcu, 56u)
#define COMPOSITOR_OBJECT_PLANE 0xeeeeeeeeu
#define COMPOSITOR_ATOMIC_NONBLOCK 0x0200u

enum
{
        COMPOSITOR_PLANE_FB,
        COMPOSITOR_PLANE_CRTC,
        COMPOSITOR_PLANE_SRC_X,
        COMPOSITOR_PLANE_SRC_Y,
        COMPOSITOR_PLANE_SRC_W,
        COMPOSITOR_PLANE_SRC_H,
        COMPOSITOR_PLANE_CRTC_X,
        COMPOSITOR_PLANE_CRTC_Y,
        COMPOSITOR_PLANE_CRTC_W,
        COMPOSITOR_PLANE_CRTC_H,
        COMPOSITOR_PLANE_TYPE,
        COMPOSITOR_PLANE_PROPS
};

// A plane's property ids by name and the value of its type (0 overlay, 1 primary, 2 cursor); false when one is missing.
static int compositor_plane_props(unsigned int plane, unsigned int props[COMPOSITOR_PLANE_PROPS], unsigned long *type)
{
        static const char *const names[COMPOSITOR_PLANE_PROPS] = {"FB_ID",  "CRTC_ID", "SRC_X",  "SRC_Y",
                                                                   "SRC_W",  "SRC_H",   "CRTC_X", "CRTC_Y",
                                                                   "CRTC_W", "CRTC_H",  "type"};
        unsigned int ids[64];
        unsigned long values[64];
        struct
        {
                unsigned long props, values;
                unsigned int count, object, type;
        } got = {(unsigned long)ids, (unsigned long)values, 64, plane, COMPOSITOR_OBJECT_PLANE};

        if (system_control(compositor.card, COMPOSITOR_OBJ_GETPROPERTIES, address_of got) != 0 || got.count > 64)
                return 0;
        memory_zero(props, sizeof(unsigned int) * COMPOSITOR_PLANE_PROPS);
        for (unsigned int at = 0; at < got.count; at++)
        {
                struct
                {
                        unsigned long values, enums;
                        unsigned int id, flags;
                        char name[32];
                        unsigned int count_values, count_enums;
                } property = {0, 0, ids[at], 0, {0}, 0, 0};

                if (system_control(compositor.card, COMPOSITOR_GETPROPERTY, address_of property) != 0)
                        continue;
                for (unsigned int name = 0; name < COMPOSITOR_PLANE_PROPS; name++)
                {
                        unsigned int same = 0;

                        while (names[name][same] && names[name][same] == property.name[same])
                                same++;
                        if (names[name][same] || property.name[same])
                                continue;
                        props[name] = ids[at];
                        if (name == COMPOSITOR_PLANE_TYPE)
                                *type = values[at];
                }
        }
        for (unsigned int name = 0; name < COMPOSITOR_PLANE_PROPS; name++)
                if (!props[name])
                        return 0;
        return 1;
}

// The primary plane of this crtc and an overlay that takes XRGB8888 on it, found once; false when the card has none.
static int compositor_overlay_ready(void)
{
        unsigned int planes[32];
        struct
        {
                unsigned long ids;
                unsigned int count, pad;
        } resources = {(unsigned long)planes, 32, 0};
        unsigned long cap[2] = {3, 1}; // DRM_CLIENT_CAP_ATOMIC, which brings every plane with it

        if (compositor.overlay_state)
                return compositor.overlay_state > 0;
        compositor.overlay_state = -1;
        if (system_control(compositor.card, COMPOSITOR_SET_CLIENT_CAP, address_of cap) != 0 ||
            system_control(compositor.card, COMPOSITOR_GETPLANERESOURCES, address_of resources) != 0 || resources.count > 32)
                return 0;

        for (unsigned int at = 0; at < resources.count; at++)
        {
                unsigned int formats[64], props[COMPOSITOR_PLANE_PROPS], takes = 0;
                unsigned long type = 3;
                struct
                {
                        unsigned int id, crtc, fb, possible, gamma, count;
                        unsigned long formats;
                } plane = {planes[at], 0, 0, 0, 0, 64, (unsigned long)formats};

                if (system_control(compositor.card, COMPOSITOR_GETPLANE, address_of plane) != 0 || plane.count > 64 ||
                    !(plane.possible & 1u << compositor.crtc_index) || !compositor_plane_props(planes[at], props, &type))
                        continue;
                for (unsigned int format = 0; format < plane.count; format++)
                        takes |= formats[format] == COMPOSITOR_XRGB8888;
                if (type == 1 && (!compositor.primary || plane.crtc == compositor.crtc))
                {
                        compositor.primary = planes[at];
                        memory_copy(compositor.primary_props, props, sizeof(props));
                }
                else if (type == 0 && takes && !compositor.overlay)
                {
                        compositor.overlay = planes[at];
                        memory_copy(compositor.overlay_props, props, sizeof(props));
                }
        }
        if (!compositor.primary || !compositor.overlay)
                return 0;
        compositor.overlay_state = 1;
        return 1;
}

/*
        The overlay's two buffers, rows of the output's width each, made again when the band wants more rows, and when the output is another
        width than they were made at: the band is composed at the output's width (compositor_present_overlaid's target, the tabs' rectangle cut to
        compositor.width), so in a buffer of a narrower one each row ran into the next and the last past the buffer's end, into the page after it
        (a space grown under them: a game's mode, a screen joined). compositor_overlay_width is the width they hold.
*/
static unsigned int compositor_overlay_width;

static int compositor_overlay_buffers(unsigned int rows)
{
        if (rows <= compositor.overlay_rows && compositor_overlay_width == (unsigned int)compositor.width)
                return 1;
        for (unsigned int which = 0; which < 2; which++)
                if (compositor.overlay_fb[which])
                {
                        unsigned int close[2] = {compositor.overlay_handle[which], 0};

                        gpu_forget((const void *)(compositor.overlay_pixels[which]), (positive)(compositor.overlay_bytes)), compositor_call(syscall(munmap), (positive)compositor.overlay_pixels[which], compositor.overlay_bytes, 0, 0, 0, 0);
                        system_control(compositor.card, COMPOSITOR_RMFB, address_of compositor.overlay_fb[which]);
                        system_control(compositor.card, COMPOSITOR_GEM_CLOSE, address_of close);
                        compositor.overlay_fb[which] = 0;
                }
        compositor.overlay_rows = 0;
        compositor_overlay_width = 0;
        rows = (rows + 63) & ~63u;
        for (unsigned int which = 0; which < 2; which++)
        {
                struct compositor_dumb dumb = {rows, (unsigned int)compositor.width, 32, 0, 0, 0, 0};
                struct compositor_map_dumb map = {0};
                struct compositor_fb2 fb = {0};
                long mapped = -COMPOSITOR_EINVAL;

                if (system_control(compositor.card, COMPOSITOR_CREATE_DUMB, address_of dumb) != 0)
                        return 0;
                fb.width = dumb.width;
                fb.height = dumb.height;
                fb.format = COMPOSITOR_XRGB8888;
                fb.handles[0] = map.handle = dumb.handle;
                fb.pitches[0] = dumb.pitch;
                if (system_control(compositor.card, COMPOSITOR_ADDFB2, address_of fb) == 0 &&
                    system_control(compositor.card, COMPOSITOR_MAP_DUMB, address_of map) == 0)
                        mapped = compositor_call(syscall(mmap), 0, dumb.size, 3, 1, (positive)compositor.card, map.offset);
                if (mapped < 0 && mapped > -4096)
                {
                        // Made but not kept: the framebuffer (if the card took it) and the buffer go back, as a scanout's do.
                        unsigned int close[2] = {dumb.handle, 0};

                        if (fb.fb_id)
                                system_control(compositor.card, COMPOSITOR_RMFB, address_of fb.fb_id);
                        system_control(compositor.card, COMPOSITOR_GEM_CLOSE, address_of close);
                        return 0;
                }
                compositor.overlay_fb[which] = fb.fb_id;
                compositor.overlay_handle[which] = dumb.handle;
                compositor.overlay_pixels[which] = (unsigned int *)mapped;
                compositor.overlay_pitch = dumb.pitch / 4;
                compositor.overlay_bytes = dumb.size;
        }
        compositor.overlay_rows = rows;
        compositor_overlay_width = (unsigned int)compositor.width;
        return 1;
}

// One enum of a property: its value, a bit for a bitmask, and its name.
struct compositor_enum
{
        unsigned long value;
        char name[32];
};

/*
        The enums of the property called name on object (of type), in the card's order, as many as room: the count, or 0
        when the object has no property by that name. Its id comes back in id.
*/
static unsigned int compositor_enums_of(unsigned int object, unsigned int type, const char *name, unsigned int *id,
                                        struct compositor_enum *enums, unsigned int room)
{
        unsigned int ids[64];
        unsigned long values[64];
        struct
        {
                unsigned long props, values;
                unsigned int count, object, type;
        } got = {(unsigned long)ids, (unsigned long)values, 64, object, type};

        if (system_control(compositor.card, COMPOSITOR_OBJ_GETPROPERTIES, address_of got) != 0 || got.count > 64)
                return 0;
        for (unsigned int at = 0; at < got.count; at++)
        {
                struct
                {
                        unsigned long values, enums;
                        unsigned int id, flags;
                        char name[32];
                        unsigned int count_values, count_enums;
                } property = {0, 0, ids[at], 0, {0}, 0, 0};
                unsigned int same = 0;

                if (system_control(compositor.card, COMPOSITOR_GETPROPERTY, address_of property) != 0)
                        continue;
                while (name[same] && name[same] == property.name[same])
                        same++;
                if (name[same] || property.name[same])
                        continue;
                *id = ids[at];
                property.enums = (unsigned long)enums;
                property.count_enums = room;
                // The values are not wanted: the first call left their count in, and the kernel puts that many through the
                // pointer, which is null, so the call failed (EFAULT) and no card's rotation was ever found.
                property.values = 0;
                property.count_values = 0;
                if (system_control(compositor.card, COMPOSITOR_GETPROPERTY, address_of property) != 0)
                        return 0;
                return property.count_enums < room ? property.count_enums : room;
        }
        return 0;
}

/*
        The turn the card does for a screen on crtc_index, in hardware: the primary plane of that crtc with a rotation
        property that lists rotate-DEGREES (windows_rotation_bit), the property's id and the value to set it to. False
        when no such plane is: the screen is then turned in software.
*/
static int compositor_rotation_hardware(unsigned int crtc_index, unsigned int degrees, unsigned int *plane,
                                        unsigned int *property, unsigned long *value)
{
        unsigned int planes[32];
        struct
        {
                unsigned long ids;
                unsigned int count, pad;
        } resources = {(unsigned long)planes, 32, 0};
        // DRM_CLIENT_CAP_ATOMIC, which the turn's commit needs and which brings every plane with it: without it the card lists
        // no primary plane, and the first take of the screens is before compositor_overlay_ready has ever asked for it.
        unsigned long cap[2] = {3, 1};

        if (system_control(compositor.card, COMPOSITOR_SET_CLIENT_CAP, address_of cap) != 0 ||
            system_control(compositor.card, COMPOSITOR_GETPLANERESOURCES, address_of resources) != 0 || resources.count > 32)
                return 0;
        for (unsigned int at = 0; at < resources.count; at++)
        {
                unsigned int formats[64], props[COMPOSITOR_PLANE_PROPS], id = 0, count;
                unsigned long type = 3, bits[8] = {0};
                struct compositor_enum enums[8];
                const char *names[8] = {0};
                struct
                {
                        unsigned int id, crtc, fb, possible, gamma, count;
                        unsigned long formats;
                } plane_info = {planes[at], 0, 0, 0, 0, 64, (unsigned long)formats};

                if (system_control(compositor.card, COMPOSITOR_GETPLANE, address_of plane_info) != 0 || plane_info.count > 64 ||
                    !(plane_info.possible & 1u << crtc_index) || !compositor_plane_props(planes[at], props, &type) || type != 1)
                        continue;
                count = compositor_enums_of(planes[at], COMPOSITOR_OBJECT_PLANE, "rotation", &id, enums, 8);
                for (unsigned int each = 0; each < count; each++)
                        names[each] = enums[each].name, bits[each] = enums[each].value;
                *value = windows_rotation_bit(names, bits, count, degrees);
                if (!*value)
                        continue;
                *plane = planes[at];
                *property = id;
                return 1;
        }
        return 0;
}

/*
        A screen the card turns: its plane shows the screen's rectangle of the scanout (source, in the scanout's pixels,
        and width by height) at the panel's own size, with the rotation property set to value, in one commit. False
        when the card refuses it.
*/
static int compositor_rotate_commit(const struct compositor_output *output, const int *source, unsigned int width,
                                    unsigned int height, unsigned int fb)
{
        unsigned int props[COMPOSITOR_PLANE_PROPS], ids[11], plane = output->turn_plane, n = 0;
        unsigned long type = 0, values[11];
        unsigned int objects[1] = {plane}, counts[1];
        struct
        {
                unsigned int flags, count;
                unsigned long objects, counts, props, values, reserved, user_data;
        } commit;

        if (!compositor_plane_props(plane, props, &type))
                return 0;
        ids[n] = props[COMPOSITOR_PLANE_FB], values[n++] = fb;
        ids[n] = props[COMPOSITOR_PLANE_CRTC], values[n++] = output->crtc;
        ids[n] = props[COMPOSITOR_PLANE_SRC_X], values[n++] = (unsigned long)source[0] << 16;
        ids[n] = props[COMPOSITOR_PLANE_SRC_Y], values[n++] = (unsigned long)source[1] << 16;
        ids[n] = props[COMPOSITOR_PLANE_SRC_W], values[n++] = (unsigned long)width << 16;
        ids[n] = props[COMPOSITOR_PLANE_SRC_H], values[n++] = (unsigned long)height << 16;
        ids[n] = props[COMPOSITOR_PLANE_CRTC_X], values[n++] = 0;
        ids[n] = props[COMPOSITOR_PLANE_CRTC_Y], values[n++] = 0;
        ids[n] = props[COMPOSITOR_PLANE_CRTC_W], values[n++] = output->mode.hdisplay;
        ids[n] = props[COMPOSITOR_PLANE_CRTC_H], values[n++] = output->mode.vdisplay;
        ids[n] = output->turn_property, values[n++] = output->turn_value;
        counts[0] = n;
        commit.flags = 0;
        commit.count = 1;
        commit.objects = (unsigned long)objects;
        commit.counts = (unsigned long)counts;
        commit.props = (unsigned long)ids;
        commit.values = (unsigned long)values;
        commit.reserved = commit.user_data = 0;
        return system_control(compositor.card, COMPOSITOR_ATOMIC, address_of commit) == 0;
}

/*
        One commit: fb on the primary plane and, where overlay says, the
        overlay's buffer which at rect, or the overlay off (rect none). It
        does not wait, and its event is the flip's; false when the card
        refuses it.
*/
static int compositor_atomic_to(unsigned int fb, int row, unsigned int which, int overlay, unsigned int shown,
                                const int *source, const int *rect)
{
        unsigned int objects[2] = {compositor.primary, compositor.overlay}, counts[2] = {1, 0}, props[12];
        unsigned long values[12];
        unsigned int n = 0;

        props[n] = compositor.primary_props[COMPOSITOR_PLANE_FB], values[n++] = fb;
        if (overlay)
        {
                const unsigned int *id = compositor.overlay_props;
                unsigned long width = rect ? (unsigned long)(rect[2] - rect[0]) : 0;
                unsigned long height = rect ? (unsigned long)(rect[3] - rect[1]) : 0;

                props[n] = id[COMPOSITOR_PLANE_FB], values[n++] = rect ? shown : 0;
                props[n] = id[COMPOSITOR_PLANE_CRTC], values[n++] = rect ? compositor.crtc : 0;
                if (rect)
                {
                        props[n] = id[COMPOSITOR_PLANE_SRC_X], values[n++] = (unsigned long)source[0] << 16;
                        props[n] = id[COMPOSITOR_PLANE_SRC_Y], values[n++] = (unsigned long)source[1] << 16;
                        props[n] = id[COMPOSITOR_PLANE_SRC_W], values[n++] = width << 16;
                        props[n] = id[COMPOSITOR_PLANE_SRC_H], values[n++] = height << 16;
                        props[n] = id[COMPOSITOR_PLANE_CRTC_X], values[n++] = (unsigned long)rect[0];
                        props[n] = id[COMPOSITOR_PLANE_CRTC_Y], values[n++] = (unsigned long)rect[1];
                        props[n] = id[COMPOSITOR_PLANE_CRTC_W], values[n++] = width;
                        props[n] = id[COMPOSITOR_PLANE_CRTC_H], values[n++] = height;
                }
                counts[1] = n - 1;
        }

        struct
        {
                unsigned int flags, count;
                unsigned long objects, counts, props, values, reserved, user_data;
        } commit = {COMPOSITOR_FLIP_EVENT | COMPOSITOR_ATOMIC_NONBLOCK, overlay ? 2u : 1u, (unsigned long)objects,
                    (unsigned long)counts, (unsigned long)props, (unsigned long)values, 0, 0};

        commit.user_data = ++compositor.flip_serial;
        if (system_control(compositor.card, COMPOSITOR_ATOMIC, address_of commit) != 0)
                return 0;
        compositor.flip_pending = 1;
        compositor.flip_issued = clock_monotonic_nanoseconds();
        compositor.flip_row = row;
        compositor.flip_buffer = which;
        compositor.on_screen = fb;
        if (overlay)
        {
                compositor.overlay_on = rect != 0;
                compositor.plane_fb = 0;
                if (rect)
                        memory_copy(compositor.overlay_rect, rect, sizeof(compositor.overlay_rect));
        }
        return 1;
}

/*
        A window on the overlay with a new buffer in a present that also composes what is round it (the desktop, its corners' rows): the overlay's
        change is carried by the flip of the plate that is composed (compositor_flip_fenced's commit, or compositor_present_tell's), so the window
        and what is round it are shown at the same vertical blank, and the desktop under the plane is composed into the plate the screen is not
        showing, as it is with no plane. fb 0: none carried. A commit that would not take it shows the plate alone, and the next present commits
        the overlay's buffer (compositor.plane_fb is still the old one).
*/
static struct
{
        unsigned int fb, which;
        int row, source[2], rect[4];
} compositor_plane_carry;

// The carried overlay's properties after the primary's in an atomic commit's lists: the overlay's object and its count, and the plane's
// state once the commit is taken (compositor_plane_carried).
static unsigned int compositor_plane_carry_props(unsigned int *objects, unsigned int *counts, unsigned int *props, unsigned long *values,
                                                 unsigned int n)
{
        const unsigned int *id = compositor.overlay_props;
        unsigned long width = (unsigned long)(compositor_plane_carry.rect[2] - compositor_plane_carry.rect[0]);
        unsigned long height = (unsigned long)(compositor_plane_carry.rect[3] - compositor_plane_carry.rect[1]);
        unsigned int from = n;

        props[n] = id[COMPOSITOR_PLANE_FB], values[n++] = compositor_plane_carry.fb;
        props[n] = id[COMPOSITOR_PLANE_CRTC], values[n++] = compositor.crtc;
        props[n] = id[COMPOSITOR_PLANE_SRC_X], values[n++] = (unsigned long)compositor_plane_carry.source[0] << 16;
        props[n] = id[COMPOSITOR_PLANE_SRC_Y], values[n++] = (unsigned long)compositor_plane_carry.source[1] << 16;
        props[n] = id[COMPOSITOR_PLANE_SRC_W], values[n++] = width << 16;
        props[n] = id[COMPOSITOR_PLANE_SRC_H], values[n++] = height << 16;
        props[n] = id[COMPOSITOR_PLANE_CRTC_X], values[n++] = (unsigned long)compositor_plane_carry.rect[0];
        props[n] = id[COMPOSITOR_PLANE_CRTC_Y], values[n++] = (unsigned long)compositor_plane_carry.rect[1];
        props[n] = id[COMPOSITOR_PLANE_CRTC_W], values[n++] = width;
        props[n] = id[COMPOSITOR_PLANE_CRTC_H], values[n++] = height;
        objects[1] = compositor.overlay;
        counts[1] = n - from;
        return n;
}

static void compositor_unscanned(unsigned int why);

// The carried overlay taken by a commit (shown), or not (none): carried no more either way.
static void compositor_plane_carried(int shown)
{
        if (shown && compositor_plane_carry.fb)
        {
                compositor.plane_fb = compositor_plane_carry.fb;
                compositor.plane_row = compositor.direct_row = compositor_plane_carry.row;
                compositor.overlay_on = 1;
                compositor_unscanned(0);
                memory_copy(compositor.plane_rect, compositor_plane_carry.rect, sizeof(compositor.plane_rect));
                memory_copy(compositor.overlay_rect, compositor_plane_carry.rect, sizeof(compositor.overlay_rect));
        }
        compositor_plane_carry.fb = 0;
}

// The tabs' buffer which on the overlay at rect, read from the same place in it.
static int compositor_atomic(unsigned int fb, int row, unsigned int which, int overlay, const int *rect, unsigned int buffer)
{
        int source[2] = {rect ? rect[0] : 0, 0};

        return compositor_atomic_to(fb, row, which, overlay, compositor.overlay_fb[buffer], source, rect);
}

// The overlay off where it is on and the commits that would take it off are refused: the plane set to nothing.
static void compositor_overlay_off(void)
{
        unsigned int off[12] = {compositor.overlay, compositor.crtc};

        compositor.overlay_owed = 0;
        if (!compositor.overlay_on)
                return;
        // Refused (a commit of the crtc's still in flight, a card being reset), the overlay is still on as the card has it: the next present asks
        // again, and what it shows is drawn there again for it. Taken as off, a refusal left whatever it scanned on the screen for good.
        if (system_control(compositor.card, COMPOSITOR_SETPLANE, address_of off) != 0 && compositor.card >= 0)
        {
                compositor.overlay_owed = 1;
                compositor_damage(compositor.overlay_rect[0], compositor.overlay_rect[1], compositor.overlay_rect[2] - compositor.overlay_rect[0],
                                  compositor.overlay_rect[3] - compositor.overlay_rect[1]);
                return;
        }
        compositor.overlay_on = 0;
        compositor.plane_fb = 0;
}

// The rectangle the tabs cover, on the output; false when they cover none of it.
static int compositor_tabs_rect(const struct compositor_frame *tabs, unsigned int count, int rect[4])
{
        rect[0] = compositor.width, rect[1] = compositor.height, rect[2] = rect[3] = 0;
        for (unsigned int at = 0; at < count; at++)
        {
                int x, y, width, height;

                compositor_outline(&tabs[at], &x, &y, &width, &height);
                rect[0] = x < rect[0] ? x : rect[0];
                rect[1] = y < rect[1] ? y : rect[1];
                rect[2] = x + width > rect[2] ? x + width : rect[2];
                rect[3] = y + height > rect[3] ? y + height : rect[3];
        }
        rect[0] = rect[0] < 0 ? 0 : rect[0];
        rect[1] = rect[1] < 0 ? 0 : rect[1];
        rect[2] = rect[2] > compositor.width ? compositor.width : rect[2];
        rect[3] = rect[3] > compositor.height ? compositor.height : rect[3];
        return rect[2] > rect[0] && rect[3] > rect[1];
}

// The frame of a window shown straight with the tabs on the overlay: false when the card will not, and the tabs are
// then patched in.
static b32 compositor_present_overlaid(int row, const struct compositor_frame *frames, unsigned int count, unsigned int windows)
{
        unsigned int which = compositor.front[row], fb = compositor_client_scans(row) ? compositor_lent_fb(row) : compositor_window_fb(row, which),
                     buffer = compositor.overlay_back;
        int rect[4], shown = compositor_tabs_rect(frames + windows, count - windows, rect), drawn = 0;
        unsigned short clip[COMPOSITOR_DAMAGE][4];
        struct compositor_dirty dirty = {fb, 0, 0, 0, (unsigned long)clip};

        if (!fb)
                return 0;
        // The card is still on its way to the last commit: this frame is made once it is there.
        if (compositor.flip_pending)
                return 1;

        if (shown)
        {
                drawn = !compositor.overlay_on || memory_compare(compositor.overlay_rect, rect, sizeof(rect)) != 0;
                for (unsigned int at = 0; at < compositor.damages && !drawn; at++)
                {
                        int meet[4];

                        memory_copy(meet, compositor.damage[at], sizeof(meet));
                        compositor_meet(meet, rect);
                        drawn = meet[2] > meet[0] && meet[3] > meet[1];
                }
                if (drawn)
                {
                        struct compositor_target target = {compositor.overlay_pixels[buffer], 0, compositor.width,
                                                           compositor.height, rect[0], rect[1], rect[2], rect[3], 0, rect[1], 0};
                        int rows[2];

                        compositor_wl_straight_pays(-1); // the window under the tabs is composed with them

                        if (!compositor_overlay_buffers((unsigned int)(rect[3] - rect[1])))
                        {
                                compositor.overlay_state = -1;
                                return 0;
                        }
                        target.pixels = compositor.overlay_pixels[buffer];
                        target.pitch = compositor.overlay_pitch;
                        compositor_scene_banded(&target, frames, count, 0, rows);
                }
        }
        if (fb != compositor.on_screen || drawn || (!shown && compositor.overlay_on))
        {
                // The overlay is in the commit only when it changes: a new picture of the tabs, or off where it was on. A fullscreen window
                // with nothing over it flips its primary alone, each frame a plane's fast update, where every flip also set the idle overlay
                // off again.
                if (!compositor_atomic(fb, row, which, drawn || (!shown && compositor.overlay_on), shown ? rect : 0, buffer))
                {
                        compositor.overlay_state = -1;
                        compositor_overlay_off();
                        return 0;
                }
                if (drawn)
                        compositor.overlay_back ^= 1;
        }
        compositor.direct_row = row;
        compositor.table->scanned++;

        for (unsigned int at = 0; at < compositor.damages; at++)
                for (unsigned int side = 0; side < 4; side++)
                        clip[at][side] = (unsigned short)compositor.damage[at][side];
        dirty.clip_count = compositor.damages;
        system_control(compositor.card, COMPOSITOR_DIRTYFB, address_of dirty);
        compositor.damages = 0;
        compositor.screen_stale = 1;
        return 1;
}

// The card's first picture is drawn before the compositor has made its window table, so what the table is to say of that
// present has nowhere to go yet.
static unsigned int compositor_unscanned_spare;

static void compositor_unscanned(unsigned int why)
{
        if (compositor.table)
                compositor.table->unscanned = why;
}

/*
        A window that does not fill the output on the overlay: the card shows
        the top window's middle straight from its buffer, over the
        compositor's own screen, and the compositor composes everything else
        and nothing under it -- where it copied the whole window every frame.
        The window is the top one, so nothing of the scene is over it but the
        pointer, on its own plane; opaque, at the size it drew, wholly on the
        output and clear of the tabs. What the overlay shows is the window
        less its rows with corners, its close button and its line, which the
        compositor still draws round it. A card with no overlay, or one that
        refuses, has the window composed as any other.

        A client's window (a Wayland or an X one: no close button, no grab of
        the compositor's) goes on it as the compositor's own do, its picture
        the client's buffer itself (compositor_lent_fb): a commit of the
        client's is then a commit of the overlay's framebuffer, and nothing is
        composed for it. Nothing the scene draws after the windows (the tabs,
        the moon's glow and menu, the snap preview, the grid) may meet the
        plane's rectangle: drawn over the window, it would be under the
        overlay, unseen. Nor may the window have layers (subsurfaces composed
        over it): its framebuffer is then not the window as it is seen.
*/
// Whether the owner's file /run/moonwater/planes-off is there (looked for every 512th present, as lists-off is): then no window goes on the
// overlay and every one is composed, for a comparison.
static int compositor_planes_off(void)
{
        static unsigned int probes;
        static int off;

        if ((probes++ & 511) == 0)
        {
                long handle = (long)system_open_at(AT_FDCWD, (string_address) "/run/moonwater/planes-off", FILE_READ | O_CLOEXEC);

                off = handle >= 0;
                if (handle >= 0)
                        system_close(handle);
        }
        return off;
}

static int compositor_plane_candidate(const struct compositor_frame *frames, const unsigned int *rows,
                                      unsigned int windows, unsigned int count, int rect[4])
{
        struct compositor_frame band;
        int row, bx, by, side, radius, top, bottom;

        unsigned int *why = compositor.table ? &compositor.table->unscanned : &compositor_unscanned_spare;

        if (compositor.card < 0)
                return *why = 1, -1;
        if (compositor_output_count() > 1)
                return *why = 13, -1;
        if (compositor.cursor.soft)
                return *why = 3, -1;
        if (compositor_planes_off())
                return *why = 21, -1;
        if (!windows || compositor.drag >= 0 || compositor.resize >= 0 || compositor.preview_draw)
                return *why = 4, -1; // a preview is over the window, and the overlay is not composed under it
        if (compositor_spots_any(0))
                return *why = 14, -1;
        row = rows[windows - 1];
        const struct compositor_frame *frame = &frames[windows - 1];

        if (!compositor_client_scans(row) && (compositor.direct_refused || !compositor.table_memfd))
                return *why = compositor.direct_refused ? 11 : 2, -1;
        // Glass is the window's WINDOW_GLASS (compositor.glassy): the row's other flags (a client's NO_CLOSE and NO_GRAB) are no glass. Tested
        // as the row's whole glass word, every client's window was refused as glass and composed.
        if (compositor.glassy[row])
                return *why = 5, -1;
        // A client's window whose picture is not its dma-buf read in place stays composed. One the shim copies into the table (the processor
        // composes, or its format is turned): on the overlay its table buffer is the screen's, so each commit is laid into the other one,
        // which first takes the whole last picture (compositor_wl.laying's last), and the whole window is damage (new-dragonfly, Haswell,
        // Chromium typing: 1.86 ms from its commit to the present against 0.15 composed, the taskbar's glass made again at each key). One in
        // the client's shared memory has no framebuffer (compositor_lent_fb makes one of a dma-buf only).
        if (compositor_wl_row_unscannable(row))
                return *why = 22, -1;
        // The window's own frame, not a subsurface's over it (layers are frames of the same row after it).
        if (windows > 1 && rows[windows - 2] == (unsigned int)row)
                return *why = 18, -1;
        // While the windows' lists are drawn the card writes a window's picture in place, each frame (a clear and then the frame's ops): shown straight,
        // the screen would scan that picture out while it is half drawn, and every frame would flicker. With list mode on every window is composed (by
        // the card, after it has drawn the pictures), so none is shown straight.
        if (!compositor_client_scans(row) && __atomic_load_n(&compositor.table->list_mode, __ATOMIC_ACQUIRE))
                return *why = 16, -1;
        if (compositor.landing[row])
                return *why = 4, -1; // a window landing is drawn smaller than its picture
        if (compositor.asking[row] || compositor.picture_w[row] != compositor.w[row] ||
            compositor.picture_h[row] != compositor.h[row])
                return *why = 6, -1;
        if (frame->x < 0 || frame->y < 0 || frame->x + frame->width > compositor.width ||
            frame->y + frame->height > compositor.height)
                return *why = 7, -1;
        radius = compositor_radius(frame);
        top = frame->y + radius;
        bottom = frame->y + frame->height - radius;
        if (compositor_close_box(frame, &bx, &by, &side))
                top = by + side > top ? by + side : top;
        if (bottom - top < 64 * compositor.scale / 100)
                return *why = 8, -1;
        rect[0] = frame->x, rect[1] = top, rect[2] = frame->x + frame->width, rect[3] = bottom;
        // A window blended over what is under it goes on the plane only where what the plane shows of it is opaque all the same (its client's
        // opaque region: Chromium's is all of it but its round top corners, which are in the rows above the plane), shown as the X format
        // (compositor_lent_fb_opaque); the overlay is not blended over the primary, where nothing is composed.
        if (frame->blend && !compositor_wl_row_opaque(row, 0, top - frame->y, frame->width, bottom - frame->y))
                return *why = 20, -1;

        compositor_band_frame(&band);
        if (compositor.band && band.x < rect[2] && band.x + band.width > rect[0] && band.y < rect[3] &&
            band.y + band.height > rect[1])
                return *why = 9, -1;
        for (unsigned int at = windows; at < count; at++)
        {
                int x, y, width, height;

                compositor_outline(&frames[at], &x, &y, &width, &height);
                if (width > 0 && height > 0 && x < rect[2] && x + width > rect[0] && y < rect[3] && y + height > rect[1])
                        return *why = 19, -1;
        }
        // The card asked last: whether it has an overlay, then the window's framebuffer.
        if (!compositor_overlay_ready())
                return *why = 10, -1;
        return (compositor_client_scans(row) ? compositor_lent_fb_opaque(row) : compositor_window_fb(row, compositor.front[row])) ? row : (*why = 11, -1);
}

// The damage less the rectangle the overlay shows; false when the pieces do not fit the list, which is then left whole.
static int compositor_damage_cut(const int *rect)
{
        int pieces[COMPOSITOR_DAMAGE][4];
        unsigned int count = 0;

        for (unsigned int at = 0; at < compositor.damages; at++)
        {
                const int *d = compositor.damage[at];
                int cut[4][4] = {{d[0], d[1], d[2], rect[1]},
                                 {d[0], rect[3], d[2], d[3]},
                                 {d[0], rect[1] > d[1] ? rect[1] : d[1], rect[0], rect[3] < d[3] ? rect[3] : d[3]},
                                 {rect[2], rect[1] > d[1] ? rect[1] : d[1], d[2], rect[3] < d[3] ? rect[3] : d[3]}};

                if (d[2] <= rect[0] || d[0] >= rect[2] || d[3] <= rect[1] || d[1] >= rect[3])
                {
                        if (count == COMPOSITOR_DAMAGE)
                                return 0;
                        memory_copy(pieces[count++], d, sizeof(pieces[0]));
                        continue;
                }
                for (unsigned int k = 0; k < 4; k++)
                {
                        int *c = cut[k];

                        c[0] = c[0] > d[0] ? c[0] : d[0], c[2] = c[2] < d[2] ? c[2] : d[2];
                        c[1] = c[1] > d[1] ? c[1] : d[1], c[3] = c[3] < d[3] ? c[3] : d[3];
                        if (c[2] <= c[0] || c[3] <= c[1])
                                continue;
                        if (count == COMPOSITOR_DAMAGE)
                                return 0;
                        memory_copy(pieces[count++], c, sizeof(pieces[0]));
                }
        }
        memory_copy(compositor.damage, pieces, sizeof(pieces[0]) * count);
        compositor.damages = count;
        return 1;
}

/*
        A change told to the card a framebuffer at a time, on more than one
        screen: each framebuffer is told what of the change is on the screens
        that show it, and none that shows nothing of it is told anything, so
        the commit a DIRTYFB is waits on the vertical blanks of the screens
        that changed and on no other's. A screen that has no framebuffer of
        its own shares the scanout's with the primary, and is told with it.
*/
static void compositor_dirty_screens(const unsigned short (*clip)[4], unsigned int clips)
{
        for (unsigned int at = 0; at < compositor.outputs; at++)
        {
                unsigned int fb = compositor.output[at].fb ? compositor.output[at].fb : compositor.fb, told = 0, users = 0;

                // A screen Canvas turns is told by compositor_turn_frame, on its own buffer.
                if (compositor.output[at].turned == 2)
                        continue;
                unsigned short mine[COMPOSITOR_DAMAGE][4];
                int seen = 0;

                for (unsigned int before = 0; before < at && !seen; before++)
                        seen = (compositor.output[before].fb ? compositor.output[before].fb : compositor.fb) == fb;
                if (seen)
                        continue;
                for (unsigned int screen = at; screen < compositor.outputs; screen++)
                {
                        unsigned short cut[COMPOSITOR_DAMAGE][4];
                        unsigned int count;

                        if ((compositor.output[screen].fb ? compositor.output[screen].fb : compositor.fb) != fb)
                                continue;
                        users++;
                        count = compositor_screen_clips(compositor.screen[screen], clip, clips, cut);
                        for (unsigned int each = 0; each < count && told < COMPOSITOR_DAMAGE; each++)
                                memory_copy(mine[told++], cut[each], sizeof(cut[each]));
                }
                // A framebuffer two screens share is told the change whole, as one framebuffer always was.
                if (users > 1)
                {
                        memory_copy(mine, clip, clips * sizeof(clip[0]));
                        told = clips;
                }
                if (told)
                {
                        struct compositor_dirty dirty = {fb, 0, 0, told, (unsigned long)mine};

                        system_control(compositor.card, COMPOSITOR_DIRTYFB, address_of dirty);
                }
        }
}

// A screen Canvas turns in software: its framebuffer over the buffer its panel is drawn in, by screen (see
// compositor_turn_buffer).
struct compositor_turn
{
        unsigned int fb, handle;
        unsigned int *pixels;
        unsigned long bytes, pitch; // pitch in bytes
        unsigned int width, height;
};
static struct compositor_turn compositor_turns[WINDOWS_OUTPUTS];

// The framebuffer the primary plane shows when the overlay is taken off it: the scanout, or the buffer Canvas turns the
// first screen in.
static unsigned int compositor_primary_fb(void)
{
        return compositor.outputs && compositor.output[0].turned == 2 ? compositor.output[0].fb : compositor.fb;
}

// Whether any screen is turned, by the card or by Canvas. Then no window is shown straight and no overlay is put up.
static int compositor_turning(void)
{
        for (unsigned int at = 0; at < compositor.outputs; at++)
                if (compositor.output[at].turned)
                        return 1;
        return 0;
}

// Whether any screen is turned by Canvas itself: its picture is made from the scene, a frame at a time.
static int compositor_turning_software(void)
{
        for (unsigned int at = 0; at < compositor.outputs; at++)
                if (compositor.output[at].turned == 2)
                        return 1;
        return 0;
}

/*
        The part of the scene a turned screen shows, turned into its own buffer and told to its framebuffer: each rectangle
        of the damage (clip), cut to the screen, is turned for the panel (windows_rotate_u32) and the panel's rectangle of it
        is given to the card, all of a screen's in one DIRTYFB. A rectangle is turned alone: two far apart (the pointer's old
        place and its new, the clock) are two small ones and not the one box that holds them both, which was most of the
        screen. The scene is composed on the scanout first, so the pointer and the tabs are in it, and it is turned from the
        machine's memory: the mirror of what the card holds where Canvas keeps one (compositor_changed_ready, which it does
        for every card while a screen is turned), the scanout itself where that is the machine's memory. The card's own
        memory is only ever written: a read of it came to about ten megabytes a second on a real card, and a frame of
        2560 by 1080 a second.
*/
#ifdef MOONWATER_GPU_C
static int compositor_gpu_rotate(unsigned int at, const int *screen, unsigned int degrees, unsigned int x0, unsigned int y0, unsigned int x1, unsigned int y1);
static void compositor_gpu_refuse(enum compositor_gpu_why why, int processor);
#endif
// gpu: the scene was composed by the GPU into the scanout (there is no mirror of it then), and the GPU turns it.
static void compositor_turn_frame(const unsigned short (*clip)[4], unsigned int clips, int gpu)
{
        const unsigned int *scene = !gpu && !compositor.mirror_stale && compositor.changed > 0 && compositor.mirror ? compositor.mirror : compositor.scanout;
        unsigned long pitch = !gpu && !compositor.mirror_stale && compositor.changed > 0 && compositor.mirror ? compositor.mirror_pitch : compositor.scanout_pitch / 4;

        for (unsigned int at = 0; at < compositor.outputs && clips; at++)
        {
                const int *screen = compositor.screen[at];
                unsigned int degrees = compositor.output[at].degrees, told = 0;
                unsigned short rect[COMPOSITOR_DAMAGE][4];

                if (compositor.output[at].turned != 2)
                        continue;
                for (unsigned int each = 0; each < clips && told < COMPOSITOR_DAMAGE; each++)
                {
                        unsigned int x0 = clip[each][0] > (unsigned int)screen[0] ? clip[each][0] : (unsigned int)screen[0];
                        unsigned int y0 = clip[each][1] > (unsigned int)screen[1] ? clip[each][1] : (unsigned int)screen[1];
                        unsigned int x1 = clip[each][2] < (unsigned int)(screen[0] + screen[2]) ? clip[each][2]
                                                                                              : (unsigned int)(screen[0] + screen[2]);
                        unsigned int y1 = clip[each][3] < (unsigned int)(screen[1] + screen[3]) ? clip[each][3]
                                                                                              : (unsigned int)(screen[1] + screen[3]);
                        unsigned int panel[4];

                        if (x0 >= x1 || y0 >= y1)
                                continue;
#ifdef MOONWATER_GPU_C
                        if (!gpu || !compositor_gpu_rotate(at, screen, degrees, x0 - screen[0], y0 - screen[1], x1 - screen[0], y1 - screen[1]))
                        {
                                if (gpu) // the present was the card's; the turn it was refused is counted as the processor's
                                        compositor_gpu_refuse(COMPOSITOR_GPU_WHY_TURN, 0);
#endif
                                windows_rotate_u32(compositor_turns[at].pixels, compositor_turns[at].pitch / 4,
                                                   scene + (unsigned long)screen[1] * pitch + (unsigned long)screen[0], pitch,
                                                   (unsigned int)screen[2], (unsigned int)screen[3], degrees, x0 - screen[0],
                                                   y0 - screen[1], x1 - screen[0], y1 - screen[1]);
#ifdef MOONWATER_GPU_C
                        }
#endif
                        windows_rotate_panel((unsigned int)screen[2], (unsigned int)screen[3], degrees, x0 - screen[0],
                                             y0 - screen[1], x1 - screen[0], y1 - screen[1], panel);
                        for (unsigned int side = 0; side < 4; side++)
                                rect[told][side] = (unsigned short)panel[side];
                        told++;
                }
                if (told)
                {
                        struct compositor_dirty dirty = {compositor_turns[at].fb, 0, 0, told, (unsigned long)rect};

                        system_control(compositor.card, COMPOSITOR_DIRTYFB, address_of dirty);
                }
        }
}

