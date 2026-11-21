#!/usr/bin/env python3
"""Build and run the pure controls for the Commander selection/action panel.

The panel module is engine-independent, so these controls need only a C++17
compiler.  The runner:

  1. checks the module sources statically (standard headers only, no input,
     file, process or network calls, no machine paths);
  2. compiles the module with its synthetic driver;
  3. runs every driver control and requires the exact expected set to pass
     (a missing or renamed control is a failure, never a silent skip);
  4. asks the driver to dump layouts for a matrix of view sizes, scales,
     fonts and host reservations, and re-checks the geometry here,
     independently of the driver's own assertions.

Nothing here starts the game, opens a window or reads game data.  A pass
says the module behaves as specified on synthetic input; it says nothing
about the integrated game.

Usage:
    python3 ce_command_panel_tests.py [--cxx COMPILER] [--build-dir DIR]
                                      [--sanitize] [--keep]

Exit status: 0 all controls passed, 1 a control failed, 2 could not run.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
PORT_DIR = os.path.dirname(TOOLS_DIR)
MODULE_DIR = os.path.join(PORT_DIR, "game")
HEADER = os.path.join(MODULE_DIR, "ce_command_panel.h")
SOURCE = os.path.join(MODULE_DIR, "ce_command_panel.cpp")
DRIVER = os.path.join(TOOLS_DIR, "tests", "ce_command_panel_driver.cpp")

EXPECTED_CONTROLS = [
    "identity_and_generation",
    "incomplete_snapshot_preserves_sets",
    "take_requires_one_selection",
    "select_ops",
    "saved_sets",
    "single_unit",
    "mixed_counts",
    "structure_selection",
    "human_owned",
    "unavailable_and_scripted",
    "stale_intent",
    "press_outside_cross_release",
    "press_inside_release_outside",
    "disabled_and_padding_capture",
    "change_while_pressed",
    "base_resolution_fit",
    "no_invented_production",
    "paint_matches_layout",
    "targets_and_host_state",
    "squad_versus_selection",
]

ALLOWED_MODULE_INCLUDES = {
    '"ce_command_panel.h"',
    "<cmath>",
    "<cstddef>",
    "<cstdint>",
    "<cstring>",
}

# Substrings that must not appear in the module: it may not read input
# devices, files, the environment, the network or start processes, and its
# public text may not carry machine paths.
FORBIDDEN_MODULE_TEXT = [
    "system(",
    "popen(",
    "fopen(",
    "getenv",
    "socket",
    "std::thread",
    "GetAsyncKeyState",
    "GetKeyState",
    "SDL_",
    "VK_",
    "DIK_",
    "/home/",
    "/srv/",
    "C:\\",
]

ACTION_LABELS = [
    "Move",
    "Advance",
    "Hold",
    "Fall Back",
    "Return to AI",
    "Take Control",
    "Focus",
    "Request Vehicle",
    "Cancel Request",
]

# Controls each dump scenario must show when nothing had to be dropped.
SCENARIO_CONTROLS = {
    "single": 6,
    "human": 6,
    "structure": 2,
    "mixed": 7,
    "all": 9,
}

VIEW_SIZES = [
    (640, 480, 1.0),
    (800, 600, 1.0),
    (1024, 768, 1.0),
    (1280, 720, 1.5),
    (1920, 1080, 2.0),
    (2560, 1440, 2.0),
    (3840, 2160, 4.0),
]

FONTS = [(8.0, 14.0), (9.0, 16.0)]

EPSILON = 0.01


class Failures:
    def __init__(self):
        self.items = []

    def add(self, message):
        self.items.append(message)
        print("FAIL " + message)


def read_text(path):
    with open(path, "r", encoding="utf-8", newline="") as handle:
        return handle.read()


def check_sources(failures):
    """Static checks on the module and driver text."""
    checked = 0
    for path in (HEADER, SOURCE, DRIVER):
        if not os.path.isfile(path) or os.path.getsize(path) == 0:
            failures.add("source missing or empty: " + os.path.basename(path))
            return checked
    for path in (HEADER, SOURCE):
        name = os.path.basename(path)
        text = read_text(path)
        checked += 1
        if "\r" in text:
            failures.add(name + ": contains CR line endings")
        for line in text.splitlines():
            stripped = line.strip()
            if stripped.startswith("#include"):
                target = stripped[len("#include"):].strip()
                if target not in ALLOWED_MODULE_INCLUDES:
                    failures.add(name + ": include not allowed: " + target)
        for needle in FORBIDDEN_MODULE_TEXT:
            if needle in text:
                failures.add(name + ": forbidden text: " + needle)
    source = read_text(SOURCE)
    for label in ACTION_LABELS:
        checked += 1
        if '"' + label + '"' not in source:
            failures.add("module is missing the action label: " + label)
    driver = read_text(DRIVER)
    checked += 1
    for needle in ("/home/", "/srv/", "C:\\"):
        if needle in driver:
            failures.add("driver contains a machine path fragment: " + needle)
    if "\r" in driver:
        failures.add("driver contains CR line endings")
    return checked


def find_compiler(requested):
    candidates = [requested] if requested else []
    if os.environ.get("CXX"):
        candidates.append(os.environ["CXX"])
    candidates += ["g++", "clang++", "c++"]
    for candidate in candidates:
        if candidate and shutil.which(candidate):
            return candidate
    return None


def compile_driver(cxx, build_dir, sanitize):
    output = os.path.join(build_dir, "ce_command_panel_driver")
    command = [
        cxx,
        "-std=c++17",
        "-O1",
        "-g",
        "-Wall",
        "-Wextra",
        "-I",
        MODULE_DIR,
        SOURCE,
        DRIVER,
        "-o",
        output,
    ]
    if sanitize:
        command[4:4] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            universal_newlines=True)
    if result.stdout.strip():
        print(result.stdout.rstrip())
    if result.returncode != 0:
        return None, 0
    warnings = result.stdout.count("warning:")
    return output, warnings


def run_driver(driver, arguments):
    result = subprocess.run([driver] + arguments, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, universal_newlines=True, timeout=120)
    return result.returncode, result.stdout


def check_driver_controls(driver, failures):
    """Run every control; the passing set must equal EXPECTED_CONTROLS."""
    code, listing = run_driver(driver, ["--list"])
    listed = [line.strip() for line in listing.splitlines() if line.strip()]
    if code != 0 or listed != EXPECTED_CONTROLS:
        failures.add("driver control list differs from the expected list")
        print("  expected: " + ", ".join(EXPECTED_CONTROLS))
        print("  listed:   " + ", ".join(listed))

    code, output = run_driver(driver, [])
    print(output.rstrip())
    passed = set()
    failed = set()
    summary = None
    for line in output.splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[0] == "PASS":
            passed.add(parts[1])
        elif len(parts) >= 2 and parts[0] == "FAIL":
            failed.add(parts[1])
        elif parts and parts[0] == "SUMMARY":
            summary = dict(item.split("=", 1) for item in parts[1:] if "=" in item)
    for name in EXPECTED_CONTROLS:
        if name in failed:
            failures.add("driver control failed: " + name)
        elif name not in passed:
            failures.add("driver control did not run: " + name)
    if summary is None:
        failures.add("driver printed no SUMMARY line")
    else:
        if summary.get("ran") != str(len(EXPECTED_CONTROLS)):
            failures.add("driver ran %s controls, expected %d"
                         % (summary.get("ran"), len(EXPECTED_CONTROLS)))
        if summary.get("failed") != "0":
            failures.add("driver reported failed controls: %s" % summary.get("failed"))
        if summary.get("checks") in (None, "0"):
            failures.add("driver executed no checks")
    if code != 0:
        failures.add("driver exit status %d" % code)
    return len(passed)


# ---------------------------------------------------------------------------
# Independent geometry checks on dumped layouts
# ---------------------------------------------------------------------------

def rect(values):
    x, y, w, h = (float(v) for v in values)
    return (x, y, w, h)


def inside(inner, outer):
    return (inner[0] >= outer[0] - EPSILON and inner[1] >= outer[1] - EPSILON
            and inner[0] + inner[2] <= outer[0] + outer[2] + EPSILON
            and inner[1] + inner[3] <= outer[1] + outer[3] + EPSILON)


def overlap(a, b):
    if a[2] <= 0 or a[3] <= 0 or b[2] <= 0 or b[3] <= 0:
        return False
    return (a[0] < b[0] + b[2] - EPSILON and b[0] < a[0] + a[2] - EPSILON
            and a[1] < b[1] + b[3] - EPSILON and b[1] < a[1] + a[3] - EPSILON)


def parse_dump(text):
    layout = {"safe": None, "reserved": [], "panel": None, "lines": [], "controls": [],
              "detail": None}
    for raw in text.splitlines():
        head, _, tail = raw.partition("|")
        parts = head.split()
        if not parts:
            continue
        tag = parts[0]
        if tag == "SAFE":
            layout["safe"] = rect(parts[1:5])
        elif tag == "RESERVED":
            layout["reserved"].append(rect(parts[1:5]))
        elif tag == "PANEL":
            layout["panel"] = rect(parts[1:5])
            layout["visible"] = parts[5] == "1"
            layout["no_room"] = parts[6] == "1"
            layout["clipped"] = parts[7] == "1"
            layout["hidden"] = int(parts[8])
        elif tag == "LINE":
            layout["lines"].append({"rect": rect(parts[1:5]), "measured": float(parts[5]),
                                    "text": tail})
        elif tag == "CONTROL":
            layout["controls"].append({
                "rect": rect(parts[1:5]),
                "text_rect": rect(parts[6:10]),
                "action": int(parts[10]),
                "enabled": parts[11] == "1",
                "measured": float(parts[12]),
                "label": tail,
            })
        elif tag == "DETAIL":
            layout["detail"] = {"rect": rect(parts[1:5]), "widest": float(parts[5])}
    return layout


def check_layout(case, layout, scenario, scale, reserved, failures):
    """Return the number of geometry assertions made for this layout."""
    made = 0

    def expect(condition, message):
        nonlocal made
        made += 1
        if not condition:
            failures.add(case + ": " + message)

    expect(layout["safe"] is not None and layout["panel"] is not None, "dump incomplete")
    if layout["safe"] is None or layout["panel"] is None:
        return made
    if not reserved:
        expect(layout["visible"], "panel hidden although the view is unobstructed")
        expect(not layout["clipped"], "panel had to drop content at this size")
    if not layout["visible"]:
        return made

    safe = layout["safe"]
    panel = layout["panel"]
    expect(inside(panel, safe), "panel leaves the safe rectangle")
    limit = max(safe[2] * 0.5, 352.0 * scale) + 1.0
    expect(panel[2] <= limit, "panel wider than its compact limit (%.1f > %.1f)"
           % (panel[2], limit))
    expect(panel[3] <= safe[3] * 0.6, "panel taller than 60% of the safe height")
    for area in layout["reserved"]:
        expect(not overlap(panel, area), "panel overlaps a host reservation")

    boxes = []
    for line in layout["lines"]:
        expect(inside(line["rect"], panel), "text line leaves the panel: " + line["text"])
        expect(line["measured"] <= line["rect"][2] + EPSILON,
               "text line wider than its box: " + line["text"])
        expect(line["text"] != "", "empty text line")
        boxes.append(line["rect"])
    seen_actions = set()
    for control in layout["controls"]:
        label = control["label"]
        expect(inside(control["rect"], panel), "control leaves the panel: " + label)
        expect(inside(control["text_rect"], control["rect"]),
               "label box leaves its control: " + label)
        expect(control["measured"] <= control["text_rect"][2] + EPSILON,
               "label wider than its box: " + label)
        expect(label != "", "control without a label")
        expect(control["action"] not in seen_actions, "action shown twice: " + label)
        seen_actions.add(control["action"])
        boxes.append(control["rect"])
    if layout["detail"] is not None:
        detail = layout["detail"]
        expect(inside(detail["rect"], panel), "detail row leaves the panel")
        expect(detail["widest"] <= detail["rect"][2] + EPSILON,
               "a detail text is wider than the detail row")
        boxes.append(detail["rect"])
    for i in range(len(boxes)):
        for j in range(i + 1, len(boxes)):
            expect(not overlap(boxes[i], boxes[j]), "two panel elements overlap")

    if not layout["clipped"]:
        expect(len(layout["controls"]) == SCENARIO_CONTROLS[scenario],
               "expected %d controls, saw %d"
               % (SCENARIO_CONTROLS[scenario], len(layout["controls"])))
        expect(layout["hidden"] == 0, "controls hidden without the clipped flag")
    return made


def reservation_sets(width, height):
    portrait = [0.0, height * 0.70, width * 0.33, height * 0.30]
    dialogue = [0.0, 0.0, float(width), height * 0.18]
    return [[], [portrait], [dialogue, portrait]]


def check_geometry(driver, failures):
    layouts = 0
    assertions = 0
    for scenario in sorted(SCENARIO_CONTROLS):
        for width, height, scale in VIEW_SIZES:
            for char_w, char_h in FONTS:
                for reserved in reservation_sets(width, height):
                    arguments = ["--dump", scenario, str(width), str(height), str(scale),
                                 str(char_w), str(char_h)]
                    for area in reserved:
                        arguments += ["%.3f" % value for value in area]
                    case = "%s %dx%d scale %.2f font %gx%g reservations %d" % (
                        scenario, width, height, scale, char_w, char_h, len(reserved))
                    code, output = run_driver(driver, arguments)
                    if code != 0:
                        failures.add(case + ": dump exit status %d" % code)
                        continue
                    layouts += 1
                    try:
                        layout = parse_dump(output)
                    except (ValueError, IndexError):
                        failures.add(case + ": dump could not be parsed")
                        continue
                    assertions += check_layout(case, layout, scenario, scale, reserved,
                                               failures)
    return layouts, assertions


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--cxx", help="C++17 compiler (default: $CXX, g++, clang++, c++)")
    parser.add_argument("--build-dir", help="directory for the test binary "
                        "(default: a temporary directory, removed afterwards)")
    parser.add_argument("--sanitize", action="store_true",
                        help="build with address and undefined-behaviour sanitizers")
    parser.add_argument("--keep", action="store_true",
                        help="keep the temporary build directory")
    args = parser.parse_args()

    failures = Failures()
    static_checks = check_sources(failures)

    cxx = find_compiler(args.cxx)
    if cxx is None:
        print("NOT RUN: no C++ compiler found; nothing was built or executed")
        return 2

    temporary = None
    if args.build_dir:
        build_dir = os.path.abspath(args.build_dir)
        os.makedirs(build_dir, exist_ok=True)
    else:
        temporary = tempfile.mkdtemp(prefix="ce-command-panel-")
        build_dir = temporary

    try:
        driver, warnings = compile_driver(cxx, build_dir, args.sanitize)
        if driver is None:
            print("NOT RUN: the module or its driver did not compile")
            return 2
        passed = check_driver_controls(driver, failures)
        layouts, assertions = check_geometry(driver, failures)
    finally:
        if temporary and not args.keep:
            shutil.rmtree(temporary, ignore_errors=True)
        elif temporary:
            print("kept build directory: " + temporary)

    print("")
    print("compiler: %s%s, warnings: %d" % (cxx, " (sanitized)" if args.sanitize else "",
                                            warnings))
    print("static source checks: %d" % static_checks)
    print("driver controls passed: %d of %d, skipped: 0" % (passed, len(EXPECTED_CONTROLS)))
    print("layouts re-checked: %d (%d geometry assertions)" % (layouts, assertions))
    if failures.items:
        print("RESULT: FAILED (%d problems)" % len(failures.items))
        return 1
    print("RESULT: PASSED (synthetic module controls only; the integrated game is "
          "not exercised)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
