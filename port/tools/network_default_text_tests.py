#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The default public build compiles main's text, not a lookalike of it.

build.py --public (no --with-network) defines CE_NETWORK_OFF, compiles none of the network's modules and resolves the CE_NETWORK_OFF
conditionals of the mirror's files (network_public_source_tests: src/ then equals main's, byte for byte). The port's own files that the
network also changed are guarded the same way, so resolving them (build.network_view, off=True) must give main's file exactly. Two pin
files hold main's side, as sha-256 of the text:

  port/network/MAIN-TEXT.sha256     every port code file a default build can compile or include (resolved text), and the compile inputs
                                    that are not code (flags files, aliases, resources)
  port/network/MAIN-SOURCES.sha256  every file of src/ and linklib/, and every patch of main's queue (the network's own patches are the
                                    ones port/network/PATCHES.txt lists)

This suite:
  1. checks each pinned file against its pin;
  2. requires every other port code file to be a network module (build.network_module) or a header that no default-compiled file includes,
     every other patch to be listed in PATCHES.txt, and no src/ or linklib/ file to be new;
  3. has mutants: a hook left outside its guard, a guard turned the wrong way, a rewrite that lost the original line, a new file in the
     default build, a network-only header reached from a default file, a changed source, a changed patch of main's queue, an unlisted patch
     and a new source file must each be caught.

Refresh the pins after merging main (the pinned text is main's own, so a file main changed is re-pinned from main's tree):
    python3 port/tools/network_default_text_tests.py --pin <main ref>
To re-pin only the files a change to main touches, from the checkout (the network modules stay as they are; --pin <ref> pins them as main's too):
    python3 port/tools/network_default_text_repin.py <path> ...
No game data, device or network is used.
"""
import hashlib
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port"))
import build  # noqa: E402

PINS = ROOT / "port/network/MAIN-TEXT.sha256"
SOURCE_PINS = ROOT / "port/network/MAIN-SOURCES.sha256"
MANIFEST = ROOT / "port/network/PATCHES.txt"
CODE = (".c", ".cpp", ".h", ".hpp", ".inl", ".inc")
SKIPPED_DIRS = {"tools", "tests", "network", "nettest-kit", "patches", "deps-patches"}
# Compile inputs that are not code: their raw hash is pinned, no conditional is resolved.
OTHER = ("port/aliases.tsv", "port/platform/windows/utf8.manifest", "port/platform/windows/utf8.rc", "port/platform/windows/version.rc",
         "port/platform/windows/launcher-version.rc")
OTHER_PATTERN = re.compile(r"^port/[^/]+/flags\.txt$")


def resolved(text):
    return build.network_view(text, off=True)


def digest(text):
    return hashlib.sha256(text.encode("latin-1")).hexdigest()


def in_scope(rel):
    parts = rel.split("/")
    return rel.startswith("port/") and not (set(parts[1:-1]) & SKIPPED_DIRS)


def scope_paths(names):
    return sorted(n for n in names if in_scope(n) and (n.lower().endswith(CODE) or n in OTHER or OTHER_PATTERN.match(n)))


def is_source_path(rel):
    return rel.startswith(("src/", "linklib/")) or (rel.startswith("port/patches/") and rel.endswith(".patch"))


def listed_network_patches():
    return {l.strip() for l in MANIFEST.read_text().splitlines() if l.strip() and not l.startswith("#")}


def checkout_files(root, dirs):
    """The repository-relative paths of the files under dirs of the checkout at root that belong to it: the tracked ones and the untracked
    ones that are not ignored (a new file not added yet still counts, so the "not in main" checks see it). An ignored file, such as
    the symlinks to retail data under src/data/ or the generated src/NVShaderFragments.inc that a working checkout has, is not a source
    of the build's text. Without git (an exported tree) every file counts."""
    done = subprocess.run(["git", "-C", str(root), "ls-files", "-z", "--cached", "--others", "--exclude-standard", "--", *dirs],
                          capture_output=True)
    if done.returncode == 0:
        return sorted(n for n in set(done.stdout.decode("utf-8", "surrogateescape").split("\0")) if n and (Path(root) / n).is_file())
    return sorted(p.relative_to(root).as_posix() for d in dirs for p in (Path(root) / d).rglob("*") if p.is_file())


def checkout_texts():
    names = checkout_files(ROOT, ["port"])
    return {n: (ROOT / n).read_bytes().decode("latin-1") for n in scope_paths(names)}


def checkout_sources():
    names = checkout_files(ROOT, ["src", "linklib", "port/patches"])
    return {n: (ROOT / n).read_bytes().decode("latin-1") for n in sorted(names) if is_source_path(n)}


def read_pins(path):
    pins = {}
    for line in path.read_text().splitlines():
        if line.strip() and not line.startswith("#"):
            h, name = line.split(None, 1)
            pins[name.strip()] = h
    return pins


def is_code(name):
    return name.lower().endswith(CODE)


def problems(texts, pins):
    """What is wrong with {path: text} against {path: sha-256 of main's resolved text}."""
    out = []
    for name, want in sorted(pins.items()):
        if name not in texts:
            out.append(f"{name}: pinned but missing")
            continue
        got = digest(resolved(texts[name]) if is_code(name) else texts[name])
        if got != want:
            out.append(f"{name}: the default flavour's text is not main's (resolved sha-256 {got[:12]}, main {want[:12]})")
    others = [n for n in texts if n not in pins]
    compiled = [n for n in texts if is_code(n) and n.lower().endswith((".c", ".cpp")) and not build.network_module(ROOT / n)]
    for name in others:
        if name.lower().endswith((".c", ".cpp")) and name in compiled:
            out.append(f"{name}: not in main, yet the default build compiles it (a network module belongs in build.network_module)")
    # headers not in main: no default-compiled file may include them
    reached = set()
    for name in compiled + [n for n in pins if n.lower().endswith((".h", ".hpp", ".inl", ".inc")) and n in texts]:
        for inc in re.findall(r'^[ \t]*#\s*include\s*[<"]([^>"]+)[>"]', resolved(texts[name]), re.M):
            reached.add(inc.rsplit("/", 1)[-1])
    for name in others:
        if name.lower().endswith((".h", ".hpp", ".inl", ".inc")) and name.rsplit("/", 1)[-1] in reached:
            out.append(f"{name}: not in main, yet a file of the default build includes it")
    return out


def source_problems(sources, pins, network_patches):
    """src/, linklib/ and the patches of main's queue against {path: sha-256 of main's file}; the network's own patches are not main's."""
    out = []
    for name, want in sorted(pins.items()):
        if name not in sources:
            out.append(f"{name}: pinned but missing")
        elif digest(sources[name]) != want:
            out.append(f"{name}: not main's file (sha-256 {digest(sources[name])[:12]}, main {want[:12]})")
    for name in sorted(sources):
        if name in pins:
            continue
        if name.startswith("port/patches/"):
            if name.rsplit("/", 1)[-1] not in network_patches:
                out.append(f"{name}: not in main's queue and not listed in port/network/PATCHES.txt")
        else:
            out.append(f"{name}: not in main")
    for name in network_patches:
        if f"port/patches/{name}" in pins:
            out.append(f"port/patches/{name}: listed as a network patch, yet main has it")
    return out


def git_blobs(ref, paths):
    """{path: text} of the files at ref, read with one git process."""
    spec = "".join(f"{ref}:{p}\n" for p in paths)
    done = subprocess.run(["git", "-C", str(ROOT), "cat-file", "--batch"], input=spec.encode(), capture_output=True, check=True).stdout
    out, pos = {}, 0
    for p in paths:
        end = done.index(b"\n", pos)
        header = done[pos:end].split()
        if header[-1] == b"missing":
            raise SystemExit(f"{ref}:{p} is missing")
        size = int(header[2])
        out[p] = done[end + 1:end + 1 + size].decode("latin-1")
        pos = end + 1 + size + 1
    return out


def pin(ref):
    files = subprocess.run(["git", "-C", str(ROOT), "ls-tree", "-r", "--name-only", ref, "--", "port", "src", "linklib"], capture_output=True,
                           text=True, check=True).stdout.split()
    head = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--short=8", ref], capture_output=True, text=True, check=True).stdout.strip()
    code = scope_paths(files)
    text = git_blobs(ref, code)
    PINS.write_text("# sha-256 of main's text of the port files a default public build compiles or includes (network_default_text_tests.py).\n"
                    f"# Taken from main at {head}; refresh with: python3 port/tools/network_default_text_tests.py --pin <main ref>\n" +
                    "\n".join(f"{digest(resolved(text[n]) if is_code(n) else text[n])}  {n}" for n in code) + "\n")
    src = [n for n in files if is_source_path(n)]
    blobs = git_blobs(ref, src)
    SOURCE_PINS.write_text("# sha-256 of main's src/ and linklib/ files and of the patches of main's queue (network_default_text_tests.py).\n"
                           f"# Taken from main at {head}; refresh with: python3 port/tools/network_default_text_tests.py --pin <main ref>\n" +
                           "\n".join(f"{digest(blobs[n])}  {n}" for n in src) + "\n")
    print(f"pinned {len(code)} port files and {len(src)} source files and patches from {ref} ({head})")


def main():
    if "--pin" in sys.argv:
        pin(sys.argv[sys.argv.index("--pin") + 1])
        return 0
    ok = True

    def check(name, good):
        nonlocal ok
        print(("PASS " if good else "FAIL ") + name)
        ok &= bool(good)

    pins = read_pins(PINS)
    texts = checkout_texts()
    check(f"main's text is pinned for {len(pins)} port files", len(pins) > 100)
    found = problems(texts, pins)
    for line in found[:15]:
        print("  ", line)
    check("every pinned file resolves to main's text, and every other port file is a network module or a header the default build never includes",
          not found)
    check("the legacy network stand-in file (platform/public/network_off.cpp) is gone and build.py does not add it: no default-compiled source needs a network stub",
          "port/platform/public/network_off.cpp" not in texts and "network_off.cpp" not in (ROOT / "port/build.py").read_text())

    spins = read_pins(SOURCE_PINS)
    sources = checkout_sources()
    network_patches = listed_network_patches()
    check(f"main's files are pinned for {len(spins)} sources and patches", len(spins) > 500)
    found = source_problems(sources, spins, network_patches)
    for line in found[:15]:
        print("  ", line)
    check("src/, linklib/ and main's patches are main's files, and every other patch is a listed network patch", not found)

    # a working checkout holds ignored files under src/ (symlinks into retail data, the generated shader include) that are no part of the
    # build's text: the walk is by git, tracked and not-yet-added files, not by the folder's contents
    with tempfile.TemporaryDirectory(prefix="ce-walk-") as walk:
        repo = Path(walk)
        for name, text in (("src/Kept.cpp", "int a;\n"), ("src/data/Retail.msl", "x\n"), ("src/Generated.inc", "y\n"), ("port/New.cpp", "int b;\n"),
                           ("port/Added.cpp", "int c;\n"), ("linklib/L.cpp", "int d;\n")):
            (repo / name).parent.mkdir(parents=True, exist_ok=True)
            (repo / name).write_text(text)
        (repo / ".gitignore").write_text("src/data/\nsrc/Generated.inc\n")
        subprocess.run(["git", "-C", str(repo), "init", "-q"], check=True)
        subprocess.run(["git", "-C", str(repo), "add", ".gitignore", "src/Kept.cpp", "port/Added.cpp", "linklib/L.cpp"], check=True)
        check("the checkout walk lists tracked and not-yet-added files and leaves ignored ones out",
              checkout_files(repo, ["src", "linklib", "port"]) == ["linklib/L.cpp", "port/Added.cpp", "port/New.cpp", "src/Kept.cpp"])
        # an exported tree has no git of its own: every file counts
        loose = repo / "loose"
        (loose / "port").mkdir(parents=True)
        (loose / "port/A.cpp").write_text("int e;\n")
        check("without git the walk lists every file", checkout_files(loose, ["port"]) == ["port/A.cpp"])
    check("the checkout's own sources are listed without the ignored files",
          not [n for n in checkout_sources() if n.startswith("src/data/")])

    # the port view a default build compiles from (build.build_port_view): main's text, file by file, and the compiler is pointed at it
    saved = (build.PUBLIC, build.WITH_NETWORK, build.VIEW)
    (ROOT / "local-data").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="ce-view-", dir=ROOT / "local-data") as tmp:
        build.VIEW = Path(tmp) / "port"
        try:
            build.PUBLIC, build.WITH_NETWORK = True, False
            build.build_port_view()
            files = {p.relative_to(build.VIEW).as_posix(): p.read_bytes().decode("latin-1") for p in build.VIEW.rglob("*") if p.is_file()}
            wrong = [n for n, want in sorted(pins.items()) if is_code(n) and
                     digest(files.get(n[len("port/"):], "\0missing")) != want]
            for name in wrong[:10]:
                print("   the view's text is not main's:", name)
            check(f"the default build's port view holds main's text for every pinned file ({len(files)} files copied)", not wrong)
            conditional = re.compile(r"^[ \t]*#[ \t]*(?:ifdef|ifndef|if|elif)\b[^\n]*CE_NETWORK_OFF", re.M)
            check("the view holds no CE_NETWORK_OFF conditional and none of the skipped directories",
                  not any(conditional.search(t) for t in files.values()) and not any(set(n.split("/")[:-1]) & build.VIEW_SKIPPED for n in files))
            check("the compiler is pointed at the view in a default build",
                  build.cc_path(build.PORT / "game/ce_frontend.cpp") == build.VIEW / "game/ce_frontend.cpp"
                  and build.cc_path(build.PORT / "include/linux") == build.VIEW / "include/linux")
            check("the tools and the network's own directory are not redirected",
                  build.cc_path(build.PORT / "tools/qifist.py") == build.PORT / "tools/qifist.py"
                  and build.cc_path(build.PORT / "network/codec.cpp") == build.PORT / "network/codec.cpp")
            build.WITH_NETWORK = True
            check("a build with the network compiles the port itself", build.cc_path(build.PORT / "game/ce_frontend.cpp") == build.PORT / "game/ce_frontend.cpp")
            build.PUBLIC, build.WITH_NETWORK = False, False
            check("a private build compiles the port itself", build.cc_path(build.PORT / "game/ce_frontend.cpp") == build.PORT / "game/ce_frontend.cpp")
        finally:
            build.PUBLIC, build.WITH_NETWORK, build.VIEW = saved

    # mutants, on texts held in memory
    victim = "port/platform/common/crtrand.cpp"
    clean = texts[victim]
    guard = "#ifndef CE_NETWORK_OFF\n// Lockstep co-op test trace"
    check("mutant source has the guard to turn", guard in clean)
    mutants = {
        "a hook left outside its guard": clean.replace("#ifndef CE_NETWORK_OFF\n\tif (gPortCrtTrace)", "\tif (gPortCrtTrace)\n#ifndef CE_NETWORK_OFF", 1),
        "a guard turned the wrong way": clean.replace("#ifndef CE_NETWORK_OFF\n\tif (gPortCrtTrace)", "#ifdef CE_NETWORK_OFF\n\tif (gPortCrtTrace)", 1),
        "a stray line in main's text": clean.replace("static thread_local unsigned int tCalls;", "int stray;\nstatic thread_local unsigned int tCalls;", 1),
    }
    for label, mutated in mutants.items():
        check(f"mutant: {label} is caught", mutated != clean and any(victim in p for p in problems({**texts, victim: mutated}, pins)))
    rewrite = "port/platform/common/crtfile.cpp"
    lost = re.sub(r"(#ifdef CE_NETWORK_OFF\n)[^\n]*\n", r"\1", texts[rewrite], count=1)
    check("mutant: a rewrite that lost its original line is caught",
          lost != texts[rewrite] and any(rewrite in p for p in problems({**texts, rewrite: lost}, pins)))
    check("mutant: a new file in the default build is caught",
          any("not in main" in p for p in problems({**texts, "port/game/ce_extra.cpp": "int x;\n"}, pins)))
    check("mutant: a network-only header reached from a default file is caught",
          any("includes it" in p for p in problems({**texts, victim: '#include "input_activity.h"\n' + clean}, pins)))
    some_source = next(n for n in sorted(sources) if n.startswith("src/") and n in spins)
    check("mutant: a changed source file is caught",
          any(some_source in p for p in source_problems({**sources, some_source: sources[some_source] + "\n// x\n"}, spins, network_patches)))
    some_patch = next(n for n in sorted(sources) if n.startswith("port/patches/") and n in spins)
    check("mutant: a changed patch of main's queue is caught",
          any(some_patch in p for p in source_problems({**sources, some_patch: sources[some_patch] + "\n"}, spins, network_patches)))
    check("mutant: a patch that is neither main's nor listed is caught",
          any("not in main's queue" in p for p in source_problems({**sources, "port/patches/0999-extra.patch": "x\n"}, spins, network_patches)))
    check("mutant: a new source file is caught",
          any("not in main" in p for p in source_problems({**sources, "src/Extra.cpp": "int x;\n"}, spins, network_patches)))
    print("network default-text controls: " + ("OK" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
