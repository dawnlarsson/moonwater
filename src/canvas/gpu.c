#ifndef MOONWATER_GPU_C
#define MOONWATER_GPU_C
// Canvas on the GPU: AMD compute, raw. One buffer holds the packets, the arguments, the kernel and the pixels.

#define GPU_IO(dir, n, size) ((dir) << 30 | (size) << 16 | 0x6400u | (0x40 + (n)))
#define GPU_PKT(op, n) (0xc0000002u | (op) << 8 | (n) << 16) // type 3, for the compute shader type
// The card's address space as its driver gives it (struct gpu_backend's window, asked at each start once the card is picked; amdgpu's until then): the card's
// own buffer at va, the maps and imports placed in [maps, end). Every address a record holds is in it, so a driver whose address space is smaller (an old
// amdgpu's 64 GiB, Ivy Bridge's 2 GiB) is given a window inside its own.
#define GPU_AMD_VA 0x1000000000ull
#define GPU_AMD_VA_MAPS 0x2000000000ull
#define GPU_VA_RECORD (1ull << 40) // a close op's record holds 40 bits of an address: no map or import ends past it, whatever the driver's space
static struct { positive va, maps, top; } gpu_window = {GPU_AMD_VA, GPU_AMD_VA_MAPS, GPU_VA_RECORD};
#define GPU_VA (gpu_window.va)
#define GPU_VA_MAPS (gpu_window.maps)
#define GPU_VA_END (gpu_window.top) // never past GPU_VA_RECORD
static positive gpu_map_next = GPU_AMD_VA_MAPS; // where the next map or import is placed (gpu_place_aligned): from GPU_VA_MAPS of the window
#define GPU_BASES_AT 40960    // the table of bases a text op names (its word 6, bits 16 to 23: 255 addresses of 8 bytes, number 0 is the buffer's start and is not read)
#define GPU_SRGB_S2L_AT 43008 // 256 x u32 IEC 61966-2-1 sRGB -> 12-bit linear (0..4095), 1024 bytes (43008..44032)
#define GPU_SRGB_L2S_AT 44032 // 4096 x u8 IEC 61966-2-1 12-bit linear -> sRGB (0..255), 4096 bytes (44032..48128)
#define GPU_DATA 49152        // the pixels start here; before them: packets at 0, arguments after the packets, the kernels from 1024, bases at 40960, sRGB LUTs at 43008
// The two slots a job is built in, used in turn so that the next job is written while one runs: slot k's packets at GPU_PACKETS * k (the driver's room for a
// job's command words: struct gpu_backend's packets), its arguments at GPU_SLOTS * GPU_PACKETS + 256 * k, the kernels from gpu_layout.code. With amdgpu's 256
// that is packets at 0 and 256, arguments at 512 and 768, the kernels from 1024. A slot is not written while its job may run (gpu_slot_take).
#define GPU_SLOTS 2
#define GPU_AMD_PACKETS 256
static struct { unsigned int packets, args, state, code; } gpu_layout = {GPU_AMD_PACKETS, GPU_SLOTS * GPU_AMD_PACKETS, 0, 1024};
#define GPU_PACKETS (gpu_layout.packets)

// broken: the card is there and drew the self-test wrong; pending: the job last waited for and not seen done (its sequence on ctx), 0 for none
// The imports the table may hold: as many as there are buffers (gpu_imports_room grows it a doubling at a time in a reservation of
// GPU_IMPORTS_MOST, address space only), where it held 64 and refused the 65th.
#define GPU_IMPORTS_MOST (1u << 20)
// The ticks an import let go of stays mapped before it is unmapped (gpu_trim): a client's buffers come round again (Chromium's three or four are lent in turn,
// each let go of as the next is lent), and a job built or still running in the tick an import was let go of may name it. Taken again within them it is
// the same import, held again (gpu_import_again); after them, with the card idle, it is unmapped. imported_dead_after is the submit serial an import was let go
// of after (gpu_submit_serial then): the submit that follows it is the last that names it (gpu_submit).
#define GPU_IMPORT_GRACE 8
static struct { int fd, broken; unsigned int family, device, ctx, handle, *imported, imports; positive *imported_va, *imported_bytes; unsigned char *imported_dead, *imported_holds; positive *imported_dead_at; unsigned int *imported_dead_after, address_to at; positive size, pending; unsigned char *imported_own, *imported_handed; positive *imported_used; unsigned int *imported_gen; } gpu;
static unsigned char *gpu_import_at_room; // the reservation the import table's arrays are in, and the imports committed in it
static unsigned int gpu_import_room;
static p64 *gpu_submit_bos;               // a submit's buffers (the ring, the imports and maps a job may read), as many as the imports have room for
// The memory the dma-buf route may reach: the window table's memfd (fd), mapped at at (its byte 0), the picture part of it (bytes), and pin(offset, on) when a
// map of its pages is made or let go, so that the program that owns them does not give them back while the card may read them.
static struct { int fd; unsigned char *at; positive bytes; void (*pin)(positive offset, b32 on); } gpu_shared = {-1, 0, 0, 0};
// The compositor's memfds registered for the route, as many as it registers (a table that grows: gpu_memfds_room).
struct gpu_memfd { int fd; void *at; positive bytes; void (*pin)(positive offset, b32 on); };
static struct gpu_memfd *gpu_memfds;
static unsigned int gpu_memfd_count;

// Growable/reserved anonymous mmap arena helpers shared across gpu.c (imports, memfds, i915 tables, bin staging).
static void *gpu_mmap_reserve(positive bytes, int prot)
{
        long got = (long)system_call_6(syscall(mmap), 0, bytes, (positive)prot, 0x4022, (positive)-1, 0);

        return got < 0 && got > -4096 ? 0 : (void *)got;
}

static void gpu_buf_free(void **ptr, positive *cap)
{
        if (*ptr)
                system_call_2(syscall(munmap), (positive)*ptr, *cap);
        *ptr = 0, *cap = 0;
}

static void *gpu_buf_grow(void **ptr, positive *cap, positive need, positive preserve)
{
        positive want;
        long p;

        if (!need || need > (~(positive)0) - 4095ull)
                return 0;
        want = (need + 4095) & ~(positive)4095;
        if (*ptr && *cap >= want)
                return *ptr;
        p = (long)system_call_6(syscall(mmap), 0, want, 3, 0x22, (positive)-1, 0);
        if (p < 0 && p > -4096)
                return 0;
        if (*ptr)
        {
                if (preserve > *cap)
                        preserve = *cap;
                if (preserve > want)
                        preserve = want;
                if (preserve)
                        memory_copy_apart((void *)p, *ptr, preserve);
                system_call_2(syscall(munmap), (positive)*ptr, *cap);
        }
        *ptr = (void *)p, *cap = want;
        return *ptr;
}

// The import table's arrays pointed at their reservation (made at the first need), with room for `want`: 1, or 0 when the memory is not there.
#define GPU_IMPORT_ARRAYS(X) X(imported, unsigned int) X(imported_va, positive) X(imported_bytes, positive) X(imported_dead, unsigned char) \
        X(imported_holds, unsigned char) X(imported_dead_at, positive) X(imported_dead_after, unsigned int) X(imported_own, unsigned char) \
        X(imported_handed, unsigned char) X(imported_used, positive) X(imported_gen, unsigned int)
static b32 gpu_imports_room(unsigned int want)
{
        positive at = 0, room;

        if (!gpu_import_at_room)
        {
                positive bytes = 0;

#define GPU_IMPORT_SIZE(name, type) bytes += ((positive)GPU_IMPORTS_MOST * sizeof(type) + 4095) & ~4095ull;
                GPU_IMPORT_ARRAYS(GPU_IMPORT_SIZE)
#undef GPU_IMPORT_SIZE
                bytes += ((positive)(GPU_IMPORTS_MOST + 64 + 1024) * sizeof(p64) + 4095) & ~4095ull; // the submit's buffers, with the maps
                if (!(gpu_import_at_room = (unsigned char address_to)gpu_mmap_reserve(bytes, 0)))
                        return false;
        }
#define GPU_IMPORT_POINT(name, type) gpu.name = (type address_to)(gpu_import_at_room + at), at += ((positive)GPU_IMPORTS_MOST * sizeof(type) + 4095) & ~4095ull;
        GPU_IMPORT_ARRAYS(GPU_IMPORT_POINT)
#undef GPU_IMPORT_POINT
        gpu_submit_bos = (p64 address_to)(gpu_import_at_room + at);
        //      Room 0 is a reservation with nothing committed yet, not a table that has room for no import: the submit's list of
        //      buffers is in it too, and a job before the first import (gpu_start's self-test) wrote it through memory not mapped.
        if (want <= gpu_import_room && gpu_import_room)
                return true;
        if (want > GPU_IMPORTS_MOST)
                return false;
        room = gpu_import_room ? 2 * (positive)gpu_import_room : 64;
        room = room < want ? want : room > GPU_IMPORTS_MOST ? GPU_IMPORTS_MOST : room;
        at = 0;
#define GPU_IMPORT_COMMIT(name, type) \
        if (system_call_3(syscall(mprotect), (positive)(gpu_import_at_room + at), (room * sizeof(type) + 4095) & ~4095ull, 3) != 0) \
                return false; \
        at += ((positive)GPU_IMPORTS_MOST * sizeof(type) + 4095) & ~4095ull;
        GPU_IMPORT_ARRAYS(GPU_IMPORT_COMMIT)
#undef GPU_IMPORT_COMMIT
        if (system_call_3(syscall(mprotect), (positive)gpu_submit_bos, ((room + 64 + 1024) * sizeof(p64) + 4095) & ~4095ull, 3) != 0)
                return false;
        gpu_import_room = (unsigned int)room;
        return true;
}

// Room for `want` memfds: 1, or 0 when the memory is not there (a fixed array of a reservation, grown a doubling at a time).
static b32 gpu_memfds_room(unsigned int want)
{
        static unsigned int room;
        positive grown;

        if (!gpu_memfds && !(gpu_memfds = (struct gpu_memfd address_to)gpu_mmap_reserve((positive)GPU_IMPORTS_MOST * sizeof(struct gpu_memfd), 0)))
                return false;
        if (want <= room)
                return true;
        if (want > GPU_IMPORTS_MOST)
                return false;
        grown = room ? 2 * (positive)room : 64;
        grown = grown < want ? want : grown > GPU_IMPORTS_MOST ? GPU_IMPORTS_MOST : grown;
        if (system_call_3(syscall(mprotect), (positive)gpu_memfds, (grown * sizeof(struct gpu_memfd) + 4095) & ~4095ull, 3) != 0)
                return false;
        room = (unsigned int)grown;
        return true;
}

// A memfd of the compositor's own, mapped shared at at for bytes, registered for the dma-buf route (gpu_map_as): a map asked inside it is made of all of it, by
// udmabuf first where the memfd is sealed as udmabuf takes one (gpu_memfd_sealed), so every picture in it is one handle and one address range of the card's.
__attribute__((unused)) static b32 gpu_register_memfd(int fd, void *at, positive bytes, void (*pin)(positive offset, b32 on))
{
        int free_slot = -1;

        if (fd < 0 || !at || !bytes || !gpu_memfds_room(gpu_memfd_count + 1))
                return false;
        for (int i = 0; i < (int)gpu_memfd_count; i++)
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
                free_slot = (int)gpu_memfd_count++;
        gpu_memfds[free_slot].fd = fd, gpu_memfds[free_slot].at = at, gpu_memfds[free_slot].bytes = bytes, gpu_memfds[free_slot].pin = pin;
        return true;
}

__attribute__((unused)) static void gpu_unregister_memfd(void *at)
{
        if (!at)
                return;
        for (unsigned int i = 0; i < gpu_memfd_count; i++)
                if (gpu_memfds[i].at == at)
                {
                        gpu_memfds[i].fd = -1, gpu_memfds[i].at = 0, gpu_memfds[i].bytes = 0, gpu_memfds[i].pin = 0;
                        return;
                }
}
static b32 gpu_userptr_off, gpu_userptr_gone; // off: /run/moonwater/gpu-nouserptr says to use the dma-buf route for every map (a test); gone: the kernel refused userptr with
                                              // ENODEV, EINVAL, EOPNOTSUPP or ENOSYS, which is not asked again (every map after goes by the dma-buf route)
// /run/moonwater/gpu-own-userptr, looked for at each start as gpu-nouserptr is: the order of the maps before the compositor's own sealed memfds went by the dma-buf
// route first (userptr first for every map, the dma-buf route its fallback, a registered memfd mapped a picture at a time), for the owner's A and B and as the way back.
static b32 gpu_own_userptr;
// The maps of the compositor's own memory that went to userptr because the dma-buf route refused them (udmabuf past its size_limit_mb, an import or a bind refused),
// and the last refusal's errno (moonwater debug): the rest of gpu_maps_user are memory that is not the compositor's (a client's pool) or not sealed.
static unsigned int gpu_maps_own_user, gpu_maps_own_errno;
// The start's probe took an anonymous page by userptr: a userptr refused after with EINVAL, EFAULT or EPERM is the address's (a dma-buf's mmap, a page gone), not
// the kernel's, and does not make userptr gone for the session (gpu_map_as).
static b32 gpu_userptr_ok;
// /run/moonwater/gpu-round-off, looked for at each start as gpu-nouserptr is: a blended window whose picture reaches into a round corner is the processor's (the
// compositor refuses it, COMPOSITOR_GPU_WHY_ROUND), as before the scene kernel laid such a corner over by its cover (12): the owner's way back if a card draws it wrong.
static b32 gpu_round_off;
// /run/moonwater/gpu-cut-off, looked for at each start as gpu-nouserptr is: the bins made without the cut (gpu_record_holds), every record a cell touches in its
// list as before 321d8bc6, the owner's way back if a window or a part of one is ever missing from the card's frame; and a check's switch, for the differential that
// holds the cut to the picture the kernel draws without it.
static b32 gpu_cut_off;
// /run/moonwater/gpu-imports-all, looked for at each start as gpu-nouserptr is: every live import named in every job's bo list, as before (gpu_import_named), for the
// owner to measure one against the other.
static b32 gpu_imports_all;
static unsigned int gpu_maps_user, gpu_maps_dma; // maps made, by userptr and by the dma-buf route (moonwater debug)
static b32 gpu_lost; // a job's submit said the card lost its context or VM (set by gpu_run; the caller recovers and clears it)
static unsigned int gpu_recovered; // how many times the card was started again after it lost its state (moonwater debug)
static unsigned int gpu_epoch; // the card's descriptor count: it goes up each time the card is let go of (gpu_stop), and with it every import and map. An address kept
                               // from an earlier epoch is none of the card's (wayland_host.c's lent and source addresses check it)
static positive gpu_bytes; // the bytes gpu_start was asked for (gpu_recover starts the card again with them)
#ifndef GPU_SUBMIT_FAKE
#define GPU_SUBMIT_FAKE(error, taken, sequence)
#endif
#ifndef GPU_OPEN_FAKE
#define GPU_OPEN_FAKE(fd)
#endif
// Every request this file makes of the kernel's DRM goes through GPU_CONTROL (a descriptor, the request, its record: 0 or a count when the kernel did it, else
// -errno), so that a check can put a kernel of its own under the whole path, and so that the path's dealings with a card are in one place.
#ifndef GPU_DUP
// A descriptor of the card of its holder's own (the clock thread's, compositor_gpu_clocks_ask), and its close: a check's fake card has a number, no file.
#define GPU_DUP(fd) ((int)system_call_3(syscall(fcntl), (positive)(fd), 1030, 0)) // F_DUPFD_CLOEXEC
#define GPU_UNDUP(fd) system_close(fd)
#endif
#ifndef GPU_CONTROL
#define GPU_CONTROL(fd, request, a) system_control(fd, request, a)
#endif
#ifndef GPU_WIDE_FAKE
#define GPU_WIDE_FAKE(fd) // a check answers for the file that lets the other gfx10 families in
#endif
#ifndef GPU_UDMABUF_OPEN
#define GPU_UDMABUF_OPEN() system_open_at(AT_FDCWD, (string_address) "/dev/udmabuf", FILE_READ_WRITE | O_CLOEXEC | O_NOFOLLOW) // a check's fake kernel answers here
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
// The kernels are the tree's gpu.asm's (CLASS-h4-2): an include made of another one (a hand build's artifacts/gpu_kernels.inc left from before a
// change of gpu.asm) compiled silently, and the card ran the old words.
#include "gpu_asm.hash"
#if !defined(GPU_KERNELS_ASM_HASH)
#error "the GPU kernels' include was made before it said its source: python3 -I test/gpu_kernels.py --print src/canvas/gpu.asm > artifacts/gpu_kernels.inc"
#elif GPU_KERNELS_ASM_HASH != GPU_ASM_HASH
#error "the GPU kernels' include is not of this src/canvas/gpu.asm: python3 -I test/gpu_kernels.py --print src/canvas/gpu.asm > artifacts/gpu_kernels.inc (and --hash src/canvas/gpu.asm > src/canvas/gpu_asm.hash after a change of gpu.asm)"
#endif

// The Intel GPU's kernels, Gen9: src/canvas/gpu_i915.asm as `build gpu-i915` assembles them. gpu_i915_fill is op 1: its entry is
// the instruction after IGT's colour replicate (64-byte aligned, see the source). The build names the file with
// -DMOONWATER_GPU_I915_KERNELS; a program compiled by hand has it where ./build gpu-i915 writes it.
#ifdef MOONWATER_GPU_I915_KERNELS
#include MOONWATER_GPU_I915_KERNELS
#else
#include "../../artifacts/gpu_i915_gen9.inc"
#endif

// Every kernel (rsrc1, rsrc2, then the code), put 256-aligned in the buffer from byte 1024, in the order of the ops that run them (4 and 5 the clang scenes, 6 the scene by hand for a width and a pitch of a multiple of 4, 7 a picture scaled by tables, 8 a picture read through the texture unit, 9 an NV12 picture scaled by tables and made XRGB).
struct gpu_code { const unsigned int *words; unsigned int size; };
static const struct gpu_code gpu_kernels[] = {
    {gpu_k_copy, sizeof(gpu_k_copy)}, {gpu_k_fill, sizeof(gpu_k_fill)}, {gpu_k_tone, sizeof(gpu_k_tone)}, {gpu_k_glass, sizeof(gpu_k_glass)},
    {gpu_cp, sizeof(gpu_cp)}, {gpu_bx, sizeof(gpu_bx)}, {gpu_k_scene, sizeof(gpu_k_scene)}, {gpu_k_scale, sizeof(gpu_k_scale)},
    {gpu_k_image, sizeof(gpu_k_image)}, {gpu_k_nv12, sizeof(gpu_k_nv12)}};
static const struct gpu_code gpu_kernels_gfx11[] = {
    {gpu_k_11_copy, sizeof(gpu_k_11_copy)}, {gpu_k_11_fill, sizeof(gpu_k_11_fill)}, {gpu_k_11_tone, sizeof(gpu_k_11_tone)}, {gpu_k_11_glass, sizeof(gpu_k_11_glass)},
    {0, 0}, {0, 0}, {gpu_k_11_scene, sizeof(gpu_k_11_scene)}, {gpu_k_11_scale, sizeof(gpu_k_11_scale)},
    {gpu_k_11_image, sizeof(gpu_k_11_image)}, {gpu_k_11_nv12, sizeof(gpu_k_11_nv12)}};
static const struct gpu_code gpu_kernels_gfx12[] = {
    {gpu_k_12_copy, sizeof(gpu_k_12_copy)}, {gpu_k_12_fill, sizeof(gpu_k_12_fill)}, {gpu_k_12_tone, sizeof(gpu_k_12_tone)}, {gpu_k_12_glass, sizeof(gpu_k_12_glass)},
    {0, 0}, {0, 0}, {gpu_k_12_scene, sizeof(gpu_k_12_scene)}, {gpu_k_12_scale, sizeof(gpu_k_12_scale)},
    {gpu_k_12_image, sizeof(gpu_k_12_image)}, {gpu_k_12_nv12, sizeof(gpu_k_12_nv12)}};
#define GPU_KERNELS (sizeof(gpu_kernels) / sizeof(gpu_kernels[0]))
#define GPU_KERNEL_OF(op) (op) // the table is in the order of the ops
static unsigned int gpu_kernel_at[GPU_KERNELS];
typedef char gpu_amd_slots_fit[GPU_SLOTS * GPU_AMD_PACKETS + 256 * GPU_SLOTS <= 1024 ? 1 : -1]; // amdgpu's packets and arguments end where its kernels begin
typedef char gpu_kernels_fit[sizeof(gpu_k_copy) + sizeof(gpu_k_fill) + sizeof(gpu_k_tone) + sizeof(gpu_k_glass) + sizeof(gpu_cp) + sizeof(gpu_bx) + sizeof(gpu_k_scene) + sizeof(gpu_k_scale) + sizeof(gpu_k_image) + sizeof(gpu_k_nv12) + 256 * 10 <= GPU_BASES_AT - 1024 ? 1 : -1]; // they end before the bases begin

#define gpu_io(dir, n, size, a) (GPU_CONTROL(gpu.fd, GPU_IO(dir, n, size), a) == 0)
#ifndef GPU_WAIT_CS
#define GPU_WAIT_CS(wait) gpu_wait_asked(wait) // a check puts a fake fence here
#endif

// The card must draw what it should before anything is trusted to it: families of the ISA the kernels are in (RDNA2, gfx10.3: Van Gogh 144, Yellow Carp 146, Raphael 149,
// Mendocino 151) are tried, and each is held to the hash of the picture one small scene of every kind of op made on the card they were written on.
#define GPU_FAMILIES(f) ((f) == 144 || (f) == 146 || (f) == 149 || (f) == 151)
#define GPU_TEST_HASH 1092551981u
// The rest of the chips the kernels are code for, which nobody has run them on: family 143 (Navi 10, 12 and 14, RDNA1; Navi 21 to 24, the RDNA2 cards). llvm-mc
// makes of gpu.asm the same 3,749 words for gfx1010, gfx1012, gfx1030 and gfx1036 (the gate asks it: gpu-kernels-chips), so the code is theirs too as far as an assembler can say; what the dispatch's
// registers and the ring do on them has been seen by no one. They are let in only where the machine's owner asks (the file /run/moonwater/gpu-gfx10, looked for at
// each start, as gpu-nouserptr is), and then held to the self-test as every card is: a card that draws it wrong is not trusted, and one that hangs on it is the
// hang's two seconds and no card. Off unless asked: a job that hangs on a ring the kernel ends only after its own timeout (sixty seconds at v6.12) is a minute of
// the processor drawing on a machine whose owner did not choose to try.
#define GPU_FAMILIES_WIDE(f) ((f) == 143)
static b32 gpu_wide; // the owner asked for them
static b32 gpu_gfx103; // the open card has GFX 10.3 compute IP (RDNA2: Navi 21/22/23/24, Van Gogh, Rembrandt, Raphael, Mendocino)
static unsigned int gpu_amd_gfx_major, gpu_amd_gfx_minor; // compute IP version from AMDGPU_INFO_HW_IP_INFO
static b32 gpu_family_ok(unsigned int family)
{
        // Enable all RDNA2 (GFX 10.3) and RDNA1 (GFX 10.1) cards via compute IP version (validated by gpu_test_hash at startup),
        // falling back to the explicit family allowlist when HW_IP_INFO is unavailable (e.g. unit-test fake cards).
        if (gpu_amd_gfx_major == 10 && (gpu_amd_gfx_minor == 3 || gpu_amd_gfx_minor == 1))
                return true;
        return GPU_FAMILIES(family) || ((gpu_wide || gpu_gfx103) && GPU_FAMILIES_WIDE(family));
}
// A driver's kernels for one instruction set (struct gpu_backend's set): the kernels in the order of the ops, each its words in the table (skip of them the set's
// own header, not code: amdgpu's rsrc1 and rsrc2), the hash the self-test's first pass draws (GPU_TEST_HASH for the set it was taken on), and for a driver whose
// command words are made from the job (i915's batch) the call that makes them: the words written at words, room of them at most, 0 for none.
struct gpu_job;
struct gpu_set
{
        const struct gpu_code *kernels;
        unsigned int count, skip, hash;
        unsigned int (*batch)(const struct gpu_job *job, unsigned int address_to words, unsigned int room);
};
static const struct gpu_set gpu_amd_rdna2 = {gpu_kernels, GPU_KERNELS, 2, GPU_TEST_HASH, 0};
static const struct gpu_set gpu_amd_rdna3 = {gpu_kernels_gfx11, GPU_KERNELS, 2, GPU_TEST_HASH, 0};
static const struct gpu_set gpu_amd_rdna4 = {gpu_kernels_gfx12, GPU_KERNELS, 2, GPU_TEST_HASH, 0};
static const struct gpu_set *gpu_set = &gpu_amd_rdna2; // the started card's (amdgpu's until a start says otherwise: a check's fake card is one)
// The prerequisites of the path, in the order it needs them. gpu_start stops at the first that fails and keeps it: the code, the kernel's error (its
// errno; 0 where the step failed by what it found, not by a call), and a bit in gpu_req.mask for each that passed. The node the checks looked at (its path,
// mode and owner), the driver's name and the family are kept whether or not that node was taken: moonwater debug says them.
enum gpu_req
{
        GPU_REQ_NONE,
        GPU_REQ_NODE,     // a render node opens (none does: the first one's error, and its mode and owner)
        GPU_REQ_DRIVER,   // a node's driver is one of gpu_backends' (amdgpu, i915: the first node's name is kept; another driver has no GPU path yet)
        GPU_REQ_FAMILY,   // the chip has a kernel set (its driver's set call: the family and device id are kept; i915 has none yet, so every i915 chip stops here)
        GPU_REQ_USERPTR,  // AMDGPU_GEM_USERPTR maps an anonymous page (19: a kernel without CONFIG_DRM_AMDGPU_USERPTR)
        GPU_REQ_CONTEXT,  // a GEM buffer, its VM map and a compute context
        GPU_REQ_RING,     // the compute ring finished the self-test's job (its error when it did not: the submit's, or 62 for a job still running)
        GPU_REQ_SELFTEST, // the card drew the self-test's picture as the kernels do
        GPU_REQ_UDMABUF,  // the dma-buf route, the fallback for a refused userptr: /dev/udmabuf makes a dma-buf of a sealed memfd page
        GPU_REQ_PRIME,    // and that dma-buf imports into the render node. Checked once the card is up (gpu_route_probe): not a step the card needs while userptr works
        GPU_REQ_COUNT
};
// first: the required step that failed (0 none; the card is not up); route: the dma-buf route's step that failed once the card is up (0 none; the card is up).
// settled: the start failed by what the nodes are, not by anything that may change while they stay as they are: no node's driver is one this file has, or every
// node of one that is answered with a chip that has no kernel set (a driver whose chip could not be asked, a node that would not open, are not settled); nodes:
// the render nodes the start saw, as gpu_nodes_seen says them (nonzero), so that a late look can tell the same nodes from new ones without starting the card;
// name: the named node's driver's whole name (driver is the table's eight bytes of it).
static struct { unsigned int first, error, mask, mode, owner, family, device, route, route_error, pick, settled; positive nodes; char driver[8], node[20], bus[32], name[32]; } gpu_req;
static char gpu_display[32]; // the display card's bus (set by the compositor before the start: "" for a headless one, or one the kernel does not name)
#define GPU_NODES 8
typedef enum gpu_end
{
        GPU_END_NONE,
        GPU_END_LATE,
        GPU_END_HUNG,
        GPU_END_WAIT,
        GPU_END_OVERFLOW
} gpu_end_t;

// Consolidated 2-slot compute ring & job lifecycle state (replaces 11 scattered job-slot globals).
struct gpu_ring_slot
{
        positive sequence, began;
};

static struct gpu_ring_state
{
        struct gpu_ring_slot slot[GPU_SLOTS]; // per-slot fence sequence and submission timestamp
        struct gpu_ring_slot before;          // job before the pending one, in the other slot, while not seen done (0: none)
        unsigned int build_slot;              // slot the last job was built in (GPU_ARGS is its arguments)
        unsigned int pending_slot;            // slot of the newest pending job
        gpu_end_t end_kind;                   // last call's end reason
        gpu_end_t found;                      // bad end gpu_idle came on before gpu_late_end reports it
        b32 end_said;                         // end of pending jobs has been reported
        b32 patient;                          // self-test wait patience (0: tick, 1: hang, 2: recover)
        unsigned int last_error;              // last job's refusal errno / ETIME / EIO
        positive last_job;                    // when the last job finished (monotonic ns)
        positive job_began;                   // when the job being run was submitted (monotonic ns)
        positive fault_poll;                  // next time to poll kmsg for a page fault (monotonic ns)
} gpu_ring;

#define gpu_end          (gpu_ring.end_kind)
#define gpu_found        (gpu_ring.found)
#define gpu_end_said     (gpu_ring.end_said)
#define gpu_fault_poll   (gpu_ring.fault_poll)
#define gpu_patient      (gpu_ring.patient)
#define gpu_before       (gpu_ring.before)
#define gpu_slot         (gpu_ring.build_slot)
#define gpu_pending_slot (gpu_ring.pending_slot)
#define gpu_last_error   (gpu_ring.last_error)
#define gpu_last_job     (gpu_ring.last_job)
#define gpu_job_began    (gpu_ring.job_began)

static void gpu_ring_reset(void)
{
        memory_zero(gpu_ring.slot, sizeof(gpu_ring.slot));
        gpu_ring.before.sequence = gpu_ring.before.began = 0;
        gpu_ring.build_slot = gpu_ring.pending_slot = 0;
        gpu_ring.end_said = 0;
        gpu_ring.found = GPU_END_NONE;
}
#define GPU_LATE_NS 16000000ull      // a job not done this long after it was submitted is late: a tick (the heaviest whole-screen job measured, 8K, is 5.5 ms)
#define GPU_HANG_NS 2000000000ull    // and one not done this long after is hung. The kernel's own timeout for a job is amdgpu.lockup_timeout, which at v6.12 is sixty
                                     // seconds for the compute ring this uses: until it ends the job nothing here ends it, the buffer stays the job's (gpu_idle), and the
                                     // processor draws
#define GPU_FAULT_FIRST 250000000ull // a pending job's first look at the kernel's log for a page fault is this long after it went late, and the next ones
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
        The card's driver, behind ten calls and the values of its row. Everything above them is any card's: the scene's records and their bins, the table of
        maps and of imports and their addresses, the tick and the marks, the late, hung and lost job, the self-test, the prerequisites. Everything below them is
        one driver's requests of the kernel and one chip's command words. A second driver, or a second chip of this one with other words, is a second struct
        gpu_backend and its kernel sets; nothing above the calls names a driver. (The requests every DRM driver has are not behind them: the version, a handle
        closed, a dma-buf to a handle and back.) Each call answers 0 or the kernel's errno unless it says otherwise.

        A refused submit is what the driver's classify says it is (enum gpu_err), since the same errno is another thing to each driver:
          amdgpu: EAGAIN (11) asked again at once; ECANCELED (125, the context lost in a reset, or guilty) and ENODEV (19, the device gone) the card lost;
                  EINVAL (22) a suspect (GPU_ERR_SUSPECT): amdgpu_cs answers it for a bad chunk or buffer of one job as well as for a context it has not
                  (amdgpu_cs_parser_init) or a VM not ready (amdgpu_cs_vm_handling), so it is refused as anything else is, and the card is lost only
                  when it comes again on the next try or submit; anything else refused, asked again up to three times without the maps whose pages are gone.
          i915:   EAGAIN and EINTR (4) asked again at once; EIO (5, a context banned after a reset, or the GPU wedged) and ENODEV the card lost; EINVAL (our
                  batch or an object the kernel will not take as it is: a misaligned batch, a pinned offset not page-aligned or not canonical), ENOSPC (28, a
                  pinned range something else holds: on Ivy Bridge and Haswell the display's), ENOENT (2, a handle the kernel does not have) and EOPNOTSUPP
                  (95, no batch for the chip: no kernel set) our own job, refused as it is and never asked again (the processor draws it, and it is counted
                  as any refused job is); anything else (EFAULT, a userptr's pages gone; ENOMEM) refused as amdgpu's.
*/
struct gpu_job
{
        const unsigned int *kernel; // the kernel's words in its set (the set's header first: amdgpu's two resource words; the code follows them in the card's buffer)
        positive code, arguments;   // the card's addresses of the code and of the arguments' block
        unsigned int columns, rows; // the grid: columns of the driver's lanes (struct gpu_backend's lanes: amdgpu's wave of 64 pixels), rows of what the kernel takes a row to be
        unsigned int address_to packets_at; // the job's room for its command words (GPU_PACKETS bytes of the card's buffer: its slot's), and its card address
        positive packets;
        unsigned int address_to state_at;   // the slot's room for state (struct gpu_backend's state bytes, 64-aligned; 0 for a driver with none), and its card
        positive state;                     // address: the only place besides packets_at a batch may write
};
enum gpu_err
{
        GPU_ERR_REFUSED, // the job refused: asked again up to three times, without the maps whose pages are gone (gpu_drop_unmapped)
        GPU_ERR_AGAIN,   // asked again at once (up to GPU_AGAIN times)
        GPU_ERR_LOST,    // the card lost its state: the caller starts it again (gpu_recover)
        GPU_ERR_OURS,    // our own job is wrong as it is (the same job is refused the same way again): refused at once, not asked again, and the card is not lost
        GPU_ERR_PLACE,   // an address the job pins is not ours to have (i915's ENOSPC: on Ivy Bridge and Haswell the display holds ranges of the one address space):
                         // the job is refused, and the maps are placed elsewhere for the next (gpu_place_elsewhere); the card is not lost
        GPU_ERR_SUSPECT  // refused as REFUSED is (asked again without the maps whose pages are gone), and the card lost only when the submit before was refused
                         // so too (amdgpu's EINVAL: a chunk or a buffer of this one job, or the context or the VM the kernel no longer has or has not restored)
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
static unsigned int gpu_text_put(char *to, unsigned int at, unsigned int room, const char *words);
static unsigned int gpu_text_num(char *to, unsigned int at, unsigned int room, positive value, unsigned int base);
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
        // 10. a job's fence as a sync_file: a descriptor that polls readable once the job is done (one of a job done long ago is readable at once)
        unsigned int (*fence)(positive sequence, int *fd);
        // 11. the card's clocks held at a level while a context of their own on the descriptor fd lives (0: as the driver has them), that context made in
        // *ctx when it is 0; null for a driver that has no such hold
        unsigned int (*clocks)(int fd, unsigned int level, unsigned int *ctx);
        // The chip's kernel set (0 for none: a card of that chip is not one the path takes, and the processor draws), what a refused submit's errno is
        // (the mapping above), the card's address space for that chip (0, or the errno of a space too small for the path), and the chip in words for the
        // notices (into's end, as gpu_text_put answers it).
        const struct gpu_set *(*set)(unsigned int family);
        enum gpu_err (*classify)(unsigned int error);
        unsigned int (*window)(unsigned int family, positive *va, positive *maps, positive *top);
        unsigned int (*describe)(unsigned int family, unsigned int device, char *into, unsigned int at, unsigned int room);
        unsigned int lanes, packets; // a column of the grid in pixels (amdgpu's wave64: 64); a slot's room for its command words, in bytes (a multiple of 256)
        unsigned int state; // a slot's room for the state its command words point at, in bytes (a multiple of 64; 0 for none): i915's binding table, surface
                            // states and interface descriptor, which the chip's batch writes there (struct gpu_job's state) and nowhere else
};

// The top of the card's address space as the last DEV_INFO said it (virtual_address_max, struct drm_amdgpu_info_device at byte 152), 0 for not said.
static positive gpu_amd_va_max;
static unsigned int gpu_amd_identify(int fd, unsigned int *family, unsigned int *device)
{
        unsigned int who[40] = {0}, ip[8] = {0}, e;
        p64 ask[4] = {(p64)who, sizeof(who) | 0x16ull << 32}; // AMDGPU_INFO_DEV_INFO: the 160 bytes up to virtual_address_max
        p64 ip_ask[4] = {(p64)ip, sizeof(ip) | 0x02ull << 32, 1ull, 0}; // AMDGPU_INFO_HW_IP_INFO (AMDGPU_HW_IP_COMPUTE = 1)

        e = gpu_raw(fd, GPU_IO(1u, 5, 32), ask);
        *family = e ? 0 : who[4], *device = e ? 0 : who[0];
        if (e)
                return e;
        gpu_gfx103 = 0, gpu_amd_gfx_major = 0, gpu_amd_gfx_minor = 0;
        gpu_amd_va_max = (positive)who[39] << 32 | who[38];
        if (!gpu_raw(fd, GPU_IO(1u, 5, 32), ip_ask))
        {
                if (!ip[6]) // available_rings == 0: compute-disabled/harvested node
                        return *family = 0, 19u;
                gpu_amd_gfx_major = ip[0], gpu_amd_gfx_minor = ip[1];
                if (ip[0] == 10 && ip[1] == 3 && (who[4] != 143 || who[2] >= 0x28))
                        gpu_gfx103 = 1;
        }
        else if (who[4] == 145 || who[4] == 148 || who[4] == 150)
                gpu_amd_gfx_major = 11;
        else if (who[4] == 152)
                gpu_amd_gfx_major = 12;
        return 0;
}

// The card's address space. amdgpu's is the VM the kernel sized (amdgpu_vm_adjust_size): gmc v6 to v8 (SI to Polaris) ask for at least 64 GiB and at most 2^40, as
// three times the machine's memory in GiB rounded up to a power of two, so a machine of less than 22 GiB has 64 GiB, which ends where the window here begins (v6.19:
// gmc_v6_0.c 836, gmc_v7_0.c 1012, gmc_v8_0.c 1127, amdgpu_vm.c 2358); gmc v9 on and RDNA ask for 2^48 (gmc_v10_0.c 834). DEV_INFO says the top it took
// (min(vm, 2^47) less the reserved top, amdgpu_kms.c 970). A space that holds the whole record's reach keeps the window it always had; a smaller one has the window
// moved to 1 GiB, maps from 2 GiB to its top, and one under 3 GiB is refused (ENOSPC).
static unsigned int gpu_amd_window(unsigned int family, positive *va, positive *maps, positive *top)
{
        (void)family;
        if (!gpu_amd_va_max || gpu_amd_va_max >= GPU_VA_RECORD)
                return *va = GPU_AMD_VA, *maps = GPU_AMD_VA_MAPS, *top = GPU_VA_RECORD, 0;
        if (gpu_amd_va_max < (3ull << 30))
                return 28;
        return *va = 1ull << 30, *maps = 2ull << 30, *top = gpu_amd_va_max & ~4095ull, 0;
}

static const struct gpu_set *gpu_amd_set(unsigned int family)
{
        if (gpu_amd_gfx_major == 12)
                return &gpu_amd_rdna4;
        if (gpu_amd_gfx_major == 11)
                return &gpu_amd_rdna3;
        return gpu_family_ok(family) ? &gpu_amd_rdna2 : 0;
}

// The signal flags the watchdog installs with: shell.c's values where the shell is built; a check build that has no shell gets the same bits here.
#ifndef SIGNAL_RESTART
#define SIGNAL_RESTART 0x10000000
#endif
#ifndef SIGNAL_CATCH_RESTORER
#define SIGNAL_CATCH_RESTORER 0
#endif
#ifndef SIGNAL_CATCH_FLAGS
#define SIGNAL_CATCH_FLAGS SIGNAL_RESTART
#endif
static void gpu_submit_alarm(int sig);

static void gpu_alarm_arm(unsigned int sec)
{
        static b32 gpu_alarm_installed;
        positive wtimer[4] = {0, 0, sec ? sec : 2u, 0};

        if (unlikely(!gpu_alarm_installed))
        {
                gpu_alarm_installed = 1;
                system_signal_install(14, (positive)gpu_submit_alarm, SIGNAL_CATCH_FLAGS & ~SIGNAL_RESTART, SIGNAL_CATCH_RESTORER, null);
        }
        system_call_3(syscall(setitimer), 0, (positive)wtimer, 0);
}

static void gpu_alarm_disarm(void)
{
        positive woff[4] = {0, 0, 0, 0};

        system_call_3(syscall(setitimer), 0, (positive)woff, 0);
}

static enum gpu_err gpu_amd_classify(unsigned int error)
{
        return error == 11 || error == 4 ? GPU_ERR_AGAIN : error == 125 || error == 19 || error == 5 ? GPU_ERR_LOST : error == 22 ? GPU_ERR_SUSPECT : GPU_ERR_REFUSED;
}

static unsigned int gpu_amd_describe(unsigned int family, unsigned int device, char *into, unsigned int at, unsigned int room)
{
        const char *arch = "";

        if (family == 110)
                arch = ", Southern Islands / GCN1 (GFX6)";
        else if (family == 120 || family == 125)
                arch = ", Sea Islands / GCN2 (GFX7)";
        else if (family == 130 || family == 135)
                arch = ", Volcanic Islands / Polaris / GCN3-4 (GFX8)";
        else if (family == 141 || family == 142)
                arch = ", Vega / GCN5 (GFX9)";
        else if (family == 143)
                arch = gpu_gfx103 ? ", RDNA2 (GFX10.3)" : ", RDNA1 (GFX10.1)";
        else if (GPU_FAMILIES(family))
                arch = ", RDNA2 APU (GFX10.3)";
        else if (family == 145 || family == 148)
                arch = ", RDNA3 (GFX11.0)";
        else if (family == 150)
                arch = ", RDNA3.5 (GFX11.5)";
        else if (family == 152)
                arch = ", RDNA4 (GFX12.0)";
        else if (gpu_amd_gfx_major == 11)
                arch = ", RDNA3 (GFX11)";
        else if (gpu_amd_gfx_major == 12)
                arch = ", RDNA4 (GFX12)";
        at = gpu_text_put(into, at, room, "amdgpu family "), at = gpu_text_num(into, at, room, family, 10);
        at = gpu_text_put(into, at, room, ", device 0x"), at = gpu_text_num(into, at, room, device, 16);
        return gpu_text_put(into, at, room, arch);
}

static unsigned int gpu_amd_buffer(positive size, positive at, unsigned int *handle, p64 *offset)
{
        p64 make[4] = {size, 4096, 2, 64}, map[1] = {0}, va[8] = {0, 1 | 14ull << 32, at, 0, size};
        unsigned int e = gpu_call(3u, 0, 32, make); // GEM_CREATE, then GEM_MMAP for the offset, then GEM_VA: read, write and run

        if (e)
                return e;
        va[0] = map[0] = (unsigned int)make[0];
        e = gpu_call(3u, 1, 8, map);
        if (!e)
                *offset = map[0], e = gpu_call(1u, 8, 64, va);
        if (e)
        {
                p64 close[1] = {(unsigned int)make[0]};

                GPU_CONTROL(gpu.fd, GPU_GEM_CLOSE, close);
                return e;
        }
        *handle = (unsigned int)make[0];
        return 0;
}

/*
        The context the jobs run on, made at AMDGPU_CTX_PRIORITY_HIGH (512) where the kernel lets it (amdgpu_ctx_priority_permit: CAP_SYS_NICE, which the
        compositor has as root, or the master of the node): a high context's compute jobs go to the one compute queue the driver made high (the first, its
        pipe priority HIGH and queue priority MAXIMUM: amdgpu_gfx_is_high_priority_compute_queue, amdgpu_ring.c), whose waves the shader units' arbiter
        launches before those of the graphics queue a game draws on and of the normal compute queues; a normal context's are spread over the others, with
        a game's own compute. Refused (EACCES), the context is a normal one. gpu_priority: 2 high, 1 normal after a refusal (gpu_priority_error its errno).
*/
static unsigned int gpu_priority, gpu_priority_error;
static unsigned int gpu_amd_context(unsigned int *made, unsigned int before)
{
        unsigned int ctx[4] = {1, 0, 0, 512}, old[4] = {2, 0, before, 0}; // AMDGPU_CTX_OP_ALLOC_CTX at AMDGPU_CTX_PRIORITY_HIGH, and FREE_CTX of the one before
        unsigned int e = gpu_call(3u, 2, 16, ctx);

        gpu_priority = e ? 1 : 2, gpu_priority_error = e;
        if (e)
                ctx[0] = 1, ctx[1] = ctx[2] = ctx[3] = 0, e = gpu_call(3u, 2, 16, ctx);
        if (e)
                return gpu_priority = 0, e;
        if (before)
                gpu_call(3u, 2, 16, old);
        *made = ctx[0];
        return 0;
}

// A map to read is registered read-only (AMDGPU_GEM_USERPTR_READONLY, 1, with the REGISTER and VALIDATE flags, 0xc): the pages of a client's pool are
// mapped read-only by the compositor, and a writable registration of them is refused (EPERM). Without VALIDATE (0x8) the kernel takes the pages later.
static unsigned int gpu_amd_user(positive from, positive bytes, unsigned int how, unsigned int *handle)
{
        const p64 flags = how == GPU_USER_READ ? 0xdull : how == GPU_USER_WRITTEN ? 0xcull : 0x8ull;
        p64 up[3] = {from, bytes, flags};
        unsigned int e, again = 0, eintr = 0;

        for (;;)
        {
                up[0] = from, up[1] = bytes, up[2] = flags;
                e = gpu_call(3u, 0x11, 24, up);
                if (e == 4 && eintr++ < 16)
                        continue;
                if (e == 11 && again++ < 8)
                {
                        system_call_2(syscall(sched_yield), 0, 0);
                        continue;
                }
                break;
        }
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
        unsigned int ib[64], n = 0;
        // Per-generation cache invalidate + SH register setup (GFX6 SI through GFX12 RDNA4; default GFX10 when ip_major == 0).
        const unsigned int gfx = gpu_amd_gfx_major ? gpu_amd_gfx_major : 10u;

        if (!k || !job->columns || !job->rows)
                return 95;
        if (gfx == 6u)
        {
                // GFX6 (SI): SURFACE_SYNC (0x43) with CP_COHER_CNTL (TC | TCL1 | ICACHE | KCACHE)
                ib[n++] = GPU_PKT(0x43, 3), ib[n++] = 0x0ec00000u, ib[n++] = 0xffffffffu, ib[n++] = 0, ib[n++] = 10;
        }
        else if (gfx >= 7u && gfx <= 9u)
        {
                // GFX7/8/9 (CI/VI/Polaris/Vega): 6-payload ACQUIRE_MEM (0x58) with CP_COHER_CNTL (no GCR_CNTL)
                ib[n++] = GPU_PKT(0x58, 5), ib[n++] = 0x0ec00000u, ib[n++] = 0xffffffffu, ib[n++] = 0xffffffu, ib[n++] = 0, ib[n++] = 0, ib[n++] = 10;
        }
        else
        {
                // GFX10/11/12 (RDNA1-4): 7-payload ACQUIRE_MEM (0x58) with GCR_CNTL (GLI|GLM|GLK|GLV|GL1|GL2 invalidate)
                ib[n++] = GPU_PKT(0x58, 6), ib[n++] = 0, ib[n++] = 0xffffffffu, ib[n++] = 0xffffffu, ib[n++] = 0, ib[n++] = 0, ib[n++] = 10;
                ib[n++] = 1u | 1u << 5 | 1u << 7 | 1u << 8 | 1u << 9 | 1u << 14;
        }
        ib[n++] = GPU_PKT(0x76, 2), ib[n++] = 0x20c, ib[n++] = (unsigned int)(code >> 8), ib[n++] = (unsigned int)(code >> 40);
        ib[n++] = GPU_PKT(0x76, 2), ib[n++] = 0x212, ib[n++] = k[0], ib[n++] = k[1];
        if (gfx == 6u)
        {
                // GFX6 (SI): SE0..SE1 + TMPRING_SIZE only
                ib[n++] = GPU_PKT(0x76, 4), ib[n++] = 0x215, ib[n++] = 0, ib[n++] = 0xffffffffu, ib[n++] = 0xffffffffu, ib[n++] = 0x100;
        }
        else
        {
                // GFX7+: COMPUTE_RESOURCE_LIMITS (0x215 = 0) merged with SE0..SE3 (0x216..0x21a)
                ib[n++] = GPU_PKT(0x76, 6), ib[n++] = 0x215, ib[n++] = 0, ib[n++] = 0xffffffffu, ib[n++] = 0xffffffffu, ib[n++] = 0x100, ib[n++] = 0xffffffffu, ib[n++] = 0xffffffffu;
        }
        if (gfx >= 11u)
        {
                // GFX11/12 (RDNA3/3.5/4): enable SE4..SE6 (0x225..0x227) and SE7 (0x229) for 6-SE Navi31+
                ib[n++] = GPU_PKT(0x76, 3), ib[n++] = 0x225, ib[n++] = 0xffffffffu, ib[n++] = 0xffffffffu, ib[n++] = 0xffffffffu;
                ib[n++] = GPU_PKT(0x76, 1), ib[n++] = 0x229, ib[n++] = 0xffffffffu;
        }
        if (gfx >= 10u)
                ib[n++] = GPU_PKT(0x76, 1), ib[n++] = 0x228, ib[n++] = 0; // COMPUTE_PGM_RSRC3 (GFX10+ only)
        ib[n++] = GPU_PKT(0x76, 6), ib[n++] = 0x240, ib[n++] = 0, ib[n++] = 0, ib[n++] = 0, ib[n++] = 0, ib[n++] = (unsigned int)args, ib[n++] = (unsigned int)(args >> 32);
        ib[n++] = GPU_PKT(0x76, 3), ib[n++] = 0x207, ib[n++] = 64, ib[n++] = 1, ib[n++] = 1;
        ib[n++] = GPU_PKT(0x15, 3), ib[n++] = job->columns, ib[n++] = job->rows, ib[n++] = 1, ib[n++] = gfx == 6u ? 0x05u : 0x45u;
        _Static_assert(sizeof(ib) <= GPU_AMD_PACKETS, "the packets end where the next slot's begin (GPU_AMD_PACKETS)");
        // The job names the buffers it may touch besides the main one (the scanout's and the pages of windows, which are userptr buffers): the kernel pins and rebinds
        // the pages of those at the submission, which it does not for a buffer nobody named when the pages under it have moved.
        p64 boin[3] = {0, named | 8ull << 32, (positive)bos}, ibc[4] = {0, job->packets, ((p64)n * 4u) | 1ull << 32, 0}, chunk[4] = {1 | 8ull << 32, (positive)ibc, 6 | 6ull << 32, (positive)boin},
            list[2] = {(positive)chunk, (positive)(chunk + 2)}, cs[3] = {gpu.ctx, named ? 2 : 1, (positive)list}; // the main buffer is valid for good (it needs no naming); a job that touches no other buffer names none
        unsigned int error = 0;
        b32 taken = false;

        memory_copy(job->packets_at, ib, (positive)n * 4u);
        GPU_SUBMIT_FAKE(error, taken, cs[0]); // a check's fake submit answers an error before the call is made, or takes the job and gives its sequence (the call is then not made)
        if (!taken && error == 0)
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
        unsigned int e, eintr = 0;

        do
                memory_copy(asked, wait, sizeof(asked)), e = gpu_call(3u, 9, 32, asked);
        while (e == 4 && ++eintr < 32 && (!wait[1] || clock_monotonic_nanoseconds() < wait[1]));
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

// AMDGPU_FENCE_TO_HANDLE (0x14) with AMDGPU_FENCE_TO_HANDLE_GET_SYNC_FILE_FD (2): union drm_amdgpu_fence_to_handle, its in half a struct drm_amdgpu_fence (the context,
// the compute ring: ip type 1, instance 0, ring 0, the sequence) and what is asked; the kernel writes the descriptor over the first word. A sequence the ring has done
// long ago is answered with a fence already signalled, one past the last submitted is EINVAL.
static unsigned int gpu_amd_fence(positive sequence, int *fd)
{
        unsigned int in[8] = {gpu.ctx, 1, 0, 0, (unsigned int)sequence, (unsigned int)(sequence >> 32), 2, 0};
        unsigned int e = gpu_call(3u, 0x14, 32, in);

        *fd = e ? -1 : (int)in[0];
        return e;
}

/*
        AMDGPU_CTX_OP_SET_STABLE_PSTATE (6; 5 is GET) on a context made for it alone (never one a job runs on): the flags are the level, AMDGPU_CTX_STABLE_PSTATE_NONE 0
        (the driver's own choice, "auto"), STANDARD 1, MIN_SCLK 2, MIN_MCLK 3, PEAK 4 (power_dpm_force_performance_level's profile_* levels: each ungates the
        graphics block, so it is not powered off between jobs, GFXOFF, and holds its clocks). The kernel holds the level for that context alone (one context of
        the whole device at a time: another's is EBUSY) and puts the level back as it found it when the context is freed, which closing the descriptor does: a
        compositor that dies leaves no clock held.
*/
static unsigned int gpu_amd_clocks(int fd, unsigned int level, unsigned int *ctx)
{
        unsigned int made[4] = {1, 0, 0, 0}, in[4] = {6, level, 0, 0}, e = *ctx ? 0 : gpu_raw(fd, GPU_IO(3u, 2, 16), made); // ALLOC_CTX, then SET_STABLE_PSTATE

        if (e)
                return e;
        if (!*ctx)
                *ctx = made[0];
        in[2] = *ctx;
        return gpu_raw(fd, GPU_IO(3u, 2, 16), in);
}

static const struct gpu_backend gpu_amd = {
    .driver = "amdgpu", .identify = gpu_amd_identify, .buffer = gpu_amd_buffer, .context = gpu_amd_context, .user = gpu_amd_user, .bind = gpu_amd_bind,
    .unbind = gpu_amd_unbind, .submit = gpu_amd_submit, .wait = gpu_amd_wait, .fault = gpu_amd_fault, .fence = gpu_amd_fence, .set = gpu_amd_set,
    .classify = gpu_amd_classify, .window = gpu_amd_window, .describe = gpu_amd_describe, .lanes = 64, .packets = GPU_AMD_PACKETS, .clocks = gpu_amd_clocks};

/*
        i915: Ivy Bridge (2012) to Raptor Lake, graphics versions 7 to 12, on the render node. Every request is the uAPI of include/uapi/drm/i915_drm.h, the
        numbers and the sizes of its records held to it below (and to intel-probe, which ran the same requests on Ivy Bridge, Haswell and a Coffee Lake), and
        none of libdrm or Mesa is under it:
          - the card's addresses are ours: every buffer a job names is in its object list pinned (EXEC_OBJECT_PINNED) at the address the table gave it, with
            NO_RELOC and no relocation anywhere, so bind and unbind only keep the table of bound ranges (by handle) the submit reads; no GGTT pin, no VM_BIND;
          - a job is one EXECBUFFER2 on the render ring of our context (its id in rsvd1), the batch the last object: the card's own buffer, the batch at its
            slot's packets (batch_start_offset), its words made by the chip's kernel set (struct gpu_set's batch). No chip has a kernel set yet, so no i915
            card is picked (GPU_REQ_FAMILY), and a submit without one is refused as our own job (EOPNOTSUPP) before the kernel is asked;
          - the wait is GEM_WAIT on the card's own buffer, which every job's batch is in: a timeout of what is left to the deadline, 0 a look that does not
            wait, ETIME still running (i915_gem_wait.c: "the wait ioctl with a timeout of 0 reimplements the busy ioctl"; -ETIME "still busy after timeout");
            where the kernel has EXEC_FENCE (I915_PARAM_HAS_EXEC_FENCE) each job asks for its sync_file (FENCE_OUT, upper half of rsvd2, by the _WR ioctl so
            that it is written back), which says one job's end where GEM_WAIT says all of them, and is the fence call's descriptor;
          - a job seen done is held to the context's lost batches (GET_RESET_STATS: batch_active and batch_pending): a reset that took one of ours is a wait
            that failed, never a job done, and the context is made not recoverable (I915_CONTEXT_PARAM_RECOVERABLE 0), so that after a reset it is banned
            and its next submit answers EIO (a card lost: started again) instead of running on from a default state;
          - userptr is synchronised (UNSYNCHRONIZED is refused by the kernel), read-only (I915_USERPTR_READ_ONLY) only where the chip's address space has
            read-only entries: gen8_ppgtt.c sets has_read_only for graphics 8 and 9 and not 11 and 12, and Ivy Bridge and Haswell answer ENODEV (seen on
            both); there a map to read is refused (EPERM), as amdgpu's of a page the compositor maps read-only is, and goes by the dma-buf route or to the
            processor, so the card is never given a window's picture to write through userptr;
          - i915 has no query of the address space's last fault: the fault call answers zeros, and the kernel's log is what says a hang.
*/
#define GPU_I915_GETPARAM GPU_IO(3u, 0x06, 16)
#define GPU_I915_GEM_CREATE GPU_IO(3u, 0x1b, 16)
#define GPU_I915_GEM_MMAP_OFFSET GPU_IO(3u, 0x24, 32)
#define GPU_I915_EXECBUFFER2 GPU_IO(1u, 0x29, 64)
#define GPU_I915_EXECBUFFER2_WR GPU_IO(3u, 0x29, 64)
#define GPU_I915_GEM_WAIT GPU_IO(3u, 0x2c, 16)
#define GPU_I915_CONTEXT_CREATE GPU_IO(3u, 0x2d, 8)
#define GPU_I915_CONTEXT_DESTROY GPU_IO(1u, 0x2e, 8)
#define GPU_I915_RESET_STATS GPU_IO(3u, 0x32, 24)
#define GPU_I915_GEM_USERPTR GPU_IO(3u, 0x33, 24)
#define GPU_I915_CONTEXT_GETPARAM GPU_IO(3u, 0x34, 24)
#define GPU_I915_CONTEXT_SETPARAM GPU_IO(3u, 0x35, 24)
_Static_assert(GPU_I915_GETPARAM == 0xc0106446u && GPU_I915_GEM_CREATE == 0xc010645bu && GPU_I915_GEM_MMAP_OFFSET == 0xc0206464u, "i915_drm.h: GETPARAM, GEM_CREATE, MMAP_OFFSET");
_Static_assert(GPU_I915_EXECBUFFER2 == 0x40406469u && GPU_I915_EXECBUFFER2_WR == 0xc0406469u && GPU_I915_GEM_WAIT == 0xc010646cu, "i915_drm.h: EXECBUFFER2, _WR, GEM_WAIT");
_Static_assert(GPU_I915_CONTEXT_CREATE == 0xc008646du && GPU_I915_CONTEXT_DESTROY == 0x4008646eu && GPU_I915_RESET_STATS == 0xc0186472u, "i915_drm.h: contexts, reset stats");
_Static_assert(GPU_I915_GEM_USERPTR == 0xc0186473u && GPU_I915_CONTEXT_GETPARAM == 0xc0186474u && GPU_I915_CONTEXT_SETPARAM == 0xc0186475u, "i915_drm.h: USERPTR, context params");
struct gpu_i915_param { int param, pad; p64 value; };                                       // drm_i915_getparam (int, then int *)
struct gpu_i915_create { p64 size; unsigned int handle, pad; };                             // drm_i915_gem_create
struct gpu_i915_mmap { unsigned int handle, pad; p64 offset, flags, extensions; };            // drm_i915_gem_mmap_offset
struct gpu_i915_object { unsigned int handle, relocation_count; p64 relocs_ptr, alignment, offset, flags, rsvd1, rsvd2; }; // drm_i915_gem_exec_object2
struct gpu_i915_exec { p64 buffers_ptr; unsigned int buffer_count, batch_start_offset, batch_len, dr1, dr4, num_cliprects; p64 cliprects_ptr, flags, rsvd1, rsvd2; };
struct gpu_i915_wait { unsigned int handle, flags; long long timeout_ns; };                 // drm_i915_gem_wait
struct gpu_i915_context { unsigned int id, pad; };                                          // drm_i915_gem_context_create and _destroy
struct gpu_i915_context_param { unsigned int id, size; p64 param, value; };                 // drm_i915_gem_context_param
struct gpu_i915_reset { unsigned int id, flags, resets, active, pending, pad; };            // drm_i915_reset_stats
struct gpu_i915_userptr { p64 from, bytes; unsigned int flags, handle; };                   // drm_i915_gem_userptr
_Static_assert(sizeof(struct gpu_i915_param) == 16 && sizeof(struct gpu_i915_create) == 16 && sizeof(struct gpu_i915_mmap) == 32 && sizeof(struct gpu_i915_object) == 56 &&
                   sizeof(struct gpu_i915_exec) == 64 && sizeof(struct gpu_i915_wait) == 16 && sizeof(struct gpu_i915_context) == 8 &&
                   sizeof(struct gpu_i915_context_param) == 24 && sizeof(struct gpu_i915_reset) == 24 && sizeof(struct gpu_i915_userptr) == 24,
               "the i915 records are i915_drm.h's sizes");
#define GPU_I915_PINNED 16ull     // EXEC_OBJECT_PINNED
#define GPU_I915_WIDE 8ull        // EXEC_OBJECT_SUPPORTS_48B_ADDRESS: an object may be placed past 4 GiB
#define GPU_I915_WRITTEN 4ull     // EXEC_OBJECT_WRITE
#define GPU_I915_RUN 0x801ull     // I915_EXEC_RENDER | I915_EXEC_NO_RELOC
#define GPU_I915_FENCE_OUT (1ull << 17)
#define GPU_I915_JOBS 4           // the jobs whose sync_file is kept (two may be pending: gpu.pending and gpu_before)
#define GPU_I915_STATE 2048u     // a slot's state: the copy's and fill's binding table, surfaces, descriptor and CURBE in its first 512, the scene's table from 512
#define GPU_I915_HANDLES (GPU_IMPORTS_MOST + 64 + 1024) // the table of bound ranges, by handle (the kernel gives the lowest free one: as many as the buffers held)

// Forward declaration for the Intel kernel set (defined below gpu_i915_gen9_batch): Gen9's words, patched at the start for Gen7, Gen7.5, Gen8 and Gen11.
static const struct gpu_set gpu_i915_gen9_set;
// Intel i915 kernel sets by family range: Gen7/7.5 (Ivy Bridge/Valleyview/Haswell), Gen8 (Broadwell/Cherry View),
// Gen9/Gen9LP (Skylake through Comet Lake, Broxton/Apollo/Gemini Lake), and Gen11 (Ice Lake, Elkhart Lake, Jasper Lake)
// all active via deterministic EU binary patching verified against intel-probe kern7/kern8/kern9/kern11 and bit-exact
// gpu_test_hash() at startup.
static const struct { unsigned int from, to; const struct gpu_set *set; } gpu_i915_sets[] = {
    {700, 750, &gpu_i915_gen9_set}, {800, 801, &gpu_i915_gen9_set}, {900, 901, &gpu_i915_gen9_set}, {1100, 1101, &gpu_i915_gen9_set}, {0, 0, 0}};

/*
        The chips, by the PCI device id CHIPSET_ID / XE_DEVICE_QUERY answers (include/drm/intel/pciids.h): family is the
        graphics version times 100, with a variant in the units where the chip differs in what the path stands on (no LLC,
        discrete LMEM, a smaller address space). A device in none of them is family 0 (older than Ivy Bridge or unknown).
*/
static const unsigned short gpu_i915_ivb[] = {0x0156, 0x0166, 0x0152, 0x015A, 0x015E, 0x0162, 0x016A, 0x016E};
static const unsigned short gpu_i915_vlv[] = {0x0155, 0x0157, 0x0F30, 0x0F31, 0x0F32, 0x0F33};
static const unsigned short gpu_i915_hsw[] = {
    0x0A02, 0x0A06, 0x0A0A, 0x0A0B, 0x0A0E, 0x0402, 0x0406, 0x040A, 0x040B, 0x040E, 0x0C02, 0x0C06, 0x0C0A, 0x0C0B, 0x0C0E, 0x0D02, 0x0D06, 0x0D0A, 0x0D0B, 0x0D0E,
    0x0A12, 0x0A16, 0x0A1A, 0x0A1B, 0x0A1E, 0x0412, 0x0416, 0x041A, 0x041B, 0x041E, 0x0C12, 0x0C16, 0x0C1A, 0x0C1B, 0x0C1E, 0x0D12, 0x0D16, 0x0D1A, 0x0D1B, 0x0D1E,
    0x0A22, 0x0A26, 0x0A2A, 0x0A2B, 0x0A2E, 0x0422, 0x0426, 0x042A, 0x042B, 0x042E, 0x0C22, 0x0C26, 0x0C2A, 0x0C2B, 0x0C2E, 0x0D22, 0x0D26, 0x0D2A, 0x0D2B, 0x0D2E};
static const unsigned short gpu_i915_bdw[] = {0x1606, 0x160B, 0x160E, 0x1602, 0x160A, 0x160D, 0x1616, 0x161B, 0x161E, 0x1612, 0x161A, 0x161D,
                                              0x1626, 0x162B, 0x162E, 0x1622, 0x162A, 0x162D, 0x1636, 0x163B, 0x163E, 0x1632, 0x163A, 0x163D};
static const unsigned short gpu_i915_chv[] = {0x22B0, 0x22B1, 0x22B2, 0x22B3};
static const unsigned short gpu_i915_gen9[] = {
    0x1906, 0x1913, 0x190E, 0x1915, 0x1902, 0x190A, 0x190B, 0x1917, 0x1916, 0x1921, 0x191E, 0x1912, 0x191A, 0x191B, 0x191D, 0x1923, 0x1926, 0x1927, 0x192A,
    0x192B, 0x192D, 0x1932, 0x193A, 0x193B, 0x193D, 0x5906, 0x5913, 0x590E, 0x5915, 0x5902, 0x5908, 0x590A, 0x590B, 0x5916, 0x5921, 0x591E, 0x5912, 0x5917,
    0x591A, 0x591B, 0x591D, 0x5926, 0x5923, 0x5927, 0x593B, 0x591C, 0x87C0, 0x3E90, 0x3E93, 0x3E99, 0x3E91, 0x3E92, 0x3E95, 0x3E96, 0x3E98, 0x3E9A, 0x3E9C, 0x3E94,
    0x3E9B, 0x3EA9, 0x3EA5, 0x3EA6, 0x3EA7, 0x3EA8, 0x3EA1, 0x3EA4, 0x3EA0, 0x3EA3, 0x3EA2, 0x87CA, 0x9BA0, 0x9BA2, 0x9BA4, 0x9BA5, 0x9BA8, 0x9BC0, 0x9BC2,
    0x9BC4, 0x9BC5, 0x9BC6, 0x9BC8, 0x9BE6, 0x9BF6, 0x9B21, 0x9BAA, 0x9BAC, 0x9B41, 0x9BCA, 0x9BCC};
static const unsigned short gpu_i915_gen9lp[] = {0x0A84, 0x1A84, 0x1A85, 0x5A84, 0x5A85, 0x3184, 0x3185};
static const unsigned short gpu_i915_icl[] = {0x8A50, 0x8A52, 0x8A53, 0x8A54, 0x8A56, 0x8A57, 0x8A58, 0x8A59, 0x8A5A, 0x8A5B, 0x8A5C, 0x8A70, 0x8A71, 0x8A51, 0x8A5D};
static const unsigned short gpu_i915_ehl[] = {0x4541, 0x4551, 0x4555, 0x4557, 0x4570, 0x4571, 0x4E51, 0x4E55, 0x4E57, 0x4E61, 0x4E71};
static const unsigned short gpu_i915_gen12[] = {
    0x9A60, 0x9A68, 0x9A70, 0x9A40, 0x9A49, 0x9A59, 0x9A78, 0x9AC0, 0x9AC9, 0x9AD9, 0x9AF8, 0x4C80, 0x4C8A, 0x4C8B, 0x4C8C, 0x4C90, 0x4C9A, 0x4680, 0x4682,
    0x4688, 0x468A, 0x468B, 0x4690, 0x4692, 0x4693, 0xA780, 0xA781, 0xA782, 0xA783, 0xA788, 0xA789, 0xA78A, 0xA78B, 0x46A0, 0x46A1, 0x46A2, 0x46A3, 0x46A6,
    0x46A8, 0x46AA, 0x462A, 0x4626, 0x4628, 0x46B0, 0x46B1, 0x46B2, 0x46B3, 0x46C0, 0x46C1, 0x46C2, 0x46C3, 0x46D0, 0x46D1, 0x46D2, 0x46D3, 0x46D4, 0xA721,
    0xA7A1, 0xA7A9, 0xA7AC, 0xA7AD, 0xA720, 0xA7A0, 0xA7A8, 0xA7AA, 0xA7AB};
static const unsigned short gpu_i915_dg1[] = {0x4905, 0x4906, 0x4907, 0x4908, 0x4909};
static const unsigned short gpu_i915_dg2[] = {
    0x5690, 0x5691, 0x5692, 0x5693, 0x5694, 0x5695, 0x5696, 0x5697, 0x5698, 0x56A0, 0x56A1, 0x56A2, 0x56A3, 0x56A4, 0x56A5, 0x56A6,
    0x56B0, 0x56B1, 0x56B2, 0x56B3, 0x56BA, 0x56BB, 0x56BC, 0x56BD, 0x56BE, 0x56BF, 0x56C0, 0x56C1, 0x56C2};
static const unsigned short gpu_i915_mtl[] = {0x7D40, 0x7D45, 0x7D55, 0x7D60, 0x7DD5};
static const unsigned short gpu_i915_arl[] = {0x7D41, 0x7D51, 0x7D67, 0x7DD1, 0xB640};
static const unsigned short gpu_i915_lnl[] = {0x6420, 0x64A0, 0x64B0};
static const unsigned short gpu_i915_bmg[] = {0xE202, 0xE209, 0xE20B, 0xE20C, 0xE20D, 0xE210, 0xE211, 0xE212, 0xE215, 0xE216, 0xE220, 0xE221, 0xE222, 0xE223};
static const unsigned short gpu_i915_ptl[] = {0xB080, 0xB081, 0xB082, 0xB083, 0xB084, 0xB085, 0xB086, 0xB087, 0xB08F, 0xB090, 0xB0A0, 0xB0B0};
#define GPU_I915_CHIPS(X) X(gpu_i915_ivb, 700, "Gen7 Ivy Bridge") X(gpu_i915_vlv, 701, "Gen7 Valleyview") X(gpu_i915_hsw, 750, "Gen7.5 Haswell") \
        X(gpu_i915_bdw, 800, "Gen8 Broadwell") X(gpu_i915_chv, 801, "Gen8 Cherry View") X(gpu_i915_gen9, 900, "Gen9 Skylake to Comet Lake")       \
        X(gpu_i915_gen9lp, 901, "Gen9 Broxton or Gemini Lake") X(gpu_i915_icl, 1100, "Gen11 Ice Lake") X(gpu_i915_ehl, 1101, "Gen11 Elkhart Lake or Jasper Lake") \
        X(gpu_i915_gen12, 1200, "Gen12 Tiger Lake to Raptor Lake") X(gpu_i915_dg1, 1201, "Gen12 DG1") X(gpu_i915_dg2, 1255, "Xe-HPG Alchemist / Arc") \
        X(gpu_i915_mtl, 1270, "Xe-LPG Meteor Lake") X(gpu_i915_arl, 1274, "Xe-LPG+ Arrow Lake") X(gpu_i915_lnl, 2000, "Xe2-LPG Lunar Lake") \
        X(gpu_i915_bmg, 2001, "Xe2-HPG Battlemage") X(gpu_i915_ptl, 3000, "Xe3 Panther Lake")
static unsigned int gpu_i915_family_of(unsigned int device)
{
#define GPU_I915_LOOK(table, family, words) \
        for (unsigned int i = 0; i < sizeof(table) / sizeof(table[0]); i++) \
                if (table[i] == device) \
                        return family;
        GPU_I915_CHIPS(GPU_I915_LOOK)
#undef GPU_I915_LOOK
        return 0;
}

// What the last identify found of the node (the one picked is the last asked), and the state of the open card: its jobs, their sync_files (+1: 0 none), and the
// table of bound ranges with the object list of a submit, each a reservation of address space taken at its first use.
// A bound range: pinned at va (EXEC_OBJECT_PINNED), or floating: an import on a chip whose address space is the display's (aliasing PPGTT, Gen7 and
// Gen7.5), listed unpinned so the kernel keeps it where it is (eb_pin_vma takes a bound buffer's place as it is), its offset there presumed from the
// kernel's last answer (the exec list's offset written back), and every surface base inside it relocated (gpu_i915_reloc_note). va is then the
// range's name in the window, not its place on the card.
struct gpu_i915_bound { positive va, bytes, presumed; unsigned int written, seen_serial, seen_idx, floating; };
struct gpu_i915_reloc { unsigned int target, delta; p64 offset, presumed; unsigned int read, write; }; // drm_i915_gem_relocation_entry
typedef char gpu_i915_reloc_size[sizeof(struct gpu_i915_reloc) == 32 ? 1 : -1];
static struct
{
        b32 llc, softpin, no_reloc, fence, probe, read_only_refused;
        unsigned int ppgtt, main, lost, eu_total; // HAS_ALIASING_PPGTT (1 aliasing, 2 and up full); the card's own buffer's handle; lost batches; EU count
        positive gtt, sequence, done;   // the address space's size (I915_CONTEXT_PARAM_GTT_SIZE, 0 not said); the last job's sequence (from 1) and the last one seen done
        struct { positive sequence; int fence; } jobs[GPU_I915_JOBS];
        struct gpu_i915_bound *bound;
        struct gpu_i915_object *objects;
        struct gpu_i915_reloc *relocs;   // the relocations of the job being built (gpu_i915_reloc_note), on the card's own buffer's exec entry
        positive relocs_room;
        unsigned int relocs_count, relocs_failed;
} gpu_i915;

static int gpu_i915_param(int fd, int param)
{
        int value = 0;
        struct gpu_i915_param ask = {param, 0, (p64)&value};

        return gpu_raw(fd, GPU_I915_GETPARAM, &ask) ? 0 : value;
}

static unsigned int gpu_i915_identify(int fd, unsigned int *family, unsigned int *device)
{
        int id = 0;
        struct gpu_i915_param ask = {4, 0, (p64)&id}; // I915_PARAM_CHIPSET_ID
        struct gpu_i915_context_param gtt = {0, 0, 3, 0}; // I915_CONTEXT_PARAM_GTT_SIZE of the descriptor's default context
        unsigned int e = gpu_raw(fd, GPU_I915_GETPARAM, &ask);

        *device = e ? 0 : (unsigned int)id & 0xffff, *family = e ? 0 : gpu_i915_family_of((unsigned int)id & 0xffff);
        if (e)
                return e;
        gpu_i915.llc = gpu_i915_param(fd, 17) > 0, gpu_i915.ppgtt = (unsigned int)gpu_i915_param(fd, 18), gpu_i915.no_reloc = gpu_i915_param(fd, 25) > 0;
        gpu_i915.softpin = gpu_i915_param(fd, 37) > 0, gpu_i915.fence = gpu_i915_param(fd, 44) > 0, gpu_i915.probe = gpu_i915_param(fd, 56) > 0;
        gpu_i915.eu_total = (unsigned int)gpu_i915_param(fd, 34); // I915_PARAM_EU_TOTAL
        gpu_i915.gtt = gpu_raw(fd, GPU_I915_CONTEXT_GETPARAM, &gtt) ? 0 : gtt.value;
        gpu_i915.read_only_refused = 0;
        return 0;
}

// The address space: the kernel's size of it (GTT_SIZE), else the chip's (i915_pci.c: ppgtt_size 31 for graphics 7, 32 Cherry View, 36 Elkhart and Jasper Lake,
// 48 the rest). A space of the whole record's reach, not shared with the display, has amdgpu's window; a smaller one, or one shared with the display (aliasing
// PPGTT: Ivy Bridge, Valleyview and Haswell, where the display's buffers have their places in the same space), has the card's buffer at 256 MiB (the address
// intel-probe ran at on Ivy Bridge and Haswell, where 8 MiB was the display's: ENOSPC) and the maps from 1 GiB to its top, the last page left out.
static unsigned int gpu_i915_window(unsigned int family, positive *va, positive *maps, positive *top)
{
        positive space = gpu_i915.gtt ? gpu_i915.gtt
                         : family < 800 ? 1ull << 31 : family == 801 ? 1ull << 32 : family == 1101 ? 1ull << 36 : 1ull << 48;
        b32 shared = gpu_i915.ppgtt <= 1 || family < 800;

        if (space >= GPU_VA_RECORD && !shared)
                return *va = GPU_AMD_VA, *maps = GPU_AMD_VA_MAPS, *top = GPU_VA_RECORD, 0;
        if (space < (1ull << 30) + (256ull << 20))
                return 28;
        return *va = 0x10000000ull, *maps = 1ull << 30, *top = (space > GPU_VA_RECORD ? GPU_VA_RECORD : space - 4096) & ~4095ull, 0;
}

static const struct gpu_set *gpu_i915_set(unsigned int family)
{
        if (!gpu_i915.softpin || !gpu_i915.no_reloc)
                return 0;
        for (unsigned int i = 0; gpu_i915_sets[i].set; i++)
                if (family >= gpu_i915_sets[i].from && family <= gpu_i915_sets[i].to)
                        return gpu_i915_sets[i].set;
        return 0;
}

static unsigned int gpu_i915_describe(unsigned int family, unsigned int device, char *into, unsigned int at, unsigned int room)
{
        const char *words = "a chip not in the tables: older than Ivy Bridge, or of the xe era";

#define GPU_I915_NAME(table, f, name) if (family == (f)) words = name;
        GPU_I915_CHIPS(GPU_I915_NAME)
#undef GPU_I915_NAME
        at = gpu_text_put(into, at, room, "i915 "), at = gpu_text_put(into, at, room, words);
        at = gpu_text_put(into, at, room, ", device 0x");
        return gpu_text_num(into, at, room, device, 16);
}

static enum gpu_err gpu_i915_classify(unsigned int error)
{
        return error == 11 || error == 4 ? GPU_ERR_AGAIN : error == 5 || error == 19 ? GPU_ERR_LOST
               : error == 28 ? GPU_ERR_PLACE : error == 22 || error == 2 || error == 95 ? GPU_ERR_OURS : GPU_ERR_REFUSED;
}

// The reservations, taken once: the table of bound ranges and room for the object list of a submit of as many objects. False when the memory is not there.
static b32 gpu_i915_room(void)
{
        if (!gpu_i915.bound && !(gpu_i915.bound = (struct gpu_i915_bound address_to)gpu_mmap_reserve((positive)GPU_I915_HANDLES * sizeof(struct gpu_i915_bound), 3)))
                return false;
        if (!gpu_i915.objects && !(gpu_i915.objects = (struct gpu_i915_object address_to)gpu_mmap_reserve((positive)GPU_I915_HANDLES * sizeof(struct gpu_i915_object), 3)))
                return false;
        return true;
}

static unsigned int gpu_i915_bind(unsigned int handle, positive va, positive bytes, b32 written)
{
        positive whole;

        if (!gpu_i915_room())
                return 12;
        if (!handle || handle >= GPU_I915_HANDLES)
                return 28;
        // A range the window does not hold, or not of whole pages, would be refused at the submit (EINVAL: the kernel takes a pinned offset page-aligned and canonical).
        if ((va & 4095) || !bytes || bytes > GPU_VA_END || va < GPU_VA || va > GPU_VA_END)
                return 22;
        whole = (bytes + 4095) & ~4095ull;
        if (!whole || whole > GPU_VA_END - va)
                return 22;
        gpu_i915.bound[handle].va = va, gpu_i915.bound[handle].bytes = whole, gpu_i915.bound[handle].written = written ? 1 : 0;
        gpu_i915.bound[handle].floating = 0, gpu_i915.bound[handle].presumed = 0;
        return 0;
}

static unsigned int gpu_i915_unbind(unsigned int handle, positive va, positive bytes)
{
        (void)bytes;
        if (!gpu_i915.bound || !handle || handle >= GPU_I915_HANDLES || !gpu_i915.bound[handle].bytes || gpu_i915.bound[handle].va != va)
                return 2;
        memory_zero(&gpu_i915.bound[handle], sizeof(gpu_i915.bound[handle]));
        return 0;
}

// The card's own buffer: made (GEM_CREATE), its map's offset (MMAP_OFFSET: write-back where the chip shares the last-level cache with the processor, LLC,
// write-combined where it does not: Valleyview, Cherry View, Broxton, Gemini Lake), and its place in the table at va, written.
static unsigned int gpu_i915_buffer(positive size, positive va, unsigned int *handle, p64 *offset)
{
        struct gpu_i915_create made = {size, 0, 0};
        struct gpu_i915_mmap map = {0, 0, 0, 0, 0};
        unsigned int e = gpu_call(3u, 0x1b, 16, &made);

        if (e)
                return e;
        map.handle = made.handle, map.flags = gpu_i915.llc ? 2 : 1; // I915_MMAP_OFFSET_WB, _WC
        e = gpu_call(3u, 0x24, 32, &map);
        if (e == 22) // DG1 / DG2 / MTL on i915 require I915_MMAP_OFFSET_FIXED (4)
                map.flags = 4, e = gpu_call(3u, 0x24, 32, &map);
        if (e != 0 || (e = gpu_i915_bind(made.handle, va, size, true)) != 0)
        {
                p64 close[1] = {made.handle};

                GPU_CONTROL(gpu.fd, GPU_GEM_CLOSE, close);
                return e;
        }
        *handle = made.handle, gpu_i915.main = made.handle, *offset = map.offset;
        return 0;
}

// A context's lost batches (GET_RESET_STATS: the ones lost while running, guilty, and while waiting), 0 when the kernel does not say.
static unsigned int gpu_i915_lost(unsigned int context)
{
        struct gpu_i915_reset stats = {context, 0, 0, 0, 0, 0};

        return gpu_call(3u, 0x32, 24, &stats) ? 0 : stats.active + stats.pending;
}

// The jobs' sync_files closed, every job up to the last submitted taken as done (a context let go of: its jobs are over for us, as amdgpu's are).
static void gpu_i915_forget(void)
{
        for (unsigned int i = 0; i < GPU_I915_JOBS; i++)
        {
                if (gpu_i915.jobs[i].fence)
                        system_close(gpu_i915.jobs[i].fence - 1);
                gpu_i915.jobs[i].fence = 0, gpu_i915.jobs[i].sequence = 0;
        }
        gpu_i915.done = gpu_i915.sequence;
}

static unsigned int gpu_i915_context(unsigned int *made, unsigned int before)
{
        struct gpu_i915_context ctx = {0, 0}, old = {before, 0};
        struct gpu_i915_context_param unrecoverable = {0, 0, 8, 0}; // I915_CONTEXT_PARAM_RECOVERABLE, false
        unsigned int e = gpu_call(3u, 0x2d, 8, &ctx);

        if (e)
                return e;
        // A kernel without the parameter keeps the context recoverable: a reset then still shows, as a lost batch at the job's end (gpu_i915_ended).
        unrecoverable.id = ctx.id;
        gpu_call(3u, 0x35, 24, &unrecoverable);
        if (before)
                gpu_call(1u, 0x2e, 8, &old), gpu_i915_forget();
        gpu_i915.lost = gpu_i915_lost(ctx.id);
        *made = ctx.id;
        return 0;
}

/*
        The compositor's own private memory (mem_buf_grow's anonymous maps, said here by gpu_own as they are made and let go of), as ranges: on a chip
        with no read-only entries a map to read inside one is made with a written userptr (gpu_i915_user), since no other program can see those pages
        and the card writes none of them (a job writes only its destination). A client's pool, or any memory not said here, stays a map to read, refused
        there. The table grows as it is filled (gpu_buf_grow) and is the loop's alone, as mem_buf_grow is.
*/
static positive *gpu_own_ranges;
static positive gpu_own_room;
static unsigned int gpu_own_count;
static void gpu_own(const void *addr, positive bytes, b32 held)
{
        positive from = (positive)addr;

        if (!addr || !bytes)
                return;
        for (unsigned int i = 0; i < gpu_own_count; i++)
                if (gpu_own_ranges[2 * i] == from)
                {
                        if (held)
                                gpu_own_ranges[2 * i + 1] = from + bytes;
                        else
                                gpu_own_count--, gpu_own_ranges[2 * i] = gpu_own_ranges[2 * gpu_own_count], gpu_own_ranges[2 * i + 1] = gpu_own_ranges[2 * gpu_own_count + 1];
                        return;
                }
        if (!held)
                return;
        if (16ull * (gpu_own_count + 1) > gpu_own_room)
        {
                void *grown = gpu_own_ranges;

                if (!gpu_buf_grow(&grown, &gpu_own_room, 16ull * (gpu_own_count + 1) * 2, 16ull * gpu_own_count))
                        return; // not said: a map inside it is a map to read, as a client's is
                gpu_own_ranges = (positive *)grown;
        }
        gpu_own_ranges[2 * gpu_own_count] = from, gpu_own_ranges[2 * gpu_own_count + 1] = from + bytes, gpu_own_count++;
}

static b32 gpu_own_holds(positive from, positive to)
{
        for (unsigned int i = 0; i < gpu_own_count; i++)
                if (from >= gpu_own_ranges[2 * i] && to <= gpu_own_ranges[2 * i + 1])
                        return true;
        return false;
}

static unsigned int gpu_i915_user(positive from, positive bytes, unsigned int how, unsigned int *handle)
{
        struct gpu_i915_userptr up = {from, bytes, 0, 0};
        unsigned int e;

        *handle = 0;
        // A map to read that the kernel cannot make read-only is refused (EPERM), never made writable: the card would then write a client's pages through it.
        // I915_USERPTR_READ_ONLY is Gen8 and Gen9's alone (full PPGTT), so on Gen7/7.5 and Gen11+ such a map is refused at once, before the kernel is asked,
        // as on a Gen8/9 kernel that answers it ENODEV; the compositor's own memory then goes by the dma-buf route and a client's pool to the processor (gpu_map_as).
        if (how == GPU_USER_READ && !(gpu.family >= 800 && gpu.family < 1100))
                gpu_i915.read_only_refused = 1;
        if (how == GPU_USER_READ && gpu_i915.read_only_refused)
        {
                if (!gpu_own_holds(from, from + bytes))
                        return 1;
                how = GPU_USER_WRITTEN; // the compositor's own private pages (gpu_own): no other program sees them
        }
        if (how == GPU_USER_READ)
                up.flags |= 1; // I915_USERPTR_READ_ONLY
        if (how != GPU_USER_LATER && gpu_i915.probe)
                up.flags |= 2; // I915_USERPTR_PROBE: the range is looked at now (EFAULT), where without it the pages are first taken at a submit
        e = gpu_call(3u, 0x33, 24, &up);
        if (e == 19 && (up.flags & 1))
        {
                // Refused, and not retried writable (the kernel has no read-only entries), but for the compositor's own private pages (gpu_own).
                gpu_i915.read_only_refused = 1, e = 1;
                if (gpu_own_holds(from, from + bytes))
                        up.flags &= ~1u, e = gpu_call(3u, 0x33, 24, &up);
        }
        *handle = e ? 0 : up.handle;
        return e;
}

static unsigned int gpu_i915_submit(const struct gpu_job *job, p64 *bos, unsigned int named, positive *sequence)
{
        static unsigned int dedup_serial;
        const positive wide = GPU_VA_END > (1ull << 32) ? GPU_I915_WIDE : 0;
        struct gpu_i915_exec run = {0, 0, 0, 0, 0, 0, 0, 0, GPU_I915_RUN, 0, 0};
        unsigned int words, n = 0, e, slot;

        *sequence = 0;
        if (!++dedup_serial)
        {
                if (gpu_i915.bound)
                        for (unsigned int i = 0; i < GPU_I915_HANDLES; i++)
                                gpu_i915.bound[i].seen_serial = 0;
                dedup_serial = 1;
        }
        if (!gpu_set || !gpu_set->batch)
                return 95;
        gpu_i915.relocs_count = 0, gpu_i915.relocs_failed = 0;
        words = gpu_set->batch(job, job->packets_at, GPU_PACKETS / 4);
        if (!words)
                return 95;
        if (gpu_i915.relocs_failed)
                return 12;
        if ((words & 1) && words < GPU_PACKETS / 4) // the batch's length is of whole quadwords (a length that is not is EINVAL): an MI_NOOP ends it
                job->packets_at[words++] = 0;
        if (words > GPU_PACKETS / 4 || (words & 1) || !gpu_i915_room() || !gpu_i915.main)
                return 22;
        if (named >= GPU_I915_HANDLES)
                return 28;
        for (unsigned int k = 0; k < named; k++)
        {
                const unsigned int handle = (unsigned int)bos[k];

                if (handle >= GPU_I915_HANDLES || !gpu_i915.bound[handle].bytes)
                        return 2; // a buffer the table never bound: it has no address to be pinned at
                if (handle == gpu_i915.main)
                        continue;
                if (gpu_i915.bound[handle].seen_serial == dedup_serial)
                {
                        if (gpu_i915.bound[handle].written)
                                gpu_i915.objects[gpu_i915.bound[handle].seen_idx].flags |= GPU_I915_WRITTEN;
                        continue;
                }
                gpu_i915.bound[handle].seen_serial = dedup_serial, gpu_i915.bound[handle].seen_idx = n;
                // A floating import is listed where the kernel last said it is, unpinned: the kernel keeps it there (or anywhere, if it had moved) and
                // writes the relocated words of its surfaces (gpu_i915_reloc_note).
                gpu_i915.objects[n] = gpu_i915.bound[handle].floating
                                          ? (struct gpu_i915_object){.handle = handle, .offset = gpu_i915.bound[handle].presumed,
                                                                     .flags = gpu_i915.bound[handle].written ? GPU_I915_WRITTEN : 0}
                                          : (struct gpu_i915_object){.handle = handle, .offset = gpu_i915.bound[handle].va,
                                                                     .flags = GPU_I915_PINNED | wide | (gpu_i915.bound[handle].written ? GPU_I915_WRITTEN : 0)};
                n++;
        }
        // The card's own buffer holds the batch, and i915 refuses a batch asked written on every generation (eb_add_vma: "Attempting to use
        // self-modifying batch buffer", EINVAL), so it is never asked written: the job's sync_file and GEM_WAIT (which waits for every fence of the
        // buffer, reads too) are what the processor waits on before it reads what the card drew there.
        gpu_i915.objects[n++] = (struct gpu_i915_object){.handle = gpu_i915.main, .relocation_count = gpu_i915.relocs_count,
                                                         .relocs_ptr = gpu_i915.relocs_count ? (p64)gpu_i915.relocs : 0, .offset = GPU_VA, .flags = GPU_I915_PINNED | wide};
        run.buffers_ptr = (p64)gpu_i915.objects, run.buffer_count = n, run.batch_start_offset = (unsigned int)(job->packets - GPU_VA), run.batch_len = 4 * words;
        run.flags |= gpu_i915.fence ? GPU_I915_FENCE_OUT : 0, run.rsvd1 = gpu.ctx;
        if ((e = gpu_raw(gpu.fd, gpu_i915.fence ? GPU_I915_EXECBUFFER2_WR : GPU_I915_EXECBUFFER2, &run)) != 0)
                return e;
        // Where the kernel put each floating import (it writes the place back into the list when it differs from the one presumed): the next job's
        // words are written for it, and its relocations are passed over.
        for (unsigned int k = 0; k + 1 < n; k++)
                if (gpu_i915.bound[gpu_i915.objects[k].handle].floating)
                        gpu_i915.bound[gpu_i915.objects[k].handle].presumed = gpu_i915.objects[k].offset;
        *sequence = ++gpu_i915.sequence, slot = (unsigned int)(*sequence % GPU_I915_JOBS);
        if (gpu_i915.jobs[slot].fence)
                system_close(gpu_i915.jobs[slot].fence - 1);
        {
                int out_fd = (int)(run.rsvd2 >> 32);

                gpu_i915.jobs[slot].sequence = *sequence;
                gpu_i915.jobs[slot].fence = (gpu_i915.fence && out_fd >= 0) ? out_fd + 1 : 0;
        }
        return 0;
}

// Jobs up to sequence seen done: their sync_files let go of, and the context's lost batches looked at. A batch lost since the last look is a reset that took
// a job of ours (the job did not draw what it was given): -1, the wait failed; else 1.
static int gpu_i915_ended(positive sequence)
{
        if (sequence > gpu_i915.done)
                gpu_i915.done = sequence;
        for (unsigned int i = 0; i < GPU_I915_JOBS; i++)
                if (gpu_i915.jobs[i].sequence && gpu_i915.jobs[i].sequence <= gpu_i915.done)
                {
                        if (gpu_i915.jobs[i].fence)
                                system_close(gpu_i915.jobs[i].fence - 1);
                        gpu_i915.jobs[i].fence = 0, gpu_i915.jobs[i].sequence = 0;
                }
        if ((sequence & 63u) == 0)
        {
                unsigned int lost = gpu_i915_lost(gpu.ctx);
                if (lost != gpu_i915.lost)
                        return gpu_i915.lost = lost, -1;
        }
        return 1;
}

// A job waited for until a time: its sync_file polled where it has one (the one job's end), else GEM_WAIT on the card's own buffer (every job's end), one call
// to the deadline either way (what is left of it: 0, a look) and asked again only when a signal ended it.
static int gpu_i915_wait(positive sequence, positive until)
{
        const unsigned int slot = (unsigned int)(sequence % GPU_I915_JOBS);
        unsigned int tries = 0;

        for (;;)
        {
                positive now = clock_monotonic_nanoseconds(), left = until > now ? until - now : 0;
                unsigned int e;

                if (!sequence || sequence <= gpu_i915.done)
                        return 1;
                left = left > (1ull << 62) ? 1ull << 62 : left; // (a negative timeout is a wait with none)
                if (gpu_i915.jobs[slot].sequence == sequence && gpu_i915.jobs[slot].fence)
                {
                        struct { int fd; short events, returned; } watch = {gpu_i915.jobs[slot].fence - 1, 1, 0}; // POLLIN
                        positive wait[2] = {left / 1000000000ull, left % 1000000000ull};
                        long r = (long)system_call_5(syscall(ppoll), (positive)&watch, 1, (positive)wait, 0, 8);

                        if (r == -4)
                        {
                                if (!left || ++tries >= 16)
                                        return 0;
                                continue;
                        }
                        if (r < 0 || (watch.returned & 0x38)) // POLLERR, POLLHUP, POLLNVAL
                                return -1;
                        return r == 0 ? 0 : gpu_i915_ended(sequence);
                }
                {
                        struct gpu_i915_wait wait = {gpu_i915.main, 0, (long long)left};

                        e = gpu_call(3u, 0x2c, 16, &wait);
                        if (e == 4 || e == 11)
                        {
                                if (!left || ++tries >= 16)
                                        return 0;
                                if (e == 11)
                                {
                                        positive ts[2] = {0, 100000};
                                        system_call_2(syscall(nanosleep), (positive)ts, 0);
                                }
                                continue;
                        }
                        if (e == 62)
                                return 0;
                        return e ? -1 : gpu_i915_ended(gpu_i915.sequence);
                }
        }
}

static unsigned int gpu_i915_fault(struct gpu_vm_fault *fault)
{
        memory_zero(fault, sizeof(*fault));
        return 0;
}

// The job's sync_file, a descriptor of its own (F_DUPFD_CLOEXEC of the one the submit kept): EOPNOTSUPP where the kernel has no EXEC_FENCE (the caller waits on
// the sequence), EALREADY for a job seen done (nothing is left to poll), ENOENT for one this table does not hold.
static unsigned int gpu_i915_fence(positive sequence, int *fd)
{
        const unsigned int slot = (unsigned int)(sequence % GPU_I915_JOBS);
        long made;

        *fd = -1;
        if (!gpu_i915.fence)
                return 95;
        if (gpu_i915.jobs[slot].sequence != sequence || !gpu_i915.jobs[slot].fence)
                return sequence && sequence <= gpu_i915.done ? 114 : 2;
        made = (long)system_call_3(syscall(fcntl), (positive)(gpu_i915.jobs[slot].fence - 1), 1030, 3);
        if (made < 0)
                return (unsigned int)-made & 255;
        *fd = (int)made;
        return 0;
}

/*
        i915's clocks (struct gpu_backend's clocks): the card's least clock, its sysfs gt_min_freq_mhz (/sys/bus/pci/devices/BUS/drm/cardN), raised for a
        level and put back for 0. The card still sleeps (RC6) between jobs: the least clock is the one a job wakes at, and the one i915's autotuning (RPS)
        keeps it at under a desktop's load, a present's job busy a third of the time, under every threshold it has (cloudless-lamp, Haswell HD 4400: 200 MHz
        all through a terminal's scroll, its scene 8.9 ms a present; 1.8 ms at 1150 MHz). Level 4 (PEAK) is RP0, 1 (STANDARD) RP1, 2 and 3 RPn, in the
        order of AMD's stable levels. While it is raised the least clock found is kept in GPU_I915_MIN_KEPT, so a compositor that dies holding it has it put
        back at the next start (gpu_i915_clocks_restore), as the kernel puts AMD's back when the descriptor closes. The descriptor and context are unused.
*/
#define GPU_I915_MIN_KEPT "/run/moonwater/gpu-min-freq"
static b32 gpu_flag(const char *path);
static unsigned int gpu_i915_min_kept; // the least clock as it was found (MHz), 0 while none is raised

// The card's sysfs file name, in path: 0 when it is there. The device's primary node is its card (DRM numbers them below 256).
static int gpu_i915_sysfs(const char *name, char *path, unsigned int room)
{
        if (!gpu_req.bus[0])
                return -1;
        for (unsigned int n = 0; n < 256; n++)
        {
                unsigned int at = gpu_text_put(path, 0, room, "/sys/bus/pci/devices/");

                at = gpu_text_put(path, at, room, gpu_req.bus), at = gpu_text_put(path, at, room, "/drm/card");
                at = gpu_text_num(path, at, room, n, 10), at = gpu_text_put(path, at, room, "/"), at = gpu_text_put(path, at, room, name);
                if (at + 1 < room && gpu_flag(path))
                        return 0;
        }
        return -1;
}

// A file's decimal number (0 for none), and one written (0 written, else the errno).
static unsigned int gpu_i915_number(const char *path)
{
        char text[24] = {0};
        long fd = system_open_at(AT_FDCWD, (string_address)path, FILE_READ | O_CLOEXEC | O_NOFOLLOW);
        unsigned int value = 0;

        if (fd < 0)
                return 0;
        if ((long)system_call_3(syscall(read), (positive)fd, (positive)text, sizeof text - 1) > 0)
                for (unsigned int i = 0; text[i] >= '0' && text[i] <= '9' && value < 100000000u; i++)
                        value = value * 10 + (unsigned int)(text[i] - '0');
        system_close((int)fd);
        return value;
}

static unsigned int gpu_i915_number_put(const char *path, unsigned int value)
{
        char text[16];
        unsigned int n = gpu_text_num(text, 0, sizeof text - 1, value, 10);
        long fd = system_open_at_mode(AT_FDCWD, (string_address)path, FILE_WRITE | O_CLOEXEC | O_NOFOLLOW, 0600), w;

        if (fd < 0)
                return (unsigned int)-fd & 255;
        text[n++] = '\n';
        w = (long)system_call_3(syscall(write), (positive)fd, (positive)text, n);
        system_close((int)fd);
        return w == (long)n ? 0 : w < 0 ? (unsigned int)-w & 255 : 5;
}

static unsigned int gpu_i915_clocks(int fd, unsigned int level, unsigned int *ctx)
{
        char path[192], rp[192];
        unsigned int e, want;

        (void)fd, (void)ctx;
        if (gpu_i915_sysfs("gt_min_freq_mhz", path, sizeof path) != 0)
                return 38;
        if (!level)
        {
                if (!gpu_i915_min_kept)
                        return 0;
                if ((e = gpu_i915_number_put(path, gpu_i915_min_kept)) != 0)
                        return e;
                gpu_i915_min_kept = 0, system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)GPU_I915_MIN_KEPT, 0);
                return 0;
        }
        if (gpu_i915_sysfs(level == 4 ? "gt_RP0_freq_mhz" : level == 1 ? "gt_RP1_freq_mhz" : "gt_RPn_freq_mhz", rp, sizeof rp) != 0 || !(want = gpu_i915_number(rp)))
                return 38;
        if (!gpu_i915_min_kept)
        {
                if (!(gpu_i915_min_kept = gpu_i915_number(path)))
                        return 5;
                gpu_i915_number_put(GPU_I915_MIN_KEPT, gpu_i915_min_kept);
        }
        return gpu_i915_number_put(path, want);
}

// At a start: a least clock a compositor raised and did not put back (it died holding it) is put back, as GPU_I915_MIN_KEPT says it was.
static void gpu_i915_clocks_restore(void)
{
        char path[192];
        unsigned int was = gpu_i915_number(GPU_I915_MIN_KEPT);

        if (!was || gpu_i915_sysfs("gt_min_freq_mhz", path, sizeof path) != 0 || gpu_i915_number_put(path, was) != 0)
                return;
        gpu_i915_min_kept = 0, system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)GPU_I915_MIN_KEPT, 0);
}

static const struct gpu_backend gpu_i915_row = {
    .driver = "i915", .identify = gpu_i915_identify, .buffer = gpu_i915_buffer, .context = gpu_i915_context, .user = gpu_i915_user, .bind = gpu_i915_bind,
    .unbind = gpu_i915_unbind, .submit = gpu_i915_submit, .wait = gpu_i915_wait, .fault = gpu_i915_fault, .fence = gpu_i915_fence, .set = gpu_i915_set,
    .classify = gpu_i915_classify, .window = gpu_i915_window, .describe = gpu_i915_describe, .clocks = gpu_i915_clocks, .lanes = 16, .packets = 1024,
    .state = GPU_I915_STATE}; // a slot's binding tables, surface states, interface descriptor and CURBE (gpu_i915_emit_batch, gpu_i915_scene_batch)

static void gpu_close(unsigned int handle);
/*
        NVIDIA Nouveau identification backend ("nouveau", Kepler 2012 through Blackwell 2026): queries chipset ID via
        DRM_IOCTL_NOUVEAU_GETPARAM (NOUVEAU_GETPARAM_CHIPSET_ID = 11) for clean diagnostics and settles onto CPU compose.
*/
#define GPU_NOUVEAU_GETPARAM GPU_IO(3u, 0x00, 16) // DRM_IOCTL_NOUVEAU_GETPARAM (DRM_COMMAND_BASE + 0x00)
static unsigned int gpu_nouveau_identify(int fd, unsigned int *family, unsigned int *device)
{
        p64 req[2] = {11ull, 0}; // NOUVEAU_GETPARAM_CHIPSET_ID = 11
        unsigned int e = gpu_raw(fd, GPU_NOUVEAU_GETPARAM, req);

        *family = e ? 0 : (unsigned int)(req[1] & 0xfffu);
        *device = e ? 0 : (unsigned int)(req[1] & 0xffffu);
        return e;
}

static const struct gpu_set *gpu_nouveau_set(unsigned int family)
{
        (void)family;
        return 0;
}

static unsigned int gpu_nouveau_describe(unsigned int family, unsigned int device, char *into, unsigned int at, unsigned int room)
{
        const char *arch = "NVIDIA GPU";

        if (family >= 0x1b0)
                arch = "Blackwell";
        else if (family >= 0x190)
                arch = "Ada Lovelace";
        else if (family >= 0x170)
                arch = "Ampere";
        else if (family >= 0x160)
                arch = "Turing";
        else if (family >= 0x140)
                arch = "Volta";
        else if (family >= 0x130)
                arch = "Pascal";
        else if (family >= 0x110)
                arch = "Maxwell";
        else if (family >= 0x0e0)
                arch = "Kepler";
        at = gpu_text_put(into, at, room, "nouveau "), at = gpu_text_put(into, at, room, arch);
        at = gpu_text_put(into, at, room, " (NV"), at = gpu_text_num(into, at, room, device, 16);
        return gpu_text_put(into, at, room, ")");
}

static const struct gpu_backend gpu_nouveau_row = {
    .driver = "nouveau", .identify = gpu_nouveau_identify, .set = gpu_nouveau_set, .classify = gpu_i915_classify,
    .describe = gpu_nouveau_describe, .lanes = 32, .packets = 256, .state = 0};

// The drivers, in the order a node of each is taken when the display's own device is none of them.
static const struct gpu_backend *const gpu_backends[] = {&gpu_amd, &gpu_i915_row, &gpu_nouveau_row};
#define GPU_BACKENDS (sizeof(gpu_backends) / sizeof(gpu_backends[0]))
static const struct gpu_backend *gpu_backend = &gpu_amd; // the card's driver: the picked node's (amdgpu's until a start picks one: a check's fake card is amdgpu's)

// The generations the processor composes faster than the card, whose card path is off unless canvas gpu is said on (the compositor's compositor_gpu_slower):
// i915's Gen7 and Gen7.5, Ivy Bridge, Valleyview and Haswell. Their aliasing PPGTT has the card compose into a shadow that the present waits for
// (gpu_needs_shadow_output), and on a Haswell HD 4600 (0x0412) the card's present measured 21.6 to 44.9 ms (up to 180) where the processor's was 0.27 to 0.34 ms.
static b32 gpu_slower(const struct gpu_backend *backend, unsigned int family)
{
        return backend == &gpu_i915_row && family >= 700 && family < 800;
}

static const struct gpu_backend *gpu_backend_of(const char *name)
{
        for (unsigned int i = 0; i < GPU_BACKENDS; i++)
                if (string_equals((string_address)name, (string_address)gpu_backends[i]->driver))
                        return gpu_backends[i];
        return 0;
}

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
// The udmabuf device's descriptor, opened once per start (gpu_stop forgets it). File scope, not function-local: a check's fake open answers a number that is not a
// descriptor, and a cache that outlives the fake handed that number to the real-card rows (EBADF, 19 rows of CHECK_gpu_kit). Forgotten, not closed: a fake
// number must not be closed.
static int gpu_udmabuf_fd = -1;

static long gpu_dmabuf_make(int memfd, positive offset, positive size)
{
        int udmabuf_fd = gpu_udmabuf_fd;
        struct { unsigned int memfd, flags; unsigned long offset, size; } create = {(unsigned int)memfd, 1, offset, size}; // UDMABUF_FLAGS_CLOEXEC

        if (udmabuf_fd < 0)
        {
                long opened = GPU_UDMABUF_OPEN();
                if (opened < 0)
                        return opened;
                udmabuf_fd = gpu_udmabuf_fd = (int)opened;
        }
        return GPU_CONTROL(udmabuf_fd, 0x40187542u, &create); // _IOW('u', 0x42, struct udmabuf_create)
}

// A dma-buf imported into the render node: its handle there, or -errno. The descriptor is closed either way (the handle holds the buffer).
static long gpu_dmabuf_in(int dmabuf)
{
        struct { unsigned int handle, flags; int fd; } prime = {0, 0, dmabuf};
        long made = GPU_CONTROL(gpu.fd, 3u << 30 | 12u << 16 | 0x642eu, &prime); // DRM_IOCTL_PRIME_FD_TO_HANDLE

        system_close(dmabuf);
        return made < 0 ? made : (long)prime.handle;
}

static void gpu_say_kernel(void);
static b32 gpu_idle(void);
static unsigned int gpu_test_hash(void);
static b32 gpu_image_test(void);
static b32 gpu_nv12_test(void);
// The texture unit's read (op 8, gpu_image) drew its own test right at the last start (gpu_image_test): a client's tiled or compressed buffer may be offered
// and read only while this holds (gpu_image_takes). Not a step of the card's: a card that fails it composes as before, and its clients are told of linear alone.
static b32 gpu_image_ok;
static b32 gpu_nv12_ok, gpu_nv12_trying; // the card drew its NV12 test right at its start (gpu_nv12_test), and the test is drawing now

// The dma-buf route's own prerequisites (udmabuf, then PRIME), checked once the card is up: a card whose userptr works does not need them, and one whose userptr
// is refused is served by them (the fallback). A step that fails is kept as gpu_req.route with its error; the card still composes.
static void gpu_route_probe(void)
{
        // A memfd page made sealed for the udmabuf driver (F_SEAL_SHRINK), its dma-buf: the descriptor, or -errno.
        long memfd = system_call_2(syscall(memfd_create), (positive) "moonwater-gpu", 1 | 2), made = memfd; // MFD_CLOEXEC | MFD_ALLOW_SEALING
        enum gpu_req step = GPU_REQ_UDMABUF;
        b32 failed;
        unsigned int e;

        if (memfd >= 0)
                system_truncate_handle((int)memfd, 4096), system_call_3(syscall(fcntl), (positive)memfd, 1033, 2), made = gpu_dmabuf_make((int)memfd, 0, 4096),
                system_close((int)memfd); // F_ADD_SEALS, F_SEAL_SHRINK
        failed = made < 0, e = failed ? (unsigned int)-made & 255 : 0;
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
static unsigned int *gpu_bin_stage, *gpu_ops_stage;
static positive gpu_bin_room, gpu_ops_room;

static void gpu_maps_reset(void);
static void gpu_kmsg_drain(void);
static void gpu_bins_forget(void);

COLD static void gpu_stop(void)
{
        void *stage = gpu_bin_stage, *ostage = gpu_ops_stage;

        gpu_udmabuf_fd = -1; // forgotten, not closed (see gpu_dmabuf_make): the next start opens the device again

        gpu_maps_reset();
        gpu_shared.fd = -1, gpu_shared.at = 0, gpu_shared.bytes = 0, gpu_shared.pin = 0;
        gpu_buf_free(&stage, &gpu_bin_room);
        gpu_buf_free(&ostage, &gpu_ops_room);
        gpu_bin_stage = gpu_ops_stage = 0;
        if (gpu.at)
                system_call_2(syscall(munmap), (positive)gpu.at, gpu.size);
        if (gpu.fd >= 0)
                system_close(gpu.fd);
        gpu_i915_forget();
        gpu_i915.main = 0;
        if (gpu_i915.bound)
                memory_zero(gpu_i915.bound, (positive)GPU_I915_HANDLES * sizeof(struct gpu_i915_bound));
        memory_zero(&gpu, sizeof(gpu));
        gpu_image_ok = gpu_nv12_ok = 0;
        if (gpu_import_at_room)
                gpu_imports_room(0); // the import table's arrays pointed at their reservation again (its room kept, empty)
        gpu.fd = -1;
        gpu_ring_reset();
        gpu_bins_forget();
        gpu_epoch++;
}

// The first card that is one the kernels are for, with bytes of pixels behind the page of commands, and one context. Every prerequisite is a step (gpu_req):
// the node, the driver, the family, userptr, a dma-buf through udmabuf and PRIME, the buffer and context, the self-test's picture and the ring that drew it.
// False if any step is refused, the first kept in gpu_req; a card that another driver or family has is passed over, and not hung by code that is not its own.
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

// The render nodes as a number: each node that opens mixed in by its minor and its driver's name ("" for a node that does not say), so that the same nodes with
// the same drivers are the same number and a node come or gone, or a driver loaded in another's place, is another one. Never 0.
static positive gpu_node_mix(positive seen, unsigned int node, const char *name)
{
        seen = (seen ^ (node + 1)) * 1099511628211ull;
        while (*name)
                seen = (seen ^ (p8)*name++) * 1099511628211ull;
        return seen;
}

// The render nodes as they are now, as gpu_start's walk of them mixes them (gpu_node_mix): the late look's way to tell a settled start's nodes from new ones
// without starting the card (a version call a node, nothing of the card made).
static positive gpu_nodes_seen(void)
{
        char path[] = "/dev/dri/renderD128";
        positive seen = 14695981039346656037ull;

        for (unsigned int node = 0; node < 64; node++)
        {
                char name[32] = "";
                p64 ver[8] = {0, 0, sizeof(name) - 1, (p64)name};
                long fd;

                path[17] = (char)('0' + (28 + node) / 10), path[18] = (char)('0' + (28 + node) % 10);
                fd = system_open_at(AT_FDCWD, (string_address)path, 2 | O_CLOEXEC | O_NOFOLLOW);
                GPU_OPEN_FAKE(fd);
                if (fd < 0)
                        continue;
                if (gpu_raw((int)fd, 3u << 30 | 64u << 16 | 0x6400u, ver) != 0)
                        name[0] = 0;
                seen = gpu_node_mix(seen, node, name);
                system_close((int)fd);
        }
        return seen | 1;
}

// Whether a file the owner puts in /run/moonwater is there (looked for at each start).
static b32 gpu_flag(const char *path)
{
        long off = system_open_at(AT_FDCWD, (string_address)path, FILE_READ | O_CLOEXEC | O_NOFOLLOW);

        if (off >= 0)
                system_close((int)off);
        return off >= 0;
}

static void gpu_req_record(const char *path, const char *driver, unsigned int family, unsigned int device, const char *bus)
{
        memory_copy(gpu_req.node, path, 20);
        gpu_req_stat(path);
        if (driver)
        {
                memory_zero(gpu_req.driver, sizeof(gpu_req.driver)), memory_zero(gpu_req.name, sizeof(gpu_req.name));
                string_copy_bounded((string_address)gpu_req.driver, (string_address)driver, sizeof(gpu_req.driver));
                string_copy_bounded((string_address)gpu_req.name, (string_address)driver, sizeof(gpu_req.name));
        }
        if (bus)
                memory_copy(gpu_req.bus, bus, sizeof(gpu_req.bus));
        gpu_req.family = family, gpu_req.device = device;
}

static void gpu_srgb_upload(void)
{
        unsigned int address_to s2l = (unsigned int address_to)((p8 address_to)gpu.at + GPU_SRGB_S2L_AT);

        for (unsigned int i = 0; i < 256; i++)
                s2l[i] = canvas_srgb_to_lin[i];
        memory_copy((p8 address_to)gpu.at + GPU_SRGB_L2S_AT, canvas_lin_to_srgb, 4096);
}

static b32 gpu_start(positive bytes)
{
        positive size;
        p64 offset = 0;
        unsigned int ctx = 0, family = 0, device = 0, e = 0, open_error = 0;
        char path[] = "/dev/dri/renderD128";
        int named = 0, known = 0, known_named = 0, unasked = 0;
        unsigned int candidates = 0, candidate_node[GPU_NODES];
        const struct gpu_backend *candidate_backend[GPU_NODES];
        char candidate_bus[GPU_NODES][32] = {{0}};
        positive seen = 14695981039346656037ull;
        long at, hash;

        if (bytes > (1ull << 32))
                return false;
        size = (GPU_DATA + bytes + (1 << 20) + 4095) & ~4095ull; // last MiB is self-test

        memory_zero(&gpu_req, sizeof(gpu_req));
        gpu.fd = -1, gpu.broken = 0, gpu_bytes = bytes, gpu_lost = 0, gpu_gfx103 = 0, gpu_userptr_ok = 0;
        gpu_userptr_off = gpu_flag("/run/moonwater/gpu-nouserptr"), gpu_userptr_gone = 0;
        gpu_own_userptr = gpu_flag("/run/moonwater/gpu-own-userptr");
        gpu_round_off = gpu_flag("/run/moonwater/gpu-round-off");
        gpu_cut_off = gpu_flag("/run/moonwater/gpu-cut-off");
        gpu_imports_all = gpu_flag("/run/moonwater/gpu-imports-all");
        {
                long off = system_open_at(AT_FDCWD, (string_address) "/run/moonwater/gpu-gfx10", FILE_READ | O_CLOEXEC | O_NOFOLLOW);

                GPU_WIDE_FAKE(off);
                gpu_wide = off >= 0;
                if (off >= 0)
                        system_close((int)off);
        }
        for (unsigned int node = 0; node < 64; node++)
        {
                char name[32] = "";
                p64 ver[8] = {0, 0, sizeof(name) - 1, (p64)name};
                long fd;
                unsigned int family_error;
                const struct gpu_backend *driver;
                b32 version;

                path[17] = (char)('0' + (28 + node) / 10), path[18] = (char)('0' + (28 + node) % 10);
                fd = system_open_at(AT_FDCWD, (string_address)path, 2 | O_CLOEXEC | O_NOFOLLOW);
                GPU_OPEN_FAKE(fd);
                if (fd < 0)
                {
                        unsigned int err = (unsigned int)-fd & 255;

                        if (node == 0 || (open_error == 2 && err != 2))
                                open_error = err, gpu_req_record(path, 0, gpu_req.family, gpu_req.device, 0);
                        continue;
                }
                version = gpu_raw((int)fd, 3u << 30 | 64u << 16 | 0x6400u, ver) == 0;
                if (!version)
                        name[0] = 0;
                seen = gpu_node_mix(seen, node, name);
                if (!named)
                        named = 1, gpu_req_record(path, name, gpu_req.family, gpu_req.device, 0);
                if (!version || !(driver = gpu_backend_of(name)))
                {
                        system_close((int)fd);
                        continue;
                }
                family_error = driver->identify((int)fd, &family, &device);
                if (!known_named)
                        known_named = 1, gpu_req_record(path, name, family, device, 0);
                known = 1, unasked |= family_error != 0;
                if (!family_error && driver->set(family) && candidates < GPU_NODES)
                        candidate_node[candidates] = node, candidate_backend[candidates] = driver,
                        gpu_bus_of((int)fd, candidate_bus[candidates], sizeof(candidate_bus[0])), candidates++;
                system_close((int)fd);
        }
        gpu_req.nodes = seen | 1;
        if (!candidates)
        {
                gpu.family = gpu_req.family, gpu.device = gpu_req.device;
                if (gpu_req_step(GPU_REQ_NODE, !named, open_error) && gpu_req_step(GPU_REQ_DRIVER, !known, 0))
                        gpu_req_step(GPU_REQ_FAMILY, true, 0);
        }
        else
        {
                int pick = gpu_pick(candidate_bus, candidates, gpu_display, &gpu_req.pick);
                int kept_fd = gpu_shared.fd, any_broken = 0;
                unsigned char *kept_at = gpu_shared.at;
                positive kept_bytes = gpu_shared.bytes;
                void (*kept_pin)(positive offset, b32 on) = gpu_shared.pin;

                // When no candidate matches the display bus, honor gpu_backends[] priority order (amdgpu > i915 > xe).
                if (gpu_req.pick != 1 && candidates > 1)
                {
                        unsigned int best_rank = GPU_BACKENDS;

                        for (unsigned int ci = 0; ci < candidates; ci++)
                                for (unsigned int bi = 0; bi < GPU_BACKENDS; bi++)
                                        if (candidate_backend[ci] == gpu_backends[bi] && bi < best_rank)
                                                best_rank = bi, pick = (int)ci;
                }
                // Try the preferred candidate first; if any bring-up step (open, window, userptr, context, self-test) fails,
                // cleanly tear down and fall back to any remaining supported candidates before giving up.
                for (unsigned int step = 0; step < candidates; step++)
                {
                        unsigned int ci = step == 0 ? (unsigned int)pick : (step <= (unsigned int)pick ? step - 1 : step);
                        long picked;

                        if (step > 0)
                        {
                                any_broken |= gpu.broken;
                                gpu_stop();
                                gpu_shared.fd = kept_fd, gpu_shared.at = kept_at, gpu_shared.bytes = kept_bytes, gpu_shared.pin = kept_pin;
                                memory_zero(&gpu_req, sizeof(gpu_req));
                                gpu_req.nodes = seen | 1;
                                gpu_userptr_ok = 0, gpu_userptr_gone = 0;
                        }
                        gpu_req.pick = gpu_display[0] ? (candidate_bus[ci][0] && string_equals((string_address)candidate_bus[ci], (string_address)gpu_display) ? 1u : 2u) : 0u;
                        path[17] = (char)('0' + (28 + candidate_node[ci]) / 10), path[18] = (char)('0' + (28 + candidate_node[ci]) % 10);
                        picked = system_open_at(AT_FDCWD, (string_address)path, 2 | O_CLOEXEC | O_NOFOLLOW);
                        if (picked >= 0)
                        {
                                gpu.fd = (int)picked, gpu_backend = candidate_backend[ci];
                                if (gpu_backend->identify(gpu.fd, &family, &device) == 0 && gpu_backend->set(family))
                                {
                                        gpu.family = family, gpu.device = device;
                                        gpu_req_record(path, gpu_backend->driver, gpu.family, gpu.device, candidate_bus[ci]);
                                }
                                else
                                        system_close(gpu.fd), gpu.fd = -1;
                        }
                        else if (step == 0)
                                open_error = (unsigned int)-picked & 255;
                        gpu.family = gpu_req.family, gpu.device = gpu_req.device;
                        do
                        {
                                if (!gpu_req_step(GPU_REQ_NODE, !named, open_error))
                                        break;
                                if (!gpu_req_step(GPU_REQ_DRIVER, !known, 0))
                                        break;
                                if (!gpu_req_step(GPU_REQ_FAMILY, gpu.fd < 0, 0))
                                        break;
                                // The picked chip's kernel set, address space and slots: the window every address is placed in, the room of each slot's command words, and where
                                // the kernels go after the slots' arguments. A space too small for the path is the context's step (its errno).
                                {
                                        positive va = 0, maps = 0, top = 0;
                                        unsigned int room = gpu_backend->packets, code;

                                        gpu_set = gpu_backend->set(gpu.family);
                                        if (gpu_backend == &gpu_i915_row)
                                                gpu_i915_clocks_restore(); // a least clock a compositor that died left raised
                                        e = gpu_backend->window(gpu.family, &va, &maps, &top);
                                        // The slots' state (struct gpu_backend's state) after their arguments, 64-aligned; the kernels after it.
                                        unsigned int state = (GPU_SLOTS * room + 256 * GPU_SLOTS + 63) & ~63u;

                                        code = (state + GPU_SLOTS * gpu_backend->state + 255) & ~255u;
                                        code = code < 1024 ? 1024 : code;
                                        if (!e && (!gpu_set || gpu_set->count > GPU_KERNELS || (room & 255) || (gpu_backend->state & 63) || va + size > maps || maps >= top ||
                                                   top > GPU_VA_RECORD))
                                                e = 22;
                                        for (unsigned int i = 0, reach = code; !e && i < gpu_set->count; i++)
                                        {
                                                unsigned int bk = gpu_set->kernels[i].size > 4 * gpu_set->skip ? gpu_set->kernels[i].size - 4 * gpu_set->skip : 0;

                                                if ((reach = (reach + bk + 255) & ~255u) > GPU_BASES_AT)
                                                        e = 28; // the kernels end before the bases begin
                                        }
                                        if (!e)
                                                gpu_window.va = va, gpu_window.maps = maps, gpu_window.top = top, gpu_map_next = maps,
                                                gpu_layout.packets = room, gpu_layout.args = GPU_SLOTS * room, gpu_layout.state = gpu_backend->state ? state : 0,
                                                gpu_layout.code = code;
                                        if (e && !gpu_req_step(GPU_REQ_CONTEXT, true, e))
                                                break;
                                }
                                // Userptr: an anonymous page the kernel takes for the card; if userptr is refused, try udmabuf+PRIME fallback before aborting init.
                                {
                                        long page = system_call_6(syscall(mmap), 0, 4096, 3, 0x22, (positive)-1, 0);

                                        gpu_alarm_arm(3u);
                                        e = page < 0 && page > -4096 ? 12 : gpu_userptr_error((positive)page, GPU_USER_WRITTEN);
                                        gpu_alarm_disarm();
                                        gpu_userptr_ok = e == 0;
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
                                gpu_alarm_arm(3u);
                                e = gpu_backend->buffer(size, GPU_VA, &gpu.handle, &offset);
                                if (!e)
                                        e = gpu_backend->context(&ctx, 0);
                                gpu_alarm_disarm();
                                if (!e)
                                {
                                        at = system_call_6(syscall(mmap), 0, size, 3, 1, (positive)gpu.fd, offset);
                                        if (at < 0 && at > -4096)
                                                e = (unsigned int)-at & 255;
                                        else
                                                gpu.at = (unsigned int address_to)at, gpu.size = size;
                                }
                                if (!gpu_req_step(GPU_REQ_CONTEXT, e != 0, e))
                                        break;
                                gpu.ctx = ctx;
                                for (unsigned int i = 0, here = gpu_layout.code; i < gpu_set->count; i++)
                                {
                                        unsigned int bytes_k = gpu_set->kernels[i].size > 4 * gpu_set->skip ? gpu_set->kernels[i].size - 4 * gpu_set->skip : 0;

                                        gpu_kernel_at[i] = here;
                                        if (gpu_set->kernels[i].words && bytes_k)
                                        {
                                                unsigned int address_to dst_k = (unsigned int address_to)((p8 address_to)gpu.at + here);

                                                memory_copy(dst_k, gpu_set->kernels[i].words + gpu_set->skip, bytes_k);
                                                // Cross-gen Intel EU binary patching from Gen9 words (verified word-for-word against intel-probe kern7/kern8/kern9/kern11):
                                                if (gpu_backend == &gpu_i915_row)
                                                {
                                                        unsigned int insns = bytes_k / 16u;

                                                        if (gpu.family < 800)
                                                        {
                                                                for (unsigned int k = 0; k < insns; k++)
                                                                {
                                                                        unsigned int *w = dst_k + 4 * k;
                                                                        unsigned int op = w[0] & 0x7fu;
                                                                        unsigned int fsub = w[1] & 1u, freg = (w[1] >> 1) & 1u, mctrl = (w[1] >> 2) & 1u;
                                                                        unsigned int drf = (w[1] >> 3) & 3u, dtype = (w[1] >> 5) & 7u;
                                                                        unsigned int s0rf = (w[1] >> 9) & 3u, s0type = (w[1] >> 11) & 7u;
                                                                        unsigned int s1rf = (w[2] >> 25) & 3u, s1type = (w[2] >> 27) & 7u;

                                                                        if (op == 0x31u)
                                                                        {
                                                                                if (((w[0] >> 24) & 0xfu) == 0xcu)
                                                                                        w[0] = (w[0] & ~0x0f000000u) | (0x5u << 24); // Gen7 Render Cache SFID = 5
                                                                                s1type = 1u; // Gen7 send Src1Type = :d
                                                                        }
                                                                        else if (op == 0x20u && gpu.family < 750)
                                                                                w[3] = (unsigned int)((int)w[3] >> 3); // Ivy Bridge/VLV jmpi JIP is in qwords (8B)
                                                                        w[0] = (w[0] & ~0x00000200u) | (mctrl << 9);
                                                                        w[1] = (w[1] & 0xffff8000u) | drf | (dtype << 2) | (s0rf << 5) | (s0type << 7) | (s1rf << 10) | (s1type << 12);
                                                                        w[2] = (w[2] & 0x01ffffffu) | (fsub << 25) | (freg << 26);
                                                                }
                                                        }
                                                        else if (gpu.family == 800 || gpu.family == 801)
                                                        {
                                                                for (unsigned int k = 0; k < insns; k++)
                                                                        if ((dst_k[4 * k] & 0x7fu) == 0x31u)
                                                                                dst_k[4 * k + 2] |= 0x08000000u; // Gen8 send Src1Type = :d
                                                        }
                                                        else if (gpu.family == 1100 || gpu.family == 1101)
                                                        {
                                                                for (unsigned int k = 0; k < insns; k++)
                                                                {
                                                                        unsigned int *w = dst_k + 4 * k;

                                                                        if (w[0] == 0x00000041u && w[1] == 0x20400208u && w[2] == 0x06000004u && w[3] == 0x00000010u)
                                                                                w[0] = 0x00000009u, w[3] = 0x00000004u; // Gen11: mul ..., 0x10 -> shl ..., 4
                                                                        else if ((w[0] & 0x7fu) == 0x31u && w[3] == 0x060a8000u)
                                                                                w[3] = 0x040a8000u; // Gen11: Media Block Write MLen 3 -> 2
                                                                }
                                                        }
                                                }
                                        }
                                        here = (here + bytes_k + 255) & ~255u;
                                }
                                gpu_srgb_upload();
                                // The self-test: the card must finish its job (RING) and draw the picture the kernels draw (SELFTEST).
                                hash = gpu_test_hash();
                                if (!gpu_req_step(GPU_REQ_RING, hash == 0, gpu_last_error))
                                {
                                        gpu_say_kernel();
                                        break;
                                }
                                if (!gpu_req_step(GPU_REQ_SELFTEST, hash != gpu_set->hash, 0))
                                {
                                        if (!gpu_recovered)
                                                gpu.broken = 1;
                                        break;
                                }
                                gpu_image_ok = (gpu_set == &gpu_amd_rdna2 || gpu_set == &gpu_amd_rdna3) && gpu_image_test();
                                gpu_nv12_ok = gpu_backend == &gpu_amd && gpu_nv12_test();
                                if ((gpu_req.mask & (1u << GPU_REQ_UDMABUF | 1u << GPU_REQ_PRIME)) != (1u << GPU_REQ_UDMABUF | 1u << GPU_REQ_PRIME))
                                        gpu_route_probe();
                                gpu_kmsg_drain();
                                return true;
                        } while (0);
                }
                gpu.broken |= any_broken;
        }
        // Settled: no node of a driver this file has, or every one of them answered with a chip that has no kernel set. The same nodes answer the same.
        gpu_req.settled = gpu_req.first == GPU_REQ_DRIVER || (gpu_req.first == GPU_REQ_FAMILY && !candidates && !unasked);
        // A card that drew the self-test wrong is kept broken past the stop (which zeroes the card's state): the compositor gives it up as untrusted and does
        // not start it again, where with the flag cleared it was a card not there, started and self-tested again by every late look.
        {
                int broken = gpu.broken;

                gpu_stop();
                gpu.broken = broken;
        }
        return false;
}

// What the start's first failed step was in this machine's words, for the notices (the issue board's detail, the log): a driver that has no path names itself and
// its node, a chip with no kernel set names its driver, its family and its device. False, and "", for the other steps, whose fix is the same on every machine.
static b32 gpu_req_words(char *into, unsigned int room)
{
        const struct gpu_backend *driver = gpu_backend_of(gpu_req.name);
        unsigned int at;

        into[0] = 0;
        if (gpu_req.first == GPU_REQ_DRIVER)
        {
                at = gpu_text_put(into, 0, room, gpu_req.name[0] ? gpu_req.name : "a driver that does not say its name");
                at = gpu_text_put(into, at, room, " on "), gpu_text_put(into, at, room, gpu_req.node[0] ? gpu_req.node : "the first render node");
                return true;
        }
        if (gpu_req.first != GPU_REQ_FAMILY || !driver)
                return false;
        if (!gpu_req.family && !gpu_req.device)
        {
                at = gpu_text_put(into, 0, room, gpu_req.name);
                gpu_text_put(into, at, room, ", a chip that did not say what it is (the kernel lines say why)");
                return true;
        }
        driver->describe(gpu_req.family, gpu_req.device, into, 0, room);
        return true;
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
#define GPU_GONE 64
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
static positive gpu_fault_next; // when a refused submit may read the log again (monotonic ns)
static positive gpu_fault_import; // the import the last fault said was in (gpu_fault_say): its address as gpu_import_fd answered it, held or let go of; 0 for none

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
// The jobs apart (moonwater debug): the ones a call waited for (gpu_run) and the ones it did not (gpu_go); of the waited ones, the time in the submit (the kernel's
// look at every userptr page the bo list names, and EAGAIN's asks again) and the time after it until the fence or the cap; and the jobs of each kind of program, 0
// a scene (ops 4, 5 and 6: a present, a program's list, a turned screen), 1 a copy (op 0), 2 a picture scaled (op 7), with their time (the waited ones' whole time).
static unsigned int gpu_jobs_waited, gpu_jobs_unwaited, gpu_jobs_kind[3];
static unsigned long long gpu_job_submit_ns, gpu_job_wait_ns, gpu_job_ns_kind[3], gpu_submit_took; // (the last submit's own time, gpu_submit)
#define GPU_JOB_KIND(op) ((op) == 0 ? 1u : (op) == 7 || (op) == 9 ? 2u : 0u) // scenes, copies, pictures scaled by tables (7, and NV12 made XRGB, 9)
#ifndef GPU_MAP_FAKE
#define GPU_MAP_FAKE(taken)
#endif
// Why the last map was refused (gpu_map_as): 1 all the maps are held, 2 no address for it, 3 the kernel would not take the pages (userptr), 4 it would not give them
// their address, 9 a written map over one a record of this tick was given, 10 a map would be let go of for it while a job given up on may still read it; for 3
// and 4 the error (the kernel's, negated) is in the bits above the first byte. moonwater canvas says it with the card's refusals.
static unsigned int gpu_map_why;

static p64 gpu_maps_handed[4];

static void gpu_maps_reset(void)
{
        for (unsigned int i = 0; i < gpu_map_count; i++)
                if (gpu_maps[i].dma && gpu_maps[i].pin)
                        gpu_maps[i].pin(gpu_maps[i].offset, 0);
        gpu_map_count = 0, gpu_map_next = GPU_VA_MAPS;
        memory_zero(gpu_maps_handed, sizeof(gpu_maps_handed));
}

#define GPU_IMPORT_GUARD 0x100000ull // the addresses kept clear on either side of an import (gpu_place_aligned)
// Addresses for bytes, none of a map's or an import's that is held: after the last ones handed out, or, where they would end past GPU_VA_END (a long
// session's maps made and let go of), the first gap from the start; 0 for none. guard is the space kept clear on either side of what is placed, and every
// held import has GPU_IMPORT_GUARD clear on either side of it too: a client's picture the card reads by its layout may be read a little past its
// buffer (gpu_amd_image_fits), and that read is to find no page there (a fault the hang's blame names), never another buffer's or the compositor's.
// Unified BO/VA descriptor and lifecycle helpers across gpu_maps and gpu.imported*.
struct gpu_bo
{
        positive va, bytes, from, to;
        unsigned int handle, listed;
        b32 written, dma, import;
};

static inline b32 gpu_bo_contains(positive va, positive bytes, positive at)
{
        return at >= va && at - va < bytes;
}

static void gpu_bo_unbind_close(const struct gpu_bo *bo, enum gpu_gone_why why)
{
        gpu_gone_put(bo->va, bo->bytes, bo->from, bo->to, bo->handle, why, bo->written, bo->dma, bo->import, bo->listed);
        gpu_backend->unbind(bo->handle, bo->va, bo->bytes);
        gpu_close(bo->handle);
}

static unsigned int gpu_bo_count(void)
{
        return gpu_map_count + gpu.imports;
}

static void gpu_bo_va_range(unsigned int idx, positive *va, positive *bytes, positive *guard)
{
        if (idx < gpu_map_count)
                *va = gpu_maps[idx].va, *bytes = gpu_maps[idx].bytes, *guard = 0;
        else
        {
                unsigned int j = idx - gpu_map_count;

                *va = gpu.imported_va[j], *bytes = gpu.imported_bytes[j], *guard = GPU_IMPORT_GUARD;
        }
}

static positive gpu_place_aligned(positive bytes, positive align, positive guard)
{
        unsigned int total = gpu_bo_count();
        b32 wrapped;

        if (!bytes || !align || (align & (align - 1)) || guard >= GPU_VA_END || bytes > GPU_VA_END - guard ||
            bytes + guard > GPU_VA_END - GPU_VA_MAPS)
                return 0;
        wrapped = gpu_backend == &gpu_i915_row || gpu_map_next > GPU_VA_END - bytes - guard;

        for (unsigned int pass = 0; pass < 2; pass++)
        {
                positive at = (pass == 0 && !wrapped) ? gpu_map_next + guard : GPU_VA_MAPS + guard;

                at = (at + align - 1) & ~(align - 1);
                for (unsigned int i = 0; i < total; i++)
                {
                        positive from, span, extra, gap;

                        if (at > GPU_VA_END - bytes - guard)
                                break;
                        gpu_bo_va_range(i, &from, &span, &extra);
                        gap = guard + extra;
                        if (at < from + span + gap && from < at + bytes + gap)
                        {
                                positive next = from + span + gap;

                                if (next < from || next > (~(positive)0) - (align - 1))
                                {
                                        at = GPU_VA_END;
                                        break;
                                }
                                at = (next + align - 1) & ~(align - 1), i = ~0u; // past it, and each held one looked at again
                        }
                }
                if (at <= GPU_VA_END - bytes - guard)
                        return at;
                if (wrapped)
                        break;
                wrapped = true;
        }
        return 0;
}

// Where an import is placed: at a whole 64 KiB. A client's tiled buffer is laid out in blocks of 64 KiB whose pipe and bank bits are the address's own
// (the swizzle XORs them in), so the texture unit reads one that starts inside a block at addresses outside it: an import at 0x2002c65000 (a page past a block)
// was read at 0x2002c64000, a page below its first, the card faulted (TCP, a permission fault) and its ring was reset. Mesa gives such a buffer an address of
// its block's alignment; so does this, for every import (a linear one loses nothing by it but address space).
#define GPU_IMPORT_ALIGN 65536ull

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
        memory_zero(gpu_maps_handed, sizeof(gpu_maps_handed));
        gpu_stop();
        gpu_shared.fd = kept_fd, gpu_shared.at = kept_at, gpu_shared.bytes = kept_bytes, gpu_shared.pin = kept_pin;
        return gpu_start(gpu_bytes);
}

// The map at i let go of, for the reason why: unmapped (its whole span), its buffer closed, the last map in its place.
static void gpu_drop(unsigned int i, enum gpu_gone_why why)
{
        const struct gpu_bo bo = {gpu_maps[i].va, gpu_maps[i].bytes, gpu_maps[i].from, gpu_maps[i].to,
                                  gpu_maps[i].handle, gpu_maps[i].listed, gpu_maps[i].written, gpu_maps[i].dma, 0};

        gpu_bo_unbind_close(&bo, why);
        if (gpu_maps[i].dma && gpu_maps[i].pin)
                gpu_maps[i].pin(gpu_maps[i].offset, 0);
        {
                unsigned int last = --gpu_map_count;

                gpu_maps[i] = gpu_maps[last];
                if (gpu_maps[i].handed)
                        gpu_maps_handed[i >> 6] |= 1ull << (i & 63);
                else
                        gpu_maps_handed[i >> 6] &= ~(1ull << (i & 63));
                gpu_maps_handed[last >> 6] &= ~(1ull << (last & 63));
        }
}

// With every map held, the one used longest ago goes first (a window dragged larger makes a map at each size, faster than gpu_trim lets them go), unless all
// are this tick's. The card may only read a map (a window's picture is a program's pages): a write of a kernel past what its records name faults there and
// is dropped; gpu_map_written is for the one the GPU draws into (a headless scanout). No map can be run as code.
// What a refused map leaves to say why, once for each way it was refused (gpu_map_why): the node asked a plain question (does it answer at all), a userptr over a page of
// anonymous memory and over the first page of the refused range (whole, and without the validation that takes the pages at once) tried and their errors kept, and the
// last lines the kernel's log has about the card (/dev/kmsg gives a record a read) kept. moonwater debug shows them, so a machine nobody can sit at can say why.
static unsigned int gpu_map_probe, gpu_map_said;
static char gpu_said[256];

/*
        The kernel's log, from its oldest record (/dev/kmsg, not waited for): its descriptor, -1 for none. gpu_kmsg_next gives its records one a read
        ("prio,seq,usec,flags;text", its NUL after it), the first 8192 of a walk, and 0 at the end, when it closes the log (handle -1). The one walk of
        gpu_say_kernel, gpu_kmsg_drain and gpu_fault_read.
*/
#define gpu_kmsg_open() ((long)system_open_at(AT_FDCWD, (string_address) "/dev/kmsg", FILE_READ | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW))
// A read that answers EPIPE (-32) is the log's records overwritten under the reader (the ring wrapped during the walk): the kernel moved the
// reader to its oldest record, and the next read goes on from there (devkmsg_read), so it is read again, not taken for the end (E-h4-1).
static char *gpu_kmsg_read(long *handle, unsigned int *records, b32 close_at_end)
{
        static char record[8192];
        bipolar got;

        do
                got = *handle >= 0 && (*records)++ < 8192 ? system_read_once(*handle, record, sizeof(record) - 1) : 0;
        while (got == -32);
        if (got > 0)
                return record[got] = 0, record;
        if (close_at_end && *handle >= 0)
                system_close(*handle), *handle = -1;
        return 0;
}

static char *gpu_kmsg_next(long *handle, unsigned int *records)
{
        return gpu_kmsg_read(handle, records, true);
}

static void gpu_say_kernel(void)
{
        static char last[3][90];
        unsigned int kept = 0, at, records = 0;
        long handle = gpu_kmsg_open();
        char *record;

        if (handle < 0)
                return;
        while ((record = gpu_kmsg_next(&handle, &records)))
        {
                char *text = (char *)string_find((string_address)record, (string_address) ";"), *finish;

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
        {
                unsigned int kept_103 = gpu_gfx103, kept_maj = gpu_amd_gfx_major, kept_min = gpu_amd_gfx_minor;
                positive kept_va = gpu_amd_va_max;

                gpu_map_probe = (gpu_backend->identify(gpu.fd, &family, &device) == 0 ? 1u : 0u) | (page ? gpu_userptr_error((positive)page, GPU_USER_WRITTEN) : 255u) << 8 |
                                gpu_userptr_error(from, GPU_USER_WRITTEN) << 16 | gpu_userptr_error(from, GPU_USER_LATER) << 24;
                gpu_gfx103 = kept_103, gpu_amd_gfx_major = kept_maj, gpu_amd_gfx_minor = kept_min, gpu_amd_va_max = kept_va;
        }
        gpu_say_kernel();
}

#define gpu_map(addr, size) gpu_map_as(addr, size, 0)
#define gpu_map_written(addr, size) gpu_map_as(addr, size, 1)
// The compositor's own memory the dma-buf route reaches that holds the pages [from, to): a registered memfd (gpu_register_memfd: whole, a map of any of it is
// made of all of it) or the pictures of the window table's memfd (gpu_shared: a map is of its own pages, since the table's rows are made and given back apart).
// Its descriptor, where its byte 0 is mapped, its bytes and its pin; false for none.
struct gpu_region
{
        int fd;
        positive base, bytes;
        void (*pin)(positive offset, b32 on);
        b32 whole;
};
static b32 gpu_region_of(positive from, positive to, struct gpu_region address_to r)
{
        for (unsigned int i = 0; i < gpu_memfd_count; i++)
                if (gpu_memfds[i].fd >= 0 && gpu_memfds[i].at && from >= (positive)gpu_memfds[i].at && to <= (positive)gpu_memfds[i].at + gpu_memfds[i].bytes)
                {
                        r->fd = gpu_memfds[i].fd, r->base = (positive)gpu_memfds[i].at, r->bytes = gpu_memfds[i].bytes, r->pin = gpu_memfds[i].pin, r->whole = true;
                        return true;
                }
        if (gpu_shared.fd >= 0 && gpu_shared.at && from >= (positive)gpu_shared.at && to <= (positive)gpu_shared.at + gpu_shared.bytes)
        {
                r->fd = gpu_shared.fd, r->base = (positive)gpu_shared.at, r->bytes = gpu_shared.bytes, r->pin = gpu_shared.pin, r->whole = false;
                return true;
        }
        return false;
}

// Whether a memfd is sealed as udmabuf takes one (F_GET_SEALS: F_SEAL_SHRINK, and not F_SEAL_WRITE): its pages can then be the card's by the dma-buf route.
static b32 gpu_memfd_sealed(int fd)
{
        long seals = (long)system_call_3(syscall(fcntl), (positive)fd, 1034, 0);

        return seals >= 0 && (seals & 2) && !(seals & 8);
}

static void gpu_map_record(positive from, positive to, positive at, unsigned int handle, b32 written, b32 dma, positive offset, void (*pin)(positive, b32))
{
        gpu_maps[gpu_map_count].from = from, gpu_maps[gpu_map_count].to = to, gpu_maps[gpu_map_count].va = at;
        gpu_maps[gpu_map_count].bytes = to - from, gpu_maps[gpu_map_count].handle = handle, gpu_maps[gpu_map_count].used = gpu_tick;
        gpu_maps[gpu_map_count].written = written, gpu_maps[gpu_map_count].dma = dma, gpu_maps[gpu_map_count].handed = 0;
        gpu_maps[gpu_map_count].listed = 0, gpu_maps[gpu_map_count].offset = offset, gpu_maps[gpu_map_count].pin = pin;
        gpu_map_count++, gpu_map_next = at + (to - from);
}

static b32 gpu_map_dmabuf(const struct gpu_region address_to r, positive from, positive to, positive at, b32 written)
{
        long made = gpu_dmabuf_make(r->fd, from - r->base, to - from);
        unsigned int e, handle;

        if (made < 0)
                return gpu_map_why = 6 | (unsigned int)-made << 8, false;
        made = gpu_dmabuf_in((int)made);
        if (made < 0)
                return gpu_map_why = 7 | (unsigned int)-made << 8, false;
        handle = (unsigned int)made;
        if ((e = gpu_backend->bind(handle, at, to - from, written)) != 0)
                return gpu_close(handle), gpu_map_why = 8 | e << 8, false;
        gpu_map_record(from, to, at, handle, written, 1, from - r->base, r->pin);
        gpu_maps_dma++;
        if (r->pin)
                r->pin(from - r->base, 1);
        return true;
}

static b32 gpu_map_try(const void *addr, positive size, b32 written)
{
        positive from, to, orig_from, orig_to, at;
        unsigned int handle = 0, e, old = 0;
        long taken = 0;
        struct gpu_region region;
        b32 own, first, widened = false;

        if (!addr || !size || size > (~(positive)0) - 4095ull || (positive)addr > (~(positive)0) - 4095ull - size)
                return gpu_map_why = 2, false;
        from = (positive)addr & ~4095ull, to = ((positive)addr + size + 4095) & ~4095ull, orig_from = from, orig_to = to;
        own = gpu_region_of(from, to, &region), first = own && !gpu_own_userptr && gpu_memfd_sealed(region.fd);

        if (first && region.whole && region.base + (region.bytes & ~4095ull) >= to && !(region.base & 4095))
                from = region.base, to = region.base + (region.bytes & ~4095ull), widened = (from != orig_from || to != orig_to);
retry_slice:
        for (unsigned int i = 0; i < gpu_map_count; i++)
                if ((written || gpu_maps[i].written) && from < gpu_maps[i].to && gpu_maps[i].from < to && gpu_maps[i].handed && gpu_maps[i].used == gpu_tick)
                {
                        if (widened)
                        {
                                from = orig_from, to = orig_to, widened = false;
                                goto retry_slice;
                        }
                        return gpu_map_why = 9, false;
                }
        for (unsigned int i = 0; i < gpu_map_count; i++)
                if ((written || gpu_maps[i].written) && from < gpu_maps[i].to && gpu_maps[i].from < to)
                {
                        if (widened)
                        {
                                from = orig_from, to = orig_to, widened = false;
                                goto retry_slice;
                        }
                        if (!gpu_idle())
                                return gpu_map_why = 10, false;
                        gpu_drop(i--, GPU_GONE_OVERLAP);
                }
        if (gpu_map_count == GPU_MAPS)
        {
                // Reclaim forgotten unhanded slots (even from this tick) before LRU eviction.
                for (unsigned int i = 0; i < gpu_map_count; i++)
                        if (!gpu_maps[i].from && !gpu_maps[i].to && !gpu_maps[i].handed)
                        {
                                if (!gpu_idle())
                                        return gpu_map_why = 10, false;
                                gpu_drop(i, GPU_GONE_FORGET);
                                break;
                        }
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
        if (!(at = gpu_place_aligned(to - from, 4096, 0)))
        {
                if (widened)
                {
                        from = orig_from, to = orig_to, widened = false;
                        goto retry_slice;
                }
                return gpu_map_why = 2, false;
        }
        if (first && gpu_map_dmabuf(&region, from, to, at, written))
                return true;
        if (widened)
        {
                // Whole-memfd udmabuf failed (e.g. >64 MiB default udmabuf size_limit_mb); retry the exact requested slice.
                from = orig_from, to = orig_to;
                if (first && gpu_map_dmabuf(&region, from, to, at, written))
                        return true;
        }
        if (!gpu_userptr_off && !gpu_userptr_gone)
        {
                taken = -(long)gpu_backend->user(from, to - from, written ? GPU_USER_WRITTEN : GPU_USER_READ, &handle);
                GPU_MAP_FAKE(taken);
                if (taken == 0)
                {
                        if ((e = gpu_backend->bind(handle, at, to - from, written)) != 0)
                        {
                                gpu_close(handle), gpu_map_why = 4 | e << 8;
                                if (own && !first)
                                        return gpu_map_dmabuf(&region, from, to, at, written);
                                return false;
                        }
                        gpu_map_record(from, to, at, handle, written, 0, 0, 0);
                        gpu_maps_user++;
                        if (first)
                                gpu_maps_own_user++, gpu_maps_own_errno = gpu_map_why >> 8;
                        return true;
                }
                if (taken == -19 && gpu_userptr_ok)
                        return gpu_lost = 1, gpu_map_why = 3 | (unsigned int)-taken << 8, false;
                if (taken == -95 || taken == -38 ||
                    (!gpu_maps_user && !gpu_userptr_ok && (taken == -19 || taken == -22 || taken == -14 || taken == -1)))
                        gpu_userptr_gone = 1;
                else if (taken != -22 && taken != -14 && taken != -1)
                        return gpu_map_why = 3 | (unsigned int)-taken << 8, gpu_map_refused(from), false;
        }
        if (first)
                return false;
        if (!own)
                return gpu_map_why = 5, false;
        return gpu_map_dmabuf(&region, from, to, at, written);
}


// The last map refused (gpu_map_as), for the card's line in moonwater debug and a fault's story: its address and bytes, what memory it was (1 the window table's
// pictures, 2 a registered memfd's, 3 other memory: a client's pool, a dma-buf's mapping, the compositor's own anonymous scratch), gpu_map_why's step and errno
// then, and the tick. (Whether an import covers the range is not known here: an import has a card address and no processor one.)
#define GPU_MAP_TABLE 1
#define GPU_MAP_REGISTERED 2
#define GPU_MAP_OTHER 3
static struct { positive at, bytes; unsigned int kind, why, tick; } gpu_map_refusal;
// Whether a live map already holds [addr, addr + bytes) (one to read, or one the card may write when written): the map is kept, marked used in this tick, and no
// new one is made. A map let go of (gpu_forget: from and to are 0) is not one. Without this a present's map of the same picture was made again each present,
// and a written map over a written map of the same range dropped the old one (gpu_map_try's overlap rule): 3,610 userptr and 7,206 dma-buf maps a minute.
static b32 gpu_map_hit(const void *addr, positive bytes, b32 written)
{
        positive p = (positive)addr;

        for (unsigned int i = 0; i < gpu_map_count; i++)
                if ((!written || gpu_maps[i].written) && (gpu_maps[i].from || gpu_maps[i].to) && p >= gpu_maps[i].from && p < gpu_maps[i].to &&
                    bytes <= gpu_maps[i].to - p)
                {
                        gpu_maps[i].used = gpu_tick;
                        return true;
                }
        return false;
}

static b32 gpu_map_as(const void *addr, positive size, b32 written)
{
        struct gpu_region region;
        positive from, to;

        if (!addr || !size || size > (~(positive)0) - 4095ull || (positive)addr > (~(positive)0) - 4095ull - size)
                return gpu_map_why = 2, false;
        from = (positive)addr & ~4095ull, to = ((positive)addr + size + 4095) & ~4095ull;

        if (gpu_map_hit(addr, size, written) || gpu_map_try(addr, size, written))
                return true;
        gpu_map_refusal.at = (positive)addr, gpu_map_refusal.bytes = size, gpu_map_refusal.why = gpu_map_why, gpu_map_refusal.tick = gpu_tick;
        gpu_map_refusal.kind = !gpu_region_of(from, to, &region) ? GPU_MAP_OTHER : region.whole ? GPU_MAP_REGISTERED : GPU_MAP_TABLE;
        return false;
}

// (The map found last is looked at first: a present asks for the same few pictures over and over, and the table is up to 256 maps looked at in order. The
// place kept is only where the looking starts: the map there is held to the pointer as any other is, so a table that has moved under it costs a look, not an answer.)
static unsigned int gpu_map_hint[32];
static positive gpu_va_lookup(const void *pointer, positive bytes, b32 written_only)
{
        unsigned int slot = (unsigned int)(((positive)pointer >> 16) & 31u);
        unsigned int start = gpu_map_hint[slot] < gpu_map_count ? gpu_map_hint[slot] : 0;

        for (unsigned int k = 0, i = start; k < gpu_map_count; k++, i = i + 1 < gpu_map_count ? i + 1 : 0)
                if ((!written_only || gpu_maps[i].written) && (positive)pointer >= gpu_maps[i].from && (positive)pointer < gpu_maps[i].to &&
                    bytes <= gpu_maps[i].to - (positive)pointer) // no sum to wrap
                {
                        gpu_maps[i].used = gpu_tick, gpu_maps[i].handed = 1;
                        gpu_maps_handed[i >> 6] |= 1ull << (i & 63);
                        if (!written_only)
                                gpu_map_hits++, gpu_map_hint[slot] = i;
                        return gpu_maps[i].va + (positive)pointer - gpu_maps[i].from;
                }
        if (!written_only)
                gpu_map_misses++;
        return 0;
}
static positive gpu_va_bytes(const void *pointer, positive bytes)
{
        return gpu_va_lookup(pointer, bytes, false);
}
#define gpu_va(pointer) gpu_va_bytes(pointer, 1)

// The same, for a map the card may write (gpu_map_written): 0 for a pointer that only a map to read holds.
static positive gpu_va_written(const void *pointer, positive bytes)
{
        return gpu_va_lookup(pointer, bytes, true);
}

// The reverse of gpu_va_bytes: the processor's pointer to the card address va, for bytes, through a map that holds them (its from is the processor's address of
// its va). 0 for none: an import has no processor address, and a map let go of (gpu_forget: from and to are 0) is none. It does not mark the map used.
__attribute__((unused)) static const void *gpu_pointer_of_va(positive va, positive bytes)
{
        for (unsigned int i = 0; i < gpu_map_count; i++)
                if ((gpu_maps[i].from || gpu_maps[i].to) && va >= gpu_maps[i].va && va - gpu_maps[i].va < gpu_maps[i].bytes &&
                    bytes <= gpu_maps[i].bytes - (va - gpu_maps[i].va))
                        return (const void *)(gpu_maps[i].from + (va - gpu_maps[i].va));
        return 0;
}

static void gpu_unimport(positive at, enum gpu_gone_why why);
static void gpu_unimport_gen(positive at, unsigned int gen, enum gpu_gone_why why);
static void gpu_release_import(positive at);
static void gpu_release_import_gen(positive at, unsigned int gen);
// S2: an import is named by its card address, which the placer reuses after it wraps, so a holder's old address can name another holder's import. Each import has
// a generation (never 0, new at each placement; a holder keeps the one it had): gpu_import_gen answers the generation of the import at an address, and a release
// with a generation (gen, not 0) is a no-op when the import there has another. Releases refused that way are counted (gpu_import_stale).
static unsigned int gpu_import_gen_last, gpu_import_stale;

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
                if (gpu.imported_dead[i] && gpu_tick - gpu.imported_dead_at[i] >= GPU_IMPORT_GRACE)
                        gpu_unimport(gpu.imported_va[i], GPU_GONE_RELEASE), i--; // swapped last into i: re-check i without O(M^2) restart
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
static b32 gpu_importing_own; // gpu_import is making the import: the compositor's own buffer, named in every job (gpu_import_named)
static positive gpu_import_again(unsigned int i, positive bytes)
{
        if (!bytes || bytes > (~(positive)0) - 4095ull || ((bytes + 4095) & ~4095ull) > gpu.imported_bytes[i] ||
            (!gpu.imported_dead[i] && gpu.imported_holds[i] == 255))
                return 0;
        gpu.imported_own[i] |= (unsigned char)gpu_importing_own, gpu.imported_handed[i] = 1, gpu.imported_used[i] = gpu_tick; // (asked for to be read now)
        if (gpu.imported_dead[i])
                gpu.imported_dead[i] = 0, gpu.imported_holds[i] = 1;
        else
                gpu.imported_holds[i] = (unsigned char)((gpu.imported_holds[i] ? gpu.imported_holds[i] : 1) + 1);
        return gpu.imported_va[i];
}

// A GEM handle of a buffer on the GPU's own node that no import holds yet, mapped read and written and never run, for bytes, at an address of its own: the
// address, 0 for a refusal (no room in the table, no address, a map the kernel refuses), and the handle is closed then. The handle is the GPU's: gpu_import and
// gpu_import_fd make it, and gpu_unimport closes it.
// Aliasing PPGTT shares the one GGTT with the display's scanout buffers, and a job that pins a buffer the display holds anywhere but where it is
// loops in i915_gem_execbuffer2_ioctl (eb_validate_vmas unbinds it, -EAGAIN, eb_relocate_parse_slow tries again). Gen7 and Gen7.5 imports float
// (gpu_i915_reloc_note: listed unpinned, their surfaces relocated), so the card draws the display's buffer itself; a Gen8 or later chip on aliasing
// PPGTT (full PPGTT turned off) has no such path here and renders into an anonymous userptr shadow, importing nothing.
static b32 gpu_needs_shadow_output(void)
{
        return gpu_backend == &gpu_i915_row && gpu_i915.ppgtt < 2 && gpu.family >= 800;
}

static positive gpu_import_at(unsigned int handle, positive bytes)
{
        if (gpu_needs_shadow_output() || !handle || !bytes || bytes > GPU_VA_END)
                return handle ? (gpu_close(handle), 0) : 0;
        positive min_sz = 4096ull;
        positive whole = (bytes + min_sz - 1ull) & ~(min_sz - 1ull), at;
        positive align = gpu_set == &gpu_amd_rdna3 ? 262144ull : (min_sz > GPU_IMPORT_ALIGN ? min_sz : GPU_IMPORT_ALIGN);

        // Room for one more import: the table grown (the memory refuses, and the import is refused as a full table's was).
        at = gpu_imports_room(gpu.imports + 1) ? gpu_place_aligned(whole, align, GPU_IMPORT_GUARD) : 0;
        if (!at || gpu_backend->bind(handle, at, whole, true) != 0)
                return gpu_close(handle), 0;
        if (gpu_backend == &gpu_i915_row && gpu.family < 800)
                gpu_i915.bound[handle].floating = 1; // the window's address names it; the kernel keeps it where the display or the last job put it
        gpu.imported_va[gpu.imports] = at, gpu.imported_bytes[gpu.imports] = whole, gpu.imported_dead[gpu.imports] = 0, gpu.imported_holds[gpu.imports] = 1;
        gpu.imported_own[gpu.imports] = (unsigned char)gpu_importing_own, gpu.imported_handed[gpu.imports] = 1, gpu.imported_used[gpu.imports] = gpu_tick;
        if (!++gpu_import_gen_last)
                ++gpu_import_gen_last;
        gpu.imported_gen[gpu.imports] = gpu_import_gen_last;
        gpu.imported[gpu.imports++] = handle;
        gpu_map_next = at + whole;
        return at;
}

// The generation of the import at address at (0 for none): read right after it is made, or held again, and kept with the address by its holder.
static unsigned int gpu_import_gen(positive at)
{
        for (unsigned int i = 0; at && i < gpu.imports; i++)
                if (gpu.imported_va[i] == at)
                        return gpu.imported_gen[i];
        return 0;
}

// A buffer of another node of the card (the scanout's dumb buffer on the display node, by its handle on that node's descriptor), made reachable here: its address, 0 for none.
// bytes is its size; the address is the one it keeps while the buffer lives. A buffer the table has already is held once more at the address it has (gpu_import_again).
// A refusal lets go of what it took (the compositor asks again each present): the shared descriptor always, the handle unless an import held already has it.
static positive gpu_import(int node, unsigned int handle, positive bytes)
{
        unsigned int share[3] = {handle, 2 | 0x80000, 0};
        int held;
        positive at;

        if (gpu_needs_shadow_output() || node < 0 || !handle || !bytes || gpu.fd < 0 || GPU_CONTROL(node, 3u << 30 | 12u << 16 | 0x642du, share) != 0)
                return 0;
        share[0] = 0;
        if (GPU_CONTROL(gpu.fd, 3u << 30 | 12u << 16 | 0x642eu, share) != 0 || !share[0])
                return system_close((int)share[2]), 0;
        system_close((int)share[2]);
        held = gpu_import_of(share[0]);
        gpu_importing_own = 1;
        at = held >= 0 ? gpu_import_again((unsigned int)held, bytes) : gpu_import_at(share[0], bytes);
        gpu_importing_own = 0;
        return at;
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

        if (gpu_needs_shadow_output() || gpu.broken || gpu.fd < 0 || fd < 0 || !bytes ||
            GPU_CONTROL(gpu.fd, 3u << 30 | 12u << 16 | 0x642eu, share) != 0 || !share[0])
                return 0;
        held = gpu_import_of(share[0]);
        return held >= 0 ? gpu_import_again((unsigned int)held, bytes) : gpu_import_at(share[0], bytes);
}

// Whether the GPU is up to be given a buffer (the card's node is open and it has not been broken).
static b32 gpu_ready(void)
{
        return !gpu.broken && gpu.fd >= 0;
}

static b32 gpu_can_import(void)
{
        return gpu_ready() && !gpu_needs_shadow_output();
}

// The maps over [from, from + bytes) let go of, as the compositor unmaps or moves the pages (a client's pool, cut, shrunk, remapped): a map's
// pages are the old ones, which the card would read where the processor reads the new. A map is taken out of every address now (its from and to
// are 0, its va kept for the unmap) and dropped by gpu_trim once the card is idle: a job of this present may still name it.
static void gpu_forget(const void *addr, positive bytes)
{
        positive from = (positive)addr, to = from + bytes;

        if (!bytes)
                return;
        if (to < from)
                to = ~(positive)0;

        for (unsigned int i = 0; i < gpu_map_count; i++)
                if (from < gpu_maps[i].to && gpu_maps[i].from < to)
                {
                        gpu_gone_put(gpu_maps[i].va, gpu_maps[i].bytes, gpu_maps[i].from, gpu_maps[i].to, gpu_maps[i].handle, GPU_GONE_FORGET, gpu_maps[i].written,
                                     gpu_maps[i].dma, 0, gpu_maps[i].listed);
                        gpu_maps[i].from = gpu_maps[i].to = 0;
                }
}

static void gpu_import_mark_dead(unsigned int i)
{
        gpu.imported_dead[i] = 1, gpu.imported_holds[i] = 0;
        gpu.imported_dead_at[i] = gpu_tick, gpu.imported_dead_after[i] = gpu_submit_serial;
}

// A buffer gpu_import made, let go of by one holder for the reason why (its mode changed, it was made again, the card is idle). While another holder has it,
// it only has one holder fewer. The last one's leaves it unmapped and its handle closed, unless a job the wait gave up on may still read it (gpu_run_job's
// pending): then it is only released, and gpu_trim unmaps it once the card is idle.
static void gpu_unimport(positive at, enum gpu_gone_why why)
{
        gpu_unimport_gen(at, 0, why);
}

static void gpu_unimport_gen(positive at, unsigned int gen, enum gpu_gone_why why)
{
        for (unsigned int i = 0; at && i < gpu.imports; i++)
                if (gpu.imported_va[i] == at && (!gen || gpu.imported_gen[i] == gen))
                {
                        const struct gpu_bo bo = {at, gpu.imported_bytes[i], 0, 0, gpu.imported[i], 0, 1, 1, 1};
                        unsigned int last;

                        if (!gpu.imported_dead[i] && gpu.imported_holds[i] > 1)
                        {
                                gpu.imported_holds[i]--;
                                return;
                        }
                        if (gpu.pending && !gpu_idle())
                        {
                                gpu_import_mark_dead(i);
                                return;
                        }
                        gpu_bo_unbind_close(&bo, why);
                        last = --gpu.imports;
#define GPU_IMPORT_SWAP(name, type) gpu.name[i] = gpu.name[last];
                        GPU_IMPORT_ARRAYS(GPU_IMPORT_SWAP)
#undef GPU_IMPORT_SWAP
                        return;
                }
        if (at && gen)
                gpu_import_stale++;
}

// An import let go of by one holder: with its last, unmapped by the gpu_trim after the card is idle (a job of this present may still name it, so not now).
static void gpu_release_import(positive at)
{
        gpu_release_import_gen(at, 0);
}

static void gpu_release_import_gen(positive at, unsigned int gen)
{
        for (unsigned int i = 0; at && i < gpu.imports; i++)
                if (gpu.imported_va[i] == at && (!gen || gpu.imported_gen[i] == gen))
                {
                        if (!gpu.imported_dead[i] && gpu.imported_holds[i] > 1)
                                gpu.imported_holds[i]--;
                        else
                                gpu_import_mark_dead(i);
                        return;
                }
        if (at && gen)
                gpu_import_stale++;
}

// The arguments the kernel reads, at byte 512 of the buffer: s, d, g are GPU addresses (GPU_VA + an offset), pitches in pixels, op 0 copy
// s to d, 1 fill d with v, 2 tone v over d, 3 glass: s over g into d (v the light side, x0 y0 where the rect is, for the grain), 4 a scene, 5 with boxes (both by clang, any width), 6 by hand with or without boxes for a width and a pitch of a multiple of 4 (gpu_scene picks).
struct gpu_args { positive s, d, g; unsigned int sp, dp, gp, w, op, v, x0, y0, rows, pad; positive tab, ops; unsigned int count, pad2; };
// ops and count: a scene's records (the card's address of the first, and how many), read by the Intel scene batch (op 6) only.
#define GPU_ARGS_AT(slot) (gpu_layout.args + 256 * (positive)(slot)) // a slot's arguments, in bytes of the buffer (after every slot's packets)
static struct gpu_args gpu_slot_args[GPU_SLOTS]; // cached RAM mirror of GPU_ARGS, flushed in one sequential WC burst in gpu_submit()
#define GPU_ARGS (&gpu_slot_args[gpu_slot])      // the arguments of the slot the job is built in
typedef char gpu_slots_fit[sizeof(struct gpu_args) <= 256 ? 1 : -1]; // a slot's arguments in its 256 bytes (gpu_start puts the kernels after the last slot's)

// Gen9 R8 byte-surface row split: 32-byte aligned interior, 16-byte multiple span, 64-byte pitch.
struct gpu_i915_split { positive interior; unsigned int span, head, tail; };
static int gpu_i915_fill_split(positive dst, unsigned int span, unsigned int pitch, struct gpu_i915_split *out)
{
        positive d32, stop = dst + span, e16 = stop & ~(positive)15;

        if (!span || pitch < span || (pitch & 63))
                return -1;
        d32 = (dst + 31) & ~(positive)31;
        out->interior = d32;
        if (d32 >= e16)
                return out->span = 0, out->head = span, out->tail = 0, 0;
        out->span = (unsigned int)(e16 - d32), out->head = (unsigned int)(d32 - dst), out->tail = (unsigned int)(stop - e16);
        return 0;
}

// The state's stores: a little-endian word is copied as the host holds it. The host's order is checked here, not assumed.
#if !(defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#error "the Intel state is little endian: gpu_i915_put stores the host's word as it is"
#endif
static void gpu_i915_put(p8 address_to at, unsigned int offset, unsigned int value)
{
        memory_copy(at + offset, &value, sizeof value);
}

// Per-generation MOCS index for RENDER_SURFACE_STATE DW1[31:25]: Gen9/11 use PTE (1); Gen12 Xe-LP uses L3+LLC WB (2) / UC (3);
// DG1 uses WB (5); DG2 uses WB (3); MTL/ARL use WB (2).
static unsigned int gpu_i915_mocs(void)
{
        if (gpu.family < 1200)
                return 1u;
        if (gpu.family == 1200)
                return (gpu_backend == &gpu_i915_row ? gpu_i915.llc : 1) ? 2u : 3u;
        if (gpu.family == 1201)
                return 5u;
        if (gpu.family == 1255)
                return 3u;
        if (gpu.family >= 2000)
                return 4u;
        return 2u;
}

/*
        A surface's base word at `at` (in the card's own buffer) that names base, on a chip whose imports float (Gen7 and Gen7.5: the display shares the
        address space, so a buffer it holds is never pinned by a job, as pinning it elsewhere has eb_relocate_parse wait for an unbind that cannot
        happen): when base is inside a floating import, the word is made the import's presumed place plus base's distance into it, and a relocation of
        the word is listed for the job (gpu_i915_submit hands the list to the kernel, which writes the word again only if the import is not where it
        was presumed). A list that cannot grow refuses the job (relocs_failed).
*/
static void gpu_i915_reloc_note(p8 address_to at, positive base)
{
        if (!gpu_i915.bound || gpu_backend != &gpu_i915_row || gpu.family >= 800)
                return;
        for (unsigned int i = 0; i < gpu.imports; i++)
        {
                unsigned int h = gpu.imported[i], word;
                const struct gpu_i915_bound *b;
                positive offset = (positive)(at - (p8 address_to)gpu.at);

                if (h >= GPU_I915_HANDLES || !(b = &gpu_i915.bound[h])->floating || base < b->va || base - b->va >= b->bytes)
                        continue;
                if (!gpu.at || at < (p8 address_to)gpu.at || offset > gpu.size - 4)
                        return (void)(gpu_i915.relocs_failed = 1);
                if (32ull * (gpu_i915.relocs_count + 1) > gpu_i915.relocs_room)
                {
                        void *grown = gpu_i915.relocs;

                        if (!gpu_buf_grow(&grown, &gpu_i915.relocs_room, 64ull * (gpu_i915.relocs_count + 1), 32ull * gpu_i915.relocs_count))
                                return (void)(gpu_i915.relocs_failed = 1);
                        gpu_i915.relocs = (struct gpu_i915_reloc *)grown;
                }
                word = (unsigned int)(b->presumed + (base - b->va));
                gpu_i915.relocs[gpu_i915.relocs_count++] = (struct gpu_i915_reloc){h, (unsigned int)(base - b->va), offset, b->presumed, 2u, b->written ? 2u : 0u};
                memory_copy(at, &word, sizeof word);
                return;
        }
}

// A RENDER_SURFACE_STATE of a byte surface (R8_UNORM, 2D, the render cache read and written, the 4-row halign and valign of IGT's gen9 fill,
// generation-appropriate MOCS, the channels R4 G5 B6 A7) at state offset at: its width and rows in bytes and rows (each minus one: fields 13:0 and 29:16), its pitch
// in bytes (minus one, 17:0), and its base the card's address of its first byte.
static void gpu_i915_surface(p8 address_to state, unsigned int at, positive base, unsigned int width, unsigned int rows, unsigned int pitch)
{
        if (gpu.family < 800)
        {
                // Gen7/Gen7.5 (IVB/VLV/HSW): 32-byte RENDER_SURFACE_STATE with 32-bit address in DW1 and MOCS in DW5[19:16]:
                // bit 16 = L3 Cacheability Control (L3CC=1), bits 18:17 = 00 (Use PTE for LLC WB). Without bit 16 every EU access bypasses L3!
                gpu_i915_put(state, at + 0, (1u << 29) | (0x140u << 18) | (1u << 8));
                gpu_i915_put(state, at + 4, (unsigned int)base);
                gpu_i915_reloc_note(state + at + 4, base);
                gpu_i915_put(state, at + 8, (width - 1) | ((rows - 1) << 16));
                gpu_i915_put(state, at + 12, pitch - 1);
                gpu_i915_put(state, at + 20, 1u << 16);
                if (gpu.family == 750)
                        gpu_i915_put(state, at + 28, (4u << 25) | (5u << 22) | (6u << 19) | (7u << 16));
                return;
        }
        gpu_i915_put(state, at + 0, (1u << 29) | (0x140u << 18) | (1u << 8) | (1u << 14) | (1u << 16));
        gpu_i915_put(state, at + 4, gpu.family < 900 ? (1u << 24) : (gpu_i915_mocs() << 25)); // Gen8 MOCS in [30:24], Gen9+ in [31:25]
        gpu_i915_put(state, at + 8, (width - 1) | ((rows - 1) << 16));
        gpu_i915_put(state, at + 12, pitch - 1);
        gpu_i915_put(state, at + 28, (4u << 25) | (5u << 22) | (6u << 19) | (7u << 16));
        gpu_i915_put(state, at + 32, (unsigned int)base);
        gpu_i915_put(state, at + 36, (unsigned int)(base >> 32));
}

// INTERFACE_DESCRIPTOR_DATA at state offset 128: the kernel's start (from the instruction base, the card's buffer), single program flow, the
// binding table at sbase (the state's own offset of the table at byte 0), one URB entry read of length 1, one thread.
static void gpu_i915_descriptor(p8 address_to state, const struct gpu_job *job, unsigned int sbase)
{
        if (gpu.family < 800)
        {
                // Gen7/Gen7.5: 20-byte (5-dword) INTERFACE_DESCRIPTOR_DATA.
                gpu_i915_put(state, 128 + 0, (unsigned int)(job->code - GPU_VA));
                gpu_i915_put(state, 128 + 4, 1u << 18);
                gpu_i915_put(state, 128 + 8, 0);
                gpu_i915_put(state, 128 + 12, sbase);
                gpu_i915_put(state, 128 + 16, 1u << 16);
                return;
        }
        gpu_i915_put(state, 128 + 0, (unsigned int)(job->code - GPU_VA));
        gpu_i915_put(state, 128 + 4, 0);
        gpu_i915_put(state, 128 + 8, 1u << 18);
        gpu_i915_put(state, 128 + 12, 0);
        gpu_i915_put(state, 128 + 16, sbase);
        gpu_i915_put(state, 128 + 20, 1u << 16);
        gpu_i915_put(state, 128 + 24, 1);
        gpu_i915_put(state, 128 + 28, 0);
}

// The batch's 64 dwords, parameterized across Gen7/7.5 (2012-2013), Gen8 (2014-2015), Gen9/9LP (2015-2020), Gen11 (2019-2021),
// and Gen12+ (2020-2026): PIPELINE_SELECT, STATE_BASE_ADDRESS, MEDIA_VFE_STATE, CURBE + IDD load, GPGPU_WALKER, PIPE_CONTROL, BB_END.
static void gpu_i915_frame(unsigned int address_to words, unsigned int sbase, unsigned int cols, unsigned int rows)
{
        unsigned int n = 0;

        if (gpu.family < 800)
        {
                unsigned int fallback = gpu.family == 750 ? ((gpu.device & 0x0030u) == 0u ? 69u : 139u) : (gpu.family == 701 ? 31u : 47u);
                unsigned int threads = gpu_i915.eu_total ? gpu_i915.eu_total * 7u - 1u : fallback;

                if (threads > 139u)
                        threads = 139u;
                words[n++] = 0x69040002u;
                words[n++] = 0x61010008u; words[n++] = 0; words[n++] = (unsigned int)GPU_VA | 1; words[n++] = (unsigned int)GPU_VA | 1;
                words[n++] = 0; words[n++] = (unsigned int)GPU_VA | 1; words[n++] = 0; words[n++] = 1; words[n++] = 0; words[n++] = 1;
                words[n++] = 0x70000006u; words[n++] = 0; words[n++] = (threads << 16) | (1u << 8) | (1u << 2); words[n++] = 0; words[n++] = 1; words[n++] = 0; words[n++] = 0; words[n++] = 0;
                words[n++] = 0x70010002u; words[n++] = 0; words[n++] = 64; words[n++] = sbase + 192;
                words[n++] = 0x70020002u; words[n++] = 0; words[n++] = 20; words[n++] = sbase + 128;
                words[n++] = 0x71050009u; words[n++] = 0; words[n++] = 1u << 30; words[n++] = 0; words[n++] = cols; words[n++] = 0; words[n++] = rows;
                words[n++] = 0; words[n++] = 1; words[n++] = 0xffffu; words[n++] = 0xffffffffu;
                words[n++] = 0x7a000003u; words[n++] = (1u << 12) | (1u << 20) | (1u << 5); words[n++] = 0; words[n++] = 0; words[n++] = 0;
                words[n++] = 0x05000000u;
                memory_zero(words + n, (64 - n) * sizeof words[0]);
                return;
        }
        words[n++] = gpu.family < 900 ? 0x69040002u : 0x69040302u;
        words[n++] = gpu.family < 900 ? 0x6101000eu : gpu.family >= 1200 ? 0x61010014u : 0x61010011u;
        words[n++] = 1; words[n++] = 0; words[n++] = 1;
        words[n++] = (unsigned int)GPU_VA | 1; words[n++] = (unsigned int)(GPU_VA >> 32);
        words[n++] = (unsigned int)GPU_VA | 1; words[n++] = (unsigned int)(GPU_VA >> 32);
        words[n++] = 0; words[n++] = 0;
        words[n++] = (unsigned int)GPU_VA | 1; words[n++] = (unsigned int)(GPU_VA >> 32);
        words[n++] = 0xfffff000u | 1; words[n++] = 65536u | 1; words[n++] = 0xfffff000u | 1; words[n++] = 65536u | 1;
        if (gpu.family >= 900)
                words[n++] = 1, words[n++] = 0, words[n++] = 0xfffff000u;
        if (gpu.family >= 1200)
                words[n++] = 1, words[n++] = 0, words[n++] = 0; // Bindless Sampler State Base Address + Size (Gen12+)
        {
                unsigned int tpeu = 7u;
                unsigned int max_threads = gpu.family >= 1200 ? 671u : gpu.family >= 1100 ? 447u : 167u;
                unsigned int eu = gpu_backend == &gpu_i915_row ? gpu_i915.eu_total : 0u;
                unsigned int threads = eu ? eu * tpeu - 1u : 63u;

                if (threads > max_threads)
                        threads = max_threads;
                words[n++] = 0x70000007u; words[n++] = 0; words[n++] = 0; words[n++] = (threads << 16) | (1u << 8); words[n++] = 0; words[n++] = 1; words[n++] = 0; words[n++] = 0; words[n++] = 0;
        }
        words[n++] = 0x70010002u; words[n++] = 0; words[n++] = 64; words[n++] = sbase + 192;
        words[n++] = 0x70020002u; words[n++] = 0; words[n++] = 32; words[n++] = sbase + 128;
        words[n++] = 0x7105000du; words[n++] = 0; words[n++] = 0; words[n++] = 0; words[n++] = 1u << 30;
        words[n++] = 0; words[n++] = 0; words[n++] = cols; words[n++] = 0; words[n++] = 0; words[n++] = rows; words[n++] = 0; words[n++] = 1;
        words[n++] = 0xffffu; words[n++] = 0xffffffffu;
        // PIPE_CONTROL: Render Target Cache Flush (bit 12) + CS Stall (bit 20) + HDC Flush (Gen12 bit 9) / DC Flush (Gen9-11 bit 5).
        words[n++] = 0x7a000004u;
        words[n++] = (1u << 12) | (1u << 20) | (gpu.family >= 1200 ? (1u << 9) : (1u << 5));
        words[n++] = 0; words[n++] = 0; words[n++] = 0; words[n++] = 0;
        words[n++] = 0x05000000u;
        memory_zero(words + n, (64 - n) * sizeof words[0]);
}

// A job's batch arguments, read through the state's mapping (their card address is the state's plus their distance from it), when the batch can be
// written: room for its 64 words, and the state and the code on the card (the code 64-byte aligned). Null otherwise: the processor draws the job.
static const struct gpu_args address_to gpu_i915_args(const struct gpu_job *job, unsigned int room)
{
        if (room < 64 || !job->state_at || job->state < GPU_VA || job->code < GPU_VA || ((job->code - GPU_VA) & 63) ||
            job->arguments < GPU_VA || job->arguments - GPU_VA > gpu.size || sizeof(struct gpu_args) > gpu.size - (job->arguments - GPU_VA))
                return 0;
        return (const struct gpu_args address_to)((p8 address_to)job->state_at + (long)(job->arguments - job->state));
}

/*
        The Intel scale (op 7, the kernel gpu_i915_scale): d[y * dp + x] = s[ytab[y] * sp + xtab[x]] for x below w and y below the rows, 4 bytes a pixel,
        nearest, the tables the processor made at a->g (w entries of xtab, then the rows' ytab, each the source's index). Four pixels a thread, the walker
        ceil(w / 4) by rows. The rows are taken only at 64-byte pitches, as the Intel encoders take them (the card loses rows at others): a->sp and a->dp
        (words, in pixels) are multiples of 16, and a->dp is not below w. The kernel itself reads and writes any pitch (measured: 37 by 23 at 40 and 104
        pixels, sources of 9, 40 and 1000 pixels), so the rule is the encoders'. Refused, 0 words (the processor draws it): a base not 16-byte aligned or
        below GPU_VA, w or rows or dp or sp past 65535 (rows 16384), w 0, a destination of more than 2^26 pixels (dp times the rows), and the usual room and
        kernel checks. The source's own height is not seen here: its pixels are indexed by the tables, so a source of more than 2^26 pixels is the caller's
        to keep off the card.
*/
// Shared Gen9 compute batch emitter: zeros state, populates binding table entries 0..nsurf-1 and surface states, writes descriptor, CURBE, and 56-dword batch.
struct gpu_i915_surf { positive base; unsigned int width, rows, pitch; };
static unsigned int gpu_i915_emit_batch(const struct gpu_job *job, unsigned int address_to words, const struct gpu_i915_surf surfs[3],
                                        unsigned int nsurf, const unsigned int curbe[4], unsigned int ncurbe, unsigned int cols, unsigned int rows)
{
        static const unsigned int off[3] = {64, 256, 320};
        p8 address_to state = (p8 address_to)job->state_at;
        unsigned int sbase = (unsigned int)(job->state - GPU_VA);

        memory_zero(state, 512);
        gpu_i915_put(state, 0, sbase + 64);
        gpu_i915_put(state, 4, sbase + 256);
        if (nsurf >= 3)
                gpu_i915_put(state, 8, sbase + 320);
        for (unsigned int i = 0; i < nsurf; i++)
                if (surfs[i].width)
                        gpu_i915_surface(state, off[i], surfs[i].base, surfs[i].width, surfs[i].rows, surfs[i].pitch);
        gpu_i915_descriptor(state, job, sbase);
        for (unsigned int i = 0; i < ncurbe; i++)
                gpu_i915_put(state, 192 + 4 * i, curbe[i]);
        gpu_i915_frame(words, sbase, cols, rows);
        return 64;
}

static unsigned int gpu_i915_scale_batch(const struct gpu_job *job, unsigned int address_to words, unsigned int room)
{
        const struct gpu_args address_to a = gpu_i915_args(job, room);

        if (!a || !a->w || a->w > 65535u || !a->rows || a->rows > 16384u || !a->sp || a->sp > 65535u || !a->dp || a->dp > 65535u || (a->dp & 15) || (a->sp & 15) || a->dp < a->w ||
            (positive)a->dp * a->rows > (1ull << 26) || a->s < GPU_VA || a->d < GPU_VA || a->g < GPU_VA || (a->s & 15) || (a->d & 15) || (a->g & 15))
                return 0;
        {
                const struct gpu_i915_surf surfs[3] = {{a->d, 16384, 16384, 16384}, {a->s, 16384, 16384, 16384}, {a->g, 16384, 16384, 16384}};
                const unsigned int curbe[4] = {a->sp, a->w, a->dp, 0};

                return gpu_i915_emit_batch(job, words, surfs, 3, curbe, 3, (a->w + 3) / 4, a->rows);
        }
}

// The scene (op 4, gpu_i915_cp.iga): the picture a->w pixels by a->gp rows (the scene's w and h), a->dp pixels a row (a multiple of 16, at least w), at
// a->d (32-byte aligned), with the bins at a->g (64-byte aligned): the table of cw * ch cells and the lists after it, as gpu_scene_build lays them. One
// thread a 16-byte block, the walker's groups the blocks of a row and the rows. Refused (0 words: the processor draws the scene) for a pitch that is not a
// multiple of 16 pixels (64 bytes: rows that share a 64-byte line raced on the card), a picture past the surface fields (w 4096,
// rows 16384, a pitch of 65536 pixels), a destination or bins not aligned as above.
static unsigned int gpu_i915_cp_batch(const struct gpu_job *job, unsigned int address_to words, unsigned int room)
{
        const struct gpu_args address_to a = gpu_i915_args(job, room);
        unsigned int blocks, cw, ch;

        if (!a || a->x0 || a->y0 || !a->w || !a->gp || a->w > 4096u || a->gp > 16384u || !a->dp || a->dp > 65536u || (a->dp & 15) || a->dp < a->w ||
            a->d < GPU_VA || (a->d & 31) || a->g < GPU_VA || (a->g & 63))
                return 0;
        blocks = (a->w + 3) >> 2, cw = (a->w + 63) >> 6, ch = (a->gp + 63) >> 6;
        {
                const struct gpu_i915_surf surfs[3] = {{a->d, 16u * blocks, a->gp, 4u * a->dp}, {a->g, 16384, 16384, 16384}, {0, 0, 0, 0}};
                const unsigned int curbe[4] = {a->w, cw, 8u * cw * ch, 0};

                return gpu_i915_emit_batch(job, words, surfs, 2, curbe, 3, blocks, a->gp);
        }
}

/*
        The Intel scene (op 6, the kernel gpu_i915_scene: fill 0, tone over 1, image copy 4 and premultiplied image over 11, the images with an
        optional 0x80 R/B swap, drawn in order over each 8 by 8 tile of the damage, a thread a tile). The damage is the patch the arguments name
        (a->x0 patches of 64 pixels, a->y0 of 4 rows, a->w by a->rows as gpu_scene_build sets them) clipped to the picture (a->sp by a->gp); the
        records are a->count of 8 words at a->ops (64-byte aligned, inside the buffer). The batch clips each record to the damage, passes over
        those that miss it and those a later fill or copy covering all of it hides, and packs the rest a 64-byte row each at the slot's part of
        the side's bins (a->g, GPU_I915_SCENE_SIDE bytes a slot) for binding table entry 1. An image is given a surface of its own at the top of
        that part (based at the 64-byte line of its first pixel in the damage, at its own pitch), its entry in the record's word 7 and its x and
        row bias in words 5 and 6 (the kernel reads byte x = word 5 + 4 x of row word 6 + y). The binding table is the slot state's from
        GPU_I915_SCENE_TABLE: entry 0 the damage (32-byte aligned, rows of whole 16-byte groups), 1 the records, 2 on the images. Refused (0 words:
        the processor draws the scene): a kind other than those, a damage whose width is not a multiple of 4 pixels or past 4096, more images
        than the send's 8-bit entry reaches (GPU_I915_SCENE_IMAGES), more records than the part holds, an image past the surface fields.
*/
#define GPU_I915_SCENE_TABLE 512u                               // the scene's binding table in a slot's state
#define GPU_I915_SCENE_IMAGES 238u                              // entries 2 to 239: a message's binding table index is 8 bits, 240 and up are not surfaces
#define GPU_I915_SCENE_SIDE (256u << 10)                        // a slot's part of the side's bins: the packed records, then the images' surfaces at its top
#define GPU_I915_SCENE_SURFACES (GPU_I915_SCENE_SIDE - 64u * GPU_I915_SCENE_IMAGES)
typedef char gpu_i915_scene_table_fits[GPU_I915_SCENE_TABLE + 4u * (2u + GPU_I915_SCENE_IMAGES) <= GPU_I915_STATE ? 1 : -1];
static unsigned int gpu_i915_scene_batch(const struct gpu_job *job, unsigned int address_to words, unsigned int room)
{
        const struct gpu_args address_to a = gpu_i915_args(job, room);
        unsigned int px0, py0, px1, py1, kept = 0, images = 0, sbase;
        positive base, pack_va;
        p8 address_to state, address_to pack;
        const unsigned int address_to rec;

        if (!a || !a->count || a->count > 16384u || a->ops < GPU_VA || (a->ops & 63) || !a->sp || (a->sp & 3) || !a->gp || a->gp > 16384u ||
            !a->dp || a->dp > 65536u || (a->dp & 15) || a->dp < a->sp || a->x0 > 65535u || a->y0 > 65535u || !a->w || !a->rows || a->d < GPU_VA ||
            a->g < GPU_VA || (a->g & 63))
                return 0;
        // The records, and the slot's part of the bins they are packed into, are read and written through the CPU mapping: they must lie inside it.
        pack_va = a->g + (gpu_slot ? GPU_I915_SCENE_SIDE : 0u);
        if (a->ops - GPU_VA > gpu.size || 32ull * a->count > gpu.size - (a->ops - GPU_VA) || pack_va - GPU_VA > gpu.size ||
            GPU_I915_SCENE_SIDE > gpu.size - (pack_va - GPU_VA))
                return 0;
        px0 = a->x0 << 6, py0 = a->y0 << 2;
        px1 = (positive)px0 + (positive)a->w < a->sp ? px0 + a->w : a->sp;
        py1 = (positive)py0 + 4 * (positive)a->rows < a->gp ? py0 + 4 * a->rows : a->gp; // in 64 bits: 4 * a->rows wraps at 2^30 rows
        if (px0 >= px1 || py0 >= py1 || (px1 - px0) > 4096u || ((px1 - px0) & 3))
                return 0;
        base = a->d + 4 * ((positive)py0 * a->dp + px0);
        if (base & 31)
                return 0;
        rec = (const unsigned int address_to)((p8 address_to)job->state_at + ((long)a->ops - (long)job->state));
        pack = (p8 address_to)job->state_at + ((long)pack_va - (long)job->state);
        for (unsigned int i = 0; i < a->count; i++)
        {
                unsigned int r[16] = {0}, k;
                long long rx0, ry0, cx0, cy0, cx1, cy1;

                memory_copy(r, rec + 8 * i, 32);
                k = r[0] & ~0x80u;
                if (r[0] > 1u && k != 4u && k != 11u)
                        return 0;
                rx0 = (int)r[1], ry0 = (int)r[2], cx0 = rx0 > (long long)px0 ? rx0 : (long long)px0, cy0 = ry0 > (long long)py0 ? ry0 : (long long)py0;
                cx1 = (int)r[3] < (long long)px1 ? (int)r[3] : (long long)px1, cy1 = (int)r[4] < (long long)py1 ? (int)r[4] : (long long)py1;
                if (cx1 <= cx0 || cy1 <= cy0)
                        continue;
                // A fill or a copy over the whole damage hides every record before it.
                if ((r[0] == 0u || k == 4u) && cx0 == px0 && cy0 == py0 && cx1 == px1 && cy1 == py1)
                        kept = 0, images = 0;
                if (64ull * (kept + 1) > GPU_I915_SCENE_SURFACES)
                        return 0;
                r[1] = (unsigned int)cx0, r[2] = (unsigned int)cy0, r[3] = (unsigned int)cx1, r[4] = (unsigned int)cy1;
                if (k == 4u || k == 11u)
                {
                        // The image's first pixel in the damage, the 64-byte line it is on, and the surface from there: as wide as the record's part of
                        // the damage, its rows, at the picture's own pitch (r[7] pixels).
                        positive va = (positive)r[5] | ((positive)r[6] << 32), at, line;
                        unsigned int rp = r[7], xo, width, rows = (unsigned int)(cy1 - cy0);
                        p8 address_to surface = pack + GPU_I915_SCENE_SURFACES + 64u * images;

                        if (!rp || rp > 65535u || va < GPU_VA || (va & 3) || images == GPU_I915_SCENE_IMAGES)
                                return 0;
                        at = va + 4ull * ((positive)(cy0 - ry0) * rp + (positive)(cx0 - rx0)), line = at & ~63ull;
                        xo = (unsigned int)(at & 63), width = xo + 4u * (unsigned int)(cx1 - cx0);
                        if (at < va || line < GPU_VA || line > GPU_VA_END || width > 16384u || rows > 16384u)
                                return 0;
                        memory_zero(surface, 64);
                        gpu_i915_surface(surface, 0, line, width, rows, 4u * rp);
                        r[5] = xo - 4u * (unsigned int)cx0, r[6] = 0u - (unsigned int)cy0, r[7] = 2u + images++;
                }
                memory_copy(pack + 64u * kept++, r, 64);
        }
        if (!kept)
        {
                words[0] = 0x05000000u, words[1] = 0u;
                return 2;
        }
        // The state as gpu_i915_emit_batch lays it (the damage's surface at 64, the records' at 256, the descriptor at 128, the CURBE at 192), with
        // the binding table at GPU_I915_SCENE_TABLE: the damage, the records, the images' surfaces (their card offsets, as the table holds them).
        state = (p8 address_to)job->state_at, sbase = (unsigned int)(job->state - GPU_VA);
        memory_zero(state, GPU_I915_SCENE_TABLE + 4u * (2u + images));
        gpu_i915_put(state, GPU_I915_SCENE_TABLE, sbase + 64), gpu_i915_put(state, GPU_I915_SCENE_TABLE + 4, sbase + 256);
        for (unsigned int j = 0; j < images; j++)
                gpu_i915_put(state, GPU_I915_SCENE_TABLE + 8 + 4 * j, (unsigned int)(pack_va - GPU_VA) + GPU_I915_SCENE_SURFACES + 64u * j);
        gpu_i915_surface(state, 64, base, 4u * (px1 - px0), py1 - py0, 4u * a->dp);
        gpu_i915_surface(state, 256, pack_va, 64, kept, 64);
        gpu_i915_descriptor(state, job, sbase + GPU_I915_SCENE_TABLE);
        gpu_i915_put(state, 192, kept), gpu_i915_put(state, 196, px0), gpu_i915_put(state, 200, py0);
        gpu_i915_put(state, 204, px1), gpu_i915_put(state, 208, py1);
        gpu_i915_frame(words, sbase, (px1 - px0 + 7) / 8, (py1 - py0 + 7) / 8);
        return 64;
}

// The batch for one job, by op: 0 copy, 1 fill, 2 tone and 3 glass take the byte surface (the whole rows, or refused: the processor draws a
// refused job), 4 the scene's cp, 6 the scene and 7 the scale have encoders of their own (above). The state region holds the binding table (entry 0
// the destination's surface state, entry 1 the source's, entry 2 the glass ground's), the surface states, the interface descriptor and at 192 the colour
// dword (or, for a glass, the grain's origin); the card's buffer at GPU_VA is the base of every pointer. Writes words[0..56) and job->state_at[0..360):
// the state's room. 0 words is a refusal.
static unsigned int gpu_i915_gen9_batch(const struct gpu_job *job, unsigned int address_to words, unsigned int room)
{
        // The arguments are in the same mapped buffer as the state: their CPU address is the state's, plus their card distance from it.
        const struct gpu_args address_to a = gpu_i915_args(job, room);
        struct gpu_i915_split s;
        unsigned int pitch, span, spitch, gpitch;

        if (!a)
                return 0;
        if (a->op == 4)
                return gpu_i915_cp_batch(job, words, room);
        if (a->op == 6)
                return gpu_i915_scene_batch(job, words, room);
        if (a->op == 7)
                return gpu_i915_scale_batch(job, words, room);
        // The surface's fields are 14 bits (width and height, minus one, in 13:0 and 29:16) and the pitch 18: a row of more than 4096 pixels
        // (16384 bytes), more than 16384 rows or a pitch over 256 KiB is refused, not wrapped.
        if (a->op > 3 || ((a->x0 || a->y0) && a->op != 3) || !a->w || !a->rows || a->w > 4096u || a->rows > 16384u || a->dp > 65536u || a->d < GPU_VA)
                return 0;
        span = 4u * a->w;
        pitch = 4u * a->dp;
        // Whole or nothing: a fill whose edges the card cannot write (a row's head or tail, or a span under 16 bytes) is refused, and the
        // processor draws all of it.
        if (gpu_i915_fill_split(a->d, span, pitch, &s) || !s.span || s.head || s.tail)
                return 0;     // refused, or not a whole fill on the card: the processor draws it
        // A copy's source (and a glass's picture): the row's bytes read from a 32-byte-aligned base, at a pitch that holds the row in whole 64-byte
        // groups (the rule the card keeps for every surface a batch reads or writes).
        spitch = 4u * a->sp;
        if ((a->op == 0 || a->op == 3) && (a->s < GPU_VA || (a->s & 31) || !a->sp || a->sp > 65536u || spitch < span || (spitch & 63)))
                return 0;
        // A glass's ground: the same rules as its picture.
        gpitch = 4u * a->gp;
        if (a->op == 3 && (a->g < GPU_VA || (a->g & 31) || !a->gp || a->gp > 65536u || gpitch < span || (gpitch & 63)))
                return 0;
        {
                const struct gpu_i915_surf surfs[3] = {
                    {s.interior, s.span, a->rows, pitch},
                    {a->s, (a->op == 0 || a->op == 3) ? s.span : 0u, a->rows, spitch},
                    {a->g, a->op == 3 ? s.span : 0u, a->rows, gpitch}};
                const unsigned int curbe[4] = {a->v, a->x0, a->y0, 0};

                return gpu_i915_emit_batch(job, words, surfs, a->op == 3 ? 3u : 2u, curbe, a->op == 3 ? 3u : 1u, s.span / 16, a->rows);
        }
}

// The set as the table will hold it, once registered: the eight ops in the order of gpu_kernels (copy, fill, tone, glass, cp, bx, scene, scale),
// the kernels without words marked empty. bx (op 5) has words but no encoder yet, so its slot stays empty. Not in GPU_I915_SETS.
static const struct gpu_code gpu_i915_gen9_kernels[] = {
        {gpu_i915_copy, sizeof(gpu_i915_copy)}, {gpu_i915_fill, sizeof(gpu_i915_fill)}, {gpu_i915_tone, sizeof(gpu_i915_tone)}, {gpu_i915_glass, sizeof(gpu_i915_glass)}, {gpu_i915_cp, sizeof(gpu_i915_cp)}, {0, 0}, {gpu_i915_scene, sizeof(gpu_i915_scene)}, {gpu_i915_scale, sizeof(gpu_i915_scale)}};
// The Intel set takes the AMD set's self-test hash: it is registered only when its kernels draw the same pixels as the AMD ones (bit-exact,
// the rule every set meets), so one picture stands for both. A set whose picture differs is not registered (the self-test would refuse it).
static const struct gpu_set gpu_i915_gen9_set = {gpu_i915_gen9_kernels, 8, 0, GPU_TEST_HASH, gpu_i915_gen9_batch};

// The bins sides of the scenes built without a wait (gpu_scene_submit): the last job that reads each (0 none), the side and the place of the last scene
static struct { positive job[2]; unsigned int side; } gpu_bins;
static void gpu_bins_forget(void)
{
        memory_zero(&gpu_bins, sizeof(gpu_bins));
}

/*
        Whether a job (its sequence, as gpu_submit answered it) is done, without waiting: 1 done, and every job before it (the ring runs them in order); 0
        still running; -1 its fence answered an error (the job ended by the queue's reset, or the context the reset left guilty refuses it): it is over, and so
        is every job of its context, the jobs after go to a context of their own, and a job whose end was not said has it said by gpu_late_end (WAIT). A
        sequence no pending job has (0, a job seen done, one of a context let go of) is done. While a job may run nothing it reads may be written (its slot's
        arguments and packets, the lists it draws: a wave that starts late reads them as they are then), so the compositor draws on the processor.
*/
static int gpu_done(positive sequence)
{
        int ended;

        if (!sequence || (sequence != gpu.pending && sequence != gpu_before.sequence))
                return 1;
        ended = gpu_backend->wait(sequence, 0); // a time long past: the kernel looks and does not wait
        if (ended > 0)
        {
                if (sequence == gpu.pending)
                        gpu.pending = 0;
                gpu_before.sequence = 0, gpu_last_job = clock_monotonic_nanoseconds();
        }
        else if (ended < 0)
        {
                if (!gpu_end_said)
                        gpu_found = GPU_END_WAIT, gpu_end_said = 1;
                // No new context either: the descriptor answers neither the wait nor the context (the device is gone, or its reset left it dead), so the job can
                // never be asked after again and the buffer would be held for good. The card is lost: the caller starts it again (gpu_recover closes the
                // descriptor, and the kernel ends what ran on it), and nothing is written here until it has.
                if (gpu_backend->context(&gpu.ctx, gpu.ctx) != 0)
                        gpu_lost = 1;
                else
                        gpu.pending = 0, gpu_before.sequence = 0, gpu_bins.job[0] = gpu_bins.job[1] = 0;
        }
        return ended;
}

// Whether the buffer is the processor's again: no job is pending (the last one submitted is done, or over with its context).
static b32 gpu_idle(void)
{
        return !gpu.pending || (gpu_done(gpu.pending) != 0 && !gpu.pending);
}

// The slot the next job is built in: the first when no job is pending, else the one the pending job is not in, once the job before it (in that slot) is
// done. -1 while it is not: nothing of that slot may be written yet.
static int gpu_slot_take(void)
{
        if (gpu.pending && gpu_before.sequence && gpu_done(gpu_before.sequence) == 0)
                return -1;
        return (int)(gpu_slot = gpu.pending ? gpu_pending_slot ^ 1u : 0);
}

// A job's sync_file (AMDGPU_FENCE_TO_HANDLE): a descriptor that polls readable when the job is done, for a loop that waits on its other descriptors too; the
// caller closes it. -errno for none (EINVAL for no job, or one the kernel does not know).
static int gpu_fence_fd(positive sequence)
{
        int fd = -1;
        unsigned int e = sequence ? gpu_backend->fence(sequence, &fd) : 22;

        return e ? -(int)e : fd;
}

// Whether the bo list of the last submit had a map (its listed is that submit's serial), in the words of the line.
static const char *gpu_listed_words(unsigned int listed)
{
        return listed && listed == gpu_submit_serial ? ", listed in the job's bo list" : ", missing from the job's bo list";
}

static void gpu_fault_say(positive at)
{
        const unsigned int room = sizeof(gpu_fault);
        unsigned int n = gpu_text_put(gpu_fault, 0, room, "card faulted reading 0x");

        n = gpu_text_put(gpu_fault, gpu_text_num(gpu_fault, n, room, at, 16), room, ": ");
        gpu_fault_import = 0;
        for (unsigned int i = 0; i < gpu_map_count; i++)
                if (gpu_bo_contains(gpu_maps[i].va, gpu_maps[i].bytes, at))
                {
                        n = gpu_text_num(gpu_fault, gpu_text_put(gpu_fault, n, room, "held map, handle "), room, gpu_maps[i].handle, 10);
                        n = gpu_text_put(gpu_fault, n, room, gpu_maps[i].dma ? ", dma-buf, " : ", userptr, ");
                        n = gpu_text_put(gpu_fault, n, room, gpu_maps[i].written ? "written, from 0x" : "read, from 0x");
                        n = gpu_text_num(gpu_fault, n, room, gpu_maps[i].from, 16);
                        n = gpu_text_num(gpu_fault, gpu_text_put(gpu_fault, n, room, " to 0x"), room, gpu_maps[i].to, 16);
                        n = gpu_text_put(gpu_fault, n, room, gpu_maps[i].from || gpu_maps[i].to ? ", used " : ", forgotten, used ");
                        n = gpu_text_put(gpu_fault, gpu_text_num(gpu_fault, n, room, gpu_tick - gpu_maps[i].used, 10), room, " ticks ago");
                        gpu_text_put(gpu_fault, n, room, gpu_listed_words(gpu_maps[i].listed));
                        return;
                }
        for (unsigned int i = 0; i < gpu.imports; i++)
                if (gpu_bo_contains(gpu.imported_va[i], gpu.imported_bytes[i], at))
                {
                        gpu_fault_import = gpu.imported_va[i];
                        n = gpu_text_num(gpu_fault, gpu_text_put(gpu_fault, n, room, "held import, handle "), room, gpu.imported[i], 10);
                        gpu_text_put(gpu_fault, n, room, gpu.imported_dead[i] ? ", released" : ", live");
                        return;
                }
        for (unsigned int k = 0; k < GPU_GONE && k < gpu_gone_at; k++)
        {
                unsigned int slot = (gpu_gone_at - 1 - k) % GPU_GONE;

                if (gpu_gone[slot].bytes && at >= gpu_gone[slot].va && at - gpu_gone[slot].va < gpu_gone[slot].bytes)
                {
                        gpu_fault_import = gpu_gone[slot].import ? gpu_gone[slot].va : 0;
                        n = gpu_text_put(gpu_fault, n, room, gpu_gone[slot].import ? "import handle " : "map handle ");
                        n = gpu_text_num(gpu_fault, n, room, gpu_gone[slot].handle, 10);
                        n = gpu_text_num(gpu_fault, gpu_text_put(gpu_fault, n, room, " dropped "), room, gpu_tick - gpu_gone[slot].tick, 10);
                        n = gpu_text_put(gpu_fault, gpu_text_put(gpu_fault, n, room, " ticks ago "), room, gpu_gone_words[gpu_gone[slot].why]);
                        gpu_text_put(gpu_fault, n, room, gpu_gone[slot].import ? "" : gpu_listed_words(gpu_gone[slot].listed));
                        return;
                }
        }
        for (unsigned int i = 0; i < gpu.imports; i++)
                if (gpu.imported_bytes[i] &&
                    at >= (gpu.imported_va[i] > GPU_IMPORT_GUARD ? gpu.imported_va[i] - GPU_IMPORT_GUARD : 0) &&
                    at < gpu.imported_va[i] + gpu.imported_bytes[i] + GPU_IMPORT_GUARD)
                {
                        gpu_fault_import = gpu.imported_va[i];
                        n = gpu_text_num(gpu_fault, gpu_text_put(gpu_fault, n, room, "guard band of held import, handle "), room, gpu.imported[i], 10);
                        gpu_text_put(gpu_fault, n, room, gpu.imported_dead[i] ? ", released" : ", live");
                        return;
                }
        gpu_text_put(gpu_fault, n, room, "in no map or import the table ever made");
}

// One kernel log record ("prio,seq,usec,flags;text") at the time usec: the usec field, as the kernel log clock's microseconds since boot.
// The number in field (from 0) of a kernel log record's head, before its ';' ("prio,seq,usec,flags"): 0 for none.
static positive gpu_kmsg_field(const char *record, unsigned int field)
{
        positive value = 0;
        unsigned int commas = 0;

        for (const char *at = record; *at && *at != ';'; at++)
        {
                if (*at == ',')
                        commas++;
                else if (commas == field && *at >= '0' && *at <= '9')
                        value = value * 10 + (positive)(*at - '0');
        }
        return value;
}
#define gpu_kmsg_seq(record) gpu_kmsg_field(record, 1)
#define gpu_kmsg_usec(record) gpu_kmsg_field(record, 2) // the record's time: the kernel log clock's microseconds since boot

static b32 gpu_kmsg_seq_ready;
static positive gpu_kmsg_floor_seq, gpu_kmsg_seen_seq, gpu_kmsg_last_since;

/*
        The log kept open at its end from the card's start (gpu_kmsg_tail): a fault's read (gpu_fault_read) reads only the records written since its
        last read, from the newest it had, and keeps the first fault after the floor across its reads (gpu_fault_pending), where it opened the log
        and walked it from the oldest record every poll, at most the oldest 8192 of them, so a log larger than that never had its newest read (E-h4-2).
*/
static long gpu_kmsg_tail = -1;
static b32 gpu_fault_pending_new; // the tail was opened again: what the reads found before is another log's

// The tail's next record, or 0 once there is none now (EAGAIN): the log stays open, at its end.
static char *gpu_kmsg_more(unsigned int *records)
{
        return gpu_kmsg_read(&gpu_kmsg_tail, records, false);
}

static void gpu_kmsg_drain(void)
{
        unsigned int records = 0;
        char *record;

        if (gpu_kmsg_tail >= 0)
                system_close(gpu_kmsg_tail);
        gpu_kmsg_tail = gpu_kmsg_open();
        gpu_fault_pending_new = 1;
        gpu_kmsg_last_since = 0, gpu_kmsg_seq_ready = gpu_kmsg_tail >= 0;
        // Read to the end, which is where the tail stays (a log of more than 8192 records then: the reads after go on from where this one stopped).
        while ((record = gpu_kmsg_more(&records)))
        {
                positive seq = gpu_kmsg_seq(record);

                gpu_kmsg_seen_seq = seq > gpu_kmsg_seen_seq ? seq : gpu_kmsg_seen_seq;
        }
        if (gpu_kmsg_seq_ready)
                gpu_kmsg_floor_seq = gpu_kmsg_seen_seq;
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
static struct gpu_fault_scan gpu_fault_pending; // the first fault after the floor that the tail's reads have found (gpu_fault_read)

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
        unsigned int records = 0;
        long handle;
        char *record;

        memory_zero(out, sizeof(*out));
        GPU_LOG_FAKE(since, out);
        if (gpu_fault_pending_new)
                memory_zero(&gpu_fault_pending, sizeof gpu_fault_pending), gpu_fault_pending_new = 0;
        if (gpu_kmsg_seq_ready && since != gpu_kmsg_last_since)
        {
                if (gpu_kmsg_last_since)
                        gpu_kmsg_floor_seq = gpu_kmsg_seen_seq, memory_zero(&gpu_fault_pending, sizeof gpu_fault_pending);
                gpu_kmsg_last_since = since;
        }
        // The tail: the records since the last read, each after the floor read into what this floor's reads have found; that is the answer.
        if (!out->have_at && gpu_kmsg_tail >= 0 && gpu_kmsg_seq_ready)
        {
                while ((record = gpu_kmsg_more(&records)))
                {
                        char *text = (char *)string_find((string_address)record, (string_address) ";");
                        positive seq = gpu_kmsg_seq(record);

                        gpu_kmsg_seen_seq = seq > gpu_kmsg_seen_seq ? seq : gpu_kmsg_seen_seq;
                        if (text && seq > gpu_kmsg_floor_seq)
                                gpu_fault_line(text + 1, &gpu_fault_pending);
                }
                memory_copy(out, &gpu_fault_pending, sizeof(*out));
                gpu_fault_query(out);
                return;
        }
        handle = out->have_at ? -1 : gpu_kmsg_open();
        while ((record = gpu_kmsg_next(&handle, &records)))
        {
                char *text;
                positive seq, ns;

                seq = gpu_kmsg_seq(record);
                if (seq > gpu_kmsg_seen_seq)
                        gpu_kmsg_seen_seq = seq;
                text = (char *)string_find((string_address)record, (string_address) ";");
                ns = gpu_kmsg_usec(record) * 1000;
                if (!text || (gpu_kmsg_seq_ready ? seq <= gpu_kmsg_floor_seq : ns + 2000000000ull < since))
                        continue;
                gpu_fault_line(text + 1, out);
        }
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
                gpu_fault_import = 0;
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
                n = gpu_text_put(gpu_fault, n, room, "; the same fault as the last one read"), gpu_fault_import = 0; // not this job's: nothing to blame
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

// The pending job waited for, in one wait that ends when the job does, until it is done or cap has passed since it was submitted (gpu_job_began). True when it is
// done, and every job before it. Otherwise gpu_end says how it was given up on: LATE (the cap was a tick: the job stays pending, its end not said), HUNG (the cap
// was its deadline), WAIT (the wait itself failed). A hang or a failed wait is said here, read in the kernel's log at once (gpu_fault), and is the caller's to take.
static b32 gpu_wait_job(positive cap)
{
        for (;;)
        {
                positive until = gpu_job_began + cap;
                int ended;

                // One wait, to the cap: the kernel wakes it when the job ends, so a job is one request to wait for however long it runs (in slices of four
                // milliseconds a job of 5.5 ms, the heaviest measured, was two).
                {
                        unsigned int wsec = (unsigned int)(cap / 1000000000ull) + 2u;

                        gpu_alarm_arm(wsec > 6u ? 6u : wsec);
                        ended = gpu_backend->wait(gpu.pending, until);
                        gpu_alarm_disarm();
                }
                if (ended < 0)
                        gpu_end = GPU_END_WAIT, gpu_last_error = 5; // EIO: the wait itself failed
                else if (ended)
                        return gpu.pending = 0, gpu_before.sequence = 0, gpu_last_job = clock_monotonic_nanoseconds(), true;
                else if (clock_monotonic_nanoseconds() < until)
                {
                        system_call_2(syscall(sched_yield), 0, 0);
                        continue;
                }
                else if (cap >= GPU_HANG_NS)
                        gpu_end = GPU_END_HUNG, gpu_last_error = 62; // ETIME: still running at its deadline
                else
                        return gpu_end = GPU_END_LATE, false;
                gpu_end_said = 1, gpu_job_error = gpu_last_error, gpu_fault_note(true);
                return false;
        }
}

/*
        The end of a pending job whose end was not said, said once to the caller (the compositor asks at each present): GPU_END_NONE while the job is within
        bounds, or ended without trouble. Its end is a bad one when its fence answers an error (gpu_idle comes on it: WAIT), when it is past its deadline and not
        done (HUNG: the oldest job the ring has not done is the one it runs, so the deadline is that one's), or when the kernel's log has a page fault since it was
        submitted (HUNG; the log is read from GPU_FAULT_FIRST after a tick, then every GPU_FAULT_EVERY, so a frame does not pay for it). Each is read in the
        kernel's log and said in gpu_fault; gpu_end and gpu_last_error say it as a job that failed in the call would.
*/
static gpu_end_t gpu_late_end(void)
{
        gpu_end_t over = GPU_END_NONE;
        positive now, began;

        if (gpu.pending && !gpu_end_said && !gpu_idle())
        {
                now = clock_monotonic_nanoseconds();
                began = gpu_before.sequence && gpu_done(gpu_before.sequence) == 0 ? gpu_before.began : gpu_job_began;
                if (now - began >= GPU_HANG_NS)
                        over = GPU_END_HUNG;
                else if (now >= gpu_fault_poll)
                {
                        struct gpu_fault_scan s;

                        gpu_fault_poll = now + GPU_FAULT_EVERY;
                        gpu_fault_read(began, &s);
                        if (s.have_at)
                                over = GPU_END_HUNG;
                }
                if (over)
                {
                        gpu_end_said = 1, gpu_end = over, gpu_last_error = 62, gpu_job_error = 62, gpu_fault_note(true);
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

// The pending jobs waited for, to the last one's deadline: true once the card is idle; false if it is lost, or the job hung or its wait failed (said here, as gpu_end
// and gpu_last_error).
__attribute__((unused)) static b32 gpu_finish(void)
{
        if (!gpu.pending)
                return !gpu_lost;
        if (gpu_idle())
                return true;
        if (gpu_lost)
                return false;
        return gpu_wait_job(GPU_HANG_NS);
}

// Held across every step of a multi-step present (compositor_card_go / multi-rect gpu_scene): gpu_run and gpu_go do not clear handed until the last step is submitted.
static b32 gpu_handed_hold;

// No record waits for a job any more (the maps' addresses are no longer handed out to one): the marks of the lookups are cleared.
static void gpu_handed_clear(void)
{
        for (unsigned int w = 0; w < 4; w++)
        {
                for (p64 m = gpu_maps_handed[w]; m; m &= m - 1)
                {
                        unsigned int i = (w << 6) + (unsigned int)__builtin_ctzll(m);

                        if (i < gpu_map_count)
                                gpu_maps[i].handed = 0;
                }
                gpu_maps_handed[w] = 0;
        }
        for (unsigned int i = 0; i < gpu.imports; i++)
                gpu.imported_handed[i] = 0;
}

/*
        A client's import whose address va is in, handed to a record (the shim's compositor_wl_live answers every client address a record is given): named in the
        bo list of the jobs until one has run (gpu_handed_clear) and in every job of this tick, as a map handed is (gpu_map_named). An address in no import is nothing.
*/
static void gpu_import_handed(positive va)
{
        static unsigned int hint;
        unsigned int i = hint;

        if (!(i < gpu.imports && va - gpu.imported_va[i] < gpu.imported_bytes[i]))
                for (i = 0; i < gpu.imports && va - gpu.imported_va[i] >= gpu.imported_bytes[i]; i++)
                        ;
        if (i >= gpu.imports)
                return;
        gpu.imported_handed[i] = 1, gpu.imported_used[i] = gpu_tick, hint = i;
}

/*
        Whether the job's bo list names the import at i (gpu_submit). The compositor's own imports (the scanouts and the turned screens' buffers, gpu_import), which
        jobs write, always; a client's only where a record of this job reads it (handed, or used in this tick) or where it was let go of since the last submit (a
        record of this job may hold its address from before), as a map is named. amdgpu makes a job wait for every other program's fence on each buffer it names and
        puts the job on each as a writer, and a client draws its next frame into the buffers it has back: every client buffer named, a present waited for the frame
        a game was drawing into the buffer it was not showing, and that frame for the present (the review's P-c1-1, the gaming team's G-g3-4). The buffer read is
        still named: the wait for its own frame is the implicit sync a client without explicit sync asks for.
*/
static b32 gpu_import_named(unsigned int i, unsigned int before)
{
        if (gpu.imported_dead[i])
                return gpu.imported_dead_after[i] == before;
        return gpu_imports_all || gpu.imported_own[i] || gpu.imported_handed[i];
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

        return pages ? gpu_maps[i].handed : gpu_maps[i].handed && now;
}

#define GPU_AGAIN 32 // the submits a job is asked again after EAGAIN (gpu_submit)
static unsigned int gpu_jobs_ours; // the jobs refused as our own (GPU_ERR_OURS), which are never asked again
static unsigned int gpu_suspects;  // the tries in a row refused as suspects (GPU_ERR_SUSPECT): a second is the card lost; a submit taken clears them
#define GPU_PLACE_STEP (256ull << 20)

// After a refusal of a place (GPU_ERR_PLACE): every map let go of (gpu_forget: made again at its next use, at a new address; its span kept until the card is
// idle, so nothing new is put there meanwhile), and the next places looked for GPU_PLACE_STEP further on. The job's records hold the old addresses, so the job
// itself is not asked again: the processor draws it, and the next present's maps are elsewhere.
static void gpu_place_elsewhere(void)
{
        for (unsigned int i = 0; i < gpu_map_count; i++)
                if (gpu_maps[i].to > gpu_maps[i].from)
                        gpu_forget((const void *)gpu_maps[i].from, gpu_maps[i].to - gpu_maps[i].from);
        gpu_map_next = gpu_map_next + GPU_PLACE_STEP < GPU_VA_END ? gpu_map_next + GPU_PLACE_STEP : GPU_VA_MAPS;
}
/*
        The kernel run over the w by rows pixels of the arguments of the slot the job was built in (gpu_slot, which the caller took with gpu_slot_take and wrote:
        GPU_ARGS), submitted and not waited for: its sequence, for gpu_done, gpu_fence_fd and gpu_wait_job; 0 when the submit was refused (gpu_last_error its
        errno; gpu_lost when the card lost its state, the caller recovers). The job is then the pending one, the one pending before it gpu_before: two at most,
        one in each slot. Its deadline is GPU_HANG_NS on (gpu_late_end).
*/
static void gpu_submit_alarm(int sig) { (void)sig; }
HOT static positive gpu_submit(void)
{
        const unsigned int which = GPU_KERNEL_OF(GPU_ARGS->op), lanes = gpu_backend->lanes;
        const struct gpu_job job = {gpu_set && which < gpu_set->count ? gpu_set->kernels[which].words : 0, GPU_VA + gpu_kernel_at[which < GPU_KERNELS ? which : 0],
                                    GPU_VA + GPU_ARGS_AT(gpu_slot), (GPU_ARGS->w + lanes - 1) / lanes, GPU_ARGS->rows,
                                    (unsigned int address_to)((p8 address_to)gpu.at + GPU_PACKETS * gpu_slot), GPU_VA + GPU_PACKETS * gpu_slot,
                                    gpu_backend->state ? (unsigned int address_to)((p8 address_to)gpu.at + gpu_layout.state + gpu_backend->state * gpu_slot) : 0,
                                    gpu_backend->state ? GPU_VA + gpu_layout.state + gpu_backend->state * gpu_slot : 0};
        p64 *bos = gpu_imports_room(gpu.imports) ? gpu_submit_bos : 0;
        positive sequence = 0, began = clock_monotonic_nanoseconds(), kept = gpu_job_began;
        unsigned int named, tries = 0, again = 0, before = gpu_submit_serial;
        enum gpu_err kind;

        if (unlikely(!bos)) // no memory for the list of the job's buffers: the job is not made, as one too late is not
                return gpu_end = GPU_END_LATE, 0;
        // A slot whose job may still run is not the next job's: its packets and arguments are that job's (gpu_slot_take is the caller's way to a free one).
        if (unlikely(gpu.pending && (gpu_slot == gpu_pending_slot || (gpu_before.sequence && gpu_done(gpu_before.sequence) == 0))))
                return gpu_end = GPU_END_LATE, 0;
        // Flush cached GPU_ARGS to the slot's VRAM arguments in one sequential WC burst before driver submit.
        memory_copy((p8 address_to)gpu.at + GPU_ARGS_AT(gpu_slot), GPU_ARGS, sizeof(struct gpu_args));
        gpu_job_began = began; // (the time a refusal reads the kernel's log from; the pending job's own again if it is refused)
        if (unlikely(!++gpu_submit_serial)) // (counted from 1: 0 is a map no submit has named)
                gpu_submit_serial++;
        for (;;)
        {
                named = 0;
                // Of the imports, the ones held and the ones let go of since the last submit (a record of this job may have been given their addresses before
                // they were): never one let go of earlier. amdgpu makes a job wait for every other program's fence on every buffer it names (AMDGPU_SYNC_NE_OWNER,
                // amdgpu_cs_sync_rings) and puts the job on each as a writer (amdgpu_cs_submit, DMA_RESV_USAGE_WRITE), and a client draws its next frame into
                // the very buffers it was given back: named, every present waited for the frame a game or Chromium was drawing, and that frame for the present.
                for (unsigned int i = 0; i < gpu.imports; i++)
                        if (gpu_import_named(i, before))
                                bos[named++] = gpu.imported[i];
                // Of the maps, only those with handed=1 (tracked in 256-bit gpu_maps_handed[4] bitmask: O(popcount) instead of scanning 20 KiB).
                for (unsigned int w = 0; w < 4; w++)
                        for (p64 m = gpu_maps_handed[w]; m; m &= m - 1)
                        {
                                unsigned int i = (w << 6) + (unsigned int)__builtin_ctzll(m);

                                if (i < gpu_map_count && gpu_map_named(i))
                                        bos[named++] = gpu_maps[i].handle, gpu_maps[i].listed = gpu_submit_serial;
                        }
                GPU_SUBMIT_SEEN(bos, named);
                gpu_end = GPU_END_NONE;
                gpu_alarm_arm(2u);
                gpu_last_error = gpu_backend->submit(&job, bos, named, &sequence);
                gpu_alarm_disarm();
                if (likely(gpu_last_error == 0))
                        break;
                // Hard submit watchdog: if a kernel DRM ioctl stalled >= 1s (interrupted by SIGALRM -> EINTR),
                // never retry it — immediately trip hang recovery so the desktop can never freeze.
                if (clock_monotonic_nanoseconds() - began >= (GPU_HANG_NS / 2))
                {
                        gpu_end = GPU_END_HUNG, gpu_last_error = 62, gpu_job_error = 62, gpu_lost = 1;
                        gpu_fault_note(true);
                        gpu_job_began = kept, gpu_submit_took = clock_monotonic_nanoseconds() - began;
                        return 0;
                }
                kind = gpu_backend->classify(gpu_last_error); // what the errno is to this driver (struct gpu_backend's mapping)
                // A suspect is a refusal the first time, and the card lost the second in a row (a try of this submit or the next submit's).
                if (kind == GPU_ERR_SUSPECT)
                        kind = ++gpu_suspects >= 2 ? GPU_ERR_LOST : GPU_ERR_REFUSED;
                // The card lost the context or its VM (amdgpu: ECANCELED after a reset, ENODEV, EINVAL twice in a row; i915: EIO, a banned context or a wedged GPU, ENODEV):
                // nothing this job can do again succeeds: the caller recovers (gpu_recover).
                // EAGAIN: a page of a userptr moved between the kernel's taking it and the submit (a migration or a compaction: on the box at a load of 120 one job in
                // ten), and the kernel asks for the call again, which takes the pages afresh. Asked again up to GPU_AGAIN times with the processor let go of between: three
                // calls in a row failed the job, the present was the processor's, a list was refused, and three such jobs in a row cooled the card for two seconds.
                if (kind == GPU_ERR_AGAIN && again++ < GPU_AGAIN)
                {
                        system_call_2(syscall(sched_yield), 0, 0);
                        continue;
                }
                // Our own job, wrong as it is (i915's EINVAL: a batch or an object it will not take): the same words are refused the same way again, so it is
                // not asked again and the card is not lost; the processor draws, and the refusal is counted as any refused job is (three in a row cool the card).
                // A page moved under the job as it was submitted, or a map's pages are gone: asked again, without the maps that are, unless the job reads one.
                // The card lost (gpu_lost, the caller recovers), or the job is refused for good: a pinned address that is not ours (gpu_place_elsewhere), our own
                // job wrong as it is (not asked again, the card is not lost), or the third refusal (or the maps that are gone were read).
                if (kind == GPU_ERR_LOST || kind == GPU_ERR_PLACE || kind == GPU_ERR_OURS || ++tries == 3 || gpu_drop_unmapped())
                {
                        gpu_job_error = gpu_last_error;
                        if (gpu_last_error != 95)
                                gpu_fault_note(false);
                        if (kind == GPU_ERR_LOST)
                                gpu_lost = 1;
                        else if (kind == GPU_ERR_PLACE)
                                gpu_place_elsewhere();
                        else if (kind == GPU_ERR_OURS)
                                gpu_jobs_ours++;
                        gpu_job_began = kept, gpu_submit_took = clock_monotonic_nanoseconds() - began;
                        return 0;
                }
        }
        gpu_suspects = 0;
        if (gpu.pending)
                gpu_before.sequence = gpu.pending, gpu_before.began = kept;
        gpu_ring.slot[gpu_slot] = (struct gpu_ring_slot){sequence, began};
        gpu.pending = sequence, gpu_pending_slot = gpu_slot, gpu_end_said = 0, gpu_fault_poll = began + GPU_LATE_NS + GPU_FAULT_FIRST;
        gpu_submit_took = clock_monotonic_nanoseconds() - began;
        return sequence;
}

// A job run and waited for a tick (the hang's two seconds for the self-test: gpu_patient); false if it did not finish (then gpu_idle holds the buffer until it has).
// A self-test after the card was started again (gpu_patient 2, gpu_test_hash's when gpu_recovered: a reset, a resume, a late look after a recovery that failed)
// waits GPU_RECOVER_NS a pass: it runs
// on the compositor's loop, so a card that does not answer after its reset held every key, report and client for both passes' two seconds; one that misses it
// is no card for now, which the late look starts again.
#define GPU_RECOVER_NS 500000000ull
static positive gpu_patience(void)
{
        return gpu_patient == 2 ? GPU_RECOVER_NS : gpu_patient ? GPU_HANG_NS : GPU_LATE_NS;
}

static b32 gpu_run_job(void)
{
        if (!gpu_idle() || !gpu_submit())
                return false;
        return gpu_wait_job(gpu_patience());
}

// A job run and waited for (gpu_run_job), timed: the count and the total wait the metrics publish.
static b32 gpu_run(void)
{
        const unsigned int kind = GPU_JOB_KIND(GPU_ARGS->op);
        b64 began = clock_monotonic_nanoseconds(), took;
        b32 done;

        gpu_submit_took = 0;
        done = gpu_run_job();
        took = clock_monotonic_nanoseconds() - began;
        gpu_jobs++, gpu_job_ns += took, gpu_jobs_waited++, gpu_jobs_kind[kind]++, gpu_job_ns_kind[kind] += took;
        gpu_job_submit_ns += gpu_submit_took, gpu_job_wait_ns += took > gpu_submit_took ? took - gpu_submit_took : 0;
        if (!gpu.pending && !gpu_handed_hold) // a job given up on may still read the maps: their addresses stay handed out until it is done (gpu_idle)
                gpu_handed_clear();
        return done;
}

// A job submitted without a wait (gpu_submit), counted as gpu_run counts one. The records it was built from are run: the marks of their maps are let go of
// (the job's bo list names them, and no map is dropped while a job may run: gpu_idle).
static positive gpu_go(void)
{
        const unsigned int kind = GPU_JOB_KIND(GPU_ARGS->op);
        positive sequence = gpu_submit();

        gpu_jobs++, gpu_jobs_unwaited++, gpu_jobs_kind[kind]++;
        if (sequence && !gpu_handed_hold)
                gpu_handed_clear();
        return sequence;
}

// ui_box_radii on a box record's rx and ry (words 6 and 7): where two corners come to more than a side, the one factor that fits them all.
static inline void gpu_radii(unsigned int *restrict r)
{
        long long w = (int)r[3] - (int)r[1], h = (int)r[4] - (int)r[2], f = 65536;

        if (unlikely(w <= 0 || h <= 0))
                return;
        if ((int)r[6] > 0 && 2 * (long long)(int)r[6] > w)
                f = (w << 16) / (2 * (long long)(int)r[6]);
        if ((int)r[7] > 0 && 2 * (long long)(int)r[7] > h)
        {
                long long fy = (h << 16) / (2 * (long long)(int)r[7]);
                if (fy < f)
                        f = fy;
        }
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
        if (i + (unsigned int)m >= n) return -1; \
        x0 = x0 < (int)dx0 ? (int)dx0 : x0, y0 = y0 < (int)dy0 ? (int)dy0 : y0, x1 = x1 > (int)dx1 ? (int)dx1 : x1, y1 = y1 > (int)dy1 ? (int)dy1 : y1; \
        if (x0 < x1 && y0 < y1) { unsigned int tx0 = (unsigned int)x0 >> 6, ty0 = (unsigned int)y0 >> 6, tx1 = (unsigned int)(x1 - 1) >> 6, ty1 = (unsigned int)(y1 - 1) >> 6; body } i += m; }
/*
        The cells of the bins a record covers whole as the scene kernel draws it, by the kernel's own rule for a first record that holds its patch (gpu.asm's
        .scene_first_rect and .scene_first_box): a fill (word 0 is 0: its colour is written over the pixel, whatever its alpha), a picture copied (4), or a box (word 0
        is 2: no change of cover after it) of alpha 255 whose corners are in no row of the cell (its rows inside its vertical radius are not the cell's), with its
        radii fitted (gpu_radii); each cell's part of the damage the lists are binned for (dx0 dy0 dx1 dy1) inside it. Every record of the cell's list before it
        is then hidden by it: none of their pixels can show. cells: the first and last column and row of them; false for none.
*/
static inline b32 gpu_record_holds(const unsigned int *restrict r, unsigned int dx0, unsigned int dy0, unsigned int dx1, unsigned int dy1, unsigned int *restrict cells)
{
        long long x0, y0, x1, y1, ry = (int)r[7];

        //      A copy of a picture with red and blue swapped (0x80) or sampled tiled/DCC (0x40, 0x20) covers its cells as a copy does.
        if (r[0] == 0 || (r[0] & ~0xe0u) == 4)
                x0 = (int)r[1], y0 = (int)r[2], x1 = (int)r[3], y1 = (int)r[4];
        else if (r[0] == 2 && r[5] >> 24 == 255 && ry >= 0 && (int)r[6] >= 0)
                x0 = ((long long)(int)r[1] + 255) >> 8, x1 = (long long)(int)r[3] >> 8, y0 = ((long long)(int)r[2] + ry + 255) >> 8, y1 = ((long long)(int)r[4] - ry) >> 8;
        else
                return false;
        x0 = x0 <= (long long)dx0 ? (long long)dx0 : (x0 + 63) & ~63ll, y0 = y0 <= (long long)dy0 ? (long long)dy0 : (y0 + 63) & ~63ll;
        x1 = x1 >= (long long)dx1 ? (long long)dx1 : x1 & ~63ll, y1 = y1 >= (long long)dy1 ? (long long)dy1 : y1 & ~63ll;
        if (x0 >= x1 || y0 >= y1)
                return false;
        cells[0] = (unsigned int)(x0 >> 6), cells[1] = (unsigned int)(y0 >> 6), cells[2] = (unsigned int)((x1 - 1) >> 6), cells[3] = (unsigned int)((y1 - 1) >> 6);
        return true;
}

// The scene's job built: its records binned into the bins at bins_at (room bytes there for the table and the lists), and the arguments of the slot the job is
// built in (gpu_slot) written. 1 built; 0 nothing to draw (the damage is empty); -1 refused (a record that runs past the scene, a scene the kernels by hand do not
// draw, bins that do not fit: gpu_end OVERFLOW). binned is the damage the lists hold (whole patches of 64 by 4).
HOT static int gpu_scene_build(positive ops_at, unsigned int n, positive bins_at, positive room, positive d_at, unsigned int pitch, unsigned int w, unsigned int h,
                           unsigned int dx0, unsigned int dy0, unsigned int dx1, unsigned int dy1, unsigned int binned[4])
{
        unsigned int address_to ops, address_to tab;
        unsigned int cw, ch, hand = 0, scene_op = 4, i, address_to list, record[16 * 8], held[4];
        positive at = 0;
        struct gpu_args address_to a = GPU_ARGS;
        b32 aligned = !(w & 3) && !(pitch & 3) && !(d_at & 15), partial, cut;

        if (!w || !h || w > 65536u || h > 65536u || ops_at > gpu.size || 32ull * (positive)n > gpu.size - ops_at)
                return -1;
        ops = (unsigned int address_to)((p8 address_to)gpu.at + ops_at);
        tab = (unsigned int address_to)((p8 address_to)gpu.at + bins_at);
        cw = (w + 63) >> 6, ch = (h + 63) >> 6, list = tab + 2 * cw * ch;

        dx1 = dx1 > w ? w : dx1, dy1 = dy1 > h ? h : dy1;
        if (dx0 >= dx1 || dy0 >= dy1)
                return 0;
        // Stage ops once from uncached USWC VRAM into cached RAM (skip on Intel i915 aligned op 6 which skips CPU binning).
        if (n > 1 && !(aligned && gpu_backend == &gpu_i915_row))
        {
                positive ops_bytes = 32ull * n;
                void *ostage = gpu_ops_stage;

                if (ops_bytes > gpu_ops_room && gpu_buf_grow(&ostage, &gpu_ops_room, ops_bytes < 65536 ? 65536 : ops_bytes, 0))
                        gpu_ops_stage = (unsigned int *)ostage;
                if (gpu_ops_stage && ops_bytes <= gpu_ops_room)
                        memory_copy_apart(gpu_ops_stage, ops, ops_bytes), ops = gpu_ops_stage;
        }
        if (unlikely(!aligned))
        {
                partial = dx0 > 0 || dy0 > 0 || dx1 < w || dy1 < h;
                for (i = 0; i < n; i++)
                {
                        unsigned int kind = ops[8 * i] & 255;
                        unsigned int m = kind == 2 ? (ops[8 * i] >> 8 & 15) : 0;

                        if (i + m >= n)
                                return -1;
                        if (kind == 2)
                                scene_op = 5, hand |= ops[8 * i] > 2;
                        else
                                hand |= kind > 2;
                        i += m;
                }
                if (hand || partial || !gpu_set->kernels[scene_op].words)
                        return -1;
        }
        dx0 &= ~63u, dy0 &= ~3u, dx1 = (dx1 + 63) & ~63u, dy1 = (dy1 + 3) & ~3u; // the kernel draws whole patches: so the ops are binned for them
        dx1 = dx1 > w ? w : dx1, dy1 = dy1 > h ? h : dy1;
        binned[0] = dx0, binned[1] = dy0, binned[2] = dx1, binned[3] = dy1;
        cut = aligned && !gpu_cut_off; // (the kernels by hand: the hidden records are passed over in the lists of the cells a later record covers whole)
        if (bins_at >= gpu.size || room > gpu.size - bins_at || 8ull * cw * ch > room)
                return gpu_end = GPU_END_OVERFLOW, -1;
        // Intel i915 op 6 (gpu_i915_scene) reads culled ops directly instead of 64x64 bins: skip CPU binning completely.
        if (!(aligned && gpu_backend == &gpu_i915_row))
        {
                positive tab_bytes = (positive)cw * ch * 8, need;
                unsigned int dty0 = dy0 >> 6, dty1 = (dy1 - 1) >> 6;
                positive row_off = (positive)dty0 * cw, row_cells = (positive)(dty1 - dty0 + 1) * cw, row_bytes = row_cells * 8;
                void *stage = gpu_bin_stage;
                int staged = 0;

                if (tab_bytes > gpu_bin_room && gpu_buf_grow(&stage, &gpu_bin_room, tab_bytes < 262144 ? 262144 : tab_bytes, 0))
                        gpu_bin_stage = (unsigned int *)stage;
                if (gpu_bin_stage && tab_bytes <= gpu_bin_room)
                        tab = gpu_bin_stage, list = tab + 2 * cw * ch, staged = 1;
                memory_zero(tab + 2 * row_off, row_bytes);
                // The three steps of the bins are the library's (bins_count, bins_starts, bins_place: lib.c, in assembly on each machine): a record's squares counted,
                // the counts made starts, and the record put in each square's list (scoped to damaged tile rows [dty0..dty1]).
                GPU_RECORDS(bins_count(tab + 2 * (ty0 * cw + tx0), cw, tx1 - tx0 + 1, ty1 - ty0 + 1, 1 + (positive)m);)
                at = bins_starts(tab + 2 * row_off, row_cells);
                if (at > (room - tab_bytes) / 32ull) // the lists must fit the room they have
                        return gpu_end = GPU_END_OVERFLOW, -1;
                need = tab_bytes + 32ull * at;
                if (staged && need > gpu_bin_room)
                {
                        stage = gpu_bin_stage;
                        if (gpu_buf_grow(&stage, &gpu_bin_room, need, tab_bytes))
                                gpu_bin_stage = (unsigned int *)stage, tab = gpu_bin_stage, list = tab + 2 * cw * ch;
                        else
                        {
                                tab = (unsigned int address_to)((p8 address_to)gpu.at + bins_at), list = tab + 2 * cw * ch, staged = 0;
                                memory_copy_apart(tab + 2 * row_off, gpu_bin_stage + 2 * row_off, row_bytes);
                        }
                }
                // A record goes into the list of each square it is in as it is, a box with its corners fitted to its sides (gpu_radii): fitted once, and the same words
                // put in every list (they were fitted again in each square: two divisions a square for a box of a whole window).
                // A record that covers a cell whole hides every record before it there: the cell's list starts at it (bins_skip), the ones before passed over.
                GPU_RECORDS(memory_copy(record, q, 32 * (1 + (positive)m)); if (e) for (int j = 0; j <= m; j++) gpu_radii(record + 8 * j);
                            if (cut && !m && gpu_record_holds(record, dx0, dy0, dx1, dy1, held))
                                    bins_skip(tab + 2 * (held[1] * cw + held[0]), cw, held[2] - held[0] + 1, held[3] - held[1] + 1);
                            bins_place(tab + 2 * (ty0 * cw + tx0), cw, tx1 - tx0 + 1, ty1 - ty0 + 1, list, record, 1 + (positive)m);)
                if (staged)
                {
                        p8 address_to dst = (p8 address_to)gpu.at + bins_at;

                        if (dty0 == 0 && dty1 + 1 == ch)
                                memory_copy_apart(dst, tab, need);
                        else
                        {
                                memory_copy_apart(dst + row_off * 8, tab + 2 * row_off, row_bytes);
                                if (at)
                                        memory_copy_apart(dst + tab_bytes, list, 32ull * at);
                        }
                }
        }
        a->tab = GPU_VA + GPU_BASES_AT, a->s = GPU_VA, a->d = d_at >= GPU_VA ? d_at : GPU_VA + d_at, a->g = GPU_VA + bins_at, a->dp = pitch, a->sp = w, a->gp = h, a->x0 = a->y0 = 0, a->w = w, a->rows = (h + 3) >> 2, a->op = scene_op;
        if (aligned)
                a->op = 6, a->x0 = dx0 >> 6, a->y0 = dy0 >> 2, a->w = (((dx1 + 63) >> 6) - (dx0 >> 6)) * 64, a->rows = ((dy1 + 3) >> 2) - (dy0 >> 2);
        a->ops = GPU_VA + ops_at, a->count = n;
        return 1;
}

// A scene drawn, and waited for (gpu_run): the bins at bins_at, to the end of the buffer. True when it was drawn (or there was nothing to draw).
static b32 gpu_scene(positive ops_at, unsigned int n, positive bins_at, positive d_at, unsigned int pitch, unsigned int w, unsigned int h, unsigned int dx0,
                     unsigned int dy0, unsigned int dx1, unsigned int dy1)
{
        unsigned int binned[4];
        int built;

        gpu_last_error = 0, gpu_end = GPU_END_NONE; // this call's answer: a hang of its job is 62 here, and an older job's is not read as its own
        if (!gpu_idle()) // the lists and the arguments are not to be written under a job that may still read them
                return false;
        gpu_slot_take();
        built = gpu_scene_build(ops_at, n, bins_at, bins_at < gpu.size ? gpu.size - bins_at : 0, d_at, pitch, w, h, dx0, dy0, dx1, dy1, binned);
        return built < 0 ? false : built == 0 ? true : gpu_run();
}

/*
        A scene drawn without a wait: built in the slot gpu_slot_take gives and binned into the side of [bins_at, the buffer's last megabyte) that no pending job
        reads (two sides, used in turn), then submitted (gpu_submit). The job's sequence, which gpu_done, gpu_fence_fd and gpu_wait_job take; 0 when nothing was
        submitted: gpu_end LATE while the slot or the side is still a running job's (the jobs before are late), OVERFLOW for bins that do not fit their side, NONE
        with nothing to draw or a scene the kernels by hand do not draw (gpu_last_error says a refused submit, gpu_lost a card that lost its state). The records,
        the tables they name and the destination are the caller's to keep as they are until the job is done.
*/
static positive gpu_scene_submit(positive ops_at, unsigned int n, positive bins_at, positive d_at, unsigned int pitch, unsigned int w, unsigned int h,
                                 unsigned int dx0, unsigned int dy0, unsigned int dx1, unsigned int dy1)
{
        positive top = gpu.size > (1 << 20) ? gpu.size - (1 << 20) : 0;
        b32 scratch = bins_at >= top && bins_at + 4096 <= gpu.size;
        positive room = scratch ? 4096ull : (bins_at < top ? ((top - bins_at) / 2) & ~4095ull : 0), at, sequence;
        unsigned int side = gpu_bins.side ^ 1u, binned[4];
        int built;

        gpu_last_error = 0, gpu_end = GPU_END_NONE;
        if (gpu_lost || gpu_done(gpu_bins.job[side]) == 0 || gpu_lost || gpu_slot_take() < 0)
                return gpu_end = gpu_lost ? GPU_END_NONE : GPU_END_LATE, 0;
        at = scratch ? bins_at : bins_at + side * room;
        built = gpu_scene_build(ops_at, n, at, room, d_at, pitch, w, h, dx0, dy0, dx1, dy1, binned);
        if (built < 0 && gpu_end == GPU_END_OVERFLOW && !scratch && bins_at < top && !gpu_lost && gpu_done(gpu_bins.job[side ^ 1u]) > 0)
        {
                positive full_room = (top - bins_at) & ~4095ull;

                if (full_room > room)
                {
                        gpu_end = GPU_END_NONE;
                        at = bins_at;
                        built = gpu_scene_build(ops_at, n, at, full_room, d_at, pitch, w, h, dx0, dy0, dx1, dy1, binned);
                        if (built > 0 && (sequence = gpu_go()) != 0)
                        {
                                gpu_bins.job[0] = gpu_bins.job[1] = sequence, gpu_bins.side = side;
                                return sequence;
                        }
                        return 0;
                }
        }
        if (built <= 0 || !(sequence = gpu_go()))
                return 0;
        gpu_bins.job[side] = sequence, gpu_bins.side = side;
        return sequence;
}

// One job moving a picture (src, sp pixels a row) into dst (dp a row) over w by h pixels, the card told by op. op 0 copies one rectangle,
// d[y * dp + x] = s[y * sp + x] for x below w and y below h, the pitches in pixels; op 7 scales nearest, the card told by tables where each column and
// row comes from: the processor has made the w entries of the columns' and then the h entries of the rows' at tab (a GPU address), each the source's
// index (the source cell whose centre is the point's). The card divides nothing, and reads nothing but what the tables name. gpu_run waits for the
// job, so when this is true the destination is written: nothing that reads it, or releases the source, is run before. false if the card did not finish it (as gpu_run says).
static b32 gpu_blit(positive src, unsigned int sp, positive dst, unsigned int dp, positive tab, unsigned int op, unsigned int w, unsigned int h)
{
        struct gpu_args address_to a;

        gpu_last_error = 0, gpu_end = GPU_END_NONE;
        if (!w || !h || !gpu_idle())
                return false;
        gpu_slot_take(), a = GPU_ARGS;
        a->s = src, a->d = dst, a->g = tab, a->sp = sp, a->dp = dp, a->gp = 0, a->w = w, a->op = op, a->v = 0, a->x0 = a->y0 = 0, a->rows = h, a->tab = op ? GPU_VA + GPU_BASES_AT : 0;
        return gpu_run();
}

// The glass job (op 3, gpu.asm glass): the picture s (sp pixels a row) over the ground g (gp a row) into d (dp a row), w by h, with the grain of the absolute
// x0, y0 (the rect's origin in the surface the ground is) and light: 0 dark glass, 1 light. Card addresses, all pitches in pixels. gpu_run waits, as gpu_blit does.
__attribute__((unused)) static b32 gpu_glass(positive s, unsigned int sp, positive g, unsigned int gp, positive d, unsigned int dp, unsigned int x0, unsigned int y0,
                                              unsigned int w, unsigned int h, unsigned int light)
{
        struct gpu_args address_to a;

        gpu_last_error = 0, gpu_end = GPU_END_NONE;
        if (!w || !h || !gpu_idle())
                return false;
        gpu_slot_take(), a = GPU_ARGS;
        a->s = s, a->d = d, a->g = g, a->sp = sp, a->dp = dp, a->gp = gp, a->w = w, a->op = 3, a->v = light ? 1u : 0u, a->x0 = x0, a->y0 = y0, a->rows = h, a->tab = 0;
        return gpu_run();
}

// gpu_glass submitted and not waited for (gpu_go, as gpu_blit_go is): its ring sequence, 0 when no slot is free or the submit is refused. The destination is
// written once gpu_done (or gpu_finish) says the sequence is done; the card runs jobs in submission order, so a later op 4 frame copy on the same ring reads
// the composed band after it. A job pending in the slot this one needs is LATE (0), and the caller draws the band on the processor.
__attribute__((unused)) static positive gpu_glass_go(positive s, unsigned int sp, positive g, unsigned int gp, positive d, unsigned int dp, unsigned int x0,
                                                     unsigned int y0, unsigned int w, unsigned int h, unsigned int light)
{
        struct gpu_args address_to a;

        gpu_last_error = 0, gpu_end = GPU_END_NONE;
        if (!w || !h || gpu_lost || gpu_slot_take() < 0)
                return 0;
        a = GPU_ARGS;
        a->s = s, a->d = d, a->g = g, a->sp = sp, a->dp = dp, a->gp = gp, a->w = w, a->op = 3, a->v = light ? 1u : 0u, a->x0 = x0, a->y0 = y0, a->rows = h, a->tab = 0;
        return gpu_go();
}

// gpu_blit submitted without waiting (gpu_go): returns the ring sequence, or 0 when no slot is free or refused.
static positive gpu_blit_go(positive src, unsigned int sp, positive dst, unsigned int dp, positive tab, unsigned int op, unsigned int w, unsigned int h)
{
        struct gpu_args address_to a;

        gpu_last_error = 0, gpu_end = GPU_END_NONE;
        if (!w || !h || gpu_lost || gpu_slot_take() < 0)
                return 0;
        a = GPU_ARGS;
        a->s = src, a->d = dst, a->g = tab, a->sp = sp, a->dp = dp, a->gp = 0, a->w = w, a->op = op, a->v = 0, a->x0 = a->y0 = 0, a->rows = h, a->tab = op ? GPU_VA + GPU_BASES_AT : 0;
        return gpu_go();
}

/*
        An NV12 picture made XRGB by the card (op 9), scaled by tables as op 7 is: its luma at luma (luma_pitch bytes a row), its chroma at chroma
        (chroma_pitch bytes a row, a U and a V byte for each 2 by 2 pixels), into dst (dp pixels a row) over w by h; the processor has made the w
        entries of the columns' and then the h entries of the rows' at tab, each the source's column and row (no pitch in them). The colour is
        compositor_wl_decode_pixel's, bit for bit (BT.601, limited range). amdgpu's set alone has the kernel. gpu_run waits, as gpu_blit's does. False,
        with nothing run, on a card whose start did not draw the NV12 test right (gpu_nv12_ok): the caller's processor walk makes the picture then.
*/
static b32 gpu_nv12(positive luma, unsigned int luma_pitch, positive chroma, unsigned int chroma_pitch, positive dst, unsigned int dp, positive tab,
                    unsigned int w, unsigned int h)
{
        struct gpu_args address_to a;

        gpu_last_error = 0, gpu_end = GPU_END_NONE;
        if (!w || !h || !gpu_idle() || gpu_backend != &gpu_amd || (!gpu_nv12_ok && !gpu_nv12_trying))
                return false;
        gpu_slot_take(), a = GPU_ARGS;
        a->s = luma, a->d = dst, a->g = tab, a->sp = luma_pitch, a->dp = dp, a->gp = chroma_pitch, a->w = w, a->op = 9, a->v = 0, a->x0 = a->y0 = 0, a->rows = h;
        a->tab = 0, a->ops = chroma, a->count = 0;
        return gpu_run();
}

/*
        A picture the card reads through its texture unit (op 8): the resource's eight words desc (gpu_amd_image) at byte 128 of the slot's arguments, the
        rectangle w by h from (x0, y0) of it written linear into dst, dp pixels a row. kind is what gpu_amd_image answered for the resource, and with
        GPU_IMAGE_OVER the picture is laid over what dst holds as a premultiplied picture is (the scene's image over) instead of copied. The unit undoes the picture's tiling and its compression (DCC) as it does
        for the card's own sampling, so a client that draws in the card's own layout is read as it drew. gpu_run waits, as gpu_blit's does.
*/
#define GPU_IMAGE_OVER 1u // gpu_image's kind: laid over, not copied
#define GPU_IMAGE_RAW 2u  // (gpu_amd_image's word) a resource of 32_UINT: the pixel's word as it is
#define GPU_IMAGE_SWAP 4u // and its red and blue the other way
#define GPU_IMAGE_MADE 8u // a resource made
static void gpu_image_args(const unsigned int desc[8], unsigned int kind, unsigned int x0, unsigned int y0, positive dst, unsigned int dp, unsigned int w,
                           unsigned int h)
{
        struct gpu_args address_to a = GPU_ARGS;

        a->s = 0, a->d = dst, a->g = 0, a->sp = 0, a->dp = dp, a->gp = 0, a->w = w, a->op = 8;
        a->v = kind & (GPU_IMAGE_OVER | GPU_IMAGE_RAW | GPU_IMAGE_SWAP), a->x0 = x0, a->y0 = y0, a->rows = h, a->tab = 0;
        memory_copy((p8 address_to)gpu.at + GPU_ARGS_AT(gpu_slot) + 128, desc, 32);
}

static b32 gpu_image(const unsigned int desc[8], unsigned int kind, unsigned int x0, unsigned int y0, positive dst, unsigned int dp, unsigned int w, unsigned int h)
{
        gpu_last_error = 0, gpu_end = GPU_END_NONE;
        if (!w || !h || !gpu_idle() || (gpu_set != &gpu_amd_rdna2 && gpu_set != &gpu_amd_rdna3))
                return false;
        gpu_slot_take();
        gpu_image_args(desc, kind, x0, y0, dst, dp, w, h);
        return gpu_run();
}

// gpu_image submitted and not waited for (gpu_go): its sequence, 0 when no slot is free or the job is refused (the caller waits and asks gpu_image). The
// rectangle is written once the sequence is done (gpu_done): a reader on the processor waits for it first; a job of the card after it runs after it.
static positive gpu_image_go(const unsigned int desc[8], unsigned int kind, unsigned int x0, unsigned int y0, positive dst, unsigned int dp, unsigned int w,
                             unsigned int h)
{
        gpu_last_error = 0, gpu_end = GPU_END_NONE;
        if (!w || !h || (gpu_set != &gpu_amd_rdna2 && gpu_set != &gpu_amd_rdna3) || gpu_lost || gpu_slot_take() < 0)
                return 0;
        gpu_image_args(desc, kind, x0, y0, dst, dp, w, h);
        return gpu_go();
}

// A scaled picture read through the texture unit (op 8 with bit 3): the same resource and layout as gpu_image, but the coordinates are the tables at tab
// (w entries of the source's columns, then the rows' source rows, relative to the resource's (x0, y0)), as op 7's are. So a tiled or DCC client picture is scaled
// by the card with no detile pass. Usable only while gpu_image_ok holds: the card draws a scaled test of its own at each start (gpu_image_test).
#define GPU_IMAGE_SCALED 16u // gpu_image's kind: the picture is scaled by the tables at tab (the job's g); op 8 reads bit 3 of its kind

// Whether the card can take a tiled or DCC picture scaled in place (gpu_image_scaled): the card is up and its scaled self-test (in gpu_image_test) drew right at
// the start. The compositor's opt-in for sampling client buffers without a detile reads this; it is false on a host with no card.
__attribute__((unused)) static b32 gpu_scaled_ready(void)
{
        return gpu_ready() && gpu_image_ok;
}
static void gpu_image_scaled_args(const unsigned int desc[8], unsigned int kind, positive tab, unsigned int x0, unsigned int y0, positive dst, unsigned int dp,
                                  unsigned int w, unsigned int h)
{
        struct gpu_args address_to a;

        gpu_image_args(desc, kind & ~GPU_IMAGE_SCALED, x0, y0, dst, dp, w, h);
        a = GPU_ARGS;
        a->g = tab, a->v |= (kind & GPU_IMAGE_SCALED) ? 8u : 0u;
}

static b32 gpu_image_scaled(const unsigned int desc[8], unsigned int kind, positive tab, unsigned int x0, unsigned int y0, positive dst, unsigned int dp,
                            unsigned int w, unsigned int h)
{
        gpu_last_error = 0, gpu_end = GPU_END_NONE;
        if (!w || !h || !gpu_idle() || !gpu_image_ok || (gpu_set != &gpu_amd_rdna2 && gpu_set != &gpu_amd_rdna3))
                return false;
        gpu_slot_take();
        gpu_image_scaled_args(desc, kind | GPU_IMAGE_SCALED, tab, x0, y0, dst, dp, w, h);
        return gpu_run();
}

__attribute__((unused)) static positive gpu_image_scaled_go(const unsigned int desc[8], unsigned int kind, positive tab, unsigned int x0, unsigned int y0,
                                                              positive dst, unsigned int dp, unsigned int w, unsigned int h)
{
        gpu_last_error = 0, gpu_end = GPU_END_NONE;
        if (!w || !h || !gpu_image_ok || (gpu_set != &gpu_amd_rdna2 && gpu_set != &gpu_amd_rdna3) || gpu_lost || gpu_slot_take() < 0)
                return 0;
        gpu_image_scaled_args(desc, kind | GPU_IMAGE_SCALED, tab, x0, y0, dst, dp, w, h);
        return gpu_go();
}

// AMD gfx10.3 (RDNA2) & gfx11 (RDNA3/3.5) SQ_IMG_RSRC_WORD0..7 texture descriptor for linear, 64K/256K tiled, and DCC buffers.
#define GPU_MOD_AMD 2ull
static unsigned int gpu_amd_image(unsigned int desc[8], positive va, positive meta, unsigned int width, unsigned int height, p64 modifier, b32 swapped, b32 unorm)
{
        unsigned int tile = 0, dcc = 0, retile = 0, piped = 0, block = 0;

        if (!width || !height || width > 16384 || height > 16384 || (va & 255))
                return 0;
        if (modifier)
        {
                unsigned int version = (unsigned int)(modifier & 0xff);
                b32 gfx11 = gpu_set == &gpu_amd_rdna3;

                if (modifier >> 56 != GPU_MOD_AMD)
                        return 0;
                if (gfx11 ? (version != 4) : (version != 1 && version != 3))
                        return 0;
                tile = (unsigned int)(modifier >> 8 & 0x1f), dcc = (unsigned int)(modifier >> 13 & 1), retile = (unsigned int)(modifier >> 14 & 1);
                piped = retile ? 1u : (unsigned int)(modifier >> 15 & 1), block = (unsigned int)(modifier >> 18 & 3);
                if (tile == 31 && gfx11)
                {
                        if ((va & 262143) || (width & 255) || (dcc && (!meta || (meta & 255) || block > 1)))
                                return 0;
                }
                else if ((tile != 9 && tile != 10 && tile != 25 && tile != 27) || (va & 65535) || (width & 127) || (dcc && (!meta || (meta & 255) || block > 1)))
                        return 0;
        }
        else if (width & 63)
                return 0;
        unorm = unorm || dcc;
        desc[0] = (unsigned int)(va >> 8);
        desc[1] = (unsigned int)(va >> 40 & 0xff) | (unorm ? 56u : 20u) << 20 | ((width - 1) & 3) << 30;
        desc[2] = ((width - 1) >> 2 & 0xfff) | (height - 1) << 14 | (gpu_set == &gpu_amd_rdna2 ? 1u << 31 : 0u);
        desc[3] = (!unorm ? 4u : swapped ? 6u | 5u << 3 | 4u << 6 | 7u << 9 : 4u | 5u << 3 | 6u << 6 | 7u << 9) | tile << 20 | 9u << 28;
        desc[4] = modifier ? 0 : width - 1;
        desc[5] = 4u << 20;
        desc[6] = dcc ? 2u << 15 | block << 17 | piped << 19 | 1u << 21 | 1u << 22 | (unsigned int)(meta >> 8 & 0xff) << 24 : 0;
        desc[7] = dcc ? (unsigned int)(meta >> 16) : 0;
        return GPU_IMAGE_MADE | (unorm ? 0 : GPU_IMAGE_RAW | (swapped ? GPU_IMAGE_SWAP : 0));
}

static b32 gpu_amd_image_fits(positive bytes, positive offset, positive meta, unsigned int width, unsigned int height, p64 modifier)
{
        positive tile_h, picture;

        if (!width || !height || width > 16384u || height > 16384u)
                return false;
        tile_h = (modifier && (modifier >> 8 & 0x1f) == 31) ? 256ull : 128ull;
        picture = (positive)width * 4 * (modifier ? ((positive)height + tile_h - 1) & ~(tile_h - 1) : (positive)height);

        if (offset > bytes || picture > bytes - offset)
                return false;
        return !(modifier >> 13 & 1) || (meta <= bytes && (picture + 255) / 256 <= bytes - meta);
}

// Whether the card reads a client's picture of this modifier now: the texture unit's read drew its test at this start, and the modifier is one
// gpu_amd_image makes a resource of (GFX10/10.3 on RDNA2, GFX11 version 4 on RDNA3/3.5).
static b32 gpu_image_takes(p64 modifier)
{
        unsigned int d[8];

        if (!gpu_image_ok || gpu.broken || gpu.fd < 0)
                return false;
        if (modifier && (modifier & 0xff) == 3 && !GPU_FAMILIES(gpu.family) && !gpu_gfx103 && !(gpu_amd_gfx_major == 10 && gpu_amd_gfx_minor >= 3))
                return false;
        if (modifier && (modifier & 0xff) == 4 && gpu_set != &gpu_amd_rdna3)
                return false;
        return gpu_amd_image(d, 0x100000, modifier >> 13 & 1 ? 0x200000 : 0, 2048, 1080, modifier, false, false) != 0;
}

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
        b32 kept_hold;

        if (gpu_lost || !gpu.at || !gpu_last_job || now - gpu_last_job < GPU_WAKE_IDLE || !gpu_idle())
                return;
        gpu_wakes++;
        ops_at = gpu.size - GPU_WAKE_AT, bins_at = ops_at + 4096, d_at = ops_at + 8192;
        ops = (unsigned int address_to)((p8 address_to)gpu.at + ops_at);
        ops[0] = 0, ops[1] = 0, ops[2] = 0, ops[3] = 64, ops[4] = 4, ops[5] = 0, ops[6] = ops[7] = 0;
        // Submit asynchronously so input forwarding never stalls ~2.2 ms on GPU clock ramp; preserve handed marks.
        kept_hold = gpu_handed_hold, gpu_handed_hold = 1;
        gpu_scene_submit(ops_at, 1, bins_at, d_at, 64, 64, 4, 0, 0, 64, 4);
        gpu_handed_hold = kept_hold;
}

/*
        The card's clocks held while a person's input arrives (the compositor's policy, compositor_gpu_clocks): gpu_clocks(fd, epoch, level) asks the
        driver of the descriptor fd, the card of that epoch (gpu_epoch), for the level (gpu_backend->clocks; 0 lets go), on a context of its own made at
        the first ask on that descriptor (a card started again has a new one, and the old context went with the old descriptor, its level with it).
        Answers 0 when the level is held as asked (or let go of), else the errno; a driver with no hold answers ENOSYS. It is the driver's power
        firmware that answers, in milliseconds (2.4 to 2.8 ms measured on the box), so the compositor asks it on a thread of its own
        (compositor_gpu_clocks_run), never on the loop: the descriptor and the epoch are the ask's, not read here, and this state is that thread's.
        gpu_clocks_state.took is the last ask's time (ns).
*/
static struct
{
        unsigned int ctx, epoch, level, asks, error;
        positive took;
} gpu_clocks_state;

static unsigned int gpu_clocks(int fd, unsigned int epoch, unsigned int level)
{
        positive began = clock_monotonic_nanoseconds();
        unsigned int e;

        if (fd < 0 || !gpu_backend->clocks)
                return 38;
        if (gpu_clocks_state.epoch != epoch)
                gpu_clocks_state.ctx = 0, gpu_clocks_state.level = 0, gpu_clocks_state.epoch = epoch;
        if (gpu_clocks_state.level == level)
                return 0;
        e = gpu_backend->clocks(fd, level, &gpu_clocks_state.ctx);
        gpu_clocks_state.took = clock_monotonic_nanoseconds() - began, gpu_clocks_state.asks++;
        if (e)
                gpu_clocks_state.error = e;
        else
                gpu_clocks_state.level = level;
        return e;
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

// One small scene of every kind of op, made from nothing but numbers (a generator for the pictures, the covers and the glyph's bitmap), and the hash of its pixels,
// 0 if the card did not finish it.
// The batch-encoded self-test's first wrong pixel, said once in the log (a chip's bring-up reads it there): the scene, the pixel, what was drawn and wanted.
static void gpu_test_say(unsigned int scene, unsigned int x, unsigned int y, unsigned int got, unsigned int want)
{
        string_format(log, "gpu: the self-test's scene %p drew pixel %p,%p as %p, wanted %p\n", (positive)scene, (positive)x, (positive)y, (positive)got,
                      (positive)want);
        log_flush();
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
        // Intel batch-encoded sets (Gen9/Gen9LP/etc.): verify the scene ops implemented by gpu_i915_scene_batch (fill 0, tone 1,
        // image copy 4, swapped image copy 4|0x80, premul over 11) bit-exact against CPU integer reference math.
        if (gpu_set && gpu_set->batch)
        {
                static const unsigned int i915_ops[][8] = {
                    {0, 0, 0, W2, H2, 0xff203040u, 0, 0},
                    {1, 4, 2, 48, 12, 0x80403020u, 0, 0},
                    {4, 8, 1, 24, 9, 0, 0, 64},
                    {4 | 0x80u, 28, 2, 44, 10, 0, 0, 64},
                    {11, 12, 4, 36, 14, 0, 0, 64}};

                for (unsigned int i = 0; i < sizeof(i915_ops) / sizeof(i915_ops[0]); i++)
                {
                        unsigned int address_to q = ops + 8 * n;

                        memory_copy(q, i915_ops[i], 32);
                        if (i >= 2)
                                q[5] = (unsigned int)(GPU_VA + src_at), q[6] = (unsigned int)((GPU_VA + src_at) >> 32);
                        n++;
                }
                memory_zero(pic, W2 * H2 * 4);
                patient = gpu_patient, gpu_patient = gpu_recovered ? 2 : 1;
                done = gpu_scene(ops_at, n, bins_at, pic_at, W2, W2, H2, 0, 0, W2, H2);
                gpu_patient = patient;
                if (!done)
                        return 0;
                for (unsigned int y = 0; y < H2; y++)
                        for (unsigned int x = 0; x < W2; x++)
                        {
                                unsigned int c = 0xff203040u;

                                if (x >= 4 && x < 48 && y >= 2 && y < 12)
                                        c = gpu_test_overv(c, 0x80403020u);
                                if (x >= 8 && x < 24 && y >= 1 && y < 9)
                                        c = src[(y - 1) * 64 + (x - 8)];
                                if (x >= 28 && x < 44 && y >= 2 && y < 10)
                                {
                                        unsigned int p = src[(y - 2) * 64 + (x - 28)];
                                        c = (p & 0xff00ff00u) | ((p >> 16) & 0xffu) | ((p & 0xffu) << 16);
                                }
                                if (x >= 12 && x < 36 && y >= 4 && y < 14)
                                        c = gpu_test_overv(c, src[(y - 4) * 64 + (x - 12)]);
                                if (pic[y * W2 + x] != c && ok)
                                        ok = false, gpu_test_say(1, x, y, pic[y * W2 + x], c);
                                else if (pic[y * W2 + x] != c)
                                        ok = false;
                        }
                if (!ok)
                        return gpu_set->hash ^ 2u;
                // A second scene over what the first drew, rows 4 to 11 alone, that no record covers whole: the card reads the picture and lays a tone
                // and a picture over it (an image's tile that starts left of and above its record), and leaves the rows outside the damage as they were.
                for (unsigned int i = 0; i < W2 * H2; i++)
                        want[i] = pic[i];
                {
                        static const unsigned int again[][8] = {{1, 2, 3, 30, 13, 0x40102030u, 0, 0}, {11, 37, 5, 61, 11, 0, 0, 64}};

                        for (n = 0; n < 2; n++)
                                memory_copy(ops + 8 * n, again[n], 32);
                        ops[8 + 5] = (unsigned int)(GPU_VA + src_at), ops[8 + 6] = (unsigned int)((GPU_VA + src_at) >> 32);
                }
                patient = gpu_patient, gpu_patient = gpu_recovered ? 2 : 1;
                done = gpu_scene(ops_at, 2, bins_at, pic_at, W2, W2, H2, 0, 4, W2, 12);
                gpu_patient = patient;
                if (!done)
                        return 0;
                for (unsigned int y = 0; y < H2; y++)
                        for (unsigned int x = 0; x < W2; x++)
                        {
                                unsigned int c = want[y * W2 + x];

                                if (y >= 4 && y < 12 && x >= 2 && x < 30 && y >= 3 && y < 13)
                                        c = gpu_test_overv(c, 0x40102030u);
                                if (y >= 5 && y < 11 && x >= 37 && x < 61)
                                        c = gpu_test_overv(c, src[(y - 5) * 64 + (x - 37)]);
                                if (pic[y * W2 + x] != c && ok)
                                        ok = false, gpu_test_say(2, x, y, pic[y * W2 + x], c);
                                else if (pic[y * W2 + x] != c)
                                        ok = false;
                        }
                return ok ? gpu_set->hash : (gpu_set->hash ^ 4u);
        }
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
        // The first job of a card just opened is waited for as long as a hang is (the card is slow to it); a late self-test would be no card. One started again
        // waits GPU_RECOVER_NS (gpu_run_job), since that start is on the loop.
        patient = gpu_patient, gpu_patient = gpu_recovered ? 2 : 1;
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


/*
        The texture unit's read held to what it must give (op 8 over a linear resource: the one layout the processor can make and check), both ways it reads
        (32_UINT's word and UNORM's channels): 64 by 4 pixels of numbers read whole, then a rectangle of them from (3, 1) with red and blue the other way (an ABGR
        picture's), each pixel compared.
        True when both are what they must be; the card's buffer's last megabyte is the self-test's, and the card is idle here (the self-test waited).
*/
static b32 gpu_image_test(void)
{
        positive base = gpu.size - (1 << 20), src_at = base + 131072, dst_at = base + 262144;
        unsigned int address_to src = (unsigned int address_to)((p8 address_to)gpu.at + src_at), address_to dst = (unsigned int address_to)((p8 address_to)gpu.at + dst_at);
        unsigned int desc[8], seed = 777u, kind;
        int was_patient = gpu_patient;
        b32 ok = true;

        gpu_patient = gpu_recovered ? 2 : 1;
        for (unsigned int i = 0; i < 64 * 4; i++)
                seed = seed * 1664525u + 1013904223u, src[i] = seed;
        // Each way the kernel reads: the word as it is (32_UINT), and the four channels as UNORM bytes (DCC's way); each straight, then swapped from (3, 1).
        for (unsigned int unorm = 0; unorm < 2 && ok; unorm++)
        {
                memory_zero(dst, 64 * 4 * 4);
                if (!(kind = gpu_amd_image(desc, GPU_VA + src_at, 0, 64, 4, 0, false, unorm)) || !gpu_image(desc, kind, 0, 0, GPU_VA + dst_at, 64, 64, 4))
                {
                        ok = false;
                        break;
                }
                ok = !memory_compare(dst, src, 64 * 4 * 4);
                memory_zero(dst, 64 * 4 * 4);
                if (!ok || !(kind = gpu_amd_image(desc, GPU_VA + src_at, 0, 64, 4, 0, true, unorm)) || !gpu_image(desc, kind, 3, 1, GPU_VA + dst_at, 64, 50, 3))
                {
                        ok = false;
                        break;
                }
                for (unsigned int y = 0; y < 3 && ok; y++)
                        for (unsigned int x = 0; x < 50 && ok; x++)
                        {
                                unsigned int c = src[(y + 1) * 64 + x + 3];

                                ok = dst[y * 64 + x] == ((c & 0xff00ff00u) | (c >> 16 & 0xff) | (c & 0xff) << 16);
                        }
        }
        // The scaled read (bit 3 of op 8): the tables at tab_at are 50 columns of the resource (x * 5 + 3, mod 64) and 3 rows ((y * 2 + 1), mod 4), and each
        // word is read as it is (32_UINT): dst[y * 64 + x] == src[ytab[y] * 64 + xtab[x]]. A card that takes the wrong table or the wrong row draws the wrong word,
        // and gpu_image_ok stays false: tiled pictures are then the processor's.
        if (ok)
        {
                positive tab_at = base + 393216;
                unsigned int address_to tab = (unsigned int address_to)((p8 address_to)gpu.at + tab_at);
                unsigned int desc[8], kind;

                for (unsigned int x = 0; x < 50; x++)
                        tab[x] = (x * 5 + 3) % 64;
                for (unsigned int y = 0; y < 3; y++)
                        tab[50 + y] = (y * 2 + 1) % 4;
                memory_zero(dst, 64 * 4 * 4);
                kind = gpu_amd_image(desc, GPU_VA + src_at, 0, 64, 4, 0, false, false);
                gpu_slot_take();
                if (!kind)
                        ok = false;
                else
                {
                        gpu_patient = gpu_recovered ? 2 : 1;
                        gpu_image_scaled_args(desc, kind | GPU_IMAGE_SCALED, GPU_VA + tab_at, 0, 0, GPU_VA + dst_at, 64, 50, 3);
                        ok = gpu_run();
                        for (unsigned int y = 0; y < 3 && ok; y++)
                                for (unsigned int x = 0; x < 50 && ok; x++)
                                        ok = dst[y * 64 + x] == src[((y * 2 + 1) % 4) * 64 + (x * 5 + 3) % 64];
                }
        }
        if (gpu.pending)
                gpu_finish();
        gpu_patient = was_patient;
        return ok;
}

// An NV12 pixel as op 9 makes it and compositor_wl_decode_pixel does (BT.601, limited range), for the test below.
static unsigned int gpu_nv12_expect(int luma, int u, int v)
{
        int c = luma - 16 < 0 ? 0 : luma - 16, d = u - 128, e = v - 128;
        int r = (298 * c + 409 * e + 128) >> 8, g = (298 * c - 100 * d - 208 * e + 128) >> 8, b = (298 * c + 516 * d + 128) >> 8;

        r = r < 0 ? 0 : r > 255 ? 255 : r, g = g < 0 ? 0 : g > 255 ? 255 : g, b = b < 0 ? 0 : b > 255 ? 255 : b;
        return 0xff000000u | (unsigned int)r << 16 | (unsigned int)g << 8 | (unsigned int)b;
}

/*
        The NV12 kernel (op 9) held to the colour the processor gives (gpu_nv12_expect, compositor_wl_decode_pixel's arithmetic): 64 by 4 pixels of a
        luma of numbers and its 2 by 2 chroma, the tables the identity, each pixel compared exactly. The card's start trusted the scene and the texture
        unit's read and never ran this one, which every video a client shows on the card goes through. True when every pixel is right.
*/
static b32 gpu_nv12_test(void)
{
        positive base = gpu.size - (1 << 20), luma_at = base + 131072, chroma_at = luma_at + 4096, dst_at = base + 262144, tab_at = base + 393216;
        p8 address_to luma = (p8 address_to)gpu.at + luma_at, address_to chroma = (p8 address_to)gpu.at + chroma_at;
        unsigned int address_to dst = (unsigned int address_to)((p8 address_to)gpu.at + dst_at), address_to tab = (unsigned int address_to)((p8 address_to)gpu.at + tab_at);
        unsigned int seed = 4242u;
        int was_patient = gpu_patient;
        b32 ok, done;

        gpu_patient = gpu_recovered ? 2 : 1;
        for (unsigned int i = 0; i < 64 * 4; i++)
                seed = seed * 1664525u + 1013904223u, luma[i] = (p8)(seed >> 24);
        for (unsigned int i = 0; i < 64 * 2; i++)
                seed = seed * 1664525u + 1013904223u, chroma[i] = (p8)(seed >> 24);
        for (unsigned int x = 0; x < 64; x++)
                tab[x] = x;
        for (unsigned int y = 0; y < 4; y++)
                tab[64 + y] = y;
        memory_zero(dst, 64 * 4 * 4);
        gpu_nv12_trying = 1;
        done = gpu_nv12(GPU_VA + luma_at, 64, GPU_VA + chroma_at, 64, GPU_VA + dst_at, 64, GPU_VA + tab_at, 64, 4);
        gpu_nv12_trying = 0;
        if (gpu.pending)
                gpu_finish();
        gpu_patient = was_patient;
        ok = done;
        for (unsigned int y = 0; y < 4 && ok; y++)
                for (unsigned int x = 0; x < 64 && ok; x++)
                        ok = dst[y * 64 + x] == gpu_nv12_expect(luma[y * 64 + x], chroma[(y >> 1) * 64 + (x & ~1u)], chroma[(y >> 1) * 64 + (x & ~1u) + 1]);
        return ok;
}

#endif
