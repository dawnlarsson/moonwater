Plug-in cases for test/perf/bench.sh (round 2). Each case is a file NAME.sh (NAME: letters, digits, underscore) in this directory that defines one function
case_NAME MACHINE GPU PATH, run by bench.sh once per machine and gpu state. The function may use these from bench.sh:
  on MACHINE 'remote command'        run a command on the machine (output on stdout; keep under ~1000 bytes, push a script for more)
  $MW link push MACHINE LOCAL REMOTE push a file to the machine
  take MACHINE NAME / drop MACHINE   the machine's lock for the case (take fails when someone else holds a young one: return 0 then)
  put MACHINE GPU PATH SCENARIO METRIC VALUE   one row of the table (long form; bench-table.py renders it)
  say TEXT                           a message on stderr
  $OUT, $HERE, $ROUNDS, $KEYS, $GAP, $RSECS, $RURL, $URL   the run's settings and output dir
A case that cannot run on a machine calls: put MACHINE GPU PATH case-NAME skip reason 1  and returns.
Ground truth: a case should check the screen against a known picture (a frame counter or pattern encoded in pixels) and report
wrong_frames_* counts (wrong, older, torn, stale, black, seethrough, wrongplace, late), plus latency medians and p99 where it has them.
