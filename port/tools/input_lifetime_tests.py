#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check production input shims and extracted game callers without a game, window or GPU.

Uses the pinned SDL library's virtual gamepads; only owned virtual IDs are exposed to enumeration. Mouse focus,
relative counts and window geometry are bounded API fakes. Complete InitDirectInput, UpdateJoystick, enumeration,
PCController axis, mouse poll/message and ShutdownMouse bodies come from source after the port patch queue.
Receipts retain source/function hashes, compiler output and every case under ignored local-data/port/.
--source-root can select an unchanged checkout to reproduce the defects. This is caller-level evidence, not a
physical controller/mouse, compositor, native display or player-feel test. No retail inputs are read.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

import deps


ROOT = Path(__file__).resolve().parents[2]
SOURCES = ("ltshell.cpp", "PCController.cpp", "Controller.cpp", "mouse.cpp")
FUNCTIONS = {
    "INIT": ("ltshell.cpp", r"HRESULT PCLTShell::InitDirectInput\( HWND hDlg \)"),
    "ENUM": ("ltshell.cpp", r"BOOL CALLBACK PCLTShell::EnumJoysticksCallback\([^\n]*"),
    "AXES": ("ltshell.cpp", r"BOOL CALLBACK PCLTShell::EnumAxesCallback\([^\n]*"),
    "UPDATE": ("ltshell.cpp", r"HRESULT PCLTShell::UpdateJoystick\(int joypad, bool first\)"),
    **{f"JOY_{axis}": ("PCController.cpp", rf"float\s+CPCController::GetJoyAnalogue{axis}\(int pad_number\)")
       for axis in ("LeftX", "LeftY", "RightX", "RightY")},
    "POLL_MOUSE": ("Controller.cpp", r"HRESULT PollMouseState\(\)"),
    "POLL_MOTION": ("Controller.cpp", r"HRESULT PollMouseMotion\(\)"),
    "SHUTDOWN_MOUSE": ("Controller.cpp", r"HRESULT ShutdownMouse\(\)"),
    "MOUSE_MESSAGE": ("mouse.cpp", r"LRESULT __stdcall\s+HandleMouseMessage\([^\n]*"),
}
CASES = ("pad-acquire", "pad-reacquire", "pad-properties", "pad-axes", "pad-limit", "pad-quick-tap", "pad-edge-order", "pad-edge-chord", "pad-edge-chord-pulses",
         "pad-edge-overflow", "pad-edge-event-overflow", "pad-edge-overflow-focus", "pad-edge-focus", "pad-edge-stale-focus", "pad-edge-hotplug",
         "pad-edge-two", "pad-edge-cross-binding", "pad-edge-two-consumers", "pad-edge-repeat-consumer", "pad-edge-bonus-two",
         "pad-edge-no-poll-held", "pad-edge-duplicate-binding", "pad-edge-no-watch", "pad-edge-missing-focus-event",
         "pad-edge-cross-overflow", "pad-edge-trigger", "pad-edge-multiplayer", "pad-edge-tape", "pad-edge-record", "pad-edge-clock",
         "pad-edge-probes", "mouse-exclusive", "relative-focus", "wheel-stale", "wheel-focus",
         "wheel-race", "wheel-menu", "wheel-before-acquire",
         "wheel-once", "wheel-focus-pending", "wheel-window", "release-overflow", "release-repress", "record-release-overflow",
         "record-mouse", "replay-key", "replay-mouse", "pause-cursor", "pause-tape", "pause-no-focus",
         "mouse-fraction", "mouse-fraction-mixed", "mouse-fraction-reset", "mouse-fraction-focus", "mouse-fraction-pending-focus", "mouse-fraction-no-focus",
         "mouse-fraction-invalid", "mouse-fraction-tape", "mouse-fraction-record",
         "mouse-fraction-clock", "mouse-fraction-probes", "mouse-fraction-disabled",
         "pad-trigger-edges", "pad-trigger-build-mode", "pad-trigger-unplug", "pad-trigger-disabled", "pad-trigger-tape", "pad-trigger-record",
         "pad-trigger-clock", "pad-trigger-probes", "pad-tutorial-info", "pad-tutorial-unplug")
FAITHFUL_CASES = ("pad-trigger-build-mode", "pad-trigger-disabled", "pad-trigger-tape", "pad-trigger-record",
                  "pad-trigger-clock", "pad-trigger-probes", "mouse-fraction-disabled", "mouse-fraction-tape")


def sha(data):
    return hashlib.sha256(data).hexdigest()


def definition(source, signature):
    found = re.findall(r"^" + signature + r"[^\n]*\n\{\n.*?^\}[ \t]*", source, re.MULTILINE | re.DOTALL)
    if len(found) != 1:
        raise RuntimeError(f"expected one complete function: {signature}")
    return found[0] + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--case", choices=CASES, action="append")
    parser.add_argument("--faithful", action="store_true", help="compile without CE_VERSION for focused faithful controls")
    parser.add_argument("--optimize", action="store_true", help="compile with -O2, as the game is (the DIPROP folding showed only there)")
    args = parser.parse_args()
    if args.faithful and (not args.case or any(case not in FAITHFUL_CASES for case in args.case)):
        parser.error("--faithful requires only explicit trigger-fence cases")
    parent = ROOT / "local-data/port/input-lifetime-tests"
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    receipt = {"source_root": str(args.source_root.resolve()), "patches": [], "cases": [],
               "limits": ["Actual production shims and extracted game callers; SDL virtual gamepads.",
                          "Mouse/focus/geometry are API fakes; no native window, GPU or physical-device evidence."]}
    passed = False
    try:
        sources = {name: (args.source_root / "src" / name).read_bytes() for name in SOURCES}
        receipt["source_sha256"] = {name: sha(data) for name, data in sources.items()}
        for name, data in sources.items():
            (out / name).write_bytes(data)
        for path in sorted((args.source_root / "port/patches").glob("*.patch")):
            data = path.read_bytes()
            sections = [section for section in re.split(br"(?=^--- a/)", data, flags=re.MULTILINE)
                        if any(section.startswith(f"--- a/{name}\n+++ b/{name}\n".encode()) for name in SOURCES)]
            if sections:
                result = subprocess.run(["patch", "--batch", "--forward", "--fuzz=0", "-p1", "-d", str(out), "-r", "-"],
                                        input=b"".join(sections), capture_output=True, timeout=10)
                receipt["patches"].append({"name": path.name, "sha256": sha(data), "exit": result.returncode,
                                           "output": (result.stdout + result.stderr).decode(errors="replace")})
                if result.returncode:
                    raise RuntimeError(f"patch failed: {path.name}")
        bodies = {name: definition((out / file).read_text(encoding="latin-1"), signature)
                  for name, (file, signature) in FUNCTIONS.items()}
        receipt["functions_sha256"] = {name: sha(body.encode()) for name, body in bodies.items()}
        calls = out / "callers.inc"
        calls.write_text("\n".join(bodies.values()))
        header = (args.source_root / "src/ltshell.h").read_text(encoding="latin-1")
        accessors = []
        for name in ("JoyButtonOnce", "JoyButtonOn", "JoyButtonRelease"):
            found = re.findall(r"\bBOOL\s+" + name + r"\([^)]*\)\s*\{[^}]*\}", header, re.DOTALL)
            if len(found) != 1:
                raise RuntimeError(f"expected one inline input accessor: {name}")
            accessors.append(found[0])
        receipt["input_accessors_sha256"] = sha("\n".join(accessors).encode())
        shims = {}
        for name in ("window.cpp", "dinput.cpp", "portlog.cpp", "inventory.cpp"):
            data = (args.source_root / "port/platform/common" / name).read_bytes()
            (out / name).write_bytes(data)
            shims[name] = sha(data)
        receipt["shims_sha256"] = shims
        driver = (ROOT / "port/tools/tests/input_lifetime_driver.cpp").read_text()
        driver = driver.replace("JOY_BUTTON_ACCESSORS", "\n".join(accessors))
        for name, path in {"WINDOW_CPP": out / "window.cpp", "DINPUT_CPP": out / "dinput.cpp",
                           "PORTLOG_CPP": out / "portlog.cpp", "INVENTORY_CPP": out / "inventory.cpp", "CALLERS_INC": calls}.items():
            driver = driver.replace(name, str(path))
        edge_test = any(case == "pad-quick-tap" or case.startswith("pad-edge-") for case in (args.case or CASES))
        edge_source = ""
        if edge_test:
            gamepad = (args.source_root / "port/game/ce_gamepad.cpp").read_text()
            edge_bodies = [definition(gamepad, sig) for sig in (r"bool Ordinary\(\)", r"bool Allowed\(int pad\)",
                r"void PortCeGamepadUpdate\(int pad\)", r"void PortCeGamepadBeginCycle\(\)",
                r"void PortCeGamepadUpdateAll\(int consumer\)", r"int PortCeGamepadEdge\(int pad, int button, bool release\)",
                r"int PortCeGamepadOn\(int pad, int button\)")]
            receipt["gamepad_edge_functions_sha256"] = {str(i): sha(body.encode()) for i, body in enumerate(edge_bodies)}
            edge_source = ('namespace { const int kButtons = 32; unsigned char sPrevious[MAX_JOYPADS][kButtons], '
                           'sCurrent[MAX_JOYPADS][kButtons]; bool sCycleReady; unsigned sCycleConsumers;\n' + '\n'.join(edge_bodies[:2]) + '\n}\n' +
                           '\n'.join(edge_bodies[2:]))
        driver = driver.replace("GAMEPAD_EDGE_FUNCTIONS", edge_source)
        mapping_source = ""
        if edge_test:
            controller = (out / "Controller.cpp").read_text(encoding="latin-1")
            mapping_source = definition(controller, r"void CController::Flush\(\)") + definition(controller, r"void CController::DoMappings\(\)")
            pccontroller = (out / "PCController.cpp").read_text(encoding="latin-1")
            for name in ("Once", "On", "Release"):
                found = [line for line in re.findall(r"^inline int CPCController::GetJoyButton" + name + r"\([^\n]*", pccontroller, re.MULTILINE)
                         if "PortCeGamepad" in line]
                if len(found) != 1:
                    raise RuntimeError(f"expected one CE gamepad accessor: {name}")
                mapping_source += "\n" + found[0] + "\n"
            receipt["controller_mapping_sha256"] = sha(mapping_source.encode())
        driver = driver.replace("GAMEPAD_MAPPING_FUNCTIONS", mapping_source)
        cpp = out / "driver.cpp"
        cpp.write_text(driver)
        receipt["driver_sha256"] = sha(cpp.read_bytes())
        binary = out / "driver"
        command = [str(deps.DATA / "llvm/bin/clang++"), "--target=i686-pc-linux-gnu", "-m32", "-std=c++17",
                   "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-fshort-wchar",
                   "-fms-extensions", "-fno-autolink", "-w", "-O2" if args.optimize else "-O0",
                   *([] if args.faithful else ['-DCE_VERSION="input-test"']), *(['-DCE_EDGE_TESTS'] if edge_test else []),
                   f"-I{deps.PORT / 'include'}", f"-I{deps.PORT / 'include/linux'}", f"-I{deps.DATA / 'dxvk-native/usr/include/dxvk'}",
                   f"-I{deps.PREFIX / 'include'}", f"-I{args.source_root / 'port/platform/common'}",
                   str(cpp), f"-L{deps.PREFIX / 'lib'}", "-lSDL3", f"-Wl,-rpath,{deps.PREFIX / 'lib'}", "-o", str(binary)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=60,
                                env=dict(os.environ, TMPDIR=str(out)))
        receipt["compile"] = {"command": command, "exit": result.returncode, "output": result.stdout + result.stderr}
        if result.returncode:
            raise RuntimeError("input driver did not compile")
        env = {key: value for key, value in os.environ.items() if not key.startswith(("BEA_PORT_", "SDL_"))}
        env.update(SDL_VIDEO_DRIVER="dummy", SDL_AUDIO_DRIVER="dummy", SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS="1")
        for case in args.case or CASES:
            result = subprocess.run([str(binary), case], env=env, capture_output=True, text=True, timeout=10)
            observation = {"case": case, "exit": result.returncode, "stdout": result.stdout,
                           "stderr": result.stderr, "pass": result.returncode == 0}
            receipt["cases"].append(observation)
            print(f"{'PASS' if observation['pass'] else 'FAIL'} {case}")
        passed = all(case["pass"] for case in receipt["cases"])
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        receipt["error"] = str(error)
        print(f"FAIL {error}")
    receipt["pass"] = passed
    (out / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(out / "result.json")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
