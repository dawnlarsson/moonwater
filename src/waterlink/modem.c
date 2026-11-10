/*
	LAST RESORT. The audio modem: the link for getting data off a machine
	that cannot be trusted, when no other path is safe, or a debugging link.
	It assumes the other end is hostile:
	  - every frame is authenticated (HMAC-SHA256 under the pairing key)
	    before any byte of it is used;
	  - a receiver accepts a frame only if its sequence number is newer
	    than the last accepted one, so a recording replays to nothing;
	  - a frame carries data, never code, and the receiver never runs it;
	  - a receiver session closes after MODEM_REFUSE_LIMIT refused frames;
	  - the transmitter sends only when the operator starts a session;
	  - the cable is galvanically isolated at both ends.

	Modulation: multi-tone FSK at 48 kHz, 16-bit mono. A symbol is
	MODEM_SYM samples. It carries one of MODEM_TONES tones, tone c being c
	whole cycles in the symbol window, so the tones are orthogonal and the
	receiver needs only the sample offset, not a clock. The default is 40
	samples and 16 tones: 1200 baud, 4 bits a symbol, 4800 bit/s raw. It
	tolerates the most noise and clock drift of the configurations swept,
	at the same payload rate as the 2400 baud, 4-tone one.

	A frame: lead-in (MODEM_PRE bytes of 0x55), sync 5A C3 96 3C, LEN (one
	byte, 0..255), SEQ (four bytes, big-endian), payload, then a 32-byte
	HMAC-SHA256 over LEN, SEQ and payload. The receiver uses nothing until
	the tag checks. Sequence numbers start at 1 in a session.

	Integer arithmetic only: no floating point, no libm in the decoder.
	The sine tables are fixed at build time. SHA-256 is inline here so the
	file stands alone; in the moonwater tree the HMAC should be lib.c's
	hash_hmac_sha256_prepared, keyed once per session as net.c does.

	Not reentrant: the decoder keeps its bit buffer in a static array.

	Build the self-test with -DMODEM_SELFTEST and the fuzz target with
	-DMODEM_FUZZ (libFuzzer). Neither is part of the library.
*/

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ---- configuration ----------------------------------------------------- */

#ifndef MODEM_SYM
#define MODEM_SYM 40 /* samples per symbol: the baud rate is 48000 / MODEM_SYM */
#endif
#ifndef MODEM_TONES
#define MODEM_TONES 16 /* tones per symbol: a power of two below MODEM_SYM / 2 */
#endif

#define MODEM_SYM_BITS ((MODEM_TONES) == 2 ? 1 : (MODEM_TONES) == 4 ? 2 \
					     : (MODEM_TONES) == 8   ? 3 \
								    : 4)
#define MODEM_RATE 48000
#define MODEM_BAUD (MODEM_RATE / MODEM_SYM)
#define MODEM_PRE 4
#define MODEM_SYNC_BYTES 4
#define MODEM_HDR 5	      /* LEN and SEQ */
#define MODEM_PAYLOAD_MAX 255 /* the largest a one-byte LEN carries */
#define MODEM_TAG 32
#define MODEM_KEY_MIN 16
#define MODEM_MAX_SAMPLES (1u << 20) /* about 21.8 seconds: the most one decode call reads */
#define MODEM_WINDOW 1024u
#define MODEM_REFUSE_LIMIT 8u
#define MODEM_LEAD 400 /* silence before and after a frame, in samples */
#define MODEM_AMP 12000
#define MODEM_SCALE 10000
#define MODEM_CORR_SHIFT 1024 /* correlations are scaled down by this before squaring */

/* Refuses a configuration the orthogonality or the tables cannot support: the build fails. */
typedef char modem_config_ok[((MODEM_TONES) >= 2 && (MODEM_TONES) < (MODEM_SYM) / 2 &&
			      ((MODEM_TONES) & ((MODEM_TONES)-1)) == 0 &&
			      ((MODEM_SYM) == 10 || (MODEM_SYM) == 20 || (MODEM_SYM) == 40))
				 ? 1
				 : -1];

/* 10000 * cos and sin of 2*pi*k/MODEM_SYM, rounded. Generated once; fixed in the file. */
#if MODEM_SYM == 10
static const int32_t modem_cos_tab[MODEM_SYM] = {10000, 8090, 3090, -3090, -8090, -10000, -8090, -3090, 3090, 8090};
static const int32_t modem_sin_tab[MODEM_SYM] = {0, 5878, 9511, 9511, 5878, 0, -5878, -9511, -9511, -5878};
#endif
#if MODEM_SYM == 20
static const int32_t modem_cos_tab[MODEM_SYM] = {10000, 9511, 8090, 5878, 3090, 0, -3090, -5878, -8090, -9511, -10000, -9511, -8090, -5878, -3090, 0, 3090, 5878, 8090, 9511};
static const int32_t modem_sin_tab[MODEM_SYM] = {0, 3090, 5878, 8090, 9511, 10000, 9511, 8090, 5878, 3090, 0, -3090, -5878, -8090, -9511, -10000, -9511, -8090, -5878, -3090};
#endif
#if MODEM_SYM == 40
static const int32_t modem_cos_tab[MODEM_SYM] = {10000, 9877, 9511, 8910, 8090, 7071, 5878, 4540, 3090, 1564, 0, -1564, -3090, -4540, -5878, -7071, -8090, -8910, -9511, -9877, -10000, -9877, -9511, -8910, -8090, -7071, -5878, -4540, -3090, -1564, 0, 1564, 3090, 4540, 5878, 7071, 8090, 8910, 9511, 9877};
static const int32_t modem_sin_tab[MODEM_SYM] = {0, 1564, 3090, 4540, 5878, 7071, 8090, 8910, 9511, 9877, 10000, 9877, 9511, 8910, 8090, 7071, 5878, 4540, 3090, 1564, 0, -1564, -3090, -4540, -5878, -7071, -8090, -8910, -9511, -9877, -10000, -9877, -9511, -8910, -8090, -7071, -5878, -4540, -3090, -1564};
#endif

/* ---- the public interface ---------------------------------------------- */

/* The samples a frame of len payload bytes takes, lead and tail included; 0 if len is too long. */
size_t modem_encode_samples(size_t len);

/* Writes one frame into pcm (cap samples). Returns the sample count, or 0 if the key is too short, the payload
   too long, or the frame does not fit in cap. */
size_t modem_encode(const uint8_t *key, size_t klen, uint32_t seq, const uint8_t *payload, size_t len,
		    int16_t *pcm, size_t cap);

/* Finds the first frame whose tag checks under key. Returns its payload length (0..255) and sets seq, or -1
   if there is none, the input is over MODEM_MAX_SAMPLES, or the key is too short. */
int modem_decode(const int16_t *pcm, size_t n, const uint8_t *key, size_t klen,
		 uint8_t *out, size_t cap, uint32_t *seq);

/* A frame is fresh only if its sequence number is newer than last and not more than MODEM_WINDOW ahead. */
int modem_fresh(uint32_t last, uint32_t seq);

/* A receiver session: one key, one sequence counter, and a count of refused frames. It closes for good after
   MODEM_REFUSE_LIMIT refusals. A refusal is counted once per decode call, not once per sync candidate, so noise
   alone cannot close a good session. */
typedef struct
{
	const uint8_t *key;
	size_t klen;
	uint32_t last;
	unsigned refused;
	int closed;
} modem_rx;

void modem_rx_open(modem_rx *rx, const uint8_t *key, size_t klen);

/* Takes one frame from pcm. Returns the payload length for a fresh, authentic frame, or -1 for anything else. */
int modem_rx_frame(modem_rx *rx, const int16_t *pcm, size_t n, uint8_t *out, size_t cap, uint32_t *seq);

/* ---- SHA-256 and HMAC-SHA256 (FIPS 180-4, RFC 2104) ---------------------- */

typedef struct
{
	uint32_t h[8];
	uint64_t bits;
	uint8_t buf[64];
	size_t fill;
} modem_sha256;

static const uint32_t modem_sha_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

#define MODEM_ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void modem_sha_compress(uint32_t h[8], const uint8_t *p)
{
	uint32_t w[64], a, b, c, d, e, f, g, hh;
	size_t i;

	for (i = 0; i < 16; i++)
		w[i] = ((uint32_t)p[4 * i] << 24) | ((uint32_t)p[4 * i + 1] << 16) |
		       ((uint32_t)p[4 * i + 2] << 8) | (uint32_t)p[4 * i + 3];
	for (i = 16; i < 64; i++)
	{
		uint32_t s0 = MODEM_ROR(w[i - 15], 7) ^ MODEM_ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
		uint32_t s1 = MODEM_ROR(w[i - 2], 17) ^ MODEM_ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}
	a = h[0];
	b = h[1];
	c = h[2];
	d = h[3];
	e = h[4];
	f = h[5];
	g = h[6];
	hh = h[7];
	for (i = 0; i < 64; i++)
	{
		uint32_t S1 = MODEM_ROR(e, 6) ^ MODEM_ROR(e, 11) ^ MODEM_ROR(e, 25);
		uint32_t ch = (e & f) ^ (~e & g);
		uint32_t t1 = hh + S1 + ch + modem_sha_k[i] + w[i];
		uint32_t S0 = MODEM_ROR(a, 2) ^ MODEM_ROR(a, 13) ^ MODEM_ROR(a, 22);
		uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
		uint32_t t2 = S0 + maj;
		hh = g;
		g = f;
		f = e;
		e = d + t1;
		d = c;
		c = b;
		b = a;
		a = t1 + t2;
	}
	h[0] += a;
	h[1] += b;
	h[2] += c;
	h[3] += d;
	h[4] += e;
	h[5] += f;
	h[6] += g;
	h[7] += hh;
}

static void modem_sha_init(modem_sha256 *c)
{
	static const uint32_t iv[8] = {
	    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
	    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
	memcpy(c->h, iv, sizeof(iv));
	c->bits = 0;
	c->fill = 0;
}

static void modem_sha_update(modem_sha256 *c, const uint8_t *p, size_t n)
{
	c->bits += (uint64_t)n * 8;
	while (n > 0)
	{
		size_t take = 64 - c->fill;

		if (take > n)
			take = n;
		memcpy(c->buf + c->fill, p, take);
		c->fill += take;
		p += take;
		n -= take;
		if (c->fill == 64)
		{
			modem_sha_compress(c->h, c->buf);
			c->fill = 0;
		}
	}
}

static void modem_sha_final(modem_sha256 *c, uint8_t out[32])
{
	uint8_t pad[72];
	uint64_t bits = c->bits;
	size_t padlen = (c->fill < 56) ? (56 - c->fill) : (120 - c->fill);
	size_t i;

	memset(pad, 0, sizeof(pad));
	pad[0] = 0x80;
	for (i = 0; i < 8; i++)
		pad[padlen + i] = (uint8_t)(bits >> (56 - 8 * i));
	modem_sha_update(c, pad, padlen + 8);
	for (i = 0; i < 8; i++)
	{
		out[4 * i] = (uint8_t)(c->h[i] >> 24);
		out[4 * i + 1] = (uint8_t)(c->h[i] >> 16);
		out[4 * i + 2] = (uint8_t)(c->h[i] >> 8);
		out[4 * i + 3] = (uint8_t)c->h[i];
	}
}

static void modem_hmac(const uint8_t *key, size_t klen, const uint8_t *msg, size_t mlen, uint8_t out[32])
{
	uint8_t k[64], inner[32];
	modem_sha256 c;
	size_t i;

	memset(k, 0, sizeof(k));
	if (klen > 64)
	{
		modem_sha_init(&c);
		modem_sha_update(&c, key, klen);
		modem_sha_final(&c, k);
	}
	else if (klen > 0)
	{
		memcpy(k, key, klen);
	}
	for (i = 0; i < 64; i++)
		k[i] ^= 0x36;
	modem_sha_init(&c);
	modem_sha_update(&c, k, 64);
	modem_sha_update(&c, msg, mlen);
	modem_sha_final(&c, inner);
	for (i = 0; i < 64; i++)
		k[i] ^= 0x36 ^ 0x5c;
	modem_sha_init(&c);
	modem_sha_update(&c, k, 64);
	modem_sha_update(&c, inner, 32);
	modem_sha_final(&c, out);
	memset(k, 0, sizeof(k));
	memset(inner, 0, sizeof(inner));
}

/* ---- framing ----------------------------------------------------------- */

#define MODEM_SYNC_WORD 0x3C96C35Au /* 5A C3 96 3C read least significant bit first */

/* One scratch buffer for the demodulated bits, sized for the largest input the decoder takes. */
static uint8_t modem_bits[(MODEM_MAX_SAMPLES / MODEM_SYM + 1) * MODEM_SYM_BITS];

static int modem_key_ok(const uint8_t *key, size_t klen)
{
	return key != 0 && klen >= MODEM_KEY_MIN;
}

size_t modem_encode_samples(size_t len)
{
	size_t syms;

	if (len > MODEM_PAYLOAD_MAX)
		return 0;
	syms = ((MODEM_PRE + MODEM_SYNC_BYTES + MODEM_HDR + len + MODEM_TAG) * 8 + MODEM_SYM_BITS - 1) / MODEM_SYM_BITS;
	return syms * MODEM_SYM + 2 * MODEM_LEAD;
}

size_t modem_encode(const uint8_t *key, size_t klen, uint32_t seq, const uint8_t *payload, size_t len,
		    int16_t *pcm, size_t cap)
{
	static const uint8_t sync[MODEM_SYNC_BYTES] = {0x5A, 0xC3, 0x96, 0x3C};
	uint8_t fb[MODEM_PRE + MODEM_SYNC_BYTES + MODEM_HDR + MODEM_PAYLOAD_MAX + MODEM_TAG];
	size_t nb, at, total, body, bit, nbits;

	if (!modem_key_ok(key, klen) || !pcm || (len > 0 && !payload) || len > MODEM_PAYLOAD_MAX)
		return 0;
	total = modem_encode_samples(len);
	if (total > cap)
		return 0;

	memset(fb, 0x55, MODEM_PRE);
	nb = MODEM_PRE;
	memcpy(fb + nb, sync, MODEM_SYNC_BYTES);
	nb += MODEM_SYNC_BYTES;
	body = nb;
	fb[nb++] = (uint8_t)len;
	fb[nb++] = (uint8_t)(seq >> 24);
	fb[nb++] = (uint8_t)(seq >> 16);
	fb[nb++] = (uint8_t)(seq >> 8);
	fb[nb++] = (uint8_t)seq;
	if (len > 0)
		memcpy(fb + nb, payload, len);
	nb += len;
	modem_hmac(key, klen, fb + body, MODEM_HDR + len, fb + nb);
	nb += MODEM_TAG;

	/* Symbols from the frame's bit stream (least significant bit of each byte first). A short last symbol is
	   padded with zero bits. */
	nbits = nb * 8;
	memset(pcm, 0, total * sizeof(int16_t));
	at = MODEM_LEAD;
	for (bit = 0; bit < nbits; bit += MODEM_SYM_BITS)
	{
		unsigned v = 0, j;
		int c, k;

		for (j = 0; j < MODEM_SYM_BITS; j++)
		{
			size_t b = bit + j;

			if (b < nbits && ((fb[b / 8] >> (b % 8)) & 1))
				v |= 1u << j;
		}
		c = (int)v + 1; /* tone 1..MODEM_TONES: c whole cycles per symbol */
		for (k = 0; k < MODEM_SYM; k++)
			pcm[at++] = (int16_t)((MODEM_AMP * modem_sin_tab[(c * k) % MODEM_SYM]) / MODEM_SCALE);
	}
	return at + MODEM_LEAD;
}

/* Reads count bytes, least significant bit first, from the bit array at bit position p. */
static int modem_read_bytes(const uint8_t *bits, size_t nb, size_t p, uint8_t *dst, size_t count)
{
	size_t i, j;

	if (p > nb || count > (nb - p) / 8)
		return 0;
	for (i = 0; i < count; i++)
	{
		uint8_t v = 0;

		for (j = 0; j < 8; j++)
			v |= (uint8_t)(bits[p + 8 * i + j] << j);
		dst[i] = v;
	}
	return 1;
}

/* Looks for a frame in the bit array. The sync word is matched with a rolling 32-bit window, so the scan costs
   one shift per bit. Sets *tagfail if a candidate had a valid sync and length but a bad tag. */
static int modem_find_frame(const uint8_t *bits, size_t nb, const uint8_t *key, size_t klen,
			    uint8_t *out, size_t cap, uint32_t *seq, int *tagfail)
{
	uint32_t window = 0;
	size_t i;

	if (nb < 32)
		return -1;
	for (i = 0; i < 32; i++)
		window |= (uint32_t)bits[i] << i;

	for (i = 0;; i++)
	{
		uint8_t buf[MODEM_HDR + MODEM_PAYLOAD_MAX + MODEM_TAG];
		uint8_t mac[MODEM_TAG];
		size_t len, p = i + 32, t;
		unsigned diff = 0;

		if (window == MODEM_SYNC_WORD && modem_read_bytes(bits, nb, p, buf, MODEM_HDR))
		{
			len = buf[0];
			if (len <= MODEM_PAYLOAD_MAX && modem_read_bytes(bits, nb, p, buf, MODEM_HDR + len + MODEM_TAG))
			{
				modem_hmac(key, klen, buf, MODEM_HDR + len, mac);
				for (t = 0; t < MODEM_TAG; t++)
					diff |= (unsigned)(mac[t] ^ buf[MODEM_HDR + len + t]);
				if (diff)
					*tagfail = 1;
				else if (len <= cap)
				{
					*seq = ((uint32_t)buf[1] << 24) | ((uint32_t)buf[2] << 16) |
					       ((uint32_t)buf[3] << 8) | buf[4];
					memcpy(out, buf + MODEM_HDR, len);
					return (int)len;
				}
			}
		}
		if (p >= nb)
			break;
		window = (window >> 1) | ((uint32_t)bits[p] << 31);
	}
	return -1;
}

static int modem_decode_core(const int16_t *pcm, size_t n, const uint8_t *key, size_t klen,
			     uint8_t *out, size_t cap, uint32_t *seq, unsigned *refused)
{
	size_t off;
	int tagfail = 0;

	if (!pcm || !out || !seq || !modem_key_ok(key, klen) || n < MODEM_SYM || n > MODEM_MAX_SAMPLES)
		return -1;

	for (off = 0; off < MODEM_SYM; off++)
	{
		size_t nb = 0, s;
		int r;

		for (s = off; s + MODEM_SYM <= n; s += MODEM_SYM)
		{
			int64_t best = -1;
			unsigned bestv = 0, j;
			int c;

			for (c = 1; c <= MODEM_TONES; c++)
			{
				int64_t mc = 0, ms = 0, e;
				int k;

				for (k = 0; k < MODEM_SYM; k++)
				{
					int64_t x = pcm[s + (size_t)k];
					size_t idx = (size_t)((c * k) % MODEM_SYM);

					mc += x * modem_cos_tab[idx];
					ms += x * modem_sin_tab[idx];
				}
				mc /= MODEM_CORR_SHIFT;
				ms /= MODEM_CORR_SHIFT;
				e = mc * mc + ms * ms;
				if (e > best)
				{
					best = e;
					bestv = (unsigned)(c - 1);
				}
			}
			for (j = 0; j < MODEM_SYM_BITS; j++)
				modem_bits[nb++] = (uint8_t)((bestv >> j) & 1);
		}
		r = modem_find_frame(modem_bits, nb, key, klen, out, cap, seq, &tagfail);
		if (r >= 0)
			return r;
	}
	if (refused && tagfail)
		*refused = 1;
	return -1;
}

int modem_decode(const int16_t *pcm, size_t n, const uint8_t *key, size_t klen,
		 uint8_t *out, size_t cap, uint32_t *seq)
{
	return modem_decode_core(pcm, n, key, klen, out, cap, seq, 0);
}

int modem_fresh(uint32_t last, uint32_t seq)
{
	uint32_t step = seq - last;

	/* step is unsigned: 0 is a repeat, anything above the window is refused, and so is a wrap back. */
	return step >= 1 && step <= MODEM_WINDOW;
}

void modem_rx_open(modem_rx *rx, const uint8_t *key, size_t klen)
{
	rx->key = key;
	rx->klen = klen;
	rx->last = 0;
	rx->refused = 0;
	rx->closed = 0;
}

static int modem_refuse(modem_rx *rx)
{
	rx->refused++;
	if (rx->refused >= MODEM_REFUSE_LIMIT)
		rx->closed = 1;
	return -1;
}

int modem_rx_frame(modem_rx *rx, const int16_t *pcm, size_t n, uint8_t *out, size_t cap, uint32_t *seq)
{
	unsigned refused = 0;
	int r;

	if (!rx || rx->closed)
		return -1;
	r = modem_decode_core(pcm, n, rx->key, rx->klen, out, cap, seq, &refused);
	if (r < 0)
		return refused ? modem_refuse(rx) : -1;
	if (!modem_fresh(rx->last, *seq))
		return modem_refuse(rx);
	rx->last = *seq;
	return r;
}

/* ---- self-test: -DMODEM_SELFTEST ---------------------------------------- */

#ifdef MODEM_SELFTEST

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* sha256 of the samples of the golden frame: payload "golden frame", seq 42, the test key. Pins the wire format. */
#define MODEM_GOLDEN "ff1ba5d9ed62dca9930870adebbfe6837293996ab2811ece1092936635889287"

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

static void test_noise(void)
{
	static const double snrs[] = {10, 6, 3, 0, -3};
	int16_t *pcm = malloc(sizeof(int16_t) * (modem_encode_samples(32) + 200));
	int16_t *noisy = malloc(sizeof(int16_t) * (modem_encode_samples(32) + 200));
	uint8_t payload[32], out[64];
	size_t i;

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
		printf("      snr %4.0f dB: %2d of 50 frames (32 bytes) decode\n", snrs[i], ok);
	}
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

int main(void)
{
	test_vectors();
	test_roundtrip();
	test_golden();
	test_bounds();
	test_wrongkey_and_replay();
	test_session();
	test_noise();
	test_bench();
	printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}

#endif /* MODEM_SELFTEST */

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
