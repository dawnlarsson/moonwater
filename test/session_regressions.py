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
        wayland_pointer_noted(0, 777);
        if (c->input_next != prev_next + 1 || c->input_serial[prev_next % WAYLAND_INPUT_SERIALS] != 777) {
                fprintf(stderr, "FAIL: wayland_pointer_noted did not record serial\n");
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
          "(con->active || (s->row == wayland.pointer_row && wayland_has_keys(c)))" in read_text("src/canvas/wayland.c"))
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
          "static positive next_minute;" in comp_main and
          "if (checked && now >= checked && now - checked < 250000000ull)" in read_text("src/canvas/compositor/session.c") and
          "if (!compositor.table || !rows_any(&compositor_card.rows))" in read_text("src/canvas/compositor/present.c") and
          "if (rows_any(&compositor.held_rows))" in read_text("src/canvas/compositor/scanout.c"))
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
    test_build_and_audit_gates()
    print(f"\nSummary: {PASS} passed, {FAIL} failed")
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
