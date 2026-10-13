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

## Use

`moonwater help` lists the system's own command: what runs at boot and at
poweroff, installing and updating, the network, `link`, Canvas. `bowl` runs
other distributions' programs, `bowl profile desktop` installs KDE.
`main.moonwater.sh` is the machine script that sets a machine up at boot.

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
