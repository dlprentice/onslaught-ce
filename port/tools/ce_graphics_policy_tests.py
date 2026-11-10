#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile the graphics policy and actual patched water/menu callers against authored native boundaries.

No game, display or platform dependencies. Private generated snippets and logs remain under local-data/.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import resource
import subprocess

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "port"
SUPPORT = PORT / "tools/tests"
CASES = ("supported", "pixel-old", "vertex-old", "formats-missing", "float-fallback", "opt-out", "legacy-disabled",
         "pixel-create-failed", "vertex-create-failed", "vertex-native-failed", "landscape-failed", "particles-failed",
         "landscape-portable", "landscape-readable-surface", "landscape-readable-texture",
         "landscape-pixel-failed", "landscape-texture-failed", "landscape-float-failed", "landscape-depth-failed", "device-failed", "save-refused", "save-off", "layer-off", "fence-CLOCK", "fence-TAPE", "fence-PROBES", "fence-RECORD")


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded


def run(command, expected=0):
    def bounded():
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    env = os.environ.copy()
    for name in ("BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD", "DISPLAY", "WAYLAND_DISPLAY", "DBUS_SESSION_BUS_ADDRESS"):
        env.pop(name, None)
    result = subprocess.run([str(x) for x in command], text=True, capture_output=True, env=env, timeout=45, preexec_fn=bounded)
    if result.returncode != expected:
        raise RuntimeError(f"command failed: {command}\n{result.stdout}{result.stderr}")
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--causal-controls", action="store_true")
    args = parser.parse_args()
    out = ROOT / "local-data/graphics-policy-tests"
    out.mkdir(parents=True, exist_ok=True)
    build = module("graphics_build", PORT / "build.py")
    helpers = module("graphics_functions", PORT / "tools/public_d3dapp_tests.py")
    texts = build.patched_text(["DXWater.cpp", "DXLandscape.cpp", "PauseMenu.cpp"])
    actual = helpers.function(texts["DXWater.cpp"], r"^HRESULT\s+CDXWater::InitDeviceObjects\s*\(") + "\n"
    actual += helpers.function(texts["DXLandscape.cpp"], r"^HRESULT\s+CDXLandscape::InitDeviceObjects\s*\(") + "\n"
    actual += helpers.function(texts["PauseMenu.cpp"], r"^void\s+CnVidia::SetValue\s*\(") + "\n"
    (out / "ce_graphics_actual.inc").write_text(actual)
    common = ["clang++", "-std=c++14", "-DBEA_GRAPHICS_POLICY_TEST", "-I", SUPPORT, "-I", PORT / "game",
              "-I", PORT / "platform/common", "-I", out]
    policy = PORT / "game/ce_graphics_policy.cpp"
    driver = SUPPORT / "ce_graphics_policy_driver.cpp"
    binary = out / "driver"
    run(common + ["-DCE_VERSION=1", policy, driver, "-o", binary])
    observations = {name: run([binary, name]) for name in CASES}
    private = out / "private-driver"
    run(common + [policy, driver, "-o", private])
    observations["private"] = run([private, "private"])
    helpers.recovery_source(out / "host-callers", True)
    host_common = common + ["-DBEA_PUBLIC_D3DAPP_TEST", "-DBEA_PUBLIC_GRAPHICS_HOST=1", "-DCE_VERSION=1", "-I", out / "host-callers"]
    host = out / "host-driver"
    run(host_common + [PORT / "platform/public/d3dapp.cpp", policy, SUPPORT / "ce_graphics_host_driver.cpp",
                       SUPPORT / "public_display_mode_stubs.cpp", "-o", host])
    host_cases = ("modern", "old-pixel", "old-vertex", "float-fallback", "no-float", "float-unused", "no-mirror", "no-depth",
                  "bad-depth-match", "physical-adapter", "faithful")
    observations.update({"host-" + name: run([host, name]) for name in host_cases})
    controls = {}
    if args.causal_controls:
        source = policy.read_text()
        changes = {
            "ignore-capabilities": ("capable = (pixelVersion & 0xffffu) >= 0x0200u && (vertexVersion & 0xffffu) >= 0x0101u && formats;", "capable = true;", "formats-missing"),
            "ignore-resource-failure": ("EXTRA_GEFORCE_FX_FEATURES.Set(0.f);", "/* authored control: leave incomplete resources enabled */", "pixel-create-failed"),
            "ignore-faithful-fence": ("if (value && *value) return false;", "/* authored control: admit faithful graphics policy */", "fence-CLOCK"),
            "ignore-opt-out": ('CE_SettingGetBool("extra_graphics", freshOptionsPending || EXTRA_GEFORCE_FX_FEATURES.mValue != 0) != 0', "true", "opt-out"),
        }
        for name, (old, new, case) in changes.items():
            if source.count(old) != 1:
                raise AssertionError(f"nonunique causal anchor: {name}")
            changed = out / f"{name}.cpp"
            changed.write_text(source.replace(old, new))
            executable = out / name
            run(common + ["-DCE_VERSION=1", changed, driver, "-o", executable])
            controls[name] = run([executable, case], expected=1)
        # Removing the actual caller's observer must fail even though the policy itself is unchanged.
        observer = "PortCeGraphicsResourceOutcome(CE_GRAPHICS_WATER, sShader && sShader->mShader && sPixelShader);"
        if actual.count(observer) != 1:
            raise AssertionError("nonunique actual water observer")
        directory = out / "no-water-observer"
        directory.mkdir(exist_ok=True)
        (directory / "ce_graphics_actual.inc").write_text(actual.replace(observer, "/* authored control: omit resource outcome */"))
        executable = directory / "driver"
        altered = common[:-2] + ["-I", directory]
        run(altered + ["-DCE_VERSION=1", policy, driver, "-o", executable])
        controls["no-water-observer"] = run([executable, "pixel-create-failed"], expected=1)
        landscape_controls = {
            "require-unused-floating-target": ("(!MY_READABLE_Z_BUFFER.mValue || mUnknown08)", "mUnknown08", "landscape-portable"),
            "ignore-required-floating-target": ("(!MY_READABLE_Z_BUFFER.mValue || mUnknown08)", "true", "landscape-float-failed"),
        }
        for label, (old, new, case) in landscape_controls.items():
            if actual.count(old) != 1:
                raise AssertionError("nonunique landscape resource requirement: " + label)
            directory = out / label
            directory.mkdir(exist_ok=True)
            (directory / "ce_graphics_actual.inc").write_text(actual.replace(old, new))
            executable = directory / "driver"
            altered = common[:-2] + ["-I", directory]
            run(altered + ["-DCE_VERSION=1", policy, driver, "-o", executable])
            controls[label] = run([executable, case], expected=1)
        host_source = (PORT / "platform/public/d3dapp.cpp").read_text()
        old = "GraphicsCapabilities(*this);"
        if host_source.count(old) != 1:
            raise AssertionError("nonunique actual capability hook")
        changed = out / "no-host-capability-hook.cpp"
        changed.write_text(host_source.replace(old, "/* authored control: omit actual device capability policy */"))
        executable = out / "no-host-capability-hook"
        run(host_common + [changed, policy, SUPPORT / "ce_graphics_host_driver.cpp",
                           SUPPORT / "public_display_mode_stubs.cpp", "-o", executable])
        controls["no-host-capability-hook"] = run([executable, "modern"], expected=1)
    receipt = {"policy_sha256": hashlib.sha256(policy.read_bytes()).hexdigest(),
               "actual_sha256": hashlib.sha256(actual.encode()).hexdigest(),
               "observations": observations, "controls": controls,
               "limits": "Production policy and actual selected resource/menu callers with synthetic D3D/settings boundaries; no GPU evidence."}
    (out / "receipt.private.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(f"graphics policy: {len(observations)} cases passed; {len(controls)} causal controls detected")


if __name__ == "__main__":
    main()
