#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Authored ICE package fixtures; no game inputs, services or external processes."""
import contextlib
import copy
import gzip
import hashlib
import io
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import package
import package_ice_inputs as ice_package
import package_signaling_inputs as signaling


def sha(data):
    return hashlib.sha256(data).hexdigest()


class IceOptInTests(unittest.TestCase):
    def setUp(self):
        owner = package.ROOT / "local-data/port/package-ice-optin-tests"
        owner.mkdir(parents=True, exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(prefix="case-", dir=owner)
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.port, self.work, self.build = (self.root / name for name in ("port", "work", "build"))
        self.licence = b"authored fixture licence\n"
        self.archive = self.root / "authored-source.tar.gz"
        self.write_archive()
        self.pin = dict(name="libjuice", version="authored", licence="MPL-2.0", sha=sha(self.archive.read_bytes()),
                        url="https://example.invalid/libjuice/authored-revision")
        self.patch_bytes = b"# SPDX-License-Identifier: MPL-2.0\n# authored source modification\n"
        self.source_patch = dict(name="libjuice-relay-only.patch", licence="MPL-2.0", sha256=sha(self.patch_bytes))
        self.patched_source = self.root / "patched-source"
        self.patched_source.mkdir()
        (self.patched_source / "authored.c").write_bytes(b"authored modified source\n")
        (self.patched_source / "LICENSE").write_bytes(self.licence)
        self.source_tree_id = package.deps.source_tree_sha256(self.patched_source)
        for rel, data in (("build.py", b"authored CE recipe\n"), ("tools/ice_deps.py", b"authored library recipe\n"),
                          ("network/libjuice-relay-only.patch", self.patch_bytes),
                          ("ice-deps.lock", b"authored source pin\n"), ("deps.lock", b"# authored no patches\n")):
            path = self.port / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        marker = self.work / "mirror/public-unreplaced.txt"
        marker.parent.mkdir(parents=True)
        marker.write_text("")
        self.prefixes = {target: self.root / "install" / target for target in ("linux", "windows")}
        cache = self.root / "cache"
        (cache / "ice-locks").mkdir(parents=True)
        for target in ("linux-sniper", "windows"):
            (cache / "ice-locks" / (target + ".lock")).touch()
        for target, prefix in self.prefixes.items():
            for rel, data in (("lib/libjuice.a", b"authored static library\n"),
                              ("include/juice/juice.h", b"authored header\n"),
                              ("licenses/MPL-2.0.txt", self.licence),
                              (".bea-ice-build.json", json.dumps(dict(schema=2, source_patches=[self.source_patch],
                               source_tree_sha256=self.source_tree_id, authored=True)).encode())):
                path = prefix / rel
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
            folder = self.build / target
            folder.mkdir(parents=True)
            game, launcher = self.names(target)
            (folder / game).write_bytes(b"authored game " + target.encode() + ice_package.MARKER + b"\0")
            (folder / launcher).write_bytes(b"authored launcher " + target.encode())
        (self.build / "flavour.txt").write_text("public-network-ice\n")
        self.source_id = "ab" * 32
        self.ice = SimpleNamespace(pin=lambda: dict(self.pin, file=self.archive), prefix=lambda target: self.prefixes[target],
                                   source_patches=lambda: [dict(self.source_patch)],
                                   checked_source=lambda: (self.patched_source, {}),
                                   require=Mock(), target_name=lambda target: "linux-sniper" if target == "linux" else target)
        self.identity = SimpleNamespace(compute_source_id=Mock(return_value=self.source_id))
        self.stack = contextlib.ExitStack()
        self.addCleanup(self.stack.close)
        for callback in (patch.object(ice_package, "REVIEWED_PIN", dict(self.pin)),
                         patch.object(ice_package, "REVIEWED_PATCH", dict(self.source_patch)),
                         patch.object(ice_package, "SOURCE_TREE_SHA256", self.source_tree_id),
                         patch.object(ice_package, "LICENCE_SHA256", sha(self.licence)),
                         patch.object(ice_package, "optional_tools", return_value=(self.ice, self.identity)),
                         patch.object(package.deps, "DATA", cache),
                         patch.object(package.deps, "PORT", self.port), patch.object(package.deps, "SYSROOT_NAME", "sniper"),
                         patch.object(package, "WORK", self.work), patch.object(package, "NETWORK", False),
                         patch.object(package, "ALLOW_UNREPLACED", False)):
            self.stack.enter_context(callback)
        self.receipts = {target: self.receipt(target) for target in ("linux", "windows")}
        self.save_receipts()

    @staticmethod
    def names(target):
        return ("bea", "onslaught-ce-launch") if target == "linux" else ("bea.exe", "onslaught-ce.exe")

    def write_archive(self, members=None):
        if members is None:
            members = [("authored/LICENSE", self.licence), ("authored/src/client.c", b"authored source and notices\n")]
        with self.archive.open("wb") as raw, gzip.GzipFile(fileobj=raw, mode="wb", mtime=0, filename="") as gz, \
                tarfile.open(fileobj=gz, mode="w") as stream:
            for name, data in members:
                entry = tarfile.TarInfo(name)
                entry.size = len(data)
                stream.addfile(entry, io.BytesIO(data))

    def receipt(self, target):
        prefix = self.prefixes[target]
        game, launcher = self.names(target)
        def artifact(name):
            data = (self.build / target / name).read_bytes()
            return dict(name=name, size=len(data), sha256=sha(data))
        return dict(schema=2, target=target, profile="public-network-ice", source_id=self.source_id,
                    source_patches=[dict(self.source_patch)], source_tree_sha256=self.source_tree_id,
                    build_recipe_sha256=sha((self.port / "build.py").read_bytes()),
                    marker="CE-NET-FEATURE:ICE-LIBJUICE:1", system_clang=False, sysroot_name="sniper", test_build=False,
                    libjuice=dict(name="libjuice", version="authored", licence="MPL-2.0", source_sha256=self.pin["sha"],
                                  source_url=self.pin["url"], build_receipt_sha256=sha((prefix / ".bea-ice-build.json").read_bytes()),
                                  static_library_sha256=sha((prefix / "lib/libjuice.a").read_bytes()),
                                  header_sha256=sha((prefix / "include/juice/juice.h").read_bytes()),
                                  licence_text_sha256=sha(self.licence)),
                    binaries=dict(game=artifact(game), launcher=artifact(launcher)))

    def save_receipts(self):
        for target, receipt in self.receipts.items():
            (self.build / target / ".bea-ice-link.json").write_text(json.dumps(receipt))

    def test_source_licence_and_provenance_are_exact_and_deterministic(self):
        first = ice_package.prepare(self.build)
        self.assertEqual(first, ice_package.prepare(self.build))
        self.identity.compute_source_id.assert_called_with(self.port.parent, with_ice=True)
        for target, payload in first.items():
            files = payload["files"]
            self.assertEqual(len(files), 9)
            self.assertEqual(files["licenses/libjuice-MPL-2.0.txt"], self.licence)
            self.assertEqual(files["source/libjuice-authored-revision.tar.gz"], self.archive.read_bytes())
            self.assertEqual(files["source/libjuice-relay-only.patch"], self.patch_bytes)
            provenance = json.loads(files["source/libjuice-PROVENANCE.json"])
            self.assertEqual(provenance["schema"], 2)
            self.assertEqual(provenance["build"], self.receipts[target])
            self.assertFalse(provenance["source_unmodified"])
            self.assertEqual(provenance["source_tree_sha256"], self.source_tree_id)
            self.assertIn(b"--fuzz=0", files["source/libjuice-APPLY.txt"])
            self.assertIn(b"under either MPL-2.0 or GPL-3.0-only", payload["notice"])
            self.assertNotIn(str(self.root).encode(), b"".join(files.values()))

    def test_missing_or_changed_source_is_refused(self):
        original = self.archive.read_bytes()
        for data in (None, b"wrong source"):
            with self.subTest(missing=data is None):
                if data is None:
                    self.archive.unlink()
                else:
                    self.archive.write_bytes(data)
                with self.assertRaises(SystemExit):
                    ice_package.prepare(self.build)
                self.archive.write_bytes(original)

    def test_missing_changed_or_unreviewed_patch_is_refused(self):
        path = self.port / "network/libjuice-relay-only.patch"
        for data in (None, b"changed patch"):
            if data is None:
                path.unlink()
            else:
                path.write_bytes(data)
            with self.subTest(missing=data is None), self.assertRaises(SystemExit):
                ice_package.prepare(self.build)
            path.write_bytes(self.patch_bytes)
        for metadata in ([], [dict(self.source_patch, sha256="00" * 32)],
                         [dict(self.source_patch, licence="other")], [self.source_patch, self.source_patch]):
            with self.subTest(metadata=metadata), patch.object(self.ice, "source_patches", return_value=metadata):
                with self.assertRaisesRegex(SystemExit, "patch metadata"):
                    ice_package.prepare(self.build)

    def test_patched_tree_content_and_mode_are_verified_independently(self):
        source = self.patched_source / "authored.c"
        original = source.read_bytes()
        source.write_bytes(original + b"changed")
        with self.assertRaisesRegex(SystemExit, "reconstructed tree"):
            ice_package.prepare(self.build)
        source.write_bytes(original)
        mode = source.stat().st_mode & 0o777
        source.chmod(mode ^ 0o100)
        with self.assertRaisesRegex(SystemExit, "reconstructed tree"):
            ice_package.prepare(self.build)
        source.chmod(mode)

    def test_source_with_missing_or_wrong_licence_is_refused(self):
        for members in ([("authored/src/client.c", b"authored source")], [("authored/LICENSE", b"wrong licence")]):
            self.write_archive(members)
            self.pin["sha"] = sha(self.archive.read_bytes())
            with self.subTest(member=members[0][0]), patch.object(ice_package, "REVIEWED_PIN", dict(self.pin)):
                with self.assertRaisesRegex(SystemExit, "LICENSE|licence"):
                    ice_package.prepare(self.build)

    def test_ordinary_dependency_patch_is_refused_even_with_a_valid_pin(self):
        patch_file = self.port / "deps-patches/libjuice-authored.patch"
        patch_file.parent.mkdir()
        patch_file.write_bytes(b"authored patch\n")
        (self.port / "deps.lock").write_text("patch:libjuice-authored.patch\t" + sha(patch_file.read_bytes()) + "\n")
        with self.assertRaisesRegex(SystemExit, "ordinary dependency patches"):
            ice_package.prepare(self.build)

    def test_unreviewed_pin_or_changed_installed_licence_is_refused(self):
        for key in ("version", "licence", "sha", "url"):
            original = self.pin[key]
            self.pin[key] = "unreviewed"
            with self.subTest(pin_field=key), self.assertRaisesRegex(SystemExit, "reviewed"):
                ice_package.prepare(self.build)
            self.pin[key] = original
        for target in ("linux", "windows"):
            path = self.prefixes[target] / "licenses/MPL-2.0.txt"
            path.write_bytes(b"wrong installed licence")
            with self.subTest(target=target), self.assertRaisesRegex(SystemExit, "licence"):
                ice_package.prepare(self.build)
            path.write_bytes(self.licence)

    def test_missing_invalid_or_incomplete_link_receipts_are_refused(self):
        for target in ("linux", "windows"):
            path = self.build / target / ".bea-ice-link.json"
            original = path.read_bytes()
            for data in (None, b"not json", b'{"status":"incomplete"}'):
                with self.subTest(target=target, data=data):
                    if data is None:
                        path.unlink()
                    else:
                        path.write_bytes(data)
                    with self.assertRaises(SystemExit):
                        ice_package.prepare(self.build)
                    path.write_bytes(original)

    def test_legacy_or_incomplete_dependency_source_receipts_are_refused(self):
        for target in ("linux", "windows"):
            path = self.prefixes[target] / ".bea-ice-build.json"
            original = path.read_bytes()
            known = json.loads(original)
            variants = [dict(known, schema=1), dict(known, schema=2.0), dict(known, source_patches=[]),
                        dict(known, source_tree_sha256="00" * 32)]
            for field in ("source_patches", "source_tree_sha256"):
                changed = dict(known);del changed[field];variants.append(changed)
            for value in variants:
                path.write_text(json.dumps(value))
                with self.subTest(target=target, value=value), self.assertRaisesRegex(SystemExit, "schema2 source identity"):
                    ice_package.prepare(self.build)
            path.write_bytes(original)

    def test_profile_source_recipe_and_input_receipt_mismatches_are_refused(self):
        mutations = (("target", "other"), ("profile", "public-network"), ("source_id", "cd" * 32),
                     ("schema", 1), ("schema", 2.0), ("source_patches", []), ("source_tree_sha256", "00" * 32),
                     ("build_recipe_sha256", "00" * 32), ("marker", "other"), ("system_clang", True),
                     ("sysroot_name", ""), ("test_build", True), ("test_build", 0))
        for target in ("linux", "windows"):
            original = copy.deepcopy(self.receipts[target])
            for key, value in mutations:
                self.receipts[target][key] = value
                self.save_receipts()
                with self.subTest(target=target, field=key, value=value), self.assertRaises(SystemExit):
                    ice_package.prepare(self.build)
                self.receipts[target] = copy.deepcopy(original)
            for key in original["libjuice"]:
                self.receipts[target]["libjuice"][key] = "wrong"
                self.save_receipts()
                with self.subTest(target=target, libjuice_field=key), self.assertRaises(SystemExit):
                    ice_package.prepare(self.build)
                self.receipts[target] = copy.deepcopy(original)
            self.save_receipts()

    def test_link_receipt_requires_both_patched_source_fields(self):
        for target in ("linux", "windows"):
            original = copy.deepcopy(self.receipts[target])
            for field in ("source_patches", "source_tree_sha256"):
                self.receipts[target] = copy.deepcopy(original)
                del self.receipts[target][field]
                self.save_receipts()
                with self.subTest(target=target, field=field), self.assertRaises(SystemExit):
                    ice_package.prepare(self.build)
            self.receipts[target] = original
            self.save_receipts()

    def test_changed_game_launcher_library_header_or_optional_receipt_is_refused(self):
        for target in ("linux", "windows"):
            paths = [self.build / target / name for name in self.names(target)]
            paths += [self.prefixes[target] / name for name in ("lib/libjuice.a", "include/juice/juice.h", ".bea-ice-build.json")]
            for path in paths:
                original = path.read_bytes()
                path.write_bytes(original + b"changed")
                with self.subTest(target=target, file=path.name), self.assertRaises(SystemExit):
                    ice_package.prepare(self.build)
                path.write_bytes(original)

    def test_invalid_source_id_or_profile_and_bad_optional_receipt_are_refused(self):
        self.identity.compute_source_id.return_value = "invalid"
        with self.assertRaisesRegex(SystemExit, "source ID"):
            ice_package.prepare(self.build)
        self.identity.compute_source_id.return_value = self.source_id
        with patch.object(package.deps, "SYSROOT_NAME", ""), self.assertRaisesRegex(SystemExit, "sniper"):
            ice_package.prepare(self.build)
        self.ice.require.side_effect = SystemExit("authored stale optional receipt")
        with self.assertRaisesRegex(SystemExit, "stale optional receipt"):
            ice_package.prepare(self.build)

    def test_binary_snapshot_rejects_changes_after_receipt_validation(self):
        data = ice_package.prepare(self.build)["linux"]
        saved = self.root / "saved-game"
        shutil.copyfile(self.build / "linux/bea", saved)
        ice_package.check_snapshot(saved, data["binaries"]["game"])
        saved.write_bytes(b"changed")
        with self.assertRaisesRegex(SystemExit, "changed"):
            ice_package.check_snapshot(saved, data["binaries"]["game"])

    def test_existing_read_only_install_lock_is_required_and_never_created(self):
        lock = package.deps.DATA / "ice-locks/linux-sniper.lock"
        lock.unlink()
        with self.assertRaisesRegex(SystemExit, "install lock"):
            ice_package.prepare(self.build)
        self.assertFalse(lock.exists())

    def test_rehashed_receipt_cannot_replace_the_required_binary_feature_marker(self):
        binary = self.build / "linux/bea"
        binary.write_bytes(b"authored non-ICE candidate")
        self.receipts["linux"] = self.receipt("linux")
        self.save_receipts()
        with self.assertRaisesRegex(SystemExit, "feature marker"):
            ice_package.prepare(self.build)

    def test_explicit_flag_requires_exact_flavour_and_cannot_use_test_exception(self):
        for flavour, enabled, test in (("public", True, False), ("public-network", True, False),
                                       ("public-network-ice", False, False), ("public-network-ice", True, True)):
            (self.build / "flavour.txt").write_text(flavour + "\n")
            args = SimpleNamespace(build=self.build, out=self.root / "out", version="authored", with_ice=enabled, test=test)
            with self.subTest(flavour=flavour, enabled=enabled, test=test), self.assertRaises(SystemExit):
                package.package(args)
            self.assertFalse(args.out.exists())

    def tool_run(self, args, **kwargs):
        tool = Path(args[0]).name
        if tool == "llvm-nm":
            text = "1 T enet_host_create\n1 T juice_create\n1 R ce_network_ice_feature_marker\n"
        elif tool == "readelf":
            text = "(RUNPATH) Library runpath: [$ORIGIN/lib]\n"
        elif tool == "llvm-readobj":
            text = ""
        else:
            raise AssertionError("unexpected external tool: " + tool)
        return subprocess.CompletedProcess(args, 0, text, "")

    def assembly_context(self):
        ff = self.root / "authored-ffmpeg-source"
        ff.write_bytes(b"authored FFmpeg source boundary\n")
        stack = contextlib.ExitStack()
        for callback in (patch.object(package.deps, "require_ffmpeg_shared"),
                         patch.object(package.deps, "ffmpeg_shared_files", return_value=[]),
                         patch.object(package.deps, "require_sdl_receipt"),
                         patch.object(package.deps, "require_sdl_link_receipt"),
                         patch.object(package.subprocess, "run", side_effect=self.tool_run),
                         patch.object(package, "strip", side_effect=lambda src, dst, windows: shutil.copyfile(src, dst)),
                         patch.object(package, "machine_paths", return_value=[]),
                         patch.object(package, "licences", return_value={"COPYING": b"authored GPL boundary\n"}),
                         patch.object(package, "notices", return_value=b"authored component notices\n"),
                         patch.object(package, "notice", return_value=b"authored CE notice\n"),
                         patch.object(package, "third_party", return_value=[]),
                         patch.object(package, "LINUX_LIBS", []), patch.object(package, "WINDOWS_DLLS", []),
                         patch.object(package, "source_epoch", return_value=1700000000),
                         patch.object(package, "glibc_needed", return_value=package.GLIBC),
                         patch.object(package, "sysroot_gaps", return_value=[]),
                         patch.object(package.deps, "lock", return_value={"FFmpeg": {"file": ff, "sha": sha(ff.read_bytes())}})):
            stack.enter_context(callback)
        return stack

    def test_both_real_archive_writers_include_exact_inputs_and_repeat_deterministically(self):
        outputs = []
        with self.assembly_context():
            for name in ("first", "second"):
                out = self.root / name
                args = SimpleNamespace(build=self.build, out=out, version="authored", with_ice=True, test=False)
                with contextlib.redirect_stdout(io.StringIO()):
                    package.package(args)
                archives = {p.name: p.read_bytes() for p in out.iterdir() if p.name.endswith((".zip", ".tar.gz"))}
                self.assertEqual(len(archives), 2)
                outputs.append(archives)
                for path in out.iterdir():
                    if path.name.endswith(".zip"):
                        with zipfile.ZipFile(path) as stream:
                            members = {key.split("/", 1)[1]: stream.read(key) for key in stream.namelist()}
                    elif path.name.endswith(".tar.gz"):
                        with tarfile.open(path) as stream:
                            members = {entry.name.split("/", 1)[1]: stream.extractfile(entry).read()
                                       for entry in stream.getmembers() if entry.isfile()}
                    else:
                        continue
                    self.assertEqual(members["licenses/libjuice-MPL-2.0.txt"], self.licence)
                    self.assertEqual(members["source/libjuice-relay-only.patch"], self.patch_bytes)
                    self.assertIn(self.source_tree_id.encode(), members["source/libjuice-APPLY.txt"])
                    self.assertFalse(json.loads(members["source/libjuice-PROVENANCE.json"])["source_unmodified"])
                    launcher = "onslaught-ce.exe" if path.name.endswith(".zip") else "onslaught-ce-launch"
                    self.assertIn(launcher, members)
                    self.assertNotIn("onslaught-ce-authored.exe", members)
                    self.assertNotIn("onslaught-ce-launch-authored", members)
                    self.assertTrue(members["README.txt"].startswith(b"Onslaught CE Vauthored\n"))
                    self.assertEqual(members["source/libjuice-authored-revision.tar.gz"], self.archive.read_bytes())
                    self.assertIn(b"source/libjuice-authored-revision.tar.gz", members["THIRD-PARTY-NOTICES.txt"])
                    self.assertIn(b"under either MPL-2.0 or GPL-3.0-only", members["ICE-SOURCE-NOTICE.txt"])
                    self.assertIn(b"ICE-enabled package", members["README.txt"])
                    self.assertIn(b"ICE mode may also contact", members["README.txt"])
                    self.assertNotIn(b"contacts no server", members["README.txt"])
        self.assertEqual(outputs[0], outputs[1])

    def test_opt_in_still_rejects_missing_tag_stripped_and_tool_error_inputs(self):
        with self.assembly_context(), patch.object(package, "NETWORK", True):
            for windows in (False, True):
                target = "windows" if windows else "linux"
                binary = self.build / target / self.names(target)[0]
                for error in (FileNotFoundError(), subprocess.CalledProcessError(1, "llvm-nm")):
                    with self.subTest(windows=windows, error=type(error).__name__), \
                            patch.object(package.subprocess, "run", side_effect=error):
                        with self.assertRaisesRegex(SystemExit, "cannot inspect"):
                            package.check_public(binary, windows, with_ice=True)
                with patch.object(package.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "", "")):
                    with self.assertRaisesRegex(SystemExit, "unstripped"):
                        package.check_public(binary, windows, with_ice=True)
                binary.write_bytes(b"authored non-ICE binary")
                with self.assertRaisesRegex(SystemExit, "feature tag"):
                    package.check_public(binary, windows, with_ice=True)

    def test_missing_binary_is_a_clean_refusal_before_output(self):
        with patch.object(package.deps, "require_ffmpeg_shared"):
            for windows in (False, True):
                with self.subTest(windows=windows), self.assertRaisesRegex(SystemExit, "candidate missing"):
                    package.check_public(self.root / "missing-candidate", windows)
        with patch.object(package.deps, "require_ffmpeg_shared", side_effect=SystemExit("authored FFmpeg shared input missing")):
            with self.assertRaisesRegex(SystemExit, "FFmpeg shared input missing"):
                package.check_public(self.root / "missing-candidate", True)
        self.assertFalse((self.work / "release").exists())

    def enable_native_fixture(self):
        for target, receipt in self.receipts.items():
            path = self.build / target / receipt["binaries"]["game"]["name"]
            path.write_bytes(path.read_bytes() + b"\n" + signaling.MARKER)
            receipt["binaries"]["game"] = ice_package.binary_identity(path)
            receipt["native_signaling"] = signaling.receipt_fields(target)
        self.save_receipts()

    def test_native_package_requires_explicit_mode_marker_and_receipt(self):
        with self.assertRaises(SystemExit):
            ice_package.prepare(self.build, with_native_signaling=True)
        self.enable_native_fixture()
        with self.assertRaises(SystemExit):
            ice_package.prepare(self.build)
        self.assertEqual({"linux", "windows"}, set(ice_package.prepare(self.build, with_native_signaling=True)))
        with self.assertRaises(SystemExit):
            package.package(SimpleNamespace(build=self.build, test=False, with_ice=False,
                                            with_native_signaling=True))

    def test_native_receipt_fields_are_exact_and_typed_for_each_target(self):
        self.enable_native_fixture()
        ice_package.prepare(self.build, with_native_signaling=True)
        for target in ("linux", "windows"):
            original = json.loads(json.dumps(self.receipts[target]))
            correct = original["native_signaling"]
            for value in (None, dict(correct, enabled=1), dict(correct, https_backend="different"),
                          dict(correct, marker="different"), dict(correct, extra=True)):
                with self.subTest(target=target, value_type=type(value).__name__):
                    self.receipts[target] = json.loads(json.dumps(original))
                    self.receipts[target]["native_signaling"] = value
                    self.save_receipts()
                    with self.assertRaises(SystemExit):
                        ice_package.prepare(self.build, with_native_signaling=True)
            self.receipts[target] = original
            self.save_receipts()

    def native_tool_run(self, args, **kwargs):
        result = self.tool_run(args, **kwargs)
        if "--coff-imports" in args:
            result.stdout += "\nName: WINHTTP.dll\n"
        elif args[:2] == ["readelf", "-d"]:
            result.stdout += "\n0x00000001 (NEEDED) Shared library: [libcurl-gnutls.so.4]\n"
        return result

    def test_native_backend_import_is_observed_for_both_targets(self):
        self.enable_native_fixture()
        with self.assembly_context(), patch.object(package, "NETWORK", True):
            for target in ("linux", "windows"):
                path = self.build / target / self.receipts[target]["binaries"]["game"]["name"]
                with self.subTest(target=target):
                    with self.assertRaises(SystemExit):
                        package.check_public(path, target == "windows", with_ice=True, with_native_signaling=True)
                    with patch.object(package.subprocess, "run", side_effect=self.native_tool_run):
                        package.check_public(path, target == "windows", with_ice=True, with_native_signaling=True)

    def test_native_archive_writers_include_exact_target_inputs_deterministically(self):
        self.enable_native_fixture()
        library = self.root / "native-sdk" / "libcurl-gnutls.so.4"
        library.parent.mkdir()
        library_data = b"authored native library fixture"
        library.write_bytes(library_data)
        source_name = "source/native-signaling/curl/curl-authored.tar.gz"
        licence_name = "licenses/native-signaling/curl-copyright.txt"
        source_data, licence_data = b"authored source fixture", b"authored notice fixture"
        payloads = {
            "linux": dict(files={source_name: source_data, licence_name: licence_data,
                                  "NATIVE-SIGNALING-SOURCE-NOTICE.txt": b"authored Linux source notice"},
                          libraries={"libcurl-gnutls.so.4": library_data},
                          paths={"libcurl-gnutls.so.4": library}, notice=b"\nAuthored Linux native notice.\n"),
            "windows": dict(files={"NATIVE-SIGNALING-SOURCE-NOTICE.txt": b"authored Windows source notice"},
                            libraries={}, paths={}, notice=b"\nAuthored Windows native notice.\n"),
        }
        records = []
        with self.assembly_context(), patch.object(signaling, "prepare", return_value=payloads), \
                patch.object(package.subprocess, "run", side_effect=self.native_tool_run), \
                patch.object(package, "glibc_needed", return_value=package.GLIBC) as floor, \
                patch.object(package, "sysroot_gaps", return_value={}) as gaps:
            for label in ("first", "second"):
                out = self.root / ("native-" + label)
                package.package(SimpleNamespace(version="9.8.7", out=out, build=self.build, test=False,
                                                with_ice=True, with_native_signaling=True))
                archives = sorted(p for p in out.iterdir() if p.name.endswith((".zip", ".tar.gz")))
                records.append({p.name: sha(p.read_bytes()) for p in archives})
                linux = next(p for p in archives if p.name.endswith(".tar.gz"))
                windows = next(p for p in archives if p.name.endswith(".zip"))
                with tarfile.open(linux) as tar:
                    names = tar.getnames()
                    for suffix, data in (("lib/libcurl-gnutls.so.4", library_data),
                                         (source_name, source_data), (licence_name, licence_data)):
                        name = next(n for n in names if n.endswith("/" + suffix))
                        self.assertEqual(data, tar.extractfile(name).read())
                with zipfile.ZipFile(windows) as zip_archive:
                    names = zip_archive.namelist()
                    self.assertTrue(any(n.endswith("/NATIVE-SIGNALING-SOURCE-NOTICE.txt") for n in names))
                    self.assertFalse(any(n.endswith("/libcurl-gnutls.so.4") or "/source/native-signaling/" in n for n in names))
            self.assertIn(library, floor.call_args.args[0])
            self.assertIn(library, gaps.call_args.args[0])
        self.assertEqual(records[0], records[1])


if __name__ == "__main__":
    unittest.main()
