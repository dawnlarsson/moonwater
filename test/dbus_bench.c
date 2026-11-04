/*
        The session bus's bench: a client of its own wire against a sessionbus that runs (a private /run and /root, as
        test/dbus_e2e.sh makes them), measuring what a desktop's programs feel.

            cc -O2 -o dbus-bench test/dbus_bench.c
            unshare -rm sh -c 'mount -t tmpfs tmpfs /run; mount -t tmpfs tmpfs /root; mkdir -p /run/moonwater;
                echo dark > /root/canvas.theme; SHELL/sessionbus & sleep 0.5; ./dbus-bench /run/moonwater/bus /root'

        Rows, each the median and the 99th percentile of its samples, in microseconds:
          call       ReadOne of the portal, one at a time: the round trip of a call the bus answers itself
          pipelined  64 ReadOne written at once and their 64 answers read: messages a second
          echo       a call through the bus to another client that answers it, one at a time (two hops each way)
          fanout N   N clients listen for SettingChanged; /root/canvas.theme is written (dark, light, ...) and the time is
                     from the rename to the last of the N having its signal, for N of 1, 10 and 100
*/
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

static const char *bench_path, *bench_root;
static unsigned bench_serial;

static long bench_ns(void)
{
        struct timespec t;

        clock_gettime(CLOCK_MONOTONIC, &t);
        return t.tv_sec * 1000000000L + t.tv_nsec;
}

struct bench_buffer
{
        unsigned char b[4096];
        long n;
};

static void bench_pad(struct bench_buffer *w, long align)
{
        while (w->n % align)
                w->b[w->n++] = 0;
}

static void bench_u32(struct bench_buffer *w, unsigned v)
{
        bench_pad(w, 4);
        memcpy(w->b + w->n, &v, 4);
        w->n += 4;
}

static void bench_string(struct bench_buffer *w, const char *s)
{
        unsigned n = (unsigned)strlen(s);

        bench_u32(w, n);
        memcpy(w->b + w->n, s, n + 1);
        w->n += n + 1;
}

static void bench_sig(struct bench_buffer *w, const char *s)
{
        unsigned char n = (unsigned char)strlen(s);

        w->b[w->n++] = n;
        memcpy(w->b + w->n, s, (size_t)n + 1);
        w->n += n + 1;
}

static void bench_field(struct bench_buffer *w, unsigned char code, char type, const char *value)
{
        char t[2] = {type, 0};

        bench_pad(w, 8);
        w->b[w->n++] = code;
        bench_sig(w, t);
        if (type == 'g')
                bench_sig(w, value);
        else
                bench_string(w, value);
}

// A message: type, flags, fields (path, interface, member, destination, signature), a body of up to two strings, or a reply to serial.
static long bench_message(unsigned char *into, unsigned char type, unsigned char flags, const char *path, const char *interface, const char *member,
                          const char *destination, const char *a, const char *b, unsigned reply)
{
        struct bench_buffer w = {.n = 0};
        long body;

        w.b[0] = 'l', w.b[1] = type, w.b[2] = flags, w.b[3] = 1;
        w.n = 4;
        bench_u32(&w, 0);
        bench_u32(&w, ++bench_serial);
        bench_u32(&w, 0);
        if (path)
                bench_field(&w, 1, 'o', path);
        if (interface)
                bench_field(&w, 2, 's', interface);
        if (member)
                bench_field(&w, 3, 's', member);
        if (reply)
        {
                bench_pad(&w, 8);
                w.b[w.n++] = 5;
                bench_sig(&w, "u");
                bench_u32(&w, reply);
        }
        if (destination)
                bench_field(&w, 6, 's', destination);
        if (a)
                bench_field(&w, 8, 'g', b ? "ss" : "s");
        {
                unsigned fields = (unsigned)(w.n - 16);

                memcpy(w.b + 12, &fields, 4);
        }
        bench_pad(&w, 8);
        body = w.n;
        if (a)
                bench_string(&w, a);
        if (b)
                bench_string(&w, b);
        {
                unsigned length = (unsigned)(w.n - body);

                memcpy(w.b + 4, &length, 4);
        }
        memcpy(into, w.b, (size_t)w.n);
        return w.n;
}

struct bench_client
{
        int fd;
        unsigned char in[1 << 20];
        long have, taken; // bytes read, and of them the message bench_next gave last
        char unique[32];
};

static void bench_write(struct bench_client *c, const void *bytes, long n)
{
        const unsigned char *p = bytes;

        while (n > 0)
        {
                long got = write(c->fd, p, (size_t)n);

                if (got < 0 && errno == EINTR)
                        continue;
                if (got <= 0)
                {
                        perror("write");
                        exit(1);
                }
                p += got;
                n -= got;
        }
}

// The next whole message into m (its bytes stay in c->in until the next call), its length; 0 when none comes in `wait_ms`.
static long bench_next(struct bench_client *c, const unsigned char **m, int wait_ms)
{
        if (c->taken)
        {
                memmove(c->in, c->in + c->taken, (size_t)(c->have - c->taken));
                c->have -= c->taken;
                c->taken = 0;
        }
        for (;;)
        {
                if (c->have >= 16)
                {
                        unsigned fields, body;
                        long total;

                        memcpy(&body, c->in + 4, 4);
                        memcpy(&fields, c->in + 12, 4);
                        total = ((16 + (long)fields + 7) & ~7L) + body;
                        if (c->have >= total)
                        {
                                *m = c->in;
                                c->taken = total;
                                return total;
                        }
                }
                {
                        struct pollfd p = {c->fd, POLLIN, 0};
                        long got;

                        if (poll(&p, 1, wait_ms) <= 0)
                                return 0;
                        got = read(c->fd, c->in + c->have, sizeof c->in - (size_t)c->have);
                        if (got <= 0)
                        {
                                fprintf(stderr, "the bus closed a connection\n");
                                exit(1);
                        }
                        c->have += got;
                }
        }
}

static struct bench_client *bench_connect(void)
{
        struct bench_client *c = calloc(1, sizeof *c);
        struct sockaddr_un a = {.sun_family = AF_UNIX};
        char line[256];
        unsigned char hello[512];
        const unsigned char *m;
        long n;

        snprintf(a.sun_path, sizeof a.sun_path, "%s", bench_path);
        c->fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (connect(c->fd, (struct sockaddr *)&a, sizeof a) < 0)
        {
                perror("connect");
                exit(1);
        }
        bench_write(c, "\0AUTH EXTERNAL\r\nDATA\r\n", 22);
        n = read(c->fd, line, sizeof line - 1);
        line[n > 0 ? n : 0] = 0;
        if (!strstr(line, "OK "))
        {
                fprintf(stderr, "not authenticated: %s\n", line);
                exit(1);
        }
        bench_write(c, "BEGIN\r\n", 7);
        n = bench_message(hello, 1, 0, "/org/freedesktop/DBus", "org.freedesktop.DBus", "Hello", "org.freedesktop.DBus", 0, 0, 0);
        bench_write(c, hello, n);
        for (;;)
        {
                long size = bench_next(c, &m, 2000);

                if (!size)
                {
                        fprintf(stderr, "no Hello\n");
                        exit(1);
                }
                if (m[1] == 2)
                {
                        unsigned fields, length;

                        memcpy(&fields, m + 12, 4);
                        memcpy(&length, m + ((16 + fields + 7) & ~7u), 4);
                        memcpy(c->unique, m + ((16 + fields + 7) & ~7u) + 4, length + 1);
                        return c;
                }
        }
}

static int bench_compare(const void *a, const void *b)
{
        long x = *(const long *)a, y = *(const long *)b;

        return x < y ? -1 : x > y;
}

static void bench_row(const char *name, long *ns, long count)
{
        qsort(ns, (size_t)count, sizeof *ns, bench_compare);
        printf("%-12s median %8.1f us   p99 %8.1f us   (%ld samples)\n", name, ns[count / 2] / 1000.0, ns[count * 99 / 100] / 1000.0, count);
}

// Waits for the reply to the last call written: messages before it (signals) are skipped.
static void bench_reply(struct bench_client *c)
{
        const unsigned char *m;

        for (;;)
        {
                if (!bench_next(c, &m, 5000))
                {
                        fprintf(stderr, "no reply\n");
                        exit(1);
                }
                if (m[1] == 2 || m[1] == 3)
                        return;
        }
}

static void bench_theme(const char *word)
{
        char path[512], temporary[520];
        FILE *f;

        snprintf(path, sizeof path, "%s/canvas.theme", bench_root);
        snprintf(temporary, sizeof temporary, "%s/.canvas.theme", bench_root);
        f = fopen(temporary, "w");
        fprintf(f, "%s\n", word);
        fclose(f);
        rename(temporary, path);
}

int main(int argc, char **argv)
{
        static long ns[20000];
        unsigned char message[1024];
        long n, count;
        struct bench_client *a;

        if (argc < 3)
        {
                fprintf(stderr, "usage: dbus-bench SOCKET SETTINGS-DIRECTORY\n");
                return 2;
        }
        bench_path = argv[1];
        bench_root = argv[2];
        a = bench_connect();
        // the round trip of a call the bus answers
        for (count = 0; count < 20000; count++)
        {
                long t0 = bench_ns();

                n = bench_message(message, 1, 0, "/org/freedesktop/portal/desktop", "org.freedesktop.portal.Settings", "ReadOne", "org.freedesktop.portal.Desktop",
                                  "org.freedesktop.appearance", "color-scheme", 0);
                bench_write(a, message, n);
                bench_reply(a);
                ns[count] = bench_ns() - t0;
        }
        bench_row("call", ns, count);
        // pipelined: 64 at once
        {
                static unsigned char batch[64 * 1024];
                long size = 0, t0 = bench_ns(), turns = 400;

                for (int k = 0; k < 64; k++)
                        size += bench_message(batch + size, 1, 0, "/org/freedesktop/portal/desktop", "org.freedesktop.portal.Settings", "ReadOne",
                                              "org.freedesktop.portal.Desktop", "org.freedesktop.appearance", "color-scheme", 0);
                for (long turn = 0; turn < turns; turn++)
                {
                        bench_write(a, batch, size);
                        for (int k = 0; k < 64; k++)
                                bench_reply(a);
                }
                printf("%-12s %8.0f calls a second (%ld in %.3f s)\n", "pipelined", turns * 64 / ((bench_ns() - t0) / 1e9), turns * 64, (bench_ns() - t0) / 1e9);
        }
        // a call to another client through the bus, answered by it
        {
                struct bench_client *b = bench_connect();
                const unsigned char *m;

                // RequestName takes su: written here, the flags after the string
                {
                        struct bench_buffer w = {.n = 0};
                        long body;
                        unsigned fields, length;

                        w.b[0] = 'l', w.b[1] = 1, w.b[2] = 0, w.b[3] = 1;
                        w.n = 4;
                        bench_u32(&w, 0);
                        bench_u32(&w, ++bench_serial);
                        bench_u32(&w, 0);
                        bench_field(&w, 1, 'o', "/org/freedesktop/DBus");
                        bench_field(&w, 2, 's', "org.freedesktop.DBus");
                        bench_field(&w, 3, 's', "RequestName");
                        bench_field(&w, 6, 's', "org.freedesktop.DBus");
                        bench_field(&w, 8, 'g', "su");
                        fields = (unsigned)(w.n - 16);
                        memcpy(w.b + 12, &fields, 4);
                        bench_pad(&w, 8);
                        body = w.n;
                        bench_string(&w, "org.example.Echo");
                        bench_u32(&w, 4);
                        length = (unsigned)(w.n - body);
                        memcpy(w.b + 4, &length, 4);
                        bench_write(b, w.b, w.n);
                }
                bench_reply(b);
                for (count = 0; count < 10000; count++)
                {
                        long t0 = bench_ns();
                        unsigned serial;

                        n = bench_message(message, 1, 0, "/org/example", "org.example.Echo", "Echo", "org.example.Echo", "ping", 0, 0);
                        bench_write(a, message, n);
                        for (;;)
                        {
                                if (!bench_next(b, &m, 5000))
                                {
                                        fprintf(stderr, "the echo was not called\n");
                                        return 1;
                                }
                                if (m[1] == 1)
                                        break;
                        }
                        memcpy(&serial, m + 8, 4);
                        // the reply goes to the caller, whose unique name the bench knows
                        n = bench_message(message, 2, 1, 0, 0, 0, a->unique, "pong", 0, serial);
                        bench_write(b, message, n);
                        bench_reply(a);
                        ns[count] = bench_ns() - t0;
                }
                bench_row("echo", ns, count);
        }
        // SettingChanged to 1, 10 and 100 listeners
        {
                static const int sizes[] = {1, 10, 100};
                static struct bench_client *listener[100];
                int made = 0;

                for (int s = 0; s < 3; s++)
                {
                        const char *words[2] = {"light", "dark"};

                        while (made < sizes[s])
                        {
                                listener[made] = bench_connect();
                                n = bench_message(message, 1, 0, "/org/freedesktop/DBus", "org.freedesktop.DBus", "AddMatch", "org.freedesktop.DBus",
                                                  "type='signal',sender='org.freedesktop.portal.Desktop',interface='org.freedesktop.portal.Settings',member='SettingChanged'",
                                                  0, 0);
                                bench_write(listener[made], message, n);
                                bench_reply(listener[made]);
                                made++;
                        }
                        for (count = 0; count < 60; count++)
                        {
                                long t0;
                                int told = 0;
                                const unsigned char *left;

                                usleep(20000);
                                // what the last change sent after the signal waited for (the gnome keys) is read first
                                for (int k = 0; k < made; k++)
                                        while (bench_next(listener[k], &left, 0))
                                                ;
                                t0 = bench_ns();
                                bench_theme(words[count & 1]);
                                for (int k = 0; k < sizes[s]; k++)
                                {
                                        const unsigned char *m;

                                        for (;;)
                                        {
                                                if (!bench_next(listener[k], &m, 3000))
                                                {
                                                        fprintf(stderr, "listener %d was not told\n", k);
                                                        return 1;
                                                }
                                                if (m[1] == 4)
                                                        break;
                                        }
                                        told++;
                                }
                                ns[count] = bench_ns() - t0;
                                // the other signals of the change (the gnome keys) are left to be skipped next time
                                (void)told;
                        }
                        {
                                char name[16];

                                snprintf(name, sizeof name, "fanout %d", sizes[s]);
                                bench_row(name, ns, count);
                        }
                }
        }
        return 0;
}
