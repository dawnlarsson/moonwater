/*
        A game pad of one machine, played on another: `moonwater link pad`.

                link pad NAME [DEVICE]  this machine's pad (the first one, or
                                        DEVICE, eventN) to the machine NAME
                link pad stop           and no more
                link pad                what is sent from here and made here

        The machine sending reads the pad's events and takes it (EVIOCGRAB)
        for as long as it is sent, so its own desktop and games do not act on
        it too. The machine taking it makes one through uinput with the same
        name, ids and bus, the same buttons and axes with their ranges, fuzz
        and flat, and the same force feedback, so Steam, SDL and a game take
        it for the pad it is and map it by its ids. Its phys is
        "waterlink/" and the sender's name, which is how the desktop's pad
        engine knows it from a game's own virtual pads
        (COMPOSITOR_ACCESS_LINKED_PHYS).

        WHAT GOES

        A session of its own, asked for with the pad's description (below),
        which the grant "pad" lets in. Then, on LINK_KEY_PAD, a register: the
        pad's whole state -- every axis and every button -- with the report's
        own kernel stamp, posted urgent at every report that changed
        something and flushed before the next is read, so each report leaves
        alone and at once. A lost datagram is a state the next one replaces;
        a register's retransmission is always the newest. Held still, the
        state is posted again every LINK_PAD_KEEPALIVE, which is how the far
        side tells a quiet pad from a lost one: after LINK_PAD_SILENCE of
        nothing it lets every button go and every axis rest, and the pad goes
        with the session.

        Back, on LINK_KEY_PAD_FORCE, a stream: the effects a game uploads to
        the pad made there, the ones it erases, and the plays, each played on
        the real pad here. And on LINK_KEY_PAD_CLOCK the far side's clock,
        asked once a second and answered at once, so the delay it shows is
        the sender's kernel stamp to its own uinput write on one clock, its
        own, between two machines as on one.

        WHAT A GRANT OF IT GIVES

        A pad and nothing else: the description's buttons are the pad's
        ranges (LINK_PAD_KEY_OK), no keyboard key, no mouse button, no touch,
        and no relative axis is forwarded, so a machine granted a pad types
        nothing into a terminal. What a pad can do on the machine it is made
        on it can: a game takes it, and the desktop's pad engine moves the
        pointer with it.

        THE PATH

        The sender's loop runs at SCHED_FIFO LINK_PAD_PRIORITY_SENDER, on the
        input core when the display took one (the CPU held at a 2 us resume
        latency that takes the USB controllers' interrupts), blocked in the
        link's one wait on the pad and the socket: a report is read, posted,
        sealed and sent in the wake that read it. The listener takes the
        datagram on its socket, and its loop is the one that writes the
        uinput report (one write of the whole report and its SYN_REPORT),
        at SCHED_FIFO LINK_PAD_PRIORITY_MACHINE while it makes a pad: a
        thread of its own would be one wake more, since the datagram comes
        to the listener's socket either way.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/

#ifndef WATERLINK_PAD_INCLUDED
#define WATERLINK_PAD_INCLUDED

#define LINK_KEY_PAD 10       // sender to machine, a register: the pad's whole state
#define LINK_KEY_PAD_FORCE 11 // machine to sender, durable: force feedback
#define LINK_KEY_PAD_CLOCK 12 // either way, a register: the clocks compared

#define LINK_PAD_KEEPALIVE 100000ull // of a pad held still, then its state again
#define LINK_PAD_SILENCE 1000000ull  // of hearing no state, then every button goes
#define LINK_PAD_PING 1000000ull     // between two asks of the sender's clock
#define LINK_PAD_DEAF 5000000ull     // a sender that hears no ask that long sends to nobody: it asks again
#define LINK_PAD_PRIORITY_SENDER 45  // below the compositor's input reader (50)
#define LINK_PAD_PRIORITY_MACHINE 30 // below the compositor (40): the listener moves files too
#define LINK_PAD_PHYS "waterlink/"   // and the sender's name: COMPOSITOR_ACCESS_LINKED_PHYS

/*      Linux input's numbers: <linux/input.h>, <linux/input-event-codes.h>
        and <linux/uinput.h>, the same on the three machines (the generic
        ioctl encoding, 64 bit). The counts are the kernel's, not this
        file's: a pad has no button past KEY_CNT and no effect id past
        FF_MAX_EFFECTS, which is FF_GAIN. */
#define LINK_PAD_KEYS 0x300   // KEY_CNT
#define LINK_PAD_AXES 0x40    // ABS_CNT
#define LINK_PAD_FORCES 0x80  // FF_CNT
#define LINK_PAD_EFFECTS 0x60 // FF_MAX_EFFECTS
#define LINK_PAD_NAME 80      // UINPUT_MAX_NAME_SIZE

#define LINK_PAD_EV_SYN 0x00
#define LINK_PAD_EV_KEY 0x01
#define LINK_PAD_EV_ABS 0x03
#define LINK_PAD_EV_FF 0x15
#define LINK_PAD_EV_UINPUT 0x0101
#define LINK_PAD_SYN_REPORT 0
#define LINK_PAD_SYN_DROPPED 3
#define LINK_PAD_UI_FF_UPLOAD 1
#define LINK_PAD_UI_FF_ERASE 2
#define LINK_PAD_FF_PERIODIC 0x51
#define LINK_PAD_FF_CUSTOM 0x5d
#define LINK_PAD_PROP_ACCELEROMETER 0x06

#define LINK_PAD_EVIOCGID 0x80084502u
#define LINK_PAD_EVIOCGNAME(n) (0x80004506u | (p32)(n) << 16)
#define LINK_PAD_EVIOCGPROP(n) (0x80004509u | (p32)(n) << 16)
#define LINK_PAD_EVIOCGKEY(n) (0x80004518u | (p32)(n) << 16)
#define LINK_PAD_EVIOCGBIT(type, n) ((0x80004520u + (type)) | (p32)(n) << 16)
#define LINK_PAD_EVIOCGABS(axis) (0x80184540u + (axis))
#define LINK_PAD_EVIOCSFF 0x40304580u
#define LINK_PAD_EVIOCRMFF 0x40044581u
#define LINK_PAD_EVIOCGEFFECTS 0x80044584u
#define LINK_PAD_EVIOCGRAB 0x40044590u
#define LINK_PAD_EVIOCSCLOCKID 0x400445a0u
#define LINK_PAD_UI_DEV_CREATE 0x5501u
#define LINK_PAD_UI_DEV_DESTROY 0x5502u
#define LINK_PAD_UI_DEV_SETUP 0x405c5503u
#define LINK_PAD_UI_ABS_SETUP 0x401c5504u
#define LINK_PAD_UI_SET_EVBIT 0x40045564u
#define LINK_PAD_UI_SET_KEYBIT 0x40045565u
#define LINK_PAD_UI_SET_ABSBIT 0x40045567u
#define LINK_PAD_UI_SET_FFBIT 0x4004556bu
#define LINK_PAD_UI_SET_PHYS 0x4008556cu
#define LINK_PAD_UI_SET_PROPBIT 0x4004556eu
#define LINK_PAD_UI_GET_SYSNAME(n) (0x8000552cu | (p32)(n) << 16)
#define LINK_PAD_UI_BEGIN_FF_UPLOAD 0xc06855c8u
#define LINK_PAD_UI_END_FF_UPLOAD 0x406855c9u
#define LINK_PAD_UI_BEGIN_FF_ERASE 0xc00c55cau
#define LINK_PAD_UI_END_FF_ERASE 0x400c55cbu

typedef struct { p16 bus, vendor, product, version; } link_pad_id;                 // struct input_id
typedef struct { b32 value, least, most, fuzz, flat, resolution; } link_pad_range; // struct input_absinfo
typedef struct { b64 seconds, microseconds; p16 type, code; b32 value; } link_pad_event; // struct input_event
typedef struct { link_pad_id id; p8 name[LINK_PAD_NAME]; p32 effects; } link_pad_setup;  // struct uinput_setup
typedef struct { p16 code, hole; link_pad_range range; } link_pad_axis_setup;            // struct uinput_abs_setup
typedef struct { p16 type; b16 id; p16 direction, trigger[2], replay[2], hole; p64 u[4]; } link_pad_effect; // struct ff_effect
typedef struct { p32 request; b32 answer; link_pad_effect effect, old; } link_pad_upload;            // struct uinput_ff_upload
typedef struct { p32 request; b32 answer; p32 effect; } link_pad_erase;                             // struct uinput_ff_erase

_Static_assert(sizeof(link_pad_range) == 24 && sizeof(link_pad_event) == 24 &&
                       sizeof(link_pad_setup) == 92 && sizeof(link_pad_axis_setup) == 28 &&
                       sizeof(link_pad_effect) == 48 && sizeof(link_pad_upload) == 104 &&
                       sizeof(link_pad_erase) == 12,
               "the input structures are the kernel's");

/*      What `moonwater link pad` shows, one record a pad: the sender keeps
        its own in its lock file, HOST_STATE/link.pad.out.eventN, and the
        listener the ones it made in HOST_STATE/link.pads. Delays are
        microseconds: a sender's from the report's kernel stamp to its
        datagram sent, a listener's from the sender's kernel stamp to its own
        uinput write, on its own clock (the sender's moved by the offset the
        clock asks found). */
enum { LINK_PAD_TRYING = 1, LINK_PAD_SENDING = 2, LINK_PAD_QUIET = 3, LINK_PAD_REFUSED = 4 };

typedef struct
{
        p8 machine[WATERLINK_NAME_MAX]; // sent to, or made for
        p8 name[LINK_PAD_NAME];         // the pad's own
        p8 node[16];                    // its event node on this machine
        p16 vendor, product;
        p8 sender; // sent from here
        p8 state;  // LINK_PAD_*
        p8 why[96];
        p64 since; // microseconds, link_now
        p64 reports;
        p64 delays;
        b64 delay_last, delay_sum, delay_least, delay_most;
        b64 offset; // the sender's clock less this one's
        p64 rtt;    // of the ask the offset came from
} link_pad_note;

/*      A pad at either end, whole: the description as it went or came, the
        axes and buttons in its order with the index of each code, the state
        now, and at the sender the device's id for each effect id the far
        pad gave out. Every count is the kernel's, so it is one size. */
struct link_pad
{
        bipolar fd; // the pad, at the sender; /dev/uinput, at the machine
        p8 sender;
        p8 begun;    // sender: taken and sending; machine: made
        p8 quiet;    // read until it said not now, and no wait has heard it since
        p8 dropped;  // the kernel dropped events: read the state again at the report
        p8 released; // machine: silence let it all go
        p8 gone;     // sender: the device went away
        p8 deaf;     // sender: the far side stopped asking for its clock, so it has no pad of this
        p8 answered; // sender: the far side made it, and said its node
        p8 far_node[16];
        p8 changed;  // sender: since the last post
        p8 axes;
        p16 keys;
        p8 effects;
        p8 forces;   // whether it has force feedback
        link_pad_id id;
        p8 name[LINK_PAD_NAME];
        p8 description[WATERLINK_FRAME_MAX];
        positive described;
        p8 axis[LINK_PAD_AXES];
        p8 axis_at[LINK_PAD_AXES]; // 0xff: no axis of this pad
        link_pad_range range[LINK_PAD_AXES];
        b32 value[LINK_PAD_AXES];
        b32 rest[LINK_PAD_AXES];
        p16 key[LINK_PAD_KEYS];
        p16 key_at[LINK_PAD_KEYS]; // 0xffff: no button of this pad
        p8 down[LINK_PAD_KEYS / 8]; // by index
        b16 effect[LINK_PAD_EFFECTS];
        p32 sequence; // sender: the last posted; machine: the last taken
        p64 stamp;    // the newest report's kernel stamp (machine: the last written)
        p64 posted;
        p64 heard;  // machine: the last state; sender: the last ask of its clock
        p64 pinged; // machine: the last ask
        p64 pings;
        b64 offset;
        p64 offset_rtt, offset_at;
        bipolar ready; // sender: the pipe the starter waits on, -1 once told
        bipolar handle; // sender: its lock file, which holds its note
        p64 noted;
        link_pad_note note;
};

#if !defined(MACOS)

static struct link_pad link_pad_out; // the sender's: one process, one pad

/*      The buttons a pad has, and nothing a keyboard or a mouse has: the
        joystick and gamepad ranges, the misc buttons, the cross's and the
        grips (BTN_DPAD_UP to BTN_GRIPR2: an Elite's paddles), the
        trigger-happy ones and KEY_RECORD, which is the share button of an
        Xbox Series pad. */
static p8 link_pad_key_ok(p32 code)
{
        return (code >= 0x100 && code < 0x110) || (code >= 0x120 && code < 0x140) ||
               (code >= 0x220 && code < 0x228) || (code >= 0x2c0 && code < 0x2e8) || code == 0xa7;
}

// Every axis but the multitouch ones (0x2f to 0x3e): a pad's touch surface is a device of its own.
static p8 link_pad_axis_ok(p32 code)
{
        return code < 0x2f;
}

static p8 link_pad_bit(const p8 address_to bits, p32 at)
{
        return bits[at / 8] >> (at % 8) & 1;
}

// Where an axis rests, for a release: 0 when its range holds it, a trigger's least, else its middle.
static b32 link_pad_rest(p32 code, link_pad_range address_to range)
{
        if (range->least <= 0 && range->most >= 0)
                return 0;
        if (code == 2 || code == 5 || code == 6 || code == 9 || code == 10)
                return range->least;
        return (b32)(((b64)range->least + range->most) / 2);
}

/*
        The description, in the request after its 'J' (LINK_ASK_PAD):

                version 1, bus vendor product version (16 bits each),
                effects (8 bits), the name's length and the name,
                properties (4 bytes), buttons (96), forces (16) and axes (8)
                as bit maps, then for each axis in code order its least,
                most, fuzz, flat and resolution (32 bits each)

        little endian. One frame carries it: with the longest name that is
        47 axes, which is the protocol's bound on a pad, and a pad of more is
        refused where it is described. The counts and the order are what the
        state on LINK_KEY_PAD is read by.
*/
#define LINK_PAD_VERSION 1
#define LINK_PAD_HEAD 12
#define LINK_PAD_MAPS (4 + LINK_PAD_KEYS / 8 + LINK_PAD_FORCES / 8 + LINK_PAD_AXES / 8)

// The tables a description gives, filled from its maps: the index of each code, in code order.
static fn link_pad_index(struct link_pad address_to pad, const p8 address_to keys,
                         const p8 address_to axes)
{
        memory_fill(pad->axis_at, (b8)-1, sizeof pad->axis_at);
        memory_fill(pad->key_at, (b8)-1, sizeof pad->key_at);
        pad->axes = 0;
        pad->keys = 0;
        for (p32 code = 0; code < LINK_PAD_AXES; code++)
                if (link_pad_bit(axes, code))
                {
                        pad->axis_at[code] = pad->axes;
                        pad->axis[pad->axes++] = (p8)code;
                }
        for (p32 code = 0; code < LINK_PAD_KEYS; code++)
                if (link_pad_bit(keys, code))
                {
                        pad->key_at[code] = pad->keys;
                        pad->key[pad->keys++] = (p16)code;
                }
}

/*      The state now, read from the device: every button and every axis,
        for the start and after the kernel dropped events. */
static fn link_pad_resync(struct link_pad address_to pad)
{
        p8 keys[LINK_PAD_KEYS / 8];

        memory_zero(keys, sizeof keys);
        (void)system_control(pad->fd, LINK_PAD_EVIOCGKEY(sizeof keys), keys);
        memory_zero(pad->down, sizeof pad->down);
        for (positive at = 0; at < pad->keys; at++)
                if (link_pad_bit(keys, pad->key[at]))
                        pad->down[at / 8] |= (p8)(1u << (at % 8));
        for (positive at = 0; at < pad->axes; at++)
        {
                link_pad_range range;

                if (system_control(pad->fd, LINK_PAD_EVIOCGABS(pad->axis[at]), address_of range) >= 0)
                        pad->value[at] = range.value;
        }
}

// The device read into the description: 0, or why it cannot be sent.
static string_address link_pad_describe(struct link_pad address_to pad)
{
        p8 address_to d = pad->description;
        p8 keys[LINK_PAD_KEYS / 8], forces[LINK_PAD_FORCES / 8], axes[LINK_PAD_AXES / 8], properties[4];
        b32 effects = 0;
        positive name;
        positive at;

        memory_zero(keys, sizeof keys);
        memory_zero(forces, sizeof forces);
        memory_zero(axes, sizeof axes);
        memory_zero(properties, sizeof properties);
        memory_zero(pad->name, sizeof pad->name);
        if (system_control(pad->fd, LINK_PAD_EVIOCGID, address_of pad->id) < 0)
                return "it is not an input device";
        (void)system_control(pad->fd, LINK_PAD_EVIOCGNAME(LINK_PAD_NAME - 1), pad->name);
        pad->name[LINK_PAD_NAME - 1] = 0;
        (void)system_control(pad->fd, LINK_PAD_EVIOCGPROP(sizeof properties), properties);
        (void)system_control(pad->fd, LINK_PAD_EVIOCGBIT(LINK_PAD_EV_KEY, sizeof keys), keys);
        (void)system_control(pad->fd, LINK_PAD_EVIOCGBIT(LINK_PAD_EV_ABS, sizeof axes), axes);
        if (system_control(pad->fd, LINK_PAD_EVIOCGBIT(LINK_PAD_EV_FF, sizeof forces), forces) < 0 ||
            system_control(pad->fd, LINK_PAD_EVIOCGEFFECTS, address_of effects) < 0 || effects <= 0)
        {
                memory_zero(forces, sizeof forces);
                effects = 0;
        }
        if (effects > LINK_PAD_EFFECTS)
                effects = LINK_PAD_EFFECTS;
        //      What a pad has and nothing else (link_pad_key_ok): the far side
        //      refuses a description with more.
        for (p32 code = 0; code < LINK_PAD_KEYS; code++)
                if (!link_pad_key_ok(code))
                        keys[code / 8] &= (p8) ~(1u << (code % 8));
        for (p32 code = 0; code < LINK_PAD_AXES; code++)
                if (!link_pad_axis_ok(code))
                        axes[code / 8] &= (p8) ~(1u << (code % 8));
        properties[0] &= 1u << LINK_PAD_PROP_ACCELEROMETER;
        properties[1] = properties[2] = properties[3] = 0;
        if (!window_input_game(keys, properties[0]))
                return "it is not a game pad";

        link_pad_index(pad, keys, axes);
        name = string_length((string_address)pad->name);
        d[0] = LINK_ASK_PAD;
        d[1] = LINK_PAD_VERSION;
        memory_copy(d + 2, address_of pad->id, 8);
        d[10] = (p8)effects;
        d[11] = (p8)name;
        memory_copy(d + LINK_PAD_HEAD, pad->name, name);
        at = LINK_PAD_HEAD + name;
        memory_copy(d + at, properties, 4);
        memory_copy(d + at + 4, keys, sizeof keys);
        memory_copy(d + at + 4 + sizeof keys, forces, sizeof forces);
        memory_copy(d + at + 4 + sizeof keys + sizeof forces, axes, sizeof axes);
        at += LINK_PAD_MAPS;
        if (at + 20 * (positive)pad->axes > WATERLINK_FRAME_MAX)
                return "it has more axes than one request carries";
        for (positive axis = 0; axis < pad->axes; axis++)
        {
                link_pad_range address_to range = pad->range + axis;

                memory_zero(range, sizeof address_to range);
                (void)system_control(pad->fd, LINK_PAD_EVIOCGABS(pad->axis[axis]), range);
                memory_copy(d + at, address_of range->least, 20);
                at += 20;
        }
        pad->described = at;
        pad->effects = (p8)effects;
        pad->forces = effects > 0;
        for (positive e = 0; e < LINK_PAD_EFFECTS; e++)
                pad->effect[e] = -1;
        return null;
}

/*      A description taken: the tables it gives, and 0, or why it is not a
        pad. Every field is judged before one is believed. */
static string_address link_pad_read_description(struct link_pad address_to pad,
                                                const p8 address_to d, positive length)
{
        const p8 address_to keys;
        const p8 address_to forces;
        const p8 address_to axes;
        p8 properties;
        positive name;
        positive at;

        if (length < LINK_PAD_HEAD || d[1] != LINK_PAD_VERSION)
                return "that pad is described in a way this machine does not read";
        name = d[11];
        at = LINK_PAD_HEAD + name;
        if (name >= LINK_PAD_NAME || at + LINK_PAD_MAPS > length || d[10] > LINK_PAD_EFFECTS)
                return "that request is malformed";
        memory_copy(address_of pad->id, d + 2, 8);
        memory_zero(pad->name, sizeof pad->name);
        //      Shown wherever pads are listed: a byte that is no text is a '?'.
        for (positive c = 0; c < name; c++)
                pad->name[c] = d[LINK_PAD_HEAD + c] >= 0x20 && d[LINK_PAD_HEAD + c] != 0x7f ? d[LINK_PAD_HEAD + c] : '?';
        properties = d[at];
        keys = d + at + 4;
        forces = keys + LINK_PAD_KEYS / 8;
        axes = forces + LINK_PAD_FORCES / 8;
        if ((properties & ~(1u << LINK_PAD_PROP_ACCELEROMETER)) || d[at + 1] || d[at + 2] || d[at + 3])
                return "that is not a game pad";
        for (p32 code = 0; code < LINK_PAD_KEYS; code++)
                if (link_pad_bit(keys, code) && !link_pad_key_ok(code))
                        return "that is not a game pad";
        for (p32 code = 0; code < LINK_PAD_AXES; code++)
                if (link_pad_bit(axes, code) && !link_pad_axis_ok(code))
                        return "that is not a game pad";
        if (!window_input_game(keys, properties))
                return "that is not a game pad";
        link_pad_index(pad, keys, axes);
        at += LINK_PAD_MAPS;
        if (at + 20 * (positive)pad->axes != length)
                return "that request is malformed";
        for (positive axis = 0; axis < pad->axes; axis++)
        {
                link_pad_range address_to range = pad->range + axis;

                memory_copy(address_of range->least, d + at, 20);
                at += 20;
                if (range->least > range->most)
                        return "that request is malformed";
                range->value = pad->rest[axis] = pad->value[axis] = link_pad_rest(pad->axis[axis], range);
        }
        pad->effects = d[10];
        pad->forces = false;
        for (positive b = 0; b < LINK_PAD_FORCES / 8; b++)
                pad->forces |= forces[b] != 0;
        if (!pad->effects)
                pad->forces = false;
        memory_copy(pad->description, d, length);
        pad->described = length;
        return null;
}

// This machine's pad made like it, through uinput: 0, or why not.
static string_address link_pad_make(struct link_pad address_to pad, string_address from)
{
        const p8 address_to maps = pad->description + LINK_PAD_HEAD + pad->description[11];
        const p8 address_to keys = maps + 4;
        const p8 address_to forces = keys + LINK_PAD_KEYS / 8;
        link_pad_setup setup;
        p8 phys[16 + WATERLINK_NAME_MAX];
        p8 system_name[32];
        p8 path[96];
        file_walk walk;
        struct linux_dirent64 address_to entry;
        bipolar fd = system_open_at(AT_FDCWD, "/dev/uinput", FILE_READ_WRITE | O_NONBLOCK | O_CLOEXEC);

        if (fd < 0)
                return "this machine cannot make a pad: /dev/uinput does not open";
        pad->fd = fd;
        if (pad->keys)
                (void)system_control(fd, LINK_PAD_UI_SET_EVBIT, LINK_PAD_EV_KEY);
        if (pad->axes)
                (void)system_control(fd, LINK_PAD_UI_SET_EVBIT, LINK_PAD_EV_ABS);
        if (pad->forces)
                (void)system_control(fd, LINK_PAD_UI_SET_EVBIT, LINK_PAD_EV_FF);
        for (positive at = 0; at < pad->keys; at++)
                (void)system_control(fd, LINK_PAD_UI_SET_KEYBIT, pad->key[at]);
        for (p32 code = 0; pad->forces && code < LINK_PAD_FORCES; code++)
                if (link_pad_bit(forces, code))
                        (void)system_control(fd, LINK_PAD_UI_SET_FFBIT, code);
        if (maps[0] & (1u << LINK_PAD_PROP_ACCELEROMETER))
                (void)system_control(fd, LINK_PAD_UI_SET_PROPBIT, LINK_PAD_PROP_ACCELEROMETER);
        for (positive at = 0; at < pad->axes; at++)
        {
                link_pad_axis_setup axis = {.code = pad->axis[at], .range = pad->range[at]};

                (void)system_control(fd, LINK_PAD_UI_SET_ABSBIT, pad->axis[at]);
                if (system_control(fd, LINK_PAD_UI_ABS_SETUP, address_of axis) < 0)
                        return "this machine cannot make a pad of those ranges";
        }
        string_copy_bounded((string_address)phys, LINK_PAD_PHYS, sizeof phys);
        string_append_bounded((string_address)phys, from, sizeof phys);
        (void)system_control(fd, LINK_PAD_UI_SET_PHYS, phys);
        memory_zero(address_of setup, sizeof setup);
        setup.id = pad->id;
        memory_copy(setup.name, pad->name, LINK_PAD_NAME);
        setup.effects = pad->forces ? pad->effects : 0;
        if (system_control(fd, LINK_PAD_UI_DEV_SETUP, address_of setup) < 0 ||
            system_control(fd, LINK_PAD_UI_DEV_CREATE, 0) < 0)
                return "this machine cannot make a pad like that one";
        pad->begun = true;

        //      Its event node, for the far side and `link pad`: the one the
        //      input device uinput made has under it.
        memory_zero(system_name, sizeof system_name);
        if (system_control(fd, LINK_PAD_UI_GET_SYSNAME(sizeof system_name - 1), system_name) >= 0)
        {
                string_copy_bounded((string_address)path, "/sys/devices/virtual/input/", sizeof path);
                string_append_bounded((string_address)path, (string_address)system_name, sizeof path);
                if (file_walk_open(address_of walk, AT_FDCWD, (string_address)path))
                {
                        while ((entry = file_walk_next(address_of walk)))
                                if (string_has_prefix(entry->d_name, "event"))
                                        string_copy_bounded((string_address)pad->note.node, entry->d_name,
                                                            sizeof pad->note.node);
                        file_walk_close(address_of walk);
                }
        }
        return null;
}

/*      The state's frame: its sequence and the report's kernel stamp, every
        axis, then the buttons as bits, all in the description's order. */
#define LINK_PAD_STATE_HEAD (4 + 8)

static positive link_pad_state_length(struct link_pad address_to pad)
{
        return LINK_PAD_STATE_HEAD + 4 * (positive)pad->axes + ((positive)pad->keys + 7) / 8;
}

static fn link_pad_note_delay(link_pad_note address_to note, b64 delay)
{
        note->delay_last = delay;
        note->delay_sum += delay;
        if (!note->delays || delay < note->delay_least)
                note->delay_least = delay;
        if (!note->delays || delay > note->delay_most)
                note->delay_most = delay;
        note->delays++;
}

static fn link_session_flush(struct link_session address_to s, p64 now);

// The state posted and sent at once, alone: a report leaves in the wake that read it.
static fn link_pad_post(struct link_session address_to s, p64 now, p8 report)
{
        struct link_pad address_to pad = s->pad;
        p8 frame[LINK_PAD_STATE_HEAD + 4 * LINK_PAD_AXES + LINK_PAD_KEYS / 8];
        positive at = LINK_PAD_STATE_HEAD;

        memory_store_unaligned(p32, frame, pad->sequence + 1);
        memory_store_unaligned(p64, frame + 4, pad->stamp);
        for (positive axis = 0; axis < pad->axes; axis++, at += 4)
                memory_store_unaligned(b32, frame + at, pad->value[axis]);
        memory_copy(frame + at, pad->down, ((positive)pad->keys + 7) / 8);
        link_self.now = now;
        if (!link_post(s, LINK_KEY_PAD, WATERLINK_FRAME_REPLACEABLE | WATERLINK_FRAME_URGENT, 'S',
                       frame, link_pad_state_length(pad)))
                return;
        pad->sequence++;
        pad->posted = now;
        pad->changed = false;
        link_session_flush(s, now);
        if (report)
        {
                pad->note.reports++;
                link_pad_note_delay(address_of pad->note, (b64)(link_now() - pad->stamp));
        }
}

// The sender's pad read until it says not now: each report that changed something is posted as it ends.
static fn link_pad_read(struct link_session address_to s, p64 now)
{
        struct link_pad address_to pad = s->pad;
        link_pad_event events[64];

        while (!pad->quiet && !pad->gone)
        {
                bipolar got = system_read_once(pad->fd, events, sizeof events);

                if (got == -EAGAIN || got == -EINTR)
                {
                        pad->quiet = got == -EAGAIN;
                        continue;
                }
                if (got <= 0)
                {
                        pad->gone = true;
                        break;
                }
                for (positive at = 0; at < (positive)got / sizeof events[0]; at++)
                {
                        link_pad_event address_to e = events + at;

                        if (e->type == LINK_PAD_EV_SYN && e->code == LINK_PAD_SYN_DROPPED)
                                pad->dropped = true;
                        else if (e->type == LINK_PAD_EV_SYN && e->code == LINK_PAD_SYN_REPORT)
                        {
                                if (pad->dropped)
                                {
                                        link_pad_resync(pad);
                                        pad->dropped = false;
                                        pad->changed = true;
                                }
                                if (pad->changed)
                                {
                                        pad->stamp = (p64)e->seconds * 1000000ull + (p64)e->microseconds;
                                        link_pad_post(s, now, true);
                                }
                        }
                        else if (pad->dropped)
                                continue;
                        else if (e->type == LINK_PAD_EV_KEY && e->code < LINK_PAD_KEYS &&
                                 pad->key_at[e->code] != 0xffff)
                        {
                                p16 index = pad->key_at[e->code];
                                p8 bit = (p8)(1u << (index % 8));
                                p8 was = pad->down[index / 8];

                                pad->down[index / 8] = e->value ? (p8)(was | bit) : (p8)(was & ~bit);
                                pad->changed |= pad->down[index / 8] != was;
                        }
                        else if (e->type == LINK_PAD_EV_ABS && e->code < LINK_PAD_AXES &&
                                 pad->axis_at[e->code] != 0xff && pad->value[pad->axis_at[e->code]] != e->value)
                        {
                                pad->value[pad->axis_at[e->code]] = e->value;
                                pad->changed = true;
                        }
                }
        }
}

/*      A state the machine took: what it changes written to the pad in one
        write, the report's SYN_REPORT last, and the delay from the sender's
        kernel stamp to that write counted when the report is a newer one
        (a keepalive that changes nothing writes nothing). */
static fn link_pad_write(struct link_pad address_to pad, const b32 address_to value,
                         const p8 address_to down, p64 stamp)
{
        static link_pad_event report[LINK_PAD_AXES + LINK_PAD_KEYS + 1];
        positive count = 0;

        for (positive axis = 0; axis < pad->axes; axis++)
                if (value[axis] != pad->value[axis])
                {
                        report[count++] = (link_pad_event){0, 0, LINK_PAD_EV_ABS, pad->axis[axis], value[axis]};
                        pad->value[axis] = value[axis];
                }
        for (positive byte = 0; byte < ((positive)pad->keys + 7) / 8; byte++)
        {
                p8 differ = down[byte] ^ pad->down[byte];

                for (positive bit = 0; differ && bit < 8; bit++)
                        if (differ >> bit & 1)
                                report[count++] = (link_pad_event){0, 0, LINK_PAD_EV_KEY,
                                                                   pad->key[byte * 8 + bit], down[byte] >> bit & 1};
                pad->down[byte] = down[byte];
        }
        if (!count)
                return;
        report[count++] = (link_pad_event){0, 0, LINK_PAD_EV_SYN, LINK_PAD_SYN_REPORT, 0};
        if (system_write_once(pad->fd, report, count * sizeof report[0]) < 0)
                return;
        pad->note.reports++;
        if (stamp && stamp > pad->stamp)
        {
                link_pad_note_delay(address_of pad->note, (b64)link_now() - (b64)stamp + pad->offset);
                pad->stamp = stamp;
        }
}

// Silence: every button up and every axis at rest, until a state comes again.
static fn link_pad_release(struct link_pad address_to pad)
{
        p8 up[LINK_PAD_KEYS / 8];

        memory_zero(up, sizeof up);
        link_pad_write(pad, pad->rest, up, 0);
        pad->released = true;
        pad->note.state = LINK_PAD_QUIET;
}

static fn link_pad_take_state(struct link_pad address_to pad, const p8 address_to frame,
                              positive length, p64 now)
{
        b32 value[LINK_PAD_AXES];
        p32 sequence;

        if (length != link_pad_state_length(pad))
                return;
        sequence = memory_load_unaligned(p32, frame);
        if (sequence <= pad->sequence)
                return;
        pad->sequence = sequence;
        pad->heard = now;
        pad->released = false;
        pad->note.state = LINK_PAD_SENDING;
        for (positive axis = 0; axis < pad->axes; axis++)
                value[axis] = memory_load_unaligned(b32, frame + LINK_PAD_STATE_HEAD + 4 * axis);
        link_pad_write(pad, value, frame + LINK_PAD_STATE_HEAD + 4 * (positive)pad->axes,
                       memory_load_unaligned(p64, frame + 4));
}

/*      The clock: the machine asks with its time, the sender answers with
        the ask's time and its own, at once; the offset is the sender's clock
        less the machine's, from the ask whose round trip was shortest, which
        holds it to within half of that. Kept until a shorter one comes, or
        it is half a minute old. */
static fn link_pad_clock_heard(struct link_pad address_to pad, const p8 address_to frame,
                               positive length, p64 now)
{
        p64 asked;
        p64 told;
        p64 rtt;

        if (length != 16)
                return;
        asked = memory_load_unaligned(p64, frame);
        told = memory_load_unaligned(p64, frame + 8);
        if (asked > now || now - asked > 4 * LINK_PAD_PING)
                return;
        rtt = now - asked;
        if (pad->offset_at && rtt > pad->offset_rtt && link_age(now, pad->offset_at) < 30000000ull)
                return;
        pad->offset = (b64)told - (b64)(asked + rtt / 2);
        pad->offset_rtt = rtt;
        pad->offset_at = now;
        pad->note.offset = pad->offset;
        pad->note.rtt = rtt;
}

// A force the machine's pad was given, played on the real one.
static fn link_pad_force(struct link_pad address_to pad, const p8 address_to frame, positive length)
{
        if (frame[0] == 'U' && length == 2 + sizeof(link_pad_effect) && frame[1] < pad->effects)
        {
                link_pad_effect effect;

                memory_copy(address_of effect, frame + 2, sizeof effect);
                if (effect.type == LINK_PAD_FF_PERIODIC && (p16)effect.u[0] == LINK_PAD_FF_CUSTOM)
                        return;
                effect.id = pad->effect[frame[1]];
                if (system_control(pad->fd, LINK_PAD_EVIOCSFF, address_of effect) >= 0)
                        pad->effect[frame[1]] = effect.id;
        }
        else if (frame[0] == 'E' && length == 2 && frame[1] < pad->effects && pad->effect[frame[1]] >= 0)
        {
                (void)system_control(pad->fd, LINK_PAD_EVIOCRMFF, (positive)(bipolar)pad->effect[frame[1]]);
                pad->effect[frame[1]] = -1;
        }
        else if (frame[0] == 'F' && length == 7)
        {
                p16 code = memory_load_unaligned(p16, frame + 1);
                link_pad_event play = {0, 0, LINK_PAD_EV_FF, code, memory_load_unaligned(b32, frame + 3)};

                //      An effect's play is by the id the far pad gave it; a
                //      gain or an autocentre (FF_GAIN, FF_AUTOCENTER) as it is.
                if (code < pad->effects && pad->effect[code] >= 0)
                        play.code = (p16)pad->effect[code];
                else if (code < LINK_PAD_EFFECTS || code >= LINK_PAD_FORCES)
                        return;
                (void)system_write_once(pad->fd, address_of play, sizeof play);
        }
}

// A frame on one of the pad's keys, at either end.
static fn link_pad_hear(struct link_session address_to s, p8 key, const p8 address_to payload,
                        positive length)
{
        struct link_pad address_to pad = s->pad;
        p64 now = link_now();

        if (!pad->begun)
                return;
        if (!pad->sender && key == LINK_KEY_PAD && payload[0] == 'S')
                link_pad_take_state(pad, payload + 1, length - 1, now);
        else if (!pad->sender && key == LINK_KEY_PAD_CLOCK && payload[0] == 'Q')
                link_pad_clock_heard(pad, payload + 1, length - 1, now);
        else if (pad->sender && key == LINK_KEY_PAD_FORCE && pad->forces)
                link_pad_force(pad, payload, length);
        else if (pad->sender && key == LINK_KEY_PAD_CLOCK && payload[0] == 'P' && length == 9)
        {
                p8 answer[16];

                memory_copy(answer, payload + 1, 8);
                memory_store_unaligned(p64, answer + 8, now);
                pad->heard = now;
                link_self.now = now;
                (void)link_post(s, LINK_KEY_PAD_CLOCK, WATERLINK_FRAME_REPLACEABLE | WATERLINK_FRAME_URGENT,
                                'Q', answer, sizeof answer);
        }
}

/*      What a game asked of the pad made here, sent to the real one: an
        upload is answered here at once (the game's ioctl waits on it), and
        what it carried goes with its id; an erase the same; a play as it is. */
static fn link_pad_forces_read(struct link_session address_to s)
{
        struct link_pad address_to pad = s->pad;
        link_pad_event events[16];

        while (!pad->quiet)
        {
                bipolar got = system_read_once(pad->fd, events, sizeof events);

                //      A uinput that cannot be read is not watched again: its
                //      wait would answer at once, every turn.
                if (got <= 0)
                {
                        pad->quiet = got != -EINTR;
                        if (got != -EINTR && got != -EAGAIN)
                                pad->forces = false;
                        continue;
                }
                for (positive at = 0; at < (positive)got / sizeof events[0]; at++)
                {
                        link_pad_event address_to e = events + at;
                        p8 frame[1 + sizeof(link_pad_effect)];

                        if (e->type == LINK_PAD_EV_UINPUT && e->code == LINK_PAD_UI_FF_UPLOAD)
                        {
                                link_pad_upload upload;

                                memory_zero(address_of upload, sizeof upload);
                                upload.request = (p32)e->value;
                                if (system_control(pad->fd, LINK_PAD_UI_BEGIN_FF_UPLOAD, address_of upload) < 0)
                                        continue;
                                //      A custom waveform's samples are behind a pointer of the game's.
                                if (upload.effect.type == LINK_PAD_FF_PERIODIC)
                                        upload.effect.u[2] = upload.effect.u[3] = 0;
                                frame[0] = (p8)upload.effect.id;
                                memory_copy(frame + 1, address_of upload.effect, sizeof upload.effect);
                                upload.answer = upload.effect.id >= 0 && upload.effect.id < pad->effects &&
                                                                link_post(s, LINK_KEY_PAD_FORCE,
                                                                          WATERLINK_FRAME_DURABLE | WATERLINK_FRAME_URGENT,
                                                                          'U', frame, sizeof frame)
                                                        ? 0
                                                        : -EBUSY;
                                (void)system_control(pad->fd, LINK_PAD_UI_END_FF_UPLOAD, address_of upload);
                        }
                        else if (e->type == LINK_PAD_EV_UINPUT && e->code == LINK_PAD_UI_FF_ERASE)
                        {
                                link_pad_erase erase = {(p32)e->value, 0, 0};

                                if (system_control(pad->fd, LINK_PAD_UI_BEGIN_FF_ERASE, address_of erase) < 0)
                                        continue;
                                frame[0] = (p8)erase.effect;
                                (void)link_post(s, LINK_KEY_PAD_FORCE, WATERLINK_FRAME_DURABLE | WATERLINK_FRAME_URGENT,
                                                'E', frame, 1);
                                erase.answer = 0;
                                (void)system_control(pad->fd, LINK_PAD_UI_END_FF_ERASE, address_of erase);
                        }
                        else if (e->type == LINK_PAD_EV_FF)
                        {
                                memory_store_unaligned(p16, frame, e->code);
                                memory_store_unaligned(b32, frame + 2, e->value);
                                (void)link_post(s, LINK_KEY_PAD_FORCE, WATERLINK_FRAME_DURABLE | WATERLINK_FRAME_URGENT,
                                                'F', frame, 6);
                        }
                }
        }
}

// The sender's note, written where it stands (its lock is on this file), at most once a second.
static fn link_pad_note_out(struct link_pad address_to pad, p64 now, p8 now_too)
{
        if (pad->handle < 0 || (!now_too && link_age(now, pad->noted) < 1000000))
                return;
        pad->noted = now;
        (void)system_call_4(syscall(pwrite64), (positive)pad->handle, (positive)address_of pad->note,
                            sizeof pad->note, 0);
}

/*      The listener that makes a pad runs at SCHED_FIFO while it makes one,
        and reset on fork: a command it starts runs as it always did. */
static positive link_pad_made;

static fn link_pad_priority(b32 policy, b32 priority)
{
        b32 parameter = priority;

        (void)system_call_3(syscall(sched_setscheduler), 0, (positive)(policy | 0x40000000),
                            (positive)address_of parameter);
}

/*      What the starter of a sender waits on: the outcome, once, 'O' for a
        pad sent, 'N' for one that will not be, 'W' for a machine that has not
        answered yet, then the words, and the pipe is closed. */
static fn link_pad_tell(struct link_pad address_to pad, p8 how, string_address words)
{
        p8 line[200];
        positive length = string_length(words);

        if (pad->ready < 0)
                return;
        if (length > sizeof line - 1)
                length = sizeof line - 1;
        line[0] = how;
        memory_copy(line + 1, words, length);
        (void)system_write_all((positive)pad->ready, line, length + 1);
        system_close(pad->ready);
        pad->ready = -1;
}

/*      The sender's pad once the machine made one like it: taken from this
        machine's desktop and games (EVIOCGRAB), read again whole, and its
        state sent. A pad another program has taken is not sent. */
static fn link_pad_begin(struct link_session address_to s, p64 now)
{
        struct link_pad address_to pad = s->pad;
        link_pad_event drain[64];
        p8 said[200];

        if (system_control(pad->fd, LINK_PAD_EVIOCGRAB, 1) < 0)
        {
                string_copy_bounded((string_address)pad->note.why, "another program here has taken the pad",
                                    sizeof pad->note.why);
                pad->note.state = LINK_PAD_REFUSED;
                pad->gone = true;
                link_pad_tell(pad, 'N', (string_address)pad->note.why);
                return;
        }
        while (system_read_once(pad->fd, drain, sizeof drain) > 0)
                ;
        link_pad_resync(pad);
        pad->begun = true;
        pad->quiet = pad->dropped = pad->deaf = false;
        pad->heard = now;
        pad->stamp = now;
        pad->sequence = 0;
        link_pad_post(s, now, false);
        pad->note.state = LINK_PAD_SENDING;
        pad->note.why[0] = 0;
        link_pad_note_out(pad, now, true);
        string_copy_bounded((string_address)said, (string_address)pad->name, sizeof said);
        string_append_bounded((string_address)said, " is sent to ", sizeof said);
        string_append_bounded((string_address)said, (string_address)pad->note.machine, sizeof said);
        if (pad->far_node[0])
        {
                string_append_bounded((string_address)said, ", which made it ", sizeof said);
                string_append_bounded((string_address)said, (string_address)pad->far_node, sizeof said);
        }
        link_pad_tell(pad, 'O', (string_address)said);
}

// A turn of a pad session at either end, from link_session_streams.
static fn link_pad_turn(struct link_session address_to s, p64 now)
{
        struct link_pad address_to pad = s->pad;

        if (pad->sender && !pad->begun && pad->answered)
                link_pad_begin(s, now);
        if (!pad->begun)
                return;
        if (pad->sender)
        {
                link_pad_read(s, now);
                if (!pad->changed && link_age(now, pad->posted) >= LINK_PAD_KEEPALIVE)
                        link_pad_post(s, now, false);
                if (link_age(now, pad->heard) > LINK_PAD_DEAF)
                        pad->deaf = true;
                link_pad_note_out(pad, now, false);
                return;
        }
        link_pad_forces_read(s);
        if (!pad->released && link_age(now, pad->heard) > LINK_PAD_SILENCE)
                link_pad_release(pad);
        if (link_age(now, pad->pinged) >= LINK_PAD_PING)
        {
                p8 asked[8];

                pad->pinged = now;
                memory_store_unaligned(p64, asked, now);
                link_self.now = now;
                (void)link_post(s, LINK_KEY_PAD_CLOCK, WATERLINK_FRAME_REPLACEABLE | WATERLINK_FRAME_URGENT,
                                'P', asked, sizeof asked);
        }
}

// When the session's pad is next due: a keepalive, a release, an ask.
static p64 link_pad_due(struct link_session address_to s, p64 due)
{
        struct link_pad address_to pad = s->pad;
        p64 next;

        if (!pad || !pad->begun)
                return due;
        if (pad->sender)
                next = pad->posted + LINK_PAD_KEEPALIVE;
        else
        {
                next = pad->pinged + LINK_PAD_PING;
                if (!pad->released && pad->heard + LINK_PAD_SILENCE + 1 < next)
                        next = pad->heard + LINK_PAD_SILENCE + 1;
        }
        return next < due ? next : due;
}

/*      The machine's half of a request for a pad: the description judged,
        the pad made, and the listener raised. 0, or why not, which the far
        side is told. */
static string_address link_pad_take(struct link_session address_to s, const p8 address_to payload,
                                    positive length, string_address from)
{
        struct link_pad address_to pad;
        string_address why;
        bipolar mapped = system_call_6(syscall(mmap), 0, sizeof(struct link_pad),
                                       FILE_PROTECT_READ | FILE_PROTECT_WRITE,
                                       FILE_MAP_PRIVATE | FILE_MAP_ANONYMOUS, (positive)(bipolar)-1, 0);

        if (mapped < 0 && mapped > -4096)
                return "this machine has no memory for a pad";
        pad = (struct link_pad address_to)mapped;
        pad->fd = -1;
        pad->ready = pad->handle = -1;
        s->pad = pad;
        why = link_pad_read_description(pad, payload, length);
        if (!why)
                why = link_pad_make(pad, from);
        if (why)
                return why;
        pad->heard = pad->pinged = link_now();
        pad->pinged -= LINK_PAD_PING;
        string_copy_bounded((string_address)pad->note.machine, from, sizeof pad->note.machine);
        memory_copy(pad->note.name, pad->name, LINK_PAD_NAME);
        pad->note.vendor = pad->id.vendor;
        pad->note.product = pad->id.product;
        pad->note.state = LINK_PAD_SENDING;
        pad->note.since = pad->heard;
        if (!link_pad_made++)
                link_pad_priority(1, LINK_PAD_PRIORITY_MACHINE);
        return null;
}

// The pad of a session that ends: the one made here destroyed, the one sent from here let go.
static fn link_pad_close(struct link_session address_to s)
{
        struct link_pad address_to pad = s->pad;

        if (!pad)
                return;
        s->pad = null;
        if (pad->sender)
        {
                for (positive e = 0; e < LINK_PAD_EFFECTS; e++)
                        if (pad->effect[e] >= 0)
                        {
                                (void)system_control(pad->fd, LINK_PAD_EVIOCRMFF, (positive)(bipolar)pad->effect[e]);
                                pad->effect[e] = -1;
                        }
                if (pad->begun)
                        (void)system_control(pad->fd, LINK_PAD_EVIOCGRAB, 0);
                pad->begun = false;
                return;
        }
        if (pad->begun)
        {
                link_pad_release(pad);
                (void)system_control(pad->fd, LINK_PAD_UI_DEV_DESTROY, 0);
                if (!--link_pad_made)
                        link_pad_priority(0, 0);
        }
        if (pad->fd >= 0)
                system_close(pad->fd);
        system_call_2(syscall(munmap), (positive)pad, sizeof(struct link_pad));
        link_self.state_dirty = true;
}

/*      The pads this listener makes, for `link pad`: written with the state
        file, gone with the last pad, and gone at the first write of a
        listener that makes none (one before it may have died with some). */
#define LINK_PADS_PATH HOST_STATE "/link.pads"

static fn link_pads_write(void)
{
        static p8 written = true;
        link_pad_note notes[LINK_SESSIONS];
        positive count = 0;

        for (positive at = 0; at < LINK_SESSIONS; at++)
                if (link_self.session[at].used && link_self.session[at].pad &&
                    link_self.session[at].pad->begun)
                        notes[count++] = link_self.session[at].pad->note;
        if (!count && written)
                (void)system_remove_at(AT_FDCWD, LINK_PADS_PATH, 0);
        if (count)
                (void)link_file_replace(LINK_PADS_PATH, notes, count * sizeof notes[0], false);
        written = count != 0;
}

// The far side's yes to a sender: its node there, kept for the starter's line.
static fn link_pad_answered(struct link_pad address_to pad, const p8 address_to node, positive length)
{
        positive keep = length < sizeof pad->far_node - 1 ? length : sizeof pad->far_node - 1;

        for (positive at = 0; at < keep; at++)
                pad->far_node[at] = byte_is_printable(node[at]) ? node[at] : '?';
        pad->far_node[keep] = 0;
        pad->answered = true;
}

// The sender ------------------------------------------------------------------------

#define LINK_PAD_OUT HOST_STATE "/link.pad.out."

static b32 link_client_run(string_address name, p8 kind, string_address address_to words, positive count);
static bipolar link_signals_open(p8 resize);
static fn link_signals_close(bipolar handle);
static fn link_signals_take(bipolar handle, b32 address_to last);
static bipolar link_wait_readable(bipolar handle, timespec address_to limit);

/*      The input core, the CPU the display holds at a 2 us resume latency
        and gives the USB controllers' interrupts (compositor_input_core_take):
        a report woken there reads and sends with no wake across cores. -1
        when there is none, a machine of one CCD or no display. */
static bipolar link_pad_input_core(void)
{
        for (positive cpu = 0;; cpu++)
        {
                p8 path[96];
                p8 word[8];
                positive at;
                bipolar got;

                string_copy_bounded((string_address)path, "/sys/devices/system/cpu/cpu", sizeof path);
                at = string_length((string_address)path);
                at += positive_into_string(path + at, cpu);
                path[at] = 0;
                if (system_access_at(AT_FDCWD, (string_address)path, 0) < 0)
                        return -1;
                string_append_bounded((string_address)path, "/power/pm_qos_resume_latency_us", sizeof path);
                got = file_read_once_at(AT_FDCWD, (string_address)path, word, sizeof word - 1);
                if (got == 2 && word[0] == '2' && word[1] == '\n')
                        return (bipolar)cpu;
        }
}

static fn link_pad_pin(void)
{
        bipolar cpu = link_pad_input_core();

        if (cpu >= 0)
        {
                p64 mask[(positive)cpu / 64 + 1];

                memory_zero(mask, sizeof mask);
                mask[cpu / 64] = 1ull << (cpu % 64);
                (void)system_call_3(syscall(sched_setaffinity), 0, sizeof mask, (positive)mask);
        }
}

// An event node's name and path from a word: eventN or /dev/input/eventN. False for neither.
static p8 link_pad_node(string_address word, p8 address_to leaf, p8 address_to path)
{
        if (string_has_prefix(word, "/dev/input/"))
                word += 11;
        if (!string_has_prefix(word, "event") || !byte_is_digit(word[5]) || string_length(word) > 15 ||
            string_first_of(word, '/'))
                return false;
        string_copy_bounded((string_address)leaf, word, 16);
        string_copy_bounded((string_address)path, "/dev/input/", 32);
        string_append_bounded((string_address)path, word, 32);
        return true;
}

/*      Whether eventN is a pad that may be sent: described as a pad, and not
        one Waterlink made of another machine's (phys "waterlink/"), which
        sent on would be a loop. The first pad looked for (any_made false) is
        one plugged in, never one a program made: Steam's virtual pads are
        made from a pad that is here already. */
static string_address link_pad_probe(string_address leaf, string_address path, p8 any_made)
{
        p8 sys[64];
        p8 text[256];
        bipolar got;
        string_address why;

        string_copy_bounded((string_address)sys, "/sys/class/input/", sizeof sys);
        string_append_bounded((string_address)sys, leaf, sizeof sys);
        got = system_read_link_at(AT_FDCWD, sys, text, sizeof text - 1);
        text[got > 0 ? got : 0] = 0;
        if (!any_made && got > 0 && string_search((string_address)text, "/devices/virtual/"))
                return "it is a pad a program made, not one plugged in";
        string_append_bounded((string_address)sys, "/device/phys", sizeof sys);
        got = file_read_once_at(AT_FDCWD, (string_address)sys, text, sizeof text - 1);
        text[got > 0 ? got : 0] = 0;
        if (string_has_prefix((string_address)text, LINK_PAD_PHYS))
                return "it is a pad Waterlink made of another machine's";
        link_pad_out.fd = system_open_at(AT_FDCWD, path, FILE_READ_WRITE | O_NONBLOCK | O_CLOEXEC);
        if (link_pad_out.fd < 0)
                return "it does not open";
        why = link_pad_describe(address_of link_pad_out);
        system_close(link_pad_out.fd);
        link_pad_out.fd = -1;
        return why;
}

// The first pad plugged in: the event node of the lowest number that is one.
static p8 link_pad_first(p8 address_to leaf, p8 address_to path)
{
        file_walk walk;
        struct linux_dirent64 address_to entry;
        positive best = ~(positive)0;

        if (!file_walk_open(address_of walk, AT_FDCWD, "/dev/input"))
                return false;
        while ((entry = file_walk_next(address_of walk)))
        {
                p8 each_leaf[16], each_path[32];
                string_address at = (string_address)entry->d_name + 5;
                p8 over = false;
                positive number;

                if (!link_pad_node((string_address)entry->d_name, each_leaf, each_path))
                        continue;
                number = string_decimal_saturated(address_of at, ~(positive)0, address_of over);
                if (number < best && !link_pad_probe((string_address)each_leaf, (string_address)each_path, false))
                {
                        best = number;
                        string_copy_bounded((string_address)leaf, (string_address)each_leaf, 16);
                        string_copy_bounded((string_address)path, (string_address)each_path, 32);
                }
        }
        file_walk_close(address_of walk);
        return best != ~(positive)0;
}

/*      A wait between two tries that a stop ends: true when it was asked to
        stop. */
static p8 link_pad_nap(p64 microseconds)
{
        bipolar signals = link_signals_open(false);
        timespec limit = {(b64)(microseconds / 1000000), (b64)(microseconds % 1000000 * 1000)};
        b32 stopped = 0;

        if (signals >= 0 && link_wait_readable(signals, address_of limit) > 0)
                link_signals_take(signals, address_of stopped);
        link_signals_close(signals);
        return stopped != 0;
}

/*      The sender, detached: the pad opened, described, its note's file
        locked for as long as it runs, the loop raised and pinned, and the
        session to NAME made and made again while the pad is there and no
        stop came. A machine that refuses refuses for good; one that does not
        answer, or loses the session, is tried again, 1 s after, then 2, on
        to 30, with the pad let go in between. */
static b32 link_pad_daemon(string_address name, string_address leaf, string_address path)
{
        struct link_pad address_to pad = address_of link_pad_out;
        p8 note_path[64];
        p64 wait = 1000000;
        b32 monotonic = 1;
        string_address why;

        string_copy_bounded((string_address)note_path, LINK_PAD_OUT, sizeof note_path);
        string_append_bounded((string_address)note_path, leaf, sizeof note_path);
        pad->handle = host_lock_file((string_address)note_path, HOST_FILE_SETLK, null);
        if (pad->handle < 0)
        {
                link_pad_tell(pad, 'N', "it is sent already: moonwater link pad stop, then send it again");
                return 1;
        }
        (void)system_call_2(syscall(ftruncate), (positive)pad->handle, 0);
        pad->fd = system_open_at(AT_FDCWD, path, FILE_READ_WRITE | O_NONBLOCK | O_CLOEXEC);
        why = pad->fd < 0 ? (string_address) "it does not open" : link_pad_describe(pad);
        if (why)
        {
                link_pad_tell(pad, 'N', why);
                (void)system_remove_at(AT_FDCWD, (string_address)note_path, 0);
                return 1;
        }
        //      The kernel's stamps on the clock the link counts by.
        (void)system_control(pad->fd, LINK_PAD_EVIOCSCLOCKID, address_of monotonic);
        pad->sender = true;
        string_copy_bounded((string_address)pad->note.machine, name, sizeof pad->note.machine);
        memory_copy(pad->note.name, pad->name, LINK_PAD_NAME);
        string_copy_bounded((string_address)pad->note.node, leaf, sizeof pad->note.node);
        pad->note.vendor = pad->id.vendor;
        pad->note.product = pad->id.product;
        pad->note.sender = true;
        pad->note.state = LINK_PAD_TRYING;
        pad->note.since = link_now();
        link_pad_note_out(pad, pad->note.since, true);
        link_pad_priority(1, LINK_PAD_PRIORITY_SENDER);
        link_pad_pin();

        for (;;)
        {
                b32 answer;

                pad->answered = pad->gone = pad->deaf = false;
                pad->far_node[0] = 0;
                pad->sequence = 0;
                answer = link_client_run(name, LINK_KIND_PAD, null, 0);
                //      A refusal is for good, and so is a stop (128 and the signal).
                if (pad->note.state == LINK_PAD_REFUSED || pad->gone || (answer > 128 && answer < LINK_FAILED))
                        break;
                if (pad->sequence)
                        wait = 1000000;
                pad->note.state = LINK_PAD_TRYING;
                link_pad_note_out(pad, link_now(), true);
                {
                        p8 said[160];

                        string_copy_bounded((string_address)said, name, sizeof said);
                        string_append_bounded((string_address)said, " does not answer yet: the pad is sent once it does",
                                              sizeof said);
                        link_pad_tell(pad, 'W', (string_address)said);
                }
                if (link_pad_nap(wait))
                        break;
                wait = wait * 2 < 30000000 ? wait * 2 : 30000000;
        }
        link_pad_tell(pad, 'N', pad->note.why[0] ? (string_address)pad->note.why
                                : pad->gone                ? (string_address) "the pad went away"
                                                           : (string_address) "it stopped");
        (void)system_remove_at(AT_FDCWD, (string_address)note_path, 0);
        return 0;
}

/*      `link pad NAME [DEVICE]`: judged here, then sent by a process of its
        own, and this one waits for the first outcome to say it. */
static b32 link_pad_send(string_address name, string_address device)
{
        link_peers peers;
        p8 leaf[16], path[32], note_path[64];
        p8 line[201];
        b32 ends[2]; // pipe2's two ints
        timespec limit = {15, 0};
        bipolar got = 0;
        string_address why;

        link_peers_load(address_of peers);
        if (!link_peer_named(address_of peers, name))
                return host_refuse("no machine is called %s\n", name);
        if (device && !link_pad_node(device, leaf, path))
                return host_refuse("%s is not an input device: eventN, or /dev/input/eventN\n", device);
        if (!device && !link_pad_first(leaf, path))
                return host_refuse("no game pad is plugged in here\n");
        string_copy_bounded((string_address)note_path, LINK_PAD_OUT, sizeof note_path);
        string_append_bounded((string_address)note_path, (string_address)leaf, sizeof note_path);
        if (host_lock_owner((string_address)note_path) > 0)
                return host_refuse("%s is sent already: moonwater link pad stop, then send it again\n",
                                   (string_address)leaf);
        why = link_pad_probe((string_address)leaf, (string_address)path, true);
        if (why)
                return host_refuse("%s cannot be sent: %s\n", (string_address)leaf, why);
        if (system_pipe(ends, O_CLOEXEC) < 0)
                return host_fail("a pipe", -EMFILE);
        if (host_detach((p32)ends[1], descriptors_none))
        {
                link_pad_out.ready = ends[1];
                system_call_1(syscall(exit_group),
                              (positive)link_pad_daemon(name, (string_address)leaf, (string_address)path));
        }
        system_close(ends[1]);
        if (link_wait_readable(ends[0], address_of limit) > 0)
                got = system_read_retry((positive)ends[0], line, sizeof line - 1);
        system_close(ends[0]);
        line[got > 0 ? got : 0] = 0;
        if (got <= 0)
                return host_refuse("%s: the sender did not start\n", (string_address)leaf);
        if (line[0] == 'N')
                return host_refuse("%s is not sent to %s: %s\n", (string_address)leaf, name,
                                   (string_address)line + 1);
        host_say(log, host_label "%s (%s)\n", (string_address)line + 1, (string_address)leaf);
        return 0;
}

// Microseconds as the page says them.
static fn link_pad_delays(link_pad_note address_to note, string_address what)
{
        if (!note->delays)
                return;
        string_format(log, "; %s last %b us, mean %b us, least %b us, most %b us", what,
                      (bipolar)note->delay_last, (bipolar)(note->delay_sum / (b64)note->delays),
                      (bipolar)note->delay_least, (bipolar)note->delay_most);
}

static fn link_pad_say(link_pad_note address_to note, p64 now)
{
        static const string_address sent[] = {"", "trying", "sending", "quiet", "refused"};
        static const string_address made[] = {"", "", "live", "quiet, every button let go", ""};
        const string_address address_to states = note->sender ? sent : made;
        p8 vendor[5], product[5];

        host_hex4(vendor, note->vendor);
        host_hex4(product, note->product);
        if (note->sender)
                string_format(log, "  sent  %s (%s:%s, %s) to %s, %s for %p s: %p reports",
                              (string_address)note->name, (string_address)vendor, (string_address)product,
                              (string_address)note->node, (string_address)note->machine,
                              states[note->state < array_count(sent) ? note->state : 0],
                              (positive)(link_age(now, note->since) / 1000000), (positive)note->reports);
        else
                string_format(log, "  made  %s (%s:%s) from %s as %s, %s for %p s: %p reports",
                              (string_address)note->name, (string_address)vendor, (string_address)product,
                              (string_address)note->machine, (string_address)note->node,
                              states[note->state < array_count(made) ? note->state : 0],
                              (positive)(link_age(now, note->since) / 1000000), (positive)note->reports);
        link_pad_delays(note, note->sender ? (string_address) "kernel stamp to sent"
                                           : (string_address) "sender's kernel stamp to uinput write");
        if (!note->sender && note->rtt)
                string_format(log, "; clocks %b us apart, to within %p us", (bipolar)note->offset,
                              (positive)(note->rtt / 2));
        if (note->why[0])
                string_format(log, "; %s", (string_address)note->why);
        string_format(log, "\n");
}

// Each sender's note file, with whether its sender runs: a file nobody holds is a sender that died, and goes.
static positive link_pad_senders(p8 stop)
{
        file_walk walk;
        struct linux_dirent64 address_to entry;
        positive count = 0;
        p64 now = link_now();

        if (!file_walk_open(address_of walk, AT_FDCWD, HOST_STATE))
                return 0;
        while ((entry = file_walk_next(address_of walk)))
        {
                p8 path[96];
                link_pad_note note;
                bipolar got;
                b32 owner;

                if (!string_has_prefix((string_address)entry->d_name, "link.pad.out."))
                        continue;
                string_copy_bounded((string_address)path, HOST_STATE "/", sizeof path);
                string_append_bounded((string_address)path, (string_address)entry->d_name, sizeof path);
                owner = host_lock_owner((string_address)path);
                memory_zero(address_of note, sizeof note);
                got = file_read_once_at(AT_FDCWD, (string_address)path, (p8 address_to)address_of note, sizeof note);
                note.name[LINK_PAD_NAME - 1] = note.node[sizeof note.node - 1] = 0;
                note.machine[WATERLINK_NAME_MAX - 1] = note.why[sizeof note.why - 1] = 0;
                if (owner > 0 && stop)
                {
                        p64 until = now + 3000000;

                        (void)host_lock_signal((string_address)path, 15);
                        while (host_lock_owner((string_address)path) > 0 && link_now() < until)
                                host_pause(20000000);
                        host_say(log, host_label "%s (%s) is no longer sent to %s\n", (string_address)note.node,
                                 (string_address)note.name, (string_address)note.machine);
                        count++;
                }
                else if (owner > 0 && got == (bipolar)sizeof note)
                {
                        link_pad_say(address_of note, now);
                        count++;
                }
                if (owner <= 0 || stop)
                        (void)system_remove_at(AT_FDCWD, (string_address)path, 0);
        }
        file_walk_close(address_of walk);
        return count;
}

// `link pad`: what is sent from here and made here.
static b32 link_pad_list(void)
{
        static link_pad_note notes[LINK_SESSIONS];
        positive made = 0;
        positive sent;
        positive length = 0;
        p64 now = link_now();

        sent = link_pad_senders(false);
        if (host_lock_owner(LINK_LOCK_PATH) > 0 &&
            link_read_private_records(LINK_PADS_PATH, (p8 address_to)notes, sizeof notes, sizeof notes[0],
                                      address_of length) >= 0)
                for (; made < length / sizeof notes[0]; made++)
                {
                        notes[made].name[LINK_PAD_NAME - 1] = notes[made].node[sizeof notes[made].node - 1] = 0;
                        notes[made].machine[WATERLINK_NAME_MAX - 1] = notes[made].why[sizeof notes[made].why - 1] = 0;
                        link_pad_say(notes + made, now);
                }
        if (!sent && !made)
                host_say(log, host_label "no pad is sent from here or made here  next: moonwater link pad NAME\n");
        log_flush();
        return 0;
}

/*      `link pad`, `link pad stop`, `link pad NAME [DEVICE]`. */
static b32 link_pad_dispatch(string_address address_to arguments, positive count)
{
        if (count == 3)
                return link_pad_list();
        if (count == 4 && string_equals(arguments[3], "stop"))
        {
                if (!link_pad_senders(true))
                        host_say(log, host_label "no pad is sent from here\n");
                return 0;
        }
        return link_pad_send(arguments[3], count == 5 ? arguments[4] : null);
}

#else

static fn link_pad_turn(struct link_session address_to s, p64 now) { (void)s, (void)now; }
static p64 link_pad_due(struct link_session address_to s, p64 due) { (void)s; return due; }
static fn link_pad_hear(struct link_session address_to s, p8 key, const p8 address_to payload, positive length)
{
        (void)s, (void)key, (void)payload, (void)length;
}
static string_address link_pad_take(struct link_session address_to s, const p8 address_to payload,
                                    positive length, string_address from)
{
        (void)s, (void)payload, (void)length, (void)from;
        return "a Mac makes no pad";
}
static fn link_pad_close(struct link_session address_to s) { s->pad = null; }
static fn link_pads_write(void) {}
static fn link_pad_answered(struct link_pad address_to pad, const p8 address_to node, positive length)
{
        (void)pad, (void)node, (void)length;
}
static b32 link_pad_dispatch(string_address address_to arguments, positive count)
{
        (void)arguments, (void)count;
        return host_refuse("a Mac sends no pad and makes none: it has no evdev or uinput\n");
}

#endif // !MACOS

#endif // WATERLINK_PAD_INCLUDED
