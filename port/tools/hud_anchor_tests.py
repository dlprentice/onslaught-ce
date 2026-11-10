#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""HUD anchoring on wide screens: the production gate (port/platform/common/hud_scale.cpp), the production viewpoint test
(port/game/ce_hud_anchor.cpp, against a fake game and engine) and the statements of widescreen.cpp that apply or withhold
patch 0043's inset, executed together (hud_anchor_driver.cpp) with ce.ini saying nothing, hud_anchor=1 and hud_anchor=0:
- no inset in ordinary single-player play, the 4:3 columns' inset otherwise (multiplayer, two viewpoints, a fenced run);
- a fenced run (a replay, a recording, probes, the virtual clock, BEA_PORT_HUD_SCALE=0, no CE layer, no widescreen modes)
  never even reads ce.ini; the answer is read once, and again only after CeHudSettingsReload (the CE Settings page's change
  applies from the next level);
- the corners do not depend on ce.ini's hud_scale (the original size keeps them): the size and the corners are separate answers;
- the developer log says each change once, with the HUD_OFFSET_X the pass really gets, and nothing when it is off.
Static: the viewpoint test is the one the HUD scale's scopes use (patch 0089), the objective panel's six explicit inset terms
(patch 0043) still call PortHudInset, and PortHudBegin is what decides them."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "port"
sys.path.insert(0, str(PORT))
import build  # noqa: E402


def anchor_text(network):
    """ce_hud_anchor.cpp as a default public build compiles it (the CE_NETWORK_OFF conditionals resolved: main's text) or as a build with the network does."""
    return build.network_view((PORT / "game/ce_hud_anchor.cpp").read_text(), off=not network)


FAKE_GAME = """#pragma once
struct FakeGame { bool multiplayer = false; bool IsMultiplayer() { return multiplayer; } };
extern FakeGame GAME;
"""
FAKE_ENGINE = """#pragma once
struct FakeEngine { int viewpoints = 1; int GetNumViewpoints() { return viewpoints; } };
extern FakeEngine ENGINE;
"""

CORNERS = "onslaught-ce: widescreen: the HUD's corner groups sit in the screen's corners"
COLUMNS = "onslaught-ce: widescreen: the HUD's corner groups sit in the 4:3 columns"


def fail(message):
    raise SystemExit("hud_anchor_tests: " + message)


def cut_inset_block(text):
    start = text.index("static bool gHudAnchored;")
    end_marker = "float PortHudOriginal() { return gHudOriginal; }"
    end = text.index(end_marker, start) + len(end_marker)
    return text[start:end] + "\n"


def static_checks():
    wide = (PORT / "platform/common/widescreen.cpp").read_text()
    block = cut_inset_block(wide)
    if block.count("PortHudAnchored()") != 1 or "!gHudAnchored" not in block:
        fail("PortHudBegin must ask PortHudAnchored once and PortHudInset must follow its answer")
    if block.index("gHudAnchored = PortHudAnchored() != 0;") > block.index("float shifted = offset + PortHudInset(viewportWidth);"):
        fail("PortHudBegin decides before it adds the inset")
    if "return shifted;" not in block:
        fail("PortHudBegin returns the offset with the inset it logged")
    anchor = anchor_text(False)
    eligible = "!GAME.IsMultiplayer() && ENGINE.GetNumViewpoints() == 1"
    if "CeHudAnchorEnabled() && " + eligible not in anchor:
        fail("the viewpoint test of ce_hud_anchor.cpp changed")
    coop = "(CE_LockstepRequested() || (!GAME.IsMultiplayer() && ENGINE.GetNumViewpoints() == 1))"
    if "CeHudAnchorEnabled() && " + coop not in anchor_text(True):
        fail("a network co-op game is not let through ce_hud_anchor.cpp's gate")
    after = (PORT / "patches/0157-network-hud-scale-coop.patch").read_text()
    if after.count(coop) != 5 or after.count(eligible) != 10 or after.count("\n+#ifdef CE_NETWORK_OFF\n") != 5:
        fail("patch 0157 no longer rewrites the HUD scale's five scopes as main's test in its CE_NETWORK_OFF branch and the co-op test in the other")
    scale = (PORT / "patches/0089-ce-hud-scale.patch").read_text()
    if scale.count(eligible) < 2:
        fail("the HUD scale's scopes no longer use the viewpoint test that PortHudAnchored mirrors")
    inset = (PORT / "patches/0043-widescreen-hud-inset.patch").read_text()
    if inset.count("PortHudInset(float(PLATFORM.GetWindowWidth()))") != 6:
        fail("the objective panel's six explicit inset terms changed (they follow PortHudBegin's decision)")
    if inset.count("PortHudBegin(portHudOffset, float(GetViewportForViewpoint(0)->Width))") != 2:
        fail("patch 0043's two PortHudBegin calls changed")
    return block


# One-line weakenings; each must make the executed driver fail on its own, in at least one of its three ini modes (the static checks above are not consulted): (name, file, old, new)
MUTANTS = (
    ("multiplayer not tested", "port/game/ce_hud_anchor.cpp", " && !GAME.IsMultiplayer()", ""),
    ("viewpoints not tested", "port/game/ce_hud_anchor.cpp", " && ENGINE.GetNumViewpoints() == 1", ""),
    ("the ini's default is off", "port/platform/common/hud_scale.cpp", 'CE_SettingGetBool("hud_anchor", 1)', 'CE_SettingGetBool("hud_anchor", 0)'),
    ("the ini is ignored", "port/platform/common/hud_scale.cpp", "return sAnchorIni == 1;", "return true;"),
    ("the HUD scale's gate is skipped", "port/platform/common/hud_scale.cpp", "if (!Enabled())\n\t\treturn false;\n\tif (sAnchorIni < 0)", "if (false)\n\t\treturn false;\n\tif (sAnchorIni < 0)"),
    ("the ini is read at every call", "port/platform/common/hud_scale.cpp", "if (sAnchorIni < 0)\n\t\tsAnchorIni =", "if (true)\n\t\tsAnchorIni ="),
    ("a reload leaves the anchor answer", "port/platform/common/hud_scale.cpp", "\tsScaleIni = -1;\n\tsAnchorIni = -1;\n}", "\tsScaleIni = -1;\n}"),
    ("the original size takes the corners with it", "port/platform/common/hud_scale.cpp", "if (!Enabled())\n\t\treturn false;\n\tif (sAnchorIni < 0)", "if (!Enabled() || !ScaleWanted())\n\t\treturn false;\n\tif (sAnchorIni < 0)"),
    ("never anchored", "BLOCK", "gHudAnchored = PortHudAnchored() != 0;", "gHudAnchored = false;"),
    ("always anchored", "BLOCK", "gHudAnchored = PortHudAnchored() != 0;", "gHudAnchored = true;"),
    ("the inset ignores the answer", "BLOCK", "return gActive && !gHudAnchored ?", "return gActive ?"),
    ("the offset loses the shift", "BLOCK", "\treturn shifted;", "\treturn offset;"),
)


def expected_log(mode):
    """The developer log of the driver's fixed sequence: six shapes, each pinned (HUD_OFFSET_X 0) then multiplayer (the shape's
    inset), then the offset 5 call pinned again (5); the rest is silent. With the ini saying 0 the first answer is the only one."""
    if mode == "off":
        return [COLUMNS + " (HUD_OFFSET_X 240)"]
    insets = (240, 320, 480, 560, 160, 0)
    return [line for inset in insets for line in (CORNERS + " (HUD_OFFSET_X 0)", COLUMNS + " (HUD_OFFSET_X %d)" % inset)] + [CORNERS + " (HUD_OFFSET_X 5)"]


def driver_passes(binary):
    for mode in ("absent", "on", "off"):
        ran = subprocess.run([str(binary), mode], capture_output=True, text=True, timeout=10)
        if ran.returncode or "PASS" not in ran.stdout:
            return mode + ": " + ran.stdout + ran.stderr
        if ran.stderr.splitlines() != expected_log(mode):
            return mode + ": the developer log said " + repr(ran.stderr.splitlines())
    return None


def mutant_checks(out, fakes, block):
    for number, (name, source, old, new) in enumerate(MUTANTS):
        folder = out / ("mutant-%d" % number)
        folder.mkdir()
        sources = {"port/game/ce_hud_anchor.cpp": anchor_text(False),
                   "port/platform/common/hud_scale.cpp": (PORT / "platform/common/hud_scale.cpp").read_text()}
        inc = block
        if source == "BLOCK":
            if inc.count(old) != 1:
                fail("mutant %r: its line is not in the block exactly once" % name)
            inc = inc.replace(old, new)
        else:
            if sources[source].count(old) != 1:
                fail("mutant %r: its line is not in %s exactly once" % (name, source))
            sources[source] = sources[source].replace(old, new)
        (folder / "widescreen_hud.inc").write_text(inc)
        (folder / "ce_hud_anchor.cpp").write_text(sources["port/game/ce_hud_anchor.cpp"])
        (folder / "hud_scale.cpp").write_text(sources["port/platform/common/hud_scale.cpp"])
        binary = folder / "driver"
        subprocess.run(["g++", "-std=c++17", "-O1", "-w", "-I" + str(folder), "-I" + str(fakes), "-I" + str(PORT / "include"),
                        str(PORT / "tools/hud_anchor_driver.cpp"), str(folder / "ce_hud_anchor.cpp"), str(folder / "hud_scale.cpp"), "-o", str(binary)],
                       check=True, timeout=60)
        if driver_passes(binary) is None:
            fail("mutant %r was not caught by the executed driver" % name)


def main():
    block = static_checks()
    parent = ROOT / "local-data/port/hud-anchor-tests"
    parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="case-", dir=parent) as directory:
        out = Path(directory)
        fakes = out / "fakes"
        fakes.mkdir()
        (fakes / "common.h").write_text("#pragma once\n")
        (fakes / "game.h").write_text(FAKE_GAME)
        (fakes / "DXEngine.h").write_text(FAKE_ENGINE)
        (out / "widescreen_hud.inc").write_text(block)
        (out / "ce_hud_anchor_default.cpp").write_text(anchor_text(False))		# main's text: the build with the network has its own driver below
        (out / "ce_hud_anchor_network.cpp").write_text(anchor_text(True))
        binary = out / "hud-anchor-test"
        command = ["g++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-I" + str(out), "-I" + str(fakes),
                   "-I" + str(PORT / "include"), str(PORT / "tools/hud_anchor_driver.cpp"),
                   str(out / "ce_hud_anchor_default.cpp"), str(PORT / "platform/common/hud_scale.cpp"), "-o", str(binary)]
        subprocess.run(command, check=True, timeout=60)
        failure = driver_passes(binary)
        if failure:
            fail(failure)
        mutant_checks(out, fakes, block)
        coop_bin = out / "hud-anchor-coop-test"
        subprocess.run(["g++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-I" + str(fakes), "-I" + str(PORT / "include"),
                        str(PORT / "tools/hud_anchor_coop_driver.cpp"), str(out / "ce_hud_anchor_network.cpp"),
                        str(PORT / "platform/common/hud_scale.cpp"), "-o", str(coop_bin)], check=True, timeout=60)
        ran = subprocess.run([str(coop_bin)], capture_output=True, text=True, timeout=10)
        if ran.returncode or "PASS" not in ran.stdout:
            fail("the co-op gate: " + ran.stdout + ran.stderr)
        # the mutant of the network flavour: a co-op game not let through must fail that driver
        gate = "(CE_LockstepRequested() || (!GAME.IsMultiplayer() && ENGINE.GetNumViewpoints() == 1))"
        weak = anchor_text(True).replace(gate, "(!GAME.IsMultiplayer() && ENGINE.GetNumViewpoints() == 1)")
        (out / "ce_hud_anchor_weak.cpp").write_text(weak)
        subprocess.run(["g++", "-std=c++17", "-O2", "-w", "-I" + str(fakes), "-I" + str(PORT / "include"), str(PORT / "tools/hud_anchor_coop_driver.cpp"),
                        str(out / "ce_hud_anchor_weak.cpp"), str(PORT / "platform/common/hud_scale.cpp"), "-o", str(out / "coop-weak")], check=True, timeout=60)
        if weak == anchor_text(True) or subprocess.run([str(out / "coop-weak")], capture_output=True, text=True, timeout=10).returncode == 0:
            fail("a gate that leaves a co-op game out was not caught by the co-op driver")
    print("HUD anchoring: gate, viewpoint test and inset decision executed with ce.ini silent, on and off; %d mutants caught by the executed driver; "
          "the co-op gate of the build with the network executed: PASS" % len(MUTANTS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
