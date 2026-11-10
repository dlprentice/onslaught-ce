#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The network game's join code, secrets and launch handoff (port/platform/common/ce_netcode.cpp), with mutants.

    python3 port/tools/ce_netcode_tests.py [--no-mutants]

Builds port/tools/tests/ce_netcode_driver.cpp with the host compiler against the real ce_netcode.cpp and the launcher's
sha256.c, runs it, checks its printed known answers against Python's hashlib, then rebuilds it from mutated copies of
ce_netcode.cpp: each mutant must fail. No game, window, network or player data.
"""
import argparse
import hashlib
import os
from pathlib import Path
import resource
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CODE = ROOT / "port/platform/common/ce_netcode.cpp"
DRIVER = ROOT / "port/tools/tests/ce_netcode_driver.cpp"
SHA = ROOT / "port/updater/sha256.c"

# (name, text in ce_netcode.cpp, replacement[, text, replacement]): each is a believable bug the tests must catch.
MUTANTS = [
    ("weight-one", "for (int k = 0; k <= i; k++)", "for (int k = 0; k < i; k++)"),
    ("no-check", "else if (Check(d) != d[kData])", "else if (false)"),
    ("no-pad-check", "else if (d[kData - 1] & 3)", "else if (false)"),
    ("no-I-for-1", "if (c == 'I' || c == 'L')", "if (c == 'L')"),
    ("no-O-for-0", "if (c == 'O')\n\t\tc = '0';", "if (false)\n\t\tc = '0';"),
    ("case-sensitive", "if (c >= 'a' && c <= 'z')\n\t\tc = char(c - 'a' + 'A');\n\tif (c == 'I'", "if (false)\n\t\tc = char(c - 'a' + 'A');\n\tif (c == 'I'"),
    ("dash-not-skipped", "if (*p == '-' || *p == ' ' || *p == '\\t' || *p == '\\r' || *p == '\\n')", "if (*p == ' ' || *p == '\\t' || *p == '\\r' || *p == '\\n')"),
    ("no-address-check", "if (!c.ipv4 || !c.port)\n\t\t\tresult = CE_CODE_BAD_ADDRESS;", "if (false)\n\t\t\tresult = CE_CODE_BAD_ADDRESS;"),
    ("join-domain", 'static const char domain[] = "onslaught-ce join v1";', 'static const char domain[] = "onslaught-ce join v2";'),
    ("start-ignores-nonce", "memcpy(m + sizeof domain - 1 + 32, nonce, 16);", "memset(m + sizeof domain - 1 + 32, 0, 16);"),
    ("digest-no-version", "memcpy(m + n, version ? version : \"\", b); n += b;", ""),
    ("key-61-bits", "*key = k & CE_CODE_KEY_MASK;", "*key = k & 0x1fffffffffffffffULL;"),
    ("age-unchecked", "else if (created > now + 5 || now - created > CE_HANDOFF_MAX_AGE)", "else if (created > now + 5)"),
    ("future-accepted", "else if (created > now + 5 || now - created > CE_HANDOFF_MAX_AGE)", "else if (now - created > CE_HANDOFF_MAX_AGE)"),
    ("mtime-unchecked", "else if ((int64_t)st.stx_mtime.tv_sec > now + 5 || now - (int64_t)st.stx_mtime.tv_sec > CE_HANDOFF_MAX_AGE)", "else if (false)"),
    ("mode-unchecked", "else if ((st.stx_mode & 0777) != 0600)", "else if (false)"),
    ("not-deleted", "\t\tunlink(path);\n\t\tstruct statx st;", "\t\tstruct statx st;"),
    ("follows-links", "O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK", "O_RDONLY | O_CLOEXEC | O_NONBLOCK"),
    ("written-0644", "O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);\n\tif (fd >= 0)\n\t{\n\t\tok = fchmod(fd, 0600)",
     "O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);\n\tif (fd >= 0)\n\t{\n\t\tok = fchmod(fd, 0644)"),
    ("duplicates-accepted", "else if (seen[k])\n\t\t\twhy = \"the handoff file repeats a line\";", ""),
    ("unknown-accepted", "if (k < 0)\n\t\t\twhy = \"the handoff file has an unknown line\";\n\t\telse if", "if (k < 0)\n\t\t\t{ p = nl + 1; continue; }\n\t\telse if"),
    ("any-level", "else if (*e || level > 0xffffffffUL || !(level == 852 || CE_CompatibilityLockstepLevel((uint32_t)level)))", "else if (*e)"),
    ("only-852", "else if (*e || level > 0xffffffffUL || !(level == 852 || CE_CompatibilityLockstepLevel((uint32_t)level)))", "else if (*e || level != 852)"),
    ("level-wraps", "else if (*e || level > 0xffffffffUL || !(level == 852 || CE_CompatibilityLockstepLevel((uint32_t)level)))",
     "else if (*e || !(level == 852 || CE_CompatibilityLockstepLevel((uint32_t)level)))"),
    ("not-the-coop-maps", "!(level == 852 || CE_CompatibilityLockstepLevel((uint32_t)level))", "!(level == 852 || level / 100 == 8 || level == 100)"),
    ("loose-endpoint", "return a < 256 && b < 256 && c < 256 && d < 256 && p > 0 && p < 65536 && !strcmp(again, s);", "return true;"),
    ("control-chars", "if ((unsigned char)*s < 0x20 || *s == 0x7f)\n\t\t\treturn false;", ""),
    # The size bounds are not mutated: no file over 4096 bytes can parse (each value is under 1024 bytes and no line may
    # repeat), so removing them changes nothing observable; the driver still checks that a large file is refused.
]


def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def build(out, code, strict=True):
    cxx = os.environ.get("CXX", "g++")
    sha = out / "sha256.o"
    if not sha.exists():
        subprocess.run([os.environ.get("CC", "gcc"), "-c", "-O1", "-o", str(sha), str(SHA)], check=True)
    exe = out / ("driver-" + hashlib.sha256(code.encode()).hexdigest()[:12])
    src = out / (exe.name + ".cpp")
    src.write_text(code)
    r = subprocess.run([cxx, "-std=c++17", "-Wall", "-Wextra", *(["-Werror"] if strict else []), "-O1", "-I", str(CODE.parent), str(DRIVER),
                        str(src), str(sha), "-o", str(exe)], capture_output=True, text=True, timeout=120)
    if r.returncode:
        raise RuntimeError("build failed:\n" + r.stderr)
    return exe


def run(exe, scratch):
    scratch.mkdir(parents=True, exist_ok=True)
    return subprocess.run([str(exe), str(scratch)], capture_output=True, text=True, timeout=120, preexec_fn=no_core)


def known_answers(stdout):
    got = {}
    for l in stdout.splitlines():
        p = l.split()
        if p[:1] == ["kat"]:
            got[p[1]] = p[2:]
    key = int(got["lobby"][0], 16)
    lobby = hashlib.sha256(b"onslaught-ce join v1" + key.to_bytes(8, "big")).digest()
    start = hashlib.sha256(b"onslaught-ce start v1" + lobby + bytes(range(16))).hexdigest()
    digest = hashlib.sha256(b"onslaught-ce lobby v1\0abc123\0" + b"0.1.8").hexdigest()
    problems = []
    if got["lobby"][1] != lobby.hex():
        problems.append("lobby secret")
    if got["start"][0] != start:
        problems.append("start secret")
    if got["digest"][0] != digest:
        problems.append("lobby digest")
    return problems


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--no-mutants", action="store_true")
    a = ap.parse_args()
    parent = ROOT / "local-data/port/ce-netcode-tests"
    parent.mkdir(parents=True, exist_ok=True)
    source = CODE.read_text()
    with tempfile.TemporaryDirectory(prefix="run-", dir=parent) as tmp:
        out = Path(tmp)
        exe = build(out, source)
        r = run(exe, out / "scratch")
        print(r.stdout.strip())
        if r.returncode or "PASS" not in r.stdout:
            print(r.stderr, file=sys.stderr)
            print("FAIL: the real code")
            return 1
        problems = known_answers(r.stdout)
        if problems:
            print("FAIL: known answers differ from hashlib: " + ", ".join(problems))
            return 1
        print("known answers match hashlib (join, start, lobby digest)")
        if a.no_mutants:
            return 0
        survived = []
        for name, *edits in MUTANTS:
            text = source
            for old, new in zip(edits[0::2], edits[1::2]):
                if text.count(old) != 1:
                    print(f"FAIL: mutant {name}: its text occurs {text.count(old)} times")
                    return 1
                text = text.replace(old, new)
            mutant = build(out, text, strict=False)
            m = run(mutant, out / ("scratch-" + name))
            killed = m.returncode != 0 or "PASS" not in m.stdout or known_answers(m.stdout)
            reason = (m.stderr.strip().splitlines() or ["known answer differs"])[0].split(": ", 1)[-1]
            print(f"mutant {name}: {'killed' if killed else 'SURVIVED'} ({reason[:110]})")
            if not killed:
                survived.append(name)
        if survived:
            print("FAIL: mutants survived: " + ", ".join(survived))
            return 1
        print(f"PASS: {len(MUTANTS)} mutants killed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
