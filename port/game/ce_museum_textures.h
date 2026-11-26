// SPDX-License-Identifier: GPL-3.0-only
// Runtime loader for the Grand Museum's original GPL-3.0-only architectural textures
// (generated into ce_museum_textures_data.h by port/tools/generate_museum_textures.py)
// plus one-time runtime-baked 3D portal signage and Atrium Directory floor-plan atlases.
// All textures upload into D3DPOOL_MANAGED with full mipmap chains for D3D9 Reset safety.
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

// Eight 1024x64 strips packed into the 1024x512 baked Portal Lintel Signage Atlas.
enum CeMuseumPortalSignId {
    CE_MUSEUM_SIGN_FORSETI = 0,       // "FORSETI FORCES"
    CE_MUSEUM_SIGN_MUSPELL,           // "MUSPELL DOMINION"
    CE_MUSEUM_SIGN_HANGAR,            // "AVIATION & NAVAL HANGAR"
    CE_MUSEUM_SIGN_CONCEPT_ART,       // "CONCEPT ART GALLERY"
    CE_MUSEUM_SIGN_EARLY_DESIGNS,     // "EARLY DESIGNS VAULT"
    CE_MUSEUM_SIGN_SCREENING,         // "SCREENING ROOM"
    CE_MUSEUM_SIGN_ATRIUM,            // "GRAND ATRIUM"
    CE_MUSEUM_SIGN_COURTYARD,         // "COASTAL COURTYARD"
    CE_MUSEUM_SIGN_COUNT
};

// Six quick-jump button tiles packed into the bottom band of the 512x512 Directory Atlas.
enum CeMuseumDirButtonId {
    CE_MUSEUM_DIR_BTN_FORSETI = 0,    // "FORSETI"
    CE_MUSEUM_DIR_BTN_MUSPELL,        // "MUSPELL"
    CE_MUSEUM_DIR_BTN_HANGAR,         // "HANGAR"
    CE_MUSEUM_DIR_BTN_CONCEPT_ART,    // "CONCEPT ART"
    CE_MUSEUM_DIR_BTN_EARLY_DESIGNS,  // "EARLY DESIGNS"
    CE_MUSEUM_DIR_BTN_CINEMA,         // "CINEMA"
    CE_MUSEUM_DIR_BTN_COUNT
};

struct CeMuseumAtlasUv {
    float u0, v0, u1, v1;
};

struct IDirect3DDevice9;
struct IDirect3DTexture9;

namespace CeMuseumTextures {
constexpr unsigned kPortalAtlasWidth     = 1024u;
constexpr unsigned kPortalAtlasHeight    = 512u;
constexpr unsigned kPortalStripHeight    = 64u;
constexpr unsigned kDirectoryAtlasWidth  = 512u;
constexpr unsigned kDirectoryAtlasHeight = 512u;
constexpr unsigned kDirectoryMapHeight   = 384u;
constexpr unsigned kAtlasMipLevels       = 4u;

// Upload all embedded architectural textures and bake signage/directory atlases into managed D3D9 textures.
// Missing or failed uploads leave their slot null so callers keep plain-colour fallback.
void LoadAll(IDirect3DDevice9 *device);
void ReleaseAll();
IDirect3DTexture9 *Get(CeMuseumTexId id);
const char *Name(CeMuseumTexId id);

IDirect3DTexture9 *GetPortalSignAtlas();
IDirect3DTexture9 *GetDirectoryAtlas();
const char *PortalSignLabel(CeMuseumPortalSignId id);
const char *DirectoryButtonLabel(CeMuseumDirButtonId id);
CeMuseumAtlasUv PortalSignUv(CeMuseumPortalSignId id);
CeMuseumAtlasUv DirectoryMapUv();
CeMuseumAtlasUv DirectoryButtonUv(CeMuseumDirButtonId id);

// Pure CPU rasterization & per-cell mipmap downsampling (0xAARRGGBB top-down row-major) shared by runtime and host tests.
void BakePortalSignAtlasRgba(unsigned int *outArgb);
void BakeDirectoryAtlasRgba(unsigned int *outArgb);
void DownsamplePortalSignAtlasLevel(const unsigned int *baseArgb, unsigned level, unsigned int *outArgb);
void DownsampleDirectoryAtlasLevel(const unsigned int *baseArgb, unsigned level, unsigned int *outArgb);
} // namespace CeMuseumTextures

#endif
