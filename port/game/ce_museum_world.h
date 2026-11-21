// SPDX-License-Identifier: GPL-3.0-only
#ifndef CE_MUSEUM_WORLD_H
#define CE_MUSEUM_WORLD_H
#include "ce_museum_cinema.h"

// A CE-owned room. Resource archives supply the exhibits, never these dimensions,
// the lighting, terrain, circulation or the identity of either world body.
namespace CeMuseumWorld {
constexpr const char *kName = "ce.exhibition.units";
enum { kMap = 974, kBase = 975, kWorld = 976, kGridShift = 9,
       kCells = 1 << kGridShift, kPatchCells = 8, kPatchSamples = kPatchCells + 1,
       kPatches = kCells / kPatchCells, kHeightSamples = kPatches * kPatches * kPatchSamples * kPatchSamples };
constexpr float kFloor = 0.0f, kWaterBelow = 96.0f;
constexpr float kEntranceX = 256.0f, kEntranceY = 240.0f;
// Small enough that every bay stands beside the nave; the exhibition planner fits
// six machines here with their full viewing distance (port/game/ce_museum_exhibition.h).
constexpr float kHalfWidth = 11.0f, kFront = -4.0f, kBack = 30.0f;
// The theater's portal is cut in the hall's maxX wall.
static_assert(CeCinemaRoom::kDoorX == kHalfWidth && CeCinemaRoom::kWallX == kHalfWidth,
              "the cinema door must stand in the hall's side wall");
constexpr unsigned kSky = 0xffdfe9eeu, kFog = 0xffdfe9eeu;
constexpr unsigned kFloorA = 0xffd5d9d6u, kFloorB = 0xffe1e3dfu;
constexpr unsigned kWall = 0xfff3f1e9u, kWallCap = 0xffd7e1e3u;
constexpr unsigned kReveal = 0xff70848du, kGlazing = 0xffb8d0d6u;

// Visitor centres keep a shoulder's clearance inside the exact rendered walls.
// The entrance is a real vestibule; the familiar return action works there too.
inline bool Inside(float x, float y) {
    const float side=x-kEntranceX, depth=y-kEntranceY, clearance=0.56f;
    const bool room=side>=-kHalfWidth+clearance && side<=kHalfWidth-clearance &&
                    depth>=kFront+clearance && depth<=kBack-clearance;
    const bool theater=CeCinemaRoom::Interior(side,depth) && side>=CeCinemaRoom::kLeft+clearance;
    return (room || theater || CeCinemaRoom::InDoor(side,depth)) && !CeCinemaRoom::Seat(side,depth);
}
// Keep the forward sightline aligned with the central plinth, with a separate
// rear row and a clear outside aisle. Small collections still centre their sign.
inline bool Plinth(unsigned i, unsigned count, float *x, float *y) {
    if (!x || !y || !count || count>6 || i>=count) return false;
    const unsigned columns=count<3 ? count : 3, col=i%columns;
    *x=kEntranceX + (float(col)-float(columns/2))*6.1f;
    *y=kEntranceY + (i<columns ? 8.0f : 20.0f);
    return true;
}
}
#endif
