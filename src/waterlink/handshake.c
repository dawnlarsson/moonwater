/*
        Waterlink's handshake: Noise IK over net.c's X25519, SHA-256, HKDF and
        AES-GCM, with WireGuard's cheap first gate in front of it.

        IK because the one who starts already knows who it is talking to --
        a peer is paired by key before anything is sent -- and IK is the
        pattern where that knowledge buys a round trip: the initiator's
        identity rides in the first message, encrypted to the responder, and
        the session keys are ready after the second.

        THE CIPHER IS AES-128, AND THE NAME SAYS SO

        Noise's AESGCM is AES-256. lib.c carries AES-128 in assembly on all
        three machines and nothing wider, and the datagram's seal is AES-128
        for that reason (waterlink.c). So the cipher function here is
        AES-128-GCM keyed by the first sixteen bytes of Noise's 32 byte key,
        with Noise's own nonce -- four zero bytes and the counter big endian --
        and everything else is Noise to the letter. The protocol name is
        therefore Noise_IK_25519_AES128GCM_SHA256 and not the standard's: a
        name is hashed into every key, so this cannot be mistaken for, or
        made to talk to, an implementation of the AES-256 one. At 31 bytes
        the name is its own initial hash, zero padded.

        Transport keys are the two halves of Split, cut to sixteen bytes the
        same way; from there the datagram counter is the nonce, little endian,
        which is seal.c's layout and not this file's.

        THE FIRST GATE

        Before any Diffie-Hellman, a datagram must carry mac1: sixteen bytes of
        HMAC-SHA256 over everything before it, keyed by the hash of
        "mac1----" and the receiver's public key. Anyone who knows the key may
        make one, so it proves nothing about who sent it -- what it does is
        make a scanner that does not know this machine's key cost two hashes
        and not a curve multiplication. Behind it, each source address gets a
        small bucket of initiations (waterlink_admit), and an initiation's
        timestamp must be newer than the last one accepted from the same peer,
        so a recorded initiation cannot be played back to tear a session down.

        THE SECOND GATE

        mac1 stops only a sender who does not know the key. One who does can
        spend the whole admission bucket from addresses it makes up, and the
        machine's paired peers find it spent. So, as WireGuard does
        (whitepaper 5.4.7), a listener under load -- more initiations passing
        mac1 than WATERLINK_ADMIT_LOADED a second -- asks for a second MAC,
        mac2, sixteen bytes after mac1 in what was padding, keyed by a cookie
        only the holder of the initiation's source address and port can
        have: a MAC of that address by a secret made again every two
        minutes. An initiation without a good mac2 then costs this machine
        three hashes and no curve, and is answered with a cookie reply, 72
        bytes where it sent 1200, which the initiator opens and asks again
        under. A sender that made its address up never sees the reply.
        Unloaded, mac2 is not asked for, and an initiation from a build that
        writes zeros there still passes.

        WireGuard seals the cookie with XChaCha20-Poly1305 under a hash of
        the responder's key, with a random 24 byte nonce and mac1 as the
        associated data. lib.c carries AES-128-GCM and no ChaCha, and a
        random 12 byte GCM nonce under one key for the machine's life is too
        few bits to leave to chance, so the construction is XChaCha's own
        shape over what lib.c has: the 24 byte nonce and the hash make a key
        for this one reply (HMAC-SHA256, cut to sixteen bytes), and that key
        seals under a zero nonce once. The MACs are HMAC-SHA256 cut to
        sixteen bytes, as mac1 already is.

        Like link.c this is a transform: the caller brings the randomness, the
        clock and the keys, and gets bytes.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/

#ifndef WATERLINK_HANDSHAKE_INCLUDED
#define WATERLINK_HANDSHAKE_INCLUDED

#include "waterlink.c"

#define WATERLINK_PROTOCOL "Noise_IK_25519_AES128GCM_SHA256"
#define WATERLINK_PROLOGUE "waterlink 3"

/*
        A group's handshake is the same IK to the group's own key, which every
        member derives from the secret, with a pre-shared key from the secret
        mixed in after the first message's tokens -- Noise's psk1 modifier --
        so a first message that opens proves its sender holds the secret, as
        well as the static key it sent. It is never answered: see nearby.c.
*/
#define WATERLINK_GROUP_PROTOCOL "Noise_IKpsk1_25519_AES128GCM_SHA256"

/*
        What the first message carries, sealed: when it was made (TAI64N, so
        a replay is refused), then either which conversation it opens or keys
        again and the index the initiator wants the far side to write on what
        it sends, or, to a group, the name the member offers.
*/
#define WATERLINK_STAMP_BYTES 12
#define WATERLINK_HELLO_BYTES (WATERLINK_STAMP_BYTES + 32)

// ephemeral, sealed static, sealed hello, mac1; then mac2
#define WATERLINK_INITIATE_BYTES (32 + 48 + WATERLINK_HELLO_BYTES + 16 + 16)
// ephemeral, sealed index, mac1
#define WATERLINK_RESPOND_BYTES (32 + 4 + 16 + 16)
// the nonce, the sealed cookie
#define WATERLINK_COOKIE_BYTES (24 + 16 + 16)
#define WATERLINK_COOKIE_DATAGRAM (16 + WATERLINK_COOKIE_BYTES)

struct waterlink_identity {
        p8 secret[WATERLINK_KEY_BYTES];
        p8 public[WATERLINK_KEY_BYTES];
        p8 gate[32];   // mac1's key for datagrams sent to this identity
        p8 sealer[32]; // what seals the cookies this identity hands out
        crypto_hmac_key gate_ready; // the gate, as every datagram is checked
};

struct waterlink_noise {
        p8 hash[32];
        p8 chain[32];
        p8 key[32];
        p64 nonce;
        p8 ephemeral[32];      // ours, secret
        p8 ephemeral_public[32];
        p8 remote_ephemeral[32];
        p8 remote_static[32];
};

static fn waterlink_mix_hash(struct waterlink_noise address_to noise,
                             p8 address_to data, positive length)
{
        crypto_sha256 hash;

        crypto_sha256_open(address_of hash);
        crypto_sha256_write(address_of hash, noise->hash, 32);
        crypto_sha256_write(address_of hash, data, length);
        crypto_sha256_close(address_of hash, noise->hash);
}

/*
        HKDF with the chaining key as salt and no info: MixKey's two outputs,
        or with hash MixKeyAndHash's three, the middle one into the hash.
*/
static fn waterlink_mix_key(struct waterlink_noise address_to noise,
                            p8 address_to material, positive length, bool hash)
{
        p8 prk[32];
        p8 out[96];

        crypto_hkdf_extract(noise->chain, 32, material, length, prk);
        crypto_hkdf_expand(prk, (p8 address_to) "", 0, out, hash ? 96 : 64);
        memory_copy(noise->chain, out, 32);
        if (hash)
                waterlink_mix_hash(noise, out + 32, 32);
        memory_copy(noise->key, out + (hash ? 64 : 32), 32);
        noise->nonce = 0;
        crypto_forget(prk, sizeof prk);
        crypto_forget(out, sizeof out);
}

static bool waterlink_mix_dh(struct waterlink_noise address_to noise,
                             p8 address_to secret, p8 address_to public)
{
        p8 shared[32];
        bool good = crypto_x25519(shared, secret, public);

        if (good)
                waterlink_mix_key(noise, shared, 32, false);
        crypto_forget(shared, sizeof shared);
        return good;
}

static fn waterlink_noise_nonce(p8 address_to iv, p64 nonce)
{
        memory_zero(iv, 4);
        network_store_64(iv + 4, nonce);
}

// EncryptAndHash: the text is sealed in place and its tag written after it.
static fn waterlink_seal_hash(struct waterlink_noise address_to noise,
                              p8 address_to text, positive length)
{
        crypto_aesgcm_key key;
        p8 iv[12];

        crypto_aesgcm_prepare(address_of key, noise->key);
        waterlink_noise_nonce(iv, noise->nonce++);
        crypto_aesgcm_seal(address_of key, iv, noise->hash, 32, text, length,
                           text + length);
        waterlink_mix_hash(noise, text, length + 16);
        crypto_forget(address_of key, sizeof key);
}

static bool waterlink_open_hash(struct waterlink_noise address_to noise,
                                p8 address_to text, positive length,
                                p8 address_to plain)
{
        crypto_aesgcm_key key;
        p8 iv[12];
        p8 sealed[64];
        bool good;

        /* Subtract before comparing so a hostile or corrupted span cannot
           wrap the tag addition and reach the copy as an enormous length. */
        if (length > sizeof sealed - 16)
                return false;

        memory_copy(sealed, text, length + 16);
        memory_copy(plain, text, length);
        crypto_aesgcm_prepare(address_of key, noise->key);
        waterlink_noise_nonce(iv, noise->nonce++);
        good = crypto_aesgcm_open(address_of key, iv, noise->hash, 32, plain,
                                  length, text + length);
        if (good)
                waterlink_mix_hash(noise, sealed, length + 16);
        crypto_forget(address_of key, sizeof key);
        return good;
}

//      A name longer than a hash is hashed, as Noise says.
static fn waterlink_noise_start(struct waterlink_noise address_to noise,
                                p8 address_to responder_public, bool group)
{
        memory_zero(noise, sizeof(address_to noise));
        if (group)
                crypto_sha256_of((p8 address_to)WATERLINK_GROUP_PROTOCOL,
                                 sizeof(WATERLINK_GROUP_PROTOCOL) - 1,
                                 noise->hash);
        else
                memory_copy(noise->hash, WATERLINK_PROTOCOL,
                            sizeof(WATERLINK_PROTOCOL) - 1);
        memory_copy(noise->chain, noise->hash, 32);
        waterlink_mix_hash(noise, (p8 address_to)WATERLINK_PROLOGUE,
                           sizeof(WATERLINK_PROLOGUE) - 1);
        //      IK's pre-message: the responder's static key.
        waterlink_mix_hash(noise, responder_public, 32);
}

static p8 waterlink_base[32] = {9};

static fn waterlink_label_key(string_address label, p8 address_to public,
                               p8 address_to key);

fn waterlink_identity_from(struct waterlink_identity address_to identity,
                           p8 address_to secret)
{
        memory_copy(identity->secret, secret, 32);
        crypto_x25519(identity->public, identity->secret, waterlink_base);
        waterlink_label_key((string_address) "mac1----", identity->public,
                            identity->gate);
        crypto_hmac_prepare(DIGEST_SHA256, 32, identity->gate, 32,
                            address_of identity->gate_ready);
        waterlink_label_key((string_address) "cookie--", identity->public,
                            identity->sealer);
}

/*      A key of the holder of public's for one use, WireGuard's labels:
        "mac1----" for mac1 on datagrams sent to it, "cookie--" for the
        cookies it hands out. */
static fn waterlink_label_key(string_address label, p8 address_to public,
                              p8 address_to key)
{
        crypto_sha256 hash;

        crypto_sha256_open(address_of hash);
        crypto_sha256_write(address_of hash, (p8 address_to)label, 8);
        crypto_sha256_write(address_of hash, public, 32);
        crypto_sha256_close(address_of hash, key);
}

// mac1's key for a datagram going to the holder of public.
static fn waterlink_gate_of(p8 address_to public, p8 address_to gate)
{
        waterlink_label_key((string_address) "mac1----", public, gate);
}

static fn waterlink_mac1(p8 address_to gate, p8 address_to message,
                         positive length, p8 address_to mac)
{
        p8 full[32];

        crypto_hmac_sha256(gate, 32, message, length, full);
        memory_copy(mac, full, 16);
}

/*
        Whether a handshake datagram carries a good mac1 for this identity,
        and the padding after it -- after an initiation's mac2 -- is zero. The
        cheap question, asked first.
*/
bool waterlink_gate_passes(struct waterlink_identity address_to me,
                           p8 address_to datagram, positive length)
{
        struct waterlink_datagram head;
        positive body;
        positive padding;
        p8 mac[32];

        if (length != WATERLINK_DATAGRAM)
                return false;

        memory_copy(address_of head, datagram, 16);
        if (head.kind == WATERLINK_KIND_INITIATE)
                body = WATERLINK_INITIATE_BYTES;
        else if (head.kind == WATERLINK_KIND_RESPOND)
                body = WATERLINK_RESPOND_BYTES;
        else
                return false;

        padding = 16 + body + (head.kind == WATERLINK_KIND_INITIATE ? 16 : 0);
        if (memory_span_byte(datagram + padding, 0, length - padding) !=
            length - padding)
                return false;

        crypto_hmac_prepared(address_of me->gate_ready, datagram, 16 + body - 16,
                             mac);
        return crypto_same(mac, datagram + 16 + body - 16, 16);
}

// A datagram's head, with the rest of it zeroed to its full size.
static fn waterlink_head(p8 address_to datagram, p32 kind, p32 receiver)
{
        struct waterlink_datagram head = {kind, receiver, 0};

        memory_zero(datagram, WATERLINK_DATAGRAM);
        memory_copy(datagram, address_of head, 16);
}

// The "e" token's half that both patterns share: ours, and its public out.
static fn waterlink_ephemeral(struct waterlink_noise address_to noise,
                              p8 address_to ephemeral, p8 address_to at)
{
        memory_copy(noise->ephemeral, ephemeral, 32);
        crypto_x25519(noise->ephemeral_public, noise->ephemeral,
                      waterlink_base);
        memory_copy(at, noise->ephemeral_public, 32);
}

/*
        Write the first message. ephemeral is 32 random bytes the caller
        drew; hello is WATERLINK_HELLO_BYTES. With a pre-shared key it is a
        group's handshake. The datagram is written whole, padded to its full
        size.
*/
bool waterlink_initiate(struct waterlink_noise address_to noise,
                        struct waterlink_identity address_to me,
                        p8 address_to responder_public, p8 address_to psk,
                        p8 address_to ephemeral, p8 address_to hello,
                        p8 address_to datagram)
{
        p8 address_to at = datagram + 16;
        p8 gate[32];

        waterlink_head(datagram, WATERLINK_KIND_INITIATE, 0);
        waterlink_noise_start(noise, responder_public, psk != null);
        memory_copy(noise->remote_static, responder_public, 32);

        // -> e, and in a psk handshake into the key too
        waterlink_ephemeral(noise, ephemeral, at);
        waterlink_mix_hash(noise, at, 32);
        if (psk)
                waterlink_mix_key(noise, at, 32, false);
        at += 32;

        // es
        if (!waterlink_mix_dh(noise, noise->ephemeral, responder_public))
                return false;

        // s
        memory_copy(at, me->public, 32);
        waterlink_seal_hash(noise, at, 32);
        at += 48;

        // ss, psk
        if (!waterlink_mix_dh(noise, me->secret, responder_public))
                return false;
        if (psk)
                waterlink_mix_key(noise, psk, 32, true);

        // payload
        memory_copy(at, hello, WATERLINK_HELLO_BYTES);
        waterlink_seal_hash(noise, at, WATERLINK_HELLO_BYTES);
        at += WATERLINK_HELLO_BYTES + 16;

        waterlink_gate_of(responder_public, gate);
        waterlink_mac1(gate, datagram, (positive)(at - datagram), at);
        return true;
}

/*
        Read a first message that passed the gate: who sent it, and its
        hello. False when any step does not verify -- a wrong static key on
        either side fails here, at the sealed static or the sealed hello.
*/
bool waterlink_accept(struct waterlink_noise address_to noise,
                      struct waterlink_identity address_to me, p8 address_to psk,
                      p8 address_to datagram, p8 address_to initiator_public,
                      p8 address_to hello)
{
        p8 address_to at = datagram + 16;

        waterlink_noise_start(noise, me->public, psk != null);

        // -> e
        memory_copy(noise->remote_ephemeral, at, 32);
        waterlink_mix_hash(noise, at, 32);
        if (psk)
                waterlink_mix_key(noise, at, 32, false);
        at += 32;

        // es
        if (!waterlink_mix_dh(noise, me->secret, noise->remote_ephemeral))
                return false;

        // s
        if (!waterlink_open_hash(noise, at, 32, noise->remote_static))
                return false;
        at += 48;

        // ss, psk
        if (!waterlink_mix_dh(noise, me->secret, noise->remote_static))
                return false;
        if (psk)
                waterlink_mix_key(noise, psk, 32, true);

        if (!waterlink_open_hash(noise, at, WATERLINK_HELLO_BYTES, hello))
                return false;

        memory_copy(initiator_public, noise->remote_static, 32);
        return true;
}

/*
        Write the answer: the responder's ephemeral and its own index,
        addressed to the initiator's index.
*/
bool waterlink_respond(struct waterlink_noise address_to noise,
                       p8 address_to ephemeral, p32 their_index, p32 our_index,
                       p8 address_to datagram)
{
        p8 address_to at = datagram + 16;
        p8 gate[32];

        waterlink_head(datagram, WATERLINK_KIND_RESPOND, their_index);

        // <- e
        waterlink_ephemeral(noise, ephemeral, at);
        waterlink_mix_hash(noise, at, 32);
        at += 32;

        // ee, se
        if (!waterlink_mix_dh(noise, noise->ephemeral,
                              noise->remote_ephemeral) ||
            !waterlink_mix_dh(noise, noise->ephemeral, noise->remote_static))
                return false;

        memory_copy(at, address_of our_index, 4);
        waterlink_seal_hash(noise, at, 4);
        at += 20;

        waterlink_gate_of(noise->remote_static, gate);
        waterlink_mac1(gate, datagram, (positive)(at - datagram), at);
        return true;
}

// Read the answer to our first message; the responder's index comes back.
bool waterlink_answered(struct waterlink_noise address_to noise,
                        struct waterlink_identity address_to me,
                        p8 address_to datagram, p32 address_to their_index)
{
        p8 address_to at = datagram + 16;

        memory_copy(noise->remote_ephemeral, at, 32);
        waterlink_mix_hash(noise, at, 32);
        at += 32;

        // ee, se
        if (!waterlink_mix_dh(noise, noise->ephemeral,
                              noise->remote_ephemeral))
                return false;
        if (!waterlink_mix_dh(noise, me->secret, noise->remote_ephemeral))
                return false;

        return waterlink_open_hash(noise, at, 4, (p8 address_to)their_index);
}

/*
        Split: MixKey's two outputs of nothing, the initiator sending with
        the first and the responder with the second, both cut to AES-128's
        sixteen bytes. The handshake state is forgotten after.
*/
fn waterlink_split(struct waterlink_noise address_to noise, bool initiator,
                   p8 address_to sending, p8 address_to receiving)
{
        waterlink_mix_key(noise, (p8 address_to) "", 0, false);
        memory_copy(initiator ? sending : receiving, noise->chain,
                    WATERLINK_AEAD_BYTES);
        memory_copy(initiator ? receiving : sending, noise->key,
                    WATERLINK_AEAD_BYTES);
        crypto_forget(noise, sizeof(address_to noise));
}

/*
        TAI64N, as WireGuard stamps its initiations: seconds past 1970 plus
        2^62, then nanoseconds, both big endian, so newer compares greater
        byte by byte.
*/
fn waterlink_stamp(p8 address_to stamp, p64 seconds, p32 nanoseconds)
{
        network_store_64(stamp, seconds + (1ull << 62));
        network_store_32(stamp + 8, nanoseconds);
}

bool waterlink_stamp_newer(p8 address_to stamp, p8 address_to last)
{
        return memory_compare(stamp, last, WATERLINK_STAMP_BYTES) > 0;
}

/*
        Initiations from a cookie-proven source address: a bucket of
        WATERLINK_ADMIT_BURST that refills at WATERLINK_ADMIT_RATE a second,
        in a table of fixed size where a new address takes the stalest entry.
        An unproven address must never spend this bucket: spoofing a paired
        peer's address would otherwise starve that peer below the load
        threshold. A second bucket caps the whole table. A third counts every
        initiation that passed mac1, and when it runs dry the listener is
        under load: an initiation then needs a good mac2 to reach the other
        two at all, so made-up addresses spend neither the table nor the
        cap, and are answered with a cookie instead. It runs dry at half the
        cap, so a sender that stays just under it leaves half the curve's
        time to the peers. None of the buckets is on the established-session
        path.
*/
#define WATERLINK_ADMIT_SOURCES 64
#define WATERLINK_ADMIT_BURST 5
#define WATERLINK_ADMIT_RATE 5
#define WATERLINK_ADMIT_GLOBAL_BURST 64
#define WATERLINK_ADMIT_GLOBAL_RATE 64
#define WATERLINK_ADMIT_LOADED (WATERLINK_ADMIT_GLOBAL_RATE / 2)
#define WATERLINK_COOKIE_SECONDS 120

// Tokens that refill at rate a second up to burst; a new bucket is full.
struct waterlink_bucket {
        p64 at; // microseconds, when last refilled; 0 for never used
        p32 tokens;
};

struct waterlink_admission {
        p8 address[WATERLINK_ADMIT_SOURCES][16];
        struct waterlink_bucket source[WATERLINK_ADMIT_SOURCES];
        struct waterlink_bucket all;
        struct waterlink_bucket arrivals;
        p8 secret[32]; // the cookies' key, the caller's to make again
        p64 secret_made; // microseconds, when; 0 for never
        p64 refused;
        p64 cookies;
};

static bool waterlink_bucket_take(struct waterlink_bucket address_to bucket,
                                  p32 burst, p32 rate, p64 now)
{
        if (!bucket->at)
        {
                bucket->at = now ? now : 1;
                bucket->tokens = burst;
        }
        else
        {
                p64 elapsed;
                p64 full_after;
                p64 earned;

                /* link_now is monotonic, but suspend/virtualization faults and
                   a hostile test clock must fail closed rather than turn an
                   unsigned subtraction into a full refill. */
                if (now < bucket->at)
                        return false;
                elapsed = now - bucket->at;
                full_after = ((p64)burst * 1000000 + rate - 1) / rate;
                earned = elapsed >= full_after
                                 ? burst
                                 : elapsed * rate / 1000000;

                if (earned)
                {
                        bucket->tokens = earned >= burst - bucket->tokens
                                                 ? burst
                                                 : bucket->tokens + (p32)earned;
                        bucket->at = now;
                }
        }
        if (!bucket->tokens)
                return false;
        bucket->tokens--;
        return true;
}

/*      Whether the cookie secret may be used now: made, and not two
        minutes ago. One too old is not stretched while a new one cannot be
        made -- the caller makes them, and a machine with no randomness has
        no handshake to protect either. */
bool waterlink_cookie_fresh(struct waterlink_admission address_to table,
                            p64 now)
{
        return table->secret_made && now >= table->secret_made &&
               now - table->secret_made <
                       (p64)WATERLINK_COOKIE_SECONDS * 1000000;
}

// The cookie for a source: a MAC of its address and port by the secret.
static fn waterlink_cookie_of(struct waterlink_admission address_to table,
                              p8 address_to address, p16 port,
                              p8 address_to cookie)
{
        p8 place[18];
        p8 full[32];

        memory_copy(place, address, 16);
        network_store_16(place + 16, port);
        crypto_hmac_sha256(table->secret, 32, place, sizeof place, full);
        memory_copy(cookie, full, 16);
        crypto_forget(full, sizeof full);
}

/*      mac2: sixteen bytes of HMAC-SHA256 by a cookie over everything of an
        initiation up to and with mac1, written where it goes or checked
        there. */
static bool waterlink_mac2_at(p8 address_to cookie, p8 address_to datagram,
                              bool write)
{
        p8 full[32];
        bool same;

        crypto_hmac_sha256(cookie, 16, datagram, 16 + WATERLINK_INITIATE_BYTES,
                           full);
        if (write)
                memory_copy(datagram + 16 + WATERLINK_INITIATE_BYTES, full, 16);
        same = crypto_same(full, datagram + 16 + WATERLINK_INITIATE_BYTES, 16);
        crypto_forget(full, sizeof full);
        return same;
}

fn waterlink_mac2(p8 address_to cookie, p8 address_to datagram)
{
        (void)waterlink_mac2_at(cookie, datagram, true);
}

/*      A cookie sealed or opened in place, sixteen bytes and a tag after:
        a key for this reply alone from the sealer and the nonce, a zero
        nonce under it, and mac1 of the initiation it answers as the
        associated data. */
static bool waterlink_cookie_box(p8 address_to sealer, p8 address_to nonce,
                                 p8 address_to mac1, p8 address_to text,
                                 bool seal)
{
        crypto_aesgcm_key key;
        p8 once[32];
        p8 iv[12];
        bool good = true;

        crypto_hmac_sha256(sealer, 32, nonce, 24, once);
        crypto_aesgcm_prepare(address_of key, once);
        memory_zero(iv, sizeof iv);
        if (seal)
                crypto_aesgcm_seal(address_of key, iv, mac1, 16, text, 16,
                                   text + 16);
        else
                good = crypto_aesgcm_open(address_of key, iv, mac1, 16, text,
                                          16, text + 16);
        crypto_forget(address_of key, sizeof key);
        crypto_forget(once, sizeof once);
        return good;
}

/*
        Whether an initiation that passed mac1 may go on to the curve: 1, 0
        for no, or -1 for no but with a cookie reply (waterlink_cookie_reply)
        -- under load, when mac2 is not good for where it came from.
*/
bipolar waterlink_admit(struct waterlink_admission address_to table,
                        p8 address_to datagram, p8 address_to address, p16 port,
                        p64 now)
{
        positive at = 0;
        positive stalest = 0;
        bool source_proven = false;

        if (!waterlink_bucket_take(address_of table->arrivals,
                                   WATERLINK_ADMIT_LOADED,
                                   WATERLINK_ADMIT_LOADED, now))
        {
                p8 cookie[16];
                bool proven = false;

                if (!waterlink_cookie_fresh(table, now))
                {
                        table->refused++;
                        return 0;
                }
                waterlink_cookie_of(table, address, port, cookie);
                proven = waterlink_mac2_at(cookie, datagram, false);
                crypto_forget(cookie, sizeof cookie);
                if (!proven)
                {
                        table->cookies++;
                        return -1;
                }
                source_proven = true;
        }

        for (; source_proven && at < WATERLINK_ADMIT_SOURCES; at++)
        {
                if (table->source[at].at &&
                    !memory_compare(table->address[at], address, 16))
                        break;
                if (table->source[at].at < table->source[stalest].at)
                        stalest = at;
        }
        if (source_proven && at == WATERLINK_ADMIT_SOURCES)
        {
                at = stalest;
                memory_copy(table->address[at], address, 16);
                table->source[at].at = 0;
        }

        if ((source_proven &&
             !waterlink_bucket_take(table->source + at, WATERLINK_ADMIT_BURST,
                                    WATERLINK_ADMIT_RATE, now)) ||
            !waterlink_bucket_take(address_of table->all,
                                   WATERLINK_ADMIT_GLOBAL_BURST,
                                   WATERLINK_ADMIT_GLOBAL_RATE, now))
        {
                table->refused++;
                return 0;
        }
        return 1;
}

/*
        The answer to an initiation turned away under load: its source's
        cookie, sealed to mac1 of the initiation by me's sealer. nonce is 24
        random bytes the caller drew; the reply is WATERLINK_COOKIE_DATAGRAM
        bytes.
*/
fn waterlink_cookie_reply(struct waterlink_identity address_to me,
                          struct waterlink_admission address_to table,
                          p8 address_to initiation, p8 address_to address,
                          p16 port, p8 address_to nonce, p8 address_to reply)
{
        struct waterlink_datagram head = {WATERLINK_KIND_COOKIE, 0, 0};

        memory_copy(reply, address_of head, 16);
        memory_copy(reply + 16, nonce, 24);
        waterlink_cookie_of(table, address, port, reply + 40);
        (void)waterlink_cookie_box(me->sealer, nonce,
                                   initiation + 16 + WATERLINK_INITIATE_BYTES - 16,
                                   reply + 40, true);
}

/*
        A cookie reply to an initiation this end sent to responder_public,
        whose mac1 was mac1: the cookie, or false when it is not one.
*/
bool waterlink_cookie_take(p8 address_to responder_public, p8 address_to mac1,
                           p8 address_to reply, positive length,
                           p8 address_to cookie)
{
        struct waterlink_datagram head;
        p8 sealer[32];
        p8 box[32];
        bool good;

        if (length != WATERLINK_COOKIE_DATAGRAM)
                return false;
        memory_copy(address_of head, reply, 16);
        if (head.kind != WATERLINK_KIND_COOKIE || head.receiver || head.counter)
                return false;
        waterlink_label_key((string_address) "cookie--", responder_public,
                            sealer);
        memory_copy(box, reply + 40, 32);
        good = waterlink_cookie_box(sealer, reply + 16, mac1, box, false);
        if (good)
                memory_copy(cookie, box, 16);
        crypto_forget(box, sizeof box);
        return good;
}

/*
        When a session may no longer be used at all -- waterlink.c's numbers:
        either side refuses one past REJECT, so an initiator that has gone
        quiet cannot keep one alive forever. The initiator keys again at
        REKEY, a time and not a count: the count would take nine billion
        datagrams a second to reach in that time, and the one past which a
        session is refused is checked here all the same.
*/
bool waterlink_session_spent(p64 age, p64 sent)
{
        return age >= (p64)WATERLINK_REJECT_SECONDS * 1000000 ||
               sent >= WATERLINK_REKEY_MESSAGES + (1ull << 20);
}

#endif // WATERLINK_HANDSHAKE_INCLUDED
