#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Authored CPU controls for the Goodies Gallery (port/game/ce_museum.cpp, patch 0199, the CE page's Goodies Gallery row).

No game asset, save file, window, game process or network is used, and nothing is written under the source tree except the scratch folder
that build.patched_text makes in local-data (a read-only source tree with a writable local-data folder is enough; --out names where the
generated source, compiler output, executables and the receipt go).

What is run, all with the compiler's address and undefined-behaviour sanitizers and no recovery:

* the visitor controller alone (port/tools/tests/ce_museum_driver.cpp), against stand-in page and state labels;
* a bridge program that compiles the real text of the hooks around stand-in game types: the Goodies gallery's state reader and writer,
  the load admission and the shutdown hook, the front end's page hook (all taken from the mirror text with patch 0199 applied), and the
  entry function with the CE-menu switch it asks (taken from ce_frontend.cpp and ce.cpp). The visit is driven end to end: entry, every
  exhibit kind, an unlock write, a load, the Back route, every other way out, invalid page requests, and a career witness compared byte
  for byte at each step;
* the entry in the places the CE menus are off (a harness switch, -autoconfigtest, the demo, the CE layer forced off): each refuses and
  leaves the front end and the witness as they were;
* one mutant per guard of the controller, of every hook and of the entry: each must be caught by the controls that cover it.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SANITIZE = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]


def masked(text):
    """text with every comment blanked (same length), so that a pattern cannot match inside one."""
    return re.sub(r"/\*.*?\*/|//[^\n]*", lambda m: "".join("\n" if c == "\n" else " " for c in m.group()), text, flags=re.S)


def function(text, pattern):
    """(whole function text, body text) of the function whose opening `{` the pattern ends on; braces are matched on the masked text."""
    flat = masked(text)
    match = re.search(pattern, flat)
    if not match:
        raise ValueError("a required source function is absent")
    start = match.end()
    end, depth = start, 1
    while end < len(flat) and depth:
        depth += (flat[end] == "{") - (flat[end] == "}")
        end += 1
    if depth:
        raise ValueError("a source function is incomplete")
    return text[match.start():end], text[start:end - 1]


def only_in_ce_version(text, needles):
    """Every line that holds one of the needles lies in the CE_VERSION branch of a conditional (not its #else branch)."""
    stack = []      # per open conditional: None (another condition) or True/False (the CE_VERSION conditional, in its first/else branch)
    for line in text.split("\n"):
        directive = re.match(r"\s*#\s*(\w+)\s*(\w*)", line)
        if directive:
            word, arg = directive.groups()
            if word in ("if", "ifdef", "ifndef"):
                stack.append(True if (word == "ifdef" and arg == "CE_VERSION") else None)
            elif word == "else" and stack and stack[-1] is not None:
                stack[-1] = False
            elif word == "endif" and stack:
                stack.pop()
            continue
        if any(n in line for n in needles) and not any(frame is True for frame in stack):
            return False
    return True


def guarded(text, marker):
    """The `#ifdef CE_VERSION` ... `#endif` block of patch 0199 that holds the marker text (the directives included)."""
    at = text.find(marker)
    if at < 0 or text.count(marker) != 1:
        raise ValueError("a patch 0199 hook marker is not found exactly once")
    begin = text.rfind("#ifdef CE_VERSION", 0, at)
    end = text.find("#endif", at)
    if begin < 0 or end < 0 or "#endif" in text[begin:at]:
        raise ValueError("a patch 0199 hook is not inside its own CE_VERSION block")
    return text[begin:end + len("#endif")]


# The bridge: authored stand-ins around the real text. @NAME@ marks where the real text goes.
BRIDGE = r'''
#define CE_VERSION "test"
#include "ce_museum.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

typedef int SINT;
enum EGoodieState : int { GS_UNKNOWN = 70, GS_INSTRUCTIONS = 71, GS_NEW = 72, GS_OLD = 73 };
enum EGoodieType : int { GT_IMAGE = 80, GT_MESH = 81, GT_FMV = 82, GT_LEVEL = 83, GT_CHEAT = 84 };
// FEP_LANGUAGE_TEST is the CE page's slot (ce_frontend.cpp). The gallery's Back asks for FEP_SAVEGAME, the page that saves the career and
// then goes on to the main menu. The values are authored labels, not the game's.
enum EFrontEndPage : int { FEP_MAIN = 1, FEP_GOODIES = 2, FEP_LANGUAGE_TEST = 3, FEP_OTHER = 4, FEP_SAVEGAME = 5 };
static const int MAX_NUM_GOODIES = 5;

static int checks = 0, failures = 0, type_errors = 0, asset_loads = 0, shutdowns = 0, options_saves = 0;
static bool ischeatactive = false, ischeat5active = false;
static void check(bool ok, int line) {
    ++checks;
    if (!ok) {
        ++failures;
        std::fprintf(stderr, "check failed at bridge control %d\n", line);
    }
}
#define CHECK(x) check((x), __LINE__)

// The career as the gallery sees it: five exhibit states, counters of what was read and written, and bytes the gallery must never touch.
struct Career {
    EGoodieState states[MAX_NUM_GOODIES];
    int reads, writes, saves;
    unsigned char unknown[19];
    EGoodieState GetGoodieState(int n) { ++reads; return states[n]; }
    void SetGoodieState(int n, EGoodieState s) { ++writes; states[n] = s; }
};
static Career CAREER;
static int get_goodie_number(int x, int y) { return y == 0 && x >= 0 && x < MAX_NUM_GOODIES ? x : -1; }
static EGoodieType get_goodie_type_hack(int n) {
    static const EGoodieType types[] = {GT_IMAGE, GT_MESH, GT_FMV, GT_LEVEL, GT_CHEAT};
    if (n < 0 || n >= MAX_NUM_GOODIES) { ++type_errors; return GT_LEVEL; }
    return types[n];
}

// ---- the Goodies gallery: the patched state reader and writer, as they are in the mirror text
@VISUAL@
@GET@
@SET@

// ---- the gallery's load admission and shutdown, with the cursor and the original continuation as stand-ins
static int mCX = 0, mCY = 0;
static void start_loading_goody() {
@LOAD@
    ++asset_loads;
}
static void gallery_shutdown() {
@SHUTDOWN@
    ++shutdowns;
}

// ---- the CE-menu switch (ce.cpp's PortCeLayer, ce_frontend.cpp's PortCeMenus) and what it asks
@LAYER@
static struct { int mAutoConfigTest; } CLIPARAMS;
static int demo = 0;
#define PLAYABLE_DEMO demo
bool PortCeExperimental(const char *) { return false; }		// ce.cpp's switches for the unfinished features: asked once each as the CE menus are admitted; not what these controls measure
void PortCeExperimentalBlock() {}		// ce.cpp: adds the [Experimental] lines to ce.ini; not what these controls measure
@MENUS@

// ---- the front end: its page table and SetPage, the patched hook then the original continuation
struct Page {};
struct CFrontEnd {
    Page storage;
    Page *mPages[8];
    EFrontEndPage mActivePage, last;
    int rejected, last_time;
    CFrontEnd() : mActivePage(FEP_LANGUAGE_TEST), last(FEP_LANGUAGE_TEST), rejected(0), last_time(0) { reset_pages(); }
    void reset_pages() {
        std::memset(mPages, 0, sizeof(mPages));
        mPages[FEP_MAIN] = mPages[FEP_GOODIES] = mPages[FEP_LANGUAGE_TEST] = mPages[FEP_OTHER] = mPages[FEP_SAVEGAME] = &storage;
    }
    void SetPage(EFrontEndPage page, int time = 0) {
@PAGE@
        // The original destination reaching the normal continuation is what `last` and `last_time` witness; the save page saves.
        last = page;
        last_time = time;
        if ((unsigned)page >= sizeof(mPages) / sizeof(mPages[0]) || !mPages[page]) {
            ++rejected;
            return;
        }
        if (page == FEP_SAVEGAME) ++CAREER.saves;
        mActivePage = page;
    }
};
static CFrontEnd entry_frontend;
#define FRONTEND entry_frontend
@ENTRY@
// Saving default options also begins ordinary navigation. Model both effects: an isolated write counter hid a trapped-gallery bug.
static void ordinary_save_options() {
    ++options_saves;
    FRONTEND.SetPage(FEP_SAVEGAME, 0);
}
static void gallery_back() {
@OPTIONS@
    ordinary_save_options();
}
#undef FRONTEND

// The career's data, not its read counters (reading is allowed outside a visit, and inside one the counters are checked on their own).
static bool same(const Career &a, const Career &b) {
    return std::memcmp(a.states, b.states, sizeof(a.states)) == 0 && std::memcmp(a.unknown, b.unknown, sizeof(a.unknown)) == 0 &&
           a.writes == b.writes && a.saves == b.saves;
}

// The entry where the CE menus are off: nothing opens, nothing changes.
static int refused() {
    Career before = CAREER;
    CHECK(!PortCeMenus());
    CHECK(!PortCeMuseumOpen());
    CHECK(!PortCeMuseumActive());
    CHECK(entry_frontend.mActivePage == FEP_LANGUAGE_TEST && entry_frontend.last == FEP_LANGUAGE_TEST);
    CHECK(same(before, CAREER));
    std::printf("museum_entry_refused checks=%d failures=%d\n", checks, failures);
    return failures ? 1 : 0;
}

// One whole visit and the ways out of it.
static int visit() {
    CHECK(PortCeMenus());
    // Outside a visit the gallery is the game's: the career's state is read, an unlock is written, a picture loads.
    CHECK(get_goodie_state(0, 0) == GS_NEW);
    set_goodie_state(0, 0, GS_INSTRUCTIONS);
    CHECK(CAREER.writes == 1 && get_goodie_state(0, 0) == GS_INSTRUCTIONS);
    asset_loads = 0; mCX = 4; mCY = 0;
    start_loading_goody();
    CHECK(asset_loads == 1);
    CAREER.reads = CAREER.writes = CAREER.saves = 0;
    const Career before = CAREER;

    // The visit begins on the CE page and reaches the gallery.
    entry_frontend.mActivePage = FEP_LANGUAGE_TEST;
    CHECK(PortCeMuseumOpen());
    CHECK(PortCeMuseumActive());
    CHECK(entry_frontend.mActivePage == FEP_GOODIES && entry_frontend.last == FEP_GOODIES && entry_frontend.last_time == 70);
    CHECK(!PortCeMuseumOpen() && PortCeMuseumActive());      // a second Select while it opens starts nothing new
    CHECK(entry_frontend.mActivePage == FEP_GOODIES);
    CHECK(shutdowns == 0);

    // Every exhibit kind, with the unlock cheats on and off: a picture and a model read as viewed and load; a video, a level and a cheat
    // read as unavailable and never load; an unlock write reaches nothing.
    for (int cheats = 0; cheats < 2; ++cheats) {
        ischeatactive = ischeat5active = cheats != 0;
        for (int n = 0; n < MAX_NUM_GOODIES; ++n) {
            const bool visual = n < 2;
            CHECK(get_goodie_state(n, 0) == (visual ? GS_OLD : GS_UNKNOWN));
            set_goodie_state(n, 0, GS_OLD);
            asset_loads = 0; mCX = n; mCY = 0;
            start_loading_goody();
            CHECK(asset_loads == (visual ? 1 : 0));
        }
    }
    ischeatactive = ischeat5active = false;
    // Cells that are no exhibit: unavailable, never loaded, never asked for a type.
    CHECK(get_goodie_state(-1, 0) == GS_UNKNOWN && get_goodie_state(MAX_NUM_GOODIES, 0) == GS_UNKNOWN && get_goodie_state(0, 1) == GS_UNKNOWN);
    asset_loads = 0; mCX = -1; mCY = 0;
    start_loading_goody();
    CHECK(asset_loads == 0 && type_errors == 0);
    CHECK(CAREER.writes == 0 && CAREER.reads == 0);
    CHECK(same(before, CAREER));

    // Back: the gallery saves its default options and asks for the save page, which would save the career; a visit saves no options, goes to
    // the CE page instead, as the CE page opens from the main menu, and saves nothing. The visit is over.
    options_saves = 0;
    gallery_back();
    CHECK(options_saves == 0);
    CHECK(entry_frontend.last == FEP_LANGUAGE_TEST && entry_frontend.mActivePage == FEP_LANGUAGE_TEST && entry_frontend.last_time == 70);
    CHECK(!PortCeMuseumActive() && CAREER.saves == 0);
    CHECK(same(before, CAREER));
    // And the gallery is the game's again: the career's own state is read, and the same unlock is written.
    CHECK(get_goodie_state(0, 0) == GS_INSTRUCTIONS && get_goodie_state(4, 0) == GS_NEW);
    set_goodie_state(2, 0, GS_OLD);
    CHECK(CAREER.states[2] == GS_OLD);
    CAREER = before;
    // The front end alone is the game's too: outside a visit the gallery's Back saves the default options and asks for the save page, which
    // saves, with its own time.
    entry_frontend.mActivePage = FEP_GOODIES;
    gallery_back();
    CHECK(options_saves == 1);
    CHECK(entry_frontend.last == FEP_SAVEGAME && entry_frontend.mActivePage == FEP_SAVEGAME && entry_frontend.last_time == 0 && CAREER.saves == 1);
    CAREER = before;
    options_saves = 0;

    // Any other way out of the gallery ends the visit and goes where it was asked to, at the time it was asked, and saves nothing.
    entry_frontend.mActivePage = FEP_LANGUAGE_TEST;
    CHECK(PortCeMuseumOpen());
    entry_frontend.SetPage(FEP_OTHER, 9);
    CHECK(entry_frontend.last == FEP_OTHER && entry_frontend.mActivePage == FEP_OTHER && entry_frontend.last_time == 9 && !PortCeMuseumActive());
    entry_frontend.mActivePage = FEP_LANGUAGE_TEST;
    CHECK(PortCeMuseumOpen());
    entry_frontend.SetPage(FEP_MAIN, 70);
    CHECK(entry_frontend.last == FEP_MAIN && entry_frontend.mActivePage == FEP_MAIN && entry_frontend.last_time == 70 && !PortCeMuseumActive());
    CHECK(same(before, CAREER));

    // The gallery closing for any reason ends it, and a new visit can begin.
    entry_frontend.mActivePage = FEP_LANGUAGE_TEST;
    CHECK(PortCeMuseumOpen());
    shutdowns = 0;
    gallery_shutdown();
    CHECK(shutdowns == 1 && !PortCeMuseumActive());
    CHECK(get_goodie_state(0, 0) == GS_INSTRUCTIONS);
    entry_frontend.mActivePage = FEP_LANGUAGE_TEST;
    CHECK(PortCeMuseumOpen() && PortCeMuseumActive());
    entry_frontend.SetPage(FEP_SAVEGAME, 0);
    CHECK(!PortCeMuseumActive() && entry_frontend.last == FEP_LANGUAGE_TEST && entry_frontend.last_time == 70 && CAREER.saves == 0);

    // A visit that does not start on the CE page never keeps the gallery as a visitor's.
    entry_frontend.mActivePage = FEP_OTHER;
    CHECK(!PortCeMuseumOpen());
    CHECK(entry_frontend.last == FEP_GOODIES && !PortCeMuseumActive());
    CHECK(get_goodie_state(1, 0) == GS_NEW);
    entry_frontend.SetPage(FEP_MAIN);
    CHECK(entry_frontend.last == FEP_MAIN);

    // A page request that is not a page: the visit ends and the request goes on to the original path unchanged.
    for (int bad : {-1, 0, 6, 8, 100}) {
        entry_frontend.mActivePage = FEP_LANGUAGE_TEST;
        const int rejected = entry_frontend.rejected;
        CHECK(PortCeMuseumOpen());
        entry_frontend.SetPage((EFrontEndPage)bad);
        CHECK((int)entry_frontend.last == bad && entry_frontend.rejected == rejected + 1 && !PortCeMuseumActive());
    }
    // The way back must exist to be taken: with no page behind it the request is the original's, not a redirect.
    entry_frontend.mActivePage = FEP_LANGUAGE_TEST;
    CHECK(PortCeMuseumOpen());
    entry_frontend.mPages[FEP_SAVEGAME] = nullptr;
    const int rejected = entry_frontend.rejected;
    entry_frontend.SetPage(FEP_SAVEGAME, 0);
    CHECK(entry_frontend.last == FEP_SAVEGAME && entry_frontend.rejected == rejected + 1 && !PortCeMuseumActive());
    entry_frontend.reset_pages();
    CHECK(same(before, CAREER));

    std::printf("museum_bridge_controls=%d failures=%d\n", checks, failures);
    return failures ? 1 : 0;
}

int main(int argc, char **argv) {
    const char *scenario = argc > 1 ? argv[1] : "visit";
    std::memset(&CAREER, 0x5a, sizeof(CAREER));
    for (int n = 0; n < MAX_NUM_GOODIES; ++n) CAREER.states[n] = GS_NEW;
    CAREER.reads = CAREER.writes = CAREER.saves = 0;
    PortCeMuseumEnd();
    if (!std::strcmp(scenario, "autoconfig")) CLIPARAMS.mAutoConfigTest = 1;
    if (!std::strcmp(scenario, "demo")) demo = 1;
    if (!std::strcmp(scenario, "visit")) return visit();
    return refused();
}
'''

# The places the CE menus are off, as the environment makes them (ce.cpp: a harness switch turns the layer off unless it is forced) and
# as the command line and the demo make them: name -> (environment, argument for the bridge).
HARNESS = ("BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD")
SCENARIOS = {
    "layer-forced-off": ({"BEA_PORT_CE_LAYER": "0"}, "layer-forced-off"),
    "clock": ({"BEA_PORT_CLOCK": "shim"}, "clock"),
    "tape": ({"BEA_PORT_TAPE": "a.tape"}, "tape"),
    "probes": ({"BEA_PORT_PROBES": "a.txt"}, "probes"),
    "record": ({"BEA_PORT_RECORD": "a.tape"}, "record"),
    "autoconfig": ({}, "autoconfig"),
    "demo": ({}, "demo"),
}


def environment(extra):
    env = {k: v for k, v in os.environ.items() if not k.startswith("BEA_PORT_")}
    env["ASAN_OPTIONS"] = "detect_leaks=0:abort_on_error=0"
    env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=0"
    env.update(extra)
    return env


def bridge_source(sources, layer_source, frontend_source):
    good, front = sources["FEPGoodies.cpp"], sources["FrontEnd.cpp"]
    visual, _ = function(good, r"(?m)^[ \t]*static\s+EGoodieState\s+ce_museum_visual_state\s*\([^{};]*\)\s*\{")
    get, _ = function(good, r"(?m)^[ \t]*static\s+EGoodieState\s+get_goodie_state\s*\([^{};]*\)\s*\{")
    set_, _ = function(good, r"(?m)^[ \t]*static\s+void\s+set_goodie_state\s*\([^{};]*\)\s*\{")
    parts = {
        "VISUAL": visual, "GET": get, "SET": set_,
        "LOAD": guarded(good, "patch 0199: a visitor starts only a picture or a model"),
        "SHUTDOWN": guarded(good, "patch 0199: the gallery is closing"),
        "OPTIONS": guarded(good, "patch 0199: a visitor saves no default options"),
        "PAGE": guarded(front, "patch 0199: admit a visitor redirect"),
        "LAYER": function(layer_source, r"(?m)^bool\s+PortCeLayer\s*\(\s*\)\s*\{")[0],
        "MENUS": function(frontend_source, r"(?m)^bool\s+PortCeMenus\s*\(\s*\)\s*\{")[0],
        "ENTRY": function(frontend_source, r'(?m)^extern "C" bool\s+PortCeMuseumOpen\s*\(\s*void\s*\)\s*\{')[0],
    }
    text = BRIDGE
    for key, value in parts.items():
        text = text.replace("@" + key + "@", value)
    if re.search(r"@[A-Z]+@", text):
        raise ValueError("a bridge placeholder is unfilled")
    return text


def diagnostics(raw):
    """Only the compiler's own messages, never the source lines it echoes."""
    out = []
    for line in raw.decode("utf-8", "replace").splitlines():
        if re.match(r"^\S+:\d+:\d+: (?:fatal )?error:", line):
            out.append(line[:240])
    return out[:12]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--compiler", default="clang++")
    ap.add_argument("--out", type=Path, help="a new or empty folder for the generated source, executables and the receipt")
    ap.add_argument("--keep-going", action="store_true", help="run every case even after a failure (the exit status is unchanged)")
    args = ap.parse_args(argv)
    compiler = shutil.which(args.compiler)
    if not compiler:
        ap.exit(2, "museum controls require an installed C++ compiler\n")
    if args.out:
        out = args.out.resolve()
        out.mkdir(parents=True, exist_ok=True)
        if any(out.iterdir()):
            ap.exit(2, "--out must be new or empty\n")
    else:
        parent = ROOT / "local-data" / "museum-tests"
        parent.mkdir(parents=True, exist_ok=True)
        out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    os.chmod(out, 0o700)
    sys.dont_write_bytecode = True
    sys.path.insert(0, str(ROOT / "port"))
    import build
    sources = build.patched_text(["FEPGoodies.cpp", "FrontEnd.cpp"])
    controller = ROOT / "port/game/ce_museum.cpp"
    driver = ROOT / "port/tools/tests/ce_museum_driver.cpp"
    layer_source = (ROOT / "port/platform/common/ce.cpp").read_text()
    frontend_source = (ROOT / "port/game/ce_frontend.cpp").read_text()
    records = []
    stop = []

    def compile_binary(name, source, implementation, fatal=True):
        binary = out / name
        # The controls themselves compile without a warning; a mutant may leave a variable unused, which says nothing about its being caught.
        command = [compiler, "-std=c++17", "-O1", "-g", "-Wall", "-Wextra"] + (["-Werror"] if fatal else []) + [*SANITIZE,
                   "-I", str(ROOT / "port/game"), str(source), str(implementation), "-o", str(binary)]
        result = subprocess.run(command, capture_output=True, timeout=120, env=environment({}))
        (out / (name + "-compile.stderr.private")).write_bytes(result.stderr)
        if result.returncode:
            for line in diagnostics(result.stderr):
                print("  " + line, file=sys.stderr)
            if fatal:
                raise RuntimeError("museum control " + name + " does not compile")
            records.append({"name": name, "error": "does not compile", "pass": False})
            stop.append(name)
            return None
        return binary

    def run_binary(name, binary, arguments=(), extra=None):
        result = subprocess.run([str(binary), *arguments], capture_output=True, timeout=60, env=environment(extra or {}))
        (out / (name + "-run.stdout.private")).write_bytes(result.stdout)
        (out / (name + "-run.stderr.private")).write_bytes(result.stderr)
        return result.returncode

    def case(name, binary, expect, arguments=(), extra=None):
        code = run_binary(name, binary, arguments, extra)
        passed = (code == 0) if expect == "pass" else (code != 0)
        records.append({"name": name, "exit": code, "expect": expect, "pass": passed})
        if not passed:
            stop.append(name)
            print("museum control failed: " + name + " (exit " + str(code) + ", expected " + expect + ")", file=sys.stderr)
        return passed

    # 1. the controller alone, and the bridge of the real hook text
    controller_driver = compile_binary("controller", driver, controller)
    case("controller", controller_driver, "pass")
    bridge_text = bridge_source(sources, layer_source, frontend_source)
    bridge_file = out / "bridge.private.cpp"
    bridge_file.write_text(bridge_text)
    bridge = compile_binary("bridge", bridge_file, controller)
    case("bridge-visit", bridge, "pass", ["visit"])
    for name, (extra, argument) in SCENARIOS.items():
        case("bridge-refused-" + name, bridge, "pass", [argument], extra)
    # The harness switches are no hindrance once the CE layer is forced on, as ce.cpp documents (screenshots of the CE layer in a replay).
    case("bridge-forced-on-visit", bridge, "pass", ["visit"], {"BEA_PORT_CE_LAYER": "1", "BEA_PORT_CLOCK": "shim"})

    # 2. one mutant per guard: the controller (against the controller driver and the bridge), the hooks, the entry and the switch
    original = controller.read_text()
    # (End's clearing of `entered` is not tried: Begin clears it again before anything can ask, so no control could tell.)
    controller_mutants = (
        ("unlock-write", "return !visitor;", "return true;"),
        ("load-any", "return !visitor || visual;", "(void)visual; return true;"),
        ("load-none", "return !visitor || visual;", "(void)visual; return !visitor;"),
        ("read-all-viewed", "return visual ? viewed : unavailable;", "(void)visual; (void)unavailable; return viewed;"),
        ("read-all-unavailable", "return visual ? viewed : unavailable;", "(void)visual; (void)viewed; return unavailable;"),
        ("read-ignores-visit", "if (!visitor) return ordinary;", "if (false) return ordinary;"),
        ("begin-gate", "if (!offered || visitor) return false;", "(void)offered; if (visitor) return false;"),
        ("begin-again", "if (!offered || visitor) return false;", "if (!offered) return false;"),
        ("end-keeps-visit", "extern \"C\" void PortCeMuseumEnd(void) {\n    visitor = false;\n", "extern \"C\" void PortCeMuseumEnd(void) {\n"),
        ("page-no-redirect", "return back ? ce : requested;", "(void)back; return requested;"),
        ("page-redirect-all", "return back ? ce : requested;", "(void)back; return ce;"),
        ("page-keeps-visit-elsewhere", "    if (current != gallery) {\n        PortCeMuseumEnd();\n", "    if (current != gallery) {\n"),
        ("page-enters-from-anywhere", "if (current == ce && requested == gallery) {", "if (requested == gallery) {"),
        ("page-wrong-entry-keeps-visit", "        PortCeMuseumEnd();\n        return requested;\n    }\n    if (current != gallery)",
         "        return requested;\n    }\n    if (current != gallery)"),
        ("page-never-entered", "            entered = true;\n", "            entered = false;\n"),
        ("page-other-keeps-visit", "    const bool back = requested == ordinary_back;\n    PortCeMuseumEnd();", "    const bool back = requested == ordinary_back;"),
    )
    for name, before, after in controller_mutants:
        if original.count(before) != 1:
            raise ValueError("a controller mutation target is not found exactly once: " + name)
        changed = out / ("mutant-controller-" + name + ".private.cpp")
        changed.write_text(original.replace(before, after, 1))
        a = compile_binary("mutant-controller-" + name + "-driver", driver, changed, fatal=False)
        b = compile_binary("mutant-controller-" + name + "-bridge", bridge_file, changed, fatal=False)
        if not (a and b):
            continue
        ca = run_binary("mutant-controller-" + name + "-driver", a)
        cb = run_binary("mutant-controller-" + name + "-bridge", b, ["visit"])
        caught = ca != 0 or cb != 0
        records.append({"name": "mutant-controller-" + name, "driver_exit": ca, "bridge_exit": cb, "expect": "caught", "pass": caught})
        if not caught:
            stop.append("mutant-controller-" + name)
            print("museum mutant survived: controller " + name, file=sys.stderr)

    def bridge_mutant(name, before, after, scenario, count=1):
        text = bridge_text
        if text.count(before) != count:
            raise ValueError("a bridge mutation target is not found as often as expected: " + name)
        changed = out / ("mutant-" + name + ".private.cpp")
        changed.write_text(text.replace(before, after))
        binary = compile_binary("mutant-" + name, changed, controller, fatal=False)
        if not binary:
            return
        extra, argument = SCENARIOS.get(scenario, ({}, scenario))
        case("mutant-" + name, binary, "caught", [argument], extra)

    # the hooks (the text patch 0199 puts in the gallery and the front end)
    bridge_mutant("hook-get-filter-off", "if (PortCeMuseumActive())\n\t\treturn ce_museum_visual_state", "if (false)\n\t\treturn ce_museum_visual_state", "visit")
    bridge_mutant("hook-get-bounds-off", "if (goodie_num < 0 || goodie_num >= MAX_NUM_GOODIES)\n\t\treturn GS_UNKNOWN;", "if (false)\n\t\treturn GS_UNKNOWN;", "visit")
    bridge_mutant("hook-get-meshes-hidden", "type == GT_IMAGE || type == GT_MESH", "type == GT_IMAGE", "visit")
    bridge_mutant("hook-get-everything-visible", "type == GT_IMAGE || type == GT_MESH", "true", "visit")
    bridge_mutant("hook-set-writes", "if (PortCeMuseumCanChangeUnlocks())", "if (true)", "visit")
    bridge_mutant("hook-load-gate-off", "if (PortCeMuseumActive() && !PortCeMuseumCanLoad(", "if (false && !PortCeMuseumCanLoad(", "visit")
    bridge_mutant("hook-load-everything", "!PortCeMuseumCanLoad(get_goodie_state(mCX, mCY) == GS_OLD)", "!PortCeMuseumCanLoad(true)", "visit")
    bridge_mutant("hook-options-saved-in-visit", "if (PortCeMuseumActive())\t\t// patch 0199: a visitor saves no default options",
                  "if (false)\t\t// patch 0199: a visitor saves no default options", "visit")
    bridge_mutant("hook-options-never-saved", "if (PortCeMuseumActive())\t\t// patch 0199: a visitor saves no default options",
                  "if (true)\t\t// patch 0199: a visitor saves no default options", "visit")
    bridge_mutant("hook-back-no-navigation", "FRONTEND.SetPage(FEP_LANGUAGE_TEST, 70);", ";", "visit")
    bridge_mutant("hook-back-wrong-destination", "FRONTEND.SetPage(FEP_LANGUAGE_TEST, 70);", "FRONTEND.SetPage(FEP_MAIN, 70);", "visit")
    bridge_mutant("hook-back-wrong-transition", "FRONTEND.SetPage(FEP_LANGUAGE_TEST, 70);", "FRONTEND.SetPage(FEP_LANGUAGE_TEST, 0);", "visit")
    bridge_mutant("hook-back-old-trapped-gallery", "if (PortCeMuseumActive())\t\t// patch 0199: a visitor saves no default options\n\t\t\t{\n\t\t\t\tFRONTEND.SetPage(FEP_LANGUAGE_TEST, 70);\n\t\t\t}\n\t\t\telse",
                  "if (!PortCeMuseumActive())\t\t// patch 0199: a visitor saves no default options", "visit")
    bridge_mutant("hook-shutdown-keeps-visit", "PortCeMuseumEnd();\t\t// patch 0199: the gallery is closing", ";", "visit")
    bridge_mutant("hook-page-off", "if (PortCeMuseumActive())\n\t{\n\t\tconst unsigned pageCount", "if (false)\n\t{\n\t\tconst unsigned pageCount", "visit")
    bridge_mutant("hook-page-range-off", "if (((unsigned)page >= pageCount) || (mPages[page] == 0))", "if (mPages[page] == 0)", "visit")
    bridge_mutant("hook-page-missing-page-off", "if (((unsigned)page >= pageCount) || (mPages[page] == 0))", "if ((unsigned)page >= pageCount)", "visit")
    bridge_mutant("hook-page-wrong-gallery", "FEP_LANGUAGE_TEST, FEP_GOODIES, FEP_SAVEGAME)", "FEP_LANGUAGE_TEST, FEP_OTHER, FEP_SAVEGAME)", "visit")
    bridge_mutant("hook-page-wrong-ce", "FEP_LANGUAGE_TEST, FEP_GOODIES, FEP_SAVEGAME)", "FEP_OTHER, FEP_GOODIES, FEP_SAVEGAME)", "visit")
    bridge_mutant("hook-page-wrong-back", "FEP_LANGUAGE_TEST, FEP_GOODIES, FEP_SAVEGAME)", "FEP_LANGUAGE_TEST, FEP_GOODIES, FEP_MAIN)", "visit")
    bridge_mutant("hook-page-wrong-back-other", "FEP_LANGUAGE_TEST, FEP_GOODIES, FEP_SAVEGAME)", "FEP_LANGUAGE_TEST, FEP_GOODIES, FEP_OTHER)", "visit")
    bridge_mutant("hook-page-ignores-answer", "page = (EFrontEndPage)PortCeMuseumPage(mActivePage, page, FEP_LANGUAGE_TEST, FEP_GOODIES, FEP_SAVEGAME);",
                  "(void)PortCeMuseumPage(mActivePage, page, FEP_LANGUAGE_TEST, FEP_GOODIES, FEP_SAVEGAME);", "visit")
    bridge_mutant("hook-page-keeps-time", "if (page != asked)\n\t\t\t\ttime = 70;", ";", "visit")
    bridge_mutant("hook-page-time-always", "if (page != asked)\n\t\t\t\ttime = 70;", "if (true)\n\t\t\t\ttime = 70;", "visit")
    bridge_mutant("hook-page-time-wrong", "time = 70;", "time = 71;", "visit")
    # the entry
    bridge_mutant("entry-no-page", "FRONTEND.SetPage(FEP_GOODIES, 70);", ";", "visit")
    bridge_mutant("entry-wrong-page", "FRONTEND.SetPage(FEP_GOODIES, 70);", "FRONTEND.SetPage(FEP_OTHER, 70);", "visit")
    bridge_mutant("entry-ignores-switch", "PortCeMuseumBegin(PortCeMenus())", "PortCeMuseumBegin(true)", "clock")
    bridge_mutant("entry-ignores-begin", "if (!PortCeMuseumBegin(PortCeMenus()))\n\t\treturn false;", ";", "visit")
    bridge_mutant("entry-reports-open-wrongly", "return PortCeMuseumActive();", "return true;", "visit")
    # the CE-menu switch the entry asks (the gates are existing code; these show that the refusals above have teeth)
    for name, before, after, scenario in (
            ("switch-ignores-layer", "PortCeLayer() &&", "true &&", "layer-forced-off"),
            ("switch-ignores-autoconfig", "!CLIPARAMS.mAutoConfigTest", "true", "autoconfig"),
            ("switch-ignores-demo", "!PLAYABLE_DEMO", "true", "demo")):
        bridge_mutant(name, before, after, scenario)
    bridge_mutant("layer-ignores-clock", '"BEA_PORT_CLOCK", ', "", "clock")
    bridge_mutant("layer-ignores-tape", '"BEA_PORT_TAPE", ', "", "tape")
    bridge_mutant("layer-ignores-probes", '"BEA_PORT_PROBES", ', "", "probes")
    bridge_mutant("layer-ignores-record", ', "BEA_PORT_RECORD"', "", "record")

    # 3. the patch's text itself: the title names the visit and the hooks are CE-only
    title_ok = 'FRONTEND.DrawTitleBar(PortCeMuseumActive() ? ToWCHAR("Goodies Gallery") : (FET.GetText(FEX_GOODIES2)), transition, dest);' in sources["FEPGoodies.cpp"]
    records.append({"name": "title-names-the-visit", "pass": title_ok})
    hooks_ce_only = all(only_in_ce_version(text, ("PortCeMuseum", "ce_museum_visual_state", "Goodies Gallery")) for text in sources.values())
    records.append({"name": "hooks-are-ce-only", "pass": hooks_ce_only})
    for row in records[-2:]:
        if not row["pass"]:
            stop.append(row["name"])
            print("museum control failed: " + row["name"], file=sys.stderr)

    receipt = {
        "status": "pass" if all(row["pass"] for row in records) else "fail",
        "cases": len(records),
        "sha256": {
            "controller": hashlib.sha256(controller.read_bytes()).hexdigest(),
            "controller_driver": hashlib.sha256(driver.read_bytes()).hexdigest(),
            "tests": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            "patch": hashlib.sha256((ROOT / "port/patches/0199-ce-museum-visitor.patch").read_bytes()).hexdigest(),
            "patched_sources": {name: hashlib.sha256(text.encode("latin-1")).hexdigest() for name, text in sources.items()},
        },
        "records": records,
        "game_assets_or_saves_used": False,
        "window_or_game_or_vm_run": False,
        "runtime_acceptance": False,
    }
    (out / "receipt.private.json").write_text(json.dumps(receipt, sort_keys=True, indent=2) + "\n")
    print(json.dumps({"status": receipt["status"], "cases": receipt["cases"], "receipt": str(out / "receipt.private.json")}))
    return 0 if receipt["status"] == "pass" else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        print("museum controls failed: " + type(error).__name__ + ": " + str(error)[:200], file=sys.stderr)
        raise SystemExit(1)
