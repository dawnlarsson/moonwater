#define _DEFAULT_SOURCE
#define _DARWIN_C_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>

#define CANVAS_WAYLAND_DIRECT_ASM_IMPLEMENTATION
#include "../src/canvas/wayland_direct.asm.c"

#define NOINLINE __attribute__((noinline))

/* ---- -O2 C reference implementations (exact copies of wayland.c & wayland_host.c) ---- */

NOINLINE int64_t ref_div120(int64_t v, int64_t pb, int64_t nb)
{
        return v >= 0 ? (v + pb) / 120 : -((-v + nb) / 120);
}

NOINLINE int64_t ref_pixels(int64_t u, int64_t s)
{
        return ref_div120(u * s, 60, 60);
}

NOINLINE int64_t ref_pixels_floor(int64_t u, int64_t s)
{
        return ref_div120(u * s, 0, 119);
}

NOINLINE int64_t ref_pixels_ceil(int64_t u, int64_t s)
{
        return ref_div120(u * s, 119, 0);
}

NOINLINE int64_t ref_units(int64_t p, int64_t s)
{
        return s == 120 || s <= 0 ? p : p * 120 / s;
}

NOINLINE uint64_t ref_units_fixed(int64_t p, int64_t s)
{
        return (uint64_t)(s == 120 || s <= 0 ? p * 256 : p * 256 * 120 / s);
}

NOINLINE void ref_cover(const struct wl_direct_rect *in, int64_t s, struct wl_direct_rect *out)
{
        if (!in || !out) return;
        int64_t rx = in->x, ry = in->y, rw = in->width, rh = in->height;
        int64_t x1 = ref_pixels_floor(rx, s), y1 = ref_pixels_floor(ry, s);
        out->x = x1;
        out->y = y1;
        out->width = ref_pixels_ceil(rx + rw, s) - x1;
        out->height = ref_pixels_ceil(ry + rh, s) - y1;
}

NOINLINE int64_t ref_text(char *into, uint64_t cap, const uint8_t *text, int64_t length)
{
        int64_t at = 0, used = 0;
        if (!into || cap == 0) return 0;
        if (!text || length <= 0) { into[0] = 0; return 0; }
        while (at < length && (uint64_t)used < cap - 1)
        {
                unsigned char c = text[at];
                int64_t want = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : (c & 0xf8) == 0xf0 ? 4 : 0;
                int good = want && at + want <= length && (uint64_t)used + (uint64_t)want < cap;
                for (int64_t more = 1; good && more < want; more++)
                        good = (text[at + more] & 0xc0) == 0x80;
                if (good && ((want == 2 && c < 0xc2) ||
                             (want == 3 && ((c == 0xe0 && text[at + 1] < 0xa0) || (c == 0xed && text[at + 1] >= 0xa0))) ||
                             (want == 4 && ((c == 0xf0 && text[at + 1] < 0x90) || c > 0xf4 || (c == 0xf4 && text[at + 1] >= 0x90)))))
                        good = 0;
                if (want == 1 && (c < 0x20 || c == 0x7f))
                        good = 0;
                if (!good)
                {
                        into[used++] = '?';
                        at++;
                        continue;
                }
                for (int64_t more = 0; more < want; more++)
                        into[used++] = (char)text[at + more];
                at += want;
        }
        into[used] = 0;
        return used;
}

NOINLINE uint32_t ref_pack_key(uint8_t *out, uint64_t cap, const struct wl_direct_key *req)
{
        uint64_t length = 0;
        if (!out || !req || !req->ids || req->count <= 0) return 0;
        uint32_t time_ms = (uint32_t)(req->micro / 1000ull);
        for (int64_t at = 0; at < req->count; at++)
        {
                if (cap - length < 24) break;
                uint32_t words[6] = {req->ids[at], (24u << 16) | 3u, req->serial, time_ms, req->code, req->pressed ? 1u : 0u};
                memcpy(out + length, words, sizeof words);
                length += sizeof words;
        }
        return (uint32_t)length;
}

NOINLINE uint32_t ref_pack_packet(uint8_t *out, uint64_t cap, const struct wl_direct_packet *pkt)
{
        uint64_t length = 0;
        if (!out || !pkt) return 0;
        uint32_t time_ms = (uint32_t)(pkt->micro / 1000ull);
        if ((pkt->dx || pkt->dy) && pkt->rel_ids && pkt->rel_count > 0)
        {
                for (int64_t at = 0; at < pkt->rel_count; at++)
                {
                        if (cap - length < 32) return (uint32_t)length;
                        uint32_t words[8] = {
                                pkt->rel_ids[at], 32u << 16,
                                (uint32_t)(pkt->micro >> 32), (uint32_t)pkt->micro,
                                (uint32_t)ref_units_fixed(pkt->dx, pkt->scale120),
                                (uint32_t)ref_units_fixed(pkt->dy, pkt->scale120),
                                (uint32_t)((uint64_t)pkt->dx * 256u),
                                (uint32_t)((uint64_t)pkt->dy * 256u)
                        };
                        memcpy(out + length, words, sizeof words);
                        length += sizeof words;
                }
        }
        if (pkt->btn && pkt->ptr_ids && pkt->ptr_count > 0)
        {
                for (int64_t at = 0; at < pkt->ptr_count; at++)
                {
                        if (cap - length < 24) return (uint32_t)length;
                        uint32_t words[6] = {pkt->ptr_ids[at], (24u << 16) | 3u, pkt->btn_serial, time_ms, pkt->btn, pkt->btn_val ? 1u : 0u};
                        memcpy(out + length, words, sizeof words);
                        length += sizeof words;
                }
        }
        if (pkt->wheel_v120 && pkt->ptr_ids && pkt->ptr_ver && pkt->ptr_count > 0)
        {
                uint32_t axis = pkt->wheel_horiz ? 1u : 0u;
                int64_t v = pkt->wheel_horiz ? (int64_t)pkt->wheel_v120 : -(int64_t)pkt->wheel_v120;
                uint32_t fixed = (uint32_t)(v * 15 * 256 / 120);
                int32_t notches = (int32_t)(v / 120);
                for (int64_t at = 0; at < pkt->ptr_count; at++)
                {
                        uint32_t id = pkt->ptr_ids[at];
                        uint8_t ver = pkt->ptr_ver[at];
                        if (ver >= 5)
                        {
                                if (cap - length < 12) return (uint32_t)length;
                                uint32_t src[3] = {id, (12u << 16) | 6u, 0u};
                                memcpy(out + length, src, sizeof src);
                                length += sizeof src;
                        }
                        if (ver >= 8)
                        {
                                if (cap - length < 16) return (uint32_t)length;
                                uint32_t v120w[4] = {id, (16u << 16) | 9u, axis, (uint32_t)v};
                                memcpy(out + length, v120w, sizeof v120w);
                                length += sizeof v120w;
                        }
                        else if (ver >= 5 && notches)
                        {
                                if (cap - length < 16) return (uint32_t)length;
                                uint32_t disc[4] = {id, (16u << 16) | 8u, axis, (uint32_t)notches};
                                memcpy(out + length, disc, sizeof disc);
                                length += sizeof disc;
                        }
                        if (cap - length < 20) return (uint32_t)length;
                        uint32_t ax[5] = {id, (20u << 16) | 4u, time_ms, axis, fixed};
                        memcpy(out + length, ax, sizeof ax);
                        length += sizeof ax;
                }
        }
        if (pkt->frame_ids && pkt->frame_count > 0)
        {
                for (int64_t at = 0; at < pkt->frame_count; at++)
                {
                        if (cap - length < 8) return (uint32_t)length;
                        uint32_t words[2] = {pkt->frame_ids[at], (8u << 16) | 5u};
                        memcpy(out + length, words, sizeof words);
                        length += sizeof words;
                }
        }
        return (uint32_t)length;
}

/* ---- Test Harness ---- */

static uint64_t g_rng = 0x9e3779b97f4a7c15ULL;
static uint64_t xorshift64(void)
{
        uint64_t x = g_rng;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        return g_rng = x;
}

static void fail(const char *msg)
{
        fprintf(stderr, "FAIL: %s\n", msg);
        exit(1);
}

static uint64_t now_ns(void)
{
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void test_math(void)
{
        int64_t vals[] = {0, 1, -1, 59, -59, 60, -60, 61, -61, 119, -119, 120, -120, 121, -121,
                          239, -239, 240, -240, 241, -241, 1000000, -1000000,
                          INT32_MIN, INT32_MAX, (int64_t)INT32_MIN - 1, (int64_t)INT32_MAX + 1,
                          (1LL << 50), -(1LL << 50)};
        int64_t biases[] = {0, 60, 119, 1, 120, -1, -60, -119, -120, 500};

        for (size_t i = 0; i < sizeof(vals) / sizeof(vals[0]); i++)
        {
                for (size_t j = 0; j < sizeof(biases) / sizeof(biases[0]); j++)
                {
                        for (size_t k = 0; k < sizeof(biases) / sizeof(biases[0]); k++)
                        {
                                int64_t a = wl_direct_div120(vals[i], biases[j], biases[k]);
                                int64_t r = ref_div120(vals[i], biases[j], biases[k]);
                                if (a != r)
                                {
                                        fprintf(stderr, "div120(%lld,%lld,%lld): asm=%lld ref=%lld\n",
                                                (long long)vals[i], (long long)biases[j], (long long)biases[k],
                                                (long long)a, (long long)r);
                                        fail("div120 mismatch");
                                }
                        }
                }
        }

        /* Exhaustive scale120 60..480 + edge scales */
        int64_t coords[] = {0, 1, -1, 2, -2, 13, -13, 119, -119, 120, -120, 121, -121,
                            1920, -1920, 3840, -3840, 16384, -16384, INT32_MIN, INT32_MAX};
        for (int64_t s = -5; s <= 480; s++)
        {
                if (s > 5 && s < 60) continue;
                for (size_t i = 0; i < sizeof(coords) / sizeof(coords[0]); i++)
                {
                        int64_t u = coords[i];
                        if (wl_direct_pixels(u, s) != ref_pixels(u, s)) fail("pixels mismatch");
                        if (wl_direct_pixels_floor(u, s) != ref_pixels_floor(u, s)) fail("pixels_floor mismatch");
                        if (wl_direct_pixels_ceil(u, s) != ref_pixels_ceil(u, s)) fail("pixels_ceil mismatch");
                        if (wl_direct_units(u, s) != ref_units(u, s)) fail("units mismatch");
                        if (wl_direct_units_fixed(u, s) != ref_units_fixed(u, s)) fail("units_fixed mismatch");
                }
        }

        /* wl_direct_cover: NULL safety, aliasing, edge values */
        struct wl_direct_rect r0 = {-17, 23, 1920, 1080}, oa, or_;
        wl_direct_cover(NULL, 180, &oa);
        wl_direct_cover(&r0, 180, NULL);
        for (int64_t s = 60; s <= 480; s += 15)
        {
                wl_direct_cover(&r0, s, &oa);
                ref_cover(&r0, s, &or_);
                if (memcmp(&oa, &or_, sizeof oa) != 0) fail("cover mismatch");
                struct wl_direct_rect alias = r0;
                wl_direct_cover(&alias, s, &alias);
                if (memcmp(&alias, &or_, sizeof alias) != 0) fail("cover alias mismatch");
        }
}

static void test_text(void)
{
        char a[256], r[256];

        /* NULL & zero-cap safety */
        if (wl_direct_text(NULL, 128, (const uint8_t *)"abc", 3) != 0) fail("text null into");
        if (wl_direct_text(a, 0, (const uint8_t *)"abc", 3) != 0) fail("text zero cap");
        memset(a, 0x5a, sizeof a);
        if (wl_direct_text(a, 16, NULL, 5) != 0 || a[0] != 0) fail("text null src");
        memset(a, 0x5a, sizeof a);
        if (wl_direct_text(a, 16, (const uint8_t *)"abc", -5) != 0 || a[0] != 0) fail("text neg len");
        memset(a, 0x5a, sizeof a);
        if (wl_direct_text(a, 1, (const uint8_t *)"abc", 3) != 0 || a[0] != 0) fail("text cap=1");

        /* All 256 single bytes */
        for (int b = 0; b < 256; b++)
        {
                uint8_t in[1] = {(uint8_t)b};
                int64_t na = wl_direct_text(a, 128, in, 1);
                int64_t nr = ref_text(r, 128, in, 1);
                if (na != nr || memcmp(a, r, (size_t)nr + 1) != 0) fail("text single byte");
        }

        /* UTF-8 edge cases: valid 2/3/4, overlong, surrogates, >U+10FFFF, truncated, controls */
        const uint8_t cases[][16] = {
                {0xc2, 0x80, 0xdf, 0xbf, 0x20, 0x7e, 0x00, 0x7f}, /* valid 2-byte + ASCII + ctrl */
                {0xc0, 0x80, 0xc1, 0xbf, 0xc2, 0x7f, 0xc2, 0xc0}, /* overlong & bad cont */
                {0xe0, 0x9f, 0xbf, 0xe0, 0xa0, 0x80, 0xed, 0x9f, 0xbf, 0xed, 0xa0, 0x80}, /* 3-byte boundaries & surrogate */
                {0xf0, 0x8f, 0xbf, 0xbf, 0xf0, 0x90, 0x80, 0x80, 0xf4, 0x8f, 0xbf, 0xbf, 0xf4, 0x90, 0x80, 0x80}, /* 4-byte boundaries */
                {0xf5, 0x80, 0x80, 0x80, 0xfe, 0xff, 0x80, 0xbf}, /* invalid lead/cont */
                {'H', 'e', 'l', 'l', 'o', ' ', 0xf0, 0x9f, 0x98, 0x80, '!', '\n', '\t', 'X'}
        };
        const int64_t lens[] = {8, 8, 12, 16, 8, 14};
        for (size_t ci = 0; ci < sizeof(lens) / sizeof(lens[0]); ci++)
        {
                for (int64_t sublen = 0; sublen <= lens[ci]; sublen++)
                {
                        for (uint64_t cap = 1; cap <= 20; cap++)
                        {
                                memset(a, 0xcc, sizeof a);
                                memset(r, 0xcc, sizeof r);
                                int64_t na = wl_direct_text(a, cap, cases[ci], sublen);
                                int64_t nr = ref_text(r, cap, cases[ci], sublen);
                                if (na != nr || memcmp(a, r, sizeof a) != 0)
                                {
                                        fprintf(stderr, "text case %zu len=%lld cap=%llu: asm=%lld ref=%lld\n",
                                                ci, (long long)sublen, (unsigned long long)cap, (long long)na, (long long)nr);
                                        fail("text edge case mismatch");
                                }
                        }
                }
        }
}

static void test_pack(void)
{
        uint8_t a[1024], r[1024];
        uint32_t ids[8] = {10, 20, 30, 40, 50, 60, 70, 80};
        uint32_t fids[4] = {101, 102, 103, 104};
        uint8_t vers[8] = {1, 4, 5, 6, 7, 8, 9, 10};

        /* NULL safety */
        struct wl_direct_key kreq = {ids, 4, 123456789ULL, 77, 30, 1, 0};
        if (wl_direct_pack_key(NULL, 128, &kreq) != 0) fail("key null out");
        if (wl_direct_pack_key(a, 128, NULL) != 0) fail("key null req");
        struct wl_direct_key kbad = {NULL, 4, 1000, 1, 2, 1, 0};
        if (wl_direct_pack_key(a, 128, &kbad) != 0) fail("key null ids");
        kbad.ids = ids; kbad.count = 0;
        if (wl_direct_pack_key(a, 128, &kbad) != 0) fail("key count=0");
        kbad.count = -3;
        if (wl_direct_pack_key(a, 128, &kbad) != 0) fail("key count<0");

        /* Key cap sweep + micro extremes */
        uint64_t micros[] = {0, 1, 999, 1000, 1001, 0xffffffffULL, 0x123456789abcdef0ULL, UINT64_MAX};
        for (size_t mi = 0; mi < sizeof(micros) / sizeof(micros[0]); mi++)
        {
                kreq.micro = micros[mi];
                kreq.pressed = (mi & 1) ? -5 : 0;
                for (uint64_t cap = 0; cap <= 120; cap++)
                {
                        memset(a, 0xaa, sizeof a);
                        memset(r, 0xaa, sizeof r);
                        uint32_t na = wl_direct_pack_key(a, cap, &kreq);
                        uint32_t nr = ref_pack_key(r, cap, &kreq);
                        if (na != nr || na > cap || memcmp(a, r, sizeof a) != 0) fail("pack_key sweep mismatch");
                }
        }

        /* Packet NULL safety */
        struct wl_direct_packet pkt = {
                -15, 42, 0x1122334455667788ULL, 180,
                ids, 4, ids, vers, 8, fids, 4,
                0x110, 1, 999, -240, 0, 0
        };
        if (wl_direct_pack_packet(NULL, 512, &pkt) != 0) fail("pkt null out");
        if (wl_direct_pack_packet(a, 512, NULL) != 0) fail("pkt null pkt");

        /* Full cap sweep 0..600 byte-by-byte */
        for (uint64_t cap = 0; cap <= 600; cap++)
        {
                memset(a, 0xbb, sizeof a);
                memset(r, 0xbb, sizeof r);
                uint32_t na = wl_direct_pack_packet(a, cap, &pkt);
                uint32_t nr = ref_pack_packet(r, cap, &pkt);
                if (na != nr || na > cap || memcmp(a, r, sizeof a) != 0)
                {
                        fprintf(stderr, "pack_packet cap=%llu: asm=%u ref=%u\n", (unsigned long long)cap, na, nr);
                        fail("pack_packet cap sweep mismatch");
                }
        }
}

static void test_fuzz(void)
{
        /* 50,000 randomized differential fuzz iterations across all functions */
        for (int it = 0; it < 50000; it++)
        {
                int64_t u = (int32_t)xorshift64();
                int64_t s = (int64_t)(xorshift64() % 600) - 60;
                if (wl_direct_pixels(u, s) != ref_pixels(u, s)) fail("fuzz pixels");
                if (wl_direct_pixels_floor(u, s) != ref_pixels_floor(u, s)) fail("fuzz floor");
                if (wl_direct_pixels_ceil(u, s) != ref_pixels_ceil(u, s)) fail("fuzz ceil");
                if (wl_direct_units(u, s) != ref_units(u, s)) fail("fuzz units");
                if (wl_direct_units_fixed(u, s) != ref_units_fixed(u, s)) fail("fuzz fixed");

                struct wl_direct_rect rin = {(int32_t)xorshift64() / 4, (int32_t)xorshift64() / 4,
                                             (int32_t)xorshift64() / 4, (int32_t)xorshift64() / 4};
                struct wl_direct_rect oa, or_;
                wl_direct_cover(&rin, s, &oa);
                ref_cover(&rin, s, &or_);
                if (memcmp(&oa, &or_, sizeof oa) != 0) fail("fuzz cover");

                if (it < 10000)
                {
                        uint8_t src[160];
                        char oa_t[160], or_t[160];
                        int64_t len = (int64_t)(xorshift64() % 160);
                        uint64_t cap = (xorshift64() % 159) + 1;
                        for (int64_t i = 0; i < len; i++) src[i] = (uint8_t)xorshift64();
                        memset(oa_t, 0xdd, sizeof oa_t);
                        memset(or_t, 0xdd, sizeof or_t);
                        int64_t na = wl_direct_text(oa_t, cap, src, len);
                        int64_t nr = ref_text(or_t, cap, src, len);
                        if (na != nr || memcmp(oa_t, or_t, sizeof oa_t) != 0) fail("fuzz text");

                        uint32_t rids[4], pids[4], fids[4];
                        uint8_t pver[4];
                        for (int i = 0; i < 4; i++)
                        {
                                rids[i] = (uint32_t)xorshift64();
                                pids[i] = (uint32_t)xorshift64();
                                fids[i] = (uint32_t)xorshift64();
                                pver[i] = (uint8_t)(xorshift64() % 12);
                        }
                        struct wl_direct_packet p = {
                                (xorshift64() & 3) ? (int32_t)xorshift64() : 0,
                                (xorshift64() & 3) ? (int32_t)xorshift64() : 0,
                                xorshift64(),
                                s,
                                (xorshift64() & 7) ? rids : NULL, (int64_t)(xorshift64() % 5),
                                (xorshift64() & 7) ? pids : NULL, (xorshift64() & 7) ? pver : NULL, (int64_t)(xorshift64() % 5),
                                (xorshift64() & 7) ? fids : NULL, (int64_t)(xorshift64() % 5),
                                (xorshift64() & 1) ? (uint32_t)xorshift64() : 0u,
                                (int32_t)xorshift64(),
                                (uint32_t)xorshift64(),
                                (xorshift64() & 1) ? ((int32_t)(xorshift64() % 2000) - 1000) : 0,
                                (int32_t)(xorshift64() & 1),
                                0
                        };
                        uint8_t ba[512], br[512];
                        uint64_t pcap = xorshift64() % 513;
                        memset(ba, 0xee, sizeof ba);
                        memset(br, 0xee, sizeof br);
                        uint32_t pa = wl_direct_pack_packet(ba, pcap, &p);
                        uint32_t pr = ref_pack_packet(br, pcap, &p);
                        if (pa != pr || memcmp(ba, br, sizeof ba) != 0)
                        {
                                fprintf(stderr, "fuzz pack_packet it=%d cap=%llu pa=%u pr=%u dx=%lld dy=%lld s=%lld rc=%lld pc=%lld fc=%lld btn=%u wv=%d wh=%d micro=%llu\n",
                                        it, (unsigned long long)pcap, pa, pr, (long long)p.dx, (long long)p.dy, (long long)p.scale120,
                                        (long long)p.rel_count, (long long)p.ptr_count, (long long)p.frame_count, p.btn, p.wheel_v120, p.wheel_horiz, (unsigned long long)p.micro);
                                for (size_t k = 0; k < sizeof ba; k++)
                                        if (ba[k] != br[k]) { fprintf(stderr, "  diff at %zu: asm=%02x ref=%02x\n", k, ba[k], br[k]); break; }
                                fail("fuzz pack_packet");
                        }
                }
        }
}

static void test_guard_pages(void)
{
        long ps = sysconf(_SC_PAGESIZE);
        if (ps <= 0) ps = 4096;
        size_t page = (size_t)ps;

        /* 3 pages: [PROT_NONE] [RW] [PROT_NONE] for input, and same for output */
        uint8_t *in_map = mmap(NULL, page * 3, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
        uint8_t *out_map = mmap(NULL, page * 3, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (in_map == MAP_FAILED || out_map == MAP_FAILED) fail("mmap failed");
        if (mprotect(in_map + page, page, PROT_READ | PROT_WRITE) != 0) fail("mprotect in");
        if (mprotect(out_map + page, page, PROT_READ | PROT_WRITE) != 0) fail("mprotect out");

        uint8_t *in_lo = in_map + page;
        uint8_t *in_hi = in_map + page * 2;
        uint8_t *out_lo = out_map + page;
        uint8_t *out_hi = out_map + page * 2;

        /* 1. wl_direct_cover flush against leading & trailing guard pages */
        struct wl_direct_rect r = {-100, 200, 800, 600}, ref_r;
        ref_cover(&r, 180, &ref_r);
        memcpy(in_hi - sizeof r, &r, sizeof r);
        wl_direct_cover((const struct wl_direct_rect *)(in_hi - sizeof r), 180,
                        (struct wl_direct_rect *)(out_hi - sizeof r));
        if (memcmp(out_hi - sizeof r, &ref_r, sizeof ref_r) != 0) fail("guard cover hi");
        memcpy(in_lo, &r, sizeof r);
        wl_direct_cover((const struct wl_direct_rect *)in_lo, 180, (struct wl_direct_rect *)out_lo);
        if (memcmp(out_lo, &ref_r, sizeof ref_r) != 0) fail("guard cover lo");

        /* 2. wl_direct_text flush against leading & trailing guard pages (including truncated UTF-8 at edge!) */
        const uint8_t sample[] = {'A', 'B', 0xc3, 0xa9, 0xe2, 0x82, 0xac, 0xf0, 0x9f, 0x98, 0x80, 0xf0, 0x9f};
        for (size_t len = 1; len <= sizeof sample; len++)
        {
                memcpy(in_hi - len, sample, len);
                for (size_t cap = 1; cap <= 16; cap++)
                {
                        char ref_out[32];
                        int64_t nr = ref_text(ref_out, cap, sample, (int64_t)len);
                        int64_t na = wl_direct_text((char *)(out_hi - cap), cap, in_hi - len, (int64_t)len);
                        if (na != nr || memcmp(out_hi - cap, ref_out, (size_t)nr + 1) != 0) fail("guard text hi");
                        na = wl_direct_text((char *)out_lo, cap, in_hi - len, (int64_t)len);
                        if (na != nr || memcmp(out_lo, ref_out, (size_t)nr + 1) != 0) fail("guard text lo");
                }
        }

        /* 3. wl_direct_pack_key flush against trailing & leading guard pages (including odd caps) */
        uint32_t ids[4] = {11, 22, 33, 44};
        memcpy(in_hi - sizeof ids, ids, sizeof ids);
        struct wl_direct_key kreq = {(const uint32_t *)(in_hi - sizeof ids), 4, 9876543210ULL, 55, 1, 1, 0};
        for (size_t cap = 1; cap <= 104; cap++)
        {
                uint8_t ref_b[128];
                uint32_t nr = ref_pack_key(ref_b, cap, &kreq);
                uint32_t na = wl_direct_pack_key(out_hi - cap, cap, &kreq);
                if (na != nr || memcmp(out_hi - cap, ref_b, nr) != 0) fail("guard key hi");
                na = wl_direct_pack_key(out_lo, cap, &kreq);
                if (na != nr || memcmp(out_lo, ref_b, nr) != 0) fail("guard key lo");
        }

        /* 4. wl_direct_pack_packet flush against trailing & leading guard pages */
        uint8_t vers[4] = {4, 5, 7, 9};
        memcpy(in_hi - 4, vers, 4);
        memcpy(in_hi - 20, ids, 16);
        struct wl_direct_packet pkt = {
                12, -34, 1234567890123ULL, 150,
                (const uint32_t *)(in_hi - 20), 4,
                (const uint32_t *)(in_hi - 20), in_hi - 4, 4,
                (const uint32_t *)(in_hi - 20), 4,
                0x110, 1, 42, 360, 0, 0
        };
        for (size_t cap = 1; cap <= 460; cap += 7)
        {
                uint8_t ref_b[512];
                uint32_t nr = ref_pack_packet(ref_b, cap, &pkt);
                uint32_t na = wl_direct_pack_packet(out_hi - cap, cap, &pkt);
                if (na != nr || memcmp(out_hi - cap, ref_b, nr) != 0) fail("guard pkt hi");
                na = wl_direct_pack_packet(out_lo, cap, &pkt);
                if (na != nr || memcmp(out_lo, ref_b, nr) != 0) fail("guard pkt lo");
        }

        munmap(in_map, page * 3);
        munmap(out_map, page * 3);
}

static void bench(void)
{
        const int N = 2000000;
        volatile int64_t sink = 0;
        uint64_t t0, t1, t2;

        /* 1. pixels */
        t0 = now_ns();
        for (int i = 0; i < N; i++) sink += ref_pixels(i - 500000, 150);
        t1 = now_ns();
        for (int i = 0; i < N; i++) sink += wl_direct_pixels(i - 500000, 150);
        t2 = now_ns();
        printf("bench pixels      : C=%.2f ns/op  ASM=%.2f ns/op  speedup=%.2fx\n",
               (double)(t1 - t0) / N, (double)(t2 - t1) / N, (double)(t1 - t0) / (double)(t2 - t1));

        /* 2. cover */
        struct wl_direct_rect r = {-123, 456, 1920, 1080}, o;
        t0 = now_ns();
        for (int i = 0; i < N; i++) { r.x = i; ref_cover(&r, 150, &o); sink += o.width; }
        t1 = now_ns();
        for (int i = 0; i < N; i++) { r.x = i; wl_direct_cover(&r, 150, &o); sink += o.width; }
        t2 = now_ns();
        printf("bench cover       : C=%.2f ns/op  ASM=%.2f ns/op  speedup=%.2fx\n",
               (double)(t1 - t0) / N, (double)(t2 - t1) / N, (double)(t1 - t0) / (double)(t2 - t1));

        /* 3. text (typical Wayland window title) */
        const uint8_t title[] = "Moonwater Canvas - Wayland Terminal (UTF-8: \xc3\xa9\xe2\x82\xac)";
        char dst[128];
        int NT = 500000;
        t0 = now_ns();
        for (int i = 0; i < NT; i++) sink += ref_text(dst, 128, title, (int64_t)(sizeof(title) - 1));
        t1 = now_ns();
        for (int i = 0; i < NT; i++) sink += wl_direct_text(dst, 128, title, (int64_t)(sizeof(title) - 1));
        t2 = now_ns();
        printf("bench text        : C=%.2f ns/op  ASM=%.2f ns/op  speedup=%.2fx\n",
               (double)(t1 - t0) / NT, (double)(t2 - t1) / NT, (double)(t1 - t0) / (double)(t2 - t1));

        /* 4. pack_key (2 keyboards) */
        uint32_t kids[2] = {10, 11};
        struct wl_direct_key kreq = {kids, 2, 123456789012ULL, 99, 30, 1, 0};
        uint8_t buf[512];
        t0 = now_ns();
        for (int i = 0; i < N; i++) { kreq.serial = (uint32_t)i; sink += ref_pack_key(buf, sizeof buf, &kreq); }
        t1 = now_ns();
        for (int i = 0; i < N; i++) { kreq.serial = (uint32_t)i; sink += wl_direct_pack_key(buf, sizeof buf, &kreq); }
        t2 = now_ns();
        printf("bench pack_key    : C=%.2f ns/op  ASM=%.2f ns/op  speedup=%.2fx\n",
               (double)(t1 - t0) / N, (double)(t2 - t1) / N, (double)(t1 - t0) / (double)(t2 - t1));

        /* 5. pack_packet (8 kHz relative motion + frame, 2 relative ptrs) */
        uint32_t rids[2] = {20, 21}, pids[2] = {30, 31}, fids[2] = {40, 41};
        uint8_t pver[2] = {8, 8};
        struct wl_direct_packet pkt = {3, -5, 123456789012ULL, 150, rids, 2, pids, pver, 2, fids, 2, 0, 0, 0, 0, 0, 0};
        t0 = now_ns();
        for (int i = 0; i < N; i++) { pkt.dx = (i & 7) + 1; sink += ref_pack_packet(buf, sizeof buf, &pkt); }
        t1 = now_ns();
        for (int i = 0; i < N; i++) { pkt.dx = (i & 7) + 1; sink += wl_direct_pack_packet(buf, sizeof buf, &pkt); }
        t2 = now_ns();
        printf("bench pack_rel    : C=%.2f ns/op  ASM=%.2f ns/op  speedup=%.2fx\n",
               (double)(t1 - t0) / N, (double)(t2 - t1) / N, (double)(t1 - t0) / (double)(t2 - t1));

        /* 6. pack_packet (full: rel + btn + wheel + frame) */
        pkt.btn = 0x110; pkt.btn_val = 1; pkt.btn_serial = 7; pkt.wheel_v120 = 120;
        t0 = now_ns();
        for (int i = 0; i < NT; i++) { pkt.btn_serial = (uint32_t)i; sink += ref_pack_packet(buf, sizeof buf, &pkt); }
        t1 = now_ns();
        for (int i = 0; i < NT; i++) { pkt.btn_serial = (uint32_t)i; sink += wl_direct_pack_packet(buf, sizeof buf, &pkt); }
        t2 = now_ns();
        printf("bench pack_full   : C=%.2f ns/op  ASM=%.2f ns/op  speedup=%.2fx\n",
               (double)(t1 - t0) / NT, (double)(t2 - t1) / NT, (double)(t1 - t0) / (double)(t2 - t1));
        (void)sink;
}

int main(void)
{
        test_math();
        test_text();
        test_pack();
        test_fuzz();
        test_guard_pages();
        printf("PASS: wayland_direct (math, text, pack_key, pack_packet, 50k fuzz, mmap guard pages)\n");
        bench();
        return 0;
}
