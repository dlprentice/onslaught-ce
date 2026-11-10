#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify and snapshot reviewed libjuice source/notices for an explicit ICE package.

No fetching or receipt creation: inputs must come from a successful pinned build.
"""
from contextlib import ExitStack, contextmanager
import fcntl
import hashlib
import io
import json
import os
from pathlib import Path
import re
import stat
import tarfile

import deps

PROFILE = "public-network-ice"
MARKER = b"CE-NET-FEATURE:ICE-LIBJUICE:1"
REVIEWED_PIN = dict(name="libjuice", version="1.7.4+b89c792e", licence="MPL-2.0",
                    sha="713b22174e8cb2e06538fa9bac2650bda21b83c86f5128813ab4dcf8f5bea9ed",
                    url="https://codeload.github.com/paullouisageneau/libjuice/tar.gz/"
                        "b89c792e3612faf2f12cf35bcc56857313a06be3")
LICENCE_SHA256 = "fab3dd6bdab226f1c08630b1dd917e11fcb4ec5e1e020e2c16f83a0a13863e85"
REVIEWED_PATCH = dict(name="libjuice-relay-only.patch", licence="MPL-2.0",
                     sha256="21d2dbecacfff42e61fc16589e26795335600354ca82c45199ba8f26235341e2")
# Independently reconstructed from the reviewed archive plus exact patch, using deps.source_tree_sha256.
SOURCE_TREE_SHA256 = "598194d6ecc4d77bce4f93e1b4edb11eb41dd6b9c96b7bc0b5e718461592d493"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read_regular(path, limit):
    """Bound reads and refuse symlinks/FIFOs, including replacement during opening."""
    path = Path(path)
    try:
        with os.fdopen(os.open(path, os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW), "rb") as stream:
            info = os.fstat(stream.fileno())
            if not stat.S_ISREG(info.st_mode) or info.st_size > limit:
                raise SystemExit(f"{path}: ICE input must be a bounded regular file")
            data = stream.read(limit + 1)
            if len(data) > limit:
                raise SystemExit(f"{path}: ICE input grew beyond its size limit")
            return data
    except OSError:
        raise SystemExit(f"{path}: ICE input missing or unreadable")


def binary_identity(path):
    data = read_regular(path, 256 * 1024 * 1024)
    return dict(name=Path(path).name, size=len(data), sha256=digest(data))


def optional_tools():
    try:
        import ice_deps
        import network_source_id
    except ImportError:
        raise SystemExit("ICE packaging requires the optional ICE build tools in this source checkout")
    if not all(callable(getattr(ice_deps, name, None)) for name in ("source_patches", "checked_source")):
        raise SystemExit("ICE packaging requires the schema2 patched-source dependency tools")
    return ice_deps, network_source_id


@contextmanager
def install_lock(ice, target):
    """Share an existing install lock without creating or writing anything in the cache."""
    path = deps.DATA / "ice-locks" / (ice.target_name(target) + ".lock")
    try:
        stream = os.fdopen(os.open(path, os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW), "rb")
    except OSError:
        raise SystemExit(f"{target}: existing ICE install lock missing or unreadable")
    with stream:
        if not stat.S_ISREG(os.fstat(stream.fileno()).st_mode):
            raise SystemExit(f"{target}: ICE install lock must be a regular file")
        fcntl.flock(stream, fcntl.LOCK_SH)
        yield


def source_inputs(ice):
    row = ice.pin()
    if {key: row.get(key) for key in REVIEWED_PIN} != REVIEWED_PIN:
        raise SystemExit("libjuice pin differs from the reviewed ICE package source/licence")
    _, patches = deps.patch_identity(row)
    if patches:
        raise SystemExit("ICE packaging refuses ordinary dependency patches outside its reviewed source policy")
    if json.dumps(ice.source_patches(), sort_keys=True) != json.dumps([REVIEWED_PATCH], sort_keys=True):
        raise SystemExit("libjuice source patch metadata differs from the reviewed policy")
    patch = read_regular(deps.PORT / "network" / REVIEWED_PATCH["name"], 65536)
    if digest(patch) != REVIEWED_PATCH["sha256"]:
        raise SystemExit("libjuice source patch bytes differ from the reviewed pin")
    archive = read_regular(row["file"], 16 * 1024 * 1024)
    if digest(archive) != REVIEWED_PIN["sha"]:
        raise SystemExit("libjuice source archive differs from the reviewed pin")
    try:
        with tarfile.open(fileobj=io.BytesIO(archive), mode="r:gz") as stream:
            members = [entry for entry in stream.getmembers()
                       if entry.name.count("/") == 1 and entry.name.endswith("/LICENSE")]
            if len(members) != 1 or not members[0].isfile():
                raise SystemExit("libjuice source archive must contain one regular top-level LICENSE")
            licence = stream.extractfile(members[0]).read(65537)
    except (OSError, tarfile.TarError):
        raise SystemExit("libjuice source archive is unreadable")
    if digest(licence) != LICENCE_SHA256:
        raise SystemExit("libjuice licence text differs from the reviewed pinned source")
    return archive, licence, patch


def expected_receipt(build, target, ice, source_id, with_native_signaling=False):
    ice.require(target)
    prefix = ice.prefix(target)
    row = REVIEWED_PIN
    receipt_bytes = read_regular(prefix / ".bea-ice-build.json", 65536)
    library = read_regular(prefix / "lib/libjuice.a", 64 * 1024 * 1024)
    header = read_regular(prefix / "include/juice/juice.h", 1024 * 1024)
    licence = read_regular(prefix / "licenses/MPL-2.0.txt", 65536)
    if digest(licence) != LICENCE_SHA256:
        raise SystemExit(f"{target}: installed libjuice licence differs from the pinned source")
    game, launcher = (("bea", "onslaught-ce-launch") if target == "linux" else ("bea.exe", "onslaught-ce.exe"))
    try:
        dependency = json.loads(receipt_bytes)
    except ValueError:
        raise SystemExit(f"{target}: invalid patched libjuice dependency receipt")
    fields = dict(schema=2, source_patches=[REVIEWED_PATCH], source_tree_sha256=SOURCE_TREE_SHA256)
    if not isinstance(dependency, dict) or json.dumps({key: dependency.get(key) for key in fields}, sort_keys=True) != json.dumps(fields, sort_keys=True):
        raise SystemExit(f"{target}: patched libjuice receipt lacks exact schema2 source identity")
    expected = dict(schema=2, target=target, profile=PROFILE, source_id=source_id,
                    source_patches=[REVIEWED_PATCH], source_tree_sha256=SOURCE_TREE_SHA256,
                    build_recipe_sha256=digest(read_regular(deps.PORT / "build.py", 2_000_000)),
                    marker=MARKER.decode("ascii"), system_clang=False, sysroot_name="sniper", test_build=False,
                    libjuice=dict(name=row["name"], version=row["version"], licence=row["licence"],
                                  source_sha256=row["sha"], source_url=row["url"],
                                  build_receipt_sha256=digest(receipt_bytes), static_library_sha256=digest(library),
                                  header_sha256=digest(header), licence_text_sha256=digest(licence)),
                    binaries=dict(game=binary_identity(Path(build) / target / game),
                                  launcher=binary_identity(Path(build) / target / launcher)))
    if with_native_signaling:
        import package_signaling_inputs
        expected["native_signaling"] = package_signaling_inputs.receipt_fields(target)
    return expected, receipt_bytes


def reconstruction_commands(revision, patch_name):
    """Normalize source modes without changing the recipient shell's umask."""
    return ("  (\n"
            "    umask 022 &&\n"
            f"    tar -xzf libjuice-{revision}.tar.gz &&\n"
            f"    cd libjuice-{revision} &&\n"
            f"    patch --batch --forward --fuzz=0 -p1 -i ../{patch_name}\n"
            "  )\n")


def prepare(build, with_native_signaling=False):
    """Verify successful build-time receipts and return immutable package payload bytes."""
    import package_signaling_inputs
    if deps.SYSROOT_NAME != "sniper":
        raise SystemExit("ICE packages require the pinned sniper build profile")
    ice, identity = optional_tools()
    with ExitStack() as stack:
        for target in ("linux", "windows"):
            stack.enter_context(install_lock(ice, target))
        archive, licence, patch = source_inputs(ice)
        source, _ = ice.checked_source()
        if deps.source_tree_sha256(source) != SOURCE_TREE_SHA256:
            raise SystemExit("libjuice patched source tree differs from the independently reconstructed tree")
        source_id = identity.compute_source_id(deps.PORT.parent, with_ice=True)
        if not isinstance(source_id, str) or not re.fullmatch(r"[0-9a-f]{64}", source_id):
            raise SystemExit("ICE package source profile must have a valid source ID")
        recipe = read_regular(deps.PORT / "tools/ice_deps.py", 2_000_000)
        lock = read_regular(deps.PORT / "ice-deps.lock", 65536)
        archive_name = "source/libjuice-" + REVIEWED_PIN["url"].rsplit("/", 1)[1] + ".tar.gz"
        patch_name = "source/" + REVIEWED_PATCH["name"]
        revision = REVIEWED_PIN["url"].rsplit("/", 1)[1]
        instructions = ("Corresponding modified libjuice source\n\n"
                        f"Verify upstream archive SHA-256 {REVIEWED_PIN['sha']}\n"
                        f"Verify patch SHA-256 {REVIEWED_PATCH['sha256']}\n"
                        "Preserve the archive, patch, original licence and copyright notices together.\n\n"
                        "From this package's source directory, run this whole parenthesized block.\n"
                        "Its umask normalizes the source modes without changing your shell's umask:\n"
                        f"{reconstruction_commands(revision, REVIEWED_PATCH['name'])}\n"
                        "The unchanged upstream archive plus patch reconstructs the modified dependency.\n"
                        "The patch changes include/juice/juice.h, src/agent.h, src/agent.c and src/juice.c;\n"
                        "upstream notices and LICENSE remain intact. Its relay-policy API and selection/egress\n"
                        "checks are MPL-2.0.\n"
                        f"Patched tree SHA-256: {SOURCE_TREE_SHA256}\n"
                        "This uses port/tools/deps.py source_tree_sha256 (relative paths, types, modes and\n"
                        "content hashes), also checked by the build receipts. The included recipe/pin record\n"
                        "the static client configuration; the tree retains its CMake build files. See the\n"
                        "package root's ICE-SOURCE-NOTICE.txt for MPL source and additional GPL options.\n").encode()
        outputs = {}
        for target in ("linux", "windows"):
            expected, receipt_bytes = expected_receipt(build, target, ice, source_id, with_native_signaling)
            game_path = build / target / expected["binaries"]["game"]["name"]
            has_native_marker = package_signaling_inputs.MARKER in read_regular(game_path, 256 * 1024 * 1024)
            if has_native_marker != with_native_signaling:
                raise SystemExit("native-signaling marker requires its explicit package opt-in and matching build")
            path = Path(build) / target / ".bea-ice-link.json"
            try:
                known = json.loads(read_regular(path, 65536))
                if with_native_signaling:
                    package_signaling_inputs.check_receipt(known.get("native_signaling") if isinstance(known, dict) else None, target)
                elif isinstance(known, dict) and "native_signaling" in known:
                    raise SystemExit("native-signaling receipt requires --with-native-signaling")
            except ValueError:
                raise SystemExit(f"{target}: ICE link receipt is invalid")
            if json.dumps(known, sort_keys=True) != json.dumps(expected, sort_keys=True):
                raise SystemExit(f"{target}: ICE link receipt/profile/inputs changed; rebuild with --public --with-network --with-ice")
            if MARKER + b"\0" not in read_regular(Path(build) / target / expected["binaries"]["game"]["name"], 256 * 1024 * 1024):
                raise SystemExit(f"{target}: ICE link receipt candidate lacks the retained feature marker")
            provenance = dict(schema=2, component="libjuice", source_archive=archive_name,
                              source_unmodified=False, source_archive_unmodified=True,
                              source_patches=[dict(REVIEWED_PATCH, package_path=patch_name)],
                              source_tree_sha256=SOURCE_TREE_SHA256, input=REVIEWED_PIN,
                              licence_text_sha256=LICENCE_SHA256, build=expected)
            notice = (f"libjuice {REVIEWED_PIN['version']} with the CE relay-policy patch is statically linked into the game.\n"
                      "Its corresponding modified source is the unchanged upstream archive plus the exact\n"
                      "MPL-2.0 patch and application instructions, included at:\n"
                      f"{archive_name}\n"
                      f"{patch_name}\nsource/libjuice-APPLY.txt\n"
                      "The source is available under MPL-2.0; its exact licence is licenses/libjuice-MPL-2.0.txt.\n"
                      "For this combination with GPL-3.0-only Onslaught CE, the MPL-covered source is additionally\n"
                      "distributed under GPL-3.0-only pursuant to MPL section 3.3. Recipients may use that source\n"
                      "under either MPL-2.0 or GPL-3.0-only. COPYING contains GPL version 3. Upstream notices are unchanged.\n"
                      "The exact source pin and build-input hashes are in source/libjuice-PROVENANCE.json.\n"
                      "source/libjuice-build-recipe.py and source/libjuice-ice-deps.lock retain the CE library recipe\n"
                      "and pin for reference. The complete upstream archive also contains its CMake build files.\n"
                      "Its static client configuration is BUILD_SHARED_LIBS=OFF, NO_TESTS=ON, NO_SERVER=ON and USE_NETTLE=OFF.\n"
                      "No download or written source offer is needed to obtain the included libjuice source.\n"
                      "This source inclusion does not replace the release's other source or licence obligations.\n").encode()
            files = {"licenses/libjuice-MPL-2.0.txt": licence, archive_name: archive,
                     patch_name: patch, "source/libjuice-APPLY.txt": instructions,
                     "source/libjuice-PROVENANCE.json": (json.dumps(provenance, sort_keys=True, indent=2) + "\n").encode(),
                     "source/libjuice-build-receipt.json": receipt_bytes,
                     "source/libjuice-build-recipe.py": recipe, "source/libjuice-ice-deps.lock": lock,
                     "ICE-SOURCE-NOTICE.txt": notice}
            outputs[target] = dict(files=files, notice=notice, binaries=expected["binaries"])
        return outputs


def check_snapshot(path, expected):
    if binary_identity(path) != dict(expected, name=Path(path).name):
        raise SystemExit("ICE package binary changed after its build receipt was checked")


def readme(base):
    text = base.decode("utf-8")
    old = re.escape("The game sends data only to the other player's address and contacts no server.").replace(r"\ ", r"\s+")
    replacement = ("\n  Direct-IP mode sends data only to the other player's address.\n"
                   "  ICE mode may also contact the STUN/TURN servers supplied by the user.\n")
    text, count = re.subn(old, replacement, text)
    if count != 1:
        raise SystemExit("ICE package README's direct-IP contact statement changed; review its networking instructions")
    title, separator, body = text.partition("\n")
    return (title + separator + "\nEXPERIMENTAL ICE-enabled package. The connection instructions below describe direct-IP mode.\n"
            "This build does not include a provider account, server address or TURN credentials.\n"
            "ICE-SOURCE-NOTICE.txt explains the included libjuice source and recipient licence options.\n\n" + body).encode()
