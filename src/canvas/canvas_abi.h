/*
        Canvas's ABI: what a program that is not Canvas can ask of it.

        The requests /dev/spark takes for the compositor, the diagnostics
        beside them, the two bindings a desktop fires and the one bit of the
        boot settings that is its own. This is a header and not a part of
        spark.c because the program that asks is never the kernel: the
        moonwater command, the monitor and the machine process include
        spark.c and read these too, and the kernel's compositor in
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

/* What the compositor starts once it has a screen. A root-level link to the
   shell image, which the build makes for every applet the SYSTEM category
   holds -- so the name here has to stay one of those, and the image_nodes
   harness is what says so. src/sh/tools.inc is the list. */
#define SPARK_TERMINAL_PROGRAM "/term"

// _IOR('s', 3, struct input_stats). Nanoseconds from a pointer event
// reaching the kernel to the cursor being on screen, and what the
// acceleration curve did with the counts a mouse reported. This, the cursor
// stats and the input devices need CAP_SYS_ADMIN: they are a live record of
// somebody's hands.
#define SPARK_IOCTL_INPUT_STATS 0x80707303u

struct input_stats {
        unsigned long events;
        unsigned long mean_ns;
        unsigned long worst_ns;
        unsigned long queue_ns;
        unsigned long draw_ns;
        unsigned long flush_ns;
        unsigned long counts;     // reported by the device
        unsigned long moved;      // pixels the cursor was moved by them
        unsigned long composes;   // full passes over every output
        unsigned long compose_ns; // spent in them
        unsigned long painted;    // pixels written, all drawing
        unsigned long runs;       // calls into the row primitives
        unsigned long driver_ns;  // of compose_ns, the driver's share
        unsigned long text_ns;    // and the share spent laying out glyphs
};

// _IOR('s', 6, struct cursor_stats). Kept separate from input_stats so the
// existing diagnostic ABI and its encoded structure size remain stable.
#define SPARK_IOCTL_CURSOR_STATS 0x80407306u

struct cursor_stats {
        unsigned long requested_generation; // urgent drag/resize sync request
        unsigned long armed_generation;     // last all-plane completion
        unsigned long updates;               // successful visible plane arms
        unsigned long failures;              // runtime paint/update failures
        int requested_x, requested_y;
        int armed_x, armed_y;       // last all-plane completed request
        unsigned int active;        // outputs retaining a hardware plane
        unsigned int shown;         // active planes currently showing it
        unsigned int wanted;        // outputs containing the logical cursor
        unsigned int recovering;    // a full commit still has to clear a plane
};

// _IOR('s', 7, struct input_devices). Every input device the compositor is
// attached to, whether it opened, and how many reports it has delivered.
// A mouse that is dead until it is plugged in again is either one the
// kernel never heard from or one whose reports went nowhere, and only a
// count per device tells the two apart from a stuck cursor.
#define SPARK_IOCTL_INPUT_DEVICES 0x82487307u
#define INPUT_DEVICES_MAX 8

struct input_device_stats {
        char name[56];
        unsigned long events;  // reports delivered to the compositor
        long opened;           // 0 once open, else the error the last try gave
};

struct input_devices {
        unsigned long count;   // devices attached, listed or not
        struct input_device_stats device[INPUT_DEVICES_MAX];
};

/*
        What the input handlers cost, and when Canvas keeps out of the way.

        Every report of every device goes through every handler attached to
        it, so whatever a handler does with one it does for each of them, for
        as long as the machine is up. These are the numbers `moonwater
        latency` shows for the two that are Moonwater's: Canvas's, which
        moves the cursor and takes the keys, and the machine's key watch,
        which fires the bound events. Events are every report counted; one in
        sixteen is timed, so the time is a sample and worst is the worst
        sample. Reading needs nothing, reset needs CAP_SYS_ADMIN and zeroes
        the counts: how much a handler costs over a minute of moving a mouse
        is a reset, the minute and a read.

        yielded is 1 while another program is master of a card Canvas draws
        on. Canvas's handler then returns at once for everything but the
        keys that make the chord that ends that program: the compositor that
        has the card reads the same devices itself, and a handler that moved
        a cursor nobody sees and woke a thread to say so was a wakeup for each
        report beside it.
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
        unsigned int latency_hold; // 1 while the CPU latency hold is taken
        unsigned int latency_holds;
        unsigned int thread_passes; // times the canvas thread woke
        unsigned int frame_ticks;   // times the frame timer fired
        struct latency_cost canvas; // Canvas's input handler
        struct latency_cost bind;   // the machine's key watch
        unsigned long quiet;        // of the canvas events, those heard yielded
        unsigned long elapsed_ns;   // since the counts were last reset, or boot
        unsigned long reserved;
};

_Static_assert(sizeof(struct latency_cost) == 32, "spark latency cost ABI");
_Static_assert(sizeof(struct latency_stats) == 112, "spark latency ABI");

// _IOWR('s', 8, struct latency_stats)
#define SPARK_IOCTL_LATENCY 0xc0707308u

/*
        Canvas, on and off.

        Off gives the display back: every program's window is asked to close,
        the compositor's input handler and thread stop, its DRM clients are
        released, every console gets its keyboard back, and the kernel's
        framebuffer console takes each screen. On takes the cards again and
        fires canvas on, and opens no window by itself: the machine script
        asks for the kernel log and a terminal on that event. It refuses
        while another program is master of a card, and names that program.

        The state below comes back whatever the request answers. Reading it
        needs nothing; on and off need CAP_SYS_ADMIN. LAYOUT reads the
        compositor keymap without a capability; setting it needs CAP_SYS_ADMIN.
        KERNEL_LOG opens the kernel log window, or leaves the one that is
        open, and TERMINAL starts a terminal as Control-Shift-T does; both
        start a root shell's worth of view or input, so both need
        CAP_SYS_ADMIN, and both answer -ENODEV while Canvas is off.

        SET_SCALE and SET_MODES change how the desktop is drawn, live, and
        need CAP_SYS_ADMIN. They are kept when Canvas is off and taken when
        it starts, so the answer is 0 either way; what is asked is in
        scale_setting and modes, and the state that comes back says what is
        in force. A scale outside 1 to SPARK_CANVAS_SCALE_MOST is -EINVAL.
*/
#define SPARK_CANVAS_STATUS 0u
#define SPARK_CANVAS_ON 1u
#define SPARK_CANVAS_OFF 2u
#define SPARK_CANVAS_LAYOUT 3u
#define SPARK_CANVAS_KERNEL_LOG 4u
#define SPARK_CANVAS_TERMINAL 5u
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
        unsigned int windows;      // programs' windows on the desktop
        unsigned int detached;     // windows off closed that are still open
        unsigned int suspended;    // 1 while another program is a card's master
        int master_pid;            // who held a card on refused, 0 for nobody
        unsigned int output_count; // outputs below, at most SPARK_CANVAS_OUTPUTS
        char master_command[16];
        char driver[16];           // the first card's driver
        struct canvas_output_state output[SPARK_CANVAS_OUTPUTS];
        unsigned int latency_hold;  // 1 while the CPU latency hold is taken
        unsigned int latency_holds; // times it was taken since Canvas started
        unsigned int thread_passes; // times the canvas thread woke
        unsigned int frame_ticks;   // times the frame timer fired
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

// Bindings, which are spark.c's table: the events a desktop fires when it
// starts and when it stops, in the table's order and under its names.
#define SPARK_BIND_CANVAS_ON 12u
#define SPARK_BIND_CANVAS_OFF 13u
#define SPARK_BIND_CANVAS_ON_NAME "canvas on"
#define SPARK_BIND_CANVAS_OFF_NAME "canvas off"

// In the boot settings' flags, whose other bits are spark.c's.
#define SPARK_SETTINGS_CANVAS_OFF 0x1u // Canvas does not start at boot

#endif // CANVAS_ABI_INCLUDED
