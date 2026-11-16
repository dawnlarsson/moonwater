/*
        A group's keys, and the service its members announce.

        The members of a group find each other on the local network over
        mDNS and DNS-SD, which is the network's own discovery
        (src/net/discovery.c): the records, the socket and the confined
        reader are its, and what is waterlink's is the service announced.
        That says a waterlink machine is here, on which port, and nothing
        more: one instance under _waterlink._udp.local named by a random
        label, pointing at a random host label, both drawn at every start.
        Which group a machine is in, and which machine it is, is never said.
        A member does not need it said: it greets every machine it finds with
        its group's handshake (nearby.c), and only a member can read one --
        anyone else drops it at the first gate.

        THE SECRET

        A group is a namespace and a secret, and every key comes from the two
        of them through PBKDF2-HMAC-SHA256, the namespace in the salt, at
        WATERLINK_GROUP_ROUNDS iterations -- a protocol constant, since every
        member must arrive at the same key. That is the only thing standing
        between a weak secret and anyone on the network: a member's greeting
        carries a gate keyed from it, so a listener who hears one can try
        guesses offline without ever sending a thing, at the price of one
        derivation a guess: 56 ms on one core of a 9950X, and a graphics card
        running PBKDF2 does thousands a second, so a word a person chose will
        fall to anyone who cares to try. A secret `moonwater link group` makes
        itself has 160 random bits, and no guessing reaches it.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/

#ifndef WATERLINK_GROUP_INCLUDED
#define WATERLINK_GROUP_INCLUDED

#include "handshake.c"
#include "../net/discovery.c" // already in the shell (src/sh/net.c); first here where the link is built without one

#define WATERLINK_NAMESPACE_MAX 32

/*      Every member derives the same key, so this is part of the protocol:
        change it and machines built before and after cannot pair. OWASP's
        2023 figure for PBKDF2-HMAC-SHA256. */
#ifndef WATERLINK_GROUP_ROUNDS
#define WATERLINK_GROUP_ROUNDS 600000
#endif

// The service, the type every DNS-SD browser is asked about, and what the
// random labels of an instance and of a host start with.
static const p8 waterlink_service_name[] = "\x0a_waterlink\x04_udp\x05local";
#define WATERLINK_SERVICE_BYTES (sizeof(waterlink_service_name) - 1 + 1)
static const discovery_mdns_service waterlink_mdns_service = {
    waterlink_service_name, WATERLINK_SERVICE_BYTES, "wl-"};

/*
        What a group's key gives a member: the identity every member shares
        -- the group's own X25519 pair, which the greetings are addressed to
        -- the pre-shared key a greeting must also prove, and the mark a peer
        record paired by the group carries.
*/
struct waterlink_group_keys {
        struct waterlink_identity identity;
        p8 psk[32];
        p32 mark;
};

/*
        PBKDF2-HMAC-SHA256, one block of output, the salt naming the protocol
        and the namespace so no two groups share a derivation.
*/
fn waterlink_group_derive(string_address namespace, p8 address_to secret,
                          positive secret_length, positive rounds,
                          p8 address_to out)
{
        p8 salt[18 + WATERLINK_NAMESPACE_MAX];
        positive namespace_length = string_length(namespace);

        if (namespace_length > WATERLINK_NAMESPACE_MAX)
                namespace_length = WATERLINK_NAMESPACE_MAX;
        memory_copy(salt, "waterlink group 1 ", 18);
        memory_copy(salt + 18, namespace, namespace_length);
        crypto_pbkdf2(DIGEST_SHA256, 32, secret, secret_length, salt,
                      18 + namespace_length, rounds, out, 32);
}

fn waterlink_group_keys_from(struct waterlink_group_keys address_to keys,
                             p8 address_to group, string_address namespace)
{
        p8 secret[32];
        p8 hashed[32];

        crypto_hkdf_expand(group, (p8 address_to) "waterlink group static", 22,
                           secret, 32);
        waterlink_identity_from(address_of keys->identity, secret);
        crypto_forget(secret, sizeof secret);
        crypto_hkdf_expand(group, (p8 address_to) "waterlink group psk", 19,
                           keys->psk, 32);

        //      A peer record keeps which group paired it as 32 bits of the
        //      namespace's hash, never zero: zero is paired by hand.
        {
                crypto_sha256 hash;

                crypto_sha256_open(address_of hash);
                crypto_sha256_write(address_of hash,
                                    (p8 address_to) "waterlink mark ", 15);
                crypto_sha256_write(address_of hash, (p8 address_to)namespace,
                                    string_length(namespace));
                crypto_sha256_close(address_of hash, hashed);
        }
        memory_copy(address_of keys->mark, hashed, 4);
        if (!keys->mark)
                keys->mark = 1;
}

#endif // WATERLINK_GROUP_INCLUDED
