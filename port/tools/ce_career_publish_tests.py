#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Host & Windows/Wine unit tests and causal mutants for career publishing & safe external .bes import.

Compiles and runs:
1. Linux: real port/platform/linux/ce_career_publish.cpp, port/platform/linux/ce_career_restore.cpp,
   and port/platform/linux/ce_backup_preview.cpp with port/tools/tests/ce_career_publish_driver.cpp.
2. Windows (under a disposable private Wine prefix, cleaned up in tearDownClass): real
   port/platform/windows/ce_career_publish.cpp and port/platform/windows/ce_backup_preview.cpp
   cross-compiled with llvm-mingw (i686 & x86_64, -Wall -Wextra -Werror), verifying custody parity
   and causal mutants independently on disposable profiles under local-data/port/ce-career-publish-tests/.
"""
import hashlib
import os
from pathlib import Path
import resource
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
import deps  # noqa: E402

PLATFORM = ROOT / "port/platform"
TESTS = ROOT / "port/tools/tests"
TOOLCHAIN = deps.DATA / "llvm-mingw/bin"
UTF8_RC = PLATFORM / "windows/utf8.rc"
LOCK = "user/.ce-career-custody.lock"

SIZE = 10004
VERSION = 0x4BD1
MISSIONS = 0x0006
MISSION_STRIDE = 64
ROUTES = 0x1906
ROUTE_STRIDE = 8
GOODIES = 0x1F46
KILLS = 0x23F6

SDL_SHIM = """// Test shim only: SDL declarations used by ce_career_publish.cpp.
#pragma once
#define SDLCALL
extern "C" {
struct SDL_Window;
typedef struct SDL_DialogFileFilter { const char *name; const char *pattern; } SDL_DialogFileFilter;
typedef void (*SDL_DialogFileCallback)(void *userdata, const char *const *filelist, int filter);
const char *SDL_GetBasePath(void);
SDL_Window *SDL_GetKeyboardFocus(void);
void SDL_ShowOpenFileDialog(SDL_DialogFileCallback callback, void *userdata, SDL_Window *window,
                            const SDL_DialogFileFilter *filters, int nfilters,
                            const char *default_location, bool allow_many);
}
"""


def sha(data):
    return hashlib.sha256(data).hexdigest()


def make_career(kill_seed):
    buf = bytearray(SIZE)
    struct.pack_into("<H", buf, 0, VERSION)
    for n in range(100):
        at = MISSIONS + n * MISSION_STRIDE
        struct.pack_into("<i", buf, at + 0x08, -1)
        struct.pack_into("<i", buf, at + 0x0C, -1)
        struct.pack_into("<f", buf, at + 0x3C, -1.0)
    for r in range(200):
        struct.pack_into("<Ii", buf, ROUTES + r * ROUTE_STRIDE, 0, -1)
    for idx, (world, to_node) in enumerate(((100, 1), (110, 2), (200, -1))):
        at = MISSIONS + idx * MISSION_STRIDE
        struct.pack_into("<I", buf, at + 0x04, 1 if idx == 0 else 0)
        struct.pack_into("<i", buf, at + 0x08, idx * 2)
        struct.pack_into("<i", buf, at + 0x0C, idx * 2 + 1)
        struct.pack_into("<I", buf, at + 0x10, world)
        struct.pack_into("<f", buf, at + 0x3C, 1.0 if idx == 0 else -1.0)
        struct.pack_into("<Ii", buf, ROUTES + idx * 2 * ROUTE_STRIDE, 1 if idx == 0 else 0, to_node)
    struct.pack_into("<I", buf, KILLS, kill_seed & 0x00FFFFFF)
    struct.pack_into("<f", buf, 0x248E, 0.8)
    struct.pack_into("<f", buf, 0x2492, 0.7)
    return bytes(buf)


def snapshot_tree(base):
    """Map relative paths of regular files (excluding custody lock) to (sha256, inode)."""
    out = {}
    for p in sorted(base.rglob("*")):
        if p.is_symlink() or not p.is_file():
            continue
        rel = p.relative_to(base).as_posix()
        if rel == LOCK:
            continue
        out[rel] = (sha(p.read_bytes()), p.stat().st_ino)
    return out


def build_driver(job, publish_cpp, name, werror=True):
    exe = job / name
    cmd = [
        "c++", "-std=c++17", "-Wall", "-Wextra", *(["-Werror"] if werror else []), "-O1", "-pthread",
        "-I", str(job / "shim"),
        "-I", str(PLATFORM / "common"),
        str(publish_cpp),
        str(PLATFORM / "linux/ce_career_restore.cpp"),
        str(PLATFORM / "linux/ce_backup_preview.cpp"),
        str(TESTS / "ce_career_publish_driver.cpp"),
        "-Wl,--wrap=fsync",
        "-o", str(exe),
    ]
    done = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
    if done.returncode:
        raise RuntimeError(f"compile {name} failed:\n" + done.stdout + done.stderr)
    return exe


class CareerPublishTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        parent = ROOT / "local-data/port/ce-career-publish-tests"
        parent.mkdir(parents=True, exist_ok=True)
        cls.job = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
        shim = cls.job / "shim/SDL3"
        shim.mkdir(parents=True)
        (shim / "SDL.h").write_text(SDL_SHIM)
        cls.driver = build_driver(cls.job, PLATFORM / "linux/ce_career_publish.cpp", "driver")
        cls.existing = make_career(111)
        cls.game_slot = make_career(222)
        cls.hero = make_career(333)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.job, ignore_errors=True)

    def make_profile(self, name, empty_saves=False):
        base = self.job / name
        for sub in ("user/savegames", "user/subbackup", "game/savegames", "package", "backup", "external"):
            (base / sub).mkdir(parents=True)
        if not empty_saves:
            (base / "user/savegames/Existing.bes").write_bytes(self.existing)
            (base / "user/savegames/Again.bes").write_bytes(self.existing)
            (base / "user/savegames/Again imported.bes").write_bytes(self.existing)
            (base / "user/savegames/Again edited.bes").write_bytes(self.existing)
            (base / "user/defaultoptions.bea").write_bytes(b"authored options")
            (base / "game/savegames/GameSlot.bes").write_bytes(self.game_slot)
        (base / "external/Hero.bes").write_bytes(self.hero)
        (base / "package/Packaged.bes").write_bytes(self.hero)
        return base

    def run_case(self, driver, base, case):
        env = dict(os.environ)
        for k in ("DISPLAY", "WAYLAND_DISPLAY", "DBUS_SESSION_BUS_ADDRESS"):
            env.pop(k, None)
        return subprocess.run([str(driver), str(base), case], capture_output=True, text=True, timeout=60, env=env)

    def test_import_publishes_new_file_behind_verified_full_backup(self):
        base = self.make_profile("import_ok")
        before = snapshot_tree(base)
        res = self.run_case(self.driver, base, "import_ok")
        self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
        after = snapshot_tree(base)
        for rel, pair in before.items():
            self.assertEqual(after.get(rel), pair, f"{rel} was modified")
        new_saves = [r for r in after if r.startswith("user/savegames/") and r not in before]
        self.assertEqual(new_saves, ["user/savegames/Hero.bes"])
        self.assertEqual(after["user/savegames/Hero.bes"][0], sha(self.hero))
        self.assertEqual((base / "user/savegames/Hero.bes").stat().st_nlink, 1)
        sets = [p.name for p in (base / "backup").iterdir()]
        self.assertEqual(len(sets), 1)
        self.assertTrue(sets[0].startswith("onslaught-backup-"))
        self.assertTrue((base / "backup" / sets[0] / "onslaught-backup.json").is_file())
        self.assertTrue((base / "backup" / sets[0] / "Existing.bes").is_file())

    def test_import_collision_suggestions_and_length_bounds(self):
        base = self.make_profile("import_suggest")
        before = snapshot_tree(base)
        res = self.run_case(self.driver, base, "import_collision_suggest")
        self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
        self.assertEqual(snapshot_tree(base), before)

    def test_edit_publishes_new_file_and_keeps_original_untouched(self):
        base = self.make_profile("edit_ok")
        before = snapshot_tree(base)
        res = self.run_case(self.driver, base, "edit_ok")
        self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
        after = snapshot_tree(base)
        for rel, pair in before.items():
            self.assertEqual(after.get(rel), pair, f"{rel} was modified")
        new_saves = [r for r in after if r.startswith("user/savegames/") and r not in before]
        self.assertEqual(new_saves, ["user/savegames/Existing edited.bes"])
        self.assertEqual(after["user/savegames/Existing edited.bes"][0], sha(self.hero))
        sets = [p.name for p in (base / "backup").iterdir()]
        self.assertEqual(len(sets), 1)

    def test_collisions_refuse_without_overwrite(self):
        for case in ("occupied_user", "occupied_game"):
            with self.subTest(case=case):
                base = self.make_profile(case)
                before = snapshot_tree(base)
                res = self.run_case(self.driver, base, case)
                self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
                self.assertEqual(snapshot_tree(base), before)
                self.assertEqual(list((base / "backup").iterdir()), [])

    def test_backup_failures_and_gate_refusals_write_zero_careers(self):
        for case, empty in (
            ("no_backup_folder", False),
            ("backup_inside_user", False),
            ("backup_busy", False),
            ("backup_no_existing_careers", True),
            ("authority_refused", False),
            ("name_refused", False),
        ):
            with self.subTest(case=case):
                base = self.make_profile(case, empty_saves=empty)
                before = snapshot_tree(base)
                res = self.run_case(self.driver, base, case)
                self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
                self.assertEqual(snapshot_tree(base), before)
                self.assertEqual(list((base / "backup").iterdir()), [])

    def _populate_read_refusal_files(self, base):
        ext = base / "external"
        (ext / "SymTarget.bes").write_bytes(self.hero)
        os.symlink(ext / "SymTarget.bes", ext / "symlink.bes")
        os.symlink(ext, base / "ext_link")
        os.link(ext / "Hero.bes", ext / "hardlink.bes")
        (ext / "short.bes").write_bytes(self.hero[:-1])
        (ext / "long.bes").write_bytes(self.hero + b"\x00")
        (ext / "empty.bes").write_bytes(b"")
        badver = bytearray(self.hero)
        struct.pack_into("<H", badver, 0, VERSION + 1)
        (ext / "badver.bes").write_bytes(bytes(badver))
        implausible = bytearray(self.hero)
        struct.pack_into("<I", implausible, MISSIONS + 0x10, 9999)
        (ext / "implausible.bes").write_bytes(bytes(implausible))
        badgoodie = bytearray(self.hero)
        struct.pack_into("<I", badgoodie, GOODIES + 10 * 4, 7)
        (ext / "badgoodie.bes").write_bytes(bytes(badgoodie))
        (ext / "bidi\u202e.bes").write_bytes(self.hero)

    def test_external_import_reader_refuses_all_unsafe_and_corrupt_inputs(self):
        base = self.make_profile("read_refusals")
        self._populate_read_refusal_files(base)
        res = self.run_case(self.driver, base, "read_refusals")
        self.assertEqual(res.returncode, 0, res.stdout + res.stderr)

    def test_async_picker_generation_safety(self):
        base = self.make_profile("picker")
        res = self.run_case(self.driver, base, "picker_lifecycle")
        self.assertEqual(res.returncode, 0, res.stdout + res.stderr)

    def test_absent_savegames_directories_and_deferred_creation(self):
        base = self.make_profile("absent_dirs")
        for sub in ("no_game_saves/user/savegames", "no_game_saves/game", "no_game_saves/package", "no_game_saves/backup",
                    "no_user_saves/user", "no_user_saves/game/savegames", "no_user_saves/package", "no_user_saves/backup",
                    "both_absent/user", "both_absent/game", "both_absent/package", "both_absent/backup",
                    "file_at_saves/user", "file_at_saves/game", "file_at_saves/package", "file_at_saves/backup"):
            (base / sub).mkdir(parents=True)
        (base / "no_game_saves/user/savegames/Existing.bes").write_bytes(self.existing)
        (base / "no_user_saves/game/savegames/GameSlot.bes").write_bytes(self.game_slot)
        (base / "file_at_saves/user/savegames").write_bytes(b"not a directory")
        res = self.run_case(self.driver, base, "absent_savegames_dirs")
        self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
        self.assertFalse((base / "no_game_saves/game/savegames").exists())
        self.assertFalse((base / "both_absent/user/savegames").exists())
        self.assertEqual(sha((base / "no_user_saves/user/savegames/Hero.bes").read_bytes()), sha(self.hero))

    def test_uncertain_post_link_preserves_new_file_and_backup(self):
        for case in ("uncertain_after_link", "post_link_case_alias", "post_link_backup_swapped"):
            with self.subTest(case=case):
                base = self.make_profile(case)
                before = snapshot_tree(base)
                res = self.run_case(self.driver, base, case)
                self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
                after = snapshot_tree(base)
                for rel, pair in before.items():
                    self.assertEqual(after.get(rel), pair)
                self.assertEqual(after["user/savegames/Hero.bes"][0], sha(self.hero))
                sets = [p.name for p in (base / "backup").iterdir() if p.name.startswith("onslaught-backup-")]
                self.assertEqual(len(sets), 1)

    def test_causal_mutants_are_caught(self):
        source = (PLATFORM / "linux/ce_career_publish.cpp").read_text()
        mutants = (
            ("if (backupRes.status != CeCareerStorageStatus::Complete)\n\t\t{\n\t\t\tout.status = mapStorageStatus(backupRes.status);\n\t\t\treturn out;\n\t\t}",
             "// mutant: continue even when full backup fails",
             "backup_no_existing_careers", True, "publish must refuse when CeCareerTakeFullSet fails"),
            ("O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK",
             "O_RDONLY | O_CLOEXEC | O_NONBLOCK",
             "read_refusals", False, "import reader must refuse symlinks"),
            ("if (!S_ISREG(st.st_mode) || st.st_nlink != 1)",
             "if (!S_ISREG(st.st_mode))",
             "read_refusals", False, "import reader must refuse hard links"),
            ("if (!CeCareerViewBuild(buf, CE_CAREER_SIZE, &view) || !CeCareerViewPlausible(view))",
             "if (!CeCareerViewBuild(buf, CE_CAREER_SIZE, &view))",
             "read_refusals", False, "import reader must refuse implausible careers"),
            ("request->namePermitted(request->owner, sourceName.c_str(), target.c_str())",
             "true",
             "name_refused", False, "publish must enforce cheat-name safety gate"),
            ("require(CeCareerRestoreStamp(stampReq, stampNow) && stampNow == backupRes.backupStamp,\n\t\t\t\t\tCE_PUBLISH_CHANGED);",
             "// mutant: skip backup stamp verification",
             "post_link_backup_swapped", False, "publish must re-verify backupStamp"),
            ("if (!inDir.insert(folded).second)\n\t\t\treturn false;",
             "// mutant: allow duplicate case-folded names in savegames",
             "post_link_case_alias", False, "post-publish scan must reject concurrent case-alias collision"),
        )
        for idx, (old, new, case, empty, what) in enumerate(mutants):
            with self.subTest(mutant=what):
                if new == "true":
                    self.assertGreaterEqual(source.count(old), 1, f"{what}: expected at least 1 occurrence")
                    mutated = source.replace(old, new)
                else:
                    self.assertEqual(source.count(old), 1, f"{what}: expected 1 occurrence")
                    mutated = source.replace(old, new, 1)
                mut_root = self.job / f"mutant-{idx}"
                (mut_root / "platform/linux").mkdir(parents=True)
                os.symlink(PLATFORM / "common", mut_root / "platform/common")
                os.symlink(ROOT / "port/game", mut_root / "game")
                mut_cpp = mut_root / "platform/linux/ce_career_publish.cpp"
                mut_cpp.write_text(mutated)
                exe = build_driver(self.job, mut_cpp, f"mutant-driver-{idx}", werror=False)
                base = self.make_profile(f"mutant-prof-{idx}", empty_saves=empty)
                if case == "read_refusals":
                    self._populate_read_refusal_files(base)
                res = self.run_case(exe, base, case)
                self.assertNotEqual(res.returncode, 0, f"mutant went unnoticed: {what}\n{res.stdout}")


class WineEnv:
    """Isolated disposable Wine prefix with no display and file-backed stdout/stderr."""

    def __init__(self, scratch):
        self.wine = shutil.which("wine")
        self.wineserver = shutil.which("wineserver")
        self.wineboot = shutil.which("wineboot")
        if not (self.wine and self.wineserver and self.wineboot):
            raise RuntimeError("wine, wineserver, and wineboot are required")
        self.prefix = (scratch / "wineprefix").resolve()
        if self.prefix == (Path.home() / ".wine").resolve():
            raise RuntimeError("refusing to use ~/.wine")
        self.private = self.prefix / ".t-career-publish"
        self.io_dir = scratch / "wine-io"
        self.io_dir.mkdir(parents=True, exist_ok=True)
        self.started = False

    def env(self):
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

    def _run_raw(self, argv, timeout=60):
        with tempfile.TemporaryFile(dir=self.io_dir) as out_f, tempfile.TemporaryFile(dir=self.io_dir) as err_f:
            proc = subprocess.Popen(
                argv, stdin=subprocess.DEVNULL, stdout=out_f, stderr=err_f,
                env=self.env(), cwd=self.prefix, start_new_session=True,
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

    def start(self):
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
            [self.wineserver, "-p"], env=self.env(),
            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=30,
        )

    def run_exe(self, win_exe, args, timeout=60):
        return self._run_raw([self.wine, win_exe, *args], timeout=timeout)

    def stop(self):
        if self.started:
            for flag in ("-k", "-w"):
                subprocess.run(
                    [self.wineserver, flag], env=self.env(),
                    stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60,
                )


def build_windows_driver(out_dir, shim_dir, compiler, res_obj, publish_cpp, name, werror=True):
    exe = out_dir / name
    cmd = [
        str(compiler), "-std=c++17", "-Wall", "-Wextra", *(["-Werror"] if werror else []), "-O2", "-static",
        "-I", str(shim_dir),
        "-I", str(PLATFORM / "windows"),
        "-I", str(PLATFORM / "common"),
        "-I", str(ROOT / "port/game"),
        str(publish_cpp),
        str(PLATFORM / "windows/ce_backup_preview.cpp"),
        str(TESTS / "ce_career_publish_driver.cpp"),
        str(res_obj),
        "-o", str(exe),
    ]
    done = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=180)
    if done.returncode:
        raise RuntimeError(f"compile {name} failed:\n" + done.stdout + done.stderr)
    return exe


class WindowsCareerPublishTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        parent = ROOT / "local-data/port/ce-career-publish-tests"
        parent.mkdir(parents=True, exist_ok=True)
        cls.job = Path(tempfile.mkdtemp(prefix="win-", dir=parent))
        cls.shim_dir = cls.job / "shim"
        shim = cls.shim_dir / "SDL3"
        shim.mkdir(parents=True)
        (shim / "SDL.h").write_text(SDL_SHIM)

        try:
            cls.cxx32 = TOOLCHAIN / "i686-w64-mingw32-clang++"
            cls.cxx64 = TOOLCHAIN / "x86_64-w64-mingw32-clang++"
            windres32 = TOOLCHAIN / "i686-w64-mingw32-windres"
            windres64 = TOOLCHAIN / "x86_64-w64-mingw32-windres"
            for tool in (cls.cxx32, cls.cxx64, windres32, windres64):
                if not tool.is_file():
                    raise FileNotFoundError(f"missing toolchain tool: {tool}")

            cls.res32 = cls.job / "utf8-32.o"
            cls.res64 = cls.job / "utf8-64.o"
            for wr, obj in ((windres32, cls.res32), (windres64, cls.res64)):
                subprocess.run([str(wr), str(UTF8_RC), "-O", "coff", "-o", str(obj)],
                               cwd=UTF8_RC.parent, check=True, capture_output=True, timeout=60)

            cls.wine = WineEnv(cls.job)
            cls.wine.start()
            cls.drive_c = cls.wine.prefix / "drive_c"
            cls.work = cls.drive_c / "cepub"
            cls.work.mkdir(parents=True, exist_ok=True)

            # Compile unmodified production windows/ce_career_publish.cpp for both 32-bit and 64-bit Windows.
            cls.prod32 = build_windows_driver(
                cls.work, cls.shim_dir, cls.cxx32, cls.res32,
                PLATFORM / "windows/ce_career_publish.cpp", "pub32.exe", werror=True)
            cls.prod64 = build_windows_driver(
                cls.work, cls.shim_dir, cls.cxx64, cls.res64,
                PLATFORM / "windows/ce_career_publish.cpp", "pub64.exe", werror=True)

            cls.existing = make_career(111)
            cls.game_slot = make_career(222)
            cls.hero = make_career(333)
        except Exception:
            cls.tearDownClass()
            raise

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, "wine"):
            cls.wine.stop()
        shutil.rmtree(cls.job, ignore_errors=True)

    def make_profile(self, name):
        base = self.work / name
        for sub in ("user/savegames", "user/subbackup", "game/savegames", "package", "backup", "external"):
            (base / sub).mkdir(parents=True)
        (base / "user/savegames/Existing.bes").write_bytes(self.existing)
        (base / "user/savegames/Again.bes").write_bytes(self.existing)
        (base / "user/savegames/Again imported.bes").write_bytes(self.existing)
        (base / "user/savegames/Again edited.bes").write_bytes(self.existing)
        (base / "game/savegames/GameSlot.bes").write_bytes(self.game_slot)
        (base / "external/Hero.bes").write_bytes(self.hero)
        (base / "package/Packaged.bes").write_bytes(self.hero)
        return base, f"C:\\cepub\\{name}"

    def run_win(self, exe_name, win_base, case):
        return self.wine.run_exe(f"C:\\cepub\\{exe_name}", [win_base, case], timeout=60)

    def test_windows_import_edit_and_suggest_under_wine(self):
        for case, expected_new in (
            ("import_ok", "user/savegames/Hero.bes"),
            ("edit_ok", "user/savegames/Existing edited.bes"),
        ):
            with self.subTest(case=case):
                base, win_base = self.make_profile(case)
                before = snapshot_tree(base)
                res = self.run_win("pub32.exe", win_base, case)
                self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
                after = snapshot_tree(base)
                for rel, pair in before.items():
                    self.assertEqual(after.get(rel), pair, f"{rel} was modified")
                new_saves = [r for r in after if r.startswith("user/savegames/") and r not in before]
                self.assertEqual(new_saves, [expected_new])
                self.assertEqual(after[expected_new][0], sha(self.hero))
                self.assertEqual(len(list((base / "backup").iterdir())), 1)

        base, win_base = self.make_profile("suggest_and_reserved")
        before = snapshot_tree(base)
        res = self.run_win("pub32.exe", win_base, "suggest_and_reserved")
        self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
        self.assertEqual(snapshot_tree(base), before)

    def test_windows_custody_refusals_and_lock_contention_under_wine(self):
        for case, expect_backup in (
            ("occupied_user", False),
            ("occupied_game", False),
            ("no_backup_folder", False),
            ("backup_inside_user", False),
            ("user_inside_backup", False),
            ("backup_refused", False),
            ("authority_refused", False),
            ("name_refused", False),
            ("lock_busy", True),
        ):
            with self.subTest(case=case):
                base, win_base = self.make_profile(case)
                before = snapshot_tree(base)
                res = self.run_win("pub32.exe", win_base, case)
                self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
                after = snapshot_tree(base)
                for rel, pair in before.items():
                    self.assertEqual(after.get(rel), pair, f"{rel} was modified")
                new_saves = [r for r in after if r.startswith("user/savegames/") and r not in before]
                self.assertEqual(new_saves, [])
                if not expect_backup:
                    self.assertEqual(list((base / "backup").iterdir()), [])

    def test_windows_import_reader_and_picker_under_wine(self):
        base, win_base = self.make_profile("read_refusals")
        ext = base / "external"
        (ext / "hidden.bes").write_bytes(self.hero)
        (ext / "short.bes").write_bytes(self.hero[:-1])
        (ext / "long.bes").write_bytes(self.hero + b"\x00")
        (ext / "empty.bes").write_bytes(b"")
        badver = bytearray(self.hero)
        struct.pack_into("<H", badver, 0, VERSION + 1)
        (ext / "badver.bes").write_bytes(bytes(badver))
        implausible = bytearray(self.hero)
        struct.pack_into("<I", implausible, MISSIONS + 0x10, 9999)
        (ext / "implausible.bes").write_bytes(bytes(implausible))
        badgoodie = bytearray(self.hero)
        struct.pack_into("<I", badgoodie, GOODIES + 10 * 4, 7)
        (ext / "badgoodie.bes").write_bytes(bytes(badgoodie))
        (ext / "bidi\u202e.bes").write_bytes(self.hero)
        res = self.run_win("pub32.exe", win_base, "read_refusals")
        self.assertEqual(res.returncode, 0, res.stdout + res.stderr)

        pbase, pwin = self.make_profile("picker")
        res = self.run_win("pub32.exe", pwin, "picker_lifecycle")
        self.assertEqual(res.returncode, 0, res.stdout + res.stderr)

        abase, awin = self.make_profile("absent_dirs")
        for sub in ("no_game_saves/user/savegames", "no_game_saves/game", "no_game_saves/package", "no_game_saves/backup",
                    "no_user_saves/user", "no_user_saves/game/savegames", "no_user_saves/package", "no_user_saves/backup",
                    "file_at_saves/user", "file_at_saves/game", "file_at_saves/package", "file_at_saves/backup"):
            (abase / sub).mkdir(parents=True)
        (abase / "no_game_saves/user/savegames/Existing.bes").write_bytes(self.existing)
        (abase / "no_user_saves/game/savegames/GameSlot.bes").write_bytes(self.game_slot)
        (abase / "file_at_saves/user/savegames").write_bytes(b"not a directory")
        res = self.run_win("pub32.exe", awin, "absent_savegames_dirs")
        self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
        self.assertFalse((abase / "no_game_saves/game/savegames").exists())
        self.assertEqual(sha((abase / "no_user_saves/user/savegames/Hero.bes").read_bytes()), sha(self.hero))

    def test_windows_rename_race_and_causal_mutants_under_wine(self):
        source = (PLATFORM / "windows/ce_career_publish.cpp").read_text()
        hook_decl = "extern void CePublishTestBeforeRename(HANDLE, const wchar_t *);\n"
        anchor_inc = '#include "ce_backup_paths.h"\n'
        anchor_ren = "\t\tconst NTSTATUS renameStatus = renameNoReplace(stage.h, saves.h, targetWide);"
        self.assertEqual(source.count(anchor_inc), 1)
        self.assertEqual(source.count(anchor_ren), 1)
        instrumented = source.replace(anchor_inc, anchor_inc + hook_decl, 1).replace(
            anchor_ren,
            "\t\tCePublishTestBeforeRename(saves.h, targetWide.c_str());\n" + anchor_ren,
            1,
        )
        inst_root = self.job / "win-inst/platform/windows"
        inst_root.mkdir(parents=True)
        os.symlink(PLATFORM / "common", self.job / "win-inst/platform/common")
        os.symlink(ROOT / "port/game", self.job / "win-inst/game")
        os.symlink(PLATFORM / "windows/ce_backup_paths.h", inst_root / "ce_backup_paths.h")
        inst_cpp = inst_root / "ce_career_publish.cpp"
        inst_cpp.write_text(instrumented)
        build_windows_driver(self.work, self.shim_dir, self.cxx32, self.res32, inst_cpp, "inst32.exe", werror=True)

        base, win_base = self.make_profile("race_ok")
        res = self.run_win("inst32.exe", win_base, "rename_race_occupied")
        self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
        # Competing Hero.bes kept Existing bytes (never overwritten!).
        self.assertEqual(sha((base / "user/savegames/Hero.bes").read_bytes()), sha(self.existing))

        mutants = (
            ("!user.below(backupRoot) && !game.below(backupRoot) && !package.below(backupRoot)",
             "true",
             "user_inside_backup", False, "Windows publish must reject backupRoot as ancestor of user"),
            ("if (!LockFileEx(h.h, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &ov))",
             "if (false)",
             "lock_busy", False, "Windows publish must acquire exclusive custody lock"),
            ("require(taken.count(targetWide) == 0 &&\n\t\t\t\t\t\t!existsEntry(saves.h, targetWide) && !existsEntry(orig.h, targetWide),\n\t\t\t\t\tCE_PUBLISH_OCCUPIED);",
             "// mutant: skip pre-flight occupied check before backup",
             "occupied_user", False, "Windows publish must refuse occupied names before CeCareerTakeFullSet"),
            ("rename->ReplaceIfExists = FALSE;",
             "rename->ReplaceIfExists = TRUE;",
             "rename_race_occupied", True, "Windows publish must rename with ReplaceIfExists=FALSE"),
        )
        for idx, (old, new, case, use_inst, what) in enumerate(mutants):
            with self.subTest(mutant=what):
                base_src = instrumented if use_inst else source
                self.assertEqual(base_src.count(old), 1, f"{what}: expected 1 occurrence")
                mroot = self.job / f"wmut-{idx}/platform/windows"
                mroot.mkdir(parents=True)
                os.symlink(PLATFORM / "common", self.job / f"wmut-{idx}/platform/common")
                os.symlink(ROOT / "port/game", self.job / f"wmut-{idx}/game")
                os.symlink(PLATFORM / "windows/ce_backup_paths.h", mroot / "ce_backup_paths.h")
                mcpp = mroot / "ce_career_publish.cpp"
                mcpp.write_text(base_src.replace(old, new, 1))
                exe_name = f"wmut-{idx}.exe"
                build_windows_driver(self.work, self.shim_dir, self.cxx32, self.res32, mcpp, exe_name, werror=False)
                mbase, mwin = self.make_profile(f"wmut-prof-{idx}")
                mres = self.run_win(exe_name, mwin, case)
                self.assertNotEqual(mres.returncode, 0, f"Windows mutant went unnoticed: {what}\n{mres.stdout}")


if __name__ == "__main__":
    unittest.main(verbosity=2)
