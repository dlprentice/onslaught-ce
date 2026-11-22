#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Host tests and causal mutants for Grand Museum Concept Art Gallery & Early Designs Vault (ce_museum_art.h/.cpp)."""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GAME = ROOT / "port/game"
HEADER = GAME / "ce_museum_art.h"
CPP = GAME / "ce_museum_art.cpp"

DRIVER = r"""
#include "ce_museum_art.h"
#include "ce_museum_exhibition.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace CeMuseumArt;
namespace Grand = CeMuseumExhibition::Grand;

static void Need(bool ok, const char *why) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", why);
        std::exit(2);
    }
}

static bool Near(float a, float b, float eps = 1e-3f) {
    return std::fabs(a - b) <= eps;
}

static bool AsciiOnly(const char *s) {
    if (!s || !*s) return false;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(s); *p; ++p)
        if (*p < 0x20u || *p > 0x7eu) return false;
    return true;
}

int main() {
    const Grand::RoomGraph graph = Grand::MakeRoomGraph();
    Need(kManifestCount == 20, "20 curated Goodies canvases across both art wings");
    Need(CountWantedResident(CeMuseumTour::kWingConceptArt) == 10, "10 paintings in Concept Art Gallery");
    Need(CountWantedResident(CeMuseumTour::kWingEarlyDesigns) == 10, "10 plates in Early Designs Vault");
    Need(CountWantedResident(CeMuseumTour::kWingCourtyard) == 0, "no canvases resident in Courtyard");
    Need(CountWantedResident(CeMuseumTour::kWingHangar) == 0, "no canvases resident in Hangar");

    // Wing-bounded VRAM cap: every wing stays under 12 MiB worst-case RGBA.
    for (int w = 0; w < int(CeMuseumTour::kWingCount); ++w) {
        const CeMuseumTour::Wing wing = static_cast<CeMuseumTour::Wing>(w);
        Need(CountWantedResident(wing) <= unsigned(kMaxResidentCanvases), "canvas count <= wing cap");
        Need(EstimateWingBytes(wing) <= 12u * 1024u * 1024u, "worst-case VRAM <= 12 MiB per wing");
    }

    // Every canvas has valid ASCII copy, mounts inside its room, and has walkable viewing space in front.
    for (unsigned i = 0; i < kManifestCount; ++i) {
        const CanvasSpec &sp = kManifest[i];
        Need(sp.retailPath && std::strncmp(sp.retailPath, "goodies\\", 8) == 0, "retail goodies key");
        Need(AsciiOnly(sp.title) && AsciiOnly(sp.category) && AsciiOnly(sp.caption), "ASCII curator text");
        Need(sp.maxWidth > 1.0f && sp.maxHeight > 1.0f && sp.centreZ > 1.5f, "sensible frame bounds");
        Need(Grand::RoomAt(graph, CeMuseumExhibition::Vec2{sp.localX, sp.localY}) == static_cast<Grand::RoomId>(sp.wing),
             "canvas sits inside its declared wing");
        const float vx = sp.localX + sp.normalX * 3.0f;
        const float vy = sp.localY + sp.normalY * 3.0f;
        Need(Grand::WalkAt(graph, CeMuseumExhibition::Vec2{vx, vy}) == CeMuseumExhibition::kWalkOpen,
             "viewing spot 3 m in front of every canvas is walkable");
        Need(FocusCanvas(vx, vy, -sp.normalX, -sp.normalY, sp.wing) == int(i),
             "standing in front and looking at canvas focuses it");
        Need(FocusCanvas(vx, vy, -sp.normalX, -sp.normalY, CeMuseumTour::kWingCourtyard) == -1,
             "wrong wing clears canvas focus");
        Need(FocusCanvas(vx, vy, sp.normalX, sp.normalY, sp.wing) == -1,
             "looking away clears canvas focus");
        // Behind the wall never triggers focus.
        const float bx = sp.localX - sp.normalX * 2.0f;
        const float by = sp.localY - sp.normalY * 2.0f;
        Need(FocusCanvas(bx, by, sp.normalX, sp.normalY, sp.wing) == -1,
             "behind wall cannot focus canvas");
    }

    // BoundedUpload: <= 512 px longest edge with aspect ratio preserved.
    const UploadSize uSmall = BoundedUpload(256, 192);
    Need(uSmall.width == 256 && uSmall.height == 192, "small texture kept unchanged");
    const UploadSize uLand = BoundedUpload(1024, 512);
    Need(uLand.width == 512 && uLand.height == 256, "landscape capped to 512 preserving 2:1");
    const UploadSize uPort = BoundedUpload(600, 1200);
    Need(uPort.width == 256 && uPort.height == 512, "portrait capped to 512 preserving 1:2");
    const UploadSize uZero = BoundedUpload(0, 512);
    Need(uZero.width == 1 && uZero.height == 1, "zero input safely clamped to 1x1");

    // FitFrame: preserves source aspect ratio within frame opening.
    const QuadExtent qWide = FitFrame(1600, 900, 3.0f, 2.2f);
    Need(Near(qWide.halfW, 1.5f) && Near(qWide.halfH, 1.5f * 9.0f / 16.0f), "wide art fits frame width");
    const QuadExtent qTall = FitFrame(600, 1200, 3.0f, 2.2f);
    Need(Near(qTall.halfH, 1.1f) && Near(qTall.halfW, 1.1f * 0.5f), "tall art fits frame height");

    std::printf("ce_museum_art_tests: all checks passed\n");
    return 0;
}
"""

MUTANTS = [
    ("upload-uncapped", "if (longest <= maxEdge) return UploadSize{srcW, srcH};", "return UploadSize{srcW, srcH};"),
    ("fit-stretches-square", "if (aspect > boxAspect) h = maxWidth / aspect;\n    else w = maxHeight * aspect;",
     "(void)aspect; (void)boxAspect;"),
    ("focus-from-behind", "if ((-dx * sp.normalX - dy * sp.normalY) <= 0.1f) continue;", "if (false) continue;"),
    ("focus-wrong-wing", "if (!ShouldBeResident(i, currentWing)) continue;", "if (false) continue;"),
    ("vram-all-resident", "return kManifest[index].wing == currentWing;", "return true;"),
]


def run(cmd, expect=0):
    res = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    return res.returncode == expect, res.stdout + res.stderr


def main():
    failures = []

    def check(name, ok, detail=""):
        print(("ok   " if ok else "FAIL ") + name + ("" if ok else ("\n" + detail if detail else "")))
        if not ok:
            failures.append(name)

    out = ROOT / "local-data" / "museum-art-tests"
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=out) as tmp:
        d = Path(tmp)
        (d / "driver.cpp").write_text(DRIVER)
        flags = ["c++", "-std=c++14", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                 "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        ok, detail = run(flags + ["-I" + str(GAME), str(d / "driver.cpp"), "-o", str(d / "driver")])
        check("art driver builds with ASan/UBSan", ok, detail)
        if ok:
            ok, detail = run([str(d / "driver")])
            check("concept art manifest, 512px bounds, aspect fit and focus hold", ok, detail)

        src = HEADER.read_text()
        for label, old, new in MUTANTS:
            if src.count(old) != 1:
                check("mutant anchor " + label, False, "not found once: " + old)
                continue
            m = d / label
            m.mkdir()
            (m / HEADER.name).write_text(src.replace(old, new, 1))
            built, detail = run(["c++", "-std=c++14", "-O1", "-I" + str(m), "-I" + str(GAME),
                                 str(d / "driver.cpp"), "-o", str(m / "driver")])
            if not built:
                check("mutant " + label + " caught", False, detail)
                continue
            caught, _ = run([str(m / "driver")], 2)
            check("mutant " + label + " caught", caught)

    cpp = CPP.read_text()
    check("runtime opens asset catalog textures lazily",
          "PortCeAssetCatalogOpenKinds" in cpp and "CE_ASSET_TEXTURE" in cpp)
    check("runtime uploads bounded <=512px textures into D3DPOOL_MANAGED",
          "BoundedUpload" in cpp and "D3DPOOL_MANAGED" in cpp and "D3DXCreateTextureFromFileInMemoryEx" in cpp)
    check("runtime degrades missing paintings cleanly without crashing",
          "gArt.slots[i].missing = true;" in cpp and "archival plate unavailable" in cpp)
    check("runtime releases textures and catalog on shutdown",
          "void RuntimeShutdown()" in cpp and "ReleaseAllTextures();" in cpp and "PortCeAssetCatalogClose" in cpp)
    check("no forbidden words in curator text",
          not re.search(r'"[^"]*(?:prototype|experimental|carrier|ce\.ini)[^"]*"', HEADER.read_text(), re.I))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
