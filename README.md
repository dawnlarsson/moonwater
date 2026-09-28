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
moonwater                              where this session runs: live, from a disk, or waiting for an answer
moonwater install DISK [--removable]   erase DISK and install Moonwater on it
moonwater update [DISK]                write this build over an install, keeping its data and settings
moonwater use [DISK]                   run this build with an install's /bowls, /root and /home
moonwater live                         leave the disks alone this session
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
moonwater keyboard [LAYOUT]            us uk de se no dk fi fr es it [us]

moonwater wifi                         the radio, saved networks and networks in range, or why there are none
moonwater wifi on|off                  unblock or block wifi
moonwater wifi add SSID [PASSWORD|-]   remember and join a network; asks at a terminal, - reads stdin
moonwater bluetooth [on|off]           the bluetooth radio and remembered devices
moonwater bluetooth add NAME           remember a bluetooth device
moonwater priority internet [wired|wifi]  which link wins when both are up [wired]

moonwater time [sync]                  local time, UTC and NTP state; sync asks now
moonwater timezone [auto|ZONE|list]    IANA name, country code, +1 or POSIX TZ string [auto]
moonwater ntp [on|off]                 set the clock from the network [on]
moonwater ntp sampling [on|off]        five samples from each of three servers, the best agreeing one wins [on]

moonwater link                         on or off, this machine's key, peers, what is open
moonwater link on|off                  listen on udp 22348, kept across boots [off]
moonwater link key                     this machine's public key
moonwater link pair NAME KEY [HOST[:PORT]]
moonwater link join NAMESPACE [SECRET] [allow GRANT...]
moonwater link leave NAMESPACE [forget]
moonwater link forget NAME
moonwater link allow|deny NAME GRANT...  run shell files log screen channels verbs
moonwater link shell NAME              a terminal on NAME
moonwater link run NAME COMMAND...     one command on NAME, with its output and status here
moonwater link push NAME FILE PATH     a file to NAME, whole or not at all
moonwater link pull NAME PATH FILE     a file from NAME
moonwater link log NAME                follow NAME's kernel log
moonwater link serve                   the listener in the foreground
```

Commands given to `moonwater` run as root through the shell, as if typed.
`bind init` runs in the background and keeps each command's output in
`/run/moonwater/init`; `bind exit` allows 10 seconds a command and 30 in all.

**Settings.** Binds, init and exit live in the boot image: set them on a live
stick and `install` carries them to the disk, while `update` keeps the disk's
own. Wifi, internet preference, timezone, NTP, keyboard and link settings live
in `/root` on the data partition, so `update` and `wipe` keep them.

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
passwords. Pair both ways, as with WireGuard: `link key` prints a machine's key
and `link pair` gives it to the other. A paired machine can do nothing until
allowed, e.g. `moonwater link allow laptop shell run`.

`link shell` sends each keystroke in its own datagram at once; `link run`
passes stdin through and exits with the far command's status (255 if the link
failed). Both ends run this shell binary, on Moonwater or Linux. A direct
address is needed; there is no NAT traversal.

For machines nobody stands in front of, join a group instead:
`moonwater link join office` makes a 160-bit secret and prints the line to run
on the others. Members on the same local network find each other over mDNS
(`_waterlink._udp`) and pair themselves, under the grants their join line gave.
Machines announce only a port, under random labels; only the secret's
600,000-round PBKDF2 result is stored. Join on the live stick before
`install` and the machine is in the group from its first boot.

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
Moonwater's own coreutils, util-linux and shell (which runs bash and dash
scripts). The default profile binds only a package's loader and libc;
`--isolated` gives package managers a complete namespace with the host's
`/proc/sys` and `/sys` read-only. `bowl expose` puts chosen programs on the
global path. A bowl is not a security sandbox: its programs run as root.

`bowl setup` checks for room before downloading. On a live stick bowls live in
memory; `moonwater install` puts them on a data partition.

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

## Tests

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
