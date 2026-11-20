/*
        The audio gauntlet's libpulse hammer: one scenario a run, against the server PULSE_SERVER names, each held to a
        deadline so a stream that never starts, a drain that never ends or an operation never answered is a failure and not
        a hang. Every scenario plays a sine, so the server's mix (audio -o) has sound in it when the stream really played.

            pa_hammer SCENARIO [ARG...]     exit 0 pass, 1 fail (the reason on stderr), 2 usage

        Scenarios (the patterns of the libraries that drive PulseAudio, old and new):
            chunks RATE FMT CH CHUNK TLEN_CHUNKS ADJUST   SDL 1.2 and 2's: a whole chunk written while one fits
            callback RATE FMT CH TLEN_MS                  SDL 3, miniaudio, OpenAL: exactly what the write callback asks
            drain-unstarted                               a stream below its prebuf drained (a program's quit)
            corked-start | cork-toggle | flush | trigger | prebuf0 | set-attr | update-rate
            many N                                        N streams at once
            churn N                                       N streams made and deleted one after another
            disconnect                                    the connection closed mid-stream
            latency                                       timing info and the latency asked of a playing stream
            record                                        a capture stream read for a second

        Built in the Arch bowl: gcc -O2 pa_hammer.c -o pa_hammer $(pkg-config --cflags --libs libpulse) -lm
*/
#include <math.h>
#include <pulse/pulseaudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static pa_mainloop *loop;
static pa_context *ctx;
static double phase;

static double now(void)
{
        struct timespec t;

        clock_gettime(CLOCK_MONOTONIC, &t);
        return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static int fail(const char *why)
{
        fprintf(stderr, "FAIL %s\n", why);
        exit(1);
}

// The loop turned until *flag or the deadline (seconds); 0 on time.
static int run_until(volatile int *flag, double seconds)
{
        double end = now() + seconds;

        while (!*flag)
        {
                if (now() > end)
                        return -1;
                if (pa_mainloop_iterate(loop, 0, NULL) < 0)
                        return -1;
                if (!*flag)
                {
                        struct timespec t = {0, 1000000};
                        nanosleep(&t, NULL);
                }
        }
        return 0;
}

static void spin(double seconds)
{
        double end = now() + seconds;

        while (now() < end)
        {
                pa_mainloop_iterate(loop, 0, NULL);
                struct timespec t = {0, 1000000};
                nanosleep(&t, NULL);
        }
}

static volatile int ctx_ready;
static void ctx_state(pa_context *c, void *u)
{
        (void)u;
        pa_context_state_t s = pa_context_get_state(c);
        if (s == PA_CONTEXT_READY)
                ctx_ready = 1;
        else if (s == PA_CONTEXT_FAILED || s == PA_CONTEXT_TERMINATED)
                ctx_ready = -1;
}

static void connect_context(void)
{
        loop = pa_mainloop_new();
        ctx = pa_context_new(pa_mainloop_get_api(loop), "gauntlet pa_hammer");
        pa_context_set_state_callback(ctx, ctx_state, NULL);
        if (pa_context_connect(ctx, NULL, PA_CONTEXT_NOAUTOSPAWN, NULL) < 0)
                fail("connect refused");
        double end = now() + 5;
        while (!ctx_ready && now() < end)
                pa_mainloop_iterate(loop, 0, NULL), spin(0.001);
        if (ctx_ready != 1)
                fail("context never ready");
}

static pa_sample_format_t format_of(const char *f)
{
        if (!strcmp(f, "s16"))
                return PA_SAMPLE_S16LE;
        if (!strcmp(f, "f32"))
                return PA_SAMPLE_FLOAT32LE;
        if (!strcmp(f, "s32"))
                return PA_SAMPLE_S32LE;
        if (!strcmp(f, "s24"))
                return PA_SAMPLE_S24LE;
        if (!strcmp(f, "u8"))
                return PA_SAMPLE_U8;
        fail("unknown format");
        return PA_SAMPLE_INVALID;
}

// count bytes of a 440 Hz sine in the spec's format.
static void sine(void *into, size_t bytes, const pa_sample_spec *ss)
{
        size_t frame = pa_frame_size(ss), frames = bytes / frame;
        unsigned char *p = into;

        for (size_t i = 0; i < frames; i++)
        {
                double v = 0.25 * sin(phase);
                phase += 2 * M_PI * 440.0 / ss->rate;
                for (unsigned c = 0; c < ss->channels; c++)
                {
                        switch (ss->format)
                        {
                        case PA_SAMPLE_S16LE: { short s = (short)(v * 32767); memcpy(p, &s, 2), p += 2; break; }
                        case PA_SAMPLE_FLOAT32LE: { float s = (float)v; memcpy(p, &s, 4), p += 4; break; }
                        case PA_SAMPLE_S32LE: { int s = (int)(v * 2147483647.0); memcpy(p, &s, 4), p += 4; break; }
                        case PA_SAMPLE_S24LE: { int s = (int)(v * 8388607.0); p[0] = (unsigned char)s, p[1] = (unsigned char)(s >> 8), p[2] = (unsigned char)(s >> 16), p += 3; break; }
                        case PA_SAMPLE_U8: *p++ = (unsigned char)(128 + v * 127); break;
                        default: break;
                        }
                }
        }
}

static volatile int stream_ready;
static void stream_state(pa_stream *s, void *u)
{
        (void)u;
        pa_stream_state_t st = pa_stream_get_state(s);
        if (st == PA_STREAM_READY)
                stream_ready = 1;
        else if (st == PA_STREAM_FAILED || st == PA_STREAM_TERMINATED)
                stream_ready = -1;
}

static pa_stream *make_stream(const pa_sample_spec *ss, const pa_buffer_attr *attr, pa_stream_flags_t flags)
{
        pa_stream *s = pa_stream_new(ctx, "gauntlet", ss, NULL);

        if (!s)
                fail("pa_stream_new");
        stream_ready = 0;
        pa_stream_set_state_callback(s, stream_state, NULL);
        if (pa_stream_connect_playback(s, NULL, attr, flags, NULL, NULL) < 0)
                fail("connect_playback refused");
        if (run_until(&stream_ready, 5) < 0 || stream_ready != 1)
                fail("stream never ready");
        return s;
}

static volatile int op_done;
static void op_success(pa_stream *s, int ok, void *u)
{
        (void)s, (void)u;
        op_done = ok ? 1 : 2;
}

// An operation waited for; fails when it is not answered within seconds or answers failure (unless allowed).
static void wait_op(pa_operation *o, double seconds, const char *what)
{
        char why[160];

        if (!o)
        {
                snprintf(why, sizeof why, "%s: no operation", what);
                fail(why);
        }
        if (run_until(&op_done, seconds) < 0)
        {
                snprintf(why, sizeof why, "%s: not answered in %.1f s", what, seconds);
                fail(why);
        }
        pa_operation_unref(o);
}

// Bytes played (the stream's read index) as the server says them.
static long long played(pa_stream *s)
{
        const pa_timing_info *t;

        op_done = 0;
        wait_op(pa_stream_update_timing_info(s, op_success, NULL), 3, "timing info");
        t = pa_stream_get_timing_info(s);
        return t ? (long long)t->read_index : -1;
}

/* The write patterns. */
static unsigned char buffer[1 << 20];

// SDL 1.2 and 2: a whole chunk written each time the stream may take one, for seconds; the stream must play.
static int pattern_chunks(pa_stream *s, const pa_sample_spec *ss, size_t chunk, double seconds)
{
        double end = now() + seconds;
        size_t total = 0, want = (size_t)(pa_bytes_per_second(ss) * seconds);

        while (now() < end && total < want)
        {
                pa_mainloop_iterate(loop, 0, NULL);
                if (pa_stream_writable_size(s) >= chunk)
                {
                        sine(buffer, chunk, ss);
                        if (pa_stream_write(s, buffer, chunk, NULL, 0, PA_SEEK_RELATIVE) < 0)
                                fail("write refused");
                        total += chunk;
                }
                else
                {
                        struct timespec t = {0, 500000};
                        nanosleep(&t, NULL);
                }
        }
        // Played for real: at least half a second's worth of what was written has been read by the server.
        if (total < want / 2)
        {
                char why[200];
                snprintf(why, sizeof why, "stalled: %zu of %zu bytes written in %.1f s (a stream that never starts asks for nothing more)", total, want, seconds * 2);
                fail(why);
        }
        return 0;
}

// SDL 3, miniaudio, OpenAL: exactly what the write callback asks, for seconds.
static size_t callback_total;
static const pa_sample_spec *callback_spec;
static void write_cb(pa_stream *s, size_t nbytes, void *u)
{
        (void)u;
        while (nbytes)
        {
                size_t n = nbytes > sizeof buffer ? sizeof buffer : nbytes;
                n -= n % pa_frame_size(callback_spec);
                if (!n)
                        break;
                sine(buffer, n, callback_spec);
                pa_stream_write(s, buffer, n, NULL, 0, PA_SEEK_RELATIVE);
                callback_total += n, nbytes -= n;
        }
}

static void play_sine(pa_stream *s, const pa_sample_spec *ss, double seconds)
{
        size_t frame = pa_frame_size(ss);

        pattern_chunks(s, ss, 1024 * frame, seconds);
}

static pa_sample_spec stereo48 = {PA_SAMPLE_S16LE, 48000, 2};

static int drain_stream(pa_stream *s, double seconds)
{
        op_done = 0;
        wait_op(pa_stream_drain(s, op_success, NULL), seconds, "drain");
        return 0;
}

int main(int argc, char **argv)
{
        if (argc < 2)
                return 2;
        connect_context();
        const char *what = argv[1];

        if (!strcmp(what, "chunks") && argc >= 8)
        {
                pa_sample_spec ss = {format_of(argv[3]), (uint32_t)atoi(argv[2]), (uint8_t)atoi(argv[4])};
                size_t chunk = (size_t)atoi(argv[5]) * pa_frame_size(&ss);
                pa_buffer_attr a = {(uint32_t)-1, (uint32_t)(chunk * (size_t)atoi(argv[6])), (uint32_t)-1, (uint32_t)-1, (uint32_t)-1};
                pa_stream *s = make_stream(&ss, &a, atoi(argv[7]) ? PA_STREAM_ADJUST_LATENCY : 0);

                pattern_chunks(s, &ss, chunk, 1.5);
                drain_stream(s, 3);
                pa_stream_disconnect(s), pa_stream_unref(s);
        }
        else if (!strcmp(what, "callback") && argc >= 6)
        {
                pa_sample_spec ss = {format_of(argv[3]), (uint32_t)atoi(argv[2]), (uint8_t)atoi(argv[4])};
                pa_buffer_attr a = {(uint32_t)-1, (uint32_t)pa_usec_to_bytes((pa_usec_t)atoi(argv[5]) * 1000, &ss), (uint32_t)-1, (uint32_t)-1, (uint32_t)-1};
                callback_spec = &ss;
                pa_stream *s = pa_stream_new(ctx, "gauntlet cb", &ss, NULL);
                stream_ready = 0;
                pa_stream_set_state_callback(s, stream_state, NULL);
                pa_stream_set_write_callback(s, write_cb, NULL);
                if (pa_stream_connect_playback(s, NULL, &a, PA_STREAM_ADJUST_LATENCY | PA_STREAM_AUTO_TIMING_UPDATE, NULL, NULL) < 0)
                        fail("connect");
                if (run_until(&stream_ready, 5) < 0 || stream_ready != 1)
                        fail("stream never ready");
                spin(1.5);
                if (callback_total < pa_bytes_per_second(&ss) / 2)
                {
                        char why[160];
                        snprintf(why, sizeof why, "the callback was asked for only %zu bytes in 1.5 s", callback_total);
                        fail(why);
                }
                drain_stream(s, 3);
        }
        else if (!strcmp(what, "drain-unstarted"))
        {
                pa_stream *s = make_stream(&stereo48, NULL, 0);
                sine(buffer, 64 * 4, &stereo48);
                pa_stream_write(s, buffer, 64 * 4, NULL, 0, PA_SEEK_RELATIVE);
                drain_stream(s, 3); // a program's quit: it drains what it wrote, which never reached the prebuf
        }
        else if (!strcmp(what, "corked-start"))
        {
                pa_stream *s = make_stream(&stereo48, NULL, PA_STREAM_START_CORKED);
                sine(buffer, 48000 * 4 / 5, &stereo48);
                pa_stream_write(s, buffer, 48000 * 4 / 5, NULL, 0, PA_SEEK_RELATIVE);
                long long before = played(s);
                spin(0.3);
                if (played(s) != before)
                        fail("a stream started corked played");
                op_done = 0;
                wait_op(pa_stream_cork(s, 0, op_success, NULL), 2, "uncork");
                play_sine(s, &stereo48, 1.0);
                if (played(s) <= before)
                        fail("uncorked, it did not play");
        }
        else if (!strcmp(what, "cork-toggle"))
        {
                pa_stream *s = make_stream(&stereo48, NULL, 0);
                for (int i = 0; i < 20; i++)
                {
                        play_sine(s, &stereo48, 0.1);
                        op_done = 0;
                        wait_op(pa_stream_cork(s, i & 1 ? 0 : 1, op_success, NULL), 2, "cork");
                }
                op_done = 0;
                wait_op(pa_stream_cork(s, 0, op_success, NULL), 2, "uncork");
                play_sine(s, &stereo48, 0.5);
                drain_stream(s, 3);
        }
        else if (!strcmp(what, "flush"))
        {
                pa_stream *s = make_stream(&stereo48, NULL, 0);
                for (int i = 0; i < 10; i++)
                {
                        play_sine(s, &stereo48, 0.1);
                        op_done = 0;
                        wait_op(pa_stream_flush(s, op_success, NULL), 2, "flush");
                }
                play_sine(s, &stereo48, 0.5);
                drain_stream(s, 3);
        }
        else if (!strcmp(what, "trigger"))
        {
                pa_buffer_attr a = {(uint32_t)-1, 48000 * 4, 48000 * 4, (uint32_t)-1, (uint32_t)-1};
                pa_stream *s = make_stream(&stereo48, &a, 0);
                sine(buffer, 4800 * 4, &stereo48);
                pa_stream_write(s, buffer, 4800 * 4, NULL, 0, PA_SEEK_RELATIVE);
                op_done = 0;
                wait_op(pa_stream_trigger(s, op_success, NULL), 2, "trigger");
                spin(0.3);
                if (played(s) <= 0)
                        fail("triggered below its prebuf, it did not play");
        }
        else if (!strcmp(what, "prebuf0"))
        {
                pa_buffer_attr a = {(uint32_t)-1, (uint32_t)-1, 0, (uint32_t)-1, (uint32_t)-1};
                pa_stream *s = make_stream(&stereo48, &a, 0);
                sine(buffer, 480 * 4, &stereo48);
                pa_stream_write(s, buffer, 480 * 4, NULL, 0, PA_SEEK_RELATIVE);
                spin(0.2);
                if (played(s) <= 0)
                        fail("prebuf 0: it did not start at once");
                play_sine(s, &stereo48, 0.5);
        }
        else if (!strcmp(what, "set-attr"))
        {
                pa_stream *s = make_stream(&stereo48, NULL, PA_STREAM_ADJUST_LATENCY);
                unsigned sizes[] = {512, 4096, 65536, 1024, 192000, 256};
                for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++)
                {
                        pa_buffer_attr a = {(uint32_t)-1, sizes[i], (uint32_t)-1, (uint32_t)-1, (uint32_t)-1};
                        op_done = 0;
                        wait_op(pa_stream_set_buffer_attr(s, &a, op_success, NULL), 2, "set_buffer_attr");
                        play_sine(s, &stereo48, 0.3);
                }
                drain_stream(s, 3);
        }
        else if (!strcmp(what, "update-rate"))
        {
                pa_stream *s = make_stream(&stereo48, NULL, PA_STREAM_VARIABLE_RATE);
                unsigned rates[] = {44100, 48000, 22050, 96000, 48000};
                for (unsigned i = 0; i < sizeof rates / sizeof rates[0]; i++)
                {
                        op_done = 0;
                        wait_op(pa_stream_update_sample_rate(s, rates[i], op_success, NULL), 2, "update_sample_rate");
                        play_sine(s, &stereo48, 0.2);
                }
        }
        else if (!strcmp(what, "many") && argc >= 3)
        {
                int n = atoi(argv[2]);
                pa_stream **s = calloc((size_t)n, sizeof *s);
                for (int i = 0; i < n; i++)
                        s[i] = make_stream(&stereo48, NULL, PA_STREAM_ADJUST_LATENCY);
                double end = now() + 1.5;
                size_t total = 0;
                while (now() < end)
                {
                        pa_mainloop_iterate(loop, 0, NULL);
                        for (int i = 0; i < n; i++)
                                if (pa_stream_writable_size(s[i]) >= 4096)
                                        sine(buffer, 4096, &stereo48), pa_stream_write(s[i], buffer, 4096, NULL, 0, PA_SEEK_RELATIVE), total += 4096;
                        struct timespec t = {0, 500000};
                        nanosleep(&t, NULL);
                }
                if (total < (size_t)n * 48000 * 4 / 2)
                        fail("many streams starved");
                for (int i = 0; i < n; i++)
                        pa_stream_disconnect(s[i]), pa_stream_unref(s[i]);
                spin(0.2);
        }
        else if (!strcmp(what, "churn") && argc >= 3)
        {
                int n = atoi(argv[2]);
                for (int i = 0; i < n; i++)
                {
                        pa_stream *s = make_stream(&stereo48, NULL, i & 1 ? PA_STREAM_ADJUST_LATENCY : 0);
                        sine(buffer, 4096, &stereo48);
                        pa_stream_write(s, buffer, 4096, NULL, 0, PA_SEEK_RELATIVE);
                        pa_stream_disconnect(s);
                        pa_stream_unref(s);
                        pa_mainloop_iterate(loop, 0, NULL);
                }
                pa_stream *s = make_stream(&stereo48, NULL, 0);
                play_sine(s, &stereo48, 0.5); // the server still serves after the churn
        }
        else if (!strcmp(what, "disconnect"))
        {
                pa_stream *s = make_stream(&stereo48, NULL, 0);
                play_sine(s, &stereo48, 0.3);
                pa_context_disconnect(ctx);
                spin(0.2);
                ctx_ready = 0;
                connect_context();
                s = make_stream(&stereo48, NULL, 0);
                play_sine(s, &stereo48, 0.5);
        }
        else if (!strcmp(what, "latency"))
        {
                pa_stream *s = make_stream(&stereo48, NULL, PA_STREAM_INTERPOLATE_TIMING | PA_STREAM_AUTO_TIMING_UPDATE | PA_STREAM_ADJUST_LATENCY);
                play_sine(s, &stereo48, 0.5);
                pa_usec_t lat = 0;
                int neg = 0;
                played(s);
                if (pa_stream_get_latency(s, &lat, &neg) < 0)
                        fail("get_latency failed");
                if (lat > 2000000)
                        fail("latency over 2 s");
        }
        else if (!strcmp(what, "record"))
        {
                pa_sample_spec ss = {PA_SAMPLE_S16LE, 48000, 2};
                pa_stream *s = pa_stream_new(ctx, "gauntlet rec", &ss, NULL);
                stream_ready = 0;
                pa_stream_set_state_callback(s, stream_state, NULL);
                if (pa_stream_connect_record(s, NULL, NULL, PA_STREAM_ADJUST_LATENCY) < 0)
                        fail("connect_record");
                if (run_until(&stream_ready, 5) < 0 || stream_ready != 1)
                        fail("record stream never ready");
                size_t got = 0;
                double end = now() + 1.0;
                while (now() < end)
                {
                        const void *data;
                        size_t n;
                        pa_mainloop_iterate(loop, 0, NULL);
                        while (pa_stream_readable_size(s) > 0 && pa_stream_peek(s, &data, &n) == 0 && n)
                                got += n, pa_stream_drop(s);
                        struct timespec t = {0, 1000000};
                        nanosleep(&t, NULL);
                }
                if (got < 48000 * 4 / 4)
                        fail("recorded under a quarter second in a second");
        }
        else
        {
                fprintf(stderr, "usage: unknown scenario %s\n", what);
                return 2;
        }
        pa_context_disconnect(ctx);
        printf("PASS %s\n", what);
        return 0;
}
