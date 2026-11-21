// SPDX-License-Identifier: GPL-3.0-only
#ifndef CE_ASSET_CATALOG_H
#define CE_ASSET_CATALOG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#define CE_ASSET_NOEXCEPT noexcept
#else
#define CE_ASSET_NOEXCEPT
#endif

/*
 * A read-only snapshot of the player's selected installation. Strings returned
 * by this interface are UTF-8, owned by the snapshot and valid until it closes.
 * Paths are relative to the selected root and use '/'. The caller owns the root
 * and must revalidate it and the chosen file when opening an asset later.
 *
 * Names and paths are searched case-insensitively for ASCII; non-ASCII bytes
 * match exactly. Diagnostics describe incomplete folders or refused banks so
 * an empty or partial result is not mistaken for a complete inventory.
 */
typedef struct CeAssetCatalog CeAssetCatalog;
enum CeAssetKind {
    CE_ASSET_MODEL = 1,
    CE_ASSET_TEXTURE = 2,
    CE_ASSET_SOUND = 4,
    CE_ASSET_ALL = CE_ASSET_MODEL | CE_ASSET_TEXTURE | CE_ASSET_SOUND,
    /* Spoken radio lines, the .ogg files of data/sounds/<language>/MessageBox: one entry per file, its group the
       language folder, its size the file's bytes. Asked for by name; not part of CE_ASSET_ALL, which keeps
       the Companion's three kinds. */
    CE_ASSET_SPEECH = 8
};

CeAssetCatalog* PortCeAssetCatalogOpen(const char* game_root, char* error, size_t error_capacity) CE_ASSET_NOEXCEPT;
/* Scan only the folders needed for this view; omitted kinds incur no file reads. */
CeAssetCatalog* PortCeAssetCatalogOpenKinds(const char* game_root, int kinds, char* error, size_t error_capacity) CE_ASSET_NOEXCEPT;
void PortCeAssetCatalogClose(CeAssetCatalog* catalog) CE_ASSET_NOEXCEPT;
size_t PortCeAssetCatalogCount(const CeAssetCatalog* catalog) CE_ASSET_NOEXCEPT;
int PortCeAssetCatalogKind(const CeAssetCatalog* catalog, size_t index) CE_ASSET_NOEXCEPT;
const char* PortCeAssetCatalogTitle(const CeAssetCatalog* catalog, size_t index) CE_ASSET_NOEXCEPT;
const char* PortCeAssetCatalogPath(const CeAssetCatalog* catalog, size_t index) CE_ASSET_NOEXCEPT;
const char* PortCeAssetCatalogGroup(const CeAssetCatalog* catalog, size_t index) CE_ASSET_NOEXCEPT;
/* File bytes for models and textures, decoded PCM bytes for a sound. */
uint64_t PortCeAssetCatalogSize(const CeAssetCatalog* catalog, size_t index) CE_ASSET_NOEXCEPT;
/* Sound's zero-based index in its bank, or -1 for another kind or bad index. */
int PortCeAssetCatalogSampleIndex(const CeAssetCatalog* catalog, size_t index) CE_ASSET_NOEXCEPT;

/* The stored DDS header from one installed texture, not the format requested by its filename.
   Reads only enough bounded AYA members for that header. Does not decode or certify the image payload. */
typedef struct CeAssetTextureHeader {
    uint32_t width, height, levels;
    char format[20];
} CeAssetTextureHeader;
int PortCeAssetCatalogTextureHeader(const CeAssetCatalog* catalog, size_t index, CeAssetTextureHeader* header) CE_ASSET_NOEXCEPT;

/* Delivers the complete validated, bounded DDS container to the game's image loader.
   The data is borrowed for the duration of the callback only. */
typedef int (*CeAssetTextureReader)(const unsigned char* dds, size_t bytes, const CeAssetTextureHeader* header, void* context);
int PortCeAssetCatalogReadTexture(const CeAssetCatalog* catalog, size_t index, CeAssetTextureReader reader, void* context) CE_ASSET_NOEXCEPT;

/* Pass SIZE_MAX as 'after' to start; SIZE_MAX is also the end/not-found value. */
size_t PortCeAssetCatalogNext(const CeAssetCatalog* catalog, size_t after, int kind_mask, const char* text) CE_ASSET_NOEXCEPT;
/* Resolve a model's texture request using the Companion's first-match order. */
size_t PortCeAssetCatalogFindTexture(const CeAssetCatalog* catalog, const char* requested) CE_ASSET_NOEXCEPT;

size_t PortCeAssetCatalogDiagnosticCount(const CeAssetCatalog* catalog) CE_ASSET_NOEXCEPT;
const char* PortCeAssetCatalogDiagnosticPath(const CeAssetCatalog* catalog, size_t index) CE_ASSET_NOEXCEPT;
const char* PortCeAssetCatalogDiagnosticMessage(const CeAssetCatalog* catalog, size_t index) CE_ASSET_NOEXCEPT;

#ifdef __cplusplus
}
#endif
#undef CE_ASSET_NOEXCEPT
#endif
