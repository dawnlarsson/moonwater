// Canvas on the GPU: AMD compute, raw. One buffer holds the packets, the arguments, the kernel and the pixels.

#define GPU_IO(dir, n, size) ((dir) << 30 | (size) << 16 | 0x6400u | (0x40 + (n)))
#define GPU_PKT(op, n) (0xc0000002u | (op) << 8 | (n) << 16) // type 3, for the compute shader type
#define GPU_VA 0x1000000000ull
#define GPU_DATA 4096 // the pixels start here; before them: packets at 0, arguments at 512, the kernel at 1024

static struct { int fd; unsigned int ctx; unsigned int address_to at; } gpu;

// rsrc1, rsrc2, then the code of the one kernel (cv.c: copy, fill, tone over, glass over; op in the arguments): clang -mcpu=gfx1036 -mwavefrontsize64 -O3, ld.lld -shared.
static const unsigned int gpu_cp[] = {
    0xe0af0002, 0x0000018c, 0xf4040202, 0xfa000020, 0xd76f0001, 0x04010c06, 0xbe80047e, 0xbf8cc07f, 0x7da80209, 0xbf880084,
    0xbfa10003, 0xf4000242, 0xfa00001c, 0xf4040282, 0xfa000008, 0xf4000182, 0xfa000028, 0xf4080002, 0xfa00002c, 0x7e040280,
    0xbe8d0380, 0xd6ff0002, 0x02020282, 0xbf8cc07f, 0x930c0709, 0x8f8c820c, 0x80030c0a, 0x82090d0b, 0xd70f6a02, 0x02020403,
    0xd5287d03, 0x01aa0609, 0xbf048106, 0xbe8a0480, 0xbf850050, 0x7e080200, 0xbe8e0480, 0xbe9204c1, 0xbf028106, 0xbe900480,
    0xbf84004d, 0xbf068206, 0xbf8400f0, 0xbf08ff00, 0x00ffffff, 0xbe920480, 0xbf840047, 0xdc308000, 0x047d0002, 0xbe830700,
    0x9389ff00, 0x00080008, 0x90039803, 0x938cff00, 0x00080010, 0xbe9204c1, 0xbf8c3f70, 0x160a06f9, 0x86000604, 0x160c06f9,
    0x86010604, 0x160e06f9, 0x86020604, 0x160806f9, 0x86030604, 0x8703ff00, 0x000000ff, 0x4a100aff, 0x0000007f, 0x4a120cff,
    0x0000007f, 0x4a140eff, 0x0000007f, 0x4a1608ff, 0x0000007f, 0x2c101088, 0x2c121288, 0x2c141488, 0xd76d0005, 0x03fe1105,
    0x00000080, 0xd76d0006, 0x03fe1306, 0x00000080, 0x2c101688, 0xd76d0007, 0x03fe1507, 0x00000080, 0x2c0a0a88, 0x2c0c0c88,
    0xd76d0004, 0x03fe1104, 0x00000080, 0x2c0e0e88, 0x7e1002ff, 0x000000ff, 0x4a0a0a03, 0x4a0c0c09, 0x2c080888, 0x90039800,
    0x4a0e0e0c, 0x260a0aff, 0x000000ff, 0x260c0cff, 0x000000ff, 0x4a080803, 0x260e10f9, 0x06060507, 0xd76f0005, 0x04151106,
    0x260810f9, 0x06060304, 0xd7720004, 0x04120f05, 0xbf820003, 0xbe8e04c1, 0xbe920480, 0xbe900480, 0xf4040302, 0xfa000000,
    0x87ea0e7e, 0xbf860003, 0xbf078006, 0xbe8a04c1, 0x859080c1, 0xf40000c2, 0xfa000018, 0x878e7e10, 0x85068081, 0xbf078106,
    0xbf84000c, 0x87807e0a, 0x85008081, 0xbf078100, 0xbf840082, 0x87807e12, 0x85008081, 0xbf078100, 0xbf850003, 0xbf8c3f70,
    0xdc708000, 0x007d0402, 0xbf810000, 0xf4040102, 0xfa000010, 0xbf8cc07f, 0xd5767d04, 0x04040e03, 0x7e0a0280, 0xd5767d06,
    0x04040e08, 0x4a000001, 0x81010702, 0xbe8a0480, 0x7e0e0305, 0xbe9204c1, 0xd6ff0004, 0x02020882, 0x3a000001, 0x87018101,
    0xd6ff0006, 0x02020c82, 0xbf068000, 0xd70f6a04, 0x0202080c, 0xd5287d05, 0x01aa0a0d, 0x34000081, 0xd70f6a06, 0x02020c04,
    0xd5287d07, 0x01aa0e05, 0xd7710000, 0x00050500, 0x85ea80c1, 0xdc308000, 0x047d0004, 0xdc308000, 0x057d0006, 0xbf8c3f71,
    0x7e0c6f04, 0xbf8c3f70, 0x4a0e00f9, 0x06000605, 0x4a1000f9, 0x06010605, 0x4c1200f9, 0x06002605, 0x4a1400f9, 0x06020605,
    0x4c1600f9, 0x06012605, 0x260e0eff, 0x000000ff, 0x261010ff, 0x000000ff, 0x4c0000f9, 0x06022605, 0x261414ff, 0x000000ff,
    0x160a0cf9, 0x03030605, 0x020e0f09, 0x0210110b, 0x02001500, 0x160e0cf9, 0x03060607, 0x16100cf9, 0x03060608, 0x16000cf9,
    0x03060600, 0x4a120eff, 0x0000007f, 0x4a1410ff, 0x0000007f, 0x4a1600ff, 0x0000007f, 0x2c121288, 0x2c141488, 0x2c0c1688,
    0xd76d0007, 0x03fe1307, 0x00000080, 0xd76d0008, 0x03fe1508, 0x00000080, 0x4a120aff, 0x0000007f, 0xd76d0000, 0x03fe0d00,
    0x00000080, 0x2c0c0e88, 0x2c0e1088, 0x2c101288, 0x2c000088, 0x4a0c08f9, 0x00060606, 0x4a0e08f9, 0x01060607, 0xd76d0005,
    0x03fe1105, 0x00000080, 0x4a0008f9, 0x02060600, 0x260c0cff, 0x000000ff, 0x260e0eff, 0x000000ff, 0x2c0a0a88, 0x260000ff,
    0x000000ff, 0xd76f0006, 0x04191107, 0x4a0808f9, 0x03060605, 0xd76f0000, 0x04192100, 0x260808ff, 0x000000ff, 0xd76f0004,
    0x04013104, 0x87807e0a, 0x85008081, 0xbf078100, 0xbf85ff7e, 0xbf8cc07f, 0xd5767d00, 0x04040e03, 0x7e020280, 0xbe9204c1,
    0xd6ff0000, 0x02020082, 0xd70f6a00, 0x0202000c, 0xd5287d01, 0x01aa020d, 0xdc308000, 0x047d0000, 0x87807e12, 0x85008081,
    0xbf078100, 0xbf84ff71, 0xbf82ff73, 0xbe9004c1, 0xbe920480, 0xbf82ff58};

#define gpu_io(dir, n, size, a) (system_control(gpu.fd, GPU_IO(dir, n, size), a) == 0)

// The card's first render node with bytes of pixels behind the page of commands, and one context; false if any step is refused.
static b32 gpu_start(positive bytes)
{
        positive size = (GPU_DATA + bytes + 4095) & ~4095ull;
        p64 make[4] = {size, 4096, 2, 64}, map[1] = {0}, va[8] = {0, 1 | 14ull << 32, GPU_VA, 0, size};
        unsigned int ctx[4] = {1, 0, 0, 0};
        long at;

        gpu.fd = (int)system_open_at(AT_FDCWD, (string_address) "/dev/dri/renderD128", 2 | O_CLOEXEC);
        if (gpu.fd < 0 || !gpu_io(3u, 0, 32, make))
                return false;
        va[0] = map[0] = (unsigned int)make[0];
        if (!gpu_io(3u, 1, 8, map) || !gpu_io(1u, 8, 64, va) || !gpu_io(3u, 2, 16, ctx))
                return false;
        at = system_call_6(syscall(mmap), 0, size, 3, 1, (positive)gpu.fd, map[0]);
        if (at < 0 && at > -4096)
                return false;
        gpu.at = (unsigned int address_to)at, gpu.ctx = ctx[0];
        memory_copy(gpu.at + 256, gpu_cp + 2, sizeof(gpu_cp) - 8);
        return true;
}

// The arguments the kernel reads, at byte 512 of the buffer: s, d, g are GPU addresses (GPU_VA + an offset), pitches in pixels, op 0 copy
// s to d, 1 fill d with v, 2 tone v over d, 3 glass: s over g into d (v the light side, x0 y0 where the rect is, for the grain).
struct gpu_args { positive s, d, g; unsigned int sp, dp, gp, w, op, v, x0, y0, rows; };
#define GPU_ARGS ((struct gpu_args address_to)(gpu.at + 128))

// Run the kernel over the arguments' w by rows pixels; false if it did not finish.
static b32 gpu_run(void)
{
        positive code = GPU_VA + 1024, args = GPU_VA + 512;
        unsigned int ib[] = {
            GPU_PKT(0x76, 2), 0x20c, (unsigned int)(code >> 8), (unsigned int)(code >> 40),
            GPU_PKT(0x76, 2), 0x212, gpu_cp[0], gpu_cp[1], GPU_PKT(0x76, 1), 0x218, 0x100,
            GPU_PKT(0x76, 6), 0x240, 0, 0, 0, 0, (unsigned int)args, (unsigned int)(args >> 32),
            GPU_PKT(0x76, 1), 0x215, 0, GPU_PKT(0x76, 3), 0x207, 64, 1, 1,
            GPU_PKT(0x15, 3), (GPU_ARGS->w + 63) / 64, GPU_ARGS->rows, 1, 0x45};
        p64 ibc[4] = {0, GPU_VA, sizeof(ib) | 1ull << 32, 0}, chunk[2] = {1 | 8ull << 32, (positive)ibc}, list = (positive)chunk,
            cs[3] = {gpu.ctx, 1, (positive)&list}, wait[4] = {0, 0, 1, (positive)gpu.ctx << 32};

        memory_copy(gpu.at, ib, sizeof(ib));
        if (!gpu_io(3u, 4, 24, cs))
                return false;
        wait[0] = cs[0], wait[1] = clock_monotonic_nanoseconds() + 2000000000ull;
        return gpu_io(3u, 9, 32, wait) && wait[0] == 0;
}
