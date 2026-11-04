# Incident response

This file holds two things: the steps we follow when a public disclosure puts
Moonwater's network stack, keys or trust data at risk, and the log of every
incident handled that way. Each log entry gives the dates, the commits that
fixed it, and the tests that fail if the fix is removed. Add an entry in the
same session as the fix, and put the commit hashes in it before the session ends.

Times are UTC unless a `+02:00` follows. Commit times are the author dates in
`git log`, which carry the committer's offset.

## Procedure

1. Find out whether the data is ours. Search `src/net/anchors.inc` for the root
   or CA, `src/net/net.c` for the key or algorithm, `src/net/ct_logs.inc` for a
   log. A disclosure about a library we do not use (Brotli, WPS, TLS session
   resumption) gets a one-line "not applicable, because" in the log, with the
   grep that shows it.
2. Reproduce with a key or input anyone can make. The eMudhra keys were
   numbers below 256, so the test mints the same shape in `small_p384_key`
   (`test/differential.py`). A regression row that needs the vendor's secret
   proves nothing.
3. Contain at the narrowest point that works, in this order. Refuse the key by
   its SPKI hash in `tls_blocked_keys`; that survives a trust-store
   regeneration. Drop a root through `ANCHORS_LEFT_OUT` in the anchors
   regenerator, never by hand-editing `anchors.inc`, so the file still
   regenerates byte for byte. Do not rely on revocation or Certificate
   Transparency as the fix: both are off or soft-fail at the default tier, and
   the attacker is on the path.
4. Prove the fix catches the attack. Disable the new check in a scratch copy
   and confirm the new rows fail. A row that passes both ways tests the shape
   of the chain, not the check.
5. Commit to main, then run the net lane on the box from a clean export of
   `HEAD` (the working tree may hold someone else's edits). Fuzz a deep run on
   the parser the bug touched: `MOONWATER_FUZZ_SECONDS` alone does nothing,
   because every target is also capped at 20,000 runs; raise
   `MOONWATER_FUZZ_RUNS` as well. Run the mutation lane on the function you
   added (`--harness net_mutation --functions '^name$' --oracle full`) and kill
   every survivor in your own lines.
6. Write the entry below and list what is still open.

## Log

### MW-2026-129: a window's door, its wake, the clipboard's door or keys.fire held by its pipe's lock froze the compositor

| | |
|---|---|
| Status | Fixed 2026-11-04 |
| Class | Denial of service: an uninterruptible wait of the root compositor's only loop on a lock another process holds |
| Attacker | a window's program, or whatever holds an end of one of the compositor's FIFOs (root today: they are 0600, MW-2026-020); the machine script for keys.fire |
| Moonwater exposure window | from `c793b422` (2026-10-11 19:16 +02:00, the compositor and its doors) to the fixing commit |
| Fixed by | the commit that adds this entry (`src/canvas/compositor.c` `compositor_doors`) |

**What it was.** Every read, write and open of a pipe takes the pipe's lock first, whatever O_NONBLOCK says, and poll takes
none (the Wayland shim's clipboard had the same fault, MW-2026-113). A process with an end of a FIFO can keep that lock: a
splice into the pipe from a socket that has nothing to say holds it while it waits, and one out of it into a socket that
is full does too, and no signal frees the one that waits for it. The compositor's loop drained a window's door when it was
knocked on, opened the door again when it closed, wrote a byte to a window's wake whenever the window had news, drained
the clipboard's door, and opened and wrote keys.fire for a chord: a program holding its own door's or wake's lock (or the
script holding keys.fire's) stopped every frame, the pointer and the keys for as long as it liked.

**The fix.** No read, write or open of those FIFOs is made on the loop. Each row has a thread (and the clipboard's door and
keys.fire one between them) that drains its door, opens it again when it closes and writes its wake; the loop polls the
doors itself (poll takes no lock), takes what a knock says from the table at once, does not poll a door its thread has,
and hears through an eventfd when the thread is done: the row is read again then (a knock that came before the drain is
taken) and a row whose door closed is let go once its new door is open, so a program that takes the row finds one. A
chord's line goes into a ring the thread says to keys.fire. A held lock now holds that row's thread alone.

**Cost.** A program's wake is one thread hop later: from `compositor_knock` to the program's ppoll returning, 2.35 us at the
median and 5 to 6 us at p99 on the loop, against 4.5 to 4.6 us and 10 to 11 us through the row's thread (the box, two
pinned cores of CCD0, 20,000 wakes each, twice). A knock reaches the loop as before. 33 threads more, idle in ppoll.

**Tests that fail without it.** `CHECK_terminal` `check_compositor_doors_held`: with a door's and a wake's locks held by
splices from idle sockets, the loop's knock, its wake and a close of the door return at once (in a child the check waits
three seconds for). With the threads asked nothing (every drain and write on the loop, as before) the child is still in the
door's read after three seconds and is killed (1 failure, gcc). A headless compositor with the threads, run as root of a
user namespace, showed forty windows opened and closed in turn (rows let go and taken again through the threads' reopen),
eight at once, and a window after them all, and lived.

**Still open.** The wake's hop could be taken off a window's own path only with a wake that is no pipe (an eventfd passed to
the program), a change of the window protocol.

### MW-2026-128: a headless compositor's window table could be truncated under it

| | |
|---|---|
| Status | Fixed 2026-11-04 (hardening: root-only, a headless compositor) |
| Class | Denial of service: SIGBUS in the root compositor from a file truncated under its mapping |
| Attacker | whatever can write the table (root today, MW-2026-050) |
| Moonwater exposure window | from `0b5d97442` (2026-10-14 13:51 +02:00, the memfd for a compositor with a card) to the fixing commit |
| Fixed by | the commit that adds this entry (`src/canvas/compositor.c` `compositor_table`) |

**What it was.** A compositor with a card keeps its window table in a memfd sealed against shrinking and growing, linked at
`/run/moonwater/windows`. A headless one (`-H`, a test's) kept it in a plain file there, which anything that can open it
for writing can truncate: the compositor's next read of a page past the new end is SIGBUS, and it ends.

**The fix.** The table is the sealed memfd whether there is a card or not; the plain file is left for when there is no
memfd. What a card does with the memfd (udmabuf, handing it to `/dev/spark` for programs) is still only with a card.

**Tests that fail without it.** `CHECK_terminal` `check_compositor_table_sealed`: in a child that is root of a user
namespace with a `/run` of its own, the table made as `compositor_table` makes it with no card is cut through its path
and read; the seal refuses the cut and the read is answered. With the plain file the child is ended by SIGBUS (1 failure,
gcc). Where no user namespace can be made the row says NOT RUN.

**Still open.** Nothing.

### MW-2026-127: a window could make itself a popup of the focused window for the clipboard

| | |
|---|---|
| Status | Fixed 2026-11-04 (hardening: every table writer is root today, MW-2026-050) |
| Class | Authorization decided from a field the requester writes (CWE-639 class): the clipboard's owner of a popup |
| Attacker | a window's program; reachable by a non-root program only if the table is ever opened to one |
| Moonwater exposure window | from `76914efe` (2026-10-30 16:03 +01:00, the clipboard) to the fixing commit |
| Fixed by | the commit that adds this entry (`src/canvas/compositor.c` `compositor_clip_root`) |

**What it was.** A paste is answered READY only to the window with the keys, or to a popup of it, after a press of the
person's on that row. Which window a popup belongs to was read from the row's `popup_parent` in the table, which its program
writes at any time; the compositor's own record of a popup's window (`compositor_popup_parent`) is taken once, with the
first picture. A window that wrote the focused window's row there after it showed was taken for that window's popup: a
press on it (which does not give it the keys) let it paste what the person copied in the focused window.

**The fix.** `compositor_clip_root` walks the compositor's own record (`compositor_popup_root`), which the Wayland shim's
paste uses too; a real popup's answer is the same.

**Tests that fail without it.** `CHECK_text_engine` section `clipboard`: a window that writes the focused window as its
popup parent, and is none, is refused a paste after its own press. With the old walk it is READY (1 failure, gcc).

**Still open.** Nothing in the clipboard's rules; the shared table's other per-row trust is the design (MW-2026-050).

### MW-2026-126: the compositor relied on the Wayland shim to ignore SIGPIPE

| | |
|---|---|
| Status | Fixed 2026-11-04 (hardening: a build without the Wayland shim) |
| Class | Denial of service: the root compositor ended by a signal its own writes raise |
| Attacker | none outside root: the machine script closing keys.fire between the compositor's open and write, or the sound server going while a volume is told |
| Moonwater exposure window | for a build with `CONFIG_MOONWATER_WAYLAND=n`, from `c793b422` (2026-10-11 19:16 +02:00) to the fixing commit; a default build ignored it from `compositor_wl_start` |
| Fixed by | the commit that adds this entry (`src/canvas/compositor.c` `compositor_pipe_ignore`) |

**What it was.** A write to a pipe or a socket whose reader has gone raises SIGPIPE, which ends a process that does not
ignore it. The compositor writes chords into keys.fire, which it opens only while the script reads it (the script can close
it between the open and the write), and volumes into the sound server's socket. Only the Wayland shim ignored SIGPIPE, when
it started; the audio control client sends with MSG_NOSIGNAL, keys.fire's write did not.

**The fix.** `compositor_signals`, where the compositor sets its signals up, ignores SIGPIPE itself.

**Tests that fail without it.** `CHECK_terminal` `check_compositor_pipe_ignored`: a child that writes to a pipe with no
reader after `compositor_pipe_ignore` is answered EPIPE and lives. With the line out it is ended by SIGPIPE (1 failure, gcc).

**Still open.** Nothing.

### MW-2026-125: the compositor's reads of /root followed links and waited on FIFOs

| | |
|---|---|
| Status | Fixed 2026-11-04 (hardening: root-only) |
| Class | Link following and a blocking open in a root process (CWE-59, CWE-833 class) |
| Attacker | root, or whatever can write /root and /run/moonwater (nothing else can) |
| Moonwater exposure window | from `c793b422` (2026-10-11 19:16 +02:00, the compositor) to the fixing commit |
| Fixed by | the commit that adds this entry (`src/canvas/compositor.c` `COMPOSITOR_SETTING_OPEN`, `compositor_script_say`) |

**What it was.** The compositor reads its settings from files of /root (`compositor_word`, `compositor_text`, the
keyboard's layout), the keys list and the sound server's page from /run, and writes chords into the machine script's FIFO
`/run/moonwater/keys.fire`. Every one was opened through a link, and the reads with a blocking open: a FIFO with no writer
put in a setting's place held the compositor in the open for ever (no frame, no input), and a link in keys.fire's place had
the chord written into whatever file it named.

**The fix.** Those reads are opened `O_NOFOLLOW | O_NONBLOCK` (`COMPOSITOR_SETTING_OPEN`): a link or a FIFO is no file
there, and is read as nothing. keys.fire is opened `O_NOFOLLOW` beside the `O_NONBLOCK` it had. The files the compositor
writes (places, the volume) were already `O_NOFOLLOW` and renamed into place.

**Tests that fail without it.** `CHECK_terminal` `check_compositor_setting_opens`: a link in a setting's place is not read
through (as a word or a file's text), a FIFO in its place is read at once as nothing (in a child the check waits two seconds
for), and a link in keys.fire's place is not written through. Without `O_NOFOLLOW` two rows are red, without `O_NONBLOCK`
one (gcc).

**Still open.** The fonts the compositor maps (`-f` and the fallbacks) may be links on purpose and are opened as before.

### MW-2026-124: the compositor read the card's connector list past what the kernel wrote

| | |
|---|---|
| Status | Fixed 2026-11-04 (hardening) |
| Class | Out-of-bounds read on the stack (CWE-125): a count trusted from the second of two calls |
| Attacker | (5) a physical peripheral: a DisplayPort MST dock or hub whose ports come and go between the compositor's two GETRESOURCES calls |
| Moonwater exposure window | from `13d03ddf` (2026-10-26 19:24 +01:00, every screen of the card read) to the fixing commit |
| Fixed by | the commit that adds this entry (`src/canvas/compositor.c` `compositor_connected_read`) |

**What it was.** The card's connectors are asked for twice, as DRM wants: how many, then the list into room for that
many (16 at most, a stack array). `drm_mode_getresources` writes no more ids than the room it is given and then answers
how many connectors there are now, which is more when a connector was made between the two calls (an MST port). The
compositor looped to that count, reading ids from past the ones written and past the array, and asked the card about
each. It is a read and the ids go only to GETCONNECTOR, which refuses one that is not a connector; nothing was written.

**The fix.** The count is held to the room given (the first answer) after the second call, for the connectors and the
crtcs. The other lists the compositor asks twice for were already right: a connector's modes and encoders (copied by the
kernel only whole, and the compositor uses them only then), a property's enums (copied up to the room and counted
against it); a property's values and the EDID blob are the next two entries.

**Tests that fail without it.** `CHECK_terminal` `check_compositor_card_lists`, against the checks' fake card (answering
as the kernel answers, `card_fake_control`): a card listing two connectors that says forty the second time is asked of
those two and no other. Without the clamp the row is red (1 failure, gcc).

**Still open.** Nothing of this shape in the compositor's DRM calls.


### MW-2026-138: a font's lookups could try subtables that the shaping budget never paid for

| | |
|---|---|
| Status | Fixed on main, 2026-11-04 |
| Class | Denial of service: shaping work a font file chooses, outside the op budget |
| Moonwater exposure window | 2026-11-04 00:07 +01:00 (`72ba74c80`, right to left text and shaping) to the fix, under a day |
| Fixed by | the commit that adds this entry |
| Tests that fail without it | `CHECK_font_shape`, `costly lookups: a line of 4096 runs out of its budget in the costly lookups, and the last never runs` |

**How it was exposed.** Arabic and Hebrew runs are shaped through a face's GSUB and GPOS lookups under a budget of
FONT_OPS (2048) operations a code point, as HarfBuzz bounds its own; the budget was charged for rules matched, glyphs
scanned and nested lookups, but not for the subtables a lookup tries at each glyph (up to 256), each a coverage search.
A face whose plan is 63 lookups of 256 subtables that cover every glyph and never apply was 16128 uncharged tries a
code point: 0.68 s to lay one line of 4096 Arabic-Indic digits, and the fuzzer met 0.6 s for a few words. Reachable
with a font the person sets for the desktop and Arabic or Hebrew text on the screen.

**The fix.** `font_try_subtables` charges one operation a subtable tried and stops when the budget is spent. Measured
on every Arabic, Urdu and Hebrew face on the box over generated paragraphs, a real face spends at most 425 operations
and 328 tries a code point (Noto Nastaliq Urdu; under 30 and 8 for the others), so the budget is not reached and the
hb-shape parity rows of CHECK_font_shape are unchanged; the same line now takes 0.09 s.

### MW-2026-137: the editor opened a device to find out it was not a file

| | |
|---|---|
| Status | Fixed on main, 2026-11-04 |
| Class | Side effects of opening a device or FIFO a link names (denial of service: a watchdog armed, a tape rewound, a waiting writer let go) |
| Moonwater exposure window | 2026-10-30 16:59 +01:00 (`3f919186`, the editor) to the fix, about 4 days 18 hours |
| Fixed by | the commit that adds this entry |
| Tests that fail without it | `CHECK_terminal`, `editor hostile: a FIFO with a writer waiting on it is refused, and the writer is not let go` |

**How it was exposed.** `editor_load` opened the path to read (O_RDONLY, O_NONBLOCK) and only then asked fstat
whether it was a regular file, and `editor_check_disk` opened it again the same way each time the window got the
keys. A link named like a document (`notes.txt -> /dev/watchdog`, left in an archive or a shared directory) opened as
root armed the machine's hardware watchdog, and its close without the magic byte left it running: the machine
reset a minute later. Opening a tape device rewinds it at its close, and opening a FIFO releases a writer blocked on
it, which is what the check uses.

**The fix.** The path is opened O_PATH first, which opens nothing; fstat on that decides, and only a regular file is
opened to be read, through /proc/self/fd of the pin, so the file read is the file looked at. The changed-on-disk look
uses O_PATH alone.

**The check.** A FIFO named notes.txt with a child blocked opening it to write: the editor refuses it and the child
must still be blocked 300 ms later. With the old open put back (the mutant) the child's open returns at once.

### MW-2026-136: a font of a few hundred kilobytes held font_load for seconds, and a kern table every pair

| | |
|---|---|
| Status | Fixed on main, 2026-11-04 |
| Class | Denial of service: work a font file chooses, without a ceiling |
| Moonwater exposure window | 2026-10-09 17:38 +02:00 (`697155d9a`, the text engine) to the fix, about 26 days |
| Fixed by | the commit that adds this entry |
| Tests that fail without it | `CHECK_font`, `kerning: a GPOS of 65535 kern features of 65535 lookups each loads in well under 50 ms` (4.2 s without the ceiling, 5 us with it) and `kerning: a kern table of 65535 subtables walks a ceiling of them a pair, 20000 pairs in well under a quarter second` (2.5 s against 10 ms) |

**How it was exposed.** `font_load` collects the kern feature's lookups from GPOS (`font_kern_lookups`) by walking
the default language's feature indices, and each kern feature's lookup indices, as far as the 16-bit counts say,
where the shaping plan caps the same walks (1024 scripts, 4096 features, 128 steps). A GPOS of 263 KB whose default
language names one kern feature 65535 times, of 65535 lookups, is 4.3 billion reads at load: 4.2 seconds on the box,
minutes with the lookups all distinct. The font a person sets for the desktop (/root/font) is loaded by the
compositor and every app as they start, so a downloaded font set there kept the desktop from coming up. The kern
table's walk (`font_kerning_units`, for a face whose GPOS kerns nothing) went through as many subtables as the table
claims, 65535 six-byte ones for every pair laid. Found by this pass's font fuzzer (a mutated case of 2.8 s).

**The fix.** The walk at load reads at most 4096 lookup indices (FONT_KERN_READS), 1024 scripts and 4096 features,
as the plan does; the kern table's walk stops at 256 subtables (FONT_SUBTABLES, the GPOS walk's ceiling). No real
face is near either.

### MW-2026-135: a font's metrics past 16384 px wrapped in 32 bits

| | |
|---|---|
| Status | Fixed on main, 2026-11-04 (hardening: wrong sizes and a signed overflow, no memory written) |
| Class | Integer truncation and signed overflow on metrics a font file chooses |
| Moonwater exposure window | 2026-10-09 17:38 +02:00 (`697155d9a`, the text engine) to the fix, about 26 days |
| Fixed by | the commit that adds this entry |
| Tests that fail without it | `CHECK_font`, `arithmetic: a cell as wide as an advance of 65535 units in an em of 16 is held at 16384 px, not wrapped` and `arithmetic: an advance past 16384 px is held there too` |

**What it was.** `font_fixed` turns font units into 16.16 pixels as units * size * 1024 / units per em, worked in 64
bits and handed back in 32. A file may say an em is 16 units and an advance 65535 (both pass the load), which is
2^34 at the terminal's 64 px and 2^42 at the largest size: the result wrapped, so the terminal's cell from such a face
was one pixel wide at some sizes and negative at others, and `font_cell_find` then shifted a cell width of up to two
such cells left by 16 in 32 bits, a signed overflow. No array was indexed by the wrapped values (the atlas and the
cell drawing clip and check their sizes), so this is wrong sizes from a hostile font, not memory corruption. The
value is now held at 16384 px either way (2^30 in 16.16, which leaves room to add a few) and the shift is in 64 bits.

### MW-2026-134: the text engine's dot pass did arithmetic on stack words it never wrote

| | |
|---|---|
| Status | Fixed on main, 2026-11-04 (hardening: undefined behaviour, no memory written) |
| Class | C undefined behaviour: a read of uninitialised automatic storage and a signed overflow on it |
| Moonwater exposure window | 2026-10-30 13:03 +01:00 (`b1727b531`) to the fix, about 5 days |
| Fixed by | the commit that adds this entry |
| Tests that fail without it | the font fuzzer's clang trap build (`font_dots`, font.c's last loop, seed 7 case 24); `CHECK_font`'s new row `arithmetic: the dot pass over a stem and a dot, its stack full of extremes, ...` runs the pass on a scribbled stack, but a plain build answers the same either way and gcc's trap build folds the read |

**What it was.** `font_dots` keeps a box for each contour that is a dot and none for the others, then in its last
loop took `box[c][2] - box[c][0]` for every contour before asking whether it was a dot. For a stem (every dotted
letter has one) those were whatever the stack held, and a difference of two of them could overflow, which a
compiler may treat as impossible. The value was never used, so nothing wrong was drawn, and nothing past an array
was read: this is undefined behaviour removed, not an exposure. Found by this pass's fuzzer (clang, UBSan trap at
the line), and by the compositor's reviewer through a window title.

### MW-2026-163: a program under a seccomp filter or a tracer could start programs neither saw, through /dev/spark

| | |
|---|---|
| Status | Fixed on main, 2026-11-04 |
| Class | Sandbox bypass: an exec made outside the system call a seccomp filter judges, and a child a ptrace supervisor is not told of |
| Moonwater exposure window | 2026-08-24 15:34 +02:00 (`e6b1208d`, spawn through /dev/spark) to the fix, 72 days |
| Fixed by | the commit that adds this entry |
| Tests that fail without it | `--harness core_state`, `a seccomp-filtered caller is refused the exec its filter would not see`, `a traced caller is refused the launch a tracer would not see` and their two `the refusal reads nothing, starts nothing and holds nothing` rows (87,428 of 87,432 with the refusal taken out) |

**How it was exposed.** SPARK_IOCTL_SPAWN (/dev/spark, mode 0666) starts a program with kernel_clone(CLONE_VM |
CLONE_UNTRACED) and kernel_execve in the child: no execve, execveat, fork, vfork, clone or clone3 system call is made.
A sandbox that refuses those calls with seccomp and allows ioctl therefore did not stop a new program image. Bowls
mount devtmpfs at /dev, so a third-party sandbox inside one (a browser's, a service manager's SystemCallFilter, bwrap
with a filter) has the device. CLONE_UNTRACED also kept the child from a tracer following forks. Booted proof (the
ring-0 pass's subagent, x86_64 KVM): uid 65534 under a filter refusing fork and execve got EPERM for both and then
started a program through the request. The child kept the filter, NO_NEW_PRIVS and its Landlock domain, so what was
escaped is the exec itself; Floodlight's own filter already refuses the request by its number. Now do_spawn refuses
(EPERM, before it reads the request) a caller whose seccomp mode is not disabled or that is being traced. The shell
falls back to fork and exec on any refusal, so a filtered or traced shell still runs everything, through calls its
filter and its tracer see.

### MW-2026-133: a large file cut short by another program killed the editor and every tab's unsaved work

| | |
|---|---|
| Status | Fixed on main, 2026-11-04 |
| Class | Denial of service with data loss (SIGBUS on a truncated file mapping) |
| Moonwater exposure window | 2026-10-30 16:59 +01:00 (`3f919186`, the editor) to the fix, about 4 days 18 hours |
| Fixed by | the commit that adds this entry |
| Tests that fail without it | `CHECK_terminal` dies with SIGBUS (status 135) at `editor hostile: a mapped file cut short under it reads zeros past the cut, its start as it was, and says it changed` |

**How it was exposed.** A file of 8 MiB or more is mapped, not read, and the document's pieces point into the
mapping. When any process that can write the file truncates it (its owner, a program rewriting it in place,
logrotate's copytruncate on a log the person has open), the kernel takes the pages past the new end out of every
mapping, and the editor's next read of one (a scroll, a search, a save) is SIGBUS. The editor died, with the
unsaved changes of every tab. The check's own comment already recorded the class: a sibling check truncating a
shared file killed the arm64 run.

**The fix.** The first mapping installs a SIGBUS handler (SA_SIGINFO): a fault inside a tab's mapping maps
anonymous zero pages over the rest of that mapping (MAP_FIXED, the compositor's pattern for a client's cut pool in
wayland_host.c), marks the tab changed on disk and returns, so the read finishes with zeros. A fault anywhere else is
handed to the handler that was there before. The person sees NULs where the file was cut and the changed-on-disk
line, and every other tab is untouched.

**The check.** A 9 MiB file is opened (mapped), truncated to 4096 bytes by the check, and read at 8 MiB: the bytes
must be zeros, the first page as it was, and the tab marked changed. With the handler's installation taken out (the
mutant) the check process dies by SIGBUS there.

### MW-2026-153: a tray format of braces wrote past the tray's text in the compositor

| | |
|---|---|
| Status | Fixed on main, 2026-11-04 |
| Class | Out-of-bounds write (up to three bytes past a 384-byte buffer in the compositor's taskbar state); memory corruption from a setting |
| Attacker | none without root: the format is `moonwater taskbar tray FORMAT` (root's), and the overflow needs a host name of 26 bytes or more (`moonwater name`, root's; a bowl's UTS namespace has its own) |
| Moonwater exposure window | 2026-10-30 15:14 +01:00 (`85de761b`, the tray format) to the commit that adds this entry; no exposure to a program that is not root |
| Fixed by | the commit that adds this entry (`src/canvas/ui/taskbar.c` `taskbar_tray_render`) |

**What it was.** `taskbar_tray_render` fills `tray_text` (`4 * SETTING_TRAY_MOST`, 384 bytes) from the person's format
(at most 96 bytes, which `setting_tray_valid` checks): a name in braces goes through `taskbar_tray_put`, which stops at
the room, and a plain character through a branch that does too, but `{{` and `}}` (a brace itself) wrote the brace and
the end after it with no look at the room. `{host}` is 6 bytes of format and up to 31 of text, so fifteen of them fill
the 384 bytes, and each `{{` after them wrote a byte and a NUL one further past the buffer, into `tray_chars`; the count
of characters that follows then read on past the text. The validator takes that format.

**The fix.** The two branches write while there is room for the brace and the end, as every other branch does. The host
name's file is `taskbar_host_file`, a seam a check points at a file of its own, as `battery_root` is.

**Tests that fail without it.** CHECK_terminal, the tray format rows: `taskbar_host_file` at `/proc/version` (a first
line of 31 printable bytes and more), a format of fifteen `{host}` and three `{{` (96 bytes, which the validator takes):
the text must end inside its buffer and the field after it (`launcher`, a sentinel) must be untouched. Without the fix
the 384 bytes hold no NUL (1 failure of 3655, gcc on the box); with it, none. The run without the fix is the mutant (the
seam in, the guard out).

### MW-2026-162: on the locked tier the ext4 readdir ports allocated with flags taken from the wrong register

| | |
|---|---|
| Status | Fixed on main, 2026-11-04 |
| Class | Ring-0 calling-convention error (an allocator called with the arguments of another signature): unzeroed memory taken as zeroed, allocation flags chosen by the caller's leftover registers |
| Moonwater exposure window | 2026-10-07 18:42 +02:00 (`b7c5be64`, sec_mem_locked partitions the kmalloc caches) to the fix, 28 days, on images of the locked tier only |
| Fixed by | the commit that adds this entry |
| Tests that fail without it | `--harness kernel_ports`, the six rows `ext4_htree_store_dirent: with the caches partitioned (random\|typed) the allocator is given a token and then kzalloc's flags` on x86_64, arm64 and riscv64 (54 of 60 with the parent's kernel.c) |

**How it was exposed.** ext4_dir_open and ext4_htree_store_dirent in kernel/kernel.c (asm, all three architectures,
since `a3d76922`) call `__kmalloc_noprof(size, GFP_KERNEL | __GFP_ZERO)`. With CONFIG_KMALLOC_PARTITION_CACHES, which
kernel/profile/sec_mem_locked turns on, slab.h declares `__kmalloc_noprof(size_t, kmalloc_token_t, gfp_t)`. So the
flags were read as the token, and the flags the allocator used were whatever the third argument register held:
- for ext4_dir_open, the caller's own third argument (in the x86_64 build examined, the opener's open(2) flags);
- for a new arena chunk, the object size on x86_64 and the minor hash being stored on arm64 and riscv64 (zero for
  ".", read off the disk for a casefolded and encrypted directory).
The arena depends on zeroed memory: a node's `next` is never written. Without __GFP_ZERO a chunk with stale bytes
sends call_filldir after a wild pointer and free_rb_tree_fname into kfree of one. The tier as shipped also sets
init_on_alloc=1, which zeroes these objects anyway, so the bug as shipped was wrong allocation flags: no direct
reclaim, and bits an opener or an image can choose, such as __GFP_MEMALLOC (the emergency reserves) and __GFP_NOFAIL.
The default and the other tiers do not partition the caches and were not affected. Now each call puts a token in the
second register and the flags in the third when the option is on: the call's own address under the random partition,
as `_CODE_LOCATION_` gives the C, and 0 under the typed one. The default build's asm is byte for byte what it was.
The harness stub takes the token signature when the option is defined and asks for exactly kzalloc's flags.

### MW-2026-132: the editor's save followed a directory swapped for a link after the file was opened

| | |
|---|---|
| Status | Fixed on main, 2026-11-04 |
| Class | Write redirection through a symlink race (time of check to time of use), as root; a save under a name cut short |
| Moonwater exposure window | 2026-10-30 16:59 +01:00 (`3f919186`, the editor) to the fix, about 4 days 18 hours |
| Fixed by | the commit that adds this entry |
| Tests that fail without it | `CHECK_terminal`, `editor hostile: a save after the file's directory was swapped for a link writes nothing through the link`, `... and writes the directory the file was opened in`, `editor hostile: its save is refused, and the name cut short is not written` |

**How it was exposed.** The editor resolved a file's path once, at open (through /proc/self/fd), and the save wrote
`DIR/.NAME.editing` and renamed it over `DIR/NAME` by that path. O_NOFOLLOW and O_EXCL guard only the last component,
so a directory of the path could be replaced after the open: a user who owns `/home/u/d` gives root `d/passwd` to
read, root opens it in the editor (the person runs as root), the user renames `d` away and makes `d` a link to `/etc`,
and root's Ctrl+S writes the document over `/etc/passwd`. The time between the open and the save is a person's, so the
race is not a race. A second way to the wrong name: /proc's name of a file whose real path is over 511 bytes came back
cut by readlinkat, and the save went to the cut name (a new file beside the real one, or a second name of it).

**The fix.** The directory the file is in is opened (O_PATH) when the file is, and kept with the tab; its entry for the
name must be the file just opened (device and inode), or the tab can show the file but not save it. The save creates
the temporary file and renames it by name in that directory (openat, renameat2 against the descriptor), so no path is
walked at save time. A path readlinkat cut short holds no directory. A file that is not there yet holds the directory
of the name it will be saved as.

**The checks.** The swap row opens `sub/note.c`, renames `sub` away, makes `sub` a link to another directory and saves:
nothing may appear through the link and the moved directory must hold the save. The cut row opens a file whose real
path is 600 bytes, with a second name (a hard link) of exactly the cut length, and saves: the save must be refused and
the cut name untouched. Mutants: the directory opened again by path at save time turns the swap rows red; the
length guard taken out turns the cut row red.

### MW-2026-123: a copy naming a FIFO held the compositor in its open

| | |
|---|---|
| Status | Fixed 2026-11-04 |
| Class | Denial of service by a blocking open in the root compositor; a confused-deputy open of whatever a program names (CWE-367 class: the open is of what the row says, not of what was checked) |
| Attacker | a window's program (root today, as every table writer is: MW-2026-050), after a press of the person's on its window; and `moonwater clipboard set`'s fields in the header, which any table writer can fill |
| Moonwater exposure window | from `76914efe` (2026-10-30 16:03 +01:00, the clipboard) to the fixing commit |
| Fixed by | `0edafad3` (`src/canvas/compositor.c` `compositor_clip_open`) |

**What it was.** A copy is a sealed memfd the program names in its row (`clip_offer_fd`, with its pid), and the compositor
opens it again through `/proc/PID/fd/N` to check and keep it. It opened whatever the descriptor was with a blocking read
open: a FIFO the program holds open for reading with no writer made that open wait for a writer for ever, and the
compositor with it -- no frame, no input, no pointer, the whole desktop stopped until a writer came (the kernel's Ctrl-Alt-
Backspace is the only way out). A terminal opened so would become the compositor's controlling terminal if it had none (no
O_NOCTTY), and a device's open is its driver's to act on (a watchdog's open arms it). The checks after the open (seals,
size, UTF-8) came too late for any of that.

**The fix.** The descriptor's file is taken with `O_PATH`, which opens nothing; `statx` on that hold must say a regular
file (a memfd is one); only then is it opened, through the hold's own `/proc/self/fd` entry (so the file opened is the one
looked at, whatever the program does to its descriptor meanwhile), and with `O_NONBLOCK | O_NOCTTY`. The seal, size and
text checks follow as before. A copy is rare (a gesture), so the two calls more cost nothing that is measured.

**Tests that fail without it.** `CHECK_text_engine`, section `clipboard`: a FIFO opened for reading with no writer offered
as a copy, in a child the check waits two seconds for: declined at once and the compositor not held. With the old open the
child is still in the open after two seconds and is killed (status 9, 1 failure). An anonymous pipe's end does not show it:
the kernel waits for a writer only on a FIFO with a name, which is why the row makes one.

**Still open.** A regular file on a FUSE mount is still opened (its open is the FUSE daemon's, which may wait); the table
is root's, so only root can name one. The program side's paste (`windows_clip_fetch`) opens `/proc/clip_pid/fd/900` with
`clip_pid` read from the shared table, which any table writer may rewrite: by design while the table is root's.

### MW-2026-122: the lift of a long press went to the next program in a closed window's row

| | |
|---|---|
| Status | Fixed 2026-11-04 |
| Class | Stale index after free (a row number kept past its window): input delivered to another window, and a clipboard gesture with it |
| Attacker | a program that opens a window when another closes, under a finger held on a touch screen (timing, no privilege beyond being a window) |
| Moonwater exposure window | from `fcfac726` (2026-11-01 23:59 +01:00, the long press) to the fixing commit |
| Fixed by | `33ce8bff` (`src/canvas/compositor.c` `compositor_leave`) |

**What it was.** A finger held still on a touch screen is a long press: `compositor_touch_tick` takes back the left press,
sends the right button, and keeps the row under the finger in `compositor.touch.hold_row`; the lift that ends the press
is a left release (WINDOW_KEY_CANCEL | WINDOW_KEY_HOLD) posted to that row. `compositor_leave`, which frees a row when its
program has gone, cleared every other row the hand keeps (the press, the drag, the resize, the close button under a
finger, the buttons held) but not this one. Rows are taken lowest first, so the next window opened is in the row that was
just freed: a window that closed under a held finger had the lift posted into the next program's ring, a release of a
press it never had. And `compositor_post` takes every button event as a gesture of the person's on the row
(`compositor_clip_gesture`), which is what lets a window copy and, holding the keys, paste for 1.5 s: a window opened
with the keys (MOONWATER_ASKED) at that moment could read the clipboard with nobody having touched it.

**The fix.** `compositor_leave` lets `touch.hold_row` go with the row, as it does `touch.closing`: the lift then goes
nowhere.

**Tests that fail without it.** `CHECK_terminal` `check_compositor_touch_hold_gone`: a long press held on the window in
row 1, the window closed and a new one shown in row 1, the finger lifted: nothing is posted to row 1, and row 1 has no
gesture. Without the line both rows are red (2 failures, gcc).

**Still open.** The clipboard's gesture rule takes any button event the compositor posts, including the releases it makes
up itself (a cancel, a long press's right click); every one of those follows a real press on the same window, so none
other than this one was found to reach a window the person did not touch.



### MW-2026-161: Control-Alt-Backspace as Canvas went off could hang the canvas thread and every later Canvas request

| | |
|---|---|
| Status | Fixed on main, 2026-11-04 |
| Class | Kernel deadlock (lock order: a kthread joined under a lock the kthread takes) |
| Moonwater exposure window | 2026-10-06 12:34 +02:00 (`cdd7decf`, the reclaim chord) to the fix, 29 days |
| Fixed by | the commit that adds this entry |
| Tests that fail without it | `--harness canvas_lifetime`, `the thread's reclaim waits for no lock its joiner holds` (16 of 17 with the parent's canvas_reclaim) |

**How it was exposed.** canvas_thread_stop joins the canvas thread with kthread_stop, and its callers hold
canvas_list_lock while it does (it asserts so with lockdep_is_held): canvas_turn_off, which is `moonwater desktop off`
and the Control-Alt-Shift-Backspace work, and client_unregister for the last card. The file's lock order says this is
safe because the thread takes only desktop.lock. But the thread answers Control-Alt-Backspace (canvas_suspend_check),
and canvas_reclaim took canvas_list_lock to list the cards. A reclaim asked for, while a program held the card, in the
moment between a turn-off taking the lock and the input handler going away, left the thread waiting for the lock and
the turn-off waiting for the thread, both uninterruptibly. Everything after them that takes the lock waited too, and
that includes the SPARK_CANVAS_STATUS request any user may make on /dev/spark. The window is short and needs a key
pressed at the machine, so this is a local hang, not an escalation. Now canvas_reclaim takes the cards from
desktop.outputs under desktop.lock. Those are the cards desktop_taken asked about, so the thread takes no lock its
joiner holds. The check runs the real canvas_reclaim with canvas_list_lock held by a joiner and counts waits for it.

### MW-2026-160: any user read root's bound command lines from /dev/spark, all but the first byte

| | |
|---|---|
| Status | Fixed on main, 2026-11-04 |
| Class | Kernel information disclosure to an unprivileged caller (a secret the caller was meant to read as empty) |
| Moonwater exposure window | 2026-09-16 07:19 +02:00 (`fc1ad81b`, the whole line readable by anyone) to 2026-09-29 12:34 +02:00 (`75d016d0`, meant to hide it), then all but byte 0 to the fix: 49 days in all |
| Fixed by | the commit that adds this entry |
| Tests that fail without it | `--harness core_state`, `a refusal does not hand the line back to the caller it refused, not one byte of it` and `a line somebody set reads as empty, and not the default, without CAP_SYS_ADMIN` (87,425 of 87,427 with the one-byte clear put back) |

**How it was exposed.** `SPARK_IOCTL_BIND` on `/dev/spark` (mode 0666) answers a GET, and a refused SET, with
`bind_answer` (src/moonwater/moonwater.c). It copied the row's whole line into `request->command` with `strscpy` to
compare it with the event's default, and for a caller without CAP_SYS_ADMIN cleared `command[0]` only; `copy_to_user`
then sent the whole 312-byte struct back. A bound line runs as root and is the text the settings' GET is
CAP_SYS_ADMIN for ("a command can carry a secret"): a booted kernel gave uid 65534 with no capabilities the bytes
`pa_cli set_network 0 psk "hunter2-SECRET-psk"` of a line root bound to volume_up. Lines the image's settings
apply at boot were exposed on every boot, and bowls mount devtmpfs at /dev, so a bowl program could read them too.
The harness rows tested `!request.command[0]` and passed. Now the whole field is zeroed; the two rows compare all 256
bytes with zero.

### MW-2026-121: a window's least size could win over its buffer's room, and the stretch wrote past the buffer

| | |
|---|---|
| Status | Fixed 2026-11-04 (hardening: not reachable with the shipped scale presets) |
| Class | Out-of-bounds write in the root compositor (CWE-787): a clamp in the wrong order |
| Attacker | none in the shipped configuration; the root-only `-s` option at 7 or 8 on a screen under 768 pixels, or a screen narrower than 192 pixels at the 200 percent preset |
| Moonwater exposure window | from `e00c3a73` (2026-10-11 22:17 +02:00, the first resize) to the fixing commit |
| Fixed by | `b010ceaf` (`src/canvas/compositor.c` `compositor_want`) |

**What it was.** `compositor_want` is where every size a window is asked to be passes (the hand's resize, a divider, a
tile, a placement, a fit after a scale). It held the size to the least a window is at the desktop's scale, 96 by 48 times
the scale, and to the most, the largest screen, which is also the room every window's buffer has; but the least was the
first test of one conditional, so a size below the least took the least even where the least was past the most. At 800
percent the least is 768 by 384: on a 256-pixel screen the window was made 768 by 384, and `compositor_stretch` filled
that size into a buffer 256 pixels a row, through the rows after it and into the buffer after it -- the window's other
buffer, the next row's (another window's pixels) when it showed its second, and past the table's end for row 31. The
hosted fuzz of this pass found it (SIGSEGV against a PROT_NONE page after the table, in `memory_fill_u32` from
`compositor_stretch` from `compositor_scale_fit`), with the run-learning read (`memory_runs_u32`) faulting the same way.

**Reachability.** The desktop's scale is the displays line's, one of the presets 50 to 200 (`windows_scale_ok`), or 100;
at 200 the least is 192 by 96, and the most is the largest screen connected, so a real screen never meets it. `-s` takes
25 to 800 and is on the compositor's command line, which only the root machine script writes (it passes none). So this
is a latent memory-safety defect, fixed because the fix is free and the write is in the root process.

**The fix.** The least first and the most last: the room of the buffer is the final word whatever the scale.

**Tests that fail without it.** `CHECK_terminal` `check_compositor_least_scale`: at 800 percent on a 256-pixel screen a
window asked 10 by 10 is no larger than the screen, and none of its other buffer or the next row's two buffers (filled
with a pattern) is written. The old order turns both rows red (2 failures, gcc).

**Still open.** `compositor_stretch` trusts `compositor.w` and `h` to be inside the buffer; every writer of them is
bounded now (`compositor_take` and `compositor_want`), and a guard there was left out as it is on the resize path.

### MW-2026-120: a pointing device's 32-bit counts overflowed the compositor's sums of them

| | |
|---|---|
| Status | Fixed 2026-11-04 |
| Class | Signed integer overflow (undefined behaviour) on device-supplied input; the wheel and the pointer moved the other way |
| Attacker | (5) a physical peripheral: a USB HID device whose report descriptor declares 32-bit relative fields |
| Moonwater exposure window | the wheel's `value * 120` and the counts' `raw_x += value` from `c793b422` / `34945374` (2026-10-11 19:16 +02:00 and 2026-10-12 08:42 +02:00), the place's `real_x += accel` from `c9d0df3b` (2026-11-02 19:23 +01:00), to the fixing commit |
| Fixed by | `7173b431` (`src/canvas/compositor.c` `compositor_counts`) |

**What it was.** The compositor (root) reads every pointing device through evdev, whose `value` is any 32 bits the
device sent; the kernel passes a HID device's relative fields through at the width its descriptor declares, so a USB
device anyone can make reports REL_X or REL_WHEEL of 2^31 - 1. `compositor_input_read` multiplied a notch by 120 in an
int (2^31 - 1 times 120 wraps to -120: a notch down became a notch up), added a report's counts into an int (two of
2^31 - 1 wrapped to -2: the pointer went left), and `compositor_report` added each report's accelerated move into the
place before the read ended and the pointer was kept on a screen (twenty reports at 500 percent pass 2^31). All three are
undefined behaviour the optimizer may build on, in the root compositor's input path; with UBSan in trap mode the hosted
fuzz of this pass trapped on the third within 20,000 steps.

**Impact.** No memory was touched: every place is kept on a screen (`compositor_screen_keep_point`) before it is used as
a coordinate. What a device could do was make the pointer and the wheel go the other way, which a device that is a
keyboard already outdoes by typing. The fix is that the behaviour is defined, not a boundary closed.

**The fix.** `compositor_counts` adds in 64 bits and holds the sum to `WINDOW_COORD_MAX` either way; the wheel's notch,
the raw counts (a mouse's and a touchpad's) and the place after acceleration go through it. A device that reports what
devices report sees no change: every legal value is far inside the bound.

**Tests that fail without it.** `CHECK_terminal`, `check_compositor_wheel`, three `device:` rows: a notch of 2^31 - 1
turns the window the way the device said (`last == WINDOW_COORD_MAX`; -120 before), two counts of 2^31 - 1 in a report
put the pointer at the right edge (it went left before), and twenty reports of 2^31 - 1 at 500 percent in one read put it
at the right edge too. Each of the three sums put back as it was turns its own row red (3573 checks, 1 failure each, gcc).

**Still open.** Nothing of this class in the compositor's input path; a tablet's and a touch screen's places go through
`compositor_tablet`, which was already bounded.




### MW-2026-131: the editor's save made a mode-0 file readable by all, and root's save of another user's set-id program root's

| | |
|---|---|
| Status | Fixed on main, 2026-11-04 |
| Class | Information leak (file permissions widened); privilege escalation (a set-user-id file changing owner to root) |
| Moonwater exposure window | 2026-10-30 16:59 +01:00 (`3f919186`, the editor) to the fix, about 4 days 18 hours |
| Fixed by | the commit that adds this entry |
| Tests that fail without it | `CHECK_terminal`, `editor hostile: a file of mode 0 is saved mode 0, not readable by everyone` and `editor hostile: a set-user-id file whose owner the save cannot keep loses its set-id bits (as root it keeps both)` |

**How it was exposed.** A save writes a new file beside the old one and renames it over it (`editor_save`). It gave
the new file `tab->mode ? tab->mode : 0644`, so a file of mode 0 (Fedora's /etc/shadow and gshadow, any file a
person locked with chmod 0) came back 0644, readable by every user. It never gave the new file the old file's owner,
and the person runs as root, so every file root saved became root's; with the mode kept, a set-user-id or set-group-id
program that belonged to another user (an unprivileged user's own `chmod 4755` binary, which runs as that user) was
root's set-user-id program once root opened it in the editor and pressed Ctrl+S, whatever its bytes were.

**The fix.** The new file is written mode 0600, then given the old file's owner and group (fchown) and its exact mode
before it takes the name. A save that cannot give the old owner (a person who is not root saving someone else's file)
drops the set-user-id and set-group-id bits. A file that was not there before keeps 0644.

**The checks.** The mode-0 row saves a file whose read mode is 0 (set as root would read it, since only root can open
one) and asks for mode 0 after; the set-id row saves a 4755 file whose recorded owner is another uid, and as a user who
cannot chown it asks for 0755 (as root, for the other uid and 4755). Each fails with its part of the fix taken out
(the 0644 fallback put back; the bits kept when chown fails).

### MW-2026-130: the editor read a dangling link's target past its end

| | |
|---|---|
| Status | Fixed on main, 2026-11-04 |
| Class | C memory safety: a string read past an unterminated buffer (stack over-read), a save written to a wrong name |
| Moonwater exposure window | 2026-11-01 21:58 +01:00 (`67ab3242`) to the fix, about 2 days 12 hours |
| Fixed by | the commit that adds this entry |
| Tests that fail without it | `CHECK_terminal`, `editor hostile: a link to nothing yet, by an absolute name, opens a new file of exactly that name` |

**How it was exposed.** `editor FILE` where FILE is a symbolic link to a file that does not exist yet opens a new
file at the link's target (`editor_load`). readlinkat writes the target and no terminator, and an absolute target was
then copied with `string_copy_bounded`, which takes the source's length first: it read on through the stack after
the 512-byte buffer to the first zero, and the tab's path became the target with whatever the stack held after it
(cut at 511 bytes). A save then created that file instead of the one the link names. Anyone who can leave a link
where the person opens files (a shared directory, an archive, a project checkout) chooses the target; the stack's
bytes chose the rest of the name.

**The check.** The row writes the stack the editor's next call uses with bytes that are not a zero, opens a link to an
absolute name that does not exist, and asks for that name exactly. With the terminator taken out (the mutant) the
row fails: the path is the target with the stack's bytes after it.


### MW-2026-080: the clipboard and paste were designed against snooping and paste injection before either existed

| | |
|---|---|
| Status | Designed in, 2026-10-30; no exposure (Canvas had no clipboard and no paste before) |
| Class | Clipboard snooping, clipboard hijack, terminal paste injection (ESC [ 201 ~), hostile copied text on a terminal |
| Moonwater exposure window | none |
| Landed by | `76914efe` (service, terminal paste filter, threat model), `6839ce20` (seal) |
| Tests that fail without it | `CHECK_text_engine` section `clipboard`: dropping the focus rule turns 225 checks red, the seal check 2, the gesture rule 165, the ESC filter 4172 |

**Why an entry with no incident.** The threat model is in `.github/SECURITY.md` (The clipboard (Canvas)). The defences were planted as
bugs before landing, as step 4 of the procedure asks of a fix: each was removed in a scratch copy and the check went
red. Open: bowl programs have no clipboard (a Wayland bridge is undecided); OSC 52 is refused on purpose.

### MW-2026-001: eMudhra intermediate CA keys computable by anyone

| | |
|---|---|
| Status | Fixed on main, 2026-10-23 |
| Class | Weak key generation in a public CA; forged TLS server certificates |
| Moonwater exposure window | 2026-09-14 16:38 to 2026-10-23 03:25, 38 days 10 hours 47 minutes |
| After public report | 2026-10-17 18:05 to 2026-10-23 03:25, 5 days 9 hours 20 minutes |
| Fixed by | `47cc1cc8` (fix), `08d96ebb` (anchor count check, audit seal) |
| Hardening after the fix | `d958266a` (refuses any EC key anyone can compute, with or without a list) |
| Related | `38ddcbc9` (runs the official Wycheproof vectors in the crypto lane) |

**What happened upstream.** eMudhra generated eight intermediate CA keys in a
key ceremony on 2025-04-30 with too little entropy. The six TLS intermediates
(EM DV, OV and EV TLS CA, each in a G3B and a G3C copy) chain to emSign Root
TLS CA - G3 and then to emSign ECC Root CA - G3. The two OV code-signing
intermediates are the other pair. Each key is a P-384 key whose private scalar
is below 256; the scalars are 156, 230, 115, 56, 155, 4, 66 and 237, and we
recovered all eight ourselves by trying every scalar below 4096 against the
published public-key hashes. Sectigo (AAA Certificate Services) cross-signed
the same public keys, so a hash of the key covers both copies. Anyone could
compute the private keys from the public certificates; Scott Helme reported
doing it in under a second.

| When (UTC) | Event |
|---|---|
| 2025-04-30 | Key ceremony that produced the weak keys |
| 2026-10-17 18:05 | Report reaches the Chrome Root Program |
| 2026-10-17 19:45 | Chrome blocks the six TLS intermediates by key hash (CRLSet) |
| 2026-10-17 22:59 | Firefox block (OneCRL) reaches default installs |
| 2026-10-18, 19 | eMudhra revokes all eight certificates |
| 2026-10-19 | Apple blocks them; Android receives a block via Play Services |
| 2026-10-20 | Microsoft blocks them; Mozilla removes emSign ECC Root CA - G3 (NSS 3.131.1) |
| 2026-10-23 03:25 | Moonwater fix `47cc1cc8` |
| about 2026-10-27 | Chrome's removal of the same root reaches all devices (announced) |

**How Moonwater was exposed.** The trust store has carried the Mozilla server
roots since `d00160d6` (2026-09-14 18:38 +02:00); before that commit it held
three hardcoded anchors, none of them eMudhra. emSign ECC Root CA - G3 was one
of the 120 roots. A server presenting `leaf <- weak intermediate <- root` was
accepted at the default tier, for three reasons:

- The default tier does not require Certificate Transparency. Logged-SCT
  checking exists only at the tight tier (`MOONWATER_STRICT_NET=2`, added in
  `dc56c493`, 2026-10-11 23:19 +02:00). A build at the tight tier refused an
  unlogged certificate and was not exposed.
- Revocation checking (`43ee3519`, 2026-10-09) is soft-fail at the default
  tier: a CRL or OCSP answer that cannot be obtained counts as "no answer" and
  the chain is accepted (`tls_revocation_holds` refuses only at the tight
  tier). The forger's leaf carries no pointers. The genuine intermediate
  certificate does carry one, signed in by the root, and from 2026-10-18 and 19
  the root's CRL listed it; but that CRL is fetched over plain HTTP, and an
  attacker who is on the path drops the fetch. Before 2026-10-18 nothing listed
  the intermediates at all.

An attacker needed a network position to serve the chain. Every client that
goes through `tls_verify_chain` was affected: wget and fetch, NTS key
establishment, and DNS over TLS. The worst case is NTS on a machine whose clock
has never been set: its first answer is believed, so a forged NTS server could
have set the clock to any time after the build date, and every later
certificate decision follows the clock. On a clock that is set, a step past 2
seconds needs a second server's agreement and cannot go back before the last
authenticated time (`clock.good`).

Checked and not exposed. The bowl root-filesystem downloads: every row is a
SHA-256 compiled into `src/bowl.c` or an OpenPGP signature against a pinned
key, and a row that pins nothing is refused, so a forged mirror certificate
serves a file that fails the digest. `moonwater setup update` writes the build
that is running onto a disk and downloads nothing.

**Evidence of exploitation.** None known. Moonwater keeps no log of accepted
chains, so a past acceptance cannot be ruled out from our side. Chrome reports
that no certificate chaining to the six TLS intermediates had been logged to
Certificate Transparency before it blocked them, which bounds only the
CT-visible part.

**The fix (`47cc1cc8`).**

- `tls_blocked_keys` in `src/net/net.c` holds the SHA-256 of each key's DER
  SubjectPublicKeyInfo, the same naming Go, Chrome and Mozilla use.
  `tls_verify_chain` refuses a leaf with one of those keys, never uses a
  served intermediate with one as a step of the path, and refuses a
  caIssuers-fetched certificate with one. The user sees "has been revoked".
  The check runs at every tier.
- emSign ECC Root CA - G3 is out of `src/net/anchors.inc` (119 roots, 39232 key
  bytes), through `ANCHORS_LEFT_OUT` in the regenerator. The bundle the file is
  generated from (Arch ca-certificates-mozilla 3.129) still lists the root, so
  a regeneration from it must keep that entry.

**Tests that fail without the fix.**

- `tls_chains`, five rows. A bench root signs the blocked key 156: a leaf under
  one intermediate, an intermediate in the middle of a chain, and a leaf
  holding the key are refused as revoked (OpenSSL accepts all three). The next
  scalar, 157, is accepted, so the check is not "any small key". A served
  certificate of the blocked key that nothing in the chain needs is accepted.
- With the comparison in `tls_key_blocked` replaced by `if (0)` in a scratch
  copy, the three refusal rows fail ("wget accepts a chain the standards matrix
  says to refuse") and 18 of 21 rows hold.
- `--harness anchors` recomputes the eight hashes from the eight scalars and
  compares them with the table in `net.c`; it also holds `anchors.inc` to 119
  roots. `CHECK_net` checks that neither the Name nor the key of the dropped
  root is an anchor.
- Results on the box from a clean export of `HEAD` after the fix: net lane
  230514 of 230514 on x86-64, 230452 of 230452 on arm64 and riscv64,
  `tls-chains` 1916 of 1916, `anchors` 481 of 481.

**Hardening after the fix (`d958266a`).** The blocklist only helps once someone
names the keys. `tls_small_keys` in `src/net/anchors.inc` holds a key hash for
k*G, k = 1 to 1024, on P-256 and P-384, and `tls_key_blocked` refuses any
certificate whose EC key is on it, at every tier. The eMudhra keys (k below 256)
would have been refused on 2026-10-17 with no list. The same commit audits the
119 built-in roots for the defect (`--harness anchors --weak`): no EC root is
k*G for k below 2^20; no RSA root has a prime factor under 2^24, shares a prime
with another root, falls to 100,000 steps of Fermat's method, or carries the
ROCA fingerprint. The detector fires on planted keys of each kind. What this
does not cover: a weak key that is not a small scalar (a key from a bad but
non-trivial generator), RSA keys on served intermediates (no runtime screen),
and keys outside P-256 and P-384.

Found on the way. The libFuzzer target for path building and anchors
(`tls_verify_fuzz`) did not compile from 2026-10-11 (`dc56c493`, which made the
chain call `tls_trust_holds`) to 2026-10-23: its lift stubbed the old function
name. The fuzz lane's first gate also failed whenever it ran first in a
process, so the failure was not read. Both are fixed (`20734c17`, `d958266a`).
For those 12 days the chain verifier was covered by the unit lanes and
`tls_chains` but not by the fuzzer. Results on the box from a clean export of
`d958266a`: net lane 230515 of 230515 on x86-64 and at the tight tier, 230453
of 230453 on arm64 and riscv64, `tls-chains` 1932 of 1932.

Deep fuzz after the fix, on `20734c17` plus the tests: eight parsers (DER,
OCSP, CRL, TLS handshake, chain verification, HTTP at the default and tight
tiers, DNS, DHCP) for 20 minutes each with the 20,000-run cap lifted, one seed
each, ASan and UBSan: no crash and no sanitizer report. That is 20 minutes a
parser, not an assurance.

**Still open.**

- A machine is fixed only after it boots an image built from `47cc1cc8` or
  later. This entry records the commit on main, not a deployment.
- emSign Root CA - G1, C1 and C3 are still trusted. Chrome is reviewing G1.
  Check the CCADB list and drop them through `ANCHORS_LEFT_OUT` if a root
  program removes one.
- Removing a root or key no longer needs a rebuild: `moonwater security block
  add SHA256|FILE` refuses a key at runtime, `security trust remove` and
  `block add` take an anchor out, and the list is kept in the settings block
  and carried by install and update (`6f93271d`, `19558fe7`, `ef52a335`,
  `d55611a2`). A built-in block (the eMudhra keys, the small-key screen) cannot
  be lifted from the CLI. A next incident of this kind is first handled with
  `block add` on the key hashes, then the source change follows. Not yet
  covered: the mutation lane has not run on the overlay code, and a signed
  feed that would deliver blocks without a person typing them is designed, not
  built.
- Nothing watches for CA incidents. This one was found on 2026-10-23 during a
  review of recent disclosures, 5 days 7 hours 40 minutes after the first
  browser block.

**Sources.** Press and vendor posts, read on 2026-10-23; the primary records
(Mozilla bug 2083449, the CCADB posts) were not read.
[Chrome](https://blog.google/security/protecting-chrome-and-android-users-from-compromised-emudhra-intermediate-ca-certificates/),
[The Hacker News](https://thehackernews.com/2026/10/eight-emudhra-ca-keys-compromised.html),
[Dennis Jackson's list of affected certificates](https://emudhra.we-have-a-problem.com/).
The eight key hashes were taken from Go's fix
(`golang/go@7f86f957633e`) and from Jackson's `impacted-intermediates.json`;
they agree, and the scalars above reproduce them.

### MW-2026-030: locale status lines printed a disk's /root words raw

| | |
|---|---|
| Status | Fixed on the worktree branch, 2026-10-26 (awaiting cherry-pick to main) |
| Class | Terminal escape / line injection from attacker-controlled on-disk state printed by a root process |
| Attacker | 6 (controls the data partition or the booted stick while the machine is off) and, today, a root-launched bowl program (case R), which can write `/root`. Not a genuinely unprivileged kernel uid (case U): `/root` is 0700 root-owned, so case U cannot plant these files. |
| Fixed by | (worktree) `src/sh/host.c` locale sinks, `test/differential.py` rows |

**What happened.** `moonwater keyboard`, `moonwater timezone`, `moonwater ntp`,
`moonwater time sync` and `moonwater status` print words they read from `/root`
with a bare `%s`: `/root/keyboard` (`locale_keyboard_say`), `/root/timezone`
and `/root/timezone.mode` (`locale_zone_line`, `locale_status_lines`,
`locale_zone_how`'s country of an auto mode, and the `timezone auto` error that
names the kept zone when the network cannot be asked), `/root/ntp.server` and
`/root/ntp.nts.servers` (`locale_ntp_status`, `locale_nts_status`), and the
server that answered a time query (`locale_ntp_answered`, named in the
`time:`/`time moved` lines of `moonwater time sync`). A `%s`
argument is sent through `plain_out`/`plain_err` (src/sh/file.c), which escapes
ESC and the C1 controls but by design keeps a newline, a tab and valid high
UTF-8 ("a line or a tab is the text's own"). `host_read_word` strips only the
trailing newline of a word, so an embedded one survives. A disk whose
`/root/timezone` reads `Europe/Berlin\n[Moonwater] ...` makes `moonwater
status` print a second line that looks like the machine's own; a word with a
bidi override (U+202E) reorders the terminal line. The genuine writers
(`locale_keyboard_set`, `locale_zone_set`, `locale_nts_servers`) validate
against a table or a name grammar, so a word only becomes malformed when it is
planted on disk, never through the CLI.

**How it was reproduced.** In a booted guest (serial root shell), planting the
word and reading the output raw with `od -c`:

```
printf 'us\n[Moonwater] INJECTED keyboard line' > /root/keyboard
moonwater keyboard | od -c
0000000   [   M   o   o   n   w   a   t   e   r   ]       k   e   y   b
0000020   o   a   r   d       u   s  \n   [   M   o   o   n   w   a   t
0000040   e   r   ]  \n
```

and the same with `/root/timezone` (~70 bytes of forged line) and
`/root/ntp.server`. The word reaches the sink identically however it is
planted, so the sandbox rows below plant it as the run's own root.

**The fix.** Each sink spells the `/root` word through `writer_terminal_name`
(`%w`) instead of `%s`. `writer_terminal_name` escapes control bytes, tab and
high bytes to `\xNN` inside the formatter, before any writer, so the newline,
the tab and the bidi override can no longer forge a line or reorder one. A
legitimate keyboard code, zone name, country code or NTP hostname is ASCII and
is shown unchanged, so no good input moves. Fixed at the sink, not the source,
because the words are also read by other programs (the compositor watches
`/root/keyboard`): the reader is left alone and each printer is made safe.

**Tests that fail without the fix** (`--harness moonwater_cli`, lane `cli`):

- `moonwater keyboard spells a newline in a /root word, not a forged line`
- `moonwater timezone spells a newline in a /root word, not a forged line`
- `moonwater ntp spells a newline in a /root word, not a forged line`
- `moonwater timezone spells a bidi override in /root/timezone, not raw`
- `moonwater timezone spells a newline in /root/timezone.mode, not a forged line`
- `moonwater timezone auto spells the kept zone in its error, not a forged line`

Each plants a hostile word under `/root` in the cli sandbox (no network), runs
the verb, and reads the output raw. On the pre-fix `host.c` (the sinks reverted
to `%s`) the newline rows see a bare `[Moonwater]`-shaped line and the bidi row
sees the raw U+202E, and are RED; on the fixed tree the bytes are spelled
`\xNN` on the verb's own line and the rows are GREEN. The `time:`/`time moved`
server lines (`locale_ntp_answered`) are the same change but need a real NTP
answer to fire, which the offline sandbox cannot give, so they are fixed as a
one-line-per-sink hardening with no row of their own; the server name they
print is the same `/root`-chosen word the `moonwater ntp` row covers.

**Still open.** The fix covers the locale cluster. Other verbs that print a
disk's or a device's word were audited and route through an escaper already:
SSIDs through `radio_display`, USB product/serial through `usb_shown`/`usb_key`,
link peer names through `link_name_good`, the machine name through `name_valid`,
an image's build through `host_plain_line`, DNS servers as numeric addresses.
A general-purpose high-byte escape of non-ASCII names (bidi in an SSID that may
legitimately be non-ASCII) is a separate decision and is not changed here.
### MW-2026-040: a Waterlink group grant taken back left its members holding it

| | |
|---|---|
| Status | Fixed on the worktree branch, 2026-10-26; awaiting cherry-pick to main |
| Class | Stale authorization: revoking a grant from a group did not reach the machines the group had already paired |
| Attacker | A linked peer (attacker 8) that was paired through a group, after the operator narrowed the group's grants to take an authority back |
| Reachable | `moonwater link group NAME SECRET allow GRANT...`, src/waterlink/command.c `link_group_join` |

**What happened.** A machine joins a group with a set of grants
(`link group lab SECRET allow run shell`). Every peer that later pairs itself
through that group is written into `/root/link.peers` with the group's grants
copied into its own `may` word (`link_pair_keep`, src/waterlink/nearby.c), and
carries the group's mark. When the operator then narrows the group
(`link group lab SECRET allow shell`, dropping `run`), the old code wrote the
new grants onto the group record alone. The peers keep the `run` bit in their
own records, and `link_request`/`link_sessions_granted` read authority from the
peer record, not the group. So a machine the operator believed they had taken
`run` from went on running commands as root. Revoking a grant from a group was
silently a no-op for everyone already in it; the only way to take it back was
`link remove` or `link deny` on each peer by name, which the operator has no
reason to know they must do.

This is the group analogue of the revocation the lane already proves for a peer
named by hand (`link deny NAME run`). A group is the form made for a fleet, so
it is the form whose revocation matters most.

**Reproduced.** In the two-namespace link lane (`sh test/run link`), machine a
pairs itself to b through group `lab` (granted `run shell`). b runs
`link group lab SECRET allow shell`, then a runs
`moonwater link run <a> 'echo kept-run'`: on the old code the command runs and
prints `kept-run` (status 0). The peer line on b still reads
`may shell run files, in group lab`.

**The fix.** `link_group_join`, when `allow` narrows a group's grants, computes
`taken = old_may & ~new_may`, and, under the peers lock it already holds, clears
those bits from every peer carrying the group's mark before it saves the group
record. Members lose the authority first (peers written, then the group), so a
failure between the two files leaves less granted than asked, never more; a
peers file that cannot be read or written changes neither. A grant the group
*starts* giving is not pushed onto existing members (they are each given more by
name as before). Machines paired by hand carry mark 0 and are never touched. The
verb now says how many members lost what.

**Tests that fail without the fix.** `waterlink-link` (`test/differential.py
--harness waterlink_link`, run by `lane_link` / `sh test/run link`), the group
block, new rows:
- `sec: a machine lab paired may no longer run once b takes run from lab` —
  RED on the parent tree (the far command runs, status 0, `kept-run`), GREEN
  after (`run is not granted`, status 255, no output).
- `sec: and keeps shell, which lab still grants, and files, which lab never
  did` — holds that narrowing takes only the dropped bit, not every bit.
- `sec: a machine paired by hand keeps the run lab took from its members` —
  holds that mark-0 records are untouched.
- `saying lab's grants again as they are takes nothing from its member` —
  a no-op `allow` of the current grants changes nothing.

Box run (`/home/dawn/sec-4`): parent tree `waterlink-link` 198 of 202 (the two
new `sec:` grant rows and the `port` row below red); with the fix 201 of 202.
The one remaining red (`a FIFO at the groups and the machines is read as
nothing`) is a pre-existing lane failure unrelated to this change (a FIFO
planted at `/root/link.groups` by the harness as root; present on the parent
tree before any of my edits) and is reported separately as open.

**Still open.** `link_pair_keep` copies grants at pairing time by design (a
member's record is its own authority), so this fix keeps the two in step only at
the moment the group is narrowed; a grant the group gives is still a
pair-time snapshot. The mutation lane has not been run on the new branch in
`link_group_join`.

### MW-2026-041: a machine could be named `port`, colliding with the `port` verb

| | |
|---|---|
| Status | Fixed on the worktree branch, 2026-10-26; awaiting cherry-pick to main |
| Class | Verb/name namespace collision; a peer name that shadows a command word |
| Attacker | Operator error weaponized by confusion: `link port ...` meant for a peer named `port` instead moves this machine's own listener |
| Reachable | src/waterlink/service.c `link_words`, used by `link_name_good` |

**What happened.** `moonwater link port [N|auto]` became a verb in `b5ec7de8`
(2026-10-06), but `port` was not added to `link_words`, the list of words
`link_name_good` refuses as machine names. So `link add port KEY HOST`
succeeded, and a machine called `port` could be linked. Afterwards
`moonwater link port` (meant as "a terminal on the machine called port") is read
as the port verb and prints this machine's listener port; `moonwater link port
22360` (meant as "run `22360` on port"? — or simply a fat-fingered peer command)
is read as `link port 22360` and silently rebinds this machine's own Waterlink
listener to UDP 22360 at the next `link on`. The other verb words
(`on`, `off`, `pair`, `add`, `log`, …) are all refused as names; `port` was the
one that slipped through when the verb was added, so the one reserved word a
machine could still be called.

No peer could exploit this against another machine — a peer name is chosen
locally — but it is a real confused-deputy footgun: a name the operator picks
for a peer turns a later `link port` from a command on that peer into a change
to the local listener. The invariant the other verb words already keep (a
machine is never called by a word link uses) was violated for exactly one word.

**Reproduced.** Link lane, new row in the existing "a machine cannot be called
WORD" loop: `moonwater link add port KEY` succeeds on the parent tree (the peer
is stored) and the row `a machine cannot be called port` is RED; with `port`
added to `link_words` the add is refused (`is not a name`) and the row is GREEN.

**The fix.** `port` added to `link_words` in src/waterlink/service.c, one line,
so `link_name_good` refuses it as it refuses every other verb word. A peer
already saved under the name `port` from before the fix still reads (the stored
record is not re-validated against the word list); `link remove port` drops it.

**Tests that fail without the fix.** `waterlink-link`, the "a machine cannot be
called WORD" loop, `port` added to the word tuple: RED on the parent tree, GREEN
after. Box run as above.

**Still open.** Nothing further; the three-letter and shorter codes are a
separate space (`link_code_shaped`) and were never names.
### MW-2026-044: the locked tier left SMT on for processors the kernel counts as unaffected

| | |
|---|---|
| Status | Fixed on main, 2026-10-30 (4f011e7b) |
| Class | Hardening that did not take: a boot parameter that does less than its name |
| Attacker | A local program in a locked-tier machine, using sibling-thread (SMT) side channels |
| Reachable | The locked tier's kernel command line (`sec_locked`), on any processor the kernel does not list as vulnerable to an SMT-sensitive attack (found on a Zen 5) |

**What happened.** The locked tier asked for `mitigations=auto,nosmt`. `nosmt`
under `mitigations=` disables simultaneous multithreading only on processors
the kernel counts as vulnerable. On a processor it counts as unaffected,
including the Zen 5 the tier lane boots on, SMT stayed on, so a locked machine
ran sibling threads of different programs on one core with the tier's own name
saying otherwise.

**The fix.** `nosmt=force`, which turns SMT off whatever the processor is.

**Test.** The tier lane's SMT row: the old locked image was 15 of 16 (the SMT
row red), the new one is 16 of 16 in three boots.

### MW-2026-050: the compositor's window table was handed to any program

| | |
|---|---|
| Status | Fixed on the sec-5 worktree, 2026-10-26 (coordinator cherry-picks to main) |
| Class | Missing authorization on a shared root resource; confused-deputy keystroke injection into a root shell, and disclosure of a root window's pixels |
| Attacker | (8) a compromised bowl program, (1) a local unprivileged process |
| Fixed by | this commit (`src/canvas/window.c`, `src/canvas/canvas.c`, `src/canvas/compositor.c`) |

**What it was.** The compositor (a root process) shares its window state as one
mapping, `/run/moonwater/windows`: a header, then every window's two pixel
buffers and every window's input ring (`event_head`, `event_type`, `event_a`,
`event_b`, `focus`, `lift`, `pid`), all writable by whoever maps it. The design
has every window program map the whole table and read and write the fields of
its row; it presumes mutual trust. Two things let a program that is not a
trusted desktop program in: the file and its door FIFOs were made `0666`
(world read-write, `compositor_table`/`compositor_table_memfd`), and the kernel
handed a descriptor of the table to any caller of `/dev/spark`'s
`WINDOWS_IOCTL_TABLE_GET` with no capability check (`windows_table_get`,
`src/canvas/canvas.c`) -- the comment even said "any program takes a descriptor
of it". The `WINDOWS_IOCTL_TABLE_SET` giver side already required
`CAP_SYS_ADMIN`; the get side did not.

A program that maps the table can: read the pixel buffer of any window,
including the Canvas terminal that runs a **root** shell (screen-scrape a
password as it is typed, a key, anything on screen); write into the focused
window's event ring to inject keystrokes into that root shell (a confused
deputy: arbitrary commands as root); set `focus`, bump `lift`, and overwrite
`pid`. The terminal the machine script starts (`desktop_terminal`) is a root
shell, so injection there is root command execution.

**Reachability, case R and case U.** Today a bowl program started from root (the
Canvas terminal, the serial shell, a `/bowls/bin` launcher -- a KDE session
`moonwater desktop` starts is one) runs as real kuid 0 with full capabilities
and sees the host `/run/moonwater` (agent 4's finding; agent 1 is making bowl
programs a real unprivileged uid). So **case R** (today): a root-launched bowl
already has `CAP_SYS_ADMIN` and is inside the trust boundary -- root is not a
wall, and this fix does not claim to stop it; that is the bowl boundary agent 1
closes. **Case U** (a genuinely unprivileged kernel uid: a local user outside
bowls now, and every bowl program after agent 1's fix): before this fix such a
process took the table through the ioctl and read and wrote it; after it, the
ioctl returns `EPERM` and the `0600` files return `EACCES`. The fix is what
makes case U safe regardless of where the bowl boundary ends up.

Which window-table requests stay available to a non-root program: **none.**
Every legitimate window-table program is root (the compositor, and the
terminal, log, launcher and settings the root machine script starts); a
KDE/Chrome-style client speaks Wayland/X, not this table. Isolated unprivileged
windows are not a feature today, and the single shared writable mapping could
not enforce per-row isolation if they were (that needs a per-client protocol).
So the boundary is root/not-root, and nothing a real client does breaks.

**How it was reproduced.** A static helper `tableget` on a stick, in a booted
guest with the compositor up: it opens `/dev/spark`, calls
`WINDOWS_IOCTL_TABLE_GET`, maps the result and reads the live output size out of
the header, then does the same dropped to uid 65534. On `final-head.efi` (main
before the fix) the unprivileged run mapped the table (`GET ok 1280x800`) and
the files were `666`; on an image built from the fix the unprivileged run is
refused (`GET denied 1`) while root still succeeds, and the files are `600`.
Lane `canvas`, scenario `g1` (`CANVAS_ONLY=g1 sh test/run canvas`).

**The fix.**
- `src/canvas/window.c`: `WINDOWS_MODE` is `0600`, the one mode the compositor
  gives the table, its memfd and the door FIFOs.
- `src/canvas/canvas.c` `windows_table_get`: `if (!capable(CAP_SYS_ADMIN))
  return -EPERM;` before it hands out a descriptor, matching
  `windows_table_set`. `capable(CAP_SYS_ADMIN)` is against the initial user
  namespace, so a process that is only root in its own namespace (an
  `unshare -r` bowl) is refused.
- `src/canvas/compositor.c`: the table file, its memfd and the door FIFOs are
  created and `fchmod`ed `WINDOWS_MODE` instead of `0666`.

**Tests that fail without the fix.**
- `canvas` lane, scenario `g1`, four rows: g1.1 root still takes the table
  (`ok`), g1.2 an unprivileged process is refused it (`denied`), g1.3 the table
  is `600`, g1.4 the doors are `600`. Run against `final-head.efi` (pre-fix)
  g1.2 reads `ok`, g1.3 and g1.4 read `666`.
- `CHECK_socket` `windows_header`: two rows pin `WINDOWS_MODE == 0600` and
  `(WINDOWS_MODE & 0077) == 0`; a flip back to `0666` fails them.

**Still open.**
- The single shared writable table remains the design. A program that is root
  (case R bowls today, and root itself -- root is not a wall) is inside it. An
  isolated unprivileged window would need a per-client protocol, not this
  mapping; that is an owner decision, not built here.
- Behaviour change, stated: `moonwater debug` run by a non-root user can no
  longer read the Canvas readout (it used the same open path) and prints "no
  compositor is running". The verb is a diagnostic; its code is in the
  contended `src/sh/host.c` and was left unchanged to merge cleanly.
- The memfd route (`/run/moonwater/windows` as a `/proc/<pid>/fd` symlink) was
  already held for non-root by `/proc` tracing when the table is a memfd; the
  `0600` mode is what holds the regular-file fallback (a headless or no-card
  compositor) and a local non-root user there.
- The door FIFOs being `0600` also stops a non-root local process knocking the
  compositor's door loop (the per-turn read bound added earlier capped the
  work; this removes the reach).
### MW-2026-020: a compositor window door any program may hold open against its row

| | |
|---|---|
| Status | Fixed on this branch, 2026-10-26 (awaiting cherry-pick) |
| Class | World-writable control FIFO a root process acts on; denial of service |
| Attacker | (1) a local unprivileged process; (8) a bowl program once it is a genuine unprivileged uid. A root-launched bowl today is host uid 0 (case R, agent 4's finding) and reaches it as root; that path is moot once bowls are an unprivileged uid, but the hole remains for case U |
| Fixed by | this commit (`src/canvas/window.c`, `src/canvas/compositor.c`) |

**What it was.** The compositor (`/compositor`, root) gives each window a row of
a shared table and a FIFO door at `/run/moonwater/window.N` (and a wake beside
it, `window.N.wake`). `compositor_table` made both with mode `0666`
(`mknodat 0010666` then `fchmodat 0666`). The compositor frees a row --
`compositor_leave`, which takes the window off the screen, gives its pixels
back and clears its pid -- when the door's poll returns `POLLHUP`, which a read
end sees only when the door's *last writer* has closed, i.e. when the program
that owns the window has ended.

A door the whole machine may write is a door the whole machine may hold open. A
local process that is not the window's owner opens `window.N` for writing and
keeps it open; the writer count never falls to zero when the real owner ends,
so the compositor never hears it leave and never frees the row. The window
stays as a ghost, and with all thirty-two doors held and the windows behind
them gone, no row is ever free again: the desktop can open no new window. (A
flood of knock bytes on a held door also keeps the root compositor's poll loop
hot, bounded at 64 reads a turn but woken every turn.)

**Why it is denial of service and not worse.** The window *table* -- every
field a program and the compositor read and write, and the pixels -- is the
compositor's `memfd`, published as a symlink to `/proc/PID/fd` of the root
compositor. A process that is not root (or the compositor's own user) cannot
open that descriptor (`ptrace_may_access`), so it cannot map the table and
cannot be a window at all; the door carries only a content-free knock that
tells the compositor to re-read the row's fields, which only root can have
written. So the exposure is the row never freed (a dead window's pixels held,
new windows refused), not reading or writing another window's state.

**Reproduced.** Booted guest (`final-head.efi`, current main, the Canvas
desktop up):

```
# stat -c %a /run/moonwater/window.0            -> 666
# setpriv --reuid 1000 --regid 1000 --clear-groups \
      sh -c 'exec 9>/run/moonwater/window.0'    -> succeeds (a non-root
                                                   program holds the door)
# as uid 65534: head -c 16 /run/moonwater/windows -> denied (cannot map the
                                                     table: knock-only)
```

On an image built from the fix the door is `600` and the non-root open is
denied (`EACCES`).

**The fix.** `WINDOWS_DOOR_MODE` is `0600` (`src/canvas/window.c`), and
`compositor_table` makes the door and its wake with it (`src/canvas/compositor.c`).
Every window is a program a desktop session started, which is root, and the
compositor is root, so a root-only door breaks no client that can function (a
non-root program cannot map the table and so cannot be a window). A future
design that wants an unprivileged program to be a window must hand it the door
and the table as descriptors, not leave a world-writable FIFO on the machine's
shared `/run`.

**Tests that fail without the fix.**

- `desktop` lane, the M5 terminal check, two rows: `M5 a window door is
  root-owned 0600` (got `666` on the pre-fix image, `600` on the fix) and `M5 a
  non-root program cannot open a window door` (got `opened`, then `denied`).
- `checks.c` `windows_client`, row `a window door is root-only`, pins
  `WINDOWS_DOOR_MODE == 0600 && (WINDOWS_DOOR_MODE & 0077) == 0` (kit/term
  lane); it is red if the constant is put back to `0666`.
- Red-on-old shown by hand: the exact commands the M5 rows parse, run against
  `final-head.efi`, print `MODE=666` and `NONROOT=opened`; against the fix's
  image, `MODE=600` and `NONROOT=denied`.

**Still open.**

- The fallback table at `WINDOWS_TABLE` for a machine with no card
  (`compositor_table`'s no-`memfd` branch) is still made `0666`; a headless
  machine has no desktop, but it could be `0600` too, left for a follow-up.
- Reclamation is still `POLLHUP`-only. Binding a row's freeing to the owner
  pid's liveness (`kill(pid, 0)`) would reclaim a dead owner's row even if a
  squatter holds the door, and is the deeper fix should an unprivileged
  window-client design ever reopen the door to non-root. Not built.
- The mutation lane has not been run on the changed lines.
### MW-2026-010: a program of a bowl started by root is the machine's root

| | |
|---|---|
| Status | Fixed at the hardened bowl tier on the worktree branch, 2026-10-26; default tier unchanged (owner's decision, below) |
| Class | Confused-deputy / sandbox escape: a compromised bowl program (attacker 8) acts as host root |
| Attacker | 8 (a taken-over program of a bowl: a browser, a KDE component), and by the same mechanism anything a bowl runs |
| Found by | This review and, independently, agent 4, 2026-10-26 |

**What happened.** A bowl's programs run in a user namespace. `bowl_become_user`
(src/bowl.c) made the namespace with `ul_namespace_map("/proc/self/uid_map",
BOWL_USER_ID, uid, 1, ...)`, mapping the bowl user's inside id 1000 to the
launcher's *outside* id. When the launcher is root -- the Canvas terminal, the
serial console, every `/bowls/bin` launcher the machine runs, the whole KDE
session -- that outside id is 0, so the bowl program was the machine's root to
the kernel: `id -u` reads 1000 inside, but every file it creates is root's,
every file root owns is its own, and a signal it sends is root's. The
non-isolated ("fast") view also keeps the host's `/root` and `/run/moonwater`.
This was deliberate (commit `6b81fbd2`, "the machine still sees root ... so the
home, /etc, the runtime directory and the devices a compositor opens need no
other mode"), left in that commit as "the one behaviour change to decide on";
it was never reconciled with attacker 8, under whom it means a compromised bowl
program is unconfined root.

**How it was reproduced.** A disposable QEMU guest on the build box, image of
current main (`final-head.efi`) with the KDE/Alpine bowls disk, driven from the
root serial shell. As a bowl program (`bowl /bowls/alpine /bin/sh -c ...`):

    id                      -> uid=1000(user) ...
    cat /proc/self/uid_map  -> 1000 0 1          (inside 1000 == host root)
    grep CapEff /proc/self/status -> 000001ffffffffff
    od -An -tx1 /root/link.key  -> the 32 bytes of the machine's private link key
    echo ... > /root/main.moonwater.sh ; ls -ln  -> written, owner 0 0, mode 644
    kill -0 1               -> succeeds

The written `/root/main.moonwater.sh` is a root-owned, non-group/non-world
writable regular file, which passes the machine script's own validation
(test/run rows at `1-ow`/`1-gp`/`1-nl`/`1-if`) and is sourced and run as root
at the next event: full root code execution and persistence. Agent 4 reproduced
the same, reading `/root/link.key`.

**The fix (worktree branch).** At the hardened bowl tier
(`MOONWATER_STRICT_BOWL >= STRICT_TIGHT`, profile `sec_bowl_hardened` and the
locked tier) `bowl_become_user` now drops the launch to a *real* unprivileged
kernel id before it makes the namespace: it sets the supplementary groups a
desktop's devices need (`video`, `render`, `input`, `audio`, `tty` -- not
`disk` or `kmem`), `setresgid`/`setresuid` to 1000, restores `PR_SET_DUMPABLE`
(the drop cleared it, which would refuse the id-map writes), makes a user
namespace mapping 1000 to 1000 (identity), keeps that namespace's capabilities
across exec for nested launches and a browser's own sandbox (they are the
namespace's, not the host's: root is unmapped, so they reach nothing the
machine owns), and sets `PR_SET_NO_NEW_PRIVS`. The fast view binds a home the
user owns (`/home/user`, prepared 0700 before the drop) over `/root`, so the
session has a writable HOME and the machine's `/root` is out of view; the
runtime directory `/run/user/1000` is prepared as the user's (replacing the
`-> /run/user/0` symlink the mapped-root design left). It fails closed: a step
it cannot take refuses the launch, where the default tier runs it as root.
`bowl setup`, `bowl --root`, `bowl udev`, `bowl bus` and a bare `bowl ROOT`
console shell stay root inside the bowl's own tree, as before. The default tier
is unchanged, 1:1 with before.

Proven in a guest built from this branch at `sec_bowl_hardened`
(`/home/dawn/sec-1-img`): a bowl program from root reads `uid_map` `1000 1000
1`, cannot read `/root/link.key` (hidden), cannot write the machine's real
`/root/main.moonwater.sh` (its write lands in its own `/home/user`, the real
`/root` untouched), and cannot `kill -0 1`; after `bowl udev` sets the DRM nodes
to 0660 group `video`/`input`/`render` it opens `card0`, `renderD128` and
`event0`; and the KDE session's `kwin_wayland`, `plasmashell` and `kded6` run as
`user` (uid 1000) with a working `wayland-0` socket and nested launches
(`dbus-run-session` -> `startplasma-wayland`) succeed.

**Tests that fail without the fix.** `sh test/run desktop` with
`MOONWATER_BOWL_TIER=hardened` against a hardened-bowl image, three rows in
`lane_desktop` (test/run): "a bowl program started by root is a real
unprivileged kernel id, not root" (uid_map host side), "and cannot read a 0600
secret the machine keeps under /root", "and cannot signal a root process of the
machine". Built OLD (this review's `src/bowl.c` reverted to HEAD) at the same
tier (`/home/dawn/sec-1-old-img`), the same three in the same guest read `host
uid 0`, `READ`, `CAN` -- red; built from the fix they read `nonzero`, `hidden`,
`refused` -- green. The lane's existing weston rows ("finds the screen", "draws
its shell: a panel and a body, neither black") run weston as the bowl user and
so also cover that the confined compositor still draws. The rows are gated by
`MOONWATER_BOWL_TIER`, as the firewall and usb lanes gate their tiers, because
the default tier is intentionally 1:1 with before.

**What is still open.**

- **Owner decision (behaviour change for legitimate users).** The confinement
  is at the hardened bowl tier only; the default tier still runs a bowl program
  started by root as the machine's root, by the owner's deferred decision from
  `6b81fbd2`. Promoting it to the default would move a desktop session's config
  from the machine's `/root` into `/home/user` and could affect a setup that
  reads `/root` from inside a bowl. Recommended to promote once the full KDE
  session (seat-managed, via `lane_desktop` with a network) is confirmed to
  draw at the hardened tier.
- The full `lane_desktop` weston draw at the hardened tier was not run offline
  (base.img has no weston/seatd and the lane installs them over the network);
  the building blocks were verified by hand (device-node access for the dropped
  id, kwin/plasmashell running as 1000 with a Wayland socket). KWin's "Failed to
  open drm node" in a hand-run session was the absent seat manager, not the id
  change.
- The `moonwater security packages` and `sudo` switches (coordinator, API on
  main at `12f31f6b`) are enforced separately; this entry is the id drop only.

### MW-2026-011: `moonwater security packages off` is enforced in the bowl launcher

| | |
|---|---|
| Status | Enforced on the worktree branch, 2026-10-26 |
| Class | Hardening control (attacker 8): a bowl's programs running the distribution's package manager |
| Related | `12f31f6b` (the switch, its settings entry, `security_switch_on`) |

**What this is.** The owner's `packages` switch (`12f31f6b`) says whether a
bowl's programs may run the distribution's package manager -- on by default, off
(and refused as on) at the bowl's tight tier. The switch, its publishing to
`/run/moonwater/security` and the accessor `security_switch_on` landed on main;
this is the enforcement half in `bowl.c`. A package manager, run in a bowl's
isolated view, is root in the bowl's own system tree and can install or replace
anything in it -- a persistence and tampering surface a taken-over program of a
bowl (attacker 8) should not reach once the owner has closed it.

**The enforcement.** `bowl_main` (src/bowl.c), before any namespace is entered,
refuses a launch that is a known package manager (by executable name,
`bowl_needs_isolated`, with `zypper` added) or that asks for a root-in-the-tree
view (`--root`, `--isolated`, an auto-isolated manager) when
`security_switch_on("packages")` is false: "the package manager is off for the
programs of a bowl". It is asked host-side on purpose -- a fast program's view
keeps the host's `/run`, so a program that reaches `bowl` by its launcher reads
the machine's switch, not a bowl's own tmpfs `/run` (which would answer the
default). `bowl setup` and `bowl profile`, the owner's own host actions, are
dispatched above this point and stay allowed.

**Tests that fail without the fix.** `sh test/run desktop`, two rows in
`lane_desktop` (test/run): with `moonwater security packages off`,
`bowl /bowls/alpine /sbin/apk --version` is refused; with it on, `apk` runs and
prints its version. Proven in a guest with the Alpine bowl: on an image built
from this, packages off refuses apk and packages on runs it
(`apk-tools 3.0.6-r0`); on an image with the gate removed (12f31f6b present, so
the switch exists and publishes), packages off still runs apk -- red.

**Still open.** The name check does not catch a package manager copied to
another name (a canonical-path check in the bowl was not added; at the hardened
tier a renamed manager run in a fast view is the unprivileged id of MW-2026-010
and cannot write the root-owned tree anyway, and at the default tier this is a
residual). The `sudo` switch's enforcement (the CLI's own root check, which must
distinguish a bowl's namespace-root from the test harness's `unshare -Urmnu`) is
a separate change in `host.c` and is not in this entry.

### MW-2026-042: a bowl's guest could have the host's files copied into its own configuration

| | |
|---|---|
| Status | Fixed on the worktree branch, 2026-10-26; awaiting cherry-pick to main |
| Class | Link following: a path inside a guest tree resolved by the host, read and written into the guest |
| Attacker | A guest: a bowl archive from a mirror, or whatever runs as root inside a bowl, that plants a link at one of the files the bowl rewrites |
| Reachable | `bowl --isolated ROOT ...`, `bowl setup`, the landing of a tree (`bowl_configure`, `bowl_write_resolv`), src/bowl.c |

**What happened.** The bowl rewrites a guest's `/etc/hosts` (an isolated entry
keeps the guest's own and adds the machine's line), `/etc/pacman.d/mirrorlist`,
`/etc/pacman.conf`, `/etc/apk/repositories` and the Nix seed, and reads each
before it writes. The reads joined the root onto the path (`/bowls/x` +
`/etc/hosts`) and gave the string to `file_slurp` or `file_read_once_at`, so
the *host* resolved it. An absolute link in the guest (`/etc/hosts ->
/root/wifi`, `/etc/shadow`, `/root/link.key`) named the host's file. The write
is safe (the parent is opened without following a link and the new file is
renamed over the name), but what it wrote was the host file's first four
kilobytes with the machine's line after it: the host's file, in a copy of mode
0644 inside the guest, readable by every program in the guest. The same read
was a blocking open, so a FIFO at the name held the verb.

**Reproduced.** `/bowls/x/etc/hosts -> /root/hostsecret` (a host file holding
`secret-from-the-host`), then `bowl --isolated /bowls/x /bin/true`: the guest's
`/etc/hosts` reads `secret-from-the-host` and `127.0.1.1 box`. With a FIFO at
the name the run ends at its limit (124). Nothing else was needed: a guest
that can make a link in its own tree.

**The fix.** `bowl_slurp_in_root` opens the path with `bowl_open_in_root`
(absolute links resolve inside the guest, nothing climbs above it: the rule
`bowl_has` and the program lookups already keep), nonblocking, and reads it
only when it is a regular file. The six reads use it with `file_slurp`'s
capacity. A name the guest does not have inside itself is a file that is not
there. A hosts file past the room it is read into is no longer written back
cut (it is left as it is).

**Tests that fail without the fix.** The cli lane (`sh test/run cli`,
`--harness moonwater_cli`), the rows after "a FIFO at /etc/resolv.conf": the
guest's hosts file holds no line of the host's and starts with the machine's
own `127.0.0.1 localhost`, and the FIFO run does not end 124. Red on the parent
shell, green after. Commits: fdbd44d0 (the read; aab14b5a on the worktree branch), 10773ac5 (the cut write).

**Still open.** The other reads of the guest are in-root already
(`bowl_open_in_root`: programs, the unpacked archive's markers); the writes
open the parent without following a link. The bowl's configuration files are
read once and written once, with no lock against a guest that swaps a
directory for a link between the two; the write's parent is opened without
following, so the swap can only send a write to a different name inside the
tree it already owns.

### MW-2026-043: `bowl profile desktop remove` could delete a host file through a link a guest made

| | |
|---|---|
| Status | Fixed on main, 2026-10-30 (f889784b); the FIFO half in 64cb4690 |
| Class | Link following: a path inside a guest tree resolved by the host, here for removal and append |
| Attacker | A guest: whatever runs as root inside a bowl, or a bowl archive that plants a link at a profile's marker |
| Reachable | `bowl profile desktop remove` and the install of a profile (`bowl_profile_*` in src/bowl.c), run by root on the host |

**What happened.** A bowl profile keeps a marker in the guest's tree (the
desktop profile's `/root/desktop`). The marker's path was joined onto the
bowl's root and handed to the host's open, read and remove, so the host
resolved it. A guest that made `ROOT/root/desktop` an absolute link to the
host's `/root/desktop` (the file that says the desktop is on) had `bowl
profile desktop remove` delete the host's file; an install appended to it. The
same class had two blocking opens of guest files (`/etc/passwd`, the bus
service files): a FIFO there held `bowl bus run` and `bowl bus` for ever
(64cb4690).

**The fix.** The marker is opened, read and removed inside the bowl's root
(`bowl_open_in_root`: absolute links resolve inside the guest, nothing climbs
above it), nonblocking, and a FIFO or any file that is not regular is not a
marker. The guest's links stay in the guest.

**Tests that fail without the fix.** The cli lane's bowl rows: with the link in
place the parent shell deletes the host's `/root/desktop` and the fixed shell
keeps it `on`; the unit rows (bowl 1506/1506, tight 1508/1508); the FIFO rows
ended at their limit (124) on the parent and return at once after.

**Related.** MW-2026-042 is the same class on the guest's configuration reads.

### MW-2026-070: a Waterlink member paired in the half second after its group was taken back

| | |
|---|---|
| Status | Fixed on the worktree branch, 2026-10-30; awaiting cherry-pick to main |
| Class | Use after revocation through a race (CWE-362): the listener acted on its cached copy of a group after the authoritative record was removed or changed, with nothing holding the change back until the copy was read again |
| Attacker | (8) a machine that was paired through a group and is being put out of it, or anyone who holds the group's old secret or a code taken back |
| Reachable | src/waterlink/nearby.c `link_pair_keep`, from `link_server_initiation` for every member's greeting |
| Moonwater exposure window | 2026-09-23 15:17 +02:00 (`a4d6fb80`, group pairing) to 2026-10-30 12:55 +01:00 (`082c7023`): 36 days 22 hours 38 minutes |
| Fixed by | `082c7023` |

**How it was found.** The class review for CVE-2026-98432 (Linux IPv4 TCP:
a connection request's IP options buffer handed to the accepted socket by
pointer and freed while another CPU answering a retransmitted SYN still read
it). This is not that bug: it is the class's general form in Moonwater's own
code, an object one side frees while another side still acts on its own
reference to it, with no grace period between them. No upstream timeline
applies.

**What happened.** The listener keeps a copy of `/root/link.groups` in
`link_nearby` and reads the file again at most twice a second
(`link_nearby_reload`). `link_pair_keep` decided from that copy whether the
group a greeting opened was still open, and gave a new member the copy's
grants. Every command that changes the groups file (`link group`, `link group
leave`, `link pair` and its code taken back) does it under the peers lock and
returns at once. So for up to half a second after a command returned, the
listener paired by what the command had just taken away:

- `link group leave NAME forget` removes the group, then removes every machine
  it paired, and prints how many. A member greeting in the window (a member
  being put out greets as fast as it likes; each greeting needs only a newer
  stamp) was written back to `/root/link.peers` with the group's grants. The
  record stays after the listener reads the file again, since nothing removes
  a member of a group that is gone: `forget` silently did not forget.
- `link group NAME SECRET allow GRANT...` that drops a grant (MW-2026-040)
  takes it from the members, but a member new in the window was given the
  copy's wider grants, for good.
- `link group NAME NEWSECRET` (a secret that leaked, rotated) still let the
  old secret pair a new member in the window.
- A pairing code the waiting machine took back (`link_pair_close`, after
  "stopped, and the code no longer works") still paired a machine in the
  window.

**Reproduced.** CHECK_waterlink_service drives the real command and listener
functions: it pairs a member, runs `link_group_leave("office", true)`, and
delivers the member's next greeting to `link_server_initiation` before the
listener reads the file again; likewise a narrowed record, a re-keyed record,
`link_pair_close` on a code and a groups file at 0644. On the parent nearby.c
the member is back in the peers file and greeted back each time. A concurrent
stress (scratch copy of the check, not committed) ran the command side in a
forked process -- join, wait 0.7 s, `link_group_leave(..., true)`, wait 1.2 s
past the listener's next look, count members left -- against this process
running the real `link_nearby_reload` and greeting as the member as fast as it
could, in its own user, mount and network namespace, built with
`MW_UBSAN=1 MW_HARDEN=1` flags, 180 s each: the parent nearby.c kept a
forgotten member after 95 of 95 cycles (344,377 greetings); the fix after 0 of
95 (350,048 greetings), with no sanitizer trap in either.

**The fix.** The copy now only says which keys a greeting may open.
`link_group_held` reads the record with the copy's key and namespace from the
groups file, under the peers lock `link_pair_keep` already holds, and the open
check (code flags, expiry, expected name) and the new member's grants come
from that record; `DONE` in either the file or the copy closes a code, since
the copy is set first and the file write may fail. A group the file no longer
holds, or a file that cannot be read (as the listener's next look would also
treat it), pairs nobody and moves nobody. The cost is one read of a file of at
most 1 KiB per greeting that reaches pairing, after its handshake.

**Tests that fail without the fix.** `CHECK_waterlink_service`
(`sh test/run waterlink`, the service half), `group_revocations`:
- `sec: a group left with its machines forgotten takes none of them back
  before the listener reads the file again`
- `sec: a member new to a group narrowed since the listener read it is given
  what the group gives now, not what it took back`
- `sec: a group given another secret is opened by the old one no more, before
  the listener reads the file again`
- `sec: a code taken back lets nobody in, before the listener reads the file
  again`
- `sec: a groups file that cannot be read pairs nobody`

Box run (`/home/dawn/handoff-red`, `/home/dawn/handoff-fix`, x86_64): parent
nearby.c with the new checks 1000 checks, 5 failures (the five above); the fix
1000 of 1000, and 1000 of 1000 again built with the `MW_UBSAN` + `MW_HARDEN`
flags and with `MW_UBSAN` + `-ftrivial-auto-var-init=pattern`. The `greetings`
check now writes its group to the file as the code and freshness checks did.

`waterlink_pre_fuzz` (`python3 test/differential.py --harness
waterlink_pre_fuzz`, in the waterlink lane): its lift reads the groups file
through a `link_groups_read` stand-in over the driver's own copy, and a
greeting's mode 64 takes the group out of that copy after the listener's was
set. The invariant `a group the file no longer holds let a machine in` aborts
the parent nearby.c on the new seed `groups_taken_back_since_read.bin`; with
the fix, 21 seeds under libFuzzer ASan/UBSan are clean.

**Still open.** The listener still greets other members from its copy for up
to half a second after `link group leave` (`link_pair_begin`): that offers
this machine to them, which they accept by their own groups, and gives them
nothing here. A session already open when a member is forgotten ends within a
second (`link_sessions_granted`), as for a peer removed by name. The mutation
lane has not been run on `link_group_held`.

## Reviews with no exposure found

Disclosures read against the code on 2026-10-23, each with the check that was
made. Advisory numbers are as secondary sources gave them; none was checked
against NVD. A row here is evidence the class was looked at on that date, not a
promise about later code.

| Class (public disclosure) | Result | Check made |
|---|---|---|
| Host treated as IP by one component and as a name by another (Node.js, Oct 2026) | Not exposed | `string_to_host` takes strict dotted quads only; the connect path and `tls_general_name_match` classify a host by that one function; `--harness tls_hostnames` holds the name check to OpenSSL over 1531 pairs, NUL bytes and partial wildcards included |
| X.509 name-constraint bypass via wildcards, case, mixed name types, unconstrained middle CA (wolfSSL, GnuTLS, pyca, Go, AWS-LC, 2026) | Not exposed | `tls_chains --random`: 1,500 random constrained chains over three seeds, none accepted that OpenSSL refuses; wildcard under an excluded subtree refused, including in another letter case |
| ECDSA edge cases: r or s of 0, point at infinity, invalid curve points, DER laxness (Psychic Signatures 2022, OSEC-2026-13) | Not exposed | Official Wycheproof vectors through the production crypto on x86-64, arm64 and riscv64: 8837 of 8837 each (`MOONWATER_WYCHEPROOF`, lane `net_crypto`) |
| CurveBall (explicit EC parameters), MD5 or SHA-1 signatures, inner and outer signature algorithm mismatch | Not exposed | `tls_parse_cert` accepts the P-256 and P-384 OIDs only, ECDSA-SHA2 and RSA-PKCS1-SHA2 only, and requires the two algorithm fields to match |
| DHCP option injection into scripts or lease files (dracut, FreeBSD dhclient, TP-Link, 2026); TunnelVision, option 121 routes (2024) | Not exposed | The client asks for options 1, 3 and 6 and reads 1, 3, 6, 51, 54, 58 and 59 as binary values; no string option is consumed and no route option is read |
| Credentials carried across a redirect (curl 2026 series; wget CVE-2021-31879) | Not exposed | The HTTP client has no userinfo, `Authorization`, cookies, netrc or proxy; userinfo in a URL or `Location` is refused |
| Brotli decoder write (Node.js, Oct 2026); response decompression bombs | Not exposed | No Brotli in the tree; the client sends no `Accept-Encoding` and decodes no content encoding |
| TLS session-ticket forgery (nginx, Oct 2026) | Not exposed | The client offers no PSK and resumes nothing |
| WPS setup bypass (Broadcom, Oct 2026) | Not exposed | No WPS code |
| Unbound source-port side channel; DNS cache poisoning in general | Not exposed | The stub draws a CSPRNG source port per query on its own connected socket, randomizes the question's letter case, checks the ID and the echoed question |
| PwnEscape, CVE-2026-99999 (kernel IPv4 checksum offset, Oct 2026) | Not reachable as configured | The profiles have no TUN or vhost; AF_PACKET with a virtio header needs CAP_NET_RAW. Kernel pinned at 7.3; confirm `ip_check_csum_start` is in the tarball at the next fetch |
| CVE-2026-98432 (kernel IPv4 TCP use-after-free: `tcp_v4_syn_recv_sock` hands `ireq->ireq_opt` to the child socket by pointer, and a plain `kfree()` races a replayed-SYN reader; kernel CNA CVSS 9.8, Oct 2026) | Not vulnerable (upstream fix present) | Pinned Linux 7.3 (tarball git-archive `c03c1a14`, sha256 `87363c2…`) carries the fix `e3f33b0a1d89` (Dumazet; shipped in 7.2.10 and 7.3). Every free of a published `ip_options_rcu` is RCU-deferred: `inet_sock_destruct` `kfree_rcu` (net/ipv4/af_inet.c:164), `tcp_v4_reqsk_destructor` `kfree_rcu` (net/ipv4/tcp_ipv4.c:1212), IP_OPTIONS replace `kfree_rcu(old, rcu)` (net/ipv4/ip_sockglue.c:1110). Both readers are under `rcu_read_lock`: `tcp_v4_send_synack` (tcp_ipv4.c:1195) and `inet_csk_route_req` (inet_connection_sock.c:767). `tcp_v4_syn_recv_sock` keeps a single owner (tcp_ipv4.c:1765/1788); the syncookie (syncookies.c:446) and reuseport-migration (`inet_reqsk_clone` + `reqsk_migrate_reset`, inet_connection_sock.c:913/975) paths share the same RCU-deferred free. IPv6 duplicates (`ipv6_dup_options`, net/ipv6/tcp_ipv6.c) so it is unaffected. Our only net edit is the read-only `mw_net_silent_gate` RST/ICMP guard; the built tree is byte-identical to the pristine tarball on af_inet.c/inet_connection_sock.c/ip_sockglue.c/syncookies.c, and tcp_ipv4.c differs only by that guard, not the option path. MPTCP/NETLABEL/NETFILTER are off under allnoconfig in every profile combination. Tripwire: `sh test/run kit` row `kernel_cve` goes red if a later fetch reverts `inet_opt`/`ireq_opt` to a plain `kfree` (proven green on 7.3, red on the pre-fix free). KASAN soak deferred (shared box under load avg 22); recipe below |
| NTP time shifting (published attacks on ntpd, 2021 and after) | Bounded | A set clock steps at most 2 s on one server's word; a bigger step needs a second server; no step goes back before the last authenticated time. No cumulative cap across polls (about 2 s per poll, polls at 4 to 30 minutes) |
| Wi-Fi downgrade and Dragonblood (WPA3 to WPA2, SAE timing) | Read, not exercised today | SAE with hash-to-element, hunting fixed at 40 whole passes, PMF required where offered, and a network joined with WPA3 is never joined with WPA2 (`src/sh/host.c`) |
| Output file named by the server (wget CVE-2016-4971 and the Content-Disposition family); a symlink planted where the file will go | Not exposed | A hostile local server against the built shell: `Content-Disposition` names with `../`, with an absolute path and with `filename*`, and redirects to `..%2f`, `/../` and a query holding `../`. Every file landed inside the working directory, named from the requested URL, not from the header or the redirect. A `Location` holding a newline or ESC byte is refused. An existing `index.html` symlink was not followed (wget wrote `index.html.1`, the target was unchanged) |
| Hostile server: redirect loops, huge or many headers, lengths past 2^64, slow drip | Bounded | 20 redirections, then exit 8. The head is capped at 16 KiB (`HTTP_HEAD_MAX`): a 64 MiB header line and 2 million headers were refused in under a second at 13 to 14 MB resident. `Content-Length: 99999999999999999999` and a chunk size of 2^64-1 are refused. A one-byte-per-half-second drip is held for as long as the read timeout resets, as GNU wget does |
| Unsigned bootstrap downloads | Not exposed | Every bowl row is a compiled-in SHA-256 or a pinned OpenPGP key; a row that pins nothing is refused (`src/bowl.c`) |
| SSH (Terrapin, regreSSHion) | Not applicable | No SSH implementation in the tree; OpenSSH inside a bowl is the distribution's package |
| An object handed to a longer-lived owner by pointer and freed while another reader still holds it (CVE-2026-98432, Linux TCP request IP options, Oct 2026; reviewed 2026-10-30) | One exposure, MW-2026-070 | The network stack is single-threaded per process, so the class was read as: a callback or nested call freeing what its caller holds, a pointer kept across a buffer that moves, a cached copy acted on after its record changed, and lengths read twice. Waterlink sessions are unmapped only from the turn, never from a frame's sink, and apply reloads the link after every sink call; TLS lends spans only while nothing receives; the overlay refresh copies anchor keys and never runs mid-verification; the CRL cache's slots are not held across calls; netlink restarts restore plain-value state; DNS clamps a MSG_TRUNC length; every confined child's answer is read whole, then checked and used on the parent's own copy; the pool's ordered ring frees nothing before every worker has left. The group-pairing copy was the exposure |

## CVE-2026-98432 dynamic proof (deferred)

The source verdict above was read from the pinned tarball and from the built
tree, and the `kernel_cve` tripwire in `sh test/run kit` fails if a later fetch
reverts the two frees to a plain `kfree()`. A KASAN runtime soak was not run:
the shared box was at load average 22 with ~3 GB free while another workload
held it, and a sanitizer kernel build (needed twice, once mutated) would have
contended destructively. The recipe for the owner, when the box is free:

1. Seed a copy tree: `git archive` HEAD into `/home/dawn/cve98432-tree/src`,
   `cp -a` `artifacts/linux-7.3.tar.xz` and its `.sign` in from
   `/home/dawn/kde-fix-tree/artifacts`, then
   `taskset -c 24-31 sudo sh build.sh arch/x64 debug_none limbo sec_default serial sec_sanitize`
   (add `desktop wifi` only if the association path is wanted; loopback alone
   exercises the race).
2. Boot it like the `ring0` lane (`-enable-kvm -smp 4`, `-serial stdio`,
   `console=ttyS0 panic=-1`). In the guest, run an exerciser that opens a TCP
   listener on loopback, connects clients whose SYNs carry Record Route /
   Timestamp options (`setsockopt(IP_OPTIONS)`), completes and closes the child
   on one thread while other threads replay the captured SYN to the same
   4-tuple via `IP_HDRINCL` raw sockets so `tcp_check_req -> tcp_v4_send_synack`
   runs concurrently; also set `net.ipv4.tcp_syncookies=2` and drive the
   listener-overflow path. Require zero KASAN reports for 5-10 minutes.
3. Prove the test can fail: in a scratch build only, change
   `inet_sock_destruct` (net/ipv4/af_inet.c:164) back to the plain
   `kfree(rcu_dereference_protected(inet->inet_opt, 1));` and, if the race is
   too narrow in the time budget, add a scratch-only `udelay()` between the
   child's detach and free. KASAN should then report a slab-use-after-free in
   `ip_options_build` / `tcp_v4_send_synack` / `inet_csk_route_req`. Say plainly
   if the window was widened.
