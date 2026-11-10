#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the actual patched HUD sprite/font paths against authored boundaries."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port"))
import build  # noqa: E402
NAMES = ("Hud.cpp", "DXSpriteRenderer.cpp", "Font.cpp", "DXBattleLine.cpp", "DXCompass.cpp", "MessageLog.cpp", "LevelBriefingLog.cpp")

# One-line weakenings of the HUD size's gate (port/platform/common/hud_scale.cpp), each of which the executed driver must catch on
# its own: (name, old, new).
MUTANTS = (
    ("ce.ini's hud_scale is ignored", "return sScaleIni == 1;", "return true;"),
    ("the default is the original size", 'CE_SettingGetBool("hud_scale", 1)', 'CE_SettingGetBool("hud_scale", 0)'),
    ("the environment variable no longer wins", "if (env && *env)\n\t\treturn *env != '0';", "if (false)\n\t\treturn *env != '0';"),
    ("an empty environment variable is a setting", "if (env && *env)\n\t\treturn *env != '0';", "if (env)\n\t\treturn *env != '0';"),
    ("ce.ini is read at every scope", "if (sScaleIni < 0)\n\t\tsScaleIni =", "if (true)\n\t\tsScaleIni ="),
    ("a reload leaves the size answer", "\tsScaleIni = -1;\n\tsAnchorIni = -1;\n}", "\tsAnchorIni = -1;\n}"),
    ("ce.ini is read before the fences are asked", "!Enabled() || !ScaleWanted())", "!ScaleWanted() || !Enabled())"),
    ("the original size takes the corners with it",
     "if (!Enabled())\n\t\treturn false;\n\tif (sAnchorIni < 0)", "if (!Enabled() || !ScaleWanted())\n\t\treturn false;\n\tif (sAnchorIni < 0)"),
    ("non-finite metrics reach the transform", "if (!isfinite(width) || !isfinite(height) ||", "if (false || !isfinite(height) ||"),
    ("a non-finite anchor reaches the transform", "!isfinite(anchorX) || !isfinite(anchorY)) return;", "false) return;"),
    ("a non-finite anchor update is taken", "if (sActive && isfinite(x) && isfinite(y))", "if (sActive)"),
)


def function(source, marker):
    start = source.index(marker)
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
        end += 1
    return source[start:end]


PROJECTION = re.compile(r"RENDERINFO\.Project\(\(D3DXVECTOR3 \*\)&screen, \(D3DXVECTOR3 \*\)&\w+, mViewport\);")
MARKER_SCOPE = "CeHudScope scope(portHudEligible, mViewportWidth, mViewportHeight, 0.f, 0.f);"
ANCHOR = "CeHudSetAnchor(screen.X, screen.Y);"
BACKDROPS = (("MessageLog.cpp", "void\tCMessageLog::Render("), ("LevelBriefingLog.cpp", "void\tCLevelBriefingLog::Render("))
# The HUD's test in each build's text: one viewpoint and not multiplayer (patch 0089), or with the network compiled in also a co-op
# game of the network release (patch 0157). The world markers' scope takes it as portHudEligible and the speaker marker's scope
# spells it out, once in each arm of patch 0190's CE_NETWORK_OFF conditional.
VIEWPOINT_TEST = "!GAME.IsMultiplayer() && ENGINE.GetNumViewpoints() == 1"
COOP_TEST = "(CE_LockstepRequested() || (" + VIEWPOINT_TEST + "))"
SPEAKER = "CeHudScope scope(%s, mViewportWidth, mViewportHeight, screen.X, screen.Y);\t// Port (patch 0190)"
# The text each build compiles (build.network_view): the default public build's, CE_NETWORK_OFF defined (build.resolve_network_off
# gives the compiler that text), and the network build's; with the HUD's test of each.
VIEWS = (("the default build", True, VIEWPOINT_TEST), ("the network build", False, COOP_TEST))


def arguments(text):
    out, depth, cur = [], 0, ""
    for c in text:
        depth += c in "(["
        depth -= c in ")]"
        if c == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += c
    out.append(cur.strip())
    return out


def marker_problems(hud, test):
    """Patch 0190 in the HUD's text as one build compiles it (test: that build's HUD test, VIEWPOINT_TEST or COOP_TEST): the world
    markers are drawn inside a scope that takes the other scopes' test after it is decided, each marker sets the anchor to its own
    projected point before anything of it is drawn, and the speaker marker scales about its point under the same test.
    Returns the problems found (none: [])."""
    problems = []
    overlay = function(hud, "void\tCHud::RenderOverlayForViewpoint(")
    eligible = re.search(r"\bconst int portHudEligible = ([^;\n]*);", overlay)
    if not eligible or eligible.group(1) != test:
        problems.append("the HUD's test (portHudEligible) is not the one this build's markers must take")
    call = overlay.find("RenderWorldTargets();")
    scope = overlay.rfind(MARKER_SCOPE, 0, call)
    if not (0 <= overlay.rfind("const int portHudEligible", 0, max(scope, 0)) < scope < call
            < overlay.index("RenderTargetIndicator();") < overlay.index("RenderTargetMarkers();") < overlay.index("RenderWeaponPanel();")):
        problems.append("the world markers' scope or a screen-fixed HUD scope moved")
    if scope >= 0 and ("}" in overlay[scope:call] or not overlay[call + len("RenderWorldTargets();"):].lstrip().startswith("}")):
        problems.append("the world markers are not drawn inside their scope")
    world = function(hud, "void\tCHud::RenderWorldTargets(")
    ends = [m.end() for m in PROJECTION.finditer(world)]
    if len(ends) != 4 or world.count(ANCHOR) != 4 or any(not world[end:].lstrip().startswith(ANCHOR) for end in ends):
        problems.append("a world marker's projection is not followed by its anchor")
    draws = [m.start() for m in re.finditer(r"CSPRITERENDERER::DrawSprite\(", world)]
    if not draws or any(not any(end < draw for end in ends) for draw in draws):
        problems.append("a world marker is drawn before any anchor is set")
    targets = function(hud, "void\tCHud::RenderTargetMarkers(")
    ends = [m.end() for m in PROJECTION.finditer(targets)]
    scope = re.match(r"\s*CeHudScope scope\(([^;]*)\);", targets[ends[2]:]) if len(ends) == 3 else None
    args = arguments(scope.group(1)) if scope else []
    if args[1:] != ["mViewportWidth", "mViewportHeight", "screen.X", "screen.Y"]:
        problems.append("the speaker marker does not scale about its projected point")
    elif args[0] != test:
        problems.append("the speaker marker's scope does not take the HUD's test (split screen would scale, or co-op would not)")
    return problems


def backdrop_problems(text, marker, original):
    """Patch 0190 in a log page: on the wide paths its backdrop covers the whole window (the game's statement with the 4:3 box's offset
    taken off and its size stretched to the window, as the pause menu's, patch 0042), and the game's own statement, unchanged, in the
    other arm. original is the page's text in src/."""
    body = function(text, marker)
    arm = re.search(r"\n(\t+)if \(PortWideActive\(\)\)[^\n]*\n\1\tCSPRITERENDERER::DrawSprite\(([^\n]*)\);\n\1else\n\1(CSPRITERENDERER::DrawSprite\(([^\n]*)\);)\n", body)
    if not arm:
        return ["the backdrop has no wide arm with the game's statement in the other"]
    wide, game = arguments(arm.group(2)), arguments(arm.group(4))
    if len(game) != 15 or "mBlank" not in game or "SA_TOP_LEFT" not in game:
        return ["the other arm is not the game's backdrop"]
    expected = list(game)
    expected[0] = game[0] + " - PortUiLeft() / PortUiScale()"
    expected[1] = game[1] + " - PortUiTop() / PortUiScale()"
    expected[9] = game[9] + " * (float)LT.GetWindowWidth() / (640.f * PortUiScale())"
    expected[10] = game[10] + " * (float)LT.GetWindowHeight() / (480.f * PortUiScale())"
    problems = []
    if wide != expected:
        problems.append("the wide arm is not the game's backdrop stretched to the window")
    if original.count(arm.group(3)) != 1:
        problems.append("the other arm is not the source's own statement")
    return problems


def corner(line):
    """The speaker scope's line anchored at the screen's corner instead of its projected point."""
    return line.replace("screen.X, screen.Y)", "0.f, 0.f)")


# Weakenings of patch 0190's text, in the patched files before either build's view is taken, that the structure checks above must
# catch in one build's text or the other's: (name, file, old, new); old is replaced once (the speaker's arms each occur once).
DEFAULT_SPEAKER, NETWORK_SPEAKER = SPEAKER % VIEWPOINT_TEST, SPEAKER % COOP_TEST
STRUCTURE_MUTANTS = (
    ("a marker loop loses its anchor", "Hud.cpp", "\t" + ANCHOR, ""),
    ("the markers are drawn after their scope ends", "Hud.cpp", "\t\tRenderWorldTargets();\n\t}\n", "\t}\n\tRenderWorldTargets();\n"),
    ("the markers' scope ignores the viewpoint test (split screen would scale)", "Hud.cpp", MARKER_SCOPE, MARKER_SCOPE.replace("portHudEligible", "1")),
    ("the default build's speaker scope ignores the viewpoint test (split screen would scale)", "Hud.cpp", DEFAULT_SPEAKER, SPEAKER % "1"),
    ("the default build's speaker marker loses its scope (the game's pixels)", "Hud.cpp", DEFAULT_SPEAKER, ""),
    ("the default build's speaker marker scales about the screen's corner", "Hud.cpp", DEFAULT_SPEAKER, corner(DEFAULT_SPEAKER)),
    ("the network build's speaker scope ignores the co-op and viewpoint test", "Hud.cpp", NETWORK_SPEAKER, SPEAKER % "1"),
    ("the network build's speaker scope drops the co-op test (a co-op game's marker keeps the game's pixels)", "Hud.cpp",
     NETWORK_SPEAKER, DEFAULT_SPEAKER),
    ("the network build's speaker marker loses its scope (the game's pixels)", "Hud.cpp", NETWORK_SPEAKER, ""),
    ("the network build's speaker marker scales about the screen's corner", "Hud.cpp", NETWORK_SPEAKER, corner(NETWORK_SPEAKER)),
    ("the message log keeps the 4:3 backdrop", "MessageLog.cpp", "PortUiLeft() / PortUiScale()", "0.f"),
    ("the briefing stretches to the box only", "LevelBriefingLog.cpp", "(float)LT.GetWindowWidth() / (640.f * PortUiScale())", "1.f"),
)


def structure_problems(texts, originals):
    """Patch 0190 in the text each build compiles. texts: the patched files with their CE_NETWORK_OFF conditionals unresolved;
    originals: the log pages' text in src/. Returns the problems found, each named with its build (none: [])."""
    problems = []
    for name, off, test in VIEWS:
        view = {file: build.network_view(text, off=off) for file, text in texts.items()}
        found = marker_problems(view["Hud.cpp"], test)
        for file, marker in BACKDROPS:
            found += backdrop_problems(view[file], marker, originals[file])
        problems += [name + ": " + problem for problem in found]
    return problems


def main():
    parent = ROOT / "local-data/port/hud-scale-tests"
    parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="case-", dir=parent) as directory:
        out = Path(directory)
        mirror = out / "src"
        mirror.mkdir()
        build.SYSTEM_NAMES.update(build.system_header_names())
        targets = [out / path.relative_to(ROOT) for path in build.tracked_sources()]
        index = build.CaseIndex(targets)
        for name in NAMES:
            source = (ROOT / "src" / name).read_bytes().decode("latin-1")
            normalized = build.normalise(source, mirror, index, [mirror, mirror / "MissionScript"])
            (mirror / name).write_bytes(normalized.encode("latin-1"))
        for patch in sorted((ROOT / "port/patches").glob("*.patch")):
            for section in re.split(r"(?=^--- a/)", patch.read_text(), flags=re.M):
                match = re.match(r"--- a/([^\n]+)\n\+\+\+ b/([^\n]+)\n", section)
                if not match or match.group(1) not in NAMES:
                    continue
                applied = subprocess.run(["patch", "--batch", "--forward", "--fuzz=0", "-p1", "-d", str(mirror)],
                                         input=section, text=True, capture_output=True, timeout=10)
                if applied.returncode or "fuzz" in applied.stdout:
                    raise RuntimeError(patch.name + ": " + applied.stdout + applied.stderr)
        # patch 0190's files as patched, both arms of each CE_NETWORK_OFF conditional still in them (structure_problems takes each view)
        texts = {name: (mirror / name).read_bytes().decode("latin-1") for name in ("Hud.cpp", "MessageLog.cpp", "LevelBriefingLog.cpp")}
        for name in NAMES:      # the text a build with the network sees: its CE_NETWORK_OFF conditionals resolved (build.network_view)
            (mirror / name).write_bytes(build.network_view((mirror / name).read_bytes().decode("latin-1")).encode("latin-1"))
        font = (mirror / "Font.cpp").read_text()
        methods = "\n".join(function(font, name) for name in
                            ("void\tCDXBitmapFont::DrawTextDynamic(", "void\tCDXBitmapFont::DrawTextWithShadow("))
        (out / "font_methods.inc").write_text(methods + "\n")
        sprite = (mirror / "DXSpriteRenderer.cpp").read_text()
        start = sprite.index("\t\tif (CeHudScaleActive())")
        end = sprite.index("\n\t\tif (!mBatch)", start)
        (out / "sprite_transform.inc").write_text(sprite[start:end] + "\n")
        hud = (mirror / "Hud.cpp").read_text()
        target = hud.index("\t\tvp.Y -= vp.Height / 2;")
        start = hud.index("\t\tif (CeHudScaleActive())", target)
        end = hud.index("\n\t\tvp.MinZ = 0;", start)
        (out / "target_viewport.inc").write_text(hud[start:end] + "\n")
        originals = {name: (ROOT / "src" / name).read_bytes().decode("latin-1") for name, _ in BACKDROPS}
        if texts["Hud.cpp"].count(DEFAULT_SPEAKER) != 1 or texts["Hud.cpp"].count(NETWORK_SPEAKER) != 1:
            raise RuntimeError("the speaker marker's scope is not in the patched Hud.cpp once in each arm")
        problems = structure_problems(texts, originals)
        if problems:
            raise RuntimeError("; ".join(problems))
        for name, file, old, new in STRUCTURE_MUTANTS:
            if texts[file].count(old) < 1:
                raise RuntimeError("structure mutant %r: its text is not in the patched %s" % (name, file))
            mutated = dict(texts)
            mutated[file] = texts[file].replace(old, new, 1)
            if not structure_problems(mutated, originals):
                raise RuntimeError("structure mutant %r was not caught" % name)
        targets = function(hud, "void\tCHud::RenderTargetMarkers(")
        if not ("float(PLATFORM.GetWindowWidth()) * 0.5f" in targets
                and targets.count("screen.X, screen.Y);") >= 3
                and targets.index("CeHudScope scope(") < targets.index("mCompass->Render(mBattleEngine);")):
            raise RuntimeError("compass or projected crosshair scope missing")
        battle = (mirror / "DXBattleLine.cpp").read_text()
        if battle.count("CeHudScalePoint(") != 2:
            raise RuntimeError("battleline vertex and point paths must both be transformed")
        compass = function((mirror / "DXCompass.cpp").read_text(), "void CDXCompass::Render(")
        # The network layer rewrites the condition to "(GAME.IsMultiplayer() && !CE_WorldActive())" (patch 0150): either form starts the block.
        start = re.search(r"\tif \(\(?GAME\.IsMultiplayer\(\)(?: && !CE_WorldActive\(\))?\)\)?", compass).start()
        end = compass.index("\n\tLT.SSS_NoCache(", start)
        (out / "compass_projection.inc").write_text(compass[start:end] + "\n")
        def compile_driver(gate, binary):
            command = ["g++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-I" + str(out),
                       "-I" + str(ROOT / "port/include"), str(ROOT / "port/tools/hud_scale_driver.cpp"),
                       str(gate), "-o", str(binary)]
            subprocess.run(command, check=True, timeout=30)

        binary = out / "hud-scale-test"
        compile_driver(ROOT / "port/platform/common/hud_scale.cpp", binary)
        subprocess.run([str(binary)], check=True, timeout=10)
        gate = (ROOT / "port/platform/common/hud_scale.cpp").read_text()
        for number, (name, old, new) in enumerate(MUTANTS):
            if gate.count(old) != 1:
                raise RuntimeError("mutant %r: its text is not in hud_scale.cpp exactly once" % name)
            mutated = out / ("hud_scale_mutant_%d.cpp" % number)
            mutated.write_text(gate.replace(old, new))
            program = out / ("hud-scale-mutant-%d" % number)
            compile_driver(mutated, program)
            if subprocess.run([str(program)], capture_output=True, timeout=10).returncode == 0:
                raise RuntimeError("mutant %r was not caught by the executed driver" % name)
    print("HUD size gate: ce.ini's hud_scale, the environment variable, read once, reload and the fences executed; "
          "%d mutants caught by the executed driver: PASS" % len(MUTANTS))
    print("Patch 0190 in the default build's text and the network build's: the world markers' scope and anchors, the speaker "
          "marker's scope (the viewpoint test; with the network, the co-op test) and the two log backdrops hold; "
          "%d structure mutants caught: PASS" % len(STRUCTURE_MUTANTS))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
