#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Pinned source and notice inputs for explicit native-signaling packages.

Only ``materialize`` downloads anything: the exact matching source artifacts
from the official Steam Runtime snapshot. Unmodified notices come from the
locked SDK archive retained by the build. Packaging and ``verify`` are offline.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import tarfile
import tempfile
import urllib.parse
import urllib.request

import deps


ROOT = Path(__file__).resolve().parents[2]
PINS = Path(__file__).with_name("package_signaling_sources.json")
SOURCES = ROOT / "local-data/native-signaling-sources"
NOTICES = ROOT / "local-data/native-signaling-notices"
REVIEWED_INPUTS = "0959f5d1614bc761fcba8087548ba23f3146ae08aa55ee33ace7443a3af924b6"
MARKER = b"CE-NATIVE-SIGNALING-V1"
MAX_SOURCE_BYTES = 64 * 1024 * 1024
SYSTEM_LIBRARIES = frozenset((
    "libc.so.6", "libm.so.6", "libdl.so.2", "libpthread.so.0",
    "librt.so.1", "libresolv.so.2", "ld-linux.so.2",
))


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def receipt_fields(target: str) -> dict:
    if target not in ("linux", "windows"):
        raise ValueError("unknown native-signaling package target")
    return dict(enabled=True, marker=MARKER.decode(),
                https_backend="winhttp" if target == "windows" else "libcurl-gnutls")


def check_receipt(value: object, target: str) -> None:
    expected = receipt_fields(target)
    if (not isinstance(value, dict) or set(value) != set(expected)
            or value.get("enabled") is not True or value != expected):
        raise ValueError("native-signaling receipt does not match the selected target")


def safe_relative(value: str) -> PurePosixPath:
    if not isinstance(value, str) or not value or "\\" in value:
        raise ValueError("invalid relative native-signaling input path")
    path = PurePosixPath(value)
    if path.is_absolute() or ".." in path.parts or value != path.as_posix():
        raise ValueError("invalid relative native-signaling input path")
    return path


def read_pinned(path: Path, row: dict) -> bytes:
    expected = row["bytes"]
    fd = os.open(path, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW)
    with os.fdopen(fd, "rb") as stream:
        before = os.fstat(stream.fileno())
        if not stat.S_ISREG(before.st_mode) or before.st_size != expected:
            raise ValueError(f"pinned input size/type differs: {path.name}")
        data = stream.read(expected + 1)
        after = os.fstat(stream.fileno())
    if ((before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns)
            != (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns)
            or len(data) != expected or digest(data) != row["sha256"]):
        raise ValueError(f"pinned input changed or has the wrong hash: {path.name}")
    return data


def load_manifest() -> dict:
    raw = PINS.read_bytes()
    if digest(raw) != REVIEWED_INPUTS:
        raise ValueError("native-signaling source/notice pins differ from the reviewed inputs")
    pins = json.loads(raw)
    if type(pins.get("schema")) is not int or pins["schema"] != 1:
        raise ValueError("unsupported native-signaling input schema")
    sdk = pins["sdk"]
    locked = deps.lock()["sysroot-sniper"]
    if (type(sdk.get("bytes")) is not int or sdk["bytes"] <= 0
            or sdk != dict(name="sysroot-sniper", version=locked["version"],
                           sha256=locked["sha"], url=locked["url"], file=locked["file"].name,
                           licence=locked["licence"], bytes=sdk["bytes"])):
        raise ValueError("native-signaling inputs differ from the locked SDK")
    snapshot = sdk["url"].rsplit("/", 1)[0]
    metadata = pins["source_metadata"]
    if (urllib.parse.urlparse(metadata["url"]).scheme != "https"
            or metadata["url"].rsplit("/", 1)[0] != snapshot):
        raise ValueError("matching sources must use the SDK's official snapshot")
    artifacts = set()
    total = 0
    for package in pins["source_packages"]:
        if not re.fullmatch(r"[a-z0-9][a-z0-9+.-]*", package["package"]):
            raise ValueError("invalid source package name")
        if not package["version"] or not package["files"]:
            raise ValueError("incomplete matching source package")
        for row in package["files"]:
            name = row["filename"]
            if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9+_.~-]*", name):
                raise ValueError("invalid matching-source filename")
            if (name in artifacts or type(row["bytes"]) is not int or row["bytes"] <= 0
                    or not re.fullmatch(r"[0-9a-f]{64}", row["sha256"])
                    or row["url"] != snapshot + "/sources/" + urllib.parse.quote(name, safe="~+_.-")):
                raise ValueError("invalid matching-source pin")
            artifacts.add(name)
            total += row["bytes"]
    if not artifacts or total > MAX_SOURCE_BYTES:
        raise ValueError("matching source inputs exceed the bounded package budget")
    sonames = set()
    for row in pins["libraries"]:
        safe_relative(row["path"])
        if safe_relative(row["soname"]).name != row["soname"] or row["soname"] in sonames:
            raise ValueError("invalid or duplicated SDK library name")
        sonames.add(row["soname"])
    if not sonames:
        raise ValueError("native-signaling SDK closure is empty")
    for row in pins["libraries"]:
        if set(row["needed"]) - sonames - SYSTEM_LIBRARIES:
            raise ValueError("native-signaling SDK closure is incomplete")
    outputs = set()
    for row in pins["notices"]:
        if set(row) != {"package", "version", "sdk_path", "archive_member", "output",
                        "licence_reference", "bytes", "sha256"}:
            raise ValueError("notices must be pinned materialized inputs")
        safe_relative(row["output"])
        safe_relative(row["archive_member"])
        safe_relative(row["sdk_path"])
        if (not row["output"].startswith("licenses/native-signaling/")
                or row["licence_reference"] != row["sdk_path"]
                or row["output"] in outputs or type(row["bytes"]) is not int
                or not 0 < row["bytes"] <= 1024 * 1024
                or not re.fullmatch(r"[0-9a-f]{64}", row["sha256"])):
            raise ValueError("invalid native-signaling notice pin")
        outputs.add(row["output"])
    if not outputs:
        raise ValueError("native-signaling notices are missing")
    return pins


def source_rows(pins: dict):
    for package in pins["source_packages"]:
        for row in package["files"]:
            yield package["package"], row


def cache_path(root: Path, relative: str) -> Path:
    path = root / str(safe_relative(relative))
    for parent in (path.parent, *path.parent.parents):
        try:
            mode = parent.lstat().st_mode
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(mode) or not stat.S_ISDIR(mode):
            raise ValueError("matching-source cache must use ordinary directories")
    return path


def source_path(package: str, row: dict) -> Path:
    return cache_path(SOURCES, package + "/" + row["filename"])


def install_notice(path: Path, data: bytes, row: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix="notice-", delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        try:
            os.link(temporary, path)
        except FileExistsError:
            read_pinned(path, row)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def sdk_notices(pins: dict, rows: list[dict]) -> dict[str, bytes]:
    """Read only exact regular members from the already downloaded public SDK."""
    sdk = pins["sdk"]
    archive = deps.DOWNLOADS / sdk["file"]
    wanted = {}
    for row in rows:
        wanted.setdefault(row["archive_member"], []).append(row)
    found = {}
    fd = os.open(archive, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW)
    with os.fdopen(fd, "rb") as stream:
        before = os.fstat(stream.fileno())
        if not stat.S_ISREG(before.st_mode) or before.st_size != sdk["bytes"]:
            raise ValueError("cached SDK archive differs from the notice pins")
        hashed = hashlib.sha256()
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            hashed.update(chunk)
        if hashed.hexdigest() != sdk["sha256"]:
            raise ValueError("cached SDK archive has the wrong hash")
        stream.seek(0)
        try:
            with tarfile.open(fileobj=stream, mode="r|*") as tar:
                for member in tar:
                    name = PurePosixPath(member.name).as_posix()
                    if name not in wanted:
                        continue
                    selected = wanted.pop(name)
                    if not member.isreg() or any(member.size != row["bytes"] for row in selected):
                        raise ValueError("SDK notice member size/type differs from its pin")
                    data = tar.extractfile(member).read()
                    for row in selected:
                        if len(data) != row["bytes"] or digest(data) != row["sha256"]:
                            raise ValueError("SDK notice member has the wrong hash")
                        found[row["output"]] = data
                    if not wanted:
                        break
        except tarfile.TarError as error:
            raise ValueError("invalid cached SDK notice archive") from error
        after = os.fstat(stream.fileno())
        if ((before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns)
                != (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns)):
            raise ValueError("SDK notice archive changed during preparation")
    if wanted:
        raise ValueError("pinned SDK notice members are missing")
    return found


def materialize_notices(pins: dict) -> dict[str, bytes]:
    files = {}
    missing = []
    for row in pins["notices"]:
        path = cache_path(NOTICES, row["output"])
        if path.exists() or path.is_symlink():
            files[row["output"]] = read_pinned(path, row)
        else:
            missing.append(row)
    if missing:
        prepared = sdk_notices(pins, missing)
        for row in missing:
            data = prepared[row["output"]]
            install_notice(cache_path(NOTICES, row["output"]), data, row)
            files[row["output"]] = data
    return files


def materialize() -> tuple[int, int]:
    pins = load_manifest()
    count = total = 0
    for package, row in source_rows(pins):
        destination = source_path(package, row)
        destination.parent.mkdir(parents=True, exist_ok=True)
        if destination.exists() or destination.is_symlink():
            read_pinned(destination, row)
        else:
            temporary = None
            try:
                with tempfile.NamedTemporaryFile(dir=destination.parent, prefix="download-", delete=False) as stream:
                    temporary = Path(stream.name)
                    size = 0
                    hashed = hashlib.sha256()
                    with urllib.request.urlopen(row["url"], timeout=60) as response:
                        final = urllib.parse.urlparse(response.geturl())
                        if final.scheme != "https" or final.netloc != urllib.parse.urlparse(row["url"]).netloc:
                            raise ValueError("matching-source download left its official host")
                        while chunk := response.read(1024 * 1024):
                            size += len(chunk)
                            if size > row["bytes"]:
                                raise ValueError("matching-source download exceeds its pin")
                            hashed.update(chunk)
                            stream.write(chunk)
                    if size != row["bytes"] or hashed.hexdigest() != row["sha256"]:
                        raise ValueError("matching-source download differs from its pin")
                    stream.flush()
                    os.fsync(stream.fileno())
                try:
                    os.link(temporary, destination)
                except FileExistsError:
                    read_pinned(destination, row)
            finally:
                if temporary is not None:
                    temporary.unlink(missing_ok=True)
        count += 1
        total += row["bytes"]
    return count, total


def prepare() -> dict:
    """Snapshot verified payload bytes; never download during package assembly."""
    pins = load_manifest()
    files = materialize_notices(pins)
    for package, row in source_rows(pins):
        path = "source/native-signaling/" + package + "/" + row["filename"]
        files[path] = read_pinned(source_path(package, row), row)
    libraries = {}
    paths = {}
    for row in pins["libraries"]:
        path = deps.SYSROOT / row["path"]
        libraries[row["soname"]] = read_pinned(path, row)
        paths[row["soname"]] = path
    provenance = dict(pins)
    files["source/native-signaling/SOURCES.json"] = (json.dumps(provenance, indent=2, sort_keys=True) + "\n").encode()
    notice = ("\nNative signaling on Linux uses the locked Steam Runtime SDK libraries listed in\n"
              "source/native-signaling/SOURCES.json. Their exact matching source artifacts are\n"
              "included under source/native-signaling/; copyright and referenced license texts\n"
              "are under licenses/native-signaling/. Shared libraries can be replaced in lib/.\n"
              "Rebuild with --with-native-signaling in addition to the public/network/ICE build flags.\n"
              "In the source checkout, prepare the matching package inputs with:\n"
              "  python port/tools/package_signaling_inputs.py materialize\n"
              "Then assemble with:\n"
              "  python port/tools/package.py VERSION --with-ice --with-native-signaling\n").encode()
    files["NATIVE-SIGNALING-SOURCE-NOTICE.txt"] = notice.lstrip()
    windows_notice = ("\nNative HTTPS signaling on Windows uses the operating system's WinHTTP.\n"
                      "No additional TLS library is bundled; the application and ICE source notices apply.\n").encode()
    return {"linux": dict(files=files, libraries=libraries, paths=paths, notice=notice),
            "windows": dict(files={"NATIVE-SIGNALING-SOURCE-NOTICE.txt": windows_notice.lstrip()},
                            libraries={}, paths={}, notice=windows_notice)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("materialize", "verify"))
    args = parser.parse_args()
    if args.command == "materialize":
        count, size = materialize()
        notice_count = len(materialize_notices(load_manifest()))
    else:
        pins = load_manifest()
        count = size = 0
        for package, row in source_rows(pins):
            read_pinned(source_path(package, row), row)
            count += 1
            size += row["bytes"]
        for row in pins["notices"]:
            read_pinned(cache_path(NOTICES, row["output"]), row)
        notice_count = len(pins["notices"])
    print(json.dumps(dict(command=args.command, source_files=count, source_bytes=size,
                         notice_files=notice_count)))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        raise SystemExit(str(error))
