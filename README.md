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
moonwater canvas [on|off]              the desktop, and on which screens [on]
moonwater canvas background [COLOUR|default]  the desktop colour, rrggbb [1e1e1e]
moonwater canvas cursor [size [N|auto]|look [light|dark|auto]]  the pointer: size 1 to 4, colours [auto]
moonwater desktop [canvas|off|PROFILE]  what the machine starts as its desktop [canvas]
moonwater settings                     the settings window, on the desktop of the compositor
moonwater airplane [on|off]            every radio at once
moonwater brightness [N%|+N|-N]        the screen backlight
moonwater power [performance|balanced|powersave]  profile and CPU governor, kept across boots
moonwater sleep | hibernate            suspend to RAM or to disk
moonwater wired [on|off]               the wired links; off keeps them down
moonwater wifi [on|off]                the radio, saved networks and those in range
moonwater wifi remove SSID             forget a saved network, and leave it
moonwater bluetooth [on|off]           the radio and the remembered devices
moonwater time [sync]                  the clock; sync sets it and the zone now
moonwater timezone [ZONE|list]         IANA name, country, +1 or POSIX; manual
moonwater ntp [on|off]                 set the clock from the network [on]
moonwater keyboard [LAYOUT|list|xkb]   us uk gb de se sv no nb dk fi fr es it [us]
moonwater name [NEW|random]            what this machine is called, like space-wizard
moonwater dns [ADDRESS...|auto]        up to three servers names are asked of, or the network gives them [auto]
moonwater dns tls [on|off|only]        names over TLS to 1.1.1.1 and 9.9.9.9 first; only sends none in the clear [off]
moonwater link                         who this machine is linked with, and what each may do
moonwater link pair [NAME]             a code, and wait for the other machine to use it
moonwater link NAME CODE               link to the machine called NAME, which waits
moonwater link push NAME FILE PATH     a file here to PATH there, whole or not at all
moonwater link on|off                  the listener, kept across boots [off]
```

`moonwater` alone prints every command and `moonwater help VERB` the page of one.
A verb with no word after it says where its switch stands, to anybody; with one
it changes it and needs root, and a word it does not take is a usage page
(exit 2) and not a refusal (exit 1). `bowl` runs other distributions' programs
(`bowl profile desktop` installs KDE), and `main.moonwater.sh` is the machine
script that sets a machine up at boot.

`moonwater settings` opens the same command as a window on the desktop: the groups
(machine, canvas, time, network, power, boot, and all commands) are listed at the left
and the rows of the one chosen at the right, each row a setting a person flips, and
under All commands a row to every line of `moonwater help`, so no verb is out of its
reach. Tab goes between the groups and the rows, the arrow keys move in either, and
a press on a group or a row does what Enter does on it. A choice opens a list of
all its choices under it: Up and Down, a letter and Enter pick one and Escape leaves
the row as it was. Typing filters the rows of
every group; a command that ends the session or throws away what is kept is run by a
second Enter.

On the desktop, Super and Space open the launcher: a search bar for the desktop's programs, the programs of PATH and shell commands, which the same chord, Escape or a press outside it puts away.

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
