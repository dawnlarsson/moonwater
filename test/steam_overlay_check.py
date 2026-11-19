#!/usr/bin/env python3
"""steam_overlay_check.py [STEAM_DIR]: whether Steam's overlay renderer still has every entry steam-overlay-gl.so relies on.
steam_overlay_check.py --blob [--write]: whether the libraries src/bowl.c embeds are test/steam_overlay_gl_shim.c as built.

Reads the dynamic symbols of each gameoverlayrenderer.so under STEAM_DIR (default ~/.local/share/Steam; steamrt32 and
steamrt64) and checks the hooks the renderer must export (the ones steam-overlay-gl.so calls into) and the GL calls it must
import by name (the ones steam-overlay-gl.so stands in front of). Prints one line a library and a last line, OK or MISSING;
exit status 0 when every library has everything, 1 when one lacks something (a Steam update changed the renderer: the shim
then does nothing for what is missing), 2 when no renderer is found. Run it in the steam bowl after Steam updates:
    bowl /bowls/arch /usr/bin/python3 -I test/steam_overlay_check.py /root/.local/share/Steam

--blob builds test/steam_overlay_gl_shim.c for i386 and x86_64 with the clang on PATH (or $CLANG) and the flags in its head, and
says whether the bytes are the arrays in src/bowl.c (bowl_steam_overlay_gl_32, _64), and whether each array is a shared object
of its machine that exports what the library stands in front of (defined global functions in .dynsym) and not
vkGetInstanceProcAddr (a Vulkan layer's chain broke when it did). The arrays are made by the clang their comment names
("Built with clang N"): another major version makes other bytes, and the comparison is not run there (status 2); the .dynsym
checks are. --write puts the built bytes into src/bowl.c, with their sizes and hashes. Status 0 the same, 1 not, 2 not run.
"""
import hashlib
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

# What the renderer exports and steam-overlay-gl.so calls: its draw at a swap and its Xlib hooks (the hot-key and the overlay's input).
EXPORTS = ["glXSwapBuffers", "XOpenDisplay", "XCreateSimpleWindow", "XNextEvent", "XPending", "IsOverlayEnabled"]
# What the renderer imports by name and steam-overlay-gl.so interposes: its draws, the end of each of its passes, its context query.
IMPORTS = ["glBegin", "glDrawArrays", "glDrawElements", "glPopAttrib", "glXGetCurrentContext"]


def dynamic_symbols(path, binding=None):
    """(defined, undefined) function names of an ELF shared object's .dynsym (of that binding only, when given: 1 global)."""
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
            if (info & 0xF) != 2 or (binding is not None and info >> 4 != binding):  # STT_FUNC, STB_*
                continue
            (undefined if shndx == 0 else defined).add(text)
    return defined, undefined


# What steam-overlay-gl.so exports: the draws and the end of a pass it stands in front of, the context query, the dlsym SDL's lookups
# go through, EGL's swap and the game's own XOpenDisplay. And what it must not: a layer's vkGetInstanceProcAddr is its own.
SHIM_EXPORTS = ["glBegin", "glDrawArrays", "glDrawElements", "glPopAttrib", "glXGetCurrentContext", "dlsym", "eglSwapBuffers",
                "XOpenDisplay"]
SHIM_NOT = ["vkGetInstanceProcAddr"]
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(ROOT, "test", "steam_overlay_gl_shim.c")
BOWL = os.path.join(ROOT, "src", "bowl.c")
ARRAYS = {"i386": "bowl_steam_overlay_gl_32", "x86_64": "bowl_steam_overlay_gl_64"}


def exported(data):
    """The global functions an ELF shared object defines in .dynsym, with its class (1, 2) and machine."""
    path = tempfile.mktemp()
    try:
        open(path, "wb").write(data)
        defined, _ = dynamic_symbols(path, binding=1)
    finally:
        os.unlink(path)
    return defined


def embedded(text, name):
    found = re.search(r"static const p8 %s\[\] = \{(.*?)\};" % name, text, re.S)
    return bytes(int(word, 16) for word in re.findall(r"0x([0-9a-fA-F]{2})", found.group(1))) if found else None


def build(clang, target):
    flags = re.search(r"clang -target \{i386,x86_64\}-linux-gnu (.*?)\n \*/", open(SOURCE).read(), re.S).group(1)
    flags = flags.replace("\\\n *", " ").split()
    out = tempfile.mktemp(suffix=".so")
    try:
        subprocess.run([clang, "-target", "%s-linux-gnu" % target] + flags + [SOURCE, "-o", out], check=True)
        return open(out, "rb").read()
    finally:
        if os.path.exists(out):
            os.unlink(out)


def array_text(name, data):
    lines = ["static const p8 %s[] = {" % name]
    for at in range(0, len(data), 12):
        lines.append("    " + " ".join("0x%02x," % byte for byte in data[at:at + 12]))
    return "\n".join(lines) + "\n};"


def blob(write):
    text = open(BOWL).read()
    whole = True
    for target, name in ARRAYS.items():
        data = embedded(text, name)
        if data is None:
            print("%s: no array %s" % (BOWL, name))
            return 1
        machine = 3 if target == "i386" else 62
        right_elf = data[:4] == b"\x7fELF" and data[4] == (1 if target == "i386" else 2) and data[16] == 3 and data[18] == machine
        defined = exported(data) if right_elf else set()
        missing = [want for want in SHIM_EXPORTS if want not in defined]
        extra = [want for want in SHIM_NOT if want in defined]
        whole &= right_elf and not missing and not extra
        print("%s (%s, %d bytes): %s" % (name, target, len(data), "not a shared object of its machine" if not right_elf else
              "exports every entry (%d) and not %s" % (len(SHIM_EXPORTS), " ".join(SHIM_NOT)) if not missing and not extra else
              "MISSING " + " ".join(missing) + (" EXPORTS " + " ".join(extra) if extra else "")))
    clang = os.environ.get("CLANG") or shutil.which("clang")
    wanted = re.search(r"Built with clang (\d+)", text)
    if not clang or not wanted:
        print("no clang here, or no \"Built with clang N\" in %s: the arrays are not compared with a build" % BOWL)
        return 2 if whole else 1
    version = subprocess.run([clang, "--version"], capture_output=True, text=True).stdout
    major = re.search(r"clang version (\d+)", version)
    if not write and (not major or major.group(1) != wanted.group(1)):
        print("%s is not clang %s: the arrays are not compared with a build" % (clang, wanted.group(1)))
        return 2 if whole else 1
    built = {target: build(clang, target) for target in ARRAYS}
    if write:
        for target, name in ARRAYS.items():
            text = re.sub(r"static const p8 %s\[\] = \{.*?\};" % name, lambda _: array_text(name, built[target]), text, count=1, flags=re.S)
        text = re.sub(r"/\* x86_64: \d+ bytes, sha256 [0-9a-f]+; i386: \d+ bytes, sha256 [0-9a-f]+ \*/",
                      "/* x86_64: %d bytes, sha256 %s; i386: %d bytes, sha256 %s */" % (
                          len(built["x86_64"]), hashlib.sha256(built["x86_64"]).hexdigest()[:16],
                          len(built["i386"]), hashlib.sha256(built["i386"]).hexdigest()[:16]), text, count=1)
        text = re.sub(r"Built with clang \d+", "Built with clang %s" % major.group(1), text, count=1)
        open(BOWL, "w").write(text)
        print("%s: the arrays written from the build (clang %s)" % (BOWL, major.group(1)))
        return blob(False)
    for target, name in ARRAYS.items():
        same = built[target] == embedded(text, name)
        whole &= same
        print("%s: %s" % (name, "the build's bytes" if same else "NOT the build's bytes (%d built)" % len(built[target])))
    print("OK" if whole else "DIFFERS")
    return 0 if whole else 1


def main():
    if sys.argv[1:2] == ["--blob"]:
        return blob(sys.argv[2:3] == ["--write"])
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
