#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Refuse ICE packages until their licence and notice handling has been reviewed.

Uses authored symbol listings and candidate files only; no game data or network.
Set BEA_PACKAGE_BINARY_FIXTURES=1 to additionally link tiny i686 ELF and PE fixtures
with the cached toolchains. These fixtures are inspected, never executed.
"""
import contextlib
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import package  # noqa: E402


class IcePackageTests(unittest.TestCase):
    def setUp(self):
        owner = package.ROOT / "local-data/port/package-ice-tests"
        owner.mkdir(parents=True, exist_ok=True)
        self.scratch = tempfile.TemporaryDirectory(prefix="case-", dir=owner)
        self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name)
        self.build = self.root / "build"
        self.work = self.root / "work"
        mirror = self.work / "mirror/public-unreplaced.txt"
        mirror.parent.mkdir(parents=True)
        mirror.write_text("")
        for windows in (False, True):
            binary = self.binary(windows)
            binary.parent.mkdir(parents=True)
            binary.write_bytes(b"authored candidate\n")
        self.symbols = {False: "1 T main\n1 T enet_host_create\n", True: "1 T _main\n1 T _enet_host_create\n"}
        self.nm_calls = []
        self.stack = contextlib.ExitStack()
        self.addCleanup(self.stack.close)
        for callback in (
            patch.object(package, "WORK", self.work),
            patch.object(package, "NETWORK", True),
            patch.object(package, "ALLOW_UNREPLACED", False),
            patch.object(package.deps, "require_ffmpeg_shared"),
            patch.object(package.deps, "ffmpeg_shared_files", return_value=[Path("authored-codec.dll")]),
            patch.object(package.deps, "require_sdl_receipt", side_effect=AssertionError("ICE candidate escaped rejection")),
            patch.object(package.deps, "require_sdl_link_receipt", side_effect=AssertionError("ICE candidate escaped rejection")),
            patch.object(package.subprocess, "run", side_effect=self.run_tool),
        ):
            self.stack.enter_context(callback)

    def binary(self, windows):
        return self.build / ("windows/bea.exe" if windows else "linux/bea")

    def run_tool(self, args, **kwargs):
        tool = Path(args[0]).name
        if tool == "llvm-nm":
            windows = Path(args[-1]).suffix == ".exe"
            self.nm_calls.append(windows)
            text = self.symbols[windows]
        elif tool == "readelf":
            text = "(RUNPATH) Library runpath: [$ORIGIN/lib]\n(NEEDED) Shared library: [authored-codec.dll]\n"
        elif tool == "llvm-readobj":
            text = "  Name: authored-codec.dll\n"
        else:
            raise AssertionError(f"unexpected tool: {tool}")
        return subprocess.CompletedProcess(args, 0, text, "")

    def test_libjuice_and_ce_ice_symbols_are_recognised(self):
        for symbol in (
            "juice_create", "_juice_destroy", "_juice_send@12", "juice_gather_candidates",
            "__imp_juice_get_state", "__imp__juice_create@4", "juice_future_api",
            "CE_NetCreateIce", "_CE_NetCreateIce", "CeIceBridge::Create()",
            "ce_net::CeIceBridge::Send(void const*, unsigned int)", "ce_network_ice_feature_marker",
        ):
            with self.subTest(symbol=symbol):
                self.assertTrue(package.links_ice("1 T " + symbol + "\n"))
        for symbol in ("main", "fruit_juice_create", "_fruit_juice_create", "juice", "juicebox", "OtherCeIceBridge::Create()"):
            with self.subTest(symbol=symbol):
                self.assertFalse(package.links_ice("1 T " + symbol + "\n"))

    def test_libjuice_is_rejected_for_either_platform_even_when_enet_matches(self):
        for windows in (False, True):
            with self.subTest(windows=windows):
                original = self.symbols[windows]
                self.symbols[windows] += "1 T " + ("_juice_create" if windows else "juice_create") + "\n"
                with self.assertRaisesRegex(SystemExit, "ICE/libjuice.*licence.*notice"):
                    package.check_public(self.binary(windows), windows)
                self.symbols[windows] = original

    def test_retained_ice_candidate_is_rejected_after_another_target_overwrites_the_marker(self):
        for flavour in (None, "public", "public-network"):
            for windows in (False, True):
                for test_mode in (False, True):
                    with self.subTest(flavour=flavour, windows=windows, test=test_mode):
                        marker = self.build / "flavour.txt"
                        if flavour is None:
                            marker.unlink(missing_ok=True)
                        else:
                            marker.write_text(flavour + "\n")
                        enet = "1 T enet_host_create\n" if flavour == "public-network" else ""
                        self.symbols = {False: "1 T main\n" + enet, True: "1 T _main\n" + enet}
                        self.symbols[windows] += "1 T juice_gather_candidates\n"
                        out = self.root / "packages"
                        args = SimpleNamespace(test=test_mode, build=self.build, out=out, version="authored")
                        with self.assertRaisesRegex(SystemExit, "ICE/libjuice"):
                            package.package(args)
                        self.assertFalse(out.exists(), "rejection must precede creating archives or copying symbols")

    def test_explicit_ice_flavour_is_still_not_a_packaging_opt_in(self):
        (self.build / "flavour.txt").write_text("public-network-ice\n")
        out = self.root / "packages"
        with self.assertRaisesRegex(SystemExit, "unknown flavour"):
            package.package(SimpleNamespace(test=True, build=self.build, out=out, version="authored"))
        self.assertEqual(self.nm_calls, [])
        self.assertFalse(out.exists())

    def test_feature_marker_is_rejected_even_without_ice_symbols(self):
        for windows in (False, True):
            with self.subTest(windows=windows):
                self.binary(windows).write_bytes(b"authored prefix\nCE-NET-FEATURE:ICE-LIBJUICE:1\nauthored suffix\n")
                self.symbols[windows] = "1 T main\n1 T enet_host_create\n"
                with self.assertRaisesRegex(SystemExit, "ICE/libjuice"):
                    package.check_public(self.binary(windows), windows)
        self.assertEqual(self.nm_calls, [], "the retained feature marker does not depend on symbol inspection")

    def test_failed_or_missing_symbol_tool_cannot_authorise_a_package(self):
        for windows in (False, True):
            for error in (subprocess.CalledProcessError(1, "llvm-nm"), FileNotFoundError("authored missing tool")):
                with self.subTest(windows=windows, error=type(error).__name__):
                    with patch.object(package.subprocess, "run", side_effect=error):
                        with self.assertRaisesRegex(SystemExit, "cannot inspect.*symbols"):
                            package.check_public(self.binary(windows), windows)

    def test_empty_symbol_listing_cannot_authorise_a_stripped_package(self):
        for windows in (False, True):
            with self.subTest(windows=windows), patch.object(package, "NETWORK", False):
                self.symbols[windows] = "\n"
                with self.assertRaisesRegex(SystemExit, "unstripped"):
                    package.check_public(self.binary(windows), windows)

    def test_normal_non_ice_default_and_network_candidates_pass(self):
        for network in (False, True):
            for windows in (False, True):
                with self.subTest(network=network, windows=windows), patch.object(package, "NETWORK", network):
                    self.symbols[windows] = "1 T main\n" + ("1 T enet_host_create\n" if network else "")
                    package.check_public(self.binary(windows), windows)


@unittest.skipUnless(os.environ.get("BEA_PACKAGE_BINARY_FIXTURES") == "1", "opt-in authored compiler fixtures")
class BinaryIcePackageTests(unittest.TestCase):
    def check_platform(self, windows):
        owner = package.ROOT / "local-data/port/package-ice-tests"
        owner.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="linked-", dir=owner) as folder, contextlib.ExitStack() as stack:
            root = Path(folder)
            work = root / "work"
            marker = work / "mirror/public-unreplaced.txt"
            marker.parent.mkdir(parents=True)
            marker.write_text("")
            stack.enter_context(patch.object(package, "WORK", work))
            stack.enter_context(patch.object(package, "NETWORK", False))
            stack.enter_context(patch.object(package, "ALLOW_UNREPLACED", False))
            stack.enter_context(patch.object(package.deps, "require_ffmpeg_shared"))
            stack.enter_context(patch.object(package.deps, "ffmpeg_shared_files", return_value=[]))
            tools = package.deps.DATA / ("llvm-mingw/bin" if windows else "llvm/bin")
            compiler = tools / ("i686-w64-mingw32-clang" if windows else "clang")
            common = [str(compiler), "-nostdlib"]
            if windows:
                link = ["-Wl,--entry,_authored_entry"]
            else:
                common += ["--target=i686-pc-linux-gnu"]
                link = [f"--ld-path={package.deps.LLD}", "-Wl,-e,authored_entry", "-Wl,-rpath,$ORIGIN/lib"]

            def run(args):
                result = subprocess.run(args, capture_output=True, text=True, timeout=30)
                if result.returncode:
                    self.fail(f"authored fixture tool {Path(args[0]).name} failed: {result.stderr}")
                return result

            plain = root / "plain.c"
            plain.write_text("int authored_entry(void) { return 0; }\n")
            binary = root / ("plain.exe" if windows else "plain")
            run(common + [str(plain), *link, "-o", str(binary)])
            package.check_public(binary, windows)
            stripped = root / ("stripped.exe" if windows else "stripped")
            run([str(tools / "llvm-strip"), "--strip-all", "-o", str(stripped), str(binary)])
            with self.assertRaisesRegex(SystemExit, "unstripped"):
                package.check_public(stripped, windows)

            juice = root / "juice.c"
            juice.write_text("int juice_create(void) { return 1; }\n")
            juice_obj = root / "juice.o"
            run(common + ["-c", str(juice), "-o", str(juice_obj)])
            archive = root / "authored-juice.a"
            run([str(tools / "llvm-ar"), "rcs", str(archive), str(juice_obj)])
            caller = root / "caller.c"
            caller.write_text("int juice_create(void); int authored_entry(void) { return juice_create(); }\n")
            legacy = root / ("legacy.exe" if windows else "legacy")
            run(common + [str(caller), str(archive), *link, "-o", str(legacy)])
            with self.assertRaisesRegex(SystemExit, "ICE/libjuice"):
                package.check_public(legacy, windows)

            feature = root / "feature.c"
            feature.write_text('__attribute__((used)) const char ce_network_ice_feature_marker[] = '
                               '"CE-NET-FEATURE:ICE-LIBJUICE:1"; int authored_entry(void) { return 0; }\n')
            lto = root / ("lto.exe" if windows else "lto")
            symbol = "_ce_network_ice_feature_marker" if windows else "ce_network_ice_feature_marker"
            run(common + ["-O2", "-flto", "-fdata-sections", "-ffunction-sections", str(feature), *link,
                          "-Wl,--gc-sections", f"-Wl,--undefined={symbol}", "-o", str(lto)])
            for candidate in (lto, stripped):
                if candidate == stripped:
                    run([str(tools / "llvm-strip"), "--strip-all", "-o", str(stripped), str(lto)])
                self.assertIn(package.ICE_FEATURE_MARKER, candidate.read_bytes())
                with self.assertRaisesRegex(SystemExit, "ICE/libjuice"):
                    package.check_public(candidate, windows)

    def test_linux_i686_static_library_lto_gc_and_stripped_candidates(self):
        self.check_platform(False)

    def test_windows_i686_static_library_lto_gc_and_stripped_candidates(self):
        self.check_platform(True)


if __name__ == "__main__":
    unittest.main()
