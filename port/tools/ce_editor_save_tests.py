#!/usr/bin/env python3
"""Exercise editor career isolation through the production serializers."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import resource
import shutil
import subprocess
import tempfile

from ce_editor_lifecycle_tests import definition
from ce_editor_playtest_tests import fixture
import build
import save_flow_tests


ROOT = Path(__file__).resolve().parents[2]
CASES = (
    "inactive", "rejected-entry", "initial-loading", "authoring", "failed-save",
    "failed-read", "invalid-document", "play-loading", "invalid-world", "duplicate-world",
    "playing", "toolbar", "retry", "leave-frontend", "leave-title", "leave-timeout", "level-end",
    "return-loading", "return-authoring", "invalid-return", "saving-policy", "system-quit", "cancel",
)


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def change_function(folder: Path, name: str, transform) -> None:
    path = folder / "editor-lifecycle.private.inc"
    text = path.read_text()
    before = definition(text, name)
    after = transform(before)
    if before == after:
        raise ValueError("mutation did not change " + name)
    path.write_text(text.replace(before, after, 1))


def replace_once(text: str, before: str, after: str) -> str:
    if text.count(before) != 1:
        raise ValueError("mutation anchor is not unique")
    return text.replace(before, after, 1)


def serializer(folder: Path, career: str) -> None:
    save = save_flow_tests.function(career, r"^void\s+CCareer::Save\(char\s*\*")
    mark = save_flow_tests.function(career, r"^void\s+CCareer::SaveAndMarkInProgress\(")
    if save.count("PortCeEditorFilterSave(this, dest);") != 1:
        raise ValueError("editor serializer hook is missing")
    enabled = "#ifndef CE_VERSION\n#error Enable production serializer code\n#endif\n"
    (folder / "editor-career-save.private.inc").write_text(enabled + save)
    (folder / "editor-career-mark.private.inc").write_text(enabled + mark)


def declaration_linkage(out: Path, career: str, compiler: str, environment: dict,
                        causal: bool) -> dict:
    pattern = r"^[^\n]*\bPortCeEditorFilterSave\([^\n;]*\);[^\n]*"
    declaration = re.search(pattern, career, re.M)
    header = re.search(pattern, (ROOT / "port/game/ce_editor.h").read_text(), re.M)
    if not declaration or not header:
        raise ValueError("serializer callback declaration is missing")
    declared = declaration.group(0)
    if 'extern "C"' not in declared:
        raise ValueError("serializer callback must match the editor header linkage")
    definition_path = out / "save-filter-definition.cpp"
    definition_path.write_text("class CCareer;\n" + header.group(0) +
                               "\nvoid PortCeEditorFilterSave(CCareer*, void*) {}\n")

    def check(name: str, prototype: str):
        caller = out / (name + ".cpp")
        caller.write_text("class CCareer;\n" + prototype +
                          "\nint main() { PortCeEditorFilterSave(nullptr, nullptr); return 0; }\n")
        binary = out / name
        built = subprocess.run([compiler, "-std=c++14", "-m32", str(caller),
                                str(definition_path), "-o", str(binary)],
                               cwd=ROOT, env=environment, capture_output=True)
        (out / (name + ".stdout")).write_bytes(built.stdout)
        (out / (name + ".stderr")).write_bytes(built.stderr)
        exit_code = None
        if built.returncode == 0:
            exit_code = subprocess.run([str(binary)], env=environment,
                                       capture_output=True, timeout=10).returncode
        return {"linked": built.returncode == 0, "exit": exit_code,
                "undefined_callback": built.returncode != 0 and
                    b"PortCeEditorFilterSave" in built.stderr}

    baseline = check("save-filter-linkage", declared)
    result = {"baseline": baseline}
    if causal:
        negative = check("save-filter-wrong-linkage", declared.replace('extern "C" ', "", 1))
        result["wrong_linkage_rejected"] = negative["undefined_callback"]
    result["passed"] = baseline["linked"] and baseline["exit"] == 0 and (
        not causal or result["wrong_linkage_rejected"])
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--causal-controls", action="store_true")
    parser.add_argument("--museum-worktree", type=Path,
                        help="also check the parked museum serializer patch in numeric patch order")
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    parent = ROOT / "local-data/ce-editor-save-tests"
    parent.mkdir(parents=True, exist_ok=True)
    out = args.out or Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    out.mkdir(parents=True, exist_ok=True)
    source, project, _ = fixture(out)
    driver_source = (ROOT / "port/tools/tests/ce_editor_playtest_driver.cpp").read_text()
    driver_prefix = re.split(r"^int main\(", driver_source, maxsplit=1, flags=re.M)[0]
    editor = (ROOT / "port/game/ce_editor.cpp").read_text()
    toolbar = definition(editor, "EditorToolbar")
    actions = re.findall(r"item\s*==\s*(\d+)", toolbar[:toolbar.index("EditorStartPlaytest")])
    if not actions:
        raise ValueError("toolbar play action is missing")
    driver_prefix += "\nstatic const int editorPlayToolbarAction = " + actions[-1] + ";\n"
    (out / "editor-playtest-driver.private.inc").write_text(driver_prefix)
    career = build.patched_text(["Career.cpp"])["Career.cpp"]
    serializer(out, career)
    compiler = shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise SystemExit("A C++ compiler is required")
    environment = {k: v for k, v in os.environ.items()
                   if not k.startswith("BEA_") and k not in ("DISPLAY", "WAYLAND_DISPLAY")}
    driver = ROOT / "port/tools/tests/ce_editor_save_driver.cpp"

    def compile_run(folder: Path, cases: tuple[str, ...], composed: bool = False):
        binary = folder / "editor-save-tests"
        command = [compiler, "-std=c++14", "-m32", "-Wall", "-Wextra", "-Werror",
                   "-Wno-unused-variable", "-Wno-unused-function", "-DDRIVER_CE_SNIPPET", "-DDRIVER_EDITOR_VIEW",
                   "-I" + str(folder), "-I" + str(ROOT / "port/game"), str(driver),
                   str(source), str(project), "-o", str(binary)]
        if composed:
            command.insert(1, "-DEDITOR_MUSEUM_COMPOSED")
        built = subprocess.run(command, cwd=ROOT, env=environment, capture_output=True)
        (folder / "compile.stdout").write_bytes(built.stdout)
        (folder / "compile.stderr").write_bytes(built.stderr)
        result = {"compiled": built.returncode == 0, "cases": {}}
        if built.returncode:
            return result
        for case in cases:
            ran = subprocess.run([str(binary), case], cwd=folder, env=environment,
                                 capture_output=True, timeout=20)
            (folder / (case + ".stdout")).write_bytes(ran.stdout)
            (folder / (case + ".stderr")).write_bytes(ran.stderr)
            result["cases"][case] = {"exit": ran.returncode,
                                      "behavior_failure": ran.returncode > 0 and bool(ran.stderr)}
        return result

    def copy_fixture(name: str) -> Path:
        folder = out / name
        folder.mkdir(exist_ok=True)
        for path in out.glob("*.inc"):
            shutil.copyfile(path, folder / path.name)
        return folder

    linkage = declaration_linkage(out, career, compiler, environment, args.causal_controls)
    baseline = compile_run(out, CASES)
    baseline_passed = linkage["passed"] and baseline["compiled"] and all(
        value["exit"] == 0 for value in baseline["cases"].values()) and len(baseline["cases"]) == len(CASES)
    controls = {}
    if baseline_passed and args.causal_controls:
        variants = {
            "filter-only-playing": ("PortCeEditorFilterSave", "!gEditorRequested", "!PortCeEditorPlaying()"),
            "filter-only-ready": ("PortCeEditorFilterSave", "!gEditorRequested", "!gEditorReady"),
            "filter-stays-active-after-exit": ("PortCeEditorFilterSave", "!gEditorRequested || ", ""),
            "filter-saves-live-progress": ("PortCeEditorFilterSave",
                "memcpy(&restored, gEditorCareer, sizeof(restored));", "memcpy(&restored, career, sizeof(restored));"),
            "filter-discards-options": ("PortCeEditorFilterSave", "EditorCopyCareerOptions(restored, *career);", ""),
            "filter-mutates-live-memory": ("PortCeEditorFilterSave",
                "memcpy(destination, &restored, sizeof(restored));", "memcpy(career, &restored, sizeof(restored));"),
            "restore-discards-options": ("EditorRestoreCareer", "EditorCopyCareerOptions(restored, CAREER);", ""),
            "restore-keeps-live-progress": ("EditorRestoreCareer",
                "memcpy(&restored, gEditorCareer, sizeof(restored));", "memcpy(&restored, &CAREER, sizeof(restored));"),
            "retry-keeps-progress": ("EditorPrepareReload", "EditorRestoreCareer();", ""),
            "cancel-keeps-progress": ("PortCeEditorCancel", "EditorRestoreCareer();", ""),
            "return-keeps-load-progress": ("PortCeEditorAfterLoad", "EditorRestoreCareer();", "(void)0;"),
            "toolbar-action-disconnected": ("EditorToolbar", "EditorStartPlaytest();", "(void)0;"),
        }
        for method in ("MusicVolume", "SoundVolume", "InvertYAxis", "InvertJetYAxis", "IsGod",
                       "Vibration", "ControllerConfigurationNum", "ScreenPosX", "ScreenPosY"):
            helper = definition(editor, "EditorCopyCareerOptions")
            match = re.search(r"^\s*target\.Set" + method + r"\([^;]+;", helper, re.M)
            if not match:
                raise ValueError("option copy method is missing: " + method)
            variants["option-omitted-" + method] = ("EditorCopyCareerOptions", match.group(0), "")
        helper = definition(editor, "EditorCopyCareerOptions")
        loop = re.search(r"player\s*<\s*\d+", helper)
        if not loop:
            raise ValueError("per-player option loop is missing")
        variants["second-player-options-omitted"] = ("EditorCopyCareerOptions", loop.group(0), "player < 1")
        for name, (function, before, after) in variants.items():
            folder = copy_fixture(name)
            change_function(folder, function, lambda text, a=before, b=after: replace_once(text, a, b))
            result = compile_run(folder, CASES)
            result["killed"] = result["compiled"] and any(
                item["behavior_failure"] for item in result["cases"].values())
            controls[name] = result
        folder = copy_fixture("serializer-hook-removed")
        path = folder / "editor-career-save.private.inc"
        path.write_text(replace_once(path.read_text(), "PortCeEditorFilterSave(this, dest);", ""))
        result = compile_run(folder, CASES)
        result["killed"] = result["compiled"] and any(
            item["behavior_failure"] for item in result["cases"].values())
        controls[folder.name] = result

    composition = {"requested": args.museum_worktree is not None}
    if baseline_passed and args.museum_worktree:
        museum = args.museum_worktree.resolve()
        museum_patch = museum / "port/patches/0221-ce-museum-save-fence.patch"
        editor_patch = ROOT / "port/patches/0222-ce-editor-save-fence.patch"
        before = build.patched_text(["Career.cpp"], skip=(editor_patch.name,))["Career.cpp"]
        folder = copy_fixture("museum-composition")
        target = folder / "Career.cpp"
        target.write_text(before)
        applied = []
        for patch in (museum_patch, editor_patch):
            ran = subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(patch)],
                                 cwd=folder, capture_output=True)
            (folder / (patch.stem + ".stdout")).write_bytes(ran.stdout)
            (folder / (patch.stem + ".stderr")).write_bytes(ran.stderr)
            applied.append(ran.returncode == 0)
        composition["patches_applied"] = applied
        composition["patch_sha256"] = {patch.name: sha(patch) for patch in (museum_patch, editor_patch)}
        if all(applied):
            composed_career = target.read_text()
            serializer(folder, composed_career)
            engine = (museum / "port/game/ce_museum_visit_engine.cpp").read_text()
            helper = definition(engine, "CopyCareerOptions").replace("CopyCareerOptions", "MuseumCopyCareerOptions")
            museum_filter = definition(engine, "PortCeMuseumVisitFilterSave").replace(
                "CopyCareerOptions", "MuseumCopyCareerOptions")
            declarations = "static unsigned char gMuseumCareer[sizeof(CCareer)];\nstatic bool gMuseumCareerSaved = false;\n"
            (folder / "editor-museum-filter.private.inc").write_text("#ifndef CE_VERSION\n#error Enable production museum filter code\n#endif\n" + declarations + helper + "\n" + museum_filter)
            composition["actual_filters"] = compile_run(folder, ("initial-loading", "playing", "museum-composition"), True)
            composition["engine_sha256"] = sha(museum / "port/game/ce_museum_visit_engine.cpp")
            if args.causal_controls:
                composition["controls"] = {}
                for owner, case in (("Editor", "playing"), ("MuseumVisit", "museum-composition")):
                    mutant = copy_fixture("composition-" + owner + "-hook-removed")
                    for filename in ("editor-career-save.private.inc", "editor-museum-filter.private.inc"):
                        shutil.copyfile(folder / filename, mutant / filename)
                    path = mutant / "editor-career-save.private.inc"
                    path.write_text(replace_once(path.read_text(), "PortCe" + owner + "FilterSave(this, dest);", ""))
                    result = compile_run(mutant, (case,), True)
                    result["killed"] = result["compiled"] and any(
                        item["behavior_failure"] for item in result["cases"].values())
                    composition["controls"][owner] = result
            result = composition["actual_filters"]
            composition["passed"] = result["compiled"] and all(
                item["exit"] == 0 for item in result["cases"].values()) and all(
                item["killed"] for item in composition.get("controls", {}).values())
        else:
            composition["passed"] = False

    passed = baseline_passed and all(item["killed"] for item in controls.values()) and (
        not composition["requested"] or composition.get("passed", False))
    inputs = [source, project, Path(__file__), driver, ROOT / "port/game/ce_editor.h",
              ROOT / "port/tools/tests/ce_editor_lifecycle_driver.cpp",
              ROOT / "port/tools/tests/ce_editor_playtest_driver.cpp",
              ROOT / "port/tools/tests/ce_editor_save_fixture.h",
              ROOT / "port/patches/0222-ce-editor-save-fence.patch"]
    receipt = {"passed": passed, "scope": "Actual i386 career serializers and editor lifecycle; authored data only",
               "baseline": baseline, "causal_controls": controls, "composition": composition,
               "declaration_linkage": linkage,
               "sha256": {str(path.relative_to(ROOT)): sha(path) for path in inputs},
               "serializer_sha256": hashlib.sha256(career.encode("latin1")).hexdigest()}
    (out / "RESULT.private.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({"passed": passed, "cases": len(baseline["cases"]),
                      "controls": len(controls), "composition": composition.get("passed"),
                      "evidence": str(out)}))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
