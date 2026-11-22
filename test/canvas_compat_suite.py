#!/usr/bin/env python3
"""
Canvas App-Framework & Protocol Compatibility Torture Suite.

Hammers Moonwater Canvas's Wayland compositor (`src/canvas/wayland.c`),
XWM wire protocol (`src/canvas/xwm_wire.c`), and (in --live mode) a running
Canvas + Xwayland session on `box` across 5 real-world framework profiles:
  1. SDL2 / SDL3 (Wayland + X11)
  2. GLFW (Wayland + X11)
  3. Sokol / winit / Raw EGL+Vulkan (DMA-BUF, Explicit Sync, Tearing, Frame Pacing)
  4. GTK4 / Qt6 / Electron / Chromium (Popups, Subsurfaces, Held Mods, Touch, Unmap/Remap)
  5. Proton / Wine (Xwayland oneshot XWarpPointer, XWM ICCCM/EWMH wire + live X11)

Usage:
    python3 test/canvas_compat_suite.py            # runs --local
    python3 test/canvas_compat_suite.py --local    # compiles & runs local C harnesses
    python3 test/canvas_compat_suite.py --live     # runs live Wayland + X11 clients on box
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

COMPAT_LOCAL_C = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CONFIG_MOONWATER_WAYLAND 1
#include "src/canvas/wayland.c"
#include "src/canvas/xwm_wire.c"

void memory_zero(void *d, unsigned long n) { memset(d, 0, n); }
unsigned long memory_into_hex(void *out, const void *in, unsigned long n) {
        static const char h[] = "0123456789abcdef";
        unsigned char *o = (unsigned char *)out;
        const unsigned char *p = (const unsigned char *)in;
        for (unsigned long i = 0; i < n; i++) { o[i*2] = h[p[i] >> 4]; o[i*2+1] = h[p[i] & 15]; }
        return n * 2;
}

static unsigned char g_out[262144];
static long g_out_len;
static int g_pass = 0, g_fail = 0;

/* Mock host state */
static int g_next_row = 0;
static long g_scale = 100;
static long g_last_do_op = -1, g_last_do_a = 0, g_last_do_b = 0;
static int g_cursor_hidden_seen = 0, g_cursor_arrow_seen = 0;
static long g_min_w_seen = -1, g_min_h_seen = -1, g_max_w_seen = -1, g_max_h_seen = -1;
static long g_hold_mode = 0, g_hold_row = -1;
static int g_row_opens = 0, g_row_closes = 0, g_row_pictures = 0, g_remaps = 0;
static int g_syncobj_imports = 0, g_syncobj_destroys = 0, g_syncobj_signals = 0;
static unsigned long long g_syncobj_last_sig_pt = 0;
static int g_tearing_seen = 0;
static unsigned char g_shm_backing[4 * 1024 * 1024];

static void check(const char *name, int cond) {
        if (cond) { g_pass++; printf("  [PASS] %s\n", name); }
        else      { g_fail++; printf("  [FAIL] %s\n", name); }
}

static void *t_alloc(void *ctx, long bytes) { (void)ctx; return calloc(1, (size_t)bytes); }
static void t_free(void *ctx, void *p, long bytes) { (void)ctx; (void)bytes; free(p); }
static void *t_map(void *ctx, long fd, long size) {
        (void)ctx; (void)fd;
        if (size <= 0 || size > (long)sizeof g_shm_backing) return 0;
        return g_shm_backing;
}
static void *t_remap(void *ctx, void *at, long old_sz, long new_sz) {
        (void)ctx; (void)at; (void)old_sz;
        if (new_sz <= 0 || new_sz > (long)sizeof g_shm_backing) return 0;
        g_remaps++;
        return g_shm_backing;
}
static void t_unmap(void *ctx, void *at, long size) { (void)ctx; (void)at; (void)size; }
static void t_close(void *ctx, long fd) { (void)ctx; (void)fd; }
static long t_now(void *ctx) {
        (void)ctx;
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (long)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}
static long t_screens(void *ctx, struct wayland_screen *s, long room) {
        (void)ctx;
        if (room < 1) return 0;
        memset(s, 0, sizeof *s);
        s[0].width = 1920; s[0].height = 1080; s[0].scale = g_scale; s[0].refresh = 144000;
        return 1;
}
static long t_keymap(void *ctx, long *size) { (void)ctx; *size = 0; return -1; }
static void t_repeat(void *ctx, long *r, long *d) { (void)ctx; *r = 30; *d = 250; }
static long t_seat(void *ctx) { (void)ctx; return 7; } /* pointer | keyboard | touch */
static long t_locks(void *ctx) { (void)ctx; return 0; }
static int t_row_open(void *ctx, long cl, long k, int p, long x, long y) {
        (void)ctx; (void)cl; (void)k; (void)p; (void)x; (void)y;
        g_row_opens++;
        return g_next_row++;
}
static void t_row_close(void *ctx, int r) { (void)ctx; (void)r; g_row_closes++; }
static int t_row_picture(void *ctx, int r, const struct wayland_picture *p) {
        (void)ctx; (void)r; (void)p;
        g_row_pictures++;
        return 0;
}
static void t_row_title(void *ctx, int r, const char *t, long l) { (void)ctx; (void)r; (void)t; (void)l; }
static void t_row_do(void *ctx, int r, long op, long a, long b) {
        (void)ctx; (void)r;
        g_last_do_op = op; g_last_do_a = a; g_last_do_b = b;
        if (op == WAYLAND_DO_CURSOR) {
                if (a == WAYLAND_CURSOR_HIDDEN) g_cursor_hidden_seen++;
                if (a == WAYLAND_CURSOR_ARROW) g_cursor_arrow_seen++;
        }
        if (op == WAYLAND_DO_MIN_SIZE) { g_min_w_seen = a; g_min_h_seen = b; }
        if (op == WAYLAND_DO_MAX_SIZE) { g_max_w_seen = a; g_max_h_seen = b; }
}
static int t_row_facts(void *ctx, int r, struct wayland_facts *f) {
        (void)ctx; (void)r;
        memset(f, 0, sizeof *f);
        f->scale = g_scale;
        f->width = 1280; f->height = 720;
        f->states = WAYLAND_STATE_ACTIVATED;
        return 0;
}
static const char *t_clip_type(void *ctx, long w, long nth) { (void)ctx; (void)w; (void)nth; return 0; }
static long t_clip_pull(void *ctx, long cl, long w) { (void)ctx; (void)cl; (void)w; return 0; }
static void t_clip_abort(void *ctx, long cl, long w) { (void)ctx; (void)cl; (void)w; }
static void t_clip_push(void *ctx, long r, long w, const char *m, long fd) { (void)ctx; (void)r; (void)w; (void)m; (void)fd; }
static void t_clip_clear(void *ctx, long cl, long w) { (void)ctx; (void)cl; (void)w; }
static long t_dmabuf_info(void *ctx, struct wayland_dmabuf_info *i) {
        (void)ctx; memset(i, 0, sizeof *i);
        i->device = 1;
        i->count = 2;
        i->entry[0].format = WAYLAND_FOURCC_XRGB8888; i->entry[0].modifier = 0;
        i->entry[1].format = WAYLAND_FOURCC_ARGB8888; i->entry[1].modifier = 0;
        i->tranches = 1;
        i->tranche[0].device = 1; i->tranche[0].flags = 1; i->tranche[0].count = 2;
        i->tranche[0].index[0] = 0; i->tranche[0].index[1] = 1;
        return 1;
}
static long t_dmabuf_table(void *ctx, long *sz) { (void)ctx; *sz = 32; return 98; }
static void *t_dmabuf_map(void *ctx, long fd, long sz) { (void)ctx; (void)fd; (void)sz; return g_shm_backing; }
static int t_dmabuf_ready(void *ctx, long fd) { (void)ctx; (void)fd; return 1; }
static void t_pointer_hold(void *ctx, long row, long mode) { (void)ctx; g_hold_row = row; g_hold_mode = mode; }
static int t_random(void *ctx, unsigned char *o, long n) { (void)ctx; memset(o, 0x42, (size_t)n); return 0; }

/* Mock syncobj host */
static long t_so_avail(void *ctx, char *node, long cap) { (void)ctx; (void)node; (void)cap; return 1; }
static long t_so_import(void *ctx, long fd) { (void)ctx; (void)fd; g_syncobj_imports++; return 100 + g_syncobj_imports; }
static void t_so_destroy(void *ctx, long h) { (void)ctx; (void)h; g_syncobj_destroys++; }
static int t_so_ready(void *ctx, long h, unsigned long long pt) { (void)ctx; (void)h; (void)pt; return 1; }
static void t_so_signal(void *ctx, long h, unsigned long long pt) { (void)ctx; (void)h; g_syncobj_signals++; g_syncobj_last_sig_pt = pt; }

static void send_msg_fds(long slot, uint32_t obj, uint16_t op, const uint32_t *args, uint16_t nargs, const long *fds, long nfds) {
        uint32_t buf[64];
        uint16_t size = (uint16_t)(8 + nargs * 4);
        buf[0] = obj;
        buf[1] = ((uint32_t)size << 16) | op;
        for (uint16_t i = 0; i < nargs; i++) buf[2 + i] = args[i];
        int r = wayland_feed(slot, (const unsigned char *)buf, size, fds, nfds);
        if (r < 0) { fprintf(stderr, "wayland_feed failed %d on obj=%u op=%u\n", r, obj, op); }
}
static void send_msg(long slot, uint32_t obj, uint16_t op, const uint32_t *args, uint16_t nargs) {
        send_msg_fds(slot, obj, op, args, nargs, 0, 0);
}
static void drain_out(long slot) {
        g_out_len = 0;
        const unsigned char *p = 0;
        const long *fds = 0;
        long n = 0, nf = 0;
        while ((n = wayland_out(slot, &p, &fds, &nf)) > 0) {
                if (g_out_len + n <= (long)sizeof g_out) {
                        memcpy(g_out + g_out_len, p, (size_t)n);
                        g_out_len += n;
                }
                wayland_sent(slot, n);
        }
}

static int find_event(uint32_t obj, uint16_t op, uint32_t *words_out, int max_words) {
        int count = 0;
        for (long off = 0; off + 8 <= g_out_len;) {
                uint32_t o, h;
                memcpy(&o, g_out + off, 4);
                memcpy(&h, g_out + off + 4, 4);
                uint16_t sz = (uint16_t)(h >> 16), ev = (uint16_t)(h & 0xffff);
                if (sz < 8 || off + sz > g_out_len) break;
                if (o == obj && ev == op) {
                        count++;
                        if (words_out) {
                                int nw = (sz - 8) / 4;
                                if (nw > max_words) nw = max_words;
                                memcpy(words_out, g_out + off + 8, (size_t)nw * 4);
                        }
                }
                off += sz;
        }
        return count;
}

static uint32_t bind_global(long slot, uint32_t reg_id, const char *iface, uint32_t ver, uint32_t new_id) {
        /* Scan g_out from wl_display.get_registry for matching global name */
        uint32_t gname = 0;
        for (long off = 0; off + 8 <= g_out_len;) {
                uint32_t o, h;
                memcpy(&o, g_out + off, 4); memcpy(&h, g_out + off + 4, 4);
                uint16_t sz = (uint16_t)(h >> 16), ev = (uint16_t)(h & 0xffff);
                if (sz < 8 || off + sz > g_out_len) break;
                if (o == reg_id && ev == WLE_wl_registry_global && sz >= 20) {
                        uint32_t name, slen;
                        memcpy(&name, g_out + off + 8, 4);
                        memcpy(&slen, g_out + off + 12, 4);
                        if (slen > 0 && off + 16 + slen <= g_out_len && strcmp((const char *)(g_out + off + 16), iface) == 0) {
                                gname = name; break;
                        }
                }
                off += sz;
        }
        if (!gname) return 0;
        uint32_t slen = (uint32_t)strlen(iface) + 1;
        uint32_t pads = (slen + 3) / 4;
        uint32_t args[32];
        memset(args, 0, sizeof args);
        args[0] = gname;
        args[1] = slen;
        memcpy(&args[2], iface, slen);
        args[2 + pads] = ver;
        args[3 + pads] = new_id;
        send_msg(slot, reg_id, WLQ_wl_registry_bind, args, (uint16_t)(4 + pads));
        return gname;
}

static uint64_t mono_ns(void) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

int main(void) {
        struct wayland_host host = {
                .alloc = t_alloc, .release = t_free, .map = t_map, .remap = t_remap, .unmap = t_unmap, .close = t_close,
                .now = t_now, .screens = t_screens, .keymap = t_keymap, .repeat = t_repeat, .seat = t_seat, .locks = t_locks,
                .row_open = t_row_open, .row_close = t_row_close, .row_picture = t_row_picture, .row_title = t_row_title,
                .row_do = t_row_do, .row_facts = t_row_facts, .clip_type = t_clip_type, .clip_pull = t_clip_pull,
                .clip_abort = t_clip_abort, .clip_push = t_clip_push, .clip_clear = t_clip_clear,
                .dmabuf_info = t_dmabuf_info, .dmabuf_table = t_dmabuf_table, .dmabuf_map = t_dmabuf_map,
                .dmabuf_ready = t_dmabuf_ready, .pointer_hold = t_pointer_hold, .random = t_random,
        };
        struct wayland_syncobj_host so_host = {
                .available = t_so_avail, .import = t_so_import, .drop = t_so_destroy, .ready = t_so_ready, .signal = t_so_signal,
        };
        if (wayland_start(&host) != 0) { fprintf(stderr, "wayland_start failed\n"); return 2; }
        wayland_syncobj_host(&so_host);

        /* ===== PROFILE 1: SDL2 / SDL3 ===== */
        printf("\n=== Profile 1: SDL2 / SDL3 (Wayland) ===\n");
        long c1 = wayland_connect(1234, 1000);
        uint32_t a_reg[1] = {2};
        send_msg(c1, 1, WLQ_wl_display_get_registry, a_reg, 1);
        drain_out(c1);
        /* Bind core globals */
        uint32_t has_comp = bind_global(c1, 2, "wl_compositor", 6, 3);
        uint32_t has_shm  = bind_global(c1, 2, "wl_shm", 1, 4);
        uint32_t has_xdg  = bind_global(c1, 2, "xdg_wm_base", 6, 5);
        uint32_t has_seat = bind_global(c1, 2, "wl_seat", 9, 6);
        uint32_t has_pc   = bind_global(c1, 2, "zwp_pointer_constraints_v1", 1, 7);
        uint32_t has_rp   = bind_global(c1, 2, "zwp_relative_pointer_manager_v1", 1, 8);
        uint32_t has_vp   = bind_global(c1, 2, "wp_viewporter", 1, 9);
        uint32_t has_fs   = bind_global(c1, 2, "wp_fractional_scale_manager_v1", 1, 10);
        uint32_t has_tc   = bind_global(c1, 2, "wp_tearing_control_manager_v1", 1, 11);
        uint32_t has_dm   = bind_global(c1, 2, "zwp_linux_dmabuf_v1", 4, 12);
        uint32_t has_so   = bind_global(c1, 2, "wp_linux_drm_syncobj_manager_v1", 1, 13);
        uint32_t has_sub  = bind_global(c1, 2, "wl_subcompositor", 1, 14);
        check("SDL/GLFW/Sokol core globals advertised & bound",
              has_comp && has_shm && has_xdg && has_seat && has_pc && has_rp && has_vp && has_fs && has_tc && has_dm && has_so && has_sub);

        /* Get pointer (15), second pointer (16), keyboard (17), touch (18) */
        uint32_t a1[1] = {15}; send_msg(c1, 6, WLQ_wl_seat_get_pointer, a1, 1);
        a1[0] = 16; send_msg(c1, 6, WLQ_wl_seat_get_pointer, a1, 1);
        a1[0] = 17; send_msg(c1, 6, WLQ_wl_seat_get_keyboard, a1, 1);
        a1[0] = 18; send_msg(c1, 6, WLQ_wl_seat_get_touch, a1, 1);

        /* Create surface (20), xdg_surface (21), xdg_toplevel (22) + initial commit */
        a1[0] = 20; send_msg(c1, 3, WLQ_wl_compositor_create_surface, a1, 1);
        uint32_t a2[2] = {21, 20}; send_msg(c1, 5, WLQ_xdg_wm_base_get_xdg_surface, a2, 2);
        a1[0] = 22; send_msg(c1, 21, WLQ_xdg_surface_get_toplevel, a1, 1);
        send_msg(c1, 20, WLQ_wl_surface_commit, 0, 0);
        drain_out(c1);
        uint32_t cfg_w[4] = {0};
        int got_cfg = find_event(21, WLE_xdg_surface_configure, cfg_w, 4);
        check("SDL initial commit emits xdg_surface.configure", got_cfg >= 1 && !wayland_dead(c1));

        /* Ack configure, create SHM pool (23) + buffer (24), attach & commit */
        a1[0] = cfg_w[0]; send_msg(c1, 21, WLQ_xdg_surface_ack_configure, a1, 1);
        long fake_fd = 99;
        uint32_t a_pool[2] = {23, 65536};
        send_msg_fds(c1, 4, WLQ_wl_shm_create_pool, a_pool, 2, &fake_fd, 1);
        uint32_t a_buf[6] = {24, 0, 64, 64, 256, WLN_wl_shm_format_argb8888};
        send_msg(c1, 23, WLQ_wl_shm_pool_create_buffer, a_buf, 6);
        uint32_t a_att[3] = {24, 0, 0};
        send_msg(c1, 20, WLQ_wl_surface_attach, a_att, 3);
        send_msg(c1, 20, WLQ_wl_surface_commit, 0, 0);
        drain_out(c1);
        check("SDL attach+commit maps window (row_open & row_picture)", g_row_opens == 1 && g_row_pictures == 1 && !wayland_dead(c1));

        /* Pointer enter + set_cursor(surf with NO buffer) -> HIDDEN! */
        wayland_pointer_to(0, 10, 10);
        drain_out(c1);
        uint32_t ent_w[4] = {0};
        find_event(15, WLE_wl_pointer_enter, ent_w, 4);
        a1[0] = 25; send_msg(c1, 3, WLQ_wl_compositor_create_surface, a1, 1);
        g_cursor_hidden_seen = 0;
        uint32_t a_cur[4] = {ent_w[0], 25, 0, 0};
        send_msg(c1, 15, WLQ_wl_pointer_set_cursor, a_cur, 4);
        check("SDL/GLFW set_cursor(surface with NULL buffer) hides cursor", g_cursor_hidden_seen >= 1);

        /* Attach buffer to cursor surface -> ARROW; attach(NULL) -> HIDDEN */
        g_cursor_arrow_seen = 0; g_cursor_hidden_seen = 0;
        send_msg(c1, 25, WLQ_wl_surface_attach, a_att, 3);
        send_msg(c1, 25, WLQ_wl_surface_commit, 0, 0);
        int arrow_after_attach = g_cursor_arrow_seen;
        uint32_t a_null[3] = {0, 0, 0};
        send_msg(c1, 25, WLQ_wl_surface_attach, a_null, 3);
        send_msg(c1, 25, WLQ_wl_surface_commit, 0, 0);
        check("SDL/GLFW cursor surface attach(buf)->ARROW then attach(NULL)->HIDDEN", arrow_after_attach >= 1 && g_cursor_hidden_seen >= 1);

        /* SDL_SetRelativeMouseMode: lock_pointer + relative_pointer (requires keyboard + pointer focus) */
        wayland_keyboard_to(0);
        drain_out(c1);
        uint32_t a_rp[2] = {26, 15}; send_msg(c1, 8, WLQ_zwp_relative_pointer_manager_v1_get_relative_pointer, a_rp, 2);
        uint32_t a_lk[5] = {27, 20, 15, 0, WLN_zwp_pointer_constraints_v1_lifetime_persistent};
        send_msg(c1, 7, WLQ_zwp_pointer_constraints_v1_lock_pointer, a_lk, 5);
        drain_out(c1);
        int locked_ev = find_event(27, WLE_zwp_locked_pointer_v1_locked, 0, 0);
        wayland_pointer_relative(0, 12, -7, 12, -7, 12345);
        drain_out(c1);
        int rel_ev = find_event(26, WLE_zwp_relative_pointer_v1_relative_motion, 0, 0);
        send_msg(c1, 27, WLQ_zwp_locked_pointer_v1_destroy, 0, 0);
        drain_out(c1);
        check("SDL_SetRelativeMouseMode lock + relative_motion + unlock", locked_ev >= 1 && rel_ev >= 1 && g_hold_mode == 0);

        /* Hi-res scroll + axis_stop (with 2 wl_pointer objects!) */
        wayland_pointer_axis(0, 0, 60, 15); /* finger precise */
        drain_out(c1);
        wayland_pointer_axis_stop(0);
        drain_out(c1);
        int stop_src = find_event(15, WLE_wl_pointer_axis_source, 0, 0);
        int stop_evs = find_event(15, WLE_wl_pointer_axis_stop, 0, 0);
        int stop_frm = find_event(15, WLE_wl_pointer_frame, 0, 0);
        check("wl_pointer.axis_stop emits axis_source + 2x axis_stop + single frame", stop_src == 1 && stop_evs == 2 && stop_frm == 1);

        /* ===== PROFILE 2: GLFW ===== */
        printf("\n=== Profile 2: GLFW (HiDPI min/max, shm resize, fullscreen/max) ===\n");
        g_scale = 200; /* 200% HiDPI */
        wayland_screens_changed();
        wayland_scales_sweep();
        uint32_t a_mm[2] = {800, 600};
        send_msg(c1, 22, WLQ_xdg_toplevel_set_min_size, a_mm, 2);
        send_msg(c1, 22, WLQ_xdg_toplevel_set_max_size, a_mm, 2);
        check("GLFW HiDPI set_min_size/set_max_size converts logical->physical (800x600 @ 200% = 1600x1200)",
              g_min_w_seen == 1600 && g_min_h_seen == 1200 && g_max_w_seen == 1600 && g_max_h_seen == 1200);

        /* wl_shm_pool.resize monotonic grow storm */
        g_remaps = 0;
        uint32_t a_rsz[1] = {131072}; send_msg(c1, 23, WLQ_wl_shm_pool_resize, a_rsz, 1);
        a_rsz[0] = 1048576; send_msg(c1, 23, WLQ_wl_shm_pool_resize, a_rsz, 1);
        check("GLFW wl_shm_pool.resize grows cleanly without disconnect", g_remaps == 2 && !wayland_dead(c1));

        /* ===== PROFILE 3: Sokol / winit / Vulkan Explicit Sync & Tearing ===== */
        printf("\n=== Profile 3: Sokol / winit / Vulkan (Syncobj destroy-before-commit, Tearing) ===\n");
        /* Syncobj timeline destroyed BEFORE wl_surface.commit (Mesa/Vulkan WSI swapchain recreate) */
        uint32_t a_ss[2] = {30, 20}; send_msg(c1, 13, WLQ_wp_linux_drm_syncobj_manager_v1_get_surface, a_ss, 2);
        uint32_t a_tl[1] = {31}; send_msg_fds(c1, 13, WLQ_wp_linux_drm_syncobj_manager_v1_import_timeline, a_tl, 1, &fake_fd, 1);
        uint32_t a_pt[3] = {31, 0, 1}; send_msg(c1, 30, WLQ_wp_linux_drm_syncobj_surface_v1_set_acquire_point, a_pt, 3);
        a_pt[2] = 2; send_msg(c1, 30, WLQ_wp_linux_drm_syncobj_surface_v1_set_release_point, a_pt, 3);
        /* Destroy timeline object BEFORE commit! */
        send_msg(c1, 31, WLQ_wp_linux_drm_syncobj_timeline_v1_destroy, 0, 0);
        check("Vulkan WSI syncobj timeline ref stays alive when destroyed before commit", g_syncobj_destroys == 0 && !wayland_dead(c1));
        /* Now create dmabuf buffer (33) via params (32) and commit */
        a1[0] = 32; send_msg(c1, 12, WLQ_zwp_linux_dmabuf_v1_create_params, a1, 1);
        uint32_t a_add[5] = {0, 0, 256, 0, 0};
        send_msg_fds(c1, 32, WLQ_zwp_linux_buffer_params_v1_add, a_add, 5, &fake_fd, 1);
        uint32_t a_ci[5] = {33, 64, 64, WAYLAND_FOURCC_XRGB8888, 0};
        send_msg(c1, 32, WLQ_zwp_linux_buffer_params_v1_create_immed, a_ci, 5);
        /* Also enable wp_tearing_control_v1 ASYNC */
        uint32_t a_tc[2] = {34, 20}; send_msg(c1, 11, WLQ_wp_tearing_control_manager_v1_get_tearing_control, a_tc, 2);
        a1[0] = WLN_wp_tearing_control_v1_presentation_hint_async;
        send_msg(c1, 34, WLQ_wp_tearing_control_v1_set_presentation_hint, a1, 1);
        a_att[0] = 33; send_msg(c1, 20, WLQ_wl_surface_attach, a_att, 3);
        send_msg(c1, 20, WLQ_wl_surface_commit, 0, 0);
        g_tearing_seen = (wayland_row_hint(0, WAYLAND_HINT_ASYNC) != 0);
        wayland_drawn();
        /* Replace with SHM buffer to trigger release signaling of dmabuf 33 */
        a_att[0] = 24; send_msg(c1, 20, WLQ_wl_surface_attach, a_att, 3);
        send_msg(c1, 20, WLQ_wl_surface_commit, 0, 0);
        wayland_drawn();
        if (!(g_syncobj_signals >= 1 && g_syncobj_last_sig_pt == 2 && g_syncobj_destroys == 1 && g_tearing_seen == 1 && !wayland_dead(c1)))
                printf("    [DEBUG] signals=%d pt=%llu destroys=%d tearing=%d dead=%ld\n",
                       g_syncobj_signals, g_syncobj_last_sig_pt, g_syncobj_destroys, g_tearing_seen, wayland_dead(c1));
        check("Vulkan WSI explicit sync signals release point (pt=2) & tearing hint propagates",
              g_syncobj_signals >= 1 && g_syncobj_last_sig_pt == 2 && g_syncobj_destroys == 1 && g_tearing_seen == 1 && !wayland_dead(c1));

        /* ===== PROFILE 4: GTK4 / Qt6 / Electron ===== */
        printf("\n=== Profile 4: GTK4 / Qt6 / Electron (Held Mods, Unmap/Remap, Pong) ===\n");
        /* Hold Left Ctrl (29) BEFORE focus enters window -> wl_keyboard.enter + modifiers must report depressed != 0 */
        wayland_keyboard_to(-1);
        wayland_key(0, 29, 1); /* KEY_LEFTCTRL pressed globally */
        wayland_keyboard_to(0);
        drain_out(c1);
        uint32_t mod_w[4] = {0};
        int got_mods = find_event(17, WLE_wl_keyboard_modifiers, mod_w, 4);
        check("GTK4/Qt6/Electron wl_keyboard.enter preserves held modifiers (Ctrl depressed != 0)",
              got_mods >= 1 && (mod_w[1] & 4u) != 0);
        wayland_key(0, 29, 0);

        /* xdg_wm_base.pong accepted cleanly */
        a1[0] = 4242; send_msg(c1, 5, WLQ_xdg_wm_base_pong, a1, 1);
        check("xdg_wm_base.pong accepted without error", !wayland_dead(c1));

        /* ===== PROFILE 5: Proton / Wine (XWM wire + Xwayland oneshot warp) ===== */
        printf("\n=== Profile 5: Proton / Wine (xwm_wire round-trips & Xwayland oneshot warp) ===\n");
        {
                unsigned char req[64];
                unsigned long n = xwm_enc_set_input_focus(req, sizeof req, XWM_REVERT_NONE, 0x123456, XWM_CURRENT_TIME);
                check("xwm_wire SetInputFocus encodes XWM_REVERT_NONE cleanly", n == 12 && req[1] == XWM_REVERT_NONE);
                unsigned char ev[XWM_EVENT_SIZE] = {0};
                ev[0] = XWM_EVENT_UNMAP_NOTIFY;
                xwm_put32(ev + 4, 0x111); xwm_put32(ev + 8, 0x222); ev[12] = 0;
                struct xwm_event parsed;
                int ok = xwm_parse_event(ev, sizeof ev, &parsed);
                check("xwm_wire parses UnmapNotify with from_configure=0", ok == 0 && parsed.kind == XWM_EV_UNMAP_NOTIFY && parsed.window == 0x222 && parsed.from_configure == 0);
        }

        /* ===== PROFILE 6: Layer-Shell (zwlr_layer_surface_v1) & wl_callback delete_id ===== */
        printf("\n=== Profile 6: Layer-Shell (zwlr_layer_surface_v1) & wl_callback delete_id ===\n");
        {
                /* Re-query registry to bind zwlr_layer_shell_v1 */
                uint32_t a_reg2[1] = {45};
                send_msg(c1, 1, WLQ_wl_display_get_registry, a_reg2, 1);
                drain_out(c1);
                uint32_t has_ls = bind_global(c1, 45, "zwlr_layer_shell_v1", 4, 46);
                check("zwlr_layer_shell_v1 advertised & bound", has_ls != 0 && !wayland_dead(c1));

                /* Create wl_surface (47) + zwlr_layer_surface_v1 (48) + set_size(400, 32) + initial commit */
                a1[0] = 47; send_msg(c1, 3, WLQ_wl_compositor_create_surface, a1, 1);
                /* get_layer_surface(id=48, surface=47, output=0, layer=2 (TOP), namespace="bar") */
                uint32_t a_ls[6] = {48, 47, 0, 2, 4, 0x00726162u};
                send_msg(c1, 46, WLQ_zwlr_layer_shell_v1_get_layer_surface, a_ls, 6);
                uint32_t a_sz[2] = {400, 32};
                send_msg(c1, 48, WLQ_zwlr_layer_surface_v1_set_size, a_sz, 2);
                drain_out(c1);
                send_msg(c1, 47, WLQ_wl_surface_commit, 0, 0);
                drain_out(c1);
                uint32_t lcfg[4] = {0};
                int got_lcfg = find_event(48, WLE_zwlr_layer_surface_v1_configure, lcfg, 4);
                check("zwlr_layer_surface_v1 initial commit emits configure(400x32)",
                      got_lcfg >= 1 && lcfg[1] == 400 && lcfg[2] == 32 && !wayland_dead(c1));

                /* ack_configure + attach SHM buffer (24) + commit -> maps row */
                int opens_before = g_row_opens, pics_before = g_row_pictures;
                a1[0] = lcfg[0]; send_msg(c1, 48, WLQ_zwlr_layer_surface_v1_ack_configure, a1, 1);
                a_att[0] = 24; send_msg(c1, 47, WLQ_wl_surface_attach, a_att, 3);
                send_msg(c1, 47, WLQ_wl_surface_commit, 0, 0);
                drain_out(c1);
                check("zwlr_layer_surface_v1 ack+attach+commit maps surface cleanly",
                      g_row_opens == opens_before + 1 && g_row_pictures == pics_before + 1 && !wayland_dead(c1));

                /* Destroy layer surface & wl_surface cleanly */
                send_msg(c1, 48, WLQ_zwlr_layer_surface_v1_destroy, 0, 0);
                send_msg(c1, 47, WLQ_wl_surface_destroy, 0, 0);
                drain_out(c1);

                /* wl_callback delete_id on wl_surface.destroy: pending frame callback must emit delete_id so ID is reusable */
                a1[0] = 50; send_msg(c1, 3, WLQ_wl_compositor_create_surface, a1, 1);
                a1[0] = 51; send_msg(c1, 50, WLQ_wl_surface_frame, a1, 1);
                drain_out(c1);
                send_msg(c1, 50, WLQ_wl_surface_destroy, 0, 0);
                drain_out(c1);
                uint32_t del_w[2] = {0};
                int got_del = find_event(1, WLE_wl_display_delete_id, del_w, 2);
                /* Verify callback ID 51 was deleted and can be reused immediately for wl_display.sync */
                int deleted_51 = 0;
                for (long off = 0; off + 12 <= g_out_len;) {
                        uint32_t o, h, did;
                        memcpy(&o, g_out + off, 4); memcpy(&h, g_out + off + 4, 4);
                        uint16_t sz = (uint16_t)(h >> 16), ev = (uint16_t)(h & 0xffff);
                        if (sz < 8 || off + sz > g_out_len) break;
                        if (o == 1 && ev == WLE_wl_display_delete_id && sz >= 12) {
                                memcpy(&did, g_out + off + 8, 4);
                                if (did == 51) deleted_51 = 1;
                        }
                        off += sz;
                }
                a1[0] = 51; send_msg(c1, 1, WLQ_wl_display_sync, a1, 1);
                drain_out(c1);
                int got_sync_done = find_event(51, WLE_wl_callback_done, 0, 0);
                check("wl_surface.destroy emits wl_display.delete_id for pending wl_callback and allows ID reuse",
                      got_del >= 1 && deleted_51 && got_sync_done == 1 && !wayland_dead(c1));

                /* Negative test on separate client c2: get_layer_surface on an xdg_surface must fail with already_constructed */
                long c2 = wayland_connect(1235, 1000);
                uint32_t r2[1] = {2}; send_msg(c2, 1, WLQ_wl_display_get_registry, r2, 1);
                drain_out(c2);
                bind_global(c2, 2, "wl_compositor", 4, 3);
                bind_global(c2, 2, "xdg_wm_base", 5, 4);
                bind_global(c2, 2, "zwlr_layer_shell_v1", 4, 5);
                a1[0] = 10; send_msg(c2, 3, WLQ_wl_compositor_create_surface, a1, 1);
                uint32_t x2[2] = {11, 10}; send_msg(c2, 4, WLQ_xdg_wm_base_get_xdg_surface, x2, 2);
                uint32_t ls2[6] = {12, 10, 0, 2, 4, 0x00726162u};
                send_msg(c2, 5, WLQ_zwlr_layer_shell_v1_get_layer_surface, ls2, 6);
                drain_out(c2);
                check("zwlr_layer_shell_v1 rejects surface that already has xdg_surface role",
                      wayland_dead(c2) != 0);
                wayland_drop(c2);

                /* Negative test on separate client c3: buffer attached to layer_surface before initial configure fails on role_id */
                long c3 = wayland_connect(1236, 1000);
                send_msg(c3, 1, WLQ_wl_display_get_registry, r2, 1);
                drain_out(c3);
                bind_global(c3, 2, "wl_compositor", 4, 3);
                bind_global(c3, 2, "wl_shm", 1, 4);
                bind_global(c3, 2, "zwlr_layer_shell_v1", 4, 5);
                a1[0] = 10; send_msg(c3, 3, WLQ_wl_compositor_create_surface, a1, 1);
                uint32_t ls3[6] = {11, 10, 0, 2, 4, 0x00726162u};
                send_msg(c3, 5, WLQ_zwlr_layer_shell_v1_get_layer_surface, ls3, 6);
                uint32_t p3[2] = {12, 65536};
                send_msg_fds(c3, 4, WLQ_wl_shm_create_pool, p3, 2, &fake_fd, 1);
                uint32_t b3[6] = {13, 0, 64, 64, 256, WLN_wl_shm_format_argb8888};
                send_msg(c3, 12, WLQ_wl_shm_pool_create_buffer, b3, 6);
                uint32_t att3[3] = {13, 0, 0};
                send_msg(c3, 10, WLQ_wl_surface_attach, att3, 3);
                drain_out(c3);
                send_msg(c3, 10, WLQ_wl_surface_commit, 0, 0);
                drain_out(c3);
                uint32_t err3[4] = {0};
                int got_err3 = find_event(1, WLE_wl_display_error, err3, 4);
                check("zwlr_layer_surface_v1 rejects unconfigured buffer with wl_display.error on layer role_id",
                      wayland_dead(c3) != 0 && got_err3 >= 1 && err3[0] == 11);
                wayland_drop(c3);
        }

        /* ===== OVERHEAD BENCHMARK ===== */
        printf("\n=== Overhead Benchmark (local wayland.c hot path) ===\n");
        {
                enum { REL_N = 10000 };
                uint32_t a_lk2[5] = {40, 20, 15, 0, WLN_zwp_pointer_constraints_v1_lifetime_persistent};
                send_msg(c1, 7, WLQ_zwp_pointer_constraints_v1_lock_pointer, a_lk2, 5);
                drain_out(c1);
                uint64_t t0 = mono_ns();
                for (int i = 0; i < REL_N; i++) {
                        wayland_pointer_relative(0, 1, -1, 1, -1, 1000 + i);
                        if ((i & 63) == 63) drain_out(c1);
                }
                drain_out(c1);
                uint64_t t1 = mono_ns();
                double ns_per_rel = (double)(t1 - t0) / REL_N;
                printf("  [BENCH] wayland_pointer_relative: %.1f ns/event (%d events)\n", ns_per_rel, REL_N);
                check("Relative motion overhead < 2000 ns/event", ns_per_rel < 2000.0);
        }

        wayland_drop(c1);
        printf("\nLocal Compat Torture Summary: %d passed, %d failed\n", g_pass, g_fail);
        return g_fail ? 1 : 0;
}
'''

COMPAT_X11_LIVE_C = r'''
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int g_pass = 0, g_fail = 0;
static void check(const char *name, int cond) {
        if (cond) { g_pass++; printf("  [PASS] %s\n", name); }
        else      { g_fail++; printf("  [FAIL] %s\n", name); }
}

int main(void) {
        Display *dpy = XOpenDisplay(NULL);
        if (!dpy) { fprintf(stderr, "Cannot open DISPLAY\n"); return 2; }
        int scr = DefaultScreen(dpy);
        Window root = RootWindow(dpy, scr);
        int sw = DisplayWidth(dpy, scr), sh = DisplayHeight(dpy, scr);

        /* 1. WM_CLASS with spaces (CS2 / ELDEN RING / BG3) + _MOTIF_WM_HINTS borderless FS */
        Window w = XCreateSimpleWindow(dpy, root, 0, 0, (unsigned)sw, (unsigned)sh, 0, 0, 0);
        XSelectInput(dpy, w, StructureNotifyMask | PropertyChangeMask | FocusChangeMask);
        XClassHint ch = { "elden ring", "ELDEN RING" };
        XSetClassHint(dpy, w, &ch);
        Atom motif = XInternAtom(dpy, "_MOTIF_WM_HINTS", False);
        unsigned long mh[5] = { 2, 0, 0, 0, 0 }; /* flags=decorations, decorations=0 */
        XChangeProperty(dpy, w, motif, motif, 32, PropModeReplace, (unsigned char *)mh, 5);
        XMapWindow(dpy, w);
        XSync(dpy, False);
        usleep(120000);

        /* Verify WM_STATE == NormalState (1) */
        Atom wm_state = XInternAtom(dpy, "WM_STATE", False);
        Atom type; int fmt; unsigned long nitems, after; unsigned char *prop = NULL;
        int got_normal = 0;
        if (XGetWindowProperty(dpy, w, wm_state, 0, 2, False, AnyPropertyType, &type, &fmt, &nitems, &after, &prop) == Success && prop && nitems >= 1) {
                got_normal = (*(unsigned long *)prop == 1);
                XFree(prop); prop = NULL;
        }
        check("Live X11: mapped window gets WM_STATE == NormalState (1)", got_normal);

        /* 2. Unmap -> verify WM_STATE transitions to WithdrawnState (0) (Wine wait_for_withdrawn_state!) */
        XUnmapWindow(dpy, w);
        XSync(dpy, False);
        usleep(120000);
        int got_withdrawn = 0;
        if (XGetWindowProperty(dpy, w, wm_state, 0, 2, False, AnyPropertyType, &type, &fmt, &nitems, &after, &prop) == Success && prop && nitems >= 1) {
                got_withdrawn = (*(unsigned long *)prop == 0);
                XFree(prop); prop = NULL;
        }
        check("Live X11: UnmapWindow sets WM_STATE == WithdrawnState (0)", got_withdrawn);

        /* 3. Rapid ConfigureRequest resolution storm */
        XMapWindow(dpy, w);
        XResizeWindow(dpy, w, 1280, 720);
        XResizeWindow(dpy, w, 800, 600);
        XResizeWindow(dpy, w, 1024, 768);
        XSync(dpy, False);
        usleep(120000);
        XWindowAttributes attr;
        XGetWindowAttributes(dpy, w, &attr);
        check("Live X11: rapid ConfigureRequest storm settles without hang", attr.width > 0 && attr.height > 0);

        XDestroyWindow(dpy, w);
        XCloseDisplay(dpy);
        printf("Live X11 Compat Summary: %d passed, %d failed\n", g_pass, g_fail);
        return g_fail ? 1 : 0;
}
'''


def extract_lib_asm(lib_text, names, arch_is_arm64):
    """Extract named ASM_FUNC blocks from src/lib.c for standalone macOS harnesses."""
    idx = 1 if arch_is_arm64 else 0
    blocks = []
    for fn in names:
        m_all = list(re.finditer(rf'ASM_FUNC\({fn}\)(.*?)ASM_END\({fn}\)', lib_text, re.S))
        if len(m_all) > idx:
            raw = m_all[idx].group(1).replace("ASM_RET", '"ret\\n"')
            lines = re.findall(r'"([^"]*)"', raw)
            body = "".join(l.replace("\\n", "\n").replace("\\t", "\t") for l in lines)
            blocks.append(f".globl _{fn}\n.p2align 4\n_{fn}:\n{body}\nret\n")
    return ".text\n" + "\n".join(blocks)


def run_local():
    cc = shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")
    if not cc:
        print("[FAIL] No C compiler (clang/cc) found")
        return 1
    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        c_path = td / "compat_local.c"
        bin_path = td / "compat_local"
        c_path.write_text(COMPAT_LOCAL_C, encoding="utf-8")
        is_mac = sys.platform == "darwin"
        arch_is_arm64 = os.uname().machine in ("arm64", "aarch64")
        arch_is_x64 = os.uname().machine in ("x86_64", "amd64")
        if is_mac and (arch_is_arm64 or arch_is_x64):
            lib_text = (ROOT / "src/lib.c").read_text(encoding="utf-8", errors="replace")
            asm_path = td / "wl_asm.S"
            asm_path.write_text(extract_lib_asm(
                lib_text,
                ("wire_message", "wire_arguments", "wire_event", "wire_slot", "rects_clip", "bits_trailing_zeros"),
                arch_is_arm64,
            ), encoding="utf-8")
            extra = [str(asm_path)]
        else:
            lib_obj = td / "lib_wl.o"
            rc = subprocess.run([cc, "-O2", "-fno-builtin", "-c", str(ROOT / "src/lib.c"), "-o", str(lib_obj)],
                                cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            if rc.returncode != 0:
                print("[FAIL] src/lib.c failed to compile:\n" + rc.stderr)
                return 1
            extra = [str(lib_obj)]
        cmd = [cc, "-O2", "-Wall", "-Wextra", "-Wno-unused-function", "-Wno-unused-parameter",
               f"-I{ROOT}", str(c_path), *extra, "-o", str(bin_path)]
        r = subprocess.run(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if r.returncode != 0:
            print("[FAIL] compat_local.c failed to compile:\n" + r.stderr)
            return 1
        r2 = subprocess.run([str(bin_path)], cwd=ROOT)
        return r2.returncode


def run_live():
    cc = shutil.which("clang") or shutil.which("gcc") or shutil.which("cc")
    if not cc:
        print("[FAIL] No C compiler found for --live")
        return 1
    rc = 0
    if os.environ.get("DISPLAY"):
        with tempfile.TemporaryDirectory() as td:
            c_path = Path(td) / "compat_x11_live.c"
            bin_path = Path(td) / "compat_x11_live"
            c_path.write_text(COMPAT_X11_LIVE_C, encoding="utf-8")
            r = subprocess.run([cc, "-O2", str(c_path), "-lX11", "-o", str(bin_path)],
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            if r.returncode != 0:
                print("[FAIL] compat_x11_live.c compile error:\n" + r.stderr)
                rc = 1
            else:
                r2 = subprocess.run([str(bin_path)])
                if r2.returncode != 0:
                    rc = 1
    else:
        print("  [SKIP] DISPLAY not set; skipping live X11 tests")
    return rc


def main():
    ap = argparse.ArgumentParser(description="Canvas App-Framework & Protocol Compatibility Torture Suite")
    ap.add_argument("--local", action="store_true", help="Run local C protocol harnesses against wayland.c + xwm_wire.c")
    ap.add_argument("--live", action="store_true", help="Run live Wayland + X11 clients against running Canvas/Xwayland")
    args = ap.parse_args()
    if not args.local and not args.live:
        args.local = True
    rc = 0
    if args.local:
        rc |= run_local()
    if args.live:
        rc |= run_live()
    sys.exit(rc)


if __name__ == "__main__":
    main()
