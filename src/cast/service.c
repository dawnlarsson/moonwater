/*
        The capture engine in the session bus's process: the host behind the ScreenCast portal (src/dbus/screencast.c) and
        the PipeWire that serves its streams (src/cast/pipewire.c), between them and the compositor's rings (src/cast/cast.h).

        A Start asks the person: a row of the table's casts is filled (WINDOWS_CAST_ASKED: the ticket, the program's pid, uid
        and name, what it may be offered) and the compositor's door rung, which says it to the machine script, which opens the
        share app; the row is looked at until the person answered there (PICKED, and LIVE when the compositor has the ring:
        Start's Response with the stream; REFUSED: the person said no). OpenPipeWireRemote is a socket pair: one end the
        program's, the other a connection of this process's PipeWire that may see that share's ring alone (opened read-only
        through the compositor's /proc, and only when it is sealed against every write but the compositor's own mapping).
        Each frame the compositor publishes rings WINDOWS_CAST_WAKE, a FIFO of this process's; the newest slot is taken and
        told to every connection of the share that is ready for one, and held while its consumer may read it; a frame that is
        not told for want of a ready consumer is told when it is ready (its eventfd, or the next turn of the loop). A share
        stopped (the tray's Stop, its window gone: STOPPED) ends its session (Closed to the program) and its connections; a
        session that ends lets its row go (FREE), which the compositor reads as the end of the ring.

        Included by src/dbus/service.c, which runs it in its loop (castbus_watch, castbus_event, castbus_tick, castbus_wait).
*/
#ifndef CASTBUS_INCLUDED
#define CASTBUS_INCLUDED

#include "cast.h"
#include "pipewire.c"

#define CASTBUS_PER 4         // PipeWire connections of one share (a program asks one, OBS's preview and its source two)
#define CASTBUS_POLLS (1 + 2 * CASTBUS_PER * WINDOWS_CASTS)
#define CASTBUS_LOOK_MS 50    // how often the table is looked at while the person picks
#define CASTBUS_AGAIN_MS 4    // and a frame that waits for its consumer tried again
#define CASTBUS_NODE 100      // a share's node is CASTBUS_NODE + 2 * its ticket
#define CASTBUS_OWNER (-1000) // the bus's loop tells the engine's descriptors by their owners, this and below (its own are above)

struct castbus_link
{
        long pw;                   // the pwire connection, -1 none
        int fd;                    // its socket
        unsigned long long holding; // the ring's slots it holds (cast_take)
        unsigned int told;         // the frame count of the ring when it was told last
};

struct castbus_share
{
        long ticket;               // 0: none
        int row;                   // its row of the table's casts
        int ring;                  // the ring's memfd, read-only (-1 until a remote asks)
        unsigned int state;        // the row's state looked at last
        struct castbus_link link[CASTBUS_PER];
};

static struct
{
        int wake;                  // WINDOWS_CAST_WAKE, read and write (so it never says the other end went)
        struct castbus_share share[WINDOWS_CASTS];
        int picking, waiting;      // shares the person is asked of; frames not told for want of a consumer
} castbus = {-1, {{0}}, 0, 0};

static void castbus_signal(void *context, int fd)
{
        unsigned long long one = 1;

        (void)context;
        sessionbus_call(syscall(write), (positive)fd, (positive)&one, 8, 0, 0, 0);
}

static long long castbus_now_ns(void *context)
{
        (void)context;
        return (long long)clock_monotonic_nanoseconds();
}

static int castbus_eventfd(void *context)
{
        (void)context;
        return (int)sessionbus_call(syscall(eventfd2), 0, 0x800 | 0x80000, 0, 0, 0, 0); // EFD_NONBLOCK | EFD_CLOEXEC
}

static int castbus_memfd(void *context, long bytes, void **mapped)
{
        long fd = sessionbus_call(syscall(memfd_create), (positive) "moonwater-cast-session", 1, 0, 0, 0, 0), at; // MFD_CLOEXEC

        (void)context;
        if (fd < 0)
                return -1;
        if (sessionbus_call(syscall(ftruncate), (positive)fd, (positive)bytes, 0, 0, 0, 0) < 0 ||
            ((at = sessionbus_call(syscall(mmap), 0, (positive)bytes, 3, 1, (positive)fd, 0)) < 0 && at > -4096))
        {
                system_close((int)fd);
                return -1;
        }
        *mapped = (void *)at;
        return (int)fd;
}

static void castbus_unmap(void *context, void *mapped, long bytes)
{
        sessionbus_release(context, mapped, bytes);
}

static void castbus_close(void *context, int fd)
{
        (void)context;
        if (fd >= 0)
                system_close(fd);
}

static unsigned castbus_cookie(void *context)
{
        unsigned value = 0;

        sessionbus_random(context, (unsigned char *)&value, sizeof value);
        return value;
}

static const struct pwire_host castbus_pwire = {0, sessionbus_alloc, sessionbus_release, castbus_now_ns, castbus_eventfd, castbus_signal,
                                                castbus_memfd, castbus_unmap, castbus_close, castbus_cookie};

// The compositor's table, followed only to the compositor (window.c's windows_board_trusted), or null.
static struct windows *castbus_table(void)
{
        return windows_board_trusted();
}

// The compositor rung: it looks at the casts again (the clipboard's door is its doorbell for them).
static void castbus_ring(struct windows *table)
{
        long door;

        __atomic_add_fetch(&table->cast_serial, 1u, __ATOMIC_ACQ_REL);
        door = sessionbus_call(syscall(openat), (positive)AT_FDCWD, (positive)WINDOWS_CLIP_DOOR, O_WRONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW, 0, 0, 0);
        if (door >= 0)
        {
                sessionbus_call(syscall(write), (positive)door, (positive) "c", 1, 0, 0, 0);
                system_close((int)door);
        }
}

static struct castbus_share *castbus_of(long ticket)
{
        for (unsigned int k = 0; ticket && k < WINDOWS_CASTS; k++)
                if (castbus.share[k].ticket == ticket)
                        return &castbus.share[k];
        return 0;
}

// One line of the share's life in the bus's log (/run/moonwater/bus.log).
static void castbus_say(const char *what, long ticket, const char *why)
{
        string_format(log, (string_address) "sessionbus: share %p %s%s%s\n", (positive)ticket, (string_address)what, (string_address)(why ? ": " : ""),
                      (string_address)(why ? why : ""));
        log_flush();
}

static void castbus_unlink(struct castbus_share *s, struct castbus_link *l, struct windows *table, const char *why)
{
        if (l->pw < 0)
                return;
        {
                unsigned long frames = 0, drops = 0;
                char said[96];

                pwire_counts(l->pw, &frames, &drops);
                said[0] = 0;
                string_copy_bounded((p8 *)said, (string_address)(why ? why : (pwire_dead(l->pw) ? pwire_dead(l->pw) : "closed")), 64);
                castbus_say("stream closed", s->ticket, said);
                string_format(log, (string_address) "sessionbus: share %p told %p frames, let %p go\n", (positive)s->ticket, (positive)frames, (positive)drops);
                log_flush();
        }
        for (unsigned int k = 0; k < WINDOWS_CAST_SLOTS; k++)
                if (table && (l->holding >> k & 1))
                        cast_give(&table->casts[s->row], k);
        pwire_close(l->pw);
        system_close(l->fd);
        l->pw = -1, l->fd = -1, l->holding = 0;
}

// A share let go: its connections closed, its ring, and its row made free (the compositor drops the ring when it sees it).
static void castbus_end(struct castbus_share *s, const char *why)
{
        struct windows *table = castbus_table();

        castbus_say("ended", s->ticket, why);
        for (int k = 0; k < CASTBUS_PER; k++)
                castbus_unlink(s, &s->link[k], table, "the share ended");
        if (s->ring >= 0)
                system_close(s->ring);
        if (table && table->casts[s->row].ticket == (unsigned int)s->ticket)
        {
                __atomic_store_n(&table->casts[s->row].state, WINDOWS_CAST_FREE, __ATOMIC_RELEASE);
                table->casts[s->row].ticket = 0;
                castbus_ring(table);
        }
        memory_zero(s, sizeof(*s));
        s->ring = -1;
        for (int k = 0; k < CASTBUS_PER; k++)
                s->link[k].pw = -1, s->link[k].fd = -1;
}

// The program's name, as its /proc says it: a Flatpak's application id, else its command's name.
static void castbus_name(long pid, char *into, positive room)
{
        char path[64], text[512];
        long fd, got;

        string_copy_bounded((p8 *)into, (string_address) "a program", room);
        windows_num_path(path, sizeof path, "/proc/", (unsigned long)pid, "/root/.flatpak-info");
        fd = sessionbus_call(syscall(openat), (positive)AT_FDCWD, (positive)path, O_RDONLY | O_CLOEXEC | O_NONBLOCK, 0, 0, 0);
        if (fd >= 0)
        {
                got = system_read_retry((positive)fd, text, sizeof text - 1);
                system_close((int)fd);
                text[got > 0 ? got : 0] = 0;
                for (long at = 0; got > 5 && text[at]; at++)
                        if ((at == 0 || text[at - 1] == '\n') && !memory_compare(text + at, "name=", 5))
                        {
                                positive n = 0;

                                at += 5;
                                while (text[at + (long)n] && text[at + (long)n] != '\n' && n + 1 < room)
                                        n++;
                                memory_copy(into, text + at, n), into[n] = 0;
                                return;
                        }
        }
        windows_num_path(path, sizeof path, "/proc/", (unsigned long)pid, "/comm");
        fd = sessionbus_call(syscall(openat), (positive)AT_FDCWD, (positive)path, O_RDONLY | O_CLOEXEC | O_NONBLOCK, 0, 0, 0);
        if (fd < 0)
                return;
        got = system_read_retry((positive)fd, text, sizeof text - 1);
        system_close((int)fd);
        while (got > 0 && (text[got - 1] == '\n' || text[got - 1] == 0))
                got--;
        if (got > 0)
        {
                positive n = (positive)got < room - 1 ? (positive)got : room - 1;

                memory_copy(into, text, n), into[n] = 0;
        }
}

// Start: the person asked, in a free row; 0, or -1 when there is no desktop or no row.
static int castbus_pick(void *context, const struct dbus_cast_ask *ask)
{
        struct windows *table = castbus_table();
        struct castbus_share *s = 0;

        (void)context;
        if (!table)
                return -1;
        for (unsigned int k = 0; k < WINDOWS_CASTS && !s; k++)
                if (!castbus.share[k].ticket && !table->casts[k].state)
                        s = &castbus.share[k], s->row = (int)k;
        if (!s)
                return -1;
        {
                struct windows_cast *row = &table->casts[s->row];
                int row_at = s->row;

                memory_zero(s, sizeof(*s));
                s->row = row_at, s->ring = -1, s->ticket = ask->ticket;
                for (int k = 0; k < CASTBUS_PER; k++)
                        s->link[k].pw = -1, s->link[k].fd = -1;
                memory_zero(row, sizeof(*row));
                // the uid as the sound server knows it (a bowl's program, uid 0 here, by its user namespace): what a share's sound is for
                row->ticket = (unsigned int)ask->ticket, row->pid = (unsigned int)ask->pid;
                row->uid = windows_peer_uid((unsigned int)ask->pid, (unsigned int)ask->uid);
                row->types = ask->types, row->cursor = ask->cursor, row->ring_fd = -1, row->latest = ~0u;
                castbus_name(ask->pid, row->app, sizeof row->app);
                __atomic_store_n(&row->state, WINDOWS_CAST_ASKED, __ATOMIC_RELEASE);
                s->state = WINDOWS_CAST_ASKED;
        }
        castbus.picking++;
        castbus_ring(table);
        return 0;
}

static void castbus_unpick(void *context, long ticket)
{
        struct castbus_share *s = castbus_of(ticket);

        (void)context;
        if (s)
                castbus_end(s, "its session ended");
}

// The ring of a live share, opened read-only from the compositor, when it is what it says it is: sealed, and as large.
static int castbus_ring_open(struct castbus_share *s, const struct windows_cast *row)
{
        char base[48], path[64];
        long fd, seals;
        p8 facts[256];
        unsigned long size = 0;

        if (s->ring >= 0)
                return s->ring;
        if (row->ring_fd < 0 || !row->ring_pid)
                return -1;
        windows_num_path(base, sizeof base, "/proc/", row->ring_pid, "/fd/");
        windows_num_path(path, sizeof path, base, (unsigned long)row->ring_fd, "");
        fd = sessionbus_call(syscall(openat), (positive)AT_FDCWD, (positive)path, O_RDONLY | O_CLOEXEC, 0, 0, 0);
        if (fd < 0)
                return -1;
        seals = sessionbus_call(syscall(fcntl), (positive)fd, 1034, 0, 0, 0, 0); // F_GET_SEALS
        memory_zero(facts, sizeof facts);
        if (system_stat_at((int)fd, (string_address) "", AT_EMPTY_PATH, 0x200, facts) >= 0)
                memory_copy(&size, facts + 40, sizeof size);
        // no write but the compositor's own mapping (F_SEAL_FUTURE_WRITE), never shrunk (F_SEAL_SHRINK), and the slots inside it
        if (seals < 0 || !(seals & 16) || !(seals & 2) || size < (unsigned long)row->slots * row->slot_bytes)
        {
                system_close((int)fd);
                return -1;
        }
        return s->ring = (int)fd;
}

// OpenPipeWireRemote: the program's end of a socket pair whose other end may see this share's ring alone; -1 when there is none.
static int castbus_remote(void *context, long ticket)
{
        struct castbus_share *s = castbus_of(ticket);
        struct windows *table = castbus_table();
        struct windows_cast *row;
        struct castbus_link *l = 0;
        int pair[2];
        struct pwire_source source;

        (void)context;
        if (!s || !table)
                return -1;
        row = &table->casts[s->row];
        if (row->ticket != (unsigned int)ticket || __atomic_load_n(&row->state, __ATOMIC_ACQUIRE) != WINDOWS_CAST_LIVE || castbus_ring_open(s, row) < 0)
                return -1;
        for (int k = 0; k < CASTBUS_PER && !l; k++)
                if (s->link[k].pw < 0)
                        l = &s->link[k];
        if (!l || sessionbus_call(syscall(socketpair), 1, 1 | 0x80000 | 0x800, 0, (positive)pair, 0, 0) < 0) // AF_UNIX, SOCK_STREAM | CLOEXEC | NONBLOCK
                return -1;
        source.width = row->width, source.height = row->height, source.stride = row->stride, source.rate = row->rate;
        source.slots = row->slots < WINDOWS_CAST_SLOTS ? row->slots : WINDOWS_CAST_SLOTS, source.slot_bytes = row->slot_bytes, source.first = 0;
        source.fd = s->ring, source.node = CASTBUS_NODE + 2u * (unsigned)ticket;
        source.name = row->kind == WINDOWS_CAST_WINDOW ? "moonwater-window" : "moonwater-screen";
        if ((l->pw = pwire_open(&source)) < 0)
        {
                system_close(pair[0]), system_close(pair[1]);
                return -1;
        }
        l->fd = pair[0], l->holding = 0, l->told = 0;
        castbus.waiting = 1;
        castbus_say(row->kind == WINDOWS_CAST_WINDOW ? "streams a window" : "streams a screen", ticket, row->app);
        return pair[1];
}

static const struct dbus_cast_host castbus_host = {0, castbus_pick, castbus_unpick, castbus_remote};

// The engine's half of the session bus started: the portal's host, the PipeWire, the wake door.
static void castbus_start(void)
{
        for (unsigned int k = 0; k < WINDOWS_CASTS; k++)
        {
                castbus.share[k].ring = -1;
                for (int j = 0; j < CASTBUS_PER; j++)
                        castbus.share[k].link[j].pw = -1, castbus.share[k].link[j].fd = -1;
        }
        pwire_start(&castbus_pwire);
        dbus_cast_set_host(&castbus_host);
        sessionbus_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)WINDOWS_CAST_WAKE, 0, 0, 0, 0);
        sessionbus_call(syscall(mknodat), (positive)AT_FDCWD, (positive)WINDOWS_CAST_WAKE, 0010000 | 0600, 0, 0, 0);
        castbus.wake = (int)sessionbus_call(syscall(openat), (positive)AT_FDCWD, (positive)WINDOWS_CAST_WAKE, O_RDWR | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW, 0, 0, 0);
}

// What a connection holds now, and the slots it gave back let go.
static void castbus_held(struct castbus_share *s, struct castbus_link *l, struct windows_cast *row)
{
        unsigned long long now = pwire_held(l->pw);

        for (unsigned int k = 0; k < WINDOWS_CAST_SLOTS; k++)
                if ((l->holding >> k & 1) && !(now >> k & 1))
                        cast_give(row, k);
        l->holding &= now;
        (void)s;
}

// The newest frame of each live share told to each of its connections that is ready for one and has not had it.
static void castbus_deliver(struct windows *table)
{
        long long now = castbus_now_ns(0);

        castbus.waiting = 0;
        for (unsigned int at = 0; at < WINDOWS_CASTS; at++)
        {
                struct castbus_share *s = &castbus.share[at];
                struct windows_cast *row;
                unsigned int frames;

                if (!s->ticket)
                        continue;
                row = &table->casts[s->row];
                if (row->ticket != (unsigned int)s->ticket || __atomic_load_n(&row->state, __ATOMIC_ACQUIRE) != WINDOWS_CAST_LIVE)
                        continue;
                frames = __atomic_load_n(&row->frames, __ATOMIC_ACQUIRE);
                for (int k = 0; k < CASTBUS_PER; k++)
                {
                        struct castbus_link *l = &s->link[k];
                        unsigned int seq;
                        int slot, told;

                        if (l->pw < 0)
                                continue;
                        castbus_held(s, l, row);
                        if (!frames || l->told == frames)
                                continue;
                        if (!pwire_ready(l->pw, now))
                        {
                                castbus.waiting = 1;
                                continue;
                        }
                        if ((slot = cast_take(row, &seq)) < 0)
                        {
                                castbus.waiting = 1;
                                continue;
                        }
                        // the frame's own damage, when it is the one after the last told; else the whole frame
                        told = pwire_frame(l->pw, (unsigned int)slot, (long long)row->stamp[slot], frames,
                                           (const int (*)[4])row->damage[slot], frames - l->told == 1 ? row->damages[slot] : 0);
                        if (told == 1 && !(l->holding >> slot & 1))
                                l->holding |= 1ull << slot, l->told = frames;
                        else
                        {
                                cast_give(row, (unsigned int)slot);
                                if (told == 1)
                                        l->told = frames;
                                else
                                        castbus.waiting = 1;
                        }
                }
        }
}

// The descriptors the engine waits on, after the bus's: the wake door, and each connection's socket and its consumer's eventfd.
static long castbus_watch(struct sessionbus_poll *watch, long *owner, long base)
{
        long count = 0;

        if (castbus.wake >= 0)
                watch[count] = (struct sessionbus_poll){castbus.wake, SESSIONBUS_POLLIN, 0}, owner[count++] = base;
        for (unsigned int at = 0; at < WINDOWS_CASTS; at++)
                for (int k = 0; k < CASTBUS_PER; k++)
                {
                        struct castbus_link *l = &castbus.share[at].link[k];
                        const unsigned char *bytes;
                        const int *fds;
                        long nfds;
                        int done;

                        if (l->pw < 0)
                                continue;
                        watch[count] = (struct sessionbus_poll){l->fd, (short)(SESSIONBUS_POLLIN | (pwire_pending(l->pw, &bytes, &fds, &nfds) ? SESSIONBUS_POLLOUT : 0)), 0};
                        owner[count++] = base - 1 - (long)(at * CASTBUS_PER + (unsigned)k) * 2;
                        if ((done = pwire_wake_fd(l->pw)) >= 0)
                                watch[count] = (struct sessionbus_poll){done, SESSIONBUS_POLLIN, 0}, owner[count++] = base - 2 - (long)(at * CASTBUS_PER + (unsigned)k) * 2;
                }
        return count;
}

// What a connection's PipeWire has to send, sent (its descriptors with the first of the bytes).
static void castbus_flush(struct castbus_link *l)
{
        const unsigned char *bytes;
        const int *fds;
        long nfds, length = pwire_pending(l->pw, &bytes, &fds, &nfds), got;
        unsigned char ctrl[16 + sizeof(int) * 64 + 8] __attribute__((aligned(8)));
        struct sessionbus_iovec iov = {(void *)bytes, (unsigned long)length};
        struct sessionbus_msghdr msg = {0, 0, 0, &iov, 1, 0, 0, 0, 0};

        if (length <= 0)
                return;
        if (nfds > 64)
        {
                nfds = 0, length = 0; // more than one message may carry: the connection ends at its next read
                return;
        }
        if (nfds > 0)
        {
                struct sessionbus_cmsghdr *cm = (struct sessionbus_cmsghdr *)ctrl;
                unsigned long clen = sizeof(struct sessionbus_cmsghdr) + (unsigned long)nfds * sizeof(int);

                memory_zero(ctrl, sizeof ctrl);
                cm->len = clen, cm->level = 1, cm->type = 1; // SOL_SOCKET, SCM_RIGHTS
                memory_copy_apart(ctrl + sizeof(struct sessionbus_cmsghdr), fds, (unsigned long)nfds * sizeof(int));
                msg.control = ctrl, msg.controllen = (clen + 7ul) & ~7ul;
        }
        got = sessionbus_call(syscall(sendmsg), (positive)l->fd, (positive)&msg, 0x40 | 0x4000, 0, 0, 0); // MSG_DONTWAIT | MSG_NOSIGNAL
        if (got > 0)
                pwire_wrote(l->pw, got);
}

// One of the engine's descriptors is ready (its owner as castbus_watch gave it).
static void castbus_event(long base, long owner, short returned)
{
        struct windows *table = castbus_table();
        long n = base - owner, index;
        struct castbus_share *s;
        struct castbus_link *l;

        if (n == 0)
        {
                unsigned char drain[256];

                while (sessionbus_call(syscall(read), (positive)castbus.wake, (positive)drain, sizeof drain, 0, 0, 0) > 0)
                        ;
                if (table)
                        castbus_deliver(table);
                return;
        }
        index = (n - 1) / 2;
        if (index < 0 || index >= (long)WINDOWS_CASTS * CASTBUS_PER)
                return;
        s = &castbus.share[index / CASTBUS_PER];
        l = &s->link[index % CASTBUS_PER];
        if (l->pw < 0)
                return;
        if ((n - 1) % 2 == 1) // the consumer read a frame: what it gave back let go, and a frame that waited for it told
        {
                unsigned long long drain;

                sessionbus_call(syscall(read), (positive)pwire_wake_fd(l->pw), (positive)&drain, 8, 0, 0, 0);
                if (table)
                        castbus_held(s, l, &table->casts[s->row]), castbus_deliver(table);
                return;
        }
        if (returned & SESSIONBUS_POLLIN)
        {
                unsigned char bytes[16384], ctrl[16 + sizeof(int) * 32 + 8] __attribute__((aligned(8)));
                struct sessionbus_iovec iov = {bytes, sizeof bytes};
                struct sessionbus_msghdr msg = {0, 0, 0, &iov, 1, ctrl, sizeof ctrl, 0, 0};
                long got = sessionbus_call(syscall(recvmsg), (positive)l->fd, (positive)&msg, 0x40 | 0x40000000, 0, 0, 0); // MSG_DONTWAIT | MSG_CMSG_CLOEXEC
                int fds[32];
                long nfds = 0;

                if (got > 0 && msg.controllen >= sizeof(struct sessionbus_cmsghdr))
                {
                        struct sessionbus_cmsghdr *cm = (struct sessionbus_cmsghdr *)ctrl;

                        if (cm->level == 1 && cm->type == 1 && cm->len > sizeof(*cm))
                                for (unsigned long k = 0; k < (cm->len - sizeof(*cm)) / sizeof(int) && nfds < 32; k++)
                                        memory_copy(&fds[nfds++], ctrl + sizeof(*cm) + k * sizeof(int), sizeof(int));
                }
                if (got == 0 || (got < 0 && got != -11 && got != -4) || (got > 0 && pwire_read(l->pw, bytes, got, fds, nfds) < 0))
                {
                        castbus_unlink(s, l, table, got > 0 ? 0 : got == 0 ? "the program closed it" : "a read failed");
                        return;
                }
        }
        else if (returned & (SESSIONBUS_POLLHUP | SESSIONBUS_POLLERR))
        {
                castbus_unlink(s, l, table, "the program hung up");
                return;
        }
        castbus_flush(l);
        if (table)
                castbus_deliver(table);
}

static void castbus_trace(struct windows *table);

// A turn of the loop: the answers of the share app and the compositor read from the rows, sessions of programs gone let go,
// frames that waited told, every connection's output sent.
static void castbus_tick(void)
{
        struct windows *table = castbus_table();

        dbus_cast_sweep();
        castbus.picking = 0;
        for (unsigned int at = 0; at < WINDOWS_CASTS; at++)
        {
                struct castbus_share *s = &castbus.share[at];
                struct windows_cast *row;
                unsigned int state;

                if (!s->ticket)
                        continue;
                if (!table)
                {
                        dbus_cast_ended(s->ticket); // the desktop went: every share with it
                        if (s->ticket)
                                castbus_end(s, "the desktop went");
                        continue;
                }
                row = &table->casts[s->row];
                state = row->ticket == (unsigned int)s->ticket ? __atomic_load_n(&row->state, __ATOMIC_ACQUIRE) : WINDOWS_CAST_STOPPED;
                if (state == WINDOWS_CAST_LIVE && s->state != WINDOWS_CAST_LIVE)
                {
                        unsigned int source = row->kind == WINDOWS_CAST_WINDOW ? 2u : 1u;
                        int x = 0, y = 0;

                        if (row->kind == WINDOWS_CAST_SCREEN && row->target < WINDOWS_OUTPUTS)
                                x = table->output_x[row->target], y = table->output_y[row->target];
                        dbus_cast_picked(s->ticket, 1, CASTBUS_NODE + 2u * (unsigned)s->ticket, source, x, y, row->width, row->height);
                }
                else if (state == WINDOWS_CAST_REFUSED || state == WINDOWS_CAST_STOPPED || state == WINDOWS_CAST_FREE)
                {
                        long ticket = s->ticket;

                        if (state == WINDOWS_CAST_REFUSED && s->state != WINDOWS_CAST_LIVE)
                                dbus_cast_picked(ticket, 0, 0, 0, 0, 0, 0, 0);
                        else
                                dbus_cast_ended(ticket);
                        if ((s = castbus_of(ticket)))
                                castbus_end(s, state == WINDOWS_CAST_REFUSED ? "the person said no" : state == WINDOWS_CAST_STOPPED ? "stopped" : "its row let go");
                        continue;
                }
                s->state = state;
                castbus.picking += state == WINDOWS_CAST_ASKED || state == WINDOWS_CAST_PICKED;
                for (int k = 0; k < CASTBUS_PER; k++)
                {
                        struct castbus_link *l = &s->link[k];

                        if (l->pw >= 0 && pwire_dead(l->pw))
                                castbus_unlink(s, l, table, 0);
                        else if (l->pw >= 0)
                                castbus_flush(l);
                }
        }
        if (table && castbus.waiting)
                castbus_deliver(table);
        castbus_trace(table);
}

// With the bus traced (-t), each live share's state once a second: the compositor's frames, the ring's slots, each connection's.
static void castbus_trace(struct windows *table)
{
        static long last;
        long now = sessionbus_now(0);

        if (!sessionbus_host.trace || !table || now - last < 1000)
                return;
        last = now;
        for (unsigned int at = 0; at < WINDOWS_CASTS; at++)
        {
                struct castbus_share *s = &castbus.share[at];
                struct windows_cast *row = &table->casts[s->row];

                if (!s->ticket)
                        continue;
                string_format(log, (string_address) "sessionbus: share %p state %p frames %p latest %p held %p%p%p%p seq %p %p %p %p\n", (positive)s->ticket,
                              (positive)row->state, (positive)row->frames, (positive)row->latest, (positive)row->held[0], (positive)row->held[1],
                              (positive)row->held[2], (positive)row->held[3], (positive)row->seq[0], (positive)row->seq[1], (positive)row->seq[2], (positive)row->seq[3]);
                for (int k = 0; k < CASTBUS_PER; k++)
                        if (s->link[k].pw >= 0)
                        {
                                unsigned activation, status, buffer;
                                unsigned long frames, drops;

                                pwire_state(s->link[k].pw, &activation, &status, &buffer);
                                pwire_counts(s->link[k].pw, &frames, &drops);
                                string_format(log, (string_address) "sessionbus:   stream told %p (at %p) holding %p pw-held %p activation %p io %p buffer %p ready %p\n",
                                              (positive)frames, (positive)s->link[k].told, (positive)s->link[k].holding, (positive)pwire_held(s->link[k].pw),
                                              (positive)activation, (positive)status, (positive)buffer, (positive)pwire_ready(s->link[k].pw, castbus_now_ns(0)));
                        }
                log_flush();
        }
}

// How long the loop may wait: shorter while the person picks or a frame waits for its consumer.
static long castbus_wait(long most)
{
        long wait = castbus.waiting ? CASTBUS_AGAIN_MS : castbus.picking ? CASTBUS_LOOK_MS : most;

        return wait < most ? wait : most;
}

#endif /* CASTBUS_INCLUDED */
