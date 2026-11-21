#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""No weak function that has a definition resolves to nothing in the Windows program.

    python3 port/tools/windows_weak_link_tests.py [--exe PATH] [--objects DIR] [-v]

The port's code names some functions as weak (`__attribute__((weak))`) so that a file can ask whether a module is in the
build before it calls into it: the save fence asks the map editor and the museum visit that way, the world view asks the
commander and the editor, the platform layer asks the frame-feel log, and the default build asks the network, which it
does not contain. On Linux a weak definition is used like any other. In a Windows object both a weak reference and a weak
definition are weak externals with a default (`.weak.<name>.default...`), and the linker chooses; a reference whose
choice were "nothing" although a definition exists would silently switch its feature off (an open save fence, a
commander view with no picture), and a direct call would jump to address 0. Nothing on Linux can see that.

This suite reads the Windows program with the pinned llvm-nm (the dependency cache's llvm-mingw) and requires:

- every weak name whose definition is in the program (a default with an address) or in an object of the build folder
  (--objects, by default the program's own folder) to have that address, not 0;
- the names in REQUIRED to be there with an address (the functions the readings of release 0.1.14 named);
- the names that do resolve to nothing to be listed (they are printed; in the default build they are the network's).

The program is --exe, else BEA_WINDOWS_EXE, else the build's local-data/port/build/windows/bea.exe. Without one that test
is SKIPPED, NOT PASSED: this is a check of a built candidate, and the controls below do not stand in for it. A release
run names the candidate's program.

Controls (they need the cache's Windows compiler and no game): four authored files are compiled and linked in both orders
(the file that only refers to the weak name first, and the file that defines it first): with the pinned linker both give
the definition's address, which is what the judge must accept; the same files linked WITHOUT the defining object, judged
with that object in the build folder, must be refused (a definition exists and the program has nothing). Authored symbol
lists cover the rest of the judge. No Windows program is run here or anywhere on this machine by this suite.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

PORT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PORT))
import build  # noqa: E402

TOOLS = build.deps.DATA / "llvm-mingw/bin"
NM = TOOLS / "llvm-nm"
CXX = TOOLS / "i686-w64-mingw32-clang++"
EXE = None          # --exe
OBJECTS = None      # --objects

# The weak names a default CE program must hold with an address, and what each is asked for.
REQUIRED = {
    "_PortCeEditorActive": "the save fence asks it: no career write during a map editor session",
    "_PortCeMuseumVisitActive": "the save fence asks it: no career write during a museum visit",
    "_PortCeMuseumMusicDuck": "patch 0258 asks it: the museum cinema ducks hall music while a film plays",
    "_PortBinkHasSound": "the museum cinema asks it: duck music to silence under a film with audio, bed without",
    "_PortBinkPause": "the museum cinema calls it: hold film sound and clock while the game is paused",
    "_PortCeCmdWorldViewToken": "the world view asks it for the commander's picture",
    "_PortCeEditorWorldViewToken": "the world view asks it for the editor's picture; the editor calls it directly",
    "_PortFeelOn": "the platform layer asks it before every frame-feel call",
}

LINE = re.compile(r"^(?:.*?: )?([0-9a-fA-F]{8,16})?\s*([A-Za-z?-]) (\S+)\s*$")
WEAK = re.compile(r"^\.weak\.(.+?)\.default(?:\..*)?$")


def symbols(text):
    """[(name, type letter, value or None)] of an llvm-nm listing (one program or object, or several with -A)."""
    out = []
    for line in text.splitlines():
        m = LINE.match(line)
        if m:
            out.append((m[3], m[2], int(m[1], 16) if m[1] else None))
    return out


def nothing(kind, value):
    """An absolute zero (the default of a weak reference) or an undefined symbol."""
    return kind in ("U", "w", "v") or value is None or (kind in ("A", "a", "W") and value == 0)


def judge(program, objects=()):
    """(problems, the weak names that resolve to nothing, the weak names with an address) for a program's symbols and,
    optionally, the symbols of the build's objects."""
    final = {}
    for name, kind, value in program:
        if not name.startswith("."):
            final[name] = (kind, value)
    weak, defined = set(), {}
    for name, kind, value in program:
        m = WEAK.match(name)
        if m:
            weak.add(m[1])
            if not nothing(kind, value):
                defined.setdefault(m[1], "the program holds its definition")
    for name, kind, value in objects:
        m = WEAK.match(name)
        if m:
            weak.add(m[1])
            if kind not in ("A", "a", "U"):
                defined.setdefault(m[1], "an object of the build defines it (weak)")
        elif kind in "TtDdBbRrSs":
            defined.setdefault(name, "an object of the build defines it")
    problems, empty, held = [], [], []
    for name in sorted(weak):
        kind, value = final.get(name, ("U", None))
        if nothing(kind, value):
            empty.append(name)
            if name in defined:
                problems.append(f"{name} resolves to nothing although {defined[name]}")
        else:
            held.append(name)
    for name, why in REQUIRED.items():
        kind, value = final.get(name, ("U", None))
        if nothing(kind, value):
            problems.append(f"{name} has no address ({why})")
        elif name not in weak:
            problems.append(f"{name} is no longer a weak name in the program: take it out of REQUIRED if that is meant")
    return sorted(set(problems)), empty, held


def nm(*paths):
    done = subprocess.run([str(NM), "-A", *map(str, paths)], capture_output=True, text=True, errors="replace")
    if done.returncode:
        raise RuntimeError("llvm-nm failed: " + done.stderr[-500:])
    return symbols(done.stdout)


def object_symbols(folder):
    files = sorted(p for p in Path(folder).rglob("*.o") if p.is_file())
    out = []
    for at in range(0, len(files), 200):
        out += nm(*files[at:at + 200])
    return out, len(files)


AUTHORED = {
    "token.h": "int Token(void) __attribute__((weak));\nint Fence(void) __attribute__((weak));\nint Absent(void) __attribute__((weak));\n",
    # refers to all three; defines none
    "user.cpp": '#include "token.h"\nint Use() { return (Token ? Token() : 1) + (Fence ? Fence() : 2) + (Absent ? Absent() : 4); }\n',
    # defines Token after including the header that calls it weak: a weak definition
    "definer.cpp": '#include "token.h"\nint Other() { return 3; }\nint Token() { return 7; }\n',
    # defines Fence without the header: an ordinary definition
    "strong.cpp": "int Fence() { return 5; }\n",
    "main.cpp": "int Use();\nint main() { return Use(); }\n",
}


class JudgeTests(unittest.TestCase):
    """The judge on authored symbol lists (made-up names; REQUIRED is emptied for them)."""

    def setUp(self):
        self.required = dict(REQUIRED)
        REQUIRED.clear()

    def tearDown(self):
        REQUIRED.clear()
        REQUIRED.update(self.required)

    def test_a_weak_definition_in_the_program_must_be_the_names_address(self):
        listing = "00000000 A .weak._f.default._user\n00401000 T .weak._f.default._definer\n00401000 T _f\n"
        self.assertEqual(([], [], ["_f"]), judge(symbols(listing)))
        listing = "00000000 A .weak._f.default._user\n00401000 T .weak._f.default._definer\n00000000 A _f\n"
        self.assertEqual((["_f resolves to nothing although the program holds its definition"], ["_f"], []), judge(symbols(listing)))

    def test_a_name_nobody_defines_may_be_nothing_and_is_listed(self):
        listing = "00000000 A .weak._net.default._user\n00000000 A _net\n"
        self.assertEqual(([], ["_net"], []), judge(symbols(listing)))
        self.assertEqual(([], ["_net"], []), judge(symbols("00000000 A .weak._net.default._user\n")))

    def test_a_definition_in_an_object_that_the_program_lacks_is_refused(self):
        program = symbols("00000000 A .weak._f.default._user\n00000000 A _f\n")
        self.assertEqual(["_f resolves to nothing although an object of the build defines it"], judge(program, symbols("a.o: 00000000 T _f\n"))[0])
        self.assertEqual(["_f resolves to nothing although an object of the build defines it (weak)"],
                         judge(program, symbols("a.o: 00000000 T .weak._f.default._definer\na.o: 00000000 W _f\n"))[0])
        self.assertEqual([], judge(program, symbols("a.o: 00000000 A .weak._f.default._user\na.o: 00000000 W _f\nb.o:          U _f\n"))[0])

    def test_a_required_name_must_be_there_weak_and_with_an_address(self):
        REQUIRED["_fence"] = "the reason"
        held = "00000000 A .weak._fence.default._user\n00401000 T _fence\n"
        self.assertEqual([], judge(symbols(held))[0])
        self.assertEqual(["_fence has no address (the reason)"], judge(symbols("00000000 A .weak._fence.default._user\n00000000 A _fence\n"))[0])
        self.assertEqual(["_fence has no address (the reason)"], judge(symbols("00401000 T _other\n"))[0])
        self.assertEqual(["_fence is no longer a weak name in the program: take it out of REQUIRED if that is meant"], judge(symbols("00401000 T _fence\n"))[0])


class LinkTests(unittest.TestCase):
    """The pinned Windows compiler and linker on four authored files."""

    @classmethod
    def setUpClass(cls):
        if not CXX.is_file() or not NM.is_file():
            raise unittest.SkipTest(f"SKIPPED, NOT PASSED: no Windows toolchain at {TOOLS} (set BEA_PORT_DATA to the dependency cache)")
        cls.tmp = tempfile.TemporaryDirectory(prefix="windows-weak-link-")
        cls.folder = Path(cls.tmp.name)
        for name, text in AUTHORED.items():
            (cls.folder / name).write_text(text)
        for name in AUTHORED:
            if name.endswith(".cpp"):
                done = subprocess.run([str(CXX), "-O1", "-c", name, "-o", name[:-4] + ".o"], cwd=cls.folder, capture_output=True, text=True)
                if done.returncode:
                    raise AssertionError(f"{name} did not compile:\n{done.stderr[-1500:]}")

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def link(self, name, *objects):
        done = subprocess.run([str(CXX), *objects, "-o", name], cwd=self.folder, capture_output=True, text=True)
        self.assertEqual(0, done.returncode, done.stderr[-1500:])
        return nm(self.folder / name)

    def setUp(self):
        self.required = dict(REQUIRED)
        REQUIRED.clear()

    def tearDown(self):
        REQUIRED.clear()
        REQUIRED.update(self.required)

    def test_both_link_orders_give_the_weak_definitions_address(self):
        objects, count = object_symbols(self.folder)
        self.assertEqual(4, count)
        for label, order in (("the file that only refers comes first", ("main.o", "user.o", "definer.o", "strong.o")),
                             ("the file that defines comes first", ("main.o", "definer.o", "user.o", "strong.o"))):
            with self.subTest(label):
                program = self.link("ordered.exe", *order)
                problems, empty, held = judge(program, objects)
                self.assertEqual([], problems)
                self.assertEqual(["__Z6Absentv"], empty)
                self.assertEqual(["__Z5Fencev", "__Z5Tokenv"], held)
                values = {name: value for name, kind, value in program}
                self.assertTrue(values["__Z5Tokenv"] and values["__Z5Fencev"])

    def test_a_program_linked_without_the_defining_object_is_refused(self):
        objects, _ = object_symbols(self.folder)
        program = self.link("lacking.exe", "main.o", "user.o", "strong.o")
        problems, empty, held = judge(program, objects)
        self.assertEqual(["__Z5Tokenv resolves to nothing although an object of the build defines it (weak)"], problems)
        self.assertEqual(["__Z5Tokenv", "__Z6Absentv"], empty)
        # judged without the build folder, the same program only shows the name as nothing: the object scan is what refuses it
        self.assertEqual(([], ["__Z5Tokenv", "__Z6Absentv"], ["__Z5Fencev"]), judge(program))


class ProgramTests(unittest.TestCase):
    """The built Windows program."""

    def test_no_weak_function_with_a_definition_resolves_to_nothing(self):
        named = EXE or os.environ.get("BEA_WINDOWS_EXE")
        exe = Path(named) if named else build.BUILD / "windows/bea.exe"
        if not exe.is_file():
            self.skipTest(f"SKIPPED, NOT PASSED: no Windows program at {exe} (build with --target windows or all, or pass --exe)")
        if not NM.is_file():
            self.skipTest(f"SKIPPED, NOT PASSED: no llvm-nm at {NM}")
        program = nm(exe)
        self.assertGreater(len(program), 1000, "the program has no symbols to read: the check needs the unstripped build")
        folder = Path(OBJECTS) if OBJECTS else exe.parent
        objects, count = object_symbols(folder)
        problems, empty, held = judge(program, objects)
        print(f"\n{exe}: {len(held)} weak names with an address, {len(empty)} that resolve to nothing; {count} objects read under {folder}")
        for name in empty:
            print(f"  nothing: {name}")
        if not count:
            print("  NO OBJECTS READ: only definitions that are in the program itself were looked for")
        self.assertEqual([], problems)
        self.assertGreaterEqual(len(held), len(REQUIRED))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", help="the Windows program to read (default: this checkout's local-data/port/build/windows/bea.exe)")
    parser.add_argument("--objects", help="the folder whose objects were linked into it (default: the program's folder)")
    parser.add_argument("-v", action="store_true")
    args = parser.parse_args()
    EXE, OBJECTS = args.exe, args.objects
    unittest.main(verbosity=2 if args.v else 1, argv=sys.argv[:1])
