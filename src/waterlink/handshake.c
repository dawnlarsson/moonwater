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

// ephemeral, sealed static, sealed hello, mac1
#define WATERLINK_INITIATE_BYTES (32 + 48 + WATERLINK_HELLO_BYTES + 16 + 16)
// ephemeral, sealed index, mac1
#define WATERLINK_RESPOND_BYTES (32 + 4 + 16 + 16)

struct waterlink_identity {
        p8 secret[WATERLINK_KEY_BYTES];
        p8 public[WATERLINK_KEY_BYTES];
        p8 gate[32]; // mac1's key for datagrams sent to this identity
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

// HKDF with the chaining key as salt and no info: two outputs.
static fn waterlink_mix_key(struct waterlink_noise address_to noise,
                            p8 address_to material, positive length)
{
        p8 prk[32];
        p8 out[64];

        crypto_hkdf_extract(noise->chain, 32, material, length, prk);
        crypto_hkdf_expand(prk, (p8 address_to) "", 0, out, 64);
        memory_copy(noise->chain, out, 32);
        memory_copy(noise->key, out + 32, 32);
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
                waterlink_mix_key(noise, shared, 32);
        crypto_forget(shared, sizeof shared);
        return good;
}

static fn waterlink_noise_nonce(p8 address_to iv, p64 nonce)
{
        memory_zero(iv, 4);
        crypto_put_be64(iv + 4, nonce);
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

        if (length + 16 > sizeof sealed)
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

// MixKeyAndHash: three outputs, the middle one into the hash.
static fn waterlink_mix_key_hash(struct waterlink_noise address_to noise,
                                 p8 address_to material, positive length)
{
        p8 prk[32];
        p8 out[96];

        crypto_hkdf_extract(noise->chain, 32, material, length, prk);
        crypto_hkdf_expand(prk, (p8 address_to) "", 0, out, 96);
        memory_copy(noise->chain, out, 32);
        waterlink_mix_hash(noise, out + 32, 32);
        memory_copy(noise->key, out + 64, 32);
        noise->nonce = 0;
        crypto_forget(prk, sizeof prk);
        crypto_forget(out, sizeof out);
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

static fn waterlink_gate_of(p8 address_to public, p8 address_to gate);

fn waterlink_identity_from(struct waterlink_identity address_to identity,
                           p8 address_to secret)
{
        memory_copy(identity->secret, secret, 32);
        crypto_x25519(identity->public, identity->secret, waterlink_base);
        waterlink_gate_of(identity->public, identity->gate);
}

// mac1's key for a datagram going to the holder of public.
static fn waterlink_gate_of(p8 address_to public, p8 address_to gate)
{
        crypto_sha256 hash;

        crypto_sha256_open(address_of hash);
        crypto_sha256_write(address_of hash, (p8 address_to) "mac1----", 8);
        crypto_sha256_write(address_of hash, public, 32);
        crypto_sha256_close(address_of hash, gate);
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
        and the padding after it is zero. The cheap question, asked first.
*/
bool waterlink_gate_passes(struct waterlink_identity address_to me,
                           p8 address_to datagram, positive length)
{
        struct waterlink_datagram head;
        positive body;
        p8 mac[16];

        if (length != WATERLINK_DATAGRAM)
                return false;

        memory_copy(address_of head, datagram, 16);
        if (head.kind == WATERLINK_KIND_INITIATE)
                body = WATERLINK_INITIATE_BYTES;
        else if (head.kind == WATERLINK_KIND_RESPOND)
                body = WATERLINK_RESPOND_BYTES;
        else
                return false;

        if (memory_span_byte(datagram + 16 + body, 0, length - 16 - body) !=
            length - 16 - body)
                return false;

        waterlink_mac1(me->gate, datagram, 16 + body - 16, mac);
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
                waterlink_mix_key(noise, at, 32);
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
                waterlink_mix_key_hash(noise, psk, 32);

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
                waterlink_mix_key(noise, at, 32);
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
                waterlink_mix_key_hash(noise, psk, 32);

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
        Split: the initiator sends with the first key and the responder with
        the second. Both cut to AES-128's sixteen bytes. The handshake state
        is forgotten after.
*/
fn waterlink_split(struct waterlink_noise address_to noise, bool initiator,
                   p8 address_to sending, p8 address_to receiving)
{
        p8 prk[32];
        p8 out[64];

        crypto_hkdf_extract(noise->chain, 32, (p8 address_to) "", 0, prk);
        crypto_hkdf_expand(prk, (p8 address_to) "", 0, out, 64);
        memory_copy(initiator ? sending : receiving, out,
                    WATERLINK_AEAD_BYTES);
        memory_copy(initiator ? receiving : sending, out + 32,
                    WATERLINK_AEAD_BYTES);
        crypto_forget(prk, sizeof prk);
        crypto_forget(out, sizeof out);
        crypto_forget(noise, sizeof(address_to noise));
}

/*
        TAI64N, as WireGuard stamps its initiations: seconds past 1970 plus
        2^62, then nanoseconds, both big endian, so newer compares greater
        byte by byte.
*/
fn waterlink_stamp(p8 address_to stamp, p64 seconds, p32 nanoseconds)
{
        crypto_put_be64(stamp, seconds + (1ull << 62));
        network_store_32(stamp + 8, nanoseconds);
}

bool waterlink_stamp_newer(p8 address_to stamp, p8 address_to last)
{
        return memory_compare(stamp, last, WATERLINK_STAMP_BYTES) > 0;
}

/*
        Initiations per source address: a bucket of WATERLINK_ADMIT_BURST
        that refills at WATERLINK_ADMIT_RATE a second, in a table of fixed
        size where a new address takes the stalest entry. A second bucket
        caps the whole table: source addresses are unauthenticated here, so
        without it a sender could rotate spoofed addresses through the table
        and keep the curve busy without limit. Neither bucket is on the
        established-session path.
*/
#define WATERLINK_ADMIT_SOURCES 64
#define WATERLINK_ADMIT_BURST 5
#define WATERLINK_ADMIT_RATE 5
#define WATERLINK_ADMIT_GLOBAL_BURST 64
#define WATERLINK_ADMIT_GLOBAL_RATE 64

// Tokens that refill at rate a second up to burst; a new bucket is full.
struct waterlink_bucket {
        p64 at; // microseconds, when last refilled; 0 for never used
        p32 tokens;
};

struct waterlink_admission {
        p8 address[WATERLINK_ADMIT_SOURCES][16];
        struct waterlink_bucket source[WATERLINK_ADMIT_SOURCES];
        struct waterlink_bucket all;
        p64 refused;
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
                p64 earned = (now - bucket->at) * rate / 1000000;

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

bool waterlink_admit(struct waterlink_admission address_to table,
                     p8 address_to address, p64 now)
{
        positive at = 0;
        positive stalest = 0;

        for (; at < WATERLINK_ADMIT_SOURCES; at++)
        {
                if (table->source[at].at &&
                    !memory_compare(table->address[at], address, 16))
                        break;
                if (table->source[at].at < table->source[stalest].at)
                        stalest = at;
        }
        if (at == WATERLINK_ADMIT_SOURCES)
        {
                at = stalest;
                memory_copy(table->address[at], address, 16);
                table->source[at].at = 0;
        }

        if (!waterlink_bucket_take(table->source + at, WATERLINK_ADMIT_BURST,
                                   WATERLINK_ADMIT_RATE, now) ||
            !waterlink_bucket_take(address_of table->all,
                                   WATERLINK_ADMIT_GLOBAL_BURST,
                                   WATERLINK_ADMIT_GLOBAL_RATE, now))
        {
                table->refused++;
                return false;
        }
        return true;
}

/*
        When a session must be keyed again, and when it may no longer be
        used at all -- waterlink.c's numbers. The initiator keys again at
        REKEY; either side refuses a session past REJECT, so an initiator
        that has gone quiet cannot keep one alive forever.
*/
bool waterlink_rekey_due(p64 age, p64 sent)
{
        return age >= (p64)WATERLINK_REKEY_SECONDS * 1000000 ||
               sent >= WATERLINK_REKEY_MESSAGES;
}

bool waterlink_session_spent(p64 age, p64 sent)
{
        return age >= (p64)WATERLINK_REJECT_SECONDS * 1000000 ||
               sent >= WATERLINK_REKEY_MESSAGES + (1ull << 20);
}

#endif // WATERLINK_HANDSHAKE_INCLUDED
