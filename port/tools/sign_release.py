#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Write and sign the update feed for a release (port/updater/README.md).

    python3 port/tools/sign_release.py VERSION [--dir DIR] [--key PEM]

Reads the two packages in DIR (default local-data/port/release/VERSION) and writes there:
  latest.json      {"name", "version", "linux-i686": {"url", "sha256"}, "windows-i686": {...}}: the URLs are the
                   release's GitHub download links (tag vVERSION, or --tag)
  latest.json.sig  the Ed25519 signature of latest.json's exact bytes, as 128 hex digits
Upload both as assets of the release (GitHub's releases/latest/download/ serves them). The private key (default
~/Private/onslaught-ce-signing/onslaught-ce-release.ed25519.pem) is read, never copied or printed; the launcher holds
the public half. Refuses a key whose public half differs from the launcher's.
"""
import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import deps  # noqa: E402

REPO = "https://github.com/dlprentice/onslaught-ce/releases/download"


def main():
    from cryptography.hazmat.primitives import serialization
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("version")
    ap.add_argument("--dir", type=Path)
    ap.add_argument("--tag", help="the release tag the packages are attached to (default vVERSION; 0.1 ships as v0.1.0)")
    ap.add_argument("--base-url", help="tests: the packages' folder URL instead of the GitHub release (e.g. file:///...)")
    ap.add_argument("--key", type=Path, default=Path.home() / "Private/onslaught-ce-signing/onslaught-ce-release.ed25519.pem")
    a = ap.parse_args()
    d = a.dir or deps.WORK / "release" / a.version
    feed = {"name": "Onslaught CE", "version": a.version}
    for plat, ext in (("linux-i686", "tar.gz"), ("windows-i686", "zip")):
        name = f"onslaught-ce-{a.version}-{plat}.{ext}"
        base = a.base_url.rstrip("/") if a.base_url else f"{REPO}/{a.tag or 'v' + a.version}"
        feed[plat] = {"url": f"{base}/{name}", "sha256": hashlib.sha256((d / name).read_bytes()).hexdigest()}
    data = (json.dumps(feed, indent=1) + "\n").encode()
    key = serialization.load_pem_private_key(a.key.read_bytes(), None)
    pub = key.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw).hex()
    embedded = re.search(r'PUBLIC_KEY_HEX\[\] = "([0-9a-f]{64})"', (deps.PORT / "updater/ce_launch.c").read_text()).group(1)
    if pub != embedded:
        sys.exit("the key's public half differs from the launcher's PUBLIC_KEY_HEX")
    (d / "latest.json").write_bytes(data)
    (d / "latest.json.sig").write_text(key.sign(data).hex() + "\n")
    print(f"wrote {d / 'latest.json'} and latest.json.sig (public key {pub})")


if __name__ == "__main__":
    main()
