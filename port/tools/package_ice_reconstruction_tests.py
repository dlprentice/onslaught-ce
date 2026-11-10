#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Reconstruct authored source archives and patches without running package or build tools."""
import ast
import difflib
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import tarfile
import tempfile
import unittest


TOOLS = Path(__file__).resolve().parent


def isolated_function(filename, name):
    """Load just the source-command emitter/hash, without importing build/package tools."""
    source = TOOLS / filename
    node = next(node for node in ast.parse(source.read_text()).body
                if isinstance(node, ast.FunctionDef) and node.name == name)
    namespace = dict(hashlib=hashlib, json=json, os=os, stat=stat)
    exec(compile(ast.Module(body=[node], type_ignores=[]), str(source), "exec"), namespace)
    return namespace[name]


class SourceReconstructionTests(unittest.TestCase):
    def test_emitted_commands_reconstruct_contents_and_modes_under_both_umasks(self):
        commands = isolated_function("package_ice_inputs.py", "reconstruction_commands")(
            "authored-revision", "libjuice-relay-only.patch")
        tree_sha = isolated_function("deps.py", "source_tree_sha256")
        for tool in ("sh", "tar", "patch"):
            self.assertIsNotNone(shutil.which(tool), f"required reconstruction tool missing: {tool}")
        owner = TOOLS.parent.parent / "local-data/ice-source-reconstruction-tests"
        owner.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="case-", dir=owner) as temporary:
            root = Path(temporary)
            original = b"/* Authored source fixture. */\nint fixture_value = 1;\n"
            modified = original.replace(b"= 1", b"= 2")
            members = {
                "src": (0o775, None),
                "src/client.c": (0o664, original),
                "LICENSE": (0o664, b"Authored fixture licence.\n"),
                "configure": (0o775, b"#!/bin/sh\nexit 0\n"),
            }
            expected = root / "expected"
            expected.mkdir()
            for relative, (mode, data) in members.items():
                path = expected / relative
                if data is None:
                    path.mkdir()
                else:
                    path.write_bytes(modified if relative == "src/client.c" else data)
                path.chmod(mode & ~0o022)
            expected_sha = tree_sha(expected)
            patch_bytes = "".join(difflib.unified_diff(original.decode().splitlines(True),
                                  modified.decode().splitlines(True), "a/src/client.c", "b/src/client.c")).encode()

            def reconstruct(label, caller_umask, block):
                destination = root / label
                destination.mkdir()
                archive = destination / "libjuice-authored-revision.tar.gz"
                with tarfile.open(archive, "w:gz") as stream:
                    top = tarfile.TarInfo("libjuice-authored-revision")
                    top.type, top.mode = tarfile.DIRTYPE, 0o775
                    stream.addfile(top)
                    for relative, (mode, data) in members.items():
                        entry = tarfile.TarInfo("libjuice-authored-revision/" + relative)
                        entry.mode = mode
                        if data is None:
                            entry.type = tarfile.DIRTYPE
                            stream.addfile(entry)
                        else:
                            entry.size = len(data)
                            stream.addfile(entry, io.BytesIO(data))
                (destination / "libjuice-relay-only.patch").write_bytes(patch_bytes)
                script = (f"umask {caller_umask}\n" + block +
                          'reconstruction_status=$?\numask\nexit "$reconstruction_status"\n')
                result = subprocess.run(["sh", "-c", script],
                                        cwd=destination, capture_output=True, timeout=10)
                self.assertEqual(result.returncode, 0, "emitted reconstruction commands failed")
                self.assertEqual(int(result.stdout.splitlines()[-1], 8), int(caller_umask, 8),
                                 "reconstruction must preserve the recipient shell's umask")
                return destination / "libjuice-authored-revision"

            for caller_umask in ("022", "077"):
                with self.subTest(caller_umask=caller_umask):
                    actual = reconstruct("fixed-" + caller_umask, caller_umask, commands)
                    self.assertEqual(tree_sha(actual), expected_sha)
                    self.assertEqual((actual / "src/client.c").read_bytes(), modified)
                    self.assertEqual((actual / "LICENSE").read_bytes(), members["LICENSE"][1])
                    self.assertEqual(stat.S_IMODE((actual / "src").stat().st_mode), 0o755)
                    self.assertEqual(stat.S_IMODE((actual / "configure").stat().st_mode), 0o755)
                    self.assertEqual(stat.S_IMODE((actual / "src/client.c").stat().st_mode), 0o644)
            control = reconstruct("without-normalization", "077", commands.replace("    umask 022 &&\n", ""))
            self.assertNotEqual(tree_sha(control), expected_sha,
                                "private-umask control must expose the original mode mismatch")
            self.assertEqual((control / "src/client.c").read_bytes(), modified)
            self.assertEqual(stat.S_IMODE((control / "src").stat().st_mode), 0o700)
            self.assertEqual(stat.S_IMODE((control / "src/client.c").stat().st_mode), 0o600)


if __name__ == "__main__":
    unittest.main()
