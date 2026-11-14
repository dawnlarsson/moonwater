#ifndef MOONWATER_GPU_C
#define MOONWATER_GPU_C
// Canvas on the GPU: AMD compute, raw. One buffer holds the packets, the arguments, the kernel and the pixels.

#define GPU_IO(dir, n, size) ((dir) << 30 | (size) << 16 | 0x6400u | (0x40 + (n)))
#define GPU_PKT(op, n) (0xc0000002u | (op) << 8 | (n) << 16) // type 3, for the compute shader type
#define GPU_VA 0x1000000000ull
#define GPU_BASES_AT 40960 // the table of bases a text op names (its word 6, bits 16 to 23: 255 addresses of 8 bytes, number 0 is the buffer's start and is not read)
#define GPU_DATA 45056 // the pixels start here; before them: packets at 0, arguments at 512, the kernels from 1024, the bases from GPU_BASES_AT

// broken: the card is there and drew the self-test wrong; pending: the job last waited for and not seen done (its sequence on ctx), 0 for none
#define GPU_IMPORTS 64
static struct { int fd, broken; unsigned int family, device, ctx, handle, imported[GPU_IMPORTS], imports; positive imported_va[GPU_IMPORTS], imported_bytes[GPU_IMPORTS]; unsigned char imported_dead[GPU_IMPORTS], imported_holds[GPU_IMPORTS]; unsigned int address_to at; positive size, pending; } gpu;
static positive gpu_last_job; // when the last job finished (monotonic ns): the idle clock's measure (gpu_wake)
// The memory the dma-buf route may reach: the window table's memfd (fd), mapped at at (its byte 0), the picture part of it (bytes), and pin(offset, on) when a
// map of its pages is made or let go, so that the program that owns them does not give them back while the card may read them.
static struct { int fd; unsigned char *at; positive bytes; void (*pin)(positive offset, b32 on); } gpu_shared = {-1, 0, 0, 0};
#define GPU_MEMFDS 64
static struct { int fd; void *at; positive bytes; void (*pin)(positive offset, b32 on); } gpu_memfds[GPU_MEMFDS];

// Register or update an mmap'd memfd region for udmabuf fallback (gpu_map_dmabuf).
__attribute__((unused)) static b32 gpu_register_memfd(int fd, void *at, positive bytes, void (*pin)(positive offset, b32 on))
{
        int free_slot = -1;

        if (fd < 0 || !at || !bytes)
                return false;
        for (int i = 0; i < (int)GPU_MEMFDS; i++)
        {
                if (gpu_memfds[i].at == at)
                {
                        gpu_memfds[i].fd = fd, gpu_memfds[i].bytes = bytes, gpu_memfds[i].pin = pin;
                        return true;
                }
                if (free_slot < 0 && !gpu_memfds[i].at)
                        free_slot = i;
        }
        if (free_slot < 0)
                return false;
        gpu_memfds[free_slot].fd = fd, gpu_memfds[free_slot].at = at, gpu_memfds[free_slot].bytes = bytes, gpu_memfds[free_slot].pin = pin;
        return true;
}

__attribute__((unused)) static void gpu_unregister_memfd(void *at)
{
        if (!at)
                return;
        for (unsigned int i = 0; i < GPU_MEMFDS; i++)
                if (gpu_memfds[i].at == at)
                {
                        gpu_memfds[i].fd = -1, gpu_memfds[i].at = 0, gpu_memfds[i].bytes = 0, gpu_memfds[i].pin = 0;
                        return;
                }
}
static b32 gpu_userptr_off, gpu_userptr_gone; // off: /run/moonwater/gpu-nouserptr says to use the dma-buf route for every map (a test); gone: the kernel refused userptr with
                                              // ENODEV, EINVAL, EOPNOTSUPP or ENOSYS, which is not asked again (every map after goes by the dma-buf route)
static unsigned int gpu_maps_user, gpu_maps_dma; // maps made, by userptr and by the dma-buf route (moonwater debug)
static b32 gpu_lost; // a job's submit said the card lost its context or VM (set by gpu_run; the caller recovers and clears it)
static unsigned int gpu_recovered; // how many times the card was started again after it lost its state (moonwater debug)
static unsigned int gpu_epoch; // the card's descriptor count: it goes up each time the card is let go of (gpu_stop), and with it every import and map. An address kept
                               // from an earlier epoch is none of the card's (wayland_host.c's lent and source addresses check it)
static positive gpu_bytes; // the bytes gpu_start was asked for (gpu_recover starts the card again with them)
#ifndef GPU_SUBMIT_FAKE
#define GPU_SUBMIT_FAKE(error, taken)
#endif
#ifndef GPU_OPEN_FAKE
#define GPU_OPEN_FAKE(fd)
#endif
// Every request this file makes of the kernel's DRM goes through GPU_CONTROL (a descriptor, the request, its record: 0 or a count when the kernel did it, else
// -errno), so that a check can put a kernel of its own under the whole path, and so that the path's dealings with a card are in one place.
#ifndef GPU_CONTROL
#define GPU_CONTROL(fd, request, a) system_control(fd, request, a)
#endif
#ifndef GPU_WIDE_FAKE
#define GPU_WIDE_FAKE(fd) // a check answers for the file that lets the other gfx10 families in
#endif
#ifndef GPU_SUBMIT_SEEN
#define GPU_SUBMIT_SEEN(list, count) // a check's copy of the buffers a submit names (gpu_run_job's bos)
#endif

// The two scene kernels a compiler made, gpu_cp and gpu_bx (ops 4 and 5), are words in a file of their own, which says when they go.
#include "gpu_compiled.inc"

// The kernels written by hand, src/canvas/gpu.asm, as `build gpu-kernels` assembles them: gpu_k_copy, gpu_k_fill, gpu_k_tone, gpu_k_glass and
// gpu_k_scene, each its rsrc1 and rsrc2 and then the code. The build names the file with -DMOONWATER_GPU_KERNELS; a program compiled by hand
// has it where ./build gpu-kernels writes it.
#ifdef MOONWATER_GPU_KERNELS
#include MOONWATER_GPU_KERNELS
#else
#include "../../artifacts/gpu_kernels.inc"
#endif

// Every kernel (rsrc1, rsrc2, then the code), put 256-aligned in the buffer from byte 1024, in the order of the ops that run them (4 and 5 the clang scenes, 6 the scene by hand for a width and a pitch of a multiple of 4, 7 a picture scaled by tables).
static const struct { const unsigned int *words; unsigned int size; } gpu_kernels[] = {
    {gpu_k_copy, sizeof(gpu_k_copy)}, {gpu_k_fill, sizeof(gpu_k_fill)}, {gpu_k_tone, sizeof(gpu_k_tone)}, {gpu_k_glass, sizeof(gpu_k_glass)},
    {gpu_cp, sizeof(gpu_cp)}, {gpu_bx, sizeof(gpu_bx)}, {gpu_k_scene, sizeof(gpu_k_scene)}, {gpu_k_scale, sizeof(gpu_k_scale)}};
#define GPU_KERNELS (sizeof(gpu_kernels) / sizeof(gpu_kernels[0]))
#define GPU_KERNEL_OF(op) (op) // the table is in the order of the ops
static unsigned int gpu_kernel_at[GPU_KERNELS];
typedef char gpu_kernels_fit[sizeof(gpu_k_copy) + sizeof(gpu_k_fill) + sizeof(gpu_k_tone) + sizeof(gpu_k_glass) + sizeof(gpu_cp) + sizeof(gpu_bx) + sizeof(gpu_k_scene) + sizeof(gpu_k_scale) + 256 * 8 <= GPU_BASES_AT - 1024 ? 1 : -1]; // they end before the bases begin

#define gpu_io(dir, n, size, a) (GPU_CONTROL(gpu.fd, GPU_IO(dir, n, size), a) == 0)
static b32 gpu_wait_asked(p64 *wait);
#ifndef GPU_WAIT_CS
#define GPU_WAIT_CS(wait) gpu_wait_asked(wait) // a check puts a fake fence here
#endif

// The card must draw what it should before anything is trusted to it: families of the ISA the kernels are in (RDNA2, gfx10.3: Van Gogh 144, Yellow Carp 146, Raphael 149,
// Mendocino 151) are tried, and each is held to the hash of the picture one small scene of every kind of op made on the card they were written on.
#define GPU_FAMILIES(f) ((f) == 144 || (f) == 146 || (f) == 149 || (f) == 151)
#define GPU_TEST_HASH 2024535671u
// The rest of the chips the kernels are code for, which nobody has run them on: family 143 (Navi 10, 12 and 14, RDNA1; Navi 21 to 24, the RDNA2 cards). llvm-mc
// makes of gpu.asm the same 3,018 words for gfx1010, gfx1012, gfx1030 and gfx1036 (the gate asks it: gpu-kernels-chips), so the code is theirs too as far as an assembler can say; what the dispatch's
// registers and the ring do on them has been seen by no one. They are let in only where the machine's owner asks (the file /run/moonwater/gpu-gfx10, looked for at
// each start, as gpu-nouserptr is), and then held to the self-test as every card is: a card that draws it wrong is not trusted, and one that hangs on it is the
// hang's two seconds and no card. Off unless asked: a job that hangs on a ring the kernel ends only after its own timeout (sixty seconds at v6.12) is a minute of
// the processor drawing on a machine whose owner did not choose to try.
#define GPU_FAMILIES_WIDE(f) ((f) == 143)
static b32 gpu_wide; // the owner asked for them
static b32 gpu_gfx103; // the open family-143 card has GFX 10.3 compute IP (Navi 21/22/23/24)
static b32 gpu_family_ok(unsigned int family)
{
        return GPU_FAMILIES(family) || ((gpu_wide || gpu_gfx103) && GPU_FAMILIES_WIDE(family));
}
// The prerequisites of the path, in the order it needs them. gpu_start stops at the first that fails and keeps it: the code, the kernel's error (its
// errno; 0 where the step failed by what it found, not by a call), and a bit in gpu_req.mask for each that passed. The node the checks looked at (its path,
// mode and owner), the driver's name and the family are kept whether or not that node was taken: moonwater debug says them.
enum gpu_req
{
        GPU_REQ_NONE,
        GPU_REQ_NODE,     // a render node opens (none does: the first one's error, and its mode and owner)
        GPU_REQ_DRIVER,   // a node's driver is amdgpu (the first node's name is kept; another driver has no GPU path yet)
        GPU_REQ_FAMILY,   // the family is one the kernels are for (the family and device id are kept)
        GPU_REQ_USERPTR,  // AMDGPU_GEM_USERPTR maps an anonymous page (19: a kernel without CONFIG_DRM_AMDGPU_USERPTR)
        GPU_REQ_CONTEXT,  // a GEM buffer, its VM map and a compute context
        GPU_REQ_RING,     // the compute ring finished the self-test's job (its error when it did not: the submit's, or 62 for a job still running)
        GPU_REQ_SELFTEST, // the card drew the self-test's picture as the kernels do
        GPU_REQ_UDMABUF,  // the dma-buf route, the fallback for a refused userptr: /dev/udmabuf makes a dma-buf of a sealed memfd page
        GPU_REQ_PRIME,    // and that dma-buf imports into the render node. Checked once the card is up (gpu_route_probe): not a step the card needs while userptr works
        GPU_REQ_COUNT
};
// first: the required step that failed (0 none; the card is not up); route: the dma-buf route's step that failed once the card is up (0 none; the card is up)
static struct { unsigned int first, error, mask, mode, owner, family, device, route, route_error, pick; char driver[8], node[20], bus[32]; } gpu_req;
static char gpu_display[32]; // the display card's bus (set by the compositor before the start: "" for a headless one, or one the kernel does not name)
#define GPU_NODES 8
static unsigned int gpu_last_error; // the last job's refusal: the submit's errno, 62 (ETIME) for one that hung (two seconds old, not done), 5 for a wait that failed
// How the last job came to be given up on (gpu_end; gpu_last_error is its errno). LATE: the wait's cap (a tick) passed with the job running: the job is only slow,
// it stays pending (gpu_idle holds the buffer) and the frame is drawn some other way, with no cool-down. HUNG: two seconds old and not done, or the kernel's log has
// a page fault since it was submitted. WAIT: the wait itself failed. A refused submit has none. gpu_late_end says a late job's end when it comes, once.
enum gpu_end
{
        GPU_END_NONE,
        GPU_END_LATE,
        GPU_END_HUNG,
        GPU_END_WAIT,
        GPU_END_OVERFLOW
};
static enum gpu_end gpu_end;   // the last call's
static enum gpu_end gpu_found; // the end of a late job that gpu_idle came on after the fact, until gpu_late_end has said it
static b32 gpu_owed;           // a late job is pending and its end is not yet said to the caller
static positive gpu_fault_poll; // when the kernel's log is next looked at for a page fault of the late job (monotonic ns)
static b32 gpu_patient;        // the self-test waits the hang's two seconds for its job, not a tick: a card just opened is slow to its first
#define GPU_LATE_NS 16000000ull      // a job not done this long after it was submitted is late: a tick (the heaviest whole-screen job measured, 8K, is 5.5 ms)
#define GPU_HANG_NS 2000000000ull    // and one not done this long after is hung. The kernel's own timeout for a job is amdgpu.lockup_timeout, which at v6.12 is sixty
                                     // seconds for the compute ring this uses: until it ends the job nothing here ends it, the buffer stays the job's (gpu_idle), and the
                                     // processor draws
#define GPU_FAULT_FIRST 250000000ull // a late job's first look at the kernel's log for a page fault is this long after it went late, and the next ones
#define GPU_FAULT_EVERY 500000000ull // this far apart

// A step of the prerequisites: passed (its bit in mask) or failed (the first failure and its error kept). GPU_STEP_FAKE is the check's fake card: it may
// make a step fail, with the error it gives; by default nothing is faked.
#ifndef GPU_STEP_FAKE
#define GPU_STEP_FAKE(code, failed, error)
#endif
static b32 gpu_req_step(enum gpu_req code, b32 failed, unsigned int error)
{
        GPU_STEP_FAKE(code, failed, error);
        if (failed)
        {
                if (!gpu_req.first)
                        gpu_req.first = code, gpu_req.error = error;
                return false;
        }
        gpu_req.mask |= 1u << code;
        return true;
}

// A request on a descriptor: 0 when the kernel did it, else its errno.
static unsigned int gpu_raw(int fd, unsigned int request, void *a)
{
        long r = GPU_CONTROL(fd, request, a);

        return r < 0 ? (unsigned int)-r & 255 : 0;
}
#define gpu_call(dir, n, size, a) gpu_raw(gpu.fd, GPU_IO(dir, n, size), a)
#define GPU_GEM_CLOSE 0x40086409u // DRM_IOW(0x09, struct drm_gem_close)

/*
        The card's driver, behind nine calls. Everything above them is any card's: the scene's records and their bins, the table of maps and of imports and
        their addresses, the tick and the marks, the late, hung and lost job, the self-test, the prerequisites. Everything below them is one driver's requests
        of the kernel and one chip's command words. A second driver, or a second chip of this one with other words, is a second struct gpu_backend and its
        kernels; nothing above the calls names amdgpu. (The requests every DRM driver has are not behind them: the version, a handle closed, a dma-buf to a
        handle and back.) Each call answers 0 or the kernel's errno unless it says otherwise.
*/
struct gpu_job
{
        const unsigned int *kernel; // the program's two resource words (the code follows them in the card's buffer)
        positive code, arguments;   // the card's addresses of the code and of the arguments' block
        unsigned int columns, rows; // the grid: columns of 64 pixels (a wave), rows of what the kernel takes a row to be
};
struct gpu_vm_fault
{
        positive addr;
        unsigned int status, hub;
};
enum
{
        GPU_USER_READ,    // pages the card may only read, taken at once
        GPU_USER_WRITTEN, // pages it may write, taken at once
        GPU_USER_LATER    // pages it may write, taken when a job first names them (the probe's question of a range the kernel would not take at once)
};
struct gpu_backend
{
        const char *driver; // the DRM driver's name (DRM_IOCTL_VERSION)
        // 1. the chip a descriptor is of: its family and device
        unsigned int (*identify)(int fd, unsigned int *family, unsigned int *device);
        // 2. the card's own buffer of size bytes, given the address va in the card's address space: its handle, and the offset its pages are mapped from
        unsigned int (*buffer)(positive size, positive va, unsigned int *handle, p64 *offset);
        // 3. a context for the jobs, and the one before it (0 for none) let go of
        unsigned int (*context)(unsigned int *made, unsigned int before);
        // 4. pages of the processor's as a buffer of the card's (userptr): its handle
        unsigned int (*user)(positive from, positive bytes, unsigned int how, unsigned int *handle);
        // 5. a buffer given an address, to read or to read and write, never to run; 6. and taken from it
        unsigned int (*bind)(unsigned int handle, positive va, positive bytes, b32 written);
        unsigned int (*unbind)(unsigned int handle, positive va, positive bytes);
        // 7. a job: the kernel run over the grid, naming the count buffers it may touch besides the card's own. Its sequence, to wait for
        unsigned int (*submit)(const struct gpu_job *job, p64 *handles, unsigned int count, positive *sequence);
        // 8. a job waited for until a time (monotonic ns): 1 done, 0 still running, -1 the wait itself failed
        int (*wait)(positive sequence, positive until);
        // 9. the last page fault of the card's address space, zeros for none
        unsigned int (*fault)(struct gpu_vm_fault *fault);
};

static unsigned int gpu_amd_identify(int fd, unsigned int *family, unsigned int *device)
{
        unsigned int who[16] = {0}, ip[8] = {0}, e;
        p64 ask[4] = {(p64)who, sizeof(who) | 0x16ull << 32}; // AMDGPU_INFO_DEV_INFO
        p64 ip_ask[4] = {(p64)ip, sizeof(ip) | 0x02ull << 32, 1ull, 0}; // AMDGPU_INFO_HW_IP_INFO (AMDGPU_HW_IP_COMPUTE = 1)

        gpu_gfx103 = 0;
        e = gpu_raw(fd, GPU_IO(1u, 5, 32), ask);
        *family = e ? 0 : who[4], *device = e ? 0 : who[0];
        if (!e && who[4] == 143 && !gpu_raw(fd, GPU_IO(1u, 5, 32), ip_ask) && ip[0] == 10 && ip[1] == 3 && who[2] >= 0x28)
                gpu_gfx103 = 1;
        return e;
}

static unsigned int gpu_amd_buffer(positive size, positive at, unsigned int *handle, p64 *offset)
{
        p64 make[4] = {size, 4096, 2, 64}, map[1] = {0}, va[8] = {0, 1 | 14ull << 32, at, 0, size};
        unsigned int e = gpu_call(3u, 0, 32, make); // GEM_CREATE, then GEM_MMAP for the offset, then GEM_VA: read, write and run

        if (!e)
                va[0] = map[0] = *handle = (unsigned int)make[0], e = gpu_call(3u, 1, 8, map);
        if (!e)
                *offset = map[0], e = gpu_call(1u, 8, 64, va);
        return e;
}

static unsigned int gpu_amd_context(unsigned int *made, unsigned int before)
{
        unsigned int ctx[4] = {1, 0, 0, 0}, old[4] = {2, 0, before, 0}; // AMDGPU_CTX_OP_ALLOC_CTX, and FREE_CTX of the one before
        unsigned int e = gpu_call(3u, 2, 16, ctx);

        if (e)
                return e;
        if (before)
                gpu_call(3u, 2, 16, old);
        *made = ctx[0];
        return 0;
}

// A map to read is registered read-only (AMDGPU_GEM_USERPTR_READONLY, 1, with the REGISTER and VALIDATE flags, 0xc): the pages of a client's pool are
// mapped read-only by the compositor, and a writable registration of them is refused (EPERM). Without VALIDATE (0x8) the kernel takes the pages later.
static unsigned int gpu_amd_user(positive from, positive bytes, unsigned int how, unsigned int *handle)
{
        p64 up[3] = {from, bytes, how == GPU_USER_READ ? 0xdull : how == GPU_USER_WRITTEN ? 0xcull : 0x8ull};
        unsigned int e = gpu_call(3u, 0x11, 24, up);

        *handle = e ? 0 : (unsigned int)(up[2] >> 32); // the kernel wrote the handle into the high half of up[2]
        return e;
}

static unsigned int gpu_amd_bind(unsigned int handle, positive at, positive bytes, b32 written)
{
        p64 va[8] = {handle, 1 | (written ? 6ull : 2ull) << 32, at, 0, bytes};

        return gpu_call(1u, 8, 64, va);
}

static unsigned int gpu_amd_unbind(unsigned int handle, positive at, positive bytes)
{
        p64 va[8] = {handle, 2, at, 0, bytes};

        return gpu_call(1u, 8, 64, va);
}

static unsigned int gpu_amd_submit(const struct gpu_job *job, p64 *bos, unsigned int named, positive *sequence)
{
        const positive code = job->code, args = job->arguments;
        const unsigned int address_to k = job->kernel;
        unsigned int ib[] = {
            // The caches that hold what the last job read -- the scalar and the instruction caches and the vector ones, with the arguments and the lists of ops the processor
            // rewrote since -- are emptied before the dispatch, as every driver does; a job that follows another read stale arguments one time in three without it.
            GPU_PKT(0x58, 6), 0, 0xffffffffu, 0xffffff, 0, 0, 10, 1 | 1 << 5 | 1 << 7 | 1 << 8 | 1 << 9 | 1 << 14,
            GPU_PKT(0x76, 2), 0x20c, (unsigned int)(code >> 8), (unsigned int)(code >> 40),
            // COMPUTE_STATIC_THREAD_MGMT_SE0, SE1, (COMPUTE_TMPRING_SIZE), SE2, SE3 (0x216 to 0x21a of the SH registers, gc_10_3_0_offset.h): every compute unit of every shader
            // engine enabled, so that no value an earlier job left in them decides which units this one runs on. COMPUTE_PGM_RSRC3 (0x228) is 0, no shared vector registers,
            // for the same reason.
            GPU_PKT(0x76, 2), 0x212, k[0], k[1], GPU_PKT(0x76, 5), 0x216, 0xffffffffu, 0xffffffffu, 0x100, 0xffffffffu, 0xffffffffu,
            GPU_PKT(0x76, 1), 0x228, 0,
            GPU_PKT(0x76, 6), 0x240, 0, 0, 0, 0, (unsigned int)args, (unsigned int)(args >> 32),
            GPU_PKT(0x76, 1), 0x215, 0, GPU_PKT(0x76, 3), 0x207, 64, 1, 1,
            GPU_PKT(0x15, 3), job->columns, job->rows, 1, 0x45};
        _Static_assert(sizeof(ib) <= 512, "the packets end where the arguments begin (GPU_ARGS)");
        // The job names the buffers it may touch besides the main one (the scanout's and the pages of windows, which are userptr buffers): the kernel pins and rebinds
        // the pages of those at the submission, which it does not for a buffer nobody named when the pages under it have moved.
        p64 boin[3] = {0, named | 8ull << 32, (positive)bos}, ibc[4] = {0, GPU_VA, sizeof(ib) | 1ull << 32, 0}, chunk[4] = {1 | 8ull << 32, (positive)ibc, 6 | 6ull << 32, (positive)boin},
            list[2] = {(positive)chunk, (positive)(chunk + 2)}, cs[3] = {gpu.ctx, named ? 2 : 1, (positive)list}; // the main buffer is valid for good (it needs no naming); a job that touches no other buffer names none
        unsigned int error = 0;
        b32 taken = false;

        memory_copy(gpu.at, ib, sizeof(ib));
        GPU_SUBMIT_FAKE(error, taken); // a check's fake submit answers an error before the call is made, or takes the job (the call is then not made)
        if (taken)
                cs[0] = 1; // the job's sequence number
        else if (error == 0)
                error = gpu_raw(gpu.fd, GPU_IO(3u, 4, 24), cs);
        *sequence = cs[0];
        return error;
}

// The wait asked of the kernel (AMDGPU_WAIT_CS), and asked again when a signal ended it (EINTR): a wait a signal cut short says nothing of the job, and the
// compositor is sent signals (a child that ended, a timer); taken as a wait that failed it was the card's hang, two seconds of the processor drawing and a
// restart, for a job that was running as it should. The time waited until is a time of the clock, so the wait asked again ends when the first would have.
static b32 gpu_wait_asked(p64 *wait)
{
        p64 asked[4];
        unsigned int e;

        do
                memory_copy(asked, wait, sizeof(asked)), e = gpu_call(3u, 9, 32, asked);
        while (e == 4);
        wait[0] = asked[0];
        return e == 0;
}

static int gpu_amd_wait(positive sequence, positive until)
{
        p64 wait[4] = {sequence, until, 1, (positive)gpu.ctx << 32}; // (the compute ring of the context)

        if (!GPU_WAIT_CS(wait))
                return -1;
        return wait[0] ? 0 : 1;
}

// AMDGPU_INFO_GPUVM_FAULT (query 0x23, the driver's version 3.55 and later): the last page fault of this descriptor's address space, struct drm_amdgpu_info_gpuvm_fault
// (the page's address, the fault register's status word, the hub: 0 the graphics hub, 1 and 2 the media hubs, the hub's index above the first byte). The kernel answers
// zeros when no fault is kept, and EINVAL without the query.
static unsigned int gpu_amd_fault(struct gpu_vm_fault *fault)
{
        p64 ask[4] = {(p64)fault, sizeof(*fault) | 0x23ull << 32};

        return gpu_call(1u, 5, 32, ask);
}

static const struct gpu_backend gpu_amd = {"amdgpu",       gpu_amd_identify, gpu_amd_buffer, gpu_amd_context, gpu_amd_user,
                                           gpu_amd_bind,   gpu_amd_unbind,   gpu_amd_submit, gpu_amd_wait,    gpu_amd_fault};
static const struct gpu_backend *gpu_backend = &gpu_amd; // the card's driver (one today)

// A handle of the card's let go of (any DRM driver's request).
static void gpu_close(unsigned int handle)
{
        p64 close[1] = {handle};

        GPU_CONTROL(gpu.fd, GPU_GEM_CLOSE, close);
}

// The node's mode and owner, from statx (the mode is at 28 of its record, the owner at 20 on every architecture).
static void gpu_req_stat(const char *path)
{
        unsigned char st[256] = {0};
        unsigned short mode = 0;
        unsigned int owner = 0;

        if (system_stat_at(AT_FDCWD, (string_address)path, 0, 0x7ffu, st) != 0)
                return;
        memory_copy(&mode, st + 28, 2), memory_copy(&owner, st + 20, 4);
        gpu_req.mode = mode & 07777u, gpu_req.owner = owner;
}

// The probe's answer for userptr: an anonymous page taken for the card by the kernel's userptr (validated, so the kernel takes the pages at once), then let go.
// 0 when it took it, else the errno.
static unsigned int gpu_userptr_error(positive at, unsigned int how)
{
        unsigned int handle = 0, e = gpu_backend->user(at, 4096, how, &handle);

        if (e == 0)
                gpu_close(handle);
        return e;
}

// A dma-buf of [offset, offset + size) of a sealed memfd, through /dev/udmabuf: its descriptor, or -errno.
static long gpu_dmabuf_make(int memfd, positive offset, positive size)
{
        struct { unsigned int memfd, flags; unsigned long offset, size; } create = {(unsigned int)memfd, 1, offset, size}; // UDMABUF_FLAGS_CLOEXEC
        long udmabuf = system_open_at(AT_FDCWD, (string_address) "/dev/udmabuf", FILE_READ_WRITE | O_CLOEXEC), made;

        if (udmabuf < 0)
                return udmabuf;
        made = GPU_CONTROL((int)udmabuf, 0x40187542u, &create); // _IOW('u', 0x42, struct udmabuf_create)
        system_close((int)udmabuf);
        return made;
}

// A dma-buf imported into the render node: its handle there, or -errno. The descriptor is closed either way (the handle holds the buffer).
static long gpu_dmabuf_in(int dmabuf)
{
        struct { unsigned int handle, flags; int fd; } prime = {0, 0, dmabuf};
        long made = GPU_CONTROL(gpu.fd, 3u << 30 | 12u << 16 | 0x642eu, &prime); // DRM_IOCTL_PRIME_FD_TO_HANDLE

        system_close(dmabuf);
        return made < 0 ? made : (long)prime.handle;
}

// A memfd page made sealed for the udmabuf driver (F_SEAL_SHRINK), its dma-buf: the descriptor, or -errno.
static long gpu_dmabuf_probe_make(void)
{
        long memfd = system_call_2(syscall(memfd_create), (positive) "moonwater-gpu", 1 | 2), made; // MFD_CLOEXEC | MFD_ALLOW_SEALING

        if (memfd < 0)
                return memfd;
        system_truncate_handle((int)memfd, 4096);
        system_call_3(syscall(fcntl), (positive)memfd, 1033, 2); // F_ADD_SEALS, F_SEAL_SHRINK
        made = gpu_dmabuf_make((int)memfd, 0, 4096);
        system_close((int)memfd);
        return made;
}

static void gpu_say_kernel(void);
static b32 gpu_idle(void);
static unsigned int gpu_test_hash(void);

// The dma-buf route's own prerequisites (udmabuf, then PRIME), checked once the card is up: a card whose userptr works does not need them, and one whose userptr
// is refused is served by them (the fallback). A step that fails is kept as gpu_req.route with its error; the card still composes.
static void gpu_route_probe(void)
{
        long made = gpu_dmabuf_probe_make();
        enum gpu_req step = GPU_REQ_UDMABUF;
        b32 failed = made < 0;
        unsigned int e = failed ? (unsigned int)-made & 255 : 0;

        GPU_STEP_FAKE(GPU_REQ_UDMABUF, failed, e);
        if (failed)
        {
                if (made >= 0)
                        system_close((int)made);
        }
        else
        {
                made = gpu_dmabuf_in((int)made); // closes the dma-buf
                step = GPU_REQ_PRIME, failed = made < 0, e = failed ? (unsigned int)-made & 255 : 0;
                GPU_STEP_FAKE(GPU_REQ_PRIME, failed, e);
                if (made >= 0)
                {
                        gpu_close((unsigned int)made); // the handle, let go
                }
        }
        if (failed)
                gpu_req.route = step, gpu_req.route_error = e;
        else
                gpu_req.mask |= 1u << GPU_REQ_UDMABUF | 1u << GPU_REQ_PRIME;
}

// Let go of the card: its buffer unmapped, its descriptor closed (the kernel frees the buffer, the context and the maps with it). After a start that failed,
// or when the card lost its state (gpu_recover).
static unsigned int *gpu_bin_stage;
static positive gpu_bin_room;

static void gpu_maps_reset(void);
static void gpu_kmsg_drain(void);

static void gpu_stop(void)
{
        gpu_maps_reset();
        gpu_shared.fd = -1, gpu_shared.at = 0, gpu_shared.bytes = 0, gpu_shared.pin = 0;
        if (gpu_bin_stage)
                system_call_2(syscall(munmap), (positive)gpu_bin_stage, gpu_bin_room);
        gpu_bin_stage = 0, gpu_bin_room = 0;
        if (gpu.at)
                system_call_2(syscall(munmap), (positive)gpu.at, gpu.size);
        if (gpu.fd >= 0)
                system_close(gpu.fd);
        memory_zero(&gpu, sizeof(gpu));
        gpu.fd = -1, gpu_owed = 0, gpu_found = GPU_END_NONE;
        gpu_epoch++;
}

// The first card that is one the kernels are for, with bytes of pixels behind the page of commands, and one context. Every prerequisite is a step (gpu_req):
// the node, the driver, the family, userptr, a dma-buf through udmabuf and PRIME, the buffer and context, the self-test's picture and the ring that drew it.
// False if any step is refused, the first kept in gpu_req; a card that another driver or family has is passed over, and not hung by code that is not its own.
static unsigned int gpu_text_put(char *to, unsigned int at, unsigned int room, const char *words);
static unsigned int gpu_text_num(char *to, unsigned int at, unsigned int room, positive value, unsigned int base);

// A device's name on its bus ("0000:c5:00.0"), from the text that names the device: the last part of a path (sysfs's link from a node to its device,
// "../../../0000:c5:00.0"), or a bus as the kernel's DRM says it, with its kind before it ("pci:0000:c5:00.0"). The text is length bytes and no 0 after them
// is counted on. Into, 0-ended; false, and "", for a text with no name or a name that does not fit.
static b32 gpu_bus_name(const char *text, positive length, char *into, unsigned int size)
{
        positive from = 0;

        into[0] = 0;
        while (length && (text[length - 1] == '/' || text[length - 1] == '\n' || !text[length - 1]))
                length--;
        for (positive i = 0; i < length; i++)
                if (text[i] == '/')
                        from = i + 1;
        if (length - from > 4 && !memory_compare(text + from, "pci:", 4))
                from += 4;
        if (from >= length || length - from >= size || text[from] == '.') // (a path's "." and ".." name no device)
                return false;
        memory_copy(into, text + from, length - from), into[length - from] = 0;
        return true;
}

// The device a DRM descriptor is of, by its name on its bus (gpu_bus_name): the same for the card's node and its render node, which is how the render node
// of the display's own card is told (gpu_pick). Asked of sysfs first, where the node's device number names its device (/sys/dev/char/226:128/device), since
// that is there for both kinds of node; then of DRM_IOCTL_GET_UNIQUE, which the kernel answers on the card's node alone (a render node is refused it, EACCES)
// and with no 0 after the name: the name ends at the length the kernel writes back, not at what the bytes after it happen to hold. "" when neither says.
static b32 gpu_bus_of(int fd, char *into, unsigned int size)
{
        unsigned char st[256] = {0};
        char path[48] = "/sys/dev/char/", link[256];
        unsigned int major = 0, minor = 0, n;
        p64 unique[2] = {sizeof(link), (p64)link};
        long got;

        into[0] = 0;
        if (system_stat_at(fd, (string_address) "", AT_EMPTY_PATH, 0x7ffu, st) == 0)
        {
                memory_copy(&major, st + 128, 4), memory_copy(&minor, st + 132, 4); // statx's stx_rdev_major and stx_rdev_minor
                n = gpu_text_num(path, 14, sizeof(path), major, 10), n = gpu_text_put(path, n, sizeof(path), ":");
                n = gpu_text_num(path, n, sizeof(path), minor, 10), gpu_text_put(path, n, sizeof(path), "/device");
                got = (long)system_read_link_at(AT_FDCWD, (string_address)path, link, sizeof(link));
                if (got > 0 && got <= (long)sizeof(link) && gpu_bus_name(link, (positive)got, into, size))
                        return true;
        }
        if (gpu_raw(fd, 0xc0106401u, unique) != 0 || unique[0] > sizeof(link)) // DRM_IOCTL_GET_UNIQUE: a name longer than the room is not copied
                return false;
        return gpu_bus_name(link, unique[0], into, size);
}

// Which candidate render node the card is (the nodes of the card's family, their buses): the one on the display's own device, else the first. why: 1 the display's
// own device, 2 the display is on another device (the card composes only if its PRIME import of the scanout works: gpu_output says so), 0 no display's bus known.
static int gpu_pick(char candidate[][32], unsigned int count, const char *display, unsigned int *why)
{
        *why = 0;
        if (!count)
                return -1;
        for (unsigned int i = 0; display[0] && i < count; i++)
                if (string_equals((string_address)candidate[i], (string_address)display))
                        return *why = 1, (int)i;
        *why = display[0] ? 2 : 0;
        return 0;
}

static b32 gpu_start(positive bytes)
{
        positive size = (GPU_DATA + bytes + (1 << 20) + 4095) & ~4095ull; // the last megabyte is the self-test's
        p64 offset = 0;
        unsigned int ctx = 0, family = 0, device = 0, e = 0, open_error = 0;
        char path[] = "/dev/dri/renderD128";
        int named = 0, amd = 0, amd_named = 0;
        unsigned int candidates = 0, candidate_node[GPU_NODES];
        char candidate_bus[GPU_NODES][32] = {{0}}; // (whole: the one picked is copied to the table every program reads, all 32 bytes of it)
        long at, hash;

        memory_zero(&gpu_req, sizeof(gpu_req));
        gpu.fd = -1, gpu.broken = 0, gpu_bytes = bytes, gpu_lost = 0, gpu_gfx103 = 0;
        // The test's switch (/run/moonwater/gpu-nouserptr): the table's pages go by the dma-buf route, and no userptr is asked for them.
        {
                long off = system_open_at(AT_FDCWD, (string_address) "/run/moonwater/gpu-nouserptr", FILE_READ | O_CLOEXEC);

                gpu_userptr_off = off >= 0, gpu_userptr_gone = 0;
                if (off >= 0)
                        system_close((int)off);
                off = system_open_at(AT_FDCWD, (string_address) "/run/moonwater/gpu-gfx10", FILE_READ | O_CLOEXEC);
                GPU_WIDE_FAKE(off);
                gpu_wide = off >= 0;
                if (off >= 0)
                        system_close((int)off);
        }
        for (unsigned int node = 0; node < 64; node++)
        {
                // Made again for each node: the kernel writes back the whole length of the driver's name, and no 0 after what it copies, so a node after
                // "i915" was read as "amdg" (never ours) and one after "virtio_gpu" was truncated at 7 bytes before this buffer was widened.
                char name[32] = "";
                p64 ver[8] = {0, 0, sizeof(name) - 1, (p64)name};
                long fd;
                unsigned int family_error;
                b32 version;

                path[17] = (char)('0' + (28 + node) / 10), path[18] = (char)('0' + (28 + node) % 10);
                fd = system_open_at(AT_FDCWD, (string_address)path, 2 | O_CLOEXEC);
                GPU_OPEN_FAKE(fd);
                if (fd < 0)
                {
                        unsigned int err = (unsigned int)-fd & 255;

                        if (node == 0 || (open_error == 2 && err != 2))
                                open_error = err, gpu_req_stat(path), memory_copy(gpu_req.node, path, sizeof(path));
                        continue;
                }
                version = gpu_raw((int)fd, 3u << 30 | 64u << 16 | 0x6400u, ver) == 0;
                if (!named) // the first node that opens is the one the table names when no amdgpu node is found
                        named = 1, memory_copy(gpu_req.node, path, sizeof(path)), gpu_req_stat(path), memory_copy(gpu_req.driver, name, sizeof(gpu_req.driver) - 1);
                if (!version || !string_equals((string_address)name, (string_address)gpu_backend->driver))
                {
                        system_close((int)fd);
                        continue;
                }
                // An amdgpu node: the first of them is the one the table names, whether or not its family is one the kernels are for.
                family_error = gpu_backend->identify((int)fd, &family, &device);
                if (!amd_named)
                {
                        amd_named = 1, memory_copy(gpu_req.node, path, sizeof(path)), gpu_req_stat(path), memory_copy(gpu_req.driver, name, sizeof(gpu_req.driver) - 1);
                        gpu_req.family = family, gpu_req.device = device;
                }
                amd = 1;
                // The candidates are the amdgpu nodes of a family the kernels are for; the one on the display's own device is taken (gpu_pick), else the first.
                if (!family_error && gpu_family_ok(family) && candidates < GPU_NODES)
                        candidate_node[candidates] = node, gpu_bus_of((int)fd, candidate_bus[candidates], sizeof(candidate_bus[0])), candidates++;
                system_close((int)fd);
        }
        if (candidates)
        {
                int pick = gpu_pick(candidate_bus, candidates, gpu_display, &gpu_req.pick);
                long picked;

                path[17] = (char)('0' + (28 + candidate_node[pick]) / 10), path[18] = (char)('0' + (28 + candidate_node[pick]) % 10);
                picked = system_open_at(AT_FDCWD, (string_address)path, 2 | O_CLOEXEC);
                if (picked >= 0)
                {
                        gpu.fd = (int)picked;
                        if (gpu_backend->identify(gpu.fd, &family, &device) == 0 && gpu_family_ok(family))
                        {
                                gpu.family = family, gpu.device = device;
                                memory_copy(gpu_req.node, path, sizeof(path)), gpu_req_stat(path), memory_copy(gpu_req.bus, candidate_bus[pick], sizeof(gpu_req.bus));
                                gpu_req.family = gpu.family, gpu_req.device = gpu.device;
                        }
                        else
                                system_close(gpu.fd), gpu.fd = -1;
                }
                else
                        named = 0, open_error = (unsigned int)-picked & 255, gpu_req_stat(path), memory_copy(gpu_req.node, path, sizeof(path));
        }
        gpu.family = gpu_req.family, gpu.device = gpu_req.device;
        do
        {
                if (!gpu_req_step(GPU_REQ_NODE, !named, open_error))
                        break;
                if (!gpu_req_step(GPU_REQ_DRIVER, !amd, 0))
                        break;
                if (!gpu_req_step(GPU_REQ_FAMILY, gpu.fd < 0, 0))
                        break;
                // Userptr: an anonymous page the kernel takes for the card; if userptr is refused, try udmabuf+PRIME fallback before aborting init.
                {
                        long page = system_call_6(syscall(mmap), 0, 4096, 3, 0x22, (positive)-1, 0);

                        e = page < 0 && page > -4096 ? 12 : gpu_userptr_error((positive)page, GPU_USER_WRITTEN);
                        if (!(page < 0 && page > -4096))
                                system_call_2(syscall(munmap), (positive)page, 4096);
                        if (e != 0)
                        {
                                gpu_route_probe();
                                if ((gpu_req.mask & (1u << GPU_REQ_UDMABUF | 1u << GPU_REQ_PRIME)) == (1u << GPU_REQ_UDMABUF | 1u << GPU_REQ_PRIME))
                                        gpu_userptr_gone = 1, e = 0;
                        }
                        if (!gpu_req_step(GPU_REQ_USERPTR, e != 0, e))
                                break;
                }
                // The buffer, its VM map and the compute context: all of the card's own memory.
                e = gpu_backend->buffer(size, GPU_VA, &gpu.handle, &offset);
                if (!e)
                        e = gpu_backend->context(&ctx, 0);
                if (!e)
                {
                        at = system_call_6(syscall(mmap), 0, size, 3, 1, (positive)gpu.fd, offset);
                        if (at < 0 && at > -4096)
                                e = (unsigned int)-at & 255;
                        else
                                gpu.at = (unsigned int address_to)at;
                }
                if (!gpu_req_step(GPU_REQ_CONTEXT, e != 0, e))
                        break;
                gpu.ctx = ctx, gpu.size = size;
                for (unsigned int i = 0, here = 1024; i < GPU_KERNELS; i++)
                {
                        gpu_kernel_at[i] = here;
                        memory_copy((p8 address_to)gpu.at + here, gpu_kernels[i].words + 2, gpu_kernels[i].size - 8);
                        here = (here + gpu_kernels[i].size - 8 + 255) & ~255u;
                }
                // The self-test: the card must finish its job (RING) and draw the picture the kernels draw (SELFTEST).
                hash = gpu_test_hash();
                if (!gpu_req_step(GPU_REQ_RING, hash == 0, gpu_last_error))
                {
                        gpu_say_kernel();
                        break;
                }
                if (!gpu_req_step(GPU_REQ_SELFTEST, hash != GPU_TEST_HASH, 0))
                {
                        gpu.broken = 1;
                        break;
                }
                if ((gpu_req.mask & (1u << GPU_REQ_UDMABUF | 1u << GPU_REQ_PRIME)) != (1u << GPU_REQ_UDMABUF | 1u << GPU_REQ_PRIME))
                        gpu_route_probe();
                gpu_kmsg_drain();
                return true;
        } while (0);
        gpu_stop();
        return false;
}

// Pages of the processor's (a window's picture: a memfd's, mapped shared) the GPU can reach, as userptr buffers at addresses of their own: gpu_map widens
// [addr, addr + size) to pages and maps them; gpu_va_bytes answers the GPU's address of a pointer that is inside one that holds bytes from it, 0 for none, and marks
// the map as used in this tick (a reader of more than a byte asks for all it reads: a map that holds the first is not one that holds the rest); gpu_trim lets go
// of the maps that were not used in the last few ticks (one tick a frame).
// 256 maps, so that a desktop of forty windows and their band, with a drag's and a landing's transient pictures, can be held in one tick (64 was the room a
// present had, and a present with no room for a picture is the processor's). A map this tick does not use is not named by its job: no cost to hold it.
#define GPU_MAPS 256
// dma: made by the dma-buf route (offset: its place in the table's memfd); bytes: the span of its GPU address (from and to are 0 for a map forgotten);
// handed: its address was given to a record that no job has run yet (gpu_run clears it, the trim does not): the next job names the map whatever the tick (gpu_map_named),
// and a written map is not put over it in the tick it was given (gpu_map_as); listed: the serial (gpu_submit_serial) of the last submit whose bo list named it, 0 for none.
static struct { positive from, to, va, bytes; unsigned int handle, used; b32 written; b32 dma; b32 handed; unsigned int listed; positive offset; void (*pin)(positive offset, b32 on); } gpu_maps[GPU_MAPS];
static unsigned int gpu_map_count, gpu_tick, gpu_submit_serial; // the submits made, counted from 1: a fault says whether a map was in the bo list of the last

static void gpu_maps_unpin_all(void)
{
        for (unsigned int i = 0; i < gpu_map_count; i++)
                if (gpu_maps[i].dma && gpu_maps[i].pin)
                        gpu_maps[i].pin(gpu_maps[i].offset, 0);
}

// The maps and imports let go of, the last GPU_GONE: a fault's address is named by what it was in, and by why it was let go of and how many ticks ago
// (gpu_fault_say). Each reason is one way a map or an import leaves the table.
enum gpu_gone_why
{
        GPU_GONE_NONE,
        GPU_GONE_TRIM,     // unused for 120 ticks (gpu_trim)
        GPU_GONE_FORGET,   // its pages were let go of by the compositor (gpu_forget), and it was dropped once the card was idle
        GPU_GONE_OVERLAP,  // a picture the card writes was mapped over it (gpu_map_as)
        GPU_GONE_EVICT,    // let go of for room, every map held (gpu_map_as)
        GPU_GONE_UNMAPPED, // its pages were not all there at a refused job (gpu_drop_unmapped)
        GPU_GONE_RECOVER,  // the card was started again (gpu_recover)
        GPU_GONE_RELEASE,  // an import released, and dropped once the card was idle (gpu_release_import)
        GPU_GONE_UNIMPORT, // an import let go of at once (gpu_unimport)
        GPU_GONE_WHY_COUNT
};
static const string_address gpu_gone_words[GPU_GONE_WHY_COUNT] = {
    "never", "by the 120-tick trim", "with its pages let go of", "by overlap with a written map", "to make room (every map held)",
    "as its pages were gone (msync)", "by the card's restart", "as a released import", "as an unimported buffer"};
#define GPU_GONE 16
static struct { positive va, bytes, from, to; unsigned int handle, why, tick, listed; b32 written, dma, import; } gpu_gone[GPU_GONE];
static unsigned int gpu_gone_at; // the ring's next slot is gpu_gone_at % GPU_GONE (the ones before it are the last)
static void gpu_gone_put(positive va, positive bytes, positive from, positive to, unsigned int handle, enum gpu_gone_why why, b32 written, b32 dma, b32 import,
                         unsigned int listed)
{
        unsigned int at = gpu_gone_at++ % GPU_GONE;

        gpu_gone[at].va = va, gpu_gone[at].bytes = bytes, gpu_gone[at].from = from, gpu_gone[at].to = to, gpu_gone[at].handle = handle;
        gpu_gone[at].why = (unsigned int)why, gpu_gone[at].tick = gpu_tick, gpu_gone[at].written = written, gpu_gone[at].dma = dma, gpu_gone[at].import = import;
        gpu_gone[at].listed = listed;
}

// The card's fault: a job that failed (ETIME, EIO or a refused submit) read the kernel's log for the first page fault since it was submitted and asked the kernel for the
// last fault of the card's address space (gpu_fault_read), and gpu_fault says what that address was in and whether the job's bo list had it (gpu_fault_note). "" until a
// job fails; gpu_faults counts the jobs that read the log. The board's table holds the same bytes (window.c's gpu_fault).
#define GPU_FAULT_BYTES 320
static char gpu_fault[GPU_FAULT_BYTES];
static unsigned int gpu_faults, gpu_job_error;
static positive gpu_job_began, gpu_fault_next; // when the job being run was submitted, and when a refused submit may read the log again (monotonic ns)

// Text into a buffer at `at`, with its null kept within room: the end of it.
static unsigned int gpu_text_put(char *to, unsigned int at, unsigned int room, const char *words)
{
        while (*words && at + 1 < room)
                to[at++] = *words++;
        to[at] = 0;
        return at;
}
static unsigned int gpu_text_num(char *to, unsigned int at, unsigned int room, positive value, unsigned int base)
{
        char digits[24];
        unsigned int n = 0;

        do
                digits[n++] = "0123456789abcdef"[value % base], value /= base;
        while (value && n < sizeof(digits));
        while (n && at + 1 < room)
                to[at++] = digits[--n];
        to[at] = 0;
        return at;
}
// The metrics the compositor publishes (window.c's metric_*): the lookups that found a map and that did not, the card's clock-ramp wakes, and its
// jobs with their total wait (gpu_run).
static unsigned int gpu_map_hits, gpu_map_misses, gpu_wakes, gpu_jobs;
static unsigned long long gpu_job_ns;
#ifndef GPU_MAP_FAKE
#define GPU_MAP_FAKE(taken)
#endif
// Why the last map was refused (gpu_map_as): 1 all the maps are held, 2 no address for it, 3 the kernel would not take the pages (userptr), 4 it would not give them
// their address, 9 a written map over one a record of this tick was given, 10 a map would be let go of for it while a job given up on may still read it; for 3
// and 4 the error (the kernel's, negated) is in the bits above the first byte. moonwater canvas says it with the card's refusals.
static unsigned int gpu_map_why;
#define GPU_VA_MAPS 0x2000000000ull
#define GPU_VA_END (1ull << 40) // a close op's record holds 40 bits of an address: no map or import ends past it
static positive gpu_map_next = GPU_VA_MAPS;

static void gpu_maps_reset(void)
{
        gpu_maps_unpin_all();
        gpu_map_count = 0, gpu_map_next = GPU_VA_MAPS;
}

// Addresses for bytes, none of a map's or an import's that is held: after the last ones handed out, or, where they would end past GPU_VA_END (a long
// session's maps made and let go of), the first gap from the start; 0 for none.
static positive gpu_place(positive bytes)
{
        positive at = gpu_map_next + bytes <= GPU_VA_END ? gpu_map_next : GPU_VA_MAPS;

        for (unsigned int i = 0; i < gpu_map_count + gpu.imports; i++)
        {
                positive from = i < gpu_map_count ? gpu_maps[i].va : gpu.imported_va[i - gpu_map_count];
                positive to = from + (i < gpu_map_count ? gpu_maps[i].bytes : gpu.imported_bytes[i - gpu_map_count]); // a forgotten map keeps its span

                if (at < to && from < at + bytes)
                        at = to, i = ~0u; // past it, and each held one looked at again
        }
        return at + bytes <= GPU_VA_END ? at : 0;
}

// The card lost its state (gpu_run's submit said so: a reset that took the context, or a VM that is gone, a resume that left it dead): every map and
// import let go of (the kernel frees them with the descriptor), and the card started again as at the first start: a new context, the self-test. False if it does
// not start again; the caller gives it up for a while (the late-probe retry starts it when it is due).
static b32 gpu_recover(void)
{
        int kept_fd = gpu_shared.fd;
        unsigned char *kept_at = gpu_shared.at;
        positive kept_bytes = gpu_shared.bytes;
        void (*kept_pin)(positive offset, b32 on) = gpu_shared.pin;

        for (unsigned int i = 0; i < gpu_map_count; i++)
        {
                gpu_gone_put(gpu_maps[i].va, gpu_maps[i].bytes, gpu_maps[i].from, gpu_maps[i].to, gpu_maps[i].handle, GPU_GONE_RECOVER, gpu_maps[i].written, gpu_maps[i].dma, 0,
                             gpu_maps[i].listed);
                if (gpu_maps[i].dma && gpu_maps[i].pin)
                        gpu_maps[i].pin(gpu_maps[i].offset, 0);
        }
        gpu_map_count = 0, gpu_map_next = GPU_VA_MAPS, gpu_lost = 0, gpu_recovered++;
        gpu_stop();
        gpu_shared.fd = kept_fd, gpu_shared.at = kept_at, gpu_shared.bytes = kept_bytes, gpu_shared.pin = kept_pin;
        return gpu_start(gpu_bytes);
}

// The map at i let go of, for the reason why: unmapped (its whole span), its buffer closed, the last map in its place.
static void gpu_drop(unsigned int i, enum gpu_gone_why why)
{
        gpu_gone_put(gpu_maps[i].va, gpu_maps[i].bytes, gpu_maps[i].from, gpu_maps[i].to, gpu_maps[i].handle, why, gpu_maps[i].written, gpu_maps[i].dma, 0, gpu_maps[i].listed);
        gpu_backend->unbind(gpu_maps[i].handle, gpu_maps[i].va, gpu_maps[i].bytes);
        gpu_close(gpu_maps[i].handle);
        if (gpu_maps[i].dma && gpu_maps[i].pin)
                gpu_maps[i].pin(gpu_maps[i].offset, 0);
        gpu_maps[i] = gpu_maps[--gpu_map_count];
}

// With every map held, the one used longest ago goes first (a window dragged larger makes a map at each size, faster than gpu_trim lets them go), unless all
// are this tick's. The card may only read a map (a window's picture is a program's pages): a write of a kernel past what its records name faults there and
// is dropped; gpu_map_written is for the one the GPU draws into (a headless scanout). No map can be run as code.
// What a refused map leaves to say why, once for each way it was refused (gpu_map_why): the node asked a plain question (does it answer at all), a userptr over a page of
// anonymous memory and over the first page of the refused range (whole, and without the validation that takes the pages at once) tried and their errors kept, and the
// last lines the kernel's log has about the card (/dev/kmsg gives a record a read) kept. moonwater debug shows them, so a machine nobody can sit at can say why.
static unsigned int gpu_map_probe, gpu_map_said;
static char gpu_said[256];

static void gpu_say_kernel(void)
{
        static char record[8192], last[3][90];
        unsigned int kept = 0, at;
        long handle = (long)system_open_at(AT_FDCWD, (string_address) "/dev/kmsg", FILE_READ | O_NONBLOCK | O_CLOEXEC);

        if (handle < 0)
                return;
        for (unsigned int records = 0; records < 8192; records++)
        {
                bipolar got = system_read_once(handle, record, sizeof(record) - 1);
                char *text, *finish;

                if (got <= 0)
                        break;
                record[got] = 0;
                text = (char *)string_find((string_address)record, (string_address) ";");
                if (!text)
                        continue;
                text++;
                for (finish = text; *finish && *finish != '\n'; finish++)
                        ;
                *finish = 0;
                if (!string_find((string_address)text, (string_address)gpu_backend->driver) && !string_find((string_address)text, (string_address) "drm"))
                        continue;
                string_copy_bounded((string_address)last[kept++ % 3], (string_address)text, sizeof(last[0]));
        }
        system_close(handle);
        gpu_said[0] = 0;
        for (unsigned int i = kept > 3 ? kept - 3 : 0; i < kept; i++)
        {
                at = (unsigned int)string_length((string_address)gpu_said);
                if (at && at + 3 < sizeof(gpu_said))
                        gpu_said[at++] = ' ', gpu_said[at++] = '|', gpu_said[at++] = ' ', gpu_said[at] = 0;
                string_copy_bounded((string_address)gpu_said + at, (string_address)last[i % 3], sizeof(gpu_said) - at);
        }
}

static void gpu_map_refused(positive from)
{
        static unsigned char *page;
        unsigned int family, device;

        if (gpu_map_why == gpu_map_said)
                return;
        gpu_map_said = gpu_map_why;
        if (!page)
        {
                long made = system_call_6(syscall(mmap), 0, 4096, 3, 0x22, (positive)-1, 0);

                if (!(made < 0 && made > -4096))
                        page = (unsigned char *)made, page[0] = 1;
        }
        gpu_map_probe = (gpu_backend->identify(gpu.fd, &family, &device) == 0 ? 1u : 0u) | (page ? gpu_userptr_error((positive)page, GPU_USER_WRITTEN) : 255u) << 8 |
                        gpu_userptr_error(from, GPU_USER_WRITTEN) << 16 | gpu_userptr_error(from, GPU_USER_LATER) << 24;
        gpu_say_kernel();
}

#define gpu_map(addr, size) gpu_map_as(addr, size, 0)
#define gpu_map_written(addr, size) gpu_map_as(addr, size, 1)
// The dma-buf route (the fallback for a refused userptr): the pages [from, to) of the window table's memfd, made a dma-buf by udmabuf, imported into the render
// node and mapped at at. False, with gpu_map_why: 5 the pages are not the table's (or there is no route), 6 udmabuf refused (the error above the first byte),
// 7 the import refused, 8 the map refused. The pin goes on for the pages the card holds, and off when that map is let go of (gpu_drop).
static b32 gpu_map_dmabuf(positive from, positive to, positive at, b32 written)
{
        int fd = -1;
        positive base = 0;
        void (*pin)(positive offset, b32 on) = 0;
        long made;
        unsigned int e, handle;

        for (unsigned int i = 0; i < GPU_MEMFDS; i++)
                if (gpu_memfds[i].fd >= 0 && gpu_memfds[i].at && from >= (positive)gpu_memfds[i].at && to <= (positive)gpu_memfds[i].at + gpu_memfds[i].bytes)
                {
                        fd = gpu_memfds[i].fd, base = (positive)gpu_memfds[i].at, pin = gpu_memfds[i].pin;
                        break;
                }
        if (fd < 0 && gpu_shared.fd >= 0 && gpu_shared.at && from >= (positive)gpu_shared.at && to <= (positive)gpu_shared.at + gpu_shared.bytes)
                fd = gpu_shared.fd, base = (positive)gpu_shared.at, pin = gpu_shared.pin;
        if (fd < 0 || !base)
                return gpu_map_why = 5, false;
        made = gpu_dmabuf_make(fd, from - base, to - from);
        if (made < 0)
                return gpu_map_why = 6 | (unsigned int)-made << 8, false;
        made = gpu_dmabuf_in((int)made);
        if (made < 0)
                return gpu_map_why = 7 | (unsigned int)-made << 8, false;
        handle = (unsigned int)made;
        if ((e = gpu_backend->bind(handle, at, to - from, written)) != 0)
                return gpu_close(handle), gpu_map_why = 8 | e << 8, false;
        gpu_maps[gpu_map_count].from = from, gpu_maps[gpu_map_count].to = to, gpu_maps[gpu_map_count].va = at, gpu_maps[gpu_map_count].written = written;
        gpu_maps[gpu_map_count].handle = handle, gpu_maps[gpu_map_count].used = gpu_tick, gpu_maps[gpu_map_count].bytes = to - from;
        gpu_maps[gpu_map_count].handed = 0, gpu_maps[gpu_map_count].listed = 0, gpu_maps[gpu_map_count].dma = 1, gpu_maps[gpu_map_count].offset = from - base;
        gpu_maps[gpu_map_count].pin = pin;
        gpu_map_count++, gpu_maps_dma++;
        if (pin)
                pin(from - base, 1);
        gpu_map_next = at + (to - from);
        return true;
}

static b32 gpu_map_as(const void *addr, positive size, b32 written)
{
        positive from = (positive)addr & ~4095ull, to = ((positive)addr + size + 4095) & ~4095ull, at;
        unsigned int handle = 0, e;
        unsigned int old = 0;
        long taken = 0;

        // A map the card writes is the only map of its pages: neither read nor written may overlap an active handed map of opposite or written kind,
        // so a written map never lets read-only lookups write through, and a read map never aliases an in-flight written map.
        for (unsigned int i = 0; i < gpu_map_count; i++)
                if ((written || gpu_maps[i].written) && from < gpu_maps[i].to && gpu_maps[i].from < to && gpu_maps[i].handed && gpu_maps[i].used == gpu_tick)
                        return gpu_map_why = 9, false; // (the records of an earlier tick are not run again: no job need wait for them)
        // A map is let go of here (overlapping an existing map when either is written, or the oldest for room) only when no job given up on may still read it:
        // else the map asked for is refused (10) and the processor draws, as it does for every present while that job runs.
        for (unsigned int i = 0; i < gpu_map_count; i++)
                if ((written || gpu_maps[i].written) && from < gpu_maps[i].to && gpu_maps[i].from < to)
                {
                        if (!gpu_idle())
                                return gpu_map_why = 10, false;
                        gpu_drop(i--, GPU_GONE_OVERLAP);
                }

        for (unsigned int i = 1; gpu_map_count == GPU_MAPS && i < GPU_MAPS; i++)
                old = gpu_tick - gpu_maps[i].used > gpu_tick - gpu_maps[old].used ? i : old;
        if (gpu_map_count == GPU_MAPS && gpu_maps[old].used != gpu_tick)
        {
                if (!gpu_idle())
                        return gpu_map_why = 10, false;
                gpu_drop(old, GPU_GONE_EVICT);
        }
        if (gpu_map_count == GPU_MAPS)
                return gpu_map_why = 1, false;
        if (!(at = gpu_place(to - from)))
                return gpu_map_why = 2, false;
        // The userptr, unless the kernel has refused it before or the file says not to ask: then the table's pages go by the dma-buf route, and the rest are refused.
        if (!gpu_userptr_off && !gpu_userptr_gone)
        {
                taken = -(long)gpu_backend->user(from, to - from, written ? GPU_USER_WRITTEN : GPU_USER_READ, &handle);
                GPU_MAP_FAKE(taken);
                if (taken == 0)
                {
                        if ((e = gpu_backend->bind(handle, at, to - from, written)) != 0)
                                return gpu_close(handle), gpu_map_why = 4 | e << 8, false; // the buffer with it
                        gpu_maps[gpu_map_count].from = from, gpu_maps[gpu_map_count].to = to, gpu_maps[gpu_map_count].va = at, gpu_maps[gpu_map_count].written = written;
                        gpu_maps[gpu_map_count].handle = handle, gpu_maps[gpu_map_count].used = gpu_tick, gpu_maps[gpu_map_count].dma = 0;
                        gpu_maps[gpu_map_count].bytes = to - from, gpu_maps[gpu_map_count].handed = 0, gpu_maps[gpu_map_count].listed = 0, gpu_maps[gpu_map_count].pin = 0;
                        gpu_map_count++, gpu_maps_user++;
                        gpu_map_next = at + (to - from);
                        return true;
                }
                if (taken == -19 || taken == -22 || taken == -95 || taken == -38 || taken == -14 || taken == -1) // ENODEV, EINVAL, EOPNOTSUPP, ENOSYS, EFAULT, EPERM
                        gpu_userptr_gone = 1;
                else
                {
                        gpu_map_why = 3 | (unsigned int)-taken << 8;
                        gpu_map_refused(from);
                        return false;
                }
        }
        return gpu_map_dmabuf(from, to, at, written);
}

// (The map found last is looked at first: a present asks for the same few pictures over and over, and the table is up to 256 maps looked at in order. The
// place kept is only where the looking starts: the map there is held to the pointer as any other is, so a table that has moved under it costs a look, not an answer.)
static unsigned int gpu_map_last;
static positive gpu_va_bytes(const void *pointer, positive bytes)
{
        for (unsigned int k = 0, i = gpu_map_last < gpu_map_count ? gpu_map_last : 0; k < gpu_map_count; k++, i = i + 1 < gpu_map_count ? i + 1 : 0)
                if ((positive)pointer >= gpu_maps[i].from && (positive)pointer < gpu_maps[i].to && bytes <= gpu_maps[i].to - (positive)pointer) // no sum to wrap
                {
                        gpu_maps[i].used = gpu_tick, gpu_maps[i].handed = 1;
                        gpu_map_hits++, gpu_map_last = i;
                        return gpu_maps[i].va + (positive)pointer - gpu_maps[i].from;
                }
        gpu_map_misses++;
        return 0;
}
#define gpu_va(pointer) gpu_va_bytes(pointer, 1)

// The same, for a map the card may write (gpu_map_written): 0 for a pointer that only a map to read holds.
static positive gpu_va_written(const void *pointer, positive bytes)
{
        for (unsigned int i = 0; i < gpu_map_count; i++)
                if (gpu_maps[i].written && (positive)pointer >= gpu_maps[i].from && (positive)pointer < gpu_maps[i].to && bytes <= gpu_maps[i].to - (positive)pointer)
                {
                        gpu_maps[i].used = gpu_tick, gpu_maps[i].handed = 1;
                        return gpu_maps[i].va + (positive)pointer - gpu_maps[i].from;
                }
        return 0;
}

static b32 gpu_idle(void);
static void gpu_unimport(positive at, enum gpu_gone_why why);
static void gpu_release_import(positive at);

// A tick is over (a present): the maps unused for 120 ticks are unmapped and their buffers freed, and the maps and imports let go of while a job might still read them
// (gpu_forget, gpu_release_import) are dropped once the card is idle. The marks of the maps handed to records are not let go of here: a record given before the trim may be
// run after it, and gpu_run lets the marks go once the job has.
static void gpu_trim(void)
{
        b32 idle;

        gpu_tick++;
        // Nothing is unmapped under a job that may still run: a job given up on as late was given its maps' addresses, and the 120 ticks are less than the
        // two seconds it may run for on a screen of more than 60 presents a second (0.83 s at 144).
        idle = gpu_idle();
        for (unsigned int i = 0; idle && i < gpu_map_count; i++)
        {
                b32 forgotten = !gpu_maps[i].from && !gpu_maps[i].to;

                if (gpu_tick - gpu_maps[i].used > 120 || forgotten)
                        gpu_drop(i--, forgotten ? GPU_GONE_FORGET : GPU_GONE_TRIM);
        }
        for (unsigned int i = 0; idle && i < gpu.imports; i++)
                if (gpu.imported_dead[i])
                        gpu_unimport(gpu.imported_va[i], GPU_GONE_RELEASE), i = ~0u; // the list has moved: looked at again from the start
}

// The maps whose pages are not all there any more (the compositor let go of what one was made over: a glass window's backdrop, a band made again at another size)
// dropped: the kernel refuses every job that names such a map (EFAULT), and each job names them all. msync with MS_ASYNC writes nothing; it says whether every
// page of the range is mapped. True when a dropped one was used in this tick, so the job in hand may read its addresses and is not to run.
static b32 gpu_drop_unmapped(void)
{
        b32 used = false;

        for (unsigned int i = 0; i < gpu_map_count; i++)
        {
                b32 forgotten = !gpu_maps[i].from && !gpu_maps[i].to;

                if (forgotten || system_call_3(syscall(msync), gpu_maps[i].from, gpu_maps[i].to - gpu_maps[i].from, 1) != 0)
                {
                        if (forgotten ? gpu_maps[i].handed && gpu_maps[i].used == gpu_tick : gpu_maps[i].used == gpu_tick)
                                used = true;
                        gpu_drop(i--, forgotten ? GPU_GONE_FORGET : GPU_GONE_UNMAPPED);
                }
        }
        return used;
}

// The import that holds a handle of the card's: its place in the table, -1 for none. The kernel answers a dma-buf it has imported already with the handle it gave
// the first time and counts nothing (drm_gem_prime_fd_to_handle: the lookup, then out), so one GEM_CLOSE lets the buffer and its map go for every holder. A buffer
// is therefore in the table once, with the count of its holders (imported_holds; 0 and 1 are both one holder), and its handle is closed by the last of them alone.
static int gpu_import_of(unsigned int handle)
{
        for (unsigned int i = 0; i < gpu.imports; i++)
                if (gpu.imported[i] == handle)
                        return (int)i;
        return -1;
}

// A buffer the table has, held once more: the address it has had since it was mapped. One released and not yet unmapped (dead: no holder, kept until the card
// is idle) is held again, by this holder alone. 0, and nothing held, for more bytes than its map has or more holders than the count holds.
static positive gpu_import_again(unsigned int i, positive bytes)
{
        if (((bytes + 4095) & ~4095ull) > gpu.imported_bytes[i] || (!gpu.imported_dead[i] && gpu.imported_holds[i] == 255))
                return 0;
        if (gpu.imported_dead[i])
                gpu.imported_dead[i] = 0, gpu.imported_holds[i] = 1;
        else
                gpu.imported_holds[i] = (unsigned char)((gpu.imported_holds[i] ? gpu.imported_holds[i] : 1) + 1);
        return gpu.imported_va[i];
}

// A GEM handle of a buffer on the GPU's own node that no import holds yet, mapped read and written and never run, for bytes, at an address of its own: the
// address, 0 for a refusal (no room in the table, no address, a map the kernel refuses), and the handle is closed then. The handle is the GPU's: gpu_import and
// gpu_import_fd make it, and gpu_unimport closes it.
static positive gpu_import_at(unsigned int handle, positive bytes)
{
        positive whole = (bytes + 4095) & ~4095ull, at;

        if (gpu.imports == GPU_IMPORTS && gpu_idle())
                for (unsigned int i = 0; i < gpu.imports; i++)
                        if (gpu.imported_dead[i])
                                gpu_unimport(gpu.imported_va[i], GPU_GONE_RELEASE), i = ~0u;
        at = gpu.imports == GPU_IMPORTS ? 0 : gpu_place(whole);
        if (!at || gpu_backend->bind(handle, at, whole, true) != 0)
                return gpu_close(handle), 0;
        gpu.imported_va[gpu.imports] = at, gpu.imported_bytes[gpu.imports] = whole, gpu.imported_dead[gpu.imports] = 0, gpu.imported_holds[gpu.imports] = 1;
        gpu.imported[gpu.imports++] = handle;
        gpu_map_next = at + whole;
        return at;
}

// A buffer of another node of the card (the scanout's dumb buffer on the display node, by its handle on that node's descriptor), made reachable here: its address, 0 for none.
// bytes is its size; the address is the one it keeps while the buffer lives. A buffer the table has already is held once more at the address it has (gpu_import_again).
// A refusal lets go of what it took (the compositor asks again each present): the shared descriptor always, the handle unless an import held already has it.
static positive gpu_import(int node, unsigned int handle, positive bytes)
{
        unsigned int share[3] = {handle, 2 | 0x80000, 0};
        int held;

        if (gpu.fd < 0 || GPU_CONTROL(node, 3u << 30 | 12u << 16 | 0x642du, share) != 0)
                return 0;
        share[0] = 0;
        if (GPU_CONTROL(gpu.fd, 3u << 30 | 12u << 16 | 0x642eu, share) != 0)
                return system_close((int)share[2]), 0;
        system_close((int)share[2]);
        held = gpu_import_of(share[0]);
        return held >= 0 ? gpu_import_again((unsigned int)held, bytes) : gpu_import_at(share[0], bytes);
}

// A client's dma-buf (a descriptor the caller keeps, which is not closed here), made reachable to the GPU as gpu_import makes a buffer: the
// card's handle is the buffer's on the GPU's own node (the same device the client allocates on). 0 for a refusal. A buffer the GPU already has
// an import of is answered by the kernel with the handle that import has: it is held once more at the address it has, a released one (let go of
// earlier in this tick) held again, and that handle is not closed here, since closing it would take the buffer and its map from the import
// that holds it. Each address answered is let go of once (gpu_release_import, gpu_unimport).
static positive gpu_import_fd(int fd, positive bytes)
{
        unsigned int share[3] = {0, 0, (unsigned int)fd};
        int held;

        if (gpu.broken || gpu.fd < 0 || GPU_CONTROL(gpu.fd, 3u << 30 | 12u << 16 | 0x642eu, share) != 0)
                return 0;
        held = gpu_import_of(share[0]);
        return held >= 0 ? gpu_import_again((unsigned int)held, bytes) : gpu_import_at(share[0], bytes);
}

// Whether the GPU is up to be given a buffer (the card's node is open and it has not been broken).
static b32 gpu_ready(void)
{
        return !gpu.broken && gpu.fd >= 0;
}

// The maps over [from, from + bytes) let go of, as the compositor unmaps or moves the pages (a client's pool, cut, shrunk, remapped): a map's
// pages are the old ones, which the card would read where the processor reads the new. A map is taken out of every address now (its from and to
// are 0, its va kept for the unmap) and dropped by gpu_trim once the card is idle: a job of this present may still name it.
static void gpu_forget(const void *addr, positive bytes)
{
        positive from = (positive)addr, to = from + bytes;

        for (unsigned int i = 0; i < gpu_map_count; i++)
                if (from < gpu_maps[i].to && gpu_maps[i].from < to)
                {
                        gpu_gone_put(gpu_maps[i].va, gpu_maps[i].bytes, gpu_maps[i].from, gpu_maps[i].to, gpu_maps[i].handle, GPU_GONE_FORGET, gpu_maps[i].written,
                                     gpu_maps[i].dma, 0, gpu_maps[i].listed);
                        gpu_maps[i].from = gpu_maps[i].to = 0;
                }
}

// A buffer gpu_import made, let go of by one holder for the reason why (its mode changed, it was made again, the card is idle). While another holder has it,
// it only has one holder fewer. The last one's leaves it unmapped and its handle closed, unless a job the wait gave up on may still read it (gpu_run_job's
// pending): then it is only released, and gpu_trim unmaps it once the card is idle.
static void gpu_unimport(positive at, enum gpu_gone_why why)
{
        for (unsigned int i = 0; at && i < gpu.imports; i++)
                if (gpu.imported_va[i] == at)
                {
                        if (!gpu.imported_dead[i] && gpu.imported_holds[i] > 1)
                        {
                                gpu.imported_holds[i]--;
                                return;
                        }
                        if (gpu.pending && !gpu_idle())
                        {
                                gpu.imported_dead[i] = 1, gpu.imported_holds[i] = 0;
                                return;
                        }
                        gpu_gone_put(at, gpu.imported_bytes[i], 0, 0, gpu.imported[i], why, 1, 1, 1, 0);
                        gpu_backend->unbind(gpu.imported[i], at, gpu.imported_bytes[i]);
                        gpu_close(gpu.imported[i]);
                        gpu.imported[i] = gpu.imported[gpu.imports - 1], gpu.imported_va[i] = gpu.imported_va[gpu.imports - 1];
                        gpu.imported_bytes[i] = gpu.imported_bytes[gpu.imports - 1], gpu.imported_dead[i] = gpu.imported_dead[gpu.imports - 1];
                        gpu.imported_holds[i] = gpu.imported_holds[gpu.imports - 1], gpu.imports--;
                        return;
                }
}

// An import let go of by one holder: with its last, unmapped by the gpu_trim after the card is idle (a job of this present may still name it, so not now).
static void gpu_release_import(positive at)
{
        for (unsigned int i = 0; at && i < gpu.imports; i++)
                if (gpu.imported_va[i] == at)
                {
                        if (!gpu.imported_dead[i] && gpu.imported_holds[i] > 1)
                                gpu.imported_holds[i]--;
                        else
                                gpu.imported_dead[i] = 1, gpu.imported_holds[i] = 0;
                        return;
                }
}

// The arguments the kernel reads, at byte 512 of the buffer: s, d, g are GPU addresses (GPU_VA + an offset), pitches in pixels, op 0 copy
// s to d, 1 fill d with v, 2 tone v over d, 3 glass: s over g into d (v the light side, x0 y0 where the rect is, for the grain), 4 a scene, 5 with boxes (both by clang, any width), 6 by hand with or without boxes for a width and a pitch of a multiple of 4 (gpu_scene picks).
struct gpu_args { positive s, d, g; unsigned int sp, dp, gp, w, op, v, x0, y0, rows, pad; positive tab; };
#define GPU_ARGS ((struct gpu_args address_to)(gpu.at + 128))

/*
        Whether the buffer is the processor's again: no job that was given up on (gpu_run waits a tick for each, GPU_LATE_NS) can still read it. A job not
        seen done is asked after again with a deadline already past; while it may run nothing may write the buffer (its arguments, lists and records would
        change under it: a wave that starts late reads them as they are then), so the compositor draws on the processor. Answered with an error (the job
        ended by the queue's reset, or the context the reset left guilty), it is over and the jobs after it go to a context of their own; if the job was a
        late one whose end is still owed, that end is kept for gpu_late_end to say.
*/
static b32 gpu_idle(void)
{
        int ended;

        if (!gpu.pending)
                return true;
        if ((ended = gpu_backend->wait(gpu.pending, clock_monotonic_nanoseconds())) >= 0)
        {
                if (!ended)
                        return false; // still running
                gpu_owed = 0, gpu_last_job = clock_monotonic_nanoseconds();
        }
        else
        {
                if (gpu_owed)
                        gpu_found = GPU_END_WAIT, gpu_owed = 0;
                if (gpu_backend->context(&gpu.ctx, gpu.ctx) != 0)
                {
                        // No new context either: the descriptor answers neither the wait nor the context (the device is gone, or its reset left it dead), so the
                        // job can never be asked after again and the buffer would be held for good. The card is lost: the caller starts it again (gpu_recover closes
                        // the descriptor, and the kernel ends what ran on it), and nothing is written here until it has.
                        gpu_lost = 1;
                        return false;
                }
        }
        gpu.pending = 0;
        return true;
}

// Whether the bo list of the last submit had a map (its listed is that submit's serial), in the words of the line.
static const char *gpu_listed_words(unsigned int listed)
{
        return listed && listed == gpu_submit_serial ? ", listed in the job's bo list" : ", missing from the job's bo list";
}

// The fault's address, said as what it was in (gpu_fault_say): a map held (its handle, userptr or dma-buf, read or written, its span from and to, its use, and whether
// the last submit's bo list had it), an import held (every submit names all), a map or an import let go of in the last sixteen (why, how many ticks ago, and for a map
// whether that list had it), or none the table ever made.
static void gpu_fault_say(positive at)
{
        const unsigned int room = sizeof(gpu_fault);
        unsigned int n = gpu_text_put(gpu_fault, 0, room, "card faulted reading 0x");

        n = gpu_text_num(gpu_fault, n, room, at, 16), n = gpu_text_put(gpu_fault, n, room, ": ");
        for (unsigned int i = 0; i < gpu_map_count; i++)
                if (at >= gpu_maps[i].va && at - gpu_maps[i].va < gpu_maps[i].bytes)
                {
                        n = gpu_text_put(gpu_fault, n, room, "held map, handle ");
                        n = gpu_text_num(gpu_fault, n, room, gpu_maps[i].handle, 10);
                        n = gpu_text_put(gpu_fault, n, room, gpu_maps[i].dma ? ", dma-buf, " : ", userptr, ");
                        n = gpu_text_put(gpu_fault, n, room, gpu_maps[i].written ? "written, " : "read, ");
                        n = gpu_text_put(gpu_fault, n, room, "from 0x"), n = gpu_text_num(gpu_fault, n, room, gpu_maps[i].from, 16);
                        n = gpu_text_put(gpu_fault, n, room, " to 0x"), n = gpu_text_num(gpu_fault, n, room, gpu_maps[i].to, 16);
                        n = gpu_text_put(gpu_fault, n, room, gpu_maps[i].from || gpu_maps[i].to ? ", used " : ", forgotten, used ");
                        n = gpu_text_num(gpu_fault, n, room, gpu_tick - gpu_maps[i].used, 10);
                        n = gpu_text_put(gpu_fault, n, room, " ticks ago");
                        gpu_text_put(gpu_fault, n, room, gpu_listed_words(gpu_maps[i].listed));
                        return;
                }
        for (unsigned int i = 0; i < gpu.imports; i++)
                if (at >= gpu.imported_va[i] && at - gpu.imported_va[i] < gpu.imported_bytes[i])
                {
                        n = gpu_text_put(gpu_fault, n, room, "held import, handle ");
                        n = gpu_text_num(gpu_fault, n, room, gpu.imported[i], 10);
                        gpu_text_put(gpu_fault, n, room, gpu.imported_dead[i] ? ", released" : ", live");
                        return;
                }
        for (unsigned int k = 0; k < GPU_GONE && k < gpu_gone_at; k++)
        {
                unsigned int slot = (gpu_gone_at - 1 - k) % GPU_GONE;

                if (gpu_gone[slot].bytes && at >= gpu_gone[slot].va && at - gpu_gone[slot].va < gpu_gone[slot].bytes)
                {
                        n = gpu_text_put(gpu_fault, n, room, gpu_gone[slot].import ? "import" : "map");
                        n = gpu_text_put(gpu_fault, n, room, " handle "), n = gpu_text_num(gpu_fault, n, room, gpu_gone[slot].handle, 10);
                        n = gpu_text_put(gpu_fault, n, room, " dropped "), n = gpu_text_num(gpu_fault, n, room, gpu_tick - gpu_gone[slot].tick, 10);
                        n = gpu_text_put(gpu_fault, n, room, " ticks ago ");
                        n = gpu_text_put(gpu_fault, n, room, gpu_gone_words[gpu_gone[slot].why]);
                        gpu_text_put(gpu_fault, n, room, gpu_gone[slot].import ? "" : gpu_listed_words(gpu_gone[slot].listed));
                        return;
                }
        }
        gpu_text_put(gpu_fault, n, room, "in no map or import the table ever made");
}

// One kernel log record ("prio,seq,usec,flags;text") at the time usec: the usec field, as the kernel log clock's microseconds since boot.
static positive gpu_kmsg_usec(const char *record)
{
        positive usec = 0;
        unsigned int commas = 0;

        for (const char *at = record; *at && *at != ';'; at++)
        {
                if (*at == ',')
                        commas++;
                else if (commas == 2 && *at >= '0' && *at <= '9')
                        usec = usec * 10 + (positive)(*at - '0');
        }
        return usec;
}

static positive gpu_kmsg_seq(const char *record)
{
        positive seq = 0;
        unsigned int commas = 0;

        for (const char *at = record; *at && *at != ';'; at++)
        {
                if (*at == ',')
                        commas++;
                else if (commas == 1 && *at >= '0' && *at <= '9')
                        seq = seq * 10 + (positive)(*at - '0');
        }
        return seq;
}

static b32 gpu_kmsg_seq_ready;
static positive gpu_kmsg_floor_seq, gpu_kmsg_seen_seq, gpu_kmsg_last_since;

static void gpu_kmsg_drain(void)
{
        static char record[8192];
        long handle = (long)system_open_at(AT_FDCWD, (string_address) "/dev/kmsg", FILE_READ | O_NONBLOCK | O_CLOEXEC);

        gpu_kmsg_last_since = 0;
        if (handle < 0)
        {
                gpu_kmsg_seq_ready = 0;
                return;
        }
        gpu_kmsg_seq_ready = 1;
        for (unsigned int records = 0; records < 8192; records++)
        {
                bipolar got = system_read_once(handle, record, sizeof(record) - 1);
                positive seq;

                if (got <= 0)
                        break;
                record[got] = 0;
                seq = gpu_kmsg_seq(record);
                if (seq > gpu_kmsg_seen_seq)
                        gpu_kmsg_seen_seq = seq;
        }
        gpu_kmsg_floor_seq = gpu_kmsg_seen_seq;
        system_close(handle);
}

// The hexadecimal number after the key in a log line ("in page starting at address 0x" then the digits): true when there is one.
static b32 gpu_key_hex(const char *text, const char *key, positive *value)
{
        const char *at = (const char *)string_find((string_address)text, (string_address)key);
        positive v = 0;
        unsigned int digits = 0;

        if (!at)
                return false;
        for (at += string_length((string_address)key);; at++, digits++)
        {
                unsigned int d = *at >= '0' && *at <= '9' ? (unsigned int)(*at - '0') : *at >= 'a' && *at <= 'f' ? (unsigned int)(*at - 'a' + 10) :
                                 *at >= 'A' && *at <= 'F' ? (unsigned int)(*at - 'A' + 10) : 99;

                if (d == 99)
                        break;
                v = v * 16 + d;
        }
        *value = v;
        return digits > 0;
}

// The words after a key in a log line, up to the end of the line and less the number the kernel puts after a name ("TCP (0x8)" is TCP, "SQC (data) (0x9)" is
// SQC (data)): into, "" when the key is not there.
static void gpu_key_words(const char *text, const char *key, char *into, unsigned int size)
{
        const char *at = (const char *)string_find((string_address)text, (string_address)key);
        unsigned int n = 0;

        into[0] = 0;
        if (!at)
                return;
        for (at += string_length((string_address)key); *at && *at != '\n' && n + 1 < size; at++)
                into[n++] = *at;
        into[n] = 0;
        at = (const char *)string_find((string_address)into, (string_address) " (0x");
        if (at)
                into[(unsigned int)(at - into)] = 0;
}

// What the card says of its last page fault: from the kernel's log since a time (monotonic ns, as its records' times are), the first fault's address, the status words
// of that fault (MAPPING_ERROR, PERMISSION_FAULTS, RW) and the client of the fault register ("Faulty UTCL2 client ID": TCP, SQC, CPC ...) when the log has them after it;
// from the kernel's query (AMDGPU_INFO_GPUVM_FAULT), the page's address, the fault register's status word and the hub that saw it. again: the query's answer is the one the
// last read had, in the same descriptor's address space (a fault that was survived, not this job's); epoch: the card's descriptor count at the read (gpu_epoch).
struct gpu_fault_scan
{
        positive at, mapping, permission, rw, query_at;
        unsigned int query_status, query_hub, epoch;
        b32 have_at, have_mapping, have_permission, have_rw, have_query, again;
        char client[24];
};
static struct gpu_fault_scan gpu_fault_last; // the last fault read (gpu_fault_note)

// One line of the kernel's log: the first page fault's address, then the words after it.
static void gpu_fault_line(const char *text, struct gpu_fault_scan *out)
{
        if (!out->have_at)
        {
                out->have_at = gpu_key_hex(text, "in page starting at address 0x", &out->at);
                return;
        }
        if (!out->have_mapping)
                out->have_mapping = gpu_key_hex(text, "MAPPING_ERROR: 0x", &out->mapping);
        if (!out->have_permission)
                out->have_permission = gpu_key_hex(text, "PERMISSION_FAULTS: 0x", &out->permission);
        if (!out->have_rw)
                out->have_rw = gpu_key_hex(text, "RW: 0x", &out->rw);
        if (!out->client[0])
                gpu_key_words(text, "Faulty UTCL2 client ID: ", out->client, sizeof(out->client));
}

// AMDGPU_INFO_GPUVM_FAULT (query 0x23, the driver's version 3.55 and later): the last page fault of this descriptor's address space, struct drm_amdgpu_info_gpuvm_fault
// (the page's address, the fault register's status word, the hub: 0 the graphics hub, 1 and 2 the media hubs, the hub's index above the first byte). 0 when the kernel
// answered, with have_query set unless no fault is kept (the kernel answers zeros then); else its errno (EINVAL on a kernel without the query).
#ifndef GPU_FAULT_FAKE
#define GPU_FAULT_FAKE(error, fault)
#endif
static unsigned int gpu_fault_query(struct gpu_fault_scan *out)
{
        struct gpu_vm_fault fault = {0, 0, 0};
        unsigned int e = gpu.fd >= 0 ? gpu_backend->fault(&fault) : 9; // EBADF

        GPU_FAULT_FAKE(e, &fault);
        if (e == 0 && (fault.addr || fault.status))
                out->have_query = true, out->query_at = fault.addr, out->query_status = fault.status, out->query_hub = fault.hub;
        return e;
}

// What the card says of its last fault since a time (monotonic ns): the kernel's log read from the oldest record, as gpu_say_kernel reads it (/dev/kmsg, no wait), and the
// kernel's query. A kernel without the query, or one that keeps no fault, leaves the log's words alone.
#ifndef GPU_LOG_FAKE
#define GPU_LOG_FAKE(since, out) // a check's fake kernel log puts a page fault here
#endif
static void gpu_fault_read(positive since, struct gpu_fault_scan *out)
{
        static char record[8192];
        long handle;

        memory_zero(out, sizeof(*out));
        GPU_LOG_FAKE(since, out);
        if (gpu_kmsg_seq_ready && since != gpu_kmsg_last_since)
        {
                if (gpu_kmsg_last_since)
                        gpu_kmsg_floor_seq = gpu_kmsg_seen_seq;
                gpu_kmsg_last_since = since;
        }
        handle = out->have_at ? -1 : (long)system_open_at(AT_FDCWD, (string_address) "/dev/kmsg", FILE_READ | O_NONBLOCK | O_CLOEXEC);
        for (unsigned int records = 0; handle >= 0 && records < 8192; records++)
        {
                bipolar got = system_read_once(handle, record, sizeof(record) - 1);
                char *text;
                positive seq, ns;

                if (got <= 0)
                        break;
                record[got] = 0;
                seq = gpu_kmsg_seq(record);
                if (seq > gpu_kmsg_seen_seq)
                        gpu_kmsg_seen_seq = seq;
                text = (char *)string_find((string_address)record, (string_address) ";");
                ns = gpu_kmsg_usec(record) * 1000;
                if (!text || (gpu_kmsg_seq_ready ? seq <= gpu_kmsg_floor_seq : ns + 2000000000ull < since))
                        continue;
                gpu_fault_line(text + 1, out);
        }
        if (handle >= 0)
                system_close(handle);
        gpu_fault_query(out);
}

// The line of a fault read (gpu_fault): where it was (gpu_fault_say), then the query's status word and hub, the client, and the log's status words. A fault the log does not
// have is the query's address; neither is the job's error alone.
static void gpu_fault_text(const struct gpu_fault_scan *s)
{
        const unsigned int room = sizeof(gpu_fault);
        unsigned int n;

        if (!s->have_at && !s->have_query)
        {
                n = gpu_text_put(gpu_fault, 0, room, "the card's job ended with error ");
                n = gpu_text_num(gpu_fault, n, room, gpu_job_error, 10);
                gpu_text_put(gpu_fault, n, room, ": the kernel's log has no page fault since it was submitted");
                return;
        }
        gpu_fault_say(s->have_at ? s->at : s->query_at);
        n = (unsigned int)string_length((string_address)gpu_fault);
        if (s->have_query)
        {
                n = gpu_text_put(gpu_fault, n, room, "; status 0x"), n = gpu_text_num(gpu_fault, n, room, s->query_status, 16);
                n = gpu_text_put(gpu_fault, n, room, ", hub "), n = gpu_text_num(gpu_fault, n, room, s->query_hub, 10);
                n = gpu_text_put(gpu_fault, n, room, (s->query_hub & 255) == 0 ? " (graphics)" : (s->query_hub & 255) <= 2 ? " (media)" : "");
        }
        if (s->client[0])
                n = gpu_text_put(gpu_fault, n, room, "; client "), n = gpu_text_put(gpu_fault, n, room, s->client);
        if (s->again)
                n = gpu_text_put(gpu_fault, n, room, "; the same fault as the last one read");
        if (!s->have_at)
                return;
        n = gpu_text_put(gpu_fault, n, room, " (MAPPING_ERROR ");
        n = s->have_mapping ? gpu_text_num(gpu_fault, n, room, s->mapping, 10) : gpu_text_put(gpu_fault, n, room, "?");
        n = gpu_text_put(gpu_fault, n, room, ", PERMISSION_FAULTS ");
        n = s->have_permission ? gpu_text_num(gpu_fault, n, room, s->permission, 10) : gpu_text_put(gpu_fault, n, room, "?");
        n = gpu_text_put(gpu_fault, n, room, ", RW ");
        n = s->have_rw ? gpu_text_num(gpu_fault, n, room, s->rw, 10) : gpu_text_put(gpu_fault, n, room, "?");
        gpu_text_put(gpu_fault, n, room, ")");
}

// A job that failed (ETIME or EIO, or a submit refused) says what the card says of its fault since it was submitted (gpu_fault_read): the card's fault text (gpu_fault),
// and the fault is kept (gpu_fault_last). A refused submit reads at most once in two seconds; a hang always reads (it costs the two seconds already).
static void gpu_fault_note(b32 hang)
{
        positive now = clock_monotonic_nanoseconds();
        struct gpu_fault_scan s;

        if (!hang && now < gpu_fault_next)
                return;
        gpu_fault_next = now + 2000000000ull, gpu_faults++;
        gpu_fault_read(gpu_job_began, &s);
        // The query keeps the last fault of the address space, which a later job that failed for another reason would read again: the log alone is bounded by the job's
        // time, so an address only the query has, the same as the last read's in the same descriptor's address space (a card started again has a new one), is said so.
        s.epoch = gpu_epoch;
        s.again = !s.have_at && s.have_query && gpu_fault_last.have_query && s.epoch == gpu_fault_last.epoch && s.query_at == gpu_fault_last.query_at &&
                  s.query_status == gpu_fault_last.query_status;
        memory_copy(&gpu_fault_last, &s, sizeof(s));
        gpu_fault_text(&s);
}

// The job in gpu.pending waited for, in one wait that ends when the job does, until it is done or cap has passed since it was submitted (gpu_job_began). True when it is done.
// Otherwise gpu_end says how it was given up on: LATE (the cap was a tick: the job stays pending and its end is owed, gpu_late_end says it), HUNG (the cap was the
// hang's two seconds), WAIT (the wait itself failed). A hang or a failed wait is read in the kernel's log at once (gpu_fault) and is the caller's to take.
static b32 gpu_wait_job(positive cap)
{
        for (;;)
        {
                positive until = gpu_job_began + cap;
                int ended;

                // One wait, to the cap: the kernel wakes it when the job ends, so a job is one request to wait for however long it runs (in slices of four
                // milliseconds a job of 5.5 ms, the heaviest measured, was two).
                ended = gpu_backend->wait(gpu.pending, until);
                if (ended < 0)
                        gpu_end = GPU_END_WAIT, gpu_last_error = 5; // EIO: the wait itself failed
                else if (ended)
                        return true;
                else if (clock_monotonic_nanoseconds() < until)
                        continue;
                else if (cap >= GPU_HANG_NS)
                        gpu_end = GPU_END_HUNG, gpu_last_error = 62; // ETIME: still running after two seconds
                else
                {
                        gpu_end = GPU_END_LATE, gpu_owed = 1, gpu_fault_poll = clock_monotonic_nanoseconds() + GPU_FAULT_FIRST;
                        return false;
                }
                gpu_job_error = gpu_last_error, gpu_fault_note(true);
                return false;
        }
}

/*
        The end of a job the wait gave up on as late, said once to the caller (the compositor asks at each present): GPU_END_NONE while the job is within bounds,
        or ended without trouble. The job's end is a bad one when its fence answers an error (gpu_idle comes on it: WAIT), when it is GPU_HANG_NS old and not
        done (HUNG), or when the kernel's log has a page fault since it was submitted (HUNG; the log is read from GPU_FAULT_FIRST after the job went late, then
        every GPU_FAULT_EVERY, so a frame does not pay for it). Each of those is read in the kernel's log and said in gpu_fault; gpu_end and gpu_last_error say it
        as a job that failed in the call would.
*/
static enum gpu_end gpu_late_end(void)
{
        enum gpu_end over = GPU_END_NONE;
        positive now;

        if (gpu.pending && gpu_owed && !gpu_idle() && gpu_owed)
        {
                now = clock_monotonic_nanoseconds();
                if (now - gpu_job_began >= GPU_HANG_NS)
                        over = GPU_END_HUNG;
                else if (now >= gpu_fault_poll)
                {
                        struct gpu_fault_scan s;

                        gpu_fault_poll = now + GPU_FAULT_EVERY;
                        gpu_fault_read(gpu_job_began, &s);
                        if (s.have_at)
                                over = GPU_END_HUNG;
                }
                if (over)
                {
                        gpu_owed = 0, gpu_end = over, gpu_last_error = 62, gpu_job_error = 62, gpu_fault_note(true);
                        return over;
                }
        }
        over = gpu_found, gpu_found = GPU_END_NONE;
        if (over)
        {
                gpu_end = over, gpu_last_error = 5, gpu_job_error = 5;
                gpu_fault_note(true);
        }
        return over;
}

// Wait synchronously for any in-flight late GPU job (up to GPU_HANG_NS from gpu_job_began).
// Returns true once idle; false if the card is lost or the job hung/errored (sets gpu_end/gpu_last_error).
__attribute__((unused)) static b32 gpu_finish(void)
{
        if (!gpu.pending)
                return !gpu_lost;
        if (gpu_idle())
                return true;
        if (gpu_lost)
                return false;
        if (gpu_wait_job(GPU_HANG_NS))
        {
                gpu.pending = 0, gpu_owed = 0, gpu_last_job = clock_monotonic_nanoseconds();
                return true;
        }
        gpu_owed = 0;
        return false;
}

// No record waits for a job any more (the maps' addresses are no longer handed out to one): the marks of the lookups are cleared.
static void gpu_handed_clear(void)
{
        for (unsigned int i = 0; i < gpu_map_count; i++)
                gpu_maps[i].handed = 0;
}

// Whether the job's bo list (the submit's buffers) names the map at i. The invariant: every map a record of the job refers to is in the list. A record gets a map's address
// from gpu_va_bytes or gpu_va_written only, which mark the map handed, and the mark stays until a job has run (gpu_run), so a map handed to a record is named in whatever tick
// the job runs (a trim between the lookup and the run moves the tick on), and so is a map used in this tick that has its pages. The kernel looks at the pages of every map
// named at every submission, so no other is named. The one exception is a map the compositor let go of (gpu_forget: its pages are unmapped, from and to are 0): the kernel
// refuses a job that names a buffer whose pages are gone (EFAULT), and the trim keeps such a map while a job given up on may run, so it is named for a record of this tick
// alone (that job is refused, as it should be: the record reads pages that are gone), not for the mark of an earlier one, whose record is not run again.
static b32 gpu_map_named(unsigned int i)
{
        const b32 pages = gpu_maps[i].from || gpu_maps[i].to, now = gpu_maps[i].used == gpu_tick;

        return pages ? gpu_maps[i].handed || now : gpu_maps[i].handed && now;
}

#define GPU_AGAIN 32 // the submits a job is asked again after EAGAIN (gpu_run_job)
// Run the kernel over the arguments' w by rows pixels; false if it did not finish (then gpu_idle holds the buffer until it has).
static b32 gpu_run_job(void)
{
        const unsigned int which = GPU_KERNEL_OF(GPU_ARGS->op);
        const struct gpu_job job = {gpu_kernels[which].words, GPU_VA + gpu_kernel_at[which], GPU_VA + 512, (GPU_ARGS->w + 63) / 64, GPU_ARGS->rows};
        p64 bos[1 + GPU_IMPORTS + GPU_MAPS];
        positive sequence = 0;
        unsigned int named, tries = 0, again = 0;

        if (!gpu_idle())
                return false;
        gpu_job_began = clock_monotonic_nanoseconds();
        if (!++gpu_submit_serial) // (counted from 1: 0 is a map no submit has named)
                gpu_submit_serial++;
        for (;;)
        {
                named = 0;
                for (unsigned int i = 0; i < gpu.imports; i++)
                        bos[named++] = gpu.imported[i];
                // Of the maps, the ones a record of this job was given and the ones this present used (gpu_map_named): the kernel looks at the pages of every map named at
                // every submission (ten 1920 by 1080 window buffers held cost a present 80 to 400 us on the box), and a map not named is not read by this job. A map the
                // compositor let go of that no record of this job holds is not named: a glass scratch grown in the tick that used it made every job of the tick fail.
                for (unsigned int i = 0; i < gpu_map_count; i++)
                        if (gpu_map_named(i))
                                bos[named++] = gpu_maps[i].handle, gpu_maps[i].listed = gpu_submit_serial;
                GPU_SUBMIT_SEEN(bos, named);
                gpu_end = GPU_END_NONE;
                gpu_last_error = gpu_backend->submit(&job, bos, named, &sequence);
                if (gpu_last_error == 0)
                        break;
                // The card lost the context or its VM (ECANCELED after a reset, ENODEV, EINVAL): nothing this job can do again succeeds: the caller recovers (gpu_recover).
                if (gpu_last_error == 125 || gpu_last_error == 19 || gpu_last_error == 22)
                {
                        gpu_job_error = gpu_last_error, gpu_fault_note(false);
                        gpu_lost = 1;
                        return false;
                }
                // EAGAIN: a page of a userptr moved between the kernel's taking it and the submit (a migration or a compaction: on the box at a load of 120 one job in
                // ten), and the kernel asks for the call again, which takes the pages afresh. Asked again up to GPU_AGAIN times with the processor let go of between: three
                // calls in a row failed the job, the present was the processor's, a list was refused, and three such jobs in a row cooled the card for two seconds.
                if (gpu_last_error == 11 && again++ < GPU_AGAIN)
                {
                        system_call_2(syscall(sched_yield), 0, 0);
                        continue;
                }
                // A page moved under the job as it was submitted, or a map's pages are gone: asked again, without the maps that are, unless the job reads one.
                if (++tries == 3 || gpu_drop_unmapped())
                {
                        gpu_job_error = gpu_last_error, gpu_fault_note(false);
                        return false;
                }
        }
        gpu.pending = sequence;
        if (!gpu_wait_job(gpu_patient ? GPU_HANG_NS : GPU_LATE_NS))
                return false;
        gpu.pending = 0;
        gpu_last_job = clock_monotonic_nanoseconds();
        return true;
}

// A job run and waited for (gpu_run_job), timed: the count and the total wait the metrics publish.
static b32 gpu_run(void)
{
        b64 began = clock_monotonic_nanoseconds();
        b32 done = gpu_run_job();

        gpu_jobs++, gpu_job_ns += clock_monotonic_nanoseconds() - began;
        if (!gpu.pending) // a job given up on may still read the maps: their addresses stay handed out until it is done (gpu_idle)
                gpu_handed_clear();
        return done;
}

// ui_box_radii on a box record's rx and ry (words 6 and 7): where two corners come to more than a side, the one factor that fits them all.
static void gpu_radii(unsigned int address_to r)
{
        long long w = (int)r[3] - (int)r[1], h = (int)r[4] - (int)r[2], f = 65536;

        if (w <= 0 || h <= 0)
                return;
        if ((int)r[6] > 0 && 2 * (long long)(int)r[6] > w)
                f = (w << 16) / (2 * (long long)(int)r[6]);
        if ((int)r[7] > 0 && 2 * (long long)(int)r[7] > h && (h << 16) / (2 * (long long)(int)r[7]) < f)
                f = (h << 16) / (2 * (long long)(int)r[7]);
        if (f < 65536)
                r[6] = (unsigned int)(int)(((long long)(int)r[6] * f) >> 16), r[7] = (unsigned int)(int)(((long long)(int)r[7] * f) >> 16);
}

// A scene: n ops of 8 words at ops_at, in order: {3 a text run (ui_scene_text's), or 0 fill or 1 tone over, x0, y0, x1, y1 in pixels, colour, 0, 0} or {2 box, x0, y0, x1, y1, colour, rx, ry} with
// the box in 256ths as ui_kit_fill takes it (an anti-aliased rounded box). Only the damage rectangle dx0 dy0 dx1 dy1 is drawn by the kernel by hand
// (the whole picture by the others), each pixel in it from the ops alone when the first op holds it: the scene is its own background., drawn into the w by h
// pixels at d_at (pitch in pixels) and each pixel written once. The ops are binned into 64-pixel squares at bins_at: two words a square, then the lists of ops.
// Each record of the scene with the squares it has any pixel of the damage in (tx0 to tx1 and ty0 to ty1, the last ones counted in): e is a box (its place in
// 256ths), m how many changes of cover follow it, which are its own and are passed over.
#define GPU_RECORDS(body) for (i = 0; i < n; i++) { unsigned int address_to q = ops + 8 * i; int e = (q[0] & 255) == 2, m = e ? (int)(q[0] >> 8 & 15) : 0, x0 = e ? (int)q[1] >> 8 : (int)q[1], y0 = e ? (int)q[2] >> 8 : (int)q[2], \
        x1 = e ? ((int)q[3] + 255) >> 8 : (int)q[3], y1 = e ? ((int)q[4] + 255) >> 8 : (int)q[4]; \
        if (i + (unsigned int)m >= n) return false; \
        x0 = x0 < (int)dx0 ? (int)dx0 : x0, y0 = y0 < (int)dy0 ? (int)dy0 : y0, x1 = x1 > (int)dx1 ? (int)dx1 : x1, y1 = y1 > (int)dy1 ? (int)dy1 : y1; \
        if (x0 < x1 && y0 < y1) { unsigned int tx0 = (unsigned int)x0 >> 6, ty0 = (unsigned int)y0 >> 6, tx1 = (unsigned int)(x1 - 1) >> 6, ty1 = (unsigned int)(y1 - 1) >> 6; body } i += m; }
static b32 gpu_scene(positive ops_at, unsigned int n, positive bins_at, positive d_at, unsigned int pitch, unsigned int w, unsigned int h, unsigned int dx0,
                     unsigned int dy0, unsigned int dx1, unsigned int dy1)
{
        unsigned int address_to ops = (unsigned int address_to)((p8 address_to)gpu.at + ops_at), address_to tab = (unsigned int address_to)((p8 address_to)gpu.at + bins_at);
        unsigned int cw = (w + 63) >> 6, ch = (h + 63) >> 6, at = 0, hand = 0, scene_op = 4, i, address_to list = tab + 2 * cw * ch, record[16 * 8];
        struct gpu_args address_to a = GPU_ARGS;
        b32 aligned = !(w & 3) && !(pitch & 3) && !(d_at & 15), partial;

        gpu_last_error = 0, gpu_end = GPU_END_NONE; // this call's answer: a hang of its job is 62 here, and an older job's is not read as its own
        if (!gpu_idle()) // the lists and the arguments are not to be written under a job that may still read them
                return false;
        dx1 = dx1 > w ? w : dx1, dy1 = dy1 > h ? h : dy1;
        if (dx0 >= dx1 || dy0 >= dy1)
                return true;
        partial = dx0 > 0 || dy0 > 0 || dx1 < w || dy1 < h;
        for (i = 0; i < n; i++)
        {
                unsigned int kind = ops[8 * i] & 255;
                unsigned int m = kind == 2 ? (ops[8 * i] >> 8 & 15) : 0;

                if (i + m >= n)
                        return false;
                if (kind == 2)
                        scene_op = 5, hand |= ops[8 * i] > 2;
                else
                        hand |= kind > 2;
                i += m;
        }
        if (!aligned && (hand || partial))
                return false;
        dx0 &= ~63u, dy0 &= ~3u, dx1 = (dx1 + 63) & ~63u, dy1 = (dy1 + 3) & ~3u; // the kernel draws whole patches: so the ops are binned for them
        dx1 = dx1 > w ? w : dx1, dy1 = dy1 > h ? h : dy1;
        if (bins_at >= gpu.size || 8ull * cw * ch > gpu.size - bins_at)
                return gpu_end = GPU_END_OVERFLOW, false;
        {
                positive tab_bytes = (positive)cw * ch * 8, need;
                int staged = 0;

                if (tab_bytes > gpu_bin_room)
                {
                        positive want = tab_bytes < 262144 ? 262144 : ((tab_bytes + 4095) & ~(positive)4095);
                        long p = system_call_6(syscall(mmap), 0, want, 3, 0x22, (positive)-1, 0);

                        if (!(p < 0 && p > -4096))
                        {
                                if (gpu_bin_stage)
                                        system_call_2(syscall(munmap), (positive)gpu_bin_stage, gpu_bin_room);
                                gpu_bin_stage = (unsigned int *)p, gpu_bin_room = want;
                        }
                }
                if (gpu_bin_stage && tab_bytes <= gpu_bin_room)
                        tab = gpu_bin_stage, list = tab + 2 * cw * ch, staged = 1;
                memory_zero(tab, tab_bytes);
                // The three steps of the bins are the library's (bins_count, bins_starts, bins_place: lib.c, in assembly on each machine): a record's squares counted,
                // the counts made starts, and the record put in each square's list.
                GPU_RECORDS(bins_count(tab + 2 * (ty0 * cw + tx0), cw, tx1 - tx0 + 1, ty1 - ty0 + 1, 1 + (positive)m);)
                at = (unsigned int)bins_starts(tab, (positive)cw * ch);
                need = tab_bytes + 32ull * at;
                if (need > gpu.size - bins_at) // the lists must fit what the buffer has left
                        return gpu_end = GPU_END_OVERFLOW, false;
                if (staged && need > gpu_bin_room)
                {
                        positive want = (need + 4095) & ~(positive)4095;
                        long p = system_call_6(syscall(mmap), 0, want, 3, 0x22, (positive)-1, 0);

                        if (!(p < 0 && p > -4096))
                        {
                                memory_copy_apart((void *)p, gpu_bin_stage, tab_bytes);
                                system_call_2(syscall(munmap), (positive)gpu_bin_stage, gpu_bin_room);
                                gpu_bin_stage = (unsigned int *)p, gpu_bin_room = want;
                                tab = gpu_bin_stage, list = tab + 2 * cw * ch;
                        }
                        else
                        {
                                tab = (unsigned int address_to)((p8 address_to)gpu.at + bins_at), list = tab + 2 * cw * ch, staged = 0;
                                memory_copy_apart(tab, gpu_bin_stage, tab_bytes);
                        }
                }
                // A record goes into the list of each square it is in as it is, a box with its corners fitted to its sides (gpu_radii): fitted once, and the same words
                // put in every list (they were fitted again in each square: two divisions a square for a box of a whole window).
                GPU_RECORDS(memory_copy(record, q, 32 * (1 + (positive)m)); if (e) for (int j = 0; j <= m; j++) gpu_radii(record + 8 * j);
                            bins_place(tab + 2 * (ty0 * cw + tx0), cw, tx1 - tx0 + 1, ty1 - ty0 + 1, list, record, 1 + (positive)m);)
                if (staged)
                        memory_copy_apart((p8 address_to)gpu.at + bins_at, tab, need);
        }
        a->tab = GPU_VA + GPU_BASES_AT, a->s = GPU_VA, a->d = d_at >= GPU_VA ? d_at : GPU_VA + d_at, a->g = GPU_VA + bins_at, a->dp = pitch, a->sp = w, a->gp = h, a->x0 = a->y0 = 0, a->w = w, a->rows = (h + 3) >> 2, a->op = scene_op;
        if (aligned)
                a->op = 6, a->x0 = dx0 >> 6, a->y0 = dy0 >> 2, a->w = (((dx1 + 63) >> 6) - (dx0 >> 6)) * 64, a->rows = ((dy1 + 3) >> 2) - (dy0 >> 2);
        return gpu_run();
}

// One job moving a picture (src, sp pixels a row) into dst (dp a row) over w by h pixels, the card told by op. op 0 copies one rectangle,
// d[y * dp + x] = s[y * sp + x] for x below w and y below h, the pitches in pixels; op 7 scales nearest, the card told by tables where each column and
// row comes from: the processor has made the w entries of the columns' and then the h entries of the rows' at tab (a GPU address), each the source's
// index (the source cell whose centre is the point's). The card divides nothing, and reads nothing but what the tables name. gpu_run waits for the
// job, so when this is true the destination is written: nothing that reads it, or releases the source, is run before. false if the card did not finish it (as gpu_run says).
static b32 gpu_blit(positive src, unsigned int sp, positive dst, unsigned int dp, positive tab, unsigned int op, unsigned int w, unsigned int h)
{
        struct gpu_args address_to a = GPU_ARGS;

        gpu_last_error = 0, gpu_end = GPU_END_NONE;
        if (!w || !h || !gpu_idle())
                return false;
        a->s = src, a->d = dst, a->g = tab, a->sp = sp, a->dp = dp, a->gp = 0, a->w = w, a->op = op, a->v = 0, a->x0 = a->y0 = 0, a->rows = h, a->tab = op ? GPU_VA + GPU_BASES_AT : 0;
        return gpu_run();
}

// One small scene of every kind of op, made from nothing but numbers (a generator for the pictures, the covers and the glyph's bitmap), and the hash of its pixels,
// 0 if the card did not finish it.
/*
        The card's idle clock: a job after a pause pays the clock's ramp (measured on the box's 610M: the first fill after half a second idle took a
        median of 2.2 ms, back to back 57 us). gpu_wake runs one tiny job when the card has been idle for longer than GPU_WAKE_IDLE, so that whatever the
        program then commits finds the card awake. It is called on an input event only (no timer: nothing runs while nothing happens), and it writes
        nothing the compositor reads: a fill of 64 by 4 into the last 64 KB of the buffer, after the card is known idle. Not called before the card
        has run a job, and never while a job may still run (gpu_idle).
*/
#define GPU_WAKE_IDLE 200000000ull
#define GPU_WAKE_AT 65536ull
static void gpu_wake(void)
{
        positive now = clock_monotonic_nanoseconds(), ops_at, bins_at, d_at;
        unsigned int address_to ops;

        if (!gpu.at || !gpu_last_job || now - gpu_last_job < GPU_WAKE_IDLE || !gpu_idle())
                return;
        gpu_wakes++;
        ops_at = gpu.size - GPU_WAKE_AT, bins_at = ops_at + 4096, d_at = ops_at + 8192;
        ops = (unsigned int address_to)((p8 address_to)gpu.at + ops_at);
        ops[0] = 0, ops[1] = 0, ops[2] = 0, ops[3] = 64, ops[4] = 4, ops[5] = 0, ops[6] = ops[7] = 0;
        gpu_scene(ops_at, 1, bins_at, d_at, 64, 64, 4, 0, 0, 64, 4);
}

static unsigned int gpu_test_overv(unsigned int dst, unsigned int src)
{
        unsigned int inv = 255u - (src >> 24);
        unsigned int trb = src & 0x00ff00ffu, tga = (src >> 8) & 0x00ff00ffu;
        unsigned int drb = (dst & 0x00ff00ffu) * inv + 0x007f007fu;
        unsigned int dga = ((dst >> 8) & 0x00ff00ffu) * inv + 0x007f007fu;
        unsigned int rb = trb + (((drb + 0x00010001u + ((drb >> 8) & 0x00ff00ffu)) >> 8) & 0x00ff00ffu);
        unsigned int ga = tga + (((dga + 0x00010001u + ((dga >> 8) & 0x00ff00ffu)) >> 8) & 0x00ff00ffu);
        unsigned int b = (rb & 0xffffu) > 255u ? 255u : rb & 0xffffu, r = (rb >> 16) > 255u ? 255u : rb >> 16;
        unsigned int g = (ga & 0xffffu) > 255u ? 255u : ga & 0xffffu, a = (ga >> 16) > 255u ? 255u : ga >> 16;

        return b | (g << 8) | (r << 16) | (a << 24);
}

static unsigned int gpu_test_textpixel(unsigned int dst, unsigned int ink, unsigned int alpha, unsigned int stroke,
                                       unsigned int cov, unsigned int lf, unsigned int rt, unsigned int up, unsigned int dn)
{
        unsigned int nmax = lf > rt ? lf : rt, boosted, mul, a, f, b, g, r;

        if (up > nmax)
                nmax = up;
        if (dn > nmax)
                nmax = dn;
        boosted = cov + ((nmax * stroke) >> 7);
        if (boosted > 255u)
                boosted = 255u;
        mul = boosted * alpha, a = (mul + (mul >> 8) + 1u) >> 8, f = a + (a >> 7);
        b = (((ink & 255u) * f) + 128u) >> 8;
        g = ((((ink >> 8) & 255u) * f) + 128u) >> 8;
        r = ((((ink >> 16) & 255u) * f) + 128u) >> 8;
        return gpu_test_overv(dst, b | (g << 8) | (r << 16) | (a << 24));
}

static unsigned int gpu_test_hash(void)
{
        enum { W = 256, H = 32, W2 = 64, H2 = 16 };
        positive base = gpu.size - (1 << 20), ops_at = base, bins_at = base + 4096, pic_at = base + 65536, src_at = base + 131072, cover_at = base + 196608, glyph_at = base + 200704;
        unsigned int address_to ops = (unsigned int address_to)((p8 address_to)gpu.at + ops_at), address_to pic = (unsigned int address_to)((p8 address_to)gpu.at + pic_at);
        unsigned int address_to src = (unsigned int address_to)((p8 address_to)gpu.at + src_at);
        p8 address_to cover = (p8 address_to)gpu.at + cover_at, address_to glyph = (p8 address_to)gpu.at + glyph_at;
        unsigned int seed = 12345u, hash = 2166136261u, n = 0;
        unsigned int want[W2 * H2];
        unsigned int address_to pal2 = (unsigned int address_to)((p8 address_to)gpu.at + cover_at + 1024);
        p8 address_to cov2 = (p8 address_to)gpu.at + cover_at + 2048;
        b32 done, patient, ok = true;
        static const unsigned int rows[][8] = {
            {0, 0, 0, W, H, 0xff203040u, 0, 0},
            {1, 10, 2, 200, 30, 0x80c0a020u, 0, 0},
            {2, 20 * 256, 4 * 256 + 100, 120 * 256 + 50, 28 * 256, 0xc0ff4020u, 6 * 256, 5 * 256},
            {2 | 1 << 8, 130 * 256, 3 * 256, 250 * 256, 29 * 256, 0xe0ffffffu, 10 * 256, 10 * 256},
            {8, 132 * 256, 5 * 256, 248 * 256, 27 * 256, 0, 8 * 256, 8 * 256},
            {4, 4, 6, 68, 22, 0, 0, 64},
            {5 | 32 << 8, 100, 4, 132, 20, 0, 0xff4080c0u, 0},
            {5 | 32 << 8 | 64 << 16, 150, 6, 182, 22, 0, 0, 0},
            {3 | 34 << 8, 40, 10, 72, 20, 0xff00ff80u, 200 | 100 << 8, 0},
            {6 | 14 << 8 | 64 << 16, 200, 10, 214, 24, 0, 0, 0}};
        static const unsigned int extra[][8] = {
            {0, 0, 0, W2, H2, 0xff203040u, 0, 0},
            {0, (unsigned int)-5, (unsigned int)-3, 12, 6, 0xff112233u, 0, 0},
            {1, (unsigned int)-3, (unsigned int)-2, 20, 8, 0x80403020u, 0, 0},
            {4, (unsigned int)-2, 1, 14, 9, 0, 0, 64},
            {11, (unsigned int)-3, 3, 15, 11, 0, 0, 64},
            {7 | 0x7fffffu << 8, 20, 2, 36, 10, 0, 0, 64},
            {10 | 18 << 8, (unsigned int)-1, 6, 17, 14, 0, 0, 0},
            {10 | 14 << 8, 41, 5, 55, 13, 0, 0, 0},
            {3 | 20 << 8, (unsigned int)-3, 10, 13, 15, 0xff102030u, 180u | 64u << 8, 0}};

        for (unsigned int i = 0; i < 64 * 16; i++)
                seed = seed * 1664525u + 1013904223u, src[i] = seed;
        for (unsigned int i = 0; i < 32 * 16 + 14 * 14; i++)
                seed = seed * 1664525u + 1013904223u, cover[i] = (p8)((seed >> 24) % 4 == 0 ? 0 : (seed >> 24) % 4 == 1 ? 255 : seed >> 16);
        for (unsigned int i = 0; i < 34 * 12; i++)
                seed = seed * 1664525u + 1013904223u, glyph[i] = (p8)(seed >> 24);
        for (unsigned int i = 0; i < 256; i++)
                seed = seed * 1664525u + 1013904223u, pal2[i] = seed | 0xff000000u, cov2[i] = (p8)(seed >> 16);
        for (unsigned int i = 0; i < sizeof(rows) / sizeof(rows[0]); i++)
        {
                unsigned int address_to q = ops + 8 * n;

                memory_copy(q, rows[i], 32);
                if (i == 5)
                        q[5] = (unsigned int)(GPU_VA + src_at), q[6] = (unsigned int)((GPU_VA + src_at) >> 32);
                else if (i == 6)
                        q[5] = (unsigned int)cover_at;
                else if (i == 7)
                        q[5] = (unsigned int)cover_at, q[6] = (unsigned int)(GPU_VA + src_at), q[7] = (unsigned int)((GPU_VA + src_at) >> 32);
                else if (i == 8)
                        q[7] = (unsigned int)(glyph_at + 35);
                else if (i == 9)
                        q[5] = (unsigned int)(cover_at + 32 * 16), q[6] = (unsigned int)(GPU_VA + src_at), q[7] = (unsigned int)((GPU_VA + src_at) >> 32 & 0xff) | 0xe0e0e0u << 8;
                n++;
        }
        memory_zero(pic, W * H * 4);
        patient = gpu_patient, gpu_patient = 1; // the first job of a card just opened is waited for as long as a hang is (the card is slow to it); a late self-test would be no card
        done = gpu_scene(ops_at, n, bins_at, pic_at, W, W, H, 0, 0, W, H);
        if (!done)
                return gpu_patient = patient, 0;
        hash = (unsigned int)hash_fnv_u32(pic, W * H, hash) | 1u;

        // Pass 2: verify turn (7), translate (10), over-image (11), and negative-coordinate clipping against exact integer math.
        n = 0;
        for (unsigned int i = 0; i < sizeof(extra) / sizeof(extra[0]); i++)
        {
                unsigned int address_to q = ops + 8 * n;

                memory_copy(q, extra[i], 32);
                if (i == 3 || i == 4)
                        q[5] = (unsigned int)(GPU_VA + src_at), q[6] = (unsigned int)((GPU_VA + src_at) >> 32);
                else if (i == 5)
                        q[5] = (unsigned int)(GPU_VA + src_at + 15 * 4), q[6] = (unsigned int)((GPU_VA + src_at + 15 * 4) >> 32);
                else if (i == 6 || i == 7)
                        q[5] = (unsigned int)(cover_at + 1024), q[7] = (unsigned int)(cover_at + 2048);
                else if (i == 8)
                        q[7] = (unsigned int)(glyph_at + 21);
                n++;
        }
        memory_zero(pic, W2 * H2 * 4);
        done = gpu_scene(ops_at, n, bins_at, pic_at, W2, W2, H2, 0, 0, W2, H2);
        gpu_patient = patient;
        if (!done)
                return 0;
        for (unsigned int y = 0; y < H2; y++)
                for (unsigned int x = 0; x < W2; x++)
                {
                        unsigned int c = 0xff203040u;

                        if (x < 12 && y < 6)
                                c = 0xff112233u;
                        if (x < 20 && y < 8)
                                c = gpu_test_overv(c, 0x80403020u);
                        if (x < 14 && y >= 1 && y < 9)
                                c = src[(y - 1) * 64 + (x + 2)];
                        if (x < 15 && y >= 3 && y < 11)
                                c = gpu_test_overv(c, src[(y - 3) * 64 + (x + 3)]);
                        if (x >= 20 && x < 36 && y >= 2 && y < 10)
                                c = src[15 + (y - 2) * 64 - (x - 20)];
                        if (x < 17 && y >= 6 && y < 14)
                                c = pal2[cov2[(y - 6) * 18 + (x + 1)]];
                        if (x >= 41 && x < 55 && y >= 5 && y < 13)
                                c = pal2[cov2[(y - 5) * 14 + (x - 41)]];
                        if (x < 13 && y >= 10 && y < 15)
                        {
                                unsigned int gi = 21u + (y - 10u) * 20u + (x + 3u);

                                c = gpu_test_textpixel(c, 0xff102030u, 180u, 64u, glyph[gi], glyph[gi - 1], glyph[gi + 1], glyph[gi - 20], glyph[gi + 20]);
                        }
                        want[y * W2 + x] = c;
                }
        for (unsigned int i = 0; i < W2 * H2; i++)
                if (pic[i] != want[i])
                        ok = false;
        return ok ? hash : (hash ^ 2u);
}

#endif
