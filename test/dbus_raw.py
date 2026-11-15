#!/usr/bin/env python3
"""
A client of the wire alone (Python's socket and struct, no bus library) against Moonwater's session bus, for what a library
would hide: when a portal Response arrives against the answer it follows, and how long a refusal takes.

    python3 dbus_raw.py ADDRESS

ADDRESS is the bus (unix:path=...). Rows of "ok NAME" or "FAIL NAME detail"; the exit status is the number of failures.
"""
import socket
import statistics
import struct
import sys
import time

address = sys.argv[1]
path = address.split("=", 1)[1]
failures = 0
PORTAL = "org.freedesktop.portal.Desktop"
DESKTOP = "/org/freedesktop/portal/desktop"


def row(ok, name, detail=""):
    global failures
    print(("ok " + name) if ok else ("FAIL %s %s" % (name, detail)))
    failures += not ok
    sys.stdout.flush()


def pad(b, n):
    return b + b"\0" * ((-len(b)) % n)


def text(value):
    value = value.encode()
    return struct.pack("<I", len(value)) + value + b"\0"


class Client:
    def __init__(self):
        self.s = socket.socket(socket.AF_UNIX)
        self.s.connect(path)
        self.s.settimeout(3)
        self.serial = 0
        self.held = b""
        self.s.sendall(b"\0AUTH EXTERNAL\r\nDATA\r\nBEGIN\r\n")
        self.call("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "Hello")
        # the authentication's lines (DATA, OK guid) come before the first message
        while b"OK " not in self.held or not self.held[self.held.index(b"OK "):].count(b"\r\n"):
            got = self.s.recv(4096)
            if not got:
                raise EOFError("the bus closed the connection in the authentication")
            self.held += got
        at = self.held.index(b"OK ")
        self.held = self.held[self.held.index(b"\r\n", at) + 2:]
        while True:
            m = self.read()
            if m["type"] == 2:
                self.unique = m["body"][4:4 + struct.unpack_from("<I", m["body"])[0]].decode()
                break

    def call(self, destination, object_path, interface, member, signature="", body=b"", flags=0):
        self.serial += 1
        fields = [(1, "o", text(object_path)), (3, "s", text(member)), (6, "s", text(destination))]
        if interface:
            fields.insert(1, (2, "s", text(interface)))
        if signature:
            fields.append((8, "g", bytes([len(signature)]) + signature.encode() + b"\0"))
        array = b""
        for code, sig, value in fields:
            array = pad(array, 8) + bytes([code, 1]) + sig.encode() + b"\0" + value
        message = pad(b"l" + bytes([1, flags, 1]) + struct.pack("<III", len(body), self.serial, len(array)) + array, 8) + body
        self.s.sendall(message)
        return self.serial

    def read(self):
        """The next message: its type, its fields by code, its body; buffered bytes past it are kept."""
        while True:
            if len(self.held) >= 16:
                body, _, fields = struct.unpack_from("<III", self.held, 4)
                total = ((16 + fields + 7) & ~7) + body
                if len(self.held) >= total:
                    m = self.held[:total]
                    self.held = self.held[total:]
                    break
            got = self.s.recv(65536)
            if not got:
                raise EOFError("the bus closed the connection")
            self.held += got
        kind = m[1]
        fields_end = 16 + struct.unpack_from("<I", m, 12)[0]
        at, parsed = 16, {}
        while at < fields_end:
            at = (at + 7) & ~7
            code, length = m[at], m[at + 1]
            sig = m[at + 2:at + 2 + length].decode()
            at += 3 + length
            if sig in ("s", "o"):
                at = (at + 3) & ~3
                n = struct.unpack_from("<I", m, at)[0]
                parsed[code] = m[at + 4:at + 4 + n].decode()
                at += 5 + n
            elif sig == "g":
                n = m[at]
                parsed[code] = m[at + 1:at + 1 + n].decode()
                at += 2 + n
            else:
                at = (at + 3) & ~3
                parsed[code] = struct.unpack_from("<I", m, at)[0]
                at += 4
        return {"type": kind, "fields": parsed, "body": m[(fields_end + 7) & ~7:]}

    def wait(self, test, seconds=3.0):
        """Messages until one test takes, or None at the end of the time."""
        end = time.time() + seconds
        while time.time() < end:
            self.s.settimeout(max(0.01, end - time.time()))
            try:
                m = self.read()
            except (socket.timeout, OSError, EOFError):
                return None
            if test(m):
                return m
        return None

    def add_match(self, rule):
        self.call("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "AddMatch", "s", text(rule))


def request_path(client, token):
    return "/org/freedesktop/portal/desktop/request/%s/%s" % (client.unique[1:].replace(".", "_"), token)


def is_response(m, handle):
    return m["type"] == 4 and m["fields"].get(1) == handle and m["fields"].get(3) == "Response"


def response_code(m):
    return struct.unpack_from("<I", m["body"])[0]


def request_body(two, token):
    """(s parent_window, s title, a{sv} options), or (s window, a{sv} options) when two: the options hold the handle_token. A body
    begins 8-aligned in its message, so an offset in it aligns as the message's does."""
    body = text("")
    if not two:
        body = pad(body, 4) + text("A file")
    body = pad(body, 4)
    length_at = len(body)
    body = pad(body + b"\0\0\0\0", 8)
    first = len(body)
    body = pad(body, 8) + text("handle_token") + b"\1s\0"
    body = pad(body, 4) + text(token)
    return body[:length_at] + struct.pack("<I", len(body) - first) + body[length_at + 4:]


# A file chooser's request, subscribed first (GDBus, libportal, Chromium): the answer is the subscribed path, the Response follows at once.
a = Client()
for member, two in (("OpenFile", False), ("SaveFile", False), ("SaveFiles", False)):
    token = "raw" + member
    handle = request_path(a, token)
    a.add_match("type='signal',interface='org.freedesktop.portal.Request',member='Response',path='%s'" % handle)
    began = time.perf_counter()
    serial = a.call(PORTAL, DESKTOP, "org.freedesktop.portal.FileChooser", member, "ssa{sv}", request_body(two, token))
    answer = a.wait(lambda m: m["type"] in (2, 3) and m["fields"].get(5) == serial)
    response = a.wait(lambda m: is_response(m, handle))
    took = (time.perf_counter() - began) * 1e6
    ok = answer is not None and answer["type"] == 2 and response is not None and response_code(response) == 2
    row(ok, "%s answers its request's path and Response(2, {}) after it, %d us from the call" % (member, took),
        "answer %r response %r" % (answer and answer["fields"], response and response["body"]))

# Subscribed only once the answer is back (Qt): the bus holds the Response until the rule comes, and sends it then.
token = "rawlate"
began = time.perf_counter()
serial = a.call(PORTAL, DESKTOP, "org.freedesktop.portal.FileChooser", "OpenFile", "ssa{sv}", request_body(False, token))
answer = a.wait(lambda m: m["type"] == 2 and m["fields"].get(5) == serial)
handle = answer["body"][4:4 + struct.unpack_from("<I", answer["body"])[0]].decode() if answer else ""
a.add_match("type='signal',interface='org.freedesktop.portal.Request',member='Response',path='%s'" % handle)
response = a.wait(lambda m: is_response(m, handle), 0.5)
took = (time.perf_counter() - began) * 1e6
row(response is not None and response_code(response) == 2 and handle == request_path(a, token),
    "a client that subscribes after the answer, as Qt does, is sent the Response at its AddMatch: %d us from the call" % took, repr(handle))

# Inhibit.CreateMonitor, which before had its path answered and no Response ever sent.
token = "rawmonitor"
handle = request_path(a, token)
a.add_match("type='signal',interface='org.freedesktop.portal.Request',path='%s'" % handle)
serial = a.call(PORTAL, DESKTOP, "org.freedesktop.portal.Inhibit", "CreateMonitor", "sa{sv}", request_body(True, token))
response = a.wait(lambda m: is_response(m, handle))
row(response is not None and response_code(response) == 2, "Inhibit.CreateMonitor's request ends at once: Response(2, {})", repr(response))

# Refusals: a name nobody owns, with auto-start allowed, and StartServiceByName of one, are ServiceUnknown at once.
for name, (destination, object_path, interface, member, signature, body) in {
        "a call to org.a11y.Bus (GetAddress)": ("org.a11y.Bus", "/org/a11y/bus", "org.a11y.Bus", "GetAddress", "", b""),
        "a call to org.freedesktop.secrets (OpenSession)": ("org.freedesktop.secrets", "/org/freedesktop/secrets", "org.freedesktop.Secret.Service",
                                                             "OpenSession", "", b""),
        "StartServiceByName of org.a11y.Bus": ("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "StartServiceByName", "su",
                                               pad(text("org.a11y.Bus"), 4) + struct.pack("<I", 0)),
}.items():
    times, errors = [], set()
    for _ in range(50):
        began = time.perf_counter()
        serial = a.call(destination, object_path, interface, member, signature, body)
        m = a.wait(lambda m: m["type"] in (2, 3) and m["fields"].get(5) == serial)
        times.append((time.perf_counter() - began) * 1e6)
        errors.add(m and m["type"] == 3 and m["fields"].get(4))
    median = statistics.median(times)
    row(errors == {"org.freedesktop.DBus.Error.ServiceUnknown"} and median < 1000,
        "%s is ServiceUnknown in a median of %d us of 50 (the slowest %d), under a millisecond" % (name, median, max(times)), repr(errors))

# The names whose owner every bowl's programs would send keys, passwords and notifications to are no client's (MW-2026-197).
for name in ("org.a11y.Bus", "org.freedesktop.secrets", "org.freedesktop.Notifications"):
    serial = a.call("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "RequestName", "su", pad(text(name), 4) + struct.pack("<I", 2 | 4))
    m = a.wait(lambda m: m["type"] in (2, 3) and m["fields"].get(5) == serial)
    row(m is not None and m["type"] == 3 and m["fields"].get(4) == "org.freedesktop.DBus.Error.AccessDenied", "a client may not own %s: AccessDenied" % name,
        repr(m and m["fields"]))

# The portal interfaces Moonwater does not serve are not claimed: their version is UnknownProperty at once, so a browser hides the feature.
for interface in ("GlobalShortcuts", "ScreenCast", "Screenshot", "RemoteDesktop"):
    times, errors = [], set()
    for _ in range(50):
        began = time.perf_counter()
        serial = a.call(PORTAL, DESKTOP, "org.freedesktop.DBus.Properties", "Get", "ss",
                        pad(text("org.freedesktop.portal." + interface), 4) + text("version"))
        m = a.wait(lambda m: m["type"] in (2, 3) and m["fields"].get(5) == serial)
        times.append((time.perf_counter() - began) * 1e6)
        errors.add(m and m["type"] == 3 and m["fields"].get(4))
    median = statistics.median(times)
    row(errors == {"org.freedesktop.DBus.Error.UnknownProperty"} and median < 1000,
        "the portal's %s is not claimed: its version is UnknownProperty in a median of %d us of 50" % (interface, median), repr(errors))

# The desktop's notifications (MW-2026-198): Notify answers an id, and closing it tells the poster alone.
def notify_body(app, summary, body, expire=-1):
    b = text(app)
    b = pad(b, 4) + struct.pack("<I", 0)
    b = pad(b, 4) + text("")
    b = pad(b, 4) + text(summary)
    b = pad(b, 4) + text(body)
    b = pad(b, 4) + struct.pack("<I", 0)  # actions: none
    b = pad(b, 4) + struct.pack("<I", 0)  # hints: none
    b = pad(b, 8)
    return pad(b, 4) + struct.pack("<i", expire)


NOTIFY = ("org.freedesktop.Notifications", "/org/freedesktop/Notifications", "org.freedesktop.Notifications")
listener = Client()
listener.add_match("type='signal'")
began = time.perf_counter()
serial = a.call(*NOTIFY, "Notify", "susssasa{sv}i", notify_body("Raw", "Hello", "from the wire"))
m = a.wait(lambda m: m["type"] in (2, 3) and m["fields"].get(5) == serial)
took = (time.perf_counter() - began) * 1e6
nid = struct.unpack_from("<I", m["body"])[0] if m and m["type"] == 2 else 0
row(nid > 0, "Notify is answered an id in %d us" % took, repr(m and m["fields"]))
serial = a.call(*NOTIFY, "CloseNotification", "u", struct.pack("<I", nid))
closed = a.wait(lambda m: m["type"] == 4 and m["fields"].get(3) == "NotificationClosed")
row(closed is not None and struct.unpack_from("<II", closed["body"]) == (nid, 3) and closed["fields"].get(6) == a.unique,
    "and CloseNotification of it is NotificationClosed(id, 3) to the poster", repr(closed))
heard = listener.wait(lambda m: m["type"] == 4 and m["fields"].get(3) == "NotificationClosed", 0.3)
row(heard is None, "and a client listening for every signal does not hear it", repr(heard))
serial = a.call(*NOTIFY, "GetCapabilities")
m = a.wait(lambda m: m["type"] in (2, 3) and m["fields"].get(5) == serial)
row(m is not None and m["type"] == 2 and m["body"] == struct.pack("<II", 9, 4) + b"body\0", "GetCapabilities is body alone", repr(m and m["body"]))
burst = 0
for k in range(10):
    serial = a.call(*NOTIFY, "Notify", "susssasa{sv}i", notify_body("Raw", "Burst %d" % k, ""))
    m = a.wait(lambda m: m["type"] in (2, 3) and m["fields"].get(5) == serial)
    burst += m is not None and m["type"] == 3 and m["fields"].get(4) == "org.freedesktop.DBus.Error.LimitsExceeded"
row(burst >= 2, "a client that posts ten at once is refused past its eight: LimitsExceeded (%d)" % burst)
listener.s.close()
a.s.close()
sys.exit(failures)
