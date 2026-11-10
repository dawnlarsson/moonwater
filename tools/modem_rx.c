/*
	modem_rx: the Mac's end of the audio modem. Reads what the box sends over the sound card, finds its frames, checks
	each frame's tag under the modem key, and writes the datagrams' data to a file. It never runs anything it receives:
	the payload is bytes written at the offset the sender named, and nothing else.

	Build (on a Mac, from the repository root):

		cc -O2 -Wall -o modem_rx tools/modem_rx.c

	Use: record the box's output as 48000 Hz, 16-bit, mono, little-endian PCM (any tool that writes raw PCM; on macOS,
	sox or ffmpeg with -f s16le -ar 48000 -ac 1), then

		modem_rx --key modem.key -o received.bin capture.raw
		modem_rx --key modem.key -o received.bin -        (PCM on stdin)

	modem.key is the 64 hex digits `moonwater link modem key` prints on the box, in a file of their own. A capture may hold
	any number of `link modem send`s: the sender's frame numbers go on from one send to the next, and each frame must follow
	the one before it. A capture that begins after an earlier one takes --after N, where N is the frame number the earlier
	send ended on (the box prints it), so the receiver starts from there.

	The first send's data goes to -o, and each send after it goes to -o.2, -o.3 and so on: a send starts at byte 0 of its
	own file, and a datagram at byte 0 after data is the start of the next send.

	Each datagram is four bytes of offset (big endian) and then its data. A gap in the offsets is reported on stderr and
	left as zeros in the output, since nothing is acknowledged and nothing is sent again. A send whose first datagram was
	lost is written into the file before it, and its gap is reported there.

	The capture is 48000 Hz, 16-bit, mono: the symbols are timed for that rate, and the box refuses to send on a card at
	another rate.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The receiver builds the codec alone: the send section of modem.c needs the shell's sound server and host. */
#define MODEM_NO_SEND 1
#include "../src/waterlink/modem.c"

/* The window the decoder is given: one call scans it all, so it is a few frames long, not the whole capture. A frame
   that straddles the edge of a window is kept for the next one, so the window keeps at least a whole frame behind. */
#define RX_WINDOW 131072u
#define RX_KEEP (modem_encode_samples(MODEM_PAYLOAD_MAX) + 1024)

static int rx_key_read(const char *path, uint8_t key[32])
{
	FILE *f = fopen(path, "rb");
	char text[128];
	size_t n, i;
	int high = -1;
	size_t at = 0;

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
			key[at++] = (uint8_t)(high * 16 + v);
			high = -1;
		}
	}
	return high < 0 && at == 32;
}

static uint32_t rx_be32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* The output for send number send: the -o name for the first, and that name with .2, .3 ... after it for the rest. */
static FILE *rx_send_output(const char *base, unsigned send, char *name, size_t size)
{
	if (send == 1)
		snprintf(name, size, "%s", base);
	else
		snprintf(name, size, "%s.%u", base, send);
	return fopen(name, "wb");
}

int main(int argc, char **argv)
{
	static int16_t window[RX_WINDOW];
	static uint8_t raw[2 * RX_WINDOW];
	static uint8_t datagram[MODEM_DATAGRAM_MAX];
	static modem_chunk_rx cr;
	uint8_t key[32];
	const char *key_path = NULL, *out_path = NULL, *in_path = "-", *after_text = NULL;
	char name[4200];
	FILE *in = stdin, *out;
	size_t fill = 0, keep, datagrams = 0, bytes = 0, expected = 0;
	unsigned gaps = 0, sends = 1;
	uint32_t after = 0;
	int closed = 0, eof = 0;
	size_t skipped = 0;

	for (int a = 1; a < argc; a++) {
		if (!strcmp(argv[a], "--key") && a + 1 < argc)
			key_path = argv[++a];
		else if (!strcmp(argv[a], "-o") && a + 1 < argc)
			out_path = argv[++a];
		else if (!strcmp(argv[a], "--after") && a + 1 < argc)
			after_text = argv[++a];
		else
			in_path = argv[a];
	}
	if (!key_path || !out_path) {
		fprintf(stderr, "usage: modem_rx --key KEYFILE -o OUTFILE [--after N] [PCM|-]\n");
		return 2;
	}
	if (!rx_key_read(key_path, key)) {
		fprintf(stderr, "modem_rx: %s is not a 32-byte key in hex\n", key_path);
		return 2;
	}
	if (after_text) {
		char *end = NULL;
		unsigned long v = strtoul(after_text, &end, 10);

		if (end == after_text || *end || v > 0xffffffffUL) {
			fprintf(stderr, "modem_rx: --after takes a frame number, 0 to 4294967295\n");
			return 2;
		}
		after = (uint32_t)v;
	}
	if (strcmp(in_path, "-") != 0) {
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
		size_t want = RX_WINDOW - fill, got, at = 0;

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
		for (size_t i = 0; i < got; i++)
			window[fill + i] = (int16_t)((uint16_t)raw[2 * i] | ((uint16_t)raw[2 * i + 1] << 8));
		fill += got;

		for (;;) {
			size_t used = 0;
			int r = modem_chunk_take(&cr, window + at, fill - at, datagram, sizeof datagram, &used);

			at += used;
			if (r < 0) {
				closed = 1;
				break;
			}
			if (r == 0)
				break;
			if ((size_t)r < 4) {
				fprintf(stderr, "modem_rx: a datagram of %d bytes has no offset: dropped\n", r);
				continue;
			}
			{
				uint32_t offset = rx_be32(datagram);
				size_t length = (size_t)r - 4;

				/* A send starts at byte 0 of its own file: after data, a datagram at 0 begins the next send. */
				if (offset == 0 && expected > 0) {
					if (fclose(out) != 0) {
						fprintf(stderr, "modem_rx: cannot write %s\n", name);
						return 1;
					}
					sends++;
					out = rx_send_output(out_path, sends, name, sizeof name);
					if (!out) {
						fprintf(stderr, "modem_rx: cannot write %s\n", name);
						return 1;
					}
					fprintf(stderr, "modem_rx: a new send begins: writing %s\n", name);
					expected = 0;
				}
				if (offset != expected) {
					gaps++;
					fprintf(stderr, "modem_rx: gap: bytes %lu to %lu were not received\n",
						(unsigned long)expected, (unsigned long)offset);
				}
				if (fseek(out, (long)offset, SEEK_SET) != 0 || fwrite(datagram + 4, 1, length, out) != length) {
					fprintf(stderr, "modem_rx: cannot write %s\n", name);
					return 1;
				}
				expected = offset + length;
				bytes += length;
				datagrams++;
			}
		}

		/* Keep what a frame could still straddle: everything from the first sample not yet taken, and at least RX_KEEP. */
		keep = fill > RX_KEEP ? fill - RX_KEEP : 0;
		if (keep < at)
			keep = at;
		memmove(window, window + keep, (fill - keep) * sizeof window[0]);
		fill -= keep;
	}

	if (fclose(out) != 0) {
		fprintf(stderr, "modem_rx: cannot write %s\n", name);
		return 1;
	}
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
