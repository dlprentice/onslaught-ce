#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The museum's build graph and the boundary between its host-buffer helpers and its game-rule adapter.

ce_museum_visit.cpp (the cache provider) and ce_museum_visit_generate.cpp (the room writer) own standard-library
buffers, so they are compiled as port code (pcxx): the game rule renames operator new to the game's private allocator,
which these translation units do not define. ce_museum_visit_engine.cpp reads game objects and keeps the game rule
(qcxx). Controls here:

  - the build graph: rule per unit, in every public flavour, on both targets; the writer's quoted-include route to the
    game's oids.h; no change to the allocator rename map; mutated graphs must be reported;
  - the generated rules: the host rule has no symbol renames, the game rule keeps them, neither switches the calling
    convention;
  - the boundary: ce_museum_visit.h's layout and callback assertions hold under both rules on both targets, and layout,
    callback and calling-convention mutants of the header each fail their own assertion;
  - oids.h: the writer finds it under the host rule (and not without its route), and both rules read the same class
    identifiers from it.

The compiler checks are syntax or IR only, with the pinned toolchain (BEA_PORT_DATA); no game data, game run or network.
An unavailable toolchain fails the compiler controls rather than skipping them.
"""
from contextlib import ExitStack
from pathlib import Path
import hashlib
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import build

HOST_UNITS = ("ce_museum_visit.cpp", "ce_museum_visit_generate.cpp")
GAME_UNITS = ("ce_museum_visit_engine.cpp", "ce_museum.cpp")
HEADER = build.PORT / "game/ce_museum_visit.h"
CALLING_CONVENTION_FLAGS = ("-mrtd", "-mregparm", "-fdefault-calling-conv")


def graph_problems(rows, mirror):
    """What is wrong with port_sources rows [(source, rule, extra)] for the museum's units."""
    problems = []
    found = {Path(source).name: (rule, extra) for source, rule, extra in rows if Path(source).parent.name == "game"}
    for name in HOST_UNITS:
        if name not in found:
            problems.append(f"{name}: not built")
        elif found[name][0] != "pcxx":
            problems.append(f"{name}: compiled as {found[name][0]}, want pcxx (host allocator)")
    for name in GAME_UNITS:
        if name not in found:
            problems.append(f"{name}: not built")
        elif found[name][0] != "qcxx":
            problems.append(f"{name}: compiled as {found[name][0]}, want qcxx (game flags and allocator renames)")
    if found.get("ce_museum_visit.cpp", ("", "x"))[1]:
        problems.append("ce_museum_visit.cpp: takes no extra flags")
    tokens = found.get("ce_museum_visit_generate.cpp", ("", ""))[1].split()
    if tokens != [f"-iquote{mirror}"]:
        problems.append(f"ce_museum_visit_generate.cpp: its oids.h route must be exactly -iquote on the mirror, got {tokens}")
    return problems


def rule_command(text, name):
    return re.search(r"^rule %s\n  command = (.*)$" % name, text, re.M).group(1)


def variable(text, name):
    return re.search(r"^%s = (.*)$" % name, text, re.M).group(1)


class Fixture(unittest.TestCase):
    """build.py's public configuration with a scratch build folder and a mirror that holds only the tracked oids.h."""

    def setUp(self):
        scratch = build.ROOT / "local-data/ce-museum-visit-build-tests"
        scratch.mkdir(parents=True, exist_ok=True)
        temporary = tempfile.TemporaryDirectory(dir=scratch)
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.mirror = self.root / "mirror/src"
        self.mirror.mkdir(parents=True)
        shutil.copy2(build.ROOT / "src/oids.h", self.mirror / "oids.h")
        mono = self.root / "src/monocypher-4.0.2/src"
        mono.mkdir(parents=True)
        (mono / "monocypher.c").write_text("/* authored fixture */\n")
        stack = ExitStack()
        self.addCleanup(stack.close)
        for key, value in {"PUBLIC": True, "WITH_NETWORK": True, "WITH_ICE": False, "WITH_NATIVE_SIGNALING": False,
                           "WITH_TEXT_CHAT": False, "BUILD": self.root / "build", "MIRROR": self.mirror,
                           "CE_DEFINES": ['-DCE_VERSION="museum-test"']}.items():
            stack.enter_context(patch.object(build, key, value))
        stack.enter_context(patch.object(build.deps, "SRC", self.root / "src"))
        stack.enter_context(patch.object(build, "link_flags", return_value=""))

    def ninja(self, target):
        return build.ninja_file(target, [], False, True).read_text()


class BuildGraphTests(Fixture):
    def test_host_buffer_set_is_exactly_the_reviewed_one(self):
        self.assertEqual(frozenset(("ce_network_boot_authority.cpp", "ce_network_invitation.cpp", "ce_project.cpp",
                                    "ce_museum_visit.cpp", "ce_museum_visit_generate.cpp")),
                         build.HOST_BUFFER_GAME_MODULES)

    def test_helpers_are_host_modules_and_adapter_keeps_the_game_rule_in_every_flavour(self):
        for network in (True, False):
            build.WITH_NETWORK = network
            for target in ("linux", "windows"):
                with self.subTest(target=target, network=network):
                    self.assertEqual([], graph_problems(build.port_sources(target), self.mirror))

    def test_control_reports_a_helper_that_lost_the_host_rule(self):
        rows = build.port_sources("linux")
        for name in HOST_UNITS:
            with self.subTest(name=name):
                mutated = [(s, "qcxx" if Path(s).name == name else r, e) for s, r, e in rows]
                self.assertTrue(any(name in p and "want pcxx" in p for p in graph_problems(mutated, self.mirror)))

    def test_control_reports_an_adapter_that_lost_the_game_rule(self):
        rows = build.port_sources("windows")
        for name in GAME_UNITS:
            with self.subTest(name=name):
                mutated = [(s, "pcxx" if Path(s).name == name else r, e) for s, r, e in rows]
                self.assertTrue(any(name in p and "want qcxx" in p for p in graph_problems(mutated, self.mirror)))

    def test_control_reports_a_missing_unit_and_a_broken_oids_route(self):
        rows = build.port_sources("linux")
        self.assertTrue(any("not built" in p for p in graph_problems(
            [r for r in rows if Path(r[0]).name != "ce_museum_visit_engine.cpp"], self.mirror)))
        for label, extra in (("no route", ""), ("a broad -I route", f"-I{self.mirror}"),
                             ("another folder", f"-iquote{self.root}"), ("an extra flag", f"-iquote{self.mirror} -DX=1")):
            with self.subTest(label):
                mutated = [(s, r, extra if Path(s).name == "ce_museum_visit_generate.cpp" else e) for s, r, e in rows]
                self.assertTrue(any("oids.h route" in p for p in graph_problems(mutated, self.mirror)))

    def test_allocator_rename_map_is_unchanged(self):
        # The fix is a rule choice for two units, not a change to allocation: no definition of the game's private names is added.
        self.assertEqual({"_Znwj": "bea_game_new", "_Znaj": "bea_game_new_array", "_ZdlPv": "bea_game_delete",
                          "_ZdaPv": "bea_game_delete_array"}, build.GAME_ALLOCATOR)
        defining = [p.relative_to(build.ROOT).as_posix() for p in (build.ROOT / "port").rglob("*")
                    if p.suffix in (".c", ".cpp", ".h", ".hpp", ".inl") and "bea_game_" in p.read_text(errors="replace")
                    and "tools" not in p.relative_to(build.ROOT / "port").parts[:1]]
        self.assertEqual([], defining)

    def test_generated_edges_and_rules(self):
        for target in ("linux", "windows"):
            with self.subTest(target=target):
                text = self.ninja(target)
                edges = {}
                for line in text.splitlines():
                    found = re.match(r"build \S+/port/game/(ce_museum\w*\.cpp)\.o: (\w+) ", line)
                    if found:
                        edges[found.group(1)] = found.group(2)
                self.assertEqual({"ce_museum.cpp": "qcxx", "ce_museum_visit.cpp": "pcxx",
                                  "ce_museum_visit_engine.cpp": "qcxx", "ce_museum_visit_generate.cpp": "pcxx"}, edges)
                host, game = rule_command(text, "pcxx"), rule_command(text, "qcxx")
                self.assertNotIn("--redefine-syms", host)
                self.assertNotIn("-include ", host)
                self.assertIn("--redefine-syms", game)
                self.assertIn("-include ", game)
                for flag in CALLING_CONVENTION_FLAGS:
                    self.assertNotIn(flag, host)
                    self.assertNotIn(flag, game)
                syms = (self.root / "build" / target / "game-allocator.syms").read_text().splitlines()
                prefix = "_" if target == "windows" else ""
                self.assertIn(f"{prefix}_Znwj {prefix}bea_game_new", syms)
                self.assertIn(f"{prefix}_ZdlPv {prefix}bea_game_delete", syms)
                generator = next(l for l in text.splitlines() if "ce_museum_visit_generate.cpp.o:" in l)
                self.assertIn(f"\n  extra = -iquote{self.mirror}", text[text.index(generator):].split("\nbuild ")[0])

    def test_host_rule_names_no_game_include_folder(self):
        for target in ("linux", "windows"):
            with self.subTest(target=target):
                host = rule_command(self.ninja(target), "pcxx")
                self.assertNotIn(str(self.mirror), host)
                self.assertNotIn("MissionScript", host)


class BoundaryCompileTests(Fixture):
    def setUp(self):
        super().setUp()
        self.work = self.root / "work"
        self.work.mkdir()
        self.texts = {target: self.ninja(target) for target in ("linux", "windows")}
        for target, text in self.texts.items():
            compiler = shlex.split(variable(text, "cxx"))[0]
            if not Path(compiler).is_file():
                self.fail(f"{target}: the pinned compiler {compiler} is missing; set BEA_PORT_DATA to a populated dependency cache")

    def host(self, target, source, action, extra=""):
        command = rule_command(self.texts[target], "pcxx").replace("$cxx", variable(self.texts[target], "cxx")).replace("$extra", extra)
        command = command.replace("-MD -MF $out.d -c -o $out $in", action + " " + str(source))
        self.assertNotIn("$", command)
        return shlex.split(command)

    def game(self, target, source, action):
        # The game rule's first stage: the front end with every game flag, before the QIfist and allocator-rename stages.
        stage = rule_command(self.texts[target], "qcxx").split(" && ")[0]
        stage = stage.replace("$cxx", variable(self.texts[target], "cxx")).replace("$extra", "")
        marker = "-MD -MF $out.d -S -emit-llvm -Xclang -disable-llvm-passes -o $out.ll $in"
        self.assertIn(marker, stage)
        command = stage.replace(marker, action + " " + str(source))
        self.assertNotIn("$", command)
        return shlex.split(command)

    def run_command(self, command):
        return subprocess.run(["nice", "-n", "10", *command], capture_output=True, text=True, cwd=build.ROOT, timeout=300)

    def stage_header(self, folder):
        """A copy of the real header in a new folder, beside a one-line translation unit that includes it; returns both."""
        folder.mkdir()
        header = folder / "ce_museum_visit.h"
        shutil.copy2(HEADER, header)
        unit = folder / "probe.cpp"
        unit.write_text('#include "ce_museum_visit.h"\n')
        return header, unit

    def test_helpers_compile_under_the_host_rule_and_the_writer_needs_its_oids_route(self):
        for target in ("linux", "windows"):
            extras = {Path(s).name: e for s, r, e in build.port_sources(target)}
            for name in HOST_UNITS:
                with self.subTest(target=target, name=name):
                    done = self.run_command(self.host(target, build.PORT / "game" / name, "-fsyntax-only", extras[name]))
                    self.assertEqual(0, done.returncode, f"{target} {name}: {len(done.stderr.splitlines())} diagnostic lines")
            with self.subTest(target=target, control="the writer without its oids.h route"):
                done = self.run_command(self.host(target, build.PORT / "game/ce_museum_visit_generate.cpp", "-fsyntax-only"))
                self.assertNotEqual(0, done.returncode)
                self.assertIn("'oids.h' file not found", done.stderr)

    def test_boundary_assertions_hold_under_both_rules_and_each_mutant_trips_its_own(self):
        copy_digest = hashlib.sha256(HEADER.read_bytes()).hexdigest()
        mutants = {
            "layout": ("RoomPlan layout changed", "    int baseLevel;\n", "    int baseLevel;\n    int inserted;\n"),
            "exhibit": ("ExhibitPlacement layout changed", "    float orientation[3];\n};", "    float orientation[3];\n    float pad;\n};"),
            "prefix": ("CarrierPrefix layout changed", "    int parentWorld;\n", "    int parentWorld;\n    int spare;\n"),
            "receiver": ("RoomReceiver changed", "typedef bool (*RoomReceiver)(const void *base, unsigned baseLength,",
                         "typedef bool (*RoomReceiver)(const void *base, unsigned long baseLength,"),
            "reader": ("SourceReader changed", "typedef bool (*SourceReader)(void *bytes, unsigned length, void *context);",
                       "typedef bool (*SourceReader)(void *bytes, unsigned length, void *context, int extra);"),
            "convention": ("RoomReceiver changed", "typedef bool (*RoomReceiver)(const void *base,",
                           "typedef bool (__attribute__((stdcall)) *RoomReceiver)(const void *base,"),
        }
        for target in ("linux", "windows"):
            for rule in ("host", "game"):
                make = self.host if rule == "host" else self.game
                header, unit = self.stage_header(self.work / f"{target}-{rule}-clean")
                self.assertEqual(copy_digest, hashlib.sha256(header.read_bytes()).hexdigest())
                with self.subTest(target=target, rule=rule, control="the unmodified header"):
                    done = self.run_command(make(target, unit, "-fsyntax-only"))
                    self.assertEqual(0, done.returncode, f"{target} {rule}: {len(done.stderr.splitlines())} diagnostic lines")
                for label, (message, old, new) in mutants.items():
                    header, unit = self.stage_header(self.work / f"{target}-{rule}-{label}")
                    text = header.read_text()
                    self.assertEqual(1, text.count(old), label)
                    header.write_text(text.replace(old, new))
                    with self.subTest(target=target, rule=rule, mutant=label):
                        done = self.run_command(make(target, unit, "-fsyntax-only"))
                        self.assertNotEqual(0, done.returncode)
                        self.assertIn(message, done.stderr)

    def identifiers(self, make, target, folder, *extra):
        folder.mkdir()
        tu = folder / "probe.cpp"
        tu.write_text('#include "oids.h"\n'
                      'extern "C" const unsigned int ce_museum_probe_oids[2] = {(unsigned int)OID_CStart, (unsigned int)OID_CFeature};\n')
        ir = folder / "probe.ll"
        done = self.run_command(make(target, tu, f"-S -emit-llvm -o {ir}", *extra))
        self.assertEqual(0, done.returncode, f"{target}: {len(done.stderr.splitlines())} diagnostic lines")
        found = re.search(r"@\"?_?ce_museum_probe_oids\"?\s*=[^\[\n]*\[2 x i32\] \[i32 (-?\d+), i32 (-?\d+)\]", ir.read_text())
        self.assertIsNotNone(found)
        return tuple(int(n) for n in found.groups())

    def test_both_rules_read_the_same_class_identifiers_from_oids_h(self):
        for target in ("linux", "windows"):
            with self.subTest(target=target):
                extra = {Path(s).name: e for s, r, e in build.port_sources(target)}["ce_museum_visit_generate.cpp"]
                host = self.identifiers(self.host, target, self.work / f"oids-{target}-host", extra)
                game = self.identifiers(self.game, target, self.work / f"oids-{target}-game")
                self.assertEqual(host, game)
                self.assertNotEqual(host[0], host[1])


if __name__ == "__main__":
    unittest.main()
