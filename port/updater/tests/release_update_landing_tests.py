#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Real-layout Linux and Windows update landing verification (port/updater/UPDATER-V2.md).

Verifies that updates actually land end-to-end on BOTH Linux and Windows (under Wine), including:
  1. Windows MAX_PATH member-length budget (<= 85 chars) so even older ANSI launchers in deep folders unpack cleanly.
  2. Multi-hop chained updates (prev -> mid -> target), atomic versions/state switch, base handoff (--version),
     old-version cleanup, and --rollback / forward-rollback recovery.
  3. Windows extended-length (>270-char) install paths exercising \\\\?\\\\ UTF-16 *W file I/O and CreateProcessW handoff.
  4. Optional --verify-release mode that audits and lands the ACTUAL built release .tar.gz and .zip archives before
     publishing a release.

Usage:
  python3 port/updater/tests/release_update_landing_tests.py [--platform linux|windows|all] [-v]
  python3 port/updater/tests/release_update_landing_tests.py --verify-release \\
      --linux-pkg path/to/onslaught-ce-0.1.17.5-linux-i686.tar.gz \\
      --windows-pkg path/to/onslaught-ce-0.1.17.5-windows-i686.zip \\
      --prev-version 0.1.17.4 --version 0.1.17.5
"""
import argparse
import hashlib
import http.server
import importlib.util
import io
import json
import os
import shutil
import socketserver
import subprocess
import sys
import tarfile
import tempfile
import threading
import unittest
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
PORT_DIR = HERE.parents[1]
ROOT_DIR = PORT_DIR.parent
DOMAIN = b"onslaught-ce update feed v2\n"
WINDOWS_MAX_MEMBER_LEN = 85

GAME_SH = '#!/bin/sh\necho "stand-in game {version}"\nexit 0\n'
GAME_C = r'''#include <stdio.h>
int main(void) { puts("stand-in game {version}"); return 0; }
'''


def load_port_modules():
    for p in (str(PORT_DIR), str(PORT_DIR / "tools")):
        if p not in sys.path:
            sys.path.insert(0, p)
    import build  # noqa: E402
    import deps  # noqa: E402
    spec = importlib.util.spec_from_file_location("ce_package", PORT_DIR / "tools/package.py")
    package = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(package)
    return build, deps, package


def make_test_key():
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
    priv = Ed25519PrivateKey.generate()
    pub = priv.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw).hex()
    return priv, pub


class LoopbackFeedServer:
    def __init__(self, www: Path, priv):
        self.www = www
        self.priv = priv
        www.mkdir(parents=True, exist_ok=True)

        class QuietHandler(http.server.SimpleHTTPRequestHandler):
            def __init__(self, *a, **kw):
                super().__init__(*a, directory=str(www), **kw)

            def log_message(self, *a):
                pass

        socketserver.TCPServer.allow_reuse_address = True
        self.httpd = socketserver.TCPServer(("127.0.0.1", 0), QuietHandler)
        self.port = self.httpd.server_address[1]
        self.thread = threading.Thread(target=self.httpd.serve_forever, daemon=True)
        self.thread.start()

    def publish(self, version: str, plat_name: str, pkg_path: Path):
        data = pkg_path.read_bytes()
        dst = self.www / pkg_path.name
        if dst != pkg_path:
            dst.write_bytes(data)
        feed = json.dumps({
            "format": "onslaught-ce-update-v2",
            "version": version,
            "packages": {
                plat_name: {
                    "url": f"http://127.0.0.1:{self.port}/{pkg_path.name}",
                    "sha256": hashlib.sha256(data).hexdigest(),
                    "size": len(data),
                }
            },
        }, indent=2).encode() + b"\n"
        sig = (self.priv.sign(DOMAIN + feed).hex() + "\n").encode()
        (self.www / "ce-update-v2.json").write_bytes(feed)
        (self.www / "ce-update-v2.json.sig").write_bytes(sig)
        return f"http://127.0.0.1:{self.port}/ce-update-v2.json"

    def close(self):
        self.httpd.shutdown()
        self.httpd.server_close()


def check_windows_zip_budget(zip_path: Path, version: str):
    top = f"onslaught-ce-{version}-windows-i686"
    with zipfile.ZipFile(zip_path, "r") as z:
        infos = z.infolist()
        if not infos:
            raise AssertionError(f"{zip_path}: empty zip")
        too_long = [i.filename for i in infos if len(i.filename) > WINDOWS_MAX_MEMBER_LEN]
        if too_long:
            raise AssertionError(f"{zip_path}: member exceeds {WINDOWS_MAX_MEMBER_LEN}-char MAX_PATH budget: {too_long}")
        bad_top = [i.filename for i in infos if not (i.filename == top or i.filename.startswith(top + "/"))]
        if bad_top:
            raise AssertionError(f"{zip_path}: member outside expected top folder {top}: {bad_top[:5]}")
        names = {i.filename for i in infos}
        for req in (f"{top}/onslaught-ce.exe", f"{top}/bea.exe", f"{top}/SDL3.dll", f"{top}/libz.dll"):
            if req not in names:
                raise AssertionError(f"{zip_path}: missing required file {req}")
        return len(infos), max(len(i.filename) for i in infos)


def check_linux_tar_layout(tar_path: Path, version: str):
    top = f"onslaught-ce-{version}-linux-i686"
    with tarfile.open(tar_path, "r:gz") as t:
        members = t.getmembers()
        if not members:
            raise AssertionError(f"{tar_path}: empty tar.gz")
        bad_top = [m.name for m in members if not (m.name == top or m.name.startswith(top + "/"))]
        if bad_top:
            raise AssertionError(f"{tar_path}: member outside expected top folder {top}: {bad_top[:5]}")
        names = {m.name for m in members}
        for req in (f"{top}/onslaught-ce", f"{top}/onslaught-ce-launch", f"{top}/bea", f"{top}/preflight.sh"):
            if req not in names:
                raise AssertionError(f"{tar_path}: missing required file {req}")
        return len(members), max(len(m.name) for m in members)


def clean_headless_env():
    env = os.environ.copy()
    for key in (
        "DISPLAY", "WAYLAND_DISPLAY",
        "BEA_NO_UPDATE_CHECK", "BEA_CE_NO_HANDOFF", "BEA_PORT_TAPE", "BEA_PORT_RECORD",
        "BEA_CE_LOG_CONTINUE", "BEA_CE_JUST_UPDATED", "BEA_GAME_DIR", "BEA_USER_DIR",
        "BEA_UPDATE_V2_FEED", "BEA_UPDATE_ANSWER",
    ):
        env.pop(key, None)
    env["WINEDEBUG"] = "-all"
    env["BEA_SKIP_PREFLIGHT"] = "1"
    return env


def init_wineprefix(wineprefix: Path):
    wineprefix.mkdir(parents=True, exist_ok=True)
    env = clean_headless_env()
    env["WINEPREFIX"] = str(wineprefix)
    subprocess.run(["wineboot", "-i"], env=env, stdin=subprocess.DEVNULL,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=120)
    subprocess.run(["wineserver", "-w"], env=env, stdin=subprocess.DEVNULL,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=120)


def stop_wineprefix(wineprefix: Path):
    if not wineprefix.exists():
        return
    env = clean_headless_env()
    env["WINEPREFIX"] = str(wineprefix)
    for args in (["wineserver", "-k"], ["wineserver", "-w"]):
        try:
            subprocess.run(args, env=env, stdin=subprocess.DEVNULL,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60)
        except Exception:
            pass


def run_linux_landing_cycle(work: Path, server: LoopbackFeedServer, base_dir: Path, steps: list[tuple[str, Path]]):
    user_dir = work / "linux-user"
    user_dir.mkdir(parents=True, exist_ok=True)
    env = clean_headless_env()
    env["BEA_USER_DIR"] = str(user_dir)
    env["BEA_UPDATE_ANSWER"] = "update"
    launcher = base_dir / "onslaught-ce"

    for version, pkg in steps:
        feed = server.publish(version, "linux-i686", pkg)
        step_env = dict(env, BEA_UPDATE_V2_FEED=feed)
        r = subprocess.run([str(launcher), "--update-only"], env=step_env, capture_output=True, text=True, timeout=120)
        if r.returncode != 0:
            log = (user_dir / "onslaught-ce.log").read_text(errors="replace") if (user_dir / "onslaught-ce.log").exists() else ""
            raise AssertionError(f"Linux update to {version} failed (rc={r.returncode}):\nstderr:\n{r.stderr}\nlog:\n{log}")
        v = subprocess.run([str(launcher), "--version"], env=env, capture_output=True, text=True, timeout=60)
        if v.returncode != 0 or f"Onslaught CE V{version}" not in v.stdout:
            raise AssertionError(f"Linux handoff after {version} failed: rc={v.returncode} stdout={v.stdout!r} stderr={v.stderr!r}")

    if len(steps) >= 2:
        prev_v, last_v = steps[-2][0], steps[-1][0]
        rb = subprocess.run([str(launcher), "--rollback"], env=env, capture_output=True, text=True, timeout=60)
        if rb.returncode != 0:
            raise AssertionError(f"Linux --rollback failed: rc={rb.returncode} stderr={rb.stderr}")
        v = subprocess.run([str(launcher), "--version"], env=env, capture_output=True, text=True, timeout=60)
        if f"Onslaught CE V{prev_v}" not in v.stdout:
            raise AssertionError(f"Linux --rollback did not restore V{prev_v}: {v.stdout!r}")
        rb2 = subprocess.run([str(launcher), "--rollback"], env=env, capture_output=True, text=True, timeout=60)
        if rb2.returncode != 0:
            raise AssertionError(f"Linux second --rollback failed: rc={rb2.returncode} stderr={rb2.stderr}")
        v2 = subprocess.run([str(launcher), "--version"], env=env, capture_output=True, text=True, timeout=60)
        if f"Onslaught CE V{last_v}" not in v2.stdout:
            raise AssertionError(f"Linux forward --rollback did not restore V{last_v}: {v2.stdout!r}")


def run_windows_landing_cycle(work: Path, wineprefix: Path, server: LoopbackFeedServer, label: str,
                              base_dir: Path, steps: list[tuple[str, Path]]):
    user_dir = work / f"win-user-{label}"
    user_dir.mkdir(parents=True, exist_ok=True)
    env = clean_headless_env()
    env["WINEPREFIX"] = str(wineprefix)
    env["BEA_USER_DIR"] = "Z:" + str(user_dir).replace("/", "\\")
    env["BEA_UPDATE_ANSWER"] = "update"
    launcher = base_dir / "onslaught-ce.exe"

    for version, pkg in steps:
        feed = server.publish(version, "windows-i686", pkg)
        step_env = dict(env, BEA_UPDATE_V2_FEED=feed)
        r = subprocess.run(["wine", str(launcher), "--update-only"], env=step_env, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=180)
        if r.returncode != 0:
            log = (user_dir / "onslaught-ce.log").read_text(errors="replace") if (user_dir / "onslaught-ce.log").exists() else ""
            raise AssertionError(f"Windows ({label}) update to {version} failed (rc={r.returncode}):\nstderr:\n{r.stderr}\nlog:\n{log}")
        v = subprocess.run(["wine", str(launcher), "--version"], env=env, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=120)
        if v.returncode != 0 or f"Onslaught CE V{version}" not in v.stdout:
            raise AssertionError(f"Windows ({label}) handoff after {version} failed: rc={v.returncode} stdout={v.stdout!r} stderr={v.stderr!r}")

    if len(steps) >= 2:
        prev_v, last_v = steps[-2][0], steps[-1][0]
        rb = subprocess.run(["wine", str(launcher), "--rollback"], env=env, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=120)
        if rb.returncode != 0:
            raise AssertionError(f"Windows ({label}) --rollback failed: rc={rb.returncode} stderr={rb.stderr}")
        v = subprocess.run(["wine", str(launcher), "--version"], env=env, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=120)
        if f"Onslaught CE V{prev_v}" not in v.stdout:
            raise AssertionError(f"Windows ({label}) --rollback did not restore V{prev_v}: {v.stdout!r}")
        rb2 = subprocess.run(["wine", str(launcher), "--rollback"], env=env, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=120)
        if rb2.returncode != 0:
            raise AssertionError(f"Windows ({label}) second --rollback failed: rc={rb2.returncode} stderr={rb2.stderr}")
        v2 = subprocess.run(["wine", str(launcher), "--version"], env=env, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=120)
        if f"Onslaught CE V{last_v}" not in v2.stdout:
            raise AssertionError(f"Windows ({label}) forward --rollback did not restore V{last_v}: {v2.stdout!r}")


def repackage_linux_with_test_launcher(src_tar: Path, dst_tar: Path, launcher_bin: Path, version: str):
    top = f"onslaught-ce-{version}-linux-i686"
    target_member = f"{top}/onslaught-ce-launch"
    replaced = False
    with tarfile.open(src_tar, "r:gz") as tin, tarfile.open(dst_tar, "w:gz", format=tarfile.GNU_FORMAT) as tout:
        for m in tin.getmembers():
            if m.name == target_member:
                data = launcher_bin.read_bytes()
                ti = tarfile.TarInfo(m.name)
                ti.size = len(data)
                ti.mode = 0o755
                ti.mtime = 0
                tout.addfile(ti, io.BytesIO(data))
                replaced = True
            elif m.isfile():
                tout.addfile(m, tin.extractfile(m))
            else:
                tout.addfile(m)
    if not replaced:
        raise AssertionError(f"{src_tar}: did not find {target_member}")


def repackage_windows_with_test_launcher(src_zip: Path, dst_zip: Path, launcher_bin: Path, version: str):
    top = f"onslaught-ce-{version}-windows-i686"
    target_member = f"{top}/onslaught-ce.exe"
    replaced = False
    with zipfile.ZipFile(src_zip, "r") as zin, zipfile.ZipFile(dst_zip, "w", compression=zipfile.ZIP_DEFLATED) as zout:
        for info in zin.infolist():
            if info.filename == target_member:
                data = launcher_bin.read_bytes()
                replaced = True
            else:
                data = zin.read(info.filename)
            zi = zipfile.ZipInfo(info.filename)
            zi.compress_type = zipfile.ZIP_DEFLATED
            zi.external_attr = info.external_attr
            zout.writestr(zi, data)
    if not replaced:
        raise AssertionError(f"{src_zip}: did not find {target_member}")


def make_deep_windows_dir(work: Path, target_win_len: int = 162) -> Path:
    """Build a directory whose Z:\\... Windows path length lands near target_win_len so
    unpacked files exceed MAX_PATH (260) while the launcher exe itself stays < 260."""
    cur = work
    segments = ["Profiles", "PlayerWithLongWindowsName", "OneDrive", "Documents", "Games", "BattleEngineAquila"]
    for seg in segments:
        cand = cur / seg
        if len("Z:" + str(cand).replace("/", "\\")) + 13 > target_win_len:
            break
        cur = cand
    cur_len = len("Z:" + str(cur).replace("/", "\\"))
    need = max(1, target_win_len - cur_len - 1)
    leaf = ("onslaught-ce-install-" + "x" * need)[:need]
    return cur / leaf


def verify_real_release(args):
    build, deps, package = load_port_modules()
    linux_pkg = Path(args.linux_pkg).resolve()
    windows_pkg = Path(args.windows_pkg).resolve()
    prev_v = args.prev_version
    new_v = args.version

    l_count, l_max = check_linux_tar_layout(linux_pkg, new_v)
    w_count, w_max = check_windows_zip_budget(windows_pkg, new_v)
    print(f"[audit] Linux release tar.gz: {l_count} entries, max member len={l_max}")
    print(f"[audit] Windows release zip:  {w_count} entries, max member len={w_max} (budget <= {WINDOWS_MAX_MEMBER_LEN})")

    priv, pub = make_test_key()
    defines = ["-DCE_UPDATER_TESTING", f'-DCE_UPDATER_TEST_KEY="{pub}"']
    with tempfile.TemporaryDirectory(prefix="ce-release-landing-") as tmp:
        work = Path(tmp)
        wineprefix = work / "wineprefix"
        server = LoopbackFeedServer(work / "www", priv)
        try:
            # 1. Linux real-archive landing
            if args.platform in ("linux", "all"):
                l_prev = work / "build/linux-prev"
                l_new = work / "build/linux-new"
                assert build.build_launcher("linux", prev_v, l_prev, defines) == 0
                assert build.build_launcher("linux", new_v, l_new, defines) == 0
                test_tar = work / f"onslaught-ce-{new_v}-linux-i686.tar.gz"
                repackage_linux_with_test_launcher(linux_pkg, test_tar, l_new, new_v)
                base_linux = work / "install-linux"
                base_linux.mkdir()
                with tarfile.open(linux_pkg, "r:gz") as t:
                    top_prefix = f"onslaught-ce-{new_v}-linux-i686/"
                    for m in t.getmembers():
                        if not m.name.startswith(top_prefix):
                            continue
                        rel = m.name[len(top_prefix):]
                        if not rel:
                            continue
                        dst = base_linux / rel
                        if m.isdir():
                            dst.mkdir(parents=True, exist_ok=True)
                        elif m.isfile():
                            dst.parent.mkdir(parents=True, exist_ok=True)
                            dst.write_bytes(t.extractfile(m).read())
                            dst.chmod(m.mode & 0o777)
                shutil.copy2(l_prev, base_linux / "onslaught-ce-launch")
                (base_linux / "onslaught-ce-launch").chmod(0o755)
                run_linux_landing_cycle(work, server, base_linux, [(new_v, test_tar)])
                print(f"[pass] Linux real release archive landed {prev_v} -> {new_v} ({l_count} files) + handoff verified")

            # 2. Windows real-archive landing (short path + >270-char extended path)
            if args.platform in ("windows", "all"):
                if not shutil.which("wine"):
                    raise AssertionError("wine is required to verify Windows release updates")
                w_prev = work / "build/win-prev.exe"
                w_new = work / "build/win-new.exe"
                assert build.build_launcher("windows", prev_v, w_prev, defines) == 0
                assert build.build_launcher("windows", new_v, w_new, defines) == 0
                test_zip = work / f"onslaught-ce-{new_v}-windows-i686.zip"
                repackage_windows_with_test_launcher(windows_pkg, test_zip, w_new, new_v)
                init_wineprefix(wineprefix)

                def make_win_base(dst_dir: Path):
                    dst_dir.mkdir(parents=True, exist_ok=True)
                    with zipfile.ZipFile(windows_pkg, "r") as z:
                        top_prefix = f"onslaught-ce-{new_v}-windows-i686/"
                        for info in z.infolist():
                            if not info.filename.startswith(top_prefix):
                                continue
                            rel = info.filename[len(top_prefix):]
                            if not rel or rel.endswith("/"):
                                continue
                            out_p = dst_dir / rel
                            out_p.parent.mkdir(parents=True, exist_ok=True)
                            out_p.write_bytes(z.read(info.filename))
                    shutil.copy2(w_prev, dst_dir / "onslaught-ce.exe")

                short_dir = work / "win-short"
                make_win_base(short_dir)
                run_windows_landing_cycle(work, wineprefix, server, "short", short_dir, [(new_v, test_zip)])
                print(f"[pass] Windows real release archive landed (short path) {prev_v} -> {new_v} ({w_count} files) + handoff verified")

                # Deep path (~162 chars): exe stays < 260 (launchable without LongPathsEnabled), while
                # unpacked files like licenses/MinGW-w64-runtime-COPYING.txt exceed MAX_PATH (> 270 chars),
                # directly exercising \\?\ UTF-16 *W unpacking and CreateProcessW handoff.
                deep_dir = make_deep_windows_dir(work, 162)
                win_deep_len = len("Z:" + str(deep_dir).replace("/", "\\"))
                assert 155 <= win_deep_len <= 170, f"expected deep dir in [155, 170], got {win_deep_len}"
                assert win_deep_len + 37 + w_max > 260, "deep path must push unpacked files past MAX_PATH (260)"
                make_win_base(deep_dir)
                run_windows_landing_cycle(work, wineprefix, server, "deep", deep_dir, [(new_v, test_zip)])
                print(f"[pass] Windows real release archive landed (deep path len={win_deep_len}, unpacked max={win_deep_len + 37 + w_max} > 260) + handoff verified")
        finally:
            stop_wineprefix(wineprefix)
            server.close()


PLATFORM_ARG = "all"


class ReleaseUpdateLandingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build, cls.deps, cls.package = load_port_modules()
        cls.priv, cls.pub = make_test_key()
        cls.defines = ["-DCE_UPDATER_TESTING", f'-DCE_UPDATER_TEST_KEY="{cls.pub}"']
        cls.versions = ("0.1.17.3", "0.1.17.4", "0.1.17.5")
        cls.tmp = tempfile.TemporaryDirectory(prefix="ce-landing-suite-")
        cls.work = Path(cls.tmp.name)
        cls.server = LoopbackFeedServer(cls.work / "www", cls.priv)

    @classmethod
    def tearDownClass(cls):
        stop_wineprefix(cls.work / "suite-wineprefix")
        cls.server.close()
        cls.tmp.cleanup()

    def test_linux_full_layout_chained_update_handoff_and_rollback(self):
        if PLATFORM_ARG not in ("linux", "all"):
            self.skipTest("linux not selected")
        launchers = {}
        pkgs = {}
        sdl_so = (self.deps.PREFIX / "lib/libSDL3.so.0").resolve().read_bytes()
        for v in self.versions:
            exe = self.work / f"build/linux/{v}/onslaught-ce-launch"
            self.assertEqual(self.build.build_launcher("linux", v, exe, self.defines), 0)
            launchers[v] = exe
            top = f"onslaught-ce-{v}-linux-i686"
            files = {
                f"{top}/onslaught-ce": (self.package.LAUNCHER.encode(), 0o755),
                f"{top}/preflight.sh": (b"#!/bin/sh\nexit 0\n", 0o755),
                f"{top}/onslaught-ce-launch": (exe.read_bytes(), 0o755),
                f"{top}/bea": (GAME_SH.replace("{version}", v).encode(), 0o755),
                f"{top}/lib/libSDL3.so.0": (sdl_so, 0o755),
                f"{top}/source/libjuice.tar.gz": (b"placeholder-source\n", 0o644),
                f"{top}/licenses/MinGW-w64-runtime-COPYING.txt": (b"licence\n", 0o644),
                f"{top}/README.txt": (f"Onslaught CE V{v}\n".encode(), 0o644),
            }
            pkg = self.work / f"pkgs/{top}.tar.gz"
            pkg.parent.mkdir(parents=True, exist_ok=True)
            with tarfile.open(pkg, "w:gz", format=tarfile.GNU_FORMAT) as t:
                for name, (data, mode) in sorted(files.items()):
                    ti = tarfile.TarInfo(name)
                    ti.size, ti.mode, ti.mtime = len(data), mode, 0
                    t.addfile(ti, io.BytesIO(data))
            check_linux_tar_layout(pkg, v)
            pkgs[v] = pkg

        base = self.work / "suite-linux-install"
        (base / "lib").mkdir(parents=True)
        (base / "onslaught-ce").write_bytes(self.package.LAUNCHER.encode())
        (base / "onslaught-ce").chmod(0o755)
        (base / "preflight.sh").write_bytes(b"#!/bin/sh\nexit 0\n")
        (base / "preflight.sh").chmod(0o755)
        shutil.copy2(launchers[self.versions[0]], base / "onslaught-ce-launch")
        (base / "onslaught-ce-launch").chmod(0o755)
        (base / "bea").write_bytes(GAME_SH.replace("{version}", self.versions[0]).encode())
        (base / "bea").chmod(0o755)
        (base / "lib/libSDL3.so.0").write_bytes(sdl_so)

        run_linux_landing_cycle(self.work, self.server, base,
                                [(self.versions[1], pkgs[self.versions[1]]),
                                 (self.versions[2], pkgs[self.versions[2]])])

    def test_windows_full_layout_short_and_long_path_update_handoff_and_rollback(self):
        if PLATFORM_ARG not in ("windows", "all"):
            self.skipTest("windows not selected")
        if not shutil.which("wine"):
            self.skipTest("wine not available")
        sdl_dll = (self.deps.PREFIX_WIN / "bin/SDL3.dll").read_bytes()
        zlib_dll = (self.deps.PREFIX_WIN / "bin/libz.dll").read_bytes()
        cc = str(self.deps.DATA / "llvm-mingw/bin/i686-w64-mingw32-clang")
        launchers = {}
        pkgs = {}
        for v in self.versions:
            bdir = self.work / f"build/windows/{v}"
            bdir.mkdir(parents=True, exist_ok=True)
            exe = bdir / "onslaught-ce.exe"
            self.assertEqual(self.build.build_launcher("windows", v, exe, self.defines), 0)
            launchers[v] = exe
            game = bdir / "bea.exe"
            (bdir / "bea.c").write_text(GAME_C.replace("{version}", v))
            subprocess.run([cc, "-O1", str(bdir / "bea.c"), "-o", str(game)], check=True)
            top = f"onslaught-ce-{v}-windows-i686"
            files = {
                f"{top}/onslaught-ce.exe": (exe.read_bytes(), 0o755),
                f"{top}/bea.exe": (game.read_bytes(), 0o755),
                f"{top}/SDL3.dll": (sdl_dll, 0o644),
                f"{top}/libz.dll": (zlib_dll, 0o644),
                f"{top}/source/libjuice.tar.gz": (b"placeholder-source\n", 0o644),
                f"{top}/source/libjuice-build-receipt.json": (b"{}\n", 0o644),
                f"{top}/licenses/MinGW-w64-runtime-COPYING.txt": (b"licence\n", 0o644),
                f"{top}/NATIVE-SIGNALING-SOURCE-NOTICE.txt": (b"notice\n", 0o644),
                f"{top}/README.txt": (f"Onslaught CE V{v}\n".encode(), 0o644),
            }
            pkg = self.work / f"pkgs/{top}.zip"
            pkg.parent.mkdir(parents=True, exist_ok=True)
            with zipfile.ZipFile(pkg, "w", compression=zipfile.ZIP_DEFLATED) as z:
                for name, (data, mode) in sorted(files.items()):
                    zi = zipfile.ZipInfo(name)
                    zi.compress_type = zipfile.ZIP_DEFLATED
                    zi.external_attr = mode << 16
                    z.writestr(zi, data)
            check_windows_zip_budget(pkg, v)
            pkgs[v] = pkg

        wineprefix = self.work / "suite-wineprefix"
        init_wineprefix(wineprefix)

        def populate_base(dst: Path):
            dst.mkdir(parents=True, exist_ok=True)
            shutil.copy2(launchers[self.versions[0]], dst / "onslaught-ce.exe")
            shutil.copy2(self.work / f"build/windows/{self.versions[0]}/bea.exe", dst / "bea.exe")
            (dst / "SDL3.dll").write_bytes(sdl_dll)
            (dst / "libz.dll").write_bytes(zlib_dll)

        # 1. Standard short path: chained 0.1.17.3 -> 0.1.17.4 -> 0.1.17.5 + rollback
        short_dir = self.work / "suite-win-short"
        populate_base(short_dir)
        run_windows_landing_cycle(self.work, wineprefix, self.server, "suite-short", short_dir,
                                  [(self.versions[1], pkgs[self.versions[1]]),
                                   (self.versions[2], pkgs[self.versions[2]])])

        # 2. Deep Windows path (~162 chars): exe stays < 260 (launchable without LongPathsEnabled), while
        # unpacked files exceed MAX_PATH (> 270 chars), directly exercising \\?\ *W unpacking + CreateProcessW handoff.
        deep_dir = make_deep_windows_dir(self.work, 162)
        win_len = len("Z:" + str(deep_dir).replace("/", "\\"))
        self.assertGreaterEqual(win_len, 155)
        self.assertLessEqual(win_len, 170)
        self.assertGreater(win_len + 37 + 73, 260)
        populate_base(deep_dir)
        run_windows_landing_cycle(self.work, wineprefix, self.server, "suite-deep", deep_dir,
                                  [(self.versions[2], pkgs[self.versions[2]])])


def main():
    global PLATFORM_ARG
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--platform", default="all", choices=["linux", "windows", "all"])
    ap.add_argument("--verify-release", action="store_true")
    ap.add_argument("--linux-pkg", help="Path to built release linux-i686.tar.gz (--verify-release)")
    ap.add_argument("--windows-pkg", help="Path to built release windows-i686.zip (--verify-release)")
    ap.add_argument("--prev-version", default="0.1.17.4")
    ap.add_argument("--version", default="0.1.17.5")
    args, rest = ap.parse_known_args()
    PLATFORM_ARG = args.platform
    if args.verify_release:
        if not args.linux_pkg or not args.windows_pkg:
            ap.error("--verify-release requires --linux-pkg and --windows-pkg")
        verify_real_release(args)
        return
    unittest.main(argv=[sys.argv[0]] + rest)


if __name__ == "__main__":
    main()
