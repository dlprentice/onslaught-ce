// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_archive_pedestal.cpp: Interactive Grand Museum Atrium Archive Pedestal
// runtime. Uses a single dedicated 8 MiB host-malloc swap arena (outside retail heaps)
// reset wholesale on every exhibit swap, renders all 213 retail .msh.aya meshes and 45
// Goodie 3D models via clean-room indexed D3DPT_TRIANGLELIST batches + D3DPOOL_MANAGED
// textures, displays all 149 Goodie concept art plates on an illuminated glass stele,
// and presents metadata steles for all 33 Cinema FMVs and 5 Bonus Levels.
#ifdef CE_VERSION
#include "common.h"
#include "ltshell.h"
#include "renderinfo.h"
#include "d3dx9.h"
#include "ce_asset_catalog.h"
#include "ce_archive_pedestal.h"
#include "ce_archive_scanner.h"
#include "ce_museum_cinema.h"
#include "ce_museum_world.h"
#include "../platform/common/portlog.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

const char *PortGameDir();
const char *PortUserDir();
extern "C" bool PortCeGoodieFiles(int goodie, const char **mesh, const char **picture, WCHAR *title);
extern "C" bool PortCeMuseumCinemaFilmName(unsigned index, char *stem, unsigned cap);
extern "C" uint64_t PortCeD3dDeviceGeneration(void) __attribute__((weak));

namespace CeArchivePedestal {
namespace {

inline uint64_t CurrentPedestalDeviceGen() {
    return PortCeD3dDeviceGeneration ? PortCeD3dDeviceGeneration() : 0u;
}

using CeArchiveScanner::ArchiveBatch;
using CeArchiveScanner::ArchiveVertex;
using CeArchiveScanner::kMaxMeshTextures;
using CeArchiveScanner::ParsedMesh;
using CeArchiveScanner::ParseError;
using CeArchiveScanner::ParseErrorName;
using CeArchiveScanner::QuarantineList;
using CeArchiveScanner::SwapArena;

constexpr unsigned kMaxRetailIndex = 512u;

struct RetailMeshEntry {
    size_t catalogIndex;
    char relPath[CeArchiveScanner::kPathCap];
    char title[128];
};

enum ActiveContent {
    kContentNone = 0,
    kContentMesh,
    kContentArt,
    kContentCinema,
    kContentBonus,
    kContentQuarantined,
    kContentUnavailable
};

struct RuntimeData {
    PedestalState state;
    SwapArena arena;
    CeAssetCatalog *catalog;
    bool catalogTried;
    uint64_t catalogFingerprint;
    QuarantineList quarantine;

    RetailMeshEntry *retailMeshes; // host-malloc array, freed in RuntimeShutdown
    unsigned retailMeshCount;

    unsigned loadedGeneration;
    bool hasLoaded;
    ActiveContent content;
    ParsedMesh mesh;
    IDirect3DDevice9 *device;
    uint64_t deviceGen;
    IDirect3DTexture9 *meshTextures[kMaxMeshTextures];
    IDirect3DTexture9 *artTexture;
    unsigned artSrcW;
    unsigned artSrcH;

    char activeTitle[128];
    char activeLine[192];
    char activeNote[192];
    char activeReason[64];
    float lastWorldX;
    float lastWorldY;
    CeMuseumTour::Wing lastWing;
};

static RuntimeData *gData = NULL;

RuntimeData *EnsureData() {
    if (!gData) {
        gData = static_cast<RuntimeData *>(std::calloc(1, sizeof(RuntimeData)));
        if (gData) {
            ResetState(&gData->state);
            gData->arena.Init();
        }
    }
    return gData;
}

void ReleaseGpuTextures(RuntimeData *d) {
    if (!d) return;
    for (unsigned i = 0; i < kMaxMeshTextures; ++i) {
        if (d->meshTextures[i]) {
            d->meshTextures[i]->Release();
            d->meshTextures[i] = NULL;
        }
    }
    if (d->artTexture) {
        d->artTexture->Release();
        d->artTexture = NULL;
    }
    d->device = NULL;
    d->deviceGen = 0u;
    d->artSrcW = 0;
    d->artSrcH = 0;
}

void ClearActiveExhibit(RuntimeData *d) {
    if (!d) return;
    ReleaseGpuTextures(d);
    d->arena.Reset();
    std::memset(&d->mesh, 0, sizeof(d->mesh));
    d->content = kContentNone;
    d->activeTitle[0] = '\0';
    d->activeLine[0] = '\0';
    d->activeNote[0] = '\0';
    d->activeReason[0] = '\0';
}

void WideToAsciiClean(const WCHAR *src, char *dst, unsigned dstCap) {
    if (!dst || !dstCap) return;
    dst[0] = '\0';
    if (!src) return;
    unsigned n = 0;
    for (; *src && n + 1u < dstCap; ++src) {
        const unsigned c = static_cast<unsigned>(*src);
        if (c >= 0x20u && c <= 0x7Eu && c != '[' && c != ']' && c != '|')
            dst[n++] = static_cast<char>(c);
        else if (c == '\t' || c == '\r' || c == '\n')
            dst[n++] = ' ';
    }
    while (n > 0u && dst[n - 1u] == ' ') --n;
    dst[n] = '\0';
}

struct TexUploadCtx {
    IDirect3DDevice9 *device;
    IDirect3DTexture9 **outTex;
    unsigned *outW;
    unsigned *outH;
    unsigned maxEdge;
};

int UploadTextureCb(const unsigned char *dds, size_t bytes,
                    const CeAssetTextureHeader *h, void *context) {
    TexUploadCtx *ctx = static_cast<TexUploadCtx *>(context);
    if (!ctx || !ctx->device || !ctx->outTex || !dds || !bytes || !h) return 0;
    unsigned w = h->width;
    unsigned ht = h->height;
    const unsigned longest = w > ht ? w : ht;
    if (ctx->maxEdge > 0u && longest > ctx->maxEdge) {
        const float sc = float(ctx->maxEdge) / float(longest);
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
    if (ctx->outW) *ctx->outW = h->width;
    if (ctx->outH) *ctx->outH = h->height;
    return 1;
}

void EnsureCatalogAndCache(RuntimeData *d) {
    if (!d || d->catalogTried) return;
    d->catalogTried = true;

    const char *gameDir = PortGameDir();
    if (!gameDir || !*gameDir) return;

    char err[192] = {};
    d->catalog = PortCeAssetCatalogOpenKinds(gameDir, CE_ASSET_MODEL | CE_ASSET_TEXTURE, err, sizeof(err));
    if (!d->catalog) {
        PortSessionLog(PORT_SESSION_GAME, false,
                       "archive pedestal: catalog unavailable (%s)", err[0] ? err : "open failed");
        return;
    }

    // Index all installed retail models & compute deterministic install fingerprint.
    const size_t total = PortCeAssetCatalogCount(d->catalog);
    unsigned modelCount = 0;
    unsigned texCount = 0;
    uint64_t fp = CeArchiveScanner::kFnv64Basis;
    for (size_t i = 0; i < total; ++i) {
        const int kind = PortCeAssetCatalogKind(d->catalog, i);
        if (kind == CE_ASSET_MODEL) {
            ++modelCount;
            fp = CeArchiveScanner::HashStringLower(fp, PortCeAssetCatalogPath(d->catalog, i));
            fp = CeArchiveScanner::HashU64(fp, PortCeAssetCatalogSize(d->catalog, i));
        } else if (kind == CE_ASSET_TEXTURE) {
            ++texCount;
        }
    }
    fp = CeArchiveScanner::HashU64(fp, modelCount);
    fp = CeArchiveScanner::HashU64(fp, texCount);
    d->catalogFingerprint = fp ? fp : 1ULL;

    const unsigned capped = modelCount > kMaxRetailIndex ? kMaxRetailIndex : modelCount;
    if (capped > 0u) {
        d->retailMeshes = static_cast<RetailMeshEntry *>(std::calloc(capped, sizeof(RetailMeshEntry)));
        if (d->retailMeshes) {
            unsigned at = 0;
            for (size_t i = 0; i < total && at < capped; ++i) {
                if (PortCeAssetCatalogKind(d->catalog, i) != CE_ASSET_MODEL) continue;
                RetailMeshEntry &e = d->retailMeshes[at++];
                e.catalogIndex = i;
                const char *rel = PortCeAssetCatalogPath(d->catalog, i);
                const char *rawTitle = PortCeAssetCatalogTitle(d->catalog, i);
                std::snprintf(e.relPath, sizeof(e.relPath), "%s", rel ? rel : "");
                FormatDisplayTitle(rawTitle ? rawTitle : e.relPath, e.title, sizeof(e.title));
            }
            d->retailMeshCount = at;
        }
    }
    d->state.counts[kCatRetailMeshes] = d->retailMeshCount;

    // Load quarantine cache if present and matching the current install fingerprint.
    const char *userDir = PortUserDir();
    if (userDir && *userDir) {
        char cachePath[1024];
        const size_t ul = std::strlen(userDir);
        const bool slash = (userDir[ul - 1] == '/' || userDir[ul - 1] == '\\');
        std::snprintf(cachePath, sizeof(cachePath),
                      slash ? "%sarchive-scan-v1.txt" : "%s/archive-scan-v1.txt", userDir);
        if (CeArchiveScanner::LoadQuarantineCache(cachePath, d->catalogFingerprint, &d->quarantine)) {
            PortSessionLog(PORT_SESSION_GAME, false,
                           "archive pedestal: loaded quarantine cache (%u quarantined, %u retail meshes)",
                           d->quarantine.count, d->retailMeshCount);
        }
    }
}

void LoadMeshByRelPath(RuntimeData *d, const char *relPath, const char *displayTitle,
                       const char *subtitle) {
    std::snprintf(d->activeTitle, sizeof(d->activeTitle), "%s", displayTitle);
    std::snprintf(d->activeLine, sizeof(d->activeLine), "%s", subtitle);

    if (!relPath || !*relPath) {
        d->content = kContentUnavailable;
        std::snprintf(d->activeNote, sizeof(d->activeNote), "Archival 3D mesh is unavailable in this install.");
        return;
    }

    if (d->quarantine.IsQuarantined(relPath)) {
        const char *why = d->quarantine.ReasonFor(relPath);
        d->content = kContentQuarantined;
        std::snprintf(d->activeReason, sizeof(d->activeReason), "%s", why ? why : "quarantined");
        std::snprintf(d->activeNote, sizeof(d->activeNote),
                      "Quarantined by archive scan (%s). Press Next or Prev to browse other exhibits.",
                      d->activeReason);
        return;
    }

    const char *gameDir = PortGameDir();
    if (!gameDir || !*gameDir || !d->arena.base) {
        d->content = kContentUnavailable;
        std::snprintf(d->activeNote, sizeof(d->activeNote), "Install directory unavailable.");
        return;
    }

    char fullPath[1024];
    const size_t gl = std::strlen(gameDir);
    const bool slash = (gameDir[gl - 1] == '/' || gameDir[gl - 1] == '\\');
    std::snprintf(fullPath, sizeof(fullPath), slash ? "%s%s" : "%s/%s", gameDir, relPath);

    unsigned char *ayaBytes = NULL;
    size_t ayaSize = 0;
    if (!CeArchiveScanner::ReadWholeFileIntoArena(fullPath, &d->arena, &ayaBytes, &ayaSize, 2u * 1024u * 1024u)) {
        d->content = kContentUnavailable;
        std::snprintf(d->activeNote, sizeof(d->activeNote), "Mesh archive file not found in this install.");
        return;
    }

    const ParseError perr = CeArchiveScanner::DecompressAndParseAyaMesh(ayaBytes, ayaSize, &d->arena, &d->mesh);
    if (perr != CeArchiveScanner::kParseOk || !d->mesh.bounds.valid) {
        const char *reason = perr != CeArchiveScanner::kParseOk ? ParseErrorName(perr) : "invalid_bounds";
        d->quarantine.Add(relPath, reason);
        ClearActiveExhibit(d);
        d->content = kContentQuarantined;
        std::snprintf(d->activeTitle, sizeof(d->activeTitle), "%s", displayTitle);
        std::snprintf(d->activeLine, sizeof(d->activeLine), "%s", subtitle);
        std::snprintf(d->activeReason, sizeof(d->activeReason), "%s", reason);
        std::snprintf(d->activeNote, sizeof(d->activeNote),
                      "Mesh quarantined safely (%s). Zero memory leaked.", reason);
        return;
    }

    // Upload referenced DDS textures into D3DPOOL_MANAGED textures (released on next swap).
    IDirect3DDevice9 *dev = LT.GetDevice();
    if (dev && d->catalog) {
        d->device = dev;
        d->deviceGen = CurrentPedestalDeviceGen();
        for (uint32_t ti = 0; ti < d->mesh.textureCount && ti < kMaxMeshTextures; ++ti) {
            const size_t catIdx = PortCeAssetCatalogFindTexture(d->catalog, d->mesh.textureNames[ti]);
            if (catIdx == SIZE_MAX) continue;
            TexUploadCtx ctx = {dev, &d->meshTextures[ti], NULL, NULL, 512u};
            (void)PortCeAssetCatalogReadTexture(d->catalog, catIdx, UploadTextureCb, &ctx);
        }
    }

    d->content = kContentMesh;
    std::snprintf(d->activeNote, sizeof(d->activeNote),
                  "%u vertices, %u triangles, %u parts - aim at brass controls or use PgUp/PgDn and Tab.",
                  d->mesh.vertexCount, d->mesh.triangleCount, d->mesh.partCount);
}

void LoadCurrentExhibit(RuntimeData *d) {
    if (!d) return;
    EnsureCatalogAndCache(d);
    ClearActiveExhibit(d);
    d->loadedGeneration = d->state.swapGeneration;
    d->hasLoaded = true;

    const Category cat = d->state.category;
    const unsigned count = d->state.counts[cat];
    if (count == 0u) {
        d->content = kContentUnavailable;
        std::snprintf(d->activeTitle, sizeof(d->activeTitle), "%s", CategoryName(cat));
        std::snprintf(d->activeLine, sizeof(d->activeLine), "No items available in this category.");
        std::snprintf(d->activeNote, sizeof(d->activeNote), "Press Category or Tab to switch archive collections.");
        return;
    }
    const unsigned ord = d->state.selected[cat] % count;

    if (cat == kCatRetailMeshes) {
        if (!d->retailMeshes || ord >= d->retailMeshCount) {
            d->content = kContentUnavailable;
            return;
        }
        const RetailMeshEntry &e = d->retailMeshes[ord];
        char sub[192];
        std::snprintf(sub, sizeof(sub), "Retail 3D Mesh Archive (%u of %u)", ord + 1u, count);
        LoadMeshByRelPath(d, e.relPath, e.title, sub);
        return;
    }

    if (cat == kCatGoodieModels) {
        const int gid = GoodieIdAt(kGoodieMesh, ord);
        const char *meshName = NULL, *picName = NULL;
        WCHAR wideTitle[256] = {};
        char title[128] = {};
        if (gid >= 0 && PortCeGoodieFiles(gid, &meshName, &picName, wideTitle)) {
            WideToAsciiClean(wideTitle, title, sizeof(title));
            if (!title[0] && meshName) FormatDisplayTitle(meshName, title, sizeof(title));
        }
        if (!title[0]) std::snprintf(title, sizeof(title), "Goodie Model %u", ord + 1u);
        char relPath[CeArchiveScanner::kPathCap] = {};
        if (meshName && meshName[0])
            std::snprintf(relPath, sizeof(relPath), "data/resources/meshes/m_%s.aya", meshName);
        char sub[192];
        std::snprintf(sub, sizeof(sub), "Goodie 3D Model (%u of %u)", ord + 1u, count);
        LoadMeshByRelPath(d, relPath, title, sub);
        return;
    }

    if (cat == kCatConceptArt) {
        const int gid = GoodieIdAt(kGoodieImage, ord);
        const char *meshName = NULL, *picName = NULL;
        WCHAR wideTitle[256] = {};
        char title[128] = {};
        if (gid >= 0 && PortCeGoodieFiles(gid, &meshName, &picName, wideTitle)) {
            WideToAsciiClean(wideTitle, title, sizeof(title));
            if (!title[0] && picName) FormatDisplayTitle(picName, title, sizeof(title));
        }
        if (!title[0]) std::snprintf(title, sizeof(title), "Concept Art Plate %u", ord + 1u);
        std::snprintf(d->activeTitle, sizeof(d->activeTitle), "%s", title);
        std::snprintf(d->activeLine, sizeof(d->activeLine), "Concept Art Lightbox (%u of %u)", ord + 1u, count);

        IDirect3DDevice9 *dev = LT.GetDevice();
        if (dev && d->catalog && picName && picName[0]) {
            const size_t catIdx = PortCeAssetCatalogFindTexture(d->catalog, picName);
            if (catIdx != SIZE_MAX) {
                TexUploadCtx ctx = {dev, &d->artTexture, &d->artSrcW, &d->artSrcH, kLightboxMaxEdge};
                if (PortCeAssetCatalogReadTexture(d->catalog, catIdx, UploadTextureCb, &ctx) && d->artTexture) {
                    d->device = dev;
                    d->deviceGen = CurrentPedestalDeviceGen();
                    d->content = kContentArt;
                    std::snprintf(d->activeNote, sizeof(d->activeNote),
                                  "%ux%u archival plate - aim at brass controls or use PgUp/PgDn and Tab.",
                                  d->artSrcW, d->artSrcH);
                    return;
                }
            }
        }
        d->content = kContentUnavailable;
        std::snprintf(d->activeNote, sizeof(d->activeNote), "Archival concept plate unavailable in this install.");
        return;
    }

    if (cat == kCatCinemaFmvs) {
        const int gid = GoodieIdAt(kGoodieFmv, ord);
        char stem[128] = {};
        char title[128] = {};
        if (PortCeMuseumCinemaFilmName(ord, stem, sizeof(stem)) && stem[0]) {
            FormatDisplayTitle(stem, title, sizeof(title));
        } else {
            const char *meshName = NULL, *picName = NULL;
            WCHAR wideTitle[256] = {};
            if (gid >= 0 && PortCeGoodieFiles(gid, &meshName, &picName, wideTitle))
                WideToAsciiClean(wideTitle, title, sizeof(title));
        }
        if (!title[0]) std::snprintf(title, sizeof(title), "Archive Film %u", ord + 1u);
        d->content = kContentCinema;
        std::snprintf(d->activeTitle, sizeof(d->activeTitle), "%s", title);
        std::snprintf(d->activeLine, sizeof(d->activeLine), "Cinema Archive Film (%u of %u)", ord + 1u, count);
        std::snprintf(d->activeNote, sizeof(d->activeNote), "%s",
                      d->state.pose.customWorldPose
                          ? "Head north into the Citadel Cinema Pavilion to watch archive films."
                          : "Head east to the Screening Room via the Concept Art wing or Directory to watch films.");
        return;
    }

    if (cat == kCatBonusLevels) {
        const int gid = GoodieIdAt(kGoodieLevel, ord);
        const int lvl = BonusLevelNumberForGoodie(gid);
        const char *meshName = NULL, *picName = NULL;
        WCHAR wideTitle[256] = {};
        char title[128] = {};
        if (gid >= 0 && PortCeGoodieFiles(gid, &meshName, &picName, wideTitle))
            WideToAsciiClean(wideTitle, title, sizeof(title));
        if (!title[0]) std::snprintf(title, sizeof(title), "Bonus Challenge %u", ord + 1u);
        d->content = kContentBonus;
        std::snprintf(d->activeTitle, sizeof(d->activeTitle), "%s", title);
        std::snprintf(d->activeLine, sizeof(d->activeLine),
                      "Bonus Level Portal (%u of %u - Stage %d)", ord + 1u, count, lvl);
        std::snprintf(d->activeNote, sizeof(d->activeNote),
                      "Launch from the Extras - Bonus Levels menu to play this challenge.");
        return;
    }
}

struct FlatVert { float x, y, z; DWORD col; };
struct TexVert { float x, y, z; DWORD col; float u, v; };

void EmitBox(IDirect3DDevice9 *dev, float cx, float cy, float cz,
             float hx, float hy, float hz, DWORD topCol, DWORD sideCol) {
    const float x0 = cx - hx, x1 = cx + hx;
    const float y0 = cy - hy, y1 = cy + hy;
    const float z0 = cz - hz, z1 = cz + hz;
    const FlatVert tris[30] = {
        {x0,y0,z0,topCol},{x1,y0,z0,topCol},{x1,y1,z0,topCol},
        {x0,y0,z0,topCol},{x1,y1,z0,topCol},{x0,y1,z0,topCol},
        {x0,y0,z1,sideCol},{x1,y0,z1,sideCol},{x1,y0,z0,sideCol},
        {x0,y0,z1,sideCol},{x1,y0,z0,sideCol},{x0,y0,z0,sideCol},
        {x1,y0,z1,sideCol},{x1,y1,z1,sideCol},{x1,y1,z0,sideCol},
        {x1,y0,z1,sideCol},{x1,y1,z0,sideCol},{x1,y0,z0,sideCol},
        {x1,y1,z1,sideCol},{x0,y1,z1,sideCol},{x0,y1,z0,sideCol},
        {x1,y1,z1,sideCol},{x0,y1,z0,sideCol},{x1,y1,z0,sideCol},
        {x0,y1,z1,sideCol},{x0,y0,z1,sideCol},{x0,y0,z0,sideCol},
        {x0,y1,z1,sideCol},{x0,y0,z0,sideCol},{x0,y1,z0,sideCol},
    };
    dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 10, tris, sizeof(FlatVert));
}

void DrawControlConsoleAndRing(IDirect3DDevice9 *dev, const RuntimeData *d, float floorZ) {
    const BasePose &pose = d->state.pose;
    const float wx = CeMuseumWorld::kEntranceX + pose.localX;
    const float wy = CeMuseumWorld::kEntranceY + pose.localY;
    const float topZ = floorZ - pose.plinthTopZ;

    // If relocated outside the Grand Museum (e.g., Sanctuary Citadel), draw a self-contained plinth.
    if (pose.customWorldPose) {
        const float h = pose.plinthTopZ;
        EmitBox(dev, wx, wy, floorZ - 0.10f, pose.plinthHalf, pose.plinthHalf, 0.10f, 0xff5a6672, 0xff3b444e);
        EmitBox(dev, wx, wy, floorZ - 0.5f * h, pose.plinthHalf * 0.90f, pose.plinthHalf * 0.90f,
                0.5f * h - 0.08f, 0xffcfd6dc, 0xffaeb6be);
    }

    // Subtle brushed-brass turntable ring collar flush atop the plinth deck.
    EmitBox(dev, wx, wy, topZ - 0.025f, pose.plinthHalf * 0.72f, pose.plinthHalf * 0.72f, 0.025f,
            0xffd6b062, 0xff9e7c3e);
    EmitBox(dev, wx, wy, topZ - 0.045f, pose.plinthHalf * 0.66f, pose.plinthHalf * 0.66f, 0.020f,
            0xff323b44, 0xff222830);

    // South-facing brass control desk rail holding the 4 interactive buttons.
    const float railY = wy + kControlOffsetY;
    EmitBox(dev, wx, railY, floorZ - 0.07f, 1.32f, 0.24f, 0.07f, 0xff4c5763, 0xff343d46);
    EmitBox(dev, wx, railY, floorZ - 0.52f, 1.18f, 0.14f, 0.38f, 0xff3c4650, 0xff2c343c);
    EmitBox(dev, wx, railY, floorZ - 0.95f, 1.30f, 0.22f, 0.06f, 0xffc69e50, 0xff947436);

    static const DWORD kBtnIdleCol[kBtnCount] = {
        0xffc89f52, // Prev: warm brass
        0xffd4ae5e, // Next: warm brass
        0xff68a8d8, // Category: Forseti cyan-steel
        0xffd89050, // Pause/Spin: amber brass
    };
    for (unsigned i = 0; i < static_cast<unsigned>(kBtnCount); ++i) {
        const bool hovered = (d->state.hoveredButton == static_cast<int>(i));
        const float bx = wx + kButtons[i].offsetX;
        const float bz = floorZ - kControlLocalZ;
        DWORD cap = hovered ? 0xfffff0a0 : kBtnIdleCol[i];
        if (i == kBtnPause && d->state.paused && !hovered) cap = 0xff5a98d0;
        const float r = hovered ? 0.14f : 0.115f;
        EmitBox(dev, bx, railY, bz, r, 0.11f, hovered ? 0.065f : 0.05f, cap, 0xff6e582c);
    }
}

void DrawSteleFrame(IDirect3DDevice9 *dev, const RuntimeData *d, float floorZ,
                    DWORD accentTop, DWORD accentBot) {
    const BasePose &pose = d->state.pose;
    const float wx = CeMuseumWorld::kEntranceX + pose.localX;
    const float wy = CeMuseumWorld::kEntranceY + pose.localY;
    const float baseZ = floorZ - pose.plinthTopZ - 0.05f;
    const float steleH = 3.10f;
    const float steleW = 3.60f;
    const float centerZ = baseZ - steleH * 0.5f - 0.22f;

    // Dual brass stanchions + illuminated smoked-glass architectural frame.
    EmitBox(dev, wx - 1.45f, wy, baseZ - 0.14f, 0.16f, 0.18f, 0.14f, 0xffc8a254, 0xff8a6c34);
    EmitBox(dev, wx + 1.45f, wy, baseZ - 0.14f, 0.16f, 0.18f, 0.14f, 0xffc8a254, 0xff8a6c34);
    EmitBox(dev, wx, wy, centerZ, steleW * 0.5f + 0.16f, 0.09f, steleH * 0.5f + 0.16f, 0xffd2aa5c, 0xff9a783a);
    EmitBox(dev, wx, wy, centerZ, steleW * 0.5f + 0.06f, 0.105f, steleH * 0.5f + 0.06f, 0xff1c242c, 0xff141a20);

    if (d->content == kContentArt && d->artTexture) {
        const LightboxExtent ext = FitLightbox(d->artSrcW, d->artSrcH, steleW - 0.16f, steleH - 0.16f);
        auto clampByte = [](float v) -> unsigned {
            const int iv = static_cast<int>(v + 0.5f);
            return iv <= 0 ? 0u : (iv >= 255 ? 255u : static_cast<unsigned>(iv));
        };
        const float fade = d->state.fadeAlpha < 0.0f ? 0.0f : (d->state.fadeAlpha > 1.0f ? 1.0f : d->state.fadeAlpha);
        const float lum = 0.18f + 0.82f * fade;
        const unsigned topRgb = clampByte(255.0f * lum);
        const unsigned botR = clampByte(236.0f * lum);
        const unsigned botG = clampByte(234.0f * lum);
        const unsigned botB = clampByte(228.0f * lum);
        const DWORD topCol = 0xff000000u | (topRgb << 16) | (topRgb << 8) | topRgb;
        const DWORD botCol = 0xff000000u | (botR << 16) | (botG << 8) | botB;

        // Render lightbox artwork opaque with full texture-stage reset (matching CeMuseumArt)
        // so residual engine stage-1 alpha or texcoord transform state can never hide the plate.
        dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        dev->SetTexture(0, d->artTexture);
        dev->SetTexture(1, NULL);
        dev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
        dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);

        // South-facing (front) illuminated plate.
        const float fy = wy - 0.125f;
        const TexVert front[4] = {
            {wx - ext.halfW, fy, centerZ - ext.halfH, topCol, 0.0f, 0.0f},
            {wx + ext.halfW, fy, centerZ - ext.halfH, topCol, 1.0f, 0.0f},
            {wx - ext.halfW, fy, centerZ + ext.halfH, botCol, 0.0f, 1.0f},
            {wx + ext.halfW, fy, centerZ + ext.halfH, botCol, 1.0f, 1.0f},
        };
        dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, front, sizeof(TexVert));

        // North-facing (back) illuminated plate so visitors circling the rotunda see it too.
        const float by = wy + 0.125f;
        const TexVert back[4] = {
            {wx + ext.halfW, by, centerZ - ext.halfH, topCol, 0.0f, 0.0f},
            {wx - ext.halfW, by, centerZ - ext.halfH, topCol, 1.0f, 0.0f},
            {wx + ext.halfW, by, centerZ + ext.halfH, botCol, 0.0f, 1.0f},
            {wx - ext.halfW, by, centerZ + ext.halfH, botCol, 1.0f, 1.0f},
        };
        dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, back, sizeof(TexVert));
    } else {
        // Illuminated archival monolith panel (Cinema / Bonus / Quarantined / Unavailable).
        const float hw = steleW * 0.44f, hh = steleH * 0.42f;
        const float fy = wy - 0.12f;
        const FlatVert q[4] = {
            {wx - hw, fy, centerZ - hh, accentTop},
            {wx + hw, fy, centerZ - hh, accentTop},
            {wx - hw, fy, centerZ + hh, accentBot},
            {wx + hw, fy, centerZ + hh, accentBot},
        };
        dev->SetTexture(0, NULL);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
        dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(FlatVert));
    }
}

void DrawArenaMesh(IDirect3DDevice9 *dev, const RuntimeData *d, float floorZ) {
    const ParsedMesh &m = d->mesh;
    if (!m.vertices || !m.indices || !m.batches || !m.bounds.valid || m.batchCount == 0u) return;

    const BasePose &pose = d->state.pose;
    const float scale = ComputeNormalizedScale(m.bounds.span, kTargetSpan);
    const float cy = std::cos(d->state.yawRad);
    const float sy = std::sin(d->state.yawRad);

    // Build D3D9 world transform: center XY around bounds center, rest lowest Z (native +Z down!)
    // cleanly atop plinthTopZ, apply uniform scale and turntable Z-yaw rotation.
    const float wx = CeMuseumWorld::kEntranceX + pose.localX;
    const float wy = CeMuseumWorld::kEntranceY + pose.localY;
    const float plinthZ = floorZ - pose.plinthTopZ - 0.06f;

    // Model local offset to move center XY -> 0 and maxZ (bottom in native Z-down) -> 0.
    const float ox = -m.bounds.centerX;
    const float oy = -m.bounds.centerY;
    const float oz = -m.bounds.maxZ;

    // Row-vector matrix: p_world = (p_local + offset) * (scale * RotZ) + (wx, wy, plinthZ)
    D3DMATRIX world = {};
    world._11 =  cy * scale; world._12 = sy * scale; world._13 = 0.0f;  world._14 = 0.0f;
    world._21 = -sy * scale; world._22 = cy * scale; world._23 = 0.0f;  world._24 = 0.0f;
    world._31 =  0.0f;       world._32 = 0.0f;       world._33 = scale; world._34 = 0.0f;
    world._41 = (ox * world._11 + oy * world._21) + wx;
    world._42 = (ox * world._12 + oy * world._22) + wy;
    world._43 = (oz * scale) + plinthZ;
    world._44 = 1.0f;

    D3DMATRIX view = {}, proj = {};
    RENDERINFO.GetView(&view);
    RENDERINFO.GetProjection(&proj);

    dev->SetVertexShader(NULL);
    dev->SetPixelShader(NULL);
    dev->SetTransform(D3DTS_WORLD, &world);
    dev->SetTransform(D3DTS_VIEW, &view);
    dev->SetTransform(D3DTS_PROJECTION, &proj);

    // Balanced 3-point gallery lighting with smooth fade-through-black transition on swap.
    const float fade = d->state.fadeAlpha;
    const unsigned ambR = static_cast<unsigned>(0x66u * fade);
    const unsigned ambG = static_cast<unsigned>(0x6cu * fade);
    const unsigned ambB = static_cast<unsigned>(0x74u * fade);
    dev->SetRenderState(D3DRS_AMBIENT, D3DCOLOR_XRGB(ambR, ambG, ambB));

    auto setDirLight = [&](DWORD idx, float dx, float dy, float dz, float r, float g, float b) {
        const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
        D3DLIGHT9 l = {};
        l.Type = D3DLIGHT_DIRECTIONAL;
        l.Direction.x = dx / len;
        l.Direction.y = dy / len;
        l.Direction.z = dz / len;
        l.Diffuse.r = r * fade; l.Diffuse.g = g * fade; l.Diffuse.b = b * fade; l.Diffuse.a = 1.0f;
        l.Ambient.r = 0.18f * r * fade; l.Ambient.g = 0.18f * g * fade; l.Ambient.b = 0.18f * b * fade; l.Ambient.a = 1.0f;
        dev->SetLight(idx, &l);
        dev->LightEnable(idx, TRUE);
    };
    setDirLight(0, -0.52f,  0.54f, 0.66f, 1.00f, 0.92f, 0.80f);
    setDirLight(1,  0.64f, -0.46f, 0.56f, 0.56f, 0.68f, 0.86f);
    setDirLight(2,  0.00f,  0.18f, 0.96f, 0.48f, 0.50f, 0.52f);
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
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
    dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
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
    dev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    dev->SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1);

    for (uint32_t bi = 0; bi < m.batchCount; ++bi) {
        const ArchiveBatch &b = m.batches[bi];
        if (b.triangleCount == 0u) continue;
        IDirect3DTexture9 *tex = (b.textureIndex >= 0 && static_cast<uint32_t>(b.textureIndex) < kMaxMeshTextures)
            ? d->meshTextures[b.textureIndex] : NULL;
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
        dev->DrawIndexedPrimitiveUP(
            D3DPT_TRIANGLELIST,
            0,
            m.vertexCount,
            b.triangleCount,
            m.indices + b.firstIndex,
            D3DFMT_INDEX16,
            m.vertices,
            sizeof(ArchiveVertex));
    }
}

} // namespace

void RuntimeInit() {
    RuntimeShutdown();
    RuntimeData *d = EnsureData();
    if (!d) return;
    ResetState(&d->state);
    d->hasLoaded = false;
    d->loadedGeneration = 0;
}

void RuntimeShutdown() {
    if (!gData) return;
    ClearActiveExhibit(gData);
    gData->arena.Shutdown();
    if (gData->retailMeshes) {
        std::free(gData->retailMeshes);
        gData->retailMeshes = NULL;
    }
    gData->retailMeshCount = 0;
    if (gData->catalog) {
        PortCeAssetCatalogClose(gData->catalog);
        gData->catalog = NULL;
    }
    std::free(gData);
    gData = NULL;
}

void RuntimeSetBasePose(float worldCenterX, float worldCenterY, float plinthTopZ) {
    RuntimeData *d = EnsureData();
    if (!d) return;
    d->state.pose.localX = worldCenterX - CeMuseumWorld::kEntranceX;
    d->state.pose.localY = worldCenterY - CeMuseumWorld::kEntranceY;
    d->state.pose.plinthTopZ = plinthTopZ > 0.1f ? plinthTopZ : kPedestalPlinthTopZ;
    d->state.pose.plinthHalf = kPedestalPlinthHalf;
    d->state.pose.customWorldPose = true;
}

void RuntimeUpdate(CeMuseumTour::Wing wing, float dtSeconds, bool paused,
                   float eyeX, float eyeY, float eyeZ,
                   float aimX, float aimY, float aimZ) {
    RuntimeData *d = EnsureData();
    if (!d) return;
    d->lastWing = wing;

    if (!ActiveInWing(d->state, wing)) {
        d->state.hoveredButton = -1;
        d->state.visitorNear = false;
        return;
    }

    if (!d->hasLoaded || d->loadedGeneration != d->state.swapGeneration)
        LoadCurrentExhibit(d);

    StepState(&d->state, dtSeconds, paused);
    d->state.visitorNear = InteractiveInWing(d->state, wing) &&
                           IsVisitorNearPedestal(d->state.pose, eyeX, eyeY);
    d->state.hoveredButton = InteractiveInWing(d->state, wing)
        ? PickButton(d->state.pose, eyeX, eyeY, eyeZ, aimX, aimY, aimZ)
        : -1;
}

bool RuntimeHandleFire(bool release, float value) {
    if (!gData || !gData->state.initialized) return false;
    const bool usable = gData->state.hoveredButton >= 0 && gData->state.hoveredButton < kBtnCount;
    const CeCinemaRoom::FireIntent intent = CeCinemaRoom::FireEdge(
        release || value <= 0.0f, usable, gData->state.fireHeld, gData->state.fireCaptured);
    if (intent == CeCinemaRoom::FireIntent::Operate && usable) {
        const int btn = gData->state.hoveredButton;
        if (ActivateButton(&gData->state, btn)) {
            if (gData->loadedGeneration != gData->state.swapGeneration)
                LoadCurrentExhibit(gData);
            PortSessionLog(PORT_SESSION_GAME, false,
                           "archive pedestal: %s -> %s (%s)",
                           kButtons[btn].label, CategoryName(gData->state.category), gData->activeTitle);
            return true;
        }
    }
    return intent != CeCinemaRoom::FireIntent::Ordinary;
}

bool RuntimeHandleKey(unsigned dik, bool repeat) {
    if (!gData || !gData->state.initialized) return false;
    if (!HandleKeyCode(&gData->state, dik, repeat)) return false;
    if (gData->loadedGeneration != gData->state.swapGeneration)
        LoadCurrentExhibit(gData);
    return true;
}

bool RuntimeIsAimingControl() {
    return gData && gData->state.initialized && gData->state.hoveredButton >= 0;
}

bool RuntimeCue(float visitorX, float visitorY, float fwdX, float fwdY,
                CeMuseumTour::Wing wing,
                char *title, unsigned titleCap,
                char *line, unsigned lineCap,
                char *note, unsigned noteCap) {
    if (!gData || !gData->state.initialized || !title || !titleCap || !line || !lineCap) return false;
    if (!InteractiveInWing(gData->state, wing)) return false;

    const int btn = gData->state.hoveredButton;
    if (btn >= 0 && btn < kBtnCount) {
        const ButtonSpec &bs = kButtons[btn];
        std::snprintf(title, titleCap, "Archive Pedestal - %s", bs.label);
        if (gData->activeTitle[0])
            std::snprintf(line, lineCap, "%s - %s", CategoryName(gData->state.category), gData->activeTitle);
        else
            std::snprintf(line, lineCap, "%s", bs.cue);
        if (note && noteCap) {
            if (btn == kBtnPause)
                std::snprintf(note, noteCap, "Press Fire to %s rotation (or P key).",
                              gData->state.paused ? "resume" : "pause");
            else
                std::snprintf(note, noteCap, "Press Fire to %s", bs.cue);
        }
        return true;
    }

    if (!FocusPedestal(gData->state.pose, visitorX, visitorY, fwdX, fwdY)) return false;
    std::snprintf(title, titleCap, "%s",
                  gData->activeTitle[0] ? gData->activeTitle : CategoryName(gData->state.category));
    std::snprintf(line, lineCap, "%s",
                  gData->activeLine[0] ? gData->activeLine : "Interactive Archive Pedestal");
    if (note && noteCap)
        std::snprintf(note, noteCap, "%s",
                      gData->activeNote[0] ? gData->activeNote : "Aim at brass controls to browse.");
    return true;
}

void RuntimeDraw(float floorZ, bool ownStateBlock) {
    if (!gData || !gData->state.initialized || !ActiveInWing(gData->state, gData->lastWing)) return;
    IDirect3DDevice9 *dev = LT.GetDevice();
    if (!dev) return;
    if (gData->device && (gData->device != dev || gData->deviceGen != CurrentPedestalDeviceGen())) {
        ReleaseGpuTextures(gData);
        gData->hasLoaded = false;
        LoadCurrentExhibit(gData);
    }

    IDirect3DStateBlock9 *saved = NULL;
    if (ownStateBlock) {
        if (FAILED(dev->CreateStateBlock(D3DSBT_ALL, &saved)) || !saved) return;
        if (FAILED(saved->Capture())) { saved->Release(); return; }
    }

    // 1. Draw brass pedestal collar, 4 interactive buttons, and lightbox/monolith stele when active.
    {
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
        dev->SetRenderState(D3DRS_LIGHTING, FALSE);
        dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
        dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
        dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
        dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        dev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
        dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);

        DrawControlConsoleAndRing(dev, gData, floorZ);

        if (gData->content == kContentArt)
            DrawSteleFrame(dev, gData, floorZ, 0xff283440, 0xff182028);
        else if (gData->content == kContentCinema)
            DrawSteleFrame(dev, gData, floorZ, 0xff6e2c34, 0xff301418);
        else if (gData->content == kContentBonus)
            DrawSteleFrame(dev, gData, floorZ, 0xff2c5d88, 0xff16324c);
        else if (gData->content == kContentQuarantined)
            DrawSteleFrame(dev, gData, floorZ, 0xff7a4d22, 0xff3d2410);
        else if (gData->content == kContentUnavailable)
            DrawSteleFrame(dev, gData, floorZ, 0xff46505a, 0xff2a3036);
    }

    // 2. Draw active 3D mesh from the 8 MiB host-malloc swap arena.
    if (gData->content == kContentMesh)
        DrawArenaMesh(dev, gData, floorZ);

    if (saved) {
        saved->Apply();
        saved->Release();
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
}

} // namespace CeArchivePedestal
#else
#include "ce_archive_pedestal.h"
namespace CeArchivePedestal {
void RuntimeInit() {}
void RuntimeShutdown() {}
void RuntimeUpdate(CeMuseumTour::Wing, float, bool, float, float, float, float, float, float) {}
bool RuntimeHandleFire(bool, float) { return false; }
bool RuntimeHandleKey(unsigned, bool) { return false; }
bool RuntimeIsAimingControl() { return false; }
bool RuntimeCue(float, float, float, float, CeMuseumTour::Wing, char *, unsigned, char *, unsigned, char *, unsigned) { return false; }
void RuntimeDraw(float, bool) {}
void RuntimeSetBasePose(float, float, float) {}
} // namespace CeArchivePedestal
#endif
