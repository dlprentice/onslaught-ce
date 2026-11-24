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
#include "ce_archive_pedestal.h"
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

static bool MountedFlushOnRealWall(const CanvasSpec &sp) {
    const float eps = 0.025f;
    const float halfW = sp.maxWidth * 0.5f + 0.155f;
    if (sp.wing == CeMuseumTour::kWingConceptArt) {
        // North wall y=27.68 (normal -Y), South wall y=10.32 (normal +Y), East wall x=39.68 (normal -X)
        if (Near(sp.localY, 27.68f, eps) && Near(sp.normalX, 0.0f) && Near(sp.normalY, -1.0f))
            return sp.localX - halfW >= 16.30f && sp.localX + halfW <= 39.70f;
        if (Near(sp.localY, 10.32f, eps) && Near(sp.normalX, 0.0f) && Near(sp.normalY, 1.0f))
            return sp.localX - halfW >= 16.30f && sp.localX + halfW <= 39.70f;
        if (Near(sp.localX, 39.68f, eps) && Near(sp.normalX, -1.0f) && Near(sp.normalY, 0.0f))
            return sp.localY - halfW >= 18.0f && sp.localY + halfW <= 27.60f;
        // 3 parallel N-S divider walls at x = 23, 28, 33 (+/-0.26 face), y [14.10..21.30]
        const float divX[3] = {23.0f, 28.0f, 33.0f};
        for (unsigned k = 0; k < 3u; ++k) {
            if (Near(sp.localX, divX[k] - 0.26f, eps) && Near(sp.normalX, -1.0f) && Near(sp.normalY, 0.0f))
                return sp.localY - halfW >= 14.10f && sp.localY + halfW <= 21.30f;
            if (Near(sp.localX, divX[k] + 0.26f, eps) && Near(sp.normalX, 1.0f) && Near(sp.normalY, 0.0f))
                return sp.localY - halfW >= 14.10f && sp.localY + halfW <= 21.30f;
        }
        return false;
    }
    if (sp.wing == CeMuseumTour::kWingEarlyDesigns) {
        if (Near(sp.localY, 27.68f, eps) && Near(sp.normalX, 0.0f) && Near(sp.normalY, -1.0f))
            return sp.localX - halfW >= -39.70f && sp.localX + halfW <= -16.30f;
        if (Near(sp.localY, 10.32f, eps) && Near(sp.normalX, 0.0f) && Near(sp.normalY, 1.0f))
            return sp.localX - halfW >= -39.70f && sp.localX + halfW <= -16.30f;
        if (Near(sp.localX, -39.68f, eps) && Near(sp.normalX, 1.0f) && Near(sp.normalY, 0.0f))
            return sp.localY - halfW >= 10.40f && sp.localY + halfW <= 27.60f;
        const float divX[3] = {-23.0f, -28.0f, -33.0f};
        for (unsigned k = 0; k < 3u; ++k) {
            if (Near(sp.localX, divX[k] - 0.26f, eps) && Near(sp.normalX, -1.0f) && Near(sp.normalY, 0.0f))
                return sp.localY - halfW >= 14.10f && sp.localY + halfW <= 21.30f;
            if (Near(sp.localX, divX[k] + 0.26f, eps) && Near(sp.normalX, 1.0f) && Near(sp.normalY, 0.0f))
                return sp.localY - halfW >= 14.10f && sp.localY + halfW <= 21.30f;
        }
        return false;
    }
    return false;
}

int main() {
    const Grand::RoomGraph graph = Grand::MakeRoomGraph();
    Need(kManifestCount == 149, "ALL 149/149 (100%) retail GT_IMAGE Goodies plates across both art wings");
    Need(kMaxTexEdge == 256u, "256px max texture edge cap for all 149 simultaneously resident canvases");
    Need(CountWantedResident(CeMuseumTour::kWingConceptArt) == 75, "75 paintings in Concept Art Gallery");
    Need(CountWantedResident(CeMuseumTour::kWingEarlyDesigns) == 74, "74 plates in Early Designs Vault");
    Need(CountWantedResident(CeMuseumTour::kWingCourtyard) == 0, "no canvases resident in Courtyard");
    Need(CountWantedResident(CeMuseumTour::kWingHangar) == 0, "no canvases resident in Hangar");
    Need(EstimateTotalBytes() <= 38u * 1024u * 1024u, "total VRAM for all 149 canvases <= 38 MiB");

    // 1. Exact 100% (149/149) retail kGoodieImage ID coverage verified against CeArchivePedestal!
    Need(CeArchivePedestal::CountGoodies(CeArchivePedestal::kGoodieImage) == 149u,
         "retail catalog defines 149 GT_IMAGE goodies");
    for (unsigned g = 0; g < 149u; ++g) {
        const int wantId = CeArchivePedestal::GoodieIdAt(CeArchivePedestal::kGoodieImage, g);
        Need(wantId >= 0, "valid retail image goodie ID");
        bool found = false;
        for (unsigned i = 0; i < kManifestCount; ++i) {
            if (kManifest[i].goodieId == wantId) { found = true; break; }
        }
        Need(found, "every single retail GT_IMAGE goodie ID (149/149) appears in museum art manifest");
    }

    // 2. Wing-bounded VRAM cap stays under 20 MiB per wing.
    for (int w = 0; w < int(CeMuseumTour::kWingCount); ++w) {
        const CeMuseumTour::Wing wing = static_cast<CeMuseumTour::Wing>(w);
        Need(CountWantedResident(wing) <= unsigned(kMaxResidentCanvases), "canvas count <= cap");
        Need(EstimateWingBytes(wing) <= 20u * 1024u * 1024u, "worst-case VRAM <= 20 MiB per wing");
    }

    // 3. Every canvas has unique goodieId, valid retailPath, ASCII copy, flush wall mount,
    // zero 3D frame overlap with any other plate, walkable 2.0m viewing spot, and accurate 2D/3D focus!
    for (unsigned i = 0; i < kManifestCount; ++i) {
        const CanvasSpec &sp = kManifest[i];
        Need(CeArchivePedestal::ClassifyGoodie(sp.goodieId) == CeArchivePedestal::kGoodieImage,
             "manifest entry is a genuine retail GT_IMAGE goodie");
        Need(sp.retailPath && std::strncmp(sp.retailPath, "goodies\\", 8) == 0, "retail goodies key");
        Need(AsciiOnly(sp.title) && AsciiOnly(sp.category) && AsciiOnly(sp.caption), "ASCII curator text");
        Need(std::strlen(sp.title) <= 56u && std::strlen(sp.category) <= 44u && std::strlen(sp.caption) <= 95u,
             "art plaque copy fits CeMuseumLabel kChars=96 buffers without truncation");
        Need(sp.maxWidth > 1.0f && sp.maxHeight > 1.0f && sp.centreZ > 1.5f, "sensible frame bounds");
        Need(Grand::RoomAt(graph, CeMuseumExhibition::Vec2{sp.localX, sp.localY}) == static_cast<Grand::RoomId>(sp.wing),
             "canvas sits inside its declared wing");
        Need(MountedFlushOnRealWall(sp), "canvas is mounted flush against a real perimeter or divider wall");

        // 3D AABB non-overlap across ALL 149 outer brass frames (+0.155m border + curator plaque below)!
        const float rxA = -sp.normalY, ryA = sp.normalX;
        const float hwA = sp.maxWidth * 0.5f + 0.155f;
        const float hhA = sp.maxHeight * 0.5f + 0.155f;
        const float ax0 = sp.localX - std::fabs(rxA) * hwA - std::fabs(sp.normalX) * 0.08f;
        const float ax1 = sp.localX + std::fabs(rxA) * hwA + std::fabs(sp.normalX) * 0.08f;
        const float ay0 = sp.localY - std::fabs(ryA) * hwA - std::fabs(sp.normalY) * 0.08f;
        const float ay1 = sp.localY + std::fabs(ryA) * hwA + std::fabs(sp.normalY) * 0.08f;
        const float az0 = sp.centreZ - hhA - 0.165f; // includes brass curator nameplate below frame
        const float az1 = sp.centreZ + hhA;
        Need(az0 > 0.40f && az1 < 5.08f, "frame and nameplate stay cleanly between skirting and picture rail");

        for (unsigned j = i + 1u; j < kManifestCount; ++j) {
            const CanvasSpec &ot = kManifest[j];
            Need(sp.goodieId != ot.goodieId, "unique goodieId per canvas");
            if (sp.wing != ot.wing) continue;
            const float rxB = -ot.normalY, ryB = ot.normalX;
            const float hwB = ot.maxWidth * 0.5f + 0.155f;
            const float hhB = ot.maxHeight * 0.5f + 0.155f;
            const float bx0 = ot.localX - std::fabs(rxB) * hwB - std::fabs(ot.normalX) * 0.08f;
            const float bx1 = ot.localX + std::fabs(rxB) * hwB + std::fabs(ot.normalX) * 0.08f;
            const float by0 = ot.localY - std::fabs(ryB) * hwB - std::fabs(ot.normalY) * 0.08f;
            const float by1 = ot.localY + std::fabs(ryB) * hwB + std::fabs(ot.normalY) * 0.08f;
            const float bz0 = ot.centreZ - hhB - 0.165f;
            const float bz1 = ot.centreZ + hhB;
            const bool overlap = (ax0 < bx1 && ax1 > bx0) && (ay0 < by1 && ay1 > by0) && (az0 < bz1 && az1 > bz0);
            Need(!overlap, "zero overlapping frames or nameplates in 3D space");
        }

        // Walkable viewing spot 2.0 m in front of every canvas (4.5 m aisles between divider walls).
        const float vx = sp.localX + sp.normalX * 2.0f;
        const float vy = sp.localY + sp.normalY * 2.0f;
        Need(Grand::WalkAt(graph, CeMuseumExhibition::Vec2{vx, vy}) == CeMuseumExhibition::kWalkOpen,
             "viewing spot 2.0 m in front of every canvas is walkable");
        // Aiming directly at the canvas in 3D (lower OR upper tier) focuses that exact canvas!
        const float aimX = sp.localX - vx;
        const float aimY = sp.localY - vy;
        const float aimZ = sp.centreZ - 1.70f;
        Need(FocusCanvas3D(vx, vy, 1.70f, aimX, aimY, aimZ, sp.wing) == int(i),
             "standing in front and aiming at canvas focuses exact tier plate");
        Need(FocusCanvas3D(vx, vy, 1.70f, aimX, aimY, aimZ, CeMuseumTour::kWingCourtyard) == -1,
             "wrong wing clears canvas focus");
        Need(FocusCanvas3D(vx, vy, 1.70f, sp.normalX, sp.normalY, 0.0f, sp.wing) != int(i),
             "looking away clears focus on that canvas");
        if (sp.centreZ < 2.5f) {
            Need(FocusCanvas(vx, vy, -sp.normalX, -sp.normalY, sp.wing) == int(i),
                 "2D FocusCanvas picks lower-tier plate cleanly");
        }
        // Behind the wall never focuses this canvas.
        const float bx = sp.localX - sp.normalX * 2.0f;
        const float by = sp.localY - sp.normalY * 2.0f;
        Need(FocusCanvas3D(bx, by, 1.70f, sp.normalX, sp.normalY, sp.centreZ - 1.70f, sp.wing) != int(i),
             "behind wall cannot focus this canvas");
    }
    Need(FocusCanvas(17.5f, 19.0f, -1.0f, 0.0f, CeMuseumTour::kWingConceptArt) == -1,
         "looking away toward open archway clears focus");
    Need(FocusCanvas(-17.5f, 19.0f, 1.0f, 0.0f, CeMuseumTour::kWingEarlyDesigns) == -1,
         "looking away toward open archway clears focus in early designs");

    // 3b. Canonical retail lore assertions for characters, bosses, and signature units.
    auto artByGoodie = [&](int gid) -> const CanvasSpec & {
        for (unsigned i = 0; i < kManifestCount; ++i)
            if (kManifest[i].goodieId == gid) return kManifest[i];
        Need(false, "art goodie ID present in manifest");
        return kManifest[0];
    };
    Need(std::strcmp(artByGoodie(0).title, "Hawk Winter") == 0 &&
         std::strstr(artByGoodie(0).caption, "dockworker") != NULL, "goodie 0 is dockworker Hawk Winter");
    Need(std::strcmp(artByGoodie(1).title, "Tatiana Kiralova") == 0 &&
         std::strstr(artByGoodie(1).caption, "scientist") != NULL, "goodie 1 is scientist Tatiana Kiralova");
    Need(std::strcmp(artByGoodie(2).title, "Col. Chuck Kramer") == 0, "goodie 2 is Col. Chuck Kramer");
    Need(std::strcmp(artByGoodie(3).title, "Maj. Jason Lorenzo") == 0, "goodie 3 is Maj. Jason Lorenzo");
    Need(std::strcmp(artByGoodie(4).title, "Lt. Tara Fox") == 0, "goodie 4 is Lt. Tara Fox");
    Need(std::strcmp(artByGoodie(5).title, "Lt. Billy Casbah") == 0, "goodie 5 is Lt. Billy Casbah");
    Need(std::strcmp(artByGoodie(6).title, "Lewis Carver") == 0, "goodie 6 is Lewis Carver");
    Need(std::strcmp(artByGoodie(7).title, "Gen. Archanus Surt") == 0, "goodie 7 is Gen. Archanus Surt");
    Need(std::strstr(artByGoodie(24).title, "ACX-1 Venturer") != NULL, "goodie 24 is ACX-1 Venturer");
    Need(std::strstr(artByGoodie(34).title, "Warrior") != NULL, "goodie 34 identifies Muspell Warrior");
    Need(std::strcmp(artByGoodie(58).title, "Thunderhead Artillery Walker") == 0, "goodie 58 is Thunderhead Artillery Walker");
    Need(std::strstr(artByGoodie(60).title, "SSV-8 Thornback") != NULL, "goodie 60 is SSV-8 Thornback");
    Need(std::strcmp(artByGoodie(61).title, "Hive Mobile Construction Facility") == 0, "goodie 61 is Hive Mobile Construction Facility");
    Need(std::strcmp(artByGoodie(62).title, "Gill-M") == 0, "goodie 62 is Gill-M");
    Need(std::strstr(artByGoodie(63).title, "ASX-9F Vilifier") != NULL, "goodie 63 is ASX-9F Vilifier");
    Need(std::strcmp(artByGoodie(64).title, "Fenrir Airborne Flagship") == 0, "goodie 64 is Fenrir Airborne Flagship");
    Need(std::strstr(artByGoodie(74).title, "Ashley Gartell") != NULL, "goodie 74 credits Ashley Gartell");
    Need(std::strstr(artByGoodie(90).title, "Hive Mobile Factory") != NULL, "goodie 90 is Hive Mobile Factory");
    Need(std::strstr(artByGoodie(91).title, "Gill-M") != NULL, "goodie 91 is Gill-M concept");
    Need(std::strstr(artByGoodie(137).title, "M-112 Archer") != NULL, "goodie 137 is M-112 Archer");
    Need(std::strstr(artByGoodie(141).title, "XF-28 Venom") != NULL, "goodie 141 is XF-28 Venom");
    Need(std::strstr(artByGoodie(157).title, "ASX-9F Vilifier") != NULL, "goodie 157 is ASX-9F Vilifier archive");

    // 4. Truncated retail filename fallback matches goodie 95 & goodie 103 and rejects mismatches.
    Need(MatchesTruncatedCatalogPath(
             "data/resources/textures/goodies%ca_cutscene_locations%ca_cl_f_command_room_interior.t.aya",
             "goodies\\ca_cutscene_locations\\ca_cl_f_command_room_interior.tga"),
         "matches retail truncated goodie 95 (.t.aya)");
    Need(MatchesTruncatedCatalogPath(
             "data/resources/textures/goodies%ca_cutscene_locations%ca_cl_f_research_compound.tga(0.aya",
             "goodies\\ca_cutscene_locations\\ca_cl_f_research_compound.tga"),
         "matches retail truncated goodie 103 (.tga(0.aya)");
    Need(!MatchesTruncatedCatalogPath(
             "data/resources/textures/goodies%ca_cutscene_locations%ca_cl_f_docks_interior.tga(0).aya",
             "goodies\\ca_cutscene_locations\\ca_cl_f_command_room_interior.tga"),
         "rejects unrelated texture path");

    // 5. BoundedUpload: <= 256 px longest edge with aspect ratio preserved.
    const UploadSize uSmall = BoundedUpload(192, 128);
    Need(uSmall.width == 192 && uSmall.height == 128, "small texture kept unchanged");
    const UploadSize uLand = BoundedUpload(1024, 512);
    Need(uLand.width == 256 && uLand.height == 128, "landscape capped to 256 preserving 2:1");
    const UploadSize uPort = BoundedUpload(600, 1200);
    Need(uPort.width == 128 && uPort.height == 256, "portrait capped to 256 preserving 1:2");
    const UploadSize uZero = BoundedUpload(0, 512);
    Need(uZero.width == 1 && uZero.height == 1, "zero input safely clamped to 1x1");

    // 6. FitFrame: preserves source aspect ratio within frame opening.
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
            check("149/149 concept art manifest, 256px bounds, wall flushness, zero 3D overlap and focus hold", ok, detail)

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
    check("runtime opens asset catalog textures lazily and handles truncated filenames",
          "PortCeAssetCatalogOpenKinds" in cpp and "CE_ASSET_TEXTURE" in cpp
          and "ResolveTextureIndex(" in cpp and "MatchesTruncatedCatalogPath(" in cpp)
    check("runtime uploads bounded <=256px textures into D3DPOOL_MANAGED and deduplicates shared paths",
          "BoundedUpload" in cpp and "D3DPOOL_MANAGED" in cpp
          and "D3DXCreateTextureFromFileInMemoryEx" in cpp and "ownsTexture = false;" in cpp)
    check("runtime batches all 149 brass frames, mats, nameplates and art quads into static D3DPOOL_MANAGED GPU buffers",
          "frameVb" in cpp and "frameIb" in cpp and "artVb" in cpp
          and "DrawIndexedPrimitive(D3DPT_TRIANGLELIST" in cpp and "ReleaseFrameGpuBuffers();" in cpp)
    check("runtime degrades missing paintings cleanly without crashing",
          "gArt.slots[i].missing = true;" in cpp and "archival plate unavailable" in cpp)
    check("runtime releases textures and catalog on shutdown",
          "void RuntimeShutdown()" in cpp and "ReleaseAllTextures();" in cpp and "PortCeAssetCatalogClose" in cpp)
    header_txt = HEADER.read_text()
    check("no forbidden words in curator text",
          not re.search(r'"[^"]*(?:prototype|experimental|carrier|ce\.ini)[^"]*"', header_txt, re.I))
    banned_lore = (
        "Tatianna",
        "XF-24",
        "Thunderhead Super-Bomber",
        "Kraken Super-Weapon",
        "orbital-ridge",
        "naval war-machine",
        "Command Fortress",
        "Carver Strike Fighter",
        "Carver Interceptor",
        "Studio Portrait - Ashley",
        "Commonwealth",
        "Dominion",
    )
    check("canonical retail lore regression checks pass (no stale Tatianna/XF-24/Super-Bomber/Kraken/Dominion labels)",
          all(term not in header_txt for term in banned_lore))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
