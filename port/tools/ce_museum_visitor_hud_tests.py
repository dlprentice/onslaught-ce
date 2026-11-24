#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Host tests for Grand Museum Visitor HUD patch (0366), procedural textures, and Atrium Directory tour."""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GAME = ROOT / "port/game"
PATCH = ROOT / "port/patches/0366-ce-museum-visitor-hud.patch"
GEN = ROOT / "port/tools/generate_museum_textures.py"
TEX_CPP = GAME / "ce_museum_textures.cpp"
TOUR_H = GAME / "ce_museum_tour.h"

DRIVER = r"""
#include "ce_museum_tour.h"
#include "ce_museum_exhibition.h"
#include "ce_museum_textures.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace CeMuseumTour;
namespace Grand = CeMuseumExhibition::Grand;

static void Need(bool ok, const char *why) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", why);
        std::exit(2);
    }
}

static bool AsciiOnly(const char *s) {
    if (!s || !*s) return false;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(s); *p; ++p)
        if (*p < 0x20u || *p > 0x7eu) return false;
    return true;
}

static bool IsBrightBrass(uint32_t px) {
    const unsigned r = (px >> 16) & 255u;
    const unsigned g = (px >> 8) & 255u;
    const unsigned b = px & 255u;
    return r >= 170u && g >= 135u && r > b + 40u;
}

static bool IsDarkObsidian(uint32_t px) {
    const unsigned r = (px >> 16) & 255u;
    const unsigned g = (px >> 8) & 255u;
    const unsigned b = px & 255u;
    return r <= 55u && g <= 65u && b <= 75u;
}

int main() {
    const Grand::RoomGraph graph = Grand::MakeRoomGraph();
    Need(kWingCount == 8, "eight rooms in Grand Museum tour");

    // Courtyard arrival cue explicitly helps players discover Shift-sprint.
    Need(std::strstr(Meta(kWingCourtyard).subtitle, "Shift") != NULL &&
         std::strstr(Meta(kWingCourtyard).subtitle, "sprint") != NULL,
         "courtyard arrival subtitle mentions Shift sprint");

    // Every wing's metadata and quick-jump landing coordinate must be walkable inside its own room.
    for (int w = 0; w < int(kWingCount); ++w) {
        const Wing wing = static_cast<Wing>(w);
        const WingMeta &m = Meta(wing);
        Need(m.wing == wing, "meta wing matches index");
        Need(AsciiOnly(m.title) && AsciiOnly(m.subtitle), "ASCII wing copy");
        const CeMuseumExhibition::Vec2 pt = {m.jumpX, m.jumpY};
        Need(Grand::RoomAt(graph, pt) == static_cast<Grand::RoomId>(wing),
             "quick-jump lands inside its target room");
        Need(Grand::WalkAt(graph, pt) == CeMuseumExhibition::kWalkOpen,
             "quick-jump lands on open walkable floor");
    }

    // Guided tour sequence visits all 8 wings and wraps to Courtyard.
    Wing cur = kWingCourtyard;
    for (int step = 0; step < int(kWingCount); ++step) cur = NextTourWing(cur);
    Need(cur == kWingCourtyard, "tour wraps after visiting every wing");

    // Atrium Directory Console stands on the west entrance flank; central sightline is open; rotunda centerpiece stands at (0, 36).
    Need(Grand::WalkAt(graph, CeMuseumExhibition::Vec2{kDirectoryX, kDirectoryY}) == CeMuseumExhibition::kWalkBlocked,
         "relocated directory console blocks walking");
    Need(Grand::WalkAt(graph, CeMuseumExhibition::Vec2{kDirectoryX, kDirectoryY - 2.0f}) == CeMuseumExhibition::kWalkOpen,
         "standing point in front of directory console is open");
    Need(Grand::WalkAt(graph, CeMuseumExhibition::Vec2{0.0f, 31.5f}) == CeMuseumExhibition::kWalkOpen,
         "central atrium entrance sightline is unobstructed");
    Need(Grand::WalkAt(graph, CeMuseumExhibition::Vec2{0.0f, 36.0f}) == CeMuseumExhibition::kWalkBlocked,
         "atrium rotunda centerpiece pedestal is reserved at (0, 36)");

    // Atrium Directory Console ray-pick hits each of the 6 brass plates and refuses out-of-range/off-axis.
    for (int i = 0; i < kDirectoryStops; ++i) {
        Need(AsciiOnly(kDirectory[i].label), "ASCII directory plate label");
        const float px = kDirectoryX + kDirectory[i].localOffsetX;
        const float py = kDirectoryY - 0.18f;
        const float pz = kDirectoryZ;
        const float ex = px, ey = py - 2.0f, ez = 1.7f;
        const DirectoryHit hit = PickDirectory(ex, ey, ez, px - ex, py - ey, pz - ez);
        Need(hit.valid && hit.index == i, "direct aim picks exact directory plate");
    }
    Need(!PickDirectory(kDirectoryX, kDirectoryY - 8.0f, 1.7f, 0.0f, 1.0f, -0.05f).valid,
         "out-of-range aim misses directory");
    Need(!PickDirectory(kDirectoryX, kDirectoryY - 2.0f, 1.7f, 0.0f, -1.0f, 0.0f).valid,
         "looking away misses directory");

    // Baked 1024x512 Portal Lintel Signage Atlas: all 8 strips have obsidian backing, brass border,
    // distinct engraved lettering, and non-overlapping UV windows.
    {
        std::vector<unsigned int> portal(CeMuseumTextures::kPortalAtlasWidth * CeMuseumTextures::kPortalAtlasHeight, 0u);
        CeMuseumTextures::BakePortalSignAtlasRgba(portal.data());
        Need(CE_MUSEUM_SIGN_COUNT == 8, "eight portal signage strips");
        std::vector<uint64_t> stripHashes(CE_MUSEUM_SIGN_COUNT, 0u);
        for (unsigned s = 0; s < CE_MUSEUM_SIGN_COUNT; ++s) {
            const CeMuseumPortalSignId id = static_cast<CeMuseumPortalSignId>(s);
            Need(AsciiOnly(CeMuseumTextures::PortalSignLabel(id)), "ASCII portal sign label");
            const CeMuseumAtlasUv uv = CeMuseumTextures::PortalSignUv(id);
            Need(uv.u0 > 0.0f && uv.u1 < 1.0f && uv.u0 < uv.u1, "valid portal U range");
            Need(uv.v0 > 0.0f && uv.v1 < 1.0f && uv.v0 < uv.v1, "valid portal V range");
            unsigned brassTextPx = 0u, obsidianPx = 0u;
            uint64_t hash = 14695981039346656037ull;
            for (unsigned y = 14u; y < 50u; ++y) {
                for (unsigned x = 40u; x < CeMuseumTextures::kPortalAtlasWidth - 40u; ++x) {
                    const uint32_t px = portal[(s * CeMuseumTextures::kPortalStripHeight + y) *
                                               CeMuseumTextures::kPortalAtlasWidth + x];
                    if (IsBrightBrass(px)) ++brassTextPx;
                    if (IsDarkObsidian(px)) ++obsidianPx;
                    hash = (hash ^ px) * 1099511628211ull;
                }
            }
            Need(brassTextPx >= 800u, "portal strip contains crisp engraved brass lettering");
            Need(obsidianPx >= 15000u, "portal strip retains honed dark obsidian background");
            for (unsigned prev = 0; prev < s; ++prev)
                Need(stripHashes[prev] != hash, "every portal strip renders a distinct title");
            stripHashes[s] = hash;
        }
    }

    // Baked 512x512 Atrium Directory Floor-Plan & Labeled Console Buttons Atlas.
    {
        std::vector<unsigned int> dir(CeMuseumTextures::kDirectoryAtlasWidth * CeMuseumTextures::kDirectoryAtlasHeight, 0u);
        CeMuseumTextures::BakeDirectoryAtlasRgba(dir.data());
        const CeMuseumAtlasUv muv = CeMuseumTextures::DirectoryMapUv();
        Need(muv.u0 > 0.0f && muv.u1 < 1.0f && muv.v0 > 0.0f && muv.v1 <= 0.75f, "valid directory map UV");
        unsigned redBeaconPx = 0u, mapBrassPx = 0u;
        for (unsigned y = 0; y < CeMuseumTextures::kDirectoryMapHeight; ++y) {
            for (unsigned x = 0; x < CeMuseumTextures::kDirectoryAtlasWidth; ++x) {
                const uint32_t px = dir[y * CeMuseumTextures::kDirectoryAtlasWidth + x];
                const unsigned r = (px >> 16) & 255u, g = (px >> 8) & 255u, b = px & 255u;
                if (r >= 175u && g <= 95u && b <= 95u) ++redBeaconPx;
                if (IsBrightBrass(px)) ++mapBrassPx;
            }
        }
        Need(redBeaconPx >= 20u, "directory floor-plan renders YOU ARE HERE beacon");
        Need(mapBrassPx >= 2000u, "directory floor-plan renders brass framing and labels");

        std::vector<uint64_t> btnHashes(CE_MUSEUM_DIR_BTN_COUNT, 0u);
        for (unsigned b = 0; b < CE_MUSEUM_DIR_BTN_COUNT; ++b) {
            const CeMuseumDirButtonId bid = static_cast<CeMuseumDirButtonId>(b);
            Need(AsciiOnly(CeMuseumTextures::DirectoryButtonLabel(bid)), "ASCII directory button label");
            const CeMuseumAtlasUv buv = CeMuseumTextures::DirectoryButtonUv(bid);
            Need(buv.u0 >= 0.0f && buv.u1 <= 1.0f && buv.u0 < buv.u1, "valid directory button U");
            Need(buv.v0 >= 0.75f && buv.v1 <= 1.0f && buv.v0 < buv.v1, "valid directory button V");
            const unsigned bx0 = static_cast<unsigned>(buv.u0 * CeMuseumTextures::kDirectoryAtlasWidth) + 10u;
            const unsigned bx1 = static_cast<unsigned>(buv.u1 * CeMuseumTextures::kDirectoryAtlasWidth) - 10u;
            const unsigned by0 = static_cast<unsigned>(buv.v0 * CeMuseumTextures::kDirectoryAtlasHeight) + 12u;
            const unsigned by1 = static_cast<unsigned>(buv.v1 * CeMuseumTextures::kDirectoryAtlasHeight) - 14u;
            unsigned brassPx = 0u;
            uint64_t hash = 14695981039346656037ull;
            for (unsigned y = by0; y < by1; ++y) {
                for (unsigned x = bx0; x < bx1; ++x) {
                    const uint32_t px = dir[y * CeMuseumTextures::kDirectoryAtlasWidth + x];
                    if (IsBrightBrass(px)) ++brassPx;
                    hash = (hash ^ px) * 1099511628211ull;
                }
            }
            Need(brassPx >= 120u, "directory button renders legible engraved brass label");
            for (unsigned prev = 0; prev < b; ++prev)
                Need(btnHashes[prev] != hash, "every directory button renders a distinct wing label");
            btnHashes[b] = hash;
        }
    }

    std::printf("ce_museum_visitor_hud_tests: all tour, signage, and directory checks passed\n");
    return 0;
}
"""


def run(cmd, expect=0):
    res = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    return res.returncode == expect, res.stdout + res.stderr


def main():
    failures = []

    def check(name, ok, detail=""):
        print(("ok   " if ok else "FAIL ") + name + ("" if ok else ("\n" + detail if detail else "")))
        if not ok:
            failures.append(name)

    # 1. Procedural GPL-3.0-only museum texture generator matches committed header byte-for-byte.
    ok, detail = run([sys.executable, "-B", str(GEN), "--check"])
    check("generate_museum_textures.py --check matches ce_museum_textures_data.h", ok, detail)

    # 2. Tour quick-jump walkability & Directory Lectern pick checks.
    out = ROOT / "local-data" / "museum-visitor-hud-tests"
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=out) as tmp:
        d = Path(tmp)
        (d / "driver.cpp").write_text(DRIVER)
        flags = ["c++", "-std=c++14", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                 "-DCE_MUSEUM_TEXTURES_HOST_TEST=1",
                 "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        ok, detail = run(flags + ["-I" + str(GAME), str(d / "driver.cpp"), str(TEX_CPP), "-o", str(d / "driver")])
        check("tour, signage & directory driver builds with ASan/UBSan", ok, detail)
        if ok:
            ok, detail = run([str(d / "driver")])
            check("every quick-jump destination is walkable, directory picking and baked atlases hold", ok, detail)

    # 3. Visitor HUD patch 0366 scope, pure retail anchoring, and photo-mode structure preservation.
    patch = PATCH.read_text()
    files = re.findall(r"^--- a/(.+)$", patch, re.M)
    check("patch 0366 touches only DXEngine.cpp and Hud.cpp",
          files == ["DXEngine.cpp", "Hud.cpp"], f"got {files}")
    check("patch 0366 anchors purely on retail lines without 0133/0190 context",
          "CePhotoDrawScope" not in patch and "CeHudScope" not in patch and "CE_NETWORK_OFF" not in patch)
    check("patch 0366 suppresses walker cockpit barrels during Museum visits only",
          "PortCeMuseumVisitActive" in patch and "player = NULL;" in patch
          and "if (player)" in patch and "#ifdef CE_VERSION" in patch)
    check("patch 0366 suppresses combat HUD overlay and battleline badge while keeping museum placards",
          patch.count("if (PortCeMuseumVisitActive())\n+\t\treturn;") == 2
          and "SetOverlayRenderStates();" in patch
          and "mCircleDarkener" in patch)
    sys.path.insert(0, str(ROOT / "port"))
    sys.path.insert(0, str(ROOT / "port/tools"))
    import build  # noqa: E402
    import ce_photo_tests  # noqa: E402
    try:
        ce_photo_tests.check_structure(build.patched_text(["DXEngine.cpp", "Hud.cpp", "ltshell.cpp", "game.cpp"]))
        check("ce_photo_tests.check_structure passes with patch 0366 applied", True)
    except AssertionError as err:
        check("ce_photo_tests.check_structure passes with patch 0366 applied", False, str(err))

    # 4. Texture loader checks (zlib decompression, managed D3D9 texture, missing-texture fallback).
    tex = TEX_CPP.read_text()
    check("texture loader decompresses embedded GPL textures and uploads D3DPOOL_MANAGED",
          "uncompress(" in tex and "D3DXCreateTextureFromFileInMemoryEx" in tex and "D3DPOOL_MANAGED" in tex)
    check("texture loader honors BEA_PORT_MUSEUM_MISSING_TEXTURE and degrades cleanly",
          "BEA_PORT_MUSEUM_MISSING_TEXTURE" in tex and "void ReleaseAll()" in tex)
    check("tour strings stay free of forbidden words",
          not re.search(r'"[^"]*(?:prototype|experimental|carrier|ce\.ini)[^"]*"', TOUR_H.read_text(), re.I))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
