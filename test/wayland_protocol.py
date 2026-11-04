#!/usr/bin/env python3
"""
Generates src/canvas/wayland_protocol.inc, the Wayland protocol tables the
shim's wire code is driven by, from the protocol XML files.

The shim does not hand-write a request's argument list: each interface's
requests and events, the version each appeared in, which are destructors and
what each argument is are read from the XML a client was built against, so
an opcode or a signature cannot drift. What the shim implements is the list
below, in the order the interfaces get their index; an interface not listed
is not in the table and a client cannot bind it.

    python3 test/wayland_protocol.py --wayland /usr/share/wayland \
        --protocols /usr/share/wayland-protocols --out src/canvas/wayland_protocol.inc
    python3 test/wayland_protocol.py ... --check      # compare, do not write

The output is checked in; the check regenerates it wherever the XML is and
fails if the file differs, so a protocol update is a deliberate commit.
"""
import argparse
import re
import sys
import xml.etree.ElementTree as ET

# (xml path under its root, interfaces). The root is "wayland" for wayland.xml
# and "protocols" for everything else.
CONFIG = [
    ("wayland", "wayland.xml", [
        "wl_display", "wl_registry", "wl_callback", "wl_compositor", "wl_surface", "wl_region", "wl_shm",
        "wl_shm_pool", "wl_buffer", "wl_output", "wl_seat", "wl_pointer", "wl_keyboard", "wl_touch",
        "wl_subcompositor", "wl_subsurface", "wl_data_device_manager", "wl_data_device", "wl_data_source",
        "wl_data_offer",
    ]),
    ("protocols", "stable/xdg-shell/xdg-shell.xml", [
        "xdg_wm_base", "xdg_positioner", "xdg_surface", "xdg_toplevel", "xdg_popup",
    ]),
    ("protocols", "unstable/xdg-output/xdg-output-unstable-v1.xml", [
        "zxdg_output_manager_v1", "zxdg_output_v1",
    ]),
    ("protocols", "unstable/xdg-decoration/xdg-decoration-unstable-v1.xml", [
        "zxdg_decoration_manager_v1", "zxdg_toplevel_decoration_v1",
    ]),
    ("protocols", "stable/viewporter/viewporter.xml", [
        "wp_viewporter", "wp_viewport",
    ]),
    ("protocols", "staging/fractional-scale/fractional-scale-v1.xml", [
        "wp_fractional_scale_manager_v1", "wp_fractional_scale_v1",
    ]),
    ("protocols", "staging/cursor-shape/cursor-shape-v1.xml", [
        "wp_cursor_shape_manager_v1", "wp_cursor_shape_device_v1",
    ]),
    ("protocols", "stable/presentation-time/presentation-time.xml", [
        "wp_presentation", "wp_presentation_feedback",
    ]),
    ("protocols", "staging/single-pixel-buffer/single-pixel-buffer-v1.xml", [
        "wp_single_pixel_buffer_manager_v1",
    ]),
    ("protocols", "staging/xdg-activation/xdg-activation-v1.xml", [
        "xdg_activation_v1", "xdg_activation_token_v1",
    ]),
    ("protocols", "unstable/primary-selection/primary-selection-unstable-v1.xml", [
        "zwp_primary_selection_device_manager_v1", "zwp_primary_selection_device_v1",
        "zwp_primary_selection_offer_v1", "zwp_primary_selection_source_v1",
    ]),
    ("protocols", "stable/linux-dmabuf/linux-dmabuf-v1.xml", [
        "zwp_linux_dmabuf_v1", "zwp_linux_buffer_params_v1", "zwp_linux_dmabuf_feedback_v1",
    ]),
    ("protocols", "unstable/idle-inhibit/idle-inhibit-unstable-v1.xml", [
        "zwp_idle_inhibit_manager_v1", "zwp_idle_inhibitor_v1",
    ]),
    ("protocols", "unstable/relative-pointer/relative-pointer-unstable-v1.xml", [
        "zwp_relative_pointer_manager_v1", "zwp_relative_pointer_v1",
    ]),
    ("protocols", "unstable/pointer-constraints/pointer-constraints-unstable-v1.xml", [
        "zwp_pointer_constraints_v1", "zwp_locked_pointer_v1", "zwp_confined_pointer_v1",
    ]),
    ("protocols", "unstable/keyboard-shortcuts-inhibit/keyboard-shortcuts-inhibit-unstable-v1.xml", [
        "zwp_keyboard_shortcuts_inhibit_manager_v1", "zwp_keyboard_shortcuts_inhibitor_v1",
    ]),
    ("protocols", "staging/tearing-control/tearing-control-v1.xml", [
        "wp_tearing_control_manager_v1", "wp_tearing_control_v1",
    ]),
    ("protocols", "staging/xwayland-shell/xwayland-shell-v1.xml", [
        "xwayland_shell_v1", "xwayland_surface_v1",
    ]),
    ("protocols", "staging/xdg-dialog/xdg-dialog-v1.xml", [
        "xdg_wm_dialog_v1", "xdg_dialog_v1",
    ]),
    ("protocols", "staging/xdg-system-bell/xdg-system-bell-v1.xml", [
        "xdg_system_bell_v1",
    ]),
    ("protocols", "staging/content-type/content-type-v1.xml", [
        "wp_content_type_manager_v1", "wp_content_type_v1",
    ]),
    ("protocols", "staging/alpha-modifier/alpha-modifier-v1.xml", [
        "wp_alpha_modifier_v1", "wp_alpha_modifier_surface_v1",
    ]),
    ("protocols", "staging/fifo/fifo-v1.xml", [
        "wp_fifo_manager_v1", "wp_fifo_v1",
    ]),
    ("protocols", "staging/commit-timing/commit-timing-v1.xml", [
        "wp_commit_timing_manager_v1", "wp_commit_timer_v1",
    ]),
    ("protocols", "unstable/pointer-gestures/pointer-gestures-unstable-v1.xml", [
        "zwp_pointer_gestures_v1", "zwp_pointer_gesture_swipe_v1", "zwp_pointer_gesture_pinch_v1", "zwp_pointer_gesture_hold_v1",
    ]),
    ("protocols", "staging/ext-idle-notify/ext-idle-notify-v1.xml", [
        "ext_idle_notifier_v1", "ext_idle_notification_v1",
    ]),
    ("protocols", "staging/xdg-toplevel-icon/xdg-toplevel-icon-v1.xml", [
        "xdg_toplevel_icon_manager_v1", "xdg_toplevel_icon_v1",
    ]),
    ("protocols", "unstable/xwayland-keyboard-grab/xwayland-keyboard-grab-unstable-v1.xml", [
        "zwp_xwayland_keyboard_grab_manager_v1", "zwp_xwayland_keyboard_grab_v1",
    ]),
]


# The pixel formats the shim reads (wl_shm.format): the two every client has, and the byte-swapped pair.
SHM_FORMATS = ("argb8888", "xrgb8888", "abgr8888", "xbgr8888")


def kind_of(arg, names):
    """The two bytes one argument is in the table: its kind letter and its interface index (0 for none)."""
    t = arg.get("type")
    null = arg.get("allow-null") == "true"
    if t in ("int", "uint", "fixed", "array", "fd"):
        return {"int": "i", "uint": "u", "fixed": "f", "array": "a", "fd": "h"}[t], None
    if t == "string":
        return ("S" if null else "s"), None
    if t == "object":
        return ("O" if null else "o"), arg.get("interface")
    if t == "new_id":
        if arg.get("interface") is None:
            return "N", None
        return "n", arg.get("interface")
    raise SystemExit("unknown argument type %r" % t)


def generate(wayland_root, protocols_root):
    interfaces = []  # (name, element)
    for root, path, wanted in CONFIG:
        base = wayland_root if root == "wayland" else protocols_root
        tree = ET.parse("%s/%s" % (base, path)).getroot()
        found = {i.get("name"): i for i in tree.findall("interface")}
        for name in wanted:
            if name not in found:
                raise SystemExit("%s is not in %s" % (name, path))
            interfaces.append((name, found[name], path))
    index = {name: n + 1 for n, (name, _, _) in enumerate(interfaces)}

    out = []
    w = out.append
    w("/* Generated by test/wayland_protocol.py from the protocol XML. Do not edit: a protocol change is a")
    w("   regeneration, and the check fails when this file is not what the XML says. */")
    w("")
    w("/* The interfaces in the table, from 1; 0 names none. */")
    w("enum {")
    w("        WLI_NONE,")
    for name, _, _ in interfaces:
        w("        WLI_%s," % name)
    w("        WLI_COUNT")
    w("};")
    w("")
    w("/* The version each interface has in the XML these tables were made from. */")
    for name, el, _ in interfaces:
        w("#define WLV_%s %s" % (name, el.get("version")))
    w("")
    w("/* Request opcodes (WLQ_), event opcodes (WLE_) and enum entries (WLN_). */")
    for name, el, _ in interfaces:
        for n, r in enumerate(el.findall("request")):
            w("#define WLQ_%s_%s %d" % (name, r.get("name"), n))
        for n, e in enumerate(el.findall("event")):
            w("#define WLE_%s_%s %d" % (name, e.get("name"), n))
        for en in el.findall("enum"):
            for entry in en.findall("entry"):
                # wl_shm lists a hundred pixel formats; the shim reads the ones it can show.
                if name == "wl_shm" and en.get("name") == "format" and entry.get("name") not in SHM_FORMATS:
                    continue
                label = re.sub(r"[^a-z0-9_]", "_", entry.get("name").lower())
                w("#define WLN_%s_%s_%s %s" % (name, en.get("name"), "e_" + label if label[0].isdigit() else label,
                                               entry.get("value")))
    w("")
    w("""/* An argument is a kind byte and an interface byte (an object's or a new id's, else 0), the list ended by a
   zero kind: i int, u uint, f fixed, s string, S string that may be null, o object, O object that may be null,
   n new id of the interface, N new id of no interface (it is a string, a version and an id on the wire), a array,
   h descriptor. */
struct wl_message
{
        unsigned char since, destructor;
        const unsigned char *args;
};

struct wl_interface_info
{
        const char *name;
        unsigned char version, requests, events;
        const struct wl_message *request;
        const struct wl_message *event;
};
""")

    def arg_list(el):
        pairs = []
        for a in el.findall("arg"):
            k, iface = kind_of(a, index)
            # An object of an interface the shim does not implement (a tablet tool) is any object: its request is
            # refused by the handler, which has nothing to do with it. A new id must be one the table has.
            if iface is not None and iface not in index:
                if k in ("n",):
                    raise SystemExit("%s makes %s, which is not in the table" % (el.get("name"), iface))
                iface = None
            pairs.append("'%s', %s" % (k, "WLI_%s" % iface if iface else "0"))
        pairs.append("0, 0")
        return "{" + ", ".join(pairs) + "}"

    for name, el, _ in interfaces:
        for kind, tag in (("request", "q"), ("event", "e")):
            for n, m in enumerate(el.findall(kind)):
                w("static const unsigned char wl_%s_%s_%s[] = %s;" % (tag, name, m.get("name"), arg_list(m)))
    w("")
    for name, el, _ in interfaces:
        for kind, tag in (("request", "q"), ("event", "e")):
            ms = el.findall(kind)
            if not ms:
                continue
            w("static const struct wl_message wl_%ss_%s[] = {" % (kind, name))
            for m in ms:
                w("        {%s, %d, wl_%s_%s_%s}," % (m.get("since") or "1", 1 if m.get("type") == "destructor" else 0,
                                                   tag, name, m.get("name")))
            w("};")
    w("")
    w("static const struct wl_interface_info wl_interfaces[WLI_COUNT] = {")
    w("        [WLI_NONE] = {0, 0, 0, 0, 0, 0},")
    for name, el, _ in interfaces:
        nr = len(el.findall("request"))
        ne = len(el.findall("event"))
        w("        [WLI_%s] = {\"%s\", %s, %d, %d, %s, %s}," % (name, name, el.get("version"), nr, ne,
                                                             "wl_requests_%s" % name if nr else "0",
                                                             "wl_events_%s" % name if ne else "0"))
    w("};")
    w("")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--wayland", default="/usr/share/wayland")
    ap.add_argument("--protocols", default="/usr/share/wayland-protocols")
    ap.add_argument("--out", required=True)
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    text = generate(args.wayland, args.protocols)
    if args.check:
        try:
            have = open(args.out).read()
        except OSError:
            have = None
        if have != text:
            sys.stderr.write("%s is not what the protocol XML says; run test/wayland_protocol.py\n" % args.out)
            return 1
        return 0
    with open(args.out, "w") as f:
        f.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
