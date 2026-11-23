/*
        test/perf/scanfb_cost.c: what a scanout framebuffer of a client's buffer costs the compositor's loop to make, on the real card. It
        makes a linear XRGB buffer the size asked on the render node (gbm), exports it, and on the display node times, N times over, what
        canvas_scan_fb_make did for every buffer new to a row (PRIME_FD_TO_HANDLE, ADDFB2 with modifiers, GEM_CLOSE) and the RMFB that
        followed once the buffer was off the screen; and what the keep of made framebuffers costs instead (the statx of the descriptor that
        names the buffer). None of these needs the display: no master is asked, nothing is shown.

            cc -O2 -o scanfb_cost test/perf/scanfb_cost.c $(pkg-config --cflags --libs gbm libdrm)
            scanfb_cost [CARD] [N] [WIDTH] [HEIGHT]     (defaults /dev/dri/card0, 2000, 1920, 1080)
*/
#define _GNU_SOURCE
#include <drm.h>
#include <drm_fourcc.h>
#include <drm_mode.h>
#include <fcntl.h>
#include <gbm.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static uint64_t now_ns(void)
{
        struct timespec t;

        clock_gettime(CLOCK_MONOTONIC, &t);
        return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

static int compare(const void *a, const void *b)
{
        uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;

        return x < y ? -1 : x > y;
}

static void say(const char *what, uint64_t *t, int n)
{
        uint64_t sum = 0;

        qsort(t, (size_t)n, sizeof(*t), compare);
        for (int at = 0; at < n; at++)
                sum += t[at];
        printf("%-34s p50 %7.2f us  p90 %7.2f  p99 %7.2f  mean %7.2f\n", what, t[n / 2] / 1e3, t[n * 9 / 10] / 1e3, t[n * 99 / 100] / 1e3, sum / 1e3 / n);
}

int main(int argc, char **argv)
{
        const char *card_path = argc > 1 ? argv[1] : "/dev/dri/card0";
        int n = argc > 2 ? atoi(argv[2]) : 2000, w = argc > 3 ? atoi(argv[3]) : 1920, h = argc > 4 ? atoi(argv[4]) : 1080;
        int render = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC), card = open(card_path, O_RDWR | O_CLOEXEC), fd;
        struct gbm_device *gbm;
        struct gbm_bo *bo;
        uint64_t *prime, *addfb, *close_t, *rmfb, *whole, *ident;

        if (render < 0 || card < 0 || n < 10 || !(gbm = gbm_create_device(render)))
                return fprintf(stderr, "scanfb_cost: no render node, no %s, or N under 10\n", card_path), 2;
        bo = gbm_bo_create(gbm, (uint32_t)w, (uint32_t)h, GBM_FORMAT_XRGB8888, GBM_BO_USE_LINEAR | GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
        if (!bo || (fd = gbm_bo_get_fd(bo)) < 0)
                return fprintf(stderr, "scanfb_cost: gbm made no linear %dx%d buffer\n", w, h), 2;
        prime = calloc((size_t)n, 8), addfb = calloc((size_t)n, 8), close_t = calloc((size_t)n, 8), rmfb = calloc((size_t)n, 8);
        whole = calloc((size_t)n, 8), ident = calloc((size_t)n, 8);
        for (int at = 0; at < n; at++)
        {
                struct drm_prime_handle p = {.fd = fd};
                struct drm_mode_fb_cmd2 f = {0};
                struct drm_gem_close c = {0};
                uint64_t t0 = now_ns(), t1, t2, t3, t4;

                if (ioctl(card, DRM_IOCTL_PRIME_FD_TO_HANDLE, &p))
                        return perror("scanfb_cost: PRIME_FD_TO_HANDLE"), 1;
                t1 = now_ns();
                f.width = (uint32_t)w, f.height = (uint32_t)h, f.pixel_format = DRM_FORMAT_XRGB8888, f.flags = DRM_MODE_FB_MODIFIERS;
                f.handles[0] = p.handle, f.pitches[0] = gbm_bo_get_stride(bo), f.offsets[0] = 0, f.modifier[0] = DRM_FORMAT_MOD_LINEAR;
                if (ioctl(card, DRM_IOCTL_MODE_ADDFB2, &f))
                        return perror("scanfb_cost: ADDFB2"), 1;
                t2 = now_ns();
                c.handle = p.handle;
                ioctl(card, DRM_IOCTL_GEM_CLOSE, &c);
                t3 = now_ns();
                if (ioctl(card, DRM_IOCTL_MODE_RMFB, &f.fb_id))
                        return perror("scanfb_cost: RMFB"), 1;
                t4 = now_ns();
                prime[at] = t1 - t0, addfb[at] = t2 - t1, close_t[at] = t3 - t2, rmfb[at] = t4 - t3, whole[at] = t4 - t0;
        }
        for (int at = 0; at < n; at++)
        {
                struct statx sx;
                uint64_t t0 = now_ns();

                statx(fd, "", AT_EMPTY_PATH, STATX_INO, &sx);
                ident[at] = now_ns() - t0;
        }
        printf("scanfb_cost: %s, linear XRGB8888 %dx%d, %d framebuffers made and removed\n", card_path, w, h, n);
        say("PRIME_FD_TO_HANDLE", prime, n);
        say("ADDFB2", addfb, n);
        say("GEM_CLOSE", close_t, n);
        say("RMFB", rmfb, n);
        say("a new buffer's framebuffer, whole", whole, n);
        say("the keep's ask instead (statx)", ident, n);
        return 0;
}
