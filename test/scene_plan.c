/*
        Comprehensive correctness, security (mmap guard-page + NULL + cap saturation),
        randomized differential fuzzing, and performance/size proof for
        src/canvas/compositor/scene_plan.asm.c against -O2 C reference implementations.
*/
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#define _DARWIN_C_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

#define COMPOSITOR_SCENE_PLAN_ASM_IMPLEMENTATION
#include "../src/canvas/compositor/scene_plan.asm.c"

/* ---------- -O2 C reference implementations (non-inlined for nm --print-size & bench) ---------- */
#if defined(__GNUC__) || defined(__clang__)
#define REF_NOINLINE __attribute__((noinline))
#else
#define REF_NOINLINE
#endif

REF_NOINLINE splan_u32 ref_plan_add(struct splan_plan *plan, const struct splan_add *req)
{
        if (!plan || !req || !plan->box || !plan->owner || !plan->edge)
                return 0;
        if (req->x2 <= req->tx1 || req->fx >= req->tx2 || req->y2 <= req->ty1 || req->fy >= req->ty2)
                return 0;
        splan_u32 need = req->radius ? 3u : 1u;
        if (plan->pieces > req->piece_cap || req->piece_cap - plan->pieces < need)
                return 0;
        splan_u32 first = plan->pieces, p = first;
        if (!req->radius) {
                plan->box[p][0] = req->fx; plan->box[p][1] = req->fy;
                plan->box[p][2] = req->x2; plan->box[p][3] = req->y2;
                plan->owner[p++] = req->at;
        } else {
                splan_i32 r = req->radius;
                plan->box[p][0] = req->fx + r; plan->box[p][1] = req->fy;
                plan->box[p][2] = req->x2 - r; plan->box[p][3] = req->y2;
                plan->owner[p++] = req->at;
                plan->box[p][0] = req->fx;     plan->box[p][1] = req->fy + r;
                plan->box[p][2] = req->fx + r; plan->box[p][3] = req->y2 - r;
                plan->owner[p++] = req->at;
                plan->box[p][0] = req->x2 - r; plan->box[p][1] = req->fy + r;
                plan->box[p][2] = req->x2;     plan->box[p][3] = req->y2 - r;
                plan->owner[p++] = req->at;
        }
        plan->pieces = p;
        splan_u32 edges = plan->edges;
        for (splan_u32 piece = first; piece < p; piece++) {
                for (splan_u32 side = 1; side < 4; side += 2) {
                        splan_i32 ey = plan->box[piece][side];
                        if (ey > req->ty1 && ey < req->ty2) {
                                splan_u32 place = edges;
                                while (place && plan->edge[place - 1] > ey) place--;
                                if (place && plan->edge[place - 1] == ey) continue;
                                if (edges >= req->edge_cap) continue;
                                memmove(plan->edge + place + 1, plan->edge + place,
                                        (edges - place) * sizeof(splan_i32));
                                plan->edge[place] = ey;
                                edges++;
                        }
                }
        }
        plan->edges = edges;
        return need;
}

REF_NOINLINE splan_u32 ref_band_edges(const splan_i32 *src, splan_u32 src_count,
                                      splan_i32 y1, splan_i32 y2,
                                      splan_i32 *dst, splan_u32 dst_cap)
{
        if (!dst || dst_cap < 2) return 0;
        splan_u32 out = 0;
        dst[out++] = y1;
        if (src) {
                for (splan_u32 k = 0; k < src_count; k++) {
                        if (src[k] > y1 && src[k] < y2 && out + 1 < dst_cap)
                                dst[out++] = src[k];
                }
        }
        dst[out++] = y2;
        return out;
}

REF_NOINLINE splan_u32 ref_span_subtract(struct splan_sub_req *req)
{
        if (!req || !req->span || !req->vis || !req->vis_cap) return 0;
        splan_u32 spans = req->spans > req->span_cap ? req->span_cap : req->spans;
        splan_u32 s = req->cursor, vc = 0;
        if (req->bx2 <= req->bx0) {
                req->spans = spans;
                req->cursor = spans;
                return 0;
        }
        while (s < spans) {
                splan_i32 low = req->span[s][0], high = req->span[s][1];
                splan_i32 x1 = low > req->bx0 ? low : req->bx0;
                splan_i32 x2 = high < req->bx2 ? high : req->bx2;
                if (x2 <= x1) { s++; continue; }
                if (vc >= req->vis_cap) break;
                req->vis[vc][0] = x1;
                req->vis[vc][1] = x2;
                vc++;
                if (low < x1 && x2 < high) {
                        req->span[s][1] = x1;
                        if (spans < req->span_cap) {
                                req->span[spans][0] = x2;
                                req->span[spans][1] = high;
                                spans++;
                        }
                        s++;
                } else if (low < x1) {
                        req->span[s][1] = x1;
                        s++;
                } else if (x2 < high) {
                        req->span[s][0] = x2;
                        s++;
                } else {
                        spans--;
                        req->span[s][0] = req->span[spans][0];
                        req->span[s][1] = req->span[spans][1];
                }
        }
        req->spans = spans;
        req->cursor = s;
        return vc;
}

REF_NOINLINE splan_i32 ref_corner_occluded(const struct splan_plan *plan, splan_u32 at,
                                           splan_i32 x1, splan_i32 y1, splan_i32 x2, splan_i32 y2)
{
        if (!plan || !plan->box || !plan->owner || !plan->pieces) return 0;
        for (splan_u32 p = 0; p < plan->pieces; p++) {
                if (plan->owner[p] > at &&
                    plan->box[p][0] <= x1 && plan->box[p][1] <= y1 &&
                    plan->box[p][2] >= x2 && plan->box[p][3] >= y2)
                        return 1;
        }
        return 0;
}

REF_NOINLINE void ref_shape_covers(const struct splan_shape *s, splan_i32 px, splan_i32 my,
                                   splan_u32 *ce, splan_u32 *cf)
{
        if (!s || !ce || !cf) return;
        if (s->r <= 0 || my < 0 || my >= s->r) { *ce = 0; *cf = 0; return; }
        splan_i32 rx = s->x + s->w - s->r;
        int right = px >= rx;
        splan_i32 col = right ? px - rx : px - s->x;
        if (col < 0 || col >= s->r) { *ce = 0; *cf = 0; return; }
        const splan_u8 *op = right ? s->outer_right : s->outer;
        if (!op) { *ce = 0; *cf = 0; return; }
        splan_u32 outer = op[my * s->r + col], inner = 0;
        splan_i32 ri = s->r - s->line, iy = my - s->line, ix = right ? col : col - s->line;
        if (ri > 0 && iy >= 0 && iy < ri && ix >= 0 && ix < ri) {
                const splan_u8 *ip = right ? s->inner_right : s->inner;
                if (ip) inner = ip[iy * ri + ix];
        }
        if (inner > outer) inner = outer;
        *ce = ((outer - inner) * (s->edge >> 24) + 127u) / 255u;
        *cf = (inner * (s->fill >> 24) + 127u) / 255u;
}

REF_NOINLINE void ref_tint_clamp(splan_i32 w, splan_i32 h, splan_i32 radius,
                                 splan_i32 edge_line, splan_u32 edge, splan_i32 out[2])
{
        if (!out) return;
        radius = radius > w / 2 ? w / 2 : radius;
        radius = radius > h / 2 ? h / 2 : radius;
        radius = radius > SPLAN_RADIUS_MOST ? SPLAN_RADIUS_MOST : radius < 0 ? 0 : radius;
        edge_line = edge_line < 0 || !(edge >> 24) ? 0 : edge_line > radius && radius ? radius : edge_line;
        out[0] = radius;
        out[1] = edge_line;
}

REF_NOINLINE splan_i32 ref_tint_decompose(const splan_i32 rect[4], splan_u32 ink,
                                          splan_i32 radius, splan_i32 edge_line, splan_u32 edge,
                                          struct splan_tint_plan *out)
{
        if (!rect || !out) return 0;
        splan_i32 x = rect[0], y = rect[1], w = rect[2], h = rect[3], cl[2];
        if (w <= 0 || h <= 0) {
                out->r = 0; out->line = 0; out->corner_count = 0; out->rect_count = 0;
                return 1;
        }
        ref_tint_clamp(w, h, radius, edge_line, edge, cl);
        splan_i32 r = cl[0], line = cl[1];
        out->r = r; out->line = line;
        if (r > 0) {
                out->corner_count = 4;
                for (int c = 0; c < 4; c++) {
                        out->corner_xy[c][0] = (c & 1) ? x + w - r : x;
                        out->corner_xy[c][1] = (c & 2) ? y + h - r : y;
                }
        } else {
                out->corner_count = 0;
        }
        splan_u32 rc = 0;
        splan_i32 lx = line > w ? w : line;
        for (splan_i32 py = y; py < y + h;) {
                splan_i32 top = py - y, bot = y + h - 1 - py, my = top < bot ? top : bot;
                int corner = my < r, edge_row = my < line;
                splan_i32 from = corner ? x + r : x + lx;
                splan_i32 to = corner ? x + w - r : x + w - lx;
                splan_i32 stop = py + 1;
                for (; stop < y + h; stop++) {
                        splan_i32 t = stop - y, b = y + h - 1 - stop, m = t < b ? t : b;
                        if ((m < r) != corner || (m < line) != edge_row) break;
                }
                if (to > from) {
                        out->rect[rc].x0 = from; out->rect[rc].y0 = py;
                        out->rect[rc].x1 = to;   out->rect[rc].y1 = stop;
                        out->rect[rc].argb = edge_row ? edge : ink;
                        out->rect[rc].is_edge = edge_row;
                        rc++;
                }
                if (!corner && line > 0) {
                        splan_i32 left_to = x + lx;
                        splan_i32 right_from = x + w - lx > left_to ? x + w - lx : left_to;
                        if (left_to > x) {
                                out->rect[rc].x0 = x; out->rect[rc].y0 = py;
                                out->rect[rc].x1 = left_to; out->rect[rc].y1 = stop;
                                out->rect[rc].argb = edge; out->rect[rc].is_edge = 1;
                                rc++;
                        }
                        if (x + w > right_from) {
                                out->rect[rc].x0 = right_from; out->rect[rc].y0 = py;
                                out->rect[rc].x1 = x + w; out->rect[rc].y1 = stop;
                                out->rect[rc].argb = edge; out->rect[rc].is_edge = 1;
                                rc++;
                        }
                }
                py = stop;
        }
        out->rect_count = rc;
        return 1;
}

REF_NOINLINE void ref_cpu_tint_cut(const splan_i32 owner_rect[4], splan_i32 ri,
                                   splan_i32 py, splan_i32 x2, splan_i32 out[2])
{
        if (!out) return;
        out[0] = out[1] = x2;
        if (owner_rect && py >= owner_rect[1] && py < owner_rect[1] + owner_rect[3]) {
                int inside = py >= owner_rect[1] + ri && py < owner_rect[1] + owner_rect[3] - ri;
                splan_i32 hf = inside ? owner_rect[0] : owner_rect[0] + ri;
                splan_i32 ht = inside ? owner_rect[0] + owner_rect[2] : owner_rect[0] + owner_rect[2] - ri;
                if (ht > hf) { out[0] = hf; out[1] = ht; }
        }
}

/* ---------- Test helpers ---------- */
static uint64_t rng_state = 0x9e3779b97f4a7c15ULL;
static uint32_t rng_u32(void)
{
        uint64_t x = rng_state;
        x ^= x << 13; x ^= x >> 7; x ^= x << 17;
        rng_state = x;
        return (uint32_t)(x >> 16);
}
static int32_t rng_range(int32_t lo, int32_t hi)
{
        return lo + (int32_t)(rng_u32() % (uint32_t)(hi - lo + 1));
}

static uint64_t now_ns(void)
{
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

#define CHECK(cond, msg) do { if (!(cond)) { \
        fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg); exit(1); } } while (0)

/* Guarded allocation: [PROT_NONE][RW][PROT_NONE], buffer placed flush at either start or end */
struct guarded_buf {
        void  *base;
        size_t total;
        void  *ptr;
};

static struct guarded_buf alloc_guarded(size_t bytes, int flush_end)
{
        size_t pg = (size_t)sysconf(_SC_PAGESIZE);
        size_t mid = ((bytes + pg - 1) / pg) * pg;
        if (mid == 0) mid = pg;
        size_t total = mid + 2 * pg;
        char *base = (char *)mmap(NULL, total, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        CHECK(base != MAP_FAILED, "mmap failed");
        CHECK(mprotect(base + pg, mid, PROT_READ | PROT_WRITE) == 0, "mprotect failed");
        struct guarded_buf g = { base, total, flush_end ? (base + pg + mid - bytes) : (base + pg) };
        return g;
}

static void free_guarded(struct guarded_buf g)
{
        munmap(g.base, g.total);
}

/* ---------- 1. Edge cases + security / NULL / saturation tests ---------- */
static void test_edge_and_security(void)
{
        /* NULL pointer safety for all 8 functions */
        splan_i32 dummy2[2] = { -1, -1 };
        splan_u32 ce = 99, cf = 99;
        struct splan_plan empty_plan = {0};
        struct splan_add req = {0};
        struct splan_sub_req sreq = {0};
        struct splan_tint_plan tp;

        CHECK(splan_plan_add(NULL, &req) == 0, "plan_add NULL plan");
        CHECK(splan_plan_add(&empty_plan, NULL) == 0, "plan_add NULL req");
        CHECK(splan_plan_add(&empty_plan, &req) == 0, "plan_add NULL arrays");

        CHECK(splan_band_edges(NULL, 10, 0, 100, NULL, 10) == 0, "band_edges NULL dst");
        CHECK(splan_band_edges(NULL, 10, 0, 100, dummy2, 1) == 0, "band_edges cap<2");
        CHECK(splan_band_edges(NULL, 10, 5, 50, dummy2, 2) == 2 && dummy2[0] == 5 && dummy2[1] == 50,
              "band_edges NULL src");

        CHECK(splan_span_subtract(NULL) == 0, "span_subtract NULL");
        CHECK(splan_span_subtract(&sreq) == 0, "span_subtract NULL arrays");

        CHECK(splan_corner_occluded(NULL, 0, 0, 0, 10, 10) == 0, "corner_occluded NULL plan");
        CHECK(splan_corner_occluded(&empty_plan, 0, 0, 0, 10, 10) == 0, "corner_occluded empty");

        splan_shape_covers(NULL, 0, 0, &ce, &cf);
        struct splan_shape bad_s = {0};
        splan_shape_covers(&bad_s, 0, 0, &ce, &cf);
        CHECK(ce == 0 && cf == 0, "shape_covers zero radius");
        splan_shape_covers(&bad_s, 0, 0, NULL, &cf);
        splan_shape_covers(&bad_s, 0, 0, &ce, NULL);

        splan_tint_clamp(100, 100, 20, 4, 0xff000000u, NULL);
        CHECK(splan_tint_decompose(NULL, 0, 0, 0, 0, &tp) == 0, "tint_decompose NULL rect");
        splan_i32 zero_rect[4] = { 10, 20, 0, 50 };
        CHECK(splan_tint_decompose(zero_rect, 0xff112233u, 8, 2, 0xffffffffu, NULL) == 0, "tint_decompose NULL out");
        CHECK(splan_tint_decompose(zero_rect, 0xff112233u, 8, 2, 0xffffffffu, &tp) == 1 &&
              tp.corner_count == 0 && tp.rect_count == 0, "tint_decompose zero w");

        splan_cpu_tint_cut(NULL, 8, 10, 200, NULL);
        splan_cpu_tint_cut(NULL, 8, 10, 200, dummy2);
        CHECK(dummy2[0] == 200 && dummy2[1] == 200, "cpu_tint_cut NULL owner");

        /* Piece cap saturation & edge cap saturation */
        splan_i32 box[3][4], edge[4];
        splan_u32 owner[3];
        struct splan_plan p = { box, owner, 0, edge, 2 };
        edge[0] = 0; edge[1] = 1000;
        req = (struct splan_add){ 10, 100, 200, 300, 0, 0, 1000, 1000, 12, 7, 2, 4 };
        CHECK(splan_plan_add(&p, &req) == 0 && p.pieces == 0, "round piece cap saturation (need 3, cap 2)");
        req.piece_cap = 3;
        CHECK(splan_plan_add(&p, &req) == 3 && p.pieces == 3 && p.edges == 4, "edge cap saturation at 4");
        /* Duplicate Y-edges should not grow edges */
        splan_i32 box2[6][4], edge2[16];
        splan_u32 owner2[6];
        struct splan_plan p2 = { box2, owner2, 0, edge2, 2 };
        edge2[0] = 0; edge2[1] = 1000;
        req = (struct splan_add){ 10, 100, 200, 300, 0, 0, 1000, 1000, 12, 1, 6, 16 };
        CHECK(splan_plan_add(&p2, &req) == 3 && p2.edges == 6, "first round frame -> 6 edges");
        req.at = 2;
        CHECK(splan_plan_add(&p2, &req) == 3 && p2.edges == 6, "duplicate Y-edges -> still 6 edges");

        /* Span cap saturation + resumable vis_cap = 1 chunking */
        splan_i32 sp[2][2] = { {0, 100}, {0, 0} };
        splan_i32 vis[1][2];
        sreq = (struct splan_sub_req){ sp, 1, 2, 20, 40, vis, 1, 0 };
        CHECK(splan_span_subtract(&sreq) == 1 && sreq.spans == 2 && vis[0][0] == 20 && vis[0][1] == 40,
              "split within cap");
        /* Now spans == 2 == span_cap; splitting [40, 100] with [50, 60] must not overflow span_cap */
        sreq.bx0 = 50; sreq.bx2 = 60; sreq.cursor = 0;
        CHECK(splan_span_subtract(&sreq) == 1 && sreq.spans == 2 && vis[0][0] == 50 && vis[0][1] == 60,
              "split saturated at span_cap");

        /* Negative & extreme coordinates in tint_clamp & tint_decompose */
        splan_i32 cl_a[2], cl_r[2];
        int32_t weird_vals[] = { INT32_MIN, -1000, -3, -1, 0, 1, 2, 63, 64, 65, 1000, INT32_MAX };
        for (size_t i = 0; i < sizeof(weird_vals)/sizeof(weird_vals[0]); i++) {
                for (size_t j = 0; j < sizeof(weird_vals)/sizeof(weird_vals[0]); j++) {
                        splan_tint_clamp(weird_vals[i], weird_vals[j], weird_vals[i], weird_vals[j], 0xff000000u, cl_a);
                        ref_tint_clamp(weird_vals[i], weird_vals[j], weird_vals[i], weird_vals[j], 0xff000000u, cl_r);
                        CHECK(cl_a[0] == cl_r[0] && cl_a[1] == cl_r[1], "tint_clamp extreme mismatch");
                }
        }
        /* Huge edge_line with r == 0 (overflow resistance) */
        splan_i32 rbig[4] = { -500, -400, 120, 80 };
        struct splan_tint_plan tp_a, tp_r;
        CHECK(splan_tint_decompose(rbig, 0x80112233u, 0, INT32_MAX, 0xffaabbccu, &tp_a) == 1, "tint huge line asm");
        CHECK(ref_tint_decompose(rbig, 0x80112233u, 0, INT32_MAX, 0xffaabbccu, &tp_r) == 1, "tint huge line ref");
        CHECK(tp_a.r == tp_r.r && tp_a.line == tp_r.line &&
              tp_a.corner_count == tp_r.corner_count && tp_a.rect_count == tp_r.rect_count &&
              memcmp(tp_a.rect, tp_r.rect, tp_a.rect_count * sizeof(tp_a.rect[0])) == 0,
              "tint_decompose huge edge_line parity");
}

/* ---------- 2. mmap PROT_NONE guard-page tests (both flush-start and flush-end) ---------- */
static void test_mmap_guard_pages(void)
{
        for (int flush_end = 0; flush_end <= 1; flush_end++) {
                const splan_u32 P = 15, E = 32, S = 16;
                struct guarded_buf g_box   = alloc_guarded(P * sizeof(splan_i32[4]), flush_end);
                struct guarded_buf g_owner = alloc_guarded(P * sizeof(splan_u32), flush_end);
                struct guarded_buf g_edge  = alloc_guarded(E * sizeof(splan_i32), flush_end);
                struct guarded_buf g_be    = alloc_guarded((E + 2) * sizeof(splan_i32), flush_end);
                struct guarded_buf g_span  = alloc_guarded(S * sizeof(splan_i32[2]), flush_end);
                struct guarded_buf g_vis   = alloc_guarded(4 * sizeof(splan_i32[2]), flush_end);
                struct guarded_buf g_mask  = alloc_guarded(16 * 16, flush_end);
                struct guarded_buf g_imask = alloc_guarded(12 * 12, flush_end);

                struct splan_plan plan = {
                        (splan_i32 (*)[4])g_box.ptr, (splan_u32 *)g_owner.ptr, 0,
                        (splan_i32 *)g_edge.ptr, 2
                };
                plan.edge[0] = 0; plan.edge[1] = 1080;

                for (splan_u32 f = 0; f < 8; f++) {
                        struct splan_add req = {
                                (splan_i32)(f * 60 - 20), (splan_i32)(f * 80 + 10),
                                (splan_i32)(f * 60 + 300), (splan_i32)(f * 80 + 260),
                                0, 0, 1920, 1080, (f & 1) ? 16 : 0, f, P, E
                        };
                        splan_plan_add(&plan, &req);
                }
                CHECK(plan.pieces <= P && plan.edges <= E, "guarded plan bounds");

                splan_u32 ne = splan_band_edges(plan.edge, plan.edges, 100, 900, (splan_i32 *)g_be.ptr, E + 2);
                CHECK(ne >= 2 && ne <= E + 2, "guarded band_edges");

                splan_i32 (*sp)[2] = (splan_i32 (*)[2])g_span.ptr;
                sp[0][0] = 0; sp[0][1] = 1920;
                splan_u32 spans = 1;
                for (splan_u32 at = plan.pieces; at-- && spans;) {
                        struct splan_sub_req sr = { sp, spans, S, plan.box[at][0], plan.box[at][2],
                                                    (splan_i32 (*)[2])g_vis.ptr, 4, 0 };
                        while (sr.cursor < sr.spans) {
                                splan_u32 nv = splan_span_subtract(&sr);
                                if (!nv) break;
                        }
                        spans = sr.spans;
                }

                (void)splan_corner_occluded(&plan, 0, 100, 100, 116, 116);

                memset(g_mask.ptr, 200, 16 * 16);
                memset(g_imask.ptr, 120, 12 * 12);
                struct splan_shape sh = { 10, 20, 100, 80, 16, 4, 0xcc112233u, 0xff445566u,
                                          (const splan_u8 *)g_mask.ptr, (const splan_u8 *)g_mask.ptr,
                                          (const splan_u8 *)g_imask.ptr, (const splan_u8 *)g_imask.ptr };
                splan_u32 ce = 0, cf = 0;
                for (int py = -1; py <= 16; py++) {
                        splan_shape_covers(&sh, 10, py, &ce, &cf);
                        splan_shape_covers(&sh, 109, py, &ce, &cf);
                        splan_shape_covers(&sh, 9, py, &ce, &cf);
                        splan_shape_covers(&sh, 110, py, &ce, &cf);
                }

                free_guarded(g_box); free_guarded(g_owner); free_guarded(g_edge);
                free_guarded(g_be);  free_guarded(g_span);  free_guarded(g_vis);
                free_guarded(g_mask); free_guarded(g_imask);
        }
}

/* ---------- 3. Randomized differential fuzzing against -O2 C reference ---------- */
static void test_fuzz_differential(void)
{
        /* A. Fuzz plan_add + band_edges + span_subtract + corner_occluded end-to-end */
        for (int trial = 0; trial < 2000; trial++) {
                splan_i32 tx1 = rng_range(-200, 200), ty1 = rng_range(-200, 200);
                splan_i32 tx2 = tx1 + rng_range(1, 1200), ty2 = ty1 + rng_range(1, 900);
                splan_u32 nframes = (splan_u32)rng_range(0, 24);
                splan_u32 pcap = (splan_u32)rng_range(0, 72);
                splan_u32 ecap = (splan_u32)rng_range(2, 160);

                splan_i32 box_a[72][4], box_r[72][4], edge_a[160], edge_r[160];
                splan_u32 own_a[72], own_r[72];
                memset(box_a, 0x5a, sizeof(box_a)); memset(box_r, 0x5a, sizeof(box_r));
                memset(own_a, 0x5a, sizeof(own_a)); memset(own_r, 0x5a, sizeof(own_r));
                memset(edge_a, 0x5a, sizeof(edge_a)); memset(edge_r, 0x5a, sizeof(edge_r));

                struct splan_plan pa = { box_a, own_a, 0, edge_a, 2 };
                struct splan_plan pr = { box_r, own_r, 0, edge_r, 2 };
                edge_a[0] = edge_r[0] = ty1;
                edge_a[1] = edge_r[1] = ty2;

                for (splan_u32 f = 0; f < nframes; f++) {
                        splan_i32 fx = rng_range(tx1 - 100, tx2 + 100);
                        splan_i32 fy = rng_range(ty1 - 100, ty2 + 100);
                        splan_i32 fw = rng_range(1, 400), fh = rng_range(1, 400);
                        splan_i32 r = (rng_u32() & 1) ? rng_range(0, 48) : 0;
                        if (r > fw / 2) r = fw / 2;
                        if (r > fh / 2) r = fh / 2;
                        struct splan_add req = { fx, fy, fx + fw, fy + fh, tx1, ty1, tx2, ty2, r, f, pcap, ecap };
                        splan_u32 ra = splan_plan_add(&pa, &req);
                        splan_u32 rr = ref_plan_add(&pr, &req);
                        CHECK(ra == rr, "fuzz plan_add ret");
                }
                CHECK(pa.pieces == pr.pieces && pa.edges == pr.edges, "fuzz plan counts");
                if (pa.pieces) {
                        CHECK(memcmp(box_a, box_r, pa.pieces * sizeof(box_a[0])) == 0, "fuzz plan boxes");
                        CHECK(memcmp(own_a, own_r, pa.pieces * sizeof(own_a[0])) == 0, "fuzz plan owners");
                }
                CHECK(memcmp(edge_a, edge_r, pa.edges * sizeof(edge_a[0])) == 0, "fuzz plan edges");

                /* Band edge filter */
                splan_i32 by1 = rng_range(ty1 - 50, ty2), by2 = by1 + rng_range(0, 500);
                splan_u32 bcap = (splan_u32)rng_range(0, 162);
                splan_i32 be_a[162], be_r[162];
                splan_u32 nba = splan_band_edges(pa.edge, pa.edges, by1, by2, be_a, bcap);
                splan_u32 nbr = ref_band_edges(pr.edge, pr.edges, by1, by2, be_r, bcap);
                CHECK(nba == nbr, "fuzz band_edges count");
                if (nba) CHECK(memcmp(be_a, be_r, nba * sizeof(splan_i32)) == 0, "fuzz band_edges values");

                /* Span subtraction across bands */
                for (splan_u32 b = 0; b + 1 < nba && b < 8; b++) {
                        splan_i32 top = be_a[b], bot = be_a[b + 1];
                        if (bot <= top) continue;
                        splan_i32 sp_a[80][2], sp_r[80][2], vis_a[8][2], vis_r[8][2];
                        sp_a[0][0] = sp_r[0][0] = tx1;
                        sp_a[0][1] = sp_r[0][1] = tx2;
                        splan_u32 sa = 1, sr_n = 1;
                        splan_u32 scap = (splan_u32)rng_range(1, 80);
                        splan_u32 vcap = (splan_u32)rng_range(1, 8);
                        for (splan_u32 at = pa.pieces; at-- && sa;) {
                                if (pa.box[at][1] >= bot || pa.box[at][3] <= top || pa.box[at][2] <= pa.box[at][0])
                                        continue;
                                struct splan_sub_req qa = { sp_a, sa, scap, pa.box[at][0], pa.box[at][2], vis_a, vcap, 0 };
                                struct splan_sub_req qr = { sp_r, sr_n, scap, pr.box[at][0], pr.box[at][2], vis_r, vcap, 0 };
                                while (qa.cursor < qa.spans || qr.cursor < qr.spans) {
                                        splan_u32 na = splan_span_subtract(&qa);
                                        splan_u32 nr = ref_span_subtract(&qr);
                                        CHECK(na == nr && qa.spans == qr.spans && qa.cursor == qr.cursor,
                                              "fuzz span_subtract state");
                                        if (na) CHECK(memcmp(vis_a, vis_r, na * sizeof(vis_a[0])) == 0, "fuzz span_subtract vis");
                                        if (!na) break;
                                }
                                sa = qa.spans; sr_n = qr.spans;
                                if (sa) CHECK(memcmp(sp_a, sp_r, sa * sizeof(sp_a[0])) == 0, "fuzz span_subtract spans");
                        }
                }

                /* Corner occlusion queries */
                for (int q = 0; q < 8; q++) {
                        splan_u32 at = (splan_u32)rng_range(0, 25);
                        splan_i32 cx1 = rng_range(tx1, tx2), cy1 = rng_range(ty1, ty2);
                        splan_i32 cr = rng_range(1, 40);
                        CHECK(splan_corner_occluded(&pa, at, cx1, cy1, cx1 + cr, cy1 + cr) ==
                              ref_corner_occluded(&pr, at, cx1, cy1, cx1 + cr, cy1 + cr),
                              "fuzz corner_occluded");
                }
        }

        /* B. Fuzz shape_covers */
        splan_u8 outer[64 * 64], outer_r[64 * 64], inner[64 * 64], inner_r[64 * 64];
        for (int i = 0; i < 64 * 64; i++) {
                outer[i] = (splan_u8)rng_u32(); outer_r[i] = (splan_u8)rng_u32();
                inner[i] = (splan_u8)rng_u32(); inner_r[i] = (splan_u8)rng_u32();
        }
        for (int trial = 0; trial < 5000; trial++) {
                splan_i32 r = rng_range(-2, 64), line = rng_range(-2, 70);
                splan_i32 x = rng_range(-100, 500), w = rng_range(1, 400);
                struct splan_shape sh = {
                        x, rng_range(-100, 500), w, rng_range(1, 400), r, line,
                        rng_u32(), rng_u32(),
                        (rng_u32() & 31) ? outer : NULL, (rng_u32() & 31) ? outer_r : NULL,
                        (rng_u32() & 31) ? inner : NULL, (rng_u32() & 31) ? inner_r : NULL
                };
                splan_i32 px = (rng_u32() & 1) ? rng_range(x - 2, x + (r > 0 ? r : 4) + 2)
                                               : rng_range(x + w - (r > 0 ? r : 4) - 2, x + w + 2);
                splan_i32 my = rng_range(-2, (r > 0 ? r : 4) + 2);
                splan_u32 cea = 0xdead, cfa = 0xbeef, cer = 0xdead, cfr = 0xbeef;
                splan_shape_covers(&sh, px, my, &cea, &cfa);
                ref_shape_covers(&sh, px, my, &cer, &cfr);
                CHECK(cea == cer && cfa == cfr, "fuzz shape_covers");
        }

        /* C. Fuzz tint_clamp, tint_decompose, cpu_tint_cut */
        for (int trial = 0; trial < 5000; trial++) {
                splan_i32 rect[4] = { rng_range(-500, 500), rng_range(-500, 500),
                                      rng_range(-5, 300), rng_range(-5, 300) };
                splan_i32 rad = rng_range(-10, 100), el = rng_range(-10, 120);
                splan_u32 ink = rng_u32(), edge = (rng_u32() & 7) ? rng_u32() : (rng_u32() & 0x00ffffffu);
                splan_i32 cla[2], clr[2];
                splan_tint_clamp(rect[2], rect[3], rad, el, edge, cla);
                ref_tint_clamp(rect[2], rect[3], rad, el, edge, clr);
                CHECK(cla[0] == clr[0] && cla[1] == clr[1], "fuzz tint_clamp");

                struct splan_tint_plan ta, tr;
                memset(&ta, 0xaa, sizeof(ta)); memset(&tr, 0xaa, sizeof(tr));
                CHECK(splan_tint_decompose(rect, ink, rad, el, edge, &ta) ==
                      ref_tint_decompose(rect, ink, rad, el, edge, &tr), "fuzz tint_decompose ret");
                CHECK(ta.r == tr.r && ta.line == tr.line &&
                      ta.corner_count == tr.corner_count && ta.rect_count == tr.rect_count,
                      "fuzz tint_decompose header");
                if (ta.corner_count)
                        CHECK(memcmp(ta.corner_xy, tr.corner_xy, sizeof(ta.corner_xy)) == 0, "fuzz tint corners");
                if (ta.rect_count)
                        CHECK(memcmp(ta.rect, tr.rect, ta.rect_count * sizeof(ta.rect[0])) == 0, "fuzz tint rects");

                splan_i32 own[4] = { rng_range(-200, 200), rng_range(-200, 200),
                                     rng_range(0, 300), rng_range(0, 300) };
                splan_i32 ri = rng_range(0, 64), py = rng_range(-220, 520), x2 = rng_range(-300, 600);
                splan_i32 ca[2], cr[2];
                splan_cpu_tint_cut(own, ri, py, x2, ca);
                ref_cpu_tint_cut(own, ri, py, x2, cr);
                CHECK(ca[0] == cr[0] && ca[1] == cr[1], "fuzz cpu_tint_cut");
        }
}

/* ---------- 4. Benchmark: ASM vs -O2 C reference ---------- */
static volatile uint64_t bench_sink = 0;

static void run_benchmarks(void)
{
        const int ITERS = 200000;
        uint64_t t0, t1, asm_ns, ref_ns;

        /* Bench 1: plan_add (12 mixed square/rounded windows) */
        struct splan_add reqs[12];
        for (int i = 0; i < 12; i++) {
                reqs[i] = (struct splan_add){
                        i * 110, i * 65, i * 110 + 480, i * 65 + 320,
                        0, 0, 1920, 1080, (i & 1) ? 16 : 0, (splan_u32)i, 36, 74
                };
        }
        splan_i32 box[36][4], edge[74];
        splan_u32 own[36];
        struct splan_plan pl = { box, own, 0, edge, 2 };

        t0 = now_ns();
        for (int it = 0; it < ITERS; it++) {
                pl.pieces = 0; pl.edges = 2; edge[0] = 0; edge[1] = 1080;
                for (int i = 0; i < 12; i++) bench_sink += splan_plan_add(&pl, &reqs[i]);
        }
        t1 = now_ns(); asm_ns = t1 - t0;

        t0 = now_ns();
        for (int it = 0; it < ITERS; it++) {
                pl.pieces = 0; pl.edges = 2; edge[0] = 0; edge[1] = 1080;
                for (int i = 0; i < 12; i++) bench_sink += ref_plan_add(&pl, &reqs[i]);
        }
        t1 = now_ns(); ref_ns = t1 - t0;
        printf("  plan_add (12 win)  : ASM %6.1f ns/op | C -O2 %6.1f ns/op | speedup %.2fx\n",
               (double)asm_ns / ITERS, (double)ref_ns / ITERS, (double)ref_ns / (double)asm_ns);

        /* Bench 2: span_subtract (30 pieces across 1 band) */
        splan_i32 sp[40][2], vis[64][2];
        t0 = now_ns();
        for (int it = 0; it < ITERS; it++) {
                sp[0][0] = 0; sp[0][1] = 1920;
                splan_u32 spans = 1;
                for (splan_u32 at = pl.pieces; at-- && spans;) {
                        struct splan_sub_req sr = { sp, spans, 40, box[at][0], box[at][2], vis, 64, 0 };
                        bench_sink += splan_span_subtract(&sr);
                        spans = sr.spans;
                }
        }
        t1 = now_ns(); asm_ns = t1 - t0;

        t0 = now_ns();
        for (int it = 0; it < ITERS; it++) {
                sp[0][0] = 0; sp[0][1] = 1920;
                splan_u32 spans = 1;
                for (splan_u32 at = pl.pieces; at-- && spans;) {
                        struct splan_sub_req sr = { sp, spans, 40, box[at][0], box[at][2], vis, 64, 0 };
                        bench_sink += ref_span_subtract(&sr);
                        spans = sr.spans;
                }
        }
        t1 = now_ns(); ref_ns = t1 - t0;
        printf("  span_subtract band : ASM %6.1f ns/op | C -O2 %6.1f ns/op | speedup %.2fx\n",
               (double)asm_ns / ITERS, (double)ref_ns / ITERS, (double)ref_ns / (double)asm_ns);

        /* Bench 3: corner_occluded */
        t0 = now_ns();
        for (int it = 0; it < ITERS * 4; it++)
                bench_sink += (uint64_t)splan_corner_occluded(&pl, 0, 120 + (it & 7), 120, 136 + (it & 7), 136);
        t1 = now_ns(); asm_ns = t1 - t0;

        t0 = now_ns();
        for (int it = 0; it < ITERS * 4; it++)
                bench_sink += (uint64_t)ref_corner_occluded(&pl, 0, 120 + (it & 7), 120, 136 + (it & 7), 136);
        t1 = now_ns(); ref_ns = t1 - t0;
        printf("  corner_occluded    : ASM %6.1f ns/op | C -O2 %6.1f ns/op | speedup %.2fx\n",
               (double)asm_ns / (ITERS * 4), (double)ref_ns / (ITERS * 4), (double)ref_ns / (double)asm_ns);

        /* Bench 4: shape_covers (div-free /255) */
        splan_u8 om[16 * 16], im[12 * 12];
        memset(om, 210, sizeof(om)); memset(im, 140, sizeof(im));
        struct splan_shape sh = { 10, 20, 400, 300, 16, 4, 0x90112233u, 0xff445566u, om, om, im, im };
        splan_u32 ce = 0, cf = 0;
        t0 = now_ns();
        for (int it = 0; it < ITERS * 8; it++) {
                splan_shape_covers(&sh, 10 + (it & 15), (it >> 4) & 15, &ce, &cf);
                bench_sink += ce + cf;
        }
        t1 = now_ns(); asm_ns = t1 - t0;

        t0 = now_ns();
        for (int it = 0; it < ITERS * 8; it++) {
                ref_shape_covers(&sh, 10 + (it & 15), (it >> 4) & 15, &ce, &cf);
                bench_sink += ce + cf;
        }
        t1 = now_ns(); ref_ns = t1 - t0;
        printf("  shape_covers       : ASM %6.1f ns/op | C -O2 %6.1f ns/op | speedup %.2fx\n",
               (double)asm_ns / (ITERS * 8), (double)ref_ns / (ITERS * 8), (double)ref_ns / (double)asm_ns);

        /* Bench 5: tint_decompose (960x540 snap preview: O(1) vs O(h)) */
        splan_i32 rect[4] = { 20, 20, 960, 540 };
        struct splan_tint_plan tp;
        t0 = now_ns();
        for (int it = 0; it < ITERS; it++) {
                splan_tint_decompose(rect, 0x402266ccu, 16, 2, 0xff4488ffu, &tp);
                bench_sink += tp.rect_count;
        }
        t1 = now_ns(); asm_ns = t1 - t0;

        t0 = now_ns();
        for (int it = 0; it < ITERS; it++) {
                ref_tint_decompose(rect, 0x402266ccu, 16, 2, 0xff4488ffu, &tp);
                bench_sink += tp.rect_count;
        }
        t1 = now_ns(); ref_ns = t1 - t0;
        printf("  tint_decompose     : ASM %6.1f ns/op | C -O2 %6.1f ns/op | speedup %.2fx\n",
               (double)asm_ns / ITERS, (double)ref_ns / ITERS, (double)ref_ns / (double)asm_ns);
}

int main(void)
{
        test_edge_and_security();
        test_mmap_guard_pages();
        test_fuzz_differential();
        printf("ALL scene_plan.asm.c TESTS PASSED (edge + security + mmap guard + 12,000 fuzz trials)\n");
#if !defined(__SANITIZE_ADDRESS__)
        run_benchmarks();
#endif
        return 0;
}
