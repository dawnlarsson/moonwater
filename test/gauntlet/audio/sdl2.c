/*
        The audio gauntlet's SDL 2 client: the same source against every SDL 2 there is to load (the bowl's sdl2-compat over SDL 3,
        Steam's runtime's own SDL 2, a game's bundled old one by LD_LIBRARY_PATH, 32-bit with -m32), each step held to a deadline.

            sdl2 FREQ FORMAT CHANNELS SAMPLES      FORMAT s16 f32 s32 u8
        Steps: open with a callback, the callback called for 1.5 s of sound; paused, not called; resumed; closed and opened again
        ten times; a queue device (SDL_QueueAudio) drained; SDL_Quit returns (a game's quit hung on a stream that never played).
*/
#include <SDL.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile long called_bytes;
static double phase;
static SDL_AudioSpec have;

static void fill(void *u, Uint8 *stream, int len)
{
        (void)u;
        int frame = SDL_AUDIO_BITSIZE(have.format) / 8 * have.channels;
        for (int i = 0; i + frame <= len; i += frame)
        {
                double v = 0.2 * sin(phase);
                phase += 2 * M_PI * 440.0 / have.freq;
                for (int c = 0; c < have.channels; c++)
                {
                        Uint8 *p = stream + i + c * (frame / have.channels);
                        if (have.format == AUDIO_F32SYS) { float f = (float)v; memcpy(p, &f, 4); }
                        else if (have.format == AUDIO_S32SYS) { Sint32 s = (Sint32)(v * 2147483647.0); memcpy(p, &s, 4); }
                        else if (have.format == AUDIO_U8) *p = (Uint8)(128 + v * 127);
                        else { Sint16 s = (Sint16)(v * 32767); memcpy(p, &s, 2); }
                }
        }
        called_bytes += len;
}

static void watchdog(int sig)
{
        (void)sig;
        static const char m[] = "FAIL a step hung past its deadline (SDL_Quit or a close waiting on the audio thread)\n";
        write(2, m, sizeof m - 1);
        _exit(1);
}

static int fail(const char *why)
{
        fprintf(stderr, "FAIL %s: %s\n", why, SDL_GetError());
        exit(1);
}

int main(int argc, char **argv)
{
        SDL_AudioSpec want;
        SDL_AudioDeviceID dev;
        SDL_version v;

        if (argc < 5)
                return 2;
        signal(SIGALRM, watchdog);
        alarm(30);
        SDL_GetVersion(&v);
        printf("SDL %d.%d.%d, driver ", v.major, v.minor, v.patch);
        if (SDL_Init(SDL_INIT_AUDIO) < 0)
                fail("SDL_Init");
        printf("%s\n", SDL_GetCurrentAudioDriver());
        SDL_memset(&want, 0, sizeof want);
        want.freq = atoi(argv[1]);
        want.format = !strcmp(argv[2], "f32") ? AUDIO_F32SYS : !strcmp(argv[2], "s32") ? AUDIO_S32SYS : !strcmp(argv[2], "u8") ? AUDIO_U8 : AUDIO_S16SYS;
        want.channels = (Uint8)atoi(argv[3]);
        want.samples = (Uint16)atoi(argv[4]);
        want.callback = fill;
        dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (!dev)
                fail("open");
        long second = (long)have.freq * have.channels * (SDL_AUDIO_BITSIZE(have.format) / 8);
        SDL_PauseAudioDevice(dev, 0);
        SDL_Delay(1500);
        if (called_bytes < second / 2)
        {
                fprintf(stderr, "FAIL the callback gave %ld bytes in 1.5 s (a second is %ld): the stream does not play\n", called_bytes, second);
                return 1;
        }
        SDL_PauseAudioDevice(dev, 1);
        SDL_Delay(100);
        long paused_at = called_bytes;
        SDL_Delay(400);
        if (called_bytes - paused_at > second / 10)
                fail("paused, the callback is still called");
        SDL_PauseAudioDevice(dev, 0);
        SDL_Delay(500);
        if (called_bytes - paused_at < second / 8)
                fail("resumed, the callback is not called");
        SDL_CloseAudioDevice(dev);
        for (int i = 0; i < 10; i++)
        {
                long before = called_bytes;
                dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
                if (!dev)
                        fail("reopen");
                SDL_PauseAudioDevice(dev, 0);
                SDL_Delay(150);
                if (called_bytes == before)
                        fail("reopened, it does not play");
                SDL_CloseAudioDevice(dev);
        }
        // A queue device: no callback, half a second queued and drained.
        want.callback = NULL;
        dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (!dev)
                fail("open queue");
        {
                static Uint8 buf[1 << 20];
                int bytes = (int)(second / 2);
                bytes -= bytes % (SDL_AUDIO_BITSIZE(have.format) / 8 * have.channels);
                called_bytes = 0;
                fill(NULL, buf, bytes > (int)sizeof buf ? (int)sizeof buf : bytes);
                SDL_QueueAudio(dev, buf, (Uint32)(bytes > (int)sizeof buf ? (int)sizeof buf : bytes));
                SDL_PauseAudioDevice(dev, 0);
                Uint32 end = SDL_GetTicks() + 3000;
                while (SDL_GetQueuedAudioSize(dev) && SDL_GetTicks() < end)
                        SDL_Delay(10);
                if (SDL_GetQueuedAudioSize(dev))
                        fail("queued audio not played in 3 s");
                SDL_CloseAudioDevice(dev);
        }
        alarm(10);
        SDL_Quit();
        printf("PASS sdl2 %s %s %s %s\n", argv[1], argv[2], argv[3], argv[4]);
        return 0;
}
