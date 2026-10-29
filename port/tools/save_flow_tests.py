#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Linux career overwrite flow, before and after patches 0117 and 0118, over the real card and file layer.

    python3 port/tools/save_flow_tests.py [-v]

The save page's own functions (Process, CreateSave, the overwrite question) and the PC memory card (PCMemoryCard.cpp) are
cut out of the repository's sources with every port patch applied (the "after" build) or with only the patches before 0117
(the "before" build, which is the source of the published 0.1.9 launcher) and compiled as the game compiles them. They run
over the real Linux path layer, file layer and atomic replace on authored folders: a game folder (never written) and a
writable folder. Only what surrounds them is authored: the front end, the message box, the career's bytes and a text table
that names each string, so a run prints what the player would have seen. No game data, real save or display is used.
Needs the pinned LLVM compiler and dxvk-native headers from deps.py and the host's 32-bit development libraries.
"""
import hashlib
import os
from pathlib import Path
import re
import resource
import signal
import subprocess
import tempfile
import unittest

import deps

ROOT = deps.PORT.parent
PATCHES = deps.PORT / "patches"
TESTS = deps.PORT / "tools/tests"
LLVM = deps.DATA / "llvm/bin/clang++"
FIRST_NEW_PATCH = "0117"
PLATFORM = ("files.cpp", "paths.cpp", "save_replace.cpp", "save_overwrite.cpp", "save_cp1252.cpp", "crt.cpp")
# build.py GAME_CRT: game objects use LIBCMT's stream table (a second fclose returns EOF; ReadSave closes twice)
GAME_CRT = {"fopen": "bea_crt_fopen", "port_fopen": "bea_crt_port_fopen", "fclose": "bea_crt_fclose"}
WRAPS = ",".join("--wrap=" + n for n in ("write", "pread", "fsync", "close", "openat", "renameat", "fstatat"))
INCLUDES = [f"-I{deps.PORT / 'include'}", f"-I{deps.PORT / 'include/linux'}", f"-I{deps.DATA / 'dxvk-native/usr/include/dxvk'}"]
# build.py NO_WIDE_LIBCALLS: LLVM must not turn a 16-bit wide-string loop into a call to glibc's 32-bit wcslen and friends.
NO_WIDE_LIBCALLS = ["-fno-builtin-" + f for f in ("wcslen", "wcscmp", "wcsncmp", "wcschr", "wcscpy", "wcsncpy", "wcscat",
                                                  "wmemcmp", "wmemchr", "wmemcpy", "wmemmove", "wmemset")]
GAME_FLAGS = ["-std=gnu++14", "-fms-compatibility", "-fms-compatibility-version=19.20", "-D__GNUC__=4", "-D__GNUC_MINOR__=2",
              "-D__GNUC_PATCHLEVEL__=1", "-D__GNUC_STDC_INLINE__=1", "-fms-extensions", "-fno-autolink", "-fno-sized-deallocation",
              "-fno-strict-aliasing", "-fwrapv", "-malign-double", "-mms-bitfields", "-fshort-wchar", "-g", "-O1", "-w",
              "-mno-sse", "-mfpmath=387", *NO_WIDE_LIBCALLS, "-include", str(deps.PORT / "include/port.h")]
PLATFORM_FLAGS = ["-std=gnu++17", "-DNOMINMAX", "-fms-extensions", "-fno-autolink", "-fno-strict-aliasing", "-fwrapv",
                  "-mms-bitfields", "-fshort-wchar", "-g", "-O1", "-w", "-ffunction-sections", "-fdata-sections", *NO_WIDE_LIBCALLS]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def sections(patch):
    return {m.group(1): m.group(0) for m in re.finditer(
        r"(?ms)^--- a/([^\n]+)\n\+\+\+ b/[^\n]+\n.*?(?=^--- a/|\Z)", patch)}


def patched(out, name, after, exclude=()):
    """src/name with the port patches applied in name order: all of them (after) or those before 0117 (before), less any
    patch whose number is in exclude."""
    (out / name).write_bytes((ROOT / "src" / name).read_bytes())
    for patch in sorted(PATCHES.glob("*.patch")):
        if (not after and patch.name[:4] >= FIRST_NEW_PATCH) or patch.name[:4] in exclude:
            continue
        part = sections(patch.read_text(encoding="latin1")).get(name)
        if part:
            (out / "section.patch").write_text(part, encoding="latin1")
            r = subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", "section.patch"], cwd=out,
                               capture_output=True, text=True, timeout=10)
            assert r.returncode == 0 and "fuzz" not in r.stdout.lower(), patch.name + r.stdout + r.stderr
    return (out / name).read_text(encoding="latin1")


def function(source, pattern):
    found = re.findall(pattern + r"[^\n]*\n\{\n.*?^\}", source, re.M | re.S)
    assert len(found) == 1, f"expected one {pattern!r}, found {len(found)}"
    return found[0]


def constants_and_names(text):
    names = sorted(set(re.findall(r"\b(?:FEPS|FEMB|FEX|FEP|FONT|BUTTON|FES)_[A-Z0-9_]+\b", text)) |
                   {"FEMB_YES", "FEMB_NO", "FEMB_Q_OVERWRITE", "FEPS_ACTIVE", "FEP_MAIN", "FEP_VIRTUAL_KEYBOARD"})
    lines = ["enum { " + ", ".join(f"{n} = {100 + i}" for i, n in enumerate(names)) + ", FEP_SUCCESS_PAGE = 9999 };",
             "static const char *NameOf(int id) {", "    switch (id) {"]
    lines += [f'    case {100 + i}: return "{n}";' for i, n in enumerate(names)]
    lines += ['    }', '    static char b[32]; snprintf(b, sizeof b, "STR%d", id - 1000); return b;', "}"]
    return "\n".join(lines) + "\n"


class Build:
    """One variant (before or after): the real card object and the driver, linked with the Linux platform layer."""

    def __init__(self, parent, after, exclude=(), label=None):
        self.after = after
        self.dir = Path(tempfile.mkdtemp(prefix=(label or ("after" if after else "before")) + "-", dir=parent))
        d = self.dir
        (d / "stubs").mkdir()
        (d / "src").mkdir()
        for name in ("MemoryCard.h", "PCMemoryCard.h"):
            (d / "src" / name).write_bytes((ROOT / "src" / name).read_bytes())
        (d / "stubs/common.h").write_text('#pragma once\n#include <windows.h>\n#include <string.h>\n#include <stdio.h>\n'
                                          '#define PC 1\n#define XBOX 2\n#define PS2 3\n#define TARGET PC\n')
        for name in ("Platform.h", "platform.h"):
            (d / "stubs" / name).write_text('#pragma once\nWCHAR *ToWCHAR(char *text);\nchar *FromWCHAR(WCHAR *text);\n')
        for name in ("memorycard.h",):
            (d / "stubs" / name).write_text('#pragma once\n#include "MemoryCard.h"\n')
        (d / "stubs/mathsconst.h").write_text("#pragma once\n")
        self.card = patched(d, "PCMemoryCard.cpp", after, exclude)
        self.page = patched(d, "FEPSaveGame.cpp", after, exclude)
        self.options = patched(d, "FEPOptions.cpp", after, exclude)
        umtexture = (ROOT / "src/UMTexture.cpp").read_text(encoding="latin1")
        start = umtexture.index("static char\t\tsFromWCHAR")
        end = umtexture.index("return sFromWCHAR[sFromWCHARNext];", start) + len("return sFromWCHAR[sFromWCHARNext];")
        (d / "converters.inc").write_text(umtexture[start:end] + "\n}\n")
        # the page: its declarations, the question, and the five functions the test exercises
        include = re.search(r'#include\s+"cliparams\.h"[^\n]*\n', self.page, re.I)
        preamble = self.page[include.end():self.page.index("char\t\t\tgSaveFromOptions")]
        parts = [preamble, function(self.page, r"^static void ask_if_you_want_to_overwrite\(\)"),
                 *[function(self.page, rf"^void\s+CFEPSaveGame::{n}\(") for n in
                   ("ButtonPressed", "Process", "RemovedMUWhinge", "AskIfYouWantToDelete", "CreateSave")]]
        ui = "\n".join(parts)
        (d / "ui.inc").write_text(ui)
        (d / "ui_constants.inc").write_text(constants_and_names(ui))
        write_options = function(self.options, r"^void\s+CFEPOptions::WriteDefaultOptionsFile\(")
        declaration = re.search(r'^extern "C" int\s+PortReplaceUserFile[^\n]*\n', self.options, re.M)
        (d / "options.inc").write_text((declaration.group(0) if declaration else "") + write_options + "\n")
        self.binary = d / "flow"
        self.compile()

    def run_cmd(self, cmd, name):
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=240)
        (self.dir / (name + ".log")).write_text(r.stdout + r.stderr)
        if r.returncode:
            raise RuntimeError(f"{name} failed:\n{(r.stdout + r.stderr)[-3000:]}")

    def compile(self):
        d = self.dir
        base = [str(LLVM), "--target=i686-pc-linux-gnu", "-m32"]
        card_inc = [f"-I{d / 'stubs'}", f"-I{d / 'src'}", *INCLUDES]
        objects = []
        card = d / "PCMemoryCard.cpp"
        self.run_cmd([*base, *GAME_FLAGS, *card_inc, "-c", str(card), "-o", str(d / "card.o")], "card")
        objects.append(d / "card.o")
        self.run_cmd([*base, *GAME_FLAGS, *card_inc, f"-DUI_SOURCE=\"{d / 'ui.inc'}\"", f"-DOPTIONS_SOURCE=\"{d / 'options.inc'}\"",
                      f"-DCONVERTER_SOURCE=\"{d / 'converters.inc'}\"", f"-I{deps.PORT / 'platform/linux'}", f"-I{d}",
                      "-c", str(TESTS / "save_flow_driver.cpp"), "-o", str(d / "driver.o")], "driver")
        objects.append(d / "driver.o")
        syms = d / "crt.syms"
        syms.write_text("".join(f"{a} {b}\n" for a, b in GAME_CRT.items()))
        for obj in objects:
            self.run_cmd([str(deps.DATA / "llvm/bin/llvm-objcopy"), f"--redefine-syms={syms}", str(obj)], "objcopy")
        for source in (*[deps.PORT / "platform/linux" / n for n in PLATFORM], deps.PORT / "platform/common/crtfile.cpp",
                       TESTS / "save_replace_faults.cpp"):
            obj = d / (source.stem + ".o")
            self.run_cmd([*base, *PLATFORM_FLAGS, f"-I{deps.PORT / 'platform/linux'}", f"-I{deps.PORT / 'platform'}", *INCLUDES,
                          "-c", str(source), "-o", str(obj)], source.stem)
            objects.append(obj)
        self.run_cmd([*base, "-m32", *map(str, objects), "-Wl,--gc-sections", f"-Wl,{WRAPS}", "-o", str(self.binary)], "link")


class Flow(unittest.TestCase):
    before = after = only0117 = only0118 = None

    @classmethod
    def setUpClass(cls):
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        parent = deps.WORK / "tests"
        parent.mkdir(parents=True, exist_ok=True)
        cls.scratch = tempfile.TemporaryDirectory(prefix="save-flow-", dir=parent)
        cls.addClassCleanup(cls.scratch.cleanup)
        cls.before = Build(Path(cls.scratch.name), False)
        cls.after = Build(Path(cls.scratch.name), True)
        cls.only0117 = Build(Path(cls.scratch.name), True, exclude=("0118",), label="only0117")
        cls.only0118 = Build(Path(cls.scratch.name), True, exclude=("0117",), label="only0118")

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="save-flow-case-", dir=deps.WORK / "tests")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.game, self.user = self.root / "game", self.root / "user"
        (self.game / "savegames").mkdir(parents=True)
        self.new = self.root / "new.bin"
        self.new.write_bytes(self.payload("NEW CAREER", 4000))

    @staticmethod
    def payload(tag, size):
        block = hashlib.sha256(tag.encode()).digest()
        return (tag.encode() + b"|" + block * (size // len(block) + 1))[:size]

    def game_save(self, name="slot.bes", tag="ORIGINAL IN THE GAME FOLDER", size=2500):
        data = self.payload(tag, size)
        (self.game / "savegames" / name).write_bytes(data)
        self.game_snapshot = self.tree(self.game)
        self.addCleanup(lambda: self.assertEqual(self.game_snapshot, self.tree(self.game), "the game folder changed"))
        return data

    def user_save(self, name="slot.bes", tag="NEWER COPY IN THE WRITABLE FOLDER", size=3000):
        data = self.payload(tag, size)
        (self.user / "savegames").mkdir(parents=True, exist_ok=True)
        (self.user / "savegames" / name).write_bytes(data)
        return data

    @staticmethod
    def tree(root):
        out = {}
        for p in sorted(root.rglob("*")):
            st = p.lstat()
            out[str(p.relative_to(root))] = ("dir",) if p.is_dir() else (sha(p.read_bytes()), st.st_mtime_ns, st.st_ino)
        return out

    def drive(self, build, *args, fault=None, fsize=None):
        env = dict(os.environ, BEA_GAME_DIR=str(self.game), BEA_USER_DIR=str(self.user), HOME=str(self.root))
        env.pop("SAVE_TEST_FAULT", None)
        if fault:
            env["SAVE_TEST_FAULT"] = fault

        def limits():
            if fsize is not None:
                signal.signal(signal.SIGXFSZ, signal.SIG_IGN)       # the write fails with EFBIG instead of killing us
                resource.setrlimit(resource.RLIMIT_FSIZE, (fsize, fsize))
        argv = [a if isinstance(a, bytes) else str(a).encode() for a in args]
        return subprocess.run([str(build.binary), *argv], env=env, capture_output=True, timeout=60, cwd=self.root,
                              preexec_fn=limits)

    def page(self, build, answers, name="slot", autosave="not", **kw):
        r = self.drive(build, "save-page", name, answers or "-", autosave, self.new, **kw)
        out = r.stdout.decode()
        events = [l[6:] for l in out.splitlines() if l.startswith("EVENT ")]
        counts = dict(kv.split("=") for l in out.splitlines() if l.startswith("COUNT ") for kv in l[6:].split())
        return events, {k: int(v) for k, v in counts.items()}, r

    def career(self, name="slot.bes"):
        return self.user / "savegames" / name

    def questions(self, events):
        return events.count("question:overwrite")

    def message_names(self, events):
        return [e[8:] for e in events if e.startswith("message:")]


class TheReportedFailure(Flow):
    """A game-folder original with the same name as a newer writable copy: published 0.1.9 against this branch."""

    COULD_NOT_DELETE = "STR118"          # Localization_GetStringById(0x76), the message the second YES ends in

    def test_before_the_first_yes_deletes_the_newer_copy_and_a_second_prompt_follows_and_nothing_is_written(self):
        original = self.game_save()
        self.user_save()
        events, counts, r = self.page(self.before, "YY")
        self.assertEqual(2, self.questions(events), events)                      # the prompt returns after one YES
        self.assertEqual(2, counts["delete"])                                    # the second YES tries to delete the original
        self.assertEqual(0, counts["write"], events)                             # and nothing was ever written
        self.assertIn(self.COULD_NOT_DELETE, self.message_names(events))
        self.assertNotIn("page:success", events)
        self.assertFalse(self.career().exists(), "the writable copy was deleted")
        self.assertEqual(original, (self.game / "savegames" / "slot.bes").read_bytes())   # and the older original now shows

    def test_after_one_prompt_replaces_the_writable_copy_and_the_original_is_never_touched(self):
        self.game_save()
        self.user_save()
        events, counts, r = self.page(self.after, "YY")
        self.assertEqual(1, self.questions(events), events)
        self.assertEqual((0, 1), (counts["delete"], counts["write"]), events)
        self.assertNotIn(self.COULD_NOT_DELETE, self.message_names(events))
        self.assertIn("page:success", events)
        self.assertEqual(self.new.read_bytes(), self.career().read_bytes())
        self.assertEqual([], [p.name for p in (self.user / "savegames").glob(".ce-write-*")])

    def test_before_a_career_that_exists_only_in_the_game_folder_cannot_be_overwritten(self):
        self.game_save()
        events, counts, _ = self.page(self.before, "YY")
        self.assertEqual((0,), (counts["write"],), events)
        self.assertIn(self.COULD_NOT_DELETE, self.message_names(events))
        self.assertFalse(self.user.exists() and any(self.user.rglob("*.bes")))

    def test_after_a_career_that_exists_only_in_the_game_folder_gets_a_writable_copy(self):
        self.game_save()
        events, counts, _ = self.page(self.after, "Y")
        self.assertEqual(1, self.questions(events), events)
        self.assertEqual((0, 1), (counts["delete"], counts["write"]))
        self.assertIn("page:success", events)
        self.assertEqual(self.new.read_bytes(), self.career().read_bytes())

    def test_a_career_that_exists_only_in_the_writable_folder_is_replaced_with_one_prompt(self):
        self.user_save()
        for label, build in (("before", self.before), ("after", self.after)):
            with self.subTest(label):
                self.career().write_bytes(self.payload("OLD " + label, 3000))
                events, counts, _ = self.page(build, "Y")
                self.assertEqual(1, self.questions(events), events)
                self.assertIn("page:success", events)
                self.assertEqual(self.new.read_bytes(), self.career().read_bytes())
                self.assertEqual(0 if label == "after" else 1, counts["delete"])

    def test_answering_no_changes_nothing_and_the_next_save_asks_again(self):
        self.game_save()
        old = self.user_save()
        before = self.tree(self.user)
        events, counts, _ = self.page(self.after, "N")
        self.assertEqual((1, 0, 0), (self.questions(events), counts["write"], counts["delete"]), events)
        self.assertEqual(before, self.tree(self.user))
        events, _, _ = self.page(self.after, "N")           # a new run is a new session: no approval survives
        self.assertEqual(1, self.questions(events), events)
        self.assertEqual(old, self.career().read_bytes())

    def test_the_name_may_differ_in_case_from_the_file_and_the_file_keeps_its_spelling(self):
        self.user_save("slot.bes")
        events, counts, _ = self.page(self.after, "Y", name="SLOT")
        self.assertEqual((1, 0, 1), (self.questions(events), counts["delete"], counts["write"]), events)
        self.assertEqual(["slot.bes"], sorted(p.name for p in (self.user / "savegames").iterdir()))
        self.assertEqual(self.new.read_bytes(), self.career().read_bytes())

    def test_a_career_named_with_a_code_page_1252_character_is_replaced_under_its_utf8_name(self):
        name = b"lat\xea"                                  # the game's byte 0xEA, as the latete cheat code spells it
        utf8 = "lat\u00ea.bes"
        self.user_save(utf8)
        events, counts, _ = self.page(self.after, "Y", name=name)
        self.assertEqual((1, 0, 1), (self.questions(events), counts["delete"], counts["write"]), events)
        self.assertEqual([utf8], sorted(p.name for p in (self.user / "savegames").iterdir()))
        self.assertEqual(self.new.read_bytes(), self.career(utf8).read_bytes())

    def test_a_new_name_is_saved_without_any_question(self):
        events, counts, _ = self.page(self.after, "", name="brand new")
        self.assertEqual((0, 1, 0), (self.questions(events), counts["write"], counts["delete"]), events)
        self.assertEqual(self.new.read_bytes(), self.career("brand new.bes").read_bytes())
        self.assertIn("page:success", events)

    def test_an_autosave_replaces_the_career_without_a_question_and_without_deleting(self):
        old = self.user_save()
        events, counts, _ = self.page(self.after, "", autosave="normal")
        self.assertEqual((0, 1, 0), (self.questions(events), counts["write"], counts["delete"]), events)
        self.assertEqual(self.new.read_bytes(), self.career().read_bytes())
        self.assertNotEqual(old, self.career().read_bytes())


class FailuresDuringTheOverwrite(Flow):
    # target-unlinked-once is the failure two processes saving the same career at the same moment can cause (PARITY.md, career writes)
    FAULTS = ("write-eio", "enospc", "fsync-eio", "readback-flip", "write-dropped", "rename-eio", "close-eio", "temp-eacces",
              "target-unlinked-once")

    def test_every_failure_shows_the_save_failed_message_and_keeps_the_previous_copy(self):
        self.game_save()
        old = self.user_save()
        before = self.tree(self.user)
        for fault in self.FAULTS:
            with self.subTest(fault=fault):
                events, counts, r = self.page(self.after, "Y", fault=fault)
                self.assertEqual(before, self.tree(self.user), events)
                self.assertEqual(old, self.career().read_bytes())
                self.assertIn("message:FEX_SAVEERROR", events)
                self.assertNotIn("page:success", events)
                self.assertNotIn("autosave-armed", events)
                self.assertEqual(1, self.questions(events))
                self.assertEqual(0, counts["delete"])

    def test_a_real_write_limit_loses_the_career_before_and_not_after(self):
        """The same kernel limit (RLIMIT_FSIZE, 512 bytes) on the autosave path, which truncates in place before the patches."""
        self.user_save(size=300)
        old = self.career().read_bytes()
        events, counts, r = self.page(self.before, "", autosave="normal", fsize=512)
        self.assertNotEqual(old, self.career().read_bytes(), "the published launcher keeps the career: " + str(events))
        self.assertLessEqual(self.career().stat().st_size, 512)
        self.career().write_bytes(old)
        events, counts, r = self.page(self.after, "", autosave="normal", fsize=512)
        self.assertEqual(old, self.career().read_bytes(), events)
        self.assertIn("message:FEX_SAVEERROR", events)
        self.assertNotIn("page:success", events)
        self.assertEqual([], [p.name for p in (self.user / "savegames").glob(".ce-write-*")])


class CardOperations(Flow):
    """The card's other operations that touch a career: admission without truncation, delete, read."""

    def test_admitting_an_existing_career_for_overwrite_no_longer_truncates_it(self):
        old = self.user_save()
        for label, build in (("before", self.before), ("after", self.after)):
            with self.subTest(label):
                self.career().write_bytes(old)
                r = self.drive(build, "card", "create", "slot", "1")
                self.assertIn("create=0", r.stdout.decode())
                kept = self.career().read_bytes() == old
                self.assertEqual(label == "after", kept, f"{label}: the card cut the existing career short at admission" )

    def test_admission_creates_one_empty_file_only_for_a_name_nobody_has_and_never_over_a_new_one(self):
        r = self.drive(self.after, "card", "create", "fresh", "0")
        self.assertIn("create=0", r.stdout.decode())
        self.assertEqual(0, self.career("fresh.bes").stat().st_size)
        r = self.drive(self.after, "card", "create", "fresh", "0")
        self.assertIn("create=6", r.stdout.decode())          # MCE_FILEEXISTS
        self.career("fresh.bes").write_bytes(b"made meanwhile")
        r = self.drive(self.after, "card", "create", "fresh", "1")
        self.assertEqual(b"made meanwhile", self.career("fresh.bes").read_bytes())

    def test_deleting_a_career_that_has_a_game_folder_original_removes_only_the_writable_copy(self):
        original = self.game_save()
        self.user_save()
        r = self.drive(self.after, "card", "delete", "slot")
        self.assertIn("delete=0", r.stdout.decode())
        self.assertFalse(self.career().exists())
        listing = self.drive(self.after, "card", "list", "x").stdout.decode().splitlines()
        self.assertEqual(["saves=1", "name=slot"], listing)           # the original is listed again, under the same name
        r = self.drive(self.after, "card", "read", "slot", len(original))
        self.assertIn("read=0", r.stdout.decode())
        self.assertEqual(original, r.stderr)

    def test_deleting_a_career_that_exists_only_in_the_game_folder_fails_and_changes_nothing(self):
        self.game_save()
        r = self.drive(self.after, "card", "delete", "slot")
        self.assertIn("delete=1", r.stdout.decode())
        self.assertFalse(self.user.exists() and any(self.user.rglob("*.bes")))
        self.assertEqual(["saves=1", "name=slot"], self.drive(self.after, "card", "list", "x").stdout.decode().splitlines())


class EachPatchIsNeeded(Flow):
    """The same scenarios with only one of the two patches: neither is enough alone."""

    def test_the_atomic_write_alone_keeps_the_double_prompt_and_the_lost_copy(self):
        self.game_save()
        self.user_save()
        events, counts, _ = self.page(self.only0117, "YY")
        self.assertEqual((2, 0), (self.questions(events), counts["write"]), events)
        self.assertFalse(self.career().exists())

    def test_the_approval_alone_fixes_the_prompt_but_a_failed_write_still_loses_the_career(self):
        self.game_save()
        self.user_save()
        events, counts, _ = self.page(self.only0118, "YY")
        self.assertEqual((1, 0, 1), (self.questions(events), counts["delete"], counts["write"]), events)
        self.assertEqual(self.new.read_bytes(), self.career().read_bytes())
        old = self.payload("OLD", 300)
        self.career().write_bytes(old)
        self.page(self.only0118, "", autosave="normal", fsize=512)
        self.assertNotEqual(old, self.career().read_bytes())


class OtherCallers(Flow):
    """The callers that write a career or the options file without asking the player anything."""

    def test_the_pause_menu_and_language_change_write_keeps_the_old_career_when_the_write_fails(self):
        old = self.user_save()
        for fault in ("write-eio", "fsync-eio", "rename-eio", "readback-flip"):
            with self.subTest(fault=fault):
                r = self.drive(self.after, "resume-like", "slot", self.new, fault=fault)
                self.assertEqual(b"create=0 write=1\n", r.stdout)
                self.assertEqual(old, self.career().read_bytes())
        r = self.drive(self.after, "resume-like", "slot", self.new)
        self.assertEqual(b"create=0 write=0\n", r.stdout)
        self.assertEqual(self.new.read_bytes(), self.career().read_bytes())

    def test_the_pause_menu_write_loses_the_career_under_a_real_write_limit_before_and_not_after(self):
        old = self.user_save(size=300)
        self.career().write_bytes(old)
        self.drive(self.before, "resume-like", "slot", self.new, fsize=512)
        self.assertNotEqual(old, self.career().read_bytes())
        self.career().write_bytes(old)
        self.drive(self.after, "resume-like", "slot", self.new, fsize=512)
        self.assertEqual(old, self.career().read_bytes())

    def test_the_options_file_is_replaced_whole_and_a_failure_keeps_the_old_one_and_is_reported(self):
        options = self.user / "defaultoptions.bea"
        self.user.mkdir(parents=True)
        old = self.payload("OLD OPTIONS", 300)
        for label, build, fsize in (("before", self.before, 512), ("after", self.after, 512)):
            with self.subTest(label):
                options.write_bytes(old)
                r = self.drive(build, "options", self.new, fsize=fsize)
                events = [l for l in r.stdout.decode().splitlines() if l.startswith("EVENT")]
                if label == "before":
                    self.assertNotEqual(old, options.read_bytes())          # cut short, and not one word to the player
                    self.assertEqual([], events)
                else:
                    self.assertEqual(old, options.read_bytes())
                    self.assertEqual(["EVENT setup-history:Couldn't write defaultoptions"], events)
        options.write_bytes(old)
        r = self.drive(self.after, "options", self.new)
        self.assertEqual((self.new.read_bytes(), b""), (options.read_bytes(), r.stdout.replace(b"EVENT", b"").strip()))
        for fault in ("write-eio", "fsync-eio", "rename-eio", "temp-eacces"):
            with self.subTest(fault=fault):
                options.write_bytes(old)
                r = self.drive(self.after, "options", self.new, fault=fault)
                self.assertEqual(old, options.read_bytes())
                self.assertIn(b"Couldn't write defaultoptions", r.stdout)

    def test_the_options_file_in_the_game_folder_is_never_touched(self):
        (self.game / "defaultoptions.bea").write_bytes(self.payload("GAME OPTIONS", 300))
        self.game_snapshot = self.tree(self.game)
        self.addCleanup(lambda: self.assertEqual(self.game_snapshot, self.tree(self.game)))
        self.drive(self.after, "options", self.new)
        self.assertEqual(self.new.read_bytes(), (self.user / "defaultoptions.bea").read_bytes())


class Approvals(Flow):
    """The one-shot approval an overwrite YES gives its retry (platform/linux/save_overwrite.cpp)."""

    def run_script(self, script, build=None):
        r = self.drive(build or self.after, "approvals", script)
        return r.stdout.decode().split()

    def test_one_retry_for_one_name_case_insensitively(self):
        self.assertEqual(["A=0", "C=1", "C=0"], self.run_script("A:Hawk:hawk C:HAWK C:HAWK"))

    def test_a_different_existing_name_is_not_approved(self):
        self.assertEqual(["A=1", "C=0"], self.run_script("A:Hawk:Eagle C:Hawk"))

    def test_asking_for_another_name_spends_the_approval(self):
        self.assertEqual(["A=0", "C=0", "C=0"], self.run_script("A:Hawk:Hawk C:Eagle C:Hawk"))

    def test_clearing_and_replacing(self):
        self.assertEqual(["A=0", "X", "C=0"], self.run_script("A:Hawk:Hawk X C:Hawk"))
        self.assertEqual(["A=0", "A=0", "C=0", "C=0"], self.run_script("A:Hawk:Hawk A:Eagle:Eagle C:Hawk C:Eagle"))

    def test_names_are_limited_to_what_the_keyboard_holds(self):
        longest, too_long = "x" * 31, "x" * 32
        self.assertEqual(["A=0", "C=1"], self.run_script(f"A:{longest}:{longest} C:{longest}"))
        self.assertEqual(["A=1", "C=0"], self.run_script(f"A:{too_long}:{too_long} C:{too_long}"))

    def test_nothing_is_approved_by_default(self):
        self.assertEqual(["C=0"], self.run_script("C:Hawk"))


class SourceTrace(unittest.TestCase):
    """Every write of a career or of the options file goes through the atomic replace, and no new one slips in."""

    @classmethod
    def setUpClass(cls):
        cls.dir = tempfile.TemporaryDirectory(prefix="save-trace-", dir=deps.WORK / "tests")
        cls.addClassCleanup(cls.dir.cleanup)
        cls.patched = {n: patched(Path(cls.dir.name), n, True) for n in
                       ("PCMemoryCard.cpp", "FEPOptions.cpp", "FEPSaveGame.cpp", "PauseMenu.cpp", "FEPMain.cpp", "Platform.cpp",
                        "FEPLoadGame.cpp", "FEPDirectory.cpp")}

    def test_no_career_or_options_path_opens_a_file_for_truncating_write(self):
        opens = {}
        for name, text in self.patched.items():
            opens[name] = re.findall(r'fopen\s*\([^;]*"(?:wb?|ab?|r\+b?|w\+b?)"', text)
        self.assertEqual({n: [] for n in self.patched}, opens)

    def test_the_card_creates_only_exclusively_and_replaces_through_the_atomic_call(self):
        card = re.sub(r"//[^\n]*", "", self.patched["PCMemoryCard.cpp"])
        self.assertEqual(1, card.count('fopen(path, "wbx")'))
        self.assertEqual(1, len(re.findall(r'PortReplaceUserFile\(path, data, datasize, "career"\)', card)))
        self.assertNotIn('"wb"', card)

    def test_the_options_file_goes_through_the_atomic_call(self):
        text = self.patched["FEPOptions.cpp"]
        self.assertEqual(1, text.count('PortReplaceUserFile("defaultoptions.bea", data, size, "options")'))

    def test_the_other_callers_write_only_through_those_two_functions(self):
        expected = {
            "PauseMenu.cpp": ("CPauseMenu::Resume", 1, 1),      # CreateSave(.., TRUE) + WriteSave, then WriteDefaultOptionsFile
            "FEPMain.cpp": ("CFEPMain::Process", 1, 1),         # the language change
            "Platform.cpp": ("CPlatform::Flip", 0, 1),          # a display reset that saves the options
            "FEPLoadGame.cpp": (None, 0, 1),                    # the options follow a load
        }
        for name, (owner, saves, options) in expected.items():
            with self.subTest(name):
                text = self.patched[name]
                self.assertEqual(saves, len(re.findall(r"MEMORYCARD\.WriteSave\(", text)))
                self.assertEqual(saves, len(re.findall(r"MEMORYCARD\.CreateSave\(.*?, TRUE\)", text)))
                self.assertEqual(options, len(re.findall(r"WriteDefaultOptionsFile\(", text)))
                self.assertEqual(0, len(re.findall(r"MEMORYCARD\.DeleteSave\(", text)))
        # SaveDefaultOptions asks the save page for the career (autosave) and writes the options file
        options = self.patched["FEPOptions.cpp"]
        self.assertIn("WriteDefaultOptionsFile(buffer, size)", options)
        self.assertEqual(0, len(re.findall(r"MEMORYCARD\.", options)))

    def live(self, name, windows=False, after=True):
        """The file as the compiler sees it on Linux (or Windows): conditionals resolved, includes empty."""
        d = Path(self.dir.name) / ("live-" + ("after" if after else "before"))
        d.mkdir(exist_ok=True)
        text = self.patched[name] if after else patched(d, name, False)
        (d / name).write_text(text, encoding="latin1")
        for include in re.findall(r'#include\s*[<"]([^>"]+)[>"]', text):
            (d / include).parent.mkdir(parents=True, exist_ok=True)
            (d / include).write_text("")
        r = subprocess.run([str(LLVM), "-E", "-P", "-x", "c++", "-nostdinc", f"-I{d}", "-DPC=1", "-DXBOX=2", "-DPS2=3", "-DTARGET=PC",
                            *(["-D_WIN32=1"] if windows else []), str(d / name)], capture_output=True, text=True, timeout=30)
        self.assertEqual(0, r.returncode, r.stderr)
        return r.stdout

    def test_the_only_deletes_are_the_directory_screen_and_the_partial_save_cleanup(self):
        for windows in (False, True):
            with self.subTest(windows=windows):
                sites = {name: len(re.findall(r"MEMORYCARD\.DeleteSave\(", self.live(name, windows))) for name in self.patched}
                self.assertEqual({"FEPDirectory.cpp": 1, "FEPSaveGame.cpp": 2}, {k: v for k, v in sites.items() if v})
                # the two in the save page are the card-full cleanups (CreateSave), never the overwrite YES (Process)
                page = self.live("FEPSaveGame.cpp", windows)
                self.assertNotIn("DeleteSave", function(page, r"^void\s+CFEPSaveGame::Process\("))
                self.assertEqual(2, len(re.findall(r"DeleteSave\(", function(page, r"^void\s+CFEPSaveGame::CreateSave\("))))

    def test_the_save_page_compiles_to_the_same_lines_on_windows_as_before_patch_0118(self):
        """0118 only widens the guards 0066 put around the approval, so a Windows build compiles the identical page."""
        self.assertEqual(self.live("FEPSaveGame.cpp", windows=True, after=False), self.live("FEPSaveGame.cpp", windows=True))

    def test_the_card_changes_on_windows_only_in_the_whole_file_write(self):
        import difflib
        before = [l.strip() for l in self.live("PCMemoryCard.cpp", windows=True, after=False).splitlines() if l.strip()]
        after = [l.strip() for l in self.live("PCMemoryCard.cpp", windows=True).splitlines() if l.strip()]
        changes = [l for l in difflib.unified_diff(before, after, lineterm="", n=0) if l[:1] in "+-" and l[:3] not in ("+++", "---")]
        self.assertEqual([
            '+extern "C" int PortReplaceUserFile(const char *game_path, const void *data, unsigned size, const char *what);',
            '-FILE *f = fopen(path, "wb");', '-if (f)', '-{', '-if (fwrite(data, datasize, 1, f) == 1)', '-{',
            '-fclose(f);', '-return(MCE_SUCCESS);', '-}', '-}', '-return 1;',
            '+return PortReplaceUserFile(path, data, datasize, "career") == 0 ? MCE_SUCCESS : 1;'],
            changes)

    def test_the_check_would_catch_the_old_delete_in_the_overwrite_yes(self):
        before = self.live("FEPSaveGame.cpp", windows=False, after=False)
        self.assertIn("DeleteSave", function(before, r"^void\s+CFEPSaveGame::Process\("))   # the published Linux flow


if __name__ == "__main__":
    unittest.main(verbosity=2)
