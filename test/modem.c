/*
        Tests for the audio modem in src/waterlink/modem.c: the SHA-256 and HMAC
        vectors, round trips at random lengths and offsets, the wire format
        (a pinned digest), input bounds, wrong keys, replay and the receiver
        session, a noise sweep, a benchmark, and a libFuzzer target.

        Run by the modem lane in test/run (sh test/run modem). The self-test
        builds with -DMODEM_SELFTEST only through that lane; the fuzz target
        is built with -DMODEM_FUZZ when the compiler has libFuzzer.
*/

/* The send section of modem.c (a file over the sound card) needs the shell's sound server and host: the self-test has
   neither, so it builds the codec alone. */
#define MODEM_NO_SEND 1
#include "../src/waterlink/modem.c"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* sha256 of the samples of the golden frame: payload "golden frame", seq 42, the test key. Pins the wire format of each
   configuration that has a pin: the default (40 samples, 8 tones at 96 kHz) and the 16-tone option. */
#if MODEM_SYM == 40 && MODEM_TONES == 8
#define MODEM_GOLDEN "2535d90caec9a60f752370d789ad4eedc1986b07ad1512bfc26e6943b7809260"
#endif

#ifndef MODEM_FUZZ /* the self-test; the fuzz build below uses none of it */

static int failures;

static void check(int ok, const char *what)
{
	printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok)
		failures++;
}

static uint32_t rng_state = 0x9e3779b9u;

static uint32_t rnd(void)
{
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;
	return rng_state;
}

static double gauss(void)
{
	double u1 = ((rnd() >> 8) + 1.0) / 16777217.0;
	double u2 = (rnd() >> 8) / 16777216.0;

	return sqrt(-2.0 * log(u1)) * cos(2.0 * 3.14159265358979323846 * u2);
}

static void hexout(const uint8_t *p, size_t n, char *out)
{
	static const char digits[] = "0123456789abcdef";
	size_t i;

	for (i = 0; i < n; i++)
	{
		out[2 * i] = digits[p[i] >> 4];
		out[2 * i + 1] = digits[p[i] & 15];
	}
	out[2 * n] = 0;
}

static const uint8_t KEY[] = "moonwater pairing key 1";
#define KEYLEN (sizeof(KEY) - 1)

static void test_vectors(void)
{
	uint8_t d[32], mac[32];
	char got[65];
	modem_sha256 c;
	uint8_t key20[20];

	modem_sha_init(&c);
	modem_sha_final(&c, d);
	hexout(d, 32, got);
	check(strcmp(got, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0, "sha256 of empty input");

	modem_sha_init(&c);
	modem_sha_update(&c, (const uint8_t *)"abc", 3);
	modem_sha_final(&c, d);
	hexout(d, 32, got);
	check(strcmp(got, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0, "sha256 of abc");

	modem_sha_init(&c);
	modem_sha_update(&c, (const uint8_t *)"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56);
	modem_sha_final(&c, d);
	hexout(d, 32, got);
	check(strcmp(got, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1") == 0, "sha256 of 56-byte message");

	memset(key20, 0x0b, sizeof(key20));
	modem_hmac(key20, sizeof(key20), (const uint8_t *)"Hi There", 8, mac);
	hexout(mac, 32, got);
	check(strcmp(got, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7") == 0,
	      "hmac-sha256, RFC 4231 test case 1");
}

static void test_roundtrip(void)
{
	size_t cap = modem_encode_samples(MODEM_PAYLOAD_MAX) + 200;
	int16_t *pcm = malloc(sizeof(int16_t) * cap), *shifted = malloc(sizeof(int16_t) * cap);
	uint8_t payload[MODEM_PAYLOAD_MAX], out[MODEM_PAYLOAD_MAX];
	int trial, bad = 0;

	for (trial = 0; trial < 200; trial++)
	{
		size_t len = rnd() % (MODEM_PAYLOAD_MAX + 1), lead = rnd() % MODEM_SYM, n, i;
		uint32_t seq = (uint32_t)trial * 7919u, got_seq = 0;
		int got;

		for (i = 0; i < len; i++)
			payload[i] = (uint8_t)rnd();
		n = modem_encode(KEY, KEYLEN, seq, payload, len, pcm, cap);
		if (n == 0)
		{
			bad++;
			continue;
		}
		memset(shifted, 0, lead * sizeof(int16_t));
		memcpy(shifted + lead, pcm, n * sizeof(int16_t));
		got = modem_decode(shifted, n + lead, KEY, KEYLEN, out, sizeof(out), &got_seq);
		if (got != (int)len || got_seq != seq || (len && memcmp(out, payload, len) != 0))
			bad++;
	}
	check(bad == 0, "200 clean frames, lengths 0..255, random offsets, round-trip exactly");
	free(pcm);
	free(shifted);
}

#ifdef MODEM_GOLDEN
static void test_golden(void)
{
	static const uint8_t payload[] = "golden frame";
	int16_t *pcm = malloc(sizeof(int16_t) * modem_encode_samples(sizeof(payload) - 1));
	uint8_t digest[32];
	char got[65];
	modem_sha256 c;
	size_t n;

	n = modem_encode(KEY, KEYLEN, 42, payload, sizeof(payload) - 1, pcm, modem_encode_samples(sizeof(payload) - 1));
	modem_sha_init(&c);
	modem_sha_update(&c, (const uint8_t *)pcm, n * sizeof(int16_t));
	modem_sha_final(&c, digest);
	hexout(digest, 32, got);
	printf("      golden sample digest: %s\n", got);
	check(strcmp(got, MODEM_GOLDEN) == 0, "frame samples match the pinned wire format");
	free(pcm);
}

#endif /* the default configuration's golden frame */

static void test_bounds(void)
{
	static const uint8_t key15[15] = "short key 15 b";
	int16_t *pcm = malloc(sizeof(int16_t) * (MODEM_MAX_SAMPLES + 2));
	uint8_t big[MODEM_PAYLOAD_MAX + 1], out[MODEM_PAYLOAD_MAX];
	uint32_t seq = 0;
	size_t i;

	check(modem_encode(KEY, KEYLEN, 1, (const uint8_t *)"x", 1, pcm, 1 << 20) > 0, "a frame encodes under a 16-byte key");
	check(modem_encode(key15, sizeof(key15), 1, (const uint8_t *)"x", 1, pcm, 1 << 20) == 0, "a 15-byte key is refused at encode");
	check(modem_decode(pcm, 4000, key15, sizeof(key15), out, sizeof(out), &seq) == -1, "a 15-byte key is refused at decode");
	check(modem_encode(KEY, KEYLEN, 1, big, MODEM_PAYLOAD_MAX + 1, pcm, 1 << 20) == 0, "a payload of 256 bytes is refused");
	check(modem_encode(KEY, KEYLEN, 1, out, 8, pcm, 100) == 0, "a frame that does not fit the buffer is refused");
	check(modem_decode(pcm, MODEM_MAX_SAMPLES + 1, KEY, KEYLEN, out, sizeof(out), &seq) == -1, "input over MODEM_MAX_SAMPLES is refused");
	check(modem_decode(pcm, 10, KEY, KEYLEN, out, sizeof(out), &seq) == -1, "input shorter than one symbol is refused");
	check(modem_decode(pcm, 4000, KEY, KEYLEN, NULL, sizeof(out), &seq) == -1, "a NULL output buffer is refused");
	for (i = 0; i < MODEM_MAX_SAMPLES; i++)
		pcm[i] = (i & 1) ? 32767 : -32768;
	check(modem_decode(pcm, MODEM_MAX_SAMPLES, KEY, KEYLEN, out, sizeof(out), &seq) == -1,
	      "a full-scale square wave of the largest size decodes to nothing, without overflow");
	free(pcm);
}

static void test_wrongkey_and_replay(void)
{
	static const uint8_t right[] = "the right pairing key";
	static const uint8_t wrong[] = "a wrong pairing key";
	uint8_t out[64];
	uint32_t seq = 0;
	size_t n;
	int16_t *big = malloc(sizeof(int16_t) * modem_encode_samples(16));

	n = modem_encode(right, sizeof(right) - 1, 5, (const uint8_t *)"0123456789abcdef", 16, big, modem_encode_samples(16));
	check(modem_decode(big, n, wrong, sizeof(wrong) - 1, out, sizeof(out), &seq) == -1, "a frame under another key is refused");
	check(modem_decode(big, n, right, sizeof(right) - 1, out, sizeof(out), &seq) == 16 && seq == 5,
	      "the same frame under the right key is accepted");
	check(modem_fresh(4, 5) && !modem_fresh(5, 5) && !modem_fresh(5, 4), "freshness: newer is fresh, a repeat or older is not");
	check(!modem_fresh(5, 5 + MODEM_WINDOW + 1) && modem_fresh(5, 5 + MODEM_WINDOW), "freshness: the window edge");
	free(big);
}

static void test_session(void)
{
	int16_t *pcm = malloc(sizeof(int16_t) * modem_encode_samples(4));
	int16_t *bad = malloc(sizeof(int16_t) * modem_encode_samples(4));
	uint8_t out[8];
	uint32_t seq = 0;
	size_t n;
	modem_rx rx;
	unsigned k;
	int refused_all = 1;

	modem_rx_open(&rx, KEY, KEYLEN);
	n = modem_encode(KEY, KEYLEN, 1, (const uint8_t *)"one.", 4, pcm, modem_encode_samples(4));
	check(modem_rx_frame(&rx, pcm, n, out, sizeof(out), &seq) == 4, "session takes a fresh frame");
	check(modem_rx_frame(&rx, pcm, n, out, sizeof(out), &seq) == -1 && rx.refused == 1 && !rx.closed,
	      "a replay is refused, counted once, and the session stays open");
	n = modem_encode(KEY, KEYLEN, 2, (const uint8_t *)"two.", 4, pcm, modem_encode_samples(4));
	check(modem_rx_frame(&rx, pcm, n, out, sizeof(out), &seq) == 4, "session takes the next fresh frame");
	n = modem_encode((const uint8_t *)"an attacker key!", 16, 9, (const uint8_t *)"evil", 4, bad, modem_encode_samples(4));
	for (k = 0; k < MODEM_REFUSE_LIMIT; k++)
		if (modem_rx_frame(&rx, bad, n, out, sizeof(out), &seq) != -1)
			refused_all = 0;
	check(refused_all && rx.closed, "attacker frames are refused, and the session closes after MODEM_REFUSE_LIMIT");
	n = modem_encode(KEY, KEYLEN, 3, (const uint8_t *)"late", 4, pcm, modem_encode_samples(4));
	check(modem_rx_frame(&rx, pcm, n, out, sizeof(out), &seq) == -1, "a closed session takes nothing, even a valid frame");
	free(pcm);
	free(bad);
}

/* A replayed frame ahead of a fresh one in the same capture must not hide the fresh one: the receiver takes it. */
static void test_stale_frame_does_not_hide_fresh(void)
{
	static const uint8_t one[] = "one.", two[] = "two.";
	size_t cap = modem_encode_samples(4);
	int16_t *a = malloc(sizeof(int16_t) * cap), *b = malloc(sizeof(int16_t) * cap), *both = malloc(sizeof(int16_t) * 2 * cap);
	size_t na = modem_encode(KEY, KEYLEN, 1, one, 4, a, cap), nb = modem_encode(KEY, KEYLEN, 2, two, 4, b, cap);
	uint8_t out[8];
	uint32_t seq = 0;
	modem_rx rx;
	int got;

	memcpy(both, a, na * sizeof(int16_t));
	memcpy(both + na, b, nb * sizeof(int16_t));
	modem_rx_open(&rx, KEY, KEYLEN);
	check(modem_rx_frame(&rx, a, na, out, sizeof(out), &seq) == 4 && rx.last == 1, "session takes the first frame");
	got = modem_rx_frame(&rx, both, na + nb, out, sizeof(out), &seq);
	check(got == 4 && seq == 2 && memcmp(out, two, 4) == 0 && !rx.closed,
	      "a replay ahead of a fresh frame in the same capture does not hide the fresh frame");
	free(a);
	free(b);
	free(both);
}

/* Frames of 0 and of MODEM_PAYLOAD_MAX bytes, the two ends of the one-byte LEN, round-trip exactly. */
static void test_length_limits(void)
{
	size_t cap = modem_encode_samples(MODEM_PAYLOAD_MAX);
	int16_t *pcm = malloc(sizeof(int16_t) * cap);
	uint8_t payload[MODEM_PAYLOAD_MAX], out[MODEM_PAYLOAD_MAX];
	size_t i;
	int ok = 1;

	for (i = 0; i < MODEM_PAYLOAD_MAX; i++)
		payload[i] = (uint8_t)(0xA5 ^ i);
	for (i = 0; i < 2; i++)
	{
		size_t len = i ? MODEM_PAYLOAD_MAX : 0;
		uint32_t seq = i ? 0xFFFFFFFEu : 1u, got_seq = 0;
		size_t n = modem_encode(KEY, KEYLEN, seq, payload, len, pcm, cap);
		int got = modem_decode(pcm, n, KEY, KEYLEN, out, sizeof(out), &got_seq);

		ok = ok && n > 0 && got == (int)len && got_seq == seq && (len == 0 || memcmp(out, payload, len) == 0);
	}
	check(ok, "a frame of 0 bytes and one of 255 bytes round-trip exactly, with the largest sequence number");
	free(pcm);
}

/* Every cut of a frame, down to the bytes the buffer holds: a decode of a truncated capture must find nothing or the
   whole frame, and must not read past the buffer (the buffer is exactly the cut's length, so ASan sees an overread). */
static void test_truncation(void)
{
	static const uint8_t payload[] = "cut";
	size_t cap = modem_encode_samples(sizeof(payload) - 1);
	int16_t *pcm = malloc(sizeof(int16_t) * cap);
	size_t n = modem_encode(KEY, KEYLEN, 3, payload, sizeof(payload) - 1, pcm, cap), cut;
	int bad = 0;

	for (cut = 0; cut <= n; cut += (cut + 400 < n) ? 97 : 1)
	{
		int16_t *exact = malloc(sizeof(int16_t) * (cut ? cut : 1));
		uint8_t out[8];
		uint32_t seq = 0;
		int got;

		memcpy(exact, pcm, cut * sizeof(int16_t));
		got = modem_decode(exact, cut, KEY, KEYLEN, out, sizeof(out), &seq);
		if (got != -1 && !(got == (int)(sizeof(payload) - 1) && seq == 3 && memcmp(out, payload, 3) == 0))
			bad++;
		if (cut == n && got != (int)(sizeof(payload) - 1))
			bad++;
		free(exact);
	}
	check(bad == 0, "a capture cut at every length finds nothing or the frame, and never reads past its buffer");
	free(pcm);
}

static void test_noise(void)
{
	static const double snrs[] = {10, 6, 3, 0, -3};
	int16_t *pcm = malloc(sizeof(int16_t) * (modem_encode_samples(32) + 200));
	int16_t *noisy = malloc(sizeof(int16_t) * (modem_encode_samples(32) + 200));
	uint8_t payload[32], out[64];
	size_t i;
	int decoded[sizeof(snrs) / sizeof(snrs[0])];

	for (i = 0; i < 32; i++)
		payload[i] = (uint8_t)(i * 37 + 11);
	for (i = 0; i < sizeof(snrs) / sizeof(snrs[0]); i++)
	{
		double sigma = ((double)MODEM_AMP / sqrt(2.0)) * pow(10.0, -snrs[i] / 20.0);
		int trial, ok = 0;

		for (trial = 0; trial < 50; trial++)
		{
			size_t n = modem_encode(KEY, KEYLEN, (uint32_t)trial, payload, 32, pcm, modem_encode_samples(32) + 200), k;
			uint32_t got_seq = 0;

			for (k = 0; k < n; k++)
			{
				double v = pcm[k] + sigma * gauss();

				if (v > 32767)
					v = 32767;
				if (v < -32768)
					v = -32768;
				noisy[k] = (int16_t)v;
			}
			if (modem_decode(noisy, n, KEY, KEYLEN, out, sizeof(out), &got_seq) == 32 &&
			    got_seq == (uint32_t)trial && memcmp(out, payload, 32) == 0)
				ok++;
		}
		decoded[i] = ok;
		printf("      snr %4.0f dB: %2d of 50 frames (32 bytes) decode\n", snrs[i], ok);
	}
#if MODEM_SYM == 40 && MODEM_TONES == 8
	/* The default configuration's margins, measured with the seeds above: 0 dB decodes every frame, -3 dB 10 of 50. */
	check(decoded[3] == 50, "at 0 dB every frame decodes");
	check(decoded[4] >= 5, "at -3 dB at least 5 of 50 frames decode");
#else
	(void)decoded;
#endif
	free(pcm);
	free(noisy);
}

static void test_bench(void)
{
	static const uint8_t payload[MODEM_PAYLOAD_MAX] = {0};
	size_t cap = modem_encode_samples(MODEM_PAYLOAD_MAX) + 200;
	int16_t *pcm = malloc(sizeof(int16_t) * cap);
	uint8_t out[MODEM_PAYLOAD_MAX];
	size_t n = modem_encode(KEY, KEYLEN, 1, payload, MODEM_PAYLOAD_MAX, pcm, cap), i;
	double air = (double)n / MODEM_RATE, ms;
	clock_t t0, t1;
	int got = 0;

	t0 = clock();
	for (i = 0; i < 40; i++)
	{
		uint32_t seq = 0;

		got = modem_decode(pcm, n, KEY, KEYLEN, out, sizeof(out), &seq);
	}
	t1 = clock();
	ms = 1000.0 * (double)(t1 - t0) / CLOCKS_PER_SEC / 40;
	printf("      config: %d samples a symbol, %d tones, %d bits a symbol, %d baud, %d bit/s raw\n",
	       MODEM_SYM, MODEM_TONES, MODEM_SYM_BITS, MODEM_BAUD, MODEM_BAUD * MODEM_SYM_BITS);
	printf("      full frame: %.3f s on air, %.0f payload bytes/s\n", air, MODEM_PAYLOAD_MAX / air);
	printf("      decode CPU: %.2f ms per frame (%.2f%% of its air time)\n", ms, 100.0 * ms / (air * 1000.0));
	check(got == MODEM_PAYLOAD_MAX, "a full-size frame decodes in the benchmark");
	free(pcm);
}

/* ---- datagrams over the modem: chunks, reassembly, the session ---- */

static size_t put_datagram(modem_tx *tx, const uint8_t *dg, size_t len, int16_t *buf, size_t cap, size_t *at)
{
	size_t n = modem_tx_datagram(tx, dg, len, buf + *at, cap - *at);

	*at += n;
	return n;
}

static void test_datagrams(void)
{
	/* the largest configuration (two tones) puts a full datagram at about 0.5 million samples */
	enum { CAP = 1 << 21 };
	static const size_t sizes[3] = {1, 251, 1200};
	static uint8_t sent[3][MODEM_DATAGRAM_MAX];
	uint8_t got[MODEM_DATAGRAM_MAX];
	int16_t *pcm = malloc(sizeof(int16_t) * CAP);
	modem_tx tx;
	modem_chunk_rx cr;
	size_t at = 0, pos = 0, used = 0, k, j;
	int ok = 1;
	int r;

	check(MODEM_CHUNKS == 5 && MODEM_CHUNK_DATA == 251, "a 1200-byte datagram is five chunks of 251 bytes");
	modem_tx_open(&tx, KEY, KEYLEN);
	check(modem_tx_datagram(&tx, sent[0], 0, pcm, CAP) == 0 && modem_tx_datagram(&tx, sent[0], 1201, pcm, CAP) == 0,
	      "a datagram of 0 or 1201 bytes is refused");
	for (k = 0; k < 3; k++) {
		for (j = 0; j < sizes[k]; j++)
			sent[k][j] = (uint8_t)rnd();
		if (put_datagram(&tx, sent[k], sizes[k], pcm, CAP, &at) == 0)
			ok = 0;
	}
	check(ok && at < CAP, "three datagrams of 1, 251 and 1200 bytes encode into one capture");
	check(modem_tx_datagram(&tx, sent[0], 1200, pcm, 1000) == 0, "a datagram whose frames do not fit is refused");

	modem_chunk_open(&cr, KEY, KEYLEN);
	ok = 1;
	for (k = 0; k < 3; k++) {
		r = modem_chunk_take(&cr, pcm + pos, at - pos, got, sizeof got, &used);
		if (r != (int)sizes[k] || memcmp(got, sent[k], sizes[k]) != 0)
			ok = 0;
		pos += used;
	}
	check(ok, "the three datagrams come back exactly, in order, from one capture");
	check(modem_chunk_take(&cr, pcm + pos, at - pos, got, sizeof got, &used) == 0 && used == 0,
	      "an exhausted capture gives no datagram");
	check(modem_chunk_take(&cr, pcm, at, got, sizeof got, &used) == 0, "a replayed capture gives nothing: every frame is stale");
	free(pcm);
}

static void test_datagram_dropped_chunk(void)
{
	enum { CAP = 1 << 20 };
	int16_t *pcm = malloc(sizeof(int16_t) * CAP);
	uint8_t big[MODEM_DATAGRAM_MAX], small[10], got[MODEM_DATAGRAM_MAX];
	size_t full = modem_encode_samples(MODEM_PAYLOAD_MAX), last = modem_encode_samples(MODEM_CHUNK_HEAD + 196);
	size_t total, at = 0, used = 0, k;
	modem_tx tx;
	modem_chunk_rx cr;
	int r;

	for (k = 0; k < sizeof big; k++)
		big[k] = (uint8_t)rnd();
	for (k = 0; k < sizeof small; k++)
		small[k] = (uint8_t)rnd();
	modem_tx_open(&tx, KEY, KEYLEN);
	total = put_datagram(&tx, big, sizeof big, pcm, CAP, &at);
	check(total == 4 * full + last, "a 1200-byte datagram is four full frames and one of 196 bytes of data");
	/* cut the third frame out, as a lost burst would: the set can never complete */
	memmove(pcm + 2 * full, pcm + 3 * full, sizeof(int16_t) * (total - 3 * full));
	at = total - full;
	put_datagram(&tx, small, sizeof small, pcm, CAP, &at);

	modem_chunk_open(&cr, KEY, KEYLEN);
	r = modem_chunk_take(&cr, pcm, at, got, sizeof got, &used);
	check(r == (int)sizeof small && memcmp(got, small, sizeof small) == 0,
	      "a partial set is dropped when the next datagram starts, and the next datagram is delivered");
	check(used + MODEM_LEAD == at, "the capture is taken through the datagram that completed, and the silence after it is left");
	free(pcm);
}

static void test_datagram_session(void)
{
	int16_t *bad = malloc(sizeof(int16_t) * 8 * modem_encode_samples(4));
	static const uint8_t attacker[] = "an attacker key!";
	uint8_t out[MODEM_DATAGRAM_MAX];
	size_t at = 0, k, n, used = 0;
	modem_chunk_rx cr;
	int ok = 1;

	for (k = 0; k < 8; k++)
		at += modem_encode(attacker, sizeof attacker - 1, (uint32_t)k + 1, (const uint8_t *)"evil", 4, bad + at,
				   modem_encode_samples(4));
	n = at;
	modem_chunk_open(&cr, KEY, KEYLEN);
	for (k = 0; k < MODEM_REFUSE_LIMIT - 1; k++)
		if (modem_chunk_take(&cr, bad, n, out, sizeof out, &used) != 0 || cr.rx.closed)
			ok = 0;
	check(ok, "attacker frames are refused without closing the session before MODEM_REFUSE_LIMIT");
	check(modem_chunk_take(&cr, bad, n, out, sizeof out, &used) == -1 && cr.rx.closed,
	      "the session closes at MODEM_REFUSE_LIMIT, and a closed session takes no datagram");
	free(bad);
}

int main(void)
{
	test_vectors();
	test_roundtrip();
#ifdef MODEM_GOLDEN
	test_golden(); /* the wire format is pinned for the configurations that define MODEM_GOLDEN */
#endif
	test_bounds();
	test_wrongkey_and_replay();
	test_session();
	test_datagrams();
	test_datagram_dropped_chunk();
	test_datagram_session();
	test_stale_frame_does_not_hide_fresh();
	test_length_limits();
	test_truncation();
	test_noise();
	test_bench();
	printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
#endif /* MODEM_FUZZ */


/* ---- fuzz target: -DMODEM_FUZZ (libFuzzer) ------------------------------ */

#ifdef MODEM_FUZZ

static const uint8_t FUZZ_KEY[] = "fuzz harness key 16+";
#define FUZZ_KEYLEN (sizeof(FUZZ_KEY) - 1)

/* Builds a real frame from the first bytes, then mutates its samples with the rest, and runs the decoder and
   a session over the result. */
int LLVMFuzzerTestOneInput(const uint8_t *d, size_t size)
{
	static int16_t pcm[MODEM_MAX_SAMPLES];
	uint8_t payload[MODEM_PAYLOAD_MAX], out[MODEM_PAYLOAD_MAX];
	uint32_t seq;
	modem_rx rx;
	size_t len, n, i;

	if (size < 3)
		return 0;
	len = d[0] % (MODEM_PAYLOAD_MAX + 1);
	for (i = 0; i < len; i++)
		payload[i] = d[1 + (i % (size - 1))];
	seq = ((uint32_t)d[1] << 8) | d[2];

	n = modem_encode(FUZZ_KEY, FUZZ_KEYLEN, seq, payload, len, pcm, MODEM_MAX_SAMPLES);
	for (i = 3; i + 2 < size && n > 0; i += 3)
	{
		size_t at = (((size_t)d[i] << 8) | d[i + 1]) % n;

		pcm[at] = (int16_t)((int)d[i + 2] * 257 - 32768);
	}
	seq = 0;
	(void)modem_decode(pcm, n, FUZZ_KEY, FUZZ_KEYLEN, out, sizeof(out), &seq);
	modem_rx_open(&rx, FUZZ_KEY, FUZZ_KEYLEN);
	(void)modem_rx_frame(&rx, pcm, n, out, sizeof(out), &seq);
	return 0;
}

#endif /* MODEM_FUZZ */
