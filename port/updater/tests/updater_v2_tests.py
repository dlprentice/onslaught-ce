#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The v2 updater (port/updater/UPDATER-V2.md), end to end, in a bubblewrap sandbox: real launchers built as updater
test builds (a test key; no release key is read), real packages served from 127.0.0.1, a disposable install with
neighbouring "<install>.old-*" paths, links and (under Wine) junctions that must stay untouched.

    python3 port/updater/tests/updater_v2_tests.py [--root DIR] [--platform linux|windows|all] [-k PATTERN]

The outer process builds four test launchers per platform (0.1.8, 0.1.8.1, 0.1.8.2, 0.1.8.3) and their packages
under DIR (default local-data/updater-v2-tests), checks every old release tag's feed define, then re-runs this file
inside bwrap (no network but loopback, read-only system, private HOME and /tmp, the player's home hidden). Windows
runs under Wine with a private prefix and no display. Nothing here reads the release signing key.
"""
import argparse
import hashlib
import http.server
import io
import json
import os
import shutil
import socket
import subprocess
import sys
import tarfile
import threading
import time
import unittest
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
PORT_DIR = HERE.parents[1]
VERSIONS = ("0.1.8", "0.1.8.1", "0.1.8.2", "0.1.8.3")
BASE = VERSIONS[0]
PLATS = {"linux": ("linux-i686", "tar.gz"), "windows": ("windows-i686", "zip")}
CRASH_STEPS = ("stage-created", "feed-fetched", "package-downloaded", "state-temp-written:1", "state-temp-written:2",
               "entry-recorded", "unpack-mid", "unpacked", "state-temp-written:3", "switched")
EXIT = {"updated": 0, "current": 10, "not_now": 11, "disabled": 12, "offline": 13, "bad_feed": 14, "bad_package": 15,
        "install_failed": 16, "busy": 17}
GAME_SH = '#!/bin/sh\necho "stand-in game {version} args: $*"\n[ -n "$STANDIN_MARK" ] && echo running > "$STANDIN_MARK"\n[ -n "$STANDIN_SLEEP" ] && sleep "$STANDIN_SLEEP"\nexit 0\n'
GAME_C = r'''#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
int main(int argc, char **argv) { printf("stand-in game {version} args:"); for (int i = 1; i < argc; ++i) printf(" %s", argv[i]); printf("\n"); fflush(stdout);
  const char *m = getenv("STANDIN_MARK"); if (m && *m) { FILE *f = fopen(m, "w"); if (f) { fputs("running\n", f); fclose(f); } }
  const char *s = getenv("STANDIN_SLEEP"); if (s && *s) Sleep(1000 * atoi(s)); return 0; }
'''


# ---- outer: build, package, prove, sandbox --------------------------------------------------------------------------

def old_tag_feeds(repos) -> dict:
    """Every 0.1.* release tag's launcher source, in each repository given (this repository's ce-v* tags and a clone of the
    public onslaught-ce's v* tags): its FEED define and whether it names the v2 feed anywhere."""
    out = {}
    for repo in repos:
        if not Path(repo).exists():
            continue
        tags = subprocess.run(["git", "-C", str(repo), "tag", "-l", "ce-v0.1.*", "v0.1.*"], capture_output=True,
                              text=True, check=True).stdout.split()
        for t in sorted(tags):
            src = subprocess.run(["git", "-C", str(repo), "show", f"{t}:port/updater/ce_launch.c"], capture_output=True,
                                 text=True)
            if src.returncode:
                continue
            feed = [l.strip() for l in src.stdout.splitlines() if l.startswith("#define FEED")]
            out[f"{Path(repo).name}:{t}"] = {"feed": feed, "mentions_v2": "ce-update-v2" in src.stdout}
    return out


def prepare(root: Path, platforms):
    sys.path.insert(0, str(PORT_DIR)); sys.path.insert(0, str(PORT_DIR / "tools"))
    import build  # noqa: E402
    import deps  # noqa: E402
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
    root.mkdir(parents=True, exist_ok=True)
    keys = {}
    for name in ("test", "wrong"):
        p = root / f"{name}.key"
        if not p.exists():
            p.write_bytes(Ed25519PrivateKey.generate().private_bytes(
                serialization.Encoding.Raw, serialization.PrivateFormat.Raw, serialization.NoEncryption()))
        keys[name] = Ed25519PrivateKey.from_private_bytes(p.read_bytes())
    pub = keys["test"].public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw).hex()
    defines = ["-DCE_UPDATER_TESTING", f'-DCE_UPDATER_TEST_KEY="{pub}"']
    spec = __import__("importlib.util").util.spec_from_file_location("ce_package", PORT_DIR / "tools/package.py")
    package = __import__("importlib.util").util.module_from_spec(spec)
    spec.loader.exec_module(package)
    old = json.loads((root / "prepared.json").read_text()) if (root / "prepared.json").exists() else {}
    info = {"pub": pub, "packages": old.get("packages", {}) if old.get("pub") == pub else {}}
    for plat in platforms:
        pname, ext = PLATS[plat]
        for v in VERSIONS:
            bdir = root / "build" / plat / v
            exe = bdir / ("onslaught-ce.exe" if plat == "windows" else "onslaught-ce-launch")
            if build.build_launcher(plat, v, exe, defines):
                sys.exit(f"{plat} {v}: launcher build failed")
            top = f"onslaught-ce-{v}-{pname}"
            readme = f"Onslaught CE {v} (preview)\n\nupdater test package\n".encode()
            if plat == "linux":
                files = {f"{top}/onslaught-ce": (package.LAUNCHER.encode(), 0o755),
                         f"{top}/preflight.sh": (b"#!/bin/sh\nexit 0\n", 0o755),
                         f"{top}/onslaught-ce-launch": (exe.read_bytes(), 0o755),
                         f"{top}/bea": (GAME_SH.replace("{version}", v).encode(), 0o755),
                         f"{top}/lib/libSDL3.so.0": ((deps.PREFIX / "lib/libSDL3.so.0").resolve().read_bytes(), 0o755),
                         f"{top}/README.txt": (readme, 0o644)}
                pkg = root / "packages" / f"{top}.tar.gz"
                pkg.parent.mkdir(parents=True, exist_ok=True)
                with tarfile.open(pkg, "w:gz", format=tarfile.GNU_FORMAT) as t:
                    for name, (data, mode) in sorted(files.items()):
                        ti = tarfile.TarInfo(name); ti.size, ti.mode, ti.mtime = len(data), mode, 0
                        t.addfile(ti, io.BytesIO(data))
            else:
                game = bdir / "bea.exe"
                (bdir / "bea.c").write_text(GAME_C.replace("{version}", v))
                subprocess.run([str(deps.DATA / "llvm-mingw/bin/i686-w64-mingw32-clang"), "-O1", str(bdir / "bea.c"),
                                "-o", str(game)], check=True)
                files = {f"{top}/onslaught-ce.exe": (exe.read_bytes(), 0o755), f"{top}/bea.exe": (game.read_bytes(), 0o755),
                         f"{top}/SDL3.dll": ((deps.PREFIX_WIN / "bin/SDL3.dll").read_bytes(), 0o644),
                         f"{top}/libz.dll": ((deps.PREFIX_WIN / "bin/libz.dll").read_bytes(), 0o644),
                         f"{top}/README.txt": (readme, 0o644)}
                pkg = root / "packages" / f"{top}.zip"
                pkg.parent.mkdir(parents=True, exist_ok=True)
                with zipfile.ZipFile(pkg, "w", zipfile.ZIP_DEFLATED) as z:
                    for name, (data, mode) in sorted(files.items()):
                        zi = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0)); zi.external_attr = mode << 16
                        zi.compress_type = zipfile.ZIP_DEFLATED
                        z.writestr(zi, data)
            info["packages"][f"{plat}/{v}"] = {"file": str(pkg), "top": top,
                                               "unpacked": sum(len(d) for d, _ in files.values())}
    # the public repository's tags: git clone --bare --filter=blob:none https://github.com/dlprentice/onslaught-ce
    info["old_tags"] = old_tag_feeds([PORT_DIR.parent, root / "onslaught-ce.git"])
    (root / "prepared.json").write_text(json.dumps(info, indent=1))
    return info


def small_user_dir(root: Path, platform: str) -> Path:
    """The tiny user-folder file system (used on Linux only; see test_disk_full_while_unpacking_or_downloading)."""
    return root / "small-user"


def sandbox(root: Path, platform: str, pattern, info) -> int:
    game = os.environ.get("BEA_GAME_DIR") or str(Path.home() / ".local/share/Steam/steamapps/common/Battle Engine Aquila")
    home = str(Path.home())
    small = info["packages"][f"{platform}/{BASE}"]["unpacked"] + (1 << 20)
    small_user = small_user_dir(root, platform)
    for d in (root / "small-install", small_user, root / "home"):
        d.mkdir(parents=True, exist_ok=True)
    cmd = ["bwrap", "--die-with-parent", "--unshare-all", "--ro-bind", "/", "/", "--dev", "/dev", "--proc", "/proc",
           "--tmpfs", "/tmp", "--tmpfs", "/run/user", "--tmpfs", home, "--ro-bind", str(PORT_DIR.parent), str(PORT_DIR.parent),
           "--bind", str(root), str(root),
           "--size", str(small), "--tmpfs", str(root / "small-install"),
           "--size", str(256 << 10), "--tmpfs", str(small_user),
           "--setenv", "HOME", str(root / "home"), "--unsetenv", "DISPLAY", "--unsetenv", "WAYLAND_DISPLAY",
           "--unsetenv", "XDG_RUNTIME_DIR", "--unsetenv", "XDG_DATA_HOME", "--unsetenv", "BEA_USER_DIR"]
    if Path(game, "data").is_dir():
        cmd += ["--ro-bind", game, game, "--setenv", "UPDV2_GAME", game]
    cmd += [sys.executable, "-I", str(Path(__file__).resolve()), "--inside", "--root", str(root), "--platform", platform]
    for k in pattern:
        cmd += ["-k", k]
    return subprocess.run(cmd).returncode


# ---- inner: the suite ----------------------------------------------------------------------------------------------

DOMAIN = b"onslaught-ce update feed v2\n"
ROOT = PLATFORM = INFO = None
KEYS = {}


class Server:
    """127.0.0.1 HTTP: files from www/; /trunc/NAME announces the full length and closes after half."""

    def __init__(self, www: Path):
        self.www, self.requests = www, []
        outer = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *a):
                pass

            def do_GET(self):
                outer.requests.append(self.path)
                trunc = self.path.startswith("/trunc/")
                p = outer.www / self.path.split("/")[-1]
                if not p.is_file():
                    self.send_error(404)
                    return
                data = p.read_bytes()
                self.send_response(200)
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data[: len(data) // 2] if trunc else data)
                if trunc:
                    self.wfile.flush()
                    self.close_connection = True

        self.httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.port = self.httpd.server_address[1]
        threading.Thread(target=self.httpd.serve_forever, daemon=True).start()

    def url(self, name, prefix="/files/"):
        return f"http://127.0.0.1:{self.port}{prefix}{name}"


def winpath(p) -> str:
    """A Windows path for Wine: under the prefix's drive_c as C:, elsewhere Z:. (Under Wine 11 the launcher's settings
    folder check, unchanged since 0.1.7, refuses every Z: user folder, so the Wine cases live on C:.)"""
    p = str(p)
    c = str(ROOT / "wineprefix/drive_c")
    if p == c or p.startswith(c + "/"):
        return "C:" + (p[len(c):] or "/").replace("/", "\\")
    return "Z:" + p.replace("/", "\\")


def tree(*roots) -> dict:
    """Every entry under the roots: type, and content hash or link target (never following links)."""
    out = {}
    for r in roots:
        r = Path(r)
        if not os.path.lexists(r):
            continue
        for dirpath, dirnames, filenames in os.walk(r, followlinks=False):
            for n in dirnames + filenames:
                p = Path(dirpath, n)
                if p.is_symlink():
                    out[str(p)] = ("link", os.readlink(p))
                elif p.is_dir():
                    out[str(p)] = ("dir",)
                else:
                    out[str(p)] = ("file", hashlib.sha256(p.read_bytes()).hexdigest())
    return out


class UpdaterV2(unittest.TestCase):
    """One fresh install, user folder and neighbourhood per test."""

    @classmethod
    def setUpClass(cls):
        cls.server = Server(ROOT / "www")
        (ROOT / "www").mkdir(exist_ok=True)
        cls.pname, cls.ext = PLATS[PLATFORM]
        if PLATFORM == "windows":
            cls.wineprefix = ROOT / "wineprefix"
            env = cls.wine_env(cls)
            subprocess.run(["wineboot", "-i"], env=env, capture_output=True, timeout=300)

    def wine_env(self):
        return {"PATH": "/usr/bin:/bin", "HOME": str(ROOT / "home"), "WINEPREFIX": str(ROOT / "wineprefix"),
                "WINEDEBUG": "-all", "WINEDLLOVERRIDES": "mscoree,mshtml,winemenubuilder.exe=d"}

    def setUp(self):
        base = ROOT / "wineprefix/drive_c/updv2-cases" if PLATFORM == "windows" else ROOT / "cases" / PLATFORM
        self.case = base / self.id().split(".")[-1]
        if self.case.exists():
            shutil.rmtree(self.case)
        self.parent = self.case / "games"
        self.outside = self.case / "outside"
        self.user = self.case / "user"
        for d in (self.parent, self.outside / "target-dir", self.user / "update", self.user / "update-v2" / "stage-0123456789abcdef"):
            d.mkdir(parents=True)
        self.install = self.unpack_base(self.parent)
        name = self.install.name
        (self.outside / "target-dir/keep.txt").write_text("link target\n")
        (self.outside / "target-file.txt").write_text("file target\n")
        (self.parent / f"{name}.old-backup").mkdir()
        (self.parent / f"{name}.old-backup/career.bes").write_bytes(b"career")
        (self.parent / f"{name}.old-20261027").mkdir()
        (self.parent / f"{name}.old-file").write_text("x")
        (self.parent / f"{name}.old-link").symlink_to(self.outside / "target-dir")
        (self.parent / f"{name}.old-filelink").symlink_to(self.outside / "target-file.txt")
        (self.parent / f"{name}.download-1").write_text("d")
        (self.parent / f"{name}.unpack-1").mkdir()
        (self.parent / "Other.old-x").mkdir()
        (self.user / "update/latest.json").write_text('{"old": "cache"}\n')
        (self.user / "update-v2/stage-0123456789abcdef/package").write_text("not ours")
        self.junction = None
        if PLATFORM == "windows":
            target = self.outside / "junction-target"
            target.mkdir()
            (target / "keep.txt").write_text("junction target\n")
            link = self.parent / f"{name}.old-junction"
            self.junction = (link, self.mklink(link, target))
        self.watched = [self.parent, self.outside, self.user / "update", self.user / "update-v2" / "stage-0123456789abcdef"]
        self.before = {k: v for k, v in tree(*self.watched).items() if not k.startswith(str(self.install))}

    def tearDown(self):
        after = {k: v for k, v in tree(*self.watched).items() if not k.startswith(str(self.install))}
        self.assertEqual(after, self.before, "something outside the install changed")

    # -- helpers --

    def mklink(self, link: Path, target: Path) -> bool:
        """A Windows junction made by Wine's own mklink /J; True when Windows lists it as <JUNCTION> to target. (Wine
        11 stores it as a Unix directory whose name carries a marker, not as a Unix symlink.)"""
        env = self.wine_env()
        subprocess.run(["wine", "cmd", "/c", "mklink", "/J", winpath(link), winpath(target)], env=env,
                       capture_output=True, timeout=120)
        r = subprocess.run(["wine", "cmd", "/c", "dir", winpath(link.parent)], env=env, capture_output=True, text=True,
                           timeout=120)
        return any("<JUNCTION>" in l and l.split("<JUNCTION>")[1].split()[0] == link.name for l in r.stdout.splitlines())

    def unpack_base(self, parent: Path, version=BASE) -> Path:
        info = INFO["packages"][f"{PLATFORM}/{version}"]
        if PLATFORM == "linux":
            with tarfile.open(info["file"]) as t:
                t.extractall(parent, filter="data")
        else:
            with zipfile.ZipFile(info["file"]) as z:
                z.extractall(parent)
        return parent / info["top"]

    def publish(self, version=VERSIONS[1], key="test", tamper=None, url_prefix="/files/", size_delta=0,
                fmt="onslaught-ce-update-v2", v1_sig=False, package_bytes=None, extra=b""):
        """Places the package and a signed feed in www/; returns the feed URL."""
        www = ROOT / "www"
        info = INFO["packages"][f"{PLATFORM}/{version}"]
        data = package_bytes if package_bytes is not None else Path(info["file"]).read_bytes()
        name = f"onslaught-ce-{version}-{self.pname}.{self.ext}"
        (www / name).write_bytes(data)
        real = Path(info["file"]).read_bytes()
        pkgs = {self.pname: {"url": self.server.url(name, url_prefix), "sha256": hashlib.sha256(real).hexdigest(),
                             "size": len(real) + size_delta}}
        feed = (json.dumps({"format": fmt, "version": version, "packages": pkgs}, indent=1) + "\n").encode() + extra
        sig = KEYS[key].sign(feed if v1_sig else DOMAIN + feed).hex() + "\n"
        if tamper == "sig":
            sig = ("0" if sig[0] != "0" else "1") + sig[1:]
        if tamper == "bytes":
            feed = feed.replace(b'"size": ', b'"size":  ')
        feed_name = f"feed-{self.id().split('.')[-1]}.json"
        (www / feed_name).write_bytes(feed)
        (www / (feed_name + ".sig")).write_text(sig)
        return self.server.url(feed_name)

    def run_launcher(self, *args, install=None, feed=None, answer="update", timeout=180, **env):
        install = install or self.install
        e = {"PATH": "/usr/bin:/bin", "HOME": str(ROOT / "home"), "BEA_SKIP_PREFLIGHT": "1",
             "BEA_UPDATE_V2_FEED": feed or self.server.url("absent.json"), "BEA_UPDATE_ANSWER": answer}
        e.update({k: str(v) for k, v in env.items()})
        if PLATFORM == "linux":
            e["BEA_USER_DIR"] = str(self.user)
            cmd = [str(install / "onslaught-ce"), *args]
        else:
            e.update(self.wine_env())
            e["BEA_USER_DIR"] = winpath(self.user)
            if "BEA_GAME_DIR" in e:
                e["BEA_GAME_DIR"] = winpath(e["BEA_GAME_DIR"])
            cmd = ["wine", str(install / "onslaught-ce.exe"), *args]
        r = subprocess.run(cmd, env=e, capture_output=True, text=True, timeout=timeout)
        r.log = "".join((self.user / n).read_text(errors="replace") for n in
                        ("onslaught-ce.log", "onslaught-ce.concurrent-1.log", "onslaught-ce.concurrent-2.log")
                        if (self.user / n).exists())
        return r

    def version_of(self, install=None):
        r = self.run_launcher("--version", install=install)
        self.assertEqual(r.returncode, 0, r.stderr)
        return r.stdout.split()[2].lstrip("vV").rstrip(",")  # "Onslaught CE V0.1.8, build ..."

    def state(self):
        p = self.install / "versions/state"
        if not p.exists():
            return None
        lines = p.read_text().splitlines()
        self.assertEqual(lines[0], "onslaught-ce-updater-state 2")
        s = {"entries": {}}
        for l in lines[1:]:
            w = l.split()
            if w[0] == "entry":
                self.assertEqual(len(w), 5, l)  # name, inode, version, status: no device number is recorded
                s["entries"][w[1]] = {"version": w[3], "status": w[4]}
            else:
                self.assertEqual(len(w), 2, l)
                s[w[0]] = w[1]
        return s

    def assert_clean(self):
        """versions/ holds exactly lock, state and the recorded entries; update-v2/ only lock and the foreign stage."""
        s = self.state()
        if s is not None:
            names = sorted(os.listdir(self.install / "versions"))
            self.assertEqual(names, sorted(["lock", "state", *s["entries"]]), names)
            for n, e in s["entries"].items():
                self.assertEqual(e["status"], "ready", n)
        self.assertEqual(sorted(os.listdir(self.user / "update-v2")), ["lock", "stage-0123456789abcdef"])

    def update(self, version=VERSIONS[1], expect="updated", **kw):
        feed = self.publish(version)
        r = self.run_launcher("--update-only", feed=feed, **kw)
        self.assertEqual(r.returncode, EXIT[expect], r.stderr + r.log)
        return r

    # -- the tests --

    def test_end_to_end_update_handoff_and_rollback(self):
        self.assertEqual(self.version_of(), BASE)
        r = self.update()
        self.assertIn("signed feed verified: v0.1.8.1", r.log)
        self.assertIn("package size and SHA-256 match the signed feed", r.log)
        s = self.state()
        self.assertEqual(s["previous"], "base")
        self.assertEqual(s["entries"][s["active"]]["version"], "0.1.8.1")
        self.assertTrue(s["active"].startswith("v0.1.8.1-"))
        self.assertEqual(self.version_of(), "0.1.8.1")
        self.assert_clean()
        # current now, and the base folder's own files never changed
        r = self.run_launcher("--update-only", feed=self.publish(VERSIONS[1]))
        self.assertEqual(r.returncode, EXIT["current"], r.stderr)
        self.assertEqual(self.run_launcher("--rollback").returncode, 0)
        self.assertEqual(self.version_of(), BASE)
        self.assertEqual(self.state()["previous"], s["active"])
        self.assertEqual(self.run_launcher("--rollback").returncode, 0)
        self.assertEqual(self.version_of(), "0.1.8.1")

    def test_retention_keeps_active_and_previous_only(self):
        self.update(VERSIONS[1])
        self.update(VERSIONS[2])
        first = [n for n, e in self.state()["entries"].items() if e["version"] == VERSIONS[1]]
        self.update(VERSIONS[3])
        s = self.state()
        self.assertEqual(sorted(e["version"] for e in s["entries"].values()), [VERSIONS[2], VERSIONS[3]])
        self.assertFalse((self.install / "versions" / first[0]).exists())
        self.assertEqual(self.version_of(), VERSIONS[3])
        self.assert_clean()

    def test_normal_start_offers_installs_and_starts_new_version(self):
        game = os.environ.get("UPDV2_GAME")
        if not game:
            self.skipTest("no game install to pass the launcher's game-data check")
        r = self.run_launcher("--", "-x", feed=self.publish(), BEA_GAME_DIR=game)
        self.assertEqual(r.returncode, 0, r.stderr + r.log)
        self.assertIn("stand-in game 0.1.8.1 args: -x", r.stdout + r.stderr + r.log)
        self.assertIn("update: starting the new version", r.log)
        self.assert_clean()

    def test_bad_signature_wrong_key_v1_signature_and_tampered_bytes(self):
        for kind, kw in (("sig", {"tamper": "sig"}), ("wrong key", {"key": "wrong"}), ("v1 signature", {"v1_sig": True}),
                         ("bytes", {"tamper": "bytes"})):
            with self.subTest(kind):
                feed, n = self.publish(**kw), len(self.server.requests)
                r = self.run_launcher("--update-only", feed=feed)
                self.assertEqual(r.returncode, EXIT["bad_feed"], r.stderr + r.log)
                self.assertIn("signature did not verify", r.log)
                self.assertFalse((self.install / "versions").exists())
                self.assertEqual(len(self.server.requests[n:]), 2, self.server.requests[n:])  # feed and signature only
                self.assert_clean()

    def test_signed_but_wrong_format_or_oversized_feed(self):
        r = self.run_launcher("--update-only", feed=self.publish(fmt="onslaught-ce-update-v1"))
        self.assertEqual(r.returncode, EXIT["bad_feed"])
        self.assertIn("not a v2 feed", r.log)
        r = self.run_launcher("--update-only", feed=self.publish(extra=b" " * 17000))
        self.assertIn(r.returncode, (EXIT["offline"], EXIT["bad_feed"]))
        self.assertFalse((self.install / "versions").exists())
        self.assert_clean()

    def test_package_hash_size_and_truncation(self):
        real = Path(INFO["packages"][f"{PLATFORM}/{VERSIONS[1]}"]["file"]).read_bytes()
        flipped = bytearray(real); flipped[len(real) // 2] ^= 1
        cases = (("hash", {"package_bytes": bytes(flipped)}), ("size smaller", {"size_delta": -1}),
                 ("size larger", {"size_delta": 1}), ("truncated", {"url_prefix": "/trunc/"}))
        for kind, kw in cases:
            with self.subTest(kind):
                r = self.run_launcher("--update-only", feed=self.publish(**kw))
                self.assertEqual(r.returncode, EXIT["bad_package"], kind + r.stderr + r.log)
                self.assertEqual(self.version_of(), BASE)
                self.assertFalse((self.install / "versions").exists())
                self.assert_clean()

    def test_not_newer_offline_and_answers(self):
        r = self.run_launcher("--update-only", feed=self.publish(BASE))
        self.assertEqual(r.returncode, EXIT["current"])
        r = self.run_launcher("--update-only", feed=f"http://127.0.0.1:{free_port()}/feed.json")
        self.assertEqual(r.returncode, EXIT["offline"])
        self.assertNotIn("Error", r.stderr)
        feed = self.publish()
        self.assertEqual(self.run_launcher("--update-only", feed=feed, answer="later").returncode, EXIT["not_now"])
        self.assertEqual(self.run_launcher("--update-only", feed=feed, answer="skip").returncode, EXIT["not_now"])
        self.assertIn("skip_version=0.1.8.1", (self.user / "ce.ini").read_text())
        n = len(self.server.requests)
        self.assertEqual(self.run_launcher("--update-only", feed=feed).returncode, EXIT["not_now"])
        self.assertFalse(any(p.endswith(self.ext) for p in self.server.requests[n:]))
        self.assertFalse((self.install / "versions").exists())
        self.assert_clean()

    def test_opt_out_contacts_nothing(self):
        feed = self.publish()
        n = len(self.server.requests)
        self.assertEqual(self.run_launcher("--update-only", feed=feed, BEA_NO_UPDATE_CHECK=1).returncode, EXIT["disabled"])
        (self.user / "ce.ini").write_text("update_check=0\n")
        self.assertEqual(self.run_launcher("--update-only", feed=feed).returncode, EXIT["disabled"])
        self.assertEqual(self.server.requests[n:], [])

    def test_disk_full_while_unpacking_or_downloading(self):
        for limit in (200_000, 2_000_000):
            with self.subTest(write_limit=limit):
                r = self.run_launcher("--update-only", feed=self.publish(), BEA_UPDATE_TEST_WRITE_LIMIT=limit)
                self.assertIn(r.returncode, (EXIT["bad_package"], EXIT["install_failed"]), r.stderr + r.log)
                self.assertEqual(self.version_of(), BASE)
                self.assert_clean()
        # a real full file system: the install on a small tmpfs (room for the base, not the new version)
        small = ROOT / "small-install" / self.id().split(".")[-1]
        small.mkdir()
        install = self.unpack_base(small)
        r = self.run_launcher("--update-only", install=install, feed=self.publish())
        self.assertEqual(r.returncode, EXIT["install_failed"], r.stderr + r.log)
        self.assertEqual(self.version_of(install), BASE)
        self.assertEqual(sorted(os.listdir(install / "versions")), ["lock", "state"])
        self.assertEqual(self.run_launcher("--update-only", install=install, feed=self.publish()).returncode,
                         EXIT["install_failed"])
        shutil.rmtree(small)
        # a full user folder: the download fails. Linux only: under Wine 11 the launcher's settings-folder check
        # (unchanged since 0.1.7) refuses any user folder below a mount point, so no tmpfs can be one; the write
        # limits above cover a failing download there.
        if PLATFORM == "windows":
            return
        real_user, self.user = self.user, small_user_dir(ROOT, PLATFORM) / "u"
        self.user.mkdir()
        (self.user / "update-v2").mkdir()
        (self.user / "update-v2/stage-0123456789abcdef").mkdir()
        r = self.run_launcher("--update-only", feed=self.publish())
        self.assertIn(r.returncode, (EXIT["bad_package"], EXIT["install_failed"]), r.stderr + r.log)
        self.assertEqual(self.version_of(), BASE)
        self.assert_clean()
        shutil.rmtree(self.user)
        self.user = real_user

    def test_crash_at_each_step_then_recovery(self):
        feed = self.publish()
        for step in CRASH_STEPS:
            with self.subTest(step=step):
                if (self.install / "versions").exists():
                    shutil.rmtree(self.install / "versions")
                r = self.run_launcher("--update-only", feed=feed, BEA_UPDATE_TEST_CRASH_AT=step)
                self.assertIn(r.returncode, (-9, 137), step + r.stderr)
                self.assertIn(f"test crash at {step.split(':')[0]}", r.stderr)
                switched = step == "switched"
                self.assertEqual(self.version_of(), VERSIONS[1] if switched else BASE)
                r = self.run_launcher("--update-only", feed=feed)
                self.assertEqual(r.returncode, EXIT["current" if switched else "updated"], step + r.stderr + r.log)
                self.assertEqual(self.version_of(), VERSIONS[1])
                self.assert_clean()

    def test_crash_after_switch_of_second_update(self):
        for step in ("state-temp-written:1", "state-temp-written:2", "switched"):
            with self.subTest(step=step):
                shutil.rmtree(self.install)
                self.unpack_base(self.parent)
                self.update(VERSIONS[1])
                feed = self.publish(VERSIONS[2])
                r = self.run_launcher("--update-only", feed=feed, BEA_UPDATE_TEST_CRASH_AT=step)
                self.assertIn(r.returncode, (-9, 137))
                self.assertIn(self.version_of(), (VERSIONS[1], VERSIONS[2]))
                self.assertIn(self.run_launcher("--update-only", feed=feed).returncode, (EXIT["updated"], EXIT["current"]))
                self.assertEqual(self.version_of(), VERSIONS[2])
                self.assert_clean()

    def test_replaced_or_foreign_entries_are_never_deleted(self):
        self.update(VERSIONS[1])
        old = self.state()["active"]
        self.update(VERSIONS[2])
        # the entry the next update would remove: moved away and replaced by a link to a victim folder
        aside = self.case / "aside"  # test-made, outside the watched neighbourhood
        aside.mkdir()
        moved = aside / "moved-entry"
        victim = aside / "victim"
        victim.mkdir()
        (victim / "precious.txt").write_text("keep")
        shutil.move(str(self.install / "versions" / old), moved)
        if PLATFORM == "linux":
            (self.install / "versions" / old).symlink_to(victim)
        else:
            self.assertTrue(self.mklink(self.install / "versions" / old, victim), "Wine made no junction")
        # a foreign folder that looks like an entry, never recorded
        foreign = self.install / "versions" / "v9.9.9-0000000000000000"
        foreign.mkdir()
        (foreign / "f.txt").write_text("foreign")
        snapshot = tree(victim, moved, foreign)
        r = self.update(VERSIONS[3])
        self.assertIn("is no longer the folder this updater made", r.log)
        self.assertEqual(tree(victim, moved, foreign), snapshot)
        self.assertTrue(any(n.startswith(old) for n in os.listdir(self.install / "versions")))  # the link/junction stays
        self.assertNotIn(old, self.state()["entries"])

    def test_foreign_file_inside_old_entry_keeps_it(self):
        self.update(VERSIONS[1])
        old = self.state()["active"]
        self.update(VERSIONS[2])
        target = self.case / "planted-target"
        target.mkdir()
        (target / "x.txt").write_text("x")
        planted = self.install / "versions" / old / "planted-link"
        planted.symlink_to(target)
        r = self.update(VERSIONS[3])
        self.assertIn(f"could not remove the old version folder {old}", r.log)
        self.assertTrue((self.install / "versions" / old).is_dir())
        self.assertTrue(planted.is_symlink())
        self.assertEqual((target / "x.txt").read_text(), "x")
        self.assertIn(old, self.state()["entries"])

    def test_active_entry_replaced_or_state_corrupt(self):
        self.update(VERSIONS[1])
        active = self.install / "versions" / self.state()["active"]
        shutil.move(str(active), self.case / "moved-active")
        active.symlink_to(self.case / "moved-active")
        self.assertEqual(self.version_of(), BASE)  # no handoff through a link
        r = self.run_launcher("--version")
        self.assertIn("incomplete or changed", r.stderr)
        active.unlink()
        shutil.move(str(self.case / "moved-active"), active)
        self.assertEqual(self.version_of(), VERSIONS[1])
        state = self.install / "versions/state"
        state.write_text(state.read_text().replace("entry ", "entry? "))
        before = tree(self.install / "versions")
        self.assertEqual(self.version_of(), BASE)
        r = self.run_launcher("--update-only", feed=self.publish(VERSIONS[2]))
        self.assertEqual(r.returncode, EXIT["install_failed"], r.stderr + r.log)
        self.assertEqual(tree(self.install / "versions"), before)

    def test_foreign_versions_folder_is_not_adopted(self):
        (self.install / "versions").mkdir()
        (self.install / "versions/mine.txt").write_text("player's")
        r = self.run_launcher("--update-only", feed=self.publish())
        self.assertEqual(r.returncode, EXIT["install_failed"], r.stderr + r.log)
        self.assertEqual(sorted(os.listdir(self.install / "versions")), ["lock", "mine.txt"])

    def test_unsafe_archive_members_are_refused(self):
        info = INFO["packages"][f"{PLATFORM}/{VERSIONS[1]}"]
        top = info["top"]
        for kind in ("dotdot", "absolute", "link"):
            with self.subTest(kind):
                buf = io.BytesIO()
                if PLATFORM == "linux":
                    with tarfile.open(info["file"]) as src, tarfile.open(fileobj=buf, mode="w:gz", format=tarfile.GNU_FORMAT) as t:
                        for m in src.getmembers():
                            t.addfile(m, src.extractfile(m))
                        evil = tarfile.TarInfo({"dotdot": f"{top}/../../escaped.txt", "absolute": f"/{top}/abs.txt",
                                                "link": f"{top}/evil-link"}[kind])
                        if kind == "link":
                            evil.type, evil.linkname = tarfile.SYMTYPE, str(self.outside / "target-dir")
                            t.addfile(evil)
                        else:
                            evil.size = 3
                            t.addfile(evil, io.BytesIO(b"bad"))
                else:
                    with zipfile.ZipFile(info["file"]) as src, zipfile.ZipFile(buf, "w") as z:
                        for n in src.namelist():
                            z.writestr(src.getinfo(n), src.read(n))
                        zi = zipfile.ZipInfo({"dotdot": f"{top}/../../escaped.txt", "absolute": f"/{top}/abs.txt",
                                              "link": f"{top}/evil-link"}[kind])
                        if kind == "link":
                            zi.external_attr = (0o120777 << 16)
                        z.writestr(zi, b"bad" if kind != "link" else str(self.outside / "target-dir"))
                data = buf.getvalue()
                name = f"evil-{kind}.{self.ext}"
                pkg = Path(ROOT / "www" / name)
                pkg.write_bytes(data)
                saved = info["file"]
                info["file"] = str(pkg)
                try:
                    r = self.run_launcher("--update-only", feed=self.publish())
                finally:
                    info["file"] = saved
                self.assertEqual(r.returncode, EXIT["install_failed"], kind + r.stderr + r.log)
                self.assertEqual(self.version_of(), BASE)
                self.assertFalse((self.case / "escaped.txt").exists() or (self.parent / "escaped.txt").exists())
                self.assert_clean()

    def test_junction_neighbour_survives_update_under_wine(self):
        if PLATFORM != "windows":
            self.skipTest("Windows only")
        link, made = self.junction
        self.assertTrue(made, "Wine could not make a junction neighbour")
        self.update()
        self.assertTrue(self.mklink.__self__ and self.junction_listed(link))
        self.assertEqual((self.outside / "junction-target/keep.txt").read_text(), "junction target\n")

    def junction_listed(self, link: Path) -> bool:
        r = subprocess.run(["wine", "cmd", "/c", "dir", winpath(link.parent)], env=self.wine_env(), capture_output=True,
                           text=True, timeout=120)
        return any("<JUNCTION>" in l and l.split("<JUNCTION>")[1].split()[0] == link.name for l in r.stdout.splitlines())

    # -- review fixes --

    def test_running_version_is_never_removed(self):
        game = os.environ.get("UPDV2_GAME")
        if not game:
            self.skipTest("no game install to pass the launcher's game-data check")
        self.update(VERSIONS[1])
        running = self.state()["active"]
        mark = self.case / "game-running"
        e = {"BEA_GAME_DIR": game, "STANDIN_SLEEP": 60, "BEA_NO_UPDATE_CHECK": 1,
             "STANDIN_MARK": winpath(mark) if PLATFORM == "windows" else mark}
        proc = self.start_launcher(**e)
        try:
            deadline = time.time() + 60
            while not mark.exists() and time.time() < deadline:
                time.sleep(0.5)
            self.assertTrue(mark.exists(), "the stand-in game did not start")
            self.update(VERSIONS[2])
            r = self.update(VERSIONS[3])  # 0.1.8.1 is now neither active nor previous, but it is running
            self.assertIn(f"the old version folder {running} is in use", r.log)
            self.assertTrue((self.install / "versions" / running).is_dir())
            self.assertIn(running, self.state()["entries"])
            self.assertIsNone(proc.poll(), "the game ended before the checks; the test proved nothing")
        finally:
            proc.wait(timeout=120)
        r = self.run_launcher("--update-only", feed=self.publish(VERSIONS[3]))
        self.assertEqual(r.returncode, EXIT["current"])
        # the next install's retention removes it once nothing runs from it
        self.assertTrue((self.install / "versions" / running).is_dir())

    def test_copied_versions_folder_is_refused_with_recovery_advice(self):
        self.update(VERSIONS[1])
        versions = self.install / "versions"
        shutil.copytree(versions, self.case / "copy", symlinks=True)
        shutil.move(str(versions), self.case / "orig")
        shutil.move(str(self.case / "copy"), versions)  # same names and contents, new inodes (a restore or copy)
        r = self.run_launcher("--version")
        self.assertEqual(r.stdout.split()[2].lstrip("vV").rstrip(","), BASE)
        self.assertIn("move the versions folder out of the Onslaught CE folder", r.stderr)
        before = tree(versions)
        n = len(self.server.requests)
        r = self.run_launcher("--update-only", feed=self.publish(VERSIONS[2]))
        self.assertEqual(r.returncode, EXIT["install_failed"], r.stderr + r.log)
        self.assertIn("move the versions folder out of the Onslaught CE folder", r.log)
        self.assertEqual(self.server.requests[n:], [])
        self.assertEqual(tree(versions), before)
        shutil.move(str(versions), self.case / "moved-out")  # the advised recovery
        self.assertEqual(self.run_launcher("--update-only", feed=self.publish(VERSIONS[2])).returncode, EXIT["updated"])
        self.assertEqual(self.version_of(), VERSIONS[2])

    def test_rollback_to_base_is_not_reoffered_and_no_check_makes_no_request(self):
        self.update(VERSIONS[1])
        self.assertEqual(self.run_launcher("--rollback").returncode, 0)
        self.assertEqual(self.version_of(), BASE)
        n = len(self.server.requests)
        r = self.run_launcher("--update-only", feed=self.publish(VERSIONS[1]))
        self.assertEqual(r.returncode, EXIT["current"], r.stderr + r.log)
        self.assertIn("already installed in versions; --rollback switches to it", r.log)
        self.assertFalse(any(q.endswith(self.ext) for q in self.server.requests[n:]))
        self.assertEqual(self.version_of(), BASE)
        n = len(self.server.requests)
        self.assertEqual(self.run_launcher("--no-update-check", "--update-only", feed=self.publish(VERSIONS[2])).returncode,
                         EXIT["disabled"])
        self.assertEqual(self.server.requests[n:], [])
        self.assert_clean()

    def read_log(self):
        p = self.user / "onslaught-ce.log"
        return p.read_text(errors="replace") if p.exists() else ""

    def start_launcher(self, **env):
        """A normal start in the background; the caller waits for the recorded process."""
        e = {"PATH": "/usr/bin:/bin", "HOME": str(ROOT / "home"), "BEA_SKIP_PREFLIGHT": "1"}
        e.update({k: str(v) for k, v in env.items()})
        if PLATFORM == "linux":
            e["BEA_USER_DIR"] = str(self.user)
            cmd = [str(self.install / "onslaught-ce")]
        else:
            e.update(self.wine_env())
            e["BEA_USER_DIR"] = winpath(self.user)
            e["BEA_GAME_DIR"] = winpath(e["BEA_GAME_DIR"])
            cmd = ["wine", str(self.install / "onslaught-ce.exe")]
        return subprocess.Popen(cmd, env=e, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    # -- adversarial review cases --

    def link_dir(self, link: Path, target: Path):
        """A directory link: a symlink on Linux, a Wine junction under Windows."""
        if PLATFORM == "linux":
            link.symlink_to(target)
        else:
            self.assertTrue(self.mklink(link, target), "Wine made no junction")

    def link_present(self, link: Path) -> bool:
        """Wine 11 keeps a junction under a marked Unix name, so ask Windows."""
        return os.path.lexists(link) if PLATFORM == "linux" else self.junction_listed(link)

    def unlink_dir(self, link: Path):
        if PLATFORM == "linux":
            link.unlink()
        else:
            subprocess.run(["wine", "cmd", "/c", "rmdir", winpath(link)], env=self.wine_env(), capture_output=True,
                           timeout=120)
            self.assertFalse(self.junction_listed(link), "junction not removed")

    def victims(self) -> Path:
        """A folder of precious files outside everything the updater owns, watched by tearDown via self.outside."""
        v = self.outside / "victim"
        v.mkdir(exist_ok=True)
        (v / "precious.txt").write_text("precious\n")
        (v / "feed.json").write_text("not a stage file\n")
        (v / "package").write_text("not a stage file\n")
        self.before.update(tree(self.outside))  # victims are planted after setUp's snapshot; tearDown checks them too
        return v

    def publish_raw(self, feed: bytes, sig: str = None, key="test") -> str:
        name = f"raw-{self.id().split('.')[-1]}-{hashlib.sha256(feed).hexdigest()[:12]}.json"
        (ROOT / "www" / name).write_bytes(feed)
        (ROOT / "www" / (name + ".sig")).write_text(sig if sig is not None else KEYS[key].sign(DOMAIN + feed).hex() + "\n")
        return self.server.url(name)

    def offer(self, version=VERSIONS[1], **over) -> dict:
        info = INFO["packages"][f"{PLATFORM}/{version}"]
        real = Path(info["file"]).read_bytes()
        name = f"onslaught-ce-{version}-{self.pname}.{self.ext}"
        (ROOT / "www" / name).write_bytes(real)
        pkg = {"url": self.server.url(name), "sha256": hashlib.sha256(real).hexdigest(), "size": len(real)}
        pkg.update(over)
        return {"format": "onslaught-ce-update-v2", "version": version, "packages": {self.pname: pkg}}

    def test_attack_versions_journal_names_outside_paths(self):
        """versions/lock's journal naming absolute, '..' or drive paths removes nothing anywhere."""
        self.update(VERSIONS[1])
        v = self.victims()
        names = ["../../outside/victim", "..", ".", "/" + str(v).lstrip("/"), str(v), "..\\..\\outside\\victim",
                 "C:\\updv2-cases", "../" + self.install.name, "~", "victim/precious.txt"]
        lines = ["onslaught-ce-updater-journal 2"] + [f"{w} {n}" for n in names for w in ("tmp", "dir")] + [""]
        (self.install / "versions/lock").write_text("\n".join(lines))
        self.update(VERSIONS[2])
        self.assertEqual(self.version_of(), VERSIONS[2])
        self.assert_clean()

    def test_attack_versions_journal_tmp_naming_state_keeps_state(self):
        """A journal whose tmp line names the real state file must not delete it (UPDATER-V2.md: only a journaled
        state.new-*)."""
        self.update(VERSIONS[1])
        state = (self.install / "versions/state").read_bytes()
        (self.install / "versions/lock").write_text("onslaught-ce-updater-journal 2\ntmp state\n")
        r = self.run_launcher("--update-only", feed=self.publish(VERSIONS[2]))
        self.assertTrue((self.install / "versions/state").exists(), "journal 'tmp state' deleted versions/state: " + r.log)
        self.assertEqual(r.returncode, EXIT["updated"], r.stderr + r.log)

    def test_attack_stage_journal_and_links(self):
        """update-v2/lock journals naming outside paths, a stage that is a link, and an update-v2 that is a link."""
        v = self.victims()
        feed = self.publish()
        self.assertEqual(self.run_launcher("--update-only", feed=feed, answer="later").returncode, EXIT["not_now"])
        lock = self.user / "update-v2/lock"
        for n in ("../../outside/victim", "..", str(v), "C:\\updv2-cases\\x", "../update", "../ce.ini"):
            for idline in ("", " 1 1"):
                with self.subTest(name=n, ids=bool(idline)):
                    lock.write_text(f"onslaught-ce-updater-journal 2\nstage {n}{idline}\n")
                    self.assertEqual(self.run_launcher("--update-only", feed=feed, answer="later").returncode,
                                     EXIT["not_now"])
        # a journaled stage folder that is a link to the victim (with its real identity when one can be read)
        link = self.user / "update-v2/stage-2222222222222222"
        self.link_dir(link, v)
        st = os.stat(v)
        for idline in ("", f" {st.st_dev} {st.st_ino}"):
            lock.write_text(f"onslaught-ce-updater-journal 2\nstage stage-2222222222222222{idline}\n")
            self.assertEqual(self.run_launcher("--update-only", feed=feed, answer="later").returncode, EXIT["not_now"])
            self.assertTrue(self.link_present(link))
        self.unlink_dir(link)
        # update-v2 itself a link to a folder holding a journal that names a victim-like stage
        real = self.case / "aside-update-v2"
        shutil.move(str(self.user / "update-v2"), real)
        fake = self.outside / "fake-update-v2"
        fake.mkdir()
        (fake / "stage-3333333333333333").mkdir()
        for f in ("feed.json", "package", "precious.txt"):
            (fake / "stage-3333333333333333" / f).write_text("keep\n")
        sid = os.stat(fake / "stage-3333333333333333")
        (fake / "lock").write_text(f"onslaught-ce-updater-journal 2\nstage stage-3333333333333333 {sid.st_dev} {sid.st_ino}\n")
        self.before.update(tree(self.outside))
        self.link_dir(self.user / "update-v2", fake)
        snap = tree(fake)
        r = self.run_launcher("--update-only", feed=feed)
        self.assertEqual(r.returncode, EXIT["install_failed"], r.stderr + r.log)
        self.assertEqual(tree(fake), snap)
        self.unlink_dir(self.user / "update-v2")
        shutil.move(str(real), self.user / "update-v2")

    def test_attack_versions_folder_is_a_link_to_a_valid_state(self):
        """A valid versions/ moved aside and replaced by a link/junction: no handoff, no install, no rollback, and
        nothing behind the link changes."""
        self.update(VERSIONS[1])
        self.update(VERSIONS[2])  # the 0.1.8.1 entry is now 'previous'; one more update would remove nothing yet
        aside = self.outside / "real-versions"
        shutil.move(str(self.install / "versions"), aside)
        self.link_dir(self.install / "versions", aside)
        snap = tree(aside)
        self.assertEqual(self.version_of(), BASE)
        r = self.run_launcher("--update-only", feed=self.publish(VERSIONS[3]))
        self.assertEqual(r.returncode, EXIT["install_failed"], r.stderr + r.log)
        self.assertIn(self.run_launcher("--rollback").returncode, (EXIT["current"], EXIT["install_failed"]))
        self.assertEqual(tree(aside), snap)
        self.unlink_dir(self.install / "versions")
        shutil.move(str(aside), self.install / "versions")
        self.assertEqual(self.version_of(), VERSIONS[2])

    def test_attack_crafted_state_entries(self):
        """State lines naming '..', absolute or link entries make the state foreign: nothing is removed."""
        self.update(VERSIONS[1])
        state = self.install / "versions/state"
        good = state.read_text()
        v = self.victims()
        st = os.stat(v)
        for bad in (f"entry ../../outside/victim {st.st_dev} {st.st_ino} 0.1.8.1 ready",
                    f"entry {v} {st.st_dev} {st.st_ino} 0.1.8.1 ready", f"entry .. {st.st_dev} {st.st_ino} 0.1.8.1 ready",
                    f"entry victim {st.st_dev} {st.st_ino} 0.1.8.1 ready"):
            with self.subTest(bad):
                state.write_text(good + bad + "\n")
                if bad.startswith("entry victim"):
                    self.link_dir(self.install / "versions/victim", v)
                r = self.run_launcher("--update-only", feed=self.publish(VERSIONS[2]))
                self.assertIn(r.returncode, (EXIT["install_failed"], EXIT["updated"]), r.stderr + r.log)
                if bad.startswith("entry victim"):
                    self.assertTrue(self.link_present(self.install / "versions/victim"))
                    self.unlink_dir(self.install / "versions/victim")
                shutil.rmtree(self.install / "versions")
                self.update(VERSIONS[1])
                good = state.read_text()

    def test_attack_hardlink_and_special_files_inside_old_entry(self):
        """An old entry due for removal holding a hard link to an outside file (and on Linux a FIFO): the outside
        file's content survives; a FIFO keeps the entry."""
        self.update(VERSIONS[1])
        old = self.state()["active"]
        self.update(VERSIONS[2])
        v = self.victims()
        os.link(v / "precious.txt", self.install / "versions" / old / "hardlink.txt")
        fifo = self.install / "versions" / old / "fifo"
        if PLATFORM == "linux":
            os.mkfifo(fifo)
        try:
            r = self.update(VERSIONS[3])
            self.assertEqual((v / "precious.txt").read_text(), "precious\n")
            if PLATFORM == "linux":  # the FIFO keeps the whole entry, hard link included
                self.assertIn(f"could not remove the old version folder {old}", r.log)
                self.assertTrue((self.install / "versions" / old / "hardlink.txt").exists())
        finally:
            if os.path.lexists(fifo):
                os.unlink(fifo)  # tearDown's tree() would block opening it

    def test_attack_bind_mount_inside_old_entry(self):
        """Linux: an old entry due for removal holding a same-file-system bind mount of a victim folder. The
        removal must not descend into the mount."""
        if PLATFORM != "linux":
            self.skipTest("Linux only (Windows mount points are reparse points, refused)")
        if not shutil.which("bwrap"):
            self.skipTest("no bwrap for a nested mount namespace")
        self.update(VERSIONS[1])
        old = self.state()["active"]
        self.update(VERSIONS[2])
        v = self.victims()
        mnt = self.install / "versions" / old / "mnt"
        mnt.mkdir()
        feed = self.publish(VERSIONS[3])
        e = {"PATH": "/usr/bin:/bin", "HOME": str(ROOT / "home"), "BEA_SKIP_PREFLIGHT": "1", "BEA_UPDATE_V2_FEED": feed,
             "BEA_UPDATE_ANSWER": "update", "BEA_USER_DIR": str(self.user)}
        cmd = ["bwrap", "--dev-bind", "/", "/", "--bind", str(v), str(mnt), str(self.install / "onslaught-ce"),
               "--update-only"]
        r = subprocess.run(cmd, env=e, capture_output=True, text=True, timeout=180)
        log = (self.user / "onslaught-ce.log").read_text(errors="replace")
        self.assertNotEqual(r.returncode, 1, r.stderr)  # nested bwrap ran the launcher
        self.assertEqual(sorted(os.listdir(v)), ["feed.json", "package", "precious.txt"],
                         "removal descended into a bind mount: " + log[-2000:])

    def retire_setup(self):
        """0.1.8.1 then 0.1.8.2 installed: returns the 0.1.8.1 entry, which the next install's retention removes."""
        self.update(VERSIONS[1])
        old = self.state()["active"]
        self.update(VERSIONS[2])
        return old

    def test_attack_mount_raced_in_and_out_during_removal(self):
        """Linux, re-review: a user+mount namespace bind-mounts a victim folder (and a victim file) in and out of the
        entry being removed, as fast as it can, while the launcher removes it. The victims must survive every round."""
        if PLATFORM != "linux":
            self.skipTest("Linux only")
        v = self.victims()
        outcomes = []
        for rnd in range(4):
            with self.subTest(round=rnd):
                if (self.install / "versions").exists():
                    shutil.rmtree(self.install / "versions")
                old = self.retire_setup()
                entry = self.install / "versions" / old
                top = INFO["packages"][f"{PLATFORM}/{VERSIONS[1]}"]["top"]
                sub = entry / top / "lib"   # a real folder of the entry, deep enough to be reached mid-walk
                fsub = entry / top / "README.txt"
                feed = self.publish(VERSIONS[3])
                script = (
                    'L="$1"; shift; "$L" --update-only & P=$!\n'
                    'while kill -0 $P 2>/dev/null; do\n'
                    '  mount --bind "$VICTIM" "$SUB" 2>/dev/null; mount --bind "$VICTIM/precious.txt" "$FSUB" 2>/dev/null\n'
                    '  umount "$FSUB" 2>/dev/null; umount "$SUB" 2>/dev/null\n'
                    'done\nwait $P; echo "launcher rc=$?"\n')
                e = {"PATH": "/usr/bin:/bin", "HOME": str(ROOT / "home"), "BEA_SKIP_PREFLIGHT": "1",
                     "BEA_UPDATE_V2_FEED": feed, "BEA_UPDATE_ANSWER": "update", "BEA_USER_DIR": str(self.user),
                     "VICTIM": str(v), "SUB": str(sub), "FSUB": str(fsub)}
                r = subprocess.run(["unshare", "-r", "-m", "sh", "-c", script, "race", str(self.install / "onslaught-ce")],
                                   env=e, capture_output=True, text=True, timeout=300)
                self.assertIn("launcher rc=0", r.stdout, r.stdout + r.stderr)
                self.assertEqual(sorted(os.listdir(v)), ["feed.json", "package", "precious.txt"], r.stdout)
                self.assertEqual((v / "precious.txt").read_text(), "precious\n")
                log = (self.user / "onslaught-ce.log").read_text(errors="replace")
                outcomes.append("removed" if f"removed the old version folder {old}" in log else "kept")
        print(f"\n[mount race] retention outcomes per round: {outcomes}", file=sys.stderr)

    def test_attack_statx_unavailable_or_incomplete(self):
        """Linux, re-review: with statx failing (ENOSYS) or lacking STATX_MNT_ID/MOUNT_ROOT (pre-5.8), nothing is
        removed, with or without a mount inside; the install itself still succeeds; the log says why."""
        if PLATFORM != "linux":
            self.skipTest("Linux only")
        shim = ROOT / "statx-shim.so"
        if not shim.exists():
            self.skipTest("no statx-shim.so (built outside the sandbox from tests/statx_shim.c)")
        v = self.victims()
        for mode in ("enosys", "nomnt"):
            for mounted in (False, True):
                with self.subTest(mode=mode, mounted=mounted):
                    if (self.install / "versions").exists():
                        shutil.rmtree(self.install / "versions")
                    old = self.retire_setup()
                    snap = tree(self.install / "versions" / old)
                    mnt = self.install / "versions" / old / "mnt"
                    mnt.mkdir()
                    feed = self.publish(VERSIONS[3])
                    e = {"PATH": "/usr/bin:/bin", "HOME": str(ROOT / "home"), "BEA_SKIP_PREFLIGHT": "1",
                         "BEA_UPDATE_V2_FEED": feed, "BEA_UPDATE_ANSWER": "update", "BEA_USER_DIR": str(self.user),
                         "LD_PRELOAD": str(shim), "STATX_SHIM": mode}
                    cmd = [str(self.install / "onslaught-ce"), "--update-only"]
                    if mounted:
                        cmd = ["bwrap", "--dev-bind", "/", "/", "--bind", str(v), str(mnt)] + cmd
                    r = subprocess.run(cmd, env=e, capture_output=True, text=True, timeout=180)
                    log = (self.user / "onslaught-ce.log").read_text(errors="replace")
                    self.assertEqual(r.returncode, EXIT["updated"], r.stderr + log[-1500:])
                    self.assertEqual(sorted(os.listdir(v)), ["feed.json", "package", "precious.txt"], log[-1500:])
                    self.assertTrue((self.install / "versions" / old).is_dir(), log[-1500:])
                    self.assertIn(f"could not remove the old version folder {old}", log)
                    self.assertTrue("statx" in log or "mount points" in log, log[-800:])
                    mnt.rmdir()
                    self.assertEqual(tree(self.install / "versions" / old), snap, "something in the kept entry changed")

    def test_attack_no_handoff_base_downgrades_active(self):
        """With 0.1.8.3 active, the base run with BEA_CE_NO_HANDOFF=1 must not install an older 0.1.8.1 over it."""
        self.update(VERSIONS[3])
        r = self.run_launcher("--update-only", feed=self.publish(VERSIONS[1]), BEA_CE_NO_HANDOFF=1)
        self.assertEqual(self.version_of(), VERSIONS[3], "active version went backwards: " + r.log[-1500:])

    def test_attack_signed_feed_rules(self):
        """Signed feeds that break a rule are refused before any package request."""
        o = self.offer()
        pkg = o["packages"][self.pname]
        name = Path(pkg["url"]).name
        bad = {
            "foreign host": self.offer(url=f"https://example.invalid/owner/onslaught-ce/releases/download/v0.1.8.1/{name}"),
            "other repo": self.offer(url=f"https://github.com/evil/onslaught-ce/releases/download/v0.1.8.1/{name}"),
            "query": self.offer(url=pkg["url"] + "?x=1"),
            "dotdot": self.offer(url=pkg["url"].replace("/files/", "/files/../files/")),
            "encoded": self.offer(url=pkg["url"].replace("/files/", "/files%2f")),
            "other platform": self.offer(url=pkg["url"].replace(self.pname, "linux-x86_64")),
            "size zero": self.offer(size=0),
            "size huge": self.offer(size=(512 << 20) + 1),
            "size negative": None, "size float": None,
            "sha upper": self.offer(sha256=pkg["sha256"].upper()),
            "sha short": self.offer(sha256=pkg["sha256"][:63]),
            "version five parts": dict(o, version="0.1.8.1.1"),
            "version prefix": dict(o, version="v0.1.8.1"),
            "version digits": dict(o, version="0.1.123456"),
        }
        raw = {k: (json.dumps(v, indent=1) + "\n").encode() for k, v in bad.items() if v is not None}
        good = (json.dumps(o, indent=1) + "\n").encode()
        raw["size negative"] = good.replace(f'"size": {pkg["size"]}'.encode(), b'"size": -1')
        raw["size float"] = good.replace(f'"size": {pkg["size"]}'.encode(), f'"size": {pkg["size"]}.0'.encode())
        raw["duplicate version"] = good.replace(b'"version":', b'"version": "9.9.9", "version":', 1)
        raw["duplicate platform"] = good.replace(f'"{self.pname}":'.encode(), f'"{self.pname}": {{}}, "{self.pname}":'.encode(), 1)
        raw["escape"] = good.replace(b'"0.1.8.1"', b'"0.1.8.\\u0031"', 1)
        raw["trailing nul"] = good + b"\0"
        raw["trailing object"] = good + b"{}"
        raw["bom"] = b"\xef\xbb\xbf" + good
        sigs = {"upper hex": KEYS["test"].sign(DOMAIN + good).hex().upper() + "\n",
                "crlf": KEYS["test"].sign(DOMAIN + good).hex() + "\r\n",
                "two newlines": KEYS["test"].sign(DOMAIN + good).hex() + "\n\n",
                "leading space": " " + KEYS["test"].sign(DOMAIN + good).hex(),
                "empty": "", "no domain": KEYS["test"].sign(good).hex() + "\n",
                "domain without newline": KEYS["test"].sign(DOMAIN[:-1] + good).hex() + "\n"}
        for kind, feed in raw.items():
            with self.subTest(feed=kind):
                n = len(self.server.requests)
                r = self.run_launcher("--update-only", feed=self.publish_raw(feed))
                self.assertIn(r.returncode, (EXIT["bad_feed"], EXIT["current"]), kind + r.stderr + r.log)
                self.assertFalse(any(p.endswith(self.ext) for p in self.server.requests[n:]), kind)
        for kind, sig in sigs.items():
            with self.subTest(sig=kind):
                n = len(self.server.requests)
                r = self.run_launcher("--update-only", feed=self.publish_raw(good, sig=sig))
                self.assertEqual(r.returncode, EXIT["bad_feed"], kind + r.stderr + r.log)
                self.assertFalse(any(p.endswith(self.ext) for p in self.server.requests[n:]), kind)
        self.assertFalse((self.install / "versions").exists())
        # control: the unmodified feed is accepted
        self.assertEqual(self.run_launcher("--update-only", feed=self.publish_raw(good)).returncode, EXIT["updated"])

    def test_attack_more_archive_members(self):
        """Hard links, FIFOs, devices, pax headers, duplicate names, a GNU long name with '..', file/dir clashes;
        Windows: backslashes, reserved device names, trailing dots, stream names, case collisions."""
        info = INFO["packages"][f"{PLATFORM}/{VERSIONS[1]}"]
        top = info["top"]
        kinds = (("hardlink", "fifo", "chardev", "pax", "dup", "longdotdot", "fileThenDir", "dirThenFile")
                 if PLATFORM == "linux" else ("backslash", "reserved", "reserved2", "trailingdot", "stream", "case",
                                              "dup", "dirsym"))
        for kind in kinds:
            with self.subTest(kind):
                buf = io.BytesIO()
                if PLATFORM == "linux":
                    fmt = tarfile.PAX_FORMAT if kind == "pax" else tarfile.GNU_FORMAT
                    with tarfile.open(info["file"]) as src, tarfile.open(fileobj=buf, mode="w:gz", format=fmt) as t:
                        for m in src.getmembers():
                            t.addfile(m, src.extractfile(m))
                        def add(name, data=b"bad", typ=tarfile.REGTYPE, **kw):
                            ti = tarfile.TarInfo(name); ti.type = typ
                            for k2, v2 in kw.items():
                                setattr(ti, k2, v2)
                            if typ == tarfile.REGTYPE:
                                ti.size = len(data); t.addfile(ti, io.BytesIO(data))
                            else:
                                t.addfile(ti)
                        if kind == "hardlink":
                            add(f"{top}/hl", typ=tarfile.LNKTYPE, linkname=str(self.outside / "target-file.txt"))
                        elif kind == "fifo":
                            add(f"{top}/fifo", typ=tarfile.FIFOTYPE)
                        elif kind == "chardev":
                            add(f"{top}/null", typ=tarfile.CHRTYPE, devmajor=1, devminor=3)
                        elif kind == "pax":
                            add(f"{top}/" + "p" * 120 + ".txt", pax_headers={"path": f"{top}/../../pax-escape.txt"})
                        elif kind == "dup":
                            add(f"{top}/README.txt", b"second copy")
                        elif kind == "longdotdot":
                            add(f"{top}/" + "a" * 120 + "/../../../longname-escape.txt")
                        elif kind == "fileThenDir":
                            add(f"{top}/clash"); add(f"{top}/clash", typ=tarfile.DIRTYPE)
                        elif kind == "dirThenFile":
                            add(f"{top}/clashdir", typ=tarfile.DIRTYPE); add(f"{top}/clashdir")
                else:
                    with zipfile.ZipFile(info["file"]) as src, zipfile.ZipFile(buf, "w") as z:
                        for n in src.namelist():
                            z.writestr(src.getinfo(n), src.read(n))
                        name = {"backslash": f"{top}\\..\\..\\bs-escape.txt", "reserved": f"{top}/con.txt",
                                "reserved2": f"{top}/LPT1", "trailingdot": f"{top}/evil.", "stream": f"{top}/x.txt:ads",
                                "case": f"{top}/readme.TXT", "dup": f"{top}/README.txt", "dirsym": f"{top}/d"}[kind]
                        zi = zipfile.ZipInfo(name)
                        if kind == "dirsym":
                            zi.external_attr = (0o120777 << 16)
                        z.writestr(zi, b"bad")
                pkg = Path(ROOT / "www" / f"evil2-{kind}.{self.ext}")
                pkg.write_bytes(buf.getvalue())
                saved = info["file"]
                info["file"] = str(pkg)
                try:
                    r = self.run_launcher("--update-only", feed=self.publish())
                finally:
                    info["file"] = saved
                self.assertEqual(r.returncode, EXIT["install_failed"], kind + r.stderr + r.log)
                self.assertEqual(self.version_of(), BASE)
                for esc in ("pax-escape.txt", "longname-escape.txt", "bs-escape.txt"):
                    self.assertFalse((self.case / esc).exists() or (self.parent / esc).exists(), esc)
                self.assert_clean()


def free_port() -> int:
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


class OldLaunchers(unittest.TestCase):
    """Every launcher released before 0.1.8 fetches only latest.json (0.1.7: no feed) and none can reach the v2 feed;
    from 0.1.8 on, the released launchers read the v2 feed and never latest.json."""

    def test_old_tags_fetch_only_latest_json(self):
        tags = INFO["old_tags"]
        latest = '#define FEED "https://github.com/dlprentice/onslaught-ce/releases/latest/download/latest.json"'
        published = {f"onslaught-ce.git:v0.1.{n}" for n in range(7)}
        self.assertLessEqual(published, set(tags), "clone the public onslaught-ce into the test root (see prepare)")
        for t, v in tags.items():
            minor = int(t.rsplit("v0.1.", 1)[1].split(".")[0])
            if minor <= 6:
                self.assertEqual(v["feed"], [latest], t)
            if minor <= 7:
                self.assertFalse(v["mentions_v2"], t)
            else:
                self.assertTrue(v["mentions_v2"], t)
                self.assertNotIn(latest, v["feed"], t)
        affected = sorted(t for t in tags if int(t.rsplit("v0.1.", 1)[1].split(".")[0]) <= 6)
        print(f"\n[old tags] {len(affected)} affected tag sources read, all fetch only latest.json: "
              f"{', '.join(affected)}; {len(tags) - len(affected)} later tags never fetch it", file=sys.stderr)


def main():
    global ROOT, PLATFORM, INFO
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", type=Path, default=PORT_DIR.parent / "local-data/updater-v2-tests")
    ap.add_argument("--platform", default="all", choices=["linux", "windows", "all"])
    ap.add_argument("--inside", action="store_true")
    ap.add_argument("--skip-build", action="store_true")
    ap.add_argument("-k", action="append", default=[])
    ap.add_argument("-v", action="store_true")
    a = ap.parse_args()
    ROOT = a.root.resolve()
    platforms = ["linux", "windows"] if a.platform == "all" else [a.platform]
    if not a.inside:
        info = json.loads((ROOT / "prepared.json").read_text()) if a.skip_build else prepare(ROOT, platforms)
        INFO = info
        rc = 0
        r = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(OldLaunchers))
        rc |= not r.wasSuccessful()
        for p in platforms:
            print(f"==== {p} (bwrap sandbox{', Wine' if p == 'windows' else ''})", flush=True)
            rc |= sandbox(ROOT, p, a.k, info) != 0
        return rc
    PLATFORM = a.platform
    INFO = json.loads((ROOT / "prepared.json").read_text())
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
    for name in ("test", "wrong"):
        KEYS[name] = Ed25519PrivateKey.from_private_bytes((ROOT / f"{name}.key").read_bytes())
    argv = [sys.argv[0], "-v"] + [x for k in a.k for x in ("-k", k)]
    prog = unittest.main(module=__name__, defaultTest="UpdaterV2", argv=argv, exit=False)
    return 0 if prog.result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
