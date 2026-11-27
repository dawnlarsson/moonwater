/*
        Differential C-reference parity, security, mmap guard-page, fuzz &
        throughput/size benchmark for src/canvas/xwm_wire.asm.c + xwm_wire.c
        (arm64, x86-64, riscv64 pure-ASM X11 wire codec).

        Build & run:
          cc -O2 -Wall -Werror test/xwm_wire.c -o /tmp/t_xwm && /tmp/t_xwm
          cc -O2 -fsanitize=address,undefined -Wall -Werror test/xwm_wire.c -o /tmp/t_xwm_asan && /tmp/t_xwm_asan
*/
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>

#include "../src/canvas/xwm_wire.c"

/* ── Reference C implementations (noinline for exact nm --print-size comparison) ── */

static unsigned int r_get16(const unsigned char *p) { return (unsigned int)p[0] | ((unsigned int)p[1] << 8); }
static unsigned int r_get32(const unsigned char *p) { return r_get16(p) | (r_get16(p + 2) << 16); }
static int r_get_i16(const unsigned char *p) { unsigned int v = r_get16(p); return v >= 0x8000u ? (int)v - 0x10000 : (int)v; }
static void r_put16(unsigned char *p, unsigned int v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
static void r_put32(unsigned char *p, unsigned int v) { r_put16(p, v); r_put16(p + 2, v >> 16); }
static unsigned long r_pad4(unsigned long n) { return (n + 3ul) & ~3ul; }

static __attribute__((noinline)) xwm_i64 ref_request_start(unsigned char *out, xwm_u64 cap, xwm_u64 bytes, xwm_u32 opcode) {
        if (!out || bytes < 4) return XWM_ERR_ARG;
        if (bytes / 4 > XWM_UNITS_MAX) return XWM_ERR_CAP;
        if (bytes > cap) return XWM_ERR_SPACE;
        memset(out, 0, (size_t)bytes);
        out[0] = (unsigned char)opcode;
        r_put16(out + 2, (unsigned int)(bytes / 4));
        return (xwm_i64)bytes;
}

static __attribute__((noinline)) xwm_i64 ref_request_words(unsigned char *out, xwm_u64 cap, xwm_u32 op_data, const xwm_u32 *words, xwm_u64 count) {
        if (!out) return XWM_ERR_ARG;
        if (count > 65534) return XWM_ERR_CAP;
        xwm_u64 bytes = 4 + 4 * count;
        if (bytes > cap) return XWM_ERR_SPACE;
        if (count && !words) return XWM_ERR_ARG;
        out[0] = (unsigned char)(op_data & 0xffu);
        out[1] = (unsigned char)((op_data >> 8) & 0xffu);
        r_put16(out + 2, (unsigned int)(1 + count));
        for (xwm_u64 i = 0; i < count; i++) r_put32(out + 4 + 4 * i, words[i]);
        return (xwm_i64)bytes;
}

static __attribute__((noinline)) xwm_i64 ref_request_name(unsigned char *out, xwm_u64 cap, xwm_u32 op_data, const unsigned char *name, xwm_u64 name_len, xwm_u64 max_len) {
        if (!name || !name_len) return XWM_ERR_ARG;
        if (name_len > max_len) return XWM_ERR_CAP;
        xwm_i64 n = ref_request_start(out, cap, 8 + r_pad4(name_len), op_data & 0xffu);
        if (n < 0) return n;
        out[1] = (unsigned char)((op_data >> 8) & 0xffu);
        r_put16(out + 4, (unsigned int)name_len);
        memcpy(out + 8, name, (size_t)name_len);
        return n;
}

static __attribute__((noinline)) xwm_i64 ref_event_words(unsigned char *out, xwm_u64 cap, xwm_u32 type_detail, const xwm_u32 *words, xwm_u64 count) {
        if (!out || count > 7) return XWM_ERR_ARG;
        if (cap < 32) return XWM_ERR_SPACE;
        if (count && !words) return XWM_ERR_ARG;
        memset(out, 0, 32);
        out[0] = (unsigned char)(type_detail & 0xffu);
        out[1] = (unsigned char)((type_detail >> 8) & 0xffu);
        for (xwm_u64 i = 0; i < count; i++) r_put32(out + 4 + 4 * i, words[i]);
        return 32;
}

static __attribute__((noinline)) xwm_i64 ref_enc_configure_window(unsigned char *out, xwm_u64 cap, xwm_u32 window, const struct xwm_configure *c) {
        unsigned int mask, vals[7], count = 0;
        if (!c || (c->mask & ~XWM_CONFIGURE_ALL)) return XWM_ERR_ARG;
        mask = c->mask;
        if (((mask & XWM_CONFIGURE_X) && (c->x < -32768 || c->x > 32767)) ||
            ((mask & XWM_CONFIGURE_Y) && (c->y < -32768 || c->y > 32767)) ||
            ((mask & XWM_CONFIGURE_WIDTH) && (!c->width || c->width > 65535)) ||
            ((mask & XWM_CONFIGURE_HEIGHT) && (!c->height || c->height > 65535)) ||
            ((mask & XWM_CONFIGURE_BORDER) && c->border > 65535) ||
            ((mask & XWM_CONFIGURE_STACK_MODE) && c->stack_mode > XWM_STACK_OPPOSITE))
                return XWM_ERR_ARG;
        for (unsigned int b = 0; b < 7; b++) count += (mask >> b) & 1u;
        xwm_i64 n = ref_request_start(out, cap, 12 + 4 * (xwm_u64)count, XWM_OP_CONFIGURE_WINDOW);
        if (n < 0) return n;
        r_put32(out + 4, window); r_put16(out + 8, mask);
        vals[0] = (unsigned int)c->x; vals[1] = (unsigned int)c->y; vals[2] = c->width; vals[3] = c->height;
        vals[4] = c->border; vals[5] = c->sibling; vals[6] = c->stack_mode;
        unsigned char *p = out + 12;
        for (unsigned int b = 0; b < 7; b++) if (mask & (1u << b)) { r_put32(p, vals[b]); p += 4; }
        return n;
}

static __attribute__((noinline)) xwm_i64 ref_enc_change_window_attributes(unsigned char *out, xwm_u64 cap, xwm_u32 window, xwm_u32 mask, const xwm_u32 *values, xwm_u64 count) {
        xwm_u64 bits = 0;
        if (!window || !mask || (mask & ~XWM_CW_ALL) || !values) return XWM_ERR_ARG;
        for (unsigned int b = 0; b < 15; b++) bits += (mask >> b) & 1u;
        if (bits != count) return XWM_ERR_ARG;
        xwm_i64 n = ref_request_start(out, cap, 12 + 4 * count, XWM_OP_CHANGE_WINDOW_ATTRIBUTES);
        if (n < 0) return n;
        r_put32(out + 4, window); r_put32(out + 8, mask);
        for (xwm_u64 i = 0; i < count; i++) r_put32(out + 12 + 4 * i, values[i]);
        return n;
}

static __attribute__((noinline)) xwm_i64 ref_server_message_size(const unsigned char *buf, xwm_u64 len) {
        if (!buf) return XWM_ERR_ARG;
        if (len < XWM_HEADER) return XWM_ERR_SHORT;
        unsigned int type = buf[0] & XWM_TYPE_MASK;
        if (type != XWM_REPLY && type != XWM_EVENT_GENERIC) return XWM_HEADER;
        xwm_u64 total = XWM_HEADER + 4ul * (xwm_u64)r_get32(buf + 4);
        return total > XWM_REPLY_MAX ? XWM_ERR_CAP : (xwm_i64)total;
}

static __attribute__((noinline)) xwm_i64 ref_setup_size(const unsigned char *buf, xwm_u64 len) {
        if (!buf) return XWM_ERR_ARG;
        if (len < XWM_SETUP_HEADER) return XWM_ERR_SHORT;
        xwm_u64 total = XWM_SETUP_HEADER + 4ul * (xwm_u64)r_get16(buf + 6);
        return total > XWM_SETUP_MAX ? XWM_ERR_CAP : (xwm_i64)total;
}

static int r_reply_head(const unsigned char *buf, xwm_u64 len, const void *out) {
        return !buf || !out ? XWM_ERR_ARG : len < XWM_HEADER ? XWM_ERR_SHORT : buf[0] != XWM_REPLY ? XWM_ERR_TYPE : XWM_OK;
}
static int r_reply_fits(const unsigned char *buf, xwm_u64 len, const void *out, xwm_u64 units) {
        int r = r_reply_head(buf, len, out);
        if (r != XWM_OK) return r;
        return (r_get32(buf + 4) != units || (len - XWM_HEADER) / 4 < units) ? XWM_ERR_LENGTH : XWM_OK;
}

static __attribute__((noinline)) xwm_i32 ref_parse_setup(const unsigned char *buf, xwm_u64 len, struct xwm_setup *out) {
        if (!buf || !out) return XWM_ERR_ARG;
        xwm_i64 sz = ref_setup_size(buf, len);
        if (sz < 0) return (xwm_i32)sz;
        xwm_u64 size = (xwm_u64)sz, p;
        if (len < size) return XWM_ERR_LENGTH;
        out->status = buf[0];
        if (buf[0] == XWM_SETUP_FAILED) {
                out->reason = buf[1]; out->major = r_get16(buf + 2); out->minor = r_get16(buf + 4);
                return XWM_SETUP_HEADER + (xwm_u64)buf[1] > size ? XWM_ERR_LENGTH : XWM_OK;
        }
        if (buf[0] == XWM_SETUP_AUTHENTICATE) return XWM_OK;
        if (buf[0] != XWM_SETUP_SUCCESS) return XWM_ERR_TYPE;
        if (size < XWM_SETUP_FIXED) return XWM_ERR_LENGTH;
        out->major = r_get16(buf + 2); out->minor = r_get16(buf + 4);
        out->release = r_get32(buf + 8); out->resource_base = r_get32(buf + 12); out->resource_mask = r_get32(buf + 16);
        out->motion_buffer = r_get32(buf + 20); out->vendor = r_get16(buf + 24); out->max_request = r_get16(buf + 26);
        out->screens = buf[28]; out->formats = buf[29]; out->image_order = buf[30]; out->bitmap_order = buf[31];
        out->scanline_unit = buf[32]; out->scanline_pad = buf[33]; out->min_keycode = buf[34]; out->max_keycode = buf[35];
        if (out->major != XWM_PROTOCOL_MAJOR) return XWM_ERR_TYPE;
        if (!out->resource_mask || (out->resource_base & out->resource_mask) || !out->screens) return XWM_ERR_ARG;
        if (out->vendor > XWM_VENDOR_MAX || out->formats > XWM_FORMATS_MAX || out->screens > XWM_SCREENS_MAX) return XWM_ERR_CAP;
        p = XWM_SETUP_FIXED + r_pad4(out->vendor);
        if (p > size || size - p < 8ul * out->formats) return XWM_ERR_LENGTH;
        p += 8ul * out->formats;
        for (unsigned int s = 0; s < out->screens; s++) {
                const unsigned char *sc = buf + p;
                unsigned long depths, visuals = 0;
                if (size - p < 40) return XWM_ERR_LENGTH;
                if (!(depths = sc[39])) return XWM_ERR_ARG;
                if (depths > XWM_DEPTHS_MAX) return XWM_ERR_CAP;
                if (s == 0) {
                        out->root = r_get32(sc); out->default_colormap = r_get32(sc + 4);
                        out->white_pixel = r_get32(sc + 8); out->black_pixel = r_get32(sc + 12);
                        out->root_events = r_get32(sc + 16); out->width = r_get16(sc + 20); out->height = r_get16(sc + 22);
                        out->width_mm = r_get16(sc + 24); out->height_mm = r_get16(sc + 26);
                        out->root_visual = r_get32(sc + 32); out->root_depth = sc[38];
                }
                p += 40;
                for (unsigned long d = 0; d < depths; d++) {
                        if (size - p < 8) return XWM_ERR_LENGTH;
                        unsigned long nv = r_get16(buf + p + 2);
                        visuals += nv; p += 8;
                        if (visuals > XWM_VISUALS_MAX) return XWM_ERR_CAP;
                        if (size - p < 24 * nv) return XWM_ERR_LENGTH;
                        p += 24 * nv;
                }
        }
        return p != size ? XWM_ERR_LENGTH : XWM_OK;
}

static __attribute__((noinline)) xwm_i32 ref_parse_error(const unsigned char *buf, xwm_u64 len, struct xwm_error *out) {
        if (!buf || !out) return XWM_ERR_ARG;
        if (len < XWM_HEADER) return XWM_ERR_SHORT;
        if (buf[0] != XWM_ERROR) return XWM_ERR_TYPE;
        out->code = buf[1]; out->sequence = r_get16(buf + 2); out->bad_value = r_get32(buf + 4);
        out->minor = r_get16(buf + 8); out->major = buf[10];
        return XWM_OK;
}

static __attribute__((noinline)) xwm_i32 ref_parse_atom_reply(const unsigned char *buf, xwm_u64 len, struct xwm_atom_reply *out) {
        int r = r_reply_fits(buf, len, out, 0);
        if (r == XWM_OK) { out->sequence = r_get16(buf + 2); out->atom = r_get32(buf + 8); }
        return r;
}

static __attribute__((noinline)) xwm_i32 ref_parse_atom_name_reply(const unsigned char *buf, xwm_u64 len, struct xwm_atom_name_reply *out) {
        int r = r_reply_head(buf, len, out);
        if (r != XWM_OK) return r;
        unsigned long name_len = r_get16(buf + 8);
        if (name_len > XWM_ATOM_NAME_MAX) return XWM_ERR_CAP;
        if ((r = r_reply_fits(buf, len, out, (name_len + 3) / 4)) != XWM_OK) return r;
        out->sequence = r_get16(buf + 2); out->name = buf + XWM_HEADER; out->name_len = name_len;
        return XWM_OK;
}

static __attribute__((noinline)) xwm_i32 ref_parse_property_reply(const unsigned char *buf, xwm_u64 len, struct xwm_property_reply *out) {
        unsigned long unit = 0;
        int r = r_reply_head(buf, len, out);
        if (r != XWM_OK) return r;
        unsigned int format = buf[1], elements = r_get32(buf + 16);
        if (!format) { if (elements) return XWM_ERR_ARG; }
        else if (format == 8 || format == 16 || format == 32) {
                unit = format / 8;
                if (elements > XWM_PROPERTY_MAX / unit) return XWM_ERR_CAP;
        } else return XWM_ERR_ARG;
        unsigned long bytes = (unsigned long)elements * unit;
        if ((r = r_reply_fits(buf, len, out, (bytes + 3) / 4)) != XWM_OK) return r;
        out->sequence = r_get16(buf + 2); out->format = format; out->type = r_get32(buf + 8);
        out->bytes_after = r_get32(buf + 12); out->elements = elements; out->value = buf + XWM_HEADER; out->value_len = bytes;
        return XWM_OK;
}

static __attribute__((noinline)) xwm_i32 ref_parse_geometry_reply(const unsigned char *buf, xwm_u64 len, struct xwm_geometry_reply *out) {
        int r = r_reply_fits(buf, len, out, 0);
        if (r != XWM_OK) return r;
        out->sequence = r_get16(buf + 2); out->depth = buf[1]; out->root = r_get32(buf + 8);
        out->x = r_get_i16(buf + 12); out->y = r_get_i16(buf + 14);
        out->width = r_get16(buf + 16); out->height = r_get16(buf + 18); out->border = r_get16(buf + 20);
        return XWM_OK;
}

static __attribute__((noinline)) xwm_i32 ref_parse_attributes_reply(const unsigned char *buf, xwm_u64 len, struct xwm_attributes_reply *out) {
        int r = r_reply_fits(buf, len, out, 3);
        if (r != XWM_OK) return r;
        out->sequence = r_get16(buf + 2); out->visual = r_get32(buf + 8); out->window_class = r_get16(buf + 12);
        out->bit_gravity = buf[14]; out->win_gravity = buf[15]; out->save_under = buf[24]; out->map_installed = buf[25];
        out->map_state = buf[26]; out->override = buf[27]; out->colormap = r_get32(buf + 28);
        out->all_masks = r_get32(buf + 32); out->your_mask = r_get32(buf + 36); out->do_not_propagate = r_get16(buf + 40);
        return XWM_OK;
}

static __attribute__((noinline)) xwm_i32 ref_parse_extension_reply(const unsigned char *buf, xwm_u64 len, struct xwm_extension_reply *out) {
        int r = r_reply_fits(buf, len, out, 0);
        if (r != XWM_OK) return r;
        if (buf[8] > 1) return XWM_ERR_ARG;
        out->sequence = r_get16(buf + 2); out->present = buf[8]; out->major_opcode = buf[9];
        out->first_event = buf[10]; out->first_error = buf[11];
        return XWM_OK;
}

static __attribute__((noinline)) xwm_i32 ref_parse_xres_client_pid(const unsigned char *buf, xwm_u64 len, xwm_u32 *pid) {
        int r = r_reply_head(buf, len, pid);
        if (r != XWM_OK) return r;
        unsigned long units = r_get32(buf + 4);
        if ((r = r_reply_fits(buf, len, pid, units)) != XWM_OK) return r;
        unsigned long at = XWM_HEADER, last = XWM_HEADER + units * 4;
        *pid = 0;
        for (unsigned int k = 0, ids = r_get32(buf + 8); k < ids; k++) {
                if (last - at < 12) return XWM_ERR_LENGTH;
                unsigned long length = r_get32(buf + at + 8);
                if (length > last - at - 12 || r_pad4(length) > last - at - 12) return XWM_ERR_LENGTH;
                if ((r_get32(buf + at + 4) & XWM_XRES_LOCAL_CLIENT_PID) && length == 4) *pid = r_get32(buf + at + 12);
                at += 12 + r_pad4(length);
        }
        return XWM_OK;
}

enum { R_EW_U8 = 1, R_EW_U16 = 2, R_EW_I16 = 3, R_EW_U32 = 4 };
#define R_EV_F(field, off, w) {(unsigned short)(unsigned long)&((struct xwm_event *)0)->field, (off), (w)}
struct r_ev_field { unsigned short dst; unsigned char off, w; };
struct r_ev_desc { unsigned char code, kind, count; struct r_ev_field f[9]; };

static void r_ev_unpack(const unsigned char *buf, struct xwm_event *out, const struct r_ev_desc *d) {
        out->kind = d->kind;
        for (unsigned int i = 0; i < d->count; i++) {
                unsigned char *dst = (unsigned char *)out + d->f[i].dst;
                if (d->f[i].w == R_EW_I16) *(int *)dst = r_get_i16(buf + d->f[i].off);
                else *(unsigned int *)dst = d->f[i].w == R_EW_U8 ? buf[d->f[i].off] : d->f[i].w == R_EW_U16 ? r_get16(buf + d->f[i].off) : r_get32(buf + d->f[i].off);
        }
}

static __attribute__((noinline)) xwm_i32 ref_parse_event(const unsigned char *buf, xwm_u64 len, struct xwm_event *out) {
        static const struct r_ev_desc descs[] = {
            {XWM_EVENT_MAP_REQUEST, XWM_EV_MAP_REQUEST, 2, {R_EV_F(parent, 4, R_EW_U32), R_EV_F(window, 8, R_EW_U32)}},
            {XWM_EVENT_CONFIGURE_REQUEST, XWM_EV_CONFIGURE_REQUEST, 9,
             {R_EV_F(parent, 4, R_EW_U32), R_EV_F(window, 8, R_EW_U32), R_EV_F(sibling, 12, R_EW_U32),
              R_EV_F(x, 16, R_EW_I16), R_EV_F(y, 18, R_EW_I16), R_EV_F(width, 20, R_EW_U16),
              R_EV_F(height, 22, R_EW_U16), R_EV_F(border, 24, R_EW_U16), R_EV_F(value_mask, 26, R_EW_U16)}},
            {XWM_EVENT_MAP_NOTIFY, XWM_EV_MAP_NOTIFY, 3, {R_EV_F(event, 4, R_EW_U32), R_EV_F(window, 8, R_EW_U32), R_EV_F(override, 12, R_EW_U8)}},
            {XWM_EVENT_UNMAP_NOTIFY, XWM_EV_UNMAP_NOTIFY, 3, {R_EV_F(event, 4, R_EW_U32), R_EV_F(window, 8, R_EW_U32), R_EV_F(from_configure, 12, R_EW_U8)}},
            {XWM_EVENT_DESTROY_NOTIFY, XWM_EV_DESTROY_NOTIFY, 2, {R_EV_F(event, 4, R_EW_U32), R_EV_F(window, 8, R_EW_U32)}},
            {XWM_EVENT_CONFIGURE_NOTIFY, XWM_EV_CONFIGURE_NOTIFY, 9,
             {R_EV_F(event, 4, R_EW_U32), R_EV_F(window, 8, R_EW_U32), R_EV_F(sibling, 12, R_EW_U32),
              R_EV_F(x, 16, R_EW_I16), R_EV_F(y, 18, R_EW_I16), R_EV_F(width, 20, R_EW_U16),
              R_EV_F(height, 22, R_EW_U16), R_EV_F(border, 24, R_EW_U16), R_EV_F(override, 26, R_EW_U8)}},
            {XWM_EVENT_PROPERTY_NOTIFY, XWM_EV_PROPERTY_NOTIFY, 4,
             {R_EV_F(window, 4, R_EW_U32), R_EV_F(atom, 8, R_EW_U32), R_EV_F(time, 12, R_EW_U32), R_EV_F(state, 16, R_EW_U8)}},
            {XWM_EVENT_CREATE_NOTIFY, XWM_EV_CREATE_NOTIFY, 8,
             {R_EV_F(parent, 4, R_EW_U32), R_EV_F(window, 8, R_EW_U32), R_EV_F(x, 12, R_EW_I16),
              R_EV_F(y, 14, R_EW_I16), R_EV_F(width, 16, R_EW_U16), R_EV_F(height, 18, R_EW_U16),
              R_EV_F(border, 20, R_EW_U16), R_EV_F(override, 22, R_EW_U8)}},
            {XWM_EVENT_FOCUS_IN, XWM_EV_FOCUS_IN, 2, {R_EV_F(event, 4, R_EW_U32), R_EV_F(mode, 8, R_EW_U8)}},
            {XWM_EVENT_FOCUS_OUT, XWM_EV_FOCUS_OUT, 2, {R_EV_F(event, 4, R_EW_U32), R_EV_F(mode, 8, R_EW_U8)}},
            {XWM_EVENT_ENTER, XWM_EV_ENTER, 2, {R_EV_F(event, 12, R_EW_U32), R_EV_F(mode, 30, R_EW_U8)}},
            {XWM_EVENT_LEAVE, XWM_EV_LEAVE, 2, {R_EV_F(event, 12, R_EW_U32), R_EV_F(mode, 30, R_EW_U8)}},
            {XWM_EVENT_SELECTION_CLEAR, XWM_EV_SELECTION_CLEAR, 3,
             {R_EV_F(time, 4, R_EW_U32), R_EV_F(owner, 8, R_EW_U32), R_EV_F(selection, 12, R_EW_U32)}},
            {XWM_EVENT_SELECTION_REQUEST, XWM_EV_SELECTION_REQUEST, 6,
             {R_EV_F(time, 4, R_EW_U32), R_EV_F(owner, 8, R_EW_U32), R_EV_F(requestor, 12, R_EW_U32),
              R_EV_F(selection, 16, R_EW_U32), R_EV_F(target, 20, R_EW_U32), R_EV_F(property, 24, R_EW_U32)}},
            {XWM_EVENT_SELECTION_NOTIFY, XWM_EV_SELECTION_NOTIFY, 5,
             {R_EV_F(time, 4, R_EW_U32), R_EV_F(requestor, 8, R_EW_U32), R_EV_F(selection, 12, R_EW_U32),
              R_EV_F(target, 16, R_EW_U32), R_EV_F(property, 20, R_EW_U32)}},
            {XWM_EVENT_EXPOSE, XWM_EV_IGNORED, 0, {{0, 0, 0}}},
            {XWM_EVENT_REPARENT_NOTIFY, XWM_EV_IGNORED, 0, {{0, 0, 0}}},
        };
        if (!buf || !out) return XWM_ERR_ARG;
        if (len < XWM_HEADER) return XWM_ERR_SHORT;
        unsigned int type = buf[0] & XWM_TYPE_MASK;
        if (type < XWM_EVENT_FIRST) return XWM_ERR_TYPE;
        memset(out, 0, sizeof *out);
        out->code = type; out->synthetic = (buf[0] & XWM_SEND_EVENT_BIT) ? 1 : 0;
        out->sequence = r_get16(buf + 2); out->detail = buf[1];
        if (type == XWM_EVENT_CLIENT_MESSAGE) {
                out->kind = XWM_EV_CLIENT_MESSAGE; out->format = buf[1];
                if (out->format != 8 && out->format != 16 && out->format != 32) return XWM_ERR_ARG;
                out->window = r_get32(buf + 4); out->atom = r_get32(buf + 8);
                if (out->format == 32) for (unsigned long i = 0; i < 5; i++) out->data[i] = r_get32(buf + 12 + 4 * i);
                return XWM_OK;
        }
        for (unsigned int i = 0; i < (unsigned int)(sizeof descs / sizeof descs[0]); i++)
                if (descs[i].code == type) { r_ev_unpack(buf, out, &descs[i]); return XWM_OK; }
        return XWM_OK;
}

static __attribute__((noinline)) xwm_i32 ref_parse_xfixes_selection(const unsigned char *buf, xwm_u64 len, xwm_u32 first_event, struct xwm_event *out) {
        if (!buf || !out) return XWM_ERR_ARG;
        if (len < XWM_HEADER) return XWM_ERR_SHORT;
        if (first_event < 36 || first_event > 127 || (buf[0] & XWM_TYPE_MASK) != first_event) return XWM_ERR_TYPE;
        memset(out, 0, sizeof *out);
        out->kind = XWM_EV_XFIXES_SELECTION; out->code = first_event;
        out->synthetic = (buf[0] & XWM_SEND_EVENT_BIT) ? 1 : 0;
        out->detail = buf[1]; out->sequence = r_get16(buf + 2);
        out->window = r_get32(buf + 4); out->owner = r_get32(buf + 8); out->selection = r_get32(buf + 12);
        out->time = r_get32(buf + 16); out->selection_time = r_get32(buf + 20);
        return XWM_OK;
}

static __attribute__((noinline)) xwm_u64 ref_latin1_utf8(const unsigned char *in, xwm_u64 n, unsigned char *out) {
        if (!n || !in || !out) return 0;
        xwm_u64 put = 0;
        for (xwm_u64 at = 0; at < n; at++) {
                if (in[at] < 0x80) out[put++] = in[at];
                else { out[put++] = (unsigned char)(0xc0 | (in[at] >> 6)); out[put++] = (unsigned char)(0x80 | (in[at] & 0x3f)); }
        }
        return put;
}

static __attribute__((noinline)) xwm_u64 ref_utf8_latin1(struct xwm_utf8_cut *cut, const unsigned char *in, xwm_u64 n, unsigned char *out) {
        if (!cut || !in || !out || !n) return 0;
        xwm_u64 put = 0;
        for (xwm_u64 at = 0; at < n; at++) {
                unsigned char c = in[at];
                if (cut->need) {
                        if ((c & 0xc0) == 0x80) {
                                cut->code = (cut->code << 6) | (c & 0x3fu);
                                if (--cut->need == 0) out[put++] = cut->code <= 0xff ? (unsigned char)cut->code : '?';
                                continue;
                        }
                        cut->need = 0; out[put++] = '?';
                }
                if (c < 0x80) out[put++] = c;
                else if ((c & 0xe0) == 0xc0) { cut->need = 1; cut->code = c & 0x1fu; }
                else if ((c & 0xf0) == 0xe0) { cut->need = 2; cut->code = c & 0x0fu; }
                else if ((c & 0xf8) == 0xf0) { cut->need = 3; cut->code = c & 0x07u; }
                else out[put++] = '?';
        }
        return put;
}

/* ── Test Harness ─────────────────────────────────────────────────────────── */

static uint64_t rng_s = 0x5eed12349abcdef0ULL;
static uint64_t rng64(void) {
        uint64_t x = rng_s;
        x ^= x << 13; x ^= x >> 7; x ^= x << 17;
        return rng_s = x;
}
static void fill_rand(unsigned char *p, size_t n) { for (size_t i = 0; i < n; i++) p[i] = (unsigned char)rng64(); }

static int g_fails = 0;
static int g_checks = 0;
#define CHECK(cond, msg) do { g_checks++; if (!(cond)) { fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg); g_fails++; } } while (0)

static double now_ns(void) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

/* ── 1. Edge Cases & Security Tests ───────────────────────────────────────── */

static void test_edge_and_security(void) {
        unsigned char buf[128], out_a[128], out_r[128];
        xwm_u32 words[8] = {1, 2, 3, 4, 5, 6, 7, 8};

        /* NULL & overflow checks on encoders */
        CHECK(xwm_asm_request_start(NULL, 64, 8, 1) == XWM_ERR_ARG, "request_start NULL");
        CHECK(xwm_asm_request_start(out_a, 64, 0, 1) == XWM_ERR_ARG, "request_start bytes=0");
        CHECK(xwm_asm_request_start(out_a, 64, 3, 1) == XWM_ERR_ARG, "request_start bytes=3");
        CHECK(xwm_asm_request_start(out_a, 64, 262144, 1) == XWM_ERR_CAP, "request_start cap overflow");
        CHECK(xwm_asm_request_start(out_a, 64, ~0ULL, 1) == XWM_ERR_CAP, "request_start u64 max");
        CHECK(xwm_asm_request_start(out_a, 7, 8, 1) == XWM_ERR_SPACE, "request_start short cap");

        CHECK(xwm_asm_request_words(NULL, 64, 1, words, 1) == XWM_ERR_ARG, "request_words NULL out");
        CHECK(xwm_asm_request_words(out_a, 64, 1, NULL, 1) == XWM_ERR_ARG, "request_words NULL words");
        CHECK(xwm_asm_request_words(out_a, 64, 1, NULL, 0) == 4, "request_words count=0");
        CHECK(xwm_asm_request_words(out_a, 64, 1, words, 65535) == XWM_ERR_CAP, "request_words count overflow");
        CHECK(xwm_asm_request_words(out_a, 64, 1, words, ~0ULL) == XWM_ERR_CAP, "request_words count u64 max");
        CHECK(xwm_asm_request_words(out_a, 7, 1, words, 1) == XWM_ERR_SPACE, "request_words short cap");

        CHECK(xwm_asm_event_words(NULL, 32, 2, words, 1) == XWM_ERR_ARG, "event_words NULL");
        CHECK(xwm_asm_event_words(out_a, 31, 2, words, 1) == XWM_ERR_SPACE, "event_words cap=31");
        CHECK(xwm_asm_event_words(out_a, 32, 2, words, 8) == XWM_ERR_ARG, "event_words count=8");
        CHECK(xwm_asm_event_words(out_a, 32, 2, NULL, 1) == XWM_ERR_ARG, "event_words NULL words");

        CHECK(xwm_asm_request_name(out_a, 64, 16, NULL, 4, 256) == XWM_ERR_ARG, "request_name NULL");
        CHECK(xwm_asm_request_name(out_a, 64, 16, (const unsigned char *)"A", 0, 256) == XWM_ERR_ARG, "request_name len=0");
        CHECK(xwm_asm_request_name(out_a, 64, 16, (const unsigned char *)"A", 257, 256) == XWM_ERR_CAP, "request_name > max");
        CHECK(xwm_asm_request_name(out_a, 11, 16, (const unsigned char *)"ABCD", 4, 256) == XWM_ERR_SPACE, "request_name short cap");

        /* Framing overflow 0xffffffff */
        memset(buf, 0, sizeof buf);
        buf[0] = XWM_REPLY;
        r_put32(buf + 4, 0xffffffffu);
        CHECK(xwm_asm_server_message_size(buf, 32) == XWM_ERR_CAP, "server_message_size 0xffffffff overflow");
        r_put32(buf + 4, 0x40000000u);
        CHECK(xwm_asm_server_message_size(buf, 32) == XWM_ERR_CAP, "server_message_size 0x40000000 overflow");
        r_put32(buf + 4, 16384u);
        CHECK(xwm_asm_server_message_size(buf, 32) == XWM_REPLY_MAX, "server_message_size exact max");
        r_put32(buf + 4, 16385u);
        CHECK(xwm_asm_server_message_size(buf, 32) == XWM_ERR_CAP, "server_message_size max+1");
        buf[0] = XWM_EVENT_GENERIC | XWM_SEND_EVENT_BIT;
        r_put32(buf + 4, 10u);
        CHECK(xwm_asm_server_message_size(buf, 32) == 72, "server_message_size GenericEvent with SendEvent bit");
        buf[0] = XWM_EVENT_KEY_PRESS | XWM_SEND_EVENT_BIT;
        r_put32(buf + 4, 0xffffffffu);
        CHECK(xwm_asm_server_message_size(buf, 32) == 32, "server_message_size normal event ignores length");
        CHECK(xwm_asm_server_message_size(NULL, 32) == XWM_ERR_ARG, "server_message_size NULL");
        CHECK(xwm_asm_server_message_size(buf, 31) == XWM_ERR_SHORT, "server_message_size len=31");

        /* Setup size overflow */
        r_put16(buf + 6, 0xffffu);
        CHECK(xwm_asm_setup_size(buf, 8) == XWM_ERR_CAP, "setup_size 0xffff");
        r_put16(buf + 6, 16382u);
        CHECK(xwm_asm_setup_size(buf, 8) == 65536, "setup_size exact 65536");
        r_put16(buf + 6, 16383u);
        CHECK(xwm_asm_setup_size(buf, 8) == XWM_ERR_CAP, "setup_size 65540");
        CHECK(xwm_asm_setup_size(NULL, 8) == XWM_ERR_ARG, "setup_size NULL");
        CHECK(xwm_asm_setup_size(buf, 7) == XWM_ERR_SHORT, "setup_size len=7");

        /* All 256 event first bytes (opcodes 0..127 x SendEvent 0/0x80) */
        for (int b0 = 0; b0 < 256; b0++) {
                fill_rand(buf, 32);
                buf[0] = (unsigned char)b0;
                if ((b0 & 0x7f) == XWM_EVENT_CLIENT_MESSAGE) {
                        static const unsigned char fmts[] = {0, 8, 16, 24, 32, 64, 255};
                        for (size_t fi = 0; fi < sizeof fmts; fi++) {
                                buf[1] = fmts[fi];
                                struct xwm_event ea, er;
                                memset(&ea, 0xaa, sizeof ea); memset(&er, 0xaa, sizeof er);
                                int ra = xwm_asm_parse_event(buf, 32, &ea);
                                int rr = ref_parse_event(buf, 32, &er);
                                CHECK(ra == rr && (ra != 0 || memcmp(&ea, &er, sizeof ea) == 0), "parse_event ClientMessage parity");
                        }
                } else {
                        struct xwm_event ea, er;
                        memset(&ea, 0xaa, sizeof ea); memset(&er, 0xaa, sizeof er);
                        int ra = xwm_asm_parse_event(buf, 32, &ea);
                        int rr = ref_parse_event(buf, 32, &er);
                        CHECK(ra == rr && (ra != 0 || memcmp(&ea, &er, sizeof ea) == 0), "parse_event opcode parity");
                }
        }

        /* ConfigureWindow all 128 masks */
        for (unsigned int m = 0; m < 128; m++) {
                struct xwm_configure cfg = {m, -32768, 32767, 1, 65535, 65535, 0x12345678u, 4};
                memset(out_a, 0xcc, sizeof out_a); memset(out_r, 0xcc, sizeof out_r);
                xwm_i64 na = xwm_asm_enc_configure_window(out_a, sizeof out_a, 0xdeadbeefu, &cfg);
                xwm_i64 nr = ref_enc_configure_window(out_r, sizeof out_r, 0xdeadbeefu, &cfg);
                CHECK(na == nr && na > 0 && memcmp(out_a, out_r, (size_t)na) == 0, "enc_configure_window mask parity");
        }
        /* ConfigureWindow boundary failures */
        {
                struct xwm_configure bad[] = {
                        {0x80, 0, 0, 1, 1, 0, 0, 0},
                        {XWM_CONFIGURE_X, -32769, 0, 1, 1, 0, 0, 0},
                        {XWM_CONFIGURE_X, 32768, 0, 1, 1, 0, 0, 0},
                        {XWM_CONFIGURE_Y, 0, -32769, 1, 1, 0, 0, 0},
                        {XWM_CONFIGURE_Y, 0, 32768, 1, 1, 0, 0, 0},
                        {XWM_CONFIGURE_WIDTH, 0, 0, 0, 1, 0, 0, 0},
                        {XWM_CONFIGURE_WIDTH, 0, 0, 65536, 1, 0, 0, 0},
                        {XWM_CONFIGURE_HEIGHT, 0, 0, 1, 0, 0, 0, 0},
                        {XWM_CONFIGURE_HEIGHT, 0, 0, 1, 65536, 0, 0, 0},
                        {XWM_CONFIGURE_BORDER, 0, 0, 1, 1, 65536, 0, 0},
                        {XWM_CONFIGURE_STACK_MODE, 0, 0, 1, 1, 0, 0, 5},
                };
                for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
                        CHECK(xwm_asm_enc_configure_window(out_a, 64, 1, &bad[i]) == XWM_ERR_ARG, "enc_configure_window bad arg");
        }

        /* Latin-1 <-> UTF-8 all 256 byte values round-trip */
        {
                unsigned char l1[256], u8a[512], u8r[512], rt_a[256], rt_r[256];
                for (int i = 0; i < 256; i++) l1[i] = (unsigned char)i;
                xwm_u64 na = xwm_asm_latin1_utf8(l1, 256, u8a);
                xwm_u64 nr = ref_latin1_utf8(l1, 256, u8r);
                CHECK(na == nr && na == 384 && memcmp(u8a, u8r, (size_t)na) == 0, "latin1_utf8 all 256 bytes");
                struct xwm_utf8_cut ca = {0, 0}, cr = {0, 0};
                xwm_u64 ra = xwm_asm_utf8_latin1(&ca, u8a, na, rt_a);
                xwm_u64 rr = ref_utf8_latin1(&cr, u8r, nr, rt_r);
                CHECK(ra == rr && ra == 256 && ca.need == 0 && memcmp(rt_a, l1, 256) == 0, "utf8_latin1 round-trip");
        }
}

/* ── 2. Randomized Differential Fuzzing ───────────────────────────────────── */

static void test_fuzz(void) {
        unsigned char buf[1024], out_a[1024], out_r[1024];

        for (int it = 0; it < 4000; it++) {
                /* Fuzz framing & all parsers on random lengths & bytes */
                size_t len = (size_t)(rng64() % 128);
                fill_rand(buf, len ? len : 1);
                if ((it & 3) == 0 && len >= 32) buf[0] = XWM_REPLY;
                if ((it & 7) == 1 && len >= 32) buf[0] = XWM_ERROR;
                if ((it & 7) == 2 && len >= 32) { buf[0] = XWM_REPLY; r_put32(buf + 4, (unsigned int)((len - 32) / 4)); }

                CHECK(xwm_asm_server_message_size(buf, len) == ref_server_message_size(buf, len), "fuzz server_message_size");
                CHECK(xwm_asm_setup_size(buf, len) == ref_setup_size(buf, len), "fuzz setup_size");

                struct xwm_error ea, er; memset(&ea, 0, sizeof ea); memset(&er, 0, sizeof er);
                int ra = xwm_asm_parse_error(buf, len, &ea), rr = ref_parse_error(buf, len, &er);
                CHECK(ra == rr && (ra != 0 || memcmp(&ea, &er, sizeof ea) == 0), "fuzz parse_error");

                struct xwm_atom_reply aa, ar; memset(&aa, 0, sizeof aa); memset(&ar, 0, sizeof ar);
                ra = xwm_asm_parse_atom_reply(buf, len, &aa); rr = ref_parse_atom_reply(buf, len, &ar);
                CHECK(ra == rr && (ra != 0 || memcmp(&aa, &ar, sizeof aa) == 0), "fuzz parse_atom_reply");

                struct xwm_atom_name_reply ana, anr; memset(&ana, 0, sizeof ana); memset(&anr, 0, sizeof anr);
                ra = xwm_asm_parse_atom_name_reply(buf, len, &ana); rr = ref_parse_atom_name_reply(buf, len, &anr);
                CHECK(ra == rr && (ra != 0 || memcmp(&ana, &anr, sizeof ana) == 0), "fuzz parse_atom_name_reply");

                struct xwm_property_reply pa, pr; memset(&pa, 0, sizeof pa); memset(&pr, 0, sizeof pr);
                if ((it & 3) == 0 && len >= 32) {
                        static const unsigned char fmts[5] = {0, 8, 16, 32, 24};
                        buf[1] = fmts[rng64() % 5];
                        unsigned int el = (unsigned int)(rng64() % 20);
                        r_put32(buf + 16, el);
                        unsigned int bytes = el * (buf[1] / 8);
                        r_put32(buf + 4, (bytes + 3) / 4);
                }
                ra = xwm_asm_parse_property_reply(buf, len, &pa); rr = ref_parse_property_reply(buf, len, &pr);
                CHECK(ra == rr && (ra != 0 || memcmp(&pa, &pr, sizeof pa) == 0), "fuzz parse_property_reply");

                struct xwm_geometry_reply ga, gr; memset(&ga, 0, sizeof ga); memset(&gr, 0, sizeof gr);
                ra = xwm_asm_parse_geometry_reply(buf, len, &ga); rr = ref_parse_geometry_reply(buf, len, &gr);
                CHECK(ra == rr && (ra != 0 || memcmp(&ga, &gr, sizeof ga) == 0), "fuzz parse_geometry_reply");

                struct xwm_attributes_reply ata, atr; memset(&ata, 0, sizeof ata); memset(&atr, 0, sizeof atr);
                if ((it & 7) == 0 && len >= 44) { buf[0] = XWM_REPLY; r_put32(buf + 4, 3); }
                ra = xwm_asm_parse_attributes_reply(buf, len, &ata); rr = ref_parse_attributes_reply(buf, len, &atr);
                CHECK(ra == rr && (ra != 0 || memcmp(&ata, &atr, sizeof ata) == 0), "fuzz parse_attributes_reply");

                struct xwm_extension_reply exa, exr; memset(&exa, 0, sizeof exa); memset(&exr, 0, sizeof exr);
                ra = xwm_asm_parse_extension_reply(buf, len, &exa); rr = ref_parse_extension_reply(buf, len, &exr);
                CHECK(ra == rr && (ra != 0 || memcmp(&exa, &exr, sizeof exa) == 0), "fuzz parse_extension_reply");

                xwm_u32 pida = 0xdeadbeefu, pidr = 0xdeadbeefu;
                ra = xwm_asm_parse_xres_client_pid(buf, len, &pida); rr = ref_parse_xres_client_pid(buf, len, &pidr);
                CHECK(ra == rr && (ra != 0 || pida == pidr), "fuzz parse_xres_client_pid");

                struct xwm_event eva, evr; memset(&eva, 0x55, sizeof eva); memset(&evr, 0x55, sizeof evr);
                ra = xwm_asm_parse_event(buf, len, &eva); rr = ref_parse_event(buf, len, &evr);
                CHECK(ra == rr && (ra != 0 || memcmp(&eva, &evr, sizeof eva) == 0), "fuzz parse_event");

                xwm_u32 fe = (xwm_u32)(rng64() % 140);
                if ((it & 3) == 0 && len >= 32) { fe = 36 + (xwm_u32)(rng64() % 92); buf[0] = (unsigned char)(fe | ((it & 4) ? 0x80 : 0)); }
                memset(&eva, 0x55, sizeof eva); memset(&evr, 0x55, sizeof evr);
                ra = xwm_asm_parse_xfixes_selection(buf, len, fe, &eva);
                rr = ref_parse_xfixes_selection(buf, len, fe, &evr);
                CHECK(ra == rr && (ra != 0 || memcmp(&eva, &evr, sizeof eva) == 0), "fuzz parse_xfixes_selection");

                /* Fuzz setup parser (valid constructed + mutated) */
                struct xwm_setup sa, sr; memset(&sa, 0, sizeof sa); memset(&sr, 0, sizeof sr);
                if ((it & 3) == 0) {
                        /* Construct a valid 1- or 2-screen setup reply */
                        unsigned int vend = (unsigned int)(rng64() % 17);
                        unsigned int fmts = (unsigned int)(rng64() % 4);
                        unsigned int scrs = 1 + (unsigned int)(rng64() % 2);
                        size_t pos = 40 + r_pad4(vend) + 8 * fmts;
                        memset(buf, 0, sizeof buf);
                        buf[0] = 1; r_put16(buf + 2, 11); r_put16(buf + 4, 0);
                        r_put32(buf + 8, 12345); r_put32(buf + 12, 0x00200000u); r_put32(buf + 16, 0x001fffffu);
                        r_put32(buf + 20, 256); r_put16(buf + 24, vend); r_put16(buf + 26, 65535);
                        buf[28] = (unsigned char)scrs; buf[29] = (unsigned char)fmts;
                        for (unsigned int s = 0; s < scrs; s++) {
                                r_put32(buf + pos, 0x100 + s); r_put32(buf + pos + 4, 0x20 + s);
                                r_put16(buf + pos + 20, 1920); r_put16(buf + pos + 22, 1080);
                                buf[pos + 38] = 24; buf[pos + 39] = 1;
                                pos += 40;
                                buf[pos] = 24; r_put16(buf + pos + 2, 1);
                                pos += 8 + 24;
                        }
                        r_put16(buf + 6, (unsigned int)((pos - 8) / 4));
                        if ((it & 8) == 8) buf[rng64() % pos] ^= (unsigned char)(1u << (rng64() & 7));
                        ra = xwm_asm_parse_setup(buf, pos, &sa);
                        rr = ref_parse_setup(buf, pos, &sr);
                        CHECK(ra == rr && (ra != 0 || memcmp(&sa, &sr, sizeof sa) == 0), "fuzz parse_setup valid/mutated");
                } else {
                        ra = xwm_asm_parse_setup(buf, len, &sa);
                        rr = ref_parse_setup(buf, len, &sr);
                        CHECK(ra == rr && (ra != 0 || memcmp(&sa, &sr, sizeof sa) == 0), "fuzz parse_setup random");
                }

                /* Fuzz UTF-8 -> Latin-1 across random split chunks */
                struct xwm_utf8_cut ca = {0, 0}, cr = {0, 0};
                size_t n1 = (size_t)(rng64() % 64), n2 = (size_t)(rng64() % 64);
                fill_rand(buf, n1 + n2 + 1);
                xwm_u64 wa1 = xwm_asm_utf8_latin1(&ca, buf, n1, out_a);
                xwm_u64 wr1 = ref_utf8_latin1(&cr, buf, n1, out_r);
                xwm_u64 wa2 = xwm_asm_utf8_latin1(&ca, buf + n1, n2, out_a + wa1);
                xwm_u64 wr2 = ref_utf8_latin1(&cr, buf + n1, n2, out_r + wr1);
                CHECK(wa1 == wr1 && wa2 == wr2 && ca.need == cr.need && ca.code == cr.code &&
                      memcmp(out_a, out_r, (size_t)(wa1 + wa2)) == 0, "fuzz utf8_latin1 streaming");

                /* Fuzz Latin-1 -> UTF-8 */
                xwm_u64 la = xwm_asm_latin1_utf8(buf, n1, out_a);
                xwm_u64 lr = ref_latin1_utf8(buf, n1, out_r);
                CHECK(la == lr && memcmp(out_a, out_r, (size_t)la) == 0, "fuzz latin1_utf8");

                /* Fuzz encoders */
                xwm_u32 w[15];
                for (int i = 0; i < 15; i++) w[i] = (xwm_u32)rng64();
                xwm_u32 mask = (xwm_u32)(rng64() & 0x7fffu);
                xwm_u64 cnt = 0;
                for (int b = 0; b < 15; b++) cnt += (mask >> b) & 1u;
                xwm_u64 cap = (xwm_u64)(rng64() % 96);
                xwm_u32 win = (xwm_u32)(rng64() % 5);
                memset(out_a, 0xdd, 128); memset(out_r, 0xdd, 128);
                xwm_i64 ea_n = xwm_asm_enc_change_window_attributes(out_a, cap, win, mask, w, cnt);
                xwm_i64 er_n = ref_enc_change_window_attributes(out_r, cap, win, mask, w, cnt);
                CHECK(ea_n == er_n && (ea_n < 0 || memcmp(out_a, out_r, (size_t)ea_n) == 0), "fuzz change_window_attributes");

                xwm_u64 rbytes = (xwm_u64)(rng64() % 80);
                memset(out_a, 0xee, 128); memset(out_r, 0xee, 128);
                ea_n = xwm_asm_request_start(out_a, cap, rbytes, (xwm_u32)(rng64() & 0xffu));
                /* use same opcode */
                xwm_u32 op = (xwm_u32)(rng64() & 0xffffu);
                memset(out_a, 0xee, 128); memset(out_r, 0xee, 128);
                ea_n = xwm_asm_request_start(out_a, cap, rbytes, op & 0xffu);
                er_n = ref_request_start(out_r, cap, rbytes, op & 0xffu);
                CHECK(ea_n == er_n && (ea_n < 0 || memcmp(out_a, out_r, (size_t)ea_n) == 0), "fuzz request_start");

                xwm_u64 wcnt = (xwm_u64)(rng64() % 10);
                memset(out_a, 0xee, 128); memset(out_r, 0xee, 128);
                ea_n = xwm_asm_request_words(out_a, cap, op, w, wcnt);
                er_n = ref_request_words(out_r, cap, op, w, wcnt);
                CHECK(ea_n == er_n && (ea_n < 0 || memcmp(out_a, out_r, (size_t)ea_n) == 0), "fuzz request_words");

                xwm_u64 nlen = (xwm_u64)(rng64() % 40);
                memset(out_a, 0xee, 128); memset(out_r, 0xee, 128);
                ea_n = xwm_asm_request_name(out_a, cap, op, buf, nlen, 32);
                er_n = ref_request_name(out_r, cap, op, buf, nlen, 32);
                CHECK(ea_n == er_n && (ea_n < 0 || memcmp(out_a, out_r, (size_t)ea_n) == 0), "fuzz request_name");

                memset(out_a, 0xee, 128); memset(out_r, 0xee, 128);
                ea_n = xwm_asm_event_words(out_a, cap, op, w, wcnt);
                er_n = ref_event_words(out_r, cap, op, w, wcnt);
                CHECK(ea_n == er_n && (ea_n < 0 || memcmp(out_a, out_r, (size_t)ea_n) == 0), "fuzz event_words");
        }
}

/* ── 3. mmap Guard-Page Tests (PROT_NONE before AND after) ────────────────── */

static unsigned char *alloc_guarded(size_t *page_out) {
        size_t pg = (size_t)sysconf(_SC_PAGESIZE);
        unsigned char *base = mmap(NULL, 3 * pg, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (base == MAP_FAILED) { perror("mmap"); exit(2); }
        if (mprotect(base + pg, pg, PROT_READ | PROT_WRITE) != 0) { perror("mprotect"); exit(2); }
        *page_out = pg;
        return base + pg;
}

static void free_guarded(unsigned char *mid, size_t pg) {
        munmap(mid - pg, 3 * pg);
}

static void test_guard_pages(void) {
        size_t pg = 0;
        unsigned char *in_pg = alloc_guarded(&pg);
        unsigned char *out_pg = alloc_guarded(&pg);

        /* Test both flush-left (offset 0, PROT_NONE immediately before) and flush-right (pg - len, PROT_NONE immediately after) */
        for (int side = 0; side < 2; side++) {
                /* 1. Encoders flush against guard pages */
                unsigned char *o = side ? (out_pg + pg - 24) : out_pg;
                xwm_u32 w[5] = {1, 2, 3, 4, 5};
                CHECK(xwm_asm_request_start(o, 24, 24, 18) == 24, "guard request_start");
                CHECK(xwm_asm_request_words(o, 24, 20 | (1u << 8), w, 5) == 24, "guard request_words");

                unsigned char *n_in = side ? (in_pg + pg - 11) : in_pg;
                memcpy(n_in, "UTF8_STRING", 11);
                unsigned char *n_out = side ? (out_pg + pg - 20) : out_pg;
                CHECK(xwm_asm_request_name(n_out, 20, 16, n_in, 11, 256) == 20, "guard request_name");

                unsigned char *ev_out = side ? (out_pg + pg - 32) : out_pg;
                CHECK(xwm_asm_event_words(ev_out, 32, 33 | (32u << 8), w, 5) == 32, "guard event_words");

                struct xwm_configure cfg = {XWM_CONFIGURE_ALL, -100, 200, 800, 600, 2, 99, 1};
                unsigned char *c_out = side ? (out_pg + pg - 40) : out_pg;
                CHECK(xwm_asm_enc_configure_window(c_out, 40, 42, &cfg) == 40, "guard enc_configure_window");

                unsigned char *cw_out = side ? (out_pg + pg - 20) : out_pg;
                CHECK(xwm_asm_enc_change_window_attributes(cw_out, 20, 42, 0x0003u, w, 2) == 20, "guard enc_change_window_attributes");

                /* 2. Framing & Reply parsers flush against guard pages */
                unsigned char *b32 = side ? (in_pg + pg - 32) : in_pg;
                memset(b32, 0, 32); b32[0] = XWM_REPLY;
                CHECK(xwm_asm_server_message_size(b32, 32) == 32, "guard server_message_size");

                unsigned char *b8 = side ? (in_pg + pg - 8) : in_pg;
                memset(b8, 0, 8); b8[0] = XWM_SETUP_AUTHENTICATE; r_put16(b8 + 6, 0);
                CHECK(xwm_asm_setup_size(b8, 8) == 8, "guard setup_size");
                struct xwm_setup *s_out = (struct xwm_setup *)(side ? (out_pg + pg - sizeof(struct xwm_setup)) : out_pg);
                CHECK(xwm_asm_parse_setup(b8, 8, s_out) == XWM_OK, "guard parse_setup");

                b32[0] = XWM_ERROR; b32[1] = 3;
                struct xwm_error *err_out = (struct xwm_error *)(side ? (out_pg + pg - sizeof(struct xwm_error)) : out_pg);
                CHECK(xwm_asm_parse_error(b32, 32, err_out) == XWM_OK, "guard parse_error");

                memset(b32, 0, 32); b32[0] = XWM_REPLY; r_put32(b32 + 8, 77);
                struct xwm_atom_reply *ar_out = (struct xwm_atom_reply *)(side ? (out_pg + pg - sizeof(struct xwm_atom_reply)) : out_pg);
                CHECK(xwm_asm_parse_atom_reply(b32, 32, ar_out) == XWM_OK, "guard parse_atom_reply");

                struct xwm_geometry_reply *gr_out = (struct xwm_geometry_reply *)(side ? (out_pg + pg - sizeof(struct xwm_geometry_reply)) : out_pg);
                CHECK(xwm_asm_parse_geometry_reply(b32, 32, gr_out) == XWM_OK, "guard parse_geometry_reply");

                b32[8] = 1;
                struct xwm_extension_reply *ex_out = (struct xwm_extension_reply *)(side ? (out_pg + pg - sizeof(struct xwm_extension_reply)) : out_pg);
                CHECK(xwm_asm_parse_extension_reply(b32, 32, ex_out) == XWM_OK, "guard parse_extension_reply");

                unsigned char *b44 = side ? (in_pg + pg - 44) : in_pg;
                memset(b44, 0, 44); b44[0] = XWM_REPLY; r_put32(b44 + 4, 3);
                struct xwm_attributes_reply *at_out = (struct xwm_attributes_reply *)(side ? (out_pg + pg - sizeof(struct xwm_attributes_reply)) : out_pg);
                CHECK(xwm_asm_parse_attributes_reply(b44, 44, at_out) == XWM_OK, "guard parse_attributes_reply");

                unsigned char *b40 = side ? (in_pg + pg - 40) : in_pg;
                memset(b40, 0, 40); b40[0] = XWM_REPLY; b40[1] = 8; r_put32(b40 + 4, 2); r_put32(b40 + 16, 7);
                struct xwm_property_reply *pr_out = (struct xwm_property_reply *)(side ? (out_pg + pg - sizeof(struct xwm_property_reply)) : out_pg);
                CHECK(xwm_asm_parse_property_reply(b40, 40, pr_out) == XWM_OK && pr_out->value_len == 7, "guard parse_property_reply");

                memset(b40, 0, 40); b40[0] = XWM_REPLY; r_put32(b40 + 4, 2); r_put16(b40 + 8, 6);
                struct xwm_atom_name_reply *an_out = (struct xwm_atom_name_reply *)(side ? (out_pg + pg - sizeof(struct xwm_atom_name_reply)) : out_pg);
                CHECK(xwm_asm_parse_atom_name_reply(b40, 40, an_out) == XWM_OK && an_out->name_len == 6, "guard parse_atom_name_reply");

                unsigned char *b48 = side ? (in_pg + pg - 48) : in_pg;
                memset(b48, 0, 48); b48[0] = XWM_REPLY; r_put32(b48 + 4, 4); r_put32(b48 + 8, 1);
                r_put32(b48 + 36, XWM_XRES_LOCAL_CLIENT_PID); r_put32(b48 + 40, 4); r_put32(b48 + 44, 9999);
                xwm_u32 *pid_out = (xwm_u32 *)(side ? (out_pg + pg - 4) : out_pg);
                CHECK(xwm_asm_parse_xres_client_pid(b48, 48, pid_out) == XWM_OK && *pid_out == 9999, "guard parse_xres_client_pid");

                /* 3. Event parsers flush against guard pages (even 31-byte truncated!) */
                unsigned char *b31 = side ? (in_pg + pg - 31) : in_pg;
                struct xwm_event *ev_s = (struct xwm_event *)(side ? (out_pg + pg - sizeof(struct xwm_event)) : out_pg);
                CHECK(xwm_asm_parse_event(b31, 31, ev_s) == XWM_ERR_SHORT, "guard parse_event len=31");

                memset(b32, 0, 32); b32[0] = XWM_EVENT_CONFIGURE_REQUEST | XWM_SEND_EVENT_BIT;
                CHECK(xwm_asm_parse_event(b32, 32, ev_s) == XWM_OK && ev_s->kind == XWM_EV_CONFIGURE_REQUEST, "guard parse_event 32");

                b32[0] = 87 | XWM_SEND_EVENT_BIT;
                CHECK(xwm_asm_parse_xfixes_selection(b32, 32, 87, ev_s) == XWM_OK, "guard parse_xfixes_selection");

                /* 4. Latin-1 <-> UTF-8 flush against guard pages */
                unsigned char *t_in = side ? (in_pg + pg - 64) : in_pg;
                for (int i = 0; i < 64; i++) t_in[i] = (unsigned char)(0x60 + i);
                unsigned char *t_out = side ? (out_pg + pg - 96) : out_pg;
                xwm_u64 un = xwm_asm_latin1_utf8(t_in, 64, t_out);
                CHECK(un == 96, "guard latin1_utf8");
                struct xwm_utf8_cut cut = {0, 0};
                unsigned char *t_back = side ? (in_pg + pg - 64) : in_pg;
                unsigned char *t_u8 = side ? (out_pg + pg - 96) : out_pg;
                CHECK(xwm_asm_utf8_latin1(&cut, t_u8, 96, t_back) == 64, "guard utf8_latin1");
        }

        free_guarded(in_pg, pg);
        free_guarded(out_pg, pg);
}

/* ── 4. Speed Benchmark (ASM vs -O2 C Reference) ──────────────────────────── */

static void run_bench(void) {
        const int N = 400000;
        unsigned char evs[8][32];
        static const unsigned char codes[8] = {22, 23, 28, 33, 19, 31, 9, 7};
        for (int i = 0; i < 8; i++) {
                fill_rand(evs[i], 32);
                evs[i][0] = codes[i];
                if (codes[i] == 33) evs[i][1] = 32;
        }
        struct xwm_event ev;
        volatile int sink = 0;

        /* Bench 1: parse_event */
        double t0 = now_ns();
        for (int i = 0; i < N; i++) sink ^= xwm_asm_parse_event(evs[i & 7], 32, &ev) ^ ev.kind;
        double ta = (now_ns() - t0) / N;

        t0 = now_ns();
        for (int i = 0; i < N; i++) sink ^= ref_parse_event(evs[i & 7], 32, &ev) ^ ev.kind;
        double tr = (now_ns() - t0) / N;
        printf("  parse_event           : ASM %5.2f ns/op | C-O2 %5.2f ns/op | speedup %.2fx\n", ta, tr, tr / ta);

        /* Bench 2: parse_property_reply + parse_attributes_reply + parse_geometry_reply */
        unsigned char prb[64], atb[44], geb[32];
        memset(prb, 0, sizeof prb); prb[0] = 1; prb[1] = 8; r_put32(prb + 4, 8); r_put32(prb + 16, 32);
        memset(atb, 0, sizeof atb); atb[0] = 1; r_put32(atb + 4, 3);
        memset(geb, 0, sizeof geb); geb[0] = 1;
        struct xwm_property_reply pr; struct xwm_attributes_reply at; struct xwm_geometry_reply ge;

        t0 = now_ns();
        for (int i = 0; i < N; i++) {
                sink ^= xwm_asm_parse_property_reply(prb, 64, &pr) ^ (int)pr.value_len;
                sink ^= xwm_asm_parse_attributes_reply(atb, 44, &at) ^ (int)at.visual;
                sink ^= xwm_asm_parse_geometry_reply(geb, 32, &ge) ^ ge.x;
        }
        ta = (now_ns() - t0) / (3.0 * N);

        t0 = now_ns();
        for (int i = 0; i < N; i++) {
                sink ^= ref_parse_property_reply(prb, 64, &pr) ^ (int)pr.value_len;
                sink ^= ref_parse_attributes_reply(atb, 44, &at) ^ (int)at.visual;
                sink ^= ref_parse_geometry_reply(geb, 32, &ge) ^ ge.x;
        }
        tr = (now_ns() - t0) / (3.0 * N);
        printf("  parse_replies (3x)    : ASM %5.2f ns/op | C-O2 %5.2f ns/op | speedup %.2fx\n", ta, tr, tr / ta);

        /* Bench 3: enc_configure_window + request_words */
        unsigned char req[64];
        xwm_u32 w5[5] = {10, 20, 30, 40, 50};
        struct xwm_configure cfg = {XWM_CONFIGURE_ALL, 100, 200, 1280, 720, 0, 1, 0};

        t0 = now_ns();
        for (int i = 0; i < N; i++) {
                sink ^= (int)xwm_asm_enc_configure_window(req, 64, 42, &cfg);
                sink ^= (int)xwm_asm_request_words(req, 64, 20, w5, 5);
        }
        ta = (now_ns() - t0) / (2.0 * N);

        t0 = now_ns();
        for (int i = 0; i < N; i++) {
                sink ^= (int)ref_enc_configure_window(req, 64, 42, &cfg);
                sink ^= (int)ref_request_words(req, 64, 20, w5, 5);
        }
        tr = (now_ns() - t0) / (2.0 * N);
        printf("  encode_requests (2x)  : ASM %5.2f ns/op | C-O2 %5.2f ns/op | speedup %.2fx\n", ta, tr, tr / ta);

        /* Bench 4: utf8_latin1 (1KB mixed ASCII + Latin-1 UTF-8) */
        unsigned char l1[512], u8[1024], back[1024];
        for (int i = 0; i < 512; i++) l1[i] = (unsigned char)((i & 3) == 0 ? (0xc0 + (i & 63)) : (0x20 + (i % 95)));
        xwm_u64 u8n = xwm_asm_latin1_utf8(l1, 512, u8);
        const int M = 100000;
        struct xwm_utf8_cut cut;

        t0 = now_ns();
        for (int i = 0; i < M; i++) { cut.need = 0; cut.code = 0; sink ^= (int)xwm_asm_utf8_latin1(&cut, u8, u8n, back); }
        ta = (now_ns() - t0) / M;

        t0 = now_ns();
        for (int i = 0; i < M; i++) { cut.need = 0; cut.code = 0; sink ^= (int)ref_utf8_latin1(&cut, u8, u8n, back); }
        tr = (now_ns() - t0) / M;
        printf("  utf8_latin1 (640B)    : ASM %5.1f ns/op | C-O2 %5.1f ns/op | speedup %.2fx\n", ta, tr, tr / ta);

        t0 = now_ns();
        for (int i = 0; i < M; i++) sink ^= (int)xwm_asm_latin1_utf8(l1, 512, u8);
        ta = (now_ns() - t0) / M;

        t0 = now_ns();
        for (int i = 0; i < M; i++) sink ^= (int)ref_latin1_utf8(l1, 512, u8);
        tr = (now_ns() - t0) / M;
        printf("  latin1_utf8 (512B)    : ASM %5.1f ns/op | C-O2 %5.1f ns/op | speedup %.2fx\n", ta, tr, tr / ta);

        (void)sink;
}

int main(void) {
        test_edge_and_security();
        test_fuzz();
        test_guard_pages();
        if (g_fails) {
                fprintf(stderr, "FAILED: %d/%d checks failed\n", g_fails, g_checks);
                return 1;
        }
        printf("xwm_wire: %d checks, 0 failures (edge + security + fuzz + mmap guard-pages PASS)\n", g_checks);
        run_bench();
        return 0;
}
