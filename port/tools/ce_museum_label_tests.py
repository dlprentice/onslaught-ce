#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The museum's words: which label is shown (host arithmetic, with mutants) and what its strings may say (a lint).

The label rules (ce_museum_label_rules.h) are plain arithmetic and are compiled and run on authored positions. The lint reads every string a player
can meet from the museum, its refusals, its hub row and its hall and exhibit tables, and fails the house style's forbidden words and the characters
the game's small face lacks (the interface guide's W3 and T2). Nothing runs the game and no retail content is used.
"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GAME = ROOT / "port/game"
RULES = GAME / "ce_museum_label_rules.h"
DRIVER = ROOT / "port/tools/tests/ce_museum_label_driver.cpp"

# Words and characters a player never reads (the guide's W3 and T2). "test" is matched as a word.
FORBIDDEN = ("ce.ini", "readme", "experimental", "prototype", "carrier", "best effort", "construction", "bounded", "identity", "signaling",
             "protocol", "origin", "https")
FORBIDDEN_WORD = ("test",)
FORBIDDEN_FILE = re.compile(r"\.[a-z]{2,4}\b")        # a file name or extension
FORBIDDEN_CHARS = set("$@[\\]^_{|}~`")

# Where player text sits in the museum's sources: the literal after each of these.
TEXT_PATTERNS = (
    re.compile(r'Refuse\(\s*"((?:[^"\\]|\\.)*)"'),
    re.compile(r'gMessage\s*=\s*"((?:[^"\\]|\\.)*)"'),
    re.compile(r'\bkOpenFailed\[\]\s*=\s*"((?:[^"\\]|\\.)*)"'),
    re.compile(r'const char \*gMessage\s*=\s*"((?:[^"\\]|\\.)*)"'),
    re.compile(r'\bHALL_TEXT\(\s*"((?:[^"\\]|\\.)*)"'),
    re.compile(r'PortCeMuseumLabelsTitle\(\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"'),		# the hall's name and the line under it (and the put-back note)
)
MUSEUM_ROW = re.compile(r'\{L"([^"]+)", CEA_(?:PAGE_MUSEUM|MUSEUM_VISIT|MUSEUM), \{L"((?:[^"\\]|\\.)*)", (?:0|L"((?:[^"\\]|\\.)*)")')


def player_strings():
    found = []
    for name in ("ce_museum_visit.cpp", "ce_museum_visit_engine.cpp", "ce_museum_halls.cpp"):
        path = GAME / name
        if not path.exists():
            continue
        text = path.read_text()
        for pattern in TEXT_PATTERNS:
            found += [(name, g) for m in pattern.finditer(text) for g in m.groups() if g]
    frontend = (GAME / "ce_frontend.cpp").read_text()
    for row in MUSEUM_ROW.finditer(frontend):						# the hub's Museum row and the Museum page's two rows: label, hint and the grey line
        found += [("ce_frontend.cpp", g) for g in row.groups() if g]
    return found


def lint(text):
    """The reasons a player string breaks the house style: [] when it does not."""
    problems = []
    lower = text.lower()
    problems += ["forbidden word '%s'" % w for w in FORBIDDEN if w in lower]
    problems += ["forbidden word '%s'" % w for w in FORBIDDEN_WORD if re.search(r"\b%s\b" % w, lower)]
    problems += ["file name '%s'" % m.group(0) for m in FORBIDDEN_FILE.finditer(text) if not re.match(r"\.(\s|$)", text[m.start():m.start() + 2])]
    problems += ["character %r the small face lacks" % c for c in sorted(FORBIDDEN_CHARS & set(text))]
    if text and text[0].islower():
        problems.append("does not start with a capital")
    if text and text[-1] not in ".?!" and len(text.split()) > 3:
        problems.append("a sentence without its full stop")
    return problems


def run(command, expect=0):
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    return result.returncode == expect, result.stdout + result.stderr


def main():
    failures = []

    def check(name, passed, detail=""):
        print(("ok   " if passed else "FAIL ") + name + ("" if passed else ("\n" + detail if detail else "")))
        if not passed:
            failures.append(name)

    with tempfile.TemporaryDirectory() as folder:
        folder = Path(folder)
        command = ["g++", "-std=c++14", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-I" + str(GAME), str(DRIVER), "-o"]
        good, out = run(command + [str(folder / "driver")])
        check("the label rules compile", good, out)
        if good:
            good, out = run([str(folder / "driver")])
            check("the label rules hold on authored positions", good, out)
        # Exercise the real room route in a normal north-facing hall and its rotated twin.
        route = GAME / "ce_museum_hall_layout.cpp"
        survey = ROOT / "port/tools/tests/ce_museum_hall_driver.cpp"
        def survey_route(source, binary):
            built, detail = run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                                 "-I" + str(GAME), str(survey), str(source), "-o", str(binary)])
            check("gallery route builds: " + source.stem, built, detail)
            return built
        if survey_route(route, folder / "survey"):
            valid, detail = run([str(folder / "survey")])
            check("two readable stops leave the exhibits clear in both headings", valid, detail)
        for label, old, new in (
                ("stops-on-display", "depth - d.radius - 2.0f", "depth"),
                ("wrong-hand-aisle", "t.forward.y, -t.forward.x", "-t.forward.y, t.forward.x")):
            mutant = folder / (label + ".cpp")
            text = route.read_text()
            if text.count(old) != 1:
                check("gallery mutant anchor: " + label, False)
                continue
            mutant.write_text(text.replace(old, new))
            if survey_route(mutant, folder / label):
                caught, detail = run([str(folder / label)], 2)
                check("gallery mutant caught: " + label, caught, detail)
        mutants = [
            ("inspect-at-any-distance", "dx * dx + dy * dy <= reach * reach", "(dx - dx + dy - dy) <= reach * reach"),
            ("inspect-loses-model-size", "reach = radius +", "reach = 0.0f +"),
            ("range-ignored", "if (!(d2 <= limit * limit))", "if (false)"),
            ("hysteresis-lost", "held ? kCeMuseumLabelKeep : kCeMuseumLabelRange", "kCeMuseumLabelRange"),
            ("behind-shown", "if (along <= 0.0f ||", "if (false ||"),
            ("view-ignored", "along * along < need * need * d2 * facing", "false"),
            ("view-hysteresis-lost", "held ? kCeMuseumLabelFrontKeep : kCeMuseumLabelFront", "kCeMuseumLabelFront"),
            ("hold-lost", "(held ? kCeMuseumLabelHold : 0.0f)", "0.0f"),
            ("most-central-lost", "if (best < 0 || score > bestScore)", "if (best < 0 || score < bestScore)"),
            ("distance-tiebreak-lost", "- kCeMuseumLabelFar * __builtin_sqrtf(d2)", ""),
            ("nearest-instead-of-central", "score = along * along / (d2 * facing) -", "score = -d2 -"),
            ("standing-on-it-lost", "score = 2.0f;", "score = -1.0f;"),
            ("sea-margin-ignored", "return z > waterLevel - margin;", "return z > waterLevel;"),
            ("sea-reach-inverted", "return z > waterLevel - margin;", "return z < waterLevel - margin;"),
            ("hall-edge-ignored", "return dx * dx + dy * dy > radius * radius;", "return false;"),
            ("hall-edge-a-square", "return dx * dx + dy * dy > radius * radius;", "return dx > radius || dx < -radius || dy > radius || dy < -radius;"),
            ("hall-edge-from-the-origin", "const float dx = x - startX, dy = y - startY;", "const float dx = x, dy = y;"),
            ("hall-edge-inverted", "return dx * dx + dy * dy > radius * radius;", "return dx * dx + dy * dy < radius * radius;"),
            ("every-view-level", "return fz * fz <= 0.25f * (fx * fx + fy * fy + fz * fz);", "return true;"),
            ("no-view-level", "return fz * fz <= 0.25f * (fx * fx + fy * fy + fz * fz);", "return false;"),
            ("view-level-by-the-wrong-angle", "return fz * fz <= 0.25f * (fx * fx + fy * fy + fz * fz);", "return fz * fz <= 0.95f * (fx * fx + fy * fy + fz * fz);"),
            ("repeat-ignores-case", "a = static_cast<char>(a - 'A' + 'a');", "(void)a;"),
            ("repeat-by-prefix", "return length != 0 && !line[length];", "return length != 0;"),
            ("repeat-of-the-first-word", "if (name[i] == ' ')\n\t\t\tlast = i + 1;", "if (false)\n\t\t\tlast = i + 1;"),
            ("never-shuts", "if (ending && age >= shown)\n\t\treturn 0.0f;", "if (false)\n\t\treturn 0.0f;"),
            ("first-letter-kept-lower", "if (start && c >= 'a' && c <= 'z')", "if (false)"),
            ("capital-runs-kept", "&& n && name[n - 1] >= 'A' && name[n - 1] <= 'Z')", "&& false)"),
            ("name-not-terminated", "\tout[n] = 0;\n\t// A number", "\t// A number"),
            ("trailing-number-kept", "if (end < n && end >= 2 && out[end - 1] == ' ')", "if (false)"),
            ("trailing-space-kept", "while (n > 0 && out[n - 1] == ' ')", "while (false)"),
            ("scale-note-wrong-word", "kWords[factor - 2]", "kWords[factor < 8 ? factor - 1 : 0]"),
            ("scale-note-cut-not-dropped", "\t\t\t\tout[0] = 0;\n\t\t\t\treturn false;", "\t\t\t\tout[n] = 0;\n\t\t\t\treturn false;"),
            ("no-growth", "float open = age >= grow ? 1.0f : static_cast<float>(age) / static_cast<float>(grow);", "float open = 1.0f;"),
        ]
        text = RULES.read_text()
        for label, old, new in mutants:
            if text.count(old) != 1:
                check("mutant " + label + " applies", False, "source text not found once: " + old)
                continue
            mutant = folder / label
            mutant.mkdir()
            (mutant / RULES.name).write_text(text.replace(old, new))
            built, out = run(["g++", "-std=c++14", "-O1", "-I" + str(mutant), str(DRIVER), "-o", str(mutant / "driver")])
            if not built:
                check("mutant " + label + " is caught", False, out)
                continue
            caught, _ = run([str(mutant / "driver")], 2)
            check("mutant " + label + " is caught", caught)

    # The second line of a label is the notes' grey: the same colour the front end's own grey lines use (a label whose grey drifts reads white or dim beside them).
    label_grey = re.search(r"\bkGrey\s*=\s*(0x[0-9a-fA-F]+)", (GAME / "ce_museum_label.cpp").read_text())
    frontend_grey = re.search(r"static const DWORD\s+kGrey\s*=\s*(0x[0-9a-fA-F]+)", (GAME / "ce_frontend.cpp").read_text())
    check("the label's grey and the front end's grey were found", bool(label_grey and frontend_grey))
    if label_grey and frontend_grey:
        check("the label's second line is the front end's grey (the text stage doubles it)", int(label_grey.group(1), 16) == int(frontend_grey.group(1), 16),
              "label %s, front end %s" % (label_grey.group(1), frontend_grey.group(1)))
        check("the grey check bites: a label drawn in the old grey fails it", int("0xffa8a8a8", 16) != int(frontend_grey.group(1), 16))

    strings = player_strings()
    check("the museum's player strings were found", len(strings) >= 10, "found %d" % len(strings))
    for source, string in strings:
        problems = lint(string)
        check("%s: %r reads as the game's own words" % (source, string[:48]), not problems, "; ".join(problems))
    # the lint itself bites
    for bad in ("Experimental. Walk through a small exhibition.", "Museum construction test: existing terrain.", "The carrier has no exhibits.",
                "Set museum_x in ce.ini.", "Open the file a_b.", "a sentence that starts small and has no end here"):
        check("the lint rejects %r" % bad[:40], bool(lint(bad)))
    check("the lint accepts a plain sentence", not lint("This hall has nothing to show from your game files. Your careers are as they were."))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
