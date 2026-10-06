/*
        What Moonwater's core gives the kernel objects built beside it.

        Canvas is one: its own object in src/canvas, linked into the same
        image, so it cannot see a name the core keeps static. Everything it
        may reach is here, and everything here has the moonwater_ prefix
        because the kernel's names are one namespace. The other direction --
        what the core asks of Canvas -- is src/canvas/canvas.h.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/
#ifndef MOONWATER_SEAM_INCLUDED
#define MOONWATER_SEAM_INCLUDED

#include <linux/fs.h>
#include <linux/percpu.h>
#include <linux/timekeeping.h>

/*
        Every request number is the encoding of the struct it carries.

        A handler copies sizeof(its struct) from the caller, and the caller
        sized its buffer from the number it sent. The two agree only while
        nobody edits one without the other, and the day they do not, the
        kernel reads or writes past what the caller allocated -- a struct
        that grew by a field, behind a number that still says the old size,
        is a copy_to_user of stack the caller never asked for. So the number
        is not typed in and trusted: it is rebuilt here from the direction,
        the request number and sizeof, and the build stops on a mismatch.
        The size is what _IOC_SIZE reads and what these are; the type is the
        letter 's', and no number is used twice.
*/
#define IOCTL_IS(command, direction, request, size)                            \
        _Static_assert((command) == (((unsigned int)(direction) << 30) |        \
                                     ((unsigned int)(size) << 16) |             \
                                     ((unsigned int)'s' << 8) |                 \
                                     (unsigned int)(request)),                  \
                       #command " does not encode the struct it carries")
#define IOCTL_NONE 0
#define IOCTL_WRITE 1
#define IOCTL_READ 2
#define IOCTL_BOTH 3

/*
        An open /dev/spark file keeps one pointer for the display client, and
        the core never looks at it: whatever the client hangs there is its
        own, and it is the client's to free when the file is released. It is
        the first member of the file's context, so the file is all a client
        is handed.
*/
static inline void **moonwater_display(struct file *file)
{
        return file->private_data;
}

struct pid;

/*
        Start a program with no arguments and no environment, as a thread of
        the kernel, and if record is given have the task write its thread
        group there before it execs. The program is borrowed, not copied.
*/
int moonwater_spawn_recorded(const char *program, struct pid **record);

// Fire one of the machine's events, as a key bound to it would.
void moonwater_bind_fire(unsigned int event);

// Whether a key belongs to a binding, which then takes it and not the desktop.
_Bool moonwater_bind_swallowed(unsigned int code, int value);

/*
        What an input handler costs, counted where it runs.

        A handler is called for every report of every device it is attached
        to, so what it does is paid for each of them. This is the counting
        both of Moonwater's handlers do, the one here and Canvas's, and what
        `moonwater latency` reads back. A count per processor, so a report is
        two stores to a line no other processor writes (the handler runs with
        preemption off, in the input core's own lock, which is what
        __this_cpu_* asks for); and one report in MOONWATER_COST_EVERY is
        timed, which spreads the two clock reads over sixteen of them.

        The reading side takes each processor's with READ_ONCE and does not
        stop anyone: a figure that is a report or two behind is a figure.
*/
#define MOONWATER_COST_EVERY 16

struct handler_cost {
        unsigned long events;   // reports counted
        unsigned long samples;  // the ones that were timed
        unsigned long total_ns; // their time, added
        unsigned long worst_ns; // the longest of them
};

static __always_inline u64 moonwater_cost_begin(struct handler_cost __percpu *cost)
{
        unsigned long events = __this_cpu_read(cost->events) + 1;

        __this_cpu_write(cost->events, events);
        return events % MOONWATER_COST_EVERY ? 0 : ktime_get_ns();
}

static __always_inline void moonwater_cost_end(struct handler_cost __percpu *cost,
                                               u64 started)
{
        u64 spent;

        if (likely(!started))
                return;

        spent = ktime_get_ns() - started;
        __this_cpu_add(cost->samples, 1);
        __this_cpu_add(cost->total_ns, spent);
        if (spent > __this_cpu_read(cost->worst_ns))
                __this_cpu_write(cost->worst_ns, spent);
}

static inline void moonwater_cost_read(struct handler_cost __percpu *cost,
                                       struct handler_cost *out)
{
        int cpu;

        for_each_possible_cpu(cpu)
        {
                const struct handler_cost *one = per_cpu_ptr(cost, cpu);

                out->events += READ_ONCE(one->events);
                out->samples += READ_ONCE(one->samples);
                out->total_ns += READ_ONCE(one->total_ns);
                if (READ_ONCE(one->worst_ns) > out->worst_ns)
                        out->worst_ns = READ_ONCE(one->worst_ns);
        }
}

static inline void moonwater_cost_reset(struct handler_cost __percpu *cost)
{
        int cpu;

        for_each_possible_cpu(cpu)
        {
                struct handler_cost *one = per_cpu_ptr(cost, cpu);

                WRITE_ONCE(one->events, 0);
                WRITE_ONCE(one->samples, 0);
                WRITE_ONCE(one->total_ns, 0);
                WRITE_ONCE(one->worst_ns, 0);
        }
}

#endif // MOONWATER_SEAM_INCLUDED
