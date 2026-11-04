/*
 *      gpu_scan_sample: the headless compositor's scan image, read as fast as
 *      a reader can copy it, for test/gpu_anim_flicker.py.
 *
 *      Each distinct frame is appended to LOG as a record: a uint64 of
 *      CLOCK_MONOTONIC nanoseconds at the start of the copy, a uint32 frame
 *      number, a uint32 row count, then one uint64 hash per row (FNV-1a over
 *      the row's bytes). The copy is of the whole image, taken while the
 *      compositor writes it in place, so a frame that was half written when it
 *      was read is a frame of its own; the classifier sees the tear in it.
 *
 *          gpu_scan_sample SCAN WIDTH HEIGHT LOG [SECONDS]
 *
 *      Runs until SECONDS have passed (default: until SIGTERM). With
 *      SCAN_FRAMES=DIR in the environment, every distinct frame's pixels are
 *      also written to DIR/frame-NNNNNN.raw, for looking at a torn one.
 *      Built by gcc and clang as a hosted helper: libc only.
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stop;

static void on_signal(int sig)
{
        (void)sig;
        stop = 1;
}

static uint64_t now_ns(void)
{
        struct timespec t;

        clock_gettime(CLOCK_MONOTONIC, &t);
        return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

static uint64_t row_hash(const unsigned char *row, size_t bytes)
{
        uint64_t h = 1469598103934665603ull;
        size_t i = 0;

        for (; i + 8 <= bytes; i += 8)
        {
                uint64_t w;

                memcpy(&w, row + i, 8);
                h = (h ^ w) * 1099511628211ull;
        }
        for (; i < bytes; i++)
                h = (h ^ row[i]) * 1099511628211ull;
        return h;
}

int main(int argc, char **argv)
{
        struct sigaction act;
        const char *scan, *log_path, *frames_dir;
        unsigned long width, height, rows;
        size_t bytes;
        unsigned char *cur, *last;
        uint64_t *hashes;
        const unsigned char *map;
        int fd, have = 0;
        uint32_t seq = 0;
        double seconds = 0;
        uint64_t start, waited = 0;
        FILE *out;

        if (argc < 5)
        {
                fprintf(stderr, "usage: %s SCAN WIDTH HEIGHT LOG [SECONDS]\n", argv[0]);
                return 2;
        }
        scan = argv[1];
        width = strtoul(argv[2], NULL, 10);
        height = strtoul(argv[3], NULL, 10);
        log_path = argv[4];
        if (argc > 5)
                seconds = strtod(argv[5], NULL);
        frames_dir = getenv("SCAN_FRAMES");
        if (!width || !height || width > 16384 || height > 16384)
                return 2;
        rows = height;
        bytes = (size_t)width * 4 * height;

        memset(&act, 0, sizeof(act));
        act.sa_handler = on_signal;
        sigaction(SIGTERM, &act, NULL);
        sigaction(SIGINT, &act, NULL);

        // The compositor makes the scan image before it draws; wait for it (a minute at most).
        for (;;)
        {
                struct stat st;

                fd = open(scan, O_RDONLY | O_CLOEXEC);
                if (fd >= 0 && fstat(fd, &st) == 0 && (size_t)st.st_size >= bytes)
                        break;
                if (fd >= 0)
                        close(fd);
                if (stop || waited > 60000000000ull)
                {
                        fprintf(stderr, "gpu_scan_sample: no scan image %s of %zu bytes\n", scan, bytes);
                        return 1;
                }
                usleep(1000);
                waited += 1000000ull;
        }
        map = mmap(NULL, bytes, PROT_READ, MAP_SHARED, fd, 0);
        if (map == MAP_FAILED)
        {
                fprintf(stderr, "gpu_scan_sample: mmap %s: %s\n", scan, strerror(errno));
                return 1;
        }
        cur = malloc(bytes);
        last = malloc(bytes);
        hashes = malloc(rows * sizeof(uint64_t));
        out = fopen(log_path, "wb");
        if (!cur || !last || !hashes || !out)
        {
                fprintf(stderr, "gpu_scan_sample: no memory or no log %s\n", log_path);
                return 1;
        }
        setvbuf(out, NULL, _IOFBF, 1 << 20);

        start = now_ns();
        while (!stop)
        {
                uint64_t t0 = now_ns();
                uint32_t n = (uint32_t)rows;
                uint32_t w = (uint32_t)(bytes / rows);

                if (seconds > 0 && (double)(t0 - start) / 1e9 >= seconds)
                        break;
                memcpy(cur, map, bytes);
                if (have && memcmp(cur, last, bytes) == 0)
                        continue;
                for (uint32_t r = 0; r < n; r++)
                        hashes[r] = row_hash(cur + (size_t)r * w, w);
                fwrite(&t0, sizeof(t0), 1, out);
                fwrite(&seq, sizeof(seq), 1, out);
                fwrite(&n, sizeof(n), 1, out);
                fwrite(hashes, sizeof(uint64_t), rows, out);
                if (frames_dir)
                {
                        char name[512];
                        FILE *raw;

                        snprintf(name, sizeof(name), "%s/frame-%06u.raw", frames_dir, (unsigned)seq);
                        raw = fopen(name, "wb");
                        if (raw)
                                fwrite(cur, 1, bytes, raw), fclose(raw);
                }
                memcpy(last, cur, bytes);
                have = 1;
                seq++;
        }
        fclose(out);
        munmap((void *)map, bytes);
        close(fd);
        printf("gpu_scan_sample: %u distinct frames in %.1f s\n", (unsigned)seq, (double)(now_ns() - start) / 1e9);
        return 0;
}
