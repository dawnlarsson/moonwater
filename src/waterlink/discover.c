/*
        Finding the other members of a group on the local network, over
        mDNS and DNS-SD -- the protocol printers announce themselves with, so
        `dns-sd -B _waterlink._udp` on a Mac or avahi-browse on Linux lists a
        machine that is listening.

        What is announced says that a waterlink machine is here, on which
        port, and nothing more: one instance under _waterlink._udp.local named
        by a random label, pointing at a random host label, both drawn at
        every start. Which group a machine is in, and which machine it is,
        is never said. A member does not need it said: it greets every
        machine it finds with its group's handshake (nearby.c), and only a
        member can read one -- anyone else drops it at the first gate.

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
        fall to anyone who cares to try. A secret `moonwater link join` makes
        itself has 160 random bits, and no guessing reaches it.

        The records are parsed the way the most exposed parser on the machine
        must be: every length checked against what is left, a compression
        pointer followed only backwards and at most a few times, a name held
        to 255 bytes and a label to 63, label types other than plain refused,
        and nothing taken from a record that another check still depends on.
        An address is never read out of a record at all -- the A record and
        the SRV target are whatever the sender wrote -- so the caller connects
        to the packet's own source, on the port the SRV names.

        Like link.c this is a transform; the caller owns the socket.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/

#ifndef WATERLINK_DISCOVER_INCLUDED
#define WATERLINK_DISCOVER_INCLUDED

#include "handshake.c"

#define WATERLINK_MDNS_PORT 5353
#define WATERLINK_MDNS_GROUP 0xe00000fbu // 224.0.0.251
#define WATERLINK_MDNS_MAX 1500

#define WATERLINK_NAMESPACE_MAX 32

/*      Every member derives the same key, so this is part of the protocol:
        change it and machines built before and after cannot pair. OWASP's
        2023 figure for PBKDF2-HMAC-SHA256. */
#ifndef WATERLINK_GROUP_ROUNDS
#define WATERLINK_GROUP_ROUNDS 600000
#endif

// The service, the type every DNS-SD browser is asked about.
static const p8 waterlink_service_name[] = "\x0a_waterlink\x04_udp\x05local";
#define WATERLINK_SERVICE_BYTES (sizeof(waterlink_service_name) - 1 + 1)

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

// Writing ---------------------------------------------------------------

/*
        The records are a fixed layout, at most a few hundred bytes with an
        echoed question in front, so they are stored straight into the
        packet: a head, a label of "wl-" and hex, a pointer back to a name
        already written, a record's type, class, TTL and length.
*/
static p8 address_to waterlink_dns_head(p8 address_to at, p16 id, p16 flags,
                                        p16 questions, p16 answers)
{
        network_store_16(at, id);
        network_store_16(at + 2, flags);
        network_store_16(at + 4, questions);
        network_store_16(at + 6, answers);
        memory_zero(at + 8, 4);
        return at + 12;
}

static p8 address_to waterlink_dns_label(p8 address_to at,
                                         p8 address_to random, positive bytes)
{
        at[0] = (p8)(3 + 2 * bytes);
        memory_copy(at + 1, "wl-", 3);
        return at + 4 + memory_into_hex(at + 4, random, bytes);
}

static p8 address_to waterlink_dns_pointer(p8 address_to at, p8 address_to packet,
                                           p8 address_to name)
{
        network_store_16(at, (p16)(0xc000 | (name - packet)));
        return at + 2;
}

static p8 address_to waterlink_dns_record(p8 address_to at, p16 type, p16 class,
                                          p32 ttl, p16 length)
{
        network_store_16(at, type);
        network_store_16(at + 2, class);
        network_store_32(at + 4, ttl);
        network_store_16(at + 8, length);
        return at + 10;
}

/*
        This machine's announcement, or with ttl 0 its goodbye: PTR from the
        service to the instance, SRV to the host on the port, an empty TXT
        (DNS-SD asks for one), and the host's A record when there is an
        address to give. The instance label is "wl-" and twenty hex digits,
        the host label "wl-" and twelve, both random and chosen by the caller.
        A reply to a one-shot query carries its ID and its question. Returns
        the length, or 0 when it would not fit.
*/
positive waterlink_mdns_announce(p8 address_to packet, positive room,
                                 p8 address_to instance_bytes,
                                 p8 address_to host_bytes, p16 port,
                                 p32 address, p32 ttl, p16 id,
                                 p8 address_to question, positive question_length)
{
        p8 address_to at;
        p8 address_to service;
        p8 address_to instance;
        p8 address_to host;

        if (room < 12 + question_length + 160)
                return 0;
        at = waterlink_dns_head(packet, id, 0x8400, question ? 1 : 0,
                                3 + (address ? 1 : 0));
        memory_copy(at, question, question_length);
        at += question_length;

        //      PTR: the service, to the instance.
        service = at;
        memory_copy(at, waterlink_service_name, WATERLINK_SERVICE_BYTES);
        at = waterlink_dns_record(at + WATERLINK_SERVICE_BYTES, 12, 1,
                                  ttl ? 4500 : 0, 1 + 23 + 2);
        instance = at;
        at = waterlink_dns_label(at, instance_bytes, 10);
        at = waterlink_dns_pointer(at, packet, service);

        //      SRV: the instance, to the host, on the port.
        at = waterlink_dns_pointer(at, packet, instance);
        at = waterlink_dns_record(at, 33, 0x8001, ttl, 6 + 1 + 15 + 7);
        network_store_32(at, 0);
        network_store_16(at + 4, port);
        host = at + 6;
        at = waterlink_dns_label(host, host_bytes, 6);
        memory_copy(at, "\x05local", 7);
        at += 7;

        //      TXT: empty, one zero byte.
        at = waterlink_dns_pointer(at, packet, instance);
        at = waterlink_dns_record(at, 16, 0x8001, ttl ? 4500 : 0, 1);
        *at++ = 0;

        if (address)
        {
                at = waterlink_dns_pointer(at, packet, host);
                at = waterlink_dns_record(at, 1, 0x8001, ttl, 4);
                network_store_32(at, address);
                at += 4;
        }
        return (positive)(at - packet);
}

// The question every browser and every member asks.
positive waterlink_mdns_query(p8 address_to packet, positive room)
{
        p8 address_to at;

        if (room < 12 + WATERLINK_SERVICE_BYTES + 4)
                return 0;
        at = waterlink_dns_head(packet, 0, 0, 1, 0);
        memory_copy(at, waterlink_service_name, WATERLINK_SERVICE_BYTES);
        at += WATERLINK_SERVICE_BYTES;
        network_store_16(at, 12);
        network_store_16(at + 2, 1);
        return (positive)(at + 4 - packet);
}

// Reading ------------------------------------------------------------------

/*
        A name at offset at, flattened into labels as lengths and bytes the
        way it sits on the wire uncompressed -- net.c's reader, the one the
        resolver trusts: a pointer only ever goes back past where it stands,
        and nothing after it may read up to it again, so no loop is possible.
        Returns the offset just past the name as it sits in the packet, or 0
        for anything that is not a name.
*/
#define WATERLINK_NAME_BYTES 256

static positive waterlink_dns_name(const p8 address_to packet, positive length,
                                   positive at, p8 address_to into,
                                   positive address_to into_length)
{
        positive ended;
        bipolar used = dns_copy_name((p8 address_to)packet, length, at, into,
                                     WATERLINK_NAME_BYTES - 1, address_of ended);

        if (used < 0)
                return 0;
        address_to into_length = (positive)used;
        return ended;
}

// Whether a flattened name is the service's, letter case aside.
static bool waterlink_is_service(const p8 address_to name, positive length)
{
        return length == WATERLINK_SERVICE_BYTES &&
               !memory_compare_ascii_case(name, waterlink_service_name, length);
}

// An instance of the service: one label and then the service's name.
static bool waterlink_instance_of(const p8 address_to name, positive length,
                                  p8 address_to label, positive address_to label_length)
{
        positive first = name[0];

        if (!first || 1 + first >= length ||
            !waterlink_is_service(name + 1 + first, length - 1 - first))
                return false;
        memory_copy(label, name + 1, first);
        address_to label_length = first;
        return true;
}

#define WATERLINK_FOUND_MAX 8

struct waterlink_found_instance {
        p8 label[63];
        positive label_length;
        p16 port;
        bool has_port;
};

struct waterlink_found {
        p16 id;
        bool response;
        bool asked; // a question for the service
        p8 question[WATERLINK_NAME_BYTES + 4];
        positive question_length;
        positive count;
        struct waterlink_found_instance instance[WATERLINK_FOUND_MAX];
};

static struct waterlink_found_instance address_to
waterlink_found_at(struct waterlink_found address_to found,
                   const p8 address_to label, positive label_length)
{
        for (positive at = 0; at < found->count; at++)
                if (found->instance[at].label_length == label_length &&
                    !memory_compare(found->instance[at].label, label,
                                    label_length))
                        return found->instance + at;

        if (found->count == WATERLINK_FOUND_MAX)
                return null;
        memory_zero(found->instance + found->count,
                    sizeof(found->instance[0]));
        memory_copy(found->instance[found->count].label, label, label_length);
        found->instance[found->count].label_length = label_length;
        return found->instance + found->count++;
}

/*
        Read one mDNS message: whether it asks about the service, and every
        instance of the service it describes with an SRV port. False when it
        is not a well formed message; a well formed one about other things
        answers true with nothing found.
*/
bool waterlink_mdns_read(const p8 address_to packet, positive length,
                         struct waterlink_found address_to found)
{
        p8 name[WATERLINK_NAME_BYTES];
        positive name_length;
        positive at = 12;
        byte_reader header = byte_reader_open(packet, length);
        p16 flags;
        p32 questions, records;

        memory_zero(found, sizeof(address_to found));
        if (length < 12 || length > WATERLINK_MDNS_MAX)
                return false;

        found->id = byte_reader_u16(&header);
        flags = byte_reader_u16(&header);
        found->response = (flags & 0x8000) != 0;
        //      Opcode, and rcode in a response, must be zero.
        if ((flags & 0x7800) || (found->response && (flags & 0x000f)))
                return false;

        questions = byte_reader_u16(&header);
        records = byte_reader_u16(&header);
        records += byte_reader_u16(&header);
        records += byte_reader_u16(&header);

        //      Each question is at least five bytes and each record eleven:
        //      a count the packet cannot hold is refused before any is read.
        if (questions * 5 + records * 11 > length - 12)
                return false;

        for (p32 q = 0; q < questions; q++)
        {
                positive start = at;
                positive next = waterlink_dns_name(packet, length, at, name,
                                                   address_of name_length);
                byte_reader tail = dns_message_at(packet, length, next);
                p32 type;

                //      The type and the class, after a name that was one.
                type = byte_reader_u16(&tail);
                (void)byte_reader_skip(&tail, 2);
                if (!next || !byte_reader_ok(&tail))
                        return false;
                if (!found->response && waterlink_is_service(name, name_length) &&
                    (type == 12 || type == 255))
                {
                        byte_reader echo = dns_message_at(packet, length, start);
                        const p8 address_to whole;

                        found->asked = true;
                        //      The question, kept whole and uncompressed, for
                        //      a reply that has to echo it.
                        if (next - start == name_length &&
                            name_length + 4 <= sizeof(found->question) &&
                            (whole = byte_reader_take(&echo, name_length + 4)))
                        {
                                memory_copy(found->question, whole,
                                            name_length + 4);
                                found->question_length = name_length + 4;
                        }
                }
                at = length - byte_reader_left(&tail);
        }

        for (p32 r = 0; r < records; r++)
        {
                positive next = waterlink_dns_name(packet, length, at, name,
                                                   address_of name_length);
                byte_reader tail = dns_message_at(packet, length, next);
                byte_reader data;
                p32 type;
                p8 label[63];
                positive label_length;
                struct waterlink_found_instance address_to instance;

                //      The type, class and time to live, then the data
                //      behind its length.
                type = byte_reader_u16(&tail);
                (void)byte_reader_skip(&tail, 6);
                data = byte_reader_vector16(&tail);
                if (!next || !byte_reader_ok(&tail))
                        return false;
                at = length - byte_reader_left(&tail);

                if (!found->response ||
                    !waterlink_instance_of(name, name_length, label,
                                           address_of label_length))
                        continue;

                if (type == 33 && byte_reader_left(&data) >= 7)
                {
                        //      Priority and weight, then the port.
                        (void)byte_reader_skip(&data, 4);
                        instance = waterlink_found_at(found, label,
                                                      label_length);
                        if (instance)
                        {
                                instance->port = byte_reader_u16(&data);
                                instance->has_port = true;
                        }
                }
        }

        return at == length;
}

#endif // WATERLINK_DISCOVER_INCLUDED
