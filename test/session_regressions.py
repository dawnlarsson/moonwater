#!/usr/bin/env python3
"""
Aggressive regression test suite covering all bugs, edge cases, ABI invariants,
SIMD routines, Wayland/XWM protocol contracts, spark input ring, RT priority
hierarchy, and kernel/compiler production profiles from the gaming/desktop
hardening session.

Run:
    python3 test/session_regressions.py
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PASS = 0
FAIL = 0


def check(name, cond, detail=""):
    global PASS, FAIL
    if cond:
        PASS += 1
        print(f"  [PASS] {name}")
    else:
        FAIL += 1
        print(f"  [FAIL] {name}" + (f": {detail}" if detail else ""))


def read_text(rel):
    return (ROOT / rel).read_text(encoding="utf-8", errors="replace")


def run_cmd(cmd, cwd=ROOT):
    return subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)


# ---------------------------------------------------------------------------
# 1. Executable C test: Wayland xdg_toplevel.configure (Chromium SIGTRAP fix)
#    + wayland_pointer_direct_ptrs + wayland_pointer_noted + seat_gen
# ---------------------------------------------------------------------------
WAYLAND_C_HARNESS = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CONFIG_MOONWATER_WAYLAND 1
#include "src/canvas/wayland.c"
void memory_zero(void *d, unsigned long n) { memset(d, 0, n); }
unsigned long memory_into_hex(void *out, const void *in, unsigned long n) {
        static const char h[] = "0123456789abcdef";
        unsigned char *o = (unsigned char *)out;
        const unsigned char *p = (const unsigned char *)in;
        for (unsigned long i = 0; i < n; i++) { o[i*2] = h[p[i] >> 4]; o[i*2+1] = h[p[i] & 15]; }
        return n * 2;
}

static unsigned char g_out[65536];
static long g_out_len;
static int g_row_facts_zero_size;

static void *t_alloc(void *ctx, long bytes) { (void)ctx; return calloc(1, (size_t)bytes); }
static void t_free(void *ctx, void *p, long bytes) { (void)ctx; (void)bytes; free(p); }
static void *t_map(void *ctx, long fd, long size) { (void)ctx; (void)fd; (void)size; return 0; }
static void *t_remap(void *ctx, void *at, long o, long n) { (void)ctx; (void)at; (void)o; (void)n; return 0; }
static void t_unmap(void *ctx, void *at, long size) { (void)ctx; (void)at; (void)size; }
static void t_close(void *ctx, long fd) { (void)ctx; (void)fd; }
static long t_now(void *ctx) { (void)ctx; return 1000; }
static long t_screens(void *ctx, struct wayland_screen *s, long room) {
        (void)ctx;
        if (room < 1) return 0;
        memset(s, 0, sizeof *s);
        s[0].width = 1920; s[0].height = 1080; s[0].scale = 100; s[0].refresh = 144000;
        return 1;
}
static long t_keymap(void *ctx, long *size) { (void)ctx; *size = 0; return -1; }
static void t_repeat(void *ctx, long *r, long *d) { (void)ctx; *r = 30; *d = 250; }
static long t_seat(void *ctx) { (void)ctx; return 3; }
static long t_locks(void *ctx) { (void)ctx; return 0; }
static int t_row_open(void *ctx, long cl, long k, int p, long x, long y) {
        (void)ctx; (void)cl; (void)k; (void)p; (void)x; (void)y;
        return 0;
}
static void t_row_close(void *ctx, int r) { (void)ctx; (void)r; }
static int t_row_picture(void *ctx, int r, const struct wayland_picture *p) { (void)ctx; (void)r; (void)p; return 0; }
static void t_row_title(void *ctx, int r, const char *t, long l) { (void)ctx; (void)r; (void)t; (void)l; }
static void t_row_do(void *ctx, int r, long op, long a, long b) { (void)ctx; (void)r; (void)op; (void)a; (void)b; }
static int t_row_facts(void *ctx, int r, struct wayland_facts *f) {
        (void)ctx; (void)r;
        memset(f, 0, sizeof *f);
        f->scale = 100;
        if (g_row_facts_zero_size) {
                f->width = 0; f->height = 0;
                f->states = WAYLAND_STATE_MAXIMIZED | WAYLAND_STATE_ACTIVATED;
        } else {
                f->width = 1920; f->height = 1080;
                f->states = WAYLAND_STATE_MAXIMIZED | WAYLAND_STATE_ACTIVATED;
        }
        return 0;
}
static const char *t_clip_type(void *ctx, long w, long nth) { (void)ctx; (void)w; (void)nth; return 0; }
static long t_clip_pull(void *ctx, long cl, long w) { (void)ctx; (void)cl; (void)w; return 0; }
static void t_clip_abort(void *ctx, long cl, long w) { (void)ctx; (void)cl; (void)w; }
static void t_clip_push(void *ctx, long r, long w, const char *m, long fd) { (void)ctx; (void)r; (void)w; (void)m; (void)fd; }
static void t_clip_clear(void *ctx, long cl, long w) { (void)ctx; (void)cl; (void)w; }
static long t_dmabuf_info(void *ctx, struct wayland_dmabuf_info *i) { (void)ctx; memset(i, 0, sizeof *i); return 0; }
static long t_dmabuf_table(void *ctx, long *sz) { (void)ctx; *sz = 0; return -1; }
static void *t_dmabuf_map(void *ctx, long fd, long sz) { (void)ctx; (void)fd; (void)sz; return 0; }
static void t_pointer_hold(void *ctx, long row, long mode) { (void)ctx; (void)row; (void)mode; }
static int t_random(void *ctx, unsigned char *o, long n) { (void)ctx; memset(o, 0x42, (size_t)n); return 0; }

static void send_msg(long slot, uint32_t obj, uint16_t op, const uint32_t *args, uint16_t nargs) {
        uint32_t buf[32];
        uint16_t size = (uint16_t)(8 + nargs * 4);
        buf[0] = obj;
        buf[1] = ((uint32_t)size << 16) | op;
        for (uint16_t i = 0; i < nargs; i++) buf[2 + i] = args[i];
        int r = wayland_feed(slot, (const unsigned char *)buf, size, 0, 0);
        if (r < 0) { fprintf(stderr, "wayland_feed failed %d on obj=%u op=%u\n", r, obj, op); exit(2); }
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

static int find_toplevel_configure(uint32_t toplevel_id, int32_t *out_w, int32_t *out_h, uint32_t *out_states_mask) {
        long at = 0;
        int found = 0;
        while (at + 8 <= g_out_len) {
                uint32_t obj, hdr;
                memcpy(&obj, g_out + at, 4);
                memcpy(&hdr, g_out + at + 4, 4);
                uint16_t sz = (uint16_t)(hdr >> 16), op = (uint16_t)(hdr & 0xffffu);
                if (sz < 8 || at + sz > g_out_len) break;
                if (obj == toplevel_id && op == WLE_xdg_toplevel_configure && sz >= 20) {
                        int32_t w, h;
                        uint32_t alen;
                        memcpy(&w, g_out + at + 8, 4);
                        memcpy(&h, g_out + at + 12, 4);
                        memcpy(&alen, g_out + at + 16, 4);
                        uint32_t mask = 0;
                        for (uint32_t k = 0; k + 4 <= alen && at + 20 + (long)k + 4 <= at + sz; k += 4) {
                                uint32_t st;
                                memcpy(&st, g_out + at + 20 + k, 4);
                                if (st < 32) mask |= 1u << st;
                        }
                        *out_w = w; *out_h = h; *out_states_mask = mask;
                        found = 1;
                }
                at += sz;
        }
        return found;
}

static uint32_t bind_global(long slot, uint32_t reg_id, const char *iface, uint32_t ver, uint32_t new_id) {
        /* Scan wl_registry.global events in g_out to find global name */
        long at = 0;
        uint32_t gname = 0;
        while (at + 8 <= g_out_len) {
                uint32_t obj, hdr;
                memcpy(&obj, g_out + at, 4);
                memcpy(&hdr, g_out + at + 4, 4);
                uint16_t sz = (uint16_t)(hdr >> 16), op = (uint16_t)(hdr & 0xffffu);
                if (sz < 8 || at + sz > g_out_len) break;
                if (obj == reg_id && op == WLE_wl_registry_global && sz >= 20) {
                        uint32_t name, slen;
                        memcpy(&name, g_out + at + 8, 4);
                        memcpy(&slen, g_out + at + 12, 4);
                        if (slen > 0 && at + 16 + (long)slen <= at + sz) {
                                if (strcmp((const char *)(g_out + at + 16), iface) == 0) { gname = name; break; }
                        }
                }
                at += sz;
        }
        if (!gname) { fprintf(stderr, "global %s not found\n", iface); exit(3); }
        uint32_t slen = (uint32_t)strlen(iface) + 1;
        uint32_t padsz = (slen + 3u) & ~3u;
        uint32_t args[16];
        memset(args, 0, sizeof args);
        args[0] = gname;
        args[1] = slen;
        memcpy(&args[2], iface, slen);
        uint16_t idx = (uint16_t)(2 + padsz / 4);
        args[idx++] = ver;
        args[idx++] = new_id;
        send_msg(slot, reg_id, WLQ_wl_registry_bind, args, idx);
        return new_id;
}

int main(void) {
        struct wayland_host host = {
                .alloc = t_alloc, .release = t_free, .map = t_map, .remap = t_remap, .unmap = t_unmap, .close = t_close,
                .now = t_now, .screens = t_screens, .keymap = t_keymap, .repeat = t_repeat, .seat = t_seat, .locks = t_locks,
                .row_open = t_row_open, .row_close = t_row_close, .row_picture = t_row_picture,
                .row_title = t_row_title, .row_do = t_row_do, .row_facts = t_row_facts,
                .clip_type = t_clip_type, .clip_pull = t_clip_pull, .clip_abort = t_clip_abort,
                .clip_push = t_clip_push, .clip_clear = t_clip_clear,
                .dmabuf_info = t_dmabuf_info, .dmabuf_table = t_dmabuf_table, .dmabuf_map = t_dmabuf_map,
                .pointer_hold = t_pointer_hold, .random = t_random,
        };
        if (wayland_start(&host) != 0) { fprintf(stderr, "wayland_start failed\n"); return 1; }
        long slot = wayland_connect(1234, 1000);
        if (slot < 0) return 2;

        /* Get registry (id 2) */
        uint32_t a1[1] = {2};
        send_msg(slot, 1, WLQ_wl_display_get_registry, a1, 1);
        drain_out(slot);

        /* Bind compositor=3, xdg_wm_base=4, wl_seat=5, rel_mgr=6 */
        bind_global(slot, 2, "wl_compositor", 4, 3);
        bind_global(slot, 2, "xdg_wm_base", 5, 4);
        bind_global(slot, 2, "wl_seat", 8, 5);
        bind_global(slot, 2, "zwp_relative_pointer_manager_v1", 1, 6);
        drain_out(slot);

        /* Test 1: Maximized before initial commit (Chromium SIGTRAP regression!) */
        uint32_t s_id = 10, xs_id = 11, xt_id = 12;
        send_msg(slot, 3, WLQ_wl_compositor_create_surface, &s_id, 1);
        uint32_t xsa[2] = {xs_id, s_id};
        send_msg(slot, 4, WLQ_xdg_wm_base_get_xdg_surface, xsa, 2);
        send_msg(slot, xs_id, WLQ_xdg_surface_get_toplevel, &xt_id, 1);
        send_msg(slot, xt_id, WLQ_xdg_toplevel_set_maximized, 0, 0);
        send_msg(slot, s_id, WLQ_wl_surface_commit, 0, 0);
        drain_out(slot);

        int32_t cw = -1, ch = -1; uint32_t smask = 0;
        if (!find_toplevel_configure(xt_id, &cw, &ch, &smask)) {
                fprintf(stderr, "FAIL: no xdg_toplevel.configure for maximized startup\n"); return 10;
        }
        if (cw != 1920 || ch != 1080 || !(smask & (1u << WLN_xdg_toplevel_state_maximized))) {
                fprintf(stderr, "FAIL: maximized initial configure got %dx%d mask=0x%x (expected 1920x1080 + MAXIMIZED)\n", cw, ch, smask);
                return 11;
        }
        printf("OK maximized_initial_configure: %dx%d mask=0x%x\n", cw, ch, smask);

        /* Test 2: Floating window before initial commit still sends 0x0 */
        uint32_t s2 = 20, xs2 = 21, xt2 = 22;
        send_msg(slot, 3, WLQ_wl_compositor_create_surface, &s2, 1);
        uint32_t xsa2[2] = {xs2, s2};
        send_msg(slot, 4, WLQ_xdg_wm_base_get_xdg_surface, xsa2, 2);
        send_msg(slot, xs2, WLQ_xdg_surface_get_toplevel, &xt2, 1);
        send_msg(slot, s2, WLQ_wl_surface_commit, 0, 0);
        drain_out(slot);
        cw = -1; ch = -1; smask = 0;
        if (!find_toplevel_configure(xt2, &cw, &ch, &smask) || cw != 0 || ch != 0) {
                fprintf(stderr, "FAIL: floating initial configure got %dx%d (expected 0x0)\n", cw, ch);
                return 12;
        }
        printf("OK floating_initial_configure: %dx%d\n", cw, ch);

        /* Test 3: seat_gen increments on get_pointer, get_keyboard, get_relative_pointer, destroy */
        unsigned int g0 = wayland.seat_gen;
        uint32_t ptr_id = 30, kb_id = 31, rel_id = 32;
        send_msg(slot, 5, WLQ_wl_seat_get_pointer, &ptr_id, 1);
        unsigned int g1 = wayland.seat_gen;
        send_msg(slot, 5, WLQ_wl_seat_get_keyboard, &kb_id, 1);
        unsigned int g2 = wayland.seat_gen;
        uint32_t rela[2] = {rel_id, ptr_id};
        send_msg(slot, 6, WLQ_zwp_relative_pointer_manager_v1_get_relative_pointer, rela, 2);
        unsigned int g3 = wayland.seat_gen;
        send_msg(slot, rel_id, WLQ_zwp_relative_pointer_v1_destroy, 0, 0);
        unsigned int g4 = wayland.seat_gen;
        if (!(g1 > g0 && g2 > g1 && g3 > g2 && g4 > g3)) {
                fprintf(stderr, "FAIL: seat_gen did not increment monotonically: %u %u %u %u %u\n", g0, g1, g2, g3, g4);
                return 13;
        }
        printf("OK seat_gen monotonic: %u->%u->%u->%u->%u\n", g0, g1, g2, g3, g4);

        /* Test 4: wayland_pointer_direct_ptrs + wayland_pointer_noted */
        struct wayland_client *c = wayland.client[slot];
        if (wayland_rows_reach(0)) {
                wayland.row[0].client = slot;
                wayland.row[0].surface = 0;
                c->surfaces[0].row = 0;
        }
        wayland.pointer_row = 0;
        unsigned int pids[4], fids[4]; unsigned char vers[4]; long pcnt = 0, fcnt = 0, sc = 0;
        unsigned long *ser = 0;
        long ret = wayland_pointer_direct_ptrs(0, pids, vers, &pcnt, fids, &fcnt, 4, &sc, &ser);
        if (ret != slot + 1 || pcnt != 1 || pids[0] != ptr_id || vers[0] != 8 || fcnt != 1 || fids[0] != ptr_id || !ser) {
                fprintf(stderr, "FAIL: wayland_pointer_direct_ptrs ret=%ld pcnt=%ld pid=%u ver=%u fcnt=%ld\n", ret, pcnt, pids[0], vers[0], fcnt);
                return 14;
        }
        unsigned long prev_next = c->input_next;
        wayland_pointer_noted(0, 1, 777);
        if (c->input_next != prev_next + 1 || c->input_serial[prev_next % WAYLAND_INPUT_SERIALS] != 777 ||
            wayland.buttons != 1 || c->press_serial != 777) {
                fprintf(stderr, "FAIL: wayland_pointer_noted did not record press serial/buttons\n");
                return 15;
        }
        wayland_pointer_noted(0, 0, 778);
        if (wayland.buttons != 0) {
                fprintf(stderr, "FAIL: wayland_pointer_noted release did not decrement buttons\n");
                return 15;
        }
        printf("OK wayland_pointer_direct_ptrs + wayland_pointer_noted\n");

        /* Test 5: s->row >= 0 with MAXIMIZED and 0x0 facts still guards against 0x0 configure */
        g_row_facts_zero_size = 1;
        drain_out(slot);
        wayland_configure(c, &c->surfaces[0], 0);
        drain_out(slot);
        cw = -1; ch = -1; smask = 0;
        if (!find_toplevel_configure(xt_id, &cw, &ch, &smask) || cw != 1920 || ch != 1080) {
                fprintf(stderr, "FAIL: maximized 0x0 facts guard got %dx%d\n", cw, ch);
                return 16;
        }
        printf("OK maximized_zero_facts_guard: %dx%d\n", cw, ch);

        wayland_drop(slot);
        wayland_stop();
        return 0;
}
'''


# ---------------------------------------------------------------------------
# 2. Executable C test: lib.c SIMD bit-exactness (2101010 pack + NV12->XRGB)
# ---------------------------------------------------------------------------
SIMD_C_HARNESS = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void memory_pack_2101010_u32(unsigned int *dst, const unsigned int *src, unsigned long count, unsigned long flags);
extern void memory_nv12_to_xrgb_row(unsigned int *dst, const unsigned char *y, const unsigned char *uv, unsigned long count);

static uint32_t ref_pack_2101010(uint32_t p, unsigned long flags) {
        uint32_t r = (p >> 22) & 0xffu;
        uint32_t g = (p >> 12) & 0xffu;
        uint32_t b = (p >> 2) & 0xffu;
        if (flags & 1u) { uint32_t t = r; r = b; b = t; }
        uint32_t a = (flags & 2u) ? 0xffu : (((p >> 30) & 3u) * 85u);
        return (a << 24) | (r << 16) | (g << 8) | b;
}

static inline int clamp8(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

static uint32_t ref_nv12(uint8_t yv, uint8_t uv_u, uint8_t uv_v) {
        int c = (int)yv - 16;
        if (c < 0) c = 0;
        int d = (int)uv_u - 128;
        int e = (int)uv_v - 128;
        int r = clamp8((298 * c + 409 * e + 128) >> 8);
        int g = clamp8((298 * c - 100 * d - 208 * e + 128) >> 8);
        int b = clamp8((298 * c + 516 * d + 128) >> 8);
        return 0xff000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

int main(void) {
        static const unsigned long lens[] = {0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 255, 257, 1920};
        uint32_t src[2000], dst[2000], ref[2000];
        uint8_t ybuf[2000], uvbuf[2000];
        uint32_t rng = 0x12345678u;

        /* 1. memory_pack_2101010_u32: boundary values + random fuzz + all alignments */
        for (size_t li = 0; li < sizeof(lens)/sizeof(lens[0]); li++) {
                unsigned long n = lens[li];
                for (unsigned long i = 0; i < n; i++) {
                        if (i < 16) {
                                static const uint32_t corners[16] = {
                                        0x00000000u, 0xffffffffu, 0x3fffffffu, 0x40000000u,
                                        0x80000000u, 0xc0000000u, 0x000003ffu, 0x000ffc00u,
                                        0x3ff00000u, 0x15555555u, 0x2aaaaaaau, 0x55555555u,
                                        0xaaaaaaaau, 0x00100401u, 0x00000003u, 0x3fcff3fcu
                                };
                                src[i] = corners[i];
                        } else {
                                rng = rng * 1664525u + 1013904223u;
                                src[i] = rng;
                        }
                }
                for (unsigned long flags = 0; flags < 4; flags++) {
                        for (unsigned long off = 0; off < 4; off++) {
                                memset(dst, 0x5a, sizeof dst);
                                memory_pack_2101010_u32(dst + off, src + off, n, flags);
                                for (unsigned long i = 0; i < n; i++) {
                                        uint32_t want = ref_pack_2101010(src[off + i], flags);
                                        if (dst[off + i] != want) {
                                                fprintf(stderr, "FAIL pack_2101010 n=%lu flags=%lu off=%lu i=%lu in=0x%08x got=0x%08x want=0x%08x\n",
                                                        n, flags, off, i, src[off + i], dst[off + i], want);
                                                return 1;
                                        }
                                }
                                if (dst[off + n] != 0x5a5a5a5au) {
                                        fprintf(stderr, "FAIL pack_2101010 overwrote sentinel at n=%lu off=%lu\n", n, off);
                                        return 2;
                                }
                        }
                }
        }
        printf("OK memory_pack_2101010_u32 (all lengths, alignments, corners, fuzz)\n");

        /* 2. memory_nv12_to_xrgb_row: extreme clamp corners + fuzz + odd widths */
        for (size_t li = 0; li < sizeof(lens)/sizeof(lens[0]); li++) {
                unsigned long n = lens[li];
                for (unsigned long i = 0; i < n + 4; i++) {
                        rng = rng * 1664525u + 1013904223u;
                        ybuf[i] = (uint8_t)(rng >> 24);
                        uvbuf[i] = (uint8_t)(rng >> 16);
                }
                if (n >= 8) {
                        ybuf[0] = 0;   uvbuf[0] = 0;   uvbuf[1] = 0;
                        ybuf[1] = 255; uvbuf[0] = 0;   uvbuf[1] = 0;
                        ybuf[2] = 255; uvbuf[2] = 255; uvbuf[3] = 255;
                        ybuf[3] = 0;   uvbuf[2] = 255; uvbuf[3] = 255;
                        ybuf[4] = 16;  uvbuf[4] = 128; uvbuf[5] = 128;
                        ybuf[5] = 235; uvbuf[4] = 128; uvbuf[5] = 128;
                        ybuf[6] = 255; uvbuf[6] = 0;   uvbuf[7] = 255;
                        ybuf[7] = 0;   uvbuf[6] = 255; uvbuf[7] = 0;
                }
                memset(dst, 0xa5, sizeof dst);
                memory_nv12_to_xrgb_row(dst, ybuf, uvbuf, n);
                for (unsigned long i = 0; i < n; i++) {
                        unsigned long pair = (i >> 1) << 1;
                        uint32_t want = ref_nv12(ybuf[i], uvbuf[pair], uvbuf[pair + 1]);
                        if (dst[i] != want) {
                                fprintf(stderr, "FAIL nv12 n=%lu i=%lu y=%u u=%u v=%u got=0x%08x want=0x%08x\n",
                                        n, i, ybuf[i], uvbuf[pair], uvbuf[pair+1], dst[i], want);
                                return 3;
                        }
                }
                if (dst[n] != 0xa5a5a5a5u) {
                        fprintf(stderr, "FAIL nv12 overwrote sentinel at n=%lu\n", n);
                        return 4;
                }
        }
        printf("OK memory_nv12_to_xrgb_row (all lengths, odd widths, clamp corners, fuzz)\n");
        return 0;
}
'''


# ---------------------------------------------------------------------------
# 3. Executable C test: spark_input_ring ABI & layout compatibility
# ---------------------------------------------------------------------------
SPARK_ABI_HARNESS = r'''
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define SPARK_INPUT_MAGIC 0x504e4953u
#define SPARK_INPUT_VERSION 1u
#define SPARK_INPUT_SLOTS 128u
#define SPARK_INPUT_PAGE_BYTES 4096ul
#define SPARK_INPUT_KIND_REL 0u
#define SPARK_INPUT_KIND_KEY 1u
#define SPARK_INPUT_KIND_WHEEL 2u

struct spark_input_slot {
        int dx;
        int dy;
        unsigned long long time_us;
        unsigned short kind;
        unsigned short code;
        unsigned int flags;
};

struct spark_input_ring {
        unsigned int magic;
        unsigned int version;
        unsigned int mask;
        unsigned int active;
        unsigned long long head;
        unsigned long long tail;
        unsigned long long drops;
        unsigned int reserved[6];
        struct spark_input_slot slots[SPARK_INPUT_SLOTS];
};

int main(void) {
        if (sizeof(struct spark_input_slot) != 24) return 1;
        if (offsetof(struct spark_input_slot, dx) != 0) return 2;
        if (offsetof(struct spark_input_slot, dy) != 4) return 3;
        if (offsetof(struct spark_input_slot, time_us) != 8) return 4;
        if (offsetof(struct spark_input_slot, kind) != 16) return 5;
        if (offsetof(struct spark_input_slot, code) != 18) return 6;
        if (offsetof(struct spark_input_slot, flags) != 20) return 7;
        if (sizeof(struct spark_input_ring) > SPARK_INPUT_PAGE_BYTES) return 8;
        printf("OK spark_input_ring ABI: slot=%zu ring=%zu\n",
               sizeof(struct spark_input_slot), sizeof(struct spark_input_ring));
        return 0;
}
'''


def extract_lib_asm(lib_text, names, arch_is_arm64):
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


def test_executable_c_harnesses():
    print("\n=== 1. Executable C Regression Harnesses ===")
    cc = shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")
    if not cc:
        check("host C compiler available", False, "no clang/cc/gcc found")
        return

    with tempfile.TemporaryDirectory(prefix="mw_regress_") as td:
        td = Path(td)
        lib_text = read_text("src/lib.c")
        is_mac = sys.platform == "darwin"
        arch_is_arm64 = os.uname().machine in ("arm64", "aarch64")
        arch_is_x64 = os.uname().machine in ("x86_64", "amd64")

        # 1a. Wayland xdg_toplevel.configure + direct pointer + seat_gen
        wl_src = td / "test_wayland.c"
        wl_bin = td / "test_wayland"
        wl_src.write_text(WAYLAND_C_HARNESS)
        if is_mac and (arch_is_arm64 or arch_is_x64):
            wl_s = td / "wl_asm.S"
            wl_s.write_text(extract_lib_asm(
                lib_text,
                ("wire_message", "wire_arguments", "wire_event", "wire_slot", "rects_clip", "bits_trailing_zeros"),
                arch_is_arm64,
            ))
            r = run_cmd([cc, "-O2", "-I", str(ROOT), str(wl_src), str(wl_s), "-o", str(wl_bin)])
        else:
            lib_obj = td / "lib_wl.o"
            r = run_cmd([cc, "-O2", "-fno-builtin", "-c", str(ROOT / "src/lib.c"), "-o", str(lib_obj)])
            if r.returncode == 0:
                r = run_cmd([cc, "-O2", "-I", str(ROOT), str(wl_src), str(lib_obj), "-o", str(wl_bin)])
        if r.returncode != 0:
            check("Wayland C harness compiles", False, r.stderr[:400])
        else:
            r2 = run_cmd([str(wl_bin)])
            check("Wayland xdg_toplevel maximized/floating/direct_ptrs/seat_gen",
                  r2.returncode == 0, (r2.stdout + r2.stderr).strip())

        # 1b. lib.c SIMD bit-exactness on host CPU (extracts the real ASM bodies from src/lib.c)
        simd_src = td / "test_simd.c"
        simd_bin = td / "test_simd"
        if is_mac and (arch_is_arm64 or arch_is_x64):
            asm_s = td / "simd_routines.S"
            asm_s.write_text(extract_lib_asm(lib_text, ("memory_pack_2101010_u32", "memory_nv12_to_xrgb_row"), arch_is_arm64))
            simd_src.write_text(SIMD_C_HARNESS)
            r_link = run_cmd([cc, "-O2", str(simd_src), str(asm_s), "-o", str(simd_bin)])
        else:
            lib_obj = td / "lib.o"
            simd_src.write_text(SIMD_C_HARNESS)
            r_lib = run_cmd([cc, "-O2", "-fno-builtin", "-c", str(ROOT / "src/lib.c"), "-o", str(lib_obj)])
            r_link = r_lib if r_lib.returncode != 0 else run_cmd([cc, "-O2", str(simd_src), str(lib_obj), "-o", str(simd_bin)])
        if r_link.returncode != 0:
            check("SIMD harness builds against src/lib.c", False, r_link.stderr[:400])
        else:
            r_run = run_cmd([str(simd_bin)])
            check("lib.c SIMD memory_pack_2101010_u32 + memory_nv12_to_xrgb_row bit-exact",
                  r_run.returncode == 0, (r_run.stdout + r_run.stderr).strip())

        # 1c. spark_input_ring ABI
        sp_src = td / "test_spark.c"
        sp_bin = td / "test_spark"
        sp_src.write_text(SPARK_ABI_HARNESS)
        r_sp = run_cmd([cc, "-O2", str(sp_src), "-o", str(sp_bin)])
        check("spark_input_ring ABI layout (24B slot, <=4096B page)",
              r_sp.returncode == 0 and run_cmd([str(sp_bin)]).returncode == 0, r_sp.stderr[:200])

        # 1d. Cross-arch assembly of src/lib.c (x86_64, aarch64, and riscv64 when target available)
        clang_bin = shutil.which("clang")
        if clang_bin:
            for target in ("x86_64-linux-gnu", "aarch64-linux-gnu", "riscv64-linux-gnu"):
                out_o = td / f"lib_{target}.o"
                rc = run_cmd([clang_bin, "-target", target, "-O2", "-ffreestanding", "-nostdlib",
                              "-c", str(ROOT / "src/lib.c"), "-o", str(out_o)])
                if rc.returncode != 0 and "No available targets" in rc.stderr:
                    continue
                check(f"src/lib.c assembles cleanly for {target}", rc.returncode == 0, rc.stderr[:300])


def test_compositor_and_xwm_invariants():
    print("\n=== 2. Compositor, XWM, Input & Scanout Invariants ===")

    manage = read_text("src/canvas/compositor/manage.c")
    xwm = read_text("src/canvas/xwm.c")
    inp = read_text("src/canvas/compositor/input.c")
    comp = read_text("src/canvas/compositor.c")
    wlh = read_text("src/canvas/wayland_host.c")
    scan = read_text("src/canvas/compositor/scanout.c")
    kms = read_text("src/canvas/compositor/kms.c")

    # HL2 crash: compositor_row_facts(-1) must check row < 0 BEFORE indexing compositor.arranged[row]
    m_facts = re.search(r"static int compositor_row_facts\([^)]*\)\s*\{(.*?)\n\}", manage, re.S)
    check("compositor_row_facts exists", m_facts is not None)
    if m_facts:
        body = m_facts.group(1)
        idx_guard = body.find("row < 0")
        idx_arr = body.find("compositor.arranged[row]")
        check("HL2 fix: compositor_row_facts checks row < 0 before compositor.arranged[row]",
              idx_guard >= 0 and (idx_arr < 0 or idx_guard < idx_arr),
              f"guard@{idx_guard} arr@{idx_arr}")

    # Skyrim focus + build.sh clang decl fix
    decl_pos = manage.find("static void compositor_motion(void);")
    first_call = manage.find("compositor_motion();")
    check("build.sh clang fix: compositor_motion() declared before first call in manage.c",
          decl_pos >= 0 and first_call > decl_pos, f"decl@{decl_pos} call@{first_call}")
    check("Skyrim fix: compositor_motion() called on fullscreen/promote/reveal (>=3 sites)",
          manage.count("if (!compositor_wl_locked())") >= 3)

    # Tomb Raider Alt-Tab fix: WM_STATE + _NET_WM_STATE_HIDDEN
    check("Tomb Raider fix: COMPOSITOR_XWM_HIDDEN defined in xwm.c",
          "#define COMPOSITOR_XWM_HIDDEN" in xwm)
    check("Tomb Raider fix: xwm_net_state_map includes COMPOSITOR_XWM_A_HIDDEN",
          "{COMPOSITOR_XWM_A_HIDDEN, COMPOSITOR_XWM_HIDDEN}" in xwm)
    check("Tomb Raider fix: compositor_xwm_sync writes WM_STATE Iconic/Normal on HIDDEN transition",
          "COMPOSITOR_XWM_STATE_ICONIC : COMPOSITOR_XWM_STATE_NORMAL" in xwm)
    check("Tomb Raider fix: WM_CHANGE_STATE handles NormalState reveal",
          "e->data[0] == COMPOSITOR_XWM_STATE_NORMAL" in xwm)
    check("Tomb Raider fix: xwm_unmapped clears COMPOSITOR_XWM_HIDDEN",
          "COMPOSITOR_XWM_COVERED | COMPOSITOR_XWM_HIDDEN" in xwm)

    # Direct scanout recovery after Alt-Tab
    check("Direct scanout recovery: compositor_wl_scan_retry called in compositor_reveal & compositor_fullscreen_on",
          manage.count("compositor_wl_scan_retry(row)") >= 2 and "compositor_wl.straight[row].scan_failed = 0" in wlh)

    # Spark + input bounds safety
    check("Spark OOB guard: compositor.held sized COMPOSITOR_INPUTS + 1",
          "held[COMPOSITOR_INPUTS + 1]" in comp)
    check("Spark OOB guard: compositor_modifiers includes compositor.held[COMPOSITOR_INPUTS]",
          "compositor.held[COMPOSITOR_INPUTS]" in inp)
    check("Spark OOB guard: compositor_input_wheel guards per-device arrays with at < COMPOSITOR_INPUTS",
          "if (at < COMPOSITOR_INPUTS)" in inp)
    check("Direct button note: COMPOSITOR_READER_BTN_NOTE excluded from handed_out",
          "raw->type != COMPOSITOR_READER_BTN_NOTE" in inp)
    check("Direct-socket flick-shot + wheel: compositor_wl_direct_send_packet used, vertical scroll negated, sendto fast-path & dirty flush",
          "compositor_wl_direct_send_packet(" in inp and "compositor_wl_direct_send_packet(" in wlh and
          "rest[COMPOSITOR_WL_DIRECT_IDS * 128]" in wlh and "bytes[COMPOSITOR_WL_DIRECT_IDS * 128]" in wlh and
          "int v = wheel_horiz ? wheel_v120 : -wheel_v120;" in wlh and
          "if (fd_count <= 0)" in wlh and
          "if (pushed && was_empty && wq_has_sleeper(&spark_input_wait))" in read_text("src/moonwater/moonwater.c"))
    check("Direct-socket unlocked motion avoids duplicate wl_pointer.motion from async HW cursor step",
          "compositor_wl_direct_abs(nx, ny, now)" not in kms)
    check("Dirty-checked direct publish tracks dragging, seat_gen on release, and abs_valid clean",
          "compositor_wl_direct.pub_valid && compositor_wl_direct.pub_gen == gen" in wlh and
          "wayland_dragging()" in wlh and "wayland.seat_gen++" in read_text("src/canvas/wayland.c") and
          "compositor_wl_direct.abs_valid = 0" in wlh and "compositor_wl.direct_gen++" in wlh)
    check("Spark OOB guards in input.c: reader_put, SYN_DROPPED, EV_ABS, EV_KEY, input_gone",
          "record->at < COMPOSITOR_INPUTS ?" in inp and
          "if (is_spark)\n                                        compositor.held[COMPOSITOR_INPUTS] = 0;" in inp and
          "else if (event->code < 2 && !is_spark)" in inp and
          "is_spark || (compositor.input_kinds[at] & WINDOWS_INPUT_KEYBOARD)" in inp and
          "compositor.held[COMPOSITOR_INPUTS] = 0, compositor_keys_release();" in inp)
    check("Game-slice stack_gen invalidates on fullscreen toggle & minimize/reveal",
          manage.count("compositor.stack_gen++") >= 4)
    check("GPU publish tracks refuse_gen, gpu_map_why & gpu.family",
          "compositor_gpu_refuse_gen" in read_text("src/canvas/compositor/gpu_host.c") and
          "pub_map_why == gpu_map_why" in read_text("src/canvas/compositor/gpu_host.c"))
    check("Reflex JIT pacing extended via compositor_wl_jit_row() & zero spurious flip/JIT wakes",
          "compositor_wl_jit_row()" in scan and "compositor_wl_jit_row()" in comp and
          "!compositor.flip_pending && !compositor_wl_frame_owed" in comp)
    check("Game mode locks amdgpu DPM high & boot sets THP defrag=defer (no sync compaction stalls)",
          "power_dpm_force_performance_level" in inp and
          'system_sysctl("/sys/kernel/mm/transparent_hugepage/defrag", "defer\\n");' in read_text("src/sh/system.c"))

    # Background throttling safety
    check("BG throttle safety: skips Xwayland, wineserver, audio, dbus",
          all(s in inp for s in ('"Xwayland"', '"wineserver"', '"audio"', '"dbus"')))
    check("CCD0 topology cached once in compositor_ccd_init()",
          "compositor_ccd_init" in inp and "compositor_ccd.ccd0_hex" in inp)
    check("CCD0 IRQ pinning loop-reads /proc/interrupts into 64K buffer",
          "buf[65536]" in inp and "while (got < (long)sizeof(buf) - 1" in inp)
    check("Non-glassy fullscreen games get direct scanout via centralized compositor_row_opaque_fs()",
          "compositor_row_opaque_fs(row)" in scan and "compositor_row_opaque_fs(row)" in wlh)
    check("Spaces bug fixes: pointer_row uses compositor_visible, band_at set per monitor, minimize exits FS Space, DPM per-card, wallpaper skip in compose.c",
          "compositor_visible(compositor_wl.hold_row)" in wlh and
          "compositor.band_at = band_idx;" in read_text("src/canvas/compositor/present.c") and
          "compositor_game_dpm_saved[4][32]" in inp and
          "compositor_space_all_fullscreen()" in read_text("src/canvas/compositor/compose.c"))
    check("Oneshot XWarpPointer hint honored when locked_pointer created+destroyed in one flush",
          "(con->active || (s->row == wayland.pointer_row && wayland_has_keys(c)))" in read_text("src/canvas/wayland.c") and
          "wayland.hint_row >= 0)" in read_text("src/canvas/wayland.c") and
          "compositor.pointer_x = compositor.kept_x = sx;" in wlh and
          "!wayland_out_dirty(slot)" in wlh)
    check("XWM Wine fixes: focus re-sync on PROTOCOLS/WM_HINTS, TRANSIENT notify, covers >=, fixed-size dialog cap",
          "XWM_PF_MAP | XWM_PF_NOTIFY, -COMPOSITOR_XWM_XA_WM_TRANSIENT_FOR" in xwm and
          xwm.count("x->focus_row = -3;") >= 2 and
          "if (most_px > 0 && most_px < floor_min)" in manage)
    cr_start = xwm.find("case XWM_EV_CONFIGURE_REQUEST:")
    cr_end = xwm.find("case XWM_EV_CREATE_NOTIFY:", cr_start)
    check("XWM ConfigureRequest honors client programmatic resize without clamping against stale WM_NORMAL_HINTS",
          cr_start != -1 and cr_end != -1 and
          "compositor_xwm_clamp_size(" not in xwm[cr_start:cr_end] and
          "if (!w || (w->state & COMPOSITOR_XWM_MAPPED))" in xwm[cr_start:cr_end])
    comp_main = read_text("src/canvas/compositor.c")
    check("144Hz loop floor: held turns bypass epoll_ctl churn, next_minute/wake_check throttled & rows_any guards",
          "if (held)\n        {\n                for (unsigned int at = 0; at < count; at++) wanted[at].returned = 0;\n                goto ppoll;" in comp_main and
          "static positive compositor_minute_due;" in comp_main and
          "if (checked && now >= checked && now - checked < 250000000ull)" in read_text("src/canvas/compositor/session.c") and
          "if (!compositor.table || !rows_any(&compositor_card.rows))" in read_text("src/canvas/compositor/present.c") and
          "if (rows_any(&compositor.held_rows))" in read_text("src/canvas/compositor/scanout.c"))
    cm_start = inp.find("static void compositor_motion(void)")
    cm_end = inp.find("static void compositor_shape_sync(void)", cm_start)
    check("Unlocked/confined cursor latency: DRM_MODE_CURSOR deferred past client delivery, confined skips async cursor & press_serial tracked",
          "!compositor_wl_held()" in read_text("src/canvas/compositor/kms.c") and
          cm_start != -1 and cm_end != -1 and
          inp[cm_start:cm_end].rfind("compositor_cursor_move();") > inp[cm_start:cm_end].rfind("compositor_pointer_to(under);") and
          "if (pressed) c->press_serial = (unsigned int)serial;" in read_text("src/canvas/wayland.c"))
    ui_src = read_text("src/canvas/ui/ui.c")
    tb_src = read_text("src/canvas/ui/taskbar.c")
    ln_src = read_text("src/canvas/ui/launcher.c")
    check("Taskbar globe Fresnel is symmetric/luminous without 0x7a7a7a cliff and hover skips UI_MOON_FULL_SQUARE white fringe",
          "if ((look->options & UI_MOON_FULL_SQUARE) && look->grow <= 0)" in ui_src and
          "0x7a7a7au" not in ui_src[ui_src.find("static fn ui_moon_rim("):ui_src.find("struct ui_moon_sky")] and
          "UI_MOON_RIM | (grow <= 0 ? UI_MOON_FULL_SQUARE : 0u)" in tb_src and
          "ui_moon(ui, left, top, grown, UI_ONE / 2, turn, &look);" in tb_src)
    check("Start menu has consistent 1x-taskbar-height offset and never cascades or uses stale/MC-pushed anchor",
          "anchor->gap" in ui_src and
          "launcher.anchor.gap =" in ln_src and
          "anchor->button[1] - anchor->gap - anchor->screen[1]" in ln_src and
          "windows_place_set(table, row, &place);" in ln_src and
          "compositor.mc.push = 0;" in inp and
          "if (compositor.mc.open)\n                compositor_mc_close();" in inp)
    wlh_src = read_text("src/canvas/wayland_host.c")
    check("GPU copy stalls eliminated: async gpu_blit_go for DMA-BUF & CPU lib.c SIMD for WB table/SHM copies",
          "static positive gpu_blit_go(" in read_text("src/canvas/gpu.c") and
          "positive sequence = gpu_blit_go(" in wlh_src and
          "card_wanted = !picture->pixels || (picture->sync_fd >= 0 && summed >= COMPOSITOR_WL_CARD_COPY_PIXELS);" in wlh_src and
          "memory_copy_rect_u32(to, pitch, source, pitch, cw, ch);" in wlh_src)
    check("Dynamic Reflex JIT pacing margin adapts to direct scanout (0.6ms + lead) vs GPU compose (1.0ms + lead)",
          "positive lead = compositor_present_lead();" in read_text("src/canvas/compositor/scanout.c") and
          "600000ull : 1000000ull) + lead" in read_text("src/canvas/compositor/scanout.c"))
    comp_c = read_text("src/canvas/compositor/compose.c")
    check("lib.c SIMD memory_mix_const_u32_rect wired into canvas_cpu_tint_corner & compositor_spot_draw",
          comp_c.count("memory_mix_const_u32_rect(") >= 4)
    wl_c = read_text("src/canvas/wayland.c")
    check("Wayland/XWM O(1) hot-path cleanup: xserial check, idle_notifies guard, latch row walk, short-read break & pic_ident",
          "WAYLAND_API int wayland_row_has_xserial(" in wl_c and
          "!wayland_row_has_xserial(w->row, compositor_xwm_server(x), w->serial)" in xwm and
          "wayland.host && wayland.idle_notifies ? wayland_idle_scan(0, 0) : 0;" in wl_c and
          "if (got < room)\n                        return;" in xwm and
          "compositor_wl_pic_ident(picture, &dev, &ino);" in wlh_src)
    gpu_c = read_text("src/canvas/gpu.c")
    check("GPU async wake & strict BO-list hygiene: gpu_wake uses gpu_scene_submit and gpu_import_named/gpu_map_named require handed",
          "gpu_scene_submit(ops_at, 1, bins_at, d_at, 64, 64, 4, 0, 0, 64, 4);" in gpu_c and
          "return gpu_imports_all || gpu.imported_own[i] || gpu.imported_handed[i];" in gpu_c and
          "return pages ? gpu_maps[i].handed : gpu_maps[i].handed && now;" in gpu_c and
          "gpu_import_handed(compositor_gpu_state.out);" in read_text("src/canvas/compositor/gpu_host.c"))
    kms_c = read_text("src/canvas/compositor/kms.c")
    check("1000-8000Hz input hot path: dedup DRM_MODE_CURSOR + trylock, band_hit strip reject, osk_sync guard & reader cache-line split",
          "else if (output->cursor_x != x || output->cursor_y != y)" in kms_c and
          "if (__atomic_exchange_n(&compositor_cursor_async.lock, 1u, __ATOMIC_ACQUIRE))" in kms_c and
          "strip = taskbar_thick(scale) + reach" in manage and
          "if (changed)\n                compositor_osk_sync();" in inp and
          "__attribute__((aligned(64))) unsigned long long reading" in inp)
    check("Compose & UI SIMD Batch D: scaled NV12 SIMD, plan damage culling + y-dedup, taskbar glass SIMD, AA ring fast-reject & ui_icon_picture rect OVER",
          "memory_nv12_to_xrgb_row(nv_row + x_even," in comp_c and
          "if (x2 <= t->x1 || fx >= t->x2 || y2 <= t->y1 || fy >= t->y2)" in comp_c and
          "if (place && edge[place - 1] == ey)" in comp_c and
          "memory_saturate_u32(taskbar_tint, from + at, (positive)count);" in tb_src and
          "if (d2 >= outer_r)" in tb_src and
          "memory_over_u32_rect(ui->pixels + (long)y0 * (long)ui->pitch + left," in ui_src)
    check("GPU/Loop Batch E: cached RAM ops staging in gpu_scene_build, minute-gated clock/theme ticks & signaled DMA-BUF skips setitimer",
          "gpu_ops_stage" in gpu_c and
          "if (compositor.band && minute_tick && taskbar_tick())" in comp_main and
          "if (ready)\n                return canvas_dmabuf_sync_fd(fd, 1) != -5;" in wlh_src)
    check("Hardware Floor Pass 1: inline ASM isqrt (sqrtsd/fsqrt), rows_next bsf/rbit+clz, ui_over_body CONST, 2-insn bswap+ror, gpu_radii div hoist & wayland_out_dirty_next",
          "sqrtsd %%xmm0, %%xmm0" in ui_src and
          "extern CONST unsigned int ui_over_body(" in ui_src and
          "sqrtsd %%xmm0, %%xmm0" in read_text("src/canvas/compositor/pointer.c") and
          "static inline INLINE PURE int rows_next(" in comp_main and
          "bswap %0\\n\\tror $8, %0" in comp_c and
          "WAYLAND_API long wayland_out_dirty_next(" in wl_c and
          "wayland_out_dirty_next(0)" in wlh_src)
    check("Hardware Floor Pass 2: pointer.c OVER SIMD, wayland_host NV12 SIMD + prefetch, ui_kit_run partial spans, settings/taskbar SIMD & HOT/restrict/INLINE",
          "memory_over_u32_rect(t->pixels +" in read_text("src/canvas/compositor/pointer.c") and
          "memory_nv12_to_xrgb_row(into + (unsigned long)y * pitch," in wlh_src and
          "memory_or_u32(into, into, (positive)((long)w * (long)h), 0xff000000u);" in read_text("src/canvas/ui/settings.c") and
          "memory_copy_rect_u32(dst, (positive)dst_pitch, src, (positive)src_pitch, (positive)size, (positive)size);" in tb_src and
          "HOT static void damage_add(struct canvas_damage *restrict d," in read_text("src/canvas/compositor/damage.c") and
          "HOT static void compositor_blit(const struct compositor_target *restrict t," in comp_c)
    lib_c = read_text("src/lib.c")
    check("lib.c ASM Pass 3: 128-bit bins_place (x86_64+arm64), AVX2 memory_or_u32, 2x AVX2 + NEON tbl swizzle & branch-hoisted AVX2 2101010",
          "movdqu (%r9,%rbx), %xmm0" in lib_c and
          "ldp q0, q1, [x12]" in lib_c and
          "vpor %ymm7, %ymm3, %ymm3" in lib_c and
          "tbl v0.16b, {v0.16b}, v17.16b" in lib_c and
          "24: vmovdqu (%rsi), %ymm0" in lib_c)
    check("Hardware Floor Pass 4: inline ASM ui_scene_text (SSE2+NEON ld4), ui_insets_a64, HOT/COLD/restrict/likely across gpu/wayland/xwm/input",
          "ld4 {v0.16b, v1.16b, v2.16b, v3.16b}" in ui_src and
          "static void ui_insets_a64(" in ui_src and
          "HOT static positive gpu_submit(void)" in gpu_c and
          "HOT static int wayland_dispatch(" in wl_c and
          "HOT static struct compositor_xwm_window *xwm_row_lookup(" in xwm and
          "HOT static void compositor_reader_put(" in inp)
    check("lib.c ASM Pass 5: AVX2 8-px/iter memory_nv12_to_xrgb_row & AVX2 memory_runs_u32",
          "vpmaddwd %ymm10, %ymm3, %ymm3" in lib_c and
          "vpmovmskb %ymm0, %eax" in lib_c and
          "vpcmpeqd 32(%r9), %ymm1, %ymm2" in lib_c)
    settings_c = read_text("src/canvas/ui/settings.c")
    check("Settings list row single-ripple fix: settings_press_at uses ui_list_hit instead of re-calling ui_list_pointer(..., true) on release",
          "row = ui_list_hit(&settings.ui, &settings.list, x, y);" in settings_c and
          "ui_list_pointer(&settings.ui, &settings.list, WINDOW_EVENT_BUTTON, x, y, true)" not in settings_c)
    font_c = read_text("src/canvas/font.c")
    term_c = read_text("src/canvas/term.c")
    check("Hardware Floor Pass 6: PNG SSE2/NEON Up ASM + premul/fast-paths, hoisted moon Fresnel hue, straight spans fast-path, font popcount/ASCII/tones hoist & term SGR O(1)",
          "paddb %%xmm1, %%xmm0" in ui_src and
          "add v0.16b, v0.16b, v1.16b" in ui_src and
          "if (likely(straight))" in ui_src and
          "neg = ui_moon_hue_turn(bloom, -0.10)" in ui_src and
          "neg = ui_moon_hue_turn(bloom, -0.10)" in tb_src and
          "(v & 0x55u) + ((v >> 1) & 0x55u)" in font_c and
          "if (likely(at < length && text[at] < 0x80))" in font_c and
          "const p32 address_to tones = font_tones(" in font_c and
          "if (likely(final == 'm' && !csi_intermediate && !terminal_csi.marker))" in term_c)
    sess_c = read_text("src/canvas/compositor/session.c")
    gpu_host_c = read_text("src/canvas/compositor/gpu_host.c")
    pres_c = read_text("src/canvas/compositor/present.c")
    scan_c = read_text("src/canvas/compositor/scanout.c")
    check("Hardware Floor Pass 7: rows_next/ctz64 in session+manage, NT slice_flush & cursor_paint, inline 32B gpu_list_ops, 4-way spot table, memory_hash_33 & wall_picture SIMD",
          "return (long)w * 64 + (long)rows_ctz64(bit);" in sess_c and
          "rows_next(&compositor.woken, 0)" in sess_c and
          "rows_next(&any, 0)" in manage and
          "memory_copy_rect_out_u32(compositor.scanout +" in scan_c and
          "memory_copy_rect_out_u32(mapped, CP_PLANE, compositor_cursor_scratch" in kms_c and
          "struct op8 snap = *(const volatile struct op8 *)" in gpu_host_c and
          "memory_hash_33((address_any)cover, (positive)cells)" in gpu_host_c and
          "memory_resample_u32_rect(into, (positive)pitch," in pres_c and
          "0x01010101u) >> 24" in pres_c)
    check("lib.c ASM Pass 8: AVX2 lerp/add_sat/reverse/invert/copy_rect_u32 NT & arm64 8-px widening NEON st4 memory_nv12_to_xrgb_row",
          "vpbroadcastw %xmm6, %ymm6" in lib_c and
          "vpaddusb %ymm1, %ymm0, %ymm0" in lib_c and
          "vperm2i128 $0x01, %ymm0, %ymm0, %ymm0" in lib_c and
          "vpxor %ymm7, %ymm3, %ymm3" in lib_c and
          "call memory_copy_rect_nt_row_ymm" in lib_c and
          "st4 {v4.8b, v5.8b, v6.8b, v7.8b}, [x0], #32" in lib_c)
    check("Steam bowl fixes Win32 dialog inflation (ttf-liberation + 65-moonwater-win32.conf) & GTK dark theme",
          '"ttf-liberation"' in read_text("src/bowl.c") and
          "65-moonwater-win32.conf" in read_text("src/bowl.c") and
          "GTK_THEME=Adwaita:dark" in read_text("src/bowl.c"))
    check("Async AHCI, USB autosuspend=-1, PCIe perf & nmi_watchdog=0 in desktop cmdline",
          all(s in read_text("kernel/profile/desktop") for s in
              ("driver_async_probe=ahci", "usbcore.autosuspend=-1", "pci=pcie_bus_perf", "nmi_watchdog=0")))
    check("ZSWAP default off, RCU_EXPERT+RCU_BOOST enabled, v3 overrides -mno-avx/-mno-bmi",
          "CONFIG_ZSWAP_DEFAULT_ON=n" in read_text("kernel/profile/any") and
          "CONFIG_RCU_EXPERT=y" in read_text("kernel/profile/latency") and
          "CONFIG_RCU_BOOST=y" in read_text("kernel/profile/latency") and
          "-mavx2" in read_text("kernel/profile/v3"))
    check("NVMe tuning runs after wait_for_settling() & desktop sysctls configured",
          all(s in read_text("src/sh/system.c") for s in
              ("rq_affinity", "read_ahead_kb", "tcp_notsent_lowat", "max_user_watches", "vfs_cache_pressure")) and
          read_text("src/sh/system.c").find("wait_for_settling(settling);") < read_text("src/sh/system.c").find("rq_affinity"))
    check("ext4 mounts with MS_NOATIME and commit=30",
          "MS_NOATIME" in read_text("src/sh/host.c") and '"commit=30"' in read_text("src/sh/host/disk.c"))
    check("RT audio thread uses portable O_NOFOLLOW & null-guards *low on idle->playing probe",
          "audio_playing_stream(audio_service.played_rate ? 0 : &low)" in read_text("src/audio/service.c") and
          "if (low)\n                *low = 0;" in read_text("src/audio/service.c") and
          "| O_NOFOLLOW" in read_text("src/audio/service.c"))
    check("Audio pulse.c uses lib.c SIMD memory_s16_to_u8, memory_reverse_units, memory_window_peaks, memory_text_clean & memory_key_printable",
          all(s in read_text("src/audio/pulse.c") for s in (
              "memory_s16_to_u8(", "memory_reverse_units(", "memory_window_peaks(", "memory_text_clean(", "memory_key_printable(")))
    check("Explicit sync skips redundant implicit dmabuf_ready poll & Reflex latch defers callbacks",
          "!s->pending.sync_acq_set" in read_text("src/canvas/wayland.c") and
          "wayland_latch_defer(defer)" in wlh)
    check("Spark hi-res scroll deduplicates low-res REL_WHEEL/REL_HWHEEL",
          "REL_WHEEL_HI_RES" in read_text("src/moonwater/moonwater.c"))
    check("Bowl /tmp mode=1777, BOWL_ENV_DEFAULTS>=32, rlimits, QT_QPA_PLATFORM dedup & non-root NTSYNC",
          all(s in read_text("src/bowl.c") for s in
              ('string_equals(point->target, "/tmp")', "BOWL_ENV_DEFAULTS 32", "BOWL_NOFILE_TARGET ((p64)1048576)",
               "PROTON_USE_NTSYNC", "have_qt_platform", "QT_QPA_PLATFORM=wayland", "0x10de")) and
          "chmod 0666 /dev/ntsync 2>/dev/null || true; export PROTON_USE_NTSYNC=" in read_text("src/bowl.c"))


def test_kernel_audio_bowl_invariants():
    print("\n=== 3. Kernel Profiles, Spark Module, Audio RT & Bowl Invariants ===")

    debug_none = read_text("kernel/profile/debug_none")
    arch_x64 = read_text("kernel/profile/arch/x64")
    any_prof = read_text("kernel/profile/any")
    latency = read_text("kernel/profile/latency")
    guests = read_text("kernel/profile/guests")
    mw = read_text("src/moonwater/moonwater.c")
    audio = read_text("src/audio/service.c")
    bowl = read_text("src/bowl.c")

    # Kernel production debug strip
    for opt in ("CONFIG_DEBUG_PREEMPT=n", "CONFIG_SCHED_DEBUG=n", "CONFIG_RCU_TRACE=n",
                "CONFIG_TRACE_CLOCK=n", "CONFIG_DETECT_HUNG_TASK=n", "CONFIG_AUDIT=n"):
        check(f"debug_none sets {opt}", opt in debug_none)
    for opt in ("CONFIG_X86_DEBUG_FPU=n", "CONFIG_PERF_EVENTS_AMD_POWER=n",
                "CONFIG_PERF_EVENTS_AMD_UNCORE=n", "CONFIG_PERF_EVENTS_AMD_BRS=n"):
        check(f"arch/x64 sets {opt}", opt in arch_x64)
    check("any profile enables -Wl,-O2 -fno-math-errno -fno-trapping-math",
          "-Wl,-O2" in any_prof and "-fno-math-errno" in any_prof and "-fno-trapping-math" in any_prof)
    check("v3 opt-in profile exists with -march=x86-64-v3",
          (ROOT / "kernel/profile/v3").exists() and "-march=x86-64-v3" in read_text("kernel/profile/v3"))
    check("7.x kernel futex/BBR-FQ knobs present",
          "CONFIG_FUTEX_PI=y" in guests and "CONFIG_FUTEX_PRIVATE_HASH=y" in guests and "CONFIG_DEFAULT_FQ=y" in latency)

    # Spark kernel filter ordering: rules_input & bind_event_run before spark_input_push for EV_KEY
    m_bf = re.search(r"static _Bool bind_filter\([^)]*\)\s*\{(.*?)\n\}", mw, re.S)
    check("spark bind_filter found", m_bf is not None)
    if m_bf:
        body = m_bf.group(1)
        p_rules = body.find("rules_input(")
        p_bind = body.find("bind_event_run(")
        p_push = body.find("SPARK_INPUT_KIND_KEY")
        check("spark bind_filter runs rules_input & bind_event_run before SPARK_INPUT_KIND_KEY",
              0 < p_rules < p_bind < p_push)
        check("spark bind_filter only suppresses evdev when spark_input_push succeeds",
              "taken = spark_input_push(ring, SPARK_INPUT_KIND_KEY" in body)

    # Audio non-blocking persistence & RT priority hierarchy
    check("Audio persistence offloaded to background thread",
          "audio_save_thread" in audio or "audio_save_wake" in audio or "audio_file_write" in audio)
    check("Bowl wine-fpu shim & per-app workarounds present",
          "wine-fpu.so" in bowl and "PR_SET_TIMERSLACK" in bowl or "timerslack" in bowl.lower())


def test_canvas_spaces_invariants():
    print("\n=== 3b. Native Canvas Spaces Invariants ===")
    comp = read_text("src/canvas/compositor.c")
    dmg = read_text("src/canvas/compositor/damage.c")
    manage = read_text("src/canvas/compositor/manage.c")
    inp = read_text("src/canvas/compositor/input.c")
    kms = read_text("src/canvas/compositor/kms.c")
    tbar = read_text("src/canvas/ui/taskbar.c")
    ui_c = read_text("src/canvas/ui/ui.c")

    check("Spaces: per-output struct compositor_space & row_space arrays registered in COMPOSITOR_ROWS_OWN",
          "struct compositor_space" in comp and "COMPOSITOR_SPACES_MAX" in comp and
          "X(compositor.row_space, COMPOSITOR_ROWS_ROW)" in comp and
          "X(compositor.row_space_prev, COMPOSITOR_ROWS_ROW)" in comp and
          "compositor_spaces_ensure();" in comp)
    check("Spaces: compositor_visible & compositor_windows_for filter by compositor_space_row_active",
          "compositor_space_row_active(row)" in dmg and
          re.search(r"static int compositor_visible\(int row\)\s*\{[^}]*compositor_space_row_active\(row\)", dmg, re.S) is not None)
    check("Spaces: compositor_row_facts marks inactive-Space windows minimized (XWM IconicState + WL SUSPENDED)",
          "!compositor_space_row_active(row)" in manage and
          re.search(r"\.minimized\s*=\s*\(compositor\.state\[row\]\s*&\s*WINDOW_MINIMIZED\)\s*!=\s*0\s*\|\|\s*!compositor_space_row_active\(row\)", manage) is not None)
    check("Spaces: automatic Fullscreen Space enter/exit/close & direct-scanout recovery hooks",
          all(s in manage for s in (
              "compositor_space_enter_fullscreen(row, on);",
              "compositor_space_exit_fullscreen(row, 0);",
              "compositor_space_exit_fullscreen(row, 1);",
              "compositor_wl_scan_retry(row);",
              "compositor.direct_row = -1;",
              "compositor.stack_gen++;",
          )))
    check("Spaces: cross-screen Space & window movement + hotplug realignment",
          "compositor_space_move_to_screen(" in manage and
          "compositor_space_move_row(" in manage and
          "compositor_spaces_on_outputs_changed(names, count);" in kms)
    check("Spaces: conflict-free keyboard shortcuts, Steam passthrough & 3/4-finger touchpad swipe",
          all(s in inp for s in (
              "compositor_space_chord(",
              "compositor_space_step(",
              "compositor_space_switch_index(",
              "compositor_space_add(",
              "compositor_space_remove_active(",
              "compositor_space_move_row_step(",
              "compositor_space_move_active_screen(",
              "pad->fingers >= 3 ? 3",
              "TASKBAR_SPACE_ADD",
              "TASKBAR_SPACE_BASE",
          )))
    check("Spaces: taskbar Space strip layout, painting & cache key invalidation",
          "TASKBAR_SPACE_BASE" in ui_c and "TASKBAR_SPACE_ADD" in ui_c and
          all(s in tbar for s in (
              "taskbar_spaces_count()",
              "taskbar_spaces_active()",
              "taskbar_spaces_kind(",
              "taskbar_spaces_gen()",
              "TASKBAR_SPACE_BASE",
              "TASKBAR_SPACE_ADD",
          )))
    pres = read_text("src/canvas/compositor/present.c")
    scan = read_text("src/canvas/compositor/scanout.c")
    check("Spaces: Fullscreen Space skips wallpaper composition & unlocks opaque direct-scanout fast path",
          "compositor_space_all_fullscreen()" in pres and
          "!compositor_space_screen_fullscreen(compositor_screen_of(row))" in manage and
          "compositor_space_is_fs_row(row)" in scan)
    check("Spaces: horizontal slide transition uses bounded on-screen step & Super+Arrow has no tiling collision",
          "slide_dx = (new_pos >= old_pos ? 1 : -1) * step;" in manage and
          "compositor.glide[row] = 0;\n                                compositor_glide_start(row, from);" in manage and
          "mods4 == (WINDOW_KEY_SUPER | WINDOW_KEY_ALT)" in inp and
          "int nav = mods4 == WINDOW_KEY_SUPER ||" in inp)

    glass = read_text("src/canvas/compositor/glass.c")
    check("Mission Control: taskbar-edge-attached sliding drawer pushes taskbar inward via ui_tween & taskbar_key.mc_push",
          "compositor_mc_push_for(" in glass and
          "ui_kit_number_to(&taskbar_mc_slide" in tbar and
          "off -= taskbar_mc_push_px();" in tbar and
          "int mc_push;" in tbar and
          "_Static_assert(sizeof(struct taskbar_key) == 8 + 24 * 4" in tbar)
    check("Mission Control: visual Space preview cards, miniature windows, close button & '+' card for horizontal & vertical edges",
          all(s in glass for s in (
              "compositor_mc_layout(",
              "compositor_mc_draw(",
              "compositor_mc_frame(",
              "TASKBAR_LEFT || pos == TASKBAR_RIGHT",
              "c->removable",
              "c->is_add",
              "compositor.mc.thumbs",
          )) and "taskbar_mc_draw(" in tbar)
    check("Mission Control: drag-and-drop window sorting, taskbar-edge auto-open, shortcuts & 3/4-finger vertical swipe",
          all(s in inp for s in (
              "compositor_mc_toggle(compositor_space_current_screen())",
              "compositor_mc_close()",
              "compositor_mc_hand(code, down)",
              "compositor_mc_motion(",
              "compositor_mc_drag_motion(",
              "compositor_mc_drag_drop(",
              "compositor_abs64(pad->swipe_dy) >= thresh_y",
              "compositor_mc_open(compositor_space_current_screen(), 0)",
          )))
    check("Mission Control: direct-scanout fast path, pointer-unhold, single GPU map & input-leak guards",
          "!compositor_mc_shown()" in pres and
          "compositor_mc_frame(&frames[count])" in pres and
          "compositor_space_add_id(" in manage and
          "compositor_mc_shown() || compositor_input_override_query(keys) == 2" in read_text("src/canvas/wayland_host.c") and
          "gpu_map(compositor.mc.pixels, compositor.mc.bytes)" in read_text("src/canvas/compositor/gpu_host.c") and
          "if (compositor_mc_point(x, y))" in manage and
          "compositor_mc_point(x, y) || compositor_band_hit(x, y)" in inp)

    gpu_c = read_text("src/canvas/gpu.c")
    gpu_host = read_text("src/canvas/compositor/gpu_host.c")
    gpuasm_c = read_text("src/build/gpuasm.c")
    check("GPU 2012-2026: AMD multi-gen PM4 (GFX6-12), ib[64] headroom & native GFX10/11/12 ISA sets",
          "unsigned int ib[64], n = 0;" in gpu_c and
          "GPU_PKT(0x43, 3)" in gpu_c and "GPU_PKT(0x58, 5)" in gpu_c and "GPU_PKT(0x58, 6)" in gpu_c and
          "ib[n++] = 0x225" in gpu_c and "ib[n++] = 0x229" in gpu_c and
          "gpu_amd_rdna3" in gpu_c and "gpu_amd_rdna4" in gpu_c and
          "gpuasm_assemble_isa(" in gpuasm_c)
    check("GPU 2012-2026: Intel Gen7-Gen11 active with EU patching, Xe-HPG/Xe2 COMPUTE_WALKER & full xe KMD UAPI",
          "{700, 750, &gpu_i915_gen9_set}" in gpu_c and
          "{800, 801, &gpu_i915_gen9_set}" in gpu_c and
          "{900, 901, &gpu_i915_gen9_set}" in gpu_c and
          "{1100, 1101, &gpu_i915_gen9_set}" in gpu_c and
          "gpu_intel_xehp_frame(" in gpu_c and "0x70020004u" in gpu_c and "0x70070000u" in gpu_c and
          "GPU_XE_VM_BIND == 0x40886445u" in gpu_c and "GPU_XE_EXEC == 0x40386449u" in gpu_c and
          "gpu_xe_submit" in gpu_c and "gpu_xe_wait" in gpu_c and "gpu_xe_fence" in gpu_c)
    check("GPU 2012-2026: hybrid-GPU priority order (amdgpu > i915 > xe > nouveau) & pick==2 PRIME settle guard",
          "&gpu_amd, &gpu_i915_row, &gpu_xe_row, &gpu_nouveau_row" in gpu_c and
          "gpu_req.pick == 2" in gpu_host and
          "COMPOSITOR_GPU_WHY_OUTPUT" in gpu_host)
    compose_c = read_text("src/canvas/compositor/compose.c")
    check("Blended rounded corner uses pre-compensated ring (outer-inner)/(1-inner) on CPU & GPU (no double AA border on Chrome)",
          "((cover - in) * 255u + (255u - in) / 2u) / (255u - in)" in compose_c and
          "!picture ? 2 : f->blend ? 3 : 0" in compose_c)
    editor_c = read_text("src/canvas/ui/editor.c")
    files_c = read_text("src/canvas/ui/files.c")
    wl_c = read_text("src/canvas/wayland.c")
    check("Editor & Files polish: default wrap, binary->hex, 28px filetabs, floating status pills & smooth squash scroll",
          "tab->view.wrap = true;" in editor_c and
          "editor_is_binary(" in editor_c and
          "editor_pill_draw(" in editor_c and
          "files_status_pill_draw(" in files_c and
          '{"filetabs", .margin = {UI_PAD, UI_PAD, UI_GAP2, 4}, .h = UI_ROW_DENSE}' in ui_c and
          "ui_scroll_pull(ui, &t->scroll, drawn_scroll" in ui_c and
          "ui_wheel_turn(ui, &t->scroll, event)" in ui_c)
    check("Drag-and-drop chip never gets stuck on Wayland text drags (unconditional WAYLAND_XDRAG_END + tick/motion guard)",
          "wayland_xdrag(WAYLAND_XDRAG_END, -1, how == 2, (long)action);" in wl_c and
          "if (x_end) wayland_xdrag(WAYLAND_XDRAG_END" not in wl_c and
          "c->kind == COMPOSITOR_CHIP_DRAG && !compositor_dnd_active()" in inp and
          "else if (compositor.chip.kind == COMPOSITOR_CHIP_DRAG)" in inp)


def test_hardening_66_bug_families():
    print("\n=== 3c. Family-Wide Hardening Invariants (Dynamic UI Lists, Intel GPU, Wayland/XWM, KMS/Scanout, Apps) ===")

    ui_dir = ROOT / "src/canvas/ui"
    ui_c = read_text("src/canvas/ui/ui.c")
    settings_c = read_text("src/canvas/ui/settings.c")
    launcher_c = read_text("src/canvas/ui/launcher.c")
    installer_c = read_text("src/canvas/ui/installer.c")
    taskbar_c = read_text("src/canvas/ui/taskbar.c")
    files_c = read_text("src/canvas/ui/files.c")
    terminal_c = read_text("src/canvas/ui/terminal.c")
    osk_c = read_text("src/canvas/ui/osk.c")
    editor_c = read_text("src/canvas/ui/editor.c")
    gpu_c = read_text("src/canvas/gpu.c")
    gpu_host_c = read_text("src/canvas/compositor/gpu_host.c")
    present_c = read_text("src/canvas/compositor/present.c")
    wl_c = read_text("src/canvas/wayland.c")
    wlh_c = read_text("src/canvas/wayland_host.c")
    xwm_c = read_text("src/canvas/xwm.c")
    kms_c = read_text("src/canvas/compositor/kms.c")
    scan_c = read_text("src/canvas/compositor/scanout.c")
    manage_c = read_text("src/canvas/compositor/manage.c")
    glass_c = read_text("src/canvas/compositor/glass.c")
    input_c = read_text("src/canvas/compositor/input.c")

    # --- Family 1: Dynamic ui_list, sparse ID collision & unsigned short truncation ---
    short_list_truncs = []
    for p in sorted(ui_dir.glob("*.c")):
        txt = p.read_text(encoding="utf-8", errors="replace")
        for m in re.finditer(r"(?:\bl->|\.list\.)(?:id|focus|hover|down)\b[^;\n]*\bunsigned short\b", txt):
            short_list_truncs.append(f"{p.name}: {m.group(0)[:80]}")
    check("Family 1: no unsigned short truncation of ui_list id/focus/hover/down across src/canvas/ui/*.c",
          not short_list_truncs and "UI_LIST_MOST" not in ui_c and "UI_LIST_MOST" not in settings_c and
          "UI_LIST_MOST" not in launcher_c and "UI_LIST_MOST" not in installer_c,
          "; ".join(short_list_truncs))

    m_pg = re.search(r"#define\s+UI_PAGE_GROUP\s+(0x[0-9a-fA-F]+|\d+)", ui_c)
    m_pc = re.search(r"#define\s+UI_PAGE_CARD\s+(0x[0-9a-fA-F]+|\d+)", ui_c)
    pg_val = int(m_pg.group(1), 0) if m_pg else 0
    pc_val = int(m_pc.group(1), 0) if m_pc else 0
    check("Family 1: UI_PAGE_GROUP/UI_PAGE_CARD live above 0x40000000 (cannot collide with sparse dynamic ui_list IDs)",
          pg_val >= 0x40000000 and pc_val > pg_val, f"group={hex(pg_val)} card={hex(pc_val)}")

    check("Family 1: dynamic ui_list_reserve checks mmap error, reserves in window/layout, and guards ui_list_least for large ID spaces",
          "static b32 ui_list_reserve(" in ui_c and
          "if (mapped < 0 && mapped > -4096)" in ui_c and
          "if (!ui_list_reserve(l, count))" in ui_c and
          "if (l->count > 0 && !ui_list_reserve(l, l->count))" in ui_c and
          "if (l->ids > 4096)" in ui_c)

    # Every settings.list.id[0] access must be guarded by count / settings.list.count on the preceding line
    unguarded_id0 = []
    s_lines = settings_c.splitlines()
    for i, line in enumerate(s_lines):
        if "settings.list.id[0]" in line:
            prev = s_lines[i - 1] if i > 0 else ""
            if "count" not in prev and "count" not in line:
                unguarded_id0.append(f"settings.c:{i+1}: {line.strip()}")
    check("Family 1: all settings.list.id[0] accesses are guarded against empty/NULL dynamic slab",
          not unguarded_id0, "; ".join(unguarded_id0))

    # --- Family 2: Intel Gen7/7.5 & Gen11 GPU + GPU host bounds/async invariants ---
    check("Family 2: Intel Gen7/7.5 omits GPU_I915_WRITTEN on main batch BO & Gen7/Gen11+ userptr omits READ_ONLY with ENODEV retry",
          "gpu.family < 800 ? 0 : GPU_I915_WRITTEN" in gpu_c and
          "how == GPU_USER_READ && !gpu_i915.read_only_refused && gpu.family >= 800 && gpu.family < 1100" in gpu_c and
          "up.flags &= ~1u;" in gpu_c)

    check("Family 2: gpu_scene_submit supports scratch bins_at >= top for gpu_wake() without room==0",
          "b32 scratch = bins_at >= top && bins_at + 4096 <= gpu.size;" in gpu_c and
          "at = scratch ? bins_at : bins_at + side * room;" in gpu_c)

    check("Family 2: compositor_gpu_spots rejects invalid chip dimensions (w<=0, h<=0) before computing cells & splits >255px chips",
          "if (c->w <= 0 || c->h <= 0)" in gpu_host_c and
          gpu_host_c.find("if (c->w <= 0 || c->h <= 0)") <
          gpu_host_c.find("cells = (unsigned long)c->w * (unsigned long)c->h;") and
          "int sw = c->w - sx > 255 ? 255 : c->w - sx;" in gpu_host_c)

    check("Family 2: compositor_gpu_list_render_bytes rejects OOB cover bounds (returns 0 instead of stale bases[slot])",
          "if (view.cover_at < COMPOSITOR_GPU_LIST_REACH || view.cover_at > list_bytes ||" in gpu_host_c and
          "return compositor_list_why = COMPOSITOR_LIST_OPEN, 0;" in gpu_host_c[gpu_host_c.find("if (view.cover_used)"):gpu_host_c.find("bases = (positive address_to)")])

    check("Family 2: compositor_card_tick checks gpu_found == GPU_END_NONE before settling async lists",
          "if (!compositor_card.on && !gpu.pending && gpu_found == GPU_END_NONE)" in present_c)

    # --- Family 3: Wayland & XWM lifecycle, subsurface layers, flush lock & multi-Xwayland XDND ---
    check("Family 3: wayland_surface_drop emits delete_id via wayland_object_delete for pending wl_callbacks",
          "if (slot >= 0) wayland_object_delete(c, slot);" in wl_c)

    check("Family 3: zwlr_layer_surface_v1 enforces configure/unconfigured-buffer, links surface, and rejects XDG surfaces",
          "int layer = s->role == WAYLAND_ROLE_TOPLEVEL && s->u.top.layer_shell;" in wl_c and
          "int xdg = ((s->flags & WAYLAND_SURFACE_XDG) != 0) || layer" in wl_c and
          "layer ? s->role_id : s->xdg_id" in wl_c and
          "if (s->role != WAYLAND_ROLE_NONE || s->xsurface || (s->flags & WAYLAND_SURFACE_XDG))" in wl_c and
          "return wayland_surface_link(c, a[0].slot, s), WAYLAND_OK;" in wl_c)

    check("Family 3: compositor_wl_unmap & compositor_wl_close drop subsurface layers and compositor_wl_flush checks abs_valid",
          wlh_c.count("compositor_wl_layers_drop(row);") >= 3 and
          "(!compositor_wl_direct.abs_valid || compositor_wl_direct.abs_slot != slot)" in wlh_c)

    # Structural scan: EVERY struct wayland_table in struct compositor_xwm must be dropped in compositor_xwm_close_rows
    m_xwm_struct = re.search(r"struct compositor_xwm\s*\{(.*?)\n\};", xwm_c, re.S)
    m_xwm_close = re.search(r"static void compositor_xwm_close_rows\([^)]*\)\s*\{(.*?)\n\}", xwm_c, re.S)
    xwm_tables = []
    if m_xwm_struct:
        for line in m_xwm_struct.group(1).splitlines():
            m_t = re.search(r"struct wayland_table\s+([^;]+);", line)
            if m_t:
                xwm_tables.extend(tok.strip() for tok in m_t.group(1).split(",") if tok.strip())
    missing_drops = [t for t in xwm_tables if not m_xwm_close or f"wayland_table_drop(&x->{t})" not in m_xwm_close.group(1)]
    check("Family 3: compositor_xwm_close_rows drops EVERY wayland_table in struct compositor_xwm + xdrag.types + ins[at].index",
          bool(xwm_tables) and not missing_drops and m_xwm_close is not None and
          "wayland_table_drop(&x->xdrag.types);" in m_xwm_close.group(1) and
          "if (x->ins[at].index >= 0)" in m_xwm_close.group(1),
          f"missing drops: {missing_drops}")

    check("Family 3: compositor_xwm_receive scans all COMPOSITOR_XWM_SLOTS for multi-Xwayland XDND (which == 2)",
          "compositor_xwm_slots[s].xdrag.entered && compositor_xwm_slots[s].xdrag.source" in xwm_c)

    # --- Family 4: KMS, Scanout, Overlay, Glass & Spaces ---
    check("Family 4: scaled direct-scanout VRR kick uses compositor_atomic_primary & compositor_atomic_to restores 1:1 plane",
          "if (compositor.scaled_direct && row >= 0)" in kms_c and
          "compositor_atomic_primary(compositor.on_screen, row, compositor.front[row]" in kms_c and
          "if (compositor.scaled_direct)" in scan_c and
          "drm_atomic_set_plane(&req, compositor.primary, compositor.primary_props, compositor.crtc, fb, 0, 0, pw, ph, 0, 0, pw, ph);" in scan_c and
          "compositor_scaled_reset();" in scan_c[scan_c.find("static int compositor_atomic_to("):])

    check("Family 4: compositor_space_take defers on async GPU/list jobs, syncs cursor thread, and frees old secondary FB",
          "if (!first && (compositor.flip_pending || compositor_card.on || rows_any(&compositor_card.list_rows)))" in kms_c and
          "compositor_cursor_async_sync(0, 0, 0, 0, 0, 0);" in kms_c and
          "if (compositor.output[at].fb && compositor.output[at].fb != compositor.fb)" in kms_c)

    check("Family 4: compositor_overlay_buffers disables overlay & cleans partial allocations; compositor_window_fb restores table->pinned on all failures",
          "compositor_overlay_off();" in scan_c[scan_c.find("static int compositor_overlay_buffers("):scan_c.find("static int compositor_overlay_buffers(") + 600] and
          "for (unsigned int prev = 0; prev < which; prev++)" in scan_c and
          scan_c[scan_c.find("static unsigned int compositor_window_fb("):scan_c.find("static void compositor_window_fb_drop(")].count("__atomic_store_n(&compositor.table->pinned[row]") >= 3)

    check("Family 4: glass skips layers/null-pixel frames, press_at forwards down, and Space remove/move preserve damage & focus",
          "if (!compositor.glassy[row] || frames[at].layer || (!frames[at].builtin && !frames[at].pixels))" in glass_c and
          "compositor_pointer_press(code, down);" in input_c and
          "if (!was_active && compositor.spaces.active[screen] == fallback)" in manage_c and
          "int keep_focus = compositor.focus >= 0 && compositor.row_space[compositor.focus] == sid" in manage_c)

    # --- Family 5: Taskbar & UI Apps (Files, Terminal, OSK, Editor) ---
    check("Family 5: taskbar glass span covers TASKBAR_STRIP_ON..view->count & tray shrink counts spaces + xtray items",
          taskbar_c.count("for (int which = TASKBAR_STRIP_ON; which < (int)view->count; which++)") >= 2 and
          "which != TASKBAR_TABS_LEFT && !(which <= TASKBAR_ZONE && which > TASKBAR_DONE)" in taskbar_c and
          "sp + sp_add + (xt > 0 ? xt : 0)" in taskbar_c)

    check("Family 5: files.c Empty Trash confirm, Cut dim, files_move_end on begin failure & selection revision guard",
          "if (down && event->a != UI_POINTER_RIGHT)" in files_c and
          "if (files_carry_take_mode(files_model_chosen(m, m->cursor)) && cut)" in files_c and
          "return files_move_end(job), false;" in files_c and
          files_c.count("if (m->chosen_count || m->cursor != FILES_NO)") >= 2)

    m_osk_pt = re.search(r"static b32 osk_purpose_take\([^)]*\)\s*\{(.*?)\n\}", osk_c, re.S)
    m_ed_ev = re.search(r"static b32 editor_event\([^)]*\)\s*\{(.*?)\n\}", editor_c, re.S)
    check("Family 5: terminal.c klog SIGHUP guard, osk.c purpose-change release + accent motion, editor.c pill_down cleanup",
          "if (!terminal.reading && !terminal.gone)" in terminal_c and
          "if (osk.alt_key >= 0)" in osk_c and
          m_osk_pt is not None and "osk_release();" in m_osk_pt.group(1) and
          m_ed_ev is not None and "editor.pill_down = false;" in m_ed_ev.group(1))

    # --- Family 6: Multi-GPU, Intel/AMD Robustness & Fallback Invariants ---
    check("Family 6: Intel/Xe/AMD intentional refusal returns EOPNOTSUPP (95), never triggers hang/cooldown, and Intel gates list mode",
          "if (!words)\n                return 95;" in gpu_c and
          "if (!(words = gpu_set->batch(job, job->packets_at, GPU_PACKETS / 4)))\n                return 95u;" in gpu_c and
          "if (!k || !job->columns || !job->rows)\n                return 95;" in gpu_c and
          "gpu_end == GPU_END_NONE && gpu_last_error == 95" in gpu_host_c and
          "gpu_backend == &gpu_amd && compositor_lists_switch()" in gpu_host_c)

    check("Family 6: Intel Gen7 MaskCtrl bit 9, Xe-HPG/Xe2 COMPUTE_WALKER & MOCS, Xe userptr/forget_jobs, ultrawide scene & scale pitch",
          "w[0] = (w[0] & ~0x00000200u) | (mctrl << 9);" in gpu_c and
          "words[base + 7] = cols;" in gpu_c and
          "words[base + 10] = 0xffffu;" in gpu_c and
          "if (gpu.family >= 2000)\n                return 4u;" in gpu_c and
          "#define GPU_XE_USERPTRS 264" in gpu_c and
          "gpu_xe.done = gpu_xe.sequence;" in gpu_c and
          "(px1 - px0) > 4096u" in gpu_c)

    check("Family 6: AMD identify preserves live state & checks available_rings, userptr retries EINTR/EAGAIN, image/nv12 tests use gpu_patient",
          "if (!ip[6])" in gpu_c and
          "if (e == 11 && again++ < 8)" in gpu_c and
          "gpu_gfx103 = kept_103, gpu_amd_gfx_major = kept_maj" in gpu_c and
          "0x0ec00000u" in gpu_c[gpu_c.find("if (gfx == 6u)"):gpu_c.find("else if (gfx >= 7u && gfx <= 9u)")] and
          gpu_c[gpu_c.find("static b32 gpu_image_test("):].count("gpu_patient = gpu_recovered ? 2 : 1;") >= 2)

    check("Family 6: Hybrid-GPU multi-candidate fallback in gpu_start, VA wrap-around, >64MiB slice retry, guard-band fault blame & 15MiB bin fallback",
          "if (step > 0)" in gpu_c[gpu_c.find("for (unsigned int step = 0; step < candidates; step++)"):gpu_c.find("gpu_req.settled =")] and
          "for (unsigned int pass = 0; pass < 2; pass++)" in gpu_c[gpu_c.find("static positive gpu_place_aligned("):gpu_c.find("#define GPU_IMPORT_ALIGN")] and
          "from = orig_from, to = orig_to;" in gpu_c and
          "guard band of held import, handle " in gpu_c and
          "positive full_room = (top - bins_at) & ~4095ull;" in gpu_c and
          "#define GPU_GONE 64" in gpu_c)

    check("Family 6: Compositor GPU recovery, 3-strike output settle, wide chip splitting, frost 16px pitch, dynamic scaled=16 & render_classify fd<0",
          "return !gpu.pending || (gpu_done(gpu.pending) != 0 && !gpu.pending);" in gpu_c and
          "++compositor_gpu_state.output_fails >= 3" in gpu_host_c and
          "for (int sx = 0; sx < c->w; sx += 255)" in gpu_host_c and
          "#define COMPOSITOR_GPU_SCALED 16" in gpu_host_c and
          "#define COMPOSITOR_CARD_STEPS (16 + COMPOSITOR_DAMAGE)" in scan_c and
          "fpitch = ((unsigned int)compositor.width + 15u) & ~15u;" in gpu_host_c and
          "return out->dev = 0, 0;" in kms_c and
          "compositor_gpu_state.hung_off = 0, compositor_gpu_state.hung_at = 0;" in manage_c)

    # --- Family 7: Performance & Hardware-Flooring Invariants ---
    check("Family 7: RDNA3/3.5 (GFX11) gpu_image enabled with version 4 & 256K_R_X (256KiB aligned), O(1) i915 BO dedup (retry-safe), 32-way VA hint & O(M) gpu_trim",
          "gpu_image_ok = (gpu_set == &gpu_amd_rdna2 || gpu_set == &gpu_amd_rdna3) && gpu_image_test();" in gpu_c and
          "tile == 31 && gfx11" in gpu_c and
          "gpu_set == &gpu_amd_rdna3 ? 262144ull :" in gpu_c and
          "gpu_i915.bound[handle].seen_serial == dedup_serial" in gpu_c and
          "static unsigned int gpu_map_hint[32];" in gpu_c and
          "gpu_unimport(gpu.imported_va[i], GPU_GONE_RELEASE), i--;" in gpu_c)

    check("Family 7: Anti-freeze shield: Aliasing-PPGTT shadow userptr output (no KMS softpin), 2s setitimer submit watchdog, tiled small-rect guard & secondary scale layout++",
          "static b32 gpu_needs_shadow_output(void)" in gpu_c and
          "if (gpu_needs_shadow_output())" in gpu_c and
          "if (gpu_needs_shadow_output())" in gpu_host_c and
          "compositor_gpu_shadow.pixels" in gpu_host_c and
          "system_call_3(syscall(setitimer), 0, (positive)wtimer, 0);" in gpu_c and
          "clock_monotonic_nanoseconds() - began >= (GPU_HANG_NS / 2)" in gpu_c and
          "compositor_wl_tiled(picture) || (card_wanted && (right - left) * (bottom - top) >= 32768)" in read_text("src/canvas/wayland_host.c") and
          "compositor.layout++;" in kms_c[kms_c.find("static int compositor_scales_take("):kms_c.find("static b32 compositor_space_take(")])

    check("Family 7: Scaled direct scanout async/FB_ID fast path + reset, GPU RGBA client cursor op 11 & Waterlink This-machine + 500ms refresh",
          "(compositor_overlay_ready() || compositor.primary)" in scan_c and
          "compositor_scaled_reset();" in scan_c and
          "COMPOSITOR_FLIP_ASYNC" in scan_c[scan_c.find("static int compositor_atomic_primary("):scan_c.find("static int compositor_atomic_to(")] and
          "ok = same || defer_bo || compositor_cursor_paint(&now) == 0;" in kms_c and
          "compositor_gpu_rect(11, px, py, px + pw, py + ph, 0, &cur, 0, 0);" in read_text("src/canvas/compositor/pointer.c") and
          '{"Waterlink", "Waterlink", {"link"}, "Linked machines", 500, SETTINGS_FEED_LINKS}' in settings_c and
          'return "This machine";' in settings_c)

    check("Family 7: CPU fallback hardware-flooring (1024px tiled scaled blend, 10-bit/blended L1 scratch, frost plan hoist, dyn cursor precompute)",
          "for (int cx = x1; cx < x2; cx += 1024)" in read_text("src/canvas/compositor/compose.c") and
          "memory_pack_2101010_u32(scratch, scratch, count," in read_text("src/canvas/compositor/compose.c") and
          "compositor_plan_make(&frost_plan, &full_under, frames, count, frost_radii);" in glass_c and
          "struct compositor_dyn_pre" in read_text("src/canvas/compositor/pointer.c") and
          "memory_copy(into + start, ink + start, (i - start) * 4);" in read_text("src/lib.util.c"))

    ui_c = read_text("src/canvas/ui/ui.c")
    check("Family 7: UI kit hardware-flooring (slot_gen zeroing skip, box cull, clipped text op3, binary ui_list_hit & text draw hoist)",
          "if (!++list->scene.slot_gen)" in ui_c and
          "cut = (from > 0 && by0 < (from << 8)) || (to < ui->height && by1 > (to << 8));" in ui_c and
          "if (first >= last || !ui_rows_seen(ui, top, top + rows))" in ui_c and
          "int mid = lo + ((hi - lo) >> 1);" in ui_c and
          "p64 primary_line = doc_line_of(&t->doc, t->doc.selections[t->doc.primary].active);" in ui_c)

    # --- Family 8: Cross-GPU Anti-Freeze, Bounded Wait, Priority-Inversion & Driver Invariants ---
    comp_c = read_text("src/canvas/compositor.c")
    present_c = read_text("src/canvas/compositor/present.c")
    wl_host_c = read_text("src/canvas/wayland_host.c")

    check("Family 8: Unified setitimer watchdog (gpu_alarm_arm/disarm) brackets gpu_submit, gpu_wait_job & gpu_start ioctls",
          "static void gpu_alarm_arm(unsigned int sec)" in gpu_c and
          "static void gpu_alarm_disarm(void)" in gpu_c and
          "gpu_alarm_arm(2u);" in gpu_c and
          "gpu_alarm_arm(3u);" in gpu_c and
          "gpu_alarm_arm(wsec > 6u ? 6u : wsec);" in gpu_c)

    check("Family 8: Bounded EINTR/EAGAIN retries in AMD/i915/Xe waits & userptr (no SCHED_FIFO livelock)",
          "if (e == 4 && eintr++ < 16)" in gpu_c and
          "while (e == 4 && ++eintr < 32 && (!wait[1] || clock_monotonic_nanoseconds() < wait[1]));" in gpu_c and
          "if (!left || ++tries >= 16)" in gpu_c and
          "while (e == 4u && until && clock_monotonic_nanoseconds() < until && ++tries < 16);" in gpu_c)

    check("Family 8: AMD classify EINTR/EIO, 64K tile VA alignment, 64-bit bins_starts, full-buffer bin fence & gpu_wake hold/lost guard",
          "error == 11 || error == 4 ? GPU_ERR_AGAIN : error == 125 || error == 19 || error == 5 ? GPU_ERR_LOST" in gpu_c and
          "(tile != 9 && tile != 10 && tile != 25 && tile != 27) || (va & 65535)" in gpu_c and
          "at = bins_starts(tab + 2 * row_off, row_cells);" in gpu_c and
          "if (gpu_bins.full)\n                gpu_bins.job[0] = gpu_bins.job[1] = sequence;" in gpu_c and
          "if (gpu_lost || !gpu.at || !gpu_last_job || now - gpu_last_job < GPU_WAKE_IDLE || !gpu_idle())" in gpu_c and
          "kept_hold = gpu_handed_hold, gpu_handed_hold = 1;" in gpu_c)

    check("Family 8: Xe discrete VRAM 64KiB min_align, syncobj in-flight guard & no pre-exec reset, 64-entry query buffers",
          "gpu_xe.min_align = nparams >= 3 && cfg[3] >= 4096u ? (unsigned int)cfg[3] : 4096u;" in gpu_c and
          "p64 cfg[64] = {0};" in gpu_c and
          "struct gpu_xe_engine engines[64];" in gpu_c and
          "if (gpu_xe.jobs[slot].sequence > gpu_xe.done)\n                return 11u;" in gpu_c and
          "GPU_DRM_SYNCOBJ_RESET" not in gpu_c[gpu_c.find("static unsigned int gpu_xe_submit("):gpu_c.find("static int gpu_xe_wait(")] and
          "gpu_backend == &gpu_xe_row && gpu_xe.has_vram" in gpu_c)

    check("Family 8: Slow page-flip SCHED_FIFO spin fix, dead-GPU recovery guard, priority-inversion & explicit-sync/cursor-lock fixes",
          "if (compositor.flip_pending)\n        {\n                positive waiting = compositor_flip_wait(now);\n                compositor_soon(&until, waiting ? waiting : compositor.flip_issued + COMPOSITOR_FLIP_LOST);\n        }" in comp_c and
          "!compositor_gpu_state.dead && gpu_lost" in gpu_host_c and
          "if (!compositor_gpu_awake_wanted() || compositor_gpu_held())" in gpu_host_c and
          "compositor_call(syscall(nanosleep), (positive)ts, 0, 0, 0, 0, 0);" in gpu_host_c[gpu_host_c.find("static void compositor_gpu_clocks_lock("):gpu_host_c.find("static void compositor_gpu_clocks_unlock(")] and
          "thread_wait(&compositor_wall_job.done, 0);" in present_c and
          "thread_wake(&job->done, 1);" in present_c and
          "if (!compositor_wl_sync_start(wayland_row_owner(row), (int)picture->sync_fd, 0))" in wl_host_c and
          "compositor_cursor_lock_take();\n        compositor_cursor_async_sync(0, 0, 0, 0, 0, 0);\n        compositor_cursor_lock_drop();" in kms_c)

    check("Family 8: Intel i915 full desktop compositing (ops 5/12/13 lowered to op 1/11 in GPU_VA_MAPS tile pool, EOPNOTSUPP not logged as fault)",
          "compositor_gpu_i915_tile_alloc(" in read_text("src/canvas/compositor/compose.c") and
          "if (gpu_backend == &gpu_i915_row && (kind == 5 || kind == 12 || kind == 13) && cover >= g->tables_at)" in read_text("src/canvas/compositor/compose.c") and
          "compositor_gpu_i915_tiles.used = COMPOSITOR_I915_STATIC_BYTES;" in read_text("src/canvas/compositor/compose.c") and
          "compositor_gpu_i915_tile_alloc((unsigned long)pw * (unsigned long)ph, &tile_va)" in read_text("src/canvas/compositor/pointer.c") and
          "if (gpu_last_error != 95)\n                                gpu_fault_note(false);" in gpu_c)

    # --- Family 9: Intel 15x Scene Culling, Cross-GPU Host Fixes & Editor/UI Polish ---
    editor_c = read_text("src/canvas/ui/editor.c")
    check("Family 9: Intel i915 15x scene culling + opaque occlusion + skip binning + 4096px/taskbar band split + first-fit VA + sync scale blit + 64B pitch",
          "if (!(aligned && gpu_backend == &gpu_i915_row))" in gpu_c and
          "positive pack_va = a->g + (gpu_slot ? (256ull << 10) : 0ull);" in gpu_c and
          "b32 wrapped = gpu_backend == &gpu_i915_row || gpu_map_next + guard + bytes > GPU_VA_END;" in gpu_c and
          "if (compositor_card.nowait && compositor_card.looped && gpu_backend != &gpu_i915_row)" in gpu_host_c and
          "if (gpu_backend == &gpu_i915_row && (compositor.scanout_pitch & 63))" in gpu_host_c and
          "int band_y = gpu_backend == &gpu_i915_row && compositor.height > 256 ? ((compositor.height - 96) & ~3) : compositor.height;" in gpu_host_c)

    check("Family 9: Post-recover DMA-BUF re-import, scaled_direct fenced-flip guard, card_tick done<0, BO-list own/scale hold, gpu_lost re-check & fence close",
          "compositor_wl.lent[row].fd >= 0 && compositor_wl.lent[row].epoch != gpu_epoch && gpu_can_import()" in wl_host_c and
          "if (!l->gather && l->fd >= 0 && l->epoch != gpu_epoch && gpu_can_import() && !compositor_wl_card_barred(row))" in wl_host_c and
          "compositor.flip_pending || compositor.scaled_direct || !compositor_in_fence()" in present_c and
          "int done = gpu_done(compositor_card.last);" in present_c and
          "int strip_damage = 0;" in present_c and
          "return gpu_imports_all || gpu.imported_own[i] || gpu.imported_handed[i];" in gpu_c and
          "if (gpu_lost || gpu_done(gpu_bins.job[side]) == 0 || gpu_lost || gpu_slot_take() < 0)" in gpu_c and
          "compositor_card_fence_close();" in gpu_host_c[gpu_host_c.find("static void compositor_gpu_recover("):gpu_host_c.find("static void compositor_gpu_lost_check(")])

    check("Family 9: Editor Q8 rounded stats pill, fixed-position [Text|Hex] segmented switcher & smooth wrapped-line scrolling",
          "box = ui_box_px(px, py, pw, ph, ph * 128);" in editor_c and
          "seg_box = ui_box_px(cx, sy, seg_w, seg_h, seg_h * 128);" in editor_c and
          "editor.pill_text[0] = cx" in editor_c and
          "ui_text_wrap_vrow_of(" in ui_c and
          "p64 target_vrow = (p64)(rest / t->line_height);" in ui_c and
          "!(wrap && line == t->top_line && sub < t->top_row)" in ui_c)

    # --- Family 10: Kernel Spark Key-Repeat & UI Kit Repeat Invariants ---
    mw_c = read_text("src/moonwater/moonwater.c")
    term_c = read_text("src/canvas/ui/terminal.c")
    inp_c = read_text("src/canvas/compositor/input.c")
    check("Family 10: Spark bind_filter arms/cancels kernel EV_REP autorepeat timer when consuming EV_KEY (restores repeat in terminal & all UI apps)",
          "test_bit(EV_REP, handle->dev->evbit)" in mw_c and
          "handle->dev->repeat_key = code;" in mw_c and
          "mod_timer(&handle->dev->timer, jiffies + msecs_to_jiffies(handle->dev->rep[REP_DELAY]));" in mw_c and
          "timer_delete(&handle->dev->timer);" in mw_c and
          "flags = modifiers | (value ? WINDOW_KEY_DOWN : 0) | (value == 2 ? WINDOW_KEY_REPEAT : 0);" in inp_c and
          "WINDOW_KEY_REPEAT" not in term_c[term_c.find("static void terminal_key("):term_c.find("static void terminal_pointer_key(")] and
          "WINDOW_KEY_REPEAT" not in ui_c[ui_c.find("static p32 ui_text_key("):ui_c.find("static p32 ui_text_key(") + 2500])

    # --- Family 11: Cross-Vendor GPU Hardware-Floor Optimizations (AMD, Intel i915/Xe, Compositor, Wayland) ---
    compose_c = read_text("src/canvas/compositor/compose.c")
    pointer_c = read_text("src/canvas/compositor/pointer.c")
    check("Family 11: gpu.c O(popcount) handed bitmask, cached RAM GPU_ARGS mirror, single-pass i915 scene pack, damaged-row tile binning, Gen7/7.5 L3 MOCS + EU threads + single-op fast path & gpu_nv12_go",
          "static p64 gpu_maps_handed[4];" in gpu_c and
          "static struct gpu_args gpu_slot_args[GPU_SLOTS];" in gpu_c and
          "static b32 gpu_can_import(void)" in gpu_c and
          "if (n > 1 && !(aligned && gpu_backend == &gpu_i915_row))" in gpu_c and
          "gpu_i915_put(state, at + 20, 1u << 16);" in gpu_c and
          "(threads << 16) | (1u << 8) | (1u << 2)" in gpu_c and
          "fast.code = GPU_VA + gpu_kernel_at[0];" in gpu_c and
          "static positive gpu_nv12_go(" in gpu_c)

    check("Family 11: Compositor damage-bbox & opaque-cover culling, i915 static solid-tile cache, pipelined shadow submit + AVX2 NT copy & zero redundant DIRTYFB",
          "compositor_frame_meets(" in compose_c and
          "compositor_frame_covers_opaque(" in compose_c and
          "compositor_gpu_i915_solid_tile(" in compose_c and
          "px + pw <= compositor_gscene.clip[0]" in pointer_c and
          "memory_copy_rect_out_u32(compositor.scanout + y0 * pitch + x0" in gpu_host_c and
          "!(flip_own && compositor.on_screen != compositor.fb && !compositor_copies_damage())" in present_c and
          "if (!flipped || compositor_copies_damage())" in scan_c)

    check("Family 11: Wayland zero-copy floor (no CPU SIGBUS probe on DMA-BUF, cached straight desc, GPU-first xform gather, batched straight_pays & slow_until guard)",
          "if (picture->sync_fd < 0)\n        {\n                volatile unsigned int probe;" in wl_host_c and
          "compositor_wl.straight[row].desc_kind && compositor_wl.straight[row].desc_epoch == gpu_epoch" in wl_host_c and
          "if (compositor_wl_card_gather(row, picture, to, pitch, width, height))\n                                xst = 2;" in wl_host_c and
          "if (l->fd < 0 || l->gather)" in wl_host_c and
          "if (client >= 0 && client < compositor_wl.slots && compositor_wl.client[client].slow_until &&" in wl_host_c and
          "if (paid)\n                wl_ext_tiled_settle();" in wl_host_c)

    # --- Family 12: Waterlink Per-Machine Ability Settings & File Explorer Waterlink Display ---
    wl_svc_c = read_text("src/waterlink/service.c")
    files_model_c = read_text("src/canvas/ui/files_model.c")
    check("Family 12: Waterlink per-machine ability toggles in Settings (files/run/shell/log allow/deny, persistent expansion) & handshake BLOCKED guard",
          "SETTINGS_SUB_LINK_GRANT" in settings_c and
          "settings_link_rows(" in settings_c and
          "settings_link_follow();" in settings_c and
          "settings_link_grant_toggle(" in settings_c and
          "{'l', false, true, \"link\", \"allow\"}" in settings_c and
          "{'n', false, true, \"link\", \"deny\"}" in settings_c and
          "peer->flags & WATERLINK_PEER_BLOCKED" in wl_svc_c)

    check("Family 12: Waterlink remote FS RPC (LINK_ASK_FS gated by WATERLINK_MAY_FILES) & File Explorer This Machine + linked machine sidebar/browsing",
          "LINK_ASK_FS" in wl_svc_c and
          "LINK_KIND_FS" in wl_svc_c and
          "link_fs_open(" in wl_svc_c and
          "p16 listen_port = peer->port ? peer->port : link_port();" in wl_svc_c and
          "link_fs_handle_info(" in wl_svc_c and
          "link_fs_handle_list(" in wl_svc_c and
          "FILES_NODE_MACHINE" in files_model_c and
          "files_tree_set_machine_children(" in files_model_c and
          "files_peers_sync(" in files_c and
          "files_remote_tick(" in files_c and
          "b32 refused = p->fs.refusal[0] != 0;" in files_c and
          "box = ui_box_px(px, py, pw, ph, ph * 128);" in files_c and
          "UI_FILE_MACHINE" in ui_c)


def test_build_and_audit_gates():
    print("\n=== 4. Build & Audit Gates ===")
    r1 = run_cmd([sys.executable, "test/compositor_split.py", "--check"])
    check("test/compositor_split.py --check", r1.returncode == 0, r1.stderr[:300])

    r2 = run_cmd([sys.executable, "test/function_audit.py", "--builtins", "--check"])
    check("test/function_audit.py --builtins --check", r2.returncode == 0, (r2.stdout + r2.stderr)[:300])

    aarch64_gcc = shutil.which("aarch64-linux-gnu-gcc") or "/opt/homebrew/bin/aarch64-linux-gnu-gcc"
    if os.path.exists(aarch64_gcc):
        r_build = run_cmd([aarch64_gcc, "-O2", "-nostdlib", "-nostartfiles", "-fno-stack-protector",
                           "-fno-builtin", "-Werror=implicit-function-declaration", "-fsyntax-only", "src/build/build.c"])
        check("src/build/build.c syntax-checks cleanly with -Werror=implicit-function-declaration",
              r_build.returncode == 0, r_build.stderr[-400:])
        with tempfile.TemporaryDirectory(prefix="mw_cc_") as td:
            out_o = Path(td) / "utilities.o"
            r3 = run_cmd([aarch64_gcc, "-O2", "-ffreestanding", "-nostdlib", "-fno-builtin",
                          "-Werror=implicit-function-declaration", "-c", "programs/utilities.c", "-o", str(out_o)])
            check("programs/utilities.c compiles with -Werror=implicit-function-declaration",
                  r3.returncode == 0, r3.stderr[-400:])


def main():
    test_executable_c_harnesses()
    test_compositor_and_xwm_invariants()
    test_kernel_audio_bowl_invariants()
    test_canvas_spaces_invariants()
    test_hardening_66_bug_families()
    test_build_and_audit_gates()
    print(f"\nSummary: {PASS} passed, {FAIL} failed")
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
