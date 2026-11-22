// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_museum_art.cpp: Framed retail Goodies concept art canvases for the Concept
// Art Gallery (Southeast) and Early Designs Vault (Southwest). Loads at most one wing's
// curated plates on demand via CeAssetCatalog + D3DXCreateTextureFromFileInMemoryEx
// (bounded <= 512 px longest edge, D3DPOOL_MANAGED for D3D9 Reset safety, aspect preserved)
// and degrades cleanly to a matte archival plaque whenever any retail file is absent.
#ifdef CE_VERSION
#include "common.h"
#include "ltshell.h"
#include "renderinfo.h"
#include "d3dx9.h"
#include "ce_asset_catalog.h"
#include "ce_museum_art.h"
#include "ce_museum_world.h"
#include "../platform/common/portlog.h"
#include <cstdio>
#include <cstring>

const char *PortGameDir();

namespace CeMuseumArt {
namespace {

struct LoadedCanvas {
    IDirect3DTexture9 *texture;
    unsigned srcW, srcH;
    bool attempted;
    bool missing;
};

struct RuntimeState {
    CeAssetCatalog *catalog;
    bool catalogTried;
    CeMuseumTour::Wing loadedWing;
    bool hasLoadedWing;
    LoadedCanvas slots[kManifestCount];
} gArt = {};

struct UploadCtx {
    IDirect3DDevice9 *device;
    LoadedCanvas *slot;
};

int UploadCallback(const unsigned char *dds, size_t bytes, const CeAssetTextureHeader *h, void *context) {
    UploadCtx *ctx = static_cast<UploadCtx *>(context);
    if (!ctx || !ctx->device || !ctx->slot || !dds || !bytes || !h) return 0;
    const UploadSize sz = BoundedUpload(h->width, h->height, kMaxTexEdge);
    IDirect3DTexture9 *tex = NULL;
    const HRESULT hr = D3DXCreateTextureFromFileInMemoryEx(
        ctx->device, dds, static_cast<UINT>(bytes),
        static_cast<UINT>(sz.width), static_cast<UINT>(sz.height),
        1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
        D3DX_FILTER_TRIANGLE, D3DX_FILTER_NONE, 0, NULL, NULL, &tex);
    if (FAILED(hr) || !tex) {
        if (tex) tex->Release();
        return 0;
    }
    ctx->slot->texture = tex;
    ctx->slot->srcW = h->width;
    ctx->slot->srcH = h->height;
    return 1;
}

void ReleaseAllTextures() {
    for (unsigned i = 0; i < kManifestCount; ++i) {
        if (gArt.slots[i].texture) {
            gArt.slots[i].texture->Release();
            gArt.slots[i].texture = NULL;
        }
        gArt.slots[i].srcW = 0;
        gArt.slots[i].srcH = 0;
        gArt.slots[i].attempted = false;
        gArt.slots[i].missing = false;
    }
    gArt.hasLoadedWing = false;
}

void EnsureCatalog() {
    if (gArt.catalog || gArt.catalogTried) return;
    gArt.catalogTried = true;
    const char *dir = PortGameDir();
    if (!dir || !*dir) return;
    char err[192] = {};
    gArt.catalog = PortCeAssetCatalogOpenKinds(dir, CE_ASSET_TEXTURE, err, sizeof err);
    if (!gArt.catalog) {
        PortSessionLog(PORT_SESSION_GAME, false,
                       "museum art: could not index installed textures (%s); canvases will show archival plaques",
                       err[0] ? err : "unavailable");
    }
}

void EnsureWingLoaded(CeMuseumTour::Wing wing) {
    const bool isArtWing = (wing == CeMuseumTour::kWingConceptArt || wing == CeMuseumTour::kWingEarlyDesigns);
    if (!isArtWing) {
        if (gArt.hasLoadedWing) ReleaseAllTextures();
        return;
    }
    if (gArt.hasLoadedWing && gArt.loadedWing == wing) return;
    ReleaseAllTextures();
    IDirect3DDevice9 *dev = LT.GetDevice();
    if (!dev) return;
    gArt.loadedWing = wing;
    gArt.hasLoadedWing = true;
    EnsureCatalog();
    unsigned loaded = 0, missing = 0;
    for (unsigned i = 0; i < kManifestCount; ++i) {
        if (!ShouldBeResident(i, wing)) continue;
        gArt.slots[i].attempted = true;
        if (!gArt.catalog) {
            gArt.slots[i].missing = true;
            ++missing;
            continue;
        }
        const size_t idx = PortCeAssetCatalogFindTexture(gArt.catalog, kManifest[i].retailPath);
        if (idx == SIZE_MAX) {
            gArt.slots[i].missing = true;
            ++missing;
            continue;
        }
        UploadCtx ctx = {dev, &gArt.slots[i]};
        if (PortCeAssetCatalogReadTexture(gArt.catalog, idx, UploadCallback, &ctx) && gArt.slots[i].texture) {
            ++loaded;
        } else {
            gArt.slots[i].missing = true;
            ++missing;
        }
    }
    PortSessionLog(PORT_SESSION_GAME, false,
                   "museum art: %s loaded %u canvases (%u unavailable)",
                   CeMuseumTour::Meta(wing).title, loaded, missing);
}

struct ArtTexVertex { float x, y, z; DWORD colour; float u, v; };
struct ArtFlatVertex { float x, y, z; DWORD colour; };

} // namespace

void RuntimeInit() {
    RuntimeShutdown();
}

void RuntimeShutdown() {
    ReleaseAllTextures();
    if (gArt.catalog) {
        PortCeAssetCatalogClose(gArt.catalog);
        gArt.catalog = NULL;
    }
    gArt.catalogTried = false;
}

void RuntimeUpdate(CeMuseumTour::Wing wing) {
    EnsureWingLoaded(wing);
}

bool RuntimeCue(float visitorX, float visitorY, float fwdX, float fwdY,
                CeMuseumTour::Wing wing,
                char *title, unsigned titleCap,
                char *line, unsigned lineCap,
                char *note, unsigned noteCap) {
    if (!title || !titleCap || !line || !lineCap) return false;
    const int idx = FocusCanvas(visitorX, visitorY, fwdX, fwdY, wing);
    if (idx < 0 || static_cast<unsigned>(idx) >= kManifestCount) return false;
    const CanvasSpec &sp = kManifest[idx];
    std::snprintf(title, titleCap, "%s", sp.title);
    std::snprintf(line, lineCap, "%s", sp.caption);
    if (note && noteCap) {
        if (gArt.slots[idx].missing)
            std::snprintf(note, noteCap, "%s - archival plate unavailable in this install.", sp.category);
        else
            std::snprintf(note, noteCap, "%s", sp.category);
    }
    return true;
}

void RuntimeDraw(float floorZ) {
    if (!gArt.hasLoadedWing) return;
    IDirect3DDevice9 *dev = LT.GetDevice();
    if (!dev) return;

    IDirect3DStateBlock9 *saved = NULL;
    if (FAILED(dev->CreateStateBlock(D3DSBT_ALL, &saved)) || !saved) return;
    if (FAILED(saved->Capture())) { saved->Release(); return; }

    D3DMATRIX world = {}, view, proj;
    world._11 = world._22 = world._33 = world._44 = 1.0f;
    RENDERINFO.GetView(&view);
    RENDERINFO.GetProjection(&proj);
    dev->SetVertexShader(NULL);
    dev->SetPixelShader(NULL);
    dev->SetTransform(D3DTS_WORLD, &world);
    dev->SetTransform(D3DTS_VIEW, &view);
    dev->SetTransform(D3DTS_PROJECTION, &proj);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

    for (unsigned i = 0; i < kManifestCount; ++i) {
        if (!ShouldBeResident(i, gArt.loadedWing)) continue;
        const CanvasSpec &sp = kManifest[i];
        const LoadedCanvas &slot = gArt.slots[i];

        // Tangent right vector along the wall plane (so painting reads left-to-right facing visitor)
        // Normal (nx, ny): visitor faces (-nx, -ny); visitor's right is (-ny, nx) in XY with Z downward!
        // Verify: facing North (0, +1) at North wall (normal 0, -1): right should be +X (+1, 0) = (-(-1), 0).
        // Facing South (0, -1) at South wall (normal 0, +1): right should be -X (-1, 0) = (-(+1), 0).
        const float rx = -sp.normalY;
        const float ry = sp.normalX;
        const float wx = CeMuseumWorld::kEntranceX + sp.localX;
        const float wy = CeMuseumWorld::kEntranceY + sp.localY;
        const float wz = floorZ - sp.centreZ;

        // Backing mat + brass bevel frame (sits 0.04 m proud of wall; painting sits 0.06 m proud).
        const float matW = sp.maxWidth * 0.5f + 0.16f;
        const float matH = sp.maxHeight * 0.5f + 0.16f;
        const float frameW = matW + 0.08f;
        const float frameH = matH + 0.08f;

        auto drawFlatRect = [&](float outDist, float hw, float hh, DWORD topCol, DWORD botCol) {
            const float bx = wx + sp.normalX * outDist;
            const float by = wy + sp.normalY * outDist;
            const ArtFlatVertex q[4] = {
                {bx - rx * hw, by - ry * hw, wz - hh, topCol},
                {bx + rx * hw, by + ry * hw, wz - hh, topCol},
                {bx - rx * hw, by - ry * hw, wz + hh, botCol},
                {bx + rx * hw, by + ry * hw, wz + hh, botCol},
            };
            dev->SetTexture(0, NULL);
            dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
            dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
            dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
            dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(ArtFlatVertex));
        };

        // Warm museum brass outer frame, dark charcoal archival mat, subtle brass plaque below frame
        drawFlatRect(0.03f, frameW, frameH, 0xffd2ab60, 0xff9e7c3e);
        drawFlatRect(0.045f, matW, matH, 0xff282e35, 0xff1e2329);
        // Brass plaque centered beneath the frame
        {
            const float px = wx + sp.normalX * 0.05f;
            const float py = wy + sp.normalY * 0.05f;
            const float pz = wz + frameH + 0.20f;
            const ArtFlatVertex pq[4] = {
                {px - rx * 0.34f, py - ry * 0.34f, pz - 0.08f, 0xffd4ae64},
                {px + rx * 0.34f, py + ry * 0.34f, pz - 0.08f, 0xffd4ae64},
                {px - rx * 0.34f, py - ry * 0.34f, pz + 0.08f, 0xffa68240},
                {px + rx * 0.34f, py + ry * 0.34f, pz + 0.08f, 0xffa68240},
            };
            dev->SetTexture(0, NULL);
            dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
            dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
            dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
            dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, pq, sizeof(ArtFlatVertex));
        }

        if (slot.texture) {
            const QuadExtent ext = FitFrame(slot.srcW, slot.srcH, sp.maxWidth, sp.maxHeight);
            const float cx = wx + sp.normalX * 0.065f;
            const float cy = wy + sp.normalY * 0.065f;
            // Subtle gallery top-lighting gradient (bright upper half, gentle lower falloff)
            const ArtTexVertex q[4] = {
                {cx - rx * ext.halfW, cy - ry * ext.halfW, wz - ext.halfH, 0xffffffff, 0.0f, 0.0f},
                {cx + rx * ext.halfW, cy + ry * ext.halfW, wz - ext.halfH, 0xffffffff, 1.0f, 0.0f},
                {cx - rx * ext.halfW, cy - ry * ext.halfW, wz + ext.halfH, 0xffeceae4, 0.0f, 1.0f},
                {cx + rx * ext.halfW, cy + ry * ext.halfW, wz + ext.halfH, 0xffeceae4, 1.0f, 1.0f},
            };
            dev->SetTexture(0, slot.texture);
            dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
            dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
            dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
            dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
            dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
            dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(ArtTexVertex));
        } else {
            // Clean archival fallback stone plate if retail artwork file is missing
            drawFlatRect(0.065f, sp.maxWidth * 0.44f, sp.maxHeight * 0.44f, 0xff5a636c, 0xff464e56);
        }
    }

    saved->Apply();
    saved->Release();
}

} // namespace CeMuseumArt
#else
#include "ce_museum_art.h"
namespace CeMuseumArt {
void RuntimeInit() {}
void RuntimeShutdown() {}
void RuntimeUpdate(CeMuseumTour::Wing) {}
bool RuntimeCue(float, float, float, float, CeMuseumTour::Wing, char *, unsigned, char *, unsigned, char *, unsigned) { return false; }
void RuntimeDraw(float) {}
} // namespace CeMuseumArt
#endif
