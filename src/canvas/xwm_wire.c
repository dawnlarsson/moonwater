/* Moonwater Canvas X11 wire codec: little-endian, no I/O, no allocation, no mutable globals, bounded reads. */
#ifndef XWM_WIRE_INCLUDED
#define XWM_WIRE_INCLUDED

#define XWM_API static __attribute__((unused))

enum { XWM_OK = 0, XWM_ERR_SPACE = -1, XWM_ERR_ARG = -2, XWM_ERR_SHORT = -3, XWM_ERR_LENGTH = -4, XWM_ERR_CAP = -5, XWM_ERR_TYPE = -6 };

/* Wire constants, caps, message types, opcodes, errors, and masks. */
#define XWM_LSB 0x6c
#define XWM_PROTOCOL_MAJOR 11
#define XWM_PROTOCOL_MINOR 0
#define XWM_SETUP_REQUEST 12
#define XWM_SETUP_HEADER 8
#define XWM_SETUP_FIXED 40
#define XWM_SETUP_MAX 65536
#define XWM_VENDOR_MAX 256
#define XWM_FORMATS_MAX 32
#define XWM_SCREENS_MAX 4
#define XWM_DEPTHS_MAX 32
#define XWM_VISUALS_MAX 4096
#define XWM_UNIT 4
#define XWM_UNITS_MAX 65535
#define XWM_HEADER 32
#define XWM_EVENT_SIZE 32
#define XWM_REQUEST_SHORT 8
#define XWM_PROPERTY_MAX 65536
#define XWM_ATOM_NAME_MAX 256
#define XWM_REPLY_MAX (XWM_HEADER + XWM_PROPERTY_MAX)
#define XWM_DATA_LONGS 5
#define XWM_CURRENT_TIME 0

#define XWM_TYPE_MASK 0x7f
#define XWM_SEND_EVENT_BIT 0x80
#define XWM_ERROR 0
#define XWM_REPLY 1
#define XWM_EVENT_FIRST 2
#define XWM_EVENT_KEY_PRESS 2
#define XWM_EVENT_KEY_RELEASE 3
#define XWM_EVENT_BUTTON_PRESS 4
#define XWM_EVENT_BUTTON_RELEASE 5
#define XWM_EVENT_MOTION_NOTIFY 6
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
#define XWM_EVENT_SELECTION_CLEAR 29
#define XWM_EVENT_SELECTION_REQUEST 30
#define XWM_EVENT_SELECTION_NOTIFY 31
#define XWM_EVENT_CLIENT_MESSAGE 33
#define XWM_EVENT_GENERIC 35
#define XWM_EVENT_LAST 35

#define XWM_OP_CREATE_WINDOW 1
#define XWM_OP_CHANGE_WINDOW_ATTRIBUTES 2
#define XWM_OP_GET_WINDOW_ATTRIBUTES 3
#define XWM_OP_DESTROY_WINDOW 4
#define XWM_OP_REPARENT_WINDOW 7
#define XWM_OP_MAP_WINDOW 8
#define XWM_OP_UNMAP_WINDOW 10
#define XWM_OP_CONFIGURE_WINDOW 12
#define XWM_OP_GET_GEOMETRY 14
#define XWM_OP_INTERN_ATOM 16
#define XWM_OP_GET_ATOM_NAME 17
#define XWM_OP_CHANGE_PROPERTY 18
#define XWM_OP_DELETE_PROPERTY 19
#define XWM_OP_GET_PROPERTY 20
#define XWM_OP_SET_SELECTION_OWNER 22
#define XWM_OP_CONVERT_SELECTION 24
#define XWM_OP_SEND_EVENT 25
#define XWM_OP_SET_INPUT_FOCUS 42
#define XWM_OP_QUERY_EXTENSION 98
#define XWM_EXT_NAME_MAX 64
#define XWM_EXT_MAJOR_FIRST 128
#define XWM_COMPOSITE_QUERY_VERSION 0
#define XWM_COMPOSITE_REDIRECT_SUBWINDOWS 2
#define XWM_REDIRECT_AUTOMATIC 0
#define XWM_REDIRECT_MANUAL 1
#define XWM_XRES_QUERY_VERSION 0
#define XWM_XRES_QUERY_CLIENT_IDS 4
#define XWM_XRES_LOCAL_CLIENT_PID 2u
#define XWM_XFIXES_QUERY_VERSION 0
#define XWM_XFIXES_SELECT_SELECTION_INPUT 2
#define XWM_XTEST_FAKE_INPUT 2
#define XWM_XTEST_KEY_PRESS 2
#define XWM_XTEST_KEY_RELEASE 3
#define XWM_XFIXES_SET_OWNER 1u
#define XWM_XFIXES_WINDOW_DESTROY 2u
#define XWM_XFIXES_CLIENT_CLOSE 4u
#define XWM_XFIXES_SELECTION_MASK 7u

#define XWM_SETUP_FAILED 0
#define XWM_SETUP_SUCCESS 1
#define XWM_SETUP_AUTHENTICATE 2

enum
{
        XWM_ERROR_REQUEST = 1, XWM_ERROR_VALUE, XWM_ERROR_WINDOW, XWM_ERROR_PIXMAP, XWM_ERROR_ATOM, XWM_ERROR_CURSOR,
        XWM_ERROR_FONT, XWM_ERROR_MATCH, XWM_ERROR_DRAWABLE, XWM_ERROR_ACCESS, XWM_ERROR_ALLOC, XWM_ERROR_COLORMAP,
        XWM_ERROR_GCONTEXT, XWM_ERROR_ID_CHOICE, XWM_ERROR_NAME, XWM_ERROR_LENGTH, XWM_ERROR_IMPLEMENTATION
};

#define XWM_CW_OVERRIDE_REDIRECT 0x200u
#define XWM_CW_EVENT_MASK 0x800u
#define XWM_CW_ALL 0x7fffu
#define XWM_MASK_STRUCTURE_NOTIFY (1u << 17)
#define XWM_MASK_SUBSTRUCTURE_NOTIFY (1u << 19)
#define XWM_MASK_SUBSTRUCTURE_REDIRECT (1u << 20)
#define XWM_MASK_FOCUS_CHANGE (1u << 21)
#define XWM_MASK_PROPERTY_CHANGE (1u << 22)
#define XWM_MASK_ALL 0x01ffffffu
#define XWM_MASK_WM_ROOT (XWM_MASK_SUBSTRUCTURE_REDIRECT | XWM_MASK_SUBSTRUCTURE_NOTIFY | XWM_MASK_PROPERTY_CHANGE | XWM_MASK_STRUCTURE_NOTIFY)

#define XWM_CONFIGURE_X 0x01u
#define XWM_CONFIGURE_Y 0x02u
#define XWM_CONFIGURE_WIDTH 0x04u
#define XWM_CONFIGURE_HEIGHT 0x08u
#define XWM_CONFIGURE_BORDER 0x10u
#define XWM_CONFIGURE_SIBLING 0x20u
#define XWM_CONFIGURE_STACK_MODE 0x40u
#define XWM_CONFIGURE_ALL 0x7fu

enum { XWM_STACK_ABOVE = 0, XWM_STACK_BELOW, XWM_STACK_TOP_IF, XWM_STACK_BOTTOM_IF, XWM_STACK_OPPOSITE };
enum { XWM_REVERT_NONE = 0, XWM_REVERT_POINTER_ROOT, XWM_REVERT_PARENT };
enum { XWM_PROP_REPLACE = 0, XWM_PROP_PREPEND, XWM_PROP_APPEND };
enum { XWM_FORMAT_8 = 8, XWM_FORMAT_16 = 16, XWM_FORMAT_32 = 32 };
enum { XWM_CLASS_COPY_FROM_PARENT = 0, XWM_CLASS_INPUT_OUTPUT, XWM_CLASS_INPUT_ONLY };
enum { XWM_UNMAPPED = 0, XWM_UNVIEWABLE, XWM_VIEWABLE };
enum
{
        XWM_EV_UNKNOWN = 0, XWM_EV_IGNORED, XWM_EV_MAP_REQUEST, XWM_EV_CONFIGURE_REQUEST, XWM_EV_UNMAP_NOTIFY,
        XWM_EV_DESTROY_NOTIFY, XWM_EV_CONFIGURE_NOTIFY, XWM_EV_PROPERTY_NOTIFY, XWM_EV_CLIENT_MESSAGE, XWM_EV_CREATE_NOTIFY,
        XWM_EV_FOCUS_IN, XWM_EV_FOCUS_OUT, XWM_EV_ENTER, XWM_EV_LEAVE, XWM_EV_MAP_NOTIFY, XWM_EV_SELECTION_CLEAR,
        XWM_EV_SELECTION_REQUEST, XWM_EV_SELECTION_NOTIFY, XWM_EV_XFIXES_SELECTION
};

struct xwm_event
{
        int kind;
        unsigned int code, synthetic, sequence, detail, event, window, parent, sibling;
        unsigned int atom, time, state, format, mode, value_mask, override, from_configure;
        unsigned int owner, requestor, selection, target, property, selection_time;
        int x, y;
        unsigned int width, height, border, data[XWM_DATA_LONGS];
};

struct xwm_setup
{
        unsigned int status, reason, major, minor, release, resource_base, resource_mask, motion_buffer, vendor, max_request;
        unsigned int formats, screens, image_order, bitmap_order, scanline_unit, scanline_pad, min_keycode, max_keycode;
        unsigned int root, root_visual, root_depth, root_events, default_colormap, white_pixel, black_pixel;
        unsigned int width, height, width_mm, height_mm;
};

struct xwm_error { unsigned int code, sequence, bad_value, minor, major; };
struct xwm_atom_reply { unsigned int sequence, atom; };
struct xwm_atom_name_reply { unsigned int sequence; const unsigned char *name; unsigned long name_len; };
struct xwm_extension_reply { unsigned int sequence, present, major_opcode, first_event, first_error; };
struct xwm_property_reply { unsigned int sequence, format, type, bytes_after, elements; const unsigned char *value; unsigned long value_len; };
struct xwm_geometry_reply { unsigned int sequence, depth, root, width, height, border; int x, y; };
struct xwm_attributes_reply
{
        unsigned int sequence, visual, window_class, bit_gravity, win_gravity, save_under, map_installed, map_state;
        unsigned int override, colormap, all_masks, your_mask, do_not_propagate;
};
struct xwm_configure { unsigned int mask; int x, y; unsigned int width, height, border, sibling, stack_mode; };

/* ---- byte helpers ---- */

XWM_API unsigned int xwm_get16(const unsigned char *p) { return (unsigned int)p[0] | ((unsigned int)p[1] << 8); }
XWM_API unsigned int xwm_get32(const unsigned char *p) { return xwm_get16(p) | (xwm_get16(p + 2) << 16); }
static int xwm_get_i16(const unsigned char *p) { unsigned int v = xwm_get16(p); return v >= 0x8000u ? (int)v - 0x10000 : (int)v; }
static void xwm_put16(unsigned char *p, unsigned int v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
XWM_API void xwm_put32(unsigned char *p, unsigned int v) { xwm_put16(p, v); xwm_put16(p + 2, v >> 16); }
static unsigned long xwm_pad4(unsigned long n) { return (n + 3ul) & ~3ul; }
static void xwm_copy_bytes(unsigned char *dst, const unsigned char *src, unsigned long n) { for (unsigned long i = 0; i < n; i++) dst[i] = src[i]; }

#ifdef WINDOW_INCLUDED
#define xwm_zero(p, n) memory_zero((p), (positive)(n))
#define xwm_name_is(name, len, word) windows_clip_same((const char *)(name), (len), (word))
#else
static void xwm_zero(void *p, unsigned long n) { for (unsigned long i = 0; i < n; i++) ((unsigned char *)p)[i] = 0; }
static int xwm_name_is(const unsigned char *name, unsigned long len, const char *word)
{
        unsigned long at = 0;

        for (; word[at]; at++)
        {
                unsigned char a = at < len ? name[at] : 0, b = (unsigned char)word[at];

                if ((a >= 'A' && a <= 'Z' ? (unsigned char)(a + 32) : a) != (b >= 'A' && b <= 'Z' ? (unsigned char)(b + 32) : b))
                        return 0;
        }
        return at == len;
}
#endif

/* ---- encoders ---- */

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

static long xwm_request_words(unsigned char *out, unsigned long cap, unsigned int opcode, unsigned char data,
                              const unsigned int *words, unsigned long count)
{
        long n = xwm_request_start(out, cap, 4 + XWM_UNIT * count, opcode);

        if (n < 0)
                return n;
        out[1] = data;
        for (unsigned long i = 0; i < count; i++)
                xwm_put32(out + 4 + XWM_UNIT * i, words[i]);
        return n;
}

static long xwm_ext_words(unsigned char *out, unsigned long cap, unsigned int major, unsigned char minor,
                          const unsigned int *words, unsigned long count)
{
        return (major < XWM_EXT_MAJOR_FIRST || major > 255) ? XWM_ERR_ARG : xwm_request_words(out, cap, major, minor, words, count);
}

static long xwm_request_name(unsigned char *out, unsigned long cap, unsigned int opcode, unsigned char data,
                             const unsigned char *name, unsigned long name_len, unsigned long max_len)
{
        long n;

        if (!name || !name_len)
                return XWM_ERR_ARG;
        if (name_len > max_len)
                return XWM_ERR_CAP;
        if ((n = xwm_request_start(out, cap, XWM_REQUEST_SHORT + xwm_pad4(name_len), opcode)) < 0)
                return n;
        out[1] = data;
        xwm_put16(out + 4, (unsigned int)name_len);
        xwm_copy_bytes(out + XWM_REQUEST_SHORT, name, name_len);
        return n;
}

static long xwm_event_words(unsigned char *out, unsigned long cap, unsigned char type, unsigned char detail,
                            const unsigned int *words, unsigned long count)
{
        if (!out)
                return XWM_ERR_ARG;
        if (cap < XWM_EVENT_SIZE)
                return XWM_ERR_SPACE;
        xwm_zero(out, XWM_EVENT_SIZE);
        out[0] = type, out[1] = detail;
        for (unsigned long i = 0; i < count; i++)
                xwm_put32(out + 4 + XWM_UNIT * i, words[i]);
        return XWM_EVENT_SIZE;
}

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

XWM_API long xwm_enc_intern_atom(unsigned char *out, unsigned long cap, int only_if_exists, const unsigned char *name, unsigned long name_len)
{
        return xwm_request_name(out, cap, XWM_OP_INTERN_ATOM, only_if_exists ? 1 : 0, name, name_len, XWM_ATOM_NAME_MAX);
}

XWM_API long xwm_enc_query_extension(unsigned char *out, unsigned long cap, const unsigned char *name, unsigned long name_len)
{
        return xwm_request_name(out, cap, XWM_OP_QUERY_EXTENSION, 0, name, name_len, XWM_EXT_NAME_MAX);
}

XWM_API long xwm_enc_get_property(unsigned char *out, unsigned long cap, int delete_it, unsigned int window, unsigned int property,
                                  unsigned int type, unsigned int long_offset, unsigned int long_length)
{
        unsigned int w[5] = {window, property, type, long_offset, long_length};

        return long_length > XWM_PROPERTY_MAX / XWM_UNIT ? XWM_ERR_CAP
                                                         : xwm_request_words(out, cap, XWM_OP_GET_PROPERTY, delete_it ? 1 : 0, w, 5);
}

XWM_API long xwm_enc_change_property(unsigned char *out, unsigned long cap, unsigned int mode, unsigned int window, unsigned int property,
                                     unsigned int type, unsigned int format, const unsigned char *data, unsigned long data_len)
{
        unsigned long unit;
        long n;

        if (mode > XWM_PROP_APPEND || (format != XWM_FORMAT_8 && format != XWM_FORMAT_16 && format != XWM_FORMAT_32))
                return XWM_ERR_ARG;
        if (data_len > XWM_PROPERTY_MAX)
                return XWM_ERR_CAP;
        unit = format / 8;
        if ((data_len % unit) || (data_len && !data))
                return XWM_ERR_ARG;
        if ((n = xwm_request_start(out, cap, 24 + xwm_pad4(data_len), XWM_OP_CHANGE_PROPERTY)) < 0)
                return n;
        out[1] = (unsigned char)mode, out[16] = (unsigned char)format;
        xwm_put32(out + 4, window), xwm_put32(out + 8, property), xwm_put32(out + 12, type);
        xwm_put32(out + 20, (unsigned int)(data_len / unit));
        xwm_copy_bytes(out + 24, data, data_len);
        return n;
}

XWM_API long xwm_enc_select_events(unsigned char *out, unsigned long cap, unsigned int window, unsigned int event_mask)
{
        unsigned int w[3] = {window, XWM_CW_EVENT_MASK, event_mask};

        return (event_mask & ~XWM_MASK_ALL) ? XWM_ERR_ARG : xwm_request_words(out, cap, XWM_OP_CHANGE_WINDOW_ATTRIBUTES, 0, w, 3);
}

XWM_API long xwm_enc_create_window(unsigned char *out, unsigned long cap, unsigned int depth, unsigned int window, unsigned int parent,
                                   int x, int y, unsigned int width, unsigned int height, unsigned int window_class, unsigned int visual)
{
        unsigned int w[7] = {window, parent, ((unsigned int)x & 0xffffu) | (((unsigned int)y & 0xffffu) << 16),
                             (width & 0xffffu) | ((height & 0xffffu) << 16), window_class << 16, visual, 0};

        if (depth > 255 || !window || x < -32768 || x > 32767 || y < -32768 || y > 32767 || !width || width > 65535 || !height ||
            height > 65535 || window_class > XWM_CLASS_INPUT_ONLY)
                return XWM_ERR_ARG;
        return xwm_request_words(out, cap, XWM_OP_CREATE_WINDOW, (unsigned char)depth, w, 7);
}

XWM_API long xwm_enc_window_op(unsigned char *out, unsigned long cap, unsigned int opcode, unsigned int window)
{
        if (opcode != XWM_OP_MAP_WINDOW && opcode != XWM_OP_UNMAP_WINDOW && opcode != XWM_OP_DESTROY_WINDOW &&
            opcode != XWM_OP_GET_GEOMETRY && opcode != XWM_OP_GET_WINDOW_ATTRIBUTES)
                return XWM_ERR_ARG;
        return xwm_request_words(out, cap, opcode, 0, &window, 1);
}

XWM_API long xwm_enc_reparent_window(unsigned char *out, unsigned long cap, unsigned int window, unsigned int parent, int x, int y)
{
        unsigned int w[3] = {window, parent, ((unsigned int)x & 0xffffu) | (((unsigned int)y & 0xffffu) << 16)};

        return xwm_request_words(out, cap, XWM_OP_REPARENT_WINDOW, 0, w, 3);
}

XWM_API long xwm_enc_configure_window(unsigned char *out, unsigned long cap, unsigned int window, const struct xwm_configure *c)
{
        unsigned int mask, vals[7], count = 0;
        long n;
        unsigned char *p;

        if (!c || (c->mask & ~XWM_CONFIGURE_ALL))
                return XWM_ERR_ARG;
        mask = c->mask;
        if (((mask & XWM_CONFIGURE_X) && (c->x < -32768 || c->x > 32767)) ||
            ((mask & XWM_CONFIGURE_Y) && (c->y < -32768 || c->y > 32767)) ||
            ((mask & XWM_CONFIGURE_WIDTH) && (!c->width || c->width > 65535)) ||
            ((mask & XWM_CONFIGURE_HEIGHT) && (!c->height || c->height > 65535)) ||
            ((mask & XWM_CONFIGURE_BORDER) && c->border > 65535) ||
            ((mask & XWM_CONFIGURE_STACK_MODE) && c->stack_mode > XWM_STACK_OPPOSITE))
                return XWM_ERR_ARG;
        for (unsigned int bit = 0; bit < 7; bit++)
                count += (mask >> bit) & 1u;
        if ((n = xwm_request_start(out, cap, 12 + XWM_UNIT * (unsigned long)count, XWM_OP_CONFIGURE_WINDOW)) < 0)
                return n;
        xwm_put32(out + 4, window), xwm_put16(out + 8, mask);
        vals[0] = (unsigned int)c->x, vals[1] = (unsigned int)c->y, vals[2] = c->width, vals[3] = c->height;
        vals[4] = c->border, vals[5] = c->sibling, vals[6] = c->stack_mode;
        p = out + 12;
        for (unsigned int bit = 0; bit < 7; bit++)
                if (mask & (1u << bit))
                        xwm_put32(p, vals[bit]), p += 4;
        return n;
}

XWM_API long xwm_enc_set_input_focus(unsigned char *out, unsigned long cap, unsigned int revert_to, unsigned int focus, unsigned int time)
{
        unsigned int w[2] = {focus, time};

        return revert_to > XWM_REVERT_PARENT ? XWM_ERR_ARG : xwm_request_words(out, cap, XWM_OP_SET_INPUT_FOCUS, (unsigned char)revert_to, w, 2);
}

XWM_API long xwm_enc_composite_query_version(unsigned char *out, unsigned long cap, unsigned int major, unsigned int cmaj, unsigned int cmin)
{
        unsigned int w[2] = {cmaj, cmin};

        return xwm_ext_words(out, cap, major, XWM_COMPOSITE_QUERY_VERSION, w, 2);
}

XWM_API long xwm_enc_composite_redirect_subwindows(unsigned char *out, unsigned long cap, unsigned int major, unsigned int window, unsigned int update)
{
        unsigned int w[2] = {window, update};

        return (update != XWM_REDIRECT_AUTOMATIC && update != XWM_REDIRECT_MANUAL)
                   ? XWM_ERR_ARG
                   : xwm_ext_words(out, cap, major, XWM_COMPOSITE_REDIRECT_SUBWINDOWS, w, 2);
}

XWM_API long xwm_enc_xres_client_pid(unsigned char *out, unsigned long cap, unsigned int major, unsigned int resource)
{
        unsigned int w[3] = {1, resource, XWM_XRES_LOCAL_CLIENT_PID};

        return !resource ? XWM_ERR_ARG : xwm_ext_words(out, cap, major, XWM_XRES_QUERY_CLIENT_IDS, w, 3);
}

XWM_API long xwm_enc_xres_query_version(unsigned char *out, unsigned long cap, unsigned int major, unsigned int cmaj, unsigned int cmin)
{
        unsigned int w = cmaj | (cmin << 8);

        return (cmaj > 255 || cmin > 255) ? XWM_ERR_ARG : xwm_ext_words(out, cap, major, XWM_XRES_QUERY_VERSION, &w, 1);
}

XWM_API long xwm_enc_xfixes_query_version(unsigned char *out, unsigned long cap, unsigned int major, unsigned int cmaj, unsigned int cmin)
{
        unsigned int w[2] = {cmaj, cmin};

        return xwm_ext_words(out, cap, major, XWM_XFIXES_QUERY_VERSION, w, 2);
}

XWM_API long xwm_enc_xfixes_select_selection_input(unsigned char *out, unsigned long cap, unsigned int major, unsigned int window,
                                                   unsigned int selection, unsigned int mask)
{
        unsigned int w[3] = {window, selection, mask};

        return (!window || !selection || (mask & ~XWM_XFIXES_SELECTION_MASK))
                   ? XWM_ERR_ARG
                   : xwm_ext_words(out, cap, major, XWM_XFIXES_SELECT_SELECTION_INPUT, w, 3);
}

XWM_API long xwm_enc_xtest_key(unsigned char *out, unsigned long cap, unsigned int major, unsigned int type, unsigned int detail)
{
        long n;

        if (major < XWM_EXT_MAJOR_FIRST || major > 255 || (type != XWM_XTEST_KEY_PRESS && type != XWM_XTEST_KEY_RELEASE) ||
            detail < 8 || detail > 255)
                return XWM_ERR_ARG;
        if ((n = xwm_request_start(out, cap, 36, major)) < 0)
                return n;
        out[1] = XWM_XTEST_FAKE_INPUT, out[4] = (unsigned char)type, out[5] = (unsigned char)detail;
        return n;
}

XWM_API long xwm_enc_send_event(unsigned char *out, unsigned long cap, int propagate, unsigned int destination, unsigned int event_mask,
                                const unsigned char *event, unsigned long event_len)
{
        unsigned int type;
        long n;

        if (!event || event_len != XWM_EVENT_SIZE || (event_mask & ~XWM_MASK_ALL))
                return XWM_ERR_ARG;
        type = event[0] & XWM_TYPE_MASK;
        if (type < XWM_EVENT_FIRST || type > XWM_EVENT_LAST)
                return XWM_ERR_ARG;
        if ((n = xwm_request_start(out, cap, 12 + XWM_EVENT_SIZE, XWM_OP_SEND_EVENT)) < 0)
                return n;
        out[1] = propagate ? 1 : 0;
        xwm_put32(out + 4, destination), xwm_put32(out + 8, event_mask);
        xwm_copy_bytes(out + 12, event, XWM_EVENT_SIZE);
        return n;
}

XWM_API long xwm_enc_client_message(unsigned char *out, unsigned long cap, unsigned int window, unsigned int type,
                                    const unsigned int *data, unsigned long count)
{
        unsigned int w[2] = {window, type};
        long n;

        if (count > XWM_DATA_LONGS || (count && !data))
                return XWM_ERR_ARG;
        if ((n = xwm_event_words(out, cap, XWM_EVENT_CLIENT_MESSAGE, XWM_FORMAT_32, w, 2)) < 0)
                return n;
        for (unsigned long i = 0; i < count; i++)
                xwm_put32(out + 12 + XWM_UNIT * i, data[i]);
        return n;
}

XWM_API long xwm_enc_input_event(unsigned char *out, unsigned long cap, unsigned int type, unsigned int detail, unsigned int time,
                                 unsigned int root, unsigned int window, int x, int y, unsigned int state)
{
        unsigned int xy = ((unsigned int)x & 0xffffu) | (((unsigned int)y & 0xffffu) << 16);
        unsigned int w[7] = {time, root, window, 0, xy, xy, (state & 0xffffu) | (1u << 16)};

        if (((type < XWM_EVENT_KEY_PRESS || type > XWM_EVENT_MOTION_NOTIFY) && type != XWM_EVENT_FOCUS_IN) ||
            detail > 255 || x < -32768 || x > 32767 || y < -32768 || y > 32767 || state > 0xffffu)
                return XWM_ERR_ARG;
        if (type == XWM_EVENT_FOCUS_IN)
        {
                unsigned int fw[2] = {window, state & 0xffu};

                return xwm_event_words(out, cap, (unsigned char)type, (unsigned char)detail, fw, 2);
        }
        return xwm_event_words(out, cap, (unsigned char)type, (unsigned char)detail, w, 7);
}

XWM_API long xwm_enc_get_atom_name(unsigned char *out, unsigned long cap, unsigned int atom)
{
        return !atom ? XWM_ERR_ARG : xwm_request_words(out, cap, XWM_OP_GET_ATOM_NAME, 0, &atom, 1);
}

XWM_API long xwm_enc_delete_property(unsigned char *out, unsigned long cap, unsigned int window, unsigned int property)
{
        unsigned int w[2] = {window, property};

        return (!window || !property) ? XWM_ERR_ARG : xwm_request_words(out, cap, XWM_OP_DELETE_PROPERTY, 0, w, 2);
}

XWM_API long xwm_enc_set_selection_owner(unsigned char *out, unsigned long cap, unsigned int owner, unsigned int selection, unsigned int time)
{
        unsigned int w[3] = {owner, selection, time};

        return !selection ? XWM_ERR_ARG : xwm_request_words(out, cap, XWM_OP_SET_SELECTION_OWNER, 0, w, 3);
}

XWM_API long xwm_enc_convert_selection(unsigned char *out, unsigned long cap, unsigned int requestor, unsigned int selection,
                                       unsigned int target, unsigned int property, unsigned int time)
{
        unsigned int w[5] = {requestor, selection, target, property, time};

        return (!requestor || !selection || !target) ? XWM_ERR_ARG : xwm_request_words(out, cap, XWM_OP_CONVERT_SELECTION, 0, w, 5);
}

XWM_API long xwm_enc_selection_notify(unsigned char *out, unsigned long cap, unsigned int time, unsigned int requestor,
                                      unsigned int selection, unsigned int target, unsigned int property)
{
        unsigned int w[5] = {time, requestor, selection, target, property};

        return (!requestor || !selection) ? XWM_ERR_ARG : xwm_event_words(out, cap, XWM_EVENT_SELECTION_NOTIFY, 0, w, 5);
}

XWM_API long xwm_enc_configure_notify(unsigned char *out, unsigned long cap, unsigned int window, int x, int y,
                                      unsigned int width, unsigned int height, unsigned int border, int override)
{
        unsigned int w[6] = {window, window, 0, ((unsigned int)x & 0xffffu) | (((unsigned int)y & 0xffffu) << 16),
                             (width & 0xffffu) | ((height & 0xffffu) << 16), (border & 0xffffu) | ((override ? 1u : 0u) << 16)};

        return !window ? XWM_ERR_ARG : xwm_event_words(out, cap, XWM_EVENT_CONFIGURE_NOTIFY, 0, w, 6);
}

XWM_API long xwm_enc_change_window_attributes(unsigned char *out, unsigned long cap, unsigned int window, unsigned int mask,
                                              const unsigned int *values, unsigned long count)
{
        unsigned long bits = 0;
        long n;

        if (!window || !mask || (mask & ~XWM_CW_ALL) || !values)
                return XWM_ERR_ARG;
        for (unsigned int bit = 0; bit < 15; bit++)
                bits += (mask >> bit) & 1u;
        if (bits != count || (n = xwm_request_start(out, cap, 12 + XWM_UNIT * count, XWM_OP_CHANGE_WINDOW_ATTRIBUTES)) < 0)
                return bits != count ? XWM_ERR_ARG : n;
        xwm_put32(out + 4, window), xwm_put32(out + 8, mask);
        for (unsigned long i = 0; i < count; i++)
                xwm_put32(out + 12 + XWM_UNIT * i, values[i]);
        return n;
}

/* ---- framing & parsers ---- */

XWM_API long xwm_message_type(const unsigned char *buf, unsigned long len)
{
        return !buf ? XWM_ERR_ARG : len < XWM_HEADER ? XWM_ERR_SHORT : (long)(buf[0] & XWM_TYPE_MASK);
}

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
        return total > XWM_REPLY_MAX ? XWM_ERR_CAP : (long)total;
}

XWM_API long xwm_setup_size(const unsigned char *buf, unsigned long len)
{
        unsigned long total;

        if (!buf)
                return XWM_ERR_ARG;
        if (len < XWM_SETUP_HEADER)
                return XWM_ERR_SHORT;
        total = XWM_SETUP_HEADER + (unsigned long)XWM_UNIT * xwm_get16(buf + 6);
        return total > XWM_SETUP_MAX ? XWM_ERR_CAP : (long)total;
}

static int xwm_reply_head(const unsigned char *buf, unsigned long len, const void *out)
{
        return !buf || !out ? XWM_ERR_ARG : len < XWM_HEADER ? XWM_ERR_SHORT : buf[0] != XWM_REPLY ? XWM_ERR_TYPE : XWM_OK;
}

static int xwm_reply_fits(const unsigned char *buf, unsigned long len, const void *out, unsigned long units)
{
        int r = xwm_reply_head(buf, len, out);

        if (r != XWM_OK)
                return r;
        return (xwm_get32(buf + 4) != units || (len - XWM_HEADER) / XWM_UNIT < units) ? XWM_ERR_LENGTH : XWM_OK;
}

XWM_API int xwm_parse_setup(const unsigned char *buf, unsigned long len, struct xwm_setup *out)
{
        long sz;
        unsigned long size, p;

        if (!buf || !out)
                return XWM_ERR_ARG;
        if ((sz = xwm_setup_size(buf, len)) < 0)
                return (int)sz;
        size = (unsigned long)sz;
        if (len < size)
                return XWM_ERR_LENGTH;
        out->status = buf[0];
        if (buf[0] == XWM_SETUP_FAILED)
        {
                out->reason = buf[1], out->major = xwm_get16(buf + 2), out->minor = xwm_get16(buf + 4);
                return XWM_SETUP_HEADER + (unsigned long)buf[1] > size ? XWM_ERR_LENGTH : XWM_OK;
        }
        if (buf[0] == XWM_SETUP_AUTHENTICATE)
                return XWM_OK;
        if (buf[0] != XWM_SETUP_SUCCESS)
                return XWM_ERR_TYPE;
        if (size < XWM_SETUP_FIXED)
                return XWM_ERR_LENGTH;
        out->major = xwm_get16(buf + 2), out->minor = xwm_get16(buf + 4);
        out->release = xwm_get32(buf + 8), out->resource_base = xwm_get32(buf + 12), out->resource_mask = xwm_get32(buf + 16);
        out->motion_buffer = xwm_get32(buf + 20), out->vendor = xwm_get16(buf + 24), out->max_request = xwm_get16(buf + 26);
        out->screens = buf[28], out->formats = buf[29], out->image_order = buf[30], out->bitmap_order = buf[31];
        out->scanline_unit = buf[32], out->scanline_pad = buf[33], out->min_keycode = buf[34], out->max_keycode = buf[35];
        if (out->major != XWM_PROTOCOL_MAJOR)
                return XWM_ERR_TYPE;
        if (!out->resource_mask || (out->resource_base & out->resource_mask) || !out->screens)
                return XWM_ERR_ARG;
        if (out->vendor > XWM_VENDOR_MAX || out->formats > XWM_FORMATS_MAX || out->screens > XWM_SCREENS_MAX)
                return XWM_ERR_CAP;
        p = XWM_SETUP_FIXED + xwm_pad4(out->vendor);
        if (p > size || size - p < 8ul * out->formats)
                return XWM_ERR_LENGTH;
        p += 8ul * out->formats;
        for (unsigned int s = 0; s < out->screens; s++)
        {
                const unsigned char *sc = buf + p;
                unsigned long depths, visuals = 0;

                if (size - p < 40)
                        return XWM_ERR_LENGTH;
                if (!(depths = sc[39]))
                        return XWM_ERR_ARG;
                if (depths > XWM_DEPTHS_MAX)
                        return XWM_ERR_CAP;
                if (s == 0)
                {
                        out->root = xwm_get32(sc), out->default_colormap = xwm_get32(sc + 4);
                        out->white_pixel = xwm_get32(sc + 8), out->black_pixel = xwm_get32(sc + 12);
                        out->root_events = xwm_get32(sc + 16), out->width = xwm_get16(sc + 20), out->height = xwm_get16(sc + 22);
                        out->width_mm = xwm_get16(sc + 24), out->height_mm = xwm_get16(sc + 26);
                        out->root_visual = xwm_get32(sc + 32), out->root_depth = sc[38];
                }
                p += 40;
                for (unsigned long d = 0; d < depths; d++)
                {
                        unsigned long nv;

                        if (size - p < 8)
                                return XWM_ERR_LENGTH;
                        nv = xwm_get16(buf + p + 2), visuals += nv, p += 8;
                        if (visuals > XWM_VISUALS_MAX)
                                return XWM_ERR_CAP;
                        if (size - p < 24 * nv)
                                return XWM_ERR_LENGTH;
                        p += 24 * nv;
                }
        }
        return p != size ? XWM_ERR_LENGTH : XWM_OK;
}

XWM_API int xwm_parse_error(const unsigned char *buf, unsigned long len, struct xwm_error *out)
{
        if (!buf || !out)
                return XWM_ERR_ARG;
        if (len < XWM_HEADER)
                return XWM_ERR_SHORT;
        if (buf[0] != XWM_ERROR)
                return XWM_ERR_TYPE;
        out->code = buf[1], out->sequence = xwm_get16(buf + 2), out->bad_value = xwm_get32(buf + 4);
        out->minor = xwm_get16(buf + 8), out->major = buf[10];
        return XWM_OK;
}

XWM_API int xwm_parse_atom_reply(const unsigned char *buf, unsigned long len, struct xwm_atom_reply *out)
{
        int r = xwm_reply_fits(buf, len, out, 0);

        if (r == XWM_OK)
                out->sequence = xwm_get16(buf + 2), out->atom = xwm_get32(buf + 8);
        return r;
}

XWM_API int xwm_parse_atom_name_reply(const unsigned char *buf, unsigned long len, struct xwm_atom_name_reply *out)
{
        unsigned long name_len;
        int r = xwm_reply_head(buf, len, out);

        if (r != XWM_OK)
                return r;
        name_len = xwm_get16(buf + 8);
        if (name_len > XWM_ATOM_NAME_MAX)
                return XWM_ERR_CAP;
        if ((r = xwm_reply_fits(buf, len, out, (name_len + XWM_UNIT - 1) / XWM_UNIT)) != XWM_OK)
                return r;
        out->sequence = xwm_get16(buf + 2), out->name = buf + XWM_HEADER, out->name_len = name_len;
        return XWM_OK;
}

XWM_API int xwm_parse_property_reply(const unsigned char *buf, unsigned long len, struct xwm_property_reply *out)
{
        unsigned long unit = 0, bytes;
        unsigned int format, elements;
        int r = xwm_reply_head(buf, len, out);

        if (r != XWM_OK)
                return r;
        format = buf[1], elements = xwm_get32(buf + 16);
        if (!format)
        {
                if (elements)
                        return XWM_ERR_ARG;
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
        if ((r = xwm_reply_fits(buf, len, out, (bytes + XWM_UNIT - 1) / XWM_UNIT)) != XWM_OK)
                return r;
        out->sequence = xwm_get16(buf + 2), out->format = format, out->type = xwm_get32(buf + 8);
        out->bytes_after = xwm_get32(buf + 12), out->elements = elements, out->value = buf + XWM_HEADER, out->value_len = bytes;
        return XWM_OK;
}

XWM_API int xwm_parse_geometry_reply(const unsigned char *buf, unsigned long len, struct xwm_geometry_reply *out)
{
        int r = xwm_reply_fits(buf, len, out, 0);

        if (r != XWM_OK)
                return r;
        out->sequence = xwm_get16(buf + 2), out->depth = buf[1], out->root = xwm_get32(buf + 8);
        out->x = xwm_get_i16(buf + 12), out->y = xwm_get_i16(buf + 14);
        out->width = xwm_get16(buf + 16), out->height = xwm_get16(buf + 18), out->border = xwm_get16(buf + 20);
        return XWM_OK;
}

XWM_API int xwm_parse_attributes_reply(const unsigned char *buf, unsigned long len, struct xwm_attributes_reply *out)
{
        int r = xwm_reply_fits(buf, len, out, 3);

        if (r != XWM_OK)
                return r;
        out->sequence = xwm_get16(buf + 2), out->visual = xwm_get32(buf + 8), out->window_class = xwm_get16(buf + 12);
        out->bit_gravity = buf[14], out->win_gravity = buf[15], out->save_under = buf[24], out->map_installed = buf[25];
        out->map_state = buf[26], out->override = buf[27], out->colormap = xwm_get32(buf + 28);
        out->all_masks = xwm_get32(buf + 32), out->your_mask = xwm_get32(buf + 36), out->do_not_propagate = xwm_get16(buf + 40);
        return XWM_OK;
}

XWM_API int xwm_parse_extension_reply(const unsigned char *buf, unsigned long len, struct xwm_extension_reply *out)
{
        int r = xwm_reply_fits(buf, len, out, 0);

        if (r != XWM_OK)
                return r;
        if (buf[8] > 1)
                return XWM_ERR_ARG;
        out->sequence = xwm_get16(buf + 2), out->present = buf[8], out->major_opcode = buf[9];
        out->first_event = buf[10], out->first_error = buf[11];
        return XWM_OK;
}

XWM_API int xwm_parse_xres_client_pid(const unsigned char *buf, unsigned long len, unsigned int *pid)
{
        unsigned long units, at = XWM_HEADER, last;
        int r = xwm_reply_head(buf, len, pid);

        if (r != XWM_OK)
                return r;
        units = xwm_get32(buf + 4);
        if ((r = xwm_reply_fits(buf, len, pid, units)) != XWM_OK)
                return r;
        last = XWM_HEADER + units * XWM_UNIT, *pid = 0;
        for (unsigned int k = 0, ids = xwm_get32(buf + 8); k < ids; k++)
        {
                unsigned long length;

                if (last - at < 12)
                        return XWM_ERR_LENGTH;
                length = xwm_get32(buf + at + 8);
                if (length > last - at - 12 || xwm_pad4(length) > last - at - 12)
                        return XWM_ERR_LENGTH;
                if ((xwm_get32(buf + at + 4) & XWM_XRES_LOCAL_CLIENT_PID) && length == 4)
                        *pid = xwm_get32(buf + at + 12);
                at += 12 + xwm_pad4(length);
        }
        return XWM_OK;
}

XWM_API void xwm_event_clear(struct xwm_event *e)
{
        xwm_zero((unsigned char *)e, sizeof *e);
        e->kind = XWM_EV_UNKNOWN;
}

/* Offset-table event unpacker shared by core events and XFIXES SelectionNotify (portable field offsets). */
enum { XWM_EW_U8 = 1, XWM_EW_U16 = 2, XWM_EW_I16 = 3, XWM_EW_U32 = 4 };
#define XWM_EV_F(field, wire_off, width) {(unsigned short)(unsigned long)&((struct xwm_event *)0)->field, (wire_off), (width)}
struct xwm_ev_field { unsigned short dst; unsigned char off, w; };
struct xwm_ev_desc { unsigned char code, kind, count; struct xwm_ev_field f[9]; };

static void xwm_event_unpack(const unsigned char *buf, struct xwm_event *out, const struct xwm_ev_desc *d)
{
        out->kind = d->kind;
        for (unsigned int i = 0; i < d->count; i++)
        {
                unsigned char *dst = (unsigned char *)out + d->f[i].dst;

                if (d->f[i].w == XWM_EW_I16)
                        *(int *)dst = xwm_get_i16(buf + d->f[i].off);
                else
                        *(unsigned int *)dst = d->f[i].w == XWM_EW_U8  ? buf[d->f[i].off]
                                             : d->f[i].w == XWM_EW_U16 ? xwm_get16(buf + d->f[i].off)
                                                                       : xwm_get32(buf + d->f[i].off);
        }
}

XWM_API int xwm_parse_event(const unsigned char *buf, unsigned long len, struct xwm_event *out)
{
        static const struct xwm_ev_desc descs[] = {
            {XWM_EVENT_MAP_REQUEST, XWM_EV_MAP_REQUEST, 2, {XWM_EV_F(parent, 4, XWM_EW_U32), XWM_EV_F(window, 8, XWM_EW_U32)}},
            {XWM_EVENT_CONFIGURE_REQUEST, XWM_EV_CONFIGURE_REQUEST, 9,
             {XWM_EV_F(parent, 4, XWM_EW_U32), XWM_EV_F(window, 8, XWM_EW_U32), XWM_EV_F(sibling, 12, XWM_EW_U32),
              XWM_EV_F(x, 16, XWM_EW_I16), XWM_EV_F(y, 18, XWM_EW_I16), XWM_EV_F(width, 20, XWM_EW_U16),
              XWM_EV_F(height, 22, XWM_EW_U16), XWM_EV_F(border, 24, XWM_EW_U16), XWM_EV_F(value_mask, 26, XWM_EW_U16)}},
            {XWM_EVENT_MAP_NOTIFY, XWM_EV_MAP_NOTIFY, 3, {XWM_EV_F(event, 4, XWM_EW_U32), XWM_EV_F(window, 8, XWM_EW_U32), XWM_EV_F(override, 12, XWM_EW_U8)}},
            {XWM_EVENT_UNMAP_NOTIFY, XWM_EV_UNMAP_NOTIFY, 3, {XWM_EV_F(event, 4, XWM_EW_U32), XWM_EV_F(window, 8, XWM_EW_U32), XWM_EV_F(from_configure, 12, XWM_EW_U8)}},
            {XWM_EVENT_DESTROY_NOTIFY, XWM_EV_DESTROY_NOTIFY, 2, {XWM_EV_F(event, 4, XWM_EW_U32), XWM_EV_F(window, 8, XWM_EW_U32)}},
            {XWM_EVENT_CONFIGURE_NOTIFY, XWM_EV_CONFIGURE_NOTIFY, 9,
             {XWM_EV_F(event, 4, XWM_EW_U32), XWM_EV_F(window, 8, XWM_EW_U32), XWM_EV_F(sibling, 12, XWM_EW_U32),
              XWM_EV_F(x, 16, XWM_EW_I16), XWM_EV_F(y, 18, XWM_EW_I16), XWM_EV_F(width, 20, XWM_EW_U16),
              XWM_EV_F(height, 22, XWM_EW_U16), XWM_EV_F(border, 24, XWM_EW_U16), XWM_EV_F(override, 26, XWM_EW_U8)}},
            {XWM_EVENT_PROPERTY_NOTIFY, XWM_EV_PROPERTY_NOTIFY, 4,
             {XWM_EV_F(window, 4, XWM_EW_U32), XWM_EV_F(atom, 8, XWM_EW_U32), XWM_EV_F(time, 12, XWM_EW_U32), XWM_EV_F(state, 16, XWM_EW_U8)}},
            {XWM_EVENT_CREATE_NOTIFY, XWM_EV_CREATE_NOTIFY, 8,
             {XWM_EV_F(parent, 4, XWM_EW_U32), XWM_EV_F(window, 8, XWM_EW_U32), XWM_EV_F(x, 12, XWM_EW_I16),
              XWM_EV_F(y, 14, XWM_EW_I16), XWM_EV_F(width, 16, XWM_EW_U16), XWM_EV_F(height, 18, XWM_EW_U16),
              XWM_EV_F(border, 20, XWM_EW_U16), XWM_EV_F(override, 22, XWM_EW_U8)}},
            {XWM_EVENT_FOCUS_IN, XWM_EV_FOCUS_IN, 2, {XWM_EV_F(event, 4, XWM_EW_U32), XWM_EV_F(mode, 8, XWM_EW_U8)}},
            {XWM_EVENT_FOCUS_OUT, XWM_EV_FOCUS_OUT, 2, {XWM_EV_F(event, 4, XWM_EW_U32), XWM_EV_F(mode, 8, XWM_EW_U8)}},
            {XWM_EVENT_ENTER, XWM_EV_ENTER, 2, {XWM_EV_F(event, 12, XWM_EW_U32), XWM_EV_F(mode, 30, XWM_EW_U8)}},
            {XWM_EVENT_LEAVE, XWM_EV_LEAVE, 2, {XWM_EV_F(event, 12, XWM_EW_U32), XWM_EV_F(mode, 30, XWM_EW_U8)}},
            {XWM_EVENT_SELECTION_CLEAR, XWM_EV_SELECTION_CLEAR, 3,
             {XWM_EV_F(time, 4, XWM_EW_U32), XWM_EV_F(owner, 8, XWM_EW_U32), XWM_EV_F(selection, 12, XWM_EW_U32)}},
            {XWM_EVENT_SELECTION_REQUEST, XWM_EV_SELECTION_REQUEST, 6,
             {XWM_EV_F(time, 4, XWM_EW_U32), XWM_EV_F(owner, 8, XWM_EW_U32), XWM_EV_F(requestor, 12, XWM_EW_U32),
              XWM_EV_F(selection, 16, XWM_EW_U32), XWM_EV_F(target, 20, XWM_EW_U32), XWM_EV_F(property, 24, XWM_EW_U32)}},
            {XWM_EVENT_SELECTION_NOTIFY, XWM_EV_SELECTION_NOTIFY, 5,
             {XWM_EV_F(time, 4, XWM_EW_U32), XWM_EV_F(requestor, 8, XWM_EW_U32), XWM_EV_F(selection, 12, XWM_EW_U32),
              XWM_EV_F(target, 16, XWM_EW_U32), XWM_EV_F(property, 20, XWM_EW_U32)}},
            {XWM_EVENT_EXPOSE, XWM_EV_IGNORED, 0, {{0, 0, 0}}},
            {XWM_EVENT_REPARENT_NOTIFY, XWM_EV_IGNORED, 0, {{0, 0, 0}}},
        };
        unsigned int type;

        if (!buf || !out)
                return XWM_ERR_ARG;
        if (len < XWM_HEADER)
                return XWM_ERR_SHORT;
        if ((type = buf[0] & XWM_TYPE_MASK) < XWM_EVENT_FIRST)
                return XWM_ERR_TYPE;
        xwm_event_clear(out);
        out->code = type, out->synthetic = (buf[0] & XWM_SEND_EVENT_BIT) ? 1 : 0;
        out->sequence = xwm_get16(buf + 2), out->detail = buf[1];
        if (type == XWM_EVENT_CLIENT_MESSAGE)
        {
                out->kind = XWM_EV_CLIENT_MESSAGE, out->format = buf[1];
                if (out->format != XWM_FORMAT_8 && out->format != XWM_FORMAT_16 && out->format != XWM_FORMAT_32)
                        return XWM_ERR_ARG;
                out->window = xwm_get32(buf + 4), out->atom = xwm_get32(buf + 8);
                if (out->format == XWM_FORMAT_32)
                        for (unsigned long i = 0; i < XWM_DATA_LONGS; i++)
                                out->data[i] = xwm_get32(buf + 12 + XWM_UNIT * i);
                return XWM_OK;
        }
        for (unsigned int i = 0; i < (unsigned int)(sizeof descs / sizeof descs[0]); i++)
                if (descs[i].code == type)
                        return xwm_event_unpack(buf, out, &descs[i]), XWM_OK;
        return XWM_OK;
}

XWM_API int xwm_parse_xfixes_selection(const unsigned char *buf, unsigned long len, unsigned int first_event, struct xwm_event *out)
{
        static const struct xwm_ev_desc xfixes_desc = {
            0, XWM_EV_XFIXES_SELECTION, 5,
            {XWM_EV_F(window, 4, XWM_EW_U32), XWM_EV_F(owner, 8, XWM_EW_U32), XWM_EV_F(selection, 12, XWM_EW_U32),
             XWM_EV_F(time, 16, XWM_EW_U32), XWM_EV_F(selection_time, 20, XWM_EW_U32)}};

        if (!buf || !out)
                return XWM_ERR_ARG;
        if (len < XWM_HEADER)
                return XWM_ERR_SHORT;
        if (first_event < XWM_EVENT_LAST + 1 || first_event > 127 || (buf[0] & XWM_TYPE_MASK) != first_event)
                return XWM_ERR_TYPE;
        xwm_event_clear(out);
        out->code = first_event, out->synthetic = (buf[0] & XWM_SEND_EVENT_BIT) ? 1 : 0;
        out->detail = buf[1], out->sequence = xwm_get16(buf + 2);
        xwm_event_unpack(buf, out, &xfixes_desc);
        return XWM_OK;
}

/* ---- selection targets, text transcoding, INCR, XDND ---- */

enum { XWM_TARGET_NONE = 0, XWM_TARGET_UTF8, XWM_TARGET_LATIN1, XWM_TARGET_MIME };
#define XWM_TEXT_MIME "text/plain;charset=utf-8"

XWM_API int xwm_target_kind(const unsigned char *name, unsigned long len)
{
        static const char *const utf8_names[] = {"UTF8_STRING", "TEXT", XWM_TEXT_MIME, "text/plain;charset=UTF8", "text/plain"};

        if (!name || !len || len > XWM_ATOM_NAME_MAX)
                return XWM_TARGET_NONE;
        for (unsigned int i = 0; i < 5; i++)
                if (xwm_name_is(name, len, utf8_names[i]))
                        return XWM_TARGET_UTF8;
        if (xwm_name_is(name, len, "STRING"))
                return XWM_TARGET_LATIN1;
        for (unsigned long at = 0; at < len; at++)
                if (name[at] == '/')
                        return name[0] != '/' && at + 1 < len ? XWM_TARGET_MIME : XWM_TARGET_NONE;
        return XWM_TARGET_NONE;
}

XWM_API long xwm_target_mime(const unsigned char *name, unsigned long len, unsigned char *out, unsigned long cap)
{
        int kind = xwm_target_kind(name, len);
        const unsigned char *from = kind == XWM_TARGET_MIME ? name : (const unsigned char *)XWM_TEXT_MIME;
        unsigned long n = kind == XWM_TARGET_MIME ? len : sizeof XWM_TEXT_MIME - 1;

        if (!out)
                return XWM_ERR_ARG;
        if (kind == XWM_TARGET_NONE)
                return 0;
        if (cap < n + 1)
                return XWM_ERR_SPACE;
        xwm_copy_bytes(out, from, n);
        out[n] = 0;
        return (long)n;
}

XWM_API unsigned long xwm_latin1_utf8(const unsigned char *in, unsigned long n, unsigned char *out)
{
        unsigned long put = 0;

        for (unsigned long at = 0; at < n; at++)
                if (in[at] < 0x80)
                        out[put++] = in[at];
                else
                        out[put++] = (unsigned char)(0xc0 | (in[at] >> 6)), out[put++] = (unsigned char)(0x80 | (in[at] & 0x3f));
        return put;
}

struct xwm_utf8_cut { unsigned int need, code; };

XWM_API unsigned long xwm_utf8_latin1(struct xwm_utf8_cut *cut, const unsigned char *in, unsigned long n, unsigned char *out)
{
        unsigned long put = 0;

        for (unsigned long at = 0; at < n; at++)
        {
                unsigned char c = in[at];

                if (cut->need)
                {
                        if ((c & 0xc0) == 0x80)
                        {
                                cut->code = (cut->code << 6) | (c & 0x3fu);
                                if (--cut->need == 0)
                                        out[put++] = cut->code <= 0xff ? (unsigned char)cut->code : '?';
                                continue;
                        }
                        cut->need = 0, out[put++] = '?';
                }
                if (c < 0x80)
                        out[put++] = c;
                else if ((c & 0xe0) == 0xc0)
                        cut->need = 1, cut->code = c & 0x1fu;
                else if ((c & 0xf0) == 0xe0)
                        cut->need = 2, cut->code = c & 0x0fu;
                else if ((c & 0xf8) == 0xf0)
                        cut->need = 3, cut->code = c & 0x07u;
                else
                        out[put++] = '?';
        }
        return put;
}

#define XWM_INCR_CHUNK 32768
#define XWM_INCR_SIZE_UNKNOWN (~0ul)

XWM_API int xwm_incr_needed(unsigned long size) { return size == XWM_INCR_SIZE_UNKNOWN || size > XWM_INCR_CHUNK; }

XWM_API long xwm_enc_incr(unsigned char *out, unsigned long cap, unsigned int window, unsigned int property, unsigned int incr, unsigned long size)
{
        unsigned char value[4];

        if (!incr)
                return XWM_ERR_ARG;
        xwm_put32(value, size >= 0xfffffffful ? 0xffffffffu : (unsigned int)size);
        return xwm_enc_change_property(out, cap, XWM_PROP_REPLACE, window, property, incr, XWM_FORMAT_32, value, 4);
}

XWM_API int xwm_property_incr(const struct xwm_property_reply *r, unsigned int incr, unsigned long *size)
{
        if (!r || !incr || r->type != incr || r->format != XWM_FORMAT_32 || r->value_len < 4)
                return 0;
        if (size)
                *size = xwm_get32(r->value);
        return 1;
}

XWM_API unsigned long xwm_incr_piece(unsigned long left, unsigned long room)
{
        unsigned long most = room > 24 ? (room - 24) & ~3ul : 0;

        most = most > XWM_INCR_CHUNK ? XWM_INCR_CHUNK : most;
        return left < most ? left : most;
}

#define XWM_XDND_VERSION 5
#define XWM_DND_COPY 1u
#define XWM_DND_MOVE 2u
#define XWM_DND_ASK 4u

XWM_API void xwm_dnd_enter(unsigned int *data, unsigned int source, unsigned int version, const unsigned int *types, unsigned long count)
{
        data[0] = source, data[1] = ((version & 0xffu) << 24) | (count > 3 ? 1u : 0u);
        for (unsigned long at = 0; at < 3; at++)
                data[2 + at] = at < count && types ? types[at] : 0;
}

XWM_API unsigned int xwm_dnd_place(long x, long y)
{
        unsigned int ux = x < 0 ? 0u : x > 32767 ? 32767u : (unsigned int)x;
        unsigned int uy = y < 0 ? 0u : y > 32767 ? 32767u : (unsigned int)y;

        return (ux << 16) | uy;
}

XWM_API unsigned int xwm_dnd_action(unsigned int atom, unsigned int copy, unsigned int move, unsigned int ask)
{
        return atom && atom == copy ? XWM_DND_COPY : atom && atom == move ? XWM_DND_MOVE : atom && atom == ask ? XWM_DND_ASK : 0;
}

XWM_API unsigned int xwm_dnd_action_atom(unsigned int action, unsigned int copy, unsigned int move, unsigned int ask)
{
        return (action & XWM_DND_COPY) ? copy : (action & XWM_DND_MOVE) ? move : (action & XWM_DND_ASK) ? ask : copy;
}

#endif /* XWM_WIRE_INCLUDED */
