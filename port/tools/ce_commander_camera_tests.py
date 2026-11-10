#!/usr/bin/env python3
# SPDX-FileCopyrightText: Onslaught CE contributors
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise only the CE camera owner with an authored consumer.

No original game source, assets, engine process, display or network is used.
Compiler and control output stays in an ignored repo-local run directory.
"""

from __future__ import annotations

import argparse
import ast
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def run_command(command: list[str], logs: Path, name: str) -> bool:
    try:
        result = subprocess.run(command, capture_output=True, timeout=60, check=False)
    except (OSError, subprocess.TimeoutExpired) as error:
        (logs / f"{name}.error.txt").write_text(type(error).__name__ + "\n")
        return False
    (logs / f"{name}.stdout").write_bytes(result.stdout)
    (logs / f"{name}.stderr").write_bytes(result.stderr)
    return result.returncode == 0


def definition(source: str, name: str) -> str:
    pattern = re.compile(
        r"(?m)^[ \t]*(?:static\s+)?(?:extern\s+\"C\"\s+)?(?:const\s+)?"
        r"[A-Za-z_][A-Za-z0-9_:<>]*(?:[ \t]+|[ \t]*[*&][ \t]*)" + re.escape(name)
        + r"\s*\([^;{}]*\)\s*\{"
    )
    match = pattern.search(source)
    if match is None:
        raise ValueError("function definition unavailable")
    depth, cursor = 1, match.end()
    quote = None
    line_comment = block_comment = False
    while cursor < len(source) and depth:
        char, pair = source[cursor], source[cursor:cursor + 2]
        if line_comment:
            if char == "\n":
                line_comment = False
        elif block_comment:
            if pair == "*/":
                block_comment = False
                cursor += 1
        elif quote:
            if char == "\\":
                cursor += 1
            elif char == quote:
                quote = None
        elif pair == "//":
            line_comment = True
            cursor += 1
        elif pair == "/*":
            block_comment = True
            cursor += 1
        elif char in ("\"", "'"):
            quote = char
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
        cursor += 1
    if depth:
        raise ValueError("function body unavailable")
    return source[match.start():cursor]


def marked(source: str, begin: str, end: str) -> str:
    if source.count(begin) != 1 or source.count(end) != 1:
        raise ValueError("source-bound marker unavailable")
    return source.split(begin, 1)[1].split(end, 1)[0]


def integration_fragment(source: str) -> str:
    block = marked(source, "// CE_STRATEGIC_CAMERA_BEGIN:", "// CE_STRATEGIC_CAMERA_END")
    # The marker's description is a comment, not part of the translation unit.
    block = block.split("\n", 1)[1]
    trap = marked(source, "// CE_STRATEGIC_TRAP_BEGIN", "// CE_STRATEGIC_TRAP_END")
    release = re.findall(r"(?m)^\tif \(event == KEY_EVENT_UP\).*return true; \}$",
                         definition(source, "Trap"))
    if len(release) != 1:
        raise ValueError("paused map key-release path unavailable")
    render = marked(source, "// CE_STRATEGIC_RENDER_BEGIN", "// CE_STRATEGIC_RENDER_END")
    navigation = marked(source, "// CE_MAP_PAN_BEGIN", "// CE_MAP_PAN_END")
    return "\n".join([
        definition(source, "Note"), definition(source, "ByOrdinal"),
        definition(source, "Refresh"), definition(source, "CommandableCount"),
        definition(source, "Cycle"), definition(source, "WhyText"), definition(source, "OrderName"), definition(source, "Order"),
        definition(source, "Ns"), definition(source, "SecondsBetween"), definition(source, "FullSpan"), definition(source, "SetView"),
        definition(source, "FitView"), definition(source, "Pan"), block,
        definition(source, "PanMapFrame"), definition(source, "Close"), definition(source, "Open"),
        "bool Trap(WORD key, int event) {\n" + trap + "\n" + release[0] + "\nreturn false;\n}",
        definition(source, "PortCeCmdFrame"), definition(source, "PortCeCmdLevelEnd"),
        definition(source, "PortCeCmdWorldViewToken"), definition(source, "PortCeCmdKey"),
        definition(source, "PortCeCmdRenderCamera"),
        definition(source, "PortCeCmdSuppressFlightHud"),
        "void StrategicRenderPrefix() {\n" + render + "\n}",
        "void MapRenderNavigation() {\n" + navigation + "\n}",
    ])


X87_FLAGS = ["-m32", "-mfpmath=387", "-mno-sse"]


def x87_flags(compiler: str, logs: Path) -> list:
    """The flags that build the host driver as the game's code is built: 32-bit x87 arithmetic (the driver then sets that unit to single
    precision, as Direct3D leaves the game's). Empty where the host cannot build and run 32-bit code (the result then says so)."""
    probe = logs / "x87-probe.cpp"
    probe.write_text('#include <algorithm>\n#include <cmath>\n#include <iostream>\n#include <string>\n#include <vector>\n'
                     'int main() { std::vector<std::string> v{"x"}; std::cout << v.size() + std::floor(1.5) << "\\n"; return 0; }\n')
    exe = logs / "x87-probe"
    built = subprocess.run([compiler, "-std=c++11"] + X87_FLAGS + [str(probe), "-o", str(exe)], capture_output=True)
    if built.returncode != 0:
        return []
    try:
        return X87_FLAGS if subprocess.run([str(exe)], capture_output=True, timeout=30).returncode == 0 else []
    except OSError:
        return []


def integration_controls(repo: Path, compiler: str, logs: Path) -> bool:
    public_flag = public_ce_define(repo)
    public_macro = public_flag[2:].split("=", 1)[0]
    source = (repo / "port" / "game" / "ce_cmdmap.cpp").read_text()
    # build.py compiles port/game with the game's rule, whose include list has no SDL directory on Windows (a Linux
    # build finds SDL's headers in the sysroot, so only a Windows build shows it): the map's wall clock is the port's.
    if re.search(r'(?m)^[ \t]*#[ \t]*include[ \t]*[<"]SDL', source) or "PortClockRawNs()" not in definition(source, "Ns"):
        raise ValueError("the map's wall clock must come from the port, not an SDL header")
    # Project Windows headers define min/max macros: standard headers must be
    # parsed first, and the authored driver injects those macros at the consumer.
    local_header = source.index('#include "')
    if any(source.index(f"#include <{header}>") > local_header
           for header in ("algorithm", "cmath")):
        raise ValueError("standard headers follow project macros")
    (logs / "ce_commander_strategic_actual.inc").write_text(integration_fragment(source))
    patch = (repo / "port" / "patches" / "0200-ce-commander-strategic-camera.patch").read_text()
    hook = "\n".join(line[1:] for line in patch.splitlines()
                     if line.startswith("+") and not line.startswith("+++")) + "\n"
    if hook.count("PortCeCmdRenderCamera(camera, player)") != 1 or f"#ifdef {public_macro}" not in hook:
        raise ValueError("presentation hook unavailable")
    (logs / "ce_commander_strategic_hook.inc").write_text(hook)
    header = (repo / "port" / "game" / "ce_photo.h").read_text()
    query = "PortCePhotoOn() || PortCeCmdSuppressFlightHud()"
    editor_query = " || PortCeEditorSuppressFlightHud()"
    has_editor_scope = editor_query in header
    if has_editor_scope:
        query += editor_query
    if header.count(query) != 1:
        raise ValueError("strategic flight-scope activation unavailable")
    (logs / "ce_photo.h").write_text(header)
    executable = logs / "camera-integration-controls"
    command = [
        compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror", "-pedantic",
        "-DCE_COMMANDER_CAMERA_INTEGRATION", public_flag, "-I", str(logs),
        "-I", str(repo / "port" / "game"),
        str(repo / "port" / "game" / "ce_commander_camera.cpp"),
        str(repo / "port" / "tools" / "ce_commander_camera_tests.cpp"),
        "-o", str(executable),
    ]
    if has_editor_scope:
        command.insert(1, "-DCE_CAMERA_EDITOR_HUD_QUERY")
    x87 = x87_flags(compiler, logs)
    (logs / "x87-single-precision.json").write_text(json.dumps({"single_precision_x87_build": bool(x87), "flags": x87}) + "\n")
    command[1:1] = x87
    if not run_command(command, logs, "integration-compile") or not run_command(
        [str(executable)], logs, "integration-controls"
    ):
        return False
    faithful = [item for item in command if item != public_flag]
    faithful[-1] = str(logs / "camera-hook-disabled-controls")
    if not run_command(faithful, logs, "hook-disabled-compile") or not run_command(
        [faithful[-1]], logs, "hook-disabled-controls"
    ):
        return False
    mutations = {}
    for name, token in (("simulation_pause", "GAME.Pause("), ("pilot_control", "SetToControl(")):
        lines = [line for line in definition(source, "Open").splitlines()
                 if token in line and "!strategic" in line]
        if len(lines) != 1:
            raise ValueError("causal guard unavailable")
        mutations[name] = source.replace(lines[0], lines[0].replace("!strategic", "true"), 1)
    closed = "if (!gOpen || !gStrategic || !gStrategicRender || !gStrategicOwner.Active()) return camera;"
    if source.count(closed) != 1:
        raise ValueError("closed-view guard unavailable")
    mutations["closed_render"] = source.replace(closed, closed.replace("return camera", "return &gStrategicCamera"))
    entry = "if (gOpen && key == DIK_V && event == KEY_EVENT_DOWN)"
    if source.count(entry) != 1:
        raise ValueError("explicit entry guard unavailable")
    mutations["explicit_map_entry"] = source.replace(entry, entry.replace("gOpen && ", ""))
    # The switch as it first shipped (Open's new record with only V marked held): M is marked held and the first M
    # after the V does nothing. And a switch that only clears M: the auto-repeat of an M still held closes the view.
    carried = "memcpy(gDown, held, sizeof(gDown));"
    if source.count(carried) != 1:
        raise ValueError("view-switch key record unavailable")
    mutations["switch_marks_m_held"] = source.replace(carried, "(void)held; gDown[DIK_V] = true;", 1)
    mutations["switch_forgets_a_held_m"] = source.replace(
        carried, "(void)held; gDown[DIK_V] = true; gDown[kOpenKey] = false;", 1)
    centre_x = "const float centreX = gViewX + gViewSpan * 0.5f;"
    centre_y = "const float centreY = gViewY + gViewSpan * 0.5f;"
    if source.count(centre_x) != 1 or source.count(centre_y) != 1:
        raise ValueError("map-centre conversion unavailable")
    mutations["idle_focus_drift"] = source.replace(centre_x, "const float centreX = gViewX;", 1).replace(
        centre_y, "const float centreY = gViewY;", 1)
    focus = "FVector(gViewX + gViewSpan * 0.5f,\n                                      gViewY + gViewSpan * 0.5f, gStrategicFocusZ)"
    if source.count(focus) != 1:
        raise ValueError("strategic map focus unavailable")
    mutations["corner_as_focus"] = source.replace(focus, "FVector(gViewX, gViewY, gStrategicFocusZ)", 1)
    if source.count("PortCeDrawHooks(true);") != 1:
        raise ValueError("live draw-filter entry unavailable")
    mutations["live_filter_disabled"] = source.replace("PortCeDrawHooks(true);", "PortCeDrawHooks(false);", 1)
    if source.count("PortCeDrawHooks(PortCePhotoOn());") != 1:
        raise ValueError("live draw-filter restoration unavailable")
    mutations["live_filter_not_restored"] = source.replace("PortCeDrawHooks(PortCePhotoOn());", "PortCeDrawHooks(true);", 1)
    action = definition(source, "StrategicSquadAction")
    if action.count("Refresh();") != 1 or action.count("!c.live_pilot") != 1:
        raise ValueError("live squad action revalidation unavailable")
    mutations["stale_live_selection"] = source.replace(action, action.replace("    Refresh();\n", "", 1), 1)
    mutations["dead_pilot_order"] = source.replace(action, action.replace(" || !c.live_pilot", "", 1), 1)
    live_keys = "!repeat && (key == DIK_TAB || key == DIK_C || key == DIK_H || key == DIK_X || key == DIK_A)"
    if source.count(live_keys) != 1:
        raise ValueError("live order repeat guard unavailable")
    mutations["repeated_live_order"] = source.replace(live_keys, live_keys.replace("!repeat && ", ""), 1)
    pointer = definition(source, "StrategicPointer")
    if pointer.count("Refresh();") != 1 or pointer.count("if (!StrategicCanAct()) return;") != 1:
        raise ValueError("live pointer authority anchors unavailable")
    mutations["stale_pointer_selection"] = source.replace(pointer, pointer.replace("    Refresh();", "    (void)0;", 1), 1)
    mutations["pointer_skips_context"] = source.replace(pointer, pointer.replace("if (!StrategicCanAct()) return;", "if (false) return;", 1), 1)
    entry_key = definition(source, "PortCeCmdKey")
    if entry_key.count("Open(true);") != 1:
        raise ValueError("direct live entry anchor unavailable")
    mutations["direct_entry_pauses"] = source.replace(entry_key, entry_key.replace("Open(true);", "Open(false);", 1), 1)
    pan = definition(source, "PanMapFrame")
    pan_cases = {
        "map_pan_per_frame": (
            "static_cast<float>(elapsed * (0.012 * 60.0))", "((void)elapsed, 0.012f)"),
        "map_pan_idle_accumulation": (
            "gMapLastNs = now;", "if (gDown[DIK_RIGHT]) gMapLastNs = now;"),
        "map_pan_unbounded_gap": (
            "(std::min)(0.1, SecondsBetween(gMapLastNs, now))", "SecondsBetween(gMapLastNs, now)"),
        "map_pan_live_view": ("if (!gOpen || gStrategic) return;", "if (!gOpen) return;"),
        "map_pan_closed_view": ("if (!gOpen || gStrategic) return;", "if (gStrategic) return;"),
    }
    for name, (before, after) in pan_cases.items():
        if pan.count(before) != 1:
            raise ValueError("map navigation causal anchor unavailable")
        mutations[name] = source.replace(pan, pan.replace(before, after, 1), 1)
    advance = "    StrategicAdvance();\n    return &gStrategicCamera;"
    if source.count(advance) != 1:
        raise ValueError("render-time camera anchor unavailable")
    mutations["live_camera_only_on_updates"] = source.replace(advance, "    return &gStrategicCamera;", 1)
    once = "if (gStrategicTickOnly || frame == gStrategicAdvancedAt) return;"
    if source.count(once) != 1:
        raise ValueError("render-time camera guard unavailable")
    mutations["live_camera_moves_twice_a_frame"] = source.replace(once, "if (gStrategicTickOnly) return;", 1)
    mutations["live_camera_ignores_stepped_switch"] = source.replace(once, "if (frame == gStrategicAdvancedAt) return;", 1)
    held_now = "if (held >= 0) return held != 0;"
    if source.count(held_now) != 1:
        raise ValueError("held-key query anchor unavailable")
    mutations["camera_ignores_the_keyboard_state"] = source.replace(held_now, "(void)held;", 1)
    mutations["camera_unknown_key_is_up"] = source.replace(held_now, "return held > 0;", 1)
    mutations["camera_ignores_the_message_switch"] = source.replace("if (!gStrategicMessageKeys) {", "if (true) {", 1)
    pan_keys = definition(source, "PanMapFrame")
    if pan_keys.count("CameraKeyDown(DIK_RIGHT)") != 1:
        raise ValueError("map pan key anchor unavailable")
    mutations["map_pan_reads_only_messages"] = source.replace(
        pan_keys, pan_keys.replace("CameraKeyDown(DIK_RIGHT)", "gDown[DIK_RIGHT]", 1), 1)
    # Which way each key moves the view (the sign of every axis the camera is given, and of the paused map's pan): a pair of keys swapped, or two axes exchanged.
    key_table = {
        "pan_horizontal_keys_swapped": ("double(CameraKeyDown(DIK_RIGHT)) - double(CameraKeyDown(DIK_LEFT))",
                                        "double(CameraKeyDown(DIK_LEFT)) - double(CameraKeyDown(DIK_RIGHT))"),
        "pan_vertical_keys_swapped": ("double(CameraKeyDown(DIK_UP) || CameraKeyDown(DIK_W)) - double(CameraKeyDown(DIK_DOWN) || CameraKeyDown(DIK_S))",
                                      "double(CameraKeyDown(DIK_DOWN) || CameraKeyDown(DIK_S)) - double(CameraKeyDown(DIK_UP) || CameraKeyDown(DIK_W))"),
        "orbit_keys_swapped": ("double(CameraKeyDown(DIK_E)) - double(CameraKeyDown(DIK_Q))", "double(CameraKeyDown(DIK_Q)) - double(CameraKeyDown(DIK_E))"),
        "tilt_keys_swapped": ("double(CameraKeyDown(DIK_NEXT)) - double(CameraKeyDown(DIK_PRIOR))",
                              "double(CameraKeyDown(DIK_PRIOR)) - double(CameraKeyDown(DIK_NEXT))"),
        "zoom_keys_swapped": ("double(CameraKeyDown(DIK_R)) - double(CameraKeyDown(DIK_F))", "double(CameraKeyDown(DIK_F)) - double(CameraKeyDown(DIK_R))"),
        "orbit_on_the_tilt_axis": ("input.orbit_horizontal = double(CameraKeyDown(DIK_E))", "input.orbit_vertical = double(CameraKeyDown(DIK_E))"),
        "map_pan_horizontal_swapped": ("(float(right) - float(left)) * distance", "(float(left) - float(right)) * distance"),
        "map_pan_vertical_swapped": ("(float(down) - float(up)) * distance", "(float(up) - float(down)) * distance"),
    }
    for name, (before, after) in key_table.items():
        if source.count(before) != 1:
            raise ValueError("key direction anchor unavailable")
        mutations[name] = source.replace(before, after, 1)
    virtual_ms = "if (PortClockVirtual()) return static_cast<std::uint64_t>(PortPresentCount()) * 33333333ull;"
    if source.count(virtual_ms) != 1:
        raise ValueError("virtual map clock unavailable")
    mutations["map_clock_ignores_virtual"] = source.replace(virtual_ms, "", 1)
    reset = "gMapLastNs = Ns();"
    if source.count(reset) != 1:
        raise ValueError("map navigation open-clock reset unavailable")
    mutations["map_pan_stale_open_clock"] = source.replace(reset, "(void)Ns();", 1)
    between = definition(source, "SecondsBetween")
    if between.count("to > from ? double(to - from) * 1e-9 : 0.0") != 1:
        raise ValueError("elapsed-time guard unavailable")
    mutations["map_pan_backwards_time"] = source.replace(
        between, between.replace("to > from ? double(to - from) * 1e-9 : 0.0", "double(to - from) * 1e-9", 1), 1)
    # The clock as a double of milliseconds since boot: exact on a host's SSE doubles, a multiple of 16 ms after a day and a half under the
    # game's single-precision x87 unit (the driver sets that unit up where the host can build 32-bit x87 code).
    mutations["clock_as_double_milliseconds"] = source.replace(
        between, between.replace("return to > from ? double(to - from) * 1e-9 : 0.0;",
                                 "return to > from ? (double(to) * 1e-6 - double(from) * 1e-6) * 0.001 : 0.0;", 1), 1)
    pan_hook = marked(source, "// CE_MAP_PAN_BEGIN", "// CE_MAP_PAN_END")
    if pan_hook.count("PanMapFrame();") != 1:
        raise ValueError("map navigation render hook unavailable")
    mutations["map_pan_missing_render_hook"] = source.replace(
        pan_hook, pan_hook.replace("PanMapFrame();", "(void)0;", 1), 1)
    release = "if (key < 256) gDown[key] = false;"
    if source.count(release) != 1:
        raise ValueError("map navigation release anchor unavailable")
    mutations["map_pan_held_after_release"] = source.replace(release, "if (key < 256) (void)key;", 1)
    results = {}
    for name, changed in mutations.items():
        folder = logs / name
        folder.mkdir()
        (folder / "ce_commander_strategic_actual.inc").write_text(integration_fragment(changed))
        (folder / "ce_commander_strategic_hook.inc").write_text(hook)
        variant = [str(folder) if item == str(logs) else item for item in command]
        variant[-1] = str(folder / "controls")
        compiled = run_command(variant, folder, "compile")
        rejected = False
        if compiled:
            result = subprocess.run([variant[-1]], capture_output=True, timeout=60, check=False)
            (folder / "controls.stdout").write_bytes(result.stdout)
            (folder / "controls.stderr").write_bytes(result.stderr)
            rejected = result.returncode == 1 and b"authored integration control failed" in result.stderr
        results[name] = {"compiled": compiled, "rejected": rejected}
    (logs / "integration-causal.json").write_text(json.dumps(results, indent=2) + "\n")
    macro_folder = logs / "legacy-macro-control"
    macro_folder.mkdir()
    unsafe = source.replace("(std::min)(", "std::min(")
    if unsafe == source:
        raise ValueError("macro compatibility control unavailable")
    (macro_folder / "ce_commander_strategic_actual.inc").write_text(integration_fragment(unsafe))
    (macro_folder / "ce_commander_strategic_hook.inc").write_text(hook)
    macro_command = [str(macro_folder) if item == str(logs) else item for item in command]
    macro_command[-1] = str(macro_folder / "controls")
    macro_rejected = not run_command(macro_command, macro_folder, "compile")
    (logs / "macro-compatibility.json").write_text(json.dumps({
        "safe_consumer_compiled": True, "unsafe_calls_rejected": macro_rejected,
    }, indent=2) + "\n")
    guard_folder = logs / "compiled-out-hook-control"
    guard_folder.mkdir()
    (guard_folder / "ce_commander_strategic_actual.inc").write_text(integration_fragment(source))
    (guard_folder / "ce_commander_strategic_hook.inc").write_text(
        hook.replace(f"#ifdef {public_macro}", "#ifdef BEA_PORT_CE_LAYER", 1)
    )
    guard_command = [str(guard_folder) if item == str(logs) else item for item in command]
    guard_command[-1] = str(guard_folder / "controls")
    guard_compiled = run_command(guard_command, guard_folder, "compile")
    guard_rejected = False
    if guard_compiled:
        result = subprocess.run([guard_command[-1]], capture_output=True, timeout=60, check=False)
        (guard_folder / "controls.stdout").write_bytes(result.stdout)
        (guard_folder / "controls.stderr").write_bytes(result.stderr)
        guard_rejected = result.returncode == 1 and b"authored integration control failed" in result.stderr
    (logs / "production-guard.json").write_text(json.dumps({
        "production_macro": public_macro,
        "compiled_out_variant_compiled": guard_compiled,
        "compiled_out_variant_rejected": guard_rejected,
    }, indent=2) + "\n")
    hud_folder = logs / "flight-hud-scope-control"
    hud_folder.mkdir()
    (hud_folder / "ce_commander_strategic_actual.inc").write_text(integration_fragment(source))
    (hud_folder / "ce_commander_strategic_hook.inc").write_text(hook)
    (hud_folder / "ce_photo.h").write_text(header.replace(query, "PortCePhotoOn()", 1))
    hud_command = [str(hud_folder) if item == str(logs) else item for item in command]
    hud_command[-1] = str(hud_folder / "controls")
    hud_compiled = run_command(hud_command, hud_folder, "compile")
    hud_rejected = False
    if hud_compiled:
        result = subprocess.run([hud_command[-1]], capture_output=True, timeout=60, check=False)
        (hud_folder / "controls.stdout").write_bytes(result.stdout)
        (hud_folder / "controls.stderr").write_bytes(result.stderr)
        hud_rejected = result.returncode == 1 and b"authored integration control failed" in result.stderr
    (logs / "flight-hud.json").write_text(json.dumps({
        "photo_only_scope_compiled": hud_compiled,
        "photo_only_scope_rejected": hud_rejected,
    }, indent=2) + "\n")
    editor_scope_passed = True
    if has_editor_scope:
        editor_folder = logs / "editor-flight-hud-scope-control"
        editor_folder.mkdir()
        (editor_folder / "ce_commander_strategic_actual.inc").write_text(integration_fragment(source))
        (editor_folder / "ce_commander_strategic_hook.inc").write_text(hook)
        (editor_folder / "ce_photo.h").write_text(header.replace(editor_query, "", 1))
        editor_command = [str(editor_folder) if item == str(logs) else item for item in command]
        editor_command[-1] = str(editor_folder / "controls")
        editor_compiled = run_command(editor_command, editor_folder, "compile")
        editor_rejected = False
        if editor_compiled:
            result = subprocess.run([editor_command[-1]], capture_output=True, timeout=60, check=False)
            (editor_folder / "controls.stdout").write_bytes(result.stdout)
            (editor_folder / "controls.stderr").write_bytes(result.stderr)
            editor_rejected = result.returncode == 1 and b"authored integration control failed" in result.stderr
        editor_scope_passed = editor_compiled and editor_rejected
        (logs / "editor-flight-hud.json").write_text(json.dumps({
            "editor_query_present": True, "missing_editor_scope_compiled": editor_compiled,
            "missing_editor_scope_rejected": editor_rejected,
        }, indent=2) + "\n")
    return macro_rejected and guard_compiled and guard_rejected and hud_compiled and hud_rejected and editor_scope_passed and all(
        result["compiled"] and result["rejected"] for result in results.values()
    )


def public_ce_define(repo: Path) -> str:
    """Bind the authored engine hook to the production CE build definition."""
    tree = ast.parse((repo / "port" / "build.py").read_text())
    definitions = []
    for node in ast.walk(tree):
        if not isinstance(node, ast.Call) or not isinstance(node.func, ast.Attribute):
            continue
        if node.func.attr != "append" or not isinstance(node.func.value, ast.Name):
            continue
        if node.func.value.id != "CE_DEFINES":
            continue
        for argument in node.args:
            for value in ast.walk(argument):
                if isinstance(value, ast.Constant) and isinstance(value.value, str):
                    match = re.search(r"-D(CE_VERSION)=", value.value)
                    if match:
                        definitions.append(match.group(1))
    if definitions != ["CE_VERSION"]:
        raise ValueError("production CE definition unavailable or ambiguous")
    return "-D" + definitions[0] + '=\"authored-camera-controls\"'


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default="c++", help="Native C++ compiler executable")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    artifact_root = repo / "local-data" / "commander-camera-tests"
    artifact_root.mkdir(parents=True, exist_ok=True)
    logs = Path(tempfile.mkdtemp(prefix="run-", dir=artifact_root))
    compiler = shutil.which(args.cxx)
    if compiler is None:
        print(json.dumps({"result": "compiler unavailable", "logs": str(logs.relative_to(repo))}))
        return 1
    executable = logs / "camera-owner-controls"
    command = [
        compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror", "-pedantic",
        "-I", str(repo / "port" / "game"),
        str(repo / "port" / "game" / "ce_commander_camera.cpp"),
        str(repo / "port" / "tools" / "ce_commander_camera_tests.cpp"),
        "-o", str(executable),
    ]
    compiled = run_command(command, logs, "compile")
    owner_passed = compiled and run_command([str(executable)], logs, "controls")
    integrated = owner_passed and integration_controls(repo, compiler, logs)
    passed = owner_passed and integrated
    print(json.dumps({
        "result": "passed" if passed else "failed",
        "scope": "authored owner and source-bound camera integration",
        "compiled": compiled,
        "owner_passed": owner_passed,
        "integration_passed": integrated,
        "logs": str(logs.relative_to(repo)),
    }))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
