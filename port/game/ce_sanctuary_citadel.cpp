// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_sanctuary_citadel.cpp: Open-Air Terrain-Backed Sanctuary Museum Citadel,
// shimmering hexagonal geodesic Archive Shield Dome with animated impact ripples,
// sealed north Cinema Pavilion, Forseti & Muspell Memorial Alcoves with official retail
// faction emblem murals, and live In-World Archive Pedestal runtime integration. Uses
// D3DPOOL_MANAGED static GPU vertex/index buffers with CPU fallback and reuses all 13
// procedural GPL-3.0-only architectural textures from CeMuseumTextures.
#ifdef CE_VERSION
#include "common.h"
#include "ltshell.h"
#include "renderinfo.h"
#include "engine.h"
#include "game.h"
#include "player.h"
#include "BattleEngine.h"
#include "Camera.h"
#include "world.h"
#include "unit.h"
#include "d3dx9.h"
#include "ce_asset_catalog.h"
#include "ce_photo.h"
#include "ce_sanctuary_citadel.h"
#include "ce_archive_pedestal.h"
#include "ce_museum_textures.h"
#include "ce_museum_tour.h"
#include "ce_museum_world.h"
#include "../platform/common/portlog.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern bool gMirrorPass;
const char *PortGameDir();
bool PortCeCmdMapOpen() __attribute__((weak));
extern "C" int PortCePlayUnitIsDriven(const void *unit) __attribute__((weak));
extern "C" uint64_t PortCeD3dDeviceGeneration(void) __attribute__((weak));

namespace CeSanctuaryCitadel {
namespace {

static inline uint64_t CurrentCitadelDeviceGen() {
    return PortCeD3dDeviceGeneration ? PortCeD3dDeviceGeneration() : 0u;
}

static_assert(kMuseumEntranceX == CeMuseumWorld::kEntranceX &&
              kMuseumEntranceY == CeMuseumWorld::kEntranceY,
              "Sanctuary pedestal entrance origin must match CeMuseumWorld");

enum Material {
    kMatFlat = 0,
    kMatCourtyard,
    kMatMarbleFloor,
    kMatRunner,
    kMatLimestone,
    kMatSlatePier,
    kMatPierCap,
    kMatPierBase,
    kMatPierLine,
    kMatPlinthMarble,
    kMatPlinthTop,
    kMatBrass,
    kMatCofferCeiling,
    kMatWater,
    kMatForsetiTrim,
    kMatMuspellTrim,
    kMatForgedIron,
    kMatCinemaWall,
    kMatCinemaCeiling,
    kMatCinemaCarpet,
    kMatCinemaSeat,
    kMatScreenGlass,
    kMatWarmGlow,
    kMatForsetiGlow,
    kMatMuspellGlow,
    kMatAquaGlow,
    kMatCount
};

struct MaterialSpec {
    CeMuseumTexId tex;
    float repeat;
    DWORD tint;
    float emissiveR, emissiveG, emissiveB;
    float specR, specG, specB;
    float specPower;
    bool unbakedGlow;
};

static const MaterialSpec kMaterials[kMatCount] = {
    {CE_MUSEUM_TEX_COUNT,            1.0f, 0xffffffff, 0.04f, 0.04f, 0.04f, 0.00f, 0.00f, 0.00f,  1.0f, false}, // flat
    {CE_MUSEUM_TEX_COURTYARD_PAVER,  3.2f, 0xfffffbf2, 0.02f, 0.02f, 0.01f, 0.14f, 0.13f, 0.11f, 14.0f, false}, // overlook sandstone
    {CE_MUSEUM_TEX_GALLERY_FLOOR,    3.0f, 0xfffaf8f3, 0.00f, 0.00f, 0.00f, 0.26f, 0.27f, 0.29f, 24.0f, false}, // rotunda marble
    {CE_MUSEUM_TEX_RUNNER_STONE,     2.6f, 0xffe2e8ee, 0.00f, 0.00f, 0.00f, 0.28f, 0.30f, 0.34f, 26.0f, false}, // basalt runner
    {CE_MUSEUM_TEX_LIMESTONE_WALL,   3.6f, 0xfffcf8f0, 0.00f, 0.00f, 0.00f, 0.09f, 0.09f, 0.08f, 10.0f, false}, // dressed limestone
    {CE_MUSEUM_TEX_SLATE_PIER,       2.4f, 0xffe4ebf2, 0.00f, 0.00f, 0.00f, 0.26f, 0.29f, 0.34f, 22.0f, false}, // fluted slate pier
    {CE_MUSEUM_TEX_LIMESTONE_WALL,   2.2f, 0xfffefaf2, 0.00f, 0.00f, 0.00f, 0.12f, 0.12f, 0.10f, 12.0f, false}, // cornice cap
    {CE_MUSEUM_TEX_SLATE_PIER,       2.2f, 0xff9aa4ae, 0.00f, 0.00f, 0.00f, 0.20f, 0.22f, 0.26f, 18.0f, false}, // base course
    {CE_MUSEUM_TEX_SLATE_PIER,       2.0f, 0xff46505a, 0.00f, 0.00f, 0.00f, 0.12f, 0.14f, 0.16f, 12.0f, false}, // shadow reveal
    {CE_MUSEUM_TEX_PLINTH_MARBLE,    1.8f, 0xfff4f7fa, 0.00f, 0.00f, 0.00f, 0.34f, 0.35f, 0.38f, 26.0f, false}, // plinth marble
    {CE_MUSEUM_TEX_SLATE_PIER,       1.6f, 0xffcfd9e2, 0.00f, 0.00f, 0.00f, 0.38f, 0.42f, 0.46f, 30.0f, false}, // plinth top deck
    {CE_MUSEUM_TEX_BRUSHED_BRASS,    1.4f, 0xfffff2d2, 0.12f, 0.09f, 0.04f, 0.88f, 0.72f, 0.40f, 28.0f, false}, // brushed brass
    {CE_MUSEUM_TEX_COFFERED_CEILING, 3.8f, 0xffedf1f6, 0.03f, 0.03f, 0.04f, 0.00f, 0.00f, 0.00f,  1.0f, false}, // coffered soffit
    {CE_MUSEUM_TEX_WATER_BASIN,      2.8f, 0xffe8f8ff, 0.10f, 0.22f, 0.28f, 0.96f, 0.98f, 1.00f, 48.0f, false}, // reflecting pool
    {CE_MUSEUM_TEX_FORSETI_TRIM,     2.0f, 0xfff2f8ff, 0.05f, 0.10f, 0.16f, 0.48f, 0.68f, 0.90f, 24.0f, false}, // Forseti cobalt trim
    {CE_MUSEUM_TEX_MUSPELL_TRIM,     2.0f, 0xffffede0, 0.10f, 0.04f, 0.01f, 0.56f, 0.36f, 0.20f, 20.0f, false}, // Muspell bronze trim
    {CE_MUSEUM_TEX_FORGED_IRON,      2.6f, 0xffd8d2d0, 0.02f, 0.02f, 0.02f, 0.40f, 0.34f, 0.30f, 18.0f, false}, // forged iron
    {CE_MUSEUM_TEX_CINEMA_ACOUSTIC,  2.6f, 0xffb8c2cc, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f,  1.0f, false}, // cinema wall
    {CE_MUSEUM_TEX_COFFERED_CEILING, 3.6f, 0xffa8b4c0, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f,  1.0f, false}, // cinema ceiling
    {CE_MUSEUM_TEX_RUNNER_STONE,     2.2f, 0xff927e82, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f,  1.0f, false}, // cinema carpet
    {CE_MUSEUM_TEX_CINEMA_ACOUSTIC,  1.2f, 0xffc87274, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f,  1.0f, false}, // theater seating
    {CE_MUSEUM_TEX_COUNT,            1.0f, 0xff283848, 0.14f, 0.20f, 0.26f, 0.32f, 0.38f, 0.46f, 28.0f, true},  // projection screen
    {CE_MUSEUM_TEX_COUNT,            1.0f, 0xfffff2cf, 0.92f, 0.82f, 0.58f, 0.60f, 0.55f, 0.40f, 20.0f, true},  // warm cove/lantern
    {CE_MUSEUM_TEX_COUNT,            1.0f, 0xff80ecff, 0.28f, 0.86f, 1.00f, 0.60f, 0.90f, 1.00f, 24.0f, true},  // Forseti cyan strip
    {CE_MUSEUM_TEX_COUNT,            1.0f, 0xffffa644, 1.00f, 0.50f, 0.12f, 0.90f, 0.55f, 0.20f, 20.0f, true},  // Muspell amber strip
    {CE_MUSEUM_TEX_COUNT,            1.0f, 0xff62e8f6, 0.22f, 0.80f, 0.92f, 0.70f, 0.95f, 1.00f, 32.0f, true},  // aqua pool rim
};

// The Citadel stands under a starfield: its key light is the moon, low in the
// south-east (the direction Living Sandbox lights its terrain from; native -Z is UP).
constexpr float kSunDirX =  0.620f;
constexpr float kSunDirY = -0.500f;
constexpr float kSunDirZ = -0.604f;

constexpr DWORD kBrassCol = 0xffd4a654u;
constexpr DWORD kLightCol = 0xffe6cb88u;
constexpr DWORD kSlateCol = 0xff405563u;
constexpr DWORD kIronCol  = 0xff263641u;

struct LitVertex {
    float x, y, z;
    float nx, ny, nz;
    DWORD colour;
    float u, v;
};

struct OverlayVertex {
    float x, y, z;
    DWORD colour;
};

struct EmblemTexVertex {
    float x, y, z;
    DWORD colour;
    float u, v;
};

enum FactionEmblemId {
    kEmblemForseti = 0,
    kEmblemMuspell = 1,
    kEmblemCount = 2
};

struct FactionEmblemSlot {
    IDirect3DTexture9 *texture;
    unsigned srcW, srcH;
    bool loaded;
};

enum {
    kLitVertCap = 72000,
    kLitIdxCap = 240000,
    kShadowVertCap = 8000,
    kShadowIdxCap = 24000,
    kBeamVertCap = 2048,
    kBeamIdxCap = 6144,
    kMaxAoBoxes = 64,
    kMaxHexCells = 256,
    kMaxHexEdges = 900,
    kCollarSegs = 128,
    kDomeVertCap = 12000,
    kDomeIdxCap = 36000,
    kEmblemMaxTexEdge = 512
};

struct AoBox {
    float minX, minY, maxX, maxY;
    float height;
};

struct HexCell {
    float cx, cy, cz; // unit normal on sphere
    float px, py, pz; // projected dome-local center (relative to kCenterX, kCenterY, floorZ)
    float vx[6], vy[6], vz[6]; // projected dome-local points (scaled by kDomeVisualRadius)
    unsigned count; // 5 or 6
};

struct HexEdge {
    float ax, ay, az;
    float bx, by, bz;
    float nx, ny, nz; // outward normal at midpoint
};

struct CitadelState {
    bool active;
    bool built;
    bool texturesLoaded;
    bool insideCinema;        // the camera is inside the sealed Cinema (drives the outdoor-draw skip)
    bool playerInsideCinema;  // the player's machine is inside it (the camera may still trail outside)
    bool overflow;
    float floorZ;
    float animTime;
    float eyeX, eyeY, eyeZ;
    float aimX, aimY, aimZ;
    bool hasAim;

    RippleBuffer ripples;
    SolidLayout solids;

    AoBox aoBoxes[kMaxAoBoxes];
    unsigned aoBoxCount;

    LitVertex verts[kLitVertCap];
    uint32_t indices[kLitIdxCap];
    uint32_t sortedIndices[kLitIdxCap];
    unsigned char triMat[kLitIdxCap / 3];
    unsigned usedVerts;
    unsigned usedIndices;
    unsigned firstIdx[kMatCount];
    unsigned triCount[kMatCount];
    unsigned minVert[kMatCount];
    unsigned maxVert[kMatCount];

    OverlayVertex shadowVerts[kShadowVertCap];
    uint16_t shadowIndices[kShadowIdxCap];
    unsigned shadowVertCount;
    unsigned shadowIdxCount;

    OverlayVertex beamVerts[kBeamVertCap];
    uint16_t beamIndices[kBeamIdxCap];
    unsigned beamVertCount;
    unsigned beamIdxCount;

    HexCell hexCells[kMaxHexCells];
    unsigned hexCellCount;
    HexEdge hexEdges[kMaxHexEdges];
    unsigned hexEdgeCount;

    // Precomputed 360 deg terrain-conforming ground-contact ribbon samples (filled once at RuntimeInit).
    float collarCos[kCollarSegs];
    float collarSin[kCollarSegs];
    float collarGroundZ[kCollarSegs];
    float collarBury[kCollarSegs];

    OverlayVertex domeVerts[kDomeVertCap];
    uint16_t domeIndices[kDomeIdxCap];

    // Lazily allocated D3DPOOL_MANAGED GPU buffers for static Citadel geometry
    // (animated geodesic shield dome stays dynamic/UP).
    IDirect3DDevice9 *gpuDevice;
    uint64_t gpuDeviceGen;
    bool gpuBuffersAttempted;
    IDirect3DVertexBuffer9 *litVb;
    IDirect3DIndexBuffer9 *litIb;
    IDirect3DVertexBuffer9 *shadowVb;
    IDirect3DIndexBuffer9 *shadowIb;
    IDirect3DVertexBuffer9 *beamVb;
    IDirect3DIndexBuffer9 *beamIb;

    // Runtime-loaded official retail faction emblem murals (zero retail bytes in git).
    IDirect3DDevice9 *emblemDevice;
    uint64_t emblemDeviceGen;
    bool emblemsAttempted;
    FactionEmblemSlot emblems[kEmblemCount];
};

static CitadelState *gCitadel = NULL;

static inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

static inline float SmoothStep(float e0, float e1, float x) {
    if (std::fabs(e1 - e0) < 1e-6f) return x >= e1 ? 1.0f : 0.0f;
    const float t = Clamp01((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}

static inline DWORD FloatToByteClamp(float f) {
    if (f <= 0.0f) return 0u;
    if (f >= 255.0f) return 255u;
    const int iv = static_cast<int>(f + 0.5f);
    return iv <= 0 ? 0u : (iv >= 255 ? 255u : static_cast<DWORD>(iv));
}

static DWORD ModulateRgb(DWORD base, float rMul, float gMul, float bMul) {
    auto ch = [](DWORD v, unsigned sh, float mul) -> DWORD {
        return FloatToByteClamp(float((v >> sh) & 255u) * mul) << sh;
    };
    return (base & 0xff000000u) | ch(base, 16, rMul) | ch(base, 8, gMul) | ch(base, 0, bMul);
}

static DWORD MakeArgb(float a, float r, float g, float b) {
    const DWORD ba = FloatToByteClamp(Clamp01(a) * 255.0f);
    const DWORD br = FloatToByteClamp(Clamp01(r) * 255.0f);
    const DWORD bg = FloatToByteClamp(Clamp01(g) * 255.0f);
    const DWORD bb = FloatToByteClamp(Clamp01(b) * 255.0f);
    return (ba << 24) | (br << 16) | (bg << 8) | bb;
}

static void RegisterAoBox(float x0, float y0, float x1, float y1, float h) {
    if (!gCitadel || gCitadel->aoBoxCount >= kMaxAoBoxes) return;
    gCitadel->aoBoxes[gCitadel->aoBoxCount++] = AoBox{x0, y0, x1, y1, h};
}

// Pools of lantern light baked into the terrace's vertex colours: warm amber
// around every lantern-bearing pier, the dais glow ring and the signal mast,
// cool cyan under the four shield emitter pylons and the Forseti colonnade,
// ember amber along the Muspell colonnade. `radius` is where a pool fades out
// on the floor; walls catch it up to about a storey and a half.
struct LightPool {
    float x, y, radius;
    float r, g, b;
};

static const LightPool kLightPools[] = {
    // Overlook portal pylons and the Cinema portal: warm lantern amber
    { 95.0f, 375.5f, 9.0f, 0.60f, 0.42f, 0.18f},
    {129.0f, 375.5f, 9.0f, 0.60f, 0.42f, 0.18f},
    {108.9f, 408.9f, 6.5f, 0.54f, 0.38f, 0.16f},
    {115.1f, 408.9f, 6.5f, 0.54f, 0.38f, 0.16f},
    { 99.0f, 408.9f, 5.5f, 0.44f, 0.31f, 0.13f},
    {125.0f, 408.9f, 5.5f, 0.44f, 0.31f, 0.13f},
    // Archive dais glow ring and the signal mast lantern
    {kCenterX, kCenterY, 9.5f, 0.50f, 0.37f, 0.17f},
    {kSignalMastX, kSignalMastY, 11.0f, 0.58f, 0.41f, 0.17f},
    // Shield emitter pylons: cool cyan
    {103.5f, 383.5f, 6.5f, 0.08f, 0.30f, 0.42f},
    {120.5f, 383.5f, 6.5f, 0.08f, 0.30f, 0.42f},
    {103.5f, 400.5f, 6.5f, 0.08f, 0.30f, 0.42f},
    {120.5f, 400.5f, 6.5f, 0.08f, 0.30f, 0.42f},
    // West Forseti colonnade (cyan) and East Muspell colonnade (ember)
    { 82.0f, 374.0f, 4.8f, 0.06f, 0.22f, 0.32f}, {142.0f, 374.0f, 4.8f, 0.40f, 0.20f, 0.05f},
    { 82.0f, 382.0f, 4.8f, 0.06f, 0.22f, 0.32f}, {142.0f, 382.0f, 4.8f, 0.40f, 0.20f, 0.05f},
    { 82.0f, 390.0f, 4.8f, 0.06f, 0.22f, 0.32f}, {142.0f, 390.0f, 4.8f, 0.40f, 0.20f, 0.05f},
    { 82.0f, 398.0f, 4.8f, 0.06f, 0.22f, 0.32f}, {142.0f, 398.0f, 4.8f, 0.40f, 0.20f, 0.05f},
    { 82.0f, 406.0f, 4.8f, 0.06f, 0.22f, 0.32f}, {142.0f, 406.0f, 4.8f, 0.40f, 0.20f, 0.05f},
    { 82.0f, 414.0f, 4.8f, 0.06f, 0.22f, 0.32f}, {142.0f, 414.0f, 4.8f, 0.40f, 0.20f, 0.05f},
};

static void AddLightPools(float wx, float wy, float lz, bool isFloor, bool isVertical,
                          float *ioR, float *ioG, float *ioB) {
    if (!isFloor && !isVertical) return;
    // Walls and piers catch a pool up to ~5.5 m; the paving takes all of it.
    const float reach = isFloor ? 1.0f : (lz < 0.0f ? 0.0f : 1.0f - SmoothStep(0.6f, 5.5f, lz));
    if (reach <= 0.0f) return;
    for (unsigned i = 0; i < sizeof(kLightPools) / sizeof(kLightPools[0]); ++i) {
        const LightPool &lp = kLightPools[i];
        const float d = std::hypot(wx - lp.x, wy - lp.y);
        if (d >= lp.radius) continue;
        const float fall = 1.0f - SmoothStep(lp.radius * 0.12f, lp.radius, d);
        const float w = fall * fall * reach;
        *ioR += lp.r * w;
        *ioG += lp.g * w;
        *ioB += lp.b * w;
    }
}

// Deep baked hemisphere ambient occlusion + floor/wall creases + cinema interior shade
// + Forseti/Muspell alcove bounce + cool moonlight with warm lantern pools.
static DWORD BakeVertexColour(float wx, float wy, float wz,
                              float nx, float ny, float nz,
                              Material m, DWORD baseCol) {
    if (!gCitadel || kMaterials[m].unbakedGlow) return baseCol;

    const float lz = gCitadel->floorZ - wz; // height above plateau floor (positive UP)
    const bool isFloor = (nz < -0.65f) && (lz < 0.25f);
    const bool isCeiling = (nz > 0.65f);
    const bool isVertical = (std::fabs(nz) < 0.65f);
    const bool inCinema = (wx >= kCinemaWallMinX - 0.1f && wx <= kCinemaWallMaxX + 0.1f &&
                           wy >= kCinemaWallMinY - 0.1f && wy <= kCinemaWallMaxY + 0.1f &&
                           lz >= -0.1f && lz <= 9.8f);

    float ao = 1.0f;
    float lightR = 0.56f, lightG = 0.64f, lightB = 0.84f; // cool moonlight under the starfield

    if (inCinema) {
        // Moody acoustic theater interior with warm screen proscenium wash
        const float dW = std::fabs(wx - kCinemaWallMinX);
        const float dE = std::fabs(kCinemaWallMaxX - wx);
        const float dS = std::fabs(wy - kCinemaWallMinY);
        const float dN = std::fabs(kCinemaWallMaxY - wy);
        const float dX = dW < dE ? dW : dE;
        const float dY = dS < dN ? dS : dN;
        const float dWall = dX < dY ? dX : dY;
        if (isFloor) {
            ao *= (0.46f + 0.54f * SmoothStep(0.0f, 2.8f, dWall));
            ao *= (0.58f + 0.42f * SmoothStep(0.0f, 3.6f, dX + dY));
        } else if (isVertical) {
            ao *= (0.45f + 0.55f * SmoothStep(0.0f, 2.0f, lz));
            if (lz > 6.8f) ao *= (0.52f + 0.48f * SmoothStep(9.2f, 6.8f, lz));
            if (dWall < 1.2f) ao *= (0.54f + 0.46f * SmoothStep(0.0f, 3.0f, dX > dY ? dX : dY));
        } else if (isCeiling) {
            ao *= (0.48f + 0.52f * SmoothStep(0.0f, 3.2f, dWall));
        }
        const float screenWash = 1.0f - SmoothStep(1.5f, 11.5f, std::hypot(wx - kCenterX, wy - 426.5f));
        lightR = 0.54f + 0.34f * screenWash;
        lightG = 0.58f + 0.32f * screenWash;
        lightB = 0.66f + 0.34f * screenWash;
    } else {
        if (isFloor) {
            // Obstacle floor-crease proximity AO around columns, dais, walls, alcoves, and balustrades
            float obsAo = 1.0f;
            for (unsigned i = 0; i < gCitadel->aoBoxCount; ++i) {
                const AoBox &b = gCitadel->aoBoxes[i];
                const float dx = wx < b.minX ? (b.minX - wx) : (wx > b.maxX ? (wx - b.maxX) : 0.0f);
                const float dy = wy < b.minY ? (b.minY - wy) : (wy > b.maxY ? (wy - b.maxY) : 0.0f);
                const float dist = std::sqrt(dx * dx + dy * dy);
                if (dist < 2.60f) {
                    const float minF = b.height > 4.0f ? 0.46f : 0.58f;
                    const float f = minF + (1.0f - minF) * SmoothStep(0.0f, 2.60f, dist);
                    if (f < obsAo) obsAo = f;
                }
            }
            ao *= obsAo;
            // Moonlit focal lift on the central rotunda medallion
            const float rotundaPool = 1.0f - SmoothStep(3.0f, 14.0f, std::hypot(wx - kCenterX, wy - kCenterY));
            lightR += 0.04f * rotundaPool;
            lightG += 0.05f * rotundaPool;
            lightB += 0.06f * rotundaPool;
        } else if (isVertical) {
            // Floor-crease AO at base of walls, piers, dais, and retaining cliff
            if (lz >= 0.0f) {
                ao *= (0.46f + 0.54f * SmoothStep(0.0f, 2.15f, lz));
                const float hNorm = Clamp01(lz / 9.5f);
                lightR *= 0.90f + 0.15f * hNorm;
                lightG *= 0.92f + 0.13f * hNorm;
                lightB *= 0.94f + 0.12f * hNorm;
            } else {
                // Retaining bastion cliff below floorZ darkens toward lower valley rock
                ao *= (0.52f + 0.48f * SmoothStep(-10.0f, 0.0f, lz));
            }
            // Moon-side facets pick up a silver edge; the far side sinks into deeper blue shade
            const float sunDot = nx * kSunDirX + ny * kSunDirY;
            if (sunDot > 0.0f) {
                lightR += 0.05f * sunDot;
                lightG += 0.07f * sunDot;
                lightB += 0.10f * sunDot;
            } else {
                lightB += 0.04f * (-sunDot);
                ao *= (0.80f + 0.20f * (1.0f + sunDot));
            }
        } else if (isCeiling) {
            ao *= 0.72f;
            lightR *= 0.94f; lightG *= 0.96f; lightB *= 1.02f;
        } else if (nz < -0.65f && lz >= 0.25f) {
            // Horizontal caps, plinth tops, and coping stones catch open sky illumination
            ao *= 1.06f;
        }

        // West Forseti Memorial Alcove cool cyan skylight bounce
        const float dForseti = std::hypot(wx - kForsetiAlcoveX, wy - kForsetiAlcoveY);
        if (dForseti < 11.5f) {
            const float fGlow = 1.0f - SmoothStep(2.0f, 11.5f, dForseti);
            lightR *= (1.0f - 0.16f * fGlow);
            lightG += 0.14f * fGlow;
            lightB += 0.28f * fGlow;
        }

        // East Muspell Memorial Alcove warm molten-amber forge bounce
        const float dMuspell = std::hypot(wx - kMuspellAlcoveX, wy - kMuspellAlcoveY);
        if (dMuspell < 11.5f) {
            const float mGlow = 1.0f - SmoothStep(2.0f, 11.5f, dMuspell);
            lightR += 0.26f * mGlow;
            lightG += 0.06f * mGlow;
            lightB *= (1.0f - 0.22f * mGlow);
        }

        // Lantern and emitter light pools: the terrace's warmth at night.
        AddLightPools(wx, wy, lz, isFloor, isVertical, &lightR, &lightG, &lightB);
    }

    return ModulateRgb(baseCol, Clamp01(ao * lightR), Clamp01(ao * lightG), Clamp01(ao * lightB));
}

static uint32_t AddLitVertex(float wx, float wy, float wz,
                             float nx, float ny, float nz,
                             Material m, DWORD sourceCol) {
    if (!gCitadel) return 0u;
    if (gCitadel->usedVerts >= kLitVertCap) {
        gCitadel->overflow = true;
        return 0u;
    }
    const DWORD tint = kMaterials[m].tint;
    const DWORD baseCol = tint ? tint : sourceCol;
    const DWORD baked = BakeVertexColour(wx, wy, wz, nx, ny, nz, m, baseCol);
    const uint32_t idx = gCitadel->usedVerts++;
    LitVertex &v = gCitadel->verts[idx];
    v.x = wx; v.y = wy; v.z = wz;
    v.nx = nx; v.ny = ny; v.nz = nz;
    v.colour = baked;
    const float ax = std::fabs(nx), ay = std::fabs(ny), az = std::fabs(nz);
    const float scale = 1.0f / kMaterials[m].repeat;
    const float rx = wx - kCenterX, ry = wy - kCenterY;
    if (az >= ax && az >= ay) { v.u = rx * scale; v.v = ry * scale; }
    else if (ax >= ay) { v.u = ry * scale; v.v = wz * scale; }
    else { v.u = rx * scale; v.v = wz * scale; }
    return idx;
}

static void AddLitTriangle(uint32_t i0, uint32_t i1, uint32_t i2, Material m) {
    if (!gCitadel) return;
    if (gCitadel->usedIndices + 3u > kLitIdxCap) {
        gCitadel->overflow = true;
        return;
    }
    const unsigned tri = gCitadel->usedIndices / 3u;
    gCitadel->indices[gCitadel->usedIndices++] = i0;
    gCitadel->indices[gCitadel->usedIndices++] = i1;
    gCitadel->indices[gCitadel->usedIndices++] = i2;
    gCitadel->triMat[tri] = static_cast<unsigned char>(m);
}

static void EmitHorizontalGrid(float x0, float y0, float x1, float y1,
                               float wz, float nz, unsigned nx, unsigned ny,
                               Material mat, DWORD col = 0xffffffff) {
    if (!gCitadel || !(x1 > x0 && y1 > y0) || !nx || !ny) return;
    if (nx > 36u) nx = 36u;
    if (ny > 36u) ny = 36u;
    const uint32_t base = gCitadel->usedVerts;
    const unsigned stride = nx + 1u;
    for (unsigned iy = 0; iy <= ny; ++iy) {
        const float wy = y0 + (y1 - y0) * (float(iy) / float(ny));
        for (unsigned ix = 0; ix <= nx; ++ix) {
            const float wx = x0 + (x1 - x0) * (float(ix) / float(nx));
            AddLitVertex(wx, wy, wz, 0.0f, 0.0f, nz, mat, col);
        }
    }
    if (gCitadel->overflow) return;
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

static void EmitFloorGrid(float x0, float y0, float x1, float y1,
                          float lift, Material mat, float step = 2.0f,
                          DWORD col = 0xffffffff) {
    if (!gCitadel || !(x1 > x0 && y1 > y0)) return;
    unsigned nx = static_cast<unsigned>(std::ceil((x1 - x0) / step));
    unsigned ny = static_cast<unsigned>(std::ceil((y1 - y0) / step));
    if (!nx) nx = 1u;
    if (!ny) ny = 1u;
    EmitHorizontalGrid(x0, y0, x1, y1, gCitadel->floorZ - lift, -1.0f, nx, ny, mat, col);
}

static void EmitVerticalWall(float ax, float ay, float bx, float by,
                             float h0, float h1, float nx, float ny,
                             Material mat, DWORD col = 0xffffffff) {
    if (!gCitadel || !(h1 > h0)) return;
    const float hLen = std::hypot(bx - ax, by - ay);
    if (hLen < 1e-4f) return;
    unsigned segU = (hLen > 1.8f) ? static_cast<unsigned>(std::ceil(hLen / 2.2f)) : 1u;
    if (segU > 16u) segU = 16u;

    float hStops[7];
    unsigned nH = 0;
    hStops[nH++] = h0;
    const float h = h1 - h0;
    if (h > 3.0f) {
        if (h0 < 0.5f && h0 + 0.65f < h1 - 0.8f) hStops[nH++] = h0 + 0.65f;
        if (h0 < 0.5f && h0 + 1.90f < h1 - 1.1f) hStops[nH++] = h0 + 1.90f;
        if (h > 6.0f && h0 + h * 0.54f > hStops[nH - 1] + 0.6f && h0 + h * 0.54f < h1 - 1.6f)
            hStops[nH++] = h0 + h * 0.54f;
        if (h1 - 1.50f > hStops[nH - 1] + 0.5f) hStops[nH++] = h1 - 1.50f;
    } else if (h > 1.0f) {
        hStops[nH++] = h0 + h * 0.45f;
    }
    hStops[nH++] = h1;

    const uint32_t base = gCitadel->usedVerts;
    const unsigned stride = segU + 1u;
    for (unsigned ih = 0; ih < nH; ++ih) {
        const float wz = gCitadel->floorZ - hStops[ih];
        for (unsigned iu = 0; iu <= segU; ++iu) {
            const float t = float(iu) / float(segU);
            AddLitVertex(ax + (bx - ax) * t, ay + (by - ay) * t, wz, nx, ny, 0.0f, mat, col);
        }
    }
    if (gCitadel->overflow) return;
    for (unsigned ih = 0; ih + 1u < nH; ++ih) {
        for (unsigned iu = 0; iu < segU; ++iu) {
            const uint32_t v00 = base + ih * stride + iu;
            const uint32_t v10 = v00 + 1u;
            const uint32_t v01 = v00 + stride;
            const uint32_t v11 = v01 + 1u;
            AddLitTriangle(v00, v10, v11, mat);
            AddLitTriangle(v00, v11, v01, mat);
        }
    }
}

static void EmitBox(float x0, float y0, float h0, float x1, float y1, float h1,
                    Material mat, DWORD col = 0xffffffff, bool includeBottom = false) {
    if (!gCitadel || !(x1 > x0 && y1 > y0 && h1 > h0)) return;
    unsigned nx = (x1 - x0) > 2.6f ? static_cast<unsigned>(std::ceil((x1 - x0) / 2.6f)) : 1u;
    unsigned ny = (y1 - y0) > 2.6f ? static_cast<unsigned>(std::ceil((y1 - y0) / 2.6f)) : 1u;
    if (nx > 12u) nx = 12u;
    if (ny > 12u) ny = 12u;

    EmitHorizontalGrid(x0, y0, x1, y1, gCitadel->floorZ - h1, -1.0f, nx, ny, mat, col);
    if (includeBottom || h0 > 2.2f)
        EmitHorizontalGrid(x0, y0, x1, y1, gCitadel->floorZ - h0, 1.0f, nx, ny, mat, col);

    EmitVerticalWall(x0, y0, x1, y0, h0, h1,  0.0f, -1.0f, mat, col);
    EmitVerticalWall(x1, y0, x1, y1, h0, h1,  1.0f,  0.0f, mat, col);
    EmitVerticalWall(x1, y1, x0, y1, h0, h1,  0.0f,  1.0f, mat, col);
    EmitVerticalWall(x0, y1, x0, y0, h0, h1, -1.0f,  0.0f, mat, col);
}

static void EmitOctagonTier(float cx, float cy, float radius, float h0, float h1,
                            Material mat, DWORD col = 0xffffffff, bool drawTop = true) {
    if (!gCitadel || !(radius > 0.05f && h1 > h0)) return;
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
        const uint32_t centerIdx = AddLitVertex(cx, cy, gCitadel->floorZ - h1, 0.0f, 0.0f, -1.0f, mat, col);
        uint32_t ring[8];
        for (unsigned i = 0; i < sides; ++i)
            ring[i] = AddLitVertex(px[i], py[i], gCitadel->floorZ - h1, 0.0f, 0.0f, -1.0f, mat, col);
        if (!gCitadel->overflow) {
            for (unsigned i = 0; i < sides; ++i)
                AddLitTriangle(centerIdx, ring[i], ring[(i + 1u) % sides], mat);
        }
    }
    for (unsigned i = 0; i < sides; ++i) {
        const unsigned j = (i + 1u) % sides;
        const float mx = 0.5f * (px[i] + px[j]) - cx;
        const float my = 0.5f * (py[i] + py[j]) - cy;
        const float ml = std::hypot(mx, my);
        EmitVerticalWall(px[i], py[i], px[j], py[j], h0, h1,
                         ml > 1e-5f ? mx / ml : 1.0f,
                         ml > 1e-5f ? my / ml : 0.0f, mat, col);
    }
}

static void EmitOctagonRingInlay(float cx, float cy, float rInner, float rOuter,
                                 float lift, Material mat, DWORD col = 0xffffffff) {
    if (!gCitadel || !(rOuter > rInner && rInner > 0.05f)) return;
    const unsigned segs = 16u;
    const float wz = gCitadel->floorZ - lift;
    uint32_t inIdx[16], outIdx[16];
    for (unsigned i = 0; i < segs; ++i) {
        const float a = float(i) * (6.283185307f / float(segs));
        const float ca = std::cos(a), sa = std::sin(a);
        inIdx[i]  = AddLitVertex(cx + ca * rInner, cy + sa * rInner, wz, 0.0f, 0.0f, -1.0f, mat, col);
        outIdx[i] = AddLitVertex(cx + ca * rOuter, cy + sa * rOuter, wz, 0.0f, 0.0f, -1.0f, mat, col);
    }
    if (!gCitadel->overflow) {
        for (unsigned i = 0; i < segs; ++i) {
            const unsigned j = (i + 1u) % segs;
            AddLitTriangle(inIdx[i], outIdx[i], outIdx[j], mat);
            AddLitTriangle(inIdx[i], outIdx[j], inIdx[j], mat);
        }
    }
}

// Alpha-blended directional soft contact-shadow decal grounded beneath plinths, columns, and piers.
static void AddContactShadow(float cx, float cy, float hx, float hy, float spread,
                             float lift = 0.042f, float maxAlpha = 0.70f) {
    if (!gCitadel) return;
    const unsigned segs = 16u;
    if (gCitadel->shadowVertCount + segs * 3u > kShadowVertCap ||
        gCitadel->shadowIdxCount + segs * 12u > kShadowIdxCap) return;

    const float effAlpha = Clamp01(maxAlpha);
    const float effSpread = spread * 1.25f;
    const float effLift = lift < 0.042f ? 0.042f : lift;
    const uint16_t base = static_cast<uint16_t>(gCitadel->shadowVertCount);
    const float wz = gCitadel->floorZ - effLift;

    const DWORD cInner = (FloatToByteClamp(effAlpha * 255.0f) << 24) | 0x00070b12u;
    const DWORD cMid   = (FloatToByteClamp(effAlpha * 0.46f * 255.0f) << 24) | 0x000a1019u;
    const DWORD cOuter = 0x000d131eu;

    // Directional shadow offset away from SE afternoon sun (toward WNW).
    const float shiftX = -kSunDirX * (effSpread * 0.30f);
    const float shiftY = -kSunDirY * (effSpread * 0.30f);

    auto shapePt = [&](float angle, float rx, float ry, float sxOff, float syOff, float *ox, float *oy) {
        const float ca = std::cos(angle), sa = std::sin(angle);
        const float px = (ca < 0.0f ? -1.0f : 1.0f) * std::pow(std::fabs(ca), 0.62f);
        const float py = (sa < 0.0f ? -1.0f : 1.0f) * std::pow(std::fabs(sa), 0.62f);
        *ox = cx + sxOff + px * rx;
        *oy = cy + syOff + py * ry;
    };

    for (unsigned i = 0; i < segs; ++i) {
        const float a = float(i) * (6.283185307f / float(segs));
        float x0, y0, x1, y1, x2, y2;
        shapePt(a, hx * 0.96f, hy * 0.96f, 0.0f, 0.0f, &x0, &y0);
        shapePt(a, hx + effSpread * 0.44f, hy + effSpread * 0.44f, shiftX * 0.55f, shiftY * 0.55f, &x1, &y1);
        shapePt(a, hx + effSpread, hy + effSpread, shiftX, shiftY, &x2, &y2);
        gCitadel->shadowVerts[gCitadel->shadowVertCount++] = {x0, y0, wz, cInner};
        gCitadel->shadowVerts[gCitadel->shadowVertCount++] = {x1, y1, wz, cMid};
        gCitadel->shadowVerts[gCitadel->shadowVertCount++] = {x2, y2, wz, cOuter};
    }
    for (unsigned i = 0; i < segs; ++i) {
        const unsigned j = (i + 1u) % segs;
        const uint16_t a0 = static_cast<uint16_t>(base + i * 3u + 0u);
        const uint16_t a1 = static_cast<uint16_t>(base + i * 3u + 1u);
        const uint16_t a2 = static_cast<uint16_t>(base + i * 3u + 2u);
        const uint16_t b0 = static_cast<uint16_t>(base + j * 3u + 0u);
        const uint16_t b1 = static_cast<uint16_t>(base + j * 3u + 1u);
        const uint16_t b2 = static_cast<uint16_t>(base + j * 3u + 2u);
        gCitadel->shadowIndices[gCitadel->shadowIdxCount++] = a0;
        gCitadel->shadowIndices[gCitadel->shadowIdxCount++] = a1;
        gCitadel->shadowIndices[gCitadel->shadowIdxCount++] = b1;
        gCitadel->shadowIndices[gCitadel->shadowIdxCount++] = a0;
        gCitadel->shadowIndices[gCitadel->shadowIdxCount++] = b1;
        gCitadel->shadowIndices[gCitadel->shadowIdxCount++] = b0;
        gCitadel->shadowIndices[gCitadel->shadowIdxCount++] = a1;
        gCitadel->shadowIndices[gCitadel->shadowIdxCount++] = a2;
        gCitadel->shadowIndices[gCitadel->shadowIdxCount++] = b2;
        gCitadel->shadowIndices[gCitadel->shadowIdxCount++] = a1;
        gCitadel->shadowIndices[gCitadel->shadowIdxCount++] = b2;
        gCitadel->shadowIndices[gCitadel->shadowIdxCount++] = b1;
    }
}

static void AddEmitterBeam(float bx, float by, float bh, float tx, float ty, float th,
                           float rBase, float rTop, DWORD cBase, DWORD cTop) {
    if (!gCitadel) return;
    const unsigned segs = 6u;
    if (gCitadel->beamVertCount + segs * 2u > kBeamVertCap ||
        gCitadel->beamIdxCount + segs * 6u > kBeamIdxCap) return;
    const uint16_t base = static_cast<uint16_t>(gCitadel->beamVertCount);
    const float bz = gCitadel->floorZ - bh;
    const float tz = gCitadel->floorZ - th;
    for (unsigned i = 0; i < segs; ++i) {
        const float a = float(i) * (6.283185307f / float(segs));
        const float ca = std::cos(a), sa = std::sin(a);
        gCitadel->beamVerts[gCitadel->beamVertCount++] = {bx + ca * rBase, by + sa * rBase, bz, cBase};
        gCitadel->beamVerts[gCitadel->beamVertCount++] = {tx + ca * rTop,  ty + sa * rTop,  tz, cTop};
    }
    for (unsigned i = 0; i < segs; ++i) {
        const unsigned j = (i + 1u) % segs;
        const uint16_t a0 = static_cast<uint16_t>(base + i * 2u + 0u);
        const uint16_t a1 = static_cast<uint16_t>(base + i * 2u + 1u);
        const uint16_t b0 = static_cast<uint16_t>(base + j * 2u + 0u);
        const uint16_t b1 = static_cast<uint16_t>(base + j * 2u + 1u);
        gCitadel->beamIndices[gCitadel->beamIdxCount++] = a0;
        gCitadel->beamIndices[gCitadel->beamIdxCount++] = b0;
        gCitadel->beamIndices[gCitadel->beamIdxCount++] = b1;
        gCitadel->beamIndices[gCitadel->beamIdxCount++] = a0;
        gCitadel->beamIndices[gCitadel->beamIdxCount++] = b1;
        gCitadel->beamIndices[gCitadel->beamIdxCount++] = a1;
    }
}

static void EmitDressedColumn(float cx, float cy, float half, float height,
                              Material shaftMat = kMatSlatePier, Material trimMat = kMatBrass,
                              Material glowMat = kMatWarmGlow) {
    AddContactShadow(cx, cy, half + 0.16f, half + 0.16f, 0.90f, 0.044f, 0.68f);
    EmitBox(cx - half - 0.18f, cy - half - 0.18f, 0.02f, cx + half + 0.18f, cy + half + 0.18f, 0.12f, kMatPierLine, kIronCol);
    EmitBox(cx - half - 0.12f, cy - half - 0.12f, 0.12f, cx + half + 0.12f, cy + half + 0.12f, 0.72f, kMatPierBase, kSlateCol);
    EmitBox(cx - half - 0.15f, cy - half - 0.15f, 0.68f, cx + half + 0.15f, cy + half + 0.15f, 0.78f, trimMat, kBrassCol);
    // Fluted shaft with corner pilasters for dramatic light-and-shadow relief
    EmitBox(cx - half, cy - half, 0.78f, cx + half, cy + half, height - 0.56f, shaftMat, kSlateCol);
    EmitBox(cx - half - 0.05f, cy - half * 0.40f, 0.90f, cx + half + 0.05f, cy + half * 0.40f, height - 0.87f, kMatLimestone, 0xfff4efe6);
    EmitBox(cx - half * 0.40f, cy - half - 0.05f, 0.90f, cx + half * 0.40f, cy + half + 0.05f, height - 0.875f, kMatLimestone, 0xfff4efe6);
    // Warm golden uplight sconce reveal near capital (1 cm vertical clearances eliminate coplanar seams)
    EmitBox(cx - half - 0.09f, cy - half - 0.09f, height - 0.86f, cx + half + 0.09f, cy + half + 0.09f, height - 0.73f, glowMat, kLightCol);
    EmitBox(cx - half - 0.13f, cy - half - 0.13f, height - 0.72f, cx + half + 0.13f, cy + half + 0.13f, height - 0.53f, trimMat, kBrassCol);
    EmitBox(cx - half - 0.22f, cy - half - 0.22f, height - 0.52f, cx + half + 0.22f, cy + half + 0.22f, height, kMatPierCap, 0xfff8f4eb);
}

static void EmitRunnerX(float x0, float x1, float cy, float halfW) {
    EmitFloorGrid(x0, cy - halfW, x1, cy + halfW, 0.048f, kMatRunner, 2.5f, 0xffd0d6dc);
    EmitFloorGrid(x0, cy - halfW - 0.12f, x1, cy - halfW, 0.058f, kMatBrass, 3.0f, kBrassCol);
    EmitFloorGrid(x0, cy + halfW, x1, cy + halfW + 0.12f, 0.058f, kMatBrass, 3.0f, kBrassCol);
}

static void EmitRunnerY(float cx, float y0, float y1, float halfW) {
    EmitFloorGrid(cx - halfW, y0, cx + halfW, y1, 0.048f, kMatRunner, 2.5f, 0xffd0d6dc);
    EmitFloorGrid(cx - halfW - 0.12f, y0, cx - halfW, y1, 0.058f, kMatBrass, 3.0f, kBrassCol);
    EmitFloorGrid(cx + halfW, y0, cx + halfW + 0.12f, y1, 0.058f, kMatBrass, 3.0f, kBrassCol);
}

static void EmitBalustradeX(float y, float x0, float x1, float pierStep) {
    EmitBox(x0, y - 0.30f, 0.02f, x1, y + 0.30f, 0.42f, kMatLimestone, 0xffeae4d8);
    EmitBox(x0, y - 0.36f, 0.42f, x1, y + 0.36f, 0.52f, kMatPierCap, 0xfff6f2e8);
    EmitBox(x0, y - 0.12f, 0.98f, x1, y + 0.12f, 1.10f, kMatBrass, kBrassCol, true);
    for (float x = x0 + 1.0f; x < x1 - 0.5f; x += 1.0f) {
        EmitBox(x - 0.08f, y - 0.08f, 0.52f, x + 0.08f, y + 0.08f, 0.98f, kMatPierCap, 0xfff2eee5);
    }
    for (float x = x0; x <= x1 + 0.1f; x += pierStep) {
        AddContactShadow(x, y, 0.36f, 0.40f, 0.65f, 0.042f, 0.54f);
        EmitBox(x - 0.32f, y - 0.38f, 0.02f, x + 0.32f, y + 0.38f, 1.24f, kMatSlatePier, kSlateCol);
        EmitBox(x - 0.36f, y - 0.42f, 1.24f, x + 0.36f, y + 0.42f, 1.36f, kMatPierCap, 0xfff5f1e7);
        EmitBox(x - 0.16f, y - 0.16f, 1.36f, x + 0.16f, y + 0.16f, 1.58f, kMatWarmGlow, kLightCol);
        EmitBox(x - 0.24f, y - 0.24f, 1.58f, x + 0.24f, y + 0.24f, 1.66f, kMatBrass, kBrassCol);
    }
}

static void EmitBalustradeY(float x, float y0, float y1, float pierStep, bool skipStartPier = false) {
    const float yBase0 = skipStartPier ? (y0 + 0.30f) : y0;
    const float yCap0  = skipStartPier ? (y0 + 0.36f) : y0;
    const float yRail0 = skipStartPier ? (y0 + 0.12f) : y0;
    EmitBox(x - 0.30f, yBase0, 0.02f, x + 0.30f, y1, 0.42f, kMatLimestone, 0xffeae4d8);
    EmitBox(x - 0.36f, yCap0,  0.42f, x + 0.36f, y1, 0.52f, kMatPierCap, 0xfff6f2e8);
    EmitBox(x - 0.12f, yRail0, 0.98f, x + 0.12f, y1, 1.10f, kMatBrass, kBrassCol, true);
    for (float y = y0 + 1.0f; y < y1 - 0.5f; y += 1.0f) {
        EmitBox(x - 0.08f, y - 0.08f, 0.52f, x + 0.08f, y + 0.08f, 0.98f, kMatPierCap, 0xfff2eee5);
    }
    const float yPier0 = skipStartPier ? (y0 + pierStep) : y0;
    for (float y = yPier0; y <= y1 + 0.1f; y += pierStep) {
        AddContactShadow(x, y, 0.40f, 0.36f, 0.65f, 0.042f, 0.54f);
        EmitBox(x - 0.38f, y - 0.32f, 0.02f, x + 0.38f, y + 0.32f, 1.24f, kMatSlatePier, kSlateCol);
        EmitBox(x - 0.42f, y - 0.36f, 1.24f, x + 0.42f, y + 0.36f, 1.36f, kMatPierCap, 0xfff5f1e7);
        EmitBox(x - 0.16f, y - 0.16f, 1.36f, x + 0.16f, y + 0.16f, 1.58f, kMatWarmGlow, kLightCol);
        EmitBox(x - 0.24f, y - 0.24f, 1.58f, x + 0.24f, y + 0.24f, 1.66f, kMatBrass, kBrassCol);
    }
}

static void EmitFactionBanner(float cx, float cy, Material trimMat, Material glowMat, bool faceEast) {
    const float s = faceEast ? 1.0f : -1.0f;
    AddContactShadow(cx, cy, 0.55f, 0.55f, 0.85f, 0.044f, 0.64f);
    EmitBox(cx - 0.45f, cy - 0.45f, 0.02f, cx + 0.45f, cy + 0.45f, 0.36f, kMatPierBase, kSlateCol);
    EmitBox(cx - 0.16f, cy - 0.16f, 0.36f, cx + 0.16f, cy + 0.16f, 6.40f, kMatSlatePier, kSlateCol);
    EmitBox(cx - 0.22f, cy - 0.22f, 6.40f, cx + 0.22f, cy + 0.22f, 6.68f, kMatBrass, kBrassCol);
    // Cross-arm & hanging sculpted metallic faction pennant (1 cm below cross-arm soffit)
    EmitBox(cx - 0.14f, cy - 1.15f, 5.95f, cx + 0.14f, cy + 1.15f, 6.18f, kMatBrass, kBrassCol, true);
    const float bx0 = faceEast ? (cx + 0.10f) : (cx - 0.26f);
    const float bx1 = faceEast ? (cx + 0.26f) : (cx - 0.10f);
    EmitBox(bx0, cy - 0.92f, 2.25f, bx1, cy + 0.92f, 5.94f, trimMat, 0xffffffff);
    EmitBox(bx0 + s * 0.02f, cy - 0.18f, 2.55f, bx1 + s * 0.02f, cy + 0.18f, 5.65f, glowMat, kLightCol);
    EmitBox(bx0 + s * 0.02f, cy - 0.96f, 2.15f, bx1 + s * 0.02f, cy + 0.96f, 2.32f, kMatBrass, kBrassCol);
}

static void SortMaterials() {
    if (!gCitadel) return;
    const unsigned triTotal = gCitadel->usedIndices / 3u;
    unsigned counts[kMatCount] = {};
    for (unsigned t = 0; t < triTotal; ++t) {
        const unsigned m = gCitadel->triMat[t];
        if (m < kMatCount) ++counts[m];
    }
    unsigned cursor = 0;
    for (unsigned m = 0; m < kMatCount; ++m) {
        gCitadel->firstIdx[m] = cursor;
        gCitadel->triCount[m] = counts[m];
        gCitadel->minVert[m] = gCitadel->usedVerts;
        gCitadel->maxVert[m] = 0;
        cursor += counts[m] * 3u;
    }
    unsigned writeAt[kMatCount];
    for (unsigned m = 0; m < kMatCount; ++m) writeAt[m] = gCitadel->firstIdx[m];
    for (unsigned t = 0; t < triTotal; ++t) {
        const unsigned m = gCitadel->triMat[t];
        if (m >= kMatCount) continue;
        unsigned &dst = writeAt[m];
        for (unsigned k = 0; k < 3u; ++k) {
            const uint32_t v = gCitadel->indices[t * 3u + k];
            gCitadel->sortedIndices[dst++] = v;
            if (v < gCitadel->minVert[m]) gCitadel->minVert[m] = v;
            if (v > gCitadel->maxVert[m]) gCitadel->maxVert[m] = v;
        }
    }
}

// Builds the complete Open-Air Terrain-Backed Sanctuary Museum Citadel geometry:
//   1. Elevated Terrace Platform + Fortified Retaining Bastion Skirt
//   2. South-East Panoramic Overlook Terrace, Launch Pad Compass & Twin Reflecting Pools
//   3. Central Grand Archive Rotunda with Stepped Octagonal Marble-and-Brass Dais
//   4. Open-Air Monumental Colonnade + 4 Shield Dome Emitter Pylons
//   5. West Forseti Memorial Alcove & East Muspell Memorial Alcove + Twin Faction Banners
//   6. North Sealed Cinema Pavilion (acoustic walls, coffered ceiling, tiered seating, screen)
static void BuildCitadelArchitecture() {
    if (!gCitadel) return;
    gCitadel->usedVerts = 0;
    gCitadel->usedIndices = 0;
    gCitadel->shadowVertCount = 0;
    gCitadel->shadowIdxCount = 0;
    gCitadel->beamVertCount = 0;
    gCitadel->beamIdxCount = 0;
    gCitadel->aoBoxCount = 0;
    gCitadel->overflow = false;

    // Register major architectural footprints first so floor vertices bake deep contact AO!
    RegisterAoBox(kCenterX - 2.15f, kCenterY - 2.15f, kCenterX + 2.15f, kCenterY + 2.15f, 1.2f); // Rotunda dais
    RegisterAoBox(kCinemaWallMinX, kCinemaWallMinY, kCinemaWallMaxX, kCinemaWallMaxY, 9.6f);   // Cinema pavilion
    RegisterAoBox(81.5f, 383.5f, 84.5f, 400.5f, 7.8f);  // West Forseti apse
    RegisterAoBox(139.5f, 383.5f, 142.5f, 400.5f, 7.8f); // East Muspell apse
    RegisterAoBox(87.0f, 390.5f, 90.0f, 393.5f, 1.2f);   // Forseti honor plinth
    RegisterAoBox(134.0f, 390.5f, 137.0f, 393.5f, 1.2f); // Muspell honor plinth
    RegisterAoBox(kSignalMastX - 1.3f, kSignalMastY - 1.3f, kSignalMastX + 1.3f, kSignalMastY + 1.3f, 9.2f); // Signal mast
    RegisterAoBox(96.0f, 365.0f, 102.0f, 377.0f, 0.8f);  // West reflecting pool
    RegisterAoBox(122.0f, 365.0f, 128.0f, 377.0f, 0.8f); // East reflecting pool
    const float colY[6] = {374.0f, 382.0f, 390.0f, 398.0f, 406.0f, 414.0f};
    for (unsigned i = 0; i < 6u; ++i) {
        RegisterAoBox(81.3f, colY[i] - 0.7f, 82.7f, colY[i] + 0.7f, 9.2f);
        RegisterAoBox(141.3f, colY[i] - 0.7f, 142.7f, colY[i] + 0.7f, 9.2f);
    }
    const float emX[2] = {103.5f, 120.5f};
    const float emY[2] = {383.5f, 400.5f};
    for (unsigned ix = 0; ix < 2u; ++ix)
        for (unsigned iy = 0; iy < 2u; ++iy)
            RegisterAoBox(emX[ix] - 0.8f, emY[iy] - 0.8f, emX[ix] + 0.8f, emY[iy] + 0.8f, 7.8f);

    // -------------------------------------------------------------------------
    // 1. Main Plateau Terrace Floors & Cliff Retaining Bastion Apron
    // -------------------------------------------------------------------------
    // South Panoramic Overlook Terrace (warm sandstone pavers, open vista toward battlefield!)
    EmitFloorGrid(74.0f, 354.0f, 150.0f, 376.0f, 0.025f, kMatCourtyard, 2.2f);
    // Central Grand Archive Rotunda Plaza (honed marble slabs)
    EmitFloorGrid(82.0f, 376.0f, 142.0f, 410.0f, 0.025f, kMatMarbleFloor, 2.0f);
    // West & East Side Colonnade Promenades + North Rear Apron around Cinema
    EmitFloorGrid(74.0f, 376.0f,  82.0f, 430.0f, 0.025f, kMatCourtyard, 2.2f);
    EmitFloorGrid(142.0f, 376.0f, 150.0f, 430.0f, 0.025f, kMatCourtyard, 2.2f);
    EmitFloorGrid(82.0f, 410.0f,  98.0f, 430.0f, 0.025f, kMatCourtyard, 2.2f);
    EmitFloorGrid(126.0f, 410.0f, 142.0f, 430.0f, 0.025f, kMatCourtyard, 2.2f);
    EmitFloorGrid(98.0f, 428.0f, 126.0f, 430.0f, 0.025f, kMatCourtyard, 2.2f);

    // Fortified Retaining Cliff Bastion Skirt anchoring terrace into plateau mountain
    // (top cleanly at 0.00f — 2.5 cm below 0.025f terrace pavers, zero Z-fighting — and
    // split cleanly around the 17 m SE Grand Access Ramp gateway X in [119.5..136.5]).
    EmitBox(72.5f, 352.5f, -10.0f, 119.5f, 354.2f, 0.00f, kMatPierBase, kSlateCol);
    EmitBox(136.5f, 352.5f, -10.0f, 151.5f, 354.2f, 0.00f, kMatPierBase, kSlateCol);
    EmitBox(72.5f, 429.8f, -10.0f, 151.5f, 431.5f, 0.00f, kMatPierBase, kSlateCol);
    EmitBox(72.5f, 354.2f, -10.0f,  74.2f, 429.8f, 0.00f, kMatPierBase, kSlateCol);
    EmitBox(149.8f, 354.2f, -10.0f, 151.5f, 429.8f, 0.00f, kMatPierBase, kSlateCol);
    // Non-overlapping outer stone coping curb strictly outside [74..150]x[354..430]
    // (flush with 0.025f terrace pavers and 2.5 cm cleanly above 0.00f plateau terrain).
    EmitBox(72.5f, 352.5f, 0.00f, 119.5f, 354.0f, 0.025f, kMatPierBase, kSlateCol);
    EmitBox(136.5f, 352.5f, 0.00f, 151.5f, 354.0f, 0.025f, kMatPierBase, kSlateCol);
    EmitBox(72.5f, 430.0f, 0.00f, 151.5f, 431.5f, 0.025f, kMatPierBase, kSlateCol);
    EmitBox(72.5f, 354.0f, 0.00f,  74.0f, 430.0f, 0.025f, kMatPierBase, kSlateCol);
    EmitBox(150.0f, 354.0f, 0.00f, 151.5f, 430.0f, 0.025f, kMatPierBase, kSlateCol);
    // Brass bastion belt-course trim around the outer terrace cornice (clean mitered corners)
    EmitBox(72.3f, 352.3f, -0.45f, 119.5f, 352.7f, -0.05f, kMatBrass, kBrassCol);
    EmitBox(136.5f, 352.3f, -0.45f, 151.7f, 352.7f, -0.05f, kMatBrass, kBrassCol);
    EmitBox(151.3f, 352.7f, -0.45f, 151.7f, 431.7f, -0.05f, kMatBrass, kBrassCol);
    EmitBox(72.3f, 352.7f, -0.45f,  72.7f, 431.7f, -0.05f, kMatBrass, kBrassCol);
    EmitBox(72.7f, 431.3f, -0.45f, 151.3f, 431.7f, -0.05f, kMatBrass, kBrassCol);

    // Ceremonial Basalt-and-Brass Wayfinding Runners (trimmed cleanly at Rotunda ring,
    // South overlook intersection, Cinema doorway runner, and SE ramp approach — zero overlap)
    EmitRunnerY(kCenterX, 355.0f, 384.64f, 1.45f); // South Overlook -> Launch Pad -> Rotunda
    EmitRunnerY(kCenterX, 399.36f, 409.90f, 1.45f); // Rotunda -> North Cinema Archway
    EmitRunnerX(89.5f, 104.64f, kCenterY, 1.20f);  // Rotunda -> West Forseti Alcove
    EmitRunnerX(119.36f, 134.5f, kCenterY, 1.20f); // Rotunda -> East Muspell Alcove
    EmitRunnerX(76.5f, 110.43f, 360.0f, 1.05f);    // South Panoramic Overlook Promenade (West)
    EmitRunnerX(113.57f, 147.5f, 360.0f, 1.05f);   // South Panoramic Overlook Promenade (East)
    EmitRunnerY(128.0f, 354.0f, 358.83f, 2.20f);   // SE Grand Access Ramp approach threshold

    // Ceremonial Flush Launch Pad Compass Ring at P0 Spawn (112, 366) — cleanly proud of runners!
    EmitOctagonRingInlay(kCenterX, 366.0f, 3.60f, 4.40f, 0.064f, kMatRunner, 0xffc8d0d8);
    EmitOctagonRingInlay(kCenterX, 366.0f, 4.40f, 4.62f, 0.070f, kMatBrass, kBrassCol);
    EmitOctagonRingInlay(kCenterX, 366.0f, 3.42f, 3.60f, 0.070f, kMatBrass, kBrassCol);

    // -------------------------------------------------------------------------
    // 2. South-East Panoramic Overlook Balustrades & Twin Reflecting Pools
    // -------------------------------------------------------------------------
    // Split South balustrade cleanly around the 17 m SE Grand Access Ramp (X in [119.5..136.5]).
    EmitBalustradeX(354.5f,  75.0f, 119.5f, 44.5f / 6.0f); // West & Central South overlook parapet
    EmitBalustradeX(354.5f, 136.5f, 149.0f, 6.25f);        // East South overlook parapet
    EmitBalustradeY(149.0f, 354.5f, 428.0f, 8.0f, true);   // Low East overlook parapet (no duplicate SE pier)
    EmitBalustradeY( 75.0f, 354.5f, 428.0f, 8.0f, true);   // Low West overlook parapet (no duplicate SW pier)

    // Twin calm sunlit reflecting basins framing the axial approach to the Rotunda
    for (int side = -1; side <= 1; side += 2) {
        const float px0 = side < 0 ? 96.0f : 122.0f;
        const float px1 = side < 0 ? 102.0f : 128.0f;
        const float py0 = 365.0f, py1 = 377.0f;
        const float pcx = 0.5f * (px0 + px1), pcy = 0.5f * (py0 + py1);
        AddContactShadow(pcx, pcy, 3.0f, 6.0f, 0.95f, 0.042f, 0.52f);
        EmitBox(px0, py0, 0.02f, px1, py0 + 0.45f, 0.42f, kMatLimestone, 0xffe8e4dc);
        EmitBox(px0, py1 - 0.45f, 0.02f, px1, py1, 0.42f, kMatLimestone, 0xffe8e4dc);
        EmitBox(px0, py0 + 0.45f, 0.02f, px0 + 0.45f, py1 - 0.45f, 0.42f, kMatLimestone, 0xffe8e4dc);
        EmitBox(px1 - 0.45f, py0 + 0.45f, 0.02f, px1, py1 - 0.45f, 0.42f, kMatLimestone, 0xffe8e4dc);
        EmitBox(px0 - 0.05f, py0 - 0.05f, 0.42f, px1 + 0.05f, py0 + 0.48f, 0.48f, kMatPierCap);
        EmitBox(px0 - 0.05f, py1 - 0.48f, 0.42f, px1 + 0.05f, py1 + 0.05f, 0.48f, kMatPierCap);
        EmitBox(px0 - 0.05f, py0 + 0.48f, 0.42f, px0 + 0.48f, py1 - 0.48f, 0.48f, kMatPierCap);
        EmitBox(px1 - 0.48f, py0 + 0.48f, 0.42f, px1 + 0.05f, py1 - 0.48f, 0.48f, kMatPierCap);
        // Luminous turquoise inner pool rim & sunlit water plane (1 cm inset from curb walls)
        EmitBox(px0 + 0.46f, py0 + 0.46f, 0.28f, px1 - 0.46f, py0 + 0.56f, 0.34f, kMatAquaGlow, kLightCol);
        EmitBox(px0 + 0.46f, py1 - 0.56f, 0.28f, px1 - 0.46f, py1 - 0.46f, 0.34f, kMatAquaGlow, kLightCol);
        EmitFloorGrid(px0 + 0.46f, py0 + 0.46f, px1 - 0.46f, py1 - 0.46f, 0.31f, kMatWater, 1.8f, 0xff4088a8);
        // Sculpted stone-and-brass water obelisk in each pool
        EmitOctagonTier(pcx, pcy, 0.72f, 0.28f, 0.60f, kMatPierBase, kSlateCol, true);
        EmitOctagonTier(pcx, pcy, 0.44f, 0.60f, 1.00f, kMatBrass, kBrassCol, true);
    }

    // Forseti signal mast on the south-east bastion corner: one tall, off-axis
    // silhouette (25.9 m against 9-10 m colonnades) that reads as a frontline
    // fortress from the valley below. A stepped slate-and-cobalt shaft, a long
    // signal yard reaching in over the terrace with two hanging lamps, a short
    // counter-yard, a warm lantern crow's nest and a cyan beacon tip.
    {
        const float mx = kSignalMastX, my = kSignalMastY;
        const float bh = kSignalMastBaseHalf;
        AddContactShadow(mx, my, bh + 0.10f, bh + 0.10f, 1.40f, 0.044f, 0.70f);
        EmitBox(mx - bh - 0.08f, my - bh - 0.08f, 0.02f, mx + bh + 0.08f, my + bh + 0.08f, 0.14f, kMatPierLine, kIronCol);
        EmitBox(mx - bh, my - bh, 0.14f, mx + bh, my + bh, 0.90f, kMatPierBase, kSlateCol);
        EmitBox(mx - bh - 0.06f, my - bh - 0.06f, 0.90f, mx + bh + 0.06f, my + bh + 0.06f, 1.02f, kMatBrass, kBrassCol);
        // Lower slate shaft with limestone pilaster strips
        EmitBox(mx - 0.78f, my - 0.78f, 1.02f, mx + 0.78f, my + 0.78f, 8.00f, kMatSlatePier, kSlateCol);
        EmitBox(mx - 0.83f, my - 0.30f, 1.14f, mx + 0.83f, my + 0.30f, 7.88f, kMatLimestone, 0xfff4efe6);
        EmitBox(mx - 0.30f, my - 0.83f, 1.14f, mx + 0.30f, my + 0.83f, 7.87f, kMatLimestone, 0xfff4efe6);
        EmitBox(mx - 0.90f, my - 0.90f, 8.00f, mx + 0.90f, my + 0.90f, 8.25f, kMatBrass, kBrassCol);
        EmitBox(mx - 0.82f, my - 0.82f, 8.26f, mx + 0.82f, my + 0.82f, 8.46f, kMatForsetiGlow, 0xff80ecff);
        // Cobalt mid shaft
        EmitBox(mx - 0.52f, my - 0.52f, 8.47f, mx + 0.52f, my + 0.52f, 16.00f, kMatForsetiTrim, 0xfff2f8ff);
        // Long signal yard reaching north-west over the terrace, two hanging lamps
        EmitBox(mx - 4.10f, my - 0.14f, 15.20f, mx + 0.52f, my + 0.14f, 15.48f, kMatBrass, kBrassCol);
        EmitBox(mx - 3.86f, my - 0.20f, 14.52f, mx - 3.46f, my + 0.20f, 15.19f, kMatForsetiGlow, 0xff80ecff);
        EmitBox(mx - 2.36f, my - 0.20f, 14.72f, mx - 1.96f, my + 0.20f, 15.19f, kMatForsetiGlow, 0xff80ecff);
        EmitBox(mx - 0.62f, my - 0.62f, 16.00f, mx + 0.62f, my + 0.62f, 16.20f, kMatBrass, kBrassCol);
        // Forged-iron upper shaft and a short counter-yard on the valley side
        EmitBox(mx - 0.30f, my - 0.30f, 16.21f, mx + 0.30f, my + 0.30f, 22.00f, kMatForgedIron, kIronCol);
        EmitBox(mx - 0.12f, my - 1.90f, 19.00f, mx + 0.12f, my + 0.30f, 19.22f, kMatBrass, kBrassCol);
        EmitBox(mx - 0.18f, my - 1.86f, 18.52f, mx + 0.18f, my - 1.50f, 18.99f, kMatWarmGlow, kLightCol);
        // Lantern crow's nest, spire and beacon tip
        EmitBox(mx - 0.95f, my - 0.95f, 22.00f, mx + 0.95f, my + 0.95f, 22.18f, kMatBrass, kBrassCol);
        EmitBox(mx - 0.42f, my - 0.42f, 22.19f, mx + 0.42f, my + 0.42f, 23.30f, kMatWarmGlow, kLightCol);
        EmitBox(mx - 0.56f, my - 0.56f, 23.31f, mx + 0.56f, my + 0.56f, 23.50f, kMatBrass, kBrassCol);
        EmitBox(mx - 0.10f, my - 0.10f, 23.51f, mx + 0.10f, my + 0.10f, 25.60f, kMatForgedIron, kIronCol);
        EmitBox(mx - 0.16f, my - 0.16f, 25.61f, mx + 0.16f, my + 0.16f, kSignalMastHeight, kMatForsetiGlow, 0xff80ecff);
        // Soft warm halo rising from the lantern
        AddEmitterBeam(mx, my, 22.30f, mx, my, 27.60f, 0.46f, 1.30f, 0x5cffd08au, 0x00ffb050u);
    }

    // -------------------------------------------------------------------------
    // 3. Central Grand Archive Rotunda & Stepped Octagonal Archive Dais
    // -------------------------------------------------------------------------
    EmitOctagonRingInlay(kCenterX, kCenterY, 5.20f, 7.10f, 0.044f, kMatRunner, 0xffd0d6dc);
    EmitOctagonRingInlay(kCenterX, kCenterY, 7.10f, 7.36f, 0.058f, kMatBrass, kBrassCol);
    EmitOctagonRingInlay(kCenterX, kCenterY, 4.95f, 5.20f, 0.058f, kMatBrass, kBrassCol);
    EmitOctagonTier(kCenterX, kCenterY, 4.95f, 0.02f, 0.055f, kMatPlinthMarble, 0xffeef2f5, true);
    // Cardinal brushed-brass compass rays inlaid around the dais (non-overlapping at center)
    EmitFloorGrid(kCenterX - 6.8f, kCenterY - 0.24f, kCenterX - 2.06f, kCenterY + 0.24f, 0.064f, kMatBrass, 2.4f, kBrassCol);
    EmitFloorGrid(kCenterX + 2.06f, kCenterY - 0.24f, kCenterX + 6.8f, kCenterY + 0.24f, 0.064f, kMatBrass, 2.4f, kBrassCol);
    EmitFloorGrid(kCenterX - 0.24f, kCenterY - 6.8f, kCenterX + 0.24f, kCenterY - 2.06f, 0.064f, kMatBrass, 2.4f, kBrassCol);
    EmitFloorGrid(kCenterX - 0.24f, kCenterY + 2.06f, kCenterX + 0.24f, kCenterY + 6.8f, 0.064f, kMatBrass, 2.4f, kBrassCol);

    // Grand Stepped Octagonal-Waisted Marble & Brushed-Brass Rotunda Centerpiece Dais
    // Cleanly encloses CeArchivePedestal's inner box while keeping its south control desk (y=389.65)
    // and top brass turntable collar (topZ = floorZ - 1.05f = -35.05f) completely flush & unclipped!
    AddContactShadow(kCenterX, kCenterY, 2.08f, 2.08f, 1.55f, 0.066f, 0.72f);
    EmitBox(kCenterX - 2.06f, kCenterY - 2.06f, 0.056f, kCenterX + 2.06f, kCenterY + 2.06f, 0.23f, kMatPierBase, kSlateCol);
    EmitOctagonTier(kCenterX, kCenterY, 2.04f, 0.23f, 0.60f, kMatPlinthMarble, 0xfff2f5f8, true);
    EmitOctagonTier(kCenterX, kCenterY, 2.07f, 0.52f, 0.60f, kMatBrass, kBrassCol, false);
    EmitBox(kCenterX - 1.86f, kCenterY - 1.86f, 0.60f, kCenterX + 1.86f, kCenterY + 1.86f, 0.98f, kMatPlinthMarble, 0xffeef2f6);
    EmitBox(kCenterX - 1.90f, kCenterY - 1.90f, 0.91f, kCenterX + 1.90f, kCenterY + 1.90f, 0.975f, kMatWarmGlow, kLightCol);
    EmitBox(kCenterX - 1.94f, kCenterY - 1.94f, 0.98f, kCenterX + 1.94f, kCenterY + 1.94f, 1.045f, kMatBrass, kBrassCol);
    EmitBox(kCenterX - 1.84f, kCenterY - 1.84f, 1.00f, kCenterX + 1.84f, kCenterY + 1.84f, kPedestalHeight, kMatPlinthTop, kSlateCol);

    // 4 Monumental Rotunda Shield-Dome Emitter Pylons framing the Archive Pedestal
    for (unsigned ix = 0; ix < 2u; ++ix) {
        for (unsigned iy = 0; iy < 2u; ++iy) {
            const float px = emX[ix], py = emY[iy];
            EmitDressedColumn(px, py, 0.50f, 7.60f, kMatSlatePier, kMatBrass, kMatForsetiGlow);
            EmitOctagonTier(px, py, 0.68f, 7.60f, 8.05f, kMatBrass, kBrassCol, true);
            EmitOctagonTier(px, py, 0.42f, 8.05f, 8.65f, kMatForsetiGlow, 0xff80ecff, true);
            // Translucent cyan energy conduit beam rising from each pylon crown toward the Shield Dome
            const float dirX = px - kCenterX;
            const float dirY = py - kCenterY;
            const float tx = kCenterX + dirX * 2.2f;
            const float ty = kCenterY + dirY * 2.2f;
            const float th = std::sqrt(kDomeVisualRadius * kDomeVisualRadius -
                                       (tx - kCenterX) * (tx - kCenterX) -
                                       (ty - kCenterY) * (ty - kCenterY));
            AddEmitterBeam(px, py, 8.55f, tx, ty, th, 0.24f, 0.72f, 0x6280ecffu, 0x0648c0ffu);
        }
    }

    // -------------------------------------------------------------------------
    // 4. Open-Air Monumental Flank Colonnades (West & East framing piers + pergola)
    // -------------------------------------------------------------------------
    for (unsigned i = 0; i < 6u; ++i) {
        EmitDressedColumn(82.0f,  colY[i], 0.48f, 9.19f, kMatSlatePier, kMatBrass, kMatForsetiGlow);
        EmitDressedColumn(142.0f, colY[i], 0.48f, 9.19f, kMatForgedIron, kMatBrass, kMatMuspellGlow);
    }
    // Open-air stone & brushed-brass entablature architraves crowning the West & East colonnades
    EmitBox(81.1f,  373.0f, 9.20f,  82.9f, 415.0f, 10.15f, kMatLimestone, 0xfff6f1e7, true);
    EmitBox(80.95f, 372.9f, 9.48f,  83.05f, 415.1f, 9.72f, kMatBrass, kBrassCol);
    EmitBox(141.1f, 373.0f, 9.20f, 142.9f, 415.0f, 10.15f, kMatLimestone, 0xfff6f1e7, true);
    EmitBox(140.95f, 372.9f, 9.48f, 143.05f, 415.1f, 9.72f, kMatBrass, kBrassCol);
    // Twin grand overlook portal pylons opening wide onto the battlefield vista
    EmitDressedColumn(95.0f,  375.5f, 0.56f, 8.60f, kMatSlatePier, kMatBrass, kMatWarmGlow);
    EmitDressedColumn(129.0f, 375.5f, 0.56f, 8.60f, kMatSlatePier, kMatBrass, kMatWarmGlow);

    // -------------------------------------------------------------------------
    // 5. West Forseti Memorial Alcove & East Muspell Memorial Alcove + Banners
    // -------------------------------------------------------------------------
    // West Forseti Commonwealth Memorial Alcove (X = 82.2..94.0, Y = 384..400, open facing East)
    EmitBox(82.2f, 383.6f, 0.02f, 83.8f, 400.4f, 8.39f, kMatLimestone, 0xfff4f7fa);
    EmitBox(83.8f, 383.6f, 0.02f, 87.2f, 384.8f, 8.39f, kMatLimestone, 0xfff4f7fa);
    EmitBox(83.8f, 399.2f, 0.02f, 87.2f, 400.4f, 8.39f, kMatLimestone, 0xfff4f7fa);
    EmitBox(82.0f, 383.4f, 8.40f, 87.5f, 400.6f, 9.15f, kMatCofferCeiling, 0xffeef4fa, true);
    EmitBox(83.8f, 385.0f, 1.20f, 84.15f, 399.0f, 7.60f, kMatForsetiTrim, 0xffe8f4ff);
    EmitBox(84.15f, 385.6f, 7.35f, 84.35f, 398.4f, 7.55f, kMatForsetiGlow, 0xff80ecff);
    EmitBox(84.15f, 385.6f, 1.25f, 84.35f, 398.4f, 1.45f, kMatForsetiGlow, 0xff80ecff);
    // Sculpted brushed-brass & charcoal archival mural frame for the official Forseti emblem
    EmitBox(84.15f, 389.90f, 2.00f, 84.38f, 394.10f, 6.80f, kMatBrass, kBrassCol);
    EmitBox(84.38f, 390.06f, 2.16f, 84.42f, 393.94f, 6.64f, kMatPierLine, kIronCol);
    // Forseti Ceremonial Honor Plinth
    AddContactShadow(kForsetiAlcoveX, kForsetiAlcoveY, 1.45f, 1.45f, 1.05f, 0.044f, 0.64f);
    EmitBox(kForsetiAlcoveX - 1.40f, kForsetiAlcoveY - 1.40f, 0.02f, kForsetiAlcoveX + 1.40f, kForsetiAlcoveY + 1.40f, 0.24f, kMatPierBase, kSlateCol);
    EmitBox(kForsetiAlcoveX - 1.20f, kForsetiAlcoveY - 1.20f, 0.24f, kForsetiAlcoveX + 1.20f, kForsetiAlcoveY + 1.20f, 0.96f, kMatPlinthMarble, 0xfff4f8fc);
    EmitBox(kForsetiAlcoveX - 1.26f, kForsetiAlcoveY - 1.26f, 0.92f, kForsetiAlcoveX + 1.26f, kForsetiAlcoveY + 1.26f, 1.04f, kMatBrass, kBrassCol);
    EmitBox(kForsetiAlcoveX - 1.12f, kForsetiAlcoveY - 1.12f, 1.04f, kForsetiAlcoveX + 1.12f, kForsetiAlcoveY + 1.12f, 1.10f, kMatPlinthTop, kSlateCol);
    // Twin Forseti Cobalt Banner Standards flanking the West overlook & alcove
    EmitFactionBanner(88.0f, 378.0f, kMatForsetiTrim, kMatForsetiGlow, true);
    EmitFactionBanner(88.0f, 405.5f, kMatForsetiTrim, kMatForsetiGlow, true);

    // East Muspell Dominion Memorial Alcove (X = 130.0..141.8, Y = 384..400, open facing West)
    EmitBox(140.2f, 383.6f, 0.02f, 141.8f, 400.4f, 8.39f, kMatForgedIron, 0xffd0cac8);
    EmitBox(136.8f, 383.6f, 0.02f, 140.2f, 384.8f, 8.39f, kMatForgedIron, 0xffd0cac8);
    EmitBox(136.8f, 399.2f, 0.02f, 140.2f, 400.4f, 8.39f, kMatForgedIron, 0xffd0cac8);
    EmitBox(136.5f, 383.4f, 8.40f, 142.0f, 400.6f, 9.15f, kMatCofferCeiling, 0xffd8d2d0, true);
    EmitBox(139.85f, 385.0f, 1.20f, 140.2f, 399.0f, 7.60f, kMatMuspellTrim, 0xffffe8d8);
    EmitBox(139.65f, 385.6f, 7.35f, 139.85f, 398.4f, 7.55f, kMatMuspellGlow, 0xffffa644);
    EmitBox(139.65f, 385.6f, 1.25f, 139.85f, 398.4f, 1.45f, kMatMuspellGlow, 0xffffa644);
    // Sculpted brushed-brass & forged-iron archival mural frame for the official Muspell emblem
    EmitBox(139.62f, 389.90f, 2.00f, 139.85f, 394.10f, 6.80f, kMatBrass, kBrassCol);
    EmitBox(139.58f, 390.06f, 2.16f, 139.62f, 393.94f, 6.64f, kMatPierLine, kIronCol);
    // Muspell Ceremonial Basalt-and-Bronze Honor Plinth
    AddContactShadow(kMuspellAlcoveX, kMuspellAlcoveY, 1.45f, 1.45f, 1.05f, 0.044f, 0.64f);
    EmitBox(kMuspellAlcoveX - 1.40f, kMuspellAlcoveY - 1.40f, 0.02f, kMuspellAlcoveX + 1.40f, kMuspellAlcoveY + 1.40f, 0.24f, kMatPierLine, kIronCol);
    EmitBox(kMuspellAlcoveX - 1.20f, kMuspellAlcoveY - 1.20f, 0.24f, kMuspellAlcoveX + 1.20f, kMuspellAlcoveY + 1.20f, 0.96f, kMatForgedIron, 0xffd0c8c6);
    EmitBox(kMuspellAlcoveX - 1.26f, kMuspellAlcoveY - 1.26f, 0.92f, kMuspellAlcoveX + 1.26f, kMuspellAlcoveY + 1.26f, 1.04f, kMatMuspellTrim, kBrassCol);
    EmitBox(kMuspellAlcoveX - 1.12f, kMuspellAlcoveY - 1.12f, 1.04f, kMuspellAlcoveX + 1.12f, kMuspellAlcoveY + 1.12f, 1.10f, kMatPlinthTop, kSlateCol);
    // Twin Muspell Crimson-Bronze Banner Standards flanking the East overlook & alcove
    EmitFactionBanner(136.0f, 378.0f, kMatMuspellTrim, kMatMuspellGlow, false);
    EmitFactionBanner(136.0f, 405.5f, kMatMuspellTrim, kMatMuspellGlow, false);

    // -------------------------------------------------------------------------
    // 6. North Sealed Cinema Pavilion (X in [98..126], Y in [410..428], H = 9.6 m)
    //    Doorway: X in [109.5..114.5], Y = 410.0, clear height 5.2 m
    // -------------------------------------------------------------------------
    // Cinema interior dark carpet floor + overlapping doorway threshold (zero floor seam at Y=410)
    EmitFloorGrid(kCinemaWallMinX + 0.3f, kCinemaWallMinY - 0.20f,
                  kCinemaWallMaxX - 0.3f, kCinemaWallMaxY - 0.3f, 0.032f, kMatCinemaCarpet, 2.0f);
    EmitFloorGrid(109.8f, 409.90f, 114.2f, 425.6f, 0.048f, kMatRunner, 2.0f);
    EmitFloorGrid(109.68f, 409.90f, 109.80f, 425.6f, 0.056f, kMatWarmGlow, 2.5f, kLightCol);
    EmitFloorGrid(114.20f, 409.90f, 114.32f, 425.6f, 0.056f, kMatWarmGlow, 2.5f, kLightCol);

    // Outer Limestone Architectural Shell + Inner Dark Acoustic Theater Lining
    // South wall split around grand central doorway [109.5..114.5]
    EmitBox(98.0f,  410.0f, 0.02f, 109.5f, 410.55f, 9.60f, kMatLimestone, 0xfff6f2e8);
    EmitBox(114.5f, 410.0f, 0.02f, 126.0f, 410.55f, 9.60f, kMatLimestone, 0xfff6f2e8);
    EmitBox(109.5f, 410.0f, 5.24f, 114.5f, 410.85f, 9.60f, kMatLimestone, 0xfff6f2e8, true);
    // South interior acoustic backing
    EmitBox(98.45f, 410.55f, 0.02f, 109.5f, 410.90f, 9.05f, kMatCinemaWall);
    EmitBox(114.5f, 410.55f, 0.02f, 125.55f, 410.90f, 9.05f, kMatCinemaWall);
    // West, East, and North exterior + interior acoustic walls
    EmitBox(98.0f,  410.55f, 0.02f,  98.45f, 428.0f, 9.60f, kMatLimestone, 0xfff6f2e8);
    EmitBox(98.45f, 410.90f, 0.02f,  98.85f, 427.55f, 9.05f, kMatCinemaWall);
    EmitBox(125.55f, 410.55f, 0.02f, 126.0f, 428.0f, 9.60f, kMatLimestone, 0xfff6f2e8);
    EmitBox(125.15f, 410.90f, 0.02f, 125.55f, 427.55f, 9.05f, kMatCinemaWall);
    EmitBox(98.45f, 427.55f, 0.02f, 125.55f, 428.0f, 9.60f, kMatLimestone, 0xfff6f2e8);
    EmitBox(98.85f, 427.15f, 0.02f, 125.15f, 427.55f, 9.05f, kMatCinemaWall);

    // Coffered Roof & Sealed Theater Ceiling (opaque above and below -> zero exterior light leak!)
    EmitHorizontalGrid(98.4f, 410.4f, 125.6f, 427.6f, gCitadel->floorZ - 9.00f, 1.0f, 10u, 8u, kMatCinemaCeiling);
    EmitBox(97.4f, 409.4f, 9.10f, 126.6f, 428.6f, 9.95f, kMatPierCap, 0xfffbf7ee, true);
    EmitBox(99.5f, 409.6f, 9.95f, 124.5f, 428.4f, 10.65f, kMatLimestone, 0xfff5f1e7);
    EmitBox(97.2f, 409.2f, 9.25f, 126.8f, 409.6f, 9.52f, kMatBrass, kBrassCol);
    // Warm recessed ceiling cove lighting strips inside theater
    EmitBox(99.2f, 411.5f, 8.78f, 99.5f, 426.5f, 8.95f, kMatWarmGlow, kLightCol);
    EmitBox(124.5f, 411.5f, 8.78f, 124.8f, 426.5f, 8.95f, kMatWarmGlow, kLightCol);
    // Transverse ceiling coffer beams (1 cm clearance from ceiling & embedded brass underside trim)
    for (float by = 414.0f; by <= 424.5f; by += 3.5f) {
        EmitBox(98.9f, by - 0.28f, 8.55f, 125.1f, by + 0.28f, 8.99f, kMatSlatePier, kSlateCol, true);
        EmitBox(99.2f, by - 0.10f, 8.48f, 124.8f, by + 0.10f, 8.56f, kMatBrass, kBrassCol, true);
    }

    // Monumental Cinema Portal Entry Framing (facing South toward Rotunda; 1 cm below 9.10f roof soffit)
    EmitDressedColumn(108.9f, 409.7f, 0.44f, 9.09f, kMatSlatePier, kMatBrass, kMatWarmGlow);
    EmitDressedColumn(115.1f, 409.7f, 0.44f, 9.09f, kMatSlatePier, kMatBrass, kMatWarmGlow);
    EmitBox(108.3f, 409.2f, 5.20f, 115.7f, 410.4f, 5.78f, kMatBrass, kBrassCol, true);
    EmitBox(109.2f, 409.15f, 5.36f, 114.8f, 409.30f, 5.56f, kMatWarmGlow, kLightCol);
    // Exterior facade flanking pilasters
    EmitDressedColumn(99.0f,  409.7f, 0.40f, 9.09f, kMatSlatePier, kMatBrass, kMatWarmGlow);
    EmitDressedColumn(125.0f, 409.7f, 0.40f, 9.09f, kMatSlatePier, kMatBrass, kMatWarmGlow);

    // Interior Stepped Seating Tiers (West [100.2..108.6] & East [115.4..123.8], wide 6.8 m center aisle)
    for (unsigned row = 0; row < 3u; ++row) {
        const float y0 = 413.2f + float(row) * 2.8f;
        const float y1 = y0 + 2.2f;
        const float deckH = 0.25f + float(2u - row) * 0.34f; // tiered rising toward back (South)
        for (int side = -1; side <= 1; side += 2) {
            const float sx0 = side < 0 ? 100.2f : 115.4f;
            const float sx1 = side < 0 ? 108.6f : 123.8f;
            EmitBox(sx0, y0, 0.02f, sx1, y1, deckH, kMatPierBase, kSlateCol);
            // Bench seat cushion + backrest + brass armrest endcaps
            EmitBox(sx0 + 0.3f, y0 + 0.65f, deckH, sx1 - 0.3f, y0 + 1.40f, deckH + 0.46f, kMatCinemaSeat);
            EmitBox(sx0 + 0.3f, y0 + 0.32f, deckH + 0.35f, sx1 - 0.3f, y0 + 0.65f, deckH + 1.15f, kMatCinemaSeat);
            EmitBox(sx0 + 0.18f, y0 + 0.30f, deckH, sx0 + 0.32f, y0 + 1.45f, deckH + 0.72f, kMatBrass, kBrassCol);
            EmitBox(sx1 - 0.32f, y0 + 0.30f, deckH, sx1 - 0.18f, y0 + 1.45f, deckH + 0.72f, kMatBrass, kBrassCol);
        }
    }

    // Illuminated Widescreen Projection Screen Frame on North Wall (X in [102.8..121.2], Y = 426.9)
    EmitBox(102.2f, 426.45f, 1.35f, 121.8f, 427.15f, 8.15f, kMatSlatePier, kSlateCol);
    EmitBox(102.5f, 426.35f, 1.55f, 121.5f, 426.55f, 7.95f, kMatBrass, kBrassCol);
    EmitBox(103.0f, 426.26f, 1.90f, 121.0f, 426.42f, 7.60f, kMatScreenGlass, 0xff283848);
    // Warm proscenium stage deck beneath screen (nosing overhangs ends by 2 cm -> zero coplanar face)
    EmitBox(100.5f, 424.6f, 0.02f, 123.5f, 427.15f, 0.55f, kMatPierBase, kSlateCol);
    EmitBox(100.48f, 424.5f, 0.48f, 123.52f, 424.75f, 0.58f, kMatBrass, kBrassCol);
    // Overhead Cinema Projector Housing above doorway (keeps aisle 100% unobstructed)
    EmitBox(110.8f, 411.1f, 6.20f, 113.2f, 412.6f, 7.10f, kMatSlatePier, kSlateCol, true);
    EmitBox(111.5f, 412.55f, 6.42f, 112.5f, 412.80f, 6.88f, kMatWarmGlow, kLightCol);

    SortMaterials();
}

// -----------------------------------------------------------------------------
// Precompute Dual Hexagonal Geodesic Dome (Goldberg Polyhedron) covering upper hemisphere + skirt
// -----------------------------------------------------------------------------
struct Vec3 { float x, y, z; };

static Vec3 VNorm(Vec3 a) {
    const float l = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
    return l > 1e-6f ? Vec3{a.x / l, a.y / l, a.z / l} : Vec3{0.0f, 0.0f, -1.0f};
}

static Vec3 VCross(Vec3 a, Vec3 b) {
    return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

static float VDot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static void BuildGeodesicHexDome() {
    if (!gCitadel) return;
    gCitadel->hexCellCount = 0;
    gCitadel->hexEdgeCount = 0;

    // Regular icosahedron oriented with a vertex straight at the zenith (z = -1 in native Z-down!).
    const float phi = 1.61803398875f;
    const float rawIco[12][3] = {
        {-1,  phi, 0}, { 1,  phi, 0}, {-1, -phi, 0}, { 1, -phi, 0},
        { 0, -1,  phi}, { 0,  1,  phi}, { 0, -1, -phi}, { 0,  1, -phi},
        { phi, 0, -1}, { phi, 0,  1}, {-phi, 0, -1}, {-phi, 0,  1},
    };
    const unsigned icoTris[20][3] = {
        {0,11,5}, {0,5,1}, {0,1,7}, {0,7,10}, {0,10,11},
        {1,5,9}, {5,11,4}, {11,10,2}, {10,7,6}, {7,1,8},
        {3,9,4}, {3,4,2}, {3,2,6}, {3,6,8}, {3,8,9},
        {4,9,5}, {2,4,11}, {6,2,10}, {8,6,7}, {9,8,1},
    };

    // Subdivide each icosahedron face with frequency N = 3 -> 92 sphere vertices, 180 triangles.
    // Its dual has 92 crisp hexagonal/pentagonal cells (~54 cells in the upper hemisphere + skirt!).
    constexpr unsigned kFreq = 3u;
    Vec3 icoV[12];
    for (unsigned i = 0; i < 12u; ++i)
        icoV[i] = VNorm(Vec3{rawIco[i][0], rawIco[i][1], rawIco[i][2]});

    // Deduplicated geodesic vertices and triangle list.
    Vec3 gVerts[256];
    unsigned gVertCount = 0;
    unsigned gTris[256][3];
    unsigned gTriCount = 0;

    auto findOrAddVert = [&](Vec3 p) -> unsigned {
        p = VNorm(p);
        for (unsigned i = 0; i < gVertCount; ++i) {
            const float dx = gVerts[i].x - p.x, dy = gVerts[i].y - p.y, dz = gVerts[i].z - p.z;
            if (dx * dx + dy * dy + dz * dz < 1e-5f) return i;
        }
        if (gVertCount >= 256u) return 0u;
        gVerts[gVertCount] = p;
        return gVertCount++;
    };

    for (unsigned fi = 0; fi < 20u; ++fi) {
        const Vec3 a = icoV[icoTris[fi][0]];
        const Vec3 b = icoV[icoTris[fi][1]];
        const Vec3 c = icoV[icoTris[fi][2]];
        unsigned grid[kFreq + 1u][kFreq + 1u];
        for (unsigned i = 0; i <= kFreq; ++i) {
            for (unsigned j = 0; j <= kFreq - i; ++j) {
                const float wa = float(kFreq - i - j) / float(kFreq);
                const float wb = float(i) / float(kFreq);
                const float wc = float(j) / float(kFreq);
                grid[i][j] = findOrAddVert(Vec3{
                    wa * a.x + wb * b.x + wc * c.x,
                    wa * a.y + wb * b.y + wc * c.y,
                    wa * a.z + wb * b.z + wc * c.z
                });
            }
        }
        for (unsigned i = 0; i < kFreq; ++i) {
            for (unsigned j = 0; j < kFreq - i; ++j) {
                if (gTriCount < 256u) {
                    gTris[gTriCount][0] = grid[i][j];
                    gTris[gTriCount][1] = grid[i + 1u][j];
                    gTris[gTriCount][2] = grid[i][j + 1u];
                    ++gTriCount;
                }
                if (i + j + 1u < kFreq && gTriCount < 256u) {
                    gTris[gTriCount][0] = grid[i + 1u][j];
                    gTris[gTriCount][1] = grid[i + 1u][j + 1u];
                    gTris[gTriCount][2] = grid[i][j + 1u];
                    ++gTriCount;
                }
            }
        }
    }

    // Triangle centroids (normalized to unit sphere) become the dual hex/pentagon corners!
    Vec3 triCen[256];
    for (unsigned t = 0; t < gTriCount; ++t) {
        const Vec3 p0 = gVerts[gTris[t][0]];
        const Vec3 p1 = gVerts[gTris[t][1]];
        const Vec3 p2 = gVerts[gTris[t][2]];
        triCen[t] = VNorm(Vec3{p0.x + p1.x + p2.x, p0.y + p1.y + p2.y, p0.z + p1.z + p2.z});
    }

    // Tangent-continuous (C^1) projection onto kDomeVisualRadius upper hemisphere + cylindrical skirt
    // extending underground down to floorZ + kDomeVisualSkirtMaxZ (native Z-down).
    auto projectDomePt = [](Vec3 n) -> Vec3 {
        Vec3 p = {};
        ProjectDomeVisualPoint(n.x, n.y, n.z, &p.x, &p.y, &p.z);
        return p;
    };

    // Build dual hexagonal/pentagonal cells across the upper hemisphere and deep sub-terrain skirt.
    for (unsigned vi = 0; vi < gVertCount && gCitadel->hexCellCount < kMaxHexCells; ++vi) {
        const Vec3 n = gVerts[vi];
        // Native Z points DOWN -> upper hemisphere has n.z <= 0; extend skirt down to n.z <= 0.46
        // so unbroken geodesic hex cells penetrate >= 6 m underground around all 360 deg.
        if (n.z > kDomeVisualSkirtNzMax) continue;

        Vec3 ringPts[8];
        unsigned rCount = 0;
        for (unsigned t = 0; t < gTriCount && rCount < 8u; ++t) {
            if (gTris[t][0] == vi || gTris[t][1] == vi || gTris[t][2] == vi) {
                ringPts[rCount++] = triCen[t];
            }
        }
        if (rCount < 5u || rCount > 6u) continue;

        // Build tangent frame around n to sort adjacent centroids counter-clockwise.
        Vec3 up = std::fabs(n.z) < 0.9f ? Vec3{0.0f, 0.0f, -1.0f} : Vec3{1.0f, 0.0f, 0.0f};
        Vec3 tU = VNorm(VCross(up, n));
        Vec3 tV = VCross(n, tU);
        float angles[6];
        for (unsigned k = 0; k < rCount; ++k) {
            const Vec3 d = {ringPts[k].x - n.x, ringPts[k].y - n.y, ringPts[k].z - n.z};
            angles[k] = std::atan2(VDot(d, tV), VDot(d, tU));
        }
        // Insertion sort by angle.
        for (unsigned a = 1; a < rCount; ++a) {
            const float ka = angles[a];
            const Vec3 kp = ringPts[a];
            unsigned b = a;
            while (b > 0 && angles[b - 1] > ka) {
                angles[b] = angles[b - 1];
                ringPts[b] = ringPts[b - 1];
                --b;
            }
            angles[b] = ka;
            ringPts[b] = kp;
        }

        HexCell &cell = gCitadel->hexCells[gCitadel->hexCellCount++];
        const Vec3 centerWp = projectDomePt(n);
        cell.cx = n.x;
        cell.cy = n.y;
        cell.cz = n.z;
        cell.px = centerWp.x;
        cell.py = centerWp.y;
        cell.pz = centerWp.z;
        cell.count = rCount;
        for (unsigned k = 0; k < rCount; ++k) {
            const Vec3 wp = projectDomePt(ringPts[k]);
            cell.vx[k] = wp.x;
            cell.vy[k] = wp.y;
            cell.vz[k] = wp.z;
        }

        // Collect deduplicated hexagonal rib edges.
        for (unsigned k = 0; k < rCount && gCitadel->hexEdgeCount < kMaxHexEdges; ++k) {
            const unsigned kn = (k + 1u) % rCount;
            const float ax = cell.vx[k], ay = cell.vy[k], az = cell.vz[k];
            const float bx = cell.vx[kn], by = cell.vy[kn], bz = cell.vz[kn];
            const float elen2 = (bx - ax) * (bx - ax) + (by - ay) * (by - ay) + (bz - az) * (bz - az);
            if (elen2 < 1e-4f) continue;
            bool dup = false;
            for (unsigned ei = 0; ei < gCitadel->hexEdgeCount; ++ei) {
                const HexEdge &e = gCitadel->hexEdges[ei];
                const float d1 = (e.ax - ax) * (e.ax - ax) + (e.ay - ay) * (e.ay - ay) + (e.az - az) * (e.az - az) +
                                 (e.bx - bx) * (e.bx - bx) + (e.by - by) * (e.by - by) + (e.bz - bz) * (e.bz - bz);
                const float d2 = (e.ax - bx) * (e.ax - bx) + (e.ay - by) * (e.ay - by) + (e.az - bz) * (e.az - bz) +
                                 (e.bx - ax) * (e.bx - ax) + (e.by - ay) * (e.by - ay) + (e.bz - az) * (e.bz - az);
                if (d1 < 0.05f || d2 < 0.05f) { dup = true; break; }
            }
            if (!dup) {
                const float mx = 0.5f * (ax + bx);
                const float my = 0.5f * (ay + by);
                const float mz = 0.5f * (az + bz);
                const Vec3 midN = (mz > 0.0f)
                    ? VNorm(Vec3{mx, my, 0.0f})
                    : VNorm(Vec3{mx, my, mz});
                gCitadel->hexEdges[gCitadel->hexEdgeCount++] = HexEdge{
                    ax, ay, az, bx, by, bz, midN.x, midN.y, midN.z
                };
            }
        }
    }
}

// Precomputes 128-segment (~2.76 m chord) terrain-conforming ground elevation and slope-adaptive
// bury depth along the visual dome perimeter once at RuntimeInit (zero per-frame HeightAt queries).
static void BuildDomeGroundCollar() {
    if (!gCitadel) return;
    const float cz = gCitadel->floorZ;
    const float step = 6.283185307f / static_cast<float>(kCollarSegs);
    for (unsigned i = 0; i < kCollarSegs; ++i) {
        const float a = static_cast<float>(i) * step;
        gCitadel->collarCos[i] = std::cos(a);
        gCitadel->collarSin[i] = std::sin(a);
        gCitadel->collarGroundZ[i] = DomePerimeterGroundZ(a, cz);
    }
    for (unsigned i = 0; i < kCollarSegs; ++i) {
        const float gz = gCitadel->collarGroundZ[i];
        const float gzPrev = gCitadel->collarGroundZ[(i + kCollarSegs - 1u) % kCollarSegs];
        const float gzNext = gCitadel->collarGroundZ[(i + 1u) % kCollarSegs];
        const float dPrev = std::fabs(gz - gzPrev);
        const float dNext = std::fabs(gz - gzNext);
        const float slopeDelta = dPrev > dNext ? dPrev : dNext;
        gCitadel->collarBury[i] = 1.50f + slopeDelta;
    }
}

static void SetupCitadelLightRig(IDirect3DDevice9 *dev) {
    // Night rig for the starfield sky: a cool moon key, a deep blue sky fill and a
    // faint warm lantern bounce, over a low ambient. The lantern pools themselves
    // are baked into the vertex colours (kLightPools), so they stay warm under it.
    dev->SetRenderState(D3DRS_AMBIENT, D3DCOLOR_XRGB(20, 25, 38));

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

    // Light 0: Cool moon key from the south-east
    setDirLight(0, -kSunDirX, -kSunDirY, -kSunDirZ,
                0.66f, 0.76f, 0.98f,
                0.60f, 0.70f, 0.92f);
    // Light 1: Deep blue night-sky fill from the opposite quarter
    setDirLight(1,  0.76f, -0.48f,  0.42f,
                0.13f, 0.19f, 0.33f,
                0.08f, 0.12f, 0.22f);
    // Light 2: Faint warm lantern up-bounce onto soffits & ceiling coffers
    setDirLight(2,  0.18f,  0.26f, -0.94f,
                0.22f, 0.16f, 0.09f,
                0.00f, 0.00f, 0.00f);

    for (DWORD i = 3; i < 8; ++i) dev->LightEnable(i, FALSE);
}

static void RestoreEngineRenderState(IDirect3DStateBlock9 *saved) {
    if (saved) {
        saved->Apply();
        saved->Release();
    }
    LT.InvalidateAllStates();
    RENDERINFO.mWorldChanged = true;
    RENDERINFO.mViewChanged = true;
    RENDERINFO.mProjectionChanged = true;
    RENDERINFO.mFogStartChanged = true;
    RENDERINFO.mFogEndChanged = true;
    RENDERINFO.mFogDensityChanged = true;
    RENDERINFO.mFVFChanged = true;
    RENDERINFO.Apply();
}

static void ReleaseGpuBuffers() {
    if (!gCitadel) return;
    if (gCitadel->litVb) { gCitadel->litVb->Release(); gCitadel->litVb = NULL; }
    if (gCitadel->litIb) { gCitadel->litIb->Release(); gCitadel->litIb = NULL; }
    if (gCitadel->shadowVb) { gCitadel->shadowVb->Release(); gCitadel->shadowVb = NULL; }
    if (gCitadel->shadowIb) { gCitadel->shadowIb->Release(); gCitadel->shadowIb = NULL; }
    if (gCitadel->beamVb) { gCitadel->beamVb->Release(); gCitadel->beamVb = NULL; }
    if (gCitadel->beamIb) { gCitadel->beamIb->Release(); gCitadel->beamIb = NULL; }
    gCitadel->gpuDevice = NULL;
    gCitadel->gpuDeviceGen = 0u;
    gCitadel->gpuBuffersAttempted = false;
}

static bool CreateManagedStaticBuffers(IDirect3DDevice9 *dev,
                                       const void *vertSrc, unsigned vertBytes, DWORD fvf,
                                       const void *idxSrc, unsigned idxBytes, D3DFORMAT idxFmt,
                                       IDirect3DVertexBuffer9 **outVb,
                                       IDirect3DIndexBuffer9 **outIb) {
    if (!dev || !vertSrc || !vertBytes || !idxSrc || !idxBytes || !outVb || !outIb)
        return false;
    IDirect3DVertexBuffer9 *vb = NULL;
    IDirect3DIndexBuffer9 *ib = NULL;
    if (FAILED(dev->CreateVertexBuffer(vertBytes, D3DUSAGE_WRITEONLY, fvf, D3DPOOL_MANAGED, &vb, NULL)) || !vb) {
        if (vb) vb->Release();
        return false;
    }
    void *vbDst = NULL;
    if (FAILED(vb->Lock(0, vertBytes, &vbDst, 0)) || !vbDst) {
        vb->Release();
        return false;
    }
    std::memcpy(vbDst, vertSrc, vertBytes);
    vb->Unlock();

    if (FAILED(dev->CreateIndexBuffer(idxBytes, D3DUSAGE_WRITEONLY, idxFmt, D3DPOOL_MANAGED, &ib, NULL)) || !ib) {
        if (ib) ib->Release();
        vb->Release();
        return false;
    }
    void *ibDst = NULL;
    if (FAILED(ib->Lock(0, idxBytes, &ibDst, 0)) || !ibDst) {
        ib->Release();
        vb->Release();
        return false;
    }
    std::memcpy(ibDst, idxSrc, idxBytes);
    ib->Unlock();

    *outVb = vb;
    *outIb = ib;
    return true;
}

static void EnsureGpuBuffers(IDirect3DDevice9 *dev) {
    if (!gCitadel || !dev) return;
    const uint64_t gen = CurrentCitadelDeviceGen();
    if (gCitadel->gpuDevice && (gCitadel->gpuDevice != dev || gCitadel->gpuDeviceGen != gen)) {
        ReleaseGpuBuffers();
    }
    if (gCitadel->gpuBuffersAttempted) return;
    gCitadel->gpuDevice = dev;
    gCitadel->gpuDeviceGen = gen;
    gCitadel->gpuBuffersAttempted = true;

    const bool litOk = (gCitadel->usedVerts > 0u && gCitadel->usedIndices >= 3u) &&
        CreateManagedStaticBuffers(
            dev,
            gCitadel->verts, gCitadel->usedVerts * static_cast<unsigned>(sizeof(LitVertex)),
            D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1,
            gCitadel->sortedIndices, gCitadel->usedIndices * static_cast<unsigned>(sizeof(uint32_t)),
            D3DFMT_INDEX32,
            &gCitadel->litVb, &gCitadel->litIb);

    const bool shadowOk = (gCitadel->shadowVertCount > 0u && gCitadel->shadowIdxCount >= 3u) &&
        CreateManagedStaticBuffers(
            dev,
            gCitadel->shadowVerts, gCitadel->shadowVertCount * static_cast<unsigned>(sizeof(OverlayVertex)),
            D3DFVF_XYZ | D3DFVF_DIFFUSE,
            gCitadel->shadowIndices, gCitadel->shadowIdxCount * static_cast<unsigned>(sizeof(uint16_t)),
            D3DFMT_INDEX16,
            &gCitadel->shadowVb, &gCitadel->shadowIb);

    const bool beamOk = (gCitadel->beamVertCount > 0u && gCitadel->beamIdxCount >= 3u) &&
        CreateManagedStaticBuffers(
            dev,
            gCitadel->beamVerts, gCitadel->beamVertCount * static_cast<unsigned>(sizeof(OverlayVertex)),
            D3DFVF_XYZ | D3DFVF_DIFFUSE,
            gCitadel->beamIndices, gCitadel->beamIdxCount * static_cast<unsigned>(sizeof(uint16_t)),
            D3DFMT_INDEX16,
            &gCitadel->beamVb, &gCitadel->beamIb);

    PortSessionLog(PORT_SESSION_GAME, false,
                   "sanctuary citadel: managed GPU buffers lit=%d shadow=%d beam=%d",
                   litOk ? 1 : 0, shadowOk ? 1 : 0, beamOk ? 1 : 0);
}

struct FactionEmblemUploadCtx {
    IDirect3DDevice9 *device;
    FactionEmblemSlot *slot;
};

static int UploadFactionEmblemCallback(const unsigned char *dds, size_t bytes,
                                       const CeAssetTextureHeader *h, void *context) {
    FactionEmblemUploadCtx *ctx = static_cast<FactionEmblemUploadCtx *>(context);
    if (!ctx || !ctx->device || !ctx->slot || !dds || !bytes || !h || !h->width || !h->height)
        return 0;
    unsigned upW = h->width;
    unsigned upH = h->height;
    const unsigned longest = upW > upH ? upW : upH;
    if (longest > kEmblemMaxTexEdge) {
        upW = static_cast<unsigned>((static_cast<uint64_t>(upW) * kEmblemMaxTexEdge) / longest);
        upH = static_cast<unsigned>((static_cast<uint64_t>(upH) * kEmblemMaxTexEdge) / longest);
        if (!upW) upW = 1u;
        if (!upH) upH = 1u;
    }
    IDirect3DTexture9 *tex = NULL;
    const HRESULT hr = D3DXCreateTextureFromFileInMemoryEx(
        ctx->device, dds, static_cast<UINT>(bytes),
        static_cast<UINT>(upW), static_cast<UINT>(upH),
        1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
        D3DX_FILTER_TRIANGLE, D3DX_FILTER_NONE, 0, NULL, NULL, &tex);
    if (FAILED(hr) || !tex) {
        if (tex) tex->Release();
        return 0;
    }
    ctx->slot->texture = tex;
    ctx->slot->srcW = h->width;
    ctx->slot->srcH = h->height;
    ctx->slot->loaded = true;
    return 1;
}

static void ReleaseFactionEmblemTextures() {
    if (!gCitadel) return;
    for (unsigned i = 0; i < kEmblemCount; ++i) {
        if (gCitadel->emblems[i].texture) {
            gCitadel->emblems[i].texture->Release();
            gCitadel->emblems[i].texture = NULL;
        }
        gCitadel->emblems[i].srcW = 0u;
        gCitadel->emblems[i].srcH = 0u;
        gCitadel->emblems[i].loaded = false;
    }
    gCitadel->emblemDevice = NULL;
    gCitadel->emblemDeviceGen = 0u;
    gCitadel->emblemsAttempted = false;
}

static void EnsureFactionEmblemTextures(IDirect3DDevice9 *dev) {
    if (!gCitadel || !dev) return;
    const uint64_t gen = CurrentCitadelDeviceGen();
    if (gCitadel->emblemDevice && (gCitadel->emblemDevice != dev || gCitadel->emblemDeviceGen != gen)) {
        ReleaseFactionEmblemTextures();
    }
    if (gCitadel->emblemsAttempted) return;
    gCitadel->emblemDevice = dev;
    gCitadel->emblemDeviceGen = gen;
    gCitadel->emblemsAttempted = true;

    const char *dir = PortGameDir();
    if (!dir || !*dir) return;
    char err[192] = {};
    CeAssetCatalog *cat = PortCeAssetCatalogOpenKinds(dir, CE_ASSET_TEXTURE, err, sizeof(err));
    if (!cat) {
        PortSessionLog(PORT_SESSION_GAME, false,
                       "sanctuary citadel: emblem catalog unavailable (%s); using matte plaques",
                       err[0] ? err : "none");
        return;
    }

    static const char *const kPrimaryPaths[kEmblemCount] = {
        "goodies\\goodies_F_Bakgrnd.tga",
        "goodies\\goodies_M_Bakgrnd.tga",
    };
    static const char *const kFallbackPaths[kEmblemCount] = {
        "mustbe_goodies\\goodies_F_Bakgrnd.tga",
        "mustbe_goodies\\goodies_M_Bakgrnd.tga",
    };

    unsigned loadedCount = 0u;
    for (unsigned i = 0; i < kEmblemCount; ++i) {
        size_t idx = PortCeAssetCatalogFindTexture(cat, kPrimaryPaths[i]);
        if (idx == SIZE_MAX) {
            idx = PortCeAssetCatalogFindTexture(cat, kFallbackPaths[i]);
        }
        if (idx == SIZE_MAX) continue;
        FactionEmblemUploadCtx ctx = {dev, &gCitadel->emblems[i]};
        if (PortCeAssetCatalogReadTexture(cat, idx, UploadFactionEmblemCallback, &ctx) &&
            gCitadel->emblems[i].texture) {
            ++loadedCount;
        }
    }
    PortCeAssetCatalogClose(cat);

    PortSessionLog(PORT_SESSION_GAME, false,
                   "sanctuary citadel: loaded %u of %u retail faction emblem murals",
                   loadedCount, static_cast<unsigned>(kEmblemCount));
}

static void DrawFactionEmblemMurals(IDirect3DDevice9 *dev) {
    if (!gCitadel || !dev) return;

    const float zeroBias = 0.0f;
    dev->SetVertexShader(NULL);
    dev->SetPixelShader(NULL);
    dev->SetRenderState(D3DRS_DEPTHBIAS, *reinterpret_cast<const DWORD *>(&zeroBias));
    dev->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, *reinterpret_cast<const DWORD *>(&zeroBias));
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

    // Official Lost Toys faction emblem crop inside 512x512 goodies_{F,M}_Bakgrnd.tga.
    constexpr float kCropU0 = 0.00f;
    constexpr float kCropU1 = 0.46f;
    constexpr float kCropV0 = 0.00f;
    constexpr float kCropV1 = 0.56f;
    constexpr float kMaxW = 3.68f;
    constexpr float kMaxH = 4.28f;
    constexpr float kCenterH = 4.40f;
    const float wz = gCitadel->floorZ - kCenterH;

    struct MuralPlacement {
        unsigned slotIdx;
        float wallX;
        float rightY; // +1 for West wall facing East (+X), -1 for East wall facing West (-X)
        DWORD plaqueTop;
        DWORD plaqueBot;
    };
    // 6 cm clean standoff in front of the dark iron recessed mat (84.42f West / 139.58f East).
    static const MuralPlacement kMurals[kEmblemCount] = {
        {kEmblemForseti,  84.48f,  1.0f, 0xff1c3e64u, 0xff0e223au}, // West Forseti Alcove
        {kEmblemMuspell, 139.52f, -1.0f, 0xff622018u, 0xff34100cu}, // East Muspell Alcove
    };

    for (unsigned i = 0; i < kEmblemCount; ++i) {
        const MuralPlacement &mp = kMurals[i];
        const FactionEmblemSlot &slot = gCitadel->emblems[mp.slotIdx];

        if (slot.texture && slot.srcW > 0u && slot.srcH > 0u) {
            const float cropAspect = (static_cast<float>(slot.srcW) * (kCropU1 - kCropU0)) /
                                     (static_cast<float>(slot.srcH) * (kCropV1 - kCropV0));
            const float boxAspect = kMaxW / kMaxH;
            float w = kMaxW;
            float h = kMaxH;
            if (cropAspect > boxAspect) h = kMaxW / cropAspect;
            else w = kMaxH * cropAspect;
            const float halfW = w * 0.5f;
            const float halfH = h * 0.5f;
            const float yLeft = kCenterY - mp.rightY * halfW;
            const float yRight = kCenterY + mp.rightY * halfW;
            const float zTop = wz - halfH;
            const float zBot = wz + halfH;

            const EmblemTexVertex quad[4] = {
                {mp.wallX, yLeft,  zTop, 0xffffffffu, kCropU0, kCropV0},
                {mp.wallX, yRight, zTop, 0xffffffffu, kCropU1, kCropV0},
                {mp.wallX, yLeft,  zBot, 0xfff0ece4u, kCropU0, kCropV1},
                {mp.wallX, yRight, zBot, 0xfff0ece4u, kCropU1, kCropV1},
            };
            dev->SetTexture(0, slot.texture);
            dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
            dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
            dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
            dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
            dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
            dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(EmblemTexVertex));
        } else {
            // Clean matte brass-bordered archival faction plaque fallback when retail files are absent.
            const float halfW = kMaxW * 0.48f;
            const float halfH = kMaxH * 0.48f;
            const float yLeft = kCenterY - mp.rightY * halfW;
            const float yRight = kCenterY + mp.rightY * halfW;
            const float zTop = wz - halfH;
            const float zBot = wz + halfH;
            const OverlayVertex plaque[4] = {
                {mp.wallX, yLeft,  zTop, mp.plaqueTop},
                {mp.wallX, yRight, zTop, mp.plaqueTop},
                {mp.wallX, yLeft,  zBot, mp.plaqueBot},
                {mp.wallX, yRight, zBot, mp.plaqueBot},
            };
            dev->SetTexture(0, NULL);
            dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
            dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
            dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
            dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
            dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, plaque, sizeof(OverlayVertex));
        }
    }

    // Restore wrap/linear sampling for any subsequent textured passes inside the state guard.
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
    dev->SetRenderState(D3DRS_LIGHTING, TRUE);
    dev->SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1);
}

static void EnforceInteriorUnitCollisions() {
    if (!gCitadel || !gCitadel->active || gCitadel->solids.count == 0u) return;
    const float fz = gCitadel->floorZ;
    const float minX = kTerraceMinX - 3.0f;
    const float maxX = kTerraceMaxX + 3.0f;
    const float minY = kTerraceMinY - 3.0f;
    const float maxY = kTerraceMaxY + 3.0f;

    auto resolveThing = [&](CThing *t, float radius, float height) {
        if (!t || t->IsDying() || t->IsShuttingDown()) return;
        const FVector pos = t->GetPos();
        if (pos.X < minX || pos.X > maxX || pos.Y < minY || pos.Y > maxY) return;
        float cofg = t->COfGHeight();
        if (!std::isfinite(cofg) || cofg < 0.0f) cofg = 0.0f;
        FVector vel = t->GetVelocity();
        float x = pos.X, y = pos.Y;
        float vx = vel.X, vy = vel.Y;
        // The Cinema roof is a floor, not a wall: its rule moves the body in height
        // only, and it runs first, so a Jet-mode landing settles on the slab (clear
        // of the wall boxes beneath it) instead of sinking through or being shoved
        // off sideways.
        float z = pos.Z;
        float vz = vel.Z;
        const RoofClamp roof = ResolveCinemaRoofHeight(x, y, &z, fz, height, cofg, &vz);
        const bool pushed = ResolveInteriorCollision(gCitadel->solids, &x, &y, z, fz,
                                                     radius, height, cofg, &vx, &vy);
        if (pushed || roof != kRoofClampNone) {
            t->MoveTo(FVector(x, y, z));
            t->SetVelocity(FVector(vx, vy, vz));
        }
    };

    CBattleEngine *playerBe = NULL;
    if (CPlayer *pl = GAME.GetPlayer(0)) {
        playerBe = pl->GetBattleEngine();
        if (playerBe) {
            resolveThing(playerBe, kBattleEngineCollisionRadius, kBattleEngineCollisionHeight);
        }
    }
    ListIterator<CUnit> it(&WORLD.GetUnitNB());
    for (CUnit *u = it.First(); u; u = it.Next()) {
        if (!u || u == static_cast<CUnit *>(playerBe)) continue;
        float r = u->GetRadius();
        if (!std::isfinite(r) || r < 0.75f || r > 1.80f) r = kBattleEngineCollisionRadius;
        resolveThing(u, r, kBattleEngineCollisionHeight);
    }
}

// Where the camera and the player's machine are, read now. The outdoor-draw skip
// asks before the world is drawn and the weapon gate asks when the trigger is
// pulled, so neither works from the frame before: a machine stepping through the
// doorway never gets one frame of fire or of outdoor world into the hall.
static void RefreshCinemaOccupancy() {
    if (!gCitadel || !gCitadel->active) return;
    const float fz = gCitadel->floorZ;
    if (CCamera *cam = ENGINE.GetCameraForViewpoint(0)) {
        const FVector eye = cam->GetPos();
        if (IsFinite3(eye.X, eye.Y, eye.Z))
            gCitadel->insideCinema = IsInsideCinema(eye.X, eye.Y, eye.Z, fz);
    }
    // The player's machine is the unit they are piloting, when they pilot one (Play
    // Any Unit): a Battle Engine left parked in the hall must not disarm a tank the
    // player is driving outside, and a piloted unit inside it must be disarmed.
    bool playerInside = false;
    CUnit *driven = NULL;
    if (PortCePlayUnitIsDriven) {
        ListIterator<CUnit> it(&WORLD.GetUnitNB());
        for (CUnit *u = it.First(); u; u = it.Next()) {
            if (u && !u->IsDying() && !u->IsShuttingDown() && PortCePlayUnitIsDriven(u)) {
                driven = u;
                break;
            }
        }
    }
    if (driven) {
        const FVector pos = driven->GetPos();
        playerInside = IsInsideCinema(pos.X, pos.Y, pos.Z, fz);
    } else if (CPlayer *pl = GAME.GetPlayer(0)) {
        if (CBattleEngine *be = pl->GetBattleEngine()) {
            const FVector pos = be->GetPos();
            playerInside = IsInsideCinema(pos.X, pos.Y, pos.Z, fz);
        }
    }
    gCitadel->playerInsideCinema = playerInside;
}

} // namespace

// -----------------------------------------------------------------------------
// Exported CeSanctuaryCitadel Runtime Implementation
// -----------------------------------------------------------------------------
void RuntimeInit(float floorZ) {
    RuntimeShutdown();
    gCitadel = static_cast<CitadelState *>(std::calloc(1, sizeof(CitadelState)));
    if (!gCitadel) return;

    gCitadel->active = true;
    gCitadel->floorZ = std::isfinite(floorZ) ? floorZ : kFloorZ;
    gCitadel->eyeX = kCenterX;
    gCitadel->eyeY = 366.0f;
    gCitadel->eyeZ = gCitadel->floorZ - 2.0f;
    gCitadel->aimX = 0.0f;
    gCitadel->aimY = 1.0f;
    gCitadel->aimZ = 0.0f;
    gCitadel->hasAim = false;
    ResetRipples(&gCitadel->ripples);
    gCitadel->solids = MakeInteriorSolidLayout();

    BuildCitadelArchitecture();
    BuildGeodesicHexDome();
    BuildDomeGroundCollar();
    gCitadel->built = (gCitadel->usedVerts > 0u && gCitadel->usedIndices > 0u && !gCitadel->overflow);

    if (IDirect3DDevice9 *dev = LT.GetDevice()) {
        CeMuseumTextures::LoadAll(dev);
        gCitadel->texturesLoaded = true;
    }

    // Mount live In-World Archive Pedestal flush atop the central octagonal rotunda dais.
    CeArchivePedestal::RuntimeInit();
    CeArchivePedestal::RuntimeSetBasePose(kPedestalX, kPedestalY, kPedestalHeight);

    PortSessionLog(PORT_SESSION_GAME, false,
                   "sanctuary citadel: built (%u verts, %u tris, %u shadow tris, %u hex cells, %u hex ribs, floorZ=%.2f)",
                   gCitadel->usedVerts, gCitadel->usedIndices / 3u,
                   gCitadel->shadowIdxCount / 3u,
                   gCitadel->hexCellCount, gCitadel->hexEdgeCount,
                   gCitadel->floorZ);
}

void RuntimeShutdown() {
    if (!gCitadel) return;
    ReleaseGpuBuffers();
    ReleaseFactionEmblemTextures();
    CeArchivePedestal::RuntimeShutdown();
    if (gCitadel->texturesLoaded) {
        CeMuseumTextures::ReleaseAll();
        gCitadel->texturesLoaded = false;
    }
    std::free(gCitadel);
    gCitadel = NULL;
}

void RuntimeUpdate(float dtSeconds, bool paused,
                   float eyeX, float eyeY, float eyeZ,
                   float aimX, float aimY, float aimZ) {
    if (!gCitadel || !gCitadel->active) return;

    if (!paused && dtSeconds > 0.0f && std::isfinite(dtSeconds)) {
        const float dt = dtSeconds > kRippleMaxDt ? kRippleMaxDt : dtSeconds;
        gCitadel->animTime += dt;
        if (gCitadel->animTime > 6283.1853f) gCitadel->animTime -= 6283.1853f;
    }
    StepRipples(&gCitadel->ripples, dtSeconds, paused);

    if (IsFinite3(eyeX, eyeY, eyeZ)) {
        gCitadel->eyeX = eyeX;
        gCitadel->eyeY = eyeY;
        gCitadel->eyeZ = eyeZ;
        gCitadel->insideCinema = IsInsideCinema(eyeX, eyeY, eyeZ, gCitadel->floorZ);
    }
    if (IsFinite3(aimX, aimY, aimZ) && (aimX * aimX + aimY * aimY + aimZ * aimZ) > 1e-8f) {
        gCitadel->aimX = aimX;
        gCitadel->aimY = aimY;
        gCitadel->aimZ = aimZ;
        gCitadel->hasAim = true;
    }

    float lEyeX = 0.0f, lEyeY = 0.0f, lEyeZ = 0.0f;
    float lAimX = 0.0f, lAimY = 1.0f, lAimZ = 0.0f;
    WorldToPedestalLocal(gCitadel->eyeX, gCitadel->eyeY, gCitadel->eyeZ, gCitadel->floorZ,
                         &lEyeX, &lEyeY, &lEyeZ);
    WorldAimToPedestalLocal(gCitadel->aimX, gCitadel->aimY, gCitadel->aimZ,
                            &lAimX, &lAimY, &lAimZ);
    CeArchivePedestal::RuntimeUpdate(CeMuseumTour::kWingAtrium, dtSeconds, paused,
                                     lEyeX, lEyeY, lEyeZ, lAimX, lAimY, lAimZ);
}

// The simulation's share of the Citadel, once per game tick while unpaused:
// units are pushed out of solid masonry, bodies on or under the Cinema roof are
// held at its height, and the player's Cinema occupancy is brought up to date.
void RuntimeTickSim() {
    if (!gCitadel || !gCitadel->active) return;
    EnforceInteriorUnitCollisions();
    RefreshCinemaOccupancy();
}

bool RuntimeHandleKey(unsigned dik, bool repeat) {
    if (!gCitadel || !gCitadel->active) return false;
    const bool aimingControl = CeArchivePedestal::RuntimeIsAimingControl();
    // Never steal default Battle Engine Cloak (Tab) unless directly aiming at the pedestal rail.
    if (dik == CeArchivePedestal::kDikTab && !CeArchivePedestal::RuntimeIsAimingControl())
        return false;
    // Require aiming at a control or close ground-level pedestal focus for [/]/PgUp/PgDn/P
    // so flying overhead in Jet mode never steals keys from the Battle Engine.
    if (dik == CeArchivePedestal::kDikLBracket || dik == CeArchivePedestal::kDikRBracket ||
        dik == CeArchivePedestal::kDikPrior || dik == CeArchivePedestal::kDikNext ||
        dik == CeArchivePedestal::kDikP) {
        bool closeFocus = false;
        if (!aimingControl && gCitadel->hasAim) {
            const float heightAboveFloor = gCitadel->floorZ - gCitadel->eyeZ;
            if (heightAboveFloor >= -2.0f && heightAboveFloor <= 7.5f) {
                float lEyeX = 0.0f, lEyeY = 0.0f, lEyeZ = 0.0f;
                float lAimX = 0.0f, lAimY = 1.0f, lAimZ = 0.0f;
                WorldToPedestalLocal(gCitadel->eyeX, gCitadel->eyeY, gCitadel->eyeZ, gCitadel->floorZ,
                                     &lEyeX, &lEyeY, &lEyeZ);
                WorldAimToPedestalLocal(gCitadel->aimX, gCitadel->aimY, gCitadel->aimZ,
                                        &lAimX, &lAimY, &lAimZ);
                const CeArchivePedestal::BasePose pose{
                    kPedestalX - kMuseumEntranceX,
                    kPedestalY - kMuseumEntranceY,
                    kPedestalHeight,
                    CeArchivePedestal::kPedestalPlinthHalf,
                    true
                };
                closeFocus = CeArchivePedestal::FocusPedestal(pose, lEyeX, lEyeY, lAimX, lAimY);
            }
        }
        if (!aimingControl && !closeFocus)
            return false;
    }
    if (CeArchivePedestal::RuntimeHandleKey(dik, repeat))
        return true;
    // Allow R shortcut strictly when aiming directly at the Archive Pedestal controls
    // (Space stays unbound so default Battle Engine Morph never collides).
    if (!repeat && CeArchivePedestal::RuntimeIsAimingControl() && dik == kDikR) {
        return CeArchivePedestal::RuntimeHandleKey(CeArchivePedestal::kDikP, false);
    }
    return false;
}

bool RuntimeHandleFire(bool release, float value) {
    if (!gCitadel || !gCitadel->active) return false;
    if (CeArchivePedestal::RuntimeHandleFire(release, value))
        return true;
    RefreshCinemaOccupancy();
    if (CinemaDisarmsWeapons(gCitadel->playerInsideCinema, gCitadel->insideCinema))
        return true;
    return false;
}

bool RuntimeIsInsideDome(float x, float y, float z) {
    const float fz = gCitadel ? gCitadel->floorZ : kFloorZ;
    return IsInsideDome(x, y, z, fz);
}

bool RuntimeIsInsideCinema(float x, float y, float z) {
    const float fz = gCitadel ? gCitadel->floorZ : kFloorZ;
    return IsInsideCinema(x, y, z, fz);
}

bool RuntimeCue(float eyeX, float eyeY, char *title, unsigned titleCap,
                char *line, unsigned lineCap, char *note, unsigned noteCap) {
    if (!gCitadel || !gCitadel->active || !title || !titleCap || !line || !lineCap)
        return false;

    float lEyeX = 0.0f, lEyeY = 0.0f, lEyeZ = 0.0f;
    WorldToPedestalLocal(eyeX, eyeY, gCitadel->eyeZ, gCitadel->floorZ, &lEyeX, &lEyeY, &lEyeZ);
    float fwdX = gCitadel->aimX, fwdY = gCitadel->aimY;
    if (!gCitadel->hasAim) {
        fwdX = kPedestalX - eyeX;
        fwdY = kPedestalY - eyeY;
    }
    if (CeArchivePedestal::RuntimeCue(lEyeX, lEyeY, fwdX, fwdY, CeMuseumTour::kWingAtrium,
                                      title, titleCap, line, lineCap, note, noteCap)) {
        return true;
    }

    if (IsInsideCinema(eyeX, eyeY, gCitadel->eyeZ, gCitadel->floorZ)) {
        std::snprintf(title, titleCap, "Sanctuary Screening Pavilion");
        std::snprintf(line, lineCap, "Sealed acoustic theater overlooking the Citadel Rotunda.");
        if (note && noteCap)
            std::snprintf(note, noteCap, "Exit south through the colonnade portal to rejoin the overlook.");
        return true;
    }

    if (std::hypot(eyeX - kForsetiAlcoveX, eyeY - kForsetiAlcoveY) <= kAlcoveCueRadius) {
        std::snprintf(title, titleCap, "Forseti Commonwealth Memorial");
        std::snprintf(line, lineCap, "Honouring the defenders of the coastal front and air wings.");
        if (note && noteCap)
            std::snprintf(note, noteCap, "Step east to the Rotunda to browse the live Archive Pedestal.");
        return true;
    }

    if (std::hypot(eyeX - kMuspellAlcoveX, eyeY - kMuspellAlcoveY) <= kAlcoveCueRadius) {
        std::snprintf(title, titleCap, "Muspell Dominion Memorial");
        std::snprintf(line, lineCap, "Forged-iron tribute to heavy armour and siege walkers.");
        if (note && noteCap)
            std::snprintf(note, noteCap, "Step west to the Rotunda to browse the live Archive Pedestal.");
        return true;
    }

    return false;
}

static bool CitadelWithinDrawRange(float floorZ) {
    if (!gCitadel) return false;
    if (gCitadel->insideCinema) return true;
    float ex = gCitadel->eyeX, ey = gCitadel->eyeY, ez = gCitadel->eyeZ;
    if (CCamera *cam = ENGINE.GetCameraForViewpoint(0)) {
        const FVector p = cam->GetPos();
        ex = p.X;
        ey = p.Y;
        ez = p.Z;
    }
    const float dx = ex - kCenterX;
    const float dy = ey - kCenterY;
    const float dz = ez - (floorZ - 20.0f);
    // Keep Citadel drawn everywhere within fog/sightline range (500m sphere around center).
    return (dx * dx + dy * dy + dz * dz) <= (500.0f * 500.0f);
}

void RuntimeDrawWorld() {
    if (!gCitadel || !gCitadel->active || !gCitadel->built || gMirrorPass) return;
    IDirect3DDevice9 *dev = LT.GetDevice();
    if (!dev) return;

    if (!gCitadel->texturesLoaded) {
        CeMuseumTextures::LoadAll(dev);
        gCitadel->texturesLoaded = true;
    }
    EnsureGpuBuffers(dev);
    EnsureFactionEmblemTextures(dev);

    if (!CitadelWithinDrawRange(gCitadel->floorZ)) return;

    IDirect3DStateBlock9 *saved = NULL;
    if (FAILED(dev->CreateStateBlock(D3DSBT_ALL, &saved)) || !saved) return;
    if (FAILED(saved->Capture())) { saved->Release(); return; }

    D3DMATRIX world = {}, view = {}, proj = {};
    world._11 = world._22 = world._33 = world._44 = 1.0f;
    RENDERINFO.GetView(&view);
    RENDERINFO.GetProjection(&proj);

    dev->SetVertexShader(NULL);
    dev->SetPixelShader(NULL);
    dev->SetTransform(D3DTS_WORLD, &world);
    dev->SetTransform(D3DTS_VIEW, &view);
    dev->SetTransform(D3DTS_PROJECTION, &proj);
    dev->SetTexture(0, NULL);
    dev->SetTexture(1, NULL);

    SetupCitadelLightRig(dev);

    D3DMATERIAL9 mat = {};
    mat.Diffuse.r = mat.Diffuse.g = mat.Diffuse.b = mat.Diffuse.a = 1.0f;
    mat.Ambient = mat.Diffuse;

    dev->SetRenderState(D3DRS_LIGHTING, TRUE);
    dev->SetRenderState(D3DRS_COLORVERTEX, TRUE);
    dev->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);
    dev->SetRenderState(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_COLOR1);
    dev->SetRenderState(D3DRS_SPECULARMATERIALSOURCE, D3DMCS_MATERIAL);
    dev->SetRenderState(D3DRS_EMISSIVEMATERIALSOURCE, D3DMCS_MATERIAL);
    dev->SetRenderState(D3DRS_LOCALVIEWER, TRUE);
    dev->SetRenderState(D3DRS_NORMALIZENORMALS, TRUE);
    dev->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
    dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 15);
    dev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    dev->SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1);

    const bool useLitGpu = (gCitadel->litVb != NULL && gCitadel->litIb != NULL);
    if (useLitGpu) {
        dev->SetStreamSource(0, gCitadel->litVb, 0, sizeof(LitVertex));
        dev->SetIndices(gCitadel->litIb);
    }

    for (unsigned m = 0; m < kMatCount; ++m) {
        if (!gCitadel->triCount[m] || gCitadel->minVert[m] > gCitadel->maxVert[m]) continue;
        IDirect3DTexture9 *tex = (kMaterials[m].tex < CE_MUSEUM_TEX_COUNT)
            ? CeMuseumTextures::Get(kMaterials[m].tex) : NULL;
        dev->SetTexture(0, tex);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, tex ? D3DTOP_MODULATE : D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1, tex ? D3DTA_TEXTURE : D3DTA_DIFFUSE);
        dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);

        mat.Emissive.r = kMaterials[m].emissiveR;
        mat.Emissive.g = kMaterials[m].emissiveG;
        mat.Emissive.b = kMaterials[m].emissiveB;
        mat.Specular.r = kMaterials[m].specR;
        mat.Specular.g = kMaterials[m].specG;
        mat.Specular.b = kMaterials[m].specB;
        mat.Specular.a = 1.0f;
        mat.Power = kMaterials[m].specPower;
        const bool hasSpec = (kMaterials[m].specR > 0.0f || kMaterials[m].specG > 0.0f || kMaterials[m].specB > 0.0f);
        dev->SetRenderState(D3DRS_SPECULARENABLE, hasSpec ? TRUE : FALSE);
        dev->SetMaterial(&mat);

        // Only flat floor inlays and flat glow strips receive depth bias; never 3D solid frames/trims like kMatBrass.
        const bool isInlay = (m == kMatRunner || m == kMatWarmGlow ||
                              m == kMatForsetiGlow || m == kMatMuspellGlow || m == kMatAquaGlow);
        const float bias = isInlay ? -0.00002f : 0.0f;
        const float slope = isInlay ? -1.0f : 0.0f;
        dev->SetRenderState(D3DRS_DEPTHBIAS, *reinterpret_cast<const DWORD *>(&bias));
        dev->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, *reinterpret_cast<const DWORD *>(&slope));

        const unsigned numVerts = gCitadel->maxVert[m] - gCitadel->minVert[m] + 1u;
        if (useLitGpu) {
            dev->DrawIndexedPrimitive(
                D3DPT_TRIANGLELIST,
                0,
                gCitadel->minVert[m],
                numVerts,
                gCitadel->firstIdx[m],
                gCitadel->triCount[m]);
        } else {
            dev->DrawIndexedPrimitiveUP(
                D3DPT_TRIANGLELIST,
                gCitadel->minVert[m],
                numVerts,
                gCitadel->triCount[m],
                gCitadel->sortedIndices + gCitadel->firstIdx[m],
                D3DFMT_INDEX32,
                gCitadel->verts,
                sizeof(LitVertex));
        }
    }

    // Official Lost Toys Forseti & Muspell emblem murals inside West & East Memorial Alcoves.
    // Kept visible even from inside the Cinema Pavilion so the open South colonnade portal
    // frames the Rotunda cleanly instead of blocking the exit with an opaque curtain.
    DrawFactionEmblemMurals(dev);

    // Soft directional contact shadows grounded beneath every column, dais, plinth, and pier
    dev->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetTexture(0, NULL);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);

    if (gCitadel->shadowVertCount > 0u && gCitadel->shadowIdxCount >= 3u) {
        const float shadowBias = -0.00003f, shadowSlope = -1.0f;
        dev->SetRenderState(D3DRS_DEPTHBIAS, *reinterpret_cast<const DWORD *>(&shadowBias));
        dev->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, *reinterpret_cast<const DWORD *>(&shadowSlope));
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        if (gCitadel->shadowVb && gCitadel->shadowIb) {
            dev->SetStreamSource(0, gCitadel->shadowVb, 0, sizeof(OverlayVertex));
            dev->SetIndices(gCitadel->shadowIb);
            dev->DrawIndexedPrimitive(
                D3DPT_TRIANGLELIST, 0, 0u, gCitadel->shadowVertCount,
                0u, gCitadel->shadowIdxCount / 3u);
        } else {
            dev->DrawIndexedPrimitiveUP(
                D3DPT_TRIANGLELIST, 0u, gCitadel->shadowVertCount,
                gCitadel->shadowIdxCount / 3u, gCitadel->shadowIndices,
                D3DFMT_INDEX16, gCitadel->shadowVerts, sizeof(OverlayVertex));
        }
    }

    // Subtle additive Shield Emitter Beams rising from the 4 rotunda corner pylons
    if (gCitadel->beamVertCount > 0u && gCitadel->beamIdxCount >= 3u) {
        const float zeroBias = 0.0f;
        dev->SetRenderState(D3DRS_DEPTHBIAS, *reinterpret_cast<const DWORD *>(&zeroBias));
        dev->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, *reinterpret_cast<const DWORD *>(&zeroBias));
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
        dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        if (gCitadel->beamVb && gCitadel->beamIb) {
            dev->SetStreamSource(0, gCitadel->beamVb, 0, sizeof(OverlayVertex));
            dev->SetIndices(gCitadel->beamIb);
            dev->DrawIndexedPrimitive(
                D3DPT_TRIANGLELIST, 0, 0u, gCitadel->beamVertCount,
                0u, gCitadel->beamIdxCount / 3u);
        } else {
            dev->DrawIndexedPrimitiveUP(
                D3DPT_TRIANGLELIST, 0u, gCitadel->beamVertCount,
                gCitadel->beamIdxCount / 3u, gCitadel->beamIndices,
                D3DFMT_INDEX16, gCitadel->beamVerts, sizeof(OverlayVertex));
        }
    }

    // Draw the live All-232/213 Archive Pedestal INSIDE this D3DSBT_ALL state guard
    // (eliminates an extra per-frame CreateStateBlock). Reset bias, blend, and streams first.
    {
        const float zeroBias = 0.0f;
        dev->SetRenderState(D3DRS_DEPTHBIAS, *reinterpret_cast<const DWORD *>(&zeroBias));
        dev->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, *reinterpret_cast<const DWORD *>(&zeroBias));
        dev->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        dev->SetStreamSource(0, NULL, 0, 0);
        dev->SetIndices(NULL);
        CeArchivePedestal::RuntimeDraw(gCitadel->floorZ, false);
    }

    RestoreEngineRenderState(saved);
}

void RuntimeDrawDomeFx() {
    if (!gCitadel || !gCitadel->active || !gCitadel->built || gMirrorPass)
        return;
    if (gCitadel->insideCinema || !CitadelWithinDrawRange(gCitadel->floorZ))
        return;
    IDirect3DDevice9 *dev = LT.GetDevice();
    if (!dev) return;

    IDirect3DStateBlock9 *saved = NULL;
    if (FAILED(dev->CreateStateBlock(D3DSBT_ALL, &saved)) || !saved) return;
    if (FAILED(saved->Capture())) { saved->Release(); return; }

    D3DMATRIX world = {}, view = {}, proj = {};
    world._11 = world._22 = world._33 = world._44 = 1.0f;
    RENDERINFO.GetView(&view);
    RENDERINFO.GetProjection(&proj);

    dev->SetVertexShader(NULL);
    dev->SetPixelShader(NULL);
    dev->SetTransform(D3DTS_WORLD, &world);
    dev->SetTransform(D3DTS_VIEW, &view);
    dev->SetTransform(D3DTS_PROJECTION, &proj);
    dev->SetTexture(0, NULL);
    dev->SetTexture(1, NULL);
    const float zeroBias = 0.0f;
    dev->SetRenderState(D3DRS_DEPTHBIAS, *reinterpret_cast<const DWORD *>(&zeroBias));
    dev->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, *reinterpret_cast<const DWORD *>(&zeroBias));
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 15);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);

    const float cx = kCenterX, cy = kCenterY, cz = gCitadel->floorZ;
    const float t = gCitadel->animTime;
    // Seen from inside, the glass overhead nearly vanishes and the ribs drop to
    // 40%, so nothing bright crosses the reticle; the horizon band and the impact
    // ripples keep their strength (DomeShellGain / DomeRibGain).
    const float insideBlend = DomeViewerInsideBlend(gCitadel->eyeX, gCitadel->eyeY, gCitadel->eyeZ, cz);

    // -------------------------------------------------------------------------
    // Pass 1: Translucent Shimmering Cyan-Gold Fresnel Geodesic Shell (Alpha Blend)
    // -------------------------------------------------------------------------
    {
        unsigned vCount = 0, iCount = 0;
        for (unsigned ci = 0; ci < gCitadel->hexCellCount; ++ci) {
            const HexCell &c = gCitadel->hexCells[ci];
            if (vCount + c.count + 1u > kDomeVertCap || iCount + c.count * 3u > kDomeIdxCap) break;

            const float az = std::atan2(c.cy, c.cx);
            const float elev = Clamp01(-c.cz); // 0 at horizon & skirt, 1 at zenith
            const float fresnel = 1.0f - elev;
            const float vPhase = -c.cz; // continuous vertical shimmer across equator into skirt
            const float shimmer = 0.5f + 0.5f * std::sin(t * 1.45f + az * 3.0f - vPhase * 6.5f);

            // Subtle crystal-clear shell overhead (~0.045 alpha), richer cyan-gold rim near horizon & skirt (~0.18 alpha)
            const float baseA = (0.042f + 0.135f * (fresnel * fresnel) + 0.030f * shimmer) *
                                DomeShellGain(elev, insideBlend);
            const float goldMix = Clamp01((0.35f - elev) * 2.2f) * 0.45f;
            const float rCol = 0.30f + 0.58f * goldMix + 0.10f * shimmer;
            const float gCol = 0.78f + 0.12f * goldMix + 0.08f * shimmer;
            const float bCol = 0.96f - 0.36f * goldMix;

            const DWORD cCenter = MakeArgb(baseA * 0.72f, rCol, gCol, bCol);
            const DWORD cEdge   = MakeArgb(baseA * 1.25f, rCol, gCol, bCol);

            const uint16_t base = static_cast<uint16_t>(vCount);
            gCitadel->domeVerts[vCount++] = {cx + c.px, cy + c.py, cz + c.pz, cCenter};
            for (unsigned k = 0; k < c.count; ++k) {
                gCitadel->domeVerts[vCount++] = {cx + c.vx[k], cy + c.vy[k], cz + c.vz[k], cEdge};
            }
            for (unsigned k = 0; k < c.count; ++k) {
                const unsigned kn = (k + 1u) % c.count;
                gCitadel->domeIndices[iCount++] = base;
                gCitadel->domeIndices[iCount++] = static_cast<uint16_t>(base + 1u + k);
                gCitadel->domeIndices[iCount++] = static_cast<uint16_t>(base + 1u + kn);
            }
        }
        if (vCount > 0u && iCount >= 3u) {
            dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
            dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
            dev->DrawIndexedPrimitiveUP(
                D3DPT_TRIANGLELIST, 0u, vCount, iCount / 3u,
                gCitadel->domeIndices, D3DFMT_INDEX16,
                gCitadel->domeVerts, sizeof(OverlayVertex));
        }
    }

    // -------------------------------------------------------------------------
    // Pass 2: Crisp Luminous Hexagonal Geodesic Lattice Ribs + Ground-Contact Ribbon (Additive)
    // -------------------------------------------------------------------------
    {
        unsigned vCount = 0, iCount = 0;
        const float halfW = 0.14f;
        for (unsigned ei = 0; ei < gCitadel->hexEdgeCount; ++ei) {
            if (vCount + 4u > kDomeVertCap || iCount + 6u > kDomeIdxCap) break;
            const HexEdge &e = gCitadel->hexEdges[ei];
            const Vec3 dir = VNorm(Vec3{e.bx - e.ax, e.by - e.ay, e.bz - e.az});
            const Vec3 nrm = Vec3{e.nx, e.ny, e.nz};
            const Vec3 side = VNorm(VCross(nrm, dir));

            const float elev = Clamp01(-e.nz);
            const float az = std::atan2(e.ny, e.nx);
            const float vPhase = -0.5f * (e.az + e.bz) / kDomeVisualRadius;
            const float pulse = 0.5f + 0.5f * std::sin(t * 2.1f - vPhase * 7.0f + az * 2.0f);
            const float alpha = (0.16f + 0.24f * (1.0f - elev) + 0.12f * pulse) *
                                DomeRibGain(elev, insideBlend);
            const float gold = Clamp01((0.30f - elev) * 2.5f) * 0.55f;
            const DWORD col = MakeArgb(alpha, 0.36f + 0.58f * gold, 0.86f + 0.08f * pulse, 1.00f - 0.38f * gold);

            const float sx = side.x * halfW, sy = side.y * halfW, sz = side.z * halfW;
            const uint16_t base = static_cast<uint16_t>(vCount);
            gCitadel->domeVerts[vCount++] = {cx + e.ax - sx, cy + e.ay - sy, cz + e.az - sz, col};
            gCitadel->domeVerts[vCount++] = {cx + e.ax + sx, cy + e.ay + sy, cz + e.az + sz, col};
            gCitadel->domeVerts[vCount++] = {cx + e.bx + sx, cy + e.by + sy, cz + e.bz + sz, col};
            gCitadel->domeVerts[vCount++] = {cx + e.bx - sx, cy + e.by - sy, cz + e.bz - sz, col};
            gCitadel->domeIndices[iCount++] = base + 0u;
            gCitadel->domeIndices[iCount++] = base + 1u;
            gCitadel->domeIndices[iCount++] = base + 2u;
            gCitadel->domeIndices[iCount++] = base + 0u;
            gCitadel->domeIndices[iCount++] = base + 2u;
            gCitadel->domeIndices[iCount++] = base + 3u;
        }

        // Terrain-conforming 3-row soft luminous ground-contact ribbon using 128 samples
        // precomputed once at RuntimeInit (gz + bury [alpha=0] -> gz - 0.15 [peak] -> gz - 3.20 [alpha=0]).
        auto domeSurfaceRadius = [](float relZ) -> float {
            if (relZ >= 0.0f) return kDomeVisualRadius;
            const float r2 = kDomeVisualRadius * kDomeVisualRadius - relZ * relZ;
            return r2 > 0.0f ? std::sqrt(r2) : 0.0f;
        };
        if (vCount + (kCollarSegs + 1u) * 3u <= kDomeVertCap &&
            iCount + kCollarSegs * 12u <= kDomeIdxCap) {
            const uint16_t base = static_cast<uint16_t>(vCount);
            const float step = 6.283185307f / static_cast<float>(kCollarSegs);
            for (unsigned i = 0; i <= kCollarSegs; ++i) {
                const unsigned idx = i % kCollarSegs;
                const float a = static_cast<float>(idx) * step;
                const float ca = gCitadel->collarCos[idx];
                const float sa = gCitadel->collarSin[idx];
                const float gz = gCitadel->collarGroundZ[idx];
                const float bury = gCitadel->collarBury[idx];
                const float zBot = gz + bury;
                const float zMid = gz - 0.15f;
                const float zTop = gz - 3.20f;
                const float rBot = domeSurfaceRadius(zBot - cz);
                const float rMid = domeSurfaceRadius(zMid - cz);
                const float rTop = domeSurfaceRadius(zTop - cz);

                const float shimmer = 0.5f + 0.5f * std::sin(t * 2.8f + a * 6.0f);
                const DWORD cBot = MakeArgb(0.0f, 0.88f, 0.84f, 0.52f);
                const DWORD cMid = MakeArgb(0.44f + 0.15f * shimmer,
                                            0.78f + 0.18f * shimmer,
                                            0.88f + 0.04f * shimmer,
                                            0.62f + 0.18f * (1.0f - shimmer));
                const DWORD cTop = MakeArgb(0.0f, 0.34f, 0.88f, 1.00f);

                gCitadel->domeVerts[vCount++] = {cx + ca * rBot, cy + sa * rBot, zBot, cBot};
                gCitadel->domeVerts[vCount++] = {cx + ca * rMid, cy + sa * rMid, zMid, cMid};
                gCitadel->domeVerts[vCount++] = {cx + ca * rTop, cy + sa * rTop, zTop, cTop};
            }
            for (unsigned i = 0; i < kCollarSegs; ++i) {
                const uint16_t a0 = static_cast<uint16_t>(base + i * 3u + 0u);
                const uint16_t a1 = static_cast<uint16_t>(base + i * 3u + 1u);
                const uint16_t a2 = static_cast<uint16_t>(base + i * 3u + 2u);
                const uint16_t b0 = static_cast<uint16_t>(base + (i + 1u) * 3u + 0u);
                const uint16_t b1 = static_cast<uint16_t>(base + (i + 1u) * 3u + 1u);
                const uint16_t b2 = static_cast<uint16_t>(base + (i + 1u) * 3u + 2u);
                // Lower buried fade strip (gz + bury -> gz - 0.15)
                gCitadel->domeIndices[iCount++] = a0;
                gCitadel->domeIndices[iCount++] = b0;
                gCitadel->domeIndices[iCount++] = b1;
                gCitadel->domeIndices[iCount++] = a0;
                gCitadel->domeIndices[iCount++] = b1;
                gCitadel->domeIndices[iCount++] = a1;
                // Upper luminous ground-contact fade strip (gz - 0.15 -> gz - 3.20)
                gCitadel->domeIndices[iCount++] = a1;
                gCitadel->domeIndices[iCount++] = b1;
                gCitadel->domeIndices[iCount++] = b2;
                gCitadel->domeIndices[iCount++] = a1;
                gCitadel->domeIndices[iCount++] = b2;
                gCitadel->domeIndices[iCount++] = a2;
            }
        }

        // ---------------------------------------------------------------------
        // Pass 2B: Animated Expanding Hexagonal Impact Shockwave Ripples (0.65 s)
        // ---------------------------------------------------------------------
        for (unsigned ri = 0; ri < kMaxRipples; ++ri) {
            const ImpactRipple &rip = gCitadel->ripples.slots[ri];
            if (!rip.active) continue;
            const float u = Clamp01(rip.age / kRippleDuration);
            const float fade = rip.intensity * std::pow(1.0f - u, 1.55f);
            if (fade < 0.01f) continue;

            const float shellR = kDomeVisualRadius + 0.10f;
            const bool onSkirt = (rip.z > cz);
            Vec3 nrm;
            Vec3 centerRel;
            if (onSkirt) {
                const float hr = std::hypot(rip.x - cx, rip.y - cy);
                nrm = hr > 1e-4f ? Vec3{(rip.x - cx) / hr, (rip.y - cy) / hr, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};
                float relZ = rip.z - cz;
                if (relZ > kDomeVisualSkirtMaxZ) relZ = kDomeVisualSkirtMaxZ;
                centerRel = {nrm.x * shellR, nrm.y * shellR, relZ};
            } else {
                nrm = VNorm(Vec3{rip.x - cx, rip.y - cy, rip.z - cz});
                centerRel = {nrm.x * shellR, nrm.y * shellR, nrm.z * shellR};
            }
            const Vec3 upRef = std::fabs(nrm.z) < 0.9f ? Vec3{0.0f, 0.0f, -1.0f} : Vec3{1.0f, 0.0f, 0.0f};
            const Vec3 tU = VNorm(VCross(upRef, nrm));
            const Vec3 tV = VCross(nrm, tU);

            auto projectRipplePt = [shellR](Vec3 q) -> Vec3 {
                if (q.z > 0.0f) {
                    const float qhr = std::hypot(q.x, q.y);
                    const float px = qhr > 1e-4f ? (q.x / qhr) * shellR : shellR;
                    const float py = qhr > 1e-4f ? (q.y / qhr) * shellR : 0.0f;
                    const float pz = q.z > kDomeVisualSkirtMaxZ ? kDomeVisualSkirtMaxZ : q.z;
                    return Vec3{px, py, pz};
                }
                const Vec3 pn = VNorm(q);
                return Vec3{pn.x * shellR, pn.y * shellR, pn.z * shellR};
            };

            const float rOuter = 0.9f + 11.5f * std::sqrt(u);
            const float bandW = 2.0f * (1.0f - 0.45f * u);
            const float rInner = rOuter > bandW ? (rOuter - bandW) : 0.0f;
            const unsigned sides = 6u;

            if (vCount + sides * 2u > kDomeVertCap || iCount + sides * 6u > kDomeIdxCap) break;
            const uint16_t base = static_cast<uint16_t>(vCount);
            const float hot = Clamp01(1.0f - u * 3.0f);
            const DWORD cOuter = MakeArgb(fade * 0.95f, 0.55f + 0.45f * hot, 0.94f + 0.06f * hot, 1.00f);
            const DWORD cInner = MakeArgb(fade * 0.25f, 0.20f + 0.60f * hot, 0.80f, 1.00f);

            for (unsigned k = 0; k < sides; ++k) {
                const float a = float(k) * (6.283185307f / float(sides));
                const float ca = std::cos(a), sa = std::sin(a);
                const Vec3 d = {tU.x * ca + tV.x * sa, tU.y * ca + tV.y * sa, tU.z * ca + tV.z * sa};
                const Vec3 pIn = projectRipplePt(Vec3{centerRel.x + d.x * rInner,
                                                      centerRel.y + d.y * rInner,
                                                      centerRel.z + d.z * rInner});
                const Vec3 pOut = projectRipplePt(Vec3{centerRel.x + d.x * rOuter,
                                                       centerRel.y + d.y * rOuter,
                                                       centerRel.z + d.z * rOuter});
                gCitadel->domeVerts[vCount++] = {cx + pIn.x,  cy + pIn.y,  cz + pIn.z,  cInner};
                gCitadel->domeVerts[vCount++] = {cx + pOut.x, cy + pOut.y, cz + pOut.z, cOuter};
            }
            for (unsigned k = 0; k < sides; ++k) {
                const unsigned kn = (k + 1u) % sides;
                const uint16_t a0 = static_cast<uint16_t>(base + k * 2u + 0u);
                const uint16_t a1 = static_cast<uint16_t>(base + k * 2u + 1u);
                const uint16_t b0 = static_cast<uint16_t>(base + kn * 2u + 0u);
                const uint16_t b1 = static_cast<uint16_t>(base + kn * 2u + 1u);
                gCitadel->domeIndices[iCount++] = a0;
                gCitadel->domeIndices[iCount++] = a1;
                gCitadel->domeIndices[iCount++] = b1;
                gCitadel->domeIndices[iCount++] = a0;
                gCitadel->domeIndices[iCount++] = b1;
                gCitadel->domeIndices[iCount++] = b0;
            }
        }

        if (vCount > 0u && iCount >= 3u) {
            dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
            dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
            dev->DrawIndexedPrimitiveUP(
                D3DPT_TRIANGLELIST, 0u, vCount, iCount / 3u,
                gCitadel->domeIndices, D3DFMT_INDEX16,
                gCitadel->domeVerts, sizeof(OverlayVertex));
        }
    }

    RestoreEngineRenderState(saved);
}

} // namespace CeSanctuaryCitadel

// -----------------------------------------------------------------------------
// Exported C ABI (called by patch 0379 in Round.cpp and DXEngine.cpp)
// -----------------------------------------------------------------------------
extern "C" bool PortCeLivingSandboxActive(void) __attribute__((weak));

static bool SanctuarySessionOpen() {
    if (!CeSanctuaryCitadel::gCitadel || !CeSanctuaryCitadel::gCitadel->active)
        return false;
    if (PortCeLivingSandboxActive && !PortCeLivingSandboxActive())
        return false;
    return true;
}

extern "C" bool PortCeSanctuaryActive(void) {
    return SanctuarySessionOpen();
}

extern "C" bool PortCeSanctuarySkipOutdoorWorld(void) {
    if (!SanctuarySessionOpen())
        return false;
    // Asked before each outdoor pass of the frame being drawn: read the camera now.
    CeSanctuaryCitadel::RefreshCinemaOccupancy();
    if (!CeSanctuaryCitadel::gCitadel->insideCinema)
        return false;
    if (PortCePhotoOn())
        return false;
    if (PortCeCmdMapOpen && PortCeCmdMapOpen())
        return false;
    return true;
}

extern "C" bool PortCeSanctuaryInterceptRound(float prevX, float prevY, float prevZ,
                                              float curX, float curY, float curZ,
                                              float *outHitX, float *outHitY, float *outHitZ) {
    if (!SanctuarySessionOpen())
        return false;
    float hx = 0.0f, hy = 0.0f, hz = 0.0f;
    if (!CeSanctuaryCitadel::InterceptIncomingSegment(
            prevX, prevY, prevZ, curX, curY, curZ,
            CeSanctuaryCitadel::gCitadel->floorZ, &hx, &hy, &hz)) {
        return false;
    }
    CeSanctuaryCitadel::RecordRipple(&CeSanctuaryCitadel::gCitadel->ripples, hx, hy, hz, 1.0f);
    if (outHitX) *outHitX = hx;
    if (outHitY) *outHitY = hy;
    if (outHitZ) *outHitZ = hz;
    return true;
}

extern "C" bool PortCeSanctuaryClampBeam(float startX, float startY, float startZ,
                                         float *ioVelX, float *ioVelY, float *ioVelZ) {
    if (!SanctuarySessionOpen())
        return false;
    float hx = 0.0f, hy = 0.0f, hz = 0.0f;
    if (!CeSanctuaryCitadel::ClampIncomingBeam(
            startX, startY, startZ, ioVelX, ioVelY, ioVelZ,
            CeSanctuaryCitadel::gCitadel->floorZ, &hx, &hy, &hz)) {
        return false;
    }
    CeSanctuaryCitadel::RecordRipple(&CeSanctuaryCitadel::gCitadel->ripples, hx, hy, hz, 1.35f);
    return true;
}

extern "C" void PortCeSanctuaryDrawWorld(void) {
    CeSanctuaryCitadel::RuntimeDrawWorld();
    CeSanctuaryCitadel::RuntimeDrawDomeFx();
}

extern "C" bool PortCeSanctuaryFire(bool release, float value) {
    return SanctuarySessionOpen() &&
           CeSanctuaryCitadel::RuntimeHandleFire(release, value);
}

extern "C" void PortCeSanctuaryRenderCinemaPlayer(int viewpoint) {
    if (!SanctuarySessionOpen() || gMirrorPass)
        return;
    CPlayer *pl = GAME.GetPlayer(viewpoint);
    CBattleEngine *be = pl ? pl->GetBattleEngine() : NULL;
    CUnit *driven = NULL;
    if (PortCePlayUnitIsDriven) {
        ListIterator<CUnit> it(&WORLD.GetUnitNB());
        for (CUnit *u = it.First(); u; u = it.Next()) {
            if (u && !u->IsDying() && !u->IsShuttingDown() && PortCePlayUnitIsDriven(u)) {
                driven = u;
                break;
            }
        }
    }
    const bool drawBe = (be && !be->IsDying() && !be->IsShuttingDown());
    if (!drawBe && !driven)
        return;
    ENGINE.SetModulate2X(true);
    if (ENGINE.GetDrawDebugStuff() & 4)
        ENGINE.DisableLighting();
    if (drawBe) {
        be->Render();
    }
    if (driven && driven != static_cast<CUnit *>(be)) {
        driven->Render();
    }
    ENGINE.SetModulate2X(true);
}

#else
#include "ce_sanctuary_citadel.h"
namespace CeSanctuaryCitadel {
void RuntimeInit(float) {}
void RuntimeShutdown() {}
void RuntimeUpdate(float, bool, float, float, float, float, float, float) {}
void RuntimeTickSim() {}
void RuntimeDrawWorld() {}
void RuntimeDrawDomeFx() {}
bool RuntimeHandleKey(unsigned, bool) { return false; }
bool RuntimeHandleFire(bool, float) { return false; }
bool RuntimeIsInsideDome(float, float, float) { return false; }
bool RuntimeIsInsideCinema(float, float, float) { return false; }
bool RuntimeCue(float, float, char *, unsigned, char *, unsigned, char *, unsigned) { return false; }
} // namespace CeSanctuaryCitadel
extern "C" {
bool PortCeSanctuaryActive(void) { return false; }
bool PortCeSanctuarySkipOutdoorWorld(void) { return false; }
bool PortCeSanctuaryInterceptRound(float, float, float, float, float, float, float *, float *, float *) { return false; }
bool PortCeSanctuaryClampBeam(float, float, float, float *, float *, float *) { return false; }
void PortCeSanctuaryDrawWorld(void) {}
bool PortCeSanctuaryFire(bool, float) { return false; }
void PortCeSanctuaryRenderCinemaPlayer(int) {}
}
#endif
