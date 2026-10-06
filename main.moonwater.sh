# Moonwater's machine script.
#
# The kernel build bakes it in as the
# fallback, so a machine whose disks never appear still has a working
# power button and reset. On a running system, copy it to
# /root/main.moonwater.sh to overlay the builtin without rebuilding:
# a regular file, owned by root, not group- or world-writable, not a
# symlink, at most 64 KiB.
#
# moonwater_init, moonwater_event and moonwater_end are optional.
# moonwater_event, when present, runs for every event. A function
# moonwater_<event> is the hard binding for that row: moonwater bind
# prints its line and SET is refused. moonwater_canvas owns both
# canvas on and canvas off ($1 is on or off); moonwater_canvas_on
# owns only that side. A literal case arm in moonwater_event still
# owns that event; *) does not. Events this file does not name still
# use `moonwater bind`.
#
# An edit takes effect on the next event: the running machine notices the
# file changed and sources it again, in place, without restarting. That
# re-runs whatever sits at the top level of this file, so keep the work
# inside functions -- a `weston &` written at the top level starts a second
# one on every edit.
#
# The builtin defines no moonwater_init, so the init list that
# `moonwater bind init add` keeps is what runs at boot. Defining
# moonwater_init here takes that row over: bind init is then refused
# and the list does not run, so define it only for a machine that
# should do exactly what this file says -- a kiosk: wipe, then one
# already-installed command. /bowls stays through a wipe, and so do
# wifi, bluetooth, the internet preference, timezone, ntp and keyboard
# on /root. The example below is that kiosk, commented out.

# Once, after boot has written a verdict. $1 is that line: live,
# disk <name>, or ask ...
#
# It has to come back. The same process reads the event queue, and it
# does not start reading until this returns, so anything that runs for
# as long as the machine is up goes in the background with & -- a
# desktop, a kiosk browser, a player. One that holds the process here
# is a machine whose power button, lid and keys are queued and never
# read.
#
# Everything here runs as root, once a boot, with no terminal and standard
# input /dev/null: no moonwater verb asks or waits for a person. A verb that
# cannot be taken back takes the word yes (moonwater wipe yes), a verb that
# is said twice leaves the machine as it was after the first, and a 1 from
# one that may not have its hardware or its network yet is `|| true`'s to
# ignore. The README's machine script section says what each may wait for.
# function moonwater_init {
#   case $1 in
#   live|disk*)
#     # On a stick that is to provision the machine it is put in (live, not
#     # disk): erases that disk, which is why it says yes.
#     # moonwater setup install nvme0n1 yes
#     # moonwater wipe yes
#     # After wipe, start whatever this machine is. A kiosk is one line
#     # of already-installed software, backgrounded, for example:
#     # chromium --kiosk --user-data-dir=/tmp/kiosk "$URL" &
#     # weston &
#     # /bowls/bin/exhibit &
#     # A saved network stays through a wipe. A password stays out of this
#     # file, which any user can read through /dev/spark, and out of ps: it
#     # is read from a file outside /root, which wipe empties, through
#     # standard input.
#     # moonwater wifi add "ssid" - < /bowls/ssid.pass || true
#     # moonwater wifi on
#     # moonwater bluetooth on
#     # moonwater priority internet wired
#     # moonwater timezone Europe/Stockholm
#     # moonwater keyboard se
#     # moonwater dns 9.9.9.9 1.1.1.1
#     # Pair by itself with every machine on the local network in the
#     # group: run `moonwater link group office SECRET allow shell run` once,
#     # as root, before install -- the secret then lives only in /root, and
#     # this line, which any user can read through /dev/spark, names the
#     # group and not the secret.
#     # moonwater link group office
#     ;;
#   esac
# }

function moonwater_poweroff {
  poweroff
}

function moonwater_reset {
  reboot
}

function moonwater_ctrl_alt_delete {
  reboot
}

# Canvas started or stopped; $1 is on or off. Canvas opens no window by
# itself: these are the windows a desktop starts with, at boot and after
# `moonwater canvas on`. Defining this function owns the canvas rows, so
# `moonwater bind canvas on|off` is refused while it is here.
#
# `moonwater desktop boot` is the desktop `moonwater desktop` says: nothing for
# Canvas, Canvas off for off, or a bowl profile's session started over the
# windows above it. It does its work once for a boot, in the background, and
# comes back at once.
function moonwater_canvas {
  case $1 in
  on)
    moonwater canvas log
    moonwater canvas terminal
    moonwater desktop boot
    ;;
  esac
}

# Every event, including those with a function above. $1 is the name;
# canvas, tablet, headphone and dock pass $2 on or off.
function moonwater_event {
  :
  # mute)
  # volume_up / volume_down
  # brightness_up / brightness_down
  # lid_close / lid_open
  # sleep
  # micmute
  # rfkill
  # tablet / headphone / dock  ($2 is on or off)
  # resume
  # recover  (last event did not finish; not a bind row)
}

# The machine is stopping; filesystems still write.
# function moonwater_end {
#   :
# }
