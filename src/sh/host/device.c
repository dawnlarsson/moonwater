/*
        src/sh/host/device.c: the verb device: which devices plugged in are let in, the rules, the census, the lock,
        the watch, the list.

        A run of src/sh/host.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. host_main, at the end
        of src/sh/host.c, names the function each verb runs.
*/

/* ---- device: which devices plugged in are let in. ---- */

/*
        moonwater device: what a device plugged in may do.

        The kernel holds the switches. Every USB device and every interface
        of one has an authorized file in sysfs: a device at 0 is not
        configured (the kernel has read its device descriptor and its
        strings, and nothing binds to it), and an interface at 0 binds no
        driver. A root hub has two defaults for what arrives below it,
        authorized_default for a new device and interface_authorized_default
        for its interfaces, and usbcore's own authorized_default is what a
        controller that arrives later starts with. This is the policy that
        turns them, kept in /root/usb.policy a line each:

          mode off|lock                     lock holds what is new; off is the kernel's own
          class NAME allow|deny             what any device may bring up besides its rule
          allow VID:PID/SERIAL [CLASS,...]  a device let in, as the classes it had then
          deny VID:PID/SERIAL               a device held, locked or not

        One function judges a device (usb_judge) and one puts the judgement
        in force (usb_apply); boot, every command and the watcher call the
        two and nothing else, so they cannot disagree. The watcher is a
        process of its own, started when the policy has something to hold,
        that hears the kernel's uevents and judges again at each burst.

        A rule names a device by what it says of itself, and a device says
        what it likes: a rule stops a device the owner never let in, not one
        that copies the identity of one that was.
*/
#define USB_POLICY "/root/usb.policy"
#define USB_LOG "/root/usb.log"
#define USB_LOCK_PATH HOST_STATE "/usb.lock"
#define USB_WATCHER_PATH HOST_STATE "/usb.watcher"
// Held by the one first lock that is waiting for USB to settle; there once a settle has ended this boot.
#define USB_SETTLE_PATH HOST_STATE "/usb.settle"
#define USB_SETTLED_PATH HOST_STATE "/usb.settled"
// A lock in force that /root/usb.policy does not keep: the policy, and which /root it is for and why.
#define USB_RUN_POLICY HOST_STATE "/usb.policy"
#define USB_UNKEPT_PATH HOST_STATE "/usb.unkept"
#define USB_DEVICES "/sys/bus/usb/devices"
#define USB_PROBE "/sys/bus/usb/drivers_probe"
#define USB_MODULE_DEFAULT "/sys/module/usbcore/parameters/authorized_default"
// What one census keeps, and how many rules a policy holds.
#define USB_MOST 64
#define USB_INTERFACES_MOST 256
#define USB_RULES_MOST 64
#define USB_PORT_ROOM 32
#define USB_KEY_ROOM 128
// Where a device is by names that do not move between boots: its controller, its root hub, its port path.
#define USB_PLACE_ROOM 64
#define USB_CONTROLLER_SET "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ._:-"
// A rule as text: its id, its key, its place, and the classes an allow rule keeps.
#define USB_RULE_ROOM (USB_KEY_ROOM + USB_PLACE_ROOM + 80)
#define USB_SERIAL_ROOM 512
#define USB_TEXT_ROOM 160
// How much of what a device says of itself is shown, at most.
#define USB_SHOWN 64
#define USB_POLICY_ROOM 16384
#define USB_LOG_MOST 65536
#define USB_LOG_LINE 640
#define USB_LOG_SHOWN 20
#define USB_LOG_ASKED 1000
// NETLINK_KOBJECT_UEVENT, and the kernel's group on it.
#define USB_UEVENT 15
#define USB_REWALK_NS ((p64)10000000000)
#define USB_POLL_NS ((p64)250000000)
#define USB_SETTLE_NS ((p64)50000000)
// A first boot's lock waits for USB to settle: unchanged this long, up this long at least, and at most.
#define USB_STILL_NS ((p64)2000000000)
#define USB_UP_LEAST_NS ((p64)3000000000)
#define USB_UP_MOST_NS ((p64)15000000000)

/*
        What an interface is, by its class (and, where one class is two
        things, its subclass and protocol). Storage is 08; hid 03 (keyboards,
        mice, and every security key and game pad besides); audio 01; video
        0e, the still cameras of 06 and the audio-video devices of 10; net
        the communications classes 02 and 0a (Ethernet over CDC, NCM, MBIM,
        RNDIS, and the modems of ACM with them) and RNDIS where it is
        e0/01/03 or ef/04; bluetooth e0/01/01 and e0/01/04; hub 09. Other is
        everything else: printers, smart cards, firmware update (fe), and the
        vendor-specific ff that most wifi sticks and many Ethernet adapters
        speak.
*/
enum
{
        USB_STORAGE,
        USB_HID,
        USB_AUDIO,
        USB_VIDEO,
        USB_NET,
        USB_BLUETOOTH,
        USB_HUB,
        USB_OTHER,
        USB_KINDS
};

static const string_address usb_kinds[USB_KINDS] = {
    "storage", "hid", "audio", "video", "net", "bluetooth", "hub", "other",
};

static p8 usb_kind(p32 class, p32 subclass, p32 protocol)
{
        switch (class)
        {
        case 0x08:
                return USB_STORAGE;
        case 0x03:
                return USB_HID;
        case 0x01:
                return USB_AUDIO;
        case 0x06:
        case 0x0e:
        case 0x10:
                return USB_VIDEO;
        case 0x02:
        case 0x0a:
                return USB_NET;
        case 0x09:
                return USB_HUB;
        case 0xe0:
                return subclass != 1                     ? USB_OTHER
                       : protocol == 1 || protocol == 4 ? USB_BLUETOOTH
                       : protocol == 3                  ? USB_NET
                                                        : USB_OTHER;
        case 0xef:
                return subclass == 4 ? USB_NET : USB_OTHER;
        }
        return USB_OTHER;
}

static bipolar usb_kind_named(string_address name)
{
        positive at = string_table_find(name, usb_kinds, sizeof(usb_kinds[0]), USB_KINDS);

        return at < USB_KINDS ? (bipolar)at : -1;
}

// The kind length bytes of a policy's line name; -1 for none.
static bipolar usb_kind_word(string_address text, positive length)
{
        p8 name[16];

        if (!length || length >= sizeof(name))
                return -1;
        memory_copy(name, text, length);
        name[length] = end;
        return usb_kind_named((string_address)name);
}

typedef struct
{
        p8 port[USB_PORT_ROOM];    // its name in sysfs: 1-4.1, or usb1 for a root hub
        p8 place[USB_PLACE_ROOM];  // 0000:00:14.0/0002/4.1 (a root hub's: 0000:00:14.0/0002), or empty
        p8 id[10];                 // vvvv:pppp
        p8 devnum[8];              // which enumeration this is: another at the same port has another
        p8 key[USB_KEY_ROOM];      // its serial as usb_key spells it, or -
        p8 product[USB_TEXT_ROOM]; // what it says it is, as it says it
        positive product_length;
        bool root;
        bool says_fixed;
        bool fixed; // it and every hub above it say fixed
        bool authorized;
} usb_device;

typedef struct
{
        p8 name[USB_PORT_ROOM]; // 1-4.1:1.0
        p8 kind;
        bool authorized;
        bool bound;
        bool fresh; // authorized by this pass, and asked to probe
} usb_interface;

typedef struct
{
        usb_device device[USB_MOST];
        usb_interface interface[USB_INTERFACES_MOST];
        positive count;
        positive interfaces;
        positive dropped;
} usb_census;

#define USB_ALLOW 1
#define USB_DENY 2

typedef struct
{
        p8 kind;
        p8 kinds; // an allow rule's classes, a bit each: what the device was when it was allowed (0: all)
        p8 id[10];
        p8 key[USB_KEY_ROOM];
        p8 port[USB_PLACE_ROOM]; // for a key of -, the place the rule is pinned to (or, written before places, the port)
} usb_rule;

typedef struct
{
        usb_rule rule[USB_RULES_MOST];
        positive count;
        positive skipped; // lines that are no rule, and rules past the most
        p8 allowed;       // a bit for each kind a device no rule names may bring up
        bool lock;
        bool kept;    // the file is there, or a lock this /root could not keep is in force (unkept)
        bool known;   // and could be read: it is root's
        bool cut;     // longer than is read
        bool unkept;  // what is read is USB_RUN_POLICY: /root/usb.policy could not be written or read
        bipolar error; // why /root/usb.policy could not be read, when it could not
} usb_policy;

/*
        A serial, in one spelling everywhere: shown by device, kept in the
        policy and the log, typed after VID:PID/ and compared. The device
        chooses it, so every byte a terminal or the policy's lines could act
        on is \xNN, as memory_into_escaped spells them (a control, a blank,
        a quote, a backslash, anything past ASCII), and a leading - is too,
        which leaves - alone to mean none. A serial too long to spell in the
        room is -# and the sixteen hex digits of its xxh64, which no serial
        spells (a key begins with - only as these two): a serial cut short
        would name every device that starts the same way, and spelled as
        none it was pinned to its port, where a device with no serial and
        the same VID:PID was the same device to its rule. Anyone who can read
        the serial can copy it, hashed or not, as with any serial.
*/
static fn usb_key(p8 address_to into, p8 address_to serial, bipolar length)
{
        positive skip = length > 0 && serial[0] == '-';
        positive2 spelled;

        string_copy_bounded(into, "-", USB_KEY_ROOM);
        if (length <= 0)
                return;
        if (skip)
                memory_copy(into, "\\x2d", 4);
        spelled = memory_into_escaped(into + 4 * skip, serial + skip, (positive)length - skip,
                                      USB_KEY_ROOM - 1 - 4 * skip, 63);
        if (spelled.x != (positive)length - skip)
        {
                p64 hash = hash_xxh64(serial, (positive)length, 0);
                p8 bytes[8];

                for (positive at = 0; at < 8; at++)
                        bytes[at] = (p8)(hash >> (56 - 8 * at));
                memory_copy(into, "-#", 2);
                into[2 + memory_into_hex(into + 2, bytes, 8)] = end;
                return;
        }
        into[spelled.y + 4 * skip] = end;
}

/* Whether a typed serial is one usb_key could have spelled: printable ASCII,
   \xNN where it stands for a byte, and those bytes spelled back the same. A
   control byte typed is refused here, as is anything else the policy's own
   lines could not keep. */
static bool usb_key_typed(string_address text)
{
        p8 raw[USB_KEY_ROOM];
        p8 again[USB_KEY_ROOM];
        positive length = string_length(text);
        positive used = 0;

        if (!length || length >= USB_KEY_ROOM || string_equals(text, "-"))
                return false;
        for (positive at = 0; at < length; at++)
        {
                positive digits = 0;

                if (!byte_is_graphic(text[at]))
                        return false;
                if (text[at] != '\\')
                {
                        raw[used++] = (p8)text[at];
                        continue;
                }
                if (text[at + 1] != 'x')
                        return false;
                raw[used++] = (p8)string_digits_hexadecimal_max(text + at + 2, 2, address_of digits);
                if (digits != 2)
                        return false;
                at += 3;
        }
        usb_key(again, raw, (bipolar)used);
        return string_equals((string_address)again, text);
}

// A port as sysfs names a device below a root hub: 1-4, 1-4.1, 3-10.2.1.
static bool usb_port_valid(string_address text)
{
        positive length = string_length(text);
        positive bus = string_span_of_set(text, "0123456789");

        return bus && length < USB_PORT_ROOM && text[bus] == '-' && byte_is_digit(text[bus + 1]) &&
               text[length - 1] != '.' && !string_find(text, "..") &&
               string_span_of_set(text + bus + 1, "0123456789.") == length - bus - 1;
}

/*
        A place: the controller a root hub belongs to, as the root hub's own
        serial names it (its bus address: 0000:00:14.0 for a PCI one), the
        root hub's product (0002 for USB 2, 0003 for USB 3: an xHCI has one of
        each, behind the same address), and the port path below it, as the
        device's sysfs name has it after the bus number: 0000:00:14.0/0002/4.1.
        The bus number in 1-4.1 is the order controllers came up in, which a
        machine whose controllers probe at once (EHCI does) can change between
        boots, and a rule pinned to it then held the keyboard it was made for.
*/
static bool usb_place_valid(string_address text)
{
        positive length = string_length(text);
        positive controller = string_span_of_set(text, USB_CONTROLLER_SET);
        string_address path = text + controller + 6;

        return controller && length < USB_PLACE_ROOM && text[controller] == '/' &&
               string_span_of_set(text + controller + 1, "0123456789abcdef") == 4 && text[controller + 5] == '/' &&
               byte_is_digit(path[0]) && text[length - 1] != '.' && !string_find(path, "..") &&
               string_span_of_set(path, "0123456789.") == string_length(path);
}

/* VID:PID, VID:PID/SERIAL or VID:PID/-@PLACE, into a rule's id, key and
   place; -@PORT, a port by bus number as rules were pinned before places,
   is read as it was. The key is empty when VID:PID was all there was, for
   the present devices to say. */
static bool usb_id_parse(string_address text, usb_rule address_to rule)
{
        static const p8 hex[] = "0123456789abcdefABCDEF";
        string_address rest;

        rule->key[0] = end;
        rule->port[0] = end;
        if (string_length(text) < 9 || text[4] != ':' || string_span_of_set(text, (string_address)hex) != 4 ||
            string_span_of_set(text + 5, (string_address)hex) != 4 || (text[9] && text[9] != '/'))
                return false;
        memory_copy(rule->id, text, 9);
        memory_to_lower_ascii(rule->id, 9);
        rule->id[9] = end;
        if (!text[9])
                return true;
        rest = text + 10;
        if (string_has_prefix(rest, "-@"))
        {
                if (!usb_port_valid(rest + 2) && !usb_place_valid(rest + 2))
                        return false;
                string_copy_bounded(rule->key, "-", sizeof(rule->key));
                string_copy_bounded(rule->port, rest + 2, sizeof(rule->port));
                return true;
        }
        //      A serial too long to keep, as usb_key spells it.
        if (string_has_prefix(rest, "-#"))
        {
                if (string_length(rest) != 18 || string_span_of_set(rest + 2, "0123456789abcdef") != 16)
                        return false;
                string_copy_bounded(rule->key, rest, sizeof(rule->key));
                return true;
        }
        if (!usb_key_typed(rest))
                return false;
        string_copy_bounded(rule->key, rest, sizeof(rule->key));
        return true;
}

// A word a verb names, parsed into rule; said and refused, 1, when it names neither a port nor an id. What
// it names keeps no classes until a device it names is seen to have some (usb_policy_line parses its own).
static b32 usb_id_word(string_address word, usb_rule address_to rule)
{
        rule->kinds = 0;
        return usb_id_parse(word, rule) ? 0
               : host_refuse("%w is no port (like 1-4.1) and no device (VID:PID, or VID:PID/SERIAL as device shows it)\n",
                             writer_terminal_name, word);
}

// A rule as the policy keeps it and device shows it: its id, and an allow rule's classes after a blank.
static fn usb_rule_text(p8 address_to into, positive room, usb_rule address_to rule)
{
        bool pinned = string_equals((string_address)rule->key, "-");

        (void)string_join(into, room, (string_address)rule->id, "/", (string_address)rule->key,
                          pinned ? (string_address) "@" : (string_address) "",
                          pinned ? (string_address)rule->port : (string_address) "");
        for (positive kind = 0, first = 1; kind < USB_KINDS; kind++)
                if ((rule->kinds >> kind) & 1)
                {
                        string_append_bounded(into, first ? (string_address) " " : (string_address) ",", room);
                        string_append_bounded(into, usb_kinds[kind], room);
                        first = 0;
                }
}

// The classes of a list a comma apart (storage,hid), as a rule keeps them; false for a word that is none.
static bool usb_kinds_parse(string_address text, p8 address_to kinds)
{
        *kinds = 0;
        for (;;)
        {
                positive length = (positive)(string_first_of_or_end(text, ',') - text);
                bipolar kind = usb_kind_word(text, length);

                if (kind < 0)
                        return false;
                *kinds |= (p8)(1u << kind);
                if (!text[length])
                        return true;
                //      A comma is between two words, never after the last one.
                if (text[length] != ',' || !text[length + 1])
                        return false;
                text += length + 1;
        }
}

/* A sysfs file of a device or an interface, as it is but for the newline the
   kernel ends it with: a serial or a product is the device's to choose, and
   nothing else of it is trimmed. Its length, or negative. */
static bipolar usb_read(string_address name, string_address leaf, p8 address_to into, positive room)
{
        p8 path[HOST_PATH_ROOM];
        bipolar got;

        into[0] = end;
        if (!string_join(path, sizeof(path), USB_DEVICES "/", name, "/", leaf))
                return -1;
        got = file_slurp_once_at(AT_FDCWD, (string_address)path, into, room);
        if (got > 0 && into[got - 1] == '\n')
                into[--got] = end;
        return got;
}

static bool usb_says(string_address name, string_address leaf, string_address word)
{
        p8 text[16];

        return usb_read(name, leaf, text, sizeof(text)) >= 0 && string_equals((string_address)text, word);
}

/* One file of a device set to a word, when it says another: a device already
   as it should be is never written, so a pass that changes nothing
   re-enumerates nothing. */
static bipolar usb_set(string_address name, string_address leaf, string_address word)
{
        p8 path[HOST_PATH_ROOM];

        if (usb_says(name, leaf, word))
                return 0;
        if (!string_join(path, sizeof(path), USB_DEVICES "/", name, "/", leaf))
                return -1;
        return tune_write((string_address)path, word);
}

/*
        The device lock. A pass holds it while it writes authorized files, and
        the kernel answers a device's only once the device has (a set
        configuration, with its timeouts, for one that stalls): so every
        taker waits a bounded time, a verb says why after two seconds, and
        an interrupted wait is asked again. Negative: the open's own error,
        or -ETIMEDOUT.
*/
#define USB_LOCK_WAIT_NS ((p64)20000000000)
#define USB_LOCK_SAY_NS ((p64)2000000000)
#define USB_LOCK_POLL_NS ((p64)50000000)

static bipolar usb_lock_take(bool say)
{
        p64 start = system_clock_ns(HOST_CLOCK_BOOTTIME);
        bool said = false;

        for (;;)
        {
                bipolar lock = host_lock(USB_LOCK_PATH, false);
                p64 waited = system_clock_ns(HOST_CLOCK_BOOTTIME) - start;

                if (lock >= 0 || (lock != -EAGAIN && lock != -EINTR))
                        return lock;
                if (waited >= USB_LOCK_WAIT_NS)
                        return -ETIMEDOUT;
                if (say && !said && waited >= USB_LOCK_SAY_NS)
                {
                        host_say(log, host_label "device waits for the device lock: the watcher is judging a device "
                                                 "that is slow to answer\n");
                        said = true;
                }
                host_pause(USB_LOCK_POLL_NS);
        }
}

// A verb that could not take the device lock, said: it changed nothing.
static b32 usb_lock_refused(bipolar error)
{
        b32 owner = host_lock_owner(USB_WATCHER_PATH);

        if (error != -ETIMEDOUT)
                return host_fail(USB_LOCK_PATH, error);
        return owner ? host_refuse("the device lock is still held after 20 seconds (the watcher, pid %p, is judging a "
                                   "device that does not answer); nothing was changed\n",
                                   (positive)owner)
                     : host_refuse("the device lock is still held after 20 seconds; nothing was changed\n");
}

/*
        Interfaces authorized by a pass are asked to bind (drivers_probe)
        once the device lock is let go: a driver's probe of a device that stalls
        waits out its own timeouts, and the next verb is not held behind it.
*/
static p8 usb_probes[USB_INTERFACES_MOST][USB_PORT_ROOM];
static positive usb_probe_count;

static fn usb_probe_flush(void)
{
        for (positive at = 0; at < usb_probe_count; at++)
                (void)tune_write(USB_PROBE, (string_address)usb_probes[at]);
        usb_probe_count = 0;
}

/* Whether the device at its port is still the one the census read: one
   plugged in since at the same port (or the same one enumerated again) has
   another devnum, and the next pass judges it. */
static bool usb_still(usb_device address_to device)
{
        p8 now[sizeof(device->devnum)];

        usb_read((string_address)device->port, "devnum", now, sizeof(now));
        return string_equals((string_address)now, (string_address)device->devnum);
}

/* A device's authorized set, unless it is no longer the one the census read. */
static bipolar usb_set_device(usb_device address_to device, string_address word)
{
        if (!usb_still(device))
                return -ESTALE;
        return usb_set((string_address)device->port, "authorized", word);
}

static bool usb_census_visit(string_address directory, string_address name, address_any opaque)
{
        usb_census address_to census = (usb_census address_to)opaque;
        positive length = string_length(name);
        p8 word[USB_SERIAL_ROOM];

        (void)directory;
        if (length >= USB_PORT_ROOM ||
            string_span_of_set(name, "0123456789abcdefghijklmnopqrstuvwxyz.:-") != length)
                return true;

        if (string_first_of(name, ':'))
        {
                usb_interface address_to one;
                p32 class;
                p32 subclass;
                p8 path[HOST_PATH_ROOM];

                if (census->interfaces == USB_INTERFACES_MOST)
                {
                        census->dropped++;
                        return true;
                }
                one = census->interface + census->interfaces++;
                memory_zero(one, sizeof(*one));
                string_copy_bounded(one->name, name, sizeof(one->name));
                usb_read(name, "bInterfaceClass", word, sizeof(word));
                class = radio_hex(word);
                usb_read(name, "bInterfaceSubClass", word, sizeof(word));
                subclass = radio_hex(word);
                usb_read(name, "bInterfaceProtocol", word, sizeof(word));
                one->kind = usb_kind(class, subclass, radio_hex(word));
                one->authorized = usb_says(name, "authorized", "1");
                one->bound = string_join(path, sizeof(path), USB_DEVICES "/", name, "/driver") &&
                             system_access_at(AT_FDCWD, (string_address)path, 0) >= 0;
                return true;
        }

        usb_device address_to device;
        p8 vendor[8];
        p8 product[8];
        bipolar got;

        if (census->count == USB_MOST)
        {
                census->dropped++;
                return true;
        }
        if (!host_starts(name, "usb") && !usb_port_valid(name))
                return true;
        //      A device that went while it was read is not there.
        if (usb_read(name, "idVendor", vendor, sizeof(vendor)) != 4 ||
            usb_read(name, "idProduct", product, sizeof(product)) != 4 ||
            string_span_of_set((string_address)vendor, "0123456789abcdef") != 4 ||
            string_span_of_set((string_address)product, "0123456789abcdef") != 4)
                return true;
        device = census->device + census->count++;
        memory_zero(device, sizeof(*device));
        string_copy_bounded(device->port, name, sizeof(device->port));
        (void)string_join(device->id, sizeof(device->id), (string_address)vendor, ":",
                          (string_address)product);
        device->root = host_starts(name, "usb");
        (void)usb_read(name, "devnum", device->devnum, sizeof(device->devnum));
        got = usb_read(name, "serial", word, sizeof(word));
        usb_key(device->key, word, got);
        //      A root hub's serial is its controller's bus address, the kernel's to say: the start of a place.
        if (device->root && got > 0 && string_span_of_set((string_address)word, USB_CONTROLLER_SET) == (positive)got &&
            !string_join(device->place, sizeof(device->place), (string_address)word, "/", (string_address)product))
                device->place[0] = end;
        got = usb_read(name, "product", device->product, sizeof(device->product));
        device->product_length = got > 0 ? (positive)got : 0;
        device->says_fixed = usb_says(name, "removable", "fixed");
        device->authorized = usb_says(name, "authorized", "1");
        return true;
}

static usb_device address_to usb_find(usb_census address_to census, string_address port)
{
        for (positive at = 0; at < census->count; at++)
                if (string_equals((string_address)census->device[at].port, port))
                        return census->device + at;
        return null;
}

// Whether an interface is one of a device's: its name is the device's and a colon.
static bool usb_owns(usb_device address_to device, usb_interface address_to one)
{
        positive length = string_length((string_address)device->port);

        return !string_compare_max((string_address)one->name, (string_address)device->port, length) &&
               one->name[length] == ':';
}

// Whether a device shows any interface: whether it is configured.
static bool usb_shows(usb_census address_to census, usb_device address_to device)
{
        for (positive at = 0; at < census->interfaces; at++)
                if (usb_owns(device, census->interface + at))
                        return true;
        return false;
}

/*
        Every device and interface the kernel shows, in the order of their
        ports. Fixed is what a device says of the port it is on, and a hub's
        descriptor says it of its own ports: a hub somebody plugs in can call
        every port it has fixed. So a device is fixed only when it says so
        and so does every hub between it and the root hub.
*/
static b32 usb_device_order(address_any left, address_any right)
{
        return string_compare((string_address)((usb_device address_to)left)->port,
                              (string_address)((usb_device address_to)right)->port);
}

static b32 usb_interface_order(address_any left, address_any right)
{
        return string_compare((string_address)((usb_interface address_to)left)->name,
                              (string_address)((usb_interface address_to)right)->name);
}

static fn usb_census_take(usb_census address_to census)
{
        census->count = 0;
        census->interfaces = 0;
        census->dropped = 0;
        host_each_entry(USB_DEVICES, usb_census_visit, census);

        //      Names are unique, so the order is the one any sort gives.
        qsort(census->device, census->count, sizeof(census->device[0]), usb_device_order);
        qsort(census->interface, census->interfaces, sizeof(census->interface[0]), usb_interface_order);

        for (positive at = 0; at < census->count; at++)
        {
                usb_device address_to device = census->device + at;
                p8 up[USB_PORT_ROOM];

                device->fixed = !device->root && device->says_fixed;
                string_copy_bounded(up, device->port, sizeof(up));
                while (device->fixed)
                {
                        p8 address_to dot = memory_last_of(up, '.', string_length((string_address)up));
                        usb_device address_to above;

                        //      No dot: what is above it is the root hub.
                        if (!dot)
                                break;
                        *dot = end;
                        above = usb_find(census, (string_address)up);
                        device->fixed = above && above->says_fixed;
                }
        }

        //      A device's place is its root hub's and the path after the bus number (usb_place_valid).
        for (positive at = 0; at < census->count; at++)
        {
                usb_device address_to device = census->device + at;
                positive bus = string_span_of_set((string_address)device->port, "0123456789");
                p8 hub[USB_PORT_ROOM];
                usb_device address_to root;

                if (device->root || !string_join(hub, sizeof(hub), "usb", (string_address)device->port))
                        continue;
                hub[3 + bus] = end;
                root = usb_find(census, (string_address)hub);
                if (root && root->place[0] &&
                    !string_join(device->place, sizeof(device->place), (string_address)root->place, "/",
                                 (string_address)device->port + bus + 1))
                        device->place[0] = end;
        }
}

/* Whether a rule names a device: its id, and its serial wherever it is; with
   no serial (-), the place it is pinned to. A pin written before places
   (usb_port_valid's 1-4) is read as it was, by the port its bus number
   gives -- and as it was, it names a device whose serial is too long to
   keep as well, which was spelled - then -- until device lock or device allow
   writes it again by place or by the serial's hash. */
static bool usb_rule_names(usb_rule address_to rule, usb_device address_to device)
{
        bool none = string_equals((string_address)device->key, "-");

        if (!string_equals((string_address)rule->id, (string_address)device->id))
                return false;
        if (!string_equals((string_address)rule->key, "-"))
                return string_equals((string_address)rule->key, (string_address)device->key);
        if (usb_port_valid((string_address)rule->port))
                return (none || string_has_prefix(device->key, "-#")) &&
                       string_equals((string_address)rule->port, (string_address)device->port);
        return none && device->place[0] && string_equals((string_address)rule->port, (string_address)device->place);
}

static usb_rule address_to usb_rule_for(usb_policy address_to policy, usb_device address_to device)
{
        for (positive at = 0; at < policy->count; at++)
                if (usb_rule_names(policy->rule + at, device))
                        return policy->rule + at;
        return null;
}

/*
        Why a device is let in or held, which is the whole policy. A deny rule
        holds it in either mode, a fixed one included: the owner said so of
        that device, and fixed only keeps the lock's default off what is
        built into the machine. Off lets in the rest. Locked, a fixed device
        or one an allow rule names is let in (usb_interface_let says which of
        its interfaces), and one no rule names is let in only when some class
        is allowed, with only the interfaces of those classes; with every
        class denied it is held, configured never.
*/
enum
{
        USB_WHY_OPEN,
        USB_WHY_FIXED,
        USB_WHY_RULE,
        USB_WHY_DENY,
        USB_WHY_CLASS,
        USB_WHY_NONE
};

static const string_address usb_why[] = {"open", "fixed", "rule", "deny rule", "class", "no rule"};

#define usb_let(why) ((why) != USB_WHY_DENY && (why) != USB_WHY_NONE)

static p8 usb_judge(usb_policy address_to policy, usb_device address_to device)
{
        usb_rule address_to rule = usb_rule_for(policy, device);

        if (rule && rule->kind == USB_DENY)
                return USB_WHY_DENY;
        if (!policy->lock)
                return USB_WHY_OPEN;
        if (device->fixed)
                return USB_WHY_FIXED;
        if (rule)
                return USB_WHY_RULE;
        return policy->allowed ? USB_WHY_CLASS : USB_WHY_NONE;
}

/* An interface of a device let in: all of them, open or fixed; those of the
   classes allowed, for one no rule names; and for one a rule names, those of
   the classes it had when it was allowed (all, for a rule that says none)
   and of the classes allowed besides. A device a rule names that shows a
   class it never had -- a stick that comes back as a keyboard too -- has
   that interface held. */
static bool usb_interface_let(usb_policy address_to policy, usb_device address_to device, p8 why,
                              usb_interface address_to one)
{
        usb_rule address_to rule = why == USB_WHY_RULE ? usb_rule_for(policy, device) : null;
        bool class = (policy->allowed >> one->kind) & 1;

        if (why == USB_WHY_CLASS)
                return class;
        if (rule && rule->kinds)
                return class || ((rule->kinds >> one->kind) & 1);
        return usb_let(why);
}

/* What a device says of itself, safe to print as radio_display makes a
   network's name safe, with a quote and a backslash spelled as well so a
   quoted field ends where it seems to; no more than USB_SHOWN of its bytes,
   and ... after one that was cut. */
static fn usb_shown(p8 address_to into, positive room, p8 address_to text, positive length)
{
        positive used = 0;
        positive at = 0;
        bool cut = length > USB_SHOWN;

        if (cut)
                length = USB_SHOWN;
        into[0] = end;
        while (at < length && used + 8 < room)
        {
                positive span = at + memory_escape_index(text + at, length - at, HEX_QUOTE | HEX_SLASH);

                radio_display(into + used, room - used - 5, text + at, span - at);
                used += string_length((string_address)into + used);
                if (span < length)
                {
                        used += radio_spell(into + used, text[span]);
                        into[used] = end;
                        span++;
                }
                at = span;
        }
        if (cut)
                string_append_bounded(into, "...", room);
}

/*
        The newest of the device log that room holds, as the bytes from its first
        whole line: where they start in text, and how many, or negative when
        the log cannot be read. A log no bigger than room is all of it, its
        first line whole; a bigger one is read from its end, and the line the
        room cuts is dropped. Through no link, as every state file is read.
*/
static bipolar usb_log_tail(p8 address_to text, positive room, p8 address_to address_to tail)
{
        system_path_identity facts;
        bipolar handle = system_open_at(AT_FDCWD, USB_LOG, FILE_READ | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
        bipolar got;
        bipolar size;
        positive length;
        positive from = 0;

        *tail = text;
        if (handle < 0)
                return handle;
        got = system_path_identity_at(handle, (string_address)"", SYSTEM_PATH_AT_EMPTY_PATH,
                                      SYSTEM_PATH_STATX_TYPE, address_of facts);
        if (got >= 0 && (facts.mode & 0170000) != 0100000)
                got = -22;
        size = got >= 0 ? system_seek(handle, 0, 2) : got;
        if (size < 0)
        {
                system_close(handle);
                return size;
        }
        length = (positive)size < room ? (positive)size : room;
        got = file_transfer_exact(syscall(pread64), handle, text, length, (p64)size - length);
        system_close(handle);
        if (got < 0)
                return got;
        if (length < (positive)size)
        {
                from = memory_span_without_byte(text, '\n', length);
                from += from < length;
        }
        *tail = text + from;
        return (bipolar)(length - from);
}

/*
        A line of the log: when, in UTC, and what was decided. The log is
        root's (0600, beside the policy), is only ever added to, and is cut
        to its newer half once it passes USB_LOG_MOST. Whoever writes holds
        the device lock, so nothing adds to the log while it is cut. What a
        device says of itself is in it only as usb_key and usb_shown spell
        it.
*/
static fn usb_log_line(string_address what)
{
        static p8 text[USB_LOG_MOST / 2];
        p8 line[USB_LOG_LINE + 32];
        p8 when[32];
        time_t stamp = time(null);
        tm broken;
        bipolar handle;
        bipolar size;
        bipolar got;
        p8 address_to tail;

        gmtime_r(address_of stamp, address_of broken);
        strftime(when, sizeof(when), "%Y-%m-%dT%H:%M:%SZ ", address_of broken);
        (void)string_join(line, sizeof(line) - 1, (string_address)when, what);
        string_append_bounded(line, "\n", sizeof(line));

        handle = host_open_state(AT_FDCWD, USB_LOG, 0600, FILE_APPEND);
        if (handle < 0)
                return;
        (void)system_write_all_checked((positive)handle, (const address_any)line,
                                       string_length((string_address)line));
        size = system_seek(handle, 0, 2);
        system_close(handle);
        if (size <= USB_LOG_MOST)
                return;

        //      The newer half, from its first whole line.
        got = usb_log_tail(text, sizeof(text), address_of tail);
        if (got < 0)
                return;
        (void)host_write_file(USB_LOG, tail, (positive)got, 0600, true);
}

/* A decision about a device, to the log, and to the terminal of whoever
   asked for it. */
static fn usb_tell(bool say, string_address what, usb_device address_to device, p8 why)
{
        p8 line[USB_LOG_LINE];
        p8 shown[USB_SHOWN * 4 + 8];

        usb_shown(shown, sizeof(shown), device->product, device->product_length);
        (void)string_join(line, sizeof(line), what, " ", (string_address)device->port, " ",
                          (string_address)device->id, "/", (string_address)device->key, " (",
                          usb_why[why], ") \"", (string_address)shown, "\"");
        usb_log_line((string_address)line);
        if (say)
                host_say(log, host_label "device %s\n", line);
}

static fn usb_tell_interface(bool say, string_address what, usb_interface address_to one, p8 why, bool let)
{
        p8 line[USB_LOG_LINE];
        bool class = why == USB_WHY_CLASS;

        (void)string_join(line, sizeof(line), "interface ", (string_address)one->name, " ",
                          usb_kinds[one->kind], " ", what, " (",
                          class            ? (string_address) "class "
                          : why == USB_WHY_RULE && !let ? (string_address) "not a class of its rule"
                                                         : usb_why[why],
                          class ? usb_kinds[one->kind] : (string_address) "", ")");
        usb_log_line((string_address)line);
        if (say)
                host_say(log, host_label "device %s\n", line);
}

static fn usb_tell_text(bool say, string_address first, string_address second)
{
        p8 line[USB_LOG_LINE];

        (void)string_join(line, sizeof(line), first, second);
        usb_log_line((string_address)line);
        if (say)
                host_say(log, host_label "device %s\n", line);
}

/*
        What the root hubs and usbcore take for what arrives. Locked, the
        interfaces of a device that is configured wait to be judged, and with
        every class denied a new device is not configured at all; off is the
        kernel's own. Whichever of a hub's two becomes 0 is written first,
        so no moment between the writes binds a driver that neither the old
        policy nor the new would have. usbcore's is what a controller that
        arrives later starts with: 0 while locked, so its devices wait until
        the watcher has seen it and set its own.
*/
/*
        A default the kernel would not take leaves a root hub configuring new
        devices while the mode is lock. It is said in the log when that
        starts and when it ends (USB_DEFAULTS_PATH says it is failing, from
        one process to the next), and the watcher walks every quarter second
        until a pass sets it (usb_defaults_failing), judging each new device
        as it comes, where it waited ten seconds for a uevent.
*/
#define USB_DEFAULTS_PATH HOST_STATE "/usb.defaults"

static bool usb_defaults_failing;

static fn usb_defaults(usb_policy address_to policy, usb_census address_to census)
{
        string_address device = policy->lock && !policy->allowed ? "0" : "1";
        string_address interface = policy->lock ? "0" : "1";
        bool held = true;
        p8 have[8];

        for (positive at = 0; at < census->count; at++)
        {
                string_address hub = (string_address)census->device[at].port;

                if (!census->device[at].root)
                        continue;
                if (device[0] == '0')
                        held &= usb_set(hub, "authorized_default", device) >= 0;
                held &= usb_set(hub, "interface_authorized_default", interface) >= 0;
                held &= usb_set(hub, "authorized_default", device) >= 0;
        }
        if (host_read_kernel(USB_MODULE_DEFAULT, have, sizeof(have)) >= 0 &&
            !string_equals((string_address)have, policy->lock ? "0" : "1"))
                held &= tune_write(USB_MODULE_DEFAULT, policy->lock ? "0" : "1") >= 0;
        usb_defaults_failing = system_access_at(AT_FDCWD, USB_DEFAULTS_PATH, 0) >= 0;
        if (held == usb_defaults_failing)
        {
                if (held)
                        (void)system_remove_at(AT_FDCWD, USB_DEFAULTS_PATH, 0);
                else
                        (void)host_write_text(USB_DEFAULTS_PATH, "1\n");
                usb_tell_text(false, held ? "the defaults hold again" : "a default could not be set: a new device may be "
                                                                         "configured until a pass sets it", "");
        }
        usb_defaults_failing = !held;
}

/* The device of the last pass at the same port with the same identity. */
static usb_device address_to usb_before(usb_census address_to before, usb_device address_to device)
{
        usb_device address_to was = before ? usb_find(before, (string_address)device->port) : null;

        return was && string_equals((string_address)was->id, (string_address)device->id) &&
                               string_equals((string_address)was->key, (string_address)device->key)
                   ? was
                   : null;
}

/*
        Each interface of a device that is let in, as the policy says: every
        one wanted is authorized before any is probed, since a driver such as
        cdc_ether binds two interfaces at once. An interface authorized binds
        nothing by itself: the kernel probes it when asked through
        drivers_probe, which usb_probe_flush asks once the device lock is let
        go. One held that the last pass (before) did not have is logged
        once. A device enumerated again since the census (another devnum)
        is left to the next pass.
*/
static fn usb_interfaces(usb_policy address_to policy, usb_census address_to census,
                         usb_device address_to device, p8 why, usb_census address_to before, bool say)
{
        //      Not the device the census read: the next pass judges what is there.
        if (!usb_still(device))
                return;
        for (positive at = 0; at < census->interfaces; at++)
        {
                usb_interface address_to one = census->interface + at;
                bool let;
                bool news = before != null;

                if (!usb_owns(device, one))
                        continue;
                let = usb_interface_let(policy, device, why, one);
                for (positive was = 0; news && was < before->interfaces; was++)
                        news = !string_equals((string_address)before->interface[was].name, (string_address)one->name);
                if (news && !let && !one->authorized)
                        usb_tell_interface(false, "held", one, why, let);
                if (let == one->authorized ||
                    usb_set((string_address)one->name, "authorized", let ? "1" : "0") < 0)
                        continue;
                one->authorized = let;
                one->fresh = let && !one->bound;
                if (one->fresh && usb_probe_count < USB_INTERFACES_MOST)
                        string_copy_bounded(usb_probes[usb_probe_count++], (string_address)one->name,
                                            sizeof(usb_probes[0]));
                usb_tell_interface(say, let ? "allowed" : "held", one, why, let);
        }
}

/*
        One pass of the policy over a census: the defaults, then every device
        as usb_judge says and the interfaces of each one let in. before is the
        last pass's census, whose devices are not news (null: none are). The
        answer is how many devices this pass let in, whose interfaces the
        kernel has just made and the next pass judges.
*/
static positive usb_apply(usb_policy address_to policy, usb_census address_to census,
                          usb_census address_to before, bool say)
{
        positive opened = 0;

        usb_defaults(policy, census);
        for (positive at = 0; at < census->count; at++)
        {
                usb_device address_to device = census->device + at;
                usb_device address_to was;
                p8 why;
                bool let;

                if (device->root)
                        continue;
                why = usb_judge(policy, device);
                let = usb_let(why);
                was = usb_before(before, device);
                if (before && !was)
                        usb_tell(false, device->authorized ? "seen allowed" : "seen held", device, why);
                if (let != device->authorized && usb_set_device(device, let ? "1" : "0") >= 0)
                {
                        usb_tell(say, let ? "allowed" : "held", device, why);
                        opened += let;
                        continue;
                }
                if (let && device->authorized)
                        usb_interfaces(policy, census, device, why, before, say);
        }
        return opened;
}

/* Passes until one lets nothing new in (three at most), each over a census
   of its own. before is what the first pass takes as known (null: nothing is
   news); after, when asked, is left holding the last census. */
static fn usb_apply_all(usb_policy address_to policy, usb_census address_to before,
                        usb_census address_to after, bool say)
{
        static usb_census census[2];
        positive pass = 0;
        usb_census address_to last = before;

        for (; pass < 3; pass++)
        {
                usb_census address_to now = census + (pass & 1);
                positive opened;

                usb_census_take(now);
                opened = usb_apply(policy, now, last, say);
                last = now;
                if (!opened)
                        break;
        }
        if (after && last != after)
                memory_copy(after, last, sizeof(*after));
}

static bool usb_policy_line(usb_policy address_to policy, string_address line)
{
        usb_rule rule;
        positive skip;

        if (string_equals(line, "mode lock") || string_equals(line, "mode off"))
        {
                policy->lock = line[5] == 'l';
                return true;
        }
        if (string_has_prefix(line, "class "))
        {
                string_address word = string_first_of(line + 6, ' ');
                bipolar kind = word ? usb_kind_word(line + 6, (positive)(word - line - 6)) : -1;

                if (kind < 0 || (!string_equals(word + 1, "allow") && !string_equals(word + 1, "deny")))
                        return false;
                if (word[1] == 'a')
                        policy->allowed |= (p8)(1u << kind);
                else
                        policy->allowed &= (p8) ~(1u << kind);
                return true;
        }
        rule.kind = string_has_prefix(line, "allow ") ? USB_ALLOW
                    : string_has_prefix(line, "deny ") ? USB_DENY
                                                       : 0;
        rule.kinds = 0;
        skip = rule.kind == USB_ALLOW ? 6 : 5;
        if (!rule.kind || policy->count == USB_RULES_MOST)
                return false;
        //      An allow rule's classes follow its id after a blank, which no
        //      id has: usb_key spells a blank in a serial.
        {
                string_address blank = string_first_of(line + skip, ' ');

                if (blank && (rule.kind != USB_ALLOW || !usb_kinds_parse(blank + 1, address_of rule.kinds)))
                        return false;
                if (blank)
                        *blank = end;
        }
        if (!usb_id_parse(line + skip, address_of rule) || !rule.key[0])
                return false;
        policy->rule[policy->count++] = rule;
        return true;
}

/* Which /root this is, as the kernel names it (its device and inode): a take
   binds the disk's over the session's, and a lock held in /run for one is
   not the other's. Empty when it cannot be said. */
static fn usb_root_mark(p8 address_to into, positive room)
{
        system_path_identity facts;
        p8 major[24];
        p8 minor[24];
        p8 inode[24];

        into[0] = end;
        if (system_path_identity_at(AT_FDCWD, "/root", 0, SYSTEM_PATH_STATX_IDENTITY, address_of facts) < 0)
                return;
        positive_into_string(major, facts.device_major);
        positive_into_string(minor, facts.device_minor);
        positive_into_string(inode, facts.inode);
        if (!string_join(into, room, (string_address)major, ":", (string_address)minor, ":", (string_address)inode))
                into[0] = end;
}

/* Whether a lock this /root could not keep is in force (USB_UNKEPT_PATH
   names this /root on its first line), and why, from its second. */
static bool usb_unkept_why(p8 address_to why, positive room)
{
        p8 text[256];
        p8 mark[64];
        bipolar got = host_read_state(USB_UNKEPT_PATH, text, sizeof(text));
        positive line;
        positive rest;

        why[0] = end;
        if (got <= 0)
                return false;
        usb_root_mark(mark, sizeof(mark));
        line = memory_span_without_byte(text, '\n', (positive)got);
        if (!mark[0] || line != string_length((string_address)mark) || memory_compare(text, mark, line))
                return false;
        line += line < (positive)got;
        rest = memory_span_without_byte(text + line, '\n', (positive)got - line);
        string_copy_bounded(why, (string_address)text + line, rest < room ? rest + 1 : room);
        return true;
}

/* The policy as /root/usb.policy keeps it. Not there is off with no rules;
   not readable (not root) is the same, and known false says so. A line that
   is no line of the policy is left out and counted. Where a lock this /root
   could not keep is in force (usb_policy_unkept), what is read is that
   lock, in /run, and unkept says so. */
static fn usb_policy_load(usb_policy address_to policy)
{
        static p8 text[USB_POLICY_ROOM];
        bipolar got = host_read_state(USB_POLICY, text, sizeof(text));
        positive at = 0;
        positive start;
        positive length;
        p8 why[8];

        memory_zero(policy, sizeof(*policy));
        policy->error = got < 0 ? got : 0;
        policy->known = got >= 0 || got == -ENOENT;
        if (got < 0 && usb_unkept_why(why, sizeof(why)))
        {
                got = host_read_state(USB_RUN_POLICY, text, sizeof(text));
                policy->unkept = got >= 0;
                policy->known |= policy->unkept;
        }
        policy->kept = got >= 0;
        policy->cut = got >= (bipolar)sizeof(text) - 1;
        while (got > 0 && host_line_next(text, (positive)got, address_of at, address_of start,
                                         address_of length))
        {
                p8 line[USB_RULE_ROOM];

                //      A file cut short ends in a line cut short, which could
                //      be a serial cut short: it is not read as a rule.
                if (!length || (policy->cut && start + length >= (positive)got))
                        continue;
                //      A policy saved by an editor that ends its lines with CR LF
                //      is the same policy: read with the CR in every serial and
                //      mode word, no line of it was a rule, the machine took it
                //      for off with no rules, and the next change wrote that.
                if (text[start + length - 1] == '\r' && !--length)
                        continue;
                if (length >= sizeof(line))
                {
                        policy->skipped++;
                        continue;
                }
                memory_copy(line, text + start, length);
                line[length] = end;
                policy->skipped += !usb_policy_line(policy, (string_address)line);
        }
}

// The policy as its file keeps it, into text; how long it is.
static positive usb_policy_text(usb_policy address_to policy, p8 address_to text, positive room)
{
        string_copy_bounded(text, policy->lock ? "mode lock\n" : "mode off\n", room);
        for (positive kind = 0; kind < USB_KINDS; kind++)
        {
                string_append_bounded(text, "class ", room);
                string_append_bounded(text, usb_kinds[kind], room);
                string_append_bounded(text, (policy->allowed >> kind) & 1 ? " allow\n" : " deny\n", room);
        }
        for (positive at = 0; at < policy->count; at++)
        {
                p8 rule[USB_RULE_ROOM];

                usb_rule_text(rule, sizeof(rule), policy->rule + at);
                string_append_bounded(text, policy->rule[at].kind == USB_ALLOW ? "allow " : "deny ", room);
                string_append_bounded(text, (string_address)rule, room);
                string_append_bounded(text, "\n", room);
        }
        return string_length((string_address)text);
}

/* The policy over /root/usb.policy, synced; a lock held in /run for want of
   it is let go once it is kept. */
static bipolar usb_policy_write(usb_policy address_to policy)
{
        static p8 text[USB_POLICY_ROOM];
        bipolar failed = host_write_file(USB_POLICY, text, usb_policy_text(policy, text, sizeof(text)), 0600, true);

        if (failed >= 0)
        {
                (void)system_remove_at(AT_FDCWD, USB_UNKEPT_PATH, 0);
                (void)system_remove_at(AT_FDCWD, USB_RUN_POLICY, 0);
        }
        return failed;
}

static b32 usb_policy_save(usb_policy address_to policy)
{
        bipolar failed = usb_policy_write(policy);

        return failed < 0 ? host_fail(USB_POLICY, failed) : 0;
}

/* A policy a verb may act on: whole, and read. What is read short, or not at
   all, is refused, since what it reads as is not the owner's policy: a verb
   that acted on it would let in what the rules past the cut hold, or write
   the policy over with what it says. */
static b32 usb_policy_too_long(void)
{
        return host_refuse("%s is too long to change here\n", USB_POLICY);
}

static b32 usb_policy_whole(usb_policy address_to policy)
{
        if (!policy->known)
                return host_fail(USB_POLICY, policy->error);
        return policy->cut ? usb_policy_too_long() : 0;
}

/*
        A lock put in force that /root/usb.policy does not keep -- it could
        not be written (a disk full or read only), or what is there cannot be
        read and is not written over: the policy in /run, which the
        watcher and every verb read in its place until the machine restarts
        or a verb keeps one, and why, beside which /root it is for. Negative
        when /run could not keep it either: nothing then holds the lock but
        what the kernel and the devices are set to now.
*/
static bipolar usb_policy_unkept(usb_policy address_to policy, string_address what, bipolar error)
{
        static p8 text[USB_POLICY_ROOM];
        p8 note[256];
        p8 mark[64];
        bipolar failed;

        usb_root_mark(mark, sizeof(mark));
        failed = host_write_file(USB_RUN_POLICY, text, usb_policy_text(policy, text, sizeof(text)), 0600, false);
        (void)string_join(note, sizeof(note), (string_address)mark, "\n" USB_POLICY " ", what, ": ",
                          file_reason(error), "\n");
        //      Both are needed for the lock to be read (usb_policy_load): a lock /run holds no note of is none.
        return failed < 0 ? failed
                          : host_write_file(USB_UNKEPT_PATH, note, string_length((string_address)note), 0600, false);
}

// Whether the policy holds anything, which is what the watcher is for.
static bool usb_watched(usb_policy address_to policy)
{
        bool deny = false;

        for (positive at = 0; at < policy->count; at++)
                deny |= policy->rule[at].kind == USB_DENY;
        return policy->kept && (policy->lock || deny);
}

/* A rule naming a device of a census. An allow rule keeps the classes of its
   interfaces, those authorized only when every is asked for false; one not
   configured shows none, and its rule lets in whatever it is. */
static fn usb_rule_from(usb_rule address_to rule, p8 kind, usb_device address_to device,
                        usb_census address_to census, bool every)
{
        rule->kind = kind;
        rule->kinds = 0;
        for (positive at = 0; kind == USB_ALLOW && at < census->interfaces; at++)
                if (usb_owns(device, census->interface + at) && (every || census->interface[at].authorized))
                        rule->kinds |= (p8)(1u << census->interface[at].kind);
        string_copy_bounded(rule->id, (string_address)device->id, sizeof(rule->id));
        string_copy_bounded(rule->key, (string_address)device->key, sizeof(rule->key));
        string_copy_bounded(rule->port, !string_equals((string_address)device->key, "-") ? (string_address) ""
                                        : device->place[0]                              ? (string_address)device->place
                                                                                        : (string_address)device->port,
                            sizeof(rule->port));
}

static bool usb_rule_same(usb_rule address_to one, usb_rule address_to other)
{
        return string_equals((string_address)one->id, (string_address)other->id) &&
               string_equals((string_address)one->key, (string_address)other->key) &&
               string_equals((string_address)one->port, (string_address)other->port);
}

/* Whether a rule of the policy is replaced by rule: the same identity, or one
   that names device (plugged in now, or null). A pin written before places
   goes with the rule that replaces it. */
static bool usb_rule_replaced(usb_rule address_to old, usb_rule address_to rule, usb_device address_to device)
{
        return usb_rule_same(old, rule) || (device && usb_rule_names(old, device));
}

/* The rules of the policy but those rule replaces, and how many went. */
static positive usb_rules_drop(usb_policy address_to policy, usb_rule address_to rule, usb_device address_to device)
{
        positive kept = 0;
        positive count = policy->count;

        for (positive at = 0; at < count; at++)
                if (!usb_rule_replaced(policy->rule + at, rule, device))
                        policy->rule[kept++] = policy->rule[at];
        policy->count = kept;
        return count - kept;
}

/* How many rules the policy would keep once rule replaces its own. */
static positive usb_rules_left(usb_policy address_to policy, usb_rule address_to rule, usb_device address_to device)
{
        positive left = 0;

        for (positive at = 0; at < policy->count; at++)
                left += !usb_rule_replaced(policy->rule + at, rule, device);
        return left;
}


/*
        The watcher: a process of its own for as long as the policy holds
        something. A uevent is only a wake-up and is never read: any of them,
        or a socket that overflowed, is another walk of sysfs, one walk for a
        burst (a device and its interfaces are several), and one every ten
        seconds whatever is heard. Where no uevent socket can be had it walks
        every quarter of a second, and so does it while a default will not
        hold (soon). Kernel-level holding (the defaults above) is what makes
        its latency safe: a new device is held by the kernel until a pass
        lets it in. The datagrams of a burst are dropped up to a count and a
        deadline: every kernel uevent wakes it (a trusted keyboard's battery
        reports among them), and what is left of a flood wakes the next wait
        at once, one pass for each.
*/
#define USB_DRAIN_MOST 512
#define USB_DRAIN_NS ((p64)100000000)

static fn usb_wait(bipolar events, bool soon)
{
        timespec span = {soon ? 0 : USB_REWALK_NS / 1000000000, soon ? USB_POLL_NS : 0};
        p64 until;

        if (events < 0)
        {
                host_pause(USB_POLL_NS);
                return;
        }
        if (descriptor_wait_readable(events, address_of span, null) <= 0)
                return;
        host_pause(USB_SETTLE_NS);
        until = system_clock_ns(HOST_CLOCK_BOOTTIME) + USB_DRAIN_NS;
        for (positive drained = 0; drained < USB_DRAIN_MOST; drained++)
                if (socket_receive((b32)events, null, 0, MSG_DONTWAIT, 0, 0) < 0 ||
                    system_clock_ns(HOST_CLOCK_BOOTTIME) >= until)
                        break;
}

static b32 usb_watch_run(void)
{
        static usb_census before;
        bipolar held = host_lock_file(USB_WATCHER_PATH, HOST_FILE_SETLK, null);
        bipolar events;
        bool first = true;
        bool waiting = false;

        //      Another watcher holds the lock of the watcher file (or the file cannot be had): one is enough, and
        //      this one ends with 1 and says nothing, for it is the usual end of a second start that raced the
        //      first (usb_watch_start checks for an owner, and two verbs can check at once). The verb says it
        //      in words where a person asked (the dispatcher's watch); the log is the watcher's own, and a
        //      watcher that is not running has nothing to add to it.
        if (held < 0)
                return 1;
        events = netlink_open_protocol(USB_UEVENT, 1);
        //      No USB here, or the sysfs it was walking is gone: nothing to hold.
        while (system_access_at(AT_FDCWD, USB_DEVICES, 0) >= 0)
        {
                static usb_policy policy;
                bipolar lock = usb_lock_take(false);

                //      A verb that has held the lock this long, or no lock to be had: the pass is
                //      asked again soon, and the log says so once (without the lock, as it cannot have it).
                if (lock < 0)
                {
                        if (!waiting)
                                usb_tell_text(false, "the watcher waits for the device lock: ", file_reason(lock));
                        waiting = true;
                        usb_wait(events, true);
                        continue;
                }
                waiting = false;
                usb_policy_load(address_of policy);
                if (!usb_watched(address_of policy))
                {
                        radio_unlock(lock);
                        break;
                }
                usb_apply_all(address_of policy, first ? null : address_of before, address_of before, false);
                radio_unlock(lock);
                usb_probe_flush();
                first = false;
                usb_wait(events, usb_defaults_failing);
        }
        if (events >= 0)
                socket_close((b32)events);
        system_close(held);
        return 0;
}

static fn usb_watch_start(void)
{
        if (host_lock_owner(USB_WATCHER_PATH))
                return;
        if (!host_detach(descriptors_none, descriptors_none))
                return;
        (void)usb_watch_run();
        system_call_1(syscall(exit_group), 0);
}

/* The watcher ended, waited for as long as the device lock is: one in a write
   to a device that stalls ends when the kernel gives up on it. */
static fn usb_watch_stop(void)
{
        p64 start = system_clock_ns(HOST_CLOCK_BOOTTIME);

        if (!host_lock_signal(USB_WATCHER_PATH, SIGTERM))
                return;
        while (host_lock_owner(USB_WATCHER_PATH) && system_clock_ns(HOST_CLOCK_BOOTTIME) - start < USB_LOCK_WAIT_NS)
                host_pause(USB_LOCK_POLL_NS);
}

/*
        Lock, with the device lock held: every device plugged in now that no rule
        names becomes an allow rule, every class is denied, and the policy is
        put in force. Locking so cuts off nothing in use -- the keyboard this
        was typed on, the mouse, the stick the data is on -- and that is the
        one net against a machine left with no way in: the lock never holds a
        device present when it runs, so a machine whose only keyboard is USB
        keeps it, whatever /proc/bus/input/devices says of others. usb_apply
        has no such net on purpose. A machine whose keyboard is gone (a kiosk
        with none, a keyboard swapped since) would let in the first keyboard
        plugged in after, which is the device this is for. A device a deny
        rule holds keeps its rule, and a fixed one needs none. Each rule keeps
        the classes of the interfaces that work now. A device the kernel
        holds unconfigured (usbcore.authorized_default=2 holds every one at
        boot) shows none, and a rule written then let in whatever it came
        back as -- a keyboard's rule let the same identity bring up storage
        and a network: usb_configure_held configures it first, its
        interfaces waiting, and its rule keeps the classes they say, as
        usb_rule_verb's allow does (a device none of whose interfaces work
        keeps the classes it has). One that cannot be configured, or shows
        no interface once it is, is not kept: held, it is let in by device
        allow PORT. Locked already, the lock keeps only what the policy lets
        work: a device it holds stays held. A lock carried over another
        /root's, which that /root's policy put in force this boot (the
        session's, before the disk was taken), is the same: what that lock
        holds was judged, and the first lock of this /root does not let it in.

        A lock that cannot be kept is put in force all the same: a boot's
        first lock that could not write /root/usb.policy (a full or read-only
        disk), or that found something there it cannot read and does not
        write over (at boot, and when asked for), used to leave the machine as the kernel had it -- on a
        sec_host_locked image every USB device held, the keyboard too, and
        nothing said -- and device lock refused with the held devices it had
        configured left half done. Now the lock is held in /run
        (usb_policy_unkept), in force until the machine restarts or a verb
        keeps a policy, the log and device say why, and device lock answers 1
        with the reason.
*/
enum
{
        USB_LOCK_ASKED,
        USB_LOCK_FIRST,
        USB_LOCK_CARRIED
};

/*
        A boot's first lock trusts what is plugged in once USB has settled,
        and a device that keeps plugging and unplugging keeps it from
        settling until the cap, fifteen seconds of uptime, when the lock was
        taken over whatever was there at that instant, the device doing the
        plugging included. So the settle keeps when each device (its port and
        identity) was first seen, and at the lock a device younger than the
        quiet time (USB_STILL_NS: first seen in the last two seconds of the
        settle, or since, and seen to arrive: what the settle's first census
        found is not young) is given no rule and is held: a lock taken because
        everything was quiet for two seconds has no such device, and one
        taken at the cap does not let in what was still coming and going.
        What it costs: a device the owner plugs in during those last two
        seconds, or one that resets itself as the cap falls, is held and
        waits for device allow PORT, and on a machine whose command line holds
        every device a keyboard that was only then enumerating has to be
        let in from another one or by ssh. It does not stop a second device,
        plugged in early and left still while another keeps the settle
        going: that one is trusted, as whatever is plugged in at the first
        boot is.
*/
typedef struct
{
        p8 port[USB_PORT_ROOM];
        p8 id[10];
        p8 key[USB_KEY_ROOM];
        p64 since;
} usb_age;

typedef struct
{
        usb_age age[USB_MOST];
        positive count;
        p64 cutoff; // first seen after this is young
} usb_settle;

static usb_age address_to usb_age_of(usb_settle address_to settle, usb_device address_to device)
{
        for (positive at = 0; at < settle->count; at++)
                if (string_equals((string_address)settle->age[at].port, (string_address)device->port) &&
                    string_equals((string_address)settle->age[at].id, (string_address)device->id) &&
                    string_equals((string_address)settle->age[at].key, (string_address)device->key))
                        return settle->age + at;
        return null;
}

static bool usb_young(usb_settle address_to settle, usb_device address_to device)
{
        usb_age address_to age = settle ? usb_age_of(settle, device) : null;

        return settle && (!age || age->since > settle->cutoff);
}

static positive usb_configure_held(usb_policy address_to policy, usb_census address_to census,
                                   usb_settle address_to settle)
{
        positive count = 0;

        for (positive at = 0; at < census->count; at++)
        {
                usb_device address_to device = census->device + at;

                if (device->root || device->fixed || device->authorized || usb_rule_for(policy, device) ||
                    usb_young(settle, device))
                        continue;
                //      Every root hub's new interfaces wait before the first is
                //      configured, so nothing binds before the policy says.
                for (positive hub = 0; !count && hub < census->count; hub++)
                        if (census->device[hub].root)
                                (void)usb_set((string_address)census->device[hub].port,
                                              "interface_authorized_default", "0");
                count += usb_set_device(device, "1") >= 0;
        }
        return count;
}

static b32 usb_lock_held(bool say, p8 how, usb_settle address_to settle)
{
        static usb_census census;
        static usb_policy policy;
        bool stay;
        bool write;
        positive kept = 0;
        bipolar error;
        bipolar run = 0;

        usb_policy_load(address_of policy);
        if (policy.cut)
                return usb_policy_too_long();
        stay = policy.lock || how == USB_LOCK_CARRIED;
        usb_census_take(address_of census);
        //      Refused before anything is configured: at most each device no rule names is a rule more.
        for (positive at = 0, more = 0; at < census.count; at++)
                if (!census.device[at].root && !census.device[at].fixed &&
                    !usb_rule_for(address_of policy, census.device + at) && policy.count + ++more > USB_RULES_MOST)
                        return host_refuse("more devices are plugged in than the %p rules a policy keeps; "
                                           "nothing was locked\n",
                                           (positive)USB_RULES_MOST);
        if (!stay && usb_configure_held(address_of policy, address_of census, settle))
                usb_census_take(address_of census);
        for (positive at = 0; at < census.count; at++)
        {
                usb_device address_to device = census.device + at;
                usb_rule address_to had = usb_rule_for(address_of policy, device);
                usb_rule rule;

                //      A pin written before places is written again by the place of the device it names,
                //      or by its serial's hash for a device whose serial is too long to keep.
                if (had && string_equals((string_address)had->key, "-") && usb_port_valid((string_address)had->port))
                {
                        if (!string_equals((string_address)device->key, "-"))
                        {
                                string_copy_bounded(had->key, (string_address)device->key, sizeof(had->key));
                                had->port[0] = end;
                        }
                        else if (device->place[0])
                                string_copy_bounded(had->port, (string_address)device->place, sizeof(had->port));
                }
                if (device->root || device->fixed || had)
                        continue;
                if (usb_young(settle, device))
                {
                        usb_tell(false, "not kept, it came while USB was settling:", device, USB_WHY_NONE);
                        continue;
                }
                usb_rule_from(address_of rule, USB_ALLOW, device, address_of census, false);
                if (!rule.kinds && !stay)
                        usb_rule_from(address_of rule, USB_ALLOW, device, address_of census, true);
                if (stay && (!device->authorized || (usb_shows(address_of census, device) && !rule.kinds)))
                        continue;
                if (!device->authorized || !rule.kinds)
                {
                        usb_tell(false, "not kept", device, USB_WHY_NONE);
                        continue;
                }
                //      Counted above, unless the census took a device more since.
                if (policy.count == USB_RULES_MOST)
                        break;
                policy.rule[policy.count++] = rule;
                kept++;
        }
        policy.lock = true;
        policy.allowed = 0;
        //      What cannot be read is not written over, at boot or asked for, as allow, deny, forget and class
        //      refuse it (usb_policy_whole): it may be the owner's, on a disk that is failing, or a link to
        //      the owner's policy. The lock is still put in force, from /run: refusing it would leave a
        //      machine whose policy cannot be read with no way to lock at all.
        write = policy.known;
        error = write ? usb_policy_write(address_of policy) : policy.error;
        if (error < 0)
        {
                run = usb_policy_unkept(address_of policy, write ? "could not be written" : "cannot be read", error);
                usb_tell_text(false, run < 0 ? "the lock is applied now, and not kept: "
                                             : "the lock is in force until the machine restarts, and not kept: ",
                              file_reason(error));
                if (run < 0)
                        usb_tell_text(false, "and /run could not keep it either: ", file_reason(run));
        }
        for (positive at = policy.count - kept; at < policy.count; at++)
        {
                p8 rule[USB_RULE_ROOM];

                usb_rule_text(rule, sizeof(rule), policy.rule + at);
                usb_tell_text(false, "rule added: allow ", (string_address)rule);
        }
        usb_tell_text(false, "mode lock", "");
        //      The mode is said before what the apply says of each device: settings reads the first line that has
        //      "device " in it as the New devices row's value, and a device held or allowed by this lock said it first.
        if (say && error >= 0)
                host_say(log, host_label "device lock: %p devices plugged in now are kept as rules; "
                                         "a new one waits for device allow\n",
                         kept);
        usb_apply_all(address_of policy, null, null, say);
        if (error < 0 && run < 0)
        {
                p8 line[256];

                //      Applied now, and the kernel holds what is new at its defaults, but nothing reads this lock again.
                (void)string_join(line, sizeof(line), USB_POLICY, write ? " could not be written (" : " cannot be read (",
                                  file_reason(error), "), and nor could " USB_RUN_POLICY " be (", file_reason(run),
                                  "): what is held now stays held, and nothing keeps the lock");
                return host_refuse("%s\n", (string_address)line);
        }
        if (error < 0)
                return write ? host_refuse(USB_POLICY " could not be written (%s): the lock is in force until the "
                                                      "machine restarts\n",
                                           file_reason(error))
                             : host_refuse(USB_POLICY " cannot be read (%s) and is not written over: the lock is in "
                                                      "force until the machine restarts\n",
                                           file_reason(error));
        return 0;
}

static b32 usb_lock_now(void)
{
        bipolar lock = usb_lock_take(true);
        b32 failed;

        if (lock < 0)
                return usb_lock_refused(lock);
        failed = usb_lock_held(true, USB_LOCK_ASKED, null);
        radio_unlock(lock);
        usb_probe_flush();
        //      A lock in force but not kept is watched as well.
        usb_watch_start();
        return failed;
}

static b32 usb_unlock_now(void)
{
        bipolar lock = usb_lock_take(true);
        static usb_policy policy;
        b32 failed;

        if (lock < 0)
                return usb_lock_refused(lock);
        usb_policy_load(address_of policy);
        //      Unlock is a way out of a lock, so an unreadable policy does not stop it: only one that is cut short.
        if (policy.cut)
        {
                radio_unlock(lock);
                return usb_policy_too_long();
        }
        policy.lock = false;
        failed = usb_policy_save(address_of policy);
        if (!failed)
        {
                usb_tell_text(false, "mode off", "");
                //      Said before the apply's lines, which begin "device allowed" and "device held": the New devices row reads the first.
                host_say(log, host_label "device unlock: every device is let in but those a deny rule holds\n");
                usb_apply_all(address_of policy, null, null, true);
        }
        radio_unlock(lock);
        usb_probe_flush();
        if (failed)
                return failed;
        if (!usb_watched(address_of policy))
                usb_watch_stop();
        return 0;
}

/*
        The device a word names: a port, or an id. A VID:PID alone is the one
        device plugged in now that says it, and is refused when none or more
        than one does, as is a whole id that more than one says (the ports are
        named); a whole id may name a device that is not plugged in.
        rule is the id the policy keeps, device the device plugged in now that
        it names, or null.
*/
static b32 usb_resolve(string_address word, usb_census address_to census, usb_rule address_to rule,
                       usb_device address_to address_to device)
{
        positive count = 0;
        p8 ports[120] = "";

        *device = null;
        usb_census_take(census);
        if (usb_port_valid(word))
        {
                *device = usb_find(census, word);
                return_if(!*device, host_refuse("no device is plugged in at %s\n", word));
                usb_rule_from(rule, 0, *device, census, true);
                return 0;
        }
        if (usb_id_word(word, rule))
                return 1;
        for (positive at = 0; at < census->count; at++)
        {
                usb_device address_to one = census->device + at;

                if (one->root || !string_equals((string_address)one->id, (string_address)rule->id) ||
                    (rule->key[0] && !usb_rule_names(rule, one)))
                        continue;
                string_append_bounded(ports, count ? " " : "", sizeof(ports));
                string_append_bounded(ports, (string_address)one->port, sizeof(ports));
                count++;
                *device = one;
        }
        if (rule->key[0])
        {
                p8 line[256];
                p8 many[24];

                //      Two devices with one serial (a clone, or two long serials with one hash) are one id: the rule
                //      names both, whichever is asked, and the one this configures and takes its classes from
                //      would otherwise be the last of them, unsaid.
                if (count < 2)
                        return 0;
                positive_into_string(many, count);
                (void)string_join(line, sizeof(line), (string_address)many, " devices plugged in share ",
                                  (string_address)rule->id, "/", (string_address)rule->key, " (ports ", (string_address)ports,
                                  "): an id names all of them, and so does a rule for any one; name the port of the one meant");
                return host_refuse("%s\n", (string_address)line);
        }
        return_if(!count, host_refuse("no %s is plugged in; name it as VID:PID/SERIAL\n", rule->id));
        return_if(count > 1, host_refuse("%p devices plugged in are %s; name one by its port or its serial, "
                                         "as device shows them\n",
                                         count, rule->id));
        usb_rule_from(rule, 0, *device, census, true);
        return 0;
}

/*
        allow and deny: a rule for the device the word names, in place of any
        rule it had, put in force at once and at every plug after. An allow
        rule for a device plugged in keeps the classes it shows now, every
        interface it has; one held and not configured is configured first,
        under the lock (the watcher would otherwise take it for a device no
        rule names and hold it again), so that its interfaces, which wait
        while the policy is locked, can say what it is, and held again if the
        rule is then not kept (the rule is written from what it shows once
        configured, so it cannot be kept first). A whole id for a
        device that is not plugged in lets it in whole.
*/
static b32 usb_rule_verb(string_address word, p8 kind)
{
        static usb_census census;
        usb_rule rule;
        usb_device address_to device;
        static usb_policy policy;
        p8 text[USB_RULE_ROOM];
        bipolar lock = usb_lock_take(true);
        bool configured = false;
        b32 failed;

        if (lock < 0)
                return usb_lock_refused(lock);
        failed = usb_resolve(word, address_of census, address_of rule, address_of device);
        usb_policy_load(address_of policy);
        if (!failed)
                failed = usb_policy_whole(address_of policy);
        //      Before anything is configured: a rule the policy has no room for changes nothing.
        if (!failed && usb_rules_left(address_of policy, address_of rule, device) >= USB_RULES_MOST)
                failed = (b32)(host_refuse("the policy keeps %p rules at most; device forget one first\n", (positive)USB_RULES_MOST));
        if (!failed && kind == USB_ALLOW && device && !device->authorized && usb_set_device(device, "1") >= 0)
        {
                p8 port[USB_PORT_ROOM];
                p8 devnum[sizeof(device->devnum)];

                usb_tell(true, "allowed", device, USB_WHY_RULE);
                string_copy_bounded(port, (string_address)device->port, sizeof(port));
                string_copy_bounded(devnum, (string_address)device->devnum, sizeof(devnum));
                usb_census_take(address_of census);
                device = usb_find(address_of census, (string_address)port);
                //      Its rule is written from what it shows now, so it has to be the same device.
                if (!device || !string_equals((string_address)device->devnum, (string_address)devnum))
                        failed = host_refuse("the device at %s went while it was let in; no rule was kept\n", port);
                else
                        configured = true;
        }
        if (!failed && device)
                usb_rule_from(address_of rule, kind, device, address_of census, true);
        rule.kind = kind;
        if (!failed)
                (void)usb_rules_drop(address_of policy, address_of rule, device);
        if (!failed && policy.count == USB_RULES_MOST)
                failed = (b32)(host_refuse("the policy keeps %p rules at most; device forget one first\n", (positive)USB_RULES_MOST));
        if (!failed)
        {
                policy.rule[policy.count++] = rule;
                failed = usb_policy_save(address_of policy);
        }
        if (failed)
        {
                //      A device let in for a rule that was not kept would stay allowed with no rule at all:
                //      it is put back as it was, held.
                if (configured && usb_set_device(device, "0") >= 0)
                        usb_tell(true, "held again", device, USB_WHY_NONE);
                radio_unlock(lock);
                return failed;
        }
        usb_rule_text(text, sizeof(text), address_of rule);
        usb_tell_text(false, kind == USB_ALLOW ? "rule added: allow " : "rule added: deny ",
                      (string_address)text);
        usb_apply_all(address_of policy, null, null, true);
        radio_unlock(lock);
        usb_probe_flush();
        if (usb_watched(address_of policy))
                usb_watch_start();
        host_say(log, host_label "device %s %s%s\n", kind == USB_ALLOW ? "allows" : "holds", text,
                 device ? ", now and at every plug" : "; it is not plugged in now");
        return 0;
}

static b32 usb_forget(string_address word)
{
        static usb_census census;
        usb_rule rule;
        usb_device address_to device = null;
        static usb_policy policy;
        p8 text[USB_RULE_ROOM];
        bipolar lock;
        positive matched = 0;
        b32 failed;

        if (usb_port_valid(word))
        {
                failed = usb_resolve(word, address_of census, address_of rule, address_of device);
                if (failed)
                        return failed;
        }
        else if (usb_id_word(word, address_of rule))
                return 1;

        lock = usb_lock_take(true);
        if (lock < 0)
                return usb_lock_refused(lock);
        usb_policy_load(address_of policy);
        failed = usb_policy_whole(address_of policy);
        if (failed)
        {
                radio_unlock(lock);
                return failed;
        }
        //      A VID:PID alone is the one rule for it.
        if (!rule.key[0])
                for (positive at = 0; at < policy.count; at++)
                        if (string_equals((string_address)policy.rule[at].id, (string_address)rule.id) &&
                            matched++ == 0)
                                rule = policy.rule[at];
        usb_rule_text(text, sizeof(text), address_of rule);
        if (matched > 1 || !rule.key[0] || !usb_rules_drop(address_of policy, address_of rule, device))
        {
                radio_unlock(lock);
                return matched > 1 ? host_refuse("%p rules are for %s; name one as device shows it\n", matched, rule.id)
                                   : host_refuse("no rule is for %s\n", rule.key[0] ? text : rule.id);
        }
        failed = usb_policy_save(address_of policy);
        if (!failed)
        {
                usb_tell_text(false, "rule removed: ", (string_address)text);
                usb_apply_all(address_of policy, null, null, true);
        }
        radio_unlock(lock);
        usb_probe_flush();
        if (failed)
                return failed;
        if (!usb_watched(address_of policy))
                usb_watch_stop();
        host_say(log, host_label "device forgot %s\n", text);
        return 0;
}

// A class as the policy says it: allow or deny, or unknown where the policy is not read.
static string_address usb_class_word(usb_policy address_to policy, positive kind)
{
        return !policy->known                    ? (string_address) "unknown"
               : (policy->allowed >> kind) & 1 ? (string_address) "allow"
                                               : (string_address) "deny";
}

static fn usb_classes_say(usb_policy address_to policy)
{
        string_format(log, host_label "  classes:");
        for (positive kind = 0; kind < USB_KINDS; kind++)
                string_format(log, "%s %s %s", kind ? "," : "", usb_kinds[kind], usb_class_word(policy, kind));
        string_format(log, "\n");
}

static b32 usb_class(string_address address_to arguments, positive count)
{
        bipolar kind = count > 3 ? usb_kind_named(arguments[3]) : -1;
        static usb_policy policy;
        bipolar lock;
        b32 failed;
        bool allow;

        if (count == 3 || (count == 4 && kind >= 0))
        {
                usb_policy_load(address_of policy);
                if (count == 3)
                        usb_classes_say(address_of policy);
                else
                        string_format(log, host_label "device class %s %s\n", usb_kinds[kind],
                                      usb_class_word(address_of policy, kind));
                log_flush();
                return 0;
        }
        return_if(count != 5 || kind < 0 ||
                      (!string_equals(arguments[4], "allow") && !string_equals(arguments[4], "deny")),
                  host_usage());
        host_need_root();
        allow = arguments[4][0] == 'a';
        lock = usb_lock_take(true);
        if (lock < 0)
                return usb_lock_refused(lock);
        usb_policy_load(address_of policy);
        failed = usb_policy_whole(address_of policy);
        if (failed)
        {
                radio_unlock(lock);
                return failed;
        }
        policy.allowed = allow ? policy.allowed | (p8)(1u << kind) : policy.allowed & (p8) ~(1u << kind);
        failed = usb_policy_save(address_of policy);
        if (!failed)
        {
                p8 line[48];

                (void)string_join(line, sizeof(line), usb_kinds[kind], allow ? " allow" : " deny");
                usb_tell_text(false, "class ", (string_address)line);
                usb_apply_all(address_of policy, null, null, true);
        }
        radio_unlock(lock);
        usb_probe_flush();
        if (failed)
                return failed;
        host_say(log, host_label "device class %s %s%s\n", usb_kinds[kind], allow ? "allow" : "deny",
                 policy.lock ? "" : " (in force while device is locked)");
        return 0;
}

static b32 usb_log_show(positive asked)
{
        static p8 text[USB_LOG_MOST + USB_LOG_LINE * 2];
        p8 address_to tail;
        bipolar got = usb_log_tail(text, sizeof(text) - 1, address_of tail);
        positive at;

        if (got == -ENOENT)
        {
                host_say(log, host_label "device has decided nothing yet\n");
                return 0;
        }
        return_if(got < 0, host_fail(USB_LOG, got));
        tail[got] = end;
        //      The start of the asked-th line from the end: past the asked-th newline counted back, a
        //      newline ending the last byte not counted.
        {
                positive2 nth = got > 1 ? memory_nth_last_of(tail, '\n', (positive)got - 1, asked) : (positive2){0, 0};

                at = nth.y == asked ? nth.x + 1 : 0;
        }
        //      The log holds only what usb_tell spelled; it is shown as a name is
        //      all the same, so a line written by anything else cannot act.
        for (positive start = at; start < (positive)got;)
        {
                positive length = memory_span_without_byte(tail + start, '\n', (positive)got - start);

                tail[start + length] = end;
                string_format(log, "%w\n", writer_terminal_name, (string_address)tail + start);
                start += length + 1;
        }
        log_flush();
        return 0;
}

// The words of the kernel's defaults, for device and for whoever cannot read the policy.
static bool usb_kernel_says(usb_census address_to census, p8 address_to into, positive room)
{
        bool locked = false;
        p8 word[8];

        string_copy_bounded(into, "  defaults:", room);
        for (positive at = 0; at < census->count; at++)
        {
                string_address hub = (string_address)census->device[at].port;
                p8 device[8];

                if (!census->device[at].root)
                        continue;
                usb_read(hub, "authorized_default", device, sizeof(device));
                usb_read(hub, "interface_authorized_default", word, sizeof(word));
                locked |= string_equals((string_address)device, "0") || string_equals((string_address)word, "0");
                string_append_bounded(into, " ", room);
                string_append_bounded(into, hub, room);
                string_append_bounded(into, " device ", room);
                string_append_bounded(into, (string_address)device, room);
                string_append_bounded(into, " interface ", room);
                string_append_bounded(into, (string_address)word, room);
                string_append_bounded(into, ",", room);
        }
        host_read_kernel(USB_MODULE_DEFAULT, word, sizeof(word));
        string_append_bounded(into, " new controllers ", room);
        string_append_bounded(into, word[0] ? (string_address)word : (string_address) "unknown", room);
        return locked;
}

/*
        moonwater device: the mode, the classes, the defaults the kernel holds,
        whether the watcher runs, then a line for every device below a root
        hub -- its port, its id and serial, allowed or held as the kernel has
        it and why the policy says so, its interfaces, and last what it says
        it is, quoted, since that is its own to say -- and the rules. For
        somebody who cannot read the policy the mode is what the kernel's
        defaults show, and the rest of it is unknown.
*/
/* Who holds the watcher's lock, asked without making or writing its file:
   0 for nobody; known false for somebody who cannot open it (it is root's,
   0600), who used to be told that no watcher ran. */
static b32 usb_watcher_owner(bool address_to known)
{
        host_record_lock lock = {.type = HOST_FILE_WRLCK};
        bipolar handle = system_open_at(AT_FDCWD, USB_WATCHER_PATH,
                                        FILE_READ | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
        bipolar asked;

        *known = handle >= 0 || handle == -ENOENT;
        if (handle < 0)
                return 0;
        do
                asked = system_call_3(syscall(fcntl), (positive)handle, HOST_FILE_GETLK, (positive)address_of lock);
        while (asked == -EINTR);
        system_close(handle);
        *known = asked >= 0;
        return asked < 0 || lock.type == HOST_FILE_UNLCK ? 0 : lock.pid;
}

static b32 usb_status(void)
{
        static usb_census census;
        static usb_policy policy;
        p8 line[USB_LOG_LINE];
        bool known;
        b32 watcher = usb_watcher_owner(address_of known);
        bool locked;

        usb_policy_load(address_of policy);
        usb_census_take(address_of census);
        locked = usb_kernel_says(address_of census, line, sizeof(line));
        string_format(log, host_label "device %s\n", (policy.known ? policy.lock : locked) ? "lock" : "unlock");
        usb_classes_say(address_of policy);
        string_format(log, host_label "%s\n", line);
        if (watcher)
                string_format(log, host_label "  watcher running, pid %p\n", (positive)watcher);
        else if (!known)
                string_format(log, host_label "  whether the watcher runs is root's to see\n");
        else
                string_format(log, host_label "  watcher not running\n");
        if (!policy.known)
                string_format(log, host_label "  the rules are root's\n");
        if (policy.unkept)
        {
                p8 why[256];

                (void)usb_unkept_why(why, sizeof(why));
                string_format(log, host_label "  in force until the machine restarts, not kept: %s\n", (string_address)why);
        }
        if (policy.skipped)
                string_format(log, host_label "  %p lines of %s are no rule and are left out\n", policy.skipped,
                              USB_POLICY);
        if (policy.cut)
                string_format(log, host_label "  %s is longer than is read\n", USB_POLICY);
        if (!census.count)
                string_format(log, host_label "  no USB controller\n");

        for (positive at = 0; at < census.count; at++)
        {
                usb_device address_to device = census.device + at;
                p8 shown[USB_SHOWN * 4 + 8];
                p8 why = usb_judge(address_of policy, device);

                if (device->root)
                        continue;
                usb_shown(shown, sizeof(shown), device->product, device->product_length);
                (void)string_join(line, sizeof(line), "  ", (string_address)device->port, " ",
                                  (string_address)device->id, "/", (string_address)device->key,
                                  device->authorized ? " allowed (" : " held (",
                                  policy.known ? usb_why[why] : (string_address) "unknown",
                                  policy.known && usb_let(why) != device->authorized
                                      ? (usb_let(why) ? "; device apply lets it in" : "; device apply holds it")
                                      : "",
                                  ")");
                for (positive one = 0; one < census.interfaces; one++)
                {
                        usb_interface address_to face = census.interface + one;

                        if (!usb_owns(device, face))
                                continue;
                        string_append_bounded(line, "; ", sizeof(line));
                        string_append_bounded(line, (string_address)face->name + string_length((string_address)device->port) + 1,
                                              sizeof(line));
                        string_append_bounded(line, " ", sizeof(line));
                        string_append_bounded(line, usb_kinds[face->kind], sizeof(line));
                        string_append_bounded(line, face->authorized ? " allowed" : " held", sizeof(line));
                }
                string_format(log, host_label "%s; \"%s\"\n", line, shown);
        }
        if (census.dropped)
                string_format(log, host_label "  and %p more the census does not keep\n", census.dropped);
        for (positive at = 0; at < policy.count; at++)
        {
                usb_rule_text(line, sizeof(line), policy.rule + at);
                string_format(log, host_label "  rule %s %s\n", policy.rule[at].kind == USB_ALLOW ? "allow" : "deny",
                              line);
        }
        log_flush();
        return 0;
}

static b32 usb_apply_verb(void)
{
        bipolar lock = usb_lock_take(true);
        static usb_policy policy;
        b32 failed;

        if (lock < 0)
                return usb_lock_refused(lock);
        usb_policy_load(address_of policy);
        //      A policy that is not read whole judges nothing: what it reads as is not what the owner wrote.
        failed = usb_policy_whole(address_of policy);
        if (!failed)
                usb_apply_all(address_of policy, null, null, true);
        radio_unlock(lock);
        usb_probe_flush();
        if (failed)
                return failed;
        if (usb_watched(address_of policy))
                usb_watch_start();
        host_say(log, host_label "device applied\n");
        return 0;
}

/*
        At boot, once /root is the one this session keeps: the policy put in
        force over what enumerated before init, and the watcher started when
        the policy holds anything. Every verdict restores. An ask's /root is
        the session's own, and on a machine whose command line holds every
        USB device (usbcore.authorized_default=2) the keyboard that answers
        the question is let in only here; the take that follows the answer
        restores again, with the disk's policy. The first restore of a boot
        logs every device as seen. A hardened machine (STRICT_HOST tight or
        locked) with no policy is locked once, at its first boot, by
        usb_first_lock: what is plugged in then is what it trusts, so a
        desktop's own keyboard keeps working. The default tier changes nothing
        until the owner locks.

        Whether a /root keeps a policy is what decides, and not whether a
        watcher runs: an ask's session locks its own /root and its watcher
        is running when the disk is taken, and a disk with no policy then
        locked nothing, the watcher found no policy in the disk's /root and
        ended, and the kernel was left holding every new device at the
        session's defaults with device saying unlock. On the default tier a
        policy-less /root taken after another one's lock was in force puts
        the kernel's own defaults back, which is what no policy means there.
*/
static fn usb_first_lock(void);

static fn usb_restore(void)
{
        static usb_census before;
        bool first = !host_lock_owner(USB_WATCHER_PATH);
        static usb_policy policy;
        bipolar lock = usb_lock_take(false);

        //      The policy is put in force at boot whether or not the lock can be had.
        if (lock < 0)
                usb_tell_text(false, "boot applies the policy without the device lock: ", file_reason(lock));
        usb_policy_load(address_of policy);
        if (policy.kept)
        {
                before.count = 0;
                usb_apply_all(address_of policy, first ? address_of before : null, null, false);
        }
        else if (!first && policy.known && MOONWATER_STRICT_HOST < STRICT_TIGHT)
                usb_apply_all(address_of policy, null, null, false);
        radio_unlock(lock);
        usb_probe_flush();
        if (!policy.kept && (MOONWATER_STRICT_HOST >= STRICT_TIGHT || !policy.known))
                usb_first_lock();
        else if (usb_watched(address_of policy))
                usb_watch_start();
}

// Every port below a root hub, and whether its device is configured: what a settle compares.
static fn usb_ports(usb_census address_to census, p8 address_to into, positive room)
{
        into[0] = end;
        for (positive at = 0; at < census->count; at++)
                if (!census->device[at].root)
                {
                        string_append_bounded(into, (string_address)census->device[at].port, room);
                        string_append_bounded(into, census->device[at].authorized ? (string_address) "+ " : (string_address) "- ",
                                              room);
                }
}

/*
        A hardened machine's first lock, once USB has settled: the devices of
        a boot go on enumerating after init starts (a hub and what is behind
        it the longest), and a snapshot taken before they have would hold the
        very keyboard it was meant to keep, which the first guest that tried
        it showed (tablet and keyboard enumerated after the restore, and held
        by it). The census is taken again every quarter second until it has
        not changed for two seconds and the machine has been up three, or it
        has been up fifteen; then the lock, under the device lock and only if
        nothing has made a policy meanwhile, and then this is the watcher. A
        process of its own does it, so the boot waits for none of it; the
        devices it has not yet judged are as the kernel has them (held from
        the start by usbcore.authorized_default=2 where the command line says
        so). A machine first locked long after it booted (setup use of a disk
        with no policy) is locked at once.

        One first lock settles at a time (USB_SETTLE_PATH): another asked for
        meanwhile -- the take's restore and the machine process's both ask --
        ends at once, since the one settling locks whichever /root is in
        place when it is done. Once a settle has ended this boot
        (USB_SETTLED_PATH), USB has settled and a later first lock -- the
        disk's, taken after an ask's session locked its own /root -- locks
        at once, carried: what the session's lock held stays held, and the
        devices it let in are kept. The session's watcher, if it still runs,
        reads the disk's policy at its next pass; this one ends when it
        cannot be the watcher.
*/
static fn usb_first_lock(void)
{
        static usb_census census;
        static usb_settle settle;
        static usb_settle next;
        static p8 seen[USB_MOST * (USB_PORT_ROOM + 2) + 1];
        static p8 now[sizeof(seen)];
        p64 still = 0;
        p64 uptime = 0;
        p64 began = 0;
        static usb_policy policy;
        bipolar settling;
        bipolar lock;
        bool carried;

        if (!host_detach(descriptors_none, descriptors_none))
                return;
        settling = host_lock_file(USB_SETTLE_PATH, HOST_FILE_SETLK, null);
        if (settling < 0)
                system_call_1(syscall(exit_group), 0);
        carried = system_access_at(AT_FDCWD, USB_SETTLED_PATH, 0) >= 0;
        seen[0] = end;
        settle.count = 0;
        while (!carried)
        {
                uptime = system_clock_ns(HOST_CLOCK_BOOTTIME);
                began = began ? began : uptime;
                usb_census_take(address_of census);
                //      When each device was first seen, carried from census to census.
                next.count = 0;
                for (positive at = 0; at < census.count; at++)
                {
                        usb_device address_to device = census.device + at;
                        usb_age address_to was = usb_age_of(address_of settle, device);
                        usb_age address_to age;

                        if (device->root)
                                continue;
                        age = next.age + next.count++;
                        string_copy_bounded(age->port, (string_address)device->port, sizeof(age->port));
                        string_copy_bounded(age->id, (string_address)device->id, sizeof(age->id));
                        string_copy_bounded(age->key, (string_address)device->key, sizeof(age->key));
                        age->since = was ? was->since : uptime;
                }
                memory_copy(address_of settle, address_of next, sizeof(settle));
                usb_ports(address_of census, now, sizeof(now));
                if (!string_equals((string_address)now, (string_address)seen) || !still)
                {
                        string_copy_bounded(seen, (string_address)now, sizeof(seen));
                        still = uptime;
                }
                if (uptime >= USB_UP_MOST_NS || (uptime >= USB_UP_LEAST_NS && uptime - still >= USB_STILL_NS))
                        break;
                host_pause(USB_POLL_NS);
        }
        //      Young is what was seen to arrive: what the first census found is not, so a lock taken long after
        //      boot, whose settle ends at its first census, keeps everything plugged in.
        settle.cutoff = uptime > began + USB_STILL_NS ? uptime - USB_STILL_NS : began;
        (void)host_write_text(USB_SETTLED_PATH, "1\n");
        lock = usb_lock_take(false);
        if (lock < 0)
                usb_tell_text(false, "the first lock is taken without the device lock: ", file_reason(lock));
        usb_policy_load(address_of policy);
        if (!policy.kept)
                (void)usb_lock_held(false, carried ? USB_LOCK_CARRIED : USB_LOCK_FIRST, carried ? null : address_of settle);
        radio_unlock(lock);
        usb_probe_flush();
        system_close(settling);
        (void)usb_watch_run();
        system_call_1(syscall(exit_group), 0);
}

// The watcher again, if it ended while the policy still holds something.
static fn usb_recover(void)
{
        static usb_policy policy;

        if (host_lock_owner(USB_WATCHER_PATH))
                return;
        usb_policy_load(address_of policy);
        if (usb_watched(address_of policy))
                usb_watch_start();
}

/*
        moonwater device list: the devices the system sees, one line each, for the settings window's
        list. Two blanks, then the name device allow and device deny take (a USB port, or a disk's name),
        what kind it is, its id and what it says it is for a USB device or its size for a disk, and last
        its state: allowed, blocked (a deny rule holds it) or waiting (held for want of an allow). A disk
        has no hold, so it is allowed; device deny of one is refused, as device allow of a name that is no
        port is. A root hub is a controller, not a device, and is not listed. A disk is a whole one in
        /sys/block that is not a loop, ram, device-mapper, optical or floppy disk, and one on a USB bus is
        listed as the USB device it is: its line is followed by "    drive NAME N MB" and the disk's
        segments. Any other disk's segments are followed by "    where internal BUS" or "    where external
        BUS" (usb_drive_bus). A reader from before these lines skips them: each starts with four blanks and is
        no part line. It reads and changes nothing and needs no root.
*/
/*
        The partitions of a disk the list shows, as the strip of its row says them. A segment is where a
        partition starts and how many bytes it is, its role (efi, linux, swap, other: from the name the kernel
        has from the GPT entry, PARTNAME in the partition's uevent, or free: the room no partition has, or
        unpartitioned for a disk with no partition table), and its name when the kernel has one. Free room
        is what the strip shows between the partitions and after the last. Start and size are sectors of
        512 bytes in sysfs; the segments are printed in order, one line each, as "    part START BYTES ROLE NAME". A disk with more
        partitions than the strip holds shows the first ones by start and ends its segments with "    more N", N being the
        partitions not shown; the room after the last shown is then free only up to the first one not shown.
        A partition's line, and an unpartitioned disk's where a file system is on the whole of it, end in what is on it:
        "fs TYPE LABEL NODE", the type and label its file system's superblock says (storage_probe_device; "-" for none, or
        where the node cannot be read, which needs root), and the kernel's name of its node in /dev, which a mount opens.
        A reader of the part lines takes their words by place and passes by the ones after those it knows.
*/
// Sixteen partitions and the free room before, between and after them are 33 segments: settings.c's SETTINGS_SEGMENTS. A drive with
// more partitions keeps the sixteen that start first, and says how many it does not show (a "more" line after its parts).
#define USB_SEGMENTS_MOST 16
#define USB_ROLE_EFI 0
#define USB_ROLE_LINUX 1
#define USB_ROLE_SWAP 2
#define USB_ROLE_OTHER 3
#define USB_ROLE_FREE 4
#define USB_ROLE_UNPARTITIONED 5
static const string_address usb_role_words[] = {"efi", "linux", "swap", "other", "free", "unpartitioned"};

typedef struct
{
        unsigned long long start, bytes;
        unsigned int role;
        b32 fs;                       // the line ends in what is on it: its file system and its node
        p8 name[64];
        p8 node[32];                  // the kernel's name of the partition's node (sdb1), the disk's own for an unpartitioned one
        p8 type[STORAGE_TYPE_ROOM];   // its file system's type, as storage_probe_device names it; empty for none
        p8 label[64];                 // and the label the file system carries, plained; empty for none
} usb_segment;

typedef struct
{
        string_address disk;
        usb_segment part[USB_SEGMENTS_MOST];
        positive count;
        positive more;                 // partitions not shown
        unsigned long long more_start; // the first start of those not shown: the room before it, after the kept ones, is free
        b32 unknown;                   // a partition whose place could not be read: no room is free
} usb_parts;

static fn usb_list_segment(unsigned long long start, unsigned long long bytes, unsigned int role, string_address name, const usb_segment address_to on)
{
        if (on && on->fs)
                host_say(log, "    part %p %p %s %s fs %s %s %s\n", (positive)start, (positive)bytes, usb_role_words[role], name ? name : (string_address)"-",
                         on->type[0] ? (string_address)on->type : (string_address)"-", on->label[0] ? (string_address)on->label : (string_address)"-",
                         (string_address)on->node);
        else
                host_say(log, "    part %p %p %s %s\n", (positive)start, (positive)bytes, usb_role_words[role], name ? name : (string_address)"-");
}

// A character as the line carries it: a blank or a control character (a newline would end the line) is an underscore.
static unsigned char usb_plain(unsigned char c)
{
        return c <= ' ' || c == 0x7f ? '_' : c;
}

// A name as one line carries it: the blanks and controls of the kernel's name are underscores. One cut short by room ends before
// the character it would have cut in two (a label is UTF-8).
static fn usb_plain_copy(p8 address_to into, positive room, string_address from)
{
        positive at = 0;

        for (; from[at] && at + 1 < room; at++)
                into[at] = (p8)usb_plain((unsigned char)from[at]);
        if (from[at])
        {
                positive lead = at;

                while (lead && (into[lead - 1] & 0xc0) == 0x80)
                        lead--;
                if (lead && lead - 1 + (into[lead - 1] >= 0xf0 ? 4u : into[lead - 1] >= 0xe0 ? 3u : into[lead - 1] >= 0xc0 ? 2u : 1u) > at)
                        at = lead - 1;
        }
        into[at] = 0;
}

/* What is on a partition, or on a disk with no table: its node's name, and the type and label its superblock says. A node that cannot
   be opened (no root) or holds nothing known has neither. */
static fn usb_segment_fs(usb_segment address_to one, string_address node)
{
        p8 path[HOST_PATH_ROOM];
        storage_identity identity;

        one->fs = 1, one->type[0] = one->label[0] = 0;
        usb_plain_copy(one->node, sizeof(one->node), node);
        if (!host_join(path, sizeof(path), "/dev/", node) || !storage_probe_device((string_address)path, address_of identity))
                return;
        usb_plain_copy(one->type, sizeof(one->type), (string_address)identity.type);
        usb_plain_copy(one->label, sizeof(one->label), (string_address)identity.label);
}

// A partition the strip does not show: its start is noted for the room after the kept ones, and it counts toward the note.
static fn usb_part_skipped(usb_parts address_to parts, unsigned long long start)
{
        if (!parts->more || start < parts->more_start)
                parts->more_start = start;
        parts->more++;
}

// A partition for the strip: the USB_SEGMENTS_MOST that start first are kept, and of the rest the first start is noted.
static fn usb_part_keep(usb_parts address_to parts, usb_segment address_to one)
{
        positive last = 0;

        if (parts->count < USB_SEGMENTS_MOST)
        {
                parts->part[parts->count++] = *one;
                return;
        }
        for (positive at = 1; at < parts->count; at++)
                if (parts->part[at].start > parts->part[last].start)
                        last = at;
        if (one->start < parts->part[last].start)
        {
                usb_part_skipped(parts, parts->part[last].start);
                parts->part[last] = *one;
        }
        else
                usb_part_skipped(parts, one->start);
}

static bool usb_part_visit(string_address directory, string_address name, address_any context)
{
        usb_parts address_to parts = (usb_parts address_to)context;
        usb_segment one;
        string_address rest;
        p8 path[HOST_PATH_ROOM], text[256];
        positive dlen = string_length(parts->disk);

        (void)directory;
        // A partition is the disk's name and digits: sda1, or nvme0n1p1 and mmcblk0p1 where the disk's name ends in a digit.
        if (string_length(name) <= dlen || string_compare_max(name, parts->disk, dlen))
                return true;
        rest = name + dlen;
        if (*rest == 'p' && byte_is_digit(parts->disk[dlen - 1]))
                rest++;
        if (!*rest)
                return true;
        for (string_address at = rest; *at; at++)
                if (!byte_is_digit(*at))
                        return true;
        memory_zero(&one, sizeof(one));
        // A partition whose start or size cannot be read is not shown, and where it is is not known: no room is free.
        if (!host_join(path, sizeof(path), "/sys/block/", parts->disk, "/", name, "/start") ||
            host_read_kernel(path, text, sizeof(text)) <= 0)
        {
                parts->more++;
                parts->unknown = 1;
                return true;
        }
        one.start = string_digits(text, null) * 512ull;
        if (!host_join(path, sizeof(path), "/sys/block/", parts->disk, "/", name, "/size") ||
            host_read_kernel(path, text, sizeof(text)) <= 0)
        {
                parts->more++;
                parts->unknown = 1;
                return true;
        }
        one.bytes = string_digits(text, null) * 512ull;
        if (host_join(path, sizeof(path), "/sys/block/", parts->disk, "/", name, "/uevent") &&
            host_read_kernel(path, text, sizeof(text)) > 0)
                for (p8 address_to line = text; *line;)
                {
                        p8 address_to stop = (p8 address_to)string_first_of_or_end((string_address)line, '\n');
                        positive room = 0;

                        if (stop - line > 9 && !string_compare_max(line, "PARTNAME=", 9))
                        {
                                for (p8 address_to at = line + 9; at < stop && room + 1 < sizeof(one.name); at++, room++)
                                        one.name[room] = usb_plain((unsigned char)*at);
                                one.name[room] = 0;
                        }
                        line = *stop ? stop + 1 : stop;
                }
        one.role = USB_ROLE_OTHER;
        if (string_find((string_address)one.name, (string_address) "EFI"))
                one.role = USB_ROLE_EFI;
        else if (string_find((string_address)one.name, (string_address) "swap"))
                one.role = USB_ROLE_SWAP;
        else if (string_find((string_address)one.name, (string_address) "Linux"))
                one.role = USB_ROLE_LINUX;
        usb_segment_fs(&one, name);
        usb_part_keep(parts, &one);
        return true;
}


static fn usb_list_strip(string_address name, unsigned long long total, usb_parts address_to parts)
{
        unsigned long long cursor = 0, span_end = total;

        if (!parts->count && !parts->more)
        {
                usb_segment whole;

                // A file system on the whole disk (a stick formatted with no table) is said as a partition's is.
                memory_zero(&whole, sizeof(whole));
                if (total)
                        usb_segment_fs(&whole, name), whole.fs = whole.type[0] != 0, usb_list_segment(0, total, USB_ROLE_UNPARTITIONED, null, &whole);
                return;
        }
        // Insertion by start: a disk's partitions are few, and sysfs lists them in no order.
        for (positive at = 1; at < parts->count; at++)
                for (positive back = at; back > 0 && parts->part[back].start < parts->part[back - 1].start; back--)
                {
                        usb_segment swap = parts->part[back];

                        parts->part[back] = parts->part[back - 1];
                        parts->part[back - 1] = swap;
                }
        // Room is free between the partitions kept, and after the last one up to the first one not shown: none of those
        // starts before the kept ones. Where a partition's place is not known, no room is free at all.
        if (parts->more && parts->more_start < span_end)
                span_end = parts->more_start;
        for (positive at = 0; at < parts->count; at++)
        {
                usb_segment address_to part = &parts->part[at];

                if (!parts->unknown && part->start > cursor)
                        usb_list_segment(cursor, part->start - cursor, USB_ROLE_FREE, null, null);
                usb_list_segment(part->start, part->bytes, part->role, part->name[0] ? (string_address)part->name : null, part);
                if (part->start + part->bytes > cursor)
                        cursor = part->start + part->bytes;
        }
        if (!parts->unknown && span_end > cursor)
                usb_list_segment(cursor, span_end - cursor, USB_ROLE_FREE, null, null);
        if (parts->more)
                // The segments of a disk of total bytes, in order, free room between and after its partitions included.
                host_say(log, "    more %p\n", (positive)(parts->more));
}

// A block device that is no drive a person keeps: a loop, ram, device-mapper, RAID, optical or floppy one, or an eMMC's boot area.
static b32 usb_not_drive(string_address name)
{
        return host_starts(name, "loop") || host_starts(name, "ram") || host_starts(name, "zram") || host_starts(name, "dm-") ||
               host_starts(name, "md") || host_starts(name, "sr") || host_starts(name, "fd") || (host_starts(name, "mmcblk") && string_find(name, "boot"));
}

/*
        The bus a drive is on, from its name and the sysfs path its /sys/class/block link names (empty where it has none), and for an
        MMC card the type its card says (SD, MMC or SDIO): sd for an SD card, mmc for an MMC one (a soldered eMMC), nvme, virtio, ata
        (SATA and IDE), and scsi for any other.
*/
static string_address usb_drive_bus(string_address name, string_address link, string_address type)
{
        if (host_starts(name, "mmcblk") || string_find(link, "/mmc"))
                return string_equals(type, "MMC") ? (string_address) "mmc" : (string_address) "sd";
        if (host_starts(name, "nvme") || string_find(link, "/nvme"))
                return "nvme";
        if (host_starts(name, "vd") || string_find(link, "/virtio"))
                return "virtio";
        if (string_find(link, "/ata"))
                return "ata";
        return "scsi";
}

// Whether a drive on a bus is one a person plugs in and takes away: one whose block device says it is removable, and an SD card.
static b32 usb_drive_external(string_address bus, b32 removable)
{
        return removable || string_equals(bus, "sd");
}

// A drive's size in sectors of 512 bytes, as sysfs says it (none where it cannot be read).
static unsigned long long usb_disk_sectors(string_address name)
{
        p8 sysfs[HOST_PATH_ROOM], text[32];

        return host_join(sysfs, sizeof(sysfs), "/sys/block/", name, "/size") && host_read_kernel(sysfs, text, sizeof(text)) > 0 ? string_digits(text, null) : 0;
}

// A drive's segments, as its strip says them (usb_list_strip): its partitions and the room between them.
static fn usb_disk_strip(string_address name, unsigned long long sectors)
{
        p8 sysfs[HOST_PATH_ROOM];
        usb_parts parts;

        memory_zero(&parts, sizeof(parts));
        parts.disk = name;
        if (host_join(sysfs, sizeof(sysfs), "/sys/block/", name))
                host_each_entry((string_address)sysfs, usb_part_visit, address_of parts);
        usb_list_strip(name, sectors * 512, &parts);
}

static bool usb_list_disk(string_address directory, string_address name, address_any context)
{
        p8 sysfs[HOST_PATH_ROOM];
        p8 link[HOST_PATH_ROOM * 2];
        p8 type[16], shown[64];
        unsigned long long sectors;
        string_address bus;

        (void)directory;
        (void)context;
        link[0] = 0, type[0] = 0;
        if (usb_not_drive(name))
                return true;
        // A disk on a USB bus is listed as the USB device it is (usb_list_device_disk). Every other disk is listed, Thunderbolt and
        // unreadable ones too.
        if (!host_join(sysfs, sizeof(sysfs), "/sys/class/block/", name) ||
            (file_link_text(sysfs, link, sizeof(link)) > 0 && string_find(link, "/usb")))
                return true;
        sectors = usb_disk_sectors(name);
        // No verb holds a drive, so its row is shown and not pressed: it says what is true of it. Its name is one word in the line.
        usb_plain_copy(shown, sizeof(shown), name);
        host_say(log, "  %s drive %p MB view only\n", (string_address)shown, (positive)(sectors * 512 / 1000000ull));
        usb_disk_strip(name, sectors);
        // Where it is, after its segments: in the machine, or plugged in.
        if (host_join(sysfs, sizeof(sysfs), "/sys/block/", name, "/device/type"))
                host_read_kernel(sysfs, type, sizeof(type));
        bus = usb_drive_bus(name, (string_address)link, (string_address)type);
        host_say(log, "    where %s %s\n",
                 usb_drive_external(bus, host_join(sysfs, sizeof(sysfs), "/sys/block/", name) && host_block_flag(sysfs, "/removable"))
                     ? (string_address) "external"
                     : (string_address) "internal",
                 bus);
        return true;
}

/*
        A USB device's drives, under its line: each disk whose sysfs path runs through the device's own interface (its port, and then
        the port with a colon: a hub's path runs through its devices' too, and those are theirs), as "    drive NAME N MB" and its
        segments. A device a deny rule holds has no driver, and so no drive.
*/
static bool usb_list_device_disk(string_address directory, string_address name, address_any context)
{
        string_address port = (string_address)context;
        p8 sysfs[HOST_PATH_ROOM], link[HOST_PATH_ROOM * 2], through[64], shown[64];
        unsigned long long sectors;

        (void)directory;
        if (usb_not_drive(name) || !host_join(sysfs, sizeof(sysfs), "/sys/class/block/", name) || file_link_text(sysfs, link, sizeof(link)) <= 0 ||
            !host_join(through, sizeof(through), "/", port, "/", port, ":") || !string_find(link, through))
                return true;
        sectors = usb_disk_sectors(name);
        usb_plain_copy(shown, sizeof(shown), name);
        host_say(log, "    drive %s %p MB\n", (string_address)shown, (positive)(sectors * 512 / 1000000ull));
        usb_disk_strip(name, sectors);
        return true;
}

static b32 usb_list_show(void)
{
        static usb_census census;
        static usb_policy policy;

        usb_policy_load(address_of policy);
        usb_census_take(address_of census);
        for (positive at = 0; at < census.count; at++)
        {
                usb_device address_to device = &census.device[at];
                p8 shown[USB_SHOWN * 4 + 8];
                p8 why;
                string_address state;

                if (device->root)
                        continue;
                // The kernel's authorized file is what is true: allowed when it lets the device in. Otherwise a deny rule
                // holds it, and what the policy would let in at the next apply is waiting for its allow.
                why = usb_judge(address_of policy, device);
                state = device->authorized ? (string_address)"allowed" : why == USB_WHY_DENY ? (string_address)"blocked" : (string_address)"waiting";
                usb_shown(shown, sizeof(shown), device->product, device->product_length);
                host_say(log, "  %s usb %s %s %s\n", (string_address)device->port, (string_address)device->id, (string_address)shown, state);
                host_each_entry((string_address)"/sys/block", usb_list_device_disk, (address_any)device->port);
        }
        host_each_entry((string_address)"/sys/block", usb_list_disk, null);
        log_flush();
        return 0;
}

/*
        What is asked is judged before who asks: a word device does not take is a
        usage page for anybody, a change needs root, and reading the mode and
        the devices needs nothing.
*/
static b32 host_device(string_address address_to arguments, positive count)
{
        enum
        {
                DEV_CLASS, DEV_LIST, DEV_LOCK, DEV_UNLOCK, DEV_ALLOW,
                DEV_DENY, DEV_FORGET, DEV_LOG, DEV_APPLY, DEV_WATCH
        };
        static const named_byte verbs[] = {
            {"class", DEV_CLASS},   {"list", DEV_LIST},     {"lock", DEV_LOCK},
            {"unlock", DEV_UNLOCK}, {"allow", DEV_ALLOW},   {"deny", DEV_DENY},
            {"forget", DEV_FORGET}, {"log", DEV_LOG},       {"apply", DEV_APPLY},
            {"watch", DEV_WATCH},
        };

        if (count == 2)
                return usb_status();
        positive at = string_table_lookup(arguments[2], verbs);
        if (at == array_count(verbs))
                return host_usage();
        switch (verbs[at].value)
        {
        case DEV_CLASS:
                return usb_class(arguments, count);
        case DEV_LIST:
                return_if(count != 3, host_usage());
                return usb_list_show();
        case DEV_LOCK:
                return_if(count != 3, host_usage());
                host_need_root();
                return usb_lock_now();
        case DEV_UNLOCK:
                return_if(count != 3, host_usage());
                host_need_root();
                return usb_unlock_now();
        case DEV_ALLOW:
        case DEV_DENY:
                return_if(count != 4, host_usage());
                host_need_root();
                return usb_rule_verb(arguments[3], verbs[at].value == DEV_ALLOW ? USB_ALLOW : USB_DENY);
        case DEV_FORGET:
                return_if(count != 4, host_usage());
                host_need_root();
                return usb_forget(arguments[3]);
        case DEV_LOG:
        {
                positive asked = USB_LOG_SHOWN;
                positive used = 0;

                return_if(count > 4, host_usage());
                if (count == 4)
                        asked = string_digits_max(arguments[3], 4, address_of used);
                return_if(count == 4 && (!used || arguments[3][used] || !asked || asked > USB_LOG_ASKED),
                          host_usage());
                host_need_root();
                return usb_log_show(asked);
        }
        case DEV_APPLY:
                return_if(count != 3, host_usage());
                host_need_root();
                return usb_apply_verb();
        case DEV_WATCH:
        {
                //      device watch is the watcher itself, which lock, apply, allow, deny and boot start in a process of
                //      its own (usb_watch_start): not a verb for a person, so it is on neither the help page nor in
                //      the README. By hand it runs in the foreground, and a second one is refused.
                b32 owner;

                return_if(count != 3, host_usage());
                host_need_root();
                owner = host_lock_owner(USB_WATCHER_PATH);
                return_if(owner, host_refuse("the device watcher is already running, pid %p\n", (positive)owner));
                return usb_watch_run();
        }
        }
        return host_usage();
}

