#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the real dependency unpacker with tiny authored archives and real patches."""
import hashlib
import fcntl
import io
import json
import os
import tarfile
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

import deps


def sha(data):
    return hashlib.sha256(data).hexdigest()


def change(before, after):
    return (f"--- a/file.txt\n+++ b/file.txt\n@@ -1 +1 @@\n-{before}\n+{after}\n").encode()


class UnpackTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="bea-deps-tests-")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.port = self.root / "port"
        (self.port / "deps-patches").mkdir(parents=True)
        (self.port / "deps.lock").write_text("")
        self.port_patch = patch.object(deps, "PORT", self.port)
        self.port_patch.start()
        self.addCleanup(self.port_patch.stop)
        self.archive = self.root / "Demo-1.tar.gz"
        with tarfile.open(self.archive, "w:gz") as tar:
            data = b"original\n"
            item = tarfile.TarInfo("Demo-1/file.txt")
            item.size = len(data)
            tar.addfile(item, io.BytesIO(data))
        self.row = {"name": "Demo", "file": self.archive, "sha": sha(self.archive.read_bytes())}
        self.dest = self.root / "cache"
        self.stamp = self.dest / ".Demo-1.tar.gz.unpacked"

    def patch_file(self, name, data, pin=True):
        path = self.port / "deps-patches" / name
        path.write_bytes(data)
        if pin:
            with (self.port / "deps.lock").open("a") as stream:
                stream.write(f"patch:{name}\t{sha(data)}\n")
        return path

    def test_fresh_and_unchanged_cache_uses_exact_identity_without_rereading_archive(self):
        self.patch_file("Demo-one.patch", change("original", "first"))
        top = deps.unpack(self.row, self.dest)
        self.assertEqual("first\n", (top / "file.txt").read_text())
        saved = json.loads(self.stamp.read_text())
        self.assertEqual([["Demo-one.patch", sha(change("original", "first"))]], saved["patches"])
        with patch.object(deps.tarfile, "open", side_effect=AssertionError("cache hit reread archive")):
            self.assertEqual(top, deps.unpack(self.row, self.dest))

    def test_new_patch_refuses_existing_cache_and_fresh_cache_applies_it(self):
        old = deps.unpack(self.row, self.dest)
        stamp = self.stamp.read_bytes()
        self.patch_file("Demo-one.patch", change("original", "first"))
        with self.assertRaisesRegex(SystemExit, "patch set changed"):
            deps.unpack(self.row, self.dest)
        self.assertEqual("original\n", (old / "file.txt").read_text())
        self.assertEqual(stamp, self.stamp.read_bytes())
        fresh = deps.unpack(self.row, self.root / "fresh")
        self.assertEqual("first\n", (fresh / "file.txt").read_text())

    def test_ordered_patch_chain_is_bound_to_stamp(self):
        self.patch_file("Demo-b.patch", change("first", "second"))
        self.patch_file("Demo-a.patch", change("original", "first"))
        top = deps.unpack(self.row, self.dest)
        self.assertEqual("second\n", (top / "file.txt").read_text())
        self.assertEqual(["Demo-a.patch", "Demo-b.patch"],
                         [name for name, _ in json.loads(self.stamp.read_text())["patches"]])

    def test_changed_and_removed_patch_refuse_stale_cache(self):
        path = self.patch_file("Demo-one.patch", change("original", "first"))
        top = deps.unpack(self.row, self.dest)
        path.write_bytes(change("original", "second"))
        (self.port / "deps.lock").write_text(f"patch:{path.name}\t{sha(path.read_bytes())}\n")
        with self.assertRaisesRegex(SystemExit, "patch set changed"):
            deps.unpack(self.row, self.dest)
        self.assertEqual("first\n", (top / "file.txt").read_text())
        path.unlink()
        (self.port / "deps.lock").write_text("")
        with self.assertRaisesRegex(SystemExit, "patch set changed"):
            deps.unpack(self.row, self.dest)

    def test_patches_are_checked_even_on_cache_hit(self):
        path = self.patch_file("Demo-one.patch", change("original", "first"))
        top = deps.unpack(self.row, self.dest)
        path.write_bytes(change("original", "second"))
        with self.assertRaisesRegex(SystemExit, "SHA-256"):
            deps.unpack(self.row, self.dest)
        self.assertEqual("first\n", (top / "file.txt").read_text())

    def test_missing_pin_extra_pin_and_duplicate_pin_refused(self):
        path = self.patch_file("Demo-one.patch", change("original", "first"), pin=False)
        with self.assertRaisesRegex(SystemExit, "patches and deps.lock pins differ"):
            deps.unpack(self.row, self.dest)
        path.unlink()
        (self.port / "deps.lock").write_text(f"patch:Demo-one.patch\t{'a' * 64}\n")
        with self.assertRaisesRegex(SystemExit, "patches and deps.lock pins differ"):
            deps.unpack(self.row, self.dest)
        (self.port / "deps.lock").write_text(f"patch:Demo-one.patch\t{'a' * 64}\n" * 2)
        with self.assertRaisesRegex(SystemExit, "duplicate"):
            deps.unpack(self.row, self.dest)

    def test_old_or_invalid_stamp_is_never_trusted(self):
        top = self.dest / "Demo-1"
        top.mkdir(parents=True)
        (top / "file.txt").write_text("unknown\n")
        for value in ("Demo-1\n", "not json", json.dumps({"top": "../other"}), json.dumps({"top": ".."})):
            with self.subTest(value=value):
                self.stamp.write_text(value)
                with self.assertRaisesRegex(SystemExit, "legacy or invalid"):
                    deps.unpack(self.row, self.dest)
                self.assertEqual("unknown\n", (top / "file.txt").read_text())

    def test_unpinned_archive_is_refused_before_extraction(self):
        row = dict(self.row, sha="b" * 64)
        with self.assertRaisesRegex(SystemExit, "SHA-256.*deps.lock"):
            deps.unpack(row, self.dest)
        self.assertFalse((self.dest / "Demo-1").exists())
        self.assertFalse(self.stamp.exists())

    def test_symlink_stamps_and_receipt_destinations_do_not_write_targets(self):
        target = self.root / "preserve.txt"
        target.write_text("untouched\n")
        self.dest.mkdir()
        self.stamp.symlink_to(target)
        with self.assertRaisesRegex(SystemExit, "symlink cache metadata"):
            deps.unpack(self.row, self.dest)
        self.assertEqual("untouched\n", target.read_text())
        receipt = self.root / "receipt"
        receipt.symlink_to(target)
        with self.assertRaisesRegex(SystemExit, "symlink cache metadata"):
            deps.write_cache_file(receipt, b"replacement\n")
        with self.assertRaisesRegex(SystemExit, "symlink cache metadata"):
            deps.write_cache_if_changed(receipt, b"replacement\n")
        self.assertEqual("untouched\n", target.read_text())

    def test_fifo_and_oversized_metadata_are_refused_without_blocking(self):
        self.dest.mkdir()
        os.mkfifo(self.stamp)
        with self.assertRaisesRegex(SystemExit, "regular file"):
            deps.unpack(self.row, self.dest)
        with self.assertRaisesRegex(SystemExit, "non-regular"):
            deps.write_cache_file(self.stamp, b"replacement")
        with self.assertRaisesRegex(SystemExit, "regular file"):
            deps.write_cache_if_changed(self.stamp, b"replacement")
        large = self.root / "large"
        large.write_bytes(b"x" * 65537)
        with self.assertRaisesRegex(SystemExit, "65536"):
            deps.read_cache_file(large)

    def test_missing_directory_or_changed_archive_pin_is_refused(self):
        top = deps.unpack(self.row, self.dest)
        row = dict(self.row, sha="b" * 64)
        with self.assertRaisesRegex(SystemExit, "archive or patch set changed"):
            deps.unpack(row, self.dest)
        (top / "file.txt").unlink()
        top.rmdir()
        with self.assertRaisesRegex(SystemExit, "missing or invalid"):
            deps.unpack(self.row, self.dest)

    def test_partial_preexisting_directory_is_not_overwritten(self):
        top = self.dest / "Demo-1"
        top.mkdir(parents=True)
        (top / "file.txt").write_text("keep\n")
        with self.assertRaisesRegex(SystemExit, "without a verified stamp"):
            deps.unpack(self.row, self.dest)
        self.assertEqual("keep\n", (top / "file.txt").read_text())
        self.assertFalse(self.stamp.exists())

    def prepare_sdl(self):
        self.row["name"] = "SDL3"
        with tarfile.open(self.archive, "w:gz") as tar:
            for name, data in (("file.txt", b"original\n"), ("include/SDL3/SDL.h", b"header\n"),
                               ("include/SDL3/SDL_revision.h", b"source revision\n")):
                item = tarfile.TarInfo("Demo-1/" + name)
                item.size = len(data)
                tar.addfile(item, io.BytesIO(data))
        self.row["sha"] = sha(self.archive.read_bytes())
        self.patch_file("SDL3-one.patch", change("original", "first"))
        for name, value in (("SRC", self.dest), ("PREFIX", self.root / "linux-prefix"),
                            ("PREFIX_WIN", self.root / "windows-prefix"), ("BUILD", self.root / "linux-build"),
                            ("BUILD_WIN", self.root / "windows-build"), ("DATA", self.root / "isolated-data")):
            p = patch.object(deps, name, value)
            p.start()
            self.addCleanup(p.stop)
        p = patch.object(deps, "lock", return_value={"SDL3": self.row})
        p.start()
        self.addCleanup(p.stop)
        p = patch.object(deps, "sdl_cmake", return_value="cmake")
        p.start()
        self.addCleanup(p.stop)

    def run_sdl(self, target):
        cmake = deps.sdl_cmake()
        cm, env = deps.sdl_recipe(target, cmake)
        return deps.build_sdl(self.row, target, cm, cmake, env)

    def install_fixture(self, target):
        prefix = deps.sdl_prefix(target)
        files = (("lib/libSDL3.so", "lib/libSDL3.so.0") if target == "linux"
                 else ("bin/SDL3.dll", "lib/libSDL3.dll.a"))
        for name in files:
            path = prefix / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(("built " + target + " " + name).encode())
        headers = prefix / "include/SDL3"
        headers.mkdir(parents=True, exist_ok=True)
        (headers / "SDL.h").write_text("header\n")
        (headers / "SDL_revision.h").write_text("generated revision\n")

    def fake_build(self, target):
        def install(_cm, _cmake, name, source, _opts, _env):
            self.assertTrue(name.startswith("sdl3-" if target == "linux" else "win32-sdl3-"))
            self.assertEqual("first\n", (source / "file.txt").read_text())
            self.install_fixture(target)
            folder = (deps.BUILD if target == "linux" else deps.BUILD_WIN) / name
            folder.mkdir(parents=True, exist_ok=True)
            (folder / "CMakeCache.txt").write_text("SDL_WAYLAND:BOOL=ON\n")
            revision = folder / "include-revision/SDL3/SDL_revision.h"
            revision.parent.mkdir(parents=True, exist_ok=True)
            revision.write_text("generated revision\n")
            pairs = (("libSDL3.so", "lib/libSDL3.so"), ("libSDL3.so.0", "lib/libSDL3.so.0")) if target == "linux" else \
                    (("SDL3.dll", "bin/SDL3.dll"), ("libSDL3.dll.a", "lib/libSDL3.dll.a"))
            for built, installed in pairs:
                (folder / built).write_bytes((deps.sdl_prefix(target) / installed).read_bytes())
        with patch.object(deps, "cmake_project", side_effect=install):
            self.run_sdl(target)

    def test_legacy_unpatched_install_is_rejected_without_running_build(self):
        self.prepare_sdl()
        self.install_fixture("linux")
        top = self.dest / "Demo-1"
        top.mkdir(parents=True)
        (top / "file.txt").write_text("original\n")
        self.stamp.write_text("Demo-1\n")
        with self.assertRaisesRegex(SystemExit, "receipt missing or invalid"):
            deps.require_sdl_receipt("linux")
        with patch.object(deps, "cmake_project") as build:
            with self.assertRaisesRegex(SystemExit, "legacy or invalid"):
                self.run_sdl("linux")
            build.assert_not_called()
        self.assertEqual("original\n", (top / "file.txt").read_text())

    def test_installed_receipt_binds_patched_source_and_both_target_artifacts(self):
        self.prepare_sdl()
        for target in ("linux", "windows"):
            self.fake_build(target)
            with deps.sdl_install_lock(target):
                deps.require_sdl_receipt(target)
        (deps.PREFIX / "lib/libSDL3.so.0").write_bytes(b"unpatched library")
        with self.assertRaisesRegex(SystemExit, "artifacts changed"):
            deps.require_sdl_receipt("linux")
        deps.require_sdl_receipt("windows")
        (deps.PREFIX_WIN / "bin/SDL3.dll").write_bytes(b"unpatched library")
        with self.assertRaisesRegex(SystemExit, "artifacts changed"):
            deps.require_sdl_receipt("windows")

    def test_patch_and_source_drift_invalidate_the_installed_receipt(self):
        self.prepare_sdl()
        self.fake_build("linux")
        (self.dest / "Demo-1/file.txt").write_text("tampered\n")
        with self.assertRaisesRegex(SystemExit, "source changed"):
            deps.require_sdl_receipt("linux")
        (self.dest / "Demo-1/file.txt").write_text("first\n")
        deps.require_sdl_receipt("linux")
        path = self.port / "deps-patches/SDL3-one.patch"
        path.write_bytes(change("original", "second"))
        (self.port / "deps.lock").write_text(f"patch:{path.name}\t{sha(path.read_bytes())}\n")
        with self.assertRaisesRegex(SystemExit, "patch set changed"):
            deps.require_sdl_receipt("linux")

    def test_failed_rebuild_invalidates_previous_receipt(self):
        self.prepare_sdl()
        self.fake_build("linux")
        deps.require_sdl_receipt("linux")
        with patch.object(deps, "cmake_project", side_effect=RuntimeError("install failed")):
            with self.assertRaisesRegex(RuntimeError, "install failed"):
                self.run_sdl("linux")
        with self.assertRaisesRegex(SystemExit, "stale or its artifacts changed"):
            deps.require_sdl_receipt("linux")
        with patch.object(deps, "cmake_project") as build:
            with self.assertRaisesRegex(SystemExit, "stale or its artifacts changed"):
                self.run_sdl("linux")
            build.assert_not_called()

    def test_repeat_build_rejects_modified_cmake_output_before_install(self):
        self.prepare_sdl()
        self.fake_build("linux")
        folder = next(deps.BUILD.iterdir())
        (folder / "libSDL3.so").write_bytes(b"altered build output")
        with patch.object(deps, "cmake_project") as build:
            with self.assertRaisesRegex(SystemExit, "differs from the just-built output"):
                self.run_sdl("linux")
            build.assert_not_called()
        # The previously verified install was not modified by the refused rebuild.
        deps.require_sdl_receipt("linux")

    def test_verified_repeat_build_is_a_valid_noop(self):
        self.prepare_sdl()
        self.fake_build("linux")
        receipt = deps.PREFIX / ".bea-sdl3-build.json"
        first = receipt.read_bytes()
        self.fake_build("linux")
        self.assertEqual(first, receipt.read_bytes())
        deps.require_sdl_receipt("linux")

    def test_wayland_off_or_changed_compiler_environment_cannot_issue_a_receipt(self):
        self.prepare_sdl()
        cmake = deps.sdl_cmake()
        cm, env = deps.sdl_recipe("linux", cmake)
        for changed_cm, changed_env in ((cm + ["-DSDL_WAYLAND=OFF"], env),
                                        (cm, dict(env, CFLAGS=env["CFLAGS"] + " -DSDL_WAYLAND=0")),
                                        (cm, dict(env, CC="/usr/bin/cc"))):
            with self.subTest(command=changed_cm, cc=changed_env.get("CC")), patch.object(deps, "cmake_project") as build:
                with self.assertRaisesRegex(SystemExit, "differs from the production recipe"):
                    deps.build_sdl(self.row, "linux", changed_cm, cmake, changed_env)
                build.assert_not_called()
        self.assertFalse(self.dest.exists())
        self.assertFalse(deps.PREFIX.exists())

    def test_changed_cmake_wayland_cache_is_rejected_before_noop_rebuild_or_package(self):
        self.prepare_sdl()
        self.fake_build("linux")
        deps.require_sdl_receipt("linux")
        folder = next(deps.BUILD.iterdir())
        (folder / "CMakeCache.txt").write_text("SDL_WAYLAND:BOOL=OFF\n")
        with self.assertRaisesRegex(SystemExit, "stale or its artifacts changed"):
            deps.require_sdl_receipt("linux")
        with patch.object(deps, "cmake_project") as build:
            with self.assertRaisesRegex(SystemExit, "stale or its artifacts changed"):
                self.run_sdl("linux")
            build.assert_not_called()

    def test_effective_environment_change_invalidates_receipt_without_saving_secrets(self):
        self.prepare_sdl()
        secret = "authored-secret-value-not-a-credential"
        with patch.dict(os.environ, {"BEA_AUTHORED_SECRET": secret, "CMAKE_PREFIX_PATH": "/untrusted"}):
            _, env = deps.sdl_recipe("linux", deps.sdl_cmake())
            self.assertNotIn("BEA_AUTHORED_SECRET", env)
            self.assertNotIn("CMAKE_PREFIX_PATH", env)
            self.fake_build("linux")
            receipt = (deps.PREFIX / ".bea-sdl3-build.json").read_text()
            self.assertNotIn(secret, receipt)
            deps.require_sdl_receipt("linux")
            with patch.dict(os.environ, {"PATH": os.environ["PATH"] + ":/different-tool-search"}):
                with self.assertRaisesRegex(SystemExit, "stale or its artifacts changed"):
                    deps.require_sdl_receipt("linux")
            deps.require_sdl_receipt("linux")

    def test_equivalent_clean_data_roots_have_identical_configuration_hashes(self):
        self.prepare_sdl()
        hashes = []
        for root in (self.root / "first", self.root / "second"):
            with patch.object(deps, "DATA", root), patch.object(deps, "SRC", root / "src"), \
                 patch.object(deps, "BUILD", root / "build-sniper"), \
                 patch.object(deps, "PREFIX", root / "prefix-sniper"), \
                 patch.object(deps, "SYSROOT", root / "sysroot-sniper"), \
                 patch.object(deps, "LLD", root / "llvm-mingw/bin/ld.lld"):
                hashes.append(deps.sdl_config_sha256("linux"))
        self.assertEqual(hashes[0], hashes[1])

    def test_similar_sibling_paths_are_not_normalized_as_the_data_root(self):
        self.prepare_sdl()
        hashes = []
        for root in (self.root / "first", self.root / "second"):
            with patch.object(deps, "DATA", root), patch.object(deps, "SRC", root / "src"), \
                 patch.object(deps, "BUILD", root / "build-sniper"), patch.object(deps, "PREFIX", root / "prefix-sniper"), \
                 patch.object(deps, "SYSROOT", root / "sysroot-sniper"), \
                 patch.object(deps, "LLD", root / "llvm-mingw/bin/ld.lld"), \
                 patch.dict(os.environ, {"PATH": str(root) + "-sibling/bin:/usr/bin"}):
                hashes.append(deps.sdl_config_sha256("linux"))
        self.assertNotEqual(hashes[0], hashes[1])

    def test_symlink_install_or_build_leaf_is_refused(self):
        self.prepare_sdl()
        preserve = self.root / "preserve"
        preserve.mkdir()
        (preserve / "important").write_text("keep\n")
        deps.PREFIX.symlink_to(preserve, target_is_directory=True)
        with patch.object(deps, "cmake_project") as build:
            with self.assertRaisesRegex(SystemExit, "symlink SDL install"):
                self.run_sdl("linux")
            build.assert_not_called()
        self.assertEqual("keep\n", (preserve / "important").read_text())
        deps.PREFIX.unlink()
        deps.unpack(self.row, deps.SRC)
        key = hashlib.sha256(json.dumps(deps.sdl_expected("linux"), sort_keys=True).encode()).hexdigest()[:16]
        deps.BUILD.mkdir(parents=True)
        (deps.BUILD / ("sdl3-" + key)).symlink_to(preserve, target_is_directory=True)
        with patch.object(deps, "cmake_project") as build:
            with self.assertRaisesRegex(SystemExit, "symlink SDL build"):
                self.run_sdl("linux")
            build.assert_not_called()
        self.assertEqual("keep\n", (preserve / "important").read_text())

    def test_install_cannot_receipt_a_different_existing_binary(self):
        self.prepare_sdl()
        def install(_cm, _cmake, name, _source, _opts, _env):
            self.install_fixture("linux")
            folder = deps.BUILD / name
            folder.mkdir(parents=True)
            (folder / "CMakeCache.txt").write_text("SDL_WAYLAND:BOOL=ON\n")
            (folder / "libSDL3.so").write_bytes(b"different newly built bytes")
            (folder / "libSDL3.so.0").write_bytes(b"different newly built bytes")
        with patch.object(deps, "cmake_project", side_effect=install):
            with self.assertRaisesRegex(SystemExit, "differs from the just-built output"):
                self.run_sdl("linux")
        with self.assertRaisesRegex(SystemExit, "stale or its artifacts changed"):
            deps.require_sdl_receipt("linux")

    def test_install_cannot_receipt_stale_headers(self):
        self.prepare_sdl()
        def install(_cm, _cmake, name, _source, _opts, _env):
            self.install_fixture("linux")
            folder = deps.BUILD / name
            folder.mkdir(parents=True)
            (folder / "CMakeCache.txt").write_text("SDL_WAYLAND:BOOL=ON\n")
            for built, installed in (("libSDL3.so", "lib/libSDL3.so"), ("libSDL3.so.0", "lib/libSDL3.so.0")):
                (folder / built).write_bytes((deps.PREFIX / installed).read_bytes())
            revision = folder / "include-revision/SDL3/SDL_revision.h"
            revision.parent.mkdir(parents=True)
            revision.write_text("generated revision\n")
            (deps.PREFIX / "include/SDL3/SDL.h").write_text("stale!\n")
        with patch.object(deps, "cmake_project", side_effect=install):
            with self.assertRaisesRegex(SystemExit, "headers differ"):
                self.run_sdl("linux")
        with self.assertRaisesRegex(SystemExit, "stale or its artifacts changed"):
            deps.require_sdl_receipt("linux")

    def test_install_cannot_receipt_an_obsolete_extra_header(self):
        self.prepare_sdl()
        folder = deps.PREFIX / "include/SDL3"
        folder.mkdir(parents=True)
        (folder / "old-header.h").write_text("obsolete\n")
        with self.assertRaisesRegex(SystemExit, "headers differ"):
            self.fake_build("linux")
        with self.assertRaisesRegex(SystemExit, "stale or its artifacts changed"):
            deps.require_sdl_receipt("linux")

    def test_installed_headers_are_bound_to_receipt(self):
        self.prepare_sdl()
        self.fake_build("windows")
        (deps.PREFIX_WIN / "include/SDL3/SDL.h").write_text("modified header\n")
        with self.assertRaisesRegex(SystemExit, "artifacts changed"):
            deps.require_sdl_receipt("windows")

    def test_shared_install_lock_allows_nested_readers_and_blocks_an_installer(self):
        self.prepare_sdl()
        lock = self.root / ".linux-prefix-sdl3.lock"
        with deps.sdl_install_lock("linux"):
            with deps.sdl_install_lock("linux"):
                with lock.open("a") as stream:
                    with self.assertRaises(BlockingIOError):
                        fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
        with lock.open("a") as stream:
            fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)

    def test_link_receipt_rejects_old_game_and_changed_sdl(self):
        self.prepare_sdl()
        self.fake_build("linux")
        folder = self.root / "build/linux"
        folder.mkdir(parents=True)
        (folder / "bea").write_bytes(b"built game")
        (folder / "onslaught-ce-launch").write_bytes(b"built launcher")
        receipt = folder / ".bea-sdl3-link.json"
        receipt.write_text(json.dumps(deps.sdl_link_receipt(self.root / "build", "linux")))
        deps.require_sdl_link_receipt(self.root / "build", "linux")
        (folder / "bea").write_bytes(b"old game")
        with self.assertRaisesRegex(SystemExit, "linked programs changed"):
            deps.require_sdl_link_receipt(self.root / "build", "linux")
        (folder / "bea").write_bytes(b"built game")
        install_receipt = deps.PREFIX / ".bea-sdl3-build.json"
        install_receipt.write_text(install_receipt.read_text() + " ")
        with self.assertRaisesRegex(SystemExit, "install or linked programs changed"):
            deps.require_sdl_link_receipt(self.root / "build", "linux")

    def test_packager_refuses_unverified_sdl_before_writing_any_output(self):
        import package
        out = self.root / "out"
        args = SimpleNamespace(version="0.1.10", out=out, test=False, build=self.root / "build")
        with patch.object(package, "check_public"), \
             patch.object(deps, "require_sdl_receipt", side_effect=SystemExit("unverified SDL")) as require:
            with self.assertRaisesRegex(SystemExit, "unverified SDL"):
                package.package(args)
            require.assert_called_once_with("linux")
        self.assertFalse(out.exists())

    def test_packager_refuses_real_stale_prefix_or_link_receipts(self):
        import package
        self.prepare_sdl()
        build_root = self.root / "build"
        for target in ("linux", "windows"):
            self.fake_build(target)
            folder = build_root / target
            folder.mkdir(parents=True)
            names = ("bea", "onslaught-ce-launch") if target == "linux" else ("bea.exe", "onslaught-ce.exe")
            for name in names:
                (folder / name).write_bytes((target + " " + name).encode())
            (folder / ".bea-sdl3-link.json").write_text(json.dumps(deps.sdl_link_receipt(build_root, target)))
        out = self.root / "out"
        args = SimpleNamespace(version="0.1.10", out=out, test=False, build=build_root)
        sdl = deps.PREFIX / "lib/libSDL3.so.0"
        original = sdl.read_bytes()
        sdl.write_bytes(b"old unpatched install")
        with patch.object(package, "check_public"):
            with self.assertRaisesRegex(SystemExit, "artifacts changed"):
                package.package(args)
        self.assertFalse(out.exists())
        sdl.write_bytes(original)
        (build_root / "linux/bea").write_bytes(b"old game")
        with patch.object(package, "check_public"):
            with self.assertRaisesRegex(SystemExit, "linked programs changed"):
                package.package(args)
        self.assertFalse(out.exists())


if __name__ == "__main__":
    unittest.main()
