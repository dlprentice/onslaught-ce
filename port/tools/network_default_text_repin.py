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

At a merge, `--changed-since BASE` (BASE = the integration head before the merge) re-pins exactly the pinned or newly default-built
files that both fail network_default_text_tests and differ from BASE, removes the pin of a file the merge deleted, resolves a path
the union merge left pinned twice by re-pinning it from the merged file (when the merge changed that file), puts each pin file
back in path order (the union merge of .gitattributes appends the other side's lines), and touches nothing else: a failing file the merge did not change is printed and the exit code is 1, for the integrator to look at.

    python3 port/tools/network_default_text_repin.py --changed-since BASE

Git no longer stops on a conflicting pin (the pin files union-merge), so this step is not optional: tools/merge_slice.py runs
it, then network_default_text_tests (which refuses a doubled or out-of-order pin), before it commits a merge.

Writes only port/network/MAIN-TEXT.sha256 and port/network/MAIN-SOURCES.sha256. A merge receipt names this step and the files it re-pinned.
No game data, device or network is used."""
import subprocess
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


def unpin(pin_path, name):
    """Removes every line of `name` from the pin file; returns 'removed' or 'absent'."""
    lines = pin_path.read_text().splitlines()
    kept = [l for l in lines if not (l.strip() and not l.startswith("#") and l.split(None, 1)[1].strip() == name)]
    if len(kept) == len(lines):
        return "absent"
    pin_path.write_text("\n".join(kept) + "\n")
    return "removed"


def failing_names():
    """The paths network_default_text_tests would report, from its own checks on the checkout."""
    found = N.problems(N.checkout_texts(), N.read_pins(N.PINS))
    found += N.source_problems(N.checkout_sources(), N.read_pins(N.SOURCE_PINS), N.listed_network_patches())
    return sorted({line.split(":", 1)[0] for line in found})


def changed_since(base):
    """Paths that differ between BASE and the checkout (committed or not), and the untracked ones git does not ignore."""
    run = lambda *a: subprocess.run(["git", "-C", str(N.ROOT), *a], capture_output=True, text=True, check=True).stdout.split("\n")
    names = run("diff", "--name-only", base) + run("ls-files", "--others", "--exclude-standard")
    return {n for n in names if n}


def sort_pins(pin_path):
    """Puts the pin lines back in path order (a union merge appends the other side's lines after ours); returns 'sorted' or ''."""
    lines = pin_path.read_text().splitlines()
    head = [l for l in lines if not l.strip() or l.startswith("#")]
    body = [l for l in lines if l.strip() and not l.startswith("#")]
    ordered = sorted(body, key=lambda l: l.split(None, 1)[1].strip())
    if ordered == body:
        return ""
    pin_path.write_text("\n".join(head + ordered) + "\n")
    return "sorted"


def merge_repin(base):
    changed = changed_since(base)
    status = 0
    resolved = False
    for pins in (N.PINS, N.SOURCE_PINS):
        # a union merge keeps both sides' pins of a file; the merged file decides (git stops on the file itself if it conflicts)
        for name in N.pin_duplicates(pins.read_text()):
            if name in changed and (N.ROOT / name).is_file():
                unpin(pins, name)
                print(f"{name}: pinned twice after the merge; both lines dropped and re-pinned from the merged file")
                resolved = True
                status = max(status, main([name]))
            else:
                print(f"{name}: pinned twice and not changed since {base}; not resolved (look at it)")
                status = 1
        if sort_pins(pins):
            print(f"{pins.relative_to(N.ROOT)}: lines put back in path order")
    failing = failing_names()
    for name in failing:
        if name not in changed:
            print(f"{name}: fails the pins but did not change since {base}; not re-pinned (look at it)")
            status = 1
            continue
        if not (N.ROOT / name).is_file():
            pins = N.SOURCE_PINS if N.is_source_path(name) else N.PINS
            print(f"{name}: {unpin(pins, name)} (deleted since {base})")
            continue
        rc = main([name])
        if rc:
            status = rc
    if not failing and status == 0 and not resolved:
        print("nothing to re-pin")
    return status


def main(argv):
    if argv[:1] == ["--changed-since"]:
        if len(argv) != 2:
            print(__doc__)
            return 2
        return merge_repin(argv[1])
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
