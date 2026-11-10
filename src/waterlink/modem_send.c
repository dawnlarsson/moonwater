/*
        The box's end of the audio modem: `moonwater link modem send FILE`, and `link push` when the transport is modem.

        The file is cut into datagrams of 1196 bytes, each sent as its offset (four bytes, big endian) and its data. Each datagram
        is cut into frames by modem_tx_datagram (modem.c), and the frames' sound is played through the sound server as a program
        of its own, the way `moonwater audio test` plays its tone: one stream for the whole file, drained at the end. Nothing is
        acknowledged and nothing is sent again. The receiver is tools/modem_rx.c on the Mac, which writes each datagram at the
        offset it came with and reports a gap where one did not arrive.

        Nothing here reads what comes back: the modem has no way back, so the box never runs a byte of anything it receives.

        This file is included by src/sh/shell.c after the sound server's client (src/audio/service.c), which it uses.
*/

#ifndef WATERLINK_MODEM_SEND_INCLUDED
#define WATERLINK_MODEM_SEND_INCLUDED

#include "modem.c"

#define MODEM_SEND_PORTION (MODEM_DATAGRAM_MAX - 4) /* file bytes in a datagram: the other four are its offset */
/* The most samples one datagram takes: MODEM_CHUNKS frames, of at most 24800 samples each (a 255 byte payload), and 124000
   is what five of them take. modem_tx_datagram refuses a datagram that does not fit, so a short buffer is a refusal, not
   a wrong sound. */
#define MODEM_SEND_SAMPLES 131072u
#define MODEM_SEND_PER_SEND 16384u /* mono samples in one write: 16384 stereo frames of four bytes, 64 KB, as the tone sends */
#define MODEM_SEND_TIMEOUT_MS 10000

static struct ctl modem_ctl;
static unsigned char modem_frame[20 + 65536];
static int16_t modem_pcm[MODEM_SEND_SAMPLES];

/* Reads up to n bytes, or fewer only at the end of the file. Answers the count, or a negative error. */
static bipolar modem_send_read(bipolar handle, p8 address_to into, positive n)
{
        positive done = 0;

        while (done < n)
        {
                bipolar got = system_read_retry((positive)handle, into + done, n - done);

                if (got < 0)
                        return got;
                if (got == 0)
                        break;
                done += (positive)got;
        }
        return (bipolar)done;
}

/* Plays count mono samples through the open stream, as the same sound in both channels. Answers 0, or -1 when the
   connection failed. */
static bipolar modem_send_pcm(unsigned long channel, const int16_t address_to pcm, positive count)
{
        positive at = 0;

        while (at < count)
        {
                positive frames = count - at < MODEM_SEND_PER_SEND ? count - at : MODEM_SEND_PER_SEND;
                short *tone = (short *)(modem_frame + 20);

                for (positive i = 0; i < frames; i++)
                {
                        tone[2 * i] = pcm[at + i];
                        tone[2 * i + 1] = pcm[at + i];
                }
                ctl_budget(&modem_ctl);
                ctl_put_be(modem_frame, (unsigned long)(frames * 4));
                ctl_put_be(modem_frame + 4, channel);
                ctl_put_be(modem_frame + 8, 0), ctl_put_be(modem_frame + 12, 0), ctl_put_be(modem_frame + 16, 0);
                if (modem_ctl.transport.send(modem_ctl.transport.context, modem_frame, 20 + frames * 4, ctl_left(&modem_ctl)) < 0)
                        return -1;
                at += frames;
        }
        return 0;
}

/*
        Sends FILE over the audio modem to the Mac's receiver. Answers 0 when every datagram was played, else what the verb
        should say: no key, no file, no sound server, or a connection that broke part of the way (the datagrams before it were
        played, and the receiver will say which bytes it did not get).
*/
static b32 modem_send_file(string_address path)
{
        uint8_t key[32];
        uint8_t datagram[MODEM_DATAGRAM_MAX];
        modem_tx tx;
        unsigned long channel, sent_bytes = 0, sent_datagrams = 0;
        size_t samples;
        struct ctl_w w;
        bipolar in, got;

        scope_exit(crypto_forget(key, sizeof key));
        if (link_secret_at(LINK_MODEM_PATH, (p8 address_to)key, false) < 0)
                return host_refuse("there is no modem key: moonwater link modem key makes one, and the receiver needs the same\n");
        in = system_open_at(AT_FDCWD, path, FILE_READ | O_CLOEXEC);
        return_if(in < 0, host_fail(path, in));
        scope_exit(system_close(in));

        modem_ctl.timeout_ms = MODEM_SEND_TIMEOUT_MS;
        if (ctl_connect(&modem_ctl, AUDIO_SOCKET) < 0)
                return host_refuse("no sound server answers on %s: the modem has no sound card to play on (moonwater audio)\n", AUDIO_SOCKET);
        if (!audio_stream_open(&modem_ctl, "Waterlink modem", "modem", &channel))
        {
                ctl_disconnect(&modem_ctl);
                return 1;
        }
        modem_tx_open(&tx, key, sizeof key);

        for (;;)
        {
                got = modem_send_read(in, (p8 address_to)(datagram + 4), MODEM_SEND_PORTION);
                if (got < 0)
                {
                        ctl_disconnect(&modem_ctl);
                        return host_fail(path, got);
                }
                if (got == 0)
                        break;
                datagram[0] = (uint8_t)(sent_bytes >> 24);
                datagram[1] = (uint8_t)(sent_bytes >> 16);
                datagram[2] = (uint8_t)(sent_bytes >> 8);
                datagram[3] = (uint8_t)sent_bytes;
                samples = modem_tx_datagram(&tx, datagram, 4 + (size_t)got, modem_pcm, MODEM_SEND_SAMPLES);
                if (!samples)
                {
                        ctl_disconnect(&modem_ctl);
                        return host_refuse("a datagram of %s did not fit its frames: the send is stopped after %p bytes\n", path, (positive)sent_bytes);
                }
                if (modem_send_pcm(channel, modem_pcm, (positive)samples) < 0)
                {
                        ctl_disconnect(&modem_ctl);
                        return host_refuse("the connection to the sound server broke after %p bytes: the receiver has what came before\n", (positive)sent_bytes);
                }
                sent_bytes += (unsigned long)got;
                sent_datagrams++;
        }

        if (!sent_datagrams)
        {
                ctl_disconnect(&modem_ctl);
                return host_refuse("%s is empty: nothing to send\n", path);
        }
        //      The drain: the sound server answers when the last of it is played, which is the only time the sender knows it is
        //      done. The stream is then deleted and the connection closed.
        ctl_begin(&modem_ctl, &w, 12);
        ctl_w_u32(&w, channel);
        if (ctl_exchange(&modem_ctl, w.len) < 0)
                host_say(log, host_label "the sound server did not say the last of it was played\n");
        ctl_begin(&modem_ctl, &w, 4);
        ctl_w_u32(&w, channel);
        ctl_exchange(&modem_ctl, w.len);
        ctl_disconnect(&modem_ctl);
        host_say(log, host_label "sent %p bytes in %p datagrams over the audio modem; nothing is acknowledged\n",
                 (positive)sent_bytes, (positive)sent_datagrams);
        return 0;
}

#endif // WATERLINK_MODEM_SEND_INCLUDED
