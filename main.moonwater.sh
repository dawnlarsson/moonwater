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

# A terminal, asked for at the keyboard (Control-Shift-T), by the compositor or
# by the kernel's own desktop: on the compositor when there is one, and on the
# kernel's desktop when there is not. It has to come back, as moonwater_init does.
function moonwater_desktop_terminal {
  if [ -S /run/moonwater/surface ] && [ -s /run/moonwater/compositor.pid ] && kill -0 $(cat /run/moonwater/compositor.pid) 2> /dev/null; then
    desktop_terminal
  else
    moonwater canvas terminal
  fi
}

# The desktop Canvas starts with: the compositor, which holds the card and
# draws windows; the panel, the kernel log and a terminal on it; and, under
# them, the kernel's own desktop, which has the card whenever the compositor
# does not.
#
# The compositor runs under a supervisor. When it exits or dies it is started
# again at once, then after half a second, then after two; a compositor that
# has ended four times without living half a minute is given up, and Canvas
# opens the kernel's terminal, so there is a shell on a screen. The keyboard
# does not depend on any of this: Control-Alt-Backspace ends the compositor and
# everything it started, and the supervisor starts it again; Control-Alt-Shift-
# Backspace turns Canvas off, to the text console, and the supervisor goes with
# it. A moonwater canvas on after that starts the desktop again.
#
# The panel, the kernel log and the terminal take their text from a font. The
# image carries two families under /usr/share/fonts (fonts/ in the repository,
# THIRD-PARTY for what they are): the terminal and the log draw with IBM Plex
# Mono, Regular and Bold, and the panel with Inter. /root/font, whose first line is a path, takes both
# instead, with no bold. A build without those files falls back to the first
# mono face in /usr/share/fonts for both. With none the compositor runs alone:
# its own panel, no terminal, and a line in the log saying why.
#
# moonwater.desktop=kernel on the kernel's command line, or /root/desktop
# naming anything but canvas, leaves the kernel's desktop as it was: its log
# and a terminal of its own.
#
# These helpers are not named moonwater_: a name that is would be an event.
function desktop_font {
  desktop_face=
  desktop_bold=
  desktop_text=
  desktop_try=$(cat /root/font 2>/dev/null)
  if [ -r "$desktop_try" ]; then
    desktop_face=$desktop_try
    desktop_text=$desktop_try
    return
  fi
  for desktop_try in /usr/share/fonts/ibm-plex-mono/IBMPlexMono-Regular.ttf /usr/share/fonts/*/*Mono*Regular*.ttf /usr/share/fonts/*Mono*.ttf; do
    if [ -r "$desktop_try" ]; then
      desktop_face=$desktop_try
      break
    fi
  done
  if [ -z "$desktop_face" ]; then
    return
  fi
  desktop_text=$desktop_face
  if [ "$desktop_face" = /usr/share/fonts/ibm-plex-mono/IBMPlexMono-Regular.ttf ] && [ -r /usr/share/fonts/ibm-plex-mono/IBMPlexMono-Bold.ttf ]; then
    desktop_bold=/usr/share/fonts/ibm-plex-mono/IBMPlexMono-Bold.ttf
  fi
  if [ -r /usr/share/fonts/inter/Inter-Regular.ttf ]; then
    desktop_text=/usr/share/fonts/inter/Inter-Regular.ttf
  fi
}

# Everything on the desktop is started here and by nothing else: the compositor
# holds the card and draws, and forks nothing on a person's behalf. A program it
# is asked for (Control-Shift-T) is the event moonwater_desktop_terminal, which
# the compositor raises and the machine answers by this file, as it answers the
# power button; and the kernel's own desktop raises the same event before it
# starts a terminal of its own. The pids are kept so that the end of a session
# ends what it started.
function desktop_run {
  "$@" &
  echo $! >> /run/moonwater/desktop.kids
}

# The kernel log, as a window on the compositor: started first, so that it sits
# behind the terminal, and waited for until its first picture is committed (it
# makes the file named by -w then), because the order in which two windows are
# made is the order they stack in.
function desktop_log {
  desktop_font
  if [ -z "$desktop_face" ]; then
    return
  fi
  rm -f /run/moonwater/klog.up
  desktop_run /klog -f "$desktop_face" ${desktop_bold:+-b "$desktop_bold"} -w /run/moonwater/klog.up
  desktop_wait=0
  until [ -e /run/moonwater/klog.up ] || ! kill -0 $! 2> /dev/null || [ $desktop_wait -ge 100 ]; do
    sleep 0.05
    desktop_wait=$((desktop_wait + 1))
  done
}

# A terminal on the compositor.
function desktop_terminal {
  desktop_font
  if [ -n "$desktop_face" ]; then
    desktop_run /terminal -f "$desktop_face" ${desktop_bold:+-b "$desktop_bold"}
  else
    echo "desktop: no font in /root/font or /usr/share/fonts, so no terminal on the compositor" > /dev/kmsg
  fi
}

# One compositor and what it carries, until the compositor is over.
function desktop_session {
  rm -f /run/moonwater/surface
  setsid /compositor &
  desktop_compositor=$!
  echo $desktop_compositor > /run/moonwater/compositor.pid
  desktop_wait=0
  until [ -S /run/moonwater/surface ] || ! kill -0 $desktop_compositor 2> /dev/null || [ $desktop_wait -ge 100 ]; do
    sleep 0.05
    desktop_wait=$((desktop_wait + 1))
  done
  echo "desktop: the compositor (pid $desktop_compositor) is up" > /dev/kmsg
  desktop_font
  : > /run/moonwater/desktop.kids
  if [ -n "$desktop_face" ]; then
    desktop_run /compositor grant panel /panel -f "$desktop_text"
  fi
  desktop_log
  desktop_terminal
  wait $desktop_compositor
  echo "desktop: the compositor (pid $desktop_compositor) ended, status $?" > /dev/kmsg
  kill $(cat /run/moonwater/desktop.kids) 2> /dev/null
  rm -f /run/moonwater/compositor.pid /run/moonwater/desktop.kids
}

function desktop_supervise {
  desktop_fails=0
  while :; do
    case $(moonwater canvas) in
    *"Canvas is on"*) ;;
    *) return ;;
    esac
    desktop_began=$SECONDS
    desktop_session
    if [ $((SECONDS - desktop_began)) -ge 30 ]; then
      desktop_fails=0
    fi
    desktop_fails=$((desktop_fails + 1))
    if [ $desktop_fails -ge 4 ]; then
      echo "desktop: the compositor ended $desktop_fails times in a row; Canvas has the card, and a terminal" > /dev/kmsg
      moonwater canvas terminal
      return
    fi
    case $desktop_fails in
    2) sleep 0.5 ;;
    3) sleep 2 ;;
    esac
  done
}

function desktop_start {
  case $(moonwater desktop) in
  *"desktop canvas"*) ;;
  *) return 1 ;;
  esac
  case $(cat /proc/cmdline) in
  *moonwater.desktop=kernel*) return 1 ;;
  esac
  if [ -s /run/moonwater/supervisor.pid ] && kill -0 $(cat /run/moonwater/supervisor.pid) 2> /dev/null; then
    return
  fi
  (desktop_supervise) > /dev/null 2>&1 &
  echo $! > /run/moonwater/supervisor.pid
}

function desktop_stop {
  kill $(cat /run/moonwater/supervisor.pid) 2> /dev/null
  kill $(cat /run/moonwater/compositor.pid) 2> /dev/null
  rm -f /run/moonwater/supervisor.pid /run/moonwater/compositor.pid
}

# Canvas started or stopped; $1 is on or off. Canvas opens no window by
# itself: these are what a desktop starts with, at boot and after
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
    desktop_start || moonwater canvas terminal
    moonwater desktop boot
    ;;
  off)
    desktop_stop
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
  # desktop terminal  (a request: Control-Shift-T, answered by moonwater_desktop_terminal)
  # recover  (last event did not finish; not a bind row)
}

# The machine is stopping; filesystems still write.
# function moonwater_end {
#   :
# }
