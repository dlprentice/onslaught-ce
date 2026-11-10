#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compose reviewed CE, Companion and optional player-tool exports into one offline bundle.

    python3 port/tools/compose_bundle.py --recipe recipe.json --out new-bundle.tar.gz

Operators must supply separately reviewed redistributable program exports with all notices.
Do not select a game install, user folder or backup. This tool cannot prove the absence
of retail or private content or establish repository provenance.

The recipe has schema=1, version, platform (linux/windows), epoch (SOURCE_DATE_EPOCH), and
components. Each component has id (ce/companion/tools), platform, version, root (relative
to the recipe or absolute), sourceRepository (public HTTPS URL), sourceCommit (full Git
SHA), entrypoint, licenses (relative filenames), and files. Files maps every relative
filename to {sha256, size, executable}. No files are discovered for inclusion. Companion's
existing package-sha256.json must agree with that selection and platform.

This checks inventory consistency and layout, not redistribution rights, source completeness,
runtime compatibility or platform acceptance. The output is new, with independent ce/,
companion/ and tools/ roots. It neither installs nor updates anything and runs no component
or subprocess.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import tarfile
import tempfile
import time
from urllib.parse import urlsplit
import zipfile


VERSION = re.compile(r"[0-9]+\.[0-9]+\.[0-9]+(?:[-+][A-Za-z0-9.-]+)?\Z")
SHA256 = re.compile(r"[0-9a-f]{64}\Z")
COMMIT = re.compile(r"[0-9a-f]{40}(?:[0-9a-f]{24})?\Z")
ENTRYPOINTS = {
    "linux": {"ce": "onslaught-ce", "companion": "OnslaughtCompanion.x86_64"},
    "windows": {"ce": "onslaught-ce.exe", "companion": "OnslaughtCompanion.exe"},
}
CE_NOTICES = {"COPYING", "NOTICE.txt", "THIRD-PARTY-NOTICES.txt"}
# Include console aliases and the superscript port digits Windows treats as devices.
# https://docs.python.org/3/library/os.path.html#os.path.isreserved
# https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file
RESERVED = {"con", "prn", "aux", "nul", "conin$", "conout$"} | {
    f"{p}{n}" for p in ("com", "lpt") for n in "123456789¹²³"
}
PRIVATE = {".git", ".hg", ".svn", "save", "saves", "savegames", "ce.ini"}
CHUNK = 1024 * 1024


def fail(message):
    raise ValueError(message)


def exact_keys(value, keys, label):
    if not isinstance(value, dict) or set(value) != set(keys.split()):
        fail(f"{label}: expected fields {keys}")


def relative_name(value):
    if not isinstance(value, str) or not value or len(value) > 512:
        fail("invalid relative filename")
    path = PurePosixPath(value)
    if path == PurePosixPath(".") or path.is_absolute() or str(path) != value or any(part in (".", "..") for part in path.parts):
        fail("filenames must be normalized relative paths")
    for part in path.parts:
        if (any(ord(c) < 32 or ord(c) == 127 or c in '\\:*?"<>|' for c in part) or part.endswith((" ", "."))
                or part.split(".", 1)[0].rstrip(" ").casefold() in RESERVED):
            fail("filename is unsafe on a supported platform")
        if part.casefold() in PRIVATE or part.casefold().startswith((".env", "local-")) or part.casefold().endswith(".bes"):
            fail("game-user, save, backup or private input is not a program export")
    return value


def checked_version(value):
    if not isinstance(value, str) or len(value) > 64 or not VERSION.fullmatch(value):
        fail("version must be a semantic-version-shaped filename-safe value")
    return value


def read_json(path):
    if path.stat().st_size > 8 * CHUNK:
        fail("JSON input exceeds 8 MiB")
    # Duplicate fields could otherwise hide a different file or component selection.
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                fail("duplicate JSON field")
            result[key] = value
        return result
    return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique)


def fingerprint(info):
    return info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns, info.st_ctime_ns, info.st_mode


def selected_tree(root, expected):
    """Reject links, special files, extra files/empty directories and case collisions."""
    if root.is_symlink() or not root.is_dir():
        fail("component root must be an ordinary directory")
    found, directories, names = {}, set(), set()
    for parent, dirs, files in os.walk(root, followlinks=False):
        for name in dirs + files:
            path = Path(parent) / name
            rel = relative_name(path.relative_to(root).as_posix())
            if rel.casefold() in names:
                fail("component has a case-insensitive path conflict")
            names.add(rel.casefold())
            info = path.lstat()
            if stat.S_ISDIR(info.st_mode):
                directories.add(rel)
            elif stat.S_ISREG(info.st_mode):
                found[rel] = fingerprint(info)
            else:
                fail("component contains a link or special file")
    parents = {p.as_posix() for name in expected for p in PurePosixPath(name).parents if str(p) != "."}
    if set(found) != set(expected) or directories != parents:
        fail("component tree differs from the explicit file inventory")
    for name, info in found.items():
        if info[2] != expected[name]["size"]:
            fail("component file size differs from the explicit inventory")
    return found


def load_recipe(path):
    recipe = read_json(path)
    exact_keys(recipe, "schema version platform epoch components", "recipe")
    if type(recipe["schema"]) is not int or recipe["schema"] != 1:
        fail("unsupported recipe schema")
    checked_version(recipe["version"])
    platform = recipe["platform"]
    if not isinstance(platform, str) or platform not in ENTRYPOINTS:
        fail("platform must be linux or windows")
    # ZIP dates start in 1980; gzip MTIME is an unsigned 32-bit Unix timestamp.
    # https://www.rfc-editor.org/rfc/rfc1952.html#section-2.3.1
    if type(recipe["epoch"]) is not int or not 315532800 <= recipe["epoch"] <= 2 ** 32 - 1:
        fail("epoch must fit ZIP dates and gzip's unsigned 32-bit timestamp (315532800 through 4294967295)")
    if not isinstance(recipe["components"], list):
        fail("components must be a list")
    components = {}
    for component in recipe["components"]:
        exact_keys(component, "id platform version root sourceRepository sourceCommit entrypoint licenses files", "component")
        identity = component["id"]
        if identity not in ("ce", "companion", "tools") or identity in components:
            fail("component IDs must be distinct: ce, companion and optionally tools")
        if component["platform"] != platform:
            fail("component platform differs from the bundle platform")
        checked_version(component["version"])
        if not isinstance(component["sourceRepository"], str):
            fail("sourceRepository must be an HTTPS URL")
        source = urlsplit(component["sourceRepository"])
        if (source.scheme != "https" or not source.hostname or source.username or source.password
                or source.query or source.fragment):
            fail("sourceRepository must be a public HTTPS URL without credentials, query or fragment")
        if not isinstance(component["sourceCommit"], str) or not COMMIT.fullmatch(component["sourceCommit"]):
            fail("sourceCommit must be a full Git SHA")
        files = component["files"]
        if not isinstance(files, dict) or not files:
            fail("each component needs an explicit nonempty file inventory")
        for name, row in files.items():
            relative_name(name)
            exact_keys(row, "sha256 size executable", "file")
            if not isinstance(row["sha256"], str) or not SHA256.fullmatch(row["sha256"]):
                fail("file SHA-256 must be lowercase hexadecimal")
            if type(row["size"]) is not int or row["size"] < 0 or type(row["executable"]) is not bool:
                fail("file size/executable metadata is invalid")
        entrypoint = relative_name(component["entrypoint"])
        if entrypoint not in files or not files[entrypoint]["executable"]:
            fail("entrypoint must name an inventoried executable")
        if identity in ENTRYPOINTS[platform] and entrypoint != ENTRYPOINTS[platform][identity]:
            fail("component entrypoint differs from the platform's exported name")
        licenses = component["licenses"]
        if (not isinstance(licenses, list) or not licenses or any(not isinstance(n, str) for n in licenses)
                or len(set(licenses)) != len(licenses) or any(n not in files or not files[n]["size"] for n in licenses)):
            fail("licenses must name distinct, nonempty inventoried files")
        if "README.txt" not in files:
            fail("each component must retain its README.txt")
        if identity == "ce":
            programs = {"bea.exe"} if platform == "windows" else {"bea", "onslaught-ce-launch", "preflight.sh"}
            required = CE_NOTICES | programs
            if not required <= files.keys() or not CE_NOTICES <= set(licenses):
                fail("CE export must retain its programs and license notices")
            if platform == "linux" and any(not files[name]["executable"] for name in programs):
                fail("Linux CE programs must be executable")
        if identity == "companion":
            required = {PurePosixPath(entrypoint).with_suffix(".pck").as_posix(), "LICENSE.txt", "LORE-LICENSE.txt"}
            if not required <= files.keys() or not {"LICENSE.txt", "LORE-LICENSE.txt"} <= set(licenses):
                fail("Companion export must retain its PCK and application/lore licenses")
        if not isinstance(component["root"], str) or not component["root"]:
            fail("root must explicitly select an export directory")
        root = Path(component["root"])
        if not root.is_absolute():
            root = path.parent / root
        # Check before resolve too: selecting a linked root is not selecting an ordinary export.
        if root.is_symlink():
            fail("linked component root is not allowed")
        root = root.resolve(strict=True)
        snapshot = selected_tree(root, files)
        if identity == "companion":
            if "package-sha256.json" not in files:
                fail("Companion export must retain package-sha256.json")
            inventory = read_json(root / "package-sha256.json")
            expected = {n: r["sha256"] for n, r in files.items() if n != "package-sha256.json"}
            if (not isinstance(inventory, dict) or inventory.get("platform") != platform
                    or inventory.get("files") != expected or not inventory.get("godotVersion")
                    or not inventory.get("dotnetRuntime")):
                fail("Companion export inventory or platform differs from the selection")
        components[identity] = dict(component, root=root, snapshot=snapshot)
    if not {"ce", "companion"} <= components.keys():
        fail("a bundle requires CE and Companion")
    roots = [c["root"] for c in components.values()]
    if any(a == b or a in b.parents or b in a.parents for i, a in enumerate(roots) for b in roots[i + 1:]):
        fail("component roots must be separate directories")
    return recipe, components


def encoded(value):
    return (json.dumps(value, sort_keys=True, indent=2) + "\n").encode("utf-8")


def metadata(recipe, components):
    manifest = {k: recipe[k] for k in ("schema", "version", "platform", "epoch")}
    manifest["inventoryScope"] = "Initial bundle only; independent component updates can change these files."
    manifest["components"] = [dict({k: c[k] for k in ("id", "platform", "version", "sourceRepository", "sourceCommit",
                                                       "entrypoint", "licenses", "files")}, directory=identity)
                              for identity, c in sorted(components.items())]
    guide = [f"Onslaught {recipe['version']} ({recipe['platform']})", "",
             "Extract this bundle into a new empty folder outside your game, user and backup folders.",
             "Keep your game data, careers, settings and backups in their existing separate folders.",
             "Read each component's README.txt before starting it.", ""]
    for identity, c in sorted(components.items()):
        guide.append(f"{identity.capitalize()}: {identity}/{c['entrypoint']} (version {c['version']})")
    guide += ["", "The CE launcher owns CE updates inside ce/. Replace Companion and tools independently",
              "inside their own folders, after closing them. Keep those folders beside ce/, not inside it.",
              "Before writing game data or careers, use the component's informed choice and verified backup.",
              "This bundle manifest records its initial contents; later component updates can change them.",
              "Assembly and hash checks do not establish platform or runtime acceptance.", ""]
    return {"bundle-manifest.json": encoded(manifest), "README.txt": "\n".join(guide).encode("utf-8")}


class CheckedReader:
    def __init__(self, stream):
        self.stream, self.digest, self.size = stream, hashlib.sha256(), 0

    def read(self, size=-1):
        data = self.stream.read(size)
        self.digest.update(data)
        self.size += len(data)
        return data


def digest(stream):
    reader = CheckedReader(stream)
    while reader.read(CHUNK):
        pass
    return reader.digest.hexdigest(), reader.size


def archive_members(path, platform):
    """Read back every member, including type, mode, size and contents; never extract."""
    if platform == "windows":
        with zipfile.ZipFile(path) as archive:
            for member in archive.infolist():
                kind = (member.external_attr >> 16) & 0o170000
                if member.is_dir() or kind != stat.S_IFREG:
                    fail("output contains a non-regular member")
                with archive.open(member) as stream:
                    yield member.filename, (member.external_attr >> 16) & 0o777, digest(stream)
    else:
        with tarfile.open(path, "r:gz") as archive:
            for member in archive:
                if not member.isfile():
                    fail("output contains a non-regular member")
                with archive.extractfile(member) as stream:
                    yield member.name, member.mode, digest(stream)


def compose(recipe_path: Path, output: Path):
    recipe, components = load_recipe(recipe_path)
    suffix = ".zip" if recipe["platform"] == "windows" else ".tar.gz"
    if not output.name.endswith(suffix):
        fail(f"this platform's output must end with {suffix}")
    if output.exists() or output.is_symlink():
        fail("output already exists; select a new output filename")
    output = output.parent.resolve(strict=True) / output.name
    if any(c["root"] in output.parents for c in components.values()):
        fail("output must be outside all selected component roots")
    top = f"onslaught-{recipe['version']}-{recipe['platform']}"
    members = {f"{top}/{name}": (data, None, {"size": len(data), "sha256": hashlib.sha256(data).hexdigest(),
                                            "executable": False}) for name, data in metadata(recipe, components).items()}
    for identity, c in components.items():
        for name, row in c["files"].items():
            members[f"{top}/{identity}/{name}"] = (c["root"] / name, c["snapshot"][name], row)
    expected = {name: (0o755 if row["executable"] else 0o644, (row["sha256"], row["size"]))
                for name, (_, _, row) in members.items()}
    with tempfile.NamedTemporaryFile(prefix=".onslaught-bundle-", suffix=".partial", dir=output.parent) as temporary:
        with _writers(temporary, recipe) as write:
            for name, (source, snapshot, row) in sorted(members.items()):
                if isinstance(source, bytes):
                    write(name, io.BytesIO(source), row)
                else:
                    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
                    with os.fdopen(os.open(source, flags), "rb") as stream:
                        if fingerprint(os.fstat(stream.fileno())) != snapshot:
                            fail("selected component changed before assembly")
                        reader = CheckedReader(stream)
                        write(name, reader, row)
                        if reader.read(1) or (reader.digest.hexdigest(), reader.size) != (row["sha256"], row["size"]):
                            fail("selected component SHA-256 differs from its inventory")
                        if fingerprint(os.fstat(stream.fileno())) != snapshot:
                            fail("selected component changed during assembly")
        for c in components.values():
            if selected_tree(c["root"], c["files"]) != c["snapshot"]:
                fail("selected component tree changed during assembly")
        temporary.flush()
        seen = {}
        for name, mode, identity in archive_members(temporary.name, recipe["platform"]):
            if name in seen:
                fail("output has a duplicate member")
            seen[name] = mode, identity
        if seen != expected:
            fail("output read-back differs from the selected component inventories")
        # Linking publishes only a verified new file and cannot replace a racing existing target.
        os.link(temporary.name, output)
    with output.open("rb") as stream:
        sha, size = digest(stream)
    return {"sha256": sha, "size": size, "files": len(members)}


def _writers(temporary, recipe):
    from contextlib import contextmanager

    @contextmanager
    def opened():
        if recipe["platform"] == "windows":
            with zipfile.ZipFile(temporary, "w", zipfile.ZIP_DEFLATED) as archive:
                def write(name, stream, row):
                    info = zipfile.ZipInfo(name, time.gmtime(recipe["epoch"])[:6])
                    info.create_system, info.compress_type = 3, zipfile.ZIP_DEFLATED
                    info.external_attr = (stat.S_IFREG | (0o755 if row["executable"] else 0o644)) << 16
                    with archive.open(info, "w") as target:
                        while data := stream.read(CHUNK):
                            target.write(data)
                yield write
        else:
            with gzip.GzipFile(filename="", mode="wb", fileobj=temporary, mtime=recipe["epoch"]) as compressed, \
                    tarfile.open(fileobj=compressed, mode="w", format=tarfile.GNU_FORMAT) as archive:
                def write(name, stream, row):
                    info = tarfile.TarInfo(name)
                    info.size, info.mode, info.mtime = row["size"], 0o755 if row["executable"] else 0o644, recipe["epoch"]
                    info.uname = info.gname = "root"
                    archive.addfile(info, stream)
                yield write
    return opened()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--recipe", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    try:
        result = compose(args.recipe, args.out)
    except (ValueError, OSError, EOFError, tarfile.TarError, zipfile.BadZipFile) as error:
        parser.exit(1, f"bundle: {error}\n")
    print(f"{result['sha256']}  {args.out.name}  ({result['size']} bytes, {result['files']} files)")


if __name__ == "__main__":
    main()
