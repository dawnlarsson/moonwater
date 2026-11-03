/*
        The compositor's side of the Wayland shim: the socket, the clients' descriptors, and the shim's host (struct
        wayland_host) made of the compositor's own functions. It is included by compositor.c after wayland.c, when
        CONFIG_MOONWATER_WAYLAND is on, and is part of the compositor's one loop: the loop polls the listening socket
        and each client's, calls wayland_feed with what was read, and writes back what the shim queued, once a pass
        before the screen is drawn (so an event waits for no frame) and once after (for the frame's own callbacks).

        A client's window is a row of the table that this process claims for it; what a program does to its row
        with the table, the shim does here by calling the compositor's functions on it, and what the compositor says
        of a row it reads back in wayland_facts. Nothing a client sent reaches any compositor function unchecked:
        the shim checks first, and every call below is one the compositor makes for its own programs.

        The socket is /run/moonwater/wayland-0, mode 0666, so any program of the machine may connect and is held to
        the same caps whoever it is; the peer's pid and uid are kept for the table and for the per-user cap (a user
        may hold at most COMPOSITOR_WL_PER_USER of the slots, root as many as are free), so one user cannot take every
        slot and starve the others. A pool is mapped read-only once; the one thing a client can do to a mapping is
        shrink its file, which raises SIGBUS where the compositor reads it, and the handler maps zero pages over the
        pool and marks it, and the copy that was in progress finishes on zeros and is refused.
*/
#ifndef WAYLAND_HOST_INCLUDED
#define WAYLAND_HOST_INCLUDED

#define COMPOSITOR_WL_PATH "/run/moonwater/wayland-0"
#define COMPOSITOR_WL_BACKLOG 16
#define COMPOSITOR_WL_PER_USER 8
#define COMPOSITOR_WL_READ 16384        // one read; a client gets at most COMPOSITOR_WL_READS of them a pass
#define COMPOSITOR_WL_READS 4
#define COMPOSITOR_WL_ACCEPTS 4
#define COMPOSITOR_POLLOUT 4
#define COMPOSITOR_POLLERR 8
#define COMPOSITOR_WL_SIGBUS 7

_Static_assert(WAYLAND_ROWS == WINDOWS, "the shim's rows are the compositor's table");

static struct
{
        int listen;
        int fd[WAYLAND_CLIENTS];
        int uid[WAYLAND_CLIENTS], pid[WAYLAND_CLIENTS];
        int started, handler;
        unsigned long frames;
        // Where each pool is mapped, so a SIGBUS can be put to the pool whose file was cut.
        struct
        {
                volatile unsigned long at, size;
                volatile int bad;
        } map[WAYLAND_CLIENTS * WAYLAND_POOLS];
} compositor_wl = {.listen = -1};

/* ---- the host ---- */

static void *compositor_wl_alloc(void *context, long bytes)
{
        (void)context;
        return compositor_map_zeroed((unsigned long)bytes);
}

static void compositor_wl_release(void *context, void *block, long bytes)
{
        (void)context;
        compositor_call(syscall(munmap), (positive)block, (positive)bytes, 0, 0, 0, 0);
}

/* A regular file of at least size bytes, mapped read-only and shared; null for anything else. */
static void *compositor_wl_map(void *context, long fd, long size)
{
        // struct statx: stx_mode at 28 (u16), stx_size at 40 (u64); asked for the type and the size, of the descriptor itself.
        unsigned char info[256];
        long got, at;
        unsigned int mode;
        unsigned long length;

        (void)context;
        memory_zero(info, sizeof info);
        got = compositor_call(syscall(statx), (positive)fd, (positive) "", 0x1000, 0x201, (positive)info, 0);
        if (got < 0)
                return 0;
        mode = (unsigned int)info[28] | (unsigned int)info[29] << 8;
        length = *(unsigned long *)(info + 40);
        if ((mode & 0170000) != 0100000 || length < (unsigned long)size)
                return 0;
        got = compositor_call(syscall(mmap), 0, (positive)size, 1, 1, (positive)fd, 0);
        if (got < 0 && got > -4096)
                return 0;
        for (at = 0; at < WAYLAND_CLIENTS * WAYLAND_POOLS; at++)
                if (!compositor_wl.map[at].at)
                {
                        compositor_wl.map[at].bad = 0;
                        compositor_wl.map[at].size = (unsigned long)size;
                        compositor_wl.map[at].at = (unsigned long)got;
                        return (void *)got;
                }
        compositor_call(syscall(munmap), (positive)got, (positive)size, 0, 0, 0, 0);
        return 0;
}

static void *compositor_wl_remap(void *context, void *at, long old_size, long new_size)
{
        long got = compositor_call(syscall(mremap), (positive)at, (positive)old_size, (positive)new_size, 1, 0, 0);

        (void)context;
        if (got < 0 && got > -4096)
                return 0;
        for (long slot = 0; slot < WAYLAND_CLIENTS * WAYLAND_POOLS; slot++)
                if (compositor_wl.map[slot].at == (unsigned long)at)
                {
                        compositor_wl.map[slot].size = (unsigned long)new_size;
                        compositor_wl.map[slot].at = (unsigned long)got;
                }
        return (void *)got;
}

static void compositor_wl_unmap(void *context, void *at, long size)
{
        (void)context;
        for (long slot = 0; slot < WAYLAND_CLIENTS * WAYLAND_POOLS; slot++)
                if (compositor_wl.map[slot].at == (unsigned long)at)
                {
                        compositor_wl.map[slot].at = 0;
                        compositor_wl.map[slot].size = 0;
                        compositor_wl.map[slot].bad = 0;
                }
        compositor_call(syscall(munmap), (positive)at, (positive)size, 0, 0, 0, 0);
}

static void compositor_wl_close(void *context, long fd)
{
        (void)context;
        system_close((int)fd);
}

/*
        A pool whose file was cut under the compositor: the signal handler of SIGBUS. The faulting address names the pool;
        zero pages are mapped over all of it, so the read that faulted goes on and finds zeros, and the pool is marked.
        A SIGBUS that is not on a pool is not this handler's: it is put back to its default and returned from, which
        raises it again and ends the compositor as it would have ended.
*/
static void compositor_wl_bus(int number, void *info, void *context)
{
        unsigned long where = *(unsigned long *)((unsigned char *)info + 16);

        (void)context;
        for (long slot = 0; slot < WAYLAND_CLIENTS * WAYLAND_POOLS; slot++)
                if (compositor_wl.map[slot].at && where >= compositor_wl.map[slot].at &&
                    where - compositor_wl.map[slot].at < compositor_wl.map[slot].size)
                {
                        // PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED
                        compositor_call(syscall(mmap), compositor_wl.map[slot].at, compositor_wl.map[slot].size, 1, 0x32,
                                        (positive)-1, 0);
                        compositor_wl.map[slot].bad = 1;
                        return;
                }
        system_signal_install((b32)number, 0, 0, 0, null);
}

/* Whether the pool the pixels are in was cut. */
static int compositor_wl_cut(const void *pixels)
{
        unsigned long at = (unsigned long)pixels;

        for (long slot = 0; slot < WAYLAND_CLIENTS * WAYLAND_POOLS; slot++)
                if (compositor_wl.map[slot].at && at >= compositor_wl.map[slot].at &&
                    at - compositor_wl.map[slot].at < compositor_wl.map[slot].size)
                        return compositor_wl.map[slot].bad;
        return 0;
}

/* A table row for a client's window, claimed as a program claims one, with its pid. -1 when none is free. */
static int compositor_wl_row_open(void *context, long client, long kind, int parent, long x, long y)
{
        struct windows *table = compositor.table;

        (void)context;
        if (!table || client < 0 || client >= WAYLAND_CLIENTS)
                return -1;
        for (int row = 0; row < WINDOWS; row++)
        {
                unsigned int none = 0;

                if (!__atomic_compare_exchange_n(&table->pid[row], &none, WINDOWS_TAKING, 0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
                        continue;
                windows_reset_row(table, (unsigned int)row);
                // A client draws its own frame, and keeps the whole of its picture, the top included.
                table->glass[row] = WINDOW_NO_CLOSE | WINDOW_NO_GRAB;
                if (kind == WAYLAND_KIND_POPUP)
                {
                        table->popup_parent[row] = (unsigned int)parent + 1;
                        table->popup_x[row] = (int)x;
                        table->popup_y[row] = (int)y;
                        table->popup_flags[row] = WINDOW_POPUP_POINT | WINDOW_POPUP_EXACT;
                        table->glass[row] |= WINDOW_NO_TAB;
                }
                // The client's pid as the kernel gave it; a peer in a namespace the compositor cannot see has none, and a row's pid
                // is never 0 while it is taken.
                __atomic_store_n(&table->pid[row], compositor_wl.pid[client] > 0 ? (unsigned int)compositor_wl.pid[client] : 1u,
                                 __ATOMIC_RELEASE);
                return row;
        }
        return -1;
}

static void compositor_wl_row_close(void *context, int row)
{
        (void)context;
        compositor_clip_leave(row);
        compositor_popup_leave(row);
        compositor_leave(row);
}

/*
        A client's buffer, the rows that changed, into the row's picture: written in place into the buffer the compositor
        shows, because it is this process that reads it, and into the other when the card is reading that one (the whole
        of it then). The table is then as a program leaves it after a picture, and the row is taken at once.
*/
static int compositor_wl_row_picture(void *context, int row, const struct wayland_picture *picture)
{
        struct windows *table = compositor.table;
        unsigned int width = (unsigned int)picture->width, height = (unsigned int)picture->height;
        unsigned int which = compositor.showing[row] ? compositor.front[row] & 1u : 0;
        unsigned long pitch = (unsigned long)compositor_stride();
        long x1 = 0, y1 = 0, x2 = 0, y2 = 0;
        int full = (int)picture->full || !picture->damages;
        unsigned int *to;

        (void)context;
        width = width > (unsigned int)compositor_most_width() ? (unsigned int)compositor_most_width() : width;
        height = height > (unsigned int)compositor_most_height() ? (unsigned int)compositor_most_height() : height;
        if (compositor.showing[row] && (table->pinned[row] || compositor.direct_row == row))
        {
                which ^= 1u;
                full = 1;
        }
        to = (unsigned int *)((char *)table + compositor.pixels + ((unsigned long)row * 2 + which) * compositor.buffer);
        if (full)
        {
                memory_copy_rect_u32(to, pitch, (const unsigned int *)picture->pixels, (unsigned long)picture->stride / 4, width, height);
                x2 = (long)width;
                y2 = (long)height;
        }
        else
                for (long at = 0; at < picture->damages; at++)
                {
                        long left = picture->damage[at].x, top = picture->damage[at].y;
                        long right = left + picture->damage[at].width, bottom = top + picture->damage[at].height;

                        right = right > (long)width ? (long)width : right;
                        bottom = bottom > (long)height ? (long)height : bottom;
                        if (right <= left || bottom <= top)
                                continue;
                        memory_copy_rect_u32(to + (unsigned long)top * pitch + (unsigned long)left, pitch,
                                             (const unsigned int *)(picture->pixels + top * picture->stride) + left,
                                             (unsigned long)picture->stride / 4, (unsigned long)(right - left), (unsigned long)(bottom - top));
                        if (x2 == 0 && y2 == 0)
                                x1 = left, y1 = top, x2 = right, y2 = bottom;
                        else
                        {
                                x1 = left < x1 ? left : x1;
                                y1 = top < y1 ? top : y1;
                                x2 = right > x2 ? right : x2;
                                y2 = bottom > y2 ? bottom : y2;
                        }
                }
        if (compositor_wl_cut(picture->pixels))
                return -1;
        table->width[row] = width;
        table->height[row] = height;
        table->buffer_index[row] = which;
        table->damage_x[row] = (int)x1;
        table->damage_y[row] = (int)y1;
        table->damage_width[row] = full ? 0 : (int)(x2 - x1);
        table->damage_height[row] = full ? 0 : (int)(y2 - y1);
        __atomic_store_n(&table->frame[row], table->frame[row] + 1, __ATOMIC_RELEASE);
        compositor_take(row);
        return 0;
}

// A title into the table, as a program writes one: cut to the room on a character's boundary.
static void compositor_wl_row_title(void *context, int row, const char *text, long length)
{
        struct windows *table = compositor.table;
        unsigned int used = 0, at;

        (void)context;
        (void)length;
        while (text[used])
                used++;
        at = used < WINDOWS_TITLE - 1 ? used : WINDOWS_TITLE - 1;
        // Not in the middle of a character: back to the byte that starts one.
        while (at && at < used && ((unsigned char)text[at] & 0xc0) == 0x80)
                at--;
        for (unsigned int index = 0; index < WINDOWS_TITLE; index++)
                table->title[row][index] = index < at ? text[index] : 0;
        compositor_take(row);
}

static void compositor_wl_row_do(void *context, int row, long what, long a, long b)
{
        (void)context;
        (void)b;
        if (row < 0 || row >= WINDOWS || !compositor.showing[row])
                return;
        switch (what)
        {
        case WAYLAND_DO_MINIMIZE:
                compositor_minimize_row(row);
                break;
        case WAYLAND_DO_MAXIMIZE:
                compositor_arrange(row, COMPOSITOR_ROOM);
                break;
        case WAYLAND_DO_FULLSCREEN:
                compositor_arrange(row, COMPOSITOR_MAXIMIZED);
                break;
        case WAYLAND_DO_UNMAXIMIZE:
        case WAYLAND_DO_UNFULLSCREEN:
                compositor_float(row);
                break;
        case WAYLAND_DO_CLOSE:
                compositor_close(row);
                break;
        case WAYLAND_DO_MOVE:
                // Only while the person holds the left button on this window: a client cannot take the pointer.
                if ((compositor.button_down & 1u) && compositor.button_row[0] == row && compositor.drag < 0 && compositor.resize < 0)
                        compositor_drag_begin(row);
                break;
        case WAYLAND_DO_RESIZE:
        {
                // The protocol's edges (top 1, bottom 2, left 4, right 8) are the compositor's own bits turned about.
                unsigned int edges = ((a & 1) ? COMPOSITOR_EDGE_TOP : 0) | ((a & 2) ? COMPOSITOR_EDGE_BOTTOM : 0) |
                                     ((a & 4) ? COMPOSITOR_EDGE_LEFT : 0) | ((a & 8) ? COMPOSITOR_EDGE_RIGHT : 0);

                if (edges && (compositor.button_down & 1u) && compositor.button_row[0] == row && compositor.drag < 0 && compositor.resize < 0)
                {
                        compositor_resize_begin(row, edges);
                        compositor_lit(row, edges, 1);
                }
                break;
        }
        default:
                break;
        }
}

/* What the compositor says of a row, in the shim's words. */
static int compositor_wl_row_facts(void *context, int row, struct wayland_facts *facts)
{
        struct windows *table = compositor.table;
        long states = 0;

        (void)context;
        if (row < 0 || row >= WINDOWS || !compositor.showing[row])
                return -1;
        facts->width = facts->height = 0;
        if (compositor.arranged[row] != COMPOSITOR_FLOATING || compositor.asking[row])
        {
                facts->width = (long)table->want_width[row];
                facts->height = (long)table->want_height[row];
        }
        if (compositor.arranged[row] == COMPOSITOR_ROOM)
                states |= WAYLAND_STATE_MAXIMIZED;
        else if (compositor.arranged[row] == COMPOSITOR_MAXIMIZED)
                states |= WAYLAND_STATE_FULLSCREEN;
        else if (compositor.arranged[row] != COMPOSITOR_FLOATING)
                states |= WAYLAND_STATE_TILED_LEFT | WAYLAND_STATE_TILED_RIGHT | WAYLAND_STATE_TILED_TOP | WAYLAND_STATE_TILED_BOTTOM;
        if (compositor.focus == row)
                states |= WAYLAND_STATE_ACTIVATED;
        if (compositor.resize == row)
                states |= WAYLAND_STATE_RESIZING;
        if (compositor.state[row] & WINDOW_MINIMIZED)
                states |= WAYLAND_STATE_SUSPENDED;
        facts->states = states;
        facts->closing = (compositor.state[row] & WINDOW_CLOSING) != 0;
        facts->x = compositor.x[row];
        facts->y = compositor.y[row];
        return 0;
}

static long compositor_wl_screens(void *context, struct wayland_screen *screens, long room)
{
        long count = (long)compositor_output_count();

        (void)context;
        count = count > room ? room : count;
        for (long at = 0; at < count; at++)
        {
                const char *name = compositor_output_name((int)at);
                int x, y, width, height;

                compositor_output_rect((int)at, &x, &y, &width, &height);
                screens[at].x = x;
                screens[at].y = y;
                screens[at].width = width;
                screens[at].height = height;
                screens[at].scale = compositor_output_scale((int)at) > 0 ? compositor_output_scale((int)at) : compositor.scale;
                screens[at].refresh = compositor.period ? (long)(1000000000000ull / compositor.period) : 60000;
                screens[at].physical_width = screens[at].physical_height = 0;
                for (long index = 0; index < 16; index++)
                        screens[at].name[index] = 0;
                for (long index = 0; name[index] && index < 15; index++)
                        screens[at].name[index] = name[index];
                if (!screens[at].name[0])
                {
                        screens[at].name[0] = 'C';
                        screens[at].name[1] = 'a';
                        screens[at].name[2] = 'n';
                        screens[at].name[3] = 'v';
                        screens[at].name[4] = 'a';
                        screens[at].name[5] = 's';
                }
        }
        return count;
}

static long compositor_wl_now(void *context)
{
        (void)context;
        return (long)(clock_monotonic_nanoseconds() / 1000000ull);
}

static const struct wayland_host compositor_wl_host = {
    0,
    compositor_wl_alloc,
    compositor_wl_release,
    compositor_wl_map,
    compositor_wl_remap,
    compositor_wl_unmap,
    compositor_wl_close,
    compositor_wl_row_open,
    compositor_wl_row_close,
    compositor_wl_row_picture,
    compositor_wl_row_title,
    compositor_wl_row_do,
    compositor_wl_row_facts,
    compositor_wl_screens,
    compositor_wl_now,
};

/* ---- the socket ---- */

/*
        The listening socket, made as the compositor starts: an old one at the path is taken away first (a compositor that
        crashed leaves it), the new one is open to every user, and no client waits on it: a connection waits in the kernel's
        queue (COMPOSITOR_WL_BACKLOG) until the loop accepts it.
*/
static void compositor_wl_start(void)
{
        unsigned char address[110];
        long length = 0, got;

        if (compositor_wl.started)
                return;
        memory_zero(address, sizeof address);
        address[0] = 1; // AF_UNIX
        while (COMPOSITOR_WL_PATH[length])
        {
                address[2 + length] = (unsigned char)COMPOSITOR_WL_PATH[length];
                length++;
        }
        compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)COMPOSITOR_WL_PATH, 0, 0, 0, 0);
        // SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC
        got = compositor_call(syscall(socket), 1, 1 | 0x800 | 0x80000, 0, 0, 0, 0);
        if (got < 0)
                return;
        compositor_wl.listen = (int)got;
        if (compositor_call(syscall(bind), (positive)got, (positive)address, (positive)(2 + length + 1), 0, 0, 0) < 0 ||
            compositor_call(syscall(listen), (positive)got, COMPOSITOR_WL_BACKLOG, 0, 0, 0, 0) < 0)
        {
                system_close(compositor_wl.listen);
                compositor_wl.listen = -1;
                return;
        }
        compositor_call(syscall(fchmodat), (positive)AT_FDCWD, (positive)COMPOSITOR_WL_PATH, 0666, 0, 0, 0);
        for (long at = 0; at < WAYLAND_CLIENTS; at++)
                compositor_wl.fd[at] = -1;
        if (wayland_start(&compositor_wl_host) < 0)
        {
                system_close(compositor_wl.listen);
                compositor_wl.listen = -1;
                return;
        }
        compositor_wl.started = 1;
        // The handler of a pool cut under a read: SA_SIGINFO and SA_NODEFER, with the library's restorer.
        compositor_wl.handler = system_signal_install(COMPOSITOR_WL_SIGBUS, (positive)compositor_wl_bus,
                                                      (SIGNAL_CATCH_FLAGS & ~SIGNAL_RESTART) | 4 | 0x40000000,
                                                      SIGNAL_CATCH_RESTORER, null);
}

static void compositor_wl_drop(long slot)
{
        if (compositor_wl.fd[slot] >= 0)
                system_close(compositor_wl.fd[slot]);
        compositor_wl.fd[slot] = -1;
        wayland_drop(slot);
}

/* One connection, if one waits: its peer is read from the socket, and it takes a slot unless the caps say no. */
static void compositor_wl_accept(void)
{
        for (int tries = 0; tries < COMPOSITOR_WL_ACCEPTS; tries++)
        {
                int credentials[3] = {0, 0, 0}, per_user = 0;
                unsigned int size = sizeof credentials;
                long fd = compositor_call(syscall(accept4), (positive)compositor_wl.listen, 0, 0, 0x800 | 0x80000, 0, 0), slot;

                if (fd < 0)
                        return;
                // SOL_SOCKET, SO_PEERCRED: the kernel's word for who is on the other end, not the client's.
                if (compositor_call(syscall(getsockopt), (positive)fd, 1, 17, (positive)credentials, (positive)&size, 0) < 0)
                {
                        system_close((int)fd);
                        continue;
                }
                for (slot = 0; slot < WAYLAND_CLIENTS; slot++)
                        per_user += compositor_wl.fd[slot] >= 0 && compositor_wl.uid[slot] == credentials[1];
                if (credentials[1] != 0 && per_user >= COMPOSITOR_WL_PER_USER)
                {
                        system_close((int)fd);
                        continue;
                }
                slot = wayland_connect(credentials[0], credentials[1]);
                if (slot < 0)
                {
                        system_close((int)fd);
                        continue;
                }
                compositor_wl.fd[slot] = (int)fd;
                compositor_wl.pid[slot] = credentials[0];
                compositor_wl.uid[slot] = credentials[1];
        }
}

/*
        What a client has sent, a bounded amount of it: up to COMPOSITOR_WL_READS reads of COMPOSITOR_WL_READ bytes with the
        descriptors that came, to the shim. A client that sends faster than this has the rest next pass, after everyone
        else's turn. A stream that ended or failed, or control data that did not fit, is a client gone.
*/
static void compositor_wl_read(long slot)
{
        static unsigned char bytes[COMPOSITOR_WL_READ];

        for (int reads = 0; reads < COMPOSITOR_WL_READS; reads++)
        {
                positive control[(sizeof(positive) * 3 + WAYLAND_FDS * sizeof(int) + 15) / sizeof(positive) + 2];
                positive message[7], vector[2] = {(positive)bytes, sizeof bytes};
                long got, fds[WAYLAND_FDS], count = 0;

                memory_zero(message, sizeof message);
                memory_zero(control, sizeof control);
                message[2] = (positive)vector;
                message[3] = 1;
                message[4] = (positive)control;
                message[5] = sizeof control;
                // MSG_DONTWAIT | MSG_CMSG_CLOEXEC
                got = compositor_call(syscall(recvmsg), (positive)compositor_wl.fd[slot], (positive)message, 0x40 | 0x40000000, 0, 0, 0);
                if (got == -11) // EAGAIN
                        return;
                if (got <= 0)
                {
                        compositor_wl_drop(slot);
                        return;
                }
                // The descriptors, from the control data: SCM_RIGHTS in SOL_SOCKET.
                for (unsigned char *at = (unsigned char *)control; message[5] >= 16 && at < (unsigned char *)control + message[5];)
                {
                        unsigned long length = *(unsigned long *)at;
                        int level = *(int *)(at + 8), type = *(int *)(at + 12);

                        if (length < 16 || at + length > (unsigned char *)control + message[5])
                                break;
                        if (level == 1 && type == 1)
                                for (unsigned long item = 16; item + 4 <= length && count < WAYLAND_FDS; item += 4)
                                        fds[count++] = *(int *)(at + item);
                        at += (length + 7) & ~7ul;
                }
                // MSG_CTRUNC: descriptors were lost, and what is left of the stream can no longer be trusted to line up.
                if (message[6] & 0x8)
                {
                        for (long item = 0; item < count; item++)
                                system_close((int)fds[item]);
                        compositor_wl_drop(slot);
                        return;
                }
                if (wayland_feed(slot, bytes, got, fds, count) < 0)
                        return;
                if (got < (long)sizeof bytes)
                        return;
        }
}

/*
        What the shim has queued for a client, written: the bytes with the descriptors that go with them, once. What the
        socket takes is taken off the queue and the rest waits for the loop to find the socket writable. A client whose
        queue is full is dead (the shim says so), and one that was sent a protocol error is dropped once it is sent.
*/
static void compositor_wl_flush(long slot)
{
        const unsigned char *bytes;
        const long *fds;
        long fd_count, length, got;
        int dead;

        if (compositor_wl.fd[slot] < 0)
                return;
        dead = (int)wayland_dead(slot);
        length = wayland_out(slot, &bytes, &fds, &fd_count);
        if (dead == 2)
        {
                compositor_wl_drop(slot);
                return;
        }
        if (length)
        {
                positive control[2 + WAYLAND_OUT_FDS], message[7], vector[2] = {(positive)bytes, (positive)length};
                int pass[WAYLAND_OUT_FDS];

                memory_zero(message, sizeof message);
                memory_zero(control, sizeof control);
                message[2] = (positive)vector;
                message[3] = 1;
                if (fd_count > 0)
                {
                        for (long at = 0; at < fd_count && at < WAYLAND_OUT_FDS; at++)
                                pass[at] = (int)fds[at];
                        control[0] = 16 + (positive)fd_count * 4;
                        control[1] = 1 | (positive)1 << 32; // SOL_SOCKET, SCM_RIGHTS
                        for (long at = 0; at < fd_count; at++)
                                ((int *)(control + 2))[at] = pass[at];
                        message[4] = (positive)control;
                        message[5] = ((16 + (positive)fd_count * 4) + 7) & ~(positive)7;
                }
                // MSG_DONTWAIT | MSG_NOSIGNAL
                got = compositor_call(syscall(sendmsg), (positive)compositor_wl.fd[slot], (positive)message, 0x40 | 0x4000, 0, 0, 0);
                if (got > 0)
                        wayland_sent(slot, got);
                else if (got != -11)
                {
                        compositor_wl_drop(slot);
                        return;
                }
        }
        if (dead == 1 && !wayland_out(slot, &bytes, &fds, &fd_count))
                compositor_wl_drop(slot);
}

static void compositor_wl_flush_all(void)
{
        for (long slot = 0; slot < WAYLAND_CLIENTS; slot++)
                if (compositor_wl.fd[slot] >= 0)
                        compositor_wl_flush(slot);
}

/* Where the polls for the clients go in the loop's list, and what comes of them. */
static unsigned int compositor_wl_poll(struct compositor_poll *wanted, unsigned int count)
{
        if (!compositor_wl.started)
                return count;
        wanted[count++] = (struct compositor_poll){compositor_wl.listen, COMPOSITOR_POLLIN, 0};
        for (long slot = 0; slot < WAYLAND_CLIENTS; slot++)
        {
                const unsigned char *bytes;
                const long *fds;
                long fd_count;
                short events = COMPOSITOR_POLLIN;

                if (compositor_wl.fd[slot] >= 0 && wayland_out(slot, &bytes, &fds, &fd_count))
                        events |= COMPOSITOR_POLLOUT;
                wanted[count++] = (struct compositor_poll){compositor_wl.fd[slot], events, 0};
        }
        return count;
}

static void compositor_wl_pass(const struct compositor_poll *wanted)
{
        if (!compositor_wl.started)
                return;
        if (wanted[0].returned)
                compositor_wl_accept();
        for (long slot = 0; slot < WAYLAND_CLIENTS; slot++)
        {
                short returned = wanted[1 + slot].returned;

                if (compositor_wl.fd[slot] < 0 || !returned)
                        continue;
                if (returned & COMPOSITOR_POLLIN)
                        compositor_wl_read(slot);
                else if (returned & (COMPOSITOR_POLLHUP | COMPOSITOR_POLLERR))
                {
                        compositor_wl_drop(slot);
                        continue;
                }
                if (compositor_wl.fd[slot] >= 0 && wayland_dead(slot))
                        compositor_wl_flush(slot);
        }
}

/*
        The windows the compositor knocked on this pass, said to their clients (a size asked, a state changed, a close),
        and everything queued sent. Called before the frame is drawn, so no event waits for it.
*/
static void compositor_wl_wake(void)
{
        if (!compositor_wl.started)
                return;
        for (int row = 0; row < WINDOWS; row++)
                if ((compositor.woken & (1u << row)) && wayland_row_owner(row) >= 0)
                {
                        wayland_row_wake(row);
                        compositor.woken &= ~(1u << row);
                }
        compositor_wl_flush_all();
}

/* A frame was drawn: the callbacks that waited for it are answered, and sent. */
static void compositor_wl_frame(void)
{
        if (!compositor_wl.started)
                return;
        wayland_frame();
        compositor_wl_flush_all();
}

static void compositor_wl_stop(void)
{
        if (!compositor_wl.started)
                return;
        for (long slot = 0; slot < WAYLAND_CLIENTS; slot++)
                if (compositor_wl.fd[slot] >= 0)
                        compositor_wl_drop(slot);
        wayland_stop();
        system_close(compositor_wl.listen);
        compositor_wl.listen = -1;
        compositor_wl.started = 0;
        compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)COMPOSITOR_WL_PATH, 0, 0, 0, 0);
}

#endif /* WAYLAND_HOST_INCLUDED */
