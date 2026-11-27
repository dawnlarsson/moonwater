/*
        Moonwater Feed (MWFD v1) — signed binary feed verifier and zero-allocation
        decoder for https://db.mw.dawning.dev/mw/v1/*.mwf.

        Wire format (little-endian):
          Header (48 bytes):
            [0..3]   magic       "MWFD" (0x4446574d)
            [4]      version     1
            [5]      kind        1=manifest, 2=wallpapers, 3=system.version, 4=keys
            [6..7]   flags       0 (unknown bits rejected)
            [8..15]  sequence    u64 monotonic counter (>= 1)
            [16..23] issued_at   u64 UTC unix seconds
            [24..31] expires_at  u64 UTC unix seconds (> issued_at)
            [32..39] key_fp      first 8 bytes of SHA-256(sec1_pubkey_65)
            [40..43] count       u32 record count
            [44..47] body_len    u32 body byte length
          Body (body_len bytes):
            Packed little-endian fields + u16-length-prefixed UTF-8 strings (no NULs)
          Trailer (64 bytes):
            [len-64..len-33] r   32-byte ECDSA P-256 r
            [len-32..len-1]  s   32-byte ECDSA P-256 low-S s (1 <= s <= n/2)

        Signature covers bytes[0 .. len - 64) directly via SHA-256 + ECDSA P-256
        (reusing crypto_sha256_of and crypto_ecdsa_p256 from src/net/net.c).

        Include after src/net/net.c.
*/
#ifndef STANDARD_MODERN_C_NET_FEED
#define STANDARD_MODERN_C_NET_FEED

#define MWFD_BASE_URL "https://db.mw.dawning.dev/mw/v1"
#define MWFD_URL_MANIFEST MWFD_BASE_URL "/manifest.mwf"
#define MWFD_URL_WALLPAPERS MWFD_BASE_URL "/wallpapers.mwf"
#define MWFD_URL_SYSTEM_VERSION MWFD_BASE_URL "/system/version.mwf"
#define MWFD_URL_KEYS MWFD_BASE_URL "/keys.mwf"

#define MWFD_VERSION 1u
#define MWFD_HEADER_LEN 48u
#define MWFD_SIG_LEN 64u
#define MWFD_MAX_BYTES (256u * 1024u)
#define MWFD_CLOCK_SKEW_MAX 300u

#define MWFD_KIND_MANIFEST 1u
#define MWFD_KIND_WALLPAPERS 2u
#define MWFD_KIND_SYSTEM_VERSION 3u
#define MWFD_KIND_KEYS 4u

#define MWFD_ALG_ECDSA_P256_SHA256 1u
#define MWFD_TAGS_MAX 12u

/*
        Pinned 65-byte uncompressed SEC1 public key (0x04 || X || Y) generated
        inside the Cloudflare FeedSigner Durable Object: key id
        dawning-p256-2ff843ad9188, fingerprint (the first 8 bytes of its SHA-256)
        2ff843ad9188f48d, as `npm run pin-pubkey` in dawning-feed printed it from
        keys.mwf after the first cron publish (2026-11-24 18:00:33 UTC, sequence 1),
        and keys.mwf, wallpapers.mwf and manifest.mwf each verified against it then.
        A key turned over is a new pin here: an all-zero one fails closed in
        mwfd_verify (pubkey[0] != 0x04).
*/
static const p8 mwfd_pinned_pubkey[65] = {
        0x04, 0x0b, 0x00, 0x6d, 0x92, 0xa4, 0xf0, 0x1a, 0x9c, 0xfe, 0x5e, 0x54, 0x58,
        0x10, 0x14, 0x3c, 0x93, 0x05, 0x43, 0x3c, 0xc1, 0xe9, 0x88, 0x2e, 0x50, 0xbb,
        0x37, 0xc7, 0x33, 0x5f, 0xf3, 0x70, 0x1c, 0x6d, 0x89, 0x42, 0x4e, 0x3c, 0xf4,
        0xec, 0x19, 0xa6, 0x2a, 0x9a, 0x7f, 0x79, 0x76, 0x4f, 0x5b, 0x74, 0xba, 0x09,
        0xbe, 0x83, 0x9e, 0xe5, 0xc7, 0x29, 0xfb, 0x7f, 0xc2, 0xe0, 0x96, 0xc5, 0xf6,
};

/* NIST P-256 half-order floor(n / 2), big-endian, for canonical low-S checks. */
static const p8 mwfd_p256_half_order_be[32] = {
        0x7f, 0xff, 0xff, 0xff, 0x80, 0x00, 0x00, 0x00,
        0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xde, 0x73, 0x7d, 0x56, 0xd3, 0x8b, 0xcf, 0x42,
        0x79, 0xdc, 0xe5, 0x61, 0x7e, 0x31, 0x92, 0xa8,
};

typedef struct mwfd_slice
{
        const p8 address_to bytes;
        p16 length;
} mwfd_slice;

typedef struct mwfd_header
{
        p8 version;
        p8 kind;
        p16 flags;
        p64 sequence;
        p64 issued_at;
        p64 expires_at;
        p8 key_fp[8];
        p32 count;
        p32 body_len;
} mwfd_header;

typedef struct mwfd_manifest_entry
{
        p8 kind_id;
        p64 sequence;
        p32 bytes;
        p8 sha256[32];
        mwfd_slice route;
} mwfd_manifest_entry;

typedef struct mwfd_wallpaper
{
        p8 tone; // 0 = dark, 1 = light
        p8 rgb[3];
        p16 width;
        p16 height;
        p32 bytes;
        p32 thumb_bytes;
        p8 sha256[32];
        p8 thumb_sha256[32];
        mwfd_slice id;
        mwfd_slice title;
        mwfd_slice author;
        mwfd_slice license;
        mwfd_slice url;
        mwfd_slice thumb_url;
        p8 tag_count;
        mwfd_slice tags[MWFD_TAGS_MAX];
} mwfd_wallpaper;

typedef struct mwfd_system_version
{
        p32 advisories_count;
        mwfd_slice channel;
        mwfd_slice version;
        mwfd_slice min_supported_version;
} mwfd_system_version;

typedef struct mwfd_key
{
        p8 algorithm;
        p8 flags; // bit 0 = active
        p8 key_fp[8];
        p8 sec1[65];
        mwfd_slice key_id;
} mwfd_key;

typedef struct mwfd_reader
{
        const p8 address_to bytes;
        positive length;
        positive at;
        bool bad;
} mwfd_reader;

static p8 mwfd_r_u8(mwfd_reader address_to r)
{
        if (r->bad || r->length - r->at < 1)
        {
                r->bad = true;
                return 0;
        }
        return r->bytes[r->at++];
}

static p16 mwfd_r_u16(mwfd_reader address_to r)
{
        if (r->bad || r->length - r->at < 2)
        {
                r->bad = true;
                return 0;
        }
        const p8 address_to p = r->bytes + r->at;
        r->at += 2;
        return (p16)((p16)p[0] | ((p16)p[1] << 8));
}

static p32 mwfd_r_u32(mwfd_reader address_to r)
{
        if (r->bad || r->length - r->at < 4)
        {
                r->bad = true;
                return 0;
        }
        const p8 address_to p = r->bytes + r->at;
        r->at += 4;
        return (p32)p[0] | ((p32)p[1] << 8) | ((p32)p[2] << 16) | ((p32)p[3] << 24);
}

static p64 mwfd_r_u64(mwfd_reader address_to r)
{
        p32 lo = mwfd_r_u32(r);
        p32 hi = mwfd_r_u32(r);
        return (p64)lo | ((p64)hi << 32);
}

static fn mwfd_r_bytes(mwfd_reader address_to r, p8 address_to out, positive count)
{
        if (r->bad || r->length - r->at < count)
        {
                r->bad = true;
                return;
        }
        memory_copy(out, (p8 address_to)(r->bytes + r->at), count);
        r->at += count;
}

static mwfd_slice mwfd_r_str16(mwfd_reader address_to r)
{
        mwfd_slice out = {null, 0};
        p16 len = mwfd_r_u16(r);
        if (r->bad || r->length - r->at < (positive)len)
        {
                r->bad = true;
                return out;
        }
        const p8 address_to start = r->bytes + r->at;
        for (positive i = 0; i < (positive)len; i++)
        {
                if (start[i] == 0)
                {
                        r->bad = true;
                        return out;
                }
        }
        r->at += (positive)len;
        out.bytes = start;
        out.length = len;
        return out;
}

/* Constant-time check that big-endian 32-byte scalar s satisfies 1 <= s <= half_order. */
static bool mwfd_is_low_s(const p8 address_to s)
{
        p32 non_zero = 0, less = 0, greater = 0;
        for (positive i = 0; i < 32; i++)
        {
                p32 a = s[i], b = mwfd_p256_half_order_be[i];
                p32 undecided = (p32)((less | greater) == 0);
                non_zero |= a;
                less |= undecided & (p32)(a < b);
                greater |= undecided & (p32)(a > b);
        }
        return non_zero != 0 && greater == 0;
}

/*
        Parse and cryptographically verify an MWFD v1 binary feed.
        - pubkey: 65-byte uncompressed SEC1 point (0x04 || X || Y)
        - expected_kind: 0 to accept any kind, or MWFD_KIND_*
        - min_sequence: minimum accepted sequence (anti-rollback; 0 for first fetch)
        - now_seconds: current UTC unix seconds (0 disables clock check for offline tests)
        Returns true iff header, bounds, fingerprint, low-S, and ECDSA P-256 signature are all valid.
*/
static bool mwfd_verify(const p8 address_to raw, positive length,
                        const p8 address_to pubkey, p8 expected_kind,
                        p64 min_sequence, p64 now_seconds,
                        mwfd_header address_to header_out)
{
        if (!raw || !pubkey || !header_out)
                return false;
        if (pubkey[0] != 0x04)
                return false;
        if (length < MWFD_HEADER_LEN + MWFD_SIG_LEN || length > MWFD_MAX_BYTES)
                return false;
        if (raw[0] != 'M' || raw[1] != 'W' || raw[2] != 'F' || raw[3] != 'D')
                return false;

        mwfd_reader r = {raw + 4, MWFD_HEADER_LEN - 4, 0, false};
        mwfd_header h;
        memory_fill(address_of h, 0, sizeof h);
        h.version = mwfd_r_u8(address_of r);
        h.kind = mwfd_r_u8(address_of r);
        h.flags = mwfd_r_u16(address_of r);
        h.sequence = mwfd_r_u64(address_of r);
        h.issued_at = mwfd_r_u64(address_of r);
        h.expires_at = mwfd_r_u64(address_of r);
        mwfd_r_bytes(address_of r, h.key_fp, 8);
        h.count = mwfd_r_u32(address_of r);
        h.body_len = mwfd_r_u32(address_of r);
        if (r.bad || r.at != r.length)
                return false;

        if (h.version != MWFD_VERSION || h.flags != 0)
                return false;
        if (expected_kind != 0 && h.kind != expected_kind)
                return false;
        if (h.sequence < 1 || h.sequence < min_sequence)
                return false;
        if (h.expires_at <= h.issued_at)
                return false;
        if (now_seconds != 0)
        {
                if (h.issued_at > now_seconds + MWFD_CLOCK_SKEW_MAX)
                        return false;
                if (now_seconds >= h.expires_at)
                        return false;
        }
        if ((positive)MWFD_HEADER_LEN + (positive)h.body_len + (positive)MWFD_SIG_LEN != length)
                return false;

        p8 pub_hash[32];
        crypto_sha256_of((p8 address_to)pubkey, 65, pub_hash);
        if (!crypto_same(pub_hash, h.key_fp, 8))
                return false;

        const p8 address_to sig = raw + length - MWFD_SIG_LEN;
        if (!mwfd_is_low_s(sig + 32))
                return false;

        p8 digest[32];
        crypto_sha256_of((p8 address_to)raw, length - MWFD_SIG_LEN, digest);
        if (!crypto_ecdsa_p256(digest, 32,
                               (p8 address_to)sig, 32,
                               (p8 address_to)(sig + 32), 32,
                               (p8 address_to)(pubkey + 1),
                               (p8 address_to)(pubkey + 33)))
                return false;

        *header_out = h;
        return true;
}

static bool mwfd_decode_manifest(const p8 address_to body, positive body_len,
                                 p32 count, mwfd_manifest_entry address_to out,
                                 positive cap)
{
        if ((positive)count > cap)
                return false;
        mwfd_reader r = {body, body_len, 0, false};
        for (p32 i = 0; i < count; i++)
        {
                out[i].kind_id = mwfd_r_u8(address_of r);
                out[i].sequence = mwfd_r_u64(address_of r);
                out[i].bytes = mwfd_r_u32(address_of r);
                mwfd_r_bytes(address_of r, out[i].sha256, 32);
                out[i].route = mwfd_r_str16(address_of r);
                if (r.bad)
                        return false;
        }
        return !r.bad && r.at == r.length;
}

static bool mwfd_decode_wallpapers(const p8 address_to body, positive body_len,
                                   p32 count, mwfd_wallpaper address_to out,
                                   positive cap)
{
        if ((positive)count > cap)
                return false;
        mwfd_reader r = {body, body_len, 0, false};
        for (p32 i = 0; i < count; i++)
        {
                mwfd_wallpaper address_to w = out + i;
                memory_fill(w, 0, sizeof *w);
                w->tone = mwfd_r_u8(address_of r);
                if (w->tone > 1)
                        return false;
                mwfd_r_bytes(address_of r, w->rgb, 3);
                w->width = mwfd_r_u16(address_of r);
                w->height = mwfd_r_u16(address_of r);
                w->bytes = mwfd_r_u32(address_of r);
                w->thumb_bytes = mwfd_r_u32(address_of r);
                mwfd_r_bytes(address_of r, w->sha256, 32);
                mwfd_r_bytes(address_of r, w->thumb_sha256, 32);
                w->id = mwfd_r_str16(address_of r);
                w->title = mwfd_r_str16(address_of r);
                w->author = mwfd_r_str16(address_of r);
                w->license = mwfd_r_str16(address_of r);
                w->url = mwfd_r_str16(address_of r);
                w->thumb_url = mwfd_r_str16(address_of r);
                w->tag_count = mwfd_r_u8(address_of r);
                if (r.bad || w->tag_count > MWFD_TAGS_MAX)
                        return false;
                for (p8 t = 0; t < w->tag_count; t++)
                {
                        w->tags[t] = mwfd_r_str16(address_of r);
                        if (r.bad)
                                return false;
                }
        }
        return !r.bad && r.at == r.length;
}

static bool mwfd_decode_system_version(const p8 address_to body, positive body_len,
                                       p32 count, mwfd_system_version address_to out)
{
        if (count != 1 || !out)
                return false;
        mwfd_reader r = {body, body_len, 0, false};
        out->advisories_count = mwfd_r_u32(address_of r);
        out->channel = mwfd_r_str16(address_of r);
        out->version = mwfd_r_str16(address_of r);
        out->min_supported_version = mwfd_r_str16(address_of r);
        return !r.bad && r.at == r.length;
}

static bool mwfd_decode_keys(const p8 address_to body, positive body_len,
                             p32 count, mwfd_key address_to out, positive cap)
{
        if ((positive)count > cap)
                return false;
        mwfd_reader r = {body, body_len, 0, false};
        for (p32 i = 0; i < count; i++)
        {
                out[i].algorithm = mwfd_r_u8(address_of r);
                out[i].flags = mwfd_r_u8(address_of r);
                mwfd_r_bytes(address_of r, out[i].key_fp, 8);
                mwfd_r_bytes(address_of r, out[i].sec1, 65);
                out[i].key_id = mwfd_r_str16(address_of r);
                if (r.bad || out[i].algorithm != MWFD_ALG_ECDSA_P256_SHA256 ||
                    (out[i].flags & ~1u) != 0 || out[i].sec1[0] != 0x04)
                        return false;
        }
        return !r.bad && r.at == r.length;
}

/* Verify downloaded asset bytes against the signed feed's exact byte count and SHA-256. */
static bool mwfd_verify_asset(const p8 address_to data, positive length,
                              p32 expected_bytes, const p8 address_to expected_sha256)
{
        if (!data || !expected_sha256 || length != (positive)expected_bytes)
                return false;
        p8 digest[32];
        crypto_sha256_of((p8 address_to)data, length, digest);
        return crypto_same(digest, (p8 address_to)expected_sha256, 32);
}

/*
        A feed fetched and verified, and an asset it names fetched and checked: net.c's HTTPS client in its confined child (the certificate
        checked, redirects followed, never down to plain HTTP, the body held in memory to the most the caller allows), and nothing of the
        bytes read before mwfd_verify has said they are the pinned key's, of the kind asked, at the least sequence asked (the highest seen,
        so an older feed is none), and not expired now. A URL that is not https:// is refused. Zero and the bytes in *body (byte_store_release
        them); negative, and nothing, else. The caller keeps what it trusts (the sequence, the bytes for a later start with no network): this
        is the network's half only, for every feed of MWFD_BASE_URL.
*/
#ifdef STANDARD_MODERN_C_NET_APART
#define MWFD_FETCH_BAD_STATUS (-71) // EPROTO: the server answered but not 200
#define MWFD_FETCH_UNVERIFIED (-74) // EBADMSG: the bytes are not the pinned key's feed, or not this one, or old, or expired

/*
        Who asks, as the hosts the feed links to ask a program to say (Wikimedia's User-Agent policy: the program, and how to reach
        its makers), so a machine fetching a curated wallpaper is never taken for an anonymous scraper and throttled or refused.
*/
#define MWFD_AGENT "Moonwater/1 (+https://dawning.dev; wallpapers and updates)"
// A host that says it is busy (429 Too Many Requests, 503) is asked again after a pause that grows, and given up after the last.
#define MWFD_FETCH_TRIES 3
#define MWFD_FETCH_PAUSE_S 3

static bipolar mwfd_fetch_bytes(string_address url, positive most, http_buffer address_to body)
{
        http_manners how = http_manners_wget;
        b32 code = 0;
        bipolar status = MWFD_FETCH_BAD_STATUS;

        memory_fill(body, 0, sizeof(*body));
        if (string_length(url) < 9 || memory_compare(url, "https://", 8))
                return MWFD_FETCH_UNVERIFIED;
        how.agent = (string_address)MWFD_AGENT, how.body_most = most;
        for (positive attempt = 0; attempt < MWFD_FETCH_TRIES; attempt++)
        {
                if (attempt)
                        system_call_2(syscall(nanosleep), (positive)(positive[2]){MWFD_FETCH_PAUSE_S * attempt * attempt, 0}, 0);
                code = 0;
                status = http_run_apart(url, address_of how, true, -1, body, address_of code, null, null);
                if (!status && code != 200)
                        status = MWFD_FETCH_BAD_STATUS;
                if (status)
                        byte_store_release(body), memory_fill(body, 0, sizeof(*body));
                if (!status || (code != 429 && code != 503))
                        break;
        }
        return status;
}

static bipolar mwfd_fetch(string_address url, p8 kind, p64 min_sequence, p64 now_seconds, http_buffer address_to body,
                          mwfd_header address_to header)
{
        bipolar status = mwfd_fetch_bytes(url, MWFD_MAX_BYTES, body);

        if (!status && !mwfd_verify(body->bytes, body->used, mwfd_pinned_pubkey, kind, min_sequence, now_seconds, header))
                byte_store_release(body), status = MWFD_FETCH_UNVERIFIED;
        return status;
}

// An asset of a verified feed (a wallpaper, its thumbnail), held to the feed's byte count for it and to its SHA-256.
static bipolar mwfd_fetch_asset(string_address url, p32 bytes, const p8 address_to sha256, http_buffer address_to body)
{
        bipolar status = mwfd_fetch_bytes(url, (positive)bytes, body);

        if (!status && !mwfd_verify_asset(body->bytes, body->used, bytes, sha256))
                byte_store_release(body), status = MWFD_FETCH_UNVERIFIED;
        return status;
}
#endif

#endif // STANDARD_MODERN_C_NET_FEED
