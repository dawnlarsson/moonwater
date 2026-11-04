#!/usr/bin/env python3
"""
GDBus (GLib's own bus client, through PyGObject) against Moonwater's session bus: a client that is not ours, as a program
of a bowl is. Rows of "ok NAME" or "FAIL NAME detail"; the exit status is the number of failures.

    python3 dbus_gio.py ADDRESS SETTINGS_DIRECTORY

ADDRESS is the bus (unix:path=...), SETTINGS_DIRECTORY the directory of the files the bus reads the settings from (/root's
place): the rows change a file there and ask the bus, a second connection, what it says and what it is told.
"""
import os
import sys
import time

import gi

gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib

address, root = sys.argv[1:3]
failures = 0


def row(ok, name, detail=""):
    global failures
    if ok:
        print("ok " + name)
    else:
        print("FAIL %s %s" % (name, detail))
        failures += 1
    sys.stdout.flush()


def connect():
    return Gio.DBusConnection.new_for_address_sync(
        address, Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION, None, None)


def pump(milliseconds):
    end = time.time() + milliseconds / 1000.0
    context = GLib.MainContext.default()
    while time.time() < end:
        while context.iteration(False):
            pass
        time.sleep(0.01)


def call(connection, destination, path, interface, method, parameters, reply_type, spin=False):
    """A call; with spin the connection's own program (another connection in this process) is given its turns while it waits."""
    if not spin:
        try:
            return connection.call_sync(destination, path, interface, method, parameters, GLib.VariantType(reply_type), Gio.DBusCallFlags.NONE, 5000, None)
        except GLib.Error as e:
            return e
    answer = []

    def done(source, result, data):
        try:
            answer.append(source.call_finish(result))
        except GLib.Error as e:
            answer.append(e)

    connection.call(destination, path, interface, method, parameters, GLib.VariantType(reply_type), Gio.DBusCallFlags.NONE, 5000, None, done, None)
    for _ in range(200):
        pump(25)
        if answer:
            break
    return answer[0] if answer else GLib.Error("no answer")


def setting(file, text):
    with open(os.path.join(root, file), "w") as f:
        f.write(text + "\n")


PORTAL = ("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop")
a = connect()
row(a.get_unique_name() is not None and a.get_unique_name().startswith(":1."), "GDBus authenticates (EXTERNAL) and says Hello: %s" % a.get_unique_name())

setting("canvas.theme", "dark")
time.sleep(1.5)
r = call(a, *PORTAL, "org.freedesktop.portal.Settings", "ReadOne", GLib.Variant("(ss)", ("org.freedesktop.appearance", "color-scheme")), "(v)")
row(not isinstance(r, GLib.Error) and r.unpack()[0] == 1, "ReadOne of the color-scheme with the theme dark is 1", str(r))
r = call(a, *PORTAL, "org.freedesktop.portal.Settings", "Read", GLib.Variant("(ss)", ("org.freedesktop.appearance", "color-scheme")), "(v)")
row(not isinstance(r, GLib.Error) and r.unpack()[0] == 1, "and the old Read, a variant in a variant, says the same", str(r))
r = call(a, *PORTAL, "org.freedesktop.portal.Settings", "ReadAll", GLib.Variant("(as)", (["org.freedesktop.appearance", "org.gnome.*"],)),
         "(a{sa{sv}})")
ok = False
if not isinstance(r, GLib.Error):
    everything = r.unpack()[0]
    ok = everything.get("org.freedesktop.appearance", {}).get("color-scheme") == 1 and \
        everything.get("org.gnome.desktop.interface", {}).get("gtk-theme") == "Adwaita-dark" and \
        everything["org.freedesktop.appearance"].get("contrast") == 0
row(ok, "ReadAll of org.freedesktop.appearance and org.gnome.* holds the schemes, the gtk-theme and the contrast", str(r))
r = call(a, *PORTAL, "org.freedesktop.DBus.Properties", "Get", GLib.Variant("(ss)", ("org.freedesktop.portal.Settings", "version")), "(v)")
row(not isinstance(r, GLib.Error) and r.unpack()[0] == 2, "the Settings portal is of version 2", str(r))
r = call(a, *PORTAL, "org.freedesktop.portal.Settings", "ReadOne", GLib.Variant("(ss)", ("org.freedesktop.appearance", "no-such")), "(v)")
row(isinstance(r, GLib.Error) and "NotFound" in str(r), "a setting that is none is the portal's NotFound", str(r))
r = call(a, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "NameHasOwner", GLib.Variant("(s)", ("org.freedesktop.portal.Desktop",)), "(b)")
row(not isinstance(r, GLib.Error) and r.unpack()[0] is True, "the portal's name has an owner", str(r))
r = call(a, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "NameHasOwner", GLib.Variant("(s)", ("org.a11y.Bus",)), "(b)")
row(not isinstance(r, GLib.Error) and r.unpack()[0] is False, "and a name nobody has is not owned", str(r))
r = call(a, "org.a11y.Bus", "/org/a11y/bus", "org.a11y.Bus", "GetAddress", None, "(s)")
row(isinstance(r, GLib.Error) and "ServiceUnknown" in str(r), "a call to a service that is not there is ServiceUnknown, not a hang", str(r))

# a second connection listens for a change and is told
b = connect()
seen = []
b.signal_subscribe("org.freedesktop.portal.Desktop", "org.freedesktop.portal.Settings", "SettingChanged", "/org/freedesktop/portal/desktop", None,
                   Gio.DBusSignalFlags.NONE, lambda c, sender, path, interface, name, parameters, data: seen.append(parameters.unpack()), None)
pump(300)
setting("canvas.theme", "light")
for _ in range(100):
    pump(50)
    if seen:
        break
row(any(s[0] == "org.freedesktop.appearance" and s[1] == "color-scheme" and s[2] == 2 for s in seen),
    "the theme set to light is told to a listener as SettingChanged(org.freedesktop.appearance, color-scheme, 2)", str(seen))
row(any(s[0] == "org.gnome.desktop.interface" and s[1] == "gtk-theme" and s[2] == "Adwaita" for s in seen), "and the gtk-theme with it", str(seen))
seen.clear()
setting("canvas.theme", "light")
pump(1500)
row(not seen, "a file written with what it held says nothing", str(seen))
setting("canvas.theme.accent", "ff8800")
for _ in range(100):
    pump(50)
    if seen:
        break
row(any(s[1] == "accent-color" and abs(s[2][0] - 1.0) < 1e-9 and abs(s[2][1] - 136 / 255.0) < 1e-9 and s[2][2] == 0.0 for s in seen),
    "an accent kept is told as (ddd) in 0 to 1", str(seen))
seen.clear()
setting("canvas.theme.accent", "reset")
for _ in range(100):
    pump(50)
    if seen:
        break
row(any(s[1] == "accent-color" and s[2][0] < 0 for s in seen), "and reset is an accent out of range, which is none", str(seen))
seen.clear()
setting("canvas.motion", "reduced")
for _ in range(100):
    pump(50)
    if seen:
        break
row(any(s[1] == "enable-animations" and s[2] is False for s in seen), "motion reduced is enable-animations false", str(seen))

seen.clear()
setting("canvas.cursor.size", "3")
for _ in range(100):
    pump(50)
    if seen:
        break
row(any(s[1] == "cursor-size" and s[2] == 72 for s in seen), "a pointer size of 3 is a cursor-size of 72 to the programs that draw their own", str(seen))

seen.clear()
setting("time.format", "12h")
for _ in range(100):
    pump(50)
    if seen:
        break
row(any(s[1] == "clock-format" and s[2] == "12h" for s in seen), "the clock read in 12 hours is clock-format 12h to a GNOME program", str(seen))

# two programs: one owns a name and answers, the other calls it by the name
methods = Gio.DBusNodeInfo.new_for_xml(
    "<node><interface name='org.example.Server'><method name='Echo'><arg type='s' direction='in'/><arg type='s' direction='out'/></method>"
    "<signal name='Ping'><arg type='s'/></signal></interface></node>").interfaces[0]
owned = []


def method_call(connection, sender, path, interface, method, parameters, invocation):
    invocation.return_value(GLib.Variant("(s)", ("echo:" + parameters.unpack()[0],)))


b.register_object("/org/example", methods, method_call, None, None)
Gio.bus_own_name_on_connection(b, "org.example.Server", Gio.BusNameOwnerFlags.NONE, lambda c, n: owned.append(n), None)
for _ in range(40):
    pump(50)
    if owned:
        break
row(owned == ["org.example.Server"], "a program owns a name (RequestName)", str(owned))
r = call(a, "org.example.Server", "/org/example", "org.example.Server", "Echo", GLib.Variant("(s)", ("hello",)), "(s)", True)
row(not isinstance(r, GLib.Error) and r.unpack()[0] == "echo:hello", "and another calls it by the name and is answered", str(r))
signals = []
a.signal_subscribe(None, "org.example.Server", "Ping", "/org/example", None, Gio.DBusSignalFlags.NONE,
                   lambda c, sender, path, interface, name, parameters, data: signals.append((sender, parameters.unpack())), None)
pump(200)
b.emit_signal(None, "/org/example", "org.example.Server", "Ping", GLib.Variant("(s)", ("up",)))
for _ in range(40):
    pump(50)
    if signals:
        break
row(len(signals) == 1 and signals[0][1] == ("up",) and signals[0][0] == b.get_unique_name(), "a signal of one program reaches the one that listens, from the sender the bus says",
    str(signals))
c = connect()
r = call(c, "org.example.Server", "/org/example", "org.example.Server", "Echo", GLib.Variant("(s)", ("again",)), "(s)", True)
row(not isinstance(r, GLib.Error) and r.unpack()[0] == "echo:again", "a third connection calls it as well", str(r))
b.close_sync(None)
pump(300)
r = call(c, "org.example.Server", "/org/example", "org.example.Server", "Echo", GLib.Variant("(s)", ("gone",)), "(s)")
row(isinstance(r, GLib.Error) and "ServiceUnknown" in str(r) or "NameHasNoOwner" in str(r), "when the owner goes the name goes with it", str(r))
c.close_sync(None)
a.close_sync(None)
sys.exit(failures)
