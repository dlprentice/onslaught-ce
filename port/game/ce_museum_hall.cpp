// SPDX-License-Identifier: GPL-3.0-only
// Grand Outdoor/Indoor Museum architecture, procedural coastal ocean/headland panorama,
// indexed subdivided geometry with baked per-vertex AO & skylight pools, calibrated 3-point
// architectural lighting with specular sheen, contact-shadow decals, original GPL-3.0-only
// architectural materials, interactive fixtures, and Screening Room host.
// All 3D exhibits, paintings, films, and audio clips load at runtime from the player's
// installed retail files; a missing texture or asset falls back cleanly to plain colour.
#ifdef CE_VERSION
#include "common.h"
#include "ltshell.h"
#include "renderinfo.h"
#include "ce_museum_hall.h"
#include "ce_museum_exhibition_host.h"
#include "ce_museum_world.h"
#include "ce_museum_textures.h"
#include "ce_museum_tour.h"
#include "ce_museum_turntable.h"
#include "ce_museum_art.h"
#include "ce_museum_audio_kiosk.h"
#include "ce_museum_cinema.h"
#include "ce_museum_cinema_catalogue.h"
#include "ce_museum_cinema_view.h"
#include "ce_museum_music_duck.h"
#include "ce_museum_film.h"
#include "ce_photo.h"
#include "binkapi.h"
#include "game.h"
#include "Controller.h"
#include "ce_tutorial_pad.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "ce_museum_visit.h"
#include "ce_museum_label.h"
#include "../platform/common/portlog.h"
#include "Texture.h"

#if defined(__has_include)
#if __has_include("ce_archive_pedestal.h")
#include "ce_archive_pedestal.h"
#endif
#endif

extern "C" unsigned PortPresentCount();

extern "C" bool PortCeMuseumVisitActive(void);
extern bool gMirrorPass;
int PortCeGamepadInputs(int id, int pad, SControlInput *out);

namespace {
struct Vertex { float x, y, z; DWORD colour; };
const DWORD kIron = 0xff263641, kSlate = 0xff405563, kEdge = 0xff899aa1;
const DWORD kBrass = 0xffd4a654, kLight = 0xffe6cb88;

enum Material {
    kMatAuto, kMatFlat, kMatFloor, kMatCourtyard, kMatInlay, kMatWall, kMatPier, kMatHeader, kMatDoor,
    kMatPlinth, kMatStand, kMatRail, kMatTrim, kMatNumeral, kMatCeiling,
    kMatStone, kMatCarpet, kMatSeat, kMatPlinthTop, kMatSmallPlinth, kMatSmallTop,
    kMatScreenFrame, kMatCinemaWall, kMatCinemaCeiling, kMatCinemaPost,
    kMatPierCap, kMatPierBase, kMatPierLine, kMatCinemaCap, kMatCinemaBase, kMatScreenIdle,
    kMatWater, kMatForsetiTrim, kMatMuspellTrim,
    kMatMuspellIron, kMatMuspellWall, kMatForsetiGlow, kMatMuspellGlow, kMatWarmGlow, kMatAquaGlow,
    kMatCount
};

// Each architectural surface maps to one original GPL-3.0-only texture (CE_MUSEUM_TEX_COUNT = plain colour),
// plus calibrated RGB emissive and specular sheen parameters for brushed brass, water, marble, and steel.
struct MaterialSpec {
    CeMuseumTexId tex;
    float repeat;
    DWORD tint;
    float emissiveR, emissiveG, emissiveB;
    float specR, specG, specB;
    float specPower;
    float quiet;
    bool unbakedGlow;
};

const MaterialSpec kMaterials[kMatCount] = {
    {CE_MUSEUM_TEX_COUNT,           1.0f, 0,          0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f,  1.0f, 0.00f, false}, // auto
    {CE_MUSEUM_TEX_COUNT,           1.0f, 0,          0.05f, 0.05f, 0.05f, 0.00f, 0.00f, 0.00f,  1.0f, 0.00f, false}, // flat
    {CE_MUSEUM_TEX_GALLERY_FLOOR,   3.0f, 0xfffaf8f3, 0.00f, 0.00f, 0.00f, 0.24f, 0.25f, 0.26f, 22.0f, 0.00f, false}, // gallery floor slabs
    {CE_MUSEUM_TEX_COURTYARD_PAVER, 3.0f, 0xfffffbf2, 0.02f, 0.02f, 0.01f, 0.14f, 0.13f, 0.11f, 14.0f, 0.00f, false}, // courtyard sandstone pavers
    {CE_MUSEUM_TEX_RUNNER_STONE,    2.6f, 0xffe2e8ee, 0.00f, 0.00f, 0.00f, 0.28f, 0.30f, 0.34f, 26.0f, 0.00f, false}, // basalt wayfinding runner
    {CE_MUSEUM_TEX_LIMESTONE_WALL,  3.8f, 0xfffcf8f0, 0.00f, 0.00f, 0.00f, 0.08f, 0.08f, 0.07f, 10.0f, 0.00f, false}, // dressed limestone walls
    {CE_MUSEUM_TEX_SLATE_PIER,      2.4f, 0xffe4ebf2, 0.00f, 0.00f, 0.00f, 0.24f, 0.27f, 0.32f, 20.0f, 0.00f, false}, // honed slate piers/columns
    {CE_MUSEUM_TEX_LIMESTONE_WALL,  3.4f, 0xfff6f1e7, 0.00f, 0.00f, 0.00f, 0.08f, 0.08f, 0.07f, 10.0f, 0.00f, false}, // portal header beams
    {CE_MUSEUM_TEX_SLATE_PIER,      2.2f, 0xffccd6de, 0.00f, 0.00f, 0.00f, 0.20f, 0.23f, 0.28f, 18.0f, 0.00f, false}, // return arch / doors
    {CE_MUSEUM_TEX_PLINTH_MARBLE,   1.8f, 0xfff4f7fa, 0.00f, 0.00f, 0.00f, 0.34f, 0.35f, 0.38f, 26.0f, 0.00f, false}, // plinth bodies
    {CE_MUSEUM_TEX_SLATE_PIER,      1.6f, 0xffc6d0d8, 0.00f, 0.00f, 0.00f, 0.24f, 0.26f, 0.30f, 20.0f, 0.00f, false}, // lecterns / flight pylons
    {CE_MUSEUM_TEX_SLATE_PIER,      1.8f, 0xffd6dee4, 0.00f, 0.00f, 0.00f, 0.22f, 0.24f, 0.28f, 18.0f, 0.00f, false}, // balustrades / rails
    {CE_MUSEUM_TEX_BRUSHED_BRASS,   1.4f, 0xfffff2d2, 0.12f, 0.09f, 0.04f, 0.88f, 0.72f, 0.40f, 28.0f, 0.00f, false}, // brushed museum brass
    {CE_MUSEUM_TEX_BRUSHED_BRASS,   1.0f, 0xfffff5ce, 0.55f, 0.48f, 0.30f, 0.72f, 0.62f, 0.36f, 24.0f, 0.00f, true},  // glowing brass numerals/bars
    {CE_MUSEUM_TEX_COFFERED_CEILING,4.0f, 0xffedf1f6, 0.03f, 0.03f, 0.04f, 0.00f, 0.00f, 0.00f,  1.0f, 0.00f, false}, // coffered ceiling
    {CE_MUSEUM_TEX_CINEMA_ACOUSTIC, 2.4f, 0xffb2bcc6, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f,  1.0f, 0.00f, false}, // cinema proscenium wall
    {CE_MUSEUM_TEX_RUNNER_STONE,    2.2f, 0xff9a8a8e, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f,  1.0f, 0.00f, false}, // cinema dark carpet
    {CE_MUSEUM_TEX_CINEMA_ACOUSTIC, 1.2f, 0xffd07e80, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f,  1.0f, 0.00f, false}, // cinema seats upholstery
    {CE_MUSEUM_TEX_SLATE_PIER,      1.6f, 0xffd0dae2, 0.00f, 0.00f, 0.00f, 0.38f, 0.42f, 0.46f, 30.0f, 0.00f, false}, // plinth top deck
    {CE_MUSEUM_TEX_PLINTH_MARBLE,   1.6f, 0xfff6f8fa, 0.06f, 0.06f, 0.06f, 0.30f, 0.32f, 0.34f, 24.0f, 0.00f, false}, // small exhibit plinth
    {CE_MUSEUM_TEX_SLATE_PIER,      1.4f, 0xffd6e0e8, 0.10f, 0.10f, 0.10f, 0.34f, 0.38f, 0.42f, 26.0f, 0.00f, false}, // small exhibit plinth top
    {CE_MUSEUM_TEX_SLATE_PIER,      2.0f, 0xff8d97a1, 0.00f, 0.00f, 0.00f, 0.16f, 0.18f, 0.20f, 16.0f, 0.00f, false}, // cinema screen frame
    {CE_MUSEUM_TEX_CINEMA_ACOUSTIC, 2.6f, 0xffc4ced8, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f,  1.0f, 0.00f, false}, // cinema side walls
    {CE_MUSEUM_TEX_COFFERED_CEILING,4.0f, 0xffb8c2cc, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f,  1.0f, 0.00f, false}, // cinema ceiling
    {CE_MUSEUM_TEX_SLATE_PIER,      1.6f, 0xffb4bec8, 0.00f, 0.00f, 0.00f, 0.18f, 0.20f, 0.22f, 16.0f, 0.00f, false}, // projector post
    {CE_MUSEUM_TEX_LIMESTONE_WALL,  2.0f, 0xfffefaf2, 0.00f, 0.00f, 0.00f, 0.12f, 0.12f, 0.10f, 12.0f, 0.00f, false}, // pier cap
    {CE_MUSEUM_TEX_SLATE_PIER,      2.0f, 0xff9aa4ae, 0.00f, 0.00f, 0.00f, 0.20f, 0.22f, 0.26f, 18.0f, 0.00f, false}, // pier base course
    {CE_MUSEUM_TEX_SLATE_PIER,      2.0f, 0xff46505a, 0.00f, 0.00f, 0.00f, 0.12f, 0.14f, 0.16f, 12.0f, 0.00f, false}, // floor shadow reveal line
    {CE_MUSEUM_TEX_LIMESTONE_WALL,  2.0f, 0xffe4e0d8, 0.00f, 0.00f, 0.00f, 0.08f, 0.08f, 0.08f, 10.0f, 0.00f, false}, // cinema projector cap
    {CE_MUSEUM_TEX_SLATE_PIER,      2.0f, 0xff848e98, 0.00f, 0.00f, 0.00f, 0.16f, 0.18f, 0.20f, 14.0f, 0.00f, false}, // cinema projector base
    {CE_MUSEUM_TEX_COUNT,           1.0f, 0,          0.00f, 0.00f, 0.00f, 0.08f, 0.10f, 0.12f, 20.0f, 0.00f, false}, // idle cinema screen glass
    {CE_MUSEUM_TEX_WATER_BASIN,     2.8f, 0xffe8f8ff, 0.10f, 0.22f, 0.28f, 0.96f, 0.98f, 1.00f, 48.0f, 0.00f, false}, // reflecting pool water
    {CE_MUSEUM_TEX_FORSETI_TRIM,    2.0f, 0xfff2f8ff, 0.05f, 0.10f, 0.16f, 0.46f, 0.66f, 0.88f, 24.0f, 0.00f, false}, // Forseti steel-blue ribs/trim
    {CE_MUSEUM_TEX_MUSPELL_TRIM,    2.0f, 0xffffede0, 0.10f, 0.04f, 0.01f, 0.56f, 0.36f, 0.20f, 20.0f, 0.00f, false}, // Muspell riveted bronze fascia
    {CE_MUSEUM_TEX_FORGED_IRON,     2.4f, 0xffe6e4e8, 0.02f, 0.02f, 0.02f, 0.44f, 0.38f, 0.34f, 18.0f, 0.00f, false}, // Muspell forged-iron buttresses
    {CE_MUSEUM_TEX_FORGED_IRON,     3.6f, 0xffc8c2c0, 0.01f, 0.01f, 0.01f, 0.24f, 0.20f, 0.18f, 14.0f, 0.00f, false}, // Muspell volcanic-iron walls
    {CE_MUSEUM_TEX_COUNT,           1.0f, 0xff80ecff, 0.28f, 0.86f, 1.00f, 0.60f, 0.90f, 1.00f, 24.0f, 0.00f, true},  // Forseti luminous cyan strip
    {CE_MUSEUM_TEX_COUNT,           1.0f, 0xffffa644, 1.00f, 0.50f, 0.12f, 0.90f, 0.55f, 0.20f, 20.0f, 0.00f, true},  // Muspell molten-amber forge strip
    {CE_MUSEUM_TEX_COUNT,           1.0f, 0xfffff2cf, 0.92f, 0.82f, 0.58f, 0.60f, 0.55f, 0.40f, 20.0f, 0.00f, true},  // warm architectural cove glow
    {CE_MUSEUM_TEX_COUNT,           1.0f, 0xff62e8f6, 0.22f, 0.80f, 0.92f, 0.70f, 0.95f, 1.00f, 32.0f, 0.00f, true},  // reflecting-pool aqua rim glow
};

bool CinemaMaterial(unsigned m) {
    switch (m) {
    case kMatStone: case kMatCarpet: case kMatSeat: case kMatScreenFrame: case kMatCinemaWall:
    case kMatCinemaCeiling: case kMatCinemaPost: case kMatCinemaCap: case kMatCinemaBase: return true;
    default: return false;
    }
}

struct LitVertex { float x, y, z, nx, ny, nz; DWORD colour; float u, v; };
struct OverlayVertex { float x, y, z; DWORD colour; };
enum {
    kLitCapacity = 110000,
    kIdxCapacity = 360000,
    kShadowVertCap = 12000,
    kShadowIdxCap = 36000,
    kGlassVertCap = 8000,
    kGlassIdxCap = 24000
};

struct Gallery {
    CeHallTour tour;
    CeHallGround ground;
    LitVertex vertices[kLitCapacity];
    uint32_t indices[kIdxCapacity];
    uint32_t sortedIndices[kIdxCapacity];
    unsigned char material[kIdxCapacity / 3];
    unsigned usedVerts, usedIndices;
    unsigned firstIdx[kMatCount], triCount[kMatCount];
    unsigned minVert[kMatCount], maxVert[kMatCount];
    OverlayVertex shadowVerts[kShadowVertCap];
    uint16_t shadowIndices[kShadowIdxCap];
    unsigned shadowVertCount, shadowIdxCount;
    OverlayVertex glassVerts[kGlassVertCap];
    uint16_t glassIndices[kGlassIdxCap];
    unsigned glassVertCount, glassIdxCount;
    IDirect3DTexture9 *texture[kMatCount];
    Material current;
    bool hasCentre;
    float centre[3];
    int zone;
    bool valid, overflow;
    const char *overflowReason;
};
static Gallery *gPtr = NULL;

Gallery *EnsureGallery() {
    if (!gPtr) {
        gPtr = static_cast<Gallery *>(std::calloc(1, sizeof(Gallery)));
        if (gPtr) {
            gPtr->zone = -1;
            gPtr->current = kMatAuto;
        }
    }
    return gPtr;
}

void FreeGallery() {
    if (gPtr) {
        std::free(gPtr);
        gPtr = NULL;
    }
}

static CeMuseumExhibition::Plan gExhibition = {};
static CeMuseumExhibition::Vec2 gExhibitionVisitor = {};
static CeMuseumExhibition::Grand::RoomGraph gGrandGraph = {};
static bool gGrandMode = false;
static float gGrandFloorZ = 0.0f;
static CeMuseumTour::Wing gCurrentWing = CeMuseumTour::kWingCourtyard;
static CeMuseumTour::Wing gAnticipatedWing = CeMuseumTour::kWingCourtyard;
static bool gWingAnnounced = false;
static double gLastStepClock = 0.0;
static bool gHasStepClock = false;

// Atrium Directory Console interaction & quick-jump state
static CeMuseumTour::DirectoryHit gDirHit = {false, -1};
static bool gDirFireHeld = false;
static bool gDirFireCaptured = false;
static bool gPendingJump = false;
static float gJumpX = 0.0f, gJumpY = 0.0f, gJumpYaw = 0.0f;
#ifdef CE_MUSEUM_HAS_ARCHIVE_PEDESTAL
static bool gArchiveKeyPrev[6] = {};
#endif

// Calibrated coastal sunlight direction (vector FROM scene TOWARD the sun; native -Z is up).
// East-Southeast afternoon sun (~37 deg elevation) rakes the Courtyard Facade, Hero Monument,
// and streams through the Grand Atrium clerestory skylight onto the Rotunda centerpiece.
constexpr float kSunDirX =  0.620f;
constexpr float kSunDirY = -0.500f;
constexpr float kSunDirZ = -0.604f;

void Overflow(const char *why) {
    if (!gPtr) return;
    if (!gPtr->overflow) gPtr->overflowReason = why;
    gPtr->overflow = true;
}

Material ColourMaterial(DWORD colour) {
    switch (colour) {
    case kBrass: case 0xffb99262: return kMatTrim;
    case kLight: return kMatNumeral;
    case kIron: case kSlate: case kEdge: return kMatStand;
    case 0xff69767a: case 0xff1c272d: return kMatStone;
    case 0xff39444b: case 0xff4b3539: return kMatCarpet;
    case 0xff913f43: case 0xffb16961: return kMatSeat;
    case 0xfff3f1e9: return kMatWall;
    case 0xffd5d9d6: case 0xffe1e3df: return kMatFloor;
    default: return kMatFlat;
    }
}

static inline float Clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }
static inline float SmoothStep(float e0, float e1, float x) {
    const float t = Clamp01((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}

static DWORD ModulateRgb(DWORD base, float rMul, float gMul, float bMul) {
    auto ch = [](DWORD v, unsigned sh, float mul) -> DWORD {
        const float f = float((v >> sh) & 255u) * mul;
        const int iv = f <= 0.0f ? 0 : f >= 255.0f ? 255 : int(f + 0.5f);
        return static_cast<DWORD>(iv) << sh;
    };
    return (base & 0xff000000u) | ch(base, 16, rMul) | ch(base, 8, gMul) | ch(base, 0, bMul);
}

// Procedural baked hemisphere ambient occlusion + wall-floor crease darkening + corner shadowing
// + skylight daylight pools + subtle vertical sun/bounce gradients for the Grand Museum complex.
static DWORD BakeGrandVertexColour(float wx, float wy, float wz,
                                   float nx, float ny, float nz,
                                   Material m, DWORD baseCol) {
    if (!gGrandMode || kMaterials[m].unbakedGlow || m == kMatScreenIdle) return baseCol;

    const float lx = wx - CeMuseumWorld::kEntranceX;
    const float ly = wy - CeMuseumWorld::kEntranceY;
    const float lz = gGrandFloorZ - wz; // height above floor (positive upward)

    using namespace CeMuseumExhibition::Grand;
    RoomId rid = RoomAt(gGrandGraph, {lx, ly});
    if (rid == kRoomCount) {
        // Snap perimeter walls/cornices slightly outside room bounds to the nearest room.
        float bestD = 1e9f;
        for (unsigned r = 0; r < kRoomCount; ++r) {
            const RoomBounds &b = gGrandGraph.rooms[r];
            const float dx = lx < b.minX ? b.minX - lx : (lx > b.maxX ? lx - b.maxX : 0.0f);
            const float dy = ly < b.minY ? b.minY - ly : (ly > b.maxY ? ly - b.maxY : 0.0f);
            const float d = dx * dx + dy * dy;
            if (d < bestD) { bestD = d; rid = static_cast<RoomId>(r); }
        }
    }
    const RoomBounds &rb = gGrandGraph.rooms[rid];

    float ao = 1.0f;
    float lightR = 1.0f, lightG = 1.0f, lightB = 1.0f;

    // 1. Distances to enclosing room walls, relaxing near open portals below portal clear height.
    float dW = std::fabs(lx - rb.minX);
    float dE = std::fabs(rb.maxX - lx);
    float dS = std::fabs(ly - rb.minY);
    float dN = std::fabs(rb.maxY - ly);
    for (unsigned p = 0; p < kPortalCount; ++p) {
        const Portal &pt = gGrandGraph.portals[p];
        if (pt.a != rid && pt.b != rid) continue;
        if (lz > pt.clearHeight - 0.15f) continue;
        if (pt.vertical) {
            if (ly >= pt.lo + 0.1f && ly <= pt.hi - 0.1f) {
                if (std::fabs(pt.line - rb.minX) < 0.6f) dW = 4.0f;
                if (std::fabs(pt.line - rb.maxX) < 0.6f) dE = 4.0f;
            }
        } else {
            if (lx >= pt.lo + 0.1f && lx <= pt.hi - 0.1f) {
                if (std::fabs(pt.line - rb.minY) < 0.6f) dS = 4.0f;
                if (std::fabs(pt.line - rb.maxY) < 0.6f) dN = 4.0f;
            }
        }
    }
    const float dX = dW < dE ? dW : dE;
    const float dY = dS < dN ? dS : dN;
    const float dWall = dX < dY ? dX : dY;

    // 2. Wall-floor crease darkening & concave room corner shadowing.
    const bool isFloor = (nz < -0.65f) && (lz < 0.25f);
    const bool isCeiling = (nz > 0.65f);
    const bool isVertical = (std::fabs(nz) < 0.65f);

    if (isFloor) {
        // Rich wall-floor crease occlusion & deep concave corner darkening along room boundaries
        const float wallAo = 0.46f + 0.54f * SmoothStep(0.0f, 2.85f, dWall);
        const float cornerAo = 0.56f + 0.44f * SmoothStep(0.0f, 3.60f, dX + dY);
        ao *= wallAo * cornerAo;
        // Grounded plinth/column/basin proximity AO on floor vertices
        float obsAo = 1.0f;
        for (unsigned i = 0; i < gGrandGraph.obstacleCount; ++i) {
            const SolidBox &sb = gGrandGraph.obstacles[i];
            if (sb.room != rid) continue;
            const float dx = lx < sb.minX ? sb.minX - lx : (lx > sb.maxX ? lx - sb.maxX : 0.0f);
            const float dy = ly < sb.minY ? sb.minY - ly : (ly > sb.maxY ? ly - sb.maxY : 0.0f);
            const float dist = std::sqrt(dx * dx + dy * dy);
            if (dist < 2.40f) {
                const float factor = 0.48f + 0.52f * SmoothStep(0.0f, 2.40f, dist);
                if (factor < obsAo) obsAo = factor;
            }
        }
        ao *= obsAo;
    } else if (isVertical) {
        // Sculptural wall-floor crease darkening at z = 0..2.10m
        const float floorCrease = 0.44f + 0.56f * SmoothStep(0.0f, 2.10f, lz);
        ao *= floorCrease;
        // Deep concave vertical corner shadow where walls meet
        const float corner = 0.50f + 0.50f * SmoothStep(0.0f, 3.20f, dX > dY ? dX : dY);
        if (dWall < 1.15f) ao *= corner;
        // Ceiling cornice cove darkening in roofed rooms
        if (!rb.openSky && rb.height > 2.0f) {
            const float fromCeil = rb.height - lz;
            if (fromCeil < 2.40f) ao *= (0.50f + 0.50f * SmoothStep(0.0f, 2.40f, fromCeil));
        }
        // Vertical architectural falloff (warm grounded mid-low bounce, crisp sky lift higher)
        const float hNorm = rb.height > 1.0f ? Clamp01(lz / rb.height) : Clamp01(lz / 12.0f);
        lightR *= 0.88f + 0.16f * hNorm;
        lightG *= 0.90f + 0.14f * hNorm;
        lightB *= 0.92f + 0.14f * hNorm;
    } else if (isCeiling) {
        // Ceiling perimeter cove shadow
        const float edgeAo = 0.46f + 0.54f * SmoothStep(0.0f, 3.20f, dWall);
        ao *= edgeAo;
    } else if (nz < -0.65f && lz >= 0.25f) {
        // Horizontal plinth tops, ledges, and cornice caps catch crisp hemisphere skylight
        ao *= 1.06f;
    }

    // 3. Wing-specific daylight skylight pools, distance falloff & faction bounce lighting!
    if (rid == kRoomCourtyard) {
        // Golden afternoon coastal sunshine bathing the open courtyard terrace
        lightR *= 1.10f; lightG *= 1.05f; lightB *= 0.95f;
        // Deep sculpted portico overhang shadow directly in front of the South Colonnade wall
        if (ly > 16.8f && lz < 7.8f) {
            const float shade = 0.66f + 0.34f * SmoothStep(20.0f, 16.8f, ly);
            ao *= shade;
        }
    } else if (rid == kRoomAtrium) {
        // 16m x 16m open sunlit clerestory skylight over (0, 36) shifted slightly WNW by ESE sun angle
        const float sunShiftX = -0.14f * (13.5f - lz);
        const float sunShiftY =  0.11f * (13.5f - lz);
        const float sx = lx - sunShiftX;
        const float sy = ly - (36.0f + sunShiftY);
        const float boxDist = std::fmax(std::fabs(sx) - 6.6f, std::fabs(sy) - 6.6f);
        const float sunPool = 1.0f - SmoothStep(-1.8f, 5.4f, boxDist);
        // Dramatic golden rotunda daylight pool contrasting richly with cool shaded colonnade perimeter
        lightR *= 0.54f + 0.56f * sunPool;
        lightG *= 0.58f + 0.48f * sunPool;
        lightB *= 0.66f + 0.34f * sunPool;
        // Warm coastal daylight spill through the 11m South Colonnade portal
        if (ly < 26.5f && std::fabs(lx) < 7.0f) {
            const float portalWash = (1.0f - SmoothStep(20.0f, 26.5f, ly)) * (1.0f - SmoothStep(4.5f, 7.0f, std::fabs(lx)));
            lightR += 0.26f * portalWash;
            lightG += 0.22f * portalWash;
            lightB += 0.14f * portalWash;
        }
    } else if (rid == kRoomForseti) {
        // Crisp cool Forseti aerospace gallery: dramatic nave skylight pool + luminous cyan alcove bounce
        const float naveLight = 1.0f - SmoothStep(1.8f, 9.2f, std::fabs(ly - 41.0f));
        const float alcoveBounce = 1.0f - SmoothStep(0.0f, 4.6f, std::fmin(std::fabs(ly - 30.5f), std::fabs(ly - 51.5f)));
        lightR *= 0.58f + 0.30f * naveLight + 0.04f * alcoveBounce;
        lightG *= 0.66f + 0.28f * naveLight + 0.22f * alcoveBounce;
        lightB *= 0.78f + 0.24f * naveLight + 0.30f * alcoveBounce;
    } else if (rid == kRoomMuspell) {
        // High-contrast forged-iron foundry with glowing molten-amber pools around the 6 Muspell mechs
        float forgePool = 0.0f;
        const float mx[3] = {22.0f, 30.0f, 38.0f};
        for (unsigned i = 0; i < 3; ++i) {
            const float dN2 = (lx - mx[i]) * (lx - mx[i]) + (ly - 47.8f) * (ly - 47.8f);
            const float dS2 = (lx - mx[i]) * (lx - mx[i]) + (ly - 34.2f) * (ly - 34.2f);
            const float pN = 1.0f - SmoothStep(1.0f, 6.2f, std::sqrt(dN2));
            const float pS = 1.0f - SmoothStep(1.0f, 6.2f, std::sqrt(dS2));
            if (pN > forgePool) forgePool = pN;
            if (pS > forgePool) forgePool = pS;
        }
        const float wallForge = 1.0f - SmoothStep(0.0f, 4.0f, std::fmin(std::fabs(ly - 30.5f), std::fabs(ly - 51.5f)));
        const float glow = forgePool > wallForge ? forgePool : wallForge;
        lightR *= 0.48f + 0.60f * glow;
        lightG *= 0.44f + 0.36f * glow;
        lightB *= 0.44f + 0.12f * glow;
    } else if (rid == kRoomHangar) {
        // Soaring sunlit glass-truss central bay contrasting with shaded industrial eaves + pendant pools!
        const float roofDist = std::fmax(std::fabs(lx) - 8.2f, std::fmax(54.0f - ly, ly - 76.0f));
        const float skyPool = 1.0f - SmoothStep(-2.0f, 7.2f, roofDist);
        lightR *= 0.54f + 0.56f * skyPool;
        lightG *= 0.58f + 0.48f * skyPool;
        lightB *= 0.66f + 0.38f * skyPool;
        // East Naval Reflecting Pool turquoise caustic bounce
        const float px = lx < 8.8f ? 8.8f - lx : (lx > 17.4f ? lx - 17.4f : 0.0f);
        const float py = ly < 55.6f ? 55.6f - ly : (ly > 74.4f ? ly - 74.4f : 0.0f);
        const float poolGlow = 1.0f - SmoothStep(0.0f, 5.2f, std::sqrt(px * px + py * py));
        lightG += 0.22f * poolGlow;
        lightB += 0.30f * poolGlow;
        // Warm overhead spotlight wash across the North Relative Scale Alcove dais
        const float scaleDist = std::hypot(lx, ly - 75.5f);
        const float scaleSpot = 1.0f - SmoothStep(2.0f, 8.0f, scaleDist);
        lightR += 0.24f * scaleSpot;
        lightG += 0.20f * scaleSpot;
        lightB += 0.12f * scaleSpot;
    } else if (rid == kRoomConceptArt || rid == kRoomEarlyDesigns) {
        // Warm museum picture-rail gallery wall wash contrasting with shaded centre floor
        const float artWall = 1.0f - SmoothStep(0.0f, 5.2f, std::fmin(std::fabs(ly - 10.4f), std::fabs(ly - 27.6f)));
        lightR *= 0.64f + 0.40f * artWall;
        lightG *= 0.65f + 0.35f * artWall;
        lightB *= 0.70f + 0.24f * artWall;
    } else if (rid == kRoomCinema) {
        lightR *= 0.62f; lightG *= 0.64f; lightB *= 0.68f;
    }

    const float r = Clamp01(ao * lightR * 1.04f);
    const float g = Clamp01(ao * lightG * 1.04f);
    const float b = Clamp01(ao * lightB * 1.04f);
    return ModulateRgb(baseCol, r, g, b);
}

static uint32_t AddLitVertex(float x, float y, float z, float nx, float ny, float nz,
                             Material m, DWORD sourceCol) {
    if (!gPtr) return 0u;
    if (gPtr->usedVerts >= kLitCapacity) { Overflow("the vertex buffer is full"); return 0u; }
    const float ax = std::fabs(nx), ay = std::fabs(ny), az = std::fabs(nz);
    const float scale = 1.0f / kMaterials[m].repeat;
    const DWORD tint = kMaterials[m].tint;
    DWORD baseCol;
    if (!gPtr->texture[m]) baseCol = CeMuseumExhibition::Shade(sourceCol, kMaterials[m].unbakedGlow ? 100u : 76u);
    else if (tint) baseCol = tint;
    else baseCol = sourceCol;

    const DWORD bakedCol = BakeGrandVertexColour(x, y, z, nx, ny, nz, m, baseCol);
    const uint32_t idx = gPtr->usedVerts++;
    LitVertex &v = gPtr->vertices[idx];
    const float rx = x - CeMuseumWorld::kEntranceX, ry = y - CeMuseumWorld::kEntranceY;
    v.x = x; v.y = y; v.z = z;
    v.nx = nx; v.ny = ny; v.nz = nz;
    v.colour = bakedCol;
    if (az >= ax && az >= ay) { v.u = rx * scale; v.v = ry * scale; }
    else if (ax >= ay) { v.u = ry * scale; v.v = z * scale; }
    else { v.u = rx * scale; v.v = z * scale; }
    return idx;
}

static void AddLitTriangle(uint32_t i0, uint32_t i1, uint32_t i2, Material m) {
    if (!gPtr) return;
    if (gPtr->usedIndices + 3u > kIdxCapacity) { Overflow("the index buffer is full"); return; }
    const unsigned tri = gPtr->usedIndices / 3u;
    gPtr->indices[gPtr->usedIndices++] = i0;
    gPtr->indices[gPtr->usedIndices++] = i1;
    gPtr->indices[gPtr->usedIndices++] = i2;
    gPtr->material[tri] = static_cast<unsigned char>(m);
}

// Compute outward unit normal for a triangle using gPtr->hasCentre or floor-up convention.
static bool FaceNormal(const Vertex &a, const Vertex &b, const Vertex &c,
                       float *onx, float *ony, float *onz) {
    const float ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
    const float vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
    float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (length < 1e-8f) return false;
    nx /= length; ny /= length; nz /= length;
    bool flip;
    if (gPtr && gPtr->hasCentre) {
        const float cx = (a.x + b.x + c.x) / 3.0f - gPtr->centre[0];
        const float cy = (a.y + b.y + c.y) / 3.0f - gPtr->centre[1];
        const float cz = (a.z + b.z + c.z) / 3.0f - gPtr->centre[2];
        flip = nx * cx + ny * cy + nz * cz < 0.0f;
    } else {
        flip = nz > 0.0f;
    }
    if (flip) { nx = -nx; ny = -ny; nz = -nz; }
    *onx = nx; *ony = ny; *onz = nz;
    return true;
}

void Triangle(const Vertex &a, const Vertex &b, const Vertex &c) {
    if (!gPtr) return;
    const Material m = gPtr->current == kMatAuto ? ColourMaterial(a.colour) : gPtr->current;
    float nx, ny, nz;
    if (!FaceNormal(a, b, c, &nx, &ny, &nz)) return;
    const uint32_t i0 = AddLitVertex(a.x, a.y, a.z, nx, ny, nz, m, a.colour);
    const uint32_t i1 = AddLitVertex(b.x, b.y, b.z, nx, ny, nz, m, b.colour);
    const uint32_t i2 = AddLitVertex(c.x, c.y, c.z, nx, ny, nz, m, c.colour);
    if (!gPtr->overflow) AddLitTriangle(i0, i1, i2, m);
}

// Indexed quad: emits 4 shared vertices and 2 triangles (6 indices).
void Quad(Vertex a, Vertex b, Vertex c, Vertex d) {
    if (!gPtr) return;
    const Material m = gPtr->current == kMatAuto ? ColourMaterial(a.colour) : gPtr->current;
    float nx, ny, nz;
    if (!FaceNormal(a, b, c, &nx, &ny, &nz)) return;
    const uint32_t i0 = AddLitVertex(a.x, a.y, a.z, nx, ny, nz, m, a.colour);
    const uint32_t i1 = AddLitVertex(b.x, b.y, b.z, nx, ny, nz, m, b.colour);
    const uint32_t i2 = AddLitVertex(c.x, c.y, c.z, nx, ny, nz, m, c.colour);
    const uint32_t i3 = AddLitVertex(d.x, d.y, d.z, nx, ny, nz, m, d.colour);
    if (!gPtr->overflow) {
        AddLitTriangle(i0, i1, i2, m);
        AddLitTriangle(i0, i2, i3, m);
    }
}

CeHallPoint Offset(CeHallPoint p, float side, float depth) {
    if (!gPtr) return p;
    return {p.x + side * gPtr->tour.right.x + depth * gPtr->tour.forward.x,
            p.y + side * gPtr->tour.right.y + depth * gPtr->tour.forward.y};
}
Vertex Point(CeHallPoint p, float z, DWORD colour) { return {p.x, p.y, z, colour}; }

void Box(CeHallPoint p, float w, float d, float z, float h, DWORD face, DWORD edge) {
    if (!gPtr) return;
    CeHallPoint a = Offset(p,-w,-d), b = Offset(p,w,-d), c = Offset(p,w,d), e = Offset(p,-w,d);
    const float top = z - h;
    const Material was = gPtr->current;
    if (gPtr->current == kMatAuto) gPtr->current = ColourMaterial(face);
    gPtr->hasCentre = true; gPtr->centre[0] = p.x; gPtr->centre[1] = p.y; gPtr->centre[2] = z - h*0.5f;
    Quad(Point(a,top,edge),Point(b,top,edge),Point(c,top,edge),Point(e,top,edge));
    Quad(Point(a,z,face),Point(b,z,face),Point(b,top,face),Point(a,top,face));
    Quad(Point(b,z,face),Point(c,z,face),Point(c,top,face),Point(b,top,face));
    Quad(Point(c,z,face),Point(e,z,face),Point(e,top,face),Point(c,top,face));
    Quad(Point(e,z,face),Point(a,z,face),Point(a,top,face),Point(e,top,face));
    gPtr->hasCentre = false; gPtr->current = was;
}
bool FloorVertex(CeHallPoint p, float lift, DWORD colour, Vertex *v) {
    float z;
    if (!gPtr || !gPtr->ground || !gPtr->ground(p.x, p.y, &z)) return false;
    *v = Point(p, z-lift, colour); return true;
}
void Strip(CeHallPoint a, CeHallPoint b, float side, float width, float lift, DWORD colour) {
    float dx = b.x-a.x, dy = b.y-a.y, length = std::sqrt(dx*dx + dy*dy);
    if (length < 0.02f) return;
    float rx=dy/length, ry=-dx/length;
    const unsigned steps = static_cast<unsigned>(std::ceil(length / 0.9f));
    if (steps > 90) { Overflow("a floor strip is too long"); return; }
    for (unsigned i = 0; i < steps; ++i) {
        const float t = float(i)/steps, u = float(i+1)/steps;
        CeHallPoint v = {a.x+t*dx,a.y+t*dy}, w = {a.x+u*dx,a.y+u*dy};
        Vertex av,bv,cv,dv;
        if (!FloorVertex({v.x+rx*(side-width),v.y+ry*(side-width)},lift,colour,&av) ||
            !FloorVertex({v.x+rx*(side+width),v.y+ry*(side+width)},lift,colour,&bv) ||
            !FloorVertex({w.x+rx*(side+width),w.y+ry*(side+width)},lift,colour,&cv) ||
            !FloorVertex({w.x+rx*(side-width),w.y+ry*(side-width)},lift,colour,&dv)) continue;
        Quad(av,bv,cv,dv);
    }
}
void Walk(CeHallPoint a, CeHallPoint b) {
    Strip(a,b,0,0.85f,0.11f,kSlate);
    Strip(a,b,-0.71f,0.075f,0.16f,kBrass);
    Strip(a,b, 0.71f,0.075f,0.16f,kBrass);
}
void Panel(CeHallPoint p, float z, unsigned number) {
    static const float segments[][4] = {{0,0.38f,0.24f,0.055f},
        {0.21f,0.19f,0.055f,0.19f},{0.21f,-0.19f,0.055f,0.19f},
        {0,-0.38f,0.24f,0.055f},{-0.21f,-0.19f,0.055f,0.19f},
        {-0.21f,0.19f,0.055f,0.19f},{0,0,0.24f,0.055f}};
    const unsigned mask = number == 1 ? 0x06u : (number == 2 ? 0x5bu : 0x4fu);
    for (unsigned i=0;i<7;++i) if (mask & (1u<<i)) {
        const float *s=segments[i];
        Box(Offset(p,s[0],-0.17f),s[2],0.032f,z-1.1f-s[1]+s[3],2*s[3],kLight,kBrass);
    }
}
void Stand(CeHallPoint stop, unsigned number) {
    const CeHallPoint p=Offset(stop,1.65f,0.62f);
    float z;
    if (!gPtr || !gPtr->ground || !gPtr->ground(p.x,p.y,&z)) return;
    Box(p,0.8f,0.47f,z+0.12f,0.35f,kIron,kSlate);
    Box(p,0.23f,0.20f,z-0.19f,0.9f,kSlate,kEdge);
    Box(p,0.58f,0.14f,z-0.49f,1.18f,kIron,kEdge);
    Box(p,0.60f,0.16f,z-1.62f,0.13f,kBrass,kLight);
    Panel(p,z,number);
}
const DWORD kIvory = 0xffe6e9df, kCeramic = 0xfff3f2df, kGlassFrame = 0xff9ebeb8;

void FittedBox(CeHallPoint p, float halfWidth, float halfDepth, float lift, float height,
               DWORD face = kIvory, DWORD cap = kCeramic) {
    float z;
    if (gPtr && gPtr->ground && gPtr->ground(p.x,p.y,&z)) Box(p,halfWidth,halfDepth,z-lift,height,face,cap);
}

void Tile(CeHallPoint p, float width, float depth, float lift, DWORD colour) {
    Vertex a,b,c,d;
    if (FloorVertex(Offset(p,-width,-depth),lift,colour,&a) &&
        FloorVertex(Offset(p, width,-depth),lift,colour,&b) &&
        FloorVertex(Offset(p, width, depth),lift,colour,&c) &&
        FloorVertex(Offset(p,-width, depth),lift,colour,&d)) Quad(a,b,c,d);
}

void AnnexLane(CeHallPoint a, CeHallPoint b) {
    Strip(a,b,0,1.18f,0.13f,kIvory);
    Strip(a,b,-0.96f,0.07f,0.18f,kBrass);
    Strip(a,b, 0.96f,0.07f,0.18f,kBrass);
}

void ExhibitionShell() {
    using namespace CeMuseumWorld;
    const CeHallPoint origin={kEntranceX,kEntranceY};
    const float span=kBack-kFront;
    const unsigned columns=10, rows=9;
    const float tileWidth=2*kHalfWidth/columns, tileDepth=span/rows;
    for (unsigned row=0;row<rows;++row) for (unsigned col=0;col<columns;++col) {
        const CeHallPoint p=Offset(origin,-kHalfWidth+(col+0.5f)*tileWidth,kFront+(row+0.5f)*tileDepth);
        Tile(p,tileWidth*0.5f,tileDepth*0.5f,0.015f,(row+col)%2?kFloorA:kFloorB);
    }
    for (int sign=-1;sign<=1;sign+=2) {
        const float side=sign*kHalfWidth;
        auto wall=[&](float a,float b) {
            FittedBox(Offset(origin,side,(a+b)*0.5f),0.30f,(b-a)*0.5f,0,7.3f,kWall,kWallCap);
            FittedBox(Offset(origin,side-sign*0.32f,(a+b)*0.5f),0.10f,(b-a)*0.5f,0,0.45f,kReveal,kWallCap);
        };
        if (sign<0) wall(kFront,kBack);
        else { wall(kFront,CeCinemaRoom::kDoorY-CeCinemaRoom::kDoorHalf); wall(CeCinemaRoom::kDoorY+CeCinemaRoom::kDoorHalf,kBack); }
    }
    FittedBox(Offset(origin,0,kBack),kHalfWidth,0.35f,0,8.0f,kWall,kWallCap);
    FittedBox(Offset(origin,0,kFront),kHalfWidth,0.35f,0,6.4f,kWall,kWallCap);
}

// Subdivided indexed plane emitter (shared vertex grid for silky-smooth per-vertex AO!).
static void EmitIndexedHorizontalGrid(float x0, float y0, float x1, float y1, float wz,
                                      float nz, unsigned nx, unsigned ny, Material mat, DWORD col) {
    if (!gPtr || !nx || !ny || nx > 40 || ny > 40) return;
    const uint32_t base = gPtr->usedVerts;
    const unsigned stride = nx + 1u;
    for (unsigned iy = 0; iy <= ny; ++iy) {
        const float ly = y0 + (y1 - y0) * (float(iy) / float(ny));
        for (unsigned ix = 0; ix <= nx; ++ix) {
            const float lx = x0 + (x1 - x0) * (float(ix) / float(nx));
            const CeHallPoint wp = CeMuseumExhibitionHost::World({lx, ly});
            AddLitVertex(wp.x, wp.y, wz, 0.0f, 0.0f, nz, mat, col);
        }
    }
    if (gPtr->overflow) return;
    for (unsigned iy = 0; iy < ny; ++iy) {
        for (unsigned ix = 0; ix < nx; ++ix) {
            const uint32_t v00 = base + iy * stride + ix;
            const uint32_t v10 = v00 + 1u;
            const uint32_t v01 = v00 + stride;
            const uint32_t v11 = v01 + 1u;
            AddLitTriangle(v00, v10, v11, mat);
            AddLitTriangle(v00, v11, v01, mat);
        }
    }
}

void Ceiling(float floor, float x0, float y0, float x1, float y1, float height, Material material = kMatCeiling) {
    if (!gPtr) return;
    const unsigned across = unsigned(std::ceil((x1 - x0) / 3.0f));
    const unsigned along = unsigned(std::ceil((y1 - y0) / 3.0f));
    if (!across || !along || across > 40 || along > 40) {
        Overflow("a ceiling is empty or too large");
        return;
    }
    EmitIndexedHorizontalGrid(x0, y0, x1, y1, floor - height, 1.0f, across, along, material, 0xffffffff);
}

// Stateless zero-BSS view used by ce_museum_cinema.inc so all Gallery memory stays on the heap in *gPtr.
struct GalleryCinemaRef {
    struct CurrentRef {
        void operator=(Material m) const { if (gPtr) gPtr->current = m; }
    } current;
    struct GroundRef {
        bool operator!() const { return !gPtr || !gPtr->ground; }
        bool operator()(float x, float y, float *z) const { return gPtr && gPtr->ground && gPtr->ground(x, y, z); }
    } ground;
    struct ValidRef {
        operator bool() const { return gPtr && gPtr->valid; }
        bool operator!() const { return !gPtr || !gPtr->valid; }
    } valid;
    struct TourRef {
        struct ForwardRef {
            struct XRef {
                operator float() const { return gPtr ? gPtr->tour.forward.x : 0.0f; }
                float operator-() const { return -(gPtr ? gPtr->tour.forward.x : 0.0f); }
            } x;
            struct YRef {
                operator float() const { return gPtr ? gPtr->tour.forward.y : 0.0f; }
                float operator-() const { return -(gPtr ? gPtr->tour.forward.y : 0.0f); }
            } y;
        } forward;
        struct RightRef {
            struct XRef {
                operator float() const { return gPtr ? gPtr->tour.right.x : 0.0f; }
                float operator-() const { return -(gPtr ? gPtr->tour.right.x : 0.0f); }
            } x;
            struct YRef {
                operator float() const { return gPtr ? gPtr->tour.right.y : 0.0f; }
                float operator-() const { return -(gPtr ? gPtr->tour.right.y : 0.0f); }
            } y;
        } right;
    } tour;
};
static const GalleryCinemaRef g = {};

#include "ce_museum_cinema.inc"

void ExhibitRoom() {
    if (!gPtr) return;
    const CeHallTour &t=gPtr->tour;
    const CeHallPoint stop=t.stop[2], model={t.display[2].x,t.display[2].y};
    const float radius=t.display[2].radius;
    const float half=radius+1.25f, front=-radius-0.85f, back=radius+1.05f;
    AnnexLane(t.stop[1],t.annexEntry); AnnexLane(t.annexEntry,t.annexCorner);
    AnnexLane(t.annexCorner,stop);
    const unsigned columns=unsigned(std::ceil((2*half)/1.30f));
    const unsigned rows=unsigned(std::ceil((back-front+2.8f)/1.30f));
    if (!columns || !rows || columns>30 || rows>30) return;
    const float tileWidth=2*half/columns, tileDepth=(back-front+2.8f)/rows;
    for (unsigned y=0;y<rows;++y) for (unsigned x=0;x<columns;++x) {
        const CeHallPoint at=Offset(model,-half+(x+0.5f)*tileWidth,front-2.8f+(y+0.5f)*tileDepth);
        Tile(at,tileWidth/2,tileDepth/2,0.18f,(x+y)%2 ? kIvory : kCeramic);
    }
    const float backHeight=5.5f, middle=(front+back)*0.5f;
    FittedBox(Offset(model,0,back),half,0.22f,-0.10f,backHeight);
    FittedBox(Offset(model,-half,middle),0.22f,(back-front)/2,-0.10f,backHeight);
    FittedBox(Offset(model,half,middle),0.22f,(back-front)/2,-0.10f,backHeight);
}

void Entry() {
    if (!gPtr || !gPtr->ground) return;
    const CeHallPoint c=CeHallEntranceArch(gPtr->tour);
    float z;
    if (!gPtr->ground(c.x,c.y,&z)) return;
    for (int side=-1;side<=1;side+=2) {
        CeHallPoint p=Offset(c,side*kCeHallArchPostOffset,0); float foot;
        if (!gPtr->ground(p.x,p.y,&foot)) return;
        Box(p,kCeHallArchFootHalfWidth,0.66f,foot+0.2f,0.48f,kIron,kEdge);
        Box(p,0.31f,0.46f,foot-0.26f,foot-z+4.55f,kSlate,kEdge);
    }
    Box(c,2.91f,0.48f,z-4.74f,0.49f,kIron,kEdge);
}

// ---- Grand Outdoor/Indoor Multi-Wing Indexed Architectural Builders ----

// Subdivide a vertical face into shared indexed vertices with floor-crease & cornice-cove bands.
static void EmitIndexedVerticalWall(float ax, float ay, float bx, float by,
                                    float z0, float z1, float floorZ,
                                    float nx, float ny, Material mat, DWORD col) {
    if (!gPtr || !(z1 > z0)) return;
    const float hLen = std::hypot(bx - ax, by - ay);
    if (hLen < 1e-4f) return;
    unsigned segU = (hLen > 1.8f) ? static_cast<unsigned>(std::ceil(hLen / 2.0f)) : 1u;
    if (segU > 16u) segU = 16u;

    // Non-uniform vertical bands capture wall-floor crease AO and upper cornice cove AO smoothly!
    float zStops[7];
    unsigned nZ = 0;
    zStops[nZ++] = z0;
    const float h = z1 - z0;
    if (h > 3.0f) {
        if (z0 < 0.55f && z0 + 0.65f < z1 - 0.8f) zStops[nZ++] = z0 + 0.65f;
        if (z0 < 0.55f && z0 + 1.85f < z1 - 1.2f) zStops[nZ++] = z0 + 1.85f;
        if (h > 6.5f && z0 + h * 0.52f > zStops[nZ - 1] + 0.6f && z0 + h * 0.52f < z1 - 1.8f)
            zStops[nZ++] = z0 + h * 0.52f;
        if (z1 - 1.65f > zStops[nZ - 1] + 0.5f) zStops[nZ++] = z1 - 1.65f;
    } else if (h > 1.0f) {
        zStops[nZ++] = z0 + h * 0.45f;
    }
    zStops[nZ++] = z1;

    const uint32_t base = gPtr->usedVerts;
    const unsigned stride = segU + 1u;
    for (unsigned iz = 0; iz < nZ; ++iz) {
        const float wz = floorZ - zStops[iz];
        for (unsigned iu = 0; iu <= segU; ++iu) {
            const float t = float(iu) / float(segU);
            const CeHallPoint wp = CeMuseumExhibitionHost::World({ax + (bx - ax) * t, ay + (by - ay) * t});
            AddLitVertex(wp.x, wp.y, wz, nx, ny, 0.0f, mat, col);
        }
    }
    if (gPtr->overflow) return;
    for (unsigned iz = 0; iz + 1u < nZ; ++iz) {
        for (unsigned iu = 0; iu < segU; ++iu) {
            const uint32_t v00 = base + iz * stride + iu;
            const uint32_t v10 = v00 + 1u;
            const uint32_t v01 = v00 + stride;
            const uint32_t v11 = v01 + 1u;
            AddLitTriangle(v00, v10, v11, mat);
            AddLitTriangle(v00, v11, v01, mat);
        }
    }
}

void LocalBox(float x0, float y0, float z0, float x1, float y1, float z1,
              float floorZ, Material mat, DWORD col = 0xffffffff, bool includeBottom = false) {
    if (!gPtr || !(x1 > x0 && y1 > y0 && z1 > z0)) return;
    const unsigned nx = (x1 - x0) > 2.6f ? static_cast<unsigned>(std::ceil((x1 - x0) / 2.6f)) : 1u;
    const unsigned ny = (y1 - y0) > 2.6f ? static_cast<unsigned>(std::ceil((y1 - y0) / 2.6f)) : 1u;
    const unsigned cx = nx > 12u ? 12u : nx;
    const unsigned cy = ny > 12u ? 12u : ny;

    // Top face (normal upward = -Z)
    EmitIndexedHorizontalGrid(x0, y0, x1, y1, floorZ - z1, -1.0f, cx, cy, mat, col);
    // Optional soffit / underside for overhead beams, lintels, and trusses (normal downward = +Z)
    if (includeBottom || z0 > 2.2f)
        EmitIndexedHorizontalGrid(x0, y0, x1, y1, floorZ - z0, 1.0f, cx, cy, mat, col);

    // Four vertical walls with automatic floor-crease & cornice-cove vertex banding
    EmitIndexedVerticalWall(x0, y0, x1, y0, z0, z1, floorZ,  0.0f, -1.0f, mat, col); // South
    EmitIndexedVerticalWall(x1, y0, x1, y1, z0, z1, floorZ,  1.0f,  0.0f, mat, col); // East
    EmitIndexedVerticalWall(x1, y1, x0, y1, z0, z1, floorZ,  0.0f,  1.0f, mat, col); // North
    EmitIndexedVerticalWall(x0, y1, x0, y0, z0, z1, floorZ, -1.0f,  0.0f, mat, col); // West
}

void LocalFloorGrid(float x0, float y0, float x1, float y1, float lift,
                    float floorZ, Material mat, float step = 2.0f, DWORD col = 0xffffffff) {
    if (!gPtr || !(x1 > x0 && y1 > y0)) return;
    unsigned nx = static_cast<unsigned>(std::ceil((x1 - x0) / step));
    unsigned ny = static_cast<unsigned>(std::ceil((y1 - y0) / step));
    if (!nx) nx = 1u;
    if (!ny) ny = 1u;
    if (nx > 32u) nx = 32u;
    if (ny > 32u) ny = 32u;
    EmitIndexedHorizontalGrid(x0, y0, x1, y1, floorZ - lift, -1.0f, nx, ny, mat, col);
}

// Regular 8-sided sculpted architectural tier (for Grand Atrium Rotunda Pedestal & Hero Monument).
static void LocalOctagonTier(float cx, float cy, float radius, float z0, float z1,
                             float floorZ, Material mat, DWORD col = 0xffffffff, bool drawTop = true) {
    if (!gPtr || !(radius > 0.05f && z1 > z0)) return;
    const unsigned sides = 8u;
    const float kPi8 = 3.14159265f / 8.0f;
    const float kStep = 6.283185307f / float(sides);
    float px[8], py[8];
    for (unsigned i = 0; i < sides; ++i) {
        const float a = kPi8 + float(i) * kStep;
        px[i] = cx + std::cos(a) * radius;
        py[i] = cy + std::sin(a) * radius;
    }
    if (drawTop) {
        const CeHallPoint wc = CeMuseumExhibitionHost::World({cx, cy});
        const uint32_t centerIdx = AddLitVertex(wc.x, wc.y, floorZ - z1, 0.0f, 0.0f, -1.0f, mat, col);
        uint32_t ring[8];
        for (unsigned i = 0; i < sides; ++i) {
            const CeHallPoint wp = CeMuseumExhibitionHost::World({px[i], py[i]});
            ring[i] = AddLitVertex(wp.x, wp.y, floorZ - z1, 0.0f, 0.0f, -1.0f, mat, col);
        }
        if (!gPtr->overflow) {
            for (unsigned i = 0; i < sides; ++i)
                AddLitTriangle(centerIdx, ring[i], ring[(i + 1u) % sides], mat);
        }
    }
    for (unsigned i = 0; i < sides; ++i) {
        const unsigned j = (i + 1u) % sides;
        const float mx = 0.5f * (px[i] + px[j]) - cx;
        const float my = 0.5f * (py[i] + py[j]) - cy;
        const float ml = std::hypot(mx, my);
        const float nx = ml > 1e-5f ? mx / ml : 1.0f;
        const float ny = ml > 1e-5f ? my / ml : 0.0f;
        EmitIndexedVerticalWall(px[i], py[i], px[j], py[j], z0, z1, floorZ, nx, ny, mat, col);
    }
}

// Sculpted 3D angled/swept structural rib or buttress prism (extruded along X or Y).
// p0..p3 define a 4-point convex profile in (along-room-depth, height) extruded by width [a0..a1].
static void EmitSweptRibX(float x0, float x1, const float yPts[4], const float zPts[4],
                          float floorZ, Material mat, DWORD col) {
    if (!gPtr || !(x1 > x0)) return;
    // Two end caps (normal -X and +X)
    const CeHallPoint w0 = CeMuseumExhibitionHost::World({x0, yPts[0]});
    const CeHallPoint w1 = CeMuseumExhibitionHost::World({x0, yPts[1]});
    const CeHallPoint w2 = CeMuseumExhibitionHost::World({x0, yPts[2]});
    const CeHallPoint w3 = CeMuseumExhibitionHost::World({x0, yPts[3]});
    const uint32_t l0 = AddLitVertex(w0.x, w0.y, floorZ - zPts[0], -1.0f, 0.0f, 0.0f, mat, col);
    const uint32_t l1 = AddLitVertex(w1.x, w1.y, floorZ - zPts[1], -1.0f, 0.0f, 0.0f, mat, col);
    const uint32_t l2 = AddLitVertex(w2.x, w2.y, floorZ - zPts[2], -1.0f, 0.0f, 0.0f, mat, col);
    const uint32_t l3 = AddLitVertex(w3.x, w3.y, floorZ - zPts[3], -1.0f, 0.0f, 0.0f, mat, col);
    if (!gPtr->overflow) { AddLitTriangle(l0, l1, l2, mat); AddLitTriangle(l0, l2, l3, mat); }

    const CeHallPoint r0 = CeMuseumExhibitionHost::World({x1, yPts[0]});
    const CeHallPoint r1 = CeMuseumExhibitionHost::World({x1, yPts[1]});
    const CeHallPoint r2 = CeMuseumExhibitionHost::World({x1, yPts[2]});
    const CeHallPoint r3 = CeMuseumExhibitionHost::World({x1, yPts[3]});
    const uint32_t k0 = AddLitVertex(r0.x, r0.y, floorZ - zPts[0],  1.0f, 0.0f, 0.0f, mat, col);
    const uint32_t k1 = AddLitVertex(r1.x, r1.y, floorZ - zPts[1],  1.0f, 0.0f, 0.0f, mat, col);
    const uint32_t k2 = AddLitVertex(r2.x, r2.y, floorZ - zPts[2],  1.0f, 0.0f, 0.0f, mat, col);
    const uint32_t k3 = AddLitVertex(r3.x, r3.y, floorZ - zPts[3],  1.0f, 0.0f, 0.0f, mat, col);
    if (!gPtr->overflow) { AddLitTriangle(k0, k2, k1, mat); AddLitTriangle(k0, k3, k2, mat); }

    // Four perimeter side quads with outward normals in YZ
    const float cy = 0.25f * (yPts[0] + yPts[1] + yPts[2] + yPts[3]);
    const float cz = 0.25f * (zPts[0] + zPts[1] + zPts[2] + zPts[3]);
    for (unsigned e = 0; e < 4u; ++e) {
        const unsigned f = (e + 1u) % 4u;
        const float dy = yPts[f] - yPts[e];
        const float dz = zPts[f] - zPts[e];
        const float len = std::hypot(dy, dz);
        if (len < 1e-4f) continue;
        float ny = dz / len;
        float nzUp = -dy / len;
        const float my = 0.5f * (yPts[e] + yPts[f]) - cy;
        const float mz = 0.5f * (zPts[e] + zPts[f]) - cz;
        if (ny * my + nzUp * mz < 0.0f) { ny = -ny; nzUp = -nzUp; }
        const float nzNative = -nzUp;
        const CeHallPoint a0 = CeMuseumExhibitionHost::World({x0, yPts[e]});
        const CeHallPoint a1 = CeMuseumExhibitionHost::World({x1, yPts[e]});
        const CeHallPoint b0 = CeMuseumExhibitionHost::World({x0, yPts[f]});
        const CeHallPoint b1 = CeMuseumExhibitionHost::World({x1, yPts[f]});
        const uint32_t v0 = AddLitVertex(a0.x, a0.y, floorZ - zPts[e], 0.0f, ny, nzNative, mat, col);
        const uint32_t v1 = AddLitVertex(a1.x, a1.y, floorZ - zPts[e], 0.0f, ny, nzNative, mat, col);
        const uint32_t v2 = AddLitVertex(b1.x, b1.y, floorZ - zPts[f], 0.0f, ny, nzNative, mat, col);
        const uint32_t v3 = AddLitVertex(b0.x, b0.y, floorZ - zPts[f], 0.0f, ny, nzNative, mat, col);
        if (!gPtr->overflow) { AddLitTriangle(v0, v1, v2, mat); AddLitTriangle(v0, v2, v3, mat); }
    }
}

// Sculpted 3D angled/swept structural rib extruded along Y (profile in XZ — for Hangar vault arches!).
static void EmitSweptRibY(float y0, float y1, const float xPts[4], const float zPts[4],
                          float floorZ, Material mat, DWORD col) {
    if (!gPtr || !(y1 > y0)) return;
    const CeHallPoint s0 = CeMuseumExhibitionHost::World({xPts[0], y0});
    const CeHallPoint s1 = CeMuseumExhibitionHost::World({xPts[1], y0});
    const CeHallPoint s2 = CeMuseumExhibitionHost::World({xPts[2], y0});
    const CeHallPoint s3 = CeMuseumExhibitionHost::World({xPts[3], y0});
    const uint32_t l0 = AddLitVertex(s0.x, s0.y, floorZ - zPts[0], 0.0f, -1.0f, 0.0f, mat, col);
    const uint32_t l1 = AddLitVertex(s1.x, s1.y, floorZ - zPts[1], 0.0f, -1.0f, 0.0f, mat, col);
    const uint32_t l2 = AddLitVertex(s2.x, s2.y, floorZ - zPts[2], 0.0f, -1.0f, 0.0f, mat, col);
    const uint32_t l3 = AddLitVertex(s3.x, s3.y, floorZ - zPts[3], 0.0f, -1.0f, 0.0f, mat, col);
    if (!gPtr->overflow) { AddLitTriangle(l0, l1, l2, mat); AddLitTriangle(l0, l2, l3, mat); }

    const CeHallPoint n0 = CeMuseumExhibitionHost::World({xPts[0], y1});
    const CeHallPoint n1 = CeMuseumExhibitionHost::World({xPts[1], y1});
    const CeHallPoint n2 = CeMuseumExhibitionHost::World({xPts[2], y1});
    const CeHallPoint n3 = CeMuseumExhibitionHost::World({xPts[3], y1});
    const uint32_t k0 = AddLitVertex(n0.x, n0.y, floorZ - zPts[0], 0.0f,  1.0f, 0.0f, mat, col);
    const uint32_t k1 = AddLitVertex(n1.x, n1.y, floorZ - zPts[1], 0.0f,  1.0f, 0.0f, mat, col);
    const uint32_t k2 = AddLitVertex(n2.x, n2.y, floorZ - zPts[2], 0.0f,  1.0f, 0.0f, mat, col);
    const uint32_t k3 = AddLitVertex(n3.x, n3.y, floorZ - zPts[3], 0.0f,  1.0f, 0.0f, mat, col);
    if (!gPtr->overflow) { AddLitTriangle(k0, k2, k1, mat); AddLitTriangle(k0, k3, k2, mat); }

    const float cx = 0.25f * (xPts[0] + xPts[1] + xPts[2] + xPts[3]);
    const float cz = 0.25f * (zPts[0] + zPts[1] + zPts[2] + zPts[3]);
    for (unsigned e = 0; e < 4u; ++e) {
        const unsigned f = (e + 1u) % 4u;
        const float dx = xPts[f] - xPts[e];
        const float dz = zPts[f] - zPts[e];
        const float len = std::hypot(dx, dz);
        if (len < 1e-4f) continue;
        float nx = dz / len;
        float nzUp = -dx / len;
        const float mx = 0.5f * (xPts[e] + xPts[f]) - cx;
        const float mz = 0.5f * (zPts[e] + zPts[f]) - cz;
        if (nx * mx + nzUp * mz < 0.0f) { nx = -nx; nzUp = -nzUp; }
        const float nzNative = -nzUp;
        const CeHallPoint a0 = CeMuseumExhibitionHost::World({xPts[e], y0});
        const CeHallPoint a1 = CeMuseumExhibitionHost::World({xPts[e], y1});
        const CeHallPoint b0 = CeMuseumExhibitionHost::World({xPts[f], y0});
        const CeHallPoint b1 = CeMuseumExhibitionHost::World({xPts[f], y1});
        const uint32_t v0 = AddLitVertex(a0.x, a0.y, floorZ - zPts[e], nx, 0.0f, nzNative, mat, col);
        const uint32_t v1 = AddLitVertex(a1.x, a1.y, floorZ - zPts[e], nx, 0.0f, nzNative, mat, col);
        const uint32_t v2 = AddLitVertex(b1.x, b1.y, floorZ - zPts[f], nx, 0.0f, nzNative, mat, col);
        const uint32_t v3 = AddLitVertex(b0.x, b0.y, floorZ - zPts[f], nx, 0.0f, nzNative, mat, col);
        if (!gPtr->overflow) { AddLitTriangle(v0, v1, v2, mat); AddLitTriangle(v0, v2, v3, mat); }
    }
}

// Flat walkable floor inlay quad (for angled Hangar flight-deck chevrons & tactical markings).
static void EmitFloorQuad(float ax, float ay, float bx, float by,
                          float cx, float cy, float dx, float dy,
                          float lift, float floorZ, Material mat, DWORD col) {
    if (!gPtr) return;
    const float wz = floorZ - lift;
    const CeHallPoint wa = CeMuseumExhibitionHost::World({ax, ay});
    const CeHallPoint wb = CeMuseumExhibitionHost::World({bx, by});
    const CeHallPoint wc = CeMuseumExhibitionHost::World({cx, cy});
    const CeHallPoint wd = CeMuseumExhibitionHost::World({dx, dy});
    const uint32_t i0 = AddLitVertex(wa.x, wa.y, wz, 0.0f, 0.0f, -1.0f, mat, col);
    const uint32_t i1 = AddLitVertex(wb.x, wb.y, wz, 0.0f, 0.0f, -1.0f, mat, col);
    const uint32_t i2 = AddLitVertex(wc.x, wc.y, wz, 0.0f, 0.0f, -1.0f, mat, col);
    const uint32_t i3 = AddLitVertex(wd.x, wd.y, wz, 0.0f, 0.0f, -1.0f, mat, col);
    if (!gPtr->overflow) {
        AddLitTriangle(i0, i1, i2, mat);
        AddLitTriangle(i0, i2, i3, mat);
    }
}

// Alpha-blended radial soft contact-shadow decal grounded beneath plinths, columns, and monuments.
// Uses a deeper umbra + wider sunlit-directional penumbra so every fixture reads solidly grounded.
static void AddContactShadow(float cx, float cy, float hx, float hy, float spread,
                             float floorZ, float lift = 0.042f, float maxAlpha = 0.72f) {
    if (!gPtr) return;
    // Boost legacy call-site alpha & spread cleanly while keeping subtle feathering
    const float effAlpha = Clamp01(maxAlpha * 1.52f);
    const float effSpread = spread * 1.38f;
    const float effLift = (lift < 0.042f) ? 0.042f : lift;
    // 16-segment rounded-rectangle / superellipse 2-ring soft halo (umbra -> penumbra -> zero alpha).
    const unsigned segs = 16u;
    const unsigned needVerts = segs * 3u;
    const unsigned needIdx = segs * 12u;
    if (gPtr->shadowVertCount + needVerts > kShadowVertCap ||
        gPtr->shadowIdxCount + needIdx > kShadowIdxCap) return;

    const uint16_t base = static_cast<uint16_t>(gPtr->shadowVertCount);
    const float wz = floorZ - effLift;
    const DWORD cInner = (static_cast<DWORD>(Clamp01(effAlpha) * 255.0f + 0.5f) << 24) | 0x00070b12u;
    const DWORD cMid   = (static_cast<DWORD>(Clamp01(effAlpha * 0.48f) * 255.0f + 0.5f) << 24) | 0x000a1019u;
    const DWORD cOuter = 0x000d131eu;

    // Subtle directional sun-cast offset opposite kSunDir (WNW) so grounded shadows match golden sunlight!
    const float shiftX = -kSunDirX * (effSpread * 0.28f);
    const float shiftY = -kSunDirY * (effSpread * 0.28f);

    auto shapePt = [&](float angle, float rx, float ry, float oxShift, float oyShift, float *ox, float *oy) {
        const float ca = std::cos(angle), sa = std::sin(angle);
        // Smooth superellipse (power 0.62) hugs rectangular plinth corners cleanly without sharp boxy artifacts
        const float sx = (ca < 0.0f ? -1.0f : 1.0f) * std::pow(std::fabs(ca), 0.62f);
        const float sy = (sa < 0.0f ? -1.0f : 1.0f) * std::pow(std::fabs(sa), 0.62f);
        *ox = cx + oxShift + sx * rx;
        *oy = cy + oyShift + sy * ry;
    };

    const float r0x = hx * 0.96f, r0y = hy * 0.96f;
    const float r1x = hx + effSpread * 0.42f, r1y = hy + effSpread * 0.42f;
    const float r2x = hx + effSpread, r2y = hy + effSpread;
    for (unsigned i = 0; i < segs; ++i) {
        const float a = float(i) * (6.283185307f / float(segs));
        float x0, y0, x1, y1, x2, y2;
        shapePt(a, r0x, r0y, 0.0f, 0.0f, &x0, &y0);
        shapePt(a, r1x, r1y, shiftX * 0.55f, shiftY * 0.55f, &x1, &y1);
        shapePt(a, r2x, r2y, shiftX, shiftY, &x2, &y2);
        const CeHallPoint w0 = CeMuseumExhibitionHost::World({x0, y0});
        const CeHallPoint w1 = CeMuseumExhibitionHost::World({x1, y1});
        const CeHallPoint w2 = CeMuseumExhibitionHost::World({x2, y2});
        gPtr->shadowVerts[gPtr->shadowVertCount++] = {w0.x, w0.y, wz, cInner};
        gPtr->shadowVerts[gPtr->shadowVertCount++] = {w1.x, w1.y, wz, cMid};
        gPtr->shadowVerts[gPtr->shadowVertCount++] = {w2.x, w2.y, wz, cOuter};
    }
    for (unsigned i = 0; i < segs; ++i) {
        const unsigned j = (i + 1u) % segs;
        const uint16_t a0 = static_cast<uint16_t>(base + i * 3u + 0u);
        const uint16_t a1 = static_cast<uint16_t>(base + i * 3u + 1u);
        const uint16_t a2 = static_cast<uint16_t>(base + i * 3u + 2u);
        const uint16_t b0 = static_cast<uint16_t>(base + j * 3u + 0u);
        const uint16_t b1 = static_cast<uint16_t>(base + j * 3u + 1u);
        const uint16_t b2 = static_cast<uint16_t>(base + j * 3u + 2u);
        // Inner umbra -> mid penumbra
        gPtr->shadowIndices[gPtr->shadowIdxCount++] = a0;
        gPtr->shadowIndices[gPtr->shadowIdxCount++] = a1;
        gPtr->shadowIndices[gPtr->shadowIdxCount++] = b1;
        gPtr->shadowIndices[gPtr->shadowIdxCount++] = a0;
        gPtr->shadowIndices[gPtr->shadowIdxCount++] = b1;
        gPtr->shadowIndices[gPtr->shadowIdxCount++] = b0;
        // Mid penumbra -> outer feather
        gPtr->shadowIndices[gPtr->shadowIdxCount++] = a1;
        gPtr->shadowIndices[gPtr->shadowIdxCount++] = a2;
        gPtr->shadowIndices[gPtr->shadowIdxCount++] = b2;
        gPtr->shadowIndices[gPtr->shadowIdxCount++] = a1;
        gPtr->shadowIndices[gPtr->shadowIdxCount++] = b2;
        gPtr->shadowIndices[gPtr->shadowIdxCount++] = b1;
    }
}

// Translucent sunlit architectural skylight glass pane or subtle atmospheric light shaft quad.
static void AddGlassQuad(float ax, float ay, float az, DWORD ca,
                         float bx, float by, float bz, DWORD cb,
                         float cx, float cy, float cz, DWORD cc,
                         float dx, float dy, float dz, DWORD cd, float floorZ) {
    if (!gPtr) return;
    if (gPtr->glassVertCount + 4u > kGlassVertCap || gPtr->glassIdxCount + 6u > kGlassIdxCap) return;
    const uint16_t base = static_cast<uint16_t>(gPtr->glassVertCount);
    const CeHallPoint wa = CeMuseumExhibitionHost::World({ax, ay});
    const CeHallPoint wb = CeMuseumExhibitionHost::World({bx, by});
    const CeHallPoint wc = CeMuseumExhibitionHost::World({cx, cy});
    const CeHallPoint wd = CeMuseumExhibitionHost::World({dx, dy});
    gPtr->glassVerts[gPtr->glassVertCount++] = {wa.x, wa.y, floorZ - az, ca};
    gPtr->glassVerts[gPtr->glassVertCount++] = {wb.x, wb.y, floorZ - bz, cb};
    gPtr->glassVerts[gPtr->glassVertCount++] = {wc.x, wc.y, floorZ - cz, cc};
    gPtr->glassVerts[gPtr->glassVertCount++] = {wd.x, wd.y, floorZ - dz, cd};
    gPtr->glassIndices[gPtr->glassIdxCount++] = base + 0u;
    gPtr->glassIndices[gPtr->glassIdxCount++] = base + 1u;
    gPtr->glassIndices[gPtr->glassIdxCount++] = base + 2u;
    gPtr->glassIndices[gPtr->glassIdxCount++] = base + 0u;
    gPtr->glassIndices[gPtr->glassIdxCount++] = base + 2u;
    gPtr->glassIndices[gPtr->glassIdxCount++] = base + 3u;
}

void DressedColumn(float cx, float cy, float half, float height, float floorZ,
                   Material shaftMat = kMatPier, Material trimMat = kMatTrim) {
    AddContactShadow(cx, cy, half + 0.14f, half + 0.14f, 0.78f, floorZ, 0.036f, 0.46f);
    LocalBox(cx - half - 0.16f, cy - half - 0.16f, 0.02f, cx + half + 0.16f, cy + half + 0.16f, 0.10f, floorZ, kMatPierLine, kIron);
    LocalBox(cx - half - 0.11f, cy - half - 0.11f, 0.10f, cx + half + 0.11f, cy + half + 0.11f, 0.66f, floorZ, kMatPierBase, kSlate);
    LocalBox(cx - half - 0.13f, cy - half - 0.13f, 0.62f, cx + half + 0.13f, cy + half + 0.13f, 0.70f, floorZ, trimMat, kBrass);
    LocalBox(cx - half, cy - half, 0.70f, cx + half, cy + half, height - 0.50f, floorZ, shaftMat, kSlate);
    LocalBox(cx - half - 0.08f, cy - half - 0.08f, height - 0.62f, cx + half + 0.08f, cy + half + 0.08f, height - 0.48f, floorZ, trimMat, kBrass);
    LocalBox(cx - half - 0.15f, cy - half - 0.15f, height - 0.48f, cx + half + 0.15f, cy + half + 0.15f, height, floorZ, kMatPierCap, kIvory);
}

void RunnerStripX(float x0, float x1, float cy, float halfW, float floorZ) {
    LocalFloorGrid(x0, cy - halfW, x1, cy + halfW, 0.055f, floorZ, kMatInlay, 2.5f, 0xffd0d6dc);
    LocalFloorGrid(x0, cy - halfW - 0.11f, x1, cy - halfW, 0.066f, floorZ, kMatTrim, 3.0f, kBrass);
    LocalFloorGrid(x0, cy + halfW, x1, cy + halfW + 0.11f, 0.066f, floorZ, kMatTrim, 3.0f, kBrass);
}

void RunnerStripY(float cx, float y0, float y1, float halfW, float floorZ) {
    LocalFloorGrid(cx - halfW, y0, cx + halfW, y1, 0.055f, floorZ, kMatInlay, 2.5f, 0xffd0d6dc);
    LocalFloorGrid(cx - halfW - 0.11f, y0, cx - halfW, y1, 0.066f, floorZ, kMatTrim, 3.0f, kBrass);
    LocalFloorGrid(cx + halfW, y0, cx + halfW + 0.11f, y1, 0.066f, floorZ, kMatTrim, 3.0f, kBrass);
}

void TurntablePlinth(float cx, float cy, float half, float topZ, float floorZ,
                     bool tiered = false, Material accentGlow = kMatCount, Material bodyMat = kMatPlinth) {
    AddContactShadow(cx, cy, half, half, tiered ? 1.55f : 1.05f, floorZ, 0.036f, tiered ? 0.54f : 0.48f);
    if (tiered) {
        // Monumental Courtyard Hero Dais: ceremonial octagonal mosaic inlay + 4-step stone/brass plinth + corner lantern bollards!
        LocalFloorGrid(cx - half - 1.15f, cy - half - 1.15f, cx + half + 1.15f, cy + half + 1.15f, 0.044f, floorZ, kMatInlay, 2.0f, 0xffc8d0d8);
        LocalFloorGrid(cx - half - 1.28f, cy - half - 1.28f, cx + half + 1.28f, cy - half - 1.15f, 0.054f, floorZ, kMatTrim, 2.5f, kBrass);
        LocalFloorGrid(cx - half - 1.28f, cy + half + 1.15f, cx + half + 1.28f, cy + half + 1.28f, 0.054f, floorZ, kMatTrim, 2.5f, kBrass);
        LocalFloorGrid(cx - half - 1.28f, cy - half - 1.15f, cx - half - 1.15f, cy + half + 1.15f, 0.054f, floorZ, kMatTrim, 2.5f, kBrass);
        LocalFloorGrid(cx + half + 1.15f, cy - half - 1.15f, cx + half + 1.28f, cy + half + 1.15f, 0.054f, floorZ, kMatTrim, 2.5f, kBrass);

        LocalBox(cx - half, cy - half, 0.02f, cx + half, cy + half, 0.34f, floorZ, kMatPierBase, kSlate);
        LocalBox(cx - half * 0.94f, cy - half * 0.94f, 0.34f, cx + half * 0.94f, cy + half * 0.94f, 0.72f, floorZ, kMatWall, 0xfff0ece2);
        LocalBox(cx - half * 0.96f, cy - half * 0.96f, 0.68f, cx + half * 0.96f, cy + half * 0.96f, 0.76f, floorZ, kMatTrim, kBrass);
        LocalOctagonTier(cx, cy, half * 0.92f, 0.76f, topZ - 0.14f, floorZ, bodyMat, 0xffe4eaee, false);
        LocalOctagonTier(cx, cy, half * 0.95f, topZ - 0.18f, topZ - 0.10f, floorZ, kMatWarmGlow, kLight, false);
        LocalOctagonTier(cx, cy, half * 0.97f, topZ - 0.10f, topZ - 0.02f, floorZ, kMatTrim, kBrass, false);
        LocalOctagonTier(cx, cy, half * 0.90f, topZ - 0.06f, topZ, floorZ, kMatPlinthTop, kSlate, true);
        // Sculpted corner memorial bollards on the four corners of the hero dais
        for (int sx = -1; sx <= 1; sx += 2) {
            for (int sy = -1; sy <= 1; sy += 2) {
                const float bx = cx + float(sx) * (half - 0.28f);
                const float by = cy + float(sy) * (half - 0.28f);
                LocalBox(bx - 0.22f, by - 0.22f, 0.34f, bx + 0.22f, by + 0.22f, 1.16f, floorZ, kMatPier, kSlate);
                LocalBox(bx - 0.25f, by - 0.25f, 1.16f, bx + 0.25f, by + 0.25f, 1.26f, floorZ, kMatTrim, kBrass);
                LocalBox(bx - 0.14f, by - 0.14f, 1.26f, bx + 0.14f, by + 0.14f, 1.44f, floorZ, kMatWarmGlow, kLight);
                LocalBox(bx - 0.20f, by - 0.20f, 1.44f, bx + 0.20f, by + 0.20f, 1.52f, floorZ, kMatTrim, kBrass);
            }
        }
        return;
    }
    LocalBox(cx - half, cy - half, 0.02f, cx + half, cy + half, 0.16f, floorZ, kMatPierBase, kSlate);
    if (accentGlow != kMatCount) {
        // Subtle glowing architectural base reveal band
        LocalBox(cx - half * 0.96f, cy - half * 0.96f, 0.16f, cx + half * 0.96f, cy + half * 0.96f, 0.23f, floorZ, accentGlow, kLight);
        LocalBox(cx - half * 0.93f, cy - half * 0.93f, 0.23f, cx + half * 0.93f, cy + half * 0.93f, topZ - 0.08f, floorZ, bodyMat, 0xffdde2e6);
    } else {
        LocalBox(cx - half * 0.94f, cy - half * 0.94f, 0.16f, cx + half * 0.94f, cy + half * 0.94f, topZ - 0.08f, floorZ, bodyMat, 0xffdde2e6);
    }
    LocalBox(cx - half * 0.98f, cy - half * 0.98f, topZ - 0.11f, cx + half * 0.98f, cy + half * 0.98f, topZ - 0.03f, floorZ, kMatTrim, kBrass);
    LocalBox(cx - half * 0.91f, cy - half * 0.91f, topZ - 0.06f, cx + half * 0.91f, cy + half * 0.91f, topZ, floorZ, kMatPlinthTop, kSlate);
}

void AudioKioskPedestal(float cx, float cy, float floorZ) {
    AddContactShadow(cx, cy, 0.44f, 0.36f, 0.65f, floorZ, 0.036f, 0.45f);
    LocalBox(cx - 0.44f, cy - 0.36f, 0.02f, cx + 0.44f, cy + 0.36f, 0.16f, floorZ, kMatPierBase, kSlate);
    LocalBox(cx - 0.32f, cy - 0.24f, 0.16f, cx + 0.32f, cy + 0.24f, 1.08f, floorZ, kMatStand, kSlate);
    LocalBox(cx - 0.42f, cy - 0.32f, 1.08f, cx + 0.42f, cy + 0.32f, 1.24f, floorZ, kMatTrim, kBrass);
    LocalBox(cx - 0.28f, cy - 0.20f, 1.24f, cx + 0.28f, cy + 0.20f, 1.32f, floorZ, kMatWarmGlow, kLight);
}

void PortalFrameX(float y, float x0, float x1, float clearH, float wallH, float floorZ,
                  Material accentMat, Material glowMat = kMatWarmGlow) {
    const float w = CeMuseumExhibition::kWallHalf;
    LocalBox(x0, y - w, clearH, x1, y + w, wallH, floorZ, kMatHeader, 0xffede9e0, true);
    LocalBox(x0, y - w - 0.08f, clearH, x1, y + w + 0.08f, clearH + 0.46f, floorZ, accentMat, kBrass, true);
    LocalBox(x0 + 0.3f, y - w - 0.11f, clearH + 0.12f, x1 - 0.3f, y + w + 0.11f, clearH + 0.24f, floorZ, glowMat, kLight);
    DressedColumn(x0 - 0.42f, y, 0.44f, wallH, floorZ);
    DressedColumn(x1 + 0.42f, y, 0.44f, wallH, floorZ);
}

void PortalFrameY(float x, float y0, float y1, float clearH, float wallH, float floorZ,
                  Material accentMat, Material glowMat = kMatWarmGlow) {
    const float w = CeMuseumExhibition::kWallHalf;
    LocalBox(x - w, y0, clearH, x + w, y1, wallH, floorZ, kMatHeader, 0xffede9e0, true);
    LocalBox(x - w - 0.08f, y0, clearH, x + w + 0.08f, y1, clearH + 0.46f, floorZ, accentMat, kBrass, true);
    LocalBox(x - w - 0.11f, y0 + 0.3f, clearH + 0.12f, x + w + 0.11f, y1 - 0.3f, clearH + 0.24f, floorZ, glowMat, kLight);
    DressedColumn(x, y0 - 0.42f, 0.44f, wallH, floorZ);
    DressedColumn(x, y1 + 0.42f, 0.44f, wallH, floorZ);
}

void BuildGrandComplex(float floorZ) {
    using namespace CeMuseumExhibition::Grand;
    const float w = CeMuseumExhibition::kWallHalf;

    // =========================================================================
    // 1. COASTAL COURTYARD (x [-15.5, 15.5], y [-6.0, 20.0], OPEN SUNLIT SKY)
    // =========================================================================
    LocalFloorGrid(-15.5f, -6.0f, 15.5f, 20.0f, 0.02f, floorZ, kMatCourtyard, 2.0f);
    // Coastal limestone cliff retaining apron framing the elevated seaside terrace
    LocalBox(-16.4f, -6.9f, -10.5f, 16.4f, -5.8f, 0.02f, floorZ, kMatPierBase, kSlate);
    LocalBox(-16.4f, -6.9f, -10.5f, -15.3f, 20.2f, 0.02f, floorZ, kMatPierBase, kSlate);
    LocalBox( 15.3f, -6.9f, -10.5f,  16.4f, 20.2f, 0.02f, floorZ, kMatPierBase, kSlate);

    // Ceremonial processional stone spine + perimeter loop around Hero Aquila dais
    RunnerStripY(0.0f, -5.2f, 5.10f, 1.35f, floorZ);
    RunnerStripX(-5.55f, 5.55f, 6.20f, 1.00f, floorZ);
    RunnerStripY(-4.60f, 7.30f, 13.70f, 0.90f, floorZ);
    RunnerStripY( 4.60f, 7.30f, 13.70f, 0.90f, floorZ);
    RunnerStripX(-5.55f, 5.55f, 14.80f, 1.00f, floorZ);
    RunnerStripY(0.0f, 15.90f, 20.00f, 1.55f, floorZ);

    // Open coastal stone balustrade (rhythmic balusters + warm brushed-brass top rail + lantern piers -> unobstructed ocean vista!)
    auto balustradeY = [&](float x, float y0, float y1) {
        LocalBox(x - 0.28f, y0, 0.02f, x + 0.28f, y1, 0.42f, floorZ, kMatWall, 0xffeae4d8);
        LocalBox(x - 0.33f, y0, 0.42f, x + 0.33f, y1, 0.52f, floorZ, kMatPierCap, 0xfff6f2e8);
        LocalBox(x - 0.12f, y0, 1.04f, x + 0.12f, y1, 1.16f, floorZ, kMatTrim, kBrass, true);
        for (float y = y0 + 0.85f; y < y1 - 0.4f; y += 0.85f) {
            LocalBox(x - 0.08f, y - 0.08f, 0.52f, x + 0.08f, y + 0.08f, 1.04f, floorZ, kMatPierCap, 0xfff2eee5);
        }
        for (float y = y0; y <= y1 + 0.1f; y += 4.8f) {
            AddContactShadow(x, y, 0.38f, 0.32f, 0.55f, floorZ, 0.036f, 0.38f);
            LocalBox(x - 0.34f, y - 0.26f, 0.02f, x + 0.34f, y + 0.26f, 1.24f, floorZ, kMatPier, kSlate);
            LocalBox(x - 0.38f, y - 0.30f, 1.24f, x + 0.38f, y + 0.30f, 1.36f, floorZ, kMatPierCap, 0xfff5f1e7);
            LocalBox(x - 0.15f, y - 0.15f, 1.36f, x + 0.15f, y + 0.15f, 1.56f, floorZ, kMatWarmGlow, kLight);
            LocalBox(x - 0.22f, y - 0.22f, 1.56f, x + 0.22f, y + 0.22f, 1.64f, floorZ, kMatTrim, kBrass);
        }
    };
    auto balustradeX = [&](float y, float x0, float x1) {
        LocalBox(x0, y - 0.28f, 0.02f, x1, y + 0.28f, 0.42f, floorZ, kMatWall, 0xffeae4d8);
        LocalBox(x0, y - 0.33f, 0.42f, x1, y + 0.33f, 0.52f, floorZ, kMatPierCap, 0xfff6f2e8);
        LocalBox(x0, y - 0.12f, 1.04f, x1, y + 0.12f, 1.16f, floorZ, kMatTrim, kBrass, true);
        for (float x = x0 + 0.85f; x < x1 - 0.4f; x += 0.85f) {
            LocalBox(x - 0.08f, y - 0.08f, 0.52f, x + 0.08f, y + 0.08f, 1.04f, floorZ, kMatPierCap, 0xfff2eee5);
        }
        for (float x = x0; x <= x1 + 0.1f; x += 4.0f) {
            AddContactShadow(x, y, 0.32f, 0.38f, 0.55f, floorZ, 0.036f, 0.38f);
            LocalBox(x - 0.26f, y - 0.34f, 0.02f, x + 0.26f, y + 0.34f, 1.24f, floorZ, kMatPier, kSlate);
            LocalBox(x - 0.30f, y - 0.38f, 1.24f, x + 0.30f, y + 0.38f, 1.36f, floorZ, kMatPierCap, 0xfff5f1e7);
            LocalBox(x - 0.15f, y - 0.15f, 1.36f, x + 0.15f, y + 0.15f, 1.56f, floorZ, kMatWarmGlow, kLight);
            LocalBox(x - 0.22f, y - 0.22f, 1.56f, x + 0.22f, y + 0.22f, 1.64f, floorZ, kMatTrim, kBrass);
        }
    };
    balustradeY(-15.5f, -6.0f, 9.7f);
    balustradeY( 15.5f, -6.0f, 9.7f);
    balustradeX( -6.0f, -15.5f, -3.5f);
    balustradeX( -6.0f,   3.5f, 15.5f);

    // Sculpted Coastal Return Arch behind arrival (y = -5.4)
    DressedColumn(-3.10f, -5.4f, 0.46f, 4.7f, floorZ);
    DressedColumn( 3.10f, -5.4f, 0.46f, 4.7f, floorZ);
    LocalBox(-3.75f, -5.90f, 4.70f, 3.75f, -4.90f, 5.40f, floorZ, kMatHeader, 0xfff5f1e8, true);
    LocalBox(-3.82f, -5.95f, 4.90f, 3.82f, -4.85f, 5.10f, floorZ, kMatTrim, kBrass);
    LocalBox(-2.40f, -5.82f, 4.66f, 2.40f, -4.98f, 4.74f, floorZ, kMatWarmGlow, kLight);

    // Flanking sunlit reflecting pools with luminous turquoise underwater rim strips & water feature plinths
    for (int side = -1; side <= 1; side += 2) {
        const float px0 = side < 0 ? -13.2f : 7.2f;
        const float px1 = side < 0 ?  -7.2f : 13.2f;
        const float py0 = 4.5f, py1 = 15.0f;
        const float pcx = 0.5f * (px0 + px1), pcy = 0.5f * (py0 + py1);
        AddContactShadow(pcx, pcy, 3.0f, 5.25f, 0.90f, floorZ, 0.036f, 0.40f);
        LocalBox(px0, py0, 0.02f, px1, py0 + 0.46f, 0.44f, floorZ, kMatWall, 0xffe8e4dc);
        LocalBox(px0, py1 - 0.46f, 0.02f, px1, py1, 0.44f, floorZ, kMatWall, 0xffe8e4dc);
        LocalBox(px0, py0 + 0.46f, 0.02f, px0 + 0.46f, py1 - 0.46f, 0.44f, floorZ, kMatWall, 0xffe8e4dc);
        LocalBox(px1 - 0.46f, py0 + 0.46f, 0.02f, px1, py1 - 0.46f, 0.44f, floorZ, kMatWall, 0xffe8e4dc);
        // Brass coping cap
        LocalBox(px0 - 0.04f, py0 - 0.04f, 0.42f, px1 + 0.04f, py0 + 0.48f, 0.48f, floorZ, kMatPierCap);
        LocalBox(px0 - 0.04f, py1 - 0.48f, 0.42f, px1 + 0.04f, py1 + 0.04f, 0.48f, floorZ, kMatPierCap);
        LocalBox(px0 - 0.04f, py0 + 0.48f, 0.42f, px0 + 0.48f, py1 - 0.48f, 0.48f, floorZ, kMatPierCap);
        LocalBox(px1 - 0.48f, py0 + 0.48f, 0.42f, px1 + 0.04f, py1 - 0.48f, 0.48f, floorZ, kMatPierCap);
        // Luminous aqua pool rim strip + shimmering reflecting water surface
        LocalBox(px0 + 0.46f, py0 + 0.46f, 0.28f, px1 - 0.46f, py0 + 0.56f, 0.34f, floorZ, kMatAquaGlow, kLight);
        LocalBox(px0 + 0.46f, py1 - 0.56f, 0.28f, px1 - 0.46f, py1 - 0.46f, 0.34f, floorZ, kMatAquaGlow, kLight);
        LocalFloorGrid(px0 + 0.46f, py0 + 0.46f, px1 - 0.46f, py1 - 0.46f, 0.31f, floorZ, kMatWater, 1.8f, 0xff4088a8);
        // Central coastal limestone obelisk plinth rising out of each reflecting pool
        LocalOctagonTier(pcx, pcy, 0.72f, 0.28f, 0.56f, floorZ, kMatPierBase, kSlate, true);
        LocalOctagonTier(pcx, pcy, 0.42f, 0.56f, 0.92f, floorZ, kMatTrim, kBrass, true);
    }

    // Monumental Courtyard Hero Battle Engine Aquila Plinth at (0, 10.5)
    TurntablePlinth(0.0f, 10.5f, 2.50f, 1.35f, floorZ, true, kMatWarmGlow, kMatPlinth);

    // Grand Museum Monumental South Colonnaded Facade (y = 20.0, height 13.5 m + crowned pediment 15.8 m)
    LocalBox(-16.3f, 20.0f - w, 0.0f, -5.5f, 20.0f + w, 13.5f, floorZ, kMatWall, 0xfff8f4eb);
    LocalBox(  5.5f, 20.0f - w, 0.0f, 16.3f, 20.0f + w, 13.5f, floorZ, kMatWall, 0xfff8f4eb);
    PortalFrameX(20.0f, -5.5f, 5.5f, 7.5f, 13.5f, floorZ, kMatTrim, kMatWarmGlow);
    // Classical stepped cornice + monumental stepped coastal entablature pediment + glowing brass frieze
    LocalBox(-16.7f, 19.1f, 12.65f, 16.7f, 20.6f, 13.65f, floorZ, kMatPierCap, 0xfffbf8f0, true);
    LocalBox(-13.8f, 19.25f, 13.65f, 13.8f, 20.5f, 14.55f, floorZ, kMatWall, 0xfff8f4eb);
    LocalBox(-9.5f,  19.35f, 14.55f,  9.5f, 20.4f, 15.35f, floorZ, kMatPierCap, 0xfffbf8f0);
    LocalBox(-9.8f,  19.28f, 14.48f,  9.8f, 19.42f, 14.64f, floorZ, kMatTrim, kBrass);
    LocalBox(-16.5f, 19.45f, 7.55f, -5.5f, 19.75f, 8.10f, floorZ, kMatTrim, kBrass);
    LocalBox(  5.5f, 19.45f, 7.55f, 16.5f, 19.75f, 8.10f, floorZ, kMatTrim, kBrass);
    // Warm architectural facade downlight cove strip under cornice
    LocalBox(-15.5f, 19.30f, 12.50f, 15.5f, 19.48f, 12.66f, floorZ, kMatWarmGlow, kLight);
    // Monumental paired colonnade piers framing the Courtyard facade
    DressedColumn(-13.5f, 19.45f, 0.44f, 12.65f, floorZ);
    DressedColumn( -8.6f, 19.45f, 0.44f, 12.65f, floorZ);
    DressedColumn(  8.6f, 19.45f, 0.44f, 12.65f, floorZ);
    DressedColumn( 13.5f, 19.45f, 0.44f, 12.65f, floorZ);
    // Warm architectural lantern sconces flanking the Grand Atrium entrance portal
    for (int side = -1; side <= 1; side += 2) {
        const float sx = float(side) * 6.35f;
        LocalBox(sx - 0.18f, 19.48f, 2.2f, sx + 0.18f, 19.72f, 4.6f, floorZ, kMatPier, kSlate);
        LocalBox(sx - 0.10f, 19.40f, 2.4f, sx + 0.10f, 19.52f, 4.4f, floorZ, kMatWarmGlow, kLight);
    }

    // =========================================================================
    // 2. SKYLIT GRAND ATRIUM ROTUNDA (x [-16, 16], y [20, 52], height 13.5 m)
    // =========================================================================
    LocalFloorGrid(-16.0f, 20.0f, 16.0f, 52.0f, 0.02f, floorZ, kMatFloor, 2.0f);

    // Central Brass Compass Rose & Rotunda Mosaic Floor Inlay at (0, 36.0)
    LocalFloorGrid(-5.2f, 30.8f, 5.2f, 41.2f, 0.048f, floorZ, kMatInlay, 2.0f, 0xffd0d6dc);
    LocalFloorGrid(-5.5f, 30.5f, 5.5f, 30.8f, 0.062f, floorZ, kMatTrim, 3.0f, kBrass);
    LocalFloorGrid(-5.5f, 41.2f, 5.5f, 41.5f, 0.062f, floorZ, kMatTrim, 3.0f, kBrass);
    LocalFloorGrid(-5.5f, 30.8f, -5.2f, 41.2f, 0.062f, floorZ, kMatTrim, 3.0f, kBrass);
    LocalFloorGrid( 5.2f, 30.8f,  5.5f, 41.2f, 0.062f, floorZ, kMatTrim, 3.0f, kBrass);
    // Inner octagonal honed marble rotunda medallion around the Archive Centerpiece Pedestal
    LocalOctagonTier(0.0f, 36.0f, 4.25f, 0.02f, 0.068f, floorZ, kMatPlinth, 0xffeef2f5, true);
    // Cardinal brass compass beams radiating across the rotunda medallion
    LocalFloorGrid(-5.0f, 35.72f, 5.0f, 36.28f, 0.076f, floorZ, kMatTrim, 2.5f, kBrass);
    LocalFloorGrid(-0.28f, 31.0f, 0.28f, 41.0f, 0.076f, floorZ, kMatTrim, 2.5f, kBrass);

    // Floor runners connecting Rotunda to ALL 6 Atrium portals (unobstructed straight spine from South portal to Rotunda!)
    RunnerStripY(0.0f, 20.0f, 30.5f, 1.35f, floorZ);        // South -> Colonnade (100% OPEN SIGHTLINE!)
    RunnerStripY(0.0f, 41.5f, 52.0f, 1.35f, floorZ);        // North -> Hangar
    RunnerStripX(-16.0f, -5.5f, 41.0f, 1.15f, floorZ);      // West -> Forseti
    RunnerStripX(  5.5f, 16.0f, 41.0f, 1.15f, floorZ);      // East -> Muspell
    RunnerStripX(-16.0f, -1.45f, 24.0f, 1.00f, floorZ);     // Southwest -> Early Designs Vault
    RunnerStripX( 1.45f, 16.0f, 24.0f, 1.00f, floorZ);      // Southeast -> Concept Art Gallery

    // GRAND ATRIUM ROTUNDA CENTERPIECE PEDESTAL at (0.0, 36.0), footprint [-2.0, 34.0]..[2.0, 38.0], height 1.05m
    // Stepped octagonal-waisted marble & brushed-brass rotunda dais with a 3.2 m square top deck cleanly
    // supporting the interactive Archive Pedestal collar at topZ = 1.05m beneath the open clerestory skylight.
    AddContactShadow(0.0f, 36.0f, 2.00f, 2.00f, 1.45f, floorZ, 0.082f, 0.50f);
    LocalBox(-2.00f, 34.00f, 0.068f, 2.00f, 38.00f, 0.22f, floorZ, kMatPierBase, kSlate);
    LocalOctagonTier(0.0f, 36.0f, 1.98f, 0.22f, 0.56f, floorZ, kMatPlinth, 0xfff2f5f8, true);
    LocalOctagonTier(0.0f, 36.0f, 2.00f, 0.48f, 0.56f, floorZ, kMatTrim, kBrass, false);
    LocalBox(-1.60f, 34.40f, 0.56f, 1.60f, 37.60f, 0.94f, floorZ, kMatPlinth, 0xffeef2f6);
    LocalBox(-1.64f, 34.36f, 0.88f, 1.64f, 37.64f, 0.96f, floorZ, kMatWarmGlow, kLight);
    LocalBox(-1.68f, 34.32f, 0.96f, 1.68f, 37.68f, 1.03f, floorZ, kMatTrim, kBrass);
    LocalBox(-1.60f, 34.40f, 0.98f, 1.60f, 37.60f, 1.05f, floorZ, kMatPlinthTop, kSlate);

    // Sleek relocated West Entrance Flank Directory Console at (kDirectoryX = -5.4, kDirectoryY = 26.8)
    {
        const float dx = CeMuseumTour::kDirectoryX;
        const float dy = CeMuseumTour::kDirectoryY;
        AddContactShadow(dx, dy, 1.65f, 0.35f, 0.72f, floorZ, 0.036f, 0.46f);
        // Subtle paved plinth pad under flank console
        LocalFloorGrid(dx - 1.95f, dy - 0.65f, dx + 1.95f, dy + 0.65f, 0.044f, floorZ, kMatInlay, 2.0f, 0xffd0d6dc);
        LocalBox(dx - 1.60f, dy - 0.32f, 0.04f, dx + 1.60f, dy + 0.32f, 0.18f, floorZ, kMatPierBase, kSlate);
        LocalBox(dx - 1.46f, dy - 0.22f, 0.18f, dx + 1.46f, dy + 0.22f, 0.98f, floorZ, kMatStand, kSlate);
        // Illuminated brass accent strip + chamfered satin-brass desk ledge
        LocalBox(dx - 1.50f, dy - 0.26f, 0.94f, dx + 1.50f, dy + 0.26f, 1.00f, floorZ, kMatWarmGlow, kLight);
        LocalBox(dx - 1.62f, dy - 0.32f, 1.00f, dx + 1.62f, dy + 0.32f, 1.13f, floorZ, kMatTrim, kBrass);
        // Recessed dark-slate display bezel behind the 6 interactive brass selector plates (at y = dy - 0.18, z = 1.25)
        LocalBox(dx - 1.54f, dy - 0.14f, 1.13f, dx + 1.54f, dy + 0.18f, 1.38f, floorZ, kMatPierBase, kSlate);
        LocalBox(dx - 1.58f, dy - 0.16f, 1.38f, dx + 1.58f, dy + 0.20f, 1.44f, floorZ, kMatTrim, kBrass);
    }

    // Four monumental clerestory skylight columns with upper arched tie-beams
    DressedColumn(-8.2f, 28.2f, 0.54f, 13.5f, floorZ);
    DressedColumn( 8.2f, 28.2f, 0.54f, 13.5f, floorZ);
    DressedColumn(-8.2f, 43.8f, 0.54f, 13.5f, floorZ);
    DressedColumn( 8.2f, 43.8f, 0.54f, 13.5f, floorZ);

    // Atrium Perimeter Ceiling with HUGE 16m x 16m Open Sunlit Glass Clerestory Skylight (x [-8,+8], y [28,44])
    Ceiling(floorZ, -16.3f, 20.0f,  -8.0f, 52.3f, 13.5f, kMatCeiling);
    Ceiling(floorZ,   8.0f, 20.0f,  16.3f, 52.3f, 13.5f, kMatCeiling);
    Ceiling(floorZ,  -8.0f, 20.0f,   8.0f, 28.0f, 13.5f, kMatCeiling);
    Ceiling(floorZ,  -8.0f, 44.0f,   8.0f, 52.3f, 13.5f, kMatCeiling);
    // Clerestory lantern curb + warm recessed cove lighting + brushed-brass glass-skylight mullions
    LocalBox(-8.3f, 27.7f, 13.0f,  8.3f, 28.2f, 14.8f, floorZ, kMatPierCap, 0xfff6f2ea, true);
    LocalBox(-8.3f, 43.8f, 13.0f,  8.3f, 44.3f, 14.8f, floorZ, kMatPierCap, 0xfff6f2ea, true);
    LocalBox(-8.3f, 28.2f, 13.0f, -7.8f, 43.8f, 14.8f, floorZ, kMatPierCap, 0xfff6f2ea, true);
    LocalBox( 7.8f, 28.2f, 13.0f,  8.3f, 43.8f, 14.8f, floorZ, kMatPierCap, 0xfff6f2ea, true);
    // Warm sunlit clerestory cove strips glowing around the inside of the 16m skylight aperture
    LocalBox(-7.8f, 28.18f, 13.12f,  7.8f, 28.34f, 13.38f, floorZ, kMatWarmGlow, kLight);
    LocalBox(-7.8f, 43.66f, 13.12f,  7.8f, 43.82f, 13.38f, floorZ, kMatWarmGlow, kLight);
    LocalBox(-7.82f, 28.3f, 13.12f, -7.66f, 43.7f, 13.38f, floorZ, kMatWarmGlow, kLight);
    LocalBox( 7.66f, 28.3f, 13.12f,  7.82f, 43.7f, 13.38f, floorZ, kMatWarmGlow, kLight);
    // Slender satin-brass clerestory glass mullions (3x3 sunlit skylight grid!)
    for (float pos = -4.0f; pos <= 4.1f; pos += 4.0f) {
        LocalBox(-7.9f, 36.0f + pos - 0.14f, 13.8f, 7.9f, 36.0f + pos + 0.14f, 14.35f, floorZ, kMatTrim, kBrass, true);
        LocalBox(pos - 0.14f, 28.1f, 13.8f, pos + 0.14f, 43.9f, 14.35f, floorZ, kMatTrim, kBrass, true);
    }
    // Translucent sunlit skylight glass panes + warm golden diagonal sunbeam shafts into the Rotunda!
    AddGlassQuad(-7.8f, 28.2f, 14.15f, 0x26dff2ff,  7.8f, 28.2f, 14.15f, 0x38fff3d6,
                  7.8f, 43.8f, 14.15f, 0x26d8eeff, -7.8f, 43.8f, 14.15f, 0x20c8e4fc, floorZ);
    // Warm translucent golden sunbeam curtains descending softly toward the Rotunda Centerpiece
    AddGlassQuad( 6.0f, 30.0f, 13.4f, 0x22ffebc0,  6.0f, 42.0f, 13.4f, 0x1effe6b2,
                  1.2f, 43.6f,  1.8f, 0x00ffebc0,  1.2f, 31.6f,  1.8f, 0x00ffebc0, floorZ);
    AddGlassQuad(-5.2f, 30.0f, 13.4f, 0x16e8f4ff,  6.0f, 30.0f, 13.4f, 0x22ffefc6,
                  1.2f, 31.6f,  1.8f, 0x00ffefc6, -8.0f, 31.6f,  1.8f, 0x00e8f4ff, floorZ);
    // Sculpted ceiling coffer beams tying the 4 clerestory columns into the North/South/East/West walls
    LocalBox(-15.7f, 27.85f, 12.65f, -8.2f, 28.55f, 13.48f, floorZ, kMatPier, kSlate, true);
    LocalBox(  8.2f, 27.85f, 12.65f, 15.7f, 28.55f, 13.48f, floorZ, kMatPier, kSlate, true);
    LocalBox(-15.7f, 43.45f, 12.65f, -8.2f, 44.15f, 13.48f, floorZ, kMatPier, kSlate, true);
    LocalBox(  8.2f, 43.45f, 12.65f, 15.7f, 44.15f, 13.48f, floorZ, kMatPier, kSlate, true);
    LocalBox(-8.55f, 20.3f, 12.65f, -7.85f, 28.2f, 13.48f, floorZ, kMatPier, kSlate, true);
    LocalBox( 7.85f, 20.3f, 12.65f,  8.55f, 28.2f, 13.48f, floorZ, kMatPier, kSlate, true);
    LocalBox(-8.55f, 43.8f, 12.65f, -7.85f, 51.7f, 13.48f, floorZ, kMatPier, kSlate, true);
    LocalBox( 7.85f, 43.8f, 12.65f,  8.55f, 51.7f, 13.48f, floorZ, kMatPier, kSlate, true);

    // Atrium West Wall (x = -16.0): portals to Vault [21.5..26.5] and Forseti [38.0..44.0]
    LocalBox(-16.0f - w, 20.0f, 0.0f, -16.0f + w, 21.5f, 13.5f, floorZ, kMatWall);
    PortalFrameY(-16.0f, 21.5f, 26.5f, 5.0f, 13.5f, floorZ, kMatTrim, kMatWarmGlow);
    LocalBox(-16.0f - w, 26.5f, 0.0f, -16.0f + w, 38.0f, 13.5f, floorZ, kMatWall);
    PortalFrameY(-16.0f, 38.0f, 44.0f, 6.0f, 13.5f, floorZ, kMatForsetiTrim, kMatForsetiGlow);
    LocalBox(-16.0f - w, 44.0f, 0.0f, -16.0f + w, 52.3f, 13.5f, floorZ, kMatWall);
    // Atrium decorative architectural wall wainscot & brass frieze on West/East piers
    LocalBox(-15.70f, 27.2f, 6.0f, -15.56f, 37.3f, 6.45f, floorZ, kMatForsetiTrim);
    LocalBox(-15.70f, 27.2f, 0.02f, -15.56f, 37.3f, 0.45f, floorZ, kMatPierBase);

    // Atrium East Wall (x = +16.0): portals to Concept Art [21.5..26.5] and Muspell [38.0..44.0]
    LocalBox(16.0f - w, 20.0f, 0.0f, 16.0f + w, 21.5f, 13.5f, floorZ, kMatWall);
    PortalFrameY(16.0f, 21.5f, 26.5f, 5.0f, 13.5f, floorZ, kMatTrim, kMatWarmGlow);
    LocalBox(16.0f - w, 26.5f, 0.0f, 16.0f + w, 38.0f, 13.5f, floorZ, kMatWall);
    PortalFrameY(16.0f, 38.0f, 44.0f, 6.0f, 13.5f, floorZ, kMatMuspellTrim, kMatMuspellGlow);
    LocalBox(16.0f - w, 44.0f, 0.0f, 16.0f + w, 52.3f, 13.5f, floorZ, kMatWall);
    LocalBox(15.56f, 27.2f, 6.0f, 15.70f, 37.3f, 6.45f, floorZ, kMatMuspellTrim);
    LocalBox(15.56f, 27.2f, 0.02f, 15.70f, 37.3f, 0.45f, floorZ, kMatPierBase);

    // Atrium North Wall (y = 52.0): grand 10 m arch to Aviation & Naval Hangar [-5.0..+5.0]
    LocalBox(-20.3f, 52.0f - w, 0.0f, -5.0f, 52.0f + w, 15.5f, floorZ, kMatWall);
    PortalFrameX(52.0f, -5.0f, 5.0f, 8.0f, 15.5f, floorZ, kMatTrim, kMatWarmGlow);
    LocalBox(  5.0f, 52.0f - w, 0.0f, 20.3f, 52.0f + w, 15.5f, floorZ, kMatWall);
    // Architectural dado frieze, skirting & framed classical sconce niches flanking the Hangar portal
    LocalBox(-15.6f, 51.56f, 6.0f, -5.9f, 51.72f, 6.45f, floorZ, kMatTrim, kBrass);
    LocalBox(  5.9f, 51.56f, 6.0f, 15.6f, 51.72f, 6.45f, floorZ, kMatTrim, kBrass);
    LocalBox(-15.6f, 51.58f, 0.02f, -5.9f, 51.72f, 0.45f, floorZ, kMatPierBase, kSlate);
    LocalBox(  5.9f, 51.58f, 0.02f, 15.6f, 51.72f, 0.45f, floorZ, kMatPierBase, kSlate);
    for (int side = -1; side <= 1; side += 2) {
        const float cx = float(side) * 10.8f;
        LocalBox(cx - 2.6f, 51.54f, 1.30f, cx + 2.6f, 51.72f, 5.20f, floorZ, kMatPierBase, kSlate);
        LocalBox(cx - 2.72f, 51.48f, 1.20f, cx + 2.72f, 51.62f, 1.38f, floorZ, kMatTrim, kBrass);
        LocalBox(cx - 2.72f, 51.48f, 5.12f, cx + 2.72f, 51.62f, 5.30f, floorZ, kMatTrim, kBrass);
        LocalBox(cx - 2.72f, 51.48f, 1.38f, cx - 2.54f, 51.62f, 5.12f, floorZ, kMatTrim, kBrass);
        LocalBox(cx + 2.54f, 51.48f, 1.38f, cx + 2.72f, 51.62f, 5.12f, floorZ, kMatTrim, kBrass);
        LocalBox(cx - 1.80f, 51.44f, 4.92f, cx + 1.80f, 51.56f, 5.06f, floorZ, kMatWarmGlow, kLight);
        // Sculpted faction relief medallion inside each Atrium North wall niche
        const Material medMat = side < 0 ? kMatForsetiTrim : kMatMuspellTrim;
        LocalBox(cx - 1.40f, 51.46f, 2.35f, cx + 1.40f, 51.56f, 4.05f, floorZ, medMat);
        LocalBox(cx - 0.14f, 51.42f, 2.00f, cx + 0.14f, 51.54f, 4.40f, floorZ, kMatTrim, kBrass);
    }

    // =========================================================================
    // 3. FORSETI FORCES WING (West: x [-44, -16], y [30, 52], height 10.5 m)
    //    Swept steel-blue structural ribs, luminous cyan alcove light strips,
    //    sculpted architraves, and crisp limestone/slate panelling!
    // =========================================================================
    LocalFloorGrid(-44.0f, 30.0f, -16.0f, 52.0f, 0.02f, floorZ, kMatFloor, 2.0f);
    RunnerStripX(-40.0f, -16.0f, 41.0f, 1.15f, floorZ);
    // Luminous cyan wayfinding inlay edges flanking the Forseti central runner
    LocalFloorGrid(-39.5f, 39.62f, -16.5f, 39.72f, 0.068f, floorZ, kMatForsetiGlow, 4.0f, 0xff78ecff);
    LocalFloorGrid(-39.5f, 42.28f, -16.5f, 42.38f, 0.068f, floorZ, kMatForsetiGlow, 4.0f, 0xff78ecff);

    LocalBox(-44.3f, 30.0f - w, 0.0f, -16.0f, 30.0f + w, 10.5f, floorZ, kMatWall);
    LocalBox(-44.3f, 52.0f - w, 0.0f, -16.0f, 52.0f + w, 10.5f, floorZ, kMatWall);
    LocalBox(-44.0f - w, 29.7f, 0.0f, -44.0f + w, 52.3f, 10.5f, floorZ, kMatWall);

    // Sculpted Forseti steel-blue architraves, crisp slate wainscot & luminous cyan cove light strips
    LocalBox(-43.6f, 30.32f, 6.25f, -16.4f, 30.58f, 7.25f, floorZ, kMatForsetiTrim);
    LocalBox(-43.6f, 51.42f, 6.25f, -16.4f, 51.68f, 7.25f, floorZ, kMatForsetiTrim);
    LocalBox(-43.68f, 30.4f, 6.25f, -43.42f, 51.6f, 7.25f, floorZ, kMatForsetiTrim);
    // Luminous cyan horizontal alcove light strips recessed under the architraves & above skirting
    LocalBox(-43.5f, 30.34f, 6.06f, -16.5f, 30.62f, 6.24f, floorZ, kMatForsetiGlow, 0xff78ecff);
    LocalBox(-43.5f, 51.38f, 6.06f, -16.5f, 51.66f, 6.24f, floorZ, kMatForsetiGlow, 0xff78ecff);
    LocalBox(-43.66f, 30.5f, 6.06f, -43.38f, 51.5f, 6.24f, floorZ, kMatForsetiGlow, 0xff78ecff);
    // Sleek slate dado wainscot + luminous cyan plinth-level light strips
    LocalBox(-43.6f, 30.32f, 0.02f, -16.4f, 30.50f, 0.72f, floorZ, kMatPier);
    LocalBox(-43.6f, 51.50f, 0.02f, -16.4f, 51.68f, 0.72f, floorZ, kMatPier);
    LocalBox(-43.5f, 30.34f, 0.72f, -16.5f, 30.54f, 0.84f, floorZ, kMatForsetiGlow, 0xff78ecff);
    LocalBox(-43.5f, 51.46f, 0.72f, -16.5f, 51.66f, 0.84f, floorZ, kMatForsetiGlow, 0xff78ecff);

    // Swept aerospace steel-blue structural ribs arching cleanly across the Forseti Wing!
    const float fRibX[4] = {-39.5f, -32.75f, -25.25f, -18.0f};
    for (unsigned i = 0; i < 4; ++i) {
        const float rx = fRibX[i];
        // Column base rises cleanly into the swept rib haunch (zero protruding ceiling cap!)
        DressedColumn(rx, 30.68f, 0.38f, 4.55f, floorZ, kMatForsetiTrim, kMatTrim);
        DressedColumn(rx, 51.32f, 0.38f, 4.55f, floorZ, kMatForsetiTrim, kMatTrim);
        // Swept diagonal aerospace haunches (above head height z = 4.4..10.5m) curving into the vaulted ceiling
        const float southY[4] = {30.35f, 31.15f, 33.40f, 30.35f};
        const float southZ[4] = { 4.40f,  4.40f, 10.20f, 10.45f};
        EmitSweptRibX(rx - 0.34f, rx + 0.34f, southY, southZ, floorZ, kMatForsetiTrim, 0xff5a8eb8);
        const float northY[4] = {51.65f, 50.85f, 48.60f, 51.65f};
        const float northZ[4] = { 4.40f,  4.40f, 10.20f, 10.45f};
        EmitSweptRibX(rx - 0.34f, rx + 0.34f, northY, northZ, floorZ, kMatForsetiTrim, 0xff5a8eb8);
        // Overhead transverse steel-blue vault rib with luminous cyan accent underside
        LocalBox(rx - 0.34f, 32.8f, 9.70f, rx + 0.34f, 49.2f, 10.48f, floorZ, kMatForsetiTrim, 0xff5a8eb8, true);
        LocalBox(rx - 0.14f, 33.4f, 9.58f, rx + 0.14f, 48.6f, 9.72f, floorZ, kMatForsetiGlow, 0xff78ecff, true);
    }
    // Forseti West Apse Monumental Commonwealth Wing Crest framing the two rotating Forseti tank turntables
    DressedColumn(-43.45f, 35.4f, 0.36f, 10.5f, floorZ, kMatForsetiTrim, kMatTrim);
    DressedColumn(-43.45f, 46.6f, 0.36f, 10.5f, floorZ, kMatForsetiTrim, kMatTrim);
    LocalBox(-43.68f, 35.8f, 0.02f, -43.36f, 46.2f, 6.06f, floorZ, kMatPier, kSlate);
    LocalBox(-43.38f, 36.1f, 0.85f, -43.24f, 36.32f, 5.85f, floorZ, kMatForsetiGlow, 0xff78ecff);
    LocalBox(-43.38f, 45.68f, 0.85f, -43.24f, 45.90f, 5.85f, floorZ, kMatForsetiGlow, 0xff78ecff);
    LocalBox(-43.38f, 36.3f, 5.72f, -43.24f, 45.7f, 5.92f, floorZ, kMatForsetiGlow, 0xff78ecff);
    // Stepped swept Forseti Commonwealth chevron wing emblem in steel-blue & brushed brass relief!
    LocalBox(-43.38f, 40.78f, 1.30f, -43.18f, 41.22f, 5.50f, floorZ, kMatTrim, kBrass);
    LocalBox(-43.34f, 40.92f, 1.65f, -43.14f, 41.08f, 5.20f, floorZ, kMatForsetiGlow, 0xff78ecff);
    for (int side = -1; side <= 1; side += 2) {
        const float s = float(side);
        // Three swept aerospace wing tiers radiating symmetrically from center (y = 41.0)
        const float wy0[4] = {41.0f + s * 0.45f, 41.0f + s * 4.10f, 41.0f + s * 4.10f, 41.0f + s * 0.45f};
        const float wz0[4] = {4.30f, 5.20f, 4.55f, 3.65f};
        EmitSweptRibX(-43.36f, -43.16f, wy0, wz0, floorZ, kMatForsetiTrim, 0xff5a8eb8);
        const float wy1[4] = {41.0f + s * 0.45f, 41.0f + s * 3.30f, 41.0f + s * 3.30f, 41.0f + s * 0.45f};
        const float wz1[4] = {3.45f, 4.15f, 3.60f, 2.90f};
        EmitSweptRibX(-43.36f, -43.16f, wy1, wz1, floorZ, kMatTrim, kBrass);
        const float wy2[4] = {41.0f + s * 0.45f, 41.0f + s * 2.40f, 41.0f + s * 2.40f, 41.0f + s * 0.45f};
        const float wz2[4] = {2.65f, 3.15f, 2.70f, 2.20f};
        EmitSweptRibX(-43.36f, -43.16f, wy2, wz2, floorZ, kMatForsetiTrim, 0xff5a8eb8);
    }
    // Forseti Ceiling with central longitudinal sunlit/luminous sky-cove skylight spine!
    Ceiling(floorZ, -44.3f, 29.7f, -15.7f, 39.2f, 10.5f, kMatCeiling);
    Ceiling(floorZ, -44.3f, 42.8f, -15.7f, 52.3f, 10.5f, kMatCeiling);
    // Recessed central skylight clerestory strip (y [39.2, 42.8]) glowing cool daylight down the Forseti nave
    LocalBox(-42.5f, 39.1f, 10.25f, -17.0f, 39.32f, 11.2f, floorZ, kMatPierCap);
    LocalBox(-42.5f, 42.68f, 10.25f, -17.0f, 42.9f, 11.2f, floorZ, kMatPierCap);
    AddGlassQuad(-42.4f, 39.3f, 10.95f, 0x38cdeeff, -17.1f, 39.3f, 10.95f, 0x38e4f6ff,
                 -17.1f, 42.7f, 10.95f, 0x38cdeeff, -42.4f, 42.7f, 10.95f, 0x38bce6ff, floorZ);

    // Six Forseti native bay plinths (with luminous cyan base reveals + soft contact shadows!)
    for (unsigned i = 0; i < kNativeBayCount; ++i) {
        const NativeBaySpot &nb = gGrandGraph.nativeBays[i];
        TurntablePlinth(nb.x, nb.y, nb.halfSize, nb.topZ, floorZ, false, kMatForsetiGlow, kMatPlinth);
        float postLift = 0.0f;
        const CeMuseumExhibition::Bay *bay = CeMuseumExhibitionHost::InputBay(gExhibition, i);
        if (bay) {
            for (unsigned b = 0; b < gExhibition.boxCount; ++b) {
                const CeMuseumExhibition::Box &bx = gExhibition.boxes[b];
                if (bx.purpose == CeMuseumExhibition::kSolidStand && bx.bay == bay->number &&
                    (bx.max.x - bx.min.x) <= 0.20f) {
                    postLift = bx.max.z - bx.min.z;
                    break;
                }
            }
        }
        if (postLift > 0.05f) {
            const float postTop = nb.topZ + postLift;
            LocalBox(nb.x - 0.14f, nb.y - 0.14f, nb.topZ, nb.x + 0.14f, nb.y + 0.14f, postTop - 0.06f,
                     floorZ, kMatStand, kSlate);
            LocalBox(nb.x - 0.18f, nb.y - 0.18f, postTop - 0.12f, nb.x + 0.18f, nb.y + 0.18f, postTop - 0.02f,
                     floorZ, kMatTrim, kBrass);
        }
    }
    TurntablePlinth(-40.8f, 37.8f, 1.45f, 0.90f, floorZ, false, kMatForsetiGlow, kMatPlinth);
    TurntablePlinth(-40.8f, 44.2f, 1.45f, 0.90f, floorZ, false, kMatForsetiGlow, kMatPlinth);
    AudioKioskPedestal(-18.5f, 34.5f, floorZ);

    // =========================================================================
    // 4. MUSPELL DOMINION WING (East: x [16, 44], y [30, 52], height 10.5 m)
    //    Angular dark forged-iron buttresses, warm molten-amber forge light strips,
    //    heavy riveted fascia, and dramatic foundry shadow contrast!
    // =========================================================================
    LocalFloorGrid(16.0f, 30.0f, 44.0f, 52.0f, 0.02f, floorZ, kMatFloor, 2.0f, 0xffd2cecc);
    RunnerStripX(16.0f, 41.0f, 41.0f, 1.15f, floorZ);
    // Molten-amber forge inlay rails flanking the Muspell Dominion central runner
    LocalFloorGrid(16.5f, 39.60f, 40.8f, 39.72f, 0.068f, floorZ, kMatMuspellGlow, 4.0f, 0xffff9a38);
    LocalFloorGrid(16.5f, 42.28f, 40.8f, 42.40f, 0.068f, floorZ, kMatMuspellGlow, 4.0f, 0xffff9a38);

    // Heavy dark volcanic forged-iron Dominion walls
    LocalBox(16.0f, 30.0f - w, 0.0f, 44.3f, 30.0f + w, 10.5f, floorZ, kMatMuspellWall);
    LocalBox(16.0f, 52.0f - w, 0.0f, 44.3f, 52.0f + w, 10.5f, floorZ, kMatMuspellWall);
    LocalBox(44.0f - w, 29.7f, 0.0f, 44.0f + w, 52.3f, 10.5f, floorZ, kMatMuspellWall);

    // Heavy riveted bronze-iron fascia + molten-amber forge strips along North, South, and East apse walls
    LocalBox(16.4f, 30.32f, 5.95f, 43.6f, 30.72f, 7.30f, floorZ, kMatMuspellTrim);
    LocalBox(16.4f, 51.28f, 5.95f, 43.6f, 51.68f, 7.30f, floorZ, kMatMuspellTrim);
    LocalBox(43.28f, 30.4f, 5.95f, 43.68f, 51.6f, 7.30f, floorZ, kMatMuspellTrim);
    // Recessed molten-amber horizontal forge-light channels (upper & lower fascia bands)
    LocalBox(16.5f, 30.36f, 5.74f, 43.5f, 30.76f, 5.94f, floorZ, kMatMuspellGlow, 0xffff9a38);
    LocalBox(16.5f, 51.24f, 5.74f, 43.5f, 51.64f, 5.94f, floorZ, kMatMuspellGlow, 0xffff9a38);
    LocalBox(43.24f, 30.5f, 5.74f, 43.64f, 51.5f, 5.94f, floorZ, kMatMuspellGlow, 0xffff9a38);
    LocalBox(16.5f, 30.34f, 0.68f, 43.5f, 30.56f, 0.84f, floorZ, kMatMuspellGlow, 0xffff9a38);
    LocalBox(16.5f, 51.44f, 0.68f, 43.5f, 51.66f, 0.84f, floorZ, kMatMuspellGlow, 0xffff9a38);
    // Heavy forged-iron armoured skirting
    LocalBox(16.4f, 30.32f, 0.02f, 43.6f, 30.52f, 0.68f, floorZ, kMatMuspellIron);
    LocalBox(16.4f, 51.48f, 0.02f, 43.6f, 51.68f, 0.68f, floorZ, kMatMuspellIron);

    // Angular dark forged-iron buttresses & cantilevered foundry roof girders (dramatic industrial silhouette!)
    const float mButtressX[4] = {18.0f, 26.0f, 34.0f, 42.0f};
    for (unsigned i = 0; i < 4; ++i) {
        const float bx = mButtressX[i];
        // Forged-iron buttress pier rises cleanly into the cantilevered knee-brace (no protruding cap near ceiling)
        DressedColumn(bx, 30.72f, 0.46f, 3.85f, floorZ, kMatMuspellIron, kMatMuspellTrim);
        DressedColumn(bx, 51.28f, 0.46f, 3.85f, floorZ, kMatMuspellIron, kMatMuspellTrim);
        // Vertical glowing molten-amber forge slot inlaid in each buttress front face
        LocalBox(bx - 0.10f, 31.12f, 0.90f, bx + 0.10f, 31.26f, 3.65f, floorZ, kMatMuspellGlow, 0xffff9a38);
        LocalBox(bx - 0.10f, 50.74f, 0.90f, bx + 0.10f, 50.88f, 3.65f, floorZ, kMatMuspellGlow, 0xffff9a38);
        // Angular cantilevered forged-iron diagonal knee-braces (above z = 3.6m, zero floor obstruction!)
        const float sKneeY[4] = {30.35f, 31.45f, 34.30f, 30.35f};
        const float sKneeZ[4] = { 3.60f,  3.60f,  9.60f, 10.45f};
        EmitSweptRibX(bx - 0.44f, bx + 0.44f, sKneeY, sKneeZ, floorZ, kMatMuspellIron, 0xff464044);
        const float nKneeY[4] = {51.65f, 50.55f, 47.70f, 51.65f};
        const float nKneeZ[4] = { 3.60f,  3.60f,  9.60f, 10.45f};
        EmitSweptRibX(bx - 0.44f, bx + 0.44f, nKneeY, nKneeZ, floorZ, kMatMuspellIron, 0xff464044);
        // Heavy industrial overhead forged-iron roof girder + glowing ember soffit channel
        LocalBox(bx - 0.44f, 33.4f, 9.25f, bx + 0.44f, 48.6f, 10.48f, floorZ, kMatMuspellIron, 0xff423c40, true);
        LocalBox(bx - 0.50f, 33.4f, 9.35f, bx + 0.50f, 48.6f, 9.55f, floorZ, kMatMuspellTrim, 0xffa86030, true);
        LocalBox(bx - 0.12f, 34.0f, 9.15f, bx + 0.12f, 48.0f, 9.28f, floorZ, kMatMuspellGlow, 0xffff9a38, true);
    }
    // East Apse forged-iron Dominion monolith panel with framing molten-amber vertical channels
    LocalBox(43.22f, 37.6f, 0.8f, 43.68f, 44.4f, 5.74f, floorZ, kMatMuspellIron);
    LocalBox(43.14f, 37.8f, 1.1f, 43.26f, 38.05f, 5.45f, floorZ, kMatMuspellGlow, 0xffff9a38);
    LocalBox(43.14f, 43.95f, 1.1f, 43.26f, 44.20f, 5.45f, floorZ, kMatMuspellGlow, 0xffff9a38);
    LocalBox(43.14f, 38.05f, 1.1f, 43.26f, 43.95f, 1.32f, floorZ, kMatMuspellGlow, 0xffff9a38);
    LocalBox(43.14f, 38.05f, 5.22f, 43.26f, 43.95f, 5.45f, floorZ, kMatMuspellGlow, 0xffff9a38);
    LocalBox(43.16f, 39.2f, 2.2f, 43.28f, 42.8f, 4.3f, floorZ, kMatMuspellTrim);
    Ceiling(floorZ, 15.7f, 29.7f, 44.3f, 52.3f, 10.5f, kMatCeiling);

    // Six Muspell rotating turntable plinths in dark forged-iron with molten-amber base reveals + rich contact shadows!
    const float mx[3] = {22.0f, 30.0f, 38.0f};
    for (unsigned i = 0; i < 3; ++i) {
        TurntablePlinth(mx[i], 47.8f, 1.85f, 0.90f, floorZ, false, kMatMuspellGlow, kMatMuspellIron);
        TurntablePlinth(mx[i], 34.2f, 1.85f, 0.90f, floorZ, false, kMatMuspellGlow, kMatMuspellIron);
    }
    AudioKioskPedestal(18.5f, 34.5f, floorZ);

    // =========================================================================
    // 5. AVIATION & NAVAL HANGAR (North High-Bay: x [-20, 20], y [52, 78], 15.5 m)
    //    Soaring vaulted steel-and-glass roof trusses, suspended pendant light bays,
    //    flight-deck chevron floor inlays, twin emblem standards, and reflecting pool!
    // =========================================================================
    LocalFloorGrid(-20.0f, 52.0f, 20.0f, 78.0f, 0.02f, floorZ, kMatFloor, 2.0f);
    RunnerStripY(0.0f, 52.0f, 73.2f, 1.45f, floorZ);
    RunnerStripX(-10.5f, -1.55f, 65.0f, 1.05f, floorZ);
    RunnerStripX(  1.55f,  7.6f, 65.0f, 1.05f, floorZ);

    // Hangar Flight-Deck Architectural Floor Composition (100% flush & walkable, zero collision impact!)
    {
        // Central Hangar Launch Compass Deck at (0.0, 65.0): basalt octagon + brass bezel + marble inner medallion
        LocalOctagonTier(0.0f, 65.0f, 4.75f, 0.02f, 0.046f, floorZ, kMatInlay, 0xffcfd6dc, true);
        LocalOctagonTier(0.0f, 65.0f, 4.95f, 0.02f, 0.040f, floorZ, kMatTrim, kBrass, true);
        LocalOctagonTier(0.0f, 65.0f, 3.15f, 0.02f, 0.056f, floorZ, kMatPlinth, 0xffeef2f5, true);
        LocalOctagonTier(0.0f, 65.0f, 1.40f, 0.02f, 0.064f, floorZ, kMatInlay, 0xffc6d0d8, true);
        LocalFloorGrid(-3.0f, 64.86f, 3.0f, 65.14f, 0.068f, floorZ, kMatTrim, 2.0f, kBrass);
        LocalFloorGrid(-0.14f, 62.0f, 0.14f, 68.0f, 0.068f, floorZ, kMatTrim, 2.0f, kBrass);

        // Bold Aerospace Flight-Deck Approach Chevrons inlaid down the central spine (y = 55..71)
        const float chevY[5] = {54.8f, 57.8f, 60.8f, 69.4f, 71.8f};
        for (unsigned ci = 0; ci < 5u; ++ci) {
            const float cy = chevY[ci];
            // Left & right swept brass + basalt runway approach chevrons flanking the central spine
            EmitFloorQuad(-1.60f, cy + 1.05f, -1.60f, cy + 0.55f, -4.40f, cy - 0.85f, -4.40f, cy - 0.35f,
                          0.052f, floorZ, kMatInlay, 0xffced6de);
            EmitFloorQuad( 1.60f, cy + 1.05f,  4.40f, cy - 0.35f,  4.40f, cy - 0.85f,  1.60f, cy + 0.55f,
                          0.052f, floorZ, kMatInlay, 0xffced6de);
            EmitFloorQuad(-1.60f, cy + 1.22f, -1.60f, cy + 1.05f, -4.40f, cy - 0.35f, -4.40f, cy - 0.18f,
                          0.060f, floorZ, kMatTrim, kBrass);
            EmitFloorQuad( 1.60f, cy + 1.22f,  4.40f, cy - 0.18f,  4.40f, cy - 0.35f,  1.60f, cy + 1.05f,
                          0.060f, floorZ, kMatTrim, kBrass);
        }

        // West Aviation Taxiway Apron Borders (framing the 3 banked aircraft bays at y = 58, 65, 72)
        const float bayY[3] = {58.0f, 65.0f, 72.0f};
        for (unsigned bi = 0; bi < 3u; ++bi) {
            const float by = bayY[bi];
            LocalFloorGrid(-11.8f, by - 2.25f, -6.4f, by - 2.08f, 0.050f, floorZ, kMatTrim, 2.5f, kBrass);
            LocalFloorGrid(-11.8f, by + 2.08f, -6.4f, by + 2.25f, 0.050f, floorZ, kMatTrim, 2.5f, kBrass);
            LocalFloorGrid(-11.8f, by - 0.16f, -6.8f, by + 0.16f, 0.050f, floorZ, kMatInlay, 2.5f, 0xffced6de);
            LocalFloorGrid(-11.8f, by - 0.05f, -7.2f, by + 0.05f, 0.058f, floorZ, kMatForsetiGlow, 2.5f, 0xff78ecff);
        }

        // East Naval Drydock Promenade Apron framing the West viewing edge of the Reflecting Pool
        LocalFloorGrid(6.6f, 55.6f, 8.6f, 74.4f, 0.044f, floorZ, kMatInlay, 2.5f, 0xffced6de);
        LocalFloorGrid(6.48f, 55.48f, 6.62f, 74.52f, 0.056f, floorZ, kMatTrim, 2.5f, kBrass);
        LocalFloorGrid(6.62f, 55.48f, 8.75f, 55.62f, 0.056f, floorZ, kMatTrim, 2.5f, kBrass);
        LocalFloorGrid(6.62f, 74.38f, 8.75f, 74.52f, 0.056f, floorZ, kMatTrim, 2.5f, kBrass);
    }

    LocalBox(-20.0f - w, 52.0f, 0.0f, -20.0f + w, 78.3f, 15.5f, floorZ, kMatWall);
    LocalBox( 20.0f - w, 52.0f, 0.0f,  20.0f + w, 78.3f, 15.5f, floorZ, kMatWall);
    LocalBox(-20.3f, 78.0f - w, 0.0f,  20.3f, 78.0f + w, 15.5f, floorZ, kMatWall);
    // Low slate architectural wall skirting + brushed-brass dado rail wrapping all Hangar walls
    LocalBox(-19.70f, 52.3f, 0.02f, -19.52f, 77.7f, 0.85f, floorZ, kMatPierBase, kSlate);
    LocalBox( 19.52f, 52.3f, 0.02f,  19.70f, 77.7f, 0.85f, floorZ, kMatPierBase, kSlate);
    LocalBox(-19.70f, 77.52f, 0.02f,  19.70f, 77.70f, 0.85f, floorZ, kMatPierBase, kSlate);
    LocalBox(-19.68f, 52.3f, 0.85f, -19.48f, 77.7f, 0.96f, floorZ, kMatTrim, kBrass);
    LocalBox( 19.48f, 52.3f, 0.85f,  19.68f, 77.7f, 0.96f, floorZ, kMatTrim, kBrass);
    LocalBox(-19.68f, 77.48f, 0.85f,  19.68f, 77.68f, 0.96f, floorZ, kMatTrim, kBrass);
    // Twin Forseti/Muspell high-bay accent friezes + luminous strips
    LocalBox(-19.65f, 52.5f, 9.6f, -19.45f, 77.5f, 10.4f, floorZ, kMatForsetiTrim);
    LocalBox(-19.62f, 52.6f, 9.42f, -19.42f, 77.4f, 9.60f, floorZ, kMatForsetiGlow, 0xff78ecff);
    LocalBox( 19.45f, 52.5f, 9.6f,  19.65f, 77.5f, 10.4f, floorZ, kMatMuspellTrim);
    LocalBox( 19.42f, 52.6f, 9.42f,  19.62f, 77.4f, 9.60f, floorZ, kMatMuspellGlow, 0xffff9a38);

    // Hangar Perimeter Ceiling leaving a SOARING 18m x 22m SUNLIT GLASS-TRUSS SKYLIGHT (x [-9,+9], y [54,76])!
    Ceiling(floorZ, -20.3f, 51.7f,  -9.0f, 78.3f, 15.5f, kMatCeiling);
    Ceiling(floorZ,   9.0f, 51.7f,  20.3f, 78.3f, 15.5f, kMatCeiling);
    Ceiling(floorZ,  -9.0f, 51.7f,   9.0f, 54.0f, 15.5f, kMatCeiling);
    Ceiling(floorZ,  -9.0f, 76.0f,   9.0f, 78.3f, 15.5f, kMatCeiling);
    // Skylight frame curbs + warm hangar cove light strips
    LocalBox(-9.3f, 53.7f, 14.9f, -8.8f, 76.3f, 16.2f, floorZ, kMatPier, kSlate, true);
    LocalBox( 8.8f, 53.7f, 14.9f,  9.3f, 76.3f, 16.2f, floorZ, kMatPier, kSlate, true);
    LocalBox(-9.0f, 53.7f, 14.9f,  9.0f, 54.2f, 16.2f, floorZ, kMatPier, kSlate, true);
    LocalBox(-9.0f, 75.8f, 14.9f,  9.0f, 76.3f, 16.2f, floorZ, kMatPier, kSlate, true);
    LocalBox(-8.9f, 54.2f, 14.82f, -8.72f, 75.8f, 15.02f, floorZ, kMatWarmGlow, kLight);
    LocalBox( 8.72f, 54.2f, 14.82f,  8.9f, 75.8f, 15.02f, floorZ, kMatWarmGlow, kLight);
    // Longitudinal truss purlins + translucent sunlit glass roof canopy + gentle daylight shaft
    LocalBox(-4.5f - 0.14f, 54.0f, 15.35f, -4.5f + 0.14f, 76.0f, 15.75f, floorZ, kMatTrim, kBrass, true);
    LocalBox( 0.0f - 0.18f, 54.0f, 15.50f,  0.0f + 0.18f, 76.0f, 16.05f, floorZ, kMatTrim, kBrass, true);
    LocalBox( 4.5f - 0.14f, 54.0f, 15.35f,  4.5f + 0.14f, 76.0f, 15.75f, floorZ, kMatTrim, kBrass, true);
    AddGlassQuad(-8.9f, 54.1f, 15.75f, 0x28d4f0ff, 8.9f, 54.1f, 15.75f, 0x36fff4d8,
                  8.9f, 75.9f, 15.75f, 0x26d0ecff, -8.9f, 75.9f, 15.75f, 0x22c4e2fc, floorZ);
    AddGlassQuad( 6.8f, 56.0f, 14.2f, 0x1affebc0,  6.8f, 73.0f, 14.2f, 0x16ffe6b0,
                  1.5f, 75.0f,  2.2f, 0x00ffebc0,  1.5f, 58.0f,  2.2f, 0x00ffebc0, floorZ);

    // Swept steel-and-brass vaulted Hangar Arch Ribs + Lower Tension Trusses + Suspended Pendant Light Bays!
    for (float y = 56.0f; y <= 75.0f; y += 6.0f) {
        DressedColumn(-19.45f, y, 0.44f, 15.5f, floorZ);
        DressedColumn( 19.45f, y, 0.44f, 15.5f, floorZ);
        // Sculpted vaulted diagonal arch haunches sweeping cleanly inward from the walls (visible right inside frame!)
        const float wRibX[4] = {-19.65f, -18.55f, -13.60f, -19.65f};
        const float wRibZ[4] = {  5.20f,   5.20f,  11.65f,  13.85f};
        EmitSweptRibY(y - 0.36f, y + 0.36f, wRibX, wRibZ, floorZ, kMatForsetiTrim, 0xff4c7ea8);
        const float eRibX[4] = { 19.65f,  18.55f,  13.60f,  19.65f};
        const float eRibZ[4] = {  5.20f,   5.20f,  11.65f,  13.85f};
        EmitSweptRibY(y - 0.36f, y + 0.36f, eRibX, eRibZ, floorZ, kMatMuspellIron, 0xff464044);

        // Lower open-web structural steel arch tie-chord (z = 11.45..11.80, visible at eye-level entry!)
        LocalBox(-14.2f, y - 0.22f, 11.48f, 14.2f, y + 0.22f, 11.82f, floorZ, kMatPier, kSlate, true);
        LocalBox(-14.2f, y - 0.26f, 11.42f, 14.2f, y + 0.26f, 11.52f, floorZ, kMatTrim, kBrass, true);
        // Upper double-chord roof truss spanning the 40m high-bay Hangar
        LocalBox(-19.5f, y - 0.24f, 13.75f, 19.5f, y + 0.24f, 14.15f, floorZ, kMatPier, kSlate, true);
        LocalBox(-19.5f, y - 0.28f, 13.68f, 19.5f, y + 0.28f, 13.80f, floorZ, kMatTrim, kBrass, true);
        LocalBox(-19.5f, y - 0.22f, 15.05f, 19.5f, y + 0.22f, 15.45f, floorZ, kMatPier, kSlate, true);
        // Vertical web struts tying lower arch chord -> mid truss -> upper roof chord
        for (float tx = -12.0f; tx <= 12.1f; tx += 4.0f) {
            LocalBox(tx - 0.12f, y - 0.14f, 11.82f, tx + 0.12f, y + 0.14f, 13.75f, floorZ, kMatPier, kSlate);
            LocalBox(tx - 0.14f, y - 0.16f, 14.15f, tx + 0.14f, y + 0.16f, 15.05f, floorZ, kMatPier, kSlate);
        }
        // Paired suspended brushed-brass Hangar Pendant Light Bays hanging from each truss (z = 8.95..9.38m)
        for (int side = -1; side <= 1; side += 2) {
            const float px = float(side) * 6.4f;
            // Twin slim steel suspension rods
            LocalBox(px - 0.68f, y - 0.05f, 9.36f, px - 0.58f, y + 0.05f, 11.48f, floorZ, kMatStand, kSlate);
            LocalBox(px + 0.58f, y - 0.05f, 9.36f, px + 0.68f, y + 0.05f, 11.48f, floorZ, kMatStand, kSlate);
            // Sculpted dark-slate & brushed-brass pendant reflector hood + warm downward glowing diffuser bar
            LocalBox(px - 0.96f, y - 0.42f, 9.08f, px + 0.96f, y + 0.42f, 9.38f, floorZ, kMatPierBase, kSlate, true);
            LocalBox(px - 1.02f, y - 0.46f, 9.00f, px + 1.02f, y + 0.46f, 9.10f, floorZ, kMatTrim, kBrass, true);
            LocalBox(px - 0.82f, y - 0.28f, 8.92f, px + 0.82f, y + 0.28f, 9.02f, floorZ, kMatWarmGlow, kLight, true);
        }
    }

    // West Aviation Bay: 3 sculpted plinths + swept aerodynamic fin flight pylons supporting banked aircraft
    const float ay[3] = {58.0f, 65.0f, 72.0f};
    const float aTop[3] = {2.75f, 3.05f, 2.75f};
    for (unsigned i = 0; i < 3; ++i) {
        TurntablePlinth(-14.5f, ay[i], 1.75f, 0.80f, floorZ, false, kMatForsetiGlow, kMatPlinth);
        // Swept aerodynamic delta-fin pylon collar + brushed-brass cradle mount (within existing plinth footprint!)
        LocalBox(-14.95f, ay[i] - 0.65f, 0.80f, -14.05f, ay[i] + 0.65f, 1.02f, floorZ, kMatPierBase, kSlate);
        LocalBox(-14.98f, ay[i] - 0.68f, 0.98f, -14.02f, ay[i] + 0.68f, 1.06f, floorZ, kMatForsetiGlow, 0xff78ecff);
        LocalBox(-14.72f, ay[i] - 0.22f, 1.04f, -14.28f, ay[i] + 0.22f, aTop[i] - 0.10f, floorZ, kMatStand, kSlate);
        LocalBox(-14.56f, ay[i] - 0.56f, 1.04f, -14.44f, ay[i] + 0.56f, 1.95f, floorZ, kMatForsetiTrim, 0xff5a8eb8);
        LocalBox(-14.78f, ay[i] - 0.28f, aTop[i] - 0.22f, -14.22f, ay[i] + 0.28f, aTop[i] - 0.08f, floorZ, kMatTrim, kBrass);
    }

    // East Naval Bay: glowing recessed reflecting-water basin (x [8.8, 17.4], y [55.6, 74.4]) + 3 lit submerged piers
    AddContactShadow(13.1f, 65.0f, 4.3f, 9.4f, 1.25f, floorZ, 0.042f, 0.58f);
    LocalBox( 8.8f, 55.6f, 0.02f, 17.4f, 56.05f, 0.46f, floorZ, kMatWall);
    LocalBox( 8.8f, 73.95f, 0.02f, 17.4f, 74.4f, 0.46f, floorZ, kMatWall);
    LocalBox( 8.8f, 56.05f, 0.02f,  9.25f, 73.95f, 0.46f, floorZ, kMatWall);
    LocalBox(16.95f, 56.05f, 0.02f, 17.4f, 73.95f, 0.46f, floorZ, kMatWall);
    // Crisp brushed-brass rim lip + corner marine light bollards + glowing aqua underwater rim lights!
    LocalBox( 8.76f, 55.56f, 0.44f, 17.44f, 56.08f, 0.50f, floorZ, kMatTrim, kBrass);
    LocalBox( 8.76f, 73.92f, 0.44f, 17.44f, 74.44f, 0.50f, floorZ, kMatTrim, kBrass);
    LocalBox( 8.76f, 56.08f, 0.44f,  9.28f, 73.92f, 0.50f, floorZ, kMatTrim, kBrass);
    LocalBox(16.92f, 56.08f, 0.44f, 17.44f, 73.92f, 0.50f, floorZ, kMatTrim, kBrass);
    // Four corner marine lantern bollards perched on the basin coping (zero extra floor obstacle!)
    for (int cx = 0; cx < 2; ++cx) {
        for (int cy = 0; cy < 2; ++cy) {
            const float bx = cx == 0 ? 9.02f : 17.18f;
            const float by = cy == 0 ? 55.82f : 74.18f;
            LocalBox(bx - 0.22f, by - 0.22f, 0.50f, bx + 0.22f, by + 0.22f, 1.18f, floorZ, kMatPier, kSlate);
            LocalBox(bx - 0.25f, by - 0.25f, 1.18f, bx + 0.25f, by + 0.25f, 1.26f, floorZ, kMatTrim, kBrass);
            LocalBox(bx - 0.14f, by - 0.14f, 1.26f, bx + 0.14f, by + 0.14f, 1.44f, floorZ, kMatAquaGlow, kLight);
            LocalBox(bx - 0.20f, by - 0.20f, 1.44f, bx + 0.20f, by + 0.20f, 1.52f, floorZ, kMatTrim, kBrass);
        }
    }
    LocalBox( 9.25f, 56.05f, 0.28f, 16.95f, 56.18f, 0.36f, floorZ, kMatAquaGlow, kLight);
    LocalBox( 9.25f, 73.82f, 0.28f, 16.95f, 73.95f, 0.36f, floorZ, kMatAquaGlow, kLight);
    LocalBox( 9.25f, 56.18f, 0.28f,  9.38f, 73.82f, 0.36f, floorZ, kMatAquaGlow, kLight);
    LocalBox(16.82f, 56.18f, 0.28f, 16.95f, 73.82f, 0.36f, floorZ, kMatAquaGlow, kLight);
    // Longitudinal glowing aqua guide strips under the water flanking the submerged ship piers
    LocalBox(10.65f, 56.4f, 0.26f, 10.77f, 73.6f, 0.33f, floorZ, kMatAquaGlow, kLight);
    LocalBox(15.23f, 56.4f, 0.26f, 15.35f, 73.6f, 0.33f, floorZ, kMatAquaGlow, kLight);
    LocalFloorGrid(9.25f, 56.05f, 16.95f, 73.95f, 0.30f, floorZ, kMatWater, 1.8f, 0xff287898);
    const float ny[3] = {58.5f, 65.0f, 71.5f};
    for (unsigned i = 0; i < 3; ++i) {
        LocalBox(11.3f, ny[i] - 1.3f, 0.28f, 14.7f, ny[i] + 1.3f, 0.65f, floorZ, kMatPierBase, kSlate);
        LocalBox(11.24f, ny[i] - 1.36f, 0.56f, 14.76f, ny[i] + 1.36f, 0.65f, floorZ, kMatTrim, kBrass);
    }

    // North Relative Scale Alcove: architectural monument elevation grid + backlit top light trough!
    AddContactShadow(0.0f, 75.3f, 5.4f, 1.4f, 1.25f, floorZ, 0.042f, 0.60f);
    DressedColumn(-6.75f, 77.35f, 0.42f, 11.6f, floorZ);
    DressedColumn( 6.75f, 77.35f, 0.42f, 11.6f, floorZ);
    LocalBox(-7.25f, 77.05f, 11.2f, 7.25f, 77.75f, 11.85f, floorZ, kMatPierCap, 0xfff6f2e8, true);
    LocalBox(-7.15f, 76.98f, 11.10f, 7.15f, 77.12f, 11.24f, floorZ, kMatTrim, kBrass);
    // Warm recessed cornice downlight bar washing the Relative Scale wall & machines
    LocalBox(-6.20f, 77.12f, 10.95f, 6.20f, 77.32f, 11.12f, floorZ, kMatWarmGlow, kLight);
    LocalBox(-6.35f, 77.42f, 0.02f, 6.35f, 77.68f, 11.2f, floorZ, kMatPierBase, kSlate);
    // Calibrated vertical meter grid columns + satin-brass horizontal elevation bands + illuminated ticks
    for (float gx = -4.0f; gx <= 4.1f; gx += 2.0f) {
        LocalBox(gx - 0.03f, 77.38f, 0.55f, gx + 0.03f, 77.44f, 10.95f, floorZ, kMatTrim, kBrass);
    }
    for (int m = 2; m <= 10; m += 2) {
        const float hz = float(m);
        LocalBox(-6.05f, 77.35f, hz - 0.045f, 6.05f, 77.44f, hz + 0.045f, floorZ, kMatTrim, kBrass);
        LocalBox(-6.22f, 77.30f, hz - 0.08f, -5.45f, 77.42f, hz + 0.08f, floorZ, kMatWarmGlow, kLight);
        LocalBox( 5.45f, 77.30f, hz - 0.08f,  6.22f, 77.42f, hz + 0.08f, floorZ, kMatWarmGlow, kLight);
    }
    LocalBox(-5.4f, 73.9f, 0.02f, 5.4f, 76.7f, 0.55f, floorZ, kMatPlinth, 0xffdde2e6);
    LocalBox(-5.44f, 73.86f, 0.48f, 5.44f, 76.74f, 0.55f, floorZ, kMatTrim, kBrass);
    // Crisp warm rear cove uplight strip behind the 3 Relative Scale vehicles so their silhouettes pop!
    LocalBox(-5.25f, 76.56f, 0.55f, 5.25f, 76.72f, 0.65f, floorZ, kMatWarmGlow, kLight);

    // Twin Monumental Flanking Hangar Wing Banner Standards on the North Wall (eliminating blank flank walls!)
    // West-North Aviation Squadron Emblem Standard (x = -13.2, y = 77.4)
    {
        const float cx = -13.2f;
        LocalBox(cx - 3.35f, 77.44f, 1.55f, cx + 3.35f, 77.68f, 9.35f, floorZ, kMatPierBase, kSlate);
        LocalBox(cx - 3.50f, 77.36f, 1.40f, cx + 3.50f, 77.54f, 1.62f, floorZ, kMatTrim, kBrass);
        LocalBox(cx - 3.50f, 77.36f, 9.28f, cx + 3.50f, 77.54f, 9.55f, floorZ, kMatTrim, kBrass);
        LocalBox(cx - 3.50f, 77.36f, 1.62f, cx - 3.28f, 77.54f, 9.28f, floorZ, kMatTrim, kBrass);
        LocalBox(cx + 3.28f, 77.36f, 1.62f, cx + 3.50f, 77.54f, 9.28f, floorZ, kMatTrim, kBrass);
        // Recessed Forseti steel-blue banner fascia + glowing cyan top & bottom strips
        LocalBox(cx - 3.15f, 77.38f, 8.20f, cx + 3.15f, 77.50f, 9.24f, floorZ, kMatForsetiTrim);
        LocalBox(cx - 3.15f, 77.32f, 8.05f, cx + 3.15f, 77.44f, 8.22f, floorZ, kMatForsetiGlow, 0xff78ecff);
        LocalBox(cx - 3.15f, 77.32f, 1.64f, cx + 3.15f, 77.44f, 1.80f, floorZ, kMatForsetiGlow, 0xff78ecff);
        // Sculpted 3D Aerospace Wing Chevron Emblem in brushed brass & steel-blue relief!
        LocalBox(cx - 0.22f, 77.26f, 2.65f, cx + 0.22f, 77.44f, 7.25f, floorZ, kMatTrim, kBrass);
        for (int s = -1; s <= 1; s += 2) {
            const float fs = float(s);
            const float wx0[4] = {cx + fs * 0.25f, cx + fs * 2.65f, cx + fs * 2.65f, cx + fs * 0.25f};
            const float wz0[4] = {6.20f, 7.15f, 6.50f, 5.55f};
            EmitSweptRibY(77.26f, 77.44f, wx0, wz0, floorZ, kMatForsetiTrim, 0xff5a8eb8);
            const float wx1[4] = {cx + fs * 0.25f, cx + fs * 2.15f, cx + fs * 2.15f, cx + fs * 0.25f};
            const float wz1[4] = {5.15f, 5.95f, 5.40f, 4.60f};
            EmitSweptRibY(77.26f, 77.44f, wx1, wz1, floorZ, kMatTrim, kBrass);
            const float wx2[4] = {cx + fs * 0.25f, cx + fs * 1.55f, cx + fs * 1.55f, cx + fs * 0.25f};
            const float wz2[4] = {4.15f, 4.75f, 4.25f, 3.65f};
            EmitSweptRibY(77.26f, 77.44f, wx2, wz2, floorZ, kMatForsetiTrim, 0xff5a8eb8);
        }
    }
    // East-North Naval Fleet Emblem Standard (x = +13.2, y = 77.4)
    {
        const float cx = 13.2f;
        LocalBox(cx - 3.35f, 77.44f, 1.55f, cx + 3.35f, 77.68f, 9.35f, floorZ, kMatPierBase, kSlate);
        LocalBox(cx - 3.50f, 77.36f, 1.40f, cx + 3.50f, 77.54f, 1.62f, floorZ, kMatTrim, kBrass);
        LocalBox(cx - 3.50f, 77.36f, 9.28f, cx + 3.50f, 77.54f, 9.55f, floorZ, kMatTrim, kBrass);
        LocalBox(cx - 3.50f, 77.36f, 1.62f, cx - 3.28f, 77.54f, 9.28f, floorZ, kMatTrim, kBrass);
        LocalBox(cx + 3.28f, 77.36f, 1.62f, cx + 3.50f, 77.54f, 9.28f, floorZ, kMatTrim, kBrass);
        // Recessed naval bronze-slate banner fascia + glowing aqua horizon strips
        LocalBox(cx - 3.15f, 77.38f, 8.20f, cx + 3.15f, 77.50f, 9.24f, floorZ, kMatMuspellTrim);
        LocalBox(cx - 3.15f, 77.32f, 8.05f, cx + 3.15f, 77.44f, 8.22f, floorZ, kMatAquaGlow, kLight);
        LocalBox(cx - 3.15f, 77.32f, 1.64f, cx + 3.15f, 77.44f, 1.80f, floorZ, kMatAquaGlow, kLight);
        // Sculpted 3D Naval Maritime Anchor / Trident Shield Crest in brushed brass & bronze relief!
        LocalBox(cx - 0.24f, 77.26f, 2.50f, cx + 0.24f, 77.44f, 7.25f, floorZ, kMatTrim, kBrass);
        LocalBox(cx - 1.55f, 77.26f, 5.85f, cx + 1.55f, 77.44f, 6.28f, floorZ, kMatTrim, kBrass);
        LocalBox(cx - 1.70f, 77.26f, 2.85f, cx + 1.70f, 77.44f, 3.28f, floorZ, kMatTrim, kBrass);
        for (int s = -1; s <= 1; s += 2) {
            const float fs = float(s);
            const float ax[4] = {cx + fs * 1.45f, cx + fs * 2.35f, cx + fs * 2.05f, cx + fs * 1.25f};
            const float az[4] = {3.28f, 4.65f, 4.90f, 3.28f};
            EmitSweptRibY(77.26f, 77.44f, ax, az, floorZ, kMatTrim, kBrass);
            // Stylized wave bars below maritime crest
            LocalBox(cx + fs * 0.45f - 0.90f, 77.30f, 2.05f, cx + fs * 0.45f + 0.90f, 77.44f, 2.24f, floorZ, kMatAquaGlow, kLight);
        }
    }

    AudioKioskPedestal(-7.5f, 55.5f, floorZ);

    // =========================================================================
    // 6. CONCEPT ART GALLERY (Southeast: x [16, 40], y [10, 28], height 8.5 m)
    // =========================================================================
    LocalFloorGrid(16.0f, 10.0f, 40.0f, 28.0f, 0.02f, floorZ, kMatFloor, 2.2f);
    RunnerStripX(16.0f, 32.4f, 24.0f, 1.00f, floorZ);
    RunnerStripX(34.6f, 40.0f, 14.5f, 1.15f, floorZ);
    RunnerStripY(33.5f, 13.35f, 25.10f, 1.00f, floorZ);
    // West facade wall sealing Concept Art Gallery from the Outdoor Courtyard (x = 16, y [9.7..20.0])
    LocalBox(16.0f - w, 9.7f, 0.0f, 16.0f + w, 20.0f, 8.5f, floorZ, kMatWall, 0xfff5f1e8);
    LocalBox(15.35f, 9.5f, 8.05f, 16.35f, 20.1f, 8.68f, floorZ, kMatPierCap, 0xfff8f4eb, true);
    LocalBox(15.50f, 9.7f, 5.20f, 15.68f, 20.0f, 5.58f, floorZ, kMatTrim, kBrass);
    DressedColumn(15.62f, 10.1f, 0.36f, 8.05f, floorZ);
    DressedColumn(15.62f, 15.0f, 0.36f, 8.05f, floorZ);
    LocalBox(16.0f, 10.0f - w, 0.0f, 40.3f, 10.0f + w, 8.5f, floorZ, kMatWall);
    // Exterior south facade cornice, brass frieze & classical pilasters facing the Coastal Courtyard vista
    LocalBox(15.4f, 9.35f, 8.05f, 40.6f, 10.35f, 8.68f, floorZ, kMatPierCap, 0xfff8f4eb, true);
    LocalBox(15.6f, 9.52f, 5.20f, 40.2f, 9.72f, 5.58f, floorZ, kMatTrim, kBrass);
    DressedColumn(21.5f, 9.62f, 0.36f, 8.05f, floorZ);
    DressedColumn(28.5f, 9.62f, 0.36f, 8.05f, floorZ);
    LocalBox(16.0f, 28.0f - w, 0.0f, 40.3f, 28.0f + w, 8.5f, floorZ, kMatWall);
    // East wall shared with Screening Room (full 9.5 m height): portal y [11.7..17.3]
    LocalBox(40.0f - w, 7.0f, 0.0f, 40.0f + w, 11.7f, 9.5f, floorZ, kMatWall);
    PortalFrameY(40.0f, 11.7f, 17.3f, 4.6f, 9.5f, floorZ, kMatTrim, kMatWarmGlow);
    LocalBox(40.0f - w, 17.3f, 0.0f, 40.0f + w, 29.0f, 9.5f, floorZ, kMatWall);
    // Brushed-brass picture rail + warm museum gallery wall-wash glow bar along North & South art walls
    LocalBox(16.4f, 27.58f, 4.65f, 39.6f, 27.72f, 4.82f, floorZ, kMatTrim, kBrass);
    LocalBox(16.4f, 10.28f, 4.65f, 39.6f, 10.42f, 4.82f, floorZ, kMatTrim, kBrass);
    LocalBox(17.0f, 27.46f, 4.82f, 39.0f, 27.62f, 4.94f, floorZ, kMatWarmGlow, kLight);
    LocalBox(17.0f, 10.38f, 4.82f, 39.0f, 10.54f, 4.94f, floorZ, kMatWarmGlow, kLight);
    LocalBox(16.4f, 27.62f, 0.02f, 39.6f, 27.72f, 0.38f, floorZ, kMatPierBase, kSlate);
    LocalBox(16.4f, 10.28f, 0.02f, 39.6f, 10.38f, 0.38f, floorZ, kMatPierBase, kSlate);
    // Central gallery viewing bench + Audio Kiosk
    AddContactShadow(28.0f, 19.0f, 4.0f, 0.6f, 0.72f, floorZ, 0.036f, 0.44f);
    LocalBox(24.0f, 18.4f, 0.02f, 32.0f, 19.6f, 0.42f, floorZ, kMatPierBase, kSlate);
    LocalBox(23.9f, 18.35f, 0.42f, 32.1f, 19.65f, 0.54f, floorZ, kMatPlinth, 0xffe6e4dc);
    AudioKioskPedestal(19.0f, 15.0f, floorZ);
    Ceiling(floorZ, 15.7f, 9.7f, 40.0f, 28.3f, 8.5f, kMatCeiling);

    // =========================================================================
    // 7. EARLY DESIGNS VAULT (Southwest: x [-40, -16], y [10, 28], height 8.5 m)
    // =========================================================================
    LocalFloorGrid(-40.0f, 10.0f, -16.0f, 28.0f, 0.02f, floorZ, kMatFloor, 2.2f);
    RunnerStripX(-36.0f, -16.0f, 24.0f, 1.00f, floorZ);
    // East facade wall sealing Early Designs Vault from the Outdoor Courtyard (x = -16, y [9.7..20.0])
    LocalBox(-16.0f - w, 9.7f, 0.0f, -16.0f + w, 20.0f, 8.5f, floorZ, kMatWall, 0xfff5f1e8);
    LocalBox(-16.35f, 9.5f, 8.05f, -15.35f, 20.1f, 8.68f, floorZ, kMatPierCap, 0xfff8f4eb, true);
    LocalBox(-15.68f, 9.7f, 5.20f, -15.50f, 20.0f, 5.58f, floorZ, kMatTrim, kBrass);
    DressedColumn(-15.62f, 10.1f, 0.36f, 8.05f, floorZ);
    DressedColumn(-15.62f, 15.0f, 0.36f, 8.05f, floorZ);
    LocalBox(-40.3f, 10.0f - w, 0.0f, -16.0f, 10.0f + w, 8.5f, floorZ, kMatWall);
    // Exterior south facade cornice, brass frieze & classical pilasters facing the Coastal Courtyard vista
    LocalBox(-40.6f, 9.35f, 8.05f, -15.4f, 10.35f, 8.68f, floorZ, kMatPierCap, 0xfff8f4eb, true);
    LocalBox(-40.2f, 9.52f, 5.20f, -15.6f, 9.72f, 5.58f, floorZ, kMatTrim, kBrass);
    DressedColumn(-21.5f, 9.62f, 0.36f, 8.05f, floorZ);
    DressedColumn(-28.5f, 9.62f, 0.36f, 8.05f, floorZ);
    LocalBox(-40.3f, 28.0f - w, 0.0f, -16.0f, 28.0f + w, 8.5f, floorZ, kMatWall);
    LocalBox(-40.0f - w, 9.7f, 0.0f, -40.0f + w, 28.3f, 8.5f, floorZ, kMatWall);
    LocalBox(-39.6f, 27.58f, 4.65f, -16.4f, 27.72f, 4.82f, floorZ, kMatTrim, kBrass);
    LocalBox(-39.6f, 10.28f, 4.65f, -16.4f, 10.42f, 4.82f, floorZ, kMatTrim, kBrass);
    LocalBox(-39.0f, 27.46f, 4.82f, -17.0f, 27.62f, 4.94f, floorZ, kMatWarmGlow, kLight);
    LocalBox(-39.0f, 10.38f, 4.82f, -17.0f, 10.54f, 4.94f, floorZ, kMatWarmGlow, kLight);
    LocalBox(-39.6f, 27.62f, 0.02f, -16.4f, 27.72f, 0.38f, floorZ, kMatPierBase, kSlate);
    LocalBox(-39.6f, 10.28f, 0.02f, -16.4f, 10.38f, 0.38f, floorZ, kMatPierBase, kSlate);
    // Central archival viewing bench + Audio Kiosk
    AddContactShadow(-28.0f, 19.0f, 4.0f, 0.6f, 0.72f, floorZ, 0.036f, 0.44f);
    LocalBox(-32.0f, 18.4f, 0.02f, -24.0f, 19.6f, 0.42f, floorZ, kMatPierBase, kSlate);
    LocalBox(-32.1f, 18.35f, 0.42f, -23.9f, 19.65f, 0.54f, floorZ, kMatPlinth, 0xffe6e4dc);
    AudioKioskPedestal(-19.0f, 15.0f, floorZ);
    Ceiling(floorZ, -40.3f, 9.7f, -15.7f, 28.3f, 8.5f, kMatCeiling);
}

void DrawDirectoryButtons(IDirect3DDevice9 *dev, float floorZ) {
    if (!gGrandMode || !dev) return;
    if (gCurrentWing != CeMuseumTour::kWingAtrium && gCurrentWing != CeMuseumTour::kWingCourtyard) return;
    Vertex verts[CeMuseumTour::kDirectoryStops * 6];
    unsigned used = 0;
    const float wy = CeMuseumWorld::kEntranceY + CeMuseumTour::kDirectoryY - 0.18f;
    const float wz = floorZ - CeMuseumTour::kDirectoryZ;
    for (int i = 0; i < CeMuseumTour::kDirectoryStops; ++i) {
        const float wx = CeMuseumWorld::kEntranceX + CeMuseumTour::kDirectoryX + CeMuseumTour::kDirectory[i].localOffsetX;
        const bool hovered = gDirHit.valid && gDirHit.index == i;
        const DWORD col = hovered ? 0xffffec90 : 0xffcfa454;
        const float hw = 0.18f, hh = 0.09f;
        const Vertex a = {wx - hw, wy, wz - hh, col};
        const Vertex b = {wx + hw, wy, wz - hh, col};
        const Vertex c = {wx + hw, wy, wz + hh, col};
        const Vertex d = {wx - hw, wy, wz + hh, col};
        verts[used++] = a; verts[used++] = b; verts[used++] = c;
        verts[used++] = a; verts[used++] = c; verts[used++] = d;
    }
    if (used) {
        dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
        dev->SetTexture(0, 0);
        dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, used / 3, verts, sizeof(Vertex));
    }
}

} // namespace

static void FindMaterials() {
    if (!gPtr) return;
    CeMuseumTextures::LoadAll(LT.GetDevice());
    for (unsigned m = 0; m < kMatCount; ++m) {
        const CeMuseumTexId tid = kMaterials[m].tex;
        gPtr->texture[m] = tid < CE_MUSEUM_TEX_COUNT ? CeMuseumTextures::Get(tid) : NULL;
    }
}

static void ReleaseMaterials() {
    if (gPtr) {
        for (unsigned m = 0; m < kMatCount; ++m) gPtr->texture[m] = NULL;
    }
    CeMuseumTextures::ReleaseAll();
}

static void SortMaterials() {
    if (!gPtr) return;
    const unsigned triangles = gPtr->usedIndices / 3u;
    unsigned at = 0u;
    for (unsigned m = 0; m < kMatCount; ++m) {
        gPtr->triCount[m] = 0u;
        gPtr->minVert[m] = gPtr->usedVerts;
        gPtr->maxVert[m] = 0u;
    }
    for (unsigned t = 0; t < triangles; ++t) ++gPtr->triCount[gPtr->material[t]];
    for (unsigned m = 0; m < kMatCount; ++m) {
        gPtr->firstIdx[m] = at;
        at += gPtr->triCount[m] * 3u;
    }
    unsigned next[kMatCount];
    for (unsigned m = 0; m < kMatCount; ++m) next[m] = gPtr->firstIdx[m];
    for (unsigned t = 0; t < triangles; ++t) {
        const unsigned m = gPtr->material[t];
        for (unsigned j = 0; j < 3u; ++j) {
            const uint32_t vi = gPtr->indices[t * 3u + j];
            gPtr->sortedIndices[next[m]++] = vi;
            if (vi < gPtr->minVert[m]) gPtr->minVert[m] = vi;
            if (vi > gPtr->maxVert[m]) gPtr->maxVert[m] = vi;
        }
    }
}

static int ExhibitionZone(float x, float y, int previous) {
    const CeMuseumExhibition::Vec2 local = CeMuseumExhibitionHost::Local(x, y);
    const int number = CeMuseumExhibition::StandingBay(gExhibition, local, previous > 0 ? previous : 0);
    if (number > 0) return number;
    const CeMuseumExhibition::Zone* arrival = CeMuseumExhibition::ZoneFor(gExhibition, CeMuseumExhibition::kZoneArrival);
    return arrival && local.x >= arrival->min.x && local.x <= arrival->max.x &&
           local.y >= arrival->min.y && local.y <= arrival->max.y ? 0 : -1;
}

static void HouseLightReset();
void CeMuseumHallClear() {
    gExhibition = CeMuseumExhibition::Plan{};
    gExhibitionVisitor = CeMuseumExhibition::Vec2{};
    gGrandMode = false;
    gCurrentWing = CeMuseumTour::kWingCourtyard;
    gAnticipatedWing = CeMuseumTour::kWingCourtyard;
    gWingAnnounced = false;
    gHasStepClock = false;
    gDirHit = {false, -1};
    gDirFireHeld = false;
    gDirFireCaptured = false;
    gPendingJump = false;
    CeMuseumTurntable::RuntimeShutdown();
    CeMuseumArt::RuntimeShutdown();
    CeMuseumAudioKiosk::RuntimeShutdown();
#ifdef CE_MUSEUM_HAS_ARCHIVE_PEDESTAL
    std::memset(gArchiveKeyPrev, 0, sizeof gArchiveKeyPrev);
    CeArchivePedestal::RuntimeShutdown();
#endif
    ReleaseMaterials();
    HouseLightReset();
    CinemaReset();
    FreeGallery();
}

static void HallFailed(const char *why) {
    PortSessionLog(PORT_SESSION_GAME, false, "museum: the hall was not built: %s", why);
}

static void SetHall(const CeHallTour &tour, CeHallGround ground, const CeMuseumExhibition::Plan* exhibition) {
    CeMuseumHallClear();
    if (!ground) { HallFailed("no ground lookup"); return; }
    if (!tour.stops || tour.stops > 3) { HallFailed("the tour has no stops or too many"); return; }
    if (!EnsureGallery()) { HallFailed("out of memory for gallery geometry"); return; }
    gPtr->tour=tour; gPtr->ground=ground; gPtr->overflow=false; gPtr->zone=0;
    FindMaterials();
    if (exhibition) {
        if (!exhibition->valid) { HallFailed("the exhibition plan is not valid"); CeMuseumHallClear(); return; }
        float floor = 0.0f;
        if (!gPtr->ground(CeMuseumWorld::kEntranceX, CeMuseumWorld::kEntranceY, &floor)) {
            HallFailed("no ground under the entrance"); CeMuseumHallClear(); return;
        }
        gExhibition = *exhibition;
        gPtr->zone = -1;
        gGrandFloorZ = floor;
        if (PortCeMuseumAuthoredMap()) {
            gGrandMode = true;
            gGrandGraph = CeMuseumExhibition::Grand::MakeRoomGraph();
            BuildGrandComplex(floor);
            CinemaShell(true, floor, 9.5f);
            CeMuseumTurntable::RuntimeInit();
            CeMuseumArt::RuntimeInit();
            CeMuseumAudioKiosk::RuntimeInit();
#ifdef CE_MUSEUM_HAS_ARCHIVE_PEDESTAL
            CeArchivePedestal::RuntimeInit();
#endif
        } else {
            ExhibitionShell();
        }
    } else {
        if (PortCeMuseumAuthoredMap()) { ExhibitionShell(); CinemaShell(false, 0, 0); }
        Walk(Offset(tour.entrance,0,-1.5f),tour.stop[0]);
        if(tour.stops >= 2) Walk(tour.stop[0],tour.stop[1]);
        if(tour.stops == 3) ExhibitRoom();
        Entry();
        for (unsigned k=0;k<tour.stops;++k) Stand(tour.stop[k],k+1);
    }
    gPtr->valid = gPtr->usedVerts && gPtr->usedIndices && !gPtr->overflow;
    if (gPtr->valid) {
        SortMaterials();
        PortSessionLog(PORT_SESSION_GAME, false,
                       "museum: built Grand Complex (%u vertices, %u triangles, %u shadow tris, %u glass tris)",
                       gPtr->usedVerts, gPtr->usedIndices / 3u,
                       gPtr->shadowIdxCount / 3u, gPtr->glassIdxCount / 3u);
        return;
    }
    HallFailed(gPtr->overflow ? (gPtr->overflowReason ? gPtr->overflowReason : "too many pieces") : "no pieces were built");
    CeMuseumHallClear();
}

void CeMuseumHallSet(const CeHallTour& tour, CeHallGround ground) {
    SetHall(tour, ground, 0);
}

void CeMuseumHallSetExhibition(const CeHallTour& tour, CeHallGround ground,
                               const CeMuseumExhibition::Plan& exhibition) {
    SetHall(tour, ground, &exhibition);
}

bool CeMuseumHallAllowsVisitor(float x,float y) {
    if (!gPtr || !gPtr->valid) return true;
    if (gGrandMode) {
        const CeMuseumExhibition::Vec2 local = CeMuseumExhibitionHost::Local(x, y);
        return CeMuseumExhibition::Grand::WalkAt(gGrandGraph, local) != CeMuseumExhibition::kWalkBlocked;
    }
    if (gExhibition.valid) {
        const CeMuseumExhibition::Vec2 local = CeMuseumExhibitionHost::Local(x, y);
        const CeMuseumExhibition::Walk walk = CeMuseumExhibition::WalkAt(gExhibition, local);
        if (walk == CeMuseumExhibition::kWalkOutside)
            return (CeCinemaRoom::Interior(local.x, local.y) || CeCinemaRoom::InDoor(local.x, local.y)) &&
                   !CeCinemaRoom::Seat(local.x, local.y);
        return walk != CeMuseumExhibition::kWalkBlocked;
    }
    return ((!PortCeMuseumAuthoredMap() || CeMuseumWorld::Inside(x,y)) && CeHallBayAllows(gPtr->tour,{x,y}));
}

static CeMuseumTour::Wing WingFromRoom(CeMuseumExhibition::Grand::RoomId r) {
    using namespace CeMuseumExhibition::Grand;
    switch (r) {
    case kRoomCourtyard:    return CeMuseumTour::kWingCourtyard;
    case kRoomAtrium:       return CeMuseumTour::kWingAtrium;
    case kRoomForseti:      return CeMuseumTour::kWingForseti;
    case kRoomMuspell:      return CeMuseumTour::kWingMuspell;
    case kRoomHangar:       return CeMuseumTour::kWingHangar;
    case kRoomConceptArt:   return CeMuseumTour::kWingConceptArt;
    case kRoomEarlyDesigns: return CeMuseumTour::kWingEarlyDesigns;
    case kRoomCinema:       return CeMuseumTour::kWingScreeningRoom;
    default:                return gCurrentWing;
    }
}

void CeMuseumHallVisitor(float x, float y) {
    gExhibitionVisitor = CeMuseumExhibitionHost::Local(x, y);
    if (!gPtr || !gPtr->valid) return;
    CinemaVisitor(x,y);

    if (gGrandMode) {
        const CeMuseumExhibition::Grand::RoomId room =
            CeMuseumExhibition::Grand::RoomAt(gGrandGraph, gExhibitionVisitor);
        const CeMuseumTour::Wing wing = WingFromRoom(room);
        const bool wingChanged = (!gWingAnnounced || wing != gCurrentWing);
        gCurrentWing = wing;
        gWingAnnounced = true;

        const double now = CinemaClock();
        float wallDt = 0.0f;
        if (gHasStepClock && now > gLastStepClock) wallDt = float(now - gLastStepClock);
        gLastStepClock = now;
        gHasStepClock = true;
        float dt = GAME.GetRenderFrameLength();
        if (!(dt > 0.0f && dt <= 0.25f)) dt = wallDt;

        // Anticipate the adjacent 3D wing when standing near an Atrium portal (with 2 m spatial
        // hysteresis so hovering on the threshold never oscillates) while capping residency to one wing.
        CeMuseumTour::Wing turntableWing = wing;
        if (wing == CeMuseumTour::kWingAtrium) {
            if (gExhibitionVisitor.y >= 45.0f ||
                (gAnticipatedWing == CeMuseumTour::kWingHangar && gExhibitionVisitor.y >= 43.0f))
                turntableWing = CeMuseumTour::kWingHangar;
            else if ((gExhibitionVisitor.x >= 9.5f ||
                      (gAnticipatedWing == CeMuseumTour::kWingMuspell && gExhibitionVisitor.x >= 7.5f)) &&
                     gExhibitionVisitor.y >= 32.0f)
                turntableWing = CeMuseumTour::kWingMuspell;
            else if ((gExhibitionVisitor.x <= -9.5f ||
                      (gAnticipatedWing == CeMuseumTour::kWingForseti && gExhibitionVisitor.x <= -7.5f)) &&
                     gExhibitionVisitor.y >= 32.0f)
                turntableWing = CeMuseumTour::kWingForseti;
        }
        gAnticipatedWing = turntableWing;

        // Convert world view ray to visitor-local coordinates (Z positive upward) for ray-pick fixtures
        float eyeX = gExhibitionVisitor.x, eyeY = gExhibitionVisitor.y, eyeZ = 1.70f;
        float aimX = 0.0f, aimY = 1.0f, aimZ = 0.0f;
        if (Cinema::s.viewValid) {
            eyeX = Cinema::s.eye[0] - CeMuseumWorld::kEntranceX;
            eyeY = Cinema::s.eye[1] - CeMuseumWorld::kEntranceY;
            eyeZ = gGrandFloorZ - Cinema::s.eye[2];
            aimX = Cinema::s.aim[0];
            aimY = Cinema::s.aim[1];
            aimZ = -Cinema::s.aim[2];
        }

        const bool paused = GAME.IsPaused();
        const bool cinemaPlaying = Cinema::s.inRoom && Cinema::s.phase == Cinema::Playing;
        CeMuseumTurntable::RuntimeUpdate(turntableWing, dt, paused, eyeX, eyeY, eyeZ, aimX, aimY, aimZ);
        CeMuseumArt::RuntimeUpdate(wing);
        CeMuseumAudioKiosk::RuntimeUpdate(wing, paused, cinemaPlaying, eyeX, eyeY, eyeZ, aimX, aimY, aimZ);
#ifdef CE_MUSEUM_HAS_ARCHIVE_PEDESTAL
        CeArchivePedestal::RuntimeUpdate(wing, dt, paused, eyeX, eyeY, eyeZ, aimX, aimY, aimZ);
        {
            static const unsigned kArchiveKeys[6] = {
                DIK_LBRACKET, DIK_RBRACKET, DIK_PRIOR, DIK_NEXT, DIK_TAB, DIK_P
            };
            for (unsigned ki = 0; ki < 6u; ++ki) {
                const unsigned dik = kArchiveKeys[ki];
                const bool down = LT.KeyDown[dik] != 0;
                const bool rising = down && !gArchiveKeyPrev[ki];
                gArchiveKeyPrev[ki] = down;
                if (rising && !paused && !Cinema::s.inRoom)
                    CeArchivePedestal::RuntimeHandleKey(dik, false);
            }
        }
#endif
        gDirHit = (wing == CeMuseumTour::kWingAtrium)
            ? CeMuseumTour::PickDirectory(eyeX, eyeY, eyeZ, aimX, aimY, aimZ)
            : CeMuseumTour::DirectoryHit{false, -1};

        if (wingChanged && wing != CeMuseumTour::kWingScreeningRoom) {
            const CeMuseumTour::WingMeta &meta = CeMuseumTour::Meta(wing);
            PortCeMuseumLabelsTitle(meta.title, meta.subtitle);
            PortSessionLog(PORT_SESSION_GAME, false, "museum: visitor entered %s", meta.title);
        }
        return;
    }

    if (gExhibition.valid) {
        const int zone = ExhibitionZone(x, y, gPtr->zone);
        if (zone == gPtr->zone) return;
        gPtr->zone = zone;
        if (zone < 0) return;
        const int bays = int(gExhibition.bayCount);
        char title[32], guide[96];
        if (zone == 0) {
            std::snprintf(title, sizeof title, "Gallery Entrance");
            std::snprintf(guide, sizeof guide, "Display 1 is on your %s. The numbers run round the hall.",
                          gExhibition.bays[0].side < 0 ? "left" : "right");
        } else {
            std::snprintf(title, sizeof title, "Display %d", zone);
            if (zone < bays) std::snprintf(guide, sizeof guide, "Display %d is next along the runner.", zone + 1);
            else std::snprintf(guide, sizeof guide, "The last display. The runner leads back to the entrance.");
        }
        PortCeMuseumLabelsWayfinding(title, guide);
        PortSessionLog(PORT_SESSION_GAME, false, "museum: visitor reached %s", title);
        return;
    }
    const int zone = CeHallZone(gPtr->tour, {x, y}, gPtr->zone);
    if (zone==gPtr->zone) return;
    gPtr->zone=zone;
    if (zone<0) return;
    const char *title = zone==0 ? "Gallery Entrance" : zone==1 ? "Display 1" :
                        zone==2 ? "Display 2" : "Collection Annex";
    const char *guide = zone==0 ? "Follow the brass lines to the displays." :
                        zone==1 && gPtr->tour.stops>1 ? "Follow the cross aisle to Display 2." :
                        zone==2 && gPtr->tour.stops==3 ? "The white lane leads to the Collection Annex." :
                        zone==3 ? "Display 3. Follow the white lane back to the entrance." :
                        zone==2 ? "Follow the aisle back to the entrance." : "The entrance is behind you.";
    PortCeMuseumLabelsWayfinding(title,guide);
    PortSessionLog(PORT_SESSION_GAME,false,"museum: visitor reached %s",title);
}

int CeMuseumHallSignLabel(float forwardX, float forwardY, int previousLabel, int *displayNumber) {
    if (!gPtr || !gPtr->valid) {
        if (displayNumber) *displayNumber = 0;
        return -1;
    }
    if (gGrandMode) {
        // In the Forseti Forces Wing, resolve close-inspection cards for the 6 native bay exhibits.
        if (gCurrentWing == CeMuseumTour::kWingForseti && gExhibition.valid) {
            const float flen = std::sqrt(forwardX * forwardX + forwardY * forwardY);
            if (flen > 1e-5f) {
                const float fx = forwardX / flen, fy = forwardY / flen;
                int best = -1;
                float bestScore = -1.0f;
                for (unsigned i = 0; i < gExhibition.bayCount && i < CeMuseumExhibition::Grand::kNativeBayCount; ++i) {
                    const CeMuseumExhibition::Grand::NativeBaySpot &nb = gGrandGraph.nativeBays[i];
                    const float dx = nb.x - gExhibitionVisitor.x, dy = nb.y - gExhibitionVisitor.y;
                    const float d = std::sqrt(dx * dx + dy * dy);
                    if (d < 0.2f || d > 8.5f) continue;
                    const float cosA = (dx * fx + dy * fy) / d;
                    if (cosA < 0.86f) continue;
                    const float score = cosA - 0.015f * d;
                    if (score > bestScore) { bestScore = score; best = static_cast<int>(i); }
                }
                if (best >= 0) {
                    if (displayNumber) *displayNumber = best + 1;
                    return gExhibition.bays[best].stableLabel;
                }
            }
        }
        if (displayNumber) *displayNumber = 0;
        return -1;
    }
    if (gExhibition.valid) {
        int previousBay = -1;
        for (unsigned i = 0; i < gExhibition.bayCount; ++i)
            if (gExhibition.bays[i].stableLabel == previousLabel)
                previousBay = gExhibition.bays[i].number;
        const int number = CeMuseumExhibition::FocusBay(gExhibition, gExhibitionVisitor,
            {forwardX, forwardY}, previousBay);
        if (displayNumber) *displayNumber = number;
        return CeMuseumExhibition::LabelForBay(gExhibition, number);
    }

    const int label = CeHallSignLabel(gPtr->tour, gPtr->zone, {forwardX, forwardY}, previousLabel);
    if (displayNumber) *displayNumber = label >= 0 ? gPtr->zone : 0;
    return label;
}

bool CeMuseumHallFacesExit(float x, float y, float forwardX, float forwardY, bool held) {
    if (!gPtr || !gPtr->valid) return false;
    if (gGrandMode)
        return CeMuseumExhibition::Grand::FacesExit(
            CeMuseumExhibitionHost::Local(x, y), {forwardX, forwardY}, held);
    if (gExhibition.valid)
        return CeMuseumExhibition::FacesExit(gExhibition,
            CeMuseumExhibitionHost::Local(x, y), {forwardX, forwardY}, held);
    return CeHallFacesExit(gPtr->tour, {x, y}, {forwardX, forwardY}, held);
}

extern "C" void PortCeMuseumCinemaView(const float *view, unsigned present) {
    CinemaCaptureView(view, present);
}

extern "C" bool PortCeMuseumCinemaFire(bool release,float value) {
    if (gGrandMode) {
        const bool cinemaPlaying = Cinema::s.inRoom && Cinema::s.phase == Cinema::Playing;
        // Route Fire cleanly to interactive museum fixtures (Projector, Directory, Archive Pedestal, Turntable, Kiosk).
        if (Cinema::s.inRoom) return CinemaAction(release, value);
        if (gDirHit.valid && gDirHit.index >= 0 && gDirHit.index < CeMuseumTour::kDirectoryStops) {
            const CeCinemaRoom::FireIntent intent = CeCinemaRoom::FireEdge(
                release || value <= 0.0f, true, gDirFireHeld, gDirFireCaptured);
            if (intent == CeCinemaRoom::FireIntent::Operate) {
                const CeMuseumTour::Wing target = CeMuseumTour::kDirectory[gDirHit.index].target;
                const CeMuseumTour::WingMeta &m = CeMuseumTour::Meta(target);
                gJumpX = CeMuseumWorld::kEntranceX + m.jumpX;
                gJumpY = CeMuseumWorld::kEntranceY + m.jumpY;
                gJumpYaw = m.jumpYaw;
                gPendingJump = true;
                PortSessionLog(PORT_SESSION_GAME, false, "museum directory: quick-jump to %s", m.title);
                return true;
            }
            if (intent != CeCinemaRoom::FireIntent::Ordinary) return true;
        } else {
            CeCinemaRoom::FireEdge(release || value <= 0.0f, false, gDirFireHeld, gDirFireCaptured);
        }
#ifdef CE_MUSEUM_HAS_ARCHIVE_PEDESTAL
        if (CeArchivePedestal::RuntimeHandleFire(release, value)) return true;
#endif
        if (CeMuseumTurntable::RuntimeHandleFire(release, value)) return true;
        if (CeMuseumAudioKiosk::RuntimeHandleFire(release, value, cinemaPlaying)) return true;
        // Consume stray Fire clicks in the museum so visitors never fire weapons inside galleries.
        return true;
    }
    return CinemaAction(release,value);
}

extern "C" bool PortCeMuseumCinemaPanel(CeMuseumCinemaConsole::Presentation *out,
    CeMuseumCinemaConsole::MeasureText measure, void *user, float lineHeight) {
    return CinemaPresentation(out, measure, user, lineHeight);
}

extern "C" bool PortCeMuseumCinemaCue(char *title,unsigned titleCap,char *line,unsigned lineCap) {
    return CinemaCue(title,titleCap,line,lineCap);
}

extern "C" bool PortCeMuseumInteractiveAim(void) {
    if (!gPtr || !gPtr->valid || !PortCeMuseumVisitActive()) return false;
    if (Cinema::s.atConsole) return true;
    if (gGrandMode) {
        if (gDirHit.valid) return true;
#ifdef CE_MUSEUM_HAS_ARCHIVE_PEDESTAL
        if (CeArchivePedestal::RuntimeIsAimingControl()) return true;
#endif
        if (CeMuseumTurntable::RuntimeIsAimingControl()) return true;
        if (CeMuseumAudioKiosk::RuntimeIsAimingControl()) return true;
    }
    return false;
}

extern "C" bool PortCeMuseumInteractiveCard(float x, float y, float fx, float fy,
                                            char *title, unsigned titleCap,
                                            char *line, unsigned lineCap,
                                            char *note, unsigned noteCap) {
    if (!gPtr || !gPtr->valid || !gGrandMode || !PortCeMuseumVisitActive()) return false;
    if (gDirHit.valid && gDirHit.index >= 0 && gDirHit.index < CeMuseumTour::kDirectoryStops) {
        const CeMuseumTour::DirectoryStop &ds = CeMuseumTour::kDirectory[gDirHit.index];
        const CeMuseumTour::WingMeta &meta = CeMuseumTour::Meta(ds.target);
        std::snprintf(title, titleCap, "Directory - %s", meta.title);
        std::snprintf(line, lineCap, "%s", meta.subtitle);
        if (note && noteCap) std::snprintf(note, noteCap, "Press Fire to move directly to this gallery.");
        return true;
    }
    if (CeMuseumAudioKiosk::RuntimeCue(title, titleCap, line, lineCap, note, noteCap)) return true;
    const CeMuseumExhibition::Vec2 local = CeMuseumExhibitionHost::Local(x, y);
#ifdef CE_MUSEUM_HAS_ARCHIVE_PEDESTAL
    if (CeArchivePedestal::RuntimeCue(local.x, local.y, fx, fy, gCurrentWing,
                                      title, titleCap, line, lineCap, note, noteCap))
        return true;
#endif
    if (CeMuseumTurntable::RuntimeCue(local.x, local.y, fx, fy, gCurrentWing,
                                      title, titleCap, line, lineCap, note, noteCap))
        return true;
    if (CeMuseumArt::RuntimeCue(local.x, local.y, fx, fy, gCurrentWing,
                                title, titleCap, line, lineCap, note, noteCap))
        return true;
    return false;
}

extern "C" bool PortCeMuseumTakeQuickJump(float *worldX, float *worldY, float *yaw) {
    if (!gPendingJump) return false;
    gPendingJump = false;
    if (worldX) *worldX = gJumpX;
    if (worldY) *worldY = gJumpY;
    if (yaw) *yaw = gJumpYaw;
    return true;
}

// Subtle minimal neutral aim dot rendered ONLY when facing an interactive fixture.
extern "C" void PortCeMuseumDrawAimDot(float cx, float cy) {
    IDirect3DDevice9 *dev = LT.GetDevice();
    if (!dev) return;
    IDirect3DStateBlock9 *saved = NULL;
    if (FAILED(dev->CreateStateBlock(D3DSBT_ALL, &saved)) || !saved) return;
    if (FAILED(saved->Capture())) { saved->Release(); return; }
    struct DotVertex { float x, y, z, rhw; DWORD colour; };
    auto drawDiamond = [&](float r, DWORD col) {
        const DotVertex q[6] = {
            {cx, cy - r, 0.0f, 1.0f, col}, {cx + r, cy, 0.0f, 1.0f, col}, {cx, cy + r, 0.0f, 1.0f, col},
            {cx, cy - r, 0.0f, 1.0f, col}, {cx, cy + r, 0.0f, 1.0f, col}, {cx - r, cy, 0.0f, 1.0f, col},
        };
        dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, q, sizeof(DotVertex));
    };
    dev->SetVertexShader(NULL);
    dev->SetPixelShader(NULL);
    dev->SetTexture(0, NULL);
    dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    drawDiamond(5.0f, 0xb014181e);
    drawDiamond(3.0f, 0xfff5ebd4);
    saved->Apply();
    saved->Release();
}

// Sunlit golden-hour coastal sky dome + soft cloud bands + layered rocky archipelago headlands
// + shimmering sunlit coastal ocean plane rendered outdoors & through the Grand Atrium skylight.
static DWORD LerpColour(DWORD a, DWORD b, float t) {
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    auto ch = [&](unsigned sh) -> DWORD {
        const float va = float((a >> sh) & 255u), vb = float((b >> sh) & 255u);
        return static_cast<DWORD>(va + (vb - va) * t + 0.5f) << sh;
    };
    return 0xff000000u | ch(16) | ch(8) | ch(0);
}

static DWORD WithAlpha(DWORD rgb, float alpha) {
    const DWORD a = static_cast<DWORD>(Clamp01(alpha) * 255.0f + 0.5f);
    return (a << 24) | (rgb & 0x00ffffffu);
}

extern "C" void PortCeMuseumAuthoredBackdrop() {
    IDirect3DDevice9 *dev = LT.GetDevice();
    if (!dev || gMirrorPass || !PortCeMuseumAuthoredMap()) return;
    dev->Clear(0, 0, D3DCLEAR_TARGET, CeMuseumWorld::kGrandSkyHorizon, 1.0f, 0);

    IDirect3DStateBlock9 *saved = NULL;
    if (FAILED(dev->CreateStateBlock(D3DSBT_ALL, &saved)) || !saved) return;
    if (FAILED(saved->Capture())) { saved->Release(); return; }

    D3DMATRIX world = {}, view = {}, proj = {};
    world._11 = world._22 = world._33 = world._44 = 1.0f;
    RENDERINFO.GetView(&view);
    RENDERINFO.GetProjection(&proj);
    // Zero translation so the procedural coastal sky dome, islands, and ocean horizon stay infinitely distant
    view._41 = 0.0f; view._42 = 0.0f; view._43 = 0.0f;

    dev->SetVertexShader(NULL);
    dev->SetPixelShader(NULL);
    dev->SetTransform(D3DTS_WORLD, &world);
    dev->SetTransform(D3DTS_VIEW, &view);
    dev->SetTransform(D3DTS_PROJECTION, &proj);
    dev->SetTexture(0, NULL);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);

    // 1. Golden-hour Coastal Sky Dome (zenith -> luminous azure -> warm apricot horizon -> deep sea nadir)
    const float elev[] = {-1.5708f, -1.02f, -0.58f, -0.26f, -0.09f, -0.005f, 0.015f, 0.14f, 0.48f, 1.5708f};
    const DWORD ringCol[] = {
        CeMuseumWorld::kGrandSkyZenith,
        0xff2f6eb2,
        CeMuseumWorld::kGrandSkyMid,
        0xff9ac6e6,
        0xffdfd6c4,
        CeMuseumWorld::kGrandSkyHorizon,
        CeMuseumWorld::kGrandSeaHorizon,
        0xff2c6f9c,
        0xff174f78,
        CeMuseumWorld::kGrandSeaDeep,
    };
    const unsigned kSegs = 36u;
    const float R = 160.0f;
    Vertex strip[(kSegs + 1u) * 2u];

    for (unsigned r = 0; r < 9u; ++r) {
        const float e0 = elev[r], e1 = elev[r + 1u];
        const float c0 = std::cos(e0), s0 = std::sin(e0);
        const float c1 = std::cos(e1), s1 = std::sin(e1);
        for (unsigned i = 0; i <= kSegs; ++i) {
            const float a = float(i) * (6.283185307f / float(kSegs));
            const float ca = std::cos(a), sa = std::sin(a);
            auto evalCol = [&](float dx, float dy, float dz, DWORD base) -> DWORD {
                const float dot = dx * kSunDirX + dy * kSunDirY + dz * kSunDirZ;
                if (dot > 0.45f && dz <= 0.08f) {
                    const float g = (dot - 0.45f) / 0.55f;
                    return LerpColour(base, CeMuseumWorld::kGrandSunGlow, g * g * 0.72f);
                }
                // Warm coastal golden atmospheric glow along the low horizon band
                if (std::fabs(dz) < 0.12f) {
                    const float hGlow = (1.0f - std::fabs(dz) / 0.12f) * (0.18f + 0.20f * std::fmax(0.0f, dx * kSunDirX + dy * kSunDirY));
                    return LerpColour(base, CeMuseumWorld::kGrandSunGlow, hGlow);
                }
                return base;
            };
            const float x0 = c0 * ca, y0 = c0 * sa, z0 = s0;
            const float x1 = c1 * ca, y1 = c1 * sa, z1 = s1;
            strip[i * 2u + 0u] = Vertex{x0 * R, y0 * R, z0 * R, evalCol(x0, y0, z0, ringCol[r])};
            strip[i * 2u + 1u] = Vertex{x1 * R, y1 * R, z1 * R, evalCol(x1, y1, z1, ringCol[r + 1u])};
        }
        dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, kSegs * 2u, strip, sizeof(Vertex));
    }

    // Enable alpha blending for layered golden-hour cloud bands, solar corona, and coastal atmosphere
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

    // 2. Soft Layered Golden-Hour Cloud Bands spanning the coastal sky (visible outdoors & through skylights!)
    {
        struct CloudBand { float elevMin, elevMax, freq1, phase1, freq2, phase2, peakAlpha; };
        const CloudBand bands[3] = {
            {-0.50f, -0.26f, 3.0f, 0.6f, 7.0f, 1.5f, 0.68f},
            {-0.32f, -0.14f, 4.0f, 2.3f, 9.0f, 0.4f, 0.58f},
            {-0.17f, -0.05f, 5.0f, 1.1f, 11.0f, 2.7f, 0.44f},
        };
        const float cloudR = 148.0f;
        for (unsigned b = 0; b < 3u; ++b) {
            const CloudBand &cb = bands[b];
            const float eMid = 0.5f * (cb.elevMin + cb.elevMax);
            const float eRow[3] = {cb.elevMin, eMid, cb.elevMax};
            for (unsigned sr = 0; sr < 2u; ++sr) {
                for (unsigned i = 0; i <= kSegs; ++i) {
                    const float a = float(i) * (6.283185307f / float(kSegs));
                    const float wisp = 0.55f + 0.30f * std::sin(a * cb.freq1 + cb.phase1) +
                                       0.24f * std::cos(a * cb.freq2 + cb.phase2);
                    const float puff = Clamp01((wisp - 0.18f) * 1.45f);
                    const float ca = std::cos(a), sa = std::sin(a);
                    for (unsigned k = 0; k < 2u; ++k) {
                        const unsigned row = sr + k;
                        const float el = eRow[row];
                        const float ce = std::cos(el), se = std::sin(el);
                        const float dx = ce * ca, dy = ce * sa, dz = se;
                        const float sunAmt = Clamp01((dx * kSunDirX + dy * kSunDirY + dz * kSunDirZ) * 0.5f + 0.5f);
                        const DWORD litCol = LerpColour(CeMuseumWorld::kGrandCloudLight, CeMuseumWorld::kGrandSunGlow, sunAmt * 0.48f);
                        const DWORD colBase = row == 0u ? litCol : (row == 1u ? LerpColour(litCol, CeMuseumWorld::kGrandCloudShade, 0.24f)
                                                                              : CeMuseumWorld::kGrandCloudShade);
                        const float rowAlpha = row == 1u ? (cb.peakAlpha * puff) : 0.0f;
                        strip[i * 2u + k] = Vertex{dx * cloudR, dy * cloudR, dz * cloudR, WithAlpha(colBase, rowAlpha)};
                    }
                }
                dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, kSegs * 2u, strip, sizeof(Vertex));
            }
        }
    }

    // 3. Soft Glowing Solar Corona + Bright Golden Sun Disc matching Light 0 direction
    {
        const float sDist = 142.0f;
        const float sx = kSunDirX * sDist, sy = kSunDirY * sDist, sz = kSunDirZ * sDist;
        float ux = -kSunDirY, uy = kSunDirX, uz = 0.0f;
        const float ul = std::hypot(ux, uy);
        ux /= ul; uy /= ul;
        const float vx = kSunDirY * uz - kSunDirZ * uy;
        const float vy = kSunDirZ * ux - kSunDirX * uz;
        const float vz = kSunDirX * uy - kSunDirY * ux;
        Vertex fan[26];
        // Outer atmospheric golden corona
        fan[0] = Vertex{sx, sy, sz, WithAlpha(CeMuseumWorld::kGrandSunGlow, 0.55f)};
        for (unsigned i = 0; i <= 24u; ++i) {
            const float a = float(i) * (6.283185307f / 24.0f);
            const float ca = std::cos(a) * 28.0f, sa = std::sin(a) * 28.0f;
            fan[i + 1u] = Vertex{sx + ux * ca + vx * sa, sy + uy * ca + vy * sa, sz + uz * ca + vz * sa,
                                 WithAlpha(CeMuseumWorld::kGrandSunGlow, 0.0f)};
        }
        dev->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 24u, fan, sizeof(Vertex));
        // Inner bright solar disc
        fan[0] = Vertex{sx, sy, sz, CeMuseumWorld::kGrandSunCore};
        for (unsigned i = 0; i <= 24u; ++i) {
            const float a = float(i) * (6.283185307f / 24.0f);
            const float ca = std::cos(a) * 8.2f, sa = std::sin(a) * 8.2f;
            fan[i + 1u] = Vertex{sx + ux * ca + vx * sa, sy + uy * ca + vy * sa, sz + uz * ca + vz * sa,
                                 WithAlpha(CeMuseumWorld::kGrandSunGlow, 0.25f)};
        }
        dev->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 24u, fan, sizeof(Vertex));
    }

    // 4. Distant Layered Coastal Archipelago Headlands & Sea-Stacks (360-degree silhouette!)
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    {
        const unsigned kRidgeSegs = 96u;
        Vertex ridge[(kRidgeSegs + 1u) * 2u];
        // Far hazy archipelago mountain range
        const float farR = 136.0f;
        for (unsigned i = 0; i <= kRidgeSegs; ++i) {
            const float a = float(i) * (6.283185307f / float(kRidgeSegs));
            const float ca = std::cos(a), sa = std::sin(a);
            const float mask = Clamp01(0.45f + 0.55f * std::sin(a * 2.0f + 0.6f) + 0.35f * std::cos(a * 5.0f - 0.8f));
            const float peak = (2.5f + 13.5f * mask * mask) * (0.65f + 0.35f * std::sin(a * 11.0f + 1.4f));
            const float sunSide = Clamp01((ca * kSunDirX + sa * kSunDirY) * 0.5f + 0.5f);
            const DWORD peakCol = LerpColour(CeMuseumWorld::kGrandHeadlandFar, CeMuseumWorld::kGrandSunGlow, sunSide * 0.28f);
            const DWORD baseCol = LerpColour(CeMuseumWorld::kGrandSeaHorizon, CeMuseumWorld::kGrandSkyHorizon, 0.45f);
            ridge[i * 2u + 0u] = Vertex{ca * farR, sa * farR, -peak, peakCol};
            ridge[i * 2u + 1u] = Vertex{ca * farR, sa * farR,  3.2f, baseCol};
        }
        dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, kRidgeSegs * 2u, ridge, sizeof(Vertex));

        // Closer dramatic Forseti rocky headlands & island promontories (broken into distinct archipelago islands!)
        const float nearR = 122.0f;
        for (unsigned i = 0; i <= kRidgeSegs; ++i) {
            const float a = float(i) * (6.283185307f / float(kRidgeSegs));
            const float ca = std::cos(a), sa = std::sin(a);
            // Distinct coastal islands separated by open ocean channels
            const float arch = std::sin(a * 3.0f - 0.4f) + 0.55f * std::cos(a * 7.0f + 1.1f);
            const float island = Clamp01((arch - 0.15f) * 1.10f);
            const float crag = 0.72f + 0.28f * std::sin(a * 17.0f + 0.5f);
            const float h = island > 0.01f ? (1.2f + 9.2f * island * crag) : -2.5f;
            // Sunlit vs shaded rocky headland facet shading
            const float slope = std::cos(a * 3.0f - 0.4f) * 0.6f + std::sin(a * 17.0f + 0.5f) * 0.4f;
            const float sunFace = Clamp01((ca * kSunDirX + sa * kSunDirY) * 0.55f + 0.35f + slope * 0.20f);
            const DWORD cliffCol = LerpColour(CeMuseumWorld::kGrandHeadlandNear, CeMuseumWorld::kGrandHeadlandLit, sunFace);
            const DWORD waterFoot = LerpColour(CeMuseumWorld::kGrandHeadlandNear, CeMuseumWorld::kGrandSeaHorizon, 0.52f);
            ridge[i * 2u + 0u] = Vertex{ca * nearR, sa * nearR, -h, cliffCol};
            ridge[i * 2u + 1u] = Vertex{ca * nearR, sa * nearR, 2.8f, waterFoot};
        }
        dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, kRidgeSegs * 2u, ridge, sizeof(Vertex));
    }

    // 5. Sunlit Shimmering Coastal Ocean Plane extending from below the cliff terrace out to the horizon!
    {
        const float oceanZ = 12.5f; // ~12.5m below visitor eye -> iconic coastal cliffside overlook!
        const float radii[7] = {16.0f, 26.0f, 42.0f, 66.0f, 94.0f, 118.0f, 134.0f};
        const DWORD seaCols[7] = {
            0xff155478,
            0xff1a648a,
            0xff22729c,
            0xff3284ad,
            0xff4d98be,
            CeMuseumWorld::kGrandSeaHorizon,
            LerpColour(CeMuseumWorld::kGrandSeaHorizon, CeMuseumWorld::kGrandSkyHorizon, 0.35f),
        };
        const unsigned kSeaSegs = 48u;
        Vertex seaStrip[(kSeaSegs + 1u) * 2u];
        for (unsigned r = 0; r < 6u; ++r) {
            for (unsigned i = 0; i <= kSeaSegs; ++i) {
                const float a = float(i) * (6.283185307f / float(kSeaSegs));
                const float ca = std::cos(a), sa = std::sin(a);
                // Golden sun glitter reflection corridor dancing across the sunlit ocean waves
                const float azDot = ca * kSunDirX + sa * kSunDirY;
                const float spec = Clamp01((azDot - 0.58f) / 0.42f);
                const float wave0 = 0.5f + 0.5f * std::sin(a * 19.0f + float(r) * 1.7f);
                const float wave1 = 0.5f + 0.5f * std::sin(a * 19.0f + float(r + 1u) * 1.7f);
                const float shim0 = spec * spec * (0.28f + 0.48f * wave0);
                const float shim1 = spec * spec * (0.28f + 0.48f * wave1);
                const DWORD c0 = LerpColour(seaCols[r], CeMuseumWorld::kGrandSeaShimmer, shim0);
                const DWORD c1 = LerpColour(seaCols[r + 1u], CeMuseumWorld::kGrandSeaShimmer, shim1);
                seaStrip[i * 2u + 0u] = Vertex{ca * radii[r], sa * radii[r], oceanZ, c0};
                seaStrip[i * 2u + 1u] = Vertex{ca * radii[r + 1u], sa * radii[r + 1u], oceanZ, c1};
            }
            dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, kSeaSegs * 2u, seaStrip, sizeof(Vertex));
        }
    }

    saved->Apply();
    saved->Release();
}

static const float kHouseStep = 1.0f / 45.0f;
static float gHouseLight = 1.0f;
static float HouseShade(unsigned m, float house) {
    const float down = m == kMatCinemaWall || m == kMatCinemaCeiling || m == kMatStone ? 0.34f : 0.62f;
    return down + (1.0f - down) * house;
}
static unsigned gHousePresent = 0;
static bool gHouseStarted = false;
static void HouseLightReset() {
    gHouseLight = 1.0f;
    gHousePresent = 0;
    gHouseStarted = false;
}
static float HouseLight() {
    const bool film = Cinema::s.inRoom && Cinema::s.phase == Cinema::Playing;
    const float target = film ? 0.0f : 1.0f;
    const unsigned present = PortPresentCount();
    if (!gHouseStarted) { gHouseLight = target; gHousePresent = present; gHouseStarted = true; return gHouseLight; }
    const unsigned steps = present - gHousePresent;
    gHousePresent = present;
    gHouseLight = CeCinemaRoom::StepHouseLight(gHouseLight, target, steps, GAME.IsPaused(), kHouseStep);
    return gHouseLight;
}

static void SetupMuseumLightRig(IDirect3DDevice9 *dev) {
    // Explicit calibrated 3-point coastal architectural light rig inside the local D3DSBT_ALL guard:
    //   Light 0: Warm golden coastal afternoon sun key matching sky dome sun direction
    //   Light 1: Cool azure sky bounce fill from opposite WNW azimuth (luminous shadow sides!)
    //   Light 2: Subtle warm limestone floor up-bounce for coffered ceilings & arch soffits
    dev->SetRenderState(D3DRS_AMBIENT, D3DCOLOR_XRGB(30, 36, 46));

    auto setDirLight = [&](DWORD idx, float dx, float dy, float dz,
                           float dr, float dg, float db,
                           float sr, float sg, float sb) {
        const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
        D3DLIGHT9 l = {};
        l.Type = D3DLIGHT_DIRECTIONAL;
        l.Direction.x = dx / len;
        l.Direction.y = dy / len;
        l.Direction.z = dz / len;
        l.Diffuse.r = dr; l.Diffuse.g = dg; l.Diffuse.b = db; l.Diffuse.a = 1.0f;
        l.Specular.r = sr; l.Specular.g = sg; l.Specular.b = sb; l.Specular.a = 1.0f;
        dev->SetLight(idx, &l);
        dev->LightEnable(idx, TRUE);
    };

    // Light 0: Photon travel direction = -sunDir (warm golden raking coastal key!)
    setDirLight(0, -kSunDirX, -kSunDirY, -kSunDirZ,
                1.08f, 0.92f, 0.68f,
                1.00f, 0.90f, 0.70f);
    // Light 1: Cool coastal azure sky fill from opposite WNW hemisphere (clean tonal separation!)
    setDirLight(1,  0.78f, -0.48f,  0.40f,
                0.28f, 0.42f, 0.62f,
                0.24f, 0.36f, 0.54f);
    // Light 2: Subtle warm floor bounce upward onto ceilings & soffits (native -Z is upward travel!)
    setDirLight(2,  0.18f,  0.28f, -0.94f,
                0.26f, 0.23f, 0.19f,
                0.00f, 0.00f, 0.00f);

    for (DWORD i = 3; i < 8; ++i) dev->LightEnable(i, FALSE);
}

extern "C" void PortCeMuseumHallDraw() {
    if (!gPtr || !gPtr->valid || gMirrorPass || !PortCeMuseumVisitActive()) return;
    IDirect3DDevice9 *dev=LT.GetDevice();
    if (!dev) return;
    IDirect3DStateBlock9 *saved=0;
    if (FAILED(dev->CreateStateBlock(D3DSBT_ALL,&saved)) || !saved) return;
    if (FAILED(saved->Capture())) { saved->Release(); return; }
    D3DMATRIX world={},view,projection;
    world._11=world._22=world._33=world._44=1;
    RENDERINFO.GetView(&view); RENDERINFO.GetProjection(&projection);
    dev->SetVertexShader(0); dev->SetPixelShader(0);
    dev->SetTransform(D3DTS_WORLD,&world); dev->SetTransform(D3DTS_VIEW,&view); dev->SetTransform(D3DTS_PROJECTION,&projection);
    dev->SetTexture(0,0); dev->SetTexture(1,0);

    // Calibrated 3-point sun/sky/bounce lighting rig
    SetupMuseumLightRig(dev);

    D3DMATERIAL9 material = {};
    material.Diffuse.r = material.Diffuse.g = material.Diffuse.b = material.Diffuse.a = 1.0f;
    material.Ambient = material.Diffuse;
    dev->SetRenderState(D3DRS_LIGHTING,TRUE); dev->SetRenderState(D3DRS_COLORVERTEX,TRUE);
    dev->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE,D3DMCS_COLOR1);
    dev->SetRenderState(D3DRS_AMBIENTMATERIALSOURCE,D3DMCS_COLOR1);
    dev->SetRenderState(D3DRS_SPECULARMATERIALSOURCE,D3DMCS_MATERIAL);
    dev->SetRenderState(D3DRS_EMISSIVEMATERIALSOURCE,D3DMCS_MATERIAL);
    dev->SetRenderState(D3DRS_LOCALVIEWER,TRUE);
    dev->SetRenderState(D3DRS_SPECULARENABLE,TRUE);
    dev->SetRenderState(D3DRS_NORMALIZENORMALS,TRUE);
    dev->SetRenderState(D3DRS_SHADEMODE,D3DSHADE_GOURAUD);
    dev->SetRenderState(D3DRS_FOGENABLE,FALSE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE); dev->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
    dev->SetRenderState(D3DRS_ZENABLE,D3DZB_TRUE); dev->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);
    dev->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL); dev->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    dev->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID); dev->SetRenderState(D3DRS_COLORWRITEENABLE,15);
    dev->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0);
    dev->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);
    dev->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP); dev->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP);
    dev->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR); dev->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
    dev->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_LINEAR);
    dev->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);
    dev->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
    dev->SetTextureStageState(1,D3DTSS_ALPHAOP,D3DTOP_DISABLE);
    dev->SetTexture(1,0);
    dev->SetFVF(D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_DIFFUSE|D3DFVF_TEX1);
    const float house = HouseLight();
    for (unsigned m = 0; m < kMatCount; ++m) {
        if (!gPtr->triCount[m] || gPtr->minVert[m] > gPtr->maxVert[m]) continue;
        IDirect3DTexture9 *texture = gPtr->texture[m];
        dev->SetTexture(0,texture);
        dev->SetTextureStageState(0,D3DTSS_COLOROP,texture ? D3DTOP_MODULATE : D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0,D3DTSS_COLORARG1,texture ? D3DTA_TEXTURE : D3DTA_DIFFUSE);
        dev->SetTextureStageState(0,D3DTSS_COLORARG2,D3DTA_DIFFUSE);
        const float quiet = texture ? kMaterials[m].quiet : 0.0f;
        const float light = CinemaMaterial(m) ? HouseShade(m, house) : 1.0f;
        const unsigned shade = unsigned(light * 255.0f + 0.5f), keep = unsigned((1.0f - quiet) * 255.0f + 0.5f);
        dev->SetRenderState(D3DRS_TEXTUREFACTOR,D3DCOLOR_ARGB(keep,shade,shade,shade));
        unsigned stage = 1;
        if (quiet > 0.0f) {
            dev->SetTextureStageState(stage,D3DTSS_COLOROP,D3DTOP_BLENDFACTORALPHA);
            dev->SetTextureStageState(stage,D3DTSS_COLORARG1,D3DTA_CURRENT);
            dev->SetTextureStageState(stage,D3DTSS_COLORARG2,D3DTA_DIFFUSE);
            dev->SetTextureStageState(stage,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
            dev->SetTextureStageState(stage,D3DTSS_ALPHAARG1,D3DTA_CURRENT);
            ++stage;
        }
        if (shade < 255) {
            dev->SetTextureStageState(stage,D3DTSS_COLOROP,D3DTOP_MODULATE);
            dev->SetTextureStageState(stage,D3DTSS_COLORARG1,D3DTA_CURRENT);
            dev->SetTextureStageState(stage,D3DTSS_COLORARG2,D3DTA_TFACTOR);
            dev->SetTextureStageState(stage,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
            dev->SetTextureStageState(stage,D3DTSS_ALPHAARG1,D3DTA_CURRENT);
            ++stage;
        }
        dev->SetTextureStageState(stage,D3DTSS_COLOROP,D3DTOP_DISABLE);
        dev->SetTextureStageState(stage,D3DTSS_ALPHAOP,D3DTOP_DISABLE);
        material.Emissive.r = kMaterials[m].emissiveR;
        material.Emissive.g = kMaterials[m].emissiveG;
        material.Emissive.b = kMaterials[m].emissiveB;
        material.Specular.r = kMaterials[m].specR;
        material.Specular.g = kMaterials[m].specG;
        material.Specular.b = kMaterials[m].specB;
        material.Specular.a = 1.0f;
        material.Power = kMaterials[m].specPower;
        const bool hasSpec = (kMaterials[m].specR > 0.0f || kMaterials[m].specG > 0.0f || kMaterials[m].specB > 0.0f);
        dev->SetRenderState(D3DRS_SPECULARENABLE, hasSpec ? TRUE : FALSE);
        dev->SetMaterial(&material);
        const bool isInlay = (m == kMatInlay || m == kMatTrim || m == kMatNumeral ||
                              m == kMatForsetiGlow || m == kMatMuspellGlow || m == kMatWarmGlow || m == kMatAquaGlow);
        const float bias = isInlay ? -0.00002f : 0.0f, slope = isInlay ? -1.0f : 0.0f;
        dev->SetRenderState(D3DRS_DEPTHBIAS,*reinterpret_cast<const DWORD *>(&bias));
        dev->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS,*reinterpret_cast<const DWORD *>(&slope));
        const unsigned numVerts = gPtr->maxVert[m] - gPtr->minVert[m] + 1u;
        dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, gPtr->minVert[m], numVerts, gPtr->triCount[m],
                                    gPtr->sortedIndices + gPtr->firstIdx[m], D3DFMT_INDEX32,
                                    gPtr->vertices, sizeof(LitVertex));
    }
    dev->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
    dev->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
    dev->SetTextureStageState(1,D3DTSS_ALPHAOP,D3DTOP_DISABLE);
    dev->SetRenderState(D3DRS_LIGHTING,FALSE);
    dev->SetTexture(0,0);
    dev->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
    dev->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);
    dev->SetFVF(D3DFVF_XYZ|D3DFVF_DIFFUSE);

    // Alpha-blended radial soft contact-shadow decals beneath every plinth, pier, and monument!
    if (gPtr->shadowVertCount && gPtr->shadowIdxCount >= 3u) {
        const float shadowBias = -0.00003f, shadowSlope = -1.0f;
        dev->SetRenderState(D3DRS_DEPTHBIAS, *reinterpret_cast<const DWORD *>(&shadowBias));
        dev->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, *reinterpret_cast<const DWORD *>(&shadowSlope));
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0u, gPtr->shadowVertCount,
                                    gPtr->shadowIdxCount / 3u, gPtr->shadowIndices,
                                    D3DFMT_INDEX16, gPtr->shadowVerts, sizeof(OverlayVertex));
    }

    // Translucent sunlit skylight glass panes & subtle golden rotunda sunbeam curtains
    if (gPtr->glassVertCount && gPtr->glassIdxCount >= 3u) {
        const float noneBias = 0.0f;
        dev->SetRenderState(D3DRS_DEPTHBIAS, *reinterpret_cast<const DWORD *>(&noneBias));
        dev->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, *reinterpret_cast<const DWORD *>(&noneBias));
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0u, gPtr->glassVertCount,
                                    gPtr->glassIdxCount / 3u, gPtr->glassIndices,
                                    D3DFMT_INDEX16, gPtr->glassVerts, sizeof(OverlayVertex));
    }

    const float none = 0.0f;
    dev->SetRenderState(D3DRS_DEPTHBIAS,*reinterpret_cast<const DWORD *>(&none));
    dev->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS,*reinterpret_cast<const DWORD *>(&none));
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    DrawDirectoryButtons(dev, gGrandFloorZ);
    CinemaConsoleGeometry(dev);
    CinemaScreen(dev);
    saved->Apply(); saved->Release();
    LT.InvalidateAllStates();

    // Draw wing-scoped framed concept art canvases, rotating 3D Goodie exhibits, and Archive Pedestal.
    if (gGrandMode) {
        CeMuseumArt::RuntimeDraw(gGrandFloorZ);
        CeMuseumTurntable::RuntimeDraw(gGrandFloorZ);
#ifdef CE_MUSEUM_HAS_ARCHIVE_PEDESTAL
        CeArchivePedestal::RuntimeDraw(gGrandFloorZ);
#endif
    }
}
#endif
