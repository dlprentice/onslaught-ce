// SPDX-License-Identifier: GPL-3.0-only
#include "common.h"
#define DAMAGE_FULL
#include <cstdlib>
#include <cstring>
#include "DX.h"
#include "ltshell.h"
#include "tweak.h"
#include "landscape.h"
#include "DXLandscape.h"
#include "LandscapeTexture.h"
#include "engine.h"
#include "map.h"
#include "maptex.h"
#include "ce_terrain_textures.h"
#include "ce_terrain_textures_math.h"
#include "../platform/common/terrain_quality.h"
#include "../platform/common/portlog.h"

extern CTweakSLONG LANDSCAPE_LOWRES;
extern "C" bool PortCeWorldViewCaptureSuspended();

namespace {
static const int kMaxViews = 2;
static const int kMaxTiles = kCeTerrainMapTiles * kCeTerrainMapTiles;

struct LevelAtlas {
    IDirect3DTexture9 *gpuTex = NULL;
    IDirect3DTexture9 *sysTex = NULL;
    unsigned short *cache = NULL;
    unsigned short *queuedTile = NULL;
    unsigned short *dirtySlots = NULL;
    int atlasPx = 0;
    int tilePx = 0;
    int tilesPerAxis = 0;
    int cacheCount = 0;
    int dirtyCount = 0;
    float uvScale = 1.0f / 512.0f;
};

struct ViewState {
    int numLevels = 0;
    bool active = false;
    CeTerrainViewFocus focus = {};
    LevelAtlas atlases[kCeTerrainMaxLevels];
    unsigned short *drawTiles[kCeTerrainMaxLevels] = {};
    int drawCount[kCeTerrainMaxLevels] = {};
};

ViewState gViews[kMaxViews];
int gNumViews = 0;
unsigned gMaxAtlas = 0;
bool gRegistered = false;
bool gAttempted = false;
bool gPrepared = false;

void BlendDamageAlpha(UWORD *dst, SINT pitch, UBYTE *alpha, SINT x, SINT y, SINT shift, SINT limit)
{
    const SINT size = 1 << shift;
    SINT w = size;
    SINT h = size;
    if (x < 0) {
        w = size + x;
        alpha -= x;
    } else {
        if (x + size > limit) w = limit - x;
        dst += x;
    }
    if (y < 0) {
        h = size + y;
        alpha -= size * y;
    } else {
        if (y + size > limit) h = limit - y;
        dst += pitch * y;
    }
    if (w <= 0 || h <= 0) return;
    for (; h > 0; --h) {
        UBYTE *a = alpha;
        UWORD *d = dst;
        for (SINT i = w; i > 0; --i) {
            const SINT v = *a;
            if (v < 32) {
                DWORD c = *d;
                c = (c + (c << 16)) & 0x7e0f81fu;
                c = ((c * static_cast<DWORD>(v)) >> 5) & 0x7e0f81fu;
                *d = static_cast<UWORD>((c >> 16) + c);
            }
            ++a;
            ++d;
        }
        dst += pitch;
        alpha += size;
    }
}

void ReleaseAtlasGpu(LevelAtlas &a)
{
    if (a.gpuTex) {
        a.gpuTex->Release();
        a.gpuTex = NULL;
    }
}

void DestroyAtlas(LevelAtlas &a)
{
    ReleaseAtlasGpu(a);
    if (a.sysTex) {
        a.sysTex->Release();
        a.sysTex = NULL;
    }
    std::free(a.cache);
    std::free(a.queuedTile);
    std::free(a.dirtySlots);
    a = LevelAtlas{};
}

void ClearDrawLists()
{
    for (int v = 0; v < kMaxViews; ++v) {
        gViews[v].active = false;
        for (int l = 0; l < kCeTerrainMaxLevels; ++l)
            gViews[v].drawCount[l] = 0;
    }
}

void DestroyResources()
{
    ClearDrawLists();
    for (int v = 0; v < kMaxViews; ++v) {
        for (int l = 0; l < kCeTerrainMaxLevels; ++l) {
            DestroyAtlas(gViews[v].atlases[l]);
            std::free(gViews[v].drawTiles[l]);
            gViews[v].drawTiles[l] = NULL;
        }
    }
    gMaxAtlas = 0;
    gPrepared = false;
}

bool ZeroSystemTexture(IDirect3DTexture9 *tex, int size)
{
    D3DLOCKED_RECT lr;
    if (FAILED(tex->LockRect(0, &lr, NULL, 0))) return false;
    unsigned char *row = static_cast<unsigned char *>(lr.pBits);
    const size_t rowBytes = static_cast<size_t>(size) * sizeof(UWORD);
    for (int y = 0; y < size; ++y) {
        std::memset(row, 0, rowBytes);
        row += lr.Pitch;
    }
    tex->UnlockRect(0);
    return true;
}

bool PrepareResources()
{
    gAttempted = true;
    if (gNumViews <= 0 || !LT.GetDevice()) return false;
    const unsigned maxAtlas = CeTerrainMaxAtlasSize(LT.m_d3dCaps.MaxTextureWidth, LT.m_d3dCaps.MaxTextureHeight);
    if (maxAtlas < 1024u) {
        PortSessionLog(PORT_SESSION_VIDEO, false,
            "terrain-textures skipped: device MaxTexture=%ux%u below 1024",
            static_cast<unsigned>(LT.m_d3dCaps.MaxTextureWidth),
            static_cast<unsigned>(LT.m_d3dCaps.MaxTextureHeight));
        return false;
    }

    gMaxAtlas = maxAtlas;
    bool ok = true;
    unsigned long long vramBytes = 0;
    for (int v = 0; v < gNumViews && ok; ++v) {
        ViewState &view = gViews[v];
        const int levels = view.numLevels < kCeTerrainMaxLevels ? view.numLevels : kCeTerrainMaxLevels;
        for (int l = 0; l < levels && ok; ++l) {
            view.drawTiles[l] = static_cast<unsigned short *>(std::malloc(kMaxTiles * sizeof(unsigned short)));
            if (!view.drawTiles[l]) { ok = false; break; }
            if (l == 0) continue;

            LevelAtlas &a = view.atlases[l];
            a.atlasPx = static_cast<int>(CeTerrainLevelAtlasPx(l, maxAtlas));
            a.tilePx = CeTerrainLevelTilePx(l);
            a.tilesPerAxis = CeTerrainLevelTilesPerAxis(l, maxAtlas);
            a.cacheCount = a.tilesPerAxis * a.tilesPerAxis;
            a.uvScale = CeTerrainLevelUvScale(l, maxAtlas);
            a.cache = static_cast<unsigned short *>(std::malloc(static_cast<size_t>(a.cacheCount) * sizeof(unsigned short)));
            a.queuedTile = static_cast<unsigned short *>(std::malloc(static_cast<size_t>(a.cacheCount) * sizeof(unsigned short)));
            a.dirtySlots = static_cast<unsigned short *>(std::malloc(static_cast<size_t>(a.cacheCount) * sizeof(unsigned short)));
            if (!a.cache || !a.queuedTile || !a.dirtySlots) { ok = false; break; }
            std::memset(a.cache, 0xff, static_cast<size_t>(a.cacheCount) * sizeof(unsigned short));
            std::memset(a.queuedTile, 0xff, static_cast<size_t>(a.cacheCount) * sizeof(unsigned short));
            a.dirtyCount = 0;

            if (FAILED(LT.D3D_CreateTextureNoCheck(static_cast<UINT>(a.atlasPx), static_cast<UINT>(a.atlasPx),
                                                   1, 0, D3DFMT_R5G6B5, D3DPOOL_SYSTEMMEM, &a.sysTex)) ||
                !a.sysTex || !ZeroSystemTexture(a.sysTex, a.atlasPx)) {
                ok = false;
                break;
            }
            if (FAILED(LT.D3D_CreateTextureNoCheck(static_cast<UINT>(a.atlasPx), static_cast<UINT>(a.atlasPx),
                                                   1, 0, D3DFMT_R5G6B5, D3DPOOL_DEFAULT, &a.gpuTex)) ||
                !a.gpuTex) {
                ok = false;
                break;
            }
            a.sysTex->AddDirtyRect(NULL);
            LT.GetDevice()->UpdateTexture(a.sysTex, a.gpuTex);
            vramBytes += static_cast<unsigned long long>(a.atlasPx) * static_cast<unsigned long long>(a.atlasPx) * 2ull;
        }
    }

    if (!ok) {
        DestroyResources();
        PortSessionLog(PORT_SESSION_VIDEO, true,
            "terrain-textures fallback: expanded atlas allocation failed (views=%d max_atlas=%u)",
            gNumViews, maxAtlas);
        return false;
    }

    gPrepared = true;
    PortSessionLog(PORT_SESSION_VIDEO, false,
        "terrain-textures ready views=%d max_atlas=%u vram_bytes=%llu",
        gNumViews, gMaxAtlas, vramBytes);
    return true;
}

void ClearDirtyQueue(LevelAtlas &a)
{
    for (int i = 0; i < a.dirtyCount; ++i)
        a.queuedTile[a.dirtySlots[i]] = 0xffffu;
    a.dirtyCount = 0;
}

class TerrainTextureDeviceObject : public DeviceObject {
public:
    HRESULT InvalidateDeviceObjects() override
    {
        ClearDrawLists();
        for (int v = 0; v < gNumViews; ++v)
            for (int l = 1; l < gViews[v].numLevels; ++l) {
                ClearDirtyQueue(gViews[v].atlases[l]);
                ReleaseAtlasGpu(gViews[v].atlases[l]);
            }
        return S_OK;
    }

    HRESULT RestoreDeviceObjects() override
    {
        ClearDrawLists();
        if (!gPrepared || !LT.GetDevice()) return S_OK;
        for (int v = 0; v < gNumViews; ++v) {
            const int levels = gViews[v].numLevels < kCeTerrainMaxLevels ? gViews[v].numLevels : kCeTerrainMaxLevels;
            for (int l = 1; l < levels; ++l) {
                LevelAtlas &a = gViews[v].atlases[l];
                if (!a.sysTex) continue;
                ReleaseAtlasGpu(a);
                if (FAILED(LT.D3D_CreateTextureNoCheck(static_cast<UINT>(a.atlasPx), static_cast<UINT>(a.atlasPx),
                                                       1, 0, D3DFMT_R5G6B5, D3DPOOL_DEFAULT, &a.gpuTex)) ||
                    !a.gpuTex) {
                    DestroyResources();
                    gAttempted = true;
                    return S_OK;
                }
                a.sysTex->AddDirtyRect(NULL);
                LT.GetDevice()->UpdateTexture(a.sysTex, a.gpuTex);
            }
        }
        return S_OK;
    }

    HRESULT DeleteDeviceObjects() override
    {
        DestroyResources();
        gAttempted = false;
        return S_OK;
    }
};

TerrainTextureDeviceObject gDeviceObject;

void RequestSlot(LevelAtlas &a, int tileX, int tileY)
{
    const int slot = CeTerrainAtlasSlot(tileX, tileY, a.tilesPerAxis);
    const unsigned short tile = static_cast<unsigned short>((tileY << 6) | tileX);
    if (a.cache[slot] == tile) return;
    if (a.queuedTile[slot] == tile) return;
    if (a.queuedTile[slot] == 0xffffu && a.dirtyCount < a.cacheCount)
        a.dirtySlots[a.dirtyCount++] = static_cast<unsigned short>(slot);
    a.queuedTile[slot] = tile;
}

void FlushAtlas(LevelAtlas &a, int level)
{
    if (a.dirtyCount <= 0 || !a.sysTex || !a.gpuTex || !LT.GetDevice()) {
        ClearDirtyQueue(a);
        return;
    }
    CMapTex *mapTex = ENGINE.GetMapTex();
    if (!mapTex) {
        ClearDirtyQueue(a);
        return;
    }
    const int mapTexIndex = 4 - level;
    const int mask = a.tilesPerAxis - 1;
    bool uploadedAny = false;
    for (int i = 0; i < a.dirtyCount; ++i) {
        const int slot = a.dirtySlots[i];
        const unsigned short tile = a.queuedTile[slot];
        a.queuedTile[slot] = 0xffffu;
        if (tile == 0xffffu) continue;
        const int slotX = slot & mask;
        const int slotY = slot / a.tilesPerAxis;
        RECT r;
        r.left = slotX * a.tilePx;
        r.top = slotY * a.tilePx;
        r.right = r.left + a.tilePx;
        r.bottom = r.top + a.tilePx;
        D3DLOCKED_RECT lr;
        if (SUCCEEDED(a.sysTex->LockRect(0, &lr, &r, 0))) {
            MAP.Unknown47eff0(level, &mapTex[mapTexIndex], lr.pBits, lr.Pitch / 2, tile, 5, 0, 0, 8, 8);
            const UWORD first = CDXLandscape::mDamage.mCells[tile];
            CDamageMark *m = first ? &CDXLandscape::mDamage.mMarks[first] : NULL;
            if (m && CDXLandscape::mDamage.mTexture && CDXLandscape::mDamage.mTexture->mData) {
                const SINT pitch = lr.Pitch / 2;
                const SINT down = 4 - level;
                do {
                    const SINT n = m->mSize - down;
                    if (n > 0) {
                        BlendDamageAlpha(static_cast<UWORD *>(lr.pBits), pitch,
                                         CDXLandscape::mDamage.mTexture->mData + MIP_OFFSETS[n],
                                         static_cast<signed char>(m->mX) >> down,
                                         static_cast<signed char>(m->mY) >> down,
                                         n, a.tilePx);
                    }
                    m = m->mNext ? &CDXLandscape::mDamage.mMarks[m->mNext] : NULL;
                } while (m);
            }
            a.sysTex->UnlockRect(0);
            a.cache[slot] = tile;
            uploadedAny = true;
        }
    }
    a.dirtyCount = 0;
    if (uploadedAny)
        LT.GetDevice()->UpdateTexture(a.sysTex, a.gpuTex);
}
}

void CeTerrainTexturesShutdown()
{
    if (gRegistered) {
        LT.RemoveDeviceObject(&gDeviceObject);
        gRegistered = false;
    }
    DestroyResources();
    for (int v = 0; v < kMaxViews; ++v) gViews[v].numLevels = 0;
    gNumViews = 0;
    gAttempted = false;
}

void CeTerrainTexturesInit(int numViews, const int *viewLevels)
{
    CeTerrainTexturesShutdown();
    if (!CeTerrainQualityResourcesAllowed()) return;
    if (numViews <= 0 || numViews > kMaxViews || !viewLevels) return;
    gNumViews = numViews;
    for (int v = 0; v < numViews; ++v) {
        int levels = viewLevels[v];
        if (levels < 1) levels = 1;
        if (levels > kCeTerrainMaxLevels) levels = kCeTerrainMaxLevels;
        gViews[v].numLevels = levels;
    }
    LT.AddDeviceObject(&gDeviceObject);
    gRegistered = true;
}

bool CeTerrainTexturesBeginView(int viewpoint, const FVector &camPos, const FVector &lookDir,
                                int numLevels, int maxLevelsUser, bool mirrorPass)
{
    if (viewpoint < 0 || viewpoint >= gNumViews) return false;
    ViewState &view = gViews[viewpoint];
    view.active = false;
    for (int l = 0; l < kCeTerrainMaxLevels; ++l) view.drawCount[l] = 0;

    // Never touch expanded caches in Normal/faithful mode, low-res mode, mirror reflection
    // passes, or the unseen cockpit pass under Commander view.
    if (!CeTerrainQualityExtended() || mirrorPass || LANDSCAPE_LOWRES.mValue != 0 ||
        PortCeWorldViewCaptureSuspended()) {
        return false;
    }
    if (!gAttempted) PrepareResources();
    if (!gPrepared) return false;
    for (int l = 1; l < view.numLevels; ++l) {
        if (!view.atlases[l].gpuTex || !view.atlases[l].sysTex) return false;
    }

    int gx = static_cast<int>(camPos.X);
    int gy = static_cast<int>(camPos.Y);
    if (gx < 0) gx = 0; else if (gx > 511) gx = 511;
    if (gy < 0) gy = 0; else if (gy > 511) gy = 511;
    const float groundZ = MAP.GetHeight(gx, gy);

    view.focus = CeTerrainComputeFocus(camPos.X, camPos.Y, camPos.Z,
                                       lookDir.X, lookDir.Y, lookDir.Z,
                                       groundZ, numLevels, maxLevelsUser, gMaxAtlas);
    view.active = true;
    return true;
}

bool CeTerrainTexturesActive(int viewpoint)
{
    return viewpoint >= 0 && viewpoint < gNumViews && gPrepared && gViews[viewpoint].active;
}

int CeTerrainTexturesChoose(int viewpoint, int numLevels, int maxLevelsUser,
                            int tileX, int tileY, float midHeight)
{
    (void)numLevels;
    (void)maxLevelsUser;
    if (!CeTerrainTexturesActive(viewpoint)) return 0;
    return CeTerrainChooseLevel(gViews[viewpoint].focus, tileX, tileY, midHeight);
}

void CeTerrainTexturesQueueTile(int viewpoint, int texLevel, unsigned short tile, int patchLevel)
{
    CeTerrainQualityTexture(texLevel);
    CeTerrainQualityPatch(patchLevel);
    if (!CeTerrainTexturesActive(viewpoint)) return;
    ViewState &view = gViews[viewpoint];
    if (texLevel < 0 || texLevel >= view.numLevels) texLevel = 0;
    if (view.drawCount[texLevel] < kMaxTiles)
        view.drawTiles[texLevel][view.drawCount[texLevel]++] = tile;

    if (texLevel >= 1) {
        LevelAtlas &a = view.atlases[texLevel];
        const int tx = tile & 63;
        const int ty = (tile >> 6) & 63;
        // Populate the tile AND its 1-tile halo (8 neighbours) in this level's atlas so
        // bilinear and 16x anisotropic sampling along LOD boundaries never reads dark
        // unpopulated texels or unrelated toroidal neighbours.
        for (int dy = -1; dy <= 1; ++dy) {
            const int ny = ty + dy;
            if (ny < 0 || ny >= kCeTerrainMapTiles) continue;
            for (int dx = -1; dx <= 1; ++dx) {
                const int nx = tx + dx;
                if (nx < 0 || nx >= kCeTerrainMapTiles) continue;
                RequestSlot(a, nx, ny);
            }
        }
    }
}

void CeTerrainTexturesFlush(int viewpoint)
{
    if (!CeTerrainTexturesActive(viewpoint)) return;
    ViewState &view = gViews[viewpoint];
    for (int l = 1; l < view.numLevels; ++l)
        FlushAtlas(view.atlases[l], l);
}

IDirect3DTexture9 *CeTerrainTexturesGetTexture(int viewpoint, int level)
{
    if (!CeTerrainTexturesActive(viewpoint) || level < 1 || level >= gViews[viewpoint].numLevels)
        return NULL;
    return gViews[viewpoint].atlases[level].gpuTex;
}

float CeTerrainTexturesUvScale(int viewpoint, int level)
{
    if (!CeTerrainTexturesActive(viewpoint) || level <= 0 || level >= gViews[viewpoint].numLevels)
        return 1.0f / 512.0f;
    return gViews[viewpoint].atlases[level].uvScale;
}

const unsigned short *CeTerrainTexturesDrawTiles(int viewpoint, int level, int *outCount)
{
    if (outCount) *outCount = 0;
    if (!CeTerrainTexturesActive(viewpoint) || level < 0 || level >= gViews[viewpoint].numLevels)
        return NULL;
    if (outCount) *outCount = gViews[viewpoint].drawCount[level];
    return gViews[viewpoint].drawTiles[level];
}

void CeTerrainTexturesInvalidateTile(unsigned short tile)
{
    if (!gPrepared) return;
    const int tx = tile & 63;
    const int ty = (tile >> 6) & 63;
    for (int v = 0; v < gNumViews; ++v) {
        for (int l = 1; l < gViews[v].numLevels; ++l) {
            LevelAtlas &a = gViews[v].atlases[l];
            if (!a.cache) continue;
            const int slot = CeTerrainAtlasSlot(tx, ty, a.tilesPerAxis);
            if ((a.cache[slot] & 0x7fffu) == tile)
                a.cache[slot] |= 0x8000u;
        }
    }
}
