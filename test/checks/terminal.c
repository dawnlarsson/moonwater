/*
        test/checks/terminal.c: that program's fakes (the kernel's DRM and the GPU model's hook, the card, a popup's
        row, what the kit hands the font engine) and the terminal's rows: its frames, the selection and its scroll,
        the kit's keys, the pointer a program asked for.

        One part of the program that the terminal's, the kit frames', the GPU kit's, the draw list's and the GPU model
        kit's sections share (checks.c opens and closes it). The terminal's program, the kit's frames, the draw list
        and the GPU model's kit run with the four faces: fonts/ibm-plex-mono/IBMPlexMono-Regular.ttf
        fonts/ibm-plex-mono/IBMPlexMono-Bold.ttf fonts/inter/Inter-Regular.ttf fonts/inter/Inter-Bold.ttf.

        A run of test/checks.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. A section is built as
        test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib -nostartfiles
        -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its
        freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name>
        test/checks.c).
*/

#include "../../src/lib.util.c"
#include "../../src/moonwater/spark.c"
#include "../../src/canvas/window.c"
#include "../../src/sh/registry.c"
#include "../../src/canvas/term.c"
#include "../../src/sh/pty.c"
#define BOWL_DEFAULT_PATH "/fixture/bin"
#include "../../src/canvas/terminfo.c"
static fn host_terminal_opening() {}
static bool shell_exec_file_ends = true; // a start that ran ends its child, as an exec would
// A start ends here in the fixture, and a check reads what was asked: the program's words, a line in /tmp/mw-launcher-started.
static bipolar shell_exec_file(string_address path, string_address address_to arguments, positive count,
                               string_address address_to environment)
{
        char line[256] = "";
        bipolar handle;

        (void)environment;
        //      A program that is not there fails as an exec does, and says nothing.
        if (string_equals(path, (string_address) "/no/such/program"))
                return -2;
        for (positive at = 0; at < count && at < 8; at++)
        {
                string_append_bounded((string_address)line, arguments[at], sizeof(line));
                string_append_bounded((string_address)line, (string_address) " ", sizeof(line));
        }
        string_append_bounded((string_address)line, (string_address) "\n", sizeof(line));
        handle = system_open_at_mode(AT_FDCWD, (string_address) "/tmp/mw-launcher-started", FILE_APPEND, 0644);
        if (handle >= 0)
        {
                system_write_once(handle, line, string_length((string_address)line));
                system_close(handle);
        }
        //      In a child of ui_start_apart the start is done, as an exec would end it; anywhere else the fixture answers
        //      as before.
        if (shell_exec_file_ends)
                system_call_1(syscall(exit_group), 0);
        return -1;
}
static fn kill_name(positive number, p8 address_to into) { positive_into_string(into, number); }
static fn bowl_session_prepare(string_address home, string_address runtime) { (void)home, (void)runtime; }
static string_address address_to bowl_environment(string_address address_to inherited) { return inherited; }
#include "../../src/canvas/screen.c"
#include "../../src/canvas/font.c"
#ifndef CHECK_gpu_kit // the GPU rows hold a client's dma-buf lent to a row, which is the shim's (gpu_lent_rows)
#define SHELL_NO_WAYLAND 1 // the shim is on in a build, and these lanes are without the shell's signals it needs; CHECK_wayland holds it
#else
// The shim's signal macros (shell.c's, which these lanes do not include): the gpu rows install no handler, so none is caught here.
#define SIGNAL_RESTART 0x10000000
#define SIGNAL_CATCH_RESTORER 0
#define SIGNAL_CATCH_FLAGS SIGNAL_RESTART
#endif
#ifdef CHECK_gpu_kit
static int gpu_fake_busy, gpu_fake_idle; // the check's fake fence: while gpu_fake_busy is set, every job waited for is answered as still running (nothing on the card is held); gpu_fake_idle answers done
static unsigned int gpu_fake_waits; // the waits answered as still running before the fence goes on to say what gpu_fake_idle or the card says: a job that ends within the late cap
static int gpu_fake_errno;          // a wait the kernel refuses is answered as failed while it is set (the fence has an error)
static unsigned int gpu_fake_wait_calls, gpu_fake_submits, gpu_fake_requests; // the waits asked, the submits made, and every request made of the kernel's DRM (GPU_CONTROL)
static positive gpu_fake_until; // the time the last wait was asked to wait until (0: it looks and does not wait)
static unsigned int gpu_fake_blocking; // the waits asked to wait until a time (a wait in place), as against a look
#define GPU_WAIT_CS(wait) (gpu_fake_wait_calls++, gpu_fake_until = (wait)[1], gpu_fake_blocking += (wait)[1] != 0, gpu_fake_errno ? false : gpu_fake_busy ? ((wait)[0] = 1, true) : gpu_fake_waits ? (gpu_fake_waits--, (wait)[0] = 1, true) : gpu_fake_idle ? ((wait)[0] = 0, true) : gpu_wait_asked(wait))
// The fake card with no hardware under it: the submit is taken (its sequence number 1) without a call, and the kernel's log has a page fault in it while gpu_fake_fault is set.
static int gpu_fake_card, gpu_fake_fault;
#define GPU_LOG_FAKE(since, out) do { if (gpu_fake_fault) (out)->have_at = 1, (out)->at = 0x2000000100ull; } while (0)
// The fake card's prerequisites: gpu_fake_step names the step that fails, with gpu_fake_error for its error (gpu.c's gpu_req_step).
static int gpu_fake_step = -1;
static unsigned int gpu_fake_error;
#define GPU_STEP_FAKE(code, failed, error) do { if (gpu_fake_step == (int)(code)) (failed) = 1, (error) = gpu_fake_error; } while (0)
// The fake kernel's answer to a userptr (gpu_map_as): the errno it gives, 0 for the real one.
static long gpu_fake_userptr;
#define GPU_MAP_FAKE(taken) do { if (gpu_fake_userptr) (taken) = -gpu_fake_userptr; } while (0)
// The fake card's losses: a submit answers ECANCELED while gpu_fake_cancel is set (and the call is not made), and a node is absent (ENOENT) while
// gpu_fake_absent counts down.
static int gpu_fake_cancel, gpu_fake_absent, gpu_fake_again;
// The words of the IB of the last job submitted (gpu_run_job's ib, which is in scope where this is expanded), kept before any answer is made: a check reads the packets a job sent.
static unsigned int gpu_fake_ib[256], gpu_fake_ib_words;
// A job the fake card takes is given the next of its sequences (gpu_fake_sequence, counted from 1), as the kernel gives a context's jobs theirs.
static positive gpu_fake_sequence;
// The display node's PRIME import and ADDFB2 of a client's dma-buf to scan out (compositor_lent_fb), faked: what was asked is kept, and the answer is
// gpu_fake_addfb_answer (0, or the kernel's -errno) with the next framebuffer id.
static unsigned char gpu_fake_addfb[128];
static int gpu_fake_addfb_calls, gpu_fake_prime_calls;
static long gpu_fake_addfb_answer;
#define COMPOSITOR_WL_PRIME(prime) (gpu_fake_prime_calls++, (prime).handle = 7u, 0L)
#define COMPOSITOR_WL_ADDFB(fb) (gpu_fake_addfb_calls++, memory_copy(gpu_fake_addfb, &(fb), sizeof(fb) < sizeof gpu_fake_addfb ? sizeof(fb) : sizeof gpu_fake_addfb), \
                                 (fb).fb_id = gpu_fake_addfb_answer ? 0u : 40u + (unsigned int)gpu_fake_addfb_calls, gpu_fake_addfb_answer)
#define GPU_SUBMIT_FAKE(error, taken, sequence) do { gpu_fake_submits++; gpu_fake_ib_words = sizeof(ib) / sizeof(ib[0]) <= 256 ? (unsigned int)(sizeof(ib) / sizeof(ib[0])) : 256; memory_copy(gpu_fake_ib, ib, 4u * gpu_fake_ib_words); if (gpu_fake_cancel) gpu_fake_cancel--, (error) = 125; else if (gpu_fake_again) gpu_fake_again--, (error) = 11; else if (gpu_fake_card) (taken) = true, (sequence) = ++gpu_fake_sequence; } while (0)
// While gpu_kernel.i915 names a driver, the first render node is the fake i915 node (GPU_KERNEL_I915, its version answering that name) and every other node is
// absent: the walk of the nodes (gpu_start, gpu_nodes_seen, whose loops name their node `node`) sees one node, whatever the machine has.
#define GPU_OPEN_FAKE(fd)                                                                                                                                          \
        do                                                                                                                                                         \
        {                                                                                                                                                          \
                if (gpu_fake_absent)                                                                                                                               \
                        gpu_fake_absent--, (fd) = -2;                                                                                                              \
                else if (gpu_kernel.i915[0])                                                                                                                       \
                {                                                                                                                                                  \
                        if ((fd) >= 0)                                                                                                                             \
                                system_close((int)(fd));                                                                                                           \
                        (fd) = node == 0 ? GPU_KERNEL_I915 : -2;                                                                                                   \
                }                                                                                                                                                  \
        } while (0)
// The file that lets the other gfx10 families in (/run/moonwater/gpu-gfx10), as the check has it: 0 the machine's own answer, 1 there (an fd that is not open: the
// close of it is refused and does nothing), -1 not there.
static int gpu_fake_wide;
#define GPU_WIDE_FAKE(fd) do { if (gpu_fake_wide > 0) { if ((fd) >= 0) system_close((int)(fd)); (fd) = 1000000; } else if (gpu_fake_wide < 0) { if ((fd) >= 0) system_close((int)(fd)); (fd) = -2; } } while (0)
// The buffers the last submit named (gpu_run_job's bos, seen before the call is made or answered): the rows ask whether a map's handle is in the list.
static p64 gpu_seen_bos[1 + 64 + 256];
static unsigned int gpu_seen_count;
#define GPU_SUBMIT_SEEN(list, count) do { gpu_seen_count = (count); memory_copy(gpu_seen_bos, (list), (positive)(count) * sizeof(p64)); } while (0)
// The fake kernel's answer to AMDGPU_INFO_GPUVM_FAULT (gpu.c's gpu_fault_query): gpu_fake_vm 0 the real call, 1 the fault below, -1 a refusal (EINVAL: a kernel without the query).
static int gpu_fake_vm;
static positive gpu_fake_vm_addr;
static unsigned int gpu_fake_vm_status, gpu_fake_vm_hub;
#define GPU_FAULT_FAKE(error, fault)                                                                                                                                   \
        do                                                                                                                                                             \
        {                                                                                                                                                              \
                if (gpu_fake_vm > 0)                                                                                                                                   \
                        (error) = 0, (fault)->addr = gpu_fake_vm_addr, (fault)->status = gpu_fake_vm_status, (fault)->hub = gpu_fake_vm_hub;                           \
                else if (gpu_fake_vm < 0)                                                                                                                              \
                        (error) = 22;                                                                                                                                  \
        } while (0)
/*
        The fake kernel's DRM under gpu.c (GPU_CONTROL), for two descriptors the rows make up: GPU_KERNEL_FD the render node and GPU_KERNEL_CARD the display's
        node. It answers as drm_prime.c and drm_gem.c do where the rows turn on it: a dma-buf imported a second time is answered with the handle it has, and
        nothing is counted (drm_gem_prime_fd_to_handle); one GEM_CLOSE lets the handle and its map go, and a handle that is not there is EINVAL
        (drm_gem_handle_delete); a map of a handle that is not there is ENOENT; GET_UNIQUE copies the name with no 0 after it, writes its length back, and is
        refused on a render node (EACCES: the request has no DRM_RENDER_ALLOW). A dma-buf's descriptor is GPU_KERNEL_DMABUF and its buffer's number, a
        number no row has open. Every other descriptor's request is made as it always is, but for the context a fake card with no descriptor is asked for
        (gpu_fake_card: the kernel makes one, as it does on a card).
*/
static unsigned int gpu_fake_fence_ask[8]; // the last AMDGPU_FENCE_TO_HANDLE asked of the fake card, and the errno it answers (0: a descriptor)
static long gpu_fake_fence_error;
#define GPU_KERNEL_FD 1000
#define GPU_KERNEL_CARD 1001
#define GPU_KERNEL_DMABUF 2000
#define GPU_KERNEL_HANDLES 512 // past the two hundred and one imports the import rows make (no count of imports is a limit)
#define GPU_KERNEL_UDMABUF 1002
#define GPU_KERNEL_I915 1003 // the fake i915 render node (gpu_kernel.i915)

static struct
{
        int on, no_context;           // answering; a context is refused (ENODEV: a device that is gone)
        unsigned int buffer[GPU_KERNEL_HANDLES]; // a handle's buffer and 1 (the handle is the index), 0 for a handle that is free
        unsigned int mapped[GPU_KERNEL_HANDLES]; // the maps the handle's buffer has in the address space
        positive va[GPU_KERNEL_HANDLES];         // the last of them
        unsigned int closes, refused; // the handles closed, and the closes of a handle that was not there
        unsigned int contexts;
        // AMDGPU_CTX_OP_SET_STABLE_PSTATE (gpu_amd_clocks): the asks, the level and the context of the last, and the errno the rows have it refused with.
        unsigned int pstate_asks, pstate_level, pstate_ctx;
        long pstate_error;
        int priority_asked, priority_refused; // the priority the last AMDGPU_CTX_OP_ALLOC_CTX asked; one above normal is refused (EACCES) while set
        unsigned int interrupts, waits; // the waits a signal ends before one is answered (EINTR), and the waits asked
        char unique[40];              // the display node's bus as GET_UNIQUE says it
        unsigned int unique_length;
        // The dma-buf route and userptr (gpu_map_as): /dev/udmabuf opens as GPU_KERNEL_UDMABUF while udmabuf is set; each UDMABUF_CREATE is counted with the
        // memfd, offset and size of the last (refused with udmabuf_error while that is set), and each GEM_USERPTR is counted and given a handle.
        int udmabuf;
        long udmabuf_error;
        unsigned long udmabuf_limit; // the bytes a dma-buf may have (udmabuf's size_limit_mb, 64 MB by default: past it EINVAL); 0 none
        unsigned int udmabufs, udmabuf_memfd, userptrs;
        unsigned long udmabuf_offset, udmabuf_size;
        // The fake i915 node (gpu_kernel_i915): its driver's name ("" no node), the device id CHIPSET_ID answers, HAS_ALIASING_PPGTT's and HAS_EXEC_FENCE's
        // answers, the address space GTT_SIZE answers (0: refused), what each request was last asked and how often, and the answers the rows set.
        char i915[16];
        unsigned int i915_chip, i915_ppgtt, i915_fence, i915_lost;
        positive i915_gtt;
        unsigned int i915_identifies, i915_creates, i915_mmap_flags, i915_contexts, i915_destroyed, i915_set_ctx, i915_userptrs, i915_userptr_flags, i915_execs, i915_waits;
        positive i915_set_param, i915_set_value;
        long i915_userptr_error, i915_exec_error, i915_wait_error;
        long long i915_wait_timeout;
        unsigned char i915_exec[64], i915_objects[8 * 56];
        int i915_fence_fd; // the sync_file the last submit was given (an eventfd: readable once the row writes it)
} gpu_kernel;

static unsigned int gpu_kernel_live(void)
{
        unsigned int live = 0;

        for (unsigned int h = 1; h < GPU_KERNEL_HANDLES; h++)
                live += gpu_kernel.buffer[h] != 0;
        return live;
}

// The handle the render node has for a buffer, 0 for none.
static unsigned int gpu_kernel_handle(unsigned int buffer)
{
        for (unsigned int h = 1; h < GPU_KERNEL_HANDLES; h++)
                if (gpu_kernel.buffer[h] == buffer + 1)
                        return h;
        return 0;
}

// The fake i915 node's answers: the requests gpu.c's i915 row makes (i915_drm.h's numbers), each as the kernel answers it where the rows turn on it.
static long gpu_kernel_i915(unsigned int request, void *a)
{
        unsigned int *words = (unsigned int *)a;
        p64 *longs = (p64 *)a;
        unsigned int h = 0;

        switch (request)
        {
        case 0xc0406400u: // DRM_IOCTL_VERSION: the name's whole length written back, no 0 after the bytes copied
        {
                positive length = string_length((string_address)gpu_kernel.i915), room = longs[2];

                memory_copy((void *)longs[3], gpu_kernel.i915, length < room ? length : room);
                longs[2] = length;
                return 0;
        }
        case 0xc0106446u: // GETPARAM: CHIPSET_ID, HAS_LLC, HAS_ALIASING_PPGTT, HAS_EXEC_NO_RELOC, HAS_EXEC_SOFTPIN, HAS_EXEC_FENCE, HAS_USERPTR_PROBE
        {
                int value = words[0] == 4 ? (gpu_kernel.i915_identifies++, (int)gpu_kernel.i915_chip) : words[0] == 18 ? (int)gpu_kernel.i915_ppgtt
                            : words[0] == 44 ? (int)gpu_kernel.i915_fence : words[0] == 17 || words[0] == 25 || words[0] == 37 || words[0] == 56 ? 1 : -1;

                if (value < 0)
                        return -22;
                *(int *)longs[1] = value;
                return 0;
        }
        case 0xc0186474u: // CONTEXT_GETPARAM: GTT_SIZE
                if (longs[1] != 3 || !gpu_kernel.i915_gtt)
                        return -22;
                longs[2] = gpu_kernel.i915_gtt, words[1] = 0;
                return 0;
        case 0xc010645bu: // GEM_CREATE
        case 0xc0186473u: // GEM_USERPTR
                if (request == 0xc0186473u)
                {
                        gpu_kernel.i915_userptrs++, gpu_kernel.i915_userptr_flags = words[4];
                        if (gpu_kernel.i915_userptr_error)
                                return -gpu_kernel.i915_userptr_error;
                }
                else
                        gpu_kernel.i915_creates++;
                for (unsigned int free = 1; !h && free < GPU_KERNEL_HANDLES; free++)
                        if (!gpu_kernel.buffer[free])
                                h = free;
                if (!h)
                        return -12;
                gpu_kernel.buffer[h] = 700 + h, gpu_kernel.mapped[h] = 0;
                words[request == 0xc0186473u ? 5 : 2] = h;
                return 0;
        case 0xc0206464u: // GEM_MMAP_OFFSET: the offset the object's map is made at (as mmap would be asked it)
                gpu_kernel.i915_mmap_flags = (unsigned int)longs[2], longs[1] = 0x100000ull * words[0];
                return 0;
        case 0xc008646du: // GEM_CONTEXT_CREATE
                words[0] = 10 + ++gpu_kernel.i915_contexts;
                return 0;
        case 0x4008646eu: // GEM_CONTEXT_DESTROY
                gpu_kernel.i915_destroyed = words[0];
                return 0;
        case 0xc0186475u: // GEM_CONTEXT_SETPARAM
                gpu_kernel.i915_set_ctx = words[0], gpu_kernel.i915_set_param = longs[1], gpu_kernel.i915_set_value = longs[2];
                return 0;
        case 0xc0186472u: // GET_RESET_STATS: the context's lost batches as batch_active, none pending
                words[2] = 0, words[3] = gpu_kernel.i915_lost, words[4] = 0;
                return 0;
        case 0x40406469u: // GEM_EXECBUFFER2 and its _WR: the record and its first eight objects kept; FENCE_OUT given an eventfd (not readable until the row writes it)
        case 0xc0406469u:
                gpu_kernel.i915_execs++;
                memory_copy(gpu_kernel.i915_exec, a, 64);
                memory_copy(gpu_kernel.i915_objects, (void *)longs[0], (words[2] < 8 ? words[2] : 8) * 56ull);
                if (gpu_kernel.i915_exec_error)
                        return -gpu_kernel.i915_exec_error;
                if (request == 0xc0406469u && (longs[5] & (1ull << 17)))
                        gpu_kernel.i915_fence_fd = (int)system_call_2(syscall(eventfd2), 0, 0x80000), longs[7] = (p64)(unsigned int)gpu_kernel.i915_fence_fd << 32;
                return 0;
        case 0xc010646cu: // GEM_WAIT: the timeout asked kept, and the answer the row sets (ETIME: still busy)
                gpu_kernel.i915_waits++, gpu_kernel.i915_wait_timeout = (long long)longs[1];
                return -gpu_kernel.i915_wait_error;
        case 0x40086409u: // DRM_IOCTL_GEM_CLOSE
                if (words[0] == 0 || words[0] >= GPU_KERNEL_HANDLES || !gpu_kernel.buffer[words[0]])
                        return gpu_kernel.refused++, -22;
                gpu_kernel.buffer[words[0]] = 0, gpu_kernel.closes++;
                return 0;
        }
        return request == 0xc0106401u ? -13 : -22; // GET_UNIQUE is refused on a render node
}

static long gpu_kernel_control(int fd, unsigned int request, void *a)
{
        unsigned int *words = (unsigned int *)a;
        p64 *longs = (p64 *)a;

        gpu_fake_requests++;
        if (fd == GPU_KERNEL_I915 && gpu_kernel.i915[0])
                return gpu_kernel_i915(request, a);
        // AMDGPU_FENCE_TO_HANDLE on the fake card: the words asked are kept, and the sync_file is an eventfd (a descriptor the row closes), or the error the row gives.
        if (gpu_fake_card && fd < 0 && request == 0xc0206454u)
        {
                memory_copy(gpu_fake_fence_ask, words, sizeof(gpu_fake_fence_ask));
                if (gpu_fake_fence_error)
                        return -gpu_fake_fence_error;
                words[0] = (unsigned int)system_call_2(syscall(eventfd2), 0, 0x80000);
                return 0;
        }
        if (!gpu_kernel.on || (fd != GPU_KERNEL_FD && fd != GPU_KERNEL_CARD && fd != GPU_KERNEL_UDMABUF))
        {
                if (gpu_fake_card && fd < 0 && request == 0xc0106442u && !gpu_kernel.no_context) // DRM_IOCTL_AMDGPU_CTX
                {
                        if (words[0] == 1 && (gpu_kernel.priority_asked = (int)words[3]) > 0 && gpu_kernel.priority_refused)
                                return -13;
                        return words[0] == 1 ? (words[0] = 2 + ++gpu_kernel.contexts, 0) : 0;
                }
                return system_control(fd, request, a);
        }
        switch (request)
        {
        case 0xc00c642du: // DRM_IOCTL_PRIME_HANDLE_TO_FD: the display node's handle is the buffer's number
                words[2] = GPU_KERNEL_DMABUF + words[0];
                return 0;
        case 0xc00c642eu: // DRM_IOCTL_PRIME_FD_TO_HANDLE
        {
                unsigned int buffer = words[2] - GPU_KERNEL_DMABUF, h = gpu_kernel_handle(buffer);

                for (unsigned int free = 1; !h && free < GPU_KERNEL_HANDLES; free++)
                        if (!gpu_kernel.buffer[free])
                                h = free, gpu_kernel.buffer[h] = buffer + 1, gpu_kernel.mapped[h] = 0;
                if (!h)
                        return -12;
                words[0] = h;
                return 0;
        }
        case 0x40187542u: // UDMABUF_CREATE on /dev/udmabuf: a dma-buf of a new buffer number (500 on)
                if (fd != GPU_KERNEL_UDMABUF)
                        return -25;
                gpu_kernel.udmabuf_memfd = words[0], gpu_kernel.udmabuf_offset = longs[1], gpu_kernel.udmabuf_size = longs[2];
                if (gpu_kernel.udmabuf_error)
                        return -gpu_kernel.udmabuf_error;
                if (gpu_kernel.udmabuf_limit && longs[2] > gpu_kernel.udmabuf_limit)
                        return -22;
                return GPU_KERNEL_DMABUF + 500 + gpu_kernel.udmabufs++;
        case 0xc0186451u: // DRM_IOCTL_AMDGPU_GEM_USERPTR: a handle of a new buffer number (900 on), written in the high half of the flags' word
        {
                unsigned int h = 0;

                for (unsigned int free = 1; !h && free < GPU_KERNEL_HANDLES; free++)
                        if (!gpu_kernel.buffer[free])
                                h = free;
                if (!h)
                        return -12;
                gpu_kernel.buffer[h] = 900 + ++gpu_kernel.userptrs + 1, gpu_kernel.mapped[h] = 0, words[5] = h;
                return 0;
        }
        case 0x40086409u: // DRM_IOCTL_GEM_CLOSE
                if (words[0] == 0 || words[0] >= GPU_KERNEL_HANDLES || !gpu_kernel.buffer[words[0]])
                        return gpu_kernel.refused++, -22;
                gpu_kernel.buffer[words[0]] = 0, gpu_kernel.mapped[words[0]] = 0, gpu_kernel.closes++;
                return 0;
        case 0x40406448u: // DRM_IOCTL_AMDGPU_GEM_VA: the handle, the operation (1 map, 2 unmap), the address
        {
                unsigned int h = (unsigned int)longs[0], op = (unsigned int)longs[1];

                if (h == 0 || h >= GPU_KERNEL_HANDLES || !gpu_kernel.buffer[h])
                        return -2;
                if (op == 1)
                        gpu_kernel.mapped[h]++, gpu_kernel.va[h] = longs[2];
                else if (!gpu_kernel.mapped[h])
                        return -2;
                else
                        gpu_kernel.mapped[h]--;
                return 0;
        }
        case 0xc0106442u: // DRM_IOCTL_AMDGPU_CTX: 1 a context made, 2 one freed, 6 a context's stable power state set
                if (gpu_kernel.no_context)
                        return -19;
                if (words[0] == 6)
                {
                        gpu_kernel.pstate_asks++, gpu_kernel.pstate_level = words[1], gpu_kernel.pstate_ctx = words[2];
                        return -gpu_kernel.pstate_error;
                }
                if (words[0] == 1 && (gpu_kernel.priority_asked = (int)words[3]) > 0 && gpu_kernel.priority_refused)
                        return -13;
                if (words[0] == 1)
                        words[0] = 2 + ++gpu_kernel.contexts;
                return 0;
        case 0xc0206449u: // DRM_IOCTL_AMDGPU_WAIT_CS: ended by a signal while any are owed, then the job is done (status 0)
                gpu_kernel.waits++;
                if (gpu_kernel.interrupts)
                        return gpu_kernel.interrupts--, -4;
                longs[0] = 0;
                return 0;
        case 0xc0106401u: // DRM_IOCTL_GET_UNIQUE
                if (fd == GPU_KERNEL_FD)
                        return -13;
                if (longs[0] >= gpu_kernel.unique_length)
                        memory_copy((void *)longs[1], gpu_kernel.unique, gpu_kernel.unique_length);
                longs[0] = gpu_kernel.unique_length;
                return 0;
        }
        return -22;
}
#define GPU_CONTROL(fd, request, a) gpu_kernel_control((int)(fd), (unsigned int)(request), (void *)(a))
#define GPU_UDMABUF_OPEN() (gpu_kernel.on && gpu_kernel.udmabuf ? (long)GPU_KERNEL_UDMABUF : system_open_at(AT_FDCWD, (string_address) "/dev/udmabuf", FILE_READ_WRITE | O_CLOEXEC))
#include "../../src/canvas/gpu.c" // the compositor's compose builds the GPU's scene when gpu.c is there before it
#endif
#ifdef CHECK_gpu_model_kit
#define GPU_MODEL_CARD // the model of the card under gpu.c's seams: test/gpu_model/card.c includes gpu.c, which the compositor then finds included
#include "../gpu_model.c"
#endif
// The compositor's calls of a card go through card_fake_control (below): a card a check makes is answered there, and every
// other descriptor's call is made as it always is.
static bipolar card_fake_control(positive handle, positive request, positive argument);
#undef system_control
#define system_control(handle, request, argument) card_fake_control((positive)(handle), (positive)(request), (positive)(argument))
// And the cards it opens by their minors are a check's too (card_fake_node): the nodes a check says are there.
static long card_fake_node(unsigned int minor);
#define COMPOSITOR_CARD_NODE(minor) card_fake_node(minor)
#ifdef CHECK_draw_list
#define COMPOSITOR_LIST_RENDER_EXTERNAL // the list rows below draw their own renderer (compositor_gpu_list_render, after this file)
#endif
/*
        A page no one may touch after every mapping the compositor makes, while a row asks for it (space_guarded, the size
        rows of check_compositor_space_sizes, which every section that includes the compositor here builds): a buffer's own pages, rounded up, and then one with no access, so a store one
        row past a buffer's end is a fault there and not a write into whatever the kernel mapped next to it. The guard page
        is kept when the buffer is given back (the row runs in a child, which ends). Off, every call is the one it always is.
*/
static int space_guarded;

static bipolar space_guard_call_6(positive number, positive a, positive b, positive c, positive d, positive e, positive f)
{
        positive length = (b + 4095) & ~4095ul;
        bipolar room, mapped;

        if (!space_guarded || number != syscall(mmap) || a || !b)
                return system_call_6(number, a, b, c, d, e, f);
        room = system_call_6(syscall(mmap), 0, length + 4096, 0, 0x22, (positive)-1, 0); // PROT_NONE, private and anonymous
        if (room < 0 && room > -4096)
                return room;
        mapped = system_call_6(syscall(mmap), (positive)room, b, c, d | 0x10, e, f); // MAP_FIXED over the room's first pages
        if (mapped < 0 && mapped > -4096)
                system_call_2(syscall(munmap), (positive)room, length + 4096);
        return mapped;
}
#define system_call_6(number, a, b, c, d, e, f) space_guard_call_6(number, a, b, c, d, e, f)
#include "../../src/canvas/compositor.c"
#undef system_call_6
#undef system_control
#define system_control(handle, request, argument)                            \
        system_call_3(syscall(ioctl), (positive)(handle),                    \
                      (positive)(request), (positive)(argument))
/*
        A card that is not there, for the checks of what the compositor asks of one: the calls compositor.c makes on
        compositor.card, answered as the kernel's DRM answers them. A list is copied only as far as the room it is given and
        its count is the card's whatever the room; a property's values and enums go through the pointers given, and a null
        one with a count is a fault (EFAULT); a blob is copied only when its length is asked exactly; SETCRTC checks the
        mode's viewport against the framebuffer with the plane's turn (drm_crtc_check_viewport), and a commit sets what it
        names. The card is one panel on one crtc, its primary plane listing rotate-0 to rotate-270 when it is asked to,
        and its descriptor a memfd, so its dumb buffers map.
*/
#define CARD_FAKE_PLANE 31u
#define CARD_FAKE_CRTC 41u
#define CARD_FAKE_CONNECTOR 51u
#define CARD_FAKE_ENCODER 61u
#define CARD_FAKE_EDID 20u
#define CARD_FAKE_BLOB 77u
#define CARD_FAKE_FB 100u
#define CARD_FAKE_BYTES (64ul << 20)

static struct
{
        int on, fd;
        unsigned int connectors, connectors_after; // what GETRESOURCES lists, and how many it says the second time (0: as many)
        unsigned int asked[64], asked_count;       // the connectors GETCONNECTOR was asked of
        unsigned short width, height;              // the panel's one mode
        int rotation, refuse_atomic;               // the plane lists a rotation property; the card refuses every commit
        int universal, atomic;                     // the client asked for every plane (the primary is listed), for commits
        unsigned long plane_rotation;              // the plane's turn: the bit the last commit set, rotate-0's at the start
        unsigned int edid_length;
        unsigned char edid[33024];
        unsigned int handles, fbs, fb_width[64], fb_height[64];
        unsigned long offset, handle_offset[64];
        unsigned int crtc_fb, crtc_x, crtc_y, refused, atomic_fb, atomics;
        // The cards the compositor's walk finds (card_fake_node): the minors that are cards, and of those the ones with no
        // connected screen, the ones another program is the master of and the firmware's; the minor that is open now (fd
        // is its descriptor, a copy of base, the memfd), each minor's last descriptor, and the minors opened, in order.
        unsigned long nodes, dark, busy, firmware;
        // What the kernel does when a connector is asked with no room for its modes: it looks. looks counts those calls;
        // unlooked is a card whose connectors the kernel has never looked at, which are of no known state until it does.
        unsigned int looks, unlooked;
        unsigned int minor, opened[160], opens;
        int base, node[64];
        // The crtc's sets in the order they came, 0 for one that turned it off and 1 for one that turned it on (set_count of them, the first 32 kept).
        unsigned char set_was[32];
        unsigned int set_count;
        // The sets that turn the crtc on refused (EINVAL) while this counts down: a link not up yet after a wake.
        unsigned int refuse_sets;
        // The rates the panel lists at its size, in the kernel's order (the preferred first, then the fastest), rate_count of them (0: its one mode
        // at 60 Hz), and the rate of the mode on the crtc (0: 60): what Canvas set at the start, then the last set's.
        unsigned int rates[160], rate_count, crtc_rate; // a connector may list as many modes as the check gives it (130 in one row)
        // A card whose first connector is a ghost: connected at the first look, with a list of modes that grew by the second (so none is read) and an
        // encoder that can have only a crtc the card has not, and whose second connector is the panel, with no encoder of its own yet.
        int ghost;
        // Every DIRTYFB asked of any descriptor (a call on -1 is one too), and the errno the fake card answers them with while it is set (ENOSYS: a card
        // that scans its framebuffer out as it is).
        unsigned int dirty_asks;
        int dirty_errno;
        // The primary plane lists IN_FENCE_FD (in_fence); the last commit's IN_FENCE_FD (-1 for none), its flags and user data; the legacy flips taken while
        // page_flips is set (refused otherwise, EINVAL, as the fake card always did), the last one's framebuffer, and the errno the card answers commits with
        // while it is set (EBUSY for a commit still in flight).
        int in_fence, atomic_in_fence, atomic_errno, page_flips;
        unsigned int atomic_flags, flips, flip_fb;
        unsigned long atomic_user;
} card_fake;

static long card_fake_node(unsigned int minor)
{
        long node;

        if (card_fake.opens < 160)
                card_fake.opened[card_fake.opens] = minor;
        card_fake.opens++;
        if (minor >= 64 || !(card_fake.nodes >> minor & 1))
                return -2;
        node = (long)system_call_3(syscall(fcntl), (positive)card_fake.base, 1030, 200 + minor); // F_DUPFD_CLOEXEC, a number no other minor has
        if (node < 0)
                return node;
        card_fake.fd = card_fake.node[minor] = (int)node;
        card_fake.minor = minor;
        return node;
}

static void card_fake_mode_at(struct compositor_mode address_to mode, unsigned int hz, unsigned int type)
{
        memory_zero(mode, sizeof(*mode));
        mode->clock = 100000 * hz / 60; // the panel's timings at hz
        mode->hdisplay = card_fake.width, mode->hsync_start = card_fake.width + 8, mode->hsync_end = card_fake.width + 16;
        mode->htotal = card_fake.width + 32;
        mode->vdisplay = card_fake.height, mode->vsync_start = card_fake.height + 2, mode->vsync_end = card_fake.height + 4;
        mode->vtotal = card_fake.height + 8;
        mode->vrefresh = hz;
        mode->type = type;
}

static void card_fake_mode(struct compositor_mode address_to mode)
{
        if (card_fake.rate_count || card_fake.crtc_rate)
        {
                card_fake_mode_at(mode, card_fake.crtc_rate ? card_fake.crtc_rate : 60, card_fake.crtc_rate == card_fake.rates[0] ? 8 : 0);
                return;
        }
        memory_zero(mode, sizeof(*mode));
        mode->clock = 100000;
        mode->hdisplay = card_fake.width, mode->hsync_start = card_fake.width + 8, mode->hsync_end = card_fake.width + 16;
        mode->htotal = card_fake.width + 32;
        mode->vdisplay = card_fake.height, mode->vsync_start = card_fake.height + 2, mode->vsync_end = card_fake.height + 4;
        mode->vtotal = card_fake.height + 8;
        mode->vrefresh = 60;
        mode->type = 8; // DRM_MODE_TYPE_PREFERRED
}

static bipolar card_fake_control(positive handle, positive request, positive argument)
{
        static const char address_to const names[12] = {"FB_ID",  "CRTC_ID", "SRC_X",  "SRC_Y",  "SRC_W", "SRC_H",
                                                        "CRTC_X", "CRTC_Y",  "CRTC_W", "CRTC_H", "type",  "rotation"};
        static const char address_to const turns[4] = {"rotate-0", "rotate-90", "rotate-180", "rotate-270"};
        unsigned int listed = card_fake.connectors_after > card_fake.connectors ? card_fake.connectors_after : card_fake.connectors;

        card_fake.dirty_asks += request == COMPOSITOR_DIRTYFB;
        if (request == COMPOSITOR_DIRTYFB && card_fake.on && (bipolar)handle == card_fake.fd && card_fake.dirty_errno)
                return -card_fake.dirty_errno;
        if (!card_fake.on || (bipolar)handle != card_fake.fd)
                return (bipolar)system_call_3(syscall(ioctl), handle, request, argument);
        switch (request)
        {
        case COMPOSITOR_VERSION:
        {
                struct compositor_version address_to v = (struct compositor_version address_to)argument;
                const char address_to name = card_fake.firmware >> card_fake.minor & 1 ? "simpledrm" : "fake";

                for (unsigned long at = 0; name[at] && at < v->name_length; at++)
                        v->name[at] = name[at];
                return 0;
        }
        case COMPOSITOR_SET_MASTER:
                return card_fake.busy >> card_fake.minor & 1 ? -13 : 0;
        case COMPOSITOR_GETRESOURCES:
        {
                struct compositor_card_res address_to r = (struct compositor_card_res address_to)argument;
                // A pointer given is the second call: a card that grew between the two says so then.
                unsigned int now = r->connectors && card_fake.connectors_after ? card_fake.connectors_after : card_fake.connectors;

                for (unsigned int at = 0; at < now && r->connectors; at++)
                        if (at < r->connector_count)
                                ((unsigned int address_to)r->connectors)[at] = CARD_FAKE_CONNECTOR + at;
                if (r->crtcs && r->crtc_count >= 1)
                        ((unsigned int address_to)r->crtcs)[0] = CARD_FAKE_CRTC;
                r->connector_count = now, r->crtc_count = 1, r->encoder_count = 1, r->fb_count = 0;
                r->min_width = r->min_height = 1, r->max_width = r->max_height = 16384;
                return 0;
        }
        case COMPOSITOR_GETCONNECTOR:
        {
                struct compositor_connector address_to c = (struct compositor_connector address_to)argument;
                unsigned int id = c->connector_id;
                int connected = id == CARD_FAKE_CONNECTOR + (card_fake.ghost ? 1u : 0u) && !(card_fake.dark >> card_fake.minor & 1);

                if (!c->mode_count)
                        card_fake.looks++, card_fake.unlooked = 0;
                else if (card_fake.unlooked)
                {
                        c->mode_count = c->encoder_count = c->prop_count = 0;
                        c->encoder_id = 0, c->connection = 3; // unknown
                        return 0;
                }

                if (card_fake.asked_count < 64)
                        card_fake.asked[card_fake.asked_count++] = id;
                if (id < CARD_FAKE_CONNECTOR || id - CARD_FAKE_CONNECTOR >= listed)
                        return -2;
                if (card_fake.ghost && id == CARD_FAKE_CONNECTOR)
                {
                        if (c->encoder_count >= 1 && c->encoders)
                                ((unsigned int address_to)c->encoders)[0] = CARD_FAKE_ENCODER + 1;
                        c->mode_count = c->modes ? 2 : 1; // one more than it was asked with: the kernel copies none
                        c->encoder_count = 1, c->prop_count = 0, c->encoder_id = 0;
                        c->connector_type = 11, c->connector_type_id = 1; // HDMI-A-1
                        c->connection = 1;
                        return 0;
                }
                if (connected && card_fake.rate_count)
                {
                        // Its modes at its size, copied only when they fit what was asked for, as the kernel does: the preferred one first.
                        if (c->mode_count >= card_fake.rate_count && c->modes)
                                for (unsigned int at = 0; at < card_fake.rate_count; at++)
                                        card_fake_mode_at((struct compositor_mode address_to)c->modes + at, card_fake.rates[at], at ? 0 : 8);
                }
                else if (connected && c->mode_count >= 1 && c->modes)
                        card_fake_mode((struct compositor_mode address_to)c->modes);
                if (connected && c->encoder_count >= 1 && c->encoders)
                        ((unsigned int address_to)c->encoders)[0] = CARD_FAKE_ENCODER;
                c->mode_count = c->encoder_count = (unsigned int)connected;
                if (connected && card_fake.rate_count)
                        c->mode_count = card_fake.rate_count;
                c->prop_count = 0;
                c->encoder_id = connected ? CARD_FAKE_ENCODER : 0;
                if (card_fake.ghost)
                        c->encoder_count = 0, c->encoder_id = 0;
                c->connector_type = 14, c->connector_type_id = id - CARD_FAKE_CONNECTOR + 1; // eDP-N
                c->connection = connected ? 1 : 2;
                return 0;
        }
        case COMPOSITOR_GETENCODER:
        {
                struct compositor_encoder address_to e = (struct compositor_encoder address_to)argument;

                if (card_fake.ghost && e->encoder_id == CARD_FAKE_ENCODER + 1)
                {
                        e->crtc_id = 0, e->possible_crtcs = 2, e->possible_clones = 0; // the second crtc, of a card with one
                        return 0;
                }
                if (e->encoder_id != CARD_FAKE_ENCODER)
                        return -2;
                e->crtc_id = CARD_FAKE_CRTC, e->possible_crtcs = 1, e->possible_clones = 0;
                return 0;
        }
        case COMPOSITOR_GETCRTC:
        {
                struct compositor_crtc address_to c = (struct compositor_crtc address_to)argument;

                if (c->crtc_id != CARD_FAKE_CRTC)
                        return -2;
                c->fb_id = card_fake.crtc_fb, c->x = card_fake.crtc_x, c->y = card_fake.crtc_y, c->mode_valid = 1;
                card_fake_mode(&c->mode);
                return 0;
        }
        case COMPOSITOR_OBJ_GETPROPERTIES:
        {
                struct
                {
                        unsigned long props, values;
                        unsigned int count, object, type;
                } address_to g = (void address_to)argument;
                unsigned int ids[16], n = 0;
                unsigned long values[16];

                if (g->object == CARD_FAKE_PLANE)
                {
                        for (unsigned int id = 1; id <= 11; id++)
                                ids[n] = id, values[n++] = id == 11 ? 1 : 0; // type: Primary
                        if (card_fake.rotation)
                                ids[n] = 12, values[n++] = card_fake.plane_rotation;
                        if (card_fake.in_fence)
                                ids[n] = 13, values[n++] = (unsigned long)-1;
                }
                else if (g->object == CARD_FAKE_CONNECTOR + 0 && card_fake.edid_length)
                        ids[n] = CARD_FAKE_EDID, values[n++] = CARD_FAKE_BLOB;
                for (unsigned int at = 0; at < n && at < g->count; at++)
                {
                        if (!g->props || !g->values)
                                return -14;
                        ((unsigned int address_to)g->props)[at] = ids[at];
                        ((unsigned long address_to)g->values)[at] = values[at];
                }
                g->count = n;
                return 0;
        }
        case COMPOSITOR_GETPROPERTY:
        {
                struct
                {
                        unsigned long values, enums;
                        unsigned int id, flags;
                        char name[32];
                        unsigned int count_values, count_enums;
                } address_to p = (void address_to)argument;
                struct
                {
                        unsigned long value;
                        char name[32];
                } address_to into = (void address_to)p->enums;
                unsigned int id = p->id, values = 0, enums = 0;

                if (id == CARD_FAKE_EDID)
                        string_copy((string_address)p->name, (string_address) "EDID");
                else if (id == 13 && card_fake.in_fence)
                        string_copy((string_address)p->name, (string_address) "IN_FENCE_FD"), values = 2; // a signed range
                else if (id >= 1 && id <= 12 && (id < 12 || card_fake.rotation))
                {
                        string_copy((string_address)p->name, (string_address)names[id - 1]);
                        // An object's id is one value (its type), a range two, an enum's and a bitmask's values its entries.
                        values = id <= 2 ? 1 : id <= 10 ? 2 : id == 11 ? 3 : 4;
                        enums = id == 11 ? 3 : id == 12 ? 4 : 0;
                }
                else
                        return -2;
                // drm_mode_getproperty_ioctl: put_user of every value up to the count given, through the pointer given.
                for (unsigned int at = 0; at < values && at < p->count_values; at++)
                {
                        if (!p->values)
                                return -14;
                        ((unsigned long address_to)p->values)[at] = id == 12 ? at : at;
                }
                p->count_values = values;
                for (unsigned int at = 0; at < enums && at < p->count_enums; at++)
                {
                        if (!p->enums)
                                return -14;
                        memory_zero(&into[at], sizeof(into[at]));
                        into[at].value = at;
                        string_copy((string_address)into[at].name, (string_address)(id == 12 ? turns[at] : at == 1 ? "Primary" : "Overlay"));
                }
                p->count_enums = enums;
                return 0;
        }
        case COMPOSITOR_GETPROPBLOB:
        {
                struct
                {
                        unsigned int id, length;
                        unsigned long data;
                } address_to b = (void address_to)argument;

                if (b->id != CARD_FAKE_BLOB || !card_fake.edid_length)
                        return -2;
                if (b->length == card_fake.edid_length)
                {
                        if (!b->data)
                                return -14;
                        memory_copy((void address_to)b->data, card_fake.edid, card_fake.edid_length);
                }
                b->length = card_fake.edid_length;
                return 0;
        }
        case COMPOSITOR_GETPLANERESOURCES:
        {
                struct
                {
                        unsigned long ids;
                        unsigned int count, pad;
                } address_to r = (void address_to)argument;

                // The primary plane is listed only to a client that asked for every plane (universal planes, or atomic).
                if (card_fake.universal && r->count >= 1 && r->ids)
                        ((unsigned int address_to)r->ids)[0] = CARD_FAKE_PLANE;
                r->count = card_fake.universal ? 1 : 0;
                return 0;
        }
        case COMPOSITOR_SET_CLIENT_CAP:
        {
                const unsigned long address_to cap = (const unsigned long address_to)argument;

                if ((cap[0] == 2 || cap[0] == 3) && cap[1])
                        card_fake.universal = 1;
                if (cap[0] == 3 && cap[1])
                        card_fake.atomic = 1;
                return cap[0] == 2 || cap[0] == 3 ? 0 : -22;
        }
        case COMPOSITOR_GETPLANE:
        {
                struct
                {
                        unsigned int id, crtc, fb, possible, gamma, count;
                        unsigned long formats;
                } address_to p = (void address_to)argument;

                if (p->id != CARD_FAKE_PLANE)
                        return -2;
                if (p->count >= 1 && p->formats)
                        ((unsigned int address_to)p->formats)[0] = COMPOSITOR_XRGB8888;
                p->crtc = CARD_FAKE_CRTC, p->fb = card_fake.crtc_fb, p->possible = 1, p->gamma = 0, p->count = 1;
                return 0;
        }
        case COMPOSITOR_CREATE_DUMB:
        {
                struct compositor_dumb address_to d = (struct compositor_dumb address_to)argument;
                unsigned long pitch = ((unsigned long)d->width * 4 + 63) & ~63ul, size = (pitch * d->height + 4095) & ~4095ul;

                if (card_fake.handles >= 63 || card_fake.offset + size > CARD_FAKE_BYTES)
                        return -12;
                d->handle = ++card_fake.handles;
                d->pitch = (unsigned int)pitch;
                d->size = size;
                card_fake.handle_offset[d->handle] = card_fake.offset;
                card_fake.offset += size;
                return 0;
        }
        case COMPOSITOR_ADDFB:
        {
                struct compositor_fb address_to f = (struct compositor_fb address_to)argument;

                if (card_fake.fbs >= 63)
                        return -12;
                card_fake.fbs++;
                card_fake.fb_width[card_fake.fbs] = f->width, card_fake.fb_height[card_fake.fbs] = f->height;
                f->fb_id = CARD_FAKE_FB + card_fake.fbs;
                return 0;
        }
        case COMPOSITOR_MAP_DUMB:
        {
                struct compositor_map_dumb address_to m = (struct compositor_map_dumb address_to)argument;

                if (!m->handle || m->handle > card_fake.handles)
                        return -2;
                m->offset = card_fake.handle_offset[m->handle];
                return 0;
        }
        case COMPOSITOR_SETCRTC:
        {
                struct compositor_crtc address_to s = (struct compositor_crtc address_to)argument;
                unsigned int fb = s->fb_id - CARD_FAKE_FB, across = s->mode.hdisplay, down = s->mode.vdisplay, swap = across;

                if (!s->fb_id || !s->mode_valid)
                {
                        if (card_fake.set_count < 32)
                                card_fake.set_was[card_fake.set_count] = 0;
                        card_fake.set_count++;
                        card_fake.crtc_fb = 0;
                        return 0;
                }
                if (!fb || fb > card_fake.fbs)
                        return -2;
                if (card_fake.refuse_sets)
                {
                        card_fake.refuse_sets--;
                        card_fake.refused++;
                        return -22;
                }
                if (card_fake.set_count < 32)
                        card_fake.set_was[card_fake.set_count] = 1;
                card_fake.set_count++;
                // The viewport, with the primary plane's turn as the kernel takes it: a quarter turn swaps the mode's sides.
                if (card_fake.plane_rotation & (2 | 8))
                        across = down, down = swap;
                if (s->x + across > card_fake.fb_width[fb] || s->y + down > card_fake.fb_height[fb])
                {
                        card_fake.refused++;
                        return -28;
                }
                card_fake.crtc_fb = s->fb_id, card_fake.crtc_x = s->x, card_fake.crtc_y = s->y;
                if (card_fake.rate_count)
                        card_fake.crtc_rate = s->mode.vrefresh;
                return 0;
        }
        case COMPOSITOR_ATOMIC:
        {
                struct
                {
                        unsigned int flags, count;
                        unsigned long objects, counts, props, values, reserved, user_data;
                } address_to a = (void address_to)argument;
                const unsigned int address_to objects = (const unsigned int address_to)a->objects;
                const unsigned int address_to counts = (const unsigned int address_to)a->counts;
                const unsigned int address_to props = (const unsigned int address_to)a->props;
                const unsigned long address_to values = (const unsigned long address_to)a->values;
                unsigned long fb = 0, rotation = 0;
                unsigned int at = 0;

                card_fake.atomics++;
                if (!card_fake.atomic)
                        return -95; // EOPNOTSUPP: the client did not ask for commits
                if (card_fake.refuse_atomic)
                        return -22;
                if (card_fake.atomic_errno)
                        return -card_fake.atomic_errno;
                card_fake.atomic_in_fence = -1, card_fake.atomic_flags = a->flags, card_fake.atomic_user = a->user_data;
                for (unsigned int object = 0; object < a->count; object++)
                        for (unsigned int each = 0; each < counts[object]; each++, at++)
                                if (objects[object] == CARD_FAKE_PLANE && props[at] == 1)
                                        fb = values[at];
                                else if (objects[object] == CARD_FAKE_PLANE && props[at] == 12)
                                        rotation = values[at];
                                else if (objects[object] == CARD_FAKE_PLANE && props[at] == 13 && card_fake.in_fence)
                                        card_fake.atomic_in_fence = (int)(long)values[at];
                                else if (objects[object] == CARD_FAKE_PLANE && props[at] == 13)
                                        return -22; // a property the plane has not
                if (fb)
                        card_fake.atomic_fb = card_fake.crtc_fb = (unsigned int)fb;
                if (rotation)
                        card_fake.plane_rotation = rotation;
                return 0;
        }
        case COMPOSITOR_PAGE_FLIP:
        {
                const unsigned int address_to f = (const unsigned int address_to)argument; // crtc, fb, flags

                if (!card_fake.page_flips)
                        return -22;
                card_fake.flips++, card_fake.flip_fb = f[1];
                return 0;
        }
        case COMPOSITOR_RMFB:
        case COMPOSITOR_GEM_CLOSE:
        case COMPOSITOR_DIRTYFB:
                return 0;
        }
        return -22;
}

// The fake card, a panel of width by height, as compositor.card; false when its memfd is not made.
static b32 card_fake_open(unsigned short width, unsigned short height)
{
        memory_zero(&card_fake, sizeof(card_fake));
        card_fake.fd = (int)system_call_2(syscall(memfd_create), (positive) "card-fake", 1);
        if (card_fake.fd < 0 || system_call_2(syscall(ftruncate), (positive)card_fake.fd, CARD_FAKE_BYTES) != 0)
                return false;
        card_fake.on = 1;
        card_fake.connectors = 1;
        card_fake.width = width, card_fake.height = height;
        card_fake.plane_rotation = 1; // rotate-0
        compositor.card = card_fake.fd;
        return true;
}

static fn card_fake_close(void)
{
        system_close(card_fake.fd);
        card_fake.on = 0;
        card_fake.fd = -1;
        compositor.card = -1;
}
#define UI_SHARED_ONLY
#include "../../src/canvas/ui/ui.c"
#undef UI_SHARED_ONLY
#ifdef CHECK_terminal
static void rtl_keep_add(int x, int y, int w, int h);
#define UI_KEEP(x, y, w, h) rtl_keep_add((x), (y), (w), (h))
#endif
/*
        A popup's row, for the checks of a menu an app opens for itself (a choice's in the settings, ui_popmenu_open). No
        compositor runs here, so the table a program takes its row from is not there, and a menu that cannot take a row
        does not open. The kit's take goes through popup_fake_take: while a check has said how tall a screen its popups
        stand on, the take gives row 0 of a table made for it (an anonymous mapping with no door and no wake, which
        windows_leave unmaps as it does the compositor's), and with none said it is the take it always was.
*/
static struct
{
        unsigned int screen; // the screen's height, in pixels, or 0: no check stands in for the compositor
        unsigned int themed; // the theme's count the table says: the kit's own, so that the popup takes no theme from the table
} popup_fake;

static long popup_fake_take(struct window_row address_to own)
{
        const unsigned int stride = 2048; // wider than any menu a check opens, and whole pages a row
        unsigned long buffer = (unsigned long)stride * popup_fake.screen * 4, bytes = WINDOWS_HEADER + 2 * buffer;
        struct windows address_to table;
        long mapped;

        if (!popup_fake.screen)
                return windows_take(own);
        mapped = compositor_call(syscall(mmap), 0, bytes, 3, 0x4022, (positive)-1, 0);
        if (mapped < 0 && mapped > -4096)
                return mapped;
        table = (struct windows address_to)mapped;
        table->stride = stride, table->buffer = buffer;
        table->output_width = stride, table->output_height = popup_fake.screen;
        table->themed = popup_fake.themed;
        address_to own = (struct window_row){table, 0, -1, -1, bytes, buffer, stride, stride, popup_fake.screen};
        return 0;
}
#define windows_take(own) popup_fake_take(own)
#ifdef CHECK_terminal
/*
        What the kit hands the font engine to draw, heard while a check listens (kit_seen.on): each run's text, its length, size,
        flags and faces, for the run's order on the screen to be read back (kit_bidi_check). Every run is drawn as it was.
*/
#define KIT_SEEN_RUNS 8
#define KIT_SEEN_TEXT 256
static struct
{
        b32 on;
        int count;
        p8 text[KIT_SEEN_RUNS][KIT_SEEN_TEXT];
        positive length[KIT_SEEN_RUNS];
        p32 size[KIT_SEEN_RUNS], flags[KIT_SEEN_RUNS], faces_count[KIT_SEEN_RUNS];
        const font_face address_to faces[KIT_SEEN_RUNS][2];
} kit_seen;

static b64 kit_seen_draw(font_engine address_to e, const font_face address_to const address_to faces, p32 count, const p8 address_to text,
                         positive length, p32 size, p32 flags, const font_target address_to target, b32 x, b32 baseline, p32 colour,
                         p32 background)
{
        if (kit_seen.on && kit_seen.count < KIT_SEEN_RUNS && length < KIT_SEEN_TEXT && count >= 1 && count <= 2)
        {
                int k = kit_seen.count++;

                memory_copy(kit_seen.text[k], text, length);
                kit_seen.text[k][length] = 0;
                kit_seen.length[k] = length, kit_seen.size[k] = size, kit_seen.flags[k] = flags, kit_seen.faces_count[k] = count;
                kit_seen.faces[k][0] = faces[0], kit_seen.faces[k][1] = count > 1 ? faces[1] : faces[0];
        }
        return font_draw(e, faces, count, text, length, size, flags, target, x, baseline, colour, background);
}
#define font_draw kit_seen_draw
#endif
#if MOONWATER_UI_ANY
#include "../../src/canvas/ui/ui.c"
// A menu's row as its tree laid it out: the height of its first item's element, which is a row in every menu a check makes.
static int popmenu_row(const struct ui_popmenu address_to m)
{
        return m->tree.node[m->items[0].node].h;
}
#endif
#ifdef CHECK_terminal
#undef font_draw
#endif
#undef windows_take
#include "../../src/canvas/ui/terminal.c"
#if MOONWATER_UI_SETTINGS
#include "../../src/canvas/ui/gallery.c"
#include "../../src/canvas/ui/settings.c"
#endif
#if MOONWATER_UI_LAUNCHER
#include "../../src/canvas/ui/launcher.c"
#endif
#if MOONWATER_UI_IDENTIFY
#include "../../src/canvas/ui/identify.c"
#endif
#if MOONWATER_UI_EDITOR
#include "../../src/canvas/ui/editor.c"
#endif
#if MOONWATER_UI_TASKBAR
#include "../../src/canvas/ui/taskbar.c"
#endif
#if MOONWATER_UI_MIXER
#ifndef offset_of
#include <stddef.h>
#define offset_of(type, member) offsetof(type, member)
#endif
#ifndef offset_of
#define offset_of(type, member) ((unsigned long)&((type *)0)->member)
#endif
#ifndef memory_fill
#define memory_fill(block, value, count) memset((block), (value), (count))
#endif
#ifndef memory_copy_apart
#define memory_copy_apart(to, from, size) memcpy((to), (from), (size))
#endif
#ifndef memory_zero
#define memory_zero(to, size) memset((to), 0, (size))
#endif
#ifndef memory_copy
#define memory_copy(to, from, size) memmove((to), (from), (size))
#endif
#ifndef memory_first_of
#define memory_first_of(block, value, size) memchr((block), (value), (size))
#endif
#ifndef string_length
#define string_length(text) strlen((const char *)(text))
#endif
#include "../../src/audio/mix.c"
#include "../../src/audio/pulse.c"
#include "../../src/canvas/ui/mixer.c"
#endif
#if MOONWATER_UI_NOTICES && MOONWATER_UI_TASKBAR
#include "../../src/canvas/ui/notices.c"
#endif

#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

/*
        The terminal's frames against a full draw.

        A frame draws only the cells that differ from what its buffer holds
        and names only the rows the compositor has to read again, so the two
        ways it can be wrong are a cell left as it was and a row left out of
        the damage. Both show in one comparison: a model of the compositor
        keeps a screen and copies into it, from each committed buffer, what
        the damage says, and after every frame the screen and the buffer
        drawn into have to be the picture that drawing every cell of the
        grid from nothing makes.

        What goes in is the emulator's own work -- text, colours, wide and
        drawn characters, erases, scrolls, lines put in and taken out -- with
        the wheel and resizes between, and the frames come at random with the
        other buffer released or not, so buffers are drawn a frame behind, two
        behind, and not at all.

        The fonts are the arguments, the repository's own: the first is what
        the grid is drawn with and the second its bold, and every one after
        them, with the second, has to load and have a letter A, which is the
        four files the image ships.
*/
static p64 render_state = 0x9e3779b97f4a7c15ull;

static p32 render_next(p32 below)
{
        XORSHIFT64(render_state);
        return below ? (p32)((render_state >> 11) % below) : 0;
}

static p32 address_to render_map(positive bytes)
{
        long mapped = system_call_6(syscall(mmap), 0, bytes, 3, 0x4022, (positive)-1, 0);

        return mapped < 0 && mapped > -4096 ? 0 : (p32 address_to)mapped;
}

// What the emulator is told next: a piece of everything a terminal is sent.
static const char address_to render_pieces[] = {
    "hello world",
    "The quick brown fox",
    "\r\n",
    "\r\n",
    "\r\n",
    "\t",
    "\b",
    "\x1b[H",
    "\x1b[2J",
    "\x1b[K",
    "\x1b[1K",
    "\x1b[2K",
    "\x1b[1;31m",
    "\x1b[42m",
    "\x1b[0m",
    "\x1b[7m",
    "\x1b[1m",
    "\x1b[4m",
    "\x1b[9m",
    "\x1b[2m",
    "\x1b[8m",
    "\x1b[38;5;208m",
    "\x1b[48;5;24m",
    "\x1b[3A",
    "\x1b[2B",
    "\x1b[5C",
    "\x1b[4D",
    "\x1b[5;7H",
    "\x1b[2L",
    "\x1b[2M",
    "\x1b[3P",
    "\x1b[3@",
    "\x1b[2S",
    "\x1b[2T",
    "\x1b[2;6r",
    "\x1b[r",
    "\x1b[?1049h",
    "\x1b[?1049l",
    "\x1b[?25l",
    "\x1b[?25h",
    "\x1b[5 q",
    "\x1b[2 q",
    "\xe4\xb8\xad\xe6\x96\x87",
    "\xe2\x94\x80\xe2\x94\x82\xe2\x94\x8c",
    "\xe2\x96\x88\xe2\x96\x91",
    "caf\xc3\xa9",
    "0123456789abcdefghijklmnopqrstuvwxyz0123456789",
    "\x1b[1;1H\x1b[44m\x1b[K",
};
#define RENDER_PIECES array_count(render_pieces)

/*
        The compositor, as far as the terminal can tell: a screen it copies
        the shown buffer's damage into, as compositor_take and
        compositor_scene would. A width of 0 is all of it, a height of 0
        none.
*/
static p32 address_to render_screen;

static fn render_apply(const p32 address_to buffer)
{
        struct windows address_to table = terminal.own.table;
        long x1 = table->damage_x[0] < 0 ? 0 : table->damage_x[0], y1 = table->damage_y[0] < 0 ? 0 : table->damage_y[0];
        long x2 = (long)table->damage_x[0] + table->damage_width[0], y2 = (long)table->damage_y[0] + table->damage_height[0];

        if (table->damage_width[0] <= 0)
        {
                for (long y = 0; y < (long)terminal.height; y++)
                        memory_copy(render_screen + y * terminal.pitch, buffer + y * terminal.pitch,
                                    (positive)terminal.width * 4);
                return;
        }

        x2 = x2 > (long)terminal.width ? (long)terminal.width : x2;
        y2 = y2 > (long)terminal.height ? (long)terminal.height : y2;
        for (long y = y1; y < y2 && x1 < x2; y++)
                memory_copy(render_screen + y * terminal.pitch + x1, buffer + y * terminal.pitch + x1,
                            (positive)(x2 - x1) * 4);
}

// Whether two pictures of the window differ anywhere in it: rows are terminal.pitch apart, and the part past
// the window's width is not the window's.
static b32 render_differs(const p32 address_to a, const p32 address_to b)
{
        for (unsigned int y = 0; y < terminal.height; y++)
                if (memory_compare(a + (positive)y * terminal.pitch, b + (positive)y * terminal.pitch,
                                   (positive)terminal.width * 4))
                        return true;
        return false;
}

// The picture of the cells in the frame just drawn, every one of them, from nothing.
static fn render_reference(p32 address_to into, const struct window_cell address_to cells)
{
        unsigned int first, shown, total;

        for (unsigned int y = 0; y < terminal.height; y++)
                memory_fill_u32(into + (positive)y * terminal.pitch, terminal.width, 0x123456);
        for (unsigned int row = 0; row < ROWS; row++)
        {
                terminal_row(&terminal_painters[0], terminal_grid(into), row, cells + (unsigned long)row * COLUMNS, null, 0,
                             COLUMNS);
                terminal_margin(into, cells, row, 0, COLUMNS);
        }
        terminal_extent(&first, &shown, &total);
        terminal_bar(into, cells, first, shown, total);
}

/*
        The render rig's lists: while render_listing is on, the window's frames are recorded into its list (ui_app_list, the table's list
        mode on, as ui_app_run leaves them for the terminal) and each frame the list took is replayed here, kinds 0 (fills) and 10 (glyphs
        through their tones), and compared with the processor's full draw. The bar's track is the kit's boxes, which this replay does not
        draw, so the columns left of it are compared, and the rows of all of them.
*/
static b32 render_listing;
static positive render_list_frames;

static positive render_clock(void)
{
        positive when[2] = {0, 0};

        system_call_2(syscall(clock_gettime), 1, (positive)address_of when);
        return when[0] * 1000000000ull + when[1];
}
static unsigned int address_to render_replay_pixels, address_to render_full_pixels;

static unsigned int render_generation(void)
{
        return render_listing && ui_app_list.base ? ((unsigned int address_to)ui_app_list.base)[UI_DRAW_LIST_GEN] : 0;
}

static void render_replay(const struct ui_draw_view address_to view, unsigned int address_to pixels, int pitch)
{
        const unsigned char address_to base = ui_app_list.base;

        for (unsigned int i = 0; i < view->count; i++)
        {
                const unsigned int address_to op = view->ops + 8 * (unsigned long)i;
                unsigned int kind = op[0] & 255;
                int x0 = (int)op[1], y0 = (int)op[2], x1 = (int)op[3], y1 = (int)op[4];

                if (kind == 0)
                        for (int y = y0; y < y1; y++)
                                for (int x = x0; x < x1; x++)
                                        pixels[(positive)y * pitch + x] = op[5];
                else if (kind == 10)
                {
                        const unsigned int address_to tones = (const unsigned int address_to)(base + op[5]);
                        const unsigned char address_to cover = base + op[7];
                        positive stride = op[0] >> 8;

                        for (int y = y0; y < y1; y++)
                                for (int x = x0; x < x1; x++)
                                        pixels[(positive)y * pitch + x] = tones[cover[(positive)(y - y0) * stride + (x - x0)]];
                }
                // The kit's boxes and rings (kinds 2, 8, 9) are the bar's track: the columns it covers are not compared.
        }
}

// The list the window's frame took, replayed and compared with the full draw of the cells it was made from (terminal.frame[2]).
static b32 render_list_check(void)
{
        struct ui_draw_view view;
        unsigned int first, shown, total;
        int left = (int)terminal.width;

        if (!render_replay_pixels)
                render_replay_pixels = render_map((positive)2048 * 1280 * 4), render_full_pixels = render_map((positive)2048 * 1280 * 4);
        if (ui_draw_list_open((unsigned char address_to)ui_app_list.base, WINDOW_LIST_BYTES, &view) != 1)
                return false;
        for (positive y = 0; y < terminal.height; y++)
                memory_fill_u32(render_replay_pixels + y * terminal.pitch, terminal.width, 0xabcdefu);
        render_replay(&view, render_replay_pixels, (int)terminal.pitch);
        render_reference(render_full_pixels, terminal.frame[2]);
        terminal_extent(&first, &shown, &total);
        if (total > shown && terminal.width >= UI_SCROLL_WIDTH * (unsigned int)terminal_scale() / 100)
                left = (int)terminal.width - (int)(UI_SCROLL_WIDTH * (unsigned int)terminal_scale() / 100);
        for (positive y = 0; y < terminal.height; y++)
                for (int x = 0; x < left; x++)
                        if (render_replay_pixels[y * terminal.pitch + (positive)x] != render_full_pixels[y * terminal.pitch + (positive)x])
                                return false;
        return true;
}

// A window of the size the grid needs in a table of its own, its rows as long as it is wide, and nothing on
// any screen yet. The mappings are made once, for the largest grid the steps below ask for.
static fn render_surface(void)
{
        static unsigned long reserved, header;
        static struct windows address_to table;
        unsigned int pixels_h, pixels_w = terminal_pixels(COLUMNS, ROWS, &pixels_h);

        if (!table)
        {
                // A window of the largest grid the steps make: 50 by 14 cells at the largest text, 39 by 84 px a cell.
                reserved = ((unsigned long)2048 * 1280 * 4 + 4095) & ~4095ul;
                header = (sizeof(struct windows) + 4095) & ~4095ul;
                table = (struct windows address_to)system_call_6(syscall(mmap), 0, header + reserved * 2, 3, 0x4022,
                                                                 (positive)-1, 0);
                render_screen = render_map(reserved);
        }

        windows_scratch_zero(table); // the header as it was mapped: its pages given back, not written
        table->pixels = header;
        table->buffer = reserved;
        table->output_width = table->output_height = 1u << 14;
        // No door: the knock goes nowhere.
        terminal.own = (struct window_row){table, 0, -1, -1, header + reserved * 2};
        terminal.width = pixels_w;
        terminal.height = pixels_h;
        terminal.pitch = 2048;
        table->stride = 2048;
        terminal.shown = -1;
        terminal_frames();
        if (render_listing)
                table->list_mode = 1;
        memory_fill_u32(render_screen, (positive)2048 * pixels_h, 0xdeadbe);
}

static b32 render_step(positive address_to compared, positive address_to wrong, positive step)
{
        p32 what = render_next(100);
        p8 bytes[256];

        if (what < 55)
        {
                const char address_to piece = render_pieces[render_next(RENDER_PIECES)];
                positive length = string_length((string_address)piece);

                memory_copy(bytes, piece, length);
                cursor_hide();
                term_bytes(bytes, length);
                to_shell_length = 0;
                cursor_show();
                return true;
        }

        if (what < 65)
        {
                terminal_view_scroll((int)render_next(9) - 4);
                return true;
        }

        if (what < 68)
        {
                // Another size: the emulator regrids, a new window is made, nothing is on the screen.
                window->columns = 1 + render_next(50);
                window->rows = 1 + render_next(14);
                cursor_hide();
                regrid(-1);
                terminal_view_clamp();
                render_surface();
                cursor_show();
                return true;
        }

        if (what < 71)
        {
                // The terminal has been left alone: the buffer that is not on the screen is
                // given back, and reads as nothing but junk until it is drawn again.
                int spare = terminal.shown < 0 ? -1 : 1 - terminal.shown;

                if (spare >= 0)
                {
                        terminal.own.table->shown[0] = terminal.own.table->frame[0];
                        terminal_discard();
                        if (!terminal.valid[spare])
                                memory_fill_u32(windows_buffer(terminal.own.table, 0, spare),
                                                (positive)terminal.pitch * terminal.height, 0xbad000);
                }
                return true;
        }

        if (what < 73)
        {
                // A text size: the metrics are taken again and the cells cut again to the window, which keeps its
                // pixels, from a window of any size, short ones and a view scrolled back included.
                cursor_hide();
                terminal_text_set(-1, terminal_steps[render_next(TERMINAL_STEPS)]);
                cursor_show();
                return true;
        }

        {
                // A frame: the last picture is taken, so the buffer not on the screen is free, or it is not.
                struct windows address_to table = terminal.own.table;

                table->shown[0] = render_next(3) ? table->frame[0] : table->frame[0] - 1;

                unsigned int listed = render_generation();

                if (!terminal_frame())
                {
                        // Only a window whose last picture was not taken may be refused.
                        (void)step;
                        return terminal.shown >= 0 && table->shown[0] != table->frame[0];
                }

                // A frame the list took (its generation moved) is replayed and compared, and no picture was drawn.
                if (render_listing && render_generation() != listed)
                {
                        address_to compared += 1;
                        render_list_frames++;
                        if (!render_list_check())
                        {
                                address_to wrong += 1;
                                if (address_to wrong < 4)
                                        string_format(log, "  frame %p: the list does not replay to the full draw\n", step);
                        }
                        return true;
                }

                {
                        p32 address_to buffer = windows_buffer(table, 0, terminal.shown);
                        static p32 address_to reference;

                        if (!reference)
                                reference = render_map((positive)2048 * 1280 * 4);

                        render_apply(buffer);
                        // The full draw is of the shown picture's own caret: its cell, cover, shape and marks, as the buffer holds them.
                        terminal_caret_index = terminal.caret_at[terminal.shown];
                        terminal_caret_cover = terminal.caret_cover[terminal.shown];
                        terminal_caret_block = terminal.caret_block[terminal.shown];
                        terminal_caret_marks = terminal.caret_marks[terminal.shown];
                        render_reference(reference, terminal.frame[terminal.shown]);
                        address_to compared += 1;
                        if (render_differs(render_screen, reference) || render_differs(buffer, reference))
                        {
                                address_to wrong += 1;
                                if (address_to wrong < 4)
                                {
                                        positive x = 0, y = 0;

                                        string_format(log, "  frame %p: the screen or the buffer is not the full draw\n", step);
                                        while (y < terminal.height && render_screen[y * terminal.pitch + x] == reference[y * terminal.pitch + x] &&
                                               buffer[y * terminal.pitch + x] == reference[y * terminal.pitch + x])
                                        {
                                                if (++x == terminal.width)
                                                        x = 0, y++;
                                        }
                                        string_format(log, "    first pixel %p,%p (cell %p,%p of %p by %p): screen %p buffer %p full %p, damage %p, shown %b\n",
                                                      x, y, x / terminal.cell_w, y / terminal.cell_h, (positive)COLUMNS, (positive)ROWS,
                                                      (positive)render_screen[y * terminal.pitch + x], (positive)buffer[y * terminal.pitch + x],
                                                      (positive)reference[y * terminal.pitch + x],
                                                      (positive)terminal.own.table->damage_width[0], (bipolar)terminal.shown);
                                }
                        }
                }
        }

        return true;
}

/*
        What the keyboard says, turned into what the shell is sent: the
        compositor's key events with the codes and characters its keymap
        gives, against the bytes the kernel's window sent for the same
        keys. A letter with Control is the control code, the key labelled
        Backspace is DEL, Return is a carriage return, and the keys that are
        no character are the sequences ANSI names, with what was held written
        into them.
*/
/*
        The terminal's selection auto-scroll: a drag held past the grid's top scrolls the scrollback back (the view goes up through
        what has been said), one held below it scrolls forward to the live end, a line at a time with the time, and the
        selection's end follows the text. A mouse and a finger that has held a word, each both ways.
*/

// An event to the terminal as the app's loop gives it (ui_app_run): the kit's context for the event first (a finger's, the time).
static b64 deliver_clock;

// What the loop gives every event (ui_event_context), with the time stepped on the test's own clock: a frame an event, from the later of
// the kit's clock and the last event's, so a drag delivered move after move has the speed it was made with and not an instant's.
static fn deliver_context(const struct window_event address_to event)
{
        deliver_clock = (ui_kit.now > deliver_clock ? ui_kit.now : deliver_clock) + 16666667ll;
        ui_event_context(event);
        ui_kit.now = deliver_clock;
}

static b32 terminal_deliver(b32 master, const struct window_event address_to event, b32 address_to dirty)
{
        deliver_context(event);
        return terminal_receive(master, event, dirty);
}

// The autoscroll of a selection at a time the row drives: the kit's clock is that time too (ui_edge_steps reads it).
static positive check_select_autoscroll_at(positive when, int address_to dirty)
{
        ui_kit.now = ui_kit.test_clock = when;
        return terminal_select_autoscroll(when, dirty);
}

static void check_terminal_select_scroll(void)
{
    b32 dirty = false;
    int x = (int)terminal.pad + 4 * (int)terminal.cell_w + (int)terminal.cell_w / 2, top = (int)terminal.pad_y;
    int cell = (int)terminal.cell_h;
    unsigned int line_before, back;
    positive now = 1000000000ull;
    struct window_event at = {WINDOW_EVENT_MOTION, 0, 0, 0};
    struct window_event press = {WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN, 0x110, 0};
    struct window_event release = {WINDOW_EVENT_BUTTON, 0, 0x110, 0};
    struct window_event touch_at = {WINDOW_EVENT_MOTION, WINDOW_KEY_TOUCH, 0, 0};
    struct window_event land = {WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN | WINDOW_KEY_TOUCH, 0x110, 0};
    struct window_event held = {WINDOW_EVENT_BUTTON, WINDOW_KEY_TOUCH | WINDOW_KEY_CANCEL | WINDOW_KEY_HOLD, 0x110, 0};
    struct window_event right_down = {WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN | WINDOW_KEY_TOUCH | WINDOW_KEY_HOLD, 0x111, 0};
    struct window_event right_up = {WINDOW_EVENT_BUTTON, WINDOW_KEY_TOUCH | WINDOW_KEY_HOLD, 0x111, 0};
    struct window_event lift = {WINDOW_EVENT_BUTTON, WINDOW_KEY_TOUCH | WINDOW_KEY_CANCEL | WINDOW_KEY_HOLD, 0x110, 0};

    for (int line = 0; line < 3 * (int)ROWS; line++)
        term_bytes("scrollback\r\n", 12);

    /* A mouse, above the grid: pressed on the third row, dragged up past the grid's top; the view goes back. */
    terminal_view_live();
    terminal_select_clear(), terminal.sel.dragging = terminal.sel.handles = false;
    at.a = x, at.b = top + 2 * cell + cell / 2;
    terminal_deliver(-1, &at, &dirty);
    terminal_deliver(-1, &press, &dirty);
    line_before = TERMINAL_LINE_OF(terminal.sel_active);
    at.b = top - 25;
    terminal_deliver(-1, &at, &dirty);
    for (int step = 0; step <= 40; step++)
        check_select_autoscroll_at(now + (positive)step * 16000000ull, &dirty);
    check("terminal: a mouse held above the grid scrolls the scrollback back", terminal.view != TERMINAL_LIVE);
    check("terminal: and the selection's end follows the text up", TERMINAL_LINE_OF(terminal.sel_active) < line_before);
    terminal_deliver(-1, &release, &dirty);

    /* A mouse, below the grid: from the scrollback, pressed mid-grid and dragged down past its bottom; the view goes forward. */
    terminal_view_live();
    terminal_view_scroll(20);
    back = terminal.view;
    terminal_select_clear(), terminal.sel.dragging = terminal.sel.handles = false;
    at.a = x, at.b = top + (int)ROWS / 2 * cell + cell / 2;
    terminal_deliver(-1, &at, &dirty);
    terminal_deliver(-1, &press, &dirty);
    line_before = TERMINAL_LINE_OF(terminal.sel_active);
    at.b = top + (int)ROWS * cell + 25;
    terminal_deliver(-1, &at, &dirty);
    for (int step = 0; step <= 40; step++)
        check_select_autoscroll_at(now + (positive)step * 16000000ull, &dirty);
    check("terminal: a mouse held below the grid scrolls the scrollback forward", terminal.view == TERMINAL_LIVE || terminal.view > back);
    check("terminal: and the selection's end follows the text down", TERMINAL_LINE_OF(terminal.sel_active) > line_before);
    terminal_deliver(-1, &release, &dirty);

    /* A finger: held on the third row for the long press (its word selected), then dragged up past the grid's top. */
    terminal_view_live();
    terminal_select_clear(), terminal.sel.dragging = terminal.sel.handles = false;
    touch_at.a = x, touch_at.b = top + 2 * cell + cell / 2;
    terminal_deliver(-1, &touch_at, &dirty);
    terminal_deliver(-1, &land, &dirty);
    terminal_deliver(-1, &held, &dirty);
    terminal_deliver(-1, &right_down, &dirty);
    terminal_deliver(-1, &right_up, &dirty);
    line_before = TERMINAL_LINE_OF(terminal.sel_active);
    touch_at.b = top - 25;
    terminal_deliver(-1, &touch_at, &dirty);
    for (int step = 0; step <= 40; step++)
        check_select_autoscroll_at(now + (positive)step * 16000000ull, &dirty);
    check("terminal: a finger held above the grid scrolls the scrollback back", terminal.view != TERMINAL_LIVE);
    check("terminal: and the held selection's end follows the text up", TERMINAL_LINE_OF(terminal.sel_active) < line_before);
    terminal_deliver(-1, &lift, &dirty);
    terminal_view_live();
    terminal_select_clear(), terminal.sel.dragging = terminal.sel.handles = false;
}

/*
        The speed of the selection's edge scroll is the kit's, at the window's scale: 120 logical pixels a second at the edge and 6
        more for each logical pixel past it. A mouse 25 pixels above the grid at 100% is 270 pixels a second, so 40 steps of 16 ms
        (0.64 s) move 172.8 pixels: about 10 lines at a 17 pixel row. It moved about 450.
*/
static void check_terminal_select_speed(void)
{
    b32 dirty = false;
    int x = (int)terminal.pad + 4 * (int)terminal.cell_w + (int)terminal.cell_w / 2, top = (int)terminal.pad_y;
    int cell = (int)terminal.cell_h;
    unsigned int first_before, first_after, shown, total;
    long moved, want;
    positive now = 1000000000ull;
    struct window_event at = {WINDOW_EVENT_MOTION, 0, 0, 0};
    struct window_event press = {WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN, 0x110, 0};
    struct window_event release = {WINDOW_EVENT_BUTTON, 0, 0x110, 0};

    for (int line = 0; line < 3 * (int)ROWS; line++)
        term_bytes("scrollback\r\n", 12);
    terminal_view_live();
    terminal_select_clear(), terminal.sel.dragging = terminal.sel.handles = false;
    at.a = x, at.b = top + 2 * cell + cell / 2;
    terminal_deliver(-1, &at, &dirty);
    terminal_deliver(-1, &press, &dirty);
    at.b = top - 25;
    terminal_deliver(-1, &at, &dirty);
    check_select_autoscroll_at(now, &dirty);
    terminal_extent(&first_before, &shown, &total);
    for (int step = 1; step <= 40; step++)
        check_select_autoscroll_at(now + (positive)step * 16000000ull, &dirty);
    terminal_extent(&first_after, &shown, &total);
    moved = (long)first_before - (long)first_after;
    want = 17280l / (100l * (long)cell);
    check("terminal: a drag 25 pixels past the grid's top at scale 100 scrolls about 172.8 pixels in 0.64 s, not 100 times that",
          moved >= want - 1 && moved <= want + 1);
    terminal_deliver(-1, &release, &dirty);
    terminal_view_live();
    terminal_select_clear(), terminal.sel.dragging = terminal.sel.handles = false;
}

/*
        A press in the grid is a press of its own: the edge a drag last held past (select_edge.past) is the drag's, and the press that
        follows a release has none. Held still in the grid, the selection does not scroll, and its end stays where the press put it.
*/
static void check_terminal_select_stale(void)
{
    b32 dirty = false;
    int x = (int)terminal.pad + 4 * (int)terminal.cell_w + (int)terminal.cell_w / 2, top = (int)terminal.pad_y;
    int cell = (int)terminal.cell_h;
    unsigned int before, line_after;
    positive now = 1000000000ull;
    struct window_event at = {WINDOW_EVENT_MOTION, 0, 0, 0};
    struct window_event press = {WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN, 0x110, 0};
    struct window_event release = {WINDOW_EVENT_BUTTON, 0, 0x110, 0};

    for (int line = 0; line < 3 * (int)ROWS; line++)
        term_bytes("scrollback\r\n", 12);

    /* A drag held past the grid's top, released; then a press in the grid, held still for two seconds of steps. */
    terminal_view_live();
    terminal_select_clear(), terminal.sel.dragging = terminal.sel.handles = false;
    at.a = x, at.b = top + 2 * cell + cell / 2;
    terminal_deliver(-1, &at, &dirty);
    terminal_deliver(-1, &press, &dirty);
    at.b = top - 25;
    terminal_deliver(-1, &at, &dirty);
    terminal_deliver(-1, &release, &dirty);

    before = terminal.view;
    at.a = x, at.b = top + (int)ROWS / 2 * cell + cell / 2;
    terminal_deliver(-1, &at, &dirty);
    terminal_deliver(-1, &press, &dirty);
    line_after = TERMINAL_LINE_OF(terminal.sel_active);
    for (int step = 0; step <= 120; step++)
        check_select_autoscroll_at(now + (positive)step * 16000000ull, &dirty);
    check("terminal: a press in the grid after a drag past its top does not scroll the view on its own",
          terminal.view == before && TERMINAL_LINE_OF(terminal.sel_active) == line_after);
    terminal_deliver(-1, &release, &dirty);
    terminal_view_live();
    terminal_select_clear(), terminal.sel.dragging = terminal.sel.handles = false;
}

/*
        The kit's edge scroll (ui_edge_steps) owes what the speed says and keeps the rest: 25 pixels past an edge at scale 100 is 270
        pixels a second, 4320 thousandths a 16 ms step, so 40 steps owe 172800 thousandths, which is ten rows of 17000 and 2800 over.
        The first call after the drag starts owes none, and a time before the last step restarts it.
*/
/*
        The kit's zoom key (ui_zoom_key): Ctrl with = or + grows, with - or _ shrinks, with 0 resets; without Control, or on a key up,
        or on another key, it is nothing. The terminal and every app without a keymap read the same chords.
*/
static void check_kit_zoom_key(void)
{
    struct window_event grow = {WINDOW_EVENT_KEY, WINDOW_KEY_DOWN | WINDOW_KEY_CONTROL, 13, '='};
    struct window_event plus = {WINDOW_EVENT_KEY, WINDOW_KEY_DOWN | WINDOW_KEY_CONTROL | WINDOW_KEY_SHIFT, 12, '+'};
    struct window_event shrink = {WINDOW_EVENT_KEY, WINDOW_KEY_DOWN | WINDOW_KEY_CONTROL, 12, '-'};
    struct window_event under = {WINDOW_EVENT_KEY, WINDOW_KEY_DOWN | WINDOW_KEY_CONTROL | WINDOW_KEY_SHIFT, 12, '_'};
    struct window_event reset = {WINDOW_EVENT_KEY, WINDOW_KEY_DOWN | WINDOW_KEY_CONTROL, 11, '0'};
    struct window_event bare = {WINDOW_EVENT_KEY, WINDOW_KEY_DOWN, 13, '='};
    struct window_event up = {WINDOW_EVENT_KEY, WINDOW_KEY_CONTROL, 13, '='};

    check("kit: Ctrl+= and Ctrl++ grow, Ctrl+- and Ctrl+_ shrink, Ctrl+0 resets, and no other key or a key up does anything",
          ui_zoom_key(&grow) == 1 && ui_zoom_key(&plus) == 1 && ui_zoom_key(&shrink) == -1 && ui_zoom_key(&under) == -1 &&
              ui_zoom_key(&reset) == 2 && ui_zoom_key(&bare) == 0 && ui_zoom_key(&up) == 0);
    check("kit: a modifier alone is a modifier key, and a letter is not", ui_key_modifier(29) && ui_key_modifier(42) && !ui_key_modifier(30));
}

/*
        The kit's clipboard keys (ui_clip_chord): Ctrl+Shift+C and Ctrl+Insert copy, Ctrl+Shift+V and Shift+Insert paste; Ctrl+C is the
        control code the terminal sends, so it is no chord. The clipboard's answer (ui_clip_read) reads a READY paste only when the app
        asked for one, and a REFUSED answer is never read.
*/
static void check_kit_clip_chord(void)
{
    struct window_event copy = {WINDOW_EVENT_KEY, WINDOW_KEY_DOWN | WINDOW_KEY_CONTROL | WINDOW_KEY_SHIFT, 46, 'C'};
    struct window_event paste = {WINDOW_EVENT_KEY, WINDOW_KEY_DOWN | WINDOW_KEY_CONTROL | WINDOW_KEY_SHIFT, 47, 'V'};
    struct window_event insert_copy = {WINDOW_EVENT_KEY, WINDOW_KEY_DOWN | WINDOW_KEY_CONTROL, 110, 0};
    struct window_event insert_paste = {WINDOW_EVENT_KEY, WINDOW_KEY_DOWN | WINDOW_KEY_SHIFT, 110, 0};
    struct window_event control_c = {WINDOW_EVENT_KEY, WINDOW_KEY_DOWN | WINDOW_KEY_CONTROL, 46, 'c'};
    struct window_event answer = {WINDOW_EVENT_CLIP, 0, 3, WINDOWS_CLIP_REFUSED};
    unsigned char *bytes = null;
    unsigned long length = 0;

    check("kit: Ctrl+Shift+C and Ctrl+Insert copy, Ctrl+Shift+V and Shift+Insert paste, and Ctrl+C is no chord",
          ui_clip_chord(&copy) == UI_CLIP_COPY && ui_clip_chord(&insert_copy) == UI_CLIP_COPY && ui_clip_chord(&paste) == UI_CLIP_PASTE &&
              ui_clip_chord(&insert_paste) == UI_CLIP_PASTE && ui_clip_chord(&control_c) == UI_CLIP_NONE);
    check("kit: a refused paste is never read: the clipboard's answer asks for nothing and holds no bytes",
          !ui_clip_read(null, &answer, true, &bytes, &length) && bytes == null && length == 0);
}

/*
        The kit's selection rule (ui_select_extend, the text widget's and the terminal's): a double click is a word and a triple a line, a
        drag by words from a word to a later one takes both words whole, one to an earlier word takes it from the press word's far end
        back to the place's word start, and a drag by characters takes the place it is at.
*/
static void check_kit_select_rule(void)
{
    p64 anchor = 0, active = 0;

    check("kit: a double click selects words and a triple click lines, a single click characters",
          ui_select_kind(1) == UI_SELECT_CHARS && ui_select_kind(2) == UI_SELECT_WORDS && ui_select_kind(3) == UI_SELECT_LINES);
    /* press on the word 10..14, drag to the word 20..25 (after it) and to 2..6 (before it). */
    ui_select_extend(UI_SELECT_WORDS, 10, 14, 20, 25, 22, &anchor, &active);
    check("kit: a word drag forward takes the press word whole and the place's word to its end", anchor == 10 && active == 25);
    ui_select_extend(UI_SELECT_WORDS, 10, 14, 2, 6, 3, &anchor, &active);
    check("kit: a word drag back takes the press word's far end and the place's word start", anchor == 14 && active == 2);
    ui_select_extend(UI_SELECT_CHARS, 10, 10, 12, 12, 12, &anchor, &active);
    check("kit: a character drag's active end is the place", anchor == 10 && active == 12);
}

static void check_edge_steps(void)
{
    struct ui_edge edge = {-25, 0, 0};
    positive now = 1000000000ull;
    long total = 0, first;

    first = (ui_kit.now = ui_kit.test_clock = (b64)(now), ui_edge_steps(&edge, 100, 1000l * 17));
    for (int step = 1; step <= 40; step++)
        total += (ui_kit.now = ui_kit.test_clock = (b64)(now + (positive)step * 16000000ull), ui_edge_steps(&edge, 100, 1000l * 17));
    check("kit: an edge scroll owes nothing at its first call, and 25 pixels past an edge at scale 100 is ten rows of 17 in 40 steps",
          first == 0 && total == 10 && edge.owed == 2800);
    check("kit: an edge scroll's clock that goes back restarts it: no step, and nothing owed",
          (ui_kit.now = ui_kit.test_clock = (b64)(now), ui_edge_steps(&edge, 100, 1000l * 17)) == 0 && edge.owed == 0 && edge.at == now);
}

static fn check_keys(void)
{
        static const struct
        {
                p32 code, character, flags;
                const char address_to sent;
                positive length;
        } keys[] = {
            {30, 'a', WINDOW_KEY_DOWN, "a", 1},
            {30, 'A', WINDOW_KEY_DOWN | WINDOW_KEY_SHIFT, "A", 1},
            {46, 'c', WINDOW_KEY_DOWN | WINDOW_KEY_CONTROL, "\x03", 1},
            {32, 'd', WINDOW_KEY_DOWN | WINDOW_KEY_CONTROL, "\x04", 1},
            {38, 'l', WINDOW_KEY_DOWN | WINDOW_KEY_CONTROL | WINDOW_KEY_REPEAT, "\x0c", 1},
            {28, '\r', WINDOW_KEY_DOWN, "\r", 1},
            {28, '\n', WINDOW_KEY_DOWN, "\r", 1},
            {14, '\b', WINDOW_KEY_DOWN, "\x7f", 1},
            {14, 127, WINDOW_KEY_DOWN, "\x7f", 1},
            {15, '\t', WINDOW_KEY_DOWN, "\t", 1},
            {15, '\t', WINDOW_KEY_DOWN | WINDOW_KEY_SHIFT, "\x1b[Z", 3},
            {1, 27, WINDOW_KEY_DOWN, "\x1b", 1},
            {103, 0, WINDOW_KEY_DOWN, "\x1b[A", 3},
            {108, 0, WINDOW_KEY_DOWN, "\x1b[B", 3},
            {103, 0, WINDOW_KEY_DOWN | WINDOW_KEY_SHIFT, "\x1b[1;2A", 6},
            {106, 0, WINDOW_KEY_DOWN | WINDOW_KEY_CONTROL, "\x1b[1;5C", 6},
            {45, 'x', WINDOW_KEY_DOWN | WINDOW_KEY_ALT, "\x1bx", 2},
            {111, 0, WINDOW_KEY_DOWN, "\x1b[3~", 4},
            {104, 0, WINDOW_KEY_DOWN, "\x1b[5~", 4},
            {59, 0, WINDOW_KEY_DOWN, "\x1bOP", 3},
            {57, ' ', WINDOW_KEY_DOWN | WINDOW_KEY_CONTROL, "\0", 1},
            {26, '[', WINDOW_KEY_DOWN | WINDOW_KEY_CONTROL, "\x1b", 1},
            {30, 'a', 0, "", 0},
            {29, 0, WINDOW_KEY_DOWN | WINDOW_KEY_CONTROL, "", 0},
        };

        for (positive at = 0; at < array_count(keys); at++)
        {
                struct window_event event = {WINDOW_EVENT_KEY, keys[at].flags, (int)keys[at].code,
                                             (int)keys[at].character};
                b32 dirty = false;

                to_shell_length = 0;
                terminal_deliver(-1, &event, &dirty);
                check("keys: the shell is sent the bytes the kernel's window sent",
                      to_shell_length == keys[at].length &&
                          !memory_compare(to_shell, keys[at].sent, keys[at].length));
        }
        to_shell_length = 0;
}

/*
        The pointer, when a program has asked for it: where it is in cells
        from where it is in pixels, the button it pressed and let go of, and
        the wheel as the buttons after the last. Pixels are the window's, so
        the cell is whole cells of this font's size from the grid's top left,
        past the paper round it.
*/
static fn check_pointer(void)
{
        unsigned int column = 3, row = 4;
        int x = (int)(terminal.pad + (column - 1) * terminal.cell_w + terminal.cell_w / 2);
        int y = (int)(terminal.pad_y + (row - 1) * terminal.cell_h + terminal.cell_h / 2);
        struct window_event motion = {WINDOW_EVENT_MOTION, 0, x, y};
        struct window_event press = {WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN, 0x110, 0};
        struct window_event release = {WINDOW_EVENT_BUTTON, 0, 0x110, 0};
        struct window_event right = {WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN, 0x111, 0};
        struct window_event wheel = {WINDOW_EVENT_WHEEL, 0, 120, 0};
        b32 dirty = false;
        char expect[16];

        mouse_mode = 1000;
        mouse_sgr = false;
        terminal.modifiers = 0;
        to_shell_length = 0;
        terminal_deliver(-1, &motion, &dirty);
        check("pointer: a motion is nothing to a program that asked for clicks", !to_shell_length);

        terminal_deliver(-1, &press, &dirty);
        expect[0] = 27, expect[1] = '[', expect[2] = 'M', expect[3] = 32, expect[4] = (char)(32 + column),
        expect[5] = (char)(32 + row);
        check("pointer: a press is the button and the cell", to_shell_length == 6 && !memory_compare(to_shell, expect, 6));

        to_shell_length = 0;
        terminal_deliver(-1, &release, &dirty);
        expect[3] = 32 + 3;
        check("pointer: a release is button 3", to_shell_length == 6 && !memory_compare(to_shell, expect, 6));

        to_shell_length = 0;
        terminal_deliver(-1, &wheel, &dirty);
        expect[3] = 32 + 64;
        check("pointer: the wheel is button 64", to_shell_length == 6 && !memory_compare(to_shell, expect, 6));

        mouse_sgr = true;
        to_shell_length = 0;
        terminal_deliver(-1, &press, &dirty);
        check("pointer: SGR press", to_shell_length == 9 && !memory_compare(to_shell, "\x1b[<0;3;4M", 9));
        to_shell_length = 0;
        terminal_deliver(-1, &right, &dirty);
        check("pointer: the right button is 2", to_shell_length == 9 && !memory_compare(to_shell, "\x1b[<2;3;4M", 9));
        to_shell_length = 0;
        terminal_deliver(-1, &release, &dirty);
        check("pointer: SGR release", to_shell_length == 9 && !memory_compare(to_shell, "\x1b[<0;3;4m", 9));

        mouse_mode = 0;
        mouse_sgr = false;
        to_shell_length = 0;

#ifdef UI_TARGET
        /*
                A finger on a terminal no program asked the pointer of: it starts no selection and sends nothing as it
                lands, past the slop it drags the scrollback with it a line for each cell's height (down is back, what
                is under the finger following it), let go moving it throws the view, and a tap is a click there.
        */
        {
                struct window_event finger = {WINDOW_EVENT_MOTION, WINDOW_KEY_TOUCH, x, y};
                struct window_event land = {WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN | WINDOW_KEY_TOUCH, 0x110, 0};
                struct window_event lift = {WINDOW_EVENT_BUTTON, WINDOW_KEY_TOUCH, 0x110, 0};
                unsigned int first, shown, total, before;
                b32 quiet;

                for (int line = 0; line < 3 * (int)ROWS; line++)
                        term_bytes("scrollback\r\n", 12);
                terminal_view_live();
                terminal_extent(&before, &shown, &total);
                terminal_deliver(-1, &finger, &dirty);
                terminal_deliver(-1, &land, &dirty);
                quiet = !to_shell_length;
                // The drag follows from where the finger is when the slop is passed: the first cell it moves is the slop's.
                for (int step = 1; step <= 5; step++)
                {
                        finger.b = y + step * (int)terminal.cell_h;
                        terminal_deliver(-1, &finger, &dirty);
                }
                terminal_extent(&first, &shown, &total);
                check("pointer: a finger dragged five cells down the paper (the first is the slop's) takes the scrollback four lines back, and sends nothing",
                      quiet && !to_shell_length && first + 4 == before);
                terminal_deliver(-1, &lift, &dirty);
                check("pointer: let go moving, the view is thrown", ui_body_going(&terminal.bar.body));
                ui_body_hold(&terminal.bar.body, ui_kit_time());
                terminal_view_live();
                finger.b = y;
                terminal_deliver(-1, &finger, &dirty);
                terminal_deliver(-1, &land, &dirty);
                terminal_deliver(-1, &lift, &dirty);
                to_shell_length = 0;
                check("pointer: a finger's tap is a click where it landed, the press and the release",
                      to_shell_length == 0 && !terminal.finger.down && !terminal.finger.dragged);

                /*
                The alternate screen: the same drag sends the arrows, a line at a time as its pixels make lines, and its lift throws them
                (ui_body_free): the free body carries them on a frame at a time until it is still, and it has no end to stop at.
                */
                {
                        positive clock = 6000000000ull;
                        long sent_drag = 0;

                        term_bytes((const p8 address_to) "\x1b[?1049h", 8);
                        to_shell_length = 0;
                        finger.b = y;
                        ui_kit.now = ui_kit.test_clock = (b64)clock;
                        terminal_deliver(-1, &finger, &dirty);
                        terminal_deliver(-1, &land, &dirty);
                        for (int step = 1; step <= 4; step++)
                        {
                                ui_kit.now = ui_kit.test_clock = (b64)(clock += 16000000ull);
                                finger.b = y + step * (int)terminal.cell_h;
                                terminal_deliver(-1, &finger, &dirty);
                        }
                        sent_drag = to_shell_length;
                        ui_kit.now = ui_kit.test_clock = (b64)(clock += 16000000ull);
                        terminal_deliver(-1, &lift, &dirty);
                        for (int k = 0; k < 400 && ui_body_going(&terminal.alt); k++)
                        {
                                b32 moved = true;

                                // The step terminal_tick takes on the frame: the body's whole pixels, sent as the drag's were, to no pty (-1), so
                                // they stay queued to be counted. The terminal's own master is 0 in a check, which is standard input: one that
                                // takes a write (a terminal, a socket) takes the arrows, and the queue is then empty whatever was thrown.
                                ui_kit.now = ui_kit.test_clock = (b64)(clock += 16000000ull);
                                terminal_finger_scroll(-1, (int)ui_body_given(&terminal.alt, ui_kit_time()), &dirty, &moved);
                        }
                        string_format(log, "  pointer alternate screen: the drag sent %b bytes, the throw sent %b more, going %b\n",
                                      (bipolar)sent_drag, (bipolar)(to_shell_length - sent_drag), (bipolar)ui_body_going(&terminal.alt));
                        check("pointer: on the alternate screen a finger's drag sends arrows, and its lift throws them on until they stop",
                              sent_drag > 0 && to_shell_length > sent_drag && !ui_body_going(&terminal.alt));
                        term_bytes((const p8 address_to) "\x1b[?1049l", 8);
                        ui_kit.test_clock = 0;
                        to_shell_length = 0;
                }
        }

        /*
                The scroll bar by a finger: drawn four pixels wide, it is taken within UI_TARGET of it (the bar's reach), so a
                finger on its thumb, or a little beside it, grabs the thumb and drags the view with it; a finger on the track
                beside the thumb pages toward it. A mouse is exact: the same place there is the grid's, not the bar's.
        */
        {
                struct window_event at = {WINDOW_EVENT_MOTION, WINDOW_KEY_TOUCH, 0, 0};
                struct window_event down = {WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN | WINDOW_KEY_TOUCH, 0x110, 0};
                struct window_event up = {WINDOW_EVENT_BUTTON, WINDOW_KEY_TOUCH, 0x110, 0};
                struct window_event mouse_at = {WINDOW_EVENT_MOTION, 0, 0, 0};
                struct window_event mouse_down = {WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN, 0x110, 0};
                struct window_event mouse_up = {WINDOW_EVENT_BUTTON, 0, 0x110, 0};
                struct ui kit;
                unsigned int first, shown, total, start;
                int scale = (int)terminal_scale() / 100, top, length;

                terminal_view_live();
                terminal_extent(&first, &shown, &total);
                terminal_bar_place(first, shown, total);
                kit = terminal_kit(null);
                ui_scroll_thumb(&kit, &terminal.bar, &top, &length);
                at.a = terminal.bar.x - 8 * scale, at.b = top + length / 2;
                terminal_deliver(-1, &at, &dirty);
                terminal_deliver(-1, &down, &dirty);
                check("pointer: a finger 8 pixels left of the bar takes its thumb, which is drawn four pixels wide",
                      terminal.bar.held != 0);
                start = first;
                at.b -= 4 * (int)terminal.cell_h;
                terminal_deliver(-1, &at, &dirty);
                terminal_extent(&first, &shown, &total);
                check("pointer: a finger dragged up four cells on the thumb takes the view back with it", first < start);
                terminal_deliver(-1, &up, &dirty);
                check("pointer: let go, the thumb is let go too", !terminal.bar.held);

                terminal_view_live();
                terminal_extent(&first, &shown, &total);
                terminal_bar_place(first, shown, total);
                ui_scroll_thumb(&kit, &terminal.bar, &top, &length);
                start = first;
                /* The thumb's reach is UI_TARGET tall, so a thumb shorter than that reaches a little past it: the track's top is beyond. */
                at.a = terminal.bar.x + 5 * scale, at.b = terminal.bar.y + (UI_CLOSE_BOTTOM + 2) * scale;
                terminal_deliver(-1, &at, &dirty);
                terminal_deliver(-1, &down, &dirty);
                terminal_deliver(-1, &up, &dirty);
                terminal_extent(&first, &shown, &total);
                check("pointer: a finger on the track beside the thumb is on the content, not the bar's: the view does not page and nothing is grabbed",
                      first == start && !terminal.bar.held);

                terminal_view_live();
                terminal_extent(&first, &shown, &total);
                terminal_bar_place(first, shown, total);
                ui_scroll_thumb(&kit, &terminal.bar, &top, &length);
                mouse_at.a = terminal.bar.x - 8 * scale, mouse_at.b = top + length / 2;
                terminal_deliver(-1, &mouse_at, &dirty);
                terminal_deliver(-1, &mouse_down, &dirty);
                check("pointer: a mouse 8 pixels left of the bar is the grid's, and takes no thumb", !terminal.bar.held);
                // That press began a selection: it wears the text pointer from its press, through its drag, to its release.
                {
                        b32 pressed = ui_kit_shape == WINDOW_POINTER_TEXT, dragged;

                        ui_kit_shape = WINDOW_POINTER_ARROW; // what a motion makes it before the terminal has the event (ui_kit_track)
                        mouse_at.a -= 40 * scale;
                        terminal_deliver(-1, &mouse_at, &dirty);
                        dragged = ui_kit_shape == WINDOW_POINTER_TEXT;
                        terminal_deliver(-1, &mouse_up, &dirty);
                        check("pointer: a selection dragged on the grid wears the text pointer from its press, and the arrow again when it is let go",
                              pressed && dragged && ui_kit_shape == WINDOW_POINTER_ARROW);
                }
                // A mouse's press on the track off the thumb: the thumb goes there and is held (the view jumps to the top, the press being
                // at the track's top). Pulled above the track the grid stretches, the view still the top; let go, it springs home.
                {
                        b64 kept_now = ui_kit.now, kept_clock = ui_kit.test_clock;
                        b32 jumped, pulled, home;
                        int frames = 0;

                        ui_kit.now = ui_kit.test_clock = 7000000000000ll;
                        terminal_view_live();
                        terminal_extent(&first, &shown, &total);
                        terminal_bar_place(first, shown, total);
                        mouse_at.a = terminal.bar.x + 5 * scale, mouse_at.b = terminal.bar.y + (UI_CLOSE_BOTTOM + 2) * scale;
                        terminal_deliver(-1, &mouse_at, &dirty);
                        terminal_deliver(-1, &mouse_down, &dirty);
                        terminal_extent(&first, &shown, &total);
                        jumped = terminal.bar.held && first == 0 && terminal_grid_pull() == 0;
                        mouse_at.b = terminal.bar.y - 60 * scale;
                        ui_kit.now += 16666667ll, ui_kit.test_clock = ui_kit.now;
                        terminal_deliver(-1, &mouse_at, &dirty);
                        terminal_extent(&first, &shown, &total);
                        pulled = terminal.bar.held && first == 0 && terminal_grid_pull() < 0 && ui_body_going(&terminal.bar.body);
                        terminal_deliver(-1, &mouse_up, &dirty);
                        while (ui_body_going(&terminal.bar.body) && frames++ < 600)
                        {
                                ui_kit.now += 16666667ll, ui_kit.test_clock = ui_kit.now;
                                terminal_bar_step();
                        }
                        terminal_extent(&first, &shown, &total);
                        home = !terminal.bar.held && frames > 0 && frames < 600 && first == 0 && terminal_grid_pull() == 0;
                        check("pointer: a mouse's press on the track off the thumb puts the thumb there and holds it: the view jumps to the place pressed", jumped);
                        check("pointer: the thumb pulled above its track stretches the grid, as a finger's pull past the oldest line does", pulled);
                        check("pointer: let go there, the stretch springs home and the grid rests on its oldest line", home);
                        ui_kit.now = kept_now, ui_kit.test_clock = kept_clock;
                        terminal_view_live();
                        terminal_bar_follow();
                }
                terminal_select_clear();
                terminal_view_live();
        }

        /*
                The cells run to the window's right edge with no column kept for the bar: the grid ends within a cell of the edge
                whether or not there is a scrollback, and the bar, where there is one, is over that edge, flush with it, from
                below the close button to the window's bottom, and its thumb is in its track at both ends of the scrollback.
        */
        {
#ifdef UI_SCROLL_INSET
                struct ui kit;
                unsigned int first, shown, total, want = terminal.pad + 40 * terminal.cell_w + terminal.cell_w / 2 + 3;
                int scale = (int)terminal_scale() / 100, top, length;
                b32 fills, flush, below, ends;

                terminal_configure(-1, want, terminal.height);
                fills = COLUMNS == 40 && terminal.pad + COLUMNS * terminal.cell_w <= terminal.width &&
                        terminal.pad + (COLUMNS + 1) * terminal.cell_w > terminal.width;
                terminal_view_live();
                for (int line = 0; line < 3 * (int)ROWS; line++)
                        term_bytes("scrollback\r\n", 12);
                terminal_view_live();
                terminal_extent(&first, &shown, &total);
                terminal_bar_place(first, shown, total);
                kit = terminal_kit(null);
                flush = total > shown && terminal.bar.x + UI_SCROLL_WIDTH * scale == (int)terminal.width &&
                        terminal.bar.inset == UI_CLOSE_BOTTOM && terminal.bar.height == (int)terminal.height;
                below = ui_scroll_thumb(&kit, &terminal.bar, &top, &length) && top + length == (int)terminal.height &&
                        top >= UI_CLOSE_BOTTOM * scale;
                terminal_view_set(0);
                terminal_extent(&first, &shown, &total);
                terminal_bar_place(first, shown, total);
                ends = ui_scroll_thumb(&kit, &terminal.bar, &top, &length) && top == UI_CLOSE_BOTTOM * scale;
                terminal_view_live();
                check("terminal: the cells run to the window's right edge with no column kept for the bar, and the bar takes none",
                      fills);
                check("terminal: the bar is flush with the window's right edge, from below the close button to its bottom", flush);
                check("terminal: the thumb is in the track, reaching the bottom at the live end and the close button's inset at the top", below && ends);
#else
                check("terminal: the cells run to the window's right edge with no column kept for the bar, and the bar takes none", false);
                check("terminal: the bar is flush with the window's right edge, from below the close button to its bottom", false);
                check("terminal: the thumb is in the track, reaching the bottom at the live end and the close button's inset at the top", false);
#endif
        }

        /*
                A long press selects the word under the finger: the right button held (WINDOW_KEY_HOLD) after a finger's
                press that its cancel took back; the drag after it extends the selection by cells, from the word's first cell;
                the lift copies it. A short touch selects nothing.
        */
        {
                struct window_event at = {WINDOW_EVENT_MOTION, WINDOW_KEY_TOUCH, 0, 0};
                struct window_event down = {WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN | WINDOW_KEY_TOUCH, 0x110, 0};
                struct window_event held = {WINDOW_EVENT_BUTTON, WINDOW_KEY_TOUCH | WINDOW_KEY_CANCEL | WINDOW_KEY_HOLD, 0x110, 0};
                struct window_event right_down = {WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN | WINDOW_KEY_TOUCH | WINDOW_KEY_HOLD, 0x111, 0};
                struct window_event right_up = {WINDOW_EVENT_BUTTON, WINDOW_KEY_TOUCH | WINDOW_KEY_HOLD, 0x111, 0};
                struct window_event lift = {WINDOW_EVENT_BUTTON, WINDOW_KEY_TOUCH | WINDOW_KEY_CANCEL | WINDOW_KEY_HOLD, 0x110, 0};
                struct window_event up = {WINDOW_EVENT_BUTTON, WINDOW_KEY_TOUCH, 0x110, 0};
                p8 address_to text;
                positive room, length;

                terminal_select_clear(), terminal.sel.dragging = terminal.sel.handles = false;
                terminal_view_live();
                term_bytes("\x1b[H\x1b[2Jalpha bravo charlie\r\n", 28);
                terminal_view_live();
                at.a = (int)(terminal.pad + 8 * terminal.cell_w + terminal.cell_w / 2), at.b = (int)(terminal.pad_y + terminal.cell_h / 2);
                terminal_deliver(-1, &at, &dirty);
                terminal_deliver(-1, &down, &dirty);
                terminal_deliver(-1, &held, &dirty);
                terminal_deliver(-1, &right_down, &dirty);
                terminal_deliver(-1, &right_up, &dirty);
                length = terminal_select_text(&text, &room);
                check("touch: a long press selects the word under the finger, and no menu opens",
                      terminal.sel.handles && terminal_selecting() && length == 5 && !memory_compare(text, "bravo", 5) &&
                          !terminal_menu.open && to_shell_length == 0);
                array_store_release(text, room, length);

                at.a = (int)(terminal.pad + 14 * terminal.cell_w + 1);
                terminal_deliver(-1, &at, &dirty);
                length = terminal_select_text(&text, &room);
                check("touch: a drag after the long press extends the selection by words, from the word's first cell",
                      length == 13 && !memory_compare(text, "bravo charlie", 13));
                array_store_release(text, room, length);

                terminal_deliver(-1, &lift, &dirty);
                check("touch: the lift ends the press and copies the selection, which stays marked, with its handles and the bar (the text widget's)",
                      terminal.sel.handles && terminal.sel.bar && !terminal.sel.dragging && terminal_selecting() && to_shell_length == 0);
                /*
                        The bar's labels are the kit's text, drawn in the faces the terminal's kit holds: terminal.ui's regular and bold, which
                        terminal_fonts sets when the faces load (as a program's own grid is drawn with terminal.plain). A frame with the bar
                        shown is drawn: before the faces were set, the label's pen read a null face and the program faulted.
                */
                check("touch: the terminal's kit has the faces its own text is drawn with (the bar's labels)",
                      terminal.ui.regular && terminal.ui.bold);
                {
                        static unsigned int bar_scratch[1 << 21];
                        struct ui kit = terminal_kit(bar_scratch);

                        // The overlay a frame draws (terminal_overlay), into a scratch picture so the frames the rows after it count stay as they were.
                        // The bar comes back once the view has been still (the clock is stepped to it), fading in: drawn at the start of the
                        // fade, then when it is in, with its labels at full alpha.
                        if ((positive)terminal.pitch * terminal.height <= sizeof bar_scratch / sizeof bar_scratch[0])
                        {
                                b64 clock = ui_kit.test_clock;

                                ui_kit.test_clock = (b64)terminal.sel.quiet_at + 1;
                                ui_select_overlay(&kit, terminal_sel());
                                ui_kit.test_clock = (b64)terminal.sel.quiet_at + UI_SELECT_BAR_FADE_NS + 1;
                                ui_select_overlay(&kit, terminal_sel());
                                ui_kit.test_clock = clock;
                        }
                        check("touch: the actions bar is drawn over the selection, its labels in the terminal's faces",
                              terminal_sel()->bar_up && terminal_selecting());
                        {
                                // The labels at each scale: the terminal's kit at that scale (its scratch fitted, as a frame does it), the bar
                                // drawn at full alpha, and its box's pixels counted against the bar's ground.
                                static const int scales[4] = {100, 150, 200, 300};
                                int was_scale = terminal.ui.scale;

                                for (int k = 0; k < 4; k++)
                                {
                                        struct ui_select address_to bs = terminal_sel();
                                        int inked = 0;
                                        unsigned int ground;
                                        b64 clock = ui_kit.test_clock;

                                        // The frame's own overlay (terminal_overlay, the kit made from the grid's origin), so the path that fits the
                                        // scratch is the one measured; the bar's box is in the grid's coordinates.
                                        unsigned int base = terminal.pad_y * terminal.pitch + terminal.pad;

                                        terminal.ui.scale = scales[k];
                                        bs->bar_up = false;
                                        ui_kit.test_clock = (b64)bs->quiet_at + 1;
                                        terminal_overlay(bar_scratch, 0);
                                        ui_kit.test_clock = (b64)bs->quiet_at + UI_SELECT_BAR_FADE_NS + 1;
                                        terminal_overlay(bar_scratch, 0);
                                        ui_kit.test_clock = clock;
                                        ground = bar_scratch[base + (positive)(bs->bar_y + 2) * terminal.pitch + (positive)(bs->bar_x + 2)];
                                        for (int yy = bs->bar_y + 1; yy < bs->bar_y + bs->bar_h - 1; yy++)
                                                for (int xx = bs->bar_x + 1; xx < bs->bar_x + bs->bar_w - 1; xx++)
                                                        inked += bar_scratch[base + (positive)yy * terminal.pitch + (positive)xx] != ground;
                                        check("touch: the actions bar's labels are drawn at every scale (pixels in the bar differ from its ground)", bs->bar_up && inked >= 100 * scales[k] / 100);
                                }
                                terminal.ui.scale = was_scale;
                        }
                }

                terminal_select_clear(), terminal.sel.handles = false;
                terminal.sel.click.at = 0, terminal.sel.click.count = 0; // a tap a moment after the last is not a double tap
                at.a = (int)(terminal.pad + 8 * terminal.cell_w + terminal.cell_w / 2);
                terminal_deliver(-1, &at, &dirty);
                terminal_deliver(-1, &down, &dirty);
                terminal_deliver(-1, &up, &dirty);
                check("touch: a short touch with no movement selects nothing", !terminal_selecting() && !terminal.sel.handles &&
                      to_shell_length == 0);
                to_shell_length = 0;
        }
        /*
                The handles' overlay drawn into a picture the window's size (pitch by height): a selection on the last line, its handles
                hung below it, draws nothing past the picture's end. The overlay's kit has the grid's origin (pad to the left and pad_y
                down) and was given the window's whole height and width, so a knob hung below the last line wrote up to pad_y rows past
                the picture: the buffer after it is the window's neighbour's memory, and the program faults later where it lands.
        */
        {
                static unsigned int overlay_frame[1 << 21];
                unsigned int pad_y_was = terminal.pad_y, height_was = terminal.height;
                positive picture, guard = 16 * (positive)terminal.pitch, spoiled = 0;

                // The window of a small text (cells 19 px, a margin of 6): the handle's knob reaches 2 * 6 px under the line, past the
                // paper of 6 px below the grid, so the picture is the grid and that paper only.
                terminal.pad_y = 6, terminal.height = ROWS * terminal.cell_h + 2 * terminal.pad_y;
                picture = (positive)terminal.pitch * terminal.height;
                if (picture + guard <= sizeof overlay_frame / sizeof overlay_frame[0])
                {
                        // The screen full, the cursor on its last row: the selection's line is the grid's bottom one.
                        terminal_view_live();
                        for (unsigned int line = 0; line < ROWS; line++)
                                term_bytes("line\r\n", 6);
                        terminal_view_live();
                        for (positive at = picture; at < picture + guard; at++)
                                overlay_frame[at] = 0xdeadbeefu;
                        terminal_select_set(terminal_place(window->head - 1, 0), terminal_place(window->head - 1, 4));
                        terminal.sel.handles = true, terminal.sel.bar = false;
                        terminal_overlay(overlay_frame, 0);
                        for (positive at = picture; at < picture + guard; at++)
                                spoiled += overlay_frame[at] != 0xdeadbeefu;
                }
                terminal.pad_y = pad_y_was, terminal.height = height_was;
                check("terminal: a handle on the last line draws nothing past the end of the picture", spoiled == 0);
        /*
                A kernel log's records (klog.c) are put on a new line after the one before, so the last record leaves the cursor on its
                own line and not on an empty one below it: selecting the records copies them as the lines they are, with no empty line
                after the last. The bytes are the reader's own pattern (klog_drain writes the separator, then the record).
        */
        {
                static const char expect[] = "alpha\nbravo\ncharlie";
                p8 address_to text = null;
                positive room = 0, length;

                terminal_view_live();
                for (unsigned int line = 0; line < ROWS; line++)
                        term_bytes("\r\n", 2);
                term_bytes("alpha", 5);
                term_bytes("\r\nbravo", 7);
                term_bytes("\r\ncharlie", 9);
                terminal_view_live();
                terminal_select_set(terminal_place(window->head - 3, 0), terminal_place(window->head - 1, window->stride));
                length = terminal_select_text(&text, &room);
                check("reading: three records select as three lines, with no empty line after the last",
                      length == sizeof(expect) - 1 && text && !memory_compare(text, expect, length));
                array_store_release(text, room, length);
                terminal_select_clear();
        }
                terminal_select_clear(), terminal.sel.dragging = terminal.sel.handles = false;
        }
#endif
}

