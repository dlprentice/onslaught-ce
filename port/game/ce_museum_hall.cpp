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
#include "d3dx9.h"
#include "ce_asset_catalog.h"
#include "ce_museum_hall.h"
#include "ce_museum_exhibition_host.h"
#include "ce_museum_world.h"
#include "ce_museum_textures.h"
#include "ce_museum_tour.h"
#include "ce_museum_turntable.h"
#include "ce_museum_art.h"
#include "ce_museum_diorama.h"
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
const char *PortGameDir();
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
    {CE_MUSEUM_TEX_CINEMA_ACOUSTIC, 2.4f, 0xffc6d0da, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f,  1.0f, 0.00f, false}, // cinema proscenium wall
    {CE_MUSEUM_TEX_RUNNER_STONE,    2.2f, 0xffb89a9e, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f,  1.0f, 0.00f, false}, // cinema carpet
    {CE_MUSEUM_TEX_CINEMA_ACOUSTIC, 1.2f, 0xffe48e92, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f,  1.0f, 0.00f, false}, // cinema seats upholstery
    {CE_MUSEUM_TEX_SLATE_PIER,      1.6f, 0xffd0dae2, 0.00f, 0.00f, 0.00f, 0.38f, 0.42f, 0.46f, 30.0f, 0.00f, false}, // plinth top deck
    {CE_MUSEUM_TEX_PLINTH_MARBLE,   1.6f, 0xfff6f8fa, 0.06f, 0.06f, 0.06f, 0.30f, 0.32f, 0.34f, 24.0f, 0.00f, false}, // small exhibit plinth
    {CE_MUSEUM_TEX_SLATE_PIER,      1.4f, 0xffd6e0e8, 0.10f, 0.10f, 0.10f, 0.34f, 0.38f, 0.42f, 26.0f, 0.00f, false}, // small exhibit plinth top
    {CE_MUSEUM_TEX_SLATE_PIER,      2.0f, 0xff8d97a1, 0.00f, 0.00f, 0.00f, 0.16f, 0.18f, 0.20f, 16.0f, 0.00f, false}, // cinema screen frame
    {CE_MUSEUM_TEX_CINEMA_ACOUSTIC, 2.6f, 0xffd6e0ea, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f,  1.0f, 0.00f, false}, // cinema side walls
    {CE_MUSEUM_TEX_COFFERED_CEILING,4.0f, 0xffccd6e0, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f,  1.0f, 0.00f, false}, // cinema ceiling
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
struct SignTexVertex { float x, y, z; DWORD colour; float u, v; };
enum {
    kLitCapacity = 140000,
    kIdxCapacity = 450000,
    kShadowVertCap = 16000,
    kShadowIdxCap = 48000,
    kGlassVertCap = 8000,
    kGlassIdxCap = 24000,
    kPortalSignVertCap = 128,
    kPortalSignIdxCap = 192
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
    SignTexVertex signVerts[kPortalSignVertCap];
    uint16_t signIndices[kPortalSignIdxCap];
    unsigned signVertCount, signIdxCount;
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

extern "C" uint64_t PortCeD3dDeviceGeneration(void) __attribute__((weak));
static inline uint64_t CurrentMuseumDeviceGen() {
    return PortCeD3dDeviceGeneration ? PortCeD3dDeviceGeneration() : 0u;
}

// Static D3DPOOL_MANAGED GPU buffers for Hall architecture, contact shadows, glass, and coastal ocean.
struct HallGpuBuffers {
    IDirect3DDevice9 *device;
    uint64_t deviceGen;
    bool attempted;
    IDirect3DVertexBuffer9 *litVb;
    IDirect3DIndexBuffer9 *litIb;
    IDirect3DVertexBuffer9 *shadowVb;
    IDirect3DIndexBuffer9 *shadowIb;
    IDirect3DVertexBuffer9 *glassVb;
    IDirect3DIndexBuffer9 *glassIb;
    IDirect3DVertexBuffer9 *portalSignVb;
    IDirect3DIndexBuffer9 *portalSignIb;
    IDirect3DDevice9 *oceanDevice;
    uint64_t oceanDeviceGen;
    bool oceanAttempted;
    IDirect3DVertexBuffer9 *oceanVb;
    IDirect3DIndexBuffer9 *oceanIb;
    unsigned oceanVertCount;
    unsigned oceanIdxCount;
    IDirect3DVertexBuffer9 *headlandVb;
    IDirect3DIndexBuffer9 *headlandIb;
    unsigned headlandVertCount;
    unsigned headlandIdxCount;
};
static HallGpuBuffers gHallGpu = {};

// Genuine Lost Toys faction emblem, title, founder seal, and cover artwork loaded dynamically at runtime
// via CeAssetCatalog (plus loose splash.tga fallback for Founder Seal — zero retail bytes in git).
enum FactionTexId {
    kFactionTexForsetiMural = 0,
    kFactionTexMuspellMural = 1,
    kFactionTexTitle        = 2,
    kFactionTexFounderSeal  = 3,
    kFactionTexCoverWest    = 4,
    kFactionTexCoverEast    = 5,
    kFactionTexCount        = 6
};
struct FactionTexSlot {
    IDirect3DTexture9 *tex;
    unsigned srcW;
    unsigned srcH;
    bool fromLooseSplash;
};
static FactionTexSlot gFactionTex[kFactionTexCount] = {};
static IDirect3DTexture9 *gSkyCubeTex[5] = {};
static IDirect3DDevice9 *gFactionTexDevice = NULL;
static uint64_t gFactionTexDeviceGen = 0u;
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
        // High-contrast forged-iron foundry with glowing molten-amber pools around all 10 Muspell displays
        float forgePool = 0.0f;
        const float mx[4] = {21.8f, 27.0f, 32.2f, 37.4f};
        for (unsigned i = 0; i < 4; ++i) {
            const float dN2 = (lx - mx[i]) * (lx - mx[i]) + (ly - 48.0f) * (ly - 48.0f);
            const float dS2 = (lx - mx[i]) * (lx - mx[i]) + (ly - 34.0f) * (ly - 34.0f);
            const float pN = 1.0f - SmoothStep(1.0f, 5.8f, std::sqrt(dN2));
            const float pS = 1.0f - SmoothStep(1.0f, 5.8f, std::sqrt(dS2));
            if (pN > forgePool) forgePool = pN;
            if (pS > forgePool) forgePool = pS;
        }
        const float apseY[4] = {33.2f, 38.0f, 44.0f, 48.8f};
        for (unsigned i = 0; i < 4; ++i) {
            const float dA2 = (lx - 41.2f) * (lx - 41.2f) + (ly - apseY[i]) * (ly - apseY[i]);
            const float pA = 1.0f - SmoothStep(1.0f, 5.5f, std::sqrt(dA2));
            if (pA > forgePool) forgePool = pA;
        }
        {
            const float dNW = std::hypot(lx - 18.4f, ly - 48.2f);
            const float pNW = 1.0f - SmoothStep(1.0f, 5.2f, dNW);
            if (pNW > forgePool) forgePool = pNW;
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
        // Warm museum picture-rail gallery wall wash along perimeter walls, 3 N-S dividers, and end walls
        const float dPerim = std::fmin(std::fabs(ly - 10.4f), std::fabs(ly - 27.6f));
        const float ax = std::fabs(lx);
        const float dDivX = std::fmin(std::fabs(ax - 23.0f), std::fmin(std::fabs(ax - 28.0f), std::fabs(ax - 33.0f)));
        const float dDiv = (ly >= 13.8f && ly <= 21.6f) ? dDivX : (dDivX + std::fmin(std::fabs(ly - 14.1f), std::fabs(ly - 21.3f)));
        const float dEnd = std::fabs(ax - 39.6f);
        const float dMin = std::fmin(dPerim, std::fmin(dDiv, dEnd));
        const float artWall = 1.0f - SmoothStep(0.0f, 4.6f, dMin);
        lightR *= 0.72f + 0.34f * artWall;
        lightG *= 0.73f + 0.30f * artWall;
        lightB *= 0.75f + 0.22f * artWall;
    } else if (rid == kRoomCinema) {
        lightR *= 0.95f; lightG *= 0.95f; lightB *= 0.96f;
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
    auto byteClamp = [](float x) -> DWORD {
        int v = static_cast<int>(Clamp01(x) * 255.0f);
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        return static_cast<DWORD>(v);
    };
    const DWORD cInner = (byteClamp(effAlpha) << 24) | 0x00070b12u;
    const DWORD cMid   = (byteClamp(effAlpha * 0.48f) << 24) | 0x000a1019u;
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
    // Accent architrave occupies [clearH .. clearH + 0.46f]; header wall begins cleanly above it (zero coplanar soffit!).
    LocalBox(x0, y - w - 0.08f, clearH, x1, y + w + 0.08f, clearH + 0.46f, floorZ, accentMat, kBrass, true);
    LocalBox(x0, y - w, clearH + 0.46f, x1, y + w, wallH, floorZ, kMatHeader, 0xffede9e0, false);
    LocalBox(x0 + 0.3f, y - w - 0.11f, clearH + 0.12f, x1 - 0.3f, y + w + 0.11f, clearH + 0.24f, floorZ, glowMat, kLight);
    DressedColumn(x0 - 0.42f, y, 0.44f, wallH, floorZ);
    DressedColumn(x1 + 0.42f, y, 0.44f, wallH, floorZ);
}

void PortalFrameY(float x, float y0, float y1, float clearH, float wallH, float floorZ,
                  Material accentMat, Material glowMat = kMatWarmGlow,
                  Material eastHalfMat = kMatHeader, float eastSplitZ = 0.0f) {
    const float w = CeMuseumExhibition::kWallHalf;
    // Accent architrave owns [clearH .. clearH + 0.46f]; upper header begins at clearH + 0.46f (zero coplanar Z-fighting!).
    LocalBox(x - w - 0.08f, y0, clearH, x + w + 0.08f, y1, clearH + 0.46f, floorZ, accentMat, kBrass, true);
    const float hdrZ0 = clearH + 0.46f;
    if (eastHalfMat != kMatHeader && eastSplitZ > hdrZ0 && eastSplitZ < wallH) {
        LocalBox(x - w, y0, hdrZ0, x - 0.02f, y1, wallH, floorZ, kMatHeader, 0xffede9e0, false);
        LocalBox(x + 0.02f, y0, hdrZ0, x + w, y1, eastSplitZ, floorZ, eastHalfMat, 0xffc8c2c0, false);
        LocalBox(x + 0.02f, y0, eastSplitZ, x + w, y1, wallH, floorZ, kMatHeader, 0xffede9e0, false);
    } else {
        LocalBox(x - w, y0, hdrZ0, x + w, y1, wallH, floorZ, kMatHeader, 0xffede9e0, false);
    }
    LocalBox(x - w - 0.11f, y0 + 0.3f, clearH + 0.12f, x + w + 0.11f, y1 - 0.3f, clearH + 0.24f, floorZ, glowMat, kLight);
    DressedColumn(x, y0 - 0.42f, 0.44f, wallH, floorZ);
    DressedColumn(x, y1 + 0.42f, 0.44f, wallH, floorZ);
}

// Mount a brass-framed 3D architectural portal lintel signboard proud (>= 4cm standoff, zero z-fighting!)
// above a portal face. Emits the 3D brushed-brass bezel into the static hall mesh and appends the
// textured face quad into the single-draw-call portal signage VB.
static void AddPortalSignboard(float wallX, float wallY, float cz,
                               float nx, float ny, float width, float height,
                               float floorZ, CeMuseumPortalSignId signId) {
    if (!gPtr) return;
    const float hw = width * 0.5f;
    const float hh = height * 0.5f;
    const float bHw = hw + 0.08f;
    const float bHh = hh + 0.055f;
    constexpr float kBezelDepth = 0.08f;
    constexpr float kQuadOffset = 0.13f; // 5 cm proud of bezel, 13 cm proud of wall face

    if (std::fabs(ny) > 0.5f) {
        const float y0 = ny < 0.0f ? (wallY - kBezelDepth) : wallY;
        const float y1 = ny < 0.0f ? wallY : (wallY + kBezelDepth);
        LocalBox(wallX - bHw, y0, cz - bHh, wallX + bHw, y1, cz + bHh, floorZ, kMatTrim, kBrass, true);
    } else {
        const float x0 = nx < 0.0f ? (wallX - kBezelDepth) : wallX;
        const float x1 = nx < 0.0f ? wallX : (wallX + kBezelDepth);
        LocalBox(x0, wallY - bHw, cz - bHh, x1, wallY + bHw, cz + bHh, floorZ, kMatTrim, kBrass, true);
    }

    if (gPtr->signVertCount + 4u > kPortalSignVertCap || gPtr->signIdxCount + 6u > kPortalSignIdxCap) return;
    const CeMuseumAtlasUv uv = CeMuseumTextures::PortalSignUv(signId);
    const float rx = -ny;
    const float ry =  nx;
    const float wx = CeMuseumWorld::kEntranceX + wallX + nx * kQuadOffset;
    const float wy = CeMuseumWorld::kEntranceY + wallY + ny * kQuadOffset;
    const float wz = floorZ - cz;
    const uint16_t base = static_cast<uint16_t>(gPtr->signVertCount);
    gPtr->signVerts[gPtr->signVertCount++] = {wx - rx * hw, wy - ry * hw, wz - hh, 0xffffffffu, uv.u0, uv.v0};
    gPtr->signVerts[gPtr->signVertCount++] = {wx + rx * hw, wy + ry * hw, wz - hh, 0xffffffffu, uv.u1, uv.v0};
    gPtr->signVerts[gPtr->signVertCount++] = {wx + rx * hw, wy + ry * hw, wz + hh, 0xfff2eee6u, uv.u1, uv.v1};
    gPtr->signVerts[gPtr->signVertCount++] = {wx - rx * hw, wy - ry * hw, wz + hh, 0xfff2eee6u, uv.u0, uv.v1};
    gPtr->signIndices[gPtr->signIdxCount++] = base + 0u;
    gPtr->signIndices[gPtr->signIdxCount++] = base + 1u;
    gPtr->signIndices[gPtr->signIdxCount++] = base + 2u;
    gPtr->signIndices[gPtr->signIdxCount++] = base + 0u;
    gPtr->signIndices[gPtr->signIdxCount++] = base + 2u;
    gPtr->signIndices[gPtr->signIdxCount++] = base + 3u;
}

static void BuildAllPortalSignboards(float floorZ) {
    const float w = CeMuseumExhibition::kWallHalf;
    // Portal 0 (y = 20.0, clearH = 7.5): Courtyard <-> Grand Atrium
    // Courtyard face fits cleanly below the FE_BEA_Title2 recessed slate frieze (z = 8.54..11.96).
    AddPortalSignboard(  0.0f, 20.0f - w, 8.22f,  0.0f, -1.0f, 3.52f, 0.44f, floorZ, CE_MUSEUM_SIGN_ATRIUM);
    AddPortalSignboard(  0.0f, 20.0f + w, 8.40f,  0.0f,  1.0f, 5.12f, 0.64f, floorZ, CE_MUSEUM_SIGN_COURTYARD);
    // Portal 1 (y = 52.0, clearH = 8.0): Grand Atrium <-> Aviation & Naval Hangar
    AddPortalSignboard(  0.0f, 52.0f - w, 8.90f,  0.0f, -1.0f, 5.12f, 0.64f, floorZ, CE_MUSEUM_SIGN_HANGAR);
    AddPortalSignboard(  0.0f, 52.0f + w, 8.90f,  0.0f,  1.0f, 5.12f, 0.64f, floorZ, CE_MUSEUM_SIGN_ATRIUM);
    // Portal 2 (x = -16.0, y [38..44], clearH = 6.0): Grand Atrium <-> Forseti Forces Wing
    AddPortalSignboard(-16.0f + w, 41.0f, 6.84f,  1.0f,  0.0f, 4.48f, 0.56f, floorZ, CE_MUSEUM_SIGN_FORSETI);
    AddPortalSignboard(-16.0f - w, 41.0f, 6.84f, -1.0f,  0.0f, 4.48f, 0.56f, floorZ, CE_MUSEUM_SIGN_ATRIUM);
    // Portal 3 (x = +16.0, y [38..44], clearH = 6.0): Grand Atrium <-> Muspell Dominion Wing
    AddPortalSignboard( 16.0f - w, 41.0f, 6.84f, -1.0f,  0.0f, 4.48f, 0.56f, floorZ, CE_MUSEUM_SIGN_MUSPELL);
    AddPortalSignboard( 16.0f + w, 41.0f, 6.84f,  1.0f,  0.0f, 4.48f, 0.56f, floorZ, CE_MUSEUM_SIGN_ATRIUM);
    // Portal 4 (x = -16.0, y [21.5..26.5], clearH = 5.0): Grand Atrium <-> Early Designs Vault
    AddPortalSignboard(-16.0f + w, 24.0f, 5.82f,  1.0f,  0.0f, 4.16f, 0.52f, floorZ, CE_MUSEUM_SIGN_EARLY_DESIGNS);
    AddPortalSignboard(-16.0f - w, 24.0f, 5.82f, -1.0f,  0.0f, 4.16f, 0.52f, floorZ, CE_MUSEUM_SIGN_ATRIUM);
    // Portal 5 (x = +16.0, y [21.5..26.5], clearH = 5.0): Grand Atrium <-> Concept Art Gallery
    AddPortalSignboard( 16.0f - w, 24.0f, 5.82f, -1.0f,  0.0f, 4.16f, 0.52f, floorZ, CE_MUSEUM_SIGN_CONCEPT_ART);
    AddPortalSignboard( 16.0f + w, 24.0f, 5.82f,  1.0f,  0.0f, 4.16f, 0.52f, floorZ, CE_MUSEUM_SIGN_ATRIUM);
    // Portal 6 (x = +40.0, y [11.7..17.3], clearH = 4.6): Concept Art Gallery <-> Screening Room
    AddPortalSignboard( 40.0f - w, 14.5f, 5.42f, -1.0f,  0.0f, 4.16f, 0.52f, floorZ, CE_MUSEUM_SIGN_SCREENING);
    AddPortalSignboard( 40.0f + w, 14.5f, 5.42f,  1.0f,  0.0f, 4.16f, 0.52f, floorZ, CE_MUSEUM_SIGN_CONCEPT_ART);
}

void BuildGrandComplex(float floorZ) {
    using namespace CeMuseumExhibition::Grand;
    const float w = CeMuseumExhibition::kWallHalf;

    // =========================================================================
    // 1. COASTAL COURTYARD (x [-15.5, 15.5], y [-6.0, 20.0], OPEN SUNLIT SKY)
    // =========================================================================
    LocalFloorGrid(-15.5f, -6.0f, 15.5f, 20.0f, 0.02f, floorZ, kMatCourtyard, 2.0f);
    // Coastal cliff retaining apron strictly outside Courtyard pavers and dropped below z = -0.02f (zero Z-fighting!)
    LocalBox(-16.4f, -6.9f, -10.5f,  16.4f, -6.02f, -0.02f, floorZ, kMatPierBase, kSlate);
    LocalBox(-16.4f, -6.02f, -10.5f, -15.52f, 20.0f, -0.02f, floorZ, kMatPierBase, kSlate);
    LocalBox( 15.52f, -6.02f, -10.5f,  16.4f, 20.0f, -0.02f, floorZ, kMatPierBase, kSlate);

    // Ceremonial processional stone spine + flush perimeter loop around Hero Aquila dais (1.45m halfW matches Atrium!)
    RunnerStripY(0.0f, -5.2f, 5.09f, 1.45f, floorZ);
    RunnerStripX(-5.61f, 5.61f, 6.20f, 1.00f, floorZ);
    RunnerStripY(-4.60f, 7.31f, 13.69f, 0.90f, floorZ);
    RunnerStripY( 4.60f, 7.31f, 13.69f, 0.90f, floorZ);
    RunnerStripX(-5.61f, 5.61f, 14.80f, 1.00f, floorZ);
    RunnerStripY(0.0f, 15.91f, 20.00f, 1.45f, floorZ);

    // Open coastal stone balustrade (rhythmic balusters + warm brushed-brass top rail + lantern piers -> unobstructed ocean vista!)
    auto balustradeY = [&](float x, float y0, float y1) {
        LocalBox(x - 0.28f, y0, 0.02f, x + 0.28f, y1, 0.42f, floorZ, kMatWall, 0xffeae4d8);
        LocalBox(x - 0.33f, y0 + 0.05f, 0.42f, x + 0.33f, y1, 0.52f, floorZ, kMatPierCap, 0xfff6f2e8);
        LocalBox(x - 0.12f, y0, 1.04f, x + 0.12f, y1, 1.16f, floorZ, kMatTrim, kBrass, true);
        for (float y = y0 + 0.85f; y < y1 - 0.4f; y += 0.85f) {
            LocalBox(x - 0.08f, y - 0.08f, 0.52f, x + 0.08f, y + 0.08f, 1.04f, floorZ, kMatPierCap, 0xfff2eee5);
        }
        // Start intermediate piers cleanly north of the balustradeX corner pier at y = -6.0 and south of facade column at y = 10.25
        for (float y = y0 + 4.2f; y <= y1 - 0.8f; y += 4.6f) {
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
    balustradeY(-15.5f, -5.72f, 9.7f);
    balustradeY( 15.5f, -5.72f, 9.7f);
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

    // Two flanking Coastal Terrace Flight-Pylon Plinths at (-10.2, 0.5) & (+10.2, 0.5) south of reflecting pools
    for (int side = -1; side <= 1; side += 2) {
        const float fx = float(side) * 10.2f;
        const float fy = 0.5f;
        const Material glowM = side < 0 ? kMatForsetiGlow : kMatMuspellGlow;
        const Material trimM = side < 0 ? kMatForsetiTrim : kMatMuspellTrim;
        const Material bodyM = side < 0 ? kMatPlinth : kMatMuspellIron;
        TurntablePlinth(fx, fy, 1.40f, 0.80f, floorZ, false, glowM, bodyM);
        LocalBox(fx - 0.45f, fy - 0.52f, 0.80f, fx + 0.45f, fy + 0.52f, 0.98f, floorZ, kMatPierBase, kSlate);
        LocalBox(fx - 0.20f, fy - 0.20f, 0.98f, fx + 0.20f, fy + 0.20f, 2.26f, floorZ, kMatStand, kSlate);
        LocalBox(fx - 0.08f, fy - 0.48f, 0.98f, fx + 0.08f, fy + 0.48f, 1.75f, floorZ, trimM, kBrass);
        LocalBox(fx - 0.26f, fy - 0.26f, 2.18f, fx + 0.26f, fy + 0.26f, 2.30f, floorZ, kMatTrim, kBrass);
    }

    // Battle Engine Aquila Heritage Showcase plinths flanking the south arrival terrace (Vulcan & Flux configs)
    TurntablePlinth(-5.8f, -1.6f, 1.15f, 0.90f, floorZ, false, kMatForsetiGlow, kMatPlinth);
    TurntablePlinth( 5.8f, -1.6f, 1.15f, 0.90f, floorZ, false, kMatForsetiGlow, kMatPlinth);

    // Monumental Courtyard Hero Battle Engine Aquila Plinth at (0, 10.5)
    TurntablePlinth(0.0f, 10.5f, 2.50f, 1.35f, floorZ, true, kMatWarmGlow, kMatPlinth);

    // Grand Museum Monumental South Colonnaded Facade (y = 20.0, height 13.5 m + crowned pediment 15.8 m)
    LocalBox(-16.0f - w, 20.0f - w, 0.0f, -5.5f, 20.0f + w, 13.5f, floorZ, kMatWall, 0xfff8f4eb);
    LocalBox(  5.5f,     20.0f - w, 0.0f, 16.0f + w, 20.0f + w, 13.5f, floorZ, kMatWall, 0xfff8f4eb);
    PortalFrameX(20.0f, -5.5f, 5.5f, 7.5f, 13.5f, floorZ, kMatTrim, kMatWarmGlow);
    // Classical stepped cornice + monumental stepped coastal entablature pediment + glowing brass frieze
    LocalBox(-16.7f, 19.1f, 12.65f, 16.7f, 20.6f, 13.65f, floorZ, kMatPierCap, 0xfffbf8f0, true);
    LocalBox(-13.8f, 19.25f, 13.65f, 13.8f, 20.5f, 14.55f, floorZ, kMatWall, 0xfff8f4eb);
    LocalBox(-9.5f,  19.35f, 14.55f,  9.5f, 20.4f, 15.35f, floorZ, kMatPierCap, 0xfffbf8f0);
    LocalBox(-9.8f,  19.28f, 14.48f,  9.8f, 19.42f, 14.64f, floorZ, kMatTrim, kBrass);
    LocalBox(-16.5f, 19.45f, 7.55f, -5.5f, 19.70f, 8.10f, floorZ, kMatTrim, kBrass);
    LocalBox(  5.5f, 19.45f, 7.55f, 16.5f, 19.70f, 8.10f, floorZ, kMatTrim, kBrass);
    // Warm architectural facade downlight cove strip under cornice
    LocalBox(-15.5f, 19.30f, 12.50f, 15.5f, 19.48f, 12.66f, floorZ, kMatWarmGlow, kLight);
    // Monumental paired colonnade piers framing the Courtyard facade
    DressedColumn(-13.5f, 19.45f, 0.44f, 12.65f, floorZ);
    DressedColumn( -8.6f, 19.45f, 0.44f, 12.65f, floorZ);
    DressedColumn(  8.6f, 19.45f, 0.44f, 12.65f, floorZ);
    DressedColumn( 13.5f, 19.45f, 0.44f, 12.65f, floorZ);
    // Warm architectural lantern sconces flanking the Grand Atrium entrance portal (inner bays stay calm stone!)
    for (int side = -1; side <= 1; side += 2) {
        const float sx = float(side) * 6.35f;
        LocalBox(sx - 0.18f, 19.48f, 2.2f, sx + 0.18f, 19.70f, 4.6f, floorZ, kMatPier, kSlate);
        LocalBox(sx - 0.10f, 19.40f, 2.4f, sx + 0.10f, 19.50f, 4.4f, floorZ, kMatWarmGlow, kLight);
    }
    // 1. Subtle recessed dark-slate frieze band + brass trim rim on the South Portal tympanum (x=0, z=10.25m)
    //    backing the metallic Battle Engine Aquila title (y=19.58, >= 4cm proud of slate at 19.62).
    LocalBox(-3.72f, 19.65f, 8.54f, 3.72f, 19.70f, 11.96f, floorZ, kMatTrim, kBrass);
    LocalBox(-3.60f, 19.62f, 8.60f, 3.60f, 19.65f, 11.90f, floorZ, kMatPierBase, 0xff161c22u);
    // 2. Obsidian-and-brass architectural medallion at the pediment apex (x=0, z=14.50m, frame 19.20, seal 19.16),
    //    strictly clearing the y=19.10, z=12.65..13.65 cornice with warm uplight tie-in!
    LocalBox(-0.82f, 19.18f, 13.66f, 0.82f, 19.26f, 13.72f, floorZ, kMatWarmGlow, kLight);
    LocalBox(-0.76f, 19.20f, 13.84f, 0.76f, 19.36f, 15.16f, floorZ, kMatPierBase, 0xff161c22u);
    LocalBox(-0.88f, 19.17f, 13.72f, 0.88f, 19.38f, 13.84f, floorZ, kMatTrim, kBrass, true);
    LocalBox(-0.88f, 19.17f, 15.16f, 0.88f, 19.38f, 15.28f, floorZ, kMatTrim, kBrass, true);
    LocalBox(-0.88f, 19.17f, 13.84f, -0.76f, 19.38f, 15.16f, floorZ, kMatTrim, kBrass);
    LocalBox( 0.76f, 19.17f, 13.84f,  0.88f, 19.38f, 15.16f, floorZ, kMatTrim, kBrass);
    // 3. Twin deep recessed bronze/slate shadowbox frames in the outer colonnade bays (x = +/-11.05m, z = 4.10m)
    //    strictly between columns +/-8.6 & +/-13.5: wall 19.70 -> frame 19.65 -> mat 19.62 -> art 19.59 (>= 3cm).
    for (int side = -1; side <= 1; side += 2) {
        const float cx = float(side) * 11.05f;
        LocalBox(cx - 1.78f, 19.65f, 2.74f, cx + 1.78f, 19.70f, 5.46f, floorZ, kMatTrim, kBrass);
        LocalBox(cx - 1.66f, 19.62f, 2.86f, cx + 1.66f, 19.65f, 5.34f, floorZ, kMatPierBase, 0xff161c22u);
        LocalBox(cx - 1.35f, 19.56f, 5.46f, cx + 1.35f, 19.68f, 5.56f, floorZ, kMatWarmGlow, kLight, true);
    }

    // =========================================================================
    // 2. SKYLIT GRAND ATRIUM ROTUNDA (x [-16, 16], y [20, 52], height 13.5 m)
    // =========================================================================
    LocalFloorGrid(-16.0f, 20.0f, 16.0f, 52.0f, 0.02f, floorZ, kMatFloor, 2.0f);

    // Central Brass Compass Rose & Rotunda Mosaic Floor Inlay at (0, 36.0) — outer north border flush with y = 41.0 runners!
    LocalFloorGrid(-5.20f, 30.04f, 5.20f, 41.96f, 0.048f, floorZ, kMatInlay, 2.0f, 0xffd0d6dc);
    LocalFloorGrid(-5.50f, 29.74f, 5.50f, 30.04f, 0.062f, floorZ, kMatTrim, 3.0f, kBrass);
    LocalFloorGrid(-5.50f, 41.96f, 5.50f, 42.26f, 0.062f, floorZ, kMatTrim, 3.0f, kBrass);
    LocalFloorGrid(-5.50f, 30.04f, -5.20f, 41.96f, 0.062f, floorZ, kMatTrim, 3.0f, kBrass);
    LocalFloorGrid( 5.20f, 30.04f,  5.50f, 41.96f, 0.062f, floorZ, kMatTrim, 3.0f, kBrass);
    // Inner octagonal honed marble rotunda medallion around the Archive Centerpiece Pedestal
    LocalOctagonTier(0.0f, 36.0f, 4.25f, 0.02f, 0.068f, floorZ, kMatPlinth, 0xffeef2f5, true);
    // Cardinal brass compass beams radiating across the rotunda medallion
    LocalFloorGrid(-5.0f, 35.72f, 5.0f, 36.28f, 0.076f, floorZ, kMatTrim, 2.5f, kBrass);
    LocalFloorGrid(-0.28f, 30.24f, 0.28f, 41.76f, 0.076f, floorZ, kMatTrim, 2.5f, kBrass);

    // Flush, non-overlapping floor runners connecting Rotunda to ALL 6 Atrium portals (unified 1.45 m spine!)
    RunnerStripY(0.0f, 20.00f, 29.74f, 1.45f, floorZ);      // South -> Courtyard (flush 1.45 m at y = 20.0!)
    RunnerStripY(0.0f, 42.26f, 52.00f, 1.45f, floorZ);      // North -> Hangar (flush 1.45 m at y = 52.0!)
    RunnerStripX(-16.0f, -5.50f, 41.0f, 1.15f, floorZ);     // West -> Forseti (flush 1.15 m at x = -16.0!)
    RunnerStripX(  5.50f, 16.0f, 41.0f, 1.15f, floorZ);     // East -> Muspell (flush 1.15 m at x = +16.0!)
    RunnerStripX(-16.0f, -1.56f, 24.0f, 1.00f, floorZ);     // Southwest -> Early Designs (flush at spine -1.56 m!)
    RunnerStripX(  1.56f, 16.0f, 24.0f, 1.00f, floorZ);     // Southeast -> Concept Art (flush at spine +1.56 m!)

    // GRAND ATRIUM ROTUNDA CENTERPIECE PEDESTAL at (0.0, 36.0), footprint [-2.0, 34.0]..[2.0, 38.0], height 1.05m
    AddContactShadow(0.0f, 36.0f, 2.00f, 2.00f, 1.45f, floorZ, 0.082f, 0.50f);
    LocalBox(-2.00f, 34.00f, 0.068f, 2.00f, 38.00f, 0.22f, floorZ, kMatPierBase, kSlate);
    LocalOctagonTier(0.0f, 36.0f, 1.98f, 0.22f, 0.56f, floorZ, kMatPlinth, 0xfff2f5f8, true);
    LocalOctagonTier(0.0f, 36.0f, 2.00f, 0.48f, 0.56f, floorZ, kMatTrim, kBrass, false);
    LocalBox(-1.60f, 34.40f, 0.56f, 1.60f, 37.60f, 0.94f, floorZ, kMatPlinth, 0xffeef2f6);
    LocalBox(-1.64f, 34.36f, 0.88f, 1.64f, 37.64f, 0.96f, floorZ, kMatWarmGlow, kLight);
    LocalBox(-1.68f, 34.32f, 0.96f, 1.68f, 37.68f, 1.03f, floorZ, kMatTrim, kBrass);
    LocalBox(-1.60f, 34.40f, 0.98f, 1.60f, 37.60f, 1.05f, floorZ, kMatPlinthTop, kSlate);

    // North Atrium Heritage Plinths (Aquila Blazer Siege Config & Lost Toys Panorama Monument)
    TurntablePlinth(-5.4f, 48.0f, 1.25f, 0.95f, floorZ, false, kMatForsetiGlow, kMatPlinth);
    TurntablePlinth( 5.4f, 48.0f, 1.25f, 0.95f, floorZ, false, kMatWarmGlow, kMatPlinth);

    // Grounded soft contact shadows under all 4 glass-domed living tabletop battle diorama tables
    for (unsigned di = 0; di < CeMuseumDiorama::kDioramaCount; ++di) {
        const CeMuseumDiorama::DioramaSpec &ds = CeMuseumDiorama::kDioramas[di];
        AddContactShadow(ds.localX, ds.localY, CeMuseumDiorama::kTableRadius, CeMuseumDiorama::kTableRadius,
                         1.15f, floorZ, 0.042f, 0.52f);
    }

    // Sleek relocated West Entrance Flank Directory Console at (kDirectoryX = -5.4, kDirectoryY = 26.8)
    {
        const float dx = CeMuseumTour::kDirectoryX;
        const float dy = CeMuseumTour::kDirectoryY;
        AddContactShadow(dx, dy, 1.65f, 0.35f, 0.72f, floorZ, 0.036f, 0.46f);
        LocalFloorGrid(dx - 1.95f, dy - 0.65f, dx + 1.95f, dy + 0.65f, 0.044f, floorZ, kMatInlay, 2.0f, 0xffd0d6dc);
        LocalBox(dx - 1.60f, dy - 0.32f, 0.04f, dx + 1.60f, dy + 0.32f, 0.18f, floorZ, kMatPierBase, kSlate);
        LocalBox(dx - 1.46f, dy - 0.22f, 0.18f, dx + 1.46f, dy + 0.22f, 0.98f, floorZ, kMatStand, kSlate);
        LocalBox(dx - 1.50f, dy - 0.26f, 0.94f, dx + 1.50f, dy + 0.26f, 1.00f, floorZ, kMatWarmGlow, kLight);
        LocalBox(dx - 1.62f, dy - 0.32f, 1.00f, dx + 1.62f, dy + 0.32f, 1.13f, floorZ, kMatTrim, kBrass);
        LocalBox(dx - 1.54f, dy - 0.14f, 1.13f, dx + 1.54f, dy + 0.18f, 1.38f, floorZ, kMatPierBase, kSlate);
        LocalBox(dx - 1.58f, dy - 0.16f, 1.38f, dx + 1.58f, dy + 0.20f, 1.44f, floorZ, kMatTrim, kBrass);
        // Upper eye-level architectural floor-plan map housing (strictly inside console obstacle footprint)
        LocalBox(dx - 0.74f, dy - 0.08f, 1.50f, dx + 0.74f, dy + 0.16f, 2.42f, floorZ, kMatPierBase, 0xff161c22u);
        LocalBox(dx - 0.84f, dy - 0.11f, 1.44f, dx + 0.84f, dy + 0.18f, 1.50f, floorZ, kMatTrim, kBrass, true);
        LocalBox(dx - 0.86f, dy - 0.14f, 2.42f, dx + 0.86f, dy + 0.18f, 2.50f, floorZ, kMatTrim, kBrass, true);
        LocalBox(dx - 0.84f, dy - 0.11f, 1.50f, dx - 0.74f, dy + 0.18f, 2.42f, floorZ, kMatTrim, kBrass);
        LocalBox(dx + 0.74f, dy - 0.11f, 1.50f, dx + 0.84f, dy + 0.18f, 2.42f, floorZ, kMatTrim, kBrass);
        LocalBox(dx - 0.60f, dy - 0.135f, 2.385f, dx + 0.60f, dy - 0.085f, 2.42f, floorZ, kMatWarmGlow, kLight, true);
    }

    // Four monumental clerestory skylight columns with upper arched tie-beams
    DressedColumn(-8.2f, 28.2f, 0.54f, 13.5f, floorZ);
    DressedColumn( 8.2f, 28.2f, 0.54f, 13.5f, floorZ);
    DressedColumn(-8.2f, 43.8f, 0.54f, 13.5f, floorZ);
    DressedColumn( 8.2f, 43.8f, 0.54f, 13.5f, floorZ);

    // Atrium Perimeter Ceiling strictly within Atrium bounds [-16..16] x [20..52], leaving 16m x 16m open skylight
    Ceiling(floorZ, -16.0f, 20.0f,  -8.0f, 52.0f, 13.5f, kMatCeiling);
    Ceiling(floorZ,   8.0f, 20.0f,  16.0f, 52.0f, 13.5f, kMatCeiling);
    Ceiling(floorZ,  -8.0f, 20.0f,   8.0f, 28.0f, 13.5f, kMatCeiling);
    Ceiling(floorZ,  -8.0f, 44.0f,   8.0f, 52.0f, 13.5f, kMatCeiling);
    // Clerestory lantern curb + warm recessed cove lighting + brushed-brass glass-skylight mullions
    LocalBox(-8.3f, 27.7f, 13.0f,  8.3f, 28.2f, 14.8f, floorZ, kMatPierCap, 0xfff6f2ea, true);
    LocalBox(-8.3f, 43.8f, 13.0f,  8.3f, 44.3f, 14.8f, floorZ, kMatPierCap, 0xfff6f2ea, true);
    LocalBox(-8.3f, 28.2f, 13.0f, -7.8f, 43.8f, 14.8f, floorZ, kMatPierCap, 0xfff6f2ea, true);
    LocalBox( 7.8f, 28.2f, 13.0f,  8.3f, 43.8f, 14.8f, floorZ, kMatPierCap, 0xfff6f2ea, true);
    LocalBox(-7.8f, 28.18f, 13.12f,  7.8f, 28.34f, 13.38f, floorZ, kMatWarmGlow, kLight);
    LocalBox(-7.8f, 43.66f, 13.12f,  7.8f, 43.82f, 13.38f, floorZ, kMatWarmGlow, kLight);
    LocalBox(-7.82f, 28.3f, 13.12f, -7.66f, 43.7f, 13.38f, floorZ, kMatWarmGlow, kLight);
    LocalBox( 7.66f, 28.3f, 13.12f,  7.82f, 43.7f, 13.38f, floorZ, kMatWarmGlow, kLight);
    for (float pos = -4.0f; pos <= 4.1f; pos += 4.0f) {
        LocalBox(-7.9f, 36.0f + pos - 0.14f, 13.8f, 7.9f, 36.0f + pos + 0.14f, 14.35f, floorZ, kMatTrim, kBrass, true);
        LocalBox(pos - 0.14f, 28.1f, 13.8f, pos + 0.14f, 43.9f, 14.35f, floorZ, kMatTrim, kBrass, true);
    }
    AddGlassQuad(-7.8f, 28.2f, 14.15f, 0x26dff2ff,  7.8f, 28.2f, 14.15f, 0x38fff3d6,
                  7.8f, 43.8f, 14.15f, 0x26d8eeff, -7.8f, 43.8f, 14.15f, 0x20c8e4fc, floorZ);
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

    // Atrium South Wall interior gold dado frieze & slate skirting matching all 4 Atrium walls
    LocalBox(-15.56f, 20.30f, 6.0f, -6.10f, 20.44f, 6.45f, floorZ, kMatTrim, kBrass);
    LocalBox(  6.10f, 20.30f, 6.0f, 15.56f, 20.44f, 6.45f, floorZ, kMatTrim, kBrass);
    LocalBox(-15.56f, 20.30f, 0.02f, -6.10f, 20.42f, 0.45f, floorZ, kMatPierBase, kSlate);
    LocalBox(  6.10f, 20.30f, 0.02f, 15.56f, 20.42f, 0.45f, floorZ, kMatPierBase, kSlate);

    // Atrium West Wall (x = -16.0): cohesive warm limestone with matching continuous gold dado frieze;
    // Forseti steel-blue accent is focused strictly on the Forseti Wing portal frame.
    LocalBox(-16.0f - w, 20.0f + w, 0.0f, -16.0f + w, 21.5f, 13.5f, floorZ, kMatWall);
    PortalFrameY(-16.0f, 21.5f, 26.5f, 5.0f, 13.5f, floorZ, kMatTrim, kMatWarmGlow);
    LocalBox(-16.0f - w, 26.5f, 0.0f, -16.0f + w, 38.0f, 13.5f, floorZ, kMatWall);
    PortalFrameY(-16.0f, 38.0f, 44.0f, 6.0f, 13.5f, floorZ, kMatForsetiTrim, kMatForsetiGlow);
    LocalBox(-16.0f - w, 44.0f, 0.0f, -16.0f + w, 52.0f - w, 13.5f, floorZ, kMatWall);
    LocalBox(-15.70f, 27.2f, 6.0f, -15.56f, 37.3f, 6.45f, floorZ, kMatTrim, kBrass);
    LocalBox(-15.70f, 27.2f, 0.02f, -15.56f, 37.3f, 0.45f, floorZ, kMatPierBase, kSlate);
    LocalBox(-15.70f, 44.7f, 6.0f, -15.56f, 51.56f, 6.45f, floorZ, kMatTrim, kBrass);
    LocalBox(-15.70f, 44.7f, 0.02f, -15.56f, 51.56f, 0.45f, floorZ, kMatPierBase, kSlate);

    // Atrium East Wall (x = +16.0): cohesive warm limestone with matching continuous gold dado frieze on Atrium face,
    // 4 cm cavity separation from Muspell forged iron face, and Dominion accent focused strictly on the Muspell portal frame.
    LocalBox(16.0f - w, 20.0f + w, 0.0f, 16.0f + w, 21.5f, 13.5f, floorZ, kMatWall);
    PortalFrameY(16.0f, 21.5f, 26.5f, 5.0f, 13.5f, floorZ, kMatTrim, kMatWarmGlow);
    LocalBox(16.0f - w, 26.5f, 0.0f, 16.0f + w, 30.0f, 13.5f, floorZ, kMatWall);
    LocalBox(16.0f - w, 30.0f, 0.0f, 15.98f,    38.0f, 13.5f, floorZ, kMatWall);
    LocalBox(16.02f,    30.0f, 0.0f, 16.0f + w, 38.0f, 10.5f, floorZ, kMatMuspellWall);
    LocalBox(16.02f,    30.0f, 10.5f, 16.0f + w, 38.0f, 13.5f, floorZ, kMatWall);
    PortalFrameY(16.0f, 38.0f, 44.0f, 6.0f, 13.5f, floorZ, kMatMuspellTrim, kMatMuspellGlow, kMatMuspellWall, 10.5f);
    LocalBox(16.0f - w, 44.0f, 0.0f, 15.98f,    52.0f - w, 13.5f, floorZ, kMatWall);
    LocalBox(16.02f,    44.0f, 0.0f, 16.0f + w, 52.0f - w, 10.5f, floorZ, kMatMuspellWall);
    LocalBox(16.02f,    44.0f, 10.5f, 16.0f + w, 52.0f - w, 13.5f, floorZ, kMatWall);
    LocalBox(15.56f, 27.2f, 6.0f, 15.70f, 37.3f, 6.45f, floorZ, kMatTrim, kBrass);
    LocalBox(15.56f, 27.2f, 0.02f, 15.70f, 37.3f, 0.45f, floorZ, kMatPierBase, kSlate);
    LocalBox(15.56f, 44.7f, 6.0f, 15.70f, 51.56f, 6.45f, floorZ, kMatTrim, kBrass);
    LocalBox(15.56f, 44.7f, 0.02f, 15.70f, 51.56f, 0.45f, floorZ, kMatPierBase, kSlate);

    // Atrium North Wall (y = 52.0 +/- w): strictly clamped to [-16.0+w..-5.0] and [5.0..16.0-w] (zero overlap with Forseti/Muspell!)
    LocalBox(-16.0f + w, 52.0f - w, 0.0f, -5.0f,     52.0f + w, 15.5f, floorZ, kMatWall);
    PortalFrameX(52.0f, -5.0f, 5.0f, 8.0f, 15.5f, floorZ, kMatTrim, kMatWarmGlow);
    LocalBox(  5.0f,     52.0f - w, 0.0f, 16.0f - w, 52.0f + w, 15.5f, floorZ, kMatWall);
    // Architectural dado frieze, skirting & framed classical niches flanking the Hangar portal (official retail crests render inside!)
    LocalBox(-15.6f, 51.56f, 6.0f, -5.9f, 51.70f, 6.45f, floorZ, kMatTrim, kBrass);
    LocalBox(  5.9f, 51.56f, 6.0f, 15.6f, 51.70f, 6.45f, floorZ, kMatTrim, kBrass);
    LocalBox(-15.6f, 51.58f, 0.02f, -5.9f, 51.70f, 0.45f, floorZ, kMatPierBase, kSlate);
    LocalBox(  5.9f, 51.58f, 0.02f, 15.6f, 51.70f, 0.45f, floorZ, kMatPierBase, kSlate);
    for (int side = -1; side <= 1; side += 2) {
        const float cx = float(side) * 10.8f;
        LocalBox(cx - 2.6f, 51.54f, 1.30f, cx + 2.6f, 51.70f, 5.20f, floorZ, kMatPierBase, kSlate);
        LocalBox(cx - 2.72f, 51.48f, 1.20f, cx + 2.72f, 51.62f, 1.38f, floorZ, kMatTrim, kBrass);
        LocalBox(cx - 2.72f, 51.48f, 5.12f, cx + 2.72f, 51.62f, 5.30f, floorZ, kMatTrim, kBrass);
        LocalBox(cx - 2.72f, 51.48f, 1.38f, cx - 2.54f, 51.62f, 5.12f, floorZ, kMatTrim, kBrass);
        LocalBox(cx + 2.54f, 51.48f, 1.38f, cx + 2.72f, 51.62f, 5.12f, floorZ, kMatTrim, kBrass);
        LocalBox(cx - 1.80f, 51.44f, 4.92f, cx + 1.80f, 51.56f, 5.06f, floorZ, kMatWarmGlow, kLight);
    }

    // =========================================================================
    // 3. FORSETI FORCES WING (West: x [-44, -16], y [30, 52], height 10.5 m)
    // =========================================================================
    LocalFloorGrid(-44.0f, 30.0f, -16.0f, 52.0f, 0.02f, floorZ, kMatFloor, 2.0f);
    RunnerStripX(-40.0f, -16.0f, 41.0f, 1.15f, floorZ);
    LocalFloorGrid(-39.5f, 39.62f, -16.5f, 39.72f, 0.068f, floorZ, kMatForsetiGlow, 4.0f, 0xff78ecff);
    LocalFloorGrid(-39.5f, 42.28f, -16.5f, 42.38f, 0.068f, floorZ, kMatForsetiGlow, 4.0f, 0xff78ecff);

    // Strictly non-overlapping Forseti perimeter walls
    LocalBox(-44.0f + w, 30.0f - w, 0.0f, -16.0f - w, 30.0f + w, 10.5f, floorZ, kMatWall);
    LocalBox(-44.0f + w, 52.0f - w, 0.0f, -16.0f + w, 52.0f,     10.5f, floorZ, kMatWall);
    LocalBox(-44.0f + w, 52.0f,     0.0f, -20.0f - w, 52.0f + w, 10.5f, floorZ, kMatWall);
    LocalBox(-44.0f - w, 30.0f - w, 0.0f, -44.0f + w, 52.0f + w, 10.5f, floorZ, kMatWall);

    // Sculpted Forseti steel-blue architraves, crisp slate wainscot & luminous cyan cove light strips
    LocalBox(-43.6f, 30.32f, 6.25f, -16.4f, 30.58f, 7.25f, floorZ, kMatForsetiTrim);
    LocalBox(-43.6f, 51.42f, 6.25f, -16.4f, 51.68f, 7.25f, floorZ, kMatForsetiTrim);
    LocalBox(-43.68f, 30.4f, 6.25f, -43.42f, 51.6f, 7.25f, floorZ, kMatForsetiTrim);
    LocalBox(-43.5f, 30.34f, 6.06f, -16.5f, 30.62f, 6.24f, floorZ, kMatForsetiGlow, 0xff78ecff);
    LocalBox(-43.5f, 51.38f, 6.06f, -16.5f, 51.66f, 6.24f, floorZ, kMatForsetiGlow, 0xff78ecff);
    LocalBox(-43.66f, 30.5f, 6.06f, -43.38f, 51.5f, 6.24f, floorZ, kMatForsetiGlow, 0xff78ecff);
    LocalBox(-43.6f, 30.32f, 0.02f, -16.4f, 30.50f, 0.72f, floorZ, kMatPier);
    LocalBox(-43.6f, 51.50f, 0.02f, -16.4f, 51.68f, 0.72f, floorZ, kMatPier);
    LocalBox(-43.5f, 30.34f, 0.72f, -16.5f, 30.54f, 0.84f, floorZ, kMatForsetiGlow, 0xff78ecff);
    LocalBox(-43.5f, 51.46f, 0.72f, -16.5f, 51.66f, 0.84f, floorZ, kMatForsetiGlow, 0xff78ecff);

    // Swept aerospace steel-blue structural ribs arching cleanly across the Forseti Wing!
    const float fRibX[4] = {-39.5f, -32.75f, -25.25f, -18.0f};
    for (unsigned i = 0; i < 4; ++i) {
        const float rx = fRibX[i];
        DressedColumn(rx, 30.68f, 0.38f, 4.55f, floorZ, kMatForsetiTrim, kMatTrim);
        DressedColumn(rx, 51.32f, 0.38f, 4.55f, floorZ, kMatForsetiTrim, kMatTrim);
        const float southY[4] = {30.35f, 31.15f, 33.40f, 30.35f};
        const float southZ[4] = { 4.40f,  4.40f, 10.20f, 10.45f};
        EmitSweptRibX(rx - 0.34f, rx + 0.34f, southY, southZ, floorZ, kMatForsetiTrim, 0xff5a8eb8);
        const float northY[4] = {51.65f, 50.85f, 48.60f, 51.65f};
        const float northZ[4] = { 4.40f,  4.40f, 10.20f, 10.45f};
        EmitSweptRibX(rx - 0.34f, rx + 0.34f, northY, northZ, floorZ, kMatForsetiTrim, 0xff5a8eb8);
        LocalBox(rx - 0.34f, 32.8f, 9.70f, rx + 0.34f, 49.2f, 10.48f, floorZ, kMatForsetiTrim, 0xff5a8eb8, true);
        LocalBox(rx - 0.14f, 33.4f, 9.58f, rx + 0.14f, 48.6f, 9.72f, floorZ, kMatForsetiGlow, 0xff78ecff, true);
    }
    // Forseti West Apse Monumental Framing Niche (official Lost Toys Forseti Mural renders inside!)
    DressedColumn(-43.45f, 35.4f, 0.36f, 10.5f, floorZ, kMatForsetiTrim, kMatTrim);
    DressedColumn(-43.45f, 46.6f, 0.36f, 10.5f, floorZ, kMatForsetiTrim, kMatTrim);
    LocalBox(-43.68f, 35.8f, 0.02f, -43.36f, 46.2f, 6.06f, floorZ, kMatPier, kSlate);
    LocalBox(-43.38f, 36.1f, 0.85f, -43.26f, 36.32f, 5.85f, floorZ, kMatForsetiGlow, 0xff78ecff);
    LocalBox(-43.38f, 45.68f, 0.85f, -43.26f, 45.90f, 5.85f, floorZ, kMatForsetiGlow, 0xff78ecff);
    LocalBox(-43.38f, 36.3f, 5.72f, -43.26f, 45.7f, 5.92f, floorZ, kMatForsetiGlow, 0xff78ecff);
    LocalBox(-43.38f, 36.3f, 0.85f, -43.26f, 45.7f, 1.05f, floorZ, kMatForsetiGlow, 0xff78ecff);

    // Forseti Ceiling strictly within [-44..-16] x [30..52] with central skylight clerestory spine
    Ceiling(floorZ, -44.0f, 30.0f, -16.0f, 39.2f, 10.5f, kMatCeiling);
    Ceiling(floorZ, -44.0f, 42.8f, -16.0f, 52.0f, 10.5f, kMatCeiling);
    LocalBox(-42.5f, 39.1f, 10.25f, -17.0f, 39.32f, 11.2f, floorZ, kMatPierCap);
    LocalBox(-42.5f, 42.68f, 10.25f, -17.0f, 42.9f, 11.2f, floorZ, kMatPierCap);
    AddGlassQuad(-42.4f, 39.3f, 10.95f, 0x38cdeeff, -17.1f, 39.3f, 10.95f, 0x38e4f6ff,
                 -17.1f, 42.7f, 10.95f, 0x38cdeeff, -42.4f, 42.7f, 10.95f, 0x38bce6ff, floorZ);

    // Six Forseti native bay plinths + 2 West Apse + 4 inter-bay Goodie turntable plinths (12 total!)
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
    TurntablePlinth(-40.8f, 38.0f, 1.30f, 0.90f, floorZ, false, kMatForsetiGlow, kMatPlinth);
    TurntablePlinth(-40.8f, 44.0f, 1.30f, 0.90f, floorZ, false, kMatForsetiGlow, kMatPlinth);
    const float fgx[2] = {-25.25f, -32.75f};
    for (unsigned i = 0; i < 2; ++i) {
        TurntablePlinth(fgx[i], 48.4f, 1.10f, 0.85f, floorZ, false, kMatForsetiGlow, kMatPlinth);
        TurntablePlinth(fgx[i], 33.6f, 1.10f, 0.85f, floorZ, false, kMatForsetiGlow, kMatPlinth);
    }
    // Three corner Forseti Turret alcoves (SAM, Pulse, Blaster — completing all 5 Forseti turrets!)
    TurntablePlinth(-40.8f, 49.0f, 1.05f, 0.85f, floorZ, false, kMatForsetiGlow, kMatPlinth);
    TurntablePlinth(-40.8f, 33.0f, 1.05f, 0.85f, floorZ, false, kMatForsetiGlow, kMatPlinth);
    TurntablePlinth(-18.0f, 48.6f, 1.05f, 0.85f, floorZ, false, kMatForsetiGlow, kMatPlinth);
    AudioKioskPedestal(-18.5f, 34.5f, floorZ);

    // =========================================================================
    // 4. MUSPELL DOMINION WING (East: x [16, 44], y [30, 52], height 10.5 m)
    // =========================================================================
    LocalFloorGrid(16.0f, 30.0f, 44.0f, 52.0f, 0.02f, floorZ, kMatFloor, 2.0f, 0xffd2cecc);
    RunnerStripX(16.0f, 39.2f, 41.0f, 1.15f, floorZ);
    LocalFloorGrid(16.5f, 39.60f, 39.0f, 39.72f, 0.068f, floorZ, kMatMuspellGlow, 4.0f, 0xffff9a38);
    LocalFloorGrid(16.5f, 42.28f, 39.0f, 42.40f, 0.068f, floorZ, kMatMuspellGlow, 4.0f, 0xffff9a38);

    // Heavy dark volcanic forged-iron Dominion perimeter walls (strictly non-overlapping!)
    LocalBox(16.0f + w, 30.0f - w, 0.0f, 44.0f - w, 30.0f + w, 10.5f, floorZ, kMatMuspellWall);
    LocalBox(16.0f - w, 52.0f - w, 0.0f, 44.0f - w, 52.0f,     10.5f, floorZ, kMatMuspellWall);
    LocalBox(20.0f + w, 52.0f,     0.0f, 44.0f - w, 52.0f + w, 10.5f, floorZ, kMatMuspellWall);
    LocalBox(44.0f - w, 30.0f - w, 0.0f, 44.0f + w, 52.0f + w, 10.5f, floorZ, kMatMuspellWall);

    // Heavy riveted bronze-iron fascia + molten-amber forge strips along North, South, and East apse walls
    LocalBox(16.4f, 30.32f, 5.95f, 43.6f, 30.72f, 7.30f, floorZ, kMatMuspellTrim);
    LocalBox(16.4f, 51.28f, 5.95f, 43.6f, 51.68f, 7.30f, floorZ, kMatMuspellTrim);
    LocalBox(43.28f, 30.4f, 5.95f, 43.68f, 51.6f, 7.30f, floorZ, kMatMuspellTrim);
    LocalBox(16.5f, 30.36f, 5.74f, 43.5f, 30.76f, 5.94f, floorZ, kMatMuspellGlow, 0xffff9a38);
    LocalBox(16.5f, 51.24f, 5.74f, 43.5f, 51.64f, 5.94f, floorZ, kMatMuspellGlow, 0xffff9a38);
    LocalBox(43.24f, 30.5f, 5.74f, 43.64f, 51.5f, 5.94f, floorZ, kMatMuspellGlow, 0xffff9a38);
    LocalBox(16.5f, 30.34f, 0.68f, 43.5f, 30.56f, 0.84f, floorZ, kMatMuspellGlow, 0xffff9a38);
    LocalBox(16.5f, 51.44f, 0.68f, 43.5f, 51.66f, 0.84f, floorZ, kMatMuspellGlow, 0xffff9a38);
    LocalBox(16.4f, 30.32f, 0.02f, 43.6f, 30.52f, 0.68f, floorZ, kMatMuspellIron);
    LocalBox(16.4f, 51.48f, 0.02f, 43.6f, 51.68f, 0.68f, floorZ, kMatMuspellIron);

    // Angular dark forged-iron buttresses & cantilevered foundry roof girders
    const float mButtressX[4] = {18.0f, 26.0f, 34.0f, 42.0f};
    for (unsigned i = 0; i < 4; ++i) {
        const float bx = mButtressX[i];
        DressedColumn(bx, 30.72f, 0.46f, 3.85f, floorZ, kMatMuspellIron, kMatMuspellTrim);
        DressedColumn(bx, 51.28f, 0.46f, 3.85f, floorZ, kMatMuspellIron, kMatMuspellTrim);
        LocalBox(bx - 0.10f, 31.12f, 0.90f, bx + 0.10f, 31.26f, 3.65f, floorZ, kMatMuspellGlow, 0xffff9a38);
        LocalBox(bx - 0.10f, 50.74f, 0.90f, bx + 0.10f, 50.88f, 3.65f, floorZ, kMatMuspellGlow, 0xffff9a38);
        const float sKneeY[4] = {30.35f, 31.45f, 34.30f, 30.35f};
        const float sKneeZ[4] = { 3.60f,  3.60f,  9.60f, 10.45f};
        EmitSweptRibX(bx - 0.44f, bx + 0.44f, sKneeY, sKneeZ, floorZ, kMatMuspellIron, 0xff464044);
        const float nKneeY[4] = {51.65f, 50.55f, 47.70f, 51.65f};
        const float nKneeZ[4] = { 3.60f,  3.60f,  9.60f, 10.45f};
        EmitSweptRibX(bx - 0.44f, bx + 0.44f, nKneeY, nKneeZ, floorZ, kMatMuspellIron, 0xff464044);
        LocalBox(bx - 0.44f, 33.4f, 9.25f, bx + 0.44f, 48.6f, 10.48f, floorZ, kMatMuspellIron, 0xff423c40, true);
        LocalBox(bx - 0.50f, 33.4f, 9.35f, bx + 0.50f, 48.6f, 9.55f, floorZ, kMatMuspellTrim, 0xffa86030, true);
        LocalBox(bx - 0.12f, 34.0f, 9.15f, bx + 0.12f, 48.0f, 9.28f, floorZ, kMatMuspellGlow, 0xffff9a38, true);
    }
    // East Apse forged-iron Dominion monolith panel with framing molten-amber channels (official Muspell Mural renders inside!)
    LocalBox(43.22f, 37.6f, 0.8f, 43.68f, 44.4f, 5.74f, floorZ, kMatMuspellIron);
    LocalBox(43.14f, 37.8f, 1.1f, 43.26f, 38.05f, 5.45f, floorZ, kMatMuspellGlow, 0xffff9a38);
    LocalBox(43.14f, 43.95f, 1.1f, 43.26f, 44.20f, 5.45f, floorZ, kMatMuspellGlow, 0xffff9a38);
    LocalBox(43.14f, 38.05f, 1.1f, 43.26f, 43.95f, 1.32f, floorZ, kMatMuspellGlow, 0xffff9a38);
    LocalBox(43.14f, 38.05f, 5.22f, 43.26f, 43.95f, 5.45f, floorZ, kMatMuspellGlow, 0xffff9a38);
    Ceiling(floorZ, 16.0f, 30.0f, 44.0f, 52.0f, 10.5f, kMatCeiling);

    // Thirteen Muspell rotating turntable plinths (2 rows of 4 + 2 East Apse + 3 corner turret alcoves!)
    const float mx[4] = {21.8f, 27.0f, 32.2f, 37.4f};
    for (unsigned i = 0; i < 4; ++i) {
        TurntablePlinth(mx[i], 48.0f, 1.50f, 0.90f, floorZ, false, kMatMuspellGlow, kMatMuspellIron);
        TurntablePlinth(mx[i], 34.0f, 1.50f, 0.90f, floorZ, false, kMatMuspellGlow, kMatMuspellIron);
    }
    TurntablePlinth(41.2f, 38.0f, 1.25f, 0.90f, floorZ, false, kMatMuspellGlow, kMatMuspellIron);
    TurntablePlinth(41.2f, 44.0f, 1.25f, 0.90f, floorZ, false, kMatMuspellGlow, kMatMuspellIron);
    TurntablePlinth(18.4f, 48.2f, 1.05f, 0.85f, floorZ, false, kMatMuspellGlow, kMatMuspellIron);
    TurntablePlinth(41.2f, 48.8f, 1.05f, 0.85f, floorZ, false, kMatMuspellGlow, kMatMuspellIron);
    TurntablePlinth(41.2f, 33.2f, 1.05f, 0.85f, floorZ, false, kMatMuspellGlow, kMatMuspellIron);
    AudioKioskPedestal(18.5f, 34.5f, floorZ);

    // =========================================================================
    // 5. AVIATION & NAVAL HANGAR (North High-Bay: x [-20, 20], y [52, 78], 15.5 m)
    // =========================================================================
    LocalFloorGrid(-20.0f, 52.0f, 20.0f, 78.0f, 0.02f, floorZ, kMatFloor, 2.0f);
    // Flush non-overlapping floor runners terminating cleanly at the central octagon (r = 4.95 m)
    RunnerStripY(0.0f, 52.00f, 60.05f, 1.45f, floorZ);
    RunnerStripY(0.0f, 69.95f, 73.20f, 1.45f, floorZ);
    RunnerStripX(-8.55f, -4.95f, 65.0f, 1.05f, floorZ);
    RunnerStripX( 4.95f,  6.48f, 65.0f, 1.05f, floorZ);

    // Hangar Flight-Deck Architectural Floor Composition
    {
        LocalOctagonTier(0.0f, 65.0f, 4.75f, 0.02f, 0.046f, floorZ, kMatInlay, 0xffcfd6dc, true);
        LocalOctagonTier(0.0f, 65.0f, 4.95f, 0.02f, 0.040f, floorZ, kMatTrim, kBrass, true);
        LocalOctagonTier(0.0f, 65.0f, 3.15f, 0.02f, 0.056f, floorZ, kMatPlinth, 0xffeef2f5, true);
        LocalOctagonTier(0.0f, 65.0f, 1.40f, 0.02f, 0.064f, floorZ, kMatInlay, 0xffc6d0d8, true);
        LocalFloorGrid(-3.0f, 64.86f, 3.0f, 65.14f, 0.068f, floorZ, kMatTrim, 2.0f, kBrass);
        LocalFloorGrid(-0.14f, 62.0f, 0.14f, 68.0f, 0.068f, floorZ, kMatTrim, 2.0f, kBrass);

        const float chevY[3] = {54.4f, 57.2f, 71.4f};
        for (unsigned ci = 0; ci < 3u; ++ci) {
            const float cy = chevY[ci];
            EmitFloorQuad(-1.60f, cy + 1.05f, -1.60f, cy + 0.55f, -4.40f, cy - 0.85f, -4.40f, cy - 0.35f,
                          0.052f, floorZ, kMatInlay, 0xffced6de);
            EmitFloorQuad( 1.60f, cy + 1.05f,  4.40f, cy - 0.35f,  4.40f, cy - 0.85f,  1.60f, cy + 0.55f,
                          0.052f, floorZ, kMatInlay, 0xffced6de);
            EmitFloorQuad(-1.60f, cy + 1.22f, -1.60f, cy + 1.05f, -4.40f, cy - 0.35f, -4.40f, cy - 0.18f,
                          0.060f, floorZ, kMatTrim, kBrass);
            EmitFloorQuad( 1.60f, cy + 1.22f,  4.40f, cy - 0.18f,  4.40f, cy - 0.35f,  1.60f, cy + 1.05f,
                          0.060f, floorZ, kMatTrim, kBrass);
        }

        // West Aviation Taxiway Apron Border framing the double-row aviation gallery cleanly along x = -8.40
        LocalFloorGrid(-8.55f, 54.0f, -8.41f, 63.84f, 0.056f, floorZ, kMatTrim, 2.5f, kBrass);
        LocalFloorGrid(-8.55f, 66.16f, -8.41f, 76.0f, 0.056f, floorZ, kMatTrim, 2.5f, kBrass);
        LocalFloorGrid(-13.8f, 64.86f, -8.55f, 65.14f, 0.050f, floorZ, kMatInlay, 2.5f, 0xffced6de);
        LocalFloorGrid(-13.8f, 64.95f, -8.55f, 65.05f, 0.058f, floorZ, kMatForsetiGlow, 2.5f, 0xff78ecff);

        // East Naval Drydock Promenade Apron framing the West viewing edge of the Reflecting Pool
        LocalFloorGrid(6.6f, 55.6f, 8.6f, 74.4f, 0.044f, floorZ, kMatInlay, 2.5f, 0xffced6de);
        LocalFloorGrid(6.48f, 55.48f, 6.62f, 74.52f, 0.056f, floorZ, kMatTrim, 2.5f, kBrass);
        LocalFloorGrid(6.62f, 55.48f, 8.75f, 55.62f, 0.056f, floorZ, kMatTrim, 2.5f, kBrass);
        LocalFloorGrid(6.62f, 74.38f, 8.75f, 74.52f, 0.056f, floorZ, kMatTrim, 2.5f, kBrass);
    }

    // Hangar South Wall flank strips & upper clerestory ABOVE the 10.5m Forseti/Muspell roofs (ZERO Z-fighting!)
    LocalBox(-20.0f + w, 52.0f,     0.0f,  -16.0f + w, 52.0f + w, 15.5f, floorZ, kMatWall);
    LocalBox( 16.0f - w, 52.0f,     0.0f,   20.0f - w, 52.0f + w, 15.5f, floorZ, kMatWall);
    LocalBox(-20.0f - w, 52.0f - w, 10.5f, -16.0f + w, 52.0f,     15.5f, floorZ, kMatWall);
    LocalBox( 16.0f - w, 52.0f - w, 10.5f,  20.0f + w, 52.0f,     15.5f, floorZ, kMatWall);
    // Hangar West, East, and North perimeter walls (clean non-overlapping corners!)
    LocalBox(-20.0f - w, 52.0f,     0.0f, -20.0f + w, 78.0f - w, 15.5f, floorZ, kMatWall);
    LocalBox( 20.0f - w, 52.0f,     0.0f,  20.0f + w, 78.0f - w, 15.5f, floorZ, kMatWall);
    LocalBox(-20.0f - w, 78.0f - w, 0.0f,  20.0f + w, 78.0f + w, 15.5f, floorZ, kMatWall);

    LocalBox(-19.70f, 52.3f, 0.02f, -19.52f, 77.7f, 0.85f, floorZ, kMatPierBase, kSlate);
    LocalBox( 19.52f, 52.3f, 0.02f,  19.70f, 77.7f, 0.85f, floorZ, kMatPierBase, kSlate);
    LocalBox(-19.70f, 77.52f, 0.02f,  19.70f, 77.70f, 0.85f, floorZ, kMatPierBase, kSlate);
    LocalBox(-19.68f, 52.3f, 0.85f, -19.48f, 77.7f, 0.96f, floorZ, kMatTrim, kBrass);
    LocalBox( 19.48f, 52.3f, 0.85f,  19.68f, 77.7f, 0.96f, floorZ, kMatTrim, kBrass);
    LocalBox(-19.68f, 77.48f, 0.85f,  19.68f, 77.68f, 0.96f, floorZ, kMatTrim, kBrass);
    // Symmetrical brushed-brass & warm cove upper architectural friezes on both West & East Hangar walls
    LocalBox(-19.65f, 52.5f, 9.6f, -19.45f, 77.5f, 10.4f, floorZ, kMatTrim, kBrass);
    LocalBox(-19.62f, 52.6f, 9.42f, -19.42f, 77.4f, 9.60f, floorZ, kMatWarmGlow, kLight);
    LocalBox( 19.45f, 52.5f, 9.6f,  19.65f, 77.5f, 10.4f, floorZ, kMatTrim, kBrass);
    LocalBox( 19.42f, 52.6f, 9.42f,  19.62f, 77.4f, 9.60f, floorZ, kMatWarmGlow, kLight);

    // Hangar Perimeter Ceiling strictly within [-20..20] x [52..78] leaving 18m x 22m glass-truss skylight
    Ceiling(floorZ, -20.0f, 52.0f,  -9.0f, 78.0f, 15.5f, kMatCeiling);
    Ceiling(floorZ,   9.0f, 52.0f,  20.0f, 78.0f, 15.5f, kMatCeiling);
    Ceiling(floorZ,  -9.0f, 52.0f,   9.0f, 54.0f, 15.5f, kMatCeiling);
    Ceiling(floorZ,  -9.0f, 76.0f,   9.0f, 78.0f, 15.5f, kMatCeiling);
    LocalBox(-9.3f, 53.7f, 14.9f, -8.8f, 76.3f, 16.2f, floorZ, kMatPier, kSlate, true);
    LocalBox( 8.8f, 53.7f, 14.9f,  9.3f, 76.3f, 16.2f, floorZ, kMatPier, kSlate, true);
    LocalBox(-9.0f, 53.7f, 14.9f,  9.0f, 54.2f, 16.2f, floorZ, kMatPier, kSlate, true);
    LocalBox(-9.0f, 75.8f, 14.9f,  9.0f, 76.3f, 16.2f, floorZ, kMatPier, kSlate, true);
    LocalBox(-8.9f, 54.2f, 14.82f, -8.72f, 75.8f, 15.02f, floorZ, kMatWarmGlow, kLight);
    LocalBox( 8.72f, 54.2f, 14.82f,  8.9f, 75.8f, 15.02f, floorZ, kMatWarmGlow, kLight);
    LocalBox(-4.5f - 0.14f, 54.0f, 15.35f, -4.5f + 0.14f, 76.0f, 15.75f, floorZ, kMatTrim, kBrass, true);
    LocalBox( 0.0f - 0.18f, 54.0f, 15.50f,  0.0f + 0.18f, 76.0f, 16.05f, floorZ, kMatTrim, kBrass, true);
    LocalBox( 4.5f - 0.14f, 54.0f, 15.35f,  4.5f + 0.14f, 76.0f, 15.75f, floorZ, kMatTrim, kBrass, true);
    AddGlassQuad(-8.9f, 54.1f, 15.75f, 0x28d4f0ff, 8.9f, 54.1f, 15.75f, 0x36fff4d8,
                  8.9f, 75.9f, 15.75f, 0x26d0ecff, -8.9f, 75.9f, 15.75f, 0x22c4e2fc, floorZ);
    AddGlassQuad( 6.8f, 56.0f, 14.2f, 0x1affebc0,  6.8f, 73.0f, 14.2f, 0x16ffe6b0,
                  1.5f, 75.0f,  2.2f, 0x00ffebc0,  1.5f, 58.0f,  2.2f, 0x00ffebc0, floorZ);

    // Symmetrical swept steel-and-brass vaulted Hangar Arch Ribs + Lower Tension Trusses + Suspended Pendant Light Bays
    for (float y = 56.0f; y <= 75.0f; y += 6.0f) {
        DressedColumn(-19.45f, y, 0.44f, 15.5f, floorZ);
        DressedColumn( 19.45f, y, 0.44f, 15.5f, floorZ);
        const float wRibX[4] = {-19.65f, -18.55f, -13.60f, -19.65f};
        const float wRibZ[4] = {  5.20f,   5.20f,  11.65f,  13.85f};
        EmitSweptRibY(y - 0.36f, y + 0.36f, wRibX, wRibZ, floorZ, kMatPier, kSlate);
        const float eRibX[4] = { 19.65f,  18.55f,  13.60f,  19.65f};
        const float eRibZ[4] = {  5.20f,   5.20f,  11.65f,  13.85f};
        EmitSweptRibY(y - 0.36f, y + 0.36f, eRibX, eRibZ, floorZ, kMatPier, kSlate);

        LocalBox(-14.2f, y - 0.22f, 11.48f, 14.2f, y + 0.22f, 11.82f, floorZ, kMatPier, kSlate, true);
        LocalBox(-14.2f, y - 0.26f, 11.42f, 14.2f, y + 0.26f, 11.52f, floorZ, kMatTrim, kBrass, true);
        LocalBox(-19.5f, y - 0.24f, 13.75f, 19.5f, y + 0.24f, 14.15f, floorZ, kMatPier, kSlate, true);
        LocalBox(-19.5f, y - 0.28f, 13.68f, 19.5f, y + 0.28f, 13.80f, floorZ, kMatTrim, kBrass, true);
        LocalBox(-19.5f, y - 0.22f, 15.05f, 19.5f, y + 0.22f, 15.45f, floorZ, kMatPier, kSlate, true);
        for (float tx = -12.0f; tx <= 12.1f; tx += 4.0f) {
            LocalBox(tx - 0.12f, y - 0.14f, 11.82f, tx + 0.12f, y + 0.14f, 13.75f, floorZ, kMatPier, kSlate);
            LocalBox(tx - 0.14f, y - 0.16f, 14.15f, tx + 0.14f, y + 0.16f, 15.05f, floorZ, kMatPier, kSlate);
        }
        for (int side = -1; side <= 1; side += 2) {
            const float px = float(side) * 6.4f;
            LocalBox(px - 0.68f, y - 0.05f, 9.36f, px - 0.58f, y + 0.05f, 11.48f, floorZ, kMatStand, kSlate);
            LocalBox(px + 0.58f, y - 0.05f, 9.36f, px + 0.68f, y + 0.05f, 11.48f, floorZ, kMatStand, kSlate);
            LocalBox(px - 0.96f, y - 0.42f, 9.08f, px + 0.96f, y + 0.42f, 9.38f, floorZ, kMatPierBase, kSlate, true);
            LocalBox(px - 1.02f, y - 0.46f, 9.00f, px + 1.02f, y + 0.46f, 9.10f, floorZ, kMatTrim, kBrass, true);
            LocalBox(px - 0.82f, y - 0.28f, 8.92f, px + 0.82f, y + 0.28f, 9.02f, floorZ, kMatWarmGlow, kLight, true);
        }
    }

    // West Aviation Bay: 9 sculpted plinths + swept aerodynamic fin flight pylons (staggered double row — 100% of aircraft!)
    auto emitFlightPylon = [&](float px, float py, float half, float topZ, bool isForseti) {
        const Material pGlow = isForseti ? kMatForsetiGlow : kMatMuspellGlow;
        const Material pTrim = isForseti ? kMatForsetiTrim : kMatMuspellTrim;
        TurntablePlinth(px, py, half, 0.80f, floorZ, false, pGlow, kMatPlinth);
        LocalBox(px - 0.38f, py - 0.50f, 0.80f, px + 0.38f, py + 0.50f, 1.00f, floorZ, kMatPierBase, kSlate);
        LocalBox(px - 0.41f, py - 0.53f, 0.96f, px + 0.41f, py + 0.53f, 1.04f, floorZ, pGlow, kLight);
        LocalBox(px - 0.18f, py - 0.18f, 1.02f, px + 0.18f, py + 0.18f, topZ - 0.10f, floorZ, kMatStand, kSlate);
        LocalBox(px - 0.06f, py - 0.44f, 1.02f, px + 0.06f, py + 0.44f, 1.85f, floorZ, pTrim, kBrass);
        LocalBox(px - 0.24f, py - 0.24f, topZ - 0.20f, px + 0.24f, py + 0.24f, topZ - 0.08f, floorZ, kMatTrim, kBrass);
    };
    const float ayOuter[5] = {55.0f, 60.0f, 65.0f, 70.0f, 75.0f};
    const float aTopOuter[5] = {2.65f, 3.10f, 2.75f, 3.10f, 2.85f};
    for (unsigned i = 0; i < 5u; ++i)
        emitFlightPylon(-15.6f, ayOuter[i], 1.35f, aTopOuter[i], i < 3u);
    const float ayInner[4] = {58.4f, 62.2f, 68.4f, 72.8f};
    const float aTopInner[4] = {2.95f, 2.55f, 2.65f, 3.05f};
    for (unsigned i = 0; i < 4u; ++i)
        emitFlightPylon(-9.8f, ayInner[i], 1.20f, aTopInner[i], i == 0u);

    // East Naval Bay: glowing recessed reflecting-water basin (x [8.8, 17.4], y [55.6, 74.4]) + 4 lit submerged piers
    AddContactShadow(13.1f, 65.0f, 4.3f, 9.4f, 1.25f, floorZ, 0.042f, 0.58f);
    LocalBox( 8.8f, 55.6f, 0.02f, 17.4f, 56.05f, 0.46f, floorZ, kMatWall);
    LocalBox( 8.8f, 73.95f, 0.02f, 17.4f, 74.4f, 0.46f, floorZ, kMatWall);
    LocalBox( 8.8f, 56.05f, 0.02f,  9.25f, 73.95f, 0.46f, floorZ, kMatWall);
    LocalBox(16.95f, 56.05f, 0.02f, 17.4f, 73.95f, 0.46f, floorZ, kMatWall);
    LocalBox( 8.76f, 55.56f, 0.44f, 17.44f, 56.08f, 0.50f, floorZ, kMatTrim, kBrass);
    LocalBox( 8.76f, 73.92f, 0.44f, 17.44f, 74.44f, 0.50f, floorZ, kMatTrim, kBrass);
    LocalBox( 8.76f, 56.08f, 0.44f,  9.28f, 73.92f, 0.50f, floorZ, kMatTrim, kBrass);
    LocalBox(16.92f, 56.08f, 0.44f, 17.44f, 73.92f, 0.50f, floorZ, kMatTrim, kBrass);
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
    LocalBox(10.65f, 56.4f, 0.26f, 10.77f, 73.6f, 0.33f, floorZ, kMatAquaGlow, kLight);
    LocalBox(15.23f, 56.4f, 0.26f, 15.35f, 73.6f, 0.33f, floorZ, kMatAquaGlow, kLight);
    LocalFloorGrid(9.25f, 56.05f, 16.95f, 73.95f, 0.30f, floorZ, kMatWater, 1.8f, 0xff287898);
    const float ny[4] = {58.2f, 62.7f, 67.2f, 71.7f};
    for (unsigned i = 0; i < 4; ++i) {
        LocalBox(11.3f, ny[i] - 1.15f, 0.28f, 14.7f, ny[i] + 1.15f, 0.65f, floorZ, kMatPierBase, kSlate);
        LocalBox(11.24f, ny[i] - 1.21f, 0.56f, 14.76f, ny[i] + 1.21f, 0.65f, floorZ, kMatTrim, kBrass);
    }

    // North Relative Scale Alcove: architectural monument elevation grid + backlit top light trough!
    AddContactShadow(0.0f, 75.3f, 5.4f, 1.4f, 1.25f, floorZ, 0.042f, 0.60f);
    DressedColumn(-6.75f, 77.35f, 0.42f, 11.6f, floorZ);
    DressedColumn( 6.75f, 77.35f, 0.42f, 11.6f, floorZ);
    LocalBox(-7.25f, 77.05f, 11.2f, 7.25f, 77.70f, 11.85f, floorZ, kMatPierCap, 0xfff6f2e8, true);
    LocalBox(-7.15f, 76.98f, 11.10f, 7.15f, 77.12f, 11.24f, floorZ, kMatTrim, kBrass);
    LocalBox(-6.20f, 77.12f, 10.95f, 6.20f, 77.32f, 11.12f, floorZ, kMatWarmGlow, kLight);
    LocalBox(-6.35f, 77.42f, 0.02f, 6.35f, 77.68f, 11.2f, floorZ, kMatPierBase, kSlate);
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
    LocalBox(-5.25f, 76.56f, 0.55f, 5.25f, 76.72f, 0.65f, floorZ, kMatWarmGlow, kLight);

    // Twin Monumental Flanking Hangar Faction Standards on North Wall (official retail Forseti/Muspell emblems render inside!)
    for (int side = -1; side <= 1; side += 2) {
        const float cx = float(side) * 13.2f;
        const Material fTrim = side < 0 ? kMatForsetiTrim : kMatMuspellTrim;
        const Material fGlow = side < 0 ? kMatForsetiGlow : kMatMuspellGlow;
        const DWORD gCol = side < 0 ? 0xff78ecffu : 0xffff9a38u;
        LocalBox(cx - 3.35f, 77.44f, 1.55f, cx + 3.35f, 77.68f, 9.35f, floorZ, kMatPierBase, kSlate);
        LocalBox(cx - 3.50f, 77.36f, 1.40f, cx + 3.50f, 77.54f, 1.62f, floorZ, kMatTrim, kBrass);
        LocalBox(cx - 3.50f, 77.36f, 9.28f, cx + 3.50f, 77.54f, 9.55f, floorZ, kMatTrim, kBrass);
        LocalBox(cx - 3.50f, 77.36f, 1.62f, cx - 3.28f, 77.54f, 9.28f, floorZ, kMatTrim, kBrass);
        LocalBox(cx + 3.28f, 77.36f, 1.62f, cx + 3.50f, 77.54f, 9.28f, floorZ, kMatTrim, kBrass);
        LocalBox(cx - 3.15f, 77.38f, 8.20f, cx + 3.15f, 77.50f, 9.24f, floorZ, fTrim);
        LocalBox(cx - 3.15f, 77.32f, 8.05f, cx + 3.15f, 77.44f, 8.22f, floorZ, fGlow, gCol);
        LocalBox(cx - 3.15f, 77.32f, 1.64f, cx + 3.15f, 77.44f, 1.80f, floorZ, fGlow, gCol);
    }

    AudioKioskPedestal(-7.5f, 55.5f, floorZ);

    // Helper for freestanding double-sided N-S museum gallery divider walls (5.45 m tall for two-tier salon hang)
    auto galleryDividerWallNS = [&](float cx) {
        AddContactShadow(cx, 17.70f, 0.25f, 3.60f, 0.72f, floorZ, 0.036f, 0.46f);
        LocalBox(cx - 0.25f, 14.10f, 0.02f, cx + 0.25f, 21.30f, 5.30f, floorZ, kMatWall, 0xfff6f2ea);
        // Dark slate skirting base + brushed-brass top cornice cap + dual-sided gallery light wash bars above upper row
        LocalBox(cx - 0.27f, 14.07f, 0.02f, cx + 0.27f, 21.33f, 0.36f, floorZ, kMatPierBase, kSlate);
        LocalBox(cx - 0.29f, 14.05f, 5.30f, cx + 0.29f, 21.35f, 5.45f, floorZ, kMatTrim, kBrass);
        LocalBox(cx - 0.34f, 14.22f, 5.10f, cx - 0.25f, 21.18f, 5.24f, floorZ, kMatWarmGlow, kLight);
        LocalBox(cx + 0.25f, 14.22f, 5.10f, cx + 0.34f, 21.18f, 5.24f, floorZ, kMatWarmGlow, kLight);
    };

    // =========================================================================
    // 6. CONCEPT ART GALLERY (Southeast: x [16, 40], y [10, 28], height 8.5 m)
    // =========================================================================
    LocalFloorGrid(16.0f, 10.0f, 40.0f, 28.0f, 0.02f, floorZ, kMatFloor, 2.2f);
    // Flush non-overlapping runners connecting Atrium SE portal (x=16) -> vertical spine (x=36.3) -> Cinema portal (x=40)
    RunnerStripX(16.00f, 35.19f, 24.0f, 1.00f, floorZ);
    RunnerStripY(36.30f, 13.24f, 25.11f, 1.00f, floorZ);
    RunnerStripX(37.41f, 40.00f, 14.5f, 1.15f, floorZ);
    // West facade wall sealing Concept Art Gallery from Courtyard (strictly non-overlapping corners!)
    LocalBox(16.0f - w, 10.0f + w, 0.0f, 16.0f + w, 20.0f - w, 8.5f, floorZ, kMatWall, 0xfff5f1e8);
    LocalBox(15.35f, 10.35f, 8.05f, 16.35f, 19.70f, 8.68f, floorZ, kMatPierCap, 0xfff8f4eb, true);
    LocalBox(15.50f,  9.72f, 5.20f, 15.68f, 19.70f, 5.58f, floorZ, kMatTrim, kBrass);
    DressedColumn(15.62f, 10.25f, 0.36f, 8.05f, floorZ);
    DressedColumn(15.62f, 15.00f, 0.36f, 8.05f, floorZ);
    LocalBox(16.0f - w, 10.0f - w, 0.0f, 40.0f - w, 10.0f + w, 8.5f, floorZ, kMatWall);
    // Exterior south facade cornice, brass frieze & classical pilasters facing the Coastal Courtyard vista
    LocalBox(15.40f, 9.35f, 8.05f, 39.70f, 10.35f, 8.68f, floorZ, kMatPierCap, 0xfff8f4eb, true);
    LocalBox(15.68f, 9.52f, 5.20f, 39.66f,  9.72f, 5.58f, floorZ, kMatTrim, kBrass);
    DressedColumn(21.5f, 9.62f, 0.36f, 8.05f, floorZ);
    DressedColumn(28.5f, 9.62f, 0.36f, 8.05f, floorZ);
    LocalBox(16.0f + w, 28.0f - w, 0.0f, 40.0f - w, 28.0f + w, 8.5f, floorZ, kMatWall);
    // East wall shared with Screening Room (full 9.5 m height): portal y [11.7..17.3] + matching exterior coastal return
    LocalBox(40.0f - w, 7.0f, 0.0f, 40.0f + w, 11.7f, 9.5f, floorZ, kMatWall);
    // Matching exterior coastal cornice + brass frieze along y = 7.0..10.0 on the Screening Room west return!
    LocalBox(39.35f, 7.07f, 8.92f, 40.0f + w, 10.05f, 9.60f, floorZ, kMatPierCap, 0xfff8f4eb, true);
    LocalBox(39.50f, 6.72f, 5.20f, 39.70f,     9.72f, 5.58f, floorZ, kMatTrim, kBrass);
    PortalFrameY(40.0f, 11.7f, 17.3f, 4.6f, 9.5f, floorZ, kMatTrim, kMatWarmGlow);
    LocalBox(40.0f - w, 17.3f, 0.0f, 40.0f + w, 29.0f, 9.5f, floorZ, kMatWall);
    // Raised brushed-brass picture rail (5.22..5.38 m) + warm wall-wash glow bar (5.38..5.50 m) cleanly above two-tier salon hang!
    LocalBox(16.4f, 27.58f, 5.22f, 39.6f, 27.70f, 5.38f, floorZ, kMatTrim, kBrass);
    LocalBox(16.4f, 10.30f, 5.22f, 39.6f, 10.42f, 5.38f, floorZ, kMatTrim, kBrass);
    LocalBox(17.0f, 27.46f, 5.38f, 39.0f, 27.62f, 5.50f, floorZ, kMatWarmGlow, kLight);
    LocalBox(17.0f, 10.38f, 5.38f, 39.0f, 10.54f, 5.50f, floorZ, kMatWarmGlow, kLight);
    LocalBox(16.4f, 27.62f, 0.02f, 39.6f, 27.70f, 0.38f, floorZ, kMatPierBase, kSlate);
    LocalBox(16.4f, 10.30f, 0.02f, 39.6f, 10.38f, 0.38f, floorZ, kMatPierBase, kSlate);
    LocalBox(39.56f, 18.6f, 5.22f, 39.70f, 27.4f, 5.38f, floorZ, kMatTrim, kBrass);
    LocalBox(39.46f, 18.8f, 5.38f, 39.62f, 27.2f, 5.50f, floorZ, kMatWarmGlow, kLight);
    // Three parallel double-sided N-S gallery divider walls (4.5 m aisles) + Audio Kiosk
    galleryDividerWallNS(23.0f);
    galleryDividerWallNS(28.0f);
    galleryDividerWallNS(33.0f);
    AudioKioskPedestal(19.0f, 15.0f, floorZ);
    Ceiling(floorZ, 16.0f, 10.0f, 40.0f, 28.0f, 8.5f, kMatCeiling);

    // =========================================================================
    // 7. EARLY DESIGNS VAULT (Southwest: x [-40, -16], y [10, 28], height 8.5 m)
    // =========================================================================
    LocalFloorGrid(-40.0f, 10.0f, -16.0f, 28.0f, 0.02f, floorZ, kMatFloor, 2.2f);
    RunnerStripX(-36.0f, -16.0f, 24.0f, 1.00f, floorZ);
    // East facade wall sealing Early Designs Vault from Courtyard (strictly non-overlapping corners!)
    LocalBox(-16.0f - w, 10.0f + w, 0.0f, -16.0f + w, 20.0f - w, 8.5f, floorZ, kMatWall, 0xfff5f1e8);
    LocalBox(-16.35f, 10.35f, 8.05f, -15.35f, 19.70f, 8.68f, floorZ, kMatPierCap, 0xfff8f4eb, true);
    LocalBox(-15.68f,  9.72f, 5.20f, -15.50f, 19.70f, 5.58f, floorZ, kMatTrim, kBrass);
    DressedColumn(-15.62f, 10.25f, 0.36f, 8.05f, floorZ);
    DressedColumn(-15.62f, 15.00f, 0.36f, 8.05f, floorZ);
    LocalBox(-40.0f + w, 10.0f - w, 0.0f, -16.0f + w, 10.0f + w, 8.5f, floorZ, kMatWall);
    // Exterior south facade cornice, brass frieze & classical pilasters facing the Coastal Courtyard vista
    LocalBox(-40.60f, 9.35f, 8.05f, -15.40f, 10.35f, 8.68f, floorZ, kMatPierCap, 0xfff8f4eb, true);
    LocalBox(-40.20f, 9.52f, 5.20f, -15.68f,  9.72f, 5.58f, floorZ, kMatTrim, kBrass);
    DressedColumn(-21.5f, 9.62f, 0.36f, 8.05f, floorZ);
    DressedColumn(-28.5f, 9.62f, 0.36f, 8.05f, floorZ);
    LocalBox(-40.0f + w, 28.0f - w, 0.0f, -16.0f - w, 28.0f + w, 8.5f, floorZ, kMatWall);
    LocalBox(-40.0f - w, 10.0f - w, 0.0f, -40.0f + w, 28.0f + w, 8.5f, floorZ, kMatWall);
    LocalBox(-39.6f, 27.58f, 5.22f, -16.4f, 27.70f, 5.38f, floorZ, kMatTrim, kBrass);
    LocalBox(-39.6f, 10.30f, 5.22f, -16.4f, 10.42f, 5.38f, floorZ, kMatTrim, kBrass);
    LocalBox(-39.0f, 27.46f, 5.38f, -17.0f, 27.62f, 5.50f, floorZ, kMatWarmGlow, kLight);
    LocalBox(-39.0f, 10.38f, 5.38f, -17.0f, 10.54f, 5.50f, floorZ, kMatWarmGlow, kLight);
    LocalBox(-39.6f, 27.62f, 0.02f, -16.4f, 27.70f, 0.38f, floorZ, kMatPierBase, kSlate);
    LocalBox(-39.6f, 10.30f, 0.02f, -16.4f, 10.38f, 0.38f, floorZ, kMatPierBase, kSlate);
    LocalBox(-39.70f, 11.0f, 5.22f, -39.56f, 27.0f, 5.38f, floorZ, kMatTrim, kBrass);
    LocalBox(-39.62f, 11.4f, 5.38f, -39.46f, 26.6f, 5.50f, floorZ, kMatWarmGlow, kLight);
    // Three parallel double-sided N-S archival divider walls (4.5 m aisles) + Audio Kiosk
    galleryDividerWallNS(-23.0f);
    galleryDividerWallNS(-28.0f);
    galleryDividerWallNS(-33.0f);
    AudioKioskPedestal(-19.0f, 15.0f, floorZ);
    Ceiling(floorZ, -40.0f, 10.0f, -16.0f, 28.0f, 8.5f, kMatCeiling);

    // Mount brass-framed 3D architectural lintel signboards proud above EVERY portal (all 14 faces).
    BuildAllPortalSignboards(floorZ);
}

static void SetMuseumAnisoSampler(IDirect3DDevice9 *dev) {
    const bool anisoSup = (LT.m_d3dCaps.RasterCaps & D3DPRASTERCAPS_ANISOTROPY) != 0 &&
                          LT.m_d3dCaps.MaxAnisotropy > 1u;
    const DWORD maxAniso = anisoSup
        ? (LT.m_d3dCaps.MaxAnisotropy < 8u ? LT.m_d3dCaps.MaxAnisotropy : 8u)
        : 1u;
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, anisoSup ? D3DTEXF_ANISOTROPIC : D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MAXANISOTROPY, maxAniso);
}

static CeMuseumDirButtonId DirectoryButtonForWing(CeMuseumTour::Wing target) {
    switch (target) {
    case CeMuseumTour::kWingForseti:       return CE_MUSEUM_DIR_BTN_FORSETI;
    case CeMuseumTour::kWingMuspell:       return CE_MUSEUM_DIR_BTN_MUSPELL;
    case CeMuseumTour::kWingHangar:        return CE_MUSEUM_DIR_BTN_HANGAR;
    case CeMuseumTour::kWingConceptArt:    return CE_MUSEUM_DIR_BTN_CONCEPT_ART;
    case CeMuseumTour::kWingEarlyDesigns:  return CE_MUSEUM_DIR_BTN_EARLY_DESIGNS;
    case CeMuseumTour::kWingScreeningRoom: return CE_MUSEUM_DIR_BTN_CINEMA;
    default:                               return CE_MUSEUM_DIR_BTN_FORSETI;
    }
}

void DrawPortalSignboards(IDirect3DDevice9 *dev) {
    if (!gGrandMode || !gPtr || !dev || !gPtr->signVertCount || gPtr->signIdxCount < 3u) return;
    IDirect3DTexture9 *atlas = CeMuseumTextures::GetPortalSignAtlas();
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetTexture(1, NULL);
    dev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    SetMuseumAnisoSampler(dev);

    if (atlas) {
        dev->SetTexture(0, atlas);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
        if (gHallGpu.portalSignVb && gHallGpu.portalSignIb) {
            dev->SetStreamSource(0, gHallGpu.portalSignVb, 0, sizeof(SignTexVertex));
            dev->SetIndices(gHallGpu.portalSignIb);
            dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, gPtr->signVertCount, 0,
                                      gPtr->signIdxCount / 3u);
            dev->SetStreamSource(0, NULL, 0, 0);
            dev->SetIndices(NULL);
        } else {
            dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0u, gPtr->signVertCount,
                                        gPtr->signIdxCount / 3u, gPtr->signIndices,
                                        D3DFMT_INDEX16, gPtr->signVerts, sizeof(SignTexVertex));
        }
    } else {
        // Plain honed-obsidian plaque fallback if baked signage atlas is unavailable.
        dev->SetTexture(0, NULL);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TFACTOR);
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        dev->SetRenderState(D3DRS_TEXTUREFACTOR, 0xff161c22u);
        dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
        dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0u, gPtr->signVertCount,
                                    gPtr->signIdxCount / 3u, gPtr->signIndices,
                                    D3DFMT_INDEX16, gPtr->signVerts, sizeof(SignTexVertex));
    }
}

void DrawDirectoryButtons(IDirect3DDevice9 *dev, float floorZ) {
    if (!gGrandMode || !dev) return;
    if (gCurrentWing != CeMuseumTour::kWingAtrium && gCurrentWing != CeMuseumTour::kWingCourtyard) return;
    IDirect3DTexture9 *dirAtlas = CeMuseumTextures::GetDirectoryAtlas();
    const float baseX = CeMuseumWorld::kEntranceX + CeMuseumTour::kDirectoryX;
    const float baseY = CeMuseumWorld::kEntranceY + CeMuseumTour::kDirectoryY;
    const float btnY = baseY - 0.18f;
    const float btnZ = floorZ - CeMuseumTour::kDirectoryZ;

    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetTexture(1, NULL);
    dev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

    if (dirAtlas) {
        // Optional golden hover bezel 1.5 cm behind the aimed button so selection pops crisply.
        if (gDirHit.valid && gDirHit.index >= 0 && gDirHit.index < CeMuseumTour::kDirectoryStops) {
            const float wx = baseX + CeMuseumTour::kDirectory[gDirHit.index].localOffsetX;
            const float hy = btnY + 0.015f;
            const float hw = 0.235f, hh = 0.102f;
            const Vertex halo[4] = {
                {wx - hw, hy, btnZ - hh, 0xffffea88u},
                {wx + hw, hy, btnZ - hh, 0xffffea88u},
                {wx - hw, hy, btnZ + hh, 0xffe4b64cu},
                {wx + hw, hy, btnZ + hh, 0xffe4b64cu},
            };
            dev->SetTexture(0, NULL);
            dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
            dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
            dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
            dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
            dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, halo, sizeof(Vertex));
        }

        // Single batched textured draw call: 1 Architectural Floor-Plan Map plaque + 6 Labeled Console Buttons!
        SignTexVertex verts[(1 + CeMuseumTour::kDirectoryStops) * 6];
        unsigned used = 0;
        auto pushQuad = [&](float cx, float cy, float cz, float hw, float hh,
                            const CeMuseumAtlasUv &uv, DWORD topCol, DWORD botCol) {
            const SignTexVertex a = {cx - hw, cy, cz - hh, topCol, uv.u0, uv.v0};
            const SignTexVertex b = {cx + hw, cy, cz - hh, topCol, uv.u1, uv.v0};
            const SignTexVertex c = {cx + hw, cy, cz + hh, botCol, uv.u1, uv.v1};
            const SignTexVertex d = {cx - hw, cy, cz + hh, botCol, uv.u0, uv.v1};
            verts[used++] = a; verts[used++] = b; verts[used++] = c;
            verts[used++] = a; verts[used++] = c; verts[used++] = d;
        };

        // Eye-level Architectural Floor-Plan Map Plaque (1.20m x 0.86m, cleanly recessed below top light bar)
        pushQuad(baseX, baseY - 0.10f, floorZ - 1.945f, 0.60f, 0.43f,
                 CeMuseumTextures::DirectoryMapUv(), 0xffffffffu, 0xfff4efe6u);

        // Six crisp labeled console buttons matching each wing's quick-jump target (0.43m x 0.164m)
        for (int i = 0; i < CeMuseumTour::kDirectoryStops; ++i) {
            const float wx = baseX + CeMuseumTour::kDirectory[i].localOffsetX;
            const bool hovered = gDirHit.valid && gDirHit.index == i;
            const DWORD topCol = hovered ? 0xffffffffu : 0xffe4dfd6u;
            const DWORD botCol = hovered ? 0xfffff4d6u : 0xffd0cbc2u;
            const CeMuseumDirButtonId btnId = DirectoryButtonForWing(CeMuseumTour::kDirectory[i].target);
            pushQuad(wx, btnY, btnZ, 0.215f, 0.082f,
                     CeMuseumTextures::DirectoryButtonUv(btnId), topCol, botCol);
        }

        SetMuseumAnisoSampler(dev);
        dev->SetTexture(0, dirAtlas);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
        dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, used / 3u, verts, sizeof(SignTexVertex));
        dev->SetTexture(0, NULL);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
        dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
        return;
    }

    // Plain-colour fallback when the baked directory atlas is unavailable.
    Vertex verts[CeMuseumTour::kDirectoryStops * 6];
    unsigned used = 0;
    for (int i = 0; i < CeMuseumTour::kDirectoryStops; ++i) {
        const float wx = baseX + CeMuseumTour::kDirectory[i].localOffsetX;
        const bool hovered = gDirHit.valid && gDirHit.index == i;
        const DWORD col = hovered ? 0xffffec90 : 0xffcfa454;
        const float hw = 0.18f, hh = 0.09f;
        const Vertex a = {wx - hw, btnY, btnZ - hh, col};
        const Vertex b = {wx + hw, btnY, btnZ - hh, col};
        const Vertex c = {wx + hw, btnY, btnZ + hh, col};
        const Vertex d = {wx - hw, btnY, btnZ + hh, col};
        verts[used++] = a; verts[used++] = b; verts[used++] = c;
        verts[used++] = a; verts[used++] = c; verts[used++] = d;
    }
    if (used) {
        dev->SetTexture(0, NULL);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
        dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, used / 3u, verts, sizeof(Vertex));
    }
}

} // namespace

struct FactionUploadCtx {
    IDirect3DDevice9 *device;
    FactionTexSlot *outSlot;
};

struct SkyUploadCtx {
    IDirect3DDevice9 *device;
    IDirect3DTexture9 **outTex;
};

static int FactionUploadCallback(const unsigned char *dds, size_t bytes,
                                 const CeAssetTextureHeader *h, void *context) {
    FactionUploadCtx *ctx = static_cast<FactionUploadCtx *>(context);
    if (!ctx || !ctx->device || !ctx->outSlot || !dds || !bytes || !h) return 0;
    IDirect3DTexture9 *tex = NULL;
    // Full mipmap chain (MipLevels = 0, D3DX_FILTER_BOX) eliminates grazing-angle courtyard shimmer.
    const HRESULT hr = D3DXCreateTextureFromFileInMemoryEx(
        ctx->device, dds, static_cast<UINT>(bytes),
        static_cast<UINT>(h->width), static_cast<UINT>(h->height),
        0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
        D3DX_FILTER_TRIANGLE, D3DX_FILTER_BOX, 0, NULL, NULL, &tex);
    if (FAILED(hr) || !tex) {
        if (tex) tex->Release();
        return 0;
    }
    ctx->outSlot->tex = tex;
    ctx->outSlot->srcW = h->width;
    ctx->outSlot->srcH = h->height;
    ctx->outSlot->fromLooseSplash = false;
    return 1;
}

static int SkyUploadCallback(const unsigned char *dds, size_t bytes,
                             const CeAssetTextureHeader *h, void *context) {
    SkyUploadCtx *ctx = static_cast<SkyUploadCtx *>(context);
    if (!ctx || !ctx->device || !ctx->outTex || !dds || !bytes || !h) return 0;
    IDirect3DTexture9 *tex = NULL;
    const HRESULT hr = D3DXCreateTextureFromFileInMemoryEx(
        ctx->device, dds, static_cast<UINT>(bytes),
        static_cast<UINT>(h->width), static_cast<UINT>(h->height),
        1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
        D3DX_FILTER_TRIANGLE, D3DX_FILTER_NONE, 0, NULL, NULL, &tex);
    if (FAILED(hr) || !tex) {
        if (tex) tex->Release();
        return 0;
    }
    *ctx->outTex = tex;
    return 1;
}

static void ReleaseFactionTextures() {
    for (unsigned i = 0; i < kFactionTexCount; ++i) {
        if (gFactionTex[i].tex) {
            gFactionTex[i].tex->Release();
        }
        gFactionTex[i] = FactionTexSlot{NULL, 0u, 0u, false};
    }
    for (unsigned i = 0; i < 5u; ++i) {
        if (gSkyCubeTex[i]) {
            gSkyCubeTex[i]->Release();
            gSkyCubeTex[i] = NULL;
        }
    }
    gFactionTexDevice = NULL;
    gFactionTexDeviceGen = 0u;
}

static bool LoadLooseSplashFallback(IDirect3DDevice9 *dev, const char *gameDir, FactionTexSlot *outSlot) {
    if (!dev || !outSlot) return false;
    auto tryPath = [&](const char *path) -> bool {
        if (!path || !*path) return false;
        D3DXIMAGE_INFO info = {};
        IDirect3DTexture9 *tex = NULL;
        const HRESULT hr = D3DXCreateTextureFromFileExA(
            dev, path, D3DX_DEFAULT, D3DX_DEFAULT,
            0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
            D3DX_FILTER_TRIANGLE, D3DX_FILTER_BOX, 0, &info, NULL, &tex);
        if (FAILED(hr) || !tex) {
            if (tex) tex->Release();
            return false;
        }
        outSlot->tex = tex;
        outSlot->srcW = info.Width;
        outSlot->srcH = info.Height;
        outSlot->fromLooseSplash = true;
        return true;
    };
    if (tryPath("data/textures/splash.tga")) return true;
    if (gameDir && *gameDir) {
        char fullPath[1024] = {};
        const int n = std::snprintf(fullPath, sizeof fullPath, "%s/data/textures/splash.tga", gameDir);
        if (n > 0 && static_cast<size_t>(n) < sizeof fullPath && tryPath(fullPath)) return true;
    }
    return false;
}

static void LoadFactionTextures(IDirect3DDevice9 *dev) {
    ReleaseFactionTextures();
    if (!dev) return;
    gFactionTexDevice = dev;
    gFactionTexDeviceGen = CurrentMuseumDeviceGen();
    const char *dir = PortGameDir();
    if (!dir || !*dir) return;
    char err[192] = {};
    CeAssetCatalog *cat = PortCeAssetCatalogOpenKinds(dir, CE_ASSET_TEXTURE, err, sizeof err);
    unsigned loaded = 0;
    unsigned skyLoaded = 0;
    if (cat) {
        static const char *const kPaths[kFactionTexCount] = {
            "goodies\\goodies_F_Bakgrnd.tga",
            "goodies\\goodies_M_Bakgrnd.tga",
            "FrontEnd\\v3\\FE_BEA_Title2.tga",
            "FrontEnd\\LostToys.tga",
            "goodies\\ca_be_aquila\\ca_bea_battle_pic.tga",
            "goodies\\ca_be_aquila\\ca_be_final01.tga",
        };
        for (unsigned i = 0; i < kFactionTexCount; ++i) {
            const size_t idx = PortCeAssetCatalogFindTexture(cat, kPaths[i]);
            if (idx == SIZE_MAX) continue;
            FactionUploadCtx ctx = {dev, &gFactionTex[i]};
            if (PortCeAssetCatalogReadTexture(cat, idx, FactionUploadCallback, &ctx) && gFactionTex[i].tex)
                ++loaded;
        }
        static const char *const kSkyPaths[5] = {
            "cubes\\cube01_cent.tga",
            "cubes\\cube01_up.tga",
            "cubes\\cube01_right.tga",
            "cubes\\cube01_down.tga",
            "cubes\\cube01_left.tga",
        };
        for (unsigned i = 0; i < 5u; ++i) {
            const size_t idx = PortCeAssetCatalogFindTexture(cat, kSkyPaths[i]);
            if (idx == SIZE_MAX) continue;
            SkyUploadCtx ctx = {dev, &gSkyCubeTex[i]};
            if (PortCeAssetCatalogReadTexture(cat, idx, SkyUploadCallback, &ctx) && gSkyCubeTex[i])
                ++skyLoaded;
        }
        PortCeAssetCatalogClose(cat);
    }
    // Fallback for Founder Seal: loose data/textures/splash.tga via D3DXCreateTextureFromFileExA
    // (cropped to the golden Lost Toys emblem), else blank obsidian-and-brass architectural medallion.
    if (!gFactionTex[kFactionTexFounderSeal].tex) {
        if (LoadLooseSplashFallback(dev, dir, &gFactionTex[kFactionTexFounderSeal]))
            ++loaded;
    }
    PortSessionLog(PORT_SESSION_GAME, false,
                   "museum: loaded %u of %u retail facade/faction textures, %u of 5 retail skybox faces",
                   loaded, static_cast<unsigned>(kFactionTexCount), skyLoaded);
}

static void ReleaseHallGpuBuffers() {
    if (gHallGpu.litVb)        { gHallGpu.litVb->Release();        gHallGpu.litVb = NULL; }
    if (gHallGpu.litIb)        { gHallGpu.litIb->Release();        gHallGpu.litIb = NULL; }
    if (gHallGpu.shadowVb)     { gHallGpu.shadowVb->Release();     gHallGpu.shadowVb = NULL; }
    if (gHallGpu.shadowIb)     { gHallGpu.shadowIb->Release();     gHallGpu.shadowIb = NULL; }
    if (gHallGpu.glassVb)      { gHallGpu.glassVb->Release();      gHallGpu.glassVb = NULL; }
    if (gHallGpu.glassIb)      { gHallGpu.glassIb->Release();      gHallGpu.glassIb = NULL; }
    if (gHallGpu.portalSignVb) { gHallGpu.portalSignVb->Release(); gHallGpu.portalSignVb = NULL; }
    if (gHallGpu.portalSignIb) { gHallGpu.portalSignIb->Release(); gHallGpu.portalSignIb = NULL; }
    if (gHallGpu.oceanVb)      { gHallGpu.oceanVb->Release();      gHallGpu.oceanVb = NULL; }
    if (gHallGpu.oceanIb)      { gHallGpu.oceanIb->Release();      gHallGpu.oceanIb = NULL; }
    if (gHallGpu.headlandVb)   { gHallGpu.headlandVb->Release();   gHallGpu.headlandVb = NULL; }
    if (gHallGpu.headlandIb)   { gHallGpu.headlandIb->Release();   gHallGpu.headlandIb = NULL; }
    gHallGpu.device = NULL;
    gHallGpu.deviceGen = 0u;
    gHallGpu.attempted = false;
    gHallGpu.oceanDevice = NULL;
    gHallGpu.oceanDeviceGen = 0u;
    gHallGpu.oceanAttempted = false;
    gHallGpu.oceanVertCount = 0;
    gHallGpu.oceanIdxCount = 0;
    gHallGpu.headlandVertCount = 0;
    gHallGpu.headlandIdxCount = 0;
}

static bool UploadManagedVb(IDirect3DDevice9 *dev, const void *data, unsigned bytes,
                            DWORD fvf, IDirect3DVertexBuffer9 **outVb) {
    if (!dev || !data || !bytes || !outVb) return false;
    IDirect3DVertexBuffer9 *vb = NULL;
    if (FAILED(dev->CreateVertexBuffer(bytes, D3DUSAGE_WRITEONLY, fvf, D3DPOOL_MANAGED, &vb, NULL)) || !vb)
        return false;
    void *dst = NULL;
    if (FAILED(vb->Lock(0, bytes, &dst, 0)) || !dst) {
        vb->Release();
        return false;
    }
    std::memcpy(dst, data, bytes);
    vb->Unlock();
    *outVb = vb;
    return true;
}

static bool UploadManagedIb(IDirect3DDevice9 *dev, const void *data, unsigned bytes,
                            D3DFORMAT fmt, IDirect3DIndexBuffer9 **outIb) {
    if (!dev || !data || !bytes || !outIb) return false;
    IDirect3DIndexBuffer9 *ib = NULL;
    if (FAILED(dev->CreateIndexBuffer(bytes, D3DUSAGE_WRITEONLY, fmt, D3DPOOL_MANAGED, &ib, NULL)) || !ib)
        return false;
    void *dst = NULL;
    if (FAILED(ib->Lock(0, bytes, &dst, 0)) || !dst) {
        ib->Release();
        return false;
    }
    std::memcpy(dst, data, bytes);
    ib->Unlock();
    *outIb = ib;
    return true;
}

static void FindMaterials();

static void EnsureHallGpuBuffers(IDirect3DDevice9 *dev) {
    if (!gPtr || !dev) return;
    const uint64_t gen = CurrentMuseumDeviceGen();
    if (gHallGpu.attempted && gHallGpu.device == dev && gHallGpu.deviceGen == gen) return;
    if (gHallGpu.device && (gHallGpu.device != dev || gHallGpu.deviceGen != gen)) {
        ReleaseHallGpuBuffers();
        FindMaterials();
    }
    gHallGpu.device = dev;
    gHallGpu.deviceGen = gen;
    gHallGpu.attempted = true;

    if (gPtr->usedVerts && gPtr->usedIndices) {
        const bool okV = UploadManagedVb(dev, gPtr->vertices, gPtr->usedVerts * sizeof(LitVertex),
                                         D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1,
                                         &gHallGpu.litVb);
        const bool okI = okV && UploadManagedIb(dev, gPtr->sortedIndices, gPtr->usedIndices * sizeof(uint32_t),
                                                D3DFMT_INDEX32, &gHallGpu.litIb);
        if (!okV || !okI) {
            if (gHallGpu.litVb) { gHallGpu.litVb->Release(); gHallGpu.litVb = NULL; }
            if (gHallGpu.litIb) { gHallGpu.litIb->Release(); gHallGpu.litIb = NULL; }
        }
    }
    if (gPtr->shadowVertCount && gPtr->shadowIdxCount >= 3u) {
        const bool okV = UploadManagedVb(dev, gPtr->shadowVerts, gPtr->shadowVertCount * sizeof(OverlayVertex),
                                         D3DFVF_XYZ | D3DFVF_DIFFUSE, &gHallGpu.shadowVb);
        const bool okI = okV && UploadManagedIb(dev, gPtr->shadowIndices, gPtr->shadowIdxCount * sizeof(uint16_t),
                                                D3DFMT_INDEX16, &gHallGpu.shadowIb);
        if (!okV || !okI) {
            if (gHallGpu.shadowVb) { gHallGpu.shadowVb->Release(); gHallGpu.shadowVb = NULL; }
            if (gHallGpu.shadowIb) { gHallGpu.shadowIb->Release(); gHallGpu.shadowIb = NULL; }
        }
    }
    if (gPtr->glassVertCount && gPtr->glassIdxCount >= 3u) {
        const bool okV = UploadManagedVb(dev, gPtr->glassVerts, gPtr->glassVertCount * sizeof(OverlayVertex),
                                         D3DFVF_XYZ | D3DFVF_DIFFUSE, &gHallGpu.glassVb);
        const bool okI = okV && UploadManagedIb(dev, gPtr->glassIndices, gPtr->glassIdxCount * sizeof(uint16_t),
                                                D3DFMT_INDEX16, &gHallGpu.glassIb);
        if (!okV || !okI) {
            if (gHallGpu.glassVb) { gHallGpu.glassVb->Release(); gHallGpu.glassVb = NULL; }
            if (gHallGpu.glassIb) { gHallGpu.glassIb->Release(); gHallGpu.glassIb = NULL; }
        }
    }
    if (gPtr->signVertCount && gPtr->signIdxCount >= 3u) {
        const bool okV = UploadManagedVb(dev, gPtr->signVerts, gPtr->signVertCount * sizeof(SignTexVertex),
                                         D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1, &gHallGpu.portalSignVb);
        const bool okI = okV && UploadManagedIb(dev, gPtr->signIndices, gPtr->signIdxCount * sizeof(uint16_t),
                                                D3DFMT_INDEX16, &gHallGpu.portalSignIb);
        if (!okV || !okI) {
            if (gHallGpu.portalSignVb) { gHallGpu.portalSignVb->Release(); gHallGpu.portalSignVb = NULL; }
            if (gHallGpu.portalSignIb) { gHallGpu.portalSignIb->Release(); gHallGpu.portalSignIb = NULL; }
        }
    }
}

struct BannerTexVertex { float x, y, z; DWORD colour; float u, v; };

static void DrawFactionBanners(IDirect3DDevice9 *dev, float floorZ) {
    if (!gGrandMode || !dev) return;
    const uint64_t gen = CurrentMuseumDeviceGen();
    if (gFactionTexDevice != dev || gFactionTexDeviceGen != gen) LoadFactionTextures(dev);

    struct BannerPlacement {
        FactionTexId texId;
        float lx, ly, cz;     // visitor-local centre, height above floor
        float nx, ny;         // unit normal pointing into room
        float w, h;           // max frame width & height (aspect-fitted when preserveAspect is true)
        float u0, v0, u1, v1; // UV window
        DWORD tintTop;
        DWORD tintBot;
        DWORD fallbackTop;
        DWORD fallbackBot;
        bool alphaOverlay;    // true for alpha-cutout + alpha-blend retail emblems/title
        bool hasArchFrame;    // true when 3D architectural shadowbox/frieze is already built in BuildGrandComplex
        bool preserveAspect;  // true to fit native source aspect ratio via CeMuseumArt::FitFrame
    };
    static const BannerPlacement kBanners[] = {
        // 1. Forseti Wing West Apse (4:3 full official Lost Toys Forseti mural!)
        {kFactionTexForsetiMural, -43.25f, 41.00f, 3.38f,  1.0f,  0.0f, 5.86f, 4.40f,
         0.00f, 0.00f, 1.00f, 1.00f, 0xffffffffu, 0xfff0ece6u, 0xff2b4c6eu, 0xff1a3048u, false, false, false},
        // 2. Muspell Wing East Apse (4:3 full official Lost Toys crimson Muspell Army mural!)
        {kFactionTexMuspellMural,  43.12f, 41.00f, 3.27f, -1.0f,  0.0f, 4.90f, 3.68f,
         0.00f, 0.00f, 1.00f, 1.00f, 0xffffffffu, 0xfff0ece6u, 0xff6e2b20u, 0xff3c1814u, false, false, false},
        // 3. Grand Atrium North Wall Left Niche (official Forseti emblem crop [0..0.46, 0..0.56])
        {kFactionTexForsetiMural, -10.80f, 51.48f, 3.15f,  0.0f, -1.0f, 3.40f, 3.10f,
         0.00f, 0.00f, 0.46f, 0.56f, 0xffffffffu, 0xfff0ece6u, 0xff2b4c6eu, 0xff1a3048u, false, false, false},
        // 4. Grand Atrium North Wall Right Niche (official Muspell emblem crop [0..0.46, 0..0.56])
        {kFactionTexMuspellMural,  10.80f, 51.48f, 3.15f,  0.0f, -1.0f, 3.40f, 3.10f,
         0.00f, 0.00f, 0.46f, 0.56f, 0xffffffffu, 0xfff0ece6u, 0xff6e2b20u, 0xff3c1814u, false, false, false},
        // 5. Aviation & Naval Hangar West-North Standard (official Forseti crest)
        {kFactionTexForsetiMural, -13.20f, 77.36f, 4.92f,  0.0f, -1.0f, 5.60f, 5.12f,
         0.00f, 0.00f, 0.46f, 0.56f, 0xffffffffu, 0xfff0ece6u, 0xff2b4c6eu, 0xff1a3048u, false, false, false},
        // 6. Aviation & Naval Hangar East-North Standard (official Muspell crest)
        {kFactionTexMuspellMural,  13.20f, 77.36f, 4.92f,  0.0f, -1.0f, 5.60f, 5.12f,
         0.00f, 0.00f, 0.46f, 0.56f, 0xffffffffu, 0xfff0ece6u, 0xff6e2b20u, 0xff3c1814u, false, false, false},
        // 7. Courtyard Grand Entrance Metallic Title (FE_BEA_Title2.tga, exact 2:1 aspect, recessed dark-slate frieze)
        {kFactionTexTitle,          0.00f, 19.58f, 10.25f, 0.0f, -1.0f, 6.60f, 3.30f,
         0.00f, 0.00f, 1.00f, 1.00f, 0xffffffffu, 0xfff4f0e8u, 0xff2b4c6eu, 0xff1a3048u, true,  true,  true},
        // 8. Courtyard Pediment Apex Lost Toys Founder Seal (strictly <= 1.35m height, clears z=12.65..13.65 cornice!)
        {kFactionTexFounderSeal,    0.00f, 19.16f, 14.50f, 0.0f, -1.0f, 1.32f, 1.24f,
         0.00f, 0.00f, 1.00f, 1.00f, 0xffffffffu, 0xfff6eee0u, 0xff2b343cu, 0xff1a2026u, true,  true,  true},
        // 9. Courtyard West Outer Colonnade Shadowbox Key Battle Art (ca_bea_battle_pic.tga, crop built-in black bars)
        {kFactionTexCoverWest,    -11.05f, 19.59f,  4.10f, 0.0f, -1.0f, 3.10f, 2.25f,
         0.00f, 0.15f, 1.00f, 0.85f, 0xffeae6deu, 0xffd8d3c8u, 0xff343e48u, 0xff232a32u, false, true,  true},
        // 10. Courtyard East Outer Colonnade Shadowbox Cover Art (ca_be_final01.tga, wall 19.70 -> art 19.59)
        {kFactionTexCoverEast,     11.05f, 19.59f,  4.10f, 0.0f, -1.0f, 3.10f, 2.25f,
         0.00f, 0.00f, 1.00f, 1.00f, 0xffeae6deu, 0xffd8d3c8u, 0xff343e48u, 0xff232a32u, false, true,  true},
    };

    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetTexture(1, NULL);
    dev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    SetMuseumAnisoSampler(dev);
    dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

    for (unsigned i = 0; i < sizeof(kBanners) / sizeof(kBanners[0]); ++i) {
        const BannerPlacement &b = kBanners[i];
        const FactionTexSlot &slot = gFactionTex[b.texId];
        IDirect3DTexture9 *tex = slot.tex;
        if (b.alphaOverlay && !tex) continue;

        float u0 = b.u0, v0 = b.v0, u1 = b.u1, v1 = b.v1;
        bool useAlpha = b.alphaOverlay;
        unsigned fitW = slot.srcW, fitH = slot.srcH;
        if (b.texId == kFactionTexFounderSeal && slot.fromLooseSplash) {
            // Loose data/textures/splash.tga fallback: crop cleanly to the golden Lost Toys emblem.
            u0 = 0.10f; u1 = 0.90f;
            v0 = 0.24f; v1 = 0.76f;
            useAlpha = false;
        }
        if (fitW && fitH && (u1 - u0 < 0.999f || v1 - v0 < 0.999f)) {
            fitW = static_cast<unsigned>(float(fitW) * (u1 - u0) + 0.5f);
            fitH = static_cast<unsigned>(float(fitH) * (v1 - v0) + 0.5f);
            if (!fitW) fitW = 1u;
            if (!fitH) fitH = 1u;
        }

        const float rx = -b.ny;
        const float ry =  b.nx;
        const float wx = CeMuseumWorld::kEntranceX + b.lx;
        const float wy = CeMuseumWorld::kEntranceY + b.ly;
        const float wz = floorZ - b.cz;
        float hw = b.w * 0.5f;
        float hh = b.h * 0.5f;
        if (b.preserveAspect && tex && fitW && fitH) {
            const CeMuseumArt::QuadExtent ext = CeMuseumArt::FitFrame(fitW, fitH, b.w, b.h);
            hw = ext.halfW;
            hh = ext.halfH;
        }

        if (!b.alphaOverlay && !b.hasArchFrame) {
            // Crisp brushed-brass inner bevel frame behind interior faction murals/crests
            const float fw = hw + 0.09f, fh = hh + 0.09f;
            const float fx = wx - b.nx * 0.015f, fy = wy - b.ny * 0.015f;
            const Vertex frameQ[4] = {
                {fx - rx * fw, fy - ry * fw, wz - fh, 0xffd6b064},
                {fx + rx * fw, fy + ry * fw, wz - fh, 0xffd6b064},
                {fx - rx * fw, fy - ry * fw, wz + fh, 0xff9a783a},
                {fx + rx * fw, fy + ry * fw, wz + fh, 0xff9a783a},
            };
            dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
            dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
            dev->SetTexture(0, NULL);
            dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
            dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
            dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
            dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
            dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, frameQ, sizeof(Vertex));
        }

        const float px = b.hasArchFrame ? wx : (wx + b.nx * 0.01f);
        const float py = b.hasArchFrame ? wy : (wy + b.ny * 0.01f);
        if (tex) {
            const BannerTexVertex q[4] = {
                {px - rx * hw, py - ry * hw, wz - hh, b.tintTop, u0, v0},
                {px + rx * hw, py + ry * hw, wz - hh, b.tintTop, u1, v0},
                {px - rx * hw, py - ry * hw, wz + hh, b.tintBot, u0, v1},
                {px + rx * hw, py + ry * hw, wz + hh, b.tintBot, u1, v1},
            };
            dev->SetTexture(0, tex);
            dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
            dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
            dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
            if (useAlpha) {
                dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
                dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
                dev->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
                dev->SetRenderState(D3DRS_ALPHAREF, 16);
                dev->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
                dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
                dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
                dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
            } else {
                dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
                dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
                dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
            }
            dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
            dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(BannerTexVertex));
        } else {
            // Clean matte stone & brass plaque fallback if retail files are absent
            const Vertex fbQ[4] = {
                {px - rx * hw, py - ry * hw, wz - hh, b.fallbackTop},
                {px + rx * hw, py + ry * hw, wz - hh, b.fallbackTop},
                {px - rx * hw, py - ry * hw, wz + hh, b.fallbackBot},
                {px + rx * hw, py + ry * hw, wz + hh, b.fallbackBot},
            };
            dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
            dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
            dev->SetTexture(0, NULL);
            dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
            dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
            dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
            dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
            dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, fbQ, sizeof(Vertex));
        }
    }
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
}

static void FindMaterials() {
    if (!gPtr) return;
    IDirect3DDevice9 *dev = LT.GetDevice();
    CeMuseumTextures::LoadAll(dev);
    for (unsigned m = 0; m < kMatCount; ++m) {
        const CeMuseumTexId tid = kMaterials[m].tex;
        gPtr->texture[m] = tid < CE_MUSEUM_TEX_COUNT ? CeMuseumTextures::Get(tid) : NULL;
    }
    LoadFactionTextures(dev);
}

static void ReleaseMaterials() {
    if (gPtr) {
        for (unsigned m = 0; m < kMatCount; ++m) gPtr->texture[m] = NULL;
    }
    ReleaseFactionTextures();
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
    CeMuseumDiorama::RuntimeShutdown();
    CeMuseumAudioKiosk::RuntimeShutdown();
#ifdef CE_MUSEUM_HAS_ARCHIVE_PEDESTAL
    std::memset(gArchiveKeyPrev, 0, sizeof gArchiveKeyPrev);
    CeArchivePedestal::RuntimeShutdown();
#endif
    ReleaseHallGpuBuffers();
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
            CeMuseumDiorama::RuntimeInit();
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
        float dt = 0.0f;
        if (gHasStepClock && now > gLastStepClock) dt = float(now - gLastStepClock);
        gLastStepClock = now;
        gHasStepClock = true;
        if (dt > 0.25f) dt = 0.25f;
        if (!(dt > 0.0f)) dt = 0.0f;

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
        CeMuseumArt::RuntimeSetAim(eyeX, eyeY, eyeZ, aimX, aimY, aimZ);
        CeMuseumArt::RuntimeUpdate(wing);
        CeMuseumDiorama::RuntimeUpdate(wing, dt, paused, eyeX, eyeY, eyeZ, aimX, aimY, aimZ);
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
        // Consume stray Fire clicks in the Cinema Room too so visitors never fire weapons inside the theater.
        if (Cinema::s.inRoom) { CinemaAction(release, value); return true; }
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
        if (CeMuseumDiorama::RuntimeHandleFire(release, value)) return true;
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
        if (CeMuseumDiorama::RuntimeIsAimingControl()) return true;
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
    if (CeMuseumDiorama::RuntimeCue(local.x, local.y, fx, fy, gCurrentWing,
                                    title, titleCap, line, lineCap, note, noteCap))
        return true;
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
    struct DotVertex { float x, y, z, rhw; DWORD colour; };
    auto drawDiamond = [&](float r, DWORD col) {
        const DotVertex q[6] = {
            {cx, cy - r, 0.0f, 1.0f, col}, {cx + r, cy, 0.0f, 1.0f, col}, {cx, cy + r, 0.0f, 1.0f, col},
            {cx, cy - r, 0.0f, 1.0f, col}, {cx, cy + r, 0.0f, 1.0f, col}, {cx - r, cy, 0.0f, 1.0f, col},
        };
        dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, q, sizeof(DotVertex));
    };
    RENDERINFO.SetFogEnabled(false);
    RENDERINFO.SetLightingEnabled(false);
    RENDERINFO.SetShader(NULL);
    RENDERINFO.SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    RENDERINFO.Apply();
    dev->SetPixelShader(NULL);
    LT.D3D_SetTexture(0, NULL);
    LT.D3D_SetTexture(1, NULL);
    LT.SRS(D3DRS_ZENABLE, FALSE);
    LT.SRS(D3DRS_ZWRITEENABLE, FALSE);
    LT.SRS(D3DRS_CULLMODE, D3DCULL_NONE);
    LT.SRS(D3DRS_ALPHATESTENABLE, FALSE);
    LT.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
    LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    LT.STS(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    LT.STS(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    LT.STS(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    LT.STS(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    LT.STS(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    drawDiamond(5.0f, 0xb014181e);
    drawDiamond(3.0f, 0xfff5ebd4);
    LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    LT.STS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    LT.STS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    LT.STS(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    LT.STS(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
}

// Textured, horizon-faded shimmering coastal ocean plane rendered over the real painted retail
// Battle Engine Aquila coastal KempyCube skybox (mKempyCube->Render() runs right before us).
// Built ONCE into a D3DPOOL_MANAGED GPU VB/IB — zero per-frame trig loops and zero dev->Clear!
static DWORD LerpColour(DWORD a, DWORD b, float t) {
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    auto ch = [&](unsigned sh) -> DWORD {
        const float va = float((a >> sh) & 255u), vb = float((b >> sh) & 255u);
        int v = static_cast<int>(va + (vb - va) * t);
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        return static_cast<DWORD>(v) << sh;
    };
    return 0xff000000u | ch(16) | ch(8) | ch(0);
}

static DWORD WithAlpha(DWORD rgb, float alpha) {
    int ia = static_cast<int>(Clamp01(alpha) * 255.0f);
    if (ia < 0) ia = 0;
    if (ia > 255) ia = 255;
    return (static_cast<DWORD>(ia) << 24) | (rgb & 0x00ffffffu);
}

struct OceanVertex { float x, y, z; DWORD colour; float u, v; };

static void EnsureOceanGpuBuffers(IDirect3DDevice9 *dev) {
    if (!dev) return;
    const uint64_t gen = CurrentMuseumDeviceGen();
    if (gHallGpu.oceanAttempted && gHallGpu.oceanDevice == dev && gHallGpu.oceanDeviceGen == gen) return;
    if (gHallGpu.oceanVb)    { gHallGpu.oceanVb->Release();    gHallGpu.oceanVb = NULL; }
    if (gHallGpu.oceanIb)    { gHallGpu.oceanIb->Release();    gHallGpu.oceanIb = NULL; }
    if (gHallGpu.headlandVb) { gHallGpu.headlandVb->Release(); gHallGpu.headlandVb = NULL; }
    if (gHallGpu.headlandIb) { gHallGpu.headlandIb->Release(); gHallGpu.headlandIb = NULL; }
    gHallGpu.oceanDevice = dev;
    gHallGpu.oceanDeviceGen = gen;
    gHallGpu.oceanAttempted = true;
    gHallGpu.oceanVertCount = 0;
    gHallGpu.oceanIdxCount = 0;
    gHallGpu.headlandVertCount = 0;
    gHallGpu.headlandIdxCount = 0;

    // 7 concentric rings from directly beneath the seaside cliff out to r = 230m horizon fade-out.
    // oceanZ = 5.5 m below visitor eye grounds the coastal courtyard terrace over the sunlit bay.
    const float oceanZ = 5.5f; // 5.5 m below visitor eye (native +Z down)
    const float radii[7] = {0.0f, 12.0f, 44.0f, 92.0f, 140.0f, 185.0f, 230.0f};
    const float alphas[7] = {1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 0.52f, 0.00f};
    const DWORD tints[7] = {
        0xff7cc0e0,
        0xff86c8e6,
        0xff9ad2ee,
        0xffb2def4,
        0xffcceaf8,
        0xffdff2fc,
        0xffeaf6ff,
    };
    const unsigned kSegs = 48u;
    const unsigned kRings = 7u;
    const unsigned kVerts = kRings * (kSegs + 1u);
    const unsigned kIndices = (kRings - 1u) * kSegs * 6u;
    const float kTileMetres = 18.0f;

    OceanVertex verts[kVerts];
    uint16_t indices[kIndices];
    unsigned vAt = 0;
    for (unsigned r = 0; r < kRings; ++r) {
        const float rad = radii[r];
        const float aRing = alphas[r];
        for (unsigned i = 0; i <= kSegs; ++i) {
            const float ang = float(i) * (6.283185307f / float(kSegs));
            const float ca = std::cos(ang), sa = std::sin(ang);
            const float ox = ca * rad, oy = sa * rad;
            const float azDot = ca * kSunDirX + sa * kSunDirY;
            const float spec = Clamp01((azDot - 0.55f) / 0.45f);
            const float shim = (r >= 1u) ? (spec * spec * 0.34f) : 0.0f;
            const DWORD col = WithAlpha(LerpColour(tints[r], CeMuseumWorld::kGrandSunGlow, shim), aRing);
            verts[vAt++] = OceanVertex{ox, oy, oceanZ, col, ox / kTileMetres, oy / kTileMetres};
        }
    }
    unsigned iAt = 0;
    const unsigned stride = kSegs + 1u;
    for (unsigned r = 0; r + 1u < kRings; ++r) {
        for (unsigned i = 0; i < kSegs; ++i) {
            const uint16_t v00 = static_cast<uint16_t>(r * stride + i);
            const uint16_t v10 = static_cast<uint16_t>(v00 + 1u);
            const uint16_t v01 = static_cast<uint16_t>(v00 + stride);
            const uint16_t v11 = static_cast<uint16_t>(v01 + 1u);
            indices[iAt++] = v00; indices[iAt++] = v10; indices[iAt++] = v11;
            indices[iAt++] = v00; indices[iAt++] = v11; indices[iAt++] = v01;
        }
    }
    if (UploadManagedVb(dev, verts, sizeof(verts), D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1, &gHallGpu.oceanVb) &&
        UploadManagedIb(dev, indices, sizeof(indices), D3DFMT_INDEX16, &gHallGpu.oceanIb)) {
        gHallGpu.oceanVertCount = kVerts;
        gHallGpu.oceanIdxCount = kIndices;
    } else {
        if (gHallGpu.oceanVb) { gHallGpu.oceanVb->Release(); gHallGpu.oceanVb = NULL; }
        if (gHallGpu.oceanIb) { gHallGpu.oceanIb->Release(); gHallGpu.oceanIb = NULL; }
    }

    // Build sculpted distant coastal headlands & sea-stacks resting cleanly on oceanZ = 5.5 m
    // so the outdoor courtyard feels grounded in a real sunlit Battle Engine Aquila archipelago!
    struct IslandRidge {
        float azDeg;    // azimuth around visitor (0 = East, -90 = South courtyard vista, 180 = West)
        float dist;     // metres from origin
        float halfArc;  // angular half-span in degrees
        float peakH;    // peak elevation above ocean surface (metres)
        float mist;     // atmospheric coastal haze blend [0..1]
    };
    static const IslandRidge kIslands[] = {
        {-132.0f, 128.0f, 14.0f, 22.0f, 0.24f}, // SW dramatic sea-stack headland
        {-102.0f, 165.0f, 16.0f, 17.0f, 0.42f}, // SSW distant coastal island ridge
        { -74.0f, 142.0f, 13.0f, 26.0f, 0.30f}, // SSE sunlit limestone cliff headland
        { -42.0f, 118.0f, 11.0f, 19.0f, 0.20f}, // SE rocky bay breakwater stack
        {-166.0f, 152.0f, 15.0f, 21.0f, 0.36f}, // WSW distant archipelago ridge
        { -12.0f, 160.0f, 14.0f, 16.0f, 0.40f}, // ESE headland cape
        {  34.0f, 172.0f, 12.0f, 18.0f, 0.46f}, // ENE far sea-stack
        { 148.0f, 168.0f, 14.0f, 20.0f, 0.44f}, // WNW far rocky island
    };
    constexpr unsigned kIslandCount = sizeof(kIslands) / sizeof(kIslands[0]);
    constexpr unsigned kProfileSegs = 6u; // 7 stations along ridge -> 6 bay-facing quads (12 tris) per island
    OceanVertex hVerts[kIslandCount * (kProfileSegs + 1u) * 2u];
    uint16_t hIndices[kIslandCount * kProfileSegs * 6u];
    unsigned hv = 0, hi = 0;
    const DWORD mistCol = 0xffcde4f2u;
    const DWORD rockBase = 0xff6b7a82u;
    const DWORD rockCrest = 0xffd2c7b2u;
    for (unsigned k = 0; k < kIslandCount; ++k) {
        const IslandRidge &ir = kIslands[k];
        const uint16_t vStart = static_cast<uint16_t>(hv);
        for (unsigned s = 0; s <= kProfileSegs; ++s) {
            const float f = float(s) / float(kProfileSegs); // 0..1 across ridge
            const float ang = (ir.azDeg + (f * 2.0f - 1.0f) * ir.halfArc) * (3.14159265f / 180.0f);
            const float ca = std::cos(ang), sa = std::sin(ang);
            // Asymmetric craggy coastal headland profile tapering cleanly into the water at both ends
            const float env = std::sin(f * 3.14159265f);
            const float crag = 0.78f + 0.22f * std::sin(f * 9.42478f + float(k) * 1.3f);
            const float elev = ir.peakH * env * crag;
            const float baseR = ir.dist - 6.0f * env;
            const float crestR = ir.dist + 5.0f * env;
            const float sunDot = Clamp01(0.55f + 0.45f * (-ca * kSunDirX - sa * kSunDirY));
            const DWORD litBase = LerpColour(rockBase, mistCol, ir.mist + 0.14f);
            const DWORD litCrest = LerpColour(LerpColour(rockBase, rockCrest, sunDot), mistCol, ir.mist);
            const float uCoord = f * 4.5f;
            hVerts[hv++] = OceanVertex{ca * baseR, sa * baseR, oceanZ + 0.35f, litBase, uCoord, 2.0f};
            hVerts[hv++] = OceanVertex{ca * crestR, sa * crestR, oceanZ - elev, litCrest, uCoord, 0.0f};
        }
        for (unsigned s = 0; s < kProfileSegs; ++s) {
            const uint16_t b0 = static_cast<uint16_t>(vStart + s * 2u);
            const uint16_t c0 = static_cast<uint16_t>(b0 + 1u);
            const uint16_t b1 = static_cast<uint16_t>(b0 + 2u);
            const uint16_t c1 = static_cast<uint16_t>(b0 + 3u);
            hIndices[hi++] = b0; hIndices[hi++] = c0; hIndices[hi++] = c1;
            hIndices[hi++] = b0; hIndices[hi++] = c1; hIndices[hi++] = b1;
        }
    }
    if (UploadManagedVb(dev, hVerts, hv * sizeof(OceanVertex), D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1,
                        &gHallGpu.headlandVb) &&
        UploadManagedIb(dev, hIndices, hi * sizeof(uint16_t), D3DFMT_INDEX16, &gHallGpu.headlandIb)) {
        gHallGpu.headlandVertCount = hv;
        gHallGpu.headlandIdxCount = hi;
    } else {
        if (gHallGpu.headlandVb) { gHallGpu.headlandVb->Release(); gHallGpu.headlandVb = NULL; }
        if (gHallGpu.headlandIb) { gHallGpu.headlandIb->Release(); gHallGpu.headlandIb = NULL; }
    }
}

extern "C" void PortCeMuseumAuthoredBackdrop() {
    IDirect3DDevice9 *dev = LT.GetDevice();
    if (!dev || gMirrorPass || !PortCeMuseumAuthoredMap()) return;
    if (gFactionTexDevice != dev || gFactionTexDeviceGen != CurrentMuseumDeviceGen()) LoadFactionTextures(dev);
    EnsureOceanGpuBuffers(dev);

    IDirect3DStateBlock9 *saved = NULL;
    if (FAILED(dev->CreateStateBlock(D3DSBT_ALL, &saved)) || !saved) return;
    if (FAILED(saved->Capture())) { saved->Release(); return; }

    D3DMATRIX world = {}, view = {}, proj = {};
    world._11 = world._22 = world._33 = world._44 = 1.0f;
    RENDERINFO.GetView(&view);
    RENDERINFO.GetProjection(&proj);
    // Zero view translation so the skybox and coastal ocean plane stay centered around the viewer.
    view._41 = 0.0f; view._42 = 0.0f; view._43 = 0.0f;

    dev->SetVertexShader(NULL);
    dev->SetPixelShader(NULL);
    dev->SetTransform(D3DTS_WORLD, &world);
    dev->SetTransform(D3DTS_VIEW, &view);
    dev->SetTransform(D3DTS_PROJECTION, &proj);
    dev->SetTexture(1, NULL);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
    dev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

    // 1. Render real painted retail coastal skybox (cubes\cube01_{cent,up,right,down,left}.tga),
    // or a clean vertex-colored coastal sky dome if retail skybox textures are absent (zero Clear!).
    bool hasSky = (gSkyCubeTex[0] && gSkyCubeTex[1] && gSkyCubeTex[2] && gSkyCubeTex[3] && gSkyCubeTex[4]);
    constexpr float kS = 1800.0f;
    constexpr float kTop = 900.0f;
    if (hasSky) {
        struct SkyVertex { float x, y, z, u, v; };
        constexpr float kU0 = 0.001953125f, kU1 = 0.998046875f, kV1 = 0.749023438f;
        static const SkyVertex kSkyVerts[20] = {
            {-kS, -kS, -kS, kU0, kU0}, { kS, -kS, -kS, kU1, kU0},
            {-kS,  kS, -kS, kU0, kU1}, { kS,  kS, -kS, kU1, kU1},
            { kS,  kS, -kS, kU1, kU0}, { kS,  kS, kTop, kU1, kV1},
            {-kS,  kS, -kS, kU0, kU0}, {-kS,  kS, kTop, kU0, kV1},
            { kS, -kS, -kS, kU1, kU0}, { kS, -kS, kTop, kU1, kV1},
            { kS,  kS, -kS, kU0, kU0}, { kS,  kS, kTop, kU0, kV1},
            {-kS, -kS, -kS, kU1, kU0}, {-kS, -kS, kTop, kU1, kV1},
            { kS, -kS, -kS, kU0, kU0}, { kS, -kS, kTop, kU0, kV1},
            {-kS,  kS, -kS, kU1, kU0}, {-kS,  kS, kTop, kU1, kV1},
            {-kS, -kS, -kS, kU0, kU0}, {-kS, -kS, kTop, kU0, kV1},
        };
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        dev->SetFVF(D3DFVF_XYZ | D3DFVF_TEX1);
        for (unsigned f = 0; f < 5u; ++f) {
            dev->SetTexture(0, gSkyCubeTex[f]);
            dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, &kSkyVerts[f * 4u], sizeof(SkyVertex));
        }
    } else {
        const DWORD cZen = CeMuseumWorld::kGrandSkyZenith;
        const DWORD cHor = CeMuseumWorld::kGrandSkyHorizon;
        const Vertex kFallbackSky[20] = {
            {-kS, -kS, -kS, cZen}, { kS, -kS, -kS, cZen}, {-kS,  kS, -kS, cZen}, { kS,  kS, -kS, cZen},
            { kS,  kS, -kS, cZen}, { kS,  kS, kTop, cHor}, {-kS,  kS, -kS, cZen}, {-kS,  kS, kTop, cHor},
            { kS, -kS, -kS, cZen}, { kS, -kS, kTop, cHor}, { kS,  kS, -kS, cZen}, { kS,  kS, kTop, cHor},
            {-kS, -kS, -kS, cZen}, {-kS, -kS, kTop, cHor}, { kS, -kS, -kS, cZen}, { kS, -kS, kTop, cHor},
            {-kS,  kS, -kS, cZen}, {-kS,  kS, kTop, cHor}, {-kS, -kS, -kS, cZen}, {-kS, -kS, kTop, cHor},
        };
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        dev->SetTexture(0, NULL);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
        for (unsigned f = 0; f < 5u; ++f) {
            dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, &kFallbackSky[f * 4u], sizeof(Vertex));
        }
    }

    // 2. Render GPU-buffered coastal ocean plane below the museum terrace, fading seamlessly into the horizon.
    if (gHallGpu.oceanVb && gHallGpu.oceanIb && gHallGpu.oceanIdxCount >= 3u) {
        const float kTileMetres = 18.0f;
        const float uOff = std::fmod(gExhibitionVisitor.x / kTileMetres, 1.0f);
        const float vOff = std::fmod(gExhibitionVisitor.y / kTileMetres, 1.0f);
        D3DMATRIX texMat = {};
        texMat._11 = 1.0f; texMat._22 = 1.0f; texMat._33 = 1.0f; texMat._44 = 1.0f;
        texMat._31 = uOff; texMat._32 = vOff;

        IDirect3DTexture9 *waterTex = CeMuseumTextures::Get(CE_MUSEUM_TEX_WATER_BASIN);
        dev->SetTransform(D3DTS_TEXTURE0, &texMat);
        dev->SetTexture(0, waterTex);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
        dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
        dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
        dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, waterTex ? D3DTOP_MODULATE : D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1, waterTex ? D3DTA_TEXTURE : D3DTA_DIFFUSE);
        dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
        dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
        dev->SetStreamSource(0, gHallGpu.oceanVb, 0, sizeof(OceanVertex));
        dev->SetIndices(gHallGpu.oceanIb);
        dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, gHallGpu.oceanVertCount, 0, gHallGpu.oceanIdxCount / 3u);
    }

    // 3. Render GPU-buffered sculpted coastal headlands & sea-stacks resting on the bay horizon.
    if (gHallGpu.headlandVb && gHallGpu.headlandIb && gHallGpu.headlandIdxCount >= 3u) {
        IDirect3DTexture9 *rockTex = CeMuseumTextures::Get(CE_MUSEUM_TEX_LIMESTONE_WALL);
        dev->SetTexture(0, rockTex);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
        dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
        dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, rockTex ? D3DTOP_MODULATE : D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1, rockTex ? D3DTA_TEXTURE : D3DTA_DIFFUSE);
        dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
        dev->SetStreamSource(0, gHallGpu.headlandVb, 0, sizeof(OceanVertex));
        dev->SetIndices(gHallGpu.headlandIb);
        dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, gHallGpu.headlandVertCount, 0, gHallGpu.headlandIdxCount / 3u);
    }

    saved->Apply();
    saved->Release();
    LT.InvalidateAllStates();
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
    // Explicit calibrated 3-point museum architectural light rig inside the local D3DSBT_ALL guard:
    //   Light 0: Warm golden coastal afternoon sun key matching sky dome sun direction
    //   Light 1: Balanced warm-neutral opposing skylight fill (keeps East & West limestone walls cohesive!)
    //   Light 2: Subtle warm limestone floor up-bounce for coffered ceilings & arch soffits
    dev->SetRenderState(D3DRS_AMBIENT, D3DCOLOR_XRGB(54, 52, 50));

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
                0.94f, 0.86f, 0.74f,
                0.90f, 0.84f, 0.70f);
    // Light 1: Balanced opposing daylight fill from WNW hemisphere (gentle coolness, cohesive limestone tone!)
    setDirLight(1,  0.78f, -0.48f,  0.40f,
                0.56f, 0.57f, 0.61f,
                0.30f, 0.34f, 0.40f);
    // Light 2: Subtle warm floor bounce upward onto ceilings & soffits (native -Z is upward travel!)
    setDirLight(2,  0.18f,  0.28f, -0.94f,
                0.26f, 0.24f, 0.21f,
                0.00f, 0.00f, 0.00f);

    for (DWORD i = 3; i < 8; ++i) dev->LightEnable(i, FALSE);
}

extern "C" void PortCeMuseumHallDraw() {
    if (!gPtr || !gPtr->valid || gMirrorPass || !PortCeMuseumVisitActive()) return;
    IDirect3DDevice9 *dev=LT.GetDevice();
    if (!dev) return;
    EnsureHallGpuBuffers(dev);

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
    const bool useLitGpu = (gHallGpu.litVb != NULL && gHallGpu.litIb != NULL);
    if (useLitGpu) {
        dev->SetStreamSource(0, gHallGpu.litVb, 0, sizeof(LitVertex));
        dev->SetIndices(gHallGpu.litIb);
    }
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
        auto toByte = [](float x) -> unsigned {
            int v = static_cast<int>(Clamp01(x) * 255.0f);
            return v < 0 ? 0u : (v > 255 ? 255u : static_cast<unsigned>(v));
        };
        const unsigned shade = toByte(light), keep = toByte(1.0f - quiet);
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
        // Depth-bias flat floor inlays and emissive strips only — never 3D brass trim frames!
        const bool isInlay = (m == kMatInlay || m == kMatNumeral ||
                              m == kMatForsetiGlow || m == kMatMuspellGlow || m == kMatWarmGlow || m == kMatAquaGlow);
        const float bias = isInlay ? -0.00002f : 0.0f, slope = isInlay ? -1.0f : 0.0f;
        dev->SetRenderState(D3DRS_DEPTHBIAS,*reinterpret_cast<const DWORD *>(&bias));
        dev->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS,*reinterpret_cast<const DWORD *>(&slope));
        const unsigned numVerts = gPtr->maxVert[m] - gPtr->minVert[m] + 1u;
        if (useLitGpu) {
            dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, gPtr->minVert[m], numVerts,
                                      gPtr->firstIdx[m], gPtr->triCount[m]);
        } else {
            dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, gPtr->minVert[m], numVerts, gPtr->triCount[m],
                                        gPtr->sortedIndices + gPtr->firstIdx[m], D3DFMT_INDEX32,
                                        gPtr->vertices, sizeof(LitVertex));
        }
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
        if (gHallGpu.shadowVb && gHallGpu.shadowIb) {
            dev->SetStreamSource(0, gHallGpu.shadowVb, 0, sizeof(OverlayVertex));
            dev->SetIndices(gHallGpu.shadowIb);
            dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, gPtr->shadowVertCount, 0,
                                      gPtr->shadowIdxCount / 3u);
        } else {
            dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0u, gPtr->shadowVertCount,
                                        gPtr->shadowIdxCount / 3u, gPtr->shadowIndices,
                                        D3DFMT_INDEX16, gPtr->shadowVerts, sizeof(OverlayVertex));
        }
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
        if (gHallGpu.glassVb && gHallGpu.glassIb) {
            dev->SetStreamSource(0, gHallGpu.glassVb, 0, sizeof(OverlayVertex));
            dev->SetIndices(gHallGpu.glassIb);
            dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, gPtr->glassVertCount, 0,
                                      gPtr->glassIdxCount / 3u);
        } else {
            dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0u, gPtr->glassVertCount,
                                        gPtr->glassIdxCount / 3u, gPtr->glassIndices,
                                        D3DFMT_INDEX16, gPtr->glassVerts, sizeof(OverlayVertex));
        }
    }

    dev->SetStreamSource(0, NULL, 0, 0);
    dev->SetIndices(NULL);
    const float none = 0.0f;
    dev->SetRenderState(D3DRS_DEPTHBIAS,*reinterpret_cast<const DWORD *>(&none));
    dev->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS,*reinterpret_cast<const DWORD *>(&none));
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);

    // Share the SAME single D3DSBT_ALL state guard across Directory, Portal Signage, Cinema, Retail Faction Banners,
    // all 149 Framed Concept Art Canvases, 4 Glass-Domed Battle Dioramas, and the Atrium Archive Pedestal!
    DrawDirectoryButtons(dev, gGrandFloorZ);
    CinemaConsoleGeometry(dev);
    CinemaScreen(dev);
    if (gGrandMode) {
        DrawPortalSignboards(dev);
        DrawFactionBanners(dev, gGrandFloorZ);
        CeMuseumArt::RuntimeDraw(gGrandFloorZ, false);
        CeMuseumDiorama::RuntimeDraw(gGrandFloorZ, false);
#ifdef CE_MUSEUM_HAS_ARCHIVE_PEDESTAL
        CeArchivePedestal::RuntimeDraw(gGrandFloorZ, false);
#endif
    }
    saved->Apply(); saved->Release();
    LT.InvalidateAllStates();

    // Render all 45 resident rotating 3D Goodie exhibits via CMESHRENDERER inside its own state guard.
    if (gGrandMode) {
        CeMuseumTurntable::RuntimeDraw(gGrandFloorZ);
    }
}
#endif
