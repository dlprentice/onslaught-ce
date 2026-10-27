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
NAMES = ("Hud.cpp", "DXSpriteRenderer.cpp", "Font.cpp", "DXBattleLine.cpp", "DXCompass.cpp")


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
        overlay = function(hud, "void\tCHud::RenderOverlayForViewpoint(")
        if not (overlay.index("RenderWorldTargets();") < overlay.index("CeHudScope scope(")
                < overlay.index("RenderTargetMarkers();") < overlay.index("RenderWeaponPanel();")):
            raise RuntimeError("world-projected marker or screen-fixed HUD scope moved")
        targets = function(hud, "void\tCHud::RenderTargetMarkers(")
        if not ("float(PLATFORM.GetWindowWidth()) * 0.5f" in targets
                and targets.count("screen.X, screen.Y);") >= 2
                and targets.index("CeHudScope scope(") < targets.index("mCompass->Render(mBattleEngine);")):
            raise RuntimeError("compass or projected crosshair scope missing")
        battle = (mirror / "DXBattleLine.cpp").read_text()
        if battle.count("CeHudScalePoint(") != 2:
            raise RuntimeError("battleline vertex and point paths must both be transformed")
        compass = function((mirror / "DXCompass.cpp").read_text(), "void CDXCompass::Render(")
        start = compass.index("\tif (GAME.IsMultiplayer())")
        end = compass.index("\n\tLT.SSS_NoCache(", start)
        (out / "compass_projection.inc").write_text(compass[start:end] + "\n")
        binary = out / "hud-scale-test"
        command = ["g++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-I" + str(out),
                   "-I" + str(ROOT / "port/include"), str(ROOT / "port/tools/hud_scale_driver.cpp"),
                   str(ROOT / "port/platform/common/hud_scale.cpp"), "-o", str(binary)]
        subprocess.run(command, check=True, timeout=30)
        subprocess.run([str(binary)], check=True, timeout=10)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
