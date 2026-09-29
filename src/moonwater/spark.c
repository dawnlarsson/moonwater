/*
        spark binary format

        A flat executable with no relocations, no dynamic linking and no
        section table -- just three regions the kernel maps directly.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater

        File layout, every region a whole number of pages:

                offset          contents                  mapped as
                0               header, then text+rodata  read + execute
                text_size       data                      read + write
                --              bss                       read + write, zeroed

        The header shares the first page with the code rather than occupying a
        page of its own: it is mapped read only along with the text, entry
        simply points past it, and every image is 4096 bytes smaller.

        The image is not position independent: the code carries absolute
        addresses, so it has to land at the base recorded in the header. The
        producer and the loader agree on that base through this header rather
        than through a constant compiled into both.

        Keeping every region page aligned is what lets the loader map each one
        with a single vm_mmap and no partial page fixups: bss is a plain
        anonymous mapping, already zero, with nothing to clear by hand.
*/

#ifndef SPARK_INCLUDED
#define SPARK_INCLUDED

#include "../platform/spark.inc"

// "SPRK", little endian
#define SPARK_MAGIC 0x4b525053u

#define SPARK_VERSION 1
#define SPARK_PAGE 4096

// Anything below this in the file cannot be a valid image. The kernel
// pre-reads BINPRM_BUF_SIZE (256) bytes for us, so the header must fit there.
#define SPARK_HEADER_SIZE 64

// An arbitrary ceiling, well past anything a flat binary should be, that keeps
// the loader's arithmetic on file supplied sizes far from overflowing.
#define SPARK_MAX_IMAGE (256UL << 20)

/*
        Kernel-to-runtime startup handoff.

        A Spark image does not need to interrogate hardware the kernel already
        brought up and classified.  The loader places this magic in one
        callee-saved entry register and the feature word in the next one.
        Ordinary ELF execution and stock kernels do not promise either value,
        so _start falls back to its own detector unless both sides speak this
        exact contract.
*/

struct header {
        unsigned int magic;    // SPARK_MAGIC
        unsigned short version;// SPARK_VERSION
        unsigned short flags;  // reserved, must be 0
        unsigned long base;    // virtual address the text region maps at
        unsigned long entry;   // first instruction, absolute
        unsigned long text_size; // page multiple, read + execute
        unsigned long data_size; // page multiple, read + write
        unsigned long bss_size;  // page multiple, read + write, zero filled
        unsigned long reserved[2]; // pads the header to exactly SPARK_HEADER_SIZE
};

// The loader reads the header out of the kernel's pre-read buffer and the
// producer writes it byte by byte, so the two only agree while this holds.
_Static_assert(sizeof(struct header) == SPARK_HEADER_SIZE,
               "spark header must be exactly SPARK_HEADER_SIZE bytes");


/*
        Spawning

        fork builds a complete copy of the caller -- address space, page
        tables, file table -- and exec then throws the address space half away
        microseconds later. For spawning a fresh program none of that copy is
        ever read. Measured on this kernel it costs about 3us of the ~10.5us a
        fork+exec spawn takes.

        A spawn that creates the task with no address space to copy skips both
        the duplication and the teardown. /dev/spark exposes that: write a
        request, get back a pid you can wait on exactly like a forked child.

        This is a device rather than a syscall on purpose. A syscall would mean
        patching arch/x86/entry/syscalls/syscall_64.tbl in the kernel tree,
        which this repo downloads rather than tracks, so it would become a
        patch to re-apply on every kernel bump.

        argv arrives as one flat block of NUL terminated strings so the whole
        request copies in with a single copy_from_user:

                "/bin/thing\0-v\0file\0"   argv_count = 3
*/

#define SPARK_DEVICE "/dev/spark"

// misc major, with a fixed minor from the range reserved for local use, so the
// node can be created statically in the initramfs without devtmpfs.
#define SPARK_DEVICE_MAJOR 10
#define SPARK_DEVICE_MINOR 250

// _IOW('s', 1, struct spawn) -- spelled out so userspace does not need
// the kernel ioctl macros to talk to it. The size is part of the encoding, so
// this changes if the request struct does. Generation-keyed environments make
// repeated launches on one open descriptor a pointer handoff after the first.
//
// One opcode carries every launch. What used to be five differed only in two
// independent booleans and how many descriptors trailed the request, so the
// kernel decoded a cross product of them from the opcode number and copied
// the descriptors in a second time at an offset that depended on which one
// arrived. Both now live in the request itself, where a caller can set them
// in any combination and the loader reads them in the one copy it already
// does.
#define SPARK_IOCTL_SPAWN 0x40407301u

// The shell's ENOEXEC rule: executable text without a #! line is handed to
// /bin/sh. Off by default so a raw spawn stays an exact execve-like interface
// and callers that do not want shell interpretation cannot acquire it by
// accident.
#define SPARK_SPAWN_SHELL 0x1u

// Take argv[0] as a utility in the immutable system /shell image rather than
// as a path to open.
#define SPARK_SPAWN_TOOL 0x2u

/*
        Which ignored signals the program keeps.

        execve carries an ignored signal across, and a spawn gives every one
        its default back, because the shell asking ignores interrupt and quit
        for its own sake, and the stop signals too under job control, and
        passing that on would leave nothing it runs able to be cancelled or
        stopped. But an ignore can also be somebody's choice that the program
        is meant to inherit: trap '' in a script, nohup's hangup, whatever the
        shell was itself started ignoring, an asynchronous list's interrupt.
        Only the caller knows which is which, so it names what stays ignored;
        a signal not named goes back to its default, and a request naming
        none is the reset of every ignore it always was.
*/
#define SPARK_SPAWN_KEEP_INTERRUPT 0x4u
#define SPARK_SPAWN_KEEP_QUIT 0x8u
#define SPARK_SPAWN_KEEP_STOP_KEY 0x10u
#define SPARK_SPAWN_KEEP_TTY_INPUT 0x20u
#define SPARK_SPAWN_KEEP_TTY_OUTPUT 0x40u
// Every ignored signal but those five.
#define SPARK_SPAWN_KEEP_IGNORED 0x80u

#define SPARK_SPAWN_FLAGS (SPARK_SPAWN_SHELL | SPARK_SPAWN_TOOL |                \
                           SPARK_SPAWN_KEEP_INTERRUPT | SPARK_SPAWN_KEEP_QUIT |  \
                           SPARK_SPAWN_KEEP_STOP_KEY |                           \
                           SPARK_SPAWN_KEEP_TTY_INPUT |                          \
                           SPARK_SPAWN_KEEP_TTY_OUTPUT |                         \
                           SPARK_SPAWN_KEEP_IGNORED)

/* What SPARK_SPAWN_TOOL runs. Part of the flag's meaning rather than a path
   the kernel picks, so it is written here beside it and nowhere else. */
#define SPARK_TOOL_PROGRAM "/shell"

/* What the compositor starts once it has a screen. A root-level link to the
   shell image, which the build makes for every applet the SYSTEM category
   holds -- so the name here has to stay one of those, and the image_nodes
   harness is what says so. src/sh/tools.inc is the list. */
#define SPARK_TERMINAL_PROGRAM "/term"

// _IOR('s', 2, struct stats). Nanoseconds accumulated inside the kernel,
// so the split between creating the task and loading the image is measured
// where it happens rather than inferred from the outside.
#define SPARK_IOCTL_STATS 0x80307302u

struct stats {
        unsigned long spawns;
        unsigned long task_ns; // time inside user_mode_thread
        unsigned long exec_ns;   // time inside kernel_execve
        unsigned long loader_ns; // time inside the spark binfmt handler
        unsigned long loads;     // binfmt invocations, which exceed spawns:
                                 // a spark image run by ordinary exec loads
                                 // too, without going through the device
        unsigned long map_ns;    // of the handler, just the region mapping --
                                 // the part that is actually ours to optimise
};

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
        One versioned view of the kernel data read by system utilities.

        The caller chooses sections, owns the output buffer and receives
        offsets rather than pointers.  That keeps the ABI relocatable and
        lets one ioctl replace a forest of open/read/parse/close cycles.  A
        stock kernel can produce the exact same records from procfs, which is
        important: acceleration must not change namespace or visibility
        semantics merely because /dev/spark exists.

        Counters use their native, lossless units.  CPU and process time is
        nanoseconds, memory and network values are bytes, and load is fixed at
        two decimal places.  Every snapshot also carries the three common
        clocks, regardless of its sections; zero sections requests just that
        common metadata.  Consumers decide presentation.
*/
#define SPARK_SNAPSHOT_VERSION 1u

#define SPARK_SNAPSHOT_SYSTEM  0x01u
#define SPARK_SNAPSHOT_CPU     0x02u
#define SPARK_SNAPSHOT_NETWORK 0x04u
#define SPARK_SNAPSHOT_PROCESS 0x08u
#define SPARK_SNAPSHOT_KERNEL  (SPARK_SNAPSHOT_SYSTEM | SPARK_SNAPSHOT_CPU | \
                                SPARK_SNAPSHOT_NETWORK)
#define SPARK_SNAPSHOT_ALL     (SPARK_SNAPSHOT_KERNEL | SPARK_SNAPSHOT_PROCESS)
#define SPARK_SNAPSHOT_MAX_BYTES (16u << 20)

struct snapshot_header {
        unsigned int version;
        unsigned int flags;
        unsigned int bytes;
        unsigned int page_size;
        unsigned long monotonic_ns;
        unsigned long realtime_seconds;
        unsigned long uptime_ns;
        unsigned long memory_total;
        unsigned long memory_available;
        unsigned long swap_total;
        unsigned long swap_free;
        unsigned int load[3];
        unsigned int cpu_offset;
        unsigned int cpu_count;
        unsigned int network_offset;
        unsigned int network_count;
        unsigned int process_offset;
        unsigned int process_count;
        unsigned int reserved;
};

struct snapshot_cpu {
        unsigned int id;       // ~0u is the all-CPU aggregate
        unsigned int reserved;
        unsigned long total_ns;
        unsigned long idle_ns;
};

struct snapshot_network {
        char name[16];
        unsigned long received;
        unsigned long transmitted;
};

struct snapshot_process {
        unsigned int pid;
        unsigned int ppid;
        unsigned int pgrp;
        unsigned int session;
        int tty;
        int tpgid;
        int nice;
        unsigned int threads;
        unsigned long user_ns;
        unsigned long system_ns;
        unsigned long start_ns;
        unsigned long virtual_bytes;
        unsigned long resident_bytes;
        unsigned int uid;
        unsigned int state;
        char command[16];
};

struct snapshot_request {
        unsigned long buffer;
        unsigned int capacity;
        unsigned int flags;
        unsigned int version;
        unsigned int used;
        unsigned int required;
        unsigned int reserved;
};

_Static_assert(sizeof(struct snapshot_header) == 112,
               "spark snapshot header ABI");
_Static_assert(sizeof(struct snapshot_cpu) == 24,
               "spark snapshot CPU ABI");
_Static_assert(sizeof(struct snapshot_network) == 32,
               "spark snapshot network ABI");
_Static_assert(sizeof(struct snapshot_process) == 96,
               "spark snapshot process ABI");
_Static_assert(sizeof(struct snapshot_request) == 32,
               "spark snapshot request ABI");

// _IOWR('s', 9, struct snapshot_request)
#define SPARK_IOCTL_SNAPSHOT 0xc0207309u

/*
        The request numbers, in one place, because more than one change adds
        them at once: 1 spawn, 2 stats, 3 input stats, 4 window create
        (window.c), 5 window commit (window.c), 6 cursor stats, 7 input
        devices, 9 snapshot, 10 Canvas on and off, 11 bindings, 12 and 13
        reading and setting the boot settings. 8 was never used and stays
        that way. 14 and 15 are Moonwater's machine process and script;
        they are defined in moonwater.c so Spark stays the image and spawn
        device. The next request takes the next free number past the
        highest, 16 at the time of writing, and a gap is never filled: an
        old program sending an old number must never reach a new request
        that happens to share it.
*/

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
*/
#define SPARK_CANVAS_STATUS 0u
#define SPARK_CANVAS_ON 1u
#define SPARK_CANVAS_OFF 2u
#define SPARK_CANVAS_LAYOUT 3u
#define SPARK_CANVAS_KERNEL_LOG 4u
#define SPARK_CANVAS_TERMINAL 5u
#define SPARK_CANVAS_OUTPUTS 4u

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
};

_Static_assert(sizeof(struct canvas_output_state) == 32, "spark canvas output ABI");
_Static_assert(sizeof(struct canvas_control) == 192, "spark canvas control ABI");

// _IOWR('s', 10, struct canvas_control)
#define SPARK_IOCTL_CANVAS 0xc0c0730au

/*
        Bindings: what the machine's own events run.

        Every event has a name and an id, and an id is never given to another
        event, so an old program asking for one can never reach a new one.
        A bound event runs its line the way `/shell -c` does, as root with
        every capability -- unless a machine-script function or case arm owns
        that event, in which case it is queued into the attached process.
        `function moonwater_poweroff` is the hard binding; `moonwater_event`
        still runs for every event when that hook exists. The line lasts until
        the machine stops; keeping it
        across a boot belongs to whoever sets it at boot. An empty line puts
        back the event's default, which for most events is nothing.

        poweroff, reset and ctrl_alt_delete default to poweroff, reboot and
        reboot. canvas on and canvas off run when the desktop starts (the
        first card at boot, or `moonwater canvas on`) and when it stops,
        not instead of `moonwater canvas on|off`. init and exit are lists
        in the settings block, not rows here.

        Reading needs nothing. Setting needs CAP_SYS_ADMIN, because the line
        runs as root, and CAP_SYS_BOOT as well for the events flagged
        SPARK_BIND_BOOT, which stop, sleep or restart the machine: a process
        allowed only to stop the machine must not choose what runs.

        A machine-script function `moonwater_<event>`, or a literal case arm
        in `moonwater_event`, owns that event while the kernel holds the
        script: bind_fire queues into the attached process instead of
        spawning the image line, and `moonwater bind` prints the script line
        rather than SET. `moonwater_event` itself, when present, is queued
        every event; `*)` is not ownership. Events the overlay does not name
        still use the image binds. SET of the image line is still allowed (the
        fallback if the machine process is not attached); the CLI refuses to
        change an owned event so the two copies cannot drift from the keyboard.

        The 312-byte request is a different ioctl from the 272-byte power
        button that used this number; a stale caller gets ENOTTY.
*/
#define SPARK_BIND_COMMAND_MAX 256u
#define SPARK_BIND_NAME_MAX 24u

#define SPARK_BIND_GET 0u
#define SPARK_BIND_SET 1u

#define SPARK_BIND_DEFAULT 0x1u // the line is the event's default
#define SPARK_BIND_RUNNING 0x2u // a run is in flight
#define SPARK_BIND_PENDING 0x4u // a run is queued
#define SPARK_BIND_BOOT 0x8u    // setting it needs CAP_SYS_BOOT as well

#define SPARK_BIND_POWEROFF 1u
#define SPARK_BIND_SLEEP 2u
#define SPARK_BIND_RESET 3u
#define SPARK_BIND_CTRL_ALT_DELETE 4u
#define SPARK_BIND_LID_CLOSE 5u
#define SPARK_BIND_LID_OPEN 6u
#define SPARK_BIND_VOLUME_UP 7u
#define SPARK_BIND_VOLUME_DOWN 8u
#define SPARK_BIND_MUTE 9u
#define SPARK_BIND_BRIGHTNESS_UP 10u
#define SPARK_BIND_BRIGHTNESS_DOWN 11u
#define SPARK_BIND_CANVAS_ON 12u
#define SPARK_BIND_CANVAS_OFF 13u
#define SPARK_BIND_MICMUTE 14u
#define SPARK_BIND_RFKILL 15u
#define SPARK_BIND_TABLET_ON 16u
#define SPARK_BIND_TABLET_OFF 17u
#define SPARK_BIND_HEADPHONE_ON 18u
#define SPARK_BIND_HEADPHONE_OFF 19u
#define SPARK_BIND_DOCK_ON 20u
#define SPARK_BIND_DOCK_OFF 21u
#define SPARK_BIND_RESUME 22u
#define SPARK_BIND_EVENTS 22u

/*
        Names in event-id order, the same strings the kernel table uses.
        Userspace matches a typed word against this rather than opening
        /dev/spark once per event. An id is never reused; new events
        append. Two-word names (canvas on) are `moonwater bind canvas on`.
*/
static const char spark_bind_event_name[SPARK_BIND_EVENTS][SPARK_BIND_NAME_MAX] = {
        "poweroff",
        "sleep",
        "reset",
        "ctrl_alt_delete",
        "lid_close",
        "lid_open",
        "volume_up",
        "volume_down",
        "mute",
        "brightness_up",
        "brightness_down",
        "canvas on",
        "canvas off",
        "micmute",
        "rfkill",
        "tablet on",
        "tablet off",
        "headphone on",
        "headphone off",
        "dock on",
        "dock off",
        "resume",
};

static const unsigned char spark_bind_stop[SPARK_BIND_EVENTS] = {
        [SPARK_BIND_POWEROFF - 1] = 1,
        [SPARK_BIND_RESET - 1] = 1,
        [SPARK_BIND_CTRL_ALT_DELETE - 1] = 1,
};

static inline int spark_bind_is_stop(unsigned int event)
{
        return event && event <= SPARK_BIND_EVENTS && spark_bind_stop[event - 1];
}

struct bind_control {
        unsigned int op;     // SPARK_BIND_GET, or SPARK_BIND_SET, which stores command first
        unsigned int event;  // 1 to count
        unsigned int runs;   // answered: runs started since boot
        unsigned int flags;  // answered: SPARK_BIND_*
        unsigned int count;  // answered: how many events there are
        unsigned int reserved[3];
        char name[SPARK_BIND_NAME_MAX];       // answered
        char command[SPARK_BIND_COMMAND_MAX]; // NUL terminated; "" sets the default
};

_Static_assert(sizeof(struct bind_control) == 312, "spark bind ABI");

// _IOWR('s', 11, struct bind_control)
#define SPARK_IOCTL_BIND 0xc138730bu

/*
        One launch request.

        stdio names the child's standard descriptors, and a pipeline stage is
        the reason it is here. Every stage of "a | b | c" is a fresh program
        with a pipe on one side and a pipe on the other, and the shell had no
        way to say so: it forked itself once per stage so the child could
        arrange its own descriptors, and paid a page table copy each time for
        an address space the child discards at exec. Naming them in the
        request lets a stage be spawned rather than forked.

        A descriptor of -1 is left alone, so a stage at either end of the
        pipeline keeps the shell's own. Every end the shell still holds is
        inherited by the child as a copy, the same as a fork, so the shell
        opens pipeline pipes close-on-exec: the three named here are installed
        without that flag and survive, and every other copy goes when the
        image loads. A reader that inherited its own write end would wait for
        an end of file that could never arrive.
*/
struct spawn {
        unsigned long path;       // user pointer, NUL terminated
        unsigned long argv;       // user pointer to the flat argv block
        unsigned int argv_bytes;  // size of that block
        unsigned int argv_count;  // number of strings in it
        unsigned long envp;       // same shape as argv; may be 0 for none
        unsigned int envp_bytes;
        unsigned int envp_count;
        unsigned long envp_generation; // 0 copies; equal nonzero values reuse
        unsigned int flags;       // SPARK_SPAWN_*, none of them required
        int stdio[3];             // stdin, stdout, stderr; -1 leaves one alone
};

// Userspace fills this field by field and the loader reads it in one
// copy_from_user, so the two only agree while the size does.
_Static_assert(sizeof(struct spawn) == 64, "spark spawn request ABI");

/*
        Settings kept inside the boot image.

        The image carries a section of its own, .mwset, that nothing
        compresses: two slots of SPARK_SETTINGS_SLOT bytes, each starting on a
        page of its own. A change is written over the older slot only, in
        place, so the file never changes size and a machine that loses power
        part way has one slot torn and the other whole. The torn one fails its
        sum, or still reads as never written, and the other is what boots.

        The EFI stub copies the section into a configuration table before it
        leaves the firmware, the kernel keeps the newest slot that checks, and
        userspace reads and replaces that copy through /dev/spark. Every
        reader goes through spark_settings_check, so a slot nobody wrote, a
        slot somebody tore and a slot somebody made up are told apart the same
        way in the stub's consumer, the compositor and the shell -- and none of
        them can walk past the end of one.

        Layout, native byte order, which is little-endian on every machine
        this builds for:

                0       magic "MWSETTNG"
                8       version, header size, slot size
                16      generation, highest is newest
                24      medium, sixteen random bytes naming the copy
                40      payload length and CRC-32 over header and payload
                48      flags, and the next id each list hands out
                64      entries: list, kind, id, length, then the text,
                        padded to four bytes; in the bind table the id is
                        the event and the text what runs on it

        A slot whose generation, length, sum and flags are all zero was never
        written: it is the image as built, and means the defaults.
*/
#define SPARK_SETTINGS_MAGIC 0x474e54544553574dull
#define SPARK_SETTINGS_VERSION 1
#define SPARK_SETTINGS_HEADER 64
#define SPARK_SETTINGS_SLOT 16384
#define SPARK_SETTINGS_PAYLOAD (SPARK_SETTINGS_SLOT - SPARK_SETTINGS_HEADER)
#define SPARK_SETTINGS_ENTRY 8
#define SPARK_SETTINGS_TEXT_MOST 4096
#define SPARK_SETTINGS_LIST_MOST 16
#define SPARK_SETTINGS_BIND_MOST 48       // events bound at once
#define SPARK_SETTINGS_BIND_TEXT_MOST 255 // a bound command, without its terminator

//      flags: each is the change from the default, so zero is as built.
#define SPARK_SETTINGS_CANVAS_OFF 0x1u   // Canvas does not start at boot
#define SPARK_SETTINGS_MOUNT_OFF 0x2u    // boot keeps the disks' data unmounted
#define SPARK_SETTINGS_STARTUP_SET 0x4u  // the startup list is the one written

//      lists, and the kinds of entry in them.
#define SPARK_SETTINGS_INIT 1
#define SPARK_SETTINGS_STARTUP 2
#define SPARK_SETTINGS_EXIT 3
#define SPARK_SETTINGS_BIND 4 // event id -> command; no entry is the event's default
#define SPARK_SETTINGS_LISTS 4

#define SPARK_SETTINGS_COMMAND 1       // run through /shell -c
#define SPARK_SETTINGS_SHELL 2         // a terminal window with a shell
#define SPARK_SETTINGS_KERNEL_SHELL 3  // the kernel log window

struct spark_settings {
        unsigned long magic;
        unsigned short version;
        unsigned short header;
        unsigned int slot;
        unsigned long generation;
        unsigned char medium[16];
        unsigned int length;
        unsigned int sum;
        unsigned int flags;
        unsigned short next[SPARK_SETTINGS_LISTS - 1]; // init, startup, exit
        unsigned char reserved[6];
        unsigned char payload[SPARK_SETTINGS_PAYLOAD];
};

struct spark_settings_entry {
        unsigned char list;
        unsigned char kind;
        unsigned short id;     // in the bind table, the event
        unsigned short length;
        unsigned short reserved;
};

_Static_assert(sizeof(struct spark_settings) == SPARK_SETTINGS_SLOT,
               "spark settings slot ABI");
_Static_assert(sizeof(struct spark_settings_entry) == SPARK_SETTINGS_ENTRY,
               "spark settings entry ABI");
_Static_assert(__builtin_offsetof(struct spark_settings, sum) == 44 &&
                   __builtin_offsetof(struct spark_settings, payload) ==
                       SPARK_SETTINGS_HEADER,
               "spark settings header ABI");

#define spark_settings_padded(length) (((unsigned long)(length) + 3) & ~3ul)

//      The sum a sealed slot carries: CRC-32 over its header with the sum field
//      read as zero, then exactly length bytes of payload -- lib.c's
//      hash_crc32, which the kernel and the shell both read before this file,
//      chained the way the moonwater command seals a slot.
static inline unsigned int spark_settings_sum(const struct spark_settings *slot)
{
        const unsigned char *bytes = (const unsigned char *)slot;
        unsigned int crc = hash_crc32(~0u, (void *)bytes, 44);

        crc = hash_crc32(crc, (void *)"\0\0\0", 4);
        crc = hash_crc32(crc, (void *)(bytes + 48), SPARK_SETTINGS_HEADER - 48);
        return ~hash_crc32(crc, (void *)slot->payload, slot->length);
}

/*
        0 for a slot never written, 1 for one whose settings check, -1 for
        anything else: another format, a torn write, a length past the slot, a
        sum that disagrees, or entries that do not walk exactly to the end
        within their limits, or an entry naming a kind or an id this version
        does not have.

        Every field of an entry is checked, not only the two the walk needs.
        The walk used to read list and length and step over kind, id and the
        reserved halfword, so a slot carrying kind 200, or a bind row for
        event 4000 when there are twenty-two events, was answered as
        well-formed and handed on. Each consumer bounded it again -- bind_row
        refuses an event outside the table, host_settings_text treats an
        unknown kind as literal text -- but a checker whose whole job is to
        say whether a slot is well-formed should not be saying yes to slots
        that are not. The bind table holds forty-eight rows against
        twenty-two events, so an out-of-range event is expressible here by
        construction rather than by accident.
*/
static inline int spark_settings_check(const struct spark_settings *slot)
{
        unsigned long counts[SPARK_SETTINGS_LISTS + 1] = {0};
        unsigned long at = 0;

        if (slot->magic != SPARK_SETTINGS_MAGIC ||
            slot->version != SPARK_SETTINGS_VERSION ||
            slot->header != SPARK_SETTINGS_HEADER ||
            slot->slot != SPARK_SETTINGS_SLOT)
                return -1;

        if (!slot->generation && !slot->length && !slot->sum && !slot->flags)
                return 0;

        if (slot->length > SPARK_SETTINGS_PAYLOAD ||
            spark_settings_sum(slot) != slot->sum)
                return -1;

        while (at < slot->length)
        {
                const unsigned char *entry = slot->payload + at;
                unsigned int list;
                unsigned int kind;
                unsigned int id;
                unsigned int length;

                if (slot->length - at < SPARK_SETTINGS_ENTRY)
                        return -1;

                list = entry[0];
                kind = entry[1];
                id = entry[2] | (unsigned int)entry[3] << 8;
                length = entry[4] | (unsigned int)entry[5] << 8;
                at += SPARK_SETTINGS_ENTRY;

                if (!list || list > SPARK_SETTINGS_LISTS ||
                    ++counts[list] > (list == SPARK_SETTINGS_BIND
                                          ? SPARK_SETTINGS_BIND_MOST
                                          : SPARK_SETTINGS_LIST_MOST) ||
                    length > (list == SPARK_SETTINGS_BIND
                                  ? SPARK_SETTINGS_BIND_TEXT_MOST
                                  : SPARK_SETTINGS_TEXT_MOST) ||
                    slot->length - at < spark_settings_padded(length))
                        return -1;

                /* The list is known good from here, so the rest of the entry
                   is judged against it: a kind names how the text runs and
                   there are three of them; in the bind table the id is the
                   event, bounded the way the kernel's own table bounds it,
                   and in every other list it is the number that list handed
                   out, which is never zero. The halfword after the length
                   belongs to a later version, and a later version would have
                   said so in the header this already checked. */
                if (!kind || kind > SPARK_SETTINGS_KERNEL_SHELL ||
                    entry[6] || entry[7] || !id ||
                    (list == SPARK_SETTINGS_BIND && id > SPARK_BIND_EVENTS))
                        return -1;

                at += spark_settings_padded(length);
        }

        return 1;
}

/*
        Which of two slots to believe: the newest that checks, a slot never
        written counting as generation zero. -1 when neither does, which reads
        as the defaults.
*/
static inline int spark_settings_newest(const struct spark_settings *slots)
{
        int first = spark_settings_check(slots);
        int second = spark_settings_check(slots + 1);

        if (first < 0)
                return second < 0 ? -1 : 1;
        if (second < 0)
                return 0;

        return slots[1].generation > slots[0].generation ? 1 : 0;
}

/*
        The settings the kernel holds: the newest slot the image booted with,
        or the last one set since. Both requests are root's, because a command
        can carry a secret. GET answers ENODATA when the image handed over
        nothing and nothing was set. SET checks the slot the way boot does and
        replaces the session copy; moonwater boot applies bind rows from the
        image. Nothing here opens or closes Canvas.
*/
struct spark_settings_request {
        unsigned long address; // user pointer to SPARK_SETTINGS_SLOT bytes
        unsigned long flags;   // none defined; nonzero is refused
};

// _IOR('s', 12, struct spark_settings_request)
#define SPARK_IOCTL_SETTINGS_GET 0x8010730cu
// _IOW('s', 13, struct spark_settings_request)
#define SPARK_IOCTL_SETTINGS_SET 0x4010730du

_Static_assert(sizeof(struct spark_settings_request) == 16,
               "spark settings request ABI");

#endif

/*
        Implementing the format above, for the kernel that loads it.

        Everything below is expanded by src/moonwater/core.c a second time, after the
        device context and the compositor it has to be written against, and
        by nothing else: the guard above means the first expansion took only
        the header, the numbers and the structures, which is all a program
        linking against this device ever needs.
*/
#ifdef SPARK_KERNEL

// Callers and spawned workers update these counters concurrently.
static atomic_long_t stat_spawns = ATOMIC_LONG_INIT(0);
static atomic_long_t stat_task_ns = ATOMIC_LONG_INIT(0);
static atomic_long_t stat_exec_ns = ATOMIC_LONG_INIT(0);
static atomic_long_t stat_loader_ns = ATOMIC_LONG_INIT(0);
static atomic_long_t stat_loads = ATOMIC_LONG_INIT(0);
static atomic_long_t stat_map_ns = ATOMIC_LONG_INIT(0);

static int execute_spark(struct linux_binprm *bprm);

/*
        The process id a program is handed to start with, which it keeps as
        who it started as: getpid is task_tgid_vnr, the id in the program's
        own pid namespace, and so is this. It was task_pid_nr, the id in the
        namespace of the machine's init, and the two are the same only there.
        Inside an isolated bowl or under unshare -p the runtime compared the
        namespace's answer with the host's, took every getpid for another
        process, and treated its stream buffers as a parent's it had forked
        away from; and every program in such a namespace was handed the id
        the host knows it by, which it has no other way to learn.
*/
static inline pid_t spark_entry_process(void)
{
        return task_tgid_vnr(current);
}

/*
        The entry facts a loaded program is handed (SPARK_ENTRY_*): read at
        load, when the task is single-threaded and past the point of no
        return, so nothing can change them before the program's first
        instruction.
*/
static unsigned long spark_entry_facts(void)
{
#ifdef CONFIG_SECCOMP
        return SPARK_ENTRY_AUXV |
               (current->seccomp.mode == SECCOMP_MODE_DISABLED
                    ? SPARK_ENTRY_UNFILTERED : 0);
#else
        return SPARK_ENTRY_AUXV | SPARK_ENTRY_UNFILTERED;
#endif
}

#ifdef CONFIG_X86_64
static unsigned long __ro_after_init spark_cpu_features;

/* Cache enabled instruction/state capabilities once, not on every exec.
   xgetbv touches only general registers and runs after the OSXSAVE gate. */
static void __init spark_cpu_features_start(void)
{
        /* xmm state is always saved, so these two need no state gate. */
        if (cpu_feature_enabled(X86_FEATURE_PCLMULQDQ))
                spark_cpu_features |= SPARK_CPU_PCLMUL;
        if (cpu_feature_enabled(X86_FEATURE_AES))
                spark_cpu_features |= SPARK_CPU_AES;
        if (!cpu_feature_enabled(X86_FEATURE_OSXSAVE) ||
            !cpu_feature_enabled(X86_FEATURE_AVX))
                return;

        u64 state = xgetbv(XCR_XFEATURE_ENABLED_MASK);
        if ((state & 6) != 6)
                return;
        if (cpu_feature_enabled(X86_FEATURE_VPCLMULQDQ))
                spark_cpu_features |= SPARK_CPU_VPCLMUL;
        if (cpu_feature_enabled(X86_FEATURE_VAES))
                spark_cpu_features |= SPARK_CPU_VAES;
        if (cpu_feature_enabled(X86_FEATURE_FMA))
                spark_cpu_features |= SPARK_CPU_FMA;
        if (!cpu_feature_enabled(X86_FEATURE_AVX2))
                return;
        spark_cpu_features |= SPARK_CPU_AVX2;
        if ((state & 0xe6) == 0xe6 &&
            cpu_feature_enabled(X86_FEATURE_AVX512F) &&
            cpu_feature_enabled(X86_FEATURE_AVX512BW) &&
            cpu_feature_enabled(X86_FEATURE_AVX512VL))
        {
                spark_cpu_features |= SPARK_CPU_AVX512;
                if (cpu_feature_enabled(X86_FEATURE_AVX512VBMI))
                        spark_cpu_features |= SPARK_CPU_AVX512_VBMI;
        }
}
#endif

#if defined(CONFIG_ARM64)
static unsigned long __ro_after_init spark_cpu_features;

/* The Boolean byte lanes from bit 0, as spark.inc lays them out: PMULL,
   then AES. Both are system capabilities, final once the CPUs are up. */
static void __init spark_cpu_features_start(void)
{
        if (cpu_have_named_feature(PMULL))
                spark_cpu_features |= SPARK_CPU_CLMUL_LANE;
        if (cpu_have_named_feature(AES))
                spark_cpu_features |= SPARK_CPU_AES_LANE;
}
#elif defined(CONFIG_RISCV)
/* Zbc, then Zvkned with the vector state this task may use. Asked at each
   exec rather than once, because whether userspace may touch V is a per-task
   control. */
static unsigned long spark_cpu_features_now(void)
{
        unsigned long lanes = 0;

        if (riscv_has_extension_unlikely(RISCV_ISA_EXT_ZBC))
                lanes |= SPARK_CPU_CLMUL_LANE;
        if (has_vector() && riscv_v_vstate_ctrl_user_allowed() &&
            riscv_has_extension_unlikely(RISCV_ISA_EXT_ZVKNED))
                lanes |= SPARK_CPU_AES_LANE;
        return lanes;
}
#endif

/*
        The top of the address space belongs to the stack, and an image may
        not be mapped into it. Generous on purpose: this only has to be larger
        than any stack setup_arg_pages will build, and being larger costs an
        image nothing -- there is no machine where a flat binary wants to live
        within a gigabyte of STACK_TOP.
*/
#define SPARK_STACK_ROOM (1UL << 30)

/*
        And the slide the stack takes below STACK_TOP when the address space
        is randomized: randomize_stack_top moves it down by up to a mask of
        pages that is 22 bits on x86-64, the widest of the three, which is
        sixteen GiB. Where the stack may be is not known until the old mm is
        gone, so an image is kept clear of all of it.
*/
#define SPARK_STACK_SLIDE (1UL << 34)

static struct linux_binfmt format = {
    .module = THIS_MODULE,
    .load_binary = execute_spark,
};

/*
        The vector a program finds its arguments through.

        setup_arg_pages copies the strings onto the new stack and leaves
        bprm->p pointing at the first of them, but that is all it does. What
        was missing is the thing create_elf_tables builds for an ELF: below the
        strings, a count, then a pointer to each argument, a null, then a
        pointer to each environment entry, and another null. Without it the
        stack pointer a program starts on points at raw text, so reading the
        count read the first eight bytes of its own path -- which is why every
        main() here took no arguments and the shell had nowhere to send what it
        had parsed.

        And a short auxiliary vector after the environment's null, the way
        create_elf_tables ends it: AT_SYSINFO_EHDR, where the vDSO went, when
        one was mapped, AT_PAGESZ, and AT_NULL. A Spark image has no
        interpreter to inform, but the runtime reads the clock through the
        vDSO and has to be told where that is; without it every clock read
        in every Spark program was a trap (3250 in one 200 MB HTTPS
        download in a KVM guest). The same pairs go into saved_auxv, so
        /proc/PID/auxv says what the stack says, and snapshot.c's page size
        reads AT_PAGESZ from it. _start trusts the words only under
        SPARK_ENTRY_AUXV, because an older loader left the strings there.

        And where the strings end, which create_elf_tables also says and this
        did not: setup_arg_pages leaves arg_start, and arg_end, env_start and
        env_end are what /proc/PID/cmdline and /proc/PID/environ read
        between. Left at zero, every program's command line read as empty --
        ps, top and pgrep -f in a bowl saw nothing, and so did anything here
        that looked a process up by what it was started as.
*/
static int spark_stack(struct linux_binprm *bprm, unsigned long *out)
{
        struct mm_struct *mm = current->mm;
        unsigned long walk = bprm->p;
        unsigned long __user *slot;
        unsigned long bottom;
        int count = bprm->argc + bprm->envc;
        unsigned long vdso = (unsigned long)mm->context.vdso;
        unsigned long auxv[] = {AT_SYSINFO_EHDR, vdso, AT_PAGESZ, PAGE_SIZE,
                                AT_NULL, 0};
        unsigned long *pairs = vdso ? auxv : auxv + 2;
        unsigned long words = vdso ? 6 : 4;
        int i;

        mm->arg_end = mm->env_start = mm->env_end = walk;

        // A count, every pointer, the two nulls that end each list, and the
        // auxiliary vector.
        bottom = (walk - ((unsigned long)(count + 3) + words) * sizeof(unsigned long)) & ~15UL;
        slot = (unsigned long __user *)bottom;

        if (put_user((unsigned long)bprm->argc, slot++))
                return -EFAULT;

        for (i = 0; i < count; i++)
        {
                long length;

                if (i == bprm->argc && put_user(0UL, slot++))
                        return -EFAULT;

                if (put_user(walk, slot++))
                        return -EFAULT;

                length = strnlen_user((void __user *)walk, MAX_ARG_STRLEN);

                if (length <= 0)
                        return -EFAULT;

                walk += (unsigned long)length;

                if (i < bprm->argc)
                        mm->arg_end = mm->env_start = walk;
                mm->env_end = walk;
        }

        // The null after argv when there was no environment to start one.
        if (!bprm->envc && put_user(0UL, slot++))
                return -EFAULT;

        if (put_user(0UL, slot++) ||
            copy_to_user(slot, pairs, words * sizeof(unsigned long)))
                return -EFAULT;
        memcpy(mm->saved_auxv, pairs, words * sizeof(unsigned long));

        *out = bottom;
        return 0;
}

/*
        The machine's own mode, put back to native for this program.

        A process that is 32-bit -- a static i386 binary, an armv7 one, and
        this build has the compatibility layers on -- carries that in its
        thread flags: TIF_ADDR32 on x86-64, which is the address space it
        may use, the size STACK_TOP and TASK_SIZE answer and the mmap layout
        arch_pick_mmap_layout picks, and TIF_32BIT on arm64 and riscv, which
        picks the signal frame, the syscall table and the audit architecture.
        The ELF loader sets them for what it loads, by SET_PERSONALITY, and
        this loader did not: an image exec'd by a 32-bit process was a 64-bit
        program in a 4 GiB address space with the 32-bit mmap layout, and on
        the machine that ran it the shell died at its first look at its own
        argument vector ("no room for arguments"). Before setup_new_exec, as
        the ELF loader does, because that is what reads them.

        The x86 line is the part of set_personality_64bit that a module can
        reach: the function itself is not exported. The syscall number the
        interrupted call left in the registers goes with it, as that
        function says it comes from a 64-bit execve, and the status bit
        that says this is a compat call, which the exit path would clear
        anyway.
*/
static inline void spark_personality(void)
{
#if defined(CONFIG_X86_64)
#ifdef CONFIG_IA32_EMULATION
        clear_thread_flag(TIF_ADDR32);
        current_thread_info()->status &= ~TS_COMPAT;
        task_pt_regs(current)->orig_ax = __NR_execve;
#endif
        // As the ELF loader's x86-64 and arm64 SET_PERSONALITY do: a Spark
        // image is laid out with no executable data, and a caller's
        // READ_IMPLIES_EXEC would make it all so.
        current->personality &= ~READ_IMPLIES_EXEC;
#elif defined(CONFIG_ARM64)
#ifdef CONFIG_COMPAT
        clear_thread_flag(TIF_32BIT);
#endif
        current->personality &= ~READ_IMPLIES_EXEC;
#elif defined(CONFIG_RISCV) && defined(CONFIG_COMPAT)
        clear_thread_flag(TIF_32BIT);
#endif
}

/*
        The address space randomized, as the ELF loader does it.

        begin_new_exec clears PF_RANDOMIZE, because whether a program is
        randomized is its own personality's answer and not the old program's;
        load_elf_binary then sets it again from the personality and
        randomize_va_space, and everything after reads it: the mmap base
        setup_new_exec picks, the stack setup_arg_pages places, the vDSO
        placed from where that stack ended up. This loader never set it, so
        every Spark program -- the shell, and every tool that reads a file, a
        network peer or an archive somebody else wrote -- ran with its stack
        at the top of the address space, its mmap area at a fixed distance
        below the stack and its vDSO next to it, where an image that must be
        based at a fixed address (there are no relocations) had one region
        left that an exploit could not name in advance, and this made it
        two fewer. The image itself stays where its header puts it; that is
        the format's, not this function's.

        Not in a module: randomize_va_space and randomize_stack_top are the
        kernel's own and are not exported. A modular build keeps the stack
        where it was, as it kept the whole ring of a window before the ring
        was held a page at a time.
*/
#if IS_BUILTIN(CONFIG_MOONWATER_CORE)
static inline void spark_randomize(void)
{
        if (!(current->personality & ADDR_NO_RANDOMIZE) &&
            READ_ONCE(randomize_va_space))
                current->flags |= PF_RANDOMIZE;
}

static inline unsigned long spark_stack_top(void)
{
        return randomize_stack_top(STACK_TOP);
}
#else
static inline void spark_randomize(void) {}

static inline unsigned long spark_stack_top(void)
{
        return STACK_TOP;
}
#endif

/* Keeping one epilogue lets the cheap format-rejection gate precede all work;
   GCC shrink wrapping otherwise emits one restore island per validation exit. */
static __attribute__((optimize("no-shrink-wrap-separate")))
int execute_spark(struct linux_binprm *bprm)
{
        u64 loader_started;
        u64 map_started;
        struct pt_regs *regs;
        const struct header *header;
        unsigned long stack_addr, span, address, populate[3] = {0};
        int ret;

        // Everything up to begin_new_exec runs while the old process is still
        // intact, so a file that is not ours must be rejected here: returning
        // -ENOEXEC lets the next handler try, and leaves the caller alive.
        // The kernel has already read the first BINPRM_BUF_SIZE bytes for us.
        header = (const struct header *)bprm->buf;

        if (header->magic != SPARK_MAGIC)
                return -ENOEXEC;

        loader_started = ktime_get_ns();
        regs = task_pt_regs(current);

        if (header->version != SPARK_VERSION)
        {
                pr_alert_ratelimited("[moonwater] " "unsupported spark version %u\n", header->version);
                return -ENOEXEC;
        }

        if (header->flags != 0)
                return -ENOEXEC;

        // Every region is a page multiple by construction, and the entry has
        // to land inside the text it points into. A malformed image must fail
        // here rather than after the mm has been torn down.
        if (header->base == 0 || (header->base & (SPARK_PAGE - 1)))
                return -ENOEXEC;

        if (header->text_size == 0 || (header->text_size & (SPARK_PAGE - 1)))
                return -ENOEXEC;

        if ((header->data_size & (SPARK_PAGE - 1)) || (header->bss_size & (SPARK_PAGE - 1)))
                return -ENOEXEC;

        /*
                Every size below comes from the file, so the arithmetic has to
                assume it is hostile. Adding two of them can wrap, and a wrapped
                sum compares small enough to pass a bound it should have failed.
                Each total is therefore checked against what remains rather than
                being formed first.

                SPARK_MAX_IMAGE is not a real limit on anything: it is far more
                than a flat binary has any business being, and it means the
                sums below cannot come near overflowing.
        */
        if (header->text_size > SPARK_MAX_IMAGE ||
            header->data_size > SPARK_MAX_IMAGE ||
            header->bss_size > SPARK_MAX_IMAGE)
                return -ENOEXEC;

        span = header->text_size + header->data_size + header->bss_size;

        if (span > SPARK_MAX_IMAGE)
                return -ENOEXEC;

        // The whole image has to fit above base without wrapping, and inside
        // the address space the process will actually have.
        if (header->base > TASK_SIZE || span > TASK_SIZE - header->base)
                return -ENOEXEC;

        /*
                And not where the stack is about to be.

                setup_arg_pages builds the stack below, before the three
                regions go up, and those are MAP_FIXED: an image based high
                enough is mapped straight over the stack that was just built
                for it. Text landing there is caught by accident, because
                put_user then faults against a read-only mapping and the task
                dies. Data landing there is not caught at all -- that region
                is writable, so the argument vector is written into the
                program's own data and the program is started on a stack
                pointer inside its own image.

                Every other field is checked against something; this was the
                one piece of geometry taken on trust. Refused here rather than
                after begin_new_exec, because here there is still a caller to
                return -ENOEXEC to.

                A fixed reserve rather than RLIMIT_STACK: the limit is what
                the stack may grow to, and reading it here would tie this
                check to a value the caller chooses. Any initial stack fits
                far inside a gigabyte, and growth past it is the guard gap's
                job rather than this one's.
        */
        if (STACK_TOP > SPARK_STACK_ROOM + SPARK_STACK_SLIDE)
        {
                unsigned long floor = STACK_TOP - SPARK_STACK_ROOM -
                                      SPARK_STACK_SLIDE;

                if (header->base >= floor || span > floor - header->base)
                        return -ENOEXEC;
        }

        if (header->entry < header->base ||
            header->entry - header->base >= header->text_size)
                return -ENOEXEC;

        // What must be present in the file, as opposed to zero filled.
        if (i_size_read(file_inode(bprm->file)) <
            (loff_t)(header->text_size + header->data_size))
                return -ENOEXEC;

        // Past this point the old mm is gone. Nothing below may return a plain
        // error code -- there is no process left to return it to -- so every
        // failure has to kill the task instead.
        ret = begin_new_exec(bprm);
        if (ret)
                return ret;

        spark_personality();
        spark_randomize();

        setup_new_exec(bprm);

        ret = setup_arg_pages(bprm, spark_stack_top(), EXSTACK_DEFAULT);
        if (ret < 0)
        {
                pr_alert_ratelimited("[moonwater] " "setup_arg_pages failed: %d\n", ret);
                goto fatal;
        }

        map_started = ktime_get_ns();

        // vm_mmap takes and drops mmap_lock around every call. This address
        // space was created moments ago and nothing else can see it yet, so
        // the three regions go up under one write lock instead of three
        // acquire/release cycles, using do_mmap directly.
        //
        // do_mmap does not populate; it reports how much wants populating and
        // the caller does it after dropping the lock.
        ret = mmap_write_lock_killable(current->mm);
        if (ret)
                goto fatal;

        const unsigned long sizes[] = {
                header->text_size, header->data_size, header->bss_size
        };
        static const char *const regions[] = {"text", "data", "bss"};
        address = header->base;
        for (unsigned int i = 0; i < array_count(sizes); i++)
        {
                if (!sizes[i])
                        continue;

                // Bss is anonymous and already zero. File regions carry their
                // offset from the image base; all three share one lock scope.
                unsigned long mapped = do_mmap(i == 2 ? NULL : bprm->file,
                    address, sizes[i], PROT_READ | (i ? PROT_WRITE : PROT_EXEC),
                    MAP_PRIVATE | MAP_FIXED | (i == 2 ? MAP_ANONYMOUS : 0),
                    0, i == 2 ? 0 : (address - header->base) >> PAGE_SHIFT,
                    &populate[i], NULL);
                if (IS_ERR_VALUE(mapped))
                {
                        mmap_write_unlock(current->mm);
                        ret = (int)mapped;
                        pr_alert_ratelimited("[moonwater] " "mapping %s failed: %d\n", regions[i], ret);
                        goto fatal;
                }
                address += sizes[i];
        }
        mmap_write_unlock(current->mm);

        // do_mmap reports eager population separately from establishing the
        // mappings. Keep the same walk for all regions, including anonymous bss.
        address = header->base;
        for (unsigned int i = 0; i < array_count(sizes); i++)
        {
                if (populate[i])
                        mm_populate(address, populate[i]);
                address += sizes[i];
        }

        atomic_long_add(ktime_get_ns() - map_started, &stat_map_ns);

        /*
                The vDSO, mapped the way the ELF loader maps it, after the
                image so nothing lands on it and before the stack names it
                in the auxiliary vector: the runtime reads the clock there
                without a trap.

                riscv64 also returns from every signal handler to the vDSO's
                rt_sigreturn and has no sa_restorer to name anything else, so
                an image mapped without one returned from its first handler to
                the trampoline's offset from zero: the shell died after every
                command that had a child. There a failure is fatal. x86_64 and
                arm64 hand the kernel their own trampoline through
                SA_RESTORER, so without a vDSO they only keep the clock trap:
                the auxiliary vector then leaves AT_SYSINFO_EHDR out, as it
                does when x86_64 boots with vdso=0 and nothing is mapped.
        */
        ret = arch_setup_additional_pages(bprm, 0);
        if (ret)
        {
                pr_alert_ratelimited("[moonwater] " "mapping the vDSO failed: %d\n", ret);
                if (IS_ENABLED(CONFIG_RISCV))
                        goto fatal;
        }

        current->mm->start_code = header->base;
        current->mm->end_code = header->base + header->text_size;
        current->mm->start_data = header->base + header->text_size;
        current->mm->end_data = header->base + header->text_size + header->data_size;
        current->mm->brk = current->mm->start_brk =
            header->base + header->text_size + header->data_size + header->bss_size;

        set_binfmt(&format);

        ret = spark_stack(bprm, &stack_addr);

        if (ret)
        {
                pr_alert_ratelimited("[moonwater] " "could not lay out the arguments: %d\n", ret);
                goto fatal;
        }

#ifdef CONFIG_X86_64
        /* CPUID and XGETBV are serialising startup work whose answer the
           kernel already has.  Spark's private entry ABI hands that answer
           to _start; an image run by an older loader simply misses the magic
           and retains its userspace detection fallback. */
        regs->r12 = SPARK_START_MAGIC_FACTS;
        regs->r13 = spark_cpu_features;
        regs->r14 = spark_entry_process();
        regs->r15 = spark_entry_facts();

        regs->ip = header->entry;
        regs->sp = stack_addr;
        regs->flags = 0x202; // IF flag set
        regs->cs = __USER_CS;
        regs->ss = __USER_DS;
#elif defined(CONFIG_ARM64)
        regs->regs[19] = SPARK_START_MAGIC_FACTS;
        regs->regs[20] = spark_cpu_features;
        regs->regs[21] = spark_entry_process();
        regs->regs[22] = spark_entry_facts();
        regs->pc = header->entry;
        regs->sp = stack_addr;
        regs->pstate = PSR_MODE_EL0t;
#elif defined(CONFIG_RISCV)
        /*
                The kernel's own start: sstatus with the floating-point unit
                on and its registers at their initial values, the 64-bit
                user XLEN and no shadow stack. SR_SPIE alone left FS off, and
                the first floating-point instruction a program ran, the
                shell's included, was an illegal instruction and SIGILL.
        */
        start_thread(regs, header->entry, stack_addr);
        regs->s2 = SPARK_START_MAGIC_FACTS;
        regs->s3 = spark_cpu_features_now();
        regs->s4 = spark_entry_process();
        regs->s5 = spark_entry_facts();
#endif

        finalize_exec(bprm);

        // Everything before this in kernel_execve is the generic prologue:
        // allocating a bprm, opening the file, building a throwaway mm to hold
        // argv and then transplanting its stack. This counter is only our part.
        atomic_long_add(ktime_get_ns() - loader_started, &stat_loader_ns);
        atomic_long_inc(&stat_loads);

        return 0;
fatal:
        force_fatal_sig(SIGKILL);
        return ret;
}

/*
        Spawning without the fork

        The usual path forks -- duplicating the caller's address space, page
        tables and file table -- and then execs, which immediately tears the
        address space back down. Nothing ever reads the copy.

        user_mode_thread creates a task with no address space to copy, and
        kernel_execve then builds the new one directly. It is the same pair the
        kernel uses to start /init. The result is a normal child of the caller:
        it reports through SIGCHLD and is reaped with wait4 like any other.
*/

struct spawn_work
{
        char *path;
        struct spawn_strings *arguments;
        struct spawn_strings *environment;
        unsigned int argc;
        bool shell_fallback;
        bool path_owned;
        // spawn_terminal's, whose first window takes the keyboard.
        bool terminal;
        // SPARK_SPAWN_KEEP_*: the ignores the program keeps.
        unsigned int keep;
        struct file *stdio[3];
};

static void spawn_strings_put(struct spawn_strings *strings)
{
        if (strings && refcount_dec_and_test(&strings->references))
                kvfree(strings);
}

static void spawn_free(struct spawn_work *work)
{
        for (unsigned int i = 0; i < array_count(work->stdio); i++)
                if (work->stdio[i])
                        fput(work->stdio[i]);
        spawn_strings_put(work->environment);
        spawn_strings_put(work->arguments);
        if (work->path_owned)
                kfree(work->path);
        kfree(work);
}

/*
        Starts one program with no arguments and no environment.

        The ioctl path exists for a program that wants to start another; this
        is for the kernel starting the first one, which is a much smaller
        request and needs none of the copying from userspace.
*/
static int spawn_enter(void *data);

#ifdef CONFIG_MOONWATER_CANVAS
static int spawn_terminal(void)
{
        struct spawn_work *work = kzalloc(sizeof(*work), GFP_KERNEL);

        if (!work)
                return -ENOMEM;

        /* Borrow the literal like do_spawn borrows its fixed /shell path;
           copying it for a worker whose next action is execve adds a slab
           round trip and no lifetime.

           /term is a link to the shell that the image makes for every applet
           in the SYSTEM category, so this is the one shell image reached
           under the name of the applet wanted -- the same multicall
           convention as every other name at the root, and it stops working
           the moment term stops being a SYSTEM applet. Nothing said the two
           had to agree until the image_nodes harness did. */
        work->path = SPARK_TERMINAL_PROGRAM;
        work->arguments = kvmalloc(sizeof(*work->arguments) +
                                   2 * sizeof(char *), GFP_KERNEL);

        /* spawn_free handles either allocation failing. */
        if (work->arguments)
                refcount_set(&work->arguments->references, 1);

        if (!work->arguments)
        {
                spawn_free(work);
                return -ENOMEM;
        }

        work->arguments->vector = (char **)(work->arguments + 1);
        work->arguments->vector[0] = work->path;
        work->arguments->vector[1] = NULL;
        work->argc = 1;
        work->terminal = true;

        if (user_mode_thread(spawn_enter, work, SIGCHLD) <= 0)
        {
                spawn_free(work);
                return -EAGAIN;
        }

        return 0;
}
#endif

/*
        A program starts able to be interrupted.

        execve resets handled signals to default but carries ignored ones
        across, so a shell that ignores SIGINT so it survives control-C would
        hand that same deafness to everything it runs, and nothing could ever
        be cancelled. What the caller said to keep stays ignored.
*/
static bool spawn_keeps(unsigned int keep, int number)
{
        switch (number)
        {
        case SIGINT: return keep & SPARK_SPAWN_KEEP_INTERRUPT;
        case SIGQUIT: return keep & SPARK_SPAWN_KEEP_QUIT;
        case SIGTSTP: return keep & SPARK_SPAWN_KEEP_STOP_KEY;
        case SIGTTIN: return keep & SPARK_SPAWN_KEEP_TTY_INPUT;
        case SIGTTOU: return keep & SPARK_SPAWN_KEEP_TTY_OUTPUT;
        default: return keep & SPARK_SPAWN_KEEP_IGNORED;
        }
}

static void spawn_default_signals(unsigned int keep)
{
        struct k_sigaction *action = current->sighand->action;
        int signal;

        spin_lock_irq(&current->sighand->siglock);

        for (signal = 0; signal < _NSIG; signal++)
                if (action[signal].sa.sa_handler == SIG_IGN &&
                    !spawn_keeps(keep, signal + 1))
                        action[signal].sa.sa_handler = SIG_DFL;

        spin_unlock_irq(&current->sighand->siglock);
}

static int spawn_enter(void *data)
{
        u64 started = ktime_get_ns();

        struct spawn_work *work = data;
        static const char *const empty_envp[] = {NULL};
        const char *const *environment = work->environment
                ? (const char *const *)work->environment->vector : empty_envp;
        int ret;

        spawn_default_signals(work->keep);

#ifdef CONFIG_MOONWATER_CANVAS
        /*
                The compositor's terminal says who it is before it becomes
                /term, so the window it opens can be told from any other
                program's. Recorded by the task itself, which is what puts it
                ahead of that window; a newer terminal replaces one that never
                opened a window at all.
        */
        if (work->terminal)
                put_pid(xchg(&canvas_spawned, get_pid(task_tgid(current))));
#endif

        /* Without the close-on-exec flag, so these three outlive the load
           while every other descriptor the caller happened to hold does
           not. A pipeline's other ends are among those. */
        for (unsigned int i = 0; i < array_count(work->stdio); i++)
                if (work->stdio[i] && (ret = replace_fd(i, work->stdio[i], 0)))
                        goto finished;

        ret = kernel_execve(work->path,
                            (const char *const *)work->arguments->vector,
                            environment);

        /*
         * The shell promises more than execve: ENOEXEC for an executable text
         * file means interpret it, not reject it. The ioctl normally cannot
         * return that error because this worker already exists by the time
         * kernel_execve sees the file, so the retry has to happen here.
         *
         * argv becomes { /bin/sh, script, original arguments after argv[0] }.
         * The raw spawn opcode never takes this branch.
         */
        if (ret == -ENOEXEC && work->shell_fallback)
        {
                const char **script_argv;

                script_argv = kcalloc((size_t)work->argc + 2,
                                      sizeof(*script_argv), GFP_KERNEL);

                if (!script_argv)
                        ret = -ENOMEM;
                else
                {
                        script_argv[0] = "/bin/sh";
                        script_argv[1] = work->path;

                        memory_copy_apart(script_argv + 2, work->arguments->vector + 1,
                                          (work->argc - 1) * sizeof(*script_argv));

                        ret = kernel_execve(script_argv[0], script_argv,
                                            environment);
                        kfree(script_argv);
                }
        }

finished:
        atomic_long_add(ktime_get_ns() - started, &stat_exec_ns);

        // kernel_execve has copied everything it needs by now, so the request
        // can go before anything else touches it.
        spawn_free(work);

        if (ret)
        {
                // The task exists by the time exec is attempted, so a bad path
                // cannot come back as an ioctl error. Exiting 127 is what a
                // shell reports for "could not run it", and it keeps the
                // caller from mistaking the failure for a clean exit.
                pr_alert_ratelimited("[moonwater] " "spawn: exec failed: %d\n", ret);
                do_exit(127 << 8);
        }

        return 0;
}

// argv and envp arrive the same way: one flat block of NUL terminated strings
// plus a count, so a single copy_from_user brings each across and the pointer
// array is built by walking it.
static int copy_strings(unsigned long user_block, unsigned int bytes,
                        unsigned int count, struct spawn_strings **out)
{
        struct spawn_strings *strings;
        char *block;
        char **vector;
        char *walk;
        size_t pointer_bytes;
        unsigned int i;

        if (count == 0 || count > SPARK_SPAWN_MAX_STRINGS || bytes == 0 ||
            bytes > SPARK_SPAWN_MAX_BYTES)
                return -EINVAL;

        /* The limits above put this below 3 MiB on every supported 64-bit
           architecture, so none of the size arithmetic can overflow. */
        pointer_bytes = ((size_t)count + 1) * sizeof(char *);

        /* The immutable bytes and their pointers have exactly the same
           lifetime. One allocation removes a slab round trip from each of
           argv and envp; kvmalloc keeps generated long commands on the fast
           path without demanding physically contiguous megabytes. */
        /* A generated environment can remain pinned to an open descriptor. */
        strings = kvmalloc(sizeof(*strings) + pointer_bytes + bytes,
                           GFP_KERNEL_ACCOUNT);
        if (!strings)
                return -ENOMEM;

        refcount_set(&strings->references, 1);
        vector = (char **)(strings + 1);
        strings->vector = vector;
        block = (char *)vector + pointer_bytes;

        if (copy_from_user(block, (const void __user *)user_block, bytes))
        {
                kvfree(strings);
                return -EFAULT;
        }

        walk = block;
        for (i = 0; i < count; i++)
        {
                size_t remaining = (size_t)(block + bytes - walk);
                size_t length = string_length_max(walk, remaining);

                if (length == remaining)
                        goto malformed;

                vector[i] = walk;
                walk += length + 1;
        }
        vector[count] = NULL;

        *out = strings;
        return 0;

malformed:
        kvfree(strings);
        return -EINVAL;
}

static long do_spawn(struct file *file, struct spawn __user *request)
{
        struct device_context *context = file->private_data;
        struct spawn args;
        struct spawn_work *work;
        struct spawn_strings *old_environment = NULL;
        struct pid *old_owner = NULL;
        const struct cred *old_cred = NULL;
        bool shell_fallback;
        const char *fixed_path;
        long ret;
        pid_t pid;

        if (copy_from_user(&args, request, sizeof(args)))
                return -EFAULT;

        /* Refusing what this kernel does not define keeps a flag added later
           from meaning "ignored" on an older loader. */
        if (args.flags & ~SPARK_SPAWN_FLAGS)
                return -EINVAL;

        shell_fallback = args.flags & SPARK_SPAWN_SHELL;
        fixed_path = args.flags & SPARK_SPAWN_TOOL ? SPARK_TOOL_PROGRAM : NULL;

        work = kzalloc(sizeof(*work), GFP_KERNEL);
        if (!work)
                return -ENOMEM;
        work->keep = args.flags;

        for (unsigned int i = 0; i < array_count(work->stdio); i++)
        {
                if (args.stdio[i] < 0)
                        continue;

                if (!(work->stdio[i] = fget(args.stdio[i])))
                {
                        ret = -EBADF;
                        goto fail;
                }
        }

        if (fixed_path)
                work->path = (char *)fixed_path;
        else
        {
                work->path = strndup_user((const char __user *)args.path,
                                          PATH_MAX);
                if (IS_ERR(work->path))
                {
                        ret = PTR_ERR(work->path);
                        work->path = NULL;
                        goto fail;
                }

                work->path_owned = true;
        }

        ret = copy_strings(args.argv, args.argv_bytes, args.argv_count,
                           &work->arguments);
        if (ret)
                goto fail;

        if (args.envp && args.envp_count)
        {
                mutex_lock(&context->spawn_lock);
                /* clone inherits the open file description and the shell's
                   generation counter.  The same generation in two process
                   branches is not the same environment, so identity is part
                   of the key.  Holding struct pid prevents numeric PID reuse
                   from making stale bytes look current later. */
                if (args.envp_generation && context->environment &&
                    context->environment_owner == task_tgid(current) &&
                    context->environment_cred == current_cred() &&
                    context->environment_generation == args.envp_generation)
                {
                        refcount_inc(&context->environment->references);
                        work->environment = context->environment;
                }
                mutex_unlock(&context->spawn_lock);

                if (!work->environment)
                {
                        ret = copy_strings(args.envp, args.envp_bytes,
                                           args.envp_count,
                                           &work->environment);
                        if (ret)
                                goto fail;

                        if (args.envp_generation)
                        {
                                mutex_lock(&context->spawn_lock);
                                old_environment = context->environment;
                                old_owner = context->environment_owner;
                                old_cred = context->environment_cred;
                                refcount_inc(&work->environment->references);
                                context->environment = work->environment;
                                context->environment_generation =
                                        args.envp_generation;
                                context->environment_owner =
                                        get_pid(task_tgid(current));
                                context->environment_cred =
                                        get_cred(current_cred());
                                mutex_unlock(&context->spawn_lock);
                                spawn_strings_put(old_environment);
                                put_pid(old_owner);
                                put_cred(old_cred);
                        }
                }
        }

        work->argc = args.argv_count;
        work->shell_fallback = shell_fallback;

        // SIGCHLD alone, so the new task is an ordinary child of the caller
        // and wait4 works on it the same way it does for a fork.
        {
                u64 started = ktime_get_ns();
                pid = user_mode_thread(spawn_enter, work, SIGCHLD);
                atomic_long_add(ktime_get_ns() - started, &stat_task_ns);
                atomic_long_inc(&stat_spawns);
        }

        if (pid < 0)
        {
                ret = pid;
                goto fail;
        }

        // work is owned by the new task from here.
        return pid;

fail:
        spawn_free(work);
        return ret;
}

static long report_stats(struct stats __user *out)
{
        struct stats stats = {
            .spawns = atomic_long_read(&stat_spawns),
            .task_ns = atomic_long_read(&stat_task_ns),
            .exec_ns = atomic_long_read(&stat_exec_ns),
            .loader_ns = atomic_long_read(&stat_loader_ns),
            .loads = atomic_long_read(&stat_loads),
            .map_ns = atomic_long_read(&stat_map_ns),
        };

        return copy_to_user(out, &stats, sizeof(stats)) ? -EFAULT : 0;
}

/*
        Typed system state in one crossing.

        These sections are already world-readable through procfs.  The ioctl
        preserves the caller's network namespace and reports no task records;
        process visibility is governed by procfs mount and ptrace policy, so
        userspace deliberately keeps that section on the procfs fallback.
*/
struct snapshot_builder
{
        u8 *data;
        u32 capacity;
        u32 used;
        u32 required;
};

static DEFINE_MUTEX(snapshot_lock);
static u8 *snapshot;
static u32 snapshot_room;

static void *snapshot_append(struct snapshot_builder *build, u32 bytes)
{
        void *record = NULL;

        if (unlikely(bytes > U32_MAX - build->required))
        {
                build->required = U32_MAX;
                return NULL;
        }

        u32 stop = build->required + bytes;

        if (likely(stop <= build->capacity))
        {
                record = build->data + build->required;
                build->used = stop;
        }

        build->required = stop;
        return record;
}

static HOT void snapshot_system(struct snapshot_header *header)
{
        struct sysinfo info;
        unsigned long loads[3];

        si_meminfo(&info);
        header->memory_total = info.totalram * info.mem_unit;
        header->memory_available = (unsigned long)si_mem_available() * PAGE_SIZE;
        si_swapinfo(&info);
        header->swap_total = info.totalswap * info.mem_unit;
        header->swap_free = info.freeswap * info.mem_unit;

        get_avenrun(loads, FIXED_1 / 200, 0);
        for (unsigned int i = 0; i < 3; i++)
                header->load[i] = LOAD_INT(loads[i]) * 100 +
                                  LOAD_FRAC(loads[i]);
}

static HOT void snapshot_cpus(struct snapshot_builder *build,
                              struct snapshot_header *header)
{
        struct snapshot_cpu aggregate = {.id = ~0u};
        struct snapshot_cpu *aggregate_out;
        int cpu;

        header->cpu_offset = build->required;
        aggregate_out = snapshot_append(build, sizeof(*aggregate_out));
        header->cpu_count++;

        for_each_online_cpu(cpu)
        {
                struct kernel_cpustat stat;
                struct snapshot_cpu record = {.id = cpu};

                kcpustat_cpu_fetch(&stat, cpu);

                /* Guest time is already included in user/nice. */
                for (unsigned int field = 0; field <= CPUTIME_STEAL; field++)
                        record.total_ns += stat.cpustat[field];

                record.idle_ns = stat.cpustat[CPUTIME_IDLE] +
                                 stat.cpustat[CPUTIME_IOWAIT];
                aggregate.total_ns += record.total_ns;
                aggregate.idle_ns += record.idle_ns;

                struct snapshot_cpu *out =
                    snapshot_append(build, sizeof(record));

                if (likely(out))
                        *out = record;
                header->cpu_count++;
        }

        if (aggregate_out)
                *aggregate_out = aggregate;
}

static HOT void snapshot_networks(struct snapshot_builder *build,
                                  struct snapshot_header *header)
{
        struct net_device *device;
        struct net *net = current->nsproxy->net_ns;

        header->network_offset = build->required;

        rcu_read_lock();
        for_each_netdev_rcu(net, device)
        {
                struct rtnl_link_stats64 temporary;
                const struct rtnl_link_stats64 *stats =
                    dev_get_stats(device, &temporary);
                struct snapshot_network record = {0};

                strscpy(record.name, device->name, sizeof(record.name));
                record.received = stats->rx_bytes;
                record.transmitted = stats->tx_bytes;

                struct snapshot_network *out =
                    snapshot_append(build, sizeof(record));

                if (likely(out))
                        *out = record;
                header->network_count++;
        }
        rcu_read_unlock();
}

/*
        The settings this machine booted with.

        The EFI stub hands over the image's .mwset section as a configuration
        table; this keeps the newest of its two slots that checks, and nothing
        when neither does, which is the defaults. A torn write, a made-up
        table or an image with no section all start the machine the same way,
        and only spark_settings_check decides which is which.

        SET replaces the copy for the rest of the session, checked the same
        way; the moonwater command writes the image itself.
*/
static struct spark_settings *settings_current;
static DEFINE_MUTEX(settings_lock);

static COLD void __init settings_start(void)
{
#if defined(CONFIG_EFI) && !defined(MODULE)
        struct linux_efi_moonwater_settings *table;
        struct spark_settings *slots;
        u32 size;
        b32 newest;

        if (efi_moonwater_settings == EFI_INVALID_TABLE_ADDR)
                return;

        table = memremap(efi_moonwater_settings, sizeof(*table), MEMREMAP_WB);
        if (!table)
                return;

        size = table->size;
        memunmap(table);

        if (size < 2 * SPARK_SETTINGS_SLOT || size > 64 * SPARK_SETTINGS_SLOT)
        {
                pr_warn("[moonwater] " "a settings table of %u bytes is not one; booting on the defaults\n", size);
                return;
        }

        table = memremap(efi_moonwater_settings, sizeof(*table) + size, MEMREMAP_WB);
        if (!table)
                return;

        slots = kmalloc(2 * SPARK_SETTINGS_SLOT, GFP_KERNEL);
        if (slots)
                memory_copy_apart(slots, table->bytes, 2 * SPARK_SETTINGS_SLOT);
        memunmap(table);

        if (!slots)
                return;

        newest = spark_settings_newest(slots);
        if (newest < 0)
                pr_warn("[moonwater] " "both settings slots in the image are damaged; booting on the defaults\n");
        else
        {
                settings_current = kmemdup(slots + newest, SPARK_SETTINGS_SLOT, GFP_KERNEL);
                pr_info("[moonwater] " "settings: slot %d, generation %llu\n", newest,
                        (unsigned long long)slots[newest].generation);
        }

        kfree(slots);
#endif
}

static long settings_get(struct spark_settings_request __user *request)
{
        struct spark_settings_request asked;
        struct spark_settings *held = NULL;
        long answer = 0;

        if (!capable(CAP_SYS_ADMIN))
                return -EPERM;
        if (copy_from_user(&asked, request, sizeof(asked)))
                return -EFAULT;
        if (asked.flags)
                return -EINVAL;

        /* A copy of the slot goes out, not the slot: what the caller's
           buffer is mapped from decides how long a copy to it takes, and
           settings_lock is not to be held for that. */
        mutex_lock(&settings_lock);
        if (!settings_current)
                answer = -ENODATA;
        else if (!(held = kmemdup(settings_current, SPARK_SETTINGS_SLOT,
                                  GFP_KERNEL)))
                answer = -ENOMEM;
        mutex_unlock(&settings_lock);

        if (answer)
                return answer;

        if (copy_to_user((void __user *)asked.address, held,
                         SPARK_SETTINGS_SLOT))
                answer = -EFAULT;
        kfree(held);

        return answer;
}

static long settings_set(struct spark_settings_request __user *request)
{
        struct spark_settings_request asked;
        struct spark_settings *settings;
        struct spark_settings *before;

        if (!capable(CAP_SYS_ADMIN))
                return -EPERM;
        if (copy_from_user(&asked, request, sizeof(asked)))
                return -EFAULT;
        if (asked.flags)
                return -EINVAL;

        settings = memdup_user((void __user *)asked.address, SPARK_SETTINGS_SLOT);
        if (IS_ERR(settings))
                return PTR_ERR(settings);

        if (spark_settings_check(settings) < 0)
        {
                kfree(settings);
                return -EINVAL;
        }

        mutex_lock(&settings_lock);
        before = settings_current;
        settings_current = settings;
        mutex_unlock(&settings_lock);

        kfree(before);
        return 0;
}

static HOT long report_snapshot(struct snapshot_request __user *out)
{
        struct snapshot_request request;
        struct snapshot_builder build;
        struct snapshot_header *header;
        u8 *data;
        u32 room;
        u32 capacity;
        long answer = 0;

        if (copy_from_user(&request, out, sizeof(request)))
                return -EFAULT;
        if (request.version != SPARK_SNAPSHOT_VERSION || request.reserved ||
            (request.flags & ~SPARK_SNAPSHOT_KERNEL) ||
            request.capacity > SPARK_SNAPSHOT_MAX_BYTES || !request.buffer)
                return -EINVAL;
        if (request.capacity < sizeof(*header))
        {
                request.required = sizeof(*header);
                request.used = 0;
                if (copy_to_user(out, &request, sizeof(request)))
                        return -EFAULT;
                return -ENOSPC;
        }

        /*
                The warm buffer is taken, not locked.

                It was held under snapshot_lock for the whole call, the copy
                to the caller's buffer included -- and /dev/spark is open to
                everyone, so a caller whose buffer lay in a page that a
                filesystem it controls answers slowly, or never (a FUSE
                mount in a user namespace is enough), held the lock for as
                long and every top, ps and free after it waited. Taken out,
                the buffer is this call's alone while it builds and copies,
                and the lock is held for two pointer moves. Whoever finishes
                first puts theirs back; the other is freed, so what stays
                resident is still one buffer of at most what a capture
                needed.
        */
        mutex_lock(&snapshot_lock);
        data = snapshot;
        room = snapshot_room;
        snapshot = NULL;
        snapshot_room = 0;
        mutex_unlock(&snapshot_lock);

        // A caller's spare capacity is not live kernel data. Keep the warm
        // buffer, but grow beyond a page only after a capture needs more.
        capacity = min_t(u32, request.capacity, max_t(u32, room, PAGE_SIZE));
retry:
        if (unlikely(capacity > room))
        {
                u8 *larger = kvrealloc(data, capacity, GFP_KERNEL);

                if (!larger)
                {
                        answer = -ENOMEM;
                        goto keep;
                }

                data = larger;
                room = capacity;
        }

        build.data = data;
        build.capacity = capacity;
        build.used = 0;
        build.required = 0;
        memory_fill(build.data, 0, sizeof(*header));
        header = snapshot_append(&build, sizeof(*header));

        header->version = SPARK_SNAPSHOT_VERSION;
        header->flags = request.flags;
        header->page_size = PAGE_SIZE;
        header->monotonic_ns = ktime_get_ns();
        header->realtime_seconds = ktime_get_real_seconds();
        header->uptime_ns = ktime_get_boottime_ns();

        if (request.flags & SPARK_SNAPSHOT_SYSTEM)
                snapshot_system(header);
        if (request.flags & SPARK_SNAPSHOT_CPU)
                snapshot_cpus(&build, header);
        if (request.flags & SPARK_SNAPSHOT_NETWORK)
                snapshot_networks(&build, header);

        if (build.required > capacity && capacity < request.capacity)
        {
                // Inventory can grow during capture. Doubling bounds this to
                // 12 retries from one page to the ABI's 16 MiB ceiling and
                // leaves ENOSPC exclusively for the caller's own capacity.
                capacity = min(max(build.required, capacity * 2), request.capacity);
                goto retry;
        }

        header->bytes = build.used;
        request.used = build.used;
        request.required = build.required;

        if (copy_to_user((void __user *)request.buffer, build.data,
                         build.used))
        {
                answer = -EFAULT;
                goto keep;
        }

        if (build.required > build.capacity)
                answer = -ENOSPC;

        if (copy_to_user(out, &request, sizeof(request)))
                answer = -EFAULT;
keep:
        mutex_lock(&snapshot_lock);
        if (data && !snapshot)
        {
                snapshot = data;
                snapshot_room = room;
                data = NULL;
        }
        mutex_unlock(&snapshot_lock);
        kvfree(data);
        return answer;
}

#endif /* SPARK_KERNEL */
