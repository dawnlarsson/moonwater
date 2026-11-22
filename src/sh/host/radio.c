/*
        src/sh/host/radio.c: the verbs wifi, bluetooth, priority and wired: the switches, the saved networks, the air
        scanned, a join, its password and its state files (wifi.last), the status, the shared link.

        A run of src/sh/host.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. host_main, at the end
        of src/sh/host.c, names the function each verb runs.
*/

#define RADIO_SSID_MOST 32
#define RADIO_PASS_MOST 64
#define RADIO_WIFI_MOST 16
#define RADIO_RFKILL_WLAN 1
#define RADIO_RFKILL_BLUETOOTH 2
#define RADIO_RFKILL_CHANGE_ALL 3
#define RADIO_LOCK_PATH NET_STATE_DIR "/radio.lock"

typedef struct
{
        p8 ssid[RADIO_SSID_MOST + 1];
        p8 pass[RADIO_PASS_MOST + 1];
        p8 ssid_length;
        p8 pass_length;
} radio_network;

static fn radio_net_wake(void)
{
        bipolar handle;
        p8 one = '1';

        host_state_ready();
        system_call_4(syscall(mknodat), AT_FDCWD,
                      (positive)(string_address)NET_WAKE_PATH, S_IFIFO | 0600, 0);
        /* The FIFO the net loop made or this did, and nothing else: the
           open followed a link standing at the name and wrote a '1' over
           the first byte of whatever it named, as root. */
        handle = system_open_at(AT_FDCWD, NET_WAKE_PATH,
                                FILE_READ_WRITE | O_NONBLOCK | O_NOFOLLOW |
                                    O_CLOEXEC);
        if (handle < 0)
                return;
        if (!file_handle_is_pipe(handle))
        {
                system_close(handle);
                return;
        }

        system_write_all((positive)handle, address_of one, 1);
        system_close(handle);
}

/* One word and its newline over a state file. The room is a timezone name's
   room, because the clock's words come through here too. */
static bipolar radio_write_word(string_address path, string_address word)
{
        p8 line[96];
        p8 have[96];

        //      A word too long for the line is refused, not cut and written.
        if (string_length(word) > sizeof(line) - 2)
                return -ENAMETOOLONG;
        string_copy_bounded(line, word, sizeof(line));
        string_append_bounded(line, "\n", sizeof(line));
        //      A word that is already there is not written again: two syncs,
        //      of the file and of its directory, at every boot for nothing.
        if (host_read_state(path, have, sizeof(have)) == (bipolar)string_length(line) &&
            !string_compare(have, line))
                return 0;
        return host_write_file(path, line, string_length(line), 0644, true);
}

/* A word kept for the next boot, or the reason it could not be, the kernel's and
   not a guess, under the name of what was being kept: 1 once it is said. */
static b32 host_save(string_address label, string_address path, string_address word)
{
        bipolar failed = radio_write_word(path, word);

        return failed < 0 ? host_fail(label, failed) : 0;
}

static bipolar radio_lock(bool wait)
{
        return host_lock(RADIO_LOCK_PATH, wait);
}

static fn radio_unlock(bipolar handle)
{
        if (handle < 0)
                return;

        system_call_2(syscall(flock), (positive)handle, RADIO_LOCK_UN);
        system_close(handle);
}

static bipolar radio_rfkill(p8 type, bool block)
{
        bipolar handle = system_open_at(AT_FDCWD, "/dev/rfkill",
                                        O_WRONLY | O_CLOEXEC | O_NONBLOCK);
        bipolar sent;

        //      No /dev/rfkill is a machine with nothing to switch, and that
        //      is no failure to say.
        if (handle < 0)
                return handle == -ENOENT ? 0 : handle;

        sent = ul_rfkill_send(handle, 0, type, RADIO_RFKILL_CHANGE_ALL, block);
        system_close(handle);
        return sent;
}

/* Whether a radio's word and its rfkill write both went through; when not,
   the first that did not is said (to somebody waiting to hear it) and
   answered. */
static b32 radio_failed(bool report, string_address path, bipolar kept, bipolar sent)
{
        if (kept >= 0 && sent >= 0)
                return 0;
        if (report)
                host_fail(kept < 0 ? path : (string_address) "/dev/rfkill", kept < 0 ? kept : sent);
        return 1;
}

/* A radio's switch: the word that outlasts a boot, and the block that is the
   radio itself. */
static b32 radio_switch(string_address path, p8 type, bool on, bool report)
{
        bipolar kept = radio_write_word(path, on ? "on" : "off");

        return radio_failed(report, path, kept, radio_rfkill(type, !on));
}

/* Whether a line has no control byte: what a saved list may hold and still be
   read, and the zone and server names of the clock. */
static bool radio_text_plain(string_address text, positive length)
{
        positive at;

        for (at = 0; at < length; at++)
                if (text[at] < 32)
                        return false;
        return true;
}

/* Whether a name is one radio_display shows as it is, character for character:
   printable ASCII and whole UTF-8 characters from U+00A0 up, and no control
   byte, DEL, C1 control or byte that starts no character. What the verbs
   store is held to what is shown. */
static bool radio_text_shown(string_address text)
{
        positive length = string_length(text);

        for (positive at = 0; at < length;)
        {
                bool printable;

                at += file_terminal_step((const p8 address_to)text + at, length - at, true,
                                         address_of printable);
                if (!printable)
                        return false;
        }
        return true;
}

/* The next line of text, its start and length (the newline is neither), with
   the cursor past it; false once there is none. */
static bool host_line_next(p8 address_to text, positive size, positive address_to at,
                           positive address_to start, positive address_to length)
{
        if (*at >= size)
                return false;
        *start = *at;
        *length = memory_span_without_byte(text + *at, '\n', size - *at);
        *at += *length + (*at + *length < size);
        return true;
}

/* The saved networks, up to room of them, or the error of a list that is there
   and cannot be read: a list that is not there has none. cut, when asked for,
   says that there was more than this read: a full buffer, or rows left over.
   The writers answer an error and do not write a list back over what they
   could not read. */
static bipolar radio_wifi_read(radio_network address_to into, positive room,
                               bool address_to cut)
{
        static p8 text[8192];
        bipolar got = host_read_state(NET_WIFI_LIST, text, sizeof(text));
        positive count = 0;
        positive at = 0;
        positive start;
        positive length;
        bool want_ssid = true;

        memory_fill(into, 0, sizeof(radio_network) * room);
        if (cut)
                *cut = false;
        if (got < 0 && got != -ENOENT)
                return got;
        if (got <= 0)
                return 0;
        /* text holds the saved passphrases in the clear, exactly as the
           radio_network array below does, so it is scrubbed the same way
           before this frame is left to whatever runs in it next. */

        while (count < room &&
               host_line_next(text, (positive)got, address_of at, address_of start,
                              address_of length))
        {
                if (want_ssid)
                {
                        if (!length)
                                continue;
                        if (length > RADIO_SSID_MOST ||
                            !radio_text_plain((string_address)(text + start), length))
                        {
                                want_ssid = false;
                                continue;
                        }
                        memory_copy(into[count].ssid, text + start, length);
                        into[count].ssid[length] = end;
                        into[count].ssid_length = (p8)length;
                        into[count].pass[0] = end;
                        into[count].pass_length = 0;
                        want_ssid = false;
                        continue;
                }

                if (into[count].ssid_length)
                {
                        if (length &&
                            (length > RADIO_PASS_MOST ||
                             !radio_text_plain((string_address)(text + start),
                                               length)))
                        {
                                memory_fill(address_of into[count], 0,
                                            sizeof(into[count]));
                        }
                        else
                        {
                                memory_copy(into[count].pass, text + start, length);
                                into[count].pass[length] = end;
                                into[count].pass_length = (p8)length;
                                count++;
                        }
                }
                want_ssid = true;
        }

        if (cut)
                *cut = got >= (bipolar)sizeof(text) - 1 || (count == room && at < (positive)got);
        if (!want_ssid && count < room && into[count].ssid_length)
                count++;

        crypto_forget(text, sizeof(text));
        return (bipolar)count;
}

/* radio_wifi_read where an unreadable list is no list: the readers that only
   ask what is saved, and so have nothing to write back over it. */
static positive radio_wifi_load(radio_network address_to into, positive room,
                                bool address_to cut)
{
        bipolar count = radio_wifi_read(into, room, cut);

        return count > 0 ? (positive)count : 0;
}

/* The index of the saved network called ssid, or count when it is not saved. */
static positive radio_saved_index(const radio_network address_to networks, positive count,
                                  string_address ssid, positive ssid_length)
{
        for (positive at = 0; at < count; at++)
                if (networks[at].ssid_length == ssid_length &&
                    !memory_compare(networks[at].ssid, ssid, ssid_length))
                        return at;
        return count;
}

static bipolar radio_wifi_save(radio_network address_to networks, positive count)
{
        static p8 text[8192];
        positive used = 0;
        positive at;

        scope_exit(crypto_forget(text, sizeof(text)));
        for (at = 0; at < count; at++)
        {
                if (used + networks[at].ssid_length + networks[at].pass_length +
                        2 >=
                    sizeof(text))
                        return -1;
                memory_copy(text + used, networks[at].ssid,
                            networks[at].ssid_length);
                used += networks[at].ssid_length;
                text[used++] = '\n';
                memory_copy(text + used, networks[at].pass,
                            networks[at].pass_length);
                used += networks[at].pass_length;
                text[used++] = '\n';
        }

        return host_write_file(NET_WIFI_LIST, text, used, 0600, true);
}

/*
        What each saved network has taught the join, or its owner has said
        of it, beside the list in /root/wifi.rules: a line "WORD NAME" for
        each, the word one of radio_rule_words. pmf: an access point of the
        name offered management frame protection, and one that does not is
        never joined; sae: it was joined with WPA3, and is never joined with
        WPA2 again; wpa2: its owner lets the tight tier join it with WPA2
        (moonwater wifi add SSID - wpa2). The first two are learnt, by a join
        that worked, and forgotten with the network or when it is added again
        with another password, which is its owner saying that it changed; the
        third is what the last add of it said. A name has no line feed, and
        the first space ends the word.
*/
#define RADIO_RULES_ROOM 4096

static const string_address radio_rule_words[] = {
    (string_address) "pmf", (string_address) "sae", (string_address) "wpa2"};

/* The rule a line says, as its bit, for the name; 0 for none. */
static p8 radio_rule_of(const p8 address_to line, positive length, const p8 address_to ssid,
                        positive ssid_length)
{
        for (positive bit = 0; bit < 3; bit++)
        {
                positive word = string_length(radio_rule_words[bit]);

                if (length == word + 1 + ssid_length &&
                    !memory_compare(line, radio_rule_words[bit], word) && line[word] == ' ' &&
                    !memory_compare(line + word + 1, ssid, ssid_length))
                        return (p8)(1u << bit);
        }
        return 0;
}

static COLD p8 radio_rules_get(p8 address_to ssid, positive ssid_length)
{
        static p8 text[RADIO_RULES_ROOM];
        bipolar got;
        positive at = 0;
        positive start;
        positive length;
        p8 rules = 0;

        if (radio_station >= 0)
                return (p8)radio_station_number(RADIO_ASK_RULES_GET, ssid, ssid_length, null, 0, 0);
        got = host_read_state(RADIO_RULES_PATH, text, sizeof(text));

        while (got > 0 && host_line_next(text, (positive)got, address_of at, address_of start,
                                         address_of length))
                rules |= radio_rule_of(text + start, length, ssid, ssid_length);
        return rules;
}

/* The rules of ssid made what in the bits of mask, the others kept as they
   are; and the lines of a name no longer saved are dropped, which keeps the
   file to the saved networks' three each. The error of the file's write, or 0:
   a join's station asks the broker, which keeps the file, and has no answer
   to give it. */
static COLD bipolar radio_rules_put(p8 address_to ssid, positive ssid_length, p8 mask, p8 what)
{
        static p8 text[RADIO_RULES_ROOM];
        static p8 kept[RADIO_RULES_ROOM];
        radio_network networks[RADIO_WIFI_MOST];
        positive count;
        bipolar got;
        p8 rules = 0;
        positive at = 0;
        positive used = 0;
        positive start;
        positive length;
        bool unknown;

        if (radio_station >= 0)
        {
                (void)radio_station_number(RADIO_ASK_RULES_PUT, ssid, ssid_length, null, mask,
                                           what);
                return 0;
        }
        count = radio_wifi_load(networks, RADIO_WIFI_MOST, null);
        //      A saved list that is there but does not read is not known to
        //      be empty, so the rules are kept as they are; a list that is
        //      not there at all has no networks, and its rules go.
        unknown = !count && system_access_at(AT_FDCWD, NET_WIFI_LIST, 0) == 0;
        got = host_read_state(RADIO_RULES_PATH, text, sizeof(text));
        //      No file is no rules. One that is there and cannot be read (a link,
        //      a FIFO, a directory) or fills the room is rules not known, and the
        //      rules written over it were the one network's: what the other
        //      networks had taught the join, which is what stops its next
        //      downgrade, was gone with it.
        if (got < 0 && got != -ENOENT)
                return got;
        if (got >= (bipolar)sizeof(text) - 1)
                return -EFBIG;

        while (got > 0 && host_line_next(text, (positive)got, address_of at, address_of start,
                                         address_of length))
        {
                bool saved = unknown;
                p8 rule = radio_rule_of(text + start, length, ssid, ssid_length);

                rules |= rule;
                if (rule)
                        continue;
                for (positive one = 0; one < count && !saved; one++)
                        saved = radio_rule_of(text + start, length, networks[one].ssid,
                                              networks[one].ssid_length) != 0;
                if (saved && used + length + 1 <= sizeof(kept))
                {
                        memory_copy(kept + used, text + start, length);
                        used += length;
                        kept[used++] = '\n';
                }
        }
        crypto_forget(networks, sizeof(networks));
        rules = (p8)((rules & ~mask) | (what & mask));
        for (positive bit = 0; bit < 3; bit++)
        {
                positive word = string_length(radio_rule_words[bit]);

                if (!(rules & (1u << bit)) || used + word + 1 + ssid_length + 1 > sizeof(kept))
                        continue;
                memory_copy(kept + used, radio_rule_words[bit], word);
                used += word;
                kept[used++] = ' ';
                memory_copy(kept + used, ssid, ssid_length);
                used += ssid_length;
                kept[used++] = '\n';
        }
        //      A file that holds these bytes already is not written again, as
        //      radio_write_word does: an add of the same network and password
        //      must not need /root to take a write, so a read-only /root keeps
        //      its rules as they are.
        if (got >= 0 && got == (bipolar)used && !memory_compare(text, kept, used))
                return 0;
        return host_write_file(RADIO_RULES_PATH, kept, used, 0600, true);
}

/* ---- wifi: why there is nothing to join with ---- */

/*
        The one line that says why wifi cannot be used, from what the kernel
        shows anybody: sysfs for the cards, their drivers and the rfkill
        switches, /dev/kmsg for what a driver said as it gave up (readable
        without privilege here, since the image leaves DMESG_RESTRICT off),
        and the firmware directories for whether the file it asked for is
        there now.

        The two drivers the image carries fail differently without their
        firmware. rtw88 fails its probe and leaves the card with no driver;
        mt7921e stays bound and never registers a radio. Both leave "Direct
        firmware load for NAME failed" against the card's address, so the
        card is found by its PCI class, 0x0280, and the file by that line.
        A USB radio has no class of its own, so it is found only by a wifi
        driver bound to it.
*/

#define RADIO_WHY_ROOM 192
#define RADIO_LAST_PATH NET_STATE_DIR "/wifi.last"

typedef struct
{
        p16 vendor;
        p16 device;
        string_address name;
} radio_chip;

/* The names these parts are sold by. Anything else is its vendor and IDs. */
static const radio_chip radio_chips[] = {
    {0x10ec, 0xc822, (string_address) "Realtek RTL8822CE"},
    {0x10ec, 0xb822, (string_address) "Realtek RTL8822BE"},
    {0x10ec, 0xc821, (string_address) "Realtek RTL8821CE"},
    {0x10ec, 0xd723, (string_address) "Realtek RTL8723DE"},
    {0x10ec, 0x8852, (string_address) "Realtek RTL8852AE"},
    {0x10ec, 0xb852, (string_address) "Realtek RTL8852BE"},
    {0x10ec, 0xc852, (string_address) "Realtek RTL8852CE"},
    {0x14c3, 0x7961, (string_address) "MediaTek MT7921"},
    {0x14c3, 0x0608, (string_address) "MediaTek MT7921K"},
    {0x14c3, 0x0616, (string_address) "MediaTek MT7922"},
    {0x14c3, 0x7920, (string_address) "MediaTek MT7920"},
    {0x14c3, 0x7925, (string_address) "MediaTek MT7925"},
    {0x17cb, 0x1103, (string_address) "Qualcomm QCA2066"},
    {0x168c, 0x003e, (string_address) "Qualcomm Atheros QCA6174"},
    {0x8086, 0x2723, (string_address) "Intel Wi-Fi 6 AX200"},
    {0x8086, 0x2725, (string_address) "Intel Wi-Fi 6E AX210"},
    {0x8086, 0x272b, (string_address) "Intel Wi-Fi 7 BE200"},
};

static const radio_chip radio_vendors[] = {
    {0x10ec, 0, (string_address) "Realtek"},  {0x14c3, 0, (string_address) "MediaTek"},
    {0x8086, 0, (string_address) "Intel"},    {0x168c, 0, (string_address) "Qualcomm Atheros"},
    {0x17cb, 0, (string_address) "Qualcomm"}, {0x14e4, 0, (string_address) "Broadcom"},
    {0x0bda, 0, (string_address) "Realtek"},  {0x0e8d, 0, (string_address) "MediaTek"},
    {0x148f, 0, (string_address) "Ralink"},
};

/* The drivers a USB radio is known by, as the start of their names. */
static const string_address radio_usb_drivers[] = {
    (string_address) "rtw88_",    (string_address) "rtw89_",   (string_address) "rtl8xxxu",
    (string_address) "rtl8192cu", (string_address) "rtl8187",  (string_address) "mt7601u",
    (string_address) "mt76x0u",   (string_address) "mt76x2u",  (string_address) "mt7663u",
    (string_address) "mt7921u",   (string_address) "mt7925u",  (string_address) "rt2800usb",
    (string_address) "rt73usb",   (string_address) "ath9k_htc", (string_address) "carl9170",
    (string_address) "ath10k_usb", (string_address) "ath6kl_usb", (string_address) "ar5523",
    (string_address) "brcmfmac",  (string_address) "zd1211rw", (string_address) "mwifiex_usb",
};

typedef struct
{
        p8 address[48];
        p8 name[64];
        p8 driver[48];
        bool found;
} radio_card;

/* Pieces joined into a bounded line; a null ends the list early. */
static fn radio_line(p8 address_to into, positive room, string_address a,
                     string_address b, string_address c, string_address d,
                     string_address e)
{
        string_address parts[5] = {a, b, c, d, e};

        into[0] = end;
        for (positive at = 0; at < 5 && parts[at]; at++)
                string_append_bounded(into, parts[at], room);
}

static p32 radio_hex(p8 address_to text)
{
        //      The text is terminated, and a terminator is not a hexadecimal
        //      digit, so the run ends at the terminator whatever the bound
        //      is and there is no length to measure first.
        if (text[0] == '0' && (text[1] | 32) == 'x')
                text += 2;
        return (p32)string_digits_hexadecimal_max(text, (positive)-1, null);
}

static bipolar radio_sys_read(string_address directory, string_address name,
                              string_address leaf, p8 address_to into, positive room)
{
        p8 path[256];

        if (!string_join(path, sizeof(path), directory, "/", name, leaf))
        {
                into[0] = end;
                return -1;
        }
        return host_read_kernel((string_address)path, into, room);
}

/* The name of the driver bound to a device, or empty. */
static fn radio_sys_driver(string_address directory, string_address name,
                           p8 address_to into, positive room)
{
        p8 path[256];
        p8 target[256];
        bipolar got;
        positive from = 0;

        into[0] = end;
        if (!string_join(path, sizeof(path), directory, "/", name, (string_address) "/driver"))
                return;
        got = system_read_link_at(AT_FDCWD, path, target, sizeof(target) - 1);
        if (got <= 0)
                return;
        p8 address_to slash = memory_last_of(target, '/', (positive)got);

        if (slash)
                from = (positive)(slash - target) + 1;
        target[got] = end;
        string_copy_bounded(into, target + from, room);
}

static fn radio_card_name(radio_card address_to card, p32 vendor, p32 device,
                          string_address kind)
{
        p8 ids[12];
        string_address maker = null;

        for (positive at = 0; at < array_count(radio_chips); at++)
                if (radio_chips[at].vendor == vendor && radio_chips[at].device == device)
                {
                        string_copy_bounded(card->name, radio_chips[at].name,
                                            sizeof(card->name));
                        return;
                }
        for (positive at = 0; at < array_count(radio_vendors); at++)
                if (radio_vendors[at].vendor == vendor)
                        maker = radio_vendors[at].name;
        host_hex4(ids, vendor);
        ids[4] = ':';
        host_hex4(ids + 5, device);
        radio_line(card->name, sizeof(card->name), maker ? maker : (string_address) "",
                   maker ? (string_address) " " : (string_address) "", kind,
                   (string_address) " ", ids);
}

static bool radio_pci_visit(string_address directory, string_address name,
                            address_any context)
{
        radio_card address_to card = (radio_card address_to)context;
        p8 text[24];
        p32 vendor;

        if (radio_sys_read(directory, name, (string_address) "/class", text,
                           sizeof(text)) < 0 ||
            !host_starts((string_address)text, (string_address) "0x0280"))
                return true;
        radio_sys_read(directory, name, (string_address) "/vendor", text, sizeof(text));
        vendor = radio_hex(text);
        radio_sys_read(directory, name, (string_address) "/device", text, sizeof(text));
        radio_card_name(card, vendor, radio_hex(text), (string_address) "wireless");
        string_copy_bounded(card->address, name, sizeof(card->address));
        radio_sys_driver(directory, name, card->driver, sizeof(card->driver));
        card->found = true;
        return false;
}

static bool radio_usb_visit(string_address directory, string_address name,
                            address_any context)
{
        radio_card address_to card = (radio_card address_to)context;
        p8 driver[48];
        p8 parent[48];
        p8 text[16];
        p32 vendor;
        positive at = 0;
        bool wifi = false;

        if (!string_find(name, (string_address) ":"))
                return true;
        radio_sys_driver(directory, name, driver, sizeof(driver));
        for (positive which = 0; which < array_count(radio_usb_drivers); which++)
                if (host_starts((string_address)driver, radio_usb_drivers[which]))
                        wifi = true;
        if (!wifi)
                return true;

        while (name[at] && name[at] != ':' && at + 1 < sizeof(parent))
        {
                parent[at] = name[at];
                at++;
        }
        parent[at] = end;
        radio_sys_read(directory, (string_address)parent, (string_address) "/idVendor",
                       text, sizeof(text));
        vendor = radio_hex(text);
        radio_sys_read(directory, (string_address)parent, (string_address) "/idProduct",
                       text, sizeof(text));
        radio_card_name(card, vendor, radio_hex(text), (string_address) "USB wireless");
        string_copy_bounded(card->address, name, sizeof(card->address));
        string_copy_bounded(card->driver, driver, sizeof(card->driver));
        card->found = true;
        return false;
}

static bool radio_net_visit(string_address directory, string_address name,
                            address_any context)
{
        p8 path[256];

        if (string_join(path, sizeof(path), directory, "/", name, (string_address) "/phy80211") &&
            system_access_at(AT_FDCWD, path, 0) >= 0)
        {
                *(bool address_to)context = true;
                return false;
        }
        return true;
}

/* 2 when a wireless switch is hard-blocked, 1 when only soft, else 0. */
static bool radio_rfkill_visit(string_address directory, string_address name,
                               address_any context)
{
        p8 address_to state = (p8 address_to)context;
        p8 text[16];

        if (radio_sys_read(directory, name, (string_address) "/type", text,
                           sizeof(text)) < 0 ||
            !string_equals((string_address)text, (string_address) "wlan"))
                return true;
        if (radio_sys_read(directory, name, (string_address) "/hard", text,
                           sizeof(text)) >= 0 &&
            string_equals((string_address)text, (string_address) "1"))
                *state = 2;
        else if (radio_sys_read(directory, name, (string_address) "/soft", text,
                                sizeof(text)) >= 0 &&
                 string_equals((string_address)text, (string_address) "1") &&
                 *state < 1)
                *state = 1;
        return true;
}

/* 2 when a wireless switch is hard-blocked, 1 when only soft, else 0. */
static p8 radio_rfkill_blocked(void)
{
        p8 state = 0;

        host_each_entry((string_address) "/sys/class/rfkill", radio_rfkill_visit,
                        address_of state);
        return state;
}

/*
        What the kernel said about one device: the last firmware file a load
        failed for, and the error its last failed probe gave. A record is
        "PRIORITY,SEQUENCE,TIME,FLAGS;TEXT" and a device's messages start
        its text with its driver and its address.
*/
typedef struct
{
        p8 firmware[128];
        p8 probe[16];
} radio_said;

static fn radio_kernel_said(string_address address, radio_said address_to said)
{
        p8 record[2048];
        p8 load[96];
        p8 probe[96];
        bipolar handle;

        said->firmware[0] = end;
        said->probe[0] = end;
        radio_line(load, sizeof(load), address,
                   (string_address) ": Direct firmware load for ", null, null, null);
        radio_line(probe, sizeof(probe), address,
                   (string_address) ": probe with driver ", null, null, null);
        handle = system_open_at(AT_FDCWD, "/dev/kmsg", FILE_READ | O_NONBLOCK | O_CLOEXEC);
        if (handle < 0)
                return;

        for (;;)
        {
                bipolar got = system_read_once(handle, record, sizeof(record) - 1);
                string_address text;
                string_address found;
                positive at;

                // Interrupted, or a record overwritten before it was read.
                if (got == -4 || got == -32)
                        continue;
                if (got <= 0)
                        break;
                record[got] = end;
                text = string_find(record, (string_address) ";");
                if (!text)
                        continue;
                text++;
                at = (positive)(string_first_of_or_end(text, '\n') - text);
                text[at] = end;

                found = string_find(text, load);
                if (found)
                {
                        found += string_length(load);
                        for (at = 0; found[at] && found[at] != ' ' &&
                                     at + 1 < sizeof(said->firmware);
                             at++)
                                said->firmware[at] = found[at];
                        said->firmware[at] = end;
                        continue;
                }
                found = string_find(text, probe);
                if (found && (found = string_find(found, (string_address) "with error ")))
                        string_copy_bounded(said->probe, found + 11, sizeof(said->probe));
        }
        system_close(handle);
}

/* Where request_firmware looks, in its order, as the file or its .zst/.xz. */
static bool radio_firmware_present(string_address name)
{
        static const string_address tails[] = {(string_address) "", (string_address) ".zst",
                                               (string_address) ".xz"};
        p8 custom[256];
        p8 release[80];
        p8 updates[160];
        p8 versioned[160];
        p8 path[512];
        string_address roots[5];
        positive count = 0;

        host_read_kernel((string_address) "/sys/module/firmware_class/parameters/path",
                       custom, sizeof(custom));
        host_read_kernel((string_address) "/proc/sys/kernel/osrelease", release,
                       sizeof(release));
        radio_line(updates, sizeof(updates), (string_address) "/lib/firmware/updates/",
                   release, null, null, null);
        radio_line(versioned, sizeof(versioned), (string_address) "/lib/firmware/",
                   release, null, null, null);
        if (custom[0])
                roots[count++] = custom;
        roots[count++] = updates;
        roots[count++] = (string_address) "/lib/firmware/updates";
        roots[count++] = versioned;
        roots[count++] = (string_address) "/lib/firmware";

        for (positive root = 0; root < count; root++)
                for (positive tail = 0; tail < array_count(tails); tail++)
                {
                        radio_line(path, sizeof(path), roots[root], (string_address) "/",
                                   name, tails[tail], null);
                        if (system_access_at(AT_FDCWD, path, 0) >= 0)
                                return true;
                }
        return false;
}

static bool radio_has_interface(void)
{
        bool radio = false;

        host_each_entry((string_address) "/sys/class/net", radio_net_visit,
                        address_of radio);
        return radio;
}

/*
        A radio's word as the machine reads it: 1 on, 0 off. With no word kept, wifi and bluetooth are off, as a
        machine that has not asked for them has them, and moonwater wifi on or bluetooth on is what raises one. A
        machine that was using one before this default keeps it: a saved network or a remembered device leaves the
        word unsaid, -1, which every caller reads as on, as it always did.
*/
static bipolar radio_kept(string_address path)
{
        radio_network networks[1];
        p8 text[16];
        bool used;

        //      A word that says neither is no switch raised: off, as with none.
        if (host_read_word(path, text, sizeof(text)) >= 0)
                return host_onoff((string_address)text) > 0 ? 1 : 0;

        if (string_equals(path, NET_WIFI_POWER))
        {
                used = radio_wifi_load(networks, 1, null) != 0;
                crypto_forget(networks, sizeof(networks));
        }
        else
                used = host_read_state(NET_BLUETOOTH_LIST, text, 2) > 0;

        return used ? -1 : 0;
}

/*
        Why wifi cannot be used, into why, or false when the machine shows
        nothing wrong: a radio is there and nothing blocks it.
*/
static bool radio_wifi_why(p8 address_to why, positive room)
{
        radio_card card;
        radio_said said;
        p8 rfkill = 0;
        bool interface = radio_has_interface();

        why[0] = end;
        //      The hardware first: a machine with no radio is told so, and not to switch on what it does not have.
        memory_fill(address_of card, 0, sizeof(card));
        if (!interface)
        {
                host_each_entry((string_address) "/sys/bus/pci/devices", radio_pci_visit,
                                address_of card);
                if (!card.found)
                        host_each_entry((string_address) "/sys/bus/usb/devices", radio_usb_visit,
                                        address_of card);
                if (!card.found)
                {
                        radio_line(why, room, (string_address) "no wireless hardware found", null,
                                   null, null, null);
                        return true;
                }
        }

        if (radio_kept(NET_WIFI_POWER) == 0)
        {
                radio_line(why, room, (string_address) "switched off (moonwater wifi on)",
                           null, null, null, null);
                return true;
        }

        rfkill = radio_rfkill_blocked();
        if (rfkill == 2)
        {
                radio_line(why, room,
                           (string_address) "blocked by rfkill (hard): a switch or key on the machine",
                           null, null, null, null);
                return true;
        }

        if (interface)
        {
                if (rfkill == 1)
                        radio_line(why, room, (string_address) "blocked by rfkill (soft)",
                                   null, null, null, null);
                return rfkill == 1;
        }

        radio_kernel_said(card.address, address_of said);
        if (said.firmware[0] && !radio_firmware_present(said.firmware))
                radio_line(why, room, card.name, (string_address) " found, firmware ",
                           said.firmware, (string_address) " missing", null);
        else if (said.firmware[0])
                radio_line(why, room, card.name, (string_address) " found, firmware ",
                           said.firmware,
                           (string_address) " came after its driver gave up; reboot",
                           null);
        else if (!card.driver[0] && said.probe[0])
                radio_line(why, room, card.name,
                           (string_address) " found, its driver failed with error ",
                           said.probe, null, null);
        else if (!card.driver[0])
                radio_line(why, room, card.name,
                           (string_address) " found, no driver for it in this image",
                           null, null, null);
        else
                radio_line(why, room, card.name, (string_address) " found, driver ",
                           card.driver, (string_address) " gave it no interface", null);
        return true;
}

/* ---- wifi: the networks in the air ---- */

/*
        What the radio has heard, one row a network: its strongest access
        point's signal and channel, what it asks of a station, and whether
        this machine is joined to it or has it saved. The kernel keeps the
        last scan's results; a new scan is asked for only when the last one
        asked for here is older than RADIO_AIR_STALE_MS, only by root, and
        waited on for RADIO_AIR_WAIT_SECONDS at most. What the kernel holds
        cannot say how old it is as a whole: a join scans for its one name,
        which leaves a list of the few that answered looking fresh, so the
        time of the last whole scan is kept in RADIO_SCAN_PATH. A join the
        machine is making scans too; the kernel then answers EBUSY, the wait
        is for that scan's results, and those are not a whole scan.

        A name is the network's to choose and arrives over the air, so it is
        written through radio_display: printable ASCII and valid UTF-8 from
        U+00A0 up as they are, every other byte as \xNN, so no name can put
        a control sequence in front of the terminal's parser.
*/

#define NL80211_CMD_GET_SCAN 32
#define NL80211_CMD_TRIGGER_SCAN 33
#define NL80211_CMD_NEW_SCAN_RESULTS 34
#define NL80211_CMD_SCAN_ABORTED 35
#define NL80211_ATTR_BSS 47
#define NL80211_BSS_BSSID 1
#define NL80211_BSS_FREQUENCY 2
#define NL80211_BSS_CAPABILITY 5
#define NL80211_BSS_INFORMATION_ELEMENTS 6
#define NL80211_BSS_SIGNAL_MBM 7
#define NL80211_BSS_STATUS 9
#define NL80211_BSS_SEEN_MS_AGO 10
#define NL80211_BSS_BEACON_IES 11
#define NL80211_BSS_STATUS_ASSOCIATED 1
#define NL80211_ATTR_STA_INFO 21
#define NL80211_STA_INFO_STA_FLAGS 17

#define RADIO_AIR_MOST 64
#define RADIO_AIR_SHOWN 16
#define RADIO_AIR_STALE_MS 30000u
#define RADIO_AIR_WAIT_SECONDS 5
#define RADIO_SCAN_PATH NET_STATE_DIR "/wifi.scan"

enum
{
        RADIO_OPEN,
        RADIO_WEP,
        RADIO_WPA,
        RADIO_WPA2,
        RADIO_WPA23,
        RADIO_WPA3,
        RADIO_OWE,
        RADIO_EAP,
        RADIO_TKIP,
        RADIO_PMF,
        RADIO_FT,
};

static const string_address radio_security_words[] = {
    (string_address) "open", (string_address) "WEP",    (string_address) "WPA",
    (string_address) "WPA2", (string_address) "WPA2/3", (string_address) "WPA3",
    (string_address) "OWE",  (string_address) "802.1X", (string_address) "TKIP",
    (string_address) "PMF",  (string_address) "FT/EXT",
};

/* Whether moonwater can join what a network asks for: open, WPA2 (PSK or
   PSK-SHA256), WPA3 (SAE) or both, with protected management frames or
   without. */
static bool radio_security_joinable(p8 security)
{
        return security == RADIO_OPEN || security == RADIO_WPA2 || security == RADIO_WPA23 ||
               security == RADIO_WPA3 || security == RADIO_PMF;
}

typedef struct
{
        p8 ssid[RADIO_SSID_MOST + 1];
        p8 ssid_length;
        p8 security;
        //      What the access point's last beacon asked, where the kernel
        //      keeps one apart from the frame its other elements came from.
        p8 beacon_security;
        //      Whether this access point, or another by the name, asks only
        //      what the join can answer: a row is the strongest by its name,
        //      and a louder twin that asks for more is no reason to say the
        //      name cannot be joined.
        bool joinable;
        bool joined;
        b32 mbm;
        p32 frequency;
        p32 seen;
        p8 bssid[6];
        //      What its two frames offer a join, together (wifi_rsn_caps).
        wifi_rsn_caps caps;
} radio_heard;

typedef struct
{
        radio_heard heard[RADIO_AIR_MOST];
        positive count;
        p32 freshest;
        bool any;
} radio_air;

/* A cipher suite as wifi_rsn_caps keeps it. */
static p8 radio_cipher_bit(p32 suite)
{
        return suite == 0x000fac04u   ? WIFI_CIPHER_CCMP
               : suite == 0x000fac02u ? WIFI_CIPHER_TKIP
                                      : WIFI_CIPHER_OTHER;
}

/*
        What an RSN element asks of a station, as a class for a scan's row,
        and into caps what it offers a join. The join answers PSK (00-0F-AC:2),
        PSK-SHA256 (6) and SAE (8), with CCMP as the group cipher and among
        the pairwise ones, with protected management frames or without. A
        network that asks for something else is a class of its own, with the
        thing it asks for as its name: TKIP or any other cipher but CCMP, and
        FT or the extended-key SAE suites with nothing the join answers
        beside them. An element cut short, or one that names no key
        management the join knows, is WPA2 with what it did say: what is
        not there is what the join always asked, CCMP and PSK.
*/
static p8 radio_rsn_security(p8 address_to element, positive length,
                             wifi_rsn_caps address_to caps)
{
        byte_reader reader = byte_reader_open(element, length);
        byte_reader pairwise;
        positive count;
        positive which;
        p32 group;
        p16 capabilities = 0;
        bool psk = false, sae = false, ft = false, eap = false, owe = false;

        memory_fill(caps, 0, sizeof(*caps));
        caps->group = WIFI_CIPHER_CCMP;
        caps->pairwise = WIFI_CIPHER_CCMP;
        caps->akms = WIFI_AKM_PSK;
        //      The version and the group cipher, then the pairwise ciphers
        //      and the key management suites, each a count and that many
        //      four byte suites, then the capabilities.
        (void)byte_reader_skip(&reader, 2);
        group = (p32)byte_reader_uint(&reader, 4, true);
        count = (p16)byte_reader_uint(&reader, 2, false);
        pairwise = byte_reader_window(&reader, 4 * count);
        count = (p16)byte_reader_uint(&reader, 2, false);
        if (!byte_reader_ok(&reader))
                return RADIO_WPA2;
        caps->group = radio_cipher_bit(group);
        caps->pairwise = 0;
        caps->akms = 0;
        while (byte_reader_left(&pairwise) >= 4)
                caps->pairwise |= radio_cipher_bit((p32)byte_reader_uint(&pairwise, 4, true));
        for (which = 0; which < count && byte_reader_left(&reader) >= 4; which++)
        {
                p32 oui = byte_reader_u24(&reader);
                p8 suite = byte_reader_u8(&reader);

                if (oui != 0x000fac)
                        caps->akms |= WIFI_AKM_OTHER;
                else if (suite == 2 || suite == 6)
                {
                        psk = true;
                        caps->akms |= suite == 2 ? WIFI_AKM_PSK : WIFI_AKM_PSK_SHA256;
                }
                else if (suite == 8)
                {
                        sae = true;
                        caps->akms |= WIFI_AKM_SAE;
                }
                else
                {
                        ft |= suite == 4 || suite == 9 || suite == 24 || suite == 25;
                        owe |= suite == 18;
                        eap |= suite != 4 && suite != 9 && suite != 24 && suite != 25 &&
                               suite != 18;
                        caps->akms |= WIFI_AKM_OTHER;
                }
        }
        if (which == count && byte_reader_left(&reader) >= 2)
                capabilities = (p16)byte_reader_uint(&reader, 2, false);
        caps->pmf = (p8)((capabilities & 0x0080 ? WIFI_PMF_CAPABLE : 0) |
                         (capabilities & 0x0040 ? WIFI_PMF_REQUIRED : 0));

        if (!psk && !sae && (ft || owe || eap))
                return ft ? RADIO_FT : owe ? RADIO_OWE : RADIO_EAP;
        if (!psk && !sae)
        {
                caps->akms |= WIFI_AKM_PSK;
                psk = true;
        }
        if (caps->group != WIFI_CIPHER_CCMP || !(caps->pairwise & WIFI_CIPHER_CCMP))
                return RADIO_TKIP;
        if (!psk)
                return RADIO_WPA3;
        if (caps->pmf & WIFI_PMF_REQUIRED)
                return RADIO_PMF;
        return sae ? RADIO_WPA23 : RADIO_WPA2;
}

/* What the elements of one frame ask of a station: the first RSN element's
   class, else a WPA vendor element, else what the capability's privacy bit
   says; and into caps what the first RSN element offers, with whether the
   first RSN extension element takes SAE's hash-to-element (bit 5 of its
   first byte). */
static p8 radio_ies_security(p8 address_to elements, positive size, p16 capability,
                             wifi_rsn_caps address_to caps)
{
        byte_reader ies = byte_reader_open(elements, elements ? size : 0);
        bool wpa = false, rsn = false, extension = false;
        p8 security = RADIO_OPEN;
        p8 h2e = 0;

        memory_fill(caps, 0, sizeof(*caps));
        while (byte_reader_left(&ies))
        {
                p8 id = byte_reader_u8(&ies);
                byte_reader data = byte_reader_vector8(&ies);
                positive span = byte_reader_left(&data);

                if (!byte_reader_ok(&ies))
                        break;
                if (id == 48 && !rsn)
                {
                        rsn = true;
                        security = radio_rsn_security((p8 address_to)byte_reader_here(&data),
                                                      span, caps);
                }
                else if (id == 244 && !extension)
                {
                        extension = true;
                        h2e = span && (byte_reader_u8(&data) & 0x20) ? 1 : 0;
                }
                else if (id == 221 && span >= 4 && (p32)byte_reader_uint(&data, 4, true) == 0x0050f201)
                        wpa = true;
        }
        if (rsn)
        {
                caps->h2e = h2e;
                return security;
        }
        return wpa ? RADIO_WPA : (capability & 0x10) ? RADIO_WEP : RADIO_OPEN;
}

/* The first element of id in a frame's elements, whole, into out: its
   length, 0 for none or one cut short. */
static positive radio_ies_element(p8 address_to elements, positive size, p8 id,
                                  p8 address_to out)
{
        byte_reader ies = byte_reader_open(elements, elements ? size : 0);

        while (byte_reader_left(&ies))
        {
                byte_reader whole = ies;
                p8 kind = byte_reader_u8(&ies);
                byte_reader data = byte_reader_vector8(&ies);
                positive span = byte_reader_left(&data);

                if (!byte_reader_ok(&ies))
                        break;
                if (kind == id)
                {
                        memory_copy(out, byte_reader_take(&whole, 2 + span), 2 + span);
                        return 2 + span;
                }
        }
        return 0;
}

static p32 radio_bss_u32(p8 address_to bss, positive length, p16 type, p32 missing)
{
        positive size = 0;
        p8 address_to value = (p8 address_to)netlink_find_span(bss, length, type, address_of size);

        return value && size >= 4 ? memory_load_unaligned(p32, value) : missing;
}

/* One access point of a scan dump: false for a message that is not one. seen,
   when asked for, is the access point's RSN element and RSN extension element
   as each of its two frames showed them (wifi_seen), from the same message. */
static bool radio_bss_read(netlink_header address_to header, radio_heard address_to one,
                           wifi_seen address_to seen)
{
        positive length = 0;
        positive size = 0;
        p8 address_to bss;
        p8 address_to elements;
        p8 address_to value;
        byte_reader ies;
        p8 address_to beacon;
        positive beacon_size = 0;
        bool named = false;
        p16 capability = 0;

        if (nl80211_command(header) != NL80211_CMD_NEW_SCAN_RESULTS)
                return false;
        bss = (p8 address_to)netlink_find(header, GENL_HEADER, NL80211_ATTR_BSS,
                                          address_of length);
        if (!bss)
                return false;

        memory_fill(one, 0, sizeof(*one));
        one->security = RADIO_OPEN;
        value = (p8 address_to)netlink_find_span(bss, length, NL80211_BSS_BSSID,
                                                 address_of size);
        if (value && size >= 6)
                memory_copy(one->bssid, value, 6);
        one->mbm = (b32)radio_bss_u32(bss, length, NL80211_BSS_SIGNAL_MBM, (p32)-10000);
        one->frequency = radio_bss_u32(bss, length, NL80211_BSS_FREQUENCY, 0);
        one->joined = radio_bss_u32(bss, length, NL80211_BSS_STATUS, 0) == NL80211_BSS_STATUS_ASSOCIATED;
        value = (p8 address_to)netlink_find_span(bss, length, NL80211_BSS_CAPABILITY,
                                                 address_of size);
        if (value && size >= 2)
                capability = memory_load_unaligned(p16, value);
        one->seen = radio_bss_u32(bss, length, NL80211_BSS_SEEN_MS_AGO, ~(p32)0);

        elements = (p8 address_to)netlink_find_span(bss, length,
                                                    NL80211_BSS_INFORMATION_ELEMENTS,
                                                    address_of size);
        beacon = (p8 address_to)netlink_find_span(bss, length, NL80211_BSS_BEACON_IES,
                                                  address_of beacon_size);
        if (!elements)
        {
                elements = beacon;
                size = beacon_size;
        }
        ies = byte_reader_open(elements, elements ? size : 0);
        while (byte_reader_left(&ies))
        {
                p8 id = byte_reader_u8(&ies);
                byte_reader data = byte_reader_vector8(&ies);
                positive span = byte_reader_left(&data);

                if (!byte_reader_ok(&ies))
                        break;
                //      The first name element is the access point's name, the one
                //      the kernel joins by: a later one is a beacon built to be
                //      read differently, and an empty or over-long first leaves
                //      it nameless.
                if (id == 0 && !named)
                {
                        named = true;
                        if (span <= RADIO_SSID_MOST)
                        {
                                memory_copy(one->ssid, byte_reader_here(&data), span);
                                one->ssid_length = (p8)span;
                        }
                }
        }
        one->security = radio_ies_security(elements, elements ? size : 0, capability,
                                           address_of one->caps);
        one->beacon_security = one->security;
        if (beacon && beacon != elements)
        {
                wifi_rsn_caps beaconed;

                one->beacon_security = radio_ies_security(beacon, beacon_size, capability,
                                                          address_of beaconed);
                wifi_caps_merge(address_of one->caps, address_of beaconed);
        }
        one->joinable = radio_security_joinable(one->security) ||
                        radio_security_joinable(one->beacon_security);
        if (seen)
        {
                memory_fill(seen, 0, sizeof(*seen));
                seen->rsn_length[0] = (p16)radio_ies_element(elements, size, 48, seen->rsn[0]);
                seen->rsnx_length[0] = (p16)radio_ies_element(elements, size, 244, seen->rsnx[0]);
                if (beacon && beacon != elements)
                {
                        seen->rsn_length[1] = (p16)radio_ies_element(beacon, beacon_size, 48,
                                                                     seen->rsn[1]);
                        seen->rsnx_length[1] = (p16)radio_ies_element(beacon, beacon_size, 244,
                                                                      seen->rsnx[1]);
                }
        }
        return true;
}

/* Keep one row per name and, when the bounded display is full, the strongest
   rows rather than whichever names the kernel happened to dump first.  Scan
   dump order is not signal order: sixty-four weak forged names used to hide a
   stronger real network merely by arriving before it.  The associated row is
   never evicted, even when its current signal is the weakest. */
static fn radio_air_keep(radio_air address_to air,
                         const radio_heard address_to one)
{
        // A hidden network's name is empty or zeros; it has no row.
        if (memory_span_byte(one->ssid, 0, one->ssid_length) == one->ssid_length)
                return;

        for (positive at = 0; at < air->count; at++)
        {
                radio_heard address_to have = air->heard + at;

                if (have->ssid_length != one->ssid_length ||
                    memory_compare(have->ssid, one->ssid, one->ssid_length))
                        continue;
                /* `joined` belongs to one BSSID, not to the SSID aggregate.
                   Never combine an associated row's bit with a stronger
                   twin's security, channel and address: that presents the
                   twin as the network carrying the live association.  An
                   associated BSS owns the row; otherwise the strongest BSS
                   owns the whole row, not three selected fields from it. */
                bool joinable = have->joinable || one->joinable;

                if ((one->joined && !have->joined) ||
                    (one->joined == have->joined && one->mbm > have->mbm))
                        *have = *one;
                have->joinable = joinable;
                return;
        }
        if (air->count < RADIO_AIR_MOST)
                air->heard[air->count++] = *one;
        else
        {
                positive weakest = positive_max;

                for (positive at = 0; at < air->count; at++)
                        if (!air->heard[at].joined &&
                            (weakest == positive_max ||
                             air->heard[at].mbm < air->heard[weakest].mbm))
                                weakest = at;
                if (weakest != positive_max &&
                    (one->joined || one->mbm > air->heard[weakest].mbm))
                        air->heard[weakest] = *one;
        }
}

static bool radio_air_seen(netlink_header address_to header, address_any context)
{
        radio_air address_to air = (radio_air address_to)context;
        radio_heard one;

        if (!radio_bss_read(header, address_of one, null))
                return true;
        /* How old the scan is, from what is not joined: the network the
           machine is on stays in the kernel's list, renewed by every
           beacon, while the rest expire after thirty seconds, so a list of
           that one alone is a scan long gone. */
        if (one.seen != ~(p32)0 && !one.joined && (!air->any || one.seen < air->freshest))
        {
                air->freshest = one.seen;
                air->any = true;
        }

        radio_air_keep(air, address_of one);
        return true;
}

/* Whether the station the machine is associated through is authorized: a
   join whose password is wrong is associated for the eight seconds its
   handshake waits, and is not joined for any of them. */
static bool radio_authorized_seen(netlink_header address_to header, address_any context)
{
        positive length = 0;
        positive size = 0;
        p8 address_to info = (p8 address_to)netlink_find(header, GENL_HEADER,
                                                         NL80211_ATTR_STA_INFO,
                                                         address_of length);
        p8 address_to flags = info ? (p8 address_to)netlink_find_span(
                                          info, length, NL80211_STA_INFO_STA_FLAGS,
                                          address_of size)
                                   : null;

        if (flags && size >= 8 &&
            (memory_load_unaligned(p32, flags + 4) & (1u << NL80211_STA_FLAG_AUTHORIZED)))
                *(bool address_to)context = true;
        return true;
}

static bool radio_authorized(nl80211 address_to session, p32 index)
{
        netlink_buffer request = {0};
        p32 sequence = netlink_sequence_take();
        bool authorized = false;

        if (!nl80211_begin(address_of request, session->family, NL80211_CMD_GET_STATION,
                           NLM_REQUEST | NLM_DUMP, sequence))
                return false;
        nl80211_attribute_u32(address_of request, NL80211_ATTR_IFINDEX, index);
        netlink_transact(session->handle, address_of request, sequence,
                         radio_authorized_seen, address_of authorized);
        return authorized;
}

/*
        The whole scans asked for here, a line each: the boot-time milliseconds
        it came in at, a space, and the name it asked for, which is none for a
        scan of the air alone. A join asks for its own name, so what a scan
        found of a network that does not answer to the wildcard (a hidden one)
        is known only of the name that was asked; and what the last scan of
        any name came to is what a status goes by. A line is kept for as long
        as it says anything, which is the time a scan is not asked for again.
*/
#define RADIO_SCAN_KEEP 16
#define RADIO_SCAN_ROOM (RADIO_SCAN_KEEP * (24 + RADIO_SSID_MOST))

typedef struct
{
        p64 when;
        string_address name;
        positive name_length;
        string_address next;
} radio_scan_line;

/* The line at the cursor, which moves past it: false at the end. */
static bool radio_scan_line_next(string_address address_to cursor, radio_scan_line address_to line)
{
        string_address at = address_to cursor;
        string_address end_of;

        while (*at == '\n')
                at++;
        if (!*at)
                return false;
        line->when = 0;
        for (; byte_is_digit((p8)*at); at++)
                line->when = line->when * 10 + (p64)(*at - '0');
        if (*at == ' ')
                at++;
        end_of = string_first_of_or_end(at, '\n');
        line->name = at;
        line->name_length = (positive)(end_of - at);
        address_to cursor = end_of;
        return true;
}

/* Milliseconds since the last whole scan asked for here, or the most there is.
   With a name, only a scan that asked for that name counts: one for another
   name, or for none, did not ask a hidden network to answer. */
static p64 radio_scan_age(p8 address_to ssid, positive ssid_length)
{
        p8 text[RADIO_SCAN_ROOM];
        p64 now = system_clock_ns(HOST_CLOCK_BOOTTIME) / 1000000;
        p64 best = ~(p64)0;
        radio_scan_line line;
        string_address cursor = (string_address)text;

        if (radio_station >= 0)
                return (p64)radio_station_number(RADIO_ASK_SCAN_AGE, ssid, ssid_length, null, 0,
                                                 0);
        if (host_read_word(RADIO_SCAN_PATH, text, sizeof(text)) <= 0)
                return best;
        while (radio_scan_line_next(address_of cursor, address_of line))
                if ((!ssid || (line.name_length == ssid_length &&
                               !memory_compare(line.name, ssid, ssid_length))) &&
                    line.when <= now && now - line.when < best)
                        best = now - line.when;
        return best;
}

/* A whole scan that asked for ssid, now: its line first, and the others as
   long as they are within the time they stand for. */
static fn radio_scan_note(p8 address_to ssid, positive ssid_length)
{
        p8 old[RADIO_SCAN_ROOM];
        p8 text[RADIO_SCAN_ROOM];
        p8 number[24];
        p64 now = system_clock_ns(HOST_CLOCK_BOOTTIME) / 1000000;
        radio_scan_line line;
        string_address cursor = (string_address)old;
        positive lines = 1;
        positive used;

        if (radio_station >= 0)
        {
                (void)radio_station_number(RADIO_ASK_SCAN_NOTE, ssid, ssid_length, null, 0, 0);
                return;
        }

        bipolar_into_string(number, (bipolar)now);
        string_copy_bounded(text, number, sizeof(text));
        string_append_bounded(text, " ", sizeof(text));
        used = string_length(text);
        memory_copy(text + used, ssid, ssid_length);
        text[used + ssid_length] = '\n';
        used += ssid_length + 1;
        text[used] = end;

        if (host_read_word(RADIO_SCAN_PATH, old, sizeof(old)) > 0)
                while (lines < RADIO_SCAN_KEEP && radio_scan_line_next(address_of cursor, address_of line))
                        if (line.when <= now && now - line.when <= RADIO_AIR_STALE_MS &&
                            !(line.name_length == ssid_length &&
                              !memory_compare(line.name, ssid, ssid_length)) &&
                            used + 24 + line.name_length + 1 < sizeof(text))
                        {
                                bipolar_into_string(number, (bipolar)line.when);
                                string_append_bounded(text, number, sizeof(text));
                                string_append_bounded(text, " ", sizeof(text));
                                used = string_length(text);
                                memory_copy(text + used, line.name, line.name_length);
                                text[used + line.name_length] = '\n';
                                used += line.name_length + 1;
                                text[used] = end;
                                lines++;
                        }
        host_state_ready();
        (void)host_write_file(RADIO_SCAN_PATH, text, used, 0644, false);
}

/* The kernel's list of what the air holds, each access point to visit. */
static fn radio_air_dump(nl80211 address_to session, p32 index, netlink_visitor visit,
                         address_any context)
{
        netlink_buffer request = {0};
        p32 sequence = netlink_sequence_take();

        if (!nl80211_begin(address_of request, session->family, NL80211_CMD_GET_SCAN,
                           NLM_REQUEST | NLM_DUMP, sequence))
                return;
        nl80211_attribute_u32(address_of request, NL80211_ATTR_IFINDEX, index);
        netlink_transact(session->handle, address_of request, sequence, visit, context);
}

/*
        A scan asked for and waited on: true when its results came, false
        for an abort, a refusal or the time up. Active, with the wildcard
        name and the one being joined when there is one -- a scan with no
        names at all is a passive one, a tenth of a second or more on every
        channel -- and flushed, so the list after it holds what answered
        and nothing the cache kept from before. For a join, only the 2.4
        and 5 GHz channels: those at 6 GHz are all listened to rather than
        asked, which on a radio with that band made the whole scan outlast
        the five seconds it is given, so a network at 6 GHz alone (WPA3 with
        hash-to-element, which the join has) is not one a join finds. A
        radio refuses a list with a channel it has none of, at once, so the
        join asks for the widest list first
        (channel 14, 144 and 169 to 177 as well, where a regulatory domain
        has them), then for the list most radios take, then for the whole
        scan.
*/
#define RADIO_SCAN_WHOLE 0
#define RADIO_SCAN_JOIN 1
#define RADIO_SCAN_JOIN_WIDE 2

//      First, last and step in MHz, and 1 for the channels only the widest
//      list holds.
static const p16 radio_join_channels[][4] = {
    {2412, 2472, 5, 0},  {5180, 5320, 20, 0}, {5500, 5700, 20, 0}, {5745, 5825, 20, 0},
    {2484, 2484, 5, 1},  {5720, 5720, 20, 1}, {5845, 5885, 20, 1}};

typedef struct
{
        nl80211 address_to session;
        p8 address_to ssid;
        positive ssid_length;
        p32 index;
        p32 sequence;
        bool acked;
        bool whole;
        bool ended;
        bool refused;
        bipolar status;
} radio_scan_turn;

/* One message of a scan's wait: the acknowledgment of the trigger and then
   the results or the abort. */
static bool radio_scan_take(netlink_header address_to header, address_any context)
{
        radio_scan_turn address_to turn = (radio_scan_turn address_to)context;
        p8 command;

        // Refused for any reason but a scan already running: no results are
        // coming, so the cached ones stand.
        if (header->type == NLMSG_IS_ERROR && header->sequence == turn->sequence &&
            !turn->acked)
        {
                turn->status = netlink_status(header, false);
                turn->acked = true;
                turn->whole = turn->status >= 0;
                turn->refused = turn->status < 0 && turn->status != -16;
                turn->ended = turn->refused;
                return !turn->ended;
        }

        command = nl80211_news(turn->session, header, turn->index);
        if (command != NL80211_CMD_NEW_SCAN_RESULTS && command != NL80211_CMD_SCAN_ABORTED)
                return true;

        //      When, and for which name: the next join asks the same of its own.
        if (turn->whole && command == NL80211_CMD_NEW_SCAN_RESULTS)
                radio_scan_note(turn->ssid, turn->ssid_length);
        turn->whole = command == NL80211_CMD_NEW_SCAN_RESULTS;
        turn->ended = true;
        return false;
}

static bool radio_air_scan(nl80211 address_to session, p32 index, p8 address_to ssid,
                           positive ssid_length, positive tier)
{
        netlink_buffer request = {0};
        netlink_buffer reply = {0};
        network_deadline deadline;
        p32 sequence = netlink_sequence_take();
        radio_scan_turn turn = {.session = session, .index = index, .sequence = sequence};
        positive nest;

        if (ssid_length > RADIO_SSID_MOST)
                ssid_length = 0;
        turn.ssid = ssid;
        turn.ssid_length = ssid_length;
        if (!session->scan ||
            socket_option_set(session->handle, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP,
                              address_of session->scan, sizeof(session->scan)) < 0)
                return false;
        scope_exit(netlink_forget(address_of request); netlink_forget(address_of reply));
        if (!nl80211_begin(address_of request, session->family, NL80211_CMD_TRIGGER_SCAN,
                           NLM_REQUEST | NLM_ACK, sequence))
                return false;
        nl80211_attribute_u32(address_of request, NL80211_ATTR_IFINDEX, index);
        nest = netlink_nested_begin(address_of request, NL80211_ATTR_SCAN_SSIDS);
        netlink_attribute_add(address_of request, 1, null, 0);
        if (ssid_length)
                netlink_attribute_add(address_of request, 2, ssid, ssid_length);
        netlink_nested_end(address_of request, nest);
        if (tier)
        {
                p16 channel = 0;

                nest = netlink_nested_begin(address_of request,
                                            NL80211_ATTR_SCAN_FREQUENCIES);
                for (positive band = 0;
                     band < sizeof(radio_join_channels) / sizeof(radio_join_channels[0]);
                     band++)
                        for (p32 mhz = radio_join_channels[band][0];
                             (!radio_join_channels[band][3] || tier == RADIO_SCAN_JOIN_WIDE) &&
                             mhz <= radio_join_channels[band][1];
                             mhz += radio_join_channels[band][2])
                                netlink_attribute_add(address_of request, ++channel,
                                                      address_of mhz, 4);
                netlink_nested_end(address_of request, nest);
        }
        nl80211_attribute_u32(address_of request, NL80211_ATTR_SCAN_FLAGS,
                              NL80211_SCAN_FLAG_FLUSH);
        if (request.failed ||
            socket_send(session->handle, request.bytes, request.used, 0, 0, 0) < 0 ||
            !network_deadline_begin(address_of deadline, RADIO_AIR_WAIT_SECONDS, 0))
                return false;

        while (network_wait_readable_until(session->handle, address_of deadline) > 0)
        {
                p32 local_port = 0;
                bipolar got = netlink_receive(session->handle, address_of reply,
                                              address_of local_port);

                if (got == NETWORK_INTERRUPTED || got == NETWORK_TRY_AGAIN)
                        continue;
                if (got < 0)
                        break;
                netlink_each(address_of reply, radio_scan_take, address_of turn);
                if (turn.ended)
                {
                        return turn.refused ? tier && turn.status == -22 &&
                                                  radio_air_scan(session, index, ssid,
                                                                 ssid_length, tier - 1)
                                            : turn.whole;
                }
        }
        return false;
}

/*
        The networks in the air, strongest first. fresh is RADIO_AIR_CACHED
        for what was heard last and nothing more, RADIO_AIR_STALE for a new
        scan when that is stale, RADIO_AIR_NOW for one whatever it is; a
        scan needs root and a link that is up. False when there is no
        wireless interface to ask.
*/
#define RADIO_AIR_CACHED 0
#define RADIO_AIR_STALE 1
#define RADIO_AIR_NOW 2
#define RADIO_AIR_JOINABLE 4

static bool radio_air_take(radio_air address_to air, p8 fresh)
{
        nl80211 session;
        nl80211_iface iface;
        bool joinable = (fresh & RADIO_AIR_JOINABLE) != 0;

        fresh &= RADIO_AIR_NOW | RADIO_AIR_STALE;

        memory_fill(air, 0, sizeof(*air));
        if (nl80211_open(address_of session) < 0)
                return false;
        scope_exit(nl80211_close(address_of session));
        if (nl80211_interface(address_of session, address_of iface) < 0)
                return false;
        radio_air_dump(address_of session, iface.index, radio_air_seen, air);
        if (fresh && bowl_is_root() &&
            (fresh == RADIO_AIR_NOW || !air->any || air->freshest > RADIO_AIR_STALE_MS ||
             radio_scan_age(null, 0) > RADIO_AIR_STALE_MS))
        {
                bipolar route = netlink_open_groups(0);

                if (route >= 0)
                {
                        netlink_link_up((b32)route, iface.index);
                        socket_close((b32)route);
                }
                radio_air_scan(address_of session, iface.index, null, 0,
                               joinable ? RADIO_SCAN_JOIN_WIDE : RADIO_SCAN_WHOLE);
                memory_fill(air, 0, sizeof(*air));
                radio_air_dump(address_of session, iface.index, radio_air_seen, air);
        }
        {
                bool joined = false;

                for (positive at = 0; at < air->count; at++)
                        joined |= air->heard[at].joined;
                if (joined && !radio_authorized(address_of session, iface.index))
                        for (positive at = 0; at < air->count; at++)
                                air->heard[at].joined = false;
        }

        for (positive at = 1; at < air->count; at++)
                for (positive back = at; back > 0 && air->heard[back].mbm >
                                                         air->heard[back - 1].mbm;
                     back--)
                {
                        radio_heard swap = air->heard[back];

                        air->heard[back] = air->heard[back - 1];
                        air->heard[back - 1] = swap;
                }
        return true;
}

/*
        The access points given up on in the last hour: sent the machine
        away, stopped answering, or failed a join. The next join tries any
        other that answers to the same name before one of these, and of
        these the one given up on longest ago first. Anybody in range can
        beacon twins of a saved network that ask for WPA2 and fail every
        join, and louder than the real one: with four remembered for a
        minute and the loudest first among those not remembered, five that
        failed quickly or four that failed slowly took turns falling off the
        list and being tried again, and the real one never was. Sixty-four
        kept for an hour hold sixty-three twins that each take the longest a
        join can, so each is tried once before the real one is, and again
        only after it; more twins than that are the limit of any list this
        size.
*/
#define RADIO_AVOID_PATH NET_STATE_DIR "/wifi.avoid"
#define RADIO_AVOID_MOST 64
#define RADIO_AVOID_MS 3600000u

typedef struct
{
        p8 bssid[6];
        p8 unused[2];
        p64 until;
} radio_avoid;

_Static_assert(RADIO_AVOID_MOST * sizeof(radio_avoid) == RADIO_ASK_AVOID_ROOM,
               "the broker's answer holds the whole avoid list");

static positive radio_avoid_load(radio_avoid address_to into)
{
        //      One byte more than the list can be, since a state read keeps
        //      one for its end: a full list is read whole.
        p8 raw[RADIO_AVOID_MOST * sizeof(radio_avoid) + 1];
        bipolar got;
        positive count;
        positive kept = 0;
        p64 now = system_clock_ns(HOST_CLOCK_BOOTTIME) / 1000000;

        if (radio_station >= 0)
        {
                radio_ask ask;
                radio_answer answer;

                memory_fill(address_of ask, 0, sizeof(ask));
                ask.what = RADIO_ASK_AVOID_LOAD;
                radio_station_ask(address_of ask, address_of answer);
                if (answer.count > RADIO_AVOID_MOST)
                        return 0;
                memory_copy(into, answer.avoid, (positive)answer.count * sizeof(radio_avoid));
                return (positive)answer.count;
        }
        //      A state file read like the others: a regular file, no link, and
        //      a FIFO at the name answers at once instead of holding the join.
        got = host_read_state(RADIO_AVOID_PATH, raw, sizeof(raw));
        count = got > 0 ? (positive)got / sizeof(radio_avoid) : 0;
        memory_copy(into, raw, count * sizeof(radio_avoid));

        for (positive at = 0; at < count; at++)
                if (into[at].until > now && into[at].until - now <= RADIO_AVOID_MS)
                        into[kept++] = into[at];
        return kept;
}

static COLD fn radio_avoid_add(p8 address_to bssid)
{
        radio_avoid list[RADIO_AVOID_MOST + 1];
        positive count;
        positive kept = 1;

        if (!bssid || !wifi_mac_set(bssid))
                return;
        if (radio_station >= 0)
        {
                (void)radio_station_number(RADIO_ASK_AVOID_ADD, null, 0, bssid, 0, 0);
                return;
        }
        count = radio_avoid_load(list + 1);
        memory_fill(list, 0, sizeof(list[0]));
        memory_copy(list[0].bssid, bssid, 6);
        list[0].until = system_clock_ns(HOST_CLOCK_BOOTTIME) / 1000000 + RADIO_AVOID_MS;
        for (positive at = 1; at <= count && kept < RADIO_AVOID_MOST; at++)
                if (memory_compare(list[at].bssid, bssid, 6))
                        list[kept++] = list[at];
        host_state_ready();
        (void)host_write_file(RADIO_AVOID_PATH, (p8 address_to)list, kept * sizeof(radio_avoid),
                        0600, false);
}

typedef struct
{
        p8 address_to ssid;
        positive ssid_length;
        radio_avoid address_to avoid;
        positive avoid_count;
        radio_heard best;
        //      When the best was given up on until, 0 for never.
        p64 until;
        bool found;
        bool secured;
        bool fits;
        //      What the saved network has taught the join, whether its secret
        //      is a key of 64 hex digits, and the best's elements as seen.
        p8 rules;
        bool key_only;
        wifi_seen seen;
} radio_pick;

/* Whether what an access point's frame asks of a station is what a saved
   network wants: WPA2 or WPA3 for one with a password, nothing for one
   without. */
static bool radio_security_fits(bool secured, p8 security)
{
        return secured ? security == RADIO_WPA2 || security == RADIO_WPA23 ||
                             security == RADIO_WPA3 || security == RADIO_PMF
                       : security == RADIO_OPEN;
}

/* The strongest the kernel still lists by the name, one given up on only
   if no other is, and the one given up on longest ago before the rest. */
static bool radio_pick_seen(netlink_header address_to header, address_any context)
{
        radio_pick address_to pick = (radio_pick address_to)context;
        radio_heard one;
        wifi_seen seen;
        p64 until = 0;
        bool fits;

        if (!radio_bss_read(header, address_of one, address_of seen) ||
            one.seen > RADIO_AIR_STALE_MS ||
            !wifi_mac_set(one.bssid) || one.ssid_length != pick->ssid_length ||
            memory_compare(one.ssid, pick->ssid, pick->ssid_length))
                return true;
        for (positive at = 0; at < pick->avoid_count; at++)
                if (!memory_compare(pick->avoid[at].bssid, one.bssid, 6))
                        until = pick->avoid[at].until;
        //      What the saved network asks of an access point: WPA2 for one
        //      with a password, nothing for one without. A twin by the same
        //      name that offers anything else, and beacons louder than the
        //      real one, is the last resort and not the first: the kernel
        //      will not join it, and asking cost a join its whole timeout.
        //      The frame the kernel keeps the elements of is the latest, a
        //      probe response as well as a beacon, and any station can send
        //      one in the real access point's name without an RSN element:
        //      what its last beacon asked counts as well, so one forged
        //      response does not push the real one out behind a twin. And
        //      of a secured one, what the join's plan would refuse: a twin
        //      that offers less than the network taught it (protection,
        //      WPA3) is exactly what a downgrade needs, and goes last.
        fits = radio_security_fits(pick->secured, one.security) ||
               radio_security_fits(pick->secured, one.beacon_security);
        if (pick->secured && fits)
        {
                wifi_plan plan;

                fits = wifi_join_plan(MOONWATER_STRICT_NET, address_of one.caps, pick->rules,
                                      pick->key_only, address_of plan) == 0;
        }
        if (pick->found && (fits < pick->fits ||
                            (fits == pick->fits &&
                             (until > pick->until ||
                              (until == pick->until && one.mbm <= pick->best.mbm)))))
                return true;
        pick->best = one;
        pick->found = true;
        pick->until = until;
        pick->fits = fits;
        pick->seen = seen;
        return true;
}

/* Whether the last choice of an access point answered "none" from the scan of
   a moment ago and not from one of its own. */
static bool radio_scan_declined;

/*
        The access point a join asks for by its BSSID. A join by name alone
        took whatever the kernel's cache named, and the cache keeps an
        access point for thirty seconds after it last heard it: after a
        disconnect that was the one that had just stopped answering, and
        authentication with it timed out, again and again. So the cache is
        trusted for anything but an access point given up on, and when it
        has nothing else by the name it is flushed and the air scanned for
        the name -- by a join that was asked for, always, and by the
        machine's own pass (force false) only when the cache has an access
        point to doubt or no scan asked for this name came in the last
        thirty seconds, as radio_air_take holds a status to: a saved network
        out of range was scanned for, five seconds of it, again for every
        network and every pass. 1 with one
        chosen, 0 when the scan, or the one a moment ago, heard none by this
        name, -1 when no scan could be had: the join then goes by name. With
        one chosen, caps is what it offers and seen what it showed.
*/
static COLD bipolar radio_bss_choose(nl80211 address_to session, p32 index,
                                     p8 address_to ssid, positive ssid_length, bool secured,
                                     p8 address_to bssid, p32 address_to frequency,
                                     bool force, p8 rules, bool key_only,
                                     wifi_rsn_caps address_to caps, wifi_seen address_to seen)
{
        radio_scan_declined = false;
        radio_avoid avoid[RADIO_AVOID_MOST];
        radio_pick pick = {.ssid = ssid,
                           .ssid_length = ssid_length,
                           .avoid = avoid,
                           .secured = secured,
                           .rules = rules,
                           .key_only = key_only};
        bipolar heard = 1;
        bool scanned = false;

        pick.avoid_count = radio_avoid_load(avoid);
        radio_air_dump(session, index, radio_pick_seen, address_of pick);
        if ((!pick.found || pick.until || !pick.fits) &&
            (force || pick.found || radio_scan_age(ssid, ssid_length) > RADIO_AIR_STALE_MS))
        {
                scanned = true;
                heard = radio_air_scan(session, index, ssid, ssid_length,
                                       RADIO_SCAN_JOIN_WIDE)
                            ? 1
                            : -1;
                pick.found = false;
                radio_air_dump(session, index, radio_pick_seen, address_of pick);
        }
        //      Nothing by the name and no scan asked for: not in range as of
        //      the scan a moment ago, which is no new failure to wait longer for.
        radio_scan_declined = !pick.found && !scanned;
        radio_pick_given_up = pick.found && pick.until != 0;
        if (!pick.found)
                return heard > 0 ? 0 : -1;
        memory_copy(bssid, pick.best.bssid, 6);
        address_to frequency = pick.best.frequency;
        address_to caps = pick.best.caps;
        address_to seen = pick.seen;
        return 1;
}

/* A channel's number from its frequency, as the kernel numbers it
   (ieee80211_freq_khz_to_channel), and 0 off every band. The sum here
   before took 5000 from anything at 4900 MHz or over in unsigned
   arithmetic: the 4.9 GHz band printed as channel 858993439, the 6 GHz
   band's channel 2 as 187 and 60 GHz as 6 GHz channels by the thousand. */
static p32 radio_channel(p32 mhz)
{
        return mhz == 2484                    ? 14
               : mhz >= 2407 && mhz < 2484    ? (mhz - 2407) / 5
               : mhz >= 4910 && mhz <= 4980   ? (mhz - 4000) / 5
               : mhz >= 5000 && mhz < 5925    ? (mhz - 5000) / 5
               : mhz == 5935                  ? 2
               : mhz >= 5950 && mhz <= 7115   ? (mhz - 5950) / 5
               : mhz >= 58320 && mhz <= 70200 ? (mhz - 56160) / 2160
                                              : 0;
}

static radio_heard address_to radio_air_find(radio_air address_to air, string_address ssid)
{
        positive length = string_length(ssid);

        for (positive at = 0; at < air->count; at++)
                if (air->heard[at].ssid_length == length &&
                    !memory_compare(air->heard[at].ssid, ssid, length))
                        return air->heard + at;
        return null;
}

/*
        The station's filter: its architecture, then the calls that need no
        more than their number, then the four whose arguments decide, then
        death. A socket is netlink (route or generic) or the EAPOL packet
        socket and nothing else; an option is a netlink group joined or left,
        or extended acks; memory is never executable. Its broker's channel is
        a socket it already holds, spoken to with sendto and recvfrom.
*/
#define RADIO_NETLINK_DROP_MEMBERSHIP 2

static COLD fn radio_filter_build(net_filter address_to filter)
{
        static const p32 plain[] = {
            (p32)syscall(recvfrom), (p32)syscall(sendto), (p32)syscall(ppoll),
            (p32)syscall(clock_gettime), (p32)syscall(getrandom),
            (p32)syscall(close), (p32)syscall(bind), (p32)syscall(getsockname),
            (p32)syscall(nanosleep), (p32)syscall(clock_nanosleep),
            (p32)syscall(munmap), (p32)syscall(mremap), (p32)syscall(madvise),
            (p32)syscall(exit_group), (p32)syscall(exit),
            (p32)syscall(rt_sigreturn), (p32)syscall(restart_syscall),
#ifdef NET_APART_RISCV_HWPROBE
            NET_APART_RISCV_HWPROBE,
#endif
        };
        static const p32 datagram[] = {SOCK_DGRAM};
        static const p32 netlink_protocols[] = {NETLINK_ROUTE, NETLINK_GENERIC};
        static const p32 netlink_options[] = {NETLINK_ADD_MEMBERSHIP,
                                              RADIO_NETLINK_DROP_MEMBERSHIP,
                                              NETLINK_EXT_ACK};
        const p32 eapol[] = {(p32)network_order_16(ETH_P_PAE)};
        positive count = array_count(plain);
        positive call;
        positive family;

        filter->used = 0;
        net_filter_put(filter, NET_FILTER_LOAD, 0, 0, NET_FILTER_ARCH);
        net_filter_put(filter, NET_FILTER_EQUAL, 1, 0, NET_APART_ARCH);
        net_filter_answer(filter, SECCOMP_RET_KILL_PROCESS);
        net_filter_put(filter, NET_FILTER_LOAD, 0, 0, NET_FILTER_NUMBER);
        for (positive at = 0; at < count; at++)
                net_filter_put(filter, NET_FILTER_EQUAL, (p8)(count - at), 0, plain[at]);
        net_filter_put(filter, NET_FILTER_ALWAYS, 0, 0, 1);
        net_filter_answer(filter, SECCOMP_RET_ALLOW);

        //      socket: netlink route or generic, or a packet socket of EAPOL's
        //      ethertype; datagrams, with SOCK_NONBLOCK or SOCK_CLOEXEC.
        call = net_filter_when(filter, (p32)syscall(socket));
        net_filter_load(filter, 0);
        family = net_filter_when(filter, AF_NETLINK);
        net_filter_require(filter, 1, ~(p32)(SOCK_NONBLOCK | SOCK_CLOEXEC), datagram,
                           array_count(datagram));
        net_filter_require(filter, 2, ~(p32)0, netlink_protocols,
                           array_count(netlink_protocols));
        net_filter_answer(filter, SECCOMP_RET_ALLOW);
        net_filter_past(filter, family);
        family = net_filter_when(filter, AF_PACKET);
        net_filter_require(filter, 1, ~(p32)(SOCK_NONBLOCK | SOCK_CLOEXEC), datagram,
                           array_count(datagram));
        net_filter_require(filter, 2, ~(p32)0, eapol, array_count(eapol));
        net_filter_answer(filter, SECCOMP_RET_ALLOW);
        net_filter_past(filter, family);
        net_filter_answer(filter, SECCOMP_RET_KILL_PROCESS);
        net_filter_past(filter, call);

        //      setsockopt: a netlink group joined or left, extended acks.
        call = net_filter_when(filter, (p32)syscall(setsockopt));
        net_filter_load(filter, 1);
        family = net_filter_when(filter, SOL_NETLINK);
        net_filter_require(filter, 2, ~(p32)0, netlink_options,
                           array_count(netlink_options));
        net_filter_answer(filter, SECCOMP_RET_ALLOW);
        net_filter_past(filter, family);
        net_filter_answer(filter, SECCOMP_RET_KILL_PROCESS);
        net_filter_past(filter, call);

        //      mmap: anything but executable.
        call = net_filter_when(filter, (p32)syscall(mmap));
        net_filter_load(filter, 2);
        net_filter_put(filter, NET_FILTER_ANY_BIT, 1, 0, NET_APART_EXECUTABLE);
        net_filter_answer(filter, SECCOMP_RET_ALLOW);
        net_filter_answer(filter, SECCOMP_RET_KILL_PROCESS);
        net_filter_past(filter, call);

        net_filter_answer(filter, SECCOMP_RET_KILL_PROCESS);
}

#define RADIO_CAP_NET_ADMIN 12
#define RADIO_CAP_NET_RAW 13
#define RADIO_CAPABILITY_VERSION 0x20080522u
#define RADIO_PR_SET_KEEPCAPS 8
#define RADIO_PR_CAPBSET_DROP 24
#define RADIO_PR_CAP_AMBIENT 47
#define RADIO_PR_CAP_AMBIENT_CLEAR_ALL 4

/*
        The station made what it may be: every capability but CAP_NET_ADMIN
        (nl80211, the link up) and CAP_NET_RAW (the EAPOL socket) out of its
        bounding set, nobody when it is root, those two raised again and no
        other, nothing ambient, not dumpable, no new privileges, then its
        filter. A user namespace that maps root and not nobody keeps root,
        which owns nothing outside it (net_apart_nobody). False at the first
        refusal, and nothing is joined.
*/
static COLD bool radio_station_confine(void)
{
        struct
        {
                p32 version;
                b32 pid;
        } head = {RADIO_CAPABILITY_VERSION, 0};
        struct
        {
                p32 effective;
                p32 permitted;
                p32 inheritable;
        } data[2] = {{0}};
        p32 keep = 1u << RADIO_CAP_NET_ADMIN | 1u << RADIO_CAP_NET_RAW;
        net_filter filter;

        for (positive cap = 0; cap < 64; cap++)
        {
                bipolar dropped;

                if (cap == RADIO_CAP_NET_ADMIN || cap == RADIO_CAP_NET_RAW)
                        continue;
                dropped = system_call_5(syscall(prctl), RADIO_PR_CAPBSET_DROP, cap, 0, 0, 0);
                if (dropped == -EINVAL)
                        break;
                if (dropped < 0)
                        return false;
        }
        if (system_call_5(syscall(prctl), RADIO_PR_SET_KEEPCAPS, 1, 0, 0, 0) < 0 ||
            !net_apart_nobody())
                return false;
        data[0].effective = data[0].permitted = keep;
        radio_filter_build(address_of filter);
        return system_call_2(syscall(capset), (positive)address_of head,
                             (positive)data) == 0 &&
               system_call_5(syscall(prctl), RADIO_PR_CAP_AMBIENT,
                             RADIO_PR_CAP_AMBIENT_CLEAR_ALL, 0, 0, 0) >= 0 &&
               system_call_5(syscall(prctl), PR_SET_DUMPABLE, 0, 0, 0, 0) == 0 &&
               system_call_5(syscall(prctl), PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0 &&
               net_filter_install(address_of filter);
}

/* The station: confined, then the join, its status to the broker, and for
   as long as the link stands, its keeper. */
static DEAD_END COLD fn radio_station_run(b32 channel, bipolar broker, p8 address_to ssid,
                                          positive ssid_length, p8 address_to pass,
                                          positive pass_length, bool force)
{
        wifi_link link = {.session = {.handle = -1}, .eapol = -1};
        p32 kept[1] = {(p32)channel};
        radio_ask ask;
        radio_answer answer;
        bipolar failed = WIFI_REFUSE_CONFINE;
        bool confined;

        radio_station = channel;
        confined = system_call_5(syscall(prctl), PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0) == 0 &&
                   system_call(syscall(getppid)) == broker &&
                   descriptors_close_outside(3, kept, 1) && radio_station_confine();
        if (confined)
                failed = nl80211_join(ssid, ssid_length, pass, pass_length, force,
                                      address_of link);
        crypto_forget(pass, pass_length);
        radio_secrets_forget();
        memory_fill(address_of ask, 0, sizeof(ask));
        ask.what = RADIO_ASK_JOINED;
        ask.status = failed;
        radio_station_ask(address_of ask, address_of answer);
        if (!failed)
        {
                //      The join's scan news is nothing to the keeper, which
                //      would wake for every scan anyone asked for.
                if (link.session.scan)
                        socket_option_set(link.session.handle, SOL_NETLINK,
                                          RADIO_NETLINK_DROP_MEMBERSHIP,
                                          address_of link.session.scan,
                                          sizeof(link.session.scan));
                wifi_keep(address_of link);
        }
        wifi_link_close(address_of link);
        system_call_1(syscall(exit_group), 0);
        unreachable_here();
}

/* A status the join can return: joined, an errno, an access point's reason
   for ending the handshake, or one of the join's own refusals. */
static CONST COLD bool radio_join_status(b64 status)
{
        return (status <= 0 && status >= -4095) ||
               (status <= -1000 && status > -1000 - 65536) ||
               (status >= WIFI_REFUSE_FIRST && status <= WIFI_REFUSE_LAST) ||
               status == WIFI_REFUSE_CONFINE;
}

/* What the broker does for one question, with checks on every field: a name
   no longer than a name, a line to log only as a number it has a line for. */
static COLD fn radio_broker_answer(radio_ask address_to ask, radio_answer address_to answer)
{
        p8 address_to ssid = ask->named ? ask->ssid : null;
        positive ssid_length = ask->named ? ask->ssid_length : 0;

        memory_fill(answer, 0, sizeof(*answer));
        if (ssid_length > sizeof(ask->ssid))
                return;
        switch (ask->what)
        {
        case RADIO_ASK_RULES_GET:
                answer->answer = ssid ? radio_rules_get(ssid, ssid_length) : 0;
                break;
        case RADIO_ASK_RULES_PUT:
                if (ssid)
                        (void)radio_rules_put(ssid, ssid_length, ask->mask, ask->rules);
                break;
        case RADIO_ASK_AVOID_LOAD:
                answer->count = radio_avoid_load((radio_avoid address_to)answer->avoid);
                break;
        case RADIO_ASK_AVOID_ADD:
                radio_avoid_add(ask->bssid);
                break;
        case RADIO_ASK_SCAN_AGE:
                answer->answer = (bipolar)radio_scan_age(ssid, ssid_length);
                break;
        case RADIO_ASK_SCAN_NOTE:
                if (ssid)
                        radio_scan_note(ssid, ssid_length);
                break;
        case RADIO_ASK_SAE_BUDGET:
                answer->answer = wifi_sae_budget(ask->bssid);
                break;
        case RADIO_ASK_SAID:
                if (ask->status == RADIO_SAID_NO_CLOCK)
                        radio_keeper_said("no clock to check the link by");
                else if (ask->status == RADIO_SAID_SOCKETS)
                        radio_keeper_said("its sockets failed");
                break;
        }
}

/*
        A broker that could not start says so to whoever asked for the join,
        and ends: nothing was asked of the air, so the join is no network's
        failure and is not kept as one (radio_last_set). It ended without a
        word, which the joiner took for a station that ended without one
        (WIFI_REFUSE_APART), and the network was written down as last failed
        for it: a cut at the keeper's lock left wifi.last as neither the state
        it was in nor the one the join makes.
*/
static DEAD_END COLD fn radio_broker_unstarted(b32 told)
{
        b64 status = WIFI_REFUSE_UNSTARTED;

        (void)system_write_all((positive)told, address_of status, sizeof(status));
        system_call_1(syscall(exit_group), 1);
        unreachable_here();
}

/*
        The broker: root, detached, holding the keeper's lock. It forks the
        station, forgets every password it carries, then answers questions
        until the station ends; the join's status goes to whoever asked for
        the join, once, through told. A status that is no status the join
        returns is the station's failure.
*/
static DEAD_END COLD fn radio_broker(b32 told, p8 address_to ssid, positive ssid_length,
                                     p8 address_to pass, positive pass_length, bool force)
{
        b32 ends[2];
        bipolar broker = system_call(syscall(getpid));
        bipolar station;
        positive raw = 0;
        bool joined = false;

        if (host_lock_file(RADIO_KEEPER_PATH, HOST_FILE_SETLK, null) < 0 ||
            system_call_4(syscall(socketpair), AF_UNIX,
                          RADIO_SOCK_SEQPACKET | SOCK_CLOEXEC, 0, (positive)ends) < 0)
                radio_broker_unstarted(told);
        station = system_fork();
        if (station < 0)
                radio_broker_unstarted(told);
        if (!station)
                radio_station_run(ends[1], broker, ssid, ssid_length, pass, pass_length, force);
        system_close(ends[1]);
        crypto_forget(pass, pass_length);
        radio_secrets_forget();

        for (;;)
        {
                radio_ask ask;
                radio_answer answer;
                network_deadline deadline;
                bipolar ready;

                //      The station asks when it has to and may keep the link
                //      for days: an hour's wait at a time, for ever.
                if (!network_deadline_begin(address_of deadline, 3600, 0))
                        break;
                ready = network_wait_readable_until(ends[0], address_of deadline);
                if (!ready)
                        continue;
                if (ready < 0 ||
                    socket_receive(ends[0], address_of ask, sizeof(ask), MSG_DONTWAIT, null,
                                   null) != (bipolar)sizeof(ask))
                        break;
                if (ask.what == RADIO_ASK_JOINED && told >= 0)
                {
                        b64 status = ask.status;

                        if (!radio_join_status(status))
                                status = WIFI_REFUSE_APART;
                        joined = status == 0;
                        if (joined)
                        {
                                host_state_ready();
                                (void)host_write_file(RADIO_KEPT_PATH, (p8 address_to) "1\n", 2, 0644,
                                                false);
                        }
                        (void)system_write_all((positive)told, address_of status, sizeof(status));
                        system_close(told);
                        told = -1;
                        memory_fill(address_of answer, 0, sizeof(answer));
                }
                else if (ask.what == RADIO_ASK_JOINED)
                        break;
                else
                        radio_broker_answer(address_of ask, address_of answer);
                if (socket_send(ends[0], address_of answer, sizeof(answer), MSG_NOSIGNAL, null,
                                0) != (bipolar)sizeof(answer))
                        break;
        }
        system_close(ends[0]);
        system_call_2(syscall(kill), (positive)station, SIGKILL);
        (void)system_wait4_retry(station, address_of raw, 0, null);
        //      A station that ended of its own -- the link went -- leaves
        //      nothing kept. One that died (a signal, its filter) left a link
        //      whose keys nobody keeps: wifi.kept stays, so the machine's next
        //      pass joins again, and the log says so.
        if (joined && (raw & 0x7f) == 0)
                system_remove_at(AT_FDCWD, RADIO_KEPT_PATH, 0);
        else if (joined)
                radio_keeper_said("its station died; the link is joined again");
        system_call_1(syscall(exit_group), 0);
        unreachable_here();
}

/* A join, apart: whatever kept the last link is ended, a broker is forked
   that outlives this process, and its word of the join is waited for. */
static COLD bipolar radio_join_apart(p8 address_to ssid, positive ssid_length,
                                     p8 address_to pass, positive pass_length, bool force)
{
        b32 told[2];
        b64 status = WIFI_REFUSE_APART;
        bipolar got;

        radio_keeper_stop();
        if (system_call_2(syscall(pipe2), (positive)told, O_CLOEXEC) < 0)
                return WIFI_REFUSE_APART;
        //      The broker's stdio is replaced; an end that is one of the
        //      first three would be lost to that.
        if (told[1] < 3 || told[0] < 3)
        {
                system_close(told[0]);
                system_close(told[1]);
                return WIFI_REFUSE_APART;
        }
        if (host_detach((p32)told[1], descriptors_none))
                radio_broker(told[1], ssid, ssid_length, pass, pass_length, force);
        system_close(told[1]);
        got = system_read_retry((positive)told[0], address_of status, sizeof(status));
        system_close(told[0]);
        return got == (bipolar)sizeof(status) ? (bipolar)status : WIFI_REFUSE_APART;
}

// One byte spelled \xNN, as a name that is shown spells what it does not show: four bytes, not ended.
static positive radio_spell(p8 address_to into, p8 byte)
{
        into[0] = '\\';
        into[1] = 'x';
        return 2 + memory_into_hex(into + 2, address_of byte, 1);
}

/* A name made safe to print, and how many columns it takes. */
static positive radio_display(p8 address_to into, positive room, p8 address_to name,
                              positive length)
{
        positive used = 0;
        positive columns = 0;
        positive at = 0;

        while (at < length && used + 5 < room)
        {
                bool printable;
                positive step = file_terminal_step(name + at, length - at, true,
                                                   address_of printable);

                if (printable && used + step < room)
                {
                        memory_copy(into + used, name + at, step);
                        used += step;
                        columns++;
                        at += step;
                        continue;
                }
                //      What is not shown is spelled a byte at a time.
                used += radio_spell(into + used, name[at]);
                columns += 4;
                at++;
        }
        into[used] = end;
        return columns;
}

/* One row: marker, name, signal, security, band and channel. */
static fn radio_air_row(radio_heard address_to heard, positive width, bool saved)
{
        p8 name[RADIO_SSID_MOST * 4 + 1];
        p8 line[320];
        p8 number[24];
        positive columns = radio_display(name, sizeof(name), heard->ssid, heard->ssid_length);
        bipolar dbm = heard->mbm / 100;
        p32 mhz = heard->frequency;
        positive bars = dbm >= -55 ? 4 : dbm >= -67 ? 3 : dbm >= -75 ? 2 : dbm >= -85 ? 1 : 0;
        string_address band = mhz >= 58320  ? (string_address) "60 GHz"
                              : mhz >= 5925 ? (string_address) "6 GHz"
                              : mhz >= 4900 ? (string_address) "5 GHz"
                                            : (string_address) "2.4 GHz";
        p32 channel = radio_channel(mhz);

        radio_line(line, sizeof(line), heard->joined ? (string_address) "* "
                                       : saved       ? (string_address) "+ "
                                                     : (string_address) "  ",
                   name, null, null, null);
        for (; columns < width + 2; columns++)
                string_append_bounded(line, (string_address) " ", sizeof(line));
        bipolar_into_string(number, dbm);
        for (positive pad = string_length(number); pad < 4; pad++)
                string_append_bounded(line, (string_address) " ", sizeof(line));
        radio_line(line + string_length(line), sizeof(line) - string_length(line), number,
                   (string_address) " dBm ", null, null, null);
        for (positive at = 0; at < 4; at++)
                string_append_bounded(line, at < bars ? (string_address) "#" : (string_address) ".",
                                      sizeof(line));
        string_append_bounded(line, (string_address) "  ", sizeof(line));
        string_append_bounded(line, radio_security_words[heard->security], sizeof(line));
        for (positive pad = string_length(radio_security_words[heard->security]); pad < 8;
             pad++)
                string_append_bounded(line, (string_address) " ", sizeof(line));
        bipolar_into_string(number, (bipolar)channel);
        radio_line(line + string_length(line), sizeof(line) - string_length(line), band,
                   (string_address) " ch ", number, null, null);
        string_format(log, host_label "%s\n", line);
}

/* ---- wifi: what the last join said, and the password asked for ---- */

/* The reason a join gave, as the words that follow the network's name. */
/* What the join's plan says of the access point heard by a saved network's
   name, the refusal or 0: a twin that offers less than the network taught
   is said for what it is, whether or not the machine's pass has asked it. */
static COLD bipolar radio_heard_refused(const radio_heard address_to heard,
                                        const radio_network address_to networks,
                                        positive count)
{
        positive at = radio_saved_index(networks, count, (string_address)heard->ssid,
                                        heard->ssid_length);
        wifi_plan plan;
        wifi_rsn_caps caps = heard->caps;

        if (at == count || !networks[at].pass_length)
                return 0;
        return wifi_join_plan(MOONWATER_STRICT_NET, address_of caps,
                              radio_rules_get((p8 address_to)networks[at].ssid,
                                              networks[at].ssid_length),
                              networks[at].pass_length == 64, address_of plan);
}

static string_address radio_join_words(bipolar failed)
{
        static p8 words[48];

        if (failed <= -1000 && failed > -1000 - 65536)
        {
                p8 number[24];

                bipolar_into_string(number, -1000 - failed);
                radio_line(words, sizeof(words),
                           (string_address) "ended the handshake (reason ", number,
                           (string_address) ")", null, null);
                return (string_address)words;
        }
        return failed == -110   ? (string_address) "did not associate"
               : failed == -111 ? (string_address) "refused the join"
               : failed == -13  ? (string_address) "did not accept the password"
               : failed == -62  ? (string_address) "did not begin the handshake"
               : failed == -121 ? (string_address) "stopped answering in the handshake"
               : failed == -113 ? (string_address) "is not in range"
               : failed == WIFI_REFUSE_TKIP
                   ? (string_address) "uses TKIP (WPA1), which is broken and never joined; "
                                      "set the access point to WPA2 or WPA3 with AES (CCMP)"
               : failed == WIFI_REFUSE_CIPHER
                   ? (string_address) "uses a cipher other than AES (CCMP), which moonwater "
                                      "does not offer"
               : failed == WIFI_REFUSE_PMF_GONE
                   ? (string_address) "no longer offers management frame protection, which it "
                                      "did (an impostor or a changed access point; wifi remove "
                                      "and add it again to accept that)"
               : failed == WIFI_REFUSE_SAE_GONE
                   ? (string_address) "no longer offers WPA3, which it was joined with (an "
                                      "impostor or a changed access point; wifi remove and add "
                                      "it again to accept that)"
               : failed == WIFI_REFUSE_NO_PMF
                   ? (string_address) "does not protect its management frames, which this "
                                      "machine requires"
               : failed == WIFI_REFUSE_WPA2
                   ? (string_address) "offers WPA2 alone, which this machine joins only for a "
                                      "network saved with wpa2 (moonwater wifi add SSID - wpa2)"
               : failed == WIFI_REFUSE_KEY_ONLY
                   ? (string_address) "asks for WPA3, which needs the password itself and not a "
                                      "key of 64 hex digits"
               : failed == WIFI_REFUSE_SUITE
                   ? (string_address) "asks for key management moonwater does not offer"
               : failed == WIFI_REFUSE_SAE_BUDGET
                   ? (string_address) "was asked for WPA3 more often than a minute allows; it is "
                                      "tried again later"
               : failed == WIFI_REFUSE_ELEMENTS
                   ? (string_address) "showed other security in its handshake than in its "
                                      "beacon (an impostor, or a downgrade on the way)"
               : failed == WIFI_REFUSE_CONFINE
                   ? (string_address) "was not joined: the Wi-Fi station cannot be confined "
                                      "on this kernel (no seccomp filter)"
               : failed == WIFI_REFUSE_APART
                   ? (string_address) "was not joined: the confined Wi-Fi station ended "
                                      "without a word"
               : failed == WIFI_REFUSE_UNSTARTED
                   ? (string_address) "was not joined: the join could not be started on this "
                                      "machine"
                   : (string_address) "could not be joined";
}

/* A join's failure that radio_join_words says in words after the network's
   name: the network's doing, or the station that could not be confined or
   ended without a word. The machine's own failures are not told so. */
static bool radio_join_told(bipolar failed)
{
        return failed == -110 || failed == -111 || failed == -13 || failed == -62 ||
               failed == -121 || failed == -113 ||
               (failed >= WIFI_REFUSE_FIRST && failed <= WIFI_REFUSE_LAST) ||
               failed == WIFI_REFUSE_CONFINE || failed == WIFI_REFUSE_APART ||
               (failed <= -1000 && failed > -1000 - 65536);
}

/*
        The network the machine last failed to join, why, when, and how many
        times in a row for that reason, for bare wifi and status and for the
        machine's own pass, which leaves a network alone for a while after
        it was not there or refused the password (radio_join_wait): three
        seconds, then twice as long each time, to a minute. Gone once a join
        works. The text is the name, the code, the boot-time milliseconds and
        the count, a line each.
*/
typedef struct
{
        p8 ssid[RADIO_SSID_MOST + 1];
        bipolar failed;
        p64 at;
        positive count;
} radio_last;

static bool radio_last_get(radio_last address_to last)
{
        p8 text[RADIO_SSID_MOST + 64];
        p8 address_to code;
        p8 address_to when;
        p8 address_to count;
        positive at;

        if (host_read_word(RADIO_LAST_PATH, text, sizeof(text)) <= 0)
                return false;
        at = (positive)(string_first_of_or_end(text, '\n') - text);
        if (!text[at] || at > RADIO_SSID_MOST)
                return false;
        text[at] = end;
        string_copy_bounded(last->ssid, text, sizeof(last->ssid));

        //      Each of the rest ends at its line: a number is read from its
        //      end backwards, and runs into the next line's digits otherwise.
        code = text + at + 1;
        when = (p8 address_to)string_first_of_or_end(code, '\n');
        last->at = 0;
        last->count = 0;
        if (*when)
        {
                *when++ = end;
                count = (p8 address_to)string_first_of_or_end(when, '\n');
                if (*count)
                {
                        *count++ = end;
                        last->count = string_to_positive(count);
                }
                last->at = string_to_positive(when);
        }
        last->failed = -(bipolar)string_to_positive(code);
        return true;
}

static fn radio_last_set(string_address ssid, bipolar failed)
{
        p8 text[RADIO_SSID_MOST + 80];
        p8 number[24];
        radio_last before;
        positive count = 1;
        positive used;

        if (!failed || failed == -19)
        {
                system_remove_at(AT_FDCWD, RADIO_LAST_PATH, 0);
                return;
        }
        //      No join was made, so there is nothing to say of the network.
        if (failed == WIFI_REFUSE_UNSTARTED)
                return;
        //      Another time for the same name and reason, or the first.
        //      A pass that asked nothing of the air, and was told by the scan of
        //      a moment ago, has not found the network no further away.
        if (radio_last_get(address_of before) && before.failed == failed &&
            string_equals((string_address)before.ssid, ssid))
                count = before.count + (radio_scan_declined ? 0 : 1);
        bipolar_into_string(number, -failed);
        radio_line(text, sizeof(text), ssid, (string_address) "\n", number,
                   (string_address) "\n", null);
        used = string_length(text);
        bipolar_into_string(number, (bipolar)(system_clock_ns(HOST_CLOCK_BOOTTIME) / 1000000));
        radio_line(text + used, sizeof(text) - used, number, (string_address) "\n", null, null,
                   null);
        used = string_length(text);
        bipolar_into_string(number, (bipolar)count);
        radio_line(text + used, sizeof(text) - used, number, (string_address) "\n", null, null,
                   null);
        host_state_ready();
        (void)host_write_file(RADIO_LAST_PATH, text, string_length(text), 0644, false);
}

/* Milliseconds the machine's own pass leaves the network that failed this
   way alone, from the time it did: only a network that was not there,
   refused the password or was refused by the join's own rules (which no
   pass will change, and each of which a scan found) is waited on, three
   seconds the first time and twice as long each, to a minute. */
static p64 radio_join_wait(const radio_last address_to last)
{
        positive doubled = last->count > 1 ? last->count - 1 : 0;
        p64 wait;

        if ((last->failed != -113 && last->failed != -13 &&
             (last->failed < WIFI_REFUSE_FIRST || last->failed > WIFI_REFUSE_LAST)) ||
            !last->at)
                return 0;
        wait = (p64)3000 << (doubled > 5 ? 5 : doubled);
        return wait > 60000 ? 60000 : wait;
}

/* Whether the last join failed so lately that the next one is left to wait:
   the pass and a restore both ask. */
static bool radio_join_waiting(void)
{
        radio_last last;

        return radio_last_get(address_of last) &&
               system_clock_ns(HOST_CLOCK_BOOTTIME) / 1000000 <
                   last.at + radio_join_wait(address_of last);
}

/*
        A password, without argv: ps shows every process's arguments to
        every user. At a terminal it is asked for with the echo off and the
        line read here, a byte at a time, so Control-C and Control-D cancel
        with the terminal put back rather than killing the process with the
        echo still off. Anywhere else it is one line of standard input.
        Negative when cancelled; else its length, 0 for an open network.
*/
#define RADIO_TERMINAL_GET 0x5401
#define RADIO_TERMINAL_SET 0x5402

static bipolar radio_password_read(p8 address_to into, positive room,
                                   string_address ssid, bool only_asked,
                                   bool group)
{
        p8 saved[64];
        p8 quiet[64];
        p8 shown[RADIO_SSID_MOST * 4 + 1];
        bool terminal = system_call_3(syscall(ioctl), 0, RADIO_TERMINAL_GET,
                                      (positive)saved) >= 0;
        positive used = 0;
        bipolar result = 0;

        into[0] = end;
        if (only_asked && !terminal)
                return 0;
        if (terminal)
        {
                p32 modes;

                memory_copy(quiet, saved, sizeof(quiet));
                modes = memory_load_unaligned(p32, quiet + 12);
                modes &= ~(p32)(0x8 | 0x2 | 0x1); // ECHO, ICANON, ISIG
                memory_copy(quiet + 12, address_of modes, 4);
                quiet[17 + 6] = 1; // VMIN
                quiet[17 + 5] = 0; // VTIME
                radio_display(shown, sizeof(shown), ssid, string_length(ssid));
                if (group)
                        host_say(log_error, host_label "secret for %s: ", shown);
                else
                        host_say(log_error, host_label "password for %s (empty for an open network): ",
                                 shown);
                system_call_3(syscall(ioctl), 0, RADIO_TERMINAL_SET, (positive)quiet);
        }

        for (;;)
        {
                p8 byte;
                bipolar got = system_read_once(0, address_of byte, 1);

                if (got == -4)
                        continue;
                if (got <= 0)
                {
                        //      The end of input with nothing before it is no
                        //      answer: a pass or a pipe that failed would
                        //      otherwise save the network as an open one.
                        //      An empty line is the answer for an open one.
                        if (!used)
                                result = -1;
                        break;
                }
                if (byte == '\n' || byte == '\r')
                        break;
                if (terminal)
                {
                        if (byte == 3 || (byte == 4 && !used))
                        {
                                result = -1;
                                break;
                        }
                        if (byte == 0x7f || byte == 8)
                        {
                                while (used && (into[used - 1] & 0xc0) == 0x80)
                                        used--;
                                if (used)
                                        used--;
                                continue;
                        }
                        if (byte == 0x15)
                        {
                                used = 0;
                                continue;
                        }
                        if (byte < 32)
                                continue;
                }
                else if (byte < 32)
                {
                        //      Not what somebody typed: a NUL would end it
                        //      where it is kept, and what lay before the NUL
                        //      would be saved as the password.
                        result = -1;
                        break;
                }
                if (used + 1 < room)
                        into[used++] = byte;
        }

        if (terminal)
        {
                system_call_3(syscall(ioctl), 0, RADIO_TERMINAL_SET, (positive)saved);
                host_say(log_error, "\n");
        }
        into[used] = end;
        crypto_forget(quiet, sizeof(quiet));
        //      A password given up on (Control-C or Control-D after some of
        //      it was typed) leaves nothing of it in the caller's bytes.
        if (result < 0)
                crypto_forget(into, room);
        return result < 0 ? result : (bipolar)used;
}

/* One network joined, asked for (force) or by the machine's own pass. */
static bipolar radio_wifi_join(string_address ssid, string_address pass, bool force)
{
        bipolar failed = -19;
        positive ssid_length = string_length(ssid);
        positive pass_length = pass ? string_length(pass) : 0;
        p64 started;

        if (!ssid_length || ssid_length > RADIO_SSID_MOST)
                return -22;

        if (pass_length && !wifi_pass_fits((p8 address_to)pass, pass_length))
                return -22;

        started = system_clock_ns(HOST_CLOCK_BOOTTIME);
        for (;;)
        {
                failed = radio_join_apart((p8 address_to)ssid, ssid_length,
                                          (p8 address_to)pass, pass_length, force);
                if (failed != -19)
                        break;
                //      No wireless interface yet: waited for only where the
                //      machine shows a card on its way. A machine with none
                //      would hold every boot, and every `wifi on`, for the
                //      whole wait, and the keeper joins when a card arrives.
                if (!radio_has_interface() ||
                    system_clock_ns(HOST_CLOCK_BOOTTIME) - started >= 8000000000)
                        break;
                host_pause(200000000);
        }

        return failed;
}

static bipolar radio_wifi_leave(void)
{
        nl80211 session;
        nl80211_iface iface;
        bipolar failed;

        //      The keeper goes whether or not the link can be asked to leave: a
        //      keeper left running would join the network again.
        radio_keeper_stop();
        failed = nl80211_open(address_of session);
        if (failed < 0)
                return failed;

        failed = nl80211_interface(address_of session, address_of iface);
        if (!failed)
                failed = nl80211_disconnect(address_of session, iface.index);

        nl80211_close(address_of session);
        return failed;
}

static b32 radio_wifi_bring(bool say)
{
        radio_network networks[RADIO_WIFI_MOST];
        positive count = radio_wifi_load(networks, RADIO_WIFI_MOST, null);
        positive at;
        bipolar failed = 0;
        bool joined = false;
        b32 switched = radio_switch(NET_WIFI_POWER, RADIO_RFKILL_WLAN, true, say);

        radio_secret_hold(networks, sizeof(networks));
        scope_exit(radio_secret_let_go(networks));

        if (!say && nl80211_associated())
        {
                radio_net_wake();
                crypto_forget(networks, sizeof(networks));
                return 0;
        }

        for (at = 0; at < count; at++)
        {
                failed = radio_wifi_join((string_address)networks[at].ssid,
                                         (string_address)networks[at].pass, say);
                if (!failed)
                {
                        joined = true;
                        if (say)
                        {
                                p8 shown[RADIO_SSID_MOST * 4 + 1];

                                radio_display(shown, sizeof(shown), networks[at].ssid,
                                              networks[at].ssid_length);
                                host_say(log, host_label "wifi joined %s\n", (string_address)shown);
                        }
                        break;
                }
                //      No interface, or no join that can start: the next
                //      network would fail the same way.
                if (failed == -19 || failed == WIFI_REFUSE_UNSTARTED)
                        break;
        }
        //      One pass is one failure, whichever of the networks it was the
        //      last of: its count is what the next pass waits by.
        if (count)
                radio_last_set((string_address)networks[at < count ? at : count - 1].ssid,
                               failed);

        radio_net_wake();
        crypto_forget(networks, sizeof(networks));

        if (!count)
        {
                if (say && !switched)
                        host_say(log, host_label "wifi on\n");
                return switched;
        }

        if (joined)
                return switched;

        if (say && failed == -19)
        {
                p8 why[RADIO_WHY_ROOM];

                return radio_wifi_why(why, sizeof(why))
                           ? host_refuse("wifi: %s\n", why)
                           : host_refuse("no wireless interface\n");
        }
        if (say)
                return radio_join_told(failed)
                           ? host_refuse("the network %s\n", radio_join_words(failed))
                           : host_fail("wifi", failed ? failed : -1);
        return 1;
}

static b32 radio_wifi_on(bool say)
{
        bipolar lock = radio_lock(true);
        b32 result;

        if (lock < 0)
                return say ? host_fail("wifi", lock) : 1;
        result = radio_wifi_bring(say);
        radio_unlock(lock);
        return result;
}

/* Wifi off: the word kept, the link left, the radio blocked. report is for
   somebody waiting to hear what did not go through. */
static b32 radio_wifi_off(bool report)
{
        bipolar lock = radio_lock(true);
        bipolar kept;
        b32 failed;

        if (lock < 0)
                return report ? host_fail("wifi", lock) : 1;
        kept = radio_write_word(NET_WIFI_POWER, "off");
        radio_wifi_leave();
        failed = radio_failed(report, NET_WIFI_POWER, kept,
                              radio_rfkill(RADIO_RFKILL_WLAN, true));
        radio_net_wake();
        radio_unlock(lock);
        return failed;
}

/* The network put on the saved list, or put there again with the password it
   was last given. -E2BIG when the list is full, -EFBIG when it is more than
   this reads and would be written back short. */
static bipolar radio_wifi_store(string_address ssid, string_address pass,
                                bool address_to same)
{
        radio_network networks[RADIO_WIFI_MOST];
        bool cut;
        bipolar loaded = radio_wifi_read(networks, RADIO_WIFI_MOST, address_of cut);
        positive count = loaded > 0 ? (positive)loaded : 0;
        positive at;
        positive ssid_length = string_length(ssid);
        positive pass_length = pass ? string_length(pass) : 0;
        bipolar failed = 0;

        if (loaded < 0)
        {
                crypto_forget(networks, sizeof(networks));
                return loaded;
        }
        at = radio_saved_index(networks, count, ssid, string_length(ssid));

        //      The same network with the same password is already what the
        //      list says: nothing is written for it, and the caller is told.
        address_to same = at < count && networks[at].pass_length == pass_length &&
                          string_equals((string_address)networks[at].pass,
                                        pass ? pass : (string_address) "");

        if (cut)
                failed = -EFBIG;
        else if (address_to same)
                failed = 0;
        else if (at == count && count++ == RADIO_WIFI_MOST)
                failed = -E2BIG;
        else
        {
                memory_fill(networks[at].ssid, 0, sizeof(networks[at].ssid));
                memory_copy(networks[at].ssid, ssid, ssid_length);
                networks[at].ssid[ssid_length] = end;
                networks[at].ssid_length = (p8)ssid_length;
                memory_fill(networks[at].pass, 0, sizeof(networks[at].pass));
                if (pass_length)
                        memory_copy(networks[at].pass, pass, pass_length);
                networks[at].pass[pass_length] = end;
                networks[at].pass_length = (p8)pass_length;
                failed = radio_wifi_save(networks, count);
        }

        crypto_forget(networks, sizeof(networks));
        return failed;
}

/* What a network that asks for what the join cannot give is told, after
   "asks for": TKIP (and WPA, whose cipher it is) is broken and the fix is at
   the access point; the rest are not built. */
static fn radio_unjoinable(p8 address_to into, positive room, p8 security)
{
        bool broken = security == RADIO_WPA || security == RADIO_TKIP;

        radio_line(into, room,
                   security == RADIO_WPA ? (string_address) "WPA (TKIP)"
                                         : radio_security_words[security],
                   broken ? (string_address) ", which is broken and never joined; set the "
                                             "access point to WPA2 or WPA3 with AES (CCMP)"
                          : (string_address) ", which moonwater cannot join yet",
                   null, null, null);
}

/* Everything of `wifi add` that happens with the radio lock held. */
static b32 radio_wifi_enter(string_address ssid, string_address pass, bool wpa2)
{
        radio_air air;
        radio_heard address_to heard = null;
        positive pass_length = pass ? string_length(pass) : 0;
        bool same;
        //      Asked, and pinned, before the network is saved: a saved network is what makes a radio with no word on, so
        //      a network saved with the pin not yet written (a failed write, a cut) would raise the radio at the next boot.
        bool off = radio_kept(NET_WIFI_POWER) == 0;
        bipolar pinned = off ? radio_write_word(NET_WIFI_POWER, "off") : 0;
        bipolar stored;
        bipolar failed;
        p8 why[RADIO_WHY_ROOM];

        b32 switched;

        return_if(pinned < 0, host_fail(NET_WIFI_POWER, pinned));
        stored = radio_wifi_store(ssid, pass, address_of same);
        if (stored == -E2BIG)
                return host_refuse("too many saved networks\n");
        if (stored == -EFBIG)
                return host_refuse("%s is too long to change here\n", NET_WIFI_LIST);
        return_if(stored < 0, host_fail(NET_WIFI_LIST, stored));
        //      What the network taught the join stands while its password
        //      does; whether WPA2 is let through the tight tier is what
        //      this add says.
        failed = radio_rules_put((p8 address_to)ssid, string_length(ssid),
                                 (p8)((same ? 0 : WIFI_RULE_PMF | WIFI_RULE_SAE) | WIFI_RULE_WPA2),
                                 wpa2 ? WIFI_RULE_WPA2 : 0);
        //      Said, and the join not made: the network is saved, but without
        //      the rules that say how it may be joined.
        return_if(failed < 0, host_fail(RADIO_RULES_PATH, failed));

        //      A radio that is off stays off: the network is saved, and moonwater wifi on is what joins it.
        if (off)
        {
                host_say(log, host_label "wifi saved %s; wifi is off, moonwater wifi on joins it\n", ssid);
                return 0;
        }
        switched = radio_switch(NET_WIFI_POWER, RADIO_RFKILL_WLAN, true, true);

        /*      What the air already says about it, from the last scan and
                without asking for another: a network that asks for what the
                join cannot give is saved and not tried, which would only
                have waited out the association timeout to say less. */
        if (radio_air_take(address_of air, RADIO_AIR_STALE | RADIO_AIR_JOINABLE) &&
            ((heard = radio_air_find(address_of air, ssid)) ||
             (radio_air_take(address_of air, RADIO_AIR_NOW | RADIO_AIR_JOINABLE) &&
              (heard = radio_air_find(address_of air, ssid)))))
        {
                //      Said again by a script at every boot, with a link that
                //      holds: nothing is left and joined over.
                if (same && heard->joined && host_lock_owner(RADIO_KEEPER_PATH) > 0 &&
                    nl80211_associated())
                {
                        host_say(log, host_label "wifi already joined %s\n", ssid);
                        return switched;
                }
                if (!heard->joinable)
                {
                        radio_unjoinable(why, sizeof(why), heard->security);
                        return host_refuse("saved, but it asks for %s\n", why);
                }
                if (heard->security != RADIO_OPEN && !pass_length)
                        return host_refuse("saved with no password, but it "
                                           "asks for one\n");
        }

        /*      No radio at all is not one that is still arriving: the join's
                eight seconds of asking are for a card whose interface is on
                its way at boot. */
        failed = radio_has_interface() ? radio_wifi_join(ssid, pass, true) : -19;
        radio_last_set(ssid, failed);

        radio_net_wake();
        if (failed == -19)
                return radio_wifi_why(why, sizeof(why))
                           ? host_refuse("saved; wifi: %s\n", why)
                           : host_refuse("saved, but there is no "
                                         "wireless interface\n");
        if (failed == -113)
                return host_refuse("saved, but it is not in range\n");
        if (radio_join_told(failed))
        {
                if (failed == -110 &&
                    radio_air_take(address_of air, RADIO_AIR_CACHED) &&
                    air.count && !radio_air_find(address_of air, ssid))
                        return host_refuse("saved, but it is not in range\n");
                return host_refuse("saved, but the network %s\n",
                                   radio_join_words(failed));
        }
        return_if(failed < 0, host_fail("wifi", failed));
        if (pass && pass[0])
                crypto_forget((address_any)pass, string_length(pass));

        host_say(log, host_label "wifi joined %s\n", ssid);
        return switched;
}

static b32 radio_wifi_add(string_address ssid, string_address pass, bool wpa2)
{
        positive ssid_length = string_length(ssid);
        positive pass_length = pass ? string_length(pass) : 0;
        bipolar lock;
        b32 result;

        if (!ssid_length || ssid_length > RADIO_SSID_MOST)
                return host_refuse("that network name is empty or too long\n");
        if (pass_length > RADIO_PASS_MOST)
                return host_refuse("that password is too long\n");
        if (pass_length && !wifi_pass_fits((p8 address_to)pass, pass_length))
                return host_refuse("a WPA password is 8 to 63 characters, "
                                   "or a key of 64 hex digits\n");
        if (!radio_text_shown(ssid) || (pass_length && !radio_text_shown(pass)))
                return host_refuse("that network name cannot be stored\n");

        /*      The radio lock from here, before the network is saved: the
                machine's own pass, finding it saved and nothing joined,
                joined it first while this scanned, and this then left that
                join to make its own. */
        lock = radio_lock(true);
        return_if(lock < 0, host_fail("wifi", lock));
        //      The broker and the station a join forks wipe their copies of
        //      the password (radio_secrets); this process forgets its own
        //      before the lock is let go.
        if (pass_length)
                radio_secret_hold((address_any)pass, pass_length);
        result = radio_wifi_enter(ssid, pass, wpa2);
        if (pass_length)
        {
                radio_secret_let_go((address_any)pass);
                crypto_forget((address_any)pass, pass_length);
        }
        radio_unlock(lock);
        return result;
}

/* Everything of `wifi remove` that happens with the radio lock held. */
static b32 radio_wifi_forget(string_address ssid)
{
        radio_network networks[RADIO_WIFI_MOST];
        bool cut;
        bipolar loaded = radio_wifi_read(networks, RADIO_WIFI_MOST, address_of cut);
        positive count = loaded > 0 ? (positive)loaded : 0;
        positive at;
        bool was_joined = false;
        bipolar failed;

        if (loaded < 0)
        {
                crypto_forget(networks, sizeof(networks));
                return host_fail(NET_WIFI_LIST, loaded);
        }
        at = radio_saved_index(networks, count, ssid, string_length(ssid));

        //      A list longer than the rows read would be written back without
        //      what was not read: twenty networks, one forgotten, and fifteen left.
        if (cut)
        {
                crypto_forget(networks, sizeof(networks));
                return host_refuse("%s is too long to change here\n", NET_WIFI_LIST);
        }
        if (at == count)
        {
                crypto_forget(networks, sizeof(networks));
                return host_refuse("no saved network is called %s\n", ssid);
        }

        //      What the last scan says, before the network is gone from the
        //      list the join would have read.
        {
                radio_air air;
                radio_heard address_to heard;

                if (radio_air_take(address_of air, RADIO_AIR_STALE | RADIO_AIR_JOINABLE) &&
                    (heard = radio_air_find(address_of air, ssid)))
                        was_joined = heard->joined;
        }

        //      Every row of the name goes: a list that holds it twice (made by
        //      hand, or damaged) would still have the network saved, and with
        //      it the password, after it was forgotten.
        for (; at < count; at = radio_saved_index(networks, count, ssid, string_length(ssid)))
        {
                for (positive move = at; move + 1 < count; move++)
                        memory_copy(address_of networks[move], address_of networks[move + 1],
                                    sizeof(networks[move]));
                count--;
                crypto_forget(address_of networks[count], sizeof(networks[count]));
        }

        //      Its rules first: a remove cut between the two leaves the
        //      network saved, and the next remove does both.
        failed = radio_rules_put((p8 address_to)ssid, string_length(ssid),
                                 WIFI_RULE_PMF | WIFI_RULE_SAE | WIFI_RULE_WPA2, 0);
        //      Nothing forgotten when its rules could not go: the network stays
        //      saved, with the rules it had, for the next remove to do both.
        if (failed < 0)
        {
                crypto_forget(networks, sizeof(networks));
                return host_fail(RADIO_RULES_PATH, failed);
        }
        failed = radio_wifi_save(networks, count);
        crypto_forget(networks, sizeof(networks));
        return_if(failed < 0, host_fail(NET_WIFI_LIST, failed));

        {
                radio_last last;

                if (radio_last_get(address_of last) &&
                    string_equals((string_address)last.ssid, ssid))
                        radio_last_set(ssid, 0);
        }

        if (was_joined)
                (void)radio_wifi_leave();
        radio_net_wake();

        host_say(log, host_label "wifi forgot %s%s\n", ssid,
                 was_joined ? " and left it" : "");
        return 0;
}

/*
        Forget a saved network. The list is rewritten without it, and the
        password with it. A network the machine is joined to when this runs
        is left too: it was saved so the machine would rejoin it, and once it
        is not saved nothing else would ever ask for the connection to end.
*/
static b32 radio_wifi_remove(string_address ssid)
{
        positive ssid_length = string_length(ssid);
        bipolar lock;
        b32 result;

        if (!ssid_length || ssid_length > RADIO_SSID_MOST)
                return host_refuse("that network name is empty or too long\n");

        lock = radio_lock(true);
        return_if(lock < 0, host_fail("wifi", lock));
        result = radio_wifi_forget(ssid);
        radio_unlock(lock);
        return result;
}

/*
        Wired, on and off, the way wifi has them.

        Off is remembered on /root, so it holds across a reboot, and does two
        things: the walk that picks a link to ask for a lease on no longer
        considers a wired one, and every wired link is taken down, so the
        lease it held is given up by the same carrier news a pulled cable
        sends and the machine moves to whatever else has carrier. On raises
        the links again and wakes the watcher to choose among them. Only a
        link with Ethernet framing that is neither loopback nor a wireless
        station is wired here.
*/
static b32 radio_wired_set(bool on)
{
        netlink_wired wired = {0};
        bipolar handle;
        bipolar failed;
        bipolar flagged = 0;
        positive links;

        if (host_save(NET_WIRED_POWER, NET_WIRED_POWER, on ? "on" : "off"))
                return 1;

        handle = netlink_open_groups(0);
        return_if(handle < 0, host_fail("wired", handle));

        failed = netlink_wired_list((b32)handle, address_of wired);
        if (failed >= 0)
                for (positive at = 0; at < wired.count; at++)
                        if (((wired.link[at].flags & IFF_UP) != 0) != on)
                        {
                                //      The first link the kernel would not move is said:
                                //      a link left up after "wired off" was said to be down.
                                bipolar moved = netlink_link_flag_up((b32)handle,
                                                                     wired.link[at].index, on);

                                if (moved < 0 && !flagged)
                                        flagged = moved;
                        }
        socket_close((b32)handle);
        radio_net_wake();
        links = wired.count;
        netlink_wired_forget(address_of wired);

        return_if(failed < 0, host_fail("wired", failed));
        return_if(flagged < 0, host_fail("wired", flagged));
        host_say(log, host_label "wired %s%s\n", on ? "on" : "off",
                 links ? "" : " (this machine has no wired link)");
        return 0;
}

static b32 radio_wired_status(void)
{
        netlink_wired wired = {0};
        bipolar handle = netlink_open_groups(0);
        bipolar failed = handle < 0 ? handle : netlink_wired_list((b32)handle, address_of wired);

        if (handle >= 0)
                socket_close((b32)handle);
        string_format(log, host_label "wired %s\n", net_wired_off() ? "off" : "on");
        if (failed >= 0)
                for (positive at = 0; at < wired.count; at++)
                        string_format(log, host_label "  %w: %s, %s\n",
                                      writer_terminal_name, wired.link[at].name,
                                      (wired.link[at].flags & IFF_UP) ? "up" : "down",
                                      (wired.link[at].flags & IFF_RUNNING) ? "carrier"
                                                                           : "no carrier");
        log_flush();
        netlink_wired_forget(address_of wired);
        return failed < 0 ? host_fail("wired", failed) : 0;
}

/*
        Bare wifi: the switch and the saved networks as they always were,
        then why wifi cannot be used, or what is in the air -- the saved and
        joined ones marked -- and why the machine is not joined when it is
        not. Scripts that read the first lines still find them first.
*/
static b32 radio_wifi_status(void)
{
        radio_network networks[RADIO_WIFI_MOST];
        positive count = radio_wifi_load(networks, RADIO_WIFI_MOST, null);
        positive at;
        bool off = radio_kept(NET_WIFI_POWER) == 0;
        bool joined = false;
        p8 why[RADIO_WHY_ROOM];
        radio_last last;
        radio_air air;

        string_format(log, host_label "wifi %s\n",
                      off ? (string_address) "off" : (string_address) "on");
        for (at = 0; at < count; at++)
        {
                p8 shown[RADIO_SSID_MOST * 4 + 1];

                radio_display(shown, sizeof(shown), networks[at].ssid,
                              networks[at].ssid_length);
                string_format(log, host_label "  %s\n", (string_address)shown);
        }

        if (radio_wifi_why(why, sizeof(why)) ||
            !radio_air_take(address_of air, RADIO_AIR_STALE))
        {
                host_say(log, host_label "wifi: %s\n",
                         why[0] ? (string_address)why
                                : (string_address) "no wireless interface");
                crypto_forget(networks, sizeof(networks));
                return 0;
        }

        if (!air.count)
                string_format(log, host_label "no networks heard%s\n",
                              bowl_is_root() ? (string_address) ""
                                             : (string_address) " (a new scan needs root)");
        else
        {
                positive width = 4;
                positive shown = air.count < RADIO_AIR_SHOWN ? air.count : RADIO_AIR_SHOWN;

                for (at = 0; at < shown; at++)
                {
                        p8 name[RADIO_SSID_MOST * 4 + 1];
                        positive columns = radio_display(name, sizeof(name),
                                                         air.heard[at].ssid,
                                                         air.heard[at].ssid_length);

                        if (columns > width)
                                width = columns > 28 ? 28 : columns;
                }
                for (at = 0; at < shown; at++)
                {
                        bool saved = radio_saved_index(networks, count, air.heard[at].ssid,
                                                       air.heard[at].ssid_length) < count;

                        radio_air_row(air.heard + at, width, saved);
                }
                if (air.count > shown)
                        string_format(log, host_label "  and %p more\n",
                                      air.count - shown);
        }

        for (at = 0; at < air.count; at++)
                joined |= air.heard[at].joined;
        if (!joined && count && !off)
        {
                radio_heard address_to heard = null;

                last.ssid[0] = end;
                last.failed = 0;
                if (radio_last_get(address_of last))
                        heard = radio_air_find(address_of air, last.ssid);
                p8 name[RADIO_SSID_MOST * 4 + 1];

                radio_display(name, sizeof(name), last.ssid, string_length(last.ssid));
                if (heard && !heard->joinable)
                {
                        radio_unjoinable(why, sizeof(why), heard->security);
                        string_format(log, host_label "not joined: %s asks for %s\n",
                                      (string_address)name, (string_address)why);
                }
                else if (heard)
                        string_format(log, host_label "not joined: %s %s\n",
                                      (string_address)name,
                                      radio_join_words(radio_heard_refused(heard, networks, count)
                                                           ?: last.failed));
                else
                {
                        bool near = false;
                        bool said = false;

                        //      No join has failed of late, or not of one in the
                        //      air: a saved network heard only from an access
                        //      point its plan refuses is said for what it is.
                        for (at = 0; at < count && !said; at++)
                        {
                                radio_heard address_to one =
                                    radio_air_find(address_of air, networks[at].ssid);
                                bipolar refused =
                                    one ? radio_heard_refused(one, networks, count) : 0;

                                near |= one != null;
                                if (refused)
                                {
                                        radio_display(name, sizeof(name), networks[at].ssid,
                                                      networks[at].ssid_length);
                                        string_format(log, host_label "not joined: %s %s\n",
                                                      (string_address)name,
                                                      radio_join_words(refused));
                                        said = true;
                                }
                        }
                        if (!near)
                                string_format(log, host_label "not joined: no saved "
                                                              "network is in range\n");
                }
        }

        host_say(log, host_label "* joined  + saved  "
                                 "moonwater wifi add SSID asks for its password\n");
        crypto_forget(networks, sizeof(networks));
        return 0;
}

/* The remembered word and the rfkill switch, set together. report is for
   the person who asked; restoring the machine's own choice stays quiet. */
static b32 radio_bluetooth_power(bool on, bool report)
{
        return radio_switch(NET_BLUETOOTH_POWER, RADIO_RFKILL_BLUETOOTH, on, report);
}

/* Remember a device's name or forget it. Nothing here pairs or connects: the
   list is the names a person keeps, shown by `moonwater bluetooth`, and a name
   added turns the radio on for whatever does pair. It is read, changed and
   written back whole under the lock the wifi verbs take, so two runs at once
   cannot lose each other's names. */
static b32 radio_bluetooth_edit(string_address identity, bool add)
{
        static p8 text[4096];
        static p8 kept[sizeof(text) + 1];
        positive length = string_length(identity);
        positive used = 0;
        positive at = 0;
        positive start;
        positive size;
        bipolar got;
        bipolar lock;
        bipolar failed = 0;
        bool found = false;
        bool off;

        if (!length || length > 128)
                return host_refuse("that bluetooth name is empty or too long\n");
        if (add && !radio_text_shown(identity))
                return host_refuse("that bluetooth name cannot be stored\n");

        lock = radio_lock(true);
        return_if(lock < 0, host_fail("bluetooth", lock));
        //      A list that is there and does not read is said, and not taken for
        //      an empty one: a remove would say no device was remembered, and an
        //      add would write the list back with the one name in it. Read before
        //      the word is pinned, so a refusal changes nothing.
        got = host_read_state(NET_BLUETOOTH_LIST, text, sizeof(text));
        if (got < 0 && got != -ENOENT)
        {
                radio_unlock(lock);
                return host_fail(NET_BLUETOOTH_LIST, got);
        }
        //      Asked, and pinned, under the lock and before the list is written: a remembered device is what makes a
        //      radio with no word on, and a bluetooth on that comes between would be written over.
        off = radio_kept(NET_BLUETOOTH_POWER) == 0;
        if (add && off)
        {
                failed = radio_write_word(NET_BLUETOOTH_POWER, "off");
                if (failed < 0)
                {
                        radio_unlock(lock);
                        return host_fail(NET_BLUETOOTH_POWER, failed);
                }
        }
        if (got < 0)
                got = 0;

        while (host_line_next(text, (positive)got, address_of at, address_of start,
                              address_of size))
        {
                if (size == length && !memory_compare(text + start, identity, length))
                {
                        found = true;
                        if (!add)
                                continue;
                }
                if (used + size + 1 >= sizeof(kept))
                {
                        failed = 4;
                        break;
                }
                memory_copy(kept + used, text + start, size);
                kept[used += size] = '\n';
                used++;
        }

        //      1: nothing by that name to forget, 2: no room for another,
        //      4: more of a list than was read, which would be written back
        //      short; or the list's own error when it would not be written.
        if (got >= (bipolar)sizeof(text) - 1)
                failed = 4;
        else if (!add && !found)
                failed = 1;
        else if (add && !found && used + length + 1 >= sizeof(kept))
                failed = 2;
        else if (add != found)
        {
                if (add)
                {
                        memory_copy(kept + used, identity, length);
                        kept[used += length] = '\n';
                        used++;
                }
                failed = host_write_file(NET_BLUETOOTH_LIST, kept, used, 0644, true);
        }
        radio_unlock(lock);

        if (failed == 1)
                return host_refuse("no remembered bluetooth device is called %s\n", identity);
        if (failed == 2)
                return host_refuse("too many saved bluetooth devices\n");
        if (failed == 4)
                return host_refuse("%s is too long to change here\n", NET_BLUETOOTH_LIST);
        return_if(failed, host_fail(NET_BLUETOOTH_LIST, failed));
        //      A name remembered is for a radio that is on to find it.
        failed = add && !off ? radio_bluetooth_power(true, true) : 0;
        host_say(log, host_label "bluetooth %s %s%s\n", add ? "remembered" : "forgot", identity,
                 add && off ? "; bluetooth is off, moonwater bluetooth on raises it" : "");
        return failed;
}

static b32 radio_bluetooth_status(void)
{
        static p8 text[4096];
        bipolar got = host_read_state(NET_BLUETOOTH_LIST, text, sizeof(text));
        bool off = radio_kept(NET_BLUETOOTH_POWER) == 0;
        positive at = 0;
        positive start;
        positive length;

        string_format(log, host_label "bluetooth %s\n",
                      off ? (string_address) "off" : (string_address) "on");
        while (got > 0 && host_line_next(text, (positive)got, address_of at,
                                         address_of start, address_of length))
                if (length)
                {
                        p8 shown[128 * 4 + 1];

                        radio_display(shown, sizeof(shown), text + start,
                                      length > 128 ? 128 : length);
                        string_format(log, host_label "  %s\n", (string_address)shown);
                }
        log_flush();
        return 0;
}

static string_address radio_internet_word(void)
{
        return net_internet_prefer() == NETLINK_PREFER_WIFI
                   ? (string_address) "wifi"
                   : (string_address) "wired";
}

/* The preference this session goes by, the copy of /root's that /run holds. */
static bipolar radio_internet_run(string_address which)
{
        p8 line[8];

        string_copy_bounded(line, which, sizeof(line));
        string_append_bounded(line, "\n", sizeof(line));
        host_state_ready();
        return host_publish(NET_INTERNET_RUN, line, string_length(line), 0644);
}

/* What is kept comes first: a /run that said wifi over a /root that kept
   wired was put back by radio_internet_copy a pass later. */
static b32 radio_internet_set(string_address which)
{
        bipolar failed;

        host_state_ready();
        if (host_save(NET_INTERNET_ROOT, NET_INTERNET_ROOT, which))
                return 1;
        failed = radio_internet_run(which);
        return_if(failed < 0, host_fail(NET_INTERNET_RUN, failed));
        radio_net_wake();

        host_say(log, host_label "internet prefers %s\n", which);
        return 0;
}

static b32 radio_internet_status(void)
{
        host_say(log, host_label "internet prefers %s\n",
                 radio_internet_word());
        return 0;
}

static fn radio_internet_copy(void)
{
        p8 text[16];
        p8 have[16];

        if (host_read_word(NET_INTERNET_ROOT, text, sizeof(text)) < 0)
                return;
        if (host_read_word(NET_INTERNET_RUN, have, sizeof(have)) >= 0 &&
            string_equals(have, text))
                return;

        if (radio_internet_run((string_address)text) >= 0)
                radio_net_wake();
}

static bool radio_wifi_wanted(bipolar power)
{
        radio_network networks[1];
        bool wanted;

        if (power >= 0)
                return power;
        wanted = radio_wifi_load(networks, 1, null) != 0;
        crypto_forget(networks, sizeof(networks));
        return wanted;
}

/*
        The join the machine loop forks, and only that, reaped once it ends:
        wait4 is asked for that pid alone. A wait on -1 would take every child
        of the machine process with it, the NTP query's status (on a pipe)
        and any job the machine script put in the background, whose wait
        then found no such child.
*/
static bipolar radio_child;

static fn radio_reap(void)
{
        positive status = 0;

        if (radio_child > 0 &&
            system_call_4(syscall(wait4), (positive)radio_child,
                          (positive)address_of status, 1, 0) != 0)
                radio_child = 0;
}

static fn radio_wifi_keep(void)
{
        bipolar lock;
        bipolar child;
        bool lost = false;

        //      A keeper holding its lock is a link the machine joined and has
        //      not lost: nothing to ask the kernel or walk sysfs for every
        //      pass while it stands.
        if (radio_child > 0 || host_lock_owner(RADIO_KEEPER_PATH))
                return;
        //      Blocked by somebody's `rfkill block wlan`, or by a switch on
        //      the machine, it is left so until `moonwater wifi on` says
        //      otherwise.
        if (radio_rfkill_blocked())
                return;
        /*      A saved network and no radio forked a join every pass that
                spent eight seconds finding no interface. The radio's arrival
                -- firmware put in place, a card rebound, a stick plugged in
                -- is what the next pass after it notices. */
        if (!radio_has_interface())
                return;
        if (nl80211_associated())
        {
                //      A link nobody here joined is nobody here's to keep. One
                //      whose keeper was started and is gone has lost its keys
                //      with it, and would die at the access point's next
                //      rekey: it is joined again.
                if (system_access_at(AT_FDCWD, RADIO_KEPT_PATH, 0) < 0)
                        return;
                lost = true;
        }
        else if (radio_join_waiting())
                return;

        lock = radio_lock(false);
        if (lock < 0)
                return;

        child = system_fork();
        if (child < 0)
        {
                radio_unlock(lock);
                return;
        }
        if (child)
        {
                system_close(lock);
                radio_child = child;
                return;
        }

        if (lost)
                radio_wifi_leave();
        radio_wifi_bring(false);
        radio_unlock(lock);
        system_call_1(syscall(exit), 0);
}

/* Whether the wifi is as a restore leaves it: a link a keeper holds, a join
   that failed a moment ago (the machine's pass asks again when it is time),
   or a radio left off and not linked. A boot restores twice, once when the
   disk is taken and once when the machine process starts, and the second
   finds it done, or finds it tried. */
static bool radio_wifi_restored(bipolar wifi)
{
        if (wifi == 0)
                return radio_rfkill_blocked() && !nl80211_associated();
        return host_lock_owner(RADIO_KEEPER_PATH) || radio_join_waiting();
}

static fn radio_restore_now(void)
{
        bipolar wifi = radio_kept(NET_WIFI_POWER);
        bipolar bluetooth = radio_kept(NET_BLUETOOTH_POWER);

        radio_internet_copy();

        if (!radio_wifi_restored(wifi))
        {
                if (wifi == 0)
                        radio_wifi_off(false);
                else if (radio_wifi_wanted(wifi))
                        radio_wifi_on(false);
        }

        if (bluetooth >= 0)
                radio_bluetooth_power(bluetooth, false);

        /*      The watcher took its first pass before /root was the kept
                disk, so it knew none of what was set there: a wired-only
                machine, a wired-off or wifi-first priority, a wifi that was
                already associated. Nothing above woke it in those cases, and
                its idle wait had grown to thirty seconds by the time bowl
                asked for a lease. */
        radio_net_wake();
}

/*
        The radios as they were kept. A take restores them where it stands
        (`moonwater boot`, which nothing waits behind but the console's
        shell). The machine process restores them in a child: a saved
        network's join is the radio lock taken, a scan and a handshake,
        seconds of it, and it stood in front of the machine script's
        moonwater_init and of every event the kernel had queued, canvas on
        first among them, so the desktop of a machine with a saved network
        started when its Wi-Fi had. The child is the loop's radio_child: the
        pass reaps it (radio_reap) and forks no join of its own while it
        runs (radio_wifi_keep), and a join that boot's own restore is still
        making is waited for by the child, on the lock, and not by the loop.
        What follows in host_restore_session does not wait for the radios:
        the device policy is in force sooner than it was, and the script's
        moonwater_init can no longer assume the saved network was tried.
        With no child to be had the restore is made here, as it was.
*/
static fn radio_restore(void)
{
        bipolar child = host_machine_self && radio_child <= 0 ? system_fork() : -1;

        if (child > 0)
        {
                radio_child = child;
                return;
        }
        radio_restore_now();
        if (!child)
                system_call_1(syscall(exit), 0);
}

static fn radio_recover(void)
{
        p8 verdict[HOST_NAME_ROOM + 16];

        radio_reap();
        if (host_read_word(HOST_VERDICT, verdict, sizeof(verdict)) >= 0 &&
            string_has_prefix(verdict, "ask "))
                return;

        radio_internet_copy();

        if (radio_wifi_wanted(radio_kept(NET_WIFI_POWER)))
                radio_wifi_keep();
}

/* What a verb says it did, when it did not fail. */
static b32 radio_done(b32 failed, string_address what)
{
        if (!failed)
                host_say(log, host_label "%s\n", what);
        return failed;
}

//      What is asked is judged before who asks: a word that is no switch is a
//      usage page for anyone, and root's refusal is for what would have been
//      done. host_main's table gives each verb its own function, as every
//      other verb has.
static b32 host_priority(string_address address_to arguments, positive count)
{
        string_address word = count > 2 ? arguments[2] : null;
        b32 answer;

        if (count == 2)
                return radio_internet_status();
        return_if(!string_equals(word, "internet"), host_usage());
        if (count == 3)
                return radio_internet_status();
        if (count != 4 || (!string_equals(arguments[3], "wired") &&
                           !string_equals(arguments[3], "wifi")))
                return host_usage();
        host_need_root();
        //      The word is kept in /root and copied to /run: two commands at once
        //      could leave the two files naming different links.
        if (!host_acquire())
                return 1;
        answer = radio_internet_set(arguments[3]);
        host_release();
        return answer;
}

static b32 host_wired(string_address address_to arguments, positive count)
{
        bipolar power = count == 3 ? host_onoff(arguments[2]) : -1;

        if (count < 3)
                return radio_wired_status();
        return_if(power < 0, host_usage());
        host_need_root();
        return radio_wired_set(power);
}

static b32 host_wifi(string_address address_to arguments, positive count)
{
        string_address word = count > 2 ? arguments[2] : null;
        bipolar power = count == 3 ? host_onoff(word) : -1;

        if (count < 3)
                return radio_wifi_status();
        if (power >= 0)
        {
                host_need_root();
                return power ? radio_wifi_on(true)
                             : radio_done(radio_wifi_off(true), "wifi off");
        }
        if (string_equals(word, "add") && count >= 4 && count <= 6)
        {
                p8 pass[256];
                positive length;
                //      wpa2 after the password, or after - : the network may
                //      be joined with WPA2 where the tight tier asks for WPA3.
                bool wpa2 = count == 6;

                return_if(wpa2 && !string_equals(arguments[5], (string_address) "wpa2"),
                          host_usage());
                //      A password in the argument is in ps and in the
                //      history of any shell that does not know this verb;
                //      the tight tier refuses the form, and anywhere it is
                //      taken a person at a terminal is told the other.
                return_if(MOONWATER_STRICT_NET >= STRICT_TIGHT && count >= 5 &&
                              !string_equals(arguments[4], (string_address) "-"),
                          host_refuse("a password on the command line is "
                                      "refused: give it on standard input "
                                      "with -\n"));
                host_need_root();
                scope_exit(crypto_forget(pass, sizeof(pass)));

                /*      No password: asked for at a terminal, open
                        anywhere else, as it always was. "-": one
                        line of standard input. Either keeps it out
                        of argv, where ps shows it to everybody. */
                if (count == 4 || string_equals(arguments[4], (string_address) "-"))
                {
                        if (radio_password_read(pass, sizeof(pass), arguments[3],
                                                count == 4, false) < 0)
                                return host_refuse("nothing saved\n");
                }
                else
                {
                        if (stream_is_terminal(2))
                                host_say(log_error, host_label "a password on the "
                                         "command line is seen by ps: - reads it "
                                         "without that\n");
                        length = string_length(arguments[4]);
                        if (length >= sizeof(pass))
                                return host_refuse("that password is too long\n");
                        memory_copy(pass, arguments[4], length + 1);
                        crypto_forget(arguments[4], length);
                }
                return radio_wifi_add(arguments[3], pass, wpa2);
        }
        if (string_equals(word, "remove") && count == 4)
        {
                host_need_root();
                return radio_wifi_remove(arguments[3]);
        }
        return host_usage();
}

/*
        Internet sharing over Bluetooth: the machine's connection to the internet, offered to the devices that pair with it as
        a PAN network access point (bt-network, on the bridge pan0, through the machine's BlueZ). Off until a person turns it
        on, and only `moonwater bluetooth share on` starts it: boot and every other verb read the kept word and never start
        it. The word is in /root; the pid of the process it started is under /run, so a boot takes that away, and the status
        says when a word is on and nothing runs.
*/
#define RADIO_SHARE_TOOL "/usr/bin/bt-network"
#define RADIO_SHARE_DAEMON "/usr/lib/bluetooth/bluetoothd"
#define RADIO_SHARE_DAEMON_OTHER "/usr/libexec/bluetooth/bluetoothd"
#define RADIO_SHARE_BRIDGE "/sys/class/net/" NET_BLUETOOTH_SHARE_BRIDGE
#define RADIO_SHARE_START_NS 1000000000ll

// Whether the kept word says on: none, or neither word, is off.
static bool radio_share_kept(void)
{
        p8 text[16];

        return host_read_word(NET_BLUETOOTH_SHARE, text, sizeof(text)) >= 0 && host_onoff((string_address)text) == 1;
}

// Whether pid is still bt-network. The file is not removed when bt-network
// ends on its own, so its pid may be another process's by the next look.
static bool radio_share_is(positive pid)
{
        p8 path[32] = "/proc/", text[32];
        if (!host_proc_path(path, sizeof(path), pid, "/comm") ||
            host_read_kernel((string_address)path, text, sizeof(text)) < 0)
                return false;
        return string_equals((string_address)text, "bt-network");
}

// The pid the sharing was started with, read from its file; 0 when none is kept or it is not bt-network.
static positive radio_share_pid(void)
{
        p8 text[24];
        positive pid = 0;

        if (host_read_word(NET_BLUETOOTH_SHARE_PID, text, sizeof(text)) < 0)
                return 0;
        pid = string_digits_max(text, 10, null);
        return pid > 1 && pid <= 0x7fffffff && radio_share_is(pid) ? pid : 0;
}

// Whether the process the sharing was started as is still there.
static bool radio_share_running(void)
{
        positive pid = radio_share_pid();

        return pid && system_call_2(syscall(kill), (positive)pid, 0) == 0;
}

// The sharing taken down: its process told to stop, and its pid let go.
static fn radio_share_stop(void)
{
        positive pid = radio_share_pid();

        if (pid)
                system_call_2(syscall(kill), (positive)pid, 15); // SIGTERM
        (void)system_remove_at(AT_FDCWD, NET_BLUETOOTH_SHARE_PID, 0);
}

// The first thing the image lacks for a sharing, named for the refusal; null when all of it is there.
static string_address radio_share_missing(void)
{
        static const struct { string_address path, alt, label; } needs[] = {
            {RADIO_SHARE_TOOL, null, "bt-network (the bluez-tools package)"},
            {RADIO_SHARE_DAEMON, RADIO_SHARE_DAEMON_OTHER, "bluetoothd (BlueZ)"},
            {RADIO_SHARE_BRIDGE, null, "the bridge " NET_BLUETOOTH_SHARE_BRIDGE},
        };

        for (positive at = 0; at < array_count(needs); at++)
                if (system_access_at(AT_FDCWD, needs[at].path, 0) < 0 &&
                    (!needs[at].alt || system_access_at(AT_FDCWD, needs[at].alt, 0) < 0))
                        return needs[at].label;
        return null;
}

// The process the sharing is: in the daemon host_detach made, its pid kept under /run, then bt-network on the bridge. Never returns.
static fn radio_share_run(void)
{
        string_address argv[] = {RADIO_SHARE_TOOL, "-s", "nap", NET_BLUETOOTH_SHARE_BRIDGE, null};
        p8 text[24];
        positive length;

        length = positive_into_string(text, (positive)system_call(syscall(getpid)));
        text[length++] = '\n';
        if (host_write_file(NET_BLUETOOTH_SHARE_PID, text, length, 0644, false) >= 0)
                (void)shell_exec_file(argv[0], argv, pointer_vector_count(argv), file_environment_all());
        system_call_1(syscall(exit), 127);
}

// The sharing started, and still there a second on: a process that ended at once says what it needs, bluetoothd most likely.
static b32 radio_share_start(void)
{
        (void)system_remove_at(AT_FDCWD, NET_BLUETOOTH_SHARE_PID, 0);
        if (host_detach(descriptors_none, descriptors_none))
                radio_share_run();
        host_pause(RADIO_SHARE_START_NS);
        if (radio_share_running())
                return 0;
        return host_refuse("bluetooth share: bt-network stopped at once, so bluetoothd is probably not running; "
                           "nothing is kept\n");
}

/*
        moonwater bluetooth share [on|off]: whether the machine shares its internet over Bluetooth (radio_share_*). Bare, it says
        the kept word, and whether the sharing runs when it is on. on checks the tools and the bridge, and that bluetooth is on,
        before anything is started or kept; off keeps the word and stops it.
*/
static b32 radio_share(string_address address_to arguments, positive count)
{
        string_address missing;

        return_if(count > 4, host_usage());
        if (count == 3)
        {
                bool kept = radio_share_kept();

                string_format(log, host_label "bluetooth share %s%s\n", kept ? "on" : "off",
                              kept && !radio_share_running()
                                  ? "; not running: moonwater bluetooth share on starts it" : "");
                return 0;
        }
        if (host_onoff(arguments[3]) < 0)
                return host_refuse("bluetooth share is on or off, not %s\n", arguments[3]);
        host_need_root();
        if (host_onoff(arguments[3]) == 0)
        {
                if (host_save("bluetooth share", NET_BLUETOOTH_SHARE, "off"))
                        return 1;
                radio_share_stop();
                host_say(log, host_label "bluetooth share off\n");
                return 0;
        }
        missing = radio_share_missing();
        if (missing)
                return host_refuse("bluetooth share needs %s, which this machine does not have: nothing is started or kept\n",
                                   missing);
        if (radio_kept(NET_BLUETOOTH_POWER) == 0)
                return host_refuse("bluetooth share needs bluetooth on: moonwater bluetooth on first; nothing is started or kept\n");
        if (!radio_share_running() && radio_share_start())
                return 1;
        if (host_save("bluetooth share", NET_BLUETOOTH_SHARE, "on"))
        {
                radio_share_stop();
                return 1;
        }
        host_say(log, host_label "bluetooth share on\n");
        return 0;
}

static b32 host_bluetooth(string_address address_to arguments, positive count)
{
        string_address word = count > 2 ? arguments[2] : null;
        bipolar power = count == 3 ? host_onoff(word) : -1;

        if (count < 3)
                return radio_bluetooth_status();
        if (string_equals(word, "share"))
                return radio_share(arguments, count);
        if (power >= 0)
        {
                host_need_root();
                return radio_done(radio_bluetooth_power(power, true),
                                  power ? "bluetooth on" : "bluetooth off");
        }
        if ((string_equals(word, "add") || string_equals(word, "remove")) && count == 4)
        {
                host_need_root();
                return radio_bluetooth_edit(arguments[3], string_equals(word, "add"));
        }
        return host_usage();
}

