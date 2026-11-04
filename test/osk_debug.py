#!/usr/bin/env python3
# The e2e lane's reading of moonwater debug (test/osk_e2e.sh): a window's place, and the keyboard line.
#
#     osk_debug.py place FILE TITLE      the window titled TITLE: "x y width height", or nothing when there is none
#     osk_debug.py keyboard FILE         "shown|hidden FIELDS INPUT PHYSICAL START": the keyboard's state, the text fields with the keys in
#                                        the focus, the last input (the mouse, a touch or a pen, a key), a physical keyboard, and whether
#                                        the desktop starts it (yes or no)
import re
import sys

WINDOW = re.compile(r"^  window\s+\d+\s+(.*?)\s+at\s+(-?\d+),(-?\d+),\s+(\d+)x(\d+)\s*$")
KEYBOARD = re.compile(r"^  keyboard\s+setting \S+; (shown|hidden); the focus has (\d+) text fields? with the keys; "
                      r"last input (.*?); a physical keyboard (plugged in|none); the desktop starts it (yes|no)\s*$")
TEXT = re.compile(r"^  text input\s+the focus has its field's content purpose (\d+) and cursor bottom (\d+) \(0 for none\)\s*$")


def main():
    mode, path = sys.argv[1], sys.argv[2]
    lines = open(path, encoding="utf-8", errors="replace").read().split("\n")
    if mode == "place":
        for line in lines:
            found = WINDOW.match(line)
            if found and found.group(1) == sys.argv[3]:
                print(" ".join(found.group(i) for i in (2, 3, 4, 5)))
                return
        return
    if mode == "keyboard":
        for line in lines:
            found = KEYBOARD.match(line)
            if found:
                state, fields, last, physical, start = found.groups()
                print(state, fields, last.replace(" ", "_"), physical, start)
                return
        print("none")
        return
    if mode == "text":
        # the focus's content purpose and cursor bottom (the debug line the text input rows read)
        for line in lines:
            found = TEXT.match(line)
            if found:
                print(" ".join(found.groups()))
                return
        print("none")
        return
    sys.exit("osk_debug.py: no such reading: " + mode)


if __name__ == "__main__":
    main()
