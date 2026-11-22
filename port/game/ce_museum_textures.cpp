// SPDX-License-Identifier: GPL-3.0-only
// Decompresses and uploads the Grand Museum's original GPL-3.0-only architectural textures.
// Plain-colour fallback remains active if any texture fails upload or is forced missing by
// BEA_PORT_MUSEUM_MISSING_TEXTURE.
#ifdef CE_VERSION
#include "common.h"
#include "ltshell.h"
#include "d3dx9.h"
#include "ce_museum_textures.h"
#include "ce_museum_textures_data.h"
#include "../platform/common/portlog.h"
#include <cstdlib>
#include <cstring>

extern "C" int uncompress(unsigned char *dest, unsigned long *destLen,
                          const unsigned char *source, unsigned long sourceLen);

namespace CeMuseumTextures {

namespace {
static_assert(static_cast<unsigned>(CE_MUSEUM_TEX_COUNT) == static_cast<unsigned>(CeMuseumTexturesData::kCount),
              "CeMuseumTexId must match CeMuseumTexturesData::kCount");

IDirect3DTexture9 *gTextures[CE_MUSEUM_TEX_COUNT] = {};

bool ForcedMissing(const char *slug) {
    const char *missing = std::getenv("BEA_PORT_MUSEUM_MISSING_TEXTURE");
    return slug && missing && *missing && std::strstr(slug, missing) != NULL;
}
} // namespace

const char *Name(CeMuseumTexId id) {
    const unsigned idx = static_cast<unsigned>(id);
    if (idx >= static_cast<unsigned>(CE_MUSEUM_TEX_COUNT)) return "";
    return CeMuseumTexturesData::kEntries[idx].name;
}

void ReleaseAll() {
    for (unsigned i = 0; i < static_cast<unsigned>(CE_MUSEUM_TEX_COUNT); ++i) {
        if (gTextures[i]) {
            gTextures[i]->Release();
            gTextures[i] = NULL;
        }
    }
}

void LoadAll(IDirect3DDevice9 *device) {
    ReleaseAll();
    if (!device) return;
    unsigned loaded = 0;
    for (unsigned i = 0; i < static_cast<unsigned>(CE_MUSEUM_TEX_COUNT); ++i) {
        const CeMuseumTexturesData::Entry &entry = CeMuseumTexturesData::kEntries[i];
        if (ForcedMissing(entry.name)) {
            PortSessionLog(PORT_SESSION_GAME, false,
                           "museum: architectural texture %s skipped by test override; using plain colour",
                           entry.name);
            continue;
        }
        if (!entry.packed || !entry.packedSize || !entry.rawSize || entry.rawSize > (4u << 20)) continue;
        unsigned char *raw = new unsigned char[entry.rawSize];
        if (!raw) continue;
        unsigned long outLen = entry.rawSize;
        const int rc = uncompress(raw, &outLen, entry.packed, entry.packedSize);
        if (rc == 0 && outLen == entry.rawSize) {
            IDirect3DTexture9 *tex = NULL;
            const HRESULT hr = D3DXCreateTextureFromFileInMemoryEx(
                device, raw, static_cast<UINT>(outLen),
                static_cast<UINT>(entry.width), static_cast<UINT>(entry.height),
                0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
                D3DX_FILTER_TRIANGLE, D3DX_FILTER_BOX, 0, NULL, NULL, &tex);
            if (SUCCEEDED(hr) && tex) {
                gTextures[i] = tex;
                ++loaded;
            } else {
                if (tex) tex->Release();
                PortSessionLog(PORT_SESSION_GAME, false,
                               "museum: architectural texture %s failed upload; using plain colour",
                               entry.name);
            }
        }
        delete[] raw;
    }
    PortSessionLog(PORT_SESSION_GAME, false,
                   "museum: loaded %u of %u architectural textures",
                   loaded, static_cast<unsigned>(CE_MUSEUM_TEX_COUNT));
}

IDirect3DTexture9 *Get(CeMuseumTexId id) {
    const unsigned idx = static_cast<unsigned>(id);
    return idx < static_cast<unsigned>(CE_MUSEUM_TEX_COUNT) ? gTextures[idx] : NULL;
}

} // namespace CeMuseumTextures
#else
#include "ce_museum_textures.h"
namespace CeMuseumTextures {
void LoadAll(IDirect3DDevice9 *) {}
void ReleaseAll() {}
IDirect3DTexture9 *Get(CeMuseumTexId) { return 0; }
const char *Name(CeMuseumTexId) { return ""; }
} // namespace CeMuseumTextures
#endif
