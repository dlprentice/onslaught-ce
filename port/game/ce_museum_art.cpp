// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_museum_art.cpp: Framed retail Goodies concept art canvases for the Concept
// Art Gallery (75 plates) and Early Designs Vault (74 plates) — 149/149 (100%) simultaneous
// salon hang. Uploads all 149 textures ONCE at RuntimeInit() with kMaxTexEdge = 512px in DXT1
// (D3DPOOL_MANAGED for D3D9 Reset safety), crops baked black padding bars, handles truncated
// retail filenames (goodie 95 & goodie 103) via MatchesTruncatedCatalogPath, and batches ALL 149
// brass frames, linen mats, nameplates, and artwork quads into static D3DPOOL_MANAGED GPU VBs/IBs.
#ifdef CE_VERSION
#include "common.h"
#include "ltshell.h"
#include "renderinfo.h"
#include "d3dx9.h"
#include "ce_asset_catalog.h"
#include "ce_museum_art.h"
#include "ce_museum_render_state.h"
#include "ce_museum_world.h"
#include "../platform/common/portlog.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

const char *PortGameDir();
extern "C" uint64_t PortCeD3dDeviceGeneration(void) __attribute__((weak));

namespace CeMuseumArt {
namespace {

inline uint64_t CurrentArtDeviceGen() {
    return PortCeD3dDeviceGeneration ? PortCeD3dDeviceGeneration() : 0u;
}

struct LoadedCanvas {
    IDirect3DTexture9 *texture;
    bool ownsTexture;
    unsigned srcW, srcH;
    unsigned cropW, cropH;
    float u0, v0, u1, v1;
    bool attempted;
    bool missing;
};

struct ArtTexVertex { float x, y, z; DWORD colour; float u, v; };
struct ArtFlatVertex { float x, y, z; DWORD colour; };

struct RuntimeState {
    CeAssetCatalog *catalog;
    IDirect3DDevice9 *device;
    uint64_t deviceGen;
    bool catalogTried;
    bool allLoaded;
    LoadedCanvas slots[kManifestCount];

    // Static D3DPOOL_MANAGED GPU buffers for all 149 frames/mats/plaques & artwork quads.
    IDirect3DDevice9 *gpuDevice;
    uint64_t gpuDeviceGen;
    float gpuFloorZ;
    bool gpuAttempted;
    IDirect3DVertexBuffer9 *frameVb;
    IDirect3DIndexBuffer9 *frameIb;
    unsigned frameVertCount;
    unsigned frameIdxCount;
    IDirect3DVertexBuffer9 *artVb;
    unsigned artVertCount;

    // 3D aim ray fed from CeMuseumHallVisitor for two-tier upper/lower plaque focus.
    bool hasAim3D;
    float eyeX, eyeY, eyeZ;
    float aimX, aimY, aimZ;
} gArt = {};

struct UploadCtx {
    IDirect3DDevice9 *device;
    LoadedCanvas *slot;
};

// Inspects mip 0 of a newly uploaded D3DPOOL_MANAGED gallery texture (DXT1 or A8R8G8B8)
// to trim solid-black power-of-two padding bars so frames and mats hug the artwork cleanly.
void DetectCanvasContentBounds(IDirect3DTexture9 *tex, unsigned fallbackW, unsigned fallbackH, LoadedCanvas *slot) {
    slot->cropW = fallbackW ? fallbackW : 1u;
    slot->cropH = fallbackH ? fallbackH : 1u;
    slot->u0 = 0.0f; slot->v0 = 0.0f; slot->u1 = 1.0f; slot->v1 = 1.0f;
    if (!tex) return;

    D3DSURFACE_DESC desc = {};
    if (FAILED(tex->GetLevelDesc(0, &desc)) || desc.Width < 16u || desc.Height < 16u) return;
    D3DLOCKED_RECT lr = {};
    if (FAILED(tex->LockRect(0, &lr, NULL, D3DLOCK_READONLY)) || !lr.pBits || lr.Pitch <= 0) return;

    const unsigned w = desc.Width, h = desc.Height;
    const bool isDxt1 = (desc.Format == D3DFMT_DXT1);
    const bool isDxt35 = (desc.Format == D3DFMT_DXT3 || desc.Format == D3DFMT_DXT5);
    if ((isDxt1 || isDxt35) && (w % 4u) == 0u && (h % 4u) == 0u) {
        const unsigned bw = w / 4u, bh = h / 4u;
        const unsigned blkStride = isDxt1 ? 8u : 16u;
        const unsigned colOff = isDxt1 ? 0u : 8u;
        auto blockNearBlack = [&](unsigned bx, unsigned by) -> bool {
            const uint8_t *blk = static_cast<const uint8_t *>(lr.pBits) + by * static_cast<unsigned>(lr.Pitch) + bx * blkStride + colOff;
            const uint16_t c0 = static_cast<uint16_t>(blk[0] | (blk[1] << 8));
            const uint16_t c1 = static_cast<uint16_t>(blk[2] | (blk[3] << 8));
            const uint32_t bits = static_cast<uint32_t>(blk[4]) |
                                  (static_cast<uint32_t>(blk[5]) << 8) |
                                  (static_cast<uint32_t>(blk[6]) << 16) |
                                  (static_cast<uint32_t>(blk[7]) << 24);
            unsigned r[4], g[4], b[4];
            r[0] = ((c0 >> 11) & 0x1fu) * 255u / 31u;
            g[0] = ((c0 >> 5)  & 0x3fu) * 255u / 63u;
            b[0] = ( c0        & 0x1fu) * 255u / 31u;
            r[1] = ((c1 >> 11) & 0x1fu) * 255u / 31u;
            g[1] = ((c1 >> 5)  & 0x3fu) * 255u / 63u;
            b[1] = ( c1        & 0x1fu) * 255u / 31u;
            if (!isDxt1 || c0 > c1) {
                r[2] = (2u * r[0] + r[1] + 1u) / 3u; g[2] = (2u * g[0] + g[1] + 1u) / 3u; b[2] = (2u * b[0] + b[1] + 1u) / 3u;
                r[3] = (r[0] + 2u * r[1] + 1u) / 3u; g[3] = (g[0] + 2u * g[1] + 1u) / 3u; b[3] = (b[0] + 2u * b[1] + 1u) / 3u;
            } else {
                r[2] = (r[0] + r[1]) / 2u; g[2] = (g[0] + g[1]) / 2u; b[2] = (b[0] + b[1]) / 2u;
                r[3] = 0u; g[3] = 0u; b[3] = 0u;
            }
            for (unsigned p = 0u; p < 16u; ++p) {
                const unsigned sel = (bits >> (p * 2u)) & 3u;
                if (r[sel] > 12u || g[sel] > 12u || b[sel] > 12u) return false;
            }
            return true;
        };
        auto rowBlack = [&](unsigned by) -> bool {
            unsigned lit = 0u;
            for (unsigned bx = 0u; bx < bw; ++bx)
                if (!blockNearBlack(bx, by) && ++lit > bw / 64u) return false;
            return true;
        };
        auto colBlack = [&](unsigned bx) -> bool {
            unsigned lit = 0u;
            for (unsigned by = 0u; by < bh; ++by)
                if (!blockNearBlack(bx, by) && ++lit > bh / 64u) return false;
            return true;
        };
        unsigned bTop = 0u, bBot = bh, bLeft = 0u, bRight = bw;
        while (bTop + 4u < bBot && rowBlack(bTop)) ++bTop;
        while (bBot > bTop + 4u && rowBlack(bBot - 1u)) --bBot;
        while (bLeft + 4u < bRight && colBlack(bLeft)) ++bLeft;
        while (bRight > bLeft + 4u && colBlack(bRight - 1u)) --bRight;
        tex->UnlockRect(0);
        const unsigned cw = (bRight - bLeft) * 4u, ch = (bBot - bTop) * 4u;
        if ((bTop > 0u || bBot < bh || bLeft > 0u || bRight < bw) && cw * 5u >= w * 2u && ch * 5u >= h * 2u) {
            const float padU = (bLeft > 0u || bRight < bw) ? (1.0f / float(w)) : 0.0f;
            const float padV = (bTop > 0u || bBot < bh) ? (1.0f / float(h)) : 0.0f;
            slot->u0 = float(bLeft * 4u) / float(w) + (bLeft > 0u ? padU : 0.0f);
            slot->u1 = float(bRight * 4u) / float(w) - (bRight < bw ? padU : 0.0f);
            slot->v0 = float(bTop * 4u) / float(h) + (bTop > 0u ? padV : 0.0f);
            slot->v1 = float(bBot * 4u) / float(h) - (bBot < bh ? padV : 0.0f);
            slot->cropW = static_cast<unsigned>(float(fallbackW) * (slot->u1 - slot->u0) + 0.5f);
            slot->cropH = static_cast<unsigned>(float(fallbackH) * (slot->v1 - slot->v0) + 0.5f);
            if (!slot->cropW) slot->cropW = 1u;
            if (!slot->cropH) slot->cropH = 1u;
        }
        return;
    }
    if (desc.Format == D3DFMT_A8R8G8B8 || desc.Format == D3DFMT_X8R8G8B8) {
        auto pxBlack = [&](unsigned x, unsigned y) -> bool {
            const uint32_t px = *reinterpret_cast<const uint32_t *>(
                static_cast<const uint8_t *>(lr.pBits) + y * static_cast<unsigned>(lr.Pitch) + x * 4u);
            return ((px >> 16) & 0xffu) <= 10u && ((px >> 8) & 0xffu) <= 10u && (px & 0xffu) <= 10u;
        };
        auto rowBlack = [&](unsigned y) -> bool {
            unsigned lit = 0u;
            for (unsigned x = 0u; x < w; ++x)
                if (!pxBlack(x, y) && ++lit > w / 64u) return false;
            return true;
        };
        auto colBlack = [&](unsigned x) -> bool {
            unsigned lit = 0u;
            for (unsigned y = 0u; y < h; ++y)
                if (!pxBlack(x, y) && ++lit > h / 64u) return false;
            return true;
        };
        unsigned top = 0u, bot = h, left = 0u, right = w;
        while (top + 16u < bot && rowBlack(top)) ++top;
        while (bot > top + 16u && rowBlack(bot - 1u)) --bot;
        while (left + 16u < right && colBlack(left)) ++left;
        while (right > left + 16u && colBlack(right - 1u)) --right;
        tex->UnlockRect(0);
        const unsigned cw = right - left, ch = bot - top;
        if ((top > 0u || bot < h || left > 0u || right < w) && cw * 5u >= w * 2u && ch * 5u >= h * 2u) {
            const float padU = 1.0f / float(w), padV = 1.0f / float(h);
            slot->u0 = float(left) / float(w) + (left > 0u ? padU : 0.0f);
            slot->u1 = float(right) / float(w) - (right < w ? padU : 0.0f);
            slot->v0 = float(top) / float(h) + (top > 0u ? padV : 0.0f);
            slot->v1 = float(bot) / float(h) - (bot < h ? padV : 0.0f);
            slot->cropW = static_cast<unsigned>(float(fallbackW) * (slot->u1 - slot->u0) + 0.5f);
            slot->cropH = static_cast<unsigned>(float(fallbackH) * (slot->v1 - slot->v0) + 0.5f);
            if (!slot->cropW) slot->cropW = 1u;
            if (!slot->cropH) slot->cropH = 1u;
        }
        return;
    }
    tex->UnlockRect(0);
}

int UploadCallback(const unsigned char *dds, size_t bytes, const CeAssetTextureHeader *h, void *context) {
    UploadCtx *ctx = static_cast<UploadCtx *>(context);
    if (!ctx || !ctx->device || !ctx->slot || !dds || !bytes || !h) return 0;
    const UploadSize sz = BoundedUpload(h->width, h->height, kMaxTexEdge);
    IDirect3DTexture9 *tex = NULL;
    // Prefer compressed DXT1/BC1 at <= 512 px (~18.6 MiB total across all 149 plates);
    // fall back to single-level A8R8G8B8 only if a device rejects compressed textures.
    HRESULT hr = D3DXCreateTextureFromFileInMemoryEx(
        ctx->device, dds, static_cast<UINT>(bytes),
        static_cast<UINT>(sz.width), static_cast<UINT>(sz.height),
        0, 0, D3DFMT_DXT1, D3DPOOL_MANAGED,
        D3DX_FILTER_TRIANGLE, D3DX_FILTER_BOX, 0, NULL, NULL, &tex);
    if (FAILED(hr) || !tex) {
        if (tex) { tex->Release(); tex = NULL; }
        hr = D3DXCreateTextureFromFileInMemoryEx(
            ctx->device, dds, static_cast<UINT>(bytes),
            static_cast<UINT>(sz.width), static_cast<UINT>(sz.height),
            1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
            D3DX_FILTER_TRIANGLE, D3DX_FILTER_BOX, 0, NULL, NULL, &tex);
    }
    if (FAILED(hr) || !tex) {
        if (tex) tex->Release();
        return 0;
    }
    ctx->slot->texture = tex;
    ctx->slot->ownsTexture = true;
    ctx->slot->srcW = h->width;
    ctx->slot->srcH = h->height;
    DetectCanvasContentBounds(tex, h->width, h->height, ctx->slot);
    return 1;
}

void ReleaseFrameGpuBuffers() {
    if (gArt.frameVb) { gArt.frameVb->Release(); gArt.frameVb = NULL; }
    if (gArt.frameIb) { gArt.frameIb->Release(); gArt.frameIb = NULL; }
    if (gArt.artVb)   { gArt.artVb->Release();   gArt.artVb = NULL; }
    gArt.gpuDevice = NULL;
    gArt.gpuDeviceGen = 0u;
    gArt.gpuAttempted = false;
    gArt.frameVertCount = 0;
    gArt.frameIdxCount = 0;
    gArt.artVertCount = 0;
}

void ReleaseAllTextures() {
    ReleaseFrameGpuBuffers();
    for (unsigned i = 0; i < kManifestCount; ++i) {
        if (gArt.slots[i].texture && gArt.slots[i].ownsTexture) {
            gArt.slots[i].texture->Release();
        }
        gArt.slots[i].texture = NULL;
        gArt.slots[i].ownsTexture = false;
        gArt.slots[i].srcW = 0;
        gArt.slots[i].srcH = 0;
        gArt.slots[i].cropW = 0;
        gArt.slots[i].cropH = 0;
        gArt.slots[i].u0 = 0.0f;
        gArt.slots[i].v0 = 0.0f;
        gArt.slots[i].u1 = 1.0f;
        gArt.slots[i].v1 = 1.0f;
        gArt.slots[i].attempted = false;
        gArt.slots[i].missing = false;
    }
    gArt.device = NULL;
    gArt.deviceGen = 0u;
    gArt.allLoaded = false;
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

// Lookup exact texture key first, then fall back to truncated retail filename prefix match
// (resolves retail goodie 95 '.t.aya' and goodie 103 '.tga(0.aya' for 149/149 coverage).
size_t ResolveTextureIndex(const CeAssetCatalog *catalog, const char *retailPath) {
    if (!catalog || !retailPath || !*retailPath) return SIZE_MAX;
    const size_t exact = PortCeAssetCatalogFindTexture(catalog, retailPath);
    if (exact != SIZE_MAX) return exact;
    const size_t total = PortCeAssetCatalogCount(catalog);
    for (size_t k = 0; k < total; ++k) {
        if (PortCeAssetCatalogKind(catalog, k) != CE_ASSET_TEXTURE) continue;
        const char *cpath = PortCeAssetCatalogPath(catalog, k);
        if (MatchesTruncatedCatalogPath(cpath, retailPath)) return k;
    }
    return SIZE_MAX;
}

void EnsureAllCanvasesLoaded() {
    IDirect3DDevice9 *dev = LT.GetDevice();
    if (!dev) return;
    const uint64_t gen = CurrentArtDeviceGen();
    if (gArt.device && (gArt.device != dev || gArt.deviceGen != gen)) ReleaseAllTextures();
    if (gArt.allLoaded && gArt.device == dev && gArt.deviceGen == gen) return;
    gArt.device = dev;
    gArt.deviceGen = gen;
    gArt.allLoaded = true;
    EnsureCatalog();
    unsigned loaded = 0, missing = 0;
    for (unsigned i = 0; i < kManifestCount; ++i) {
        gArt.slots[i].attempted = true;
        if (!gArt.catalog) {
            gArt.slots[i].missing = true;
            ++missing;
            continue;
        }
        // Share texture pointer if an earlier plate already uploaded the identical retail path (e.g. goodies 61/90 & 63/157).
        bool shared = false;
        for (unsigned prev = 0; prev < i; ++prev) {
            if (gArt.slots[prev].texture && std::strcmp(kManifest[prev].retailPath, kManifest[i].retailPath) == 0) {
                gArt.slots[i].texture = gArt.slots[prev].texture;
                gArt.slots[i].ownsTexture = false;
                gArt.slots[i].srcW = gArt.slots[prev].srcW;
                gArt.slots[i].srcH = gArt.slots[prev].srcH;
                gArt.slots[i].cropW = gArt.slots[prev].cropW;
                gArt.slots[i].cropH = gArt.slots[prev].cropH;
                gArt.slots[i].u0 = gArt.slots[prev].u0;
                gArt.slots[i].v0 = gArt.slots[prev].v0;
                gArt.slots[i].u1 = gArt.slots[prev].u1;
                gArt.slots[i].v1 = gArt.slots[prev].v1;
                ++loaded;
                shared = true;
                break;
            }
        }
        if (shared) continue;

        const size_t idx = ResolveTextureIndex(gArt.catalog, kManifest[i].retailPath);
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
    unsigned trimmed = 0;
    for (unsigned i = 0; i < kManifestCount; ++i) {
        if (gArt.slots[i].texture && (gArt.slots[i].u0 > 0.0f || gArt.slots[i].u1 < 1.0f ||
                                      gArt.slots[i].v0 > 0.0f || gArt.slots[i].v1 < 1.0f)) {
            ++trimmed;
        }
    }
    PortSessionLog(PORT_SESSION_GAME, false,
                   "museum art: preloaded %u of %u gallery canvases (%u unavailable, %u trimmed)",
                   loaded, static_cast<unsigned>(kManifestCount), missing, trimmed);
}

constexpr unsigned kMaxFlatQuadsPerCanvas = 10u; // 4 outer frame + 4 mat + 1 plaque + 1 optional fallback

DWORD LerpArtColour(DWORD a, DWORD b, float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    auto ch = [&](unsigned sh) -> DWORD {
        const float va = float((a >> sh) & 255u);
        const float vb = float((b >> sh) & 255u);
        int iv = static_cast<int>(va + (vb - va) * t + 0.5f);
        if (iv < 0) iv = 0;
        if (iv > 255) iv = 255;
        return static_cast<DWORD>(iv) << sh;
    };
    return 0xff000000u | ch(16) | ch(8) | ch(0);
}

void BuildFrameGeometry(float floorZ, ArtFlatVertex *verts, uint16_t *indices,
                        unsigned *outVerts, unsigned *outIndices) {
    unsigned vAt = 0u, iAt = 0u;
    auto pushQuad = [&](float cx, float cy, float cz, float rx, float ry,
                        float hw, float hh, DWORD topCol, DWORD botCol) {
        const uint16_t base = static_cast<uint16_t>(vAt);
        verts[vAt++] = {cx - rx * hw, cy - ry * hw, cz - hh, topCol};
        verts[vAt++] = {cx + rx * hw, cy + ry * hw, cz - hh, topCol};
        verts[vAt++] = {cx - rx * hw, cy - ry * hw, cz + hh, botCol};
        verts[vAt++] = {cx + rx * hw, cy + ry * hw, cz + hh, botCol};
        indices[iAt++] = base + 0u; indices[iAt++] = base + 1u; indices[iAt++] = base + 2u;
        indices[iAt++] = base + 1u; indices[iAt++] = base + 3u; indices[iAt++] = base + 2u;
    };
    // Hollow 4-strip border around exact opening (inW, inH) so zero frame or mat pixels lie behind artwork.
    auto pushHollowBorder = [&](float cx, float cy, float cz, float rx, float ry,
                                float outW, float outH, float inW, float inH,
                                DWORD topCol, DWORD botCol) {
        const float spanH = 2.0f * outH;
        const float tTop = spanH > 1e-5f ? (outH - inH) / spanH : 0.0f;
        const float tBot = spanH > 1e-5f ? (outH + inH) / spanH : 1.0f;
        const DWORD cInTop = LerpArtColour(topCol, botCol, tTop);
        const DWORD cInBot = LerpArtColour(topCol, botCol, tBot);
        const float stripH = 0.5f * (outH - inH);
        const float midH = 0.5f * (outH + inH);
        const float stripW = 0.5f * (outW - inW);
        const float midW = 0.5f * (outW + inW);
        // Top strip (cz - outH .. cz - inH), bottom strip (cz + inH .. cz + outH)
        pushQuad(cx, cy, cz - midH, rx, ry, outW, stripH, topCol, cInTop);
        pushQuad(cx, cy, cz + midH, rx, ry, outW, stripH, cInBot, botCol);
        // Left & right strips strictly between top and bottom strips (cz - inH .. cz + inH)
        pushQuad(cx - rx * midW, cy - ry * midW, cz, rx, ry, stripW, inH, cInTop, cInBot);
        pushQuad(cx + rx * midW, cy + ry * midW, cz, rx, ry, stripW, inH, cInTop, cInBot);
    };

    for (unsigned i = 0; i < kManifestCount; ++i) {
        const CanvasSpec &sp = kManifest[i];
        const LoadedCanvas &slot = gArt.slots[i];
        const float rx = -sp.normalY;
        const float ry = sp.normalX;
        const float wx = CeMuseumWorld::kEntranceX + sp.localX;
        const float wy = CeMuseumWorld::kEntranceY + sp.localY;
        const float wz = floorZ - sp.centreZ;
        const unsigned effW = slot.cropW ? slot.cropW : slot.srcW;
        const unsigned effH = slot.cropH ? slot.cropH : slot.srcH;
        const QuadExtent ext = slot.texture
            ? FitFrame(effW, effH, sp.maxWidth, sp.maxHeight)
            : QuadExtent{sp.maxWidth * 0.5f, sp.maxHeight * 0.5f};
        const float matW = ext.halfW + 0.10f;
        const float matH = ext.halfH + 0.10f;
        const float frameW = matW + 0.055f;
        const float frameH = matH + 0.055f;

        // Hollow brushed-brass outer frame & hollow charcoal archival mat (zero backing pixels behind artwork!)
        pushHollowBorder(wx + sp.normalX * 0.025f, wy + sp.normalY * 0.025f, wz, rx, ry,
                         frameW, frameH, matW, matH, 0xffd6b064, 0xff9a783a);
        pushHollowBorder(wx + sp.normalX * 0.042f, wy + sp.normalY * 0.042f, wz, rx, ry,
                         matW, matH, ext.halfW, ext.halfH, 0xff2a3038, 0xff1e242b);
        pushQuad(wx + sp.normalX * 0.046f, wy + sp.normalY * 0.046f, wz + frameH + 0.11f, rx, ry, 0.28f, 0.055f, 0xffd6b064, 0xffa4803e);
        if (!slot.texture) {
            pushQuad(wx + sp.normalX * 0.060f, wy + sp.normalY * 0.060f, wz, rx, ry,
                     ext.halfW, ext.halfH, 0xff5a636c, 0xff464e56);
        }
    }
    *outVerts = vAt;
    *outIndices = iAt;
}

void BuildArtQuadGeometry(float floorZ, ArtTexVertex *verts) {
    for (unsigned i = 0; i < kManifestCount; ++i) {
        const CanvasSpec &sp = kManifest[i];
        const LoadedCanvas &slot = gArt.slots[i];
        const float rx = -sp.normalY;
        const float ry = sp.normalX;
        const float cx = CeMuseumWorld::kEntranceX + sp.localX + sp.normalX * 0.060f;
        const float cy = CeMuseumWorld::kEntranceY + sp.localY + sp.normalY * 0.060f;
        const float wz = floorZ - sp.centreZ;
        const unsigned effW = slot.cropW ? slot.cropW : slot.srcW;
        const unsigned effH = slot.cropH ? slot.cropH : slot.srcH;
        const QuadExtent ext = FitFrame(effW, effH, sp.maxWidth, sp.maxHeight);
        const float u0 = slot.texture ? slot.u0 : 0.0f;
        const float v0 = slot.texture ? slot.v0 : 0.0f;
        const float u1 = slot.texture ? slot.u1 : 1.0f;
        const float v1 = slot.texture ? slot.v1 : 1.0f;
        ArtTexVertex *q = &verts[i * 4u];
        q[0] = {cx - rx * ext.halfW, cy - ry * ext.halfW, wz - ext.halfH, 0xffffffff, u0, v0};
        q[1] = {cx + rx * ext.halfW, cy + ry * ext.halfW, wz - ext.halfH, 0xffffffff, u1, v0};
        q[2] = {cx - rx * ext.halfW, cy - ry * ext.halfW, wz + ext.halfH, 0xffeeece6, u0, v1};
        q[3] = {cx + rx * ext.halfW, cy + ry * ext.halfW, wz + ext.halfH, 0xffeeece6, u1, v1};
    }
}

void EnsureFrameGpuBuffers(IDirect3DDevice9 *dev, float floorZ) {
    if (!dev) return;
    const uint64_t gen = CurrentArtDeviceGen();
    if (gArt.gpuAttempted && gArt.gpuDevice == dev && gArt.gpuDeviceGen == gen && std::fabs(gArt.gpuFloorZ - floorZ) < 1e-4f)
        return;
    ReleaseFrameGpuBuffers();
    gArt.gpuDevice = dev;
    gArt.gpuDeviceGen = gen;
    gArt.gpuFloorZ = floorZ;
    gArt.gpuAttempted = true;

    const unsigned maxFlatVerts = kManifestCount * kMaxFlatQuadsPerCanvas * 4u;
    const unsigned maxFlatIdx = kManifestCount * kMaxFlatQuadsPerCanvas * 6u;
    ArtFlatVertex *flatVerts = static_cast<ArtFlatVertex *>(std::malloc(maxFlatVerts * sizeof(ArtFlatVertex)));
    uint16_t *flatIdx = static_cast<uint16_t *>(std::malloc(maxFlatIdx * sizeof(uint16_t)));
    if (flatVerts && flatIdx) {
        unsigned vc = 0, ic = 0;
        BuildFrameGeometry(floorZ, flatVerts, flatIdx, &vc, &ic);
        if (vc >= 4u && ic >= 6u) {
            IDirect3DVertexBuffer9 *vb = NULL;
            IDirect3DIndexBuffer9 *ib = NULL;
            const UINT vbBytes = vc * static_cast<UINT>(sizeof(ArtFlatVertex));
            const UINT ibBytes = ic * static_cast<UINT>(sizeof(uint16_t));
            if (SUCCEEDED(dev->CreateVertexBuffer(vbBytes, D3DUSAGE_WRITEONLY,
                                                  D3DFVF_XYZ | D3DFVF_DIFFUSE, D3DPOOL_MANAGED, &vb, NULL)) && vb &&
                SUCCEEDED(dev->CreateIndexBuffer(ibBytes, D3DUSAGE_WRITEONLY,
                                                 D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib, NULL)) && ib) {
                void *vd = NULL, *id = NULL;
                if (SUCCEEDED(vb->Lock(0, vbBytes, &vd, 0)) && vd) {
                    std::memcpy(vd, flatVerts, vbBytes);
                    vb->Unlock();
                    if (SUCCEEDED(ib->Lock(0, ibBytes, &id, 0)) && id) {
                        std::memcpy(id, flatIdx, ibBytes);
                        ib->Unlock();
                        gArt.frameVb = vb;
                        gArt.frameIb = ib;
                        gArt.frameVertCount = vc;
                        gArt.frameIdxCount = ic;
                        vb = NULL;
                        ib = NULL;
                    }
                }
            }
            if (vb) vb->Release();
            if (ib) ib->Release();
        }
    }
    if (flatVerts) std::free(flatVerts);
    if (flatIdx) std::free(flatIdx);

    // Static GPU VB for all 149 textured artwork quads (4 vertices per plate, drawn via DrawPrimitive).
    const unsigned artVertsCount = kManifestCount * 4u;
    ArtTexVertex *artVerts = static_cast<ArtTexVertex *>(std::malloc(artVertsCount * sizeof(ArtTexVertex)));
    if (artVerts) {
        BuildArtQuadGeometry(floorZ, artVerts);
        IDirect3DVertexBuffer9 *avb = NULL;
        const UINT bytes = artVertsCount * static_cast<UINT>(sizeof(ArtTexVertex));
        if (SUCCEEDED(dev->CreateVertexBuffer(bytes, D3DUSAGE_WRITEONLY,
                                              D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1,
                                              D3DPOOL_MANAGED, &avb, NULL)) && avb) {
            void *dst = NULL;
            if (SUCCEEDED(avb->Lock(0, bytes, &dst, 0)) && dst) {
                std::memcpy(dst, artVerts, bytes);
                avb->Unlock();
                gArt.artVb = avb;
                gArt.artVertCount = artVertsCount;
                avb = NULL;
            }
        }
        if (avb) avb->Release();
        std::free(artVerts);
    }
}

} // namespace

void RuntimeInit() {
    RuntimeShutdown();
    EnsureAllCanvasesLoaded();
}

void RuntimeShutdown() {
    ReleaseAllTextures();
    if (gArt.catalog) {
        PortCeAssetCatalogClose(gArt.catalog);
        gArt.catalog = NULL;
    }
    gArt.catalogTried = false;
    gArt.hasAim3D = false;
}

void RuntimeUpdate(CeMuseumTour::Wing) {
    EnsureAllCanvasesLoaded();
}

void RuntimeSetAim(float eyeX, float eyeY, float eyeZ, float aimX, float aimY, float aimZ) {
    gArt.eyeX = eyeX; gArt.eyeY = eyeY; gArt.eyeZ = eyeZ;
    gArt.aimX = aimX; gArt.aimY = aimY; gArt.aimZ = aimZ;
    gArt.hasAim3D = true;
}

bool RuntimeCue(float visitorX, float visitorY, float fwdX, float fwdY,
                CeMuseumTour::Wing wing,
                char *title, unsigned titleCap,
                char *line, unsigned lineCap,
                char *note, unsigned noteCap) {
    if (!title || !titleCap || !line || !lineCap) return false;
    int idx = gArt.hasAim3D
        ? FocusCanvas3D(gArt.eyeX, gArt.eyeY, gArt.eyeZ, gArt.aimX, gArt.aimY, gArt.aimZ, wing)
        : FocusCanvas(visitorX, visitorY, fwdX, fwdY, wing);
    if (idx < 0) idx = FocusCanvas(visitorX, visitorY, fwdX, fwdY, wing);
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

void RuntimeDraw(float floorZ, bool ownStateBlock) {
    IDirect3DDevice9 *dev = LT.GetDevice();
    if (!dev) return;
    if (!gArt.allLoaded || gArt.device != dev || gArt.deviceGen != CurrentArtDeviceGen()) EnsureAllCanvasesLoaded();
    if (!gArt.allLoaded) return;
    EnsureFrameGpuBuffers(dev, floorZ);

    IDirect3DStateBlock9 *saved = NULL;
    if (ownStateBlock) {
        if (FAILED(dev->CreateStateBlock(D3DSBT_ALL, &saved)) || !saved) return;
        if (FAILED(saved->Capture())) { saved->Release(); return; }
    }

    D3DMATRIX world = {}, view, proj;
    world._11 = world._22 = world._33 = world._44 = 1.0f;
    RENDERINFO.GetView(&view);
    RENDERINFO.GetProjection(&proj);
    dev->SetVertexShader(NULL);
    dev->SetPixelShader(NULL);
    dev->SetTransform(D3DTS_WORLD, &world);
    dev->SetTransform(D3DTS_VIEW, &view);
    dev->SetTransform(D3DTS_PROJECTION, &proj);
    dev->SetTexture(1, NULL);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    dev->SetRenderState(D3DRS_RANGEFOGENABLE, FALSE);
    dev->SetRenderState(D3DRS_FOGVERTEXMODE, D3DFOG_NONE);
    dev->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_NONE);
    dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    const float zeroBias = 0.0f;
    dev->SetRenderState(D3DRS_DEPTHBIAS, *reinterpret_cast<const DWORD *>(&zeroBias));
    dev->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, *reinterpret_cast<const DWORD *>(&zeroBias));
    dev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
    dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

    // 1. Draw ALL 149 brass frames, linen/charcoal mats, nameplates & fallbacks in ONE static GPU draw call!
    dev->SetTexture(0, NULL);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    if (gArt.frameVb && gArt.frameIb && gArt.frameIdxCount >= 3u) {
        dev->SetStreamSource(0, gArt.frameVb, 0, sizeof(ArtFlatVertex));
        dev->SetIndices(gArt.frameIb);
        dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, gArt.frameVertCount, 0, gArt.frameIdxCount / 3u);
    } else {
        // CPU fallback if GPU buffer allocation failed.
        const unsigned maxFlatVerts = kManifestCount * kMaxFlatQuadsPerCanvas * 4u;
        const unsigned maxFlatIdx = kManifestCount * kMaxFlatQuadsPerCanvas * 6u;
        ArtFlatVertex *fv = static_cast<ArtFlatVertex *>(std::malloc(maxFlatVerts * sizeof(ArtFlatVertex)));
        uint16_t *fi = static_cast<uint16_t *>(std::malloc(maxFlatIdx * sizeof(uint16_t)));
        if (fv && fi) {
            unsigned vc = 0, ic = 0;
            BuildFrameGeometry(floorZ, fv, fi, &vc, &ic);
            if (vc >= 4u && ic >= 3u) {
                dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, vc, ic / 3u, fi,
                                            D3DFMT_INDEX16, fv, sizeof(ArtFlatVertex));
            }
        }
        if (fv) std::free(fv);
        if (fi) std::free(fi);
    }

    // 2. Render ALL 149 preloaded textured artwork plates preserving exact source aspect ratio from static GPU VB.
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    dev->SetIndices(NULL);
    if (gArt.artVb && gArt.artVertCount == kManifestCount * 4u) {
        dev->SetStreamSource(0, gArt.artVb, 0, sizeof(ArtTexVertex));
        for (unsigned i = 0; i < kManifestCount; ++i) {
            IDirect3DTexture9 *tex = gArt.slots[i].texture;
            if (!tex) continue;
            dev->SetTexture(0, tex);
            dev->DrawPrimitive(D3DPT_TRIANGLESTRIP, i * 4u, 2u);
        }
        dev->SetStreamSource(0, NULL, 0, 0);
    } else {
        dev->SetStreamSource(0, NULL, 0, 0);
        for (unsigned i = 0; i < kManifestCount; ++i) {
            const LoadedCanvas &slot = gArt.slots[i];
            if (!slot.texture) continue;
            const CanvasSpec &sp = kManifest[i];
            const float rx = -sp.normalY;
            const float ry = sp.normalX;
            const float cx = CeMuseumWorld::kEntranceX + sp.localX + sp.normalX * 0.060f;
            const float cy = CeMuseumWorld::kEntranceY + sp.localY + sp.normalY * 0.060f;
            const float wz = floorZ - sp.centreZ;
            const unsigned effW = slot.cropW ? slot.cropW : slot.srcW;
            const unsigned effH = slot.cropH ? slot.cropH : slot.srcH;
            const QuadExtent ext = FitFrame(effW, effH, sp.maxWidth, sp.maxHeight);
            const ArtTexVertex q[4] = {
                {cx - rx * ext.halfW, cy - ry * ext.halfW, wz - ext.halfH, 0xffffffff, slot.u0, slot.v0},
                {cx + rx * ext.halfW, cy + ry * ext.halfW, wz - ext.halfH, 0xffffffff, slot.u1, slot.v0},
                {cx - rx * ext.halfW, cy - ry * ext.halfW, wz + ext.halfH, 0xffeeece6, slot.u0, slot.v1},
                {cx + rx * ext.halfW, cy + ry * ext.halfW, wz + ext.halfH, 0xffeeece6, slot.u1, slot.v1},
            };
            dev->SetTexture(0, slot.texture);
            dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(ArtTexVertex));
        }
    }

    if (saved) {
        saved->Apply();
        saved->Release();
        CeMuseumRestoreEngineRenderState();
    }
}

} // namespace CeMuseumArt

extern "C" void PortCeMuseumArtDeleteDeviceObjects(void) {
    CeMuseumArt::ReleaseFrameGpuBuffers();
    CeMuseumArt::ReleaseAllTextures();
}
#else
#include "ce_museum_art.h"
namespace CeMuseumArt {
void RuntimeInit() {}
void RuntimeShutdown() {}
void RuntimeUpdate(CeMuseumTour::Wing) {}
void RuntimeSetAim(float, float, float, float, float, float) {}
bool RuntimeCue(float, float, float, float, CeMuseumTour::Wing, char *, unsigned, char *, unsigned, char *, unsigned) { return false; }
void RuntimeDraw(float, bool) {}
} // namespace CeMuseumArt
#endif
