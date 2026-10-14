/*
        Canvas's ABI: what a program that is not Canvas can ask of it.

        The requests /dev/spark takes for the compositor, the numbers of the
        window table's two calls, the two bindings a desktop fires and the one
        bit of the boot settings that is its own. This is a header and not a
        part of spark.c because the program that asks is never the kernel: the
        moonwater command, the monitor and the machine process include
        spark.c and read these too, and the kernel's side of the desktop in
        src/canvas/canvas.c includes this one alone. Nothing here is more
        than a number, a layout and a name, so it compiles in both and
        includes nothing.

        The request numbers are spark.c's to hand out, and its list says
        which of them are here.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/
#ifndef CANVAS_ABI_INCLUDED
#define CANVAS_ABI_INCLUDED

/*
        What the input handlers cost, and when Canvas keeps out of the way.

        Every report of every device goes through every handler attached to
        it, so whatever a handler does with one it does for each of them, for
        as long as the machine is up. These are the numbers `moonwater
        latency` shows for the two that are Moonwater's: Canvas's, which
        hears the two chords that end a program or Canvas, and the machine's
        key watch, which fires the bound events. Events are every report counted; one in
        sixteen is timed, so the time is a sample and worst is the worst
        sample. Reading needs nothing, reset needs CAP_SYS_ADMIN and zeroes
        the counts: how much a handler costs over a minute of moving a mouse
        is a reset, the minute and a read.

        yielded is 1 while another program is master of a card Canvas draws
        on. quiet is the reports Canvas's handler had nothing to do with,
        which is every one but the modifiers and Backspace: the compositor
        reads the same devices itself, and a handler that woke a thread for
        each report beside it was a wakeup for every one.
*/
struct latency_cost {
        unsigned long events;   // reports the handler was called with
        unsigned long samples;  // the ones that were timed
        unsigned long total_ns; // their time, added
        unsigned long worst_ns; // the longest of them
};

#define SPARK_LATENCY_READ 0u
#define SPARK_LATENCY_RESET 1u

struct latency_stats {
        unsigned int request;      // SPARK_LATENCY_*
        unsigned int yielded;      // 1 while another program has the display
        unsigned int retired_hold;  // not filled in since the kernel's own desktop went
        unsigned int retired_holds; // likewise
        unsigned int thread_passes; // times the canvas thread woke
        unsigned int retired_ticks; // likewise
        struct latency_cost canvas; // Canvas's input handler
        struct latency_cost bind;   // the machine's key watch
        unsigned long quiet;        // of the canvas events, those it had nothing to do with
        unsigned long elapsed_ns;   // since the counts were last reset, or boot
        unsigned long reserved;
};

_Static_assert(sizeof(struct latency_cost) == 32, "spark latency cost ABI");
_Static_assert(sizeof(struct latency_stats) == 112, "spark latency ABI");

// _IOWR('s', 8, struct latency_stats)
#define SPARK_IOCTL_LATENCY 0xc0707308u

/*
        Canvas, on and off.

        Off gives the display back: the input handler and the thread stop,
        the DRM clients are released, every console gets its keyboard back,
        and the kernel's framebuffer console takes each screen. On takes the
        cards again and fires canvas on, and the machine script starts the
        compositor on that event. It refuses while another program is master
        of a card, and names that program.

        The state below comes back whatever the request answers. Reading it
        needs nothing; on and off need CAP_SYS_ADMIN.

        SET_SCALE and SET_MODES say how the desktop is drawn, and need
        CAP_SYS_ADMIN. They are kept when Canvas is off and taken when it
        starts, so the answer is 0 either way; what is asked is in
        scale_setting and modes, and the state that comes back says what is
        in force. A compositor reads the scale when it starts, and a mode is
        taken at the card's next hotplug. A scale outside 1 to
        SPARK_CANVAS_SCALE_MOST is -EINVAL.

        3, 4 and 5 were the keymap, the kernel log window and a terminal of
        the kernel's own desktop. Nothing answers them, and they stay
        numbers nothing is given: they are -EINVAL.
*/
#define SPARK_CANVAS_STATUS 0u
#define SPARK_CANVAS_ON 1u
#define SPARK_CANVAS_OFF 2u
#define SPARK_CANVAS_SET_SCALE 6u
#define SPARK_CANVAS_SET_MODES 7u
#define SPARK_CANVAS_OUTPUTS 4u

// How many device pixels one drawn pixel is: 0 asks the screens, a number is
// that number. Where the setting in force came from is the origin.
#define SPARK_CANVAS_SCALE_AUTO 0u
#define SPARK_CANVAS_SCALE_MOST 4u
#define SPARK_CANVAS_ORIGIN_DEFAULT 0u
#define SPARK_CANVAS_ORIGIN_COMMAND_LINE 1u
#define SPARK_CANVAS_ORIGIN_SET 2u

// Which mode a screen is driven at: the largest it lists, or the one it marks
// preferred.
#define SPARK_CANVAS_MODES_LARGEST 0u
#define SPARK_CANVAS_MODES_PREFERRED 1u

struct canvas_output_state {
        char connector[16];
        unsigned int width, height, refresh;
        unsigned int reserved;
};

struct canvas_control {
        unsigned int request;      // SPARK_CANVAS_*
        unsigned int running;      // 1 while Canvas holds a card
        unsigned int cards;        // cards Canvas holds
        unsigned int retired_windows;  // not filled in since the kernel's own desktop went
        unsigned int retired_detached; // likewise
        unsigned int suspended;    // 1 while another program is a card's master
        int master_pid;            // who held a card on refused, 0 for nobody
        unsigned int output_count; // outputs below, at most SPARK_CANVAS_OUTPUTS
        char master_command[16];
        char driver[16];           // the first card's driver
        struct canvas_output_state output[SPARK_CANVAS_OUTPUTS];
        unsigned int retired_hold;  // likewise
        unsigned int retired_holds; // likewise
        unsigned int thread_passes; // times the canvas thread woke
        unsigned int retired_ticks; // likewise
        unsigned int scale;         // device pixels one drawn pixel is, now
        unsigned int scale_setting; // SPARK_CANVAS_SCALE_AUTO or the number asked
        unsigned int scale_dpi;     // what auto went by, 0 when no screen could say
        unsigned int scale_origin;  // SPARK_CANVAS_ORIGIN_*
        unsigned int modes;         // SPARK_CANVAS_MODES_*
        unsigned int reserved;
};

_Static_assert(sizeof(struct canvas_output_state) == 32, "spark canvas output ABI");
_Static_assert(sizeof(struct canvas_control) == 232, "spark canvas control ABI");

// _IOWR('s', 10, struct canvas_control)
#define SPARK_IOCTL_CANVAS 0xc0e8730au

// _IO('s', 17) -- the compositor's window table given to the device: the
// argument its descriptor, -1 none; root's only
#define WINDOWS_IOCTL_TABLE_SET 0x00007311u

// _IO('s', 18) -- a descriptor of the window table, close-on-exec; ENOENT
// when no compositor has given one
#define WINDOWS_IOCTL_TABLE_GET 0x00007312u

/* Each request's size field is the size of its struct, or the kernel copies
   the wrong number of bytes and answers as if it had not. */
#define SPARK_IOCTL_SIZE(request) (((request) >> 16) & 0x3fffu)
_Static_assert(SPARK_IOCTL_SIZE(SPARK_IOCTL_LATENCY) == sizeof(struct latency_stats), "spark ioctl size");
_Static_assert(SPARK_IOCTL_SIZE(SPARK_IOCTL_CANVAS) == sizeof(struct canvas_control), "spark ioctl size");

// Bindings, which are spark.c's table: the events a desktop fires when it
// starts and when it stops, in the table's order and under its names.
#define SPARK_BIND_CANVAS_ON 12u
#define SPARK_BIND_CANVAS_OFF 13u
#define SPARK_BIND_CANVAS_ON_NAME "canvas on"
#define SPARK_BIND_CANVAS_OFF_NAME "canvas off"

// In the boot settings' flags, whose other bits are spark.c's.
#define SPARK_SETTINGS_CANVAS_OFF 0x1u // Canvas does not start at boot

#endif // CANVAS_ABI_INCLUDED
