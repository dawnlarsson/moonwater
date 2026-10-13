# Moonwater's machine script.
#
# Built into the kernel as the fallback; /root/main.moonwater.sh overlays it
# (a regular root-owned file, not group- or world-writable, not a symlink, at
# most 64 KiB). An edit takes effect on the next event, by sourcing the file
# again: keep work inside functions, since the top level runs again each time.
#
# Events are functions. moonwater_<event> runs for that event and owns its
# row (`moonwater bind` shows the line and refuses to change it); an event with
# an argument runs moonwater_<event>_<argument>, as moonwater_canvas_on below.
# moonwater_event, if defined, runs for every event with the name in $1.
# moonwater_init runs once after boot with the boot's verdict in $1 (live,
# disk <name>, ask ...) and replaces `moonwater bind init`'s list; it must
# come back, so anything long-lived goes in the background with &.
# moonwater_end runs as the machine stops. Everything runs as root with no
# terminal.
#
# Keys are functions too: key_ and a chord -- modifiers (ctrl, shift, alt,
# super) and a key joined by _ -- runs when the chord is pressed on the
# desktop. The key is a letter, a digit, f1 to f12, or one of enter space tab
# escape backspace delete insert home end pageup pagedown up down left right
# minus equal comma dot slash semicolon apostrophe grave backslash leftbrace
# rightbrace.
#
# A kiosk, for example:
# function moonwater_init {
#   moonwater wipe yes
#   chromium --kiosk --user-data-dir=/tmp/kiosk "$URL" &
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

function moonwater_canvas_on {
  moonwater canvas log
  desktop_start || moonwater canvas terminal
  moonwater desktop boot
}

function moonwater_canvas_off {
  desktop_stop
}

# The kernel's own desktop asks for a terminal at its Control-Shift-T.
function moonwater_desktop_terminal {
  if desktop_up; then
    desktop_terminal
  else
    moonwater canvas terminal
  fi
}

function key_ctrl_shift_t {
  desktop_terminal
}

# The desktop: the compositor, the kernel log and a terminal on it, under a
# supervisor that starts the compositor again when it ends (at once, after half
# a second, after two) and gives up to the kernel's desktop after four short
# lives. Nothing here is named moonwater_, which would make it an event.

function desktop_up {
  [ -e /run/moonwater/windows ] && [ -s /run/moonwater/compositor.pid ] && kill -0 $(cat /run/moonwater/compositor.pid) 2> /dev/null
}

# The first line of /root/font, a path, or nothing.
function desktop_named {
  desktop_try=
  [ -r /root/font ] && IFS= read -r desktop_try < /root/font
}

# The faces: /root/font for everything, else IBM Plex Mono (and its bold) for
# terminals and Inter for the tabs, else the first mono face found.
function desktop_font {
  desktop_face=
  desktop_bold=
  desktop_named
  for desktop_try in "$desktop_try" /usr/share/fonts/ibm-plex-mono/IBMPlexMono-Regular.ttf /usr/share/fonts/*/*Mono*Regular*.ttf /usr/share/fonts/*Mono*.ttf; do
    if [ -n "$desktop_try" ] && [ -r "$desktop_try" ]; then
      desktop_face=$desktop_try
      break
    fi
  done
  desktop_text=$desktop_face
  if [ "$desktop_face" = /usr/share/fonts/ibm-plex-mono/IBMPlexMono-Regular.ttf ]; then
    [ -r /usr/share/fonts/ibm-plex-mono/IBMPlexMono-Bold.ttf ] && desktop_bold=/usr/share/fonts/ibm-plex-mono/IBMPlexMono-Bold.ttf
    [ -r /usr/share/fonts/inter/Inter-Regular.ttf ] && desktop_text=/usr/share/fonts/inter/Inter-Regular.ttf
  fi
}

# A program on the desktop, from a subshell so it is not this shell's child to
# reap; its pid is kept so the session's end ends it.
function desktop_run {
  ("$@" & echo $! >> /run/moonwater/desktop.kids)
}

function desktop_wait_for {
  desktop_wait=0
  until [ -e "$1" ] || ! kill -0 $2 2> /dev/null || [ $desktop_wait -ge 100 ]; do
    sleep 0.05
    desktop_wait=$((desktop_wait + 1))
  done
}

# The kernel log, waited for so that it stacks under the terminal.
function desktop_log {
  desktop_font
  [ -n "$desktop_face" ] || return
  rm -f /run/moonwater/klog.up
  desktop_run /klog -f "$desktop_face" ${desktop_bold:+-b "$desktop_bold"} -w /run/moonwater/klog.up
  desktop_wait_for /run/moonwater/klog.up $(tail -n 1 /run/moonwater/desktop.kids)
}

function desktop_terminal {
  desktop_font
  if [ -n "$desktop_face" ]; then
    desktop_run /terminal -f "$desktop_face" ${desktop_bold:+-b "$desktop_bold"}
  else
    echo "desktop: no font in /root/font or /usr/share/fonts, so no terminal on the compositor" > /dev/kmsg
  fi
}

# The key_ functions, for the compositor to listen for, and the FIFO it names a
# pressed one on; the listener runs what it names.
function desktop_keys_list {
  rm -f /run/moonwater/keys /run/moonwater/keys.fire
  declare -F | while read -r desktop_word desktop_flag desktop_name; do
    case $desktop_name in
    key_*) echo "$desktop_name" ;;
    esac
  done > /run/moonwater/keys
  mkfifo -m 600 /run/moonwater/keys.fire
}

function desktop_keys_answer {
  while :; do
    while IFS= read -r desktop_pressed; do
      case $desktop_pressed in
      key_*) declare -F "$desktop_pressed" > /dev/null && "$desktop_pressed" ;;
      esac
    done < /run/moonwater/keys.fire
  done
}

# One compositor and what it carries, until the compositor ends.
function desktop_session {
  rm -f /run/moonwater/windows
  desktop_font
  desktop_keys_list
  setsid /compositor ${desktop_text:+-f "$desktop_text"} &
  desktop_compositor=$!
  echo $desktop_compositor > /run/moonwater/compositor.pid
  desktop_wait_for /run/moonwater/windows $desktop_compositor
  if [ -e /run/moonwater/windows ]; then
    echo "desktop: the compositor (pid $desktop_compositor) is up" > /dev/kmsg
  else
    echo "desktop: the compositor (pid $desktop_compositor) made no window table" > /dev/kmsg
  fi
  : > /run/moonwater/desktop.kids
  desktop_keys_answer &
  echo $! >> /run/moonwater/desktop.kids
  desktop_log
  desktop_terminal
  wait $desktop_compositor
  echo "desktop: the compositor (pid $desktop_compositor) ended, status $?" > /dev/kmsg
  kill $(cat /run/moonwater/desktop.kids) 2> /dev/null
  rm -f /run/moonwater/compositor.pid /run/moonwater/desktop.kids
}

function desktop_on {
  case $(moonwater canvas) in
  *"Canvas is on"*) return 0 ;;
  esac
  return 1
}

function desktop_supervise {
  desktop_fails=0
  while desktop_on; do
    desktop_began=$SECONDS
    desktop_session
    [ $((SECONDS - desktop_began)) -ge 30 ] && desktop_fails=0
    desktop_fails=$((desktop_fails + 1))
    # Canvas turned off is why it ended, which is no failure.
    desktop_on || break
    if [ $desktop_fails -ge 4 ]; then
      echo "desktop: the compositor ended $desktop_fails times in a row; Canvas has the card, and a terminal" > /dev/kmsg
      moonwater canvas terminal
      break
    fi
    case $desktop_fails in
    2) sleep 0.5 ;;
    3) sleep 2 ;;
    esac
  done
  rm -f /run/moonwater/supervisor.pid
}

# The compositor's desktop, unless the settings or moonwater.desktop=kernel say
# the kernel's, or there is no font to draw with.
function desktop_start {
  case $(moonwater desktop) in
  *"desktop canvas"*) ;;
  *) return 1 ;;
  esac
  case $(cat /proc/cmdline) in
  *moonwater.desktop=kernel*) return 1 ;;
  esac
  desktop_font
  if [ -z "$desktop_face" ]; then
    desktop_named
    echo "desktop: no font to draw with: /root/font names ${desktop_try:-no file}, which is not readable, and no mono face is under /usr/share/fonts; the desktop is the kernel's" > /dev/kmsg
    return 1
  fi
  if [ -s /run/moonwater/supervisor.pid ] && kill -0 $(cat /run/moonwater/supervisor.pid) 2> /dev/null; then
    return
  fi
  (desktop_supervise) > /dev/null 2>&1 &
  echo $! > /run/moonwater/supervisor.pid
}

# Waits up to three seconds for the compositor to be gone, so the card is
# Canvas's again when this returns.
function desktop_stop {
  kill $(cat /run/moonwater/supervisor.pid) 2> /dev/null
  desktop_pid=$(cat /run/moonwater/compositor.pid 2> /dev/null)
  kill $desktop_pid 2> /dev/null
  desktop_wait=0
  while [ -n "$desktop_pid" ] && kill -0 $desktop_pid 2> /dev/null && [ $desktop_wait -lt 60 ]; do
    sleep 0.05
    desktop_wait=$((desktop_wait + 1))
  done
  rm -f /run/moonwater/supervisor.pid /run/moonwater/compositor.pid
}
