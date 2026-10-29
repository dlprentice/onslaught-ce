#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Linux atomic career replace (patch 0117): the real path layer, file layer and PortReplaceUserFile over authored folders.

    python3 port/tools/save_replace_tests.py [-v]

Needs the pinned LLVM compiler and dxvk-native headers from deps.py, plus host 32-bit C/C++ development libraries (as
files_tests.py). The files are short authored sentinels, never careers; the game, its data and any real folder are not
touched. Failures are injected with linker wrappers that exist only in the test executable (tests/save_replace_faults.cpp):
write errors and short writes, a full disk, a failed flush, a corrupted read-back, a failed rename, a process killed between
the write and the rename, a swapped temporary file. A real full disk is made with a size-limited tmpfs in a private user
namespace. Every failure must leave the previous file byte-identical and remove only the temporary file the call made.
The temporary file's name and creation are pinned with real objects, not injected errors: files and links planted at the
names a call is about to draw must be skipped and left alone, and two writers drawing names at the same moment must never meet a
taken one. Mutants of save_replace.cpp (no read-back, no identity check, write in place, no O_EXCL, a name that never advances,
a counter that restarts with every call) must be caught by these same checks; dropping only O_NOFOLLOW is an equivalent mutant,
shown as such.
"""
import hashlib
import os
from pathlib import Path
import re
import resource
import shutil
import signal
import subprocess
import tempfile
import threading
import unittest

import deps

HEADER = deps.PORT / "platform/common/save_replace.h"
CODES = {name: int(value) for name, value in
         zip(re.findall(r"^\s*(PORT_REPLACE_[A-Z]+)", HEADER.read_text(), re.M), range(100))}
WRAPS = ",".join("--wrap=" + n for n in ("write", "pread", "fsync", "close", "openat", "renameat", "fstatat", "usleep"))


def sha(data):
    return hashlib.sha256(data).hexdigest()


def payload(tag, size):
    block = hashlib.sha256(tag.encode()).digest()
    return (block * (size // len(block) + 1))[:size]


def compile_driver(source_dir, out, replace_source=None, extra=()):
    platform = deps.PORT / "platform/linux"
    cmd = [str(deps.DATA / "llvm/bin/clang++"), "--target=i686-pc-linux-gnu", "-m32", "-pthread", "-std=c++17",
           "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-fshort-wchar", "-fms-extensions",
           "-fno-autolink", "-w", "-O0", "-g", f"-I{platform}", f"-I{deps.PORT / 'platform'}", f"-I{deps.PORT / 'include/linux'}",
           f"-I{deps.DATA / 'dxvk-native/usr/include/dxvk'}",
           str(source_dir / "save_replace_driver.cpp"), str(source_dir / "save_replace_faults.cpp"),
           str(platform / "files.cpp"), str(platform / "paths.cpp"), str(replace_source or platform / "save_replace.cpp"),
           str(source_dir / "mods_none.cpp"),
           f"-Wl,{WRAPS}", *extra, "-o", str(out)]
    subprocess.run(cmd, check=True, capture_output=True, text=True)


class Fixture(unittest.TestCase):
    """Two authored roots and a way to run the driver in them."""

    driver = None
    scratch = None

    @classmethod
    def setUpClass(cls):
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        parent = deps.WORK / "tests"
        parent.mkdir(parents=True, exist_ok=True)
        cls.scratch = tempfile.TemporaryDirectory(prefix="save-replace-build-", dir=parent)
        cls.addClassCleanup(cls.scratch.cleanup)
        cls.driver = Path(cls.scratch.name) / "driver"
        compile_driver(deps.PORT / "tools/tests", cls.driver)

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="save-replace-case-", dir=deps.WORK / "tests")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.game = self.root / "game"
        self.user = self.root / "user"
        self.payloads = self.root / "payloads"
        (self.game / "SaveGames").mkdir(parents=True)
        self.payloads.mkdir()
        (self.game / "SaveGames" / "Hawk.bes").write_bytes(b"ORIGINAL GAME HAWK " + payload("game", 600))
        self.game_before = self.tree(self.game)
        self.addCleanup(lambda: self.assertEqual(self.game_before, self.tree(self.game), "the game folder changed"))

    @staticmethod
    def tree(root):
        out = {}
        for p in sorted(root.rglob("*")):
            rel = str(p.relative_to(root))
            st = p.lstat()
            if p.is_symlink():
                out[rel] = ("link", os.readlink(p))
            elif p.is_dir():
                out[rel] = ("dir",)
            elif p.is_file():
                out[rel] = ("file", sha(p.read_bytes()), st.st_mtime_ns, st.st_ino, st.st_mode & 0o7777)
            else:
                out[rel] = ("other", st.st_mode)
        return out

    def user_save(self, name="Hawk.bes", data=b"USER OLD HAWK ", size=900):
        folder = self.user / "SaveGames"
        folder.mkdir(parents=True, exist_ok=True)
        data = data + payload(name, size)
        (folder / name).write_bytes(data)
        return data

    def run_driver(self, *args, fault=None, game=None, user=None, timeout=60, env_extra=None):
        env = dict(os.environ, BEA_GAME_DIR=str(game or self.game), BEA_USER_DIR=str(user or self.user),
                   HOME=str(self.root))
        for name in ("SAVE_TEST_FAULT", "SAVE_TEST_N", "SAVE_TEST_SLEEPS"):
            env.pop(name, None)
        if fault:
            env["SAVE_TEST_FAULT"] = fault
        env.update(env_extra or {})
        return subprocess.run([str(self.driver), *map(str, args)], env=env, capture_output=True, timeout=timeout,
                              cwd=self.root)

    def replace(self, game_path, data, fault=None, **kw):
        f = self.payloads / ("p-" + sha(data)[:12])
        f.write_bytes(data)
        r = self.run_driver("replace", game_path, f, fault=fault, **kw)
        code = int(r.stdout.decode().strip()) if r.returncode == 0 and r.stdout.strip() else None
        return code, r

    def temporaries(self):
        return sorted(p.name for p in self.user.rglob(".ce-write-*"))

    LOOKS = 10                                                  # save_replace.cpp: the career's name is looked up at most this often
    WAITS = [50 << i for i in range(LOOKS - 1)]                 # 50, 100 ... 12,800 microseconds, none after the last look

    def sleeps(self, log):
        """The microseconds save_replace.cpp asked to sleep, in order (the fault wrapper's record)."""
        return [int(x) for x in log.read_text().split()] if log.exists() else []

    def career(self, name="Hawk.bes"):
        return self.user / "SaveGames" / name

    def collide(self, count, kind, binary=None, name="Hawk.bes", data=None):
        """The driver plants `count` real objects of `kind` (file, symlink-dangling, symlink-file) at the temporary names it is
        about to draw, then replaces `name`. Returns (result code, the driver's pid, the run, the new bytes)."""
        data = data or payload("collide", 3000)
        f = self.payloads / ("p-" + sha(data)[:12])
        f.write_bytes(data)
        saved, self.driver = self.driver, binary or self.driver
        try:
            r = self.run_driver("collide", self.user / "SaveGames", "savegames\\" + name, f, count, kind)
        finally:
            self.driver = saved
        fields = r.stdout.decode().split()
        if r.returncode != 0 or len(fields) != 2:
            self.fail(f"the collide command failed ({r.returncode}): {r.stdout!r} {r.stderr!r}")
        return int(fields[0]), int(fields[1]), r, data

    def damage_to_planted(self, pid, count, kind):
        """What became of the objects `collide` planted: a list of problems, empty when every one is exactly as planted, nothing
        they point at changed, and no other temporary file is left."""
        folder = self.user / "SaveGames"
        problems = []
        for k in range(1, count + 1):
            p = folder / f".ce-write-{pid}-{k}"
            if kind == "file":
                if p.is_symlink() or not p.is_file() or p.read_bytes() != f"PLANTED-{k}".encode():
                    problems.append(f"the planted file {k} was changed or removed")
                continue
            want = f"ce-planted-missing-{k}" if kind == "symlink-dangling" else f"../outside-{k}"
            if not p.is_symlink() or os.readlink(p) != want:
                problems.append(f"the planted link {k} was replaced or removed")
            if kind == "symlink-dangling" and os.path.lexists(folder / f"ce-planted-missing-{k}"):
                problems.append(f"the planted link {k} was written through: what it pointed at now exists")
            if kind == "symlink-file" and (self.user / f"outside-{k}").read_bytes() != f"OUTSIDE-{k}".encode():
                problems.append(f"the planted link {k} was written through: the file it pointed at changed")
        extra = sorted(set(self.temporaries()) - {f".ce-write-{pid}-{k}" for k in range(1, count + 1)})
        if extra:
            problems.append(f"temporary files that are not the planted ones are left: {extra}")
        return problems

    def race(self, binary=None, rounds=40):
        """Two threads of one process replace two careers of one folder at the same moment, `rounds` times. Returns the driver's
        counters ({rounds, failed, eexist}) and the run."""
        self.user_save("Hawk.bes")
        self.user_save("Eagle.bes")
        pa, pb = self.payloads / "race-a", self.payloads / "race-b"
        pa.write_bytes(payload("race-a", 6000))
        pb.write_bytes(payload("race-b", 7000))
        saved, self.driver = self.driver, binary or self.driver
        try:
            r = self.run_driver("race", r"savegames\Hawk.bes", pa, r"savegames\Eagle.bes", pb, rounds)
        finally:
            self.driver = saved
        if r.returncode != 0:
            self.fail(f"the race command failed ({r.returncode}): {r.stdout!r} {r.stderr!r}")
        return {k: int(v) for k, v in (part.split("=") for part in r.stdout.decode().split())}, r


class AtomicReplace(Fixture):
    def test_creates_a_new_career_and_leaves_no_temporary_file(self):
        data = payload("new", 3000)
        code, r = self.replace(r"savegames\Fresh.bes", data)
        self.assertEqual((CODES["PORT_REPLACE_OK"], b""), (code, r.stderr))
        self.assertEqual(data, self.career("Fresh.bes").read_bytes())
        self.assertEqual([], self.temporaries())
        self.assertEqual(["Fresh.bes"], sorted(p.name for p in (self.user / "SaveGames").iterdir()))

    def test_replaces_a_user_career_with_exactly_the_new_bytes_and_keeps_its_permissions(self):
        self.user_save()
        self.career().chmod(0o640)
        data = payload("new", 4096)
        code, r = self.replace(r"savegames\Hawk.bes", data)
        self.assertEqual(CODES["PORT_REPLACE_OK"], code)
        self.assertEqual(data, self.career().read_bytes())
        self.assertEqual(0o640, self.career().stat().st_mode & 0o7777)
        self.assertEqual([], self.temporaries())

    def test_a_career_longer_than_the_read_back_chunk(self):
        self.user_save()
        data = payload("big", 200_000)
        code, _ = self.replace(r"savegames\Hawk.bes", data)
        self.assertEqual((CODES["PORT_REPLACE_OK"], data), (code, self.career().read_bytes()))

    def test_a_career_that_exists_only_in_the_game_folder_gets_a_shadow_copy(self):
        data = payload("shadow", 1500)
        original = (self.game / "SaveGames" / "Hawk.bes").read_bytes()
        self.assertFalse(self.user.exists())
        code, _ = self.replace(r"savegames\Hawk.bes", data)
        self.assertEqual(CODES["PORT_REPLACE_OK"], code)
        self.assertEqual(data, self.career().read_bytes())
        self.assertEqual(original, (self.game / "SaveGames" / "Hawk.bes").read_bytes())
        # the game's own reads now find the shadow, and the listing names the career once
        self.assertEqual(data, self.run_driver("read", r"savegames\Hawk.bes").stdout)
        self.assertEqual(f"Hawk.bes\t{len(data)}\n", self.run_driver("list", r"savegames\*.bes").stdout.decode())

    def test_the_case_of_the_existing_name_is_kept(self):
        old = self.user_save("HAWK.BES")
        code, _ = self.replace(r"savegames\hawk.bes", payload("case", 700))
        self.assertEqual(CODES["PORT_REPLACE_OK"], code)
        self.assertEqual(["HAWK.BES"], sorted(p.name for p in (self.user / "SaveGames").iterdir()))
        self.assertNotEqual(old, self.career("HAWK.BES").read_bytes())

    def test_the_options_file_in_the_user_root_uses_the_same_path(self):
        (self.game / "defaultoptions.bea").write_bytes(b"GAME OPTIONS")
        self.game_before = self.tree(self.game)
        code, _ = self.replace("defaultoptions.bea", payload("options", 5000))
        self.assertEqual(CODES["PORT_REPLACE_OK"], code)
        self.assertEqual(payload("options", 5000), (self.user / "defaultoptions.bea").read_bytes())

    # --- every injected failure leaves the previous file intact --------------------------------------------------------
    FAILURES = [
        ("write-eio", "PORT_REPLACE_WRITE"), ("enospc", "PORT_REPLACE_WRITE"), ("write-zero", "PORT_REPLACE_WRITE"),
        ("close-eio", "PORT_REPLACE_WRITE"), ("fsync-eio", "PORT_REPLACE_SYNC"),
        ("readback-flip", "PORT_REPLACE_VERIFY"), ("readback-short", "PORT_REPLACE_VERIFY"),
        ("readback-eio", "PORT_REPLACE_VERIFY"), ("write-dropped", "PORT_REPLACE_VERIFY"),
        ("rename-eio", "PORT_REPLACE_PUBLISH"), ("rename-exdev", "PORT_REPLACE_PUBLISH"),
        ("temp-exists", "PORT_REPLACE_CREATE"), ("temp-eacces", "PORT_REPLACE_CREATE"),
    ]

    def test_every_failure_keeps_the_previous_career_byte_for_byte(self):
        old = self.user_save()
        for fault, expected in self.FAILURES:
            with self.subTest(fault=fault):
                before = self.tree(self.user)
                code, r = self.replace(r"savegames\Hawk.bes", payload("new-" + fault, 5000), fault=fault)
                self.assertEqual(CODES[expected], code, r.stderr)
                self.assertEqual(before, self.tree(self.user), "the user folder changed")
                self.assertEqual(old, self.career().read_bytes())
                self.assertEqual([], self.temporaries())
                log = r.stderr.decode()
                self.assertEqual(1, log.count("career was not saved"), log)
                self.assertIn("the previous file is unchanged", log)
                self.assertNotIn("Hawk", log)   # the log never names the career

    def test_a_failed_first_write_of_a_game_only_career_creates_no_shadow(self):
        for fault, expected in self.FAILURES:
            with self.subTest(fault=fault):
                code, r = self.replace(r"savegames\Hawk.bes", payload("new", 5000), fault=fault)
                self.assertEqual(CODES[expected], code, r.stderr)
                self.assertFalse((self.user / "SaveGames" / "Hawk.bes").exists())
                self.assertEqual([], self.temporaries())

    def test_survivable_hiccups_still_write_the_exact_bytes(self):
        self.user_save()
        for fault in ("short-writes", "write-eintr", "dirsync-einval", "dirsync-enosys"):
            with self.subTest(fault=fault):
                data = payload("hiccup-" + fault, 3333)
                code, r = self.replace(r"savegames\Hawk.bes", data, fault=fault)
                self.assertEqual((CODES["PORT_REPLACE_OK"], b""), (code, r.stderr))
                self.assertEqual(data, self.career().read_bytes())
                self.assertEqual([], self.temporaries())

    def test_a_failed_folder_flush_is_reported_but_the_complete_new_file_is_in_place(self):
        self.user_save()
        data = payload("durable", 2500)
        code, r = self.replace(r"savegames\Hawk.bes", data, fault="dirsync-eio")
        self.assertEqual(CODES["PORT_REPLACE_DURABLE"], code)
        self.assertEqual(data, self.career().read_bytes())
        self.assertEqual([], self.temporaries())
        self.assertIn("save again", r.stderr.decode())

    def test_only_the_call_s_own_temporary_file_is_removed(self):
        old = self.user_save()
        # a temporary file left by a crashed run, and one that another process swaps into the temporary name
        stale = self.user / "SaveGames" / ".ce-write-1-1"
        stale.write_bytes(b"STALE FROM ANOTHER RUN")
        code, r = self.replace(r"savegames\Hawk.bes", payload("swap", 2000), fault="swap-temp")
        self.assertEqual(CODES["PORT_REPLACE_PUBLISH"], code, r.stderr)
        self.assertEqual(old, self.career().read_bytes())
        self.assertEqual(b"STALE FROM ANOTHER RUN", stale.read_bytes())
        swapped = [p for p in (self.user / "SaveGames").glob(".ce-write-*") if p.read_bytes() == b"SWAPPED"]
        self.assertEqual(1, len(swapped), "the file swapped into the name was removed")

    # --- crashes ------------------------------------------------------------------------------------------------------
    def test_a_crash_between_the_write_and_the_rename_keeps_the_old_career_and_lists_no_ghost(self):
        old = self.user_save()
        code, r = self.replace(r"savegames\Hawk.bes", payload("crash", 6000), fault="crash-before-rename")
        self.assertEqual(-signal.SIGKILL, r.returncode)
        self.assertEqual(old, self.career().read_bytes())
        left = self.temporaries()
        self.assertEqual(1, len(left), left)
        self.assertEqual(payload("crash", 6000), (self.user / "SaveGames" / left[0]).read_bytes(), "the temp is complete")
        # the game's listing and read still see exactly the old career
        self.assertEqual(f"Hawk.bes\t{len(old)}\n", self.run_driver("list", r"savegames\*.bes").stdout.decode())
        self.assertEqual(old, self.run_driver("read", r"savegames\Hawk.bes").stdout)
        # the next save works, and does not touch the stale file
        data = payload("after", 3000)
        code, _ = self.replace(r"savegames\Hawk.bes", data)
        self.assertEqual(CODES["PORT_REPLACE_OK"], code)
        self.assertEqual(data, self.career().read_bytes())
        self.assertEqual(left, self.temporaries())

    def test_a_crash_in_the_middle_of_writing_keeps_the_old_career(self):
        old = self.user_save()
        code, r = self.replace(r"savegames\Hawk.bes", payload("midway", 6000), fault="crash-mid-write")
        self.assertEqual(-signal.SIGKILL, r.returncode)
        self.assertEqual(old, self.career().read_bytes())
        self.assertEqual(f"Hawk.bes\t{len(old)}\n", self.run_driver("list", r"savegames\*.bes").stdout.decode())

    def test_a_crash_after_the_rename_leaves_the_complete_new_career(self):
        self.user_save()
        data = payload("done", 6000)
        code, r = self.replace(r"savegames\Hawk.bes", data, fault="crash-after-rename")
        self.assertEqual(-signal.SIGKILL, r.returncode)
        self.assertEqual(data, self.career().read_bytes())
        self.assertEqual([], self.temporaries())

    # --- targets that are not a plain file ------------------------------------------------------------------------------
    def test_a_symlink_to_the_original_is_refused_and_the_original_is_untouched(self):
        folder = self.user / "SaveGames"
        folder.mkdir(parents=True)
        os.symlink(self.game / "SaveGames" / "Hawk.bes", folder / "Hawk.bes")
        before = self.tree(self.user)
        code, r = self.replace(r"savegames\Hawk.bes", payload("link", 1000))
        self.assertEqual(CODES["PORT_REPLACE_REFUSED"], code, r.stderr)
        self.assertEqual(before, self.tree(self.user))

    def unlinked(self, fault, n=None, data=None, old=None):
        """One replace of the existing career while the wrapper shows its name without a link as FAULT says; returns the status,
        the process result, the sleeps save_replace.cpp asked for and the bytes it was asked to save."""
        log = self.root / "sleeps.txt"
        log.unlink(missing_ok=True)
        data = data or payload("loser", 2000)
        code, r = self.replace(r"savegames\Hawk.bes", data, fault=fault,
                               env_extra={"SAVE_TEST_SLEEPS": str(log), **({} if n is None else {"SAVE_TEST_N": str(n)})})
        return code, r, self.sleeps(log), data

    def test_an_ordinary_save_never_sleeps(self):
        """The wait is only for a name that shows no link: replacing a career, creating one, and shadowing a game-folder one all go
        straight through."""
        log = self.root / "sleeps.txt"
        folder = self.user / "SaveGames"
        for case in ("existing", "new", "shadow"):
            with self.subTest(case=case):
                (folder / "Hawk.bes").unlink(missing_ok=True)
                (folder / "New.bes").unlink(missing_ok=True)
                if case == "existing":
                    self.user_save()
                name = "New.bes" if case == "new" else "Hawk.bes"     # shadow: Hawk.bes exists only in the game folder
                folder.mkdir(parents=True, exist_ok=True)
                code, r = self.replace(rf"savegames\{name}", payload(case, 3000), env_extra={"SAVE_TEST_SLEEPS": str(log)})
                self.assertEqual(CODES["PORT_REPLACE_OK"], code, r.stderr)
                self.assertEqual([], self.sleeps(log), f"a {case} career made the save sleep")

    def test_a_career_unlinked_by_another_replace_during_the_check_is_waited_for_and_replaced(self):
        """Two processes replacing the same career at the same moment: the second looks the name up, the first one's rename lands, and
        the attributes it then reads belong to a file with no link left (st_nlink 0). It used to refuse (about 0.3 % of tightly looped
        replaces by two writers on tmpfs). Now it waits 50 microseconds, looks again, finds the other save's complete file and
        replaces that. The fault wrapper makes the same observation once, deterministically; the sleep is recorded."""
        self.user_save()
        code, r, sleeps, data = self.unlinked("target-unlinked-once")
        self.assertEqual(CODES["PORT_REPLACE_OK"], code, r.stderr)
        self.assertEqual(data, self.career().read_bytes())
        self.assertEqual([], self.temporaries())
        self.assertNotIn("was not saved", r.stderr.decode())
        self.assertEqual([50], sleeps, "one wait of 50 microseconds before the second look, none after it")

    def test_the_waits_double_and_the_last_look_is_not_followed_by_one(self):
        """A name that never shows a link is looked up ten times with waits of 50 microseconds doubling (25.55 ms in all) and is then
        refused: the career is untouched, nothing is left behind, the next save works."""
        old = self.user_save()
        code, r, sleeps, _ = self.unlinked("target-unlinked-always", data=payload("loser", 2000))
        self.assertEqual(CODES["PORT_REPLACE_REFUSED"], code, r.stderr)
        self.assertEqual(old, self.career().read_bytes())
        self.assertEqual([], self.temporaries())
        self.assertIn("career was not saved: the file cannot be written", r.stderr.decode())
        self.assertIn("the previous file is unchanged", r.stderr.decode())
        self.assertEqual(self.WAITS, sleeps)
        self.assertEqual(25550, sum(sleeps))
        data = payload("retry", 2500)
        code, _ = self.replace(r"savegames\Hawk.bes", data)
        self.assertEqual(CODES["PORT_REPLACE_OK"], code)
        self.assertEqual(data, self.career().read_bytes())

    def test_the_name_is_looked_up_ten_times_not_more_and_not_fewer(self):
        """The first nine looks showing no link and the tenth a file: replaced. All ten: refused. (An independent review found the suites
        pinned neither number: a loop of two looks or a hundred passed them.)"""
        self.user_save()
        code, r, sleeps, data = self.unlinked("target-unlinked-looks", n=self.LOOKS - 1)
        self.assertEqual(CODES["PORT_REPLACE_OK"], code, r.stderr)
        self.assertEqual(data, self.career().read_bytes())
        self.assertEqual(self.WAITS, sleeps)
        code, r, sleeps, _ = self.unlinked("target-unlinked-looks", n=self.LOOKS, data=payload("late", 2000))
        self.assertEqual(CODES["PORT_REPLACE_REFUSED"], code, r.stderr)
        self.assertEqual(data, self.career().read_bytes(), "the first replace's career is what is left")
        self.assertEqual(self.WAITS, sleeps)

    def test_a_rename_that_takes_a_while_is_waited_out_and_one_that_takes_too_long_is_refused(self):
        """What the file system does: the other save's rename keeps the old file at no link for as long as it takes (about a microsecond
        on tmpfs, 40 to 400 microseconds on btrfs, milliseconds when the process is descheduled in it). The wrapper reports no link for
        that long from the first look, however many looks there are: 20 microseconds to 20 milliseconds are waited out (the immediate
        looks that were tried first spanned about 5 microseconds and did not get past 20), a window longer than the 25.55 ms of waits
        is refused."""
        for microseconds in (20, 100, 400, 1500, 6000, 20000):
            with self.subTest(window_us=microseconds):
                self.user_save()
                code, r, sleeps, data = self.unlinked("target-unlinked-for-us", n=microseconds, data=payload(f"w{microseconds}", 2000))
                self.assertEqual(CODES["PORT_REPLACE_OK"], code, r.stderr)
                self.assertEqual(data, self.career().read_bytes())
                self.assertTrue(sleeps and self.WAITS[:len(sleeps)] == sleeps, f"the waits are 50, 100, 200 ...: {sleeps}")
        old = self.user_save()
        code, r, sleeps, _ = self.unlinked("target-unlinked-for-us", n=5_000_000)
        self.assertEqual(CODES["PORT_REPLACE_REFUSED"], code, r.stderr)
        self.assertEqual(old, self.career().read_bytes())
        self.assertEqual(self.WAITS, sleeps)
        self.assertEqual([], self.temporaries())

    def test_a_look_that_fails_after_the_name_showed_no_link_is_judged_by_its_error(self):
        """While the save waits the name can go away (the other save was deleted, or the career removed): that is a new file, and the
        save creates it. Any other failure of the look (an I/O error, a loop, no permission, a path that stopped being a folder) is
        not a missing name and refuses the save, whether it comes after the first no-link look or after the ninth."""
        for n in (1, self.LOOKS - 1):
            for error in ("eio", "eloop", "eacces", "enotdir", "enametoolong"):
                with self.subTest(after_looks=n, error=error):
                    old = self.user_save()
                    code, r, sleeps, _ = self.unlinked("target-unlinked-then-" + error, n=n)
                    self.assertEqual(CODES["PORT_REPLACE_REFUSED"], code, r.stderr)
                    self.assertEqual(old, self.career().read_bytes())
                    self.assertEqual([], self.temporaries())
                    self.assertEqual(self.WAITS[:n], sleeps)
        for n in (1, self.LOOKS - 1):
            with self.subTest(after_looks=n, error="enoent"):
                self.user_save()
                code, r, sleeps, data = self.unlinked("target-unlinked-then-enoent", n=n, data=payload(f"gone{n}", 2500))
                self.assertEqual(CODES["PORT_REPLACE_OK"], code, r.stderr)
                self.assertEqual(data, self.career().read_bytes())
                self.assertEqual(self.WAITS[:n], sleeps)

    def test_a_hard_linked_career_is_refused_at_once_without_waiting(self):
        self.user_save()
        os.link(self.career(), self.user / "other-name")
        before = self.tree(self.user)
        log = self.root / "sleeps.txt"
        code, _ = self.replace(r"savegames\Hawk.bes", payload("hard", 1000), env_extra={"SAVE_TEST_SLEEPS": str(log)})
        self.assertEqual(CODES["PORT_REPLACE_REFUSED"], code)
        self.assertEqual(before, self.tree(self.user))
        self.assertEqual([], self.sleeps(log), "only a name that shows no link is waited for")

    def test_an_empty_buffer_never_replaces_a_career(self):
        old = self.user_save()
        before = self.tree(self.user)
        code, r = self.replace(r"savegames\Hawk.bes", b"")
        self.assertEqual(CODES["PORT_REPLACE_REFUSED"], code, r.stderr)
        self.assertEqual(before, self.tree(self.user))
        self.assertEqual(old, self.career().read_bytes())

    def test_a_read_only_career_is_not_replaced(self):
        if os.geteuid() == 0:
            self.skipTest("root ignores file permissions")
        old = self.user_save()
        self.career().chmod(0o444)
        before = self.tree(self.user)
        code, r = self.replace(r"savegames\Hawk.bes", payload("ro", 1000))
        self.assertEqual(CODES["PORT_REPLACE_REFUSED"], code, r.stderr)
        self.assertEqual(before, self.tree(self.user))
        self.assertEqual(old, self.career().read_bytes())

    def test_a_folder_or_a_special_file_with_the_name_is_refused(self):
        folder = self.user / "SaveGames"
        folder.mkdir(parents=True)
        (folder / "Dir.bes").mkdir()
        os.mkfifo(folder / "Fifo.bes")
        before = self.tree(self.user)
        for name in ("Dir.bes", "Fifo.bes"):
            with self.subTest(name=name):
                code, _ = self.replace(r"savegames" "\\" + name, payload("odd", 1000))
                self.assertEqual(CODES["PORT_REPLACE_REFUSED"], code)
                self.assertEqual(before, self.tree(self.user))

    def test_two_careers_whose_names_differ_only_in_case_are_never_guessed_between(self):
        self.user_save("Hawk.bes")
        self.user_save("HAWK.BES")
        before = self.tree(self.user)
        code, _ = self.replace(r"savegames\hawk.bes", payload("ambiguous", 1000))
        self.assertEqual(CODES["PORT_REPLACE_REFUSED"], code)
        self.assertEqual(before, self.tree(self.user))

    def test_a_writable_folder_inside_the_game_folder_is_refused(self):
        inside = self.game / "ce-user"
        code, r = self.replace(r"savegames\Hawk.bes", payload("inside", 1000), user=inside)
        self.assertEqual(CODES["PORT_REPLACE_REFUSED"], code, r.stderr)
        self.assertFalse(inside.exists())

    def test_a_folder_that_cannot_be_written_fails_without_touching_the_career(self):
        if os.geteuid() == 0:
            self.skipTest("root ignores directory permissions")
        old = self.user_save()
        folder = self.user / "SaveGames"
        folder.chmod(0o555)
        self.addCleanup(lambda: folder.chmod(0o755))
        code, _ = self.replace(r"savegames\Hawk.bes", payload("ro", 1000))
        self.assertEqual(CODES["PORT_REPLACE_CREATE"], code)
        self.assertEqual(old, self.career().read_bytes())

    # --- atomicity as other processes see it --------------------------------------------------------------------------
    def test_a_reader_never_sees_a_short_empty_or_mixed_career(self):
        a, b = self.payloads / "A", self.payloads / "B"
        a.write_bytes(payload("A", 20_480))
        b.write_bytes(payload("B", 20_480 + 17))
        self.user_save()
        self.career().write_bytes(a.read_bytes())       # the readers' two valid answers are A and B, from the start
        original = self.payloads / "original"
        original.write_bytes((self.game / "SaveGames" / "Hawk.bes").read_bytes())
        results = {}

        def read(i, path):
            results[i] = self.run_driver("read-loop", path, a, b, 1500, original)

        # two readers open the file itself; two use the game's own two-root lookup
        readers = [threading.Thread(target=read, args=(i, str(self.career()) if i < 2 else r"savegames\Hawk.bes"))
                   for i in range(4)]
        for t in readers:
            t.start()
        writer = self.run_driver("write-loop", r"savegames\Hawk.bes", a, b, 300)
        for t in readers:
            t.join()
        self.assertEqual(0, writer.returncode, writer.stderr)
        for i in range(4):
            out = results[i].stdout.decode()
            self.assertEqual(0, results[i].returncode, out + results[i].stderr.decode())
            self.assertRegex(out, r"ok=[1-9]\d* bad=0 ")
            print(f"  reader {i} ({'host file' if i < 2 else 'game lookup'}): {out.strip()}")
            if i < 2:    # the file itself: every open succeeds and every read is complete
                self.assertRegex(out, r"missing=0 other=0")
        self.assertIn(self.career().read_bytes(), (a.read_bytes(), b.read_bytes()))
        self.assertEqual([], self.temporaries())

    def test_two_writers_racing_leave_one_complete_career(self):
        self.user_save()
        pairs = []
        for tag in ("W1", "W2"):
            a, b = self.payloads / (tag + "a"), self.payloads / (tag + "b")
            a.write_bytes(payload(tag + "a", 8000))
            b.write_bytes(payload(tag + "b", 8000))
            pairs.append((a, b))
        results = {}

        def write(i):
            results[i] = self.run_driver("write-loop", r"savegames\Hawk.bes", *pairs[i], 150)

        threads = [threading.Thread(target=write, args=(i,)) for i in range(2)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        for i in range(2):
            self.assertEqual(0, results[i].returncode, results[i].stdout + results[i].stderr)
        self.assertIn(self.career().read_bytes(), [p.read_bytes() for pair in pairs for p in pair])
        self.assertEqual([], self.temporaries())


class TemporaryNames(Fixture):
    """The temporary file is created exclusively and without following a link, and its name is drawn so that two writers never
    share one. The wrapper's injected EEXIST cannot tell a missing O_EXCL from a present one, so these plant real objects at
    the names a call is about to draw (the driver's `collide` command) and run two writers at the same moment (`race`)."""

    def planted_case(self, count, kind):
        old = self.user_save()
        code, pid, r, new = self.collide(count, kind)
        return old, new, code, pid, r

    def test_regular_files_at_the_names_it_would_draw_are_skipped_not_opened_or_removed(self):
        old, new, code, pid, r = self.planted_case(15, "file")      # the sixteenth draw is the last the call makes
        self.assertEqual((CODES["PORT_REPLACE_OK"], b""), (code, r.stderr))
        self.assertEqual(new, self.career().read_bytes())
        self.assertEqual([], self.damage_to_planted(pid, 15, "file"))

    def test_when_every_name_it_could_draw_is_a_file_it_fails_cleanly_and_removes_none_of_them(self):
        old, new, code, pid, r = self.planted_case(16, "file")
        self.assertEqual(CODES["PORT_REPLACE_CREATE"], code, r.stderr)
        self.assertIn("no temporary file (File exists)", r.stderr.decode())
        self.assertEqual(old, self.career().read_bytes())
        self.assertEqual([], self.damage_to_planted(pid, 16, "file"))

    def test_a_link_to_a_file_at_a_name_it_would_draw_is_skipped_and_never_written_through(self):
        old, new, code, pid, r = self.planted_case(2, "symlink-file")
        self.assertEqual((CODES["PORT_REPLACE_OK"], b""), (code, r.stderr))
        self.assertEqual(new, self.career().read_bytes())
        self.assertEqual([], self.damage_to_planted(pid, 2, "symlink-file"))

    def test_a_dangling_link_at_a_name_it_would_draw_is_skipped_and_nothing_is_created_through_it(self):
        old, new, code, pid, r = self.planted_case(2, "symlink-dangling")
        self.assertEqual((CODES["PORT_REPLACE_OK"], b""), (code, r.stderr))
        self.assertEqual(new, self.career().read_bytes())
        self.assertEqual([], self.damage_to_planted(pid, 2, "symlink-dangling"))

    def test_when_every_name_it_could_draw_is_a_link_it_fails_cleanly_and_touches_nothing(self):
        old, new, code, pid, r = self.planted_case(16, "symlink-file")
        self.assertEqual(CODES["PORT_REPLACE_CREATE"], code, r.stderr)
        self.assertEqual(old, self.career().read_bytes())
        self.assertEqual([], self.damage_to_planted(pid, 16, "symlink-file"))

    def test_two_writers_drawing_their_names_at_the_same_moment_never_meet_a_taken_name(self):
        counters, r = self.race()
        self.assertEqual({"rounds": 40, "failed": 0, "eexist": 0}, counters, r.stderr)
        self.assertEqual([], self.temporaries())
        self.assertEqual(payload("race-a", 6000), self.career("Hawk.bes").read_bytes())
        self.assertEqual(payload("race-b", 7000), self.career("Eagle.bes").read_bytes())


class FullDisk(Fixture):
    """A real ENOSPC: a 64 KiB tmpfs mounted in a private user and mount namespace (no privileges, nothing global)."""

    def test_a_full_disk_keeps_the_previous_career_and_frees_the_temporary_file(self):
        mnt = self.root / "mnt"
        mnt.mkdir()
        probe = subprocess.run(["unshare", "--user", "--map-root-user", "--mount", "sh", "-c",
                                f'mount -t tmpfs -o size=64k tmpfs "{mnt}"'], capture_output=True)
        if probe.returncode:
            self.skipTest("no unprivileged mount namespace here: " + probe.stderr.decode().strip())
        game = self.root / "tgame"
        (game / "savegames").mkdir(parents=True)
        old = payload("old", 4096)
        (self.payloads / "old").write_bytes(old)
        (self.payloads / "new").write_bytes(payload("new", 40_000))
        script = f"""set -e
mount -t tmpfs -o size=64k tmpfs "{mnt}"
mkdir -p "{mnt}/user/savegames"
cp "{self.payloads}/old" "{mnt}/user/savegames/Hawk.bes"
dd if=/dev/zero of="{mnt}/filler" bs=1k count=44 2>/dev/null
df -k "{mnt}" | tail -1 >&2
BEA_GAME_DIR="{game}" BEA_USER_DIR="{mnt}/user" "{self.driver}" replace 'savegames\\Hawk.bes' "{self.payloads}/new"
ls -a "{mnt}/user/savegames"
sha256sum "{mnt}/user/savegames/Hawk.bes"
"""
        r = subprocess.run(["unshare", "--user", "--map-root-user", "--mount", "sh", "-c", script], capture_output=True,
                           timeout=60)
        out, err = r.stdout.decode(), r.stderr.decode()
        self.assertEqual(0, r.returncode, err)
        self.assertEqual(str(CODES["PORT_REPLACE_WRITE"]), out.splitlines()[0], err)   # ENOSPC while writing
        self.assertIn("No space left on device", err)
        self.assertNotIn(".ce-write", out)                                                 # the partial temp is gone
        self.assertIn(f"{sha(old)}  ", out)                                                # and the old career is intact


class Mutants(Fixture):
    """The checks above must fail for a replace that is wrong in the ways that matter."""

    SOURCE = (deps.PORT / "platform/linux/save_replace.cpp").read_text()

    def mutant(self, name, old, new):
        self.assertEqual(1, self.SOURCE.count(old), f"mutation anchor for {name} changed")
        source = Path(self.scratch.name) / f"{name}.cpp"
        source.write_text(self.SOURCE.replace(old, new))
        binary = Path(self.scratch.name) / name
        if not binary.exists():
            compile_driver(deps.PORT / "tools/tests", binary, replace_source=source)
        return binary

    def attempt(self, binary, fault, data, n=None):
        saved, self.driver = self.driver, binary
        try:
            return self.replace(r"savegames\Hawk.bes", data, fault=fault, env_extra={} if n is None else {"SAVE_TEST_N": str(n)})
        finally:
            self.driver = saved

    def attempt_logged(self, binary, fault, data, n=None):
        """attempt() with the sleeps the mutant asked for: (status, sleeps)."""
        log = self.root / "mutant-sleeps.txt"
        log.unlink(missing_ok=True)
        saved, self.driver = self.driver, binary
        try:
            code, _ = self.replace(r"savegames\Hawk.bes", data, fault=fault,
                                   env_extra={"SAVE_TEST_SLEEPS": str(log), **({} if n is None else {"SAVE_TEST_N": str(n)})})
        finally:
            self.driver = saved
        return code, self.sleeps(log)

    def test_without_the_second_look_a_career_unlinked_during_the_check_is_refused(self):
        mutant = self.mutant("no-second-look", "look < LOOKS", "look < 1")
        self.user_save()
        code, _ = self.attempt(mutant, "target-unlinked-once", payload("x", 5000))
        self.assertEqual(CODES["PORT_REPLACE_REFUSED"], code)    # the real code replaces it (the test above), so it would fail the mutant

    def test_without_the_wait_a_rename_that_takes_a_while_is_refused(self):
        mutant = self.mutant("no-wait", "\t\tif (look + 1 < LOOKS)\n\t\t\tusleep(FIRST_WAIT_US << look);\n", "")
        for microseconds in (400, 6000):
            self.user_save()
            code, _ = self.attempt(mutant, "target-unlinked-for-us", payload("x", 5000), n=microseconds)
            self.assertEqual(CODES["PORT_REPLACE_REFUSED"], code, f"{microseconds} us")    # the real code waits it out

    def test_with_a_constant_wait_the_waits_do_not_double(self):
        mutant = self.mutant("constant-wait", "usleep(FIRST_WAIT_US << look)", "usleep(FIRST_WAIT_US)")
        self.user_save()
        code, sleeps = self.attempt_logged(mutant, "target-unlinked-always", payload("x", 5000))
        self.assertEqual([50] * (self.LOOKS - 1), sleeps)
        self.assertNotEqual(self.WAITS, sleeps)                              # the suite expects 50, 100, 200 ...

    def test_with_a_wait_after_the_last_look_the_refusal_comes_late(self):
        mutant = self.mutant("wait-after-last", "if (look + 1 < LOOKS)", "if (true)")
        self.user_save()
        code, sleeps = self.attempt_logged(mutant, "target-unlinked-always", payload("x", 5000))
        self.assertEqual(self.LOOKS, len(sleeps))
        self.assertNotEqual(self.WAITS, sleeps)

    def test_with_two_looks_a_name_that_shows_no_link_eight_times_is_refused(self):
        mutant = self.mutant("two-looks", "const int LOOKS = 10;", "const int LOOKS = 2;")
        self.user_save()
        code, _ = self.attempt(mutant, "target-unlinked-looks", payload("x", 5000), n=self.LOOKS - 1)
        self.assertEqual(CODES["PORT_REPLACE_REFUSED"], code)

    def test_with_a_hundred_looks_a_name_that_shows_no_link_ten_times_is_replaced(self):
        mutant = self.mutant("hundred-looks", "const int LOOKS = 10;", "const int LOOKS = 100;")
        self.user_save()
        code, _ = self.attempt(mutant, "target-unlinked-looks", payload("x", 5000), n=self.LOOKS)
        self.assertEqual(CODES["PORT_REPLACE_OK"], code)    # the real code refuses after ten looks

    def test_if_a_look_that_fails_were_a_new_file_the_career_would_be_replaced_whatever_the_error(self):
        mutant = self.mutant("failed-look-is-new", ": errno != ENOENT)", ": false)")
        self.user_save()
        code, _ = self.attempt(mutant, "target-unlinked-then-eio", payload("x", 5000), n=1)
        self.assertEqual(CODES["PORT_REPLACE_OK"], code)    # an I/O error on the name is no new file: the real code refuses

    def test_a_target_that_is_not_waiting_for_a_rename_must_not_make_the_save_sleep(self):
        mutant = self.mutant("wait-for-any-link-count", "if (!exists || target.st_nlink != 0)", "if (!exists || target.st_nlink == 1)")
        self.user_save()
        os.link(self.career(), self.user / "other-name")
        code, sleeps = self.attempt_logged(mutant, None, payload("x", 5000))
        self.assertEqual(CODES["PORT_REPLACE_REFUSED"], code)
        self.assertEqual(self.WAITS, sleeps)                # the mutant waits for a hard link; the real code refuses at once

    def test_without_the_read_back_a_corrupted_write_is_published(self):
        mutant = self.mutant("no-readback", "if (!step && !ReadsBack(fd, bytes, size))", "if (false)")
        self.user_save()
        code, _ = self.attempt(mutant, "readback-flip", payload("x", 5000))
        self.assertEqual(CODES["PORT_REPLACE_OK"], code)    # the suite expects VERIFY here, so it would fail the mutant
        code, _ = self.attempt(mutant, "write-dropped", payload("y", 5000))
        self.assertEqual(CODES["PORT_REPLACE_OK"], code)
        self.assertEqual(b"", self.career().read_bytes())   # an empty career published over the good one

    def test_without_the_identity_check_a_swapped_file_is_published(self):
        mutant = self.mutant("no-identity", "named.st_dev != mine.st_dev || named.st_ino != mine.st_ino ||\n\t\t\tnamed.st_nlink != 1",
                             "false")
        self.user_save()
        code, _ = self.attempt(mutant, "swap-temp", payload("x", 5000))
        self.assertEqual(CODES["PORT_REPLACE_OK"], code)
        self.assertEqual(b"SWAPPED", self.career().read_bytes())

    def test_a_cleanup_that_removes_by_name_deletes_the_file_another_process_swapped_in(self):
        mutant = self.mutant("unlink-any", "named.st_dev == mine.st_dev && named.st_ino == mine.st_ino &&\n\t\t\tunlinkat",
                             "unlinkat")
        self.user_save()
        code, _ = self.attempt(mutant, "swap-temp", payload("x", 5000))
        self.assertEqual(CODES["PORT_REPLACE_PUBLISH"], code)
        swapped = [p for p in (self.user / "SaveGames").glob(".ce-write-*") if p.read_bytes() == b"SWAPPED"]
        self.assertEqual([], swapped)                          # the suite expects this file to survive

    EXCL_FLAGS = "O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC"

    def test_without_o_excl_a_planted_file_is_opened_written_and_renamed_over_the_career(self):
        mutant = self.mutant("no-excl", self.EXCL_FLAGS, "O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC")
        self.user_save()
        code, pid, r, new = self.collide(1, "file", binary=mutant)
        self.assertEqual(CODES["PORT_REPLACE_OK"], code)            # it still reports success ...
        self.assertNotEqual([], self.damage_to_planted(pid, 1, "file"))   # ... having taken over a file that was not its own

    def test_without_o_excl_a_planted_link_stops_the_save_where_the_real_code_skips_it(self):
        mutant = self.mutant("no-excl", self.EXCL_FLAGS, "O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC")
        self.user_save()
        code, pid, r, new = self.collide(1, "symlink-file", binary=mutant)
        self.assertEqual(CODES["PORT_REPLACE_CREATE"], code)        # O_NOFOLLOW alone ends the loop with ELOOP

    def test_without_o_excl_and_o_nofollow_a_planted_link_is_written_through(self):
        mutant = self.mutant("no-excl-no-nofollow", self.EXCL_FLAGS, "O_RDWR | O_CREAT | O_CLOEXEC")
        self.user_save()
        code, pid, r, new = self.collide(1, "symlink-file", binary=mutant)
        self.assertEqual(new, (self.user / "outside-1").read_bytes())   # the file outside the folder was overwritten with the career

    def test_dropping_only_o_nofollow_changes_nothing_because_o_excl_already_refuses_a_link(self):
        """An equivalent mutant, shown rather than assumed: O_CREAT | O_EXCL fails with EEXIST on a link at the name whatever it
        points at (open(2)), so O_NOFOLLOW is belt and braces and no black-box check can tell the two apart."""
        mutant = self.mutant("no-nofollow", "O_EXCL | O_NOFOLLOW | O_CLOEXEC", "O_EXCL | O_CLOEXEC")
        self.user_save()
        code, pid, r, new = self.collide(2, "symlink-file", binary=mutant)
        self.assertEqual((CODES["PORT_REPLACE_OK"], b""), (code, r.stderr))
        self.assertEqual([], self.damage_to_planted(pid, 2, "symlink-file"))
        self.assertEqual(new, self.career().read_bytes())

    def test_a_name_that_never_advances_fails_on_the_first_taken_name_and_collides_between_writers(self):
        mutant = self.mutant("constant-name", "__sync_add_and_fetch(&sequence, 1)", "1u")
        self.user_save()
        code, pid, r, new = self.collide(1, "file", binary=mutant)
        self.assertEqual(CODES["PORT_REPLACE_CREATE"], code)        # the real code draws the next name and succeeds
        counters, r = self.race(binary=mutant)
        self.assertGreater(counters["failed"], 0, "the two writers should have fought over one name")
        self.assertGreater(counters["eexist"], 0)

    def test_a_counter_that_restarts_with_every_call_makes_concurrent_writers_meet_a_taken_name(self):
        mutant = self.mutant("counter-per-call", "static unsigned sequence;", "unsigned sequence = 0;")
        counters, r = self.race(binary=mutant)
        self.assertEqual(0, counters["failed"], r.stderr)           # the exclusive create still rescues both of them ...
        self.assertGreater(counters["eexist"], 0)                   # ... but only after meeting each other's name (real code: 0)

    def test_writing_in_place_loses_the_old_career_on_a_failure_and_a_crash(self):
        old = self.user_save()
        in_place = Path(self.scratch.name) / "in_place.cpp"
        in_place.write_text(r'''
#include "handles.h"
#include "common/save_replace.h"
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
int PortReplaceUserFile(const char *game_path, const void *data, unsigned size, const char *)
{
    char leaf[256];
    int parent = PortUserParent(game_path, leaf, sizeof(leaf), true);
    if (parent < 0) return PORT_REPLACE_REFUSED;
    int fd = openat(parent, leaf, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    for (unsigned done = 0; fd >= 0 && done < size;) {
        ssize_t put = write(fd, (const char *)data + done, size - done);
        if (put <= 0) { close(fd); close(parent); return PORT_REPLACE_WRITE; }
        done += put;
    }
    if (fd >= 0) close(fd);
    close(parent);
    return fd < 0 ? PORT_REPLACE_CREATE : PORT_REPLACE_OK;
}
''')
        binary = Path(self.scratch.name) / "in-place"
        compile_driver(deps.PORT / "tools/tests", binary, replace_source=in_place)
        code, _ = self.attempt(binary, "write-eio", payload("x", 5000))
        self.assertNotEqual(old, self.career().read_bytes(), "the in-place writer should have damaged the career")


if __name__ == "__main__":
    unittest.main(verbosity=2)
