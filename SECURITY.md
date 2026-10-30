# Security

How to report a problem, and the threat models of the services that hold
someone's data. Incidents and their exposure windows are logged in
`incident_response.md`.

## Reporting

Open a private security advisory on the repository, or write to the
maintainer. Say what you found, how to reproduce it with input anyone can make,
and what it reaches. We answer with a fix on main and an entry in
`incident_response.md`.

## The clipboard (Canvas)

The compositor (`src/canvas/compositor.c`, `compositor_clip_*`) holds two
selections: CLIPBOARD (copy and paste) and PRIMARY (the last selection, pasted
with the middle button). Programs reach them through the window table
(`src/canvas/window.c`, `windows_clip_*`); scripts through
`moonwater clipboard`. The setting `moonwater canvas clipboard on|off`,
`... history N` and `... limit MIB` govern it.

### What it holds and where

- Text only: `text/plain;charset=utf-8`, valid UTF-8 (shortest forms, no
  surrogates), no NUL, at most the limit (16 MiB by default, 1 to 16).
  Anything else is declined at the copy.
- A copy is a memfd the copying program makes and **seals** against growing,
  shrinking and writing. The compositor opens it through `/proc/PID/fd`, checks
  the seals, the size and the text, and keeps it at descriptor 900 (CLIPBOARD)
  or 901 (PRIMARY), numbers nothing else is given. No byte is copied; the
  copying program cannot change the text after it was taken; the copy outlives
  the window that made it.
- Nothing is written to a disk. The history (`canvas clipboard history N`,
  0 to 32, **0 by default**) is memfds in the compositor's memory. Turning the
  service off, `moonwater clipboard clear`, or the compositor ending empties
  both selections and the history.

### Who may read and write

| Who | Copy | Paste |
|---|---|---|
| The window with the keys, right after a key or a button of the person's on it (1.5 s) | yes | yes |
| Any window right after a gesture of the person's on it | yes | no |
| A window with no gesture of the person's (a background program, a script driving a window) | no | no |
| A popup (a menu) of the window with the keys | as its window | as its window |
| `moonwater clipboard` (root, from a shell) | yes | yes |
| A non-root process, a bowl's program | no access (it cannot map the table) | no access |

The compositor answers a paste request with READY or REFUSED; a refused
window learns nothing, not even the length. A window that asks to watch is told
that the clipboard changed, never what it holds.

### Threats and what stops them

1. **Background snooping.** A window that is not in front reads what was
   copied (a password from a manager). Stopped: a paste is given only to the
   window with the keys, and only within 1.5 s of the person's key or button
   on it. Checked by `CHECK_text_engine` section `clipboard`, which drives the
   real compositor functions through a random stream of offers, asks, focus
   changes and gestures; removing the focus rule turns 225 checks red.
2. **Clipboard hijack.** A program replaces what was copied (a wallet address
   swapped). Stopped: a copy needs a gesture on the copying window; the text
   is a sealed memfd, so the program cannot change it after the compositor
   took it. Removing the seal check or the gesture rule turns the check red.
3. **Paste injection into a terminal.** Copied text from a web page or a file
   carries `ESC [ 201 ~` (ending a bracketed paste early) or other escape
   sequences, so pasting it runs a command or rewrites the screen. Stopped:
   `term_paste_filter` (`src/canvas/term.c`) removes ESC, every C0 control but
   tab and the line ends, DEL and the C1 controls (U+0080 to U+009F) from
   every paste, bracketed or not, so no paste can contain a sequence at all;
   line ends become CR. A program that asked for bracketed paste (DECSET 2004)
   gets the text between the markers; with the terminal's own line editor
   (the shell's prompt) the text goes into the line being typed and nothing
   runs until Enter. Checked against a model of the filter and with the attack
   string; letting ESC through turns 4172 checks red.
4. **Malformed or huge data.** A copy of invalid UTF-8, a NUL that truncates
   the text for a C reader, or a gigabyte. Stopped at the compositor (and again
   by the reader, `windows_clip_read`): checked, bounded, declined.
5. **Output of the command line.** `moonwater clipboard get` and `watch` print
   copied text, which is anybody's. To a terminal, C0 and C1 controls, DEL and
   the bidirectional controls (U+202A to U+202E, U+2066 to U+2069, U+200E,
   U+200F, U+061C) are printed escaped (`\xNN`, `\u{N}`), so the text cannot
   move the cursor, retitle the window or reorder the line it is read on;
   `watch` bounds each line. `get` to a file or a pipe writes the exact bytes,
   which is what a script asked for.
6. **Every root program is trusted.** The table is root's (mode 0600, the
   compositor's memfd reached through `/proc` of a root process); any root
   program can read any window's pixels and could open the compositor's
   descriptors itself. The rules above keep well-behaved root programs from
   snooping by accident and define the protocol a future unprivileged window
   must be held to; they are not a boundary between root programs.

### What is not done

- Bowl programs (a desktop in a bowl, Wayland or X clients) have no access:
  they run their own compositor and its clipboard. A bridge to a Wayland data
  device is a decision for later.
- Only text. No images, no rich text, no files.
- The terminal does not answer OSC 52 (a program setting the clipboard by an
  escape sequence); that would let anything printed to the terminal write the
  clipboard.
