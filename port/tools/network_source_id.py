#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Declared common source/dependency identity for the public i686 port profile.

Both supported targets use one transcript: public-effective game inputs, both platforms' port sources,
patches, build recipes and declared dependency/data pins. No checkout name, commit, environment, local
save, installed dependency or executable enters it. This is source compatibility metadata, not binary
attestation, semantic equivalence or anti-cheat. The caller must separately select the public pinned build;
a non-public or compiler/sysroot override build must not advertise this identity.

The helper reads working bytes named by the Git index, without importing build/dependency modules,
fetching inputs, materializing retail data or executing a compiler. Untracked production sources,
symlinks, missing indexed inputs and local literal includes outside the bound input set refuse.
Conditional preprocessing is the compiler's job: conservative dormant source is also bound; missing
console-only includes and pinned SDK includes are not interpreted here. Arbitrary compiler -I/-D flags
and edits made after this snapshot are outside this declaration and must not be accepted by its caller.

CLI: python3 port/tools/network_source_id.py [ROOT]
"""
from __future__ import annotations

import argparse
import ast
import hashlib
import os
from pathlib import Path, PurePosixPath
import posixpath
import re
import stat
import struct
import subprocess
import sys

# The exporter omits these two private-only inputs. build_mirror must omit exactly the same paths
# for --public even when it is run from a tree which still contains their originals.
PUBLIC_OMISSIONS = ("src/splinebasis.c", "linklib/nvtristrip_vc6.h")
DOMAIN = b"Onslaught CE declared-source compatibility v1\0"
PROFILE = b"public-i686-linux-and-windows-v1\0"
# Source directories may grow without a separately maintained module list. Tools/probes are not
# production roots; an actually consumed quoted helper outside these roots joins through closure.
NONPRODUCTION_PORT_ROOTS = ("port/tools/", "port/probe/", "port/docs/")
SOURCE_SUFFIXES = frozenset((".c", ".cpp", ".h", ".hpp", ".inc", ".rc", ".manifest"))
RECIPES = ("port/build.py", "port/tools/deps.py", "port/tools/qifist.py", "port/vorbis/build.py",
           "port/tools/network_source_id.py", "port/deps.lock", "port/aliases.tsv",
           "config/toolchain.sha256", "config/vc6-include.sha256", "config/game-data.sha256")
ICE_RECIPES = ("port/tools/ice_deps.py", "port/ice-deps.lock", "port/network/libjuice-relay-only.patch")
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"\r\n]+)[>"]', re.M)
QUOTED_INCLUDE = re.compile(r'^\s*#\s*include\s*"([^"\r\n]+)"', re.M)
RESOURCE_FILE = re.compile(r'^\s*[0-9]+\s+[0-9]+\s+"([^"\r\n]+)"', re.M)
PIN = re.compile(r"([0-9a-f]{64})[ \t]+(.+)")


class SourceIDError(ValueError):
    """A source snapshot cannot make the declared public-profile identity."""


def _name(name: str) -> str:
    if (not name or "\\" in name or "\0" in name or any(ord(c) < 32 for c in name)
            or str(PurePosixPath(name)) != name or name.startswith("/")
            or any(p in ("", ".", "..") for p in name.split("/"))):
        raise SourceIDError("invalid source-relative name")
    return name


def _root(path: Path) -> Path:
    path = Path(os.path.abspath(path))
    try:
        for p in (path, *path.parents):
            if p.is_symlink():
                raise SourceIDError("symlink in source root")
        if not path.is_dir():
            raise SourceIDError("source root is not a directory")
    except OSError as exc:
        raise SourceIDError("source root is unavailable") from exc
    return path


def _git(root: Path, *args: str) -> bytes:
    # Worktree/index/config overrides must not redirect a supposedly explicit source root. No lazy fetch.
    env = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
    env.update(GIT_OPTIONAL_LOCKS="0", GIT_NO_LAZY_FETCH="1", GIT_CONFIG_NOSYSTEM="1",
               GIT_CONFIG_GLOBAL=os.devnull, GIT_LITERAL_PATHSPECS="1")
    try:
        result = subprocess.run(["git", "-C", str(root), *args], env=env, capture_output=True, timeout=30)
    except (OSError, subprocess.SubprocessError) as exc:
        raise SourceIDError("source index is unavailable") from exc
    if result.returncode:
        raise SourceIDError("source index query refused")
    return result.stdout


def _index(root: Path) -> frozenset[str]:
    # Same source authority as build.py's tracked_sources(), extended to the port and manifest inputs.
    if _git(root, "ls-files", "--unmerged", "-z", "--", "src", "linklib", "port", "config"):
        raise SourceIDError("unmerged source index")
    raw = _git(root, "ls-files", "-z", "--", "src", "linklib", "port", "config")
    try:
        names = [_name(p.decode("utf-8")) for p in raw.split(b"\0") if p]
    except UnicodeError as exc:
        raise SourceIDError("source names must be UTF-8") from exc
    if len(names) != len(set(names)) or len(names) != len({p.casefold() for p in names}):
        raise SourceIDError("ambiguous source index")
    return frozenset(names)


def _read(root: Path, name: str) -> bytes:
    name = _name(name)
    path = root
    try:
        for part in name.split("/"):
            path /= part
            info = path.lstat()
            if stat.S_ISLNK(info.st_mode):
                raise SourceIDError("symlink input: " + name)
        fd = os.open(path, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK)
        try:
            before = os.fstat(fd)
            if not stat.S_ISREG(before.st_mode):
                raise SourceIDError("nonregular input: " + name)
            chunks = []
            while chunk := os.read(fd, 1024 * 1024):
                chunks.append(chunk)
            after = os.fstat(fd)
            identity = lambda s: (s.st_dev, s.st_ino, s.st_size, s.st_mtime_ns, s.st_ctime_ns)
            if identity(before) != identity(after) or identity(after) != identity(path.stat(follow_symlinks=False)):
                raise SourceIDError("input changed during snapshot: " + name)
            return b"".join(chunks)
        finally:
            os.close(fd)
    except OSError as exc:
        raise SourceIDError("input unavailable: " + name) from exc


def _literal_settings(blob: bytes) -> tuple[tuple[str, ...], dict[str, str], int]:
    try:
        tree = ast.parse(blob)
        found = {}
        for node in tree.body:
            if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name):
                name = node.targets[0].id
                if name in ("PUBLIC_REPLACEMENTS", "PUBLIC_SKIPPED_PATCHES", "NV_FRAGMENTS_SIZE"):
                    if name in found:
                        raise SourceIDError("duplicate public build setting")
                    if name == "NV_FRAGMENTS_SIZE" and isinstance(node.value, ast.BinOp) and isinstance(node.value.op, ast.Sub):
                        value = ast.literal_eval(node.value.left) - ast.literal_eval(node.value.right)
                    else:
                        value = ast.literal_eval(node.value)
                    found[name] = value
        replacements = found["PUBLIC_REPLACEMENTS"]
        skipped = found["PUBLIC_SKIPPED_PATCHES"]
        size = found["NV_FRAGMENTS_SIZE"]
        if (type(replacements) is not tuple or not replacements or len(replacements) != len(set(replacements))
                or any(type(n) is not str or "/" in n or _name(n) != n for n in replacements)
                or type(skipped) is not dict or any(type(k) is not str or type(v) is not str
                    or _name(k) != k or "/" in k or v not in replacements for k, v in skipped.items())
                or type(size) is not int or size < 16 or size > 1024 * 1024):
            raise SourceIDError("invalid public build settings")
        return replacements, skipped, size
    except (KeyError, SyntaxError, ValueError, TypeError) as exc:
        raise SourceIDError("public build settings are not explicit literals") from exc


def _port_input(name: str) -> bool:
    parts = PurePosixPath(name).parts
    return (name.startswith("port/") and not name.startswith(NONPRODUCTION_PORT_ROOTS)
            and "tests" not in parts and "docs" not in parts
            and (PurePosixPath(name).suffix.lower() in SOURCE_SUFFIXES or parts[-1] == "flags.txt")
            and name != "port/platform/common/nvtristrip_bridge.cpp")


def _source_input(name: str) -> bool:
    return (name.startswith(("src/", "linklib/")) or _port_input(name)
            or name.startswith(("port/patches/", "port/deps-patches/")) and name.endswith(".patch"))


def _check_untracked(root: Path, tracked: frozenset[str]) -> None:
    # Include ignored candidates: an ignored *.cpp is still consumed by production globs. Never traverse links.
    for rel in ("src", "linklib", "port"):
        top = root / rel
        if top.is_symlink():
            raise SourceIDError("symlink in production tree: " + rel)
        if not top.exists():
            continue
        for here, dirs, files in os.walk(top, followlinks=False):
            dirs[:] = [d for d in dirs if d not in ("tests", "__pycache__")
                       and not ((Path(here) / d).relative_to(root).as_posix() + "/").startswith(NONPRODUCTION_PORT_ROOTS)
                       and not (Path(here) / d).is_symlink()]
            for name in files:
                candidate = (Path(here) / name).relative_to(root).as_posix()
                if _source_input(candidate) and candidate not in tracked:
                    raise SourceIDError("untracked production input: " + candidate)
            # A symlink directory may introduce arbitrary headers/sources through include search or globs.
            for child in Path(here).iterdir():
                if child.is_symlink() and child.name not in ("tests", "__pycache__"):
                    raise SourceIDError("symlink in production tree: " + child.relative_to(root).as_posix())


def _data_pins(blob: bytes) -> dict[str, str]:
    out = {}
    try:
        for line in blob.decode("utf-8").splitlines():
            if not line.strip() or line.startswith("#"):
                continue
            match = PIN.fullmatch(line)
            if match is None:
                raise SourceIDError("invalid generated-data pin")
            digest, name = match.groups()
            _name(name)
            if not name.startswith("data/") or name.casefold() in out:
                raise SourceIDError("ambiguous generated-data pin")
            out[name.casefold()] = digest
    except UnicodeError as exc:
        raise SourceIDError("invalid generated-data pins") from exc
    if not out:
        raise SourceIDError("generated-data pins are empty")
    return out


def _includes(logical: str, blob: bytes):
    text = blob.decode("latin-1")
    if logical.startswith("port/patches/"):
        here = None
        for line in text.splitlines():
            if line.startswith("+++ "):
                path = line[4:].split("\t", 1)[0].split(" ", 1)[0]
                here = "src/" + path.split("/", 1)[1] if "/" in path and path != "/dev/null" else None
            elif here and line.startswith("+") and not line.startswith("+++"):
                for name in INCLUDE.findall(line[1:]):
                    yield here, name, '"' in line
        return
    if PurePosixPath(logical).suffix.lower() not in SOURCE_SUFFIXES:
        return
    for name in QUOTED_INCLUDE.findall(text):
        yield logical, name, True
    for name in INCLUDE.findall(text):
        if name not in QUOTED_INCLUDE.findall(text):
            yield logical, name, False
    if logical.endswith(".rc"):
        for name in RESOURCE_FILE.findall(text):
            yield logical, name, True


def _check_include_closure(root: Path, entries: dict[str, bytes], sources: dict[str, str],
                           tracked: frozenset[str], pins: dict[str, str]) -> None:
    # Bound every existing local literal include. Unavailable external/console-only declarations remain
    # source text, not claims that those inactive paths compiled. The compiler still validates actual closure.
    by_case = {name.casefold(): name for name in tracked}
    effective = {name.casefold(): name for name in entries}
    search = ("src", "src/MissionScript", "port/d3dx/include", "port/include", "port/game",
              "port/include/linux", "port/include/windows")
    queue = list(entries)
    for logical in queue:
        for here, given, quoted in _includes(logical, entries[logical]):
            name = given.replace("\\", "/")
            if name.casefold().startswith("data/"):
                if name.casefold() not in pins and name.casefold() != "data/nvshaderfragments.inc":
                    raise SourceIDError("unpinned generated include")
                continue
            if name.startswith("/") or re.match(r"^[A-Za-z]:", name):
                raise SourceIDError("absolute include is outside the profile")
            bases = ((posixpath.dirname(here),) if quoted else ()) + search
            for base in bases:
                candidate = posixpath.normpath(base + "/" + name)
                if candidate.startswith("../"):
                    raise SourceIDError("include escapes source root")
                if candidate.casefold().startswith("src/data/"):
                    generated = candidate[4:].casefold()
                    if generated not in pins and generated != "data/nvshaderfragments.inc":
                        raise SourceIDError("unpinned generated include")
                    break
                if candidate.casefold() in effective:
                    break
                indexed = by_case.get(candidate.casefold())
                actual = root / candidate
                if indexed is None and not actual.exists() and not actual.is_symlink():
                    continue
                if indexed is None:
                    raise SourceIDError("untracked local include")
                if indexed in PUBLIC_OMISSIONS or "/tests/" in indexed:
                    raise SourceIDError("include reaches a nonproduction input")
                entries[indexed] = _read(root, indexed)
                sources[indexed] = indexed
                effective[indexed.casefold()] = indexed
                queue.append(indexed)
                break


def source_entries(root: Path, *, with_ice=False) -> dict[str, bytes]:
    """Read the fixed profile's logical-name/raw-byte snapshot; never returns retail data or host names."""
    root = _root(root)
    tracked = _index(root)
    _check_untracked(root, tracked)
    wanted_recipes = RECIPES + (ICE_RECIPES if with_ice else ())
    for name in wanted_recipes:
        if name not in tracked:
            raise SourceIDError("required recipe is not indexed: " + name)
    recipes = {name: _read(root, name) for name in wanted_recipes}
    replacements, skipped, nv_size = _literal_settings(recipes["port/build.py"])
    excluded = set(PUBLIC_OMISSIONS) | {"src/" + name for name in replacements}
    excluded.add("port/platform/common/nvtristrip_bridge.cpp")
    excluded |= {"port/patches/" + name for name in skipped}
    sources = {name: name for name in tracked if _source_input(name) and name not in excluded}
    for name in replacements:
        source = "port/platform/public/" + name
        if source not in tracked:
            raise SourceIDError("missing public replacement: " + name)
        sources["src/" + name] = source
    if not any(name.startswith("src/") and name.endswith(".cpp") for name in sources):
        raise SourceIDError("no game source in public profile")
    entries = {logical: _read(root, actual) for logical, actual in sources.items()}
    entries.update(recipes)
    pins = _data_pins(entries["config/game-data.sha256"])
    _check_include_closure(root, entries, sources, tracked, pins)
    # Hash the actual deterministic zero-filled include, not any retained decompilation extraction.
    entries["@generated/data/NVShaderFragments.inc"] = (",\n".join(
        ", ".join(["0x00"] * 16) for _ in range(nv_size // 16)) + "\n").encode("ascii")
    # Catch replacement/deletion/index edits across the snapshot. This is no claim of an immutable filesystem.
    if tracked != _index(root):
        raise SourceIDError("source index changed during snapshot")
    for logical, actual in sources.items():
        if _read(root, actual) != entries[logical]:
            raise SourceIDError("source changed during snapshot")
    for name, blob in recipes.items():
        if _read(root, name) != blob:
            raise SourceIDError("recipe changed during snapshot")
    if with_ice:
        entries["@profile/optional-ice"] = b"public-i686-linux-and-windows-ice-v1\0"
    return entries


def digest_entries(entries: dict[str, bytes]) -> str:
    """The versioned wire-independent transcript. Logical path ordering is UTF-8 byte ordering."""
    digest = hashlib.sha256(DOMAIN + PROFILE + struct.pack(">Q", len(entries)))
    for name in sorted(entries, key=lambda s: s.encode("utf-8")):
        path = name.encode("utf-8")
        data = entries[name]
        digest.update(struct.pack(">Q", len(path)) + path + struct.pack(">Q", len(data)) + hashlib.sha256(data).digest())
    return digest.hexdigest()


def compute_source_id(root: Path, *, with_ice=False) -> str:
    """Return one lowercase SHA-256 identity for either target of the declared public source profile."""
    return digest_entries(source_entries(root, with_ice=with_ice))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", nargs="?", type=Path, default=Path(__file__).absolute().parents[2])
    parser.add_argument("--with-ice", action="store_true", help="bind the optional ICE client recipe and pin as a distinct profile")
    args = parser.parse_args()
    try:
        print(compute_source_id(args.root, with_ice=args.with_ice))
    except (SourceIDError, OSError):
        print("network source identity refused: incomplete or unsupported source profile", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
