/*
        What the toolkit variants of the stress bench (sdl_anim.c, glfw_anim.c) share: the client's clock, the frame times
        from it, and the one RESULT line. A toolkit hides the frame callback and the presentation, so the frame time here is
        the time between the returns of the toolkit's vsynced swap, which is what an application built on it sees.
*/
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static uint64_t tk_now(void)
{
        struct timespec t;
        clock_gettime(CLOCK_MONOTONIC, &t);
        return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

static int tk_cmp(const void *a, const void *b)
{
        double x = *(const double *)a, y = *(const double *)b;
        return x < y ? -1 : x > y;
}

static double tk_pct(double *v, int n, double p)
{
        if (!n)
                return -1;
        qsort(v, (size_t)n, sizeof(double), tk_cmp);
        int i = (int)ceil(p / 100.0 * n) - 1;
        return v[i < 0 ? 0 : i >= n ? n - 1 : i];
}

static double tk_iv[200000];
static int tk_n;
static uint64_t tk_last, tk_first, tk_exec, tk_warm;
static long tk_frames;

static void tk_frame(void)
{
        uint64_t t = tk_now();
        if (!tk_first)
                tk_first = t;
        if (t >= tk_warm && tk_last && tk_n < 200000)
                tk_iv[tk_n++] = (double)(t - tk_last) / 1e6;
        if (t >= tk_warm)
                tk_frames++;
        tk_last = t;
}

static void tk_report(const char *name, const char *renderer)
{
        const char *t0 = getenv("STRESS_T0");
        uint64_t launch = t0 ? strtoull(t0, NULL, 10) : 0;
        double p50 = tk_pct(tk_iv, tk_n, 50), p99 = tk_pct(tk_iv, tk_n, 99), period = 1000.0 / 60;
        long dropped = 0;
        for (int i = 0; i < tk_n; i++)
                if (tk_iv[i] > period * 1.5)
                        dropped += (long)floor(tk_iv[i] / period + 0.5) - 1;
        printf("RESULT mode=%s renderer=%s frames=%ld fps=%.2f frame_p50=%.3f frame_p99=%.3f dropped=%ld first_frame_ms=%.3f "
               "launch_to_first_ms=%.3f\n",
               name, renderer, tk_frames, p50 > 0 ? 1000.0 / p50 : 0, p50, p99, dropped, (double)(tk_first - tk_exec) / 1e6,
               launch ? (double)(tk_first - launch) / 1e6 : -1.0);
}
