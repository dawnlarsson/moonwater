#!/usr/bin/env python3
"""steam_overlay_check.py [STEAM_DIR]: whether Steam's overlay renderer still has every entry steam-overlay-gl.so relies on.

Reads the dynamic symbols of each gameoverlayrenderer.so under STEAM_DIR (default ~/.local/share/Steam; steamrt32 and
steamrt64) and checks the hooks the renderer must export (the ones steam-overlay-gl.so calls into) and the GL calls it must
import by name (the ones steam-overlay-gl.so stands in front of). Prints one line a library and a last line, OK or MISSING;
exit status 0 when every library has everything, 1 when one lacks something (a Steam update changed the renderer: the shim
then does nothing for what is missing), 2 when no renderer is found. Run it in the steam bowl after Steam updates:
    bowl /bowls/arch /usr/bin/python3 -I test/steam_overlay_check.py /root/.local/share/Steam
"""
import os
import struct
import sys

# What the renderer exports and steam-overlay-gl.so calls: its draw at a swap and its Xlib hooks (the hot-key and the overlay's input).
EXPORTS = ["glXSwapBuffers", "XOpenDisplay", "XCreateSimpleWindow", "XNextEvent", "XPending", "IsOverlayEnabled"]
# What the renderer imports by name and steam-overlay-gl.so interposes: its draws, the end of each of its passes, its context query.
IMPORTS = ["glBegin", "glDrawArrays", "glDrawElements", "glPopAttrib", "glXGetCurrentContext"]


def dynamic_symbols(path):
    """(defined, undefined) function names of an ELF shared object's .dynsym."""
    data = open(path, "rb").read()
    if data[:4] != b"\x7fELF":
        raise ValueError("not ELF")
    wide = data[4] == 2
    if wide:
        shoff, = struct.unpack_from("<Q", data, 0x28)
        shentsize, shnum = struct.unpack_from("<HH", data, 0x3A)
    else:
        shoff, = struct.unpack_from("<I", data, 0x20)
        shentsize, shnum = struct.unpack_from("<HH", data, 0x2E)
    sections = []
    for at in range(shnum):
        base = shoff + at * shentsize
        if wide:
            name, kind, flags, addr, offset, size, link, info, align, entsize = struct.unpack_from("<IIQQQQIIQQ", data, base)
        else:
            name, kind, flags, addr, offset, size, link, info, align, entsize = struct.unpack_from("<IIIIIIIIII", data, base)
        sections.append((kind, offset, size, link, entsize))
    defined, undefined = set(), set()
    for kind, offset, size, link, entsize in sections:
        if kind != 11:  # SHT_DYNSYM
            continue
        strings = sections[link]
        for at in range(size // entsize):
            base = offset + at * entsize
            if wide:
                name, info, other, shndx, value, length = struct.unpack_from("<IBBHQQ", data, base)
            else:
                name, value, length, info, other, shndx = struct.unpack_from("<IIIBBH", data, base)
            end = data.index(b"\0", strings[1] + name)
            text = data[strings[1] + name:end].decode("latin-1").split("@")[0]
            if (info & 0xF) != 2:  # STT_FUNC
                continue
            (undefined if shndx == 0 else defined).add(text)
    return defined, undefined


def main():
    steam = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser("~/.local/share/Steam")
    found = 0
    whole = True
    for runtime in ("steamrt32", "steamrt64"):
        path = os.path.join(steam, runtime, "gameoverlayrenderer.so")
        if not os.path.exists(path):
            continue
        found += 1
        defined, undefined = dynamic_symbols(path)
        missing = [name for name in EXPORTS if name not in defined] + [name for name in IMPORTS if name not in undefined]
        whole &= not missing
        print("%s: %s" % (path, "every entry (%d exports, %d imports)" % (len(EXPORTS), len(IMPORTS)) if not missing
                                else "MISSING " + " ".join(missing)))
    if not found:
        print("no gameoverlayrenderer.so under %s" % steam)
        return 2
    print("OK" if whole else "MISSING")
    return 0 if whole else 1


if __name__ == "__main__":
    sys.exit(main())
