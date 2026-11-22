#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise fresh-profile quality with the actual startup-open hook and native visual setters."""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import resource
import subprocess

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "port"
SUPPORT = PORT / "tools/tests"
CASES = ("missing", "existing", "existing-saved-on", "existing-saved-off", "retail-leftover", "retail-leftover-opt-out", "profile-unknown", "no-antialiasing", "permission", "io-error",
         "unclassified", "no-observation", "subsequent-existing", "opt-out", "unsupported", "colour16", "landscape-limited",
         "antialiasing-levels", "antialiasing-all-levels", "rows-saved-off", "rows-saved-on", "rows-legacy-on", "rows-one-saved", "rows-unwritable", "rows-refused",
         "layer-off", "fence-CLOCK", "fence-TAPE", "fence-PROBES", "fence-RECORD")


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def block(text, begin):
    opening = text.index("{", begin)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[begin:end]


def native_inline(text, owner, method, target):
    declaration = re.search(r"\bclass\s+" + owner + r"\b", text)
    if not declaration:
        raise AssertionError("native class absent: " + owner)
    body = block(text, declaration.start())
    declaration = re.search(r"\b(?:virtual\s+)?int\s+" + method + r"\s*\([^;{}]*\)\s*\{", body)
    if not declaration:
        raise AssertionError("native inline method absent: " + owner + "::" + method)
    method_body = "{" + block(body, declaration.start()).split("{", 1)[1]
    return "int " + target + "::" + method + "() " + method_body + "\n"


def definition(text, pattern):
    found = re.search(r"^[^\n;{}]*\b" + pattern + r"\s*\([^;{}]*\)[^;{}]*\{", text, flags=re.M)
    if not found:
        raise AssertionError("native definition absent: " + pattern)
    return block(text, found.start())


def stand_in_tweaks(driver_text):
    """{name: initial value} of every CTweakSLONG stand-in a test driver defines."""
    found = {}
    for statement in re.findall(r"^CTweakSLONG\s+([^;]+);", driver_text, flags=re.M):
        for name, value in re.findall(r'(\w+)\((?:"[^"]*",\s*)?(-?\d+)\)', statement):
            found[name] = int(value)
    return found


def game_tweaks(texts):
    """{name: initial value} of the game's own CTweakSLONG definitions in these source texts (each names the tweak twice)."""
    return {name: int(value) for text in texts
            for name, value in re.findall(r'^CTweakSLONG\s+(\w+)\("\1",\s*(-?\d+)\)\s*;', text, flags=re.M)}


def stand_in_differences(driver_text, game, authored=()):
    """Every tweak stand-in of a driver that does not start at the game's own initial value, as text; a driver with none is one too.
    authored names the stand-ins a driver starts elsewhere on purpose: each must really differ, so a stale exception is a difference."""
    stand_ins = stand_in_tweaks(driver_text)
    wrong = [f"{name}: the driver starts it at {value}, the game at {game.get(name, 'no such tweak')}"
             for name, value in sorted(stand_ins.items()) if name not in authored and game.get(name) != value]
    wrong += [f"{name}: named as an authored departure, but the driver starts it where the game does (or has no such stand-in)"
              for name in authored if name not in stand_ins or game.get(name) == stand_ins[name]]
    return wrong if stand_ins else ["the driver defines no CTweakSLONG stand-in"]


TWEAK_SOURCES = ("PauseMenu.cpp", "DXParticleTexture.cpp", "DXLandscape.cpp")	# where the game defines the tweaks the two graphics drivers stand in for


def check_stand_ins(build, driver, authored=()):
    """A stand-in that starts where the game does not tests a state no player has: the feature tweak starts set, and a driver that
    started it clear once hid that an existing profile got the extra features switched on. Raises with every difference; authored
    names the stand-ins the driver starts elsewhere on purpose."""
    game = game_tweaks(build.patched_text(list(TWEAK_SOURCES)).values())
    text = Path(driver).read_text()
    wrong = stand_in_differences(text, game, authored)
    if wrong:
        raise AssertionError(f"{Path(driver).name}: stand-in initial values differ from the game's:\n  " + "\n  ".join(wrong))
    # the comparison itself must see a changed value, a missing definition and an exception nothing needs
    probe = re.sub(r'\bEXTRA_GEFORCE_FX_FEATURES\(((?:"[^"]*",\s*)?)(-?\d+)\)',
                   lambda m: f"EXTRA_GEFORCE_FX_FEATURES({m.group(1)}{1 - int(m.group(2))})", text, count=1)
    if probe == text or len(stand_in_differences(probe, game, authored)) != 1 or not stand_in_differences(text, {}, authored) or \
            not stand_in_differences(text, game, tuple(authored) + ("GEFORCE_FX_POWER",)):
        raise AssertionError("the stand-in comparison does not detect a changed initial value")
    return stand_in_tweaks(text)


def run(command, expected=0):
    def bounded():
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    env = os.environ.copy()
    for name in ("BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD", "DISPLAY", "WAYLAND_DISPLAY", "DBUS_SESSION_BUS_ADDRESS"):
        env.pop(name, None)
    result = subprocess.run([str(x) for x in command], capture_output=True, text=True, timeout=45, env=env, preexec_fn=bounded)
    if result.returncode != expected:
        raise RuntimeError(f"command failed: {command}\n{result.stdout}{result.stderr}")
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--causal-controls", action="store_true")
    parser.add_argument("--output", type=Path, default=ROOT / "local-data/graphics-defaults-tests")
    args = parser.parse_args()
    out = args.output.resolve()
    if not out.is_relative_to((ROOT / "local-data").resolve()):
        raise SystemExit("outputs must be inside this checkout's ignored local-data")
    out.mkdir(parents=True, exist_ok=True)
    build = module("defaults_build", PORT / "build.py")
    helpers = module("defaults_functions", PORT / "tools/public_d3dapp_tests.py")
    texts = build.patched_text(["ltshell.cpp", "PauseMenu.cpp", "MenuItem.h"])
    menu = texts["PauseMenu.cpp"]
    actual = ""
    classes = ("CShadowDetail", "CTreeDetail", "CLandscapeRes", "CTrilinear", "CTextureRes", "CEnable32Bit")
    for owner in classes:
        for method in ("SetValue", "GetValue"):
            actual += helpers.function(menu, r"^[^\n]*\b" + owner + "::" + method + r"\s*\(") + "\n"
    for owner in ("CLandscapeRes", "CEnable32Bit"):
        actual += helpers.function(menu, r"^[^\n]*\b" + owner + r"::GetNumChoices\s*\(") + "\n"
    for owner in ("CShadowDetail", "CTreeDetail"):
        actual += native_inline(menu, "CDetailItem", "GetNumChoices", owner)
    actual += native_inline(menu, "CTextureRes", "GetNumChoices", "CTextureRes")
    actual += native_inline(texts["MenuItem.h"], "CBOOLMenuItem", "GetNumChoices", "CTrilinear")
    for pattern in (r"MultiSampleIndex", r"(?:D3DMULTISAMPLE_TYPE|int)\s+MultiSampleType"):
        actual += definition(menu, pattern) + "\n"
    for method in ("GetNumChoices", "GetValue", "SetValue"):
        actual += definition(menu, "CMultiSample::" + method) + "\n"
    actual += helpers.function(menu, r"^void PortCeGraphicsApplyFreshDefaults\s*\(") + "\n"
    shell = texts["ltshell.cpp"]
    declared = re.search(r"char\s+DEFAULT_OPTIONS_NAME\s*\[[^\]]*\]\s*=\s*\"([^\"]*)\"\s*;", shell)
    if not declared or declared.group(1) != "defaultoptions.bea" or \
            (PORT / "game/ce_graphics_policy.cpp").read_text().count('PortUserFileState("defaultoptions.bea")') != 1:
        raise AssertionError("the profile's own options file must be asked for by the game's DEFAULT_OPTIONS_NAME")
    opening = re.search(r"\bFILE\s*\*\s*file\s*=\s*fopen\(DEFAULT_OPTIONS_NAME[^;]*;", shell)
    if not opening:
        raise AssertionError("native options open absent")
    first = shell.rfind("\n#if", 0, opening.start()) + 1
    observer = "PortCeGraphicsOptionsOpened(file != NULL, errno);"
    if shell.count(observer) != 1:
        raise AssertionError("native options observation must occur exactly once")
    last = shell.index("\n#endif", shell.index(observer, opening.end())) + len("\n#endif")
    fragment = shell[first:last]
    if fragment.count("fopen(") != 1 or fragment.count("CE_BootOpenLocal(") != 1:
        raise AssertionError("native options wrapper no longer has the reviewed ordinary and network branches")
    actual += "void OpenNativeOptions()\n{\n" + fragment + "\n}\n"
    (out / "ce_graphics_defaults_actual.inc").write_text(actual)
    policy = PORT / "game/ce_graphics_policy.cpp"
    driver = SUPPORT / "ce_graphics_defaults_driver.cpp"
    stand_ins = check_stand_ins(build, driver)
    common = ["clang++", "-std=c++14", "-DBEA_GRAPHICS_POLICY_TEST", "-DBEA_PUBLIC_GRAPHICS_HOST=1", "-DCE_NETWORK_OFF=1",
              "-I", SUPPORT, "-I", PORT / "game", "-I", PORT / "platform/common"]

    def compile_one(directory, source=policy, public=True, network=False):
        executable = directory / "driver"
        flags = [flag for flag in common if flag != "-DCE_NETWORK_OFF=1"] if network else common
        run(flags + (["-DCE_VERSION=1"] if public else []) + ["-I", directory, source, driver, "-o", executable])
        return executable

    binary = compile_one(out)
    observations = {case: run([binary, case]) for case in CASES}
    private = out / "private"
    private.mkdir(exist_ok=True)
    (private / "ce_graphics_defaults_actual.inc").write_text(actual)
    observations["private"] = run([compile_one(private, public=False), "private"])
    network = out / "network"
    network.mkdir(exist_ok=True)
    (network / "ce_graphics_defaults_actual.inc").write_text(actual)
    network_binary = compile_one(network, network=True)
    observations.update({"network-" + case: run([network_binary, case]) for case in CASES})

    def second_start(executable, first):
        """The same profile started again: it now has options of its own, and ce.ini holds what its first start wrote."""
        return run([executable, "existing"] + [line[len("wrote "):] for line in first.splitlines() if line.startswith("wrote ")])

    # A profile that already has options (an upgrade from a release without the row) and no extra_graphics line keeps the extra
    # features off; a new profile gets them and still has them at its second start, because its first start wrote the choice.
    for label, executable in (("", binary), ("network-", network_binary)):
        if "state capable=1 extras=0" not in observations[label + "existing"]:
            raise AssertionError("an existing profile with no extra_graphics line must keep the extra features off")
        first = observations[label + "missing"]
        if "state capable=1 extras=1" not in first or "wrote extra_graphics=1" not in first:
            raise AssertionError("a new profile must get the extra features and write the choice once")
        again = observations[label + "second-start"] = second_start(executable, first)
        if "state capable=1 extras=1" not in again or "wrote " in again:
            raise AssertionError("a new profile's second start must keep the extra features and write nothing")
    controls = {}
    if args.causal_controls:
        policy_text = policy.read_text()
        mutations = {
            "treat-unreadable-as-new": ("policy", ": error == ENOENT;", ": true;", "permission"),
            "overwrite-existing-profile": ("policy", 'opened ? PortUserFileState("defaultoptions.bea") == 0 : error == ENOENT;', "true;", "existing"),
            "retail-leftover-counts-as-saved": ("policy", 'opened ? PortUserFileState("defaultoptions.bea") == 0 : error == ENOENT;', "!opened && error == ENOENT;", "retail-leftover"),
            "unknown-profile-state-is-fresh": ("policy", 'PortUserFileState("defaultoptions.bea") == 0', 'PortUserFileState("defaultoptions.bea") <= 0', "profile-unknown"),
            "profile-copy-ignored": ("policy", 'PortUserFileState("defaultoptions.bea") == 0', 'PortUserFileState("defaultoptions.bea") != 7', "existing"),
            "game-tweak-decides-an-existing-profile": ("policy", "requested = saved >= 0 ? saved != 0 : freshOptionsPending;",
                                                       "requested = saved >= 0 ? saved != 0 : freshOptionsPending || EXTRA_GEFORCE_FX_FEATURES.mValue != 0;", "existing"),
            "saved-extras-choice-ignored": ("policy", "requested = saved >= 0 ? saved != 0 : freshOptionsPending;", "requested = freshOptionsPending;", "existing-saved-on"),
            "fresh-extras-written-over-a-saved-choice": ("policy", 'if (saved < 0 && freshOptionsPending) CE_SettingSet("extra_graphics", 1);',
                                                         'if (freshOptionsPending) CE_SettingSet("extra_graphics", 1);', "opt-out"),
            "existing-profile-gets-an-extras-line": ("policy", 'if (saved < 0 && freshOptionsPending) CE_SettingSet("extra_graphics", 1);',
                                                     'if (saved < 0) CE_SettingSet("extra_graphics", requested);', "existing"),
            "reapply-on-reset": ("policy", "freshOptionsPending = false;", "/* authored control: retain fresh state */", "missing"),
            "omit-native-open-observer": ("actual", observer, "/* authored control: omit startup observation */", "missing"),
            "omit-native-quality-call": ("actual", "geometry.SetValue(geometry.GetNumChoices() - 1);", "/* authored control: omit geometry setter */", "missing"),
            "omit-antialiasing-default": ("actual", "if (antialiasing.GetNumChoices() > 1) antialiasing.SetValue(antialiasing.GetNumChoices() - 1);",
                                          "/* authored control: omit anti-aliasing */", "missing"),
            "antialiasing-without-choices": ("actual", "if (antialiasing.GetNumChoices() > 1) antialiasing.SetValue(antialiasing.GetNumChoices() - 1);",
                                             "antialiasing.SetValue(antialiasing.GetNumChoices() - 1);", "no-antialiasing"),
            "antialiasing-first-level-only": ("actual", "antialiasing.SetValue(antialiasing.GetNumChoices() - 1);", "antialiasing.SetValue(1);", "antialiasing-levels"),
            "antialiasing-second-highest": ("actual", "antialiasing.SetValue(antialiasing.GetNumChoices() - 1);", "antialiasing.SetValue(antialiasing.GetNumChoices() - 2);",
                                            "antialiasing-all-levels"),
            "unit-models-fresh-off": ("policy", "const int kFreshUnitModels = 1;", "const int kFreshUnitModels = 0;", "missing"),
            "tree-models-fresh-on": ("policy", "const int kFreshTreeModels = 0;", "const int kFreshTreeModels = 1;", "missing"),
            "legacy-full-models-ignored": ("policy", "if (savedLegacy == 1)", "if (false && savedLegacy == 1)", "rows-legacy-on"),
            "terrain-fresh-normal": ("policy", "const int kFreshTerrainDetail = 1;", "const int kFreshTerrainDetail = 0;", "missing"),
            "omit-terrain-default": ("policy", 'FreshBeyondRetailRow("terrain_detail_distance", CE_TerrainDetailRequest, kFreshTerrainDetail);',
                                     "/* authored control: omit Terrain detail distance */", "missing"),
            "aniso-fresh-four": ("policy", "const int kFreshAnisotropicFiltering = 16;", "const int kFreshAnisotropicFiltering = 4;", "missing"),
            "omit-aniso-default": ("policy", 'FreshAnisoRow("anisotropic_filtering", CE_AnisoRequest, kFreshAnisotropicFiltering);',
                                   "/* authored control: omit Anisotropic filtering */", "missing"),
            "water-fresh-256": ("policy", "const int kFreshWaterReflectionRes = 1024;", "const int kFreshWaterReflectionRes = 256;", "missing"),
            "omit-water-default": ("policy", 'FreshWaterReflectionRow("water_reflection_res", CE_WaterReflectionRequest, kFreshWaterReflectionRes);',
                                   "/* authored control: omit Water reflections */", "missing"),
            "shadow-fresh-64": ("policy", "const int kFreshDynamicShadowRes = 256;", "const int kFreshDynamicShadowRes = 64;", "missing"),
            "omit-shadow-default": ("policy", 'FreshDynamicShadowRow("dynamic_shadow_res", CE_DynamicShadowRequest, kFreshDynamicShadowRes);',
                                    "/* authored control: omit Dynamic shadows */", "missing"),
            "saved-rows-ignored": ("policy", "    const int saved = CE_SettingGetBool(key, -1);\n    if (saved >= 0) request(saved);", "    const int saved = CE_SettingGetBool(key, -1);\n    if (saved >= 0) request(1);", "rows-saved-off"),
            "saved-rows-rewritten": ("policy", "    const int saved = CE_SettingGetBool(key, -1);\n    if (saved >= 0) request(saved);", "    const int saved = CE_SettingGetBool(key, -1);\n    if (saved >= 0) { request(saved); CE_SettingSet(key, saved); }", "rows-saved-off"),
            "default-rows-written-wrongly": ("policy", "    const int saved = CE_SettingGetBool(key, -1);\n    if (saved >= 0) request(saved);\n    else if (request(fresh)) CE_SettingSet(key, fresh);", "    const int saved = CE_SettingGetBool(key, -1);\n    if (saved >= 0) request(saved);\n    else if (request(fresh)) CE_SettingSet(key, 1 - fresh);", "missing"),
            "default-rows-not-written": ("policy", "    const int saved = CE_SettingGetBool(key, -1);\n    if (saved >= 0) request(saved);\n    else if (request(fresh)) CE_SettingSet(key, fresh);", "    const int saved = CE_SettingGetBool(key, -1);\n    if (saved >= 0) request(saved);\n    else request(fresh);", "missing"),
            "refused-request-written": ("policy", "    const int saved = CE_SettingGetBool(key, -1);\n    if (saved >= 0) request(saved);\n    else if (request(fresh)) CE_SettingSet(key, fresh);", "    const int saved = CE_SettingGetBool(key, -1);\n    if (saved >= 0) request(saved);\n    else { request(fresh); CE_SettingSet(key, fresh); }", "rows-refused"),
            "omit-device-defaults-hook": ("policy", "PortCeGraphicsApplyFreshDefaults();", "/* authored control: omit fresh native settings */", "missing"),
            "reverse-texture-quality": ("actual", "textures.SetValue(0);", "textures.SetValue(textures.GetNumChoices() - 1);", "missing"),
        }
        for name, (target, old, new, case) in mutations.items():
            original = policy_text if target == "policy" else actual
            if original.count(old) != 1:
                raise AssertionError("nonunique causal anchor: " + name)
            directory = out / name
            directory.mkdir(exist_ok=True)
            (directory / "ce_graphics_defaults_actual.inc").write_text(actual.replace(old, new) if target == "actual" else actual)
            changed = directory / "policy.cpp"
            changed.write_text(policy_text.replace(old, new) if target == "policy" else policy_text)
            controls[name] = run([compile_one(directory, changed), case], expected=1)
        # A new profile whose first start does not write the choice loses the extra features at its second start: every case of
        # one start still agrees with itself there, so this control runs the two starts.
        old = 'if (saved < 0 && freshOptionsPending) CE_SettingSet("extra_graphics", 1);'
        if policy_text.count(old) != 1:
            raise AssertionError("nonunique causal anchor: fresh-extras-not-kept")
        directory = out / "fresh-extras-not-kept"
        directory.mkdir(exist_ok=True)
        (directory / "ce_graphics_defaults_actual.inc").write_text(actual)
        (directory / "policy.cpp").write_text(policy_text.replace(old, "/* authored control: the new profile's choice is not kept */"))
        unkept = compile_one(directory, directory / "policy.cpp")
        first = run([unkept, "missing"], expected=1)
        if "wrote extra_graphics=1" in first or "state capable=1 extras=0" not in second_start(unkept, first):
            raise AssertionError("the two-start control did not lose the new profile's extra features")
        controls["fresh-extras-not-kept"] = first
    receipt = {"policy_sha256": hashlib.sha256(policy.read_bytes()).hexdigest(), "stand_in_tweaks": stand_ins,
               "patch_sha256": hashlib.sha256((PORT / "patches/0204-ce-graphics-policy.patch").read_bytes()).hexdigest(),
               "actual_sha256": hashlib.sha256(actual.encode()).hexdigest(), "observations": observations, "controls": controls,
               "limits": "Actual native startup-open fragment, getters and setters with authored file/device/tweak boundaries; not a GPU or full startup run."}
    (out / "receipt.private.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(f"graphics defaults: {len(observations)} cases passed; {len(controls)} causal controls detected")


if __name__ == "__main__":
    main()
