// Canvas on the GPU: AMD compute, raw. One buffer holds the packets, the arguments, the kernel and the pixels.

#define GPU_IO(dir, n, size) ((dir) << 30 | (size) << 16 | 0x6400u | (0x40 + (n)))
#define GPU_PKT(op, n) (0xc0000002u | (op) << 8 | (n) << 16) // type 3, for the compute shader type
#define GPU_VA 0x1000000000ull
#define GPU_DATA 4096 // the pixels start here; before them: packets at 0, arguments at 512, the kernel at 1024

static struct { int fd; unsigned int ctx; unsigned int address_to at; } gpu;

// rsrc1, rsrc2, then the code of a 2D copy, 16 bytes a lane: clang -mcpu=gfx1036 -mwavefrontsize64 -O3, ld.lld -shared.
static const unsigned int gpu_cp[] = {
    0xe0af0001, 0x0000018c, 0xf4000002, 0xfa000018, 0xd76f0000, 0x04010c06, 0xbf8cc07f, 0x7d880000, 0xbe80246a, 0xbf88001d,
    0xbfa10001, 0xf4040202, 0xfa000010, 0xf4080002, 0xfa000000, 0xbf8cc07f, 0xd5767d04, 0x04000e08, 0x7e0a0280, 0xd6ff0001,
    0x02020884, 0xd70f6a01, 0x02020200, 0xd5287d02, 0x01aa0401, 0xdc388000, 0x017d0001, 0xbf8c3f70, 0xd5767d06, 0x04000e09,
    0x7e0e0305, 0xd6ff0005, 0x02020c84, 0xd70f6a05, 0x02020a02, 0xd5287d06, 0x01aa0c03, 0xdc788000, 0x007d0105, 0xbf810000};

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

// Copy rows of bytes (a multiple of 16) from offset from to offset to in the buffer, pitches in bytes; false if it did not finish.
static b32 gpu_copy(positive from, unsigned int from_pitch, positive to, unsigned int to_pitch, unsigned int bytes, unsigned int rows)
{
        positive code = GPU_VA + 1024, args = GPU_VA + 512;
        unsigned int w4 = bytes / 16, ib[] = {
            GPU_PKT(0x76, 2), 0x20c, (unsigned int)(code >> 8), (unsigned int)(code >> 40),
            GPU_PKT(0x76, 2), 0x212, gpu_cp[0], gpu_cp[1], GPU_PKT(0x76, 1), 0x218, 0x100,
            GPU_PKT(0x76, 6), 0x240, 0, 0, 0, 0, (unsigned int)args, (unsigned int)(args >> 32),
            GPU_PKT(0x76, 1), 0x215, 0, GPU_PKT(0x76, 3), 0x207, 64, 1, 1,
            GPU_PKT(0x15, 3), (w4 + 63) / 64, rows, 1, 0x45};
        p64 *arg = (p64 address_to)(gpu.at + 128), ibc[4] = {0, GPU_VA, sizeof(ib) | 1ull << 32, 0},
            chunk[2] = {1 | 8ull << 32, (positive)ibc}, list = (positive)chunk, cs[3] = {gpu.ctx, 1, (positive)&list},
            wait[4] = {0, 0, 1, (positive)gpu.ctx << 32};

        arg[0] = GPU_VA + from, arg[1] = GPU_VA + to;
        ((unsigned int address_to)arg)[4] = from_pitch / 16, ((unsigned int address_to)arg)[5] = to_pitch / 16, ((unsigned int address_to)arg)[6] = w4;
        memory_copy(gpu.at, ib, sizeof(ib));
        if (!gpu_io(3u, 4, 24, cs))
                return false;
        wait[0] = cs[0], wait[1] = clock_monotonic_nanoseconds() + 2000000000ull;
        return gpu_io(3u, 9, 32, wait) && wait[0] == 0;
}
