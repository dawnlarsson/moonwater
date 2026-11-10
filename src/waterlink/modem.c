/*
	The audio modem: the link for getting data off a machine that cannot be
	trusted, or a debugging link.
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

	The tests are test/modem.c, run by the modem lane (sh test/run modem).
*/

/* The box builds this freestanding (-ffreestanding -nostdlib): the compiler's own stddef.h and stdint.h are there, but
   string.h is not, and memcpy and memset are the kernel library's, declared in lib.c ahead of any file that includes
   this one. A hosted build (the test, the receiver in tools/) gets string.h as before. */
#include <stddef.h>
#include <stdint.h>
#if __STDC_HOSTED__
#include <string.h>
#endif

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

/* As modem_rx_frame, and sets *used to the sample just past the frame taken (0 when none is taken), so a caller can
   take the frames of one capture one after another. */
int modem_rx_next(modem_rx *rx, const int16_t *pcm, size_t n, uint8_t *out, size_t cap, uint32_t *seq, size_t *used);

/* ---- datagrams over the modem ------------------------------------------ */

/* A Waterlink datagram is cut into chunks, one frame each. A chunk's payload is a four-byte head, the datagram's id
   (two bytes, big endian), the chunk's index and the chunk count, then the chunk's bytes. Every chunk but the last is
   MODEM_CHUNK_DATA bytes, so a chunk's place is index * MODEM_CHUNK_DATA. The modem's own sequence number is the only
   freshness there is: a chunk is taken only as modem_rx_next takes it. */
#define MODEM_DATAGRAM_MAX 1200u
#define MODEM_CHUNK_HEAD 4
#define MODEM_CHUNK_DATA (MODEM_PAYLOAD_MAX - MODEM_CHUNK_HEAD)
#define MODEM_CHUNKS ((MODEM_DATAGRAM_MAX + MODEM_CHUNK_DATA - 1) / MODEM_CHUNK_DATA)

/* The sender: one per session, with its own datagram ids. */
typedef struct
{
	const uint8_t *key;
	size_t klen;
	uint32_t seq;
	uint16_t id;
} modem_tx;

void modem_tx_open(modem_tx *tx, const uint8_t *key, size_t klen);

/* Writes every frame of one datagram (1 to MODEM_DATAGRAM_MAX bytes) into pcm, back to back. Returns the sample
   count, or 0 if the datagram is the wrong size or the frames do not all fit in cap; a 0 means the caller sends
   nothing from pcm. */
size_t modem_tx_datagram(modem_tx *tx, const uint8_t *dg, size_t len, int16_t *pcm, size_t cap);

/* The receiver: one session, and at most one datagram being put together. A chunk that starts another datagram, or
   that does not fit the set's count, drops the partial set. A set that never completes is never delivered. */
typedef struct
{
	modem_rx rx;
	uint16_t id;
	unsigned count;
	unsigned have;
	int building;
	size_t total;
	uint8_t part[MODEM_CHUNKS * MODEM_CHUNK_DATA];
} modem_chunk_rx;

void modem_chunk_open(modem_chunk_rx *cr, const uint8_t *key, size_t klen);

/* Takes the fresh frames of pcm one by one until one completes a datagram, which is copied to out. Returns its length
   (with *used past the frame that completed it), or 0 when pcm holds no further complete datagram (*used is what was
   taken; call again with the rest), or -1 when the session is closed or the input is refused. */
int modem_chunk_take(modem_chunk_rx *cr, const int16_t *pcm, size_t n, uint8_t *out, size_t cap, size_t *used);

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
   one shift per bit. Sets *refusal if a candidate had a valid sync and length but a bad tag, or, when gated, an
   authentic frame that is not fresh after last: such a frame is passed over, so a replay ahead of a fresh frame
   does not hide it. */
static int modem_find_frame(const uint8_t *bits, size_t nb, const uint8_t *key, size_t klen,
			    uint8_t *out, size_t cap, uint32_t *seq, int *refusal, int gated, uint32_t last, size_t *end_bit)
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
					*refusal = 1;
				else if (len <= cap)
				{
					uint32_t got = ((uint32_t)buf[1] << 24) | ((uint32_t)buf[2] << 16) |
					       ((uint32_t)buf[3] << 8) | buf[4];
					if (gated && !modem_fresh(last, got))
							*refusal = 1;
						else
						{
							*seq = got;
						memcpy(out, buf + MODEM_HDR, len);
						*end_bit = i + 32 + 8 * (MODEM_HDR + len + MODEM_TAG);
						return (int)len;
						}
				}
			}
		}
		if (p >= nb)
			break;
		window = (window >> 1) | ((uint32_t)bits[p] << 31);
	}
	return -1;
}

/* Symbol decisions for every start sample, computed once. A window that starts at s correlates with tone c as
   P*cos(c*s) + Q*sin(c*s), where P and Q are the sums of x[t]*cos and x[t]*sin over the window in absolute
   sample positions t. P and Q slide one sample at a time, so each start costs about 4*MODEM_TONES multiplies,
   not the 2*MODEM_TONES*MODEM_SYM of a window computed afresh. The result is exact integer arithmetic. */
static uint8_t modem_best[MODEM_MAX_SAMPLES];

static int modem_decode_core(const int16_t *pcm, size_t n, const uint8_t *key, size_t klen,
			     uint8_t *out, size_t cap, uint32_t *seq, unsigned *refused, int gated, uint32_t last, size_t *used)
{
	int64_t P[MODEM_TONES + 1], Q[MODEM_TONES + 1];
	size_t ia[MODEM_TONES + 1], ir[MODEM_TONES + 1], is[MODEM_TONES + 1];
	size_t s, off;
	int refusal = 0, c, k;

	if (!pcm || !out || !seq || !modem_key_ok(key, klen) || n < MODEM_SYM || n > MODEM_MAX_SAMPLES)
		return -1;

	/* The window for start 0 covers samples 0..MODEM_SYM-1. The tone indices (c*t) mod MODEM_SYM are kept per
	   tone and stepped by c each sample, with one compare and subtract, not a modulo. */
	for (c = 1; c <= MODEM_TONES; c++) {
		P[c] = 0;
		Q[c] = 0;
		for (k = 0; k < MODEM_SYM; k++) {
			size_t idx = ((size_t)c * (size_t)k) % MODEM_SYM;

			P[c] += (int64_t)pcm[k] * modem_cos_tab[idx];
			Q[c] += (int64_t)pcm[k] * modem_sin_tab[idx];
		}
		ia[c] = ((size_t)c * (MODEM_SYM - 1)) % MODEM_SYM; /* the entering sample at s = 0, s+N-1; stepped before use */
		ir[c] = 0;                                         /* the sample that leaves: the one at s-1, set at s = 1 */
		is[c] = 0;
	}
	for (s = 0; s + MODEM_SYM <= n; s++) {
		int64_t best = -1;
		unsigned bestv = 0;

		if (s > 0) {
			int64_t xa = pcm[s + MODEM_SYM - 1], xr = pcm[s - 1];

			for (c = 1; c <= MODEM_TONES; c++) {
				/* ia is for the entering sample s+N-1: step it onto that before use. ir is for the leaving sample
				   s-1, which is used as it stands and stepped after. */
				ia[c] += (size_t)c;
				if (ia[c] >= MODEM_SYM)
					ia[c] -= MODEM_SYM;
				P[c] += xa * modem_cos_tab[ia[c]] - xr * modem_cos_tab[ir[c]];
				Q[c] += xa * modem_sin_tab[ia[c]] - xr * modem_sin_tab[ir[c]];
				ir[c] += (size_t)c;
				if (ir[c] >= MODEM_SYM)
					ir[c] -= MODEM_SYM;
			}
		}
		for (c = 1; c <= MODEM_TONES; c++) {
			int64_t cs = modem_cos_tab[is[c]], ss = modem_sin_tab[is[c]];
			int64_t mc = (P[c] * cs + Q[c] * ss) >> 24;
			int64_t ms = (Q[c] * cs - P[c] * ss) >> 24;
			int64_t e = mc * mc + ms * ms;

			if (e > best) {
				best = e;
				bestv = (unsigned)(c - 1);
			}
			is[c] += (size_t)c;
			if (is[c] >= MODEM_SYM)
				is[c] -= MODEM_SYM;
		}
		modem_best[s] = (uint8_t)bestv;
	}

	for (off = 0; off < MODEM_SYM; off++) {
		size_t nb = 0, at;
		int r;

		for (at = off; at + MODEM_SYM <= n; at += MODEM_SYM) {
			unsigned j, v = modem_best[at];

			for (j = 0; j < MODEM_SYM_BITS; j++)
				modem_bits[nb++] = (uint8_t)((v >> j) & 1);
		}
		size_t endbit = 0;

		r = modem_find_frame(modem_bits, nb, key, klen, out, cap, seq, &refusal, gated, last, &endbit);
		if (r >= 0) {
			if (used)
				*used = off + ((endbit + MODEM_SYM_BITS - 1) / MODEM_SYM_BITS) * MODEM_SYM;
			return r;
		}
	}
	if (refused && refusal)
		*refused = 1;
	return -1;
}

int modem_decode(const int16_t *pcm, size_t n, const uint8_t *key, size_t klen,
		 uint8_t *out, size_t cap, uint32_t *seq)
{
	return modem_decode_core(pcm, n, key, klen, out, cap, seq, 0, 0, 0, 0);
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

int modem_rx_next(modem_rx *rx, const int16_t *pcm, size_t n, uint8_t *out, size_t cap, uint32_t *seq, size_t *used)
{
	unsigned refused = 0;
	size_t frame_end = 0;
	int r;

	if (used)
		*used = 0;
	if (!rx || rx->closed)
		return -1;
	r = modem_decode_core(pcm, n, rx->key, rx->klen, out, cap, seq, &refused, 1, rx->last, &frame_end);
	if (r < 0)
		return refused ? modem_refuse(rx) : -1;
	if (!modem_fresh(rx->last, *seq))
		return modem_refuse(rx);
	rx->last = *seq;
	if (used)
		*used = frame_end;
	return r;
}

int modem_rx_frame(modem_rx *rx, const int16_t *pcm, size_t n, uint8_t *out, size_t cap, uint32_t *seq)
{
	return modem_rx_next(rx, pcm, n, out, cap, seq, 0);
}

/* ---- datagram chunks, the code ---------------------------------------- */

void modem_tx_open(modem_tx *tx, const uint8_t *key, size_t klen)
{
	tx->key = key;
	tx->klen = klen;
	tx->seq = 0;
	tx->id = 0;
}

size_t modem_tx_datagram(modem_tx *tx, const uint8_t *dg, size_t len, int16_t *pcm, size_t cap)
{
	uint8_t payload[MODEM_PAYLOAD_MAX];
	size_t count, index, total = 0;

	if (!tx || !dg || len == 0 || len > MODEM_DATAGRAM_MAX)
		return 0;
	count = (len + MODEM_CHUNK_DATA - 1) / MODEM_CHUNK_DATA;
	for (index = 0; index < count; index++) {
		size_t take = len - index * MODEM_CHUNK_DATA, got;

		if (take > MODEM_CHUNK_DATA)
			take = MODEM_CHUNK_DATA;
		payload[0] = (uint8_t)(tx->id >> 8);
		payload[1] = (uint8_t)tx->id;
		payload[2] = (uint8_t)index;
		payload[3] = (uint8_t)count;
		memcpy(payload + MODEM_CHUNK_HEAD, dg + index * MODEM_CHUNK_DATA, take);
		got = modem_encode(tx->key, tx->klen, ++tx->seq, payload, MODEM_CHUNK_HEAD + take, pcm + total, cap - total);
		if (got == 0)
			return 0;
		total += got;
	}
	tx->id++;
	return total;
}

void modem_chunk_open(modem_chunk_rx *cr, const uint8_t *key, size_t klen)
{
	modem_rx_open(&cr->rx, key, klen);
	cr->id = 0;
	cr->count = 0;
	cr->have = 0;
	cr->building = 0;
	cr->total = 0;
}

/* Puts one chunk into its set. Returns the datagram's length when this chunk completes it, else 0. */
static size_t modem_chunk_put(modem_chunk_rx *cr, const uint8_t *p, size_t len, uint8_t *out, size_t cap)
{
	uint16_t id;
	unsigned index, count, data;

	if (len < MODEM_CHUNK_HEAD) {
		cr->building = 0;
		return 0;
	}
	id = (uint16_t)(((unsigned)p[0] << 8) | p[1]);
	index = p[2];
	count = p[3];
	data = (unsigned)(len - MODEM_CHUNK_HEAD);
	if (count == 0 || count > MODEM_CHUNKS || index >= count || data == 0 || data > MODEM_CHUNK_DATA ||
	    (index + 1 < count && data != MODEM_CHUNK_DATA)) {
		cr->building = 0;
		return 0;
	}
	if (!cr->building || id != cr->id || count != cr->count) {
		cr->building = 1;
		cr->id = id;
		cr->count = count;
		cr->have = 0;
		cr->total = 0;
	}
	memcpy(cr->part + index * MODEM_CHUNK_DATA, p + MODEM_CHUNK_HEAD, data);
	cr->have |= 1u << index;
	if (index + 1 == count)
		cr->total = (size_t)index * MODEM_CHUNK_DATA + data;
	if (cr->total == 0 || cr->have != (1u << count) - 1)
		return 0;
	cr->building = 0;
	if (cr->total > MODEM_DATAGRAM_MAX || cr->total > cap)
		return 0;
	memcpy(out, cr->part, cr->total);
	return cr->total;
}

int modem_chunk_take(modem_chunk_rx *cr, const int16_t *pcm, size_t n, uint8_t *out, size_t cap, size_t *used)
{
	uint8_t payload[MODEM_PAYLOAD_MAX];
	size_t at = 0;

	if (used)
		*used = 0;
	if (!cr || !pcm || !out)
		return -1;
	for (;;) {
		size_t took = 0;
		uint32_t seq;
		int len = modem_rx_next(&cr->rx, pcm + at, n - at, payload, sizeof payload, &seq, &took);
		size_t whole;

		if (len < 0) {
			if (used)
				*used = at;
			return cr->rx.closed ? -1 : 0;
		}
		at += took;
		whole = modem_chunk_put(cr, payload, (size_t)len, out, cap);
		if (whole) {
			if (used)
				*used = at;
			return (int)whole;
		}
	}
}
