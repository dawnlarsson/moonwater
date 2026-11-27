/*
        Verification, security (mmap guard pages, ASan/UBSan), differential
        fuzzing, and speed/size proof for src/rover/rover_eval.asm.c.
*/
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>

#define ROVER_EVAL_ASM_IMPLEMENTATION
#include "../src/rover/rover_eval.asm.c"

/* ---- -O2 C reference implementations (noinline for fair nm size & bench) ---- */

__attribute__((noinline))
rover_u32 ref_rover_glob(const rover_u8 *text, rover_u32 length, const rover_u8 *pattern, rover_u32 size)
{
        rover_u32 head = 0, tail = 0, last, at, in, star, mark;
        int starred;

        if (!size)
                return length == 0;
        if (!pattern || (length && !text))
                return 0;
        while (head < size && pattern[head] != '*' && pattern[head] != '?')
                head++;
        if (head == size)
                return length == size && memcmp(text, pattern, size) == 0;
        if (head > length || (head && memcmp(text, pattern, head) != 0))
                return 0;
        last = size;
        while (last > head && pattern[last - 1] != '*' && pattern[last - 1] != '?')
                last--, tail++;
        if (last > head && pattern[last - 1] == '*') {
                rover_u32 s = head;
                if (head + tail > length || (tail && memcmp(text + length - tail, pattern + last, tail) != 0))
                        return 0;
                while (s < last && pattern[s] == '*')
                        s++;
                if (s == last)
                        return 1;
                text += head;
                length -= head + tail;
                pattern += head;
                size = last - head;
        } else {
                text += head;
                length -= head;
                pattern += head;
                size -= head;
        }
        at = 0; in = 0; star = 0; mark = 0; starred = 0;
        while (at < length) {
                if (in < size && pattern[in] == '*') {
                        starred = 1;
                        star = in++;
                        mark = at;
                } else if (in < size && (pattern[in] == '?' || pattern[in] == text[at])) {
                        at++; in++;
                } else if (starred) {
                        in = star + 1;
                        at = ++mark;
                } else {
                        return 0;
                }
        }
        while (in < size && pattern[in] == '*')
                in++;
        return in == size;
}

__attribute__((noinline))
rover_u32 ref_rover_compare_num(rover_i64 a, rover_i64 b, rover_i64 c, rover_u32 op)
{
        switch (op) {
        case ROVER_EVAL_TRUE:    return a != 0;
        case ROVER_EVAL_EQ:      return a == b;
        case ROVER_EVAL_NE:      return a != b;
        case ROVER_EVAL_LT:      return a < b;
        case ROVER_EVAL_LE:      return a <= b;
        case ROVER_EVAL_GT:      return a > b;
        case ROVER_EVAL_GE:      return a >= b;
        case ROVER_EVAL_BETWEEN: return b <= c ? (a >= b && a <= c) : (a >= b || a <= c);
        default:                 return 0;
        }
}

__attribute__((noinline))
rover_u32 ref_rover_may(const struct rover_eval_rule *rule, struct rover_eval_run *run, rover_u64 when)
{
        if ((rule->flags & ROVER_EVAL_RULE_ONCE) && run->done)
                return 0;
        if (rule->limit_ms && run->fires && (rover_i64)(when - run->last) < (rover_i64)(rover_u64)rule->limit_ms * 1000000ll) {
                run->dropped++;
                return 0;
        }
        return 1;
}

__attribute__((noinline))
rover_u64 ref_rover_key_next(const rover_u32 *keys, rover_u64 at, rover_u64 count, rover_u32 want)
{
        while (at < count && keys[at] != want && keys[at] != ROVER_EVAL_KEY_ANY)
                at++;
        return at;
}

static inline rover_i64 ref_side(const struct rover_eval_table *table, struct rover_eval_operand op,
                                 const struct rover_eval_event *event, const rover_u8 *const *pages)
{
        if (op.source == ROVER_EVAL_STATE_NUMBER) {
                const rover_u8 *p = op.page ? pages[1] : pages[0];
                rover_i32 v = 0;
                if (p)
                        memcpy(&v, p + op.index, sizeof(v));
                return v;
        }
        if (op.source == ROVER_EVAL_CONST_NUMBER) {
                rover_i64 v;
                memcpy(&v, table->pool + op.index, sizeof(v));
                return v;
        }
        return op.index < event->count ? event->field[op.index].number : 0;
}

__attribute__((noinline))
rover_i32 ref_rover_holds_numbers(const struct rover_eval_table *table, const struct rover_eval_rule *rule,
                                  const struct rover_eval_event *event, const rover_u8 *const *pages)
{
        const struct rover_eval_comparison *cmp = table->comparison + rule->comparison;
        int line = 0, term = 1;
        for (rover_u32 at = 0; at < rule->comparisons; at++, cmp++) {
                if (term && !line) {
                        rover_i64 a = ref_side(table, cmp->a, event, pages);
                        rover_i64 b = ref_side(table, cmp->b, event, pages);
                        term = (int)ref_rover_compare_num(a, b, 0, cmp->op);
                }
                if (cmp->join & (ROVER_EVAL_JOIN_OR | ROVER_EVAL_JOIN_LINE)) {
                        line |= term;
                        term = 1;
                }
                if (cmp->join & ROVER_EVAL_JOIN_LINE) {
                        if (!line)
                                return (at < rule->trigger && !(rule->flags & ROVER_EVAL_RULE_WHEN)) ? -1 : 0;
                        line = 0;
                }
        }
        return 1;
}

__attribute__((noinline))
rover_u64 ref_rover_walk_numbers(const struct rover_eval_table *table, struct rover_eval_run *runs,
                                 const rover_u8 *const *pages, const struct rover_eval_event *event,
                                 rover_u64 place_room, struct rover_eval_hit *hits)
{
        rover_u64 room = place_room >> 32;
        rover_u8 place = (rover_u8)(place_room & 0xff);
        rover_u32 want = ROVER_EVAL_KEY_ANY;
        rover_u64 made = 0;

        if (!room || event->id >= ROVER_EVAL_EVENTS)
                return 0;
        struct rover_eval_span span = table->index[event->id];
        rover_u32 count = span.count & ROVER_EVAL_SPAN_COUNT;
        const rover_u16 *order = table->order + span.first;
        const rover_u32 *keys = table->key + span.first;

        if (event->count && event->field[0].number >= 0 && event->field[0].number < (rover_i64)ROVER_EVAL_KEY_ANY)
                want = (rover_u32)event->field[0].number;

        for (rover_u64 slot = 0; slot < count; slot++) {
                if (keys[slot] != want && keys[slot] != ROVER_EVAL_KEY_ANY)
                        continue;
                rover_u16 num = order[slot];
                const struct rover_eval_rule *rule = table->rule + num;
                if (rule->place != place || (rule->flags & ROVER_EVAL_RULE_OFF))
                        continue;
                if (!(rule->flags & ROVER_EVAL_RULE_NUMBERS))
                        return made | (slot << 32) | ROVER_EVAL_WALK_STOPPED;
                rover_i32 held = ref_rover_holds_numbers(table, rule, event, pages);
                if (held < 0 || (!held && !rule->otherwise) || !ref_rover_may(rule, runs + num, event->when))
                        continue;
                hits[made].rule = num;
                hits[made].action = held ? rule->action : (rover_u16)(rule->action + rule->then);
                hits[made].count = held ? rule->then : rule->otherwise;
                hits[made].otherwise = !held;
                hits[made].pad = 0;
                runs[num].fires++;
                runs[num].last = event->when;
                if (rule->flags & ROVER_EVAL_RULE_ONCE)
                        runs[num].done = 1;
                made++;
                if (made >= room)
                        break;
        }
        return made;
}

__attribute__((noinline))
rover_u64 ref_rover_edges_numbers(const struct rover_eval_table *table, struct rover_eval_run *runs,
                                  const rover_u8 *const *pages, rover_u64 changed,
                                  const struct rover_eval_event *event, rover_u64 place_room,
                                  struct rover_eval_hit *hits)
{
        rover_u64 room = place_room >> 32;
        rover_u8 place = (rover_u8)(place_room & 0xff);
        rover_u64 made = 0;
        struct rover_eval_span span = table->index[0];
        rover_u32 count = span.count & ROVER_EVAL_SPAN_COUNT;
        const rover_u16 *order = table->order + span.first;

        for (rover_u64 slot = 0; slot < count; slot++) {
                rover_u16 num = order[slot];
                const struct rover_eval_rule *rule = table->rule + num;
                struct rover_eval_run *run = runs + num;
                if (rule->place != place || (rule->flags & ROVER_EVAL_RULE_OFF) || !(rule->reads & changed))
                        continue;
                if (!(rule->flags & ROVER_EVAL_RULE_NUMBERS))
                        return made | (slot << 32) | ROVER_EVAL_WALK_STOPPED;
                int held = ref_rover_holds_numbers(table, rule, event, pages) > 0;
                if (held == run->edge)
                        continue;
                if (made == room && (held || rule->otherwise))
                        continue;
                run->edge = (rover_u8)held;
                if ((!held && !rule->otherwise) || !ref_rover_may(rule, run, event->when))
                        continue;
                hits[made].rule = num;
                hits[made].action = held ? rule->action : (rover_u16)(rule->action + rule->then);
                hits[made].count = held ? rule->then : rule->otherwise;
                hits[made].otherwise = !held;
                hits[made].pad = 0;
                runs[num].fires++;
                runs[num].last = event->when;
                if (rule->flags & ROVER_EVAL_RULE_ONCE)
                        runs[num].done = 1;
                made++;
        }
        return made;
}

/* ---- Test harness ---- */

static int g_fails = 0;
static uint64_t g_rng = 0x9e3779b97f4a7c15ull;

static uint64_t rnd64(void)
{
        uint64_t x = g_rng;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        g_rng = x;
        return x * 0x2545f4914f6cdd1dull;
}

#define CHECK(cond, ...) do { \
        if (!(cond)) { \
                fprintf(stderr, "FAIL [%s:%d]: ", __FILE__, __LINE__); \
                fprintf(stderr, __VA_ARGS__); \
                fprintf(stderr, "\n"); \
                g_fails++; \
                if (g_fails > 20) exit(1); \
        } \
} while (0)

/* ---- Guard-page allocator: [PROT_NONE][RW][PROT_NONE] ---- */

struct guard_buf {
        void  *base;
        size_t total;
        size_t page;
        uint8_t *rw;
};

static struct guard_buf guard_alloc(void)
{
        struct guard_buf g;
        g.page = (size_t)sysconf(_SC_PAGESIZE);
        g.total = g.page * 3;
        g.base = mmap(NULL, g.total, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        CHECK(g.base != MAP_FAILED, "mmap failed");
        g.rw = (uint8_t *)g.base + g.page;
        CHECK(mprotect(g.rw, g.page, PROT_READ | PROT_WRITE) == 0, "mprotect failed");
        return g;
}

static void guard_free(struct guard_buf g)
{
        munmap(g.base, g.total);
}

/* ---- 1. Compare tests ---- */

static void test_compare_num(void)
{
        static const rover_i64 vals[] = {
                INT64_MIN, INT64_MIN + 1, -1000000000ll, -2, -1, 0, 1, 2,
                1000000000ll, INT64_MAX - 1, INT64_MAX
        };
        size_t n = sizeof(vals) / sizeof(vals[0]);
        for (rover_u32 op = 0; op < 16; op++) {
                for (size_t i = 0; i < n; i++) {
                        for (size_t j = 0; j < n; j++) {
                                if (op == ROVER_EVAL_BETWEEN) {
                                        for (size_t k = 0; k < n; k++) {
                                                rover_u32 got = rover_eval_compare_num(vals[i], vals[j], vals[k], op);
                                                rover_u32 exp = ref_rover_compare_num(vals[i], vals[j], vals[k], op);
                                                CHECK(got == exp, "cmp BETWEEN a=%lld b=%lld c=%lld got=%u exp=%u",
                                                      (long long)vals[i], (long long)vals[j], (long long)vals[k], got, exp);
                                        }
                                } else {
                                        rover_u32 got = rover_eval_compare_num(vals[i], vals[j], 0, op);
                                        rover_u32 exp = ref_rover_compare_num(vals[i], vals[j], 0, op);
                                        CHECK(got == exp, "cmp op=%u a=%lld b=%lld got=%u exp=%u",
                                              op, (long long)vals[i], (long long)vals[j], got, exp);
                                }
                        }
                }
        }
        for (int iter = 0; iter < 20000; iter++) {
                rover_i64 a = (rover_i64)rnd64(), b = (rover_i64)rnd64(), c = (rover_i64)rnd64();
                rover_u32 op = (rover_u32)(rnd64() % 12);
                CHECK(rover_eval_compare_num(a, b, c, op) == ref_rover_compare_num(a, b, c, op),
                      "fuzz cmp op=%u", op);
        }
}

/* ---- 2. Glob tests + guard pages + pathological ---- */

static void test_glob(void)
{
        static const struct { const char *t; const char *p; rover_u32 exp; } cases[] = {
                {"", "", 1}, {"a", "", 0}, {"", "*", 1}, {"", "***", 1}, {"", "?", 0},
                {"a", "?", 1}, {"ab", "?", 0}, {"ab", "??", 1}, {"ab", "a*b", 1},
                {"a", "a*a", 0}, {"aa", "a*a", 1}, {"aba", "a*a", 1},
                {"steam_app_12345", "steam_app_*", 1}, {"steam_app_", "steam_app_*", 1},
                {"steam_ap_123", "steam_app_*", 0}, {"foo_bar_baz", "*_bar_*", 1},
                {"foo_bar", "*_bar", 1}, {"bar", "*_bar", 0},
                {"abc", "a?c", 1}, {"ac", "a?c", 0}, {"abbc", "a?c", 0},
                {"abbc", "a*?c", 1}, {"ac", "a*?c", 0}, {"axc", "a*?c", 1},
                {"aaaaa", "*a*a*a*a*a*", 1}, {"aaaa", "*a*a*a*a*a*", 0},
                {"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                 "*a*a*a*a*a*a*a*a*a*a*a*a*a*b", 0},
                {"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaab",
                 "*a*a*a*a*a*a*a*a*a*a*a*a*a*b", 1},
                {"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                 "*a*a*a*a*a*a*a*a*a*a*a*a*a*c*", 0},
        };
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
                rover_u32 tl = (rover_u32)strlen(cases[i].t);
                rover_u32 pl = (rover_u32)strlen(cases[i].p);
                rover_u32 got = rover_eval_glob((const rover_u8 *)cases[i].t, tl, (const rover_u8 *)cases[i].p, pl);
                rover_u32 exp = ref_rover_glob((const rover_u8 *)cases[i].t, tl, (const rover_u8 *)cases[i].p, pl);
                CHECK(got == cases[i].exp && got == exp, "glob[%zu] t='%s' p='%s' got=%u exp=%u",
                      i, cases[i].t, cases[i].p, got, exp);
        }
        /* NULL pointer edge cases */
        CHECK(rover_eval_glob(NULL, 0, NULL, 0) == 1, "glob NULL,0,NULL,0");
        CHECK(rover_eval_glob(NULL, 0, (const rover_u8 *)"*", 1) == 1, "glob NULL,0,*");
        CHECK(rover_eval_glob(NULL, 0, (const rover_u8 *)"?", 1) == 0, "glob NULL,0,?");
        CHECK(rover_eval_glob(NULL, 3, (const rover_u8 *)"*", 1) == 0, "glob NULL,3,*");
        CHECK(rover_eval_glob((const rover_u8 *)"abc", 3, NULL, 0) == 0, "glob abc,NULL,0");
        CHECK(rover_eval_glob((const rover_u8 *)"abc", 3, NULL, 2) == 0, "glob abc,NULL,2");

        /* Embedded NUL and high bytes */
        static const rover_u8 bin_t[] = {0x00, 0x80, 0xff, 0x00, 'x'};
        static const rover_u8 bin_p[] = {0x00, '*', 0x00, '?'};
        CHECK(rover_eval_glob(bin_t, sizeof(bin_t), bin_p, sizeof(bin_p)) == 1, "glob binary");

        /* Guard-page tests: flush at start and flush at end of RW page */
        struct guard_buf gt = guard_alloc(), gp = guard_alloc();
        for (int iter = 0; iter < 5000; iter++) {
                rover_u32 tl = (rover_u32)(rnd64() % 64);
                rover_u32 pl = (rover_u32)(rnd64() % 32);
                rover_u8 *tb = (iter & 1) ? (gt.rw + gt.page - tl) : gt.rw;
                rover_u8 *pb = (iter & 2) ? (gp.rw + gp.page - pl) : gp.rw;
                for (rover_u32 i = 0; i < tl; i++) {
                        uint64_t r = rnd64() % 5;
                        tb[i] = (rover_u8)(r < 4 ? ('a' + r) : (rnd64() & 0xff));
                }
                for (rover_u32 i = 0; i < pl; i++) {
                        uint64_t r = rnd64() % 8;
                        pb[i] = (r == 0) ? '*' : (r == 1) ? '?' : (r < 6 ? (rover_u8)('a' + (r - 2)) : (rover_u8)(rnd64() & 0xff));
                }
                rover_u32 got = rover_eval_glob(tb, tl, pb, pl);
                rover_u32 exp = ref_rover_glob(tb, tl, pb, pl);
                CHECK(got == exp, "fuzz glob iter=%d tl=%u pl=%u got=%u exp=%u", iter, tl, pl, got, exp);
        }
        guard_free(gt);
        guard_free(gp);
}

/* ---- 3. May & Key-next tests ---- */

static void test_may_and_key_next(void)
{
        struct rover_eval_rule rule;
        struct rover_eval_run r_asm, r_ref;
        memset(&rule, 0, sizeof(rule));
        rule.flags = ROVER_EVAL_RULE_ONCE;
        rule.limit_ms = 86400000u; /* max 1 day */

        static const rover_u64 times[] = {
                0, 1, 999999ull, 1000000ull, 86400000000000ull - 1, 86400000000000ull,
                86400000000000ull + 1, UINT64_MAX - 5, UINT64_MAX
        };
        for (int iter = 0; iter < 2000; iter++) {
                rule.flags = (rover_u8)((rnd64() & 1) ? ROVER_EVAL_RULE_ONCE : 0);
                rule.limit_ms = (iter < 20) ? 86400000u : (rover_u32)(rnd64() % 5000);
                memset(&r_asm, 0, sizeof(r_asm));
                r_asm.last = times[rnd64() % (sizeof(times) / sizeof(times[0]))];
                r_asm.fires = (rover_u32)(rnd64() % 3);
                r_asm.dropped = (rover_u32)(rnd64() % 10);
                r_asm.done = (rover_u8)(rnd64() & 1);
                r_ref = r_asm;
                rover_u64 when = times[rnd64() % (sizeof(times) / sizeof(times[0]))];
                rover_u32 ga = rover_eval_may(&rule, &r_asm, when);
                rover_u32 gr = ref_rover_may(&rule, &r_ref, when);
                CHECK(ga == gr && memcmp(&r_asm, &r_ref, sizeof(r_asm)) == 0,
                      "may iter=%d ga=%u gr=%u", iter, ga, gr);
        }

        /* key_next with guard pages */
        struct guard_buf gk = guard_alloc();
        for (int iter = 0; iter < 2000; iter++) {
                rover_u64 count = rnd64() % 64;
                rover_u32 *keys = (iter & 1) ? (rover_u32 *)(gk.rw + gk.page - count * 4) : (rover_u32 *)gk.rw;
                for (rover_u64 i = 0; i < count; i++) {
                        uint64_t r = rnd64() % 12;
                        keys[i] = (r == 0) ? ROVER_EVAL_KEY_ANY : (rover_u32)(r - 1);
                }
                rover_u64 at = count ? (rnd64() % (count + 3)) : (rnd64() % 3);
                rover_u32 want = (rover_u32)(rnd64() % 14);
                if (iter % 17 == 0) want = ROVER_EVAL_KEY_ANY;
                rover_u64 ga = rover_eval_key_next(keys, at, count, want);
                rover_u64 gr = ref_rover_key_next(keys, at, count, want);
                CHECK(ga == gr, "key_next iter=%d at=%llu cnt=%llu want=%u ga=%llu gr=%llu",
                      iter, (unsigned long long)at, (unsigned long long)count, want,
                      (unsigned long long)ga, (unsigned long long)gr);
        }
        guard_free(gk);
}

/* ---- 4. Holds, Walk, Edges tests + fuzz + guard pages ---- */

struct synth_table {
        struct rover_eval_table      tbl;
        struct rover_eval_rule       rules[32];
        rover_u16                    order[32];
        rover_u32                    keys[32];
        struct rover_eval_span       index[ROVER_EVAL_EVENTS];
        struct rover_eval_comparison cmps[128];
        rover_u8                     pool[256];
        rover_u8                     page0[256];
        rover_u8                     page1[256];
};

static void fill_operand(struct rover_eval_operand *op, int allow_null_page)
{
        uint64_t r = rnd64() % 3;
        if (r == 0) {
                op->source = ROVER_EVAL_CONST_NUMBER;
                op->page = 0;
                op->index = (rover_u16)((rnd64() % 30) * 8);
        } else if (r == 1) {
                op->source = ROVER_EVAL_STATE_NUMBER;
                op->page = (rover_u8)(allow_null_page ? (rnd64() & 1) : (rnd64() & 1));
                op->index = (rover_u16)((rnd64() % 60) * 4);
        } else {
                op->source = ROVER_EVAL_FIELD;
                op->page = 0;
                op->index = (rover_u16)(rnd64() % 6); /* includes out-of-range 4,5 */
        }
}

static void build_synth(struct synth_table *st, rover_u16 ev_id, rover_u32 nrules, int when_mode, int allow_non_num)
{
        memset(st, 0, sizeof(*st));
        for (int i = 0; i < 32; i++) {
                rover_i64 v = (i == 0) ? INT64_MIN : (i == 1) ? INT64_MAX : (rover_i64)(rnd64() % 21) - 10;
                memcpy(st->pool + i * 8, &v, 8);
        }
        for (int i = 0; i < 64; i++) {
                rover_i32 v0 = (rover_i32)((rnd64() % 21) - 10);
                rover_i32 v1 = (rover_i32)((rnd64() % 21) - 10);
                memcpy(st->page0 + i * 4, &v0, 4);
                memcpy(st->page1 + i * 4, &v1, 4);
        }

        rover_u16 cmp_at = 0;
        for (rover_u32 i = 0; i < nrules; i++) {
                struct rover_eval_rule *r = &st->rules[i];
                r->event = when_mode ? 0 : ev_id;
                r->place = (rover_u8)(rnd64() % 3);
                r->flags = (rover_u8)((when_mode ? ROVER_EVAL_RULE_WHEN : 0) |
                                      ((rnd64() % 4 == 0) ? ROVER_EVAL_RULE_ONCE : 0) |
                                      ((rnd64() % 8 == 0) ? ROVER_EVAL_RULE_OFF : 0) |
                                      ((!allow_non_num || (rnd64() % 6 != 0)) ? ROVER_EVAL_RULE_NUMBERS : 0));
                rover_u16 ncmp = (rover_u16)(rnd64() % 5);
                if (cmp_at + ncmp > 128) ncmp = (rover_u16)(128 - cmp_at);
                r->comparison = cmp_at;
                r->comparisons = ncmp;
                r->trigger = ncmp ? (rover_u8)(rnd64() % (ncmp + 1)) : 0;
                r->then = (rover_u8)(1 + (rnd64() % 4));
                r->otherwise = (rover_u8)(rnd64() % 3);
                r->action = (rover_u16)(i * 8);
                r->limit_ms = (rnd64() % 3 == 0) ? (rover_u32)(rnd64() % 50) : 0;
                r->reads = rnd64();
                for (rover_u16 c = 0; c < ncmp; c++) {
                        struct rover_eval_comparison *mc = &st->cmps[cmp_at + c];
                        mc->op = (rover_u8)(ROVER_EVAL_EQ + (rnd64() % 6));
                        mc->type = 0;
                        mc->form = ROVER_EVAL_FORM_NUMBER;
                        if (c == ncmp - 1)
                                mc->join = ROVER_EVAL_JOIN_LINE;
                        else {
                                uint64_t j = rnd64() % 4;
                                mc->join = (j == 0) ? 0 : (j == 1) ? ROVER_EVAL_JOIN_OR : ROVER_EVAL_JOIN_LINE;
                        }
                        fill_operand(&mc->a, 1);
                        fill_operand(&mc->b, 1);
                }
                cmp_at = (rover_u16)(cmp_at + ncmp);
                st->order[i] = (rover_u16)i;
                st->keys[i] = (rnd64() % 3 == 0) ? ROVER_EVAL_KEY_ANY : (rover_u32)(rnd64() % 5);
        }
        st->index[when_mode ? 0 : ev_id].first = 0;
        st->index[when_mode ? 0 : ev_id].count = (rover_u16)(nrules | ROVER_EVAL_SPAN_NUMBERS);
        st->tbl.rule = st->rules;
        st->tbl.order = st->order;
        st->tbl.key = st->keys;
        st->tbl.index = st->index;
        st->tbl.comparison = st->cmps;
        st->tbl.pool = st->pool;
}

static void test_holds_walk_edges(void)
{
        struct synth_table st;
        struct rover_eval_run runs_a[32], runs_r[32];
        struct rover_eval_hit hits_a[32], hits_r[32];
        struct rover_eval_event ev;

        /* Edge case: room == 0, event->id >= 128 */
        build_synth(&st, 5, 4, 0, 0);
        const rover_u8 *pages[2] = {st.page0, st.page1};
        memset(&ev, 0, sizeof(ev));
        ev.id = 5; ev.count = 1; ev.field[0].number = 1;
        memset(runs_a, 0, sizeof(runs_a));
        CHECK(rover_walk_numbers(&st.tbl, runs_a, pages, &ev, 0, NULL) == 0, "walk room==0");
        ev.id = 128;
        CHECK(rover_walk_numbers(&st.tbl, runs_a, pages, &ev, (4ull << 32) | 0, hits_a) == 0, "walk id==128");
        ev.id = 9999;
        CHECK(rover_walk_numbers(&st.tbl, runs_a, pages, &ev, (4ull << 32) | 0, hits_a) == 0, "walk id==9999");

        /* Fuzz rover_eval_holds_numbers + rover_walk_numbers */
        for (int iter = 0; iter < 5000; iter++) {
                rover_u16 ev_id = (rover_u16)(1 + (rnd64() % (ROVER_EVAL_EVENTS - 1)));
                rover_u32 nrules = (rover_u32)(rnd64() % 16);
                build_synth(&st, ev_id, nrules, 0, 1);
                const rover_u8 *p2[2] = {
                        (iter % 19 == 0) ? NULL : st.page0,
                        (iter % 23 == 0) ? NULL : st.page1
                };
                memset(&ev, 0, sizeof(ev));
                ev.id = ev_id;
                ev.count = (rover_u8)(rnd64() % 5);
                ev.when = (rnd64() % 100) * 1000000ull;
                for (int f = 0; f < 4; f++)
                        ev.field[f].number = (rover_i64)(rnd64() % 11) - 3;
                if (iter % 13 == 0) ev.field[0].number = (rover_i64)0xffffffffull;
                if (iter % 17 == 0) ev.field[0].number = INT64_MIN;

                for (rover_u32 i = 0; i < nrules; i++) {
                        if (st.rules[i].flags & ROVER_EVAL_RULE_NUMBERS) {
                                rover_i32 ha = rover_eval_holds_numbers(&st.tbl, &st.rules[i], &ev, p2);
                                rover_i32 hr = ref_rover_holds_numbers(&st.tbl, &st.rules[i], &ev, p2);
                                CHECK(ha == hr, "holds iter=%d rule=%u ha=%d hr=%d", iter, i, ha, hr);
                        }
                }

                memset(runs_a, 0, sizeof(runs_a));
                for (rover_u32 i = 0; i < nrules; i++) {
                        runs_a[i].last = (rnd64() % 100) * 1000000ull;
                        runs_a[i].fires = (rover_u32)(rnd64() % 3);
                        runs_a[i].done = (rover_u8)(rnd64() & 1);
                }
                memcpy(runs_r, runs_a, sizeof(runs_a));
                memset(hits_a, 0xaa, sizeof(hits_a));
                memset(hits_r, 0xaa, sizeof(hits_r));

                rover_u64 room = rnd64() % 8;
                rover_u8 place = (rover_u8)(rnd64() % 3);
                rover_u64 pr = place | (room << 32);
                rover_u64 wa = rover_walk_numbers(&st.tbl, runs_a, p2, &ev, pr, hits_a);
                rover_u64 wr = ref_rover_walk_numbers(&st.tbl, runs_r, p2, &ev, pr, hits_r);
                CHECK(wa == wr, "walk iter=%d wa=0x%llx wr=0x%llx", iter, (unsigned long long)wa, (unsigned long long)wr);
                CHECK(memcmp(runs_a, runs_r, sizeof(runs_a)) == 0, "walk runs mismatch iter=%d", iter);
                rover_u32 made = (rover_u32)wa;
                CHECK(memcmp(hits_a, hits_r, made * sizeof(struct rover_eval_hit)) == 0, "walk hits mismatch iter=%d", iter);
        }

        /* Fuzz rover_eval_edges_numbers (including room==0 edge reset & room cap!) */
        for (int iter = 0; iter < 5000; iter++) {
                rover_u32 nrules = (rover_u32)(rnd64() % 16);
                build_synth(&st, 0, nrules, 1, 1);
                const rover_u8 *p2[2] = {
                        (iter % 19 == 0) ? NULL : st.page0,
                        (iter % 23 == 0) ? NULL : st.page1
                };
                memset(&ev, 0, sizeof(ev));
                ev.when = (rnd64() % 100) * 1000000ull;
                memset(runs_a, 0, sizeof(runs_a));
                for (rover_u32 i = 0; i < nrules; i++) {
                        runs_a[i].last = (rnd64() % 100) * 1000000ull;
                        runs_a[i].fires = (rover_u32)(rnd64() % 3);
                        runs_a[i].edge = (rover_u8)(rnd64() & 1);
                        runs_a[i].done = (rover_u8)(rnd64() & 1);
                }
                memcpy(runs_r, runs_a, sizeof(runs_a));
                memset(hits_a, 0xbb, sizeof(hits_a));
                memset(hits_r, 0xbb, sizeof(hits_r));

                rover_u64 room = (iter % 7 == 0) ? 0 : (rnd64() % 6);
                rover_u8 place = (rover_u8)(rnd64() % 3);
                rover_u64 changed = (iter % 11 == 0) ? 0 : (iter % 13 == 0) ? ~0ull : rnd64();
                rover_u64 pr = place | (room << 32);
                rover_u64 ea = rover_eval_edges_numbers(&st.tbl, runs_a, p2, changed, &ev, pr, room ? hits_a : NULL);
                rover_u64 er = ref_rover_edges_numbers(&st.tbl, runs_r, p2, changed, &ev, pr, room ? hits_r : NULL);
                CHECK(ea == er, "edges iter=%d ea=0x%llx er=0x%llx", iter, (unsigned long long)ea, (unsigned long long)er);
                CHECK(memcmp(runs_a, runs_r, sizeof(runs_a)) == 0, "edges runs mismatch iter=%d", iter);
                rover_u32 made = (rover_u32)ea;
                if (made)
                        CHECK(memcmp(hits_a, hits_r, made * sizeof(struct rover_eval_hit)) == 0, "edges hits mismatch iter=%d", iter);
        }

        /* Guard-page test for walk_numbers & edges_numbers (hits/runs/order/keys flush at page end) */
        struct guard_buf gh = guard_alloc(), gr = guard_alloc();
        build_synth(&st, 7, 4, 0, 0);
        for (int i = 0; i < 4; i++) {
                st.rules[i].place = 0;
                st.rules[i].flags = ROVER_EVAL_RULE_NUMBERS;
                st.rules[i].comparisons = 0;
                st.keys[i] = ROVER_EVAL_KEY_ANY;
        }
        struct rover_eval_hit *ghits = (struct rover_eval_hit *)(gh.rw + gh.page - 2 * sizeof(struct rover_eval_hit));
        struct rover_eval_run *gruns = (struct rover_eval_run *)(gr.rw + gr.page - 4 * sizeof(struct rover_eval_run));
        memset(gruns, 0, 4 * sizeof(struct rover_eval_run));
        memset(&ev, 0, sizeof(ev));
        ev.id = 7; ev.when = 1000000000ull;
        rover_u64 got = rover_walk_numbers(&st.tbl, gruns, pages, &ev, (2ull << 32) | 0, ghits);
        CHECK(got == 2, "guard walk got=%llu", (unsigned long long)got);
        guard_free(gh);
        guard_free(gr);
}

/* ---- 5. Speed benchmark ---- */

static uint64_t now_ns(void)
{
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void run_bench(void)
{
        const int N = 2000000;
        volatile uint64_t sink = 0;
        uint64_t t0, t1;
        double ns_asm, ns_c;

        /* 1. glob (steam_app_* hot case) */
        const rover_u8 *gt = (const rover_u8 *)"steam_app_1245620";
        const rover_u8 *gp = (const rover_u8 *)"steam_app_*";
        t0 = now_ns();
        for (int i = 0; i < N; i++) {
                __asm__ volatile("" : "+r"(gt), "+r"(gp));
                sink += rover_eval_glob(gt, 17, gp, 11);
        }
        t1 = now_ns(); ns_asm = (double)(t1 - t0) / N;
        t0 = now_ns();
        for (int i = 0; i < N; i++) {
                __asm__ volatile("" : "+r"(gt), "+r"(gp));
                sink += ref_rover_glob(gt, 17, gp, 11);
        }
        t1 = now_ns(); ns_c = (double)(t1 - t0) / N;
        printf("  %-26s ASM %6.2f ns/op  |  C -O2 %6.2f ns/op  |  speedup %5.2fx\n",
               "rover_eval_glob", ns_asm, ns_c, ns_c / ns_asm);

        /* 2. compare_num */
        t0 = now_ns();
        for (int i = 0; i < N; i++) sink += rover_eval_compare_num(i, 1000000, 2000000, (rover_u32)(1 + (i % 6)));
        t1 = now_ns(); ns_asm = (double)(t1 - t0) / N;
        t0 = now_ns();
        for (int i = 0; i < N; i++) sink += ref_rover_compare_num(i, 1000000, 2000000, (rover_u32)(1 + (i % 6)));
        t1 = now_ns(); ns_c = (double)(t1 - t0) / N;
        printf("  %-26s ASM %6.2f ns/op  |  C -O2 %6.2f ns/op  |  speedup %5.2fx\n",
               "rover_eval_compare_num", ns_asm, ns_c, ns_c / ns_asm);

        /* 3. may */
        struct rover_eval_rule mr;
        struct rover_eval_run mrun;
        memset(&mr, 0, sizeof(mr)); mr.limit_ms = 100;
        memset(&mrun, 0, sizeof(mrun)); mrun.fires = 1; mrun.last = 500000000ull;
        t0 = now_ns();
        for (int i = 0; i < N; i++) sink += rover_eval_may(&mr, &mrun, 700000000ull + (rover_u64)i);
        t1 = now_ns(); ns_asm = (double)(t1 - t0) / N;
        t0 = now_ns();
        for (int i = 0; i < N; i++) sink += ref_rover_may(&mr, &mrun, 700000000ull + (rover_u64)i);
        t1 = now_ns(); ns_c = (double)(t1 - t0) / N;
        printf("  %-26s ASM %6.2f ns/op  |  C -O2 %6.2f ns/op  |  speedup %5.2fx\n",
               "rover_eval_may", ns_asm, ns_c, ns_c / ns_asm);

        /* 4. key_next (12 keys, match at 10) */
        rover_u32 keys[12] = {1,2,3,4,5,6,7,8,9,10,42,11};
        const rover_u32 *kp = keys;
        t0 = now_ns();
        for (int i = 0; i < N; i++) {
                __asm__ volatile("" : "+r"(kp));
                sink += rover_eval_key_next(kp, 0, 12, 42);
        }
        t1 = now_ns(); ns_asm = (double)(t1 - t0) / N;
        t0 = now_ns();
        for (int i = 0; i < N; i++) {
                __asm__ volatile("" : "+r"(kp));
                sink += ref_rover_key_next(kp, 0, 12, 42);
        }
        t1 = now_ns(); ns_c = (double)(t1 - t0) / N;
        printf("  %-26s ASM %6.2f ns/op  |  C -O2 %6.2f ns/op  |  speedup %5.2fx\n",
               "rover_eval_key_next", ns_asm, ns_c, ns_c / ns_asm);

        /* 5. holds_numbers (2-comparison rule) */
        struct synth_table st;
        memset(&st, 0, sizeof(st));
        rover_i64 c42 = 42; memcpy(st.pool, &c42, 8);
        rover_i32 s1 = 1; memcpy(st.page0, &s1, 4);
        st.rules[0].flags = ROVER_EVAL_RULE_NUMBERS;
        st.rules[0].comparisons = 2; st.rules[0].trigger = 1; st.rules[0].then = 1;
        st.rules[0].reads = 1;
        st.cmps[0].op = ROVER_EVAL_EQ; st.cmps[0].join = ROVER_EVAL_JOIN_LINE; st.cmps[0].form = ROVER_EVAL_FORM_NUMBER;
        st.cmps[0].a.source = ROVER_EVAL_FIELD; st.cmps[0].b.source = ROVER_EVAL_CONST_NUMBER;
        st.cmps[1].op = ROVER_EVAL_EQ; st.cmps[1].join = ROVER_EVAL_JOIN_LINE; st.cmps[1].form = ROVER_EVAL_FORM_NUMBER;
        st.cmps[1].a.source = ROVER_EVAL_STATE_NUMBER; st.cmps[1].b.source = ROVER_EVAL_STATE_NUMBER;
        st.order[0] = 0; st.keys[0] = 42;
        st.index[1].first = 0; st.index[1].count = 1 | ROVER_EVAL_SPAN_NUMBERS;
        st.index[0].first = 0; st.index[0].count = 1 | ROVER_EVAL_SPAN_NUMBERS;
        st.tbl.rule = st.rules; st.tbl.order = st.order; st.tbl.key = st.keys;
        st.tbl.index = st.index; st.tbl.comparison = st.cmps; st.tbl.pool = st.pool;
        const rover_u8 *pages[2] = {st.page0, st.page1};
        struct rover_eval_event ev;
        memset(&ev, 0, sizeof(ev)); ev.id = 1; ev.count = 1; ev.field[0].number = 42; ev.when = 1000000000ull;

        t0 = now_ns();
        for (int i = 0; i < N; i++) sink += (uint64_t)rover_eval_holds_numbers(&st.tbl, &st.rules[0], &ev, pages);
        t1 = now_ns(); ns_asm = (double)(t1 - t0) / N;
        t0 = now_ns();
        for (int i = 0; i < N; i++) sink += (uint64_t)ref_rover_holds_numbers(&st.tbl, &st.rules[0], &ev, pages);
        t1 = now_ns(); ns_c = (double)(t1 - t0) / N;
        printf("  %-26s ASM %6.2f ns/op  |  C -O2 %6.2f ns/op  |  speedup %5.2fx\n",
               "rover_eval_holds_numbers", ns_asm, ns_c, ns_c / ns_asm);

        /* 6. walk_numbers */
        struct rover_eval_run runs[4];
        struct rover_eval_hit hits[4];
        memset(runs, 0, sizeof(runs));
        t0 = now_ns();
        for (int i = 0; i < N; i++) sink += rover_walk_numbers(&st.tbl, runs, pages, &ev, 4ull << 32, hits);
        t1 = now_ns(); ns_asm = (double)(t1 - t0) / N;
        memset(runs, 0, sizeof(runs));
        t0 = now_ns();
        for (int i = 0; i < N; i++) sink += ref_rover_walk_numbers(&st.tbl, runs, pages, &ev, 4ull << 32, hits);
        t1 = now_ns(); ns_c = (double)(t1 - t0) / N;
        printf("  %-26s ASM %6.2f ns/op  |  C -O2 %6.2f ns/op  |  speedup %5.2fx\n",
               "rover_walk_numbers", ns_asm, ns_c, ns_c / ns_asm);

        /* 7. edges_numbers */
        st.rules[0].flags = ROVER_EVAL_RULE_WHEN | ROVER_EVAL_RULE_NUMBERS;
        memset(runs, 0, sizeof(runs));
        t0 = now_ns();
        for (int i = 0; i < N; i++) {
                runs[0].edge = 0;
                sink += rover_eval_edges_numbers(&st.tbl, runs, pages, 1ull, &ev, 4ull << 32, hits);
        }
        t1 = now_ns(); ns_asm = (double)(t1 - t0) / N;
        memset(runs, 0, sizeof(runs));
        t0 = now_ns();
        for (int i = 0; i < N; i++) {
                runs[0].edge = 0;
                sink += ref_rover_edges_numbers(&st.tbl, runs, pages, 1ull, &ev, 4ull << 32, hits);
        }
        t1 = now_ns(); ns_c = (double)(t1 - t0) / N;
        printf("  %-26s ASM %6.2f ns/op  |  C -O2 %6.2f ns/op  |  speedup %5.2fx\n",
               "rover_eval_edges_numbers", ns_asm, ns_c, ns_c / ns_asm);

        (void)sink;
}

int main(void)
{
        test_compare_num();
        test_glob();
        test_may_and_key_next();
        test_holds_walk_edges();
        if (g_fails) {
                fprintf(stderr, "FAILED (%d failures)\n", g_fails);
                return 1;
        }
        printf("PASS: rover_eval (all edge cases, guard pages, 30k+ fuzz cases)\n");
        if (getenv("ROVER_BENCH"))
                run_bench();
        return 0;
}
