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
moonwater                              the commands
moonwater status                       this session as one page: build, disks, Canvas, binds, settings
moonwater setup                        live or kept on a disk, and the installs found
moonwater setup install DISK [removable]  erase DISK and install Moonwater on it
moonwater setup update [DISK]          write this build over an install, keeping its data and settings
moonwater setup use [DISK]             run this build with an install's /bowls, /root and /home
moonwater setup live                   leave the disks alone this session
moonwater wipe                         forget /home and extra /root; keep the machine

moonwater bind                         the machine's events, and what each runs
moonwater bind EVENT [COMMAND]         one event; empty puts the default back
moonwater bind poweroff [COMMAND]      the power button [poweroff]
moonwater bind canvas on|off [COMMAND] when the desktop starts or stops
moonwater bind init                    what runs at every boot, with ids
moonwater bind init add "command"      run a command at boot, once the disks are settled
moonwater bind init remove ID|"command"
moonwater bind init mount on|off       mount the data partition over /bowls, /root and /home [on]
moonwater bind exit                    what runs at poweroff or reboot
moonwater bind exit add "command"      run a command before the disks go read-only
moonwater bind exit remove ID|"command"

moonwater canvas [on|off]              the desktop, and on which screens [on]
moonwater canvas log|terminal          open the kernel log window, or a terminal
moonwater bios [reboot]                whether firmware setup is on offer; reboot restarts into it (UEFI)
moonwater airplane [on|off]            every radio at once
moonwater brightness [N%|+N|-N]        the screen backlight
moonwater power [performance|balanced|powersave]  platform profile and CPU governor; kept across boots
moonwater cpu [boost|smt on|off]       turbo and SMT, kept across boots; cpu online|offline N for hotplug
moonwater charge [limit N|off]         where the battery stops charging (20 to 100); kept across boots
moonwater sleep | hibernate            suspend to RAM or to disk
moonwater keyboard [LAYOUT]            us uk de se no dk fi fr es it [us]
moonwater name [NEW|random]            what this machine is called; one is rolled at first boot, like space-wizard

moonwater wired [on|off]                the wired links, and which have carrier; off keeps them down across a reboot
moonwater wifi                         the radio, saved networks and networks in range, or why there are none
moonwater wifi on|off                  unblock or block wifi
moonwater wifi add SSID [PASSWORD|-]   remember and join a network; asks at a terminal, - reads stdin
moonwater wifi remove SSID             forget a saved network; leaves it if it is the joined one
moonwater bluetooth [on|off]           the bluetooth radio and remembered devices
moonwater bluetooth add NAME           remember a bluetooth device
moonwater bluetooth remove NAME        forget a remembered bluetooth device
moonwater priority internet [wired|wifi]  which link wins when both are up [wired]

moonwater time [sync]                  local time, UTC and NTP state; sync asks now
moonwater timezone [auto|ZONE|list]    IANA name, country code, +1 or POSIX TZ string [auto]
moonwater ntp [on|off]                 set the clock from the network [on]
moonwater ntp sampling [on|off]        five samples from each of three servers, the best agreeing one wins [on]

moonwater link                         who this machine is linked with and what each may do
moonwater link pair [NAME]             make a code, like space-wizard abc-def, and wait for a machine to use it
moonwater link NAME CODE               link to the machine called NAME, which is waiting on that code
moonwater link NAME [COMMAND...]       a terminal on NAME, or one command with its output and status here
moonwater link push NAME FILE PATH     a file to NAME, whole or not at all
moonwater link pull NAME PATH FILE     a file from NAME
moonwater link log NAME                follow NAME's kernel log
moonwater link add NAME KEY [HOST[:PORT]]   link by key, with no code
moonwater link remove NAME
moonwater link allow|deny NAME GRANT...  shell run log files (any file but the link's own)
moonwater link group [NAME [SECRET] [allow GRANT...]]   machines on one network that link themselves
moonwater link group leave NAME [forget]
moonwater link on|off                  listen on udp 22348, kept across boots [off]
```

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
`moonwater setup` says which of these a session is.

**Settings.** Binds, init and exit live in the boot image: set them on a live
stick and `setup install` carries them to the disk, while `setup update` keeps
the disk's own. Wifi, wired, bluetooth, internet preference, power and charge settings,
timezone, NTP, keyboard, name and link settings live in `/root` on the data
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
Open and WPA2 (including WPA2/WPA3 mixed) networks can be joined; WPA3-only,
802.1X and WEP networks are saved but not tried. A password on the command line
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
`/root/ntp.server` is believed on its own. The timezone is auto until set
by hand: on each new network the machine makes one HTTPS request to Cloudflare
and takes the zone it reports, at most once every three minutes. There is no
zoneinfo directory: each of tzdata's 420 zones maps to the POSIX rule in its
TZif footer (`src/build/zones.py` regenerates them), which is right from the
zone's last change on.

**Kiosks.** `moonwater wipe` empties `/home` and `/root` except the settings
above and the machine script overlay; `/bowls` survives. A kiosk is a machine
script whose `moonwater_init` wipes and then starts Chromium, Weston or any
bowl program -- the builtin script has this commented out. Start it in the
background: events wait until `moonwater_init` returns.

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
is a command), except the link's own key, machines, groups and the machine
script, which no peer reads or writes.

`link NAME` sends each keystroke in its own datagram at once; with a command it
passes stdin through and exits with the far command's status (255 if the link
failed). Both ends run this shell binary, on Moonwater or Linux. A direct
address is needed; there is no NAT traversal.

For machines nobody stands in front of, join a group instead:
`moonwater link group office` makes a 160-bit secret and prints the line to run
on the others. Members on the same local network find each other over mDNS
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

## Canvas and the terminal

Canvas, the desktop, is part of the kernel: a compositor that draws with the
CPU through DRM, so it works on any display the kernel can drive.
`moonwater canvas off` closes every window and leaves a shell on the text
console, from which another display server such as Weston can take the screen.

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

If a graphics driver draws a broken hardware cursor, boot with
`moonwater.cursor_plane=0` and Canvas draws the pointer itself.

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
default profile binds only a package's loader and libc; `--isolated`
gives package managers a complete namespace with the host's `/proc/sys` and
`/sys` read-only. `bowl expose` puts chosen programs on the
global path. A bowl is not a security sandbox: its programs run as root.

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
does not: root's passwd and group lines, a machine id, `/tmp/.X11-unix`. What was exposed is
recorded in the bowl, and remove undoes exactly that: launchers that still
name the bowl, the profile's packages and the dependencies nothing else needs,
the script.

The `desktop` session has been run on a stock Linux kernel with no display:
KWin `--virtual` and plasmashell on a session bus, and `desktop` itself as far
as KWin's DRM backend. It has not been booted on Moonwater with a display,
input devices, a running logind or PipeWire, and only the x86-64 Alpine
packages have been installed.

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

Four profiles in `kernel/profile/` choose how the build leans where safety and
the reference disagree:

| Profile | Tools and builtins | `MOONWATER_STRICT` |
| --- | --- | --- |
| `sec_reference` | all | 0: the reference exactly, holes and all |
| `sec_default` | all | 1: sanitise only what hostile input made dangerous |
| `sec_hardened` | all | 2: refuse where the default sanitises |
| `sec_locked` | a kiosk's allow list | 2 |

`sec_default` is in the default profile list, and it is the only tier that
promises bash, dash and GNU behaviour for anything a script does. The others
are choices, and what they refuse is refused on purpose. All four turn on Yama,
which Floodlight's restricted launches need. To pick one, name it last after
the rest of the default list:

```sh
sh build.sh debug_none limbo desktop wifi serial sec_hardened
```

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

The shell and network threat model, review checklist, and executable evidence
map live in [SECURITY.md](SECURITY.md),
[SECURITY_CHECKLIST.md](SECURITY_CHECKLIST.md), and
[SECURITY_TEST_MATRIX.md](SECURITY_TEST_MATRIX.md). The current, deliberately
non-certifying assessment and prioritized gap register are in
[SECURITY_REVIEW.md](SECURITY_REVIEW.md). An unchecked checklist
item is visible work, not a silently skipped test or a claim that the risk
does not apply.

```
sh test/run                     every lane
sh test/run shell text          named lanes only
sh test/run bench               the benchmarks
sh test/run bench --list        what there is to measure
```

The `boot` and `canvas` lanes need a built image (`MOONWATER_IMAGE=dist/bootx64.efi`)
and say so rather than pass quietly.

There are three files: `test/run`, `test/checks.c` (every C check and benchmark)
and `test/differential.py`. Nobody writes cases. Each program is declared as a
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