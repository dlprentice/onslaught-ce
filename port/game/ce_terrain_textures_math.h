// SPDX-License-Identifier: GPL-3.0-only
// Pure layout, focus, and toroidal safety math for expanded CE terrain textures.
// Kept free of D3D9 and game headers so host unit tests compile the exact logic.
#ifndef CE_TERRAIN_TEXTURES_MATH_H
#define CE_TERRAIN_TEXTURES_MATH_H

#include <cmath>

static const int kCeTerrainMapTiles = 64;
static const float kCeTerrainTileSize = 8.0f;
static const int kCeTerrainMaxLevels = 5;

// Largest square atlas texture dimension we use for expanded terrain caches:
// 4096 in 1P on modern GPUs (so L1, L2, AND L3 hold the entire 64x64 map permanently and L4 covers
// 32x32 tiles = 4x the area), capped to 2048 in 2P split-screen for 32-bit VA headroom, 1024 on
// older hardware, or 0 to fall back to retail 512.
inline unsigned CeTerrainMaxAtlasSize(unsigned maxTextureWidth, unsigned maxTextureHeight, int numViews = 1)
{
    const unsigned limit = maxTextureWidth < maxTextureHeight ? maxTextureWidth : maxTextureHeight;
    if (numViews <= 1 && limit >= 4096u) return 4096u;
    if (limit >= 2048u) return 2048u;
    if (limit >= 1024u) return 1024u;
    return 0u;
}

inline int CeTerrainLevelTilePx(int level)
{
    if (level < 0 || level >= kCeTerrainMaxLevels) return 0;
    return 8 << level;
}

// Level 0 uses the game's whole-map 512x512 texture; levels 1..4 grow up to maxAtlas
// (capped at the full 64x64 map size, so L1 is 1024 and L2 is 2048 on 2048-capable GPUs).
inline unsigned CeTerrainLevelAtlasPx(int level, unsigned maxAtlas)
{
    if (level <= 0 || level >= kCeTerrainMaxLevels) return 512u;
    if (maxAtlas < 1024u) return 512u;
    const unsigned fullMap = static_cast<unsigned>(kCeTerrainMapTiles * (8 << level));
    return fullMap < maxAtlas ? fullMap : maxAtlas;
}

inline int CeTerrainLevelTilesPerAxis(int level, unsigned maxAtlas)
{
    const int tilePx = CeTerrainLevelTilePx(level);
    if (tilePx <= 0) return 0;
    return static_cast<int>(CeTerrainLevelAtlasPx(level, maxAtlas) / static_cast<unsigned>(tilePx));
}

// Maximum Chebyshev radius (in tiles from focusTile) that may be drawn at `level` while
// guaranteeing that every drawn tile PLUS its 1-tile seam-prevention halo occupies strictly
// fewer than tilesPerAxis slots per axis (zero toroidal wrap collisions, ever).
//   tilesPerAxis == 64 -> whole map fits (safeHalf = 64)
//   tilesPerAxis == 32 -> core 29x29 (safeHalf = 14) + 1-tile halo = 31x31 < 32x32
//   tilesPerAxis == 16 -> core 13x13 (safeHalf = 6)  + 1-tile halo = 15x15 < 16x16
//   tilesPerAxis == 8  -> core 5x5   (safeHalf = 2)  + 1-tile halo = 7x7   < 8x8
inline int CeTerrainLevelSafeHalf(int level, unsigned maxAtlas)
{
    const int n = CeTerrainLevelTilesPerAxis(level, maxAtlas);
    if (n >= kCeTerrainMapTiles) return kCeTerrainMapTiles;
    const int half = (n / 2) - 2;
    return half > 0 ? half : 0;
}

inline float CeTerrainLevelUvScale(int level, unsigned maxAtlas)
{
    const int n = CeTerrainLevelTilesPerAxis(level, maxAtlas);
    if (n <= 0) return 1.0f / 512.0f;
    return 1.0f / (static_cast<float>(n) * kCeTerrainTileSize);
}

inline int CeTerrainAtlasSlot(int tileX, int tileY, int tilesPerAxis)
{
    const int mask = tilesPerAxis - 1;
    return (tileY & mask) * tilesPerAxis + (tileX & mask);
}

struct CeTerrainViewFocus
{
    float camX, camY, camZ;
    int focusTileX[kCeTerrainMaxLevels];
    int focusTileY[kCeTerrainMaxLevels];
    int safeHalf[kCeTerrainMaxLevels];
    int maxTexLevel;
    unsigned maxAtlas;
};

inline int CeTerrainClampTile(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// Compute per-level toroidal focus centers for both low-angle Battle Engine views and
// steep overhead Commander cameras. In the game's coordinate system, negative Z is up,
// so altitude above ground is (groundZ - camZ) and looking downward has dirZ > 0.
inline CeTerrainViewFocus CeTerrainComputeFocus(
    float camX, float camY, float camZ,
    float dirX, float dirY, float dirZ,
    float groundZ, int numLevels, int maxLevelsUser, unsigned maxAtlas)
{
    CeTerrainViewFocus f = {};
    f.camX = camX;
    f.camY = camY;
    f.camZ = camZ;
    int maxLvl = numLevels - 1;
    if (maxLevelsUser - 1 < maxLvl) maxLvl = maxLevelsUser - 1;
    if (maxLvl >= kCeTerrainMaxLevels) maxLvl = kCeTerrainMaxLevels - 1;
    if (maxLvl < 0) maxLvl = 0;
    f.maxTexLevel = maxLvl;
    f.maxAtlas = maxAtlas;

    const float alt = groundZ > camZ ? (groundZ - camZ) : 0.0f;
    const float hlen = std::sqrt(dirX * dirX + dirY * dirY);
    const float hx = hlen > 1e-4f ? (dirX / hlen) : 0.0f;
    const float hy = hlen > 1e-4f ? (dirY / hlen) : 0.0f;

    const bool steepOverhead = (dirZ > 0.25f) && (alt > 12.0f);
    float hitX = camX, hitY = camY, hitHoriz = 1e6f;
    if (dirZ > 0.15f && alt > 0.5f) {
        const float hitT = alt / dirZ;
        hitHoriz = hitT * hlen;
        if (hitHoriz <= 200.0f) {
            hitX = camX + dirX * hitT;
            hitY = camY + dirY * hitT;
        }
    }

    for (int l = 0; l < kCeTerrainMaxLevels; ++l) {
        const int safe = CeTerrainLevelSafeHalf(l, maxAtlas);
        f.safeHalf[l] = safe;
        if (safe >= kCeTerrainMapTiles) {
            f.focusTileX[l] = 32;
            f.focusTileY[l] = 32;
            continue;
        }
        const float halfMeters = static_cast<float>(safe) * kCeTerrainTileSize;
        float fx = camX, fy = camY;
        if (steepOverhead && hitHoriz <= 160.0f) {
            fx = hitX;
            fy = hitY;
        } else {
            float ahead = halfMeters * 0.70f;
            if (dirZ > 0.20f && hitHoriz < ahead) ahead = hitHoriz;
            fx = camX + hx * ahead;
            fy = camY + hy * ahead;
        }
        const int lo = safe;
        const int hi = (kCeTerrainMapTiles - 1) - safe;
        const int rawX = static_cast<int>(std::floor(fx * (1.0f / kCeTerrainTileSize)));
        const int rawY = static_cast<int>(std::floor(fy * (1.0f / kCeTerrainTileSize)));
        f.focusTileX[l] = lo <= hi ? CeTerrainClampTile(rawX, lo, hi) : 32;
        f.focusTileY[l] = lo <= hi ? CeTerrainClampTile(rawY, lo, hi) : 32;
    }
    return f;
}

inline bool CeTerrainTileInsideSafeBox(const CeTerrainViewFocus &f, int level, int tileX, int tileY)
{
    const int safe = f.safeHalf[level];
    if (safe >= kCeTerrainMapTiles) return true;
    const int dx = tileX - f.focusTileX[level];
    const int dy = tileY - f.focusTileY[level];
    return dx >= -safe && dx <= safe && dy >= -safe && dy <= safe;
}

// Select the finest texture level for tile (tileX, tileY) that matches screen-space
// resolution (avoiding both blur and distant non-mipped shimmer) and fits strictly
// inside the level's zero-wrap safe toroidal window.
inline int CeTerrainChooseLevel(const CeTerrainViewFocus &f, int tileX, int tileY, float midHeight)
{
    if (f.maxTexLevel <= 0) return 0;
    const float cx = static_cast<float>(tileX) * kCeTerrainTileSize + 4.0f;
    const float cy = static_cast<float>(tileY) * kCeTerrainTileSize + 4.0f;
    const float dx = cx - f.camX;
    const float dy = cy - f.camY;
    const float dz = midHeight - f.camZ;
    const float dist2 = dx * dx + dy * dy + dz * dz;

    const float l4Dist = f.maxAtlas >= 4096u ? 125.0f : 90.0f;
    const float l3Dist = f.maxAtlas >= 4096u ? 280.0f : 185.0f;
    const float l2Dist = f.maxAtlas >= 4096u ? 520.0f : 360.0f;

    if (f.maxTexLevel >= 4 && dist2 <= l4Dist * l4Dist && CeTerrainTileInsideSafeBox(f, 4, tileX, tileY))
        return 4;
    if (f.maxTexLevel >= 3 && dist2 <= l3Dist * l3Dist && CeTerrainTileInsideSafeBox(f, 3, tileX, tileY))
        return 3;
    if (f.maxTexLevel >= 2 && dist2 <= l2Dist * l2Dist && CeTerrainTileInsideSafeBox(f, 2, tileX, tileY))
        return 2;
    if (f.maxTexLevel >= 1 && CeTerrainTileInsideSafeBox(f, 1, tileX, tileY))
        return 1;
    return 0;
}

#endif
