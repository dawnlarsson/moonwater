#!/usr/bin/env python3
"""Every package a bowl profile names (src/bowl.c), held to its distribution's repositories as they are today.

    python3 -I test/bowl_packages.py [--arch x86_64]

A name a distribution has dropped fails the profile's whole install at once (Alpine 3.24 and Arch dropped Mesa's VDPAU driver:
`bowl profile chrome install` stopped at "mesa-vdpau-gallium (no such package)"), and nothing in the gate can see it, since the
repositories move under a tree that does not. This asks them: Alpine's and Arch's package indexes (a name, or for Arch a name another
package provides, which pacman takes), Fedora's metadata service and Debian's archive (madison, the stable suite). Network only; not
part of the gate. Exit 1 when a list names a package its distribution does not have.
"""
import io
import json
import re
import sys
import tarfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ARCH = sys.argv[sys.argv.index("--arch") + 1] if "--arch" in sys.argv else "x86_64"
ALPINE = "https://dl-cdn.alpinelinux.org/alpine/v3.24/{repo}/" + ARCH + "/APKINDEX.tar.gz"
ARCHLINUX = "https://geo.mirror.pkgbuild.com/{repo}/os/x86_64/{repo}.db"
FEDORA = "https://mdapi.fedoraproject.org/f44/pkg/{name}"
DEBIAN = "https://api.ftp-master.debian.org/madison?package={name}&text=on"
VERBS = {"add", "del", "install", "remove", "-y", "-S", "-R", "--noconfirm", "--needed"}


def fetch(url, timeout=120):
    with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": "moonwater-bowl-packages"}), timeout=timeout) as r:
        return r.read()


def lists(source, distro):
    """The profile lists of one distribution: name -> packages, from the arrays and the #define'd lists."""
    found = {}
    for m in re.finditer(r"static string_address (bowl_\w*%s\w*)\[\] = \{(.*?)\};" % distro, source, re.S):
        found[m.group(1)] = m.group(2)
    for m in re.finditer(r"#define (BOWL_\w*%s\w*PACKAGES)\s*\\\n(.*?)\n\n" % distro.upper(), source, re.S):
        found[m.group(1)] = m.group(2)
    out = {}
    for name, body in found.items():
        if name.endswith(("_expose", "_install", "_remove")):
            continue
        words = [w for w in re.findall(r'"([A-Za-z0-9][A-Za-z0-9.+_@-]*)"', body) if w not in VERBS]
        if words:
            out[name] = words
    return out


def alpine_names():
    names = set()
    for repo in ("main", "community"):
        with tarfile.open(fileobj=io.BytesIO(fetch(ALPINE.format(repo=repo)))) as t:
            text = t.extractfile("APKINDEX").read().decode(errors="replace")
        names.update(line[2:] for line in text.splitlines() if line.startswith("P:"))
    return names


def arch_names():
    names, provided = set(), set()
    for repo in ("core", "extra", "multilib"):
        with tarfile.open(fileobj=io.BytesIO(fetch(ARCHLINUX.format(repo=repo)))) as t:
            for member in t.getmembers():
                if not member.name.endswith("/desc"):
                    continue
                fields, key = {}, None
                for line in t.extractfile(member).read().decode(errors="replace").splitlines():
                    if line.startswith("%") and line.endswith("%"):
                        key = line
                        fields[key] = []
                    elif line and key:
                        fields[key].append(line)
                names.update(fields.get("%NAME%", []))
                provided.update(re.split(r"[<>=]", p)[0] for p in fields.get("%PROVIDES%", []))
    return names, provided


def fedora_has(name):
    try:
        fetch(FEDORA.format(name=name), 60)
        return True
    except Exception:
        return False


def debian_has(name):
    try:
        text = fetch(DEBIAN.format(name=name), 60).decode(errors="replace")
    except Exception:
        return None
    return any(" stable " in line or line.split("|")[2].strip() == "stable" for line in text.splitlines() if line.count("|") >= 2)


def main():
    source = (ROOT / "src/bowl.c").read_text()
    missing = 0

    def report(distro, name, packages, has):
        nonlocal missing
        gone = [p for p in packages if not has(p)]
        missing += len(gone)
        print("%-8s %-32s %3d packages%s" % (distro, name, len(packages), ", missing: " + " ".join(gone) if gone else ""))

    alpine = alpine_names()
    for name, packages in lists(source, "alpine").items():
        report("alpine", name, packages, lambda p: p in alpine)
    arch, provided = arch_names()
    for name, packages in lists(source, "arch").items():
        report("arch", name, packages, lambda p: p in arch or p in provided)
    for name, packages in lists(source, "fedora").items():
        report("fedora", name, packages, fedora_has)
    for name, packages in lists(source, "debian").items():
        report("debian", name, packages, lambda p: debian_has(p) is not False)
    print("%d named packages missing" % missing)
    return 1 if missing else 0


sys.exit(main())
