#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prepare the extra Linux libraries for a native-signaling offscreen menu test.

Use only for an existing public network+ICE+native-signaling Linux build with
its link receipt, pinned Sniper SDK and normal offscreen library directories.
No downloads, service calls, game launch or credentials. Output is a fresh,
private directory under this checkout's ignored local-data. This adds the
signaling library and any otherwise missing SDK dependency without putting the
SDK's driver stack in the desktop process's dynamic-library path.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
CURL = "libcurl-gnutls.so.4"
LOADER = Path("/lib/ld-linux.so.2")
# Only the pinned native curl closure is eligible; never supply a display,
# C/C++ runtime, SDL or game library on behalf of an absent host driver.
FAMILY = frozenset("""curl-gnutls nghttp2 idn2 ssh2 psl nettle gnutls brotlidec
brotlicommon unistring p11-kit tasn1 hogweed gmp ffi z zstd pthread""".split())
SONAME = re.compile(r"lib([A-Za-z0-9_+-]{1,80})\.so(?:\.[0-9]+)*\Z")
MISSING = re.compile(r"error while loading shared libraries: (lib[^/ :\n]+): cannot open shared object file")
SDK_PATHS = ("usr/lib/i386-linux-gnu", "lib/i386-linux-gnu")
PLAYER_PATHS = ("prefix-sniper/lib", "dxvk-native/usr/lib32", "ffmpeg-shared-sniper/linux/usr/lib")


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def refuse(reason):
    raise ValueError(reason)


def prepare(game, data, output):
    game = game.resolve(strict=True)
    data = data.resolve(strict=True)
    out = output.resolve()
    if not out.is_relative_to((ROOT / "local-data").resolve()) or out.exists():
        refuse("output must be a new directory under local-data")
    link = json.loads((game.parent / ".bea-ice-link.json").read_text())
    native = link.get("native_signaling", {})
    if not (link.get("schema") == 2 and link.get("profile") == "public-network-ice" and
            link.get("target") == "linux" and link.get("test_build") is False and
            link.get("system_clang") is False and link.get("sysroot_name") == "sniper" and
            native.get("enabled") is True and native.get("https_backend") == "libcurl-gnutls" and
            link["binaries"]["game"]["sha256"] == digest(game)):
        refuse("expected the matching native-signaling Linux game and link receipt")
    sdk = (data / "sysroot-sniper").resolve(strict=True)
    player = [(data / z).resolve(strict=True) for z in PLAYER_PATHS]
    if any(":" in str(p) or "\n" in str(p) for p in (*player, out)):
        refuse("invalid library search path")
    out.mkdir(mode=0o700, parents=True)
    libs = out / "lib"
    libs.mkdir(mode=0o700)
    for _ in range(32):
        q = subprocess.run([str(LOADER), "--library-path", ":".join(map(str, [*player, libs])),
                            "--list", str(game)], capture_output=True, text=True, timeout=15,
                           env={"PATH": "/usr/bin:/bin", "LC_ALL": "C"})
        if q.returncode == 0:
            if not re.search(r"^\s*" + re.escape(CURL) + r" => ", q.stdout, re.M):
                refuse("native HTTPS backend absent from the binary")
            break
        match = MISSING.search(q.stderr)
        if match is None or not SONAME.fullmatch(match[1]):
            refuse("dynamic loader failed for a reason other than an unavailable SONAME")
        name = match[1]
        family = SONAME.fullmatch(name).group(1)
        if family not in FAMILY:
            refuse("missing library is outside the native HTTPS dependency family")
        candidates = [(sdk / sub / name) for sub in SDK_PATHS]
        src = next((p.resolve() for p in candidates if p.is_file() and p.resolve().is_relative_to(sdk)), None)
        dest = libs / name
        if src is None or dest.exists() or dest.is_symlink():
            refuse("missing dependency is not a fresh pinned SDK library")
        shutil.copyfile(src, dest)
        dest.chmod(0o400)
    else:
        refuse("dynamic dependency count exceeded")
    records = {p.name: digest(p) for p in sorted(libs.iterdir())}
    admission = {"schema": 1, "target": "linux32", "native_https": True,
                 "game_sha256": digest(game), "added_sdk_libraries": records,
                 "game_process_ran": False, "transport_proven": False}
    (out / "receipt.json").write_text(json.dumps(admission, indent=2) + "\n")
    return admission


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("game", "toolchain-data", "output"):
        parser.add_argument("--" + key, type=Path, required=True)
    args = parser.parse_args()
    try:
        result = prepare(args.game, args.toolchain_data, args.output)
    except (ValueError, OSError, KeyError, TypeError, subprocess.SubprocessError) as err:
        print("native-signaling runtime did not pass:", type(err).__name__, str(err), file=sys.stderr)
        return 1
    print("native-signaling runtime libraries verified:", len(result["added_sdk_libraries"]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
