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

	Modulation: multi-tone FSK at MODEM_RATE (96 kHz), 16-bit mono. A symbol
	is MODEM_SYM samples. It carries one of MODEM_TONES tones, tone c being c
	whole cycles in the symbol window, so the tones are orthogonal and the
	receiver needs only the sample offset, not a clock. The default is 40
	samples and 8 tones: 2400 baud, 3 bits a symbol, 7200 bit/s raw, with the
	top tone at 19.2 kHz, which ordinary audio gear passes. The 16-tone
	option (MODEM_TONES 16, at the same rate) carries 9600 bit/s, and its top
	tone is 38.4 kHz: it needs a cable and a card that pass that.

	A frame: lead-in (MODEM_PRE bytes of 0x55), sync A7 3D 69 C5 (the wire
	version: version 1 frames, with a 32-byte tag, had 5A C3 96 3C), LEN
	(one byte, 0..255), SEQ (four bytes, big-endian), payload, then a
	16-byte tag: the first 16 bytes of HMAC-SHA256 over LEN, SEQ and payload
	(the HMAC truncated to 128 bits). The receiver uses nothing until the
	tag checks. A receiver's session starts from 0, so its first frame is 1; a sender goes on from the last number it
	sent under the same key (the send section keeps it), so one recording can hold any number of sends, and the receiver takes
	each frame that follows the one before.

	Integer arithmetic only: no floating point, no libm in the decoder.
	The sine tables are fixed at build time. The tag is lib.c's: sha256_blocks
	and hash_hmac_sha256_prepared, with each session's key made ready once, as
	net.c makes its HMAC keys ready. Where this machine has no body for
	hash_hmac_sha256_prepared, the tag is made from sha256_blocks here.

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

#ifndef MODEM_RATE
#define MODEM_RATE 96000 /* the card's rate, both ways: the card must run at it, and a card at another rate is refused */
#endif
#ifndef MODEM_SYM
#define MODEM_SYM 40 /* samples per symbol: the baud rate is MODEM_RATE / MODEM_SYM */
#endif
/* Tones per symbol: a power of two below MODEM_SYM / 2. The default, 8, puts the top tone at 19.2 kHz at 96 kHz, which ordinary
   audio gear passes. The 16-tone option (-DMODEM_TONES=16, off by default) carries 9600 bit/s at the same rate, and its top
   tone is 38.4 kHz: build it only for a cable and a card that pass 38.4 kHz. The top tone of any setting is
   MODEM_TONES * MODEM_RATE / MODEM_SYM. */
#ifndef MODEM_TONES
#define MODEM_TONES 8
#endif

#define MODEM_SYM_BITS ((MODEM_TONES) == 2 ? 1 : (MODEM_TONES) == 4 ? 2 \
					     : (MODEM_TONES) == 8   ? 3 \
								    : 4)
#define MODEM_BAUD (MODEM_RATE / MODEM_SYM)
#define MODEM_PRE 4
#define MODEM_SYNC_BYTES 4
#define MODEM_HDR 5	      /* LEN and SEQ */
#define MODEM_PAYLOAD_MAX 255 /* the largest a one-byte LEN carries */
#define MODEM_MAC 32 /* HMAC-SHA256's output */
#define MODEM_TAG 16 /* a frame's tag: HMAC-SHA256 truncated to its first 16 bytes (a 128-bit tag), 16 bytes a frame less than 32 */
#define MODEM_KEY_MIN 16
#define MODEM_MAX_SAMPLES (1u << 20) /* about 10.9 seconds at 96 kHz: the most one decode call reads */
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

/* ---- HMAC-SHA256 under a key made ready once (FIPS 180-4, RFC 2104) ------ */

/*
	The block core is lib.c's sha256_blocks, which takes whole 64-byte blocks into eight words of state, and the tag is
	lib.c's hash_hmac_sha256_prepared, which takes the two states of a key's pads and a message and answers whether it made
	the tag. It has a body on x86_64 with the SHA extensions; elsewhere it answers false and the tag is made here from
	sha256_blocks, as net.c's crypto_hmac_sha256_prepared makes it. A key is made ready once per session (modem_mac_open),
	so a frame's tag costs the blocks of its message and the two that close it.
*/

#define MODEM_SHA_BLOCK 64

typedef struct
{
	uint32_t inner[8];
	uint32_t outer[8];
	int ok; /* the key is one the modem takes (MODEM_KEY_MIN bytes or more), and its pads are made */
} modem_mac;

static const uint32_t modem_sha_iv[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

/* Digests len bytes of msg into h, which already holds the done bytes (a multiple of 64) that came before them, and
   pads the tail as FIPS 180-4 does: after this h holds the digest's words of the whole message. */
static void modem_sha_run(uint32_t h[8], const uint8_t *msg, size_t len, uint64_t done)
{
	uint8_t block[2 * MODEM_SHA_BLOCK];
	size_t whole = len / MODEM_SHA_BLOCK, rest = len % MODEM_SHA_BLOCK, blocks = rest < 56 ? 1 : 2, i;
	uint64_t bits = (done + len) * 8;

	sha256_blocks(h, msg, whole);
	memset(block, 0, sizeof block);
	if (rest)
		memcpy(block, msg + whole * MODEM_SHA_BLOCK, rest);
	block[rest] = 0x80;
	for (i = 0; i < 8; i++)
		block[blocks * MODEM_SHA_BLOCK - 1 - i] = (uint8_t)(bits >> (8 * i));
	sha256_blocks(h, block, blocks);
}

static void modem_sha_bytes(const uint32_t h[8], uint8_t out[32])
{
	size_t i;

	for (i = 0; i < 8; i++)
	{
		out[4 * i] = (uint8_t)(h[i] >> 24);
		out[4 * i + 1] = (uint8_t)(h[i] >> 16);
		out[4 * i + 2] = (uint8_t)(h[i] >> 8);
		out[4 * i + 3] = (uint8_t)h[i];
	}
}

/* SHA-256 of len bytes in one call: how a key over a block is digested. */
static void modem_sha256(const uint8_t *msg, size_t len, uint8_t out[32])
{
	uint32_t h[8];

	memcpy(h, modem_sha_iv, sizeof h);
	modem_sha_run(h, msg, len, 0);
	modem_sha_bytes(h, out);
	memset(h, 0, sizeof h);
}

/* Makes a key ready: the chaining states of its two pads, which a message under the key starts from. A key over a
   block is digested first, as RFC 2104 has it. m->ok says whether the key is one the modem takes. */
static void modem_mac_open(modem_mac *m, const uint8_t *key, size_t klen)
{
	uint8_t k[MODEM_SHA_BLOCK], pad[MODEM_SHA_BLOCK];
	size_t i;

	m->ok = key != 0 && klen >= MODEM_KEY_MIN;
	if (!key)
		klen = 0;
	memset(k, 0, sizeof k);
	if (klen > MODEM_SHA_BLOCK)
		modem_sha256(key, klen, k);
	else if (klen > 0)
		memcpy(k, key, klen);
	for (i = 0; i < MODEM_SHA_BLOCK; i++)
		pad[i] = k[i] ^ 0x36;
	memcpy(m->inner, modem_sha_iv, sizeof m->inner);
	sha256_blocks(m->inner, pad, 1);
	for (i = 0; i < MODEM_SHA_BLOCK; i++)
		pad[i] = k[i] ^ 0x5c;
	memcpy(m->outer, modem_sha_iv, sizeof m->outer);
	sha256_blocks(m->outer, pad, 1);
	memset(k, 0, sizeof k);
	memset(pad, 0, sizeof pad);
}

/* The portable tag: the message under the inner state, then the inner digest under the outer state. */
static void modem_hmac_portable(const modem_mac *m, const uint8_t *msg, size_t mlen, uint8_t out[32])
{
	uint32_t h[8];
	uint8_t inner[32];

	memcpy(h, m->inner, sizeof h);
	modem_sha_run(h, msg, mlen, MODEM_SHA_BLOCK);
	modem_sha_bytes(h, inner);
	memcpy(h, m->outer, sizeof h);
	modem_sha_run(h, inner, sizeof inner, MODEM_SHA_BLOCK);
	modem_sha_bytes(h, out);
	memset(h, 0, sizeof h);
	memset(inner, 0, sizeof inner);
}

/* The tag of msg under a key made ready by modem_mac_open: lib.c's where this machine has its body, else the portable one. */
static void modem_hmac(const modem_mac *m, const uint8_t *msg, size_t mlen, uint8_t out[32])
{
	if (!hash_hmac_sha256_prepared(m->inner, m->outer, msg, mlen, out))
		modem_hmac_portable(m, msg, mlen, out);
}

/* The frame's tag: the HMAC under the key made ready, truncated to its first MODEM_TAG bytes (RFC 2104, FIPS 198-1 truncation
   to the left-most bits). The full MODEM_MAC bytes are made and the rest is dropped; the comparison is over MODEM_TAG bytes only. */
static void modem_tag(const modem_mac *m, const uint8_t *msg, size_t mlen, uint8_t out[MODEM_TAG])
{
	uint8_t mac[MODEM_MAC];

	modem_hmac(m, msg, mlen, mac);
	memcpy(out, mac, MODEM_TAG);
	memset(mac, 0, sizeof(mac));
}

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
	modem_mac mac;
	uint32_t last;
	unsigned refused;
	int closed;
	int consume; /* a consumer of the capture: a refusal moves *used past the frames refused */
} modem_rx;

void modem_rx_open(modem_rx *rx, const uint8_t *key, size_t klen);

/* Takes one frame from pcm. Returns the payload length for a fresh, authentic frame, or -1 for anything else. */
int modem_rx_frame(modem_rx *rx, const int16_t *pcm, size_t n, uint8_t *out, size_t cap, uint32_t *seq);

/* As modem_rx_frame, and sets *used to the sample just past the frame taken (0 when none is taken), so a caller can
   take the frames of one capture one after another. The frame taken is the fresh one that ends first in pcm, whatever
   its symbol phase; a refused candidate is passed over and the rest of pcm is still searched. */
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
	modem_mac mac;
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

/* ---- framing ----------------------------------------------------------- */

/* The sync word is the wire version: a frame with a 16-byte tag carries A7 3D 69 C5, read least significant bit first. Version 1
   (a 32-byte tag) carried 5A C3 96 3C, so a receiver of one version never reads frames of the other as its own. */
#define MODEM_SYNC_WORD 0xC5693DA7u

/* One scratch buffer for the demodulated bits, sized for the largest input the decoder takes. */
static uint8_t modem_bits[(MODEM_MAX_SAMPLES / MODEM_SYM + 1) * MODEM_SYM_BITS];

size_t modem_encode_samples(size_t len)
{
	size_t syms;

	if (len > MODEM_PAYLOAD_MAX)
		return 0;
	syms = ((MODEM_PRE + MODEM_SYNC_BYTES + MODEM_HDR + len + MODEM_TAG) * 8 + MODEM_SYM_BITS - 1) / MODEM_SYM_BITS;
	return syms * MODEM_SYM + 2 * MODEM_LEAD;
}

static size_t modem_encode_mac(const modem_mac *mac, uint32_t seq, const uint8_t *payload, size_t len,
			       int16_t *pcm, size_t cap)
{
	static const uint8_t sync[MODEM_SYNC_BYTES] = {0xA7, 0x3D, 0x69, 0xC5}; /* MODEM_SYNC_WORD, bytes in order */
	uint8_t fb[MODEM_PRE + MODEM_SYNC_BYTES + MODEM_HDR + MODEM_PAYLOAD_MAX + MODEM_TAG];
	size_t nb, at, total, body, bit, nbits;
	int16_t tone[MODEM_TONES + 1][MODEM_SYM];
	int c, k;

	if (!mac->ok || !pcm || (len > 0 && !payload) || len > MODEM_PAYLOAD_MAX)
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
	modem_tag(mac, fb + body, MODEM_HDR + len, fb + nb);
	nb += MODEM_TAG;

	/* Symbols from the frame's bit stream (least significant bit of each byte first). A short last symbol is
	   padded with zero bits. */
	nbits = nb * 8;
	memset(pcm, 0, total * sizeof(int16_t));
	/* One symbol per tone, computed once for this frame: a symbol is a copy of its tone's samples, so the frame's
	   samples are written by one copy per symbol and no sample is computed twice. Tone c is c whole cycles. */
	for (c = 1; c <= MODEM_TONES; c++)
		for (k = 0; k < MODEM_SYM; k++)
			tone[c][k] = (int16_t)((MODEM_AMP * modem_sin_tab[(c * k) % MODEM_SYM]) / MODEM_SCALE);
	at = MODEM_LEAD;
	for (bit = 0; bit < nbits; bit += MODEM_SYM_BITS)
	{
		unsigned v = 0, j;

		for (j = 0; j < MODEM_SYM_BITS; j++)
		{
			size_t b = bit + j;

			if (b < nbits && ((fb[b / 8] >> (b % 8)) & 1))
				v |= 1u << j;
		}
		memcpy(pcm + at, tone[(int)v + 1], sizeof tone[0]);
		at += MODEM_SYM;
	}
	return at + MODEM_LEAD;
}

size_t modem_encode(const uint8_t *key, size_t klen, uint32_t seq, const uint8_t *payload, size_t len,
		    int16_t *pcm, size_t cap)
{
	modem_mac mac;
	size_t n;

	modem_mac_open(&mac, key, klen);
	n = modem_encode_mac(&mac, seq, payload, len, pcm, cap);
	memset(&mac, 0, sizeof mac);
	return n;
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

/* Notes that the bits before bit are a frame the search refused, so a consumer of the capture may move past it. */
static void modem_cut(size_t *cut_bit, size_t bit)
{
	if (bit > *cut_bit)
		*cut_bit = bit;
}

/* The frames one decode call has authenticated. The phases near the true one all decode the same frame, so it is found
   at many phases with the same bytes, and its tag is the same test at each: a frame whose bytes (length, sequence, payload
   and tag) are one already checked is taken from here, not by another HMAC. The key is the call's, so a verdict holds. */
#define MODEM_MEMO 4
typedef struct
{
	uint8_t frame[MODEM_MEMO][MODEM_HDR + MODEM_PAYLOAD_MAX + MODEM_TAG];
	size_t total[MODEM_MEMO];
	unsigned count, next;
} modem_memo;

static int modem_memo_has(const modem_memo *m, const uint8_t *bytes, size_t total)
{
	unsigned i;
	size_t t;

	for (i = 0; i < m->count; i++) {
		unsigned diff = 0;

		if (m->total[i] != total)
			continue;
		for (t = 0; t < total; t++)
			diff |= (unsigned)(m->frame[i][t] ^ bytes[t]);
		if (!diff)
			return 1;
	}
	return 0;
}

static void modem_memo_add(modem_memo *m, const uint8_t *bytes, size_t total)
{
	memcpy(m->frame[m->next], bytes, total);
	m->total[m->next] = total;
	m->next = (m->next + 1) % MODEM_MEMO;
	if (m->count < MODEM_MEMO)
		m->count++;
}

/* Looks for a frame in the bit array. The sync word is matched with a rolling 32-bit window, so the scan costs
   one shift per bit. Sets *refusal if a candidate had a valid sync and length but a bad tag, or, when gated, an
   authentic frame that is not fresh after last: such a frame is passed over, so a replay ahead of a fresh frame
   does not hide it. */
static int modem_find_frame(const uint8_t *bits, size_t nb, const modem_mac *mac,
			    uint8_t *out, size_t cap, uint32_t *seq, int *refusal, int gated, uint32_t last, size_t *end_bit, size_t *cut_bit,
			    modem_memo *memo)
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
		uint8_t tag[MODEM_TAG];
		size_t len, p = i + 32, t;
		unsigned diff = 0;

		if (window == MODEM_SYNC_WORD && modem_read_bytes(bits, nb, p, buf, MODEM_HDR))
		{
			len = buf[0];
			if (len <= MODEM_PAYLOAD_MAX && modem_read_bytes(bits, nb, p, buf, MODEM_HDR + len + MODEM_TAG))
			{
				if (modem_memo_has(memo, buf, MODEM_HDR + len + MODEM_TAG))
					diff = 0;
				else {
					modem_tag(mac, buf, MODEM_HDR + len, tag);
					for (t = 0; t < MODEM_TAG; t++)
						diff |= (unsigned)(tag[t] ^ buf[MODEM_HDR + len + t]);
					if (!diff)
						modem_memo_add(memo, buf, MODEM_HDR + len + MODEM_TAG);
				}
				if (diff) {
					*refusal = 1;
					modem_cut(cut_bit, i + 32 + 8 * (MODEM_HDR + len + MODEM_TAG));
				}
				else if (len <= cap)
				{
					uint32_t got = ((uint32_t)buf[1] << 24) | ((uint32_t)buf[2] << 16) |
					       ((uint32_t)buf[3] << 8) | buf[4];
					if (gated && !modem_fresh(last, got))
					{
						*refusal = 1;
						modem_cut(cut_bit, i + 32 + 8 * (MODEM_HDR + len + MODEM_TAG));
					}
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
   sample positions t. P and Q slide one sample at a time, so each start costs about 7*MODEM_TONES multiplies,
   not the 2*MODEM_TONES*MODEM_SYM of a window computed afresh. The result is exact integer arithmetic. */
static uint8_t modem_best[MODEM_MAX_SAMPLES];
#define MODEM_BLOCK 128 /* starts per block of the sweep: the block's state stays in L1 while each tone runs over it */
#define MODEM_TIE_BITS 5 /* the low bits of a block entry hold the tone, so one compare takes the energy and the tie rule */

/* The energy of a tone at a start, from its sums P and Q and the table values cs and ss at its rotation c*s mod
   MODEM_SYM: the squared magnitude of the two rounded products (P*cos + Q*sin) >> 24 and (Q*cos - P*sin) >> 24. The four
   products are three multiplies, by the Gauss identity: k1 = cos*(P+Q), then P*cos + Q*sin = k1 - Q*(cos-sin) and
   Q*cos - P*sin = k1 - P*(cos+sin). Exact, so the shifted values are the ones the four-multiply form gives. */
static int64_t modem_tone_energy(int64_t P, int64_t Q, int64_t cs, int64_t ss)
{
	int64_t k1 = cs * (P + Q);
	int64_t mc = (k1 - Q * (cs - ss)) >> 24;
	int64_t ms = (k1 - P * (cs + ss)) >> 24;

	return mc * mc + ms * ms;
}

static int modem_decode_core(const int16_t *pcm, size_t n, const modem_mac *mac,
			     uint8_t *out, size_t cap, uint32_t *seq, unsigned *refused, int gated, uint32_t last, size_t *used,
			     size_t *cut)
{
	int64_t P[MODEM_TONES + 1], Q[MODEM_TONES + 1];
	size_t ix[MODEM_TONES + 1];
	int64_t blk_e[MODEM_BLOCK], blk_d[MODEM_BLOCK];
	size_t s, s0, s1, off, cut_samples = 0;
	int refusal = 0, c, k;
	uint8_t pick[MODEM_PAYLOAD_MAX];
	uint32_t pick_seq = 0;
	size_t pick_end = 0, pick_len = 0;
	int found = 0;
	modem_memo memo;

	if (!pcm || !out || !seq || !mac->ok || n < MODEM_SYM || n > MODEM_MAX_SAMPLES)
		return -1;
	memo.count = 0;
	memo.next = 0;

	/* The window for start 0 covers samples 0..MODEM_SYM-1: P and Q are the sums over absolute positions t, with the
	   tone index (c*t) mod MODEM_SYM. Moving the window from s-1 to s takes out the sample at s-1 and puts in the one at
	   s+N-1, and those two share a tone index, since they are MODEM_SYM apart: the update is one difference times one table
	   entry. ix[c] holds c*(s-1) mod MODEM_SYM for that update, then steps by c to c*s, the rotation start s is read at. */
	for (c = 1; c <= MODEM_TONES; c++) {
		P[c] = 0;
		Q[c] = 0;
		ix[c] = 0;
		for (k = 0; k < MODEM_SYM; k++) {
			size_t idx = ((size_t)c * (size_t)k) % MODEM_SYM;

			P[c] += (int64_t)pcm[k] * modem_cos_tab[idx];
			Q[c] += (int64_t)pcm[k] * modem_sin_tab[idx];
		}
	}
	/* Start 0 is read at rotation 0, where ix already is. The starts after it go in blocks of MODEM_BLOCK. Within a block
	   each tone runs through its starts with its sums in registers, and the table values carry from one start to the
	   next: a start's update uses the values its predecessor was read at. The block keeps one packed entry per start,
	   energy << MODEM_TIE_BITS | (the tone's index, complemented): a larger entry is a larger energy, and at a tie the
	   lower tone has the larger low bits, so one strict compare in tone order gives the same tie rule as before. Each
	   tone's state is carried from one block to the next in P, Q and ix. */
	{
		int64_t cs0 = modem_cos_tab[0], ss0 = modem_sin_tab[0];
		int64_t best = -1;
		unsigned bestv = 0;

		for (c = 1; c <= MODEM_TONES; c++) {
			int64_t e = modem_tone_energy(P[c], Q[c], cs0, ss0);

			if (e > best) {
				best = e;
				bestv = (unsigned)(c - 1);
			}
		}
		modem_best[0] = (uint8_t)bestv;
	}
	for (s0 = 1; s0 + MODEM_SYM <= n; s0 = s1) {
		size_t starts = n - MODEM_SYM + 1; /* the starts are 0 .. n - MODEM_SYM: this is one past the last */

		s1 = s0 + MODEM_BLOCK < starts ? s0 + MODEM_BLOCK : starts;
		for (s = s0; s < s1; s++) {
			blk_d[s - s0] = (int64_t)pcm[s + MODEM_SYM - 1] - pcm[s - 1];
			blk_e[s - s0] = -1;
		}
		for (c = 1; c <= MODEM_TONES; c++) {
			int64_t Pc = P[c], Qc = Q[c];
			size_t r = ix[c], t, len = s1 - s0;
			int64_t cp = modem_cos_tab[r], sp = modem_sin_tab[r];
			int64_t tie = (int64_t)((1u << MODEM_TIE_BITS) - 1u - (unsigned)(c - 1));

			for (t = 0; t < len; t++) {
				int64_t d = blk_d[t], cs, ss, e;

				Pc += d * cp;
				Qc += d * sp;
				r += (size_t)c;
				if (r >= MODEM_SYM)
					r -= MODEM_SYM;
				cs = modem_cos_tab[r];
				ss = modem_sin_tab[r];
				cp = cs;
				sp = ss;
				e = (modem_tone_energy(Pc, Qc, cs, ss) << MODEM_TIE_BITS) | tie;
				if (e > blk_e[t])
					blk_e[t] = e;
			}
			P[c] = Pc;
			Q[c] = Qc;
			ix[c] = r;
		}
		for (s = s0; s < s1; s++)
			modem_best[s] = (uint8_t)((1u << MODEM_TIE_BITS) - 1u - (unsigned)(blk_e[s - s0] & ((1 << MODEM_TIE_BITS) - 1)));
	}

	/* Every phase is searched, and the frame that ends first in the capture is the one taken. A capture can hold frames
	   at different phases (two sends recorded one after another), so taking the first phase that has a frame would pass
	   over a frame that comes earlier in time at another phase. A refused candidate at any phase is passed over, not
	   the end of the search. */
	for (off = 0; off < MODEM_SYM; off++) {
		size_t nb = 0, at, endbit = 0, end, cut_bit = 0;
		uint8_t cand[MODEM_PAYLOAD_MAX];
		uint32_t cand_seq = 0;
		int r;

		for (at = off; at + MODEM_SYM <= n; at += MODEM_SYM) {
			unsigned j, v = modem_best[at];

			for (j = 0; j < MODEM_SYM_BITS; j++)
				modem_bits[nb++] = (uint8_t)((v >> j) & 1);
		}

		r = modem_find_frame(modem_bits, nb, mac, cand, cap, &cand_seq, &refusal, gated, last, &endbit, &cut_bit, &memo);
		if (r < 0) {
			if (cut_bit) {
				size_t at_sample = off + ((cut_bit + MODEM_SYM_BITS - 1) / MODEM_SYM_BITS) * MODEM_SYM;

				if (at_sample > cut_samples)
					cut_samples = at_sample;
			}
			continue;
		}
		end = off + ((endbit + MODEM_SYM_BITS - 1) / MODEM_SYM_BITS) * MODEM_SYM;
		if (!found || end < pick_end) {
			found = 1;
			pick_end = end;
			pick_len = (size_t)r;
			pick_seq = cand_seq;
			memcpy(pick, cand, pick_len);
		}
	}
	if (found) {
		memcpy(out, pick, pick_len);
		*seq = pick_seq;
		if (used)
			*used = pick_end;
		return (int)pick_len;
	}
	if (cut)
		*cut = cut_samples;
	if (refused && refusal)
		*refused = 1;
	return -1;
}

int modem_decode(const int16_t *pcm, size_t n, const uint8_t *key, size_t klen,
		 uint8_t *out, size_t cap, uint32_t *seq)
{
	modem_mac mac;
	int r;

	modem_mac_open(&mac, key, klen);
	r = modem_decode_core(pcm, n, &mac, out, cap, seq, 0, 0, 0, 0, 0);
	memset(&mac, 0, sizeof mac);
	return r;
}

int modem_fresh(uint32_t last, uint32_t seq)
{
	uint32_t step = seq - last;

	/* step is unsigned: 0 is a repeat, anything above the window is refused, and so is a wrap back. */
	return step >= 1 && step <= MODEM_WINDOW;
}

void modem_rx_open(modem_rx *rx, const uint8_t *key, size_t klen)
{
	modem_mac_open(&rx->mac, key, klen);
	rx->last = 0;
	rx->refused = 0;
	rx->closed = 0;
	rx->consume = 0;
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
	size_t frame_end = 0, cut = 0;
	int r;

	if (used)
		*used = 0;
	if (!rx || rx->closed)
		return -1;
	r = modem_decode_core(pcm, n, &rx->mac, out, cap, seq, &refused, 1, rx->last, &frame_end, &cut);
	if (r < 0)
	{
		if (used && rx->consume)
			*used = cut;
		return refused ? modem_refuse(rx) : -1;
	}
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
	modem_mac_open(&tx->mac, key, klen);
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
		got = modem_encode_mac(&tx->mac, ++tx->seq, payload, MODEM_CHUNK_HEAD + take, pcm + total, cap - total);
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
			at += took;
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

/* ---- the send: a file over the sound card -------------------------- */

/*
        The box's end of the audio modem: `moonwater link modem send FILE`, and `link push` when the transport is modem.

        The file is cut into datagrams of 1196 bytes, each sent as its offset (four bytes, big endian) and its data. Each datagram
        is cut into frames by modem_tx_datagram (above), and the frames' sound is played through the sound server as a program
        of its own, the way `moonwater audio test` plays its tone: one stream for the whole file, drained at the end. Nothing is
        acknowledged and nothing is sent again. The receiver is tools/modem_rx.c on the Mac, which writes each datagram at the
        offset it came with and reports a gap where one did not arrive.

        Nothing here reads what comes back: the modem has no way back, so the box never runs a byte of anything it receives.

        A send goes on from the last one made with the same key: the sequence number and the datagram id of the last frame are
        kept in LINK_MODEM_SEQ_PATH (modem_seq_load, modem_seq_save), so one recording can hold any number of sends. The card
        must run at MODEM_RATE: the symbols are timed for it, and a card at another rate is refused rather than sent to.

        While `link transport modem` says so, the listener carries Waterlink's datagrams over the modem too (the last part of this
        section): the same key, the same freshness rule, and two counts kept in records of their own.

        The shell includes this section after the sound server's client (src/audio/service.c, and control.c beside it),
        which it uses. The self-test and the receiver include modem.c with MODEM_NO_SEND defined, and get the codec alone.
*/

#ifndef MODEM_NO_SEND

_Static_assert(MODEM_DATAGRAM_MAX == WATERLINK_DATAGRAM && MODEM_CHUNKS * MODEM_CHUNK_DATA >= WATERLINK_DATAGRAM,
               "the modem's datagrams must be the size of a Waterlink datagram");

#define MODEM_SEND_PORTION (MODEM_DATAGRAM_MAX - 4) /* file bytes in a datagram: the other four are its offset */
/* The most samples one frame takes: a full 255-byte payload, lead and tail included. The most one datagram takes is
   MODEM_CHUNKS of them. modem_tx_datagram refuses a datagram that does not fit, so a short buffer is a refusal, not a
   wrong sound. */
#define MODEM_FRAME_SAMPLES_MAX ((((MODEM_PRE + MODEM_SYNC_BYTES + MODEM_HDR + MODEM_PAYLOAD_MAX + MODEM_TAG) * 8 + MODEM_SYM_BITS - 1) / MODEM_SYM_BITS) * MODEM_SYM + 2 * MODEM_LEAD)
#define MODEM_SEND_SAMPLES ((size_t)MODEM_CHUNKS * MODEM_FRAME_SAMPLES_MAX)
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

/* Plays count mono samples through the open stream c, as the same sound in both channels. Answers 0, or -1 when the
   connection failed. Each write is waited for within c's budget, which the call starts. */
static bipolar modem_play_samples(struct ctl *c, unsigned long channel, const int16_t address_to pcm, positive count)
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
                ctl_budget(c);
                ctl_put_be(modem_frame, (unsigned long)(frames * 4));
                ctl_put_be(modem_frame + 4, channel);
                ctl_put_be(modem_frame + 8, 0), ctl_put_be(modem_frame + 12, 0), ctl_put_be(modem_frame + 16, 0);
                if (c->transport.send(c->transport.context, modem_frame, 20 + frames * 4, ctl_left(c)) < 0)
                        return -1;
                at += frames;
        }
        return 0;
}

/* Plays count mono samples through the file's stream. Answers 0, or -1 when the connection failed. */
static bipolar modem_send_pcm(unsigned long channel, const int16_t address_to pcm, positive count)
{
        return modem_play_samples(&modem_ctl, channel, pcm, count);
}

/*
        The count a send goes on from, kept in one record of LINK_MODEM_SEQ_PATH: the key's fingerprint (an HMAC of a fixed
        label under the key, so the record counts for that key and no other), the last frame's sequence number, and the
        datagram id after it, both big endian. A record for another key, or none, counts from 0.
*/
#define MODEM_SEQ_RECORD (MODEM_MAC + 4 + 2) /* the fingerprint is the full HMAC: the records a version 1 box kept still read */

static void modem_seq_fingerprint(const uint8_t *key, uint8_t out[MODEM_MAC])
{
        static const uint8_t label[] = "waterlink modem sequence";

        modem_mac mac;

        modem_mac_open(&mac, key, 32);
        modem_hmac(&mac, label, sizeof label - 1, out);
        memset(&mac, 0, sizeof mac);
}

/* Reads the count for key into seq and id. Answers 0, or the negative error of a file that is there and is not a private
   record: that is refused rather than read as a count of 0, which would send numbers the receiver has already taken. */
static bipolar modem_seq_load(string_address path, const uint8_t *key, uint32_t *seq, uint16_t *id)
{
        uint8_t record[MODEM_SEQ_RECORD], fingerprint[MODEM_MAC];
        positive got = 0;
        bipolar read;
        unsigned diff = 0;

        *seq = 0;
        *id = 0;
        modem_seq_fingerprint(key, fingerprint);
        read = link_read_private_records(path, record, sizeof record, sizeof record, &got);
        if (read == -ENOENT)
                return 0;
        if (read < 0)
                return read;
        if (got != sizeof record)
                return -EIO;
        for (positive i = 0; i < MODEM_MAC; i++)
                diff |= (unsigned)(record[i] ^ fingerprint[i]);
        if (diff)
                return 0;
        *seq = ((uint32_t)record[MODEM_MAC] << 24) | ((uint32_t)record[MODEM_MAC + 1] << 16) |
               ((uint32_t)record[MODEM_MAC + 2] << 8) | (uint32_t)record[MODEM_MAC + 3];
        *id = (uint16_t)(((unsigned)record[MODEM_MAC + 4] << 8) | record[MODEM_MAC + 5]);
        return 0;
}

/* Writes the count for key, whole or not at all, before the frames that carry it are played. */
static bipolar modem_seq_save(string_address path, const uint8_t *key, uint32_t seq, uint16_t id)
{
        uint8_t record[MODEM_SEQ_RECORD];

        modem_seq_fingerprint(key, record);
        record[MODEM_MAC] = (uint8_t)(seq >> 24);
        record[MODEM_MAC + 1] = (uint8_t)(seq >> 16);
        record[MODEM_MAC + 2] = (uint8_t)(seq >> 8);
        record[MODEM_MAC + 3] = (uint8_t)seq;
        record[MODEM_MAC + 4] = (uint8_t)(id >> 8);
        record[MODEM_MAC + 5] = (uint8_t)id;
        return link_file_replace(path, record, sizeof record, true);
}

/*
        What is sent is a stream: a marker byte, then the file's bytes. The marker is MODEM_MARK_GZIP (1) when the bytes are a gzip
        member (RFC 1952), made by the shell's own gzip into LINK_MODEM_ZIP_PATH, and MODEM_MARK_RAW (0) when they are the file as
        it is. The gzip copy is sent only when it is smaller. The marker is the stream's first byte, so it is the first datagram's
        first byte, and the receiver reads it before anything else. The copy is removed when the send is over, however it ended.
*/
#define LINK_MODEM_ZIP_PATH "/root/link.modem.gz"
#define MODEM_MARK_RAW 0
#define MODEM_MARK_GZIP 1

static bipolar modem_zip_fd = -1;

static void modem_zip_drop(void)
{
        if (modem_zip_fd >= 0)
                (void)system_close(modem_zip_fd);
        modem_zip_fd = -1;
        (void)system_remove_at(AT_FDCWD, LINK_MODEM_ZIP_PATH, 0);
}

/*
        Chooses what is sent from in, the file: *src is the descriptor the stream is read from, rewound, *mark is its marker and
        *stream its length, marker included (1 for an empty file, which the send refuses). Answers 0, or the negative error of a
        file that cannot be read, or cannot be compressed.
*/
static bipolar modem_stream_choose(bipolar in, bipolar *src, uint8_t *mark, positive *stream)
{
        bipolar raw, zipped;

        raw = system_seek(in, 0, SEEK_END);
        if (raw < 0)
                return raw;
        if (system_seek(in, 0, SEEK_SET) < 0)
                return -EIO;
        (void)system_remove_at(AT_FDCWD, LINK_MODEM_ZIP_PATH, 0);
        modem_zip_fd = system_open_at_mode(AT_FDCWD, LINK_MODEM_ZIP_PATH,
                                           FILE_READ_WRITE | FILE_CREATE | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (modem_zip_fd < 0)
                return modem_zip_fd;
        if (gzip_stream(in, modem_zip_fd, false, null) != 0)
                return -EIO;
        zipped = system_seek(modem_zip_fd, 0, SEEK_END);
        if (zipped < 0 || system_seek(modem_zip_fd, 0, SEEK_SET) < 0)
                return -EIO;
        if (zipped < raw)
        {
                *src = modem_zip_fd;
                *mark = MODEM_MARK_GZIP;
                *stream = (positive)zipped + 1;
                return 0;
        }
        if (system_seek(in, 0, SEEK_SET) < 0)
                return -EIO;
        *src = in;
        *mark = MODEM_MARK_RAW;
        *stream = (positive)raw + 1;
        return 0;
}

/*
        Sends FILE over the audio modem to the Mac's receiver, as the stream modem_stream_choose makes of it. Answers 0 when every
        datagram was played, else what the verb should say: no key, no file, no sound server, or a connection that broke part of
        the way (the datagrams before it were played, and the receiver will say which bytes it did not get).
*/
static b32 modem_send_file(string_address path)
{
        uint8_t key[32];
        uint8_t datagram[MODEM_DATAGRAM_MAX];
        modem_tx tx;
        unsigned long channel, sent_bytes = 0, sent_datagrams = 0;
        size_t samples;
        struct ctl_w w;
        struct audio_state page;
        uint32_t first_seq;
        uint16_t first_id;
        uint8_t mark = MODEM_MARK_RAW;
        positive stream = 0;
        bipolar in, src, got;

        scope_exit(crypto_forget(key, sizeof key));
        scope_exit(modem_zip_drop());
        if (link_secret_at(LINK_MODEM_PATH, (p8 address_to)key, false) < 0)
                return host_refuse("there is no modem key: moonwater link modem key makes one, and the receiver needs the same\n");
        in = system_open_at(AT_FDCWD, path, FILE_READ | O_CLOEXEC);
        return_if(in < 0, host_fail(path, in));
        scope_exit(system_close(in));
        got = modem_stream_choose(in, &src, &mark, &stream);
        if (got < 0)
                return host_refuse("%s could not be read or compressed for the modem (error %p): nothing is sent\n", path, (positive)(-got));
        if (stream <= 1)
                return host_refuse("%s is empty: nothing to send\n", path);
        if (modem_seq_load(LINK_MODEM_SEQ_PATH, key, &first_seq, &first_id) < 0)
                return host_refuse("%s is not a private record, so the modem's count cannot be read and nothing is sent (remove it to count from 0 again)\n",
                                   LINK_MODEM_SEQ_PATH);

        modem_ctl.timeout_ms = MODEM_SEND_TIMEOUT_MS;
        if (ctl_connect(&modem_ctl, AUDIO_SOCKET) < 0)
                return host_refuse("no sound server answers on %s: the modem has no sound card to play on (moonwater audio)\n", AUDIO_SOCKET);
        if (!audio_stream_open(&modem_ctl, "Waterlink modem", "modem", MODEM_RATE, &channel))
        {
                ctl_disconnect(&modem_ctl);
                return 1;
        }
        //      The stream asked the card for MODEM_RATE when the output was idle, and the server plays it at the card's rate. A card
        //      that runs at another rate (busy at it, or one that does not offer it) would play every symbol out of time: the stream
        //      is deleted, and nothing is sent to it.
        if (audio_test_page(&page) && page.rate != MODEM_RATE)
        {
                ctl_begin(&modem_ctl, &w, 4);
                ctl_w_u32(&w, channel);
                ctl_exchange(&modem_ctl, w.len);
                ctl_disconnect(&modem_ctl);
                return host_refuse("the sound card runs at %p Hz, and the modem's symbols are timed for %p Hz: nothing is sent on a card at another rate\n",
                                   (positive)page.rate, (positive)MODEM_RATE);
        }
        modem_tx_open(&tx, key, sizeof key);
        tx.seq = first_seq;
        tx.id = first_id;

        for (;;)
        {
                if (!sent_datagrams)
                {
                        //      The first datagram begins with the marker, the stream's first byte.
                        datagram[4] = mark;
                        got = modem_send_read(src, (p8 address_to)(datagram + 5), MODEM_SEND_PORTION - 1);
                        if (got >= 0)
                                got++;
                }
                else
                        got = modem_send_read(src, (p8 address_to)(datagram + 4), MODEM_SEND_PORTION);
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
                //      The count is written before this datagram's sound is played, so a later send cannot reuse a number
                //      the receiver may already hold.
                if (modem_seq_save(LINK_MODEM_SEQ_PATH, key, tx.seq, tx.id) < 0)
                {
                        ctl_disconnect(&modem_ctl);
                        return host_refuse("the count could not be saved to %s: the send is stopped before this datagram is played\n", LINK_MODEM_SEQ_PATH);
                }
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
        host_say(log, host_label "sent %p bytes in %p datagrams over the audio modem, the file as %s (the receiver writes it back as it was); nothing is acknowledged; the last frame is %p (a receiver for a later recording takes --after %p)\n",
                 (positive)sent_bytes, (positive)sent_datagrams, mark == MODEM_MARK_GZIP ? "gzip" : "raw", (positive)tx.seq, (positive)tx.seq);
        return 0;
}

/* ---- Waterlink's datagrams over the modem ----------------------------- */

/*
        While `link transport modem` says so and the modem is ready, the listener carries the link's datagrams over the modem
        (link_send_to, service.c): each one goes into a queue instead of onto the socket, and the queue is played through a
        playback stream at the modem's rate, never more than MODEM_LEAD_US ahead of the sound server's clock. The sound card's
        record stream is read in the same turn: its frames go into a window that modem_chunk_take searches, and each datagram it
        completes enters the link (link_modem_heard) as one that came from nowhere in particular, so the session keeps its place.

        The two counts are kept in records of their own, as the send's count is: LINK_MODEM_SEQ_PATH for the last frame sent under
        the key (written before a datagram's sound is played), and LINK_MODEM_RX_PATH for the last frame taken from the far side.
        A restart of the listener, or the transport going to the network and back, keeps the freshness rule: the far side's count
        is not taken from zero, which would refuse every frame it has sent since. A full queue drops the datagram, which the link
        sends again on its own timers.

        Both machines need the same modem key, and a cable from each one's line out to the other's line in. A machine that hears
        its own sound takes its own frames for the far side's: the modem cannot tell them apart.
*/

#define MODEM_QUEUE 8          /* datagrams waiting to be played: about twenty seconds of the modem's time */
#define MODEM_LEAD_US 500000   /* the most sound queued ahead of the sound server's clock: half a second */
#define MODEM_IN_SAMPLES 65536 /* captured samples held: 0.68 seconds at 96 kHz, twice the longest frame */
#define MODEM_IN_STEP 8192     /* new captured samples that bring the window to a search */
#define MODEM_IN_POLL_US 20000 /* how often a listening modem looks at its capture, when nothing else wakes it */
#define MODEM_CHECK_US 1000000 /* how often the transport setting is read */
#define MODEM_RETRY_US 5000000 /* before a modem that would not open, or that broke, is tried again */
#define MODEM_FRAME_IN 32768   /* the most a record frame carries: the server's chunk is 16 KB */
#define MODEM_IO_MS 1000       /* the longest a frame's bytes are waited for, once its first byte is there */

/*
        The retransmission floor and the window's ceiling over the modem. A datagram put when the queue has room waits behind
        the MODEM_QUEUE - 1 others and the one being written, then plays for its own air: MODEM_QUEUE + 1 datagrams at most.
        Its acknowledgement is one more datagram at most, and the sound server plays up to MODEM_LEAD_US after the last sample
        is written. The floor is that much air and that lead; the window is no bigger than the queue holds.
*/
#define MODEM_DATAGRAM_AIR_US ((p64)MODEM_SEND_SAMPLES * 1000000u / MODEM_RATE)
#define MODEM_RTO_FLOOR_US ((MODEM_QUEUE + 2) * MODEM_DATAGRAM_AIR_US + MODEM_LEAD_US)
#define MODEM_WINDOW_CEILING ((p64)MODEM_QUEUE * WATERLINK_DATAGRAM)

static struct
{
        p8 bytes[WATERLINK_DATAGRAM];
        positive length;
        p64 when; /* the send time the link gave the datagram: the time its frames were sent */
} modem_queue[MODEM_QUEUE];
static positive modem_queue_at, modem_queue_count;
static p64 modem_wl_when; /* the send time of the datagram being written, while modem_wl_count is set */

static struct ctl modem_play, modem_record;
static unsigned long modem_play_channel, modem_record_channel;
static bool modem_open;
static bool modem_warned;
static p64 modem_checked, modem_retry_at, modem_busy_until;
static uint8_t modem_key[32];
static modem_tx modem_wl_tx;
static modem_chunk_rx modem_wl_rx;
static positive modem_wl_count, modem_wl_at;
static int16_t modem_wl_pcm[MODEM_SEND_SAMPLES];
static int16_t modem_in[MODEM_IN_SAMPLES];
static positive modem_in_fill, modem_in_seen;
static unsigned char modem_frame_in[MODEM_FRAME_IN];

/*
        Queues one sealed datagram for the modem, from the link's send, sent at when. False when the queue is full. The link's
        own datagrams are filled only while the queue has room (link_modem_free, service.c), so they never find it full; a
        keepalive or a handshake answer can, and is lost as a full socket loses one, to be sent again by its own timers.
*/
static bool link_modem_put(p8 address_to bytes, positive length, p64 when)
{
        positive slot;

        if (!length || length > WATERLINK_DATAGRAM || modem_queue_count == MODEM_QUEUE)
                return false;
        slot = (modem_queue_at + modem_queue_count) % MODEM_QUEUE;
        memory_copy(modem_queue[slot].bytes, bytes, length);
        modem_queue[slot].length = length;
        modem_queue[slot].when = when;
        modem_queue_count++;
        return true;
}

/* The retransmission floor over the modem, and the most the link's window may be: the queue's bytes. */
static p64 link_modem_floor(void)
{
        return MODEM_RTO_FLOOR_US;
}

static p64 link_modem_ceiling(void)
{
        return MODEM_WINDOW_CEILING;
}

/*
        The send time of the oldest datagram the modem still holds: the one being written, or the head of the queue. A frame the
        link sent at or after it may be in the queue. ~0 when the modem holds nothing.
*/
static p64 link_modem_queued_from(void)
{
        if (modem_wl_count)
                return modem_wl_when;
        if (modem_queue_count)
                return modem_queue[modem_queue_at].when;
        return ~0ull;
}

/* The datagrams the queue has room for now. The link sends none that do not fit. */
static positive link_modem_free(void)
{
        return MODEM_QUEUE - modem_queue_count;
}

/* Whether the link's datagrams go over the modem now: the transport is the modem and the modem is open. */
static bool link_modem_carrying(void)
{
        return modem_open;
}

/* Drops count samples from the front of the window. */
static void modem_in_drop(positive count)
{
        for (positive k = count; k < modem_in_fill; k++)
                modem_in[k - count] = modem_in[k];
        modem_in_fill -= count;
        modem_in_seen = modem_in_seen > count ? modem_in_seen - count : 0;
}

/* Puts the left channel of record frame bytes (16-bit stereo, little-endian) into the window, the older half dropped when
   the window is full. */
static void modem_capture(const unsigned char *bytes, positive size)
{
        for (positive at = 0; at + 4 <= size; at += 4)
        {
                if (modem_in_fill == MODEM_IN_SAMPLES)
                        modem_in_drop(MODEM_IN_SAMPLES / 2);
                modem_in[modem_in_fill++] = (int16_t)((unsigned)bytes[at] | ((unsigned)bytes[at + 1] << 8));
        }
}

/*
        Takes the frames a connection has waiting, and waits for none: a frame of the capture channel is given to the window when
        capture is set, every other frame (the server's commands, the data of another stream) is read past. False when the
        connection failed.
*/
static bool modem_read_frames(struct ctl *c, unsigned long channel, bool capture)
{
        unsigned char head[20];

        for (int turn = 0; turn < 256; turn++)
        {
                unsigned long size, got;
                long ready = ctl_wait(c->socket.fd, 1, 0);

                if (ready < 0)
                        return false;
                if (!ready)
                        return true;
                if (c->transport.receive(c->transport.context, head, 20, MODEM_IO_MS) < 0)
                        return false;
                size = ctl_be(head);
                got = ctl_be(head + 4);
                if (size > MODEM_FRAME_IN)
                        return false;
                if (size && c->transport.receive(c->transport.context, modem_frame_in, (long)size, MODEM_IO_MS) < 0)
                        return false;
                if (capture && got == channel && size)
                        modem_capture(modem_frame_in, (positive)size);
        }
        return true;
}

/*
        A record stream for the default source, as a program of its own: 16 bits, two channels, at MODEM_RATE, as the playback stream
        is made. Answers true with the stream's channel in *channel.
*/
static bool modem_record_create(struct ctl *c, unsigned long *channel)
{
        struct ctl_w w;
        struct ctl_r r;
        long got;
        static const char name[] = "Waterlink modem";
        static const unsigned long both[2] = {0x10000, 0x10000};

        ctl_begin(c, &w, 5); // CREATE_RECORD_STREAM
        ctl_w_byte(&w, 'a');
        ctl_w_byte(&w, 3);
        ctl_w_byte(&w, 2);
        ctl_w_be32(&w, MODEM_RATE);
        ctl_w_byte(&w, 'm');
        ctl_w_byte(&w, 2);
        ctl_w_byte(&w, 1);
        ctl_w_byte(&w, 2);
        ctl_w_u32(&w, CTL_NONE); // the source's index: the default source
        ctl_w_string(&w, 0);     // the source's name: none
        ctl_w_u32(&w, CTL_NONE); // the buffer's length: the server's
        ctl_w_bool(&w, 0);       // not corked
        ctl_w_u32(&w, CTL_NONE); // the fragment's size: the server's
        for (int i = 0; i < 5; i++)
                ctl_w_bool(&w, 0); // remap, remix, format, rate and channels are the server's to keep
        ctl_w_bool(&w, 0);         // may move
        ctl_w_bool(&w, 0);         // variable rate
        ctl_w_bool(&w, 0);         // peak detection
        ctl_w_bool(&w, 0);         // latency adjustment
        ctl_w_byte(&w, 'P');
        ctl_w_string(&w, "media.name");
        ctl_w_u32(&w, (unsigned long)ctl_length(name));
        ctl_w_byte(&w, 'x');
        ctl_w_be32(&w, (unsigned long)ctl_length(name));
        ctl_w_raw(&w, name, ctl_length(name));
        ctl_w_byte(&w, 'N');
        ctl_w_u32(&w, CTL_NONE); // direct: no meter of another's
        ctl_w_bool(&w, 0);       // early requests
        ctl_w_bool(&w, 0);       // may inhibit auto suspend
        ctl_w_bool(&w, 0);       // fails on suspend
        ctl_w_byte(&w, 'B');     // formats: none
        ctl_w_byte(&w, 0);
        ctl_w_cvolume(&w, both, 2);
        for (int i = 0; i < 5; i++)
                ctl_w_bool(&w, 0); // muted, volume set, mute set, relative, passthrough: all off
        if (w.bad)
                return false;
        ctl_budget(c);
        got = ctl_exchange(c, w.len);
        if (got < 15)
                return false;
        r = (struct ctl_r){c->frame + 20 + 10, got - 10, 0, 0};
        *channel = ctl_r_u32(&r);
        return true;
}

/*
        Forgets the modem: both connections are let go (the sound server deletes their streams with them) and the queue is
        dropped. The link sends over the network again from the next datagram.
*/
static void link_modem_stop(void)
{
        ctl_disconnect(&modem_play);
        ctl_disconnect(&modem_record);
        crypto_forget(modem_key, sizeof modem_key);
        crypto_forget(&modem_wl_tx.mac, sizeof modem_wl_tx.mac);
        crypto_forget(&modem_wl_rx.rx.mac, sizeof modem_wl_rx.rx.mac);
        modem_open = false;
        modem_queue_count = 0;
        modem_wl_count = 0;
        modem_wl_at = 0;
        modem_in_fill = 0;
        modem_in_seen = 0;
}

/*
        Opens the modem: its key, its two counts, a playback stream and a record stream on the sound server. False when any of it
        is not there, and then nothing is open. The card must run at MODEM_RATE, as the send's does.
*/
static bool link_modem_start(p64 now)
{
        struct audio_state page;
        uint32_t seq = 0, rx_seq = 0;
        uint16_t id = 0, rx_id = 0;

        if (link_secret_at(LINK_MODEM_PATH, (p8 address_to)modem_key, false) < 0)
        {
                if (!modem_warned)
                        host_say(log, host_label "the audio modem has no key: moonwater link modem key makes one, and the far side needs the same\n");
                modem_warned = true;
                return false;
        }
        if (modem_seq_load(LINK_MODEM_SEQ_PATH, modem_key, &seq, &id) < 0 ||
            modem_seq_load(LINK_MODEM_RX_PATH, modem_key, &rx_seq, &rx_id) < 0)
        {
                if (!modem_warned)
                        host_say(log, host_label "the audio modem's counts cannot be read: %s or %s is not a private record, and the link stays on the network\n",
                                 LINK_MODEM_SEQ_PATH, LINK_MODEM_RX_PATH);
                modem_warned = true;
                link_modem_stop();
                return false;
        }
        modem_play.timeout_ms = MODEM_SEND_TIMEOUT_MS;
        modem_record.timeout_ms = MODEM_SEND_TIMEOUT_MS;
        if (ctl_connect(&modem_play, AUDIO_SOCKET) < 0 || !audio_stream_open(&modem_play, "Waterlink modem", "modem", MODEM_RATE, &modem_play_channel))
        {
                link_modem_stop();
                return false;
        }
        //      The playback stream asked the card for MODEM_RATE when the output was idle. A card that runs at another rate
        //      would play every symbol out of time, so the link stays on the network.
        if (audio_test_page(&page) && page.rate != MODEM_RATE)
        {
                if (!modem_warned)
                        host_say(log, host_label "the sound card runs at %p Hz, and the modem's symbols are timed for %p Hz: the link stays on the network\n",
                                 (positive)page.rate, (positive)MODEM_RATE);
                modem_warned = true;
                link_modem_stop();
                return false;
        }
        if (ctl_connect(&modem_record, AUDIO_SOCKET) < 0 || !modem_record_create(&modem_record, &modem_record_channel))
        {
                if (!modem_warned)
                        host_say(log, host_label "the audio modem cannot listen: the sound server has no record stream for it (moonwater audio says which card is open)\n");
                modem_warned = true;
                link_modem_stop();
                return false;
        }
        modem_warned = false;
        modem_tx_open(&modem_wl_tx, modem_key, sizeof modem_key);
        modem_wl_tx.seq = seq;
        modem_wl_tx.id = id;
        modem_chunk_open(&modem_wl_rx, modem_key, sizeof modem_key);
        modem_wl_rx.rx.consume = 1;
        modem_wl_rx.rx.last = rx_seq;
        modem_queue_at = 0;
        modem_queue_count = 0;
        modem_wl_count = 0;
        modem_wl_at = 0;
        modem_in_fill = 0;
        modem_in_seen = 0;
        modem_busy_until = now;
        modem_open = true;
        host_say(log, host_label "the Waterlink datagrams go over the audio modem\n");
        return true;
}

/*
        Plays the queue at the modem's rate: a datagram is coded when the one before it is played, its count is written before its
        sound, and it is written in pieces while the sound server has room for half a second ahead: a poll every MODEM_IN_POLL_US
        keeps the lead near MODEM_LEAD_US. Answers false when the playback connection failed, and sets *next to the next poll.
*/
static bool link_modem_play(p64 now, p64 *next)
{
        *next = now + MODEM_IN_POLL_US;
        if (modem_busy_until < now)
                modem_busy_until = now;
        for (;;)
        {
                positive room, left;

                if (!modem_wl_count)
                {
                        if (!modem_queue_count)
                                return true;
                        modem_wl_when = modem_queue[modem_queue_at].when;
                        modem_wl_count = modem_tx_datagram(&modem_wl_tx, modem_queue[modem_queue_at].bytes,
                                                           modem_queue[modem_queue_at].length, modem_wl_pcm,
                                                           MODEM_SEND_SAMPLES);
                        modem_queue_at = (modem_queue_at + 1) % MODEM_QUEUE;
                        modem_queue_count--;
                        modem_wl_at = 0;
                        if (!modem_wl_count)
                                continue; /* the modem refused the datagram (its frames do not fit): it is dropped */
                        /* the count is kept before the sound that carries it is played, so a later send cannot reuse it */
                        if (modem_seq_save(LINK_MODEM_SEQ_PATH, modem_key, modem_wl_tx.seq, modem_wl_tx.id) < 0)
                        {
                                modem_wl_count = 0;
                                if (!modem_warned)
                                        host_say(log, host_label "the audio modem's count could not be saved to %s: the datagram is not played\n", LINK_MODEM_SEQ_PATH);
                                modem_warned = true;
                                continue;
                        }
                        modem_warned = false;
                }
                if (modem_busy_until - now >= MODEM_LEAD_US)
                {
                        *next = now + MODEM_IN_POLL_US;
                        return true;
                }
                /* 48 samples in each millisecond of room */
                room = (positive)((p64)(MODEM_LEAD_US - (modem_busy_until - now)) * 48 / 1000);
                left = modem_wl_count - modem_wl_at;
                if (room > left)
                        room = left;
                if (room && modem_play_samples(&modem_play, modem_play_channel, modem_wl_pcm + modem_wl_at, room) < 0)
                        return false;
                modem_wl_at += room;
                modem_busy_until += (p64)room * 1000 / 48;
                if (modem_wl_at < modem_wl_count)
                {
                        *next = now + MODEM_IN_POLL_US;
                        return true;
                }
                modem_wl_count = 0;
                modem_wl_at = 0;
        }
}

/*
        Searches the window for the datagrams in it, once the search is due. Each datagram it completes goes to the link, and what
        it took or refused is dropped from the window, so no frame is searched twice. The count of the far side is kept when it
        has moved. Answers -1 when the receiver is closed.
*/
static int link_modem_hear(p64 now)
{
        uint8_t datagram[WATERLINK_DATAGRAM];
        uint32_t before = modem_wl_rx.rx.last;
        int result = 0;

        for (;;)
        {
                size_t used = 0;
                int got = modem_chunk_take(&modem_wl_rx, modem_in, modem_in_fill, datagram, sizeof datagram, &used);

                modem_in_drop(used < modem_in_fill ? (positive)used : modem_in_fill);
                if (got > 0)
                {
                        link_modem_heard(datagram, (positive)got, now);
                        continue;
                }
                if (got < 0)
                        result = -1;
                break;
        }
        modem_in_seen = modem_in_fill;
        if (modem_wl_rx.rx.last != before)
                (void)modem_seq_save(LINK_MODEM_RX_PATH, modem_key, modem_wl_rx.rx.last, 0);
        return result;
}

/* The modem is off because something it needed failed: it is tried again after MODEM_RETRY_US, and the link is on the
   network until then. */
static p64 link_modem_fail(p64 now, string_address why)
{
        host_say(log, host_label "%s\n", why);
        link_modem_stop();
        modem_retry_at = now + MODEM_RETRY_US;
        return now + MODEM_CHECK_US;
}

/*
        The modem's turn, once a loop: the transport is read every second (the modem opens when the transport is the modem and the
        sound card is ready, and closes when either stops); then the queue plays, the capture is read and, once a step of it is
        new, searched. Answers the time the modem next needs a turn by.
*/
static p64 link_modem_turn(p64 now)
{
        p64 next = now + MODEM_CHECK_US, play = now + MODEM_IN_POLL_US;

        if (now - modem_checked >= MODEM_CHECK_US)
        {
                modem_checked = now;
                if (!link_modem_in_use())
                        link_modem_stop();
                else if (!modem_open && now >= modem_retry_at && !link_modem_start(now))
                        modem_retry_at = now + MODEM_RETRY_US;
        }
        if (!modem_open)
                return next;
        if (!link_modem_play(now, &play) || !modem_read_frames(&modem_play, CTL_NONE, false) ||
            !modem_read_frames(&modem_record, modem_record_channel, true))
                return link_modem_fail(now, "the sound server's streams for the audio modem broke: the modem is off for now");
        if (modem_in_fill - modem_in_seen >= MODEM_IN_STEP && link_modem_hear(now) < 0)
                return link_modem_fail(now, "the audio modem's receiver is closed after too many refused frames: the modem is off for now");
        return play < now + MODEM_IN_POLL_US ? play : now + MODEM_IN_POLL_US;
}

#endif /* MODEM_NO_SEND */
