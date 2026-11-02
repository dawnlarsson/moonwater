/*
        A Wayland client that speaks the wire itself, with no libwayland: the
        end-to-end test of the Canvas shim. It connects on the descriptor
        WAYLAND_SOCKET names (3 when unset), binds the globals, makes a 320x240
        ARGB8888 window on a shared-memory pool, commits a known picture, and
        reads back the pointer motion and button the shim delivers for the
        window. It exits 0 when the window is shown and both events arrive with
        their coordinates, and 1 with a line on stderr otherwise.

        Built and run by test/run's hooks lane, which hands the shim the other
        end of a socketpair (CHECK_wayland_client in test/checks.c). Linux only:
        it uses memfd_create.

            cc -std=gnu11 -Wall -Wextra -Werror -D_GNU_SOURCE test/wayland_client.c -o wayland-client
*/
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#define WIDTH 320
#define HEIGHT 240
#define STRIDE (WIDTH * 4)
#define BYTES (STRIDE * HEIGHT)

#define EVENT_MAX 256
#define QUEUE 64

struct event
{
        unsigned long object, op;
        unsigned char body[EVENT_MAX];
        long n;
};

static int sock;
static unsigned char in[8192];
static long in_length;
static struct event queue[QUEUE];
static long queued;
static unsigned long output_width, output_height;

/*
        The pixel every picture carries: opaque, with three channels that vary
        across the window. The harness computes the same values to compare.
*/
static unsigned long pixel(long x, long y)
{
        return 0xff000000UL | ((unsigned long)((x * 7) & 0xff) << 16) | ((unsigned long)((y * 5) & 0xff) << 8) |
               (unsigned long)((x ^ y) & 0xff);
}

static void fail(const char *what)
{
        fprintf(stderr, "wayland client: %s\n", what);
        exit(1);
}

static unsigned long word(const unsigned char *at)
{
        return (unsigned long)at[0] | (unsigned long)at[1] << 8 | (unsigned long)at[2] << 16 |
               (unsigned long)at[3] << 24;
}

static void put(unsigned char *at, unsigned long v)
{
        at[0] = (unsigned char)v;
        at[1] = (unsigned char)(v >> 8);
        at[2] = (unsigned char)(v >> 16);
        at[3] = (unsigned char)(v >> 24);
}

/*
        A request being built: the object, the opcode, then each argument as a
        word, a string (length with the NUL, the bytes, padded), or nothing for a
        descriptor, which goes beside the bytes.
*/
struct request
{
        unsigned char b[1024];
        long n;
};

static void begin(struct request *r, unsigned long object, unsigned long op)
{
        put(r->b, object);
        put(r->b + 4, op);
        r->n = 8;
}

static void u(struct request *r, unsigned long v)
{
        put(r->b + r->n, v);
        r->n += 4;
}

static void s(struct request *r, const char *text)
{
        unsigned long count = (unsigned long)strlen(text) + 1;
        long padded = (long)((count + 3) & ~3UL);

        u(r, count);
        for (long i = 0; i < padded; i++)
                r->b[r->n + i] = (unsigned long)i < count ? (unsigned char)text[i] : 0;
        r->n += padded;
}

/*
        Sends one request; a descriptor, when given, rides with its bytes.
*/
static void send_request(struct request *r, int descriptor)
{
        struct iovec iov;
        struct msghdr msg;
        char control[CMSG_SPACE(sizeof(int))];
        struct cmsghdr *cmsg;
        long sent = 0;

        put(r->b + 4, word(r->b + 4) | ((unsigned long)r->n << 16));
        if (descriptor < 0)
        {
                while (sent < r->n)
                {
                        ssize_t wrote = send(sock, r->b + sent, (size_t)(r->n - sent), MSG_NOSIGNAL);

                        if (wrote < 0 && errno == EINTR)
                                continue;
                        if (wrote <= 0)
                                fail("send");
                        sent += wrote;
                }
                return;
        }
        iov.iov_base = r->b;
        iov.iov_len = (size_t)r->n;
        memset(&msg, 0, sizeof msg);
        memset(control, 0, sizeof control);
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = control;
        msg.msg_controllen = sizeof control;
        cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(cmsg), &descriptor, sizeof(int));
        if (sendmsg(sock, &msg, MSG_NOSIGNAL) != r->n)
                fail("sendmsg");
}

/*
        Reads from the socket until at least one whole event is in hand, and
        queues every whole event it finds. Events are queued in the order the
        shim sent them.
*/
static void read_more(void)
{
        ssize_t got;

        got = read(sock, in + in_length, sizeof in - (size_t)in_length);
        if (got < 0 && errno == EINTR)
                return;
        if (got <= 0)
                fail("connection closed before the event came");
        in_length += got;
        while (in_length >= 8)
        {
                unsigned long size = word(in + 4) >> 16;
                struct event *e;

                if (size < 8 || size > (unsigned long)in_length)
                        break;
                if (queued == QUEUE || size - 8 > EVENT_MAX)
                        fail("event queue full");
                e = &queue[queued++];
                e->object = word(in);
                e->op = word(in + 4) & 0xffff;
                e->n = (long)size - 8;
                memcpy(e->body, in + 8, (size_t)e->n);
                if (e->object == 1 && e->op == 0)
                {
                        fprintf(stderr, "wayland client: the shim refused object %lu with code %lu\n",
                                word(e->body), word(e->body + 4));
                        exit(1);
                }
                memmove(in, in + size, (size_t)(in_length - (long)size));
                in_length -= (long)size;
        }
}

/*
        Waits for an event of an object and opcode, in order, and takes it off
        the queue. Events of other kinds stay queued for a later wait.
*/
static long queued_count(unsigned long object, unsigned long op)
{
        long n = 0;

        for (long i = 0; i < queued; i++)
                if (queue[i].object == object && queue[i].op == op)
                        n++;
        return n;
}

static struct event *wait_event(unsigned long object, unsigned long op)
{
        for (;;)
        {
                for (long i = 0; i < queued; i++)
                        if (queue[i].object == object && queue[i].op == op)
                        {
                                static struct event taken;

                                taken = queue[i];
                                for (long j = i; j + 1 < queued; j++)
                                        queue[j] = queue[j + 1];
                                queued--;
                                return &taken;
                        }
                read_more();
        }
}

/*
        Reads a whole string out of an event body at an offset, and returns the
        offset past it, padded.
*/
static long text_of(const struct event *e, long at, char *into, long room)
{
        unsigned long count = word(e->body + at);
        long padded = (long)((count + 3) & ~3UL);

        if (count == 0 || count > (unsigned long)room || (long)count > e->n - at - 4)
                fail("a string in an event is malformed");
        memcpy(into, e->body + at + 4, count);
        into[count - 1] = 0;
        return at + 4 + padded;
}

int main(void)
{
        const char *socket_number = getenv("WAYLAND_SOCKET");
        struct request r;
        struct event *e;
        char name[64];
        unsigned long serial;
        int shm_fd;
        unsigned char *pool;
        long at;
        int got_compositor = 0, got_shm = 0, got_seat = 0, got_output = 0, got_xdg = 0;
        unsigned long compositor = 3, shm = 4, xdg = 5, seat = 6, output = 7;
        unsigned long pointer = 14;

        sock = socket_number ? atoi(socket_number) : 3;

        /* the registry: every global is advertised with its name, interface and version. */
        begin(&r, 1, 1);
        u(&r, 2);
        send_request(&r, -1);
        for (int i = 0; i < 5; i++)
        {
                unsigned long global;

                e = wait_event(2, 0);
                global = word(e->body);
                at = text_of(e, 4, name, sizeof name);
                if (!strcmp(name, "wl_compositor")) got_compositor = (int)global;
                if (!strcmp(name, "wl_shm")) got_shm = (int)global;
                if (!strcmp(name, "wl_seat")) got_seat = (int)global;
                if (!strcmp(name, "wl_output")) got_output = (int)global;
                if (!strcmp(name, "xdg_wm_base")) got_xdg = (int)global;
                (void)at;
        }
        if (!got_compositor || !got_shm || !got_seat || !got_output || !got_xdg)
                fail("a global is missing from the registry");

        /* bind them: compositor, shm, xdg, seat and output, with the versions the shim offers. */
        begin(&r, 2, 0); u(&r, (unsigned long)got_compositor); s(&r, "wl_compositor"); u(&r, 4); u(&r, compositor); send_request(&r, -1);
        begin(&r, 2, 0); u(&r, (unsigned long)got_shm); s(&r, "wl_shm"); u(&r, 1); u(&r, shm); send_request(&r, -1);
        begin(&r, 2, 0); u(&r, (unsigned long)got_xdg); s(&r, "xdg_wm_base"); u(&r, 1); u(&r, xdg); send_request(&r, -1);
        begin(&r, 2, 0); u(&r, (unsigned long)got_seat); s(&r, "wl_seat"); u(&r, 5); u(&r, seat); send_request(&r, -1);
        begin(&r, 2, 0); u(&r, (unsigned long)got_output); s(&r, "wl_output"); u(&r, 3); u(&r, output); send_request(&r, -1);

        /* what the binds send back: two shm formats, the seat's capabilities, the output's geometry, mode,
           scale and done. The mode is sent once, and it is the screen the shim was told. */
        wait_event(shm, 0);
        wait_event(shm, 0);
        e = wait_event(seat, 0);
        if (word(e->body) == 0)
                fail("seat has no capabilities");
        wait_event(output, 0);
        e = wait_event(output, 1);
        output_width = word(e->body + 4);
        output_height = word(e->body + 8);
        if (output_width != 1024 || output_height != 768)
                fail("the output's mode is not the screen Canvas gave");
        wait_event(output, 3);
        wait_event(output, 2);
        if (queued_count(output, 1) != 0)
                fail("the output sent its mode more than once");

        /* the pointer, then the surface, its xdg surface and its toplevel; the toplevel configures with a size. */
        begin(&r, seat, 0); u(&r, pointer); send_request(&r, -1);
        begin(&r, compositor, 0); u(&r, 9); send_request(&r, -1);
        begin(&r, xdg, 2); u(&r, 10); u(&r, 9); send_request(&r, -1);
        begin(&r, 10, 1); u(&r, 11); send_request(&r, -1);
        e = wait_event(11, 0);
        if (word(e->body) != output_width || word(e->body + 4) != output_height)
                fail("the window's configure size is not the output's mode");
        e = wait_event(10, 0);
        serial = word(e->body);
        begin(&r, 10, 4); u(&r, serial); send_request(&r, -1);
        begin(&r, 11, 2); s(&r, "Moonwater wire test"); send_request(&r, -1);
        begin(&r, 11, 3); s(&r, "org.moonwater.test"); send_request(&r, -1);

        /* a sync round trip, so the binds and the window requests are answered before the picture. */
        begin(&r, 1, 0); u(&r, 8); send_request(&r, -1);
        wait_event(8, 0);
        wait_event(1, 1);

        /* a memfd of the picture, written before the pool sees it. */
        shm_fd = memfd_create("wayland-client", 0);
        if (shm_fd < 0)
                fail("memfd_create");
        if (ftruncate(shm_fd, BYTES) != 0)
                fail("ftruncate");
        pool = mmap(0, BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
        if (pool == MAP_FAILED)
                fail("mmap");
        for (long y = 0; y < HEIGHT; y++)
                for (long x = 0; x < WIDTH; x++)
                        put(pool + y * STRIDE + x * 4, pixel(x, y));

        begin(&r, shm, 0); u(&r, 12); u(&r, BYTES); send_request(&r, shm_fd);
        begin(&r, 12, 0); u(&r, 13); u(&r, 0); u(&r, WIDTH); u(&r, HEIGHT); u(&r, STRIDE); u(&r, 0); send_request(&r, -1);

        /* the commit: attach, damage, a frame callback, commit. The buffer comes back released, and the callback done. */
        begin(&r, 9, 1); u(&r, 13); u(&r, 0); u(&r, 0); send_request(&r, -1);
        begin(&r, 9, 2); u(&r, 0); u(&r, 0); u(&r, WIDTH); u(&r, HEIGHT); send_request(&r, -1);
        begin(&r, 9, 3); u(&r, 15); send_request(&r, -1);
        begin(&r, 9, 6); send_request(&r, -1);
        wait_event(13, 0);
        wait_event(15, 0);

        /* the pointer: the shim's enter when the window takes the focus, then a motion at 100,50 and a button. */
        e = wait_event(pointer, 0);
        if (word(e->body + 4) != 9)
                fail("the pointer's enter names another surface");
        e = wait_event(pointer, 2);
        if (word(e->body + 4) != 100 * 256 || word(e->body + 8) != 50 * 256)
                fail("pointer motion has the wrong place");
        e = wait_event(pointer, 3);
        if (word(e->body + 8) != 0x110 || word(e->body + 12) != 1)
                fail("pointer button has the wrong button or state");
        wait_event(pointer, 5);

        /* a last sync: every event before it has arrived, and no protocol error has (read_more exits on one). */
        begin(&r, 1, 0); u(&r, 20); send_request(&r, -1);
        wait_event(20, 0);
        wait_event(1, 1);

        (void)shm_fd;
        printf("wayland client: window shown, pointer delivered\n");
        return 0;
}
