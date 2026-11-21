/*
        Input handling: report latency, DMA/EPP/CCD0 tuning, desktop key chords, tray volume/mic/notices,
        pointer motion/buttons/wheel/pad gestures, frame-ahead prediction, chips, touch, and reader thread.
*/

static void compositor_latency(unsigned int which, positive reported, positive now)
{
        positive took = reported && now > reported ? now - reported : 0;

        compositor.latency[which].count++;
        compositor.latency[which].total += took;
        if (took > compositor.latency[which].worst)
                compositor.latency[which].worst = took;
}

static inline long long compositor_abs64(long long v) { return v < 0 ? -v : v; }
static inline long long compositor_clamp64(long long v, long long lo, long long hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline int compositor_in_row(int row, int x, int y)
{
        return x >= compositor.x[row] && x < compositor.x[row] + (int)compositor.w[row] &&
               y >= compositor.y[row] && y < compositor.y[row] + (int)compositor.h[row];
}

static void compositor_hold_touch(positive now)
{
        int zero = 0;

        compositor.hold_until = now + UI_HOLD_NS;
        if (compositor.hold >= 0 || compositor.holds == ~0ul)
                return;
        if ((compositor.hold = (int)system_open_at(AT_FDCWD, (string_address) "/dev/cpu_dma_latency", O_WRONLY | O_CLOEXEC)) < 0)
                { compositor.hold = -1, compositor.holds = ~0ul; return; }
        system_write_once(compositor.hold, &zero, sizeof(zero));
        compositor.holds++;
}

static void compositor_hold_drop(positive now)
{
        if (compositor.hold >= 0 && now >= compositor.hold_until)
                system_close(compositor.hold), compositor.hold = -1;
}

static int compositor_write_sysfs(const char *path, const void *data, unsigned long len)
{
        long fd = system_open_at(AT_FDCWD, (string_address)path, O_WRONLY | O_CLOEXEC);

        if (fd < 0)
                return 0;
        system_write_once((int)fd, data, len);
        system_close((int)fd);
        return 1;
}

static int compositor_multi_ccd(void)
{
        char l3[64], pkg[64];

        compositor_word("/sys/devices/system/cpu/cpu0/cache/index3/shared_cpu_list", l3, sizeof(l3));
        compositor_word("/sys/devices/system/cpu/cpu0/topology/package_cpus_list", pkg, sizeof(pkg));
        return l3[0] && pkg[0] && !string_equals((string_address)l3, (string_address)pkg);
}

static char compositor_game_epp_saved[32];
static int compositor_game_epp_active;

static void compositor_game_epp(int on)
{
        const char *want = on ? "performance" : compositor_game_epp_saved;
        unsigned long len;

        if (on == compositor_game_epp_active)
                return;
        if (on && !compositor_game_epp_saved[0])
                compositor_word("/sys/devices/system/cpu/cpu0/cpufreq/energy_performance_preference", compositor_game_epp_saved, sizeof(compositor_game_epp_saved));
        if (!compositor_game_epp_saved[0] || (on && string_equals((string_address)compositor_game_epp_saved, (string_address) "performance")))
                return;
        len = (unsigned long)string_length((string_address)want);
        for (unsigned int cpu = 0; cpu < 64; cpu++)
        {
                char path[96];

                windows_num_path(path, sizeof(path), "/sys/devices/system/cpu/cpu", cpu, "/cpufreq/energy_performance_preference");
                if (!compositor_write_sysfs(path, want, len))
                        break;
        }
        compositor_game_epp_active = on;
}

static int compositor_cpu_list_mask(const char *path, unsigned long mask[2])
{
        char list[64];
        const char *p = list;

        mask[0] = mask[1] = 0;
        compositor_word(path, list, sizeof(list));
        while (*p)
        {
                unsigned int lo = 0, hi = 0;

                while (*p >= '0' && *p <= '9')
                        lo = lo * 10 + (unsigned int)(*p++ - '0');
                hi = lo;
                if (*p == '-')
                {
                        p++, hi = 0;
                        while (*p >= '0' && *p <= '9')
                                hi = hi * 10 + (unsigned int)(*p++ - '0');
                }
                for (unsigned int c = lo; c <= hi && c < 128; c++)
                        mask[c >> 6] |= 1ul << (c & 63);
                if (*p == ',')
                        p++;
                else
                        break;
        }
        return (mask[0] | mask[1]) != 0;
}

static int compositor_ccd0_mask(unsigned long mask[2])
{
        return compositor_multi_ccd() &&
               compositor_cpu_list_mask("/sys/devices/system/cpu/cpu0/cache/index3/shared_cpu_list", mask);
}

static void compositor_pin_ccd0(void)
{
        unsigned long mask[2];

        if (compositor_ccd0_mask(mask))
                compositor_call(syscall(sched_setaffinity), 0, sizeof(mask), (positive)mask, 0, 0, 0);
}

static void compositor_pin_irqs_ccd0(void)
{
        static char buf[32768];
        long fd, got;

        if (!compositor_multi_ccd() || (fd = system_open_at(AT_FDCWD, (string_address) "/proc/interrupts", FILE_READ | O_CLOEXEC)) < 0)
                return;
        got = (long)compositor_call(syscall(read), (positive)fd, (positive)buf, sizeof(buf) - 1, 0, 0, 0);
        system_close((int)fd);
        if (got <= 0)
                return;
        buf[got] = 0;
        for (long at = 0; at < got;)
        {
                char *nl = (char *)memory_first_of(buf + at, '\n', (positive)(got - at));
                long line = at, stop = nl ? nl - buf : got;
                char saved = buf[stop];

                at = stop + 1, buf[stop] = 0;
                if (string_find((string_address)buf + line, (string_address) "xhci_hcd") ||
                    string_find((string_address)buf + line, (string_address) "amdgpu"))
                {
                        long p = line + (long)string_span_of_set((string_address)buf + line, (string_address) " ");
                        positive used = 0, irq = string_digits((string_address)buf + p, &used);
                        char path[64];

                        if (used > 0 && buf[p + (long)used] == ':' && irq > 0 && irq < 4096)
                                windows_num_path(path, sizeof(path), "/proc/irq/", (unsigned int)irq, "/smp_affinity"),
                                compositor_write_sysfs(path, "000000ff", 8);
                }
                buf[stop] = saved;
        }
}

static void compositor_game_watch(void)
{
        static const char *const batteries[] = {"/sys/class/power_supply/BAT0/status", "/sys/class/power_supply/BAT1/status",
                                                "/sys/class/power_supply/battery/status"};
        int game = compositor.games != ~0ul && compositor_fullscreen_up(), us = 0;

        if (game == (compositor.game >= 0))
                return;
        if (!game)
                { compositor_gpu_game_clocks(0), compositor_game_epp(0), system_close(compositor.game), compositor.game = -1; return; }
        if ((compositor.game = (int)system_open_at(AT_FDCWD, (string_address) "/dev/cpu_dma_latency", O_WRONLY | O_CLOEXEC)) < 0)
                { compositor.game = -1, compositor.games = ~0ul; return; }
        for (unsigned int at = 0; at < 3; at++)
        {
                char status[16];

                compositor_word(batteries[at], status, sizeof(status));
                us |= string_equals((string_address)status, (string_address) "Discharging");
        }
        system_write_once(compositor.game, &us, sizeof(us));
        if (!us)
                compositor_game_epp(1), compositor_gpu_game_clocks(1);
        compositor.games++;
}

// EEVDF custom 100 us runtime slice + uclamp.min boost + multi-CCD V-Cache pinning for fullscreen top games,
// plus uclamp.max cap + secondary-CCD pinning for background window processes while a fullscreen game runs.
#define COMPOSITOR_GAME_SLICE_NS 100000ull
#define COMPOSITOR_GAME_UCLAMP_MIN 512u
#define COMPOSITOR_BG_UCLAMP_MAX 256u
#define COMPOSITOR_SCHED_FLAG_UCLAMP_MIN 0x20ull
#define COMPOSITOR_SCHED_FLAG_UCLAMP_MAX 0x40ull
#define COMPOSITOR_GAME_SLICES 8
#define COMPOSITOR_BG_SLICES 32
#define COMPOSITOR_GAME_SLICE_FILE "/run/moonwater/game-slice"
static b32 compositor_game_off;
struct compositor_sched_attr
{
        unsigned int size, policy;
        unsigned long long flags;
        int nice;
        unsigned int priority;
        unsigned long long runtime, deadline, period;
        unsigned int util_min, util_max;
};
_Static_assert(sizeof(struct compositor_sched_attr) == 56, "sched_attr: SCHED_ATTR_SIZE_VER1");

static unsigned int compositor_game_slice_of(unsigned int pid, unsigned long long slice)
{
        char path[40];
        p8 buffer[2048];
        bipolar handle, got;
        unsigned int asked = 0;
        unsigned long ccd_mask[2] = {0, 0}, all_mask[2] = {~0ul, ~0ul};
        int pin_ccd = compositor_ccd0_mask(ccd_mask);

        windows_num_path(path, sizeof(path), "/proc/", pid, "/task");
        if ((handle = system_open_at(AT_FDCWD, (string_address)path, FILE_READ | O_DIRECTORY | O_CLOEXEC)) < 0)
                return 0;
        while ((got = system_call_3(syscall(getdents64), (positive)handle, (positive)buffer, sizeof(buffer))) > 0)
                for (bipolar at = 0; at < got;)
                {
                        string_address name = (string_address)(buffer + at + 19);
                        struct compositor_sched_attr attr = {0};
                        positive tid = 0;

                        at += memory_load_unaligned(p16, buffer + at + 16);
                        for (; *name >= '0' && *name <= '9'; name++)
                                tid = tid * 10 + (positive)(*name - '0');
                        if (!tid || *name || compositor_call(syscall(sched_getattr), tid, (positive)&attr, sizeof(attr), 0, 0, 0) != 0 ||
                            (attr.policy != 0 && attr.policy != 3) || (attr.runtime == COMPOSITOR_GAME_SLICE_NS) == !!slice)
                                continue;
                        attr.size = sizeof(attr), attr.runtime = slice;
                        attr.flags = COMPOSITOR_SCHED_FLAG_UCLAMP_MIN;
                        attr.util_min = slice ? COMPOSITOR_GAME_UCLAMP_MIN : 0;
                        if (compositor_call(syscall(sched_setattr), tid, (positive)&attr, 0, 0, 0, 0) != 0)
                        {
                                attr.flags = 0, attr.util_min = 0;
                                asked += compositor_call(syscall(sched_setattr), tid, (positive)&attr, 0, 0, 0, 0) == 0;
                        }
                        else
                                asked++;
                        if (pin_ccd)
                                compositor_call(syscall(sched_setaffinity), tid, sizeof(ccd_mask),
                                                (positive)(slice ? ccd_mask : all_mask), 0, 0, 0);
                }
        system_close(handle);
        return asked;
}

static void compositor_bg_throttle_of(unsigned int pid, int throttle)
{
        char path[40], comm[32];
        p8 buffer[2048];
        bipolar handle, got;
        unsigned long ccd0[2] = {0, 0}, pkg[2] = {0, 0}, bg_mask[2] = {0, 0}, all_mask[2] = {~0ul, ~0ul};
        int pin_bg = 0;

        if (throttle)
        {
                windows_num_path(path, sizeof(path), "/proc/", pid, "/comm");
                compositor_word(path, comm, sizeof(comm));
                if (string_equals((string_address)comm, (string_address) "Xwayland") ||
                    string_equals((string_address)comm, (string_address) "wineserver") ||
                    string_equals((string_address)comm, (string_address) "audio") ||
                    string_equals((string_address)comm, (string_address) "dbus"))
                        return;
        }
        if (compositor_ccd0_mask(ccd0) &&
            compositor_cpu_list_mask("/sys/devices/system/cpu/cpu0/topology/package_cpus_list", pkg))
        {
                bg_mask[0] = pkg[0] & ~ccd0[0];
                bg_mask[1] = pkg[1] & ~ccd0[1];
                pin_bg = (bg_mask[0] | bg_mask[1]) != 0;
        }
        windows_num_path(path, sizeof(path), "/proc/", pid, "/task");
        if ((handle = system_open_at(AT_FDCWD, (string_address)path, FILE_READ | O_DIRECTORY | O_CLOEXEC)) < 0)
                return;
        while ((got = system_call_3(syscall(getdents64), (positive)handle, (positive)buffer, sizeof(buffer))) > 0)
                for (bipolar at = 0; at < got;)
                {
                        string_address name = (string_address)(buffer + at + 19);
                        struct compositor_sched_attr attr = {0};
                        positive tid = 0;

                        at += memory_load_unaligned(p16, buffer + at + 16);
                        for (; *name >= '0' && *name <= '9'; name++)
                                tid = tid * 10 + (positive)(*name - '0');
                        if (!tid || *name || compositor_call(syscall(sched_getattr), tid, (positive)&attr, sizeof(attr), 0, 0, 0) != 0 ||
                            (attr.policy != 0 && attr.policy != 3))
                                continue;
                        attr.size = sizeof(attr);
                        attr.flags = COMPOSITOR_SCHED_FLAG_UCLAMP_MAX;
                        attr.util_max = throttle ? COMPOSITOR_BG_UCLAMP_MAX : 1024u;
                        (void)compositor_call(syscall(sched_setattr), tid, (positive)&attr, 0, 0, 0, 0);
                        if (pin_bg)
                                (void)compositor_call(syscall(sched_setaffinity), tid, sizeof(bg_mask),
                                                      (positive)(throttle ? bg_mask : all_mask), 0, 0, 0);
                }
        system_close(handle);
}

static void compositor_game_slice_note(const unsigned int *pids, unsigned int count)
{
        char text[COMPOSITOR_GAME_SLICES * 12];
        positive at = 0;
        bipolar handle = system_open_at_mode(AT_FDCWD, (string_address)COMPOSITOR_GAME_SLICE_FILE, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);

        if (handle < 0)
                return;
        for (unsigned int k = 0; k < count; k++)
                at += positive_into_string((string_address)text + at, (positive)pids[k]), text[at++] = '\n';
        if (at)
                system_write_once(handle, text, at);
        system_close(handle);
}

static int compositor_pid_in(const unsigned int *list, unsigned int n, unsigned int pid)
{
        for (unsigned int i = 0; i < n; i++)
                if (list[i] == pid)
                        return 1;
        return 0;
}

static void compositor_game_slice(void)
{
        static unsigned int pids[COMPOSITOR_GAME_SLICES], count;
        static unsigned int bg_pids[COMPOSITOR_BG_SLICES], bg_count;
        static b32 started;
        unsigned int now[COMPOSITOR_GAME_SLICES], nows = 0, me = (unsigned int)compositor_call(syscall(getpid), 0, 0, 0, 0, 0, 0);
        unsigned int bg_now[COMPOSITOR_BG_SLICES], bg_nows = 0;
        int rows[COMPOSITOR_GAME_SLICES];
        unsigned int tops = compositor_game_off ? 0 : compositor_fullscreen_tops(rows, COMPOSITOR_GAME_SLICES);
        b32 changed = false;

        if (!started)
        {
                started = 1;
                if (compositor.table_giver)
                {
                        char text[COMPOSITOR_GAME_SLICES * 12 + 1];
                        bipolar handle = system_open_at(AT_FDCWD, (string_address)COMPOSITOR_GAME_SLICE_FILE, FILE_READ | O_NOFOLLOW | O_CLOEXEC), got = 0;
                        unsigned int value = 0;

                        if (handle >= 0)
                                got = system_read_once(handle, text, sizeof text - 1), system_close(handle);
                        for (bipolar k = 0; k < got; k++)
                                if (text[k] >= '0' && text[k] <= '9')
                                        value = value * 10 + (unsigned int)(text[k] - '0');
                                else
                                {
                                        if (value && value != me)
                                                compositor_game_slice_of(value, 0);
                                        value = 0;
                                }
                        if (got > 0)
                                compositor_game_slice_note(pids, 0);
                }
        }
        for (unsigned int k = 0; k < tops && compositor.table; k++)
        {
                unsigned int pid = __atomic_load_n(&compositor.table->pid[rows[k]], __ATOMIC_ACQUIRE);

                if (pid && pid != me && !compositor_pid_in(now, nows, pid))
                        now[nows++] = pid;
        }
        for (unsigned int k = 0; k < count; k++)
                if (!compositor_pid_in(now, nows, pids[k]))
                        compositor_game_slice_of(pids[k], 0), changed = true;
        for (unsigned int j = 0; j < nows; j++)
                if (!compositor_pid_in(pids, count, now[j]))
                        compositor_game_slice_of(now[j], COMPOSITOR_GAME_SLICE_NS), changed = true;
        if (nows > 0 && compositor.table)
                for (int r = 0; r < (int)compositor_rows_in_use && bg_nows < COMPOSITOR_BG_SLICES; r++)
                {
                        unsigned int pid = __atomic_load_n(&compositor.table->pid[r], __ATOMIC_ACQUIRE);

                        if (pid && pid != me && !compositor_pid_in(now, nows, pid) && !compositor_pid_in(bg_now, bg_nows, pid))
                                bg_now[bg_nows++] = pid;
                }
        for (unsigned int k = 0; k < bg_count; k++)
                if (!compositor_pid_in(bg_now, bg_nows, bg_pids[k]))
                        compositor_bg_throttle_of(bg_pids[k], 0);
        for (unsigned int j = 0; j < bg_nows; j++)
                if (!compositor_pid_in(bg_pids, bg_count, bg_now[j]))
                        compositor_bg_throttle_of(bg_now[j], 1);
        memory_copy(bg_pids, bg_now, sizeof bg_now[0] * bg_nows), bg_count = bg_nows;
        if (!changed)
                return;
        memory_copy(pids, now, sizeof now[0] * nows), count = nows;
        if (compositor.table_giver)
                compositor_game_slice_note(pids, count);
}

// Desktop chords from /run/moonwater/keys -> FIFO /run/moonwater/keys.fire (runner starts everything).
#define COMPOSITOR_KEYS 32
#define COMPOSITOR_KEYS_FILE "/run/moonwater/keys"
#define COMPOSITOR_KEYS_FIRE "/run/moonwater/keys.fire"
static const char *compositor_keys_fire = COMPOSITOR_KEYS_FIRE;

static struct
{
        unsigned int count;
        unsigned short modifiers[COMPOSITOR_KEYS], code[COMPOSITOR_KEYS];
        char chord[COMPOSITOR_KEYS][32];
} compositor_keys;

static unsigned int compositor_key_word(const char *word, unsigned int length, unsigned int *code)
{
        static const struct { const char *name; unsigned int flag; } modifiers[] = {
            {"control", WINDOW_KEY_CONTROL}, {"ctrl", WINDOW_KEY_CONTROL}, {"shift", WINDOW_KEY_SHIFT},
            {"alt", WINDOW_KEY_ALT}, {"super", WINDOW_KEY_SUPER}};
        unsigned int number = 0;

        for (unsigned int at = 0; at < sizeof(modifiers) / sizeof(modifiers[0]); at++)
                if (string_length((string_address)modifiers[at].name) == length && !memory_compare(modifiers[at].name, word, length))
                        return modifiers[at].flag;
        if (!length || length > 3)
                return ~0u;
        for (unsigned int at = 0; at < length; at++)
        {
                if (word[at] < '0' || word[at] > '9')
                        return ~0u;
                number = number * 10 + (unsigned int)(word[at] - '0');
        }
        return number && number < 0x300 ? (*code = number, 0u) : ~0u;
}

static void compositor_keys_read(void)
{
        static char text[4096];
        bipolar handle = system_open_at(AT_FDCWD, (string_address)COMPOSITOR_KEYS_FILE, COMPOSITOR_SETTING_OPEN);
        bipolar got = handle < 0 ? 0 : system_read_once(handle, text, sizeof(text) - 1);
        unsigned int at = 0;

        compositor_keys.count = 0;
        if (handle >= 0)
                system_close(handle);
        text[got > 0 ? got : 0] = 0;
        while (text[at] && compositor_keys.count < COMPOSITOR_KEYS)
        {
                unsigned int start = at, close, modifiers = 0, code = 0, bad;

                while (text[at] && text[at] != '\n')
                        at++;
                close = at, at += text[at] != 0;
                while (start < close && (text[start] == ' ' || text[start] == '\t'))
                        start++;
                while (close > start && (text[close - 1] == ' ' || text[close - 1] == '\t' || text[close - 1] == '\r'))
                        close--;
                if (close == start)
                        continue;
                bad = close - start < 5 || memory_compare(text + start, "key_", 4);
                for (unsigned int word = start + 4; word < close && !bad;)
                {
                        unsigned int stop = word, flag;

                        while (stop < close && text[stop] != '_')
                                stop++;
                        flag = compositor_key_word(text + word, stop - word, &code);
                        bad = flag == ~0u, modifiers |= bad ? 0 : flag, word = stop + 1;
                }
                if (bad || !code || close - start >= sizeof(compositor_keys.chord[0]))
                {
                        p8 name[48];
                        positive length = close - start < sizeof(name) - 1 ? close - start : sizeof(name) - 1;

                        memory_copy(name, text + start, length), name[length] = 0;
                        compositor_kmsg((string_address) "compositor: no chord in the rules' keys line ", 0, (string_address)name);
                        continue;
                }
                memory_copy(compositor_keys.chord[compositor_keys.count], text + start, close - start);
                compositor_keys.chord[compositor_keys.count][close - start] = 0;
                compositor_keys.code[compositor_keys.count] = (unsigned short)code;
                compositor_keys.modifiers[compositor_keys.count++] = (unsigned short)modifiers;
        }
}

static void compositor_script_say(const char *name)
{
        bipolar handle;
        char line[COMPOSITOR_DOOR_LINE];
        positive length = string_length((string_address)name);
        unsigned int head = compositor_doors.line_head;

        if (length > sizeof(line) - 2)
                length = sizeof(line) - 2;
        if (compositor_doors.threads)
        {
                if (head - __atomic_load_n(&compositor_doors.line_tail, __ATOMIC_ACQUIRE) >= 16)
                        { compositor_kmsg((string_address) "compositor: a line for the rules is dropped, sixteen wait unread: ", 0, (string_address)name); return; }
                memory_copy(compositor_doors.lines[head % 16], name, length);
                compositor_doors.lines[head % 16][length] = '\n', compositor_doors.lines[head % 16][length + 1] = 0;
                __atomic_store_n(&compositor_doors.line_head, head + 1, __ATOMIC_RELEASE);
                compositor_door_ask(COMPOSITOR_DOOR_MISC, COMPOSITOR_DOOR_LINES);
                return;
        }
        if ((handle = system_open_at(AT_FDCWD, (string_address)compositor_keys_fire, O_WRONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW)) < 0)
                { compositor_kmsg((string_address) "compositor: nothing reads /run/moonwater/keys.fire, error ", (positive)-handle, (string_address) ""); return; }
        memory_copy(line, name, length), line[length] = '\n';
        system_write_once(handle, line, length + 1);
        system_close(handle);
}

// Sound server master volume & mic controls: shm state page + control socket + /root/audio.* persistence.
#define COMPOSITOR_VOLUME_STEP 5
#define COMPOSITOR_VOLUME_WAIT_MS 20
#define COMPOSITOR_VOLUME_AGAIN_NS 2000000000ull
#define COMPOSITOR_VOLUME_LOOK_NS 500000000ull
#define COMPOSITOR_VOLUME_FIND_NS 2000000000ull
#define COMPOSITOR_VOLUME_FLASH_NS 1500000000ull
#define COMPOSITOR_VOLUME_HOLD_NS 800000000ull
#define COMPOSITOR_VOLUME_STALE_NS 4000000000ull

static const char *compositor_volume_path[5] = {AUDIO_STATE_PATH, SETTING_AUDIO_VOLUME_PATH, SETTING_AUDIO_MUTE_PATH, CTL_SOCKET, SETTING_AUDIO_SOURCE_MUTE_PATH};
static struct ctl compositor_ctl;
static positive compositor_ctl_again;

static struct
{
        const volatile struct audio_state *page;
        struct audio_state copy;
        positive look_at, flash_until, hold_until, seen_at, mic_flash_until;
        unsigned int seen_seq;
        int percent, muted, present, recording, mic_muted;
} compositor_volume;

static void compositor_volume_unmap(void)
{
        if (compositor_volume.page)
                compositor_call(syscall(munmap), (positive)compositor_volume.page, 4096ul * ((sizeof(struct audio_state) + 4095) / 4096), 0, 0, 0, 0);
        compositor_volume.page = null;
}

static int compositor_volume_map(void)
{
        long fd, size, got;

        if (compositor_volume.page)
                return 1;
        if ((fd = compositor_call(syscall(openat), (positive)AT_FDCWD, (positive)compositor_volume_path[0], COMPOSITOR_SETTING_OPEN, 0, 0, 0)) < 0)
                return 0;
        size = compositor_call(syscall(lseek), (positive)fd, 0, 2, 0, 0, 0);
        got = size < (long)sizeof(struct audio_state) ? -1
              : compositor_call(syscall(mmap), 0, 4096ul * ((sizeof(struct audio_state) + 4095) / 4096), 1, 1, (positive)fd, 0);
        compositor_call(syscall(close), (positive)fd, 0, 0, 0, 0, 0);
        if (got < 0 && got > -4096)
                return 0;
        compositor_volume.page = (const volatile struct audio_state *)got;
        return 1;
}

static int compositor_row_named(const char *word)
{
        for (unsigned int row = 0; row < compositor_rows_in_use; row++)
        {
                const volatile char *name = compositor.table->name[row];
                unsigned int at = 0;

                if (!compositor.showing[row])
                        continue;
                while (word[at] && name[at] == word[at])
                        at++;
                if (!word[at] && !name[at])
                        return (int)row;
        }
        return COMPOSITOR_NONE;
}

static int compositor_quick_row(void) { return compositor_row_named("mixer-quick"); }

static void compositor_table_button_set(int x, int y, int w, int h, unsigned int edge, unsigned int screen)
{
        struct windows *table = compositor.table;

        if (!table)
                return;
        table->button_x = x, table->button_y = y;
        table->button_w = (unsigned int)w, table->button_h = (unsigned int)h;
        table->button_edge = edge, table->button_screen = screen;
}

static void compositor_button_rect(int x, int y, int which)
{
        int reach = compositor.touching ? COMPOSITOR_FINGER_TARGET * compositor.scale / 100 : 0;

        for (compositor.band_at = 0; compositor.table && compositor.band_at < compositor_bands(); compositor.band_at++)
        {
                struct taskbar_view view = compositor_band_view();
                int rect[4];

                if (taskbar_hit_reach(&view, x, y, reach) != which || !taskbar_anchor(&view, which, &rect[0], &rect[1], &rect[2], &rect[3]))
                        continue;
                compositor_table_button_set(rect[0], rect[1], rect[2], rect[3], view.position, (unsigned int)compositor.band_at);
                break;
        }
        compositor.band_at = 0;
}

static void compositor_popup_trigger(const char *named, const char *say, int x, int y, int slot)
{
        if (named && compositor_row_named(named) >= 0)
                return;
        compositor_button_rect(x, y, slot);
        compositor_script_say(say);
}

static void compositor_volume_open(int x, int y) { compositor_popup_trigger("mixer-quick", "volume_button", x, y, TASKBAR_VOLUME); }
static void compositor_mic_open(int x, int y) { compositor_popup_trigger("mixer-quick", "mic_button", x, y, TASKBAR_MIC); }
static void compositor_notices_open(int x, int y) { compositor_popup_trigger("notices", "notices_button", x, y, TASKBAR_NOTICES); }

static struct windows_issues compositor_notices_copy;
static unsigned int compositor_notices_seq;

static int compositor_notices_look(void)
{
        unsigned int seq, count = 0, worst;

        if (!compositor.table)
                return 0;
        seq = __atomic_load_n(&compositor.table->issue_seq, __ATOMIC_ACQUIRE);
        if (seq == compositor_notices_seq || (seq & 1u) || !windows_issues_read(compositor.table, &compositor_notices_copy))
                return 0;
        compositor_notices_seq = seq;
        worst = windows_issues_summary(&compositor_notices_copy, &count);
        return taskbar_notices_set(count > 0, worst);
}

static void compositor_volume_show(void)
{
        int changed = taskbar_volume_set(compositor_volume.present, compositor_volume.percent, compositor_volume.muted, compositor_volume.flash_until != 0);

        changed |= taskbar_mic_set(compositor_volume.present && (compositor_volume.recording || compositor_volume.mic_flash_until), compositor_volume.mic_muted);
        if (changed)
                compositor_tabs_changed();
}

static void compositor_volume_look(positive now)
{
        struct audio_state *copy = &compositor_volume.copy;

        if (compositor_volume.flash_until && now >= compositor_volume.flash_until)
                compositor_volume.flash_until = 0;
        if (compositor_volume.mic_flash_until && now >= compositor_volume.mic_flash_until)
                compositor_volume.mic_flash_until = 0;
        if (now >= compositor_volume.hold_until)
        {
                if (!compositor_volume_map() || !audio_state_read(compositor_volume.page, copy))
                        compositor_volume.present = 0, compositor_volume.recording = 0;
                else
                {
                        unsigned long percent = ((unsigned long)copy->volume * 100 + 32768) / 65536;

                        compositor_volume.present = 1, compositor_volume.percent = percent > 100 ? 100 : (int)percent;
                        compositor_volume.muted = copy->muted != 0, compositor_volume.recording = 0, compositor_volume.mic_muted = copy->input_muted != 0;
                        for (unsigned int k = 0; k < AUDIO_STATE_RECORDS; k++)
                                compositor_volume.recording += copy->capture && copy->record[k].index != 0xffffffffu && !(copy->record[k].flags & (AUDIO_STATE_MONITOR | AUDIO_STATE_CORKED)) &&
                                                               (copy->record[k].flags & AUDIO_STATE_PLAYING);
                        if (copy->seq != compositor_volume.seen_seq)
                                compositor_volume.seen_seq = copy->seq, compositor_volume.seen_at = now;
                        if (now - compositor_volume.seen_at > COMPOSITOR_VOLUME_STALE_NS)
                                compositor_volume_unmap(), compositor_volume.seen_at = now;
                }
        }
        compositor_volume.look_at = now + (compositor_volume.present ? COMPOSITOR_VOLUME_LOOK_NS : COMPOSITOR_VOLUME_FIND_NS);
        if (compositor_volume.flash_until && compositor_volume.flash_until < compositor_volume.look_at)
                compositor_volume.look_at = compositor_volume.flash_until;
        if (compositor_volume.mic_flash_until && compositor_volume.mic_flash_until < compositor_volume.look_at)
                compositor_volume.look_at = compositor_volume.mic_flash_until;
        compositor_volume_show();
}

static void compositor_volume_write(const char *path, const char *text)
{
        (void)file_write_atomic_at(AT_FDCWD, (string_address)path, text, string_length((string_address)text), 0644);
}

static int compositor_ctl_ready(positive now)
{
        compositor_ctl.timeout_ms = COMPOSITOR_VOLUME_WAIT_MS;
        if (compositor_ctl.open)
                return 1;
        if (now < compositor_ctl_again)
                return 0;
        if (ctl_connect(&compositor_ctl, compositor_volume_path[3]) < 0)
                return compositor_ctl_again = now + COMPOSITOR_VOLUME_AGAIN_NS, 0;
        return 1;
}

static void compositor_ctl_answered(long answer, positive now)
{
        if (answer == CTL_FAILED)
        {
                ctl_disconnect(&compositor_ctl);
                compositor_ctl_again = now + COMPOSITOR_VOLUME_AGAIN_NS;
        }
}

static void compositor_volume_tell(int volume, int mute)
{
        positive now = clock_monotonic_nanoseconds();
        const char *sink = compositor_volume.copy.name;
        long answer = 0;

        if (!compositor_volume.present || !sink[0])
                return;
        if (!compositor_ctl_ready(now))
                return;
        if (volume >= 0)
                answer = ctl_set_sink_volume(&compositor_ctl, sink, (unsigned long)volume * 65536 / 100);
        if (answer >= 0 && mute >= 0)
                answer = ctl_set_sink_mute(&compositor_ctl, sink, mute);
        compositor_ctl_answered(answer, now);
}

static void compositor_volume_step(int step, int toggle)
{
        positive now = clock_monotonic_nanoseconds();
        char text[8];
        int percent, muted;

        compositor_volume_look(now);
        if (!compositor_volume.present)
                return;
        percent = compositor_volume.percent, muted = compositor_volume.muted;
        if (toggle)
                muted = !muted;
        else
        {
                percent += step;
                percent = percent < 0 ? 0 : percent > 100 ? 100 : percent;
                muted = 0;
        }
        compositor_volume_tell(percent != compositor_volume.percent ? percent : -1, muted != compositor_volume.muted ? muted : -1);
        if (percent != compositor_volume.percent)
        {
                int at = 0;

                at = (int)positive_into_string((string_address)text, (positive)percent);
                text[at++] = '\n', text[at] = 0;
                compositor_volume_write(compositor_volume_path[1], text);
        }
        if (muted != compositor_volume.muted)
                compositor_volume_write(compositor_volume_path[2], muted ? "on\n" : "off\n");
        compositor_volume.percent = percent, compositor_volume.muted = muted;
        compositor_volume.look_at = compositor_volume.flash_until = now + COMPOSITOR_VOLUME_FLASH_NS;
        compositor_volume.hold_until = now + COMPOSITOR_VOLUME_HOLD_NS;
        compositor_volume_show();
}

static void compositor_mic_step(void)
{
        positive now = clock_monotonic_nanoseconds();
        const char *source = compositor_volume.copy.input_name;
        int muted;

        compositor_volume_look(now);
        if (!compositor_volume.present || !source[0])
                return;
        muted = !compositor_volume.mic_muted;
        if (compositor_ctl_ready(now))
                compositor_ctl_answered(ctl_set_source_mute(&compositor_ctl, source, muted), now);
        compositor_volume_write(compositor_volume_path[4], muted ? "on\n" : "off\n");
        compositor_volume.mic_muted = muted;
        compositor_volume.look_at = compositor_volume.mic_flash_until = now + COMPOSITOR_VOLUME_FLASH_NS;
        compositor_volume.hold_until = now + COMPOSITOR_VOLUME_HOLD_NS;
        compositor_volume_show();
}

static void compositor_volume_key(unsigned int code, int value)
{
        if (code == CK_KEY_MICMUTE)
        {
                if (value == 1)
                        compositor_mic_step();
        }
        else if (code == CK_KEY_MUTE)
        {
                if (value == 1)
                        compositor_volume_step(0, 1);
        }
        else
                compositor_volume_step(code == CK_KEY_VOLUME_UP ? COMPOSITOR_VOLUME_STEP : -COMPOSITOR_VOLUME_STEP, 0);
}

static void compositor_settings_open(void)
{
        int row = compositor_row_named("settings");

        if (row >= 0) { compositor_reveal(row); return; }
        compositor.settings_asked++;
        compositor_script_say("tray_double");
}

static void compositor_menu_close(void);

static void compositor_launcher_open(int x, int y)
{
        compositor_menu_close();
        compositor_popups_close(-1, -1);
        compositor.launcher_asked++;
        compositor_popup_trigger(null, "launcher_button", x, y, TASKBAR_LAUNCHER);
}

static int compositor_logo_tap(unsigned char *alone, unsigned int bit, int value, int others)
{
        if (!(bit & (CK_HELD_LSUPER | CK_HELD_RSUPER)))
        {
                if (value == 1)
                        *alone = 0;
                return 0;
        }
        if (value == 1)
                *alone = !others;
        else if (value == 0 && *alone)
        {
                *alone = 0;
                return !others;
        }
        return 0;
}

static void compositor_logo_act(void)
{
        int up = compositor_launcher_row();

        compositor_menu_close(), compositor_popups_close(-1, -1);
        if (up >= 0) { compositor_close(up), compositor_tabs_changed(); return; }
        for (compositor.band_at = 0; compositor.table && compositor.band_at < compositor_bands(); compositor.band_at++)
        {
                struct taskbar_view view = compositor_band_view();
                int x, y, w, h;

                if (!taskbar_launcher_on() || !taskbar_slot(&view, TASKBAR_LAUNCHER, &x, &y, &w, &h) || w <= 0 || h <= 0)
                        continue;
                compositor.band_at = 0;
                compositor_launcher_open(x + w / 2, y + h / 2);
                return;
        }
        compositor.band_at = 0;
}

static unsigned int compositor_modifiers(void)
{
        unsigned int held = 0;

        for (unsigned int at = 0; at < compositor.input_count; at++)
                held |= compositor.held[at];
        return compositor_held_flags(held);
}

// Built-in tiling & multi-screen chords (Super+Arrow, Super+Shift+Left/Right).
static void compositor_tile_chord(unsigned int code)
{
        static const unsigned char next[10][4] = {
            [COMPOSITOR_FLOATING]     = {COMPOSITOR_LEFT, COMPOSITOR_RIGHT, COMPOSITOR_MAXIMIZED, COMPOSITOR_FLOATING},
            [COMPOSITOR_MAXIMIZED]    = {COMPOSITOR_LEFT, COMPOSITOR_RIGHT, COMPOSITOR_MAXIMIZED, COMPOSITOR_FLOATING},
            [COMPOSITOR_LEFT]         = {COMPOSITOR_LEFT, COMPOSITOR_FLOATING, COMPOSITOR_TOP_LEFT, COMPOSITOR_BOTTOM_LEFT},
            [COMPOSITOR_RIGHT]        = {COMPOSITOR_FLOATING, COMPOSITOR_RIGHT, COMPOSITOR_TOP_RIGHT, COMPOSITOR_BOTTOM_RIGHT},
            [COMPOSITOR_ROOM]         = {COMPOSITOR_LEFT, COMPOSITOR_RIGHT, COMPOSITOR_ROOM, COMPOSITOR_FLOATING},
            [COMPOSITOR_BOTTOM]       = {COMPOSITOR_BOTTOM_LEFT, COMPOSITOR_BOTTOM_RIGHT, COMPOSITOR_MAXIMIZED, COMPOSITOR_BOTTOM},
            [COMPOSITOR_TOP_LEFT]     = {COMPOSITOR_TOP_LEFT, COMPOSITOR_TOP_RIGHT, COMPOSITOR_MAXIMIZED, COMPOSITOR_LEFT},
            [COMPOSITOR_TOP_RIGHT]    = {COMPOSITOR_TOP_LEFT, COMPOSITOR_TOP_RIGHT, COMPOSITOR_MAXIMIZED, COMPOSITOR_RIGHT},
            [COMPOSITOR_BOTTOM_LEFT]  = {COMPOSITOR_BOTTOM_LEFT, COMPOSITOR_BOTTOM_RIGHT, COMPOSITOR_LEFT, COMPOSITOR_BOTTOM_LEFT},
            [COMPOSITOR_BOTTOM_RIGHT] = {COMPOSITOR_BOTTOM_LEFT, COMPOSITOR_BOTTOM_RIGHT, COMPOSITOR_RIGHT, COMPOSITOR_BOTTOM_RIGHT},
        };
        int row = compositor.focus;
        unsigned int dir = code == CK_KEY_LEFT ? 0 : code == CK_KEY_RIGHT ? 1 : code == CK_KEY_UP ? 2 : code == CK_KEY_DOWN ? 3 : 4;
        unsigned int cur, how;

        if (row < 0 || !compositor_visible(row) || compositor.cycle)
                return;
        cur = compositor.arranged[row];
        how = cur < sizeof(next) / sizeof(next[0]) && dir < 4 ? next[cur][dir] : cur;
        if (how != cur)
                how == COMPOSITOR_FLOATING ? compositor_float(row) : compositor_arrange(row, how);
}

static void compositor_screen_chord(unsigned int code)
{
        int row = compositor.focus, from, to = -1, right = code == CK_KEY_RIGHT, keep[4];
        long long best = -1, level = 0;
        unsigned int count, how;
        const int (*screens)[4] = compositor_screens(&count);

        if (row < 0 || !compositor_visible(row) || compositor.cycle || count < 2)
                return;
        from = compositor_screen_of(row);
        for (unsigned int at = 0; at < count; at++)
        {
                const int *s = screens[at], *f = screens[from];
                long long gap = right ? (long long)s[0] - (f[0] + f[2]) : (long long)f[0] - (s[0] + s[2]);
                long long off = compositor_abs64(((long long)s[1] * 2 + s[3]) - ((long long)f[1] * 2 + f[3]));

                if ((int)at != from && gap >= 0 && (best < 0 || gap < best || (gap == best && off < level)))
                        best = gap, level = off, to = (int)at;
        }
        if (to < 0)
                return;
        how = compositor.arranged[row];
        compositor_row_saved_get(row, keep);
        compositor_screen_carry(screens[from], screens[to], &keep[0], &keep[1], keep[2], keep[3]);
        compositor_row_saved_set(row, keep);
        if (how == COMPOSITOR_FLOATING)
        {
                int rect[4] = {compositor.x[row], compositor.y[row], (int)compositor.w[row], (int)compositor.h[row]};

                compositor_screen_carry(screens[from], screens[to], &rect[0], &rect[1], rect[2], rect[3]);
                compositor_set_rect(row, COMPOSITOR_FLOATING, rect);
        }
        else
                compositor_arrange_on(row, how == COMPOSITOR_ROOM ? COMPOSITOR_MAXIMIZED : how, to);
}

#ifdef CONFIG_MOONWATER_WAYLAND
static int compositor_wl_key(unsigned int code, int value);
static int compositor_wl_locked(void);
static void compositor_wl_counts(int dx, int dy, int udx, int udy, positive now);
static void compositor_wl_hold_keep(void);
static void compositor_wl_input(void);
static int compositor_wl_suspends(unsigned int code, int value, int super);
#else
static int compositor_wl_locked(void) { return 0; }
#endif

static int compositor_fullscreen_key(int super, int alone)
{
        int row = compositor.focus;

        if (row < 0 || row >= COMPOSITOR_ROWS || !compositor_visible(row) || (!super && !alone))
                return 0;
#ifdef CONFIG_MOONWATER_WAYLAND
        if (!super && wayland_row_owner(row) >= 0)
                return 0;
#endif
        return 1;
}

// Protocol-neutral input override registry (e.g. Steam/Proton overlay).
#define COMPOSITOR_INPUT_OVERRIDES_MAX 4
static struct { compositor_input_override_fn fn; void *ctx; } compositor_input_override_list[COMPOSITOR_INPUT_OVERRIDES_MAX];

static int compositor_input_override_register(compositor_input_override_fn fn, void *ctx)
{
        if (!fn || compositor_input_overrides >= COMPOSITOR_INPUT_OVERRIDES_MAX)
                return -1;
        for (unsigned int i = 0; i < compositor_input_overrides; i++)
                if (compositor_input_override_list[i].fn == fn && compositor_input_override_list[i].ctx == ctx)
                        return 0;
        compositor_input_override_list[compositor_input_overrides].fn = fn;
        compositor_input_override_list[compositor_input_overrides++].ctx = ctx;
        return 0;
}

static void compositor_input_override_unregister(compositor_input_override_fn fn, void *ctx)
{
        for (unsigned int i = 0; i < compositor_input_overrides; i++)
                if (compositor_input_override_list[i].fn == fn && compositor_input_override_list[i].ctx == ctx)
                        { compositor_input_override_list[i] = compositor_input_override_list[--compositor_input_overrides]; return; }
}

static int compositor_input_override_run(int row, struct compositor_input_override_event *ev, int max_mode)
{
        int best = 0;

        if (!compositor_input_overrides || !compositor.table || row < 0 || row >= COMPOSITOR_ROWS)
                return 0;
        ev->row = row, ev->pid = __atomic_load_n(&compositor.table->pid[row], __ATOMIC_ACQUIRE);
        for (unsigned int i = 0; i < compositor_input_overrides; i++)
        {
                int r = compositor_input_override_list[i].fn(compositor_input_override_list[i].ctx, ev);

                if (max_mode)
                        best = r > best ? r : best;
                else if (r)
                        return 1;
        }
        return best;
}

static int compositor_input_override_query(int row)
{
        struct compositor_input_override_event ev = {.kind = COMPOSITOR_OVERRIDE_QUERY};
        return compositor_input_override_run(row, &ev, 1);
}

static int compositor_input_override_key(int row, unsigned int code, int value, unsigned int modifiers)
{
        struct compositor_input_override_event ev = {.kind = COMPOSITOR_OVERRIDE_KEY, .flags = modifiers, .a = (int)code, .b = value};
        return compositor_input_override_run(row, &ev, 0);
}

static int compositor_input_override_pointer(int row, unsigned int type, unsigned int flags, int a, int b)
{
        struct compositor_input_override_event ev = {.kind = COMPOSITOR_OVERRIDE_POINTER, .ev_type = type, .flags = flags, .a = a, .b = b};

        if (row >= 0 && row < COMPOSITOR_ROWS)
                ev.width = (int)compositor.w[row], ev.height = (int)compositor.h[row];
        return compositor_input_override_run(row, &ev, 0);
}

static int compositor_steam_passes(unsigned int code, int value, unsigned int modifiers, unsigned int chord)
{
        static unsigned char passed[32];
        unsigned char mask = (unsigned char)(1u << (code & 7)), *bit = &passed[(code >> 3) & 31];
        int pass;

        if (code >= 256)
                return 0;
        if (value != 1)
                return pass = (*bit & mask) != 0, (!value ? (void)(*bit &= (unsigned char)~mask) : (void)0), pass;
        pass = (code == CK_KEY_ESC && (modifiers & WINDOW_KEY_SUPER)) || (chord == WINDOW_KEY_ALT && code == CK_KEY_TAB);
        for (unsigned int at = 0; !pass && at < compositor_keys.count; at++)
                pass = code == compositor_keys.code[at] &&
                       (modifiers & (WINDOW_KEY_SHIFT | WINDOW_KEY_CONTROL | WINDOW_KEY_ALT | WINDOW_KEY_SUPER)) == compositor_keys.modifiers[at];
        *bit = pass ? (unsigned char)(*bit | mask) : (unsigned char)(*bit & ~mask);
        return pass;
}

static int compositor_key_chord(unsigned int code, int value, unsigned int modifiers, unsigned int chord, unsigned int mods4)
{
        for (unsigned int k = 0; k < compositor_keys.count; k++)
                if (code == compositor_keys.code[k] && mods4 == compositor_keys.modifiers[k])
                {
                        if (value == 1)
                                compositor_menu_close(), compositor_popups_close(-1, -1), compositor_script_say(compositor_keys.chord[k]);
                        return 1;
                }
        if (mods4 == WINDOW_KEY_SUPER && (code == CK_KEY_LEFT || code == CK_KEY_RIGHT || code == CK_KEY_UP || code == CK_KEY_DOWN))
                return value == 1 ? compositor_tile_chord(code) : (void)0, 1;
        if (mods4 == (WINDOW_KEY_SUPER | WINDOW_KEY_SHIFT) && (code == CK_KEY_LEFT || code == CK_KEY_RIGHT))
                return value == 1 ? compositor_screen_chord(code) : (void)0, 1;
        if (code == CK_KEY_F11 && !compositor.cycle && compositor_fullscreen_key(mods4 == WINDOW_KEY_SUPER, mods4 == 0))
                return value == 1 ? compositor_fullscreen_toggle(compositor.focus) : (void)0, 1;
        return ((chord & ~WINDOW_KEY_SHIFT) == (WINDOW_KEY_ALT | WINDOW_KEY_CONTROL) && code == CK_KEY_BACKSPACE) ||
               ((modifiers & WINDOW_KEY_ALT) && compositor.cycle);
}

static void compositor_key(unsigned int at, unsigned int code, int value, positive reported)
{
        unsigned int bit = compositor_held_bit(code), characters[2] = {0, 0}, flags, modifiers, chord, mods4;
        int count, target;

        taskbar_moon_seen(clock_monotonic_nanoseconds());
        if (value == 1)
                compositor_keytrace_read(code, reported, clock_monotonic_nanoseconds());
        if (code == CK_KEY_ESC && value)
        {
                if (compositor.drag >= 0 || compositor.resize >= 0) { if (value == 1) compositor_hold_cancel(); return; }
                if (compositor_dnd_escape(value))
                        return;
        }
        if (compositor.assist_armed && value == 1 && !bit)
                if (compositor_assist_close(), code == CK_KEY_ESC)
                        return;
        if (bit)
                compositor.held[at] = value ? (unsigned char)(compositor.held[at] | bit) : (unsigned char)(compositor.held[at] & ~bit);
        if (compositor_logo_tap(&compositor.logo_alone, bit, value,
                                (compositor_modifiers() & (WINDOW_KEY_SHIFT | WINDOW_KEY_CONTROL | WINDOW_KEY_ALT | WINDOW_KEY_ALTGR)) != 0))
                compositor_logo_act();
        if (bit)
        {
                if (compositor.drag >= 0)
                        compositor_preview(compositor.drag);
                compositor_dnd_keys();
        }
        modifiers = compositor_modifiers();
        chord = modifiers & (WINDOW_KEY_SHIFT | WINDOW_KEY_CONTROL | WINDOW_KEY_ALT);
        mods4 = chord | (modifiers & WINDOW_KEY_SUPER);
        if (!compositor_steam_passes(code, (int)value, modifiers, chord) &&
            compositor_input_overrides && compositor_input_override_key(compositor.focus, code, (int)value, modifiers))
        {
                if ((bit & (CK_HELD_ALT | CK_HELD_ALTGR)) && !(modifiers & WINDOW_KEY_ALT) && compositor.cycle)
                        compositor_focus_commit();
                return;
        }
        if (bit & (CK_HELD_ALT | CK_HELD_ALTGR))
        {
                if (!(modifiers & WINDOW_KEY_ALT) && compositor.cycle)
                        compositor_focus_commit();
#ifdef CONFIG_MOONWATER_WAYLAND
                compositor_wl_key(code, value);
#endif
                return;
        }
#ifdef CONFIG_MOONWATER_WAYLAND
        if (!bit)
        {
                int suspended = compositor_wl_suspends(code, value, (modifiers & WINDOW_KEY_SUPER) != 0);

                if (suspended == 2)
                        return;
                if (suspended == 1 && code != CK_KEY_MUTE && code != CK_KEY_VOLUME_DOWN && code != CK_KEY_VOLUME_UP && code != CK_KEY_MICMUTE)
                        { compositor_wl_key(code, value); return; }
        }
#endif
        if (chord == WINDOW_KEY_ALT && code == CK_KEY_TAB) { if (value) compositor_focus_step(); return; }
        if (chord == WINDOW_KEY_ALT && !compositor.cycle && (code == CK_KEY_F4 || code == CK_KEY_F9))
                { if (value == 1) code == CK_KEY_F4 ? compositor_close(compositor.focus) : compositor_minimize_row(compositor.focus); return; }
        if (!(modifiers & (WINDOW_KEY_CONTROL | WINDOW_KEY_ALT | WINDOW_KEY_SUPER)) &&
            (code == CK_KEY_MUTE || code == CK_KEY_VOLUME_DOWN || code == CK_KEY_VOLUME_UP || code == CK_KEY_MICMUTE))
        {
                if (value)
                        compositor_volume_key(code, value);
                return;
        }
        if (compositor_key_chord(code, value, modifiers, chord, mods4))
                return;
#ifdef CONFIG_MOONWATER_WAYLAND
        if (compositor_wl_key(code, value))
                return;
#endif
        if ((count = compositor_keymap_event(&compositor.keymap, code, value, modifiers, characters)) < 0)
                return;
        if (compositor.focus < 0)
                compositor_refocus();
        if (compositor.focus < 0)
                return;
        target = compositor_popup_top(compositor.focus);
        if (code < 256)
        {
                unsigned char mask = (unsigned char)(1u << (code & 7)), *down = &compositor.keys_down[code >> 3];

                if (value == 1)
                {
                        if (compositor.keys_to != target)
                                compositor_keys_release();
                        *down |= mask, compositor.keys_to = target;
                }
                else if (!(*down & mask))
                        return;
                else
                        target = compositor.keys_to, (!value ? (void)(*down &= (unsigned char)~mask) : (void)0);
        }
        flags = modifiers | (value ? WINDOW_KEY_DOWN : 0) | (value == 2 ? WINDOW_KEY_REPEAT : 0);
        for (int each = 0; each < (count ? count : 1); each++)
                compositor_post(target, WINDOW_EVENT_KEY, flags, (int)code, (int)characters[each]);
        if (value == 1)
                compositor_latency(2, reported, clock_monotonic_nanoseconds());
}

static void compositor_pointer_sync(void)
{
        struct compositor_predict *track = &compositor.predict[compositor.owner];

        if (compositor.pointer_x != compositor.shown_x || compositor.pointer_y != compositor.shown_y)
                compositor.real_x = compositor.pointer_x, compositor.real_y = compositor.pointer_y,
                compositor.accel_x = compositor.accel_y = 0, track->lead_x = track->lead_y = 0;
}

static void compositor_pointer_shown(void)
{
        struct compositor_predict *track = &compositor.predict[compositor.owner];
        unsigned int count;
        const int (*screens)[4] = compositor_screens(&count);

        compositor.pointer_x = (int)(compositor.real_x + (compositor.at_real ? 0 : compositor_predict_px(track->lead_x)));
        compositor.pointer_y = (int)(compositor.real_y + (compositor.at_real ? 0 : compositor_predict_px(track->lead_y)));
        compositor_screen_keep_point(screens, count, compositor.real_x, compositor.real_y, &compositor.pointer_x, &compositor.pointer_y);
        compositor.shown_x = compositor.pointer_x, compositor.shown_y = compositor.pointer_y;
}

static void compositor_pointer_clamp(void)
{
        unsigned int count;
        const int (*screens)[4] = compositor_screens(&count);
        int nowhere = -2147483647 - 1;

        compositor_pointer_sync();
        compositor_screen_keep_point(screens, count, compositor.absolute ? nowhere : compositor.kept_x,
                                     compositor.absolute ? nowhere : compositor.kept_y, &compositor.real_x, &compositor.real_y);
        compositor.kept_x = compositor.real_x, compositor.kept_y = compositor.real_y, compositor.absolute = 0;
        compositor_pointer_shown();
}

static int compositor_shaken(int delta, unsigned long long now) { return compositor_shake(&compositor.shake, delta, now) && !compositor.cursor.shake_off; }

static void compositor_magnify(positive now)
{
        compositor.cursor.magnify = 0, compositor.cursor.magnified = 1;
        compositor.cursor.magnified_until = now + (compositor.cursor.shake_ns ? compositor.cursor.shake_ns : COMPOSITOR_MAGNIFIED_NS);
}

static void compositor_pointer_to(int row)
{
        int x, y;

        compositor_window_point(row, compositor.pointer_x, compositor.pointer_y, &x, &y);
        compositor.told_row = compositor_post(row, WINDOW_EVENT_MOTION, 0, x, y) ? row : COMPOSITOR_NONE;
        compositor.told_x = x, compositor.told_y = y;
}

#ifdef CONFIG_MOONWATER_WAYLAND
static int compositor_picture_poll(int row);
#endif
#define CP_CLIENT_SHAPES 1

static int compositor_asked_shape(int row)
{
        int asked = compositor_wl_asked_shape(row);
        unsigned int shape;

        if (asked == -2 || (asked >= 0 && CP_CLIENT_SHAPES))
                return asked;
        if (!compositor.table || row < 0 || row >= COMPOSITOR_ROWS)
                return -1;
        shape = __atomic_load_n(&compositor.table->pointer_shape[row], __ATOMIC_ACQUIRE);
        return shape < CP_SHAPES ? (int)shape : CP_ARROW;
}

static int compositor_slot_band(int x, int y, int tabs, int *slot)
{
        int found = -1, reach = tabs && compositor.touching ? COMPOSITOR_FINGER_TARGET * compositor.scale / 100 : 0;

        for (compositor.band_at = 0; compositor.table && compositor.band_at < compositor_bands() && found < 0; compositor.band_at++)
        {
                struct taskbar_view view = compositor_band_view();
                int hit = taskbar_hit_reach(&view, x, y, reach);

                if (hit == TASKBAR_LAUNCHER || (tabs && hit >= 0))
                        found = compositor.band_at, *slot = hit;
        }
        compositor.band_at = 0;
        return found;
}

static int compositor_moon_band(int x, int y) { int slot; return compositor_slot_band(x, y, 0, &slot); }

static const char *compositor_settings_root = "";

static int compositor_row_write(const char *path, const char *word)
{
        const struct settings_row *row = setting_by_path((string_address)path);
        p8 kept[32];
        long value;
        char where[160], line[40];
        bipolar handle;
        positive length;

        if (!row || !setting_parse(row, (string_address)word, kept, sizeof(kept) - 1, &value))
                return 0;
        string_copy_bounded((p8 *)where, (string_address)compositor_settings_root, sizeof(where));
        string_append_bounded((p8 *)where, (string_address)path, sizeof(where));
        string_copy_bounded((p8 *)line, kept, sizeof(line) - 1);
        length = string_length((string_address)line), line[length++] = '\n';
        if ((handle = system_open_at_mode(AT_FDCWD, (string_address)where, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644)) < 0)
                return 0;
        system_write_all((b32)handle, line, length);
        system_close(handle);
        return 1;
}

static struct { int which, band, held; } compositor_edit_drag;

static int compositor_edit_done(void)
{
        const char *paths[TASKBAR_EDIT_ROWS], *words[TASKBAR_EDIT_ROWS];
        int count = taskbar_edit_words(paths, words), kept = 0;

        for (int at = 0; at < count; at++)
                kept += compositor_row_write(paths[at], words[at]);
        compositor_edit_drag.held = 0, taskbar_edit_set(0), compositor_tabs_changed();
        return kept == count;
}

static void compositor_menu_close(void)
{
        int rect[4];

        if (!taskbar_menu_rect(rect))
                return;
        taskbar_menu_shut(), compositor_damage(rect[0], rect[1], rect[2], rect[3]);
}

static void compositor_menu_open(int x, int y)
{
        int slot = TASKBAR_HIT_NONE, band = compositor_slot_band(x, y, 1, &slot), rect[4];
        struct taskbar_view view;

        compositor_menu_close();
        if (band < 0)
                return;
        compositor.band_at = band, view = compositor_band_view(), compositor_menu.band = band, compositor_menu.drawn = 0;
        if (slot == TASKBAR_LAUNCHER ? taskbar_menu_open(&view) : taskbar_tab_menu_open(&view, slot))
                taskbar_menu_rect(rect), compositor_damage(rect[0], rect[1], rect[2], rect[3]);
        compositor.band_at = 0;
}

static void compositor_menu_chose(int id)
{
        compositor_menu_close();
        if (id == TASKBAR_MENU_EDIT)
                taskbar_edit_set(1), compositor_tabs_changed();
        else if (id == TASKBAR_MENU_SETTINGS)
                compositor_settings_open();
        else if (id == TASKBAR_MENU_CLOCK && compositor_row_write(SETTING_CLOCK_FORMAT_PATH, taskbar_clock_other()))
                taskbar_clock_turn(), compositor_tabs_changed();
        else if (id >= TASKBAR_MENU_EXIT)
                taskbar_tab_chose(id);
}

static int compositor_edit_press(const struct taskbar_view *view, int band, int x, int y, int physical)
{
        int which = taskbar_edit_item(view, x, y);

        if (which == TASKBAR_HIT_NONE)
                return 0;
        if (!physical)
                return 1;
        if (which == TASKBAR_DONE)
                compositor_edit_done();
        else
                compositor_edit_drag.which = which, compositor_edit_drag.band = band, compositor_edit_drag.held = 1;
        return 1;
}

static int compositor_edit_release(const struct taskbar_view *view, int x, int y, int physical)
{
        if (!compositor_edit_drag.held)
                return 0;
        compositor_edit_drag.held = 0;
        if (physical && taskbar_edit_drop(view, compositor_edit_drag.which, x, y))
                compositor_tabs_changed();
        return 1;
}

static int compositor_band_hand(unsigned int code, int down)
{
        int x = compositor.pointer_x, y = compositor.pointer_y, rect[4], taken = 0;

        if (taskbar_menu_rect(rect))
        {
                if (x >= rect[0] && x < rect[0] + rect[2] && y >= rect[1] && y < rect[1] + rect[3])
                {
                        int answer = taskbar_menu_hand(WINDOW_EVENT_BUTTON, down ? WINDOW_KEY_DOWN : 0, (int)code, 0, x, y);

                        compositor_damage(rect[0], rect[1], rect[2], rect[3]);
                        if (answer < 0)
                                compositor_menu_close();
                        else if (answer > 0)
                                compositor_menu_chose(answer);
                        return 1;
                }
                if (down)
                        return compositor_menu_close(), 1;
        }
        if (!down && code == COMPOSITOR_BTN_LEFT && !taskbar_editing())
                compositor_edit_drag.held = 0;
        if (!taskbar_editing() || code != COMPOSITOR_BTN_LEFT)
                return 0;
        if (!down)
        {
                if (!compositor_edit_drag.held)
                        return 0;
                compositor.band_at = compositor_edit_drag.band;
                struct taskbar_view view = compositor_band_view();
                taken = compositor_edit_release(&view, x, y, compositor_post_physical);
                compositor.band_at = 0;
                return taken;
        }
        for (int band = 0; band < compositor_bands() && !taken; band++)
        {
                compositor.band_at = band;
                struct taskbar_view view = compositor_band_view();
                taken = compositor_edit_press(&view, band, x, y, compositor_post_physical);
                compositor.band_at = 0;
        }
        return taken;
}

static void compositor_motion(void)
{
        int under, tab, rect[4], held, of, asked, dnd_shape = -1;
        unsigned int zone;

        compositor_pointer_clamp();
        under = compositor_under(compositor.pointer_x, compositor.pointer_y, &tab);
        taskbar_moon_seen(clock_monotonic_nanoseconds());
        taskbar_moon_hover(tab == TASKBAR_LAUNCHER ? compositor_moon_band(compositor.pointer_x, compositor.pointer_y) : -1);
        if (taskbar_menu_rect(rect))
                taskbar_menu_hand(WINDOW_EVENT_MOTION, 0, 0, 0, compositor.pointer_x, compositor.pointer_y),
                compositor_damage(rect[0], rect[1], rect[2], rect[3]);
        held = compositor.resize < 0 && compositor.drag < 0 ? compositor.grab : COMPOSITOR_NONE;
        zone = compositor.resize >= 0 || compositor.drag >= 0 || under < 0 || held >= 0 ? 0
               : compositor_edge_zone(under, compositor.pointer_x, compositor.pointer_y);
        of = held >= 0 ? held : compositor.resize < 0 && compositor.drag < 0 && under >= 0 && !zone ? under : COMPOSITOR_NONE;
        asked = of >= 0 ? compositor_asked_shape(of) : -1;
        compositor.cursor.shape = compositor.resize >= 0 ? compositor_cursor_by_edges[compositor.edges[compositor.resize] & 15]
                                  : compositor.drag >= 0 ? CP_ARROW
                                  : zone ? compositor_cursor_by_edges[zone & 15]
                                  : asked >= 0 ? (unsigned int)asked : CP_ARROW;
        if (compositor_dnd_active())
                compositor_dnd_motion(under), dnd_shape = compositor_dnd_shape();
        if (dnd_shape >= 0)
                compositor.cursor.shape = (unsigned int)dnd_shape, asked = -1;
        compositor.cursor.client_hidden = asked == -2, compositor.cursor.asked_row = of, compositor.cursor.asked_seen = asked;
#ifdef CONFIG_MOONWATER_WAYLAND
        compositor_picture_poll(asked >= 0 ? of : COMPOSITOR_NONE);
#endif
        compositor_cursor_moved(clock_monotonic_nanoseconds());
        if (compositor.cursor.magnify)
                compositor_magnify(clock_monotonic_nanoseconds());
        compositor_cursor_move();
        if (compositor.motion_ts && !compositor_cursor_in_scene())
                compositor_latency(0, compositor.motion_ts, clock_monotonic_nanoseconds()), compositor.motion_ts = 0;
        compositor_lit(compositor.resize >= 0 ? compositor.resize : compositor.drag >= 0 ? compositor.drag : under,
                       compositor.resize >= 0 ? compositor.edges[compositor.resize]
                       : compositor.drag >= 0 ? COMPOSITOR_LIT_ALL
                       : compositor.touching ? 0 : zone,
                       compositor.resize >= 0 || compositor.drag >= 0);
        if (compositor.resize >= 0) { compositor_resize_move(); return; }
        if (compositor.drag >= 0)
        {
                int row = compositor.drag;

                if (compositor.arranged[row] != COMPOSITOR_FLOATING)
                {
                        if (!ui_touch_past(compositor.press_x, compositor.press_y, compositor.pointer_x, compositor.pointer_y,
                                           ui_touch_slop(compositor_row_scale(row))))
                                return;
                        compositor_float_for_drag(row);
                }
                compositor.drag_moved = 1;
                compositor_damage_row(row);
                compositor_place(row, compositor.pointer_x - compositor.drag_x, compositor.pointer_y - compositor.drag_y);
                compositor_damage_row(row), compositor_preview(row);
                return;
        }
        compositor_shared_hover(under), compositor.hover = under;
        if (compositor.grab >= 0)
                compositor_pointer_to(compositor.grab);
        else if (compositor.touch.gesture.mode == UI_TOUCH_HELD && compositor.touch.hold_row >= 0)
                compositor_pointer_to(compositor.touch.hold_row);
        else if (under >= 0 && !__atomic_load_n(&compositor.table->hover_off[under], __ATOMIC_ACQUIRE))
                compositor_pointer_to(under);
}

static void compositor_shape_sync(void)
{
        int of = compositor.cursor.asked_row;

        if (of >= 0 && of < COMPOSITOR_ROWS && compositor_asked_shape(of) != compositor.cursor.asked_seen)
                compositor_motion();
#ifdef CONFIG_MOONWATER_WAYLAND
        else if (compositor_picture_poll(compositor.cursor.asked_seen >= 0 ? of : COMPOSITOR_NONE))
                compositor_cursor_move();
#endif
}

static unsigned int compositor_snap(const int *box, unsigned int *touch, int *screen)
{
        unsigned int count;
        const int (*screens)[4] = compositor_screens(&count);
        int at = compositor_screen_owner(screens, count, box[0], box[1], box[2], box[3]);

        *touch = 0, *screen = at;
        if (at < 0 || compositor_screen_cover(screens[at], box[0], box[1], box[2], box[3]) <= 0 ||
            (compositor_modifiers() & WINDOW_KEY_CONTROL))
                return COMPOSITOR_FLOATING;
        return compositor_snap_rule(box, screens[at], compositor_snap_free(screens, count, box, at), compositor.snap_held,
                                    COMPOSITOR_SNAP_EDGE * compositor_output_scale(at) / 100,
                                    COMPOSITOR_SNAP_CORNER * compositor_output_scale(at) / 100, touch);
}

static void compositor_box_of(int row, int *box)
{
        int x = compositor.owner != COMPOSITOR_PREDICT_MOUSE ? compositor.real_x : compositor.pointer_x;
        int y = compositor.owner != COMPOSITOR_PREDICT_MOUSE ? compositor.real_y : compositor.pointer_y;

        box[0] = x - compositor.drag_x, box[1] = y - compositor.drag_y;
        box[2] = (int)compositor.w[row], box[3] = (int)compositor.h[row];
}

static void compositor_preview(int row)
{
        unsigned int count, how = COMPOSITOR_FLOATING, touch = 0;
        const int (*screens)[4] = compositor_screens(&count);
        int screen = 0, rect[4] = {0, 0, 0, 0}, box[4];

        if (row >= 0 && row == compositor.drag && compositor.drag_moved)
        {
                compositor_box_of(row, box);
                how = compositor_target(row, compositor.pointer_x, compositor.pointer_y, box, &screen, &touch);
                if (how != COMPOSITOR_FLOATING)
                        compositor_screen_part(screens[screen], how, rect);
                compositor_overlays_drag(compositor.pointer_x, compositor.pointer_y);
        }
        else if (row < 0 && compositor.grid_on)
                compositor_overlays_damage(), compositor.grid_on = 0;
        if (row < 0)
                compositor.grid_since = 0;
        compositor.snap_held = touch;
        if (how == COMPOSITOR_FLOATING)
        {
                if (compositor.preview[2] > 0)
                        compositor_damage(compositor.preview[0], compositor.preview[1], compositor.preview[2], compositor.preview[3]), compositor_preview_leave();
                memory_zero(compositor.preview, sizeof(compositor.preview)), compositor.preview_row = COMPOSITOR_NONE;
                return;
        }
        if (row == compositor.preview_row && !memory_compare(rect, compositor.preview, sizeof(rect)))
                return;
        if (compositor.preview[2] > 0)
                compositor_damage(compositor.preview[0], compositor.preview[1], compositor.preview[2], compositor.preview[3]);
        compositor_preview_enter(row, rect, how);
        memory_copy(compositor.preview, rect, sizeof(rect)), compositor.preview_row = row;
        compositor_damage(rect[0], rect[1], rect[2], rect[3]);
}

static void compositor_lit(int row, unsigned int edges, unsigned int held);

static void compositor_drag_begin_from(int under, int press_x, int press_y)
{
        compositor_popups_close(under, -1);
        compositor.drag = under, compositor.drag_moved = 0;
        compositor_grab_set(COMPOSITOR_GRAB_MOVE, under, COMPOSITOR_BTN_LEFT, 0);
        compositor.drag_x = compositor.pointer_x - compositor.x[under], compositor.drag_y = compositor.pointer_y - compositor.y[under];
        compositor.press_x = press_x, compositor.press_y = press_y;
        compositor_hold_take(0, under);
        if (compositor.arranged[under] == COMPOSITOR_FLOATING)
        {
                int keep[4] = {compositor.x[under], compositor.y[under], (int)compositor.w[under], (int)compositor.h[under]};

                memory_copy(compositor.drag_keep, keep, sizeof(keep));
        }
        else
                compositor_row_saved_get(under, compositor.drag_keep);
        compositor_lit(under, COMPOSITOR_LIT_ALL, 1);
}

static void compositor_drag_start_client(int under)
{
        compositor_drag_begin_from(under, compositor.button_x, compositor.button_y);
        if (compositor.arranged[under] != COMPOSITOR_FLOATING &&
            ui_touch_past(compositor.press_x, compositor.press_y, compositor.pointer_x, compositor.pointer_y, ui_touch_slop(compositor_row_scale(under))))
                compositor_float_for_drag(under), compositor.drag_moved = 1, compositor_damage_row(under), compositor_preview(under);
}

static int compositor_grab_begin_client(int row, unsigned int edges, int is_move, int cancel)
{
        if (row < 0 || row >= COMPOSITOR_ROWS)
                return 0;
        if (cancel)
                return compositor_grab_end(COMPOSITOR_GRAB_STOP, row);
        if (!compositor.showing[row] || !(compositor.button_down & 1u) || compositor.button_row[0] != row ||
            compositor.drag >= 0 || compositor.resize >= 0)
                return 0;
        if (is_move)
                return compositor_drag_start_client(row), 1;
        if (!edges)
                return 0;
        compositor_resize_begin(row, edges);
        return compositor.resize == row ? (compositor_lit(row, compositor.edges[row], 1), 1) : 0;
}

#ifdef CONFIG_MOONWATER_WAYLAND
static void compositor_drag_begin_client(int under) { compositor_drag_start_client(under); }
static void compositor_drag_stop(int row) { (void)compositor_grab_end(COMPOSITOR_GRAB_STOP, row); }
#endif

static void compositor_release_as(unsigned int code, int snap, unsigned int flags)
{
        int dragged = compositor.drag, to = COMPOSITOR_NONE, resized = compositor.resize, drop_screen = 0, drop_box[4];
        unsigned int index = code - COMPOSITOR_BTN_LEFT, drop_how = COMPOSITOR_FLOATING, drop_touch = 0;

        compositor_pointer_press(code, 0);
        if (dragged >= 0 && snap && compositor.drag_moved)
                compositor_box_of(dragged, drop_box),
                drop_how = compositor_target(dragged, compositor.pointer_x, compositor.pointer_y, drop_box, &drop_screen, &drop_touch);
        compositor_grab_end(COMPOSITOR_GRAB_COMMIT, COMPOSITOR_NONE);
        if (code == COMPOSITOR_BTN_RIGHT && compositor.edge_right)
        {
                compositor.edge_right = 0;
                if (compositor.active_grab.kind == COMPOSITOR_GRAB_EDGE_CHORD)
                        compositor_grab_clear();
                compositor_lit(COMPOSITOR_NONE, 0, 0);
        }
        if (index < 8 && (compositor.button_down >> index & 1))
                to = compositor.button_row[index], compositor.button_down &= (unsigned char)~(1u << index), compositor_grab_settle();
        compositor_dnd_release(to);
        compositor_post(to, WINDOW_EVENT_BUTTON,
                        flags | (compositor_modifiers() & (WINDOW_KEY_SHIFT | WINDOW_KEY_CONTROL | WINDOW_KEY_ALT | WINDOW_KEY_SUPER)),
                        (int)code, 0);
        if (dragged >= 0 && snap && compositor.drag_moved && drop_how != COMPOSITOR_FLOATING)
        {
                compositor_arrange_on(dragged, drop_how, drop_screen);
                compositor_row_saved_set(dragged, compositor.drag_keep);
                compositor.assist_on = compositor.assist_armed = 1;
                compositor.assist_row = dragged, compositor.assist_how = (unsigned int)compositor.arranged[dragged], compositor.assist_screen = drop_screen;
                compositor_assist_refresh();
                if (!compositor.assist_count)
                        compositor_assist_close();
                compositor_overlays_damage();
        }
        if (compositor.grid_on)
                compositor_overlays_damage(), compositor.grid_on = 0;
        if (snap && (resized >= 0 || (dragged >= 0 && compositor.drag_moved)))
                compositor_remember(resized >= 0 ? resized : dragged);
}

static void compositor_at_hand(void (*act)(unsigned int, int), unsigned int code, int arg)
{
        int led_x, led_y;

        compositor_pointer_sync();
        if (compositor.at_real || compositor.touching || compositor.owner != COMPOSITOR_PREDICT_MOUSE)
                { act(code, arg); return; }
        led_x = compositor.pointer_x, led_y = compositor.pointer_y;
        compositor.at_real = 1, compositor_pointer_shown();
        if ((compositor.pointer_x != led_x || compositor.pointer_y != led_y) && !compositor_wl_locked())
                compositor_motion();
        act(code, arg);
        compositor.at_real = 0, compositor_pointer_shown(), compositor_cursor_move();
}

static void compositor_release_act(unsigned int code, int snap) { compositor_release_as(code, snap, 0); }
static void compositor_release(unsigned int code, int snap) { compositor_at_hand(compositor_release_act, code, snap); }

static int compositor_on_button(int row, int x, int y, int (*box)(const struct compositor_frame *, int *, int *, int *))
{
        struct compositor_frame frame;
        int bx, by, side;

        compositor_frame_of(row, &frame);
        if (compositor.touch.resumed || !box(&frame, &bx, &by, &side))
                return 0;
        return ui_reach_hit(compositor.touching, side + 2 * (COMPOSITOR_TOUCH_BUTTON_SLOP * frame.scale / 100), x, y, bx, by, side, side);
}

static int compositor_minimize_press(unsigned int code, int down)
{
        int under, tab = COMPOSITOR_NONE;

        if (!down)
        {
                if (compositor.active_grab.kind != COMPOSITOR_GRAB_MINIMIZE_BTN || !code || code != compositor.active_grab.code)
                        return 0;
                return compositor_grab_clear(), 1;
        }
        if (compositor.active_grab.kind == COMPOSITOR_GRAB_MINIMIZE_BTN)
                compositor_grab_clear();
        if (code != COMPOSITOR_BTN_LEFT || compositor_wl_held())
                return 0;
        compositor_pointer_clamp();
        under = compositor_under(compositor.pointer_x, compositor.pointer_y, &tab);
        if (under < 0 || tab >= 0 || !compositor_visible(under) ||
            !compositor_on_button(under, compositor.pointer_x, compositor.pointer_y, compositor_minimize_box))
                return 0;
        compositor_popups_close(-1, -1);
        compositor_grab_set(COMPOSITOR_GRAB_MINIMIZE_BTN, under, code, 0);
        compositor_minimize_row(under);
        return 1;
}

static void compositor_menu_roots(struct compositor_rows *roots)
{
        rows_none(roots);
        for (int row = 0; row < (int)compositor_rows_in_use; row++)
                if (compositor_popup_parent[row] >= 0 && compositor.showing[row] && !(compositor.state[row] & WINDOW_CLOSING))
                        rows_add(roots, compositor_popup_root(row));
}

static void compositor_cancel_holds(const struct compositor_rows *roots)
{
        for (unsigned int index = 0; index < 8; index++)
                if ((compositor.button_down >> index & 1) && compositor.button_row[index] >= 0 && rows_has(roots, compositor.button_row[index]))
                        compositor_release_as(COMPOSITOR_BTN_LEFT + index, 0, WINDOW_KEY_CANCEL);
        if ((compositor.drag >= 0 && rows_has(roots, compositor.drag)) || (compositor.resize >= 0 && rows_has(roots, compositor.resize)))
                compositor_release_as(COMPOSITOR_BTN_LEFT, 0, WINDOW_KEY_CANCEL);
}

static void compositor_edge_drag_start(int row)
{
        if (row < 0 || row >= COMPOSITOR_ROWS || !compositor.showing[row] || compositor.fullscreen[row])
                return;
        compositor_raise(row), compositor_focus(row);
        compositor_drag_begin_from(row, compositor.pointer_x, compositor.pointer_y);
        compositor.cursor.shape = CP_ARROW, compositor_cursor_move();
}

static void compositor_button_deliver(int row, unsigned int code, int save_left, int check_locked)
{
        compositor.grab = compositor.grab >= 0 ? compositor.grab : row;
        if (!check_locked || !compositor_wl_locked())
                compositor_pointer_to(row);
        compositor_post(row, WINDOW_EVENT_BUTTON,
                        WINDOW_KEY_DOWN | (compositor_modifiers() & (WINDOW_KEY_SHIFT | WINDOW_KEY_CONTROL | WINDOW_KEY_ALT | WINDOW_KEY_SUPER)),
                        (int)code, 0);
        if (code - COMPOSITOR_BTN_LEFT < 8)
        {
                compositor.button_down |= (unsigned char)(1u << (code - COMPOSITOR_BTN_LEFT));
                compositor.button_row[code - COMPOSITOR_BTN_LEFT] = row;
                if (save_left && code == COMPOSITOR_BTN_LEFT)
                        compositor.button_x = compositor.pointer_x, compositor.button_y = compositor.pointer_y;
        }
}

static int compositor_button_band(int tab, unsigned int code, int up)
{
        int px = compositor.pointer_x, py = compositor.pointer_y;

        if (tab == TASKBAR_LAUNCHER && code == COMPOSITOR_BTN_RIGHT)
                return compositor_menu_open(px, py), 1;
        if (tab <= TASKBAR_TRAY_ITEM_BASE && tab > TASKBAR_TRAY_ITEM_BASE - TASKBAR_TRAY_ITEM_MAX)
        {
                if (code == COMPOSITOR_BTN_LEFT || code == COMPOSITOR_BTN_RIGHT)
                        compositor_tray_click(TASKBAR_TRAY_ITEM_BASE - tab, code == COMPOSITOR_BTN_RIGHT ? 3 : 1);
                return 1;
        }
        if (tab == TASKBAR_LAUNCHER || tab == TASKBAR_MIC || tab == TASKBAR_VOLUME || tab == TASKBAR_NOTICES || tab == COMPOSITOR_TRAY)
        {
                switch (taskbar_cell_press(&compositor.cells, tab, code - COMPOSITOR_BTN_LEFT, clock_monotonic_nanoseconds(), px, py, compositor.scale))
                {
                case TASKBAR_ACT_LAUNCHER:
                        if (up >= 0)
                                compositor_close(up), compositor_tabs_changed();
                        else if (taskbar_launcher_on())
                                compositor_launcher_open(px, py);
                        break;
                case TASKBAR_ACT_VOLUME_OPEN: compositor_volume_open(px, py); break;
                case TASKBAR_ACT_VOLUME_MUTE: compositor_volume_step(0, 1); break;
                case TASKBAR_ACT_MIC_OPEN: compositor_mic_open(px, py); break;
                case TASKBAR_ACT_MIC_MUTE: compositor_mic_step(); break;
                case TASKBAR_ACT_SETTINGS: compositor_settings_open(); break;
                case TASKBAR_ACT_NOTICES_OPEN: compositor_notices_open(px, py); break;
                default: break;
                }
                return 1;
        }
        if (tab == TASKBAR_STRIP_BACK || tab == TASKBAR_STRIP_ON)
        {
                if (code == COMPOSITOR_BTN_LEFT && taskbar_strip_page(tab == TASKBAR_STRIP_ON ? 1 : -1))
                        compositor_tabs_changed();
                return 1;
        }
        if (tab < 0)
                return 0;
        if (code == COMPOSITOR_BTN_RIGHT)
                compositor_menu_open(px, py);
        else if (compositor.listed[tab] == TASKBAR_GHOST)
                { if (code == COMPOSITOR_BTN_LEFT) taskbar_pin_start(tab, clock_monotonic_nanoseconds()); }
        else if (code == COMPOSITOR_BTN_MIDDLE)
                compositor_close(compositor.listed[tab]);
        else if (code == COMPOSITOR_BTN_LEFT)
                compositor_reveal(compositor.listed[tab]);
        return 1;
}

static int compositor_button_window(int under, unsigned int code, int alt)
{
        int px = compositor.pointer_x, py = compositor.pointer_y, band, asked;
        unsigned int edges;

        if (compositor.grab >= 0 || compositor_wl_held())
                return 0;
        edges = compositor_edge_zone(under, px, py);
        if (code == COMPOSITOR_BTN_RIGHT && !alt)
        {
                compositor_raise(under), compositor_focus(under);
                if (!edges || compositor.fullscreen[under])
                        return 0;
                compositor.edge_right = (unsigned int)under + 1;
                compositor_grab_set(COMPOSITOR_GRAB_EDGE_CHORD, under, COMPOSITOR_BTN_RIGHT, edges);
                compositor.press_x = px, compositor.press_y = py;
                return compositor_lit(under, edges, 1), 1;
        }
        if (code != COMPOSITOR_BTN_LEFT && !(code == COMPOSITOR_BTN_RIGHT && alt))
                return 0;
        if (code == COMPOSITOR_BTN_LEFT && !alt && compositor.edge_right)
        {
                int row = (int)compositor.edge_right - 1;

                compositor.edge_right = 0;
                if (compositor.active_grab.kind == COMPOSITOR_GRAB_EDGE_CHORD)
                        compositor_grab_clear();
                if (row >= 0 && row < COMPOSITOR_ROWS && compositor.showing[row] && !compositor.fullscreen[row] &&
                    (compositor_edge_zone(row, px, py) ||
                     !ui_touch_past(compositor.press_x, compositor.press_y, px, py, ui_touch_slop(compositor_row_scale(row)))))
                        return compositor_edge_drag_start(row), 1;
        }
        compositor_raise(under), compositor_focus(under);
        if (compositor.touching && !alt && code == COMPOSITOR_BTN_LEFT && compositor_on_button(under, px, py, compositor_close_box))
        {
                compositor.press = COMPOSITOR_NONE, compositor.touch.closing = under;
                compositor_grab_set(COMPOSITOR_GRAB_TOUCH_CLOSE, under, COMPOSITOR_BTN_LEFT, 0);
                return ui_touch_pend(&compositor.touch.gesture, 1), 1;
        }
        if (edges && code == COMPOSITOR_BTN_LEFT)
        {
                if (!compositor_shared_begin(under, edges))
                        compositor_resize_begin(under, edges);
                return compositor_lit(under, edges, 1), 1;
        }
        if (alt && code == COMPOSITOR_BTN_RIGHT)
        {
                int cx = compositor.x[under] + (int)compositor.w[under] / 2, cy = compositor.y[under] + (int)compositor.h[under] / 2;

                compositor_resize_begin(under, (px < cx ? COMPOSITOR_EDGE_LEFT : COMPOSITOR_EDGE_RIGHT) |
                                                   (py < cy ? COMPOSITOR_EDGE_TOP : COMPOSITOR_EDGE_BOTTOM));
                if (compositor.resize == under)
                        compositor_lit(under, compositor.edges[under], 1);
                return 1;
        }
        band = !alt && code == COMPOSITOR_BTN_LEFT && !compositor.fullscreen[under] && !(compositor.asks[under] & WINDOW_NO_GRAB) &&
               px >= compositor.x[under] && px < compositor.x[under] + (int)compositor.w[under] &&
               py >= compositor.y[under] && py < compositor.y[under] + COMPOSITOR_GRAB * compositor.scale / 100;
        if (band && compositor_on_button(under, px, py, compositor_close_box))
                return compositor.press = COMPOSITOR_NONE, compositor_close(under), 1;
        asked = compositor_asked_shape(under);
        if (!alt && !(band && asked >= -1 && asked <= (int)WINDOW_POINTER_ARROW))
                return 0;
        if (ui_click_again(&compositor.press_click, clock_monotonic_nanoseconds(), px, py, compositor.scale) == 2 && under == compositor.press)
                return compositor.press = COMPOSITOR_NONE,
                       compositor_filling(under) ? compositor_float(under) : compositor_arrange(under, COMPOSITOR_MAXIMIZED), 1;
        compositor.press = under, compositor_drag_begin_from(under, px, py);
        return 1;
}

static void compositor_button(unsigned int code, int down)
{
        int under, tab, alt = (compositor_modifiers() & WINDOW_KEY_ALT) != 0, up;
        struct compositor_rows roots;

        compositor_pointer_clamp();
        under = compositor_under(compositor.pointer_x, compositor.pointer_y, &tab);
        if (compositor_band_hand(code, down))
                return;
        if (!down) { compositor_release(code, 1); return; }
        if (code == COMPOSITOR_BTN_RIGHT && compositor.resize >= 0 && !alt)
                { int row = compositor.resize; compositor_hold_cancel(), compositor_edge_drag_start(row); return; }
        if (code == COMPOSITOR_BTN_RIGHT && compositor_hold_cancel())
                return;
        if (under >= 0 && compositor_popup_parent[under] >= 0)
                { compositor_popups_close(-1, under), compositor_button_deliver(under, code, 0, 0); return; }
        compositor_menu_roots(&roots);
        if (compositor_popups_close(-1, -1)) { compositor_cancel_holds(&roots); return; }
        up = compositor_launcher_row();
        if (compositor.focus >= 0 && (compositor.asks[compositor.focus] & WINDOW_DISMISS) && compositor.showing[compositor.focus] &&
            !compositor_in_row(compositor.focus, compositor.pointer_x, compositor.pointer_y))
                compositor_close(compositor.focus);
        if (compositor_button_band(tab, code, up) || under < 0 || compositor_button_window(under, code, alt))
                return;
        if (compositor_in_row(under, compositor.pointer_x, compositor.pointer_y))
                compositor_button_deliver(under, code, 1, 1);
}

static void compositor_press_at(unsigned int code, int down);
static void compositor_press(unsigned int code, int down) { compositor_at_hand(compositor_press_at, code, down); }

static void compositor_press_at(unsigned int code, int down)
{
        if (down)
                compositor.logo_alone = 0, compositor_pointer_press(code, 1);
        if (down && code == COMPOSITOR_BTN_LEFT && compositor.assist_armed)
        {
                int at = compositor_assist_at(compositor.pointer_x, compositor.pointer_y);

                if (at >= 0)
                {
                        int row = (int)compositor.assist_rows[at], screen = compositor.assist_screen;
                        unsigned int how = compositor.assist_zone[at];

                        compositor_assist_close(), compositor_arrange_on(row, how, screen);
                        return;
                }
                compositor_assist_close();
        }
        if (!compositor_minimize_press(code, down))
                compositor_button(code, down);
}

// Touchpad finger tool keys (1..5 fingers), shared by key decode and SYN_DROPPED recovery.
static const unsigned short compositor_pad_tools[5] = {
    COMPOSITOR_BTN_TOOL_FINGER, COMPOSITOR_BTN_TOOL_DOUBLETAP, COMPOSITOR_BTN_TOOL_TRIPLETAP,
    COMPOSITOR_BTN_TOOL_QUADTAP, COMPOSITOR_BTN_TOOL_QUINTTAP,
};

static int compositor_pad_tool_count(unsigned int code)
{
        for (int k = 0; k < 5; k++)
                if (compositor_pad_tools[k] == code)
                        return k + 1;
        return 0;
}

static int compositor_pad_finger_key(unsigned int code) { return code == COMPOSITOR_BTN_TOUCH || compositor_pad_tool_count(code) != 0; }

static void compositor_pad_key(struct compositor_pad *pad, unsigned int code, int value)
{
        int count = compositor_pad_tool_count(code);

        if (code == COMPOSITOR_BTN_TOUCH)
                pad->touching = value != 0;
        else if (value)
                pad->fingers = count ? count : 5;
        else if (pad->fingers == (count ? count : 5))
                pad->fingers = 0;
        pad->moving = 0, pad->left_x = pad->left_y = 0;
}

static int compositor_speed(void) { return compositor.trackpad_speed < 25 || compositor.trackpad_speed > 400 ? 100 : (int)compositor.trackpad_speed; }

static inline long long compositor_scale_rem(long long delta, long long num, long long den, long long *rem)
{
        long long total = delta * num + *rem, out = total / den;

        *rem = total - out * den;
        return out;
}

static inline int compositor_scale_rem_i(long long delta, long long num, long long den, int *rem)
{
        long long r = *rem, out = compositor_scale_rem(delta, num, den, &r);

        *rem = (int)r;
        return (int)out;
}

static int compositor_pad_report(struct compositor_pad *pad, int *x, int *y)
{
        long dx = (long)pad->next_x - pad->at_x, dy = (long)pad->next_y - pad->at_y;
        int kind = pad->fingers >= 2 ? 2 : 1;
        int per = kind == 2 ? COMPOSITOR_PAD_WHEEL * compositor_speed() / 100 : COMPOSITOR_PAD_COUNTS;

        *x = *y = 0, pad->at_x = pad->next_x, pad->at_y = pad->next_y;
        if (!pad->touching || !pad->fingers || !pad->moving)
                return pad->moving = pad->touching && pad->fingers, 0;
        *x = compositor_scale_rem_i(dx, per, pad->per_mm_x, &pad->left_x);
        *y = compositor_scale_rem_i(kind == 2 ? -dy : dy, per, pad->per_mm_y, &pad->left_y);
        return *x || *y ? kind : 0;
}

/* ---- frame-ahead prediction ---- */

static positive compositor_predict_period(void) { return compositor.period ? compositor.period : 1000000000ull / 60; }

static void compositor_predict_lead(struct compositor_predict *track)
{
        const long long fx = COMPOSITOR_PREDICT_FX, period = (long long)compositor_predict_period() / 1000;
        const long long cap = (long long)COMPOSITOR_PREDICT_CAP * compositor.scale / 100 * fx;
        long long n = track->count, tn = (long long)track->when[n - 1], i0 = n - 1;
        long long St = 0, Stt = 0, Sx = 0, Stx = 0, Sy = 0, Sty = 0, path = 0, m, denom, lx, ly, net, g, w, ws, lim, len2;

        track->lead_x = track->lead_y = 0;
        while (i0 > 0 && (long long)track->when[i0 - 1] >= tn - (long long)COMPOSITOR_PREDICT_WINDOW_NS)
                i0--;
        m = n - i0;
        if (m < 2 || tn - (long long)track->when[i0] < (long long)COMPOSITOR_PREDICT_SPAN_NS || cap <= 0)
                return;
        for (long long i = i0; i < n; i++)
        {
                long long t = ((long long)track->when[i] - tn) / 1000;

                St += t, Stt += t * t, Sx += track->x[i], Stx += t * track->x[i], Sy += track->y[i], Sty += t * track->y[i];
                if (i > i0)
                        path += compositor_abs64(track->x[i] - track->x[i - 1]) + compositor_abs64(track->y[i] - track->y[i - 1]);
        }
        if (!(denom = m * Stt - St * St))
                return;
        lx = (m * Stx - St * Sx) * period / denom, ly = (m * Sty - St * Sy) * period / denom;
        net = compositor_abs64(track->x[n - 1] - track->x[i0]) + compositor_abs64(track->y[n - 1] - track->y[i0]);
        g = path > 0 ? net * 1000 / path : 0;
        w = compositor_clamp64((g - COMPOSITOR_PREDICT_STRAIGHT_LO) * 1000 / COMPOSITOR_PREDICT_STRAIGHT_SPAN, 0, 1000);
        ws = compositor_clamp64((net * 1000000 / (tn - (long long)track->when[i0]) - COMPOSITOR_PREDICT_STILL_LO) * 1000 /
                                (COMPOSITOR_PREDICT_STILL_HI - COMPOSITOR_PREDICT_STILL_LO), 0, 1000);
        lx = lx * w / 1000 * ws / 1000, ly = ly * w / 1000 * ws / 1000;
        if ((track->x[n - 1] - track->x[n - 2] < 0 && lx > 0) || (track->x[n - 1] - track->x[n - 2] > 0 && lx < 0))
                lx = 0;
        if ((track->y[n - 1] - track->y[n - 2] < 0 && ly > 0) || (track->y[n - 1] - track->y[n - 2] > 0 && ly < 0))
                ly = 0;
        lx = compositor_predict_px(lx), ly = compositor_predict_px(ly), lim = cap / fx, len2 = lx * lx + ly * ly;
        if (len2 > lim * lim)
        {
                long long len = (long long)compositor_isqrt((unsigned long long)len2);

                len += len * len < len2;
                lx = lx * lim / len, ly = ly * lim / len;
        }
        track->lead_x = lx * fx, track->lead_y = ly * fx;
}

static void compositor_predict_sample(unsigned int cls, long long x, long long y, positive when, positive now)
{
        struct compositor_predict *track = &compositor.predict[cls];

        if (track->count && track->x[track->count - 1] == x && track->y[track->count - 1] == y)
                return;
        if (track->count == COMPOSITOR_PREDICT_HISTORY)
        {
                memory_copy(track->x, track->x + 1, sizeof(track->x[0]) * (COMPOSITOR_PREDICT_HISTORY - 1));
                memory_copy(track->y, track->y + 1, sizeof(track->y[0]) * (COMPOSITOR_PREDICT_HISTORY - 1));
                memory_copy(track->when, track->when + 1, sizeof(track->when[0]) * (COMPOSITOR_PREDICT_HISTORY - 1));
                memory_copy(track->arrived, track->arrived + 1, sizeof(track->arrived[0]) * (COMPOSITOR_PREDICT_HISTORY - 1));
                track->count--;
        }
        track->x[track->count] = x, track->y[track->count] = y, track->when[track->count] = when, track->arrived[track->count++] = now;
        track->last = now, track->lead_x = track->lead_y = 0;
        if (!compositor.predict_off[cls] && track->count >= 2)
                compositor_predict_lead(track);
}

static void compositor_predict_mouse(positive when, positive now)
{
        (void)when, (void)now;
        compositor.owner = COMPOSITOR_PREDICT_MOUSE, compositor.osk_typed = 0;
        compositor_pointer_shown(), compositor_osk_sync();
}

static void compositor_predict_place(unsigned int cls, int x, int y, positive now)
{
        compositor_pointer_sync();
        compositor.owner = cls, compositor.osk_typed = 0, compositor.real_x = x, compositor.real_y = y;
        compositor_predict_sample(cls, (long long)x * COMPOSITOR_PREDICT_FX, (long long)y * COMPOSITOR_PREDICT_FX, now, now);
        compositor_pointer_shown(), compositor_osk_sync();
}

static void compositor_predict_clear(unsigned int cls)
{
        struct compositor_predict *track = &compositor.predict[cls];

        if (cls == COMPOSITOR_PREDICT_MOUSE)
                return;
        compositor_pointer_sync();
        if (!track->lead_x && !track->lead_y)
                return;
        track->lead_x = track->lead_y = 0, compositor.owner = cls;
        compositor.kept_x = compositor.real_x = (int)(track->x[track->count - 1] / COMPOSITOR_PREDICT_FX);
        compositor.kept_y = compositor.real_y = (int)(track->y[track->count - 1] / COMPOSITOR_PREDICT_FX);
        compositor_pointer_shown();
        compositor.touching = cls == COMPOSITOR_PREDICT_TOUCH, compositor.retracting = 1;
        compositor_motion();
        compositor.retracting = compositor.touching = 0;
}

static positive compositor_predict_due(void)
{
        positive period = compositor_predict_period(), due = 0;

        for (unsigned int cls = COMPOSITOR_PREDICT_TOUCH; cls < COMPOSITOR_PREDICTS; cls++)
        {
                const struct compositor_predict *track = &compositor.predict[cls];

                if ((track->lead_x || track->lead_y) && (!due || track->last + period < due))
                        due = track->last + period;
        }
        return due;
}

static void compositor_predict_expire(positive now)
{
        positive period = compositor_predict_period();

        for (unsigned int cls = COMPOSITOR_PREDICT_TOUCH; cls < COMPOSITOR_PREDICTS; cls++)
                if ((compositor.predict[cls].lead_x || compositor.predict[cls].lead_y) && now >= compositor.predict[cls].last + period)
                        compositor_predict_clear(cls);
}

static void compositor_predict_set(unsigned int cls, int on)
{
        compositor.predict_off[cls] = (unsigned char)!on;
        if (!on)
                compositor_predict_clear(cls);
}

static int compositor_report(positive now, positive stamp)
{
        int moved = 0;

        compositor_pointer_sync();
        if (compositor.tablet_have)
        {
                if (compositor.touch.gesture.mode == UI_TOUCH_NONE)
                {
                        if (compositor.tablet_have & 1)
                                compositor.real_x = compositor.tablet_x;
                        if (compositor.tablet_have & 2)
                                compositor.real_y = compositor.tablet_y;
                        compositor_predict_place(COMPOSITOR_PREDICT_PEN, compositor.real_x, compositor.real_y, now);
                        compositor.absolute = moved = 1;
                }
                compositor.tablet_have = 0;
        }
        if (compositor.raw_x || compositor.raw_y)
        {
                int dx = compositor.raw_x, dy = compositor.raw_y, gain, ax, ay;

                compositor.raw_x = compositor.raw_y = 0;
#ifdef CONFIG_MOONWATER_WAYLAND
                if (compositor_wl_locked())
                        return compositor_wl_counts(dx, dy, dx, dy, now), compositor_sling_sent++, moved;
#endif
                gain = compositor_gain_wanted(compositor_gain_of(dx, dy, now > compositor.stamp ? now - compositor.stamp : 0),
                                              compositor.flat, compositor.sensitivity);
                compositor.stamp = now;
                ax = compositor_accel(dx, &compositor.accel_x, gain), ay = compositor_accel(dy, &compositor.accel_y, gain);
                compositor.real_x = compositor_counts(compositor.real_x, ax), compositor.real_y = compositor_counts(compositor.real_y, ay);
#ifdef CONFIG_MOONWATER_WAYLAND
                compositor_wl_counts(ax, ay, dx, dy, now), compositor_wl_hold_keep();
#endif
                compositor_predict_mouse(stamp, now);
                moved = 1;
        }
        if (moved)
                compositor.touch.hidden = 0;
        return moved;
}

static int compositor_picture_under(int x, int y)
{
        if (compositor_band_hit(x, y) != TASKBAR_HIT_NONE)
                return COMPOSITOR_NONE;
        for (unsigned int at = compositor.stacked; at--;)
                if (compositor_visible(compositor.stack[at]) && compositor_in_row(compositor.stack[at], x, y))
                        return compositor.stack[at];
        return COMPOSITOR_NONE;
}

static int compositor_wheel_stream(unsigned int at, positive now)
{
        struct compositor_device *dev = &compositor.devices[at];
        positive last = dev->wheel_at, since = dev->wheel_stream_at;
        int within = last && now >= last && now - last <= UI_HAND_GAP_NS;
        int stream = within || (dev->wheel_streamed && since && now >= since && now - since <= COMPOSITOR_WHEEL_MEMORY_NS);

        if (stream)
                dev->wheel_streamed |= ++dev->wheel_run >= COMPOSITOR_WHEEL_STREAMS, dev->wheel_stream_at = now;
        else
                dev->wheel_run = 0, compositor.stream_hist_n[at] = 0;
        dev->wheel_at = now;
        return stream;
}

static int compositor_wheel_streams(unsigned int at, positive now)
{
        return (compositor.input_kinds[at] & WINDOWS_INPUT_MOUSE) ? 0 : compositor_wheel_stream(at, now);
}

static long long compositor_stream_gain(unsigned long long rate)
{
        long long x = rate > 100000 ? 100000 : rate > 1000 ? (long long)rate - 1000 : 0;
        unsigned long long xq, f;

        if (!x)
                return COMPOSITOR_GAIN_BASE;
        xq = (unsigned long long)x * 65536 / 3500;
        xq = xq > 65536 ? 65536 : xq;
        f = xq * ui_touch_isqrt(xq * 65536) / 65536;
        return COMPOSITOR_GAIN_BASE + COMPOSITOR_GAIN_BASE * 3 * (long long)f / 65536;
}

static unsigned long long compositor_stream_rate(unsigned int at)
{
        unsigned int n = compositor.stream_hist_n[at], m = n < COMPOSITOR_STREAM_HIST ? n : COMPOSITOR_STREAM_HIST;
        positive last, first;

        if (m < 2)
                return 0;
        last = compositor.stream_hist[at][(n - 1) % COMPOSITOR_STREAM_HIST];
        first = compositor.stream_hist[at][(n - m) % COMPOSITOR_STREAM_HIST];
        return last < first ? 0 : last == first ? 100000000000ull : (unsigned long long)(m - 1) * 100000000000ull / (last - first);
}

static long long compositor_stream_gain_at(unsigned int at, positive now)
{
        compositor.stream_hist[at][compositor.stream_hist_n[at]++ % COMPOSITOR_STREAM_HIST] = now;
        return compositor_stream_gain(compositor_stream_rate(at));
}

static long long compositor_stream_turn(unsigned int at, long long value120, long long gain, int horizontal)
{
        value120 = compositor_clamp64(value120, -1000000000ll, 1000000000ll);
        return compositor_scale_rem(value120, gain * (long long)compositor_speed(), COMPOSITOR_GAIN_TURN,
                                    &compositor.wheel_rest[at][horizontal ? 1 : 0]);
}

static void compositor_wheel(int v120, int horizontal, unsigned int flags)
{
        int row = compositor.grab, wx = compositor.pointer_x, wy = compositor.pointer_y;

        flags |= compositor_modifiers() & (WINDOW_KEY_SHIFT | WINDOW_KEY_CONTROL | WINDOW_KEY_ALT);
        compositor_pointer_clamp();
        if (row < 0)
        {
                int over = compositor_band_hit(compositor.pointer_x, compositor.pointer_y), turns;
                static int strip_notch, vol_notch;

                if ((over >= 0 || over == TASKBAR_STRIP_BACK || over == TASKBAR_STRIP_ON) &&
                    taskbar_strip_listed[0] + taskbar_strip_listed[1] > taskbar_strip_shown[0] + taskbar_strip_shown[1])
                {
                        if ((turns = ui_notch_take(&strip_notch, v120)) && taskbar_strip_step(-turns))
                                compositor_tabs_changed();
                        return;
                }
                if (!horizontal && taskbar_cell_of(over, TASKBAR_BUTTON_WHEEL) == TASKBAR_ACT_VOLUME_STEP)
                {
                        if ((turns = ui_notch_take(&vol_notch, v120)))
                                compositor_volume_step(COMPOSITOR_VOLUME_STEP * turns, 0);
                        return;
                }
                row = compositor.hover = compositor_picture_under(compositor.pointer_x, compositor.pointer_y);
        }
        if (row >= 0)
        {
                compositor_window_point(row, compositor.pointer_x, compositor.pointer_y, &wx, &wy);
                if (!compositor_wl_locked() && (row != compositor.told_row || wx != compositor.told_x || wy != compositor.told_y))
                        compositor_pointer_to(row);
                compositor_post(row, WINDOW_EVENT_WHEEL, flags, v120, horizontal)
                    ? compositor.table->wheel_sent++ : compositor.table->wheel_full++;
        }
        else
                compositor.table->wheel_nowhere++;
        compositor.table->wheel_row = row, compositor.table->wheel_x = wx, compositor.table->wheel_y = wy;
}

static void compositor_pad_slot(struct compositor_pad *pad, unsigned int code, int value)
{
        int slot = pad->slot;

        if (code == COMPOSITOR_ABS_MT_SLOT)
                pad->slot = (int)compositor_clamp64(value, 0, UI_TOUCH_SLOTS - 1);
        else if (code == COMPOSITOR_ABS_MT_TRACKING_ID)
                pad->mt_down[slot] = value >= 0;
        else if (code == COMPOSITOR_ABS_MT_POSITION_X)
                pad->mt_x[slot] = value;
        else if (code == COMPOSITOR_ABS_MT_POSITION_Y)
                pad->mt_y[slot] = value;
}

static int compositor_pad_span(const struct compositor_pad *pad, int a, int b)
{
        long long pmx = pad->per_mm_x > 0 ? pad->per_mm_x : 1, pmy = pad->per_mm_y > 0 ? pad->per_mm_y : 1;
        long long dx = (long long)(pad->mt_x[a] - pad->mt_x[b]) * 1000 / pmx, dy = (long long)(pad->mt_y[a] - pad->mt_y[b]) * 1000 / pmy;

        return (int)ui_touch_isqrt((unsigned long long)(dx * dx + dy * dy));
}

#define COMPOSITOR_PAD_PINCH_SLOP 4000
static void compositor_pinch(const struct ui_touch_event *e);

static void compositor_pad_pinch_emit(int kind, int scale)
{
        struct ui_touch_event e = {(enum ui_touch_kind)kind, compositor.pointer_x, compositor.pointer_y, 0, 0, scale};
        compositor_pinch(&e);
}

static int compositor_pad_pinch(struct compositor_pad *pad)
{
        int first = -1, second = -1, count = 0, span, scale;

        for (int slot = 0; slot < UI_TOUCH_SLOTS; slot++)
                if (pad->mt_down[slot])
                        count++, first < 0 ? (first = slot) : second < 0 ? (second = slot) : 0;
        if (count != pad->pinch_count)
        {
                if (pad->pinching)
                        compositor_pad_pinch_emit(UI_TOUCH_PINCH_END, pad->pinch_scale);
                pad->pinching = 0, pad->pinch_scale = UI_TOUCH_PINCH_ONE, pad->pinch_count = count;
                pad->pinch_ref = count == 2 ? compositor_pad_span(pad, first, second) : 0;
                return 0;
        }
        if (count != 2)
                return 0;
        span = compositor_pad_span(pad, first, second);
        if (!pad->pinching)
        {
                if (!ui_touch_span_past(pad->pinch_ref, span, COMPOSITOR_PAD_PINCH_SLOP))
                        return 0;
                pad->pinching = 1, pad->pinch_scale = UI_TOUCH_PINCH_ONE;
                compositor_pad_pinch_emit(UI_TOUCH_PINCH_BEGIN, UI_TOUCH_PINCH_ONE);
        }
        if ((scale = ui_touch_ratio(span, pad->pinch_ref, COMPOSITOR_PAD_PINCH_SLOP)) != pad->pinch_scale)
                pad->pinch_scale = scale, compositor_pad_pinch_emit(UI_TOUCH_PINCH_UPDATE, scale);
        return 1;
}

#define COMPOSITOR_PAD_GAIN_SLOW 40
#define COMPOSITOR_PAD_GAIN_FAST 240
#define COMPOSITOR_PAD_GAIN_TOP 614
#define COMPOSITOR_PAD_GAIN_PAUSE_NS 100000000ull

static void compositor_pad_gain(struct compositor_pad *pad, positive now, int *x, int *y)
{
        unsigned long long since = pad->gain_at, gap = now - since;
        long long ax = compositor_abs64(*x), ay = compositor_abs64(*y), turn = ax > ay ? ax : ay, speed, gain;

        pad->gain_at = now;
        if (!since || now <= since || gap > COMPOSITOR_PAD_GAIN_PAUSE_NS)
                { pad->gain_left_x = pad->gain_left_y = 0; return; }
        speed = turn * 1000000000ll / (long long)gap / COMPOSITOR_PAD_WHEEL;
        gain = speed <= COMPOSITOR_PAD_GAIN_SLOW ? 256
               : speed >= COMPOSITOR_PAD_GAIN_FAST ? COMPOSITOR_PAD_GAIN_TOP
               : 256 + (speed - COMPOSITOR_PAD_GAIN_SLOW) * (COMPOSITOR_PAD_GAIN_TOP - 256) / (COMPOSITOR_PAD_GAIN_FAST - COMPOSITOR_PAD_GAIN_SLOW);
        *x = compositor_scale_rem_i(*x, gain, 256, &pad->gain_left_x);
        *y = compositor_scale_rem_i(*y, gain, 256, &pad->gain_left_y);
}

static void compositor_pad_apply(unsigned int at, positive now)
{
        struct compositor_pad *pad = compositor_pad_of(at);
        int x, y, kind = compositor_pad_report(pad, &x, &y), pinch = compositor_pad_pinch(pad);

        if (kind == 1)
                compositor.raw_x = compositor_counts(compositor.raw_x, x), compositor.raw_y = compositor_counts(compositor.raw_y, y);
        else if (kind == 2 && !pinch)
        {
                compositor.table->input_wheel_read[at]++, compositor.table->input_wheel_used[at]++, compositor.table->input_wheel_last[at] = now;
                compositor_pad_gain(pad, now, &x, &y);
                if (y)
                        compositor_wheel(y, 0, WINDOW_WHEEL_PRECISE);
                if (x)
                        compositor_wheel(x, 1, WINDOW_WHEEL_PRECISE);
                if (x || y)
                        pad->turned = 1;
        }
}

/* ---- touch marks, chips & write-only overlay scratch ---- */

static void compositor_spot_undraw(int slot)
{
        int *drawn = compositor.spot[slot].drawn;

        if (drawn[2])
                compositor_damage_over(drawn[0], drawn[1], drawn[2], drawn[2]), drawn[2] = 0;
}

static void compositor_spot_redraw(int slot)
{
        int radius = compositor_spot_radius(compositor.scale), *drawn = compositor.spot[slot].drawn;

        compositor_spot_undraw(slot);
        drawn[0] = compositor.spot[slot].x - radius, drawn[1] = compositor.spot[slot].y - radius, drawn[2] = 2 * radius;
        compositor_damage_over(drawn[0], drawn[1], drawn[2], drawn[2]);
}

static void compositor_spot_set(int slot, int x, int y)
{
        if (!compositor.spots_on || slot < 0 || slot >= UI_TOUCH_SLOTS ||
            (compositor.spot[slot].on == 1 && compositor.spot[slot].x == x && compositor.spot[slot].y == y))
                return;
        compositor.spot[slot].x = x, compositor.spot[slot].y = y, compositor.spot[slot].on = 1, compositor.spot[slot].alpha = 255;
        compositor.spots_shown |= 1u << slot, compositor.spots_fading &= ~(1u << slot);
        compositor_spot_redraw(slot);
}

static void compositor_spot_lift(int slot, positive now)
{
        if (slot < 0 || slot >= UI_TOUCH_SLOTS || compositor.spot[slot].on != 1)
                return;
        compositor.spot[slot].on = 2, compositor.spot[slot].lifted = now, compositor.spots_fading |= 1u << slot;
}

static unsigned int compositor_chip_stroke(long px, long py, long ax, long ay, long bx, long by, long radius)
{
        long dx = bx - ax, dy = by - ay, qx = px - ax, qy = py - ay;
        long long len2 = (long long)dx * dx + (long long)dy * dy, dot = (long long)qx * dx + (long long)qy * dy, d2, c;

        if (dot <= 0)
                d2 = (long long)qx * qx + (long long)qy * qy;
        else if (dot >= len2)
                d2 = (long long)(px - bx) * (px - bx) + (long long)(py - by) * (py - by);
        else
                { long long cross = (long long)qx * dy - (long long)qy * dx; d2 = cross * cross / len2; }
        c = (long long)radius + 1 - (long long)ui_touch_isqrt((unsigned long long)d2);
        return c <= 0 ? 0 : c >= 2 ? 255 : 128;
}

static void compositor_chip_stroke_into(unsigned char *layer, int w, int h, long ax, long ay, long bx, long by, long radius)
{
        long x0 = compositor_clamp64((ax < bx ? ax : bx) - radius / 2 - 2, 0, w), x1 = compositor_clamp64((ax > bx ? ax : bx) + radius / 2 + 3, 0, w);
        long y0 = compositor_clamp64((ay < by ? ay : by) - radius / 2 - 2, 0, h), y1 = compositor_clamp64((ay > by ? ay : by) + radius / 2 + 3, 0, h);

        for (long y = y0; y < y1; y++)
                for (long x = x0; x < x1; x++)
                {
                        unsigned int c = compositor_chip_stroke(2 * x + 1, 2 * y + 1, 2 * ax, 2 * ay, 2 * bx, 2 * by, radius);

                        if (c > layer[y * w + x])
                                layer[y * w + x] = (unsigned char)c;
                }
}

static void compositor_chip_segs(unsigned char *layer, int w, int h, long cx, long cy, long g, const signed char (*s)[4], int n, long r)
{
        for (int k = 0; k < n; k++)
                compositor_chip_stroke_into(layer, w, h, cx + s[k][0] * g / 2, cy + s[k][1] * g / 2, cx + s[k][2] * g / 2, cy + s[k][3] * g / 2, r);
}

static void compositor_chip_rect_strokes(unsigned char *layer, int w, int h, long x0, long y0, long x1, long y1, long r)
{
        long p[4][4] = {{x0, y0, x1, y0}, {x1, y0, x1, y1}, {x1, y1, x0, y1}, {x0, y1, x0, y0}};

        for (int k = 0; k < 4; k++)
                compositor_chip_stroke_into(layer, w, h, p[k][0], p[k][1], p[k][2], p[k][3], r);
}

static void compositor_chip_body(unsigned char *cover, int w, int h)
{
        long r = h / 2;

        for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++)
                        cover[y * w + x] = (unsigned char)(compositor_chip_stroke(2 * x + 1, 2 * y + 1, 2 * r, 2 * r, 2 * (w - r), 2 * r, 2 * r) *
                                                           COMPOSITOR_CHIP_BODY_COVER / 255);
}

static void compositor_chip_hide(void);

static int compositor_chip_prep(int w, int h, int zero_glyph)
{
        struct compositor_chip *c = &compositor.chip;

        if (w < 1 || h < 1 || w > COMPOSITOR_CHIP_W_MOST || h > COMPOSITOR_CHIP_H_MOST)
                return 0;
        if (!c->cover)
                c->cover = (unsigned char *)compositor_map_zeroed(2UL * COMPOSITOR_CHIP_W_MOST * COMPOSITOR_CHIP_H_MOST);
        if (!c->cover)
                return 0;
        compositor_chip_hide();
        compositor_chip_body(c->cover, w, h);
        if (zero_glyph)
                memory_zero(c->cover + (unsigned long)w * h, (positive)w * h);
        return 1;
}

static void compositor_chip_place(int *box)
{
        const struct compositor_chip *c = &compositor.chip;

        box[2] = c->w, box[3] = c->h;
        box[0] = c->kind == COMPOSITOR_CHIP_CONTROLS ? compositor.x[c->row] + c->rel_x : c->rel_x;
        box[1] = c->kind == COMPOSITOR_CHIP_CONTROLS ? compositor.y[c->row] + c->rel_y - c->slide : c->rel_y;
}

static void compositor_chip_redraw(void)
{
        struct compositor_chip *c = &compositor.chip;
        int box[5], changed = 0;

        compositor_chip_place(box), box[4] = (int)c->alpha;
        for (int i = 0; i < 5; i++)
                changed |= box[i] != c->drawn[i];
        if (!changed)
                return;
        if (c->drawn[2] > 0 && c->drawn[3] > 0)
                compositor_damage_over(c->drawn[0], c->drawn[1], c->drawn[2], c->drawn[3]);
        memory_copy(c->drawn, box, sizeof(c->drawn));
        if (c->drawn[2] > 0 && c->drawn[3] > 0)
                compositor_damage_over(c->drawn[0], c->drawn[1], c->drawn[2], c->drawn[3]);
}

static void compositor_chip_hide(void)
{
        struct compositor_chip *c = &compositor.chip;

        if (c->drawn[2] > 0 && c->drawn[3] > 0)
                compositor_damage_over(c->drawn[0], c->drawn[1], c->drawn[2], c->drawn[3]);
        memory_zero(c->drawn, sizeof(c->drawn)), c->kind = COMPOSITOR_CHIP_NONE, c->alpha = 0;
}

static void compositor_chip_show(int kind, int row, int cells, int rx, int ry, int w, int h, int slide, positive now, positive hold, positive fade)
{
        struct compositor_chip *c = &compositor.chip;

        c->kind = kind, c->row = row, c->cells = cells, c->rel_x = rx, c->rel_y = ry, c->w = w, c->h = h;
        c->slide = slide, c->alpha = 255, c->shown = now, c->hold = hold, c->fade = fade;
        compositor_chip_redraw();
}

static int compositor_chip_controls(int row, positive now)
{
        static const signed char s_close[2][4] = {{-2, -2, 2, 2}, {2, -2, -2, 2}}, s_min[1][4] = {{-2, 0, 2, 0}};
        int cells = row >= 0 && row < COMPOSITOR_ROWS && (compositor.asks[row] & WINDOW_NO_TAB) ? 2 : 3;
        int scale = compositor.scale, cell = UI_EDGE_CONTROL_CELL * scale / 100, w = cells * cell, h = COMPOSITOR_CHIP_HEIGHT_DP * scale / 100;
        long g = COMPOSITOR_CHIP_GLYPH_DP * scale / 100, cy = h / 2, r = 3 * scale / 200 > 2 ? 3 * scale / 200 : 2;
        unsigned char *layer;

        if (row < 0 || row >= COMPOSITOR_ROWS || !compositor_visible(row) || cell < 1 || (int)compositor.w[row] < w + 2 * cell || !compositor_chip_prep(w, h, 1))
                return 0;
        layer = compositor.chip.cover + (unsigned long)w * h;
        for (int at = 0; at < cells; at++)
        {
                long cx = (long)at * cell + cell / 2;
                int i = cells == 2 && at > 0 ? at + 1 : at;

                if (i == 0)
                        compositor_chip_segs(layer, w, h, cx, cy, g, s_close, 2, r);
                else if (i == 1)
                        compositor_chip_segs(layer, w, h, cx, cy, g, s_min, 1, r);
                else
                        compositor_chip_rect_strokes(layer, w, h, cx - g, cy - g, cx + g, cy + g, r);
        }
        compositor_chip_show(COMPOSITOR_CHIP_CONTROLS, row, cells, ((int)compositor.w[row] - w) / 2, COMPOSITOR_CHIP_TOP_DP * scale / 100,
                             w, h, compositor.table && compositor.table->motion ? 0 : h, now, COMPOSITOR_CHIP_HOLD_NS, (positive)ui_paced(COMPOSITOR_CHIP_FADE_NS));
        return 1;
}

static void compositor_chip_hint(const int *rect, int x, int y, int side, positive now)
{
        int scale = compositor.scale, vertical = side == UI_EDGE_LEFT || side == UI_EDGE_RIGHT;
        int w = (vertical ? COMPOSITOR_CHIP_HINT_H_DP : COMPOSITOR_CHIP_HINT_W_DP) * scale / 100;
        int h = (vertical ? COMPOSITOR_CHIP_HINT_W_DP : COMPOSITOR_CHIP_HINT_H_DP) * scale / 100;
        int m = COMPOSITOR_CHIP_HINT_MARGIN_DP * scale / 100, rx, ry;

        if (!compositor_chip_prep(w, h, 0))
                return;
        if (vertical)
                rx = side == UI_EDGE_LEFT ? rect[0] + m : rect[0] + rect[2] - w - m,
                ry = (int)compositor_clamp64(y - h / 2, rect[1] + m, rect[1] + rect[3] - h - m);
        else
                rx = (int)compositor_clamp64(x - w / 2, rect[0] + m, rect[0] + rect[2] - w - m),
                ry = side == UI_EDGE_BOTTOM ? rect[1] + rect[3] - h - m : rect[1] + m;
        compositor_chip_show(COMPOSITOR_CHIP_HINT, COMPOSITOR_NONE, 0, rx, ry, w, h, 0, now, COMPOSITOR_CHIP_HINT_NS, 0);
}

static void compositor_chip_part(unsigned char *glyph, int w, int h, long x, long y, long size, const struct ui_icon_part *part, long radius)
{
        long x0 = x + part->x0 * size / 48, y0 = y + part->y0 * size / 48, x1 = x + part->x1 * size / 48, y1 = y + part->y1 * size / 48;

        if (part->kind == UI_ICON_LINE)
                compositor_chip_stroke_into(glyph, w, h, x0, y0, x1, y1, radius);
        else if (part->kind == UI_ICON_RING)
                compositor_chip_rect_strokes(glyph, w, h, x0, y0, x1, y1, radius);
        else if (part->kind == UI_ICON_BOX || part->kind == UI_ICON_SHADE)
                for (long py = y0 < 0 ? 0 : y0; py < y1 && py < h; py++)
                        for (long px = x0 < 0 ? 0 : x0; px < x1 && px < w; px++)
                        {
                                unsigned char want = part->kind == UI_ICON_BOX ? 255 : 85;
                                if (want > glyph[py * w + px])
                                        glyph[py * w + px] = want;
                        }
}

static void compositor_chip_icon(unsigned char *glyph, int w, int h, long x, long y, long size, int kind, long radius)
{
        if (kind < 0 || kind >= UI_FILE_KINDS)
                return;
        if (kind >= UI_FILE_PLAIN && kind <= UI_FILE_DOCUMENT)
                for (int k = 0; k < 7; k++)
                        compositor_chip_part(glyph, w, h, x, y, size, &ui_file_page[k], radius);
        for (int k = 0; k < UI_FILE_PARTS && ui_file_parts[kind][k].kind; k++)
                compositor_chip_part(glyph, w, h, x, y, size, &ui_file_parts[kind][k], radius);
}

static void compositor_chip_figure(unsigned char *glyph, int w, int h, long x, long y, long fw, long fh, int figure, long radius)
{
        static const unsigned char lit[10] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f};
        long x1 = x + fw, ym = y + fh / 2, y1 = y + fh;
        long ends[7][4] = {{x, y, x1, y}, {x1, y, x1, ym}, {x1, ym, x1, y1}, {x, y1, x1, y1}, {x, ym, x, y1}, {x, y, x, ym}, {x, ym, x1, ym}};

        for (int k = 0; k < 7; k++)
                if (lit[figure] >> k & 1)
                        compositor_chip_stroke_into(glyph, w, h, ends[k][0], ends[k][1], ends[k][2], ends[k][3], radius);
}

static void compositor_chip_drag(int action)
{
        static const signed char s_copy[2][4] = {{-2, 0, 2, 0}, {0, -2, 0, 2}}, s_slash[1][4] = {{-2, 2, 2, -2}};
        static const signed char s_move[3][4] = {{-2, 0, 2, 0}, {1, -1, 2, 0}, {1, 1, 2, 0}};
        struct compositor_chip *c = &compositor.chip;
        int scale = compositor.scale, h = COMPOSITOR_CHIP_HEIGHT_DP * scale / 100, w = h * 3 / 2, gap = COMPOSITOR_CHIP_DRAG_GAP_DP * scale / 100;
        unsigned int items = compositor_chip_drag_items > 1 ? (compositor_chip_drag_items > 999 ? 999 : compositor_chip_drag_items) : 0;
        int figures = items >= 100 ? 3 : items >= 10 ? 2 : items ? 1 : 0, glyph_w = w, kind = (int)compositor_chip_drag_kind - 1;
        int icon = kind >= 0 && kind < UI_FILE_KINDS ? h * 2 / 3 : 0, icon_x = glyph_w - h / 8;

        if (action < 0)
        {
                compositor_chip_drag_items = compositor_chip_drag_kind = 0;
                if (c->kind == COMPOSITOR_CHIP_DRAG)
                        compositor_chip_hide();
                return;
        }
        w += (icon ? icon + h / 6 : 0) + (figures ? figures * (h * 2 / 5) + h / 4 : 0);
        glyph_w += icon ? icon + h / 6 : 0;
        if (c->kind != COMPOSITOR_CHIP_DRAG || c->cells != action || c->w != w)
        {
                long g = COMPOSITOR_CHIP_GLYPH_DP * scale / 100, cx = (h * 3 / 2) / 2, cy = h / 2, r = 3 * scale / 200 > 2 ? 3 * scale / 200 : 2;
                unsigned char *glyph;

                if (!compositor_chip_prep(w, h, 1))
                        return;
                glyph = c->cover + (unsigned long)w * h;
                if (action & WINDOWS_DRAG_COPY)
                        compositor_chip_segs(glyph, w, h, cx, cy, g, s_copy, 2, r);
                else if (action & WINDOWS_DRAG_MOVE)
                        compositor_chip_segs(glyph, w, h, cx, cy, g, s_move, 3, r);
                else if (action & WINDOWS_DRAG_ASK)
                        for (long dot = -1; dot <= 1; dot++)
                                compositor_chip_stroke_into(glyph, w, h, cx + dot * g, cy, cx + dot * g, cy, r + 1);
                else
                        compositor_chip_segs(glyph, w, h, cx, cy, g, s_slash, 1, r);
                if (icon)
                        compositor_chip_icon(glyph, w, h, icon_x, (h - icon) / 2, icon, kind, r > 2 ? r - 1 : r);
                for (int k = 0, value = (int)items; k < figures; k++, value /= 10)
                        compositor_chip_figure(glyph, w, h, glyph_w - h / 8 + (figures - 1 - k) * (h * 2 / 5), cy - g, h / 4, 2 * g, value % 10, r);
                c->kind = COMPOSITOR_CHIP_DRAG, c->row = COMPOSITOR_NONE, c->cells = action, c->w = w, c->h = h, c->slide = 0, c->alpha = 255;
                c->shown = clock_monotonic_nanoseconds(), c->hold = (positive)1 << 62, c->fade = 0;
        }
        c->rel_x = compositor.pointer_x + gap, c->rel_y = compositor.pointer_y + gap;
        compositor_chip_redraw();
}

static int compositor_chip_cell(int x, int y)
{
        const struct compositor_chip *c = &compositor.chip;
        int x0, y0, slop = UI_EDGE_CONTROL_SLOP * compositor.scale / 100, cell;

        if (c->kind != COMPOSITOR_CHIP_CONTROLS)
                return -1;
        x0 = compositor.x[c->row] + c->rel_x, y0 = compositor.y[c->row] + c->rel_y;
        if (x < x0 || x >= x0 + c->w || y < y0 - slop || y >= y0 + c->h + slop)
                return -1;
        cell = (x - x0) / (c->w / c->cells);
        if (cell >= c->cells)
                cell = c->cells - 1;
        return c->cells == 2 && cell > 0 ? cell + 1 : cell;
}

static int compositor_chip_on(int x, int y)
{
        int box[4];

        if (compositor.chip.kind == COMPOSITOR_CHIP_NONE)
                return 0;
        compositor_chip_place(box);
        return x >= box[0] && x < box[0] + box[2] && y >= box[1] && y < box[1] + box[3];
}

static void compositor_chip_tick(positive now)
{
        struct compositor_chip *c = &compositor.chip;
        positive elapsed, total;

        if (c->kind == COMPOSITOR_CHIP_NONE)
                return;
        elapsed = now > c->shown ? now - c->shown : 0, total = c->hold + c->fade;
        if (elapsed >= total) { compositor_chip_hide(); return; }
        c->alpha = elapsed <= c->hold ? 255 : (unsigned int)(255 * (total - elapsed) / c->fade);
        if (c->kind == COMPOSITOR_CHIP_CONTROLS)
        {
                b64 e = ui_bezier_at(&ui_ease, ui_progress((b64)elapsed, ui_paced((b64)COMPOSITOR_CHIP_SLIDE_NS)));
                c->slide = compositor.table && compositor.table->motion ? 0 : c->h - (int)(((b64)c->h * e) / UI_ONE);
        }
        compositor_chip_redraw();
}

static void compositor_chip_draw(const struct compositor_target *t)
{
        const struct compositor_chip *c = &compositor.chip;
        int box[4], layers = c->kind == COMPOSITOR_CHIP_HINT ? 1 : 2, x1, y1, x2, y2;

        if (c->kind == COMPOSITOR_CHIP_NONE || !c->alpha)
                return;
        compositor_chip_place(box);
        x1 = box[0] > t->x1 ? box[0] : t->x1, y1 = box[1] > t->y1 ? box[1] : t->y1;
        x2 = box[0] + box[2] < t->x2 ? box[0] + box[2] : t->x2, y2 = box[1] + box[3] < t->y2 ? box[1] + box[3] : t->y2;
        if (x2 <= x1 || y2 <= y1)
                return;
        compositor_reading(t);
        for (int layer = 0; layer < layers; layer++)
                memory_mix_const_u32_rect(t->pixels + (unsigned long)(y1 - t->oy) * t->pitch + (unsigned long)(x1 - t->ox), t->pitch,
                                          c->cover + (unsigned long)layer * c->w * c->h + (unsigned long)(y1 - box[1]) * c->w + (unsigned long)(x1 - box[0]),
                                          (positive)c->w, (positive)(x2 - x1), (positive)(y2 - y1),
                                          c->kind == COMPOSITOR_CHIP_HINT || layer ? COMPOSITOR_CHIP_GLYPH : COMPOSITOR_CHIP_BODY, c->alpha);
}

static int compositor_spots_any(int fading) { return (fading ? compositor.spots_fading : compositor.spots_shown) != 0 || compositor.chip.kind != COMPOSITOR_CHIP_NONE; }

static void compositor_spots_tick(positive now)
{
        compositor_chip_tick(now);
        for (unsigned int slot = 0; slot < UI_TOUCH_SLOTS; slot++)
        {
                positive since;
                unsigned int alpha;

                if (compositor.spot[slot].on != 2)
                        continue;
                since = now > compositor.spot[slot].lifted ? now - compositor.spot[slot].lifted : 0;
                if (since >= COMPOSITOR_SPOT_FADE_NS)
                {
                        compositor_spot_undraw((int)slot);
                        compositor.spot[slot].on = 0, compositor.spot[slot].alpha = 0;
                        compositor.spots_shown &= ~(1u << slot), compositor.spots_fading &= ~(1u << slot);
                        continue;
                }
                if ((alpha = (unsigned int)((COMPOSITOR_SPOT_FADE_NS - since) * 255 / COMPOSITOR_SPOT_FADE_NS)) != compositor.spot[slot].alpha)
                        compositor.spot[slot].alpha = alpha, compositor_spot_redraw((int)slot);
        }
}

static void compositor_spots_clear(void)
{
        for (unsigned int slot = 0; slot < UI_TOUCH_SLOTS; slot++)
                compositor_spot_undraw((int)slot), compositor.spot[slot].on = 0;
        compositor.spots_shown = compositor.spots_fading = 0;
}

static void compositor_spots_ready(void)
{
        int radius = compositor_spot_radius(compositor.scale);
        compositor_corner_mask(radius), compositor_corner_mask(radius - compositor_px1(compositor.scale));
}

static void compositor_spots_marks(const struct compositor_target *t)
{
        if (!compositor.spots_shown)
                return;
        for (unsigned int slot = 0; slot < UI_TOUCH_SLOTS; slot++)
                if (compositor.spot[slot].on)
                        compositor_spot_draw(t, compositor.spot[slot].x, compositor.spot[slot].y, compositor.scale, compositor.spot[slot].alpha);
}

static void compositor_spots_draw(const struct compositor_target *t) { compositor_spots_marks(t), compositor_chip_draw(t); }

#define COMPOSITOR_OVERLAY_PIXELS ((unsigned long)COMPOSITOR_CHIP_W_MOST * COMPOSITOR_CHIP_H_MOST)
_Static_assert(COMPOSITOR_OVERLAY_PIXELS >= (unsigned long)CP_SPAN_MOST * CP_SPAN_MOST, "overlay scratch fits cursor");
_Static_assert(COMPOSITOR_OVERLAY_PIXELS >= (unsigned long)(2 * COMPOSITOR_MASK_MOST) * (2 * COMPOSITOR_MASK_MOST), "overlay scratch fits spot");
static unsigned int *compositor_overlay_scratch;

static int compositor_overlay_compose(const struct compositor_target *t, const struct compositor_frame *frames, unsigned int count,
                                      const int rect[4], void (*draw)(const struct compositor_target *))
{
        int box[4] = {rect[0], rect[1], rect[2], rect[3]}, clip[4] = {t->x1, t->y1, t->x2, t->y2};

        compositor_meet(box, clip);
        if (box[2] <= box[0] || box[3] <= box[1])
                return 1;
        if (!compositor_overlay_scratch)
                compositor_overlay_scratch = (unsigned int *)compositor_map_zeroed(4ul * COMPOSITOR_OVERLAY_PIXELS);
        if (!compositor_overlay_scratch)
                return 0;
        struct compositor_target square = {compositor_overlay_scratch, (unsigned long)(box[2] - box[0]), t->width, t->height,
                                           box[0], box[1], box[2], box[3], box[0], box[1], 0, 0};
        compositor_scene(&square, frames, count), draw(&square);
        compositor_blit(t, box[0], box[1], box[2] - box[0], box[3] - box[1], compositor_overlay_scratch, (unsigned long)(box[2] - box[0]));
        return 1;
}

static void compositor_spots_compose(const struct compositor_target *t, const struct compositor_frame *frames, unsigned int count)
{
        const struct compositor_chip *c = &compositor.chip;
        int radius = compositor_spot_radius(compositor.scale), box[4];

        for (unsigned int slot = 0; slot < UI_TOUCH_SLOTS; slot++)
                if (compositor.spot[slot].on)
                {
                        int sbox[4] = {compositor.spot[slot].x - radius, compositor.spot[slot].y - radius,
                                       compositor.spot[slot].x + radius, compositor.spot[slot].y + radius};
                        if (!compositor_overlay_compose(t, frames, count, sbox, compositor_spots_marks))
                                return;
                }
        if (c->kind == COMPOSITOR_CHIP_NONE || !c->alpha || !c->cover)
                return;
        compositor_chip_place(box);
        {
                int edge[4] = {box[0], box[1], box[0] + box[2], box[1] + box[3]};
                compositor_overlay_compose(t, frames, count, edge, compositor_spots_draw);
        }
}

static void compositor_cursor_overlay_draw(const struct compositor_target *square)
{
        if (compositor_spots_any(0))
        {
                compositor_spots_marks(square);
                if (compositor.chip.kind != COMPOSITOR_CHIP_NONE && compositor.chip.alpha && compositor.chip.cover)
                        compositor_chip_draw(square);
        }
        compositor_cursor_draw(square, compositor.cursor.x, compositor.cursor.y, &compositor.cursor.drawn_as,
                               compositor_cursor_ink(0, 0), compositor_cursor_ink(1, 0));
}

static void compositor_cursor_compose(const struct compositor_target *t, const struct compositor_frame *frames, unsigned int count)
{
        int hot_x, hot_y, span = compositor_pointer_span(&compositor.cursor.drawn_as, &hot_x, &hot_y);
        int box[4] = {compositor.cursor.x - hot_x, compositor.cursor.y - hot_y, compositor.cursor.x - hot_x + span, compositor.cursor.y - hot_y + span};

        if (compositor.cursor.drawn && !compositor_overlay_compose(t, frames, count, box, compositor_cursor_overlay_draw))
                compositor_cursor_draw(t, compositor.cursor.x, compositor.cursor.y, &compositor.cursor.drawn_as,
                                       compositor_cursor_ink(0, 0), compositor_cursor_ink(1, 0));
}

static void compositor_touch_settings_read(void)
{
        char word[16];
        long value;
        int on;

        compositor_word(SETTING_TOUCH_PATH, word, sizeof(word));
        compositor_setting_kept(SETTING_TOUCH_PATH, word, &value);
        on = value == 0;
        if (!on && compositor.spots_on)
                compositor_spots_clear();
        compositor.spots_on = on;
        compositor_setting_apply_binds(COMPOSITOR_SET_TOUCH);
}

static void compositor_touch_point(unsigned int at, int slot, int *x, int *y)
{
        const struct compositor_touch_screen *screen = &compositor.screen_touch[at];
        int sx, sy, width, height, out = 0;

        for (unsigned int k = 0; k < compositor.outputs; k++)
                if (compositor_builtin_panel(compositor.output[k].name)) { out = (int)k; break; }
        compositor_output_rect(out, &sx, &sy, &width, &height);
        *x = sx + compositor_tablet(screen->x[slot], screen->range[0], screen->range[1], width - 1);
        *y = sy + compositor_tablet(screen->y[slot], screen->range[2], screen->range[3], height - 1);
}

#ifndef COMPOSITOR_EVIOCGMTSLOTS
#define COMPOSITOR_EVIOCGMTSLOTS(bytes) (0x80000000u | (bytes) << 16 | 0x450au)
#endif

static b32 compositor_mt_slots(unsigned int at, int ids[UI_TOUCH_SLOTS], int xs[UI_TOUCH_SLOTS], int ys[UI_TOUCH_SLOTS], int *slot)
{
        struct { unsigned int code; int values[UI_TOUCH_SLOTS]; } asked;
        const unsigned int codes[3] = {COMPOSITOR_ABS_MT_TRACKING_ID, COMPOSITOR_ABS_MT_POSITION_X, COMPOSITOR_ABS_MT_POSITION_Y};
        int *into[3] = {ids, xs, ys};
        struct compositor_absinfo current = {0};

        for (int k = 0; k < UI_TOUCH_SLOTS; k++)
                ids[k] = -1;
        if (at >= COMPOSITOR_INPUTS || compositor.inputs[at] < 0)
                return false;
        for (unsigned int c = 0; c < 3; c++)
        {
                memory_zero(&asked, sizeof asked), asked.code = codes[c];
                if (system_control(compositor.inputs[at], COMPOSITOR_EVIOCGMTSLOTS((unsigned int)sizeof asked), &asked) != 0)
                {
                        for (int k = 0; k < UI_TOUCH_SLOTS; k++)
                                ids[k] = -1;
                        return false;
                }
                for (int k = 0; k < UI_TOUCH_SLOTS; k++)
                        into[c][k] = c == 0 && asked.values[k] < 0 ? -1 : asked.values[k];
        }
        if (slot && system_control(compositor.inputs[at], COMPOSITOR_EVIOCGABS(COMPOSITOR_ABS_MT_SLOT), &current) == 0)
                *slot = current.value;
        return true;
}

static void compositor_touch_event(unsigned int at, const struct compositor_report_event *event)
{
        struct compositor_touch_screen *screen = &compositor.screen_touch[at];
        int slot = screen->slots ? screen->slot : 0;

        if (screen->dropped)
                return;
        if (event->type == COMPOSITOR_EV_ABS && event->code == COMPOSITOR_ABS_MT_SLOT)
                screen->slot = event->value;
        else if (slot < 0 || slot >= UI_TOUCH_SLOTS)
                return;
        else if (event->type == COMPOSITOR_EV_ABS && screen->slots && event->code == COMPOSITOR_ABS_MT_TRACKING_ID)
                screen->id[slot] = event->value < 0 ? -1 : event->value;
        else if (event->type == COMPOSITOR_EV_ABS && event->code == (screen->slots ? COMPOSITOR_ABS_MT_POSITION_X : 0u))
                screen->x[slot] = event->value;
        else if (event->type == COMPOSITOR_EV_ABS && event->code == (screen->slots ? COMPOSITOR_ABS_MT_POSITION_Y : 1u))
                screen->y[slot] = event->value;
        else if (event->type == COMPOSITOR_EV_KEY && event->code == COMPOSITOR_BTN_TOUCH && !screen->slots)
                screen->id[0] = event->value ? 0 : -1;
}

static void compositor_touch_button(unsigned int code, int down)
{
        compositor.touching = 1, compositor_press(code, down), compositor.touching = 0;
}

static void compositor_touch_motion(int x, int y, positive now)
{
        compositor.touch.x = x, compositor.touch.y = y;
        compositor_predict_place(COMPOSITOR_PREDICT_TOUCH, x, y, now);
        compositor.kept_x = compositor.real_x, compositor.kept_y = compositor.real_y;
        compositor.touching = 1, compositor_motion(), compositor.touching = 0;
}

static void compositor_touch_resume(void)
{
        compositor.touch.resumed = 1, compositor_touch_button(COMPOSITOR_BTN_LEFT, 1), compositor.touch.resumed = 0;
}

static void compositor_touch_cancel(void)
{
        compositor.touching = 1, compositor_release_as(COMPOSITOR_BTN_LEFT, 0, WINDOW_KEY_CANCEL), compositor.touching = 0;
}

static void compositor_touch_right(void)
{
        compositor_touch_button(COMPOSITOR_BTN_RIGHT, 1), compositor_touch_button(COMPOSITOR_BTN_RIGHT, 0);
}

#define COMPOSITOR_PINCH_LEGACY_LOW 64
#define COMPOSITOR_PINCH_LEGACY_STEPS 16

static void compositor_pinch_legacy(int row, int scale)
{
        int steps = 0;

        while (steps++ < COMPOSITOR_PINCH_LEGACY_STEPS && (long)scale * 4 >= (long)compositor.touch.pinch_level * 5 &&
               compositor.touch.pinch_level < (1 << 20))
                compositor.touch.pinch_level = compositor.touch.pinch_level * 5 / 4, compositor_post(row, WINDOW_EVENT_WHEEL, WINDOW_KEY_CONTROL, 120, 0);
        while (steps++ < 2 * COMPOSITOR_PINCH_LEGACY_STEPS && (long)scale * 5 <= (long)compositor.touch.pinch_level * 4 &&
               compositor.touch.pinch_level > COMPOSITOR_PINCH_LEGACY_LOW)
                compositor.touch.pinch_level = compositor.touch.pinch_level * 4 / 5, compositor_post(row, WINDOW_EVENT_WHEEL, WINDOW_KEY_CONTROL, -120, 0);
}

static void compositor_pinch(const struct ui_touch_event *e)
{
        struct compositor_touch *touch = &compositor.touch;
        int row, x, y;
        unsigned int flags = e->kind == UI_TOUCH_PINCH_BEGIN ? WINDOW_PINCH_BEGIN : e->kind == UI_TOUCH_PINCH_END ? WINDOW_PINCH_END : 0;

        if (e->kind == UI_TOUCH_PINCH_BEGIN)
                touch->pinch_row = compositor_picture_under(e->x, e->y), touch->pinch_level = WINDOW_PINCH_ONE;
        row = touch->pinch_row;
        if (row >= 0 && compositor_visible(row))
        {
                compositor_window_point(row, e->x, e->y, &x, &y);
                x = (int)compositor_clamp64(x, 0, 0x7fff), y = (int)compositor_clamp64(y, 0, 0x7fff);
                compositor.touching = 1;
                if (__atomic_load_n(&compositor.table->pinch_wanted[row], __ATOMIC_ACQUIRE))
                        compositor_post(row, WINDOW_EVENT_PINCH, flags, e->scale, window_pinch_pack(x, y));
                else if (e->kind == UI_TOUCH_PINCH_UPDATE)
                        compositor_pinch_legacy(row, e->scale);
                compositor.touching = 0;
        }
        if (e->kind == UI_TOUCH_PINCH_END)
                touch->pinch_row = COMPOSITOR_NONE;
}

static void compositor_touch_grab_clear(unsigned int kind)
{
        if (compositor.active_grab.kind == kind)
                compositor_grab_clear();
}

static void compositor_touch_apply(unsigned int at, const struct ui_touch_out *out, positive now)
{
        int wheel = 0;

        for (unsigned int index = 0; index < out->count; index++)
        {
                const struct ui_touch_event *e = &out->event[index];

                switch (e->kind)
                {
                case UI_TOUCH_START:
                        compositor.touch.device = (int)at;
                        compositor.touch.physical = at < COMPOSITOR_INPUTS && compositor_input_physical[at];
                        compositor.touch.hidden = 1, compositor.touch.closing = compositor.touch.hold_row = COMPOSITOR_NONE;
                        compositor_touch_grab_clear(COMPOSITOR_GRAB_TOUCH_CLOSE), compositor_touch_grab_clear(COMPOSITOR_GRAB_TOUCH_HOLD);
                        compositor_touch_motion(e->x, e->y, now);
                        break;
                case UI_TOUCH_MOVE: compositor_touch_motion(e->x, e->y, now); break;
                case UI_TOUCH_PRESS: compositor_touch_button(COMPOSITOR_BTN_LEFT, 1); break;
                case UI_TOUCH_RELEASE:
                        compositor.touch.closing = COMPOSITOR_NONE, compositor_touch_grab_clear(COMPOSITOR_GRAB_TOUCH_CLOSE);
                        compositor_predict_clear(COMPOSITOR_PREDICT_TOUCH), compositor_touch_button(COMPOSITOR_BTN_LEFT, 0);
                        break;
                case UI_TOUCH_CANCEL: compositor_touch_cancel(); break;
                case UI_TOUCH_CLOSE:
                        compositor_predict_clear(COMPOSITOR_PREDICT_TOUCH);
                        if (compositor.touch.closing >= 0)
                                compositor.press = COMPOSITOR_NONE, compositor_close(compositor.touch.closing);
                        compositor.touch.closing = COMPOSITOR_NONE, compositor_touch_grab_clear(COMPOSITOR_GRAB_TOUCH_CLOSE);
                        break;
                case UI_TOUCH_RESUME:
                        compositor.touch.closing = COMPOSITOR_NONE, compositor_touch_grab_clear(COMPOSITOR_GRAB_TOUCH_CLOSE);
                        compositor_touch_resume();
                        break;
                case UI_TOUCH_RIGHT: compositor_touch_right(); break;
                case UI_TOUCH_WHEEL:
                        compositor.touching = 1, wheel = 1;
                        compositor_wheel(compositor.table && (compositor.table->scroll_off & WINDOW_SCROLL_FLIP) ? -e->v120 : e->v120, e->horizontal, WINDOW_WHEEL_PRECISE);
                        break;
                case UI_TOUCH_TURN_END: compositor_wheel(0, 0, WINDOW_WHEEL_PRECISE | WINDOW_WHEEL_STOP); break;
                case UI_TOUCH_HOLD:
                        {
                                int tab;

                                compositor_predict_place(COMPOSITOR_PREDICT_TOUCH, compositor.touch.x, compositor.touch.y, now);
                                compositor.touch.holding = 1, compositor_touch_cancel();
                                compositor.touch.hold_row = compositor_under(compositor.pointer_x, compositor.pointer_y, &tab);
                                if (compositor.touch.hold_row >= 0)
                                        compositor_grab_set(COMPOSITOR_GRAB_TOUCH_HOLD, compositor.touch.hold_row, COMPOSITOR_BTN_LEFT, 0);
                                compositor_touch_right(), compositor.touch.holding = 0;
                        }
                        break;
                case UI_TOUCH_HOLD_END:
                        if (compositor.touch.hold_row >= 0)
                                compositor_post(compositor.touch.hold_row, WINDOW_EVENT_BUTTON,
                                                WINDOW_KEY_TOUCH | WINDOW_KEY_CANCEL | WINDOW_KEY_HOLD, COMPOSITOR_BTN_LEFT, 0);
                        compositor.touch.hold_row = COMPOSITOR_NONE, compositor_touch_grab_clear(COMPOSITOR_GRAB_TOUCH_HOLD);
                        break;
                case UI_TOUCH_IDLE: compositor_predict_clear(COMPOSITOR_PREDICT_TOUCH); break;
                case UI_TOUCH_PINCH_BEGIN:
                case UI_TOUCH_PINCH_UPDATE:
                case UI_TOUCH_PINCH_END:
                        compositor_pinch(e);
                        break;
                }
        }
        if (wheel)
                compositor.touching = 0;
}

#ifdef CONFIG_MOONWATER_WAYLAND
static void compositor_wl_touch_report(unsigned int at, int dropped);
#endif

static int compositor_swipe_screen(int x, int y, int *rect, int *number)
{
        unsigned int count;
        const int (*screens)[4] = compositor_screens(&count);

        for (unsigned int at = 0; at < count; at++)
                if (x >= screens[at][0] && x < screens[at][0] + screens[at][2] && y >= screens[at][1] && y < screens[at][1] + screens[at][3])
                        return memory_copy(rect, screens[at], 4 * sizeof(int)), *number = (int)at, 1;
        return 0;
}

static int compositor_swipe_landing(unsigned int at, positive now, const struct ui_touch_frame *frame, int slot)
{
        struct compositor_swipe *swipe = &compositor.swipe;
        int x = frame->x[slot], y = frame->y[slot], rect[4] = {0, 0, 0, 0}, number = -1, row = COMPOSITOR_NONE, side = UI_EDGE_NONE;
        int band = UI_EDGE_BAND * compositor.scale / 100, cell = compositor_chip_cell(x, y);

        if (cell >= 0)
                side = UI_EDGE_CONTROL;
        else
        {
                int tab = TASKBAR_HIT_NONE, under, on = 0;

                if (!compositor_chip_on(x, y))
                        compositor_chip_hide();
                if (!compositor_swipe_screen(x, y, rect, &number))
                        return 0;
                side = ui_edge_band_side(x, y, rect, band);
                under = compositor_under(x, y, &tab);
                if (tab == TASKBAR_HIT_NONE && under >= 0 && y >= compositor.y[under] && y - compositor.y[under] < band)
                {
                        compositor.touching = 1;
                        if (!compositor_edge_zone(under, x, y))
                                side = UI_EDGE_TOP;
                        compositor.touching = 0;
                }
                if (side == UI_EDGE_LEFT || side == UI_EDGE_RIGHT)
                {
                        if ((side == UI_EDGE_LEFT ? compositor.side_left : compositor.side_right) == COMPOSITOR_SIDE_OFF)
                                return 0;
                }
                else if (side != UI_EDGE_TOP && side != UI_EDGE_BOTTOM)
                        return 0;
                if (compositor.drag >= 0 || compositor.resize >= 0 || tab != TASKBAR_HIT_NONE)
                        return 0;
                if (under >= 0)
                        compositor.touching = 1,
                        on = compositor_on_button(under, x, y, compositor_close_box) || compositor_on_button(under, x, y, compositor_minimize_box),
                        compositor.touching = 0;
                if (on || (side == UI_EDGE_TOP && under < 0))
                        return 0;
                row = under;
        }
        swipe->state = COMPOSITOR_SWIPE_UNDECIDED, swipe->device = (int)at, swipe->slot = slot;
        swipe->side = side, swipe->cell = cell, swipe->row = row, swipe->screen = number;
        memory_copy(swipe->rect, rect, sizeof(rect));
        swipe->x0 = x, swipe->y0 = y, swipe->deadline = now + (positive)ui_hold_ns;
        memory_copy(&swipe->landing, frame, sizeof(*frame));
        return 1;
}

static void compositor_touch_feed(unsigned int at, positive now, const struct ui_touch_frame *landing, struct ui_touch_frame *report)
{
        struct ui_touch_out out;

        ui_touch_begin(&compositor.touch.gesture, landing, &out);
        compositor_touch_apply(at, &out, now);
        report->pointer_x = compositor.pointer_x, report->pointer_y = compositor.pointer_y;
        report->busy = compositor.drag >= 0 || compositor.resize >= 0;
        ui_touch_report(&compositor.touch.gesture, report, &out);
        compositor_touch_apply(at, &out, now);
#ifdef CONFIG_MOONWATER_WAYLAND
        compositor_wl_touch_report(at, 0);
#endif
}

static void compositor_swipe_hand(unsigned int at, positive now, const struct ui_touch_frame *frame)
{
        struct ui_touch_frame report;

        compositor.swipe.state = COMPOSITOR_SWIPE_NONE;
        memory_copy(&report, frame, sizeof(report));
        compositor_touch_feed(at, now, &compositor.swipe.landing, &report);
}

static void compositor_swipe_act(unsigned int at, positive now, const struct ui_touch_frame *frame)
{
        struct compositor_swipe *swipe = &compositor.swipe;
        int row = swipe->row, thick = taskbar_thick(compositor.scale), span = COMPOSITOR_SWIPE_LAUNCHER_DP * compositor.scale / 100;

        if (row >= 0 && compositor.fullscreen[row] && (swipe->armed_row != row || now >= swipe->armed_until))
        {
                swipe->armed_row = row, swipe->armed_until = now + COMPOSITOR_SWIPE_ARM_NS;
                compositor_chip_hint(swipe->rect, swipe->x0, swipe->y0, swipe->side, now);
                compositor_swipe_hand(at, now, frame);
                return;
        }
        swipe->armed_row = COMPOSITOR_NONE;
        if (swipe->side == UI_EDGE_TOP)
        {
                if (!compositor_chip_controls(row, now)) { compositor_swipe_hand(at, now, frame); return; }
                compositor_raise(row), compositor_focus(row);
        }
        else if (swipe->side == UI_EDGE_BOTTOM)
                compositor_table_button_set(swipe->x0 - span / 2, swipe->rect[1] + swipe->rect[3] - thick, span, thick, TASKBAR_BOTTOM, (unsigned int)swipe->screen),
                compositor.launcher_asked++, compositor_script_say("launcher_button");
        else if ((swipe->side == UI_EDGE_LEFT ? compositor.side_left : compositor.side_right) == COMPOSITOR_SIDE_WINDOWS)
                compositor_focus_step(), compositor_focus_commit();
        else
        {
                int right = swipe->side == UI_EDGE_RIGHT;
                compositor_table_button_set(right ? swipe->rect[0] + swipe->rect[2] - thick : swipe->rect[0], swipe->y0 - span / 2, thick, span,
                                            right ? TASKBAR_RIGHT : TASKBAR_LEFT, (unsigned int)swipe->screen);
                if (!right)
                        compositor.launcher_asked++;
                compositor_script_say(right ? "volume_button" : "launcher_button");
        }
        swipe->state = COMPOSITOR_SWIPE_SWIPED;
}

static int compositor_swipe_report(unsigned int at, positive now, const struct ui_touch_frame *frame)
{
        struct compositor_swipe *swipe = &compositor.swipe;
        int other = -1, count = ui_touch_fingers(frame, -1, &other), verdict;

        if (swipe->state == COMPOSITOR_SWIPE_SWIPED)
                return !count ? (void)(swipe->state = COMPOSITOR_SWIPE_NONE) : (void)0, 1;
        if (swipe->state == COMPOSITOR_SWIPE_UNDECIDED)
        {
                if (!count)
                {
                        if (swipe->side == UI_EDGE_CONTROL)
                        {
                                int cell = swipe->cell, row = compositor.chip.row;

                                swipe->state = COMPOSITOR_SWIPE_NONE, compositor_chip_hide();
                                if (row >= 0 && row < COMPOSITOR_ROWS && compositor_visible(row))
                                        cell == 0 ? compositor_close(row) : cell == 1 ? compositor_minimize_row(row)
                                        : compositor_filling(row) ? compositor_float(row) : compositor_arrange(row, COMPOSITOR_MAXIMIZED);
                        }
                        else
                                compositor_swipe_hand(at, now, frame);
                        return 1;
                }
                if (count >= 2 || frame->id[swipe->slot] < 0 || frame->id[swipe->slot] != swipe->landing.id[swipe->slot])
                        return compositor_swipe_hand(at, now, frame), 1;
                verdict = ui_edge_judge(swipe->side, swipe->x0, swipe->y0, frame->x[swipe->slot], frame->y[swipe->slot],
                                        ui_touch_slop(compositor.scale), UI_EDGE_SWIPE * compositor.scale / 100);
                if (verdict == UI_EDGE_UNDECIDED && now >= swipe->deadline)
                        verdict = UI_EDGE_FAILED;
                if (verdict == UI_EDGE_FAILED)
                        compositor_swipe_hand(at, now, frame);
                else if (verdict == UI_EDGE_SWIPED)
                        compositor_swipe_act(at, now, frame);
                return 1;
        }
        if (count != 1 || compositor.touch.gesture.mode != UI_TOUCH_NONE)
                return 0;
        return compositor_swipe_landing(at, now, frame, other);
}

static void compositor_touch_report(unsigned int at, positive now)
{
        struct compositor_touch_screen *screen = &compositor.screen_touch[at];
        struct compositor_touch *touch = &compositor.touch;
        struct ui_touch_frame frame;
        struct ui_touch_out out;

        WINDOW_TRACE("compositor report: device %b, gesture mode %b, dropped %b\n", (bipolar)at, (bipolar)touch->gesture.mode, (bipolar)screen->dropped);
        for (int slot = 0; slot < UI_TOUCH_SLOTS; slot++)
                if (screen->id[slot] >= 0)
                {
                        int x, y;
                        compositor_touch_point(at, slot, &x, &y);
                        WINDOW_TRACE("compositor finger: slot %b tracking id %b px %b,%b\n", (bipolar)slot, (bipolar)screen->id[slot], (bipolar)x, (bipolar)y);
                }
        if (screen->dropped)
        {
#ifdef CONFIG_MOONWATER_WAYLAND
                compositor_wl_touch_report(at, 1);
#endif
                screen->dropped = 0;
                if (compositor.swipe.state != COMPOSITOR_SWIPE_NONE && compositor.swipe.device == (int)at)
                        compositor.swipe.state = COMPOSITOR_SWIPE_NONE;
                for (int slot = 0; slot < UI_TOUCH_SLOTS; slot++)
                {
                        if (screen->id[slot] >= 0)
                                compositor_spot_lift(slot, now);
                        screen->id[slot] = screen->was[slot] = -1;
                }
                if (touch->gesture.mode != UI_TOUCH_NONE && touch->device == (int)at)
                        ui_touch_drop(&touch->gesture, &out), compositor_touch_apply(at, &out, now);
                if (screen->slots)
                {
                        int ids[UI_TOUCH_SLOTS], xs[UI_TOUCH_SLOTS], ys[UI_TOUCH_SLOTS];
                        if (compositor_mt_slots(at, ids, xs, ys, &screen->slot))
                                for (int slot = 0; slot < UI_TOUCH_SLOTS; slot++)
                                        screen->id[slot] = ids[slot], screen->x[slot] = xs[slot], screen->y[slot] = ys[slot];
                }
                return;
        }
        if ((touch->gesture.mode != UI_TOUCH_NONE && touch->device != (int)at) ||
            (compositor.swipe.state != COMPOSITOR_SWIPE_NONE && compositor.swipe.device != (int)at))
                { memory_copy(screen->was, screen->id, sizeof(screen->was)); return; }
        memory_zero(&frame, sizeof(frame));
        memory_copy(frame.id, screen->id, sizeof(frame.id)), memory_copy(frame.was, screen->was, sizeof(frame.was));
        for (int slot = 0; slot < UI_TOUCH_SLOTS; slot++)
                if (screen->id[slot] >= 0)
                        compositor_touch_point(at, slot, &frame.x[slot], &frame.y[slot]), compositor_spot_set(slot, frame.x[slot], frame.y[slot]);
                else if (screen->was[slot] >= 0)
                        compositor_spot_lift(slot, now);
        frame.scale = compositor.scale, frame.now = now, frame.pointer_x = compositor.pointer_x, frame.pointer_y = compositor.pointer_y;
        frame.busy = compositor.drag >= 0 || compositor.resize >= 0;
        if (!compositor_swipe_report(at, now, &frame))
                compositor_touch_feed(at, now, &frame, &frame);
        memory_copy(screen->was, screen->id, sizeof(screen->was));
}

static positive compositor_touch_due(void)
{
        positive due = ui_touch_due(&compositor.touch.gesture, compositor.button_down & 1);

        if (compositor.swipe.state == COMPOSITOR_SWIPE_UNDECIDED && (!due || compositor.swipe.deadline < due))
                due = compositor.swipe.deadline;
        return due;
}

static void compositor_touch_tick(positive now)
{
        struct ui_touch_out out;
        int was;

        if (compositor.swipe.state == COMPOSITOR_SWIPE_UNDECIDED && now >= compositor.swipe.deadline)
                { compositor_swipe_hand((unsigned int)compositor.swipe.device, now, &compositor.swipe.landing); return; }
        was = compositor_post_physical;
        ui_touch_tick(&compositor.touch.gesture, now, compositor.button_down & 1, &out);
        compositor_post_physical = was || compositor.touch.physical;
        compositor_touch_apply((unsigned int)compositor.touch.device, &out, now);
        compositor_post_physical = was;
}

/* ---- SYN_DROPPED recovery, real-time reader thread, and evdev dispatch ---- */

#define COMPOSITOR_INPUT_READS 16
#define COMPOSITOR_EVIOCGKEY(bytes) (0x80000000u | (bytes) << 16 | 0x4518u)
#define COMPOSITOR_KEY_BYTES 96
static void compositor_input_read_device(unsigned int at);

static b32 compositor_input_down(const unsigned char *bits, unsigned int code)
{
        return code < COMPOSITOR_KEY_BYTES * 8 && (bits[code >> 3] >> (code & 7) & 1);
}

static void compositor_input_state(unsigned int at, unsigned char *bits)
{
        memory_zero(bits, COMPOSITOR_KEY_BYTES);
        if (at >= COMPOSITOR_INPUTS || compositor.inputs[at] < 0 || compositor.devices[at].wakeonly ||
            system_control(compositor.inputs[at], COMPOSITOR_EVIOCGKEY(COMPOSITOR_KEY_BYTES), bits) != 0)
                { memory_zero(bits, COMPOSITOR_KEY_BYTES); return; }
        if (compositor.touchpad[at] && compositor_input_down(bits, COMPOSITOR_BTN_LEFT) && compositor_pad_of(at)->clicked)
        {
                unsigned int clicked = compositor_pad_of(at)->clicked;
                bits[COMPOSITOR_BTN_LEFT >> 3] &= (unsigned char)~(1u << (COMPOSITOR_BTN_LEFT & 7));
                bits[clicked >> 3] |= (unsigned char)(1u << (clicked & 7));
        }
        if (!compositor.touchpad[at] && compositor_input_down(bits, COMPOSITOR_BTN_TOUCH))
                bits[COMPOSITOR_BTN_LEFT >> 3] |= (unsigned char)(1u << (COMPOSITOR_BTN_LEFT & 7));
}

static void compositor_input_dropped(unsigned int at, positive now)
{
        unsigned char any[COMPOSITOR_KEY_BYTES], one[COMPOSITOR_KEY_BYTES];

        memory_zero(any, sizeof any);
        for (unsigned int device = 0; device < compositor.input_count && device < COMPOSITOR_INPUTS; device++)
        {
                compositor_input_state(device, one);
                for (unsigned int k = 0; k < COMPOSITOR_KEY_BYTES; k++)
                        any[k] |= one[k];
                for (unsigned int code = 0; code < 256 && compositor.held[device]; code++)
                {
                        unsigned int bit = compositor_held_bit(code);
                        if (bit && (compositor.held[device] & bit) && !compositor_input_down(one, code))
                                compositor_key(device, code, 0, now);
                }
                for (unsigned int code = 0; code < 256; code++)
                        if (compositor_held_bit(code) && compositor_input_down(one, code))
                                compositor.held[device] |= (unsigned char)compositor_held_bit(code);
        }
        for (unsigned int code = 0; code < 256; code++)
                if ((compositor.keys_down[code >> 3] >> (code & 7) & 1) && !compositor_input_down(any, code))
                        compositor_key(at, code, 0, now);
        for (unsigned int index = 0; index < 8; index++)
                if ((compositor.button_down >> index & 1) && !compositor_input_down(any, COMPOSITOR_BTN_LEFT + index))
                        compositor_release(COMPOSITOR_BTN_LEFT + index, 0);
        if ((compositor.drag >= 0 || compositor.resize >= 0) && !compositor_input_down(any, COMPOSITOR_BTN_LEFT) &&
            !compositor_input_down(any, COMPOSITOR_BTN_RIGHT))
                compositor_release(COMPOSITOR_BTN_LEFT, 0);
}

#define COMPOSITOR_READER_RING 8192
#define COMPOSITOR_READER_PRIORITY 50
#define COMPOSITOR_READER_NOTE_NS 50000000ull
#define COMPOSITOR_READER_NOTE_DELAY_NS 16000000ull
#define COMPOSITOR_READER_GONE 0xffffu
#define COMPOSITOR_READER_NOTE 0xfffeu
#define COMPOSITOR_READER_UNWATCH -1

struct compositor_reader_record
{
        struct compositor_input_event raw;
        unsigned int at, generation;
        unsigned long long read, sent;
};

static int compositor_input_take(unsigned int at, const struct compositor_input_event *raw, bipolar got, int moved);
static void compositor_input_gone(unsigned int at);
static void compositor_input_end(unsigned int at, int moved);

static struct
{
        int on, knock, wake, set;
        unsigned int changes, knocked, dropped, gone_owing;
        unsigned long long handed, reading, handed_out, reading_out, taken, direct, noted, note_at, late, late_most, keys, note_due, note_read, note_sent;
        int down[256], own[COMPOSITOR_INPUTS];
        unsigned int generation[COMPOSITOR_INPUTS], gone_owed[COMPOSITOR_INPUTS];
        struct compositor_reader_record ring[COMPOSITOR_READER_RING];
} compositor_reader = {0, -1, -1, -1};

static int compositor_reader_dup(unsigned int at)
{
        int fd = at < compositor.input_count ? compositor.inputs[at] : -1;
        return fd >= 0 ? (int)compositor_call(syscall(fcntl), (positive)fd, 1030, 0, 0, 0, 0) : -1;
}

static void compositor_reader_changed(unsigned int at)
{
        unsigned long long one = 1;

        if (at < COMPOSITOR_INPUTS && compositor_reader.on)
        {
                int own = compositor_reader_dup(at);
                int old = __atomic_exchange_n(&compositor_reader.own[at], own >= 0 ? own + 1 : COMPOSITOR_READER_UNWATCH, __ATOMIC_ACQ_REL);
                if (old > 0)
                        system_close(old - 1);
        }
        if (at < COMPOSITOR_INPUTS)
                __atomic_add_fetch(&compositor_reader.generation[at], 1, __ATOMIC_RELEASE);
        __atomic_add_fetch(&compositor_reader.changes, 1, __ATOMIC_RELEASE);
        if (compositor_reader.on)
                system_write_once(compositor_reader.wake, &one, sizeof one);
}

static int compositor_reader_running(void) { return compositor_reader.on; }

#ifdef CONFIG_MOONWATER_WAYLAND
static int compositor_wl_direct_send(long dx, long dy, positive micro);
static int compositor_wl_direct_clean(void);
static int compositor_wl_direct_key(unsigned int code, int pressed, unsigned long *serial, int *row, int to);
static void compositor_wl_key_noted(unsigned int at, unsigned int code, int value, unsigned long serial, int row, positive stamp, positive read, positive sent);
static void compositor_wl_direct_publish(void);
#endif

static void compositor_reader_knock(void)
{
        unsigned long long one = 1;

        if (!__atomic_exchange_n(&compositor_reader.knocked, 1u, __ATOMIC_SEQ_CST))
                system_write_once(compositor_reader.knock, &one, sizeof one);
}

static int compositor_reader_full(void)
{
        return compositor_reader.handed - __atomic_load_n(&compositor_reader.reading, __ATOMIC_ACQUIRE) +
               (compositor_reader.dropped ? 2 : 1) > COMPOSITOR_READER_RING;
}

static void compositor_reader_put(unsigned int at, unsigned int generation, const struct compositor_input_event *raw)
{
        unsigned long long handed = compositor_reader.handed;
        struct compositor_reader_record *record;

        if (compositor_reader_full())
        {
                if (raw->type == COMPOSITOR_READER_GONE && at < COMPOSITOR_INPUTS)
                        compositor_reader.gone_owing += !compositor_reader.gone_owed[at], compositor_reader.gone_owed[at] = generation + 1;
                else
                        compositor_reader.dropped = at + 1;
                return;
        }
        if (compositor_reader.dropped)
        {
                record = &compositor_reader.ring[handed++ % COMPOSITOR_READER_RING];
                memory_zero(record, sizeof *record);
                record->raw.type = COMPOSITOR_EV_SYN, record->raw.code = COMPOSITOR_SYN_DROPPED;
                record->at = compositor_reader.dropped - 1, record->generation = __atomic_load_n(&compositor_reader.generation[record->at], __ATOMIC_ACQUIRE);
                compositor_reader.dropped = 0, compositor_reader.handed_out++;
        }
        record = &compositor_reader.ring[handed++ % COMPOSITOR_READER_RING];
        record->raw = *raw, record->at = at, record->generation = generation;
        record->read = raw->type == COMPOSITOR_READER_NOTE ? compositor_reader.note_read : 0;
        record->sent = raw->type == COMPOSITOR_READER_NOTE ? compositor_reader.note_sent : 0;
        compositor_reader.handed_out += raw->type != COMPOSITOR_READER_NOTE;
        __atomic_store_n(&compositor_reader.handed, handed, __ATOMIC_SEQ_CST);
}

#ifdef CONFIG_MOONWATER_WAYLAND
static int compositor_reader_key(unsigned int at, unsigned int code, int value, positive micro)
{
        struct compositor_input_event note = {0, 0, COMPOSITOR_READER_NOTE, 0, 0};
        unsigned long serial = 0;
        int row = -1;

        if (code >= 256 || (!value && !compositor_reader.down[code]))
                return 0;
        if (value == 2)
                return compositor_reader.down[code] != 0;
        compositor_reader.note_read = clock_monotonic_nanoseconds();
        if (!compositor_wl_direct_key(code, value, &serial, &row, value ? -1 : compositor_reader.down[code] - 1))
                return compositor_reader.down[code] = 0, 0;
        compositor_reader.note_sent = clock_monotonic_nanoseconds();
        compositor_reader.down[code] = value ? row + 1 : 0, compositor_reader.keys++;
        note.seconds = (long)serial, note.microseconds = (long)micro, note.code = (unsigned short)code, note.value = value;
        compositor_reader_put(at, (unsigned int)row, &note);
        if (value == 1 && __atomic_load_n(&compositor_keytrace.until, __ATOMIC_RELAXED))
                return compositor_reader_knock(), 1;
        if (!compositor_reader.note_due)
                compositor_reader.note_due = clock_monotonic_nanoseconds() + COMPOSITOR_READER_NOTE_DELAY_NS;
        return 1;
}

static void compositor_reader_direct_stat(positive now_us, positive micro)
{
        positive late = now_us - micro;

        __atomic_add_fetch(&compositor_reader.direct, 1, __ATOMIC_RELEASE);
        if (late > compositor_reader.late_most && late < 1000000000ull)
                compositor_reader.late_most = late;
        compositor_reader.late += late >= 100;
}
#endif

static int compositor_reader_direct(unsigned int at, const struct compositor_input_event *raw, unsigned int count)
{
#ifdef CONFIG_MOONWATER_WAYLAND
        long dx = 0, dy = 0;
        int key = -1, value = 0;
        positive micro;

        if (compositor.touchpad[at] || compositor.touchscreen[at] || compositor.devices[at].wakeonly || __atomic_load_n(&compositor.dark, __ATOMIC_RELAXED) ||
            __atomic_load_n(&compositor_reader.taken, __ATOMIC_ACQUIRE) != compositor_reader.handed_out || compositor_reader.dropped)
                return 0;
        for (unsigned int k = 0; k < count; k++)
                if (raw[k].type == COMPOSITOR_EV_REL && raw[k].code == COMPOSITOR_REL_X)
                        dx += raw[k].value;
                else if (raw[k].type == COMPOSITOR_EV_REL && raw[k].code == COMPOSITOR_REL_Y)
                        dy += raw[k].value;
                else if (raw[k].type == COMPOSITOR_EV_KEY && raw[k].code < 256 && key < 0)
                        key = raw[k].code, value = raw[k].value;
                else if (raw[k].type != 4u && !(raw[k].type == COMPOSITOR_EV_SYN && raw[k].code == 0))
                        return 0;
        micro = (positive)raw[count - 1].seconds * 1000000ull + (positive)raw[count - 1].microseconds;
        if (key >= 0)
                return !dx && !dy && compositor_reader_key(at, (unsigned int)key, value, micro);
        if ((!dx && !dy) || !compositor_wl_direct_send(dx, dy, micro))
                return 0;
        compositor_reader_direct_stat(clock_monotonic_nanoseconds() / 1000, micro);
        return 1;
#else
        (void)at, (void)raw, (void)count;
        return 0;
#endif
}

static void compositor_reader_sync(int *watched, unsigned int *generations, unsigned int *count)
{
        unsigned int devices = __atomic_load_n(&compositor.input_count, __ATOMIC_ACQUIRE);

        for (unsigned int at = 0; at < COMPOSITOR_INPUTS && (at < devices || at < *count); at++)
        {
                int fd = __atomic_exchange_n(&compositor_reader.own[at], 0, __ATOMIC_ACQ_REL) - 1;

                if (fd == -1)
                        continue;
                if (watched[at] >= 0)
                        system_call_4(syscall(epoll_ctl), (positive)compositor_reader.set, 2, (positive)watched[at], 0), system_close(watched[at]);
                watched[at] = -1, generations[at] = __atomic_load_n(&compositor_reader.generation[at], __ATOMIC_ACQUIRE);
                if (fd >= 0)
                {
                        struct compositor_epoll_event in = {COMPOSITOR_POLLIN, at};
                        if (system_call_4(syscall(epoll_ctl), (positive)compositor_reader.set, 1, (positive)fd, (positive)&in) == 0)
                                watched[at] = fd;
                        else
                                system_close(fd);
                }
        }
        *count = devices > *count ? devices : *count;
}

static b32 compositor_reader_owed(void)
{
        for (unsigned int at = 0; compositor_reader.gone_owing && at < COMPOSITOR_INPUTS; at++)
                if (compositor_reader.gone_owed[at])
                {
                        struct compositor_input_event gone = {0, 0, COMPOSITOR_READER_GONE, 0, 0};

                        if (compositor_reader_full())
                                break;
                        compositor_reader_put(at, compositor_reader.gone_owed[at] - 1, &gone);
                        compositor_reader.gone_owed[at] = 0, compositor_reader.gone_owing--;
                        compositor_reader_knock();
                }
        return compositor_reader.gone_owing != 0;
}

static unsigned int compositor_spark_want;

static unsigned int compositor_reader_spark_drain(struct spark_input_ring *ring, positive *last_ns)
{
        unsigned long long tail, head;
        unsigned int count = 0, forwarded = 0;
        long async_dx = 0, async_dy = 0;

        if (!ring)
                return 0;
        tail = ring->tail, head = __atomic_load_n(&ring->head, __ATOMIC_ACQUIRE);
        while (tail != head)
        {
                const struct spark_input_slot *s = &ring->slots[tail & ring->mask];
                int dx = s->dx, dy = s->dy;
                positive micro = s->time_us;

                __atomic_store_n(&ring->tail, ++tail, __ATOMIC_RELEASE);
                if (dx || dy)
                {
                        positive now = clock_monotonic_nanoseconds();
                        *last_ns = now;
#ifdef CONFIG_MOONWATER_WAYLAND
                        if (compositor_wl_direct_send(dx, dy, micro))
                                { compositor_reader_direct_stat(now / 1000, micro), count++; continue; }
#endif
                        struct compositor_input_event ev = {(long)(micro / 1000000ull), (long)(micro % 1000000ull), COMPOSITOR_EV_REL, 0, 0};
                        if (dx)
                                ev.code = COMPOSITOR_REL_X, ev.value = dx, compositor_reader_put(COMPOSITOR_INPUTS, 0, &ev);
                        if (dy)
                                ev.code = COMPOSITOR_REL_Y, ev.value = dy, compositor_reader_put(COMPOSITOR_INPUTS, 0, &ev);
                        ev.type = COMPOSITOR_EV_SYN, ev.code = 0, ev.value = 0;
                        compositor_reader_put(COMPOSITOR_INPUTS, 0, &ev);
                        async_dx += dx, async_dy += dy, forwarded = 1;
                }
                count++;
        }
        if (async_dx || async_dy)
                compositor_cursor_async_step(async_dx, async_dy, *last_ns);
        if (forwarded)
                compositor_reader_knock();
        return count;
}

static struct spark_input_ring *compositor_reader_spark_open(void)
{
        int fd = (int)system_open_at(AT_FDCWD, (string_address)SPARK_DEVICE, FILE_READ_WRITE | O_CLOEXEC);
        struct compositor_epoll_event ev = {COMPOSITOR_POLLIN, COMPOSITOR_INPUTS + 1};
        long mapped;

        if (fd < 0)
                return 0;
        mapped = (long)system_call_6(syscall(mmap), 0, SPARK_INPUT_PAGE_BYTES, 3, 1, (positive)fd, SPARK_INPUT_MMAP_PGOFF * SPARK_INPUT_PAGE_BYTES);
        if (!(mapped < 0 && mapped > -4096) &&
            ((struct spark_input_ring *)mapped)->magic == SPARK_INPUT_MAGIC &&
            ((struct spark_input_ring *)mapped)->version == SPARK_INPUT_VERSION &&
            ((struct spark_input_ring *)mapped)->mask == SPARK_INPUT_SLOTS - 1u &&
            system_call_4(syscall(epoll_ctl), (positive)compositor_reader.set, 1, (positive)fd, (positive)&ev) == 0)
                return __atomic_store_n(&((struct spark_input_ring *)mapped)->active, 1u, __ATOMIC_RELEASE), (struct spark_input_ring *)mapped;
        if (!(mapped < 0 && mapped > -4096))
                system_call_2(syscall(munmap), (positive)mapped, SPARK_INPUT_PAGE_BYTES);
        return system_close(fd), (struct spark_input_ring *)0;
}

static fn compositor_reader_run(address_any context)
{
        static int watched[COMPOSITOR_INPUTS];
        static unsigned int generations[COMPOSITOR_INPUTS];
        unsigned int count = 0, changes = ~0u;
        int priority = COMPOSITOR_READER_PRIORITY;
        struct spark_input_ring *spark_ring;
        positive spark_last = 0;

        (void)context;
        compositor_call(syscall(sched_setscheduler), 0, 1, (positive)&priority, 0, 0, 0);
        compositor_pin_ccd0();
        for (unsigned int at = 0; at < COMPOSITOR_INPUTS; at++)
                watched[at] = -1;
        spark_ring = compositor_reader_spark_open();
        for (;;)
        {
                struct compositor_epoll_event ready[16];
                long got, wait[2] = {0, 0};
                positive now;
                b32 owed;

                if (spark_ring)
                {
                        if (!__atomic_load_n(&spark_ring->active, __ATOMIC_RELAXED))
                                __atomic_store_n(&spark_ring->active, 1u, __ATOMIC_RELEASE);
                        compositor_reader_spark_drain(spark_ring, &spark_last);
                        if (spark_last && (now = clock_monotonic_nanoseconds()) - spark_last < 500000ull)
                                for (positive stop = now + 15000ull; clock_monotonic_nanoseconds() < stop;)
                                {
                                        if (__atomic_load_n(&spark_ring->head, __ATOMIC_ACQUIRE) != spark_ring->tail)
                                                compositor_reader_spark_drain(spark_ring, &spark_last), stop = clock_monotonic_nanoseconds() + 15000ull;
#if defined(__x86_64__)
                                        __asm__ volatile("pause" ::: "memory");
#elif defined(__aarch64__)
                                        __asm__ volatile("yield" ::: "memory");
#endif
                                }
                }
                if (__atomic_load_n(&compositor_reader.changes, __ATOMIC_ACQUIRE) != changes)
                        changes = __atomic_load_n(&compositor_reader.changes, __ATOMIC_ACQUIRE), compositor_reader_sync(watched, generations, &count);
                now = compositor_reader.note_due ? clock_monotonic_nanoseconds() : 0, owed = compositor_reader_owed();
                if (compositor_reader.note_due && now >= compositor_reader.note_due)
                        compositor_reader.note_due = 0, compositor_reader_knock();
                if (compositor_reader.note_due)
                        wait[1] = (long)(compositor_reader.note_due - now);
                if (owed && (!compositor_reader.note_due || wait[1] > 1000000))
                        wait[1] = 1000000;
                got = (long)system_call_6(syscall(epoll_pwait2), (positive)compositor_reader.set, (positive)ready, 16,
                                          compositor_reader.note_due || owed ? (positive)wait : 0, 0, 0);
                for (long k = 0; k < got; k++)
                {
                        unsigned int at = (unsigned int)ready[k].data, put = 0;
                        struct compositor_input_event raw[64];

                        if (at == COMPOSITOR_INPUTS + 1) { compositor_reader_spark_drain(spark_ring, &spark_last); continue; }
                        if (at >= COMPOSITOR_INPUTS) { unsigned long long word; system_read_once(compositor_reader.wake, &word, sizeof word); continue; }
                        for (unsigned int reads = 0; watched[at] >= 0 && reads < COMPOSITOR_INPUT_READS; reads++)
                        {
                                bipolar bytes = system_read_once(watched[at], raw, sizeof raw);
                                unsigned int records = bytes > 0 ? (unsigned int)(bytes / (bipolar)sizeof raw[0]) : 0, from = 0;
                                long async_dx = 0, async_dy = 0;
                                int is_mouse = !compositor.touchpad[at] && !compositor.touchscreen[at] && !compositor.devices[at].wakeonly;

                                if (bytes < 0 && bytes != -COMPOSITOR_EAGAIN && bytes != -4)
                                {
                                        struct compositor_input_event gone = {0, 0, COMPOSITOR_READER_GONE, 0, 0};
                                        system_call_4(syscall(epoll_ctl), (positive)compositor_reader.set, 2, (positive)watched[at], 0);
                                        system_close(watched[at]), watched[at] = -1;
                                        compositor_reader_put(at, generations[at], &gone), put++;
                                        break;
                                }
                                for (unsigned int k2 = 0; k2 < records; k2++)
                                        if (raw[k2].type == COMPOSITOR_EV_SYN && raw[k2].code == 0)
                                        {
                                                if (!compositor_reader_direct(at, raw + from, k2 + 1 - from))
                                                        for (unsigned int r = from; r <= k2; r++)
                                                        {
                                                                if (is_mouse && raw[r].type == COMPOSITOR_EV_REL)
                                                                        async_dx += raw[r].code == COMPOSITOR_REL_X ? raw[r].value : 0,
                                                                        async_dy += raw[r].code == COMPOSITOR_REL_Y ? raw[r].value : 0;
                                                                compositor_reader_put(at, generations[at], &raw[r]), put++;
                                                        }
                                                from = k2 + 1;
                                        }
                                if (async_dx || async_dy)
                                        compositor_cursor_async_step(async_dx, async_dy, clock_monotonic_nanoseconds());
                                for (unsigned int r = from; r < records; r++)
                                        compositor_reader_put(at, generations[at], &raw[r]), put++;
                                if (bytes < (bipolar)sizeof raw)
                                        break;
                        }
                        if (put)
                                compositor_reader.note_due = 0, compositor_reader_knock();
                }
                if (__atomic_load_n(&compositor_reader.direct, __ATOMIC_ACQUIRE) != __atomic_load_n(&compositor_reader.noted, __ATOMIC_ACQUIRE) &&
                    (now = clock_monotonic_nanoseconds()) - compositor_reader.note_at >= COMPOSITOR_READER_NOTE_NS)
                        compositor_reader.note_at = now, compositor_reader_knock();
        }
}

static int compositor_reader_start(void)
{
        struct compositor_epoll_event wake = {COMPOSITOR_POLLIN, COMPOSITOR_INPUTS};

        compositor_reader.knock = (int)compositor_call(syscall(eventfd2), 0, 0x80000 | 0x800, 0, 0, 0, 0);
        compositor_reader.wake = (int)compositor_call(syscall(eventfd2), 0, 0x80000 | 0x800, 0, 0, 0, 0);
        compositor_reader.set = (int)system_call_1(syscall(epoll_create1), 0x80000);
        if (compositor_reader.knock < 0 || compositor_reader.wake < 0 || compositor_reader.set < 0 ||
            system_call_4(syscall(epoll_ctl), (positive)compositor_reader.set, 1, (positive)compositor_reader.wake, (positive)&wake) != 0)
                goto none;
        for (unsigned int at = 0; at < COMPOSITOR_INPUTS; at++)
        {
                int own = compositor_reader_dup(at);
                compositor_reader.own[at] = own >= 0 ? own + 1 : 0;
        }
        compositor_reader.on = 1;
        if (thread_start(compositor_reader_run, null))
                return 1;
        compositor_reader.on = 0;
        for (unsigned int at = 0; at < COMPOSITOR_INPUTS; at++)
                if (compositor_reader.own[at] > 0)
                        system_close(compositor_reader.own[at] - 1), compositor_reader.own[at] = 0;
none:
        if (compositor_reader.knock >= 0) system_close(compositor_reader.knock);
        if (compositor_reader.wake >= 0) system_close(compositor_reader.wake);
        if (compositor_reader.set >= 0) system_close(compositor_reader.set);
        compositor_reader.knock = compositor_reader.wake = compositor_reader.set = -1;
        return 0;
}

static void compositor_reader_take(void)
{
        unsigned long long word, at = compositor_reader.reading, handed, direct;
        struct compositor_input_event raw[64];

        system_read_once(compositor_reader.knock, &word, sizeof word);
        __atomic_store_n(&compositor_reader.knocked, 0u, __ATOMIC_SEQ_CST);
        handed = __atomic_load_n(&compositor_reader.handed, __ATOMIC_SEQ_CST);
        while (at < handed)
        {
                const struct compositor_reader_record *first = &compositor_reader.ring[at % COMPOSITOR_READER_RING];
                unsigned int device = first->at, generation = first->generation, count = 0;
                int moved = 0, is_spark = device == COMPOSITOR_INPUTS;

#ifdef CONFIG_MOONWATER_WAYLAND
                if (first->raw.type == COMPOSITOR_READER_NOTE)
                {
                        compositor_wl_key_noted(first->at, first->raw.code, first->raw.value, (unsigned long)first->raw.seconds, (int)first->generation,
                                                (positive)first->raw.microseconds * 1000ull, first->read, first->sent);
                        at++;
                        continue;
                }
#endif
                while (at < handed && count < 64)
                {
                        const struct compositor_reader_record *record = &compositor_reader.ring[at % COMPOSITOR_READER_RING];

                        if (record->at != device || record->generation != generation || record->raw.type == COMPOSITOR_READER_NOTE)
                                break;
                        raw[count++] = record->raw, at++, compositor_reader.reading_out++;
                        if (record->raw.type == COMPOSITOR_READER_GONE)
                                break;
                }
                if (!is_spark && (device >= compositor.input_count || compositor.inputs[device] < 0 ||
                                  generation != __atomic_load_n(&compositor_reader.generation[device], __ATOMIC_ACQUIRE)))
                        continue;
                compositor_post_physical = is_spark || (device < COMPOSITOR_INPUTS && compositor_input_physical[device]);
                if (!is_spark && raw[count - 1].type == COMPOSITOR_READER_GONE)
                {
                        if (count > 1)
                                moved = compositor_input_take(device, raw, (bipolar)((count - 1) * sizeof raw[0]), 0);
                        compositor_input_gone(device);
                }
                else
                        moved = compositor_input_take(device, raw, (bipolar)(count * sizeof raw[0]), 0);
                compositor_input_end(device, moved);
                compositor_post_physical = 0;
                if (!is_spark && compositor.inputs[device] < 0)
                        compositor_input_physical[device] = 0;
        }
        __atomic_store_n(&compositor_reader.reading, at, __ATOMIC_RELEASE);
        if ((direct = __atomic_load_n(&compositor_reader.direct, __ATOMIC_ACQUIRE)) != compositor_reader.noted)
        {
                __atomic_store_n(&compositor_reader.noted, direct, __ATOMIC_RELEASE);
#ifdef CONFIG_MOONWATER_WAYLAND
                compositor_wl_input();
#endif
#ifdef MOONWATER_GPU_C
                compositor_gpu_wake();
#endif
                compositor_hold_touch(clock_monotonic_nanoseconds());
        }
}

static void compositor_reader_settle(void)
{
        if (!compositor_reader.on)
                return;
#ifdef CONFIG_MOONWATER_WAYLAND
        compositor_wl_direct_publish();
        if (!compositor_wl_direct_clean())
                return;
#endif
        __atomic_store_n(&compositor_reader.taken, compositor_reader.reading_out, __ATOMIC_RELEASE);
}

static void compositor_input_read(unsigned int at)
{
        compositor_post_physical = at < COMPOSITOR_INPUTS && compositor_input_physical[at];
        compositor_input_read_device(at);
        compositor_post_physical = 0;
        if (at < COMPOSITOR_INPUTS && compositor.inputs[at] < 0)
                compositor_input_physical[at] = 0;
}

static void compositor_input_read_device(unsigned int at)
{
        struct compositor_input_event raw[64];
        int moved = 0;

        for (unsigned int reads = 0; reads < COMPOSITOR_INPUT_READS; reads++)
        {
                bipolar got = system_read_once(compositor.inputs[at], raw, sizeof(raw));

                if (got < 0 && got != -COMPOSITOR_EAGAIN && got != -4)
                        compositor_input_gone(at);
                if (got <= 0)
                        break;
                moved = compositor_input_take(at, raw, got, moved);
                if (got < (bipolar)sizeof(raw))
                        break;
        }
        compositor_input_end(at, moved);
}

static void compositor_input_gone(unsigned int at)
{
        compositor_sling_only = 0;
        system_close(compositor.inputs[at]), compositor.inputs[at] = -1, compositor.table->input_number[at] = -1;
        if ((unsigned int)compositor.devices[at].number < COMPOSITOR_DEVICES)
                compositor.device_open[compositor.devices[at].number / 64] &= ~(1ul << (compositor.devices[at].number % 64));
        compositor.held[at] = 0;
        if (compositor.input_kinds[at] & WINDOWS_INPUT_KEYBOARD)
                compositor_keys_release();
        if (compositor.input_kinds[at] & (WINDOWS_INPUT_MOUSE | WINDOWS_INPUT_TABLET | WINDOWS_INPUT_BUTTONS |
                                          WINDOWS_INPUT_TOUCH | WINDOWS_INPUT_FINGERS | WINDOWS_INPUT_DIRECT))
        {
                compositor.cursor.held = 0, compositor_release(COMPOSITOR_BTN_LEFT, 0);
                for (unsigned int index = 1; index < 8; index++)
                        if (compositor.button_down >> index & 1)
                                compositor_release(COMPOSITOR_BTN_LEFT + index, 0);
        }
        if (compositor.touchscreen[at] && compositor.touch.gesture.mode != UI_TOUCH_NONE && compositor.touch.device == (int)at)
        {
                if (compositor.touch.gesture.mode == UI_TOUCH_HELD && compositor.touch.hold_row >= 0)
                        compositor_post(compositor.touch.hold_row, WINDOW_EVENT_BUTTON,
                                        WINDOW_KEY_TOUCH | WINDOW_KEY_CANCEL | WINDOW_KEY_HOLD, COMPOSITOR_BTN_LEFT, 0);
                compositor.touch.hold_row = COMPOSITOR_NONE;
                compositor_touch_grab_clear(COMPOSITOR_GRAB_TOUCH_HOLD), compositor_touch_grab_clear(COMPOSITOR_GRAB_TOUCH_CLOSE);
                ui_touch_reset(&compositor.touch.gesture);
        }
        if (compositor.touchscreen[at])
                for (int slot = 0; slot < UI_TOUCH_SLOTS; slot++)
                        compositor_spot_lift(slot, clock_monotonic_nanoseconds());
        compositor_device_reset(at), compositor_reader_changed(at);
}

static void compositor_input_wheel(unsigned int at, unsigned int code, int value, positive now)
{
        int fine = code == COMPOSITOR_REL_WHEEL_HI || code == COMPOSITOR_REL_HWHEEL_HI;
        int sideways = code == COMPOSITOR_REL_HWHEEL || code == COMPOSITOR_REL_HWHEEL_HI;

        compositor.table->input_wheel_read[at]++, compositor.table->input_wheel_last[at] = now;
        if (!fine && (compositor.hires[at] >> sideways & 1))
                return;
        int stream = compositor_wheel_streams(at, now);
        long long value120 = fine ? (long long)value : (long long)value * 120;
        unsigned int flags = stream ? (WINDOW_WHEEL_PRECISE | WINDOW_WHEEL_STREAM) : 0;

        if (compositor.table)
                window_wheel_note(compositor.table, at, now, stream);
        compositor.table->input_wheel_used[at]++;
        if (stream)
                value120 = compositor_stream_turn(at, value120, compositor_stream_gain_at(at, now), sideways);
        compositor_wheel(compositor_counts(0, value120), sideways, flags);
}

static void compositor_input_pad_drop(unsigned int at)
{
        struct compositor_pad *pad = compositor_pad_of(at);
        unsigned char bits[COMPOSITOR_KEY_BYTES];
        int ids[UI_TOUCH_SLOTS];

        pad->moving = 0, pad->left_x = pad->left_y = 0;
        compositor_mt_slots(at, ids, pad->mt_x, pad->mt_y, &pad->slot);
        pad->slot = (int)compositor_clamp64(pad->slot, 0, UI_TOUCH_SLOTS - 1);
        for (int slot = 0; slot < UI_TOUCH_SLOTS; slot++)
                pad->mt_down[slot] = ids[slot] >= 0;
        compositor_input_state(at, bits);
        pad->touching = compositor_input_down(bits, COMPOSITOR_BTN_TOUCH), pad->fingers = 0;
        for (int k = 4; k >= 0; k--)
                if (compositor_input_down(bits, compositor_pad_tools[k])) { pad->fingers = k + 1; break; }
}

static int compositor_input_take(unsigned int at, const struct compositor_input_event *raw, bipolar got, int moved)
{
        struct compositor_report_event events[64];
        positive count;
        int is_spark = at >= COMPOSITOR_INPUTS;

#ifdef MOONWATER_GPU_C
        if (is_spark || !compositor.devices[at].wakeonly)
                compositor_gpu_wake();
#endif
        if (!is_spark && compositor.devices[at].wakeonly)
                return compositor_sling_only = 0, moved;
        count = evdev_decode((const p8 *)raw, (positive)got, (p64 *)events, 64);
        for (positive i = 0; i < count; i++)
        {
                const struct compositor_report_event *event = &events[i];
                positive now = event->stamp;
                int pad = !is_spark && compositor.touchpad[at], touch = !is_spark && compositor.touchscreen[at];

                if (compositor.dark && ((event->type == 1 && event->value == 1) || event->type == 2 || event->type == 3))
                        compositor_dark_woken();
                compositor_stamp_now = now;
                if (touch || pad || !compositor_wl_locked() ||
                    !((event->type == COMPOSITOR_EV_REL && event->code < 2) || (event->type == COMPOSITOR_EV_SYN && event->code == 0) || event->type == 4u))
                        compositor_sling_only = 0;
#ifdef CONFIG_MOONWATER_WAYLAND
                if (event->type == COMPOSITOR_EV_KEY || event->type == COMPOSITOR_EV_REL || event->type == COMPOSITOR_EV_ABS)
                        compositor_wl_input();
#endif
                if (touch || pad)
                {
                        if ((event->type == COMPOSITOR_EV_KEY && event->code == COMPOSITOR_BTN_TOUCH && event->value) ||
                            (event->type == COMPOSITOR_EV_ABS && event->code == COMPOSITOR_ABS_MT_TRACKING_ID && event->value >= 0))
                                window_trace_touch();
                        WINDOW_TRACE_START();
                        WINDOW_TRACE_PART("compositor event: monotonic %p device %p ", (positive)clock_monotonic_nanoseconds(), now);
                        window_trace_word((const char address_to)compositor.devices[at].name);
                        WINDOW_TRACE_PART(" type %b code %b value %b", (bipolar)event->type, (bipolar)event->code, (bipolar)event->value);
                        WINDOW_TRACE_END();
                }
                if ((event->type == COMPOSITOR_EV_REL && event->code < 2) || event->type == COMPOSITOR_EV_ABS)
                        compositor.motion_ts = compositor.motion_ts ? compositor.motion_ts : now;
                if (touch)
                {
                        if (event->type == COMPOSITOR_EV_SYN && event->code == 0)
                                compositor_touch_report(at, now);
                        else if (event->type == COMPOSITOR_EV_SYN && event->code == COMPOSITOR_SYN_DROPPED)
                                compositor.table->input_dropped[at]++, compositor.screen_touch[at].dropped = 1;
                        else
                                compositor_touch_event(at, event);
                        continue;
                }
                switch (event->type)
                {
                case COMPOSITOR_EV_REL:
                        if (event->code == COMPOSITOR_REL_X)
                        {
                                compositor.raw_x = compositor_counts(compositor.raw_x, event->value);
                                if (compositor_shaken(event->value, now))
                                        compositor.cursor.magnify = 1;
                        }
                        else if (event->code == COMPOSITOR_REL_Y)
                                compositor.raw_y = compositor_counts(compositor.raw_y, event->value);
                        else if (event->code == COMPOSITOR_REL_WHEEL || event->code == COMPOSITOR_REL_HWHEEL ||
                                 event->code == COMPOSITOR_REL_WHEEL_HI || event->code == COMPOSITOR_REL_HWHEEL_HI)
                                compositor_input_wheel(at, event->code, event->value, now);
                        break;
                case COMPOSITOR_EV_SYN:
                        if (event->code == COMPOSITOR_SYN_DROPPED)
                        {
                                compositor.table->input_dropped[at]++;
                                if (pad)
                                        compositor_input_pad_drop(at);
                                compositor_input_dropped(at, now);
                        }
                        else if (event->code == 0)
                        {
                                if (pad)
                                        compositor_pad_apply(at, now);
                                moved |= compositor_report(now, now);
                        }
                        break;
                case COMPOSITOR_EV_ABS:
                        if (pad && event->code >= COMPOSITOR_ABS_MT_SLOT && event->code <= COMPOSITOR_ABS_MT_TRACKING_ID)
                                compositor_pad_slot(compositor_pad_of(at), event->code, event->value);
                        else if (event->code < 2 && pad)
                                *(event->code ? &compositor_pad_of(at)->next_y : &compositor_pad_of(at)->next_x) = event->value;
                        else if (event->code < 2)
                        {
                                int *range = event->code ? compositor.absolute_y[at] : compositor.absolute_x[at];
                                *(event->code ? &compositor.tablet_y : &compositor.tablet_x) =
                                    compositor_tablet(event->value, range[0], range[1], event->code ? compositor.height : compositor.width);
                                compositor.tablet_have |= event->code ? 2 : 1;
                        }
                        break;
                case COMPOSITOR_EV_KEY:
                        if (pad && compositor_pad_finger_key(event->code))
                        {
                                struct compositor_pad *p = compositor_pad_of(at);
                                b32 down = p->touching || p->fingers;

                                compositor_pad_key(p, event->code, event->value);
                                if (down && !p->touching && !p->fingers)
                                        (p->turned ? compositor_wheel(0, 0, WINDOW_WHEEL_PRECISE | WINDOW_WHEEL_STOP) : (void)0), p->turned = 0;
                        }
                        else if (event->code == COMPOSITOR_BTN_TOOL_PEN && !event->value && !pad)
                                compositor_predict_clear(COMPOSITOR_PREDICT_PEN);
                        else if ((event->code >= COMPOSITOR_BTN_LEFT && event->code <= COMPOSITOR_BTN_TASK) || event->code == COMPOSITOR_BTN_TOUCH)
                        {
                                unsigned int code = event->code == COMPOSITOR_BTN_TOUCH ? COMPOSITOR_BTN_LEFT : event->code;

                                if (pad && code == COMPOSITOR_BTN_LEFT)
                                {
                                        struct compositor_pad *p = compositor_pad_of(at);
                                        if (event->value)
                                                p->clicked = p->fingers >= 2 ? COMPOSITOR_BTN_RIGHT : COMPOSITOR_BTN_LEFT;
                                        code = p->clicked ? p->clicked : code;
                                }
                                if ((moved |= compositor_report(now, now)))
                                        compositor_motion(), moved = 0;
                                compositor_press(code, event->value != 0);
                        }
                        else if (event->code < 256)
                        {
                                if (event->value == 1 && (compositor.input_kinds[at] & WINDOWS_INPUT_KEYBOARD))
                                        compositor.osk_typed = 1;
                                compositor_key(at, event->code, event->value, now);
                        }
                        break;
                }
        }
        return moved;
}

static void compositor_input_end(unsigned int at, int moved)
{
        if (moved)
                compositor_motion();
        compositor_stamp_now = 0;
        if (at >= COMPOSITOR_INPUTS || !compositor.devices[at].wakeonly)
                compositor_hold_touch(clock_monotonic_nanoseconds());
}
