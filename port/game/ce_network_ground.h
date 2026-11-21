// SPDX-License-Identifier: GPL-3.0-only
// The ground blend of the Building Defence levels (856 and 858) in a network co-op game (patch 0232).
//
// Both levels ship the same unfinished landscape: a flat height field (every sample 0) whose mixer layers are a
// checkerboard of the "sand" texture and blobs of the "snow" one over the grass, so the ground shows large pink and
// teal patches with hard edges. Their light map (MIXERMAP.mMap, 512 x 512 levels into the height field's colour table) is
// a patchwork too: the shading and hill shadows of other terrain, in regions with straight edges, on ground that has no
// relief. In a co-op session both are replaced right after they are read: the mixer map by one layer of the level's own
// "rock" texture over its grass, weighted by integer value noise (soft rock mottling like the interior of the finished
// island levels, 850 and 200, that use the same texture set), and the light map by the level that flat open ground has
// on those levels (32, their commonest lit value) with a gentle low-frequency variation. Nothing is shipped: the textures
// are the player's own; only the weights (signed 9x9 samples per 8x8-unit tile) and light levels are computed here.
//
// The mixer map is read only by the landscape's texture builder (CHeightField::Unknown47eff0) and the Commander's map
// view; no simulation code reads it, so the lockstep state does not depend on it. The weights are integer arithmetic
// of the tile position alone, the same on every peer and independent of graphics options. Outside a co-op session (and
// in a build without the network) the level keeps its original data: plain -level 856 shows the original.
#ifndef CE_NETWORK_GROUND_H
#define CE_NETWORK_GROUND_H
#include <stdint.h>

enum {
	CE_GROUND_TILES = 64,				// 64 x 64 tiles, each 9 x 9 weight and height samples
	CE_GROUND_SAMPLES = 81,
	CE_GROUND_ROCK = 1					// the texture set's second mixer texture ("rock": MIXER_NAMES in maptex.cpp)
};

// 1 when the loaded map is the Building Defence ground in a co-op session: sharing the level (CE_LockstepSharing),
// level 856 or 858, and a height field whose samples are all 0 (a mod that gives the level real terrain keeps its own
// blend). heights may be NULL (then 0).
int CeGroundEligible(int sharing, uint32_t level, const int16_t *heights);

// The weight of the rock layer at sample (x, y), 0..512 on both axes (tile * 8 + sample): -40..36.
int CeGroundWeight(int x, int y);
// The light level at texel (x, y) of the 512 x 512 light map: 28..33.
int CeGroundLight(int x, int y);

// Whether the last map loaded was rewritten (CMap::Deserialize sets it after every map). While it is, CDXWater::Render
// draws the water plane CeGroundWaterDrop() units lower (the game's Z grows downward): the flat ground lies at the water
// level and the plane drawn there z-fights with it, while one unit down the ground hides it on the map and the sea still
// shows beyond the map's edge, as it did. Drawing only: MAP.GetWaterLevel() and everything that reads it are unchanged.
void CeGroundSetApplied(int applied);
float CeGroundWaterDrop();

// Rewrites the slots in place (one layer, the rock texture, the weights above) and the light map (lights: 512 * 512
// levels, index y * 512 + x). It allocates nothing, so it needs every slot to hold data for at least one layer and the
// light map to exist; otherwise it changes nothing and returns 0. Slot is the game's SMixerSlot (mCount, mData,
// mTextures), slot index = ty * 64 + tx and sample index = sy * 9 + sx, as Unknown47eff0 reads them.
template <class Slot>
int CeGroundRewrite(Slot *slots, int count, uint8_t *lights)
{
	if (!slots || !lights || count != CE_GROUND_TILES * CE_GROUND_TILES) return 0;
	for (int s = 0; s < count; s++)
		if (slots[s].mCount < 1 || !slots[s].mData) return 0;
	for (int s = 0; s < count; s++) {
		Slot &slot = slots[s];
		const int tx = s % CE_GROUND_TILES, ty = s / CE_GROUND_TILES;
		slot.mCount = 1;
		slot.mTextures[0] = CE_GROUND_ROCK;
		for (int sy = 0; sy < 9; sy++)
			for (int sx = 0; sx < 9; sx++)
				slot.mData[sy * 9 + sx] = (uint8_t)(int8_t)CeGroundWeight(tx * 8 + sx, ty * 8 + sy);
	}
	for (int i = 0; i < 512 * 512; i++)
		lights[i] = (uint8_t)CeGroundLight(i % 512, i / 512);
	return 1;
}

#endif
