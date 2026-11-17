#!/bin/sh
# fc-run.sh SCRIPT ARGS...: the helper loop that answers machine asks outside the bowl (as run2.sh), without Chromium, then SCRIPT
# under the bowl's python3. For framecode.py and other clients that need no browser. Each run has its own token file, so a loop
# from an earlier run ends on its own and never races this one for the asks.
tok=/tmp/cperf/helper.$$
: > $tok
rm -f /tmp/cperf/cmd.*
(while [ -e $tok ]; do for c in /tmp/cperf/cmd.*.sh; do [ -e "$c" ] || continue; sed "s#^moonwater #${MW:-moonwater} #" "$c" > "$c.run"; rm -f "$c"; sh "$c.run" > "${c%.sh}.out" 2>&1; mv "${c%.sh}.out" "${c%.sh}.done"; rm -f "$c.run"; done; sleep 0.05; done) &
s=$1; shift
bowl ${BOWL:-/bowls/alpine} /usr/bin/python3 /tmp/cperf/$s "$@" 2>/tmp/cperf/fc.err
rm -f $tok
