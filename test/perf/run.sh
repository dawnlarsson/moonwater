#!/bin/sh
# Chromium on a live Canvas, measured with nobody at the machine, from a computer linked to it (moonwater link):
#
#     sh test/perf/run.sh MACHINE [NAME]
#
# Pushes this directory and test/wayland_chromium.py (the DevTools client) to MACHINE's /tmp/cperf and runs auto.sh there: a fresh
# Chromium with DevTools on 127.0.0.1:9222, the scenarios of auto.py (five sites loaded and scrolled, a link clicked, the window
# resized 800 to 1800 pixels and back, idle, a new tab), each with the page's frame gaps and the browser's answer time; the compositor
# and Chromium profiled the whole while (moonwater profile); moonwater debug's GPU and client lines before and after. The report is
# printed and kept at /tmp/cperf/runs/NAME/report.txt on MACHINE.
#
# MACHINE needs the chrome profile (bowl profile chrome install) and an Arch bowl with python3 at /bowls/arch for the driver.
# The other tools here, pushed with it:
#     sample.sh NAME SECONDS     every thread of the compositor, Chromium, Xwayland and Steam, five times a second: state, CPU ticks
#                                (user and system), the system call it is in
#     resident.py PATH [MB]      which parts of a sparse file are resident (mincore, touching nothing): a memfd as /proc/PID/fd/N
#     bootwatch.sh [LIMIT_KB]    Shmem and the processes holding the most shared memory twice a second from early in a boot; the
#                                compositor stopped (SIGSTOP) past the limit so the OOM killer has nothing to take. Push it to /root
#                                (kept across a reboot) and start it as soon as the link answers.
# The link joins a command's words and the far shell reads them again (as ssh does): give it one quoted string.
machine=${1:?usage: sh test/perf/run.sh MACHINE [NAME]}
name=${2:-run}
here=$(cd "$(dirname "$0")" && pwd)
link=${MOONWATER:-moonwater}
$link link "$machine" 'mkdir -p /tmp/cperf/runs' < /dev/null || exit 1
for file in cperf.py auto.py auto.sh sample.sh resident.py bootwatch.sh; do
        $link link push "$machine" "$here/$file" "/tmp/cperf/$file" < /dev/null || exit 1
done
$link link push "$machine" "$here/../wayland_chromium.py" /tmp/cperf/wayland_chromium.py < /dev/null || exit 1
$link link "$machine" "sh /tmp/cperf/auto.sh $name" < /dev/null
