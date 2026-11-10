#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The patched career-owner entry gates, over authored in-memory boundaries.

The exact beginnings of CreateSave, WriteSave and DeleteSave are compiled for network-on and default-off flavours;
their remaining bodies are fake counters. Separately, the complete patched methods run against fake in-memory
platform APIs in Linux and Windows preprocessor modes. The real patched options writer remains callable. A header mirror
supplies Linux and Windows target syntax checks of the whole card unit: with no --mirror, the headers of this checkout
are generated here (every header of src/ as build.py normalises it, each patch's section applied, the network's
conditionals left in, so that the network and private flavours see their own text and the default flavour is compiled with
CE_NETWORK_OFF defined); with --mirror, the folder of a mirror build.py made (either flavour; see pick_mirror). No game,
actual save, device, network connection or Windows runtime is exercised.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / "port"), str(ROOT / "port/tools")]
import build
from ce_photo_tests import patched
import save_flow_tests

PATCH = "0185-network-career-write-fence.patch"
DRIVER = ROOT / "port/tools/tests/network_career_fence_driver.cpp"
FULL_DRIVER = ROOT / "port/tools/tests/network_career_full_driver.cpp"
METHODS = ("CreateSave", "WriteSave", "DeleteSave")
CONDITION = "CE_BootRequested() || CE_LockstepRequested()"
HEADERS = (".h", ".hpp", ".inl", ".stf")      # what the whole-card checks take from a mirror
WITNESSES = ("common.h", "PCMemoryCard.h")    # headers that stand directly in the folder the checks include from


def pick_mirror(path):
    """The folder the whole-card checks include the game's headers from: build.py's MIRROR (local-data/port/mirror/src), given as itself or as the mirror root that holds it
    (local-data/port/mirror, with linklib/ beside src/). A mirror of either flavour does (the card unit compiles the same on a default build's and on a network build's);
    a folder with no mirror in it, or one from which the headers would not be found, is refused rather than compiled against. Raises ValueError."""
    for folder in (path, path / "src"):
        if all((folder / name).is_file() for name in WITNESSES):
            return folder
    raise ValueError(f"{path}: not a generated mirror (expected {' and '.join(WITNESSES)} in it or in its src folder; a build's is local-data/port/mirror/src)")


def generate_mirror(folder):
    """The header side of build.py's mirror, made here from this checkout and no game data: every header of src/ as the build normalises it, with each patch's section for it applied in
    order. The network's conditionals stay in, as a build with the network and a private build see them. Returns the folder to include from."""
    names = sorted(str(f.relative_to(ROOT / "src")) for f in build.tracked_sources()
                   if f.is_relative_to(ROOT / "src") and f.suffix.lower() in HEADERS)
    for name in names:
        (folder / "src" / name).parent.mkdir(parents=True, exist_ok=True)
    patched(folder, names)
    return pick_mirror(folder / "src")


def method(source, name):
    return save_flow_tests.function(source, r"^int\s+CPCMemoryCard::" + name + r"\(")


def entrance(source, name):
    text = method(source, name)
    # The gate must be the first statement: an early file operation would invalidate the safety claim.
    match = re.match(r"(?s)^(.*?\n\{\n)(#ifndef CE_NETWORK_OFF\n.*?\n#endif\n)", text)
    if not match or CONDITION not in match.group(2):
        raise ValueError("missing or late network guard: " + name)
    signature = match.group(1).splitlines()[0].split("//", 1)[0].replace("CPCMemoryCard::", "").strip() + ";"
    fake = {"CreateSave": "++creates; if (save) *save = 91; return 0;",
            "WriteSave": "++writes; return 0;", "DeleteSave": "++deletes; return 0;"}[name]
    return signature, match.group(1) + match.group(2) + "    " + fake + "\n}\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--mirror", type=Path, help="a mirror build.py made, of either flavour: its src folder or the mirror root that holds it (local-data/port/mirror/src or local-data/port/mirror); "
                                                   "without it the headers are generated from this checkout")
    args = ap.parse_args()
    if args.mirror is not None:
        try:
            args.mirror = pick_mirror(args.mirror.resolve())
        except ValueError as refused:
            ap.error(str(refused))
    parent = ROOT / "local-data/port/network-career-fence-tests"
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    result = {"cases": [], "syntax": [], "scope": __doc__, "passed": False}

    def case(label, ok):
        result["cases"].append({"name": label, "passed": bool(ok)})
        print(("PASS " if ok else "FAIL ") + label)
        if not ok:
            raise AssertionError(label)

    card = build.patched_text(["PCMemoryCard.cpp"])["PCMemoryCard.cpp"]
    options = build.patched_text(["FEPOptions.cpp"])["FEPOptions.cpp"]
    old_public = build.patched_text(["PCMemoryCard.cpp"], skip=(PATCH,), network=False)["PCMemoryCard.cpp"]
    new_public = build.patched_text(["PCMemoryCard.cpp"], network=False)["PCMemoryCard.cpp"]
    case("network-off card source matches before the fence", old_public == new_public)
    declarations, entries = zip(*(entrance(card, name) for name in METHODS))
    options_writer = save_flow_tests.function(options, r"^void\s+CFEPOptions::WriteDefaultOptionsFile\(")

    def compile_fake(label, entry_text, options_text=options_writer, defines=()):
        folder = out / label
        folder.mkdir()
        (folder / "career_declarations.inc").write_text("\n".join(declarations) + "\n")
        (folder / "career_entries.inc").write_text(entry_text)
        (folder / "options_writer.inc").write_text(options_text)
        binary = folder / "driver"
        cmd = ["g++", "-std=c++14", "-O1", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
               '-DCE_VERSION="fixture"', *defines, "-I" + str(folder), str(DRIVER), "-o", str(binary)]
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
        (folder / "build.log").write_text(p.stdout + p.stderr)
        case(label + " compiles", p.returncode == 0)
        return binary

    def invoke(label, binary, boot, lockstep, expected=0):
        p = subprocess.run([str(binary), str(boot), str(lockstep)], capture_output=True, timeout=10)
        if p.returncode != expected:
            (binary.parent / f"failure-{boot}-{lockstep}.log").write_bytes(p.stderr)
        case(label, p.returncode == expected)

    joined = "\n".join(entries)
    for platform, defines in (("linux", ()), ("windows-preprocessor", ("-D_WIN32=1",))):
        active = compile_fake(platform + "-network", joined, defines=defines)
        off = compile_fake(platform + "-default", joined, defines=(*defines, "-DCE_NETWORK_OFF=1"))
        for boot, lockstep in ((0, 0), (1, 0), (0, 1), (1, 1)):
            invoke(f"{platform} network boot={boot} lockstep={lockstep}", active, boot, lockstep)
            invoke(f"{platform} default boot={boot} lockstep={lockstep}", off, boot, lockstep)

    def compile_full(label, sources, defines=(), ce_version=True):
        folder = out / label
        folder.mkdir()
        (folder / "career_declarations.inc").write_text("\n".join(declarations) + "\n")
        (folder / "career_full_methods.inc").write_text("\n".join(sources))
        (folder / "options_writer.inc").write_text(options_writer)
        binary = folder / "driver"
        cmd = ["g++", "-std=c++14", "-O1", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
               "-Wno-unused-function", "-Wno-unused-variable", *(['-DCE_VERSION="fixture"'] if ce_version else []), *defines,
               "-I" + str(folder), str(FULL_DRIVER), "-o", str(binary)]
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
        (folder / "build.log").write_text(p.stdout + p.stderr)
        case(label + " compiles", p.returncode == 0)
        return binary

    full_methods = [method(card, name) for name in METHODS]
    for platform, defines in (("linux", ()), ("windows-preprocessor", ("-D_WIN32=1",))):
        for flavour, network_defines, ce_version in (("network", (), True), ("default", ("-DCE_NETWORK_OFF=1",), True),
                                                      ("private", (), False)):
            binary = compile_full(platform + "-full-" + flavour, full_methods, (*defines, *network_defines), ce_version)
            for boot, lockstep in ((0, 0), (1, 0), (0, 1), (1, 1)):
                invoke(f"{platform} full {flavour} boot={boot} lockstep={lockstep}", binary, boot, lockstep)
        for i, name in enumerate(METHODS):
            changed = list(full_methods)
            assert changed[i].count(CONDITION) == 1
            changed[i] = changed[i].replace(CONDITION, "false", 1)
            binary = compile_full(platform + "-full-mutant-no-" + name.lower(), changed, defines)
            invoke(f"{platform} full mutant: missing {name} guard", binary, 1, 0, expected=1)

    for i, name in enumerate(METHODS):
        changed = list(entries)
        assert changed[i].count(CONDITION) == 1
        changed[i] = changed[i].replace(CONDITION, "false", 1)
        binary = compile_fake("mutant-no-" + name.lower(), "\n".join(changed))
        invoke("mutant: missing " + name + " guard", binary, 1, 0, expected=1)

    for label, replacement, boot, lockstep in (("boot", "CE_LockstepRequested()", 1, 0),
                                                ("test-lockstep", "CE_BootRequested()", 0, 1)):
        binary = compile_fake("mutant-no-" + label, joined.replace(CONDITION, replacement))
        invoke("mutant: missing " + label + " request", binary, boot, lockstep, expected=1)
    assert joined.count("if (save) *save = -1;") == 1
    binary = compile_fake("mutant-create-output", joined.replace("if (save) *save = -1;", "if (save) *save = 0;"))
    invoke("mutant: refused create does not initialize its output", binary, 1, 0, expected=1)
    assert options_writer.count("if (PortReplaceUserFile(") == 1
    blocked_options = options_writer.replace("if (PortReplaceUserFile(", "if (0 && PortReplaceUserFile(")
    binary = compile_fake("mutant-options-disabled", joined, options_text=blocked_options)
    invoke("mutant: admitted options writer is disabled", binary, 1, 0, expected=1)
    body = method(card, "CreateSave")
    late = card.replace(body, body.replace("\n#ifndef CE_NETWORK_OFF\n", "\n++creates;\n#ifndef CE_NETWORK_OFF\n", 1), 1)
    try:
        entrance(late, "CreateSave")
    except ValueError:
        case("mutant: a side effect before the guard is refused", True)
    else:
        case("mutant: a side effect before the guard is refused", False)

    # The mirror the checks include from: the one given, or the headers generated from this checkout. What picks and refuses a folder is itself checked first.
    nothing = out / "no-mirror-here"
    (nothing / "src").mkdir(parents=True)
    (nothing / "src" / "common.h").write_text("")       # one witness of two is not a mirror
    for label, folder in (("a missing folder", out / "missing"), ("a folder without a mirror", nothing), ("a folder with one of the headers", nothing / "src")):
        try:
            pick_mirror(folder)
        except ValueError:
            case("mirror choice: " + label + " is refused", True)
        else:
            case("mirror choice: " + label + " is refused", False)
    headers = args.mirror if args.mirror is not None else generate_mirror(out / "generated")
    case("mirror choice: the folder is the one the headers stand in", pick_mirror(headers) == headers)
    if headers.name == "src":
        case("mirror choice: the mirror root is taken for its src folder", pick_mirror(headers.parent) == headers)
    mirror = out / "mirror"
    mirror.mkdir()
    for source in headers.rglob("*"):
        if source.is_file() and source.suffix.lower() in HEADERS:
            target = mirror / source.relative_to(headers)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
    (mirror / "PCMemoryCard.cpp").write_text(card, encoding="latin1")
    for platform in ("linux", "windows"):
        target = build.TARGETS[platform]
        # build.py's include list for the game's own files (port/game holds the CE headers the patched card includes)
        includes = [build.PORT / "d3dx/include", build.PORT / "include", build.PORT / "game", *target["includes"], mirror, mirror / "MissionScript"]
        for flavour, defines in (("network", ['-DCE_VERSION="fixture"']),
                                  ("default", ['-DCE_VERSION="fixture"', "-DCE_NETWORK_OFF=1"]),
                                  ("private", [])):
            cmd = [*target["cxx"], "-std=gnu++14", *build.MSCXX, *build.COMMON, *build.X87, *build.NO_MATH_BUILTINS,
                   *defines,
                   "-fsyntax-only", "-include", str(build.PORT / "include/port.h"),
                   *["-I" + str(p) for p in includes], str(mirror / "PCMemoryCard.cpp")]
            p = subprocess.run(cmd, capture_output=True, text=True, timeout=90)
            label = platform + "-" + flavour
            (out / (label + "-syntax.log")).write_text(p.stdout + p.stderr)
            result["syntax"].append({"name": label, "passed": p.returncode == 0})
            case(label + " whole-card target syntax", p.returncode == 0)

    result["passed"] = True
    (out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print("receipt: " + str(out / "result.json"))


if __name__ == "__main__":
    main()
