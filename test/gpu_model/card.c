/*
        The model card: the model under gpu.c's check seams, so that gpu.c's own path (gpu_scene, gpu_blit, the self-test gpu_test_hash)
        runs on it as it runs on a card. A submit (GPU_SUBMIT_FAKE, where gpu_amd_submit's job, ib, bos and named are in scope) is taken without
        a call to the kernel, given a sequence, and read as the command processor reads it: the packets of the IB at the job's slot, SET_SH_REG into
        the registers, DISPATCH_DIRECT the grid; its regions are the card's own buffer (read, write, run) and the buffers its bo list names, at
        the addresses gpu.c gave them (a map read or read and write as it was bound; an import at its address, with the memory behind it a
        row gives in gpu_model_card.import_host). The job is done when the submit returns, so the fence (GPU_WAIT_CS) answers done. A failure
        of the model is kept (the first one, until gpu_model_card_reset) and the job is taken as it would be on the card, where a fault is
        the kernel log's and not the submit's.

        gpu_start cannot be run to its end with no card (its second open of the node picked has no fake), so gpu_model_card_start does what
        it does for the card's buffer: the buffer itself (memory), the kernels copied into it at gpu_kernel_at.
*/

static void gpu_model_card_submit(positive ib_va, unsigned int ib_bytes, const p64 address_to bos, unsigned int named);
static positive gpu_model_sequence; // the last job's sequence (a job is done when its submit returns)
#define GPU_SUBMIT_FAKE(error, taken, sequence) (gpu_model_card_submit(job->packets, (unsigned int)sizeof(ib), bos, named), (sequence) = ++gpu_model_sequence, (taken) = true)
#define GPU_WAIT_CS(wait) ((wait)[0] = 0, true)
static p64 gpu_model_seen[1 + 64 + 256];
static unsigned int gpu_model_seen_count;
#define GPU_SUBMIT_SEEN(list, count) (gpu_model_seen_count = (count), memory_copy(gpu_model_seen, (list), (positive)(count) * sizeof(p64)))

// The kernel's answers for the maps (gpu_map, gpu_drop) on the model card: a userptr buffer is a handle of its own (AMDGPU_GEM_USERPTR writes it in the
// high half of the flags' word), its address given and taken (GEM_VA) and the handle let go of (GEM_CLOSE) are done. The model reaches a map's pages by
// gpu_maps (the regions of a job), so nothing else is kept. Every other request of the card's descriptor (none: gpu.fd is -1) is refused, EBADF.
static unsigned int gpu_model_handles = 100;
static long gpu_model_control(int fd, unsigned int request, void address_to a)
{
        p64 address_to longs = (p64 address_to)a;

        if (request == 0xc0186451u) // DRM_IOCTL_AMDGPU_GEM_USERPTR
                return longs[2] = (longs[2] & 0xffffffffull) | (p64)++gpu_model_handles << 32, 0;
        if (request == 0x40406448u || request == 0x40086409u) // DRM_IOCTL_AMDGPU_GEM_VA, DRM_IOCTL_GEM_CLOSE
                return 0;
        return fd < 0 ? -9 : system_control(fd, request, a);
}
#define GPU_CONTROL(fd, request, a) gpu_model_control((int)(fd), (unsigned int)(request), (void address_to)(a))

#include "../../src/canvas/gpu.c"

#define GPU_MODEL_CARD_WORDS 4096 // the words of each kernel the card counts the runs of

static struct
{
        struct gpu_model_region region[1 + GPU_IMPORTS + GPU_MAPS];
        p8 address_to import_host[GPU_IMPORTS]; // the memory behind each import, as the row that made it says (none: the job may not reach it)
        p32 sh[0x400];                          // the SH registers the packets set, by their offsets (0x20c COMPUTE_PGM_LO ...)
        unsigned int jobs, faults;
        p32 hits[GPU_KERNELS][GPU_MODEL_CARD_WORDS]; // each kernel's code words' runs, every job since the reset
        p8 address_to buffer;
        positive bytes;
        // a staged kernel (gpu_model_stage): its code at staged_at in the buffer, its resource words, which a dispatch of that code is given
        positive staged_at;
        p32 staged_rsrc[2];
        unsigned int staged_words;
} gpu_model_card;
#define GPU_MODEL_STAGE (128u << 10) // the room for a staged kernel's code, below the self-test's megabyte

static void gpu_model_card_reset(void)
{
        memory_zero(&gpu_model.fault, sizeof(gpu_model.fault)), memory_zero(&gpu_model.cost, sizeof(gpu_model.cost));
        memory_zero(gpu_model_card.hits, sizeof(gpu_model_card.hits));
        gpu_model_card.jobs = gpu_model_card.faults = 0;
}

static void gpu_model_card_refuse(const char *what, p64 value)
{
        if (gpu_model_fail(GPU_MODEL_DISPATCH, what))
                return;
        gpu_model_number(value, 16);
}

// The regions of a job: the card's buffer, then each buffer the bo list names (a map that still has its pages, an import with memory).
static unsigned int gpu_model_card_regions(const p64 address_to bos, unsigned int named)
{
        struct gpu_model_region *r = gpu_model_card.region;
        unsigned int count = 0;

        r[count++] = (struct gpu_model_region){GPU_VA, gpu.size, (p8 address_to)gpu.at, GPU_MODEL_READ | GPU_MODEL_WRITE | GPU_MODEL_RUN};
        for (unsigned int k = 0; k < named; k++)
        {
                for (unsigned int i = 0; i < gpu_map_count; i++)
                        if (gpu_maps[i].handle == bos[k] && gpu_maps[i].from && count < sizeof(gpu_model_card.region) / sizeof(gpu_model_card.region[0]))
                                r[count++] = (struct gpu_model_region){gpu_maps[i].va, gpu_maps[i].bytes, (p8 address_to)gpu_maps[i].from,
                                                                       GPU_MODEL_READ | (gpu_maps[i].written ? GPU_MODEL_WRITE : 0)};
                for (unsigned int i = 0; i < gpu.imports; i++)
                        if (gpu.imported[i] == bos[k] && gpu_model_card.import_host[i] && count < sizeof(gpu_model_card.region) / sizeof(gpu_model_card.region[0]))
                                r[count++] = (struct gpu_model_region){gpu.imported_va[i], gpu.imported_bytes[i], gpu_model_card.import_host[i], GPU_MODEL_READ | GPU_MODEL_WRITE};
        }
        return count;
}

// A DISPATCH_DIRECT with the registers as the packets left them: the grid, the threads of a group, the code and its resource words, the user data.
static void gpu_model_card_dispatch(const p32 address_to grid)
{
        const p32 *sh = gpu_model_card.sh;
        positive code = ((p64)sh[0x20d] << 32 | sh[0x20c]) << 8, room = 0;
        const p8 *host = null;

        if (grid[2] != 1 || grid[3] != 0x45 || sh[0x207] != 64 || sh[0x208] != 1 || sh[0x209] != 1)
        {
                gpu_model_card_refuse("a dispatch the model does not take: its grid's z, its initiator or its group is not gpu_amd_submit's ", (p64)grid[3] << 32 | grid[2]);
                return;
        }
        for (unsigned int k = 0; k < gpu_model.regions && !host; k++)
                if (code >= gpu_model.region[k].va && code - gpu_model.region[k].va < gpu_model.region[k].bytes && (gpu_model.region[k].can & GPU_MODEL_RUN))
                        host = gpu_model.region[k].host + (code - gpu_model.region[k].va), room = gpu_model.region[k].bytes - (code - gpu_model.region[k].va);
        if (!host || (code & 255))
        {
                gpu_model_card_refuse("a dispatch of code where the card may not run: ", code);
                return;
        }
        if (gpu_model_card.staged_words && code == GPU_VA + gpu_model_card.staged_at)
                room = 4ull * gpu_model_card.staged_words; // a staged kernel is run with its own resource words, as a gpu.c built with it would send them
        gpu_model_dispatch((const p32 address_to)host, (unsigned int)(room / 4 < GPU_MODEL_CODE ? room / 4 : GPU_MODEL_CODE), code,
                           gpu_model_card.staged_words && code == GPU_VA + gpu_model_card.staged_at ? gpu_model_card.staged_rsrc[0] : sh[0x212],
                           gpu_model_card.staged_words && code == GPU_VA + gpu_model_card.staged_at ? gpu_model_card.staged_rsrc[1] : sh[0x213], sh + 0x240, grid[0], grid[1]);
        for (unsigned int k = 0; k < GPU_KERNELS; k++)
                if (code == GPU_VA + gpu_kernel_at[k])
                        for (unsigned int at = 0; at < GPU_MODEL_CARD_WORDS; at++)
                                gpu_model_card.hits[k][at] += gpu_model.hits[at];
}

// The job: the IB's packets (type 3: ACQUIRE_MEM, SET_SH_REG and DISPATCH_DIRECT, the ones gpu_amd_submit sends) read from the card's memory.
static void gpu_model_card_submit(positive ib_va, unsigned int ib_bytes, const p64 address_to bos, unsigned int named)
{
        const p32 *ib;
        unsigned int words = ib_bytes / 4;

        gpu_model_card.jobs++;
        gpu_model.region = gpu_model_card.region, gpu_model.regions = gpu_model_card_regions(bos, named), gpu_model.last = 0, gpu_model.code_va = 0;
        if (!(ib = (const p32 address_to)gpu_model_reach(ib_va, ib_bytes, GPU_MODEL_READ)))
                gpu_model_card_refuse("an IB outside the card's buffer: ", ib_va), words = 0;
        memory_zero(gpu_model_card.sh, sizeof(gpu_model_card.sh));
        for (unsigned int at = 0; at < words && !gpu_model.fault.code;)
        {
                p32 head = ib[at], op = head >> 8 & 255, count = (head >> 16 & 0x3fff) + 1;

                if (head >> 30 != 3 || at + 1 + count > words)
                {
                        gpu_model_card_refuse("a packet the model does not read: ", head);
                        break;
                }
                if (op == 0x76) // SET_SH_REG: the offset, then a value a register
                        for (unsigned int k = 1; k < count && ib[at + 1] + k - 1 < 0x400; k++)
                                gpu_model_card.sh[ib[at + 1] + k - 1] = ib[at + 1 + k];
                else if (op == 0x15 && count == 4) // DISPATCH_DIRECT: x, y, z, the initiator
                        gpu_model_card_dispatch(ib + at + 1);
                else if (op != 0x58) // ACQUIRE_MEM: the caches emptied, which the model has none of
                        gpu_model_card_refuse("a packet the model does not take: ", head);
                at += 1 + count;
        }
        gpu_model_card.faults += gpu_model.fault.code != 0;
}

// The card's buffer as gpu_start makes it for bytes of the caller's (and the self-test's megabyte), with the kernels copied in.
static b32 gpu_model_card_start(positive bytes)
{
        positive size = (GPU_DATA + bytes + GPU_MODEL_STAGE + (1 << 20) + 4095) & ~4095ull;
        long at = system_call_6(syscall(mmap), 0, size, 3, 0x22, (positive)-1, 0);

        if (at < 0 && at > -4096)
                return false;
        gpu_model_card.buffer = (p8 address_to)at, gpu_model_card.bytes = size, gpu_model_card.staged_at = size - (1 << 20) - GPU_MODEL_STAGE, gpu_model_card.staged_words = 0;
        gpu.fd = -1, gpu.ctx = 1, gpu.at = (unsigned int address_to)at, gpu.size = size, gpu.pending = 0, gpu.imports = 0, gpu.broken = 0, gpu_map_count = 0;
        for (unsigned int i = 0, here = 1024; i < GPU_KERNELS; i++)
        {
                gpu_kernel_at[i] = here;
                memory_copy((p8 address_to)gpu.at + here, gpu_kernels[i].words + 2, gpu_kernels[i].size - 8);
                here = (here + gpu_kernels[i].size - 8 + 255) & ~255u;
        }
        gpu_model_card_reset();
        return true;
}

static void gpu_model_card_stop(void)
{
        if (gpu_model_card.buffer)
                system_call_2(syscall(munmap), (positive)gpu_model_card.buffer, gpu_model_card.bytes);
        gpu_model_card.buffer = null, gpu.at = null, gpu.size = 0;
}
