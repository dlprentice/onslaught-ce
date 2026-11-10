#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Authored source-profile fixtures: no compiler, game data, downloads or build-module imports."""
from __future__ import annotations

import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import network_source_id as subject

ROOT = Path(__file__).absolute().parents[2]
BUILD = b'''PUBLIC_REPLACEMENTS = ("SDK.cpp", "SDK.h")
PUBLIC_SKIPPED_PATCHES = {"private-sdk.patch": "SDK.cpp"}
NV_FRAGMENTS_SIZE = 0x20 - 0x10
'''


class Profile(unittest.TestCase):
    def setUp(self):
        scratch = ROOT / "local-data/source-id-tests"
        scratch.mkdir(parents=True, exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(prefix="case-", dir=scratch)
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name) / "working"
        self.root.mkdir()
        self.git("init", "-q")
        for name in subject.RECIPES:
            self.write(name, b"# authored recipe sentinel\n")
        self.write("port/build.py", BUILD)
        self.write("port/tools/network_source_id.py", Path(subject.__file__).read_bytes())
        self.write("config/game-data.sha256", hashlib.sha256(b"not read").hexdigest().encode() + b"  data/fixture.inc\n")
        self.write("src/Game.cpp", b'#include "Game.h"\n#include "data/fixture.inc"\nint game() {return 7;}\n')
        self.write("src/Game.h", b"int game();\n")
        self.write("src/SDK.cpp", b"private superseded sentinel\n")
        self.write("src/SDK.h", b"private superseded header\n")
        self.write("src/splinebasis.c", b"private nonpublic data sentinel\n")
        self.write("linklib/nvtristrip_vc6.h", b"private nonpublic unused header\n")
        self.write("linklib/aliases.tsv", b"alias\ttarget\n")
        self.write("port/platform/public/SDK.cpp", b'#include "SDK.h"\nint sdk() {return 3;}\n')
        self.write("port/platform/public/SDK.h", b"int sdk();\n")
        self.write("port/platform/public/nvtristrip_public.cpp", b"int strip() {return 0;}\n")
        self.write("port/platform/common/nvtristrip_bridge.cpp", b"private nonpublic bridge sentinel\n")
        self.write("port/platform/common/common.h", b"int common();\n")
        self.write("port/platform/linux/pc.cpp", b'#include "../common/common.h"\nint linux_port=1;\n')
        self.write("port/platform/windows/pc.cpp", b"int windows_port=1;\n")
        self.write("port/platform/windows/utf8.manifest", b"<manifest/>\n")
        self.write("port/platform/windows/utf8.rc", b'1 24 "utf8.manifest"\n')
        self.write("port/game/production.cpp", b"int port_game=1;\n")
        self.write("port/game/production.h", b"int port();\n")
        self.write("port/network/protocol.cpp", b"int protocol=1;\n")
        self.write("port/d3dx/flags.txt", b"matrix.cpp: -O2\n")
        self.write("port/patches/production.patch", b"--- a/Game.cpp\n+++ b/Game.cpp\n@@ -1 +1 @@\n-int x;\n+int y;\n")
        self.write("port/patches/private-sdk.patch", b"not applied public\n")
        self.write("port/deps-patches/fixture.patch", b"dependency patch sentinel\n")
        self.write("README.md", b"unrelated docs\n")
        self.write("port/tools/tests/unrelated.cpp", b"unrelated test\n")
        self.git("add", ".")

    def write(self, name, data):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def git(self, *args, data=None):
        return subprocess.run(["git", "-C", str(self.root), *args], input=data,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True).stdout

    def identity(self):
        return subject.compute_source_id(self.root)

    def refuses(self, message=None):
        with self.assertRaises(subject.SourceIDError) as caught:
            self.identity()
        if message:
            self.assertIn(message, str(caught.exception))

    def test_transcript_independent_golden_and_utf8_byte_order(self):
        entries = {"z": b"A", "a": b"B\0C", "é": b"D"}
        packed = b"Onslaught CE declared-source compatibility v1\0public-i686-linux-and-windows-v1\0"
        packed += (3).to_bytes(8, "big")
        for path, payload in [("a", b"B\0C"), ("z", b"A"), ("é", b"D")]:
            name = path.encode("utf-8")
            packed += len(name).to_bytes(8, "big") + name + len(payload).to_bytes(8, "big") + hashlib.sha256(payload).digest()
        expected = hashlib.sha256(packed).hexdigest()
        self.assertEqual(expected, "16b428a1dbfb0d35f1d7e4db1dc14317503120f2d954b396eb8ab474b8f87f5f")
        self.assertEqual(subject.digest_entries(entries), expected)

    def test_transcript_frames_path_length_payload_length_and_count(self):
        values = [{"a": b"bc"}, {"ab": b"c"}, {"a": b"b", "c": b""}, {"a": b"bc\0"}, {}]
        self.assertEqual(len({subject.digest_entries(e) for e in values}), len(values))

    def test_repeated_call_is_stable_and_lowercase_sha256(self):
        first = self.identity()
        self.assertRegex(first, r"^[0-9a-f]{64}$")
        self.assertEqual(first, self.identity())

    def test_optional_ice_requires_each_indexed_recipe(self):
        for name in subject.ICE_RECIPES:
            with self.assertRaisesRegex(subject.SourceIDError, "required recipe is not indexed"):
                subject.compute_source_id(self.root, with_ice=True)
            self.write(name, b"# authored optional recipe\n")
            self.git("add", name)
        self.assertRegex(subject.compute_source_id(self.root, with_ice=True), r"^[0-9a-f]{64}$")

    def test_ice_profile_binds_each_optional_input_without_changing_default_selection(self):
        default = self.identity()
        for name in subject.ICE_RECIPES:
            self.write(name, b"# authored optional recipe\n")
            self.git("add", name)
        ice = subject.compute_source_id(self.root, with_ice=True)
        self.assertNotEqual(default, ice)
        self.assertEqual(default, self.identity())
        for name in subject.ICE_RECIPES:
            self.write(name, b"# changed optional recipe\n")
            changed = subject.compute_source_id(self.root, with_ice=True)
            self.assertNotEqual(ice, changed)
            self.assertEqual(default, self.identity())
            ice = changed

    def test_optional_ice_pin_cannot_be_a_symlink(self):
        for name in subject.ICE_RECIPES:
            self.write(name, b"# authored optional recipe\n")
            self.git("add", name)
        target = self.root / subject.ICE_RECIPES[-1]
        target.unlink()
        target.symlink_to(self.root / "README.md")
        with self.assertRaises(subject.SourceIDError):
            subject.compute_source_id(self.root, with_ice=True)

    def test_private_and_history_independent_public_effective_tree_equal(self):
        self.git("-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid", "commit", "-qm", "private fixture")
        original = self.identity()
        for name in ("src/SDK.cpp", "src/SDK.h", *subject.PUBLIC_OMISSIONS, "port/patches/private-sdk.patch"):
            self.git("rm", "-q", name)
        self.git("-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid", "commit", "-qm", "public fixture")
        self.assertEqual(original, self.identity())
        self.write("README.md", b"new unrelated commit\n")
        self.git("add", "README.md")
        self.git("-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid", "commit", "-qm", "different history")
        self.assertEqual(original, self.identity())

    def test_working_content_not_index_blob_is_hashed(self):
        first = self.identity()
        self.write("src/Game.cpp", b"int game() {return 8;}\n")
        self.assertNotEqual(first, self.identity())

    def test_actual_production_input_categories_change_id(self):
        first = self.identity()
        for name in ("src/Game.cpp", "src/Game.h", "port/platform/public/SDK.cpp", "port/platform/public/SDK.h",
                     "port/platform/linux/pc.cpp", "port/platform/windows/pc.cpp", "port/network/protocol.cpp",
                     "port/platform/windows/utf8.manifest", "port/platform/windows/utf8.rc", "port/d3dx/flags.txt",
                     "port/patches/production.patch", "port/deps-patches/fixture.patch", "linklib/aliases.tsv",
                     "port/aliases.tsv", "port/tools/deps.py", "port/tools/qifist.py", "port/vorbis/build.py",
                     "port/tools/network_source_id.py", "config/toolchain.sha256", "config/vc6-include.sha256", "port/deps.lock"):
            with self.subTest(name=name):
                old = (self.root / name).read_bytes()
                self.write(name, old + b"\n# changed input\n")
                self.assertNotEqual(first, self.identity())
                self.write(name, old)

    def test_game_data_pin_change_changes_id_without_reading_data(self):
        first = self.identity()
        self.write("config/game-data.sha256", b"1" * 64 + b"  data/fixture.inc\n")
        self.assertNotEqual(first, self.identity())
        self.assertFalse((self.root / "src/data").exists())

    def test_native_network_bridge_layout_recipe_is_bound(self):
        self.write("port/network/flags.txt", b"native_posix_stat.cpp: -mno-ms-bitfields\n")
        self.git("add", "port/network/flags.txt")
        native = self.identity()
        self.write("port/network/flags.txt", b"native_posix_stat.cpp: -mms-bitfields\n")
        self.assertNotEqual(native, self.identity())
        self.write("port/network/flags.txt", b"native_posix_stat.cpp: -mno-ms-bitfields\n")
        self.assertEqual(native, self.identity())

    def test_nv_generator_size_and_build_flags_are_bound(self):
        first = self.identity()
        self.write("port/build.py", BUILD.replace(b"0x20", b"0x30") + b"COMMON=['-O2']\n")
        self.assertNotEqual(first, self.identity())
        self.assertIn("@generated/data/NVShaderFragments.inc", subject.source_entries(self.root))

    def test_docs_tests_outputs_and_unused_private_inputs_do_not_change_id(self):
        first = self.identity()
        for name in ("README.md", "port/tools/tests/unrelated.cpp", "src/SDK.cpp", "src/SDK.h",
                     *subject.PUBLIC_OMISSIONS, "port/platform/common/nvtristrip_bridge.cpp",
                     "port/patches/private-sdk.patch", "local-data/build/arbitrary.bin"):
            self.write(name, b"different nonproduction sentinel\n")
        self.assertEqual(first, self.identity())

    def test_new_tracked_both_target_sources_are_bound(self):
        first = self.identity()
        for target in ("linux", "windows"):
            name = f"port/platform/{target}/added.cpp"
            self.write(name, b"int added() {return 2;}\n")
            self.git("add", name)
            second = self.identity()
            self.assertNotEqual(first, second)
            first = second

    def test_new_production_module_needs_no_handmaintained_root_list(self):
        first = self.identity()
        for module in ("new-module", "tools-extra"):
            name = f"port/{module}/core.cpp"
            self.write(name, b"int new_module;\n")
            self.refuses("untracked production input")
            self.git("add", name)
            second = self.identity()
            self.assertNotEqual(first, second)
            first = second

    def test_local_literal_include_binds_tracked_nonstandard_extension(self):
        first = self.identity()
        self.write("port/game/production.cpp", b'#include "../tools/source-body.def"\n')
        self.write("port/tools/source-body.def", b"int custom_body;\n")
        self.git("add", "port/tools/source-body.def")
        with_include = self.identity()
        self.assertNotEqual(first, with_include)
        self.write("port/tools/source-body.def", b"int changed_body;\n")
        self.assertNotEqual(with_include, self.identity())

    def test_added_patch_include_is_also_closed(self):
        self.write("port/patches/production.patch", b'--- a/Game.cpp\n+++ b/Game.cpp\n@@ -1 +1 @@\n-int x;\n+#include "../port/tools/private.def"\n')
        self.write("port/tools/private.def", b"not indexed\n")
        self.refuses("untracked local include")
        self.git("add", "port/tools/private.def")
        before = self.identity()
        self.write("port/tools/private.def", b"changed indexed include\n")
        self.assertNotEqual(before, self.identity())

    def test_untracked_port_cpp_refuses_even_when_git_ignored(self):
        self.write(".gitignore", b"port/game/hidden.cpp\n")
        self.write("port/game/hidden.cpp", b"int hidden;\n")
        self.refuses("untracked production input")

    def test_untracked_header_refuses(self):
        self.write("port/include/untracked.h", b"int hidden;\n")
        self.refuses("untracked production input")

    def test_unknown_unpinned_generated_include_refuses(self):
        self.write("src/Game.cpp", b'#include "data/not-pinned.inc"\n')
        self.refuses("unpinned generated include")

    def test_patch_unpinned_generated_include_refuses(self):
        self.write("port/patches/production.patch", b'--- a/Game.cpp\n+++ b/Game.cpp\n@@ -1 +1 @@\n-int x;\n+#include "data/unknown.inc"\n')
        self.refuses("unpinned generated include")

    def test_nested_relative_generated_include_requires_pin(self):
        self.write("src/Nested/body.cpp", b'#include "../data/unknown.inc"\nint nested;\n')
        self.git("add", "src/Nested/body.cpp")
        self.refuses("unpinned generated include")
        self.write("src/Nested/body.cpp", b'#include "../data/fixture.inc"\nint nested;\n')
        self.assertRegex(self.identity(), r"^[0-9a-f]{64}$")

    def test_missing_tracked_source_refuses(self):
        (self.root / "src/Game.h").unlink()
        self.refuses("input unavailable")

    def test_missing_required_recipe_or_replacement_refuses(self):
        for name in ("port/deps.lock", "port/platform/public/SDK.cpp"):
            with self.subTest(name=name):
                old = (self.root / name).read_bytes()
                (self.root / name).unlink()
                self.refuses()
                self.write(name, old)

    def test_unindexed_required_recipe_refuses(self):
        self.git("rm", "--cached", "-q", "port/tools/qifist.py")
        self.refuses("recipe is not indexed")

    def test_source_symlink_refuses_even_same_content(self):
        path = self.root / "src/Game.h"
        old = path.read_bytes()
        path.unlink()
        twin = self.write("local-data/twin.h", old)
        path.symlink_to(twin)
        self.refuses("symlink")

    def test_production_directory_symlink_refuses(self):
        folder = self.root / "port/network"
        other = self.root / "other-network"
        folder.rename(other)
        folder.symlink_to(other, target_is_directory=True)
        self.refuses("symlink")

    def test_source_root_symlink_refuses(self):
        alias = self.root.parent / "alias"
        alias.symlink_to(self.root, target_is_directory=True)
        with self.assertRaises(subject.SourceIDError):
            subject.compute_source_id(alias)

    def test_nonregular_selected_input_refuses(self):
        path = self.root / "src/Game.h"
        path.unlink()
        path.mkdir()
        self.refuses("nonregular")

    def test_case_colliding_index_refuses(self):
        self.write("src/game.h", b"case alias\n")
        self.git("add", "src/game.h")
        self.refuses("ambiguous source index")

    def test_data_pin_duplicates_and_malformed_pins_refuse(self):
        for blob in (b"", b"bad  data/fixture.inc\n", b"0" * 64 + b"  data/fixture.inc\n" + b"1" * 64 + b"  DATA/FIXTURE.INC\n",
                     b"0" * 64 + b"  ../outside\n"):
            self.write("config/game-data.sha256", blob)
            self.refuses()

    def test_nonliteral_or_invalid_build_settings_refuse(self):
        for blob in (BUILD.replace(b'("SDK.cpp", "SDK.h")', b"dynamic()"),
                     BUILD.replace(b"0x20 - 0x10", b"15"),
                     BUILD + b"PUBLIC_REPLACEMENTS = ('other.cpp',)\n"):
            self.write("port/build.py", blob)
            self.refuses()

    def test_native_nv_range_keeps_the_builds_partial_group_policy(self):
        self.write("port/build.py", BUILD.replace(b"0x20 - 0x10", b"0x63C108 - 0x634550"))
        generated = subject.source_entries(self.root)["@generated/data/NVShaderFragments.inc"]
        self.assertEqual(generated.count(b"0x00"), ((0x63C108 - 0x634550) // 16) * 16)

    def test_index_change_mid_snapshot_refuses(self):
        real = subject._index
        calls = 0
        def changed(root):
            nonlocal calls
            calls += 1
            return real(root) if calls == 1 else frozenset()
        with patch.object(subject, "_index", side_effect=changed):
            self.refuses("index changed")

    def test_source_change_after_initial_read_refuses(self):
        real = subject._read
        calls = 0
        def changed(root, name):
            nonlocal calls
            value = real(root, name)
            if name == "src/Game.cpp":
                calls += 1
                if calls > 1:
                    return value + b"changed"
            return value
        with patch.object(subject, "_read", side_effect=changed):
            self.refuses("source changed")

    def test_recipe_change_after_initial_read_refuses(self):
        real = subject._read
        calls = 0
        def changed(root, name):
            nonlocal calls
            value = real(root, name)
            if name == "port/tools/qifist.py":
                calls += 1
                if calls > 1:
                    return value + b"changed"
            return value
        with patch.object(subject, "_read", side_effect=changed):
            self.refuses("recipe changed")

    def test_fifo_selected_input_refuses_without_blocking(self):
        path = self.root / "src/Game.h"
        path.unlink()
        os.mkfifo(path)
        self.refuses("nonregular")

    def test_missing_console_only_header_is_not_a_compile_or_equivalence_claim(self):
        self.write("src/Game.cpp", b'#if TARGET == PS2\n#include "PS2Only.h"\n#endif\nint x;\n')
        self.assertRegex(self.identity(), r"^[0-9a-f]{64}$")

    def test_absolute_literal_include_refuses(self):
        self.write("src/Game.cpp", b'#include "/outside/source.h"\n')
        self.refuses("absolute include")

    def test_include_of_omitted_input_refuses(self):
        self.write("src/Game.cpp", b'#include "../linklib/nvtristrip_vc6.h"\n')
        self.refuses("nonproduction input")

    def test_git_environment_cannot_redirect_explicit_root(self):
        first = self.identity()
        with patch.dict(os.environ, {"GIT_INDEX_FILE": "/not-an-index", "GIT_DIR": "/not-a-repo",
                                     "GIT_WORK_TREE": "/not-a-tree", "BEA_GAME_DIR": "/not-game",
                                     "BEA_PORT_SYSROOT": "host", "BEA_NETWORK_SIMULATION_ID": "caller-label"}):
            self.assertEqual(first, self.identity())

    def test_cli_success_has_only_digest_and_failure_has_no_host_paths(self):
        helper = Path(subject.__file__).absolute()
        result = subprocess.run([sys.executable, str(helper), str(self.root)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, self.identity() + "\n")
        (self.root / "src/Game.h").unlink()
        result = subprocess.run([sys.executable, str(helper), str(self.root)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertFalse(result.stdout)
        self.assertNotIn(str(self.root), result.stderr)
        self.assertIn("refused", result.stderr)


if __name__ == "__main__":
    unittest.main()
