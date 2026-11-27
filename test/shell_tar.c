/*
        Differential C-reference parity, security, fuzz, mmap guard-page & throughput
        proof for src/sh/shell_tar.asm.c (arm64, x86-64, riscv64 pure-ASM).

        Build & run:
          cc -O2 -Wall -Werror test/shell_tar.c -o /tmp/mw_shell_tar && /tmp/mw_shell_tar
          cc -O2 -fsanitize=address,undefined -Wall -Werror test/shell_tar.c -o /tmp/mw_shell_tar_asan && /tmp/mw_shell_tar_asan
*/
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>

#define SH_SHELL_TAR_ASM_IMPLEMENTATION
#include "../src/sh/shell_tar.asm.c"

/* ── Reference C implementations (matching src/sh/tar.c lines 1-275 exactly) ── */

#define REF_TAR_BLOCK        512u
#define REF_TAR_CHKSUM       148u
#define REF_TAR_CHKSUM_WIDTH 8u
#define REF_TAR_SPACE_SUM    ((uint32_t)' ' * REF_TAR_CHKSUM_WIDTH)

static __attribute__((noinline)) sh_tar_u32 ref_tar_header_sum(const sh_tar_u8 *block) {
        if (!block) return 0;
        uint32_t total = 0, field = 0;
        for (size_t i = 0; i < REF_TAR_BLOCK; i++) total += block[i];
        for (size_t i = 0; i < REF_TAR_CHKSUM_WIDTH; i++) field += block[REF_TAR_CHKSUM + i];
        return total - field + REF_TAR_SPACE_SUM;
}

static __attribute__((noinline)) sh_tar_i32 ref_tar_header_sum_match(const sh_tar_u8 *block,
                                                                     sh_tar_u64 stored,
                                                                     sh_tar_u64 expect) {
        if (!block) return 0;
        if (stored == expect) return 1;
        uint64_t high = 0;
        for (size_t at = 0; at < REF_TAR_BLOCK; at++) {
                if ((at < REF_TAR_CHKSUM || at >= REF_TAR_CHKSUM + REF_TAR_CHKSUM_WIDTH) &&
                    block[at] > 127)
                        high++;
        }
        return ((int64_t)stored == (int64_t)expect - (int64_t)high * 256) ? 1 : 0;
}

static __attribute__((noinline)) sh_tar_i32 ref_tar_base256(const sh_tar_u8 *field,
                                                            sh_tar_u64 width,
                                                            sh_tar_u64 *value) {
        if (!field || !width || !value || (field[0] & 0x40))
                return 0;
        uint64_t held = (uint64_t)(field[0] & 0x7f);
        for (uint64_t at = 1; at < width; at++) {
                if (held > (UINT64_MAX >> 8))
                        return 0;
                held = (held << 8) | field[at];
        }
        *value = held;
        return 1;
}

static __attribute__((noinline)) sh_tar_i32 ref_tar_octal_eight(sh_tar_u64 word,
                                                                sh_tar_u64 *value) {
        if (!value) return 0;
        if ((word ^ 0x3030303030303030ull) & 0xf8f8f8f8f8f8f8f8ull)
                return 0;
        word &= 0x0707070707070707ull;
        word = (word * 8 + (word >> 8)) & 0x00ff00ff00ff00ffull;
        word = (word * 64 + (word >> 16)) & 0x0000ffff0000ffffull;
        word = (word * 4096 + (word >> 32)) & 0xffffffull;
        *value = word;
        return 1;
}

static __attribute__((noinline)) sh_tar_i32 ref_tar_field_value(const sh_tar_u8 *field,
                                                                sh_tar_u64 width,
                                                                sh_tar_u64 *value) {
        if (!field || !value) return 0;
        if (width && (field[0] & 0x80))
                return ref_tar_base256(field, width, value);

        if (width == 8) {
                uint64_t word, number;
                memcpy(&word, field, 8);
                if ((word >> 56) == 0 || (word >> 56) == ' ') {
                        if (ref_tar_octal_eight((word & 0x00ffffffffffffffull) |
                                                    0x3000000000000000ull,
                                                &number)) {
                                *value = number >> 3;
                                return 1;
                        }
                }
        } else if (width == 12 && (field[11] == 0 || field[11] == ' ') &&
                   field[0] >= '0' && field[0] <= '7' && field[1] >= '0' &&
                   field[1] <= '7' && field[2] >= '0' && field[2] <= '7') {
                uint64_t word, number;
                memcpy(&word, field + 3, 8);
                if (ref_tar_octal_eight(word, &number)) {
                        *value = ((uint64_t)((field[0] - '0') * 64 + (field[1] - '0') * 8 +
                                             (field[2] - '0')) << 24) | number;
                        return 1;
                }
        }

        if (width >= 32)
                return 0;

        if (width && !field[0]) {
                *value = 0;
                return 1;
        }

        uint8_t digits[32];
        if (width) memcpy(digits, field, (size_t)width);
        digits[width] = 0;

        uint64_t at = 0;
        while (at < width && digits[at] == ' ') at++;
        if (at >= width || !digits[at] || digits[at] == ' ') {
                *value = 0;
                return 1;
        }

        uint64_t keep = 0;
        int any = 0;
        while (at < width && digits[at] >= '0' && digits[at] <= '7') {
                uint64_t d = (uint64_t)(digits[at] - '0');
                if (keep > (UINT64_MAX - d) / 8)
                        return 0;
                keep = keep * 8 + d;
                at++;
                any = 1;
        }
        if (!any) return 0;

        while (at < width && (digits[at] == ' ' || !digits[at]))
                at++;
        if (at < width && digits[at])
                return 0;

        *value = keep;
        return 1;
}

static __attribute__((noinline)) sh_tar_i32 ref_tar_field_moment(const sh_tar_u8 *field,
                                                                 sh_tar_u64 width,
                                                                 sh_tar_i64 *value) {
        if (!field || !value) return 0;
        if (!width || !(field[0] & 0x80)) {
                sh_tar_u64 plain = 0;
                if (!ref_tar_field_value(field, width, &plain) ||
                    plain > (uint64_t)INT64_MAX)
                        return 0;
                *value = (sh_tar_i64)plain;
                return 1;
        }

        int64_t held = (int64_t)(field[0] & 0x3f) - (int64_t)(field[0] & 0x40);
        for (uint64_t at = 1; at < width; at++) {
                if (held > (INT64_MAX >> 8) || held < (INT64_MIN >> 8))
                        return 0;
                held = held * 256 + (int64_t)field[at];
        }
        *value = held;
        return 1;
}

static inline uint64_t ref_octal_digits_word(uint64_t value) {
        uint64_t word = value & 0xffffffull;
        word = (word | (word << 20)) & 0x00000fff00000fffull;
        word = (word | (word << 10)) & 0x003f003f003f003full;
        word = (word | (word << 5))  & 0x0707070707070707ull;
        return __builtin_bswap64(word) | 0x3030303030303030ull;
}

static __attribute__((noinline)) void ref_tar_field_put_octal(sh_tar_u8 *field,
                                                              sh_tar_u64 width,
                                                              sh_tar_u64 value) {
        if (!field || !width) return;
        if (width == 8 && value < (1ull << 21)) {
                uint64_t word = ref_octal_digits_word(value) >> 8;
                memcpy(field, &word, 8);
                return;
        }
        if (width == 7 && value < (1ull << 18)) {
                uint64_t word = ref_octal_digits_word(value) >> 16;
                memcpy(field, &word, 7);
                return;
        }
        if (width == 12 && value < (1ull << 33)) {
                uint64_t word = ref_octal_digits_word(value);
                uint64_t high = value >> 24;
                field[0] = (uint8_t)('0' + ((high >> 6) & 7));
                field[1] = (uint8_t)('0' + ((high >> 3) & 7));
                field[2] = (uint8_t)('0' + (high & 7));
                memcpy(field + 3, &word, 8);
                field[11] = 0;
                return;
        }
        memset(field, '0', (size_t)width);
        field[width - 1] = 0;
        uint8_t rev[32], digits[32];
        size_t len = 0;
        uint64_t v = value;
        do {
                rev[len++] = (uint8_t)('0' + (v & 7));
                v >>= 3;
        } while (v);
        for (size_t i = 0; i < len; i++) digits[i] = rev[len - 1 - i];
        if (len >= width) len = (size_t)(width - 1);
        memcpy(field + (width - 1 - len), digits, len);
}

/* ── Test Harness ──────────────────────────────────────────────────────────── */

static uint64_t g_checks = 0;
static int      g_failures = 0;

static uint64_t g_rng = 0x9e3779b97f4a7c15ull;
static uint64_t rng_u64(void) {
        uint64_t x = g_rng;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        g_rng = x;
        return x;
}

static uint64_t now_ns(void) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void check_field_value(const char *tag, const uint8_t *buf, uint64_t w) {
        sh_tar_u64 a = 0xdeadbeefcafebabeull, r = 0xdeadbeefcafebabeull;
        sh_tar_i32 ar = sh_tar_field_value(buf, w, &a);
        sh_tar_i32 rr = ref_tar_field_value(buf, w, &r);
        g_checks++;
        if (ar != rr || a != r) {
                fprintf(stderr, "FAIL field_value[%s] w=%llu: asm=(%d,0x%llx) ref=(%d,0x%llx)\n",
                        tag, (unsigned long long)w, ar, (unsigned long long)a,
                        rr, (unsigned long long)r);
                g_failures++;
        }
}

static void check_base256(const char *tag, const uint8_t *buf, uint64_t w) {
        sh_tar_u64 a = 0x1122334455667788ull, r = 0x1122334455667788ull;
        sh_tar_i32 ar = sh_tar_base256(buf, w, &a);
        sh_tar_i32 rr = ref_tar_base256(buf, w, &r);
        g_checks++;
        if (ar != rr || a != r) {
                fprintf(stderr, "FAIL base256[%s] w=%llu: asm=(%d,0x%llx) ref=(%d,0x%llx)\n",
                        tag, (unsigned long long)w, ar, (unsigned long long)a,
                        rr, (unsigned long long)r);
                g_failures++;
        }
}

static void check_moment(const char *tag, const uint8_t *buf, uint64_t w) {
        sh_tar_i64 a = (sh_tar_i64)0x2233445566778899ll, r = (sh_tar_i64)0x2233445566778899ll;
        sh_tar_i32 ar = sh_tar_field_moment(buf, w, &a);
        sh_tar_i32 rr = ref_tar_field_moment(buf, w, &r);
        g_checks++;
        if (ar != rr || a != r) {
                fprintf(stderr, "FAIL field_moment[%s] w=%llu: asm=(%d,%lld) ref=(%d,%lld)\n",
                        tag, (unsigned long long)w, ar, (long long)a, rr, (long long)r);
                g_failures++;
        }
}

static void check_octal_eight(const char *tag, uint64_t word) {
        sh_tar_u64 a = 0xaaaabbbbccccddddull, r = 0xaaaabbbbccccddddull;
        sh_tar_i32 ar = sh_tar_octal_eight(word, &a);
        sh_tar_i32 rr = ref_tar_octal_eight(word, &r);
        g_checks++;
        if (ar != rr || a != r) {
                fprintf(stderr, "FAIL octal_eight[%s] w=0x%llx: asm=(%d,0x%llx) ref=(%d,0x%llx)\n",
                        tag, (unsigned long long)word, ar, (unsigned long long)a,
                        rr, (unsigned long long)r);
                g_failures++;
        }
}

static void check_put_octal(const char *tag, uint64_t w, uint64_t val) {
        uint8_t abuf[64], rbuf[64];
        memset(abuf, 0xa5, sizeof(abuf));
        memset(rbuf, 0xa5, sizeof(rbuf));
        sh_tar_field_put_octal(abuf, w, val);
        ref_tar_field_put_octal(rbuf, w, val);
        g_checks++;
        if (memcmp(abuf, rbuf, sizeof(abuf)) != 0) {
                fprintf(stderr, "FAIL put_octal[%s] w=%llu val=0x%llx\n",
                        tag, (unsigned long long)w, (unsigned long long)val);
                g_failures++;
        }
}

static void check_header(const char *tag, const uint8_t *blk) {
        sh_tar_u32 as = sh_tar_header_sum(blk);
        sh_tar_u32 rs = ref_tar_header_sum(blk);
        g_checks++;
        if (as != rs) {
                fprintf(stderr, "FAIL header_sum[%s]: asm=%u ref=%u\n", tag, as, rs);
                g_failures++;
        }
        /* Unsigned match, signed-char match, and mismatch */
        uint64_t high = 0;
        for (size_t i = 0; i < 512; i++) {
                if ((i < 148 || i >= 156) && blk[i] > 127) high++;
        }
        uint64_t tests[4] = {
                (uint64_t)rs,
                (uint64_t)((int64_t)rs - (int64_t)high * 256),
                (uint64_t)rs + 1,
                (uint64_t)((int64_t)rs - (int64_t)high * 256) - 1
        };
        for (int t = 0; t < 4; t++) {
                sh_tar_i32 am = sh_tar_header_sum_match(blk, tests[t], (uint64_t)rs);
                sh_tar_i32 rm = ref_tar_header_sum_match(blk, tests[t], (uint64_t)rs);
                g_checks++;
                if (am != rm) {
                        fprintf(stderr, "FAIL header_sum_match[%s] t=%d: asm=%d ref=%d\n",
                                tag, t, am, rm);
                        g_failures++;
                }
        }
}

static void run_guard_page_tests(void) {
        long ps = sysconf(_SC_PAGESIZE);
        if (ps <= 0) ps = 4096;
        size_t page = (size_t)ps;
        /* 3 pages: [PROT_NONE] [RW] [PROT_NONE] */
        uint8_t *map = (uint8_t *)mmap(NULL, 3 * page, PROT_READ | PROT_WRITE,
                                       MAP_PRIVATE | MAP_ANON, -1, 0);
        if (map == MAP_FAILED) {
                perror("mmap");
                g_failures++;
                return;
        }
        if (mprotect(map, page, PROT_NONE) != 0 ||
            mprotect(map + 2 * page, page, PROT_NONE) != 0) {
                perror("mprotect");
                munmap(map, 3 * page);
                g_failures++;
                return;
        }
        uint8_t *rw_lo = map + page;
        uint8_t *rw_hi = map + 2 * page;

        /* 512-byte header flush against leading guard page AND trailing guard page */
        uint8_t *hdrs[2] = { rw_lo, rw_hi - 512 };
        for (int h = 0; h < 2; h++) {
                uint8_t *b = hdrs[h];
                for (size_t i = 0; i < 512; i++) b[i] = (uint8_t)((i * 37 + h * 113) & 0xff);
                check_header(h == 0 ? "guard_lo_512" : "guard_hi_512", b);
        }

        /* Field buffers of every width 0..32 flush against BOTH leading and trailing guard pages */
        for (uint64_t w = 0; w <= 32; w++) {
                uint8_t *ptrs[2] = { rw_lo, rw_hi - w };
                for (int side = 0; side < 2; side++) {
                        uint8_t *p = ptrs[side];
                        /* Octal value */
                        if (w > 0) {
                                for (uint64_t i = 0; i + 1 < w; i++) p[i] = (uint8_t)('0' + (i & 7));
                                p[w - 1] = 0;
                        }
                        check_field_value("guard_oct", p, w);
                        check_moment("guard_mom_oct", p, w);

                        /* Put octal flush against guard page (must NOT touch byte w or byte -1!) */
                        sh_tar_field_put_octal(p, w, 0755);
                        uint8_t ref[64];
                        ref_tar_field_put_octal(ref, w, 0755);
                        g_checks++;
                        if (w > 0 && memcmp(p, ref, (size_t)w) != 0) {
                                fprintf(stderr, "FAIL guard put_octal w=%llu side=%d\n",
                                        (unsigned long long)w, side);
                                g_failures++;
                        }

                        /* Base-256 positive & negative */
                        if (w > 0) {
                                memset(p, 0, (size_t)w);
                                p[0] = 0x80;
                                if (w > 1) p[w - 1] = 0x42;
                                check_base256("guard_b256_pos", p, w);
                                check_field_value("guard_fv_b256", p, w);
                                check_moment("guard_mom_pos", p, w);

                                memset(p, 0xff, (size_t)w);
                                check_base256("guard_b256_neg", p, w);
                                check_moment("guard_mom_neg", p, w);
                        }
                }
        }

        munmap(map, 3 * page);
}

int main(void) {
        /* 1. NULL pointer safety checks */
        {
                sh_tar_u64 u = 0x55;
                sh_tar_i64 s = 0x55;
                uint8_t dummy[16] = {0};
                g_checks += 12;
                if (sh_tar_header_sum(NULL) != 0) g_failures++;
                if (sh_tar_header_sum_match(NULL, 0, 0) != 0) g_failures++;
                if (sh_tar_header_sum_match(NULL, 1, 2) != 0) g_failures++;
                if (sh_tar_base256(NULL, 8, &u) != 0) g_failures++;
                if (sh_tar_base256(dummy, 8, NULL) != 0) g_failures++;
                if (sh_tar_base256(dummy, 0, &u) != 0) g_failures++;
                if (sh_tar_octal_eight(0x3030303030303030ull, NULL) != 0) g_failures++;
                if (sh_tar_field_value(NULL, 8, &u) != 0) g_failures++;
                if (sh_tar_field_value(dummy, 8, NULL) != 0) g_failures++;
                if (sh_tar_field_moment(NULL, 8, &s) != 0) g_failures++;
                if (sh_tar_field_moment(dummy, 8, NULL) != 0) g_failures++;
                sh_tar_field_put_octal(NULL, 8, 123);
                sh_tar_field_put_octal(dummy, 0, 123);
                if (u != 0x55 || s != 0x55) g_failures++;
        }

        /* 2. Exhaustive edge cases & security cases */
        {
                const uint8_t z8[8]   = {0};
                const uint8_t sp8[8]  = {' ',' ',' ',' ',' ',' ',' ',' '};
                const uint8_t f8a[8]  = {'0','0','0','0','7','5','5',0};
                const uint8_t f8b[8]  = {'0','0','0','0','7','5','5',' '};
                const uint8_t f8c[8]  = {' ',' ',' ','7','5','5',' ',0};
                const uint8_t f8d[8]  = {'7','7','7','7','7','7','7','7'};
                const uint8_t f8bad1[8] = {'0','0','0','8','0','0','0',0};
                const uint8_t f8bad2[8] = {'0','0','0','9','0','0','0',0};
                const uint8_t f8bad3[8] = {'0','0','0','/','0','0','0',0};
                const uint8_t f8bad4[8] = {'1','2','3',0,'a','b','c',0};
                const uint8_t f8sp0[8]  = {' ',' ',0,'a','b','c',0,0};
                check_field_value("z8", z8, 8);
                check_field_value("sp8", sp8, 8);
                check_field_value("f8a", f8a, 8);
                check_field_value("f8b", f8b, 8);
                check_field_value("f8c", f8c, 8);
                check_field_value("f8d", f8d, 8);
                check_field_value("f8bad1", f8bad1, 8);
                check_field_value("f8bad2", f8bad2, 8);
                check_field_value("f8bad3", f8bad3, 8);
                check_field_value("f8bad4", f8bad4, 8);
                check_field_value("f8sp0", f8sp0, 8);
                check_field_value("w0", z8, 0);

                const uint8_t f12a[12] = "00000012345";
                const uint8_t f12b[12] = {'7','7','7','7','7','7','7','7','7','7','7',' '};
                const uint8_t f12c[12] = {' ',' ','1','2','3','4','5',' ',' ',' ',0,0};
                const uint8_t f12bad[12] = {'0','0','8','0','0','0','0','0','0','0','0',0};
                check_field_value("f12a", f12a, 12);
                check_field_value("f12b", f12b, 12);
                check_field_value("f12c", f12c, 12);
                check_field_value("f12bad", f12bad, 12);

                /* Octal 64-bit max & overflow */
                const uint8_t oct_max[23]  = "1777777777777777777777";
                const uint8_t oct_ovf1[23] = "2000000000000000000000";
                const uint8_t oct_ovf2[23] = "7777777777777777777777";
                const uint8_t oct_smax[23] = "0777777777777777777777";
                const uint8_t oct_smax1[23]= "1000000000000000000000";
                const uint8_t oct_w32[32]  = "0000000000000000000000000000001";
                check_field_value("oct_max", oct_max, 23);
                check_field_value("oct_ovf1", oct_ovf1, 23);
                check_field_value("oct_ovf2", oct_ovf2, 23);
                check_field_value("oct_w32", oct_w32, 32);
                check_moment("mom_oct_smax", oct_smax, 23);
                check_moment("mom_oct_smax1", oct_smax1, 23);
                check_moment("mom_oct_max", oct_max, 23);

                /* Base-256 positive, negative, sign extension, overflow */
                const uint8_t b256_zero[12]   = {0x80,0,0,0,0,0,0,0,0,0,0,0};
                const uint8_t b256_umax[12]   = {0x80,0,0,0,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff};
                const uint8_t b256_uovf[12]   = {0x80,0,0,1,0,0,0,0,0,0,0,0};
                const uint8_t b256_uovf2[12]  = {0x81,0,0,0,0,0,0,0,0,0,0,0};
                const uint8_t b256_neg1[12]   = {0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff};
                const uint8_t b256_smax[12]   = {0x80,0,0,0,0x7f,0xff,0xff,0xff,0xff,0xff,0xff,0xff};
                const uint8_t b256_smax1[12]  = {0x80,0,0,0,0x80,0,0,0,0,0,0,0};
                const uint8_t b256_smin[12]   = {0xff,0xff,0xff,0xff,0x80,0,0,0,0,0,0,0};
                const uint8_t b256_smin1[12]  = {0xff,0xff,0xff,0xfe,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff};
                const uint8_t b256_c0[1]      = {0xc0};
                const uint8_t b256_bf[1]      = {0xbf};
                const uint8_t b256_80[1]      = {0x80};
                const uint8_t b256_ff[1]      = {0xff};
                const uint8_t *b256s[] = {
                        b256_zero, b256_umax, b256_uovf, b256_uovf2, b256_neg1,
                        b256_smax, b256_smax1, b256_smin, b256_smin1
                };
                for (size_t i = 0; i < sizeof(b256s)/sizeof(b256s[0]); i++) {
                        check_base256("edge_b256", b256s[i], 12);
                        check_field_value("edge_fv_b256", b256s[i], 12);
                        check_moment("edge_mom_b256", b256s[i], 12);
                }
                check_base256("b256_c0", b256_c0, 1);
                check_base256("b256_bf", b256_bf, 1);
                check_base256("b256_80", b256_80, 1);
                check_base256("b256_ff", b256_ff, 1);
                check_moment("mom_c0", b256_c0, 1);
                check_moment("mom_bf", b256_bf, 1);
                check_moment("mom_80", b256_80, 1);
                check_moment("mom_ff", b256_ff, 1);

                /* octal_eight edge cases */
                check_octal_eight("all0", 0x3030303030303030ull);
                check_octal_eight("all7", 0x3737373737373737ull);
                check_octal_eight("seq",  0x3736353433323130ull);
                check_octal_eight("bad8", 0x3030303830303030ull);
                check_octal_eight("bad/", 0x30302f3030303030ull);
                check_octal_eight("hi",   0xb030303030303030ull);

                /* put_octal edge cases */
                const uint64_t widths[] = { 0, 1, 2, 3, 6, 7, 8, 9, 11, 12, 13, 22, 23, 32 };
                const uint64_t vals[] = {
                        0, 1, 7, 8, 0755, 0777777, (1ull << 18) - 1, 1ull << 18,
                        (1ull << 21) - 1, 1ull << 21, (1ull << 24) - 1, 1ull << 24,
                        (1ull << 33) - 1, 1ull << 33, UINT64_MAX >> 1, UINT64_MAX
                };
                for (size_t wi = 0; wi < sizeof(widths)/sizeof(widths[0]); wi++) {
                        for (size_t vi = 0; vi < sizeof(vals)/sizeof(vals[0]); vi++) {
                                check_put_octal("edge_put", widths[wi], vals[vi]);
                        }
                }

                /* Header checksum edge cases */
                uint8_t blk[512];
                memset(blk, 0, 512);
                check_header("all_zero", blk);
                memset(blk, 0xff, 512);
                check_header("all_ff", blk);
                memset(blk, 0x80, 512);
                check_header("all_80", blk);
                memset(blk, 0x7f, 512);
                check_header("all_7f", blk);
                /* High bytes ONLY inside [148..155] */
                memset(blk, 'A', 512);
                memset(blk + 148, 0xff, 8);
                check_header("high_in_chksum_only", blk);
        }

        /* 3. Randomized differential fuzzing (50,000 iterations) */
        for (int it = 0; it < 50000; it++) {
                uint8_t buf[40];
                uint64_t w = rng_u64() % 35;
                int mode = it & 7;
                if (mode == 0) {
                        /* Pure octal + NUL/space */
                        for (uint64_t i = 0; i < w; i++) buf[i] = (uint8_t)('0' + (rng_u64() & 7));
                        if (w > 0) buf[w - 1] = (rng_u64() & 1) ? 0 : ' ';
                } else if (mode == 1) {
                        /* Leading/trailing spaces + octal */
                        uint64_t lead = w ? (rng_u64() % w) : 0;
                        uint64_t digs = (w > lead) ? (rng_u64() % (w - lead + 1)) : 0;
                        for (uint64_t i = 0; i < lead; i++) buf[i] = ' ';
                        for (uint64_t i = lead; i < lead + digs; i++) buf[i] = (uint8_t)('0' + (rng_u64() & 7));
                        for (uint64_t i = lead + digs; i < w; i++) buf[i] = (rng_u64() & 1) ? 0 : ' ';
                } else if (mode == 2) {
                        /* GNU base-256 positive */
                        if (w > 0) {
                                memset(buf, 0, (size_t)w);
                                buf[0] = (uint8_t)(0x80 | (rng_u64() & 0x3f));
                                for (uint64_t i = 1; i < w; i++) buf[i] = (uint8_t)(rng_u64() & 0xff);
                                if (w > 8 && (rng_u64() & 1)) {
                                        buf[0] = 0x80;
                                        for (uint64_t i = 1; i < w - 8; i++) buf[i] = 0;
                                }
                        }
                } else if (mode == 3) {
                        /* GNU base-256 negative */
                        if (w > 0) {
                                memset(buf, 0xff, (size_t)w);
                                uint64_t tail = (w > 8) ? 8 : w;
                                for (uint64_t i = w - tail; i < w; i++) buf[i] = (uint8_t)(rng_u64() & 0xff);
                                if (w > 8 && (rng_u64() & 1)) buf[0] = 0xff;
                                else buf[0] |= 0xc0;
                        }
                } else {
                        /* Arbitrary bytes */
                        for (uint64_t i = 0; i < w; i++) buf[i] = (uint8_t)(rng_u64() & 0xff);
                }
                check_field_value("fuzz", buf, w);
                check_base256("fuzz", buf, w);
                check_moment("fuzz", buf, w);

                uint64_t word = rng_u64();
                if (it & 1) {
                        word = 0x3030303030303030ull | (word & 0x0707070707070707ull);
                        if ((it & 3) == 3) word ^= (1ull << (rng_u64() & 63));
                }
                check_octal_eight("fuzz", word);

                uint64_t pw = (it & 3) == 0 ? 8 : ((it & 3) == 1 ? 12 : ((it & 3) == 2 ? 7 : (rng_u64() % 33)));
                uint64_t pv = rng_u64();
                if (it & 8) pv &= (1ull << ((pw == 7) ? 18 : (pw == 8 ? 21 : 33))) - 1;
                check_put_octal("fuzz", pw, pv);
        }

        /* 2,000 random 512-byte headers */
        for (int it = 0; it < 2000; it++) {
                uint8_t blk[512];
                for (size_t i = 0; i < 512; i += 8) {
                        uint64_t r = rng_u64();
                        memcpy(blk + i, &r, 8);
                }
                check_header("fuzz_hdr", blk);
        }

        /* 4. Hardware mmap guard-page tests */
        run_guard_page_tests();

        if (g_failures != 0) {
                fprintf(stderr, "FAILED: %d failures across %llu checks\n",
                        g_failures, (unsigned long long)g_checks);
                return 1;
        }

        /* 5. Throughput benchmark vs -O2 C reference */
        uint8_t b_hdr[512];
        for (size_t i = 0; i < 512; i++) b_hdr[i] = (uint8_t)((i * 73 + 17) & 0xff);
        uint32_t expect_sum = ref_tar_header_sum(b_hdr);
        uint64_t high = 0;
        for (size_t i = 0; i < 512; i++) if ((i < 148 || i >= 156) && b_hdr[i] > 127) high++;
        uint64_t signed_sum = (uint64_t)((int64_t)expect_sum - (int64_t)high * 256);

        uint8_t b_f8[8]   = {'0','0','0','0','7','5','5',0};
        uint8_t b_f12[12] = {'0','0','0','0','1','2','3','4','5','6','7',0};
        uint8_t b_b256[12]= {0x80,0,0,0,0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef};
        uint8_t b_out[16];
        const uint8_t * volatile p_hdr  = b_hdr;
        const uint8_t * volatile p_f8   = b_f8;
        const uint8_t * volatile p_f12  = b_f12;
        const uint8_t * volatile p_b256 = b_b256;
        uint8_t * volatile p_out        = b_out;
        volatile uint64_t v_w8  = 8, v_w12 = 12;
        volatile uint64_t v_ssum = signed_sum, v_esum = expect_sum;
        volatile uint64_t v_val8 = 0755, v_val12 = 01234567;
        volatile uint64_t sink = 0;
        const int N = 2000000;
        sh_tar_u64 uv = 0;
        sh_tar_i64 sv = 0;

        /* Warmup */
        for (int i = 0; i < 10000; i++) {
                sink += sh_tar_header_sum(p_hdr) + ref_tar_header_sum(p_hdr);
                sink += sh_tar_field_value(p_f12, v_w12, &uv) + ref_tar_field_value(p_f12, v_w12, &uv);
        }

        uint64_t h0 = now_ns();
        for (int i = 0; i < N; i++) sink += sh_tar_header_sum(p_hdr);
        uint64_t h1 = now_ns();
        for (int i = 0; i < N; i++) sink += ref_tar_header_sum(p_hdr);
        uint64_t h2 = now_ns();

        uint64_t m0 = now_ns();
        for (int i = 0; i < N; i++) sink += (uint64_t)sh_tar_header_sum_match(p_hdr, v_ssum, v_esum);
        uint64_t m1 = now_ns();
        for (int i = 0; i < N; i++) sink += (uint64_t)ref_tar_header_sum_match(p_hdr, v_ssum, v_esum);
        uint64_t m2 = now_ns();

        uint64_t v0 = now_ns();
        for (int i = 0; i < N; i++) {
                sink += (uint64_t)sh_tar_field_value(p_f8, v_w8, &uv) + uv;
                sink += (uint64_t)sh_tar_field_value(p_f12, v_w12, &uv) + uv;
        }
        uint64_t v1 = now_ns();
        for (int i = 0; i < N; i++) {
                sink += (uint64_t)ref_tar_field_value(p_f8, v_w8, &uv) + uv;
                sink += (uint64_t)ref_tar_field_value(p_f12, v_w12, &uv) + uv;
        }
        uint64_t v2 = now_ns();

        uint64_t p0 = now_ns();
        for (int i = 0; i < N; i++) {
                sh_tar_field_put_octal(p_out, v_w8, v_val8);
                sh_tar_field_put_octal(p_out, v_w12, v_val12);
                sink += b_out[5];
        }
        uint64_t p1 = now_ns();
        for (int i = 0; i < N; i++) {
                ref_tar_field_put_octal(p_out, v_w8, v_val8);
                ref_tar_field_put_octal(p_out, v_w12, v_val12);
                sink += b_out[5];
        }
        uint64_t p2 = now_ns();

        uint64_t t0 = now_ns();
        for (int i = 0; i < N; i++) sink += (uint64_t)sh_tar_field_moment(p_b256, v_w12, &sv) + (uint64_t)sv;
        uint64_t t1 = now_ns();
        for (int i = 0; i < N; i++) sink += (uint64_t)ref_tar_field_moment(p_b256, v_w12, &sv) + (uint64_t)sv;
        uint64_t t2 = now_ns();

        double ah = (double)(h1 - h0) / (double)N,       rh = (double)(h2 - h1) / (double)N;
        double am = (double)(m1 - m0) / (double)N,       rm = (double)(m2 - m1) / (double)N;
        double av = (double)(v1 - v0) / (double)(2 * N), rv = (double)(v2 - v1) / (double)(2 * N);
        double ap = (double)(p1 - p0) / (double)(2 * N), rp = (double)(p2 - p1) / (double)(2 * N);
        double at = (double)(t1 - t0) / (double)N,       rt = (double)(t2 - t1) / (double)N;

        printf("PASS: 0 failures across %llu differential + guard-page checks\n", (unsigned long long)g_checks);
        printf("BENCH sh_tar_header_sum       : ASM %5.1f ns/op | C -O2 %5.1f ns/op | %.2fx\n", ah, rh, rh / ah);
        printf("BENCH sh_tar_header_sum_match : ASM %5.1f ns/op | C -O2 %5.1f ns/op | %.2fx\n", am, rm, rm / am);
        printf("BENCH sh_tar_field_value      : ASM %5.1f ns/op | C -O2 %5.1f ns/op | %.2fx\n", av, rv, rv / av);
        printf("BENCH sh_tar_field_put_octal  : ASM %5.1f ns/op | C -O2 %5.1f ns/op | %.2fx\n", ap, rp, rp / ap);
        printf("BENCH sh_tar_field_moment     : ASM %5.1f ns/op | C -O2 %5.1f ns/op | %.2fx\n", at, rt, rt / at);
        (void)sink;
        return 0;
}
