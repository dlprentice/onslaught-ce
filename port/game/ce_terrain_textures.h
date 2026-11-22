// SPDX-License-Identifier: GPL-3.0-only
#ifndef CE_TERRAIN_TEXTURES_H
#define CE_TERRAIN_TEXTURES_H

#include "maths.h"

struct IDirect3DTexture9;

// Expanded toroidal landscape texture atlases and 1-tile seam-prevention halo for
// ordinary CE Extended terrain sessions. Allocates nothing in Normal or faithful runs
// and leaves CDXLandscape/CLandscapeView/CLandscapeTexture object layouts untouched.
void CeTerrainTexturesInit(int numViews, const int *viewLevels);
void CeTerrainTexturesShutdown();
bool CeTerrainTexturesBeginView(int viewpoint, const FVector &camPos, const FVector &lookDir,
                                int numLevels, int maxLevelsUser, bool mirrorPass);
bool CeTerrainTexturesActive(int viewpoint);
int CeTerrainTexturesChoose(int viewpoint, int numLevels, int maxLevelsUser,
                            int tileX, int tileY, float midHeight);
void CeTerrainTexturesQueueTile(int viewpoint, int texLevel, unsigned short tile, int patchLevel);
void CeTerrainTexturesFlush(int viewpoint);
IDirect3DTexture9 *CeTerrainTexturesGetTexture(int viewpoint, int level);
float CeTerrainTexturesUvScale(int viewpoint, int level);
const unsigned short *CeTerrainTexturesDrawTiles(int viewpoint, int level, int *outCount);
void CeTerrainTexturesInvalidateTile(unsigned short tile);

#endif
