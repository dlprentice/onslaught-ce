#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Write and sign the v2 update feed for a release (port/updater/UPDATER-V2.md).

    python3 port/tools/sign_update_v2.py VERSION [--dir DIR] [--tag TAG] [--key PEM]

--key (or the environment variable ONSLAUGHT_CE_SIGNING_KEY) names the private key file; there is no default location.

Reads the two packages in DIR (default local-data/port/release/VERSION) and writes there:
  ce-update-v2.json      {"format": "onslaught-ce-update-v2", "version", "packages": {PLATFORM: {"url", "sha256",
                         "size"}}}: the URLs are the release's GitHub download links (tag vVERSION, or --tag)
  ce-update-v2.json.sig  Ed25519 over b"onslaught-ce update feed v2\\n" + the feed's exact bytes, as 128 hex digits

It never writes latest.json (the 0.1.0-0.1.6 launchers' feed name) and refuses a DIR that holds one. Publishing the
feed needs the maintainer's explicit decision (UPDATER-V2.md, "Publishing"). The private key (the file --key or
ONSLAUGHT_CE_SIGNING_KEY names) is read, never copied or printed, and must match the launcher's PUBLIC_KEY_HEX. The tests use feed_bytes() and sign() with their own key.
"""
import argparse
import hashlib
import json
import os
import re
import sys
from pathlib import Path

FORMAT = "onslaught-ce-update-v2"
DOMAIN = b"onslaught-ce update feed v2\n"
FEED_NAME = "ce-update-v2.json"
REPO = "https://github.com/dlprentice/onslaught-ce/releases/download"
PLATFORMS = (("linux-i686", "tar.gz"), ("windows-i686", "zip"))
LAUNCHER = Path(__file__).resolve().parents[1] / "updater/ce_launch.c"


def feed_bytes(version: str, packages: dict) -> bytes:
    """packages: {platform: (url, sha256 hex, size)}. The exact bytes that are signed and published."""
    feed = {"format": FORMAT, "version": version,
            "packages": {p: {"url": u, "sha256": h, "size": n} for p, (u, h, n) in packages.items()}}
    return (json.dumps(feed, indent=1) + "\n").encode()


def sign(private_key, data: bytes) -> bytes:
    """The .sig file's bytes for data, signed with a cryptography Ed25519PrivateKey."""
    return (private_key.sign(DOMAIN + data).hex() + "\n").encode()


def main():
    from cryptography.hazmat.primitives import serialization
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("version")
    ap.add_argument("--dir", type=Path)
    ap.add_argument("--tag", help="the release tag the packages are attached to (default vVERSION)")
    ap.add_argument("--key", type=Path, default=os.environ.get("ONSLAUGHT_CE_SIGNING_KEY") or None,
                    help="the private key (PEM); default: the file the environment variable ONSLAUGHT_CE_SIGNING_KEY names")
    a = ap.parse_args()
    if a.key is None:
        sys.exit("the private key file is not named: pass --key PEM or set ONSLAUGHT_CE_SIGNING_KEY")
    if not re.fullmatch(r"[0-9]{1,5}(\.[0-9]{1,5}){0,3}", a.version):
        sys.exit("VERSION: one to four numeric parts (the launcher compares them numerically)")
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import deps  # noqa: E402
    d = a.dir or deps.WORK / "release" / a.version
    if (d / "latest.json").exists() or (d / "latest.json.sig").exists():
        sys.exit(f"{d} holds latest.json: never publish the old feed name (UPDATER-V2.md); remove it from this folder first")
    packages = {}
    for plat, ext in PLATFORMS:
        name = f"onslaught-ce-{a.version}-{plat}.{ext}"
        data = (d / name).read_bytes()
        packages[plat] = (f"{REPO}/{a.tag or 'v' + a.version}/{name}", hashlib.sha256(data).hexdigest(), len(data))
    data = feed_bytes(a.version, packages)
    key = serialization.load_pem_private_key(a.key.read_bytes(), None)
    pub = key.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw).hex()
    embedded = re.search(r'PUBLIC_KEY_HEX\[\] = "([0-9a-f]{64})"', LAUNCHER.read_text()).group(1)
    if pub != embedded:
        sys.exit("the key's public half differs from the launcher's PUBLIC_KEY_HEX")
    (d / FEED_NAME).write_bytes(data)
    (d / (FEED_NAME + ".sig")).write_bytes(sign(key, data))
    print(f"wrote {d / FEED_NAME} and {FEED_NAME}.sig (public key {pub}); publishing needs the maintainer's explicit decision")


if __name__ == "__main__":
    main()
