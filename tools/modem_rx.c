/*
        modem_rx: the Mac's end of the audio modem. Reads what the box sends over the sound card, finds its frames, checks
        each frame's tag under the modem key, and writes the datagrams' data to a file. It never runs anything it receives:
        the payload is bytes written at the offset the sender named, and nothing else.

        Build (on a Mac, from the repository root). The modem calls two functions of lib.c, so lib.c is compiled without its
        _start and only those two names are kept global (the rest would replace the C library's):

                cc -O2 -w -DSTANDARD_NO_ENTRY -c src/lib.c -o lib_full.o
                ld -r -exported_symbol _sha256_blocks -exported_symbol _hash_hmac_sha256_prepared -o lib.o lib_full.o
                cc -O2 -Wall -o modem_rx tools/modem_rx.c lib.o -lz

        (-lz: the system's zlib, which inflates a send that was compressed on the box.)

        Use: record the box's output as MODEM_RATE Hz (48000), 16-bit, mono, little-endian PCM (any tool that writes raw PCM; on macOS,
        sox or ffmpeg with -f s16le -ar 48000 -ac 1), then

                modem_rx --key modem.key -o received.bin capture.raw
                modem_rx --key modem.key -o received.bin -        (PCM on stdin)

        --rate HZ says what rate the capture was recorded at (default MODEM_RATE); any other rate is refused, with the rate
        named, since the symbols are timed for MODEM_RATE and a capture at another rate cannot be read.

        modem.key is the 64 hex digits `moonwater link modem key` prints on the box, in a file of their own. A capture may hold
        any number of `link modem send`s: the sender's frame numbers go on from one send to the next, and each frame must follow
        the one before it. A capture that begins after an earlier one takes --after N, where N is the frame number the earlier
        send ended on (the box prints it), so the receiver starts from there.

        The first send's data goes to -o, and each send after it goes to -o.2, -o.3 and so on: a send starts at byte 0 of its
        own file, and a datagram at byte 0 after data is the start of the next send.

        Each datagram is four bytes of offset (big endian) and then its data. A gap in the offsets is reported on stderr and
        left as zeros in the output, since nothing is acknowledged and nothing is sent again. A send whose first datagram was
        lost is written into the file before it, and its gap is reported there.

        A send's first byte is its marker: 0 means the rest is the file as it was, and 1 means the rest is a gzip member (RFC 1952),
        which the box made of the file because it was smaller. When a send arrives whole, with no gap and its first datagram, the
        receiver takes the marker off and writes the file as it was (a gzip member is inflated). A send with a gap is left as it
        was received, marker first, and says so.

        The capture is MODEM_RATE Hz, 16-bit, mono: the symbols are timed for that rate, and the box refuses to send on a card at
        another rate.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

/* The receiver builds the codec alone: the send section of modem.c needs the shell's sound server and host. The two
   functions of lib.c that the codec calls, and the platform writes they name, are in modem_host.h. */
#define MODEM_NO_SEND 1
#include "../src/waterlink/modem_host.h"
#include "../src/waterlink/modem.c"

/* The window the decoder is given: one call scans it all, so it is a few frames long, not the whole capture. A frame
   that straddles the edge of a window is kept for the next one, so the window keeps at least a whole frame behind. */
#define RX_WINDOW 131072u
#define RX_KEEP (modem_encode_samples(MODEM_PAYLOAD_MAX) + 1024)

static int rx_key_read(const char *path, p8 key[32])
{
        FILE *f = fopen(path, "rb");
        char text[128];
        sized n, i;
        int high = -1;
        sized at = 0;

        if (!f)
                return 0;
        n = fread(text, 1, sizeof text - 1, f);
        fclose(f);
        text[n] = 0;
        for (i = 0; i < n; i++) {
                int v;
                char c = text[i];

                if (c == ' ' || c == '\n' || c == '\r' || c == '\t')
                        continue;
                if (c >= '0' && c <= '9')
                        v = c - '0';
                else if (c >= 'a' && c <= 'f')
                        v = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F')
                        v = c - 'A' + 10;
                else
                        return 0;
                if (high < 0) {
                        high = v;
                } else {
                        if (at >= 32)
                                return 0;
                        key[at++] = (p8)(high * 16 + v);
                        high = -1;
                }
        }
        return high < 0 && at == 32;
}

static p32 rx_be32(const p8 *p)
{
        return ((p32)p[0] << 24) | ((p32)p[1] << 16) | ((p32)p[2] << 8) | (p32)p[3];
}

/* The output for send number send: the -o name for the first, and that name with .2, .3 ... after it for the rest. */
static FILE *rx_send_output(const char *base, unsigned send, char *name, sized size)
{
        if (send == 1)
                snprintf(name, size, "%s", base);
        else
                snprintf(name, size, "%s.%u", base, send);
        return fopen(name, "wb");
}

/* Inflates a gzip member from src to dst. Answers 1 when the whole member came through, 0 for a damaged or short one. */
static int rx_gunzip(FILE *src, FILE *dst)
{
        static unsigned char in_buf[65536], out_buf[65536];
        z_stream strm;
        int ret = Z_OK, ok = 0;

        memory_zero(&strm, sizeof strm);
        if (inflateInit2(&strm, 16 + MAX_WBITS) != Z_OK)
                return 0;
        do {
                strm.avail_in = (uInt)fread(in_buf, 1, sizeof in_buf, src);
                if (ferror(src) || strm.avail_in == 0)
                        break;
                strm.next_in = in_buf;
                do {
                        sized have;

                        strm.avail_out = sizeof out_buf;
                        strm.next_out = out_buf;
                        ret = inflate(&strm, Z_NO_FLUSH);
                        if (ret == Z_STREAM_ERROR || ret == Z_NEED_DICT || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR)
                                goto done;
                        have = sizeof out_buf - strm.avail_out;
                        if (fwrite(out_buf, 1, have, dst) != have)
                                goto done;
                } while (strm.avail_out == 0);
        } while (ret != Z_STREAM_END);
        ok = ret == Z_STREAM_END;
done:
        inflateEnd(&strm);
        return ok;
}

/* Unwraps a finished send by its marker byte, the stream's first: 0 is the file as it was, 1 is gzip, which is inflated. A send
   is unwrapped only when its first datagram came and no datagram of it is missing: a gap in a gzip stream cannot be inflated,
   and a gap in either leaves zeros the marker cannot be trusted past. Anything else is left as it was received. */
static void rx_finish_send(const char *path, int first, unsigned gaps)
{
        FILE *src, *dst;
        char temp[4300];
        int marker, ok = 1;

        if (!first) {
                fprintf(stderr, "modem_rx: %s: the send's first frame was not received, so its marker is unknown: left as received\n", path);
                return;
        }
        if (gaps) {
                fprintf(stderr, "modem_rx: %s: the send has gaps, so it is not unwrapped: left as received, its first byte the marker\n", path);
                return;
        }
        src = fopen(path, "rb");
        if (!src) {
                fprintf(stderr, "modem_rx: cannot read %s\n", path);
                return;
        }
        marker = fgetc(src);
        if (marker == EOF) {
                fclose(src);
                return;
        }
        if (marker != 0 && marker != 1) {
                fprintf(stderr, "modem_rx: %s: marker %d is not one this receiver knows: left as received\n", path, marker);
                fclose(src);
                return;
        }
        snprintf(temp, sizeof temp, "%s.unwrap", path);
        dst = fopen(temp, "wb");
        if (!dst) {
                fprintf(stderr, "modem_rx: cannot write %s\n", temp);
                fclose(src);
                return;
        }
        if (marker == 1) {
                ok = rx_gunzip(src, dst);
        } else {
                static unsigned char buf[65536];
                sized n;

                while ((n = fread(buf, 1, sizeof buf, src)) > 0)
                        if (fwrite(buf, 1, n, dst) != n) {
                                ok = 0;
                                break;
                        }
                ok = ok && !ferror(src);
        }
        fclose(src);
        if (fclose(dst) != 0)
                ok = 0;
        if (!ok) {
                fprintf(stderr, "modem_rx: %s: the send could not be unwrapped (a gzip stream that is damaged or ends early): left as received\n", path);
                remove(temp);
                return;
        }
        if (rename(temp, path) != 0) {
                fprintf(stderr, "modem_rx: cannot write %s\n", path);
                remove(temp);
                return;
        }
        fprintf(stderr, "modem_rx: %s: unwrapped from %s\n", path, marker == 1 ? "gzip" : "raw");
}

int main(int argc, char **argv)
{
        static b16 window[RX_WINDOW];
        static p8 raw[2 * RX_WINDOW];
        static p8 datagram[MODEM_DATAGRAM_MAX];
        static modem_chunk_rx cr;
        p8 key[32];
        const char *key_path = null, *out_path = null, *in_path = "-", *after_text = null;
        char name[4200];
        FILE *in = stdin, *out;
        sized fill = 0, keep, datagrams = 0, bytes = 0, expected = 0;
        unsigned gaps = 0, sends = 1, send_gaps = 0;
        int send_first = 0;
        p32 after = 0;
        int closed = 0, eof = 0;
        sized skipped = 0;
        unsigned long rate = MODEM_RATE;

        for (int a = 1; a < argc; a++) {
                if (!string_compare(argv[a], "--key") && a + 1 < argc)
                        key_path = argv[++a];
                else if (!string_compare(argv[a], "-o") && a + 1 < argc)
                        out_path = argv[++a];
                else if (!string_compare(argv[a], "--after") && a + 1 < argc)
                        after_text = argv[++a];
                else if (!string_compare(argv[a], "--rate") && a + 1 < argc) {
                        char *end = null;

                        rate = strtoul(argv[++a], &end, 10);
                        if (end == argv[a] || *end) {
                                fprintf(stderr, "modem_rx: --rate takes a rate in Hz\n");
                                return 2;
                        }
                } else
                        in_path = argv[a];
        }
        if (!key_path || !out_path) {
                fprintf(stderr, "usage: modem_rx --key KEYFILE -o OUTFILE [--after N] [--rate HZ] [PCM|-]\n");
                return 2;
        }
        if (rate != MODEM_RATE) {
                fprintf(stderr, "modem_rx: the capture is at %lu Hz, and the modem's symbols are timed for %d Hz: nothing is read from a capture at another rate\n",
                        rate, (int)MODEM_RATE);
                return 2;
        }
        if (!rx_key_read(key_path, key)) {
                fprintf(stderr, "modem_rx: %s is not a 32-byte key in hex\n", key_path);
                return 2;
        }
        if (after_text) {
                char *end = null;
                unsigned long v = strtoul(after_text, &end, 10);

                if (end == after_text || *end || v > 0xffffffffUL) {
                        fprintf(stderr, "modem_rx: --after takes a frame number, 0 to 4294967295\n");
                        return 2;
                }
                after = (p32)v;
        }
        if (string_compare(in_path, "-") != 0) {
                in = fopen(in_path, "rb");
                if (!in) {
                        fprintf(stderr, "modem_rx: cannot read %s\n", in_path);
                        return 2;
                }
        }
        out = rx_send_output(out_path, sends, name, sizeof name);
        if (!out) {
                fprintf(stderr, "modem_rx: cannot write %s\n", name);
                return 2;
        }
        modem_chunk_open(&cr, key, sizeof key);
        cr.rx.last = after;

        while (!eof) {
                sized want = RX_WINDOW - fill, got, at = 0;

                /* Samples are little-endian 16-bit; fread returns whole samples only. */
                got = fread(raw, 2, want, in);
                eof = got < want;
                if (closed) {
                        /* The session is closed and decodes nothing more, but the input is read to its end, so a live pipe
                           is not cut short and the samples not decoded are counted. */
                        skipped += got;
                        fill = 0;
                        continue;
                }
                for (sized i = 0; i < got; i++)
                        window[fill + i] = (b16)((p16)raw[2 * i] | ((p16)raw[2 * i + 1] << 8));
                fill += got;

                for (;;) {
                        sized used = 0;
                        int r = modem_chunk_take(&cr, window + at, fill - at, datagram, sizeof datagram, &used);

                        at += used;
                        if (r < 0) {
                                closed = 1;
                                break;
                        }
                        if (r == 0)
                                break;
                        if ((sized)r < 4) {
                                fprintf(stderr, "modem_rx: a datagram of %d bytes has no offset: dropped\n", r);
                                continue;
                        }
                        {
                                p32 offset = rx_be32(datagram);
                                sized length = (sized)r - 4;

                                /* A send starts at byte 0 of its own file: after data, a datagram at 0 begins the next send. */
                                if (offset == 0 && expected > 0) {
                                        if (fclose(out) != 0) {
                                                fprintf(stderr, "modem_rx: cannot write %s\n", name);
                                                return 1;
                                        }
                                        rx_finish_send(name, send_first, send_gaps);
                                        sends++;
                                        out = rx_send_output(out_path, sends, name, sizeof name);
                                        if (!out) {
                                                fprintf(stderr, "modem_rx: cannot write %s\n", name);
                                                return 1;
                                        }
                                        fprintf(stderr, "modem_rx: a new send begins: writing %s\n", name);
                                        expected = 0;
                                        send_first = 0;
                                        send_gaps = 0;
                                }
                                if (offset != expected) {
                                        gaps++;
                                        send_gaps++;
                                        fprintf(stderr, "modem_rx: gap: bytes %lu to %lu were not received\n",
                                                (unsigned long)expected, (unsigned long)offset);
                                }
                                if (fseek(out, (long)offset, SEEK_SET) != 0 || fwrite(datagram + 4, 1, length, out) != length) {
                                        fprintf(stderr, "modem_rx: cannot write %s\n", name);
                                        return 1;
                                }
                                expected = offset + length;
                                if (offset == 0)
                                        send_first = 1;
                                bytes += length;
                                datagrams++;
                        }
                }

                /* Keep what a frame could still straddle: everything from the first sample not yet taken, and at least RX_KEEP. */
                keep = fill > RX_KEEP ? fill - RX_KEEP : 0;
                if (keep < at)
                        keep = at;
                memory_copy(window, window + keep, (fill - keep) * sizeof window[0]);
                fill -= keep;
        }

        if (fclose(out) != 0) {
                fprintf(stderr, "modem_rx: cannot write %s\n", name);
                return 1;
        }
        if (datagrams > 0)
                rx_finish_send(name, send_first, send_gaps);
        if (in != stdin)
                fclose(in);
        fprintf(stderr, "modem_rx: %lu datagrams, %lu bytes in %u send(s), the last to %s; %u gaps; %u refused frames%s; the last frame is %lu\n",
                (unsigned long)datagrams, (unsigned long)bytes, sends, name, gaps, cr.rx.refused,
                cr.rx.closed ? "; the session closed after too many refusals" : "", (unsigned long)cr.rx.last);
        if (skipped)
                fprintf(stderr, "modem_rx: %lu samples after the session closed were read and not decoded\n",
                        (unsigned long)skipped);
        return datagrams > 0 && gaps == 0 && !cr.rx.closed ? 0 : 1;
}
