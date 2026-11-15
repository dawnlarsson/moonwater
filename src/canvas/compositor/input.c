// How long a report took to be on its way to the screen or the window, which `moonwater latency`
// of the kernel desktop was for: a count, a total and the worst, in nanoseconds.
static void compositor_latency(unsigned int which, positive reported, positive now)
{
        positive took = reported && now > reported ? now - reported : 0;

        compositor.latency[which].count++;
        compositor.latency[which].total += took;
        if (took > compositor.latency[which].worst)
                compositor.latency[which].worst = took;
}

/*
        How long the processor is allowed to take waking up. The largest cost
        from a pointer moving to the program that draws it is the processor
        coming back from an idle state, and the deeper the state the longer
        that takes, so while a person is at the machine it is asked to stay
        out of them: a zero in /dev/cpu_dma_latency, which holds for as long
        as the file is open, taken after the first thing they do (the first
        pays the wakeup either way: the processor was idle when the interrupt
        came) and let go of half a second after the last. What counts is what
        a hand or a key did and not a program's commit, so a clock ticking in
        a window does not keep it. A request of zero is a processor polling,
        which to cpufreq is busy, so it is not held a moment longer than that.
*/
static void compositor_hold_touch(positive now)
{
        int zero = 0;

        compositor.hold_until = now + UI_HOLD_NS;
        if (compositor.hold >= 0 || compositor.holds == ~0ul)
                return;

        compositor.hold = (int)system_open_at(AT_FDCWD, (string_address) "/dev/cpu_dma_latency", O_WRONLY | O_CLOEXEC);
        if (compositor.hold < 0)
        {
                // Not there on this machine: it is not asked for again.
                compositor.hold = -1;
                compositor.holds = ~0ul;
                return;
        }

        system_write_once(compositor.hold, &zero, sizeof(zero));
        compositor.holds++;
}

static void compositor_hold_drop(positive now)
{
        if (compositor.hold < 0 || now < compositor.hold_until)
                return;

        system_close(compositor.hold);
        compositor.hold = -1;
}

/*
        The desktop's keys, which are the machine script's: a function there
        named key_ and a chord -- modifiers and a key joined by _, as
        key_ctrl_shift_t -- is run when the chord is pressed. The script
        writes its key_ functions' names to /run/moonwater/keys, a line each,
        before it starts this program; a chord pressed is no window's key, and
        its name is written, a line, to the FIFO /run/moonwater/keys.fire,
        which the script reads and runs. Everything on the desktop is started
        by the machine script and by nothing else; this process forks nothing.
        The keys are the physical ones, so a chord is the same key whatever
        the layout; the modifiers are ctrl (or control), shift, alt and super.
*/
#define COMPOSITOR_KEYS 32
#define COMPOSITOR_KEYS_FILE "/run/moonwater/keys"
#define COMPOSITOR_KEYS_FIRE "/run/moonwater/keys.fire"
static const char *compositor_keys_fire = COMPOSITOR_KEYS_FIRE; // and a check's own

static const struct
{
        const char *name;
        unsigned char code;
} compositor_key_names[] = {
    {"escape", 1}, {"1", 2}, {"2", 3}, {"3", 4}, {"4", 5}, {"5", 6}, {"6", 7}, {"7", 8}, {"8", 9}, {"9", 10},
    {"0", 11}, {"minus", 12}, {"equal", 13}, {"backspace", 14}, {"tab", 15}, {"q", 16}, {"w", 17}, {"e", 18},
    {"r", 19}, {"t", 20}, {"y", 21}, {"u", 22}, {"i", 23}, {"o", 24}, {"p", 25}, {"leftbrace", 26},
    {"rightbrace", 27}, {"enter", 28}, {"a", 30}, {"s", 31}, {"d", 32}, {"f", 33}, {"g", 34}, {"h", 35},
    {"j", 36}, {"k", 37}, {"l", 38}, {"semicolon", 39}, {"apostrophe", 40}, {"grave", 41}, {"backslash", 43},
    {"z", 44}, {"x", 45}, {"c", 46}, {"v", 47}, {"b", 48}, {"n", 49}, {"m", 50}, {"comma", 51}, {"dot", 52},
    {"slash", 53}, {"space", 57}, {"f1", 59}, {"f2", 60}, {"f3", 61}, {"f4", 62}, {"f5", 63}, {"f6", 64},
    {"f7", 65}, {"f8", 66}, {"f9", 67}, {"f10", 68}, {"f11", 87}, {"f12", 88}, {"home", 102}, {"up", 103},
    {"pageup", 104}, {"left", 105}, {"right", 106}, {"end", 107}, {"down", 108}, {"pagedown", 109},
    {"insert", 110}, {"delete", 111},
};

static struct
{
        unsigned int count;
        unsigned short modifiers[COMPOSITOR_KEYS];
        unsigned char code[COMPOSITOR_KEYS];
        char chord[COMPOSITOR_KEYS][32];
} compositor_keys;

// One word of a chord, as a modifier's flag (and *code untouched) or a key's code (and 0); ~0u for neither.
static unsigned int compositor_key_word(const char *word, unsigned int length, unsigned int *code)
{
        static const struct { const char *name; unsigned int flag; } modifiers[] = {
            {"control", WINDOW_KEY_CONTROL}, {"ctrl", WINDOW_KEY_CONTROL}, {"shift", WINDOW_KEY_SHIFT},
            {"alt", WINDOW_KEY_ALT}, {"super", WINDOW_KEY_SUPER}};

        for (unsigned int at = 0; at < sizeof(modifiers) / sizeof(modifiers[0]); at++)
                if (string_length((string_address)modifiers[at].name) == length &&
                    !memory_compare(modifiers[at].name, word, length))
                        return modifiers[at].flag;
        for (unsigned int at = 0; at < sizeof(compositor_key_names) / sizeof(compositor_key_names[0]); at++)
                if (string_length((string_address)compositor_key_names[at].name) == length &&
                    !memory_compare(compositor_key_names[at].name, word, length))
                {
                        *code = compositor_key_names[at].code;
                        return 0;
                }
        return ~0u;
}

// The chords the script bound, read once at the start: a line that is not one is left out, and said.
static void compositor_keys_read(void)
{
        static char text[4096];
        bipolar handle = system_open_at(AT_FDCWD, (string_address)COMPOSITOR_KEYS_FILE, COMPOSITOR_SETTING_OPEN);
        bipolar got = handle < 0 ? 0 : system_read_once(handle, text, sizeof(text) - 1);
        unsigned int at = 0;

        if (handle >= 0)
                system_close(handle);
        text[got > 0 ? got : 0] = 0;

        while (text[at] && compositor_keys.count < COMPOSITOR_KEYS)
        {
                unsigned int start = at, close, modifiers = 0, code = 0, bad = 0;

                while (text[at] && text[at] != '\n')
                        at++;
                close = at;
                if (text[at])
                        at++;
                while (start < close && (text[start] == ' ' || text[start] == '\t'))
                        start++;
                while (close > start && (text[close - 1] == ' ' || text[close - 1] == '\t' || text[close - 1] == '\r'))
                        close--;
                if (close == start)
                        continue;

                // key_, then the words.
                bad = close - start < 5 || memory_compare(text + start, "key_", 4);
                for (unsigned int word = start + 4; word < close && !bad;)
                {
                        unsigned int stop = word, flag;

                        while (stop < close && text[stop] != '_')
                                stop++;
                        flag = compositor_key_word(text + word, stop - word, &code);
                        bad = flag == ~0u;
                        modifiers |= bad ? 0 : flag;
                        word = stop + 1;
                }

                // Named by the function, which is what the script's author looks for: there is no chord in it.
                if (bad || !code || close - start >= sizeof(compositor_keys.chord[0]))
                {
                        p8 name[48];
                        positive length = close - start < sizeof(name) - 1 ? close - start : sizeof(name) - 1;

                        memory_copy(name, text + start, length);
                        name[length] = 0;
                        compositor_kmsg((string_address) "compositor: no chord in the machine script's ", 0, (string_address)name);
                        continue;
                }

                memory_copy(compositor_keys.chord[compositor_keys.count], text + start, close - start);
                compositor_keys.chord[compositor_keys.count][close - start] = 0;
                compositor_keys.code[compositor_keys.count] = (unsigned char)code;
                compositor_keys.modifiers[compositor_keys.count++] = (unsigned short)modifiers;
        }
}

// A name said to the script on the FIFO, a line: a chord pressed, or a thing done to the desktop that the script
// starts a program for (tray_double, launcher_button). A script that does not listen hears nothing.
static void compositor_script_say(const char *name)
{
        bipolar handle;
        char line[COMPOSITOR_DOOR_LINE];
        positive length = string_length((string_address)name);
        unsigned int head = compositor_doors.line_head;

        if (length > sizeof(line) - 2)
                length = sizeof(line) - 2;
        // Said by the doors' thread, which may wait on the FIFO's lock (compositor_doors); a ring that is full drops the line.
        if (compositor_doors.worker[COMPOSITOR_DOOR_MISC])
        {
                if (head - __atomic_load_n(&compositor_doors.line_tail, __ATOMIC_ACQUIRE) >= 16)
                        return;
                memory_copy(compositor_doors.lines[head % 16], name, length);
                compositor_doors.lines[head % 16][length] = '\n';
                compositor_doors.lines[head % 16][length + 1] = 0;
                __atomic_store_n(&compositor_doors.line_head, head + 1, __ATOMIC_RELEASE);
                compositor_door_ask(COMPOSITOR_DOOR_MISC, COMPOSITOR_DOOR_LINES);
                return;
        }
        // Not through a link: the FIFO is the machine script's, and a link in its place would have this write into what it names.
        handle = system_open_at(AT_FDCWD, (string_address)compositor_keys_fire, O_WRONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
        if (handle < 0)
        {
                compositor_kmsg((string_address) "compositor: nothing reads /run/moonwater/keys.fire, error ", (positive)-handle,
                                (string_address) "");
                return;
        }
        memory_copy(line, name, length);
        line[length] = '\n';
        system_write_once(handle, line, length + 1);
        system_close(handle);
}


/*
        The sound server's master volume on the desktop: the keys (mute, volume down and volume up, five percent a press and a
        held key repeats), the wheel over the tray's volume cell and a middle press on it. The compositor reads the server's page
        (src/audio/state.c, mapped read-only) for the volume and for whether there is a server at all, which the tray's cell
        shows (and shows only then), and it changes the volume at once over the server's socket (control.c: SET_SINK_VOLUME and the
        mute, the sink by the name the page gives it) and as the settings do, /root/audio.volume and /root/audio.mute replaced whole,
        which keeps it across a restart and is all there is when the socket does not answer. What it shows of its own change it shows at once, as the
        number for a moment, and the page is not believed for a moment after, so the cell does not go back to the old value before
        the server has the new. A left press on the cell is the script's (volume_button): the mixer is a program, and everything on
        the desktop is started by the machine script.
*/
#define COMPOSITOR_VOLUME_STEP 5
#define COMPOSITOR_VOLUME_WAIT_MS 20           // the most the desktop waits for the server's answer: a key is not to hold the compositor
#define COMPOSITOR_VOLUME_AGAIN_NS 2000000000ull // a server that did not answer is tried again no sooner than this
#define COMPOSITOR_VOLUME_LOOK_NS 500000000ull // the page is looked at this often while there is a server,
#define COMPOSITOR_VOLUME_FIND_NS 2000000000ull // and this often for one to start
#define COMPOSITOR_VOLUME_FLASH_NS 1500000000ull
#define COMPOSITOR_VOLUME_HOLD_NS 800000000ull
#define COMPOSITOR_VOLUME_STALE_NS 4000000000ull // a page that has not moved for this long is looked for again by its name

// The page, the two files and the socket, which a check points at files of its own.
static const char *compositor_volume_path[5] = {AUDIO_STATE_PATH, SETTING_AUDIO_VOLUME_PATH, SETTING_AUDIO_MUTE_PATH, CTL_SOCKET, SETTING_AUDIO_SOURCE_MUTE_PATH};

// The connection to the server's socket (src/audio/control.c), made at the first change and kept; one that failed is tried again later.
static struct ctl compositor_ctl;
static positive compositor_ctl_again;

static struct
{
        const volatile struct audio_state *page;
        struct audio_state copy;
        positive look_at, flash_until, hold_until, seen_at, mic_flash_until;
        unsigned int seen_seq;
        int percent, muted, present;
        int recording, mic_muted; // programs that record from a microphone (not what plays), and whether the microphone is muted
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
        fd = compositor_call(syscall(openat), (positive)AT_FDCWD, (positive)compositor_volume_path[0], COMPOSITOR_SETTING_OPEN, 0, 0, 0);
        if (fd < 0)
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

/*
        The volume cell pressed: the popup is started by the machine script (volume_button), which starts the mixer in its popup form
        (-q -a) beside the cell, as the launcher is beside its button; the table holds the cell's rectangle, the edge its band is along
        and its screen before the script is told. A popup that is up is put away by this very press (WINDOW_DISMISS), and the cell then
        asks for none, so the cell opens it and closes it.
*/
// The first window showing whose name is word (the table's name, whole), or COMPOSITOR_NONE.
static int compositor_row_named(const char *word)
{
        for (unsigned int row = 0; row < WINDOWS; row++)
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

static int compositor_quick_row(void)
{
        return compositor_row_named("mixer-quick");
}

// The table holds the rectangle of the band's cell at a point (which is the hit taskbar_hit_reach names, and taskbar_anchor anchors),
// its edge and its screen, for the script to start the popup beside it. Nothing when no cell of that kind is there.
static void compositor_button_rect(int x, int y, int which)
{
        struct windows *table = compositor.table;
        int reach = compositor.touching ? COMPOSITOR_FINGER_TARGET * compositor.scale / 100 : 0;

        for (compositor.band_at = 0; table && compositor.band_at < compositor_bands(); compositor.band_at++)
        {
                struct taskbar_view view = compositor_band_view();
                int rect[4];

                if (taskbar_hit_reach(&view, x, y, reach) != which || !taskbar_anchor(&view, which, &rect[0], &rect[1], &rect[2], &rect[3]))
                        continue;
                table->button_x = rect[0], table->button_y = rect[1];
                table->button_w = (unsigned int)rect[2], table->button_h = (unsigned int)rect[3];
                table->button_edge = view.position, table->button_screen = (unsigned int)compositor.band_at;
                break;
        }
        compositor.band_at = 0;
}

static void compositor_volume_open(int x, int y)
{
        if (compositor_quick_row() >= 0)
                return;
        compositor_button_rect(x, y, TASKBAR_VOLUME);
        compositor_script_say("volume_button");
}

// The microphone cell pressed: the same popup form of the mixer (mic_button, which adds -i) with the recording strips in it, beside the cell.
static void compositor_mic_open(int x, int y)
{
        if (compositor_quick_row() >= 0)
                return;
        compositor_button_rect(x, y, TASKBAR_MIC);
        compositor_script_say("mic_button");
}

/*
        The notices dot follows the board: the board's sequence number is looked at each pass, and the board is copied only when it moved (the
        feeder publishes once a second while a kernel notice stands, so that is a copy a second and a redraw only when the dot changes). Answers
        1 when the dot changed, and the tabs are then drawn again.
*/
static struct windows_issues compositor_notices_copy;
static unsigned int compositor_notices_seq;
static int compositor_notices_look(void)
{
        unsigned int seq, count = 0, worst;

        if (!compositor.table)
                return 0;
        seq = __atomic_load_n(&compositor.table->issue_seq, __ATOMIC_ACQUIRE);
        if (seq == compositor_notices_seq || (seq & 1u))
                return 0;
        if (!windows_issues_read(compositor.table, &compositor_notices_copy))
                return 0;
        compositor_notices_seq = seq;
        worst = windows_issues_summary(&compositor_notices_copy, &count);
        return taskbar_notices_set(count > 0, worst);
}

// What the tray's cells show, told to the band; the band is drawn again when one changed. The microphone's is there while a program records
// from a microphone, and for a moment after its mute was thrown (to see it thrown).
static void compositor_volume_show(void)
{
        int changed = taskbar_volume_set(compositor_volume.present, compositor_volume.percent, compositor_volume.muted, compositor_volume.flash_until != 0);

        changed |= taskbar_mic_set(compositor_volume.present && (compositor_volume.recording || compositor_volume.mic_flash_until), compositor_volume.mic_muted);
        if (changed)
                compositor_tabs_changed();
}

// The notices dot pressed: the script is told (notices_button) and starts the notices popup beside the dot, which lists the board and starts
// Settings on its System notices page. A popup already up is put away by the press before this runs, so none is started twice.
static void compositor_notices_open(int x, int y)
{
        if (compositor_row_named("notices") >= 0)
                return;
        compositor_button_rect(x, y, TASKBAR_NOTICES);
        compositor_script_say("notices_button");
}

// The page looked at: whether there is a server, and the volume and the mute it says (not for a moment after our own change).
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

                        compositor_volume.present = 1;
                        compositor_volume.percent = percent > 100 ? 100 : (int)percent;
                        compositor_volume.muted = copy->muted != 0;
                        // A program that records from a microphone: a record stream that is not paused and does not record what plays, while recording is allowed.
                        compositor_volume.recording = 0;
                        for (unsigned int k = 0; k < AUDIO_STATE_RECORDS; k++)
                                compositor_volume.recording += copy->capture && copy->record[k].index != 0xffffffffu && !(copy->record[k].flags & (AUDIO_STATE_MONITOR | AUDIO_STATE_CORKED)) &&
                                                               (copy->record[k].flags & AUDIO_STATE_PLAYING);
                        compositor_volume.mic_muted = copy->input_muted != 0;
                        if (copy->seq != compositor_volume.seen_seq)
                                compositor_volume.seen_seq = copy->seq, compositor_volume.seen_at = now;
                        // A server that was started again has a page of its own: this one is let go when it stands still.
                        if (now - compositor_volume.seen_at > COMPOSITOR_VOLUME_STALE_NS)
                        {
                                compositor_volume_unmap();
                                compositor_volume.seen_at = now;
                        }
                }
        }
        compositor_volume.look_at = now + (compositor_volume.present ? COMPOSITOR_VOLUME_LOOK_NS : COMPOSITOR_VOLUME_FIND_NS);
        if (compositor_volume.flash_until && compositor_volume.flash_until < compositor_volume.look_at)
                compositor_volume.look_at = compositor_volume.flash_until;
        if (compositor_volume.mic_flash_until && compositor_volume.mic_flash_until < compositor_volume.look_at)
                compositor_volume.look_at = compositor_volume.mic_flash_until;
        compositor_volume_show();
}

// A small file of /root replaced whole: written beside it and renamed over it.
static void compositor_volume_write(const char *path, const char *text)
{
        char stage[48];
        positive length = string_length((string_address)path), size = string_length((string_address)text);
        bipolar handle, written;

        if (length + sizeof(".new") > sizeof(stage))
                return;
        memory_copy(stage, path, length);
        memory_copy(stage + length, ".new", sizeof(".new"));
        handle = system_open_at_mode(AT_FDCWD, (string_address)stage, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0644);
        if (handle < 0)
                return;
        written = system_write_once(handle, text, size);
        system_close(handle);
        if (written != (bipolar)size || system_rename_at(AT_FDCWD, (string_address)stage, AT_FDCWD, (string_address)path, 0) < 0)
                system_remove_at(AT_FDCWD, (string_address)stage, 0);
}

/*
        The volume and the mute told to the server over its socket (the sink by the name the page gives it), so a key is heard at once and
        not at the server's next look at the files. volume is a percent or -1 for none, mute is 0, 1 or -1 for none. The wait for the
        server's answer is short (COMPOSITOR_VOLUME_WAIT_MS), and a server that does not answer is let go and tried again in two seconds;
        the files are written all the same, so the person's volume is kept whatever the socket did.
*/
// The connection to the server's socket, made at the first change and kept: false when there is none (and none is tried again for two seconds).
static int compositor_ctl_ready(positive now)
{
        compositor_ctl.timeout_ms = COMPOSITOR_VOLUME_WAIT_MS;
        if (compositor_ctl.open)
                return 1;
        if (now < compositor_ctl_again)
                return 0;
        if (ctl_connect(&compositor_ctl, compositor_volume_path[3]) < 0)
        {
                compositor_ctl_again = now + COMPOSITOR_VOLUME_AGAIN_NS;
                return 0;
        }
        return 1;
}

// What the server answered: a connection that failed is let go and tried again later.
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

/*
        A change of the master volume: step percent more (or less) and unmuted, or with toggle the mute thrown and the volume as it
        is. Nothing without a server: the keys have nothing to turn. The number is shown for a moment in the tray.
*/
static void compositor_volume_step(int step, int toggle)
{
        positive now = clock_monotonic_nanoseconds();
        char text[8];
        int percent, muted;

        compositor_volume_look(now); // inside a moment of our own change it is our own number that is stepped, so a held key adds up
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

                if (percent >= 100)
                        text[at++] = '1', text[at++] = '0', text[at++] = '0';
                else
                {
                        if (percent >= 10)
                                text[at++] = (char)('0' + percent / 10);
                        text[at++] = (char)('0' + percent % 10);
                }
                text[at++] = '\n', text[at] = 0;
                compositor_volume_write(compositor_volume_path[1], text);
        }
        if (muted != compositor_volume.muted)
                compositor_volume_write(compositor_volume_path[2], muted ? "on\n" : "off\n");
        compositor_volume.percent = percent, compositor_volume.muted = muted;
        compositor_volume.flash_until = now + COMPOSITOR_VOLUME_FLASH_NS;
        compositor_volume.hold_until = now + COMPOSITOR_VOLUME_HOLD_NS;
        compositor_volume.look_at = compositor_volume.flash_until;
        compositor_volume_show();
}

/*
        The microphone's mute thrown (the microphone key, a middle press on its cell): told to the server over its socket by the source's
        name as the volume's is, and kept in /root/audio.source.mute for the next boot. The cell is shown for a moment, struck or not,
        whether or not a program records, to see it thrown. Nothing without a server or a microphone.
*/
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
        compositor_volume.mic_flash_until = now + COMPOSITOR_VOLUME_FLASH_NS;
        compositor_volume.hold_until = now + COMPOSITOR_VOLUME_HOLD_NS;
        compositor_volume.look_at = compositor_volume.mic_flash_until;
        compositor_volume_show();
}

// A volume key: five percent a press, the mute thrown by its own key; a held key repeats the steps and not the mute. The microphone's key is a mute alone.
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

/*
        The tray, the clock and the battery, pressed twice quickly: the settings. A window of that name that is
        up is brought up and given the keys; with none, the script is told (tray_double) and starts it, since
        everything on the desktop is started by the machine script and by nothing else.
*/
static void compositor_settings_open(void)
{
        int row = compositor_row_named("settings");

        if (row >= 0)
        {
                compositor_reveal(row);
                return;
        }
        compositor.settings_asked++;
        compositor_script_say("tray_double");
}

/*
        The launcher button pressed with no launcher up: the script is told (launcher_button) and starts the launcher, since
        everything on the desktop is started by the machine script and by nothing else. Before it says so the table holds the
        rectangle of the button that was pressed, the edge its band is along and its screen, which the launcher opens beside
        (window.c's button_x and the rest); the band is the one whose button the point is on.
*/
static void compositor_launcher_open(int x, int y)
{
        compositor_button_rect(x, y, TASKBAR_LAUNCHER);
        compositor.launcher_asked++;
        compositor_script_say("launcher_button");
}

/*
        The logo key alone. A logo key (Super, either one) pressed with no other modifier held and let go with no other key and no
        button of the pointer pressed between is a tap, and a tap is a press on the launcher's tab: the launcher opens beside the tab,
        or is put away if it is up (compositor_logo_act). Held with another key it is the chord it always was (Super and an arrow,
        Super and Space, a chord of the script's) and no tap, and so it is if it was pressed while another modifier was held. The
        rule is this function and one byte of state, so it is checked with no keyboard: bit is the key's held bit (0 for a key that
        is no modifier), value its event (1 down, 0 up, 2 a repeat), others whether any modifier but the logo keys is held after it.
        Answers 1 for the release that ends a tap.
*/
static int compositor_logo_tap(unsigned char *alone, unsigned int bit, int value, int others)
{
        if (!(bit & (CK_HELD_LSUPER | CK_HELD_RSUPER)))
        {
                // Any other key going down, a modifier or not, ends it; a release or a repeat of one says nothing.
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

// A tap of the logo key: with the launcher up it is put away, as a press on its tab would put it; with none, and a band that has the
// tab, the script is asked for one beside the tab of the first band that has it. With no tab (`taskbar launcher off`) it does nothing.
static void compositor_logo_act(void)
{
        int up = compositor_launcher_row();

        if (up >= 0)
        {
                compositor_close(up);
                compositor_tabs_changed();
                return;
        }
        for (compositor.band_at = 0; compositor.table && compositor.band_at < compositor_bands(); compositor.band_at++)
        {
                struct taskbar_view view = compositor_band_view();
                int x, y, w, h;

                if (!taskbar_slot(&view, TASKBAR_LAUNCHER, &x, &y, &w, &h) || w <= 0 || h <= 0)
                        continue;
                compositor.band_at = 0;
                compositor_launcher_open(x + w / 2, y + h / 2);
                return;
        }
        compositor.band_at = 0;
}

// What is held on every keyboard, as the flags a key event carries.
static unsigned int compositor_modifiers(void)
{
        unsigned int held = 0;

        for (unsigned int at = 0; at < compositor.input_count; at++)
                held |= compositor.held[at];
        return compositor_held_flags(held);
}

/*
        A key from the keyboard on input number at.

        Some are the compositor's and go no further, as the kernel desktop's
        were: Alt and AltGr themselves (a window that was sent Alt going down
        and then, with the keys moved on by Alt-Tab, Alt going up would hold a
        modifier that never was), Alt-Tab and Alt-F9 with both edges of the
        key and whatever repeats, any other key typed while an Alt-Tab is under
        way, the chords of the script's key_ functions, and Control-Alt-Backspace and the same with Shift,
        which Canvas hears for itself and acts on. The rest make characters, one event each, for the window that has
        the keys; with none, a key gives them to the topmost.
*/
/*
        The tiling chords, built in: Super with an arrow walks the window with the keys through the arrangements a drag's
        snap makes (compositor_arrange, compositor_screen_part), so a window tiled by a key and one let go at an edge are the
        same rectangle. One step a press, and the same step from the same place every time:

                floating        Left, Right: that half      Up: fill            Down: nothing
                a half          Up: its top quarter         Down: its bottom quarter
                                toward the other side: back to the rectangle it had (the chord that put it there, undone)
                a top quarter   Down: its half              Up: fill            toward the other side: the other top quarter
                a low quarter   Up: its half                toward the other side: the other low quarter
                filled          Down: back to its rectangle                     Left, Right: that half

        and any other press is nothing: Down never puts a window away, and Left or Right never takes it off its screen
        (Super and Shift with Left or Right does that, compositor_screen_chord). A chord the machine script binds never gets
        here (compositor_key runs the script's first), so a user's binding of the same keys takes them.
*/
static unsigned int compositor_chord_next(unsigned int how, unsigned int code)
{
        int filled = how == COMPOSITOR_MAXIMIZED || how == COMPOSITOR_ROOM;

        if (code == CK_KEY_LEFT || code == CK_KEY_RIGHT)
        {
                int left = code == CK_KEY_LEFT;

                if (how == COMPOSITOR_FLOATING || filled)
                        return left ? COMPOSITOR_LEFT : COMPOSITOR_RIGHT;
                if (how == (left ? COMPOSITOR_RIGHT : COMPOSITOR_LEFT))
                        return COMPOSITOR_FLOATING;
                if (how == (left ? COMPOSITOR_TOP_RIGHT : COMPOSITOR_TOP_LEFT))
                        return left ? COMPOSITOR_TOP_LEFT : COMPOSITOR_TOP_RIGHT;
                if (how == COMPOSITOR_BOTTOM || how == (left ? COMPOSITOR_BOTTOM_RIGHT : COMPOSITOR_BOTTOM_LEFT))
                        return left ? COMPOSITOR_BOTTOM_LEFT : COMPOSITOR_BOTTOM_RIGHT;
                return how;
        }
        if (code == CK_KEY_UP)
                switch (how)
                {
                case COMPOSITOR_FLOATING: case COMPOSITOR_TOP_LEFT: case COMPOSITOR_TOP_RIGHT: case COMPOSITOR_BOTTOM:
                        return COMPOSITOR_MAXIMIZED;
                case COMPOSITOR_LEFT: return COMPOSITOR_TOP_LEFT;
                case COMPOSITOR_RIGHT: return COMPOSITOR_TOP_RIGHT;
                case COMPOSITOR_BOTTOM_LEFT: return COMPOSITOR_LEFT;
                case COMPOSITOR_BOTTOM_RIGHT: return COMPOSITOR_RIGHT;
                default: return how;
                }
        if (code == CK_KEY_DOWN)
                switch (how)
                {
                case COMPOSITOR_MAXIMIZED: case COMPOSITOR_ROOM: return COMPOSITOR_FLOATING;
                case COMPOSITOR_LEFT: return COMPOSITOR_BOTTOM_LEFT;
                case COMPOSITOR_RIGHT: return COMPOSITOR_BOTTOM_RIGHT;
                case COMPOSITOR_TOP_LEFT: return COMPOSITOR_LEFT;
                case COMPOSITOR_TOP_RIGHT: return COMPOSITOR_RIGHT;
                default: return how;
                }
        return how;
}

static void compositor_tile_chord(unsigned int code)
{
        int row = compositor.focus;
        unsigned int how;

        if (row < 0 || !compositor_visible(row) || compositor.cycle)
                return;
        how = compositor_chord_next(compositor.arranged[row], code);
        if (how == compositor.arranged[row])
                return;
        if (how == COMPOSITOR_FLOATING)
                compositor_float(row);
        else
                compositor_arrange(row, how);
}

/*
        Super and Shift with Left or Right: the window with the keys to the next screen that way -- the nearest whose left
        edge is at or past this one's right (or whose right is at or before its left), the one most level with it of two as
        near -- keeping what it fills there, a floating one its share of the room as a screen's carry does (and the
        rectangle a restore puts back goes with it). There is no wrap at the last screen. Its scale is the new screen's.
*/
static void compositor_screen_chord(unsigned int code)
{
        int row = compositor.focus, from, to = -1, right = code == CK_KEY_RIGHT;
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
                long long off = ((long long)s[1] * 2 + s[3]) - ((long long)f[1] * 2 + f[3]);

                off = off < 0 ? -off : off;
                if ((int)at == from || gap < 0)
                        continue;
                if (best < 0 || gap < best || (gap == best && off < level))
                        best = gap, level = off, to = (int)at;
        }
        if (to < 0)
                return;
        how = compositor.arranged[row];
        {
                int keep_x = compositor.saved_x[row], keep_y = compositor.saved_y[row];

                compositor_screen_carry(screens[from], screens[to], &keep_x, &keep_y, (int)compositor.saved_w[row], (int)compositor.saved_h[row]);
                compositor.saved_x[row] = keep_x, compositor.saved_y[row] = keep_y;
        }
        if (how == COMPOSITOR_FLOATING)
        {
                int rect[4] = {compositor.x[row], compositor.y[row], (int)compositor.w[row], (int)compositor.h[row]};

                compositor_screen_carry(screens[from], screens[to], &rect[0], &rect[1], rect[2], rect[3]);
                compositor_set_rect(row, COMPOSITOR_FLOATING, rect);
                return;
        }
        compositor_arrange_on(row, how == COMPOSITOR_ROOM ? COMPOSITOR_MAXIMIZED : how, to);
}

#ifdef CONFIG_MOONWATER_WAYLAND
// A key a client's window reads as it is, from the keymap it was sent (wayland_host.c): true when the key was the client's.
static int compositor_wl_key(unsigned int code, int value);
static int compositor_wl_hold_counts(int dx, int dy, positive now);
static void compositor_wl_hold_keep(void);
static void compositor_wl_input(void);
static int compositor_wl_suspends(unsigned int code, int value, int super);
#else
// No Wayland shim in the build: no window holds the pointer, and the hand's counts are nobody's.
static int compositor_wl_hold_counts(int dx, int dy, positive now)
{
        (void)dx, (void)dy, (void)now;
        return 0;
}
#endif

static void compositor_key(unsigned int at, unsigned int code, int value, positive reported)
{
        unsigned int bit = compositor_held_bit(code), characters[2] = {0, 0}, flags, modifiers, chord;
        int count, target;

        // Escape while the hand moves or resizes a window puts it back as it was at the press, and is no window's key.
        if (code == CK_KEY_ESC && value && (compositor.drag >= 0 || compositor.resize >= 0))
        {
                if (value == 1)
                        compositor_hold_cancel();
                return;
        }

        // The snap assist offered after a drop goes at any key that is not a modifier: Escape is only that, and any other key
        // goes on to the window that has the keys, as it would have.
        if (compositor.assist_armed && value == 1 && !bit)
        {
                compositor_assist_close();
                if (code == CK_KEY_ESC)
                        return;
        }

        if (bit)
        {
                if (value)
                        compositor.held[at] |= (unsigned char)bit;
                else
                        compositor.held[at] &= (unsigned char)~bit;
        }
        // The logo key pressed and let go alone is a press on the launcher's tab (compositor_logo_tap). The key itself goes on as it did.
        if (compositor_logo_tap(&compositor.logo_alone, bit, value,
                                (compositor_modifiers() & (WINDOW_KEY_SHIFT | WINDOW_KEY_CONTROL | WINDOW_KEY_ALT | WINDOW_KEY_ALTGR)) != 0))
                compositor_logo_act();
        if (bit)
        {
                // A modifier is part of what a drag snaps to (Control is no snap): its target is read again now, not at the
                // next motion.
                if (compositor.drag >= 0)
                        compositor_preview(compositor.drag);
        }

        modifiers = compositor_modifiers();
        chord = modifiers & (WINDOW_KEY_SHIFT | WINDOW_KEY_CONTROL | WINDOW_KEY_ALT);

        if (bit & (CK_HELD_ALT | CK_HELD_ALTGR))
        {
                if (!(modifiers & WINDOW_KEY_ALT) && compositor.cycle)
                        compositor_focus_commit();
#ifdef CONFIG_MOONWATER_WAYLAND
                // A client's window reads Alt and AltGr itself: they are modifiers of its keymap.
                compositor_wl_key(code, value);
#endif
                return;
        }

#ifdef CONFIG_MOONWATER_WAYLAND
        // A client's keyboard shortcuts inhibitor on the window with the keys suspends the compositor's chords: the keys are the
        // window's, except Super with Escape, the one chord kept (it is the compositor's, held a second to give the keys back), and the
        // media keys, which are not chords and stay the desktop's (compositor_wl_suspends: 0 the chords run, 1 the window has the key,
        // 2 the kept chord).
        if (!bit)
        {
                int suspended = compositor_wl_suspends(code, value, (modifiers & WINDOW_KEY_SUPER) != 0);

                if (suspended == 2)
                        return;
                if (suspended == 1 && code != CK_KEY_MUTE && code != CK_KEY_VOLUME_DOWN && code != CK_KEY_VOLUME_UP && code != CK_KEY_MICMUTE)
                {
                        compositor_wl_key(code, value);
                        return;
                }
        }
#endif

        if (chord == WINDOW_KEY_ALT && code == CK_KEY_TAB)
        {
                if (value)
                        compositor_focus_step();
                return;
        }

        if (chord == WINDOW_KEY_ALT && code == CK_KEY_F4 && !compositor.cycle)
        {
                if (value == 1)
                        compositor_close(compositor.focus);
                return;
        }

        if (chord == WINDOW_KEY_ALT && code == CK_KEY_F9 && !compositor.cycle)
        {
                if (value == 1)
                        // Alt-F9: the window with the keys is put away, and the keys go to the one under it.
                        compositor_minimize_row(compositor.focus);
                return;
        }

        // The volume keys are the desktop's, whichever window has the keys.
        if (!(modifiers & (WINDOW_KEY_CONTROL | WINDOW_KEY_ALT | WINDOW_KEY_SUPER)) &&
            (code == CK_KEY_MUTE || code == CK_KEY_VOLUME_DOWN || code == CK_KEY_VOLUME_UP || code == CK_KEY_MICMUTE))
        {
                if (value)
                        compositor_volume_key(code, value);
                return;
        }

        // A chord the script bound is its own and no window's; a press says so, and not a repeat, so a key held
        // down does not fill the desktop with what it starts.
        for (unsigned int at = 0; at < compositor_keys.count; at++)
                if (code == compositor_keys.code[at] &&
                    (modifiers & (WINDOW_KEY_SHIFT | WINDOW_KEY_CONTROL | WINDOW_KEY_ALT | WINDOW_KEY_SUPER)) ==
                        compositor_keys.modifiers[at])
                {
                        if (value == 1)
                                // A chord pressed: said to the script, which runs what it is bound to.
                                compositor_script_say(compositor_keys.chord[((unsigned int)(at))]);
                        return;
                }

        // Super alone with an arrow: the tiling chords, which the script's bindings above have had first say; and Super and
        // Shift with Left or Right, a window to the next screen.
        if ((modifiers & (WINDOW_KEY_SUPER | WINDOW_KEY_SHIFT | WINDOW_KEY_CONTROL | WINDOW_KEY_ALT)) == WINDOW_KEY_SUPER &&
            (code == CK_KEY_LEFT || code == CK_KEY_RIGHT || code == CK_KEY_UP || code == CK_KEY_DOWN))
        {
                if (value == 1)
                        compositor_tile_chord(code);
                return;
        }
        if ((modifiers & (WINDOW_KEY_SUPER | WINDOW_KEY_SHIFT | WINDOW_KEY_CONTROL | WINDOW_KEY_ALT)) == (WINDOW_KEY_SUPER | WINDOW_KEY_SHIFT) &&
            (code == CK_KEY_LEFT || code == CK_KEY_RIGHT))
        {
                if (value == 1)
                        compositor_screen_chord(code);
                return;
        }

        if (((chord & ~WINDOW_KEY_SHIFT) == (WINDOW_KEY_ALT | WINDOW_KEY_CONTROL) && code == CK_KEY_BACKSPACE) ||
            ((modifiers & WINDOW_KEY_ALT) && compositor.cycle))
                return;

#ifdef CONFIG_MOONWATER_WAYLAND
        // A client's window is given the key as it is, with no characters made of it here.
        if (compositor_wl_key(code, value))
                return;
#endif
        count = compositor_keymap_event(&compositor.keymap, code, value, modifiers, characters);
        if (count < 0)
                return;

        if (compositor.focus < 0)
                compositor_refocus();
        if (compositor.focus < 0)
                return;

        // A press goes to the window that has the keys and is held there; a repeat or a
        // release of a key whose press went elsewhere (or nowhere) is nobody's to hear.
        // While the window has a popup open, the topmost popup is where its keys go.
        // A repeat or a release goes where its press went (keys_to), not where the keys go now: a popup opened
        // since the press would otherwise get a release it never had the press of, and the window the press went
        // to would be left holding the key down.
        target = compositor_popup_top(compositor.focus);
        if (code < 256)
        {
                unsigned char mask = (unsigned char)(1u << (code & 7)), *down = &compositor.keys_down[code >> 3];

                if (value == 1)
                {
                        if (compositor.keys_to != target)
                                compositor_keys_release();
                        *down |= mask;
                        compositor.keys_to = target;
                }
                else if (!(*down & mask))
                        return;
                else
                {
                        target = compositor.keys_to;
                        if (!value)
                                *down &= (unsigned char)~mask;
                }
        }

        flags = modifiers | (value ? WINDOW_KEY_DOWN : 0) | (value == 2 ? WINDOW_KEY_REPEAT : 0);
        for (int each = 0; each < (count ? count : 1); each++)
                compositor_post(target, WINDOW_EVENT_KEY, flags, (int)code, (int)characters[each]);
        if (value == 1)
                compositor_latency(2, reported, clock_monotonic_nanoseconds());
}

/*
        The pointer stays on a screen, wherever the motions of a burst took it
        (compositor_screen_keep_point): where they end off every screen it is
        kept on the screen it was on, and a tablet's point, which comes from
        nowhere, goes to the nearest screen.
*/
static void compositor_pointer_clamp(void)
{
        unsigned int count;
        const int (*screens)[4] = compositor_screens(&count);
        int nowhere = -2147483647 - 1;

        compositor_pointer_sync();
        compositor_screen_keep_point(screens, count, compositor.absolute ? nowhere : compositor.kept_x,
                                     compositor.absolute ? nowhere : compositor.kept_y, &compositor.real_x,
                                     &compositor.real_y);
        compositor.kept_x = compositor.real_x;
        compositor.kept_y = compositor.real_y;
        compositor.absolute = 0;
        compositor_pointer_shown();
}

/*
        The pointer is the real place led to where it will be (the lead of the class that owns it, see predict), and held on a
        screen. A press is handled at the real place, so while it is (at_real) there is no lead.
*/
// A pointer set from outside the prediction (a warp, a screen's change, a caller that writes the pointer) is the real place,
// and the lead it had is over. Done before the hand moves again, so a report counts from where the pointer was put.
static void compositor_pointer_sync(void)
{
        struct compositor_predict *track = &compositor.predict[compositor.owner];

        if (compositor.pointer_x != compositor.shown_x || compositor.pointer_y != compositor.shown_y)
        {
                compositor.real_x = compositor.pointer_x, compositor.real_y = compositor.pointer_y;
                compositor.accel_x = compositor.accel_y = 0;
                track->lead_x = track->lead_y = 0;
        }
}

static void compositor_pointer_shown(void)
{
        struct compositor_predict *track = &compositor.predict[compositor.owner];
        unsigned int count;
        const int (*screens)[4] = compositor_screens(&count);
        long long lead_x, lead_y;

        lead_x = compositor.at_real ? 0 : compositor_predict_px(track->lead_x);
        lead_y = compositor.at_real ? 0 : compositor_predict_px(track->lead_y);
        compositor.pointer_x = (int)(compositor.real_x + lead_x);
        compositor.pointer_y = (int)(compositor.real_y + lead_y);
        compositor_screen_keep_point(screens, count, compositor.real_x, compositor.real_y, &compositor.pointer_x,
                                     &compositor.pointer_y);
        compositor.shown_x = compositor.pointer_x, compositor.shown_y = compositor.pointer_y;
}

// Whether a movement of the hand is a shake that finds the pointer: `moonwater canvas cursor shake off` says none is.
static int compositor_shaken(int delta, unsigned long long now)
{
        return compositor_shake(&compositor.shake, delta, now) && !compositor.cursor.shake_off;
}

// The pointer was shaken: it is large until its time is up, which is COMPOSITOR_MAGNIFIED_NS or what `moonwater canvas cursor shake delay` says, from this last shake.
static void compositor_magnify(positive now)
{
        compositor.cursor.magnify = 0;
        compositor.cursor.magnified = 1;
        compositor.cursor.magnified_until = now + (compositor.cursor.shake_ns ? compositor.cursor.shake_ns : COMPOSITOR_MAGNIFIED_NS);
}

// The pointer, to a window: where it is in the window's picture.
// Told only when the place went in the ring: one a full ring refused is not taken as told, or the wheel after the
// program has caught up (compositor_wheel) would turn at the place it had before.
static void compositor_pointer_to(int row)
{
        int x = compositor.pointer_x - compositor.x[row], y = compositor.pointer_y - compositor.y[row];

        compositor.told_row = compositor_post(row, WINDOW_EVENT_MOTION, 0, x, y) ? row : COMPOSITOR_NONE;
        compositor.told_x = x;
        compositor.told_y = y;
}

#ifdef CONFIG_MOONWATER_WAYLAND
static int compositor_wl_asked_shape(int row); // wayland_host.c: the CP_ shape the row's client asked for, or -1
static int compositor_picture_poll(int row);   // the picture row's client drew for the pointer, after the shim (below)
#endif

/*
        The shapes a window's client asks for are drawn with the compositor's own bitmaps (CP_ARROW to CP_NOTALLOWED, the codes of
        wayland_cursor_code in wayland.c); a picture it draws itself is worn in their place (compositor_picture). This is the one decision point for them:
        with CP_CLIENT_SHAPES 0 a client's shape is ignored and the program's own shape (the arrow when it has none) is worn instead.
*/
#define CP_CLIENT_SHAPES 1

/*
        The shape a window's pointer wears over its controls: the one its client asked for with wp_cursor_shape, or else the one the
        program's kit published (WINDOW_POINTER_*, the arrow when it has none). -1 is no request, which is none for a window of the kit.
*/
static int compositor_asked_shape(int row)
{
#ifdef CONFIG_MOONWATER_WAYLAND
        int asked = compositor_wl_asked_shape(row);

        if (asked == -2 || (asked >= 0 && CP_CLIENT_SHAPES))
                return asked;
#endif
        if (!compositor.table || row < 0 || row >= WINDOWS)
                return -1;
        // A program's shape is one of the CP_ codes it knows; any other value is the arrow, so a program cannot make the -2 of no pointer.
        unsigned int shape = __atomic_load_n(&compositor.table->pointer_shape[row], __ATOMIC_ACQUIRE);

        return shape < CP_SHAPES ? (int)shape : CP_ARROW;
}

static void compositor_motion(void)
{
        int under, tab;

        compositor_pointer_clamp();
        under = compositor_under(compositor.pointer_x, compositor.pointer_y, &tab);

        // The shape of what the hand has hold of or is over: the edges it could take, and inside a window (away from its edges)
        // the shape its client asked for with wp_cursor_shape, or the program's controls publish (the arrow when it has none).
        {
                // A window a button is held in (compositor.grab) has the pointer for as long as it is held: its shape is worn wherever the
                // hand has gone, off the window and over another included, so a selection dragged past its window's edge keeps its pointer.
                int held = compositor.resize < 0 && compositor.drag < 0 ? compositor.grab : COMPOSITOR_NONE;
                unsigned int zone = compositor.resize >= 0 || compositor.drag >= 0 || under < 0 || held >= 0 ? 0
                                    : compositor_edge_zone(under, compositor.pointer_x, compositor.pointer_y);
                int of = held >= 0 ? held : compositor.resize < 0 && compositor.drag < 0 && under >= 0 && !zone ? under : COMPOSITOR_NONE;
                int asked = of >= 0 ? compositor_asked_shape(of) : -1;

                compositor.cursor.shape = compositor.resize >= 0 ? compositor_cursor_by_edges[compositor.edges[compositor.resize] & 15]
                                          : compositor.drag >= 0 ? CP_ARROW
                                          : zone ? compositor_cursor_by_edges[zone & 15]
                                          : asked >= 0 ? (unsigned int)asked
                                          : CP_ARROW;
                // A client that asked for no pointer over its window has none there (-2 is that answer).
                compositor.cursor.client_hidden = asked == -2;
                compositor.cursor.asked_row = of;
                compositor.cursor.asked_seen = asked;
#ifdef CONFIG_MOONWATER_WAYLAND
                // Over the window's own part, the picture its client drew for the pointer, where it drew one, in place of the arrow.
                compositor_picture_poll(asked >= 0 ? of : COMPOSITOR_NONE);
#endif
        }
        compositor_cursor_moved(clock_monotonic_nanoseconds());
        if (compositor.cursor.magnify)
                compositor_magnify(clock_monotonic_nanoseconds());
        compositor_cursor_move();

        // On the plane the pointer is on the screen when the call returns; drawn, when the frame has gone.
        if (compositor.motion_ts && !compositor_cursor_in_scene())
        {
                compositor_latency(0, compositor.motion_ts, clock_monotonic_nanoseconds());
                compositor.motion_ts = 0;
        }

        // The edge the hand holds, all of a window it moves, or for a pointer with no button down the edge it is over.
        compositor_lit(compositor.resize >= 0  ? compositor.resize
                       : compositor.drag >= 0 ? compositor.drag
                                              : under,
                       compositor.resize >= 0 ? compositor.edges[compositor.resize]
                       : compositor.drag >= 0 ? COMPOSITOR_LIT_ALL
                       : under < 0 || compositor.grab >= 0 || compositor.touching
                           ? 0
                           : compositor_edge_zone(under, compositor.pointer_x, compositor.pointer_y),
                       compositor.resize >= 0 || compositor.drag >= 0);

        if (compositor.resize >= 0)
        {
                compositor_resize_move();
                return;
        }

        if (compositor.drag >= 0)
        {
                int row = compositor.drag;

                // Taken by the hand and moved: a window that fills a place is the window it was, under the hand, once the
                // hand is UI_SLOP from where it pressed (in its own scale). Short of that it is still a press -- a click, or
                // half a double click with a shaking hand -- and nothing moves.
                if (compositor.arranged[row] != COMPOSITOR_FLOATING)
                {
                        if (!ui_touch_past(compositor.press_x, compositor.press_y, compositor.pointer_x, compositor.pointer_y,
                                           ui_touch_slop(compositor_row_scale(row))))
                                return;
                        compositor_float_for_drag(row);
                }

                // Kept on the output as it is moved, as when it was placed.
                compositor.drag_moved = 1;
                compositor_damage_row(row);
                compositor_place(row, compositor.pointer_x - compositor.drag_x, compositor.pointer_y - compositor.drag_y);
                compositor_damage_row(row);
                compositor_preview(row);
                return;
        }

        // A held button keeps the pointer with the window it went down on. Otherwise a window is told of the motion
        // over it only when it wants the motion (the window hover_off flag): each report of it wakes the program.
        compositor_shared_hover(under);
        compositor.hover = under;
        if (compositor.grab >= 0)
                compositor_pointer_to(compositor.grab);
        // A finger that was held for a long press has no button down (its press was taken back), and what it selects follows it as
        // a held button's drag does, to the window it landed on, whether or not that window asked for the pointer's motion.
        else if (compositor.touch.gesture.mode == UI_TOUCH_HELD && compositor.touch.hold_row >= 0)
                compositor_pointer_to(compositor.touch.hold_row);
        else if (under >= 0 && !__atomic_load_n(&compositor.table->hover_off[under], __ATOMIC_ACQUIRE))
                compositor_pointer_to(under);
}

/*
        A window answers a motion after the compositor has read its shape (the kit publishes the control under the pointer when it
        has handled the motion), so the shape it publishes while the pointer rests is read again here: when it is not the one the
        last motion read, the pointer is moved over the window as a motion would move it. The Wayland host calls it when a client's
        cursor request arrives (wayland_host.c, WAYLAND_DO_CURSOR), and the loop calls it on each wake.
*/
static void compositor_shape_sync(void)
{
        int of = compositor.cursor.asked_row;

        if (of >= 0 && of < WINDOWS && compositor_asked_shape(of) != compositor.cursor.asked_seen)
                compositor_motion();
#ifdef CONFIG_MOONWATER_WAYLAND
        // The client's picture is looked at each pass too (its serial moves with every change): a new one, or none, is shown now.
        else if (compositor_picture_poll(compositor.cursor.asked_seen >= 0 ? of : COMPOSITOR_NONE))
                compositor_cursor_move();
#endif
}

/*
        Where a drag ended, read as an arrangement: the window's own rectangle, on the screen it is mostly on, and the edges of
        that screen it touches (compositor_snap_rule). The pointer is not read: the hand moves the window and the window is
        what snaps. The edges touched last frame are the dead zone's (compositor.snap_held). Control held is no snap at all, so a
        window can be set flush against an edge; let go of it and the edges count again.
*/
static unsigned int compositor_snap(const int *box, unsigned int *touch, int *screen)
{
        unsigned int count, free;
        const int (*screens)[4] = compositor_screens(&count);
        int at = compositor_screen_owner(screens, count, box[0], box[1], box[2], box[3]);

        *touch = 0;
        *screen = at;
        if (at < 0 || compositor_screen_cover(screens[at], box[0], box[1], box[2], box[3]) <= 0 ||
            (compositor_modifiers() & WINDOW_KEY_CONTROL))
                return COMPOSITOR_FLOATING;
        free = compositor_snap_free(screens, count, box, at);
        return compositor_snap_rule(box, screens[at], free, compositor.snap_held, COMPOSITOR_SNAP_EDGE * compositor_output_scale(at) / 100,
                                    COMPOSITOR_SNAP_CORNER * compositor_output_scale(at) / 100, touch);
}

// The rectangle a window dragged is where the hand has it, before placement keeps it on its screen (compositor_place
// clamps it to the screen, so a window held at an edge by the clamp is not one pushed there): a snap reads its edges
// from it (compositor_snap). A finger or a pen is led, so its pointer is where it is going, and a frame with no new
// place takes the lead away again (compositor_predict_clear). The snap reads the place the hand is at, which is the
// same between places as at them; the pointer's lead would flip a target between the two, every report.
static void compositor_box_of(int row, int *box)
{
        int x = compositor.pointer_x, y = compositor.pointer_y;

        if (compositor.owner != COMPOSITOR_PREDICT_MOUSE)
                x = compositor.real_x, y = compositor.real_y;
        box[0] = x - compositor.drag_x, box[1] = y - compositor.drag_y;
        box[2] = (int)compositor.w[row], box[3] = (int)compositor.h[row];
}

/*
        The snap preview: the target the window being moved touches (compositor_target), from the frame it is touched in, and
        damaged where it was and where it is. Its target is there while the contact is, and gone the moment it is lost or the
        window is let go; what is drawn of it is there at once, moves to a new target and fades out when there is none
        (compositor_preview_shown). Once the window has moved only.
*/
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
                compositor_overlays_drag(row, compositor.pointer_x, compositor.pointer_y, how, screen);
        }
        else if (row < 0 && compositor.grid_on)
        {
                compositor_overlays_damage();
                compositor.grid_on = 0;
        }
        if (row < 0)
                compositor.grid_since = 0;
        compositor.snap_held = touch;
        if (how == COMPOSITOR_FLOATING)
        {
                if (compositor.preview[2] > 0)
                {
                        compositor_damage(compositor.preview[0], compositor.preview[1], compositor.preview[2], compositor.preview[3]);
                        compositor_preview_leave();
                }
                memory_zero(compositor.preview, sizeof(compositor.preview));
                compositor.preview_row = COMPOSITOR_NONE;
                return;
        }
        if (row == compositor.preview_row && !memory_compare(rect, compositor.preview, sizeof(rect)))
                return;
        if (compositor.preview[2] > 0)
                compositor_damage(compositor.preview[0], compositor.preview[1], compositor.preview[2], compositor.preview[3]);
        compositor_preview_enter(row, rect, how);
        memory_copy(compositor.preview, rect, sizeof(rect));
        compositor.preview_row = row;
        compositor_damage(rect[0], rect[1], rect[2], rect[3]);
}

// The button is let go. A window that was being moved is arranged by where the
// hand let go, unless the release is not the hand's (the device went away).
static void compositor_lit(int row, unsigned int edges, unsigned int held);

/*
        The window is taken hold of to be moved, wherever the pointer is on it now: its popups go, the hand's place on it
        is kept, and what a drop on a target would restore. The press on its top does this, and so does a client's own
        request to be moved while the person holds a button on it (the Wayland shim's xdg_toplevel.move). press_x and
        press_y mark where the hold began: for a native press, where the pointer is now; for a client move, where the button
        went down so its own drag threshold does not pay a second compositor slop.
*/
static void compositor_drag_begin_from(int under, int press_x, int press_y)
{
        compositor_popups_close(under, -1);
        compositor.drag = under;
        compositor.drag_moved = 0;
        compositor.drag_x = compositor.pointer_x - compositor.x[under];
        compositor.drag_y = compositor.pointer_y - compositor.y[under];
        compositor.press_x = press_x;
        compositor.press_y = press_y;
        compositor_hold_take(0, under);
        // What a drop on a target keeps for a restore: where it floats now, or where it floated before.
        if (compositor.arranged[under] == COMPOSITOR_FLOATING)
        {
                int keep[4] = {compositor.x[under], compositor.y[under], (int)compositor.w[under], (int)compositor.h[under]};

                memory_copy(compositor.drag_keep, keep, sizeof(keep));
        }
        else
        {
                int keep[4] = {compositor.saved_x[under], compositor.saved_y[under], (int)compositor.saved_w[under],
                               (int)compositor.saved_h[under]};

                memory_copy(compositor.drag_keep, keep, sizeof(keep));
        }
        // The window is in the hand: all of its line is lit until it is let go.
        compositor_lit(under, COMPOSITOR_LIT_ALL, 1);
}

static void compositor_drag_begin(int under)
{
        compositor_drag_begin_from(under, compositor.pointer_x, compositor.pointer_y);
}

#ifdef CONFIG_MOONWATER_WAYLAND
static void compositor_drag_begin_client(int under)
{
        compositor_drag_begin_from(under, compositor.button_x, compositor.button_y);
        if (compositor.arranged[under] != COMPOSITOR_FLOATING &&
            ui_touch_past(compositor.press_x, compositor.press_y, compositor.pointer_x, compositor.pointer_y,
                          ui_touch_slop(compositor_row_scale(under))))
        {
                compositor_float_for_drag(under);
                compositor.drag_moved = 1;
                compositor_damage_row(under);
                compositor_preview(under);
        }
}

static void compositor_drag_stop(int row)
{
        int resized;

        if (row < 0 || (compositor.drag != row && compositor.resize != row))
                return;
        resized = compositor.resize == row;
        compositor_preview(COMPOSITOR_NONE);
        compositor.drag = compositor.resize = compositor.shared_row = COMPOSITOR_NONE;
        compositor_mark_clear();
        compositor_lit(COMPOSITOR_NONE, 0, 0);
        compositor.hold_rows = 0;
        if (resized)
                compositor.woken |= 1u << row;
}
#endif

// A release with flags: WINDOW_KEY_CANCEL is one that is no click (a finger that became two, or was held).
static void compositor_release_as(unsigned int code, int snap, unsigned int flags)
{
        int dragged = compositor.drag, to = COMPOSITOR_NONE, resized = compositor.resize;
        unsigned int index = code - COMPOSITOR_BTN_LEFT, drop_how = COMPOSITOR_FLOATING;
        int drop_screen = 0, drop_box[4];
        unsigned int drop_touch = 0;

        compositor_pointer_press(code, 0);
        // Where the window lands is read before the preview and the grid are taken away: the grid's region, or the snap.
        if (dragged >= 0 && snap && compositor.drag_moved)
        {
                compositor_box_of(dragged, drop_box);
                drop_how = compositor_target(dragged, compositor.pointer_x, compositor.pointer_y, drop_box, &drop_screen, &drop_touch);
        }
        // The preview goes with the drag; where the window lands is the target read above, from its rectangle.
        compositor_preview(COMPOSITOR_NONE);
        compositor.drag = COMPOSITOR_NONE;
        compositor.resize = COMPOSITOR_NONE;
        // What the hand held is let go: its line is no longer lit.
        if (dragged >= 0 || resized >= 0)
                compositor_lit(COMPOSITOR_NONE, 0, 0);
        if (resized >= 0)
        {
                compositor_mark_clear();
                compositor.woken |= 1u << resized;
                if (compositor.shared_row >= 0)
                {
                        compositor.woken |= compositor.shared_lo_set | compositor.shared_hi_set;
                        compositor.shared_row = COMPOSITOR_NONE;
                }
        }
        // A button is let go in the window it was pressed in: a second button pressed over another window while the
        // first is held is that window's, and the first's release is the one that window held the grab for. A button
        // that was never recorded as pressed in a window (its press was taken by a popup, a minimise or a hold's cancel)
        // goes to none: the window the grab holds is not its window, and it never had the press. The grab is kept while
        // another button still down holds it.
        if (index < 8 && (compositor.button_down >> index & 1))
        {
                to = compositor.button_row[index];
                compositor.button_down &= (unsigned char)~(1u << index);
                compositor_grab_settle();
        }
        compositor_post(to, WINDOW_EVENT_BUTTON, flags, (int)code, 0);
        // A press and a release with no motion between is a click on the window's top, which only raises it:
        // a window at the top of the output, where the snap margin is, would otherwise fill it when clicked.
        if (dragged >= 0 && snap && compositor.drag_moved)
        {
                unsigned int how = drop_how;
                int screen = drop_screen;

                if (how != COMPOSITOR_FLOATING)
                {
                        compositor_arrange_on(dragged, how, screen);
                        // The rectangle a restore puts back is the one from before the drag (compositor.drag_keep).
                        compositor.saved_x[dragged] = compositor.drag_keep[0], compositor.saved_y[dragged] = compositor.drag_keep[1];
                        compositor.saved_w[dragged] = (unsigned int)compositor.drag_keep[2];
                        compositor.saved_h[dragged] = (unsigned int)compositor.drag_keep[3];
                        // The others of the screen are offered the space the drop left, until a click or Escape.
                        compositor.assist_on = compositor.assist_armed = 1;
                        compositor.assist_row = dragged;
                        compositor.assist_how = (unsigned int)compositor.arranged[dragged];
                        compositor.assist_screen = screen;
                        compositor_assist_refresh();
                        if (!compositor.assist_count)
                                compositor_assist_close();
                        // They come up now, not during the drag: where they are is drawn for the next present.
                        compositor_overlays_damage();
                }
        }
        if (compositor.grid_on)
        {
                compositor_overlays_damage();
                compositor.grid_on = 0;
        }
        // Where the hand put a window is where a window of its name opens next (compositor_remember).
        if (snap && (resized >= 0 || (dragged >= 0 && compositor.drag_moved)))
                compositor_remember(resized >= 0 ? resized : dragged);
}

/*
        A button of the mouse is handled at the hand's real place, presses and releases alike, whatever the pointer is led to:
        a click lands where the hand was. A finger's or a pen's press is at its led place, as it always was. act(code, arg) is
        the press or the release itself (compositor_press_at, compositor_release_act).
*/
static void compositor_at_hand(void (*act)(unsigned int, int), unsigned int code, int arg)
{
        int real;

        compositor_pointer_sync();
        real = !compositor.at_real && !compositor.touching && compositor.owner == COMPOSITOR_PREDICT_MOUSE;
        if (!real)
        {
                act(code, arg);
                return;
        }
        // The window under the press is told the motion to the real place before the button, as each report was told before
        // the change: a button carries no place of its own, only the last motion's. A pointer that is not led is told already.
        {
                int led_x = compositor.pointer_x, led_y = compositor.pointer_y;

                compositor.at_real = 1;
                compositor_pointer_shown();
                if ((compositor.pointer_x != led_x || compositor.pointer_y != led_y) && !compositor_wl_hold_counts(0, 0, 0))
                        compositor_motion();
        }
        act(code, arg);
        compositor.at_real = 0;
        compositor_pointer_shown();
        compositor_cursor_move();
}

static void compositor_release_act(unsigned int code, int snap)
{
        compositor_release_as(code, snap, 0);
}

// A release is at the real place too (see compositor_press): a mouse's button let go where the hand is.
static void compositor_release(unsigned int code, int snap)
{
        compositor_at_hand(compositor_release_act, code, snap);
}

/*
        Whether a point is on a window's button, the close one or the minimise one (box: compositor_close_box or
        compositor_minimize_box): its box, or for a finger the box and COMPOSITOR_TOUCH_BUTTON_SLOP more
        on every side (ui_reach_hit), and no further: the corner of the window it sits in is the finger's own, a target of
        UI_TARGET (compositor_corner_reach), and wins everywhere in it but the disc. None for a press being made again
        without its buttons (compositor_touch_resume).
*/
static int compositor_on_button(int row, int x, int y, int (*box)(const struct compositor_frame *, int *, int *, int *))
{
        struct compositor_frame frame;
        int bx, by, side;

        compositor_frame_of(row, &frame);
        if (compositor.touch.resumed || !box(&frame, &bx, &by, &side))
                return 0;
        return ui_reach_hit(compositor.touching, side + 2 * (COMPOSITOR_TOUCH_BUTTON_SLOP * frame.scale / 100), x, y, bx, by, side, side);
}

/*
        The minimise button's press, and the release that is its: a press on the button puts its window away, and neither
        the press nor its release is given to any window or starts a drag (compositor_press). The code of the press is
        what its release must be, and a press of any button first lets go of what an earlier one held, so a release a
        finger's cancel lost is not held over.
*/
static unsigned int compositor_minimize_code;

static int compositor_minimize_press(unsigned int code, int down)
{
        int under, tab = COMPOSITOR_NONE;

        if (!down)
        {
                if (!code || code != compositor_minimize_code)
                        return 0;
                compositor_minimize_code = 0;
                return 1;
        }
        compositor_minimize_code = 0;
        if (code != COMPOSITOR_BTN_LEFT || compositor_wl_held())
                return 0;

        compositor_pointer_clamp();
        under = compositor_under(compositor.pointer_x, compositor.pointer_y, &tab);
        if (under < 0 || tab >= 0 || !compositor_visible(under) ||
            !compositor_on_button(under, compositor.pointer_x, compositor.pointer_y, compositor_minimize_box))
                return 0;

        compositor_popups_close(-1, -1);
        compositor_minimize_code = code;
        compositor_minimize_row(under);
        return 1;
}

// The windows that own the menus open now (the root of each one's chain), as a bit each.
static unsigned int compositor_menu_roots(void)
{
        unsigned int roots = 0;

        for (int row = 0; row < WINDOWS; row++)
                if (compositor_popup_parent[row] >= 0 && compositor.showing[row] && !(compositor.state[row] & WINDOW_CLOSING))
                        roots |= 1u << compositor_popup_root(row);
        return roots;
}

/*
        A press that dismisses a menu takes what the hand had in hand in the window that owns it, as a press that is no click
        does: a drag or a resize of that window is let go where it is, and a button held in it (a selection, a drag in its
        contents) is released with WINDOW_KEY_CANCEL, so the window does not finish it as a click.
*/
static void compositor_cancel_holds(unsigned int roots)
{
        for (unsigned int index = 0; index < 8; index++)
                if ((compositor.button_down >> index & 1) && compositor.button_row[index] >= 0 && roots >> compositor.button_row[index] & 1)
                        compositor_release_as(COMPOSITOR_BTN_LEFT + index, 0, WINDOW_KEY_CANCEL);
        if ((compositor.drag >= 0 && roots >> compositor.drag & 1) || (compositor.resize >= 0 && roots >> compositor.resize & 1))
                compositor_release_as(COMPOSITOR_BTN_LEFT, 0, WINDOW_KEY_CANCEL);
}

static void compositor_button(unsigned int code, int down)
{
        int under, tab, alt = (compositor_modifiers() & WINDOW_KEY_ALT) != 0, top, band, up;
        unsigned int roots;
        positive now;

        compositor_pointer_clamp();
        under = compositor_under(compositor.pointer_x, compositor.pointer_y, &tab);

        if (!down)
        {
                compositor_release(code, 1);
                return;
        }

        // A right press while the hand moves or resizes a window takes the move back, as Escape does (compositor_hold_cancel);
        // the press is nothing else. A resize the right button holds (Alt) has no second right press.
        if (code == COMPOSITOR_BTN_RIGHT && compositor_hold_cancel())
                return;

        if (under >= 0 && compositor_popup_parent[under] >= 0)
        {
                compositor_popups_close(-1, under);
                compositor.grab = compositor.grab >= 0 ? compositor.grab : under;
                compositor_pointer_to(under);
                compositor_post(under, WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN, (int)code, 0);
                if (code - COMPOSITOR_BTN_LEFT < 8)
                {
                        compositor.button_down |= (unsigned char)(1u << (code - COMPOSITOR_BTN_LEFT));
                        compositor.button_row[code - COMPOSITOR_BTN_LEFT] = under;
                }
                return;
        }
        // A press outside every popup that is open asks them to go, and is theirs: it goes no further, to the window
        // below, the desktop or a tab, with a finger the same as a mouse (compositor_touch_button is this path too).
        roots = compositor_menu_roots();
        if (compositor_popups_close(-1, -1))
        {
                compositor_cancel_holds(roots);
                return;
        }

        // The launcher that is up before this press, if there is one: the press below on the launcher's button puts it away
        // (it is asked to go here, like any press outside it) and asks for no other.
        up = compositor_launcher_row();

        // A window that asked to be put away by a press anywhere but on it (WINDOW_DISMISS) is asked to go by one on
        // the desktop, a tab, another window or the tray alike; the press then goes on as it would have.
        if (compositor.focus >= 0 && (compositor.asks[compositor.focus] & WINDOW_DISMISS) && compositor.showing[compositor.focus] &&
            !(compositor.pointer_x >= compositor.x[compositor.focus] &&
              compositor.pointer_x < compositor.x[compositor.focus] + (int)compositor.w[compositor.focus] &&
              compositor.pointer_y >= compositor.y[compositor.focus] &&
              compositor.pointer_y < compositor.y[compositor.focus] + (int)compositor.h[compositor.focus]))
                compositor_close(compositor.focus);

        // The panel's cells (the launcher's button, the microphone, the volume and the tray) take a press by the taskbar's rule
        // (taskbar_cell_press): what it does is the rule's action, and the compositor carries the action out. Each takes its
        // press alone: none of them passes it on to a window.
        if (tab == TASKBAR_LAUNCHER || tab == TASKBAR_MIC || tab == TASKBAR_VOLUME || tab == TASKBAR_NOTICES || tab == COMPOSITOR_TRAY)
        {
                switch (taskbar_cell_press(&compositor.cells, tab, code - COMPOSITOR_BTN_LEFT, clock_monotonic_nanoseconds(),
                                           compositor.pointer_x, compositor.pointer_y, compositor.scale))
                {
                case TASKBAR_ACT_LAUNCHER:
                        // with the launcher up the press puts it away, and with none it asks the script for one beside the button
                        if (up >= 0)
                        {
                                compositor_close(up);
                                compositor_tabs_changed();
                        }
                        else
                                compositor_launcher_open(compositor.pointer_x, compositor.pointer_y);
                        break;
                case TASKBAR_ACT_VOLUME_OPEN: compositor_volume_open(compositor.pointer_x, compositor.pointer_y); break;
                case TASKBAR_ACT_VOLUME_MUTE: compositor_volume_step(0, 1); break;
                case TASKBAR_ACT_MIC_OPEN: compositor_mic_open(compositor.pointer_x, compositor.pointer_y); break;
                case TASKBAR_ACT_MIC_MUTE: compositor_mic_step(); break;
                case TASKBAR_ACT_SETTINGS: compositor_settings_open(); break;
                case TASKBAR_ACT_NOTICES_OPEN: compositor_notices_open(compositor.pointer_x, compositor.pointer_y); break;
                default: break;
                }
                return;
        }

        // A press on a window's tab brings the window up, and a middle press asks it to go. The tray takes none.
        if (tab >= 0)
        {
                if (code == COMPOSITOR_BTN_MIDDLE)
                        compositor_close(compositor.listed[tab]);
                else if (code == COMPOSITOR_BTN_LEFT)
                        compositor_reveal(compositor.listed[tab]);
                return;
        }

        if (under < 0)
                return;

        if ((code == COMPOSITOR_BTN_LEFT || (code == COMPOSITOR_BTN_RIGHT && alt)) && compositor.grab < 0 && !compositor_wl_held())
        {
                unsigned int edges = compositor_edge_zone(under, compositor.pointer_x, compositor.pointer_y);

                now = clock_monotonic_nanoseconds();
                compositor_raise(under);
                compositor_focus(under);

                /*
                        A finger on the close button: the window goes when the finger lifts from it without
                        moving (compositor_touch_report), and one that moves past the slop was making the press the
                        corner or the titlebar gives (compositor_touch_resume). The X is the finger's only within its
                        disc and a little more; the rest of the corner is the corner's.
                */
                if (compositor.touching && !alt && code == COMPOSITOR_BTN_LEFT &&
                    compositor_on_button(under, compositor.pointer_x, compositor.pointer_y, compositor_close_box))
                {
                        compositor.press = COMPOSITOR_NONE;
                        compositor.touch.closing = under;
                        ui_touch_pend(&compositor.touch.gesture, 1);
                        return;
                }

                // Just outside a window's edges is the edge, to resize by. An edge resize makes
                // this ordinary geometry: a double click after it fills the screen and does not
                // restore what was. The edge the hand has is lit at once.
                if (edges && code == COMPOSITOR_BTN_LEFT)
                {
                        if (!compositor_shared_begin(under, edges))
                                compositor_resize_begin(under, edges);
                        compositor_lit(under, edges, 1);
                        return;
                }

                // With Alt held, the right button resizes from the corner nearest the hand, which a window
                // filling the output has no other way to be.
                if (alt && code == COMPOSITOR_BTN_RIGHT)
                {
                        int centre_x = compositor.x[under] + (int)compositor.w[under] / 2;
                        int centre_y = compositor.y[under] + (int)compositor.h[under] / 2;

                        compositor_resize_begin(under, (compositor.pointer_x < centre_x ? COMPOSITOR_EDGE_LEFT
                                                                                        : COMPOSITOR_EDGE_RIGHT) |
                                                           (compositor.pointer_y < centre_y ? COMPOSITOR_EDGE_TOP
                                                                                            : COMPOSITOR_EDGE_BOTTOM));
                        compositor_lit(under, compositor.edges[under], 1);
                        return;
                }

                /*
                        The top of a window is its titlebar, drawn as nothing:
                        a press there is the window's whatever is under it --
                        on the close button it asks the window to go, twice
                        quickly it fills the output or gives it back, and
                        otherwise it takes hold of the window to move it.
                        With Alt, anywhere on the window is the same. A control
                        of the window under the pointer there (a button, a tab,
                        a field: the pointer shows a hand or an I-beam over it)
                        is the window's press, not the titlebar's: its pressing
                        is what the window's controls are for.
                */
                band = !alt && code == COMPOSITOR_BTN_LEFT && !(compositor.asks[under] & WINDOW_NO_GRAB) &&
                       compositor.pointer_x >= compositor.x[under] &&
                       compositor.pointer_x < compositor.x[under] + (int)compositor.w[under] &&
                       compositor.pointer_y >= compositor.y[under] &&
                       compositor.pointer_y < compositor.y[under] + COMPOSITOR_GRAB * compositor.scale / 100;

                // The close button is the compositor's, drawn over the band, so it takes its press whatever pointer the window's
                // kit shows under it (a control's hand or I-beam): only the rest of the band gives way to a control.
                if (band && compositor_on_button(under, compositor.pointer_x, compositor.pointer_y, compositor_close_box))
                {
                        // Not one of a pair, or a second click would fill the screen on its way out.
                        compositor.press = COMPOSITOR_NONE;
                        compositor_close(under);
                        return;
                }
                // The arrow, or none asked (-1) or no pointer (-2): a control's shape is positive. The comparison is signed, or -1 and -2
                // would read as huge unsigned values and the titlebar would give way to nothing.
                top = band && compositor_asked_shape(under) <= (int)WINDOW_POINTER_ARROW;

                if (alt || top)
                {
                        // The count is kept for every press on a top (the second of a run of two, on the same window, is the pair).
                        int run = ui_click_again(&compositor.press_click, now, compositor.pointer_x, compositor.pointer_y, compositor.scale);
                        int again = run == 2 && under == compositor.press;

                        compositor.press = under;
                        if (again)
                        {
                                compositor.press = COMPOSITOR_NONE;
                                // One placed filling its screen's room is given back as a maximized one is.
                                if (compositor.arranged[under] == COMPOSITOR_MAXIMIZED ||
                                    compositor.arranged[under] == COMPOSITOR_ROOM)
                                        compositor_float(under);
                                else
                                        compositor_arrange(under, COMPOSITOR_MAXIMIZED);
                                return;
                        }
                        compositor_drag_begin(under);
                        return;
                }
        }

        // The window's own: a press inside its picture. The motion first, so it knows where.
        if (compositor.pointer_x < compositor.x[under] || compositor.pointer_y < compositor.y[under] ||
            compositor.pointer_x >= compositor.x[under] + (int)compositor.w[under] ||
            compositor.pointer_y >= compositor.y[under] + (int)compositor.h[under])
                return;

        compositor.grab = compositor.grab >= 0 ? compositor.grab : under;
        if (!compositor_wl_hold_counts(0, 0, 0))
                compositor_pointer_to(under);
        compositor_post(under, WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN, (int)code, 0);
        if (code - COMPOSITOR_BTN_LEFT < 8)
        {
                compositor.button_down |= (unsigned char)(1u << (code - COMPOSITOR_BTN_LEFT));
                compositor.button_row[code - COMPOSITOR_BTN_LEFT] = under;
                if (code == COMPOSITOR_BTN_LEFT)
                        compositor.button_x = compositor.pointer_x, compositor.button_y = compositor.pointer_y;
        }
}

/*
        Any press or release of a button, from the mouse or a finger: the minimise button's first, which takes its
        press and release alone, and then the window's (compositor_button).
*/
static void compositor_press_at(unsigned int code, int down);

// A press is at the real place of a mouse's button (compositor_at_hand).
static void compositor_press(unsigned int code, int down)
{
        compositor_at_hand(compositor_press_at, code, down);
}

static void compositor_press_at(unsigned int code, int down)
{
        if (down)
                compositor.logo_alone = 0; // a button pressed while the logo key is held is that chord's, and no tap of the key
        if (down)
                compositor_pointer_press(code, 1);
        if (down && code == COMPOSITOR_BTN_LEFT && compositor.assist_armed)
        {
                int at = compositor_assist_at(compositor.pointer_x, compositor.pointer_y);

                if (at >= 0)
                {
                        int row = (int)compositor.assist_rows[at];
                        unsigned int how = compositor.assist_zone[at];
                        int screen = compositor.assist_screen;

                        compositor_assist_close();
                        compositor_arrange_on(row, how, screen);
                        return;
                }
                compositor_assist_close();
        }
        if (!compositor_minimize_press(code, down))
                compositor_button(code, down);
}

// Whether a key of a touchpad is one of its touch and tool keys, which say where fingers are and press nothing.
static int compositor_pad_finger_key(unsigned int code)
{
        return code == COMPOSITOR_BTN_TOUCH || code == COMPOSITOR_BTN_TOOL_FINGER || code == COMPOSITOR_BTN_TOOL_DOUBLETAP ||
               code == COMPOSITOR_BTN_TOOL_TRIPLETAP || code == COMPOSITOR_BTN_TOOL_QUADTAP || code == COMPOSITOR_BTN_TOOL_QUINTTAP;
}

// A touchpad's touch and tool keys: whether a finger touches it, and how many are on it. A finger more or less, or
// none, starts the touch again: where one finger was is not where two are, and the pad says one place for them.
static void compositor_pad_key(struct compositor_pad *pad, unsigned int code, int value)
{
        int count = code == COMPOSITOR_BTN_TOOL_FINGER      ? 1
                    : code == COMPOSITOR_BTN_TOOL_DOUBLETAP ? 2
                    : code == COMPOSITOR_BTN_TOOL_TRIPLETAP ? 3
                    : code == COMPOSITOR_BTN_TOOL_QUADTAP   ? 4
                                                            : 5;

        if (code == COMPOSITOR_BTN_TOUCH)
                pad->touching = value != 0;
        else if (value)
                pad->fingers = count;
        else if (pad->fingers == count)
                pad->fingers = 0;
        pad->moving = 0;
        pad->left_x = pad->left_y = 0;
}

/*
        One report of a touchpad over: what its finger did since the last one, as a mouse's counts (x and y), or
        with two fingers or more on it as the wheel in 120ths (x sideways, y up). Which it was: 1 a move, 2 the
        wheel, and 0 neither, as the first report of a touch is, which says only where the finger is.
*/
// The trackpad's speed setting, a percent from 25 to 400 (the Scroll speed row; 0, a compositor that has read none, is 100). A pad's
// millimetre is COMPOSITOR_PAD_WHEEL of the wheel's 120ths at 100 percent, and that times the percent; a stream's notch is the gain
// (compositor_stream_gain) times the same percent.
static int compositor_speed(void)
{
        return compositor.trackpad_speed < 25 || compositor.trackpad_speed > 400 ? 100 : (int)compositor.trackpad_speed;
}

static int compositor_pad_report(struct compositor_pad *pad, int *x, int *y)
{
        long dx = (long)pad->next_x - pad->at_x, dy = (long)pad->next_y - pad->at_y;
        int kind = pad->fingers >= 2 ? 2 : 1;
        int per = kind == 2 ? COMPOSITOR_PAD_WHEEL * compositor_speed() / 100 : COMPOSITOR_PAD_COUNTS;
        long left_x, left_y;

        *x = *y = 0;
        pad->at_x = pad->next_x;
        pad->at_y = pad->next_y;
        if (!pad->touching || !pad->fingers || !pad->moving)
        {
                pad->moving = pad->touching && pad->fingers;
                return 0;
        }
        left_x = pad->left_x + dx * per;
        left_y = pad->left_y + (kind == 2 ? -dy : dy) * per;
        *x = (int)(left_x / pad->per_mm_x);
        *y = (int)(left_y / pad->per_mm_y);
        pad->left_x = (int)(left_x - (long)*x * pad->per_mm_x);
        pad->left_y = (int)(left_y - (long)*y * pad->per_mm_y);
        return *x || *y ? kind : 0;
}

/* ---- the prediction a frame ahead ---- */

// The frame a lead looks ahead by: the compositor's period, and 60 Hz before any screen has said one.
static positive compositor_predict_period(void)
{
        return compositor.period ? compositor.period : 1000000000ull / 60;
}

/*
        The lead of a class: the least-squares slope of its places over the window, times one frame (see the comment on the
        prediction), per axis; then the gates; then the cap on its length, rounded up before the scale so it is never past the
        cap, whole pixels or not.
*/
static void compositor_predict_lead(struct compositor_predict *track)
{
        const long long fx = COMPOSITOR_PREDICT_FX, period = (long long)compositor_predict_period() / 1000;
        const long long cap = (long long)COMPOSITOR_PREDICT_CAP * compositor.scale / 100 * fx;
        long long tn = (long long)track->when[track->count - 1], St = 0, Stt = 0, Sx = 0, Stx = 0, Sy = 0, Sty = 0, m, i0, i, n;
        long long denom, lead_x, lead_y, net, path, g, w, span, speed, ws, length2, length, limit, step;

        track->lead_x = track->lead_y = 0;
        n = track->count;
        i0 = n - 1;
        while (i0 > 0 && (long long)track->when[i0 - 1] >= tn - (long long)COMPOSITOR_PREDICT_WINDOW_NS)
                i0--;
        m = n - i0;
        if (m < 2 || tn - (long long)track->when[i0] < (long long)COMPOSITOR_PREDICT_SPAN_NS || cap <= 0)
                return;
        for (i = i0; i < n; i++)
        {
                long long t = ((long long)track->when[i] - tn) / 1000; /* microseconds, not after the newest */

                St += t, Stt += t * t;
                Sx += track->x[i], Stx += t * track->x[i];
                Sy += track->y[i], Sty += t * track->y[i];
        }
        denom = m * Stt - St * St;
        if (!denom)
                return;
        lead_x = (m * Stx - St * Sx) * period / denom;
        lead_y = (m * Sty - St * Sy) * period / denom;

        /* the straightness: the net move over the window against the path it took, both axes in Manhattan lengths */
        net = track->x[n - 1] - track->x[i0];
        net = (net < 0 ? -net : net);
        path = 0;
        for (i = i0 + 1; i < n; i++)
        {
                step = track->x[i] - track->x[i - 1];
                path += step < 0 ? -step : step;
                step = track->y[i] - track->y[i - 1];
                path += step < 0 ? -step : step;
        }
        step = track->y[n - 1] - track->y[i0];
        net += step < 0 ? -step : step;
        g = path > 0 ? net * 1000 / path : 0;
        w = (g - COMPOSITOR_PREDICT_STRAIGHT_LO) * 1000 / COMPOSITOR_PREDICT_STRAIGHT_SPAN;
        w = w < 0 ? 0 : w > 1000 ? 1000 : w;

        /* the speed: the net move over the window's span, in 1/1024 px a millisecond */
        span = tn - (long long)track->when[i0];
        speed = net * 1000000 / span;
        ws = (speed - COMPOSITOR_PREDICT_STILL_LO) * 1000 / (COMPOSITOR_PREDICT_STILL_HI - COMPOSITOR_PREDICT_STILL_LO);
        ws = ws < 0 ? 0 : ws > 1000 ? 1000 : ws;
        lead_x = lead_x * w / 1000 * ws / 1000;
        lead_y = lead_y * w / 1000 * ws / 1000;

        /* a turn: an axis whose newest step went back has no lead on it */
        step = track->x[n - 1] - track->x[n - 2];
        if ((step < 0 && lead_x > 0) || (step > 0 && lead_x < 0))
                lead_x = 0;
        step = track->y[n - 1] - track->y[n - 2];
        if ((step < 0 && lead_y > 0) || (step > 0 && lead_y < 0))
                lead_y = 0;

        /* the lead is shown in whole pixels: those are the cap's, rounded up before the scale, so the length shown is never past it */
        lead_x = compositor_predict_px(lead_x), lead_y = compositor_predict_px(lead_y);
        limit = cap / COMPOSITOR_PREDICT_FX;
        length2 = lead_x * lead_x + lead_y * lead_y;
        if (length2 > limit * limit)
        {
                length = (long long)compositor_isqrt((unsigned long long)length2);
                if (length * length < length2)
                        length++;
                lead_x = lead_x * limit / length;
                lead_y = lead_y * limit / length;
        }
        track->lead_x = lead_x * COMPOSITOR_PREDICT_FX, track->lead_y = lead_y * COMPOSITOR_PREDICT_FX;
}

// A class reported the place x, y (in 1/1024 px) on its device's time when, and was read at now. A place it repeats is no new
// one, and its lead stays as it was.
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
        track->x[track->count] = x, track->y[track->count] = y, track->when[track->count] = when, track->arrived[track->count] = now;
        track->count++;
        track->last = now;
        track->lead_x = track->lead_y = 0;
        if (!compositor.predict_off[cls] && track->count >= 2)
                compositor_predict_lead(track);
}

// The mouse moved. The mouse is not predicted (the owner's call): its real place is the pointer, with no lead and no sample, so
// the pointer is drawn and told where the hand is. Its times are not used.
static void compositor_predict_mouse(positive when, positive now)
{
        (void)when;
        (void)now;
        compositor.owner = COMPOSITOR_PREDICT_MOUSE;
        compositor.osk_typed = 0;
        compositor_pointer_shown();
        compositor_osk_sync();
}

// A finger or a pen is at x, y: it is the hand's real place, and the pointer is where it will be, so that is where the windows
// it reaches are told. The mouse has no lead while the finger or the pen is the pointer.
static void compositor_predict_place(unsigned int cls, int x, int y, positive now)
{
        compositor_pointer_sync();
        compositor.owner = cls;
        compositor.osk_typed = 0;
        compositor.real_x = x, compositor.real_y = y;
        compositor_predict_sample(cls, (long long)x * COMPOSITOR_PREDICT_FX, (long long)y * COMPOSITOR_PREDICT_FX, now, now);
        compositor.predict[COMPOSITOR_PREDICT_MOUSE].lead_x = compositor.predict[COMPOSITOR_PREDICT_MOUSE].lead_y = 0;
        compositor_pointer_shown();
        compositor_osk_sync();
}

// A class's lead is over (a touch lifted, a pen out of range, a stop, or its setting turned off). A mouse's pointer goes back to
// its real place, and the windows are told when it moved; a finger or a pen goes back to its newest place, and the windows are told
// where that is.
static void compositor_predict_clear(unsigned int cls)
{
        struct compositor_predict *track = &compositor.predict[cls];
        int moved_x, moved_y;

        compositor_pointer_sync();
        moved_x = compositor.pointer_x, moved_y = compositor.pointer_y;
        if (!track->lead_x && !track->lead_y)
                return;
        track->lead_x = track->lead_y = 0;
        if (cls == COMPOSITOR_PREDICT_MOUSE)
        {
                if (compositor.owner != COMPOSITOR_PREDICT_MOUSE)
                        return;
                compositor_pointer_shown();
                if (compositor.pointer_x != moved_x || compositor.pointer_y != moved_y)
                        compositor_motion();
                return;
        }
        compositor.owner = cls;
        compositor.real_x = (int)(track->x[track->count - 1] / COMPOSITOR_PREDICT_FX);
        compositor.real_y = (int)(track->y[track->count - 1] / COMPOSITOR_PREDICT_FX);
        compositor.kept_x = compositor.real_x;
        compositor.kept_y = compositor.real_y;
        compositor_pointer_shown();
        compositor.touching = cls == COMPOSITOR_PREDICT_TOUCH;
        // The cursor, a window in the hand and the windows told go back to the real place; the motion the windows are told says it is
        // no step the hand made (WINDOW_KEY_SETTLE), since a window that reads a flick off its moves (the kit's throw) takes a step
        // back at the lift, the retraction of the lead, for the finger turning.
        compositor.retracting = 1;
        compositor_motion();
        compositor.retracting = 0;
        compositor.touching = 0;
}

// The time the next lead is due to be cleared (a frame after its newest place), or 0 when no class is led.
static positive compositor_predict_due(void)
{
        positive period = compositor_predict_period(), due = 0;

        for (unsigned int cls = 0; cls < COMPOSITOR_PREDICTS; cls++)
        {
                const struct compositor_predict *track = &compositor.predict[cls];

                if ((track->lead_x || track->lead_y) && (!due || track->last + period < due))
                        due = track->last + period;
        }
        return due;
}

// The leads whose frame has gone by with no new place are cleared: a hand that stopped is back on where it stopped.
static void compositor_predict_expire(positive now)
{
        positive period = compositor_predict_period();

        for (unsigned int cls = 0; cls < COMPOSITOR_PREDICTS; cls++)
                if ((compositor.predict[cls].lead_x || compositor.predict[cls].lead_y) && now >= compositor.predict[cls].last + period)
                        compositor_predict_clear(cls);
}

// A class's prediction turned on or off, read with the other settings the compositor takes at once. The mouse is always
// predicted, so only touch and pen have a setting (`moonwater canvas predict touch`): the one row, for both.
static void compositor_predict_set(unsigned int cls, int on)
{
        compositor.predict_off[cls] = (unsigned char)!on;
        if (!on)
                compositor_predict_clear(cls);
}

static void compositor_predict_read(void)
{
        char word[16];
        long value;
        int touch;

        compositor_word(SETTING_PREDICT_TOUCH_PATH, word, sizeof(word));
        compositor_setting_kept(SETTING_PREDICT_TOUCH_PATH, word, &value);
        touch = value == 0;
        compositor_predict_set(COMPOSITOR_PREDICT_TOUCH, touch);
        compositor_predict_set(COMPOSITOR_PREDICT_PEN, touch);
}

/*
        One report of a device is over: a mouse's counts since the last one are
        one movement of the hand, with the gain of its speed, and a tablet's
        place is where the report says on the output. Both axes of either are
        one movement: taken one at a time the cursor moved twice for it, the
        second time with the other axis a report out of date, and two axes
        timed each against the last gave left and right one speed and up and
        down another.
*/
static int compositor_report(positive now, positive stamp)
{
        int moved = 0;

        compositor_pointer_sync();
        if (compositor.tablet_have)
        {
                // A finger on a touch screen has the pointer: an absolute device's place (a tablet the touch screen is not) waits for the
                // last finger's lift, so a stale place of the tablet cannot put the pointer, and a press or a hold, elsewhere.
                if (compositor.touch.gesture.mode == UI_TOUCH_NONE)
                {
                        if (compositor.tablet_have & 1)
                                compositor.real_x = compositor.tablet_x;
                        if (compositor.tablet_have & 2)
                                compositor.real_y = compositor.tablet_y;
                        compositor_predict_place(COMPOSITOR_PREDICT_PEN, compositor.real_x, compositor.real_y, now);
                        compositor.absolute = 1;
                        moved = 1;
                }
                compositor.tablet_have = 0;
        }

        if (compositor.raw_x || compositor.raw_y)
        {
                int dx = compositor.raw_x, dy = compositor.raw_y, gain;

                compositor.raw_x = compositor.raw_y = 0;
#ifdef CONFIG_MOONWATER_WAYLAND
                // A client's window holds the pointer locked: the hand's counts are its, and the pointer does not move.
                if (compositor_wl_hold_counts(dx, dy, now))
                        return moved;
#endif
                gain = compositor_gain_wanted(compositor_gain_of(dx, dy, now > compositor.stamp ? now - compositor.stamp : 0), compositor.flat,
                                              compositor.sensitivity);
                compositor.stamp = now;
                compositor.real_x = compositor_counts(compositor.real_x, compositor_accel(dx, &compositor.accel_x, gain));
                compositor.real_y = compositor_counts(compositor.real_y, compositor_accel(dy, &compositor.accel_y, gain));
#ifdef CONFIG_MOONWATER_WAYLAND
                compositor_wl_hold_keep();
#endif
                compositor_predict_mouse(stamp, now);
                moved = 1;
        }

        // A mouse, a pad or a tablet moved: the pointer is the style's own again where it is (compositor_motion).
        if (moved)
                compositor.touch.hidden = 0;
        return moved;
}

// What is under a point of its picture: the topmost window whose picture holds it, with the grip a window has just
// outside its edges (compositor_under's, for a hand that means to resize) not counted, and none over a tab.
static int compositor_picture_under(int x, int y)
{
        if (compositor_band_hit(x, y) != TASKBAR_HIT_NONE)
                return COMPOSITOR_NONE;
        for (unsigned int at = compositor.stacked; at--;)
        {
                int row = compositor.stack[at];

                if (compositor_visible(row) && x >= compositor.x[row] && x < compositor.x[row] + (int)compositor.w[row] &&
                    y >= compositor.y[row] && y < compositor.y[row] + (int)compositor.h[row])
                        return row;
        }
        return COMPOSITOR_NONE;
}

// Whether a wheel event of device at, read at now, is a stream's (UI_HAND_GAP_NS from the device's last one). The
// device's last is kept either way, so a stream that began is one for each notch after it.
static int compositor_wheel_stream(unsigned int at, positive now)
{
        positive last = compositor.wheel_at[at], since = compositor.wheel_stream_at[at];
        int within = last && now >= last && now - last <= UI_HAND_GAP_NS, stream = within;

        // A device that has streamed (COMPOSITOR_WHEEL_STREAMS notches in a gesture) keeps the stream for three seconds after its last
        // stream notch: the first notch of its next gesture is the stream's, not a row step. A device that has never streamed, or whose
        // notches come long apart, is never promoted.
        if (!within && compositor.wheel_streamed[at] && since && now >= since && now - since <= COMPOSITOR_WHEEL_MEMORY_NS)
                stream = 1;
        if (stream)
        {
                if (++compositor.wheel_run[at] >= COMPOSITOR_WHEEL_STREAMS)
                        compositor.wheel_streamed[at] = 1;
                compositor.wheel_stream_at[at] = now;
        }
        else
                compositor.wheel_run[at] = 0, compositor.stream_hist_n[at] = 0;
        compositor.wheel_at[at] = now;
        return stream;
}

// Whether a wheel event of device at, read at now, is a stream's. A mouse proper (relative, with buttons: WINDOWS_INPUT_MOUSE) never is, however
// fast its wheel is spun: its detents are notches, each the same distance, and a spin of them must not move less than a lone one does (it did:
// notches under 60 ms apart were taken for a trackpad, and moved 8 to 32 px against a lone notch's row). Only a tablet (a virtual machine's
// pointer, which a Mac's trackpad rides) can be a stream.
static int compositor_wheel_streams(unsigned int at, positive now)
{
        return (compositor.input_kinds[at] & WINDOWS_INPUT_MOUSE) ? 0 : compositor_wheel_stream(at, now);
}

/*
        A stream's gain: the pixels a stream notch moves at 100 percent, in 256ths, as a function of the notches' rate in centi-notches a
        second (compositor_stream_rate). Eight pixels up to 10 a second, then a gentle power curve, 3 x (rate - 10) / 35 to the 1.5, so
        a notch is four times as far at 45 a second (32 px). Continuous, rising, and held at the top past 45; a rate of nothing is the base.
*/
static long long compositor_stream_gain(unsigned long long rate)
{
        long long x = rate > 100000 ? 100000 : rate > 1000 ? (long long)rate - 1000 : 0;
        unsigned long long xq, f;

        if (!x)
                return COMPOSITOR_GAIN_BASE;
        xq = (unsigned long long)x * 65536 / 3500;
        xq = xq > 65536 ? 65536 : xq;
        f = xq * ui_touch_isqrt(xq * 65536) / 65536; // x to the 1.5 in 65536ths
        return COMPOSITOR_GAIN_BASE + COMPOSITOR_GAIN_BASE * 3 * (long long)f / 65536;
}

// The rate of the device's stream notches, in centi-notches a second, over the last COMPOSITOR_STREAM_HIST of them (the notch
// that is being read is the last). Fewer than two notches, or a clock that went back, is no rate.
static unsigned long long compositor_stream_rate(unsigned int at)
{
        unsigned int n = compositor.stream_hist_n[at], m = n < COMPOSITOR_STREAM_HIST ? n : COMPOSITOR_STREAM_HIST;
        positive last, first;

        if (m < 2)
                return 0;
        last = compositor.stream_hist[at][(n - 1) % COMPOSITOR_STREAM_HIST];
        first = compositor.stream_hist[at][(n - m) % COMPOSITOR_STREAM_HIST];
        if (last < first)
                return 0;
        return last == first ? 100000000000ull : (unsigned long long)(m - 1) * 100000000000ull / (last - first);
}

// The gain of a stream notch read at now: the notch goes into the device's history, then the rate of the history gives the gain.
static long long compositor_stream_gain_at(unsigned int at, positive now)
{
        unsigned int slot = compositor.stream_hist_n[at] % COMPOSITOR_STREAM_HIST;

        compositor.stream_hist[at][slot] = now;
        compositor.stream_hist_n[at]++;
        return compositor_stream_gain(compositor_stream_rate(at));
}

/*
        A stream's turn in 120ths of UI_NOTCH, for a notch of value120 (a notch is 120, the hi-res wheel's 120ths are as they are) at gain
        (256ths of a pixel a notch at 100 percent) and the trackpad's speed: value120 x gain x percent over COMPOSITOR_GAIN_TURN. What does
        not come whole is kept for the next turn.
*/
static long long compositor_stream_turn(unsigned int at, long long value120, long long gain, int horizontal)
{
        int axis = horizontal ? 1 : 0;
        long long pct = compositor_speed(), total, turn;

        value120 = value120 > 1000000000ll ? 1000000000ll : value120 < -1000000000ll ? -1000000000ll : value120;
        total = value120 * gain * pct + compositor.wheel_rest[at][axis];
        turn = total / COMPOSITOR_GAIN_TURN;
        compositor.wheel_rest[at][axis] = total - turn * COMPOSITOR_GAIN_TURN;
        return turn;
}

// The wheel goes to what holds the pointer, or to what is under it when the wheel turns. Not to what was under it when
// the pointer last moved (compositor.hover): a menu or a terminal that a key opened, a window closed or moved or
// raised by the keys, and the desktop before the first motion all lie under a pointer that has not moved, and a wheel
// sent to the window it last moved over scrolled that one, or nothing.
// Every turn of the wheel comes here, a mouse's, a stream's, a trackpad's or two fingers' on a touch screen: v120 in 120ths of a
// notch (of UI_NOTCH with WINDOW_WHEEL_PRECISE), and the window's flags. The window converts it once (ui_wheel_turn).
static void compositor_wheel(int v120, int horizontal, unsigned int flags)
{
        int row = compositor.grab;

        // The keys held are the wheel's too (Control zooms a view, Shift turns a wheel across): the window reads them from the event as it does a key's.
        flags |= compositor_modifiers() & (WINDOW_KEY_SHIFT | WINDOW_KEY_CONTROL | WINDOW_KEY_ALT);

        // A motion read with the wheel in one batch has moved the pointer and not yet kept it on a screen (that is
        // done when the batch is over): kept first, as before a button, or a flick past the edge sends the wheel to
        // no window, or to one in the room beside a shorter screen.
        compositor_pointer_clamp();
        if (row < 0 && !horizontal && taskbar_cell_of(compositor_band_hit(compositor.pointer_x, compositor.pointer_y), TASKBAR_BUTTON_WHEEL) == TASKBAR_ACT_VOLUME_STEP)
        {
                // The wheel over the tray's volume cell turns the volume, five percent a notch (120ths of one add up).
                static int notch;
                int turns = ui_notch_take(&notch, v120);

                if (turns)
                        compositor_volume_step(COMPOSITOR_VOLUME_STEP * turns, 0);
                return;
        }
        if (row < 0)
                row = compositor.hover = compositor_picture_under(compositor.pointer_x, compositor.pointer_y);
        // Where the pointer is goes first, as it does before a button: a window that turns a wheel by where the pointer
        // is (the settings window's two columns, a terminal's report to a program that asked for the mouse) has only
        // the last motion it was told of, which is none when it opened under a pointer that has not moved. Told once:
        // a wheel that reports in 120ths sends several events to a notch, and each would take two of the ring's places.
        if (row >= 0 && !compositor_wl_hold_counts(0, 0, 0) &&
            (row != compositor.told_row || compositor.pointer_x - compositor.x[row] != compositor.told_x ||
             compositor.pointer_y - compositor.y[row] != compositor.told_y))
                compositor_pointer_to(row);
        // What became of it, for moonwater debug.
        if (row < 0)
                compositor.table->wheel_nowhere++;
        else if (compositor_post(row, WINDOW_EVENT_WHEEL, flags, v120, horizontal))
                compositor.table->wheel_sent++;
        else
                compositor.table->wheel_full++;
        compositor.table->wheel_row = row;
        compositor.table->wheel_x = row < 0 ? compositor.pointer_x : compositor.pointer_x - compositor.x[row];
        compositor.table->wheel_y = row < 0 ? compositor.pointer_y : compositor.pointer_y - compositor.y[row];
}

// A touchpad's report over: its finger's move is a mouse's, and two fingers' are the wheel, counted for moonwater
// debug as the device's wheel.
/*
        A multitouch pad's slots (ABS_MT_SLOT selects one; TRACKING_ID says whether a finger is down in it, POSITION_X and Y where
        it is). The events of other codes are not the pad's.
*/
static void compositor_pad_slot(struct compositor_pad *pad, unsigned int code, int value)
{
        int slot = pad->slot;

        if (code == COMPOSITOR_ABS_MT_SLOT)
                pad->slot = value < 0 ? 0 : value >= UI_TOUCH_SLOTS ? UI_TOUCH_SLOTS - 1 : value;
        else if (code == COMPOSITOR_ABS_MT_TRACKING_ID)
                pad->mt_down[slot] = value >= 0;
        else if (code == COMPOSITOR_ABS_MT_POSITION_X)
                pad->mt_x[slot] = value;
        else if (code == COMPOSITOR_ABS_MT_POSITION_Y)
                pad->mt_y[slot] = value;
}

// The span between two slots' fingers, in micro-millimetres (the pad's units to a millimetre, per axis).
static int compositor_pad_span(const struct compositor_pad *pad, int a, int b)
{
        long long pmx = pad->per_mm_x > 0 ? pad->per_mm_x : 1, pmy = pad->per_mm_y > 0 ? pad->per_mm_y : 1;
        long long dx = (long long)(pad->mt_x[a] - pad->mt_x[b]) * 1000 / pmx, dy = (long long)(pad->mt_y[a] - pad->mt_y[b]) * 1000 / pmy;

        return (int)ui_touch_isqrt((unsigned long long)(dx * dx + dy * dy));
}

// A pinch's threshold: 4 mm of span change, about the touch screen's 16 px at 100%.
#define COMPOSITOR_PAD_PINCH_SLOP 4000

static void compositor_pinch(const struct ui_touch_event *e);

static void compositor_pad_pinch_event(int kind, int scale)
{
        struct ui_touch_event e = {UI_TOUCH_IDLE, 0, 0, 0, 0, 0};

        e.kind = (enum ui_touch_kind)kind, e.x = compositor.pointer_x, e.y = compositor.pointer_y, e.scale = scale;
        compositor_pinch(&e);
}

/*
        Two fingers on a multitouch pad: a span change past the pinch slop begins a pinch, about the pointer, as the touch screen's
        does (the same recogniser's ratio, ui_touch_ratio), and it ends when the fingers change. While a pinch is under way the
        wheel is not turned (the pan rides in the pinch's focal point; compositor_pad_apply). A parallel move with the span still
        is the wheel, as before. Answers whether a pinch is under way (the wheel is held).
*/
static int compositor_pad_pinch(struct compositor_pad *pad)
{
        int first = -1, second = -1, count = 0, span, scale;

        for (int slot = 0; slot < UI_TOUCH_SLOTS; slot++)
                if (pad->mt_down[slot])
                {
                        count++;
                        if (first < 0)
                                first = slot;
                        else if (second < 0)
                                second = slot;
                }
        if (count != pad->pinch_count)
        {
                if (pad->pinching)
                        compositor_pad_pinch_event(UI_TOUCH_PINCH_END, pad->pinch_scale);
                pad->pinching = 0, pad->pinch_scale = UI_TOUCH_PINCH_ONE;
                pad->pinch_count = count;
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
                compositor_pad_pinch_event(UI_TOUCH_PINCH_BEGIN, UI_TOUCH_PINCH_ONE);
        }
        scale = ui_touch_ratio(span, pad->pinch_ref, COMPOSITOR_PAD_PINCH_SLOP);
        if (scale != pad->pinch_scale)
        {
                pad->pinch_scale = scale;
                compositor_pad_pinch_event(UI_TOUCH_PINCH_UPDATE, scale);
        }
        return 1;
}

/*
        A hand's speed on the pad is its gain, as a person's scroll on any trackpad is: a slow hand goes one for one (precise), and a quick one
        goes further, up to COMPOSITOR_PAD_GAIN_TOP 256ths of that at COMPOSITOR_PAD_GAIN_FAST millimetres a second and above, rising in a
        line from COMPOSITOR_PAD_GAIN_SLOW. A report's turn (120ths of a notch, 12 a millimetre) over the time since the pad's last report is
        the speed; the first report of a hand, and one after a pause, have none and are not gained. What the gain leaves under a whole 120th is
        carried to the next report.
*/
#define COMPOSITOR_PAD_GAIN_SLOW 40
#define COMPOSITOR_PAD_GAIN_FAST 240
#define COMPOSITOR_PAD_GAIN_TOP 614
#define COMPOSITOR_PAD_GAIN_PAUSE_NS 100000000ull
static void compositor_pad_gain(struct compositor_pad *pad, positive now, int *x, int *y)
{
        unsigned long long since = pad->gain_at, gap = now - since;
        long long turn = (*x < 0 ? -(long long)*x : *x) > (*y < 0 ? -(long long)*y : *y) ? (*x < 0 ? -(long long)*x : *x) : (*y < 0 ? -(long long)*y : *y);
        long long speed, gain;

        pad->gain_at = now;
        if (!since || now <= since || gap > COMPOSITOR_PAD_GAIN_PAUSE_NS)
        {
                pad->gain_left_x = pad->gain_left_y = 0;
                return;
        }
        speed = turn * 1000000000ll / (long long)gap / COMPOSITOR_PAD_WHEEL; // millimetres a second
        gain = speed <= COMPOSITOR_PAD_GAIN_SLOW ? 256
               : speed >= COMPOSITOR_PAD_GAIN_FAST ? COMPOSITOR_PAD_GAIN_TOP
               : 256 + (speed - COMPOSITOR_PAD_GAIN_SLOW) * (COMPOSITOR_PAD_GAIN_TOP - 256) / (COMPOSITOR_PAD_GAIN_FAST - COMPOSITOR_PAD_GAIN_SLOW);
        {
                long long gx = (long long)*x * gain + pad->gain_left_x, gy = (long long)*y * gain + pad->gain_left_y;

                *x = (int)(gx / 256), *y = (int)(gy / 256);
                pad->gain_left_x = (int)(gx - (long long)*x * 256), pad->gain_left_y = (int)(gy - (long long)*y * 256);
        }
}

static void compositor_pad_apply(unsigned int at, positive now)
{
        int x, y, kind = compositor_pad_report(&compositor.pad[at], &x, &y);
        int pinch = compositor_pad_pinch(&compositor.pad[at]);

        if (kind == 1)
        {
                compositor.raw_x = compositor_counts(compositor.raw_x, x);
                compositor.raw_y = compositor_counts(compositor.raw_y, y);
        }
        else if (kind == 2 && !pinch)
        {
                compositor.table->input_wheel_read[at]++;
                compositor.table->input_wheel_used[at]++;
                compositor.table->input_wheel_last[at] = now;
                compositor_pad_gain(&compositor.pad[at], now, &x, &y);
                if (y)
                        compositor_wheel(y, 0, WINDOW_WHEEL_PRECISE);
                if (x)
                        compositor_wheel(x, 1, WINDOW_WHEEL_PRECISE);
                // A precise turn went out: the fingers' lift ends it (the stop is sent then, and only then).
                if (x || y)
                        compositor.pad[at].turned = 1;
        }
}

/* ---- touch: fingers on a screen ---- */

/*
        The marks of the fingers (`moonwater canvas touch on`): each one damaged where it was drawn and where it is
        drawn now, as what lies over every window, so no glass is made again for it, and nothing at all is done for
        them while the setting is off or no finger is down or fading.
*/
static void compositor_spot_undraw(int slot)
{
        int *drawn = compositor.spot[slot].drawn;

        if (!drawn[2])
                return;
        compositor.damage_by = COMPOSITOR_OVER;
        compositor_damage(drawn[0], drawn[1], drawn[2], drawn[2]);
        compositor.damage_by = 0;
        drawn[2] = 0;
}

static void compositor_spot_redraw(int slot)
{
        int radius = compositor_spot_radius(compositor.scale), *drawn = compositor.spot[slot].drawn;

        compositor_spot_undraw(slot);
        drawn[0] = compositor.spot[slot].x - radius;
        drawn[1] = compositor.spot[slot].y - radius;
        drawn[2] = 2 * radius;
        compositor.damage_by = COMPOSITOR_OVER;
        compositor_damage(drawn[0], drawn[1], drawn[2], drawn[2]);
        compositor.damage_by = 0;
}

// A finger down at x, y: its mark follows it.
static void compositor_spot_set(int slot, int x, int y)
{
        if (!compositor.spots_on || slot < 0 || slot >= UI_TOUCH_SLOTS)
                return;
        if (compositor.spot[slot].on == 1 && compositor.spot[slot].x == x && compositor.spot[slot].y == y)
                return;
        compositor.spot[slot].x = x;
        compositor.spot[slot].y = y;
        compositor.spot[slot].on = 1;
        compositor.spot[slot].alpha = 255;
        compositor.spots_shown |= 1u << slot;
        compositor.spots_fading &= ~(1u << slot);
        compositor_spot_redraw(slot);
}

// Its finger lifted: the mark fades from now (compositor_spots_tick).
static void compositor_spot_lift(int slot, positive now)
{
        if (slot < 0 || slot >= UI_TOUCH_SLOTS || compositor.spot[slot].on != 1)
                return;
        compositor.spot[slot].on = 2;
        compositor.spot[slot].lifted = now;
        compositor.spots_fading |= 1u << slot;
}

/*
        The chip (compositor.chip): a swipe's controls and indicator, their coverage made once a show (the body and the glyphs in
        integers: half pixels, a stroke's radius in half pixels, the strokes two to a pixel's ring) and drawn over the scene
        with the marks, in the processor's pixels and in the card's records (compositor_gpu_spots). The strip is three cells
        (close, minimise, maximise) on a capsule of its height; a cell is a control's target (compositor_chip_cell).
*/
// The coverage of a point by a stroke from a to b, in half pixels (a pixel's centre is 2x + 1), of a radius in half pixels: 255 within it,
// 128 on the pixel's ring, none beyond.
static unsigned int compositor_chip_stroke(long px, long py, long ax, long ay, long bx, long by, long radius)
{
        long dx = bx - ax, dy = by - ay, qx = px - ax, qy = py - ay;
        long long len2 = (long long)dx * dx + (long long)dy * dy, dot = (long long)qx * dx + (long long)qy * dy, d2, c, cross;

        if (dot <= 0)
                d2 = (long long)qx * qx + (long long)qy * qy;
        else if (dot >= len2)
                d2 = (long long)(px - bx) * (px - bx) + (long long)(py - by) * (py - by);
        else
                cross = (long long)qx * dy - (long long)qy * dx, d2 = cross * cross / len2;
        c = (long long)radius + 1 - (long long)ui_touch_isqrt((unsigned long long)d2);
        return c <= 0 ? 0 : c >= 2 ? 255 : 128;
}

// The body: a capsule of the box's height across the w x h box, at its strength.
static void compositor_chip_body(unsigned char *cover, int w, int h)
{
        long r = h / 2;

        for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++)
                        cover[y * w + x] = (unsigned char)(compositor_chip_stroke(2 * x + 1, 2 * y + 1, 2 * r, 2 * r, 2 * (w - r), 2 * r, 2 * r) *
                                                            COMPOSITOR_CHIP_BODY_COVER / 255);
}

// One stroke of a glyph into the glyph layer: the larger coverage where strokes meet. Only the pixels it can reach are looked at.
static void compositor_chip_stroke_into(unsigned char *layer, int w, int h, long ax, long ay, long bx, long by, long radius)
{
        long x0 = (ax < bx ? ax : bx) - radius / 2 - 2, x1 = (ax > bx ? ax : bx) + radius / 2 + 3;
        long y0 = (ay < by ? ay : by) - radius / 2 - 2, y1 = (ay > by ? ay : by) + radius / 2 + 3;

        x0 = x0 < 0 ? 0 : x0, y0 = y0 < 0 ? 0 : y0;
        x1 = x1 > w ? w : x1, y1 = y1 > h ? h : y1;
        for (long y = y0; y < y1; y++)
                for (long x = x0; x < x1; x++)
                {
                        unsigned int c = compositor_chip_stroke(2 * x + 1, 2 * y + 1, 2 * ax, 2 * ay, 2 * bx, 2 * by, radius);

                        if (c > layer[y * w + x])
                                layer[y * w + x] = (unsigned char)c;
                }
}

// The control cell i of a chip of cells is: 0 close, 1 minimise, 2 maximise. A chip of two has no minimise.
static int compositor_chip_kind(int cells, int i)
{
        return cells == 2 && i > 0 ? i + 1 : i;
}

// The glyphs of the cells: close (an X), minimise (a bar), maximise (a square), each a G-pixel half size about its cell's middle.
static void compositor_chip_glyphs(unsigned char *layer, int w, int h, int scale, int cells)
{
        int cell = w / cells;
        long g = COMPOSITOR_CHIP_GLYPH_DP * scale / 100, cy = h / 2, radius = 3 * scale / 200 > 2 ? 3 * scale / 200 : 2;

        for (int at = 0; at < cells; at++)
        {
                long cx = (long)at * cell + cell / 2;
                int i = compositor_chip_kind(cells, at);

                if (i == 0)
                        compositor_chip_stroke_into(layer, w, h, cx - g, cy - g, cx + g, cy + g, radius),
                            compositor_chip_stroke_into(layer, w, h, cx + g, cy - g, cx - g, cy + g, radius);
                else if (i == 1)
                        compositor_chip_stroke_into(layer, w, h, cx - g, cy, cx + g, cy, radius);
                else
                        compositor_chip_stroke_into(layer, w, h, cx - g, cy - g, cx + g, cy - g, radius),
                            compositor_chip_stroke_into(layer, w, h, cx + g, cy - g, cx + g, cy + g, radius),
                            compositor_chip_stroke_into(layer, w, h, cx + g, cy + g, cx - g, cy + g, radius),
                            compositor_chip_stroke_into(layer, w, h, cx - g, cy + g, cx - g, cy - g, radius);
        }
}

// The coverage of a chip of w x h: the body, and the glyphs of its cells after it when there are any (glyphs: how many).
static void compositor_chip_paint(int w, int h, int scale, int glyphs)
{
        unsigned char *cover = compositor.chip.cover;

        compositor_chip_body(cover, w, h);
        if (glyphs)
        {
                memory_zero(cover + (unsigned long)w * h, (positive)w * h);
                compositor_chip_glyphs(cover + (unsigned long)w * h, w, h, scale, glyphs);
        }
}

// The chip's memory, mapped once at its largest: the coverage, and the scene under it when a target is only written.
static int compositor_chip_map(void)
{
        struct compositor_chip *c = &compositor.chip;

        if (!c->cover)
                c->cover = (unsigned char *)compositor_map_zeroed(2UL * COMPOSITOR_CHIP_W_MOST * COMPOSITOR_CHIP_H_MOST);
        if (!c->scratch)
                c->scratch = (unsigned int *)compositor_map_zeroed(4UL * COMPOSITOR_CHIP_W_MOST * COMPOSITOR_CHIP_H_MOST);
        return c->cover && c->scratch;
}

// Whether the table asks for reduced motion (the kit's motion setting), when a chip slides in at once.
static int compositor_chip_reduced(void)
{
        return compositor.table && compositor.table->motion;
}

// Where the chip is drawn now, in the desktop's pixels: a controls chip follows its window and slides down into its place.
static void compositor_chip_place(int *box)
{
        const struct compositor_chip *c = &compositor.chip;

        box[2] = c->w, box[3] = c->h;
        if (c->kind == COMPOSITOR_CHIP_CONTROLS)
                box[0] = compositor.x[c->row] + c->rel_x, box[1] = compositor.y[c->row] + c->rel_y - c->slide;
        else
                box[0] = c->rel_x, box[1] = c->rel_y;
}

// A box is damaged as the chip is: over every window, so the scene under it is composed again.
static void compositor_chip_damage(const int *box)
{
        if (box[2] <= 0 || box[3] <= 0)
                return;
        compositor.damage_by = COMPOSITOR_OVER;
        compositor_damage(box[0], box[1], box[2], box[3]);
        compositor.damage_by = 0;
}

// The chip's box and alpha, when either changed since it was last drawn: the old place and the new are damaged.
static void compositor_chip_redraw(void)
{
        struct compositor_chip *c = &compositor.chip;
        int box[5], changed = 0;

        compositor_chip_place(box);
        box[4] = (int)c->alpha;
        for (int i = 0; i < 5; i++)
                changed |= box[i] != c->drawn[i];
        if (!changed)
                return;
        compositor_chip_damage(c->drawn);
        memory_copy(c->drawn, box, sizeof(c->drawn));
        compositor_chip_damage(c->drawn);
}

// The chip is taken off: its place is damaged and nothing is drawn.
static void compositor_chip_hide(void)
{
        struct compositor_chip *c = &compositor.chip;

        compositor_chip_damage(c->drawn);
        memory_zero(c->drawn, sizeof(c->drawn));
        c->kind = COMPOSITOR_CHIP_NONE;
        c->alpha = 0;
}

// The window's controls over its top band, when they fit it (a cell each, and a cell's margin either side); false when they do not. A window
// that asked for no tab has no minimise: nothing would bring it back (compositor_frame's minimise button is left out for it the same way).
static int compositor_chip_controls(int row, positive now)
{
        struct compositor_chip *c = &compositor.chip;
        int cells = row >= 0 && row < WINDOWS && (compositor.asks[row] & WINDOW_NO_TAB) ? 2 : 3;
        int scale = compositor.scale, cell = UI_EDGE_CONTROL_CELL * scale / 100, w = cells * cell, h = COMPOSITOR_CHIP_HEIGHT_DP * scale / 100;

        if (row < 0 || row >= WINDOWS || !compositor_visible(row) || cell < 1 || w > COMPOSITOR_CHIP_W_MOST || h > COMPOSITOR_CHIP_H_MOST)
                return 0;
        if ((int)compositor.w[row] < w + 2 * cell || !compositor_chip_map())
                return 0;
        compositor_chip_hide();
        compositor_chip_paint(w, h, scale, cells);
        c->kind = COMPOSITOR_CHIP_CONTROLS, c->row = row, c->cells = cells;
        c->rel_x = ((int)compositor.w[row] - w) / 2, c->rel_y = COMPOSITOR_CHIP_TOP_DP * scale / 100;
        c->w = w, c->h = h, c->alpha = 255;
        c->slide = compositor_chip_reduced() ? 0 : h;
        c->shown = now, c->hold = COMPOSITOR_CHIP_HOLD_NS, c->fade = (positive)ui_paced(COMPOSITOR_CHIP_FADE_NS);
        compositor_chip_redraw();
        return 1;
}

// The indicator of a first swipe into a window that covers its screen: a bar at the edge it came from, about the finger's place.
static void compositor_chip_hint(const int *rect, int x, int y, int side, positive now)
{
        struct compositor_chip *c = &compositor.chip;
        int scale = compositor.scale, w = COMPOSITOR_CHIP_HINT_W_DP * scale / 100, h = COMPOSITOR_CHIP_HINT_H_DP * scale / 100;
        int margin = COMPOSITOR_CHIP_HINT_MARGIN_DP * scale / 100, left = rect[0] + margin, right = rect[0] + rect[2] - w - margin;
        int top = rect[1] + margin, bottom = rect[1] + rect[3] - h - margin;

        if (side == UI_EDGE_LEFT || side == UI_EDGE_RIGHT)
                w = h, h = COMPOSITOR_CHIP_HINT_W_DP * scale / 100, top = rect[1] + margin, bottom = rect[1] + rect[3] - h - margin;
        if (w < 1 || h < 1 || w > COMPOSITOR_CHIP_W_MOST || h > COMPOSITOR_CHIP_H_MOST || !compositor_chip_map())
                return;
        compositor_chip_hide();
        compositor_chip_paint(w, h, scale, 0);
        c->kind = COMPOSITOR_CHIP_HINT, c->row = COMPOSITOR_NONE;
        if (side == UI_EDGE_LEFT || side == UI_EDGE_RIGHT)
        {
                // A bar along a side edge, about the finger's height.
                c->rel_x = side == UI_EDGE_LEFT ? rect[0] + margin : rect[0] + rect[2] - w - margin;
                c->rel_y = y - h / 2;
                c->rel_y = c->rel_y < top ? top : c->rel_y > bottom ? bottom : c->rel_y;
        }
        else
        {
                c->rel_x = x - w / 2;
                c->rel_x = c->rel_x < left ? left : c->rel_x > right ? right : c->rel_x;
                c->rel_y = side == UI_EDGE_BOTTOM ? rect[1] + rect[3] - h - margin : rect[1] + margin;
        }
        c->w = w, c->h = h, c->slide = 0, c->alpha = 255;
        c->shown = now, c->hold = COMPOSITOR_CHIP_HINT_NS, c->fade = 0;
        compositor_chip_redraw();
}

// The control of a controls chip a point is over (0 close, 1 minimise, 2 maximise), with the finger's slop above and below; -1 for none.
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
        return compositor_chip_kind(c->cells, cell >= c->cells ? c->cells - 1 : cell);
}

// Whether a point is on the chip's box (not its slop), which a press there leaves the chip up for.
static int compositor_chip_on(int x, int y)
{
        int box[4];

        if (compositor.chip.kind == COMPOSITOR_CHIP_NONE)
                return 0;
        compositor_chip_place(box);
        return x >= box[0] && x < box[0] + box[2] && y >= box[1] && y < box[1] + box[3];
}

// The chip as the time has made it: full while it holds and fading after; its slide; and at its end, gone.
static void compositor_chip_tick(positive now)
{
        struct compositor_chip *c = &compositor.chip;
        positive elapsed, total;

        if (c->kind == COMPOSITOR_CHIP_NONE)
                return;
        elapsed = now > c->shown ? now - c->shown : 0;
        total = c->hold + c->fade;
        if (elapsed >= total)
        {
                compositor_chip_hide();
                return;
        }
        c->alpha = elapsed <= c->hold ? 255 : (unsigned int)(255 * (total - elapsed) / c->fade);
        if (c->kind == COMPOSITOR_CHIP_CONTROLS)
        {
                b64 e = ui_bezier_at(&ui_ease, ui_progress((b64)elapsed, ui_paced((b64)COMPOSITOR_CHIP_SLIDE_NS)));

                c->slide = compositor_chip_reduced() ? 0 : c->h - (int)(((b64)c->h * e) / UI_ONE);
        }
        compositor_chip_redraw();
}

// The chip over a target the scene is read from, the body's ink and the glyphs', at its alpha.
static void compositor_chip_draw(const struct compositor_target *t)
{
        const struct compositor_chip *c = &compositor.chip;
        int box[4], layers = c->kind == COMPOSITOR_CHIP_HINT ? 1 : 2;

        if (c->kind == COMPOSITOR_CHIP_NONE || !c->alpha)
                return;
        compositor_chip_place(box);

        // The box cut to the target, the same for both layers: a target it does not meet (a slice above or below it) has nothing
        // of it, and its width or height, none or less, is not a count to hand the mix.
        int x1 = box[0] > t->x1 ? box[0] : t->x1, y1 = box[1] > t->y1 ? box[1] : t->y1;
        int x2 = box[0] + box[2] < t->x2 ? box[0] + box[2] : t->x2, y2 = box[1] + box[3] < t->y2 ? box[1] + box[3] : t->y2;

        if (x2 <= x1 || y2 <= y1)
                return;
        compositor_reading(t);
        for (int layer = 0; layer < layers; layer++)
        {
                unsigned int ink = c->kind == COMPOSITOR_CHIP_HINT || layer ? COMPOSITOR_CHIP_GLYPH : COMPOSITOR_CHIP_BODY;
                const unsigned char *cover = c->cover + (unsigned long)layer * c->w * c->h;

                memory_mix_const_u32_rect(t->pixels + (unsigned long)(y1 - t->oy) * t->pitch + (unsigned long)(x1 - t->ox),
                                          t->pitch,
                                          cover + (unsigned long)(y1 - box[1]) * c->w + (unsigned long)(x1 - box[0]),
                                          (positive)c->w, (positive)(x2 - x1), (positive)(y2 - y1), ink, c->alpha);
        }
}

// The chip over a target that is only written (the scanout): the scene under its box composed again into a scratch, the chip over it, and
// the box copied out, as the marks are.
static void compositor_chip_compose(const struct compositor_target *t, const struct compositor_frame *frames, unsigned int count)
{
        const struct compositor_chip *c = &compositor.chip;
        int box[4], clip[4] = {t->x1, t->y1, t->x2, t->y2};

        if (c->kind == COMPOSITOR_CHIP_NONE || !c->alpha || !c->scratch)
                return;
        compositor_chip_place(box);
        {
                int edge[4] = {box[0], box[1], box[0] + box[2], box[1] + box[3]};

                compositor_meet(edge, clip);
                if (edge[2] <= edge[0] || edge[3] <= edge[1])
                        return;
                {
                        struct compositor_target square = {c->scratch, (unsigned long)(edge[2] - edge[0]), t->width, t->height, edge[0], edge[1],
                                                           edge[2], edge[3], edge[0], edge[1], 0};

                        compositor_scene(&square, frames, count);
                        compositor_spots_marks(&square);
                        compositor_chip_draw(&square);
                        compositor_blit(t, edge[0], edge[1], edge[2] - edge[0], edge[3] - edge[1], c->scratch, (unsigned long)(edge[2] - edge[0]));
                }
        }
}

// Whether any mark is drawn, and whether any is fading, which is a frame to draw each period until it is gone. A chip is drawn while it is up,
// and fades on the same frames.
static int compositor_spots_any(int fading)
{
        return (fading ? compositor.spots_fading : compositor.spots_shown) != 0 || compositor.chip.kind != COMPOSITOR_CHIP_NONE;
}

// The fading marks at now: each fainter by the time since its finger lifted, and gone after COMPOSITOR_SPOT_FADE_NS.
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
                        compositor.spot[slot].on = 0;
                        compositor.spot[slot].alpha = 0;
                        compositor.spots_shown &= ~(1u << slot);
                        compositor.spots_fading &= ~(1u << slot);
                        continue;
                }
                alpha = (unsigned int)((COMPOSITOR_SPOT_FADE_NS - since) * 255 / COMPOSITOR_SPOT_FADE_NS);
                if (alpha != compositor.spot[slot].alpha)
                {
                        compositor.spot[slot].alpha = alpha;
                        compositor_spot_redraw((int)slot);
                }
        }
}

// Every mark taken off at once: the setting was turned off.
static void compositor_spots_clear(void)
{
        for (unsigned int slot = 0; slot < UI_TOUCH_SLOTS; slot++)
        {
                compositor_spot_undraw((int)slot);
                compositor.spot[slot].on = 0;
        }
        compositor.spots_shown = compositor.spots_fading = 0;
}

// The masks a mark is drawn with, made before the scene's bands, which draw marks into their slices at once.
static void compositor_spots_ready(void)
{
        int radius = compositor_spot_radius(compositor.scale);

        compositor_corner_mask(radius);
        compositor_corner_mask(radius - compositor_px1(compositor.scale));
}

// The fingers' marks over a target the scene was composed into and may be read (a slice of a scene composed apart).
static void compositor_spots_marks(const struct compositor_target *t)
{
        if (!compositor.spots_shown)
                return;
        for (unsigned int slot = 0; slot < UI_TOUCH_SLOTS; slot++)
                if (compositor.spot[slot].on)
                        compositor_spot_draw(t, compositor.spot[slot].x, compositor.spot[slot].y, compositor.scale,
                                             compositor.spot[slot].alpha);
}

// The marks and the chip over a target the scene was composed into and may be read (a slice of a scene composed apart).
static void compositor_spots_draw(const struct compositor_target *t)
{
        compositor_spots_marks(t);
        compositor_chip_draw(t);
}

/*
        The marks over a target that is only written (the scanout): what is under each mark is composed again into a
        scratch of its own, the marks are drawn over that, and it is copied out, as the round corners are.
*/
static void compositor_spots_compose(const struct compositor_target *t, const struct compositor_frame *frames,
                                     unsigned int count)
{
        int radius = compositor_spot_radius(compositor.scale);

        for (unsigned int slot = 0; slot < UI_TOUCH_SLOTS; slot++)
        {
                int box[4] = {compositor.spot[slot].x - radius, compositor.spot[slot].y - radius,
                              compositor.spot[slot].x + radius, compositor.spot[slot].y + radius};
                int clip[4] = {t->x1, t->y1, t->x2, t->y2};

                if (!compositor.spot[slot].on)
                        continue;
                compositor_meet(box, clip);
                if (box[2] <= box[0] || box[3] <= box[1])
                        continue;
                if (!compositor.spot_scratch)
                        compositor.spot_scratch = (unsigned int *)compositor_map_zeroed(
                            (unsigned long)4 * (2 * COMPOSITOR_MASK_MOST) * (2 * COMPOSITOR_MASK_MOST));
                if (!compositor.spot_scratch)
                        return;
                {
                        struct compositor_target square = {compositor.spot_scratch, (unsigned long)(box[2] - box[0]),
                                                           t->width, t->height, box[0], box[1], box[2], box[3],
                                                           box[0], box[1], 0};

                        compositor_scene(&square, frames, count);
                        compositor_spots_marks(&square);
                        compositor_blit(t, box[0], box[1], box[2] - box[0], box[3] - box[1], compositor.spot_scratch,
                                        (unsigned long)(box[2] - box[0]));
                }
        }
        compositor_chip_compose(t, frames, count);
}

// `moonwater canvas touch`, read with the other settings the compositor takes at once: on (the row's standard, nothing
// kept or a file that says default) or off, which takes every mark away.
// What a side edge's setting runs (COMPOSITOR_SIDE_*): the choice's index, off for a file that names none.
static int compositor_side_read(const char *path)
{
        char word[16];
        long value;

        compositor_word(path, word, sizeof(word));
        compositor_setting_kept(path, word, &value);
        return value < COMPOSITOR_SIDE_OFF || value > COMPOSITOR_SIDE_ACT ? COMPOSITOR_SIDE_OFF : (int)value;
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
        compositor.side_left = compositor_side_read(SETTING_TOUCH_LEFT_PATH);
        compositor.side_right = compositor_side_read(SETTING_TOUCH_RIGHT_PATH);
}

/*
        The screen a touch screen is: the machine's own panel when one is connected (compositor_builtin_panel), and
        else the primary. A finger's place in the device's units is a point of that screen, as a tablet's is of the
        space.
*/
static int compositor_touch_output(void)
{
        for (unsigned int at = 0; at < compositor.outputs; at++)
                if (compositor_builtin_panel(compositor.output[at].name))
                        return (int)at;
        return 0;
}

static void compositor_touch_point(unsigned int at, int slot, int *x, int *y)
{
        const struct compositor_touch_screen *screen = &compositor.screen_touch[at];
        int sx, sy, width, height;

        compositor_output_rect(compositor_touch_output(), &sx, &sy, &width, &height);
        *x = sx + compositor_tablet(screen->x[slot], screen->range[0], screen->range[1], width - 1);
        *y = sy + compositor_tablet(screen->y[slot], screen->range[2], screen->range[3], height - 1);
}

// A touch screen's event, kept until its report is over: its slot, a finger's id and its place.
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

// What a finger does, done as a finger: hit wide, and told so (WINDOW_KEY_TOUCH).
static void compositor_touch_button(unsigned int code, int down)
{
        compositor.touching = 1;
        compositor_press(code, down);
        compositor.touching = 0;
}

static void compositor_touch_motion(int x, int y, positive now)
{
        compositor.touch.x = x, compositor.touch.y = y;
        compositor_predict_place(COMPOSITOR_PREDICT_TOUCH, x, y, now);
        compositor.kept_x = compositor.real_x;
        compositor.kept_y = compositor.real_y;
        compositor.touching = 1;
        compositor_motion();
        compositor.touching = 0;
}

/*
        A finger that went down on the close button and moved past the slop was not pressing it: the press the window would
        have had there without the button is made now, from where the finger went down (the pointer is still there, a
        finger's motion waits for the slop), so the corner it is in is resized and the titlebar it is on is taken to move, and
        what the finger does next is that.
*/
static void compositor_touch_resume(void)
{
        compositor.touch.resumed = 1;
        compositor_touch_button(COMPOSITOR_BTN_LEFT, 1);
        compositor.touch.resumed = 0;
}

// The left press taken back: a release that says it was no click, for a finger that became two or was held.
static void compositor_touch_cancel(void)
{
        compositor.touching = 1;
        compositor_release_as(COMPOSITOR_BTN_LEFT, 0, WINDOW_KEY_CANCEL);
        compositor.touching = 0;
}

// A click of the right button where the first finger went down, which is where the pointer still is.
static void compositor_touch_right(void)
{
        compositor_touch_button(COMPOSITOR_BTN_RIGHT, 1);
        compositor_touch_button(COMPOSITOR_BTN_RIGHT, 0);
}

/*
        The events of the touch recogniser (ui/ui.c) applied to the windows, the pointer and the buttons: what the fingers mean is
        the kit's, what they do here is the compositor's.
*/
/*
        A pinch goes to the window its begin lands on (the window under the focal point then), and its updates and end go to that
        window, wherever the middle has gone. A window that takes pinches (the window pinch_wanted flag) is sent WINDOW_EVENT_PINCH; any other is
        sent Ctrl with a wheel of a notch for each quarter the scale has moved from the last notch (compositor_pinch_legacy), up for
        a bigger scale, down for a smaller, and no notch under 1/16 of the scale it began at.
*/
#define COMPOSITOR_PINCH_LEGACY_LOW 64 // 1024ths: the scale the legacy notches stop at (a sixteenth)
#define COMPOSITOR_PINCH_LEGACY_STEPS 16 // the most notches one update sends

static void compositor_pinch_legacy(int row, int scale)
{
        int steps = 0;

        while (steps++ < COMPOSITOR_PINCH_LEGACY_STEPS && (long)scale * 4 >= (long)compositor.touch.pinch_level * 5 &&
               compositor.touch.pinch_level < (1 << 20))
        {
                compositor.touch.pinch_level = compositor.touch.pinch_level * 5 / 4;
                compositor_post(row, WINDOW_EVENT_WHEEL, WINDOW_KEY_CONTROL, 120, 0);
        }
        while (steps++ < 2 * COMPOSITOR_PINCH_LEGACY_STEPS && (long)scale * 5 <= (long)compositor.touch.pinch_level * 4 &&
               compositor.touch.pinch_level > COMPOSITOR_PINCH_LEGACY_LOW)
        {
                compositor.touch.pinch_level = compositor.touch.pinch_level * 4 / 5;
                compositor_post(row, WINDOW_EVENT_WHEEL, WINDOW_KEY_CONTROL, -120, 0);
        }
}

static void compositor_pinch(const struct ui_touch_event *e)
{
        struct compositor_touch *touch = &compositor.touch;
        int row, x, y;
        unsigned int flags = 0;

        if (e->kind == UI_TOUCH_PINCH_BEGIN)
        {
                touch->pinch_row = compositor_picture_under(e->x, e->y);
                touch->pinch_level = WINDOW_PINCH_ONE;
                flags = WINDOW_PINCH_BEGIN;
        }
        else if (e->kind == UI_TOUCH_PINCH_END)
                flags = WINDOW_PINCH_END;
        row = touch->pinch_row;
        if (row >= 0 && compositor_visible(row))
        {
                // The focal point in the window's own pixels, kept to the packing's room (window_pinch_pack).
                x = e->x - compositor.x[row], y = e->y - compositor.y[row];
                x = x < 0 ? 0 : x > 0x7fff ? 0x7fff : x;
                y = y < 0 ? 0 : y > 0x7fff ? 0x7fff : y;
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
                        compositor.touch.hidden = 1;
                        compositor.touch.closing = COMPOSITOR_NONE;
                        compositor.touch.hold_row = COMPOSITOR_NONE;
                        compositor_touch_motion(e->x, e->y, now);
                        break;
                case UI_TOUCH_MOVE:
                        compositor_touch_motion(e->x, e->y, now);
                        break;
                case UI_TOUCH_PRESS:
                        compositor_touch_button(COMPOSITOR_BTN_LEFT, 1);
                        break;
                case UI_TOUCH_RELEASE:
                        compositor.touch.closing = COMPOSITOR_NONE;
                        compositor_predict_clear(COMPOSITOR_PREDICT_TOUCH);
                        compositor_touch_button(COMPOSITOR_BTN_LEFT, 0);
                        break;
                case UI_TOUCH_CANCEL:
                        compositor_touch_cancel();
                        break;
                case UI_TOUCH_CLOSE:
                        // Lifted unmoved on the close button it went down on: the window goes.
                        compositor_predict_clear(COMPOSITOR_PREDICT_TOUCH);
                        if (compositor.touch.closing >= 0)
                        {
                                compositor.press = COMPOSITOR_NONE;
                                compositor_close(compositor.touch.closing);
                        }
                        compositor.touch.closing = COMPOSITOR_NONE;
                        break;
                case UI_TOUCH_RESUME:
                        // A press on the X that moves is no press of it: the press it was under the X is made.
                        compositor.touch.closing = COMPOSITOR_NONE;
                        compositor_touch_resume();
                        break;
                case UI_TOUCH_RIGHT:
                        compositor_touch_right();
                        break;
                case UI_TOUCH_WHEEL:
                        compositor.touching = 1;
                        // Fingers on glass are direct: the content goes with them whatever the Scrolling group's natural direction says of a wheel and
                        // a pad, which the kit and the clients apply to every precise turn, so this one is turned the other way first.
                        compositor_wheel(compositor.table && (compositor.table->scroll_off & WINDOW_SCROLL_FLIP) ? -e->v120 : e->v120, e->horizontal, WINDOW_WHEEL_PRECISE);
                        wheel = 1;
                        break;
                case UI_TOUCH_TURN_END:
                        compositor_wheel(0, 0, WINDOW_WHEEL_PRECISE | WINDOW_WHEEL_STOP);
                        break;
                case UI_TOUCH_HOLD:
                        // The left press taken back and the right button sent are both the long press's: the cancel carries WINDOW_KEY_HOLD too.
                        {
                                int tab;

                                // The hold is at the finger's own place (the last one it reported), not at the pointer, which a second
                                // device may have left elsewhere.
                                compositor_predict_place(COMPOSITOR_PREDICT_TOUCH, compositor.touch.x, compositor.touch.y, now);
                                compositor.touch.holding = 1;
                                compositor_touch_cancel();
                                compositor.touch.hold_row = compositor_under(compositor.pointer_x, compositor.pointer_y, &tab);
                                compositor_touch_right();
                                compositor.touch.holding = 0;
                        }
                        break;
                case UI_TOUCH_HOLD_END:
                        // Lifted after a long press: the release that ends it, in the window it landed on (a terminal copies its selection).
                        if (compositor.touch.hold_row >= 0)
                                compositor_post(compositor.touch.hold_row, WINDOW_EVENT_BUTTON,
                                                WINDOW_KEY_TOUCH | WINDOW_KEY_CANCEL | WINDOW_KEY_HOLD, COMPOSITOR_BTN_LEFT, 0);
                        compositor.touch.hold_row = COMPOSITOR_NONE;
                        break;
                case UI_TOUCH_IDLE:
                        compositor_predict_clear(COMPOSITOR_PREDICT_TOUCH);
                        break;
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

/*
        A report of a touch screen over: the marks follow the fingers, and then the kit's gesture is told the fingers as they
        are, begun on the first finger down (ui_touch_begin), and each report's events are applied as they come. A report of
        another touch screen waits until this one's gesture is over.
*/
/*
        The screen's edges (struct compositor_swipe): the system's gestures from the top and the bottom of a screen. A finger that
        lands alone in such a band, on nothing the band's own cells or a window's own buttons take, is held back from the kit until
        ui_edge_judge decides it (compositor_swipe_report): a swipe is the system's, and a finger that is not one is the window's
        as if the band were not there (compositor_swipe_hand). A finger on the controls of a shown chip is a tap on one of them.
*/
#ifdef CONFIG_MOONWATER_WAYLAND
static void compositor_wl_touch_report(unsigned int at, int dropped); // wayland_host.c: the fingers as wl_touch
#endif

// The screen a point is on: its rectangle in the desktop's pixels and its number; false when no screen holds it.
static int compositor_swipe_screen(int x, int y, int *rect, int *number)
{
        unsigned int count;
        const int (*screens)[4] = compositor_screens(&count);

        for (unsigned int at = 0; at < count; at++)
                if (x >= screens[at][0] && x < screens[at][0] + screens[at][2] && y >= screens[at][1] && y < screens[at][1] + screens[at][3])
                {
                        memory_copy(rect, screens[at], 4 * sizeof(int));
                        *number = (int)at;
                        return 1;
                }
        return 0;
}

// Whether a finger landing at x, y on side may be the system's: not on a tab or a tray cell, not on a window's close or minimise button,
// with no drag or resize under way, and on a top band only over a window (row is the window under it, or COMPOSITOR_NONE).
static int compositor_swipe_free(int x, int y, int side, int *row)
{
        int tab = TASKBAR_HIT_NONE, under, on = 0;

        if (compositor.drag >= 0 || compositor.resize >= 0)
                return 0;
        under = compositor_under(x, y, &tab);
        if (tab != TASKBAR_HIT_NONE)
                return 0;
        if (under >= 0)
        {
                compositor.touching = 1;
                on = compositor_on_button(under, x, y, compositor_close_box) || compositor_on_button(under, x, y, compositor_minimize_box);
                compositor.touching = 0;
        }
        if (on || (side == UI_EDGE_TOP && under < 0))
                return 0;
        *row = under;
        return 1;
}

// A finger that landed alone, with the kit taking no gesture: a control of a shown chip, or a top or bottom band the system may take. A landing
// that is not on the chip takes it away. True when the finger is the system's until it is decided.
static int compositor_swipe_landing(unsigned int at, positive now, const struct ui_touch_frame *frame, int slot)
{
        struct compositor_swipe *swipe = &compositor.swipe;
        int x = frame->x[slot], y = frame->y[slot], rect[4] = {0, 0, 0, 0}, number = -1, row = COMPOSITOR_NONE, side = UI_EDGE_NONE, cell;
        int band = UI_EDGE_BAND * compositor.scale / 100;

        cell = compositor_chip_cell(x, y);
        if (cell >= 0)
                side = UI_EDGE_CONTROL;
        else
        {
                if (!compositor_chip_on(x, y))
                        compositor_chip_hide();
                if (!compositor_swipe_screen(x, y, rect, &number))
                        return 0;
                side = ui_edge_band_side(x, y, rect, band);
                // A window is frameless: its top band (the band a title bar would be) is the top edge the controls come from, wherever
                // the window is on its screen, as the screen's own top is.
                {
                        int tab = TASKBAR_HIT_NONE, under = compositor_under(x, y, &tab);

                        if (tab == TASKBAR_HIT_NONE && under >= 0 && y >= compositor.y[under] && y - compositor.y[under] < band)
                        {
                                // A grip of the window (its edges and corners, which take the finger for a resize) is not its top band.
                                compositor.touching = 1;
                                if (!compositor_edge_zone(under, x, y))
                                        side = UI_EDGE_TOP;
                                compositor.touching = 0;
                        }
                }
                if (side == UI_EDGE_LEFT || side == UI_EDGE_RIGHT)
                {
                        if ((side == UI_EDGE_LEFT ? compositor.side_left : compositor.side_right) == COMPOSITOR_SIDE_OFF)
                                return 0;
                }
                else if (side != UI_EDGE_TOP && side != UI_EDGE_BOTTOM)
                        return 0;
                if (!compositor_swipe_free(x, y, side, &row))
                        return 0;
        }
        swipe->state = COMPOSITOR_SWIPE_UNDECIDED, swipe->device = (int)at, swipe->slot = slot;
        swipe->side = side, swipe->cell = cell, swipe->row = row, swipe->screen = number;
        memory_copy(swipe->rect, rect, sizeof(rect));
        swipe->x0 = x, swipe->y0 = y;
        swipe->deadline = now + (positive)ui_hold_ns;
        memory_copy(&swipe->landing, frame, sizeof(*frame));
        return 1;
}

/*
        The kit's gesture fed a report: the first finger down, begun from the landing frame (ui_touch_begin: START and PRESS),
        then the frame as it is then (its pointer and busy hold, which the press may have moved), each answer applied as it
        comes; and the Wayland clients are told. Both the device's report and a swipe's held finger take this path.
*/
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

// The finger is not the system's after all, or was not decided in time: the kit is given it as it would have been without the band, its landing,
// then the report as it is now. Nothing reached the window before this.
static void compositor_swipe_hand(unsigned int at, positive now, const struct ui_touch_frame *frame)
{
        struct ui_touch_frame report;

        compositor.swipe.state = COMPOSITOR_SWIPE_NONE;
        memory_copy(&report, frame, sizeof(report));
        compositor_touch_feed(at, now, &compositor.swipe.landing, &report);
}

// The launcher, opened by a swipe up: its rectangle is a panel's thickness along the bottom of its screen, beside the finger, as the launcher
// button's is (window.c's button_*), and the script is told as it is told of the button.
static void compositor_swipe_launcher(int x, const int *rect, int number)
{
        struct windows *table = compositor.table;
        int w = COMPOSITOR_SWIPE_LAUNCHER_DP * compositor.scale / 100, h = taskbar_thick(compositor.scale);

        if (table)
        {
                table->button_x = x - w / 2, table->button_y = rect[1] + rect[3] - h;
                table->button_w = (unsigned int)w, table->button_h = (unsigned int)h;
                table->button_edge = TASKBAR_BOTTOM, table->button_screen = (unsigned int)number;
        }
        compositor.launcher_asked++;
        compositor_script_say("launcher_button");
}

// A tap on a control of the chip: the window's close, its minimise, or its maximise (restored when it is maximised), and the chip is taken away.
static void compositor_chip_press(int cell)
{
        int row = compositor.chip.row;

        compositor_chip_hide();
        if (row < 0 || row >= WINDOWS || !compositor_visible(row))
                return;
        if (cell == 0)
                compositor_close(row);
        else if (cell == 1)
                compositor_minimize_row(row);
        else if (compositor.arranged[row] == COMPOSITOR_MAXIMIZED)
                compositor_float(row);
        else
                compositor_arrange(row, COMPOSITOR_MAXIMIZED);
}

// A swipe in from a side edge that its setting runs something for: the window switcher's step, or on the left the launcher and on the right
// the volume popup, beside the finger. The launcher's rectangle is a panel's thickness along its edge, as the bottom one's is.
static void compositor_swipe_side(int side, int y, const int *rect, int number)
{
        struct windows *table = compositor.table;
        int what = side == UI_EDGE_LEFT ? compositor.side_left : compositor.side_right;
        int w = taskbar_thick(compositor.scale), h = COMPOSITOR_SWIPE_LAUNCHER_DP * compositor.scale / 100;

        if (what == COMPOSITOR_SIDE_WINDOWS)
        {
                compositor_focus_step();
                compositor_focus_commit();
                return;
        }
        if (table)
        {
                table->button_x = side == UI_EDGE_RIGHT ? rect[0] + rect[2] - w : rect[0];
                table->button_y = y - h / 2;
                table->button_w = (unsigned int)w, table->button_h = (unsigned int)h;
                table->button_edge = side == UI_EDGE_RIGHT ? TASKBAR_RIGHT : TASKBAR_LEFT, table->button_screen = (unsigned int)number;
        }
        if (side == UI_EDGE_LEFT)
        {
                compositor.launcher_asked++;
                compositor_script_say("launcher_button");
        }
        else
                compositor_script_say("volume_button");
}

// A swipe decided: the system's gesture. Up from the bottom opens the launcher beside the finger; down on a window shows its controls and
// raises the window. A first swipe into a window that covers its screen is the window's, with an indicator at its edge, and a second swipe
// within COMPOSITOR_SWIPE_ARM_NS is the system's.
static void compositor_swipe_act(unsigned int at, positive now, const struct ui_touch_frame *frame)
{
        struct compositor_swipe *swipe = &compositor.swipe;
        int row = swipe->row;

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
                if (!compositor_chip_controls(row, now))
                {
                        compositor_swipe_hand(at, now, frame);
                        return;
                }
                compositor_raise(row);
                compositor_focus(row);
        }
        else if (swipe->side == UI_EDGE_BOTTOM)
                compositor_swipe_launcher(swipe->x0, swipe->rect, swipe->screen);
        else
                compositor_swipe_side(swipe->side, swipe->y0, swipe->rect, swipe->screen);
        swipe->state = COMPOSITOR_SWIPE_SWIPED;
}

// A report of a touch screen while the edges may take the finger: true when the report is the system's (the kit is not told it), false when the
// kit takes the report as it always did. A decided swipe keeps its finger until every finger is up.
static int compositor_swipe_report(unsigned int at, positive now, const struct ui_touch_frame *frame)
{
        struct compositor_swipe *swipe = &compositor.swipe;
        int other = -1, count, slop = ui_touch_slop(compositor.scale), swipe_px = UI_EDGE_SWIPE * compositor.scale / 100, verdict;

        count = ui_touch_fingers(frame, -1, &other);
        if (swipe->state == COMPOSITOR_SWIPE_SWIPED)
        {
                if (!count)
                        swipe->state = COMPOSITOR_SWIPE_NONE;
                return 1;
        }
        if (swipe->state == COMPOSITOR_SWIPE_UNDECIDED)
        {
                if (!count)
                {
                        // Lifted with no travel: a tap on a control of the chip, or the window's click where it landed.
                        if (swipe->side == UI_EDGE_CONTROL)
                        {
                                int cell = swipe->cell;

                                swipe->state = COMPOSITOR_SWIPE_NONE;
                                compositor_chip_press(cell);
                        }
                        else
                                compositor_swipe_hand(at, now, frame);
                        return 1;
                }
                if (count >= 2 || frame->id[swipe->slot] < 0 || frame->id[swipe->slot] != swipe->landing.id[swipe->slot])
                {
                        compositor_swipe_hand(at, now, frame);
                        return 1;
                }
                verdict = ui_edge_judge(swipe->side, swipe->x0, swipe->y0, frame->x[swipe->slot], frame->y[swipe->slot], slop, swipe_px);
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

        // The trace (window.c): the gesture's mode and each finger's place in pixels, at the report.
        WINDOW_TRACE("compositor report: device %b, gesture mode %b, dropped %b\n", (bipolar)at, (bipolar)touch->gesture.mode, (bipolar)screen->dropped);
        for (int slot = 0; slot < UI_TOUCH_SLOTS; slot++)
                if (screen->id[slot] >= 0)
                {
                        int x, y;

                        compositor_touch_point(at, slot, &x, &y);
                        WINDOW_TRACE("compositor finger: slot %b tracking id %b px %b,%b\n", (bipolar)slot, (bipolar)screen->id[slot],
                                     (bipolar)x, (bipolar)y);
                }

        // Events were lost: no finger is trusted, and what they were doing ends as if they were lifted, no click made.
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
                {
                        ui_touch_drop(&touch->gesture, &out);
                        compositor_touch_apply(at, &out, now);
                }
                return;
        }
        if (touch->gesture.mode != UI_TOUCH_NONE && touch->device != (int)at)
        {
                memory_copy(screen->was, screen->id, sizeof(screen->was));
                return;
        }
        if (compositor.swipe.state != COMPOSITOR_SWIPE_NONE && compositor.swipe.device != (int)at)
        {
                memory_copy(screen->was, screen->id, sizeof(screen->was));
                return;
        }

        // The marks follow the fingers, and fade from those lifted.
        for (int slot = 0; slot < UI_TOUCH_SLOTS; slot++)
                if (screen->id[slot] >= 0)
                {
                        int x, y;

                        compositor_touch_point(at, slot, &x, &y);
                        compositor_spot_set(slot, x, y);
                }
                else if (screen->was[slot] >= 0)
                        compositor_spot_lift(slot, now);

        memory_zero(&frame, sizeof(frame));
        memory_copy(frame.id, screen->id, sizeof(frame.id));
        memory_copy(frame.was, screen->was, sizeof(frame.was));
        for (int slot = 0; slot < UI_TOUCH_SLOTS; slot++)
                if (screen->id[slot] >= 0)
                        compositor_touch_point(at, slot, &frame.x[slot], &frame.y[slot]);
        frame.scale = compositor.scale;
        frame.now = now;
        frame.pointer_x = compositor.pointer_x, frame.pointer_y = compositor.pointer_y;
        frame.busy = compositor.drag >= 0 || compositor.resize >= 0;

        // A finger in a band the system may take is the system's, or held back from the kit, until it is decided.
        if (compositor_swipe_report(at, now, &frame))
        {
                memory_copy(screen->was, screen->id, sizeof(screen->was));
                return;
        }

        compositor_touch_feed(at, now, &frame, &frame);
        memory_copy(screen->was, screen->id, sizeof(screen->was));
}

// When the first finger is a long press, or 0: the kit's time for it (a press still for ui_hold_ns, the left button down there).
static positive compositor_touch_due(void)
{
        positive due = ui_touch_due(&compositor.touch.gesture, compositor.button_down & 1);

        if (compositor.swipe.state == COMPOSITOR_SWIPE_UNDECIDED && (!due || compositor.swipe.deadline < due))
                due = compositor.swipe.deadline;
        return due;
}

// The long press's time has come: the kit's hold, applied (the left press taken back and the right button held at the pointer).
static void compositor_touch_tick(positive now)
{
        struct ui_touch_out out;

        // A finger in a band that is no swipe in time is the window's, as it landed (a slow drag, or a long press in the band).
        if (compositor.swipe.state == COMPOSITOR_SWIPE_UNDECIDED && now >= compositor.swipe.deadline)
        {
                compositor_swipe_hand((unsigned int)compositor.swipe.device, now, &compositor.swipe.landing);
                return;
        }
        ui_touch_tick(&compositor.touch.gesture, now, compositor.button_down & 1, &out);
        compositor_touch_apply((unsigned int)compositor.touch.device, &out, now);
}

/* ---- end of touch ---- */

/*
        A device's reports, read and acted on. One read takes up to sixty-four records, and a read that comes back short of that had
        the device's queue empty as it read, so the read that would answer EAGAIN is not made: a mouse's report is one read a turn
        where it was two. No more than COMPOSITOR_INPUT_READS reads are made of one device in a turn: a device that never stops (a
        thousand records and more between two polls is no hand's) is read again the next turn, its descriptor still readable, and
        the turn between them forwards what was read, takes the doors and draws the frame, where the loop stayed in this function
        for as long as the device went on.
*/
#define COMPOSITOR_INPUT_READS 16
static void compositor_input_read(unsigned int at)
{
        struct compositor_input_event raw[64];
        struct compositor_report_event events[64];
        int moved = 0;

        for (unsigned int reads = 0; reads < COMPOSITOR_INPUT_READS; reads++)
        {
                bipolar got = system_read_once(compositor.inputs[at], raw, sizeof(raw));
                positive count;

                // A device that went away answers every poll at once, so it
                // is let go of rather than read in a loop.
                if (got < 0 && got != -COMPOSITOR_EAGAIN && got != -4)
                {
                        system_close(compositor.inputs[at]);
                        compositor.inputs[at] = -1;
                        compositor.table->input_number[at] = -1;
                        compositor.devices &= ~(1ul << compositor.device_number[at]);

                        // What it held down will never be let go of: not a key, not a button, not a finger. A keyboard's keys are
                        // not told apart by device, so every key down is let go (to the window it went to); a pointer lets go of
                        // each button it may have held. A keyboard that held no button releases none another device holds.
                        compositor.held[at] = 0;
                        if (compositor.input_kinds[at] & WINDOWS_INPUT_KEYBOARD)
                                compositor_keys_release();
                        if (compositor.input_kinds[at] & (WINDOWS_INPUT_MOUSE | WINDOWS_INPUT_TABLET | WINDOWS_INPUT_BUTTONS |
                                                          WINDOWS_INPUT_TOUCH | WINDOWS_INPUT_FINGERS | WINDOWS_INPUT_DIRECT))
                        {
                                // The left is let go even with no button recorded: a drag of a titlebar or a resize has none.
                                // The pointer holds no button it was told of either, so it is its own size again.
                                compositor.cursor.held = 0;
                                compositor_release(COMPOSITOR_BTN_LEFT, 0);
                                for (unsigned int index = 1; index < 8; index++)
                                        if (compositor.button_down >> index & 1)
                                                compositor_release(COMPOSITOR_BTN_LEFT + index, 0);
                        }
                        if (compositor.touchscreen[at] && compositor.touch.gesture.mode != UI_TOUCH_NONE &&
                            compositor.touch.device == (int)at)
                                ui_touch_reset(&compositor.touch.gesture);
                        if (compositor.touchscreen[at])
                                for (int slot = 0; slot < UI_TOUCH_SLOTS; slot++)
                                        compositor_spot_lift(slot, clock_monotonic_nanoseconds());
                        compositor.touchscreen[at] = 0;
                }

                if (got <= 0)
                        break;
#ifdef MOONWATER_GPU_C
                // The first input after a pause wakes the card's idle clock (gpu_wake: a tiny job, only when the card has been idle a while)
                if (!compositor.wakeonly[at])
                        compositor_gpu_wake();
#endif

                // It woke the loop, and that was all it was for.
                if (compositor.wakeonly[at])
                        continue;

                // The whole records of what was read, each with its stamp worked out, in one pass (evdev_decode).
                count = evdev_decode((const p8 *)raw, (positive)got, (p64 *)events, 64);
                for (positive i = 0; i < count; i++)
                {
                        const struct compositor_report_event *event = &events[i];
                        positive now = event->stamp;

                        // The events this one posts carry its own stamp (the device's), not the time the loop reads them at.
                        compositor_stamp_now = now;
#ifdef CONFIG_MOONWATER_WAYLAND
                        // The idle clock (ext-idle-notify) counts from the input a person gives: keys, buttons, motion, fingers.
                        if (event->type == COMPOSITOR_EV_KEY || event->type == COMPOSITOR_EV_REL || event->type == COMPOSITOR_EV_ABS)
                                compositor_wl_input();
#endif
                        // The touch trace (window.c): a touch-down looks for its switch, and each raw event of a touch screen or a pad is a line.
                        if (compositor.touchscreen[at] || compositor.touchpad[at])
                        {
                                // A finger down is BTN_TOUCH, or, on a screen that reports slots only (a virtio multitouch device sends no BTN_TOUCH), a tracking id.
                                if ((event->type == COMPOSITOR_EV_KEY && event->code == COMPOSITOR_BTN_TOUCH && event->value) ||
                                    (event->type == COMPOSITOR_EV_ABS && event->code == COMPOSITOR_ABS_MT_TRACKING_ID && event->value >= 0))
                                        window_trace_touch();
                                WINDOW_TRACE_START();
                                WINDOW_TRACE_PART("compositor event: monotonic %p device %p ", (positive)clock_monotonic_nanoseconds(), now);
                                window_trace_word((const char address_to)compositor.input_name[at]);
                                WINDOW_TRACE_PART(" type %b code %b value %b", (bipolar)event->type, (bipolar)event->code, (bipolar)event->value);
                                WINDOW_TRACE_END();
                        }
                        if ((event->type == COMPOSITOR_EV_REL && event->code < 2) || event->type == COMPOSITOR_EV_ABS)
                                compositor.motion_ts = compositor.motion_ts ? compositor.motion_ts : now;

                        // A touch screen's events are its fingers', taken when its report is over.
                        if (compositor.touchscreen[at])
                        {
                                if (event->type == COMPOSITOR_EV_SYN && event->code == 0)
                                        compositor_touch_report(at, now);
                                else if (event->type == COMPOSITOR_EV_SYN && event->code == COMPOSITOR_SYN_DROPPED)
                                {
                                        compositor.table->input_dropped[at]++;
                                        compositor.screen_touch[at].dropped = 1;
                                }
                                else
                                        compositor_touch_event(at, event);
                                continue;
                        }

                        if (event->type == COMPOSITOR_EV_REL && event->code == COMPOSITOR_REL_X)
                        {
                                compositor.raw_x = compositor_counts(compositor.raw_x, event->value);
                                if (compositor_shaken(event->value, now))
                                        compositor.cursor.magnify = 1;
                        }
                        else if (event->type == COMPOSITOR_EV_REL && event->code == COMPOSITOR_REL_Y)
                                compositor.raw_y = compositor_counts(compositor.raw_y, event->value);
                        else if (event->type == COMPOSITOR_EV_REL &&
                                 (event->code == COMPOSITOR_REL_WHEEL || event->code == COMPOSITOR_REL_HWHEEL ||
                                  event->code == COMPOSITOR_REL_WHEEL_HI || event->code == COMPOSITOR_REL_HWHEEL_HI))
                        {
                                int fine = event->code == COMPOSITOR_REL_WHEEL_HI || event->code == COMPOSITOR_REL_HWHEEL_HI;
                                int sideways = event->code == COMPOSITOR_REL_HWHEEL || event->code == COMPOSITOR_REL_HWHEEL_HI;

                                compositor.table->input_wheel_read[at]++;
                                compositor.table->input_wheel_last[at] = now;
                                // A wheel that reports in 120ths is read from those, and its notches are not counted again.
                                if (fine || !(compositor.hires[at] >> sideways & 1))
                                {
                                        // A stream's notches are the finger's motion, one for one; a mouse's are stepped (compositor_wheel_stream).
                                        int stream = compositor_wheel_streams(at, now);

                                        if (compositor.table)
                                                window_wheel_note(compositor.table, at, now, stream);

                                        compositor.table->input_wheel_used[at]++;
                                        long long value120 = fine ? (long long)event->value : (long long)event->value * 120;
                                        unsigned int flags = 0;

                                        if (stream)
                                        {
                                                long long gain = compositor_stream_gain_at(at, now);

                                                value120 = compositor_stream_turn(at, value120, gain, sideways);
                                                flags = WINDOW_WHEEL_PRECISE | WINDOW_WHEEL_STREAM;
                                        }
                                        compositor_wheel(compositor_counts(0, value120), sideways, flags);
                                }
                        }
                        else if (event->type == COMPOSITOR_EV_SYN && event->code == COMPOSITOR_SYN_DROPPED)
                        {
                                compositor.table->input_dropped[at]++;
                                // What the kernel dropped may be a lift and a touch somewhere else: the touchpad's
                                // next report says where the finger is, and is no move from where it was before.
                                if (compositor.touchpad[at])
                                {
                                        compositor.pad[at].moving = 0;
                                        compositor.pad[at].left_x = compositor.pad[at].left_y = 0;
                                }
                        }
                        else if (event->type == COMPOSITOR_EV_ABS && compositor.touchpad[at] && event->code >= COMPOSITOR_ABS_MT_SLOT &&
                                 event->code <= COMPOSITOR_ABS_MT_TRACKING_ID)
                                compositor_pad_slot(&compositor.pad[at], event->code, event->value);
                        else if (event->type == COMPOSITOR_EV_ABS && event->code < 2 && compositor.touchpad[at])
                        {
                                if (event->code)
                                        compositor.pad[at].next_y = event->value;
                                else
                                        compositor.pad[at].next_x = event->value;
                        }
                        else if (event->type == COMPOSITOR_EV_ABS && event->code < 2)
                        {
                                int *range = event->code ? compositor.absolute_y[at] : compositor.absolute_x[at];
                                int place = compositor_tablet(event->value, range[0], range[1],
                                                              event->code ? compositor.height : compositor.width);

                                if (event->code)
                                        compositor.tablet_y = place;
                                else
                                        compositor.tablet_x = place;
                                compositor.tablet_have |= event->code ? 2 : 1;
                        }
                        else if (event->type == COMPOSITOR_EV_SYN && event->code == 0)
                        {
                                if (compositor.touchpad[at])
                                        compositor_pad_apply(at, now);
                                moved |= compositor_report(now, now);
                        }
                        else if (event->type == COMPOSITOR_EV_KEY && compositor.touchpad[at] &&
                                 compositor_pad_finger_key(event->code))
                        {
                                // The last finger lifting ends the precise gesture: the region in hand lets go at its lift (WINDOW_WHEEL_STOP).
                                // Fingers that turned no wheel (a move of the pointer, a tap, a click) end none, and their lift sends nothing:
                                // a stop is a wheel event to the window under the pointer, which an open menu there is not to be closed by.
                                b32 down = compositor.pad[at].touching || compositor.pad[at].fingers;

                                compositor_pad_key(&compositor.pad[at], event->code, event->value);
                                if (down && !compositor.pad[at].touching && !compositor.pad[at].fingers)
                                {
                                        if (compositor.pad[at].turned)
                                                compositor_wheel(0, 0, WINDOW_WHEEL_PRECISE | WINDOW_WHEEL_STOP);
                                        compositor.pad[at].turned = 0;
                                }
                        }
                        else if (event->type == COMPOSITOR_EV_KEY && event->code == COMPOSITOR_BTN_TOOL_PEN && !event->value &&
                                 !compositor.touchpad[at] && !compositor.touchscreen[at])
                                compositor_predict_clear(COMPOSITOR_PREDICT_PEN);
                        else if (event->type == COMPOSITOR_EV_KEY &&
                                 ((event->code >= COMPOSITOR_BTN_LEFT && event->code <= COMPOSITOR_BTN_TASK) ||
                                  event->code == COMPOSITOR_BTN_TOUCH))
                        {
                                // A touch is the left button (a touchpad's is no press, above); the others are where
                                // they are, but a touchpad's left pressed with two fingers on it, which is its right.
                                unsigned int code = event->code == COMPOSITOR_BTN_TOUCH ? COMPOSITOR_BTN_LEFT : event->code;

                                if (compositor.touchpad[at] && code == COMPOSITOR_BTN_LEFT)
                                {
                                        if (event->value)
                                                compositor.pad[at].clicked = compositor.pad[at].fingers >= 2 ? COMPOSITOR_BTN_RIGHT
                                                                                                             : COMPOSITOR_BTN_LEFT;
                                        code = compositor.pad[at].clicked ? compositor.pad[at].clicked : code;
                                }
                                moved |= compositor_report(now, now);
                                if (moved)
                                        compositor_motion(), moved = 0;
                                compositor_press(code, event->value != 0);
                        }
                        else if (event->type == COMPOSITOR_EV_KEY && event->code < 256)
                        {
                                // A key of a physical keyboard is the last input (the on-screen keyboard's auto rule).
                                if (event->value == 1 && (compositor.input_kinds[at] & WINDOWS_INPUT_KEYBOARD))
                                        compositor.osk_typed = 1;
                                compositor_key(at, event->code, event->value, now);
                        }
                }
                if (got < (bipolar)sizeof(raw))
                        break;
        }

        if (moved)
                compositor_motion();
        compositor_stamp_now = 0;

        if (!compositor.wakeonly[at])
                compositor_hold_touch(clock_monotonic_nanoseconds());
}
