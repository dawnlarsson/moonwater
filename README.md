<img width="1153" height="160" alt="Dawning Linux Header-2" src="https://github.com/user-attachments/assets/675f22bb-fc92-43a2-9ba5-d957ee0d4d56" />

<br><br>

Moonwater is a research Linux distribution: think "Linux++". The foundations of
userspace -- the shell, coreutils, util-linux, the desktop and its terminal --
live in the Moonwater kernel module, so the system has a fixed, working base
whatever else is installed. Performance and latency are much of the point, and
the whole system is meant to stay under 15 MB.

## The `moonwater` command

`moonwater` manages the system itself: where it lives, what runs at boot, and
what the machine's events do.

```sh
moonwater status                       this session as one page
moonwater help [VERB]                  these commands, or one command's
moonwater setup                        where this session runs, and the installs found
moonwater setup install DISK [removable] [yes]  erase DISK and put Moonwater on it
                                       removable takes a disk that says it is; yes
                                       is for a script, which cannot be asked
moonwater setup update [DISK]          write this build over an install, keeping its data
moonwater setup use [DISK]             run this build with an install's data
moonwater setup live                   leave the disks alone this session
moonwater wipe [yes]                   forget /home and /root, keep the machine; yes
                                       is for a script, which cannot be asked

moonwater bind                         the machine's events, and what each runs
moonwater bind EVENT [COMMAND]         one event; empty puts the default back
moonwater bind poweroff [COMMAND]      the power button [poweroff]
moonwater bind canvas on|off [COMMAND]  when the desktop starts or stops
moonwater bind init                    what runs at every boot, with ids
moonwater bind init add "command"      run a command at boot, once disks settle
moonwater bind init remove ID|"command"  stop running one at boot
moonwater bind init mount [on|off]     mount kept disks at boot [on]
moonwater bind exit                    what runs at poweroff or reboot
moonwater bind exit add "command"      run a command before the disks go read-only
moonwater bind exit remove ID|"command"  stop running one at the stop

moonwater canvas [on|off]              the desktop, and on which screens [on]
moonwater canvas log|terminal          open the kernel log or a terminal
moonwater canvas scale [N|auto]        device pixels to a drawn one, 1 to 4 [auto]
moonwater canvas modes [largest|preferred]  which mode a screen is driven at [largest]
moonwater desktop [canvas|off|PROFILE]  what the machine starts as its desktop [canvas]
moonwater desktop start|stop           begin the chosen profile now, or end its session
moonwater latency [reset]              what the input handlers cost a report, and who has the display
moonwater airplane [on|off]            every radio at once
moonwater brightness [N%|+N|-N]        the screen backlight
moonwater power [performance|balanced|powersave]  profile and CPU governor, kept across boots
moonwater cpu [boost|smt on|off]       turbo and SMT, kept across boots
moonwater cpu online|offline N         hotplug one processor
moonwater charge [limit N|off]         where the battery stops charging, 20 to 100
moonwater sleep | hibernate            suspend to RAM or to disk
moonwater bios [reboot]                whether firmware setup is on offer; reboot goes

moonwater wired [on|off]               the wired links; off keeps them down
moonwater wifi [on|off]                the radio, saved networks and those in range
moonwater wifi add SSID [PASSWORD|-]   remember a network and join it; asks for
                                       the password, - reads it from stdin
moonwater wifi remove SSID             forget a saved network, and leave it
moonwater bluetooth [on|off]           the radio and the remembered devices
moonwater bluetooth add NAME           keep a device's name; the radio goes on for what pairs
moonwater bluetooth remove NAME        forget a bluetooth device
moonwater priority [internet [wired|wifi]]  which link wins when both are up [wired]

moonwater time [sync]                  the clock; sync sets it and the zone now
moonwater timezone [ZONE|list]         IANA name, country, +1 or POSIX; manual
moonwater timezone auto                from the network, one Cloudflare request
                                       per network joined [auto]
moonwater ntp [on|off]                 set the clock from the network [on]
moonwater ntp server [NAME|auto]       who is asked first: a name or address [auto]
moonwater ntp sampling [on|off]        keep the lowest-delay sample of five [on]

moonwater keyboard [LAYOUT|list|xkb]   us uk gb de se sv no nb dk fi fr es it [us]
moonwater name [NEW|random]            what this machine is called, like space-wizard
moonwater dns [ADDRESS...|auto]        up to three servers names are asked of, or the network's [auto]

moonwater link                         who this machine is linked with, and what each may do
moonwater link pair [NAME]             a code, and wait for the other machine to use it
moonwater link NAME CODE               link to the machine called NAME, which waits
moonwater link NAME [COMMAND...]       a terminal on NAME, or one command with its status
moonwater link push NAME FILE PATH     a file here to PATH there, whole or not at all
moonwater link pull NAME PATH FILE     PATH there to a file here
moonwater link log NAME                follow NAME's kernel log
moonwater link add NAME KEY [HOST[:PORT]]  link by key, with no code
moonwater link remove NAME             stop knowing it
moonwater link allow|deny NAME GRANT...  shell run log files (any file but the link's own)
moonwater link group [NAME [SECRET|-] [allow GRANT...]]  machines on one network that link themselves
moonwater link group leave NAME [forget]  stop, and forget the group's key
moonwater link on|off                  the listener, kept across boots [off]
moonwater link port [N|auto]           the udp port it takes, and `link add` assumes [22348]
```

`moonwater` alone prints these, and `moonwater help VERB` one command's. A verb
with no word after it says where its switch stands, to anybody; with one it
changes it and needs root, and a word it does not take is a usage page (exit 2)
and not a refusal (exit 1). The keyboard layouts `gb`, `sv` and `nb` are `uk`,
`se` and `no` by their country's other codes, and `keyboard xkb` says the
layout in XKB's name (`gb` for `uk`), which is what a compositor from a bowl
reads as `XKB_DEFAULT_LAYOUT`. `boot`, `ask` and `machine` are
init's and the first terminal's, started by them and not typed by hand.

Names are asked of the resolver the network's DHCP lease names, and of nothing
behind it: `moonwater dns ADDRESS...` keeps up to three of your own instead
(`/root/dns`, written to `/etc/resolv.conf` now, at every boot and at every
lease), and `dns auto` goes back. 1.1.1.1 is written only for a network whose
lease names no resolver, and is asked by a lookup only when the file names none.

Commands given to `moonwater` run as root through the shell, as if typed.
`bind init` runs in the background and keeps each command's output in
`/run/moonwater/init`; `bind exit` allows 10 seconds a command and 30 in all.

**Live and installed.** A machine started from the stick is a live session:
Moonwater runs from memory and nothing is kept after power off. `moonwater
setup install DISK` erases DISK and writes this Moonwater on it with a data
partition for `/bowls`, `/root` and `/home`, and a session started from that
disk keeps them there; a disk that says it is removable, a USB stick, is
installed only with the word `removable`. When a stick finds an install of
another build, `setup use` runs this build on the disk's data, `setup update`
writes this build onto the disk first, and `setup live` leaves the disk alone.
The same three are asked of an install of this very build that is not known to
be the one the session started from (more than one disk has it, or the only one
is on USB or Thunderbolt or says it is removable, or `MOONWATER_STRICT` is
tight): every copy of a release says the same build, and a stick pushed in
says it too. `moonwater setup` says which of these a session is.

**Settings.** Binds, init and exit live in the boot image: set them on a live
stick and `setup install` carries them to the disk, while `setup update` keeps
the disk's own. Wifi, wired, bluetooth, internet preference, power and charge settings,
timezone, NTP, keyboard, desktop, name and link settings live in `/root` on the data
partition, so `setup update` and `wipe` keep them. A machine gets its name the first
time it boots, from a live stick too, and `setup install` carries it to the disk.

**Events.** Besides the power button and Canvas, `bind` covers `reset`, `mute`,
`micmute`, `volume_up`/`down`, `brightness_up`/`down`, `lid_close`/`open`,
`sleep`, `resume`, `rfkill`, and `tablet`, `headphone` and `dock` on/off. Bound
keys are not grabbed; an unbound key still types. A PC case's reset button is
wired to the hardware and cannot be bound.

**Wifi.** Bare `moonwater wifi` lists networks strongest first (`*` joined,
`+` saved). When wifi cannot work it says why in one line -- no hardware, no
driver, missing firmware, rfkill, switched off, or why the last join failed.
Open and WPA2 (including WPA2/WPA3 mixed) networks can be joined, WPA2 with
CCMP and plain PSK, no protected management frames required; WPA2 that asks
for TKIP (`TKIP`), for PMF (`PMF`) or for FT or PSK-SHA256 key management
(`FT/256`), WPA3-only, 802.1X and WEP networks are saved but not tried, and
are listed under those names. The machine's own rejoin leaves a network that was
not there, or refused the password, alone for three seconds, then twice as long
each time to a minute, and scans for it at most every thirty seconds. A password on the command line
shows in `ps`, so leave it off or pipe it with `-`. The image carries the
firmware its wifi and bluetooth drivers load, fetched from linux-firmware at a
pinned commit and checked against pinned SHA-256s; no blob is in this
repository.

**Time.** NTP runs from boot until turned off, walking several public servers
until the kernel reports the clock synchronised. NTP sampling takes five
samples a query and keeps the one with the fastest round trip, RFC 5905's clock
filter, so a queueing spike never sets the clock; it also asks three servers
and believes the one with the least root distance among those whose answers
agree, so one wrong server cannot set it either. A server named in
`/root/ntp.server` is asked first, but a step past two seconds still needs a
second server to agree when the clock already has a plausible wall time. The
timezone is auto until set by hand: on each new network the machine makes one
HTTPS request to Cloudflare and takes the zone it reports, at most once every
three minutes. There is no zoneinfo directory: each of tzdata's 420 zones maps
to the POSIX rule in its TZif footer (`src/build/zones.py` regenerates them),
which is right from the zone's last change on.

**Kiosks.** `moonwater wipe yes` empties `/home` and `/root` except the settings
above and the machine script overlay; `/bowls` survives. A file system mounted
below `/home` or `/root` is left as it is, and a `/home` or `/root` that is a
symlink is refused. A kiosk is a machine script whose `moonwater_init` wipes
and then starts Chromium, Weston or any bowl program -- the builtin script has
this commented out. Start it in the background: events wait until
`moonwater_init` returns.

## Link

`moonwater link` is ssh by key over waterlink (`src/waterlink/`): UDP datagrams
sealed with AES-128-GCM after a Noise IK handshake, with no users and no
passwords. Every machine has a name (`moonwater name`), and two are linked in
a minute: `moonwater link pair` on one prints its name and a six-symbol code,
`moonwater link space-wizard abc-def` on the other uses them, and from then on
`moonwater link space-wizard` is a terminal there and
`moonwater link space-wizard uname -a` one command, run as root on a machine
that allowed it. A code works once, for five minutes, from a machine on the
same network (found over mDNS), and the two machines then may use each other's
terminal, commands, files and log. `link pair space-wizard` lets in only that
name. A code is thirty bits stretched by 600,000 rounds of PBKDF2, so it holds
for the minutes it lives and no longer.

Without a code, link by key, both ways, as WireGuard does: `link key` prints a
machine's key and `link add NAME KEY HOST` gives it to the other. A machine
added so can do nothing until allowed, e.g. `moonwater link allow laptop shell
run`; `moonwater link` shows each machine's grants. `files` is push and pull of
any file root has, so it is as good as `run` (a file pushed over a boot script
is a command), except the link's own key, machines, groups, stamps, state, lock and the
machine script, which no peer reads or writes. A pulled file has the far file's mode
(under this machine's umask, and without setuid, setgid or sticky bits).

`link NAME` sends each keystroke in its own datagram at once, and a paste in
full frames; with a command it passes stdin through and exits with the far
command's status (255 if the link failed or its stdin could not be read, 141 if
whoever reads its output went away, which also hangs up on the command). Both ends run this shell binary, on Moonwater or Linux. A direct
address is needed; there is no NAT traversal. The listener takes udp 22348
unless `moonwater link port N` (1 to 65535, kept in `/root/link.port`) says
another, read when it starts; `link add NAME KEY HOST` means that port too, and
`link add NAME KEY HOST:PORT` says where a machine that listens elsewhere is.

For machines nobody stands in front of, join a group instead:
`moonwater link group office` makes a 160-bit secret and prints the line to run
on the others (`link group office -`, which takes the secret from standard
input or asks for it with the echo off); one of your own is eight characters or
more, and under twenty is said to be guessable. A secret given on the line is
in `ps` while the command lives, and the shell does not keep that line in its
history (`wifi add SSID PASSWORD` is not kept either; a pairing code is, it is
good once and for five minutes). The history rule is a denylist for a
`moonwater` named as such, behind `sudo`, `env` and the like: not `sudo -u root`,
`bash -c`, `ssh HOST moonwater`, a function or an alias, or a verb after twelve
words. `MOONWATER_STRICT` tight refuses both argument forms.
Members on the same local network find each other over mDNS
(`_waterlink._udp`) and link themselves, under the grants their group line
gave. Machines announce only a port, under random labels; only the secret's
600,000-round PBKDF2 result is stored. Join on the live stick before
`setup install` and the machine is in the group from its first boot.

## The machine script

`main.moonwater.sh` at the repository root is the machine script: its hooks
(`moonwater_init`, `moonwater_end`, `moonwater_event`), per-event functions,
and the poweroff and reset fallbacks. The kernel bakes it into the module;
`CONFIG_MOONWATER_MACHINE_SCRIPT` can point elsewhere. Copy it to
`/root/main.moonwater.sh` to override the builtin without rebuilding. That file
must be a regular root-owned file, not group- or world-writable, not a symlink,
and at most 64 KiB, or the builtin stays and the kernel log says why.

An event the overlay defines -- `function moonwater_mute`, or a `mute)` arm --
belongs to the script, and `moonwater bind mute` points there
(`mute: /root/main.moonwater.sh:8`) and refuses to change it. Defining
`moonwater_init` or `moonwater_end` takes over the init or exit list the same
way. A running machine picks up a changed file on its next start.

### Scripting the machine

Every `moonwater` verb and `bowl` work from the machine script, from `bind init`
and `bind exit` lines, and from anything else that has no one at it. The script
runs as root, once a boot, after the boot has said what the session is and
`/root` is the disk's: with no terminal, standard input `/dev/null`,
`TERM=dumb`, `HOME=/root` and `LANG=C.UTF-8`, and nothing it runs reads or waits
for a person. Output into a pipe or a file has no colour in it.

An answer is 0 when the verb did what it was asked or showed it, 1 when it
refused or failed (a line of words says why), and 2 when it was not a command
(the usage page). A script is its own judge of a 1: `|| true` goes after a verb
that may not have its hardware or its network yet.

**What cannot be taken back is said yes to.** `moonwater setup install DISK
[removable] yes` erases DISK, and `moonwater wipe yes` forgets `/home` and
`/root`. Without the word, at a terminal, they ask as they always did (the
disk's name typed for an install, `yes` for a wipe); anywhere else, they
refuse at once with a 1 and say the line that would have been right, and
read nothing, so a pipe that happens to carry a disk's name is not an
answer. `setup update` and `setup use` keep the disk's data and ask nothing;
`ask` is the first terminal's question and is refused to anything else. An
install is idempotent where it counts: a stick of this build whose disk holds
an install of it attaches that disk at boot, and `setup install` of the disk
a session already keeps is refused.

**The same line twice is the same machine.** A settings verb that finds the
state as asked says so and writes nothing: `bind init add` of a line already in
the list (it is not added a second time), `bind EVENT COMMAND` of the command
the event runs, `bind init mount`, `wifi add` of a saved network with its
password (and, while it is joined and kept, no leaving and joining again),
`bluetooth add`, `dns`, `timezone`, `keyboard`, `ntp`, `priority`, `wired`,
`name NAME`, `link add`, `link allow`, `link group`, `link on`. What is a
refusal the second time, because it was said to be: `remove` of what is not
there, `canvas on` and `off` and `desktop stop` of what is as asked ("already"),
and `link pair`. What does its work again: `wifi on` joins again, `time sync`
and `timezone auto` ask the network, `name random` rolls another name and
`setup update` writes this build over the disk's again, keeping its settings.

**What waits, and how long.** A verb that changes a setting waits up to 30
seconds for another `moonwater` command that is changing one. `wifi on` and
`wifi add` take up to 8 seconds for a card to appear and 20 for the
association and 8 for the handshake, and come back at once when there is no
wireless hardware; `link on` and `link off` up to 3; `link group` with a secret a
second or so of key stretching and the 3; `link add` with a host name the
resolver's seconds; `setup install` and `update` look up
to 10 seconds of uptime for the stick the session started from; `bowl udev`
3. `time sync` asks up to seven servers two seconds a sample, and `timezone
auto` one HTTPS request, which is over a minute on a network that does not
answer: the machine does both by itself (`ntp` and `timezone auto` are on
unless turned off), so a script has no need of them. `bowl setup` and `bowl
profile` take what their downloads take, minutes. A slow line in
`moonwater_init` holds every event behind it (the power button, the lid, the
Canvas windows) until it returns: put what takes time in the background with
`&`, or in `bind init`, which does that for you.

Never from a script: `sleep`, `hibernate` and `bios reboot` (they leave),
`canvas off` (it closes the windows), `link pair` and `link NAME CODE` (they
wait five minutes for a person on the other machine and say so), `link NAME`
and `link log NAME` (a terminal on another machine, and a log that does not
end), and `ask`.

**Secrets stay out of the script.** The machine script is readable through
`/dev/spark`, and a command line is in `ps`. `moonwater wifi add SSID - <
/root/office.pass` reads the password from a file through standard input, and
`moonwater link group NAME - < /root/office.secret` reads a group's secret the
same way, and `moonwater link group NAME` with no secret joins the group this
machine already has: give it the secret once, as root, before the install. A `link
group` that finds no group by the name makes a new one with a secret of its
own and prints it. `wifi add SSID` with no password and no terminal saves an
open network, as it always did (and is refused, with the network saved, if the
air says it asks for one); `-` is how a script gives a password.

```sh
# /root/main.moonwater.sh
function moonwater_init {
  case $1 in
  live)                              # a stick that provisions the disk it is put in:
    [ -b /dev/nvme0n1 ] && moonwater setup install nvme0n1 yes   # erases it
    ;;
  disk*) ;;
  *) return ;;                       # a question is waiting for a person
  esac
  moonwater name kiosk-7
  moonwater timezone Europe/Stockholm
  moonwater keyboard se
  moonwater dns 9.9.9.9 1.1.1.1
  moonwater priority internet wired
  moonwater wifi add office - < /root/office.pass || true
  moonwater bluetooth add headset
  moonwater link group fleet allow shell run || true
  moonwater link add depot "$(cat /root/depot.key)" depot.lan
  moonwater canvas scale 2
  moonwater bind lid_close "moonwater sleep"
  moonwater brightness 60% || true   # a desktop has no backlight
  moonwater power balanced || true
  bowl setup alpine > /run/bowl.log 2>&1 &
  moonwater desktop boot
}

function moonwater_canvas {
  case $1 in on) moonwater canvas terminal ;; esac
}
```

The lines that cannot go in `moonwater_init` because it owns the list are
`bind init add` and `bind exit add`; a machine that wants those and a script
leaves `moonwater_init` undefined, and the list runs at boot with the same
rules and the same standard input. A kiosk that says `moonwater wipe yes` first
keeps what it reads (a password file, a key) outside `/root`, which a wipe
empties: in `/bowls`, which it does not.

## Canvas and the terminal

Canvas, the desktop, is part of the kernel: a compositor that draws with the
CPU through DRM, so it works on any display the kernel can drive. It is an
object of its own in `src/canvas`, linked beside the Moonwater core, and the
two meet in two headers: `src/canvas/canvas.h`, what the core asks of it, and
`src/moonwater/seam.h`, what it asks of the core.
`moonwater canvas off` closes every window and leaves a shell on the text
console, from which another display server such as Weston can take the screen.

A program that takes the display for itself, Weston or the KDE session of a
bowl profile, is master of the card: Canvas stops drawing, drops every key, and
draws the desktop again, windows and keyboard back, when that program lets go
or ends. `moonwater canvas` says when one holds it. A compositor that has the
card and no way to be used, with no input or no picture, would leave nothing at
the keyboard to work with, so Control-Alt-Backspace, pressed while another
program holds the card, ends the program and every other program of its
session, and Canvas has the screen back. A session that is init's own, which
is every shell on a console that was never given another, is not ended: only
the program, which a session started there has to be ended by hand. Pressed
with nobody holding the card it is the key it always was.

`moonwater desktop` is what the machine starts as its desktop: Canvas, the
default; `off`, which turns Canvas off as soon as it has started, for a machine
that is a text console; or a bowl profile, whose session the machine script's
canvas line starts once for each boot, after the disks are there, over Canvas's
own windows, so that a session that ends leaves a terminal where it was. The
profile has to be installed (`bowl profile desktop`), and the choice is kept in
`/root` like the keyboard's. `moonwater desktop stop` ends the session whatever
way it was started, from any terminal or from the serial port. A machine script
of one's own, in `/root`, starts nothing unless it runs `moonwater desktop boot`
in its `moonwater_canvas` function, as the builtin one does.

`moonwater latency` says where a report from a mouse or a keyboard spends its
time on this machine, as far as the machine can see: the two input handlers
Moonwater attaches to every device, Canvas's and the machine's key watch, are
counted by the kernel and one report in sixteen is timed, so what each costs a
report is a number and not a guess. While another program has the display
Canvas's handler returns at once for everything but the keys of the chord
above, with no lock taken and nothing woken, and the page says so. `latency
reset` zeroes the counts: a minute of moving the mouse is a reset, the minute
and a bare read. What comes after the kernel is the compositor's and the
screen's, and is read there. On a KDE desktop of a bowl profile,
`libinput debug-events` in a terminal reads the same devices as KWin and shows
each report's time; `qdbus6 org.kde.KWin /KWin supportInformation` names the
renderer, where `llvmpipe` means the CPU is drawing the desktop and not the
card; and `grep . /sys/module/usbhid/parameters/*poll` names the interval a
mouse and a keyboard are asked for a report at, which is 1 ms here and not the
8 ms most devices give (the kernel's command line carries `usbhid.mousepoll=1
usbhid.kbpoll=1`; `#> cmdline -usbhid.mousepoll=1` in a profile takes one out).

Canvas opens no window by itself. The machine script's `moonwater_canvas`
function opens the kernel log and a terminal when Canvas starts, at boot and
after `moonwater canvas on`; change it to start a desktop with something
else, or nothing. Control-Shift-T opens another terminal. The terminal
answers as `xterm-256color`, so nano, vim, less, htop, btop and other ncurses
programs draw as they do in xterm or tmux:

- 256 colours; true colour is drawn in the nearest of them.
- UTF-8: wide characters take two columns and combining marks none. Glyphs the
  built-in font lacks draw as a box.
- Full-screen programs get their own screen; resizing keeps the text in place,
  and lines scrolled away under a progress bar stay in scrollback.
- xterm's keys, mouse reporting, focus events, cursor shapes, synchronized
  output and queries.

Not there yet: pasting (there is no clipboard) and true colour kept as true
colour.

How Canvas draws is the machine's setting, changed live and kept across boots.
`moonwater canvas scale 2` draws everything the compositor owns at twice the
size and lays every window out again; `auto`, the default, asks the screens,
and trusts only a real panel (eDP, DisplayPort, HDMI, DVI, LVDS or DSI with a
size in its EDID that fits its mode): two from 150 dots to the inch, three
from 260, never so many that the desktop is left under 1280 by 640. A virtual
display reports a size that is made up, so it is one. `moonwater canvas modes
preferred` drives a real screen at the mode it marks preferred and not the
largest it lists. Both are kept in `/root` and put back when the machine starts;
`canvas.scale=2` and `canvas.modes=preferred` on the kernel command line give
the first frame the same.

If a graphics driver draws a broken hardware cursor, boot with
`moonwater.cursor_plane=0` and Canvas draws the pointer itself. Three more
kernel parameters switch things for diagnosis and are not settings (each is
writable in `/sys/module/moonwater/parameters/`): `moonwater.simd=0` draws with
plain stores instead of the wide ones, and `moonwater.dirhash_simd=0` hashes
directory names without the wide routine, for timing one against the other or
for a machine where either misbehaves; `moonwater.pm_dark=N` is the guest
lane's power-management switch (see the `canvas` lane in `test/run`), and no
machine needs it.

## Bowl

Bowl runs Alpine, Arch, Debian, Fedora or Nix package managers directly on the
Moonwater kernel, with no VM:

```sh
bowl setup <alpine | arch | debian | fedora | nix>
pacman -Syu package_name
```

Software can come from several distributions on one system, alongside
Moonwater's own coreutils, util-linux and shell (bash when it is called
bash, dash when it is called dash or sh, as Debian's `/bin/sh` is). The
default profile binds only a package's loader, libraries and data, and shows
`/etc` as Moonwater's with the bowl's under it (read-only; a name both have is
Moonwater's, which is how passwd, resolv.conf and the zone stay the machine's
and a package's own configuration is where it looks); `--isolated`
gives package managers a complete namespace with the host's `/proc/sys` and
`/sys` read-only. `bowl expose` puts chosen programs on the
global path. A bowl is not a security sandbox.

A program run in the default view is run as the user, uid and gid 1000, and
not as root, because software written for a distribution asks who it is:
Chrome, Chromium and the programs built on them (Electron, Steam's browser)
stop at "Running as root without --no-sandbox" and the flag that gets past it
turns their sandbox off. The user is root of this machine seen through a user
namespace: the files it makes are root's, every file root owns is its own, so
the home, the runtime directory and the devices a compositor opens need no
other mode, and what it lacks is what only the machine's root can do (mount, a
raw socket, the machine's settings). Root is what a package manager runs as
(they are isolated by their name), what `bowl --root ROOT program` or
`BOWL_ROOT=1` asks for, and the shell a bare `bowl ROOT` starts, which is the
way in for administering a bowl. A package manager started from a desktop
session says so and stops: it needs the machine's root, which a console shell
has. A machine that will not make the namespace runs the program as root.

`bowl setup` checks for room before downloading. On a live stick bowls live in
memory; `moonwater setup install` puts them on a data partition.

### Profiles

```sh
bowl profile                     # what there is, and what is installed
bowl profile desktop             # same as: bowl profile desktop install
bowl profile desktop remove
```

A profile is what a machine is for, built from components that each come from
the bowl best placed to supply them. Every program runs against its own bowl's
loader and libraries, and they meet over the host's `/run`, `/tmp` and `/dev`,
which every fast view shares, so a Wayland client from one bowl draws on a
compositor from another. `desktop` is KDE Plasma from Alpine, where it runs
on musl.

Install lands the component bowls it needs, has each distribution's own
manager add the packages, and does the glue: every program the bowl holds gets
a launcher in `/bowls/bin` and a link in `/bin`, so `dbus-run-session` finding
`dbus-daemon`, or Plasma starting `/usr/bin/kwin_wayland`, works without a
shell in between. A name `/bin` already has stays Moonwater's. It then writes
a session script named for the profile (`desktop`, also in `/bin`). Every
bowl launch also makes what a guest assumes the host has and Moonwater's `/etc`
does not: the passwd and group lines of root and the user, a machine id,
`/tmp/.X11-unix`. What was exposed is
recorded in the bowl, and remove undoes exactly that: launchers that still
name the bowl, the profile's packages and the dependencies nothing else needs,
the script.

A package manager that ends well has its bowl looked at again, and every
program it added gets a launcher and a link in `/bin` (a program that is
taken keeps the name it has), so a desktop that execs `/usr/bin/chromium-browser`
by name finds what `apk add chromium` put there without anybody having typed
it at a shell. `bowl bus` starts the system message bus of the first bowl that
has a `dbus-daemon`, as the user, with one policy for a machine of one user, on
`/run/dbus/system_bus_socket` where every client looks: the daemon as a
distribution ships it ends at "Unknown username" for the users it drops to.

The `desktop` session starts what the compositor needs of the machine before
it: `bowl udev`, `bowl bus`, and the keyboard's layout read when it starts. A compositor
finds its keyboards and mice through libinput, which asks udev what each of
`/dev/input` is and skips what udev has not named, so with no udev KWin drew
the desktop and could not be typed at. `bowl udev` starts the first udev a bowl
has, eudev's `udevd` from Alpine or systemd's from the others, in that bowl's
own view with its `/etc/udev`, has it name every device that is there, and
leaves it running for the ones plugged in later; run again it does nothing. A
profile installed before this keeps its old session script until `bowl profile
desktop` is run again.

The `desktop` session has been booted on Moonwater under QEMU with a virtio
display and USB keyboard and tablet, and typed and clicked at. It has not been
run with a logind or the system bus (Plasma says it cannot load a session
backend, and its shutdown and lock menus have nothing to ask), on a real GPU,
or with PipeWire's own devices, and only the x86-64 Alpine packages have been
installed.

## gzip

`gzip` is GNU gzip's command line: `-1` to `-9`, `--fast`, `--best`, and
GNU's refusals of everything else. One spelling GNU refuses is Moonwater's own:
`gzip --ultra` runs an optimal parser past `-9` (the parse behind libdeflate's
levels 10 to 12), which takes several times `-9`'s time for a few percent less
output, and every gzip reads what it writes. `-10` and up still mean their
last digit, as in GNU. The bytes never depend on how many CPUs ran.
`gzip -d` says what GNU gzip says, word for word, and keeps what it keeps: the
data of a truncated or corrupt stream up to where it broke, trailing zeros as
padding, other trailing bytes as a warning with status 2.

## xz

`xz` is xz 5.8's command line for `-0` to `-9`, `-e`, `-C`, `-T`, `--block-size`,
the branch converters (`--x86`, `--powerpc`, `--ia64`, `--arm`, `--armthumb`,
`--arm64`, `--sparc`, `--riscv`), `--delta` and `--lzma2` with all its options,
in xz's order and with its wording for everything it refuses; it reads every
stream those write, filters included, and any xz reads its output. Where xz
leaves a choice open Moonwater makes it for the ratio: blocks at `-0` to `-3`
are four dictionaries and at least 8 MiB, not one megabyte, and `-e` tries the
x86 converter on each block and keeps it when it pays. A stream that is one
block at `-4` or above runs its match finder on a second CPU when there is one.
The bytes never depend on how many CPUs ran.

## Building

The build is one C program, `src/build/build.c`, on this project's own
freestanding stack. A bare machine needs only a C compiler and an assembler:

```sh
cc -O2 -static -nostdlib -nostartfiles -fno-stack-protector -fno-builtin -w \
   -o build src/build/build.c
```

`sh build.sh` runs that line and hands over. The tool is also the build's
pieces:

```sh
./build config <profile ...>                    compose artifacts/.config
./build verify-config <config> [profile ...]    what the profiles did not get
./build asm <arch> <in.asm> <out.S>             one architecture out of a .asm
./build spark <source> <output> [debug]         link a spark program
./build freestanding [-v] [--run] [--watch] [source] [output]
./build floor [arch]                            prove the ISA floor
./build key <name>                              a value from artifacts/.config
./build config-header <config> <header>         the header a .config gives the programs
./build surface <config>                        the names that .config links into an image
./build switches [check]                        write or check the per-tool Kconfig switches
```

Nothing in it names this project; every path and setting can be overridden
with `--set name=value`. Building a kernel needs Linux and a case-sensitive
filesystem, so elsewhere point `--host` (or `MOONWATER_BUILD_HOST`) at a
machine that has them; QEMU still runs locally.

The kernel's built-in command line is every composed profile's `#> cmdline`
words, joined in the order the profiles are composed, so a profile adds words
and none replaces the string. A word spelled with a minus, `#> cmdline
-drm_client_lib.active=`, takes the same word out of what came before it:
`terminal`, `console` and `server` give the screen to the framebuffer console
that way and keep the rest. A `#> overrides CONFIG_NAME` line says the profile
means to win over an earlier one on that option, which is what the report of
profiles that disagree leaves out.

The bundled userspace is two Kconfig options, both on by default:

| `MOONWATER_SHELL` | `MOONWATER_UTILITIES` | Bundled payload |
| --- | --- | --- |
| `y` | `y` | One `/shell` image: PID 1, `/bin/sh` and every applet |
| `y` | `n` | PID 1 and the shell, without the utilities |
| `n` | `y` | Utilities only; `/init` must come from another initramfs |
| `n` | `n` | Nothing bundled |

`CONFIG_MOONWATER_UTIL_LINUX=n` drops the util-linux applets and
`CONFIG_MOONWATER_SHELL_MONITOR=n` the monitor; disabled applets are dropped
from the binary, not just from the path. The `.config` reaches the programs
as one header, `artifacts/moonwater_config.h`, and the build refuses an image
that does not carry that header's record.

Every tool has a switch of its own, `CONFIG_MOONWATER_TOOL_<NAME>` (`TOOL_TAC`,
`TOOL_WGET`), in a menuconfig menu under its category. One switched off is
compiled out of the shell's table and linked nowhere, so its name is not
found. `CONFIG_MOONWATER_TOOLS_ALL=n` turns the default of every one off, for
an allow list. `/init`, `/term` and `/moonwater` have no switch, because the
kernel and init run them by path. Builtins have the same,
`CONFIG_MOONWATER_BUILTIN_<NAME>` (`BUILTIN_ULIMIT`, `BUILTIN_BRACKET` for `[`),
with `CONFIG_MOONWATER_BUILTINS_ALL` as their default. POSIX's special builtins,
with `cd`, `true` and `false`, have none: they are the language, not commands
it runs. A name in both tables, such as `mount` or `kill`, is gone only when
both of its switches are off. The switches live in
`src/moonwater/Kconfig.switches`, which `./build switches` writes from
`src/sh/tools.inc` and the shell's builtin table.

### Security tiers

Five profiles in `kernel/profile/` choose how the build leans where safety and
the reference disagree, and how hard the kernel under it is:

| Profile | Tools and builtins | `MOONWATER_STRICT` | The kernel |
| --- | --- | --- | --- |
| `sec_reference` | all | 0: the reference exactly, holes and all | the baseline |
| `sec_default` | all | 1: sanitise only what hostile input made dangerous | the baseline |
| `sec_workstation` | all | 1 | hardened in every region that costs a program nothing it expects: not lockdown, not the 32-bit ABI |
| `sec_hardened` | all | 2: refuse where the default sanitises | every region hardened |
| `sec_locked` | a kiosk's allow list | 3: and what init can shut until the next boot | every region locked |

`sec_default` is in the default profile list, and it is the only tier that
promises bash, dash and GNU behaviour for anything a script does. The others
are choices, and what they refuse is refused on purpose. All of them turn on
Yama, which Floodlight's restricted launches need. To pick one, name it last
after the rest of the default list:

```sh
sh build.sh debug_none limbo desktop wifi serial sec_hardened
```

**The baseline.** The kernel is built from `allnoconfig`, where an option
nobody names is off whatever Kconfig says its default is. Until `sec_baseline`
existed the shipped kernel had no stack protector, no usercopy bounds check, no
`FORTIFY_SOURCE`, no slab freelist hardening, no CPU vulnerability mitigations
on x86-64 (`CPU_MITIGATIONS` is a prompt that `allnoconfig` turns off, and
every mitigation under it goes with it), no `W^X` on riscv64 and a null-page
floor of 4 KiB. Every tier composes `sec_baseline` and its architecture's file
(`sec_baseline.x64`, `.arm`, `.riscv`); it costs close to nothing on the paths
this project measures, and the mitigations are chosen at boot from what the
processor says it is vulnerable to. A profile names its parts with
`#> includes NAME...` and an architecture's own lines sit in `NAME.ARCH`
beside it, which is how a region asks for x86's indirect branch tracking
without every other build reporting it dropped.

**The regions.** A tier is every region at one level; a region is a part of the
hardening that can move alone. Name its profile after the tier to take that one
further (`sh build.sh ... sec_default sec_net_locked sec_mem_hardened`), or keep
one at a gentler level than the tier around it by composing its profile first.
Each region has a hardened and a locked level, and the locked one is the
hardened one and more, so name one or the other. The files are
`sec_<region>_hardened` and `sec_<region>_locked`; `sec_exec` and `sec_build`
have only `sec_exec_hardened` and `sec_build_hardened`.

| Region | Hardened | Locked adds |
| --- | --- | --- |
| `sec_mem` (kernel) | heap zeroed on allocation, locals zeroed, caches never merged, typed buckets, shuffled free lists, random stack offset per syscall, checked lists, W+X mappings warned about at boot | zeroed on free and every call-used register cleared at return, partitioned kmalloc caches, corruption and an oops panic (and reboot after 5 s), KFENCE |
| `sec_cpu` (kernel) | indirect branch tracking, call depth tracking, straight-line speculation barriers, user shadow stacks, TSX off; arm64 memory tagging | `mitigations=auto,nosmt pti=on` |
| `sec_surface` (kernel) | no ACPI table or SSDT replacement from userspace, no MSR/cpuid devices, no ioperm/iopl, no vsyscall | no 32-bit or x32 ABI, no LDT, no FUSE, no binfmt_misc, no watch queue, no POSIX message queues, no io_uring or file handle calls |
| `sec_dma` (kernel) | VT-d, AMD-Vi and SMMU, strict translated DMA, no passthrough, firmware PCI DMA off | no PCI hotplug, USB4, FireWire or PC card |
| `sec_lockdown` (kernel) | integrity lockdown from boot, signed modules only, Landlock | confidentiality lockdown |
| `sec_files` | `STRICT_FILES` 2 and find without `-delete` or `-fprint*` | 3 |
| `sec_text` | `STRICT_TEXT` 2 | 3 |
| `sec_net` | `STRICT_NET` 2 and no `wget` or `fetch` | 3 |
| `sec_host` | `STRICT_HOST` 2: a disk is asked about, `kptr_restrict` 2, io_uring refused, Yama 2, widest mmap randomisation | 3: no module loads, no ptrace attach (Yama 3), `hidepid` on /proc, the data partition `nosuid`, `/dev/shm` `noexec` |
| `sec_bowl` | `STRICT_BOWL` 2 | 3 |
| `sec_exec` | Floodlight sealed with every command-making switch off | (none) |
| `sec_build` | the image's own programs built with zeroed locals, probed stack frames, cleared call-used registers and the stack protector (+5.7% on a shell start, inside the noise on a compute loop) | (none) |

The userspace regions are what `MOONWATER_STRICT_FILES`, `_TEXT`, `_NET`, `_HOST`
and `_BOWL` set (`-1` follows the whole build's level, so a region left alone
changes nothing), and a tool names its region and never the whole level. The
sysctls init writes before it starts anything are the FILES and HOST regions'
(`fs.protected_*`, `kptr_restrict`, `dmesg_restrict`, `io_uring_disabled`,
`randomize_va_space`, at the tight level Yama and mmap's randomisation, at
the locked level Yama 3 and `modules_disabled`); they used to be written by the network
watcher a moment after boot, and not at all where it never started.
`/proc` and `/sys` are mounted `nosuid,nodev,noexec` and `/dev`
`nosuid,noexec` by the kernel module, at every tier.

What a tier's image is expected to say once it has booted is a table in
`lane_tier` (`test/run`), and `MOONWATER_IMAGE=dist/bootx64.efi
MOONWATER_TIER=hardened sh test/run tier` reads it back from a guest: the
sysctls init wrote, the mount flags, the lockdown line, the W+X page-table
check and the IOMMU's strict mode. `sh test/run switches` is the composition
half (what each tier and region names, and that a region keeps to its own
level).

What `sec_hardened` (`STRICT_TIGHT`) refuses where the default does what GNU
coreutils does:

| Tool | Default (GNU) | `sec_hardened` |
| --- | --- | --- |
| `mkdir`, `mkfifo`, `mknod`, `install`, `cp` | make an entry in a world-writable directory without the sticky bit | refuse it (`Permission denied`) |
| `cp` | writes through a destination that is a link; under `POSIXLY_CORRECT` makes the file a dangling link names | replaces the link itself, never touching what it points at, and always refuses a dangling one |
| `split`, `csplit`, and any output opened for writing (`tar -f`, `wget -O`) | write through an output name that is a link | replace the link with the new file |
| `chown -R -L`, `chgrp -R -L` | follow the links met in the tree | follow none below the operand |
| `cp -r` | makes the new directory where it goes and fills it | builds it in a private stage and publishes it whole |
| `install` | leaves the copy in place, mode 0600, when the owner asked for is refused | takes the copy away and says it was not published |
| `pinky -l` | copies each user's `~/.project` and `~/.plan` to the terminal as they are | leaves them out |
| `wget`, `fetch` and every other HTTP client in the tree | accept a `204` that declares `Content-Length` or `Transfer-Encoding` (its body is never read; the connection closes after the one response) and read a `205`'s content like any other body, as wget and curl do | refuse a `204` that declares any body framing and a `205` with a non-zero length, chunked or close-delimited content (RFC 9110 15.3.5, 15.3.6, RFC 9112 6.1) |

Every row but the HTTP one has a check in `floodlight_hardened` (`test/run`),
run against a shell built from `kernel/profile/sec_hardened`'s configuration;
the default half is the differential engine's, against GNU. The HTTP row's
tight half is `CHECK_net` built with `MOONWATER_STRICT` 2 (the `net-tight`
tally of `sh test/run net`) and the framing harness's second tier; its default
half is held against GNU wget and curl by `wget_mutation`.

Two differences are not tier choices. `kill` is util-linux's, not
coreutils' (`kill -l` takes several names, `-s0` is a signal), because
coreutils 9.11 does not build `kill` by default. `install` invoked as
`ginstall` reports itself as `install`.

Locales are read from the machine, not compiled in. Where `/usr/lib/locale`
(or its `locale-archive`) has a locale's files, `sort`, `ls`, `join`, `comm`,
`expr` and the shell's globs and `[[ a < b ]]` order text by that locale's
`LC_COLLATE` exactly as the C library's `strcoll` does, `sort -n -h -g` read the
locale's decimal point and thousands separator, and `date`, `numfmt` and friends
read `LC_TIME` and `LC_NUMERIC`. The image ships no locale data, so there every
locale is C and orders by byte; the C locale never pays for any of this. `uname`
answers `Moonwater` for `-o` and ends `-a` at the machine: Moonwater is not GNU
and does not say it is.

To take a single tool or builtin out, add a line to a profile of your own, or
use menuconfig:

```sh
# CONFIG_MOONWATER_TOOL_WGET is not set
# CONFIG_MOONWATER_BUILTIN_HISTORY is not set
```

`./build config-header <profile> <header>` shows what a profile turns off, and
`./build surface <profile>` lists the names it would link.

## Tests

The shell and network threat model, invariants, decisions, executable evidence
map and open gaps are all in [.github/SECURITY.md](.github/SECURITY.md). It is
deliberately non-certifying: a gap is visible work, not a silently skipped test
or a claim that the risk does not apply.

```
sh test/run                     every lane
sh test/run shell text          named lanes only
sh test/run bench               the benchmarks
sh test/run bench --list        what there is to measure
```

For a procedural network campaign with clang's libFuzzer, ASan and UBSan:

```
python3 test/network.py --output /tmp/moonwater-network --seeds 1 7 42 --runs 200000 --seconds 30
python3 -m unittest discover -s test -p test_network_cases.py
```

The output directory must be new. Each seed runs all 14 existing targets:
TLS DER, handshake and certificate verification; Waterlink authentication and
transport state; DHCP, SNTP, Wi-Fi EAPOL and scan records, DNS, netlink,
cryptography, bowl signatures, and HTTP in both tiers. Original fixtures stay
as controls alongside generated truncations, trailing data, and integer
boundaries. Generation rotates across fixtures and mutation classes, with
at most 4,096 additional inputs and 64 MiB per target. Reports record cap
exhaustion, actual executions, instrumented edges/features, sanitizer results,
and retained logs and corpora. The run or time limit stops each target,
whichever comes first; exit 2 means a required tool or target did not run.
Increase both budgets for longer campaigns. These counters measure hosted
production-code lifts; kernel, driver and live network integration still need
the `net` and `waterlink` lanes and guest tests. Passing is bounded evidence,
not a guarantee against compromise.
`sh test/run fuzz` also enables these procedural boundaries by default;
`MOONWATER_FUZZ_BOUNDARIES=0` keeps the original corpus for comparisons.

The loopback TLS integration paths can be added to a trace-PC source-coverage
record. This exercises the real shell client across the certificate-chain
matrix, 500 seeded hostile flights, 60 fragmented or cut delivery schedules,
and HTTPS downgrade cases, while retaining each exact binary and map:

```
python3 test/network_integration.py --output /tmp/moonwater-network-integration \
  --baseline /tmp/moonwater-coverage --mutations 500 --schedules 60
```

`--against` names a saved `coverage_report` JSON when a block-by-block delta is
wanted. The output directory must be new. The maps cover userspace in
`src/net/`; kernel, driver, namespace and physical-network behavior remains in
the `net`, `netem`, guest and hardware lanes. Exit 2 means one of the requested
integration harnesses did not run.

The DHCP and SNTP clients also have a hostile-server lane over namespace veth
pairs. It covers forged, stale, duplicated, delayed, dropped and refused DHCP
traffic, address conflicts, invalid SNTP origins, spoofed time, rate limiting
and large clock steps:

```
python3 test/differential.py --harness net_netem --shell /path/to/shell
```

The default requires the kernel's `sch_netem` support and adds deterministic
loss, reordering, duplication and jitter. `--plain-only` retains all protocol
and namespace cases on kernels without that scheduler, and reports the reduced
lane by its own name.

A few environment variables exist for the tests alone, and each says so where
it is read: `WATERLINK_PAIR_SECONDS` and `WATERLINK_REKEY_SECONDS` (shorten the
pairing window and the rekey interval, never lengthen them),
`WATERLINK_NO_SEGMENTS` and `WATERLINK_STATS` (measuring the link),
`MOONWATER_UTMP`, `MOONWATER_RFKILL_ROOT` and `MOONWATER_RFKILL_DEVICE` (fixture
paths, ignored when the real and effective user or group differ) and
`MOONWATER_STDBUF_LIBRARY`. Nothing on a machine sets them, and there is no
command for any.

The `boot` and `canvas` lanes need a built image (`MOONWATER_IMAGE=dist/bootx64.efi`)
and say so rather than pass quietly.

The main test runner is `test/run`, with C checks and benchmarks in
`test/checks.c` and procedural and differential harnesses in
`test/differential.py`. The network campaign adds `test/network.py` and its
bounded generator, `test/network_cases.py`. Each program is declared as a
grammar -- its options, values, operands and inputs -- and the engine runs the
system's tool and ours on the same inputs, comparing status, output, effects on
disk and diagnostics. Deliberate differences are pinned with a reason and fail
if they ever disappear.

## "Moonwater"?

Some believe a bowl of water left out under a full moon absorbs celestial
energy. Much of this project is unproven and experimental, so the name fits.

## License

Apache-2.0

Everything in /kernel is strictly same license as the Linux Kernel: GPL-2.0
