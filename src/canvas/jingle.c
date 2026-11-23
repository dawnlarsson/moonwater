/*
        Canvas notification & UI jingle system: synthesizes rich, lush 48 kHz stereo PCM chimes and plays them
        through the sound server's sample cache (src/audio/pulse.c via src/audio/control.c).

        Frequency & mixing design:
        - All fundamentals sit at or above 784 Hz (G5) to stay completely clear of low-mid mud (200-500 Hz)
          and conversational vocal fundamentals (~100-500 Hz), so chimes never muddy game audio or music.
        - Additive partials place bell presence in the 2.5-4.3 kHz sweet spot and a brief airy shimmer
          in 5.0-7.5 kHz with fast decay, cutting cleanly through speech and dense mixes without harshness.
        - Micro-detuned stereo unison (+-1.5 cents) and gentle panning/echo create a warm, spacious stereo image.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/
#ifndef MOONWATER_CANVAS_JINGLE
#define MOONWATER_CANVAS_JINGLE

#define CANVAS_JINGLE_RATE 48000
#define CANVAS_JINGLE_CHANNELS 2
#define CANVAS_JINGLE_PI 3.14159265358979323846
#define CANVAS_JINGLE_MAX_FRAMES 31200 // 650 ms at 48 kHz stereo S16LE
#define CANVAS_JINGLE_RETRY_MS 2000
#define CANVAS_JINGLE_CFG_TTL_MS 500
#define CANVAS_JINGLES_PATH "/root/canvas.jingles"

enum
{
        CANVAS_JINGLE_VOLUME = 0, // crisp upper-mid tap when adjusting volume
        CANVAS_JINGLE_POP,        // bubbly two-tone pop when a popup opens
        CANVAS_JINGLE_NOTICE,     // glassy bell chime for a new system notice
        CANVAS_JINGLE_WELCOME,    // lush ascending major-9th arpeggio on desktop start
        CANVAS_JINGLE_COUNT
};

struct canvas_jingle
{
        long last_ms[CANVAS_JINGLE_COUNT];
        long retry_ms;
        long cfg_until_ms;
        int cfg_enabled;
};

static const char *canvas_jingle_name(int id)
{
        switch (id)
        {
        case CANVAS_JINGLE_VOLUME: return "moonwater-volume";
        case CANVAS_JINGLE_POP: return "moonwater-pop";
        case CANVAS_JINGLE_NOTICE: return "moonwater-notice";
        case CANVAS_JINGLE_WELCOME: return "moonwater-welcome";
        default: return 0;
        }
}

static inline short canvas_jingle_clamp(long v)
{
        return v > 32767 ? 32767 : v < -32767 ? -32767 : (short)v;
}

// Additive stereo bell/chime note using recursive 2-multiply oscillators (zero trig calls in the sample loop).
static void canvas_jingle_note(short *buf, int total, int start_ms, int len_ms, f64 freq, f64 amp, f64 pan)
{
        int start = start_ms * (CANVAS_JINGLE_RATE / 1000);
        int len = len_ms * (CANVAS_JINGLE_RATE / 1000);
        int attack = 5 * (CANVAS_JINGLE_RATE / 1000);
        f64 gain_l = amp * (1.0 - pan * 0.35), gain_r = amp * (1.0 + pan * 0.35);
        f64 detune = 0.00087; // ~1.5 cents L/R spread for lush stereo width
        f64 wl = 2.0 * CANVAS_JINGLE_PI * freq * (1.0 - detune) / (f64)CANVAS_JINGLE_RATE;
        f64 wr = 2.0 * CANVAS_JINGLE_PI * freq * (1.0 + detune) / (f64)CANVAS_JINGLE_RATE;
        f64 mult[4] = {1.0, 2.0, 2.76, 4.02};
        f64 cl[4], cr[4], s1l[4], s2l[4], s1r[4], s2r[4];

        if (start < 0 || len <= 0 || start >= total)
                return;
        if (start + len > total)
                len = total - start;
        for (int k = 0; k < 4; k++)
        {
                f64 al = wl * mult[k], ar = wr * mult[k];
                cl[k] = 2.0 * (f64)cosine((decimal)al), s2l[k] = 0.0, s1l[k] = (f64)sine((decimal)al);
                cr[k] = 2.0 * (f64)cosine((decimal)ar), s2r[k] = 0.0, s1r[k] = (f64)sine((decimal)ar);
        }
        for (int i = 0; i < len; i++)
        {
                f64 u = (f64)i / (f64)len;
                f64 fade = i < attack ? (f64)i / (f64)attack : 1.0;
                f64 env_body = (1.0 - u) * (1.0 - u) * (1.0 - u * 0.5);
                f64 env_mid = env_body * (1.0 - u) * (1.0 - u);
                f64 env_pres = env_mid * (1.0 - u) * (1.0 - u);
                f64 env_air = i < len / 3 ? (1.0 - (f64)i / (f64)(len / 3)) : 0.0;
                f64 sl = s2l[0] * env_body + 0.28 * s2l[1] * env_mid + 0.16 * s2l[2] * env_pres + 0.09 * s2l[3] * (env_air * env_air * env_air);
                f64 sr = s2r[0] * env_body + 0.28 * s2r[1] * env_mid + 0.16 * s2r[2] * env_pres + 0.09 * s2r[3] * (env_air * env_air * env_air);
                int idx = (start + i) * 2;

                buf[idx] = canvas_jingle_clamp((long)buf[idx] + (long)(sl * fade * gain_l * 32767.0));
                buf[idx + 1] = canvas_jingle_clamp((long)buf[idx + 1] + (long)(sr * fade * gain_r * 32767.0));
                for (int k = 0; k < 4; k++)
                {
                        f64 nl = cl[k] * s1l[k] - s2l[k];
                        f64 nr = cr[k] * s1r[k] - s2r[k];
                        s2l[k] = s1l[k], s1l[k] = nl;
                        s2r[k] = s1r[k], s1r[k] = nr;
                }
        }
}

// Gentle crossed-stereo shimmer tail for spaciousness without muddying transients.
static void canvas_jingle_echo(short *buf, int total, int delay_ms, f64 feedback)
{
        int d = delay_ms * (CANVAS_JINGLE_RATE / 1000);

        if (d <= 0 || d >= total)
                return;
        for (int i = d; i < total; i++)
        {
                int dst = i * 2, src = (i - d) * 2;
                long l = (long)buf[dst] + (long)((f64)buf[src + 1] * feedback);
                long r = (long)buf[dst + 1] + (long)((f64)buf[src] * feedback);

                buf[dst] = canvas_jingle_clamp(l);
                buf[dst + 1] = canvas_jingle_clamp(r);
        }
}

static int canvas_jingle_synth(int id, short *buf, int cap)
{
        int frames = 0;

        switch (id)
        {
        case CANVAS_JINGLE_VOLUME:
                frames = 65 * (CANVAS_JINGLE_RATE / 1000);
                if (frames > cap) frames = cap;
                memory_zero(buf, (positive)frames * CANVAS_JINGLE_CHANNELS * sizeof(short));
                canvas_jingle_note(buf, frames, 0, 65, 987.77, 0.22, 0.0); // B5
                break;
        case CANVAS_JINGLE_POP:
                frames = 115 * (CANVAS_JINGLE_RATE / 1000);
                if (frames > cap) frames = cap;
                memory_zero(buf, (positive)frames * CANVAS_JINGLE_CHANNELS * sizeof(short));
                canvas_jingle_note(buf, frames, 0, 60, 880.00, 0.18, -0.20);   // A5
                canvas_jingle_note(buf, frames, 24, 90, 1318.51, 0.20, 0.20);  // E6
                break;
        case CANVAS_JINGLE_NOTICE:
                frames = 360 * (CANVAS_JINGLE_RATE / 1000);
                if (frames > cap) frames = cap;
                memory_zero(buf, (positive)frames * CANVAS_JINGLE_CHANNELS * sizeof(short));
                canvas_jingle_note(buf, frames, 0, 210, 1046.50, 0.19, -0.25); // C6
                canvas_jingle_note(buf, frames, 75, 240, 1567.98, 0.20, 0.25); // G6
                canvas_jingle_echo(buf, frames, 70, 0.16);
                break;
        case CANVAS_JINGLE_WELCOME:
                frames = 650 * (CANVAS_JINGLE_RATE / 1000);
                if (frames > cap) frames = cap;
                memory_zero(buf, (positive)frames * CANVAS_JINGLE_CHANNELS * sizeof(short));
                canvas_jingle_note(buf, frames, 0, 380, 783.99, 0.14, -0.35);   // G5
                canvas_jingle_note(buf, frames, 70, 380, 987.77, 0.14, -0.12);  // B5
                canvas_jingle_note(buf, frames, 140, 380, 1174.66, 0.14, 0.12); // D6
                canvas_jingle_note(buf, frames, 210, 400, 1567.98, 0.16, 0.35); // G6
                canvas_jingle_echo(buf, frames, 75, 0.18);
                break;
        default:
                return 0;
        }
        return frames;
}

static int canvas_jingles_enabled(struct canvas_jingle *j, long now_ms)
{
        if (j && j->cfg_until_ms && now_ms < j->cfg_until_ms)
                return j->cfg_enabled;
        int on = 1;
#ifndef CTL_NO_SYSTEM
        char buf[16];
        long fd = ctl_call(syscall(openat), (positive)-100, (positive)CANVAS_JINGLES_PATH, 0, 0, 0, 0);
        long n;

        if (fd >= 0)
        {
                n = ctl_call(syscall(read), (positive)fd, (positive)buf, (positive)(sizeof(buf) - 1), 0, 0, 0);
                ctl_call(syscall(close), (positive)fd, 0, 0, 0, 0, 0);
                if (n >= 3 && buf[0] == 'o' && buf[1] == 'f' && buf[2] == 'f')
                        on = 0;
        }
#endif
        if (j)
                j->cfg_enabled = on, j->cfg_until_ms = now_ms + CANVAS_JINGLE_CFG_TTL_MS;
        return on;
}

static long canvas_jingle_play(struct ctl *c, struct canvas_jingle *j, const char *socket_path, int id, unsigned long volume, long now_ms, long min_gap_ms)
{
        static short pcm[CANVAS_JINGLE_MAX_FRAMES * CANVAS_JINGLE_CHANNELS];
        const char *name = canvas_jingle_name(id);
        long ans;

        if (!c || !name)
                return CTL_INVALID;
        if (!canvas_jingles_enabled(j, now_ms))
                return 0;
        if (j && min_gap_ms > 0 && j->last_ms[id] && now_ms - j->last_ms[id] < min_gap_ms)
                return 0;
        if (!c->open)
        {
                if (!socket_path || (j && j->retry_ms && now_ms < j->retry_ms))
                        return CTL_FAILED;
                if (ctl_connect(c, socket_path) < 0)
                {
                        if (j)
                                j->retry_ms = now_ms + CANVAS_JINGLE_RETRY_MS;
                        return CTL_FAILED;
                }
        }
        ans = ctl_play_sample(c, name, volume);
        if (ans == CTL_NOENTITY)
        {
                int frames = canvas_jingle_synth(id, pcm, CANVAS_JINGLE_MAX_FRAMES);

                if (frames <= 0)
                        return CTL_INVALID;
                ans = ctl_upload_sample(c, name, CTL_FORMAT_S16LE, CANVAS_JINGLE_CHANNELS, CANVAS_JINGLE_RATE, pcm,
                                        (unsigned long)frames * CANVAS_JINGLE_CHANNELS * sizeof(short));
                if (ans == 0)
                        ans = ctl_play_sample(c, name, volume);
        }
        if (ans == CTL_FAILED)
        {
                ctl_disconnect(c);
                if (j)
                        j->retry_ms = now_ms + CANVAS_JINGLE_RETRY_MS;
                return CTL_FAILED;
        }
        if (ans == 0 && j)
                j->last_ms[id] = now_ms;
        return ans;
}

#endif // MOONWATER_CANVAS_JINGLE
