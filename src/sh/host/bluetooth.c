/*
        src/sh/host/bluetooth.c: Bluetooth devices paired and kept, through the kernel's own sockets and no daemon: the
        verbs bluetooth (the radio, the paired devices and the names kept), scan, pair and forget, the paired devices and
        their keys on the data partition, and the keeper, the process that lets a paired pad back in when it wakes and
        carries a Bluetooth LE pad's reports to the kernel.

        A run of src/sh/host.c, included after radio.c, whose switches, lock and words it uses; host_main's table names
        host_bluetooth, at the end of this file.

        A classic pad (HID over BR/EDR) ends in the kernel's HIDP: the pairing reads the pad's report descriptor and its
        vendor and product from its SDP records, connects its control and interrupt channels and hands both to HIDP, and
        from then on its reports go from the radio to the kernel's driver for it with nothing here in the way. The keeper
        only accepts the two channels again when the pad pages the machine. An LE pad (HID over GATT) is read here: the
        keeper finds the pad's HID service, makes a uhid device of its report map and writes each notification to it as it
        comes. The kernel answers the pad's link key or long term key itself once they are loaded, which the keeper does
        when it starts and whenever the controller comes back.

        Latency. A classic link is kept out of sniff mode: the controller's default link policy, and each pad's link, allow
        a role switch and nothing else, so a pad's reports are not held to a sniff interval and the controller refuses a
        pad that asks for one. An LE link asks for the shortest connection interval, 7.5 ms, with no peripheral latency:
        the kernel's defaults for every LE connection, each pad's own parameters, and a connection already up when it is
        bridged.

        The paired devices are /root/bluetooth.pads, root's alone since it holds their keys: one line a device, its
        address, its kind (br, le, le-random), its link key, its two long term keys and its identity key as the kernel gave
        them (mgmt's records, in hex), its ids (vendor, product, version, parser, country, subclass), its report
        descriptor, and its name. /etc is made again at every boot, and /root is the data partition's.
*/

#define BT_KEPT "/root/bluetooth.pads"
#define BT_FOUND NET_STATE_DIR "/bluetooth.scan"
#define BT_KEEPER NET_STATE_DIR "/bluetooth.keeper"
#define BT_WAKE NET_STATE_DIR "/bluetooth.wake"

#define BT_FAMILY 31            // AF_BLUETOOTH
#define BT_PROTO_L2CAP 0
#define BT_PROTO_HCI 1
#define BT_PROTO_HIDP 6
#define BT_SEQPACKET 5
#define BT_LEVEL 274            // SOL_BLUETOOTH
#define BT_SECURITY 4
#define BT_SECURE_NONE 1        // BT_SECURITY_LOW: SDP
#define BT_SECURE 2             // BT_SECURITY_MEDIUM: an authenticated and encrypted link, the pad's key asked
#define BT_CHANNEL_RAW 0
#define BT_CHANNEL_CONTROL 3
#define BT_NO_INDEX 0xffff
#define BT_NO_SIGNAL 0x4000     // MSG_NOSIGNAL
#define BT_HIDP_ADD 0x400448c8  // HIDPCONNADD, _IOW('H', 200, int)
#define BT_CONN_INFO 0x800448d5 // HCIGETCONNINFO, _IOR('H', 213, int)
#define BT_BREDR 0
#define BT_LE_PUBLIC 1
#define BT_LE_RANDOM 2
#define BT_PSM_SDP 1
#define BT_PSM_CONTROL 0x11
#define BT_PSM_INTERRUPT 0x13
#define BT_CID_ATT 4
#define BT_MAP_MOST 4096        // HID_MAX_DESCRIPTOR_SIZE, the most a report descriptor the kernel takes is
#define BT_ATT_MTU 517
#define BT_INTERVAL 6           // 7.5 ms, in the 1.25 ms units of an LE connection interval
#define BT_TIMEOUT 42           // the kernel's own LE supervision timeout, 420 ms
#define BT_POLICY_SWITCH 1      // HCI_LP_RSWITCH, and no HCI_LP_SNIFF
#define BT_IO_NONE 3            // NoInputNoOutput: pads have no keys to type a number on, so pairing is just works

enum
{
        BT_READ_INDEX_LIST = 0x0003, BT_READ_INFO = 0x0004, BT_SET_POWERED = 0x0005, BT_SET_CONNECTABLE = 0x0007,
        BT_SET_BONDABLE = 0x0009, BT_SET_SSP = 0x000b, BT_SET_LE = 0x000d, BT_LOAD_LINK_KEYS = 0x0012,
        BT_LOAD_LTKS = 0x0013, BT_GET_CONNECTIONS = 0x0015, BT_PIN_REPLY = 0x0016, BT_SET_IO = 0x0018,
        BT_PAIR = 0x0019, BT_UNPAIR = 0x001b, BT_CONFIRM_REPLY = 0x001c, BT_START_DISCOVERY = 0x0023,
        BT_STOP_DISCOVERY = 0x0024, BT_CONFIRM_NAME = 0x0025, BT_SET_SECURE = 0x002d, BT_LOAD_IRKS = 0x0030,
        BT_ADD_DEVICE = 0x0033, BT_REMOVE_DEVICE = 0x0034, BT_LOAD_PARAMS = 0x0035, BT_SET_DEFAULTS = 0x004b,
};
enum
{
        BT_EV_COMPLETE = 0x0001, BT_EV_STATUS = 0x0002, BT_EV_INDEX_ADDED = 0x0004, BT_EV_INDEX_REMOVED = 0x0005,
        BT_EV_SETTINGS = 0x0006, BT_EV_LINK_KEY = 0x0009, BT_EV_LTK = 0x000a, BT_EV_CONNECTED = 0x000b,
        BT_EV_DISCONNECTED = 0x000c, BT_EV_PIN = 0x000e, BT_EV_CONFIRM = 0x000f, BT_EV_FOUND = 0x0012,
        BT_EV_DISCOVERING = 0x0013, BT_EV_IRK = 0x0018,
};
// The controller's settings (mgmt's current and supported settings words).
enum { BT_POWERED = 0x1, BT_HAS_SSP = 0x40, BT_HAS_BREDR = 0x80, BT_HAS_LE = 0x200, BT_HAS_SECURE = 0x800 };
// What a device record holds, in its keys word.
enum { BT_HAS_KEY = 1, BT_HAS_LTK = 2, BT_HAS_LTK2 = 4, BT_HAS_IRK = 8 };

static p16 bt_le16(const p8 address_to at) { return (p16)(at[0] | at[1] << 8); }
static p32 bt_le32(const p8 address_to at) { return (p32)at[0] | (p32)at[1] << 8 | (p32)at[2] << 16 | (p32)at[3] << 24; }
static fn bt_put16(p8 address_to at, p32 value) { at[0] = (p8)value, at[1] = (p8)(value >> 8); }
static fn bt_put32(p8 address_to at, p32 value) { bt_put16(at, value), bt_put16(at + 2, value >> 16); }
static p16 bt_be16(const p8 address_to at) { return (p16)(at[0] << 8 | at[1]); }
static fn bt_put_be16(p8 address_to at, p32 value) { at[0] = (p8)(value >> 8), at[1] = (p8)value; }

/*
        Addresses. The kernel's are six bytes, least significant first; a person's are the six as hex pairs, most
        significant first, with colons: upper case as the verbs say them, lower case as the kernel's HID devices name
        a Bluetooth device's uniq.
*/
static fn bt_address_text(p8 address_to text, const p8 address_to address, bool upper)
{
        string_address digits = upper ? (string_address) "0123456789ABCDEF" : (string_address) "0123456789abcdef";

        for (positive at = 0; at < 6; at++)
        {
                p8 byte = address[5 - at];

                text[at * 3] = digits[byte >> 4], text[at * 3 + 1] = digits[byte & 15];
                text[at * 3 + 2] = at < 5 ? ':' : end;
        }
}

static b32 bt_nibble(p8 digit)
{
        p8 low = digit | 32;

        return digit >= '0' && digit <= '9' ? digit - '0' : low >= 'a' && low <= 'f' ? low - 'a' + 10 : -1;
}

static bool bt_address_read(const p8 address_to text, positive length, p8 address_to address)
{
        if (length != 17)
                return false;
        for (positive at = 0; at < 6; at++)
        {
                b32 high = bt_nibble(text[at * 3]), low = bt_nibble(text[at * 3 + 1]);

                if (high < 0 || low < 0 || (at < 5 && text[at * 3 + 2] != ':'))
                        return false;
                address[5 - at] = (p8)(high << 4 | low);
        }
        return true;
}

// length bytes of hex at text into into, all or nothing.
static bool bt_hex_read(const p8 address_to text, positive digits, p8 address_to into, positive length)
{
        if (digits != length * 2)
                return false;
        for (positive at = 0; at < length; at++)
        {
                b32 high = bt_nibble(text[at * 2]), low = bt_nibble(text[at * 2 + 1]);

                if (high < 0 || low < 0)
                        return false;
                into[at] = (p8)(high << 4 | low);
        }
        return true;
}

static string_address bt_kind_word(p8 type) { return type == BT_BREDR ? "br" : type == BT_LE_PUBLIC ? "le" : "le-random"; }

static bipolar bt_kind_read(const p8 address_to text, positive length)
{
        for (p8 type = BT_BREDR; type <= BT_LE_RANDOM; type++)
                if (string_length(bt_kind_word(type)) == length && !memory_compare(text, bt_kind_word(type), length))
                        return type;
        return -1;
}

/* ---- a growing text, which nothing written here outgrows ---- */

typedef struct
{
        p8 address_to bytes;
        positive used, room;
} bt_text;

static bool bt_put(bt_text address_to text, const address_any bytes, positive length)
{
        if (!array_store_reserve(text->bytes, text->room, text->used, text->used + length + 1, 4096))
                return false;
        memory_copy(text->bytes + text->used, bytes, length);
        text->used += length;
        text->bytes[text->used] = end;
        return true;
}

static bool bt_put_hex(bt_text address_to text, const p8 address_to bytes, positive length)
{
        if (!length)
                return bt_put(text, "-", 1);
        if (!array_store_reserve(text->bytes, text->room, text->used, text->used + length * 2 + 1, 4096))
                return false;
        text->used += memory_into_hex(text->bytes + text->used, (address_any)bytes, length);
        text->bytes[text->used] = end;
        return true;
}

static fn bt_text_free(bt_text address_to text) { array_store_release(text->bytes, text->room, text->used); }

/* ---- the paired devices ---- */

typedef struct
{
        p8 address[6], type, keys;
        p8 key[25], ltk[2][36], irk[23]; // mgmt_link_key_info, mgmt_ltk_info and mgmt_irk_info, as the kernel gave them
        p16 vendor, product, version, parser;
        p8 country, subclass;
        positive map_at, map_size;      // the report descriptor, in the book's maps
        p8 name[249];
} bt_pad;

typedef struct
{
        bt_pad address_to pad;
        positive pads, pad_room;
        p8 address_to maps;
        positive maps_used, maps_room;
} bt_book;

static fn bt_book_free(bt_book address_to book)
{
        array_store_release(book->pad, book->pad_room, book->pads);
        array_store_release(book->maps, book->maps_room, book->maps_used);
}

static bt_pad address_to bt_book_find(bt_book address_to book, const p8 address_to address)
{
        for (positive at = 0; at < book->pads; at++)
                if (!memory_compare(book->pad[at].address, address, 6))
                        return book->pad + at;
        return null;
}

static bt_pad address_to bt_book_add(bt_book address_to book, const p8 address_to address, p8 type)
{
        bt_pad address_to pad = bt_book_find(book, address);

        if (pad)
                return pad;
        if (!array_store_reserve(book->pad, book->pad_room, book->pads, book->pads + 1, 8))
                return null;
        pad = book->pad + book->pads++;
        memory_zero(pad, sizeof(*pad));
        memory_copy(pad->address, address, 6);
        pad->type = type;
        return pad;
}

static bool bt_book_map(bt_book address_to book, bt_pad address_to pad, const p8 address_to map, positive size)
{
        if (size > BT_MAP_MOST)
                size = BT_MAP_MOST;
        if (!array_store_reserve(book->maps, book->maps_room, book->maps_used, book->maps_used + size, 4096))
                return false;
        memory_copy(book->maps + book->maps_used, map, size);
        pad->map_at = book->maps_used, pad->map_size = size;
        book->maps_used += size;
        return true;
}

// A name as it is kept: one line of it, a byte that would end the line or move the cursor taken for a question mark.
static fn bt_name_keep(p8 address_to into, const p8 address_to name, positive length)
{
        if (length > 248)
                length = 248;
        for (positive at = 0; at < length; at++)
                into[at] = name[at] < 32 || name[at] == 127 ? '?' : name[at];
        into[length] = end;
}

// The next word of a line at at (one space ends it), its start and length; at is left past the space.
static bool bt_word(const p8 address_to line, positive size, positive address_to at, positive address_to start, positive address_to length)
{
        if (*at >= size)
                return false;
        *start = *at;
        *length = memory_span_without_byte((address_any)(line + *at), ' ', size - *at);
        *at += *length + (*at + *length < size);
        return true;
}

static bool bt_line_read(bt_book address_to book, const p8 address_to line, positive size)
{
        positive at = 0, start[8], length[8];
        p8 address[6], ids[10], map[BT_MAP_MOST];
        bipolar type;
        bt_pad address_to pad;

        //      A line that is no device's is passed over (false is for no memory alone): one bad line does not take the
        //      others with it, and the keeper does not stop and start again over it.
        for (positive word = 0; word < 8; word++)
                if (!bt_word(line, size, address_of at, start + word, length + word))
                        return true;
        if (!bt_address_read(line + start[0], length[0], address) || (type = bt_kind_read(line + start[1], length[1])) < 0)
                return true;
        if (!(pad = bt_book_add(book, address, (p8)type)))
                return false;
        if (bt_hex_read(line + start[2], length[2], pad->key, sizeof(pad->key)))
                pad->keys |= BT_HAS_KEY;
        if (bt_hex_read(line + start[3], length[3], pad->ltk[0], sizeof(pad->ltk[0])))
                pad->keys |= BT_HAS_LTK;
        if (bt_hex_read(line + start[4], length[4], pad->ltk[1], sizeof(pad->ltk[1])))
                pad->keys |= BT_HAS_LTK2;
        if (bt_hex_read(line + start[5], length[5], pad->irk, sizeof(pad->irk)))
                pad->keys |= BT_HAS_IRK;
        if (bt_hex_read(line + start[6], length[6], ids, sizeof(ids)))
        {
                pad->vendor = bt_le16(ids), pad->product = bt_le16(ids + 2), pad->version = bt_le16(ids + 4);
                pad->parser = bt_le16(ids + 6), pad->country = ids[8], pad->subclass = ids[9];
        }
        if (length[7] > 1 && length[7] % 2 == 0 && length[7] / 2 <= BT_MAP_MOST &&
            bt_hex_read(line + start[7], length[7], map, length[7] / 2) && !bt_book_map(book, pad, map, length[7] / 2))
                return false;
        bt_name_keep(pad->name, line + at, size - at);
        return true;
}

/* A state file whole, however long it has grown, into text: its length, or the error (a file that does not fit in what
   could be had is one, and never a part of it). */
static bipolar bt_read_whole(string_address path, bt_text address_to text)
{
        for (positive want = 16384;; want = text->room * 2)
        {
                bipolar got;

                if (!array_store_reserve(text->bytes, text->room, 0, want, want))
                        return -ENOMEM;
                got = host_read_state(path, text->bytes, text->room);
                if (got < 0 || (positive)got + 1 < text->room)
                        return got;
        }
}

/* The paired devices, as kept: 0, or the error of a book that is there and does not read (one that is not there is
   empty). */
static bipolar bt_book_read(bt_book address_to book)
{
        bt_text text = {0};
        bipolar got;
        positive at = 0, start, length;

        memory_zero(book, sizeof(*book));
        got = bt_read_whole(BT_KEPT, address_of text);
        if (got == -ENOENT)
                got = 0;
        while (got > 0 && host_line_next(text.bytes, (positive)got, address_of at, address_of start, address_of length))
                if (length && !bt_line_read(book, text.bytes + start, length))
                        got = -ENOMEM;
        if (text.bytes)
                crypto_forget(text.bytes, text.room);
        bt_text_free(address_of text);
        return got < 0 ? got : 0;
}

static bipolar bt_book_write(bt_book address_to book)
{
        bt_text text = {0};
        p8 ids[10], address[18];
        bool done = true;
        bipolar wrote;

        for (positive at = 0; done && at < book->pads; at++)
        {
                bt_pad address_to pad = book->pad + at;
                string_address kind = bt_kind_word(pad->type);

                bt_put16(ids, pad->vendor);
                bt_put16(ids + 2, pad->product);
                bt_put16(ids + 4, pad->version);
                bt_put16(ids + 6, pad->parser);
                ids[8] = pad->country;
                ids[9] = pad->subclass;
                bt_address_text(address, pad->address, true);
                done = bt_put(&text, address, 17) && bt_put(&text, " ", 1) && bt_put(&text, kind, string_length(kind)) &&
                       bt_put(&text, " ", 1) && bt_put_hex(&text, pad->key, pad->keys & BT_HAS_KEY ? sizeof(pad->key) : 0) &&
                       bt_put(&text, " ", 1) && bt_put_hex(&text, pad->ltk[0], pad->keys & BT_HAS_LTK ? sizeof(pad->ltk[0]) : 0) &&
                       bt_put(&text, " ", 1) && bt_put_hex(&text, pad->ltk[1], pad->keys & BT_HAS_LTK2 ? sizeof(pad->ltk[1]) : 0) &&
                       bt_put(&text, " ", 1) && bt_put_hex(&text, pad->irk, pad->keys & BT_HAS_IRK ? sizeof(pad->irk) : 0) &&
                       bt_put(&text, " ", 1) && bt_put_hex(&text, ids, sizeof(ids)) && bt_put(&text, " ", 1) &&
                       bt_put_hex(&text, book->maps + pad->map_at, pad->map_size) && bt_put(&text, " ", 1) &&
                       bt_put(&text, pad->name, string_length(pad->name)) && bt_put(&text, "\n", 1);
        }
        if (!done)
        {
                bt_text_free(address_of text);
                return -ENOMEM;
        }
        wrote = book->pads ? host_write_file(BT_KEPT, text.bytes, text.used, 0600, true) : system_remove_at(AT_FDCWD, BT_KEPT, 0);
        if (text.bytes)
                crypto_forget(text.bytes, text.used);
        bt_text_free(address_of text);
        return wrote == -ENOENT ? 0 : wrote;
}

/* ---- the kernel's Bluetooth management socket ---- */

typedef struct bt_link bt_link;
typedef fn (address_to bt_heard)(bt_link address_to link, p16 event, const p8 address_to data, positive size);
struct bt_link
{
        bipolar mgmt;
        p16 index;
        p32 settings, supported;
        p8 own[6]; // this controller's address
        bt_heard heard;  // what an event that answers no command is given to
        address_any about;
};

static p8 bt_said[6 + 65535]; // one event: mgmt's header and the most its length can say
static p8 address_to bt_out;
static positive bt_out_room;

static bipolar bt_socket(positive kind, positive protocol)
{
        return system_call_3(syscall(socket), BT_FAMILY, kind | SOCK_CLOEXEC, protocol);
}

static bipolar bt_hci_open(p16 index, p16 channel)
{
        p8 place[6];
        bipolar handle = bt_socket(SOCK_RAW, BT_PROTO_HCI);
        bipolar bound;

        if (handle < 0)
                return handle;
        bt_put16(place, BT_FAMILY);
        bt_put16(place + 2, index);
        bt_put16(place + 4, channel);
        bound = system_call_3(syscall(bind), (positive)handle, (positive)place, sizeof(place));
        if (bound < 0)
        {
                system_close(handle);
                return bound;
        }
        return handle;
}

static bipolar bt_send(bt_link address_to link, p16 opcode, const address_any data, positive size)
{
        if (size > 65535 || !array_store_reserve(bt_out, bt_out_room, 0, 6 + size, 4096))
                return -ENOMEM;
        bt_put16(bt_out, opcode);
        bt_put16(bt_out + 2, link->index);
        bt_put16(bt_out + 4, (p32)size);
        memory_copy(bt_out + 6, data, size);
        return system_call_3(syscall(write), (positive)link->mgmt, (positive)bt_out, 6 + size);
}

// One event into bt_said by the deadline: its length, 0 when the deadline came first, or the error.
// With no deadline the socket is readable already (the keeper's poll said so), and one read is made.
static bipolar bt_hear(bt_link address_to link, const network_deadline address_to deadline)
{
        for (;;)
        {
                bipolar ready = deadline ? network_wait_readable_until(link->mgmt, deadline) : 1;
                bipolar got;

                if (ready <= 0)
                        return ready;
                got = system_call_3(syscall(read), (positive)link->mgmt, (positive)bt_said, sizeof(bt_said));
                if (got >= 6 && 6 + bt_le16(bt_said + 4) <= (positive)got)
                        return got;
                if (got < 0 && got != -EAGAIN && got != -EINTR)
                        return got;
                if (!deadline)
                        return 0;
        }
}

static fn bt_heard_pass(bt_link address_to link)
{
        p16 index = bt_le16(bt_said + 2), event = bt_le16(bt_said);

        if (link->heard && event != BT_EV_COMPLETE && event != BT_EV_STATUS &&
            (index == link->index || event == BT_EV_INDEX_ADDED || event == BT_EV_INDEX_REMOVED))
                link->heard(link, event, bt_said + 6, bt_le16(bt_said + 4));
}

/* A command and its answer: the status (0 done, mgmt's code otherwise), with the answer's parameters in reply when
   it is room enough, or a negative error when nothing answered by the time. What is heard meanwhile goes on to
   link->heard as it comes. */
static bipolar bt_ask(bt_link address_to link, p16 opcode, const address_any data, positive size, p8 address_to reply,
                      positive room, positive address_to got, positive milliseconds)
{
        network_deadline deadline;
        bipolar sent = bt_send(link, opcode, data, size);

        if (got)
                *got = 0;
        if (sent < 0)
                return sent;
        if (!network_deadline_begin(address_of deadline, milliseconds / 1000, milliseconds % 1000 * 1000000))
                return -EINVAL;
        for (;;)
        {
                bipolar heard = bt_hear(link, address_of deadline);
                p16 event, length;

                if (heard <= 0)
                        return heard ? heard : -ETIMEDOUT;
                event = bt_le16(bt_said), length = bt_le16(bt_said + 4);
                if ((event == BT_EV_COMPLETE || event == BT_EV_STATUS) && length >= 3 && bt_le16(bt_said + 6) == opcode &&
                    bt_le16(bt_said + 2) == link->index)
                {
                        if (event == BT_EV_COMPLETE && reply && got)
                        {
                                *got = length - 3u < room ? length - 3u : room;
                                memory_copy(reply, bt_said + 9, *got);
                        }
                        return bt_said[8];
                }
                bt_heard_pass(link);
        }
}

static bipolar bt_say(bt_link address_to link, p16 opcode, const address_any data, positive size)
{
        return bt_ask(link, opcode, data, size, null, 0, null, 3000);
}

// An address as mgmt says one: its six bytes and its kind.
static fn bt_place(p8 address_to into, const p8 address_to address, p8 type)
{
        memory_copy(into, address, 6);
        into[6] = type;
}

/* One HCI command on the controller, as root may send one through a raw socket: what mgmt has no word for. The
   kernel reads the controller's answer as it reads its own (a default link policy written is the one it keeps). */
static fn bt_hci_command(p16 index, p16 opcode, const p8 address_to data, p8 size)
{
        p8 packet[4 + 255];
        bipolar handle = bt_hci_open(index, BT_CHANNEL_RAW);

        if (handle < 0)
                return;
        packet[0] = 1, bt_put16(packet + 1, opcode), packet[3] = size;
        memory_copy(packet + 4, data, size);
        if (system_call_3(syscall(write), (positive)handle, (positive)packet, 4u + size) < 0)
                packet[0] = 0;
        system_close(handle);
}

// The handle of the link to address (ACL 1, LE 0x80), or -1 when there is none.
static b32 bt_link_handle(p16 index, const p8 address_to address, p8 kind)
{
        p8 asked[8 + 16] = {0};
        bipolar handle = bt_hci_open(index, BT_CHANNEL_RAW);
        bipolar told;

        if (handle < 0)
                return -1;
        memory_copy(asked, address, 6);
        asked[6] = kind;
        told = system_control(handle, BT_CONN_INFO, (positive)asked);
        system_close(handle);
        return told < 0 ? -1 : (b32)bt_le16(asked + 8);
}

/* A pad's link at the pace of its reports. A classic one is taken out of sniff mode and kept out of it (its link
   policy a role switch alone); an LE one is asked for 7.5 ms with no peripheral latency. */
static fn bt_link_awake(bt_link address_to link, const p8 address_to address, p8 type)
{
        p8 asked[14] = {0};
        b32 handle = bt_link_handle(link->index, address, type == BT_BREDR ? 1 : 0x80);

        if (handle < 0)
                return;
        bt_put16(asked, (p32)handle);
        if (type == BT_BREDR)
        {
                bt_put16(asked + 2, BT_POLICY_SWITCH);
                bt_hci_command(link->index, 0x080d, asked, 4); // Write Link Policy Settings
                bt_hci_command(link->index, 0x0804, asked, 2); // Exit Sniff Mode: refused, and harmless, when not in it
                return;
        }
        bt_put16(asked + 2, BT_INTERVAL);
        bt_put16(asked + 4, BT_INTERVAL);
        bt_put16(asked + 6, 0);
        bt_put16(asked + 8, BT_TIMEOUT);
        bt_hci_command(link->index, 0x2013, asked, 14); // LE Connection Update
}

/* The first controller, powered and set for pads: just works pairing that keeps its keys, Secure Simple Pairing and
   Secure Connections where the controller has them, LE where it has it, not connectable to anyone (a paired pad is
   let in by the accept list, mgmt's devices), LE links at 7.5 ms, and classic links without sniff mode. 0 when it is
   ready, or the reason it is not (a negative error, or mgmt's status). */
static bipolar bt_ready(bt_link address_to link)
{
        static const p8 defaults[] = {0x17, 0x00, 2, BT_INTERVAL, 0, 0x18, 0x00, 2, BT_INTERVAL, 0, 0x19, 0x00, 2, 0, 0};
        p8 reply[280], on = 1, off = 0, io = BT_IO_NONE, policy[2] = {BT_POLICY_SWITCH, 0};
        positive got = 0;
        bipolar status;

        link->index = BT_NO_INDEX;
        status = bt_ask(link, BT_READ_INDEX_LIST, null, 0, reply, sizeof(reply), address_of got, 3000);
        if (status)
                return status;
        if (got < 4 || !bt_le16(reply))
                return -ENODEV;
        link->index = bt_le16(reply + 2);
        status = bt_ask(link, BT_READ_INFO, null, 0, reply, sizeof(reply), address_of got, 3000);
        if (status || got < 17)
                return status ? status : -EIO;
        memory_copy(link->own, reply, 6);
        link->supported = bt_le32(reply + 9), link->settings = bt_le32(reply + 13);
        if ((status = bt_say(link, BT_SET_IO, address_of io, 1)) || (status = bt_say(link, BT_SET_BONDABLE, address_of on, 1)))
                return status;
        if (link->supported & BT_HAS_SSP && (status = bt_say(link, BT_SET_SSP, address_of on, 1)))
                return status;
        if (link->supported & BT_HAS_LE && (status = bt_say(link, BT_SET_LE, address_of on, 1)))
                return status;
        if (link->supported & BT_HAS_SECURE && (status = bt_say(link, BT_SET_SECURE, address_of on, 1)))
                return status;
        if (link->supported & BT_HAS_BREDR && (status = bt_say(link, BT_SET_CONNECTABLE, address_of off, 1)))
                return status;
        //      Kernels before mgmt's default system configuration have no such command; the links are then the
        //      kernel's own default interval, and that is no reason to refuse a pad.
        status = bt_say(link, BT_SET_DEFAULTS, defaults, sizeof(defaults));
        status = bt_ask(link, BT_SET_POWERED, address_of on, 1, reply, sizeof(reply), address_of got, 10000);
        if (status)
                return status;
        link->settings = got >= 4 ? bt_le32(reply) : link->settings | BT_POWERED;
        //      Written after the power comes up, since the kernel writes its own policy (sniff allowed) as it powers on.
        if (link->supported & BT_HAS_BREDR)
                bt_hci_command(link->index, 0x080f, policy, 2); // Write Default Link Policy Settings
        return 0;
}

static bipolar bt_open(bt_link address_to link)
{
        memory_zero(link, sizeof(*link));
        link->mgmt = bt_hci_open(BT_NO_INDEX, BT_CHANNEL_CONTROL);
        return link->mgmt < 0 ? link->mgmt : 0;
}

static bool bt_controller_is(void);

/* Why a verb could not reach the controller, said: the radio switched off, none there, or what the kernel said. */
static b32 bt_refuse(bipolar why)
{
        if (radio_kept(NET_BLUETOOTH_POWER) == 0)
                return host_refuse("bluetooth is off: moonwater bluetooth on raises it\n");
        if (why == -ENODEV || why == -EAFNOSUPPORT || !bt_controller_is())
                return host_refuse("no Bluetooth radio is here: none is plugged in, it has no driver, or the device "
                                   "policy holds it (moonwater device list)\n");
        if (why == 0x12)
                return host_refuse("bluetooth is blocked (rfkill): moonwater bluetooth on lifts it\n");
        if (why > 0)
                return host_refuse("the Bluetooth controller refused, mgmt status %p\n", (positive)why);
        return host_fail("bluetooth", why);
}

/* The keys a link was given, as the kernel hands them out (each event's own record, and whether to keep it), into pad.
   An identity key also names the pad's identity address, which is the one kept in place of the private one it used. */
static bool bt_keys_take(bt_pad address_to pad, p16 event, const p8 address_to data, positive size)
{
        if (event == BT_EV_LINK_KEY && size >= 26 && data[0] && !memory_compare(data + 1, pad->address, 6))
        {
                memory_copy(pad->key, data + 1, sizeof(pad->key));
                pad->keys |= BT_HAS_KEY;
                return true;
        }
        if (event == BT_EV_LTK && size >= 37 && data[0] && !memory_compare(data + 1, pad->address, 6))
        {
                //      One key for each role the link may take (mgmt's central byte), two at most.
                positive slot = data[9] ? 0 : 1;

                memory_copy(pad->ltk[slot], data + 1, sizeof(pad->ltk[slot]));
                pad->keys |= slot ? BT_HAS_LTK2 : BT_HAS_LTK;
                return true;
        }
        if (event == BT_EV_IRK && size >= 30 && data[0] &&
            (!memory_compare(data + 1, pad->address, 6) || !memory_compare(data + 7, pad->address, 6)))
        {
                memory_copy(pad->irk, data + 7, sizeof(pad->irk));
                pad->keys |= BT_HAS_IRK;
                for (positive slot = 0; slot < 2; slot++)
                        memory_copy(pad->ltk[slot], data + 7, 7);
                memory_copy(pad->address, data + 7, 6);
                pad->type = data[13];
                return true;
        }
        return false;
}

/* Every pad's keys to the kernel, which answers a pad's link key and long term key request from them by itself, and
   every pad let in: a classic pad to page the machine (the accept list), an LE pad connected whenever it advertises,
   at the pace bt_ready set. Identity keys first, so the long term keys of a pad with a private address are found. */
static fn bt_keys_load(bt_link address_to link, bt_book address_to book)
{
        bt_text keys = {0}, ltks = {0}, irks = {0}, params = {0};
        p8 count[2] = {0}, debug = 0;
        positive key_count = 0, ltk_count = 0, irk_count = 0, param_count = 0;
        bool held = bt_put(&keys, address_of debug, 1) && bt_put(&keys, count, 2) && bt_put(&ltks, count, 2) &&
                    bt_put(&irks, count, 2) && bt_put(&params, count, 2);

        for (positive at = 0; held && at < book->pads; at++)
        {
                bt_pad address_to pad = book->pad + at;
                p8 param[15];

                if (pad->keys & BT_HAS_KEY)
                        held = bt_put(&keys, pad->key, sizeof(pad->key)), key_count++;
                if (held && pad->keys & BT_HAS_LTK)
                        held = bt_put(&ltks, pad->ltk[0], sizeof(pad->ltk[0])), ltk_count++;
                if (held && pad->keys & BT_HAS_LTK2)
                        held = bt_put(&ltks, pad->ltk[1], sizeof(pad->ltk[1])), ltk_count++;
                if (held && pad->keys & BT_HAS_IRK)
                        held = bt_put(&irks, pad->irk, sizeof(pad->irk)), irk_count++;
                if (held && pad->type != BT_BREDR)
                {
                        bt_place(param, pad->address, pad->type);
                        bt_put16(param + 7, BT_INTERVAL);
                        bt_put16(param + 9, BT_INTERVAL);
                        bt_put16(param + 11, 0);
                        bt_put16(param + 13, BT_TIMEOUT);
                        held = bt_put(&params, param, sizeof(param)), param_count++;
                }
        }
        if (held)
        {
                bt_put16(keys.bytes + 1, (p32)key_count);
                bt_put16(ltks.bytes, (p32)ltk_count);
                bt_put16(irks.bytes, (p32)irk_count);
                bt_put16(params.bytes, (p32)param_count);
                if (link->supported & BT_HAS_LE)
                {
                        held = bt_say(link, BT_LOAD_IRKS, irks.bytes, irks.used) == 0;
                        held = bt_say(link, BT_LOAD_LTKS, ltks.bytes, ltks.used) == 0 && held;
                        held = bt_say(link, BT_LOAD_PARAMS, params.bytes, params.used) == 0 && held;
                }
                if (link->supported & BT_HAS_BREDR)
                        held = bt_say(link, BT_LOAD_LINK_KEYS, keys.bytes, keys.used) == 0 && held;
        }
        for (positive at = 0; at < book->pads; at++)
        {
                p8 device[8];

                bt_place(device, book->pad[at].address, book->pad[at].type);
                device[7] = book->pad[at].type == BT_BREDR ? 1 : 2; // let it page us; connect it when it advertises
                held = bt_say(link, BT_ADD_DEVICE, device, sizeof(device)) == 0 && held;
        }
        bt_text address_to texts[] = {address_of keys, address_of ltks, address_of irks, address_of params};
        for (positive at = 0; at < array_count(texts); at++)
        {
                if (texts[at]->bytes)
                        crypto_forget(texts[at]->bytes, texts[at]->used);
                bt_text_free(texts[at]);
        }
}

/* ---- what is near: the scan ---- */

typedef struct
{
        p8 address[6], type, hid;
        b32 rssi;
        p32 class;      // the class of device a classic one says
        p16 appearance; // and the appearance an LE one says
        p8 name[249];
} bt_seen;

typedef struct
{
        bt_seen address_to seen;
        positive count, room;
} bt_air;

/* What a device is, by the class of device it says, or its LE appearance, or a HID service it offers: the word a
   scan's line says, and the order it is shown in (a pad first). */
static string_address bt_seen_kind(const bt_seen address_to seen, positive address_to rank)
{
        p32 major = seen->class >> 8 & 0x1f, minor = seen->class >> 2 & 0x3f, category = seen->appearance >> 6;

        *rank = 0;
        if (seen->appearance == 0x03c3 || seen->appearance == 0x03c4 || (major == 5 && ((minor & 15) == 1 || (minor & 15) == 2)))
                return "gamepad";
        *rank = 1;
        if (seen->appearance == 0x03c1 || (major == 5 && (minor & 0x30) == 0x10))
                return "keyboard";
        if (seen->appearance == 0x03c2 || (major == 5 && (minor & 0x30) == 0x20))
                return "mouse";
        if (major == 5 || category == 0x0f || seen->hid)
                return "input";
        *rank = 2;
        if (major == 4 || category == 0x21 || category == 0x22 || category == 0x25)
                return "audio";
        if (major == 2 || category == 1)
                return "phone";
        if (major == 1 || category == 2)
                return "computer";
        *rank = 3;
        return "other";
}

// What an advertisement or an inquiry's extended answer says of a device: its name, class, appearance and HID service.
static fn bt_eir_read(bt_seen address_to seen, const p8 address_to data, positive size)
{
        for (positive at = 0; at + 1 < size && data[at] && at + 1 + data[at] <= size; at += 1 + data[at])
        {
                p8 type = data[at + 1];
                const p8 address_to value = data + at + 2;
                positive length = data[at] - 1u;

                if ((type == 0x09 || (type == 0x08 && !seen->name[0])) && length)
                        bt_name_keep(seen->name, value, length);
                else if (type == 0x0d && length >= 3)
                        seen->class = (p32)value[0] | (p32)value[1] << 8 | (p32)value[2] << 16;
                else if (type == 0x19 && length >= 2)
                        seen->appearance = bt_le16(value);
                else if (type == 0x02 || type == 0x03)
                        for (positive uuid = 0; uuid + 2 <= length; uuid += 2)
                                seen->hid |= bt_le16(value + uuid) == 0x1812;
        }
}

static fn bt_scan_heard(bt_link address_to link, p16 event, const p8 address_to data, positive size)
{
        bt_air address_to air = (bt_air address_to)link->about;
        bt_seen address_to seen = null;
        p8 confirm[8];

        if (event != BT_EV_FOUND || size < 14 || 14u + bt_le16(data + 12) > size)
                return;
        for (positive at = 0; at < air->count && !seen; at++)
                if (!memory_compare(air->seen[at].address, data, 6))
                        seen = air->seen + at;
        if (!seen)
        {
                if (!array_store_reserve(air->seen, air->room, air->count, air->count + 1, 32))
                        return;
                seen = air->seen + air->count++;
                memory_zero(seen, sizeof(*seen));
                memory_copy(seen->address, data, 6);
        }
        seen->type = data[6], seen->rssi = (b8)data[7]; // the rssi is a signed byte
        bt_eir_read(seen, data + 14, bt_le16(data + 12));
        //      A classic device's name is asked of it by the kernel when the scan says it does not know it.
        if (bt_le32(data + 8) & 1)
        {
                bt_place(confirm, data, data[6]);
                confirm[7] = seen->name[0] != 0;
                if (bt_send(link, BT_CONFIRM_NAME, confirm, sizeof(confirm)) < 0)
                        confirm[7] = 0;
        }
}

// Whether the kernel has a Bluetooth controller (an hci entry under its class; the class is there with none).
static bool bt_controller_visit(string_address directory, string_address name, address_any context)
{
        (void)directory;
        if (!string_has_prefix(name, "hci"))
                return true;
        *(bool address_to)context = true;
        return false;
}

static bool bt_controller_is(void)
{
        bool here = false;

        host_each_entry((string_address) "/sys/class/bluetooth", bt_controller_visit, address_of here);
        return here;
}

/* Listening for seconds, discovery started again each time the kernel ends one (an inquiry's 10.24 s), into air. */
static bipolar bt_listen(bt_link address_to link, bt_air address_to air, positive seconds)
{
        network_deadline deadline;
        p8 types = (link->supported & BT_HAS_BREDR ? 1 : 0) | (link->supported & BT_HAS_LE ? 6 : 0);
        bipolar status = bt_say(link, BT_START_DISCOVERY, address_of types, 1);

        if (status)
                return status;
        link->heard = bt_scan_heard, link->about = air;
        if (!network_deadline_begin(address_of deadline, seconds, 0))
                return -EINVAL;
        for (;;)
        {
                bipolar heard = bt_hear(link, address_of deadline);

                if (heard <= 0)
                        break;
                bt_heard_pass(link);
                if (bt_le16(bt_said) == BT_EV_DISCOVERING && bt_le16(bt_said + 4) >= 2 && !bt_said[7] &&
                    bt_send(link, BT_START_DISCOVERY, address_of types, 1) < 0)
                        break;
        }
        link->heard = null;
        status = bt_say(link, BT_STOP_DISCOVERY, address_of types, 1);
        return 0;
}

// The order a scan's devices are shown in: pads, then the rest of the input devices, then the others, nearest first.
static bool bt_seen_before(const bt_seen address_to one, const bt_seen address_to other)
{
        positive rank_one, rank_other;

        bt_seen_kind(one, address_of rank_one), bt_seen_kind(other, address_of rank_other);
        if (rank_one != rank_other)
                return rank_one < rank_other;
        if (!one->name[0] != !other->name[0])
                return one->name[0] != 0;
        return one->rssi > other->rssi;
}

/* moonwater bluetooth scan [SECONDS]: what is near, pads first, a line each: its address, what it is, LE when it is a
   Bluetooth LE device, and its name; kept under /run for a pair to find its kind by. */
static b32 bt_scan(string_address seconds_word)
{
        bt_link link;
        bt_air air = {0};
        bt_text found = {0};
        positive seconds = 10, used = 0;
        bipolar why;

        if (seconds_word)
        {
                seconds = string_digits_max(seconds_word, 5, address_of used);
                if (!used || seconds_word[used] || !seconds || seconds > 3600)
                        return host_refuse("a scan is 1 to 3600 seconds, not %s\n", seconds_word);
        }
        host_need_root();
        if (radio_kept(NET_BLUETOOTH_POWER) == 0)
                return bt_refuse(0);
        if ((why = bt_open(address_of link)) < 0 || (why = bt_ready(address_of link)))
        {
                if (link.mgmt >= 0)
                        system_close(link.mgmt);
                return bt_refuse(why);
        }
        why = bt_listen(address_of link, address_of air, seconds);
        system_close(link.mgmt);
        if (why)
        {
                array_store_release(air.seen, air.room, air.count);
                return bt_refuse(why);
        }
        for (positive at = 1; at < air.count; at++)
                for (positive back = at; back && bt_seen_before(air.seen + back, air.seen + back - 1); back--)
                {
                        bt_seen swap = air.seen[back];

                        air.seen[back] = air.seen[back - 1], air.seen[back - 1] = swap;
                }
        string_format(log, host_label "bluetooth scan: %p found\n", (positive)air.count);
        for (positive at = 0; at < air.count; at++)
        {
                bt_seen address_to seen = air.seen + at;
                p8 address[18], shown[249 * 4 + 1];
                positive rank;
                string_address kind = bt_seen_kind(seen, address_of rank);
                string_address lane = bt_kind_word(seen->type);

                bt_address_text(address, seen->address, true);
                radio_display(shown, sizeof(shown), seen->name, string_length(seen->name));
                string_format(log, host_label "  %s  %s%s  %s\n", (string_address)address, kind,
                              seen->type == BT_BREDR ? (string_address) "" : (string_address) " le", (string_address)shown);
                if (!bt_put(&found, address, 17) || !bt_put(&found, " ", 1) || !bt_put(&found, lane, string_length(lane)) ||
                    !bt_put(&found, " ", 1) || !bt_put(&found, kind, string_length(kind)) || !bt_put(&found, " ", 1) ||
                    !bt_put(&found, seen->name, string_length(seen->name)) || !bt_put(&found, "\n", 1))
                        break;
        }
        log_flush();
        host_state_ready();
        why = found.used ? host_write_file(BT_FOUND, found.bytes, found.used, 0644, false) : 0;
        bt_text_free(address_of found);
        array_store_release(air.seen, air.room, air.count);
        return why < 0 ? host_fail(BT_FOUND, why) : 0;
}

// The kind and name the last scan heard address say, when it heard it.
static bool bt_found(const p8 address_to address, p8 address_to type, p8 address_to name)
{
        bt_text text = {0};
        bipolar got = bt_read_whole(BT_FOUND, address_of text);
        positive at = 0, start, length;
        bool found = false;

        while (!found && got > 0 && host_line_next(text.bytes, (positive)got, address_of at, address_of start, address_of length))
        {
                const p8 address_to line = text.bytes + start;
                positive word = 0, from[3], size[3];
                p8 heard[6];
                bipolar kind;
                bool whole = true;

                for (positive part = 0; part < 3 && whole; part++)
                        whole = bt_word(line, length, address_of word, from + part, size + part);
                if (!whole || !bt_address_read(line + from[0], size[0], heard) || memory_compare(heard, address, 6) ||
                    (kind = bt_kind_read(line + from[1], size[1])) < 0)
                        continue;
                *type = (p8)kind;
                bt_name_keep(name, line + word, length - word);
                found = true;
        }
        bt_text_free(address_of text);
        return found;
}

/* ---- a classic pad's records: SDP ---- */

/* An L2CAP channel to address (sockaddr_l2: family, PSM, address, CID, address kind), at the security level asked,
   connected within seconds: the socket, nonblocking, or the error. */
static bipolar bt_l2cap_open(const p8 address_to address, p8 type, p16 psm, p16 cid, p8 level, positive seconds)
{
        p8 place[14] = {0}, security[2] = {level, 0}, error[4] = {0};
        p32 error_size = sizeof(error);
        network_deadline deadline;
        bipolar handle = bt_socket(BT_SEQPACKET | SOCK_NONBLOCK, BT_PROTO_L2CAP);
        bipolar done;

        if (handle < 0)
                return handle;
        bt_put16(place, BT_FAMILY);
        bt_put16(place + 10, cid);
        place[12] = type == BT_BREDR ? BT_BREDR : BT_LE_PUBLIC;
        done = system_call_5(syscall(setsockopt), (positive)handle, BT_LEVEL, BT_SECURITY, (positive)security, sizeof(security));
        if (done >= 0)
                done = system_call_3(syscall(bind), (positive)handle, (positive)place, sizeof(place));
        bt_put16(place + 2, psm);
        memory_copy(place + 4, address, 6);
        place[12] = type;
        if (done >= 0)
                done = system_call_3(syscall(connect), (positive)handle, (positive)place, sizeof(place));
        if (done == -EINPROGRESS)
        {
                done = network_deadline_begin(address_of deadline, seconds, 0) ? network_wait_writable_until(handle, address_of deadline) : -EINVAL;
                done = done > 0 ? system_call_5(syscall(getsockopt), (positive)handle, SOL_SOCKET, SO_ERROR, (positive)error,
                                                (positive)address_of error_size)
                                : done ? done : -ETIMEDOUT;
                if (done >= 0 && bt_le32(error))
                        done = -(bipolar)bt_le32(error);
        }
        if (done < 0)
        {
                system_close(handle);
                return done;
        }
        return handle;
}

/* One search of a service class, and the attributes from first to last of every record that has it, the answer's
   attribute lists joined into lists (continued as long as the server says there is more): 0, or the error. */
static bipolar bt_sdp_search(bipolar handle, p16 uuid, p16 first, p16 last, bt_text address_to lists)
{
        p8 ask[40], got[1024], more[17] = {0};
        network_deadline deadline;

        for (p16 transaction = 1; transaction < 64; transaction++)
        {
                positive at = 5, count;
                bipolar size;

                ask[0] = 0x06, bt_put_be16(ask + 1, transaction);
                ask[at++] = 0x35, ask[at++] = 3, ask[at++] = 0x19, bt_put_be16(ask + at, uuid), at += 2;
                bt_put_be16(ask + at, sizeof(got) - 16), at += 2;
                ask[at++] = 0x35, ask[at++] = 5, ask[at++] = 0x0a, bt_put_be16(ask + at, first), bt_put_be16(ask + at + 2, last), at += 4;
                memory_copy(ask + at, more, 1u + more[0]), at += 1u + more[0];
                bt_put_be16(ask + 3, (p32)(at - 5));
                if (system_call_6(syscall(sendto), (positive)handle, (positive)ask, at, BT_NO_SIGNAL, 0, 0) < 0 ||
                    !network_deadline_begin(address_of deadline, 5, 0) || network_wait_readable_until(handle, address_of deadline) <= 0)
                        return -ETIMEDOUT;
                size = system_call_3(syscall(read), (positive)handle, (positive)got, sizeof(got));
                if (size < 10 || got[0] != 0x07 || bt_be16(got + 1) != transaction)
                        return size < 0 ? size : -EPROTO;
                count = bt_be16(got + 5);
                if (7 + count + 1 > (positive)size || got[7 + count] > 16 || 7 + count + 1 + got[7 + count] > (positive)size)
                        return -EPROTO;
                if (!bt_put(lists, got + 7, count))
                        return -ENOMEM;
                memory_copy(more, got + 7 + count, 1u + got[7 + count]);
                if (!more[0])
                        return 0;
        }
        return -EPROTO;
}

/* The data element at at: its type (the header's five high bits) and where its value starts and how long it is;
   false when it would run past size. */
static bool bt_sdp_element(const p8 address_to data, positive size, positive at, p8 address_to type, positive address_to start,
                           positive address_to length)
{
        p8 index;
        positive skip = 1, value = 0;

        if (at >= size)
                return false;
        *type = data[at] >> 3, index = data[at] & 7;
        if (*type == 0)
                value = 0;
        else if (index < 5)
                value = (positive)1 << index;
        else
        {
                positive bytes = (positive)1 << (index - 5);

                if (bytes > size - at - 1)
                        return false;
                for (positive byte = 0; byte < bytes; byte++)
                        value = value << 8 | data[at + 1 + byte];
                skip += bytes;
        }
        if (skip > size - at || value > size - at - skip)
                return false;
        *start = at + skip, *length = value;
        return true;
}

// An unsigned value of an element, as wide as it is (SDP's are big-endian).
static p32 bt_sdp_number(const p8 address_to data, positive start, positive length)
{
        p32 value = 0;

        for (positive at = 0; at < length && at < 4; at++)
                value = value << 8 | data[start + at];
        return value;
}

// The first report descriptor (type 0x22) of a HIDDescriptorList: a sequence of sequences, each a type and its bytes.
static fn bt_sdp_map(bt_book address_to book, bt_pad address_to pad, const p8 address_to data, positive start, positive stop)
{
        p8 type;
        positive one, one_size, part, part_size;

        for (positive at = start; at < stop && bt_sdp_element(data, stop, at, &type, &one, &one_size); at = one + one_size)
                if (type == 6 && bt_sdp_element(data, one + one_size, one, &type, &part, &part_size) && type == 1 &&
                    bt_sdp_number(data, part, part_size) == 0x22 &&
                    bt_sdp_element(data, one + one_size, part + part_size, &type, &part, &part_size) && type == 4 && part_size)
                {
                        if (!bt_book_map(book, pad, data + part, part_size))
                                pad->map_size = 0;
                        return;
                }
}

/* One record's attributes, the pairs of an id and its value from start to stop: from a HID record the parser, subclass,
   country and report descriptor; from a Device ID record the vendor, product and version, which choose the kernel's
   driver for the pad. */
static fn bt_sdp_record(bt_book address_to book, bt_pad address_to pad, const p8 address_to data, positive start, positive stop,
                        bool hid)
{
        p8 id_type, type;
        positive id, id_size, value, size;

        for (positive at = start; at < stop && bt_sdp_element(data, stop, at, &id_type, &id, &id_size) &&
                                  bt_sdp_element(data, stop, id + id_size, &type, &value, &size);
             at = value + size)
        {
                p32 attribute = id_type == 1 ? bt_sdp_number(data, id, id_size) : 0;
                p32 number = type == 1 ? bt_sdp_number(data, value, size) : 0;

                if (!hid && type == 1)
                {
                        if (attribute == 0x0201)
                                pad->vendor = (p16)number;
                        else if (attribute == 0x0202)
                                pad->product = (p16)number;
                        else if (attribute == 0x0203)
                                pad->version = (p16)number;
                }
                else if (hid && type == 1 && attribute == 0x0201)
                        pad->parser = (p16)number;
                else if (hid && type == 1 && attribute == 0x0202)
                        pad->subclass = (p8)number;
                else if (hid && type == 1 && attribute == 0x0203)
                        pad->country = (p8)number;
                else if (hid && type == 6 && attribute == 0x0206 && !pad->map_size)
                        bt_sdp_map(book, pad, data, value, value + size);
        }
}

/* What a search answered: one sequence of records, each a sequence of attributes. */
static fn bt_sdp_read(bt_book address_to book, bt_pad address_to pad, const bt_text address_to lists, bool hid)
{
        const p8 address_to data = lists->bytes;
        p8 type;
        positive outer, outer_size, record, size;

        if (!bt_sdp_element(data, lists->used, 0, &type, &outer, &outer_size) || type != 6)
                return;
        for (positive at = outer; at < outer + outer_size && bt_sdp_element(data, outer + outer_size, at, &type, &record, &size);
             at = record + size)
                if (type == 6)
                        bt_sdp_record(book, pad, data, record, record + size, hid);
}

/* ---- a classic pad handed to the kernel's HIDP ---- */

static bipolar bt_hidp_add(bt_book address_to book, const bt_pad address_to pad, bipolar control, bipolar interrupt)
{
        p8 asked[168] = {0};
        positive map = (positive)(book->maps + pad->map_at);
        bipolar handle = bt_socket(SOCK_RAW, BT_PROTO_HIDP);
        bipolar added;

        if (handle < 0)
                return handle;
        bt_put32(asked, (p32)control);
        bt_put32(asked + 4, (p32)interrupt);
        bt_put16(asked + 8, pad->parser);
        bt_put16(asked + 10, (p32)pad->map_size);
        memory_copy(asked + 16, address_of map, sizeof(map));
        asked[24] = pad->country, asked[25] = pad->subclass;
        bt_put16(asked + 26, pad->vendor);
        bt_put16(asked + 28, pad->product);
        bt_put16(asked + 30, pad->version);
        string_copy_bounded(asked + 40, pad->name, 128);
        added = system_control(handle, BT_HIDP_ADD, (positive)asked);
        system_close(handle);
        return added;
}

// Whether the kernel has an input device of the Bluetooth device at address (its uniq, as HIDP and uhid name it).
static bool bt_input_visit(string_address directory, string_address name, address_any context)
{
        p8 path[256], uniq[32];

        if (!string_join(path, sizeof(path), directory, "/", name, (string_address) "/uniq") ||
            host_read_kernel(path, uniq, sizeof(uniq)) != 17 || string_compare(uniq, (string_address)context))
                return true;
        *(p8 address_to)context = end;
        return false;
}

static bool bt_input_is(const p8 address_to address)
{
        p8 uniq[18];

        bt_address_text(uniq, address, false);
        host_each_entry((string_address) "/sys/class/input", bt_input_visit, uniq);
        return !uniq[0];
}

// Waited for up to seconds: whether the pad's input device came.
static bool bt_input_wait(const p8 address_to address, positive seconds)
{
        for (positive turn = 0; turn < seconds * 10; turn++)
        {
                if (bt_input_is(address))
                        return true;
                host_pause(100000000);
        }
        return bt_input_is(address);
}

/* ---- the pair ---- */

typedef struct
{
        bt_pad pad;
} bt_pairing;

// Pairing's events: the pad's just works confirmed, its legacy PIN said (0000), its keys and its name taken.
static fn bt_pair_heard(bt_link address_to link, p16 event, const p8 address_to data, positive size)
{
        bt_pairing address_to pairing = (bt_pairing address_to)link->about;
        p8 reply[24] = {0};

        if (size >= 7 && (event == BT_EV_CONFIRM || event == BT_EV_PIN) && !memory_compare(data, pairing->pad.address, 6))
        {
                memory_copy(reply, data, 7);
                if (event == BT_EV_PIN)
                        reply[7] = 4, memory_copy(reply + 8, "0000", 4);
                if (bt_send(link, event == BT_EV_PIN ? BT_PIN_REPLY : BT_CONFIRM_REPLY, reply, event == BT_EV_PIN ? 24u : 7u) < 0)
                        reply[0] = 0;
                return;
        }
        if (event == BT_EV_CONNECTED && size >= 13 && !memory_compare(data, pairing->pad.address, 6) && 13u + bt_le16(data + 11) <= size)
        {
                bt_seen seen = {0};

                bt_eir_read(address_of seen, data + 13, bt_le16(data + 11));
                if (seen.name[0])
                        memory_copy(pairing->pad.name, seen.name, sizeof(seen.name));
                return;
        }
        bt_keys_take(address_of pairing->pad, event, data, size);
}

static fn bt_keeper_wake(void)
{
        bipolar handle = system_open_at(AT_FDCWD, BT_WAKE, O_WRONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
        p8 one = '1';

        if (handle < 0)
                return;
        if (file_handle_is_pipe(handle))
                system_write_all((positive)handle, address_of one, 1);
        system_close(handle);
}

/* A classic pad's records read and its two channels handed to HIDP: 0, or the step that failed, said. */
static b32 bt_pair_classic(bt_book address_to book, bt_pad address_to pad)
{
        bt_text lists = {0};
        bipolar sdp = bt_l2cap_open(pad->address, BT_BREDR, BT_PSM_SDP, 0, BT_SECURE_NONE, 10);
        bipolar control, interrupt, added;

        if (sdp < 0)
                return host_fail("bluetooth pair: its records (SDP)", sdp);
        added = bt_sdp_search(sdp, 0x1124, 0x0200, 0x020f, address_of lists);
        if (!added)
                bt_sdp_read(book, pad, address_of lists, true);
        lists.used = 0;
        if (!added && !bt_sdp_search(sdp, 0x1200, 0x0200, 0x0205, address_of lists))
                bt_sdp_read(book, pad, address_of lists, false);
        system_close(sdp);
        bt_text_free(address_of lists);
        if (added < 0)
                return host_fail("bluetooth pair: its HID record", added);
        if (!pad->map_size)
                return host_refuse("bluetooth pair: it says no HID report descriptor, so it is no pad or keyboard this pairs\n");
        control = bt_l2cap_open(pad->address, BT_BREDR, BT_PSM_CONTROL, 0, BT_SECURE, 10);
        interrupt = control < 0 ? control : bt_l2cap_open(pad->address, BT_BREDR, BT_PSM_INTERRUPT, 0, BT_SECURE, 10);
        added = interrupt < 0 ? interrupt : bt_hidp_add(book, pad, control, interrupt);
        if (control >= 0)
                system_close(control);
        if (interrupt >= 0)
                system_close(interrupt);
        return added < 0 ? host_fail("bluetooth pair: its HID channels", added) : 0;
}

/*
        moonwater bluetooth pair ADDRESS: pairs the device a scan heard there (just works: pads have nothing to type a
        number on), keeps its keys and what it is in /root, and connects it. A classic one is handed to the kernel's HIDP
        here; an LE one to the keeper, which bridges it. Either is let in again when it wakes.
*/
static b32 bt_pair(string_address text)
{
        bt_link link;
        bt_pairing pairing = {0};
        bt_book book;
        bt_pad address_to kept;
        p8 asked[8], reply[8], shown[249 * 4 + 1], address[18], unpair[8];
        bipolar status, lock;
        positive got;
        b32 failed = 0;
        bool heard;

        if (!bt_address_read(text, string_length(text), pairing.pad.address))
                return host_refuse("%s is no Bluetooth address: six hex pairs with colons, as moonwater bluetooth scan says them\n", text);
        host_need_root();
        if (radio_kept(NET_BLUETOOTH_POWER) == 0)
                return bt_refuse(0);
        if ((status = bt_open(address_of link)) < 0 || (status = bt_ready(address_of link)))
        {
                if (link.mgmt >= 0)
                        system_close(link.mgmt);
                return bt_refuse(status);
        }
        //      Its kind is the scan's: a classic and an LE device can share an address, and mgmt pairs one of them.
        heard = bt_found(pairing.pad.address, address_of pairing.pad.type, pairing.pad.name);
        if (!heard)
        {
                bt_air air = {0};

                host_say(log, host_label "bluetooth pair: listening for %s\n", text);
                if (bt_listen(address_of link, address_of air, 10))
                        air.count = 0;
                for (positive at = 0; at < air.count; at++)
                        if (!memory_compare(air.seen[at].address, pairing.pad.address, 6))
                        {
                                pairing.pad.type = air.seen[at].type;
                                memory_copy(pairing.pad.name, air.seen[at].name, sizeof(air.seen[at].name));
                                heard = true;
                        }
                array_store_release(air.seen, air.room, air.count);
        }
        if (!heard)
        {
                system_close(link.mgmt);
                return host_refuse("bluetooth pair: nothing at %s is in range: put it in pairing mode and scan again\n", text);
        }
        link.heard = bt_pair_heard, link.about = address_of pairing;
        bt_place(asked, pairing.pad.address, pairing.pad.type), asked[7] = BT_IO_NONE;
        bt_place(unpair, pairing.pad.address, pairing.pad.type), unpair[7] = 1;
        status = bt_ask(address_of link, BT_PAIR, asked, sizeof(asked), reply, sizeof(reply), address_of got, 30000);
        //      Paired before, its keys in the kernel and not here (a pairing cut short, or paired again from the pad's
        //      side): those are dropped, and it is paired again.
        if (status == 0x13 && bt_say(address_of link, BT_UNPAIR, unpair, sizeof(unpair)) == 0)
                status = bt_ask(address_of link, BT_PAIR, asked, sizeof(asked), reply, sizeof(reply), address_of got, 30000);
        if (status)
        {
                system_close(link.mgmt);
                if (status == 0x05)
                        return host_refuse("bluetooth pair: %s refused the pairing, or stopped listening for it: put it in pairing mode again\n", text);
                if (status == 0x04 || status == 0x08 || status == -ETIMEDOUT)
                        return host_refuse("bluetooth pair: %s did not answer: put it in pairing mode, near, and try again\n", text);
                return bt_refuse(status);
        }
        if (!(pairing.pad.keys & (BT_HAS_KEY | BT_HAS_LTK | BT_HAS_LTK2)))
                host_say(log, host_label "bluetooth pair: %s gave no key to keep, so it pairs again after it sleeps\n", text);
        bt_address_text(address, pairing.pad.address, true);

        lock = radio_lock(true);
        if (lock < 0)
        {
                system_close(link.mgmt);
                return host_fail("bluetooth", lock);
        }
        status = bt_book_read(address_of book);
        kept = status < 0 ? null : bt_book_add(address_of book, pairing.pad.address, pairing.pad.type);
        if (!kept)
        {
                radio_unlock(lock);
                system_close(link.mgmt);
                bt_book_free(address_of book);
                return host_fail(BT_KEPT, status < 0 ? status : -ENOMEM);
        }
        //      Paired again, it keeps the name it had when this pairing heard none.
        if (!pairing.pad.name[0])
                memory_copy(pairing.pad.name, kept->name, sizeof(kept->name));
        pairing.pad.map_at = pairing.pad.map_size = 0;
        *kept = pairing.pad;
        if (kept->type == BT_BREDR)
                failed = bt_pair_classic(address_of book, kept);
        status = failed ? 0 : bt_book_write(address_of book);
        radio_unlock(lock);
        crypto_forget(address_of pairing, sizeof(pairing));
        if (failed || status < 0)
        {
                //      Nothing is kept of a device that could not be used: it is not left half paired in the kernel.
                if (bt_say(address_of link, BT_UNPAIR, unpair, sizeof(unpair)))
                        unpair[7] = 0;
                system_close(link.mgmt);
                bt_book_free(address_of book);
                return failed ? failed : host_fail(BT_KEPT, status);
        }
        bt_keys_load(address_of link, address_of book);
        if (kept->type == BT_BREDR)
                bt_link_awake(address_of link, kept->address, BT_BREDR);
        system_close(link.mgmt);
        radio_display(shown, sizeof(shown), kept->name, string_length(kept->name));
        memory_copy(asked, kept->address, 6);
        bt_book_free(address_of book);
        bt_keeper_wake();
        bt_keep();
        host_say(log, host_label "bluetooth paired %s %s%s\n", (string_address)address, (string_address)shown,
                 bt_input_wait(asked, 8) ? (string_address) ", connected" : (string_address) "; it connects when it wakes");
        return 0;
}

/* moonwater bluetooth forget ADDRESS: the device's keys dropped here and in the kernel, its link taken down and it let in
   no more. */
static b32 bt_forget(string_address text)
{
        bt_link link;
        bt_book book;
        bt_pad address_to pad;
        p8 address[6], asked[8];
        bipolar status, lock;

        if (!bt_address_read(text, string_length(text), address))
                return host_refuse("%s is no Bluetooth address: six hex pairs with colons, as moonwater bluetooth says them\n", text);
        host_need_root();
        lock = radio_lock(true);
        if (lock < 0)
                return host_fail("bluetooth", lock);
        if ((status = bt_book_read(address_of book)) < 0)
        {
                radio_unlock(lock);
                return host_fail(BT_KEPT, status);
        }
        if (!(pad = bt_book_find(address_of book, address)))
        {
                radio_unlock(lock);
                bt_book_free(address_of book);
                return host_refuse("no paired bluetooth device is at %s\n", text);
        }
        bt_place(asked, pad->address, pad->type), asked[7] = 1;
        crypto_forget(pad, sizeof(*pad));
        *pad = book.pad[--book.pads];
        status = bt_book_write(address_of book);
        radio_unlock(lock);
        bt_book_free(address_of book);
        if (status < 0)
                return host_fail(BT_KEPT, status);
        if (bt_open(address_of link) >= 0 && bt_ready(address_of link) == 0)
        {
                status = bt_say(address_of link, BT_UNPAIR, asked, sizeof(asked));
                status = bt_say(address_of link, BT_REMOVE_DEVICE, asked, 7);
        }
        if (link.mgmt >= 0)
                system_close(link.mgmt);
        bt_keeper_wake();
        host_say(log, host_label "bluetooth forgot %s\n", text);
        return 0;
}

/* ---- the keeper ---- */

typedef struct
{
        p8 address[6];
        bipolar control, interrupt;
        positive since;
} bt_door; // a classic pad's two channels, as they come in

typedef struct
{
        p16 handle, ccc;
        p8 id, kind; // the Report Reference's id and type: 1 input, 2 output, 3 feature
} bt_report;

typedef struct
{
        p8 address[6], type;
        bool numbered; // the report map's reports have ids, each report sent and given with its id first
        bipolar att, uhid;
        positive mtu;
        bt_report address_to report;
        positive reports, report_room;
} bt_hog; // an LE pad bridged: its ATT channel, and the uhid device made of its HID service

static struct
{
        bt_link link;
        bt_book book;
        bipolar control, interrupt, wake;
        bool ready;   // the controller set, the keys loaded and the pads let in
        bool powered;
        bt_door address_to door;
        positive doors, door_room;
        bt_hog address_to hog;
        positive hogs, hog_room;
        bool connect; // an LE pad connected, for the loop to bridge
} bt_keeper;

static fn bt_hog_close(positive at)
{
        bt_hog address_to hog = bt_keeper.hog + at;

        if (hog->att >= 0)
                system_close(hog->att);
        if (hog->uhid >= 0)
                system_close(hog->uhid);
        array_store_release(hog->report, hog->report_room, hog->reports);
        *hog = bt_keeper.hog[--bt_keeper.hogs];
}

static bt_report address_to bt_hog_find(bt_hog address_to hog, p16 handle, p8 id, p8 kind)
{
        for (positive at = 0; at < hog->reports; at++)
                if (handle ? hog->report[at].handle == handle : hog->report[at].kind == kind && (!hog->numbered || hog->report[at].id == id))
                        return hog->report + at;
        return null;
}

// A report the pad notified, as an input report of its uhid device.
static fn bt_hog_input(bt_hog address_to hog, const p8 address_to value, positive size, p16 handle)
{
        static p8 event[4 + 2 + BT_MAP_MOST];
        bt_report address_to report = bt_hog_find(hog, handle, 0, 1);
        positive at = 6;

        if (!report || hog->uhid < 0)
                return;
        if (hog->numbered)
                event[at++] = report->id;
        if (size > sizeof(event) - at)
                size = sizeof(event) - at;
        memory_copy(event + at, value, size);
        bt_put32(event, 12);
        bt_put16(event + 4, (p32)(at - 6 + size)); // UHID_INPUT2
        if (system_call_3(syscall(write), (positive)hog->uhid, (positive)event, at + size) < 0)
                event[0] = 0;
}

static bipolar bt_att_send(bt_hog address_to hog, const p8 address_to bytes, positive size)
{
        return system_call_6(syscall(sendto), (positive)hog->att, (positive)bytes, size, BT_NO_SIGNAL, 0, 0);
}

/* What the pad says without being asked: a notification carried to the kernel, an indication carried and confirmed,
   and a request (the pad asking this machine's attributes, which it has none of) answered with an error, as ATT wants
   every request answered. */
static fn bt_att_unasked(bt_hog address_to hog, const p8 address_to said, positive size)
{
        p8 answer[5];

        if ((said[0] == 0x1b || said[0] == 0x1d) && size >= 3)
        {
                bt_hog_input(hog, said + 3, size - 3, bt_le16(said + 1));
                if (said[0] == 0x1d && bt_att_send(hog, (const p8 address_to) "\x1e", 1) < 0)
                        answer[0] = 0;
                return;
        }
        if (said[0] == 0x02)
        {
                answer[0] = 0x03, bt_put16(answer + 1, BT_ATT_MTU);
                if (bt_att_send(hog, answer, 3) < 0)
                        answer[0] = 0;
                return;
        }
        if (said[0] & 0x40 || said[0] & 1 || said[0] > 0x20)
                return;
        answer[0] = 0x01, answer[1] = said[0], bt_put16(answer + 2, 0);
        answer[4] = said[0] == 0x04 || said[0] == 0x06 || said[0] == 0x08 || said[0] == 0x10 ? 0x0a : 0x06;
        if (bt_att_send(hog, answer, 5) < 0)
                answer[0] = 0;
}

/* One request on the pad's ATT channel and its answer, into into: the answer's length; 0 for an error response; -1 when
   the channel failed or nothing came in two seconds. What the pad says unasked meanwhile is dealt with as it comes. */
static bipolar bt_att_ask(bt_hog address_to hog, const p8 address_to ask, positive size, p8 answer, p8 address_to into, positive room)
{
        network_deadline deadline;

        if (bt_att_send(hog, ask, size) < 0 || !network_deadline_begin(address_of deadline, 2, 0))
                return -1;
        for (;;)
        {
                bipolar got;

                if (network_wait_readable_until(hog->att, address_of deadline) <= 0)
                        return -1;
                got = system_call_3(syscall(read), (positive)hog->att, (positive)into, room);
                if (got <= 0)
                {
                        if (got == -EAGAIN || got == -EINTR)
                                continue;
                        return -1;
                }
                if (into[0] == answer)
                        return got;
                if (into[0] == 0x01 && got >= 5 && into[1] == ask[0])
                        return 0;
                bt_att_unasked(hog, into, (positive)got);
        }
}

// The attributes from start to stop of one type, read in turns (each answer's last handle the next start).
typedef bool (address_to bt_att_each)(bt_hog address_to hog, const p8 address_to entry, positive each, address_any context);

static bool bt_att_walk(bt_hog address_to hog, p8 ask_code, p16 start, p16 stop, p16 type, bt_att_each visit, address_any context)
{
        p8 ask[7] = {0}, got[BT_ATT_MTU + 1];

        for (p32 from = start; from && from <= stop;)
        {
                positive each, sent = 5, last = 0;
                bipolar size;

                ask[0] = ask_code, bt_put16(ask + 1, from), bt_put16(ask + 3, stop);
                if (ask_code != 0x04)
                {
                        bt_put16(ask + 5, type);
                        sent = 7;
                }
                size = bt_att_ask(hog, ask, sent, (p8)(ask_code + 1), got, sizeof(got));
                if (size < 0)
                        return false;
                if (size < 2)
                        return true;
                each = ask_code == 0x04 ? (got[1] == 1 ? 4u : 18u) : got[1];
                if (each < 4)
                        return true;
                for (positive at = 2; at + each <= (positive)size; at += each)
                {
                        last = ask_code == 0x10 ? bt_le16(got + at + 2) : bt_le16(got + at);
                        if (!visit(hog, got + at, each, context))
                                return true;
                }
                if (last < from || last >= 0xffff)
                        return true;
                from = (p32)last + 1;
        }
        return true;
}

typedef struct { p16 hid_start, hid_end, dis_start, dis_end; } bt_services;

static bool bt_service_visit(bt_hog address_to hog, const p8 address_to entry, positive each, address_any context)
{
        bt_services address_to services = (bt_services address_to)context;
        p16 uuid = each == 6 ? bt_le16(entry + 4) : 0;

        (void)hog;
        if (uuid == 0x1812 && !services->hid_start)
                services->hid_start = bt_le16(entry), services->hid_end = bt_le16(entry + 2);
        if (uuid == 0x180a)
                services->dis_start = bt_le16(entry), services->dis_end = bt_le16(entry + 2);
        return true;
}

typedef struct { p16 declare, value, uuid; } bt_trait;
typedef struct { bt_trait address_to trait; positive count, room; } bt_traits;

static bool bt_trait_visit(bt_hog address_to hog, const p8 address_to entry, positive each, address_any context)
{
        bt_traits address_to traits = (bt_traits address_to)context;

        (void)hog;
        if (each < 5)
                return true;
        if (!array_store_reserve(traits->trait, traits->room, traits->count, traits->count + 1, 16))
                return false;
        traits->trait[traits->count].declare = bt_le16(entry);
        traits->trait[traits->count].value = bt_le16(entry + 3);
        traits->trait[traits->count++].uuid = each == 7 ? bt_le16(entry + 5) : 0;
        return true;
}

static bool bt_describe_visit(bt_hog address_to hog, const p8 address_to entry, positive each, address_any context)
{
        p16 address_to handles = (p16 address_to)context; // the CCC, then the Report Reference

        (void)hog;
        if (each == 4 && bt_le16(entry + 2) == 0x2902)
                handles[0] = bt_le16(entry);
        if (each == 4 && bt_le16(entry + 2) == 0x2908)
                handles[1] = bt_le16(entry);
        return true;
}

static bool bt_pnp_visit(bt_hog address_to hog, const p8 address_to entry, positive each, address_any context)
{
        p16 address_to ids = (p16 address_to)context;

        (void)hog;
        if (each >= 9)
                ids[0] = bt_le16(entry + 3), ids[1] = bt_le16(entry + 5), ids[2] = bt_le16(entry + 7);
        return false;
}

// An attribute's whole value, read and read on (Read Blob) while it fills an answer, up to room bytes: its length.
static positive bt_att_read_long(bt_hog address_to hog, p16 handle, p8 address_to into, positive room)
{
        p8 ask[5], got[BT_ATT_MTU + 1];
        positive used = 0;
        bipolar size;

        ask[0] = 0x0a, bt_put16(ask + 1, handle);
        size = bt_att_ask(hog, ask, 3, 0x0b, got, sizeof(got));
        while (size > 1)
        {
                positive part = (positive)size - 1 < room - used ? (positive)size - 1 : room - used;

                memory_copy(into + used, got + 1, part);
                used += part;
                if ((positive)size - 1 < hog->mtu - 1 || used >= room)
                        break;
                ask[0] = 0x0c, bt_put16(ask + 1, handle), bt_put16(ask + 3, (p32)used);
                size = bt_att_ask(hog, ask, 5, 0x0d, got, sizeof(got));
        }
        return used;
}

/* An LE pad's HID service found and made a uhid device, its input reports turned on: true when it is bridged. */
static bool bt_hog_open(const p8 address_to address, p8 type)
{
        static p8 event[4 + 276 + BT_MAP_MOST];
        p8 ask[5], got[BT_ATT_MTU + 1], text[18];
        bt_services services = {0};
        bt_traits traits = {0};
        p16 ids[3] = {0};
        positive map_size = 0;
        bt_pad address_to pad = bt_book_find(&bt_keeper.book, address);
        bt_hog address_to hog;
        bipolar size;

        for (positive at = 0; at < bt_keeper.hogs; at++)
                if (!memory_compare(bt_keeper.hog[at].address, address, 6))
                        return true;
        if (!pad || !array_store_reserve(bt_keeper.hog, bt_keeper.hog_room, bt_keeper.hogs, bt_keeper.hogs + 1, 4))
                return false;
        hog = bt_keeper.hog + bt_keeper.hogs;
        memory_zero(hog, sizeof(*hog));
        memory_copy(hog->address, address, 6);
        hog->type = type, hog->uhid = -1, hog->mtu = 23;
        hog->att = bt_l2cap_open(address, type, 0, BT_CID_ATT, BT_SECURE, 10);
        if (hog->att < 0)
                return false;
        bt_keeper.hogs++;
        ask[0] = 0x02, bt_put16(ask + 1, BT_ATT_MTU);
        size = bt_att_ask(hog, ask, 3, 0x03, got, sizeof(got));
        if (size >= 3)
                hog->mtu = bt_le16(got + 1) < 23 ? 23 : bt_le16(got + 1) > BT_ATT_MTU ? BT_ATT_MTU : bt_le16(got + 1);
        if (!bt_att_walk(hog, 0x10, 1, 0xffff, 0x2800, bt_service_visit, address_of services) || !services.hid_start ||
            !bt_att_walk(hog, 0x08, services.hid_start, services.hid_end, 0x2803, bt_trait_visit, address_of traits))
        {
                array_store_release(traits.trait, traits.room, traits.count);
                bt_hog_close(bt_keeper.hogs - 1);
                return false;
        }
        for (positive at = 0; at < traits.count; at++)
        {
                p16 stop = at + 1 < traits.count ? traits.trait[at + 1].declare - 1 : services.hid_end, handles[2] = {0, 0};
                bt_report address_to report;

                if (traits.trait[at].uuid == 0x2a4b && !map_size)
                        map_size = bt_att_read_long(hog, traits.trait[at].value, event + 4 + 276, BT_MAP_MOST);
                if (traits.trait[at].uuid != 0x2a4d || !array_store_reserve(hog->report, hog->report_room, hog->reports, hog->reports + 1, 8))
                        continue;
                report = hog->report + hog->reports++;
                memory_zero(report, sizeof(*report));
                report->handle = traits.trait[at].value, report->kind = 1;
                if (stop > report->handle && !bt_att_walk(hog, 0x04, report->handle + 1, stop, 0, bt_describe_visit, handles))
                        break;
                report->ccc = handles[0];
                ask[0] = 0x0a, bt_put16(ask + 1, handles[1]);
                if (handles[1] && (size = bt_att_ask(hog, ask, 3, 0x0b, got, sizeof(got))) >= 3)
                        report->id = got[1], report->kind = got[2];
                hog->numbered |= report->id != 0;
        }
        array_store_release(traits.trait, traits.room, traits.count);
        if (services.dis_start && !bt_att_walk(hog, 0x08, services.dis_start, services.dis_end, 0x2a50, bt_pnp_visit, ids))
                map_size = 0;
        for (positive at = 0; at < hog->reports; at++)
        {
                p8 on[5] = {0x12, 0, 0, 1, 0};

                if (hog->report[at].kind != 1 || !hog->report[at].ccc)
                        continue;
                bt_put16(on + 1, hog->report[at].ccc);
                if (bt_att_ask(hog, on, 5, 0x13, got, sizeof(got)) < 0)
                        map_size = 0;
        }
        hog->uhid = map_size ? system_open_at(AT_FDCWD, "/dev/uhid", FILE_READ_WRITE | O_NONBLOCK | O_CLOEXEC) : -1;
        if (hog->uhid < 0)
        {
                bt_hog_close(bt_keeper.hogs - 1);
                return false;
        }
        //      UHID_CREATE2: name, phys (this machine's address) and uniq (the pad's), the report map, bus Bluetooth and its
        //      ids, which choose the kernel's driver for it as they do a classic pad's.
        memory_zero(event, 4 + 276);
        bt_put32(event, 11);
        string_copy_bounded(event + 4, pad->name[0] ? pad->name : (p8 address_to) "Bluetooth LE pad", 128);
        bt_address_text(text, bt_keeper.link.own, false), memory_copy(event + 4 + 128, text, 18);
        bt_address_text(text, address, false), memory_copy(event + 4 + 192, text, 18);
        bt_put16(event + 4 + 256, (p32)map_size);
        bt_put16(event + 4 + 258, 5);
        bt_put32(event + 4 + 260, ids[0]);
        bt_put32(event + 4 + 264, ids[1]);
        bt_put32(event + 4 + 268, ids[2]);
        if (system_call_3(syscall(write), (positive)hog->uhid, (positive)event, 4 + 276 + map_size) < 0)
        {
                bt_hog_close(bt_keeper.hogs - 1);
                return false;
        }
        bt_link_awake(&bt_keeper.link, address, type);
        return true;
}

/* What the kernel asks of an LE pad's uhid device: an output report (a rumble, a light) written to the pad, and a get
   or set of a report read or written there and answered, as uhid waits for each answer. */
static fn bt_hog_kernel(bt_hog address_to hog)
{
        static p8 event[4 + 4376], answer[4 + 8 + BT_MAP_MOST], ask[3 + BT_ATT_MTU];
        p8 got[BT_ATT_MTU + 1];
        bipolar size = system_call_3(syscall(read), (positive)hog->uhid, (positive)event, sizeof(event));
        p32 type = size >= 4 ? bt_le32(event) : 0;
        static const p8 kinds[] = {3, 2, 1}; // uhid's report types (feature, output, input) as Report Reference's
        bt_report address_to report;

        if (type == 6 && size >= 4 + 4096 + 3) // UHID_OUTPUT
        {
                const p8 address_to data = event + 4;
                positive length = bt_le16(event + 4 + 4096), skip = hog->numbered && length ? 1 : 0;

                report = bt_hog_find(hog, 0, length ? data[0] : 0, event[4 + 4098] < 3 ? kinds[event[4 + 4098]] : 2);
                if (!report || length - skip > hog->mtu - 3)
                        return;
                ask[0] = 0x52, bt_put16(ask + 1, report->handle), memory_copy(ask + 3, data + skip, length - skip);
                if (bt_att_send(hog, ask, 3 + length - skip) < 0)
                        ask[0] = 0;
                return;
        }
        if ((type == 9 && size >= 10) || (type == 13 && size >= 12)) // UHID_GET_REPORT, UHID_SET_REPORT
        {
                p8 rnum = event[8];
                positive length = type == 13 ? bt_le16(event + 10) : 0, skip = hog->numbered && length ? 1 : 0, used = 0;
                bipolar asked = -1;

                report = bt_hog_find(hog, 0, rnum, event[9] < 3 ? kinds[event[9]] : 3);
                memory_zero(answer, 4 + 8);
                bt_put32(answer, type + 1);
                memory_copy(answer + 4, event + 4, 4);
                if (report && type == 9)
                {
                        ask[0] = 0x0a, bt_put16(ask + 1, report->handle);
                        asked = bt_att_ask(hog, ask, 3, 0x0b, got, sizeof(got));
                        if (asked >= 1)
                        {
                                if (hog->numbered)
                                        answer[12 + used++] = rnum;
                                memory_copy(answer + 12 + used, got + 1, (positive)asked - 1), used += (positive)asked - 1;
                        }
                        bt_put16(answer + 10, (p32)used);
                }
                if (report && type == 13 && length >= skip && length - skip <= hog->mtu - 3 && length <= 4096)
                {
                        ask[0] = 0x12, bt_put16(ask + 1, report->handle), memory_copy(ask + 3, event + 12 + skip, length - skip);
                        asked = bt_att_ask(hog, ask, 3 + length - skip, 0x13, got, sizeof(got));
                }
                bt_put16(answer + 8, asked >= 1 ? 0 : 5); // EIO
                if (system_call_3(syscall(write), (positive)hog->uhid, (positive)answer, type == 9 ? 12 + used : 10u) < 0)
                        answer[0] = 0;
        }
}

/* ---- the keeper's controller, pads and events ---- */

static fn bt_keeper_heard(bt_link address_to link, p16 event, const p8 address_to data, positive size)
{
        bt_pad address_to pad = size >= 7 ? bt_book_find(&bt_keeper.book, data + (event == BT_EV_LINK_KEY || event == BT_EV_LTK ? 1 : event == BT_EV_IRK ? 7 : 0)) : null;

        (void)link;
        if (event == BT_EV_INDEX_ADDED || event == BT_EV_INDEX_REMOVED)
        {
                bt_keeper.ready = false;
                while (event == BT_EV_INDEX_REMOVED && bt_keeper.hogs)
                        bt_hog_close(bt_keeper.hogs - 1);
                return;
        }
        if (event == BT_EV_SETTINGS && size >= 4)
        {
                bool powered = (bt_le32(data) & BT_POWERED) != 0;

                bt_keeper.ready &= powered == bt_keeper.powered || !powered;
                bt_keeper.powered = powered;
                return;
        }
        if (event == BT_EV_CONNECTED && pad && pad->type != BT_BREDR)
        {
                bt_keeper.connect = true;
                return;
        }
        if (event == BT_EV_DISCONNECTED && size >= 7)
        {
                for (positive at = 0; at < bt_keeper.hogs; at++)
                        if (!memory_compare(bt_keeper.hog[at].address, data, 6))
                        {
                                bt_hog_close(at);
                                break;
                        }
                return;
        }
        //      A pad given new keys (paired again, from here) has them kept, under the lock the verbs take.
        if (pad && bt_keys_take(pad, event, data, size))
        {
                bipolar lock = radio_lock(true);
                bt_book book;

                if (lock < 0)
                        return;
                if (bt_book_read(address_of book) == 0)
                {
                        bt_pad address_to kept = bt_book_find(address_of book, pad->address);

                        if (kept && bt_keys_take(kept, event, data, size) && bt_book_write(address_of book) < 0)
                                kept->keys = 0;
                }
                radio_unlock(lock);
                bt_book_free(address_of book);
        }
}

// The LE pads up already (paired a moment ago, or connected before the keeper was), bridged.
static fn bt_keeper_connected(void)
{
        static p8 reply[65535];
        positive got = 0;

        if (bt_ask(&bt_keeper.link, BT_GET_CONNECTIONS, null, 0, reply, sizeof(reply), address_of got, 3000) || got < 2)
                return;
        for (positive at = 0; at < bt_le16(reply) && 2 + at * 7 + 7 <= got; at++)
        {
                const p8 address_to place = reply + 2 + at * 7;
                bt_pad address_to pad = bt_book_find(&bt_keeper.book, place);

                if (pad && pad->type != BT_BREDR)
                        bt_hog_open(place, place[6]);
        }
}

static bool bt_keeper_setup(void)
{
        bt_book_free(&bt_keeper.book);
        if (bt_book_read(&bt_keeper.book) < 0 || bt_ready(&bt_keeper.link))
                return false;
        bt_keeper.link.heard = bt_keeper_heard;
        bt_keeper.powered = true;
        bt_keys_load(&bt_keeper.link, &bt_keeper.book);
        for (positive at = bt_keeper.hogs; at > 0; at--)
                if (!bt_book_find(&bt_keeper.book, bt_keeper.hog[at - 1].address))
                        bt_hog_close(at - 1);
        bt_keeper_connected();
        return true;
}

static bipolar bt_listener(p16 psm)
{
        p8 place[14] = {0}, security[2] = {BT_SECURE, 0};
        bipolar handle = bt_socket(BT_SEQPACKET | SOCK_NONBLOCK, BT_PROTO_L2CAP);
        bipolar done;

        if (handle < 0)
                return handle;
        bt_put16(place, BT_FAMILY);
        bt_put16(place + 2, psm);
        done = system_call_5(syscall(setsockopt), (positive)handle, BT_LEVEL, BT_SECURITY, (positive)security, sizeof(security));
        if (done >= 0)
                done = system_call_3(syscall(bind), (positive)handle, (positive)place, sizeof(place));
        if (done >= 0)
                done = system_call_2(syscall(listen), (positive)handle, 8);
        if (done < 0)
        {
                system_close(handle);
                return done;
        }
        return handle;
}

/* A classic pad's channel, come in: kept by its pad's door until the other one comes, and the two handed to HIDP
   then. A channel from an address no pad here has is closed (the link's key already said who it is). */
static fn bt_keeper_door(bipolar listener, bool control)
{
        p8 place[14] = {0};
        p32 size = sizeof(place);
        bipolar handle = system_call_4(syscall(accept4), (positive)listener, (positive)place, (positive)address_of size, SOCK_CLOEXEC);
        bt_pad address_to pad = handle >= 0 ? bt_book_find(&bt_keeper.book, place + 4) : null;
        bt_door address_to door = null;

        if (handle < 0)
                return;
        if (!pad || pad->type != BT_BREDR || !pad->map_size)
        {
                system_close(handle);
                return;
        }
        for (positive at = 0; at < bt_keeper.doors && !door; at++)
                if (!memory_compare(bt_keeper.door[at].address, pad->address, 6))
                        door = bt_keeper.door + at;
        if (!door)
        {
                if (!array_store_reserve(bt_keeper.door, bt_keeper.door_room, bt_keeper.doors, bt_keeper.doors + 1, 4))
                {
                        system_close(handle);
                        return;
                }
                door = bt_keeper.door + bt_keeper.doors++;
                memory_copy(door->address, pad->address, 6);
                door->control = door->interrupt = -1;
        }
        door->since = clock_monotonic_nanoseconds();
        if (control ? door->control >= 0 : door->interrupt >= 0)
                system_close(control ? door->control : door->interrupt);
        if (control)
                door->control = handle;
        else
                door->interrupt = handle;
        if (door->control < 0 || door->interrupt < 0)
                return;
        if (bt_hidp_add(&bt_keeper.book, pad, door->control, door->interrupt) >= 0)
                bt_link_awake(&bt_keeper.link, pad->address, BT_BREDR);
        system_close(door->control), system_close(door->interrupt);
        *door = bt_keeper.door[--bt_keeper.doors];
}

// Doors a second channel never came to, shut after ten seconds.
static fn bt_keeper_doors_tidy(void)
{
        positive now = clock_monotonic_nanoseconds();

        for (positive at = bt_keeper.doors; at > 0; at--)
        {
                bt_door address_to door = bt_keeper.door + at - 1;

                if (now - door->since < 10000000000ull)
                        continue;
                if (door->control >= 0)
                        system_close(door->control);
                if (door->interrupt >= 0)
                        system_close(door->interrupt);
                *door = bt_keeper.door[--bt_keeper.doors];
        }
}

/*
        The keeper: one process, holding its lock while it stands, started by the machine's pass (bt_keep) whenever
        bluetooth is on, a device is paired and a controller is here, and by a pair. It sets the controller and loads the
        keys, again whenever the controller comes back (plugged in again, powered again, after a sleep); takes a classic
        pad's channels when it pages the machine; bridges an LE pad when it connects; reads the book again when a verb
        changed it (a byte on its FIFO); and ends when no device is paired.
*/
static fn bt_keeper_run(void)
{
        system_poll_descriptor address_to set = null;
        positive set_room = 0, plain[4] = {0, 0, 0, 0}, ignored[4] = {SIGNAL_IGNORE, 0, 0, 0}, none = 0;
        bipolar lock = host_lock(BT_KEEPER, false), reset = 0;

        if (lock < 0)
                system_call_1(syscall(exit_group), 0);
        //      A fork of the machine's shell keeps its handlers and its mask: the keeper has the kernel's own, so the
        //      SIGTERM bluetooth off sends ends it, and a write to a channel gone is an error and not a death.
        for (b32 number = 1; number < 32 && reset >= 0; number++)
                if (number != 9 && number != 19)
                        reset = system_signal_action(number, number == 13 ? ignored : plain, null, 8);
        if (reset >= 0)
                reset = system_signal_mask(SIG_SETMASK, address_of none, null, 8);
        host_state_ready();
        //      The FIFO the verbs wake this by, made here, and nothing else: a name that is not a FIFO is not read.
        bt_keeper.wake = system_call_4(syscall(mknodat), AT_FDCWD, (positive)(string_address)BT_WAKE, S_IFIFO | 0600, 0);
        bt_keeper.wake = bt_keeper.wake >= 0 || bt_keeper.wake == -EEXIST
                             ? system_open_at(AT_FDCWD, BT_WAKE, FILE_READ_WRITE | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC) : -1;
        if (bt_keeper.wake >= 0 && !file_handle_is_pipe(bt_keeper.wake))
        {
                system_close(bt_keeper.wake);
                bt_keeper.wake = -1;
        }
        bt_keeper.control = bt_listener(BT_PSM_CONTROL);
        bt_keeper.interrupt = bt_listener(BT_PSM_INTERRUPT);
        if (bt_open(&bt_keeper.link) < 0)
                system_call_1(syscall(exit_group), 1);
        for (;;)
        {
                positive count = 0;
                timespec limit = {10, 0};
                bipolar ready;

                if (!bt_keeper.ready)
                {
                        bt_keeper.ready = bt_keeper_setup();
                        if (!bt_keeper.book.pads)
                                system_call_1(syscall(exit_group), 0);
                }
                if (bt_keeper.connect)
                {
                        bt_keeper.connect = false;
                        bt_keeper_connected();
                }
                bt_keeper_doors_tidy();
                if (!array_store_reserve(set, set_room, 0, 4 + bt_keeper.hogs * 2, 16))
                        system_call_1(syscall(exit_group), 1);
                set[count++] = (system_poll_descriptor){(b32)bt_keeper.link.mgmt, SYSTEM_POLL_READ, 0};
                set[count++] = (system_poll_descriptor){(b32)bt_keeper.wake, SYSTEM_POLL_READ, 0};
                set[count++] = (system_poll_descriptor){(b32)bt_keeper.control, SYSTEM_POLL_READ, 0};
                set[count++] = (system_poll_descriptor){(b32)bt_keeper.interrupt, SYSTEM_POLL_READ, 0};
                for (positive at = 0; at < bt_keeper.hogs; at++)
                {
                        set[count++] = (system_poll_descriptor){(b32)bt_keeper.hog[at].att, SYSTEM_POLL_READ, 0};
                        set[count++] = (system_poll_descriptor){(b32)bt_keeper.hog[at].uhid, SYSTEM_POLL_READ, 0};
                }
                //      Asleep until something comes: ten seconds only while a controller is not set or a door is open.
                ready = system_poll_wait(set, count, bt_keeper.ready && !bt_keeper.doors ? null : address_of limit, null);
                if (ready <= 0)
                        continue;
                //      The pads first, while the set's places are still theirs: an event below may close one. A pad
                //      closed here moves the last into its place, so they are walked from the end.
                for (positive at = bt_keeper.hogs; at > 0; at--)
                {
                        system_poll_descriptor address_to att = set + 4 + (at - 1) * 2;
                        bt_hog address_to hog = bt_keeper.hog + at - 1;

                        if (att[0].returned & SYSTEM_POLL_READ)
                        {
                                p8 said[BT_ATT_MTU + 1];
                                bipolar got = system_call_3(syscall(read), (positive)hog->att, (positive)said, sizeof(said));

                                if (got > 0)
                                        bt_att_unasked(hog, said, (positive)got);
                                else if (got != -EAGAIN && got != -EINTR)
                                        att[0].returned |= SYSTEM_POLL_HANGUP;
                        }
                        if (att[1].returned & SYSTEM_POLL_READ)
                                bt_hog_kernel(hog);
                        if ((att[0].returned | att[1].returned) & (SYSTEM_POLL_HANGUP | SYSTEM_POLL_ERROR))
                                bt_hog_close(at - 1);
                }
                if (set[0].returned & SYSTEM_POLL_READ && bt_hear(&bt_keeper.link, null) > 0)
                        bt_heard_pass(&bt_keeper.link);
                if (set[1].returned & SYSTEM_POLL_READ)
                {
                        p8 drained[64];

                        while (system_call_3(syscall(read), (positive)bt_keeper.wake, (positive)drained, sizeof(drained)) > 0)
                                drained[0] = 0;
                        bt_keeper.ready = false;
                }
                if (set[2].returned & SYSTEM_POLL_READ)
                        bt_keeper_door(bt_keeper.control, true);
                if (set[3].returned & SYSTEM_POLL_READ)
                        bt_keeper_door(bt_keeper.interrupt, false);
        }
}

/* The machine's pass: the keeper started when it is wanted and not running. A pass that finds it standing asks no more
   than its lock. */
static fn bt_keep(void)
{
        //      Cheapest and most often no first: a machine that never paired a device asks one access.
        if (system_access_at(AT_FDCWD, BT_KEPT, 0) < 0 || host_lock_owner(BT_KEEPER) ||
            radio_kept(NET_BLUETOOTH_POWER) == 0 || !bt_controller_is())
                return;
        if (host_detach(descriptors_none, descriptors_none))
                bt_keeper_run();
}

/* moonwater bluetooth: the radio, the paired devices (connected when the kernel has an input device of theirs) and the
   names kept. */
static b32 bt_status(void)
{
        bt_book book;
        bt_text found = {0};
        bipolar got = bt_book_read(address_of book), heard = bt_read_whole(BT_FOUND, address_of found);
        positive at = 0, start, length;
        p8 address[18], shown[249 * 4 + 1];

        radio_bluetooth_status();
        for (positive pad = 0; got >= 0 && pad < book.pads; pad++)
        {
                bt_address_text(address, book.pad[pad].address, true);
                radio_display(shown, sizeof(shown), book.pad[pad].name, string_length(book.pad[pad].name));
                string_format(log, host_label "  %s  %s  %s\n", (string_address)address,
                              bt_input_is(book.pad[pad].address) ? (string_address) "connected" : (string_address) "paired", (string_address)shown);
        }
        //      And what the last scan heard that is not paired: near, what it is, and its name, for a pair to be asked of.
        while (heard > 0 && host_line_next(found.bytes, (positive)heard, address_of at, address_of start, address_of length))
        {
                const p8 address_to line = found.bytes + start;
                positive word = 0, from[3], size[3];
                p8 near[6], kind[16];
                bool whole = true;

                for (positive part = 0; part < 3 && whole; part++)
                        whole = bt_word(line, length, address_of word, from + part, size + part);
                if (!whole || !bt_address_read(line + from[0], size[0], near) || (got >= 0 && bt_book_find(address_of book, near)) ||
                    size[2] >= sizeof(kind))
                        continue;
                memory_copy(kind, line + from[2], size[2]), kind[size[2]] = end;
                bt_address_text(address, near, true);
                radio_display(shown, sizeof(shown), (p8 address_to)line + word, length - word > 248 ? 248 : length - word);
                string_format(log, host_label "  %s  near %s  %s\n", (string_address)address, (string_address)kind, (string_address)shown);
        }
        bt_text_free(address_of found);
        bt_book_free(address_of book);
        log_flush();
        return 0;
}

static b32 host_bluetooth(string_address address_to arguments, positive count)
{
        string_address word = count > 2 ? arguments[2] : null;
        bipolar power = count == 3 ? host_onoff(word) : -1;

        if (count < 3)
                return bt_status();
        if (string_equals(word, "share"))
                return radio_share(arguments, count);
        if (power >= 0)
        {
                b32 failed;

                host_need_root();
                if (!power)
                        host_lock_signal(BT_KEEPER, 15); // SIGTERM: the pads' links go with the radio
                failed = radio_done(radio_bluetooth_power(power, true), power ? "bluetooth on" : "bluetooth off");
                if (!failed && power)
                        bt_keep();
                return failed;
        }
        if (string_equals(word, "scan") && count <= 4)
                return bt_scan(count == 4 ? arguments[3] : null);
        if (string_equals(word, "pair") && count == 4)
                return bt_pair(arguments[3]);
        if (string_equals(word, "forget") && count == 4)
                return bt_forget(arguments[3]);
        if ((string_equals(word, "add") || string_equals(word, "remove")) && count == 4)
        {
                host_need_root();
                return radio_bluetooth_edit(arguments[3], string_equals(word, "add"));
        }
        return host_usage();
}
