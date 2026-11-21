#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""A program built with one of build.py's test switches is never packaged as a release.

build.py --commander-test and --play-unit-test add test machinery to a public build (an order tape, probes, a proof switch, god mode
for the Battle Engine, all driven by environment variables), and --mods-windows-test switches on the mod loader that releases keep off
on Windows. The build folder's flavour.txt said "public" for such a build as for a release, and package.py had nothing to tell them
apart. build.py now records the switch per target in flavour.txt (flavour_marker) and package.py refuses it unless --test is given.
The marker text in every case here is made by build.py's own function and read by package.py's own reader. No compiler, game files
or network required."""
import contextlib
import io
import re
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))
import build  # noqa: E402
import package  # noqa: E402

BUILD_SOURCE = (HERE.parent / "build.py").read_text()


class Reached(Exception):
    """The packager got past the flavour checks, to its first look at a program."""


class TestBuildMarker(unittest.TestCase):
    def setUp(self):
        self.assertTrue(hasattr(build, "flavour_marker") and hasattr(build, "test_build_switches"),
                        "build.py does not record a test switch in flavour.txt: a --commander-test or --play-unit-test build says 'public'")
        self.assertTrue(hasattr(package, "test_build_problem"), "package.py does not look for a test build in flavour.txt")

    def test_every_test_switch_build_py_offers_with_public_is_recorded(self):
        # the switches as the command line declares them: every "--...-test" option, less the one main() refuses together with --public
        offered = set(re.findall(r'ap\.add_argument\("--([a-z-]+-test)"', BUILD_SOURCE)) | \
            set(re.findall(r'ap\.add_argument\("--(test-[a-z-]+)"', BUILD_SOURCE))
        self.assertIn('raise SystemExit("--test-automation is for private network test builds; it cannot be combined with --public")', BUILD_SOURCE)
        offered.discard("test-automation")
        # --test-kit adds files, not a define (no production file is compiled differently); main() records it by name
        self.assertIn("test-kit", offered)
        offered.discard("test-kit")
        self.assertIn('switches = test_build_switches(CE_DEFINES) + (["test-kit"] if TEST_KIT else [])', BUILD_SOURCE)
        self.assertEqual(offered, {"commander-test", "play-unit-test", "mods-windows-test", "marker-test"})
        for switch in sorted(offered):
            with self.subTest(switch=switch):
                # the define main() adds for it, as written there
                found = re.search(r"if a\." + switch.replace("-", "_") + r':\n\s+CE_DEFINES\.append\("(-D[A-Z_]+)=1"\)', BUILD_SOURCE)
                self.assertIsNotNone(found, "main() adds one define for the switch")
                self.assertEqual(build.test_build_switches(["-DCE_NETWORK_OFF=1", found.group(1) + "=1"]), [switch])
        self.assertEqual(build.test_build_switches(["-DCE_NETWORK_OFF=1", "'-DCE_VERSION=\"0.1.14\"'", "-DBEA_PUBLIC_GRAPHICS_HOST=1"]), [])
        # and main() writes the marker from the build's own defines, before the build, and clears a target only after its build succeeded
        self.assertIn("switches = test_build_switches(CE_DEFINES)", BUILD_SOURCE)
        self.assertRegex(BUILD_SOURCE, r"write_flavour_marker\(\"public-network-ice\" if WITH_ICE else \"public-network\" if WITH_NETWORK else \"public\",\n"
                                       r"\s+mark=\(\[\"linux\", \"windows\"\] if a\.target == \"all\" else \[a\.target\]\) if switches else \(\), switches=switches\)")
        self.assertRegex(BUILD_SOURCE, r"if full_public and r\.returncode == 0 and not switches:\n\s+write_flavour_marker\(clear=\[target\]\)")

    def test_the_marker_follows_each_target_s_last_build(self):
        marker = build.flavour_marker
        self.assertEqual(marker("", "public"), b"public\n", "a release build's marker is the word alone, as before")
        both = marker("", "public", mark=["linux", "windows"], switches=["commander-test", "play-unit-test"])
        self.assertEqual(both, b"public\ntest-build linux: commander-test play-unit-test\ntest-build windows: commander-test play-unit-test\n")
        # a later build of one target without the switch: its line stays until that build has succeeded, the other target's stays after it
        started = marker(both.decode(), "public")
        self.assertEqual(started, both, "starting a build takes no test line off")
        linux_done = marker(started.decode(), "public", clear=["linux"])
        self.assertEqual(linux_done, b"public\ntest-build windows: commander-test play-unit-test\n")
        self.assertEqual(marker(linux_done.decode(), "public", clear=["windows"]), b"public\n")
        # a one-target test build beside a release program of the other target
        self.assertEqual(marker("public\n", "public", mark=["windows"], switches=["mods-windows-test"]), b"public\ntest-build windows: mods-windows-test\n")
        # the word can change; the test lines are about the programs, not the word
        self.assertEqual(marker("public\ntest-build linux: play-unit-test\n", "public-network"), b"public-network\ntest-build linux: play-unit-test\n")
        # a line it cannot read is kept, so the packager still sees it
        self.assertEqual(marker("public\ntest-build linux: PLAY UNIT\n", "public", clear=["linux"]), b"public\ntest-build linux: PLAY UNIT\n")

    def folder(self, text):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        if text is not None:
            (Path(folder.name) / "flavour.txt").write_bytes(text)
        return Path(folder.name)

    def test_the_packager_reads_what_the_build_writes(self):
        marker = build.flavour_marker
        self.assertEqual(package.build_test_switches(self.folder(None)), {})
        self.assertEqual(package.build_test_switches(self.folder(marker("", "public"))), {})
        self.assertIsNone(package.test_build_problem(self.folder(marker("", "public"))))
        self.assertIsNone(package.test_build_problem(self.folder(marker("", "public-network"))))
        one = self.folder(marker("", "public", mark=["linux"], switches=["play-unit-test"]))
        self.assertEqual(package.build_test_switches(one), {"linux": ["play-unit-test"]})
        self.assertEqual(package.build_flavour(one), "public", "the flavour word is still the first line")
        problem = package.test_build_problem(one)
        self.assertIn("the linux program was built with --play-unit-test", problem)
        self.assertIn("never packaged as a release", problem)
        self.assertIn("--test", problem)
        both = self.folder(marker("", "public", mark=["linux", "windows"], switches=["commander-test", "play-unit-test"]))
        problem = package.test_build_problem(both)
        self.assertIn("the linux program was built with --commander-test and --play-unit-test; the windows program was built with "
                      "--commander-test and --play-unit-test", problem)
        # a line that is neither is not passed over
        for text in (b"public\ntest-build linux: PLAY UNIT\n", b"public\nsomething else\n", b"public\ntest-build macos: play-unit-test\n"):
            with self.subTest(text=text), self.assertRaises(SystemExit) as stopped:
                package.build_flavour(self.folder(text))
            self.assertIn("unknown line", str(stopped.exception))

    def package(self, text, test):
        """package.py's own package() on a build folder with this marker, as far as its first look at a program."""
        args = SimpleNamespace(test=test, build=self.folder(text), out=None, version="0.0.0-test-build-check")
        with patch.object(package, "check_public", side_effect=Reached), contextlib.redirect_stdout(io.StringIO()):
            package.package(args)

    def test_package_refuses_a_test_build_and_takes_a_release_build(self):
        marker = build.flavour_marker
        release = marker("", "public")
        after_test = marker(marker("", "public", mark=["linux", "windows"], switches=["play-unit-test"]).decode(), "public")
        for target in ("linux", "windows"):
            after_test = marker(after_test.decode(), "public", clear=[target])
        for name, text in (("no marker", None), ("release", release), ("release after a test build of both", after_test)):
            with self.subTest(name=name), self.assertRaises(Reached):
                self.package(text, False)
        cases = (("--commander-test", marker("", "public", mark=["linux", "windows"], switches=["commander-test"])),
                 ("--play-unit-test", marker("", "public", mark=["linux"], switches=["play-unit-test"])),
                 ("--mods-windows-test", marker("", "public", mark=["windows"], switches=["mods-windows-test"])),
                 ("--marker-test", marker("", "public", mark=["linux"], switches=["marker-test"])),
                 # the release build of linux alone after a test build of both: the Windows program is still the test build
                 ("--play-unit-test", marker(marker("", "public", mark=["linux", "windows"], switches=["play-unit-test"]).decode(), "public",
                                             clear=["linux"])),
                 # a release build that was started and failed: the programs are still the test build's
                 ("--commander-test", marker(marker("", "public", mark=["linux", "windows"], switches=["commander-test"]).decode(), "public")))
        for switch, text in cases:
            with self.subTest(text=text):
                with self.assertRaises(SystemExit) as stopped:
                    self.package(text, False)
                self.assertIn("flavour.txt", str(stopped.exception))
                self.assertIn(switch, str(stopped.exception))
                self.assertIn("never packaged as a release", str(stopped.exception))
                with self.assertRaises(Reached):		# a local test package may hold it
                    self.package(text, True)


if __name__ == "__main__":
    unittest.main()
