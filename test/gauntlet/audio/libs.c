/*
        The audio gauntlet's clients of the other libraries, one source, one library a build (-DLIB_SDL3, -DLIB_SDL12, -DLIB_OPENAL,
        -DLIB_PORTAUDIO, -DLIB_AO, -DLIB_ALSA, -DLIB_MINIAUDIO), each playing a 440 Hz sine and held to deadlines: the sound flows
        (its callback or its writes go on for 1.5 s), it stops, it is closed and opened again, and its shutdown returns.

            libs FREQ CHANNELS FRAMES          exit 0 pass, 1 fail
*/
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static double phase, rate = 48000;
static int channels = 2;
static volatile long frames_made;

static void sine_s16(short *out, long frames)
{
        for (long i = 0; i < frames; i++)
        {
                short s = (short)(0.2 * sin(phase) * 32767);
                phase += 2 * M_PI * 440.0 / rate;
                for (int c = 0; c < channels; c++)
                        out[i * channels + c] = s;
        }
        frames_made += frames;
}

static void sine_f32(float *out, long frames)
{
        for (long i = 0; i < frames; i++)
        {
                float s = (float)(0.2 * sin(phase));
                phase += 2 * M_PI * 440.0 / rate;
                for (int c = 0; c < channels; c++)
                        out[i * channels + c] = s;
        }
        frames_made += frames;
}

static void sleep_ms(int ms)
{
        struct timespec t = {ms / 1000, (ms % 1000) * 1000000L};
        nanosleep(&t, NULL);
}

static void watchdog(int sig)
{
        (void)sig;
        static const char m[] = "FAIL a step hung past its deadline\n";
        write(2, m, sizeof m - 1);
        _exit(1);
}

static void fail(const char *why)
{
        fprintf(stderr, "FAIL %s\n", why);
        exit(1);
}

// The sound flowed: at least half the frames of the time waited were made.
static void flowed(long before, double seconds, const char *what)
{
        if (frames_made - before < (long)(rate * seconds / 2))
        {
                char why[200];
                snprintf(why, sizeof why, "%s: %ld frames in %.1f s at %.0f Hz: the sound does not flow", what, frames_made - before, seconds, rate);
                fail(why);
        }
}

#if defined(LIB_SDL3)
#include <SDL3/SDL.h>
static void SDLCALL more(void *u, SDL_AudioStream *s, int additional, int total)
{
        (void)u, (void)total;
        static float buf[65536];
        int frames = additional / (int)(sizeof(float) * channels);
        while (frames > 0)
        {
                int n = frames > 65536 / channels ? 65536 / channels : frames;
                sine_f32(buf, n);
                SDL_PutAudioStreamData(s, buf, n * (int)sizeof(float) * channels);
                frames -= n;
        }
}
static void run(int frames)
{
        (void)frames;
        if (!SDL_Init(SDL_INIT_AUDIO))
                fail("SDL_Init");
        printf("SDL3 driver %s\n", SDL_GetCurrentAudioDriver());
        for (int round = 0; round < 6; round++)
        {
                SDL_AudioSpec spec = {SDL_AUDIO_F32, channels, (int)rate};
                SDL_AudioStream *s = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, more, NULL);
                if (!s)
                        fail("SDL_OpenAudioDeviceStream");
                long before = frames_made;
                SDL_ResumeAudioStreamDevice(s);
                SDL_Delay(round ? 300 : 1500);
                flowed(before, round ? 0.3 : 1.5, "SDL3 stream");
                SDL_PauseAudioStreamDevice(s);
                SDL_DestroyAudioStream(s);
        }
        SDL_Quit();
}
#elif defined(LIB_SDL12)
#include <SDL/SDL.h>
static void fill12(void *u, Uint8 *stream, int len)
{
        (void)u;
        sine_s16((short *)stream, len / (2 * channels));
}
static void run(int frames)
{
        if (SDL_Init(SDL_INIT_AUDIO) < 0)
                fail("SDL_Init");
        for (int round = 0; round < 6; round++)
        {
                SDL_AudioSpec want = {0}, have;
                want.freq = (int)rate, want.format = AUDIO_S16SYS, want.channels = (Uint8)channels, want.samples = (Uint16)frames, want.callback = fill12;
                if (SDL_OpenAudio(&want, &have) < 0)
                        fail("SDL_OpenAudio");
                long before = frames_made;
                SDL_PauseAudio(0);
                SDL_Delay(round ? 300 : 1500);
                flowed(before, round ? 0.3 : 1.5, "SDL 1.2");
                SDL_CloseAudio();
        }
        SDL_Quit();
}
#elif defined(LIB_OPENAL)
#include <AL/al.h>
#include <AL/alc.h>
static void run(int frames)
{
        for (int round = 0; round < 4; round++)
        {
                ALCdevice *d = alcOpenDevice(NULL);
                if (!d)
                        fail("alcOpenDevice");
                ALCcontext *c = alcCreateContext(d, NULL);
                alcMakeContextCurrent(c);
                ALuint src, bufs[4];
                alGenSources(1, &src);
                alGenBuffers(4, bufs);
                static short pcm[65536];
                channels = 2;
                for (int b = 0; b < 4; b++)
                {
                        sine_s16(pcm, frames);
                        alBufferData(bufs[b], AL_FORMAT_STEREO16, pcm, frames * 4, (ALsizei)rate);
                }
                alSourceQueueBuffers(src, 4, bufs);
                alSourcePlay(src);
                long played = 0, want = (long)rate * (round ? 1 : 3) / 2;
                time_t end = time(NULL) + 6;
                while (played < want && time(NULL) < end)
                {
                        ALint done = 0;
                        alGetSourcei(src, AL_BUFFERS_PROCESSED, &done);
                        while (done-- > 0)
                        {
                                ALuint b;
                                alSourceUnqueueBuffers(src, 1, &b);
                                played += frames;
                                sine_s16(pcm, frames);
                                alBufferData(b, AL_FORMAT_STEREO16, pcm, frames * 4, (ALsizei)rate);
                                alSourceQueueBuffers(src, 1, &b);
                        }
                        ALint state;
                        alGetSourcei(src, AL_SOURCE_STATE, &state);
                        if (state != AL_PLAYING)
                                alSourcePlay(src);
                        sleep_ms(5);
                }
                if (played < want)
                        fail("OpenAL: its buffers were not played");
                alSourceStop(src);
                alDeleteSources(1, &src);
                alDeleteBuffers(4, bufs);
                alcMakeContextCurrent(NULL);
                alcDestroyContext(c);
                alcCloseDevice(d);
        }
}
#elif defined(LIB_PORTAUDIO)
#include <portaudio.h>
static int pa_cb(const void *in, void *out, unsigned long frames, const PaStreamCallbackTimeInfo *t, PaStreamCallbackFlags f, void *u)
{
        (void)in, (void)t, (void)f, (void)u;
        sine_f32(out, (long)frames);
        return paContinue;
}
static void run(int frames)
{
        if (Pa_Initialize() != paNoError)
                fail("Pa_Initialize");
        PaDeviceIndex dev = Pa_GetDefaultOutputDevice();
        if (dev == paNoDevice)
                fail("PortAudio: no output device");
        printf("PortAudio device %s (%s)\n", Pa_GetDeviceInfo(dev)->name, Pa_GetHostApiInfo(Pa_GetDeviceInfo(dev)->hostApi)->name);
        for (int round = 0; round < 4; round++)
        {
                PaStream *s;
                if (Pa_OpenDefaultStream(&s, 0, channels, paFloat32, rate, (unsigned long)frames, pa_cb, NULL) != paNoError)
                        fail("Pa_OpenDefaultStream");
                long before = frames_made;
                Pa_StartStream(s);
                Pa_Sleep(round ? 300 : 1500);
                flowed(before, round ? 0.3 : 1.5, "PortAudio");
                Pa_StopStream(s);
                Pa_CloseStream(s);
        }
        Pa_Terminate();
}
#elif defined(LIB_AO)
#include <ao/ao.h>
static void run(int frames)
{
        ao_initialize();
        int driver = ao_driver_id("pulse");
        if (driver < 0)
                fail("libao: no pulse driver");
        for (int round = 0; round < 4; round++)
        {
                ao_sample_format f = {16, (int)rate, channels, AO_FMT_LITTLE, NULL};
                ao_device *d = ao_open_live(driver, &f, NULL);
                if (!d)
                        fail("ao_open_live");
                static short pcm[65536];
                long want = (long)rate * (round ? 1 : 3) / 2;
                time_t end = time(NULL) + 6;
                for (long done = 0; done < want; done += frames)
                {
                        if (time(NULL) > end)
                                fail("libao: its writes stalled");
                        sine_s16(pcm, frames);
                        if (!ao_play(d, (char *)pcm, (uint_32)(frames * 2 * channels)))
                                fail("ao_play");
                }
                ao_close(d);
        }
        ao_shutdown();
}
#elif defined(LIB_ALSA)
#include <alsa/asoundlib.h>
static void run(int frames)
{
        for (int round = 0; round < 4; round++)
        {
                snd_pcm_t *pcm;
                if (snd_pcm_open(&pcm, "pulse", SND_PCM_STREAM_PLAYBACK, 0) < 0)
                        fail("snd_pcm_open pulse");
                if (snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, (unsigned)channels, (unsigned)rate, 1,
                                       (unsigned)((long)frames * 4000000L / (long)rate)) < 0)
                        fail("snd_pcm_set_params");
                static short buf[65536];
                long want = (long)rate * (round ? 1 : 3) / 2;
                time_t end = time(NULL) + 6;
                for (long done = 0; done < want;)
                {
                        if (time(NULL) > end)
                                fail("ALSA through pulse: its writes stalled");
                        sine_s16(buf, frames);
                        snd_pcm_sframes_t n = snd_pcm_writei(pcm, buf, (snd_pcm_uframes_t)frames);
                        if (n < 0)
                                n = snd_pcm_recover(pcm, (int)n, 1);
                        if (n > 0)
                                done += n;
                }
                snd_pcm_drain(pcm);
                snd_pcm_close(pcm);
        }
}
#elif defined(LIB_MINIAUDIO)
#define MINIAUDIO_IMPLEMENTATION
#define MA_ENABLE_ONLY_SPECIFIC_BACKENDS
#define MA_ENABLE_PULSEAUDIO
#include "miniaudio.h"
static void ma_cb(ma_device *d, void *out, const void *in, ma_uint32 frames)
{
        (void)d, (void)in;
        sine_f32(out, frames);
}
static void run(int frames)
{
        for (int round = 0; round < 4; round++)
        {
                ma_device_config c = ma_device_config_init(ma_device_type_playback);
                c.playback.format = ma_format_f32, c.playback.channels = (ma_uint32)channels, c.sampleRate = (ma_uint32)rate;
                c.periodSizeInFrames = (ma_uint32)frames, c.dataCallback = ma_cb;
                ma_device d;
                if (ma_device_init(NULL, &c, &d) != MA_SUCCESS)
                        fail("ma_device_init");
                long before = frames_made;
                ma_device_start(&d);
                sleep_ms(round ? 300 : 1500);
                flowed(before, round ? 0.3 : 1.5, "miniaudio");
                ma_device_uninit(&d);
        }
}
#else
#error "one LIB_ a build"
#endif

int main(int argc, char **argv)
{
        if (argc < 4)
                return 2;
        rate = atof(argv[1]), channels = atoi(argv[2]);
        signal(SIGALRM, watchdog);
        alarm(40);
        run(atoi(argv[3]));
        printf("PASS %s %s %s\n", argv[1], argv[2], argv[3]);
        return 0;
}
