#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Re-pin single files of the default-text pins after a change reaches main (network_default_text_tests.py).

`network_default_text_tests.py --pin <ref>` pins EVERY file from a ref, which would also pin the network modules as main's now that the
network line is in main. This pins only the files named on the command line (repository-relative paths, or paths below port/), from the
checkout, with that test's own functions (resolved text read as latin-1 for port code, the raw text for src/, linklib/ and patches), and
leaves every other line of the pin files alone. A file the pin file does not list yet is added in its sorted place; a file the checkout
does not have is an error.

    python3 port/tools/network_default_text_repin.py port/platform/common/mods.cpp port/platform/common/mods.h
    python3 port/tools/network_default_text_tests.py

Writes only port/network/MAIN-TEXT.sha256 and port/network/MAIN-SOURCES.sha256. A merge receipt names this step and the files it re-pinned.
No game data, device or network is used."""
import sys
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import network_default_text_tests as N  # noqa: E402


def repin(pin_path, name, want):
    """Sets the line of `name` in the pin file to `want`; returns 'changed', 'added' or 'unchanged'."""
    lines = pin_path.read_text().splitlines()
    body = [i for i, line in enumerate(lines) if line.strip() and not line.startswith("#")]
    for i in body:
        sha, listed = lines[i].split(None, 1)
        if listed.strip() == name:
            if sha == want:
                return "unchanged"
            lines[i] = f"{want}  {name}"
            pin_path.write_text("\n".join(lines) + "\n")
            return "changed"
    at = next((i for i in body if lines[i].split(None, 1)[1].strip() > name), (body[-1] + 1) if body else len(lines))
    lines.insert(at, f"{want}  {name}")
    pin_path.write_text("\n".join(lines) + "\n")
    return "added"


def main(argv):
    names = [("port/" + a if not a.startswith(("port/", "src/", "linklib/")) else a) for a in argv]
    if not names:
        print(__doc__)
        return 2
    for name in names:
        path = N.ROOT / name
        if not path.is_file():
            print(f"{name}: not in the checkout")
            return 1
        text = path.read_bytes().decode("latin-1")
        if N.is_source_path(name):
            outcome = repin(N.SOURCE_PINS, name, N.digest(text))
        elif N.in_scope(name) and (name.lower().endswith(N.CODE) or name in N.OTHER or N.OTHER_PATTERN.match(name)):
            outcome = repin(N.PINS, name, N.digest(N.resolved(text) if N.is_code(name) else text))
        else:
            print(f"{name}: not a file the pins cover")
            return 1
        print(f"{name}: {outcome}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
