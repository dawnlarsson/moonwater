/*
        src/sh/host/nl80211.c: the radio's words to the kernel: nl80211, a station joined and left and the process
        that keeps it (the keeper), WPA's and SAE's handshakes; the wifi verb is radio.c's.

        A run of src/sh/host.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. host_main, at the end
        of src/sh/host.c, names the function each verb runs.
*/

/* ---- radio: wifi and bluetooth, and the nl80211 they ask the kernel through. ---- */

/*
        Wireless and bluetooth, as moonwater verbs.

        Secrets stay on /root so an image update does not take the password
        with it. /ip watch still owns the address: this only joins the radio
        and says which link to prefer when both have carrier.
*/

/* ---- nl80211: how the wifi above talks to the kernel. ---- */

/* ---- nl80211: join a station, leave it ---- */

#ifndef STANDARD_MODERN_C_NET_NL80211
#define STANDARD_MODERN_C_NET_NL80211

#define GENL_ID_CTRL 16
#define GENL_HEADER 4
#define CTRL_CMD_GETFAMILY 3
#define CTRL_ATTR_FAMILY_ID 1
#define CTRL_ATTR_FAMILY_NAME 2
#define CTRL_ATTR_MCAST_GROUPS 7
#define CTRL_ATTR_MCAST_GRP_NAME 1
#define CTRL_ATTR_MCAST_GRP_ID 2

#define NL80211_CMD_GET_WIPHY 1
#define NL80211_CMD_NEW_WIPHY 3
#define NL80211_CMD_GET_INTERFACE 5
#define NL80211_CMD_NEW_INTERFACE 7
#define NL80211_CMD_NEW_KEY 11
#define NL80211_CMD_GET_STATION 17
#define NL80211_CMD_SET_STATION 18
#define NL80211_CMD_NEW_STATION 19
#define NL80211_CMD_CONNECT 46
#define NL80211_CMD_AUTHENTICATE 37
#define NL80211_CMD_ASSOCIATE 38
#define NL80211_CMD_DEAUTHENTICATE 39
#define NL80211_CMD_DISASSOCIATE 40
#define NL80211_CMD_DISCONNECT 48
#define NL80211_CMD_SET_REKEY_OFFLOAD 79

#define NL80211_ATTR_WIPHY 1
#define NL80211_ATTR_IFINDEX 3
#define NL80211_ATTR_IFNAME 4
#define NL80211_ATTR_IFTYPE 5
#define NL80211_ATTR_MAC 6
#define NL80211_ATTR_KEY_DATA 7
#define NL80211_ATTR_KEY_IDX 8
#define NL80211_ATTR_KEY_CIPHER 9
#define NL80211_ATTR_KEY_SEQ 10
#define NL80211_ATTR_KEY_DEFAULT 11
#define NL80211_ATTR_WIPHY_FREQ 38
#define NL80211_ATTR_IE 42
#define NL80211_ATTR_SCAN_FREQUENCIES 44
#define NL80211_ATTR_SCAN_SSIDS 45
#define NL80211_ATTR_FRAME 51
#define NL80211_ATTR_SSID 52
#define NL80211_ATTR_AUTH_TYPE 53
#define NL80211_ATTR_REASON_CODE 54
#define NL80211_ATTR_KEY_TYPE 55
#define NL80211_ATTR_TIMED_OUT 65
#define NL80211_ATTR_USE_MFP 66
#define NL80211_ATTR_STA_FLAGS2 67
#define NL80211_ATTR_CONTROL_PORT 68
#define NL80211_ATTR_PRIVACY 70
#define NL80211_ATTR_STATUS_CODE 72
#define NL80211_ATTR_CIPHER_SUITES_PAIRWISE 73
#define NL80211_ATTR_CIPHER_SUITE_GROUP 74
#define NL80211_ATTR_WPA_VERSIONS 75
#define NL80211_ATTR_AKM_SUITES 76
#define NL80211_ATTR_REKEY_DATA 122
#define NL80211_REKEY_DATA_KEK 1
#define NL80211_REKEY_DATA_KCK 2
#define NL80211_REKEY_DATA_REPLAY_CTR 3
#define NL80211_ATTR_AUTH_DATA 156
#define NL80211_ATTR_SCAN_FLAGS 158
#define NL80211_ATTR_SPLIT_WIPHY_DUMP 174
#define NL80211_ATTR_EXT_FEATURES 217
#define NL80211_ATTR_PMK 254

#define NL80211_IFTYPE_ADHOC 1
#define NL80211_IFTYPE_STATION 2
#define NL80211_IFTYPE_P2P_CLIENT 8
#define NL80211_SCAN_FLAG_FLUSH 2
#define NL80211_AUTHTYPE_OPEN 0
#define NL80211_AUTHTYPE_SAE 4
#define NL80211_MFP_REQUIRED 1
#define NL80211_WPA_VERSION_2 2
#define NL80211_KEYTYPE_GROUP 0
#define NL80211_KEYTYPE_PAIRWISE 1
#define NL80211_STA_FLAG_AUTHORIZED 1
#define NL80211_EXT_FEATURE_4WAY_HANDSHAKE_STA_PSK 15
#define WLAN_CIPHER_CCMP 0x000fac04u
#define WLAN_CIPHER_BIP_CMAC_128 0x000fac06u
#define WLAN_SUITE_IEEE 0x000fac00u
#define NL80211_CONNECT_SECONDS 20
#define WIFI_EAPOL_SECONDS 8
#define WIFI_EAPOL_HDR 99
#define PACKET_HOST 0

typedef struct
{
        b32 handle;
        p16 family;
        p32 mlme;
        p32 scan;
} nl80211;

/* One station interface: what a join asks of the radio it is on. */
typedef struct
{
        p32 index;
        p32 wiphy;
        bool has_mac;
        p8 mac[6];
        p8 name[IFNAME_SIZE];
} nl80211_card;

#define NL80211_CARDS 4

/* The wireless interface the machine joins by: the first station the kernel
   lists, or, with more than one, the one that has a station of its own, which
   is the one that is joined. */
typedef struct
{
        p32 index;
        p32 wiphy;
        bool found;
        bool has_mac;
        p8 mac[6];
        p8 name[IFNAME_SIZE];
        p8 cards;
        nl80211_card card[NL80211_CARDS];
        p8 others;
        p32 other[8];
} nl80211_iface;

typedef struct
{
        p32 wiphy;
        p32 seen;
        bool offload;
} nl80211_wiphy_query;

static COLD bipolar nl80211_disconnect(nl80211 address_to session, p32 index);

typedef struct
{
        p16 family;
        p32 mlme;
        p32 scan;
} nl80211_family_info;

static COLD bool nl80211_begin(netlink_buffer address_to buffer, p16 family, p8 command,
                          p16 flags, p32 sequence)
{
        p8 address_to body;

        if (!netlink_begin(buffer, family, flags, sequence, GENL_HEADER))
                return false;

        body = (p8 address_to)netlink_body(buffer);
        body[0] = command;
        body[1] = 1;
        body[2] = 0;
        body[3] = 0;
        return true;
}

typedef struct
{
        p16 type;
        const address_any data;
        positive length;
} nl80211_attr_spec;

static COLD bool nl80211_put_attrs(netlink_buffer address_to buffer,
                                   const nl80211_attr_spec address_to specs,
                                   positive count)
{
        for (positive i = 0; i < count; i++)
                if (!netlink_attribute_add(buffer, specs[i].type,
                                           (address_any)specs[i].data,
                                           specs[i].length))
                        return false;
        return true;
}

static COLD bool nl80211_attribute_u32(netlink_buffer address_to buffer, p16 type,
                                  p32 value)
{
        return netlink_attribute_add(buffer, type, address_of value, 4);
}

static COLD p32 nl80211_find_u32(netlink_header address_to header, p16 type, p32 missing)
{
        positive size = 0;
        p8 address_to at = (p8 address_to)netlink_find(header, GENL_HEADER, type,
                                                       address_of size);

        if (!at || size < 4)
                return missing;
        return memory_load_unaligned(p32, at);
}

static COLD p16 nl80211_find_u16(netlink_header address_to header, p16 type,
                            p16 missing)
{
        positive size = 0;
        p8 address_to at = (p8 address_to)netlink_find(header, GENL_HEADER, type,
                                                       address_of size);

        if (!at || size < 2)
                return missing;
        return (p16)memory_load_unaligned(p16, at);
}

static COLD bool nl80211_attr(netlink_header address_to header, p16 type)
{
        return netlink_find(header, GENL_HEADER, type, null) != null;
}

/* The command of a generic netlink message, 0 for one too short to hold the
   header; no message of the family's has command 0. */
static COLD p8 nl80211_command(netlink_header address_to header)
{
        return header->length < NETLINK_HEADER + GENL_HEADER
                   ? 0
                   : ((p8 address_to)header)[NETLINK_HEADER];
}

/* Whether a hardware address is one: all zeros is what none is. */
static COLD bool wifi_mac_set(p8 address_to mac)
{
        return memory_span_byte(mac, 0, 6) != 6;
}

/* The command of a message of nl80211's about this link, and 0 for any
   other: the news on the mlme and scan sockets is every interface's, and
   only this one's counts. */
static COLD p8 nl80211_news(nl80211 address_to session,
                            netlink_header address_to header, p32 index)
{
        return header->type == session->family &&
                       nl80211_find_u32(header, NL80211_ATTR_IFINDEX, 0) == index
                   ? nl80211_command(header)
                   : 0;
}

/* One multicast group of the family: its name and the id to join it by. */
static COLD bool nl80211_group_seen(p16 type, p8 address_to value, positive size,
                                    address_any context)
{
        nl80211_family_info address_to info = (nl80211_family_info address_to)context;
        positive name_length = 0;
        positive id_length = 0;
        p8 address_to name = (p8 address_to)netlink_find_span(
            value, size, CTRL_ATTR_MCAST_GRP_NAME, address_of name_length);
        p8 address_to id = (p8 address_to)netlink_find_span(
            value, size, CTRL_ATTR_MCAST_GRP_ID, address_of id_length);

        (void)type;
        if (!name || name_length < 5 || !id || id_length < 4)
                return true;
        if (!memory_compare(name, "mlme", 5))
                info->mlme = memory_load_unaligned(p32, id);
        else if (!memory_compare(name, "scan", 5))
                info->scan = memory_load_unaligned(p32, id);
        return true;
}

static COLD bool nl80211_family_seen(netlink_header address_to header,
                                address_any context)
{
        nl80211_family_info address_to info = (nl80211_family_info address_to)context;
        positive size = 0;
        positive groups_length = 0;
        p8 address_to at = (p8 address_to)netlink_find(header, GENL_HEADER,
                                                       CTRL_ATTR_FAMILY_ID,
                                                       address_of size);
        p8 address_to groups;

        if (at && size >= 2)
                info->family = memory_load_unaligned(p16, at);

        groups = (p8 address_to)netlink_find(header, GENL_HEADER,
                                             CTRL_ATTR_MCAST_GROUPS,
                                             address_of groups_length);
        if (groups)
                netlink_attributes(groups, groups_length, nl80211_group_seen, info);

        return false;
}

static COLD bipolar nl80211_open(nl80211 address_to session)
{
        netlink_buffer request = {0};
        p32 sequence;
        p8 name[] = "nl80211";
        bipolar handle;
        nl80211_family_info info = {0};

        memory_fill(session, 0, sizeof(*session));
        session->handle = -1;

        handle = netlink_open_protocol(NETLINK_GENERIC, 0);
        if (handle < 0)
                return handle;

        bool kept = false;
        scope_exit(if (!kept) socket_close((b32)handle));
        sequence = netlink_sequence_take();
        if (!nl80211_begin(address_of request, GENL_ID_CTRL, CTRL_CMD_GETFAMILY,
                           NLM_REQUEST | NLM_ACK, sequence))
                return -1;

        netlink_attribute_add(address_of request, CTRL_ATTR_FAMILY_NAME, name,
                              sizeof(name));

        if (netlink_transact((b32)handle, address_of request, sequence,
                             nl80211_family_seen, address_of info) < 0 ||
            !info.family)
                return -19;

        if (info.mlme &&
            socket_option_set((b32)handle, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP,
                              address_of info.mlme, sizeof(info.mlme)) >= 0)
                session->mlme = info.mlme;

        kept = true;
        session->handle = (b32)handle;
        session->family = info.family;
        session->scan = info.scan;
        return 0;
}

static COLD fn nl80211_close(nl80211 address_to session)
{
        if (session->handle >= 0)
                socket_close(session->handle);
        session->handle = -1;
}

static COLD bool nl80211_iface_seen(netlink_header address_to header,
                               address_any context)
{
        nl80211_iface address_to found = (nl80211_iface address_to)context;
        p8 command = nl80211_command(header);
        nl80211_card address_to card;
        p32 type;
        p32 index;
        positive length = 0;
        positive mac_length = 0;
        string_address name;
        p8 address_to mac;

        if (command != NL80211_CMD_NEW_INTERFACE &&
            command != NL80211_CMD_GET_INTERFACE)
                return true;

        /* A station, and only a station: an access point, a monitor or a
           P2P device on the same machine is not one to join from, and the
           first of those in the dump used to be taken when no station
           followed it. The rest that no lease is for are counted too. */
        type = nl80211_find_u32(header, NL80211_ATTR_IFTYPE, 0);
        index = nl80211_find_u32(header, NL80211_ATTR_IFINDEX, 0);
        if (index && type != NL80211_IFTYPE_STATION && type != NL80211_IFTYPE_ADHOC &&
            type != NL80211_IFTYPE_P2P_CLIENT && found->others < 8)
                found->other[found->others++] = index;
        if (type != NL80211_IFTYPE_STATION || !index || found->cards == NL80211_CARDS)
                return true;

        card = found->card + found->cards++;
        card->index = index;
        card->wiphy = nl80211_find_u32(header, NL80211_ATTR_WIPHY, 0);
        name = (string_address)netlink_find(header, GENL_HEADER, NL80211_ATTR_IFNAME,
                                            address_of length);
        if (name && length)
        {
                if (length >= IFNAME_SIZE)
                        length = IFNAME_SIZE - 1;
                memory_copy(card->name, name, length);
                card->name[length] = end;
        }
        mac = (p8 address_to)netlink_find(header, GENL_HEADER, NL80211_ATTR_MAC,
                                          address_of mac_length);
        if (mac && mac_length >= 6)
        {
                memory_copy(card->mac, mac, 6);
                card->has_mac = true;
        }

        return true;
}

static COLD bipolar nl80211_station(nl80211 address_to session, p32 index,
                                    p8 address_to mac);

static COLD bipolar nl80211_interface(nl80211 address_to session,
                                 nl80211_iface address_to found)
{
        netlink_buffer request = {0};
        p32 sequence = netlink_sequence_take();
        positive chosen = 0;
        p8 mac[6];

        memory_fill(found, 0, sizeof(*found));
        if (!nl80211_begin(address_of request, session->family,
                           NL80211_CMD_GET_INTERFACE,
                           NLM_REQUEST | NLM_DUMP, sequence))
                return -1;

        if (netlink_transact(session->handle, address_of request, sequence,
                             nl80211_iface_seen, found) < 0)
                return -1;
        if (!found->cards)
                return -19;

        //      With several radios the one that is joined is the one the
        //      machine is on, whichever the kernel listed first: a pass that
        //      looked at the first alone joined a second time over a link
        //      that stood on the other.
        for (positive at = 0; at < found->cards && found->cards > 1; at++)
                if (nl80211_station(session, found->card[at].index, mac) > 0)
                {
                        chosen = at;
                        break;
                }
        found->found = true;
        found->index = found->card[chosen].index;
        found->wiphy = found->card[chosen].wiphy;
        found->has_mac = found->card[chosen].has_mac;
        memory_copy(found->mac, found->card[chosen].mac, sizeof(found->mac));
        memory_copy(found->name, found->card[chosen].name, sizeof(found->name));
        return 0;
}

/* The wifi links the watcher takes no lease on: access points, monitors,
   anything that is not a station. */
static COLD fn radio_links_unleased(netlink_search address_to search)
{
        nl80211 session;
        nl80211_iface iface;

        if (nl80211_open(address_of session) < 0)
                return;
        nl80211_interface(address_of session, address_of iface);
        memory_copy(search->skip, iface.other, sizeof(search->skip));
        search->skip_count = iface.others;
        nl80211_close(address_of session);
}

/* Whether a password is one WPA takes: eight to sixty-three characters, or
   sixty-four hexadecimal digits, which are the key itself. */
static COLD bool wifi_pass_fits(p8 address_to pass, positive length)
{
        if (length != 64)
                return length >= 8 && length <= 63;
        return !array_any(at, length, digit_known(pass[at], 16) >= 16);
}

static COLD bool wifi_psk(p8 address_to ssid, positive ssid_length,
                     p8 address_to pass, positive pass_length, p8 address_to pmk)
{
        if (!wifi_pass_fits(pass, pass_length))
                return false;
        if (pass_length == 64)
                return memory_from_hex(pmk, (string_address)pass, 32);

        if (!ssid_length || ssid_length > 32)
                return false;

        //      IEEE 802.11i: PBKDF2-HMAC-SHA1 of the passphrase, the SSID as
        //      its salt, 4096 rounds, 256 bits.
        crypto_pbkdf2(DIGEST_SHA1, 20, pass, pass_length, ssid, ssid_length,
                      4096, pmk, 32);
        return true;
}

/*
        The PTK of a handshake: the PMK over the two addresses and the two
        nonces, each pair smaller first. PSK (suite 2) takes 802.11i's
        PRF-512 of HMAC-SHA1, all 64 bytes of it; PSK-SHA256 (6) and SAE (8)
        take the KDF of 802.11-2016 12.7.1.7.2, HMAC-SHA-256 of a counter
        from 1, the label, the data and the length in bits, 384 bits of it.
        Either way the KCK, the KEK and the TK are its first three sixteen
        bytes.
*/
static COLD fn wifi_ptk(p8 address_to pmk, p8 address_to ap, p8 address_to sta,
                   p8 address_to anonce, p8 address_to snonce, p8 address_to ptk,
                   p8 akm)
{
        p8 label[] = "Pairwise key expansion";
        p8 data[6 + 6 + 32 + 32];
        p8 input[22 + 1 + 76 + 1];
        p8 hash[20];
        p8 address_to min_mac;
        p8 address_to max_mac;
        p8 address_to min_nonce;
        p8 address_to max_nonce;
        positive used = 0;
        positive which;

        if (memory_compare(ap, sta, 6) < 0)
        {
                min_mac = ap;
                max_mac = sta;
        }
        else
        {
                min_mac = sta;
                max_mac = ap;
        }
        if (memory_compare(anonce, snonce, 32) < 0)
        {
                min_nonce = anonce;
                max_nonce = snonce;
        }
        else
        {
                min_nonce = snonce;
                max_nonce = anonce;
        }

        memory_copy(data, min_mac, 6);
        memory_copy(data + 6, max_mac, 6);
        memory_copy(data + 12, min_nonce, 32);
        memory_copy(data + 44, max_nonce, 32);
        if (akm != 2)
        {
                p8 block[2 + 22 + 76 + 2];
                p8 sum[32];

                memory_fill(ptk, 0, 64);
                for (which = 1; used < 48; which++)
                {
                        block[0] = (p8)which;
                        block[1] = 0;
                        memory_copy(block + 2, label, 22);
                        memory_copy(block + 24, data, 76);
                        //      384, little endian.
                        block[100] = 0x80;
                        block[101] = 0x01;
                        crypto_hmac_sha256(pmk, 32, block, sizeof(block), sum);
                        memory_copy(ptk + used, sum, used + 32 > 48 ? 48 - used : 32);
                        used += 32;
                }
                crypto_forget(block, sizeof(block));
                crypto_forget(sum, sizeof(sum));
                crypto_forget(data, sizeof(data));
                return;
        }
        memory_copy(input, label, 22);
        input[22] = 0;
        memory_copy(input + 23, data, 76);
        for (which = 0; used < 64; which++)
        {
                input[99] = (p8)which;
                crypto_hmac(DIGEST_SHA1, 20, pmk, 32, input, 100, hash);
                memory_copy(ptk + used, hash, used + 20 > 64 ? 64 - used : 20);
                used += 20;
        }
        crypto_forget(data, sizeof(data));
        crypto_forget(input, sizeof(input));
        crypto_forget(hash, sizeof(hash));
}

/* The key descriptor version an access point's key frames carry for a key
   management suite: 2 (HMAC-SHA1-128) for PSK, 3 (AES-128-CMAC) for
   PSK-SHA256, and 0, "as the suite defines", for SAE, whose MIC is the CMAC
   all the same. */
static COLD p8 wifi_key_version(p8 akm)
{
        return akm == 2 ? 2 : akm == 8 ? 0 : 3;
}

/*
        What an access point's RSN element offers, as far as a join cares:
        its key management suites, management frame protection, the group
        cipher and the pairwise set, each a mask, and whether its RSN
        extension element takes SAE's hash-to-element. The kernel keeps two
        frames of an access point, its latest (a probe response anybody can
        send in its name) and its last beacon, and a join takes what either
        offers: a forged frame that takes an offer away -- protection, WPA3,
        hash-to-element -- would have the join ask for less, while one that
        adds an offer costs a join that a forger can stop anyway. Message 3
        of the handshake is then held to the elements read here
        (wifi_eapol_step).
*/
#define WIFI_AKM_PSK 1
#define WIFI_AKM_PSK_SHA256 2
#define WIFI_AKM_SAE 4
#define WIFI_AKM_OTHER 8
#define WIFI_PMF_CAPABLE 1
#define WIFI_PMF_REQUIRED 2
#define WIFI_CIPHER_CCMP 1
#define WIFI_CIPHER_TKIP 2
#define WIFI_CIPHER_OTHER 4

typedef struct
{
        p8 akms;
        p8 pmf;
        p8 group;
        p8 pairwise;
        p8 h2e;
} wifi_rsn_caps;

static COLD fn wifi_caps_merge(wifi_rsn_caps address_to into, const wifi_rsn_caps address_to from)
{
        into->akms |= from->akms;
        into->pmf |= from->pmf;
        into->group |= from->group;
        into->pairwise |= from->pairwise;
        into->h2e |= from->h2e;
}

/*
        What a saved network has taught the join, or its owner has said of
        it (radio_rules): an access point by its name offered management
        frame protection; it was joined with WPA3; its owner lets the tight
        tier join it with WPA2.
*/
#define WIFI_RULE_PMF 1
#define WIFI_RULE_SAE 2
#define WIFI_RULE_WPA2 4

/* A join's refusals of its own, which it makes having chosen an access
   point and before it sends anything, and the one message 3 can bring:
   below every errno and every -1000-R reason a join reports. */
#define WIFI_REFUSE_TKIP (-70001)
#define WIFI_REFUSE_CIPHER (-70002)
#define WIFI_REFUSE_PMF_GONE (-70003)
#define WIFI_REFUSE_SAE_GONE (-70004)
#define WIFI_REFUSE_NO_PMF (-70005)
#define WIFI_REFUSE_WPA2 (-70006)
#define WIFI_REFUSE_KEY_ONLY (-70007)
#define WIFI_REFUSE_SUITE (-70008)
#define WIFI_REFUSE_SAE_BUDGET (-70009)
#define WIFI_REFUSE_ELEMENTS (-70010)
#define WIFI_REFUSE_FIRST WIFI_REFUSE_ELEMENTS
#define WIFI_REFUSE_LAST WIFI_REFUSE_TKIP
//      The machine's doing, not the network's: no station could be confined
//      here, so nothing was joined; or the station ended without a status.
#define WIFI_REFUSE_CONFINE (-70101)
#define WIFI_REFUSE_APART (-70102)
//      And the broker could not start: the keeper's lock, its channel or the
//      station's fork failed, so nothing was asked of the air (radio_broker).
#define WIFI_REFUSE_UNSTARTED (-70103)

/* What a join asks of an access point: the key management suite (2, 6 or
   8), the protection it asks for, and the PSK suite it may try when WPA3
   came to nothing for a reason that is not the password, or 0. */
typedef struct
{
        p8 akm;
        p8 pmf;
        p8 fallback;
} wifi_plan;

/*
        The join's choice for an access point that offers caps, at a tier,
        for a saved network with rules, whose secret is a passphrase or a
        key of 64 hex digits (key_only, which is a PMK and no SAE password).
        0 with the plan, or the refusal.

          ciphers   CCMP for the group and among the pairwise ones, or
                    nothing: TKIP (WPA1, and the group cipher of a mixed
                    WPA/WPA2 network) is broken
          memory    an access point of a name that once offered protection
                    is never joined without it, and one joined with WPA3
                    is never joined with WPA2: a twin that offers less is
                    what a forged deauthentication, or a password guessed
                    from a captured handshake, needs
          WPA3      whenever it is offered and there is a password for it,
                    with protection required, and WPA2 after it only when
                    that is offered too, the memory allows it and the tier
                    does
          WPA2      PSK-SHA256 when it is offered with protection, else
                    PSK; protection asked for whenever it is offered, as
                    required, since the kernel takes no optional form from a
                    driver with no connect of its own
          tight     protection always, and WPA2 only for a network whose
                    owner said so (WIFI_RULE_WPA2)
*/
static COLD bipolar wifi_join_plan(positive tier, const wifi_rsn_caps address_to caps,
                                   p8 rules, bool key_only, wifi_plan address_to plan)
{
        bool tight = tier >= STRICT_TIGHT;
        bool pmf = caps->pmf != 0;
        bool sae = (caps->akms & WIFI_AKM_SAE) && !key_only;
        p8 psk = (caps->akms & WIFI_AKM_PSK_SHA256) && (pmf || !(caps->akms & WIFI_AKM_PSK))
                     ? 6
                 : caps->akms & WIFI_AKM_PSK ? 2
                                             : 0;

        memory_fill(plan, 0, sizeof(*plan));
        if (!(caps->group & WIFI_CIPHER_CCMP) || (caps->group & ~WIFI_CIPHER_CCMP) ||
            !(caps->pairwise & WIFI_CIPHER_CCMP))
                return (caps->group | caps->pairwise) & WIFI_CIPHER_TKIP ? WIFI_REFUSE_TKIP
                                                                         : WIFI_REFUSE_CIPHER;
        if (!sae && !psk)
                return caps->akms & WIFI_AKM_SAE ? WIFI_REFUSE_KEY_ONLY : WIFI_REFUSE_SUITE;
        if ((rules & WIFI_RULE_PMF) && !pmf)
                return WIFI_REFUSE_PMF_GONE;
        if ((rules & WIFI_RULE_SAE) && !sae)
                return caps->akms & WIFI_AKM_SAE ? WIFI_REFUSE_KEY_ONLY : WIFI_REFUSE_SAE_GONE;
        if (sae)
        {
                plan->akm = 8;
                plan->pmf = WIFI_PMF_CAPABLE | WIFI_PMF_REQUIRED;
                if (psk && !(rules & WIFI_RULE_SAE) &&
                    (!tight || (pmf && (rules & WIFI_RULE_WPA2))))
                        plan->fallback = psk;
                return 0;
        }
        if (tight && !pmf)
                return WIFI_REFUSE_NO_PMF;
        if (tight && !(rules & WIFI_RULE_WPA2))
                return WIFI_REFUSE_WPA2;
        plan->akm = psk;
        plan->pmf = pmf ? WIFI_PMF_CAPABLE | WIFI_PMF_REQUIRED : 0;
        return 0;
}

/* The RSN element a station offers, in the association request and again
   in message 2, which the access point compares with it: CCMP for both
   ciphers and the one key management suite asked for; with protection,
   the capable and required bits, the PMKID when there is one (SAE's) and
   BIP-CMAC-128 as the group management cipher. With no protection it is
   the element the join always sent. */
static COLD positive wifi_rsn_build(p8 address_to out, p8 akm, p8 pmf, const p8 address_to pmkid)
{
        static const p8 head[] = {0x30, 0x14, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04,
                                  0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00,
                                  0x00, 0x0f, 0xac};
        positive at = sizeof(head);

        memory_copy(out, head, sizeof(head));
        out[at++] = akm;
        out[at++] = (p8)((pmf & WIFI_PMF_CAPABLE ? 0x80 : 0) | (pmf & WIFI_PMF_REQUIRED ? 0x40 : 0));
        out[at++] = 0;
        if (!(pmf & WIFI_PMF_CAPABLE))
                return at;
        out[at++] = pmkid ? 1 : 0;
        out[at++] = 0;
        if (pmkid)
        {
                memory_copy(out + at, pmkid, 16);
                at += 16;
        }
        out[at++] = 0x00;
        out[at++] = 0x0f;
        out[at++] = 0xac;
        out[at++] = 0x06;
        out[1] = (p8)(at - 2);
        return at;
}

/*
        The access point's RSN element and RSN extension element as the
        frames a join chose it from showed them, whole, element id and
        length first: [0] its latest frame's, [1] its last beacon's where
        the kernel kept that apart. A handshake's message 3 carries the
        access point's own, under the MIC of a key only it and this station
        have: one that matches neither is not what the join was decided
        from, which a forged beacon or probe response that took an offer
        away makes, and it is refused (802.11-2020 12.7.6.4, as
        wpa_supplicant refuses it).
*/
#define WIFI_ELEMENT_ROOM (2 + 255)

typedef struct
{
        p8 rsn[2][WIFI_ELEMENT_ROOM];
        p8 rsnx[2][WIFI_ELEMENT_ROOM];
        p16 rsn_length[2];
        p16 rsnx_length[2];
} wifi_seen;

/* Whether an element of message 3 is one of the two seen (none, when
   neither frame had one). */
static COLD bool wifi_seen_one(p8 (address_to seen)[WIFI_ELEMENT_ROOM],
                               const p16 address_to lengths, const p8 address_to got,
                               positive length)
{
        if (!lengths[0] && !lengths[1])
                return !length;
        for (positive at = 0; at < 2; at++)
                if (lengths[at] && lengths[at] == length && !memory_compare(seen[at], got, length))
                        return true;
        return false;
}

/* Whether message 3's RSN element is one the access point showed, and its
   RSN extension element too, or none where neither frame had one: an
   extension element stripped from what the station saw would have it hunt
   for the password element where the access point takes hash-to-element.
   A join that saw no RSN element at all (no scan could be had) has nothing
   to hold it to. */
static COLD bool wifi_seen_matches(wifi_seen address_to seen, const p8 address_to rsn,
                                   positive rsn_length, const p8 address_to rsnx,
                                   positive rsnx_length)
{
        if (!seen->rsn_length[0] && !seen->rsn_length[1])
                return true;
        return wifi_seen_one(seen->rsn, seen->rsn_length, rsn, rsn_length) &&
               wifi_seen_one(seen->rsnx, seen->rsnx_length, rsnx, rsnx_length);
}

static COLD bool nl80211_ext_bit(p8 address_to bits, positive length, positive which)
{
        return which / 8 < length && (bits[which / 8] & (1u << (which % 8)));
}

static COLD bool nl80211_wiphy_seen(netlink_header address_to header, address_any context)
{
        nl80211_wiphy_query address_to query = (nl80211_wiphy_query address_to)context;
        p8 command = nl80211_command(header);
        positive size = 0;
        p8 address_to bits;
        p32 wiphy;

        if (command != NL80211_CMD_NEW_WIPHY && command != NL80211_CMD_GET_WIPHY)
                return true;

        wiphy = nl80211_find_u32(header, NL80211_ATTR_WIPHY, query->seen);
        if (nl80211_attr(header, NL80211_ATTR_WIPHY))
                query->seen = wiphy;
        if (wiphy != query->wiphy)
                return true;

        bits = (p8 address_to)netlink_find(
            header, GENL_HEADER, NL80211_ATTR_EXT_FEATURES, address_of size);
        if (bits && nl80211_ext_bit(bits, size,
                                    NL80211_EXT_FEATURE_4WAY_HANDSHAKE_STA_PSK))
                query->offload = true;
        return true;
}

static COLD bool nl80211_psk_offload(nl80211 address_to session, p32 wiphy)
{
        netlink_buffer request = {0};
        p32 sequence;
        nl80211_wiphy_query query = {.wiphy = wiphy, .seen = ~0u};

        sequence = netlink_sequence_take();
        if (!nl80211_begin(address_of request, session->family,
                           NL80211_CMD_GET_WIPHY, NLM_REQUEST | NLM_DUMP,
                           sequence))
                return false;
        //      A dump that is not split is the wiphy as the first nl80211 told
        //      it, and has none of its extended features: iw asks for split
        //      too, and a driver's offload was never heard of without it.
        const nl80211_attr_spec attrs[] = {
            {NL80211_ATTR_WIPHY, address_of wiphy, sizeof(wiphy)},
            {NL80211_ATTR_SPLIT_WIPHY_DUMP, null, 0},
        };
        if (!nl80211_put_attrs(address_of request, attrs, array_count(attrs)))
                return false;
        if (netlink_transact(session->handle, address_of request, sequence,
                             nl80211_wiphy_seen, address_of query) < 0)
                return false;
        return query.offload;
}

static COLD bool nl80211_station_seen(netlink_header address_to header,
                                 address_any context)
{
        p8 command = nl80211_command(header);
        p8 address_to into = (p8 address_to)context;
        positive length = 0;
        p8 address_to mac;

        if (command != NL80211_CMD_NEW_STATION &&
            command != NL80211_CMD_GET_STATION)
                return true;
        mac = (p8 address_to)netlink_find(header, GENL_HEADER, NL80211_ATTR_MAC,
                                          address_of length);
        if (mac && length >= 6)
        {
                memory_copy(into, mac, 6);
                return false;
        }
        return true;
}

/* Whether the link has a station, and its address in mac: 1 when it has one,
   0 when the kernel was asked and has none, -1 when the asking failed, which
   says nothing of the link. */
static COLD bipolar nl80211_station(nl80211 address_to session, p32 index,
                                    p8 address_to mac)
{
        netlink_buffer request = {0};
        p32 sequence = netlink_sequence_take();

        memory_fill(mac, 0, 6);
        if (!nl80211_begin(address_of request, session->family,
                           NL80211_CMD_GET_STATION, NLM_REQUEST | NLM_DUMP,
                           sequence))
                return -1;
        nl80211_attribute_u32(address_of request, NL80211_ATTR_IFINDEX, index);
        if (netlink_transact(session->handle, address_of request, sequence,
                             nl80211_station_seen, mac) < 0)
                return -1;
        return wifi_mac_set(mac) ? 1 : 0;
}

static COLD fn nl80211_forget_secret(netlink_buffer address_to buffer)
{
        if (buffer->bytes && buffer->used)
                crypto_forget(buffer->bytes, buffer->used);
        netlink_forget(buffer);
}

static COLD bipolar nl80211_new_key(nl80211 address_to session, p32 index, p8 idx,
                               p32 type, p8 address_to mac, p8 address_to key,
                               positive key_length, p8 address_to seq,
                               positive seq_length, bool group_default, p32 cipher)
{
        netlink_buffer request = {0};
        p32 sequence = netlink_sequence_take();

        scope_exit(nl80211_forget_secret(address_of request));
        if (!nl80211_begin(address_of request, session->family, NL80211_CMD_NEW_KEY,
                           NLM_REQUEST | NLM_ACK, sequence))
                return -1;
        const nl80211_attr_spec base_attrs[] = {
            {NL80211_ATTR_IFINDEX, address_of index, sizeof(index)},
            {NL80211_ATTR_KEY_DATA, key, key_length},
            {NL80211_ATTR_KEY_IDX, address_of idx, 1},
            {NL80211_ATTR_KEY_CIPHER, address_of cipher, sizeof(cipher)},
            {NL80211_ATTR_KEY_TYPE, address_of type, sizeof(type)},
        };
        if (!nl80211_put_attrs(address_of request, base_attrs, array_count(base_attrs)))
                return -1;
        if (mac && !netlink_attribute_add(address_of request, NL80211_ATTR_MAC, mac, 6))
                return -1;
        if (seq && seq_length &&
            !netlink_attribute_add(address_of request, NL80211_ATTR_KEY_SEQ, seq, seq_length))
                return -1;
        if (group_default &&
            !netlink_attribute_add(address_of request, NL80211_ATTR_KEY_DEFAULT, null, 0))
                return -1;
        return netlink_transact(session->handle, address_of request, sequence,
                                null, null);
}

static COLD bipolar nl80211_authorize(nl80211 address_to session, p32 index,
                                 p8 address_to mac)
{
        netlink_buffer request = {0};
        p32 sequence = netlink_sequence_take();
        p32 flags[2];

        flags[0] = (p32)1 << NL80211_STA_FLAG_AUTHORIZED;
        flags[1] = flags[0];
        if (!nl80211_begin(address_of request, session->family,
                           NL80211_CMD_SET_STATION, NLM_REQUEST | NLM_ACK,
                           sequence))
                return -1;
        const nl80211_attr_spec attrs[] = {
            {NL80211_ATTR_IFINDEX, address_of index, sizeof(index)},
            {NL80211_ATTR_MAC, mac, 6},
            {NL80211_ATTR_STA_FLAGS2, flags, sizeof(flags)},
        };
        if (!nl80211_put_attrs(address_of request, attrs, array_count(attrs)))
                return -1;
        return netlink_transact(session->handle, address_of request, sequence,
                                null, null);
}

/*
        The joined link's keys, and one EAPOL-Key state machine that serves
        both the join's four-way handshake and every rekey the access point
        starts after it: a new four-way handshake for the pairwise key, or
        the two-message group key handshake. Nothing answered those once the
        join returned, so the access point gave up and sent the machine away
        with reason 15 or 16 -- on one network, forty minutes in.
*/
#define WIFI_KEY_PAIRWISE 0x0008
#define WIFI_KEY_INSTALL 0x0040
#define WIFI_KEY_ACK 0x0080
#define WIFI_KEY_MIC 0x0100
#define WIFI_KEY_SECURE 0x0200
#define WIFI_KEY_REQUEST 0x0800
#define WIFI_KEY_ENCRYPTED 0x1000
#define WIFI_KEEP_CHECK_SECONDS 30

typedef struct
{
        nl80211 session;
        p32 index;
        b32 eapol;
        p8 sta[6];
        p8 bssid[6];
        p8 pmk[32];
        p8 snonce[32];
        p8 anonce[32];
        p8 ptk[64];
        p8 pending[64];
        p8 gtk[4][16];
        p8 gtk_installed; // one bit per driver slot; zero bytes are a valid key
        p8 igtk[2][16];
        p8 igtk_installed; // bit 0 key id 4, bit 1 key id 5, as gtk_installed
        p8 replay[8];
        p8 answered;
        bool replay_set;
        bool installed;
        bool pending_set;
        bool nonce_used;
        //      What was asked of the access point: the suite (2, 6 or 8), the
        //      key descriptor version its frames carry, protection, and the
        //      RSN element (with the RSN extension element after it for
        //      hash-to-element) that the association request and message 2
        //      carry.
        p8 akm;
        p8 version;
        p8 pmf;
        p8 rsn[48];
        p8 rsn_length;
        wifi_seen seen;
        //      The access point's Transition Disable bits from an
        //      authenticated message 3 (WIFI_TRANSITION_*).
        p8 transition_disable;
} wifi_link;

static COLD fn wifi_link_close(wifi_link address_to link)
{
        if (link->eapol >= 0)
                socket_close(link->eapol);
        nl80211_close(address_of link->session);
        crypto_forget(link, sizeof(*link));
        link->eapol = -1;
        link->session.handle = -1;
}

/* A well-formed RSN EAPOL-Key frame's length, taken from its own body
   length and never past what arrived; 0 for anything else. */
static COLD positive wifi_eapol_length(p8 address_to frame, positive got)
{
        positive length;

        if (got < WIFI_EAPOL_HDR || frame[1] != 3 || frame[4] != 2)
                return 0;
        length = 4 + (positive)network_load_16(frame + 2);
        if (length < WIFI_EAPOL_HDR || length > got ||
            network_load_16(frame + 97) > length - WIFI_EAPOL_HDR)
                return 0;
        return length;
}

/* The frame's MIC with its MIC field zero, HMAC-SHA1-128 for PSK and
   AES-128-CMAC for PSK-SHA256 and SAE: written into the frame, or compared
   in constant time with the MIC it carries, which is left in place so that
   another key can be tried. */
static COLD bool wifi_eapol_mic(const wifi_link address_to link, p8 address_to kck,
                                p8 address_to frame, positive length, bool check)
{
        p8 hash[20];
        p8 carried[16];
        bool same;

        memory_copy(carried, frame + 81, 16);
        memory_fill(frame + 81, 0, 16);
        if (link->akm == 2)
                crypto_hmac(DIGEST_SHA1, 20, kck, 16, frame, length, hash);
        else
                crypto_aes_cmac(kck, frame, length, hash);
        same = crypto_same(carried, hash, 16);
        memory_copy(frame + 81, check ? carried : hash, 16);
        crypto_forget(hash, sizeof(hash));
        return same;
}

/* A key frame to the access point: its key information with the link's
   descriptor version, the replay counter it answers, the SNonce and RSN
   element for message 2, and its MIC under kck. The key length is 0, as
   wpa_supplicant sends for RSN. */
static COLD bipolar wifi_eapol_reply(wifi_link address_to link, p8 address_to kck,
                                     p16 info, p8 address_to replay, bool nonce)
{
        p8 frame[WIFI_EAPOL_HDR + sizeof(link->rsn)];
        positive length = WIFI_EAPOL_HDR + (nonce ? link->rsn_length : 0);
        socket_address_packet to = {.family = AF_PACKET,
                                    .protocol = network_order_16(ETH_P_PAE),
                                    .index = link->index,
                                    .halen = 6};
        bipolar sent;

        memory_fill(frame, 0, sizeof(frame));
        frame[0] = 1;
        frame[1] = 3;
        network_store_16(frame + 2, (p16)(length - 4));
        frame[4] = 2;
        network_store_16(frame + 5, (p16)((info & ~7) | link->version));
        memory_copy(frame + 9, replay, 8);
        if (nonce)
        {
                memory_copy(frame + 17, link->snonce, 32);
                network_store_16(frame + 97, link->rsn_length);
                memory_copy(frame + 99, link->rsn, link->rsn_length);
        }
        wifi_eapol_mic(link, kck, frame, length, false);
        memory_copy(to.addr, link->bssid, 6);
        sent = socket_send(link->eapol, frame, length, 0, address_of to, sizeof(to));
        crypto_forget(frame, sizeof(frame));
        return sent == (bipolar)length ? 0 : -1;
}

/* What key data holds that a station takes: the first GTK and its id, the
   first IGTK (key id 4 or 5, its packet number then the key), the access
   point's own RSN element and RSN extension element, whole, and the
   Transition Disable bitmap (IEEE 802.11-2020 12.7.2: KDE 00-0F-AC:32;
   WPA3 specification 13.5), whose first bit says this network is never to
   be joined with WPA2 again. */
#define WIFI_TRANSITION_WPA3_PERSONAL 1

typedef struct
{
        p8 gtk[16];
        p8 gtk_id;
        p8 igtk[6 + 16];
        p8 igtk_id;
        p8 transition_disable;
        p8 rsn[WIFI_ELEMENT_ROOM];
        p8 rsnx[WIFI_ELEMENT_ROOM];
        p16 rsn_length;
        p16 rsnx_length;
} wifi_key_data;

/* Key data wrapped under the KEK, into what: 1 with a GTK, 0 when the data
   unwraps and holds none, -1 when it does not unwrap. Each kind is the
   first of it; a GTK or IGTK element of any other length is no key. */
static COLD bipolar wifi_gtk_take(p8 address_to kek, p8 address_to data, positive length,
                                  wifi_key_data address_to what)
{
        p8 plain[CRYPTO_WRAP_MOST];
        positive size = 0;
        bipolar found = 0;
        byte_reader reader;

        memory_fill(what, 0, sizeof(*what));
        if (!crypto_aes_key_unwrap(kek, data, length, plain, address_of size))
                return -1;
        reader = byte_reader_open(plain, size);
        while (byte_reader_left(&reader))
        {
                byte_reader whole = reader;
                p8 kind = byte_reader_u8(&reader);
                byte_reader element = byte_reader_vector8(&reader);
                positive span = byte_reader_left(&element);
                p32 type;

                if (!byte_reader_ok(&reader))
                        break;
                //      The access point's elements, as they are, to be held
                //      to what its beacon said.
                if ((kind == 0x30 && !what->rsn_length) ||
                    (kind == 0xf4 && !what->rsnx_length))
                {
                        p8 address_to into = kind == 0x30 ? what->rsn : what->rsnx;

                        memory_copy(into, byte_reader_take(&whole, 2 + span), 2 + span);
                        if (kind == 0x30)
                                what->rsn_length = (p16)(2 + span);
                        else
                                what->rsnx_length = (p16)(2 + span);
                        continue;
                }
                if (kind != 0xdd || span < 4)
                        continue;
                //      The OUI and data type, read once: each test below is of
                //      the same four bytes.
                type = (p32)byte_reader_uint(&element, 4, true);
                //      The GTK KDE: 00-0F-AC:1, the key id, a reserved
                //      byte, then a CCMP key of sixteen bytes.
                if (span == 4 + 2 + 16 && type == 0x000fac01 && !found)
                {
                        p8 key_id = byte_reader_u8(&element);

                        (void)byte_reader_u8(&element);
                        what->gtk_id = (p8)(key_id & 3);
                        memory_copy(what->gtk, byte_reader_take(&element, 16), 16);
                        found = 1;
                }
                //      Transition Disable: 00-0F-AC:32 and its bitmap.
                else if (span >= 4 + 1 && type == 0x000fac20)
                        what->transition_disable |= byte_reader_u8(&element);
                //      The IGTK KDE: 00-0F-AC:9, the key id (4 or 5, two
                //      bytes little endian), the six byte packet number the
                //      key starts at, and a BIP-CMAC-128 key of sixteen bytes.
                else if (span == 4 + 2 + 6 + 16 && type == 0x000fac09 && !what->igtk_id)
                {
                        p16 key_id = (p16)byte_reader_uint(&element, 2, false);

                        if (key_id == 4 || key_id == 5)
                        {
                                memory_copy(what->igtk, byte_reader_take(&element, 22), 22);
                                what->igtk_id = (p8)key_id;
                        }
                }
        }
        crypto_forget(plain, sizeof(plain));
        return found;
}

/* The replay counter and keys for a driver that answers group rekeys by
   itself while the machine sleeps. Most drivers do not, and nothing here
   depends on it: the keeper answers every rekey it is awake for. */
static COLD fn wifi_rekey_offload(wifi_link address_to link)
{
        netlink_buffer request = {0};
        p32 sequence = netlink_sequence_take();
        positive nest;
        const nl80211_attr_spec rekey[] = {
            {NL80211_REKEY_DATA_KEK, link->ptk + 16, 16},
            {NL80211_REKEY_DATA_KCK, link->ptk, 16},
            {NL80211_REKEY_DATA_REPLAY_CTR, link->replay, 8},
        };

        scope_exit(nl80211_forget_secret(address_of request));
        if (!nl80211_begin(address_of request, link->session.family,
                           NL80211_CMD_SET_REKEY_OFFLOAD, NLM_REQUEST | NLM_ACK, sequence))
                return;
        nl80211_attribute_u32(address_of request, NL80211_ATTR_IFINDEX, link->index);
        nest = netlink_nested_begin(address_of request, NL80211_ATTR_REKEY_DATA);
        nl80211_put_attrs(address_of request, rekey, array_count(rekey));
        netlink_nested_end(address_of request, nest);
        if (!request.failed)
                netlink_transact(link->session.handle, address_of request, sequence, null, null);
}

/*
        One EAPOL-Key frame from the access point: 1 when a four-way
        handshake completes, 2 when a group key handshake does, 0 for a
        frame answered or ignored, negative when a key would not go in.

        These frames arrive unencrypted from anyone in range, even with the
        keys in. So nothing is taken from a frame whose replay counter is not
        past the last one whose MIC checked. Message 1 has no MIC and only
        makes a pending PTK, which replaces the installed one once a message
        3 carrying the same ANonce checks under it. A key already in is not
        put in again when a message is resent: that reinstallation is the
        one KRACK used to reset the nonces. The group management key of a
        link with protection is held the same way. A fresh message 3 whose
        RSN elements are not what the access point showed when it was
        chosen is a downgrade, and is neither answered nor keyed:
        WIFI_REFUSE_ELEMENTS.
*/
static COLD bipolar wifi_eapol_step(wifi_link address_to link, p8 address_to frame,
                                    positive got)
{
        positive length = wifi_eapol_length(frame, got);
        p16 info = length ? network_load_16(frame + 5) : 0;
        bool pairwise = (info & WIFI_KEY_PAIRWISE) != 0;
        bool fresh = false;
        p8 address_to kck = null;
        wifi_key_data what;
        p8 idx;
        bipolar found;
        bipolar failed = 0;

        if (!length || (info & 7) != link->version || !(info & WIFI_KEY_ACK) ||
            (info & WIFI_KEY_REQUEST) ||
            (link->replay_set && memory_compare(frame + 9, link->replay, 8) <= 0))
                return 0;

        if (!(info & WIFI_KEY_MIC))
        {
                if (!pairwise)
                        return 0;
                if (!link->nonce_used &&
                    system_random_fill(link->snonce, sizeof(link->snonce), 0) < 0)
                        return -1;
                link->nonce_used = true;
                memory_copy(link->anonce, frame + 17, 32);
                wifi_ptk(link->pmk, link->bssid, link->sta, link->anonce, link->snonce,
                         link->pending, link->akm);
                link->pending_set = true;
                if (link->answered < 255)
                        link->answered++;
                wifi_eapol_reply(link, link->pending, 0x010a, frame + 9, true);
                return 0;
        }

        if (!(info & WIFI_KEY_ENCRYPTED))
                return 0;
        if (!pairwise)
        {
                if (link->installed && (info & WIFI_KEY_SECURE) &&
                    wifi_eapol_mic(link, link->ptk, frame, length, true))
                        kck = link->ptk;
        }
        else if (info & WIFI_KEY_INSTALL)
        {
                //      The pending key for a new handshake's message 3; the
                //      installed one for a message 3 resent after it.
                fresh = link->pending_set && !memory_compare(frame + 17, link->anonce, 32) &&
                        wifi_eapol_mic(link, link->pending, frame, length, true);
                if (fresh)
                        kck = link->pending;
                else if (link->installed && wifi_eapol_mic(link, link->ptk, frame, length, true))
                        kck = link->ptk;
        }
        if (!kck)
                return 0;

        found = wifi_gtk_take(kck + 16, frame + 99, network_load_16(frame + 97),
                              address_of what);
        idx = what.gtk_id;
        if (found < 0 || (!pairwise && !found))
        {
                crypto_forget(address_of what, sizeof(what));
                return 0;
        }
        if (fresh && !wifi_seen_matches(address_of link->seen, what.rsn, what.rsn_length,
                                        what.rsnx, what.rsnx_length))
        {
                crypto_forget(address_of what, sizeof(what));
                return WIFI_REFUSE_ELEMENTS;
        }
        //      Only from a message 3 whose MIC checked: what the access point
        //      said under the key, not what anyone in range may say.
        link->transition_disable |= what.transition_disable;
        /* A failed group-key install is retried under the same replay
           counter. Do not acknowledge or consume it until the driver has
           the key. The joining handshake fails as a whole on an error, so
           its existing early reply remains part of that transaction. */
        if (pairwise)
        {
                memory_copy(link->replay, frame + 9, 8);
                link->replay_set = true;
                failed = wifi_eapol_reply(link, kck, 0x030a, link->replay, false);
        }
        if (!failed && fresh && (!link->installed || memory_compare(link->ptk + 32,
                                                                    kck + 32, 16)))
                failed = nl80211_new_key(address_of link->session, link->index, 0,
                                         NL80211_KEYTYPE_PAIRWISE, link->bssid, kck + 32,
                                         16, null, 0, false, WLAN_CIPHER_CCMP);
        if (!failed && fresh)
        {
                memory_copy(link->ptk, link->pending, sizeof(link->ptk));
                link->pending_set = false;
                link->nonce_used = false;
                crypto_forget(link->pending, sizeof(link->pending));
        }
        //      A message 3 resent for a handshake already done is answered
        //      and nothing more: held back and let through after a group
        //      rekey, it carries a group key the access point has moved on
        //      from, and putting that in again would reset its replay
        //      counter. Each key id also keeps its own last key.
        if (!failed && found && (fresh || !pairwise) &&
            (!(link->gtk_installed & (1u << idx)) ||
             memory_compare(what.gtk, link->gtk[idx], 16)))
        {
                failed = nl80211_new_key(address_of link->session, link->index, idx,
                                         NL80211_KEYTYPE_GROUP, null, what.gtk, 16, frame + 65,
                                         6, true, WLAN_CIPHER_CCMP);
                if (!failed)
                {
                        memory_copy(link->gtk[idx], what.gtk, 16);
                        link->gtk_installed |= (p8)(1u << idx);
                }
        }
        //      The group management key, for the broadcast deauthentication
        //      and disassociation frames of an access point that protects
        //      them: only on a link that asked for protection, once per key
        //      id and key, and before the group handshake is answered, as
        //      the group key is.
        if (!failed && link->pmf && what.igtk_id && (fresh || !pairwise) &&
            (!(link->igtk_installed & (1u << (what.igtk_id - 4))) ||
             memory_compare(what.igtk + 6, link->igtk[what.igtk_id - 4], 16)))
        {
                //      The IGTK is a group key of its own cipher, its packet
                //      number the six bytes at what.igtk.
                failed = nl80211_new_key(address_of link->session, link->index, what.igtk_id,
                                         NL80211_KEYTYPE_GROUP, null, what.igtk + 6, 16,
                                         what.igtk, 6, false, WLAN_CIPHER_BIP_CMAC_128);
                if (!failed)
                {
                        memory_copy(link->igtk[what.igtk_id - 4], what.igtk + 6, 16);
                        link->igtk_installed |= (p8)(1u << (what.igtk_id - 4));
                }
        }
        crypto_forget(address_of what, sizeof(what));
        if (!failed && !pairwise)
        {
                failed = wifi_eapol_reply(link, kck, 0x0302, frame + 9, false);
                if (!failed)
                {
                        memory_copy(link->replay, frame + 9, 8);
                        link->replay_set = true;
                }
        }
        if (!failed && pairwise && !link->installed)
                failed = nl80211_authorize(address_of link->session, link->index,
                                           link->bssid);
        if (failed)
                return failed;
        link->installed = true;
        wifi_rekey_offload(link);
        return pairwise ? 1 : 2;
}

typedef struct
{
        nl80211 address_to session;
        p32 index;
        b32 reason;
} nl80211_news_turn;

/* One message of the news: the 802.11 reason a link went away for, 1 where
   it names none or a zero, which is still a link gone. */
static COLD bool nl80211_link_news_take(netlink_header address_to header,
                                        address_any context)
{
        nl80211_news_turn address_to turn = (nl80211_news_turn address_to)context;
        p8 command = nl80211_news(turn->session, header, turn->index);
        positive size = 0;
        p8 address_to frame;

        if (command == NL80211_CMD_DISCONNECT)
                turn->reason = nl80211_find_u16(header, NL80211_ATTR_REASON_CODE, 1);
        else if (command == NL80211_CMD_DEAUTHENTICATE ||
                 command == NL80211_CMD_DISASSOCIATE)
        {
                //      The frame itself: its reason follows the 24-byte
                //      management header.
                frame = (p8 address_to)netlink_find(header, GENL_HEADER,
                                                    NL80211_ATTR_FRAME,
                                                    address_of size);
                turn->reason = frame && size >= 26 ? frame[24] | frame[25] << 8 : 1;
        }
        else
                return true;
        turn->reason = turn->reason ? turn->reason : 1;
        return false;
}

/* The link's news on the mlme socket: the 802.11 reason it went, 0 while
   it stands, -1 when the socket lost events and the link has to be asked
   after. Every interface's news comes here; only this one's counts. */
static COLD b32 nl80211_link_news(nl80211 address_to session, p32 index)
{
        netlink_buffer reply = {0};
        p32 local_port = 0;
        nl80211_news_turn turn = {.session = session, .index = index};
        bipolar got = netlink_receive(session->handle, address_of reply,
                                      address_of local_port);

        scope_exit(netlink_forget(address_of reply));
        if (got < 0)
                return got == NETWORK_INTERRUPTED || got == NETWORK_TRY_AGAIN
                           ? 0 : -1;
        netlink_each(address_of reply, nl80211_link_news_take, address_of turn);
        return turn.reason;
}

/* The link's EAPOL socket and its mlme news, waited on together until the
   deadline: bit 1 when a frame is waiting, bit 2 news, 0 at the deadline. */
static COLD bipolar wifi_wait(wifi_link address_to link,
                              const network_deadline address_to deadline)
{
        system_poll_descriptor waited[2] = {
            {link->eapol, SYSTEM_POLL_READ, 0},
            {link->session.handle, SYSTEM_POLL_READ, 0}};
        bipolar ready = network_wait_set(waited, 2, deadline);

        if (ready <= 0)
                return ready;
        return (waited[0].returned ? 1 : 0) | (waited[1].returned ? 2 : 0);
}

/* Whether a frame on the EAPOL socket came from the access point the link
   is with: its hardware address is the access point's and no other's.
   Anything else is another station's frame the access point relayed, or a
   forgery, and is not the access point talking (wpa_supplicant drops it
   too): before this, a message 1 from any source address was answered, and
   its ANonce replaced the pending key a real message 3 is checked under. */
static COLD bool wifi_eapol_from(wifi_link address_to link, socket_address_packet address_to from,
                                 p32 size)
{
        //      The kernel gives back the address only as far as the hardware
        //      address goes, which is 18 bytes of the 20 here.
        return size >= (p32)((p8 address_to)from->addr - (p8 address_to)from) + 6 &&
               from->pkttype == PACKET_HOST && from->halen == 6 &&
               !memory_compare(from->addr, link->bssid, 6);
}

/* One frame off the EAPOL socket, through the state machine. */
static COLD bipolar wifi_eapol_take(wifi_link address_to link)
{
        p8 frame[512];
        socket_address_packet from;
        p32 size = sizeof(from);
        bipolar got;
        bipolar step;

        memory_fill(address_of from, 0, sizeof(from));
        got = socket_receive(link->eapol, frame, sizeof(frame), MSG_DONTWAIT, address_of from,
                             address_of size);
        step = got > 0 && wifi_eapol_from(link, address_of from, size)
                   ? wifi_eapol_step(link, frame, (positive)got)
                   : 0;

        crypto_forget(frame, sizeof(frame));
        return step;
}

/*
        The join's four-way handshake, and what it says when it does not
        finish. It used to wait eight seconds on the EAPOL socket alone and
        call every failure a wrong password; now the access point sending
        the machine away ends it at once, and how far it got says why:

          no message 1 at all          -62  did not begin the handshake
          message 1 answered, then
            the access point asked
            again                      -13  did not accept the password
            sent the machine away    -1000-R ended the handshake (reason R)
            nothing                   -121  stopped answering in the handshake

        A message 1 resent after message 2 is the access point finding no
        MIC it could make in it: a password it does not have.
*/
static COLD bipolar wifi_handshake(wifi_link address_to link)
{
        network_deadline deadline;
        b32 reason = 0;
        bipolar step = 0;

        if (!network_deadline_begin(address_of deadline, WIFI_EAPOL_SECONDS, 0))
                return -1;
        while (step != 1 && step >= 0 && reason <= 0)
        {
                bipolar ready = wifi_wait(link, address_of deadline);

                if (ready <= 0)
                        break;
                if (ready & 2)
                        reason = nl80211_link_news(address_of link->session, link->index);
                if (ready & 1)
                        step = wifi_eapol_take(link);
        }
        if (step == 1)
                return 0;
        if (step < 0)
                return step;
        if (link->answered >= 2)
                return -13;
        if (reason > 0)
                return -(1000 + (bipolar)reason);
        return link->answered ? -121 : -62;
}

/*
        The station, apart, and its keeper.

        Everything the join and the keeper read comes from the air: scan
        elements of any access point in range, SAE commits and confirms,
        EAPOL-Key frames. So all of it -- the scan, the choice, WPA3's
        exchange, the association, the four-way handshake and, for as long
        as the link stands, its rekeys -- runs in the station, a process that
        is nobody with CAP_NET_ADMIN and CAP_NET_RAW and nothing else (no
        capability can come back through exec, which it may not make), not
        dumpable, killed with its broker, and under a filter that kills any
        call but its netlink and EAPOL sockets' (radio_filter_build). It opens
        no file. The little it needs of the machine's state -- what a saved
        network has taught the join, the access points to try last, when the
        air was last scanned for a name, the WPA3 budget -- and the little it
        has to leave there it asks of its broker over a channel of fixed-size
        questions (radio_ask): the broker is root, does the one thing each
        question names to the one file it names, and reads nothing else from
        the station but the join's status.

        The broker is the keeper the rest of the machine knows: it holds a
        write lock on wifi.keeper for as long as its station lives, which
        names it to whatever joins or leaves next (host_lock_owner), which
        ends it and waits until it is gone (and the station with it); what a
        pass finds of it is what says the link is there, without asking the
        kernel. wifi.kept says a station joined and has not ended of its own:
        a link with no keeper and that file is one whose keeper died. The
        joining process waits for the join's status and nothing else, holding
        the radio lock, so no join can start between the two.

        It fails closed: a kernel that will not take the filter joins nothing
        (WIFI_REFUSE_CONFINE), and a station that ends without a status is
        WIFI_REFUSE_APART.
*/
#define RADIO_KEEPER_PATH NET_STATE_DIR "/wifi.keeper"
#define RADIO_KEPT_PATH NET_STATE_DIR "/wifi.kept"

//      A socket of whole messages for the broker's channel: a question is
//      read whole or not at all.
#define RADIO_SOCK_SEQPACKET 5

#define RADIO_ASK_RULES_GET 1
#define RADIO_ASK_RULES_PUT 2
#define RADIO_ASK_AVOID_LOAD 3
#define RADIO_ASK_AVOID_ADD 4
#define RADIO_ASK_SCAN_AGE 5
#define RADIO_ASK_SCAN_NOTE 6
#define RADIO_ASK_SAE_BUDGET 7
#define RADIO_ASK_SAID 8
#define RADIO_ASK_JOINED 9
//      What the station may have logged, by number: the broker logs its
//      own literal for it, so nothing the station holds reaches the log.
#define RADIO_SAID_NO_CLOCK 1
#define RADIO_SAID_SOCKETS 2
//      How long the station waits for its broker to answer, which it does
//      at once: one that does not is gone.
#define RADIO_ASK_SECONDS 10
//      RADIO_AVOID_MOST of radio_avoid, sixteen bytes each.
#define RADIO_ASK_AVOID_ROOM 1024

//      One question, every field of it a fixed size the broker checks.
typedef struct
{
        p32 what;
        p8 ssid_length;
        p8 mask;
        p8 rules;
        p8 named;
        p8 ssid[32];
        p8 bssid[6];
        p8 unused[2];
        bipolar status;
} radio_ask;

typedef struct
{
        bipolar answer;
        p64 count;
        p8 avoid[RADIO_ASK_AVOID_ROOM];
} radio_answer;

//      The station's end of its broker's channel; -1 everywhere else.
static b32 radio_station = -1;

/* A question to the broker and its answer, which every question has, so a
   write is made before the station goes on. A broker that does not answer
   is gone, and its station with it. */
static COLD fn radio_station_ask(radio_ask address_to ask, radio_answer address_to answer)
{
        network_deadline deadline;

        memory_fill(answer, 0, sizeof(*answer));
        if (socket_send(radio_station, ask, sizeof(*ask), MSG_NOSIGNAL, null, 0) !=
                (bipolar)sizeof(*ask) ||
            !network_deadline_begin(address_of deadline, RADIO_ASK_SECONDS, 0) ||
            network_wait_readable_until(radio_station, address_of deadline) <= 0 ||
            socket_receive(radio_station, answer, sizeof(*answer), MSG_DONTWAIT, null, null) !=
                (bipolar)sizeof(*answer))
                system_call_1(syscall(exit_group), 1);
}

//      A question about a name or an access point, its answer's number.
static COLD bipolar radio_station_number(p32 what, const p8 address_to ssid, positive ssid_length,
                                     const p8 address_to bssid, p8 mask, p8 rules)
{
        radio_ask ask;
        radio_answer answer;

        memory_fill(address_of ask, 0, sizeof(ask));
        ask.what = what;
        if (ssid && ssid_length <= sizeof(ask.ssid))
        {
                memory_copy(ask.ssid, ssid, ssid_length);
                ask.ssid_length = (p8)ssid_length;
                ask.named = 1;
        }
        if (bssid)
                memory_copy(ask.bssid, bssid, 6);
        ask.mask = mask;
        ask.rules = rules;
        radio_station_ask(address_of ask, address_of answer);
        return answer.answer;
}

/*
        Where passwords are in this process when a join forks: the saved
        networks a pass loaded, a password a person typed. The broker, which
        never needs one, and the station, once its join is over, wipe every
        region held here, as the keeper used to be started only after the
        joining process had forgotten them.
*/
#define RADIO_SECRETS 4

static struct
{
        address_any at;
        positive length;
} radio_secrets[RADIO_SECRETS];

static fn radio_secret_hold(address_any at, positive length)
{
        for (positive slot = 0; slot < RADIO_SECRETS; slot++)
                if (!radio_secrets[slot].at)
                {
                        radio_secrets[slot].at = at;
                        radio_secrets[slot].length = length;
                        return;
                }
}

static fn radio_secret_let_go(address_any at)
{
        for (positive slot = 0; slot < RADIO_SECRETS; slot++)
                if (radio_secrets[slot].at == at)
                        radio_secrets[slot].at = null;
}

static fn radio_secrets_forget(void)
{
        for (positive slot = 0; slot < RADIO_SECRETS; slot++)
                if (radio_secrets[slot].at)
                        crypto_forget(radio_secrets[slot].at, radio_secrets[slot].length);
}

/* What the keeper or whoever starts it has to say, in the kernel log: its
   output is /dev/null. The reason is a literal at every call. */
static COLD fn radio_keeper_said(string_address reason)
{
        string_address line[] = {"wifi keeper: ", reason, null};

        host_kmsg(line);
}

//      The station's line for the log, said by its broker (RADIO_SAID_*).
static COLD fn radio_station_said(positive which)
{
        radio_ask ask;
        radio_answer answer;

        memory_fill(address_of ask, 0, sizeof(ask));
        ask.what = RADIO_ASK_SAID;
        ask.status = (bipolar)which;
        radio_station_ask(address_of ask, address_of answer);
}

static COLD fn radio_keeper_stop(void)
{
        if (host_lock_owner(RADIO_KEEPER_PATH))
        {
                host_lock_signal(RADIO_KEEPER_PATH, SIGKILL);
                host_lock_wait(RADIO_KEEPER_PATH);
        }
        system_remove_at(AT_FDCWD, RADIO_KEPT_PATH, 0);
}

static COLD fn radio_avoid_add(p8 address_to bssid);

/* The link roamed: the station is another access point's, with a handshake
   of its own to come. What was held of the last one is no key for it, and
   what the last one showed of itself is not what this one shows: a roam the
   driver made on its own has no elements to hold the next message 3 to. */
static COLD fn wifi_roam(wifi_link address_to link, p8 address_to to)
{
        memory_copy(link->bssid, to, 6);
        crypto_forget(link->ptk, sizeof(link->ptk));
        crypto_forget(link->pending, sizeof(link->pending));
        crypto_forget(link->gtk, sizeof(link->gtk));
        link->gtk_installed = 0;
        crypto_forget(link->igtk, sizeof(link->igtk));
        link->igtk_installed = 0;
        memory_fill(address_of link->seen, 0, sizeof(link->seen));
        memory_fill(link->replay, 0, sizeof(link->replay));
        link->replay_set = false;
        link->installed = false;
        link->pending_set = false;
        link->nonce_used = false;
}

static COLD fn wifi_keep(wifi_link address_to link)
{
        network_deadline check;
        p8 now[6];

        if (!network_deadline_begin(address_of check, WIFI_KEEP_CHECK_SECONDS, 0))
        {
                radio_station_said(RADIO_SAID_NO_CLOCK);
                return;
        }
        for (;;)
        {
                positive seconds;
                positive nanoseconds;
                bipolar ready = wifi_wait(link, address_of check);
                bipolar there;

                if (ready < 0)
                {
                        radio_station_said(RADIO_SAID_SOCKETS);
                        return;
                }
                if (ready & 2)
                {
                        b32 news = nl80211_link_news(address_of link->session,
                                                     link->index);

                        if (news > 0 ||
                            (news < 0 && nl80211_station(address_of link->session,
                                                         link->index, now) == 0))
                                break;
                }
                if (ready & 1)
                        wifi_eapol_take(link);

                //      Whatever woke it, the link is looked at every thirty
                //      seconds: news about other radios and frames from
                //      anyone in range must not push the look back for good.
                //      A question the kernel did not answer is no answer.
                if (network_deadline_left(address_of check, address_of seconds,
                                          address_of nanoseconds))
                        continue;
                if (!network_deadline_begin(address_of check, WIFI_KEEP_CHECK_SECONDS, 0))
                {
                        radio_station_said(RADIO_SAID_NO_CLOCK);
                        return;
                }
                there = nl80211_station(address_of link->session, link->index, now);
                if (!there)
                        break;
                if (there > 0 && memory_compare(now, link->bssid, 6))
                        wifi_roam(link, now);
        }
        //      Sent away, or the access point stopped answering: the next
        //      join tries another with the same name first.
        radio_avoid_add(link->bssid);
}

static COLD bool wifi_link_mac(string_address name, p8 address_to mac)
{
        netlink_search search;
        bipolar handle = netlink_open_groups(0);

        memory_fill(address_of search, 0, sizeof(search));
        if (handle < 0)
                return false;
        scope_exit(socket_close((b32)handle));
        search.wanted = name;
        if (netlink_link_find((b32)handle, address_of search) < 0 ||
            !search.has_hardware)
                return false;
        memory_copy(mac, search.hardware, 6);
        return true;
}

typedef struct
{
        nl80211 address_to session;
        p8 address_to bssid;
        p32 sequence;
        p32 port;
        p32 index;
        bipolar result;
        bool done;
        bool got_ack;
        bool associated;
} nl80211_join_turn;

/* One message of a join's wait: the acknowledgment of the connect, then the
   connect's own news. Anything that ends the wait is its result. */
static COLD bool nl80211_join_take(netlink_header address_to header,
                                   address_any context)
{
        nl80211_join_turn address_to turn = (nl80211_join_turn address_to)context;
        p8 command;

        if (header->type == NLMSG_IS_ERROR && header->sequence == turn->sequence &&
            header->port == turn->port)
        {
                bipolar ack = netlink_status(header, false);

                turn->got_ack = true;
                if (ack < 0)
                {
                        turn->result = ack;
                        turn->done = true;
                }
                return !turn->done;
        }

        command = nl80211_news(turn->session, header, turn->index);
        if (command == NL80211_CMD_CONNECT)
        {
                p16 status = nl80211_find_u16(header, NL80211_ATTR_STATUS_CODE, 0);
                positive mac_length = 0;
                p8 address_to mac;

                if (nl80211_attr(header, NL80211_ATTR_TIMED_OUT))
                {
                        turn->result = -110;
                        turn->done = true;
                }
                else if (status)
                {
                        turn->result = -111;
                        turn->done = true;
                }
                else
                {
                        mac = (p8 address_to)netlink_find(header, GENL_HEADER,
                                                          NL80211_ATTR_MAC,
                                                          address_of mac_length);
                        if (mac && mac_length >= 6)
                                memory_copy(turn->bssid, mac, 6);
                        turn->associated = true;
                }
        }
        else if (command == NL80211_CMD_DISCONNECT && turn->got_ack)
        {
                turn->result = -111;
                turn->done = true;
        }
        return !turn->done;
}

/* How often a join on a socket with no mlme news asks the kernel whether it
   is there yet. */
#define NL80211_POLL_NANOSECONDS 200000000

/* One wait on the join's socket: until the deadline, or, when it is the
   asking that tells of the association (asking), until the next time to ask
   if that comes first, which is NETWORK_TRY_AGAIN: nothing came, and the
   deadline has not. */
static COLD bipolar nl80211_join_wait(nl80211 address_to session,
                                      const network_deadline address_to deadline, bool asking)
{
        network_deadline slice;
        positive seconds;
        positive nanoseconds;
        bipolar got;

        if (!asking)
                return network_wait_readable_until(session->handle, deadline);
        if (!network_deadline_left(deadline, address_of seconds,
                                   address_of nanoseconds))
                return 0;
        /* Keep the join's original clock origin. A fresh slice must not
           renew an expired join when unrelated news keeps the socket ready. */
        slice = *deadline;
        if (seconds || nanoseconds > NL80211_POLL_NANOSECONDS)
                slice.budget = deadline->budget -
                               (seconds * NETWORK_NANOSECONDS + nanoseconds) +
                               NL80211_POLL_NANOSECONDS;
        got = network_wait_readable_until(session->handle, address_of slice);
        return !got && network_deadline_left(deadline, address_of seconds,
                                             address_of nanoseconds)
                   ? NETWORK_TRY_AGAIN
                   : got;
}

static COLD bipolar nl80211_wait_associated(nl80211 address_to session, p32 sequence,
                                       p32 index, p8 address_to bssid)
{
        netlink_buffer reply = {0};
        network_deadline deadline;
        nl80211_join_turn turn = {.session = session,
                                  .bssid = bssid,
                                  .sequence = sequence,
                                  .index = index};

        if (!network_deadline_begin(address_of deadline, NL80211_CONNECT_SECONDS, 0))
                return -1;

        scope_exit(netlink_forget(address_of reply));
        for (;;)
        {
                //      A socket that heard no mlme news is the one the join
                //      learns of the association from by asking, so what it
                //      waits for is the next time to ask as well as the
                //      deadline.
                bool asking = turn.got_ack && !turn.associated && !session->mlme;
                bipolar got;

                if (asking && nl80211_station(session, index, bssid) > 0)
                        return 0;

                got = nl80211_join_wait(session, address_of deadline, asking);
                if (got == NETWORK_TRY_AGAIN)
                        continue;
                if (got <= 0)
                {
                        if (turn.got_ack && !turn.associated &&
                            nl80211_station(session, index, bssid) > 0)
                                return 0;
                        return got < 0 ? got : -110;
                }

                turn.port = 0;
                got = netlink_receive(session->handle, address_of reply,
                                      address_of turn.port);
                if (got == NETWORK_INTERRUPTED || got == NETWORK_TRY_AGAIN)
                        continue;
                if (got < 0)
                        return got;

                netlink_each(address_of reply, nl80211_join_take, address_of turn);
                if (turn.done || (turn.got_ack && turn.associated))
                        return turn.done ? turn.result : 0;
        }
}

static COLD bipolar nl80211_connect(nl80211 address_to session, p32 index,
                               p8 address_to ssid, positive ssid_length,
                               p8 address_to pmk, bool offload, p8 address_to bssid,
                               p32 frequency, const wifi_link address_to link)
{
        netlink_buffer request = {0};
        p32 sequence = netlink_sequence_take();
        p32 open = NL80211_AUTHTYPE_OPEN;
        p32 version = NL80211_WPA_VERSION_2;
        p32 ccmp = WLAN_CIPHER_CCMP;
        p32 psk = WLAN_SUITE_IEEE | link->akm;
        p32 mfp = NL80211_MFP_REQUIRED;
        bipolar sent;

        scope_exit(nl80211_forget_secret(address_of request));
        if (!nl80211_begin(address_of request, session->family,
                           NL80211_CMD_CONNECT, NLM_REQUEST | NLM_ACK, sequence))
                return -1;

        const nl80211_attr_spec base[] = {
            {NL80211_ATTR_IFINDEX, address_of index, 4},
            {NL80211_ATTR_SSID, ssid, ssid_length},
            {NL80211_ATTR_AUTH_TYPE, address_of open, 4},
        };
        nl80211_put_attrs(address_of request, base, array_count(base));
        //      The access point chosen from a fresh scan, rather than
        //      whichever the kernel's cache holds by this name.
        if (bssid)
        {
                netlink_attribute_add(address_of request, NL80211_ATTR_MAC, bssid, 6);
                if (frequency)
                        nl80211_attribute_u32(address_of request, NL80211_ATTR_WIPHY_FREQ,
                                              frequency);
        }

        if (pmk)
        {
                /* The RSN element for the association request, which a
                   driver whose station management is mac80211's builds from
                   what it is handed and nothing else. Without it the access
                   point was asked to associate a station that offered no
                   security at all: hostapd took it, then started 802.1X on
                   it, and the four-way handshake never began. The same bytes
                   go in message 2, which the access point compares to these. */
                const nl80211_attr_spec wpa[] = {
                    {NL80211_ATTR_PRIVACY, null, 0},
                    {NL80211_ATTR_WPA_VERSIONS, address_of version, 4},
                    {NL80211_ATTR_CIPHER_SUITES_PAIRWISE, address_of ccmp, 4},
                    {NL80211_ATTR_CIPHER_SUITE_GROUP, address_of ccmp, 4},
                    {NL80211_ATTR_AKM_SUITES, address_of psk, 4},
                    {NL80211_ATTR_IE, link->rsn, link->rsn_length},
                };
                nl80211_put_attrs(address_of request, wpa, array_count(wpa));
                /* Management frame protection, which the kernel takes as
                   required or not at all: a driver with no connect of its
                   own (mac80211's kind) refuses the optional form outright.
                   Asked for, the kernel drops a deauthentication or
                   disassociation frame that is not protected. */
                if (link->pmf)
                        nl80211_attribute_u32(address_of request, NL80211_ATTR_USE_MFP, mfp);
                if (offload)
                        netlink_attribute_add(address_of request, NL80211_ATTR_PMK, pmk,
                                              32);
                else
                        netlink_attribute_add(address_of request,
                                              NL80211_ATTR_CONTROL_PORT, null, 0);
        }

        if (request.failed)
                return -1;

        sent = socket_send(session->handle, request.bytes, request.used, 0, 0, 0);
        if (sent < 0)
                return sent;
        return sequence;
}

/*
        WPA3-Personal's SAE (802.11-2020 12.4, RFC 7664's dragonfly) over
        the NIST P-256 group, group 19, on net.c's field and point routines.
        On the wire a scalar is 32 big-endian bytes and an element is x then
        y, 32 bytes each.

        The password element is found one of two ways. Hash-to-element
        (12.4.4.2.3) makes a point PT of the network's name and the password
        once, by two simplified SWU maps, with no loop and no branch on
        anything the password decides; it is used whenever the access point
        says it takes it, in its RSN extension element, which is every 6 GHz
        network and every one set to it alone. Hunting and pecking
        (12.4.4.2.2) is the method every WPA3 station has, used for the
        rest: every pass of the hunt is worked out whole, forty of them at
        least however early one lands, and the one that counts is kept by
        mask, which is the Dragonblood advice that the first implementations
        lacked.
*/
#define WIFI_SAE_GROUP 19
#define WIFI_SAE_PASSES 40
#define WIFI_SAE_PASSES_MOST 255
#define WIFI_SAE_SECONDS 10
#define WIFI_SAE_TOKEN_MOST 64
#define WIFI_SAE_TOKEN_STATUS 76
#define WIFI_SAE_H2E_STATUS 126
#define WIFI_SAE_TOKEN_EXTENSION 93

typedef struct
{
        p8 pwe[64];
        p8 rand[32];
        p8 scalar[32];
        p8 element[64];
        p8 peer_scalar[32];
        p8 peer_element[64];
        p8 kck[32];
        p8 pmk[32];
        p8 pmkid[16];
} wifi_sae;

/* 802.11's KDF-Hash-Length with SHA-256, for a length that is a whole
   number of hashes: HMAC of a counter from 1 (two bytes, little endian),
   the label, the context and the length in bits. */
static COLD fn wifi_sae_kdf(p8 address_to key, positive key_length, string_address label,
                            p8 address_to context, positive context_length,
                            p8 address_to out, positive bits)
{
        p8 block[2 + 24 + 64 + 2];
        positive label_length = string_length(label);
        positive length = 2 + label_length + context_length + 2;

        for (positive at = 0; at * 256 < bits; at++)
        {
                block[0] = (p8)(at + 1);
                block[1] = 0;
                memory_copy(block + 2, label, label_length);
                memory_copy(block + 2 + label_length, context, context_length);
                block[length - 2] = (p8)bits;
                block[length - 1] = (p8)(bits >> 8);
                crypto_hmac_sha256(key, key_length, block, length, out + at * 32);
        }
        crypto_forget(block, sizeof(block));
}

/* (p + 1) / 4, the exponent that takes a square root of a square of this
   field: p is 3 mod 4. */
static COLD fn wifi_sae_root_exponent(p64 address_to exponent)
{
        p64 carry = 1;

        for (positive i = 0; i < 4; i++)
                exponent[i] = (crypto_p256_p[i] >> 2) | (i < 3 ? crypto_p256_p[i + 1] << 62 : 0);
        for (positive i = 0; i < 4; i++)
        {
                p64 sum = exponent[i] + carry;

                carry = sum < carry;
                exponent[i] = sum;
        }
}

/* The two addresses, the higher first. */
static COLD fn wifi_sae_addresses(const p8 address_to first, const p8 address_to second,
                                  p8 address_to key)
{
        bool higher = memory_compare(first, second, 6) > 0;

        memory_copy(key, higher ? first : second, 6);
        memory_copy(key + 6, higher ? second : first, 6);
}

/*
        The password element by hunting and pecking, for the password and
        the two addresses: pwd-seed is HMAC-SHA-256 of the password and a
        counter under the addresses, pwd-value the KDF of it; the first
        value under p that is the x of a point is kept, with the y whose low
        bit is the seed's. Every pass is worked out in full, and the first
        that lands is kept by mask.
*/
static COLD bool wifi_sae_pwe(p8 address_to password, positive password_length,
                              p8 address_to first, p8 address_to second, p8 address_to pwe)
{
        p8 key[12];
        p8 data[64 + 1];
        p8 seed[32];
        p8 value[32];
        p8 p_bytes[32];
        p64 x[CRYPTO_FE_MAX], y[CRYPTO_FE_MAX], y2[CRYPTO_FE_MAX], b[CRYPTO_FE_MAX];
        p64 root[CRYPTO_FE_MAX], square[CRYPTO_FE_MAX], zero[CRYPTO_FE_MAX];
        p64 negative_y[CRYPTO_FE_MAX], exponent[CRYPTO_FE_MAX], scratch[CRYPTO_FE_MAX];
        p64 kept_x[CRYPTO_FE_MAX], kept_y[CRYPTO_FE_MAX];
        p64 found = 0;
        bool done = false;

        if (password_length > 64)
                return false;
        wifi_sae_addresses(first, second, key);
        crypto_fe_store_be(p_bytes, crypto_p256_p, 4);
        crypto_fe_load_be(b, crypto_p256_b_be, 4);
        crypto_fe_mul(b, b, crypto_p256_field.square, &crypto_p256_field);
        memory_fill(zero, 0, sizeof(zero));
        wifi_sae_root_exponent(exponent);
        memory_fill(kept_x, 0, sizeof(kept_x));
        memory_fill(kept_y, 0, sizeof(kept_y));
        memory_copy(data, password, password_length);
        for (positive pass = 1; pass <= WIFI_SAE_PASSES_MOST && !done; pass++)
        {
                p64 below;
                p64 residue;
                p64 lands;
                p64 odd;

                data[password_length] = (p8)pass;
                crypto_hmac_sha256(key, 12, data, password_length + 1, seed);
                wifi_sae_kdf(seed, 32, "SAE Hunting and Pecking", p_bytes, 32, value, 256);
                crypto_fe_load_be(x, value, 4);
                below = crypto_fe_subtract_raw(scratch, x, crypto_p256_p, 4);
                //      y^2 = x^3 - 3x + b, in Montgomery form.
                crypto_fe_mul(x, x, crypto_p256_field.square, &crypto_p256_field);
                crypto_fe_sqr(square, x, &crypto_p256_field);
                crypto_fe_mul(y2, square, x, &crypto_p256_field);
                crypto_fe_sub(y2, y2, x, &crypto_p256_field);
                crypto_fe_sub(y2, y2, x, &crypto_p256_field);
                crypto_fe_sub(y2, y2, x, &crypto_p256_field);
                crypto_fe_add(y2, y2, b, &crypto_p256_field);
                crypto_fe_power(root, y2, exponent, &crypto_p256_field);
                crypto_fe_sqr(square, root, &crypto_p256_field);
                crypto_fe_sub(scratch, square, y2, &crypto_p256_field);
                residue = crypto_fe_zero_bit(scratch, 4);
                lands = below & residue & (found ^ 1);
                //      The y whose low bit, as a plain integer, is the seed's.
                crypto_fe_mul(y, root, crypto_unit, &crypto_p256_field);
                odd = y[0] & 1;
                crypto_fe_sub(negative_y, zero, root, &crypto_p256_field);
                crypto_fe_select(root, root, negative_y, 4, odd ^ (p64)(seed[31] & 1));
                crypto_fe_select(kept_x, kept_x, x, 4, lands);
                crypto_fe_select(kept_y, kept_y, root, 4, lands);
                found |= lands;
                done = found && pass >= WIFI_SAE_PASSES;
        }
        crypto_fe_mul(kept_x, kept_x, crypto_unit, &crypto_p256_field);
        crypto_fe_mul(kept_y, kept_y, crypto_unit, &crypto_p256_field);
        crypto_fe_store_be(pwe, kept_x, 4);
        crypto_fe_store_be(pwe + 32, kept_y, 4);
        crypto_forget(data, sizeof(data));
        crypto_forget(seed, sizeof(seed));
        crypto_forget(value, sizeof(value));
        crypto_forget(x, sizeof(x));
        crypto_forget(y, sizeof(y));
        crypto_forget(y2, sizeof(y2));
        crypto_forget(root, sizeof(root));
        crypto_forget(square, sizeof(square));
        crypto_forget(negative_y, sizeof(negative_y));
        crypto_forget(scratch, sizeof(scratch));
        crypto_forget(kept_x, sizeof(kept_x));
        crypto_forget(kept_y, sizeof(kept_y));
        return found != 0;
}

/* A scalar from 1 to the group order less one, as 32 bytes. */
static COLD bool wifi_sae_scalar(p8 address_to out)
{
        p64 limbs[CRYPTO_FE_MAX];
        bool good = false;

        for (positive tries = 0; tries < 64 && !good; tries++)
        {
                if (system_random_fill(out, 32, 0) < 0)
                        break;
                good = crypto_scalar_from_int_be(limbs, out, 32, crypto_p256_n, 4);
        }
        crypto_forget(limbs, sizeof(limbs));
        return good;
}

/* out = (a + b) mod the group order, all as 32 bytes. */
static COLD fn wifi_sae_scalar_add(p8 address_to out, const p8 address_to a,
                                   const p8 address_to b)
{
        p64 x[CRYPTO_FE_MAX], y[CRYPTO_FE_MAX];

        crypto_fe_load_be(x, a, 4);
        crypto_fe_load_be(y, b, 4);
        crypto_fe_add(x, x, y, &crypto_p256_order);
        crypto_fe_store_be(out, x, 4);
        crypto_forget(x, sizeof(x));
        crypto_forget(y, sizeof(y));
}

/* k times the point (x then y), as bytes; false when it is infinity. */
static COLD bool wifi_sae_multiply(p8 address_to out, const p8 address_to k,
                                   const p8 address_to point)
{
        crypto_point base, result;
        p64 x[CRYPTO_FE_MAX], y[CRYPTO_FE_MAX], scalar[CRYPTO_FE_MAX];
        bool finite;

        crypto_fe_load_be(x, point, 4);
        crypto_fe_load_be(y, point + 32, 4);
        crypto_fe_load_be(scalar, k, 4);
        crypto_point_set_xy(&base, x, y, &crypto_p256_field);
        crypto_point_scalar_private(&result, &base, scalar, null);
        finite = !crypto_fe_is_zero(result.z, 4);
        crypto_point_affine(&result);
        crypto_fe_store_be(out, result.x, 4);
        crypto_fe_store_be(out + 32, result.y, 4);
        crypto_forget(scalar, sizeof(scalar));
        crypto_forget(&base, sizeof(base));
        crypto_forget(&result, sizeof(result));
        return finite;
}

/* The commit: scalar = (rand + mask) mod r, which is neither zero nor one,
   and element = the inverse of mask * PWE. */
static COLD bool wifi_sae_commit(wifi_sae address_to sae)
{
        p8 mask[32];
        p8 product[64];
        p64 y[CRYPTO_FE_MAX], zero[CRYPTO_FE_MAX];
        bool good = false;

        memory_fill(zero, 0, sizeof(zero));
        for (positive tries = 0; tries < 16 && !good; tries++)
        {
                p8 high = 0;

                if (!wifi_sae_scalar(sae->rand) || !wifi_sae_scalar(mask))
                        break;
                wifi_sae_scalar_add(sae->scalar, sae->rand, mask);
                for (positive at = 0; at < 31; at++)
                        high |= sae->scalar[at];
                good = (high || sae->scalar[31] > 1) && wifi_sae_multiply(product, mask, sae->pwe);
        }
        if (good)
        {
                crypto_fe_load_be(y, product + 32, 4);
                crypto_fe_sub(y, zero, y, &crypto_p256_field);
                memory_copy(sae->element, product, 32);
                crypto_fe_store_be(sae->element + 32, y, 4);
        }
        crypto_forget(mask, sizeof(mask));
        crypto_forget(product, sizeof(product));
        crypto_forget(y, sizeof(y));
        return good;
}

/* Whether the access point's commit is one to take, by what costs nothing:
   its scalar is above one and under the order, its element is a point of
   the curve, and the two are not our own sent back (a reflection). */
static COLD bool wifi_sae_commit_good(const wifi_sae address_to sae, const p8 address_to scalar,
                                      const p8 address_to element)
{
        p64 limbs[CRYPTO_FE_MAX];

        if (!crypto_scalar_from_int_be(limbs, scalar, 32, crypto_p256_n, 4) ||
            (limbs[0] == 1 && !limbs[1] && !limbs[2] && !limbs[3]))
                return false;
        if (!crypto_point_is_on_curve(element, element + 32, &crypto_p256_field,
                                      crypto_p256_b_be))
                return false;
        return memory_compare(scalar, sae->scalar, 32) || memory_compare(element, sae->element, 64);
}

/*
        The access point's commit taken in, which is where the cost is: two
        scalar multiplications. The shared secret K = rand * (peer-scalar *
        PWE + peer-element), of which only the x goes on: keyseed is its
        HMAC-SHA-256 under a key of zeros, the KCK and the PMK are the KDF
        of that over the sum of the two scalars, and the PMKID is the sum's
        high 128 bits. False when a point on the way is infinity.
*/
static COLD bool wifi_sae_take(wifi_sae address_to sae, const p8 address_to scalar,
                               const p8 address_to element)
{
        p8 shared[64], sum_point[64], term[64], keyseed[32], zeros[32], sum[32], keys[64];
        crypto_point first, second, total;
        p64 x[CRYPTO_FE_MAX], y[CRYPTO_FE_MAX];
        bool good = false;

        memory_fill(zeros, 0, sizeof(zeros));
        memory_copy(sae->peer_scalar, scalar, 32);
        memory_copy(sae->peer_element, element, 64);
        if (wifi_sae_multiply(term, scalar, sae->pwe))
        {
                crypto_fe_load_be(x, term, 4);
                crypto_fe_load_be(y, term + 32, 4);
                crypto_point_set_xy(&first, x, y, &crypto_p256_field);
                crypto_fe_load_be(x, element, 4);
                crypto_fe_load_be(y, element + 32, 4);
                crypto_point_set_xy(&second, x, y, &crypto_p256_field);
                crypto_point_add(&total, &first, &second);
                if (!crypto_fe_is_zero(total.z, 4))
                {
                        crypto_point_affine(&total);
                        crypto_fe_store_be(sum_point, total.x, 4);
                        crypto_fe_store_be(sum_point + 32, total.y, 4);
                        good = wifi_sae_multiply(shared, sae->rand, sum_point);
                        if (good)
                        {
                                crypto_hmac_sha256(zeros, 32, shared, 32, keyseed);
                                wifi_sae_scalar_add(sum, sae->scalar, scalar);
                                wifi_sae_kdf(keyseed, 32, "SAE KCK and PMK", sum, 32, keys, 512);
                                memory_copy(sae->kck, keys, 32);
                                memory_copy(sae->pmk, keys + 32, 32);
                                memory_copy(sae->pmkid, sum, 16);
                        }
                }
        }
        crypto_forget(shared, sizeof(shared));
        crypto_forget(sum_point, sizeof(sum_point));
        crypto_forget(term, sizeof(term));
        crypto_forget(keyseed, sizeof(keyseed));
        crypto_forget(sum, sizeof(sum));
        crypto_forget(keys, sizeof(keys));
        crypto_forget(x, sizeof(x));
        crypto_forget(y, sizeof(y));
        crypto_forget(&first, sizeof(first));
        crypto_forget(&second, sizeof(second));
        crypto_forget(&total, sizeof(total));
        return good;
}

/* CN(KCK, counter, scalar, element, peer-scalar, peer-element): the two
   sides' confirms are the one computation with the roles swapped. */
static COLD fn wifi_sae_confirm(const p8 address_to kck, const p8 address_to counter,
                                const p8 address_to scalar, const p8 address_to element,
                                const p8 address_to other_scalar,
                                const p8 address_to other_element, p8 address_to out)
{
        p8 block[2 + 32 + 64 + 32 + 64];

        memory_copy(block, counter, 2);
        memory_copy(block + 2, scalar, 32);
        memory_copy(block + 34, element, 64);
        memory_copy(block + 98, other_scalar, 32);
        memory_copy(block + 130, other_element, 64);
        crypto_hmac_sha256((p8 address_to)kck, 32, block, sizeof(block), out);
}

/* A small number in Montgomery form. */
static COLD fn wifi_sae_small(p64 address_to out, p64 value)
{
        memory_fill(out, 0, 4 * sizeof(p64));
        out[0] = value;
        crypto_fe_mul(out, out, crypto_p256_field.square, &crypto_p256_field);
}

/* Forty-eight big-endian bytes modulo p, as a plain integer: the high
   sixteen times 2^256 (which is what the conversion to Montgomery form
   multiplies by) plus the low thirty-two brought under p once. */
static COLD fn wifi_sae_reduce(p64 address_to out, const p8 address_to bytes)
{
        p64 high[CRYPTO_FE_MAX], low[CRYPTO_FE_MAX], under[CRYPTO_FE_MAX];
        p64 less;

        memory_fill(high, 0, sizeof(high));
        high[0] = network_load_64(bytes + 8);
        high[1] = network_load_64(bytes);
        crypto_fe_load_be(low, bytes + 16, 4);
        less = crypto_fe_subtract_raw(under, low, crypto_p256_p, 4);
        crypto_fe_select(low, under, low, 4, less);
        crypto_fe_mul(high, high, crypto_p256_field.square, &crypto_p256_field);
        crypto_fe_add(out, high, low, &crypto_p256_field);
        crypto_forget(high, sizeof(high));
        crypto_forget(low, sizeof(low));
        crypto_forget(under, sizeof(under));
}

/* RFC 9380's simplified SWU map of u, a plain integer under p, to a point
   of the curve, as plain x and y: Z = -10, the two candidates and their
   squares chosen by mask, the square root by the exponent (p + 1) / 4, and
   the sign of y taken from the low bit of u. */
static COLD fn wifi_sae_sswu(p64 address_to x_out, p64 address_to y_out, const p64 address_to u)
{
        const crypto_field address_to f = &crypto_p256_field;
        p64 um[4], u2[4], t1[4], t2[4], m[4], t[4], z[4], a[4], b[4], zero[4], one[4];
        p64 x1a[4], x1b[4], x1[4], x2[4], gx1[4], gx2[4], y1[4], v[4], x[4], y[4], ny[4];
        p64 exponent[4];
        p64 is_zero, is_square, odd;

        wifi_sae_root_exponent(exponent);
        memory_fill(zero, 0, sizeof(zero));
        memory_copy(one, f->one, sizeof(one));
        wifi_sae_small(t1, 10);
        crypto_fe_sub(z, zero, t1, f);
        wifi_sae_small(t1, 3);
        crypto_fe_sub(a, zero, t1, f);
        crypto_fe_load_be(b, crypto_p256_b_be, 4);
        crypto_fe_mul(b, b, f->square, f);

        crypto_fe_mul(um, u, f->square, f);
        //      m = z^2 u^4 + z u^2, and t = 1 / m (zero for zero).
        crypto_fe_sqr(u2, um, f);
        crypto_fe_mul(t1, z, u2, f);
        crypto_fe_sqr(t2, t1, f);
        crypto_fe_add(m, t1, t2, f);
        crypto_fe_inv(t, m, f);
        //      x1 = b / (z a) when m is zero, else (-b / a) (1 + t).
        crypto_fe_mul(t1, z, a, f);
        crypto_fe_inv(t1, t1, f);
        crypto_fe_mul(x1a, b, t1, f);
        crypto_fe_sub(t1, zero, b, f);
        crypto_fe_inv(t2, a, f);
        crypto_fe_mul(t1, t1, t2, f);
        crypto_fe_add(t2, one, t, f);
        crypto_fe_mul(x1b, t1, t2, f);
        is_zero = crypto_fe_zero_bit(m, 4);
        crypto_fe_select(x1, x1b, x1a, 4, is_zero);
        //      gx1 = x1^3 + a x1 + b; x2 = z u^2 x1; gx2 likewise.
        crypto_fe_sqr(t1, x1, f);
        crypto_fe_mul(t1, t1, x1, f);
        crypto_fe_mul(t2, a, x1, f);
        crypto_fe_add(t1, t1, t2, f);
        crypto_fe_add(gx1, t1, b, f);
        crypto_fe_mul(t1, z, u2, f);
        crypto_fe_mul(x2, t1, x1, f);
        crypto_fe_sqr(t1, x2, f);
        crypto_fe_mul(t1, t1, x2, f);
        crypto_fe_mul(t2, a, x2, f);
        crypto_fe_add(t1, t1, t2, f);
        crypto_fe_add(gx2, t1, b, f);
        //      Whichever of gx1 and gx2 is a square gives the point.
        crypto_fe_power(y1, gx1, exponent, f);
        crypto_fe_sqr(t1, y1, f);
        crypto_fe_sub(t1, t1, gx1, f);
        is_square = crypto_fe_zero_bit(t1, 4);
        crypto_fe_select(v, gx2, gx1, 4, is_square);
        crypto_fe_select(x, x2, x1, 4, is_square);
        crypto_fe_power(y, v, exponent, f);
        //      The y whose low bit is u's.
        crypto_fe_mul(t1, y, crypto_unit, f);
        odd = t1[0] & 1;
        crypto_fe_sub(ny, zero, y, f);
        crypto_fe_select(y, y, ny, 4, odd ^ (u[0] & 1));
        crypto_fe_mul(x_out, x, crypto_unit, f);
        crypto_fe_mul(y_out, y, crypto_unit, f);
        crypto_forget(um, sizeof(um));
        crypto_forget(u2, sizeof(u2));
        crypto_forget(t1, sizeof(t1));
        crypto_forget(t2, sizeof(t2));
        crypto_forget(m, sizeof(m));
        crypto_forget(t, sizeof(t));
        crypto_forget(x1, sizeof(x1));
        crypto_forget(x1a, sizeof(x1a));
        crypto_forget(x1b, sizeof(x1b));
        crypto_forget(x2, sizeof(x2));
        crypto_forget(gx1, sizeof(gx1));
        crypto_forget(gx2, sizeof(gx2));
        crypto_forget(y1, sizeof(y1));
        crypto_forget(v, sizeof(v));
        crypto_forget(x, sizeof(x));
        crypto_forget(y, sizeof(y));
        crypto_forget(ny, sizeof(ny));
}

/* PT for a network's name and a password: HKDF-Extract of the password
   salted by the name, two HKDF-Expands to 48 bytes ("SAE Hash to Element
   u1 P1" and "u2 P2"), each reduced by p and mapped, and the sum of the
   two points, as x then y. */
static COLD bool wifi_sae_pt(p8 address_to ssid, positive ssid_length, p8 address_to password,
                             positive password_length, p8 address_to pt)
{
        p8 seed[32];
        p8 value[48];
        p64 u[CRYPTO_FE_MAX], x[CRYPTO_FE_MAX], y[CRYPTO_FE_MAX];
        crypto_point point[2], total;
        bool good = true;

        crypto_hkdf_extract(ssid, ssid_length, password, password_length, seed);
        for (positive which = 0; which < 2; which++)
        {
                string_address label = which ? (string_address) "SAE Hash to Element u2 P2"
                                             : (string_address) "SAE Hash to Element u1 P1";

                good &= crypto_hkdf_expand(seed, (p8 address_to)label, 25, value, 48);
                wifi_sae_reduce(u, value);
                wifi_sae_sswu(x, y, u);
                crypto_point_set_xy(&point[which], x, y, &crypto_p256_field);
        }
        crypto_point_add(&total, &point[0], &point[1]);
        good &= !crypto_fe_is_zero(total.z, 4);
        crypto_point_affine(&total);
        crypto_fe_store_be(pt, total.x, 4);
        crypto_fe_store_be(pt + 32, total.y, 4);
        crypto_forget(seed, sizeof(seed));
        crypto_forget(value, sizeof(value));
        crypto_forget(u, sizeof(u));
        crypto_forget(x, sizeof(x));
        crypto_forget(y, sizeof(y));
        crypto_forget(point, sizeof(point));
        crypto_forget(&total, sizeof(total));
        return good;
}

/* The element of two stations from PT: PT times HKDF-Extract under 32
   zeros of the higher address then the lower, brought to 1 .. r - 1 (one
   subtraction, since a hash is under twice r - 1, then one more). */
static COLD bool wifi_sae_pwe_from_pt(const p8 address_to pt, const p8 address_to first,
                                      const p8 address_to second, p8 address_to pwe)
{
        p8 key[12];
        p8 zeros[32];
        p8 value[32];
        p64 v[CRYPTO_FE_MAX], below[CRYPTO_FE_MAX], one[CRYPTO_FE_MAX], diff[CRYPTO_FE_MAX];
        p64 less;
        p64 carry = 1;
        bool good;

        wifi_sae_addresses(first, second, key);
        memory_fill(zeros, 0, sizeof(zeros));
        crypto_hkdf_extract(zeros, 32, key, 12, value);
        crypto_fe_load_be(v, value, 4);
        memory_fill(one, 0, sizeof(one));
        one[0] = 1;
        crypto_fe_subtract_raw(below, crypto_p256_n, one, 4);
        less = crypto_fe_subtract_raw(diff, v, below, 4);
        crypto_fe_select(v, diff, v, 4, less);
        for (positive i = 0; i < 4; i++)
        {
                p64 sum = v[i] + carry;

                carry = sum < carry;
                v[i] = sum;
        }
        crypto_fe_store_be(value, v, 4);
        good = wifi_sae_multiply(pwe, value, pt);
        crypto_forget(value, sizeof(value));
        crypto_forget(v, sizeof(v));
        return good;
}

/*
        One authentication frame of the exchange, as an access point sent
        it: its transaction and status, and what the two it reads say.

          commit (1), status 0 or 126   the group; for group 19, the scalar
                                        and the element, then only whole
                                        elements (an identifier, rejected
                                        groups)
          token asked for (1, 76)        the group, then the token: the rest
                                        of the frame, or with
                                        hash-to-element a token container
                                        (255, n, 93, token)
          confirm (2), status 0          the send-confirm and the confirm,
                                        and nothing after them
          any other status              nothing more

        False for a frame that is no SAE authentication frame from bssid
        (transmitter and network both), that is cut short or runs on past
        what it says it is, or whose token is empty or over
        WIFI_SAE_TOKEN_MOST.
*/
typedef struct
{
        p16 transaction;
        p16 status;
        p16 group;
        p16 send_confirm;
        const p8 address_to scalar;
        const p8 address_to element;
        const p8 address_to confirm;
        const p8 address_to token;
        positive token_length;
} wifi_sae_frame;

static COLD bool wifi_sae_frame_read(const p8 address_to frame, positive size,
                                     const p8 address_to bssid, bool h2e,
                                     wifi_sae_frame address_to out)
{
        byte_reader reader = byte_reader_open(frame, size);
        p16 control = (p16)byte_reader_uint(&reader, 2, false);
        const p8 address_to transmitter;
        const p8 address_to network;
        p16 algorithm;

        memory_fill(out, 0, sizeof(*out));
        (void)byte_reader_skip(&reader, 2 + 6);
        transmitter = byte_reader_take(&reader, 6);
        network = byte_reader_take(&reader, 6);
        (void)byte_reader_skip(&reader, 2);
        algorithm = (p16)byte_reader_uint(&reader, 2, false);
        out->transaction = (p16)byte_reader_uint(&reader, 2, false);
        out->status = (p16)byte_reader_uint(&reader, 2, false);
        //      A management frame of the authentication subtype, not
        //      protected, from the access point and in its network.
        if (!byte_reader_ok(&reader) || (control & 0x40ff) != 0x00b0 || algorithm != 3 ||
            memory_compare(transmitter, bssid, 6) || memory_compare(network, bssid, 6))
                return false;
        if (out->transaction == 1 &&
            (out->status == 0 || out->status == WIFI_SAE_H2E_STATUS))
        {
                out->group = (p16)byte_reader_uint(&reader, 2, false);
                if (out->group != WIFI_SAE_GROUP)
                        return byte_reader_ok(&reader);
                out->scalar = byte_reader_take(&reader, 32);
                out->element = byte_reader_take(&reader, 64);
                while (byte_reader_ok(&reader) && byte_reader_left(&reader))
                {
                        (void)byte_reader_u8(&reader);
                        (void)byte_reader_vector8(&reader);
                }
                return byte_reader_ok(&reader);
        }
        if (out->transaction == 1 && out->status == WIFI_SAE_TOKEN_STATUS)
        {
                out->group = (p16)byte_reader_uint(&reader, 2, false);
                if (h2e)
                {
                        byte_reader container;

                        if (byte_reader_u8(&reader) != 255)
                                return false;
                        container = byte_reader_vector8(&reader);
                        if (byte_reader_u8(&container) != WIFI_SAE_TOKEN_EXTENSION ||
                            !byte_reader_end(&reader))
                                return false;
                        reader = container;
                }
                out->token_length = byte_reader_left(&reader);
                out->token = byte_reader_take(&reader, out->token_length);
                return byte_reader_ok(&reader) && out->token_length &&
                       out->token_length <= WIFI_SAE_TOKEN_MOST;
        }
        if (out->transaction == 2 && !out->status)
        {
                out->send_confirm = (p16)byte_reader_uint(&reader, 2, false);
                out->confirm = byte_reader_take(&reader, 32);
                return byte_reader_ok(&reader) && byte_reader_end(&reader);
        }
        return out->transaction == 1 || out->transaction == 2;
}

/* A commit as the body of an authentication frame: transaction 1, status
   0 (126 when the exchange uses hash-to-element), the group, the token an
   access point asked for, the scalar and the element. The token goes
   before the scalar, or, for hash-to-element, after the element in a
   container element of its own. */
static COLD positive wifi_sae_commit_body(const wifi_sae address_to sae, const p8 address_to token,
                                          positive token_length, p8 address_to out, bool h2e)
{
        positive at = 0;

        out[at++] = 1, out[at++] = 0;
        out[at++] = h2e ? WIFI_SAE_H2E_STATUS : 0, out[at++] = 0;
        out[at++] = WIFI_SAE_GROUP, out[at++] = 0;
        if (token_length && !h2e)
        {
                memory_copy(out + at, token, token_length);
                at += token_length;
        }
        memory_copy(out + at, sae->scalar, 32);
        memory_copy(out + at + 32, sae->element, 64);
        at += 96;
        if (token_length && h2e)
        {
                out[at++] = 255;
                out[at++] = (p8)(1 + token_length);
                out[at++] = WIFI_SAE_TOKEN_EXTENSION;
                memory_copy(out + at, token, token_length);
                at += token_length;
        }
        return at;
}

/*
        The exchange's state as the access point's frames move it, one
        netlink message at a time. What it costs is bounded however many
        frames come, from the access point or in its name: at most
        WIFI_SAE_FRAMES_MOST authentication events are read; a commit is
        checked for what costs nothing (wifi_sae_commit_good) and only one,
        the first that passes, is taken, at two scalar multiplications;
        one token round; and the first confirm decides. A commit that does
        not pass, a confirm before any commit, and a frame from anyone else
        are passed over. A refusal, the link sent away and the kernel's
        timeout end it: -111 before our confirm went, -13 after it (the
        password, as the access point saw it), -110 for the timeout. body
        is a frame to send when the message is read, or nothing.
*/
#define WIFI_SAE_FRAMES_MOST 64

typedef struct
{
        wifi_sae address_to sae;
        const p8 address_to bssid;
        p32 index;
        p16 family;
        p32 port;
        bool h2e;
        positive frames;
        bool token_sent;
        bool taken;
        bool done;
        bipolar result;
        p8 body[2 + 2 + 2 + WIFI_SAE_TOKEN_MOST + 96 + 3];
        positive body_length;
} wifi_sae_turn;

static COLD bool wifi_sae_end(wifi_sae_turn address_to turn, bipolar result)
{
        turn->done = true;
        turn->result = result;
        return false;
}

static COLD bool wifi_sae_news(netlink_header address_to header, address_any context)
{
        wifi_sae_turn address_to turn = (wifi_sae_turn address_to)context;
        wifi_sae_frame read;
        p8 address_to frame;
        positive size = 0;
        p8 command;

        if (turn->done)
                return false;
        if (header->type == NLMSG_IS_ERROR)
                return header->port == turn->port && netlink_status(header, true) < 0
                           ? wifi_sae_end(turn, turn->taken ? -13 : -111)
                           : true;
        if (header->type != turn->family ||
            nl80211_find_u32(header, NL80211_ATTR_IFINDEX, 0) != turn->index)
                return true;
        command = nl80211_command(header);
        if (command == NL80211_CMD_DEAUTHENTICATE || command == NL80211_CMD_DISASSOCIATE)
                return wifi_sae_end(turn, turn->taken ? -13 : -111);
        if (command != NL80211_CMD_AUTHENTICATE)
                return true;
        if (++turn->frames > WIFI_SAE_FRAMES_MOST)
                return wifi_sae_end(turn, -111);
        if (nl80211_attr(header, NL80211_ATTR_TIMED_OUT))
                return wifi_sae_end(turn, -110);
        frame = (p8 address_to)netlink_find(header, GENL_HEADER, NL80211_ATTR_FRAME,
                                            address_of size);
        if (!frame || !wifi_sae_frame_read(frame, size, turn->bssid, turn->h2e, address_of read))
                return true;
        if (read.transaction == 1 && read.status == WIFI_SAE_TOKEN_STATUS)
        {
                //      Anti-clogging: the same commit again with the token
                //      it asked for, once.
                if (turn->token_sent || turn->taken)
                        return wifi_sae_end(turn, -111);
                turn->token_sent = true;
                turn->body_length = wifi_sae_commit_body(turn->sae, read.token, read.token_length,
                                                         turn->body, turn->h2e);
                return true;
        }
        if (read.transaction == 1 && (!read.status || read.status == WIFI_SAE_H2E_STATUS))
        {
                //      One commit is taken in; before it, a commit of the
                //      other method is a refusal of ours.
                if (turn->taken)
                        return true;
                if (read.status != (turn->h2e ? WIFI_SAE_H2E_STATUS : 0))
                        return wifi_sae_end(turn, -111);
                if (read.group != WIFI_SAE_GROUP ||
                    !wifi_sae_commit_good(turn->sae, read.scalar, read.element))
                        return true;
                turn->taken = true;
                if (!wifi_sae_take(turn->sae, read.scalar, read.element))
                        return wifi_sae_end(turn, -111);
                turn->body[0] = 2, turn->body[1] = 0, turn->body[2] = 0, turn->body[3] = 0;
                turn->body[4] = 1, turn->body[5] = 0;
                wifi_sae_confirm(turn->sae->kck, turn->body + 4, turn->sae->scalar,
                                 turn->sae->element, turn->sae->peer_scalar,
                                 turn->sae->peer_element, turn->body + 6);
                turn->body_length = 6 + 32;
                return true;
        }
        if (read.transaction == 2 && !read.status)
        {
                p8 counter[2];
                p8 want[32];
                bool same;

                if (!turn->taken)
                        return true;
                counter[0] = (p8)read.send_confirm;
                counter[1] = (p8)(read.send_confirm >> 8);
                wifi_sae_confirm(turn->sae->kck, counter, turn->sae->peer_scalar,
                                 turn->sae->peer_element, turn->sae->scalar,
                                 turn->sae->element, want);
                same = crypto_same(want, read.confirm, 32);
                crypto_forget(want, sizeof(want));
                return wifi_sae_end(turn, same ? 0 : -13);
        }
        return read.status ? wifi_sae_end(turn, turn->taken ? -13 : -111) : true;
}

/*
        The work SAE asks of the station before the access point has proved
        anything: the password element (a hunt of forty passes or more, or
        PT and its multiple), a commit and, once its commit comes, two
        scalar multiplications more. Anybody in range can make a join start
        it again and again -- a twin of the name that refuses every commit,
        a refusal forged in the real one's name -- so the exchanges are
        counted, in /run/moonwater/wifi.sae, by the boot clock: at most
        WIFI_SAE_PEER_MOST with one access point and WIFI_SAE_ALL_MOST with
        all of them in any WIFI_SAE_WINDOW_MS. One over is refused before
        any of the work, as WIFI_REFUSE_SAE_BUDGET, which the machine's pass
        waits on as it waits on a wrong password.
*/
#define RADIO_SAE_PATH NET_STATE_DIR "/wifi.sae"
#define WIFI_SAE_WINDOW_MS 60000u
#define WIFI_SAE_PEER_MOST 6
#define WIFI_SAE_ALL_MOST 24

typedef struct
{
        p8 bssid[6];
        p8 unused[2];
        p64 at;
} wifi_sae_spent;

static COLD bool wifi_sae_budget(const p8 address_to bssid)
{
        wifi_sae_spent list[WIFI_SAE_ALL_MOST + 1];
        bipolar got;
        positive count;
        p64 now = system_clock_ns(HOST_CLOCK_BOOTTIME) / 1000000;
        positive kept = 1;
        positive peer = 0;

        if (radio_station >= 0)
                return radio_station_number(RADIO_ASK_SAE_BUDGET, null, 0, bssid, 0, 0) != 0;
        got = file_read_once_at(AT_FDCWD, RADIO_SAE_PATH, list + 1,
                                WIFI_SAE_ALL_MOST * sizeof(list[0]));
        count = got > 0 ? (positive)got / sizeof(list[0]) : 0;

        for (positive at = 1; at <= count; at++)
                if (list[at].at <= now && now - list[at].at < WIFI_SAE_WINDOW_MS)
                {
                        peer += !memory_compare(list[at].bssid, bssid, 6);
                        list[kept++] = list[at];
                }
        if (peer >= WIFI_SAE_PEER_MOST || kept > WIFI_SAE_ALL_MOST)
                return false;
        memory_fill(list, 0, sizeof(list[0]));
        memory_copy(list[0].bssid, bssid, 6);
        list[0].at = now;
        host_state_ready();
        //      Kept if it can be: with no file the exchanges are counted an exchange at a time (SECURITY.md, Open).
        (void)host_write_file(RADIO_SAE_PATH, (p8 address_to)list, kept * sizeof(list[0]), 0600, false);
        return true;
}

static COLD bipolar nl80211_authenticate(nl80211 address_to session, p32 index,
                                         p8 address_to ssid, positive ssid_length,
                                         p8 address_to bssid, p32 frequency,
                                         p8 address_to data, positive data_length)
{
        netlink_buffer request = {0};
        p32 sequence = netlink_sequence_take();
        p32 type = NL80211_AUTHTYPE_SAE;
        bipolar sent;

        scope_exit(netlink_forget(address_of request));
        if (!nl80211_begin(address_of request, session->family, NL80211_CMD_AUTHENTICATE,
                           NLM_REQUEST, sequence))
                return -1;
        const nl80211_attr_spec attrs[] = {
            {NL80211_ATTR_IFINDEX, address_of index, 4},
            {NL80211_ATTR_WIPHY_FREQ, address_of frequency, 4},
            {NL80211_ATTR_MAC, bssid, 6},
            {NL80211_ATTR_SSID, ssid, ssid_length},
            {NL80211_ATTR_AUTH_TYPE, address_of type, 4},
            {NL80211_ATTR_AUTH_DATA, data, data_length},
        };
        if (!nl80211_put_attrs(address_of request, attrs, array_count(attrs)) || request.failed)
                return -1;
        sent = socket_send(session->handle, request.bytes, request.used, 0, 0, 0);
        return sent < 0 ? sent : 0;
}

/* The next datagram of mlme news, through visit, before the deadline: 1
   when one was read, 0 at the deadline, negative when the socket failed. */
static COLD bipolar wifi_mlme_next(nl80211 address_to session,
                                   const network_deadline address_to deadline,
                                   netlink_visitor visit, address_any context,
                                   p32 address_to port)
{
        for (;;)
        {
                netlink_buffer reply = {0};
                bipolar got = network_wait_readable_until(session->handle, deadline);

                if (got <= 0)
                        return got;
                got = netlink_receive(session->handle, address_of reply, port);
                if (got == NETWORK_INTERRUPTED || got == NETWORK_TRY_AGAIN)
                {
                        netlink_forget(address_of reply);
                        continue;
                }
                if (got >= 0)
                        netlink_each(address_of reply, visit, context);
                netlink_forget(address_of reply);
                return got < 0 ? got : 1;
        }
}

/*
        The SAE exchange with the access point bssid, through the kernel's
        authentication command, as wpa_supplicant runs it for a driver of
        mac80211's kind, which has no connect of its own for SAE: each
        commit and confirm body goes in NL80211_CMD_AUTHENTICATE (the
        transaction and status first, the algorithm left to the kernel), and
        the access point's frames come back as events of that command on the
        socket the commands go out on. A command is sent without waiting for
        its acknowledgement, since a transaction that reads to it throws the
        frame events it passes away; an error for it still comes, and is a
        refusal. 0 with the PMK and PMKID in sae, else as wifi_sae_news says.
*/
static COLD bipolar wifi_sae_exchange(nl80211 address_to session, p32 index, p8 address_to ssid,
                                      positive ssid_length, p8 address_to bssid,
                                      p32 frequency, wifi_sae address_to sae, bool h2e)
{
        wifi_sae_turn turn = {.sae = sae, .bssid = bssid, .index = index,
                              .family = session->family, .h2e = h2e};
        network_deadline deadline;
        bipolar failed;

        turn.body_length = wifi_sae_commit_body(sae, null, 0, turn.body, h2e);
        failed = nl80211_authenticate(session, index, ssid, ssid_length, bssid, frequency,
                                      turn.body, turn.body_length);
        turn.body_length = 0;
        if (failed < 0 || !network_deadline_begin(address_of deadline, WIFI_SAE_SECONDS, 0))
                return failed < 0 ? failed : -1;
        while (!turn.done)
        {
                failed = wifi_mlme_next(session, address_of deadline, wifi_sae_news,
                                        address_of turn, address_of turn.port);
                if (failed <= 0)
                        return failed < 0 ? failed : -110;
                //      What the datagram asked for, unless it also ended the
                //      exchange in a refusal.
                if (turn.body_length && (!turn.done || !turn.result))
                {
                        failed = nl80211_authenticate(session, index, ssid, ssid_length, bssid,
                                                      frequency, turn.body, turn.body_length);
                        turn.body_length = 0;
                        if (failed < 0)
                                return failed;
                }
        }
        return turn.result;
}

/* The status of an association response from bssid: false for a frame that
   is not one, or is cut short. */
static COLD bool wifi_assoc_status_read(const p8 address_to frame, positive size,
                                        const p8 address_to bssid, p16 address_to status)
{
        byte_reader reader = byte_reader_open(frame, size);
        p16 control = (p16)byte_reader_uint(&reader, 2, false);
        const p8 address_to transmitter;

        (void)byte_reader_skip(&reader, 2 + 6);
        transmitter = byte_reader_take(&reader, 6);
        (void)byte_reader_skip(&reader, 6 + 2 + 2);
        address_to status = (p16)byte_reader_uint(&reader, 2, false);
        return byte_reader_ok(&reader) &&
               ((control & 0x40ff) == 0x0010 || (control & 0x40ff) == 0x0030) &&
               !memory_compare(transmitter, bssid, 6);
}

typedef struct
{
        const p8 address_to bssid;
        p32 index;
        p16 family;
        p32 port;
        positive frames;
        bool done;
        bipolar result;
} wifi_assoc_turn;

static COLD bool wifi_assoc_news(netlink_header address_to header, address_any context)
{
        wifi_assoc_turn address_to turn = (wifi_assoc_turn address_to)context;
        p8 address_to frame;
        positive size = 0;
        p16 status = 0;
        p8 command;

        if (turn->done)
                return false;
        if (header->type == NLMSG_IS_ERROR)
        {
                if (header->port != turn->port || netlink_status(header, true) >= 0)
                        return true;
                turn->done = true;
                turn->result = -111;
                return false;
        }
        if (header->type != turn->family ||
            nl80211_find_u32(header, NL80211_ATTR_IFINDEX, 0) != turn->index)
                return true;
        command = nl80211_command(header);
        if (command == NL80211_CMD_DEAUTHENTICATE || command == NL80211_CMD_DISASSOCIATE ||
            (command == NL80211_CMD_ASSOCIATE && ++turn->frames > WIFI_SAE_FRAMES_MOST))
                turn->result = -111;
        else if (command != NL80211_CMD_ASSOCIATE)
                return true;
        else if (nl80211_attr(header, NL80211_ATTR_TIMED_OUT))
                turn->result = -110;
        else
        {
                frame = (p8 address_to)netlink_find(header, GENL_HEADER, NL80211_ATTR_FRAME,
                                                    address_of size);
                if (!frame || !wifi_assoc_status_read(frame, size, turn->bssid, address_of status))
                        return true;
                turn->result = status ? -111 : 0;
        }
        turn->done = true;
        return false;
}

/* The association that follows a finished exchange: the RSN element (with
   the PMKID the exchange made, and the RSN extension element after it for
   hash-to-element), the cipher and key management suites, and protected
   management frames required. */
static COLD bipolar nl80211_associate(nl80211 address_to session, p32 index, p8 address_to ssid,
                                      positive ssid_length, p8 address_to bssid,
                                      p32 frequency, const wifi_link address_to link)
{
        netlink_buffer request = {0};
        p32 sequence = netlink_sequence_take();
        p32 version = NL80211_WPA_VERSION_2;
        p32 ccmp = WLAN_CIPHER_CCMP;
        p32 akm = WLAN_SUITE_IEEE | link->akm;
        p32 mfp = NL80211_MFP_REQUIRED;
        wifi_assoc_turn turn = {.bssid = bssid, .index = index, .family = session->family};
        network_deadline deadline;
        bipolar sent;

        scope_exit(netlink_forget(address_of request));
        if (!nl80211_begin(address_of request, session->family, NL80211_CMD_ASSOCIATE,
                           NLM_REQUEST, sequence))
                return -1;
        const nl80211_attr_spec attrs[] = {
            {NL80211_ATTR_IFINDEX, address_of index, 4},
            {NL80211_ATTR_MAC, bssid, 6},
            {NL80211_ATTR_SSID, ssid, ssid_length},
            {NL80211_ATTR_WIPHY_FREQ, address_of frequency, 4},
            {NL80211_ATTR_IE, link->rsn, link->rsn_length},
            {NL80211_ATTR_PRIVACY, null, 0},
            {NL80211_ATTR_WPA_VERSIONS, address_of version, 4},
            {NL80211_ATTR_CIPHER_SUITES_PAIRWISE, address_of ccmp, 4},
            {NL80211_ATTR_CIPHER_SUITE_GROUP, address_of ccmp, 4},
            {NL80211_ATTR_AKM_SUITES, address_of akm, 4},
            {NL80211_ATTR_USE_MFP, address_of mfp, 4},
            {NL80211_ATTR_CONTROL_PORT, null, 0},
        };
        if (!nl80211_put_attrs(address_of request, attrs, array_count(attrs)) || request.failed)
                return -1;
        sent = socket_send(session->handle, request.bytes, request.used, 0, 0, 0);
        if (sent < 0 || !network_deadline_begin(address_of deadline, NL80211_CONNECT_SECONDS, 0))
                return sent < 0 ? sent : -1;
        while (!turn.done)
        {
                bipolar got = wifi_mlme_next(session, address_of deadline, wifi_assoc_news,
                                             address_of turn, address_of turn.port);

                if (got <= 0)
                        return got < 0 ? got : -110;
        }
        return turn.result;
}

/* Whether the wireless interface has a station: a station the kernel could
   not be asked about counts as one, so that a failed question is no reason
   to join again over a link that stands. */
static COLD bool nl80211_associated(void)
{
        nl80211 session;
        nl80211_iface iface;
        p8 mac[6];
        bool up = false;

        if (nl80211_open(address_of session) < 0)
                return false;
        bipolar asked = nl80211_interface(address_of session, address_of iface);

        //      No card at all is no station; a question that failed for
        //      another reason counts as one.
        if (asked == 0)
                up = nl80211_station(address_of session, iface.index, mac) != 0;
        else
                up = asked != -ERROR_NO_DEVICE;
        nl80211_close(address_of session);
        return up;
}

static COLD bipolar radio_bss_choose(nl80211 address_to session, p32 index,
                                     p8 address_to ssid, positive ssid_length, bool secured,
                                     p8 address_to bssid, p32 address_to frequency,
                                     bool force, p8 rules, bool key_only,
                                     wifi_rsn_caps address_to caps, wifi_seen address_to seen);
static COLD p8 radio_rules_get(p8 address_to ssid, positive ssid_length);
#define RADIO_RULES_PATH "/root/wifi.rules"
static COLD bipolar radio_rules_put(p8 address_to ssid, positive ssid_length, p8 mask, p8 what);

/*
        WPA3 with the access point chosen: the exchange counted against its
        budget, the password element (hash-to-element when the access point
        takes it, the hunt otherwise), the commit and the exchange, the
        association with the PMKID it made, and the four-way handshake on
        its PMK. The EAPOL socket is opened before the association, since
        message 1 follows it by a few milliseconds.
*/
static COLD bipolar wifi_sae_join(wifi_link address_to link, p8 address_to ssid,
                                  positive ssid_length, p8 address_to pass, positive pass_length,
                                  p8 address_to bssid, p32 frequency, bool h2e)
{
        wifi_sae sae;
        bipolar failed;
        bool made;

        scope_exit(crypto_forget(address_of sae, sizeof(sae)));
        memory_fill(address_of sae, 0, sizeof(sae));
        if (!wifi_sae_budget(bssid))
                return WIFI_REFUSE_SAE_BUDGET;
        if (link->eapol < 0)
        {
                failed = net_packet_open(link->index, ETH_P_PAE);
                if (failed < 0)
                        return failed;
                link->eapol = (b32)failed;
        }
        nl80211_disconnect(address_of link->session, link->index);
        memory_fill(link->bssid, 0, 6);
        if (h2e)
        {
                p8 point[64];

                made = wifi_sae_pt(ssid, ssid_length, pass, pass_length, point) &&
                       wifi_sae_pwe_from_pt(point, link->sta, bssid, sae.pwe);
                crypto_forget(point, sizeof(point));
        }
        else
                made = wifi_sae_pwe(pass, pass_length, link->sta, bssid, sae.pwe);
        if (!made || !wifi_sae_commit(address_of sae))
                return -1;
        failed = wifi_sae_exchange(address_of link->session, link->index, ssid, ssid_length,
                                   bssid, frequency, address_of sae, h2e);
        if (failed)
                return failed;
        memory_copy(link->pmk, sae.pmk, 32);
        link->rsn_length = (p8)wifi_rsn_build(link->rsn, 8, link->pmf, sae.pmkid);
        //      The RSN extension element, in the association request and in
        //      message 2 alike: bit 5, hash-to-element.
        if (h2e)
        {
                link->rsn[link->rsn_length++] = 244;
                link->rsn[link->rsn_length++] = 1;
                link->rsn[link->rsn_length++] = 0x20;
        }
        failed = nl80211_associate(address_of link->session, link->index, ssid, ssid_length,
                                   bssid, frequency, link);
        if (failed)
                return failed;
        memory_copy(link->bssid, bssid, 6);
        return wifi_mac_set(link->sta) ? wifi_handshake(link) : -1;
}

/*
        A join, into link: on success the link keeps its sockets and keys for
        the keeper, with a four-way handshake of its own where it needs one.
        -113 when a scan heard no network by this name. What it asks of the
        access point it chose is wifi_join_plan's, at this build's tier and
        with what the saved network has taught it; a refusal of the plan's
        is made before anything is sent. A PSK is made only once an access
        point is chosen and asks for one, since a network that is not there,
        or that is joined with WPA3, is not worth the four thousand rounds.
        force asks for a scan whenever the cache has no access point to join;
        the machine's own pass leaves that to what a scan for the name found
        lately (radio_bss_choose).
*/
/* Whether the access point the last choice made was one a join had given
   up on (radio_avoid): the best there was, behind every other. */
static bool radio_pick_given_up;

/* nl80211_join's answer when a second look found no other access point. */
#define RADIO_JOIN_NO_OTHER 1

static COLD bipolar nl80211_join_once(p8 address_to ssid, positive ssid_length,
                                      p8 address_to pass, positive pass_length, bool force,
                                      bool again, wifi_link address_to link)
{
        nl80211_iface iface;
        p8 chosen[6];
        p32 frequency = 0;
        bipolar picked = -1;
        bipolar route;
        bipolar failed;
        bipolar sequence;
        bool offload = false;
        bool secured = pass_length != 0;
        bool key_only = pass_length == 64;
        bool keyed = false;
        bool kept = false;
        p8 rules = 0;
        wifi_rsn_caps caps;
        wifi_plan plan;

        wifi_link_close(link);
        scope_exit(if (!kept) wifi_link_close(link));
        memory_fill(chosen, 0, 6);
        memory_fill(address_of caps, 0, sizeof(caps));
        memory_fill(address_of plan, 0, sizeof(plan));
        //      Whatever kept the last link's keys is gone before the station
        //      that runs this is forked (radio_join_apart): its EAPOL socket
        //      would hear this join's message 1 as a rekey.
        failed = nl80211_open(address_of link->session);
        if (!failed)
                failed = nl80211_interface(address_of link->session, address_of iface);
        if (failed < 0)
                return failed;
        link->index = iface.index;

        route = netlink_open_groups(0);
        if (route >= 0)
        {
                netlink_link_up((b32)route, iface.index);
                socket_close((b32)route);
        }

        if (iface.has_mac)
                memory_copy(link->sta, iface.mac, 6);
        else
                wifi_link_mac((string_address)iface.name, link->sta);

        if (secured)
                rules = radio_rules_get(ssid, ssid_length);
        picked = radio_bss_choose(address_of link->session, iface.index, ssid, ssid_length,
                                  secured, chosen, address_of frequency, force, rules, key_only,
                                  address_of caps, address_of link->seen);
        if (again && (picked <= 0 || radio_pick_given_up))
                return RADIO_JOIN_NO_OTHER;
        if (!picked)
                return -113;

        link->akm = 2;
        if (secured)
        {
                //      With no scan to read the access point from, what the
                //      network taught stands in for its elements: WPA2, with
                //      protection where it offered that once. WPA3 needs an
                //      access point to send its exchange to, and there is none.
                if (picked < 0)
                {
                        caps.akms = rules & WIFI_RULE_SAE ? 0 : WIFI_AKM_PSK;
                        caps.pmf = rules & (WIFI_RULE_PMF | WIFI_RULE_SAE) ? WIFI_PMF_CAPABLE : 0;
                        caps.group = WIFI_CIPHER_CCMP;
                        caps.pairwise = WIFI_CIPHER_CCMP;
                }
                failed = wifi_join_plan(MOONWATER_STRICT_NET, address_of caps, rules, key_only,
                                        address_of plan);
                if (failed < 0)
                {
                        radio_avoid_add(picked > 0 ? chosen : null);
                        return failed;
                }
                link->akm = plan.akm;
                link->pmf = plan.pmf;
        }

        //      WPA3 first where it is the plan, and WPA2 after it only where
        //      the plan allows that and the exchange came to nothing for a
        //      reason that is not the password; then the driver's own
        //      four-way handshake where it has one, and if that fails, once
        //      more with this one.
        for (;;)
        {
                link->version = wifi_key_version(link->akm);
                if (secured && link->akm == 8)
                {
                        failed = wifi_sae_join(link, ssid, ssid_length, pass, pass_length, chosen,
                                               frequency, caps.h2e != 0);
                        if (failed && failed != -13 && failed != WIFI_REFUSE_ELEMENTS &&
                            plan.fallback)
                        {
                                //      A new association, and none of the
                                //      last one's handshake.
                                link->akm = plan.fallback;
                                link->pmf = caps.pmf ? WIFI_PMF_CAPABLE | WIFI_PMF_REQUIRED : 0;
                                plan.fallback = 0;
                                link->answered = 0;
                                link->replay_set = link->installed = false;
                                link->pending_set = link->nonce_used = false;
                                link->gtk_installed = link->igtk_installed = 0;
                                memory_fill(link->replay, 0, sizeof(link->replay));
                                crypto_forget(link->pmk, sizeof(link->pmk));
                                crypto_forget(link->ptk, sizeof(link->ptk));
                                crypto_forget(link->pending, sizeof(link->pending));
                                crypto_forget(link->gtk, sizeof(link->gtk));
                                crypto_forget(link->igtk, sizeof(link->igtk));
                                if (link->eapol >= 0)
                                        socket_close(link->eapol);
                                link->eapol = -1;
                                continue;
                        }
                        break;
                }
                link->rsn_length = (p8)wifi_rsn_build(link->rsn, link->akm, link->pmf, null);
                if (secured && !keyed)
                {
                        if (!wifi_psk(ssid, ssid_length, pass, pass_length, link->pmk))
                        {
                                failed = -22;
                                break;
                        }
                        keyed = true;
                        offload = link->akm == 2 &&
                                  nl80211_psk_offload(address_of link->session, iface.wiphy);
                }
                if (secured && !offload && link->eapol < 0)
                {
                        failed = net_packet_open(iface.index, ETH_P_PAE);
                        if (failed < 0)
                                break;
                        link->eapol = (b32)failed;
                }
                nl80211_disconnect(address_of link->session, iface.index);
                memory_fill(link->bssid, 0, 6);
                sequence = nl80211_connect(address_of link->session, iface.index, ssid,
                                           ssid_length, secured ? link->pmk : null, offload,
                                           picked > 0 ? chosen : null, frequency, link);
                failed = sequence < 0 ? sequence
                                      : nl80211_wait_associated(address_of link->session,
                                                                (p32)sequence, iface.index,
                                                                link->bssid);
                if (!failed && !wifi_mac_set(link->bssid))
                        nl80211_station(address_of link->session, iface.index, link->bssid);
                if (!failed && link->eapol >= 0)
                        failed = wifi_mac_set(link->sta) && wifi_mac_set(link->bssid)
                                     ? wifi_handshake(link)
                                     : -1;
                if (!failed || !secured || !offload)
                        break;
                offload = false;
        }

        if (failed)
        {
                nl80211_disconnect(address_of link->session, iface.index);
                //      Not this one again first, if another answers to the name.
                radio_avoid_add(wifi_mac_set(link->bssid) ? link->bssid
                                : picked > 0          ? chosen
                                                      : null);
        }
        else if (secured)
        {
                //      What this join has taught the network: an access point
                //      by its name protects its management frames, and WPA3
                //      is what it is joined with -- or what the access point
                //      said, under the key, to keep to (Transition Disable).
                p8 learned = (link->pmf && caps.pmf ? WIFI_RULE_PMF : 0) |
                             (link->akm == 8 ||
                                      (link->transition_disable & WIFI_TRANSITION_WPA3_PERSONAL)
                                  ? WIFI_RULE_SAE
                                  : 0);

                //      The join stands whatever becomes of this, but a rule that
                //      is not kept is the next join's downgrade: said, with the
                //      file it was for. (From a station the put is a question to
                //      the broker, which keeps the file and answers 0.)
                if (learned & ~rules)
                {
                        bipolar put = radio_rules_put(ssid, ssid_length, learned, learned);

                        if (put < 0)
                                host_fail(RADIO_RULES_PATH, put);
                }
        }
        kept = !failed;
        return failed;
}

/*
        A join. A failure at an access point puts it behind every other by the
        name (radio_avoid_add), and in the machine's own pass the air is looked
        at once more at once: another by the name is asked now, not after the
        wait the failure earns. A louder twin that refuses every join -- WPA3
        under a password its owner never chose, say -- otherwise held the real
        one off for as long as that wait grew, a minute at the last. A join a
        person asked for answers at once and pays for no second look, but for
        an access point that never answered at all: one heard a moment ago
        and gone since is still in the kernel's list of what was heard, and
        the add said "did not associate" with the network itself in range,
        as wpa_supplicant goes on to the next. A wrong password at the one
        access point there is finds nothing else and waits as it did.
*/
static COLD bipolar nl80211_join(p8 address_to ssid, positive ssid_length, p8 address_to pass,
                                 positive pass_length, bool force, wifi_link address_to link)
{
        bipolar failed = nl80211_join_once(ssid, ssid_length, pass, pass_length, force, false,
                                           link);
        bipolar again;

        if (!failed || (force && failed != -110) || failed == -19 || failed == -113 ||
            failed == WIFI_REFUSE_SAE_BUDGET)
                return failed;
        again = nl80211_join_once(ssid, ssid_length, pass, pass_length, force, true, link);
        return again == RADIO_JOIN_NO_OTHER ? failed : again;
}

static bipolar nl80211_disconnect(nl80211 address_to session, p32 index)
{
        netlink_buffer request = {0};
        p32 sequence = netlink_sequence_take();

        if (!nl80211_begin(address_of request, session->family,
                           NL80211_CMD_DISCONNECT, NLM_REQUEST | NLM_ACK,
                           sequence))
                return -1;

        nl80211_attribute_u32(address_of request, NL80211_ATTR_IFINDEX, index);
        return netlink_transact(session->handle, address_of request, sequence,
                                null, null);
}

#endif


