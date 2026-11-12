<img width="1153" height="160" alt="Dawning Linux Header-2" src="https://github.com/user-attachments/assets/675f22bb-fc92-43a2-9ba5-d957ee0d4d56" />

<br><br>

Moonwater is a research Linux distribution: think "Linux++". The foundations of
userspace -- the shell, coreutils, util-linux, the desktop and its terminal --
live in the Moonwater kernel module, so the system has a fixed, working base
whatever else is installed. Performance and latency are much of the point, and
the whole system is meant to stay under 15 MB.

## Build

```sh
sh build.sh --run                       default profiles, booted in a window
sh build.sh arch/x64 debug_none serial  the profiles named
sh build.sh --help                      every option
```

The build is one C program, `src/build/build.c`, on this project's own
freestanding stack: a bare machine needs a C compiler and an assembler.
Profiles in `kernel/profile/` compose the kernel configuration and the security
tiers (`sec_default`, `sec_hardened`, `sec_locked`; see
[.github/SECURITY.md](.github/SECURITY.md)).

## The `moonwater` command

`moonwater` manages the system itself: where it lives, what runs at boot, and
what the events of the machine do. Its commands are grouped as the Settings window groups its
rows, and the ones no row has are under All commands:

```sh
# Machine
moonwater language [CODE|default]      the language the windows Canvas draws itself speak; bare lists them, machine-translated ones marked [en]
moonwater name [NEW|random]            what this machine is called, like space-wizard


# Canvas
moonwater canvas                       whether Canvas is on, and the screens it draws on
moonwater canvas arrange [NAME[@X,Y|@off]...|default]  where each screen is: names left to right, the first the primary [default]
moonwater canvas background [COLOUR|default]  the desktop colour, rrggbb [1e1e1e]
moonwater canvas border width [PIXELS|default]  how wide the highlight on the edge or corner of a window is when the pointer is over it or the hand holds it, 1 to 4 pixels [2]
moonwater canvas border grip [PIXELS|default]  how far outside the edge of a window a press of the pointer takes it to resize, 2 to 16 pixels; a finger takes 16 [6]
moonwater canvas caret blink [slow|normal|fast|off]  how the caret blinks, or off for a solid caret [normal]
moonwater canvas caret thickness [thin|normal|thick]  how wide the caret is: 1, 2 or 3 pixels at 100%; a block cursor is a whole cell [normal]
moonwater canvas clipboard [on|off]    copy and paste between windows; off empties the clipboard [on]
moonwater canvas clipboard history [N|default]  copies the compositor keeps in memory, never on a disk, 0 to 32 [0]
moonwater canvas clipboard limit [MIB|default]  the most one copy may be, 1 to 16 MiB [16]
moonwater canvas gpu [on|off]          draw and compose on the GPU where Canvas has a driver for it, on the processor elsewhere; alpha [off]
moonwater canvas identify              show the number of each screen for three seconds
moonwater canvas motion [full|reduced]  reduced: the windows Canvas draws change at once, with no spring, stretch or ripple [full]
moonwater canvas motion speed [PERCENT|default]  how fast windows, menus and controls move, 50 to 200 percent of the speed they were designed at, higher quicker; reduced motion is none at any speed [115]
moonwater canvas record [SECONDS]      record the presents of the compositor for SECONDS (1 to 10, 3 by default)
moonwater canvas record report         the report of the presents: the paths, flips, late and held presents
moonwater canvas text scale [PERCENT|default]  how large the text of the windows Canvas draws is, 50 to 300 percent [100]
moonwater canvas theme [light|dark|auto]  the colours of the windows Canvas draws itself; auto is light from 7 to 19 by the clock [dark]
moonwater canvas theme accent [COLOUR|reset]  what the controls fill with, rrggbb; the text on it is picked to be read [ffffff]
moonwater canvas windows [forget]      where windows open again, from where the hand put them; forget empties it
moonwater canvas windows remember [on|off]  a window opens where the hand last put it, kept across boots [on]


# Display
moonwater brightness [N%|+N|-N]        the screen backlight
moonwater canvas modes [largest|preferred]  which mode a screen is driven at [largest]


# Input
moonwater canvas cursor [size [PERCENT|default]|look [light|dark|auto]]  the pointer: its size, 50 to 300 percent of the size the desktop scale gives it, and its colours [80, auto]
moonwater canvas cursor style [classic|dynamic]  the classic pointers, or one filled shape that stretches with a fast hand and is a rounded block over text [dynamic]
moonwater canvas cursor squeeze [PERCENT|default]  how far a pointer shrinks while a button is held, 0 to 50 percent, 0 for none; the right button grows it by half that [25]
moonwater canvas cursor shake [on|off]  a shake of the hand makes the pointer large for a moment [on]
moonwater canvas cursor shake delay [MS|default]  how long it stays large after the last shake, 100 to 10000 ms [1200]
moonwater canvas cursor shake sensitivity [low|normal|high]  how much shaking makes the pointer large: low takes more and wider shakes, high fewer and smaller [normal]
moonwater canvas cursor click [on|off]  a held button changes how large the pointer is: the left shrinks it, the right grows it [on]
moonwater canvas predict touch [on|off]  a finger or a pen is taken a frame ahead of where it is, to the windows it reaches [on]
moonwater canvas touch [on|off]        a circle under each finger on a touch screen, fading for a second after it lifts [on]
moonwater canvas touch hold [MS|default]  how long a finger held still is a long press, the right button, and a key of the on-screen keyboard shows its accents, 300 to 1500 ms [500]
moonwater canvas touch left [off|windows|launcher]  a swipe in from the left edge runs the window switcher, or opens the launcher [off]
moonwater canvas touch right [off|windows|volume]  a swipe in from the right edge runs the window switcher, or opens the volume popup [off]
moonwater canvas wheel smooth [on|off]  a notch runs to its step (two and a half rows) at once and eases out; off moves a whole step at once [on]
moonwater keyboard [LAYOUT|list|xkb]   us uk gb de se sv no nb dk fi fr es it ar il ir [us]
moonwater keyboard onscreen [auto|off|on]  a keyboard on the screen while a text field has the keys: auto when no keyboard is plugged in, off, or on [auto]
moonwater keyboard repeat delay [MS|default]  how long a key is held before it repeats, 100 to 2000 ms [250]
moonwater keyboard repeat rate [PER-SECOND|off|default]  repeats a second of a held key, 1 to 100, or none [30]
moonwater keyboard start               exit 0 when the desktop starts the on-screen keyboard: the setting on, or auto with a touch screen plugged in or no keyboard
moonwater mouse acceleration [on|off]  the pointer outruns a fast hand; off is one to one [on]
moonwater mouse double click [MS|default]  how soon a second press near the first is a double click, 200 to 900 ms; a third as soon is a triple [450]
moonwater mouse sensitivity [PERCENT|default]  how far the pointer goes for the hand, 10 to 500 percent [100]


# Scrolling
moonwater canvas scroll drag [on|off]  a mouse press on a list that moves drags the list as a finger does, and its release throws it; a slider, a scroll bar or a text keeps its own drag; off, the mouse scrolls a list by its wheel and its bar alone [off]
moonwater canvas scroll momentum [on|off]  a drag or a trackpad hand carries on past its release and slows; off, the content stops where the fingers leave it, for the guest only: momentum from a patched host is not stopped (no phase signal yet) [on]
moonwater canvas scroll natural [on|off]  a wheel and a trackpad move the content with the fingers, as on a touch screen; off, the other way, as a wheel mouse does [off]
moonwater canvas scroll rubber [on|off]  past the top or the bottom the content stretches and springs back; off, it stops at the end [on]
moonwater canvas terminal wheel [LINES|default]  how many lines a notch of the mouse wheel moves the terminal, 1 to 10; a full-screen program is sent as many arrows [3]
moonwater canvas trackpad speed [PERCENT|slow|normal|fast|default]  how far a wheel notch and a trackpad scroll move the content, 25 to 400 percent: a notch is two and a half rows, a stream notch of a trackpad 8 px at 100 and more as its notches come faster; slow, normal and fast are 60, 100 and 160 [100]


# Sound
moonwater audio                        the volume and the mute, the sound card and the cards there are, the programs that play, the microphone and the programs that record, and whether the sound server starts
moonwater audio capture [on|off]       let programs record from the microphone; off ends every recording and refuses new ones [on]
moonwater audio device [NAME|default]  the sound card that plays, by the name bare audio lists; default picks the first that is not HDMI [default]
moonwater audio input mute [on|off]    silence the microphone, and keep its volume [off]
moonwater audio input volume [PERCENT|default]  the volume of the microphone, 0 to 100 percent [100]
moonwater audio mute [on|off]          silence everything that plays, and keep the volume [off]
moonwater audio server [on|off]        the server programs play their sound through; off keeps it from starting with the desktop [on]
moonwater audio source [NAME|default]  the microphone programs record from, by the name bare audio lists; default picks the first [default]
moonwater audio test                   play a second of 440 Hz through the sound server and say what became of it: no server, no card, a mixer that is off, muted, or played on this card
moonwater audio volume [PERCENT|default]  the volume of everything that plays, 0 to 100 percent [100]


# Taskbar
moonwater taskbar display [primary|all]  the screens the taskbar is on: the primary, or all, each listing its windows [primary]
moonwater taskbar fullscreen [on|off]  the taskbar slides away while a window is fullscreen, and comes back when none is [on]
moonwater taskbar launcher [on|off]    a tab with the launcher mark at the start of the taskbar that opens the launcher beside it and puts it away again [off]
moonwater taskbar padding [PX|default]  the room round the text inside each taskbar tab, 0 to 24 pixels at the taskbar scale [6]
moonwater taskbar position [bottom|top|left|right]  the edge of the screen the taskbar sits on [bottom]
moonwater taskbar scale [PERCENT|default]  how large the taskbar and what it shows are, 50 to 200 percent of the screen scale [100]
moonwater taskbar text [center|edge]   on a left or right taskbar, which way the tops of the letters face: the middle of its screen or its edge [center]
moonwater taskbar tray [FORMAT|default]  what the tray shows: text with {battery} {battery.percent} {battery.state} {time.24h.hour} {time.24h.min} {time.12h.hour} {time.ampm} {date.year} {date.month} {date.day} {date.weekday} {date.monthname} {cpu.load} {mem.used} {temp} {host} in it; {{ and }} are the braces [the battery, then the clock]


# Time
moonwater ntp [on|off]                 set the clock from the network [on]
moonwater ntp nts [on|off|only]        authenticated time (RFC 8915) first; only refuses plain [on]
moonwater ntp nts servers [NAME[:PORT]...|default]  who is asked for it, up to four
moonwater ntp sampling [on|off]        keep the lowest-delay sample of five [on]
moonwater ntp server [NAME|auto]       who is asked first: a name or address [auto]
moonwater time [sync]                  the clock; sync sets it and the zone now
moonwater time date [off|ymd|dmy|mdy]  the date beside the clock on the tray, year first, day first or month first, and in GNOME programs [off]
moonwater time format [24h|12h]        the hours the clock is read in, on the tray and in GNOME programs [24h]
moonwater timezone [ZONE|list]         IANA name, country, +1 or POSIX; manual
moonwater timezone auto                from the network, one Cloudflare request per network joined [auto]


# Network
moonwater airplane [on|off]            every radio at once
moonwater dns [ADDRESS...|auto]        up to three servers names are asked of, or the network gives them [auto]
moonwater dns tls [on|off|only]        names over TLS to 1.1.1.1 and 9.9.9.9 first; only sends none in the clear [off]
moonwater priority [internet [wired|wifi]]  which link wins when both are up [wired]
moonwater wired [on|off]               the wired links; off keeps them down


# Waterlink
moonwater link                         who this machine is linked with, and what each may do
moonwater link add NAME KEY [HOST[:PORT]]  link by key, with no code
moonwater link allow|deny NAME GRANT...  shell run log files (any file but the link state)
moonwater link group [NAME [SECRET|-] [allow GRANT...]]  machines on one network that link themselves
moonwater link group leave NAME [forget]  stop, and forget the key of the group
moonwater link key                     the public key of this machine, which link add takes
moonwater link log NAME                follow the kernel log of NAME
moonwater link modem key               the audio modem's key, in hex, made on first use: the receiver needs the same
moonwater link modem send FILE         FILE over the sound card to the Mac's receiver (tools/modem_rx.c); nothing is acknowledged
moonwater link NAME [COMMAND...]       a terminal on NAME, or one command with its status
moonwater link NAME CODE               link to the machine called NAME, which waits
moonwater link on|off                  the listener, kept across boots [off]
moonwater link pair [NAME]             a code, and wait for the other machine to use it
moonwater link port [N|auto]           the udp port it takes, and `link add` assumes [22348]
moonwater link pull NAME PATH FILE     PATH there to a file here
moonwater link push NAME FILE PATH     a file here to PATH there, whole or not at all
moonwater link remove NAME             stop knowing it
moonwater link transport [network|modem]  how linked machines reach each other: network, or the audio modem, which needs a sound card open on the sound server [network]


# Wi-Fi
moonwater wifi [on|off]                the radio, saved networks and those in range [off]
moonwater wifi add SSID [PASSWORD|-] [wpa2]  remember a network, joined once wifi is on; asks for the password, - reads it from stdin; wpa2 lets a tight tier join it without WPA3
moonwater wifi remove SSID             forget a saved network, and leave it


# Bluetooth
moonwater bluetooth [on|off]           the radio and the remembered devices [off]
moonwater bluetooth add NAME           keep the name of a device, found once the radio is on
moonwater bluetooth remove NAME        forget a bluetooth device
moonwater bluetooth share [on|off]     the machine's internet, shared with the Bluetooth devices that pair with it; needs the Bluetooth network tools [off]
moonwater bluetooth share [on|off]     the machine's internet over Bluetooth to the devices that pair with it; off until on (root)


# Devices
moonwater device [lock|unlock]         unlock lets a new device in; lock holds it until it is allowed [unlock]
moonwater device [lock|unlock]         lock keeps what is plugged in now, and holds a new device until it is allowed [unlock]
moonwater device allow|deny ID|PORT    let one device in, or hold it, now and at every plug; ID is VID:PID, or VID:PID/SERIAL as device shows it
moonwater device apply                 judge every device again now, as boot does
moonwater device class NAME [allow|deny]  what a new device no rule names may bring up: storage hid audio video net bluetooth hub other [deny]
moonwater device forget ID|PORT        drop the rule of one device
moonwater device list                  the devices the system sees, each allowed, blocked or waiting
moonwater device log [N]               the last decisions, the newest last [20]


# Power
moonwater battery [NAME|auto|none]     which battery the Battery row reads: auto is the first present one, none is no battery [auto]
moonwater charge [limit N|limit off]   where the battery stops charging, 20 to 100
moonwater cpu [boost|smt on|off]       turbo and SMT, kept across boots
moonwater cpu online|offline N         hotplug one processor
moonwater power [performance|balanced|powersave]  profile and CPU governor, kept across boots


# Boot
moonwater bind                         the events of the machine, and what each runs
moonwater bind canvas on|off [COMMAND]  when the desktop starts or stops
moonwater bind EVENT [COMMAND]         one event; empty puts the default back
moonwater bind exit                    what runs at poweroff or reboot
moonwater bind exit add "command"      run a command before the disks go read-only
moonwater bind exit remove ID|"command"  stop running one at the stop
moonwater bind init                    what runs at every boot, with ids
moonwater bind init add "command"      run a command at boot, once disks settle
moonwater bind init canvas [on|off]    start Canvas at boot [on]
moonwater bind init mount [on|off]     mount kept disks at boot [on]
moonwater bind init remove ID|"command"  stop running one at boot
moonwater bind poweroff [COMMAND]      the power button [poweroff]


# All commands
moonwater bios [reboot]                whether firmware setup is on offer; reboot goes
moonwater clipboard [primary]          what the clipboard holds and who copied it, never the text (root)
moonwater clipboard clear              empty both selections and the history (root)
moonwater clipboard get [primary]      the copied text: exact to a file or a pipe, escaped to a terminal (root)
moonwater clipboard set [primary] [TEXT]  copy TEXT, or standard input (root)
moonwater clipboard watch [primary]    a line each time something is copied, the text escaped (root)
moonwater debug                        what the desktop is doing and why (root): the card, the screens, the windows and their scroll bodies, the pointer, the input devices and the wheel
moonwater desktop [on|off|PROFILE]     the desktop, now and at every start; off leaves the text console [on]
moonwater desktop start|stop           begin the chosen desktop now, or end its session
moonwater help [VERB]                  these commands, or the page of one
moonwater latency [reset]              what the input handlers cost a report, and who has the display
moonwater notices [clear]              the errors and warnings of the kernel the desktop has (root): clear dismisses them
moonwater security                     the built-in trust, and what was added to it or taken from it
moonwater security aia [on|off]        fetch a CA a server left out, from where its certificate says [on]
moonwater security block               every key refused, built in and added
moonwater security block add SHA256|FILE  refuse a key wherever it is: an anchor, a CA, a leaf, a CT log
moonwater security block remove SHA256  stop refusing a key that was added; a built-in one stays
moonwater security ct [on|off]         ask Certificate Transparency of every chain, not only at the tight tier [off]
moonwater security http [on|off]       plain http in wget and fetch, and redirects to it [on]
moonwater security packages [on|off]   programs of a bowl may run the package manager of its distribution [on]
moonwater security revocation [soft|hard]  hard refuses a certificate whose revocation cannot be told [soft]
moonwater security sudo [on|off]       moonwater through sudo or su in a bowl; on trusts root in a bowl as root of the machine, for single-user machines [off]
moonwater security tls12 [on|off]      TLS 1.2 where a server offers nothing newer; off asks for 1.3 alone [on]
moonwater security trust               every anchor, built in and added, by the SHA-256 of its key
moonwater security trust add FILE      trust a CA certificate, PEM or DER, as the built-in anchors are
moonwater security trust remove SHA256  stop trusting an anchor that was added
moonwater sensors                      the processor use, clocks, GPU load, heat, fans and power of this machine, read now
moonwater settings [gallery]           the settings window, on the desktop of the compositor; gallery, every control of the UI kit
moonwater setup                        where this session runs, and the installs found
moonwater setup install DISK [removable] [yes]  erase DISK and put Moonwater on it removable takes a disk that says it is; yes is for a script, which cannot be asked
moonwater setup live                   leave the disks alone this session
moonwater setup update [DISK]          write this build over an install, keeping its data
moonwater setup use [DISK]             run this build with the data of an install
moonwater sleep | hibernate            suspend to RAM or to disk
moonwater status                       this session as one page
moonwater window                       the windows on the desktop: name, pid, place, size, title
moonwater window move PID|NAME PLACE...  put the windows of a program, or of a name, where PLACE says
moonwater window PLACE... COMMAND...   run COMMAND, its first window put where PLACE says: center, at X,Y, maximized, fullscreen, screen N, size WxH, name NAME (what it is remembered by)
moonwater wipe [yes]                   forget /home and /root, keep the machine; yes is for a script, which cannot be asked
```

`moonwater` alone prints the main page, and `moonwater help VERB` every row of one: the
main page leaves out the rows that only a verb's own page shows, such as `bind poweroff`
and `link key`.

`moonwater desktop on|off|PROFILE` is the one switch of what is on the screen, kept for
every start and put in force at once: `on` is the compositor, `off` the text console, a
profile (`bowl profile desktop` installs KDE) its session. A session has the display
only once the compositor has let go of it, since nothing else can be the card's master
while it is, so the compositor ends first and `desktop on` brings it back.

A switch with no word after it says where it stands, to anybody; with one it
changes it and needs root, and a word it does not take is a usage page (exit 2)
and not a refusal (exit 1). `bowl` runs other distributions' programs
(`bowl profile desktop` installs KDE, `bowl profile chrome` Alpine's Chromium as a
program of Canvas, `bowl profile steam` Arch's Steam with its 32-bit graphics and
sound, on x86-64: `moonwater desktop` does not take either for a session), and
`main.moonwater.sh` is the machine script that sets a machine up at boot.

`moonwater security` changes what the TLS client (wget, the clock's NTS, names over
TLS) trusts without a rebuild. A key is named by the SHA-256 of its DER
SubjectPublicKeyInfo, as `openssl x509 -pubkey -noout | openssl pkey -pubin -outform
der | sha256sum` prints it, or by a certificate file. Blocking a key takes a built-in
anchor out of trust, and refuses any CA, leaf or Certificate Transparency log with it;
a block wins over a trust of the same key, and the keys built in as compromised stay
refused. An anchor added is held to the rules of the built-in ones (a CA, P-256, P-384
or RSA of 2048 to 4096 bits, no name constraints). Both lists are kept with the image's
settings, so they go along with `setup install` and `update`, and every program reads
them from /run/moonwater/security when it connects; a file there that does not read
refuses every checked certificate until boot or the next change writes it again. The
five switches of TLS only tighten, each from what the default tier does today, and none
can loosen a tight build; a change reaches every connection opened after it, while one
already open, and a download's own redirects and revocation fetches, keep what they
started with. `http off` leaves the CRL and caIssuers fetches inside TLS alone, which
are plain http by design and checked by their signatures.

`packages` and `sudo` are the bowls' two switches, kept and published the same way.
`packages` says whether the programs of a bowl may run their distribution's package
manager (apk, apt and dpkg, pacman, dnf and rpm, zypper, nix); it is on by default and
off at the bowl's tight tier (`sec_bowl_hardened`, and `sec_hardened` and `sec_locked`,
which put every region there), which refuses `packages on`. `sudo` says whether the
`moonwater` command may be used through a bowl's sudo or su, by a process that is root
only inside the bowl: on trusts root in a bowl as root of the machine, which is for a
machine with one user, so it is off at every tier and only the default tier can turn
it on. A published file that is not root's, that a group or others may write, or that
does not read counts as no file: packages on, sudo off. Bowl and the command's root
check are what ask them; until that asking lands, a switch is kept, published and
listed and changes nothing else.

At the tight and locked net tiers (`sec_net_hardened`, `sec_net_locked`) a closed
port gives a scanner nothing: the kernel sysctl `net.ipv4.moonwater_silent` is set
to 1 and the kernel drops the TCP reset and the ICMP port-unreachable it would
otherwise send for a segment or datagram to a port with no socket, for traffic that
did not arrive on loopback (IPv6 included). Loopback keeps refusing a closed port at
once, and a port with a listener is reached as ever: it hides closed ports from the
outside, it is not a firewall for open ones, and it adds nothing to the path of a
packet for an open port. The default tier leaves it 0, so UDP `traceroute` still
reads a port-unreachable. The sign it is working is that a scan stops moving
`/proc/net/snmp`'s `TcpOutRsts` and `IcmpOutDestUnreachs`.

Wi-Fi and Bluetooth are off until `moonwater wifi on` and `moonwater bluetooth on` raise
them: a machine that has not asked for a radio has it soft-blocked from boot, and `wifi add` and
`bluetooth add` keep a name for when it is on and do not switch it. A machine that already
used one (a saved network, a remembered device) keeps it on as it was, and `airplane off`
brings back what was on.

`moonwater device lock` holds what somebody plugs in: a stick, a keyboard that types on its
own, a hub, a network, audio or camera function. Every device plugged in when it runs
becomes a rule in /root/usb.policy, with the classes it shows then (one the kernel holds
unconfigured is configured first to show them), and keeps working; one with no serial is
pinned to its place, the controller, root hub and port path, which survive a boot that
numbers the buses otherwise. A new device, one no rule names, or a class a known device never had (a stick that comes
back as a keyboard too) stays unauthorized until `device allow` lets it in or `device class`
allows what it is. The kernel holds the switches (the `authorized` file of each device
and interface, the defaults of each root hub), so a held device is never configured and
no driver sees it; a watcher puts the policy in force at each plug, and every decision is
a line of /root/usb.log, with what a device says of itself escaped. It cannot stop the
kernel reading a new device's descriptors and strings before anything decides, or a hub
doing its own work; a device that copies the VID:PID and serial of one that was allowed
is that device to it; DMA over Thunderbolt or PCIe is the IOMMU's (the `sec_dma_*` profiles); and root
can undo it. The policy is read when /root is, so a device plugged in during boot is up
until then: only the kernel command line `usbcore.authorized_default=2`, which
`sec_host_locked` sets, closes that window. The tight tier locks at the first boot, once
USB has settled, and a /root it takes later with no policy of its own; a lock that cannot
be written to /root, or finds a policy there it cannot read (which it does not write over), is in
force from /run until the machine restarts, and `device` says why.

`moonwater settings` opens the same command as a window on the desktop: the groups
(machine, canvas, display, input, taskbar, time, network, Wi-Fi, Bluetooth, USB, power, boot, and all commands) are
listed at the left and the rows of the one chosen at the right, each row a setting a
person flips, and under All commands a row to every line of `moonwater help`, so no
verb is out of its reach. Tab goes between the groups and the rows, the arrow keys
move in either, and a press on a group or a row does what Enter does on it. A choice
opens a list of all its choices under it: Up and Down, a letter and Enter pick one
and Escape leaves the row as it was. The group on show is kept up with: the networks
in range (Wi-Fi), the devices kept (Bluetooth), the battery and the clock are asked
again every second or two, and joining a network asks for its passphrase in the
window. Typing filters the rows of every group; a command that ends the session or
throws away what is kept is run by a second Enter.

Canvas's own windows are drawn with one UI kit (src/canvas/ui/ui.c), Dawning Native's
controls and their motion to the number: a press springs, a drag squashes, a toggle
stretches its row, a radio's pip flies, and so on. `moonwater canvas theme` picks light,
dark or auto (light from 7 to 19), `canvas theme accent` the colour its controls fill
with, `canvas motion speed` how fast the motion runs (115 percent of the speed it was
designed at unless set), and `canvas motion reduced` turns the motion off; every open
window turns to a change at once. `moonwater settings gallery` opens the settings window on the kit's
gallery: every control, as the kit's own demo page has them, to see and press.

The words those windows say are one table of ids (src/canvas/ui/ui.c), English being the table itself, and
every other language is a file of `languages/` (the image carries them as /usr/share/moonwater/language/CODE.txt)
with the same ids. `moonwater language CODE` picks one, and every open window says its words in it at once; bare, it
lists the languages and marks the ones a machine translated and no person has reviewed. Es, fr, de, pt, it, nl, pl,
tr, sv, ru, uk and el are the ones the shipped faces (Inter, IBM Plex Mono) draw. Ar, he, fa and ur (Arabic, Hebrew,
Persian, Urdu) read from the right: their files say `@direction rtl` in the header (no other file does), and they are
drawn in the Noto Sans Arabic and Noto Sans Hebrew faces the image carries beside Inter, which draws the Latin they keep
(moonwater, Ctrl+R, the digits 0-9). All sixteen besides English are machine translations. The Indic scripts, Thai and the
Chinese, Japanese and Korean ones need a face and shaping that are not there.
`languages/glossary` fixes the terms, and `languages/keep` the few English literals a converted file keeps.

On the desktop, Super and Space open the launcher: a search bar for the desktop's programs, the programs of PATH and shell commands, which the same chord, Escape or a press outside it puts away. With `moonwater taskbar launcher on` the taskbar has a tab with the launcher mark at its start (its left end, or the top of a side taskbar) that opens it beside the tab, above a bottom taskbar, below a top one and to the side of a left or right one, and a press on the tab while it is open puts it away. Under its list are two buttons, Power off and Restart: each asks for a second press before it powers the machine off or restarts it (a key held down is one press, and its repeats confirm nothing), and Shift-Tab puts the keys on them.

The mixer (`/mixer`, or Mixer in the launcher) shows the sound server's volumes as columns: the output device first, then a strip for each program that has a sound (the streams of one program are one strip), each a disc with the program's letter, its name, its volume, a level meter beside a vertical slider, and a Mute button. Up and Down change the volume of the strip the keys are on by one, Page Up and Page Down by ten, Home and End make it 100 and 0, and M, Space and Enter mute it; Left, Right and Tab go from strip to strip, the wheel over a strip changes it two a notch, and a strip pressed on the slider goes where the hand does. When the strips do not fit, the wheel over the ground, Shift with the wheel and a drag scroll them. It reads the server's page of memory (`/run/moonwater/pulse/state`) and changes volumes over the server's socket, so a volume set on the command line (`moonwater audio volume`), by a program (`pactl`) or in the mixer is one volume; with no server running it says so. The output device's strip says under its volume why that output plays, and is dim and says so when nothing is plugged into it. Two tabs along its top, Playback and Recording (Control with Page Up and Page Down or Tab goes between them), show what plays and what records: Recording has the microphone first, then a strip for each program that records from one (a program that records what plays is none), their volumes and mutes changed the same way; with recording turned off (`moonwater audio capture`) it says so and has an Allow button that turns it on, and with no microphone it says that. `mixer -i` starts on Recording, or shows it in a mixer that is open. The small mixers that open from the tray's cells are glass over what is under them. Escape closes it.

With the sound server running the taskbar's tray has a volume cell before the battery's: a speaker with no wave, one or two by the volume, crossed out when it is muted. The wheel over it changes the master volume five percent a notch, a middle press mutes it, and a press opens a small mixer beside it: the output device's strip alone, with a Mixer row under it that opens the whole of the mixer; the keys going elsewhere put it away. The volume keys (Mute, Volume Down, Volume Up) do the same wherever the keys are, five percent a press, and the cell says the number for a moment; Volume Up on a muted machine unmutes it. A change is told to the server over its socket at once and kept in `moonwater audio volume` and `moonwater audio mute` (/root/audio.volume and /root/audio.mute) for the next boot; with the socket not there the files alone are written and the server reads them again twice a second. With no server there is no cell and the keys do nothing. While a program records from a microphone (not one that records what plays, nor with recording turned off) the tray has a red microphone cell before the volume's, so that recording is never silent: a press opens the mixer's Recording strips beside it, which name the programs, a middle press and the microphone key (`moonwater audio input mute`, struck when it is muted) mute the microphone, over the socket at once and in /root/audio.source.mute.

While the board holds a notice or an issue (the kernel's errors and warnings the desktop has, and the options' own problems), the tray starts with a small dot at its leading end: amber when the worst of them is a warning, red when it is an error. A press on it opens the notices popup beside it, which lists up to six of them, errors first, each with its severity word at its right and, for a kernel notice, its count and the minutes since its last record. Its two rows are Open System notices, which starts Settings on its System notices page, and Dismiss all, which clears the kernel notices (`moonwater notices clear` does the same); an option's own issue stays on the board until its program clears it. With nothing on the board there is no dot and no space for one. The dot is drawn from the board's sequence number, so it costs nothing while the board is still.

A sound card starts with its own mixer muted, and nothing else here sets it, so when the sound server opens a card it puts the card's output switches on (Master, Speaker, Headphone, PCM, Front, Line Out, S/PDIF and HDMI), their gains at the hardware's 0 dB and auto-mute on, and says what it found in its log and on its page. `moonwater audio` says when nothing can be heard and why (no card is open and the server plays to a clock, a switch of the card that will not stay on, a volume of 0, a mute), and `moonwater audio test` plays a second of 440 Hz through the server and says in words what became of it.

Every screen the card has connected is one desktop. `moonwater canvas arrange` lays the
screens by their connectors' names, left to right with their tops level, the first the
primary (the taskbar's, and where a window opens), one at a place given as NAME@X,Y or
dark as NAME@off; with nothing kept they go left to right in the card's order, and
`moonwater canvas` lists them in that order with where each is. The pointer crosses from
one screen to the next where their edges meet and stops beside the rest of an edge. A
window dragged across shows on both while it straddles them; it belongs to the screen
that holds the most of it, which is where it is maximized and halved and, with `taskbar
display all`, whose taskbar lists it, and one let go just past a shared edge has crossed
it and is not arranged. A screen plugged in joins the desktop, and one unplugged takes
nothing with it: its windows and the pointer come to the primary, at the same share of
the room they had. The desktop is drawn at one scale on every screen, and each screen
at its own rate: a change on one waits on that screen's refresh alone, so a 60 Hz screen
beside a 50 Hz one still draws sixty frames a second.

`moonwater window PLACE... COMMAND...` starts COMMAND with where its first window goes.
`center` is the middle of the screen's room, the screen less the taskbar; `at X,Y` puts
the window's top left at X,Y from the screen's; `maximized` fills the room and keeps to
it when the taskbar moves, and a double click gives the window back the place it would
have had; `fullscreen` fills the whole screen as a double click does, the taskbar still
drawn over it. `screen N` counts the screens from 1 in the order `moonwater canvas` lists
them (one that is not there is the first), and `size WxH` asks the program for that
size, up to the screen's. A window is always kept on its screen. The placement reaches
the command as MOONWATER_PLACE in its environment: the first window of the compositor's
own that it opens (the terminal, the log, the settings, the launcher, `/surface`) takes
it and empties it, so what that program starts after is not placed too, while programs
the command starts before any window is open each take their own copy. A program that is
no window of the compositor's, from a bowl, Wayland or X, never reads it. `window move`
puts windows already open: every one of the pid or the name `moonwater window` lists.

A window the hand moves or resizes is remembered by its name (terminal, klog, settings,
or the one `name NAME` gave it), and the next window of that name opens there: on the
same screen, found by its connector's name, or the nearest one left when that screen is
gone, at the size it had up to the screen's, and a step down and across from one already
there. A placement stands over it. The places are kept in /root/canvas.windows, written
whole and renamed over the last; `canvas windows` says them, `canvas windows forget`
empties them, `canvas windows remember off` stops it, and wipe keeps them with the
settings.

## Layout

| | |
| --- | --- |
| `kernel/` | kernel profiles, patches, firmware |
| `src/` | `lib.c` (the library, in assembly per architecture), `sh/` (shell and tools), `net/`, `canvas/` (desktop), `waterlink/`, `moonwater/` (the module), `build/` |
| `programs/` | the shell, the utilities and the monitor as programs |
| `test/` | `run` (lanes and seams), `differential.py`, `checks.c` |
| `.github/` | `SECURITY.md` (threat model, tiers, evidence), `FIELD_NOTES.md` |

## Tests

The whole suite is hours of a machine, so a run names what changed:

```sh
sh test/run -j 8 --changed       the seams this tree has changed from main
sh test/run -j 8 --seam net      one seam (sh test/run --seams lists them)
sh test/run shell text           named lanes
sh test/run --all                every lane, asked for on purpose
```

Builds are cached under `~/.cache/moonwater-test` (`MOONWATER_TEST_CACHE=off`).

## "Moonwater"?

Some believe a bowl of water left out under a full moon absorbs celestial
energy. Much of this project is unproven and experimental, so the name fits.

## License

Apache-2.0

Everything in /kernel is strictly same license as the Linux Kernel: GPL-2.0
