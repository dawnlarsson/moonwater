/*
        src/sh/host/page.c: the verbs setup (install, update, update usb, rollback, use, live, removable), status,
        help, wipe, machine, boot and ask: the page of verbs and its rows, the sticks and an update from one, a
        rollback, a wipe.

        A run of src/sh/host.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. host_main, at the end
        of src/sh/host.c, names the function each verb runs.
*/

// The command ---------------------------------------------------

static fn host_title(writer out)
{
        string_format(out, TERM_BOLD "Moonwater" TERM_RESET "\n\n");
}

/*
        The pages, one row to a command as it is typed and what it does.

        The whole page, a wrong argument's page and `help VERB` are drawn from
        this one table, so the picture and the usage read as the same page. A row
        belongs to the verb its command starts with, or to every word of
        verbs where one row serves two. A row with no command is the line
        after the one above it. Most are on the main page and on their
        verb's own, 'b'; a verb with more to say than a line (setup, init,
        link) has a row for the main page, 'm', and the rows of its own, 'f'.
        A row 'r' is a verb's rows from the table of settings (registry.c),
        in its order, each one a row of both: @canvas is every setting row
        whose words start with canvas.
*/
typedef struct
{
        string_address verbs;
        string_address command;
        string_address text;
        p8 kind;
} host_row;

static const host_row host_rows[] = {
    {null, "status", "this session as one page", 'b'},
    {null, "version", "the commit this build was made from, its date and the release of the kernel", 'b'},
    {null, "help [VERB]", "these commands, or the page of one", 'b'},
    {null, "setup", "where it runs: install, update, use, live", 'm'},
    {null, "setup", "where this session runs, and the installs found", 'f'},
    {null, "setup install DISK [removable] [yes]", "erase DISK and put Moonwater on it", 'f'},
    {null, "", "removable takes a disk that says it is; yes", 'f'},
    {null, "", "is for a script, which cannot be asked", 'f'},
    {null, "setup update [DISK]", "write this build over an install, keeping its data", 'f'},
    {null, "setup update usb [DISK] [yes]", "write the build a stick carries over an install, keeping its data and the build it had", 'f'},
    {null, "setup rollback [DISK] [yes]", "start the build an install had before its last update again, keeping its data", 'f'},
    {null, "setup use [DISK]", "run this build with the data of an install", 'f'},
    {null, "setup live", "leave the disks alone this session", 'f'},
    {null, "install", "the drives this can be installed on, and which hold Moonwater", 'b'},
    {null, "install DRIVE [update|overwrite|use] [yes]", "update keeps an install's data and settings; use runs this", 'b'},
    {null, "", "build with them and writes nothing; overwrite", 'b'},
    {null, "", "erases DRIVE and installs anew; yes is for a script, which cannot be asked", 'b'},
    {null, "wipe [yes]", "forget /home and /root, keep the machine; yes", 'b'},
    {null, "", "is for a script, which cannot be asked", 'b'},
    {null, "rover load|status|state|watch", "the rules: /root/rules over the image's", 'b'},
    {null, "init [add|remove ...]", "what runs at boot", 'm'},
    {null, "exit [add|remove ...]", "what runs when the machine stops", 'm'},
    {null, "init", "what runs at every boot, with ids", 'f'},
    {null, "init add \"command\"", "run a command at boot, once disks settle", 'f'},
    {null, "init remove ID|\"command\"", "stop running one at boot", 'f'},
    {null, "@init", null, 'r'},
    {null, "exit", "what runs at poweroff or reboot", 'f'},
    {null, "exit add \"command\"", "run a command before the disks go read-only", 'f'},
    {null, "exit remove ID|\"command\"", "stop running one at the stop", 'f'},
    {null, "canvas", "whether Canvas is on, and the screens it draws on", 'b'},
    {null, "@canvas", null, 'r'},
    {null, "canvas arrange [NAME[@X,Y|@off]...|default]", "where each screen is: names left to right, the first the primary [default]", 'b'},
    {null, "canvas windows [forget]", "where windows open again, from where the hand put them; forget empties it", 'b'},
    {null, "canvas identify", "show the number of each screen for three seconds", 'b'},
    {null, "canvas wallpaper", "each Scene's wallpaper, and the pictures remembered", 'b'},
    {null, "canvas wallpaper edit [on|off]", "the editor over the desktop, of the Scene you are on (a solid colour on its colour wheel, the moon, the pictures): open, shut, or turned", 'b'},
    {null, "canvas wallpaper scene N CHOICE [SCREEN]", "Scene N's: auto, dark, white (the moon), colour:rrggbb (a solid colour) or a picture's name, on SCREEN or the one you are on", 'b'},
    {null, "canvas wallpaper picture FILE", "remember a picture (a PNG or a JPEG) and make it the wallpaper of the Scene you are on", 'b'},
    {null, "canvas wallpaper forget NAME", "remember a picture no more; a Scene that had it has the moon", 'b'},
    {null, "canvas wallpaper curated [fetch|get HEX]", "the feed's curated wallpapers: those kept, fetched and verified anew, or one's picture by its digest", 'b'},
    {null, "canvas record [SECONDS]", "record the presents of the compositor for SECONDS (1 to 10, 3 by default)", 'b'},
    {null, "canvas record report", "the report of the presents: the paths, flips, late and held presents", 'b'},
    {null, "canvas shot", "the screen as the compositor last composed it, to a file of the record (root)", 'b'},
    {null, "canvas film SECONDS [X Y W H]", "every present for SECONDS (1 to 60), of the rectangle or the whole screen, to a file of the record (root)", 'b'},
    {null, "canvas keys SECONDS", "every key for SECONDS (1 to 60) followed to the vblank that showed it, a line a key in the record's keys.txt (root)", 'b'},
    {null, "canvas frames SECONDS", "every window's frames, presents and vblanks for SECONDS (1 to 60), a line each in the record's frames.txt (root)", 'b'},
    {null, "desktop [on|off]", "the desktop, now and at every start; off leaves the text console [on]", 'b'},
    {null, "desktop start|rest", "begin the chosen desktop now; rest is the text console until the next start, the choice kept as it is", 'b'},
    {null, "@desktop", null, 'r'},
    {null, "desktop startup", "the programs and commands the desktop starts with it, in order, each on or off", 'b'},
    {null, "desktop startup add app ID COMMAND", "start a program of the start menu, by its id, with the desktop", 'b'},
    {null, "desktop startup add command COMMAND", "run a command line with the desktop, as typed", 'b'},
    {null, "desktop startup remove|on|off N", "take the Nth out, or start it or keep it but not start it", 'b'},
    {null, "desktop startup move N TO", "put the Nth in place TO", 'b'},
    {null, "desktop startup none", "start nothing with the desktop", 'b'},
    {null, "desktop autostart [PROGRAM...|none|default]", "the programs desktop startup began with: each an absolute path, in this order [none]", 'b'},
    {null, "desktop autostart add|remove PROGRAM", "one more of them, or one fewer", 'b'},
    {null, "keyboard start", "exit 0 when the desktop starts the on-screen keyboard: the setting on, or auto with a touch screen plugged in or no keyboard", 'b'},
    {null, "settings [gallery]", "the settings window, on the desktop of the compositor; gallery, every control of the UI kit", 'b'},
    {null, "window", "the windows on the desktop: name, pid, place, size, title", 'b'},
    {null, "window PLACE... COMMAND...", "run COMMAND, its first window put where PLACE says:", 'b'},
    {null, "", "center, at X,Y, maximized, fullscreen, screen N,", 'b'},
    {null, "", "size WxH, name NAME (what it is remembered by)", 'b'},
    {null, "window move PID|NAME PLACE...", "put the windows of a program, or of a name, where PLACE says", 'b'},
    {null, "clipboard [primary]", "what the clipboard holds and who copied it, never the text (root)", 'b'},
    {null, "clipboard get [primary]", "the copied text: exact to a file or a pipe, escaped to a terminal (root)", 'b'},
    {null, "clipboard set [primary] [TEXT]", "copy TEXT, or standard input (root)", 'b'},
    {null, "clipboard clear", "empty both selections and the history (root)", 'b'},
    {null, "clipboard watch [primary]", "a line each time something is copied, the text escaped (root)", 'b'},
    {null, "latency [reset]", "what the input handlers cost a report, and who has the display", 'b'},
    {null, "debug", "what the desktop is doing and why (root): the card, the screens, the windows and their scroll bodies, the pointer, the input devices and the wheel", 'b'},
    {null, "notices [clear]", "the errors and warnings of the kernel the desktop has (root): clear dismisses them", 'b'},
    {null, "sensors", "the processor use, clocks, GPU load, heat, fans and power of this machine, read now", 'b'},
    {null, "profile NAME|PID [SECONDS]", "where the processor time of a program goes: the share of each thread, its kernel part, and its functions", 'b'},
    {null, "memory [log [SECONDS]]", "installed, usable, available and shared memory and the largest memfds (root); log: to the kernel log every SECONDS [60]", 'b'},
    {null, "apps [list]", "the apps installed in the bowls, a line each: source, id, version, state, name, entry", 'b'},
    {null, "apps search TEXT [SOURCE]", "what the sources set up offer for TEXT: source, name, version, state, summary (root)", 'b'},
    {null, "apps install|remove SOURCE NAME", "an app or a package in, or out again: flatpak, arch, alpine, debian, fedora; its bowl set up first (root)", 'b'},
    {null, "apps update [SOURCE]", "every source brought up to date, or one (root)", 'b'},
    {null, "apps sources", "each source: ready, not-installed, not-set-up or unavailable, its bowl and the bowl's size in KiB", 'b'},
    {null, "apps source setup BOWL", "a bowl set up: arch, alpine, debian, fedora (root)", 'b'},
    {null, "apps source remove BOWL yes", "a whole bowl taken away, with every app, profile and launcher of it (root)", 'b'},
    {null, "open TARGET...", "each opened with what opens it: a page, a link of an app's scheme (discord:, vscode:), a file, a folder", 'b'},
    {null, "defaults", "the default apps: each kind (browser, mail, folders, text, images, video, music, documents) and what opens it", 'b'},
    {null, "defaults KIND|TYPE [APP|auto]", "every app that opens it, * the one that does; or the one that does from now (root)", 'b'},
    {null, "@airplane", null, 'r'},
    {null, "@brightness", null, 'r'},
    {null, "@power", null, 'r'},
    {null, "@cpu", null, 'r'},
    {null, "cpu online|offline N", "hotplug one processor", 'b'},
    {null, "@charge", null, 'r'},
    {null, "@battery", null, 'r'},
    {"sleep hibernate", "sleep | hibernate", "suspend to RAM or to disk", 'b'},
    {null, "bios [reboot]", "whether firmware setup is on offer; reboot goes", 'b'},
    {null, "splash [FILE|default|off]", "the picture the loader draws before the kernel starts, kept on the system partition: a binary PPM of at most 1 MiB, the built-in one, or off", 'b'},
    {null, "@wired", null, 'r'},
    {null, "@wifi", null, 'r'},
    {null, "wifi add SSID [PASSWORD|-] [wpa2]", "remember a network, joined once wifi is on; asks for", 'b'},
    {null, "", "the password, - reads it from stdin; wpa2 lets", 'b'},
    {null, "", "a tight tier join it without WPA3", 'b'},
    {null, "wifi remove SSID", "forget a saved network, and leave it", 'b'},
    {null, "@bluetooth", null, 'r'},
    {null, "bluetooth scan [SECONDS]", "what is near for SECONDS [10], pads first: each one's address, kind and name (root)", 'b'},
    {null, "bluetooth pair ADDRESS", "pair a pad, keyboard or mouse a scan heard, keep its keys and connect it; it comes back when it wakes (root)", 'b'},
    {null, "bluetooth forget ADDRESS", "unpair a device and drop its keys here and in the kernel (root)", 'b'},
    {null, "bluetooth add NAME", "keep the name of a device, found once the radio is on", 'b'},
    {null, "bluetooth remove NAME", "forget a bluetooth device", 'b'},
    {null, "bluetooth share [on|off]", "the internet of this machine over Bluetooth to the devices that pair with it; off until on (root)", 'b'},
    {null, "@device", null, 'r'},
    {null, "device [lock|unlock]", "lock keeps what is plugged in now, and holds a new device until it is allowed [unlock]", 'b'},
    {null, "device class NAME [allow|deny]", "what a new device no rule names may bring up: storage hid audio video net bluetooth hub other [deny]", 'b'},
    {null, "device list", "the devices the system sees, each allowed, blocked or waiting", 'b'},
    {null, "device open NODE BYTES TYPE", "mount a partition read-only at /run/media/USER, as Files' Open does", 'b'},
    {null, "device writable NODE", "let a partition opened be written to: ext4 btrfs xfs f2fs vfat exfat", 'b'},
    {null, "device eject NODE", "unmount a partition opened, and take its folder away", 'b'},
    {null, "device tidy", "let go of what was opened from a drive taken out", 'b'},
    {null, "device allow|deny ID|PORT", "let one device in, or hold it, now and at every plug;", 'b'},
    {null, "", "ID is VID:PID, or VID:PID/SERIAL as device shows it", 'b'},
    {null, "device forget ID|PORT", "drop the rule of one device", 'b'},
    {null, "device log [N]", "the last decisions, the newest last [20]", 'b'},
    {null, "device apply", "judge every device again now, as boot does", 'b'},
    {null, "@priority", null, 'r'},
    {null, "@time", null, 'r'},
    {null, "@timezone", null, 'r'},
    {null, "timezone auto", "from the network, one Cloudflare request", 'b'},
    {null, "", "per network joined [auto]", 'b'},
    {null, "@ntp", null, 'r'},
    {null, "@keyboard", null, 'r'},
    {null, "@mouse", null, 'r'},
    {null, "@taskbar", null, 'r'},
    {null, "audio", "the volume and the mute, the sound card and the cards there are, the programs that play, the microphone and the programs that record, and whether the sound server starts", 'b'},
    {null, "audio test", "play a second of 440 Hz through the sound server and say what became of it: no server, no card, a mixer that is off, muted, or played on this card", 'b'},
    {null, "@audio", null, 'r'},
    {null, "@name", null, 'r'},
    {null, "@language", null, 'r'},
    {null, "@dns", null, 'r'},
    {null, "network", "the network as one page: each interface with its kind and its addresses, the name servers in their order, and what discovery may look up", 'b'},
    {null, "@network", null, 'r'},
    {null, "security [trust|block|SWITCH ...]", "keys TLS trusts and refuses besides the built-in ones, and the switches of TLS and bowls", 'm'},
    {null, "security", "the built-in trust, and what was added to it or taken from it", 'f'},
    {null, "security trust", "every anchor, built in and added, by the SHA-256 of its key", 'f'},
    {null, "security trust add FILE", "trust a CA certificate, PEM or DER, as the built-in anchors are", 'f'},
    {null, "security trust remove SHA256", "stop trusting an anchor that was added", 'f'},
    {null, "security block", "every key refused, built in and added", 'f'},
    {null, "security block add SHA256|FILE", "refuse a key wherever it is: an anchor, a CA, a leaf, a CT log", 'f'},
    {null, "security block remove SHA256", "stop refusing a key that was added; a built-in one stays", 'f'},
    {null, "security tls12 [on|off]", "TLS 1.2 where a server offers nothing newer; off asks for 1.3 alone [on]", 'f'},
    {null, "security aia [on|off]", "fetch a CA a server left out, from where its certificate says [on]", 'f'},
    {null, "security revocation [soft|hard]", "hard refuses a certificate whose revocation cannot be told [soft]", 'f'},
    {null, "security ct [on|off]", "ask Certificate Transparency of every chain, not only at the tight tier [off]", 'f'},
    {null, "security http [on|off]", "plain http in wget and fetch, and redirects to it [on]", 'f'},
    {null, "security packages [on|off]", "programs of a bowl may run the package manager of its distribution [on]", 'f'},
    {null, "security sudo [on|off]", "moonwater through sudo or su in a bowl; on trusts root in a bowl as root of the machine, for single-user machines [off]", 'f'},
    {null, "workarounds [NAME|all [on|off]|app PROG NAME on|off|default]", "active app/Wine/Steam/Chrome compatibility workarounds; opt out globally or per executable", 'b'},
    {null, "link", "who this machine is linked with, and what each may do", 'f'},
    {null, "link [pair|NAME]", "machines linked by name, with a code", 'm'},
    {null, "link pair [NAME]", "a code, and wait for the other machine to use it", 'f'},
    {null, "link NAME CODE", "link to the machine called NAME, which waits; a machine known by NAME that comes with a new key is kept as NAME, and its old key goes", 'f'},
    {null, "link cancel", "end every wait for a code, so the pair or link NAME CODE that waits ends with no answer", 'f'},
    {null, "link NAME [COMMAND...]", "a terminal on NAME, or one command with its status", 'f'},
    {null, "link key", "the public key of this machine, which link add takes", 'f'},
    {null, "link push NAME FILE PATH", "a file here to PATH there, whole or not at all", 'f'},
    {null, "link pull NAME PATH FILE", "PATH there to a file here", 'f'},
    {null, "link log NAME", "follow the kernel log of NAME", 'f'},
    {null, "link pad NAME [DEVICE]", "this machine's game pad, the first or DEVICE (eventN), played on NAME as if plugged in there; NAME must allow input (pad, its old word, is taken too)", 'f'},
    {null, "link pad stop", "send no pad any more", 'f'},
    {null, "link on|off NAME", "switch the machine called NAME off, kept with its key and grants and no link to or from it, or on again", 'f'},
    {null, "link pad", "the pads sent from here and made here, and the delay of each", 'f'},
    {null, "link add NAME KEY [HOST[:PORT]]", "link by key, with no code", 'f'},
    {null, "link remove NAME", "stop knowing it", 'f'},
    {null, "link allow|deny NAME GRANT...", "shell run log files input, every one from the start (files is any file root has, as powerful as shell; input lets a game pad of NAME be made here, and pad is its old word)", 'f'},
    {null, "link group [NAME [SECRET|-] [allow GRANT...]]", "machines on one network that link themselves", 'f'},
    {null, "link group add NAME:KEY|- [allow GRANT...]", "a group by its key, - reading it from standard input: it is kept in the image, so a machine that starts from it is in the group (every grant unless allow says fewer)", 'f'},
    {null, "link group key [NAME]", "the key of a group as NAME:KEY, which anybody who has it can join with", 'f'},
    {null, "link group leave NAME [forget]", "stop, and forget the key of the group", 'f'},
    {null, "link group new [NAME] [allow GRANT...]", "a group of a new random key that images carry: this machine is in it and says its key; made again, the machines the old key paired are forgotten here", 'f'},
    {null, "link group stamp IMAGE [NAME|NAME:KEY|-] [allow GRANT...]", "a group put in an image file a stick is written from, one this machine is in or the key a line or standard input gives (no root then, and nothing of this machine's own): whoever holds a copy of it is in the group", 'f'},
    {null, "link on|off", "the listener, kept across boots [off]", 'f'},
    {null, "link doctor", "the listener, the udp port, the addresses, a route and discovery, each a pass or a fail with its fix; then how this machine is reached", 'f'},
    {null, "link paths", "the ways in this machine has (its udp port, discovery on the network, its public address) and where each machine linked was last heard", 'f'},
    {null, "link port [N|auto]", "the udp port it takes, and `link add` assumes [22348]", 'f'},
    {null, "link transport [network|modem]", "how linked machines reach each other: network, or the audio modem, which needs a sound card open on the sound server [network]", 'f'},
    {null, "link modem key", "the key of the audio modem, in hex, made on first use: the receiver needs the same", 'f'},
    {null, "link modem send FILE", "FILE over the sound card to the receiver on the Mac (tools/modem_rx.c); nothing is acknowledged", 'f'},
    {null, "@ssh", null, 'r'},
    {null, "ssh key remove LABEL", "take a key back by its label; every session ends", 'b'},
    {null, "ssh log [N]", "the last changes, starts, refusals and logins, the newest last [20]", 'b'},
};

/* The column the words of a row start in. A command that would not leave two
   blanks before it keeps them for the line below. */
#define HOST_COLUMN 36

static fn host_blank(writer out, positive count)
{
        static const p8 blanks[] = "                                        ";

        string_format(out, "%s", (string_address)blanks + sizeof(blanks) - 1 - count);
}

static fn host_row_write(writer out, const host_row address_to row)
{
        positive length = string_length(row->command);

        if (!length)
                host_blank(out, HOST_COLUMN);
        else
        {
                string_format(out, TERM_BOLD "  %s" TERM_RESET, row->command);
                if (length + 4 > HOST_COLUMN)
                {
                        string_format(out, "\n");
                        host_blank(out, HOST_COLUMN);
                }
                else
                        host_blank(out, HOST_COLUMN - 2 - length);
        }
        string_format(out, TERM_DIM "%s" TERM_RESET "\n", row->text);
}

/* Whether a row is one of verb's: the words it lists, else its command's first. */
static bool host_row_is(const host_row address_to row, string_address verb)
{
        string_address list = row->verbs ? row->verbs : row->command;
        positive length = string_length(verb);

        for (;;)
        {
                positive word = (positive)(string_first_of_or_end(list, ' ') - list);

                if (word == length && !string_compare_max(list, verb, length))
                        return true;
                if (!list[word] || !row->verbs)
                        return false;
                list += word + 1;
        }
}

/* The rows of the settings table whose words start with first: all of them, or those of verb when it is that word. */
static positive host_settings_rows_write(writer out, string_address first, string_address verb)
{
        positive written = 0, length = string_length(first);

        if (verb && string_compare(verb, first))
                return 0;
        for (positive at = 0; at < SETTINGS_ROWS; at++)
        {
                const struct settings_row address_to setting = &settings_rows[at];
                host_row row = {null, (string_address)setting->usage, (string_address)setting->help, 'b'};

                if (!setting->usage || string_compare_max((string_address)setting->usage, first, length) ||
                    (setting->usage[length] != ' ' && setting->usage[length]))
                        continue;
                host_row_write(out, &row);
                written++;
        }
        return written;
}

/*
        A verb's own page: its full rows where it has them, and what the main
        page shows of it where it has not. How many rows were written, so that
        a word that is no verb is told from one. The main page is
        host_page_write's.
*/
static positive host_rows_write(writer out, string_address verb)
{
        bool full = false;
        bool live = false;
        positive written = 0;

        for (positive at = 0; at < array_count(host_rows); at++)
                full |= host_rows[at].kind == 'f' && host_rows[at].command[0] &&
                        host_row_is(host_rows + at, verb);

        for (positive at = 0; at < array_count(host_rows); at++)
        {
                const host_row address_to row = host_rows + at;

                if (row->kind == 'r')
                {
                        written += host_settings_rows_write(out, row->command + 1, verb);
                        continue;
                }
                if (row->command[0])
                        live = host_row_is(row, verb);
                if (live && (full ? row->kind != 'm' : row->kind != 'f'))
                {
                        host_row_write(out, row);
                        written++;
                }
        }
        return written;
}

/*
        The main page's rows, in the sections of the settings window (registry.c):
        a section is the group of the first settings row whose verb a row's
        first word is, and All commands is the rows no settings row has. A
        settings row of a marker is in its own group. Each block is a row and
        the lines under it, which go with it. A row of a verb's full page ('f')
        is not on the main page.
*/
#define HOST_LINES (array_count(host_rows) + SETTINGS_ROWS)

typedef struct
{
        positive section; // the settings row that names the section, or SETTINGS_ROWS for All commands
        positive first;   // the block's first line
        positive count;   // and how many lines it has
} host_block;

// The settings row that names the group at is in: the last row at or before at that has a section.
static positive host_group_of(positive at)
{
        while (!settings_rows[at].section)
                at--;
        return at;
}

// Whether word is the first word of command.
static bool host_word_first(string_address command, string_address word)
{
        positive length = string_length(word);

        return !string_compare_max(command, word, length) && (command[length] == ' ' || !command[length]);
}

// The section of a command: the group of the first settings row whose verb it starts with, All commands when none does.
static positive host_section_of(string_address command)
{
        for (positive at = 0; at < SETTINGS_ROWS; at++)
                if (settings_rows[at].read[0] && host_word_first(command, (string_address)settings_rows[at].read[0]))
                        return host_group_of(at);
        return (positive)SETTINGS_ROWS;
}

// Whether command comes before other in the alphabet, a capital as its small letter.
static bool host_command_before(string_address command, string_address other)
{
        return string_compare_folded(command, other) < 0;
}

// Whether block comes before other: by section, then by the command of its row.
static bool host_block_before(const host_block address_to block, const host_block address_to other, const host_row address_to lines)
{
        if (block->section != other->section)
                return block->section < other->section;
        return host_command_before(lines[block->first].command, lines[other->first].command);
}

// The lines and blocks of the main page, in table order; the count of blocks. Each settings row a marker names is a block of its own.
static positive host_page_collect(host_row address_to lines, host_block address_to blocks)
{
        positive rows = 0, count = 0;
        bool shown = false;

        for (positive at = 0; at < array_count(host_rows); at++)
        {
                const host_row address_to row = host_rows + at;

                if (row->kind == 'r')
                {
                        shown = false;
                        for (positive set = 0; set < SETTINGS_ROWS; set++)
                        {
                                const struct settings_row address_to setting = &settings_rows[set];
                                host_row line = {null, (string_address)setting->usage, (string_address)setting->help, 'b'};

                                if (!setting->usage || !host_word_first((string_address)setting->usage, (string_address)row->command + 1))
                                        continue;
                                blocks[count].section = host_group_of(set);
                                blocks[count].first = rows;
                                blocks[count].count = 1;
                                lines[rows++] = line;
                                count++;
                        }
                        continue;
                }
                if (row->kind == 'f')
                {
                        shown = false;
                        continue;
                }
                if (row->command[0])
                {
                        blocks[count].section = host_section_of(row->command);
                        blocks[count].first = rows;
                        blocks[count].count = 0;
                        count++;
                        shown = true;
                }
                if (shown)
                {
                        lines[rows++] = *row;
                        blocks[count - 1].count++;
                }
        }
        return count;
}

// The main page's rows, each section named and sorted by its command, two blank lines before each section after the first.
static fn host_page_rows_write(writer out)
{
        static host_row lines[HOST_LINES];
        static host_block blocks[HOST_LINES];
        positive count = host_page_collect(lines, blocks);

        for (positive at = 1; at < count; at++)
        {
                host_block key = blocks[at];
                positive back = at;

                while (back && host_block_before(&key, &blocks[back - 1], lines))
                {
                        blocks[back] = blocks[back - 1];
                        back--;
                }
                blocks[back] = key;
        }
        for (positive at = 0; at < count; at++)
        {
                const host_block address_to block = blocks + at;

                if (!at || block->section != blocks[at - 1].section)
                {
                        if (at)
                                string_format(out, "\n\n");
                        string_format(out, TERM_BOLD "%s" TERM_RESET "\n",
                                      block->section < SETTINGS_ROWS ? (string_address)settings_rows[block->section].section
                                                                     : (string_address)"All commands");
                }
                for (positive each = 0; each < block->count; each++)
                        host_row_write(out, lines + block->first + each);
        }
}

static fn host_page_write(writer out)
{
        host_page_rows_write(out);
        host_say(out,
                 "\n"
                 TERM_DIM "    Settings stay in the image this session started from.\n"
                 "    install takes the settings of this session; update keeps those of the disk.\n"
                 "    The rules are /root/rules/NAME.rover over the image's (moonwater rover).\n" TERM_RESET);
}

/* A wrong argument's page: the verb's rows, or the whole of it for a word that is none. */
static b32 host_usage(void)
{
        if (!host_verb || !host_rows_write(log_error, host_verb))
        {
                host_title(log_error);
                host_page_write(log_error);
        }
        log_flush();
        return 2;
}

/*
        The wifi line of status, from what the kernel already holds: status
        never scans. Joined and where, why wifi cannot be used, or what the
        last join said.
*/
static fn host_status_wifi(void)
{
        p8 why[RADIO_WHY_ROOM];
        radio_last last;
        p8 name[RADIO_SSID_MOST * 4 + 1];
        radio_air air;

        if (radio_wifi_why(why, sizeof(why)))
        {
                string_format(log, "  wifi: %s\n", why);
                return;
        }
        if (radio_air_take(address_of air, RADIO_AIR_CACHED))
                for (positive at = 0; at < air.count; at++)
                        if (air.heard[at].joined)
                        {
                                radio_display(name, sizeof(name), air.heard[at].ssid,
                                              air.heard[at].ssid_length);
                                string_format(log, "  wifi joined %s\n", name);
                                return;
                        }
        if (radio_last_get(address_of last))
        {
                radio_display(name, sizeof(name), last.ssid, string_length(last.ssid));
                string_format(log, "  wifi on, not joined: %s %s\n", name,
                              radio_join_words(last.failed));
                return;
        }
        string_format(log, "  wifi on\n");
}

// Sticks --------------------------------------------------------

/*
        A build on a stick, looked at without starting it.

        A stick written by build.sh --usb carries the image where firmware
        starts it (HOST_IMAGE), and one written by build.sh --usb-update, or
        by hand, carries it as HOST_UPDATE_FILE on a FAT stick that may hold
        anything else besides. Every FAT partition of a disk something can be
        plugged into (host_disk_external) is looked at, read-only, unless it
        is an install's own system partition or on the disk an update writes
        to: the update file first, then the loader. What a stick carries is
        judged by its bytes (host_image_fit), never by its name.

        The device policy decides first: a stick on a USB device the policy
        holds is not mounted. A held device has no disk to begin with, since
        nothing binds to it; this is for the moments the kernel and the
        policy disagree, at a boot before the first restore or with the
        policy changed and not yet applied.
*/
typedef struct
{
        p8 name[HOST_NAME_ROOM]; // the partition
        p8 disk[HOST_NAME_ROOM]; // and its disk
        string_address file;     // where on it the image is, or none
        p8 build[HOST_BUILD_ROOM];
        b32 fit; // host_image_fit's answer
        p16 machine;
        bool held; // the device policy holds the device it is on
} host_stick;

typedef struct
{
        host_stick found[HOST_STICKS];
        positive count;
        positive dropped;
        string_address skip; // a disk not looked at
} host_sticks;

/*
        Whether the device policy lets a disk be read. A disk on USB is
        below the devices its sysfs place names -- every hub between it and
        its root hub, and the stick itself (1-4, 1-4.2) -- and the interface
        its driver is bound to (1-4.2:1.0). Each device must be authorized
        and let in by the policy (usb_judge, which boot, every verb and the
        watcher judge by), and so must the interface (usb_interface_let). A
        device on the path the census does not show is held: what cannot be
        judged is not read. A disk on no USB bus is not the device policy's.
*/
static bool host_stick_let(string_address disk)
{
        static usb_census census;
        static usb_policy policy;
        p8 sysfs[HOST_PATH_ROOM];
        p8 link[HOST_PATH_ROOM * 2];
        p8 word[USB_PORT_ROOM];
        usb_device address_to device = null;
        string_address at;
        bool loaded = false;

        if (!host_join(sysfs, sizeof(sysfs), "/sys/class/block/", disk) ||
            file_link_text(sysfs, link, sizeof(link)) <= 0)
                return false;

        for (at = (string_address)link; *at;)
        {
                positive length = (positive)(string_first_of_or_end(at, '/') - at);
                bool port;

                if (length && length < sizeof(word))
                {
                        memory_copy(word, at, length);
                        word[length] = end;
                        port = usb_port_valid((string_address)word);
                        if ((port || (device && string_first_of((string_address)word, ':'))) &&
                            !loaded)
                        {
                                usb_census_take(address_of census);
                                usb_policy_load(address_of policy);
                                loaded = true;
                        }
                        if (port)
                        {
                                device = usb_find(address_of census, (string_address)word);
                                if (!device || !device->authorized ||
                                    !usb_let(usb_judge(address_of policy, device)))
                                        return false;
                        }
                        else if (device && string_first_of((string_address)word, ':'))
                        {
                                bool found = false;

                                for (positive one = 0; one < census.interfaces; one++)
                                        if (string_equals((string_address)census.interface[one].name,
                                                          (string_address)word))
                                        {
                                                usb_interface address_to it = census.interface + one;

                                                if (!usb_owns(device, it) || !it->authorized ||
                                                    !usb_interface_let(address_of policy, device,
                                                                       usb_judge(address_of policy,
                                                                                 device),
                                                                       it))
                                                        return false;
                                                found = true;
                                        }
                                if (!found)
                                        return false;
                                //      Below the interface is the disk's own:
                                //      its SCSI host, target and block device.
                                return true;
                        }
                }
                at += length + (at[length] == '/');
        }

        //      A USB device with no interface on the path is not one a disk is on.
        return !device;
}

/* What the stick mounted at where carries, into stick: the update file, else
   the loader; nothing when it carries neither. */
static fn host_stick_look(host_stick address_to stick, string_address where)
{
        static const string_address files[] = {HOST_UPDATE_FILE, HOST_IMAGE};
        p8 path[HOST_PATH_ROOM];

        stick->file = null;
        stick->fit = HOST_FIT_NONE;
        for (positive at = 0; at < array_count(files); at++)
                if (host_join(path, sizeof(path), where, files[at]) &&
                    system_access_at(AT_FDCWD, path, 0) >= 0)
                {
                        stick->file = files[at];
                        stick->fit = host_image_fit(path, stick->build, sizeof(stick->build),
                                                    address_of stick->machine);
                        return;
                }
}

static bool host_sticks_visit(storage_identity address_to identity, address_any opaque)
{
        host_sticks address_to sticks = (host_sticks address_to)opaque;
        string_address name = identity->path + sizeof("/dev/") - 1;
        p8 parent[HOST_NAME_ROOM];
        host_stick address_to stick;

        if (!string_equals(identity->type, "vfat") ||
            string_equals(identity->partlabel, HOST_SYSTEM_NAME) || !host_name_valid(name) ||
            !host_parent(name, parent, sizeof(parent)) ||
            (sticks->skip && string_equals(parent, sticks->skip)) ||
            !host_disk_external(parent))
                return true;

        if (sticks->count == HOST_STICKS)
        {
                sticks->dropped++;
                return true;
        }

        stick = sticks->found + sticks->count;
        memory_zero(stick, sizeof(address_to stick));
        string_copy_bounded(stick->name, name, sizeof(stick->name));
        string_copy_bounded(stick->disk, parent, sizeof(stick->disk));
        stick->fit = HOST_FIT_NONE;

        if (!host_stick_let(parent))
                stick->held = true;
        else if (host_mount(name, HOST_STICK, "vfat", HOST_READ_ONLY) >= 0)
        {
                host_stick_look(stick, HOST_STICK);
                host_unmount(HOST_STICK);
        }

        //      A FAT stick with no Moonwater on it is nobody's business here.
        if (stick->held || stick->file)
                sticks->count++;
        return true;
}

/* Every stick plugged in that carries an image, or that the device policy
   holds, but those on skip. */
static fn host_sticks_take(host_sticks address_to sticks, string_address skip)
{
        memory_zero(sticks, sizeof(address_to sticks));
        sticks->skip = skip;
        storage_each_device(host_sticks_visit, sticks);
}

/* Which of two builds a stick's is, in the words a line about it starts with. */
static string_address host_build_named(string_address build, string_address other)
{
        b32 order = host_build_order(build, other);

        return order == 0    ? "the same build"
               : order == 1  ? "a newer build"
               : order == -1 ? "an older build"
                             : "another build";
}

/*
        The sticks, as setup says them under the installs: the build each
        carries beside running, an image refused and why, and a stick the
        device policy holds, once for the disk. shown is the partition
        already said to carry this image, which is not said twice. With one
        install, and a stick whose build is neither running nor the one the
        install has, the command that writes it.
*/
static fn host_sticks_said(string_address running, string_address shown,
                           host_census address_to census)
{
        static host_sticks sticks;
        host_install address_to install = census->count == 1 ? census->found : null;
        bool other = false;

        host_sticks_take(address_of sticks, null);
        for (positive at = 0; at < sticks.count; at++)
        {
                host_stick address_to stick = sticks.found + at;
                bool said = false;

                for (positive before = 0; before < at; before++)
                        said |= sticks.found[before].held &&
                                string_equals(sticks.found[before].disk, stick->disk);

                if (stick->held)
                {
                        if (!said)
                                string_format(log, "  %s is held by the device policy, so what "
                                                   "it carries is not read\n", stick->disk);
                        continue;
                }
                if (stick->fit)
                {
                        string_format(log, "  %s carries an image this machine cannot start: %s\n",
                                      stick->name, host_fit_said(stick->fit, stick->machine));
                        continue;
                }
                if (string_equals(stick->build, running))
                {
                        if (!shown || !string_equals(stick->name, shown))
                                string_format(log, "  the same build on %s\n", stick->name);
                        continue;
                }
                string_format(log, "  %s on %s: %s\n",
                              host_build_named(stick->build, running), stick->name,
                              stick->build);
                other |= install && (!install->readable ||
                                     !string_equals(stick->build, install->build));
        }

        if (other)
                string_format(log, "  moonwater setup update usb writes it over %s, keeping "
                                   "its data\n", install->disk);
}

/*
        What a stick says its update's bytes are, where it says it: beside
        the image, its name and .sha256, a line whose first word is the
        image's SHA-256 in hex, as sha256sum writes it. It is the stick's
        word about its own bytes, so it catches a copy that went wrong and
        not a stick that was made to lie (anyone who writes the image writes
        this too). 0 when the image is what it says or it says nothing; 1,
        said, when it says another or says what is not a sum.
*/
static b32 host_stick_digest(string_address image, bool address_to said)
{
        p8 path[HOST_PATH_ROOM];
        p8 text[160];
        p8 want[32];
        p8 sum[32];
        bipolar got;

        *said = false;
        if (!host_join(path, sizeof(path), image, HOST_DIGEST_SUFFIX))
                return host_refuse("%s: path too long\n", image);

        got = file_slurp_regular_at(AT_FDCWD, path, text, sizeof(text), O_NOFOLLOW);
        if (got == -ERROR_NO_ENTRY)
                return 0;
        if (got < 64 || !memory_from_hex(want, (string_address)text, 32) ||
            (got > 64 && text[64] != ' ' && text[64] != '\n' && text[64] != '\t'))
                return host_refuse("%s says nothing that is a SHA-256, so the image beside it "
                                   "is not written\n", path);

        if (!host_image_sum(image, false, sum))
                return host_refuse("%s could not be read to its end\n", image);

        if (memory_compare(sum, want, sizeof(sum)))
                return host_refuse("%s is not the image %s says it is: the copy on the "
                                   "stick is damaged, or is not the one the sum was made of; "
                                   "nothing written\n", image, path);

        *said = true;
        return 0;
}

/*
        The install a command that writes its system partition names, or
        else the one this session keeps, the one boot asked about, or the
        only one there is. A session that keeps one disk can still have
        another's image written: only the system partition is touched.
*/
static host_install address_to host_install_chosen(host_census address_to census,
                                                   string_address disk)
{
        p8 verdict[HOST_NAME_ROOM + 16];
        host_install address_to install = null;

        host_read_word(HOST_VERDICT, verdict, sizeof(verdict));
        host_census_take(census);

        if (disk)
                install = host_census_find(census, disk);
        else if (string_has_prefix(verdict, "disk "))
                install = host_census_find(census, verdict + 5);
        else if (string_has_prefix(verdict, "ask "))
                install = host_census_find(census, verdict + 4);
        //      The disk a live session could not keep at boot (host_settle_kept_away).
        else if (string_has_prefix(verdict, "live ") && host_census_find(census, verdict + 5))
                install = host_census_find(census, verdict + 5);
        else if (census->count == 1)
                install = census->found;

        if (!install)
                host_refuse(disk               ? "%s has no Moonwater install\n"
                            : census->count > 1 ? "more than one disk has Moonwater; name one%s\n"
                                                : "no disk here has Moonwater installed%s\n",
                            disk ? disk : (string_address) "");
        return install;
}

/* A question a person answers yes to before an image is written, at a
   terminal; whoever said yes in the command's words is not asked. */
static bool host_said_yes(bool said_yes, string_address question)
{
        p8 answer[8];

        if (said_yes)
                return true;

        host_say(log, host_label "%s Type " HOST_YES " to go on: ", question);
        if (host_read_line(answer, sizeof(answer)) < 0 || !string_equals(answer, HOST_YES))
        {
                host_say(log, host_label "nothing written\n");
                return false;
        }
        return true;
}

/*
        The one stick whose build an update writes over install: an image
        this machine starts, of a build the install does not have. None, or
        more than one, is refused in words that say why, so a person who
        plugged in the wrong stick, a stick the policy holds or one that
        carries the build already there is told which.
*/
static host_stick address_to host_stick_chosen(host_sticks address_to sticks,
                                               host_install address_to install)
{
        host_stick address_to chosen = null;
        host_stick address_to same = null;
        host_stick address_to held = null;
        host_stick address_to refused = null;

        for (positive at = 0; at < sticks->count; at++)
        {
                host_stick address_to stick = sticks->found + at;

                if (stick->held)
                        held = held ? held : stick;
                else if (stick->fit)
                        refused = refused ? refused : stick;
                else if (install->readable && string_equals(stick->build, install->build))
                        same = same ? same : stick;
                else
                {
                        if (chosen)
                        {
                                host_refuse("%s and %s both carry a build to write: leave the one "
                                            "to write plugged in\n", chosen->name, stick->name);
                                return null;
                        }
                        chosen = stick;
                }
        }

        if (chosen)
                return chosen;

        if (refused)
                host_refuse("%s carries an image this machine cannot start, so nothing was "
                            "written: %s\n", refused->name,
                            host_fit_said(refused->fit, refused->machine));
        else if (same)
                host_refuse("%s carries the build %s already has: %s\n", same->name,
                            install->disk, same->build);
        else if (held)
                host_refuse("%s is held by the device policy, so what it carries is not "
                            "read: moonwater device list shows it, and device allow lets it "
                            "in\n", held->disk);
        else
                host_refuse("no stick plugged in carries a Moonwater image: build.sh "
                            "--usb-update writes one, or copy dist/%s to %s on any FAT "
                            "stick\n", HOST_UPDATE_FILE + sizeof("/moonwater/") - 1,
                            HOST_UPDATE_FILE);
        return null;
}

/*
        The build a stick carries, written over an install and kept as the
        one it starts, without the stick being started: the update's own
        steps (host_place_image) with the stick's image as the source. It is
        judged three times, by its bytes each time: when the sticks are
        looked at, once mounted for the write, and as the copy on the
        install; it is the digest beside it, when the stick has one; and it
        is read back from the install's disk, unmounted, as the bytes it was
        copied from. The build the install had is kept as previous.efi, which
        setup rollback starts again.
*/
static b32 host_update_stick_locked(host_install address_to install, bool said_yes)
{
        static host_sticks sticks;
        host_stick address_to stick;
        host_settings disk;
        p8 source[HOST_PATH_ROOM];
        p8 build[HOST_BUILD_ROOM];
        p8 question[HOST_BUILD_ROOM * 2 + 128];
        p16 machine;
        bool carried;
        bool lost;
        bool unkept;
        bool summed = false;
        bipolar mounted;
        b32 fit;
        b32 failed;

        if (!host_install_refresh(install))
                return host_refuse("%s is no longer here\n", install->disk);
        host_install_read(install);

        host_sticks_take(address_of sticks, install->disk);
        stick = host_stick_chosen(address_of sticks, install);
        if (!stick)
                return 1;

        //      Looked at again where it is to be read from: the stick can have
        //      been changed, or another put in its place, since. And once more
        //      after the question, which is asked with nothing mounted: a
        //      command ended at it (Ctrl-C) left the stick mounted for the
        //      rest of the session, and the stick can have been changed while
        //      the answer was waited for.
        for (positive look = 0; look < 2; look++)
        {
                if (!host_stick_let(stick->disk))
                        return host_refuse("%s is held by the device policy, so it is not read\n",
                                           stick->disk);
                mounted = host_mount(stick->name, HOST_MEDIUM, "vfat", HOST_READ_ONLY);
                if (mounted < 0)
                        return host_fail(stick->name, mounted);

                failed = 0;
                summed = false;
                fit = host_join(source, sizeof(source), HOST_MEDIUM, stick->file)
                          ? host_image_fit(source, build, sizeof(build), address_of machine)
                          : HOST_FIT_NONE;
                if (fit)
                        failed = host_refuse("%s carries an image this machine cannot start, so "
                                             "nothing was written: %s\n", stick->name,
                                             host_fit_said(fit, machine));
                else if (!string_equals(build, stick->build))
                        failed = host_refuse("what %s carries changed while it was looked at; "
                                             "nothing written\n", stick->name);
                else
                        failed = host_stick_digest(source, address_of summed);

                if (failed)
                {
                        host_unmount(HOST_MEDIUM);
                        return 1;
                }
                if (look || said_yes)
                        break;

                host_unmount(HOST_MEDIUM);
                host_say(log, host_label "%s has %s\n", install->disk,
                         install->readable ? (string_address)install->build
                                           : (string_address) "an image whose build cannot be read");
                host_say(log, host_label "%s carries %s: %s%s\n", stick->name,
                         install->readable ? host_build_named(build, install->build)
                                           : (string_address) "a build",
                         build, summed ? ", and its bytes are the sum beside it" : "");

                //      An image whose build cannot be read may not read at all, and
                //      one that does not is not kept (host_place_keep_copy).
                (void)string_join(question, sizeof(question), "Writing it over ", install->disk,
                                  install->readable
                                      ? " keeps the data there, and the build it has as the one "
                                        "to go back to."
                                      : " keeps the data there, and the image it has as the one "
                                        "to go back to if that image can be read.");
                if (!host_said_yes(false, (string_address)question))
                        return 1;
        }
        if (said_yes)
        {
                host_say(log, host_label "%s has %s\n", install->disk,
                         install->readable ? (string_address)install->build
                                           : (string_address) "an image whose build cannot be read");
                host_say(log, host_label "%s carries %s: %s%s\n", stick->name,
                         install->readable ? host_build_named(build, install->build)
                                           : (string_address) "a build",
                         build, summed ? ", and its bytes are the sum beside it" : "");
        }

        //      The install as it was asked about, and no other (host_system_mount).
        if (host_system_mount(install, true))
        {
                host_unmount(HOST_MEDIUM);
                return 1;
        }

        host_say(log, host_label "writing the build on %s to %s\n", stick->name,
                 install->system);

        positive splash = 0;

        failed = host_settings_carried(install, address_of disk, address_of lost);
        if (!failed)
                host_splash_ensure(HOST_MEDIUM, HOST_SYSTEM, install->disk, address_of splash);
        if (!failed)
                failed = host_place_image(install->system, HOST_SYSTEM, source, build,
                                          address_of disk, address_of carried, address_of unkept);
        if (!failed)
        {
                p8 whose[HOST_NAME_ROOM + 24];

                (void)string_join(whose, sizeof(whose), "the image on ", stick->name);
                host_settings_landed(install, address_of disk, carried, lost,
                                     (string_address)whose);
                host_say(log, unkept ? host_label "%s starts %s from its next start, keeping its "
                                                  "data; the image it had could not be read and "
                                                  "is not kept, so moonwater setup rollback "
                                                  "starts what " HOST_KEPT_IMAGE " already had. "
                                                  "reboot starts it now\n"
                                     : host_label "%s starts %s from its next start, keeping its "
                                                  "data; moonwater setup rollback starts the build "
                                                  "it had again. reboot starts it now\n",
                         install->disk, build);
        }

        host_unmount(HOST_SYSTEM);
        host_unmount(HOST_MEDIUM);
        if (!failed)
                host_splash_installed(install->system, splash);
        return failed;
}

/*
        previous.efi made the image that starts and the one that started
        kept as previous.efi in its place: back is the build previous.efi
        reads as, and carry is stamped into it. The partition is left
        unmounted. What the steps found is left in place for the caller:
        whether the slots were carried, whether the loader could be kept, and
        whether it was stranded, which it can be when this fails, since the
        last rename of a rollback is after its switch.
*/
static b32 host_back_image(string_address name, string_address system, string_address back,
                           host_settings address_to carry, host_place address_to place)
{
        if (!host_place_open(place, system, null, back, carry))
                return host_refuse("%s: path too long\n", system);
        place->source = place->kept;

        if (host_place_written(place, true))
                return 1;

        if (!host_on_disk(name, system, host_back_landed, place))
                return host_refuse("%s did not read back from the disk as the images that "
                                   "were written\n", name);

        return 0;
}

/*
        The build an install had before its last update, made the one it
        starts again, and the one it starts now kept in its place: so a
        second rollback undoes the first. The data is not touched, and the
        settings the install keeps now go with it, as an update carries
        them. A build that does not start at all cannot run this: the
        firmware's own menu or shell starts \EFI\moonwater\previous.efi, or
        a stick does, and either is a session from which this runs.
*/
static b32 host_rollback_locked(host_install address_to install, bool said_yes)
{
        host_settings disk;
        p8 path[HOST_PATH_ROOM];
        p8 back[HOST_BUILD_ROOM];
        p8 asked[HOST_BUILD_ROOM];
        p16 machine;
        host_place place;
        bool lost;
        b32 fit;
        b32 failed;

        memory_zero(address_of place, sizeof(place));
        if (!host_install_refresh(install))
                return host_refuse("%s is no longer here\n", install->disk);
        host_install_read(install);

        if (host_system_mount(install, false))
                return 1;

        failed = 0;
        asked[0] = end;
        fit = host_join(path, sizeof(path), HOST_SYSTEM, HOST_KEPT_IMAGE)
                  ? host_image_fit(path, back, sizeof(back), address_of machine)
                  : HOST_FIT_NONE;
        if (fit == HOST_FIT_NONE)
                failed = host_refuse("%s keeps no build from before: an update keeps the one "
                                     "it replaces, and none has run since it was "
                                     "installed\n", install->disk);
        else if (fit)
                failed = host_refuse("the build %s keeps from before is not one this machine "
                                     "starts: %s\n", install->disk, host_fit_said(fit, machine));
        else if (install->readable && string_equals(back, install->build))
                failed = host_refuse("the build %s keeps from before is the one it starts: "
                                     "%s\n", install->disk, back);

        if (!failed)
        {
                host_say(log, host_label "%s has %s\n", install->disk,
                         install->readable ? (string_address)install->build
                                           : (string_address) "an image whose build cannot "
                                                              "be read");
                host_say(log, host_label "and keeps %s from before\n", back);
                //      The question is asked with nothing mounted: a command
                //      ended at it (Ctrl-C) left the system partition mounted
                //      to be written for the rest of the session. After the
                //      answer it is mounted again, and the build it keeps is
                //      the one the question was about or nothing is written.
                if (!said_yes)
                {
                        string_copy_bounded(asked, back, sizeof(asked));
                        host_unmount(HOST_SYSTEM);
                        if (!host_said_yes(false, install->readable
                                                      ? "Starting that one again keeps the data, "
                                                        "and the build it has now as the one to "
                                                        "go back to."
                                                      : "Starting that one again keeps the data, "
                                                        "and the image it has now as the one to go "
                                                        "back to if that image can be read."))
                                return 1;
                        if (host_system_mount(install, true))
                                return 1;
                        if (host_image_fit(path, back, sizeof(back), address_of machine) ||
                            !string_equals(back, asked))
                                failed = host_refuse("the build %s keeps from before changed while "
                                                     "the answer was waited for; nothing written\n",
                                                     install->disk);
                }
        }

        if (!failed)
                failed = host_settings_carried(install, address_of disk, address_of lost);
        if (!failed)
                failed = host_back_image(install->system, HOST_SYSTEM, back, address_of disk,
                                         address_of place);
        if (!failed)
        {
                host_settings_landed(install, address_of disk, place.carried, lost,
                                     "the build from before");
                host_say(log, place.unkept
                                  ? host_label "%s starts %s from its next start, keeping its "
                                               "data, and keeps it as previous.efi too: the image "
                                               "it had could not be read and is not kept. reboot "
                                               "starts it now\n"
                                  : host_label "%s starts %s from its next start, keeping its "
                                               "data; moonwater setup rollback starts the other "
                                               "again. reboot starts it now\n",
                         install->disk, back);
        }
        /*      The loader was switched and the build it was could not be made
                previous.efi: what starts is the build asked for, and the other
                is only in the copy beside it, which the next write removes. */
        else if (place.stranded)
        {
                host_settings_landed(install, address_of disk, place.carried, lost,
                                     "the build from before");
                host_say(log_error, host_label "%s starts %s from its next start, keeping its "
                                               "data, and keeps it as previous.efi too: the "
                                               "build it started until now could not be kept "
                                               "there, and is only in " HOST_KEPT_IMAGE_NEXT
                                               " on %s, which a firmware's shell can start and "
                                               "the next update or rollback removes\n",
                         install->disk, back, install->system);
        }

        host_unmount(HOST_SYSTEM);
        return failed;
}

/* setup update usb and setup rollback: the install chosen, under the lock. */
static b32 host_image_verb(string_address disk, bool said_yes, bool rollback)
{
        host_census census;
        host_install address_to install;
        b32 failed = 1;

        if (!host_acquire())
                return 1;

        install = host_install_chosen(address_of census, disk);
        if (install)
                failed = rollback ? host_rollback_locked(install, said_yes)
                                  : host_update_stick_locked(install, said_yes);
        host_release();
        return failed;
}

/*
        Where this session runs and what is on the disks: the build, whether
        the session is live, kept on a disk or waiting for an answer, every
        install found and the stick this image is on. Setup and status both
        open with it.
*/
static fn host_setup_state(void)
{
        p8 running[HOST_BUILD_ROOM];
        p8 verdict[HOST_NAME_ROOM + 16];
        p8 disk[HOST_NAME_ROOM];
        host_census census;
        host_medium_search search;

        if (host_running_build(running, sizeof(running)))
                string_format(log, "  this is %s, commit %s\n", running, host_commit);
        else
                string_format(log, "  this build's version cannot be read\n");

        host_read_word(HOST_VERDICT, verdict, sizeof(verdict));
        if (string_has_prefix(verdict, "disk "))
                string_format(log, "  kept on %s: %s /root /home\n",
                              verdict + 5, BOWL_ROOT_DIRECTORY);
        else if (string_has_prefix(verdict, "ask "))
                string_format(log, "  waiting: %s has another build; "
                                   "moonwater setup use, update or live\n",
                              verdict + 4);
        else if (string_has_prefix(verdict, "live ") && verdict[5])
                string_format(log, "  live session: %s's Moonwater data could not be mounted at "
                                   "boot, so nothing is kept after power off; moonwater setup "
                                   "use %s tries it again\n",
                              verdict + 5, verdict + 5);
        else if (host_data_disk(disk, sizeof(disk)))
                string_format(log, "  live session: nothing is kept after power off; %s "
                                   "already holds Moonwater's data, which moonwater setup "
                                   "install %s would erase\n",
                              disk, disk);
        else
                string_format(log, "  live session: nothing is kept after power "
                                   "off; moonwater setup install DISK keeps it\n");

        host_census_take(address_of census);
        host_census_said(address_of census);
        for (positive at = 0; at < census.count; at++)
        {
                host_install address_to install = census.found + at;

                host_install_read(install);
                //      Only root can mount an image to read it: for anybody else
                //      the build is not unreadable, it is not looked at.
                static const string_address install_lines[] = {
                    "  installed on %s, its build is read as root\n",
                    "  installed on %s, image unreadable\n",
                    "  installed on %s, this build\n",
                    "  installed on %s, another build: %s\n",
                };
                positive kind = !bowl_is_root()                    ? 0
                                : !install->readable               ? 1
                                : string_equals(install->build, running) ? 2
                                                                   : 3;

                string_format(log, install_lines[kind], install->disk, install->build);
        }

        if (!census.count)
                string_format(log, "  not installed on any disk here\n");

        //      Nor is a stick looked for by anybody who cannot mount it: the
        //      search keeps at it for up to ten seconds while storage arrives.
        search.found = false;
        if (running[0] && bowl_is_root() && host_medium_find(address_of search, running, null))
        {
                string_format(log, "  this image is on %s\n", search.name);
                host_unmount(HOST_MEDIUM);
        }

        //      And the builds the sticks plugged in carry, which an update can
        //      write without starting them.
        if (running[0] && bowl_is_root())
                host_sticks_said(running, search.found ? (string_address)search.name : null,
                                 address_of census);
}

/*
        This session as one page: the build, the disks, Canvas, the bound
        events, init and exit, then the commands. Nothing is a log line;
        the words that name each fact stay as they are.
*/
static b32 host_status(void)
{
        host_settings settings;
        struct canvas_control canvas;

        host_state_ready();
        host_title(log);
        host_setup_state();

        string_format(log, "\n");

        if (host_canvas_request(SPARK_CANVAS_STATUS, address_of canvas) >= 0)
                host_canvas_write(false, address_of canvas);

        locale_status_lines();

        {
                p8 name[NAME_ROOM];

                host_read_word(NAME_PATH, name, sizeof(name));
                if (name_valid(name))
                        string_format(log, "  name %s\n", name);
        }

        dns_say(false);

        host_status_wifi();

        if (host_settings_session(address_of settings))
        {
                host_settings_lines(address_of settings, 0, true);
                string_format(log, "  init mount is %s\n",
                              settings.flags & SPARK_SETTINGS_MOUNT_OFF ? "off" : "on");
                string_format(log, "  init canvas is %s\n",
                              settings.flags & SPARK_SETTINGS_CANVAS_OFF ? "off" : "on");
                host_settings_lines(address_of settings, 1, true);
        }
        //      Else they are root's and not known to this caller: no line, where
        //      there was "init mount is on" and nothing run at boot as facts.

        string_format(log, "\n");
        host_page_write(log);
        return 0;
}

/* use and update: the named install, the one boot asked about, or the only one. */
static b32 host_answer(bool update, string_address disk)
{
        p8 verdict[HOST_NAME_ROOM + 16];
        host_census census;
        host_install address_to install = null;

        host_read_word(HOST_VERDICT, verdict, sizeof(verdict));

        /*      The data this session already keeps. An update touches only the
                system partition, so it can go ahead underneath what is mounted;
                keeping a second disk's data on top of the first cannot. */
        if (string_has_prefix(verdict, "disk "))
        {
                if (disk && !string_equals(disk, verdict + 5))
                        return host_refuse("this session already keeps %s\n",
                                           verdict + 5);

                if (update)
                {
                        host_census_take(address_of census);
                        install = host_census_find(address_of census, verdict + 5);
                        return install ? host_update(install)
                                       : host_refuse("%s is no longer here\n",
                                                     verdict + 5);
                }

                host_say(log, host_label "already kept on %s\n", verdict + 5);
                return 0;
        }

        host_census_take(address_of census);

        if (disk)
                install = host_census_find(address_of census, disk);
        else if (string_has_prefix(verdict, "ask "))
                install = host_census_find(address_of census, verdict + 4);
        //      The disk a live session could not keep at boot (host_settle_kept_away).
        else if (string_has_prefix(verdict, "live ") &&
                 host_census_find(address_of census, verdict + 5))
                install = host_census_find(address_of census, verdict + 5);
        else if (census.count == 1)
                install = census.found;

        //      A disk that was named is answered about first: with two
        //      installs elsewhere, asking after a third said "more than one
        //      disk has Moonwater; name one" with its name run on the end.
        if (!install)
                return host_refuse(disk                ? "%s has no Moonwater install\n"
                                   : census.count > 1 ? "more than one disk has "
                                                        "Moonwater; name one%s\n"
                                                      : "no disk here has Moonwater "
                                                        "installed%s\n",
                                   disk ? disk : (string_address)"");

        return host_take(install, update);
}

#include "../../waterlink/command.c"
#include "../ssh.c" // moonwater ssh: OpenSSH's server from bowl profile ssh, run from its settings and nothing else
#include "../languages.c"

/*
        Forget userspace, keep the machine.

        /home is emptied. /root is emptied except the overlay and the files
        an image update already leaves on the data partition. /bowls
        stays: that is the pre-installed software a kiosk starts after wipe.
        A kiosk's machine script says it at every boot, and a script says the
        word that goes with it: `wipe yes`. At a terminal it asks instead.

        What /root keeps is each setting's own path, taken from the macro the
        command that writes it names it by, so a setting added without a line
        here is not wiped by default. A file system mounted below /home or
        /root is not this machine's userspace and stays as it is.
*/
#define HOST_WIPE_KEEP(path) ((path) + sizeof("/root/") - 1)

//      The files of the settings table's plain rows are added when a wipe runs
//      (host_wipe_table), after the files named here by hand.
// Room for the hand-named files, every row of the registry and every discovery switch: wipe must never run with a keep list that
// was cut short, since a name that does not fit is a setting it deletes (host_wipe_table refuses a list that does not fit).
static string_address host_wipe_keep[96 + SETTINGS_ROWS + array_count(discovery_switches)] = {
    HOST_WIPE_KEEP("/root/rules"),
    HOST_WIPE_KEEP(NET_WIFI_LIST),
    HOST_WIPE_KEEP(RADIO_RULES_PATH),
    HOST_WIPE_KEEP(NET_WIFI_POWER),
    HOST_WIPE_KEEP(NET_WIRED_POWER),
    HOST_WIPE_KEEP(NET_BLUETOOTH_LIST),
    HOST_WIPE_KEEP(NET_BLUETOOTH_POWER),
    HOST_WIPE_KEEP(NET_BLUETOOTH_SHARE),
    HOST_WIPE_KEEP(USB_POLICY),
    HOST_WIPE_KEEP(USB_LOG),
    HOST_WIPE_KEEP(NET_INTERNET_ROOT),
    HOST_WIPE_KEEP(TUNE_KEPT),
    HOST_WIPE_KEEP(WORKAROUNDS_KEPT),
    HOST_WIPE_KEEP(CLOCK_ZONE_PATH),
    HOST_WIPE_KEEP(LOCALE_ZONE_MODE_PATH),
    HOST_WIPE_KEEP(LOCALE_NTP_PATH),
    HOST_WIPE_KEEP(LOCALE_NTP_SERVER_PATH),
    HOST_WIPE_KEEP(LOCALE_NTP_SAMPLING_PATH),
    HOST_WIPE_KEEP(LOCALE_NTS_PATH),
    HOST_WIPE_KEEP(LOCALE_NTS_SERVERS_PATH),
    HOST_WIPE_KEEP(CLOCK_GOOD_PATH),
    HOST_WIPE_KEEP(LOCALE_KEYBOARD_PATH),
    HOST_WIPE_KEEP(LOCALE_CANVAS_MODES_PATH),
    HOST_WIPE_KEEP(SETTING_CANVAS_ARRANGE_PATH),
    HOST_WIPE_KEEP(SETTING_CANVAS_DISPLAYS_PATH),
    //  The tray's format, whose row is a special one (locale_taskbar_tray), so the table's plain rows do not name it.
    HOST_WIPE_KEEP(SETTING_TASKBAR_TRAY_PATH),
    //  The programs pinned to the taskbar, whose row is a special one too (locale_taskbar_pinned).
    HOST_WIPE_KEEP(SETTING_TASKBAR_PINNED_PATH),
    //  The sound card and the microphone, whose rows are special ones (locale_audio_pick); the rest of the Sound rows are plain.
    HOST_WIPE_KEEP(SETTING_AUDIO_DEVICE_PATH),
    HOST_WIPE_KEEP(SETTING_AUDIO_MIRROR_PATH),
    HOST_WIPE_KEEP(SETTING_AUDIO_SOURCE_PATH),
    HOST_WIPE_KEEP(DESKTOP_PATH),
    //  The programs the desktop starts with it (desktop_startup), and the list it began with (desktop_autostart): no row of the table keeps either.
    HOST_WIPE_KEEP(SETTING_DESKTOP_STARTUP_PATH),
    HOST_WIPE_KEEP(SETTING_DESKTOP_AUTOSTART_PATH),
    HOST_WIPE_KEEP(NET_DNS_PATH),
    HOST_WIPE_KEEP(NET_DNS_ORDER_PATH),
    HOST_WIPE_KEEP(NET_DNS_DEFAULT_PATH),
    HOST_WIPE_KEEP(NET_DNS_INTERNAL_PATH),
    HOST_WIPE_KEEP(NET_DNS_SUFFIXES_PATH),
    HOST_WIPE_KEEP(DNS_TLS_KEPT),
    //  The network's discovery lists (src/net/discovery.c, kept by leaf in /root); its switches are added with the plain rows.
    DISCOVERY_STUN_LEAF,
    DISCOVERY_URL_LEAF,
    HOST_WIPE_KEEP(NAME_PATH),
    HOST_WIPE_KEEP(LINK_SWITCH_PATH),
    HOST_WIPE_KEEP(LINK_KEY_PATH),
    HOST_WIPE_KEEP(LINK_PEERS_PATH),
    HOST_WIPE_KEEP(LINK_PORT_PATH),
    HOST_WIPE_KEEP(LINK_TRANSPORT_PATH),
    HOST_WIPE_KEEP(LINK_GROUPS_PATH),
    HOST_WIPE_KEEP(LINK_STAMPS_PATH),
    //  The sound modem's key, which `moonwater link modem key` makes and the far side holds the same of, and the two
    //  counts that go with it (src/waterlink/modem.c): the last frame sent and the last one taken, by the key's
    //  fingerprint. A key kept with its counts forgotten would count from 0 again under the same key.
    HOST_WIPE_KEEP(LINK_MODEM_PATH),
    HOST_WIPE_KEEP(LINK_MODEM_SEQ_PATH),
    HOST_WIPE_KEEP(LINK_MODEM_RX_PATH),
    //  The ssh server's settings, its keys and its host key, kept as the link's are; a timed on still ends at the next boot.
    HOST_WIPE_KEEP(SSH_STATE),
    //  Where the windows were last put, which the compositor writes and no plain row names.
    HOST_WIPE_KEEP(SETTING_WINDOWS_PLACES_PATH),
    //  The text size every terminal follows, which is the module's own
    //  (TERMINAL_SIZE_DIR in src/canvas/ui/terminal.c) and so named here.
    "terminal",
    //  The text fields' and text areas' keymap (ui.c's UI_TEXT_KEYS_PATH), which the text widgets keep.
    "canvas.text.keys",
    //  The per-program volumes the sound server remembers (src/audio, AUDIO_APPS_PATH), which no plain row names.
    "audio.apps",
    null,
};

// The keep list whole, or false when a name would not fit (then wipe deletes nothing).
static b32 host_wipe_table(void)
{
        positive used = 0;

        while (host_wipe_keep[used])
                used++;
        for (positive at = 0; at < SETTINGS_ROWS; at++)
                //      Only a row under /root names a file wipe keeps: one kept
                //      anywhere else is not in what wipe empties to begin with.
                if (settings_rows[at].how == SETTING_PLAIN && settings_rows[at].path &&
                    string_has_prefix(settings_rows[at].path, "/root/"))
                {
                        if (used + 1 >= array_count(host_wipe_keep))
                                return false;
                        host_wipe_keep[used++] = (string_address)settings_rows[at].path + sizeof("/root/") - 1;
                }
        //      And discovery's switches, a word each in /root (discovery_switches).
        for (positive at = 0; at < array_count(discovery_switches); at++)
        {
                if (used + 1 >= array_count(host_wipe_keep))
                        return false;
                host_wipe_keep[used++] = discovery_switches[at].leaf;
        }
        host_wipe_keep[used] = null;
        return true;
}

static b32 host_wipe_path(string_address path, positive mode,
                          string_address address_to keep)
{
        bipolar failed = bowl_reset_walk(path, BOWL_RESET_ONE_MOUNT, keep);

        //      An emptied directory that was not there is made, with the mode
        //      it is meant to have whatever the caller's umask says.
        if (!failed)
                failed = host_make_directory(path, mode, true);
        return failed < 0 ? host_fail(path, failed) : 0;
}

/*
        One command at a time changes what /root keeps: the radio lock is what
        wifi add and the verbs that write beside a list hold, and the settings
        lock is bind's and an install's. A wipe between a list's write and its
        rename took the half-written file and left `wifi add` with an error
        for a network it had just joined.
*/
static b32 host_wipe(bool said_yes)
{
        bipolar lock;
        b32 failed;

        host_need_root();

        if (!said_yes)
        {
                p8 answer[8];

                return_if(!host_asks(),
                          host_refuse("wiping forgets everything in /home and /root but the "
                                      "settings, and nothing here can be asked: "
                                      "moonwater wipe " HOST_YES "\n"));
                host_say(log, host_label "Wiping forgets everything in /home and /root but "
                                         "the settings. Type " HOST_YES " to go on: ");
                if (host_read_line(answer, sizeof(answer)) < 0 ||
                    !string_equals(answer, HOST_YES))
                {
                        host_say(log, host_label "nothing forgotten\n");
                        return 1;
                }
        }

        if (!host_acquire())
                return 1;
        lock = radio_lock(true);

        if (!host_wipe_table())
        {
                radio_unlock(lock);
                host_release();
                host_say(log, host_label "nothing forgotten: the list of settings to keep did not fit\n");
                return 1;
        }
        failed = host_wipe_path("/home", 0755, null) ||
                 host_wipe_path("/root", 0700, host_wipe_keep);

        radio_unlock(lock);
        host_release();
        if (!failed)
                host_say(log, host_label "userspace forgotten\n");
        return failed;
}

/*
        `moonwater setup`: a live session, or one that keeps its data on a
        disk, and the ways to change which. Arguments are judged before
        root is asked for, so a wrong one is a usage page for anyone.
*/
static b32 host_setup(string_address address_to arguments, positive count)
{
        string_address verb = count > 2 ? arguments[2] : null;
        bool update = verb && string_equals(verb, "update");

        if (!verb)
        {
                host_state_ready();
                host_title(log);
                host_setup_state();
                string_format(log, "\n");
                host_rows_write(log, "setup");
                log_flush();
                return 0;
        }

        if (string_equals(verb, "install"))
        {
                positive words = 4;
                bool removable = count > words && string_equals(arguments[words], "removable");
                bool said_yes;

                words += removable;
                said_yes = count > words && string_equals(arguments[words], HOST_YES);
                words += said_yes;
                return_if(count < 4 || count != words || !host_disk_word(arguments[3])[0],
                          host_usage());
                host_need_root();
                if (!said_yes && !host_asks())
                {
                        host_say(log_error, host_label "installing erases %s and nothing here "
                                                       "can be asked: moonwater setup install "
                                                       "%s%s " HOST_YES "\n",
                                 arguments[3], arguments[3],
                                 removable ? (string_address) " removable"
                                           : (string_address) "");
                        return 1;
                }
                host_state_ready();
                return host_install_disk(arguments[3], removable, said_yes);
        }

        if (string_equals(verb, "live") && count == 3)
        {
                p8 kept[HOST_NAME_ROOM];

                host_need_root();
                host_state_ready();
                //      What a disk has bound stays bound until the power goes, so
                //      a session that keeps one is not live, and the verdict is
                //      left saying so for the status and for the next use.
                if (host_session_disk(kept, sizeof(kept)))
                        return host_refuse("this session already keeps %s, and is not "
                                           "live until it powers off\n", kept);
                system_remove_at(AT_FDCWD, HOST_QUESTION, 0);
                //      Said as it fails, and then not said to have been done: the
                //      terminal that waits on the verdict gets nothing from this line.
                if (host_verdict_set("live", ""))
                        return 1;
                host_say(log, host_label "the disks are left alone this session\n");
                return 0;
        }

        /*      An image from a stick, or the one kept from before: [DISK]
                [yes], judged before root is asked for, and refused where
                nothing can be asked and nobody said yes, as install is. */
        if ((update && count > 3 && string_equals(arguments[3], "usb")) ||
            string_equals(verb, "rollback"))
        {
                positive first = update ? 4 : 3;
                bool said_yes = count > first && string_equals(arguments[count - 1], HOST_YES);
                positive words = count - said_yes;
                string_address disk = words > first ? host_disk_word(arguments[first]) : null;

                return_if(words > first + 1 || (disk && !disk[0]), host_usage());
                host_need_root();
                if (!said_yes && !host_asks())
                        return host_refuse("%s and nothing here can be asked: moonwater setup "
                                           "%s%s%s " HOST_YES "\n",
                                           update ? "writing a stick's build over an install "
                                                    "changes what it starts,"
                                                  : "going back to the build an install had "
                                                    "changes what it starts,",
                                           update ? "update usb" : "rollback",
                                           disk ? " " : "", disk ? disk : (string_address) "");
                host_state_ready();
                return host_image_verb(disk, said_yes, !update);
        }

        if ((update || string_equals(verb, "use")) && count <= 4)
        {
                string_address disk = count == 4 ? host_disk_word(arguments[3]) : null;

                //      A DISK that names nothing is a wrong argument, and is
                //      judged before root is asked for, as the others are.
                return_if(disk && !disk[0], host_usage());
                host_need_root();
                host_state_ready();
                return host_answer(update, disk);
        }

        return host_usage();
}

/* moonwater help [VERB]: the main page, or one verb's. */
static b32 host_help(string_address address_to arguments, positive count)
{
        return_if(count > 3, host_usage());

        if (count == 3)
        {
                host_verb = arguments[2];
                return host_rows_write(log, host_verb) ? (log_flush(), 0) : host_usage();
        }

        host_title(log);
        host_page_write(log);
        return 0;
}

static b32 host_status_verb(string_address address_to arguments, positive count)
{
        return count == 2 ? host_status() : host_usage();
}

static b32 host_wipe_verb(string_address address_to arguments, positive count)
{
        bool said_yes = count == 3 && string_equals(arguments[2], HOST_YES);

        return_if(count > 3 || (count == 3 && !said_yes), host_usage());
        return host_wipe(said_yes);
}

/* The build, one fact a line: the commit, its date and the kernel's release; a script reads them by their first word. */
static b32 host_version_verb(string_address address_to arguments, positive count)
{
        file_machine machine;

        (void)arguments;
        return_if(count != 2, host_usage());
        host_say(log, "commit %s\n", host_commit);
        host_say(log, "date %s\n", host_commit_date);
        if (file_machine_read(address_of machine))
                host_say(log, "kernel %s\n", machine.release);
        else
                host_say(log, "kernel unknown\n");
        return 0;
}

static b32 host_machine_verb(string_address address_to arguments, positive count)
{
        return count == 2 ? host_machine_run() : host_usage();
}

/* boot and ask are init's and the terminal's, and take no words. */
static b32 host_boot_verb(string_address address_to arguments, positive count)
{
        return_if(count > 2, host_usage());
        host_need_root();
        host_state_ready();
        return host_boot();
}

static b32 host_ask_verb(string_address address_to arguments, positive count)
{
        p8 verdict[HOST_NAME_ROOM + 16];

        return_if(count > 2, host_usage());
        host_need_root();
        return_if(!host_asks(), host_refuse("moonwater ask is a question for a terminal\n"));
        host_state_ready();

        host_read_word(HOST_VERDICT, verdict, sizeof(verdict));
        if (!string_has_prefix(verdict, "ask ") ||
            system_rename_at(AT_FDCWD, HOST_QUESTION, AT_FDCWD,
                             HOST_QUESTION_TAKEN, 0) < 0)
                return host_refuse("there is nothing to ask\n");

        host_question(verdict + 4);
        return 0;
}
