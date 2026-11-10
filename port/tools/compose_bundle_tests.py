#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise actual tar/zip composition with wholly invented program exports; no game or network."""
from contextlib import contextmanager, redirect_stderr, redirect_stdout
import copy
import hashlib
import io
import json
import os
from pathlib import Path
import tarfile
import tempfile
import time
import unittest
from unittest.mock import patch
import zipfile

import compose_bundle as bundle

ROOT = Path(__file__).resolve().parents[2]


def inventory(root):
    return {p.relative_to(root).as_posix(): {
        "sha256": hashlib.sha256(p.read_bytes()).hexdigest(), "size": p.stat().st_size,
        "executable": p.name in ("onslaught-ce", "onslaught-ce-launch", "bea", "preflight.sh",
                                  "onslaught-ce.exe", "bea.exe", "OnslaughtCompanion.x86_64",
                                  "OnslaughtCompanion.exe", "level-tool.py"),
    } for p in sorted(root.rglob("*")) if p.is_file()}


def fixture(root, platform="linux", tools=True):
    """These are text strings with program filenames, not executable or retail fixtures."""
    root.mkdir(parents=True, exist_ok=True)
    programs = {
        "ce": (["onslaught-ce.exe", "bea.exe"] if platform == "windows" else
               ["onslaught-ce", "onslaught-ce-launch", "bea", "preflight.sh"])
              + ["COPYING", "NOTICE.txt", "THIRD-PARTY-NOTICES.txt", "README.txt", "lib/authored-runtime.txt"],
        "companion": [bundle.ENTRYPOINTS[platform]["companion"], "OnslaughtCompanion.pck",
                      "LICENSE.txt", "LORE-LICENSE.txt", "README.txt", "data/authored-runtime.txt"],
    }
    if tools:
        programs["tools"] = ["level-tool.py", "LICENSE.txt", "README.txt"]
    components = []
    for identity, names in programs.items():
        component_root = root / identity
        for name in names:
            path = component_root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(f"Invented {identity} fixture: {name}\n", encoding="utf-8")
        if identity == "companion":
            exported = {n: row["sha256"] for n, row in inventory(component_root).items()}
            (component_root / "package-sha256.json").write_bytes(bundle.encoded({
                "platform": platform, "godotVersion": "authored-engine", "dotnetRuntime": "authored-runtime",
                "files": exported,
            }))
        components.append({
            "id": identity, "platform": platform, "version": "0.2.0-fixture", "root": identity,
            "sourceRepository": f"https://example.invalid/{identity}", "sourceCommit": "a" * 40,
            "entrypoint": bundle.ENTRYPOINTS[platform].get(identity, "level-tool.py"),
            "licenses": sorted(bundle.CE_NOTICES) if identity == "ce" else
                        (["LICENSE.txt", "LORE-LICENSE.txt"] if identity == "companion" else ["LICENSE.txt"]),
            "files": inventory(component_root),
        })
    recipe = {"schema": 1, "version": "0.2.0-fixture", "platform": platform, "epoch": 1700000000,
              "components": components}
    recipe_path = root / "recipe.json"
    recipe_path.write_bytes(bundle.encoded(recipe))
    return recipe, recipe_path


class ComposeBundleTests(unittest.TestCase):
    def setUp(self):
        owner = ROOT / "local-data/port/compose-bundle-tests"
        owner.mkdir(parents=True, exist_ok=True)
        self.scratch = tempfile.TemporaryDirectory(prefix="authored-", dir=owner)
        self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name)
        self.recipe, self.recipe_path = fixture(self.root)
        self.output = self.root / "bundle.tar.gz"

    def save(self):
        self.recipe_path.write_bytes(bundle.encoded(self.recipe))

    def component(self, identity):
        return next(c for c in self.recipe["components"] if c["id"] == identity)

    def rejects(self, message=None, output=None):
        output = output or self.output
        with self.assertRaises(ValueError) as error:
            bundle.compose(self.recipe_path, output)
        if message:
            self.assertIn(message, str(error.exception))
        self.assertFalse(output.exists())
        self.assertEqual(list(self.root.glob(".onslaught-bundle-*.partial")), [])

    def test_actual_archives_both_platforms_exact_payloads_modes_manifest_and_determinism(self):
        for platform, suffix in (("linux", ".tar.gz"), ("windows", ".zip")):
            with self.subTest(platform=platform):
                root = self.root / platform
                recipe, path = fixture(root, platform)
                before = {p.relative_to(root): p.read_bytes() for p in root.rglob("*") if p.is_file()}
                first, second = root / ("one" + suffix), root / ("two" + suffix)
                result = bundle.compose(path, first)
                # Filesystem dates are not archive dates; the recipe fixes the latter.
                for c in recipe["components"]:
                    for p in (root / c["id"]).rglob("*"):
                        if p.is_file():
                            os.utime(p, (1800000000, 1800000000))
                self.assertEqual(result, bundle.compose(path, second))
                self.assertEqual(first.read_bytes(), second.read_bytes())
                self.assertEqual(result["sha256"], hashlib.sha256(first.read_bytes()).hexdigest())
                top = f"onslaught-0.2.0-fixture-{platform}"
                expected = {f"{top}/{c['id']}/{name}": (root / c["id"] / name).read_bytes()
                            for c in recipe["components"] for name in c["files"]}
                if platform == "windows":
                    with zipfile.ZipFile(first) as archive:
                        actual = {m.filename: archive.read(m) for m in archive.infolist()}
                        modes = {m.filename: (m.external_attr >> 16) & 0o777 for m in archive.infolist()}
                        self.assertEqual(len(actual), len(archive.infolist()))
                else:
                    with tarfile.open(first, "r:gz") as archive:
                        actual = {m.name: archive.extractfile(m).read() for m in archive.getmembers()}
                        modes = {m.name: m.mode for m in archive.getmembers()}
                        self.assertEqual(len(actual), len(archive.getmembers()))
                self.assertEqual(set(actual), set(expected) | {top + "/bundle-manifest.json", top + "/README.txt"})
                self.assertEqual({n: actual[n] for n in expected}, expected)
                manifest = json.loads(actual[top + "/bundle-manifest.json"])
                self.assertEqual(manifest["platform"], platform)
                self.assertEqual([c["directory"] for c in manifest["components"]], ["ce", "companion", "tools"])
                for c in manifest["components"]:
                    self.assertNotIn("root", c)
                    self.assertNotIn("snapshot", c)
                    self.assertEqual(c["files"], next(r["files"] for r in recipe["components"] if r["id"] == c["id"]))
                    self.assertEqual(modes[f"{top}/{c['id']}/{c['entrypoint']}"], 0o755)
                if platform == "linux":
                    for name in ("bea", "onslaught-ce-launch", "preflight.sh"):
                        self.assertEqual(modes[f"{top}/ce/{name}"], 0o755)
                self.assertEqual(modes[top + "/README.txt"], 0o644)
                guide = actual[top + "/README.txt"].decode()
                self.assertIn("outside your game, user and backup folders", guide)
                self.assertIn("CE launcher owns CE updates inside ce/", guide)
                self.assertIn("Replace Companion and tools independently", guide)
                self.assertNotIn(str(root), actual[top + "/bundle-manifest.json"].decode())
                self.assertEqual(before, {p: (root / p).read_bytes() for p in before})

    def test_cli_can_compose_without_running_components_subprocesses_or_network(self):
        with patch("sys.argv", ["compose_bundle.py", "--recipe", str(self.recipe_path), "--out", str(self.output)]), \
                patch("subprocess.run", side_effect=AssertionError("no subprocess")), \
                patch("subprocess.Popen", side_effect=AssertionError("no process")), \
                patch("socket.socket", side_effect=AssertionError("no network")), redirect_stdout(io.StringIO()) as output:
            bundle.main()
        self.assertTrue(self.output.is_file())
        self.assertIn("bundle.tar.gz", output.getvalue())

    def test_optional_tool_tree_can_be_absent_and_versions_remain_independent(self):
        self.recipe["components"] = [c for c in self.recipe["components"] if c["id"] != "tools"]
        self.component("ce")["version"] = "0.1.13"
        self.component("companion")["version"] = "0.3.0-fixture"
        self.save()
        bundle.compose(self.recipe_path, self.output)
        with tarfile.open(self.output, "r:gz") as archive:
            manifest = json.load(archive.extractfile("onslaught-0.2.0-fixture-linux/bundle-manifest.json"))
        self.assertEqual([c["version"] for c in manifest["components"]], ["0.1.13", "0.3.0-fixture"])
        self.assertFalse(any("/tools/" in n for n, _, _ in bundle.archive_members(self.output, "linux")))

    def test_wrong_component_platform_is_rejected(self):
        self.component("companion")["platform"] = "windows"
        self.save()
        self.rejects("component platform")

    def test_linux_ce_auxiliary_programs_must_be_executable(self):
        original = copy.deepcopy(self.recipe)
        for name in ("bea", "onslaught-ce-launch", "preflight.sh"):
            with self.subTest(program=name):
                self.recipe = copy.deepcopy(original)
                self.component("ce")["files"][name]["executable"] = False
                self.save()
                self.rejects("Linux CE programs must be executable", output=self.root / (name + ".tar.gz"))

    def test_epoch_boundaries_work_for_both_formats_and_overflow_is_rejected(self):
        for platform, suffix in (("linux", ".tar.gz"), ("windows", ".zip")):
            root = self.root / ("epoch-" + platform)
            self.recipe, self.recipe_path = fixture(root, platform)
            for epoch in (315532800, 2 ** 32 - 1):
                with self.subTest(platform=platform, valid_epoch=epoch):
                    self.recipe["epoch"] = epoch
                    self.save()
                    output = root / (str(epoch) + suffix)
                    bundle.compose(self.recipe_path, output)
                    if platform == "linux":
                        self.assertEqual(int.from_bytes(output.read_bytes()[4:8], "little"), epoch)
                        with tarfile.open(output, "r:gz") as archive:
                            self.assertTrue(all(m.mtime == epoch for m in archive))
                    else:
                        expected = time.gmtime(epoch)[:5] + (time.gmtime(epoch).tm_sec // 2 * 2,)
                        with zipfile.ZipFile(output) as archive:
                            self.assertTrue(all(m.date_time == expected for m in archive.infolist()))
            for epoch in (2 ** 32, 4354819198):
                with self.subTest(platform=platform, invalid_epoch=epoch):
                    self.recipe["epoch"] = epoch
                    self.save()
                    self.rejects("epoch must fit", output=root / (str(epoch) + suffix))

    def test_cli_reports_epoch_overflow_as_a_validation_error(self):
        self.recipe["epoch"] = 2 ** 32
        self.save()
        with patch("sys.argv", ["compose_bundle.py", "--recipe", str(self.recipe_path), "--out", str(self.output)]), \
                redirect_stderr(io.StringIO()) as errors, self.assertRaises(SystemExit) as error:
            bundle.main()
        self.assertEqual(error.exception.code, 1)
        self.assertIn("epoch must fit", errors.getvalue())
        self.assertNotIn("Traceback", errors.getvalue())
        self.assertFalse(self.output.exists())

    def test_wrong_companion_export_platform_is_rejected(self):
        path = self.root / "companion/package-sha256.json"
        data = json.loads(path.read_bytes())
        data["platform"] = "windows"
        path.write_bytes(bundle.encoded(data))
        self.component("companion")["files"] = inventory(path.parent)
        self.save()
        self.rejects("Companion export inventory or platform")

    def test_extra_missing_or_empty_directory_inputs_are_rejected(self):
        extra = self.root / "ce/extra.txt"
        extra.write_text("Invented unselected content")
        self.rejects("explicit file inventory")
        extra.unlink()
        (self.root / "ce/empty").mkdir()
        self.rejects("explicit file inventory")
        (self.root / "ce/empty").rmdir()
        self.component("ce")["files"]["absent.txt"] = {"sha256": "f" * 64, "size": 0, "executable": False}
        self.save()
        self.rejects("explicit file inventory")

    def test_wrong_content_of_same_size_is_rejected_before_output_exists(self):
        path = self.root / "ce/onslaught-ce"
        path.write_bytes(b"x" * path.stat().st_size)
        self.rejects("SHA-256")

    def test_companion_inventory_must_agree_with_every_selected_file(self):
        self.component("companion")["files"]["README.txt"]["sha256"] = "f" * 64
        self.save()
        self.rejects("Companion export inventory or platform")

    def test_linked_file_directory_root_and_special_file_are_rejected(self):
        for target in ("ce/README.txt", "ce/lib"):
            with self.subTest(target=target):
                original = self.root / target
                retained = original.with_name(original.name + "-authored-original")
                original.rename(retained)
                original.symlink_to(retained, target_is_directory=retained.is_dir())
                self.rejects("link or special")
                original.unlink()
                retained.rename(original)
        (self.root / "ce-link").symlink_to(self.root / "ce", target_is_directory=True)
        self.component("ce")["root"] = "ce-link"
        self.save()
        self.rejects("linked component root")
        self.component("ce")["root"] = "ce"
        self.save()
        os.mkfifo(self.root / "ce/authored-fifo")
        self.rejects("link or special")

    def test_case_conflicts_are_rejected_even_for_linux_output(self):
        path = self.root / "ce/readme.txt"
        path.write_text("Invented conflicting name")
        self.component("ce")["files"] = inventory(path.parent)
        self.save()
        self.rejects("case-insensitive path conflict")

    def test_unsafe_inventory_names_are_rejected(self):
        for name in (".", "../outside.txt", "/absolute", "lib//file", "lib/./file", "CON", "aux.txt",
                     "lib\\file", "stream:other", "tail. ", ".git/config", ".env-secret", "ce.ini"):
            with self.subTest(name=name):
                recipe = copy.deepcopy(self.recipe)
                self.component("ce")["files"][name] = {"sha256": "f" * 64, "size": 0, "executable": False}
                self.save()
                self.rejects()
                self.recipe = recipe

    def test_windows_device_aliases_are_rejected_without_rejecting_ordinary_names(self):
        self.recipe, self.recipe_path = fixture(self.root / "device-windows", "windows")
        tools = self.recipe_path.parent / "tools"
        for index, name in enumerate(("CONIN$", "conout$.txt", "COM¹.txt", "com².log", "LPT³.dat", "NUL .txt", "AUX   .txt")):
            with self.subTest(reserved=name):
                path = tools / name
                path.write_text("Invented filename regression fixture\n")
                self.component("tools")["files"] = inventory(tools)
                self.save()
                try:
                    self.rejects("filename is unsafe", output=self.root / ("alias-" + str(index) + ".zip"))
                finally:
                    path.unlink()
        for name in ("CONIN-dollar.txt", "COM10.txt", "COM¹-extra.txt", "NUL-file.txt", "AUX notes.txt"):
            (tools / name).write_text("Invented ordinary-name control\n")
        self.component("tools")["files"] = inventory(tools)
        self.save()
        bundle.compose(self.recipe_path, self.root / "ordinary-names.zip")

    def test_required_components_ids_entrypoint_and_licenses_are_checked(self):
        original = copy.deepcopy(self.recipe)
        changes = [
            lambda: self.recipe["components"].pop(0),
            lambda: self.recipe["components"].append(copy.deepcopy(self.component("ce"))),
            lambda: self.component("ce").update(entrypoint="README.txt"),
            lambda: self.component("ce").update(licenses=["missing-notice"]),
            lambda: self.component("ce").update(licenses=["COPYING"]),
            lambda: self.component("companion").update(licenses=["LICENSE.txt"]),
            lambda: self.component("tools").update(sourceRepository="https://user:secret@example.invalid/source"),
            lambda: self.component("ce").update(sourceCommit="short"),
            lambda: self.recipe.update(version="../unsafe"),
            lambda: self.recipe.update(epoch=0),
            lambda: self.recipe.update(schema=True),
        ]
        for change in changes:
            with self.subTest(change=changes.index(change)):
                self.recipe = copy.deepcopy(original)
                change()
                self.save()
                self.rejects()

    def test_component_roots_cannot_overlap(self):
        tool = self.component("tools")
        ce = self.component("ce")
        tool.update(root=ce["root"], files=copy.deepcopy(ce["files"]), entrypoint=ce["entrypoint"], licenses=ce["licenses"])
        self.save()
        self.rejects("separate directories")

    def test_existing_output_and_racing_output_are_preserved(self):
        sentinel = b"Invented pre-existing output"
        self.output.write_bytes(sentinel)
        with self.assertRaisesRegex(ValueError, "already exists"):
            bundle.compose(self.recipe_path, self.output)
        self.assertEqual(self.output.read_bytes(), sentinel)
        self.output.unlink()
        original = bundle.os.link

        def race(source, target):
            Path(target).write_bytes(sentinel)
            original(source, target)

        with patch.object(bundle.os, "link", side_effect=race), self.assertRaises(FileExistsError):
            bundle.compose(self.recipe_path, self.output)
        self.assertEqual(self.output.read_bytes(), sentinel)
        self.assertEqual(list(self.root.glob(".onslaught-bundle-*.partial")), [])

    def test_output_cannot_be_inside_any_component_root(self):
        self.rejects("outside all selected", output=self.root / "ce/new.tar.gz")

    def test_source_mutation_during_assembly_prevents_output(self):
        original = bundle._writers

        @contextmanager
        def mutate(temporary, recipe):
            with original(temporary, recipe) as write:
                def changed(name, stream, row):
                    write(name, stream, row)
                    if name.endswith("/ce/README.txt"):
                        with (self.root / "ce/README.txt").open("ab") as source:
                            source.write(b"Invented concurrent mutation")
                yield changed

        with patch.object(bundle, "_writers", mutate):
            self.rejects("selected component")

    def test_output_readback_failure_prevents_publication(self):
        with patch.object(bundle, "archive_members", return_value=iter([])):
            self.rejects("output read-back")

    def test_duplicate_json_fields_are_rejected(self):
        self.recipe_path.write_text('{"schema": 1, "schema": 2}')
        self.rejects("duplicate JSON field")


if __name__ == "__main__":
    unittest.main()
