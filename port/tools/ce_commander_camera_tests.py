#!/usr/bin/env python3
# SPDX-FileCopyrightText: Onslaught CE contributors
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the CE camera and its production keybinding and factory consumers with an authored world.

No original game source, assets, engine process, display or network is used.
Compiler and control output stays in an ignored repo-local run directory.
"""

from __future__ import annotations

import argparse
import ast
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def default_jobs() -> int:
    env = os.environ.get("BEA_MUTANT_JOBS", "").strip()
    if env:
        value = int(env)
        if value < 1:
            raise ValueError(f"BEA_MUTANT_JOBS must be >= 1, got {value}")
        return value
    return min(6, os.cpu_count() or 1)


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


def native_scene_owner_token(text):
    marker = "uint64_t PortCeCmdWorldViewToken()"
    start = text.index(marker)
    opening = text.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end] + "\n"


def integration_fragment(source: str) -> str:
    block = marked(source, "// CE_STRATEGIC_CAMERA_BEGIN:", "// CE_STRATEGIC_CAMERA_END")
    # The marker's description is a comment, not part of the translation unit.
    block = block.split("\n", 1)[1]
    trap = marked(source, "// CE_STRATEGIC_TRAP_BEGIN", "// CE_STRATEGIC_TRAP_END")
    release = re.findall(r"(?m)^\tif \(event == KEY_EVENT_UP\).*return true; \}$",
                         definition(source, "Trap"))
    if len(release) != 1:
        raise ValueError("paused map key-release path unavailable")
    paused_keys = marked(definition(source, "Trap"), release[0], "switch (key)")
    render = marked(source, "// CE_STRATEGIC_RENDER_BEGIN", "// CE_STRATEGIC_RENDER_END")
    navigation = marked(source, "// CE_MAP_PAN_BEGIN", "// CE_MAP_PAN_END")
    return ("\n".join([
        definition(source, "OpenKey"), definition(source, "Ns"), definition(source, "SecondsBetween"),
        definition(source, "Note"), definition(source, "ByOrdinal"),
        definition(source, "Refresh"), definition(source, "CommandableCount"),
        definition(source, "Cycle"), definition(source, "WhyText"), definition(source, "OrderName"), definition(source, "SpreadRound"), definition(source, "Order"),
        definition(source, "SelectOnly"), definition(source, "SelectToggle"), definition(source, "SelectAll"), definition(source, "GroupKey"),
        definition(source, "FullSpan"), definition(source, "SetView"),
        definition(source, "FitView"), definition(source, "Pan"), block,
        definition(source, "PanMapFrame"), definition(source, "Close"), definition(source, "Open"),
        "bool Trap(WORD key, int event) {\n" + trap + "\n" + release[0] + paused_keys + "\nreturn false;\n}",
        definition(source, "PortCeCmdFrame"), definition(source, "PortCeCmdLevelEnd"),
        definition(source, "PortCeCmdKey"),
        definition(source, "PortCeCmdRenderCamera"),
        definition(source, "PortCeCmdSuppressFlightHud") + "\n\n" + definition(source, "PortCeCmdHideCockpit"),
        "void StrategicRenderPrefix() {\n" + render + "\n}",
        "void MapRenderNavigation() {\n" + navigation + "\n}",
    ])) + "\n" + native_scene_owner_token(source)


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


def integration_controls(repo: Path, compiler: str, logs: Path, jobs: int) -> bool:
    public_flag = public_ce_define(repo)
    public_macro = public_flag[2:].split("=", 1)[0]
    source = (repo / "port" / "game" / "ce_cmdmap.cpp").read_text()
    # The key store and the whole factory owner are compiled below. Only the stock
    # settings, engine objects and physical input table are supplied by the host fixture.
    support = logs / "authored-engine"
    support.mkdir()
    for header_name in ("common.h", "game.h", "world.h", "unit.h", "thing.h",
                        "SpawnerThng.h", "WorldPhysicsManager.h"):
        (support / header_name).write_text("// The authored factory world precedes the production source.\n")
    policy = (repo / "port/game/ce_dodge_assist.cpp").read_text()
    policy_part = "\n".join(definition(policy, name) for name in
                            ("KeyBound", "PortCeActionEnabled", "PortCeActionConflict"))
    (support / "camera_binding_conflicts.inc").write_text(policy_part + "\n")
    driver = (repo / "port/tools/ce_commander_camera_tests.cpp").read_text()
    scancodes = (repo / "port/include/linux/dinput.h").read_text()
    used = sorted(set(re.findall(r"\bDIK_[A-Z0-9_]+\b", source + policy_part + driver)))
    labels = []
    for key in used:
        matched = re.findall(r"(?m)^#define[ \t]+" + key + r"[ \t]+(0[xX][0-9a-fA-F]+)\b", scancodes)
        if len(matched) != 1:
            raise ValueError("no unique platform scan code: " + key)
        labels.append(f"constexpr unsigned short {key} = {matched[0]};")
    (support / "camera_platform_keys.inc").write_text("\n".join(labels) + "\n")
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
    patch = (repo / "port" / "patches" / "0315-ce-hook-render-camera.patch").read_text()
    hooks_text = (repo / "port" / "game" / "ce_hooks.cpp").read_text()
    if "ce_hook_render_camera(viewpoint, camera, player)" not in patch:
        raise ValueError("render_camera site patch unavailable")
    if f"#ifdef {public_macro}" not in hooks_text or "CeHookCmdRenderCamera" not in hooks_text or "PortCeCmdRenderCamera(camera, player)" not in hooks_text:
        raise ValueError("presentation hook unavailable")
    if hooks_text.index("CeHookCmdRenderCamera") > hooks_text.index("CeHookPlayUnitRenderCamera"):
        raise ValueError("commander camera must run before play-unit camera")
    hook = "    camera = ce_hook_render_camera(0, camera, player);\n"
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
    shared_sources = [
        repo / "port" / "game" / "ce_commander_camera.cpp",
        repo / "port" / "game" / "ce_hud_reservation.cpp",
        repo / "port" / "game" / "ce_dodge_binding.cpp",
        repo / "port" / "game" / "ce_command_selection.cpp",
        repo / "port" / "game" / "ce_command_panel.cpp",
        repo / "port" / "game" / "ce_hooks.cpp",
        repo / "port" / "game" / "ce_modes.cpp",
        repo / "port" / "tools" / "tests" / "ce_commander_camera_factory.cpp",
    ]
    driver_source = repo / "port" / "tools" / "ce_commander_camera_tests.cpp"
    x87 = x87_flags(compiler, logs)
    (logs / "x87-single-precision.json").write_text(json.dumps({"single_precision_x87_build": bool(x87), "flags": x87}) + "\n")
    common_flags = [
        compiler, *x87,
        *(["-DCE_CAMERA_EDITOR_HUD_QUERY"] if has_editor_scope else []),
        "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic",
        "-DCE_COMMANDER_CAMERA_INTEGRATION",
    ]
    shared_dir = logs / "shared-objs"
    shared_dir.mkdir()

    def compile_shared_obj(src: Path) -> tuple[bool, Path]:
        obj = shared_dir / (src.stem + ".o")
        cmd = [
            *common_flags, public_flag,
            "-I", str(logs), "-I", str(repo / "port" / "game"), "-I", str(support),
            "-c", str(src), "-o", str(obj),
        ]
        return run_command(cmd, shared_dir, src.stem), obj

    if jobs == 1:
        shared_results = [compile_shared_obj(src) for src in shared_sources]
    else:
        with ThreadPoolExecutor(max_workers=jobs) as pool:
            shared_results = list(pool.map(compile_shared_obj, shared_sources))
    if not all(ok for ok, _ in shared_results):
        return False
    shared_objs = [str(obj) for _, obj in shared_results]

    def link_driver_cmd(inc_dir: Path, out_exe: Path) -> list[str]:
        return [
            *common_flags, public_flag,
            "-I", str(inc_dir), "-I", str(repo / "port" / "game"), "-I", str(support),
            str(driver_source), *shared_objs, "-o", str(out_exe),
        ]

    faithful = [
        *common_flags,
        "-I", str(logs), "-I", str(repo / "port" / "game"), "-I", str(support),
        *(str(src) for src in shared_sources), str(driver_source),
        "-o", str(logs / "camera-hook-disabled-controls"),
    ]

    def run_baseline() -> bool:
        return run_command(link_driver_cmd(logs, executable), logs, "integration-compile") and run_command(
            [str(executable)], logs, "integration-controls"
        )

    def run_hook_disabled() -> bool:
        return run_command(faithful, logs, "hook-disabled-compile") and run_command(
            [faithful[-1]], logs, "hook-disabled-controls"
        )

    if jobs == 1:
        if not run_baseline() or not run_hook_disabled():
            return False
    else:
        with ThreadPoolExecutor(max_workers=min(2, jobs)) as pool:
            f_base = pool.submit(run_baseline)
            f_disabled = pool.submit(run_hook_disabled)
            if not (f_base.result() and f_disabled.result()):
                return False
    mutations = {}
    for name, token in (("simulation_pause", "GAME.Pause("), ("pilot_control", "SetToControl(")):
        lines = [line for line in definition(source, "Open").splitlines()
                 if token in line and "!strategic" in line]
        if len(lines) != 1:
            raise ValueError("causal guard unavailable")
        mutations[name] = source.replace(lines[0], lines[0].replace("!strategic", "true"), 1)
    closed = "if (!gOpen || !gStrategic || !gStrategicRender || !gStrategicOwner.Active()) return NULL;"
    if source.count(closed) != 1:
        raise ValueError("closed-view guard unavailable")
    mutations["closed_render"] = source.replace(closed, closed.replace("return NULL", "return &gStrategicCamera"))
    # The live view leaves the game's frame alone. The engine's presentation hook is given the strategic camera again (the view's first
    # form); the flight HUD's draws are dropped again; the view takes the device's draw filter; each must be caught.
    inert_camera = definition(source, 'PortCeCmdRenderCamera')
    if definition(source, "PortCeCmdRenderCamera").count(inert_camera) != 1:
        raise ValueError("inert presentation hook unavailable")
    mutations["engine_given_the_strategic_camera"] = source.replace(
        inert_camera, inert_camera[:inert_camera.index("{") + 1] + "    (void)player;\n    return gOpen && gStrategic ? &gStrategicCamera : camera;\n}", 1)
    inert_hud = definition(source, "PortCeCmdSuppressFlightHud")
    if inert_hud.count("return false;") != 1:
        raise ValueError("inert flight-scope query unavailable")
    mutations["flight_hud_dropped_again"] = source.replace(inert_hud, inert_hud.replace("return false;", "return gOpen && gStrategic;", 1), 1)
    begin = "        gStrategicRender = true;\n        return UpdateStrategicView"
    if source.count(begin) != 1 or "PortCeDrawHooks" in source or "PortCeDrawsDropped" in source:
        raise ValueError("the live view must not name the draw filter")
    mutations["live_view_takes_the_draw_filter"] = source.replace(
        begin, "        gStrategicRender = true;\n        PortCeDrawHooks(true);\n        return UpdateStrategicView", 1)
    # The view the pointer picks from: its age, its window and its entry are each checked, and a frame with no picture leaves none.
    read = definition(source, "StrategicViewRead")
    for name, clause in (("stale_view_sample", "PortPresentCount() - gStrategicSample.present > 1 || "),
                         ("view_sample_from_another_window", "gStrategicSampleWidth != L.w || "),
                         ("view_sample_from_another_entry", "gStrategicSampleSession != gStrategicViewSession ||")):
        if read.count(clause) != 1:
            raise ValueError("view-sample guard unavailable")
        mutations[name] = source.replace(read, read.replace(clause, "", 1), 1)
    failed = "        } else StrategicViewInvalidate();"
    if source.count(failed) != 1:
        raise ValueError("picture-failure anchor unavailable")
    mutations["picture_failure_keeps_the_view"] = source.replace(failed, "        }", 1)
    hints = "            DrawStrategicHints();\n            StrategicPictureEnd();"
    if source.count(hints) != 1:
        raise ValueError("picture frame anchor unavailable")
    mutations["picture_never_ended"] = source.replace(hints, "            DrawStrategicHints();", 1)
    left = "        gStrategicRender = false; gStrategicOriginal = NULL;\n        StrategicLeft();"
    if source.count(left) != 1:
        raise ValueError("picture release anchor unavailable")
    mutations["picture_kept_after_leaving"] = source.replace(left, "        gStrategicRender = false; gStrategicOriginal = NULL;", 1)
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
        carried, "(void)held; gDown[DIK_V] = true; gDown[gCloseKey] = false;", 1)
    centre_x = "float centreX = gViewX + gViewSpan * 0.5f;"
    centre_y = "float centreY = gViewY + gViewSpan * 0.5f;"
    if source.count(centre_x) != 1 or source.count(centre_y) != 1:
        raise ValueError("map-centre conversion unavailable")
    mutations["idle_focus_drift"] = source.replace(centre_x, "float centreX = gViewX;", 1).replace(
        centre_y, "float centreY = gViewY;", 1)
    focus = "FVector(gViewX + gViewSpan * 0.5f,\n                                      gViewY + gViewSpan * 0.5f, gStrategicFocusZ)"
    if source.count(focus) != 1:
        raise ValueError("strategic map focus unavailable")
    mutations["corner_as_focus"] = source.replace(focus, "FVector(gViewX, gViewY, gStrategicFocusZ)", 1)
    action = definition(source, "StrategicSquadAction")
    if action.count("Refresh();") != 1 or action.count("!c.live_pilot") != 1:
        raise ValueError("live squad action revalidation unavailable")
    mutations["stale_live_selection"] = source.replace(action, action.replace("    Refresh();\n", "", 1), 1)
    mutations["dead_pilot_order"] = source.replace(action, action.replace(" || !c.live_pilot", "", 1), 1)
    live_keys = "!repeat && (key == DIK_TAB || key == DIK_C || key == DIK_H || key == DIK_X || key == DIK_A || key == DIK_B || key == DIK_DELETE"
    if source.count(live_keys) != 1:
        raise ValueError("live order repeat guard unavailable")
    mutations["repeated_live_order"] = source.replace(live_keys, live_keys.replace("!repeat && ", ""), 1)
    pointer = definition(source, "StrategicPointer")
    if pointer.count("Refresh();") != 2 or pointer.count("if (!StrategicCanAct()) { gUnitUi.Cancel(); return; }") != 1:
        raise ValueError("live pointer authority anchors unavailable")
    mutations["stale_pointer_selection"] = source.replace(pointer, pointer.replace("    Refresh();", "    (void)0;", 2), 1)
    mutations["pointer_skips_context"] = source.replace(pointer, pointer.replace("if (!StrategicCanAct()) { gUnitUi.Cancel(); return; }", "if (false) return;", 1), 1)
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
        "pan_horizontal_keys_swapped": ('double((CameraKeyDown(DIK_RIGHT) || CameraKeyDown(DIK_D))) - double((CameraKeyDown(DIK_LEFT) || (CameraKeyDown(DIK_A) && !CameraKeyDown(DIK_LCONTROL) && !CameraKeyDown(DIK_RCONTROL))))',
                                        '-( double((CameraKeyDown(DIK_RIGHT) || CameraKeyDown(DIK_D))) - double((CameraKeyDown(DIK_LEFT) || (CameraKeyDown(DIK_A) && !CameraKeyDown(DIK_LCONTROL) && !CameraKeyDown(DIK_RCONTROL)))) )'),
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
    for name, before, after in (
        ("fixed_commander_open_key", "return (unsigned)PortCeActionKey(CeControlAction::Commander);",
         "return DIK_M;"),
        ("occupied_commander_key_opens", " || PortCeActionConflict(CeControlAction::Commander, (int)key)", ""),
        ("strategic_close_reloads_key", "key == DIK_ESCAPE || key == gCloseKey",
         "key == DIK_ESCAPE || key == OpenKey()"),
        ("paused_close_uses_fixed_m", "if (key == gCloseKey) { Close(); return true; }",
         "if (key == DIK_M) { Close(); return true; }"),
        ("factory_card_when_absent", "if (!CeFactoryRead(f)) return -1;",
         "if (!CeFactoryRead(f)) return 1;"),
        ("factory_button_does_not_queue", "else if (factoryButton == 2) CeFactoryQueue();",
         "else if (factoryButton == 2) CeFactoryCancel();"),
        ("factory_button_does_not_cancel", "else if (factoryButton == 3) CeFactoryCancel();",
         "else if (factoryButton == 3) CeFactoryQueue();"),
    ):
        if source.count(before) != 1:
            raise ValueError("combined input control no longer names one source operation: " + name)
        mutations[name] = source.replace(before, after, 1)
    presentation_cases = (
        ("return_not_started", "Close", "BeginNativeReturn();", "CancelNativeReturn();"),
        ("return_target_stale", "NativeReturnPresentation", "const CeCommanderPresentedPose target = NativeCameraPose(original);", "const CeCommanderPresentedPose target = gNativeReturnFrom;"),
        ("return_ignores_unit_owner", "NativeReturnEligible", "CePlayUnitOwnsPilot() ||", "false ||"),
        ("return_ignores_world", "NativeReturnEligible", "!SameCameraBinding(context.binding, gNativeReturnBinding)", "false"),
        ("return_overrides_secondary_camera", "NativeReturnPresentation", "original != gNativeReturnOriginal ||", "false ||"),
        ("presentation_moves_pilot_camera", "NativeStrategicPresentation", "NativePresentPose(shown);", "static_cast<CGenericCamera*>(original)->mPos = FVector(shown.pos[0], shown.pos[1], shown.pos[2]); NativePresentPose(shown);"),
        ("return_survives_level_end", "PortCeCmdLevelEnd", "CancelNativeReturn();", "(void)gNativeReturning;"),
        ("reopen_snaps_to_pilot", "PrepareNativeEntry", "gNativeEntrySeeded = NativeReturnEligible();", "gNativeEntrySeeded = false;"),
        ("entry_has_no_flight", "NativeStrategicPresentation", "gNativeEntering = true;", "gNativeEntering = false;"),
        ("cockpit_drawn_during_return", "PortCeCmdHideCockpit", " || NativeReturnEligible()", ""),
    )
    for name, function_name, before, after in presentation_cases:
        body = definition(source, function_name)
        if body.count(before) != 1:
            raise RuntimeError("presentation mutation anchor unavailable: " + name)
        mutations[name] = source.replace(body, body.replace(before, after, 1), 1)

    unsafe = source.replace("(std::min)(", "std::min(")
    if unsafe == source:
        raise ValueError("macro compatibility control unavailable")

    def eval_mutation(name: str, changed: str) -> dict:
        folder = logs / name
        folder.mkdir()
        (folder / "ce_commander_strategic_actual.inc").write_text(integration_fragment(changed))
        (folder / "ce_commander_strategic_hook.inc").write_text(hook)
        variant = link_driver_cmd(folder, folder / "controls")
        compiled = run_command(variant, folder, "compile")
        rejected = False
        if compiled:
            result = subprocess.run([variant[-1]], capture_output=True, timeout=60, check=False)
            (folder / "controls.stdout").write_bytes(result.stdout)
            (folder / "controls.stderr").write_bytes(result.stderr)
            rejected = result.returncode == 1 and b"authored integration control failed" in result.stderr
        return {"compiled": compiled, "rejected": rejected}

    def eval_macro() -> bool:
        macro_folder = logs / "legacy-macro-control"
        macro_folder.mkdir()
        (macro_folder / "ce_commander_strategic_actual.inc").write_text(integration_fragment(unsafe))
        (macro_folder / "ce_commander_strategic_hook.inc").write_text(hook)
        macro_command = link_driver_cmd(macro_folder, macro_folder / "controls")
        return not run_command(macro_command, macro_folder, "compile")

    def eval_editor_scope() -> tuple[bool, bool]:
        editor_folder = logs / "editor-flight-hud-scope-control"
        editor_folder.mkdir()
        (editor_folder / "ce_commander_strategic_actual.inc").write_text(integration_fragment(source))
        (editor_folder / "ce_commander_strategic_hook.inc").write_text(hook)
        (editor_folder / "ce_photo.h").write_text(header.replace(editor_query, "", 1))
        editor_command = link_driver_cmd(editor_folder, editor_folder / "controls")
        editor_compiled = run_command(editor_command, editor_folder, "compile")
        editor_rejected = False
        if editor_compiled:
            result = subprocess.run([editor_command[-1]], capture_output=True, timeout=60, check=False)
            (editor_folder / "controls.stdout").write_bytes(result.stdout)
            (editor_folder / "controls.stderr").write_bytes(result.stderr)
            editor_rejected = result.returncode == 1 and b"authored integration control failed" in result.stderr
        return editor_compiled, editor_rejected

    results = {}
    editor_scope_passed = True
    if jobs == 1:
        for name, changed in mutations.items():
            results[name] = eval_mutation(name, changed)
        macro_rejected = eval_macro()
        if has_editor_scope:
            editor_compiled, editor_rejected = eval_editor_scope()
            editor_scope_passed = editor_compiled and editor_rejected
    else:
        with ThreadPoolExecutor(max_workers=jobs) as pool:
            mut_futures = [(name, pool.submit(eval_mutation, name, changed)) for name, changed in mutations.items()]
            macro_future = pool.submit(eval_macro)
            editor_future = pool.submit(eval_editor_scope) if has_editor_scope else None
            for name, future in mut_futures:
                results[name] = future.result()
            macro_rejected = macro_future.result()
            if editor_future is not None:
                editor_compiled, editor_rejected = editor_future.result()
                editor_scope_passed = editor_compiled and editor_rejected
    (logs / "integration-causal.json").write_text(json.dumps(results, indent=2) + "\n")
    (logs / "macro-compatibility.json").write_text(json.dumps({
        "safe_consumer_compiled": True, "unsafe_calls_rejected": macro_rejected,
    }, indent=2) + "\n")
    # The presentation hook hands back the camera it is given in every build, so there is no compiled-out control of it any more; the
    # flight scope is photo mode's and the editor's, so there is no photo-only control either (the editor's own control is below).
    if has_editor_scope:
        (logs / "editor-flight-hud.json").write_text(json.dumps({
            "editor_query_present": True, "missing_editor_scope_compiled": editor_compiled,
            "missing_editor_scope_rejected": editor_rejected,
        }, indent=2) + "\n")
    return macro_rejected and editor_scope_passed and all(
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
    parser.add_argument("-j", "--jobs", type=int, default=None, help="max parallel compiles/runs (default: BEA_MUTANT_JOBS or min(6, cpu_count))")
    args = parser.parse_args()
    jobs = default_jobs() if args.jobs is None else args.jobs
    if jobs < 1:
        raise SystemExit(f"--jobs must be >= 1, got {jobs}")
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
        compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic",
        "-I", str(repo / "port" / "game"),
        str(repo / "port" / "game" / "ce_commander_camera.cpp"), str(repo / "port" / "game" / "ce_hud_reservation.cpp"),
        str(repo / "port" / "tools" / "ce_commander_camera_tests.cpp"),
        "-o", str(executable),
    ]
    compiled = run_command(command, logs, "compile")
    owner_passed = compiled and run_command([str(executable)], logs, "controls")
    integrated = owner_passed and integration_controls(repo, compiler, logs, jobs)
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
