#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""A --public build patches src/ to exactly what main's patches alone produce.

build.py --public defines CE_NETWORK_OFF. Every hunk of every patch in port/network/PATCHES.txt is inside CE_NETWORK_OFF guards
(an insertion in #ifndef, a rewrite in #ifdef old #else new #endif), so with the define set the network leaves the game's own code. This
suite builds the patched source tree twice with build.py's own rules, once with the whole queue and once with the queue minus the
network patches, resolves the CE_NETWORK_OFF conditionals as the preprocessor would for a public build, and requires every file to be
identical. The reference retains patch 0157 only as context required by the non-network HUD patch 0190: applying that context must first
leave every resolved public file unchanged. All non-network patches remain in both final trees. The public replacements that carry guards (d3dapp.*) are checked against the hashes of
main's files. Mutants: a network line outside any guard, an unguarded rewrite and a lost original line must each be caught.
No game data, device or network is used; the retail include names come from the mirror rules (build.normalise).
"""
import hashlib
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port"))
import build  # noqa: E402

MANIFEST = ROOT / "port/network/PATCHES.txt"
# Reviewed public replacements that the network guards touch, through host 1863f816 (update after review). 0.1.14: the device recovery code of d3dapp.* is no longer
# behind CE_NETWORK_OFF (patch 0085 applies in the default build), so these are the default build's text of the files.
MAIN_REPLACEMENTS = {
    "d3dapp.cpp": "d0338c119452316b940edcfb9ec6020230f6acd47b292e6c795feda03a9f59f5",
    "d3dapp.h": "cc0b7513d58804631fdc3ebce96864ba9d9087a48252ee28e4d1ec23a29c84f1",
}
CODE = (".c", ".cpp", ".h", ".hpp", ".inl", ".inc")
# Guarded network patches whose lines are the context of default patches: 0190's hunks need 0157's HUD context, and the hunks of the lane patches of
# 0.1.14 (the graphics policy 0204, the editor and museum lifecycles 0206 and 0207 and the patches built on them) sit beside lines that 0065, 0067,
# 0076, 0082, 0121, 0129, 0146, 0151 and 0189 add inside their CE_NETWORK_OFF guards (the include block and message-loop lines of ltshell.cpp, the
# startup-script and level-start lines of game.cpp). The reference tree applies them first as context only; applying each must leave every resolved
# public file unchanged (patched_tree checks that). The set is the smallest that lets every default patch apply, found by removing one patch at a time
# from the whole network queue. This is not an optional-patch classification.
PUBLIC_CONTEXT_PATCHES = frozenset(("0065-network-world.patch", "0067-network-session.patch", "0076-network-graceful-departure-exit.patch",
                                    "0082-compatibility-hooks.patch", "0121-lockstep-message-timing.patch", "0129-lockstep-particle-draws.patch",
                                    "0146-network-boot-script.patch", "0151-network-teardown-gate.patch", "0157-network-hud-scale-coop.patch",
                                    "0189-network-campaign-outcomes.patch"))
# The mandatory public HUD feature also guards its calls into the optional network layer.
# Device recovery (0085) is mandatory in the public build as well: its recovery block and its changed reset and zero-delta lines are not guarded; only the lines
# that touch the network session (the observation the session service reads, and the yield an active session leaves to its own boundary) are.
# The editor and museum lifecycles (0206, 0207), Full models (0208) and the Dodge Button (0220) guard their calls into the network session the same way.
PUBLIC_GUARDED_PATCHES = frozenset(("0190-ce-hud-4k.patch", "0085-public-device-recovery.patch", "0206-native-editor-lifecycle.patch",
                                    "0207-ce-museum-visit-lifecycle.patch", "0208-ce-full-models.patch", "0220-ce-dodge-assist.patch"))


def strip(text):
    """The text as a compiler with CE_NETWORK_OFF defined sees it."""
    return build.network_view(text, off=True)


def patched_tree(dest, patches, context_only=()):
    """build.build_mirror's src/ for a public build, without game data: normalised sources, the public replacements, then PATCHES."""
    context_only = frozenset(context_only)
    if sorted(p.name for p in patches if p.name in context_only) != sorted(context_only):
        raise RuntimeError("Reference-only context must occur exactly once in the patch queue")
    build.SYSTEM_NAMES.update(build.system_header_names())
    files = build.tracked_sources()
    rel = [f.relative_to(ROOT) for f in files]
    for name in build.PUBLIC_REPLACEMENTS:
        if Path("src") / name not in rel and (build.PORT / "platform/public" / name).exists():
            files.append(build.PORT / "platform/public" / name)
            rel.append(Path("src") / name)
    rows = [l.split(maxsplit=1) for l in (ROOT / "config/game-data.sha256").read_text().splitlines() if l.strip()]
    mirror_root, mirror = dest, dest / "src"
    targets = [mirror_root / r for r in rel] + [mirror / r for _, r in rows] + [mirror / "data/NVShaderFragments.inc"]
    index = build.CaseIndex(targets)
    for src, r in zip(files, rel):
        if r.parent == Path("src") and r.name in build.PUBLIC_REPLACEMENTS and (build.PORT / "platform/public" / r.name).exists():
            src = build.PORT / "platform/public" / r.name
        raw = src.read_bytes()
        if src.suffix.lower() in CODE:
            raw = build.normalise(raw.decode("latin-1"), (mirror_root / r).parent, index, [mirror, mirror / "MissionScript"]).encode("latin-1")
        target = mirror_root / r
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(raw)
    def public_snapshot():
        return {f.relative_to(mirror): strip(f.read_bytes().decode("latin-1")).encode("latin-1")
                if f.suffix.lower() in CODE else f.read_bytes()
                for f in mirror.rglob("*") if f.is_file()}
    checked_context = set()
    for patch in patches:
        if build.PUBLIC_SKIPPED_PATCHES.get(patch.name) not in (None,):
            continue
        before = public_snapshot() if patch.name in context_only else None
        r = subprocess.run(["patch", "-p1", "-d", str(mirror), "-i", str(patch), "--forward", "-r", "-", "--no-backup-if-mismatch"],
                           capture_output=True, text=True)
        if r.returncode or "fuzz" in r.stdout:
            raise RuntimeError(f"{patch.name} does not apply cleanly:\n{r.stdout}{r.stderr}")
        if before is not None and before != public_snapshot():
            raise RuntimeError(f"{patch.name}: reference-only context changes resolved public source")
        if before is not None:
            checked_context.add(patch.name)
    if checked_context != context_only:
        raise RuntimeError("Reference-only context was skipped without a neutrality check")
    return mirror


def differences(network_tree, main_tree):
    """Files whose resolved text is not main's."""
    bad = []
    for a in sorted(network_tree.rglob("*")):
        if not a.is_file():
            continue
        rel = a.relative_to(network_tree)
        b = main_tree / rel
        text = a.read_bytes().decode("latin-1")
        if a.suffix.lower() in CODE:
            text = strip(text)
        if not b.is_file():
            bad.append((str(rel), "exists only with the network patches"))
            continue
        other = b.read_bytes().decode("latin-1")
        if b.suffix.lower() in CODE:
            other = strip(other)        # the public replacements (d3dapp.*) carry guards in both trees; the hashes above pin main's text
        if text != other:
            bad.append((str(rel), "differs from main's after resolving CE_NETWORK_OFF"))
    for b in sorted(main_tree.rglob("*")):
        if b.is_file() and not (network_tree / b.relative_to(main_tree)).is_file():
            bad.append((str(b.relative_to(main_tree)), "exists only without the network patches"))
    return bad


def main():
    ok = True

    def check(name, good):
        nonlocal ok
        print(("PASS " if good else "FAIL ") + name)
        ok &= bool(good)

    listed = [l.strip() for l in MANIFEST.read_text().splitlines() if l.strip() and not l.startswith("#")]
    patches = sorted((ROOT / "port/patches").glob("*.patch"))
    names = {p.name for p in patches}
    check("every listed network patch exists", all(n in names for n in listed))
    check("reference-only context patches are listed network patches", PUBLIC_CONTEXT_PATCHES <= set(listed))
    check("public patches that use network guards remain mandatory",
          PUBLIC_GUARDED_PATCHES <= names and PUBLIC_GUARDED_PATCHES.isdisjoint(listed))
    check("guard users are listed network patches or the mandatory public HUD patch",
          all(("CE_NETWORK_OFF" in (ROOT / "port/patches" / n).read_text()) ==
              (n in listed or n in PUBLIC_GUARDED_PATCHES) for n in names))
    prefixes = [n[:4] for n in names]
    check("no network patch shares its number with another patch", all(prefixes.count(n[:4]) == 1 for n in listed))
    for name, digest in MAIN_REPLACEMENTS.items():
        text = (build.PORT / "platform/public" / name).read_bytes().decode("latin-1")
        check(f"public/{name} resolves to main's file", hashlib.sha256(strip(text).encode("latin-1")).hexdigest() == digest)
    (ROOT / "local-data").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="ce-network-source-", dir=ROOT / "local-data") as tmp:
        tmp = Path(tmp)
        network = patched_tree(tmp / "network", patches)
        main_tree = patched_tree(tmp / "main", [p for p in patches if p.name not in listed or p.name in PUBLIC_CONTEXT_PATCHES],
                                 context_only=PUBLIC_CONTEXT_PATCHES)
        bad = differences(network, main_tree)
        for rel, why in bad[:12]:
            print("  ", why + ":", rel)
        check("the resolved patched source of a public build equals main's (%d files compared)" %
              sum(1 for f in network.rglob("*") if f.is_file()), not bad)
        # mutants of the resolved game.cpp: each must differ from main's
        game = network / "game.cpp"
        resolved_main = strip((main_tree / "game.cpp").read_bytes().decode("latin-1"))
        text = game.read_bytes().decode("latin-1")
        check("game.cpp resolves to main's before any mutation", strip(text) == resolved_main)
        guard = "#ifndef CE_NETWORK_OFF\n\tif (!CE_SessionBeforeUpdate()) return;"
        mutants = (
            ("a network line outside any guard", text.replace(guard, "\tif (!CE_SessionBeforeUpdate()) return;\n#ifndef CE_NETWORK_OFF", 1)),
            ("a guarded rewrite that lost its original line", re.sub(r"(#ifdef CE_NETWORK_OFF\n)[^\n]*\n", r"\1", text, count=1)),
            ("an extra line in main's text", text.replace("void CGame::Update()", "int stray;\nvoid CGame::Update()", 1)),
        )
        for label, mutated in mutants:
            check(f"mutant: {label} is caught", mutated != text and strip(mutated) != resolved_main)
        # the default build hands the compiler main's text itself, line for line: build.py resolves the guards in the mirror
        # (build.resolve_network_off), because the preprocessor would skip the network's lines but still count them (every __LINE__ after a
        # hook, and every line table, would move). Resolving both trees as the build does must leave every file byte-identical.
        unresolved = (network / "game.cpp").read_bytes()
        for tree in (network, main_tree):
            build.resolve_network_off([f for f in tree.rglob("*") if f.is_file()])
        moved = [str(a.relative_to(network)) for a in sorted(network.rglob("*")) if a.is_file() and a.read_bytes() != (main_tree / a.relative_to(network)).read_bytes()]
        for name in moved[:12]:
            print("   differs after build.resolve_network_off:", name)
        check("the default build's mirror, resolved as build.py resolves it, is main's byte for byte (%d files compared)" %
              sum(1 for f in network.rglob("*") if f.is_file()), not moved)
        source = (build.PORT / "build.py").read_text()
        check("a default build resolves the mirror's guards and builds the port view, and build_mirror itself leaves the network's hooks in",
              re.search(r"if default_flavour\(\):\n\s+resolve_network_off\(sorted\(MIRROR_ROOT\.rglob\(\"\*\"\)\)\)\n\s+build_port_view\(\)", source) is not None
              and "resolve_network_off(" not in source[source.index("def build_mirror():"):source.index("NVTRISTRIP =")])
        check("mutant: left unresolved (the preprocessor alone), game.cpp is not main's byte for byte",
              unresolved != (main_tree / "game.cpp").read_bytes() and unresolved.count(b"\n") > (main_tree / "game.cpp").read_bytes().count(b"\n"))
    print("network public-source controls: " + ("OK" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
