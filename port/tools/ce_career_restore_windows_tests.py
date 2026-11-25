#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise port/platform/windows/ce_career_restore.cpp under a private isolated Wine prefix.

    python3 port/tools/ce_career_restore_windows_tests.py [-v]

Compiles the real Windows career custody backend and preview reader for shipping 32-bit Windows
(i686-w64-mingw32-clang++ -Wall -Wextra -Werror) and 64-bit Windows, runs the 32-bit binary in
an isolated disposable Wine prefix (never ~/.wine, never any display), verifies every API
contract and filesystem invariant independently, and proves two adverse mutants fail:
  1. removing `.pending-` backup staging (partial set leaks into CePreviewList)
  2. changing ReplaceIfExists=FALSE to TRUE (competing target gets clobbered instead of Occupied)

Note: this runs under Wine on Linux, not native Windows 11 NTFS. Needs llvm-mingw and Wine.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
import deps  # noqa: E402

TOOLCHAIN = deps.DATA / "llvm-mingw/bin"
RESTORE_SRC = ROOT / "port/platform/windows/ce_career_restore.cpp"
PREVIEW_SRC = ROOT / "port/platform/windows/ce_backup_preview.cpp"
DRIVER_SRC = ROOT / "port/tools/tests/ce_career_restore_windows_driver.cpp"
UTF8_RC = ROOT / "port/platform/windows/utf8.rc"

COMPLETE, REFUSED, BUSY, CANCELLED, CHANGED, OCCUPIED, UNCERTAIN, UNSUPPORTED = range(8)
LOCK_NAME = ".ce-career-custody.lock"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def make_career(kills_byte: int = 0, world: int = 120, complete: int = 1) -> bytes:
    """Synthetic 10,004-byte CE career that satisfies CeCareerViewBuild and CeCareerViewPlausible."""
    buf = bytearray(10004)
    buf[0] = 0xD1  # CE_CAREER_VERSION = 0x4BD1 (little-endian)
    buf[1] = 0x4B
    buf[0x0006 + 0x10] = world & 0xFF
    buf[0x0006 + 0x11] = (world >> 8) & 0xFF
    buf[0x0006 + 0x04] = complete & 0xFF
    buf[0x23F6] = kills_byte & 0xFF
    return bytes(buf)


def tree_snapshot(root: Path, *, ignore_lock: bool = True) -> dict[str, tuple[str, int]]:
    """{posix relpath: (sha256, inode)} for all regular files under root."""
    out: dict[str, tuple[str, int]] = {}
    for p in sorted(root.rglob("*")):
        if p.is_symlink() or not p.is_file():
            continue
        rel = p.relative_to(root).as_posix()
        if ignore_lock and rel in (LOCK_NAME, f"user/{LOCK_NAME}"):
            continue
        out[rel] = (sha256(p.read_bytes()), p.stat().st_ino)
    return out


def parse_kv(stdout: str) -> dict[str, str]:
    out: dict[str, str] = {}
    for line in stdout.splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            out[k.strip()] = v.strip()
    return out


def parse_list(stdout: str) -> tuple[bool, list[tuple[str, bool, str, int, int, str]]]:
    ok = False
    items = []
    for line in stdout.splitlines():
        if line.startswith("LIST_OK="):
            ok = line.split("=", 1)[1].strip() == "1"
        elif line.startswith("LIST_ITEM="):
            parts = line.split("=", 1)[1].split("|", 5)
            items.append((parts[0], parts[1] == "1", parts[2], int(parts[3]), int(parts[4]), parts[5]))
    return ok, items


def relax_and_rmtree(path: Path) -> None:
    if not path.exists():
        return
    for folder, dirs, files in os.walk(path):
        for n in dirs + files:
            p = Path(folder, n)
            if not p.is_symlink():
                try:
                    p.chmod(0o755 if p.is_dir() else 0o644)
                except OSError:
                    pass
    shutil.rmtree(path, ignore_errors=True)


class WineEnv:
    """Isolated private Wine prefix with no display and file-backed stdout/stderr."""

    def __init__(self, scratch: Path):
        self.wine = shutil.which("wine")
        self.wineserver = shutil.which("wineserver")
        self.wineboot = shutil.which("wineboot")
        if not (self.wine and self.wineserver and self.wineboot):
            raise unittest.SkipTest("wine, wineserver, and wineboot are required")
        supplied = os.environ.get("BEA_WINE_PREFIX")
        self.owned = not supplied
        self.prefix = Path(supplied).resolve() if supplied else (scratch / "wineprefix").resolve()
        if self.prefix == (Path.home() / ".wine").resolve():
            raise RuntimeError("refusing to use ~/.wine")
        self.private = self.prefix / ".t-career-restore"
        self.io_dir = scratch / "wine-io"
        self.io_dir.mkdir(parents=True, exist_ok=True)
        self.started = False
        self.alias_drive: Path | None = None

    def env(self) -> dict[str, str]:
        drop = {
            "DISPLAY", "WAYLAND_DISPLAY", "WAYLAND_SOCKET", "XAUTHORITY",
            "DBUS_SESSION_BUS_ADDRESS", "WINEPREFIX", "WINEARCH", "WINEDEBUG",
            "WINEDLLOVERRIDES", "BEA_GAME_DIR", "BEA_USER_DIR",
        }
        e = {k: v for k, v in os.environ.items() if k not in drop}
        home = self.private / "home"
        e.update(
            WINEPREFIX=str(self.prefix),
            WINEDEBUG="-all",
            WINEDLLOVERRIDES="mscoree,mshtml=",
            CE_CAREER_RESTORE_DISPOSABLE="1",
            BEA_REPORT_NO_OPEN="1",
            BROWSER="/bin/true",
            HOME=str(home),
            XDG_RUNTIME_DIR=str(self.private / "run"),
            XDG_CONFIG_HOME=str(home / ".config"),
            XDG_DATA_HOME=str(home / ".local/share"),
            XDG_CACHE_HOME=str(home / ".cache"),
            LC_ALL="C.UTF-8",
            LANG="C.UTF-8",
        )
        return e

    def _run_raw(self, argv: list[str], timeout: int = 60) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryFile(dir=self.io_dir) as out_f, tempfile.TemporaryFile(dir=self.io_dir) as err_f:
            proc = subprocess.Popen(
                argv,
                stdin=subprocess.DEVNULL,
                stdout=out_f,
                stderr=err_f,
                env=self.env(),
                cwd=self.prefix,
                start_new_session=True,
            )
            try:
                rc = proc.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, signal.SIGKILL)
                proc.wait()
                raise
            out_f.seek(0)
            err_f.seek(0)
            return subprocess.CompletedProcess(
                argv, rc, out_f.read().decode("utf-8", "replace"), err_f.read().decode("utf-8", "replace")
            )

    def start(self) -> None:
        self.prefix.mkdir(parents=True, exist_ok=True)
        for sub in ("home", "run"):
            (self.private / sub).mkdir(parents=True, exist_ok=True)
        (self.private / "run").chmod(0o700)
        self.started = True
        if not (self.prefix / "system.reg").is_file():
            boot = self._run_raw([self.wineboot, "-i"], timeout=90)
            if boot.returncode != 0:
                raise RuntimeError(f"wineboot -i failed ({boot.returncode}):\n{boot.stdout}{boot.stderr}")
            self._run_raw([self.wineserver, "-w"], timeout=60)
        subprocess.run(
            [self.wineserver, "-p"],
            env=self.env(),
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=30,
        )

    def run_exe(self, win_exe: str, args: list[str], timeout: int = 60) -> subprocess.CompletedProcess[str]:
        return self._run_raw([self.wine, win_exe, *args], timeout=timeout)

    def popen_exe(self, win_exe: str, args: list[str]) -> tuple[subprocess.Popen[bytes], object, object]:
        out_f = tempfile.TemporaryFile(dir=self.io_dir)
        err_f = tempfile.TemporaryFile(dir=self.io_dir)
        proc = subprocess.Popen(
            [self.wine, win_exe, *args],
            stdin=subprocess.DEVNULL,
            stdout=out_f,
            stderr=err_f,
            env=self.env(),
            cwd=self.prefix,
            start_new_session=True,
        )
        return proc, out_f, err_f

    def stop(self) -> None:
        if self.alias_drive and self.alias_drive.is_symlink():
            try:
                self.alias_drive.unlink()
            except OSError:
                pass
        if self.started:
            for flag in ("-k", "-w"):
                subprocess.run(
                    [self.wineserver, flag],
                    env=self.env(),
                    stdin=subprocess.DEVNULL,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    timeout=60,
                )
        if not self.owned and self.private.exists():
            relax_and_rmtree(self.private)


def build_binaries(build_dir: Path) -> dict[str, Path]:
    cxx32 = TOOLCHAIN / "i686-w64-mingw32-clang++"
    cxx64 = TOOLCHAIN / "x86_64-w64-mingw32-clang++"
    windres32 = TOOLCHAIN / "i686-w64-mingw32-windres"
    windres64 = TOOLCHAIN / "x86_64-w64-mingw32-windres"
    for tool in (cxx32, cxx64, windres32, windres64):
        if not tool.is_file():
            raise FileNotFoundError(f"missing pinned toolchain binary: {tool}")

    res32 = build_dir / "utf8-32.o"
    res64 = build_dir / "utf8-64.o"
    for wr, res in ((windres32, res32), (windres64, res64)):
        subprocess.run(
            [str(wr), str(UTF8_RC), "-O", "coff", "-o", str(res)],
            cwd=UTF8_RC.parent,
            check=True,
            capture_output=True,
            timeout=60,
        )

    base_flags = ["-std=c++17", "-Wall", "-Wextra", "-Werror", "-O2", "-static"]
    out: dict[str, Path] = {}

    def compile_exe(name: str, compiler: Path, res: Path, restore_cpp: Path, extra_inc: list[str]) -> Path:
        exe = build_dir / name
        cmd = [
            str(compiler), *base_flags, *extra_inc,
            str(restore_cpp), str(PREVIEW_SRC), str(DRIVER_SRC), str(res),
            "-o", str(exe),
        ]
        r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=180)
        if r.returncode != 0:
            raise RuntimeError(f"build {name} failed:\n{r.stdout}{r.stderr}")
        return exe

    # Shipping 32-bit ABI and 64-bit ABI, both strictly unmodified production source.
    out["prod32"] = compile_exe("driver32.exe", cxx32, res32, RESTORE_SRC, [])
    out["prod64"] = compile_exe("driver64.exe", cxx64, res64, RESTORE_SRC, [])

    # Instrumented scratch source for fault/race verification and adverse mutants.
    src_text = RESTORE_SRC.read_text(encoding="utf-8")
    hook_decls = (
        "extern bool CeRestoreTestAllowManifestWrite(HANDLE);\n"
        "extern void CeRestoreTestBeforePublishRename(HANDLE, const wchar_t *);\n"
        "extern bool CeRestoreTestIsLocalNtfs(bool);\n"
        "extern bool CeRestoreTestPostPublishFlush(HANDLE);\n"
        "extern void CeRestoreTestAfterPublishClose(HANDLE, const wchar_t *);\n"
        "extern void CeRestoreTestAfterBackupRename(const std::string &);\n"
    )
    anchor_include = '#include "ce_backup_paths.h"\n'
    anchor_manifest = "    writeAll(list.h, jsonBytes);"
    anchor_publish = "    const NTSTATUS publishStatus ="
    anchor_staging = '  const std::string staging = ".pending-" + finalName;'
    anchor_noreplace = "  info->ReplaceIfExists = FALSE;"
    anchor_ntfs = '  return _wcsicmp(fsName, L"NTFS") == 0;'
    anchor_post_flush = "    require(FlushFileBuffers(stage.h) != FALSE, Status::Uncertain);"
    anchor_post_close = "    closeNow(stage);"
    anchor_post_backup = "  out.backupSet = joinPath(s.r.backupRoot, finalName);"
    for label, anchor in (
        ("include", anchor_include),
        ("manifest", anchor_manifest),
        ("publish", anchor_publish),
        ("staging", anchor_staging),
        ("noreplace", anchor_noreplace),
        ("ntfs", anchor_ntfs),
        ("post_flush", anchor_post_flush),
        ("post_close", anchor_post_close),
        ("post_backup", anchor_post_backup),
    ):
        if src_text.count(anchor) != 1:
            raise AssertionError(f"expected unique anchor for {label}: {anchor!r}")

    instrumented = src_text.replace(anchor_include, anchor_include + hook_decls, 1)
    instrumented = instrumented.replace(
        anchor_manifest,
        "    require(CeRestoreTestAllowManifestWrite(list.h));\n" + anchor_manifest,
        1,
    )
    instrumented = instrumented.replace(
        anchor_publish,
        "    CeRestoreTestBeforePublishRename(session.slots.h, wideTarget.c_str());\n" + anchor_publish,
        1,
    )
    instrumented = instrumented.replace(
        anchor_ntfs,
        '  return CeRestoreTestIsLocalNtfs(_wcsicmp(fsName, L"NTFS") == 0);',
        1,
    )
    instrumented = instrumented.replace(
        anchor_post_flush,
        "    require(CeRestoreTestPostPublishFlush(stage.h), Status::Uncertain);",
        1,
    )
    instrumented = instrumented.replace(
        anchor_post_close,
        anchor_post_close + "\n    CeRestoreTestAfterPublishClose(session.slots.h, wideTarget.c_str());",
        1,
    )
    instrumented = instrumented.replace(
        anchor_post_backup,
        anchor_post_backup + "\n  CeRestoreTestAfterBackupRename(out.backupSet);",
        1,
    )
    inc_flags = [
        "-I" + str(ROOT / "port/platform/windows"),
        "-I" + str(ROOT / "port/platform/common"),
        "-I" + str(ROOT / "port/game"),
    ]
    fault_src = build_dir / "ce_career_restore_fault.cpp"
    fault_src.write_text(instrumented, encoding="utf-8")
    out["fault32"] = compile_exe("driver32-fault.exe", cxx32, res32, fault_src, inc_flags)

    mutant_nostage_src = build_dir / "ce_career_restore_mutant_nostage.cpp"
    mutant_nostage_src.write_text(
        instrumented.replace(anchor_staging, "  const std::string staging = finalName;", 1),
        encoding="utf-8",
    )
    out["mutant_nostage"] = compile_exe("driver32-mutant-nostage.exe", cxx32, res32, mutant_nostage_src, inc_flags)

    mutant_replace_src = build_dir / "ce_career_restore_mutant_replace.cpp"
    mutant_replace_src.write_text(
        instrumented.replace(anchor_noreplace, "  info->ReplaceIfExists = TRUE;", 1),
        encoding="utf-8",
    )
    out["mutant_replace"] = compile_exe("driver32-mutant-replace.exe", cxx32, res32, mutant_replace_src, inc_flags)

    return out


class WindowsCareerCustodyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.present = make_career(kills_byte=1, world=120)
        cls.alternate = make_career(kills_byte=5, world=121)
        cls.previous = make_career(kills_byte=9, world=122)
        assert len({sha256(cls.present), sha256(cls.alternate), sha256(cls.previous)}) == 3

        scratch_parent = deps.WORK / "tests"
        scratch_parent.mkdir(parents=True, exist_ok=True)
        cls.scratch = Path(tempfile.mkdtemp(prefix="ce-career-restore-win-", dir=scratch_parent))
        cls.build_dir = cls.scratch / "build"
        cls.build_dir.mkdir()
        cls.wine = WineEnv(cls.scratch)
        try:
            cls.bins = build_binaries(cls.build_dir)
            cls.wine.start()
            cls.run_tag = f"ce-restore-win-{os.getpid()}-{int(time.time())}"
            cls.drive_root = cls.wine.prefix / "drive_c" / cls.run_tag
            cls.drive_root.mkdir(parents=True)
            cls.bin_dir = cls.drive_root / "bin"
            cls.bin_dir.mkdir()
            cls.win_bins: dict[str, str] = {}
            for key, path in cls.bins.items():
                dest = cls.bin_dir / path.name
                shutil.copyfile(path, dest)
                cls.win_bins[key] = f"C:\\{cls.run_tag}\\bin\\{path.name}"
            cls.case_num = 0
        except Exception:
            cls.wine.stop()
            relax_and_rmtree(cls.scratch)
            raise

    @classmethod
    def tearDownClass(cls) -> None:
        try:
            if hasattr(cls, "drive_root") and cls.drive_root.exists():
                relax_and_rmtree(cls.drive_root)
        finally:
            cls.wine.stop()
            relax_and_rmtree(cls.scratch)

    def new_profile(self, name: str, *, source_name: str = "Amber.bes",
                    with_options: bool = True) -> tuple[Path, str]:
        self.__class__.case_num += 1
        leaf = f"{self.__class__.case_num:02d}-{name}"
        d = self.drive_root / leaf
        for sub in (
            "user/savegames", "user/nested", "game/savegames", "package", "external",
            "source/onslaught-backup-20260405-123000",
        ):
            (d / sub).mkdir(parents=True)
        (d / "AUTHORED").write_text("disposable windows custody fixture\n", encoding="utf-8")
        (d / "user/savegames/Amber.bes").write_bytes(self.present)
        (d / "user/savegames/Birch.bes").write_bytes(self.alternate)
        (d / "game/savegames/Protected.bes").write_bytes(self.alternate)
        if with_options:
            (d / "user/defaultoptions.bea").write_bytes(b"disposable windows options")
        src = d / "source/onslaught-backup-20260405-123000"
        (src / source_name).write_bytes(self.previous)
        manifest = {
            "Folder": "C:\\authored-only",
            "Game": "C:\\authored-only",
            "Created": "2026-04-05T12:30:00Z",
            "Reason": "Manual",
            "Target": 1,
            "Files": [{
                "Name": source_name,
                "Original": "C:\\authored-only\\" + source_name,
                "Size": len(self.previous),
                "Sha256": sha256(self.previous),
            }],
        }
        (src / "onslaught-backup.json").write_text(json.dumps(manifest), encoding="utf-8")
        return d, f"C:\\{self.run_tag}\\{leaf}"

    def call(self, win_base: str, cmd: str, *extra: str, bin_key: str = "prod32") -> tuple[int, dict[str, str], str]:
        r = self.wine.run_exe(self.win_bins[bin_key], [win_base, cmd, *extra], timeout=60)
        return r.returncode, parse_kv(r.stdout), r.stdout + r.stderr

    def assert_originals_intact(self, d: Path, *, with_options: bool = True, source_name: str = "Amber.bes") -> None:
        self.assertEqual((d / "user/savegames/Amber.bes").read_bytes(), self.present)
        self.assertEqual((d / "user/savegames/Birch.bes").read_bytes(), self.alternate)
        self.assertEqual((d / "game/savegames/Protected.bes").read_bytes(), self.alternate)
        if with_options:
            self.assertEqual((d / "user/defaultoptions.bea").read_bytes(), b"disposable windows options")
        self.assertEqual((d / "source/onslaught-backup-20260405-123000" / source_name).read_bytes(), self.previous)

    def assert_full_set_folder(self, set_dir: Path, identity: str, reason: str,
                               expected_files: dict[str, bytes]) -> None:
        self.assertTrue(set_dir.is_dir(), set_dir)
        self.assertRegex(set_dir.name, r"^onslaught-backup-\d{8}-\d{6}-\d{18}$")
        raw_manifest = (set_dir / "onslaught-backup.json").read_bytes()
        self.assertEqual(sha256(raw_manifest), identity)
        parsed = json.loads(raw_manifest.decode("utf-8"))
        self.assertEqual(parsed["Reason"], reason)
        self.assertEqual(len(parsed["Files"]), len(expected_files))
        self.assertEqual(
            {f["Name"]: f["Sha256"] for f in parsed["Files"]},
            {k: sha256(v) for k, v in expected_files.items()},
        )
        for name, data in expected_files.items():
            self.assertEqual((set_dir / name).read_bytes(), data)

    def test_01_fail_closed_on_empty_request(self) -> None:
        rc, kv, raw = self.call("C:\\unused", "fail-closed")
        self.assertEqual(rc, 0, raw)
        self.assertEqual(kv.get("FAIL_CLOSED"), "1", raw)

    def test_02_backup_all_and_take_full_set(self) -> None:
        d, w = self.new_profile("backup-all")
        # Add a shadowed game career with the same spelling to verify user precedence.
        (d / "game/savegames/Amber.bes").write_bytes(self.previous)
        before_saves = tree_snapshot(d / "user/savegames")
        before_game = tree_snapshot(d / "game")

        rc, kv, raw = self.call(w, "backup")
        self.assertEqual(rc, 0, raw)
        self.assertEqual(int(kv["STATUS"]), COMPLETE, raw)
        self.assertRegex(kv["BACKUP_IDENTITY"], r"^[0-9a-f]{64}$")
        self.assertRegex(kv["BACKUP_STAMP"], r"^[0-9a-f]{64}$")
        set_name = kv["BACKUP_SET"].rsplit("\\", 1)[-1]
        expected = {
            "Amber.bes": self.present,  # user precedence over shadowed game copy
            "Birch.bes": self.alternate,
            "Protected.bes": self.alternate,
            "defaultoptions.bea": b"disposable windows options",
        }
        self.assert_full_set_folder(d / "external" / set_name, kv["BACKUP_IDENTITY"], "All careers", expected)
        self.assertEqual(tree_snapshot(d / "user/savegames"), before_saves)
        self.assertEqual(tree_snapshot(d / "game"), before_game)
        self.assertEqual((d / "game/savegames/Amber.bes").read_bytes(), self.previous)
        self.assertFalse([p for p in (d / "external").iterdir() if p.name.startswith(".pending-")])

        # CeCareerTakeFullSet returns verified set + identity + stamp, and lists cleanly.
        rc2, kv2, raw2 = self.call(w, "take")
        self.assertEqual(rc2, 0, raw2)
        self.assertEqual(int(kv2["STATUS"]), COMPLETE, raw2)
        self.assertRegex(kv2["BACKUP_IDENTITY"], r"^[0-9a-f]{64}$")
        self.assertRegex(kv2["BACKUP_STAMP"], r"^[0-9a-f]{64}$")
        set2 = kv2["BACKUP_SET"].rsplit("\\", 1)[-1]
        self.assertNotEqual(set2, set_name)
        self.assert_full_set_folder(d / "external" / set2, kv2["BACKUP_IDENTITY"], "All careers", expected)

        rc3, _, raw3 = self.call(w, "list")
        self.assertEqual(rc3, 0, raw3)
        list_ok, items = parse_list(raw3)
        self.assertTrue(list_ok, raw3)
        self.assertEqual(len(items), 2, items)
        self.assertTrue(all(it[1] and it[3] == 4 and it[4] == 3 and it[5] == "All careers" for it in items))

        # Absent defaultoptions.bea is never fabricated.
        d2, w2 = self.new_profile("backup-no-options", with_options=False)
        rc4, kv4, raw4 = self.call(w2, "backup")
        self.assertEqual(rc4, 0, raw4)
        self.assertEqual(int(kv4["STATUS"]), COMPLETE, raw4)
        self.assertFalse((d2 / "user/defaultoptions.bea").exists())
        set4 = kv4["BACKUP_SET"].rsplit("\\", 1)[-1]
        self.assert_full_set_folder(
            d2 / "external" / set4,
            kv4["BACKUP_IDENTITY"],
            "All careers",
            {"Amber.bes": self.present, "Birch.bes": self.alternate, "Protected.bes": self.alternate},
        )

    def test_03_stamp_and_suggest_sequences_and_31_char_truncation(self) -> None:
        d, w = self.new_profile("suggest-read-only")
        before = tree_snapshot(d, ignore_lock=False)
        rc, kv, raw = self.call(w, "stamp")
        self.assertEqual(rc, 0, raw)
        self.assertEqual(kv["READ_OK"], "1")
        self.assertEqual(kv["STAMP_OK"], "1")
        self.assertRegex(kv["STAMP"], r"^[0-9a-f]{64}$")
        self.assertEqual(tree_snapshot(d, ignore_lock=False), before)

        # Wrong identity fails CeCareerRestoreStamp cleanly.
        rc_bad, kv_bad, raw_bad = self.call(w, "stamp", "wrong-identity")
        self.assertEqual(rc_bad, 0, raw_bad)
        self.assertEqual(kv_bad["STAMP_OK"], "0")
        self.assertEqual(kv_bad["STAMP"], "")

        # Ordinary suggestion is read-only (no lock file created!) and produces "Amber restored.bes".
        rc_s, kv_s, raw_s = self.call(w, "suggest")
        self.assertEqual(rc_s, 0, raw_s)
        self.assertEqual(int(kv_s["STATUS"]), COMPLETE, raw_s)
        self.assertEqual(kv_s["TARGET"], "Amber restored.bes")
        self.assertEqual(tree_snapshot(d, ignore_lock=False), before)
        self.assertFalse((d / "user" / LOCK_NAME).exists())

        # Case-insensitive avoidance across BOTH user\savegames and game\savegames.
        (d / "user/savegames/AMBER RESTORED.BES").write_bytes(self.alternate)
        (d / "game/savegames/Amber restored 2.bes").write_bytes(self.alternate)
        rc_s2, kv_s2, raw_s2 = self.call(w, "suggest")
        self.assertEqual(rc_s2, 0, raw_s2)
        self.assertEqual(int(kv_s2["STATUS"]), COMPLETE, raw_s2)
        self.assertEqual(kv_s2["TARGET"], "Amber restored 3.bes")

        # Repeated restored source advances suffix (" restored" -> " restored 2", " restored 8" -> " restored 9").
        for src_name, want in (
            ("Amber restored.bes", "Amber restored 2.bes"),
            ("Amber restored 8.bes", "Amber restored 9.bes"),
            ("The Restored Pilot.bes", "The Restored Pilot restored.bes"),
            ("A" * 31 + ".bes", "A" * 22 + " restored.bes"),
        ):
            ds, ws = self.new_profile("suggest-" + str(len(src_name)), source_name=src_name)
            rc_r, kv_r, raw_r = self.call(ws, "suggest", f"source-name={src_name}")
            self.assertEqual(rc_r, 0, raw_r)
            self.assertEqual(int(kv_r["STATUS"]), COMPLETE, (src_name, raw_r))
            self.assertEqual(kv_r["TARGET"], want, src_name)
            self.assertLessEqual(len(kv_r["TARGET"]) - 4, 31)

    def test_04_restore_as_new_end_to_end_and_occupied_refusals(self) -> None:
        d, w = self.new_profile("restore-happy")
        before_user = tree_snapshot(d / "user")
        before_game = tree_snapshot(d / "game")

        rc, kv, raw = self.call(w, "restore", "expect-pair=Amber.bes|Amber restored.bes")
        self.assertEqual(rc, 0, raw)
        self.assertEqual(int(kv["STATUS"]), COMPLETE, raw)
        self.assertEqual(kv["NEW_FILE"], "Amber restored.bes")
        self.assertRegex(kv["BACKUP_IDENTITY"], r"^[0-9a-f]{64}$")
        self.assertRegex(kv["BACKUP_STAMP"], r"^[0-9a-f]{64}$")
        self.assertGreaterEqual(int(kv["PERMIT_CALLS"]), 2)
        self.assertGreaterEqual(int(kv["NAME_PERMIT_CALLS"]), 2)

        # Restored career is byte-identical to historical source, single link, originals untouched.
        restored = d / "user/savegames/Amber restored.bes"
        self.assertTrue(restored.is_file())
        self.assertEqual(restored.read_bytes(), self.previous)
        self.assertEqual(restored.stat().st_nlink, 1)
        self.assert_originals_intact(d)
        self.assertEqual(tree_snapshot(d / "game"), before_game)
        after_user = tree_snapshot(d / "user")
        self.assertEqual({k: v for k, v in after_user.items() if k != "savegames/Amber restored.bes"}, before_user)
        self.assertFalse([p for p in (d / "user/savegames").iterdir() if p.name.startswith(".pending-")])

        # Backup set has Reason == "Manual" and captured pre-restore state.
        set_name = kv["BACKUP_SET"].rsplit("\\", 1)[-1]
        self.assert_full_set_folder(
            d / "external" / set_name,
            kv["BACKUP_IDENTITY"],
            "Manual",
            {
                "Amber.bes": self.present,
                "Birch.bes": self.alternate,
                "Protected.bes": self.alternate,
                "defaultoptions.bea": b"disposable windows options",
            },
        )

        # Exact collision -> Occupied, original untouched, no extra backup set made.
        before_sets = sorted(p.name for p in (d / "external").iterdir())
        rc2, kv2, raw2 = self.call(w, "restore", "target=Amber restored.bes")
        self.assertEqual(rc2, 0, raw2)
        self.assertEqual(int(kv2["STATUS"]), OCCUPIED, raw2)
        self.assertEqual(kv2["NEW_FILE"], "")
        self.assertEqual(restored.read_bytes(), self.previous)
        self.assertEqual(sorted(p.name for p in (d / "external").iterdir()), before_sets)

        # Case-variant collision in user\savegames -> Occupied.
        rc3, kv3, raw3 = self.call(w, "restore", "target=AMBER RESTORED.BES")
        self.assertEqual(rc3, 0, raw3)
        self.assertEqual(int(kv3["STATUS"]), OCCUPIED, raw3)
        self.assertEqual(kv3["NEW_FILE"], "")

        # Collision with game\savegames career (case-insensitive) -> Occupied.
        rc4, kv4, raw4 = self.call(w, "restore", "target=PROTECTED.BES")
        self.assertEqual(rc4, 0, raw4)
        self.assertEqual(int(kv4["STATUS"]), OCCUPIED, raw4)
        self.assertEqual(kv4["NEW_FILE"], "")

        # Round-trip: take a fresh full set via CeCareerBackupAll, then restore Birch.bes from it.
        d2, w2 = self.new_profile("restore-from-full-set")
        rc_b, kv_b, raw_b = self.call(w2, "backup")
        self.assertEqual(rc_b, 0, raw_b)
        self.assertEqual(int(kv_b["STATUS"]), COMPLETE, raw_b)
        full_set_win = kv_b["BACKUP_SET"]
        full_set_dir = d2 / "external" / full_set_win.rsplit("\\", 1)[-1]
        full_set_before = tree_snapshot(full_set_dir)
        rc_r, kv_r, raw_r = self.call(
            w2, "restore",
            f"source-set={full_set_win}",
            "source-name=Birch.bes",
        )
        self.assertEqual(rc_r, 0, raw_r)
        self.assertEqual(int(kv_r["STATUS"]), COMPLETE, raw_r)
        self.assertEqual(kv_r["NEW_FILE"], "Birch restored.bes")
        self.assertEqual((d2 / "user/savegames/Birch restored.bes").read_bytes(), self.alternate)
        self.assertEqual(tree_snapshot(full_set_dir), full_set_before)
        self.assert_originals_intact(d2)

    def test_05_changed_stale_stamp_and_mutated_source_refusals(self) -> None:
        for flag in ("no-stamp", "wrong-identity", "mutate-source-bytes", "replace-source-same-bytes"):
            with self.subTest(flag=flag):
                d, w = self.new_profile("changed-" + flag)
                before_saves = tree_snapshot(d / "user/savegames")
                rc, kv, raw = self.call(w, "restore", flag, "target=Amber restored.bes")
                self.assertEqual(rc, 0, raw)
                self.assertEqual(int(kv["STATUS"]), CHANGED, (flag, raw))
                self.assertEqual(kv["NEW_FILE"], "")
                self.assertEqual(tree_snapshot(d / "user/savegames"), before_saves)
                self.assertFalse(list((d / "external").iterdir()))

    def test_06_permission_name_cancel_and_protected_path_refusals(self) -> None:
        # Missing/denied permission, denied/missing namePermitted, cancelled.
        for label, extra, expected in (
            ("no-permit", ["no-permit"], REFUSED),
            ("null-permit", ["null-permit"], REFUSED),
            ("no-name-permit", ["no-name-permit"], REFUSED),
            ("null-name-permit", ["null-name-permit"], REFUSED),
            ("wrong-pair", ["expect-pair=Amber.bes|Other.bes", "target=Amber restored.bes"], REFUSED),
            ("nonportable-con", ["target=CON.bes"], REFUSED),
            ("nonportable-long", ["target=" + "A" * 32 + ".bes"], REFUSED),
            ("nonportable-escape", ["target=..\\Escape.bes"], REFUSED),
            ("cancelled", ["cancel"], CANCELLED),
        ):
            with self.subTest(case=label):
                d, w = self.new_profile("auth-" + label)
                before_saves = tree_snapshot(d / "user/savegames")
                rc, kv, raw = self.call(w, "restore", *extra)
                self.assertEqual(rc, 0, raw)
                self.assertEqual(int(kv["STATUS"]), expected, (label, raw))
                self.assertEqual(kv["NEW_FILE"], "")
                self.assertEqual(tree_snapshot(d / "user/savegames"), before_saves)
                self.assertFalse(list((d / "external").iterdir()))

        # Protected paths: backupRoot inside user/game/package, backupRoot ancestor of user,
        # user inside game, relative traversal, UNC path, and nested drive alias (S:\ -> user\nested).
        d, w = self.new_profile("protected-paths")
        alias = self.wine.prefix / "dosdevices/s:"
        if alias.is_symlink():
            alias.unlink()
        alias.symlink_to(f"../drive_c/{self.run_tag}/{d.name}/user/nested")
        self.wine.alias_drive = alias
        try:
            for label, override in (
                ("root-is-user", f"root={w}\\user"),
                ("root-inside-user", f"root={w}\\user\\nested"),
                ("root-is-game", f"root={w}\\game"),
                ("root-is-package", f"root={w}\\package"),
                ("root-ancestor-of-user", f"root={w}"),
                ("root-dotdot", f"root={w}\\external\\..\\external"),
                ("root-unc", "root=\\\\server\\share\\backups"),
                ("root-nested-drive-alias", "root=S:\\"),
                ("user-is-game", f"game={w}\\user"),
                ("game-inside-user", f"game={w}\\user\\nested"),
            ):
                with self.subTest(protected=label):
                    rc, kv, raw = self.call(w, "backup", override)
                    self.assertEqual(rc, 0, raw)
                    self.assertEqual(int(kv["STATUS"]), REFUSED, (label, raw))
                    self.assertEqual(kv["BACKUP_SET"], "")
        finally:
            if alias.is_symlink():
                alias.unlink()
            self.wine.alias_drive = None

    def test_07_filesystem_custody_refusals(self) -> None:
        # Hard-linked career (NumberOfLinks == 2) -> Refused.
        d1, w1 = self.new_profile("hardlink")
        os.link(d1 / "user/savegames/Amber.bes", d1 / "user/savegames/Alias.bes")
        rc, kv, raw = self.call(w1, "backup")
        self.assertEqual(rc, 0, raw)
        self.assertEqual(int(kv["STATUS"]), REFUSED, raw)
        self.assertFalse(list((d1 / "external").iterdir()))

        # Win32 symlink career file (NTFS reparse point) -> Refused.
        d2, w2 = self.new_profile("symlink-file")
        rc, kv, raw = self.call(
            w2, "backup",
            f"symlink-file={w2}\\user\\savegames\\Link.bes|{w2}\\user\\savegames\\Amber.bes",
        )
        self.assertEqual(rc, 0, raw)
        self.assertEqual(int(kv["STATUS"]), REFUSED, raw)
        self.assertFalse(list((d2 / "external").iterdir()))

        # Win32 symlink backupRoot directory (NTFS reparse point) -> Refused.
        d3, w3 = self.new_profile("symlink-root")
        (d3 / "external").rmdir()
        rc, kv, raw = self.call(
            w3, "backup",
            f"symlink-dir={w3}\\external|{w3}\\user",
        )
        self.assertEqual(rc, 0, raw)
        self.assertEqual(int(kv["STATUS"]), REFUSED, raw)

        # Hidden and system career members -> Refused.
        d4, w4 = self.new_profile("hidden-file")
        rc, kv, raw = self.call(w4, "backup", f"hide={w4}\\user\\savegames\\Amber.bes")
        self.assertEqual(rc, 0, raw)
        self.assertEqual(int(kv["STATUS"]), REFUSED, raw)

        d4b, w4b = self.new_profile("system-file")
        rc, kv, raw = self.call(w4b, "backup", f"system={w4b}\\user\\savegames\\Amber.bes")
        self.assertEqual(rc, 0, raw)
        self.assertEqual(int(kv["STATUS"]), REFUSED, raw)

        # Corrupt career member -> Refused.
        d5, w5 = self.new_profile("corrupt-career")
        (d5 / "user/savegames/Bad.bes").write_bytes(b"not a valid career")
        rc, kv, raw = self.call(w5, "backup")
        self.assertEqual(rc, 0, raw)
        self.assertEqual(int(kv["STATUS"]), REFUSED, raw)

        # Too many members (> 32 manifest limit checked before staging) -> Refused without staging.
        d6, w6 = self.new_profile("too-many-members")
        for i in range(30):
            (d6 / f"user/savegames/Extra{i:02d}.bes").write_bytes(self.present)
        rc, kv, raw = self.call(w6, "backup")
        self.assertEqual(rc, 0, raw)
        self.assertEqual(int(kv["STATUS"]), REFUSED, raw)
        self.assertFalse(list((d6 / "external").iterdir()))

        # Directory entry cap (> 2048 entries) -> Refused.
        d7, w7 = self.new_profile("dir-entry-cap")
        for i in range(2048):
            (d7 / f"user/savegames/irrelevant-{i}.txt").touch()
        rc, kv, raw = self.call(w7, "backup")
        self.assertEqual(rc, 0, raw)
        self.assertEqual(int(kv["STATUS"]), REFUSED, raw)

        # Cross-directory case-spelling mismatch (user\Amber.bes vs game\AMBER.BES) -> Refused.
        d8, w8 = self.new_profile("case-spelling-mismatch")
        (d8 / "game/savegames/AMBER.BES").write_bytes(self.alternate)
        rc, kv, raw = self.call(w8, "backup")
        self.assertEqual(rc, 0, raw)
        self.assertEqual(int(kv["STATUS"]), REFUSED, raw)

    def test_08_busy_lock_contention_same_process_and_cross_process(self) -> None:
        # Same-process second handle holding LockFileEx -> Busy.
        d1, w1 = self.new_profile("busy-same-proc")
        for cmd in ("backup", "take", "restore"):
            rc, kv, raw = self.call(w1, cmd, "hold-lock")
            self.assertEqual(rc, 0, raw)
            self.assertEqual(int(kv["STATUS"]), BUSY, (cmd, raw))
        self.assertFalse(list((d1 / "external").iterdir()))

        # Separate Wine process holding LockFileEx -> Busy, then succeeds once released.
        d2, w2 = self.new_profile("busy-cross-proc")
        holder, out_f, err_f = self.wine.popen_exe(self.win_bins["prod32"], [w2, "lock-hold"])
        try:
            ready = d2 / "lock-ready"
            deadline = time.monotonic() + 15
            while not ready.exists() and holder.poll() is None and time.monotonic() < deadline:
                time.sleep(0.02)
            self.assertTrue(ready.exists(), "lock holder did not reach ready state")

            rc, kv, raw = self.call(w2, "backup")
            self.assertEqual(rc, 0, raw)
            self.assertEqual(int(kv["STATUS"]), BUSY, raw)
            self.assertFalse(list((d2 / "external").iterdir()))

            (d2 / "lock-release").write_text("go\n", encoding="utf-8")
            self.assertEqual(holder.wait(timeout=15), 0)
        finally:
            if holder.poll() is None:
                os.killpg(holder.pid, signal.SIGKILL)
                holder.wait()
            out_f.close()
            err_f.close()

        # Once released, backup succeeds cleanly.
        rc_ok, kv_ok, raw_ok = self.call(w2, "backup")
        self.assertEqual(rc_ok, 0, raw_ok)
        self.assertEqual(int(kv_ok["STATUS"]), COMPLETE, raw_ok)

    def test_09_adverse_mutant_controls_staging_and_noreplace(self) -> None:
        # Control 1: cut_manifest during backup.
        # Real code keeps partial write inside `.pending-*`, so CePreviewList sees NO new set.
        def check_cut_manifest(bin_key: str) -> tuple[bool, list]:
            d, w = self.new_profile("cut-" + bin_key)
            rc0, kv0, raw0 = self.call(w, "backup")
            self.assertEqual(rc0, 0, raw0)
            self.assertEqual(int(kv0["STATUS"]), COMPLETE, raw0)
            rc_l0, _, raw_l0 = self.call(w, "list")
            self.assertEqual(rc_l0, 0, raw_l0)
            ok0, before_items = parse_list(raw_l0)
            self.assertTrue(ok0 and len(before_items) == 1)

            rc1, kv1, raw1 = self.call(w, "backup", "fault=cut_manifest", bin_key=bin_key)
            self.assertEqual(rc1, 0, raw1)
            rc_l1, _, raw_l1 = self.call(w, "list")
            self.assertEqual(rc_l1, 0, raw_l1)
            ok1, after_items = parse_list(raw_l1)
            self.assertTrue(ok1)
            held = (
                int(kv1["STATUS"]) == REFUSED
                and kv1["BACKUP_SET"] == ""
                and after_items == before_items
            )
            return held, after_items

        real_ok, real_items = check_cut_manifest("fault32")
        self.assertTrue(real_ok, f"real backend failed cut_manifest check: {real_items}")
        mutant_ok, mutant_items = check_cut_manifest("mutant_nostage")
        self.assertFalse(
            mutant_ok,
            f"adverse mutant without .pending- staging was NOT caught: {mutant_items}",
        )

        # Control 2: competing target created immediately before publish rename.
        # Real code uses ReplaceIfExists=FALSE -> Occupied, competing bytes preserved.
        def check_race_publish(bin_key: str) -> tuple[bool, dict[str, str], bytes]:
            d, w = self.new_profile("race-" + bin_key)
            rc, kv, raw = self.call(w, "restore", "fault=race_publish", bin_key=bin_key)
            self.assertEqual(rc, 0, raw)
            target = d / "user/savegames/Amber restored.bes"
            content = target.read_bytes() if target.is_file() else b""
            leftover = [p.name for p in (d / "user/savegames").iterdir() if p.name.startswith(".pending-")]
            held = (
                int(kv["STATUS"]) == OCCUPIED
                and kv["NEW_FILE"] == ""
                and content == b"COMPETING_WRITER_SENTINEL"
                and not leftover
            )
            return held, kv, content

        race_ok, race_kv, race_bytes = check_race_publish("fault32")
        self.assertTrue(race_ok, f"real backend failed race_publish check: {race_kv} bytes={race_bytes[:32]!r}")
        mut_race_ok, mut_race_kv, mut_race_bytes = check_race_publish("mutant_replace")
        self.assertFalse(
            mut_race_ok,
            f"adverse mutant with ReplaceIfExists=TRUE was NOT caught: {mut_race_kv} len={len(mut_race_bytes)}",
        )

    def test_10_uncertain_post_publish_preserves_files_and_backup(self) -> None:
        # 1. Post-rename FlushFileBuffers failure in CeCareerRestoreAsNew -> Uncertain,
        # preserving BOTH the published newFile (exact payload, single link) and backupSet.
        d1, w1 = self.new_profile("uncertain-flush")
        rc1, kv1, raw1 = self.call(w1, "restore", "fault=post_publish_flush_fail", bin_key="fault32")
        self.assertEqual(rc1, 0, raw1)
        self.assertEqual(int(kv1["STATUS"]), UNCERTAIN, raw1)
        self.assertEqual(kv1["NEW_FILE"], "Amber restored.bes")
        self.assertTrue(kv1["BACKUP_SET"], raw1)
        restored1 = d1 / "user/savegames/Amber restored.bes"
        self.assertTrue(restored1.is_file())
        self.assertEqual(restored1.read_bytes(), self.previous)
        self.assertEqual(restored1.stat().st_nlink, 1)
        set1 = kv1["BACKUP_SET"].rsplit("\\", 1)[-1]
        self.assert_full_set_folder(
            d1 / "external" / set1,
            kv1["BACKUP_IDENTITY"],
            "Manual",
            {
                "Amber.bes": self.present,
                "Birch.bes": self.alternate,
                "Protected.bes": self.alternate,
                "defaultoptions.bea": b"disposable windows options",
            },
        )
        self.assert_originals_intact(d1)

        # 2. Concurrent post-publish byte tamper -> Uncertain, never deleted or rolled back.
        d2, w2 = self.new_profile("uncertain-tamper")
        rc2, kv2, raw2 = self.call(w2, "restore", "fault=post_publish_tamper", bin_key="fault32")
        self.assertEqual(rc2, 0, raw2)
        self.assertEqual(int(kv2["STATUS"]), UNCERTAIN, raw2)
        self.assertEqual(kv2["NEW_FILE"], "Amber restored.bes")
        self.assertTrue(kv2["BACKUP_SET"], raw2)
        restored2 = d2 / "user/savegames/Amber restored.bes"
        self.assertTrue(restored2.is_file())
        self.assertEqual(restored2.read_bytes()[0], self.previous[0] ^ 0xFF)
        self.assertEqual(restored2.read_bytes()[1:], self.previous[1:])
        self.assert_originals_intact(d2)

        # 3. Post-rename backup tamper -> CeCareerBackupAll & CeCareerTakeFullSet return Uncertain,
        # preserving backupSet; CeCareerTakeFullSet clears backupIdentity and backupStamp.
        for cmd in ("backup", "take"):
            d3, w3 = self.new_profile("uncertain-" + cmd)
            rc3, kv3, raw3 = self.call(w3, cmd, "fault=post_backup_tamper", bin_key="fault32")
            self.assertEqual(rc3, 0, raw3)
            self.assertEqual(int(kv3["STATUS"]), UNCERTAIN, (cmd, raw3))
            self.assertTrue(kv3["BACKUP_SET"], (cmd, raw3))
            set3 = d3 / "external" / kv3["BACKUP_SET"].rsplit("\\", 1)[-1]
            self.assertTrue(set3.is_dir(), (cmd, set3))
            if cmd == "take":
                self.assertEqual(kv3["BACKUP_IDENTITY"], "")
                self.assertEqual(kv3["BACKUP_STAMP"], "")
            self.assert_originals_intact(d3)

    def test_11_unsupported_non_ntfs_refusal(self) -> None:
        d, w = self.new_profile("unsupported-non-ntfs")
        before = tree_snapshot(d, ignore_lock=False)

        rc_s, kv_s, raw_s = self.call(w, "stamp", "fault=non_ntfs", bin_key="fault32")
        self.assertEqual(rc_s, 0, raw_s)
        self.assertEqual(kv_s["STAMP_OK"], "0")
        self.assertEqual(kv_s["STAMP"], "")

        rc_g, kv_g, raw_g = self.call(w, "suggest", "fault=non_ntfs", bin_key="fault32")
        self.assertEqual(rc_g, 0, raw_g)
        self.assertEqual(int(kv_g["STATUS"]), UNSUPPORTED, raw_g)
        self.assertEqual(kv_g["TARGET"], "")

        for cmd in ("backup", "take", "restore"):
            rc, kv, raw = self.call(w, cmd, "fault=non_ntfs", bin_key="fault32")
            self.assertEqual(rc, 0, raw)
            self.assertEqual(int(kv["STATUS"]), UNSUPPORTED, (cmd, raw))
            self.assertEqual(kv["BACKUP_SET"], "")
            self.assertEqual(kv["BACKUP_IDENTITY"], "")
            self.assertEqual(kv["BACKUP_STAMP"], "")
            self.assertEqual(kv["NEW_FILE"], "")

        # Non-NTFS check fails before creating lock or any backup/career files.
        self.assertEqual(tree_snapshot(d, ignore_lock=False), before)

    def test_12_ntfs_directory_junction_refusal(self) -> None:
        # 1. backupRoot is an NTFS directory junction (IO_REPARSE_TAG_MOUNT_POINT) -> Refused.
        d1, w1 = self.new_profile("junction-root")
        (d1 / "external").rmdir()
        (d1 / "junction-target").mkdir()
        rc1, kv1, raw1 = self.call(
            w1, "backup",
            f"junction-dir={w1}\\external|{w1}\\junction-target",
        )
        self.assertEqual(rc1, 0, raw1)
        self.assertEqual(int(kv1["STATUS"]), REFUSED, raw1)
        self.assertEqual(kv1["BACKUP_SET"], "")
        self.assertFalse(list((d1 / "junction-target").iterdir()))

        # 2. user\savegames is an NTFS directory junction -> Refused.
        d2, w2 = self.new_profile("junction-saves")
        shutil.move(str(d2 / "user/savegames"), str(d2 / "real-saves"))
        rc2, kv2, raw2 = self.call(
            w2, "backup",
            f"junction-dir={w2}\\user\\savegames|{w2}\\real-saves",
        )
        self.assertEqual(rc2, 0, raw2)
        self.assertEqual(int(kv2["STATUS"]), REFUSED, raw2)
        self.assertEqual(kv2["BACKUP_SET"], "")
        self.assertFalse(list((d2 / "external").iterdir()))

    def test_13_non_ascii_utf8_user_and_backup_paths(self) -> None:
        # Both user and backupRoot live under non-ASCII UTF-8 directory names.
        self.__class__.case_num += 1
        leaf = f"{self.__class__.case_num:02d}-utf8-Aur\u00e9-\u05e9\u05dc\u05d5\u05dd"
        d = self.drive_root / leaf
        user_rel = "profil-\u00e9l\u00e8ve"
        root_rel = "r\u00e9serve-\u05d2\u05d9\u05d1\u05d5\u05d9"
        for sub in (
            f"{user_rel}/savegames", "game/savegames", "package", root_rel,
            "source/onslaught-backup-20260405-123000",
        ):
            (d / sub).mkdir(parents=True)
        (d / "AUTHORED").write_text("disposable windows custody fixture\n", encoding="utf-8")
        (d / user_rel / "savegames/Amber.bes").write_bytes(self.present)
        (d / "game/savegames/Protected.bes").write_bytes(self.alternate)
        (d / user_rel / "defaultoptions.bea").write_bytes(b"utf8 path options")
        src = d / "source/onslaught-backup-20260405-123000"
        (src / "Amber.bes").write_bytes(self.previous)
        manifest = {
            "Folder": "C:\\authored-only",
            "Game": "C:\\authored-only",
            "Created": "2026-04-05T12:30:00Z",
            "Reason": "Manual",
            "Target": 1,
            "Files": [{
                "Name": "Amber.bes",
                "Original": "C:\\authored-only\\Amber.bes",
                "Size": len(self.previous),
                "Sha256": sha256(self.previous),
            }],
        }
        (src / "onslaught-backup.json").write_text(json.dumps(manifest), encoding="utf-8")

        w = f"C:\\{self.run_tag}\\{leaf}"
        w_user = f"user={w}\\{user_rel}"
        w_root = f"root={w}\\{root_rel}"

        # CeCareerBackupAll + CePreviewList on UTF-8 paths.
        rc_b, kv_b, raw_b = self.call(w, "backup", w_user, w_root)
        self.assertEqual(rc_b, 0, raw_b)
        self.assertEqual(int(kv_b["STATUS"]), COMPLETE, raw_b)
        set_b = kv_b["BACKUP_SET"].rsplit("\\", 1)[-1]
        self.assert_full_set_folder(
            d / root_rel / set_b,
            kv_b["BACKUP_IDENTITY"],
            "All careers",
            {
                "Amber.bes": self.present,
                "Protected.bes": self.alternate,
                "defaultoptions.bea": b"utf8 path options",
            },
        )

        # CeCareerTakeFullSet on UTF-8 paths.
        rc_t, kv_t, raw_t = self.call(w, "take", w_user, w_root)
        self.assertEqual(rc_t, 0, raw_t)
        self.assertEqual(int(kv_t["STATUS"]), COMPLETE, raw_t)

        # CeCareerRestoreSuggest + CeCareerRestoreAsNew on UTF-8 paths.
        rc_s, kv_s, raw_s = self.call(w, "suggest", w_user, w_root)
        self.assertEqual(rc_s, 0, raw_s)
        self.assertEqual(int(kv_s["STATUS"]), COMPLETE, raw_s)
        self.assertEqual(kv_s["TARGET"], "Amber restored.bes")

        rc_r, kv_r, raw_r = self.call(w, "restore", w_user, w_root)
        self.assertEqual(rc_r, 0, raw_r)
        self.assertEqual(int(kv_r["STATUS"]), COMPLETE, raw_r)
        self.assertEqual(kv_r["NEW_FILE"], "Amber restored.bes")
        self.assertEqual((d / user_rel / "savegames/Amber restored.bes").read_bytes(), self.previous)
        self.assertEqual((d / user_rel / "savegames/Amber.bes").read_bytes(), self.present)

        rc_l, _, raw_l = self.call(w, "list", w_user, w_root)
        self.assertEqual(rc_l, 0, raw_l)
        ok_l, items = parse_list(raw_l)
        self.assertTrue(ok_l and len(items) == 3 and all(it[1] for it in items), (ok_l, items))

    def test_14_defaultoptions_symlink_hardlink_hidden_system_dir_refusal(self) -> None:
        # 1. defaultoptions.bea is a Win32 symlink -> Refused.
        d1, w1 = self.new_profile("opt-symlink", with_options=False)
        (d1 / "user/real-options.bea").write_bytes(b"real options")
        rc1, kv1, raw1 = self.call(
            w1, "backup",
            f"symlink-file={w1}\\user\\defaultoptions.bea|{w1}\\user\\real-options.bea",
        )
        self.assertEqual(rc1, 0, raw1)
        self.assertEqual(int(kv1["STATUS"]), REFUSED, raw1)
        self.assertFalse(list((d1 / "external").iterdir()))

        # 2. defaultoptions.bea is a hardlink (NumberOfLinks == 2) -> Refused.
        d2, w2 = self.new_profile("opt-hardlink")
        os.link(d2 / "user/defaultoptions.bea", d2 / "user/defaultoptions-alias.bea")
        rc2, kv2, raw2 = self.call(w2, "backup")
        self.assertEqual(rc2, 0, raw2)
        self.assertEqual(int(kv2["STATUS"]), REFUSED, raw2)
        self.assertFalse(list((d2 / "external").iterdir()))

        # 3. defaultoptions.bea is hidden -> Refused.
        d3, w3 = self.new_profile("opt-hidden")
        rc3, kv3, raw3 = self.call(w3, "backup", f"hide={w3}\\user\\defaultoptions.bea")
        self.assertEqual(rc3, 0, raw3)
        self.assertEqual(int(kv3["STATUS"]), REFUSED, raw3)
        self.assertFalse(list((d3 / "external").iterdir()))

        # 4. defaultoptions.bea is system -> Refused.
        d4, w4 = self.new_profile("opt-system")
        rc4, kv4, raw4 = self.call(w4, "backup", f"system={w4}\\user\\defaultoptions.bea")
        self.assertEqual(rc4, 0, raw4)
        self.assertEqual(int(kv4["STATUS"]), REFUSED, raw4)
        self.assertFalse(list((d4 / "external").iterdir()))

        # 5. defaultoptions.bea is a directory -> Refused.
        d5, w5 = self.new_profile("opt-dir", with_options=False)
        (d5 / "user/defaultoptions.bea").mkdir()
        rc5, kv5, raw5 = self.call(w5, "backup")
        self.assertEqual(rc5, 0, raw5)
        self.assertEqual(int(kv5["STATUS"]), REFUSED, raw5)
        self.assertFalse(list((d5 / "external").iterdir()))

    def test_15_absent_savegames_directories_and_deferred_creation(self) -> None:
        # 1. Absent game\savegames with populated user\savegames succeeds across backup, take, suggest, and restore.
        d1, w1 = self.new_profile("no-game-savegames")
        shutil.rmtree(d1 / "game/savegames")
        rc_b, kv_b, raw_b = self.call(w1, "backup")
        self.assertEqual(rc_b, 0, raw_b)
        self.assertEqual(int(kv_b["STATUS"]), COMPLETE, raw_b)
        self.assertFalse((d1 / "game/savegames").exists())
        rc_r, kv_r, raw_r = self.call(w1, "restore")
        self.assertEqual(rc_r, 0, raw_r)
        self.assertEqual(int(kv_r["STATUS"]), COMPLETE, raw_r)
        self.assertEqual((d1 / "user/savegames/Amber restored.bes").read_bytes(), self.previous)
        self.assertFalse((d1 / "game/savegames").exists())

        # 2. Absent user\savegames with populated game\savegames:
        #    suggest, backup, take, and refused/cancelled restores MUST NOT create user\savegames;
        #    authorized restore creates user\savegames after backup + gates pass.
        d2, w2 = self.new_profile("no-user-savegames", with_options=False)
        shutil.rmtree(d2 / "user/savegames")
        rc_s2, kv_s2, raw_s2 = self.call(w2, "suggest")
        self.assertEqual(rc_s2, 0, raw_s2)
        self.assertEqual(int(kv_s2["STATUS"]), COMPLETE, raw_s2)
        self.assertEqual(kv_s2["TARGET"], "Amber restored.bes")
        self.assertFalse((d2 / "user/savegames").exists())

        rc_b2, kv_b2, raw_b2 = self.call(w2, "backup")
        self.assertEqual(rc_b2, 0, raw_b2)
        self.assertEqual(int(kv_b2["STATUS"]), COMPLETE, raw_b2)
        self.assertFalse((d2 / "user/savegames").exists())

        rc_deny, kv_deny, raw_deny = self.call(w2, "restore", "no-permit")
        self.assertEqual(rc_deny, 0, raw_deny)
        self.assertEqual(int(kv_deny["STATUS"]), REFUSED, raw_deny)
        self.assertFalse((d2 / "user/savegames").exists())

        rc_cancel, kv_cancel, raw_cancel = self.call(w2, "restore", "cancel")
        self.assertEqual(rc_cancel, 0, raw_cancel)
        self.assertEqual(int(kv_cancel["STATUS"]), CANCELLED, raw_cancel)
        self.assertFalse((d2 / "user/savegames").exists())

        rc_ok, kv_ok, raw_ok = self.call(w2, "restore", "target=Amber restored.bes")
        self.assertEqual(rc_ok, 0, raw_ok)
        self.assertEqual(int(kv_ok["STATUS"]), COMPLETE, raw_ok)
        self.assertEqual((d2 / "user/savegames/Amber restored.bes").read_bytes(), self.previous)

        # 3. Both user\savegames and game\savegames absent (zero existing files): mandatory backup fails closed
        #    and leaves user\savegames absent.
        d3, w3 = self.new_profile("both-savegames-absent", with_options=False)
        shutil.rmtree(d3 / "user/savegames")
        shutil.rmtree(d3 / "game/savegames")
        rc_r3, kv_r3, raw_r3 = self.call(w3, "restore", "target=Amber.bes")
        self.assertEqual(rc_r3, 0, raw_r3)
        self.assertEqual(int(kv_r3["STATUS"]), REFUSED, raw_r3)
        self.assertFalse((d3 / "user/savegames").exists())

        # 4. Regular file at user\savegames is refused.
        d4, w4 = self.new_profile("file-at-savegames")
        shutil.rmtree(d4 / "user/savegames")
        (d4 / "user/savegames").write_bytes(b"not a directory")
        rc_r4, kv_r4, raw_r4 = self.call(w4, "restore")
        self.assertEqual(rc_r4, 0, raw_r4)
        self.assertEqual(int(kv_r4["STATUS"]), REFUSED, raw_r4)


if __name__ == "__main__":
    os.umask(0o077)
    unittest.main()
