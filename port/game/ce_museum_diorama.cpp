// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_museum_diorama.cpp: Runtime GPU-buffered renderer for the Grand Museum's
// four glass-domed living tabletop battle dioramas. Loads miniature retail .msh.aya unit
// models via CeArchiveScanner (host-malloc SwapArena destroyed immediately after GPU VB/IB
// upload — zero retail heap allocations, zero leaks), degrades cleanly to procedural
// miniature silhouettes when retail files are absent, caches tables/terrain/geodesic domes
// in static D3DPOOL_MANAGED VBs/IBs, and animates units, 3D combat tracers & impact flashes.
#ifdef CE_VERSION
#include "common.h"
#include "ltshell.h"
#include "renderinfo.h"
#include "d3dx9.h"
#include "ce_asset_catalog.h"
#include "ce_archive_scanner.h"
#include "ce_museum_diorama.h"
#include "ce_museum_cinema.h"
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

constexpr unsigned kMaxUniqueMeshes = 36u;
constexpr unsigned kMaxSharedTextures = 96u;
constexpr unsigned kDioramaTexMaxEdge = 128u;
constexpr float kPi = 3.14159265359f;

struct LitStageVertex {
    float x, y, z;
    float nx, ny, nz;
    DWORD colour;
};
constexpr DWORD kStageFvf = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE;

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

struct RuntimeData {
    DioramaState state;
    IDirect3DDevice9 *device;
    uint64_t deviceGen;
    bool assetsAttempted;
    bool stageAttempted;
    float stageFloorZ;

    // Deduplicated miniature unit meshes & textures (all D3DPOOL_MANAGED).
    UniqueMeshGpu meshes[kMaxUniqueMeshes];
    unsigned meshCount;
    SharedTexEntry sharedTex[kMaxSharedTextures];
    unsigned sharedTexCount;
    int unitMeshMap[kDioramaCount][kMaxUnitsPerDiorama];

    // Static GPU buffers for all 4 tables + sculpted 3D terrains + brass dome ribs.
    IDirect3DVertexBuffer9 *stageVb;
    IDirect3DIndexBuffer9 *stageIb;
    unsigned stageVertCount;
    unsigned stageIdxCount;

    // Static GPU buffers for all 4 translucent crystal geodesic domes + skycarrier cloud rings.
    IDirect3DVertexBuffer9 *domeVb;
    IDirect3DIndexBuffer9 *domeIb;
    unsigned domeVertCount;
    unsigned domeIdxCount;
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
        }
    }
    return gDio;
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

void ReleaseStageBuffers(RuntimeData *d) {
    if (!d) return;
    if (d->stageVb) { d->stageVb->Release(); d->stageVb = NULL; }
    if (d->stageIb) { d->stageIb->Release(); d->stageIb = NULL; }
    if (d->domeVb)  { d->domeVb->Release();  d->domeVb = NULL; }
    if (d->domeIb)  { d->domeIb->Release();  d->domeIb = NULL; }
    d->stageVertCount = 0;
    d->stageIdxCount = 0;
    d->domeVertCount = 0;
    d->domeIdxCount = 0;
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
    const HRESULT hr = D3DXCreateTextureFromFileInMemoryEx(
        ctx->device, dds, static_cast<UINT>(bytes),
        static_cast<UINT>(w), static_cast<UINT>(ht),
        1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
        D3DX_FILTER_TRIANGLE, D3DX_FILTER_NONE, 0, NULL, NULL, &tex);
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

    auto pushBox = [&](float x0, float y0, float z0, float x1, float y1, float z1, DWORD col) {
        // z0 is lower (more positive in native Z-down), z1 is higher (more negative in native Z-down).
        if (vAt + 20u > 96u || iAt + 30u > 192u) return;
        auto face = [&](float ax, float ay, float az, float bx, float by, float bz,
                        float cx, float cy, float cz, float dx, float dy, float dz,
                        float nx, float ny, float nz) {
            const uint16_t b = static_cast<uint16_t>(vAt);
            verts[vAt++] = {ax, ay, az, nx, ny, nz, col, 0.0f, 0.0f};
            verts[vAt++] = {bx, by, bz, nx, ny, nz, col, 1.0f, 0.0f};
            verts[vAt++] = {cx, cy, cz, nx, ny, nz, col, 1.0f, 1.0f};
            verts[vAt++] = {dx, dy, dz, nx, ny, nz, col, 0.0f, 1.0f};
            indices[iAt++] = b + 0u; indices[iAt++] = b + 1u; indices[iAt++] = b + 2u;
            indices[iAt++] = b + 0u; indices[iAt++] = b + 2u; indices[iAt++] = b + 3u;
        };
        // Top (-Z)
        face(x0,y0,z1, x1,y0,z1, x1,y1,z1, x0,y1,z1,  0,0,-1);
        // Sides
        face(x0,y0,z0, x1,y0,z0, x1,y0,z1, x0,y0,z1,  0,-1,0);
        face(x1,y0,z0, x1,y1,z0, x1,y1,z1, x1,y0,z1,  1,0,0);
        face(x1,y1,z0, x0,y1,z0, x0,y1,z1, x1,y1,z1,  0,1,0);
        face(x0,y1,z0, x0,y0,z0, x0,y0,z1, x0,y1,z1, -1,0,0);
    };

    const DWORD hull = out->isForseti ? 0xff4a88c4u : 0xffb84830u;
    const DWORD trim = out->isForseti ? 0xffa8daf8u : 0xffe89c42u;
    const DWORD ice  = 0xffd8f0ffu;

    switch (out->fallback) {
    case kShapeJet:
        pushBox(-0.18f, -0.55f, 0.0f,  0.18f,  0.55f, -0.22f, hull);
        pushBox(-0.62f, -0.20f, -0.04f, 0.62f, 0.14f, -0.14f, trim);
        pushBox(-0.10f, -0.48f, -0.22f, 0.10f, -0.22f, -0.36f, trim);
        break;
    case kShapeShip:
        pushBox(-0.26f, -0.68f, 0.0f,  0.26f,  0.68f, -0.24f, hull);
        pushBox(-0.16f, -0.18f, -0.24f, 0.16f, 0.24f, -0.52f, trim);
        break;
    case kShapeWalker:
        pushBox(-0.26f, -0.22f, 0.0f,  -0.08f, 0.22f, -0.45f, hull);
        pushBox( 0.08f, -0.22f, 0.0f,   0.26f, 0.22f, -0.45f, hull);
        pushBox(-0.32f, -0.28f, -0.42f, 0.32f, 0.32f, -0.78f, trim);
        break;
    case kShapeFortress:
        pushBox(-0.52f, -0.52f, 0.0f,  0.52f,  0.52f, -0.38f, hull);
        pushBox(-0.28f, -0.28f, -0.38f, 0.28f, 0.28f, -0.72f, trim);
        break;
    case kShapeIceberg:
        pushBox(-0.54f, -0.48f, 0.0f,  0.54f,  0.48f, -0.44f, ice);
        pushBox(-0.30f, -0.26f, -0.44f, 0.24f, 0.30f, -0.92f, 0xffeef9ffu);
        break;
    case kShapeTank:
    default:
        pushBox(-0.36f, -0.48f, 0.0f,  0.36f,  0.48f, -0.26f, hull);
        pushBox(-0.20f, -0.18f, -0.26f, 0.20f, 0.20f, -0.46f, trim);
        pushBox(-0.05f,  0.18f, -0.34f, 0.05f, 0.56f, -0.42f, trim);
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
                   "museum dioramas: prepared %u retail + %u fallback miniature meshes (%u shared textures) across 4 tables",
                   retailOk, fallbackOk, d->sharedTexCount);
}

// =============================================================================
// Static GPU Stage Builder: 4 Ornate Octagonal Tables, 3D Relief Terrains & Geodesic Domes
// =============================================================================
struct StageBuilder {
    LitStageVertex *verts;
    uint32_t *indices;
    unsigned vCap, iCap, vUsed, iUsed;

    uint32_t PushV(float wx, float wy, float wz, float nx, float ny, float nz, DWORD col) {
        if (vUsed >= vCap) return 0u;
        const uint32_t idx = vUsed++;
        verts[idx] = {wx, wy, wz, nx, ny, nz, col};
        return idx;
    }
    void PushTri(uint32_t a, uint32_t b, uint32_t c) {
        if (iUsed + 3u > iCap) return;
        indices[iUsed++] = a;
        indices[iUsed++] = b;
        indices[iUsed++] = c;
    }
    void PushQuad(float ax, float ay, float az, float bx, float by, float bz,
                  float cx, float cy, float cz, float dx, float dy, float dz,
                  float nx, float ny, float nz, DWORD colTop, DWORD colBot) {
        if (vUsed + 4u > vCap || iUsed + 6u > iCap) return;
        const uint32_t i0 = PushV(ax, ay, az, nx, ny, nz, colBot);
        const uint32_t i1 = PushV(bx, by, bz, nx, ny, nz, colBot);
        const uint32_t i2 = PushV(cx, cy, cz, nx, ny, nz, colTop);
        const uint32_t i3 = PushV(dx, dy, dz, nx, ny, nz, colTop);
        PushTri(i0, i1, i2);
        PushTri(i0, i2, i3);
    }
    void OctagonTier(float wcx, float wcy, float floorZ, float rBot, float rTop,
                     float z0, float z1, DWORD topCol, DWORD sideCol, bool drawTop) {
        const unsigned sides = 8u;
        const float off = kPi / 8.0f;
        const float step = 2.0f * kPi / float(sides);
        float bx[8], by[8], tx[8], ty[8];
        for (unsigned i = 0; i < sides; ++i) {
            const float a = off + float(i) * step;
            const float ca = std::cos(a), sa = std::sin(a);
            bx[i] = wcx + ca * rBot; by[i] = wcy + sa * rBot;
            tx[i] = wcx + ca * rTop; ty[i] = wcy + sa * rTop;
        }
        const float wz0 = floorZ - z0;
        const float wz1 = floorZ - z1;
        if (drawTop) {
            const uint32_t ctr = PushV(wcx, wcy, wz1, 0.0f, 0.0f, -1.0f, topCol);
            uint32_t ring[8];
            for (unsigned i = 0; i < sides; ++i)
                ring[i] = PushV(tx[i], ty[i], wz1, 0.0f, 0.0f, -1.0f, topCol);
            for (unsigned i = 0; i < sides; ++i)
                PushTri(ctr, ring[i], ring[(i + 1u) % sides]);
        }
        for (unsigned i = 0; i < sides; ++i) {
            const unsigned j = (i + 1u) % sides;
            const float mx = 0.5f * (bx[i] + bx[j]) - wcx;
            const float my = 0.5f * (by[i] + by[j]) - wcy;
            const float ml = std::sqrt(mx * mx + my * my);
            const float nx = ml > 1e-5f ? mx / ml : 1.0f;
            const float ny = ml > 1e-5f ? my / ml : 0.0f;
            PushQuad(bx[i], by[i], wz0, bx[j], by[j], wz0,
                     tx[j], ty[j], wz1, tx[i], ty[i], wz1,
                     nx, ny, 0.0f, topCol, sideCol);
        }
    }
    void TubeSegment(float ax, float ay, float az, float bx, float by, float bz,
                     float halfW, DWORD col) {
        const float dx = bx - ax, dy = by - ay, dz = bz - az;
        const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (len < 1e-4f) return;
        // Build two orthogonal side offsets for a sleek 3D brass strut rib.
        float ux = -dy, uy = dx, uz = 0.0f;
        float ul = std::sqrt(ux * ux + uy * uy);
        if (ul < 1e-4f) { ux = 1.0f; uy = 0.0f; ul = 1.0f; }
        ux = (ux / ul) * halfW; uy = (uy / ul) * halfW;
        const float vx = (dy * uz - dz * uy) / len;
        const float vy = (dz * ux - dx * uz) / len;
        const float vz = (dx * uy - dy * ux) / len;
        PushQuad(ax - ux, ay - uy, az - uz, bx - ux, by - uy, bz - uz,
                 bx + ux, by + uy, bz + uz, ax + ux, ay + uy, az + uz,
                 0.0f, 0.0f, -1.0f, col, col);
        PushQuad(ax - vx, ay - vy, az - vz, bx - vx, by - vy, bz - vz,
                 bx + vx, by + vy, bz + vz, ax + vx, ay + vy, az + vz,
                 0.0f, 1.0f, 0.0f, col, col);
    }
};

DWORD TerrainColour(Theme theme, float rx, float ry, float h) {
    switch (theme) {
    case kThemeCoastal:
        if (h <= 0.004f) return 0xff1e7fa8u;      // sparkling turquoise bay water
        if (h < 0.045f)  return 0xffd2be88u;      // sunlit coastal beachhead sand
        if (h < 0.16f)   return 0xff3b7d48u;      // emerald island meadow
        return 0xff5a6658u;                       // fortified coastal ridge stone
    case kThemeVolcanic: {
        const float canyon = std::fabs(rx * 0.8f - ry * 0.6f + 0.25f * std::sin(ry * 2.5f));
        if (canyon < 0.11f) return 0xffff6a1eu;   // glowing molten lava vein!
        if (canyon < 0.18f) return 0xffa8381cu;   // scorching volcanic fissure rim
        if (h > 0.22f)      return 0xff463e42u;   // high obsidian basalt crest
        return 0xff2b282cu;                       // dark forged basalt plateau
    }
    case kThemeSkycarrier:
        return (h > 0.020f) ? 0xff245a8cu : 0xff163e68u; // deep sapphire sea swell
    case kThemeArctic:
        if (h <= 0.012f) return 0xff143452u;      // dark polar arctic sea
        if (h < 0.10f)   return 0xff8ecae6u;      // crystalline pale-cyan ice shelf
        return 0xffe4f3fcu;                       // snow-capped polar glacier peak
    default:
        return 0xff606870u;
    }
}

void EmitDioramaTerrain(StageBuilder &sb, Theme theme, float wcx, float wcy, float floorZ) {
    // Polar disk grid: 10 concentric rings x 24 angular segments = 240 quads per table!
    constexpr unsigned kRings = 10u;
    constexpr unsigned kSegs = 24u;
    const uint32_t base = sb.vUsed;
    const float topBase = floorZ - kTabletopZ;
    const float ctrH = TerrainHeight(theme, 0.0f, 0.0f);
    sb.PushV(wcx, wcy, topBase - ctrH, 0.0f, 0.0f, -1.0f, TerrainColour(theme, 0.0f, 0.0f, ctrH));

    for (unsigned r = 1; r <= kRings; ++r) {
        const float rad = kTerrainRadius * (float(r) / float(kRings));
        for (unsigned s = 0; s < kSegs; ++s) {
            const float a = float(s) * (2.0f * kPi / float(kSegs));
            const float rx = std::cos(a) * rad;
            const float ry = std::sin(a) * rad;
            const float h = TerrainHeight(theme, rx, ry);
            // Finite-difference surface normal (native -Z is up).
            const float eps = 0.06f;
            const float hx = TerrainHeight(theme, rx + eps, ry) - TerrainHeight(theme, rx - eps, ry);
            const float hy = TerrainHeight(theme, rx, ry + eps) - TerrainHeight(theme, rx, ry - eps);
            float nx = -hx / (2.0f * eps);
            float ny = -hy / (2.0f * eps);
            float nz = -1.0f;
            const float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
            nx /= nl; ny /= nl; nz /= nl;
            sb.PushV(wcx + rx, wcy + ry, topBase - h, nx, ny, nz, TerrainColour(theme, rx, ry, h));
        }
    }
    // Inner fan
    for (unsigned s = 0; s < kSegs; ++s) {
        const uint32_t v1 = base + 1u + s;
        const uint32_t v2 = base + 1u + ((s + 1u) % kSegs);
        sb.PushTri(base, v1, v2);
    }
    // Concentric rings
    for (unsigned r = 1; r < kRings; ++r) {
        const uint32_t r0 = base + 1u + (r - 1u) * kSegs;
        const uint32_t r1 = base + 1u + r * kSegs;
        for (unsigned s = 0; s < kSegs; ++s) {
            const unsigned sn = (s + 1u) % kSegs;
            sb.PushTri(r0 + s, r1 + s, r1 + sn);
            sb.PushTri(r0 + s, r1 + sn, r0 + sn);
        }
    }
}

void EmitDioramaDomeRibs(StageBuilder &sb, float wcx, float wcy, float floorZ) {
    // 8 delicate brushed-brass geodesic meridian ribs + equatorial collar + upper crown ring!
    constexpr unsigned kMeridians = 8u;
    constexpr unsigned kSteps = 6u;
    const float domeH = kDomeTopZ - kTabletopZ;
    const DWORD brass = 0xffd8b262u;
    for (unsigned m = 0; m < kMeridians; ++m) {
        const float ang = (kPi / 8.0f) + float(m) * (2.0f * kPi / float(kMeridians));
        const float ca = std::cos(ang), sa = std::sin(ang);
        for (unsigned s = 0; s < kSteps; ++s) {
            const float p0 = (float(s) / float(kSteps)) * (0.5f * kPi);
            const float p1 = (float(s + 1u) / float(kSteps)) * (0.5f * kPi);
            const float r0 = kDomeRadius * std::cos(p0), z0 = kTabletopZ + domeH * std::sin(p0);
            const float r1 = kDomeRadius * std::cos(p1), z1 = kTabletopZ + domeH * std::sin(p1);
            sb.TubeSegment(wcx + ca * r0, wcy + sa * r0, floorZ - z0,
                           wcx + ca * r1, wcy + sa * r1, floorZ - z1, 0.024f, brass);
        }
    }
    // Horizontal brass latitude ring at 45 deg elevation + brass crown finial ring
    for (unsigned ring = 0; ring < 2u; ++ring) {
        const float phi = ring == 0u ? 0.55f : 1.32f;
        const float rad = kDomeRadius * std::cos(phi);
        const float wz = floorZ - (kTabletopZ + domeH * std::sin(phi));
        for (unsigned m = 0; m < kMeridians; ++m) {
            const float a0 = (kPi / 8.0f) + float(m) * (2.0f * kPi / float(kMeridians));
            const float a1 = (kPi / 8.0f) + float((m + 1u) % kMeridians) * (2.0f * kPi / float(kMeridians));
            sb.TubeSegment(wcx + std::cos(a0) * rad, wcy + std::sin(a0) * rad, wz,
                           wcx + std::cos(a1) * rad, wcy + std::sin(a1) * rad, wz, 0.020f, brass);
        }
    }
    // Crown brass finial button atop dome apex
    sb.OctagonTier(wcx, wcy, floorZ, 0.14f, 0.06f, kDomeTopZ - 0.02f, kDomeTopZ + 0.10f, 0xffe6c474u, 0xffaa843cu, true);
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

    // 1. Build opaque tables + 3D terrains + geodesic brass rib cages for all 4 dioramas.
    const unsigned kMaxStageVerts = 12000u;
    const unsigned kMaxStageIdx = 36000u;
    LitStageVertex *sv = static_cast<LitStageVertex *>(std::malloc(kMaxStageVerts * sizeof(LitStageVertex)));
    uint32_t *si = static_cast<uint32_t *>(std::malloc(kMaxStageIdx * sizeof(uint32_t)));
    if (sv && si) {
        StageBuilder sb = {sv, si, kMaxStageVerts, kMaxStageIdx, 0u, 0u};
        for (unsigned di = 0; di < kDioramaCount; ++di) {
            const DioramaSpec &ds = kDioramas[di];
            const float wcx = CeMuseumWorld::kEntranceX + ds.localX;
            const float wcy = CeMuseumWorld::kEntranceY + ds.localY;
            const DWORD accent = (ds.theme == kThemeCoastal || ds.theme == kThemeSkycarrier)
                ? 0xff5ce0ffu : (ds.theme == kThemeVolcanic ? 0xffff8830u : 0xff9ce8ffu);

            // Ornate octagonal slate-and-brass command table pedestal
            sb.OctagonTier(wcx, wcy, floorZ, 1.72f, 1.56f, 0.02f, 0.18f, 0xff46525eu, 0xff2e363eu, true);
            sb.OctagonTier(wcx, wcy, floorZ, 1.24f, 1.06f, 0.18f, 0.80f, 0xff36404au, 0xff242b32u, false);
            sb.OctagonTier(wcx, wcy, floorZ, 1.98f, 2.05f, 0.80f, 0.90f, 0xff4c5864u, 0xff303842u, false);
            sb.OctagonTier(wcx, wcy, floorZ, 2.06f, 2.06f, 0.90f, 0.95f, accent,      accent,      false);
            sb.OctagonTier(wcx, wcy, floorZ, 2.08f, 2.05f, 0.95f, 1.06f, 0xffd6b064u, 0xff9a783au, true);
            // Recessed inner brass bezel lip framing the terrain dish
            sb.OctagonTier(wcx, wcy, floorZ, 1.88f, 1.88f, 1.04f, 1.08f, 0xff222a32u, 0xff181e24u, true);

            EmitDioramaTerrain(sb, ds.theme, wcx, wcy, floorZ);
            EmitDioramaDomeRibs(sb, wcx, wcy, floorZ);
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

    // 2. Build translucent crystal geodesic dome glass panes + skycarrier cloud rings!
    const unsigned kMaxDomeVerts = 2048u;
    const unsigned kMaxDomeIdx = 6144u;
    FlatVfxVertex *dv = static_cast<FlatVfxVertex *>(std::malloc(kMaxDomeVerts * sizeof(FlatVfxVertex)));
    uint16_t *diBuf = static_cast<uint16_t *>(std::malloc(kMaxDomeIdx * sizeof(uint16_t)));
    if (dv && diBuf) {
        unsigned dvUsed = 0u, diUsed = 0u;
        constexpr unsigned kSegs = 16u;
        constexpr unsigned kRings = 4u;
        const float domeH = kDomeTopZ - kTabletopZ;
        for (unsigned dIdx = 0; dIdx < kDioramaCount; ++dIdx) {
            const DioramaSpec &ds = kDioramas[dIdx];
            const float wcx = CeMuseumWorld::kEntranceX + ds.localX;
            const float wcy = CeMuseumWorld::kEntranceY + ds.localY;

            // Skycarrier diorama gets two translucent silver-white atmospheric cloud rings!
            if (ds.theme == kThemeSkycarrier) {
                for (unsigned cr = 0; cr < 2u; ++cr) {
                    const float cz = floorZ - (kTabletopZ + (cr == 0u ? 0.28f : 0.44f));
                    const float rIn = cr == 0u ? 0.55f : 0.95f;
                    const float rOut = cr == 0u ? 1.48f : 1.72f;
                    const uint16_t cbase = static_cast<uint16_t>(dvUsed);
                    for (unsigned s = 0; s < kSegs; ++s) {
                        const float a = float(s) * (2.0f * kPi / float(kSegs));
                        const float ca = std::cos(a), sa = std::sin(a);
                        const float wobble = 0.08f * std::sin(a * 3.0f + float(cr) * 1.4f);
                        dv[dvUsed++] = {wcx + ca * rIn,  wcy + sa * rIn,  cz, 0x38eaf4ffu};
                        dv[dvUsed++] = {wcx + ca * (rOut + wobble), wcy + sa * (rOut + wobble), cz, 0x10dceeffu};
                    }
                    for (unsigned s = 0; s < kSegs; ++s) {
                        const unsigned sn = (s + 1u) % kSegs;
                        const uint16_t a0 = static_cast<uint16_t>(cbase + s * 2u);
                        const uint16_t b0 = static_cast<uint16_t>(cbase + sn * 2u);
                        diBuf[diUsed++] = a0; diBuf[diUsed++] = a0 + 1u; diBuf[diUsed++] = b0 + 1u;
                        diBuf[diUsed++] = a0; diBuf[diUsed++] = b0 + 1u; diBuf[diUsed++] = b0;
                    }
                }
            }

            // Geodesic glass hemisphere with Fresnel-style rim gleam (clear viewing inside!).
            const uint16_t base = static_cast<uint16_t>(dvUsed);
            for (unsigned r = 0; r <= kRings; ++r) {
                const float phi = (float(r) / float(kRings)) * (0.5f * kPi);
                const float rad = kDomeRadius * std::cos(phi);
                const float wz = floorZ - (kTabletopZ + domeH * std::sin(phi));
                // Lower equator rim has crisp specular glass tint; mid dome is crystal clear.
                const DWORD col = (r == 0u) ? 0x36c8e8ffu : (r == 1u ? 0x20d8f2ffu : (r == 2u ? 0x18e4f6ffu : 0x24f2faffu));
                for (unsigned s = 0; s < kSegs; ++s) {
                    const float a = float(s) * (2.0f * kPi / float(kSegs));
                    dv[dvUsed++] = {wcx + std::cos(a) * rad, wcy + std::sin(a) * rad, wz, col};
                }
            }
            for (unsigned r = 0; r < kRings; ++r) {
                const uint16_t r0 = static_cast<uint16_t>(base + r * kSegs);
                const uint16_t r1 = static_cast<uint16_t>(base + (r + 1u) * kSegs);
                for (unsigned s = 0; s < kSegs; ++s) {
                    const unsigned sn = (s + 1u) % kSegs;
                    diBuf[diUsed++] = r0 + s; diBuf[diUsed++] = r0 + sn; diBuf[diUsed++] = r1 + sn;
                    diBuf[diUsed++] = r0 + s; diBuf[diUsed++] = r1 + sn; diBuf[diUsed++] = r1 + s;
                }
            }
        }
        if (dvUsed >= 3u && diUsed >= 3u) {
            if (UploadManagedVb(dev, dv, dvUsed * sizeof(FlatVfxVertex), kFlatFvf, &d->domeVb) &&
                UploadManagedIb(dev, diBuf, diUsed * sizeof(uint16_t), D3DFMT_INDEX16, &d->domeIb)) {
                d->domeVertCount = dvUsed;
                d->domeIdxCount = diUsed;
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

void DrawStudsAndTracers(IDirect3DDevice9 *dev, const RuntimeData *d, float floorZ) {
    // Batch all 4 diorama brass pause/resume studs + live 3D laser/pulse/artillery tracers + flashes!
    FlatVfxVertex batch[2400];
    unsigned used = 0u;

    auto pushBox = [&](float cx, float cy, float cz, float hx, float hy, float hz, DWORD c) {
        if (used + 30u > 2400u) return;
        const float x0 = cx - hx, x1 = cx + hx, y0 = cy - hy, y1 = cy + hy, z0 = cz - hz, z1 = cz + hz;
        const FlatVfxVertex tris[30] = {
            {x0,y0,z0,c},{x1,y0,z0,c},{x1,y1,z0,c}, {x0,y0,z0,c},{x1,y1,z0,c},{x0,y1,z0,c},
            {x0,y0,z1,c},{x1,y0,z1,c},{x1,y0,z0,c}, {x0,y0,z1,c},{x1,y0,z0,c},{x0,y0,z0,c},
            {x1,y0,z1,c},{x1,y1,z1,c},{x1,y1,z0,c}, {x1,y0,z1,c},{x1,y1,z0,c},{x1,y0,z0,c},
            {x1,y1,z1,c},{x0,y1,z1,c},{x0,y1,z0,c}, {x1,y1,z1,c},{x0,y1,z0,c},{x1,y1,z0,c},
            {x0,y1,z1,c},{x0,y0,z1,c},{x0,y0,z0,c}, {x0,y1,z1,c},{x0,y0,z0,c},{x0,y1,z0,c},
        };
        std::memcpy(&batch[used], tris, sizeof(tris));
        used += 30u;
    };

    auto pushBeam = [&](float ax, float ay, float az, float bx, float by, float bz, float r, DWORD col) {
        if (used + 12u > 2400u) return;
        const float dx = bx - ax, dy = by - ay;
        float px = -dy, py = dx;
        const float pl = std::sqrt(px * px + py * py);
        if (pl > 1e-4f) { px = (px / pl) * r; py = (py / pl) * r; }
        else { px = r; py = 0.0f; }
        // Crossed horizontal & vertical luminous beam ribbons so the streak reads sharply from every angle!
        batch[used++] = {ax - px, ay - py, az, col}; batch[used++] = {bx - px, by - py, bz, col}; batch[used++] = {bx + px, by + py, bz, col};
        batch[used++] = {ax - px, ay - py, az, col}; batch[used++] = {bx + px, by + py, bz, col}; batch[used++] = {ax + px, ay + py, az, col};
        batch[used++] = {ax, ay, az - r, col}; batch[used++] = {bx, by, bz - r, col}; batch[used++] = {bx, by, bz + r, col};
        batch[used++] = {ax, ay, az - r, col}; batch[used++] = {bx, by, bz + r, col}; batch[used++] = {ax, ay, az + r, col};
    };

    // 1. Interactive brass pause/resume stud stanchions for all 4 tables
    for (unsigned di = 0; di < kDioramaCount; ++di) {
        float sx = 0.0f, sy = 0.0f, sz = 0.0f;
        StudPosition(di, &sx, &sy, &sz);
        const float wx = CeMuseumWorld::kEntranceX + sx;
        const float wy = CeMuseumWorld::kEntranceY + sy;
        const float wz = floorZ - sz;
        const bool hovered = (d->state.hoveredIndex == static_cast<int>(di));
        const bool paused = d->state.paused[di];
        const DWORD cap = hovered ? 0xfffff098u : (paused ? 0xff5ea4dcu : 0xffe6bc62u);
        pushBox(wx, wy, floorZ - 0.06f, 0.12f, 0.12f, 0.06f, 0xffb8924cu);
        pushBox(wx, wy, floorZ - 0.54f, 0.065f, 0.065f, 0.44f, 0xff4c5864u);
        pushBox(wx, wy, wz, hovered ? 0.14f : 0.11f, hovered ? 0.14f : 0.11f, 0.06f, cap);
    }

    // 2. Live 3D combat tracers & miniature impact flashes inside each crystal dome
    for (unsigned di = 0; di < kDioramaCount; ++di) {
        const DioramaSpec &ds = kDioramas[di];
        const float wcx = CeMuseumWorld::kEntranceX + ds.localX;
        const float wcy = CeMuseumWorld::kEntranceY + ds.localY;
        const float topBase = floorZ - kTabletopZ;
        for (unsigned ti = 0; ti < ds.tracerCount && ti < kMaxTracersPerDiorama; ++ti) {
            TracerSegment seg = {};
            EvaluateTracer(ds, ds.tracers[ti], d->state.simTime[di], &seg);
            if (seg.active) {
                pushBeam(wcx + seg.ax, wcy + seg.ay, topBase - seg.az,
                         wcx + seg.bx, wcy + seg.by, topBase - seg.bz,
                         0.018f, seg.colour);
            }
            if (seg.impactAlpha > 0.05f) {
                const float r = 0.04f + 0.09f * (1.0f - seg.impactAlpha);
                const DWORD flashCol = (static_cast<DWORD>(seg.impactAlpha * 240.0f) << 24) |
                                       (seg.colour & 0x00ffffffu);
                pushBox(wcx + seg.impactX, wcy + seg.impactY, topBase - seg.impactZ, r, r, r, flashCol);
            }
        }
    }

    if (used >= 3u) {
        D3DMATRIX id = {};
        id._11 = id._22 = id._33 = id._44 = 1.0f;
        dev->SetTransform(D3DTS_WORLD, &id);
        dev->SetRenderState(D3DRS_LIGHTING, FALSE);
        dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        dev->SetTexture(0, NULL);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
        dev->SetStreamSource(0, NULL, 0, 0);
        dev->SetIndices(NULL);
        dev->SetFVF(kFlatFvf);
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
    std::free(gDio);
    gDio = NULL;
}

void RuntimeUpdate(CeMuseumTour::Wing wing, float dtSeconds, bool paused,
                   float eyeX, float eyeY, float eyeZ,
                   float aimX, float aimY, float aimZ) {
    RuntimeData *d = EnsureData();
    if (!d) return;
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
            std::snprintf(note, noteCap, "%s - aim at the brass stud to %s battle motion.",
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
        if (FAILED(dev->CreateStateBlock(D3DSBT_ALL, &saved)) || !saved) return;
        if (FAILED(saved->Capture())) { saved->Release(); return; }
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

    // Balanced 3-point gallery lighting for tables, terrains, brass dome ribs, and miniature units.
    dev->SetRenderState(D3DRS_AMBIENT, D3DCOLOR_XRGB(86, 92, 102));
    auto setLight = [&](DWORD idx, float dx, float dy, float dz, float r, float g, float b) {
        const float l = std::sqrt(dx * dx + dy * dy + dz * dz);
        D3DLIGHT9 lt = {};
        lt.Type = D3DLIGHT_DIRECTIONAL;
        lt.Direction.x = dx / l; lt.Direction.y = dy / l; lt.Direction.z = dz / l;
        lt.Diffuse.r = r; lt.Diffuse.g = g; lt.Diffuse.b = b; lt.Diffuse.a = 1.0f;
        lt.Ambient.r = 0.16f * r; lt.Ambient.g = 0.16f * g; lt.Ambient.b = 0.16f * b; lt.Ambient.a = 1.0f;
        dev->SetLight(idx, &lt);
        dev->LightEnable(idx, TRUE);
    };
    setLight(0, -0.55f,  0.52f, 0.65f, 0.98f, 0.90f, 0.78f);
    setLight(1,  0.65f, -0.44f, 0.56f, 0.48f, 0.60f, 0.80f);
    setLight(2,  0.00f,  0.15f, 0.96f, 0.42f, 0.44f, 0.48f);
    for (DWORD li = 3; li < 8; ++li) dev->LightEnable(li, FALSE);

    D3DMATERIAL9 mat = {};
    mat.Diffuse.r = mat.Diffuse.g = mat.Diffuse.b = mat.Diffuse.a = 1.0f;
    mat.Ambient = mat.Diffuse;
    dev->SetMaterial(&mat);

    dev->SetRenderState(D3DRS_LIGHTING, TRUE);
    dev->SetRenderState(D3DRS_NORMALIZENORMALS, TRUE);
    dev->SetRenderState(D3DRS_COLORVERTEX, TRUE);
    dev->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);
    dev->SetRenderState(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_COLOR1);
    dev->SetRenderState(D3DRS_SPECULARMATERIALSOURCE, D3DMCS_MATERIAL);
    dev->SetRenderState(D3DRS_EMISSIVEMATERIALSOURCE, D3DMCS_MATERIAL);
    dev->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
    dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    const float zero = 0.0f;
    dev->SetRenderState(D3DRS_DEPTHBIAS, *reinterpret_cast<const DWORD *>(&zero));
    dev->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, *reinterpret_cast<const DWORD *>(&zero));

    // 1. Draw all 4 ornate octagonal command tables, 3D sculpted terrains & brass dome ribs in ONE GPU draw call!
    if (d->stageVb && d->stageIb && d->stageIdxCount >= 3u) {
        dev->SetTexture(0, NULL);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        dev->SetFVF(kStageFvf);
        dev->SetStreamSource(0, d->stageVb, 0, sizeof(LitStageVertex));
        dev->SetIndices(d->stageIb);
        dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, d->stageVertCount, 0, d->stageIdxCount / 3u);
    }

    // 2. Draw all animated miniature 3D units from static GPU VBs/IBs!
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
    dev->SetRenderState(D3DRS_ALPHAREF, 8);
    dev->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    dev->SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1);

    for (unsigned di = 0; di < kDioramaCount; ++di) {
        const DioramaSpec &ds = kDioramas[di];
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

    // 3. Draw interactive brass control studs + live 3D combat tracers & impact flashes!
    DrawStudsAndTracers(dev, d, floorZ);

    // 4. Draw all 4 translucent crystal geodesic domes + cloud rings in ONE static GPU draw call!
    if (d->domeVb && d->domeIb && d->domeIdxCount >= 3u) {
        dev->SetTransform(D3DTS_WORLD, &id);
        dev->SetRenderState(D3DRS_LIGHTING, FALSE);
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
        dev->SetFVF(kFlatFvf);
        dev->SetStreamSource(0, d->domeVb, 0, sizeof(FlatVfxVertex));
        dev->SetIndices(d->domeIb);
        dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, d->domeVertCount, 0, d->domeIdxCount / 3u);
    }

    dev->SetStreamSource(0, NULL, 0, 0);
    dev->SetIndices(NULL);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);

    if (saved) {
        saved->Apply();
        saved->Release();
        LT.InvalidateAllStates();
        RENDERINFO.mWorldChanged = true;
        RENDERINFO.mViewChanged = true;
        RENDERINFO.mProjectionChanged = true;
        RENDERINFO.mFVFChanged = true;
        RENDERINFO.Apply();
    }
}

} // namespace CeMuseumDiorama
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
#endif
