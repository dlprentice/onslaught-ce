// SPDX-License-Identifier: GPL-3.0-only
// Runtime loader for the Grand Museum's original GPL-3.0-only architectural textures
// (generated into ce_museum_textures_data.h by port/tools/generate_museum_textures.py).
// Textures upload into D3DPOOL_MANAGED with full mipmap chains for D3D9 Reset safety.
#ifndef CE_MUSEUM_TEXTURES_H
#define CE_MUSEUM_TEXTURES_H

enum CeMuseumTexId {
    CE_MUSEUM_TEX_LIMESTONE_WALL = 0,
    CE_MUSEUM_TEX_SLATE_PIER,
    CE_MUSEUM_TEX_GALLERY_FLOOR,
    CE_MUSEUM_TEX_COURTYARD_PAVER,
    CE_MUSEUM_TEX_RUNNER_STONE,
    CE_MUSEUM_TEX_COFFERED_CEILING,
    CE_MUSEUM_TEX_BRUSHED_BRASS,
    CE_MUSEUM_TEX_WATER_BASIN,
    CE_MUSEUM_TEX_FORSETI_TRIM,
    CE_MUSEUM_TEX_MUSPELL_TRIM,
    CE_MUSEUM_TEX_CINEMA_ACOUSTIC,
    CE_MUSEUM_TEX_PLINTH_MARBLE,
    CE_MUSEUM_TEX_FORGED_IRON,
    CE_MUSEUM_TEX_COUNT
};

struct IDirect3DDevice9;
struct IDirect3DTexture9;

namespace CeMuseumTextures {
// Upload all embedded architectural textures into managed D3D9 textures.
// Missing or failed uploads leave their slot null so callers keep plain-colour fallback.
void LoadAll(IDirect3DDevice9 *device);
void ReleaseAll();
IDirect3DTexture9 *Get(CeMuseumTexId id);
const char *Name(CeMuseumTexId id);
} // namespace CeMuseumTextures

#endif
