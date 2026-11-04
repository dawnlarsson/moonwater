#!/usr/bin/env python3
# The on-screen keyboard's input for the e2e lane (test/osk_e2e.sh): a pointer on the compositor's evdev fifo (-i) taps the
# keyboard's keys, where the keyboard's own trace (osk -t) says each key is.
#
#     osk_input.py FIFO KEYS COMMAND...
#
# A command is one of
#     tap:LABEL          a press and a lift on a key of the page shown
#     hold:LABEL:SECS    a press held for SECS, then the lift
#     text:STRING        each character as a tap: a capital is Shift and the letter, a space is Space, a newline is Enter
#     wait:SECS          nothing, for the screen to settle
#     ctrl:s             Control held over the letter s (Ctrl+S), on the fifo's own keys
#     super:up           Super held over an arrow (Super+Up, the window's fill), on the fifo's own keys
# KEYS is the keyboard's trace: one line a key, "page row column x y w h word", the rectangle in the screen's pixels. The pages
# are numbered as the keyboard numbers them (0 letters, 1 numbers, 2 the second symbols page), and the commands follow the page
# that a key on the page shown changes to (123, ABC and #+= do).
import os
import struct
import sys
import time

EV_SYN, EV_KEY, EV_REL = 0, 1, 2
BTN_LEFT, REL_X, REL_Y = 0x110, 0, 1


class Pointer:
    def __init__(self, fifo):
        self.fd = os.open(fifo, os.O_WRONLY)
        self.at = [0, 0]

    def send(self, kind, code, value):
        now = time.time()
        os.write(self.fd, struct.pack("qqHHi", int(now), int((now - int(now)) * 1000000), kind, code, value))

    def syn(self):
        self.send(EV_SYN, 0, 0)

    def move(self, x, y):
        # a SYN after each count: the compositor takes the counts between two SYNs as one movement, and its acceleration would
        # turn a batch of counts into a fast one; a count alone is one to one
        for _ in range(abs(x - self.at[0])):
            self.send(EV_REL, REL_X, 1 if x > self.at[0] else -1)
            self.syn()
        for _ in range(abs(y - self.at[1])):
            self.send(EV_REL, REL_Y, 1 if y > self.at[1] else -1)
            self.syn()
        self.at = [x, y]
        time.sleep(0.02)

    def press(self):
        self.send(EV_KEY, BTN_LEFT, 1)
        self.syn()

    def lift(self):
        self.send(EV_KEY, BTN_LEFT, 0)
        self.syn()


def load(path):
    keys = {}
    for line in open(path):
        fields = line.rstrip("\n").split(" ", 7)
        if len(fields) == 8:
            page, row, column, x, y, w, h = map(int, fields[:7])
            keys[(page, fields[7])] = (x, y, w, h)
    return keys


def main():
    fifo, trace = sys.argv[1], sys.argv[2]
    keys = load(trace)
    pointer = Pointer(fifo)
    page = 0
    # the screen's corner first: a pointer that starts anywhere is brought to (0, 0) by a long move (the compositor holds it on the screen)
    for _ in range(1200):
        pointer.send(EV_REL, REL_X, -1)
        pointer.syn()
        pointer.send(EV_REL, REL_Y, -1)
        pointer.syn()
    pointer.at = [0, 0]
    time.sleep(0.2)

    def centre(label):
        x, y, w, h = keys[(page, label)]
        return x + w // 2, y + h // 2

    def tap(label, seconds=0.0):
        nonlocal page
        pointer.move(*centre(label))
        pointer.press()
        if seconds:
            time.sleep(seconds)
        pointer.lift()
        time.sleep(0.15)
        page = {"123": 1, "ABC": 0, "#+=": 2}.get(label, page)

    for command in sys.argv[3:]:
        kind, _, rest = command.partition(":")
        if kind == "tap":
            tap(rest)
        elif kind == "hold":
            label, _, seconds = rest.partition(":")
            tap(label, float(seconds))
        elif kind == "wait":
            time.sleep(float(rest))
        elif kind == "ctrl":
            # Control held over one letter, as a physical keyboard's keys on the fifo (the compositor takes the fifo for no keyboard, so
            # these reach the window with the keys by the same path as the on-screen keys do)
            key = {"s": 31}[rest]
            for code, value in ((29, 1), (key, 1), (key, 0), (29, 0)):
                pointer.send(EV_KEY, code, value)
                pointer.syn()
            time.sleep(0.3)
        elif kind == "super":
            # Super held over an arrow: the window chords (compositor_tile_chord) take it from the fifo as from a keyboard
            key = {"up": 103, "down": 108, "left": 105, "right": 106}[rest]
            for code, value in ((125, 1), (key, 1), (key, 0), (125, 0)):
                pointer.send(EV_KEY, code, value)
                pointer.syn()
            time.sleep(0.5)
        elif kind == "text":
            for ch in rest:
                if ch == "\n":
                    tap("Enter")
                elif ch == " ":
                    tap("Space")
                elif ch.isupper():
                    tap("Shift")
                    tap(ch.lower())
                else:
                    tap(ch)
        else:
            sys.exit("osk_input.py: no such command: " + command)
    time.sleep(0.5)


if __name__ == "__main__":
    main()
