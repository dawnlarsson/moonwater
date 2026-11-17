/*
        test/checks/draw.c: the issue board, draw lists and the GPU assembly.

        The terminal's program, the kit's frames, the draw list and the GPU model's kit run with the four faces:
        fonts/ibm-plex-mono/IBMPlexMono-Regular.ttf fonts/ibm-plex-mono/IBMPlexMono-Bold.ttf
        fonts/inter/Inter-Regular.ttf fonts/inter/Inter-Bold.ttf.

        A run of test/checks.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. A section is built as
        test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib -nostartfiles
        -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its
        freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name>
        test/checks.c).
*/

#ifdef CHECK_issues
#include "../../src/lib.util.c"
#include "../../src/canvas/window.c"
#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted
/*
        The issue board (window.c's windows_issue_set, windows_issue_clear and windows_issues_read): a set is one issue, a set again with the
        same key and code updates it in place, a clear takes one code or all of a key and no other key, a board with no slot free refuses
        the set and counts it until a clear frees one. A reader is held to the seqlock (a writer inside it is a torn read, retried and then
        given up) and to nothing the table says of itself: a count it does not match, a severity that is none, a key or a detail with no
        null, a lock held by a writer that is gone.
*/
static struct windows *iss_table_scratch; // the table, mapped at its first use (windows_scratch_table)
static struct windows_issues iss_copy;

// The slot of a key on a copy, or -1 (the reader keeps each issue in its slot, so a test finds it by its key).
static int iss_at(const struct windows_issues *all, const char *key)
{
        for (int i = 0; i < WINDOWS_ISSUES; i++)
                if (all->issue[i].used && string_equals((string_address)all->issue[i].key, (string_address)key))
                        return i;
        return -1;
}

// 1 when a copy has an issue whose key and detail are at their rooms, each with its null (the reader ended them).
static int iss_long(const struct windows_issues *all)
{
        for (int i = 0; i < WINDOWS_ISSUES; i++)
                if (all->issue[i].used && string_length((string_address)all->issue[i].key) == WINDOWS_ISSUE_KEY - 1 &&
                    string_length((string_address)all->issue[i].detail) == WINDOWS_ISSUE_DETAIL - 1)
                        return 1;
        return 0;
}

static void iss_rows_basic(void)
{
        unsigned int slots[WINDOWS_ISSUES];

        windows_scratch_zero(windows_scratch_table(&iss_table_scratch));
        check("issues: a set on the board is one issue", windows_issue_set(windows_scratch_table(&iss_table_scratch), "canvas gpu", WINDOWS_ISSUE_ERROR, WINDOWS_ISSUE_GPU_MAP, 3, 22, "pictures refused") == 1);
        check("issues: the set is read back, its key, severity, code, numbers and detail whole",
              windows_issues_read(windows_scratch_table(&iss_table_scratch), &iss_copy) == 1 && iss_copy.count == 1 && string_equals((string_address)iss_copy.issue[0].key, (string_address)"canvas gpu") &&
                  iss_copy.issue[0].severity == WINDOWS_ISSUE_ERROR && iss_copy.issue[0].code == WINDOWS_ISSUE_GPU_MAP && iss_copy.issue[0].a == 3 &&
                  iss_copy.issue[0].b == 22 && string_equals((string_address)iss_copy.issue[0].detail, (string_address)"pictures refused"));
        check("issues: the same key and code is updated in place, not added to",
              windows_issue_set(windows_scratch_table(&iss_table_scratch), "canvas gpu", WINDOWS_ISSUE_WARNING, WINDOWS_ISSUE_GPU_MAP, 4, 12, "again") == 1 && windows_scratch_table(&iss_table_scratch)->issue_count == 1);
        windows_issues_read(windows_scratch_table(&iss_table_scratch), &iss_copy);
        check("issues: the update changes the severity, numbers and detail it was set with",
              iss_copy.count == 1 && iss_copy.issue[0].severity == WINDOWS_ISSUE_WARNING && iss_copy.issue[0].a == 4 && iss_copy.issue[0].b == 12 &&
                  string_equals((string_address)iss_copy.issue[0].detail, (string_address)"again"));
        check("issues: the same key with another code is a second issue", windows_issue_set(windows_scratch_table(&iss_table_scratch), "canvas gpu", WINDOWS_ISSUE_WARNING, WINDOWS_ISSUE_GPU_LISTS, 0, 0, null) == 1 && windows_scratch_table(&iss_table_scratch)->issue_count == 2);
        check("issues: another key is an issue of its own", windows_issue_set(windows_scratch_table(&iss_table_scratch), "audio device", WINDOWS_ISSUE_ERROR, WINDOWS_ISSUE_AUDIO_OPEN, 5, 0, "") == 1);
        windows_issues_read(windows_scratch_table(&iss_table_scratch), &iss_copy);
        check("issues: worst severity is per key", windows_issues_worst(&iss_copy, "canvas gpu", null) == WINDOWS_ISSUE_WARNING &&
              windows_issues_worst(&iss_copy, "audio device", null) == WINDOWS_ISSUE_ERROR && windows_issues_worst(&iss_copy, "wifi", null) == 0);
        check("issues: the count of a key is its issues", windows_issues_worst(&iss_copy, "canvas gpu", slots) == WINDOWS_ISSUE_WARNING && slots[0] == 2);
        check("issues: a clear of one code leaves the others of the key", windows_issue_clear(windows_scratch_table(&iss_table_scratch), "canvas gpu", WINDOWS_ISSUE_GPU_LISTS) == 1 && windows_scratch_table(&iss_table_scratch)->issue_count == 2);
        check("issues: a clear of every code takes the key's issues and no other key's", windows_issue_clear(windows_scratch_table(&iss_table_scratch), "canvas gpu", WINDOWS_ISSUE_ANY) == 1 && windows_scratch_table(&iss_table_scratch)->issue_count == 1);
        check("issues: a clear of a key that has none changes nothing", windows_issue_clear(windows_scratch_table(&iss_table_scratch), "canvas gpu", WINDOWS_ISSUE_ANY) == 0 && windows_scratch_table(&iss_table_scratch)->issue_count == 1);
        windows_issues_read(windows_scratch_table(&iss_table_scratch), &iss_copy);
        check("issues: after the clears the copy holds the other key's one issue", iss_copy.count == 1 && iss_at(&iss_copy, "audio device") >= 0);
        check("issues: a set with no key, a bad severity or no code is refused",
              windows_issue_set(windows_scratch_table(&iss_table_scratch), null, WINDOWS_ISSUE_ERROR, WINDOWS_ISSUE_GPU_MAP, 0, 0, null) == 0 &&
                  windows_issue_set(windows_scratch_table(&iss_table_scratch), "", WINDOWS_ISSUE_ERROR, WINDOWS_ISSUE_GPU_MAP, 0, 0, null) == 0 &&
                  windows_issue_set(windows_scratch_table(&iss_table_scratch), "canvas gpu", 0, WINDOWS_ISSUE_GPU_MAP, 0, 0, null) == 0 &&
                  windows_issue_set(windows_scratch_table(&iss_table_scratch), "canvas gpu", 3, WINDOWS_ISSUE_GPU_MAP, 0, 0, null) == 0 &&
                  windows_issue_set(windows_scratch_table(&iss_table_scratch), "canvas gpu", WINDOWS_ISSUE_ERROR, WINDOWS_ISSUE_NONE, 0, 0, null) == 0 &&
                  windows_issue_set(windows_scratch_table(&iss_table_scratch), "canvas gpu", WINDOWS_ISSUE_ERROR, WINDOWS_ISSUE_COUNT, 0, 0, null) == 0 && windows_scratch_table(&iss_table_scratch)->issue_count == 1);
        {
                static struct windows *notified_scratch;
                struct windows *const notified_at = windows_scratch_table(&notified_scratch);

                check("issues: a program's notification is a code the board takes, no kernel notice's, with a name for moonwater debug",
                      windows_issue_set(notified_at, "files", WINDOWS_ISSUE_WARNING, WINDOWS_ISSUE_NOTIFICATION, 7, 1, "copied") == 1 &&
                          notified_at->issue_count == 1 && notified_at->issues[0].code == WINDOWS_ISSUE_NOTIFICATION && notified_at->issues[0].a == 7 &&
                          !windows_issue_kernel(WINDOWS_ISSUE_NOTIFICATION) && windows_issue_names[WINDOWS_ISSUE_NOTIFICATION] != null);
        }
        {
                char wide[WINDOWS_ISSUE_DETAIL * 2];

                memory_fill(wide, 'w', sizeof(wide) - 1);
                wide[sizeof(wide) - 1] = 0;
                check("issues: a key past its room is cut to it, and the detail to its room",
                      windows_issue_set(windows_scratch_table(&iss_table_scratch), "a key that is far longer than the room it has", WINDOWS_ISSUE_WARNING, WINDOWS_ISSUE_GPU_COOLING, 0, 0, wide) == 1);
        }
        windows_issues_read(windows_scratch_table(&iss_table_scratch), &iss_copy);
        check("issues: the long key is cut to the key's room, the long detail to the detail's, both ended",
              iss_copy.count == 2 && iss_long(&iss_copy) == 1);
}

static void iss_rows_full(void)
{
        char key[WINDOWS_ISSUE_KEY];

        windows_scratch_zero(windows_scratch_table(&iss_table_scratch));
        for (int i = 0; i < WINDOWS_ISSUES; i++)
        {
                key[0] = 'k', key[1] = (char)('a' + i / 26), key[2] = (char)('a' + i % 26), key[3] = 0;
                if (windows_issue_set(windows_scratch_table(&iss_table_scratch), key, WINDOWS_ISSUE_WARNING, WINDOWS_ISSUE_GPU_LISTS, (unsigned int)i, 0, null) != 1)
                        break;
        }
        check("issues: a board takes as many issues as it has slots", windows_scratch_table(&iss_table_scratch)->issue_count == WINDOWS_ISSUES);
        check("issues: a full board refuses the next set, counted, and holds the others",
              windows_issue_set(windows_scratch_table(&iss_table_scratch), "kzz", WINDOWS_ISSUE_ERROR, WINDOWS_ISSUE_AUDIO_OPEN, 0, 0, null) == 0 && windows_scratch_table(&iss_table_scratch)->issue_refused == 1 &&
                  windows_scratch_table(&iss_table_scratch)->issue_count == WINDOWS_ISSUES);
        check("issues: an update of an issue the full board holds is still made", windows_issue_set(windows_scratch_table(&iss_table_scratch), "kaa", WINDOWS_ISSUE_ERROR, WINDOWS_ISSUE_GPU_LISTS, 9, 0, null) == 1);
        windows_issues_read(windows_scratch_table(&iss_table_scratch), &iss_copy);
        check("issues: the reader sees the refusals and the full count", iss_copy.count == WINDOWS_ISSUES && iss_copy.refused == 1);
        check("issues: a clear frees a slot, and the refusals are counted again from zero",
              windows_issue_clear(windows_scratch_table(&iss_table_scratch), "kab", WINDOWS_ISSUE_ANY) == 1 && windows_scratch_table(&iss_table_scratch)->issue_refused == 0);
        check("issues: a refused key takes the freed slot the next time it is set",
              windows_issue_set(windows_scratch_table(&iss_table_scratch), "kzz", WINDOWS_ISSUE_ERROR, WINDOWS_ISSUE_AUDIO_OPEN, 0, 0, null) == 1 && windows_scratch_table(&iss_table_scratch)->issue_count == WINDOWS_ISSUES);
}

static void iss_rows_torn(void)
{
        windows_scratch_zero(windows_scratch_table(&iss_table_scratch));
        windows_issue_set(windows_scratch_table(&iss_table_scratch), "canvas gpu", WINDOWS_ISSUE_ERROR, WINDOWS_ISSUE_GPU_MAP, 1, 2, "x");
        memory_zero(&iss_copy, sizeof(iss_copy));
        windows_scratch_table(&iss_table_scratch)->issue_seq |= 1u;
        check("issues: a writer inside the board is a torn read, given up after its tries, and the copy is empty",
              windows_issues_read(windows_scratch_table(&iss_table_scratch), &iss_copy) == 0 && iss_copy.count == 0);
        windows_scratch_table(&iss_table_scratch)->issue_seq += 1u;
        check("issues: a board the writer left whole is read again", windows_issues_read(windows_scratch_table(&iss_table_scratch), &iss_copy) == 1 && iss_copy.count == 1);
        windows_scratch_table(&iss_table_scratch)->issue_lock = 1;
        check("issues: a set while a dead writer holds the lock is refused, and nothing is written",
              windows_issue_set(windows_scratch_table(&iss_table_scratch), "canvas wifi", WINDOWS_ISSUE_WARNING, WINDOWS_ISSUE_GPU_LISTS, 0, 0, null) == 0 && windows_scratch_table(&iss_table_scratch)->issue_count == 1);
        check("issues: a clear while the lock is held does nothing", windows_issue_clear(windows_scratch_table(&iss_table_scratch), "canvas gpu", WINDOWS_ISSUE_ANY) == 0 && windows_scratch_table(&iss_table_scratch)->issue_count == 1);
        windows_scratch_table(&iss_table_scratch)->issue_lock = 0;
}

static void iss_rows_hostile(void)
{
        windows_scratch_zero(windows_scratch_table(&iss_table_scratch));
        windows_issue_set(windows_scratch_table(&iss_table_scratch), "canvas gpu", WINDOWS_ISSUE_ERROR, WINDOWS_ISSUE_GPU_MAP, 1, 2, "fine");
        windows_scratch_table(&iss_table_scratch)->issue_count = 0xffffffffu;
        windows_issues_read(windows_scratch_table(&iss_table_scratch), &iss_copy);
        check("issues: a count the table claims that the slots do not hold is not trusted", iss_copy.count == 1);
        windows_scratch_table(&iss_table_scratch)->issues[5].used = 1, windows_scratch_table(&iss_table_scratch)->issues[5].severity = 7, windows_scratch_table(&iss_table_scratch)->issues[5].code = WINDOWS_ISSUE_GPU_MAP;
        memory_copy(windows_scratch_table(&iss_table_scratch)->issues[5].key, "canvas wifi", 12);
        windows_issues_read(windows_scratch_table(&iss_table_scratch), &iss_copy);
        check("issues: a slot whose severity is none is no issue", iss_copy.count == 1);
        windows_scratch_table(&iss_table_scratch)->issues[6].used = 1, windows_scratch_table(&iss_table_scratch)->issues[6].severity = WINDOWS_ISSUE_WARNING, windows_scratch_table(&iss_table_scratch)->issues[6].code = WINDOWS_ISSUE_GPU_MAP;
        memory_zero(windows_scratch_table(&iss_table_scratch)->issues[6].key, sizeof(windows_scratch_table(&iss_table_scratch)->issues[6].key));
        windows_issues_read(windows_scratch_table(&iss_table_scratch), &iss_copy);
        check("issues: a slot with an empty key is no issue", iss_copy.count == 1);
        for (int i = 0; i < WINDOWS_ISSUE_KEY; i++)
                windows_scratch_table(&iss_table_scratch)->issues[7].key[i] = 'q';
        windows_scratch_table(&iss_table_scratch)->issues[7].used = 1, windows_scratch_table(&iss_table_scratch)->issues[7].severity = WINDOWS_ISSUE_WARNING, windows_scratch_table(&iss_table_scratch)->issues[7].code = WINDOWS_ISSUE_GPU_LISTS;
        for (int i = 0; i < WINDOWS_ISSUE_DETAIL; i++)
                windows_scratch_table(&iss_table_scratch)->issues[7].detail[i] = 'd';
        windows_issues_read(windows_scratch_table(&iss_table_scratch), &iss_copy);
        check("issues: a key or a detail with no null is ended by the reader",
              iss_copy.count == 2 && iss_long(&iss_copy) == 1);
        windows_scratch_table(&iss_table_scratch)->issues[8].used = 1, windows_scratch_table(&iss_table_scratch)->issues[8].severity = WINDOWS_ISSUE_ERROR, windows_scratch_table(&iss_table_scratch)->issues[8].code = 9999u;
        memory_copy(windows_scratch_table(&iss_table_scratch)->issues[8].key, "canvas odd", 11);
        windows_issues_read(windows_scratch_table(&iss_table_scratch), &iss_copy);
        check("issues: a code past the list is kept as the code it is, and no crash", iss_copy.count == 3 && iss_copy.issue[8].code == 9999u);
        check("issues: a worst severity of a key with a bad slot is the good one's", windows_issues_worst(&iss_copy, "canvas gpu", null) == WINDOWS_ISSUE_ERROR);
        check("issues: a null table or copy is refused", windows_issue_set(null, "canvas gpu", WINDOWS_ISSUE_ERROR, WINDOWS_ISSUE_GPU_MAP, 0, 0, null) == 0 &&
              windows_issues_read(null, &iss_copy) == 0 && windows_issues_read(windows_scratch_table(&iss_table_scratch), null) == 0);
}

// The GPU's issues from the table (windows_gpu_wanted) and its status (windows_gpu_judge): each a row of its own.
static struct windows_issue iss_want[WINDOWS_GPU_CODES];

static unsigned int iss_wanted_has(unsigned int count, unsigned int code)
{
        for (unsigned int k = 0; k < count; k++)
                if (iss_want[k].code == code)
                        return k + 1;
        return 0;
}

static void iss_rows_gpu(void)
{
        unsigned int count;
        static struct windows *gpu_scratch;
        struct windows *const gpu_at = windows_scratch_fresh(&gpu_scratch); // zero, as the table on the stack was

        windows_scratch_zero(gpu_at);
        gpu_at->gpu_on = 1, gpu_at->gpu_able = 1;
        count = windows_gpu_wanted(gpu_at, iss_want, WINDOWS_GPU_CODES);
        check("gpu issues: a working GPU with no refusals raises none", count == 0);
        gpu_at->gpu_map_why = 3u | 19u << 8;
        count = windows_gpu_wanted(gpu_at, iss_want, WINDOWS_GPU_CODES);
        check("gpu issues: a map refused at step 3 with error 19 is the missing userptr, an error",
              iss_wanted_has(count, WINDOWS_ISSUE_GPU_USERPTR) && iss_want[iss_wanted_has(count, WINDOWS_ISSUE_GPU_USERPTR) - 1].severity == WINDOWS_ISSUE_ERROR &&
                  !iss_wanted_has(count, WINDOWS_ISSUE_GPU_MAP));
        gpu_at->gpu_map_why = 3u | 22u << 8;
        count = windows_gpu_wanted(gpu_at, iss_want, WINDOWS_GPU_CODES);
        check("gpu issues: another error at step 3 is the generic map error with its numbers",
              iss_wanted_has(count, WINDOWS_ISSUE_GPU_MAP) && !iss_wanted_has(count, WINDOWS_ISSUE_GPU_USERPTR) &&
                  iss_want[iss_wanted_has(count, WINDOWS_ISSUE_GPU_MAP) - 1].a == 3 && iss_want[iss_wanted_has(count, WINDOWS_ISSUE_GPU_MAP) - 1].b == 22);
        memory_copy(gpu_at->gpu_said, "amdgpu: HMM_MIRROR kernel config option is not enabled", 52);
        gpu_at->gpu_map_why = 2u | 12u << 8;
        count = windows_gpu_wanted(gpu_at, iss_want, WINDOWS_GPU_CODES);
        check("gpu issues: the kernel's words naming HMM_MIRROR are the userptr case, whatever the step",
              iss_wanted_has(count, WINDOWS_ISSUE_GPU_USERPTR) && !iss_wanted_has(count, WINDOWS_ISSUE_GPU_MAP));
        memory_zero(gpu_at->gpu_said, sizeof(gpu_at->gpu_said));
        gpu_at->gpu_map_why = 0;
        gpu_at->gpu_refused[9] = 12, gpu_at->gpu_drawn = 10, gpu_at->gpu_processor = 90;
        count = windows_gpu_wanted(gpu_at, iss_want, WINDOWS_GPU_CODES);
        check("gpu issues: a refusal reason shows when it refused enough presents, with its count and its reason's index",
              iss_wanted_has(count, WINDOWS_ISSUE_GPU_REFUSED) && iss_want[iss_wanted_has(count, WINDOWS_ISSUE_GPU_REFUSED) - 1].a == 12 &&
                  iss_want[iss_wanted_has(count, WINDOWS_ISSUE_GPU_REFUSED) - 1].b == 9);
        gpu_at->gpu_refused[9] = 3;
        count = windows_gpu_wanted(gpu_at, iss_want, WINDOWS_GPU_CODES);
        check("gpu issues: fewer refusals than the minimum raise none", !iss_wanted_has(count, WINDOWS_ISSUE_GPU_REFUSED));
        gpu_at->gpu_refused[9] = 0, gpu_at->gpu_able = 0;
        count = windows_gpu_wanted(gpu_at, iss_want, WINDOWS_GPU_CODES);
        check("gpu issues: lists off with the GPU working says so, a warning", iss_wanted_has(count, WINDOWS_ISSUE_GPU_LISTS) && iss_want[iss_wanted_has(count, WINDOWS_ISSUE_GPU_LISTS) - 1].severity == WINDOWS_ISSUE_WARNING);
        gpu_at->gpu_able = 1, gpu_at->gpu_state = WINDOWS_GPU_UNTRUSTED, gpu_at->gpu_family = 144;
        count = windows_gpu_wanted(gpu_at, iss_want, WINDOWS_GPU_CODES);
        check("gpu issues: an untrusted card is an error with its family", iss_wanted_has(count, WINDOWS_ISSUE_GPU_UNTRUSTED) && iss_want[iss_wanted_has(count, WINDOWS_ISSUE_GPU_UNTRUSTED) - 1].a == 144);
        gpu_at->gpu_state = WINDOWS_GPU_NO_CARD;
        count = windows_gpu_wanted(gpu_at, iss_want, WINDOWS_GPU_CODES);
        check("gpu issues: a card of no family we have kernels for is a warning with its family", iss_wanted_has(count, WINDOWS_ISSUE_GPU_FAMILY) && !iss_wanted_has(count, WINDOWS_ISSUE_GPU_UNTRUSTED));
        gpu_at->gpu_family = 0, gpu_at->gpu_state = WINDOWS_GPU_COOLING, gpu_at->gpu_steps = 2;
        count = windows_gpu_wanted(gpu_at, iss_want, WINDOWS_GPU_CODES);
        check("gpu issues: cooling down after hangs is a warning with the count", iss_wanted_has(count, WINDOWS_ISSUE_GPU_COOLING) && iss_want[iss_wanted_has(count, WINDOWS_ISSUE_GPU_COOLING) - 1].a == 2);
        gpu_at->gpu_state = WINDOWS_GPU_UP, gpu_at->gpu_steps = 0, gpu_at->list_tried[0] = 100, gpu_at->list_lost[0][0] = 4, gpu_at->pid[0] = 7;
        count = windows_gpu_wanted(gpu_at, iss_want, WINDOWS_GPU_CODES);
        check("gpu issues: lost frames under a tenth of those tried raise none", !iss_wanted_has(count, WINDOWS_ISSUE_GPU_LOST));
        gpu_at->list_lost[0][0] = 20;
        count = windows_gpu_wanted(gpu_at, iss_want, WINDOWS_GPU_CODES);
        check("gpu issues: lost frames over a tenth of those tried are a warning with both counts",
              iss_wanted_has(count, WINDOWS_ISSUE_GPU_LOST) && iss_want[iss_wanted_has(count, WINDOWS_ISSUE_GPU_LOST) - 1].a == 20 &&
                  iss_want[iss_wanted_has(count, WINDOWS_ISSUE_GPU_LOST) - 1].b == 100);
        gpu_at->refused = 1, gpu_at->refused_error = 22;
        count = windows_gpu_wanted(gpu_at, iss_want, WINDOWS_GPU_CODES);
        check("gpu issues: a framebuffer the card refused for scan-out is a warning with the kernel's error", iss_wanted_has(count, WINDOWS_ISSUE_GPU_SCANOUT) && iss_want[iss_wanted_has(count, WINDOWS_ISSUE_GPU_SCANOUT) - 1].a == 22);
        gpu_at->gpu_on = 0;
        count = windows_gpu_wanted(gpu_at, iss_want, WINDOWS_GPU_CODES);
        check("gpu issues: with the setting off there are none, whatever the counters say", count == 0);
        check("gpu status: the setting off is off", windows_gpu_judge(gpu_at, WINDOWS_GPU_STATUS_IN_USE, 90, 10) == WINDOWS_GPU_STATUS_OFF);
        gpu_at->gpu_on = 1;
        check("gpu status: a card that is none is no supported GPU", (gpu_at->gpu_state = WINDOWS_GPU_NO_CARD, windows_gpu_judge(gpu_at, WINDOWS_GPU_STATUS_NOT, 0, 50)) == WINDOWS_GPU_STATUS_NONE);
        gpu_at->gpu_state = WINDOWS_GPU_UP;
        check("gpu status: almost all presents on the card are in use", windows_gpu_judge(gpu_at, WINDOWS_GPU_STATUS_NOT, 95, 5) == WINDOWS_GPU_STATUS_IN_USE);
        check("gpu status: half is partly in use", windows_gpu_judge(gpu_at, WINDOWS_GPU_STATUS_NOT, 50, 50) == WINDOWS_GPU_STATUS_PARTLY);
        check("gpu status: none on the card is not in use", windows_gpu_judge(gpu_at, WINDOWS_GPU_STATUS_PARTLY, 0, 100) == WINDOWS_GPU_STATUS_NOT);
        check("gpu status: in use stays in use at 70 percent, where it would not be entered again", windows_gpu_judge(gpu_at, WINDOWS_GPU_STATUS_IN_USE, 70, 30) == WINDOWS_GPU_STATUS_IN_USE);
        check("gpu status: not in use is not left at 70 percent", windows_gpu_judge(gpu_at, WINDOWS_GPU_STATUS_NOT, 70, 30) == WINDOWS_GPU_STATUS_PARTLY);
        check("gpu status: fewer than sixteen presents keep the answer", windows_gpu_judge(gpu_at, WINDOWS_GPU_STATUS_IN_USE, 3, 2) == WINDOWS_GPU_STATUS_IN_USE);
}

// The metrics' helpers (windows_metric_stats and windows_metric_rate): the mean and the worst of a ring in microseconds, and a rate per second.
static void iss_rows_metrics(void)
{
        unsigned long long ring[WINDOWS_METRIC_RUN + 40];
        unsigned int mean = 99, worst = 99;

        memory_zero(ring, sizeof(ring));
        windows_metric_stats(ring, 0, &mean, &worst);
        check("metrics: an empty ring has a mean and a worst of none", mean == 0 && worst == 0);
        ring[0] = 1000, ring[1] = 3000, ring[2] = 2000;
        windows_metric_stats(ring, 3, &mean, &worst);
        check("metrics: the mean and the worst of three presents, in microseconds", mean == 2 && worst == 3);
        for (unsigned int i = 0; i < WINDOWS_METRIC_RUN + 40; i++)
                ring[i] = 5000;
        ring[WINDOWS_METRIC_RUN + 10] = 90000000ull;
        windows_metric_stats(ring, WINDOWS_METRIC_RUN + 40, &mean, &worst);
        check("metrics: a ring is read as its last 128 samples, and its worst is among them", mean == 5 && worst == 5);
        ring[7] = 40000000ull;
        windows_metric_stats(ring, WINDOWS_METRIC_RUN, &mean, &worst);
        check("metrics: a sample in the ring is its worst, and the mean follows it", worst == 40000 && mean == 5 + (40000 - 5) / 128);
        check("metrics: a rate is the count per second over the nanoseconds it took", windows_metric_rate(10, 2000000000ull) == 5 && windows_metric_rate(0, 1000) == 0);
        check("metrics: no time at all is no rate, not a division by zero", windows_metric_rate(10, 0) == 0);

        // The expander's lines (windows_issue_lines): none while shut, a message and a detail each while open.
        {
                struct windows_issues board;

                memory_zero(&board, sizeof(board));
                board.issue[0].used = 1, board.issue[0].severity = WINDOWS_ISSUE_WARNING, board.issue[0].code = WINDOWS_ISSUE_GPU_LISTS;
                string_copy_bounded((string_address)board.issue[0].key, (string_address)"canvas gpu", WINDOWS_ISSUE_KEY);
                board.issue[1].used = 1, board.issue[1].severity = WINDOWS_ISSUE_ERROR, board.issue[1].code = WINDOWS_ISSUE_GPU_MAP;
                string_copy_bounded((string_address)board.issue[1].key, (string_address)"canvas gpu", WINDOWS_ISSUE_KEY);
                string_copy_bounded((string_address)board.issue[1].detail, (string_address)"the kernel's words", WINDOWS_ISSUE_DETAIL);
                board.issue[2].used = 1, board.issue[2].severity = WINDOWS_ISSUE_WARNING, board.issue[2].code = WINDOWS_ISSUE_GPU_REQUIRES;
                string_copy_bounded((string_address)board.issue[2].key, (string_address)"canvas gpu", WINDOWS_ISSUE_KEY);
                string_copy_bounded((string_address)board.issue[2].detail, (string_address)"a fix the message says", WINDOWS_ISSUE_DETAIL);
                check("expander lines: a shut expander shows none", windows_issue_lines(&board, "canvas gpu", 0) == 0);
                check("expander lines: three issues open show a message each, and the detail of those that have one (not a prerequisite's fix)",
                      windows_issue_lines(&board, "canvas gpu", 1) == 4);
                check("expander lines: another key's issues are none of these", windows_issue_lines(&board, "audio device", 1) == 0);
        }
        // The sound card's issues (audio/service.c's audio_board_set and audio_board_clear, under the audio device row's key): a card that would not
        // open is an error, a rate it did not take a warning, and a card that opens clears both.
        {
                static struct windows *audio_scratch;
                struct windows *const audio_at = windows_scratch_fresh(&audio_scratch); // zero, as the table on the stack was
                struct windows_issues copy;

                windows_scratch_zero(audio_at);
                check("audio board: a card that would not open is an error under the audio device key",
                      windows_issue_set(audio_at, "audio device", WINDOWS_ISSUE_ERROR, WINDOWS_ISSUE_AUDIO_OPEN, (unsigned int)-5, 0, "") == 1);
                check("audio board: a rate the card did not take is a warning with both rates",
                      windows_issue_set(audio_at, "audio device", WINDOWS_ISSUE_WARNING, WINDOWS_ISSUE_AUDIO_RATE, 48000, 44100, "") == 1);
                windows_issues_read(audio_at, &copy);
                check("audio board: both are read back, the worst is the error", windows_issues_worst(&copy, "audio device", null) == WINDOWS_ISSUE_ERROR &&
                      windows_issues_worst(&copy, "audio device", null) == WINDOWS_ISSUE_ERROR && copy.count == 2);
                check("audio board: a card that opens clears its open error and leaves the rate's warning when the rate is fine",
                      windows_issue_clear(audio_at, "audio device", WINDOWS_ISSUE_AUDIO_OPEN) == 1 && windows_issue_clear(audio_at, "audio device", WINDOWS_ISSUE_AUDIO_RATE) == 1);
                windows_issues_read(audio_at, &copy);
                check("audio board: with both cleared the device has no issue", windows_issues_worst(&copy, "audio device", null) == 0 && copy.count == 0);
        }
}

// The kernel notices (window.c's kernel_notice_class, kernel_notices_record, kernel_notices_publish): lines the box's own log showed, with the level
// the kernel gave each (the journal does not keep it; the level is what the driver prints them at), what class they are and their severity. A
// code of 0 is a line that is not raised.
struct iss_kernel_line { unsigned int level; const char *text; unsigned int code, severity; };
static const struct iss_kernel_line iss_kernel_lines[] = {
    {3, "amdgpu 0000:14:00.0: [drm] *ERROR* Unsupported screen format RA24 little-endian (0x34324152)", WINDOWS_ISSUE_KERNEL_GPU, WINDOWS_ISSUE_ERROR},
    {3, "amdgpu 0000:14:00.0: [drm] *ERROR* LTTPR count is nonzero but invalid lane count reported. Assuming no LTTPR present.", 0, 0},
    {1, "[moonwater] Moonwater starting...", 0, 0},
    {1, "[moonwater] Spark format unregistered", 0, 0},
    {1, "[moonwater] could not register /dev/spark: -16", WINDOWS_ISSUE_KERNEL_SYSTEM, WINDOWS_ISSUE_ERROR},
    {3, "amdgpu 0000:14:00.0: ring comp_1.0.0 timeout, signaled seq=1234, emitted seq=1235", WINDOWS_ISSUE_KERNEL_GPU, WINDOWS_ISSUE_ERROR},
    {4, "amdgpu 0000:14:00.0: Starting comp_1.0.0 ring reset", WINDOWS_ISSUE_KERNEL_GPU, WINDOWS_ISSUE_WARNING},
    {3, "amdgpu 0000:14:00.0: [gfxhub] page fault (src_id:0 ring:24 vmid:8 pasid:32768)", WINDOWS_ISSUE_KERNEL_GPU, WINDOWS_ISSUE_ERROR},
    {3, "amdgpu 0000:14:00.0:   in page starting at address 0x0000000000000000 from client 18 (UTCL2)", WINDOWS_ISSUE_KERNEL_GPU, WINDOWS_ISSUE_ERROR},
    {4, "amdgpu 0000:14:00.0: Failed to setup vendor infoframe on connector HDMI-A-1: -22", 0, 0},
    {6, "amdgpu 0000:14:00.0: GPU reset begin!", 0, 0}, // the kernel's info, after the error line of the ring that timed out, which is what is raised
    {5, "amdgpu 0000:14:00.0: GPU reset begin! on a ring timeout", 0, 0}, // a notice is not a fault, whatever else the line says
    {3, "amdgpu 0000:16:00.0: ring comp_1.3.0 timeout, signaled seq=10, emitted seq=13", WINDOWS_ISSUE_KERNEL_GPU, WINDOWS_ISSUE_ERROR},
    {3, "amdgpu 0000:16:00.0: Failed to load firmware \"amdgpu/psp_13_0_0_sos.bin\"", WINDOWS_ISSUE_KERNEL_GPU, WINDOWS_ISSUE_ERROR},
    {6, "amdgpu 0000:16:00.0: Trusted Memory Zone (TMZ) feature disabled as experimental (default)", 0, 0},
    {4, "amdgpu 0000:16:00.0: Trusted Memory Zone (TMZ) feature disabled as experimental (default)", 0, 0}, // "fault" is not in "default"
    {6, "i915 0000:00:02.0: [drm] [ENCODER:113:DDI A/PHY A] failed to retrieve link info, disabling eDP", WINDOWS_ISSUE_KERNEL_GPU, WINDOWS_ISSUE_WARNING},
    {6, "i915 0000:00:02.0: [drm] Finished loading DMC firmware i915/kbl_dmc_ver1_04.bin (v1.4)", 0, 0},
    {6, "[drm:amdgpu_dm_init] DM_PPLIB: values for V-Blank", 0, 0},
    {3, "snd_hda_intel 0000:16:00.6: no codecs found!", 0, 0},
    {3, "snd_hda_intel 0000:14:00.6: no codecs found!", 0, 0},
    {4, "snd_hda_intel 0000:14:00.6: codec error timeout", WINDOWS_ISSUE_KERNEL_AUDIO, WINDOWS_ISSUE_WARNING},
    {4, "FAT-fs (nvme0n1p1): Volume was not properly unmounted. Some data may be corrupt. Please run fsck.", WINDOWS_ISSUE_KERNEL_STORAGE, WINDOWS_ISSUE_WARNING},
    {3, "EXT4-fs error (device sda1): ext4_find_entry:1455: inode #2: comm ls: reading directory lblock 0", WINDOWS_ISSUE_KERNEL_STORAGE, WINDOWS_ISSUE_ERROR},
    {3, "Buffer I/O error on dev sdb1, logical block 0, async page read", WINDOWS_ISSUE_KERNEL_STORAGE, WINDOWS_ISSUE_ERROR},
    {3, "blk_update_request: I/O error, dev sda, sector 1234 op 0x0:(READ) flags 0x0 phys_seg 1 prio class 2", WINDOWS_ISSUE_KERNEL_STORAGE, WINDOWS_ISSUE_ERROR},
    {6, "EXT4-fs (nvme1n1p2): mounted filesystem 0a9d7823-8415-47dc-bb85-94d069364491 r/w with ordered data mode. Quota mode: disabled.", 0, 0},
    {6, "nvme nvme0: 8/0/0 default/read/poll queues", 0, 0},
    {3, "nvme nvme0: I/O 123 QID 3 timeout, aborting", WINDOWS_ISSUE_KERNEL_STORAGE, WINDOWS_ISSUE_ERROR},
    {4, "workqueue: name exceeds WQ_NAME_LEN. Truncating to: hdmi_frl_status_polling_workque", 0, 0},
    {4, "xhci_hcd 0000:00:14.0: WARN: buffer overrun event for slot 3 ep 2 on endpoint", WINDOWS_ISSUE_KERNEL_USB, WINDOWS_ISSUE_WARNING},
    {3, "usb 1-3: device descriptor read/64, error -71", WINDOWS_ISSUE_KERNEL_USB, WINDOWS_ISSUE_ERROR},
    {6, "usb 1-1: USB disconnect, device number 2", 0, 0},
    {4, "usb 1-1: USB disconnect, device number 2", 0, 0}, // a device unplugged is a person's, not a fault, at the level it is printed at or any
    {6, "xhci_hcd 0000:00:14.0: xHCI Host Controller", 0, 0},
    {6, "usb 1-1: New USB device found, idVendor=046d, idProduct=c52b, bcdDevice=12.11", 0, 0},
    {4, "usb usb3: We don't know the algorithms for LPM for this host, disabling LPM.", 0, 0},
    {3, "iwlwifi 0000:00:14.3: Microcode SW error detected. Restarting 0x2000000.", WINDOWS_ISSUE_KERNEL_WIFI, WINDOWS_ISSUE_ERROR},
    {4, "iwlwifi 0000:00:14.3: Direct firmware load for iwlwifi-cc-a0-77.ucode failed with error -2", 0, 0},
    {6, "iwlwifi 0000:00:14.3: loaded firmware version 77.a0fb5e42.0 cc-a0-77.ucode op_mode iwlmvm", 0, 0},
    {3, "iwlwifi 0000:00:14.3: Failed to start RT ucode: -110", WINDOWS_ISSUE_KERNEL_WIFI, WINDOWS_ISSUE_ERROR},
    {6, "mt7921e 0000:0c:00.0: Loading firmware patch: mediatek/WIFI_MT7922_patch_mcu_1_1_hdr.bin", 0, 0},
    {3, "mt7921e 0000:0c:00.0: Message 00020007 (seq 1) timeout", WINDOWS_ISSUE_KERNEL_WIFI, WINDOWS_ISSUE_ERROR},
    {6, "cfg80211: failed to load regulatory.db", 0, 0},
    {3, "Bluetooth: hci0: command 0x0c03 tx timeout", WINDOWS_ISSUE_KERNEL_BLUETOOTH, WINDOWS_ISSUE_ERROR},
    {3, "Bluetooth: hci0: Opcode 0x0c03 failed: -110", WINDOWS_ISSUE_KERNEL_BLUETOOTH, WINDOWS_ISSUE_ERROR},
    {6, "Bluetooth: Core ver 2.22", 0, 0},
    {3, "mce: [Hardware Error]: CPU 0: Machine Check: 0 Bank 1: be00000000800400", WINDOWS_ISSUE_KERNEL_SYSTEM, WINDOWS_ISSUE_ERROR},
    {3, "thermal thermal_zone3: critical temperature reached (105 C), shutting down", WINDOWS_ISSUE_KERNEL_SYSTEM, WINDOWS_ISSUE_ERROR},
    {4, "CPU0: Package temperature above threshold, cpu clock throttled (total events = 1)", WINDOWS_ISSUE_KERNEL_SYSTEM, WINDOWS_ISSUE_WARNING},
    {6, "thermal_sys: Registered thermal governor 'step_wise'", 0, 0},
    {6, "ACPI: thermal: Thermal Zone [TZ00] (28 C)", 0, 0},
    {6, "acpi LNXTHERM:00: registered as thermal_zone0", 0, 0},
    {4, "wmi_bus wmi_bus-PNP0C14:03: [Firmware Bug]: WQBC data block query control method not found", 0, 0},
    {3, "wmi_bus wmi_bus-PNP0C14:03: [Firmware Bug]: WQBC data block query control method not found", 0, 0}, // the board's firmware, said even at error
    {6, "PCI: Using host bridge windows from ACPI; if necessary, use \"pci=nocrs\" and report a bug", 0, 0},
    {4, "ACPI BIOS Error (bug): Could not resolve symbol [_SB.PCI0.GPP8.PEGP._DSM], AE_NOT_FOUND", 0, 0},
    {3, "ACPI Error: Aborting method \\_SB.PCI0.SAT0.SPT0._SDD due to previous error (AE_NOT_FOUND) (20260408/psparse-543)", 0, 0},
    {4, "RDSEED32 is broken. Disabling the corresponding CPUID bit.", 0, 0},
    {6, "virt/tdx: TDX not supported by the host platform", 0, 0},
    {4, "kauditd_printk_skb: 225 callbacks suppressed", 0, 0},
    {2, "Out of memory: Killed process 4321 (ld) total-vm:1048576kB, anon-rss:524288kB", WINDOWS_ISSUE_KERNEL_SYSTEM, WINDOWS_ISSUE_ERROR},
    {4, "oom_kill_process.cold+0x2c/0x7d", WINDOWS_ISSUE_KERNEL_SYSTEM, WINDOWS_ISSUE_ERROR}, // a Call Trace's frame, printed at the default level, 4
    {6, "oom_kill_process.cold+0x2c/0x7d", 0, 0},
    {2, "BUG: unable to handle page fault for address: ffff8880a1b2c3d4", WINDOWS_ISSUE_KERNEL_SYSTEM, WINDOWS_ISSUE_ERROR},
    {4, "WARNING: CPU: 3 PID: 1 at kernel/sched/core.c:1234 __schedule+0x1f", WINDOWS_ISSUE_KERNEL_SYSTEM, WINDOWS_ISSUE_ERROR},
    {4, "check[1234]: segfault at 0 ip 0000000000401000 sp 00007ffd error 4 in check[400000+1000]", WINDOWS_ISSUE_KERNEL_PROGRAM, WINDOWS_ISSUE_WARNING},
    {4, "bash[99]: segfault at 7f00 ip 00007f1 sp 00007f0 error 6 likely on CPU 2", WINDOWS_ISSUE_KERNEL_PROGRAM, WINDOWS_ISSUE_WARNING},
    {6, "bash[99]: segfault at 7f00 ip 00007f1 sp 00007f0 error 6 likely on CPU 2", WINDOWS_ISSUE_KERNEL_PROGRAM, WINDOWS_ISSUE_WARNING}, // x86 prints a crash at info
    {6, "traps: foo[321] trap invalid opcode ip:7f12a4 sp:7ffd8c error:0 in libc.so.6[7f12a0+1000]", WINDOWS_ISSUE_KERNEL_PROGRAM, WINDOWS_ISSUE_WARNING},
    {4, "Call Trace:", 0, 0},
    {6, "Mem-Info:", 0, 0},
    {4, "lowmem_reserve[]: 0 0 0 0 0", 0, 0},
    {3, "tmpfs: Unknown parameter 'bad'", WINDOWS_ISSUE_KERNEL_SYSTEM, WINDOWS_ISSUE_ERROR},
    {4, "__vm_enough_memory: pid: 1234, comm: xz, bytes: 9999 not enough memory for the allocation", 0, 0},
    {6, "Hardware name: Micro-Star International Co., Ltd. MS-7D25/PRO", 0, 0},
    {4, "CPU: 4 UID: 0 PID: 99 Comm: cloudflared Not tainted 7.2.11-arch1-1", 0, 0},
    {6, "Linux version 7.2.11-arch1-1 (builduser@archlinux)", 0, 0},
    {5, "usb 2-1: notice: a device is in a new state", 0, 0},
    {7, "pci 0000:00:02.0: debug line that a level of seven does not raise", 0, 0},
    {1, "Out of memory and the kernel's words after a level one line", WINDOWS_ISSUE_KERNEL_SYSTEM, WINDOWS_ISSUE_ERROR},
};

// A record of /dev/kmsg in the kernel's format: "priority,sequence,time,flags;text" and the newline. Returns its length.
static unsigned int iss_kernel_record(unsigned int level, const char *text, char *into, unsigned int room)
{
        unsigned int at = 0;

        into[at++] = (char)('0' + (level & 7));
        for (const char *h = ",1000,123456789,-;"; *h && at + 2 < room; h++)
                into[at++] = *h;
        for (const char *t = text; *t && at + 2 < room; t++)
                into[at++] = *t;
        into[at++] = '\n';
        into[at] = 0;
        return at;
}

static void iss_rows_kernel(void)
{
        struct kernel_notices k;
        char record[KERNEL_NOTICE_LINE];
        unsigned int bad = 0, lines = sizeof(iss_kernel_lines) / sizeof(iss_kernel_lines[0]), raised = 0;

        for (unsigned int i = 0; i < lines; i++)
        {
                unsigned int sev = 0, code = kernel_notice_class(iss_kernel_lines[i].text, (unsigned int)string_length((string_address)iss_kernel_lines[i].text),
                                                                iss_kernel_lines[i].level, &sev);

                if (code != iss_kernel_lines[i].code || (code && sev != iss_kernel_lines[i].severity))
                        bad++;
        }
        check("kernel notices: each real line of the box's log is classified as it should be (its class and severity, or not raised)", bad == 0);
        check("kernel notices: the table has forty lines or more", lines >= 40);
        memory_zero(&k, sizeof(k));
        k.fd = -2;
        for (unsigned int i = 0; i < lines; i++)
        {
                unsigned int n = iss_kernel_record(iss_kernel_lines[i].level, iss_kernel_lines[i].text, record, sizeof(record));

                raised += kernel_notices_record(&k, record, n, 1000);
        }
        check("kernel notices: the lines that are raised are counted as records, and the others are not", raised == 35);
        check("kernel notices: a word is found where a word of the line starts, not inside one (fault is not in default, usb is not in xusb)",
              !kernel_notice_has("feature default", 15, "fault") && kernel_notice_has("page fault", 10, "fault") && kernel_notice_has("a_fault", 7, "fault") &&
                  !kernel_notice_has("xusb 1", 6, "usb") && kernel_notice_has("usb1", 4, "usb") && kernel_notice_has("[drm:x]", 7, "drm"));
        {
                // The level is the kernel's own word on it: each line of the table, said at notice, info or debug in turn, is raised only by the words of a rule of level 7.
                unsigned int loud = 0;

                for (unsigned int level = 5; level <= 7; level++)
                        for (unsigned int i = 0; i < lines; i++)
                        {
                                unsigned int sev = 0, length = (unsigned int)string_length((string_address)iss_kernel_lines[i].text);

                                loud += kernel_notice_class(iss_kernel_lines[i].text, length, level, &sev) != 0 &&
                                        !kernel_notice_has(iss_kernel_lines[i].text, length, "segfault at|traps:|disabling eDP");
                        }
                check("kernel notices: at notice, info or debug no line of the table is raised but a program's crash and the panel i915 gave up", loud == 0);
        }
        memory_zero(&k, sizeof(k));
        k.fd = -2;
        check("kernel notices: a record with no semicolon is no record", kernel_notices_record(&k, "3,1,2,-", 7, 1) == 0);
        check("kernel notices: a record whose priority is not a number is no record", kernel_notices_record(&k, "x,1,2,-;amdgpu error", 21, 1) == 0);
        check("kernel notices: an empty read is no record", kernel_notices_record(&k, "", 0, 1) == 0);
        // Hostile: an escape and a bell in the message; a line of a kilobyte; a flood of the same error.
        {
                unsigned int n = iss_kernel_record(3, "amdgpu 0000:14:00.0: ring timeout \x1b[2J\x07 cleared", record, sizeof(record));
                unsigned int clean = 1;

                kernel_notices_record(&k, record, n, 2000);
                for (unsigned int i = 0; i < WINDOWS_ISSUE_DETAIL && k.last[0][i]; i++)
                        if ((unsigned char)k.last[0][i] < 0x20 || k.last[0][i] == 0x7f)
                                clean = 0;
                check("kernel notices: an escape and a bell in a line are blanks in the detail, the words stay", clean && k.last[0][0] == 'a' && k.count[0] == 1);
        }
        {
                char big[900];
                unsigned int at;

                for (at = 0; at < sizeof(big) - 1; at++)
                        big[at] = (char)('a' + (char)(at % 26));
                big[at] = 0;
                kernel_notice_sanitize(k.last[0], sizeof(k.last[0]), big, at);
                check("kernel notices: a long line is cut to the detail's room, ended", string_length((string_address)k.last[0]) == WINDOWS_ISSUE_DETAIL - 1);
        }
        memory_zero(&k, sizeof(k));
        k.fd = -2;
        for (unsigned int i = 0; i < 10000; i++)
        {
                unsigned int n = iss_kernel_record(3, "amdgpu 0000:14:00.0: [drm] *ERROR* Unsupported screen format RA24 little-endian (0x34324152)", record, sizeof(record));

                kernel_notices_record(&k, record, n, 3000 + i);
        }
        check("kernel notices: a flood of one class counts every record in that one class", k.count[0] == 10000 && k.count[1] == 0);
}

static void iss_rows_kernel_board(void)
{
        static struct windows *board_scratch;
        struct windows *const board_at = windows_scratch_fresh(&board_scratch); // zero, as the table on the stack was
        struct kernel_notices k;
        char record[KERNEL_NOTICE_LINE];
        struct windows_issues copy;
        unsigned long long t0 = 5000000000ull;
        unsigned int n;

        windows_scratch_zero(board_at);
        memory_zero(&k, sizeof(k));
        k.fd = -2;
        for (unsigned int i = 0; i < 3000; i++)
        {
                n = iss_kernel_record(4, "xhci_hcd 0000:00:14.0: WARN: buffer overrun event for slot 3 ep 2 on endpoint", record, sizeof(record));
                kernel_notices_record(&k, record, n, t0);
        }
        n = iss_kernel_record(3, "amdgpu 0000:14:00.0: ring comp_1.0.0 timeout, signaled seq=1, emitted seq=2", record, sizeof(record));
        kernel_notices_record(&k, record, n, t0);
        kernel_notices_publish(board_at, &k, t0 + 1000000000ull);
        windows_issues_read(board_at, &copy);
        check("kernel notices: a flood is one issue per class on the board, and the warning and the error are their severities",
              copy.count == 2 && windows_issues_worst(&copy, "usb", null) == WINDOWS_ISSUE_WARNING && windows_issues_worst(&copy, "canvas gpu", null) == WINDOWS_ISSUE_ERROR);
        kernel_notices_publish(board_at, &k, t0 + 601ull * 1000000000ull);
        windows_issues_read(board_at, &copy);
        check("kernel notices: a warning not seen for ten minutes is cleared, and its count starts again",
              windows_issues_worst(&copy, "usb", null) == 0 && k.count[1] == 0 && windows_issues_worst(&copy, "canvas gpu", null) == WINDOWS_ISSUE_ERROR);
        kernel_notices_publish(board_at, &k, t0 + 3600ull * 1000000000ull);
        windows_issues_read(board_at, &copy);
        check("kernel notices: an error is kept until it is dismissed, however long ago it was", windows_issues_worst(&copy, "canvas gpu", null) == WINDOWS_ISSUE_ERROR);
}

// The radio states rfkill gives (window.c's radio_key_of_type and radio_blocked_detail): a Wi-Fi or Bluetooth switch that blocks is on its key,
// a block in hardware says to turn the switch, a block in software is the fault only for a radio that was asked for (the machine blocks the others
// on purpose, until moonwater wifi on or moonwater bluetooth on) and says the command that lifts it, and a type that is no radio of Settings' is none.
static void iss_rows_radio(void)
{
        check("radio: a Wi-Fi radio is under the wifi key, a Bluetooth one under bluetooth", string_equals((string_address)radio_key_of_type("wlan"), (string_address)"wifi") &&
              string_equals((string_address)radio_key_of_type("bluetooth"), (string_address)"bluetooth"));
        check("radio: another type is no radio of Settings'", radio_key_of_type("wwan") == null && radio_key_of_type("nfc") == null);
        check("radio: a radio that is not blocked is no issue", radio_blocked_detail("wlan", 0, 0, 0) == null && radio_blocked_detail("wlan", 0, 0, 1) == null);
        check("radio: a block in software on a radio nobody asked for is no issue, the machine's own default",
              radio_blocked_detail("wlan", 1, 0, 0) == null && radio_blocked_detail("bluetooth", 1, 0, 0) == null);
        check("radio: a block in software on a radio that was asked for says the command that lifts it",
              string_compare((string_address)radio_blocked_detail("wlan", 1, 0, 1), (string_address)"blocked in software (rfkill) though it is switched on: run moonwater wifi on") == 0 &&
              string_compare((string_address)radio_blocked_detail("bluetooth", 1, 0, 1), (string_address)"blocked in software (rfkill) though it is switched on: run moonwater bluetooth on") == 0);
        check("radio: a block in hardware is said to be the switch, asked for or not",
              radio_blocked_detail("bluetooth", 0, 1, 0) != null && string_length((string_address)radio_blocked_detail("bluetooth", 0, 1, 0)) > 20 &&
              string_compare((string_address)radio_blocked_detail("wlan", 1, 1, 0), (string_address)radio_blocked_detail("wlan", 0, 1, 1)) == 0);
        check("radio: another type is never blocked", radio_blocked_detail("wwan", 1, 1, 1) == null);
        check("radio: asked for is the word on, whatever else the radio has, and with no word the radio in use",
              radio_asked("on", 0) && !radio_asked("off", 40) && !radio_asked("", 40) && !radio_asked("yes", 0) && radio_asked(null, 12) && !radio_asked(null, 0) && !radio_asked(null, -1));
}

// The worst issue under canvas gpu on the table, read as a copy.
static unsigned int dz_worst(struct windows *b)
{
        windows_issues_read(b, &iss_copy);
        return windows_issues_worst(&iss_copy, "canvas gpu", null);
}

// Dismissing (windows_kernel_dismiss): the class's issue leaves the board, the feeder takes the bit at its next publish and counts from nothing, and an
// error that is not dismissed stays on the board.
static void iss_rows_dismiss(void)
{
        static struct windows *board_scratch;
        struct windows *const board_at = windows_scratch_fresh(&board_scratch); // zero, as the table on the stack was
        struct kernel_notices k;
        char record[KERNEL_NOTICE_LINE];
        unsigned int n;

        windows_scratch_zero(board_at);
        memory_zero(&k, sizeof(k));
        k.fd = -2;
        n = iss_kernel_record(3, "amdgpu 0000:14:00.0: ring comp_1.0.0 timeout, signaled seq=1, emitted seq=2", record, sizeof(record));
        kernel_notices_record(&k, record, n, 1000);
        kernel_notices_publish(board_at, &k, 1000);
        check("dismiss: an error is on the board before it is dismissed", dz_worst(board_at) == WINDOWS_ISSUE_ERROR);
        check("dismiss: dismissing its class clears its issue and leaves the bit for the feeder",
              windows_kernel_dismiss(board_at, 1u << 0) == 1 && (board_at->kernel_dismiss & 1u) != 0);
        kernel_notices_publish(board_at, &k, 2000);
        check("dismiss: the feeder takes the bit at its publish and counts that class from nothing", (board_at->kernel_dismiss & 1u) == 0 && k.count[0] == 0);
        check("dismiss: a class that was not dismissed keeps its issue", dz_worst(board_at) == 0);
        n = iss_kernel_record(3, "amdgpu 0000:14:00.0: ring comp_1.0.0 timeout, signaled seq=3, emitted seq=4", record, sizeof(record));
        kernel_notices_record(&k, record, n, 3000);
        kernel_notices_publish(board_at, &k, 3000);
        check("dismiss: a new record after the dismissal is raised again as one record", k.count[0] == 1 && dz_worst(board_at) == WINDOWS_ISSUE_ERROR);
}

/*
        A window's identity (window.c's windows_app_id_set, windows_icon_set and their readers): what a setter takes is read back, what
        it refuses leaves the row as it was, a picture of any size and shape is the icon's 32 by 32 with nothing read past a row's width
        (the picture ends at a page that is not there), and a reader holds a row somebody else wrote to its bounds, its seqlock and a
        premultiplied pixel.
*/
static unsigned int idn_picture[64 * 64], idn_icon[WINDOWS_ICON * WINDOWS_ICON];

static void iss_rows_identity(void)
{
        char word[WINDOWS_APP_ID], longest[WINDOWS_APP_ID + 8];
        unsigned int seq, wrong = 0, clear = 0, kept;
        long mapped;

        windows_scratch_zero(windows_scratch_table(&iss_table_scratch));
        check("identity: a row that has said nothing has no app_id and no icon",
              windows_app_id_read(windows_scratch_table(&iss_table_scratch), 3, word) == 0 && !word[0] && windows_icon_read(windows_scratch_table(&iss_table_scratch), 3, idn_icon) == 0);
        check("identity: an app_id set is read back whole", windows_app_id_set(windows_scratch_table(&iss_table_scratch), 3, "org.mozilla.firefox") == 1 &&
              windows_app_id_read(windows_scratch_table(&iss_table_scratch), 3, word) == 19 && string_equals((string_address)word, (string_address) "org.mozilla.firefox"));
        check("identity: and it is that row's and no other's", windows_app_id_read(windows_scratch_table(&iss_table_scratch), 2, word) == 0 && windows_app_id_read(windows_scratch_table(&iss_table_scratch), 4, word) == 0);
        for (unsigned int at = 0; at < sizeof(longest); at++)
                longest[at] = 'a';
        longest[WINDOWS_APP_ID - 1] = 0;
        check("identity: the longest app_id, 63 bytes, is taken", windows_app_id_set(windows_scratch_table(&iss_table_scratch), 5, longest) == 1 && windows_app_id_read(windows_scratch_table(&iss_table_scratch), 5, word) == WINDOWS_APP_ID - 1);
        longest[WINDOWS_APP_ID - 1] = 'a', longest[WINDOWS_APP_ID] = 0;
        check("identity: one byte longer, no text, an empty one, a space, a control byte, a delete and a row past the table are refused, and the row is as it was",
              windows_app_id_set(windows_scratch_table(&iss_table_scratch), 3, longest) == 0 && windows_app_id_set(windows_scratch_table(&iss_table_scratch), 3, null) == 0 && windows_app_id_set(windows_scratch_table(&iss_table_scratch), 3, "") == 0 &&
              windows_app_id_set(windows_scratch_table(&iss_table_scratch), 3, "two words") == 0 && windows_app_id_set(windows_scratch_table(&iss_table_scratch), 3, "bell\007") == 0 && windows_app_id_set(windows_scratch_table(&iss_table_scratch), 3, "del\177") == 0 &&
              windows_app_id_set(windows_scratch_table(&iss_table_scratch), 3, "esc\033[31m") == 0 && windows_app_id_set(windows_scratch_table(&iss_table_scratch), WINDOWS, "files") == 0 && windows_app_id_set(null, 0, "files") == 0 &&
              windows_app_id_read(windows_scratch_table(&iss_table_scratch), 3, word) == 19 && string_equals((string_address)word, (string_address) "org.mozilla.firefox"));
        check("identity: a shorter app_id set over a longer leaves none of the longer", windows_app_id_set(windows_scratch_table(&iss_table_scratch), 3, "files") == 1 &&
              windows_app_id_read(windows_scratch_table(&iss_table_scratch), 3, word) == 5 && string_equals((string_address)word, (string_address) "files") && !windows_scratch_table(&iss_table_scratch)->app_id[3][6] && !windows_scratch_table(&iss_table_scratch)->app_id[3][18]);
        // A row somebody else wrote: no null in its room, and bytes that would move a terminal's cursor.
        for (unsigned int at = 0; at < WINDOWS_APP_ID; at++)
                windows_scratch_table(&iss_table_scratch)->app_id[7][at] = at % 5 == 0 ? '\033' : at % 7 == 0 ? ' ' : 'z';
        seq = windows_app_id_read(windows_scratch_table(&iss_table_scratch), 7, word);
        for (unsigned int at = 0; at < WINDOWS_APP_ID - 1; at++)
                wrong += word[at] != (at % 5 == 0 || at % 7 == 0 ? '?' : 'z');
        check("identity: an app_id with no null in its room is read to its room and ended there, its control bytes and spaces made question marks",
              seq == WINDOWS_APP_ID - 1 && !word[WINDOWS_APP_ID - 1] && !wrong && windows_app_id_read(windows_scratch_table(&iss_table_scratch), WINDOWS, word) == 0 && !word[0]);

        // A picture the icon's size is the icon; a pixel whose colour is over its alpha is held to it.
        for (unsigned int at = 0; at < WINDOWS_ICON * WINDOWS_ICON; at++)
                idn_picture[at] = 0xff000000u | at * 0x010203u;
        idn_picture[5] = 0x40ff8020u;
        check("identity: a picture is taken as an icon", windows_icon_set(windows_scratch_table(&iss_table_scratch), 3, idn_picture, WINDOWS_ICON, WINDOWS_ICON, WINDOWS_ICON) == 1);
        seq = windows_icon_read(windows_scratch_table(&iss_table_scratch), 3, idn_icon);
        wrong = 0;
        for (unsigned int at = 0; at < WINDOWS_ICON * WINDOWS_ICON; at++)
                wrong += at != 5 && idn_icon[at] != idn_picture[at];
        check("identity: a picture 32 by 32 is read back pixel for pixel, its sequence even and not 0", seq && !(seq & 1u) && !wrong);
        check("identity: a pixel whose colour is over its alpha is kept premultiplied, each colour no more than the alpha", idn_icon[5] == 0x40404020u);
        check("identity: a second picture moves the sequence on", windows_icon_set(windows_scratch_table(&iss_table_scratch), 3, idn_picture, WINDOWS_ICON, WINDOWS_ICON, WINDOWS_ICON) == 1 &&
              windows_icon_read(windows_scratch_table(&iss_table_scratch), 3, idn_icon) == seq + 2);

        // Larger: each pixel of the icon is the mean of the two by two under it.
        for (unsigned int y = 0; y < 64; y++)
                for (unsigned int x = 0; x < 64; x++)
                        idn_picture[y * 64 + x] = 0xff000000u | ((x & 1) ? 0x00640000u : 0x00c80000u) | ((y & 1) ? 0x0000000au : 0x00000014u) | (x / 2) << 8;
        windows_icon_set(windows_scratch_table(&iss_table_scratch), 3, idn_picture, 64, 64, 64);
        windows_icon_read(windows_scratch_table(&iss_table_scratch), 3, idn_icon);
        wrong = 0;
        for (unsigned int at = 0; at < WINDOWS_ICON * WINDOWS_ICON; at++)
                wrong += idn_icon[at] != (0xff96000fu | (at % WINDOWS_ICON) << 8);
        check("identity: a picture 64 by 64 is made 32 by 32, each pixel the mean of the four under it", !wrong);

        // Wider than high: the longer side is the icon's, the shorter in the middle with clear above and below.
        for (unsigned int at = 0; at < 64 * 16; at++)
                idn_picture[at] = 0xff112233u;
        windows_icon_set(windows_scratch_table(&iss_table_scratch), 3, idn_picture, 64, 16, 64);
        windows_icon_read(windows_scratch_table(&iss_table_scratch), 3, idn_icon);
        wrong = 0;
        for (unsigned int y = 0; y < WINDOWS_ICON; y++)
                for (unsigned int x = 0; x < WINDOWS_ICON; x++)
                        wrong += idn_icon[y * WINDOWS_ICON + x] != (y >= 12 && y < 20 ? 0xff112233u : 0u), clear += !idn_icon[y * WINDOWS_ICON + x];
        check("identity: a picture 64 by 16 is its 32 by 8 in the middle of the icon, clear above and below", !wrong && clear == 24 * WINDOWS_ICON);

        // Smaller: one pixel is the whole icon.
        idn_picture[0] = 0x80402010u;
        windows_icon_set(windows_scratch_table(&iss_table_scratch), 3, idn_picture, 1, 1, 1);
        windows_icon_read(windows_scratch_table(&iss_table_scratch), 3, idn_icon);
        wrong = 0;
        for (unsigned int at = 0; at < WINDOWS_ICON * WINDOWS_ICON; at++)
                wrong += idn_icon[at] != 0x80402010u;
        check("identity: a picture of one pixel is an icon of that pixel", !wrong);

        // What is refused, and the row as it was after each.
        kept = windows_icon_read(windows_scratch_table(&iss_table_scratch), 3, idn_icon);
        check("identity: no pixels, a side of 0, a side over 1024, rows shorter than the width and a row past the table are refused, and the icon is the one it was",
              windows_icon_set(windows_scratch_table(&iss_table_scratch), 3, null, 32, 32, 32) == 0 && windows_icon_set(windows_scratch_table(&iss_table_scratch), 3, idn_picture, 0, 32, 32) == 0 &&
              windows_icon_set(windows_scratch_table(&iss_table_scratch), 3, idn_picture, 32, 0, 32) == 0 && windows_icon_set(windows_scratch_table(&iss_table_scratch), 3, idn_picture, WINDOWS_ICON_MOST + 1, 1, WINDOWS_ICON_MOST + 1) == 0 &&
              windows_icon_set(windows_scratch_table(&iss_table_scratch), 3, idn_picture, 1, WINDOWS_ICON_MOST + 1, 1) == 0 && windows_icon_set(windows_scratch_table(&iss_table_scratch), 3, idn_picture, 0x80000000u, 0x80000000u, 0x80000000u) == 0 &&
              windows_icon_set(windows_scratch_table(&iss_table_scratch), 3, idn_picture, 32, 32, 31) == 0 && windows_icon_set(windows_scratch_table(&iss_table_scratch), 3, idn_picture, 32, 32, 0) == 0 &&
              windows_icon_set(windows_scratch_table(&iss_table_scratch), WINDOWS, idn_picture, 32, 32, 32) == 0 && windows_icon_set(null, 3, idn_picture, 32, 32, 32) == 0 &&
              windows_icon_read(windows_scratch_table(&iss_table_scratch), 3, idn_icon) == kept && idn_icon[0] == 0x80402010u && idn_icon[1023] == 0x80402010u);

        // The largest picture, and a stride wider than the width, each ending at a page that is not mapped: nothing past a row's width is read.
        mapped = window_call(WINDOW_SYS_MMAP, 0, 5 * 4096, 3, 0x22, -1, 0);
        if (mapped > 0 && window_call(WINDOW_SYS_MUNMAP, mapped + 4 * 4096, 4096, 0, 0, 0, 0) == 0)
        {
                unsigned int *idn_end = (unsigned int *)(mapped + 4 * 4096), *idn_across = idn_end - WINDOWS_ICON_MOST, *idn_apart = idn_end - (39 * 100 + 4);

                for (unsigned int at = 0; at < 4 * 1024; at++)
                        ((unsigned int *)mapped)[at] = 0xff000000u | at;
                check("identity: a picture 1024 idn_across and one high that ends at the end of its page is taken", windows_icon_set(windows_scratch_table(&iss_table_scratch), 6, idn_across, WINDOWS_ICON_MOST, 1, WINDOWS_ICON_MOST) == 1);
                check("identity: so is one 1 idn_across and 1024 high", windows_icon_set(windows_scratch_table(&iss_table_scratch), 6, idn_across, 1, WINDOWS_ICON_MOST, 1) == 1);
                check("identity: a picture 4 wide and 40 high with rows 100 apart, whose last row ends at the end of its page, is taken: nothing past a row's width is read",
                      windows_icon_set(windows_scratch_table(&iss_table_scratch), 6, idn_apart, 4, 40, 100) == 1 && windows_icon_read(windows_scratch_table(&iss_table_scratch), 6, idn_icon) != 0);
        }
        else
                check("identity: a page to end a picture at is mapped", 0);

        // The seqlock: a writer inside is no picture, a sequence at its end wraps past 0, and a row somebody else wrote is held premultiplied.
        __atomic_store_n(&windows_scratch_table(&iss_table_scratch)->icon_seq[3], kept | 1u, __ATOMIC_RELEASE);
        check("identity: an icon a writer is in the middle of is no icon to a reader", windows_icon_read(windows_scratch_table(&iss_table_scratch), 3, idn_icon) == 0);
        __atomic_store_n(&windows_scratch_table(&iss_table_scratch)->icon_seq[3], 0xfffffffeu, __ATOMIC_RELEASE);
        idn_picture[0] = 0xff010203u;
        check("identity: the sequence after the last even one is 2, never 0, which is no icon",
              windows_icon_set(windows_scratch_table(&iss_table_scratch), 3, idn_picture, 1, 1, 1) == 1 && windows_scratch_table(&iss_table_scratch)->icon_seq[3] == 2 && windows_icon_read(windows_scratch_table(&iss_table_scratch), 3, idn_icon) == 2);
        for (unsigned int at = 0; at < WINDOWS_ICON * WINDOWS_ICON; at++)
                windows_scratch_table(&iss_table_scratch)->icon[9][at] = 0x10ffffffu;
        windows_scratch_table(&iss_table_scratch)->icon_seq[9] = 8;
        windows_icon_read(windows_scratch_table(&iss_table_scratch), 9, idn_icon);
        wrong = 0;
        for (unsigned int at = 0; at < WINDOWS_ICON * WINDOWS_ICON; at++)
                wrong += idn_icon[at] != 0x10101010u;
        check("identity: an icon written straight into the row with colours over their alpha is read premultiplied", !wrong);
        check("identity: a row past the table has no icon", windows_icon_read(windows_scratch_table(&iss_table_scratch), WINDOWS, idn_icon) == 0 && windows_icon_read(null, 0, idn_icon) == 0);

        // A row taken again is nobody yet.
        windows_reset_row(windows_scratch_table(&iss_table_scratch), 3);
        check("identity: a row given to the next window has no app_id and no icon, and the rows beside it keep theirs",
              windows_app_id_read(windows_scratch_table(&iss_table_scratch), 3, word) == 0 && windows_icon_read(windows_scratch_table(&iss_table_scratch), 3, idn_icon) == 0 && !windows_scratch_table(&iss_table_scratch)->app_id[3][0] &&
              windows_app_id_read(windows_scratch_table(&iss_table_scratch), 5, word) == WINDOWS_APP_ID - 1 && windows_icon_read(windows_scratch_table(&iss_table_scratch), 9, idn_icon) == 8);
}

b32 main(void)
{
        iss_rows_identity();
        iss_rows_dismiss();
        iss_rows_basic();
        iss_rows_full();
        iss_rows_torn();
        iss_rows_hostile();
        iss_rows_gpu();
        iss_rows_metrics();
        iss_rows_kernel();
        iss_rows_kernel_board();
        iss_rows_radio();
        return test_report(null);
}
#endif /* CHECK_issues */

#ifdef CHECK_draw_list
/*
        The draw list (ui.c's ui_draw_list, the production form of a recorded frame): a frame recorded by ui_scene_begin and ui_scene_end is read
        back by the reader op for op, its damage is the picture or the band's rows, the halves swap, a torn read is refused, and a list that
        outgrows its room is refused and the frame is painted on the processor. The frames are the kit's own painters (fills, a tone, a rounded
        box, a ring, a dot, a band's clear); their reference is the same painters recorded into a scene of the check's own, so what this adds is
        the transport alone. Nothing here may write a pixel: the picture stays as it was.
*/
enum { DL_W = 240, DL_H = 160, DL_ROOM = 64, DL_SMALL = 3, DL_BYTES = 64 + 2 * (64 * 32 + 4096), DL_SMALL_BYTES = 64 + 2 * (3 * 32 + 64) };
static unsigned int dl_memory[DL_BYTES / 4], dl_small[DL_SMALL_BYTES / 4], dl_pixels[DL_W * DL_H], dl_cpu[DL_W * DL_H];
static unsigned int dl_ref_ops[DL_ROOM * 8];
static unsigned char dl_ref_cover[4096];
static struct ui_scene dl_ref_scene;
static struct ui dl_live, dl_ref;

// The frame: a fill, a tone over, a rounded box and its ring, a dot, each a function of seed.
static void dl_paint(struct ui address_to ui, int seed)
{
        struct ui_box box;

        ui_fill(ui, 10 + seed, 10, 90, 40, 0x80ff8040u);
        box.x0 = (20 + seed) * 256, box.y0 = 30 * 256, box.x1 = 200 * 256, box.y1 = 90 * 256, box.rx = 12 * 256, box.ry = 12 * 256;
        ui_kit_fill(ui, &box, 0xc0336699u, null);
        box.x0 = 40 * 256, box.y0 = 100 * 256, box.x1 = 180 * 256, box.y1 = 150 * 256, box.rx = 0, box.ry = 0;
        ui_kit_ring(ui, &box, 2 * 256, 0xff00ff00u);
        ui_dot(ui, 5 + seed, 5, 0x80ffffffu);
}

// A frame's ui: the picture fields and the scale, no painting yet (ui_begin comes after the recorder is set, so its clear is recorded).
static void dl_setup(struct ui address_to ui, unsigned int address_to pixels)
{
        memory_zero(ui, sizeof(*ui));
        ui->scale = 100, ui->top = 0, ui->bottom = DL_H, ui->pixels = pixels, ui->pitch = DL_W, ui->width = DL_W, ui->height = DL_H;
}

// A frame into the list: recorder set by the list, begun, painted, published. Answers 0 when the list does not take it.
static b32 dl_frame(struct ui_draw_list address_to list, int seed)
{
        dl_setup(&dl_live, dl_pixels);
        if (!ui_scene_begin(&dl_live, list))
                return 0;
        ui_begin(&dl_live, dl_pixels, DL_W, DL_W, DL_H);
        dl_paint(&dl_live, seed);
        return ui_scene_end(&dl_live, list);
}

// The reference: the same frame recorded into a scene of the check's own.
static void dl_reference(int seed)
{
        dl_setup(&dl_ref, dl_pixels);
        dl_ref_scene.ops = dl_ref_ops, dl_ref_scene.room = DL_ROOM, dl_ref_scene.cover = dl_ref_cover, dl_ref_scene.size = sizeof(dl_ref_cover);
        dl_ref_scene.used = 0, dl_ref_scene.at = 0, dl_ref_scene.count = 0, dl_ref_scene.lost = 0;
        dl_ref_scene.dx0 = dl_ref_scene.dy0 = dl_ref_scene.dx1 = dl_ref_scene.dy1 = 0;
        dl_ref.scene = &dl_ref_scene;
        ui_begin(&dl_ref, dl_pixels, DL_W, DL_W, DL_H);
        dl_paint(&dl_ref, seed);
        dl_ref.scene = null;
}

static int dl_same_ops(const unsigned int address_to got, unsigned int count)
{
        for (unsigned int i = 0; i < count * 8; i++)
                if (got[i] != dl_ref_ops[i])
                        return 0;
        return 1;
}

static int dl_pixels_clear(const unsigned int address_to pixels, unsigned long count)
{
        for (unsigned long i = 0; i < count; i++)
                if (pixels[i])
                        return 0;
        return 1;
}

static void dl_rows(void)
{
        static struct ui_draw_list list, small;
        static struct ui_draw_view view, held;
        static struct ui cpu;
        unsigned char address_to base = (unsigned char address_to)dl_memory;
        unsigned int address_to head = dl_memory;
        unsigned int gen, saved;
        int r, painted = 0;

        check("draw list: the memory takes a list of its room", ui_draw_list_init(&list, base, DL_BYTES, DL_ROOM) && list.half == 6144 && list.room == DL_ROOM);
        check("draw list: a memory too small for a half is refused", !ui_draw_list_init(&small, (unsigned char address_to)dl_small, 100, DL_ROOM));
        check("draw list: a list of three ops takes its halves", ui_draw_list_init(&small, (unsigned char address_to)dl_small, DL_SMALL_BYTES, DL_SMALL));

        // The round trip: one frame, read back op for op, its damage the picture's, the picture untouched.
        check("draw list: a frame is recorded and published", dl_frame(&list, 1));
        check("draw list: a recorded frame writes no pixel", dl_pixels_clear(dl_pixels, DL_W * DL_H));
        check("draw list: the recorder is cleared when the frame ends", dl_live.scene == null);
        r = ui_draw_list_open(base, DL_BYTES, &view);
        dl_reference(1);
        check("draw list: the published frame opens", r == 1 && view.gen == 2 && view.half == 0);
        check("draw list: its ops are the frame's, count for count", r == 1 && view.count == dl_ref_scene.count && view.count >= 4);
        check("draw list: read back op for op", r == 1 && dl_same_ops(view.ops, view.count));
        check("draw list: the damage is the picture", r == 1 && view.x0 == 0 && view.y0 == 0 && view.x1 == DL_W && view.y1 == DL_H && view.width == DL_W && view.height == DL_H && view.pitch == DL_W);
        check("draw list: the damage is the reference's", r == 1 && view.x0 == dl_ref_scene.dx0 && view.y0 == dl_ref_scene.dy0 && view.x1 == dl_ref_scene.dx1 && view.y1 == dl_ref_scene.dy1);
        check("draw list: a view holds while nothing publishes", r == 1 && ui_draw_list_still(base, &view));
        held = view;

        // The band: a frame whose only op is a band's clear; its damage is the band's rows, and the halves go to the other one.
        dl_setup(&dl_live, dl_pixels);
        check("draw list: a band's frame begins", ui_scene_begin(&dl_live, &list));
        dl_live.band_top = 40, dl_live.band_bottom = 60;
        ui_clear(&dl_live);
        check("draw list: a band's frame publishes", ui_scene_end(&dl_live, &list));
        r = ui_draw_list_open(base, DL_BYTES, &view);
        check("draw list: the band's damage is its rows", r == 1 && view.half == 1 && view.count == 1 && view.x0 == 0 && view.y0 == 40 && view.x1 == DL_W && view.y1 == 60);

        // The halves swap: the next frame is in the first half again, and the view of the first frame is refused once that half is written over.
        check("draw list: the next frame publishes into the first half", dl_frame(&list, 3));
        dl_reference(3);
        r = ui_draw_list_open(base, DL_BYTES, &view);
        check("draw list: the halves swap, the frame in the first half", r == 1 && view.half == 0 && view.ops == held.ops && view.count == dl_ref_scene.count);
        check("draw list: read back op for op after the swap", r == 1 && dl_same_ops(view.ops, view.count));
        check("draw list: a view that spans a publish is refused", !ui_draw_list_still(base, &held));

        // A publish in progress: an odd generation is not read, and an even one again is.
        gen = head[UI_DRAW_LIST_GEN];
        head[UI_DRAW_LIST_GEN] = gen | 1;
        check("draw list: a publish in progress is not read", ui_draw_list_open(base, DL_BYTES, &view) == 0);
        head[UI_DRAW_LIST_GEN] = gen;
        check("draw list: the same list reads again once the publish is whole", ui_draw_list_open(base, DL_BYTES, &view) == 1);

        // A header that claims more ops than its room is no list to read.
        saved = head[UI_DRAW_LIST_COUNT];
        head[UI_DRAW_LIST_COUNT] = DL_ROOM + 1;
        check("draw list: a header that claims more ops than its room is refused", ui_draw_list_open(base, DL_BYTES, &view) == -1);
        head[UI_DRAW_LIST_COUNT] = saved;

        // A list that outgrows its room: the frame is lost, the list says so, and the frame is painted on the processor.
        check("draw list: a frame that outgrows its room still publishes", dl_frame(&small, 1));
        r = ui_draw_list_open((unsigned char address_to)dl_small, DL_SMALL_BYTES, &view);
        check("draw list: a list that outgrows its room is refused", r == -1);
        dl_setup(&cpu, dl_cpu);
        ui_begin(&cpu, dl_cpu, DL_W, DL_W, DL_H);
        dl_paint(&cpu, 1);
        for (unsigned long i = 0; i < DL_W * DL_H; i++)
                painted += dl_cpu[i] != 0;
        check("draw list: the processor paints the frame the list refused", painted > 0 && cpu.scene == null);
}

/*
        The painters that record (R2 and step 2): each probe paints with a scene set and counts the pixels it wrote, which must be none, and
        whether the frame was given up to the processor (scene->lost, R3 and R4: those painters write the picture and say so, and their rows are
        printed as named known diffs, not failures, until the kernel records them). The launcher's grid of dots is also recorded at four scales,
        as nine kit boxes.
*/
static font_face dl_face;
static font_engine dl_engine;
static p8 dl_atlas[512 * 512];
static unsigned int dl_scratch[1200 * UI_SCRATCH_ROWS], dl_probe_pixels[DL_W * DL_H], dl_replay[DL_W * DL_H];
static unsigned int dl_probe_ops[DL_ROOM * 8];
static unsigned char dl_probe_cover[4096];
static struct ui_scene dl_probe_scene;

static void dl_probe_reset(void)
{
        memory_zero(dl_probe_pixels, sizeof(dl_probe_pixels));
        memory_zero(&dl_probe_scene, sizeof(dl_probe_scene));
        dl_probe_scene.ops = dl_probe_ops, dl_probe_scene.room = DL_ROOM, dl_probe_scene.cover = dl_probe_cover, dl_probe_scene.size = sizeof(dl_probe_cover);
}

static unsigned long dl_probe_writes(void)
{
        unsigned long n = 0;

        for (unsigned long i = 0; i < DL_W * DL_H; i++)
                n += dl_probe_pixels[i] != 0;
        return n;
}

// A kit ui on the probe picture; with fonts (when the check was given them) the text painters are usable.
static void dl_probe_ui(struct ui address_to ui, b32 scene)
{
        memory_zero(ui, sizeof(*ui));
        ui->pixels = dl_probe_pixels, ui->pitch = DL_W, ui->width = DL_W, ui->height = DL_H, ui->top = 0, ui->bottom = DL_H, ui->scale = 100;
        ui->regular = ui->bold = &dl_face, ui->engine = &dl_engine;
        ui->scratch = dl_scratch, ui->scratch_width = 1200;
        ui->scene = scene ? &dl_probe_scene : null;
}

// The recorded ops replayed the way the kit's records mean: a fill writes its colour, a tone lays itself over (ui_over).
static void dl_replay_ops(const struct ui_scene address_to scene, unsigned int address_to into)
{
        for (unsigned int k = 0; k < scene->count; k++)
        {
                const unsigned int address_to op = scene->ops + 8 * k;

                for (int y = (int)op[2]; y < (int)op[4]; y++)
                        for (int x = (int)op[1]; x < (int)op[3]; x++)
                                into[(unsigned long)y * DL_W + (unsigned long)x] = (op[0] & 255) == 0 ? op[5] : ui_over(op[5], into[(unsigned long)y * DL_W + (unsigned long)x]);
        }
}

/*
        font_cell's known-ground branch against its plan (font_cell_plan) interpreted on the processor: the cell's rectangle filled with the ground, and
        the glyph's box translated through the tones. Random cells of one and two cells, sizes, colours and grounds, and the target cut at its edges
        (a cell past the left, the right, the top or the bottom); both start from the same noise, so the cells' pixels and the noise outside them must
        match. CPU only: this is the reference the recording (ui.c) is built on.
*/
static unsigned int dlc_seed = 2463534242u;

static unsigned int dlc_rand(void)
{
        dlc_seed ^= dlc_seed << 13, dlc_seed ^= dlc_seed >> 17, dlc_seed ^= dlc_seed << 5;
        return dlc_seed;
}

static void dl_cell_plan_rows(void)
{
        static unsigned int want[DL_W * DL_H], got[DL_W * DL_H];
        const font_face address_to faces[1] = {&dl_face};
        const p32 codes[6] = {'A', 'g', 'W', '@', 0xe9, 0x4e2d};
        long bad = 0, translates = 0, plans = 0;

        for (int round = 0; round < 600; round++)
        {
                p32 code = codes[dlc_rand() % 6], em = (10 + dlc_rand() % 30) << 6, colour = dlc_rand() & 0xffffffu, background = dlc_rand() & 0xffffffu;
                p32 cells = 1 + dlc_rand() % 2;
                font_metrics grid = font_metrics_at(faces[0], em);
                font_target ref = {want, DL_W, 20 + (int)(dlc_rand() % (DL_W - 20)), 10 + (int)(dlc_rand() % (DL_H - 10))};
                font_target plan_target = {got, DL_W, ref.width, ref.height};
                b32 x = (b32)(dlc_rand() % (unsigned int)(ref.width + 40)) - 20, y = (b32)(dlc_rand() % (unsigned int)(ref.height + 20)) - 10;
                font_cell_plan_t plan;

                for (positive i = 0; i < (positive)DL_W * DL_H; i++)
                        want[i] = got[i] = dlc_rand();
                font_cell(&dl_engine, faces, 1, code, cells, &grid, em, 0, &ref, x, y, colour, background);
                font_cell_plan(&dl_engine, faces, 1, code, cells, &grid, em, 0, &plan_target, x, y, colour, background, &plan);
                plans++;
                if (!plan.unknown && !plan.empty)
                {
                        for (int yy = plan.top; yy < plan.bottom; yy++)
                                for (int xx = plan.left; xx < plan.right; xx++)
                                        got[(positive)yy * DL_W + xx] = plan.ground;
                        if (plan.translate)
                        {
                                translates++;
                                for (int yy = plan.above; yy < plan.below; yy++)
                                        for (int xx = plan.from; xx < plan.to; xx++)
                                                got[(positive)yy * DL_W + xx] = plan.tones[plan.coverage[(positive)(yy - plan.above) * plan.stride + (xx - plan.from)]];
                        }
                }
                for (positive i = 0; i < (positive)DL_W * DL_H; i++)
                        if (want[i] != got[i])
                        {
                                bad++;
                                break;
                        }
        }
        check("font plan: the cells of 600 random glyphs, sizes, colours, grounds and clips are font_cell's pixels", bad == 0 && plans == 600);
        check("font plan: translates were planned (the test has glyphs to translate)", translates > 300);
}

// A wide cell of a code view (ui.c's ui_scene_wide, the recorded plan) replayed (fills, and the translate op 10 through its tones and cover) is
// font_cell's two-cell pixels: four scales, a light and a dark ground, a CJK and a fullwidth code point, the noise around the cells the same.
static void dl_replay_translate(const struct ui_scene address_to scene, unsigned int address_to into)
{
        const unsigned char address_to base = scene->cover - scene->at;

        for (unsigned int k = 0; k < scene->count; k++)
        {
                const unsigned int address_to op = scene->ops + 8 * k;

                if ((op[0] & 255) != 10)
                {
                        for (int y = (int)op[2]; y < (int)op[4]; y++)
                                for (int x = (int)op[1]; x < (int)op[3]; x++)
                                        into[(unsigned long)y * DL_W + (unsigned long)x] = (op[0] & 255) == 0 ? op[5] : ui_over(op[5], into[(unsigned long)y * DL_W + (unsigned long)x]);
                        continue;
                }
                {
                        unsigned int w = op[0] >> 8;
                        const unsigned int address_to tones = (const unsigned int address_to)(base + op[5]);
                        const unsigned char address_to cover = base + op[7];

                        for (int y = (int)op[2]; y < (int)op[4]; y++)
                                for (int x = (int)op[1]; x < (int)op[3]; x++)
                                        into[(unsigned long)y * DL_W + (unsigned long)x] = tones[cover[(unsigned long)(y - (int)op[2]) * w + (unsigned long)(x - (int)op[1])]];
                }
        }
}

static void dl_wide_rows(void)
{
        static unsigned int want[DL_W * DL_H], got[DL_W * DL_H];
        const font_face address_to faces[2] = {&dl_face, &dl_face};
        const unsigned int scales[4] = {100, 150, 200, 300};
        const p32 codes[3] = {0x4e2d, 0xff21, 0x1f600};
        const p32 grounds[2] = {0xf6f6f6u, 0x111113u}, inks[2] = {0x1d1d1fu, 0xdfe7efu};
        long bad = 0, cases = 0, recorded = 0;

        for (int k = 0; k < 4; k++)
                for (int light = 0; light < 2; light++)
                        for (int c = 0; c < 3; c++)
                        {
                                p32 em = (13 * scales[k] / 100) << 6;
                                font_metrics grid = font_metrics_at(faces[0], em);
                                font_target ref = {want, DL_W, 3 * grid.cell_width + 8, grid.cell_height + 6};
                                font_target probe = {dl_probe_pixels, DL_W, ref.width, ref.height};
                                b32 x = 3, y = 2;
                                struct ui probe_ui;

                                for (positive i = 0; i < (positive)DL_W * DL_H; i++)
                                        want[i] = got[i] = dlc_rand();
                                font_cell(&dl_engine, faces, 2, codes[c], 2, &grid, em, 0, &ref, x, y, inks[light], grounds[light]);
                                dl_probe_reset();
                                dl_probe_ui(&probe_ui, 1);
                                ui_scene_wide(&probe_ui, faces, codes[c], &grid, em, &probe, 0, 0, x, y, inks[light], grounds[light]);
                                cases++;
                                recorded += !dl_probe_scene.lost && dl_probe_scene.count > 0;
                                if (dl_probe_scene.lost)
                                {
                                        bad++;
                                        continue;
                                }
                                dl_replay_translate(&dl_probe_scene, got);
                                for (positive i = 0; i < (positive)DL_W * DL_H; i++)
                                        if (want[i] != got[i])
                                        {
                                                bad++;
                                                break;
                                        }
                        }
        check("draw list: a wide cell records (no loss) at 100, 150, 200 and 300 percent, on a light and a dark ground", recorded == cases && cases == 24);
        check("draw list: a wide cell's replay is font_cell's two cells, bit for bit, at four scales, light and dark, CJK, fullwidth and an emoji (no colour glyphs: an unknown glyph)", bad == 0);
}

// A cluster of one cell (a base and its combining marks): font_cluster_plan's coverage painted by font_plan_paint is what the recorder replays
// (ui_scene_cluster: fill and translate), at four scales, light and dark, for a Latin base with a combining acute and a diaeresis, and the
// Hebrew and Arabic marks. The two rules that combine the marks' boxes are measured against font_draw of the same cluster over the ground (the
// processor's previous marks path): saturating add (font_cluster_plan's) and max, the error summed per channel over the cell. Saturating add is
// the processor's rule, so the check holds it to be no further from the blend than max.
static b32 dl_utf8(p32 code, p8 address_to at)
{
        if (code < 0x80)
                return at[0] = (p8)code, 1;
        if (code < 0x800)
                return at[0] = (p8)(0xc0 | code >> 6), at[1] = (p8)(0x80 | (code & 63)), 2;
        at[0] = (p8)(0xe0 | code >> 12), at[1] = (p8)(0x80 | (code >> 6 & 63)), at[2] = (p8)(0x80 | (code & 63));
        return 3;
}

static void dl_cluster_rows(void)
{
        static unsigned int want[DL_W * DL_H], got[DL_W * DL_H], draw[DL_W * DL_H], sumpaint[DL_W * DL_H], maxpaint[DL_W * DL_H];
        static unsigned char cover[256 * 256], maxed[256 * 256];
        const font_face address_to faces[2] = {&dl_face, &dl_face};
        const unsigned int scales[4] = {100, 150, 200, 300};
        const p32 clusters[3][2] = {{'e', 0x0301}, {0x05d0, 0x05b8}, {0x0628, 0x064e}};
        const p32 grounds[2] = {0xf6f6f6u, 0x111113u}, inks[2] = {0x1d1d1fu, 0xdfe7efu};
        long bad = 0, cases = 0, recorded = 0, err_sum = 0, err_max = 0;

        for (int k = 0; k < 4; k++)
                for (int light = 0; light < 2; light++)
                        for (int c = 0; c < 3; c++)
                        {
                                p32 em = (13 * scales[k] / 100) << 6;
                                font_metrics grid = font_metrics_at(faces[0], em);
                                font_target ref = {want, DL_W, grid.cell_width + 6, grid.cell_height + 6};
                                font_target probe = {dl_probe_pixels, DL_W, ref.width, ref.height};
                                b32 x = 3, y = 2;
                                font_cell_plan_t plan;
                                struct ui probe_ui;
                                p8 utf8[12];
                                positive bytes = 0;

                                // the processor's paint of the cluster over noise, and the recording replayed over the same noise
                                for (positive i = 0; i < (positive)DL_W * DL_H; i++)
                                        want[i] = dlc_rand();
                                for (positive i = 0; i < (positive)DL_W * DL_H; i++)
                                        got[i] = want[i];
                                font_cluster_plan(&dl_engine, faces, 1, clusters[c], 2, &grid, em, 0, &ref, x, y, inks[light], grounds[light], cover, &plan);
                                font_plan_paint(&plan, &ref);
                                dl_probe_reset();
                                dl_probe_ui(&probe_ui, 1);
                                font_cluster_plan(&dl_engine, faces, 1, clusters[c], 2, &grid, em, 0, &probe, x, y, inks[light], grounds[light], cover, &plan);
                                ui_scene_cluster(&probe_ui, &plan, inks[light], grounds[light], 0, 0);
                                cases++;
                                recorded += !dl_probe_scene.lost && dl_probe_scene.count > 0;
                                if (dl_probe_scene.lost)
                                        bad++;
                                else
                                {
                                        dl_replay_translate(&dl_probe_scene, got);
                                        for (positive i = 0; i < (positive)DL_W * DL_H; i++)
                                                if (want[i] != got[i])
                                                {
                                                        bad++;
                                                        break;
                                                }
                                }

                                // the marks' error against font_draw of the cluster over the ground, per rule
                                for (int q = 0; q < 2; q++)
                                        bytes += dl_utf8(clusters[c][q], utf8 + bytes);
                                for (positive i = 0; i < (positive)DL_W * DL_H; i++)
                                        draw[i] = sumpaint[i] = maxpaint[i] = grounds[light];
                                {
                                        font_target dt = {draw, DL_W, ref.width, ref.height};
                                        font_target st = {sumpaint, DL_W, ref.width, ref.height};
                                        font_target mt = {maxpaint, DL_W, ref.width, ref.height};
                                        font_cell_plan_t maxplan;
                                        p64 chain = font_chain(faces, 1, 1);
                                        p32 flags = font_polarity(0, inks[light], grounds[light]);
                                        int width = grid.cell_width, height = grid.cell_height;

                                        font_draw(&dl_engine, faces, 2, utf8, bytes, em, 0, &dt, (b64)x * 65536, (b32)(y + grid.baseline), inks[light], FONT_UNKNOWN);
                                        font_cluster_plan(&dl_engine, faces, 1, clusters[c], 2, &grid, em, 0, &st, x, y, inks[light], grounds[light], cover, &plan);
                                        font_plan_paint(&plan, &st);
                                        memory_zero(maxed, sizeof(maxed));
                                        for (int q = 0; q < 2; q++)
                                        {
                                                p32 ident = chain ? font_identify(&dl_engine, chain, font_cell_shape(&grid, 1, 1), 1 << 3 | 1) : 0;
                                                b16 ink[2];
                                                b32 dx, dy;
                                                font_bitmap box = font_cell_box(&dl_engine, faces, 1, clusters[c][q], 1, &grid, em, flags, font_cell_base(faces, 1, em, flags), ident, &dx, &dy, ink);

                                                for (b32 r = 0; r < box.height; r++)
                                                        for (b32 cc = 0; cc < box.width; cc++)
                                                        {
                                                                b32 cx = dx + cc, cy = dy + r;
                                                                p8 address_to at;

                                                                if (cx < 0 || cy < 0 || cx >= width || cy >= height)
                                                                        continue;
                                                                at = maxed + (positive)cy * (positive)width + (positive)cx;
                                                                if (box.coverage[(positive)r * box.stride + cc] > (unsigned char)*at)
                                                                        *at = box.coverage[(positive)r * box.stride + cc];
                                                        }
                                        }
                                        maxplan = plan;
                                        maxplan.coverage = (const p8 address_to)maxed + (positive)(plan.above - y) * (positive)width + (plan.from - x);
                                        font_plan_paint(&maxplan, &mt);
                                        for (int yy = (int)plan.top; yy < plan.bottom; yy++)
                                                for (int xx = (int)plan.left; xx < plan.right; xx++)
                                                        for (int ch = 0; ch < 24; ch += 8)
                                                        {
                                                                int d = (int)(draw[(positive)yy * DL_W + xx] >> ch & 255);

                                                                err_sum += d - (int)(sumpaint[(positive)yy * DL_W + xx] >> ch & 255) < 0 ? (int)(sumpaint[(positive)yy * DL_W + xx] >> ch & 255) - d : d - (int)(sumpaint[(positive)yy * DL_W + xx] >> ch & 255);
                                                                err_max += d - (int)(maxpaint[(positive)yy * DL_W + xx] >> ch & 255) < 0 ? (int)(maxpaint[(positive)yy * DL_W + xx] >> ch & 255) - d : d - (int)(maxpaint[(positive)yy * DL_W + xx] >> ch & 255);
                                                        }
                                }
                        }
        check("draw list: a cluster of one cell records without loss at 100, 150, 200 and 300 percent, light and dark", recorded == cases && cases == 24);
        check("draw list: the saturating add of the marks is no further from font_draw than max (the rule kept)", err_sum <= err_max);
        check("draw list: a cluster's replay is the processor's paint, bit for bit, at four scales, light and dark, Latin, Hebrew and Arabic marks", bad == 0);
}

static void dl_probes(void)
{
        static struct ui probe;
        static struct ui_control control;
        static struct taskbar_view view;
        static unsigned int cpu[DL_W * DL_H];
        const unsigned int scales[4] = {100, 150, 200, 300};
        int failed = 0;
        positive font_size = 0, font_bytes = program_argument_count() > 1 ? compositor_map_file(program_argument(1), &font_size) : 0;
        b32 fonts = font_bytes && font_load(&dl_face, (const p8 address_to)font_bytes, font_size, 0) == 0;

        if (fonts)
                font_start(&dl_engine, dl_atlas, 512, 512);
        dl_cell_plan_rows();
        dl_wide_rows();
        dl_cluster_rows();

        // The spinner: its ring and arc are recorded (the arc as coalesced tone rects), nothing is painted.
        dl_probe_reset();
        dl_probe_ui(&probe, 1);
        memory_zero(&control, sizeof(control));
        control.x = 20, control.y = 20, control.w = 28;
        ui_spinner_draw(&probe, &control);
        check("draw list: the spinner records its ring and arc, painting nothing", dl_probe_writes() == 0 && !dl_probe_scene.lost && dl_probe_scene.count > 0);

        // The apps mark (UI_ICON_APPS) at four scales: four kit boxes (round corners, in 256ths: the replay above reads plain pixel rectangles,
        // which these are not; the GPU's kit boxes are proven by CHECK_gpu_kit) are recorded and nothing is painted, and the same call with no
        // scene paints the four tiles on the processor.
        for (int k = 0; k < 4; k++)
        {
                unsigned int s = scales[k], painted = 0;
                struct ui mark;

                memory_zero(&mark, sizeof(mark));
                memory_zero(cpu, sizeof(cpu));
                mark.pixels = cpu, mark.pitch = DL_W, mark.width = DL_W, mark.height = DL_H, mark.top = 0, mark.bottom = DL_H, mark.scale = (int)s;
                ui_icon(&mark, UI_ICON_APPS, 10, 10, (int)(16 * s / 100), 0, 0xff3a8f5cu);
                for (unsigned long i = 0; i < DL_W * DL_H; i++)
                        painted += cpu[i] != 0;
                dl_probe_reset();
                mark.pixels = dl_probe_pixels, mark.scene = &dl_probe_scene;
                ui_icon(&mark, UI_ICON_APPS, 10, 10, (int)(16 * s / 100), 0, 0xff3a8f5cu);
                failed += dl_probe_writes() != 0 || dl_probe_scene.lost || dl_probe_scene.count != 4 || painted == 0;
        }
        check("draw list: the apps mark records its four tiles at 100, 150, 200 and 300 percent and paints nothing, and paints them with no scene", failed == 0);

        // The speaker and the microphone cells: their rings and boxes are recorded (kit boxes, proven on the GPU by CHECK_gpu_kit).
        // The cells are those of the same band's tray, with the sound server there and a program recording.
        memory_zero(&view, sizeof(view));
        dl_probe_reset();
        view.width = DL_W, view.height = DL_H, view.scale = 100, view.pixels = dl_probe_pixels, view.scene = &dl_probe_scene;
        taskbar_volume_set(1, 60, 0, 0);
        taskbar_mic_set(1, 0);
        taskbar_volume_cell(&view, taskbar_tree(&view), taskbar_node(&view, TASKBAR_VOLUME), 0xffffffffu, 0xff000000u);
        check("draw list: the speaker cell records, painting nothing", dl_probe_writes() == 0 && !dl_probe_scene.lost && dl_probe_scene.count > 0);
        dl_probe_reset();
        taskbar_mic_cell(&view, taskbar_tree(&view), taskbar_node(&view, TASKBAR_MIC));
        check("draw list: the microphone cell records, painting nothing", dl_probe_writes() == 0 && !dl_probe_scene.lost && dl_probe_scene.count > 0);

        // The painters that are still the processor's (R3 and R4): named known diffs. Their frames fall back to the processor.
        static unsigned int glass[64 * 64];

        dl_probe_reset();
        view.pixels = dl_probe_pixels, view.scene = &dl_probe_scene, view.glass = glass, view.scale = 100;
        taskbar_glass_tab(&view, 0, 0, 64, 0, 0, DL_W, 5, 5, 20, 20);
        string_format(log, "  known diff: taskbar_glass_tab (R3): the frame is the picture's, lost=%p, writes=%p\n", (positive)dl_probe_scene.lost,
                      (positive)dl_probe_writes());
        if (fonts)
        {
        dl_probe_reset();
        dl_probe_ui(&probe, 1);
        ui_cell_line(&probe, 5, 5, 100, 20, "Terminal", 0xffffffffu, 0xff000000u);
        string_format(log, "  known diff: ui_cell_line (R4): the frame is the picture's, lost=%p, writes=%p\n", (positive)dl_probe_scene.lost,
                      (positive)dl_probe_writes());
        dl_probe_reset();
        dl_probe_ui(&probe, 1);
        ui_cell_turned(&probe, 5, 5, 20, 100, "Terminal", FONT_TOPS_LEFT, 0xffffffffu, 0xff000000u);
        string_format(log, "  known diff: ui_cell_turned (R4): the frame is the picture's, lost=%p, writes=%p\n", (positive)dl_probe_scene.lost,
                      (positive)dl_probe_writes());
        }
        else
                string_format(log, "  the cell text and input probes need the regular font as argument 1 (skipped)\n");
}

/*
        The app's side (ui.c's ui_app_present, ui_app_run's list): a window whose table has its lists, in list mode, records its frame into
        its row's list and publishes it with the picture's knock (buffer 0), and paints no pixel; a frame that records nothing is discarded
        and painted as pixels; a row the compositor refused (list_state 2) is painted as pixels; and with list mode 0 nothing is recorded.
        The table is a span of the check's own: its header, the two pictures of row 0 and the lists after them, as windows_buffer and
        window_list_of read them.
*/
#define DL_BUFFER 155648ul // a picture of DL_W by DL_H, a page rounded
// The table the list rows share: a header, the pictures of DL_ROWS rows and a list, mapped at its first use (dl_table).
#define DL_ROWS 32
#define DL_SPAN (WINDOWS_HEADER + 2 * DL_ROWS * DL_BUFFER + WINDOW_LIST_BYTES)
static struct windows *dl_span_at;

static struct windows *dl_table(void)
{
        if (!dl_span_at)
        {
                long got = compositor_call(syscall(mmap), 0, DL_SPAN, 3, 0x4022, (positive)-1, 0);

                dl_span_at = got < 0 && got > -4096 ? 0 : (struct windows *)got;
        }
        return dl_span_at;
}
static struct ui dl_app_ui;

static void dl_app_draw(unsigned int address_to pixels)
{
        ui_begin(&dl_app_ui, pixels, DL_W, DL_W, DL_H);
        dl_paint(&dl_app_ui, 1);
}

static void dl_app_nothing(unsigned int address_to pixels)
{
        (void)pixels;
}

static int dl_zero_words(const unsigned int address_to pixels, unsigned long count)
{
        for (unsigned long i = 0; i < count; i++)
                if (pixels[i])
                        return 0;
        return 1;
}

static void dl_app_rows(void)
{
        struct windows address_to table = dl_table();
        struct window_row own = {table, 0, -1, -1, 0};
        static struct ui_draw_view view;
        int shown = -1, dirty = 1;
        unsigned int gen_before;

        table->pixels = WINDOWS_HEADER, table->buffer = DL_BUFFER;
        table->lists = WINDOWS_LISTS_AT(DL_BUFFER, DL_ROWS), table->list_bytes = WINDOW_LIST_BYTES, table->capacity = DL_ROWS;
        table->list_mode = 1;
        // The row starts as one the compositor has not refused: the list rows before these share the table and end on a refused row (list_state 2, the
        // partial list's), which ui_list_wanted rightly paints as pixels.
        table->list_state[0] = 0;
        // The pictures start clear: the list rows before these wrote a marker into buffer 0.
        memory_zero(windows_buffer(table, 0, 0), DL_BUFFER);
        memory_zero(windows_buffer(table, 0, 1), DL_BUFFER);
        ui_app_ui = &dl_app_ui, ui_app_row = 0;
        dl_setup(&dl_app_ui, dl_pixels);
        check("app list: the row's list takes its room", ui_draw_list_init(&ui_app_list, window_list_of(table, 0), WINDOW_LIST_BYTES, WINDOW_LIST_ROOM));

        // A frame in list mode: recorded, published and knocked from buffer 0, the pixels of the picture untouched.
        check("app list: the frame is taken", ui_app_present(&own, &shown, &dirty, DL_W, DL_H, dl_app_draw));
        check("app list: the frame's picture is buffer 0 and its frame counted up", shown == 0 && table->buffer_index[0] == 0 && table->frame[0] == 1);
        check("app list: the frame is clean, and nothing is due", !dirty && dl_app_ui.scene == null);
        check("app list: no pixel of the pictures is written", dl_zero_words(windows_buffer(table, 0, 0), DL_BUFFER / 4) && dl_zero_words(windows_buffer(table, 0, 1), DL_BUFFER / 4));
        check("app list: the list holds the frame", ui_draw_list_open(window_list_of(table, 0), WINDOW_LIST_BYTES, &view) == 1 && view.width == DL_W &&
              view.height == DL_H && view.count >= 4);
        dl_reference(1);
        check("app list: the frame reads back op for op, as the reference records it", view.count == dl_ref_scene.count && dl_same_ops(view.ops, view.count));

        // A frame that records nothing (its painter reached no recorded op) is discarded and painted as pixels.
        table->shown[0] = table->frame[0];
        gen_before = ((unsigned int address_to)window_list_of(table, 0))[UI_DRAW_LIST_GEN];
        dirty = 1;
        check("app list: a frame that records nothing is painted as pixels", ui_app_present(&own, &shown, &dirty, DL_W, DL_H, dl_app_nothing) && shown == 1 && !dirty);
        check("app list: a discarded frame publishes no list", ((unsigned int address_to)window_list_of(table, 0))[UI_DRAW_LIST_GEN] == gen_before && dl_app_ui.scene == null);

        // A row the compositor refused: ui_app_present reads table->buffer_index so a stale local shown == 0 syncs and flips cleanly to slot 1.
        table->shown[0] = table->frame[0];
        table->buffer_index[0] = 0;
        shown = 0;
        table->list_state[0] = 2;
        dirty = 1;
        check("app list: a refused row syncs front from table->buffer_index and paints into slot 1",
              ui_app_present(&own, &shown, &dirty, DL_W, DL_H, dl_app_draw) && shown == 1 && table->buffer_index[0] == 1 && !dirty);
        check("app list: and no list is published for it", ((unsigned int address_to)window_list_of(table, 0))[UI_DRAW_LIST_GEN] == gen_before);
        table->list_state[0] = 0;

        // List mode 0: every frame is the program's pixels.
        table->list_mode = 0;
        table->shown[0] = table->frame[0];
        dirty = 1;
        check("app list: with list mode 0 a frame is painted as pixels", ui_app_present(&own, &shown, &dirty, DL_W, DL_H, dl_app_draw) && !dirty);
        check("app list: and no list is published with list mode 0", ((unsigned int address_to)window_list_of(table, 0))[UI_DRAW_LIST_GEN] == gen_before);
        table->list_mode = 1;
}

/*
        The compositor's side of a list: a renderer the check draws (the stub of compositor.c is the GPU's, not written yet). It records what it was
        given, marks the picture's first pixel and sets a damage when its answer is 1, and answers dl_render_answer.
*/
static int dl_render_calls, dl_render_answer;
static const unsigned int address_to dl_render_list;
static int dl_render_width, dl_render_height;
static unsigned long dl_render_pitch;

static int compositor_gpu_list_render(int row, const unsigned int address_to list, unsigned int address_to picture, unsigned long pitch_words,
                                      int width, int height, int address_to damage)
{
        (void)row;
        dl_render_calls++;
        dl_render_list = list, dl_render_width = width, dl_render_height = height, dl_render_pitch = pitch_words;
        if (dl_render_answer != 1)
                return 0;
        picture[0] = 0xff00ff00u;
        damage[0] = 5, damage[1] = 6, damage[2] = 40, damage[3] = 50;
        return 1;
}

// A list published from the kit's app ui (the frame ui_app_present would publish, without the knock).
static void dl_publish(void)
{
        dl_setup(&dl_app_ui, dl_pixels);
        ui_scene_begin(&dl_app_ui, &ui_app_list);
        ui_begin(&dl_app_ui, dl_pixels, DL_W, DL_W, DL_H);
        dl_paint(&dl_app_ui, 1);
        ui_scene_end(&dl_app_ui, &ui_app_list);
}

static void dl_list_rows(void)
{
        struct windows address_to table = dl_table();
        unsigned int address_to head;
        int damage[4] = {0, 0, 0, 0};
        int calls;
        unsigned int invalid;

        table->pixels = WINDOWS_HEADER, table->buffer = DL_BUFFER;
        table->lists = WINDOWS_LISTS_AT(DL_BUFFER, DL_ROWS), table->list_bytes = WINDOW_LIST_BYTES, table->capacity = DL_ROWS;
        head = (unsigned int address_to)window_list_of(table, 0);
        compositor_rows_make(compositor_rows_in_use); // the compositor's rows, which nothing before these rows made
        compositor.table = table, compositor.pixels = WINDOWS_HEADER, compositor.buffer = DL_BUFFER;
        compositor.front[0] = 0;
        table->stride = DL_W, table->list_mode = 1, table->list_refused = 0;
        table->list_state[0] = 0, table->list_seen[0] = 0;
        ui_draw_list_init(&ui_app_list, (unsigned char address_to)head, WINDOW_LIST_BYTES, WINDOW_LIST_ROOM);
        dl_publish();

        // A new list the renderer draws: the picture is its, the damage is its, and the row's state is 1.
        dl_render_answer = 1, dl_render_calls = 0;
        compositor_lists_take(0, DL_W, DL_H, damage);
        check("compositor list: a new list is drawn by the renderer, once",
              dl_render_calls == 1 && dl_render_list == (const unsigned int address_to)head && dl_render_width == DL_W && dl_render_height == DL_H &&
                  dl_render_pitch == DL_W);
        check("compositor list: the renderer's picture is buffer 0 and its damage stands",
              windows_buffer(table, 0, 0)[0] == 0xff00ff00u && damage[0] == 5 && damage[1] == 6 && damage[2] == 40 && damage[3] == 50);
        check("compositor list: the row is rendered from its list, and that generation is seen", table->list_state[0] == 1 && table->list_seen[0] == 2);

        // The renderer refuses: the row is refused, its whole picture damaged, and counted.
        dl_publish();
        dl_render_answer = 0, dl_render_calls = 0;
        damage[0] = damage[1] = damage[2] = damage[3] = 9;
        compositor_lists_take(0, DL_W, DL_H, damage);
        check("compositor list: a refused list is refused, counted, and the whole picture damaged",
              dl_render_calls == 1 && table->list_state[0] == 2 && table->list_refused == 1 && damage[2] == 0);

        // A picture that is not a list (its generation seen already) clears the refusal: the next frame is tried as a list.
        calls = dl_render_calls;
        compositor_lists_take(0, DL_W, DL_H, damage);
        check("compositor list: a picture of the program's clears the refusal, and no list is drawn for it",
              dl_render_calls == calls && table->list_state[0] == 0 && table->list_refused == 1);

        // A list a publish is writing (an odd generation) is no refusal: it is not taken, drawn, counted or seen, and the take after the publish
        // draws it.
        {
                unsigned int state = table->list_state[0], refused = table->list_refused, seen = table->list_seen[0];
                int taken;

                dl_publish();
                head[UI_DRAW_LIST_GEN] |= 1;
                calls = dl_render_calls;
                invalid = table->list_invalid[WINDOW_LIST_PUBLISHING];
                taken = compositor_lists_take(0, DL_W, DL_H, damage);
                check("compositor list: a list a publish is writing is not taken, drawn, refused or counted, and its generation is not seen",
                      taken < 0 && dl_render_calls == calls && table->list_state[0] == state && table->list_refused == refused &&
                          table->list_seen[0] == seen && table->list_invalid[WINDOW_LIST_PUBLISHING] == invalid);
                head[UI_DRAW_LIST_GEN] &= ~1u;
                dl_render_answer = 1;
                check("compositor list: and the take after the publish draws it",
                      compositor_lists_take(0, DL_W, DL_H, damage) == 1 && dl_render_calls == calls + 1 && table->list_state[0] == 1);
                dl_render_answer = 0;
        }

        // A list whose count is past its room is refused without the renderer, and counted as the header's.
        dl_publish();
        head[UI_DRAW_LIST_COUNT] = WINDOW_LIST_ROOM + 1;
        calls = dl_render_calls;
        invalid = table->list_invalid[WINDOW_LIST_HEADER];
        compositor_lists_take(0, DL_W, DL_H, damage);
        check("compositor list: a list past its room is refused without the renderer", dl_render_calls == calls && table->list_state[0] == 2);
        check("compositor list: and counted as a header that is not the kit's", table->list_invalid[WINDOW_LIST_HEADER] == invalid + 1);

        // A list of another size than the picture is taken at is refused, counted as the size's, and both sizes are kept.
        dl_publish();
        calls = dl_render_calls;
        invalid = table->list_invalid[WINDOW_LIST_SIZE];
        compositor_lists_take(0, DL_W - 8, DL_H, damage);
        check("compositor list: a list of another size is refused without the renderer, counted as the size's",
              dl_render_calls == calls && table->list_state[0] == 2 && table->list_invalid[WINDOW_LIST_SIZE] == invalid + 1);
        check("compositor list: and the list's size and the size the picture was taken at are kept",
              table->list_size_list[0] == DL_W && table->list_size_list[1] == DL_H && table->list_size_taken[0] == DL_W - 8 &&
                  table->list_size_taken[1] == DL_H);

        // List mode 0: a new list is not drawn, its generation is taken as seen, and the refusal is over; with the mode back on it is not drawn again.
        dl_publish();
        table->list_mode = 0;
        calls = dl_render_calls;
        compositor_lists_take(0, DL_W, DL_H, damage);
        check("compositor list: with list mode 0 a new list is not drawn", dl_render_calls == calls && table->list_state[0] == 0);
        table->list_mode = 1;
        compositor_lists_take(0, DL_W, DL_H, damage);
        check("compositor list: a list taken in list mode 0 is not drawn when the mode is on", dl_render_calls == calls);
        /* A partial list published immediately after refusal (compositor_listed[0] == 0) must be refused with COMPOSITOR_LIST_PARTIAL. */
        {
                struct ui_draw_view pv = {0};

                dl_publish();
                if (ui_draw_list_open((const unsigned char address_to)head, WINDOW_LIST_BYTES, &pv) == 1)
                {
                        head[UI_DRAW_LIST_DX0] = 4; /* the damage's x0 past the picture's left: a partial list */
                        compositor_listed[0] = 0;
                        compositor_lists_take(0, DL_W, DL_H, damage);
                        check("compositor list: partial list right after pixel fallback is refused with COMPOSITOR_LIST_PARTIAL",
                              table->list_state[0] == 2 && compositor_list_why == COMPOSITOR_LIST_PARTIAL);
                }
        }
        /*
                A row the program before left a list in, taken by the next (windows_reset_row): the new window's first pictures, of pixels or
                before its own list is made, are not read as a list of its own, at another size (refused and counted as the size's: on the
                owner's machine 8 of them, a 560 by 256 list on a window taken at 1920 by 1080) or at the same (drawn into the new window as
                its picture); and the list the new program makes is drawn.
        */
        {
                unsigned int refused, sized;

                dl_publish();
                windows_reset_row(table, 0);
                refused = table->list_refused, sized = table->list_invalid[WINDOW_LIST_SIZE];
                calls = dl_render_calls;
                dl_render_answer = 1;
                compositor_lists_take(0, DL_W + 64, DL_H + 32, damage);
                check("compositor list: a row's next window, larger, is not held to the list the one before left: nothing refused or counted",
                      table->list_refused == refused && table->list_invalid[WINDOW_LIST_SIZE] == sized && table->list_state[0] == 0 &&
                          dl_render_calls == calls);
                dl_publish();
                windows_reset_row(table, 0);
                compositor_lists_take(0, DL_W, DL_H, damage);
                check("compositor list: and one of the same size is not drawn from that list as its picture",
                      dl_render_calls == calls && table->list_state[0] == 0 && table->list_refused == refused);
                ui_draw_list_init(&ui_app_list, (unsigned char address_to)head, WINDOW_LIST_BYTES, WINDOW_LIST_ROOM);
                dl_publish();
                check("compositor list: the list the row's new program makes is drawn",
                      compositor_lists_take(0, DL_W, DL_H, damage) == 1 && dl_render_calls == calls + 1 && table->list_state[0] == 1);
                dl_render_answer = 0;
        }
        table->list_mode = 1;
}

// The back-off (ui.c's ui_list_wanted, ui_list_back_lost, ui_list_back_whole): a window's tries are run as ui_app_present runs them, a
// recorder that loses every "every"-th try (0 never). A switch is a list frame followed by a pixel frame; the rows bound the switches and the tries.
static void dl_backoff_run(int every, int frames, int *switches, int *tries, int *lists)
{
        static struct windows *table_scratch;
        struct windows *const table_at = windows_scratch_table(&table_scratch);
        struct window_row own = {.table = table_at, .row = 0};
        int last_list = 0;

        windows_scratch_zero(table_at);
        table_at->list_mode = 1;
        ui_app_list.ready = 1, ui_app_row = 0, ui_app_ui = &dl_live;
        ui_list_skip = ui_list_wait = ui_list_whole_run = 0;
        dl_live.width = DL_W, dl_live.height = DL_H, dl_live.scale = 100;
        *switches = *tries = *lists = 0;
        for (int f = 0; f < frames; f++)
        {
                if (!ui_list_wanted(&own))
                {
                        *switches += last_list, last_list = 0;
                        continue;
                }
                (*tries)++;
                if (every && *tries % every == 0)
                {
                        ui_list_back_lost(&dl_live);
                        *switches += last_list, last_list = 0;
                }
                else
                {
                        ui_list_back_whole();
                        (*lists)++, last_list = 1;
                }
        }
        ui_app_list.ready = 0, ui_app_ui = null;
}

static void dl_backoff_rows(void)
{
        int switches, tries, lists;

        dl_backoff_run(0, 200, &switches, &tries, &lists);
        check("draw list back-off: a window that never loses draws every frame as a list", lists == 200 && switches == 0 && tries == 200);
        dl_backoff_run(1, 200, &switches, &tries, &lists);
        check("draw list back-off: a window whose every frame is lost paints pixels, with at most a few tries in 200 frames", lists == 0 && tries <= 6 && switches == 0);
        dl_backoff_run(2, 200, &switches, &tries, &lists);
        check("draw list back-off: a recorder that loses every second try makes a few switches in 200 frames, not one per loss", switches <= 6 && tries <= 12);
        dl_backoff_run(5, 200, &switches, &tries, &lists);
        check("draw list back-off: a recorder that loses every fifth try makes a few switches in 200 frames", switches <= 6 && lists > 0);
        dl_backoff_run(0, 200, &switches, &tries, &lists);
        check("draw list back-off: the state is reset for the next window", switches == 0);
}

b32 main(void)
{
        compositor_rows_make(compositor_rows_in_use); // the compositor's rows, made before any check reads one
        // The transitions run as designed in every check (ui_paced at 100): the durations the rows pin are the designed ones.
        ui_pace = UI_PACE_DESIGNED;
        dl_list_rows();
        dl_app_rows();
        dl_rows();
        dl_backoff_rows();
        dl_probes();
        return test_report(null);
}
#endif /* CHECK_draw_list */

#ifdef CHECK_gpuasm
#include "../../src/lib.util.c"
#ifdef GPUASM_SOURCE
#include GPUASM_SOURCE
#else
#include "../../src/build/gpuasm.c"
#endif
/*
        The build's assembler for the GPU's kernels (src/build/gpuasm.c), asked what it refuses and what it makes of
        the smallest kernels; and, given a source and an output on the command line, the assembler itself, which is how
        test/run holds a copy of it with one opcode changed to LLVM's words and requires the difference to show.
        What it makes of src/canvas/gpu.asm and test/gpu_stress.asm is compared with llvm-mc by test/gpu_kernels.py.
*/
#define SHARED_counted
#include "../checks.c"


#undef SHARED_counted

#define HEAD ".amdgcn_target \"amdgcn-amd-amdhsa--gfx1036\"\n.text\nk:\n"
#define BLOCK(vgprs)                                                                                      \
        ".size k, .-k\n.rodata\n.amdhsa_kernel k\n.amdhsa_group_segment_fixed_size 0\n"                   \
        ".amdhsa_private_segment_fixed_size 0\n.amdhsa_kernarg_size 64\n"                                 \
        ".amdhsa_user_sgpr_private_segment_buffer 1\n.amdhsa_user_sgpr_kernarg_segment_ptr 1\n"           \
        ".amdhsa_system_sgpr_workgroup_id_x 1\n.amdhsa_system_sgpr_workgroup_id_y 1\n"                    \
        ".amdhsa_next_free_vgpr " vgprs "\n.amdhsa_next_free_sgpr 24\n.amdhsa_wavefront_size32 0\n"       \
        ".amdhsa_forward_progress 1\n"                                                                   \
        ".end_amdhsa_kernel\n"
#define KERNEL(code, vgprs) HEAD code BLOCK(vgprs)

//      The assembler stops, with this line.
static p8 refuses(string_address source, string_address message)
{
        return !gpuasm_assemble("bad.asm", source) && string_equals(gpuasm_message, message);
}

//      The assembler makes this text.
static p8 makes(string_address source, string_address text)
{
        return gpuasm_assemble("bad.asm", source) && string_equals((string_address)gpuasm_output, text);
}

#define refused(name, source, message) check(name, refuses(source, message))

static b32 gpuasm_files(string_address from, string_address to)
{
        static p8 text[1 << 20];
        bipolar got = file_slurp(from, text, sizeof(text));
        bipolar handle;

        if (got < 0 || (positive)got + 1 >= sizeof(text))
                return string_report(log_error, 2, "gpuasm: cannot read %s\n", from);
        if (!gpuasm_assemble(from, text))
                return string_report(log_error, 1, "%s\n", gpuasm_message);
        handle = system_open_output_at(AT_FDCWD, to, true, 0644);
        if (handle < 0 || system_write_all((positive)handle, gpuasm_output, gpuasm_output_length) != gpuasm_output_length)
                return string_report(log_error, 2, "gpuasm: cannot write %s\n", to);
        system_close(handle);
        return 0;
}

b32 main()
{
        if (program_argument_count() == 3)
                return gpuasm_files(program_argument_list()[1], program_argument_list()[2]);

        //      What it makes: a kernel's two resource words and its code, the vector registers rounded up to fours.
        check("a kernel of one instruction",
              makes(KERNEL("\ts_endpgm\n", "1"),
                    "// Generated by build gpu-kernels from bad.asm. Do not edit; edit the .asm and build again.\n"
                    "static const unsigned int gpu_k_k[] = {\n    0xe0ac0000, 0x0000018c, 0xbf810000};\n"));
        check("nine vector registers are a granule of three",
              makes(KERNEL("\ts_endpgm\n", "9"),
                    "// Generated by build gpu-kernels from bad.asm. Do not edit; edit the .asm and build again.\n"
                    "static const unsigned int gpu_k_k[] = {\n    0xe0ac0002, 0x0000018c, 0xbf810000};\n"));
        check("a macro is the instructions it holds",
              makes(HEAD ".macro two a, b\n\ts_mov_b32 \\a, \\b ; a comment\n\ts_mov_b32 \\b, \\a\n.endm\n\ttwo s1, s2\n\ts_endpgm\n" BLOCK("1"),
                    "// Generated by build gpu-kernels from bad.asm. Do not edit; edit the .asm and build again.\n"
                    "static const unsigned int gpu_k_k[] = {\n    0xe0ac0000, 0x0000018c, 0xbe810302, 0xbe820301, 0xbf810000};\n"));
        check("an .irp is the instructions it repeats",
              makes(HEAD "\t.irp n, 1, 2\n\ts_mov_b32 s\\n, 0\n\t.endr\n\ts_endpgm\n" BLOCK("1"),
                    "// Generated by build gpu-kernels from bad.asm. Do not edit; edit the .asm and build again.\n"
                    "static const unsigned int gpu_k_k[] = {\n    0xe0ac0000, 0x0000018c, 0xbe810380, 0xbe820380, 0xbf810000};\n"));
        check("a branch is counted in words past the next",
              makes(HEAD "\ts_branch .t\n\ts_endpgm\n.t:\n\ts_endpgm\n" BLOCK("1"),
                    "// Generated by build gpu-kernels from bad.asm. Do not edit; edit the .asm and build again.\n"
                    "static const unsigned int gpu_k_k[] = {\n    0xe0ac0000, 0x0000018c, 0xbf820001, 0xbf810000, 0xbf810000};\n"));
        check("a load's offset is 21 bits, signed",
              makes(KERNEL("\ts_load_dword s1, s[0:1], -4\n", "1"),
                    "// Generated by build gpu-kernels from bad.asm. Do not edit; edit the .asm and build again.\n"
                    "static const unsigned int gpu_k_k[] = {\n    0xe0ac0000, 0x0000018c, 0xf4000040, 0xfa1ffffc};\n"));
        check("a memory offset is 12 bits, signed",
              makes(KERNEL("\tglobal_load_dword v34, v30, s[102:103] offset:-1\n", "1"),
                    "// Generated by build gpu-kernels from bad.asm. Do not edit; edit the .asm and build again.\n"
                    "static const unsigned int gpu_k_k[] = {\n    0xe0ac0000, 0x0000018c, 0xdc308fff, 0x2266001e};\n"));
        check("a literal that is an inline constant is the constant",
              makes(KERNEL("\ts_mov_b32 s1, 0xffffffff\n\ts_mov_b32 s1, 0x3f800000\n\ts_mov_b32 s1, 65\n", "1"),
                    "// Generated by build gpu-kernels from bad.asm. Do not edit; edit the .asm and build again.\n"
                    "static const unsigned int gpu_k_k[] = {\n    0xe0ac0000, 0x0000018c, 0xbe8103c1, 0xbe8103f2, 0xbe8103ff, 0x00000041};\n"));

        //      What it refuses, and where: the file and line, and what was wrong, the text it was about in quotes.
        refused("an instruction it has no row for", KERNEL("\ts_bogus s1, s2\n", "1"), "bad.asm:4: unknown instruction 's_bogus'");
        refused("an instruction in an encoding it has not been checked in", KERNEL("\ts_mov_b32_e32 s1, s2\n", "1"),
                "bad.asm:4: unknown instruction 's_mov_b32_e32'");
        refused("a scalar register that does not exist", KERNEL("\ts_mov_b32 s1, s2\n\ts_mov_b32 s106, s1\n", "1"),
                "bad.asm:5: a register out of range 's106'");
        refused("a vector register that does not exist", KERNEL("\tv_mov_b32 v256, v0\n", "1"), "bad.asm:4: a register out of range 'v256'");
        refused("a register that is not one", KERNEL("\ts_mov_b32 s1, sx\n", "1"), "bad.asm:4: unknown operand 'sx'");
        refused("a range that runs backwards", KERNEL("\ts_mov_b64 s[3:2], s[0:1]\n", "1"),
                "bad.asm:4: a register range that is not low:high 's[3:2]'");
        refused("a pair that is not even", KERNEL("\ts_mov_b64 s[1:2], s[0:1]\n", "1"),
                "bad.asm:4: a scalar register range that is not aligned 's[1:2]'");
        refused("a quad that is not a multiple of four", KERNEL("\ts_load_dwordx4 s[2:5], s[0:1], 0\n", "1"),
                "bad.asm:4: a scalar register range that is not aligned 's[2:5]'");
        refused("a range of the wrong width", KERNEL("\ts_mov_b64 s[0:2], s[0:1]\n", "1"),
                "bad.asm:4: the wrong number of registers 's[0:2]'");
        refused("a vector register for a scalar", KERNEL("\ts_mov_b32 s1, v1\n", "1"),
                "bad.asm:4: a vector register where a scalar one is wanted 'v1'");
        refused("a scalar register for a vector destination", KERNEL("\tv_mov_b32 s1, v1\n", "1"),
                "bad.asm:4: a vector register is wanted 's1'");
        refused("vcc where 32 bits are wanted", KERNEL("\ts_mov_b32 s1, vcc\n", "1"),
                "bad.asm:4: a 64-bit register where 32 bits are wanted 'vcc'");
        refused("a label that is never defined", KERNEL("\ts_mov_b32 s1, s2\n\ts_branch .nowhere\n\ts_endpgm\n", "1"),
                "bad.asm:5: a branch to a label that is never defined: '.nowhere'");
        refused("a label defined twice", HEAD "\ts_endpgm\n.a:\n\ts_endpgm\n.a:\n\ts_endpgm\n" BLOCK("1"),
                "bad.asm:7: a label defined twice '.a'");
        refused("a literal where the form has none", KERNEL("\tv_pk_min_u16 v1, v2, 0x1234\n", "1"),
                "bad.asm:4: a literal where this form has none '0x1234'");
        refused("a literal in a 64-bit operand", KERNEL("\tv_cmp_gt_u64 vcc, 0x12345678, v[2:3]\n", "1"),
                "bad.asm:4: a 64-bit operand takes an inline constant only '0x12345678'");
        refused("two literals", KERNEL("\tv_mad_u32_u24 v1, 0x1234, v2, 0x5678\n", "1"),
                "bad.asm:4: only one literal to an instruction '0x5678'");
        refused("a constant of more than 32 bits", KERNEL("\ts_mov_b32 s1, 0x100000000\n", "1"), "bad.asm:4: a constant of more than 32 bits '0x100000000'");
        refused("a number written with a leading zero", KERNEL("\ts_mov_b32 s1, 010\n", "1"), "bad.asm:4: unknown operand '010'");
        refused("an operand too many", KERNEL("\ts_mov_b32 s1, s2, s3\n", "1"), "bad.asm:4: the wrong number of operands for 's_mov_b32'");
        refused("an operand too few", KERNEL("\ts_mov_b32 s1\n", "1"), "bad.asm:4: the wrong number of operands for 's_mov_b32'");
        refused("an empty operand", KERNEL("\ts_mov_b32 s1,\n", "1"), "bad.asm:4: an empty operand");
        refused("clamp where the instruction has none", KERNEL("\tv_and_b32 v1, v2, v3 clamp\n", "1"),
                "bad.asm:4: clamp on an instruction that has none: 'v_and_b32'");
        refused("_e32 for operands the short encoding cannot hold", KERNEL("\tv_add_nc_u32_e32 v1, v2, s3\n", "1"),
                "bad.asm:4: operands that do not fit the 32-bit encoding of 'v_add_nc_u32_e32'");
        refused("a wait counter that is not one", KERNEL("\ts_waitcnt foo(0)\n", "1"), "bad.asm:4: unknown counter 'foo'");
        refused("a wait counter past its largest", KERNEL("\ts_waitcnt expcnt(8)\n", "1"), "bad.asm:4: a counter past its largest 'expcnt'");
        refused("a branch to something that is not a name", KERNEL("\ts_branch 5\n\ts_endpgm\n", "1"),
                "bad.asm:4: a branch to a label that is never defined: '5'");
        refused("an offset past 21 bits", KERNEL("\ts_load_dword s1, s[0:1], 0x100000\n", "1"),
                "bad.asm:4: an offset of -0x100000 to 0xfffff '0x100000'");
        refused("an offset before 21 bits", KERNEL("\ts_load_dword s1, s[0:1], -0x100001\n", "1"),
                "bad.asm:4: an offset of -0x100000 to 0xfffff '-0x100001'");
        refused("a memory offset past 12 bits", KERNEL("\tglobal_load_dword v1, v2, s[4:5] offset:2048\n", "1"),
                "bad.asm:4: an offset of -2048 to 2047, not 'offset:2048'");
        refused("an offset on an instruction with none", KERNEL("\ts_mov_b32 s1, s2 offset:4\n", "1"),
                "bad.asm:4: an offset on an instruction that has none: 's_mov_b32'");
        refused("a 64-bit operand is not its low 32 bits", KERNEL("\ts_mov_b64 s[2:3], 0xffffffff\n", "1"),
                "bad.asm:4: a 64-bit operand takes an inline constant only '0xffffffff'");
        refused("three scalar values at once", KERNEL("\tv_mad_u32_u24 v1, s1, s2, s3\n", "1"),
                "bad.asm:4: more than two scalar values to read at once: 'v_mad_u32_u24'");
        refused("two registers and a literal", KERNEL("\tv_mad_u32_u24 v1, s1, 0x1234, s3\n", "1"),
                "bad.asm:4: more than two scalar values to read at once: 'v_mad_u32_u24'");
        refused("a load into vcc", KERNEL("\ts_load_dwordx2 vcc, s[0:1], 0\n", "1"),
                "bad.asm:4: a scalar register to load into 'vcc'");
        refused("an encoding named twice", KERNEL("\tv_cndmask_b32_e64_e64 v1, v2, v3, vcc\n", "1"),
                "bad.asm:4: unknown instruction 'v_cndmask_b32_e64_e64'");
        refused("a kernel defined twice", KERNEL("\ts_endpgm\n", "1") BLOCK("1"), "bad.asm:22: a kernel defined twice 'k'");
        refused("a kernel that is not a C name", HEAD "\ts_endpgm\n.size k, .-k\n.rodata\n.amdhsa_kernel k.x\n",
                "bad.asm:7: a kernel's name is a C name, not 'k.x'");
        refused("a directive it does not know", KERNEL("\t.bogus 1\n", "1"), "bad.asm:4: unknown directive '.bogus'");
        refused("a section it does not know", KERNEL("\t.data\n", "1"), "bad.asm:4: unknown directive '.data'");
        refused("a macro called with the wrong number of arguments",
                HEAD ".macro two a, b\n\ts_mov_b32 \\a, \\b\n.endm\n\ttwo s1\n" BLOCK("1"),
                "bad.asm:7: the wrong number of arguments to the macro 'two'");
        refused("a mistake inside a macro is the line that called it",
                HEAD ".macro one a\n\ts_mov_b32 \\a, s999\n.endm\n\tone s1\n" BLOCK("1"),
                "bad.asm:7: in macro one: a register out of range 's999'");
        refused("a macro that is never closed", HEAD ".macro two a, b\n\ts_mov_b32 \\a, \\b\n",
                "bad.asm:4: a block that is never closed, wanted '.endm'");
        refused("text after an .endm", HEAD ".macro one\n.endm[\n", "bad.asm:5: text after the close of a block: '['");
        refused("text after an .end_amdhsa_kernel", KERNEL("\ts_endpgm\n", "1") ".amdhsa_kernel z\n.end_amdhsa_kernel x\n",
                "bad.asm:21: text after .end_amdhsa_kernel: 'x'");
        refused("an .endm with no .macro", HEAD ".endm\n", "bad.asm:4: a close with no open '.endm'");
        refused("a macro defined twice", HEAD ".macro a\n.endm\n.macro a\n.endm\n", "bad.asm:6: a macro defined twice 'a'");
        refused("a name given to no one", KERNEL("\ts_mov_b32 s1, \\x\n", "1"),
                "bad.asm:4: a \\name that no macro or .irp gave a value 's_mov_b32 s1, \\x'");
        refused("a kernel with no .size", HEAD "\ts_endpgm\n.rodata\n.amdhsa_kernel k\n"
                ".amdhsa_group_segment_fixed_size 0\n.amdhsa_private_segment_fixed_size 0\n.amdhsa_kernarg_size 64\n"
                ".amdhsa_user_sgpr_private_segment_buffer 1\n.amdhsa_user_sgpr_kernarg_segment_ptr 1\n"
                ".amdhsa_system_sgpr_workgroup_id_x 1\n.amdhsa_system_sgpr_workgroup_id_y 1\n"
                ".amdhsa_next_free_vgpr 1\n.amdhsa_next_free_sgpr 24\n.amdhsa_wavefront_size32 0\n.amdhsa_forward_progress 1\n.end_amdhsa_kernel\n",
                "bad.asm:6: a kernel with no .size: 'k'");
        refused("a kernel with no label", ".amdgcn_target \"amdgcn-amd-amdhsa--gfx1036\"\n.text\n" BLOCK("1"),
                "bad.asm:3: .size of a name with no label 'k'");
        refused("a kernel that asks for scratch", HEAD "\ts_endpgm\n.size k, .-k\n.rodata\n.amdhsa_kernel k\n"
                ".amdhsa_private_segment_fixed_size 16\n",
                "bad.asm:8: the dispatch gives a kernel no scratch: '.amdhsa_private_segment_fixed_size'");
        refused("a property the block has not got", HEAD "\ts_endpgm\n.size k, .-k\n.rodata\n.amdhsa_kernel k\n"
                ".amdhsa_group_segment_fixed_size 0\n.end_amdhsa_kernel\n",
                "bad.asm:9: the kernel's block has no '.amdhsa_private_segment_fixed_size'");
        refused("a property it does not assemble for", HEAD "\ts_endpgm\n.size k, .-k\n.rodata\n.amdhsa_kernel k\n"
                ".amdhsa_wavefront_size32 1\n",
                "bad.asm:8: not the value this assembles for: '.amdhsa_wavefront_size32'");
        // Forward progress is the first resource word's top bit, which every kernel is assembled with: a block that leaves it to the assembler's default
        // (clear before LLVM 19, set from it) or asks for none is refused, so the words do not turn on the LLVM that is at hand.
        refused("a block that does not ask for forward progress", HEAD "\ts_endpgm\n.size k, .-k\n.rodata\n.amdhsa_kernel k\n"
                ".amdhsa_group_segment_fixed_size 0\n.amdhsa_private_segment_fixed_size 0\n.amdhsa_kernarg_size 64\n"
                ".amdhsa_user_sgpr_private_segment_buffer 1\n.amdhsa_user_sgpr_kernarg_segment_ptr 1\n"
                ".amdhsa_system_sgpr_workgroup_id_x 1\n.amdhsa_system_sgpr_workgroup_id_y 1\n"
                ".amdhsa_next_free_vgpr 1\n.amdhsa_next_free_sgpr 24\n.amdhsa_wavefront_size32 0\n.end_amdhsa_kernel\n",
                "bad.asm:18: the kernel's block has no '.amdhsa_forward_progress'");
        refused("a block that asks for no forward progress", HEAD "\ts_endpgm\n.size k, .-k\n.rodata\n.amdhsa_kernel k\n"
                ".amdhsa_forward_progress 0\n",
                "bad.asm:8: not the value this assembles for: '.amdhsa_forward_progress'");
        refused("a property it has never heard of", HEAD "\ts_endpgm\n.size k, .-k\n.rodata\n.amdhsa_kernel k\n.amdhsa_bogus 1\n",
                "bad.asm:8: unknown directive '.amdhsa_bogus'");
        refused("a property given twice", HEAD "\ts_endpgm\n.size k, .-k\n.rodata\n.amdhsa_kernel k\n"
                ".amdhsa_kernarg_size 64\n.amdhsa_kernarg_size 64\n",
                "bad.asm:9: a property given twice '.amdhsa_kernarg_size'");
        refused("a kernel block never closed", HEAD "\ts_endpgm\n.size k, .-k\n.rodata\n.amdhsa_kernel k\n",
                "bad.asm:7: an .amdhsa_kernel that is never closed");
        refused("an instruction in .rodata", HEAD ".rodata\n\ts_endpgm\n", "bad.asm:5: an instruction outside .text 's_endpgm'");
        refused("code before the target", ".text\nk:\n\ts_endpgm\n",
                "bad.asm:3: .amdgcn_target must come before the code");
        refused("a target it does not assemble for", ".amdgcn_target \"amdgcn-amd-amdhsa--gfx1030\"\n",
                "bad.asm:1: this assembles for \"amdgcn-amd-amdhsa--gfx1036\" only, not '\"amdgcn-amd-amdhsa--gfx1030\"'");
        refused("a file with no kernel", ".amdgcn_target \"amdgcn-amd-amdhsa--gfx1036\"\n.text\n\ts_endpgm\n",
                "bad.asm:3: no .amdhsa_kernel in the file");
        refused(".size of something else", HEAD "\ts_endpgm\n.size k, .-j\n", "bad.asm:5: .size is read as name, .-name only 'k, .-j'");
        refused(".type of two names", HEAD ".type k x,@function\n", "bad.asm:4: .type is read as name,@function only, not 'k x,@function'");
        refused(".type of something else", HEAD ".type k,@object\n", "bad.asm:4: .type is read as name,@function only, not 'k,@object'");

        return test_report(null);
}
#endif /* CHECK_gpuasm */

