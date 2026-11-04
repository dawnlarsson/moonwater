/*
        Moonwater Canvas, the X11 wire codec: the bytes the window manager of Xwayland sends to the X server and the bytes
        the server sends back, read and written, and nothing else.

        WHAT IT IS

        The compositor is the window manager of Xwayland, the rootless X server the Wayland shim runs. The window manager is
        the client of Xwayland's window manager connection. This file is the whole of what that connection says and hears:
        the setup, the requests the window manager makes (intern an atom, read and write a property, map, unmap, configure,
        focus, send a client message, read a geometry or the attributes of a window), and the replies, errors and events the
        server sends back. It does no I/O, allocates nothing and keeps no state. A caller gives it a buffer, and it answers
        with the number of bytes it wrote, or with the parsed fields, or with an error.

        THREAT MODEL

        Every byte the server sends, and every number in it, is hostile. A server that is not the one we expect, or a client
        that talks to us through it, may send anything:
        1. A parser checks the buffer's length before it reads a field, and checks each length field against the buffer
           before it uses the field. A message whose length field claims more than the buffer holds is refused: it is never
           read past.
        2. Every count and every length is compared with a named cap before it sizes anything. No count is used as an index
           before it is bounded.
        3. An encoder writes only inside the caller's buffer. When the message does not fit it answers XWM_ERR_SPACE and
           writes nothing.
        4. A reply or event whose type is not one this file reads is reported as unknown (its sequence and its type), and
           nothing past its 32 bytes is read.

        The byte order is little-endian throughout: the setup request asks for it (byte order 'l'), and a server answers in
        the order it was asked. Freestanding C: no library, no allocation, no globals beyond constants, and only int, long,
        char and unsigned values.
*/
#ifndef XWM_WIRE_INCLUDED
#define XWM_WIRE_INCLUDED

/* What the compositor calls, and what the checks reach: static, and unused where a build does not call them. */
#define XWM_API static __attribute__((unused))

/*
        The results. An encoder answers the number of bytes it wrote, which is more than zero. Every other function answers
        XWM_OK, or one of these.
*/
#define XWM_OK 0
#define XWM_ERR_SPACE (-1)  /* the caller's buffer is too small for what would be written */
#define XWM_ERR_ARG (-2)    /* a value the protocol does not allow in its field, or a null pointer */
#define XWM_ERR_SHORT (-3)  /* the buffer is shorter than the fixed part the function reads */
#define XWM_ERR_LENGTH (-4) /* a length field claims more or less than the buffer or the structure holds */
#define XWM_ERR_CAP (-5)    /* a count or a length is over its cap below */
#define XWM_ERR_TYPE (-6)   /* the message is not one this function reads */

/* The wire: the setup, and the sizes of what it carries. */
#define XWM_LSB 0x6c                 /* the byte order 'l': little-endian, the only one we ask for */
#define XWM_PROTOCOL_MAJOR 11
#define XWM_PROTOCOL_MINOR 0
#define XWM_SETUP_REQUEST 12         /* the setup request, with no authorization */
#define XWM_SETUP_HEADER 8           /* the setup reply's first eight bytes: status, and the length in units at 6 */
#define XWM_SETUP_FIXED 40           /* the setup reply's fixed part, up to the vendor string */
#define XWM_SETUP_MAX 65536          /* the whole setup reply, in bytes */
#define XWM_VENDOR_MAX 256           /* the vendor string, in bytes */
#define XWM_FORMATS_MAX 32           /* the pixmap formats the server lists */
#define XWM_SCREENS_MAX 4            /* the screens the server lists */
#define XWM_DEPTHS_MAX 32            /* the depths one screen lists */
#define XWM_VISUALS_MAX 4096         /* the visuals one screen lists, over all its depths: a GLX server lists more than 256, and a visual is 24 bytes of the setup reply, which XWM_SETUP_MAX bounds */
#define XWM_UNIT 4                   /* a length field counts four-byte units */
#define XWM_UNITS_MAX 65535          /* the most units a request's length field can hold */
#define XWM_HEADER 32                /* a reply, an error or an event: its fixed part, and the whole of an event */
#define XWM_EVENT_SIZE 32
#define XWM_REQUEST_SHORT 8          /* a request that is one window or one atom with no name: its fixed part */
#define XWM_PROPERTY_MAX 65536       /* the bytes of a property's value, in a request or in a reply */
#define XWM_ATOM_NAME_MAX 256        /* the bytes of an atom's name */
#define XWM_REPLY_MAX (XWM_HEADER + XWM_PROPERTY_MAX) /* the most of a reply or a GenericEvent taken whole */
#define XWM_DATA_LONGS 5             /* the data of a ClientMessage of format 32 */
#define XWM_CURRENT_TIME 0

/* The message types: the first byte of a reply, an error, or an event, with the send-event bit taken off. */
#define XWM_TYPE_MASK 0x7f
#define XWM_SEND_EVENT_BIT 0x80
#define XWM_ERROR 0
#define XWM_REPLY 1
#define XWM_EVENT_FIRST 2
#define XWM_EVENT_ENTER 7
#define XWM_EVENT_LEAVE 8
#define XWM_EVENT_FOCUS_IN 9
#define XWM_EVENT_FOCUS_OUT 10
#define XWM_EVENT_EXPOSE 12
#define XWM_EVENT_CREATE_NOTIFY 16
#define XWM_EVENT_DESTROY_NOTIFY 17
#define XWM_EVENT_UNMAP_NOTIFY 18
#define XWM_EVENT_MAP_NOTIFY 19
#define XWM_EVENT_MAP_REQUEST 20
#define XWM_EVENT_REPARENT_NOTIFY 21
#define XWM_EVENT_CONFIGURE_NOTIFY 22
#define XWM_EVENT_CONFIGURE_REQUEST 23
#define XWM_EVENT_PROPERTY_NOTIFY 28
#define XWM_EVENT_CLIENT_MESSAGE 33
#define XWM_EVENT_GENERIC 35
#define XWM_EVENT_LAST 35

/* The requests the window manager makes: the opcode in the first byte. */
#define XWM_OP_CREATE_WINDOW 1
#define XWM_OP_CHANGE_WINDOW_ATTRIBUTES 2
#define XWM_OP_GET_WINDOW_ATTRIBUTES 3
#define XWM_OP_DESTROY_WINDOW 4
#define XWM_OP_MAP_WINDOW 8
#define XWM_OP_UNMAP_WINDOW 10
#define XWM_OP_CONFIGURE_WINDOW 12
#define XWM_OP_GET_GEOMETRY 14
#define XWM_OP_INTERN_ATOM 16
#define XWM_OP_CHANGE_PROPERTY 18
#define XWM_OP_GET_PROPERTY 20
#define XWM_OP_SEND_EVENT 25
#define XWM_OP_SET_INPUT_FOCUS 42

/* QueryExtension and the Composite extension's requests: the major opcode of an extension is the one QueryExtension names. */
#define XWM_OP_QUERY_EXTENSION 98
#define XWM_EXT_NAME_MAX 64          /* the bytes of an extension's name */
#define XWM_EXT_MAJOR_FIRST 128      /* an extension's major opcode is at least this: the core's are below it */
#define XWM_COMPOSITE_QUERY_VERSION 0
#define XWM_COMPOSITE_REDIRECT_SUBWINDOWS 2
#define XWM_REDIRECT_AUTOMATIC 0
#define XWM_REDIRECT_MANUAL 1

/* The setup reply's status: the first byte. */
#define XWM_SETUP_FAILED 0
#define XWM_SETUP_SUCCESS 1
#define XWM_SETUP_AUTHENTICATE 2

/* The errors: the code in the second byte of an error. */
#define XWM_ERROR_REQUEST 1
#define XWM_ERROR_VALUE 2
#define XWM_ERROR_WINDOW 3
#define XWM_ERROR_PIXMAP 4
#define XWM_ERROR_ATOM 5
#define XWM_ERROR_CURSOR 6
#define XWM_ERROR_FONT 7
#define XWM_ERROR_MATCH 8
#define XWM_ERROR_DRAWABLE 9
#define XWM_ERROR_ACCESS 10
#define XWM_ERROR_ALLOC 11
#define XWM_ERROR_COLORMAP 12
#define XWM_ERROR_GCONTEXT 13
#define XWM_ERROR_ID_CHOICE 14
#define XWM_ERROR_NAME 15
#define XWM_ERROR_LENGTH 16
#define XWM_ERROR_IMPLEMENTATION 17

/* The window attributes that can be set (ChangeWindowAttributes), by their bit in the value mask. */
#define XWM_CW_EVENT_MASK 0x800u

/* The event masks a window selects. */
#define XWM_MASK_STRUCTURE_NOTIFY (1u << 17)
#define XWM_MASK_SUBSTRUCTURE_NOTIFY (1u << 19)
#define XWM_MASK_SUBSTRUCTURE_REDIRECT (1u << 20)
#define XWM_MASK_FOCUS_CHANGE (1u << 21)
#define XWM_MASK_PROPERTY_CHANGE (1u << 22)
#define XWM_MASK_ALL 0x01ffffffu     /* the bits a mask may have: the twenty-five the protocol defines */
#define XWM_MASK_WM_ROOT (XWM_MASK_SUBSTRUCTURE_REDIRECT | XWM_MASK_SUBSTRUCTURE_NOTIFY | XWM_MASK_PROPERTY_CHANGE | XWM_MASK_STRUCTURE_NOTIFY)

/* The value mask of a ConfigureWindow, and of a ConfigureRequest: one bit for each field, in the order the values come. */
#define XWM_CONFIGURE_X 0x01u
#define XWM_CONFIGURE_Y 0x02u
#define XWM_CONFIGURE_WIDTH 0x04u
#define XWM_CONFIGURE_HEIGHT 0x08u
#define XWM_CONFIGURE_BORDER 0x10u
#define XWM_CONFIGURE_SIBLING 0x20u
#define XWM_CONFIGURE_STACK_MODE 0x40u
#define XWM_CONFIGURE_ALL 0x7fu

/* The stack modes. */
#define XWM_STACK_ABOVE 0
#define XWM_STACK_BELOW 1
#define XWM_STACK_TOP_IF 2
#define XWM_STACK_BOTTOM_IF 3
#define XWM_STACK_OPPOSITE 4

/* The focus revert modes. */
#define XWM_REVERT_NONE 0
#define XWM_REVERT_POINTER_ROOT 1
#define XWM_REVERT_PARENT 2

/* The property modes, and the formats a property's value is in (the bits of each element). */
#define XWM_PROP_REPLACE 0
#define XWM_PROP_PREPEND 1
#define XWM_PROP_APPEND 2
#define XWM_FORMAT_8 8
#define XWM_FORMAT_16 16
#define XWM_FORMAT_32 32

/* The window classes, and the map states a window's attributes report. */
#define XWM_CLASS_COPY_FROM_PARENT 0
#define XWM_CLASS_INPUT_OUTPUT 1
#define XWM_CLASS_INPUT_ONLY 2
#define XWM_UNMAPPED 0
#define XWM_UNVIEWABLE 1
#define XWM_VIEWABLE 2

/* What an event is, as parse_event reads it. An event whose kind is XWM_EV_IGNORED is one the window manager knows and
   does not need (Expose, ReparentNotify): its code says which. An event of XWM_EV_UNKNOWN is any other, and only its
   code and sequence are set. */
#define XWM_EV_UNKNOWN 0
#define XWM_EV_IGNORED 1
#define XWM_EV_MAP_REQUEST 2
#define XWM_EV_CONFIGURE_REQUEST 3
#define XWM_EV_UNMAP_NOTIFY 4
#define XWM_EV_DESTROY_NOTIFY 5
#define XWM_EV_CONFIGURE_NOTIFY 6
#define XWM_EV_PROPERTY_NOTIFY 7
#define XWM_EV_CLIENT_MESSAGE 8
#define XWM_EV_CREATE_NOTIFY 9
#define XWM_EV_FOCUS_IN 10
#define XWM_EV_FOCUS_OUT 11
#define XWM_EV_ENTER 12
#define XWM_EV_LEAVE 13
#define XWM_EV_MAP_NOTIFY 14

/*
        What the parsers fill in. A field a kind does not have is zero after xwm_parse_event (it clears the whole struct
        first), and a caller reads only the fields its kind names.
*/
struct xwm_event
{
        int kind;               /* XWM_EV_*: what the event is */
        unsigned int code;      /* its type, with the send-event bit taken off */
        unsigned int synthetic; /* 1 when a client sent the event (the server sets the bit) */
        unsigned int sequence;
        unsigned int detail;    /* the byte after the type: a stack mode, a detail, a state's number, or a format */
        unsigned int event;     /* the window the event is reported to */
        unsigned int window;
        unsigned int parent;
        unsigned int sibling;
        unsigned int atom;      /* a property's atom, or a ClientMessage's type */
        unsigned int time;
        unsigned int state;     /* PropertyNotify: NewValue (0) or Deleted (1) */
        unsigned int format;    /* ClientMessage: 8, 16 or 32; the data is read only for 32 */
        unsigned int mode;      /* the mode of a focus or crossing event */
        unsigned int value_mask; /* ConfigureRequest: which of its fields the client asked to change */
        unsigned int override;  /* the override-redirect flag */
        unsigned int from_configure;
        int x, y;
        unsigned int width, height, border;
        unsigned int data[XWM_DATA_LONGS];
};

struct xwm_setup
{
        unsigned int status;        /* XWM_SETUP_FAILED, XWM_SETUP_SUCCESS or XWM_SETUP_AUTHENTICATE */
        unsigned int reason;        /* a failed setup: the length of its reason (the string is not copied) */
        unsigned int major, minor;
        unsigned int release;
        unsigned int resource_base; /* the ids the window manager may make are resource_base | (id & resource_mask) */
        unsigned int resource_mask;
        unsigned int motion_buffer;
        unsigned int vendor;        /* the vendor string's length, in bytes */
        unsigned int max_request;   /* the largest request the server takes, in four-byte units */
        unsigned int formats;
        unsigned int screens;
        unsigned int image_order, bitmap_order, scanline_unit, scanline_pad, min_keycode, max_keycode;
        /* the first screen */
        unsigned int root;          /* its root window: the one the window manager selects events on */
        unsigned int root_visual;
        unsigned int root_depth;
        unsigned int root_events;   /* its current input masks */
        unsigned int default_colormap, white_pixel, black_pixel;
        unsigned int width, height, width_mm, height_mm;
};

struct xwm_error
{
        unsigned int code, sequence, bad_value, minor, major;
};

struct xwm_atom_reply
{
        unsigned int sequence, atom;
};

struct xwm_extension_reply
{
        unsigned int sequence, present, major_opcode, first_event, first_error;
};

/* The value is a pointer into the buffer the caller passed, which the parser checked: it is valid while that buffer is. */
struct xwm_property_reply
{
        unsigned int sequence, format, type, bytes_after, elements;
        const unsigned char *value;
        unsigned long value_len;
};

struct xwm_geometry_reply
{
        unsigned int sequence, depth, root, width, height, border;
        int x, y;
};

struct xwm_attributes_reply
{
        unsigned int sequence, visual, window_class, bit_gravity, win_gravity, save_under, map_installed, map_state;
        unsigned int override, colormap, all_masks, your_mask, do_not_propagate;
};

/* The fields of a ConfigureWindow. Only the ones named in mask are sent. */
struct xwm_configure
{
        unsigned int mask;
        int x, y;
        unsigned int width, height, border, sibling, stack_mode;
};

/* ---- bytes ---- */

XWM_API unsigned int xwm_get16(const unsigned char *p)
{
        return (unsigned int)p[0] | (unsigned int)p[1] << 8;
}

XWM_API unsigned int xwm_get32(const unsigned char *p)
{
        return (unsigned int)p[0] | (unsigned int)p[1] << 8 | (unsigned int)p[2] << 16 | (unsigned int)p[3] << 24;
}

/* An INT16 field, as an int. */
XWM_API int xwm_get_i16(const unsigned char *p)
{
        unsigned int v = xwm_get16(p);

        return v >= 0x8000u ? (int)v - 0x10000 : (int)v;
}

XWM_API void xwm_put16(unsigned char *p, unsigned int v)
{
        p[0] = (unsigned char)(v & 0xffu);
        p[1] = (unsigned char)((v >> 8) & 0xffu);
}

XWM_API void xwm_put32(unsigned char *p, unsigned int v)
{
        xwm_put16(p, v & 0xffffu);
        xwm_put16(p + 2, (v >> 16) & 0xffffu);
}

XWM_API void xwm_zero(unsigned char *p, unsigned long n)
{
        for (unsigned long i = 0; i < n; i++)
                p[i] = 0;
}

XWM_API unsigned long xwm_pad4(unsigned long n)
{
        return (n + 3) & ~3ul;
}

/* ---- the encoders: each writes one request into out and answers its length, or an error; nothing is written on an error ---- */

/*
        Starts a request of `bytes` bytes (a multiple of four): checks it against the length field and the buffer, zeroes it,
        and writes its opcode and its length in units. The caller writes the rest.
*/
XWM_API long xwm_request_start(unsigned char *out, unsigned long cap, unsigned long bytes, unsigned int opcode)
{
        if (!out)
                return XWM_ERR_ARG;
        if (bytes / XWM_UNIT > XWM_UNITS_MAX)
                return XWM_ERR_CAP;
        if (bytes > cap)
                return XWM_ERR_SPACE;
        xwm_zero(out, bytes);
        out[0] = (unsigned char)opcode;
        xwm_put16(out + 2, (unsigned int)(bytes / XWM_UNIT));
        return (long)bytes;
}

/* The setup request: byte order, protocol 11.0, and no authorization. */
XWM_API long xwm_enc_setup_request(unsigned char *out, unsigned long cap)
{
        if (!out)
                return XWM_ERR_ARG;
        if (cap < XWM_SETUP_REQUEST)
                return XWM_ERR_SPACE;
        xwm_zero(out, XWM_SETUP_REQUEST);
        out[0] = XWM_LSB;
        xwm_put16(out + 2, XWM_PROTOCOL_MAJOR);
        xwm_put16(out + 4, XWM_PROTOCOL_MINOR);
        return XWM_SETUP_REQUEST;
}

/* InternAtom: the name's bytes, padded to a unit. */
XWM_API long xwm_enc_intern_atom(unsigned char *out, unsigned long cap, int only_if_exists, const unsigned char *name, unsigned long name_len)
{
        long n;

        if (!name || name_len == 0)
                return XWM_ERR_ARG;
        if (name_len > XWM_ATOM_NAME_MAX)
                return XWM_ERR_CAP;
        n = xwm_request_start(out, cap, XWM_REQUEST_SHORT + xwm_pad4(name_len), XWM_OP_INTERN_ATOM);
        if (n < 0)
                return n;
        out[1] = only_if_exists ? 1 : 0;
        xwm_put16(out + 4, (unsigned int)name_len);
        for (unsigned long i = 0; i < name_len; i++)
                out[XWM_REQUEST_SHORT + i] = name[i];
        return n;
}

/* GetProperty: the value's start and length are in units of its format, and the length is capped. */
XWM_API long xwm_enc_get_property(unsigned char *out, unsigned long cap, int delete_it, unsigned int window, unsigned int property, unsigned int type, unsigned int long_offset, unsigned int long_length)
{
        long n;

        if (long_length > XWM_PROPERTY_MAX / XWM_UNIT)
                return XWM_ERR_CAP;
        n = xwm_request_start(out, cap, 24, XWM_OP_GET_PROPERTY);
        if (n < 0)
                return n;
        out[1] = delete_it ? 1 : 0;
        xwm_put32(out + 4, window);
        xwm_put32(out + 8, property);
        xwm_put32(out + 12, type);
        xwm_put32(out + 16, long_offset);
        xwm_put32(out + 20, long_length);
        return n;
}

/* ChangeProperty: the data is the value's bytes, little-endian, in whole elements of the format. */
XWM_API long xwm_enc_change_property(unsigned char *out, unsigned long cap, unsigned int mode, unsigned int window, unsigned int property, unsigned int type, unsigned int format, const unsigned char *data, unsigned long data_len)
{
        unsigned long unit;
        long n;

        if (mode > XWM_PROP_APPEND)
                return XWM_ERR_ARG;
        if (format != XWM_FORMAT_8 && format != XWM_FORMAT_16 && format != XWM_FORMAT_32)
                return XWM_ERR_ARG;
        if (data_len > XWM_PROPERTY_MAX)
                return XWM_ERR_CAP;
        unit = format / 8;
        if (data_len % unit)
                return XWM_ERR_ARG;
        if (data_len && !data)
                return XWM_ERR_ARG;
        n = xwm_request_start(out, cap, 24 + xwm_pad4(data_len), XWM_OP_CHANGE_PROPERTY);
        if (n < 0)
                return n;
        out[1] = (unsigned char)mode;
        xwm_put32(out + 4, window);
        xwm_put32(out + 8, property);
        xwm_put32(out + 12, type);
        out[16] = (unsigned char)format;
        xwm_put32(out + 20, (unsigned int)(data_len / unit));
        for (unsigned long i = 0; i < data_len; i++)
                out[24 + i] = data[i];
        return n;
}

/* ChangeWindowAttributes with one value: the window's event mask. */
XWM_API long xwm_enc_select_events(unsigned char *out, unsigned long cap, unsigned int window, unsigned int event_mask)
{
        long n;

        if (event_mask & ~XWM_MASK_ALL)
                return XWM_ERR_ARG;
        n = xwm_request_start(out, cap, 16, XWM_OP_CHANGE_WINDOW_ATTRIBUTES);
        if (n < 0)
                return n;
        xwm_put32(out + 4, window);
        xwm_put32(out + 8, XWM_CW_EVENT_MASK);
        xwm_put32(out + 12, event_mask);
        return n;
}

/* CreateWindow with no values: depth 0 copies the parent's. The window manager's check window is one of these. */
XWM_API long xwm_enc_create_window(unsigned char *out, unsigned long cap, unsigned int depth, unsigned int window, unsigned int parent, int x, int y, unsigned int width, unsigned int height, unsigned int window_class, unsigned int visual)
{
        long n;

        if (depth > 255 || window == 0 || x < -32768 || x > 32767 || y < -32768 || y > 32767)
                return XWM_ERR_ARG;
        if (width == 0 || width > 65535 || height == 0 || height > 65535 || window_class > XWM_CLASS_INPUT_ONLY)
                return XWM_ERR_ARG;
        n = xwm_request_start(out, cap, 32, XWM_OP_CREATE_WINDOW);
        if (n < 0)
                return n;
        out[1] = (unsigned char)depth;
        xwm_put32(out + 4, window);
        xwm_put32(out + 8, parent);
        xwm_put16(out + 12, (unsigned int)x & 0xffffu);
        xwm_put16(out + 14, (unsigned int)y & 0xffffu);
        xwm_put16(out + 16, width);
        xwm_put16(out + 18, height);
        xwm_put16(out + 22, window_class);
        xwm_put32(out + 24, visual);
        return n;
}

/* The requests of one window, each eight bytes: MapWindow, UnmapWindow, DestroyWindow, GetGeometry, GetWindowAttributes. */
XWM_API long xwm_enc_window_op(unsigned char *out, unsigned long cap, unsigned int opcode, unsigned int window)
{
        long n;

        if (opcode != XWM_OP_MAP_WINDOW && opcode != XWM_OP_UNMAP_WINDOW && opcode != XWM_OP_DESTROY_WINDOW &&
                opcode != XWM_OP_GET_GEOMETRY && opcode != XWM_OP_GET_WINDOW_ATTRIBUTES)
                return XWM_ERR_ARG;
        n = xwm_request_start(out, cap, XWM_REQUEST_SHORT, opcode);
        if (n < 0)
                return n;
        xwm_put32(out + 4, window);
        return n;
}

/* ConfigureWindow: the value mask's bits, in order, each with its value as four bytes. */
XWM_API long xwm_enc_configure_window(unsigned char *out, unsigned long cap, unsigned int window, const struct xwm_configure *c)
{
        unsigned int mask, bit;
        unsigned long count = 0;
        long n;
        unsigned char *p;

        if (!c)
                return XWM_ERR_ARG;
        mask = c->mask;
        if (mask & ~XWM_CONFIGURE_ALL)
                return XWM_ERR_ARG;
        if ((mask & XWM_CONFIGURE_X) && (c->x < -32768 || c->x > 32767))
                return XWM_ERR_ARG;
        if ((mask & XWM_CONFIGURE_Y) && (c->y < -32768 || c->y > 32767))
                return XWM_ERR_ARG;
        if ((mask & XWM_CONFIGURE_WIDTH) && (c->width == 0 || c->width > 65535))
                return XWM_ERR_ARG;
        if ((mask & XWM_CONFIGURE_HEIGHT) && (c->height == 0 || c->height > 65535))
                return XWM_ERR_ARG;
        if ((mask & XWM_CONFIGURE_BORDER) && c->border > 65535)
                return XWM_ERR_ARG;
        if ((mask & XWM_CONFIGURE_STACK_MODE) && c->stack_mode > XWM_STACK_OPPOSITE)
                return XWM_ERR_ARG;
        for (bit = 0; bit < 7; bit++)
                count += (mask >> bit) & 1u;
        n = xwm_request_start(out, cap, 12 + XWM_UNIT * count, XWM_OP_CONFIGURE_WINDOW);
        if (n < 0)
                return n;
        xwm_put32(out + 4, window);
        xwm_put16(out + 8, mask);
        p = out + 12;
        if (mask & XWM_CONFIGURE_X)
                xwm_put32(p, (unsigned int)c->x), p += 4;
        if (mask & XWM_CONFIGURE_Y)
                xwm_put32(p, (unsigned int)c->y), p += 4;
        if (mask & XWM_CONFIGURE_WIDTH)
                xwm_put32(p, c->width), p += 4;
        if (mask & XWM_CONFIGURE_HEIGHT)
                xwm_put32(p, c->height), p += 4;
        if (mask & XWM_CONFIGURE_BORDER)
                xwm_put32(p, c->border), p += 4;
        if (mask & XWM_CONFIGURE_SIBLING)
                xwm_put32(p, c->sibling), p += 4;
        if (mask & XWM_CONFIGURE_STACK_MODE)
                xwm_put32(p, c->stack_mode), p += 4;
        return n;
}

XWM_API long xwm_enc_set_input_focus(unsigned char *out, unsigned long cap, unsigned int revert_to, unsigned int focus, unsigned int time)
{
        long n;

        if (revert_to > XWM_REVERT_PARENT)
                return XWM_ERR_ARG;
        n = xwm_request_start(out, cap, 12, XWM_OP_SET_INPUT_FOCUS);
        if (n < 0)
                return n;
        out[1] = (unsigned char)revert_to;
        xwm_put32(out + 4, focus);
        xwm_put32(out + 8, time);
        return n;
}

/* QueryExtension: the name's bytes, padded to a unit. The reply says whether the extension is there and its opcodes. */
XWM_API long xwm_enc_query_extension(unsigned char *out, unsigned long cap, const unsigned char *name, unsigned long name_len)
{
        long n;

        if (!name || name_len == 0)
                return XWM_ERR_ARG;
        if (name_len > XWM_EXT_NAME_MAX)
                return XWM_ERR_CAP;
        n = xwm_request_start(out, cap, XWM_REQUEST_SHORT + xwm_pad4(name_len), XWM_OP_QUERY_EXTENSION);
        if (n < 0)
                return n;
        xwm_put16(out + 4, (unsigned int)name_len);
        for (unsigned long i = 0; i < name_len; i++)
                out[XWM_REQUEST_SHORT + i] = name[i];
        return n;
}

/* Composite QueryVersion: twelve bytes, the client's version (the protocol's answer is the one the server can give). The major
   opcode is the one QueryExtension gave; an extension's opcode is at least XWM_EXT_MAJOR_FIRST. */
XWM_API long xwm_enc_composite_query_version(unsigned char *out, unsigned long cap, unsigned int major_opcode, unsigned int client_major, unsigned int client_minor)
{
        long n;

        if (major_opcode < XWM_EXT_MAJOR_FIRST || major_opcode > 255)
                return XWM_ERR_ARG;
        n = xwm_request_start(out, cap, 12, major_opcode);
        if (n < 0)
                return n;
        out[1] = XWM_COMPOSITE_QUERY_VERSION;
        xwm_put32(out + 4, client_major);
        xwm_put32(out + 8, client_minor);
        return n;
}

/* Composite RedirectSubwindows: the window's children are redirected to offscreen pixmaps, Manual (the window manager composes
   them) or Automatic. Twelve bytes. */
XWM_API long xwm_enc_composite_redirect_subwindows(unsigned char *out, unsigned long cap, unsigned int major_opcode, unsigned int window, unsigned int update)
{
        long n;

        if (major_opcode < XWM_EXT_MAJOR_FIRST || major_opcode > 255)
                return XWM_ERR_ARG;
        if (update != XWM_REDIRECT_AUTOMATIC && update != XWM_REDIRECT_MANUAL)
                return XWM_ERR_ARG;
        n = xwm_request_start(out, cap, 12, major_opcode);
        if (n < 0)
                return n;
        out[1] = XWM_COMPOSITE_REDIRECT_SUBWINDOWS;
        xwm_put32(out + 4, window);
        out[8] = (unsigned char)update;
        return n;
}

/*
        SendEvent: the 32 bytes of an event, which the server delivers to the destination. The event is one the protocol
        sends: a type from 2 to 35 (a reply or an error is never sent).
*/
XWM_API long xwm_enc_send_event(unsigned char *out, unsigned long cap, int propagate, unsigned int destination, unsigned int event_mask, const unsigned char *event, unsigned long event_len)
{
        unsigned int type;
        long n;

        if (!event || event_len != XWM_EVENT_SIZE || (event_mask & ~XWM_MASK_ALL))
                return XWM_ERR_ARG;
        type = event[0] & XWM_TYPE_MASK;
        if (type < XWM_EVENT_FIRST || type > XWM_EVENT_LAST)
                return XWM_ERR_ARG;
        n = xwm_request_start(out, cap, 12 + XWM_EVENT_SIZE, XWM_OP_SEND_EVENT);
        if (n < 0)
                return n;
        out[1] = propagate ? 1 : 0;
        xwm_put32(out + 4, destination);
        xwm_put32(out + 8, event_mask);
        for (unsigned long i = 0; i < XWM_EVENT_SIZE; i++)
                out[12 + i] = event[i];
        return n;
}

/*
        A ClientMessage of format 32, as the 32 bytes SendEvent carries: the window it is about, its type atom, and up to five
        data words (the rest are zero). Its sequence is zero: the server sets it for a client's events.
*/
XWM_API long xwm_enc_client_message(unsigned char *out, unsigned long cap, unsigned int window, unsigned int type, const unsigned int *data, unsigned long count)
{
        if (!out)
                return XWM_ERR_ARG;
        if (count > XWM_DATA_LONGS || (count && !data))
                return XWM_ERR_ARG;
        if (cap < XWM_EVENT_SIZE)
                return XWM_ERR_SPACE;
        xwm_zero(out, XWM_EVENT_SIZE);
        out[0] = XWM_EVENT_CLIENT_MESSAGE;
        out[1] = XWM_FORMAT_32;
        xwm_put32(out + 4, window);
        xwm_put32(out + 8, type);
        for (unsigned long i = 0; i < count; i++)
                xwm_put32(out + 12 + XWM_UNIT * i, data[i]);
        return XWM_EVENT_SIZE;
}

/* ---- framing: how long the message a server sent is, from its first 32 bytes ---- */

/* The type of a message, with the send-event bit taken off, or XWM_ERR_SHORT when it has no 32 bytes yet. */
XWM_API long xwm_message_type(const unsigned char *buf, unsigned long len)
{
        if (!buf)
                return XWM_ERR_ARG;
        if (len < XWM_HEADER)
                return XWM_ERR_SHORT;
        return (long)(buf[0] & XWM_TYPE_MASK);
}

/*
        The whole length of the next message from the server: 32 bytes, or for a reply and a GenericEvent 32 plus four times
        the length field. A length field that makes it more than XWM_REPLY_MAX is refused, so a caller never buffers more.
*/
XWM_API long xwm_server_message_size(const unsigned char *buf, unsigned long len)
{
        unsigned int type;
        unsigned long total;

        if (!buf)
                return XWM_ERR_ARG;
        if (len < XWM_HEADER)
                return XWM_ERR_SHORT;
        type = buf[0] & XWM_TYPE_MASK;
        if (type != XWM_REPLY && type != XWM_EVENT_GENERIC)
                return XWM_HEADER;
        total = XWM_HEADER + (unsigned long)XWM_UNIT * xwm_get32(buf + 4);
        if (total > XWM_REPLY_MAX)
                return XWM_ERR_CAP;
        return (long)total;
}

/* The whole length of the setup reply, from its first eight bytes. */
XWM_API long xwm_setup_size(const unsigned char *buf, unsigned long len)
{
        unsigned long total;

        if (!buf)
                return XWM_ERR_ARG;
        if (len < XWM_SETUP_HEADER)
                return XWM_ERR_SHORT;
        total = XWM_SETUP_HEADER + (unsigned long)XWM_UNIT * xwm_get16(buf + 6);
        if (total > XWM_SETUP_MAX)
                return XWM_ERR_CAP;
        return (long)total;
}

/* ---- the parsers: each reads a message the buffer holds whole, checks it, and fills its struct; nothing is read past len ---- */

/*
        A fixed-size reply: type 1, and a length field of `units`. The buffer must hold the whole reply.
*/
XWM_API int xwm_reply_fits(const unsigned char *buf, unsigned long len, unsigned long units)
{
        if (len < XWM_HEADER)
                return XWM_ERR_SHORT;
        if (buf[0] != XWM_REPLY)
                return XWM_ERR_TYPE;
        if (xwm_get32(buf + 4) != units)
                return XWM_ERR_LENGTH;
        if ((len - XWM_HEADER) / XWM_UNIT < units)
                return XWM_ERR_LENGTH;
        return XWM_OK;
}

XWM_API int xwm_parse_setup(const unsigned char *buf, unsigned long len, struct xwm_setup *out)
{
        long size_or_error;
        unsigned long size, p, vendor, formats;
        unsigned int screens, s;

        if (!buf || !out)
                return XWM_ERR_ARG;
        size_or_error = xwm_setup_size(buf, len);
        if (size_or_error < 0)
                return (int)size_or_error;
        size = (unsigned long)size_or_error;
        if (len < size)
                return XWM_ERR_LENGTH;
        out->status = buf[0];
        if (buf[0] == XWM_SETUP_FAILED)
        {
                out->reason = buf[1];
                out->major = xwm_get16(buf + 2);
                out->minor = xwm_get16(buf + 4);
                if (XWM_SETUP_HEADER + (unsigned long)buf[1] > size)
                        return XWM_ERR_LENGTH;
                return XWM_OK;
        }
        if (buf[0] == XWM_SETUP_AUTHENTICATE)
                return XWM_OK;
        if (buf[0] != XWM_SETUP_SUCCESS)
                return XWM_ERR_TYPE;
        if (size < XWM_SETUP_FIXED)
                return XWM_ERR_LENGTH;

        out->major = xwm_get16(buf + 2);
        out->minor = xwm_get16(buf + 4);
        out->release = xwm_get32(buf + 8);
        out->resource_base = xwm_get32(buf + 12);
        out->resource_mask = xwm_get32(buf + 16);
        out->motion_buffer = xwm_get32(buf + 20);
        out->vendor = xwm_get16(buf + 24);
        out->max_request = xwm_get16(buf + 26);
        screens = buf[28];
        out->formats = buf[29];
        out->screens = screens;
        out->image_order = buf[30];
        out->bitmap_order = buf[31];
        out->scanline_unit = buf[32];
        out->scanline_pad = buf[33];
        out->min_keycode = buf[34];
        out->max_keycode = buf[35];
        if (out->major != XWM_PROTOCOL_MAJOR)
                return XWM_ERR_TYPE;
        if (out->resource_mask == 0 || (out->resource_base & out->resource_mask))
                return XWM_ERR_ARG;
        if (out->vendor > XWM_VENDOR_MAX || out->formats > XWM_FORMATS_MAX)
                return XWM_ERR_CAP;
        if (screens == 0)
                return XWM_ERR_ARG;
        if (screens > XWM_SCREENS_MAX)
                return XWM_ERR_CAP;

        /* the vendor string, padded to a unit, then the formats (eight bytes each), then the screens */
        vendor = out->vendor;
        p = XWM_SETUP_FIXED + xwm_pad4(vendor);
        if (p > size)
                return XWM_ERR_LENGTH;
        formats = out->formats;
        if (size - p < 8 * formats)
                return XWM_ERR_LENGTH;
        p += 8 * formats;
        for (s = 0; s < screens; s++)
        {
                const unsigned char *sc;
                unsigned long depths, d, visuals = 0;

                if (size - p < 40)
                        return XWM_ERR_LENGTH;
                sc = buf + p;
                depths = sc[39];
                if (depths == 0)
                        return XWM_ERR_ARG;
                if (depths > XWM_DEPTHS_MAX)
                        return XWM_ERR_CAP;
                if (s == 0)
                {
                        out->root = xwm_get32(sc + 0);
                        out->default_colormap = xwm_get32(sc + 4);
                        out->white_pixel = xwm_get32(sc + 8);
                        out->black_pixel = xwm_get32(sc + 12);
                        out->root_events = xwm_get32(sc + 16);
                        out->width = xwm_get16(sc + 20);
                        out->height = xwm_get16(sc + 22);
                        out->width_mm = xwm_get16(sc + 24);
                        out->height_mm = xwm_get16(sc + 26);
                        out->root_visual = xwm_get32(sc + 32);
                        out->root_depth = sc[38];
                }
                p += 40;
                for (d = 0; d < depths; d++)
                {
                        unsigned long nv;

                        if (size - p < 8)
                                return XWM_ERR_LENGTH;
                        nv = xwm_get16(buf + p + 2);
                        visuals += nv;
                        if (visuals > XWM_VISUALS_MAX)
                                return XWM_ERR_CAP;
                        p += 8;
                        if (size - p < 24 * nv)
                                return XWM_ERR_LENGTH;
                        p += 24 * nv;
                }
        }
        if (p != size)
                return XWM_ERR_LENGTH;
        return XWM_OK;
}

/* An error: type 0, 32 bytes. */
XWM_API int xwm_parse_error(const unsigned char *buf, unsigned long len, struct xwm_error *out)
{
        if (!buf || !out)
                return XWM_ERR_ARG;
        if (len < XWM_HEADER)
                return XWM_ERR_SHORT;
        if (buf[0] != XWM_ERROR)
                return XWM_ERR_TYPE;
        out->code = buf[1];
        out->sequence = xwm_get16(buf + 2);
        out->bad_value = xwm_get32(buf + 4);
        out->minor = xwm_get16(buf + 8);
        out->major = buf[10];
        return XWM_OK;
}

XWM_API int xwm_parse_atom_reply(const unsigned char *buf, unsigned long len, struct xwm_atom_reply *out)
{
        int r;

        if (!buf || !out)
                return XWM_ERR_ARG;
        r = xwm_reply_fits(buf, len, 0);
        if (r != XWM_OK)
                return r;
        out->sequence = xwm_get16(buf + 2);
        out->atom = xwm_get32(buf + 8);
        return XWM_OK;
}

/*
        A GetProperty reply. The value is `elements` of the format, which is 0, 8, 16 or 32; its bytes are capped at
        XWM_PROPERTY_MAX, and the length field must be exactly the value's length in units, so the value sits whole in len.
*/
XWM_API int xwm_parse_property_reply(const unsigned char *buf, unsigned long len, struct xwm_property_reply *out)
{
        unsigned long unit, bytes, units;
        unsigned int format, elements;

        if (!buf || !out)
                return XWM_ERR_ARG;
        if (len < XWM_HEADER)
                return XWM_ERR_SHORT;
        if (buf[0] != XWM_REPLY)
                return XWM_ERR_TYPE;
        format = buf[1];
        elements = xwm_get32(buf + 16);
        if (format == 0)
        {
                if (elements)
                        return XWM_ERR_ARG;
                unit = 0;
        }
        else if (format == XWM_FORMAT_8 || format == XWM_FORMAT_16 || format == XWM_FORMAT_32)
        {
                unit = format / 8;
                if (elements > XWM_PROPERTY_MAX / unit)
                        return XWM_ERR_CAP;
        }
        else
                return XWM_ERR_ARG;
        bytes = (unsigned long)elements * unit;
        units = xwm_get32(buf + 4);
        if (units != (bytes + XWM_UNIT - 1) / XWM_UNIT)
                return XWM_ERR_LENGTH;
        if ((len - XWM_HEADER) / XWM_UNIT < units)
                return XWM_ERR_LENGTH;
        out->sequence = xwm_get16(buf + 2);
        out->format = format;
        out->type = xwm_get32(buf + 8);
        out->bytes_after = xwm_get32(buf + 12);
        out->elements = elements;
        out->value = buf + XWM_HEADER;
        out->value_len = bytes;
        return XWM_OK;
}

XWM_API int xwm_parse_geometry_reply(const unsigned char *buf, unsigned long len, struct xwm_geometry_reply *out)
{
        int r;

        if (!buf || !out)
                return XWM_ERR_ARG;
        r = xwm_reply_fits(buf, len, 0);
        if (r != XWM_OK)
                return r;
        out->sequence = xwm_get16(buf + 2);
        out->depth = buf[1];
        out->root = xwm_get32(buf + 8);
        out->x = xwm_get_i16(buf + 12);
        out->y = xwm_get_i16(buf + 14);
        out->width = xwm_get16(buf + 16);
        out->height = xwm_get16(buf + 18);
        out->border = xwm_get16(buf + 20);
        return XWM_OK;
}

XWM_API int xwm_parse_attributes_reply(const unsigned char *buf, unsigned long len, struct xwm_attributes_reply *out)
{
        int r;

        if (!buf || !out)
                return XWM_ERR_ARG;
        r = xwm_reply_fits(buf, len, 3);
        if (r != XWM_OK)
                return r;
        out->sequence = xwm_get16(buf + 2);
        out->visual = xwm_get32(buf + 8);
        out->window_class = xwm_get16(buf + 12);
        out->bit_gravity = buf[14];
        out->win_gravity = buf[15];
        out->save_under = buf[24];
        out->map_installed = buf[25];
        out->map_state = buf[26];
        out->override = buf[27];
        out->colormap = xwm_get32(buf + 28);
        out->all_masks = xwm_get32(buf + 32);
        out->your_mask = xwm_get32(buf + 36);
        out->do_not_propagate = xwm_get16(buf + 40);
        return XWM_OK;
}

/* A QueryExtension reply: 32 bytes, with no more. present is 0 or 1; the opcodes are the extension's (zero when it is absent). */
XWM_API int xwm_parse_extension_reply(const unsigned char *buf, unsigned long len, struct xwm_extension_reply *out)
{
        if (!buf || !out)
                return XWM_ERR_ARG;
        if (xwm_reply_fits(buf, len, 0) != XWM_OK)
                return xwm_reply_fits(buf, len, 0);
        if (buf[8] > 1)
                return XWM_ERR_ARG;
        out->sequence = xwm_get16(buf + 2);
        out->present = buf[8];
        out->major_opcode = buf[9];
        out->first_event = buf[10];
        out->first_error = buf[11];
        return XWM_OK;
}

/* Sets every field of an event to zero, and its kind to XWM_EV_UNKNOWN. */
XWM_API void xwm_event_clear(struct xwm_event *e)
{
        e->kind = XWM_EV_UNKNOWN;
        e->code = 0;
        e->synthetic = 0;
        e->sequence = 0;
        e->detail = 0;
        e->event = 0;
        e->window = 0;
        e->parent = 0;
        e->sibling = 0;
        e->atom = 0;
        e->time = 0;
        e->state = 0;
        e->format = 0;
        e->mode = 0;
        e->value_mask = 0;
        e->override = 0;
        e->from_configure = 0;
        e->x = 0;
        e->y = 0;
        e->width = 0;
        e->height = 0;
        e->border = 0;
        for (unsigned long i = 0; i < XWM_DATA_LONGS; i++)
                e->data[i] = 0;
}

/*
        An event: 32 bytes, of any type from 2 to 35. An event this file does not read is XWM_EV_UNKNOWN and still succeeds,
        so the caller can go on framing the stream; a reply (1) or an error (0) is not an event and is refused as a type.
*/
XWM_API int xwm_parse_event(const unsigned char *buf, unsigned long len, struct xwm_event *out)
{
        unsigned int type;

        if (!buf || !out)
                return XWM_ERR_ARG;
        if (len < XWM_HEADER)
                return XWM_ERR_SHORT;
        type = buf[0] & XWM_TYPE_MASK;
        if (type < XWM_EVENT_FIRST)
                return XWM_ERR_TYPE;
        xwm_event_clear(out);
        out->code = type;
        out->synthetic = (buf[0] & XWM_SEND_EVENT_BIT) ? 1 : 0;
        out->sequence = xwm_get16(buf + 2);
        out->detail = buf[1];
        switch (type)
        {
        case XWM_EVENT_MAP_REQUEST:
                out->kind = XWM_EV_MAP_REQUEST;
                out->parent = xwm_get32(buf + 4);
                out->window = xwm_get32(buf + 8);
                break;
        case XWM_EVENT_CONFIGURE_REQUEST:
                out->kind = XWM_EV_CONFIGURE_REQUEST;
                out->parent = xwm_get32(buf + 4);
                out->window = xwm_get32(buf + 8);
                out->sibling = xwm_get32(buf + 12);
                out->x = xwm_get_i16(buf + 16);
                out->y = xwm_get_i16(buf + 18);
                out->width = xwm_get16(buf + 20);
                out->height = xwm_get16(buf + 22);
                out->border = xwm_get16(buf + 24);
                out->value_mask = xwm_get16(buf + 26);
                break;
        case XWM_EVENT_MAP_NOTIFY:
                out->kind = XWM_EV_MAP_NOTIFY;
                out->event = xwm_get32(buf + 4);
                out->window = xwm_get32(buf + 8);
                out->override = buf[12];
                break;
        case XWM_EVENT_UNMAP_NOTIFY:
                out->kind = XWM_EV_UNMAP_NOTIFY;
                out->event = xwm_get32(buf + 4);
                out->window = xwm_get32(buf + 8);
                out->from_configure = buf[12];
                break;
        case XWM_EVENT_DESTROY_NOTIFY:
                out->kind = XWM_EV_DESTROY_NOTIFY;
                out->event = xwm_get32(buf + 4);
                out->window = xwm_get32(buf + 8);
                break;
        case XWM_EVENT_CONFIGURE_NOTIFY:
                out->kind = XWM_EV_CONFIGURE_NOTIFY;
                out->event = xwm_get32(buf + 4);
                out->window = xwm_get32(buf + 8);
                out->sibling = xwm_get32(buf + 12);
                out->x = xwm_get_i16(buf + 16);
                out->y = xwm_get_i16(buf + 18);
                out->width = xwm_get16(buf + 20);
                out->height = xwm_get16(buf + 22);
                out->border = xwm_get16(buf + 24);
                out->override = buf[26];
                break;
        case XWM_EVENT_PROPERTY_NOTIFY:
                out->kind = XWM_EV_PROPERTY_NOTIFY;
                out->window = xwm_get32(buf + 4);
                out->atom = xwm_get32(buf + 8);
                out->time = xwm_get32(buf + 12);
                out->state = buf[16];
                break;
        case XWM_EVENT_CLIENT_MESSAGE:
                out->kind = XWM_EV_CLIENT_MESSAGE;
                out->format = buf[1];
                if (out->format != XWM_FORMAT_8 && out->format != XWM_FORMAT_16 && out->format != XWM_FORMAT_32)
                        return XWM_ERR_ARG;
                out->window = xwm_get32(buf + 4);
                out->atom = xwm_get32(buf + 8);
                if (out->format == XWM_FORMAT_32)
                        for (unsigned long i = 0; i < XWM_DATA_LONGS; i++)
                                out->data[i] = xwm_get32(buf + 12 + XWM_UNIT * i);
                break;
        case XWM_EVENT_CREATE_NOTIFY:
                out->kind = XWM_EV_CREATE_NOTIFY;
                out->parent = xwm_get32(buf + 4);
                out->window = xwm_get32(buf + 8);
                out->x = xwm_get_i16(buf + 12);
                out->y = xwm_get_i16(buf + 14);
                out->width = xwm_get16(buf + 16);
                out->height = xwm_get16(buf + 18);
                out->border = xwm_get16(buf + 20);
                out->override = buf[22];
                break;
        case XWM_EVENT_FOCUS_IN:
        case XWM_EVENT_FOCUS_OUT:
                out->kind = type == XWM_EVENT_FOCUS_IN ? XWM_EV_FOCUS_IN : XWM_EV_FOCUS_OUT;
                out->event = xwm_get32(buf + 4);
                out->mode = buf[8];
                break;
        case XWM_EVENT_ENTER:
        case XWM_EVENT_LEAVE:
                out->kind = type == XWM_EVENT_ENTER ? XWM_EV_ENTER : XWM_EV_LEAVE;
                out->event = xwm_get32(buf + 8);
                out->mode = buf[26];
                break;
        case XWM_EVENT_EXPOSE:
        case XWM_EVENT_REPARENT_NOTIFY:
                out->kind = XWM_EV_IGNORED;
                break;
        default:
                break;
        }
        return XWM_OK;
}

#endif /* XWM_WIRE_INCLUDED */
