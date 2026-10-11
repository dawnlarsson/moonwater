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

/*
        Surfaces, protocol version 2: what a program asks of the compositor
        that runs in userspace (src/canvas/compositor.c), which holds the card
        while it runs and hands it back to Canvas when it stops. The page a
        surface is drawn into is laid out in window.c, beside version 1.

        The way there is a socket and not /dev/spark. A program makes its page
        itself, as a sealed memfd it cannot shrink under the reader, and hands
        it over with SCM_RIGHTS in its create message; the compositor checks
        the size and the seals before it maps a byte, writes its half of the
        header and answers. A commit is a message, so the compositor sleeps
        in poll until there is one, and an event it writes into the page's
        ring is followed by a message that wakes the program the same way.
        Closing the socket is closing the surface: one connection, one
        surface, as one file is one window in version 1.

        Every message both ways is one struct surface_message on a
        SOCK_SEQPACKET connection. Only CREATE carries a descriptor.
*/
#define SURFACE_SOCKET "/run/moonwater/surface"
#define SURFACE_PROTOCOL 2u
#define SURFACE_MAGIC 0x3253574du // 'MWS2'
#define SURFACE_HEADER 2916u
#define SURFACE_FORMAT_XR24 0x34325258u

// struct surface_message kind. CREATE is answered by CREATED, COMMIT by
// nothing; WAKE says the page's event ring has something.
//
// ACTIVATE asks the compositor to restore, raise and focus the surface
// whose id is request.surface_id: only the surface that holds the panel role
// may ask, and it is answered by nothing. GRANT is the one message a root
// program sends without a surface: it says that the process request.parent
// (a pid) may claim the role request.role, with the flags in request.flags
// (SURFACE_GRANT_*), and is answered by GRANTED with a status. A grant
// lasts as long as the process does and no longer: a pid that was reused
// finds none. Only a peer whose uid is 0 is heard, and the program never
// grants itself anything it was not given.
#define SURFACE_CREATE 1u
#define SURFACE_COMMIT 2u
#define SURFACE_CREATED 3u
#define SURFACE_WAKE 4u
#define SURFACE_ACTIVATE 5u
#define SURFACE_GRANT 6u
#define SURFACE_GRANTED 7u

// struct surface_message GRANT's flags
#define SURFACE_GRANT_REPLACE 1u // the holder of the role may be replaced by it
#define SURFACE_GRANT_REVOKE 2u  // takes the grant back, and the role with it

// Roles (canvas spec 02). Values are never renumbered.
#define SURFACE_ROLE_APP 0u
#define SURFACE_ROLE_BACKGROUND 1u
#define SURFACE_ROLE_PANEL 2u
#define SURFACE_ROLE_LAUNCHER 3u
#define SURFACE_ROLE_WINDOW_LIST 4u
#define SURFACE_ROLE_TRAY 5u
#define SURFACE_ROLE_ICONS 6u
#define SURFACE_ROLE_CONTROL 7u
#define SURFACE_ROLE_LOG 8u
#define SURFACE_ROLE_LEGACY 9u

// role_state
#define SURFACE_ROLE_NONE 0u
#define SURFACE_ROLE_PENDING 1u
#define SURFACE_ROLE_ACTIVE 2u
#define SURFACE_ROLE_REFUSED 3u
#define SURFACE_ROLE_REVOKED 4u

// layer
#define SURFACE_LAYER_BACKGROUND 0u
#define SURFACE_LAYER_BOTTOM 1u
#define SURFACE_LAYER_NORMAL 2u
#define SURFACE_LAYER_TOP 3u
#define SURFACE_LAYER_OVERLAY 4u

// lock_applied
#define SURFACE_LOCK_MOVABLE 1u
#define SURFACE_LOCK_RESIZABLE 2u
#define SURFACE_LOCK_NO_FOCUS_CLICK 4u
#define SURFACE_LOCK_NO_KEYBOARD 8u
#define SURFACE_LOCK_PASS_POINTER 16u
#define SURFACE_LOCK_DECORATED 32u

// anchor_applied: the edges a surface is held to
#define SURFACE_ANCHOR_TOP 1u
#define SURFACE_ANCHOR_BOTTOM 2u
#define SURFACE_ANCHOR_LEFT 4u
#define SURFACE_ANCHOR_RIGHT 8u

// struct surface_request flags
#define SURFACE_REQUEST_FOCUS 1u
#define SURFACE_REQUEST_TRANSIENT 2u
#define SURFACE_REQUEST_CHILD 4u

// struct surface_event type
#define SURFACE_EVENT_POINTER_ENTER 1u
#define SURFACE_EVENT_POINTER_LEAVE 2u
#define SURFACE_EVENT_POINTER_MOTION 3u
#define SURFACE_EVENT_POINTER_BUTTON 4u
#define SURFACE_EVENT_POINTER_WHEEL 5u
#define SURFACE_EVENT_KEY 6u
#define SURFACE_EVENT_FOCUS 7u
#define SURFACE_EVENT_CONFIGURE 8u
#define SURFACE_EVENT_CLOSE 9u
#define SURFACE_EVENT_ROLE 10u
#define SURFACE_EVENT_RELEASE 11u
#define SURFACE_EVENT_STYLE 12u
#define SURFACE_EVENT_LIST 13u    // serial: the window list in the page changed
#define SURFACE_EVENT_REFUSED 14u // a: SURFACE_REFUSED_*, b: the negative errno

// SURFACE_EVENT_REFUSED a: what a program asked of a surface that is locked.
// The page holds the position and size it has; this says it was not given
// the one it asked for.
#define SURFACE_REFUSED_MOVE 1u
#define SURFACE_REFUSED_RESIZE 2u

// The window list a panel is sent, in its page past the header (window.c).
#define SURFACE_LISTING_MAX 32u
#define SURFACE_LISTING_TITLE 28u
#define SURFACE_LIST_FOCUSED 1u
#define SURFACE_LIST_MINIMIZED 2u

// A create the compositor refuses for its size, because the role fixes it
// (a panel is as wide as the output and as high as its zone), is answered
// SURFACE_ESIZE with the size it wants in the reply's request.width and
// height; the program makes its page that size and asks again.
#define SURFACE_ESIZE (-90)

// SURFACE_EVENT_KEY flags; POINTER_BUTTON uses SURFACE_KEY_DOWN alone.
#define SURFACE_KEY_DOWN 1u
#define SURFACE_KEY_REPEAT 2u
#define SURFACE_KEY_SHIFT 4u
#define SURFACE_KEY_CONTROL 8u
#define SURFACE_KEY_ALT 16u
#define SURFACE_KEY_ALTGR 32u
#define SURFACE_KEY_SUPER 64u

// SURFACE_EVENT_CLOSE reason
#define SURFACE_CLOSE_REQUESTED 0u
#define SURFACE_CLOSE_REPLACED 1u
#define SURFACE_CLOSE_REFUSED 2u
#define SURFACE_CLOSE_PARENT 3u

struct surface_request {
        unsigned int size;         // sizeof(struct surface_request)
        unsigned int protocol;     // in: 2 or 0; out: the agreed protocol
        unsigned int role;         // SURFACE_ROLE_*
        unsigned int width, height;
        unsigned int buffer_slots; // 1 or 2
        unsigned int flags;        // SURFACE_REQUEST_*
        unsigned int parent;       // surface_id, with SURFACE_REQUEST_CHILD
        int rel_x, rel_y;
        unsigned int anchor;
        int exclusive;
        unsigned int surface_id;   // out
        unsigned int bytes;        // out: what the page has to hold
};

struct surface_message {
        unsigned int kind;  // SURFACE_CREATE ...
        int status;         // CREATED: 0, or a negative errno
        struct surface_request request;
};

_Static_assert(sizeof(struct surface_request) == 56, "surface request ABI");
_Static_assert(sizeof(struct surface_message) == 64, "surface message ABI");

// The request's fields at their numbers (canvas spec 01 section 5.1), and the
// message around it: a field that moves is a protocol that is not 2.
#define SURFACE_REQUEST_AT(field, offset)                                      \
        _Static_assert(__builtin_offsetof(struct surface_request, field) == (offset), \
                       "surface request ABI " #field)
SURFACE_REQUEST_AT(size, 0);
SURFACE_REQUEST_AT(protocol, 4);
SURFACE_REQUEST_AT(role, 8);
SURFACE_REQUEST_AT(width, 12);
SURFACE_REQUEST_AT(height, 16);
SURFACE_REQUEST_AT(buffer_slots, 20);
SURFACE_REQUEST_AT(flags, 24);
SURFACE_REQUEST_AT(parent, 28);
SURFACE_REQUEST_AT(rel_x, 32);
SURFACE_REQUEST_AT(rel_y, 36);
SURFACE_REQUEST_AT(anchor, 40);
SURFACE_REQUEST_AT(exclusive, 44);
SURFACE_REQUEST_AT(surface_id, 48);
SURFACE_REQUEST_AT(bytes, 52);
_Static_assert(__builtin_offsetof(struct surface_message, status) == 4, "surface message ABI");
_Static_assert(__builtin_offsetof(struct surface_message, request) == 8, "surface message ABI");

#endif // CANVAS_ABI_INCLUDED
