/*
        test/perf/pacing.c: test/perf/pacing.py as one static program, for machines with no python (the Intel boxes): a client of the
        wire with a window that draws every frame and reads back what the compositor says of each (wp_presentation's presented or
        discarded, the frame callback's done), the way Mesa's Vulkan WSI paces a swapchain.

            cc -O2 -static -o pacing test/perf/pacing.c -lm
            ./pacing MODE [SECONDS] [BUFFERS]

        MODE: fifo (wp_fifo_v1: every commit waits for the last barrier and sets its own, drawn when a buffer is free), frame (drawn on
        the frame callback), mailbox (drawn when a buffer is free, no barrier) or timed (wp_commit_timing_v1: each commit for the refresh
        after the last presented, with FIFO). The rows it says are pacing.py's.
*/
#define _GNU_SOURCE
#include <math.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define W 256
#define H 256
#define MOST 20000

static int wire;
static unsigned char in[1 << 16];
static size_t in_len;
static uint32_t next_id = 100;

struct commit
{
        uint64_t sent, when, got, callback, seq;
        uint32_t refresh, flags, fb, cb;
        int presented, discarded;
};
static struct commit commits[MOST];
static int count;

static uint64_t now_ns(void)
{
        struct timespec t;

        clock_gettime(CLOCK_MONOTONIC, &t);
        return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

static unsigned char out[4096];
static size_t out_len;

static void put(uint32_t obj, uint32_t op, int words, const uint32_t *w, const char *text)
{
        size_t tl = text ? strlen(text) + 1 : 0, tpad = (tl + 3) & ~3ul;
        uint32_t size = 8 + (text ? 4 + (uint32_t)tpad : 0) + 4 * (uint32_t)words;

        memcpy(out + out_len, &obj, 4);
        size_t head = out_len + 4;
        out_len += 8;
        if (text)
        {
                uint32_t l = (uint32_t)tl;
                memcpy(out + out_len, &l, 4);
                memset(out + out_len + 4, 0, tpad);
                memcpy(out + out_len + 4, text, tl);
                out_len += 4 + tpad;
        }
        memcpy(out + out_len, w, 4 * (size_t)words);
        out_len += 4 * (size_t)words;
        uint32_t h = (size << 16) | op;
        memcpy(out + head, &h, 4);
}

static void flush_out(void)
{
        if (out_len && write(wire, out, out_len) != (ssize_t)out_len)
                exit(2);
        out_len = 0;
}

#define MSG(obj, op, ...)                                                                    \
        do                                                                                   \
        {                                                                                    \
                uint32_t w_[] = {0, ##__VA_ARGS__};                                          \
                put(obj, op, (int)(sizeof w_ / 4) - 1, w_ + 1, 0);                           \
        } while (0)

struct event
{
        uint32_t obj, op, len;
        const unsigned char *body;
        uint64_t at;
};

// Reads what came within timeout ms and calls each(event) for every whole event.
static void pump(int timeout, void (*each)(struct event *))
{
        struct pollfd p = {wire, POLLIN, 0};

        if (poll(&p, 1, timeout) > 0)
        {
                ssize_t got = read(wire, in + in_len, sizeof in - in_len);
                if (got <= 0)
                {
                        fprintf(stderr, "the compositor ended the connection\n");
                        exit(1);
                }
                in_len += (size_t)got;
        }
        uint64_t at = now_ns();
        size_t pos = 0;
        while (pos + 8 <= in_len)
        {
                uint32_t obj, head;
                memcpy(&obj, in + pos, 4);
                memcpy(&head, in + pos + 4, 4);
                if (pos + (head >> 16) > in_len)
                        break;
                struct event e = {obj, head & 0xffff, (head >> 16) - 8, in + pos + 8, at};
                each(&e);
                pos += head >> 16;
        }
        memmove(in, in + pos, in_len - pos);
        in_len -= pos;
}

static uint32_t names[8];
static const char *wanted[] = {"wl_compositor", "wl_shm", "xdg_wm_base", "wp_presentation", "wp_fifo_manager_v1", "wp_commit_timing_manager_v1"};
static int synced, serial_got;
static uint32_t serial;

static void on_global(struct event *e)
{
        if (e->obj == 2 && e->op == 0)
        {
                uint32_t name, len;
                memcpy(&name, e->body, 4);
                memcpy(&len, e->body + 4, 4);
                for (int at = 0; at < 6; at++)
                        if (!strcmp((const char *)e->body + 8, wanted[at]))
                                names[at] = name;
        }
        if (e->obj == 3 && e->op == 0)
                synced = 1;
}

enum
{
        COMP = 4,
        SHM,
        WM,
        PRES,
        FIFOM,
        TIMERM,
        SURF,
        XSURF,
        TOP,
        FIFO,
        TIMER,
        POOL = 20,
        BUF0 = 30
};

static void on_configure(struct event *e)
{
        if (e->obj == XSURF && e->op == 0)
                memcpy(&serial, e->body, 4), serial_got = 1;
        if (e->obj == WM && e->op == 0)
        {
                uint32_t s;
                memcpy(&s, e->body, 4);
                MSG(WM, 3, s);
        }
}

static int buffers = 3, freebuf[16], frame_ready = 1, mode_fifo, mode_frame, mode_timed;
static uint64_t last_when, last_refresh;

static void on_event(struct event *e)
{
        if (e->obj == 1 && e->op == 0 && e->len >= 12)
        {
                uint32_t w[2];
                memcpy(w, e->body, 8);
                fprintf(stderr, "protocol error: object %u code %u: %s\n", w[0], w[1], (const char *)e->body + 12);
        }
        if (e->obj >= BUF0 && e->obj < BUF0 + (uint32_t)buffers && e->op == 0)
                freebuf[e->obj - BUF0] = 1;
        else if (e->obj == WM && e->op == 0)
                on_configure(e);
        else if (e->obj > 100)
                for (int at = count - 1; at >= 0 && at >= count - 256; at--)
                {
                        struct commit *c = &commits[at];
                        if (e->obj == c->cb && e->op == 0)
                        {
                                c->callback = e->at, frame_ready = 1;
                                break;
                        }
                        if (e->obj == c->fb)
                        {
                                if (e->op == 1)
                                {
                                        uint32_t w[7];
                                        memcpy(w, e->body, 28);
                                        c->when = ((uint64_t)w[0] << 32 | w[1]) * 1000000000ull + w[2];
                                        c->refresh = w[3], c->seq = (uint64_t)w[4] << 32 | w[5], c->flags = w[6];
                                        c->presented = 1, c->got = e->at;
                                        last_when = c->when, last_refresh = c->refresh;
                                }
                                else if (e->op == 2)
                                        c->discarded = 1;
                                break;
                        }
                }
}

static int pool_fd;

static void draw(void)
{
        int b = 0;
        while (!freebuf[b])
                b++;
        freebuf[b] = 0;
        int n = count, band = (n * 8) % H;
        uint32_t px = 0xff000000u | (uint32_t)(n & 255) << 16 | (uint32_t)((n * 7) & 255) << 8 | 0x80;
        static uint32_t row[W * 8];
        for (int at = 0; at < W * 8; at++)
                row[at] = px;
        pwrite(pool_fd, row, sizeof row, (off_t)b * W * H * 4 + (off_t)band * W * 4);
        struct commit *c = &commits[count];
        memset(c, 0, sizeof *c);
        c->fb = ++next_id, c->cb = ++next_id;
        MSG(SURF, 1, BUF0 + b, 0, 0);
        MSG(SURF, 9, 0, band, W, 8);
        MSG(SURF, 3, c->cb);
        MSG(PRES, 1, SURF, c->fb);
        if (names[4] && (mode_fifo || mode_timed))
        {
                MSG(FIFO, 1);
                MSG(FIFO, 0);
        }
        if (names[5] && mode_timed && last_when)
        {
                int owed = 0;
                for (int at = count - 1; at >= 0 && at >= count - 8; at--)
                        owed += !commits[at].presented && !commits[at].discarded;
                uint64_t target = last_when + last_refresh * (uint64_t)(1 + owed);
                MSG(TIMER, 0, (uint32_t)((target / 1000000000ull) >> 32), (uint32_t)(target / 1000000000ull), (uint32_t)(target % 1000000000ull));
        }
        MSG(SURF, 6);
        c->sent = now_ns();
        flush_out();
        count++;
        frame_ready = 0;
}

static int cmp_u64(const void *a, const void *b)
{
        uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
        return x < y ? -1 : x > y;
}

int main(int argc, char **argv)
{
        const char *mode = argc > 1 ? argv[1] : "fifo";
        double seconds = argc > 2 ? atof(argv[2]) : 5.0;
        struct sockaddr_un addr = {.sun_family = AF_UNIX};
        const char *path = getenv("WAYLAND_DISPLAY") ? getenv("WAYLAND_DISPLAY") : "/run/moonwater/wayland-0";

        buffers = argc > 3 ? atoi(argv[3]) : 3;
        if (buffers < 1 || buffers > 16)
                buffers = 3;
        mode_fifo = !strcmp(mode, "fifo"), mode_frame = !strcmp(mode, "frame"), mode_timed = !strcmp(mode, "timed");
        wire = socket(AF_UNIX, SOCK_STREAM, 0);
        snprintf(addr.sun_path, sizeof addr.sun_path, "%s", path);
        if (connect(wire, (struct sockaddr *)&addr, sizeof addr) < 0)
                return perror("connect"), 1;
        MSG(1, 1, 2);
        MSG(1, 0, 3);
        flush_out();
        while (!synced)
                pump(2000, on_global);
        for (int at = 0; at < 4; at++)
                if (!names[at])
                        return fprintf(stderr, "no %s\n", wanted[at]), 1;
        static const uint32_t ids[] = {COMP, SHM, WM, PRES, FIFOM, TIMERM}, versions[] = {4, 1, 1, 1, 1, 1};
        for (int at = 0; at < 6; at++)
                if (names[at])
                {
                        uint32_t name = names[at];
                        // bind: name, then the interface string, the version and the new id
                        {
                                unsigned char *p = out + out_len;
                                size_t tl = strlen(wanted[at]) + 1, tpad = (tl + 3) & ~3ul;
                                uint32_t size = (uint32_t)(8 + 4 + 4 + tpad + 8), obj = 2, h = size << 16 | 0, l = (uint32_t)tl;
                                memcpy(p, &obj, 4), memcpy(p + 4, &h, 4), memcpy(p + 8, &name, 4), memcpy(p + 12, &l, 4);
                                memset(p + 16, 0, tpad), memcpy(p + 16, wanted[at], tl);
                                memcpy(p + 16 + tpad, &versions[at], 4), memcpy(p + 20 + tpad, &ids[at], 4);
                                out_len += size;
                        }
                }
        MSG(COMP, 0, SURF);
        MSG(WM, 2, XSURF, SURF);
        MSG(XSURF, 1, TOP);
        put(TOP, 2, 0, 0, "pacing");
        if (names[4])
                MSG(FIFOM, 1, FIFO, SURF);
        if (names[5])
                MSG(TIMERM, 1, TIMER, SURF);
        MSG(SURF, 6);
        flush_out();
        while (!serial_got)
        {
                pump(2000, on_configure);
                flush_out();
        }
        MSG(XSURF, 4, serial);
        pool_fd = memfd_create("pacing", 0);
        if (ftruncate(pool_fd, (off_t)W * H * 4 * buffers) < 0)
                return 1;
        {
                struct msghdr m = {0};
                char control[CMSG_SPACE(sizeof(int))];
                flush_out();
                // create_pool: new id, fd (ancillary), size
                uint32_t body[4] = {SHM, (16u << 16) | 0, POOL, (uint32_t)(W * H * 4 * buffers)};
                struct iovec v = {body, 16};
                m.msg_iov = &v, m.msg_iovlen = 1, m.msg_control = control, m.msg_controllen = sizeof control;
                struct cmsghdr *cm = CMSG_FIRSTHDR(&m);
                cm->cmsg_level = SOL_SOCKET, cm->cmsg_type = SCM_RIGHTS, cm->cmsg_len = CMSG_LEN(sizeof(int));
                memcpy(CMSG_DATA(cm), &pool_fd, sizeof(int));
                if (sendmsg(wire, &m, 0) != 16)
                        return 1;
        }
        for (int at = 0; at < buffers; at++)
        {
                MSG(POOL, 0, BUF0 + at, (uint32_t)(at * W * H * 4), W, H, W * 4, 1);
                freebuf[at] = 1;
        }
        flush_out();
        uint64_t end = now_ns() + (uint64_t)(seconds * 1e9);
        while (now_ns() < end && count < MOST)
        {
                int any = 0;
                for (int at = 0; at < buffers; at++)
                        any |= freebuf[at];
                if (any && (!mode_frame || frame_ready))
                {
                        draw();
                        pump(0, on_event); // what came is read as it comes: a client that only writes is one the compositor stops for
                        // A mailbox client draws at most a frame a millisecond (a game at 1000 frames a second): unpaced, a client of
                        // shared memory sends thousands of commits in one read, and the shim's queue of their events (64 KiB) is full.
                        if (!mode_fifo && !mode_frame && !mode_timed)
                                usleep(1000);
                        continue;
                }
                pump(50, on_event);
                flush_out();
        }
        uint64_t stop = now_ns() + 300000000ull;
        while (now_ns() < stop)
                pump(50, on_event);

        int shown = 0, discarded = 0, unanswered = 0;
        static struct commit *s[MOST];
        for (int at = 0; at < count; at++)
        {
                if (commits[at].presented)
                        s[shown++] = &commits[at];
                discarded += commits[at].discarded;
                unanswered += !commits[at].presented && !commits[at].discarded;
        }
        printf("mode %s buffers %d fifo %d timer %d\n", mode, buffers, names[4] != 0, names[5] != 0);
        printf("commits %d presented %d discarded %d unanswered %d\n", count, shown, discarded, unanswered);
        if (shown < 3)
                return 0;
        int from = shown / 10, n = shown - from;
        struct commit **k = s + from, *first = k[0], *last = k[n - 1];
        double measured = (double)(last->when - first->when) / (double)(last->seq > first->seq ? last->seq - first->seq : 1);
        printf("refresh said %u measured %.0f (ns)\n", first->refresh, measured);
        double worst = 0;
        for (int at = 0; at < n; at++)
        {
                double d = fabs((double)(k[at]->when - first->when) - (double)(k[at]->seq - first->seq) * measured);
                worst = d > worst ? d : worst;
        }
        printf("seq_vs_time %.1f us\n", worst / 1000);
        int same = 0, early = 0, late = 0, hist[8] = {0};
        for (int at = 0; at < n; at++)
        {
                early += k[at]->when < k[at]->sent;
                if (at)
                {
                        uint64_t g = k[at]->seq - k[at - 1]->seq;
                        same += g == 0;
                        late += g >= 2;
                        hist[g < 7 ? g : 7]++;
                }
        }
        printf("same_seq %d\nearly %d\nlate_frames %d\n", same, early, late);
        double mean = 0, dev = 0;
        for (int at = 1; at < n; at++)
                mean += (double)(k[at]->when - k[at - 1]->when);
        mean /= n - 1;
        for (int at = 1; at < n; at++)
        {
                double d = (double)(k[at]->when - k[at - 1]->when) - mean;
                dev += d * d;
        }
        dev = sqrt(dev / (n - 1));
        printf("interval refreshes");
        for (int g = 0; g < 8; g++)
                if (hist[g])
                        printf(" %d:%d", g, hist[g]);
        printf(" mean %.1f us sd %.1f us\n", mean / 1000, dev / 1000);
        int flags[16] = {0};
        for (int at = 0; at < n; at++)
                flags[k[at]->flags & 15]++;
        printf("flags");
        for (int f = 0; f < 16; f++)
                if (flags[f])
                        printf(" 0x%x:%d", f, flags[f]);
        printf("\n");
        double lead = 0, least = 1e18;
        int leads = 0;
        for (int at = 0; at + 1 < n; at++)
                if (k[at]->callback)
                {
                        double l = (double)k[at + 1]->when - (double)k[at]->callback;
                        lead += l, leads++;
                        least = l < least ? l : least;
                }
        if (leads)
                printf("callback_lead mean %.0f us least %.0f us\n", lead / leads / 1000, least / 1000);
        static uint64_t lat[MOST];
        for (int at = 0; at < n; at++)
                lat[at] = k[at]->when - k[at]->sent;
        qsort(lat, (size_t)n, sizeof lat[0], cmp_u64);
        printf("latency median %.0f us p95 %.0f us\n", lat[n / 2] / 1000.0, lat[n * 95 / 100] / 1000.0);
        return 0;
}
