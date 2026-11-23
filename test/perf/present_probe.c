/*
        test/perf/present_probe.c: a Wayland client (fullscreen, or a window with -w) that times its own frames from commit to the screen, for the present path's
        measurements (tearing policy, latch, direct scanout). It draws into linear dma-bufs from the render node (gbm), asks
        wp_presentation feedback for every commit, and at the end says, one block on standard output:
          - frames committed, presented, discarded (replaced before they were latched), and presented with each flag;
          - commit to presented (the card's vblank stamp, or the compositor's clock for a torn flip): p50, p90, p99, worst;
          - frame callback to commit and to presented (callback-paced runs), and the feedback's arrival less its stamp
            (how long the compositor took to tell);
          - each presentation flag counted (vsync, hw_clock, hw_completion, zero_copy: a direct flip says zero_copy);
          - how long each shown frame stayed up, in refreshes by the feedback's sequence (1, 2, 3+), and for torn frames
            where the tear was (the quarter of the period past the vblank before it).

            present_probe [-r FPS] [-c US] [-a | -l] [-W FRACTION] [-w WIDTHxHEIGHT] [-s SECONDS] [-q]

        -r frames a second, free-running on the clock (0, the default: one frame per frame callback, as a vsync'd game);
        -c a render's cost in microseconds, spent busy before each commit (a frame that takes that long);
        -a the tearing-control async hint on every commit; -l the hint only on a frame that is late (the client's own
        late rule: no frame was shown at the last vblank and it is within -W of the period past it, default 0.25);
        -w a window of that size instead of fullscreen (a composed frame, where the card's job runs);
        -s seconds to run (default 10); -q no per-second lines.

        Built in a bowl that has the libraries (test/present_probe.sh does it). Exit 0 when it ran, 2 when it could not start.
*/
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <gbm.h>
#include <linux/dma-buf.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

#include "linux-dmabuf-client.h"
#include "presentation-time-client.h"
#include "tearing-control-client.h"
#include "xdg-shell-client.h"

#define BUFFERS 3

struct buffer
{
        struct wl_buffer *buffer;
        struct gbm_bo *bo;
        void *data;
        int fd, busy;
        unsigned int stride;
        size_t size;
};

struct frame
{
        uint64_t committed, presented, callback, told; // commit, the stamp, the callback before it (0: none), the feedback's arrival
        uint64_t seq;
        uint32_t flags, refresh;
        int state; // 0 pending, 1 presented, 2 discarded
        int torn_hint;
};

static struct wl_display *display;
static struct wl_compositor *compositor;
static struct xdg_wm_base *wm_base;
static struct zwp_linux_dmabuf_v1 *dmabuf;
static struct wp_presentation *presentation;
static struct wp_tearing_control_manager_v1 *tearing_manager;
static struct wl_surface *surface;
static struct wp_tearing_control_v1 *tearing;
static struct gbm_device *gbm;
static struct buffer buffers[BUFFERS];
static struct frame *frames;
static size_t frames_room, frames_count;
static int width, height, configured, closed, callback_due = 1, hint_now = -1;
static uint64_t callback_at; // when the newest frame callback came
static uint64_t vblank_last, vblank_refresh; // the newest vsync'd stamp the feedback gave, and the period it said

static uint64_t now_ns(void)
{
        struct timespec t;

        clock_gettime(CLOCK_MONOTONIC, &t);
        return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

static void spin_until(uint64_t when)
{
        while (now_ns() < when)
                ;
}

static void buffer_release(void *data, struct wl_buffer *wl_buffer)
{
        (void)wl_buffer;
        ((struct buffer *)data)->busy = 0;
}
static const struct wl_buffer_listener buffer_listener = {buffer_release};

static void feedback_sync_output(void *data, struct wp_presentation_feedback *feedback, struct wl_output *output)
{
        (void)data, (void)feedback, (void)output;
}

static void feedback_presented(void *data, struct wp_presentation_feedback *feedback, uint32_t sec_hi, uint32_t sec_lo, uint32_t nsec,
                               uint32_t refresh, uint32_t seq_hi, uint32_t seq_lo, uint32_t flags)
{
        struct frame *f = &frames[(size_t)(uintptr_t)data];

        f->told = now_ns();
        f->seq = (uint64_t)seq_hi << 32 | seq_lo;
        f->presented = ((uint64_t)sec_hi << 32 | sec_lo) * 1000000000ull + nsec;
        f->refresh = refresh, f->flags = flags, f->state = 1;
        if (flags & WP_PRESENTATION_FEEDBACK_KIND_VSYNC)
                vblank_last = f->presented > vblank_last ? f->presented : vblank_last, vblank_refresh = refresh;
        wp_presentation_feedback_destroy(feedback);
}

static void feedback_discarded(void *data, struct wp_presentation_feedback *feedback)
{
        frames[(size_t)(uintptr_t)data].state = 2;
        wp_presentation_feedback_destroy(feedback);
}
static const struct wp_presentation_feedback_listener feedback_listener = {feedback_sync_output, feedback_presented, feedback_discarded};

static void frame_done(void *data, struct wl_callback *callback, uint32_t time)
{
        (void)data, (void)time;
        wl_callback_destroy(callback);
        callback_due = 1, callback_at = now_ns();
}
static const struct wl_callback_listener frame_listener = {frame_done};

static void wm_ping(void *data, struct xdg_wm_base *base, uint32_t serial)
{
        (void)data;
        xdg_wm_base_pong(base, serial);
}
static const struct xdg_wm_base_listener wm_listener = {wm_ping};

static void surface_configure(void *data, struct xdg_surface *xdg, uint32_t serial)
{
        (void)data;
        xdg_surface_ack_configure(xdg, serial);
        configured = 1;
}
static const struct xdg_surface_listener surface_listener = {surface_configure};

static void toplevel_configure(void *data, struct xdg_toplevel *toplevel, int32_t w, int32_t h, struct wl_array *states)
{
        (void)data, (void)toplevel, (void)states;
        if (w > 0 && h > 0)
                width = w, height = h;
}
static void toplevel_close(void *data, struct xdg_toplevel *toplevel)
{
        (void)data, (void)toplevel;
        closed = 1;
}
static void toplevel_bounds(void *data, struct xdg_toplevel *toplevel, int32_t w, int32_t h) { (void)data, (void)toplevel, (void)w, (void)h; }
static void toplevel_caps(void *data, struct xdg_toplevel *toplevel, struct wl_array *caps) { (void)data, (void)toplevel, (void)caps; }
static const struct xdg_toplevel_listener toplevel_listener = {toplevel_configure, toplevel_close, toplevel_bounds, toplevel_caps};

static void global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
        (void)data;
        if (!strcmp(interface, wl_compositor_interface.name))
                compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
        else if (!strcmp(interface, xdg_wm_base_interface.name))
                wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1), xdg_wm_base_add_listener(wm_base, &wm_listener, 0);
        else if (!strcmp(interface, zwp_linux_dmabuf_v1_interface.name))
                dmabuf = wl_registry_bind(registry, name, &zwp_linux_dmabuf_v1_interface, version < 3 ? version : 3);
        else if (!strcmp(interface, wp_presentation_interface.name))
                presentation = wl_registry_bind(registry, name, &wp_presentation_interface, 1);
        else if (!strcmp(interface, wp_tearing_control_manager_v1_interface.name))
                tearing_manager = wl_registry_bind(registry, name, &wp_tearing_control_manager_v1_interface, 1);
}
static void global_remove(void *data, struct wl_registry *registry, uint32_t name) { (void)data, (void)registry, (void)name; }
static const struct wl_registry_listener registry_listener = {global, global_remove};

static int buffer_make(struct buffer *b)
{
        struct zwp_linux_buffer_params_v1 *params;

        b->bo = gbm_bo_create(gbm, (uint32_t)width, (uint32_t)height, GBM_FORMAT_XRGB8888, GBM_BO_USE_LINEAR | GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
        if (!b->bo)
                b->bo = gbm_bo_create(gbm, (uint32_t)width, (uint32_t)height, GBM_FORMAT_XRGB8888, GBM_BO_USE_LINEAR | GBM_BO_USE_RENDERING);
        if (!b->bo)
                return 0;
        b->fd = gbm_bo_get_fd(b->bo), b->stride = gbm_bo_get_stride(b->bo);
        b->size = (size_t)b->stride * (size_t)height;
        b->data = mmap(0, b->size, PROT_READ | PROT_WRITE, MAP_SHARED, b->fd, 0);
        if (b->data == MAP_FAILED)
                return 0;
        params = zwp_linux_dmabuf_v1_create_params(dmabuf);
        zwp_linux_buffer_params_v1_add(params, (uint32_t)b->fd, 0, 0, b->stride, 0, 0);
        b->buffer = zwp_linux_buffer_params_v1_create_immed(params, width, height, GBM_FORMAT_XRGB8888, 0);
        zwp_linux_buffer_params_v1_destroy(params);
        wl_buffer_add_listener(b->buffer, &buffer_listener, b);
        return 1;
}

// A band that moves each frame, so every frame differs (and a tear is visible to an eye on the screen).
static void draw(struct buffer *b, unsigned int n)
{
        struct dma_buf_sync sync = {DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE};
        int band = (int)(n * 17u % (unsigned int)(width > 64 ? width - 64 : 1));

        ioctl(b->fd, DMA_BUF_IOCTL_SYNC, &sync);
        for (int y = 0; y < height; y++)
        {
                uint32_t *row = (uint32_t *)((char *)b->data + (size_t)y * b->stride);

                for (int x = 0; x < width; x += 64)
                        row[x] = 0xff202020u;
                for (int x = band; x < band + 64 && x < width; x++)
                        row[x] = n & 1 ? 0xffe0e0e0u : 0xff4080ffu;
        }
        sync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE;
        ioctl(b->fd, DMA_BUF_IOCTL_SYNC, &sync);
}

// The client's late rule (-l): nothing was shown at the newest vblank (the last frame presented before it and none pending), and
// it is no more than `window` of the period past it.
static int frame_late(uint64_t t, double window)
{
        uint64_t latest;

        if (!vblank_last || !vblank_refresh || t < vblank_last)
                return 0;
        for (size_t at = frames_count; at-- > 0 && frames[at].committed + 50000000ull > t;)
                if (!frames[at].state)
                        return 0; // one still on its way takes the vblank
        latest = vblank_last + (t - vblank_last) / vblank_refresh * vblank_refresh;
        if (latest == vblank_last)
                return 0; // the newest vblank showed a frame
        return (double)(t - latest) < window * (double)vblank_refresh;
}

static int compare(const void *a, const void *b)
{
        uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;

        return x < y ? -1 : x > y;
}

static int compare_signed(const void *a, const void *b)
{
        int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;

        return x < y ? -1 : x > y;
}

int main(int argc, char **argv)
{
        struct wl_registry *registry;
        struct xdg_surface *xdg;
        struct xdg_toplevel *toplevel;
        double fps = 0, window = 0.25;
        long cost_us = 0, seconds = 10;
        int option, hint_all = 0, hint_late = 0, quiet = 0, render, windowed = 0;
        uint64_t start, end, next_at = 0, second_at;
        unsigned int n = 0, blocked = 0, last_second_presented = 0;

        while ((option = getopt(argc, argv, "r:c:alW:w:s:q")) != -1)
                switch (option)
                {
                case 'r': fps = atof(optarg); break;
                case 'c': cost_us = atol(optarg); break;
                case 'a': hint_all = 1; break;
                case 'l': hint_late = 1; break;
                case 'W': window = atof(optarg); break;
                case 'w': windowed = sscanf(optarg, "%dx%d", &width, &height) == 2 && width > 0 && height > 0; break;
                case 's': seconds = atol(optarg); break;
                case 'q': quiet = 1; break;
                default: fprintf(stderr, "present_probe [-r FPS] [-c US] [-a | -l] [-W FRACTION] [-w WIDTHxHEIGHT] [-s SECONDS] [-q]\n"); return 2;
                }
        if (!(display = wl_display_connect(0)))
                return fprintf(stderr, "present_probe: no Wayland display\n"), 2;
        registry = wl_display_get_registry(display);
        wl_registry_add_listener(registry, &registry_listener, 0);
        wl_display_roundtrip(display);
        if (!compositor || !wm_base || !dmabuf || !presentation)
                return fprintf(stderr, "present_probe: the compositor lacks wl_compositor, xdg_wm_base, linux-dmabuf or wp_presentation\n"), 2;
        if ((hint_all || hint_late) && !tearing_manager)
                return fprintf(stderr, "present_probe: the compositor has no wp_tearing_control_manager_v1\n"), 2;
        render = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
        if (render < 0 || !(gbm = gbm_create_device(render)))
                return fprintf(stderr, "present_probe: no render node\n"), 2;
        surface = wl_compositor_create_surface(compositor);
        if (tearing_manager)
                tearing = wp_tearing_control_manager_v1_get_tearing_control(tearing_manager, surface);
        xdg = xdg_wm_base_get_xdg_surface(wm_base, surface);
        xdg_surface_add_listener(xdg, &surface_listener, 0);
        toplevel = xdg_surface_get_toplevel(xdg);
        xdg_toplevel_add_listener(toplevel, &toplevel_listener, 0);
        xdg_toplevel_set_title(toplevel, "present probe");
        xdg_toplevel_set_app_id(toplevel, "present-probe");
        if (!windowed)
                xdg_toplevel_set_fullscreen(toplevel, 0);
        wl_surface_commit(surface);
        {
                int asked_w = width, asked_h = height;

                while (!configured && wl_display_dispatch(display) >= 0)
                        ;
                if (windowed)
                        width = asked_w, height = asked_h; // the size asked, whatever the configure suggested
        }
        if (width <= 0 || height <= 0)
                width = 1920, height = 1080;
        for (int at = 0; at < BUFFERS; at++)
                if (!buffer_make(&buffers[at]))
                        return fprintf(stderr, "present_probe: gbm could not make a linear %dx%d buffer\n", width, height), 2;
        // A few frames to settle into fullscreen (and the compositor onto the direct path) before the measured run.
        start = now_ns(), end = start + (uint64_t)seconds * 1000000000ull + 500000000ull, second_at = start + 1500000000ull;
        while (!closed && now_ns() < end)
        {
                uint64_t t = now_ns();
                struct buffer *b = 0;
                int ready = fps > 0 ? (next_at == 0 || t >= next_at) : callback_due;

                if (ready)
                {
                        for (int at = 0; at < BUFFERS && !b; at++)
                                if (!buffers[at].busy)
                                        b = &buffers[at];
                        if (!b)
                                blocked++;
                }
                if (ready && b)
                {
                        uint64_t began = now_ns();
                        struct wp_presentation_feedback *feedback;
                        int hint;

                        if (cost_us > 0)
                                spin_until(began + (uint64_t)cost_us * 1000ull);
                        draw(b, n++);
                        t = now_ns();
                        hint = hint_all || (hint_late && frame_late(t, window));
                        if (tearing && hint != hint_now)
                        {
                                wp_tearing_control_v1_set_presentation_hint(tearing, hint ? WP_TEARING_CONTROL_V1_PRESENTATION_HINT_ASYNC
                                                                                          : WP_TEARING_CONTROL_V1_PRESENTATION_HINT_VSYNC);
                                hint_now = hint;
                        }
                        if (frames_count == frames_room)
                        {
                                frames_room = frames_room ? frames_room * 2 : 4096;
                                frames = realloc(frames, frames_room * sizeof(*frames));
                                if (!frames)
                                        return fprintf(stderr, "present_probe: out of memory\n"), 2;
                        }
                        memset(&frames[frames_count], 0, sizeof(*frames));
                        frames[frames_count].committed = t, frames[frames_count].torn_hint = hint;
                        frames[frames_count].callback = fps <= 0 ? callback_at : 0;
                        feedback = wp_presentation_feedback(presentation, surface);
                        wp_presentation_feedback_add_listener(feedback, &feedback_listener, (void *)(uintptr_t)frames_count);
                        frames_count++;
                        wl_surface_attach(surface, b->buffer, 0, 0);
                        wl_surface_damage_buffer(surface, 0, 0, width, height);
                        if (fps <= 0)
                        {
                                wl_callback_add_listener(wl_surface_frame(surface), &frame_listener, 0);
                                callback_due = 0;
                        }
                        b->busy = 1;
                        wl_surface_commit(surface);
                        wl_display_flush(display);
                        if (fps > 0)
                                next_at = (next_at ? next_at : began) + (uint64_t)(1e9 / fps);
                }
                {
                        struct pollfd pfd = {wl_display_get_fd(display), POLLIN, 0};
                        int timeout = 1;

                        if (fps > 0 && next_at > now_ns())
                                timeout = (int)((next_at - now_ns()) / 1000000ull);
                        else if (fps <= 0 && !callback_due)
                                timeout = 5;
                        else if (fps > 0)
                                timeout = 0;
                        while (wl_display_prepare_read(display) != 0)
                                wl_display_dispatch_pending(display);
                        wl_display_flush(display);
                        if (poll(&pfd, 1, timeout) > 0)
                                wl_display_read_events(display);
                        else
                                wl_display_cancel_read(display);
                        wl_display_dispatch_pending(display);
                        if (fps > 0 && next_at > now_ns() && next_at - now_ns() < 1000000ull)
                                spin_until(next_at);
                }
                if (!quiet && now_ns() >= second_at)
                {
                        unsigned int shown = 0;

                        for (size_t at = 0; at < frames_count; at++)
                                shown += frames[at].state == 1;
                        fprintf(stderr, "present_probe: %u presented this second\n", shown - last_second_presented);
                        last_second_presented = shown, second_at += 1000000000ull;
                }
        }
        // The feedbacks still to come.
        for (int spins = 0; spins < 20; spins++)
                wl_display_roundtrip(display);
        {
                uint64_t *latency = calloc(frames_count + 1, sizeof(uint64_t)), *to_commit = calloc(frames_count + 1, sizeof(uint64_t)),
                         *to_shown = calloc(frames_count + 1, sizeof(uint64_t));
                int64_t *telling = calloc(frames_count + 1, sizeof(int64_t));
                size_t callbacks = 0, hw_clock = 0, hw_done = 0, prev_seq_valid = 0;
                uint64_t prev_seq = 0;
                size_t shown = 0, discarded = 0, pending = 0, vsync = 0, zero = 0, torn = 0, hinted = 0, held[4] = {0}, tear_band[4] = {0};
                uint64_t prev = 0, refresh = vblank_refresh ? vblank_refresh : 6944444ull;
                size_t skip = 0;

                // The first half second is the settle: fullscreen taken, the compositor onto its path.
                while (skip < frames_count && frames[skip].committed < start + 500000000ull)
                        skip++;
                for (size_t at = skip; at < frames_count; at++)
                {
                        struct frame *f = &frames[at];

                        hinted += f->torn_hint != 0;
                        if (f->state == 2)
                        {
                                discarded++;
                                continue;
                        }
                        if (f->state != 1)
                        {
                                pending++;
                                continue;
                        }
                        telling[shown] = (int64_t)(f->told - f->presented); // negative: told before the stamp (a vblank's stamp is its end, the first line's scan)
                        latency[shown++] = f->presented > f->committed ? f->presented - f->committed : 0;
                        if (f->callback && f->callback <= f->committed)
                        {
                                to_commit[callbacks] = f->committed - f->callback;
                                to_shown[callbacks++] = f->presented > f->callback ? f->presented - f->callback : 0;
                        }
                        vsync += (f->flags & WP_PRESENTATION_FEEDBACK_KIND_VSYNC) != 0;
                        hw_clock += (f->flags & WP_PRESENTATION_FEEDBACK_KIND_HW_CLOCK) != 0;
                        hw_done += (f->flags & WP_PRESENTATION_FEEDBACK_KIND_HW_COMPLETION) != 0;
                        zero += (f->flags & WP_PRESENTATION_FEEDBACK_KIND_ZERO_COPY) != 0;
                        if (!(f->flags & WP_PRESENTATION_FEEDBACK_KIND_VSYNC))
                        {
                                torn++;
                                if (vblank_last && vblank_refresh)
                                {
                                        uint64_t base = f->presented >= vblank_last ? vblank_last + (f->presented - vblank_last) / vblank_refresh * vblank_refresh
                                                                                   : vblank_last - ((vblank_last - f->presented + vblank_refresh - 1) / vblank_refresh) * vblank_refresh;
                                        uint64_t into = f->presented - base;
                                        tear_band[into * 4 / vblank_refresh < 4 ? into * 4 / vblank_refresh : 3]++;
                                }
                        }
                        // Held: by the sequence where both are vsync'd flips with one (exact), else by the stamps over the refresh.
                        if (prev && f->presented > prev)
                        {
                                uint64_t k = prev_seq_valid && f->seq && (f->flags & WP_PRESENTATION_FEEDBACK_KIND_VSYNC) && f->seq > prev_seq
                                                 ? f->seq - prev_seq
                                                 : (f->presented - prev + refresh / 2) / refresh;
                                held[k < 1 ? 0 : k > 3 ? 3 : k]++;
                        }
                        prev = f->presented;
                        prev_seq = f->seq, prev_seq_valid = f->seq && (f->flags & WP_PRESENTATION_FEEDBACK_KIND_VSYNC);
                }
                qsort(latency, shown, sizeof(uint64_t), compare);
                qsort(telling, shown, sizeof(int64_t), compare_signed);
                qsort(to_commit, callbacks, sizeof(uint64_t), compare);
                qsort(to_shown, callbacks, sizeof(uint64_t), compare);
                printf("frames %zu committed (after the 0.5 s settle), %zu presented, %zu discarded, %zu never answered, %u times no free buffer\n",
                       frames_count - skip, shown, discarded, pending, blocked);
                printf("flags %zu vsync, %zu hw_clock, %zu hw_completion, %zu zero_copy, %zu torn (no vsync); %zu commits carried the async hint; "
                       "refresh %.3f ms; %s\n", vsync, hw_clock, hw_done, zero, torn, hinted, (double)refresh / 1e6, windowed ? "a window" : "fullscreen");
                if (shown)
                        printf("commit to presented ms: p50 %.3f p90 %.3f p99 %.3f worst %.3f mean %.3f\n", latency[shown / 2] / 1e6,
                               latency[shown * 9 / 10] / 1e6, latency[shown * 99 / 100] / 1e6, latency[shown - 1] / 1e6,
                               ({
                                       double sum = 0;
                                       for (size_t at = 0; at < shown; at++)
                                               sum += (double)latency[at];
                                       sum / (double)shown / 1e6;
                               }));
                if (callbacks)
                        printf("callback to commit ms: p50 %.3f p90 %.3f p99 %.3f; callback to presented ms: p50 %.3f p90 %.3f p99 %.3f (%zu frames)\n",
                               to_commit[callbacks / 2] / 1e6, to_commit[callbacks * 9 / 10] / 1e6, to_commit[callbacks * 99 / 100] / 1e6,
                               to_shown[callbacks / 2] / 1e6, to_shown[callbacks * 9 / 10] / 1e6, to_shown[callbacks * 99 / 100] / 1e6, callbacks);
                if (shown)
                        printf("feedback's arrival less its stamp ms (negative: before the stamp, which is the vblank's end): p50 %.3f p90 %.3f p99 %.3f\n",
                               (double)telling[shown / 2] / 1e6, (double)telling[shown * 9 / 10] / 1e6, (double)telling[shown * 99 / 100] / 1e6);
                printf("shown for: 1 refresh %zu, 2 refreshes %zu, 3 or more %zu (under half a refresh apart: %zu)\n", held[1], held[2], held[3], held[0]);
                printf("tears in the period's quarters: %zu %zu %zu %zu\n", tear_band[0], tear_band[1], tear_band[2], tear_band[3]);
                free(latency), free(to_commit), free(to_shown), free(telling);
        }
        return 0;
}
