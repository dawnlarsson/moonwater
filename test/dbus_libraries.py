#!/usr/bin/env python3
"""
What the session bus gives its clients, held to the client libraries that read it: every message must be one libdbus
(dbus_message_demarshal) and GDBus (Gio.DBusMessage.new_from_blob) both take. Each library ends its own connection on a
message it cannot read, so a message the bus passes on that one of them refuses lets any client disconnect any other: the
bus's grammar must be at least as strict as theirs.

    DBFUZZ_DUMP=FILE ./check-dbus 0 200000 && python3 test/dbus_libraries.py FILE

FILE is what CHECK_dbus's grammar fuzz wrote: each message a client was given, as four bytes of its length and its bytes.
One line for each message a library refused (at most twenty), then a summary; the exit status is 1 when any was refused.
"""
import ctypes
import struct
import sys

import gi

gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib


class Error(ctypes.Structure):
    _fields_ = [("name", ctypes.c_char_p), ("message", ctypes.c_char_p), ("dummy", ctypes.c_uint), ("padding", ctypes.c_void_p)]


libdbus = ctypes.CDLL("libdbus-1.so.3")
libdbus.dbus_message_demarshal.restype = ctypes.c_void_p
libdbus.dbus_message_demarshal.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.POINTER(Error)]
libdbus.dbus_error_init.argtypes = [ctypes.POINTER(Error)]
libdbus.dbus_error_free.argtypes = [ctypes.POINTER(Error)]
libdbus.dbus_message_unref.argtypes = [ctypes.c_void_p]


def by_libdbus(blob):
    error = Error()
    libdbus.dbus_error_init(ctypes.byref(error))
    message = libdbus.dbus_message_demarshal(blob, len(blob), ctypes.byref(error))
    if message:
        libdbus.dbus_message_unref(ctypes.c_void_p(message))
        return None
    text = (error.message or b"").decode(errors="replace")
    libdbus.dbus_error_free(ctypes.byref(error))
    return text


def body(blob):
    fields = struct.unpack_from("<I", blob, 12)[0]
    return blob[(16 + fields + 7) & ~7:]


def by_gdbus(blob):
    """GDBus must take the message and read it as it is: written out again, its body is the same bytes (a body starts at 8
    in both, so its padding falls the same). GLib 2.90 misread some arrays without an error, which only this shows."""
    try:
        message = Gio.DBusMessage.new_from_blob(blob, Gio.DBusCapabilityFlags.NONE)
    except GLib.Error as error:
        return error.message
    again = message.to_blob(Gio.DBusCapabilityFlags.NONE)
    if body(again) != body(blob):
        return "read otherwise than it was sent: the body written again differs"
    return None


data = open(sys.argv[1], "rb").read()
at = count = refused = 0
while at + 4 <= len(data):
    size = struct.unpack_from("<I", data, at)[0]
    blob = data[at + 4:at + 4 + size]
    at += 4 + size
    count += 1
    for name, verdict in (("libdbus", by_libdbus(blob)), ("GDBus", by_gdbus(blob))):
        if verdict is not None:
            refused += 1
            if refused <= 20:
                print("%s refused message %d of %d bytes: %s\n    %s" % (name, count, size, verdict[:200], blob[:96].hex()))
print("dbus_libraries: %d messages, %d refusals" % (count, refused))
sys.exit(1 if refused else 0)
