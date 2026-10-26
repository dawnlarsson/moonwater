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
what the events of the machine do. A selection:

```sh
moonwater status                       this session as one page
moonwater help [VERB]                  these commands, or the page of one
moonwater setup                        where this session runs, and the installs found
moonwater setup install DISK [removable] [yes]  erase DISK and put Moonwater on it
                                       removable takes a disk that says it is; yes
                                       is for a script, which cannot be asked
moonwater setup update [DISK]          write this build over an install, keeping its data
moonwater setup use [DISK]             run this build with the data of an install
moonwater setup live                   leave the disks alone this session
moonwater wipe [yes]                   forget /home and /root, keep the machine; yes
                                       is for a script, which cannot be asked
moonwater bind                         the events of the machine, and what each runs
moonwater bind EVENT [COMMAND]         one event; empty puts the default back
moonwater bind init                    what runs at every boot, with ids
moonwater bind init add "command"      run a command at boot, once disks settle
moonwater bind init remove ID|"command"  stop running one at boot
moonwater canvas                       whether Canvas is on, and the screens it draws on
moonwater desktop [on|off|PROFILE]     the desktop, now and at every start; off leaves the text console [on]
moonwater desktop start|stop           begin the chosen desktop now, or end its session
moonwater settings                     the settings window, on the desktop of the compositor
moonwater window                       the windows on the desktop: name, pid, place, size, title
moonwater window PLACE... COMMAND...   run COMMAND, its first window put where PLACE says:
                                       center, at X,Y, maximized, fullscreen, screen N,
                                       size WxH, name NAME (what it is remembered by)
moonwater window move PID|NAME PLACE...  put the windows of a program, or of a name, where PLACE says
moonwater latency [reset]              what the input handlers cost a report, and who has the display
moonwater debug                        what the desktop is doing and why (root): the card, the screens, the windows, the pointer
moonwater name [NEW|random]            what this machine is called, like space-wizard
moonwater keyboard [LAYOUT|list|xkb]   us uk gb de se sv no nb dk fi fr es it [us]
moonwater keyboard repeat delay [MS|default]  how long a key is held before it repeats, 100 to 2000 ms [250]
moonwater keyboard repeat rate [PER-SECOND|off|default]  repeats a second of a held key, 1 to 100, or none [30]
moonwater timezone [ZONE|list]         IANA name, country, +1 or POSIX; manual
moonwater timezone auto                from the network, one Cloudflare request per network joined [auto]
moonwater canvas scale [N|auto]        device pixels to a drawn pixel, 1 to 4, read as the desktop starts [auto]
moonwater canvas modes [largest|preferred]  which mode a screen is driven at [largest]
moonwater canvas arrange [NAME[@X,Y|@off]...|default]  where each screen is: names left to right, the first the primary [default]
moonwater canvas background [COLOUR|default]  the desktop colour, rrggbb [1e1e1e]
moonwater canvas cursor [size [N|auto]|look [light|dark|auto]]  the pointer: size 1 to 4, colours [auto]
moonwater canvas cursor shake [on|off]  a shake of the hand makes the pointer large for a moment [on]
moonwater canvas cursor shake delay [MS|default]  how long it stays large after the last shake, 100 to 10000 ms [1200]
moonwater canvas windows remember [on|off]  a window opens where the hand last put it, kept across boots [on]
moonwater canvas windows [forget]      where windows open again, from where the hand put them; forget empties it
moonwater mouse sensitivity [PERCENT|default]  how far the pointer goes for the hand, 10 to 500 percent [100]
moonwater mouse acceleration [on|off]  the pointer outruns a fast hand; off is one to one [on]
moonwater taskbar position [bottom|top|left|right]  the edge of the screen the taskbar sits on [bottom]
moonwater taskbar display [primary|all]  the screens the taskbar is on: the primary, or all, each listing its windows [primary]
moonwater taskbar text [center|edge]   on a left or right taskbar, which way the tops of the letters face: the middle of its screen or its edge [center]
moonwater ntp [on|off]                 set the clock from the network [on]
moonwater time [sync]                  the clock; sync sets it and the zone now
moonwater ntp server [NAME|auto]       who is asked first: a name or address [auto]
moonwater ntp sampling [on|off]        keep the lowest-delay sample of five [on]
moonwater ntp nts [on|off|only]        authenticated time (RFC 8915) first; only refuses plain [on]
moonwater ntp nts servers [NAME[:PORT]...|default]  who is asked for it, up to four
moonwater wired [on|off]               the wired links; off keeps them down
moonwater airplane [on|off]            every radio at once
moonwater priority [internet [wired|wifi]]  which link wins when both are up [wired]
moonwater dns tls [on|off|only]        names over TLS to 1.1.1.1 and 9.9.9.9 first; only sends none in the clear [off]
moonwater dns [ADDRESS...|auto]        up to three servers names are asked of, or the network gives them [auto]
moonwater security                     the built-in trust, and what was added to it or taken from it
moonwater security trust               every anchor, built in and added, by the SHA-256 of its key
moonwater security trust add FILE      trust a CA certificate, PEM or DER, as the built-in anchors are
moonwater security trust remove SHA256  stop trusting an anchor that was added
moonwater security block               every key refused, built in and added
moonwater security block add SHA256|FILE  refuse a key wherever it is: an anchor, a CA, a leaf, a CT log
moonwater security block remove SHA256  stop refusing a key that was added; a built-in one stays
moonwater security tls12 [on|off]      TLS 1.2 where a server offers nothing newer; off asks for 1.3 alone [on]
moonwater security aia [on|off]        fetch a CA a server left out, from where its certificate says [on]
moonwater security revocation [soft|hard]  hard refuses a certificate whose revocation cannot be told [soft]
moonwater security ct [on|off]         ask Certificate Transparency of every chain, not only at the tight tier [off]
moonwater security http [on|off]       plain http in wget and fetch, and redirects to it [on]
moonwater security packages [on|off]   programs of a bowl may run the package manager of its distribution [on]
moonwater security sudo [on|off]       moonwater through sudo or su in a bowl; on trusts root in a bowl as root
                                       of the machine, for single-user machines [off]
moonwater wifi [on|off]                the radio, saved networks and those in range [off]
moonwater bluetooth [on|off]           the radio and the remembered devices [off]
moonwater bluetooth add NAME           keep the name of a device, found once the radio is on
moonwater bluetooth remove NAME        forget a bluetooth device
moonwater usb [lock|unlock]            lock keeps what is plugged in now, and holds a new device until it is allowed [unlock]
moonwater usb class NAME [allow|deny]  what a new device no rule names may bring up: storage hid audio video net bluetooth hub other [deny]
moonwater usb allow|deny ID|PORT       let one device in, or hold it, now and at every plug;
                                       ID is VID:PID, or VID:PID/SERIAL as usb shows it
moonwater usb forget ID|PORT           drop the rule of one device
moonwater usb log [N]                  the last decisions, the newest last [20]
moonwater usb apply                    judge every device again now, as boot does
moonwater power [performance|balanced|powersave]  profile and CPU governor, kept across boots
moonwater brightness [N%|+N|-N]        the screen backlight
moonwater cpu [boost|smt on|off]       turbo and SMT, kept across boots
moonwater cpu online|offline N         hotplug one processor
moonwater charge [limit N|limit off]   where the battery stops charging, 20 to 100
moonwater battery [NAME|auto|none]     which battery the Battery row reads:
                                       auto is the first present one, none is no battery [auto]
moonwater bind init canvas [on|off]    start Canvas at boot [on]
moonwater bind init mount [on|off]     mount kept disks at boot [on]
moonwater sleep | hibernate            suspend to RAM or to disk
moonwater bios [reboot]                whether firmware setup is on offer; reboot goes
moonwater wifi add SSID [PASSWORD|-] [wpa2]  remember a network, joined once wifi is on; asks for
                                       the password, - reads it from stdin; wpa2 lets a tight
                                       tier join it without WPA3
moonwater wifi remove SSID             forget a saved network, and leave it
moonwater link                         who this machine is linked with, and what each may do
moonwater link pair [NAME]             a code, and wait for the other machine to use it
moonwater link NAME CODE               link to the machine called NAME, which waits
moonwater link push NAME FILE PATH     a file here to PATH there, whole or not at all
moonwater link on|off                  the listener, kept across boots [off]
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
(`bowl profile desktop` installs KDE), and `main.moonwater.sh` is the machine
script that sets a machine up at boot.

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

`moonwater usb lock` holds what somebody plugs in: a stick, a keyboard that types on its
own, a hub, a network, audio or camera function. Every device plugged in when it runs
becomes a rule in /root/usb.policy, with the classes it shows then (one the kernel holds
unconfigured is configured first to show them), and keeps working; one with no serial is
pinned to its place, the controller, root hub and port path, which survive a boot that
numbers the buses otherwise. A new device, one no rule names, or a class a known device never had (a stick that comes
back as a keyboard too) stays unauthorized until `usb allow` lets it in or `usb class`
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
force from /run until the machine restarts, and `usb` says why.

`moonwater settings` opens the same command as a window on the desktop: the groups
(machine, canvas, mouse, time, network, Wi-Fi, Bluetooth, USB, power, boot, and all commands) are
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

On the desktop, Super and Space open the launcher: a search bar for the desktop's programs, the programs of PATH and shell commands, which the same chord, Escape or a press outside it puts away.

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
the room they had. The desktop is drawn at one scale on every screen.

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
