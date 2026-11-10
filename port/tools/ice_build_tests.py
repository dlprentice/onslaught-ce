#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Authored optional-build fixtures; no downloads, game data, services or real compilers."""
import hashlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
from contextlib import ExitStack, nullcontext
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import build
import ice_bridge_build
import ice_deps
import package


class OptionalInputs(unittest.TestCase):
    def test_pin_matches_the_tested_bridge_and_is_outside_ordinary_dependencies(self):
        row = ice_deps.pin()
        self.assertEqual(ice_bridge_build.JUICE_SHA256, row["sha"])
        self.assertEqual(ice_bridge_build.JUICE_URL, row["url"])
        self.assertEqual(ice_bridge_build.JUICE_LICENSE, row["licence"])
        self.assertNotIn("libjuice", build.deps.lock())

    def test_both_recipes_are_static_client_only_and_ignore_caller_compiler_settings(self):
        with patch.dict(os.environ, {"CC": "/authored/unexpected-cc", "CFLAGS": "unexpected", "SOURCE_DATE_EPOCH": "99"}):
            for target in ("linux", "windows"):
                cmd, env = ice_deps.recipe(target)
                for flag in ("-DBUILD_SHARED_LIBS=OFF", "-DNO_TESTS=ON", "-DNO_SERVER=ON", "-DUSE_NETTLE=OFF"):
                    self.assertIn(flag, cmd)
                self.assertEqual("0", env["SOURCE_DATE_EPOCH"])
                self.assertNotIn("CC", env)
                self.assertNotIn("CFLAGS", env)
                self.assertTrue(any("-ffile-prefix-map=" in flag for flag in cmd))
                self.assertIn("qcD", " ".join(cmd))
                self.assertNotIn(str(build.deps.PREFIX), str(ice_deps.prefix(target)))
                self.assertNotIn(str(build.deps.PREFIX_WIN), str(ice_deps.prefix(target)))
                self.assertEqual(target == "windows", "-mno-incremental-linker-compatible" in " ".join(cmd))

    def test_enabled_profile_is_bound_by_the_real_identity_helper(self):
        digest = "ab" * 32
        with patch.object(build, "PUBLIC", True), patch.object(build, "WITH_ICE", True), \
             patch.object(build, "SYSTEM_CLANG", False), patch.object(build.deps, "SYSROOT_NAME", "sniper"), \
             patch.object(build, "NETWORK_SOURCE_ID", None), \
             patch.object(build.network_source_id, "compute_source_id", return_value=digest) as compute:
            build.configure_network_identity()
            compute.assert_called_once_with(build.ROOT, with_ice=True)
            self.assertEqual(digest, build.NETWORK_SOURCE_ID)

    def test_existing_builds_never_validate_or_link_optional_inputs(self):
        with patch.object(build, "WITH_ICE", False), patch.object(build, "vorbis_lib", return_value=""), \
             patch.object(build.deps, "require_ffmpeg_shared", return_value=[]), patch.object(ice_deps, "require") as require:
            for target in ("linux", "windows"):
                for public, network in ((True, False), (True, True), (False, False)):
                    with patch.object(build, "PUBLIC", public), patch.object(build, "WITH_NETWORK", network):
                        self.assertNotIn("libjuice", build.link_flags(target))
            require.assert_not_called()

    def test_missing_receipt_refuses_before_link_flags_can_be_emitted(self):
        with patch.object(build, "WITH_ICE", True), patch.object(build, "vorbis_lib", return_value=""), \
             patch.object(build.deps, "require_ffmpeg_shared", return_value=[]), \
             patch.object(ice_deps, "require", side_effect=SystemExit("optional ICE receipt missing")):
            for target in ("linux", "windows"):
                with self.assertRaisesRegex(SystemExit, "receipt missing"):
                    build.link_flags(target)

    def test_unreviewed_ice_package_is_refused(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "flavour.txt").write_text("public-network-ice\n")
            with self.assertRaisesRegex(SystemExit, "unknown flavour"):
                package.build_flavour(root)


class ReceiptTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="ce-ice-receipt-")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.archive = self.root / "authored.tar.gz"
        with tarfile.open(self.archive, "w:gz") as stream:
            for name, data in (("LICENSE", b"authored fixture licence\n"), ("src/client.c", b"int fixture;\n")):
                entry = tarfile.TarInfo("authored/" + name)
                entry.size = len(data)
                stream.addfile(entry, io.BytesIO(data))
        self.row = dict(name="libjuice", version="authored", licence="fixture", sha=hashlib.sha256(self.archive.read_bytes()).hexdigest(),
                        url="https://example.invalid/authored", file=self.archive)
        self.data = patch.object(build.deps, "DATA", self.root)
        self.data.start()
        self.addCleanup(self.data.stop)
        self.pin = patch.object(ice_deps, "pin", return_value=self.row)
        self.pin.start()
        self.addCleanup(self.pin.stop)
        port = self.root / "fixture-port"
        (port / "network").mkdir(parents=True)
        (port / "deps.lock").write_bytes((build.deps.PORT / "deps.lock").read_bytes())
        patch_data = b"--- a/src/client.c\n+++ b/src/client.c\n@@ -1 +1 @@\n-int fixture;\n+int patched_fixture;\n"
        (port / "network/libjuice-relay-only.patch").write_bytes(patch_data)
        (port / "ice-deps.lock").write_text("patch:libjuice-relay-only.patch\t" +
                                           hashlib.sha256(patch_data).hexdigest() + "\tMPL-2.0\n")
        self.port = patch.object(build.deps, "PORT", port)
        self.port.start()
        self.addCleanup(self.port.stop)
        self.src = ice_deps.prepare_source(self.row)
        self.assertEqual(b"int patched_fixture;\n", (self.src / "src/client.c").read_bytes())
        dest = ice_deps.prefix("windows")
        for name in ("lib/libjuice.a", "include/juice/juice.h", "licenses/MPL-2.0.txt"):
            path = dest / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"authored artifact\n")
        self.receipt = dest / ".bea-ice-build.json"
        saved = dict(ice_deps.expected("windows"), artifacts=ice_deps.artifacts("windows"))
        self.receipt.write_text(json.dumps(saved))

    def test_matching_receipt_is_usable_and_changed_archive_is_refused(self):
        archive = ice_deps.require("windows")
        archive.write_bytes(b"changed authored archive\n")
        with self.assertRaisesRegex(SystemExit, "artifacts changed"):
            ice_deps.require("windows")

    def test_changed_header_licence_and_source_are_independently_refused(self):
        for path in (ice_deps.prefix("windows") / "include/juice/juice.h",
                     ice_deps.prefix("windows") / "licenses/MPL-2.0.txt", self.src / "src/client.c"):
            old = path.read_bytes()
            path.write_bytes(b"changed authored input\n")
            with self.subTest(path=path.name), self.assertRaisesRegex(SystemExit, "stale|changed"):
                ice_deps.require("windows")
            path.write_bytes(old)
        ice_deps.require("windows")

    def test_recipe_change_and_missing_receipt_are_refused(self):
        with patch.object(ice_deps, "recipe", return_value=(["authored-changed-recipe"], {})):
            with self.assertRaisesRegex(SystemExit, "stale"):
                ice_deps.require("windows")
        self.receipt.unlink()
        with self.assertRaisesRegex(SystemExit, "receipt missing"):
            ice_deps.require("windows")


    def test_legacy_wrong_type_and_incomplete_patch_receipts_are_refused(self):
        original = json.loads(self.receipt.read_text())
        invalid = [dict(original, schema=value) for value in (1, True, 2.0)]
        invalid += [{k: v for k, v in original.items() if k != "source_patches"},
                    dict(original, source_patches=[]), dict(original, source_tree_sha256="ef" * 32)]
        for known in invalid:
            self.receipt.write_text(json.dumps(known))
            with self.subTest(schema=known.get("schema")), self.assertRaisesRegex(SystemExit, "stale|changed"):
                ice_deps.require("windows")
        self.receipt.write_text(json.dumps(original))
        stamp = ice_deps.source_root() / ".bea-ice-source.json"
        saved = json.loads(stamp.read_text())
        stamp.write_text(json.dumps(dict(saved, schema=2.0)))
        with self.assertRaisesRegex(SystemExit, "patched source is stale"):
            ice_deps.require("windows")
        stamp.write_text(json.dumps(saved))
        ice_deps.require("windows")

    def test_patch_mutation_and_incomplete_source_are_refused_without_repair(self):
        patch_path = build.deps.PORT / "network/libjuice-relay-only.patch"
        original = patch_path.read_bytes()
        patch_path.write_bytes(original + b"# changed\n")
        with self.assertRaisesRegex(SystemExit, "differs from its pin"):
            ice_deps.require("windows")
        patch_path.write_bytes(original)
        (ice_deps.source_root() / ".bea-ice-source.json").unlink()
        with self.assertRaisesRegex(SystemExit, "incomplete optional ICE source"):
            ice_deps.prepare_source(self.row)


class SchedulingTests(unittest.TestCase):
    def test_changed_optional_archive_relinks_without_recompiling(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            source = root / "fixture.cpp"
            source.write_text("int authored_fixture;\n")
            archive = root / "libjuice.a"
            archive.write_bytes(b"authored archive\n")
            calls, compiler = root / "calls", root / "compiler.py"
            compiler.write_text("#!/usr/bin/env python3\nimport pathlib,sys\n"
                                f"calls=pathlib.Path({str(calls)!r})\n"
                                "out=pathlib.Path(sys.argv[sys.argv.index('-o')+1])\n"
                                "out.parent.mkdir(parents=True,exist_ok=True);out.write_bytes(b'x')\n"
                                "with calls.open('a') as s:s.write(('compile' if '-c' in sys.argv else 'link')+'\\n')\n"
                                "if '-MF' in sys.argv:\n"
                                " pathlib.Path(sys.argv[sys.argv.index('-MF')+1]).write_text(str(out)+': '+sys.argv[-1]+'\\n')\n")
            compiler.chmod(0o755)
            targets = {"linux": dict(cxx=[str(compiler)], cc=[str(compiler)], includes=[])}
            with patch.object(build, "BUILD", root / "build"), patch.object(build, "TARGETS", targets), \
                 patch.object(build, "WITH_ICE", True), patch.object(build, "link_flags", return_value=""), \
                 patch.object(build, "port_sources", return_value=[(source, "pcxx", "")]), \
                 patch.object(ice_deps, "require", return_value=archive):
                ninja = build.ninja_file("linux", [], False, True)
                for _ in range(2):
                    subprocess.run(["ninja", "-f", str(ninja)], check=True, capture_output=True)
                self.assertEqual(["compile", "link"], calls.read_text().splitlines())
                exe = root / "build/linux/bea"
                changed = exe.stat().st_mtime_ns + 1_000_000
                os.utime(archive, ns=(changed, changed))
                subprocess.run(["ninja", "-f", str(ninja)], check=True, capture_output=True)
                self.assertEqual(["compile", "link", "link"], calls.read_text().splitlines())


class LinkReceiptTests(unittest.TestCase):
    def test_receipt_binds_both_binaries_and_optional_inputs_without_paths(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            install = root / "optional"
            for name in ("lib/libjuice.a", "include/juice/juice.h", "licenses/MPL-2.0.txt", ".bea-ice-build.json"):
                path = install / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"authored optional input\n" if name != ".bea-ice-build.json" else
                                 json.dumps({"schema": 2, "source_patches": [{"name": "authored.patch", "sha256": "ab" * 32,
                                                                          "licence": "MPL-2.0"}],
                                             "source_tree_sha256": "cd" * 32}).encode())
            for target in ("linux", "windows"):
                names = ("bea", "onslaught-ce-launch") if target == "linux" else ("bea.exe", "onslaught-ce.exe")
                out = root / "build" / target
                out.mkdir(parents=True)
                (out / names[0]).write_bytes(b"authored executable\0" + build.ICE_BINARY_TAG.encode() + b"\0")
                (out / names[1]).write_bytes(b"authored launcher\n")
                with patch.object(build, "BUILD", root / "build"), patch.object(build, "PUBLIC", True), \
                     patch.object(build, "WITH_NETWORK", True), patch.object(build, "WITH_ICE", True), \
                     patch.object(build, "NETWORK_SOURCE_ID", "ab" * 32), patch.object(build, "CE_DEFINES", []), \
                     patch.object(ice_deps, "require", return_value=install / "lib/libjuice.a"), \
                     patch.object(ice_deps, "prefix", return_value=install):
                    build.write_ice_link_receipt(target)
                saved = json.loads((out / ".bea-ice-link.json").read_text())
                self.assertEqual(2, saved["schema"])
                self.assertEqual("cd" * 32, saved["source_tree_sha256"])
                self.assertEqual("authored.patch", saved["source_patches"][0]["name"])
                self.assertEqual("public-network-ice", saved["profile"])
                self.assertEqual("ab" * 32, saved["source_id"])
                self.assertNotIn(str(root), json.dumps(saved))
                for key, name in zip(("game", "launcher"), names):
                    self.assertEqual(hashlib.sha256((out / name).read_bytes()).hexdigest(), saved["binaries"][key]["sha256"])
                    self.assertEqual((out / name).stat().st_size, saved["binaries"][key]["size"])
                self.assertEqual(hashlib.sha256((install / "lib/libjuice.a").read_bytes()).hexdigest(),
                                 saved["libjuice"]["static_library_sha256"])
                self.assertEqual(hashlib.sha256((install / ".bea-ice-build.json").read_bytes()).hexdigest(),
                                 saved["libjuice"]["build_receipt_sha256"])

    def test_no_positive_receipt_without_the_compiled_marker(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "linux").mkdir()
            (root / "linux/bea").write_bytes(b"authored executable without the optional feature\n")
            with patch.object(build, "BUILD", root), patch.object(build, "PUBLIC", True), \
                 patch.object(build, "WITH_NETWORK", True), patch.object(build, "WITH_ICE", True), \
                 patch.object(ice_deps, "require", return_value=root / "libjuice.a"):
                with self.assertRaisesRegex(SystemExit, "missing its retained feature marker"):
                    build.write_ice_link_receipt("linux")
            self.assertFalse((root / "linux/.bea-ice-link.json").exists())

    def test_failed_game_or_launcher_link_keeps_the_receipt_incomplete(self):
        for game_rc, launcher_rc in ((1, 0), (0, 1), (0, 0)):
            with self.subTest(game_rc=game_rc, launcher_rc=launcher_rc), tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                targets = {name: {key: list(value) for key, value in row.items()} for name, row in build.TARGETS.items()}
                result = type("Result", (), {"returncode": game_rc})()
                with ExitStack() as stack:
                    for module, name, value in ((sys, "argv", ["build.py", "--public", "--with-network", "--with-ice"]),
                                                (build, "BUILD", root), (build, "PUBLIC", False),
                                                (build, "WITH_NETWORK", False), (build, "WITH_ICE", False),
                                                (build, "CE_DEFINES", []), (build, "TARGETS", targets)):
                        stack.enter_context(patch.object(module, name, value))
                    for module, name, value in ((build, "build_mirror", []), (build, "configure_network_identity", None),
                                                (build, "check_compiler", None), (build, "ninja_file", root / "ninja"),
                                                (build.subprocess, "run", result), (build, "build_launcher", launcher_rc),
                                                (build.deps, "sdl_install_lock", nullcontext()),
                                                (ice_deps, "install_lock", nullcontext()),
                                                (build.deps, "require_sdl_receipt", None),
                                                (build.deps, "sdl_receipt_sha256", "cd" * 32),
                                                (build.deps, "sdl_link_receipt", {})):
                        stack.enter_context(patch.object(module, name, return_value=value))
                    write = stack.enter_context(patch.object(build, "write_ice_link_receipt"))
                    self.assertEqual(game_rc or launcher_rc, build.main())
                    self.assertEqual({"status": "incomplete"}, json.loads((root / "linux/.bea-ice-link.json").read_text()))
                    if game_rc or launcher_rc:
                        write.assert_not_called()
                    else:
                        write.assert_called_once_with("linux")


if __name__ == "__main__":
    unittest.main()
