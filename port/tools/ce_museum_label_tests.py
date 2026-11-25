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
# The production label code and the visit's place tracking, run frame by frame against a faked game boundary.
LABEL = GAME / "ce_museum_label.cpp"
PLACES = GAME / "ce_museum_visit_places.h"
VISIT_ENGINE = GAME / "ce_museum_visit_engine.cpp"
VISIT_DRIVER = ROOT / "port/tools/tests/ce_museum_label_visit_driver.cpp"
FAKE_HEADERS = ("common.h", "game.h", "player.h", "BattleEngine.h", "ltshell.h", "Hud.h", "renderinfo.h", "Texture.h", "DXEngine.h")
# Mutants in the behaviour (the label code and the place tracking the visit engine runs), each with the frame check that kills it.
BEHAVIOUR_MUTANTS = (
    ("signs-never-armed", PLACES, "\tPortCeMuseumLabelsSigns(true);\n", "", "arriving shows no card"),
    ("signs-call-ignored", LABEL, "\tgSigns = signs;", "\t(void)signs;", "arriving shows no card"),
    ("card-rule-not-applied", LABEL, "CeMuseumCardLabel(gSigns, ", "CeMuseumCardLabel(false, ", "arriving shows no card"),
    ("leave-never-told", PLACES, "\tPortCeMuseumLabelsPlace(places->place >= 0);\n", "", "walking out of the entrance closes"),
    ("place-call-ignored", LABEL, "{\n\tgInPlace = inPlace;", "{\n\t(void)inPlace;", "walking out of the entrance closes"),
    ("place-not-applied", LABEL, "CeMuseumTitleElsewhere(cinema, gWayfinding, gInPlace)", "CeMuseumTitleElsewhere(cinema, gWayfinding, true)", "walking out of the entrance closes"),
    ("title-not-latched", LABEL, "\t\tif (elsewhere)\n\t\t\tgHasTitle = false;", "", "it stays shut until the hall names"),
    ("cinema-not-applied", LABEL, "CeMuseumTitleElsewhere(cinema, gWayfinding, gInPlace)", "CeMuseumTitleElsewhere(false, gWayfinding, gInPlace)", "in the screening room the title closes"),
    ("edge-without-keep", PLACES, "previous == 0 ? kCeMuseumArrivalKeep : 0.0f", "0.0f", "a step past the edge keeps"),
)

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
            ("card-from-view-in-a-hall-of-signs", "return signs ? -1 : nearest;", "return nearest;"),
            ("card-ignores-the-sign", "\tif (sign >= 0)\n\t\treturn sign;\n\treturn signs", "\treturn signs"),
            ("title-rides-into-the-cinema", "if (elsewhere || now < since", "if (now < since"),
            ("title-never-times-out", "|| now - since >= shown)\n\t\treturn 0.0f;\n\treturn CeMuseumCapsuleOpen", ")\n\t\treturn 0.0f;\n\treturn CeMuseumCapsuleOpen"),
            ("title-outlives-its-place", "return cinema || (wayfinding && !inPlace);", "return cinema;"),
            ("every-title-a-place", "return cinema || (wayfinding && !inPlace);", "return cinema || !inPlace;"),
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

    # The label code and the place tracking, frame by frame (the behaviour, not only the rules).
    with tempfile.TemporaryDirectory() as folder:
        folder = Path(folder)

        def visit_frames(name, label_text, places_text):
            work = folder / name
            work.mkdir()
            (work / LABEL.name).write_text(label_text)
            (work / PLACES.name).write_text(places_text)
            for header in FAKE_HEADERS:
                (work / header).write_text('#include "ce_museum_label_fake.h"\n')
            (work / "ce_museum_cinema_panel.inc").write_text("namespace {\nvoid CinemaPanelDraw() {}\n}\n")
            built, out = run(["g++", "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-DCE_VERSION=1", "-I" + str(work),
                              "-I" + str(ROOT / "port/tools/tests"), "-I" + str(GAME), "-I" + str(ROOT / "port/include"),
                              str(VISIT_DRIVER), str(work / LABEL.name), "-o", str(work / "driver")])
            if not built:
                return None, out
            result = subprocess.run([str(work / "driver")], cwd=ROOT, capture_output=True, text=True)
            return result.returncode, result.stdout + result.stderr

        label_text, places_text = LABEL.read_text(), PLACES.read_text()
        code, out = visit_frames("good", label_text, places_text)
        check("the label code and place tracking build against the fake boundary", code is not None, out)
        check("arrival, leaving a place, its edge and the cinema draw as they should", code == 0, out)

        # Verify GAME.IsPaused() HUD suppression and zero empty opaque pill during open < 0.45f ramp.
        pause_dir = folder / "pause_and_pill"
        pause_dir.mkdir()
        (pause_dir / LABEL.name).write_text(label_text)
        (pause_dir / PLACES.name).write_text(places_text)
        fake_h = (ROOT / "port/tools/tests/ce_museum_label_fake.h").read_text().replace(
            "struct FakeGame { CPlayer player; bool IsMultiplayer() { return false; } CPlayer *GetPlayer(int) { return &player; } };",
            "struct FakeGame { CPlayer player; bool paused = false; bool IsPaused() { return paused; } bool IsMultiplayer() { return false; } CPlayer *GetPlayer(int) { return &player; } };"
        )
        (pause_dir / "ce_museum_label_fake.h").write_text(fake_h)
        for header in FAKE_HEADERS:
            (pause_dir / header).write_text('#include "ce_museum_label_fake.h"\n')
        (pause_dir / "ce_museum_cinema_panel.inc").write_text("namespace {\nvoid CinemaPanelDraw() {}\n}\n")
        pause_driver = r"""
#include "ce_museum_label_fake.h"
#include "hud_scale.h"
#include "ce_museum_label.h"
#include <cstdio>
#include <cstdlib>
std::vector<FakeText> gFakeTexts;
std::vector<FakeBar> gFakeBars;
FakeShell LT; FakeGame GAME; FakeEngine ENGINE; FakeHud HUD; FakePlatform PLATFORM; FakeRenderInfo RENDERINFO;
bool gMirrorPass = false;
float gMessageBoxX = 0, gMessageBoxY = 0, gMessageBoxWidth = 0, gMessageTextY = 0;
CeHudScope::CeHudScope(int, float, float, float, float) : mPreviousScale(1), mPreviousX(0), mPreviousY(0), mPreviousActive(false) {}
CeHudScope::~CeHudScope() {}
static float gHudScale = 1.0f;
float CeHudScaleFactor() { return gHudScale; }
static unsigned long long gTicks = 50000;
extern "C" unsigned PortPresentCount(void) { return 1; }
extern "C" unsigned long long CE_ScreenshotTicks(void) { return gTicks; }
extern "C" bool PortCeMuseumVisitActive(void) { return true; }
extern "C" void PortCeMuseumVisitFrame(void) {}
extern "C" bool PortCeMuseumCinemaCue(char *, unsigned, char *, unsigned) { return false; }
extern "C" void PortCeMuseumCinemaView(const float *, unsigned) {}
static bool gAimingControl = false;
static bool gInteractiveCardOn = false;
extern "C" bool PortCeMuseumInteractiveAim(void) { return gAimingControl; }
extern "C" bool PortCeMuseumInteractiveCard(float, float, float, float,
                                            char *title, unsigned titleCap,
                                            char *line, unsigned lineCap,
                                            char *note, unsigned noteCap) {
    if (!gInteractiveCardOn) return false;
    std::snprintf(title, titleCap, "XF-28 Venom");
    if (gAimingControl) {
        std::snprintf(line, lineCap, "Forseti Advanced Interceptor");
        std::snprintf(note, noteCap, "Turntable rotating. Press Fire to pause rotation.");
    } else {
        std::snprintf(line, lineCap, "Mach 2 successor to the F-24 Valkyrie, banked on the western coastal showcase pylon.");
        std::snprintf(note, noteCap, "Forseti Advanced Interceptor - aim at the brass stud to pause rotation.");
    }
    return true;
}
int CeMuseumHallSignLabel(float, float, int, int *n) { if (n) *n = 0; return -1; }
bool CeMuseumHallFacesExit(float, float, float, float, bool) { return false; }
static void Need(bool ok, const char *why) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(2); } }
int main() {
    PortCeMuseumLabelsClear();
    PortCeMuseumLabelsTitle("Grand Atrium", "Central rotunda and living battle dioramas.");
    // Frame at t=0 (open=0.0 < 0.45): must NOT draw an empty opaque capsule!
    gFakeBars.clear(); gFakeTexts.clear();
    PortCeMuseumLabelsRender();
    Need(gFakeBars.empty() && gFakeTexts.empty(), "open=0 draws zero empty capsule");
    // Frame at t=100ms (open=0.285 < 0.45): still zero empty capsule!
    gTicks += 100; gFakeBars.clear(); gFakeTexts.clear();
    PortCeMuseumLabelsRender();
    Need(gFakeBars.empty() && gFakeTexts.empty(), "open<0.45 draws zero empty capsule");
    // Frame at t=200ms (open=0.571 >= 0.45): bar AND visible text draw together!
    gTicks += 100; gFakeBars.clear(); gFakeTexts.clear();
    PortCeMuseumLabelsRender();
    Need(!gFakeBars.empty() && !gFakeTexts.empty(), "open>=0.45 draws both capsule and text together");
    Need(((gFakeTexts[0].colour >> 24) & 0xffu) >= 140u, "text alpha is immediately readable when bar appears");
    // Pause menu active: HUD must be completely suppressed (zero floating pill over pause menu)!
    GAME.paused = true;
    gTicks += 200; gFakeBars.clear(); gFakeTexts.clear();
    PortCeMuseumLabelsRender();
    Need(gFakeBars.empty() && gFakeTexts.empty(), "GAME.IsPaused() suppresses museum HUD completely");
    // Unpause resumes cleanly.
    GAME.paused = false;
    gFakeBars.clear(); gFakeTexts.clear();
    PortCeMuseumLabelsRender();
    Need(!gFakeBars.empty() && !gFakeTexts.empty(), "unpausing restores active museum HUD capsule");

    // Interactive exhibit plaque when NOT aiming at a stud: shows Name + Class + Fact, suppressing "aim at" hint!
    PortCeMuseumLabelsClear();
    PortCeMuseumLabelsAdd("Dummy", "Class", "Fact", 900.0f, 900.0f, -2.0f, 1.0f);
    gInteractiveCardOn = true;
    gAimingControl = false;
    gTicks += 400; gFakeBars.clear(); gFakeTexts.clear();
    PortCeMuseumLabelsRender();
    Need(gFakeTexts.size() >= 3u, "interactive plaque draws name, class and fact rows");
    Need(gFakeTexts[0].text == L"XF-28 Venom", "plaque row 0 is exhibit name");
    Need(gFakeTexts[1].text == L"Forseti Advanced Interceptor", "plaque row 1 is exhibit class");
    Need(gFakeTexts[2].text.find(L"Mach 2 successor") != std::wstring::npos, "plaque row 2 is exhibit fact");
    for (const FakeText &ft : gFakeTexts)
        Need(ft.text.find(L"aim at") == std::wstring::npos, "control hint suppressed when not aiming at stud");
    return 0;
}
"""
        (pause_dir / "pause_driver.cpp").write_text(pause_driver)
        pbuilt, pout = run(["g++", "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-DCE_VERSION=1",
                            "-I" + str(pause_dir), "-I" + str(GAME), "-I" + str(ROOT / "port/include"),
                            str(pause_dir / "pause_driver.cpp"), str(pause_dir / LABEL.name),
                            "-o", str(pause_dir / "pause_driver")])
        check("museum label pause-suppression and no-empty-pill driver builds", pbuilt, pout)
        if pbuilt:
            pok, pdetail = run([str(pause_dir / "pause_driver")])
            check("museum HUD suppresses on pause and never draws an empty capsule", pok, pdetail)

        for name, path, old, new, killer in BEHAVIOUR_MUTANTS:
            text = path.read_text()
            if text.count(old) != 1:
                check("behaviour mutant " + name + " applies", False, "source text not found once in " + path.name + ": " + old)
                continue
            mutated = text.replace(old, new)
            code, out = visit_frames(name, mutated if path == LABEL else label_text, mutated if path == PLACES else places_text)
            check("behaviour mutant " + name + " is killed by '" + killer + "'", code == 2 and ("FAIL: " + killer) in out, out)
    # The Museum's one caption rule (ce_museum_label_rules.h), as arithmetic, with mutants; then what a frame draws under it.
    caption_driver = r"""
#include "ce_museum_label_rules.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
static void Need(bool ok, const char *why) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(2); } }
static bool Near(float a, float b) { return std::fabs(a - b) < 0.01f; }
int main() {
    // A short title: fully open it is the text plus 70, and the text clears both bracket caps (half the bar's height each).
    CeMuseumCaptionFit f = CeMuseumCaptionLayout(200.0f, 46.0f, 1280.0f, 1.0f, 1.0f, 0.0f);
    Need(Near(f.fullWidth, 270.0f) && Near(f.barWidth, 270.0f) && Near(f.scaleX, 1.0f) && !f.overflow, "a short title is its text plus 70");
    Need(f.textRoom >= 200.0f && Near(f.textRoom, 270.0f - 46.0f), "its text clears both bracket caps");
    Need(Near(CeMuseumCaptionCapInset(46.0f), 23.0f) && Near(CeMuseumCaptionCapInset(60.0f), 30.0f), "a cap reaches half the bar's height inward");
    // While opening, the bar is never narrower than the text plus the two caps, and never wider than when open.
    float last = 0.0f;
    for (float open = 0.45f; open <= 1.0f; open += 0.05f) {
        const CeMuseumCaptionFit o = CeMuseumCaptionLayout(200.0f, 46.0f, 1280.0f, 1.0f, open, 0.0f);
        Need(o.barWidth >= 200.0f + 2.0f * 23.0f - 0.01f, "an opening bar always holds its text between the caps");
        Need(o.barWidth <= o.fullWidth + 0.01f && o.barWidth >= last - 0.01f, "an opening bar only grows, up to its full width");
        last = o.barWidth;
    }
    Need(Near(CeMuseumCaptionLayout(200.0f, 46.0f, 1280.0f, 1.0f, 0.45f, 0.0f).barWidth, 246.0f), "first visible: text plus two caps");
    // The bar is never wider than 60% of the view ON SCREEN, whatever the HUD scale and however long the text.
    const float views[4] = {640.0f, 1280.0f, 1920.0f, 3440.0f};
    const float scales[4] = {1.0f, 1.5f, 2.25f, 3.0f};
    for (int v = 0; v < 4; ++v) for (int k = 0; k < 4; ++k) for (float widest = 20.0f; widest < 2400.0f; widest += 37.0f) {
        const CeMuseumCaptionFit c = CeMuseumCaptionLayout(widest, 46.0f, views[v], scales[k], 1.0f, 0.0f);
        Need(c.barWidth * scales[k] <= views[v] * 0.60f + 0.05f, "the bar is at most 60 percent of the view on screen");
        Need(c.scaleX <= 1.0f && c.scaleX >= kCeMuseumCaptionMinSqueeze - 1e-4f, "text is squeezed at most to the floor");
        Need(c.overflow == (widest > c.textRoom), "overflow says exactly when the text needs wrapping or squeezing");
        if (!c.overflow) Need(Near(c.scaleX, 1.0f) && widest <= c.textRoom + 0.01f, "text that fits is not squeezed");
        else if (c.scaleX > kCeMuseumCaptionMinSqueeze + 1e-3f)
            Need(widest * c.scaleX <= c.textRoom + 0.05f, "squeezed text fits between the caps");
    }
    // A larger HUD scale leaves less room in the caption's own units: the same title wraps sooner, it does not spill.
    Need(CeMuseumCaptionLayout(500.0f, 46.0f, 1280.0f, 1.0f, 1.0f, 0.0f).fullWidth >
         CeMuseumCaptionLayout(500.0f, 46.0f, 1280.0f, 2.0f, 1.0f, 0.0f).fullWidth, "the HUD scale divides the room");
    Need(!CeMuseumCaptionLayout(500.0f, 46.0f, 1280.0f, 1.0f, 1.0f, 0.0f).overflow &&
         CeMuseumCaptionLayout(500.0f, 46.0f, 1280.0f, 2.0f, 1.0f, 0.0f).overflow, "a title that fits at 1x overflows, and wraps, at 2x");
    // A plaque keeps the radio's width for short text and grows for long text only as far as the rule allows.
    Need(Near(CeMuseumCaptionLayout(60.0f, 60.0f, 1280.0f, 1.0f, 1.0f, 230.0f).fullWidth, 230.0f), "a short plaque keeps the radio's width");
    Need(Near(CeMuseumCaptionLayout(400.0f, 60.0f, 1280.0f, 1.0f, 1.0f, 230.0f).fullWidth, 470.0f), "a longer plaque grows with its text");
    Need(Near(CeMuseumCaptionLayout(4000.0f, 60.0f, 1280.0f, 1.0f, 1.0f, 230.0f).fullWidth, 768.0f), "a plaque stops at 60 percent of the view");
    // Bad input draws nothing.
    Need(CeMuseumCaptionLayout(-1.0f, 46.0f, 1280.0f, 1.0f, 1.0f, 0.0f).barWidth == 0.0f &&
         CeMuseumCaptionLayout(100.0f, 46.0f, 0.0f, 1.0f, 1.0f, 0.0f).barWidth == 0.0f, "bad input gives an empty fit");
    Need(Near(CeMuseumCaptionLayout(200.0f, 46.0f, 1280.0f, 0.0f, 1.0f, 0.0f).fullWidth, 270.0f), "a zero HUD scale is taken as 1");
    // The break is the space nearest the middle; short text and text with no space are not broken.
    Need(CeMuseumCaptionSplit("Walk past the Battle Engine to enter the Grand Atrium - hold Shift to sprint.") == 40, "a long line breaks at its middle space");
    Need(CeMuseumCaptionSplit("aaaa bbbbbbbbbbbbbbbbbbbbbbbb") == 4 && CeMuseumCaptionSplit("aaaaaaaaaaaaaaaaaaaaaaaa bbbb") == 24,
         "a lopsided line breaks at its only space");
    Need(CeMuseumCaptionSplit("short one") == -1 && CeMuseumCaptionSplit("nospacesinthislongline") == -1 &&
         CeMuseumCaptionSplit(" leadingspaceonlyhere") == -1 && CeMuseumCaptionSplit((const char *)0) == -1 &&
         CeMuseumCaptionSplit(L"") == -1, "nothing to break: -1");
    Need(CeMuseumCaptionSplit(L"Central rotunda and living battle dioramas.") == 19, "wide text breaks the same way");
    return 0;
}
"""
    caption_mutants = [
        ("pad-too-thin", "static const float kCeMuseumCaptionPad = 70.0f;", "static const float kCeMuseumCaptionPad = 48.0f;"),
        ("cap-inset-ignored", "return barHeight > 0.0f ? barHeight * 0.5f : 0.0f;", "return barHeight > 0.0f ? 12.0f : 0.0f;"),
        ("hud-scale-ignored", "fit.most = viewWidth * kCeMuseumCaptionViewShare / scale;", "fit.most = viewWidth * kCeMuseumCaptionViewShare;"),
        ("no-view-cap", "if (full > fit.most)\n\t\tfull = fit.most;", "if (false)\n\t\tfull = fit.most;"),
        ("view-share-too-wide", "static const float kCeMuseumCaptionViewShare = 0.60f;", "static const float kCeMuseumCaptionViewShare = 0.90f;"),
        ("opening-bar-cuts-text", "float closed = widest * fit.scaleX + 2.0f * inset;", "float closed = widest * fit.scaleX + 24.0f;"),
        ("squeeze-unbounded", "if (fit.scaleX < kCeMuseumCaptionMinSqueeze)\n\t\t\tfit.scaleX = kCeMuseumCaptionMinSqueeze;", "(void)kCeMuseumCaptionMinSqueeze;"),
        ("split-at-first-space", "if (away < bestAway)", "if (best < 0)"),
        ("min-width-dropped", "if (full < minWidth)\n\t\tfull = minWidth;", "(void)minWidth;"),
    ]
    rules_text = RULES.read_text()
    with tempfile.TemporaryDirectory() as folder:
        folder = Path(folder)
        (folder / "caption.cpp").write_text(caption_driver)
        built, out = run(["g++", "-std=c++14", "-O1", "-Wall", "-Wextra", "-Werror", "-I" + str(GAME),
                          str(folder / "caption.cpp"), "-o", str(folder / "caption")])
        check("the caption rule driver builds", built, out)
        if built:
            good, out = run([str(folder / "caption")])
            check("captions keep text between the bracket caps and inside 60% of the view at every HUD scale", good, out)
        for label, old, new in caption_mutants:
            if rules_text.count(old) != 1:
                check("caption mutant " + label + " applies", False, "source text not found once: " + old)
                continue
            mdir = folder / label
            mdir.mkdir()
            (mdir / RULES.name).write_text(rules_text.replace(old, new))
            built, out = run(["g++", "-std=c++14", "-O1", "-I" + str(mdir), "-I" + str(GAME),
                              str(folder / "caption.cpp"), "-o", str(mdir / "caption")])
            if not built:
                check("caption mutant " + label + " is caught", False, out)
                continue
            caught, _ = run([str(mdir / "caption")], 2)
            check("caption mutant " + label + " is caught", caught)

    label_src = LABEL.read_text()
    check("both caption bars use the one rule, read the HUD scale, and wrap before they squeeze",
          label_src.count("CeMuseumCaptionLayout(") >= 4 and label_src.count("CeHudScaleFactor()") == 2
          and label_src.count("WrapLine(font,") == 2 and "widest + 48.0f" not in label_src and "viewW - 36.0f" not in label_src)
    check("the exhibit marker is the brass diamond, not a font letter",
          'L"v"' not in label_src and "PortCeMuseumDrawExhibitMarker(pixel.x, pixel.y - size * 1.6f, size);" in label_src
          and 'extern "C" void PortCeMuseumDrawExhibitMarker(float cx, float cy, float size) {' in (GAME / "ce_museum_hall.cpp").read_text())
    check("a plaque shows name, class and one fact; a control hint only while the visitor aims at the control",
          "ExhibitCapsule(label, label.line, gInspecting ? label.detail : NULL," in label_src
          and "if (PortCeMuseumInteractiveAim && PortCeMuseumInteractiveAim())\n\t\t\t\tCopy(card.note, inote);" in label_src)

    # The visit engine runs the place tracking: armed when the hall of displays is built, stepped once a visit frame, dropped at the end.
    engine = VISIT_ENGINE.read_text()
    frame = engine[engine.find("extern \"C\" void PortCeMuseumVisitFrame(void)"):engine.find("bool CeMuseumVisit::GenerateInstalled()")]
    check("the visit frame steps the place tracking once", frame.count("CeMuseumVisitPlacesStep(&gPlaces, here.X, here.Y);") == 1)
    check("the built hall arms the place tracking", engine.count("CeMuseumVisitPlacesArm(&gPlaces, exhibition);") == 1)
    check("the visit's end and a new hall drop it", engine.count("CeMuseumVisitPlacesDrop(&gPlaces);") == 2)

    # The second line of a label sits on translucent HUD radio capsules under MODULATE2X / StageColour doubling:
    # brighter than the front end's dim 0xff545454 grey, and strictly below 0x80 per channel so doubling stays a crisp light grey instead of white.
    label_grey = re.search(r"\bkGrey\s*=\s*(0x[0-9a-fA-F]+)", (GAME / "ce_museum_label.cpp").read_text())
    frontend_grey = re.search(r"static const DWORD\s+kGrey\s*=\s*(0x[0-9a-fA-F]+)", (GAME / "ce_frontend.cpp").read_text())
    check("the label's grey and the front end's grey were found", bool(label_grey and frontend_grey))
    if label_grey and frontend_grey:
        grey = int(label_grey.group(1), 16)
        fe_grey = int(frontend_grey.group(1), 16)
        ch = (grey >> 16) & 0xff
        fe_ch = (fe_grey >> 16) & 0xff
        neutral = (grey >> 24) == 0xff and ch == ((grey >> 8) & 0xff) == (grey & 0xff)
        check("the label's second line is a crisp neutral light grey under doubling (brighter than front end grey, below 0x80 saturation)",
              neutral and fe_ch < ch < 0x80,
              "label %s (ch=%#x), front end %s (ch=%#x)" % (label_grey.group(1), ch, frontend_grey.group(1), fe_ch))
        check("the grey check bites: both dim front-end grey and saturating 0xffa8a8a8 fail it",
              not (fe_ch < fe_ch < 0x80) and not (fe_ch < 0xa8 < 0x80))

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
