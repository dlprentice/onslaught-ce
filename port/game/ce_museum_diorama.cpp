// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_museum_diorama.cpp: Runtime GPU-buffered renderer for the Grand Museum's
// four blown-glass living tabletop battle dioramas. Loads miniature retail .msh.aya unit
// models via CeArchiveScanner (host-malloc SwapArena destroyed immediately after GPU VB/IB
// upload - zero retail heap allocations, zero leaks), uploads full mipmapped 256px textures
// plus a shared 256x256 procedural micro-relief terrain detail map, caches sculpted 32-seg
// drum pedestals, 32x64 terrains, slender 4-arch brass frames, lit curator lecterns, water/
// magma/cloud layers, and 48x16 blown-glass domes in D3DPOOL_MANAGED VBs/IBs, and renders
// per-table 3-light moods, additive crossed-quad starbursts, smoke puffs, and live hardware
// specular dome highlights.
#ifdef CE_VERSION
#include "common.h"
#include "ltshell.h"
#include "renderinfo.h"
#include "d3dx9.h"
#include "ce_asset_catalog.h"
#include "ce_archive_scanner.h"
#include "ce_museum_diorama.h"
#include "ce_museum_render_state.h"
#include "ce_museum_cinema.h"
#include "ce_museum_textures.h"
#include "ce_museum_world.h"
#include "../platform/common/portlog.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

const char *PortGameDir();
extern "C" uint64_t PortCeD3dDeviceGeneration(void) __attribute__((weak));

namespace CeMuseumDiorama {
namespace {

inline uint64_t CurrentDioramaDeviceGen() {
    return PortCeD3dDeviceGeneration ? PortCeD3dDeviceGeneration() : 0u;
}

using CeArchiveScanner::ArchiveBatch;
using CeArchiveScanner::ArchiveVertex;
using CeArchiveScanner::kMaxMeshTextures;
using CeArchiveScanner::ParsedMesh;
using CeArchiveScanner::ParseError;
using CeArchiveScanner::SwapArena;

constexpr unsigned kMaxUniqueMeshes = 44u;
constexpr unsigned kMaxSharedTextures = 112u;
constexpr unsigned kDioramaTexMaxEdge = 256u;
constexpr unsigned kDetailTexEdge = 256u;
constexpr unsigned kMaxStageVerts = 32000u;
constexpr unsigned kMaxStageIdx = 128000u;
constexpr unsigned kMaxDomeVerts = 12288u;
constexpr unsigned kMaxDomeIdx = 56000u;
constexpr unsigned kMaxVfxVerts = 8192u;
constexpr float kPi = 3.14159265359f;

struct LitStageVertex {
    float x, y, z;
    float nx, ny, nz;
    DWORD colour;
    float u, v;
};
constexpr DWORD kStageFvf = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1;

struct LitDomeVertex {
    float x, y, z;
    float nx, ny, nz;
    DWORD colour;
};
constexpr DWORD kDomeFvf = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE;

struct FlatVfxVertex {
    float x, y, z;
    DWORD colour;
};
constexpr DWORD kFlatFvf = D3DFVF_XYZ | D3DFVF_DIFFUSE;

struct SharedTexEntry {
    size_t catalogIndex;
    IDirect3DTexture9 *tex;
};

struct MeshBatchGpu {
    int sharedTexSlot; // -1 = untextured vertex colour
    uint32_t firstIndex;
    uint32_t triangleCount;
};

struct UniqueMeshGpu {
    char stem[64];
    FallbackShape fallback;
    bool isForseti;
    bool loadedRetail;
    IDirect3DVertexBuffer9 *vb;
    IDirect3DIndexBuffer9 *ib;
    uint32_t vertexCount;
    uint32_t indexCount;
    MeshBatchGpu batches[CeArchiveScanner::kMaxMeshBatches];
    uint32_t batchCount;
    float centerX, centerY, maxZ, span;
};

struct DrawRange {
    unsigned firstIndex;
    unsigned triCount;
};

struct RuntimeData {
    DioramaState state;
    IDirect3DDevice9 *device;
    uint64_t deviceGen;
    bool assetsAttempted;
    bool stageAttempted;
    float stageFloorZ;

    // Deduplicated miniature unit meshes & full-mipmapped textures (all D3DPOOL_MANAGED).
    UniqueMeshGpu meshes[kMaxUniqueMeshes];
    unsigned meshCount;
    SharedTexEntry sharedTex[kMaxSharedTextures];
    unsigned sharedTexCount;
    int unitMeshMap[kDioramaCount][kMaxUnitsPerDiorama];

    // Shared 256x256 procedural micro-relief terrain detail map (D3DTOP_MODULATE2X).
    IDirect3DTexture9 *terrainDetailTex;

    // Static GPU buffers for all 4 circular drum pedestals, lecterns, 32x64 terrains & brass frames.
    IDirect3DVertexBuffer9 *stageVb;
    IDirect3DIndexBuffer9 *stageIb;
    unsigned stageVertCount;
    unsigned stageIdxCount;
    DrawRange pedSlateRange[kDioramaCount];
    DrawRange pedBrassRange[kDioramaCount];
    DrawRange terrainRange[kDioramaCount];

    // Static GPU buffers for floor shadows, water/foam, magma ribbons, cloud decks & 48x16 glass domes.
    IDirect3DVertexBuffer9 *domeVb;
    IDirect3DIndexBuffer9 *domeIb;
    unsigned domeVertCount;
    unsigned domeIdxCount;
    DrawRange shadowRange;
    DrawRange waterRange;
    DrawRange magmaRange;
    DrawRange cloudRange;
    DrawRange domeGlassRange;

    // Dynamic host scratch buffer for tracers, 3-plane additive starbursts, smoke & lectern jewels.
    FlatVfxVertex *vfxBatch;

    // Reusable D3D9 state block for standalone ownStateBlock draws (avoids per-frame allocation).
    IDirect3DStateBlock9 *stateBlock;
    IDirect3DDevice9 *stateBlockDev;
    uint64_t stateBlockGen;
};

static RuntimeData *gDio = NULL;

RuntimeData *EnsureData() {
    if (!gDio) {
        gDio = static_cast<RuntimeData *>(std::calloc(1, sizeof(RuntimeData)));
        if (gDio) {
            Reset(&gDio->state);
            for (unsigned d = 0; d < kDioramaCount; ++d)
                for (unsigned u = 0; u < kMaxUnitsPerDiorama; ++u)
                    gDio->unitMeshMap[d][u] = -1;
            gDio->vfxBatch = static_cast<FlatVfxVertex *>(std::malloc(kMaxVfxVerts * sizeof(FlatVfxVertex)));
        }
    }
    return gDio;
}

inline float Clamp01(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

inline unsigned ClampByteU(unsigned v) {
    return v > 255u ? 255u : v;
}

// Clamp AFTER float->int conversion so x87 FE_TONEAREST (255.0f + 0.5f -> 256) never wraps to 0!
inline unsigned FloatToByte(float v) {
    if (v <= 0.0f) return 0u;
    if (v >= 255.0f) return 255u;
    const int iv = static_cast<int>(v + 0.5f);
    return iv <= 0 ? 0u : (iv >= 255 ? 255u : static_cast<unsigned>(iv));
}

inline DWORD PackArgb(unsigned a, unsigned r, unsigned g, unsigned b) {
    return (ClampByteU(a) << 24) | (ClampByteU(r) << 16) | (ClampByteU(g) << 8) | ClampByteU(b);
}

inline DWORD ScaleRgb(DWORD col, float k) {
    const unsigned a = (col >> 24) & 255u;
    const unsigned r = FloatToByte(float((col >> 16) & 255u) * k);
    const unsigned g = FloatToByte(float((col >> 8) & 255u) * k);
    const unsigned b = FloatToByte(float(col & 255u) * k);
    return PackArgb(a, r, g, b);
}

inline DWORD LerpArgb(DWORD c0, DWORD c1, float t) {
    t = Clamp01(t);
    const float a0 = float((c0 >> 24) & 255u), a1 = float((c1 >> 24) & 255u);
    const float r0 = float((c0 >> 16) & 255u), r1 = float((c1 >> 16) & 255u);
    const float g0 = float((c0 >> 8) & 255u),  g1 = float((c1 >> 8) & 255u);
    const float b0 = float(c0 & 255u),         b1 = float(c1 & 255u);
    return PackArgb(FloatToByte(a0 + (a1 - a0) * t),
                    FloatToByte(r0 + (r1 - r0) * t),
                    FloatToByte(g0 + (g1 - g0) * t),
                    FloatToByte(b0 + (b1 - b0) * t));
}

bool UploadManagedVb(IDirect3DDevice9 *dev, const void *data, unsigned bytes,
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

bool UploadManagedIb(IDirect3DDevice9 *dev, const void *data, unsigned bytes,
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

// Generate ONE shared 256x256 tileable mipmapped procedural micro-relief rock/sand detail map
// centred around 0x808080 for crisp D3DTOP_MODULATE2X terrain shading.
IDirect3DTexture9 *CreateTerrainDetailTexture(IDirect3DDevice9 *dev) {
    if (!dev) return NULL;
    const unsigned edge = kDetailTexEdge;
    const size_t tgaBytes = 18u + static_cast<size_t>(edge) * static_cast<size_t>(edge) * 4u;
    unsigned char *tga = static_cast<unsigned char *>(std::calloc(1, tgaBytes));
    if (!tga) return NULL;

    tga[2]  = 2u; // uncompressed true-color TGA
    tga[12] = static_cast<unsigned char>(edge & 0xffu);
    tga[13] = static_cast<unsigned char>((edge >> 8) & 0xffu);
    tga[14] = static_cast<unsigned char>(edge & 0xffu);
    tga[15] = static_cast<unsigned char>((edge >> 8) & 0xffu);
    tga[16] = 32u;
    tga[17] = 0x28u;

    auto hash2 = [](unsigned x, unsigned y, unsigned seed) -> float {
        uint32_t h = (x & 255u) * 374761393u + (y & 255u) * 668265263u + seed * 1442695041u;
        h = (h ^ (h >> 13)) * 1274126177u;
        h ^= (h >> 16);
        return float(h & 0xffffu) / 65535.0f;
    };
    auto tileNoise = [&](unsigned px, unsigned py, unsigned period, unsigned seed) -> float {
        const float fx = float(px) * float(period) / float(edge);
        const float fy = float(py) * float(period) / float(edge);
        // The lattice cell and the position inside it come from the same floor.
        // A bare float-to-int conversion rounds to nearest in the game's x87
        // build, so past each half cell it named the NEXT cell while the fraction
        // still counted from this one: a hard seam down the middle of every cell.
        const float flx = std::floor(fx);
        const float fly = std::floor(fy);
        const unsigned x0 = static_cast<unsigned>(static_cast<int>(flx)) % period;
        const unsigned y0 = static_cast<unsigned>(static_cast<int>(fly)) % period;
        const unsigned x1 = (x0 + 1u) % period;
        const unsigned y1 = (y0 + 1u) % period;
        const float tx = fx - flx;
        const float ty = fy - fly;
        const float sx = tx * tx * (3.0f - 2.0f * tx);
        const float sy = ty * ty * (3.0f - 2.0f * ty);
        const float a = hash2(x0, y0, seed);
        const float b = hash2(x1, y0, seed);
        const float c = hash2(x0, y1, seed);
        const float d = hash2(x1, y1, seed);
        const float ab = a + (b - a) * sx;
        const float cd = c + (d - c) * sx;
        return ab + (cd - ab) * sy;
    };

    unsigned char *pix = tga + 18u;
    for (unsigned y = 0; y < edge; ++y) {
        for (unsigned x = 0; x < edge; ++x) {
            const float n0 = tileNoise(x, y, 8u, 11u);
            const float n1 = tileNoise(x, y, 16u, 29u);
            const float n2 = tileNoise(x, y, 32u, 53u);
            const float n3 = tileNoise(x, y, 64u, 97u);
            const float fine = hash2(x, y, 131u);
            // Subtle geological mineral grain + micro-relief directional bevel around neutral 0.5.
            const float strata = 0.5f + 0.5f * std::sin((float(y) * 6.0f + n0 * 14.0f) * (2.0f * kPi / float(edge)));
            const float v = (n0 * 0.24f + n1 * 0.28f + n2 * 0.24f + n3 * 0.14f + fine * 0.10f) * 0.85f
                          + strata * 0.15f;
            // Centred at 128 +/- 42 so D3DTOP_MODULATE2X keeps exact diffuse value while adding crisp tactile micro-relief.
            const float delta = (v - 0.5f) * 84.0f;
            int r = static_cast<int>(128.0f + delta * 1.03f + 0.5f);
            int g = static_cast<int>(128.0f + delta * 1.00f + 0.5f);
            int b = static_cast<int>(128.0f + delta * 0.96f + 0.5f);
            if (r < 70) r = 70; else if (r > 186) r = 186;
            if (g < 70) g = 70; else if (g > 186) g = 186;
            if (b < 70) b = 70; else if (b > 186) b = 186;
            const size_t off = (static_cast<size_t>(y) * edge + x) * 4u;
            pix[off + 0u] = static_cast<unsigned char>(b);
            pix[off + 1u] = static_cast<unsigned char>(g);
            pix[off + 2u] = static_cast<unsigned char>(r);
            pix[off + 3u] = 255u;
        }
    }

    IDirect3DTexture9 *tex = NULL;
    const HRESULT hr = D3DXCreateTextureFromFileInMemoryEx(
        dev, tga, static_cast<UINT>(tgaBytes),
        static_cast<UINT>(edge), static_cast<UINT>(edge),
        0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
        D3DX_FILTER_TRIANGLE, D3DX_FILTER_BOX, 0, NULL, NULL, &tex);
    std::free(tga);
    if (FAILED(hr) || !tex) {
        if (tex) tex->Release();
        return NULL;
    }
    return tex;
}

void ReleaseDioramaStateBlock(RuntimeData *d) {
    if (!d) return;
    if (d->stateBlock) { d->stateBlock->Release(); d->stateBlock = NULL; }
    d->stateBlockDev = NULL;
    d->stateBlockGen = 0u;
}

void ReleaseStageBuffers(RuntimeData *d) {
    if (!d) return;
    if (d->stageVb) { d->stageVb->Release(); d->stageVb = NULL; }
    if (d->stageIb) { d->stageIb->Release(); d->stageIb = NULL; }
    if (d->domeVb)  { d->domeVb->Release();  d->domeVb = NULL; }
    if (d->domeIb)  { d->domeIb->Release();  d->domeIb = NULL; }
    if (d->terrainDetailTex) { d->terrainDetailTex->Release(); d->terrainDetailTex = NULL; }
    ReleaseDioramaStateBlock(d);
    d->stageVertCount = 0;
    d->stageIdxCount = 0;
    d->domeVertCount = 0;
    d->domeIdxCount = 0;
    std::memset(d->pedSlateRange, 0, sizeof(d->pedSlateRange));
    std::memset(d->pedBrassRange, 0, sizeof(d->pedBrassRange));
    std::memset(d->terrainRange, 0, sizeof(d->terrainRange));
    d->shadowRange = {};
    d->waterRange = {};
    d->magmaRange = {};
    d->cloudRange = {};
    d->domeGlassRange = {};
    d->stageAttempted = false;
}

void ReleaseUnitAssets(RuntimeData *d) {
    if (!d) return;
    for (unsigned i = 0; i < d->meshCount && i < kMaxUniqueMeshes; ++i) {
        if (d->meshes[i].vb) { d->meshes[i].vb->Release(); d->meshes[i].vb = NULL; }
        if (d->meshes[i].ib) { d->meshes[i].ib->Release(); d->meshes[i].ib = NULL; }
    }
    d->meshCount = 0;
    for (unsigned i = 0; i < d->sharedTexCount && i < kMaxSharedTextures; ++i) {
        if (d->sharedTex[i].tex) { d->sharedTex[i].tex->Release(); d->sharedTex[i].tex = NULL; }
    }
    d->sharedTexCount = 0;
    for (unsigned di = 0; di < kDioramaCount; ++di)
        for (unsigned ui = 0; ui < kMaxUnitsPerDiorama; ++ui)
            d->unitMeshMap[di][ui] = -1;
    d->assetsAttempted = false;
}

struct DioTexUploadCtx {
    IDirect3DDevice9 *device;
    IDirect3DTexture9 **outTex;
};

int DioTexUploadCb(const unsigned char *dds, size_t bytes,
                   const CeAssetTextureHeader *h, void *context) {
    DioTexUploadCtx *ctx = static_cast<DioTexUploadCtx *>(context);
    if (!ctx || !ctx->device || !ctx->outTex || !dds || !bytes || !h) return 0;
    unsigned w = h->width, ht = h->height;
    const unsigned longest = w > ht ? w : ht;
    if (longest > kDioramaTexMaxEdge) {
        const float sc = float(kDioramaTexMaxEdge) / float(longest);
        w = static_cast<unsigned>(float(w) * sc + 0.5f);
        ht = static_cast<unsigned>(float(ht) * sc + 0.5f);
        if (!w) w = 1u;
        if (!ht) ht = 1u;
    }
    IDirect3DTexture9 *tex = NULL;
    // Full mipmap chain (MipLevels = 0, D3DX_FILTER_BOX) cures miniature minification shimmer!
    const HRESULT hr = D3DXCreateTextureFromFileInMemoryEx(
        ctx->device, dds, static_cast<UINT>(bytes),
        static_cast<UINT>(w), static_cast<UINT>(ht),
        0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
        D3DX_FILTER_TRIANGLE, D3DX_FILTER_BOX, 0, NULL, NULL, &tex);
    if (FAILED(hr) || !tex) {
        if (tex) tex->Release();
        return 0;
    }
    *ctx->outTex = tex;
    return 1;
}

int GetOrLoadSharedTexture(RuntimeData *d, IDirect3DDevice9 *dev,
                           const CeAssetCatalog *cat, const char *texName) {
    if (!d || !dev || !cat || !texName || !*texName) return -1;
    const size_t catIdx = PortCeAssetCatalogFindTexture(cat, texName);
    if (catIdx == SIZE_MAX) return -1;
    for (unsigned i = 0; i < d->sharedTexCount; ++i) {
        if (d->sharedTex[i].catalogIndex == catIdx) return static_cast<int>(i);
    }
    if (d->sharedTexCount >= kMaxSharedTextures) return -1;
    IDirect3DTexture9 *tex = NULL;
    DioTexUploadCtx ctx = {dev, &tex};
    if (!PortCeAssetCatalogReadTexture(cat, catIdx, DioTexUploadCb, &ctx) || !tex)
        return -1;
    const unsigned slot = d->sharedTexCount++;
    d->sharedTex[slot].catalogIndex = catIdx;
    d->sharedTex[slot].tex = tex;
    return static_cast<int>(slot);
}

// Build a clean procedural miniature fallback mesh in native Z-down coordinates so dioramas
// remain lively and legible even when retail mesh files are unavailable.
void BuildFallbackUnitMesh(IDirect3DDevice9 *dev, UniqueMeshGpu *out) {
    if (!dev || !out) return;
    ArchiveVertex verts[96];
    uint16_t indices[192];
    unsigned vAt = 0, iAt = 0;

    auto pushBlock = [&](float x0, float y0, float z0, float x1, float y1, float z1, DWORD col) {
        if (vAt + 20u > 96u || iAt + 30u > 192u) return;
        auto face = [&](float ax, float ay, float az, float bx, float by, float bz,
                        float cx, float cy, float cz, float dx, float dy, float dz,
                        float nx, float ny, float nz) {
            const uint16_t b = static_cast<uint16_t>(vAt);
            verts[vAt++] = {ax, ay, az, nx, ny, nz, col, 0.0f, 0.0f};
            verts[vAt++] = {bx, by, bz, nx, ny, nz, col, 1.0f, 0.0f};
            verts[vAt++] = {cx, cy, cz, nx, ny, nz, col, 1.0f, 1.0f};
            verts[vAt++] = {dx, dy, dz, nx, ny, nz, col, 0.0f, 1.0f};
            indices[iAt++] = b + 0u; indices[iAt++] = b + 2u; indices[iAt++] = b + 1u;
            indices[iAt++] = b + 0u; indices[iAt++] = b + 3u; indices[iAt++] = b + 2u;
        };
        face(x0,y0,z1, x1,y0,z1, x1,y1,z1, x0,y1,z1,  0,0,-1);
        face(x0,y0,z0, x1,y0,z0, x1,y0,z1, x0,y0,z1,  0,-1,0);
        face(x1,y0,z0, x1,y1,z0, x1,y1,z1, x1,y0,z1,  1,0,0);
        face(x1,y1,z0, x0,y1,z0, x0,y1,z1, x1,y1,z1,  0,1,0);
        face(x0,y1,z0, x0,y0,z0, x0,y0,z1, x0,y1,z1, -1,0,0);
    };

    const DWORD hull = out->isForseti ? 0xff4a88c4u : 0xffb84830u;
    const DWORD trim = out->isForseti ? 0xffa8daf8u : 0xffe89c42u;

    switch (out->fallback) {
    case kShapeJet:
        pushBlock(-0.18f, -0.55f, 0.0f,  0.18f,  0.55f, -0.22f, hull);
        pushBlock(-0.62f, -0.20f, -0.04f, 0.62f, 0.14f, -0.14f, trim);
        pushBlock(-0.10f, -0.48f, -0.22f, 0.10f, -0.22f, -0.36f, trim);
        break;
    case kShapeShip:
        pushBlock(-0.26f, -0.68f, 0.0f,  0.26f,  0.68f, -0.24f, hull);
        pushBlock(-0.16f, -0.18f, -0.24f, 0.16f, 0.24f, -0.52f, trim);
        break;
    case kShapeWalker:
        pushBlock(-0.26f, -0.22f, 0.0f,  -0.08f, 0.22f, -0.45f, hull);
        pushBlock( 0.08f, -0.22f, 0.0f,   0.26f, 0.22f, -0.45f, hull);
        pushBlock(-0.32f, -0.28f, -0.42f, 0.32f, 0.32f, -0.78f, trim);
        break;
    case kShapeFortress:
        pushBlock(-0.52f, -0.52f, 0.0f,  0.52f,  0.52f, -0.38f, hull);
        pushBlock(-0.28f, -0.28f, -0.38f, 0.28f, 0.28f, -0.72f, trim);
        break;
    case kShapeSubmarine:
        pushBlock(-0.20f, -0.72f, 0.0f,  0.20f,  0.72f, -0.20f, 0xff3b4248u);
        pushBlock(-0.09f, -0.14f, -0.20f, 0.09f, 0.20f, -0.44f, trim);
        pushBlock(-0.32f, -0.58f, -0.06f, 0.32f, -0.42f, -0.12f, hull);
        break;
    case kShapeTank:
    default:
        pushBlock(-0.36f, -0.48f, 0.0f,  0.36f,  0.48f, -0.26f, hull);
        pushBlock(-0.20f, -0.18f, -0.26f, 0.20f, 0.20f, -0.46f, trim);
        pushBlock(-0.05f,  0.18f, -0.34f, 0.05f, 0.56f, -0.42f, trim);
        break;
    }

    if (UploadManagedVb(dev, verts, vAt * sizeof(ArchiveVertex),
                        D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1, &out->vb) &&
        UploadManagedIb(dev, indices, iAt * sizeof(uint16_t), D3DFMT_INDEX16, &out->ib)) {
        out->vertexCount = vAt;
        out->indexCount = iAt;
        out->batches[0] = {-1, 0u, iAt / 3u};
        out->batchCount = 1u;
        out->centerX = 0.0f;
        out->centerY = 0.0f;
        out->maxZ = 0.0f;
        out->span = 1.25f;
    } else {
        if (out->vb) { out->vb->Release(); out->vb = NULL; }
        if (out->ib) { out->ib->Release(); out->ib = NULL; }
    }
}

// Find retail model relative path matching "data/resources/meshes/m_<stem>.msh.aya" case-insensitively.
const char *FindRetailModelPath(const CeAssetCatalog *cat, const char *stem) {
    if (!cat || !stem || !*stem) return NULL;
    char wantName[128];
    std::snprintf(wantName, sizeof(wantName), "m_%s.msh.aya", stem);
    const size_t total = PortCeAssetCatalogCount(cat);
    for (size_t i = 0; i < total; ++i) {
        if (PortCeAssetCatalogKind(cat, i) != CE_ASSET_MODEL) continue;
        const char *rel = PortCeAssetCatalogPath(cat, i);
        if (!rel) continue;
        const char *base = rel;
        for (const char *p = rel; *p; ++p)
            if (*p == '/' || *p == '\\') base = p + 1;
        if (CeArchiveScanner::EqualsFoldAscii(base, wantName)) return rel;
    }
    return NULL;
}

void EnsureUnitAssetsLoaded(RuntimeData *d, IDirect3DDevice9 *dev) {
    if (!d || !dev) return;
    const uint64_t gen = CurrentDioramaDeviceGen();
    if (d->assetsAttempted && d->device == dev && d->deviceGen == gen) return;
    if (d->device && (d->device != dev || d->deviceGen != gen)) {
        ReleaseUnitAssets(d);
        ReleaseStageBuffers(d);
    }
    d->device = dev;
    d->deviceGen = gen;
    d->assetsAttempted = true;

    // 1. Register deduplicated unique mesh stems across all 4 dioramas.
    for (unsigned di = 0; di < kDioramaCount; ++di) {
        const DioramaSpec &ds = kDioramas[di];
        for (unsigned ui = 0; ui < ds.unitCount && ui < kMaxUnitsPerDiorama; ++ui) {
            const UnitSpec &u = ds.units[ui];
            int found = -1;
            for (unsigned m = 0; m < d->meshCount; ++m) {
                if (CeArchiveScanner::EqualsFoldAscii(d->meshes[m].stem, u.meshStem)) {
                    found = static_cast<int>(m);
                    break;
                }
            }
            if (found < 0 && d->meshCount < kMaxUniqueMeshes) {
                found = static_cast<int>(d->meshCount++);
                UniqueMeshGpu &um = d->meshes[found];
                std::memset(&um, 0, sizeof(um));
                std::snprintf(um.stem, sizeof(um.stem), "%s", u.meshStem);
                um.fallback = u.fallback;
                um.isForseti = u.isForseti;
            }
            d->unitMeshMap[di][ui] = found;
        }
    }

    // 2. Open CeAssetCatalog and one temporary SwapArena (destroyed right after GPU upload!).
    const char *gameDir = PortGameDir();
    CeAssetCatalog *cat = NULL;
    SwapArena arena = {};
    if (gameDir && *gameDir) {
        char err[192] = {};
        cat = PortCeAssetCatalogOpenKinds(gameDir, CE_ASSET_MODEL | CE_ASSET_TEXTURE, err, sizeof(err));
        if (cat) arena.Init(false);
    }

    unsigned retailOk = 0u, fallbackOk = 0u;
    for (unsigned m = 0; m < d->meshCount; ++m) {
        UniqueMeshGpu &um = d->meshes[m];
        bool ok = false;
        if (cat && arena.base && gameDir) {
            const char *rel = FindRetailModelPath(cat, um.stem);
            if (rel) {
                char fullPath[1024];
                const size_t gl = std::strlen(gameDir);
                const bool slash = gl > 0 && (gameDir[gl - 1] == '/' || gameDir[gl - 1] == '\\');
                std::snprintf(fullPath, sizeof(fullPath), slash ? "%s%s" : "%s/%s", gameDir, rel);
                arena.Reset();
                unsigned char *ayaBytes = NULL;
                size_t ayaSize = 0;
                if (CeArchiveScanner::ReadWholeFileIntoArena(fullPath, &arena, &ayaBytes, &ayaSize, 2u * 1024u * 1024u)) {
                    ParsedMesh pm = {};
                    const ParseError pe = CeArchiveScanner::DecompressAndParseAyaMesh(ayaBytes, ayaSize, &arena, &pm);
                    if (pe == CeArchiveScanner::kParseOk && pm.bounds.valid &&
                        pm.vertexCount > 0u && pm.indexCount > 0u && pm.batchCount > 0u) {
                        if (UploadManagedVb(dev, pm.vertices, pm.vertexCount * sizeof(ArchiveVertex),
                                            D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1, &um.vb) &&
                            UploadManagedIb(dev, pm.indices, pm.indexCount * sizeof(uint16_t), D3DFMT_INDEX16, &um.ib)) {
                            int texSlots[kMaxMeshTextures];
                            for (unsigned t = 0; t < kMaxMeshTextures; ++t) texSlots[t] = -1;
                            for (uint32_t t = 0; t < pm.textureCount && t < kMaxMeshTextures; ++t)
                                texSlots[t] = GetOrLoadSharedTexture(d, dev, cat, pm.textureNames[t]);

                            um.vertexCount = pm.vertexCount;
                            um.indexCount = pm.indexCount;
                            um.batchCount = pm.batchCount < CeArchiveScanner::kMaxMeshBatches
                                ? pm.batchCount : CeArchiveScanner::kMaxMeshBatches;
                            for (uint32_t bi = 0; bi < um.batchCount; ++bi) {
                                const ArchiveBatch &b = pm.batches[bi];
                                um.batches[bi].firstIndex = b.firstIndex;
                                um.batches[bi].triangleCount = b.triangleCount;
                                um.batches[bi].sharedTexSlot =
                                    (b.textureIndex >= 0 && static_cast<uint32_t>(b.textureIndex) < kMaxMeshTextures)
                                        ? texSlots[b.textureIndex] : -1;
                            }
                            um.centerX = pm.bounds.centerX;
                            um.centerY = pm.bounds.centerY;
                            um.maxZ = pm.bounds.maxZ;
                            um.span = pm.bounds.span;
                            um.loadedRetail = true;
                            ok = true;
                            ++retailOk;
                        } else {
                            if (um.vb) { um.vb->Release(); um.vb = NULL; }
                            if (um.ib) { um.ib->Release(); um.ib = NULL; }
                        }
                    }
                }
            }
        }
        if (!ok) {
            BuildFallbackUnitMesh(dev, &um);
            if (um.vb) ++fallbackOk;
        }
    }

    arena.Shutdown();
    if (cat) PortCeAssetCatalogClose(cat);

    PortSessionLog(PORT_SESSION_GAME, false,
                   "museum dioramas: prepared %u retail + %u fallback miniature meshes (%u mipmapped textures) across 4 tables",
                   retailOk, fallbackOk, d->sharedTexCount);
}

// =============================================================================
// Bounds-Checked Static GPU Builders: 32-Seg Drums, Lecterns, 32x64 Terrains,
// Slender 4-Arch Brass Frames, Water/Magma/Cloud Layers & 48x16 Blown Glass Domes
// =============================================================================
struct StageBuilder {
    LitStageVertex *verts;
    uint32_t *indices;
    unsigned vCap, iCap, vUsed, iUsed;

    bool HasSpace(unsigned addV, unsigned addI) const {
        return (vUsed + addV <= vCap) && (iUsed + addI <= iCap);
    }
    uint32_t PushV(float wx, float wy, float wz, float nx, float ny, float nz,
                   DWORD col, float u, float v) {
        if (vUsed >= vCap) return 0u;
        const uint32_t idx = vUsed++;
        verts[idx] = {wx, wy, wz, nx, ny, nz, col, u, v};
        return idx;
    }
    void PushTri(uint32_t a, uint32_t b, uint32_t c) {
        if (iUsed + 3u > iCap || a >= vUsed || b >= vUsed || c >= vUsed) return;
        indices[iUsed++] = a;
        indices[iUsed++] = b;
        indices[iUsed++] = c;
    }
    void PushQuadUv(float ax, float ay, float az, float au, float av,
                    float bx, float by, float bz, float bu, float bv,
                    float cx, float cy, float cz, float cu, float cv,
                    float dx, float dy, float dz, float du, float dv,
                    float nx, float ny, float nz, DWORD colBot, DWORD colTop) {
        if (!HasSpace(4u, 6u)) return;
        const uint32_t i0 = PushV(ax, ay, az, nx, ny, nz, colBot, au, av);
        const uint32_t i1 = PushV(bx, by, bz, nx, ny, nz, colBot, bu, bv);
        const uint32_t i2 = PushV(cx, cy, cz, nx, ny, nz, colTop, cu, cv);
        const uint32_t i3 = PushV(dx, dy, dz, nx, ny, nz, colTop, du, dv);
        PushTri(i0, i1, i2);
        PushTri(i0, i2, i3);
    }
    // Smooth circular sculpted drum tier with true smooth radial normals (no 8-sided polygonal kinks!).
    void DrumTier(float wcx, float wcy, float floorZ, float rBot, float rTop,
                  float z0, float z1, unsigned segs, DWORD topCol, DWORD sideCol,
                  bool drawTop, float uvScale) {
        if (segs < 8u || segs > 64u) return;
        const float wz0 = floorZ - z0;
        const float wz1 = floorZ - z1;
        const float dr = rBot - rTop;
        const float dz = z1 - z0;
        const float sl = std::sqrt(dr * dr + dz * dz);
        const float nrScale = sl > 1e-5f ? (dz / sl) : 1.0f;
        const float nzSlope = sl > 1e-5f ? (-dr / sl) : 0.0f;

        if (drawTop && HasSpace(segs + 1u, segs * 3u)) {
            const uint32_t ctr = PushV(wcx, wcy, wz1, 0.0f, 0.0f, -1.0f, topCol, 0.0f, 0.0f);
            const uint32_t ringBase = vUsed;
            for (unsigned i = 0; i < segs; ++i) {
                const float a = float(i) * (2.0f * kPi / float(segs));
                const float ca = std::cos(a), sa = std::sin(a);
                PushV(wcx + ca * rTop, wcy + sa * rTop, wz1, 0.0f, 0.0f, -1.0f,
                      topCol, ca * rTop * uvScale, sa * rTop * uvScale);
            }
            for (unsigned i = 0; i < segs; ++i) {
                PushTri(ctr, ringBase + i, ringBase + ((i + 1u) % segs));
            }
        }
        if (HasSpace((segs + 1u) * 2u, segs * 6u)) {
            const uint32_t base = vUsed;
            const float vH = dz * uvScale;
            for (unsigned i = 0; i <= segs; ++i) {
                const float a = float(i % segs) * (2.0f * kPi / float(segs));
                const float ca = std::cos(a), sa = std::sin(a);
                const float u = (float(i) / float(segs)) * (2.0f * kPi * rTop * uvScale);
                const float nx = ca * nrScale, ny = sa * nrScale;
                PushV(wcx + ca * rBot, wcy + sa * rBot, wz0, nx, ny, nzSlope, sideCol, u, 0.0f);
                PushV(wcx + ca * rTop, wcy + sa * rTop, wz1, nx, ny, nzSlope, topCol,  u, vH);
            }
            for (unsigned i = 0; i < segs; ++i) {
                const uint32_t i0 = base + i * 2u;
                const uint32_t i1 = i0 + 2u;
                PushTri(i0, i1, i1 + 1u);
                PushTri(i0, i1 + 1u, i0 + 1u);
            }
        }
    }
    // Smooth annular flat collar ring (for brass bezel ledges & stepped pedestal shoulders).
    void AnnulusRing(float wcx, float wcy, float wz, float rIn, float rOut,
                     unsigned segs, DWORD colIn, DWORD colOut, float uvScale) {
        if (segs < 8u || segs > 64u) return;
        if (!HasSpace((segs + 1u) * 2u, segs * 6u)) return;
        const uint32_t base = vUsed;
        for (unsigned i = 0; i <= segs; ++i) {
            const float a = float(i % segs) * (2.0f * kPi / float(segs));
            const float ca = std::cos(a), sa = std::sin(a);
            PushV(wcx + ca * rIn,  wcy + sa * rIn,  wz, 0.0f, 0.0f, -1.0f, colIn,  ca * rIn * uvScale,  sa * rIn * uvScale);
            PushV(wcx + ca * rOut, wcy + sa * rOut, wz, 0.0f, 0.0f, -1.0f, colOut, ca * rOut * uvScale, sa * rOut * uvScale);
        }
        for (unsigned i = 0; i < segs; ++i) {
            const uint32_t i0 = base + i * 2u;
            const uint32_t i1 = i0 + 2u;
            PushTri(i0, i0 + 1u, i1 + 1u);
            PushTri(i0, i1 + 1u, i1);
        }
    }
    // 3D oriented box helper with true outward face normals (used for the 3D-lit curator lectern).
    void OrientedBox(float cx, float cy, float cz,
                     float fx, float fy, float fz,
                     float rx, float ry, float rz,
                     float ux, float uy, float uz,
                     float hf, float hr, float hu,
                     DWORD colTop, DWORD colSide, float uvScale) {
        if (!HasSpace(24u, 36u)) return;
        auto pt = [&](float sf, float sr, float su, float *ox, float *oy, float *oz) {
            *ox = cx + fx * sf * hf + rx * sr * hr + ux * su * hu;
            *oy = cy + fy * sf * hf + ry * sr * hr + uy * su * hu;
            *oz = cz + fz * sf * hf + rz * sr * hr + uz * su * hu;
        };
        auto emitFace = [&](float n0, float n1, float n2,
                            float af, float ar, float au,
                            float bf, float br, float bu,
                            float cf, float cr, float cu,
                            float df, float dr, float du,
                            DWORD cBot, DWORD cTop, float wu, float wv) {
            float ax, ay, az, bx, by, bz, cpx, cpy, cpz, dx, dy, dz;
            pt(af, ar, au, &ax, &ay, &az);
            pt(bf, br, bu, &bx, &by, &bz);
            pt(cf, cr, cu, &cpx, &cpy, &cpz);
            pt(df, dr, du, &dx, &dy, &dz);
            PushQuadUv(ax, ay, az, 0.0f, 0.0f,
                       bx, by, bz, wu * uvScale, 0.0f,
                       cpx, cpy, cpz, wu * uvScale, wv * uvScale,
                       dx, dy, dz, 0.0f, wv * uvScale,
                       n0, n1, n2, cBot, cTop);
        };
        // Top (+u), Front (+f), Back (-f), Right (+r), Left (-r)
        emitFace( ux,  uy,  uz, -1,-1, 1,  1,-1, 1,  1, 1, 1, -1, 1, 1, colTop,  colTop,  2.0f * hf, 2.0f * hr);
        emitFace( fx,  fy,  fz,  1,-1,-1,  1, 1,-1,  1, 1, 1,  1,-1, 1, colSide, colTop,  2.0f * hr, 2.0f * hu);
        emitFace(-fx, -fy, -fz, -1, 1,-1, -1,-1,-1, -1,-1, 1, -1, 1, 1, colSide, colTop,  2.0f * hr, 2.0f * hu);
        emitFace( rx,  ry,  rz, -1, 1,-1,  1, 1,-1,  1, 1, 1, -1, 1, 1, colSide, colTop,  2.0f * hf, 2.0f * hu);
        emitFace(-rx, -ry, -rz,  1,-1,-1, -1,-1,-1, -1,-1, 1,  1,-1, 1, colSide, colTop,  2.0f * hf, 2.0f * hu);
    }
};

struct DomeBuilder {
    LitDomeVertex *verts;
    uint16_t *indices;
    unsigned vCap, iCap, vUsed, iUsed;

    bool HasSpace(unsigned addV, unsigned addI) const {
        return (vUsed + addV <= vCap) && (vUsed + addV <= 65535u) && (iUsed + addI <= iCap);
    }
    uint16_t PushV(float wx, float wy, float wz, float nx, float ny, float nz, DWORD col) {
        if (vUsed >= vCap || vUsed >= 65535u) return 0u;
        const uint16_t idx = static_cast<uint16_t>(vUsed++);
        verts[idx] = {wx, wy, wz, nx, ny, nz, col};
        return idx;
    }
    void PushTri(uint16_t a, uint16_t b, uint16_t c) {
        if (iUsed + 3u > iCap || a >= vUsed || b >= vUsed || c >= vUsed) return;
        indices[iUsed++] = a;
        indices[iUsed++] = b;
        indices[iUsed++] = c;
    }
};

// Terrain vertex colour with baked crevice ambient occlusion & biome stratification.
DWORD TerrainColour(Theme theme, float rx, float ry, float h, float slopeMag, float ao) {
    DWORD base = 0xff606870u;
    switch (theme) {
    case kThemeCoastal: {
        if (h <= 0.005f) {
            // Shallow sandy seabed transitioning to deep teal bay floor beneath translucent water.
            const float shore = rx * 0.72f + ry * 0.42f + 0.16f * std::sin(ry * 2.3f);
            const float depth = Clamp01((-0.36f - shore) / 1.10f);
            base = LerpArgb(0xff2c8ca8u, 0xff114668u, depth);
        } else if (h < 0.065f) {
            base = LerpArgb(0xffdcc792u, 0xffc6ad72u, h / 0.065f); // sunlit beachhead sand
        } else if (h < 0.28f) {
            const float t = (h - 0.065f) / 0.215f;
            const DWORD meadow = LerpArgb(0xff3e8446u, 0xff2e6b3eu, t);
            const DWORD cliff  = 0xff6d685eu; // limestone bluff on steep cuts
            base = LerpArgb(meadow, cliff, Clamp01((slopeMag - 0.32f) * 2.4f));
        } else {
            const float t = Clamp01((h - 0.28f) / 0.22f);
            base = LerpArgb(0xff5a6654u, 0xff7e7a70u, t); // fortified limestone crest
        }
        break;
    }
    case kThemeVolcanic: {
        const float lavaD = VolcanicLavaDistance(rx, ry);
        if (lavaD < 0.085f) {
            // Incandescent lava bed beneath the additive magma ribbon.
            base = LerpArgb(0xffffd84au, 0xffff5c14u, lavaD / 0.085f);
            return base;
        }
        if (lavaD < 0.17f) {
            const float f = (lavaD - 0.085f) / 0.085f;
            base = LerpArgb(0xffc83c14u, 0xff3b2b2eu, f); // glowing scorched basalt bank
        } else {
            const float t = Clamp01(h / 0.50f);
            const DWORD darkBasalt = LerpArgb(0xff2a262bu, 0xff4e4548u, t);
            const DWORD ashCrest   = 0xff6a5c58u;
            base = LerpArgb(darkBasalt, ashCrest, Clamp01((h - 0.34f) * 5.0f));
        }
        break;
    }
    case kThemeSkycarrier: {
        const float t = Clamp01(h / 0.045f);
        base = LerpArgb(0xff12365eu, 0xff266294u, t); // layered sapphire ocean swell
        break;
    }
    case kThemeNautilus: {
        if (h <= 0.005f) {
            // Luminous equatorial turquoise coral lagoon floor.
            const float r = std::sqrt(rx * rx + ry * ry);
            const float lagoon = Clamp01(r / kTerrainRadius);
            base = LerpArgb(0xff147692u, 0xff0d4d72u, lagoon * 0.7f);
        } else if (h < 0.060f) {
            base = LerpArgb(0xffe6d4a4u, 0xffccb882u, h / 0.060f); // warm coral cove sand
        } else if (h < 0.24f) {
            const float t = (h - 0.060f) / 0.18f;
            const DWORD jungle = LerpArgb(0xff389652u, 0xff2c7c48u, t);
            const DWORD reefRock = 0xff686058u; // equatorial volcanic rock face
            base = LerpArgb(jungle, reefRock, Clamp01((slopeMag - 0.34f) * 2.1f));
        } else {
            const float t = Clamp01((h - 0.24f) / 0.26f);
            base = LerpArgb(0xff428452u, 0xff786e60u, t * 0.72f); // lush volcanic cliff peak
        }
        break;
    }
    default:
        break;
    }
    return ScaleRgb(base, ao);
}

void EmitDioramaTerrainAndStrata(StageBuilder &sb, Theme theme, float wcx, float wcy, float floorZ) {
    // High-resolution polar disk grid: 32 concentric rings x 64 angular segments (4,096 tris/table!).
    constexpr unsigned kRings = 32u;
    constexpr unsigned kSegs = 64u;
    const float topBase = floorZ - kTabletopZ;
    if (!sb.HasSpace(1u + kRings * kSegs + (kSegs + 1u) * 2u, (kSegs + (kRings - 1u) * kSegs * 2u + kSegs * 2u) * 3u))
        return;

    const uint32_t base = sb.vUsed;
    const float uvTile = 3.2f / kTerrainRadius;
    auto evalPoint = [&](float rx, float ry, float *outH, float *nx, float *ny, float *nz, float *slopeMag, float *ao) {
        const float h = TerrainHeight(theme, rx, ry);
        const float eps = 0.045f;
        const float hxp = TerrainHeight(theme, rx + eps, ry);
        const float hxm = TerrainHeight(theme, rx - eps, ry);
        const float hyp = TerrainHeight(theme, rx, ry + eps);
        const float hym = TerrainHeight(theme, rx, ry - eps);
        const float gx = (hxp - hxm) / (2.0f * eps);
        const float gy = (hyp - hym) / (2.0f * eps);
        float tx = -gx, ty = -gy, tz = -1.0f;
        const float nl = std::sqrt(tx * tx + ty * ty + tz * tz);
        tx /= nl; ty /= nl; tz /= nl;
        // Baked crevice ambient occlusion from local Laplacian curvature (depressions darken cleanly!).
        const float avgNeigh = 0.25f * (hxp + hxm + hyp + hym);
        const float concavity = avgNeigh - h;
        const float localAo = Clamp01(1.0f - (concavity > 0.0f ? concavity * 7.5f : 0.0f));
        *outH = h;
        *nx = tx; *ny = ty; *nz = tz;
        *slopeMag = std::sqrt(gx * gx + gy * gy);
        *ao = 0.66f + 0.34f * localAo;
    };

    float ch = 0.0f, cnx = 0.0f, cny = 0.0f, cnz = -1.0f, cslope = 0.0f, cao = 1.0f;
    evalPoint(0.0f, 0.0f, &ch, &cnx, &cny, &cnz, &cslope, &cao);
    sb.PushV(wcx, wcy, topBase - ch, cnx, cny, cnz,
             TerrainColour(theme, 0.0f, 0.0f, ch, cslope, cao), 0.0f, 0.0f);

    for (unsigned r = 1; r <= kRings; ++r) {
        const float rad = kTerrainRadius * (float(r) / float(kRings));
        for (unsigned s = 0; s < kSegs; ++s) {
            const float a = float(s) * (2.0f * kPi / float(kSegs));
            const float rx = std::cos(a) * rad;
            const float ry = std::sin(a) * rad;
            float h = 0.0f, nx = 0.0f, ny = 0.0f, nz = -1.0f, slope = 0.0f, ao = 1.0f;
            evalPoint(rx, ry, &h, &nx, &ny, &nz, &slope, &ao);
            sb.PushV(wcx + rx, wcy + ry, topBase - h, nx, ny, nz,
                     TerrainColour(theme, rx, ry, h, slope, ao),
                     rx * uvTile, ry * uvTile);
        }
    }
    for (unsigned s = 0; s < kSegs; ++s) {
        const uint32_t v1 = base + 1u + s;
        const uint32_t v2 = base + 1u + ((s + 1u) % kSegs);
        sb.PushTri(base, v1, v2);
    }
    for (unsigned r = 1; r < kRings; ++r) {
        const uint32_t r0 = base + 1u + (r - 1u) * kSegs;
        const uint32_t r1 = base + 1u + r * kSegs;
        for (unsigned s = 0; s < kSegs; ++s) {
            const unsigned sn = (s + 1u) % kSegs;
            sb.PushTri(r0 + s, r1 + s, r1 + sn);
            sb.PushTri(r0 + s, r1 + sn, r0 + sn);
        }
    }

    // 4 cm sculpted geological cutaway strata rim framing R = kTerrainRadius (1.84 m).
    const DWORD strataTop = (theme == kThemeVolcanic) ? 0xff463a38u :
                            (theme == kThemeCoastal)  ? 0xff786b56u :
                            (theme == kThemeNautilus) ? 0xff5a6856u : 0xff284666u;
    const DWORD strataBot = 0xff221f24u;
    const uint32_t rimBase = sb.vUsed;
    const float wzRimBot = topBase + 0.020f;
    const float wzRimTop = topBase - 0.040f;
    for (unsigned s = 0; s <= kSegs; ++s) {
        const float a = float(s % kSegs) * (2.0f * kPi / float(kSegs));
        const float ca = std::cos(a), sa = std::sin(a);
        const float u = float(s) * 0.25f;
        sb.PushV(wcx + ca * kTerrainRadius, wcy + sa * kTerrainRadius, wzRimBot, ca, sa, 0.0f, strataBot, u, 0.0f);
        sb.PushV(wcx + ca * kTerrainRadius, wcy + sa * kTerrainRadius, wzRimTop, ca, sa, 0.0f, strataTop, u, 0.15f);
    }
    for (unsigned s = 0; s < kSegs; ++s) {
        const uint32_t i0 = rimBase + s * 2u;
        const uint32_t i1 = i0 + 2u;
        sb.PushTri(i0, i1, i1 + 1u);
        sb.PushTri(i0, i1 + 1u, i0 + 1u);
    }
}

// Sleek museum vitrine brass frame: 48-seg base bezel + 4 ultra-slender (1.7cm) normal-lit
// 4-sided diagonal-cardinal arch mullions (24 smooth steps) + sculpted brass apex finial.
// Eye-level 45-deg horizontal brass ring is intentionally OMITTED so sightlines stay razor-clean!
void EmitDioramaBrassFrame(StageBuilder &sb, float wcx, float wcy, float floorZ) {
    const float domeH = kDomeTopZ - kTabletopZ;
    const DWORD brassBright = 0xffe4c272u;
    const DWORD brassMid    = 0xffc49c4eu;
    const DWORD brassDeep   = 0xff8c6c30u;

    // 1. Smooth 48-segment circular brushed-brass base bezel framing the blown-glass dome.
    sb.DrumTier(wcx, wcy, floorZ, kDomeRadius + 0.040f, kDomeRadius + 0.025f,
                kTabletopZ - 0.01f, kTabletopZ + 0.036f, 48u, brassBright, brassDeep, false, 0.5f);
    sb.AnnulusRing(wcx, wcy, floorZ - (kTabletopZ + 0.036f),
                   kDomeRadius - 0.020f, kDomeRadius + 0.025f, 48u, brassMid, brassBright, 0.5f);

    // 2. Four ultra-slender (halfW = 0.0085m = 1.7cm) 4-sided normal-lit arch mullions (24 smooth steps!).
    // Placed at 45/135/225/315 deg so the front lectern sightline looks unobstructed into the hero scene.
    constexpr unsigned kArches = 4u;
    constexpr unsigned kSteps = 24u;
    constexpr float kHalfW = 0.0085f;
    for (unsigned m = 0; m < kArches; ++m) {
        const float ang = (kPi * 0.25f) + float(m) * (0.5f * kPi);
        const float ca = std::cos(ang), sa = std::sin(ang);
        // Side tangent normal perpendicular to meridian plane:
        const float sx = -sa, sy = ca;
        for (unsigned s = 0; s < kSteps; ++s) {
            const float p0 = (float(s) / float(kSteps)) * (0.5f * kPi);
            const float p1 = (float(s + 1u) / float(kSteps)) * (0.5f * kPi);
            const float cp0 = std::cos(p0), sp0 = std::sin(p0);
            const float cp1 = std::cos(p1), sp1 = std::sin(p1);
            const float r0 = (kDomeRadius + 0.004f) * cp0;
            const float r1 = (kDomeRadius + 0.004f) * cp1;
            const float z0 = floorZ - (kTabletopZ + domeH * sp0);
            const float z1 = floorZ - (kTabletopZ + domeH * sp1);

            // True outward radial normals at step endpoints (native -Z is up)
            const float on0x = cp0 * ca, on0y = cp0 * sa, on0z = -sp0;
            const float on1x = cp1 * ca, on1y = cp1 * sa, on1z = -sp1;
            const float omx = 0.5f * (on0x + on1x), omy = 0.5f * (on0y + on1y), omz = 0.5f * (on0z + on1z);
            const float oml = std::sqrt(omx * omx + omy * omy + omz * omz);
            const float nxOut = oml > 1e-5f ? omx / oml : ca;
            const float nyOut = oml > 1e-5f ? omy / oml : sa;
            const float nzOut = oml > 1e-5f ? omz / oml : 0.0f;

            const float c0x = wcx + ca * r0, c0y = wcy + sa * r0;
            const float c1x = wcx + ca * r1, c1y = wcy + sa * r1;
            const float v0 = float(s) * 0.12f, v1 = float(s + 1u) * 0.12f;

            // 4 corners at start (0) and end (1): Outer-Left, Outer-Right, Inner-Right, Inner-Left
            const float ol0x = c0x + on0x * kHalfW - sx * kHalfW, ol0y = c0y + on0y * kHalfW - sy * kHalfW, ol0z = z0 + on0z * kHalfW;
            const float or0x = c0x + on0x * kHalfW + sx * kHalfW, or0y = c0y + on0y * kHalfW + sy * kHalfW, or0z = z0 + on0z * kHalfW;
            const float ir0x = c0x - on0x * kHalfW + sx * kHalfW, ir0y = c0y - on0y * kHalfW + sy * kHalfW, ir0z = z0 - on0z * kHalfW;
            const float il0x = c0x - on0x * kHalfW - sx * kHalfW, il0y = c0y - on0y * kHalfW - sy * kHalfW, il0z = z0 - on0z * kHalfW;

            const float ol1x = c1x + on1x * kHalfW - sx * kHalfW, ol1y = c1y + on1y * kHalfW - sy * kHalfW, ol1z = z1 + on1z * kHalfW;
            const float or1x = c1x + on1x * kHalfW + sx * kHalfW, or1y = c1y + on1y * kHalfW + sy * kHalfW, or1z = z1 + on1z * kHalfW;
            const float ir1x = c1x - on1x * kHalfW + sx * kHalfW, ir1y = c1y - on1y * kHalfW + sy * kHalfW, ir1z = z1 - on1z * kHalfW;
            const float il1x = c1x - on1x * kHalfW - sx * kHalfW, il1y = c1y - on1y * kHalfW - sy * kHalfW, il1z = z1 - on1z * kHalfW;

            // Outer lit gold face (catches warm specular glint!)
            sb.PushQuadUv(ol0x, ol0y, ol0z, 0.0f, v0,  or0x, or0y, or0z, 0.1f, v0,
                          or1x, or1y, or1z, 0.1f, v1,  ol1x, ol1y, ol1z, 0.0f, v1,
                          nxOut, nyOut, nzOut, brassBright, brassBright);
            // Right side face
            sb.PushQuadUv(or0x, or0y, or0z, 0.0f, v0,  ir0x, ir0y, ir0z, 0.1f, v0,
                          ir1x, ir1y, ir1z, 0.1f, v1,  or1x, or1y, or1z, 0.0f, v1,
                          sx, sy, 0.0f, brassMid, brassMid);
            // Left side face
            sb.PushQuadUv(il0x, il0y, il0z, 0.0f, v0,  ol0x, ol0y, ol0z, 0.1f, v0,
                          ol1x, ol1y, ol1z, 0.1f, v1,  il1x, il1y, il1z, 0.0f, v1,
                          -sx, -sy, 0.0f, brassMid, brassMid);
            // Inner face
            sb.PushQuadUv(ir0x, ir0y, ir0z, 0.0f, v0,  il0x, il0y, il0z, 0.1f, v0,
                          il1x, il1y, il1z, 0.1f, v1,  ir1x, ir1y, ir1z, 0.0f, v1,
                          -nxOut, -nyOut, -nzOut, brassDeep, brassDeep);
        }
    }

    // 3. Sculpted 24-segment brushed-brass apex finial collar + tapered crown cap.
    sb.DrumTier(wcx, wcy, floorZ, 0.125f, 0.085f, kDomeTopZ - 0.020f, kDomeTopZ + 0.025f, 24u, brassBright, brassDeep, true, 1.0f);
    sb.DrumTier(wcx, wcy, floorZ, 0.055f, 0.016f, kDomeTopZ + 0.025f, kDomeTopZ + 0.090f, 24u, 0xfff0d284u, brassMid, true, 1.0f);
}

// 3D-lit angled brass-and-slate curator lectern (replaces the old unlit yellow cube stanchion!).
void EmitCuratorLectern(StageBuilder &sb, unsigned di, float floorZ, bool brassPass) {
    float sx = 0.0f, sy = 0.0f, sz = 0.0f;
    StudPosition(di, &sx, &sy, &sz);
    const DioramaSpec &ds = kDioramas[di];
    const float wx = CeMuseumWorld::kEntranceX + sx;
    const float wy = CeMuseumWorld::kEntranceY + sy;
    const float fx = ds.studDirX, fy = ds.studDirY;
    const float rx = -fy, ry = fx; // horizontal right tangent

    // Tilted 32 deg ergonomic reading plane facing outward toward the visitor.
    const float tilt = 0.56f;
    const float ct = std::cos(tilt), st = std::sin(tilt);
    // Desk forward (downward along slope toward visitor): (ct*fx, ct*fy, +st) in Z-down
    const float dfx = ct * fx, dfy = ct * fy, dfz = st;
    // Desk normal (upward + outward toward visitor): (st*fx, st*fy, -ct) in Z-down
    const float dux = st * fx, duy = st * fy, duz = -ct;
    const float deskZ = floorZ - (sz - 0.015f);

    if (!brassPass) {
        // Dark-slate stepped base + tapered lectern pedestal column + angled dark-bronze plate.
        sb.OrientedBox(wx, wy, floorZ - 0.055f, fx, fy, 0.0f, rx, ry, 0.0f, 0.0f, 0.0f, -1.0f,
                       0.145f, 0.165f, 0.035f, 0xff3b444eu, 0xff242b32u, 2.0f);
        sb.OrientedBox(wx, wy, floorZ - 0.520f, fx, fy, 0.0f, rx, ry, 0.0f, 0.0f, 0.0f, -1.0f,
                       0.055f, 0.070f, 0.435f, 0xff323b44u, 0xff222830u, 1.5f);
        // Angled dark-slate desk core
        sb.OrientedBox(wx, wy, deskZ, dfx, dfy, dfz, rx, ry, 0.0f, dux, duy, duz,
                       0.135f, 0.195f, 0.022f, 0xff283038u, 0xff1c2228u, 2.5f);
        // Raised dark-bronze curator nameplate sitting flush atop desk
        sb.OrientedBox(wx + dux * 0.024f, wy + duy * 0.024f, deskZ + duz * 0.024f,
                       dfx, dfy, dfz, rx, ry, 0.0f, dux, duy, duz,
                       0.095f, 0.155f, 0.008f, 0xff4c3d2au, 0xff322618u, 3.0f);
    } else {
        // Brushed-brass collar plinth, vertical inlay ribs, angled desk bezel & jewel socket.
        sb.OrientedBox(wx, wy, floorZ - 0.105f, fx, fy, 0.0f, rx, ry, 0.0f, 0.0f, 0.0f, -1.0f,
                       0.125f, 0.145f, 0.016f, 0xffdcb668u, 0xff9a7838u, 2.0f);
        sb.OrientedBox(wx, wy, floorZ - 0.940f, fx, fy, 0.0f, rx, ry, 0.0f, 0.0f, 0.0f, -1.0f,
                       0.072f, 0.088f, 0.022f, 0xffdcb668u, 0xff9a7838u, 2.0f);
        // Angled brushed-brass frame bevel around lectern desk
        sb.OrientedBox(wx + dux * 0.008f, wy + duy * 0.008f, deskZ + duz * 0.008f,
                       dfx, dfy, dfz, rx, ry, 0.0f, dux, duy, duz,
                       0.145f, 0.205f, 0.012f, 0xffe4c272u, 0xffa6823eu, 2.5f);
        // Circular brass bezel housing for the glowing interactive status jewel
        const float jx = wx + dux * 0.034f - dfx * 0.032f;
        const float jy = wy + duy * 0.034f - dfy * 0.032f;
        const float jz = deskZ + duz * 0.034f - dfz * 0.032f;
        sb.OrientedBox(jx, jy, jz, dfx, dfy, dfz, rx, ry, 0.0f, dux, duy, duz,
                       0.032f, 0.032f, 0.012f, 0xfff0d284u, 0xffa8843eu, 4.0f);
    }
}

// Translucent water plane + white surf foam contours for Coastal (I) and Nautilus (IV).
void EmitDioramaWaterAndFoam(DomeBuilder &db, Theme theme, float wcx, float wcy, float floorZ) {
    const float waterH = (theme == kThemeCoastal) ? 0.020f : 0.024f;
    const float wz = floorZ - (kTabletopZ + waterH);
    constexpr unsigned kRings = 16u;
    constexpr unsigned kSegs = 48u;
    if (!db.HasSpace(1u + kRings * kSegs, (kSegs + (kRings - 1u) * kSegs * 2u) * 3u)) return;

    auto waterColAt = [&](float rx, float ry) -> DWORD {
        const float th = TerrainHeight(theme, rx, ry);
        // Crisp white surf foam where terrain rises through the waterline!
        if (th > 0.004f && th < 0.058f) {
            const float f = 1.0f - std::fabs(th - 0.024f) / 0.034f;
            const float foam = Clamp01(f);
            const DWORD shallow = (theme == kThemeCoastal) ? 0x8422a0bcu : 0x8218b8c4u;
            return LerpArgb(shallow, 0xd8eefaffu, foam * 0.85f);
        }
        if (th >= 0.058f) return 0x00167896u; // fully transparent under land
        const float r = std::sqrt(rx * rx + ry * ry);
        const float edge = Clamp01(r / kTerrainRadius);
        return (theme == kThemeCoastal)
            ? LerpArgb(0x901880a4u, 0x980f567cu, edge * 0.85f)
            : LerpArgb(0x88169cb4u, 0x920e6888u, edge * 0.85f);
    };

    const uint16_t base = db.PushV(wcx, wcy, wz, 0.0f, 0.0f, -1.0f, waterColAt(0.0f, 0.0f));
    for (unsigned r = 1; r <= kRings; ++r) {
        const float rad = (kTerrainRadius - 0.015f) * (float(r) / float(kRings));
        for (unsigned s = 0; s < kSegs; ++s) {
            const float a = float(s) * (2.0f * kPi / float(kSegs));
            const float rx = std::cos(a) * rad;
            const float ry = std::sin(a) * rad;
            db.PushV(wcx + rx, wcy + ry, wz, 0.0f, 0.0f, -1.0f, waterColAt(rx, ry));
        }
    }
    for (unsigned s = 0; s < kSegs; ++s) {
        const uint16_t v1 = static_cast<uint16_t>(base + 1u + s);
        const uint16_t v2 = static_cast<uint16_t>(base + 1u + ((s + 1u) % kSegs));
        db.PushTri(base, v1, v2);
    }
    for (unsigned r = 1; r < kRings; ++r) {
        const uint16_t r0 = static_cast<uint16_t>(base + 1u + (r - 1u) * kSegs);
        const uint16_t r1 = static_cast<uint16_t>(base + 1u + r * kSegs);
        for (unsigned s = 0; s < kSegs; ++s) {
            const unsigned sn = (s + 1u) % kSegs;
            db.PushTri(static_cast<uint16_t>(r0 + s), static_cast<uint16_t>(r1 + s), static_cast<uint16_t>(r1 + sn));
            db.PushTri(static_cast<uint16_t>(r0 + s), static_cast<uint16_t>(r1 + sn), static_cast<uint16_t>(r0 + sn));
        }
    }

    // Crisp white surf-wake rings around beaching landers (Coastal) or surfacing Gill-M (Nautilus).
    auto emitFoamRing = [&](float fx, float fy, float rIn, float rOut, DWORD cPeak) {
        constexpr unsigned kFSegs = 24u;
        if (!db.HasSpace(kFSegs * 3u, kFSegs * 12u)) return;
        const float rMid = 0.5f * (rIn + rOut);
        const float fwz = wz - 0.004f;
        const uint16_t fb = static_cast<uint16_t>(db.vUsed);
        const DWORD cEdge = cPeak & 0x00ffffffu;
        for (unsigned s = 0; s < kFSegs; ++s) {
            const float a = float(s) * (2.0f * kPi / float(kFSegs));
            const float ca = std::cos(a), sa = std::sin(a);
            db.PushV(wcx + fx + ca * rIn,  wcy + fy + sa * rIn,  fwz, 0.0f, 0.0f, -1.0f, cEdge);
            db.PushV(wcx + fx + ca * rMid, wcy + fy + sa * rMid, fwz, 0.0f, 0.0f, -1.0f, cPeak);
            db.PushV(wcx + fx + ca * rOut, wcy + fy + sa * rOut, fwz, 0.0f, 0.0f, -1.0f, cEdge);
        }
        for (unsigned s = 0; s < kFSegs; ++s) {
            const unsigned sn = (s + 1u) % kFSegs;
            const uint16_t a0 = static_cast<uint16_t>(fb + s * 3u);
            const uint16_t b0 = static_cast<uint16_t>(fb + sn * 3u);
            db.PushTri(a0, a0 + 1u, b0 + 1u); db.PushTri(a0, b0 + 1u, b0);
            db.PushTri(a0 + 1u, a0 + 2u, b0 + 2u); db.PushTri(a0 + 1u, b0 + 2u, b0 + 1u);
        }
    };
    if (theme == kThemeCoastal) {
        emitFoamRing(-0.74f, -0.14f, 0.14f, 0.28f, 0xb8f0faffu);
        emitFoamRing(-0.46f, -0.86f, 0.14f, 0.27f, 0xb8f0faffu);
        emitFoamRing(-1.14f, -0.58f, 0.24f, 0.42f, 0x90dff4ffu);
    } else if (theme == kThemeNautilus) {
        emitFoamRing( 0.06f,  0.04f, 0.24f, 0.46f, 0xc8eafcffu); // churning surf around Gill-M!
        emitFoamRing( 0.82f, -0.46f, 0.18f, 0.36f, 0x98e4faffu); // wake around Thornback sub
        emitFoamRing(-0.86f, -0.58f, 0.18f, 0.34f, 0x8ce0f8ffu); // wake around Marshall 1
    }
}

// Dedicated additive glowing molten magma surface across Volcanic Foundry's 3 caldera channels.
void EmitVolcanicMagmaRivers(DomeBuilder &db, float wcx, float wcy, float floorZ) {
    // Sample a fine Cartesian grid inside R = 1.76m and emit bright glowing magma quads only along
    // the carved channels (VolcanicLavaDistance < 0.145m), floating at z = kTabletopZ + 0.044m!
    constexpr int kGrid = 36;
    const float span = kTerrainRadius - 0.06f;
    const float step = (2.0f * span) / float(kGrid);
    const float wz = floorZ - (kTabletopZ + 0.044f);
    for (int gy = 0; gy < kGrid; ++gy) {
        for (int gx = 0; gx < kGrid; ++gx) {
            const float x0 = -span + float(gx) * step;
            const float y0 = -span + float(gy) * step;
            const float x1 = x0 + step;
            const float y1 = y0 + step;
            const float mx = 0.5f * (x0 + x1), my = 0.5f * (y0 + y1);
            if (mx * mx + my * my > span * span) continue;
            const float d00 = VolcanicLavaDistance(x0, y0);
            const float d10 = VolcanicLavaDistance(x1, y0);
            const float d11 = VolcanicLavaDistance(x1, y1);
            const float d01 = VolcanicLavaDistance(x0, y1);
            if (d00 >= 0.15f && d10 >= 0.15f && d11 >= 0.15f && d01 >= 0.15f) continue;
            if (!db.HasSpace(4u, 6u)) return;
            auto lavaCol = [&](float d) -> DWORD {
                if (d >= 0.145f) return 0x00ff3c08u;
                const float core = Clamp01(1.0f - d / 0.145f);
                const unsigned a = FloatToByte(core * core * 225.0f);
                const DWORD rgb = LerpArgb(0xffff4808u, 0xffffdf58u, core * core);
                return (a << 24) | (rgb & 0x00ffffffu);
            };
            const uint16_t i0 = db.PushV(wcx + x0, wcy + y0, wz, 0.0f, 0.0f, -1.0f, lavaCol(d00));
            const uint16_t i1 = db.PushV(wcx + x1, wcy + y0, wz, 0.0f, 0.0f, -1.0f, lavaCol(d10));
            const uint16_t i2 = db.PushV(wcx + x1, wcy + y1, wz, 0.0f, 0.0f, -1.0f, lavaCol(d11));
            const uint16_t i3 = db.PushV(wcx + x0, wcy + y1, wz, 0.0f, 0.0f, -1.0f, lavaCol(d01));
            db.PushTri(i0, i1, i2);
            db.PushTri(i0, i2, i3);
        }
    }
}

// Three volumetric multi-ring atmospheric cloud decks beneath the hovering Skycarriers.
void EmitSkycarrierClouds(DomeBuilder &db, float wcx, float wcy, float floorZ) {
    constexpr unsigned kSegs = 48u;
    const float heights[3] = {0.22f, 0.35f, 0.47f};
    const float rIns[3]    = {0.42f, 0.68f, 0.96f};
    const float rOuts[3]   = {1.44f, 1.62f, 1.74f};
    const DWORD cMids[3]   = {0x42e4f0ffu, 0x3cf0f6ffu, 0x34f8fbffu};

    for (unsigned cr = 0; cr < 3u; ++cr) {
        if (!db.HasSpace(kSegs * 3u, kSegs * 12u)) return;
        const float cz = floorZ - (kTabletopZ + heights[cr]);
        const float rIn = rIns[cr], rOut = rOuts[cr];
        const float rMid = 0.5f * (rIn + rOut);
        const DWORD cPeak = cMids[cr];
        const DWORD cZero = cPeak & 0x00ffffffu;
        const uint16_t base = static_cast<uint16_t>(db.vUsed);
        for (unsigned s = 0; s < kSegs; ++s) {
            const float a = float(s) * (2.0f * kPi / float(kSegs));
            const float ca = std::cos(a), sa = std::sin(a);
            const float wIn  = 0.07f * std::sin(a * 3.0f + float(cr) * 1.9f);
            const float wOut = 0.09f * std::cos(a * 4.0f - float(cr) * 1.3f);
            const float zRip = 0.014f * std::sin(a * 5.0f + float(cr) * 2.1f);
            db.PushV(wcx + ca * (rIn + wIn),   wcy + sa * (rIn + wIn),   cz + zRip, 0.0f, 0.0f, -1.0f, cZero);
            db.PushV(wcx + ca * (rMid + wOut * 0.5f), wcy + sa * (rMid + wOut * 0.5f), cz - zRip, 0.0f, 0.0f, -1.0f, cPeak);
            db.PushV(wcx + ca * (rOut + wOut), wcy + sa * (rOut + wOut), cz + zRip, 0.0f, 0.0f, -1.0f, cZero);
        }
        for (unsigned s = 0; s < kSegs; ++s) {
            const unsigned sn = (s + 1u) % kSegs;
            const uint16_t a0 = static_cast<uint16_t>(base + s * 3u);
            const uint16_t b0 = static_cast<uint16_t>(base + sn * 3u);
            db.PushTri(a0, a0 + 1u, b0 + 1u); db.PushTri(a0, b0 + 1u, b0);
            db.PushTri(a0 + 1u, a0 + 2u, b0 + 2u); db.PushTri(a0 + 1u, b0 + 2u, b0 + 1u);
        }
    }
}

void EnsureStageGpuBuffers(RuntimeData *d, IDirect3DDevice9 *dev, float floorZ) {
    if (!d || !dev) return;
    const uint64_t gen = CurrentDioramaDeviceGen();
    if (d->stageAttempted && d->device == dev && d->deviceGen == gen && std::fabs(d->stageFloorZ - floorZ) < 1e-4f)
        return;
    ReleaseStageBuffers(d);
    d->device = dev;
    d->deviceGen = gen;
    d->stageFloorZ = floorZ;
    d->stageAttempted = true;

    // 0. Create shared 256x256 mipmapped procedural micro-relief terrain detail texture.
    d->terrainDetailTex = CreateTerrainDetailTexture(dev);

    // 1. Build opaque sculpted 32-seg drum pedestals, lit curator lecterns, 32x64 terrains
    //    with cutaway strata rims, and sleek 4-arch brushed-brass dome frames.
    LitStageVertex *sv = static_cast<LitStageVertex *>(std::malloc(kMaxStageVerts * sizeof(LitStageVertex)));
    uint32_t *si = static_cast<uint32_t *>(std::malloc(kMaxStageIdx * sizeof(uint32_t)));
    if (sv && si) {
        StageBuilder sb = {sv, si, kMaxStageVerts, kMaxStageIdx, 0u, 0u};
        for (unsigned di = 0; di < kDioramaCount; ++di) {
            const DioramaSpec &ds = kDioramas[di];
            const float wcx = CeMuseumWorld::kEntranceX + ds.localX;
            const float wcy = CeMuseumWorld::kEntranceY + ds.localY;
            const DWORD accent = (ds.theme == kThemeCoastal)    ? 0xff46d4f4u :
                                 (ds.theme == kThemeVolcanic)   ? 0xffff7828u :
                                 (ds.theme == kThemeSkycarrier) ? 0xff62c8ffu : 0xff28e0d0u;

            // (a) Slate & sculpted circular 32-seg drum pedestal + lectern body
            const unsigned slateStart = sb.iUsed;
            sb.DrumTier(wcx, wcy, floorZ, 1.78f, 1.62f, 0.04f, 0.18f, 32u, 0xff46505cu, 0xff283038u, true,  0.55f);
            sb.DrumTier(wcx, wcy, floorZ, 1.32f, 1.14f, 0.18f, 0.78f, 32u, 0xff36404au, 0xff20262eu, false, 0.55f);
            sb.DrumTier(wcx, wcy, floorZ, 1.14f, 1.98f, 0.78f, 0.90f, 32u, 0xff4c5864u, 0xff2c343eu, false, 0.55f);
            sb.DrumTier(wcx, wcy, floorZ, 2.02f, 2.02f, 0.90f, 0.945f, 32u, accent,     accent,      false, 0.40f);
            // Recessed dark inner bed cleanly beneath terrain dish troughs (zero polygonal gaps or clipping!)
            sb.DrumTier(wcx, wcy, floorZ, 1.88f, 1.88f, 0.99f, 1.020f, 48u, 0xff1c2228u, 0xff161b20u, true, 0.50f);
            EmitCuratorLectern(sb, di, floorZ, false);
            d->pedSlateRange[di] = {slateStart, (sb.iUsed - slateStart) / 3u};

            // (b) Brushed-brass 48-seg table rim, 48-seg dome base bezel, 4 slender cardinal arches,
            //     apex finial & angled lectern brass trim
            const unsigned brassStart = sb.iUsed;
            sb.DrumTier(wcx, wcy, floorZ, 2.05f, 2.02f, 0.945f, 1.055f, 48u, 0xffe2be6eu, 0xff9a7838u, false, 0.60f);
            sb.AnnulusRing(wcx, wcy, floorZ - 1.055f, 1.855f, 2.02f, 48u, 0xffc49c4eu, 0xffe6c474u, 0.60f);
            EmitDioramaBrassFrame(sb, wcx, wcy, floorZ);
            EmitCuratorLectern(sb, di, floorZ, true);
            d->pedBrassRange[di] = {brassStart, (sb.iUsed - brassStart) / 3u};

            // (c) High-res 32x64 sculpted diorama terrain + 4cm cutaway geological strata rim
            const unsigned terrStart = sb.iUsed;
            EmitDioramaTerrainAndStrata(sb, ds.theme, wcx, wcy, floorZ);
            d->terrainRange[di] = {terrStart, (sb.iUsed - terrStart) / 3u};
        }
        if (sb.vUsed >= 3u && sb.iUsed >= 3u) {
            if (UploadManagedVb(dev, sv, sb.vUsed * sizeof(LitStageVertex), kStageFvf, &d->stageVb) &&
                UploadManagedIb(dev, si, sb.iUsed * sizeof(uint32_t), D3DFMT_INDEX32, &d->stageIb)) {
                d->stageVertCount = sb.vUsed;
                d->stageIdxCount = sb.iUsed;
            } else {
                if (d->stageVb) { d->stageVb->Release(); d->stageVb = NULL; }
                if (d->stageIb) { d->stageIb->Release(); d->stageIb = NULL; }
            }
        }
    }
    if (sv) std::free(sv);
    if (si) std::free(si);

    // 2. Build soft radial floor contact shadows, translucent water/foam planes, additive magma
    //    ribbons, volumetric cloud decks, and 48x16 smooth-normal optical blown glass hemispheres.
    LitDomeVertex *dv = static_cast<LitDomeVertex *>(std::malloc(kMaxDomeVerts * sizeof(LitDomeVertex)));
    uint16_t *diBuf = static_cast<uint16_t *>(std::malloc(kMaxDomeIdx * sizeof(uint16_t)));
    if (dv && diBuf) {
        DomeBuilder db = {dv, diBuf, kMaxDomeVerts, kMaxDomeIdx, 0u, 0u};

        // (a) Soft radial floor contact shadows under all 4 circular pedestals
        const unsigned shadowStart = db.iUsed;
        for (unsigned dIdx = 0; dIdx < kDioramaCount; ++dIdx) {
            const DioramaSpec &ds = kDioramas[dIdx];
            const float wcx = CeMuseumWorld::kEntranceX + ds.localX;
            const float wcy = CeMuseumWorld::kEntranceY + ds.localY;
            const float wz = floorZ - 0.046f;
            constexpr unsigned kShSegs = 32u;
            if (!db.HasSpace(kShSegs * 3u, kShSegs * 12u)) break;
            const uint16_t sb0 = static_cast<uint16_t>(db.vUsed);
            for (unsigned s = 0; s < kShSegs; ++s) {
                const float a = float(s) * (2.0f * kPi / float(kShSegs));
                const float ca = std::cos(a), sa = std::sin(a);
                db.PushV(wcx + ca * 1.56f, wcy + sa * 1.56f, wz, 0.0f, 0.0f, -1.0f, 0x70060a10u);
                db.PushV(wcx + ca * 2.02f, wcy + sa * 2.02f, wz, 0.0f, 0.0f, -1.0f, 0x38060a10u);
                db.PushV(wcx + ca * 2.48f, wcy + sa * 2.48f, wz, 0.0f, 0.0f, -1.0f, 0x00060a10u);
            }
            for (unsigned s = 0; s < kShSegs; ++s) {
                const unsigned sn = (s + 1u) % kShSegs;
                const uint16_t a0 = static_cast<uint16_t>(sb0 + s * 3u);
                const uint16_t b0 = static_cast<uint16_t>(sb0 + sn * 3u);
                db.PushTri(a0, a0 + 1u, b0 + 1u); db.PushTri(a0, b0 + 1u, b0);
                db.PushTri(a0 + 1u, a0 + 2u, b0 + 2u); db.PushTri(a0 + 1u, b0 + 2u, b0 + 1u);
            }
        }
        d->shadowRange = {shadowStart, (db.iUsed - shadowStart) / 3u};

        // (b) Translucent turquoise/coral water planes + white surf foam rings (Dioramas I & IV)
        const unsigned waterStart = db.iUsed;
        for (unsigned dIdx = 0; dIdx < kDioramaCount; ++dIdx) {
            const DioramaSpec &ds = kDioramas[dIdx];
            if (ds.theme == kThemeCoastal || ds.theme == kThemeNautilus) {
                EmitDioramaWaterAndFoam(db, ds.theme,
                                        CeMuseumWorld::kEntranceX + ds.localX,
                                        CeMuseumWorld::kEntranceY + ds.localY, floorZ);
            }
        }
        d->waterRange = {waterStart, (db.iUsed - waterStart) / 3u};

        // (c) Dedicated glowing molten magma river ribbons (Diorama II - Volcanic Foundry Siege)
        const unsigned magmaStart = db.iUsed;
        for (unsigned dIdx = 0; dIdx < kDioramaCount; ++dIdx) {
            const DioramaSpec &ds = kDioramas[dIdx];
            if (ds.theme == kThemeVolcanic) {
                EmitVolcanicMagmaRivers(db,
                                        CeMuseumWorld::kEntranceX + ds.localX,
                                        CeMuseumWorld::kEntranceY + ds.localY, floorZ);
            }
        }
        d->magmaRange = {magmaStart, (db.iUsed - magmaStart) / 3u};

        // (d) Volumetric 3-deck cloud rings (Diorama III - Skycarrier Fleet Clash)
        const unsigned cloudStart = db.iUsed;
        for (unsigned dIdx = 0; dIdx < kDioramaCount; ++dIdx) {
            const DioramaSpec &ds = kDioramas[dIdx];
            if (ds.theme == kThemeSkycarrier) {
                EmitSkycarrierClouds(db,
                                     CeMuseumWorld::kEntranceX + ds.localX,
                                     CeMuseumWorld::kEntranceY + ds.localY, floorZ);
            }
        }
        d->cloudRange = {cloudStart, (db.iUsed - cloudStart) / 3u};

        // (e) Four smooth-normal 48x16 optical blown-glass hemispheres (front-face CCW -> D3DCULL_CW!)
        const unsigned glassStart = db.iUsed;
        constexpr unsigned kDomeSegs = 48u;
        constexpr unsigned kDomeRings = 16u;
        const float domeH = kDomeTopZ - kTabletopZ;
        for (unsigned dIdx = 0; dIdx < kDioramaCount; ++dIdx) {
            const DioramaSpec &ds = kDioramas[dIdx];
            const float wcx = CeMuseumWorld::kEntranceX + ds.localX;
            const float wcy = CeMuseumWorld::kEntranceY + ds.localY;
            if (!db.HasSpace((kDomeRings + 1u) * kDomeSegs, kDomeRings * kDomeSegs * 6u)) break;

            const uint16_t base = static_cast<uint16_t>(db.vUsed);
            for (unsigned r = 0; r <= kDomeRings; ++r) {
                const float t = float(r) / float(kDomeRings);
                const float phi = t * (0.5f * kPi);
                const float cp = std::cos(phi), sp = std::sin(phi);
                const float rad = kDomeRadius * cp;
                const float wz = floorZ - (kTabletopZ + domeH * sp);
                // Ultra-clear subtle sky-glass tint (alpha 0x14 at bezel -> 0x0A mid -> 0x0E apex)
                const float aVal = (t < 0.35f)
                    ? (20.0f - (t / 0.35f) * 10.0f)
                    : (10.0f + ((t - 0.35f) / 0.65f) * 4.0f);
                const unsigned alpha = FloatToByte(aVal);
                const DWORD col = PackArgb(alpha, 206u, 232u, 248u);
                for (unsigned s = 0; s < kDomeSegs; ++s) {
                    const float a = float(s) * (2.0f * kPi / float(kDomeSegs));
                    const float ca = std::cos(a), sa = std::sin(a);
                    // True outward normal on hemisphere (native -Z is up) for live hardware specular!
                    db.PushV(wcx + ca * rad, wcy + sa * rad, wz, cp * ca, cp * sa, -sp, col);
                }
            }
            for (unsigned r = 0; r < kDomeRings; ++r) {
                const uint16_t r0 = static_cast<uint16_t>(base + r * kDomeSegs);
                const uint16_t r1 = static_cast<uint16_t>(base + (r + 1u) * kDomeSegs);
                for (unsigned s = 0; s < kDomeSegs; ++s) {
                    const unsigned sn = (s + 1u) % kDomeSegs;
                    db.PushTri(static_cast<uint16_t>(r0 + s), static_cast<uint16_t>(r0 + sn), static_cast<uint16_t>(r1 + sn));
                    db.PushTri(static_cast<uint16_t>(r0 + s), static_cast<uint16_t>(r1 + sn), static_cast<uint16_t>(r1 + s));
                }
            }
        }
        d->domeGlassRange = {glassStart, (db.iUsed - glassStart) / 3u};

        if (db.vUsed >= 3u && db.iUsed >= 3u) {
            if (UploadManagedVb(dev, dv, db.vUsed * sizeof(LitDomeVertex), kDomeFvf, &d->domeVb) &&
                UploadManagedIb(dev, diBuf, db.iUsed * sizeof(uint16_t), D3DFMT_INDEX16, &d->domeIb)) {
                d->domeVertCount = db.vUsed;
                d->domeIdxCount = db.iUsed;
            } else {
                if (d->domeVb) { d->domeVb->Release(); d->domeVb = NULL; }
                if (d->domeIb) { d->domeIb->Release(); d->domeIb = NULL; }
            }
        }
    }
    if (dv) std::free(dv);
    if (diBuf) std::free(diBuf);
}

void BuildUnitWorldMatrix(const UniqueMeshGpu &um, float wx, float wy, float baseWorldZ,
                          float targetSpan, float yawRad, float pitchRad, float rollRad,
                          D3DMATRIX *out) {
    const float span = um.span > 1e-4f ? um.span : 1.0f;
    float sc = targetSpan / span;
    if (sc < 0.004f) sc = 0.004f;
    if (sc > 2.5f) sc = 2.5f;

    const float cy = std::cos(yawRad),   sy = std::sin(yawRad);
    const float cp = std::cos(pitchRad), sp = std::sin(pitchRad);
    const float cr = std::cos(rollRad),  sr = std::sin(rollRad);

    // Row-vector composite R = R_roll(Y) * R_pitch(X) * R_yaw(Z)
    const float r00 = cr * cy - sr * sp * sy;
    const float r01 = cr * sy + sr * sp * cy;
    const float r02 = -sr * cp;
    const float r10 = -cp * sy;
    const float r11 =  cp * cy;
    const float r12 =  sp;
    const float r20 = sr * cy + cr * sp * sy;
    const float r21 = sr * sy - cr * sp * cy;
    const float r22 = cr * cp;

    const float ox = -um.centerX;
    const float oy = -um.centerY;
    const float oz = -um.maxZ;

    std::memset(out, 0, sizeof(*out));
    out->_11 = r00 * sc; out->_12 = r01 * sc; out->_13 = r02 * sc;
    out->_21 = r10 * sc; out->_22 = r11 * sc; out->_23 = r12 * sc;
    out->_31 = r20 * sc; out->_32 = r21 * sc; out->_33 = r22 * sc;
    out->_41 = ox * out->_11 + oy * out->_21 + oz * out->_31 + wx;
    out->_42 = ox * out->_12 + oy * out->_22 + oz * out->_32 + wy;
    out->_43 = ox * out->_13 + oy * out->_23 + oz * out->_33 + baseWorldZ;
    out->_44 = 1.0f;
}

// Configure dramatic per-table 3-light gallery mood (low ambient, crisp key + cool rim + biome uplight!).
void SetupDioramaLights(IDirect3DDevice9 *dev, Theme theme) {
    dev->SetRenderState(D3DRS_AMBIENT, D3DCOLOR_XRGB(26, 30, 38));
    auto setLight = [&](DWORD idx, float dx, float dy, float dz,
                        float dr, float dg, float db,
                        float sr, float sg, float sb) {
        const float l = std::sqrt(dx * dx + dy * dy + dz * dz);
        const float inv = l > 1e-5f ? (1.0f / l) : 1.0f;
        D3DLIGHT9 lt = {};
        lt.Type = D3DLIGHT_DIRECTIONAL;
        lt.Direction.x = dx * inv; lt.Direction.y = dy * inv; lt.Direction.z = dz * inv;
        lt.Diffuse.r = dr; lt.Diffuse.g = dg; lt.Diffuse.b = db; lt.Diffuse.a = 1.0f;
        lt.Ambient.r = 0.04f * dr; lt.Ambient.g = 0.04f * dg; lt.Ambient.b = 0.04f * db; lt.Ambient.a = 1.0f;
        lt.Specular.r = sr; lt.Specular.g = sg; lt.Specular.b = sb; lt.Specular.a = 1.0f;
        dev->SetLight(idx, &lt);
        dev->LightEnable(idx, TRUE);
    };

    switch (theme) {
    case kThemeCoastal:
        // Golden coastal sun key + crisp marine sky fill + subtle turquoise bay uplight
        setLight(0, -0.58f,  0.46f,  0.67f, 1.04f, 0.94f, 0.78f, 0.95f, 0.88f, 0.74f);
        setLight(1,  0.62f, -0.48f,  0.62f, 0.36f, 0.56f, 0.82f, 0.38f, 0.56f, 0.80f);
        setLight(2, -0.24f, -0.22f, -0.94f, 0.14f, 0.34f, 0.42f, 0.14f, 0.30f, 0.38f);
        break;
    case kThemeVolcanic:
        // Crisp gallery spot + cool obsidian rim + FIERY UPWARD ORANGE MAGMA LIGHT 2!
        setLight(0, -0.48f,  0.52f,  0.70f, 0.86f, 0.78f, 0.72f, 0.90f, 0.82f, 0.74f);
        setLight(1,  0.66f, -0.46f,  0.58f, 0.28f, 0.36f, 0.54f, 0.36f, 0.44f, 0.64f);
        setLight(2,  0.05f, -0.05f, -0.99f, 0.96f, 0.38f, 0.08f, 0.96f, 0.46f, 0.14f);
        break;
    case kThemeSkycarrier:
        // High-altitude crisp sun key + stratospheric azure rim + sunlit cloud-deck bounce
        setLight(0, -0.52f,  0.58f,  0.62f, 1.06f, 0.98f, 0.90f, 1.00f, 0.96f, 0.90f);
        setLight(1,  0.68f, -0.42f,  0.60f, 0.44f, 0.64f, 0.94f, 0.52f, 0.72f, 1.00f);
        setLight(2,  0.00f,  0.10f, -0.99f, 0.26f, 0.32f, 0.44f, 0.24f, 0.30f, 0.40f);
        break;
    case kThemeNautilus:
    default:
        // Bright equatorial tropical sun + deep ocean azure fill + turquoise reef lagoon bounce
        setLight(0, -0.46f,  0.44f,  0.77f, 1.04f, 0.96f, 0.82f, 0.98f, 0.92f, 0.80f);
        setLight(1,  0.60f, -0.54f,  0.58f, 0.34f, 0.58f, 0.78f, 0.40f, 0.66f, 0.86f);
        setLight(2,  0.10f, -0.15f, -0.98f, 0.14f, 0.44f, 0.46f, 0.16f, 0.44f, 0.46f);
        break;
    }
    for (DWORD li = 3; li < 8; ++li) dev->LightEnable(li, FALSE);
}

// Draw rising miniature smoke puffs (soft alpha blend) + multi-salvo tracers, 3-plane crossed
// additive starburst quads, and glowing lectern status jewels (pure additive, ZWRITEENABLE = FALSE).
// Runs strictly BEFORE the blown-glass dome passes!
void DrawDioramaVfx(IDirect3DDevice9 *dev, const RuntimeData *d, float floorZ) {
    if (!dev || !d || !d->vfxBatch) return;
    FlatVfxVertex *batch = d->vfxBatch;

    D3DMATRIX id = {};
    id._11 = id._22 = id._33 = id._44 = 1.0f;
    dev->SetTransform(D3DTS_WORLD, &id);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetTexture(0, NULL);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    dev->SetStreamSource(0, NULL, 0, 0);
    dev->SetIndices(NULL);
    dev->SetFVF(kFlatFvf);

    // Evaluate all active tracers once per frame and share the results across smoke + additive passes.
    TracerSegment evaluated[kDioramaCount][kMaxTracersPerDiorama] = {};
    for (unsigned di = 0; di < kDioramaCount; ++di) {
        const DioramaSpec &ds = kDioramas[di];
        for (unsigned ti = 0; ti < ds.tracerCount && ti < kMaxTracersPerDiorama; ++ti)
            EvaluateTracer(ds, ds.tracers[ti], d->state.simTime[di], &evaluated[di][ti]);
    }

    // -------------------------------------------------------------------------
    // Sub-pass A: Soft alpha-faded rising miniature smoke puffs above impacts
    // -------------------------------------------------------------------------
    unsigned used = 0u;
    auto pushCrossedPuff = [&](float cx, float cy, float cz, float rad, DWORD cCenter, DWORD cEdge) {
        // 3 orthogonal crossed diamond fans (XY, XZ, YZ) = 3 * 4 triangles = 36 verts
        if (used + 36u > kMaxVfxVerts) return;
        auto emitDiamond = [&](float ax, float ay, float az, float bx, float by, float bz) {
            const FlatVfxVertex ctr = {cx, cy, cz, cCenter};
            const FlatVfxVertex p0  = {cx + ax, cy + ay, cz + az, cEdge};
            const FlatVfxVertex p1  = {cx + bx, cy + by, cz + bz, cEdge};
            const FlatVfxVertex p2  = {cx - ax, cy - ay, cz - az, cEdge};
            const FlatVfxVertex p3  = {cx - bx, cy - by, cz - bz, cEdge};
            batch[used++] = ctr; batch[used++] = p0; batch[used++] = p1;
            batch[used++] = ctr; batch[used++] = p1; batch[used++] = p2;
            batch[used++] = ctr; batch[used++] = p2; batch[used++] = p3;
            batch[used++] = ctr; batch[used++] = p3; batch[used++] = p0;
        };
        emitDiamond(rad, 0.0f, 0.0f, 0.0f, rad, 0.0f);
        emitDiamond(rad, 0.0f, 0.0f, 0.0f, 0.0f, rad);
        emitDiamond(0.0f, rad, 0.0f, 0.0f, 0.0f, rad);
    };

    for (unsigned di = 0; di < kDioramaCount; ++di) {
        const DioramaSpec &ds = kDioramas[di];
        const float wcx = CeMuseumWorld::kEntranceX + ds.localX;
        const float wcy = CeMuseumWorld::kEntranceY + ds.localY;
        const float topBase = floorZ - kTabletopZ;
        for (unsigned ti = 0; ti < ds.tracerCount && ti < kMaxTracersPerDiorama; ++ti) {
            const TracerSegment &seg = evaluated[di][ti];
            if (seg.smokeAlpha > 0.04f) {
                const float r = 0.040f + 0.055f * (1.0f - seg.smokeAlpha);
                const unsigned a = FloatToByte(Clamp01(seg.smokeAlpha) * 110.0f);
                const DWORD cMid = PackArgb(a, 54u, 58u, 64u);
                const DWORD cOut = PackArgb(0u, 36u, 40u, 46u);
                pushCrossedPuff(wcx + seg.impactX, wcy + seg.impactY,
                                topBase - (seg.impactZ + seg.smokeRise), r, cMid, cOut);
            }
        }
    }
    if (used >= 3u) {
        dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, used / 3u, batch, sizeof(FlatVfxVertex));
    }

    // -------------------------------------------------------------------------
    // Sub-pass B: Pure additive multi-salvo tracers, 3-plane crossed starbursts & lectern jewels
    // -------------------------------------------------------------------------
    used = 0u;
    auto pushRibbon = [&](float ax, float ay, float az, float bx, float by, float bz,
                          float r, DWORD colTail, DWORD colHead) {
        if (used + 12u > kMaxVfxVerts) return;
        const float dx = bx - ax, dy = by - ay;
        float px = -dy, py = dx;
        const float pl = std::sqrt(px * px + py * py);
        if (pl > 1e-4f) { px = (px / pl) * r; py = (py / pl) * r; }
        else { px = r; py = 0.0f; }
        // Crossed horizontal & vertical luminous ribbons (tapered alpha at tail -> bright head!)
        batch[used++] = {ax - px, ay - py, az, colTail}; batch[used++] = {bx - px, by - py, bz, colHead}; batch[used++] = {bx + px, by + py, bz, colHead};
        batch[used++] = {ax - px, ay - py, az, colTail}; batch[used++] = {bx + px, by + py, bz, colHead}; batch[used++] = {ax + px, ay + py, az, colTail};
        batch[used++] = {ax, ay, az - r, colTail}; batch[used++] = {bx, by, bz - r, colHead}; batch[used++] = {bx, by, bz + r, colHead};
        batch[used++] = {ax, ay, az - r, colTail}; batch[used++] = {bx, by, bz + r, colHead}; batch[used++] = {ax, ay, az + r, colTail};
    };

    // 3-plane crossed additive starburst quad flare (replaces unlit cube impact boxes!).
    auto pushStarburst = [&](float cx, float cy, float cz, float rad, float alpha, DWORD rgb) {
        // 3 orthogonal starburst diamonds + 1 rotated 45 deg accent plane = 4 * 12 = 48 verts
        if (used + 48u > kMaxVfxVerts) return;
        const unsigned aCore = FloatToByte(Clamp01(alpha) * 255.0f);
        const DWORD cCore = PackArgb(aCore, 255u, 250u, 232u);
        const DWORD cMid  = (static_cast<DWORD>(FloatToByte(Clamp01(alpha) * 190.0f)) << 24) | (rgb & 0x00ffffffu);
        const DWORD cTip  = rgb & 0x00ffffffu;
        auto emitStar = [&](float ax, float ay, float az, float bx, float by, float bz, DWORD cCtr) {
            const FlatVfxVertex ctr = {cx, cy, cz, cCtr};
            const FlatVfxVertex p0  = {cx + ax, cy + ay, cz + az, cTip};
            const FlatVfxVertex p1  = {cx + bx, cy + by, cz + bz, cTip};
            const FlatVfxVertex p2  = {cx - ax, cy - ay, cz - az, cTip};
            const FlatVfxVertex p3  = {cx - bx, cy - by, cz - bz, cTip};
            batch[used++] = ctr; batch[used++] = p0; batch[used++] = p1;
            batch[used++] = ctr; batch[used++] = p1; batch[used++] = p2;
            batch[used++] = ctr; batch[used++] = p2; batch[used++] = p3;
            batch[used++] = ctr; batch[used++] = p3; batch[used++] = p0;
        };
        // 3 orthogonal principal starburst planes (XY, XZ, YZ)
        emitStar(rad, 0.0f, 0.0f, 0.0f, rad, 0.0f, cCore);
        emitStar(rad, 0.0f, 0.0f, 0.0f, 0.0f, rad, cCore);
        emitStar(0.0f, rad, 0.0f, 0.0f, 0.0f, rad, cCore);
        // Secondary diagonal color-bloom flare plane
        const float dRad = rad * 0.78f;
        emitStar(dRad, dRad, 0.0f, -dRad * 0.5f, dRad * 0.5f, dRad, cMid);
    };

    // 1. Small glowing interactive status jewel on each 3D-lit brass curator lectern
    for (unsigned di = 0; di < kDioramaCount; ++di) {
        float sx = 0.0f, sy = 0.0f, sz = 0.0f;
        StudPosition(di, &sx, &sy, &sz);
        const DioramaSpec &ds = kDioramas[di];
        const float fx = ds.studDirX, fy = ds.studDirY;
        const float tilt = 0.56f;
        const float ct = std::cos(tilt), st = std::sin(tilt);
        const float dfx = ct * fx, dfy = ct * fy, dfz = st;
        const float dux = st * fx, duy = st * fy, duz = -ct;
        const float deskZ = floorZ - (sz - 0.015f);
        const float jx = CeMuseumWorld::kEntranceX + sx + dux * 0.048f - dfx * 0.032f;
        const float jy = CeMuseumWorld::kEntranceY + sy + duy * 0.048f - dfy * 0.032f;
        const float jz = deskZ + duz * 0.048f - dfz * 0.032f;

        const bool hovered = (d->state.hoveredIndex == static_cast<int>(di));
        const bool paused = d->state.paused[di];
        const float pulse = 0.78f + 0.22f * std::sin(d->state.simTime[di] * 3.2f + float(di));
        const DWORD jewelRgb = hovered ? 0x00ffe878u : (paused ? 0x0046b4ffu : 0x0052ffaau);
        const float jRad = hovered ? 0.058f : 0.036f;
        pushStarburst(jx, jy, jz, jRad, hovered ? 1.0f : pulse * 0.88f, jewelRgb);
    }

    // 2. Live 3D multi-salvo tracers, sustained beams & 3-plane additive starbursts inside all 4 domes
    for (unsigned di = 0; di < kDioramaCount; ++di) {
        const DioramaSpec &ds = kDioramas[di];
        const float wcx = CeMuseumWorld::kEntranceX + ds.localX;
        const float wcy = CeMuseumWorld::kEntranceY + ds.localY;
        const float topBase = floorZ - kTabletopZ;
        for (unsigned ti = 0; ti < ds.tracerCount && ti < kMaxTracersPerDiorama; ++ti) {
            const TracerSegment &seg = evaluated[di][ti];
            if (seg.active) {
                const float ax = wcx + seg.ax, ay = wcy + seg.ay, az = topBase - seg.az;
                const float bx = wcx + seg.bx, by = wcy + seg.by, bz = topBase - seg.bz;
                const DWORD rgb = seg.colour & 0x00ffffffu;
                if (seg.style == kTracerBeam) {
                    // Sustained bright energy lance + hot white inner core
                    pushRibbon(ax, ay, az, bx, by, bz, 0.022f, (0x90u << 24) | rgb, (0xe0u << 24) | rgb);
                    pushRibbon(ax, ay, az, bx, by, bz, 0.008f, 0xd0ffffffu, 0xffffffffu);
                } else if (seg.style == kTracerPulse) {
                    // Crisp 2-round pulse burst train (lead bolt + trailing staggered bolt!)
                    const float mx = ax + (bx - ax) * 0.56f;
                    const float my = ay + (by - ay) * 0.56f;
                    const float mz = az + (bz - az) * 0.56f;
                    const float gx = ax + (bx - ax) * 0.42f;
                    const float gy = ay + (by - ay) * 0.42f;
                    const float gz = az + (bz - az) * 0.42f;
                    pushRibbon(mx, my, mz, bx, by, bz, 0.015f, (0x38u << 24) | rgb, (0xf0u << 24) | rgb);
                    pushRibbon(ax, ay, az, gx, gy, gz, 0.012f, (0x24u << 24) | rgb, (0xc0u << 24) | rgb);
                } else {
                    // Artillery shell arc or AA flak streak with incandescent head
                    const float thick = (seg.style == kTracerArtillery) ? 0.020f : 0.015f;
                    pushRibbon(ax, ay, az, bx, by, bz, thick, (0x28u << 24) | rgb, (0xf8u << 24) | rgb);
                    pushRibbon(ax + (bx - ax) * 0.65f, ay + (by - ay) * 0.65f, az + (bz - az) * 0.65f,
                               bx, by, bz, thick * 0.45f, 0xa0fff4d8u, 0xffffffffu);
                }
            }
            if (seg.impactAlpha > 0.05f) {
                const float baseRad = (seg.style == kTracerArtillery) ? 0.075f :
                                      (seg.style == kTracerBeam)      ? 0.055f : 0.060f;
                const float rad = baseRad + 0.085f * (1.0f - seg.impactAlpha);
                pushStarburst(wcx + seg.impactX, wcy + seg.impactY, topBase - seg.impactZ,
                              rad, seg.impactAlpha, seg.colour);
            }
        }
    }

    if (used >= 3u) {
        dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
        dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, used / 3u, batch, sizeof(FlatVfxVertex));
    }
}

} // namespace

void RuntimeInit() {
    RuntimeShutdown();
    RuntimeData *d = EnsureData();
    if (!d) return;
    Reset(&d->state);
    IDirect3DDevice9 *dev = LT.GetDevice();
    if (dev) EnsureUnitAssetsLoaded(d, dev);
}

void RuntimeShutdown() {
    if (!gDio) return;
    ReleaseStageBuffers(gDio);
    ReleaseUnitAssets(gDio);
    if (gDio->vfxBatch) {
        std::free(gDio->vfxBatch);
        gDio->vfxBatch = NULL;
    }
    std::free(gDio);
    gDio = NULL;
}

void RuntimeUpdate(CeMuseumTour::Wing wing, float dtSeconds, bool paused,
                   float eyeX, float eyeY, float eyeZ,
                   float aimX, float aimY, float aimZ) {
    RuntimeData *d = EnsureData();
    if (!d) return;
    if (paused) {
        d->state.fireHeld = false;
        d->state.fireCaptured = false;
    }
    Step(&d->state, dtSeconds, paused);
    d->state.hoveredIndex = PickStud(eyeX, eyeY, eyeZ, aimX, aimY, aimZ, wing);
}

bool RuntimeHandleFire(bool release, float value) {
    if (!gDio || !gDio->state.initialized) return false;
    const bool usable = gDio->state.hoveredIndex >= 0 &&
                        static_cast<unsigned>(gDio->state.hoveredIndex) < kDioramaCount;
    const CeCinemaRoom::FireIntent intent = CeCinemaRoom::FireEdge(
        release || value <= 0.0f, usable, gDio->state.fireHeld, gDio->state.fireCaptured);
    if (intent == CeCinemaRoom::FireIntent::Operate && usable) {
        TogglePause(&gDio->state, gDio->state.hoveredIndex);
        PortSessionLog(PORT_SESSION_GAME, false,
                       "museum diorama: %s %s",
                       kDioramas[gDio->state.hoveredIndex].title,
                       gDio->state.paused[gDio->state.hoveredIndex] ? "paused" : "resumed");
        return true;
    }
    return intent != CeCinemaRoom::FireIntent::Ordinary;
}

bool RuntimeIsAimingControl() {
    return gDio && gDio->state.initialized && gDio->state.hoveredIndex >= 0;
}

bool RuntimeCue(float visitorX, float visitorY, float fwdX, float fwdY,
                CeMuseumTour::Wing wing,
                char *title, unsigned titleCap,
                char *line, unsigned lineCap,
                char *note, unsigned noteCap) {
    if (!gDio || !gDio->state.initialized || !title || !titleCap || !line || !lineCap) return false;
    int idx = gDio->state.hoveredIndex;
    const bool atStud = idx >= 0 && static_cast<unsigned>(idx) < kDioramaCount;
    if (!atStud) idx = FocusDiorama(visitorX, visitorY, fwdX, fwdY, wing);
    if (idx < 0 || static_cast<unsigned>(idx) >= kDioramaCount) return false;
    const DioramaSpec &ds = kDioramas[idx];
    std::snprintf(title, titleCap, "%s", ds.title);
    std::snprintf(line, lineCap, "%s", atStud ? ds.subtitle : ds.caption);
    if (note && noteCap) {
        if (atStud)
            std::snprintf(note, noteCap, "%s",
                          gDio->state.paused[idx] ? "Battle motion paused. Press Fire to resume."
                                                  : "Battle in progress. Press Fire to pause motion.");
        else
            std::snprintf(note, noteCap, "%s - aim at lectern to %s.",
                          ds.subtitle, gDio->state.paused[idx] ? "resume" : "pause");
    }
    return true;
}

void RuntimeDraw(float floorZ, bool ownStateBlock) {
    RuntimeData *d = EnsureData();
    IDirect3DDevice9 *dev = LT.GetDevice();
    if (!d || !dev) return;

    EnsureUnitAssetsLoaded(d, dev);
    EnsureStageGpuBuffers(d, dev, floorZ);

    IDirect3DStateBlock9 *saved = NULL;
    if (ownStateBlock) {
        const uint64_t gen = CurrentDioramaDeviceGen();
        if (d->stateBlock && (d->stateBlockDev != dev || d->stateBlockGen != gen)) {
            d->stateBlock->Release();
            d->stateBlock = NULL;
            d->stateBlockDev = NULL;
            d->stateBlockGen = 0u;
        }
        if (!d->stateBlock) {
            if (SUCCEEDED(dev->CreateStateBlock(D3DSBT_ALL, &d->stateBlock)) && d->stateBlock) {
                d->stateBlockDev = dev;
                d->stateBlockGen = gen;
            }
        }
        saved = d->stateBlock;
        if (!saved || FAILED(saved->Capture())) return;
    }

    D3DMATRIX id = {}, view = {}, proj = {};
    id._11 = id._22 = id._33 = id._44 = 1.0f;
    RENDERINFO.GetView(&view);
    RENDERINFO.GetProjection(&proj);
    dev->SetVertexShader(NULL);
    dev->SetPixelShader(NULL);
    dev->SetTransform(D3DTS_WORLD, &id);
    dev->SetTransform(D3DTS_VIEW, &view);
    dev->SetTransform(D3DTS_PROJECTION, &proj);
    dev->SetTexture(1, NULL);
    dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    dev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);

    dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    const float zero = 0.0f;
    dev->SetRenderState(D3DRS_DEPTHBIAS, *reinterpret_cast<const DWORD *>(&zero));
    dev->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, *reinterpret_cast<const DWORD *>(&zero));

    // -------------------------------------------------------------------------
    // 1. Soft radial floor contact shadows beneath all 4 circular drum pedestals
    // -------------------------------------------------------------------------
    if (d->domeVb && d->domeIb && d->shadowRange.triCount > 0u) {
        const float shBias = -0.00002f;
        dev->SetRenderState(D3DRS_DEPTHBIAS, *reinterpret_cast<const DWORD *>(&shBias));
        dev->SetRenderState(D3DRS_LIGHTING, FALSE);
        dev->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
        dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        dev->SetTexture(0, NULL);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
        dev->SetFVF(kDomeFvf);
        dev->SetStreamSource(0, d->domeVb, 0, sizeof(LitDomeVertex));
        dev->SetIndices(d->domeIb);
        dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, d->domeVertCount,
                                  d->shadowRange.firstIndex, d->shadowRange.triCount);
        dev->SetRenderState(D3DRS_DEPTHBIAS, *reinterpret_cast<const DWORD *>(&zero));
    }

    // -------------------------------------------------------------------------
    // 2. Per-diorama dramatic 3-light moods: sculpted 32-seg drums, brushed-brass
    //    arches & lecterns, 32x64 MODULATE2X terrains, and mipmapped miniature units!
    // -------------------------------------------------------------------------
    dev->SetRenderState(D3DRS_LIGHTING, TRUE);
    dev->SetRenderState(D3DRS_NORMALIZENORMALS, TRUE);
    dev->SetRenderState(D3DRS_COLORVERTEX, TRUE);
    dev->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);
    dev->SetRenderState(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_COLOR1);
    dev->SetRenderState(D3DRS_SPECULARMATERIALSOURCE, D3DMCS_MATERIAL);
    dev->SetRenderState(D3DRS_EMISSIVEMATERIALSOURCE, D3DMCS_MATERIAL);
    dev->SetRenderState(D3DRS_LOCALVIEWER, TRUE);
    dev->SetRenderState(D3DRS_SPECULARENABLE, TRUE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);

    IDirect3DTexture9 *slateTex = CeMuseumTextures::Get(CE_MUSEUM_TEX_SLATE_PIER);
    IDirect3DTexture9 *brassTex = CeMuseumTextures::Get(CE_MUSEUM_TEX_BRUSHED_BRASS);

    auto makeMat = [](float sr, float sg, float sb, float power) -> D3DMATERIAL9 {
        D3DMATERIAL9 m = {};
        m.Diffuse.r = m.Diffuse.g = m.Diffuse.b = m.Diffuse.a = 1.0f;
        m.Ambient = m.Diffuse;
        m.Specular.r = sr; m.Specular.g = sg; m.Specular.b = sb; m.Specular.a = 1.0f;
        m.Power = power;
        return m;
    };
    const D3DMATERIAL9 matSlate = makeMat(0.24f, 0.26f, 0.30f, 24.0f);
    const D3DMATERIAL9 matBrass = makeMat(0.95f, 0.80f, 0.46f, 44.0f);
    const D3DMATERIAL9 matUnit  = makeMat(0.38f, 0.40f, 0.44f, 28.0f);

    for (unsigned di = 0; di < kDioramaCount; ++di) {
        const DioramaSpec &ds = kDioramas[di];
        SetupDioramaLights(dev, ds.theme);

        // (a) Static stage slices for diorama di (slate drum/lectern, brass arches, micro-detail terrain)
        if (d->stageVb && d->stageIb && d->stageIdxCount >= 3u) {
            dev->SetTransform(D3DTS_WORLD, &id);
            dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
            dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
            dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
            dev->SetFVF(kStageFvf);
            dev->SetStreamSource(0, d->stageVb, 0, sizeof(LitStageVertex));
            dev->SetIndices(d->stageIb);
            dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

            // Slate drum pedestal & lectern body
            if (d->pedSlateRange[di].triCount > 0u) {
                dev->SetMaterial(&matSlate);
                dev->SetTexture(0, slateTex);
                dev->SetTextureStageState(0, D3DTSS_COLOROP, slateTex ? D3DTOP_MODULATE : D3DTOP_SELECTARG1);
                dev->SetTextureStageState(0, D3DTSS_COLORARG1, slateTex ? D3DTA_TEXTURE : D3DTA_DIFFUSE);
                dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
                dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, d->stageVertCount,
                                          d->pedSlateRange[di].firstIndex, d->pedSlateRange[di].triCount);
            }

            // Sleek brushed-brass frame, 4 slender cardinal arches, finial & lectern bezel
            if (d->pedBrassRange[di].triCount > 0u) {
                dev->SetMaterial(&matBrass);
                dev->SetTexture(0, brassTex);
                dev->SetTextureStageState(0, D3DTSS_COLOROP, brassTex ? D3DTOP_MODULATE : D3DTOP_SELECTARG1);
                dev->SetTextureStageState(0, D3DTSS_COLORARG1, brassTex ? D3DTA_TEXTURE : D3DTA_DIFFUSE);
                dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
                dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, d->stageVertCount,
                                          d->pedBrassRange[di].firstIndex, d->pedBrassRange[di].triCount);
            }

            // 32x64 sculpted terrain + cutaway strata rim modulated by 256x256 micro-relief detail map!
            if (d->terrainRange[di].triCount > 0u) {
                const D3DMATERIAL9 matTerr = (ds.theme == kThemeSkycarrier)
                    ? makeMat(0.55f, 0.65f, 0.78f, 36.0f)
                    : (ds.theme == kThemeVolcanic ? makeMat(0.28f, 0.22f, 0.20f, 20.0f)
                                                  : makeMat(0.18f, 0.20f, 0.20f, 16.0f));
                dev->SetMaterial(&matTerr);
                dev->SetTexture(0, d->terrainDetailTex);
                if (d->terrainDetailTex) {
                    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE2X);
                    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
                    dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
                } else {
                    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
                    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
                }
                dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, d->stageVertCount,
                                          d->terrainRange[di].firstIndex, d->terrainRange[di].triCount);
            }
        }

        // (b) Animated miniature 3D units for diorama di (full mipmaps + diecast metallic specular!)
        dev->SetMaterial(&matUnit);
        dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
        dev->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
        dev->SetRenderState(D3DRS_ALPHAREF, 8);
        dev->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        dev->SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1);

        const float wcx = CeMuseumWorld::kEntranceX + ds.localX;
        const float wcy = CeMuseumWorld::kEntranceY + ds.localY;
        const float topBase = floorZ - kTabletopZ;
        for (unsigned ui = 0; ui < ds.unitCount && ui < kMaxUnitsPerDiorama; ++ui) {
            const int midx = d->unitMeshMap[di][ui];
            if (midx < 0 || static_cast<unsigned>(midx) >= d->meshCount) continue;
            const UniqueMeshGpu &um = d->meshes[midx];
            if (!um.vb || !um.ib || um.batchCount == 0u) continue;

            const UnitSpec &us = ds.units[ui];
            UnitPose pose = {};
            EvaluateUnitPose(ds.theme, us, d->state.simTime[di], &pose);

            D3DMATRIX world = {};
            BuildUnitWorldMatrix(um, wcx + pose.dx, wcy + pose.dy, topBase - pose.dz,
                                 us.targetSpan, pose.yawRad, pose.pitchRad, pose.rollRad, &world);
            dev->SetTransform(D3DTS_WORLD, &world);
            dev->SetStreamSource(0, um.vb, 0, sizeof(ArchiveVertex));
            dev->SetIndices(um.ib);
            for (uint32_t bi = 0; bi < um.batchCount; ++bi) {
                const MeshBatchGpu &b = um.batches[bi];
                if (b.triangleCount == 0u) continue;
                IDirect3DTexture9 *tex = (b.sharedTexSlot >= 0 && static_cast<unsigned>(b.sharedTexSlot) < d->sharedTexCount)
                    ? d->sharedTex[b.sharedTexSlot].tex : NULL;
                dev->SetTexture(0, tex);
                if (tex) {
                    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
                    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
                    dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
                    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
                    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
                    dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
                } else {
                    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
                    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
                    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
                    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
                }
                dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, um.vertexCount, b.firstIndex, b.triangleCount);
            }
        }
    }

    // -------------------------------------------------------------------------
    // 3. Translucent interior environmental layers: water/foam, glowing magma, cloud decks
    // -------------------------------------------------------------------------
    if (d->domeVb && d->domeIb && d->domeIdxCount >= 3u) {
        dev->SetTransform(D3DTS_WORLD, &id);
        dev->SetRenderState(D3DRS_LIGHTING, FALSE);
        dev->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
        dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        dev->SetTexture(0, NULL);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
        dev->SetFVF(kDomeFvf);
        dev->SetStreamSource(0, d->domeVb, 0, sizeof(LitDomeVertex));
        dev->SetIndices(d->domeIb);

        if (d->waterRange.triCount > 0u) {
            dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
            dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
            dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, d->domeVertCount,
                                      d->waterRange.firstIndex, d->waterRange.triCount);
        }
        if (d->magmaRange.triCount > 0u) {
            dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
            dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
            dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, d->domeVertCount,
                                      d->magmaRange.firstIndex, d->magmaRange.triCount);
        }
        if (d->cloudRange.triCount > 0u) {
            dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
            dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
            dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, d->domeVertCount,
                                      d->cloudRange.firstIndex, d->cloudRange.triCount);
        }
    }

    // -------------------------------------------------------------------------
    // 4. Combat VFX (smoke puffs, multi-salvo tracers, 3-plane additive starbursts)
    //    & lectern status jewels - rendered strictly BEFORE the blown-glass dome!
    // -------------------------------------------------------------------------
    DrawDioramaVfx(dev, d, floorZ);

    // -------------------------------------------------------------------------
    // 5. Optical Blown-Glass Domes (48x16, front-face D3DCULL_CW only!):
    //    Pass A: ultra-clear ~4-8% tinted crystal shell (zero double-shell haze)
    //    Pass B: live view-dependent hardware specular skylight gleam (Power = 64)
    // -------------------------------------------------------------------------
    if (d->domeVb && d->domeIb && d->domeGlassRange.triCount > 0u) {
        dev->SetTransform(D3DTS_WORLD, &id);
        dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_CW);
        dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        dev->SetTexture(0, NULL);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
        dev->SetFVF(kDomeFvf);
        dev->SetStreamSource(0, d->domeVb, 0, sizeof(LitDomeVertex));
        dev->SetIndices(d->domeIb);

        // Pass A: Ultra-clear tinted blown-glass shell (front-face only)
        dev->SetRenderState(D3DRS_LIGHTING, FALSE);
        dev->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
        dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, d->domeVertCount,
                                  d->domeGlassRange.firstIndex, d->domeGlassRange.triCount);

        // Pass B: Live view-dependent hardware specular skylight reflection (additive ONE/ONE)
        auto setGlassLight = [&](DWORD idx, float dx, float dy, float dz, float sr, float sg, float sb) {
            const float l = std::sqrt(dx * dx + dy * dy + dz * dz);
            D3DLIGHT9 lt = {};
            lt.Type = D3DLIGHT_DIRECTIONAL;
            lt.Direction.x = dx / l; lt.Direction.y = dy / l; lt.Direction.z = dz / l;
            lt.Specular.r = sr; lt.Specular.g = sg; lt.Specular.b = sb; lt.Specular.a = 1.0f;
            dev->SetLight(idx, &lt);
            dev->LightEnable(idx, TRUE);
        };
        setGlassLight(0, -0.45f,  0.40f, 0.80f, 0.96f, 0.92f, 0.84f); // warm Atrium clerestory skylight
        setGlassLight(1,  0.60f, -0.48f, 0.64f, 0.42f, 0.56f, 0.72f); // secondary cool skylight bounce
        for (DWORD li = 2; li < 8; ++li) dev->LightEnable(li, FALSE);

        D3DMATERIAL9 glassMat = {};
        glassMat.Specular.r = 0.96f; glassMat.Specular.g = 0.91f; glassMat.Specular.b = 0.82f; glassMat.Specular.a = 1.0f;
        glassMat.Power = 64.0f;
        dev->SetMaterial(&glassMat);

        dev->SetRenderState(D3DRS_AMBIENT, D3DCOLOR_XRGB(0, 0, 0));
        dev->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_MATERIAL);
        dev->SetRenderState(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_MATERIAL);
        dev->SetRenderState(D3DRS_SPECULARMATERIALSOURCE, D3DMCS_MATERIAL);
        dev->SetRenderState(D3DRS_EMISSIVEMATERIALSOURCE, D3DMCS_MATERIAL);
        dev->SetRenderState(D3DRS_LIGHTING, TRUE);
        dev->SetRenderState(D3DRS_NORMALIZENORMALS, TRUE);
        dev->SetRenderState(D3DRS_SPECULARENABLE, TRUE);
        dev->SetRenderState(D3DRS_LOCALVIEWER, TRUE);
        dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
        dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
        dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, d->domeVertCount,
                                  d->domeGlassRange.firstIndex, d->domeGlassRange.triCount);
    }

    // Clean state teardown for sibling draws sharing the Museum Hall state block.
    dev->SetStreamSource(0, NULL, 0, 0);
    dev->SetIndices(NULL);
    dev->SetTexture(0, NULL);
    dev->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);
    dev->SetRenderState(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_COLOR1);
    dev->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
    dev->SetRenderState(D3DRS_LOCALVIEWER, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);

    if (saved) {
        saved->Apply();
        CeMuseumRestoreEngineRenderState();
    }
}

} // namespace CeMuseumDiorama

extern "C" void PortCeMuseumDioramaDeleteDeviceObjects(void) {
    if (CeMuseumDiorama::gDio) {
        CeMuseumDiorama::ReleaseUnitAssets(CeMuseumDiorama::gDio);
        CeMuseumDiorama::ReleaseStageBuffers(CeMuseumDiorama::gDio);
    }
}

extern "C" void PortCeMuseumDioramaInvalidateStateBlock(void) {
    CeMuseumDiorama::ReleaseDioramaStateBlock(CeMuseumDiorama::gDio);
}
#else
#include "ce_museum_diorama.h"
namespace CeMuseumDiorama {
void RuntimeInit() {}
void RuntimeShutdown() {}
void RuntimeUpdate(CeMuseumTour::Wing, float, bool, float, float, float, float, float, float) {}
bool RuntimeHandleFire(bool, float) { return false; }
bool RuntimeIsAimingControl() { return false; }
bool RuntimeCue(float, float, float, float, CeMuseumTour::Wing, char *, unsigned, char *, unsigned, char *, unsigned) { return false; }
void RuntimeDraw(float, bool) {}
} // namespace CeMuseumDiorama

extern "C" void PortCeMuseumDioramaInvalidateStateBlock(void) {}
#endif
