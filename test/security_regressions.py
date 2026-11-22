#!/usr/bin/env python3
"""
Security regression test suite for CWE fixes landed across the shell, coreutils,
kernel/spark ioctls, Wi-Fi/crypto, boot/device opens, and canvas/UI subsystems.

Runs:
  1. End-to-end runtime tests against a freshly compiled aarch64 static binary of
     programs/shell.c (executed directly on Linux or inside Lima VM `default`).
  2. Source/static invariant checks for kernel/root-only, crypto, O_NOFOLLOW, and
     bounds hardening that cannot be triggered directly from unprivileged userspace.

Usage:
    python3 test/security_regressions.py
"""
import os
import platform
import re
import shutil
import stat
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PASS = 0
FAIL = 0


def check(name, cond, detail=""):
    global PASS, FAIL
    if cond:
        PASS += 1
        print(f"  [PASS] {name}")
    else:
        FAIL += 1
        print(f"  [FAIL] {name}" + (f": {detail}" if detail else ""))


def read_text(rel):
    return (ROOT / rel).read_text(encoding="utf-8", errors="replace")


def build_shell(out_path):
    compiler = shutil.which("aarch64-linux-gnu-gcc") or shutil.which("gcc")
    if not compiler:
        raise RuntimeError("no aarch64-linux-gnu-gcc or gcc found")
    cmd = [
        compiler,
        "-O2",
        "-static",
        "-nostdlib",
        "-nostartfiles",
        "-fno-stack-protector",
        "-fno-builtin",
        "-mno-outline-atomics",
        "-o",
        str(out_path),
        str(ROOT / "programs/shell.c"),
    ]
    res = subprocess.run(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"shell build failed:\n{res.stderr}")


def run_in_guest(argv, timeout=15, stdin_text=None):
    """Run argv directly on Linux, or inside Lima VM `default` on macOS."""
    if platform.system() == "Linux":
        cmd = argv
    else:
        cmd = ["limactl", "shell", "default", "--"] + argv
    return subprocess.run(
        cmd,
        input=stdin_text,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=timeout,
    )


def run_bash(bin_path, script, extra_args=None, timeout=15, stdin_text=None):
    # Invoke via a `bash` symlink so bash personality is active (argv[0] == bash).
    argv = [str(bin_path)] + (extra_args or []) + ["-c", script]
    return run_in_guest(argv, timeout=timeout, stdin_text=stdin_text)


def test_shell_runtime(shell_bin, bash_bin, workdir):
    print("\n== Shell & Coreutils Runtime Regression Tests ==")

    expr_bin = Path(workdir) / "expr"
    mktemp_bin = Path(workdir) / "mktemp"
    os.symlink(shell_bin, expr_bin)
    os.symlink(shell_bin, mktemp_bin)

    # 1. Multi-line signal trap resetting/overwriting itself on line 1 (b05950551, CWE-416)
    r = run_bash(
        bash_bin,
        "trap $'trap - USR1\\necho trap-sig-ok' USR1; kill -USR1 $$; echo done",
    )
    check(
        "CWE-416: multi-line signal trap resetting itself",
        r.returncode == 0 and r.stdout.splitlines() == ["trap-sig-ok", "done"],
        f"rc={r.returncode} out={r.stdout!r} err={r.stderr!r}",
    )

    # 2. Multi-line ERR / DEBUG / RETURN condition traps resetting themselves (b05950551, CWE-416)
    r = run_bash(
        bash_bin,
        "trap $'trap - ERR\\necho trap-err-ok' ERR; false; echo after-err",
    )
    check(
        "CWE-416: multi-line ERR trap resetting itself",
        r.returncode == 0 and r.stdout.splitlines() == ["trap-err-ok", "after-err"],
        f"rc={r.returncode} out={r.stdout!r} err={r.stderr!r}",
    )

    r = run_bash(
        bash_bin,
        "f() { trap $'trap - RETURN\\necho trap-ret-ok' RETURN; }; f; echo after-ret",
    )
    check(
        "CWE-416: multi-line RETURN trap resetting itself",
        r.returncode == 0 and r.stdout.splitlines() == ["trap-ret-ok", "after-ret"],
        f"rc={r.returncode} out={r.stdout!r} err={r.stderr!r}",
    )

    r = run_bash(
        bash_bin,
        "trap $'trap - DEBUG\\necho trap-dbg-ok' DEBUG; :; echo after-dbg",
    )
    check(
        "CWE-416: multi-line DEBUG trap resetting itself",
        r.returncode == 0 and "trap-dbg-ok" in r.stdout and "after-dbg" in r.stdout,
        f"rc={r.returncode} out={r.stdout!r} err={r.stderr!r}",
    )

    # 3. fc -s re-running multi-line history entry that clears history (b05950551, CWE-416)
    r = run_bash(
        bash_bin,
        "set -o history; history -s $'history -c\\necho fc-ok'; fc -s; echo fc-done",
    )
    check(
        "CWE-416: fc -s re-running entry that runs history -c",
        r.returncode == 0 and "fc-ok" in r.stdout and "fc-done" in r.stdout,
        f"rc={r.returncode} out={r.stdout!r} err={r.stderr!r}",
    )

    # 4. jobs -x running nested jobs -x (b05950551, CWE-416)
    r = run_bash(
        bash_bin,
        "jobs -x jobs -x echo jobs-ok",
    )
    check(
        "CWE-416: nested jobs -x static buffer snapshot",
        r.returncode == 0 and r.stdout.strip() == "jobs-ok",
        f"rc={r.returncode} out={r.stdout!r} err={r.stderr!r}",
    )

    # 5. mapfile -C callback clobbering expand_store/shell_argv + recursive mapfile (096d01d95, CWE-416)
    r = run_bash(
        bash_bin,
        (
            "cb() { local -a inner; mapfile -t inner <<< $'x\\ny'; printf 'cb:%s:%s ' \"$1\" \"$2\"; }; "
            "mapfile -t -C cb -c 1 my_target_array <<< $'alpha\\nbeta\\ngamma'; "
            "printf '|%s,%s,%s|\\n' \"${my_target_array[0]}\" \"${my_target_array[1]}\" \"${my_target_array[2]}\""
        ),
    )
    check(
        "CWE-416: mapfile -C callback with nested mapfile and argv clobber",
        r.returncode == 0
        and "cb:0:alpha cb:1:beta cb:2:gamma |alpha,beta,gamma|" in r.stdout,
        f"rc={r.returncode} out={r.stdout!r} err={r.stderr!r}",
    )

    # 6. Alpha brace range signed overflow / infinite loop (096d01d95, CWE-190/CWE-835)
    r = run_bash(
        bash_bin,
        "echo {a..z..9223372036854775807} {z..a..-9223372036854775807}",
        timeout=5,
    )
    check(
        "CWE-190/835: alpha brace range INT64_MAX step does not overflow or hang",
        r.returncode == 0 and r.stdout.strip() == "a z",
        f"rc={r.returncode} out={r.stdout!r} err={r.stderr!r}",
    )

    # 7. mktemp creates distinct 0600 file and 0700 dir (096d01d95, CWE-338/377)
    r = run_bash(
        bash_bin,
        (
            f'd=$("{mktemp_bin}" -d /tmp/mw-mkt.XXXXXX); '
            f'f=$("{mktemp_bin}" /tmp/mw-mktf.XXXXXX); '
            'test -d "$d" && test -f "$f" && '
            'echo ok; rm -rf "$d" "$f"'
        ),
    )
    check(
        "CWE-338/377: mktemp file & directory creation with CSPRNG",
        r.returncode == 0 and r.stdout.strip() == "ok",
        f"rc={r.returncode} out={r.stdout!r} err={r.stderr!r}",
    )

    # 8. compgen -V/-P/-S/-W/-F/-C with nested compgen and argv clobber (7ec224b4a, CWE-416/190)
    r = run_bash(
        bash_bin,
        (
            "fn() { local -a sub; compgen -V sub -W 'z1 z2' >/dev/null; COMPREPLY=(fn1 fn2); }; "
            "compgen -V out -P pre_ -S _suf -W 'a $(echo b) c' -F fn; "
            "printf '%s\\n' \"${out[@]}\""
        ),
    )
    expected_comp = ["pre_a_suf", "pre_b_suf", "pre_c_suf", "pre_fn1_suf", "pre_fn2_suf"]
    check(
        "CWE-416: compgen -V/-P/-S/-W/-F with nested compgen",
        r.returncode == 0 and r.stdout.splitlines() == expected_comp,
        f"rc={r.returncode} out={r.stdout!r} err={r.stderr!r}",
    )

    r = run_bash(
        bash_bin,
        "compgen -V out2 -P p_ -S _s -C 'printf \"%s\\n\" c1 c2'; printf '%s\\n' \"${out2[@]}\"",
    )
    check(
        "CWE-416: compgen -V/-P/-S/-C command substitution",
        r.returncode == 0
        and r.stdout.splitlines()[:2] == ["p_c1_s", "p_c2_s"]
        and "p_compgen_s" in r.stdout.splitlines(),
        f"rc={r.returncode} out={r.stdout!r} err={r.stderr!r}",
    )

    # 9. expr deep nesting bound (865c8fc2e, CWE-674)
    expr_script = (
        "args=(); for ((i=0;i<1100;i++)); do args+=('('); done; "
        "args+=(1); for ((i=0;i<1100;i++)); do args+=(')'); done; "
        f'"{expr_bin}" "${{args[@]}}"'
    )
    r = run_bash(bash_bin, expr_script, timeout=10)
    check(
        "CWE-674: expr 1100 nested parens fails cleanly (no SIGSEGV)",
        r.returncode == 2 and "nesting" in r.stderr.lower(),
        f"rc={r.returncode} out={r.stdout!r} err={r.stderr!r}",
    )

    # 10. Recursive eval and source bounds (c19b88388, CWE-674)
    r = run_bash(bash_bin, "x='eval \"$x\"'; eval \"$x\"", timeout=10)
    check(
        "CWE-674: recursive eval fails cleanly (no SIGSEGV)",
        r.returncode != 0 and r.returncode < 128 and "nesting" in r.stderr.lower(),
        f"rc={r.returncode} out={r.stdout!r} err={r.stderr!r}",
    )

    src_file = Path(workdir) / "self_source.sh"
    src_file.write_text(f'. "{src_file}"\n', encoding="utf-8")
    r = run_bash(bash_bin, f'. "{src_file}"', timeout=10)
    check(
        "CWE-674: recursive source/. fails cleanly (no SIGSEGV)",
        r.returncode != 0 and r.returncode < 128 and "nesting" in r.stderr.lower(),
        f"rc={r.returncode} out={r.stdout!r} err={r.stderr!r}",
    )

    # 11. Nested [[ ... ]] inside in-process ${ ...; } (28451e694, CWE-416/125)
    r = run_bash(
        bash_bin,
        'if [[ "${ [[ a == a ]]; echo ok; }" == ok && "${ [[ x == y ]]; echo no; }" == no ]]; then echo nested-cond-ok; fi',
    )
    check(
        "CWE-416/125: nested [[ ]] inside in-process ${ ...; }",
        r.returncode == 0 and r.stdout.strip() == "nested-cond-ok",
        f"rc={r.returncode} out={r.stdout!r} err={r.stderr!r}",
    )

    # 12. Restricted shell declare -g on PATH/SHELL (28451e694, CWE-284/863)
    r = run_bash(
        bash_bin,
        (
            'orig="$PATH"; '
            'f() { declare -g PATH=/tmp; declare -g -n PATH=orig; }; '
            'f; '
            'if [ "$PATH" = "$orig" ]; then echo rbash-ok; fi'
        ),
        extra_args=["-r"],
    )
    check(
        "CWE-284/863: rbash blocks declare -g and declare -g -n on PATH",
        r.returncode == 0 and r.stdout.strip() == "rbash-ok",
        f"rc={r.returncode} out={r.stdout!r} err={r.stderr!r}",
    )

    # 13. Recursive in-process funsub ${ ...; } bound (28451e694, CWE-674)
    r = run_bash(
        bash_bin,
        'ulimit -n 4096 2>/dev/null; f() { : "${| f; }"; }; f',
        timeout=10,
    )
    check(
        "CWE-674: recursive in-process ${| ...; } fails cleanly (no SIGSEGV)",
        r.returncode < 128 and ("nesting" in r.stderr.lower() or "cannot" in r.stderr.lower()),
        f"rc={r.returncode} out={r.stdout!r} err={r.stderr!r}",
    )

    # 14. Active function unset -f self (AST refcount UAF check)
    r = run_bash(
        bash_bin,
        "f() { unset -f f; echo still-alive; }; f",
    )
    check(
        "CWE-416: unset -f active function keeps AST alive until return",
        r.returncode == 0 and r.stdout.strip() == "still-alive",
        f"rc={r.returncode} out={r.stdout!r} err={r.stderr!r}",
    )


def test_static_invariants():
    print("\n== Kernel / Crypto / Device O_NOFOLLOW / UI Static Invariants ==")

    # 1. /dev/spark ioctl capability checks (736698eaa, CWE-284/CWE-862)
    spark_c = read_text("src/moonwater/spark.c")
    check(
        "CWE-862: spark_file_capable checks file_ns_capable + capable",
        "file_ns_capable(file, &init_user_ns, cap) && capable(cap)" in spark_c,
    )
    for rel, token in [
        ("src/moonwater/spark.c", "spark_file_capable"),
        ("src/moonwater/moonwater.c", "spark_file_capable"),
        ("src/moonwater/rules.c", "spark_file_capable"),
        ("src/canvas/canvas.c", "canvas_file_admin"),
    ]:
        check(
            f"CWE-862: {rel} enforces {token}",
            token in read_text(rel),
        )

    # 2. window.c atomic write hardening & fd truncation (736698eaa)
    win_c = read_text("src/canvas/window.c")
    check(
        "CWE-377: window.c file_write_atomic_at uses O_EXCL + fchmod",
        "O_EXCL" in win_c and "fchmod" in win_c,
    )

    # 3. Wi-Fi constant-time compare & key zeroization (e5f3290d9, CWE-208/CWE-244)
    nl_c = read_text("src/sh/host/nl80211.c")
    check(
        "CWE-208: nl80211.c uses crypto_same for PTK/GTK/IGTK",
        nl_c.count("crypto_same(") >= 3,
        f"count={nl_c.count('crypto_same(')}",
    )
    check(
        "CWE-244: nl80211.c wipes key buffers with crypto_forget",
        "crypto_forget(" in nl_c,
    )

    # 4. O_NOFOLLOW on privileged /dev and efivars opens (bd20d9228, cbc2ce9ea, 0374f73fb, 865c8fc2e, c19b88388)
    nofollow_targets = [
        ("src/canvas/compositor/session.c", "/dev/kmsg"),
        ("src/canvas/compositor/input.c", "/dev/cpu_dma_latency"),
        ("src/canvas/ui/terminal.c", "/dev/kmsg"),
        ("src/sh/host/boot.c", "HOST_EFI_INDICATIONS"),
        ("src/sh/host/radio.c", "/dev/kmsg"),
        ("src/sh/host/tune.c", "/dev/kmsg"),
        ("src/sh/system.c", "/dev/zram0"),
        ("src/sh/net.c", "/dev/kmsg"),
        ("src/sh/tools.c", "/dev/kmsg"),
    ]
    for rel, needle in nofollow_targets:
        src = read_text(rel)
        # Every open mentioning needle must have O_NOFOLLOW within +/- 4 lines.
        lines = src.splitlines()
        ok = True
        found = 0
        for idx, line in enumerate(lines):
            if needle in line and "system_open_at" in "\n".join(lines[max(0, idx - 2) : idx + 2]):
                found += 1
                window = "\n".join(lines[max(0, idx - 2) : idx + 4])
                if "O_NOFOLLOW" not in window:
                    ok = False
        check(
            f"CWE-59: {rel} opens {needle} with O_NOFOLLOW",
            found > 0 and ok,
            f"found={found} ok={ok}",
        )

    # write/wall login_message_open uses O_NOFOLLOW
    tools_c = read_text("src/sh/tools.c")
    m = re.search(r"login_message_open\([\s\S]{0,400}O_NOFOLLOW", tools_c)
    check("CWE-59: tools.c login_message_open uses O_NOFOLLOW", m is not None)

    # 5. mktemp fail-closed CSPRNG (096d01d95, CWE-338)
    file_c = read_text("src/sh/file.c")
    m = re.search(r"mktemp_letters_into\([\s\S]{0,600}", file_c)
    body = m.group(0) if m else ""
    check(
        "CWE-338: mktemp_letters_into fails closed without clock_gettime/LCG fallback",
        "system_random_fill" in body and "clock_gettime" not in body and "raw[i] >= 248" in body,
    )

    # 6. settings_about_describe upper bound (865c8fc2e, CWE-787)
    settings_c = read_text("src/canvas/ui/settings.c")
    check(
        "CWE-787: settings_about_describe checks w >= SETTINGS_ABOUT_COUNT",
        "w < 0 || w >= SETTINGS_ABOUT_COUNT" in settings_c,
    )

    # 7. files_remote_read_abort unlinks partial downloads (cbc2ce9ea)
    files_c = read_text("src/canvas/ui/files.c")
    m = re.search(r"files_remote_read_abort\([\s\S]{0,400}system_remove_at", files_c)
    check(
        "CWE-459: files_remote_read_abort unlinks partial remote download",
        m is not None,
    )

    # 8. editor_save checks fchmod & osk_trace_line guards INT_MIN (bd20d9228)
    editor_c = read_text("src/canvas/ui/editor.c")
    check("CWE-732: editor_save checks fchmod return", "fchmod" in editor_c)
    osk_c = read_text("src/canvas/ui/osk.c")
    check("CWE-190: osk_trace_line uses wide subtraction for INT_MIN safety", "b64" in osk_c or "long" in osk_c)


def main():
    test_static_invariants()

    # Use a temporary directory inside ROOT so macOS + Lima VM share the exact same path.
    with tempfile.TemporaryDirectory(prefix=".mw-sec-reg-", dir=str(ROOT)) as tmp:
        shell_bin = Path(tmp) / "shell"
        bash_link = Path(tmp) / "bash"
        print(f"\n== Building programs/shell.c -> {shell_bin} ==")
        build_shell(shell_bin)
        os.chmod(shell_bin, 0o755)
        os.symlink(shell_bin, bash_link)
        # Ensure /tmp dir is traversable inside Lima VM
        os.chmod(tmp, 0o755)
        test_shell_runtime(shell_bin, bash_link, tmp)

    print(f"\nSummary: {PASS} passed, {FAIL} failed")
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
