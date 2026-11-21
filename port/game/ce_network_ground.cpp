// SPDX-License-Identifier: GPL-3.0-only
// The Building Defence ground blend in a network co-op game (patch 0232; ce_network_ground.h says what and why).
// Integer arithmetic only: every peer computes the same weights whatever its compiler flags or graphics options.
#include "ce_network_ground.h"

namespace {

// A 32-bit integer hash of a lattice point, reduced to 0..255.
unsigned Hash(int x, int y, unsigned seed)
{
	uint32_t h = (uint32_t)x * 0x27d4eb2du + (uint32_t)y * 0x165667b1u + seed * 0x9e3779b9u;
	h ^= h >> 15;
	h *= 0x85ebca6bu;
	h ^= h >> 13;
	h *= 0xc2b2ae35u;
	h ^= h >> 16;
	return h & 255u;
}

// t in 0..cell to 0..256 along a smoothstep.
int Smooth(int t, int cell)
{
	const int u = t * 256 / cell;
	return (u * u * (768 - 2 * u)) >> 16;
}

// Value noise with cells of cell samples: 0..255.
int Noise(int x, int y, int cell, unsigned seed)
{
	const int ix = x / cell, iy = y / cell;
	const int fx = Smooth(x - ix * cell, cell), fy = Smooth(y - iy * cell, cell);
	const int a = (int)Hash(ix, iy, seed), b = (int)Hash(ix + 1, iy, seed);
	const int c = (int)Hash(ix, iy + 1, seed), d = (int)Hash(ix + 1, iy + 1, seed);
	const int top = a * 256 + (b - a) * fx, bottom = c * 256 + (d - c) * fx;
	return (top * 256 + (bottom - top) * fy) >> 16;
}

// Three octaves (cell size in samples, weight, seed): patches some five tiles across, broken up at two finer scales.
const struct { int cell, weight; unsigned seed; } kOctaves[] = { { 40, 4, 1 }, { 14, 2, 2 }, { 5, 1, 3 } };
const int kLow = -40, kHigh = 36;	// the rock layer's weight range: from none to about half the ground under rock
const int kLightLow = 28, kLightHigh = 33;	// light levels around 32, flat open ground's level on 850 and 200
bool waterHidden;

}

int CeGroundEligible(int sharing, uint32_t level, const int16_t *heights)
{
	if (!sharing || (level != 856 && level != 858) || !heights) return 0;
	for (int i = 0; i < CE_GROUND_TILES * CE_GROUND_TILES * CE_GROUND_SAMPLES; i++)
		if (heights[i]) return 0;
	return 1;
}

int CeGroundWeight(int x, int y)
{
	int sum = 0, total = 0;
	for (const auto &octave : kOctaves) {
		sum += Noise(x, y, octave.cell, octave.seed) * octave.weight;
		total += octave.weight;
	}
	return kLow + (sum / total) * (kHigh - kLow) / 255;
}

int CeGroundLight(int x, int y)
{
	const int n = (Noise(x, y, 96, 4) * 3 + Noise(x, y, 24, 5)) / 4;
	return kLightLow + n * (kLightHigh - kLightLow) / 255;
}

void CeGroundSetApplied(int applied)
{
	waterHidden = applied != 0;
}

float CeGroundWaterDrop()
{
	return waterHidden ? 1.0f : 0.0f;
}
